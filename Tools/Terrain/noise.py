"""Deterministic vectorised noise primitives used by the Fairhaven terrain build.

Everything here is pure numpy and seeded, so a given seed always produces the
same world. No engine dependency: this module runs under plain CPython 3.12.
"""
from __future__ import annotations

import numpy as np


def _fade(t: np.ndarray) -> np.ndarray:
    """Perlin's quintic interpolant, 6t^5 - 15t^4 + 10t^3."""
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def perlin(height: int, width: int, cells_y: float, cells_x: float, seed: int) -> np.ndarray:
    """Tile-free 2D Perlin gradient noise sampled on a height x width grid.

    ``cells_y`` / ``cells_x`` give the number of lattice cells across each axis;
    larger values mean higher frequency. Returns float32 roughly in [-1, 1].
    """
    gy = int(np.ceil(cells_y)) + 2
    gx = int(np.ceil(cells_x)) + 2

    rng = np.random.default_rng(seed)
    angles = rng.uniform(0.0, 2.0 * np.pi, size=(gy, gx)).astype(np.float32)
    grad_x = np.cos(angles)
    grad_y = np.sin(angles)

    ys = np.linspace(0.0, cells_y, height, endpoint=False, dtype=np.float32)
    xs = np.linspace(0.0, cells_x, width, endpoint=False, dtype=np.float32)

    y0 = np.floor(ys).astype(np.int32)
    x0 = np.floor(xs).astype(np.int32)
    ty = (ys - y0)[:, None]
    tx = (xs - x0)[None, :]

    y0c = y0[:, None]
    x0c = x0[None, :]
    y1c = y0c + 1
    x1c = x0c + 1

    # Dot product of each corner gradient with the vector to the sample point.
    n00 = grad_x[y0c, x0c] * tx + grad_y[y0c, x0c] * ty
    n10 = grad_x[y0c, x1c] * (tx - 1.0) + grad_y[y0c, x1c] * ty
    n01 = grad_x[y1c, x0c] * tx + grad_y[y1c, x0c] * (ty - 1.0)
    n11 = grad_x[y1c, x1c] * (tx - 1.0) + grad_y[y1c, x1c] * (ty - 1.0)

    fx = _fade(tx)
    fy = _fade(ty)

    top = n00 + fx * (n10 - n00)
    bottom = n01 + fx * (n11 - n01)
    return (top + fy * (bottom - top)).astype(np.float32)


def fbm(height: int, width: int, cells: float, seed: int, octaves: int = 5,
        persistence: float = 0.5, lacunarity: float = 2.0) -> np.ndarray:
    """Fractal brownian motion built from stacked Perlin octaves. Range ~[-1, 1]."""
    total = np.zeros((height, width), dtype=np.float32)
    amplitude = 1.0
    frequency = float(cells)
    norm = 0.0
    for octave in range(octaves):
        total += amplitude * perlin(height, width, frequency, frequency, seed + octave * 7919)
        norm += amplitude
        amplitude *= persistence
        frequency *= lacunarity
    return total / max(norm, 1e-6)


def ridged(height: int, width: int, cells: float, seed: int, octaves: int = 5,
           persistence: float = 0.5, lacunarity: float = 2.0) -> np.ndarray:
    """Ridged multifractal noise. Range [0, 1], sharp crests, good for mountains."""
    total = np.zeros((height, width), dtype=np.float32)
    amplitude = 1.0
    frequency = float(cells)
    norm = 0.0
    for octave in range(octaves):
        layer = perlin(height, width, frequency, frequency, seed + octave * 6151)
        layer = 1.0 - np.abs(layer) * 2.0
        np.clip(layer, 0.0, 1.0, out=layer)
        layer *= layer
        total += amplitude * layer
        norm += amplitude
        amplitude *= persistence
        frequency *= lacunarity
    return (total / max(norm, 1e-6)).astype(np.float32)


def warped_fbm(height: int, width: int, cells: float, seed: int, warp_strength: float,
               octaves: int = 5) -> np.ndarray:
    """FBM whose sample position is displaced by another FBM.

    Domain warping removes the grid-aligned look that plain fbm has and is the
    cheapest way to make large landmasses read as organic.
    """
    base = fbm(height, width, cells, seed, octaves=octaves)
    warp = fbm(height, width, cells * 0.5, seed + 104729, octaves=3)
    # Cheap approximate warp: blend the base with a shifted copy of itself.
    shift = np.clip(warp * warp_strength, -1.0, 1.0)
    rolled = np.roll(base, int(max(1, height * 0.01)), axis=0)
    return (base * (1.0 - 0.35 * np.abs(shift)) + rolled * (0.35 * np.abs(shift))).astype(np.float32)


def smoothstep(edge0: float, edge1: float, values: np.ndarray) -> np.ndarray:
    """Hermite smoothstep, clamped. Returns float32 in [0, 1]."""
    if abs(edge1 - edge0) < 1e-9:
        return (values >= edge1).astype(np.float32)
    t = np.clip((values - edge0) / (edge1 - edge0), 0.0, 1.0).astype(np.float32)
    return t * t * (3.0 - 2.0 * t)


def remap(values: np.ndarray, lo: float, hi: float) -> np.ndarray:
    """Rescale an array to [lo, hi] based on its own min/max."""
    vmin = float(values.min())
    vmax = float(values.max())
    if vmax - vmin < 1e-9:
        return np.full_like(values, lo, dtype=np.float32)
    return (lo + (values - vmin) * (hi - lo) / (vmax - vmin)).astype(np.float32)
