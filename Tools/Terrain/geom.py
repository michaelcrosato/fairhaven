"""Polyline helpers shared by terrain carving and world feature export."""
from __future__ import annotations

import math
from typing import Sequence

import numpy as np


def densify(points: Sequence[Sequence[float]], step: float) -> list[list[float]]:
    """Subdivide a polyline so consecutive points are at most ``step`` apart.

    Extra attributes beyond (x, y) are linearly interpolated.
    """
    if len(points) < 2:
        return [list(p) for p in points]
    out: list[list[float]] = [list(points[0])]
    for a, b in zip(points, points[1:]):
        ax, ay = a[0], a[1]
        bx, by = b[0], b[1]
        length = math.hypot(bx - ax, by - ay)
        count = max(1, int(math.ceil(length / step)))
        for i in range(1, count + 1):
            t = i / count
            out.append([a[k] + (b[k] - a[k]) * t for k in range(len(a))])
    return out


def smooth_profile(values: Sequence[float], window: int, passes: int = 3) -> list[float]:
    """Moving-average smoothing with clamped edges; used for road grades."""
    data = list(values)
    if window < 2 or len(data) < 3:
        return data
    half = window // 2
    for _ in range(passes):
        padded = [data[0]] * half + data + [data[-1]] * half
        data = [sum(padded[i:i + 2 * half + 1]) / (2 * half + 1) for i in range(len(data))]
    return data


def polyline_field(wx: np.ndarray, wy: np.ndarray, points: Sequence[Sequence[float]],
                   attr_index: int | None = None,
                   margin: float = 0.0) -> tuple[np.ndarray, np.ndarray | None]:
    """Distance from every grid sample to a polyline.

    ``wx`` broadcasts along columns, ``wy`` along rows. Returns the minimum
    distance and, when ``attr_index`` is given, the value of that attribute at
    the nearest point on the polyline.

    ``margin`` restricts each segment to its own bounding box grown by that many
    world units; samples outside every box keep distance 1e12. This matters a
    lot: the naive form touches the whole grid once per segment, which at
    4033x4033 is 16 million samples times roughly 1500 segments, and it
    allocates six full-size temporaries each time. Every caller smoothsteps its
    influence to zero well inside the margin it passes, so the result is
    identical where it is used. Pass 0.0 for the exact full-grid behaviour.
    """
    shape = np.broadcast_shapes(wx.shape, wy.shape)
    best_d = np.full(shape, np.float32(1e12), dtype=np.float32)
    best_attr = np.zeros(shape, dtype=np.float32) if attr_index is not None else None

    rows, cols = shape
    xs = np.asarray(wx).reshape(-1)
    ys = np.asarray(wy).reshape(-1)
    windowed = margin > 0.0 and xs.size > 1 and ys.size > 1
    if windowed:
        x_origin, x_step = float(xs[0]), float(xs[1] - xs[0])
        y_origin, y_step = float(ys[0]), float(ys[1] - ys[0])
        windowed = abs(x_step) > 1e-9 and abs(y_step) > 1e-9

    for a, b in zip(points, points[1:]):
        ax, ay = np.float32(a[0]), np.float32(a[1])
        dx, dy = np.float32(b[0] - a[0]), np.float32(b[1] - a[1])
        length_sq = float(dx * dx + dy * dy)
        if length_sq < 1e-6:
            continue

        if windowed:
            c0 = int(math.floor((min(a[0], b[0]) - margin - x_origin) / x_step))
            c1 = int(math.ceil((max(a[0], b[0]) + margin - x_origin) / x_step)) + 1
            r0 = int(math.floor((min(a[1], b[1]) - margin - y_origin) / y_step))
            r1 = int(math.ceil((max(a[1], b[1]) + margin - y_origin) / y_step)) + 1
            c0, c1 = max(c0, 0), min(c1, cols)
            r0, r1 = max(r0, 0), min(r1, rows)
            if c0 >= c1 or r0 >= r1:
                continue
        else:
            r0, r1, c0, c1 = 0, rows, 0, cols

        sub_x = wx[:, c0:c1] if wx.shape[-1] > 1 else wx
        sub_y = wy[r0:r1, :] if wy.shape[0] > 1 else wy

        t = ((sub_x - ax) * dx + (sub_y - ay) * dy) / np.float32(length_sq)
        np.clip(t, 0.0, 1.0, out=t)
        px = ax + t * dx
        py = ay + t * dy
        d = np.hypot(sub_x - px, sub_y - py).astype(np.float32)

        view_d = best_d[r0:r1, c0:c1]
        closer = d < view_d
        if best_attr is not None:
            attr = np.float32(a[attr_index]) + t * np.float32(b[attr_index] - a[attr_index])
            np.copyto(best_attr[r0:r1, c0:c1], attr, where=closer)
        np.copyto(view_d, d, where=closer)

    return best_d, best_attr


def sample_bilinear(grid: np.ndarray, wx: float, wy: float, origin: float, quad: float) -> float:
    """Bilinearly sample a [row=Y, col=X] grid at a world position."""
    size_y, size_x = grid.shape
    fx = (wx - origin) / quad
    fy = (wy - origin) / quad
    fx = min(max(fx, 0.0), size_x - 1.001)
    fy = min(max(fy, 0.0), size_y - 1.001)
    x0, y0 = int(fx), int(fy)
    tx, ty = fx - x0, fy - y0
    v00 = float(grid[y0, x0])
    v10 = float(grid[y0, x0 + 1])
    v01 = float(grid[y0 + 1, x0])
    v11 = float(grid[y0 + 1, x0 + 1])
    return (v00 * (1 - tx) + v10 * tx) * (1 - ty) + (v01 * (1 - tx) + v11 * tx) * ty
