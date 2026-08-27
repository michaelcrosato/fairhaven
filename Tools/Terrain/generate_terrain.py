"""Generate Fairhaven's landscape heightmap, paint weights and world features.

Runs under plain CPython (numpy + Pillow). Produces everything the Unreal
content build needs, plus PNG previews so the terrain can be reviewed without
opening the editor.

    python Tools/Terrain/generate_terrain.py [--out DIR] [--seed N]

Outputs (default Tools/Terrain/Output):
    heightmap.r16         uint16 little-endian, row-major [Y][X], SIZE*SIZE
    weight_<Layer>.r8     uint8 landscape paint weight per layer
    world_features.json   town, roads, river, coast, ponds, biome anchors
    preview_relief.png    hillshaded relief with water and contours
    preview_biomes.png    dominant paint layer per texel
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import geom
import noise as ns
import world_config as C


def log(message: str) -> None:
    print("[terrain] " + message, flush=True)


def warped(edge0: float, edge1: float, coord, seed: int, amount: float):
    """Smoothstep whose boundary wanders, so region edges never read as a line."""
    jitter = ns.fbm(C.SIZE, C.SIZE, 2.4, seed, octaves=4) * np.float32(amount)
    return ns.smoothstep(edge0, edge1, coord + jitter)


# ---------------------------------------------------------------------------
# Height field
# ---------------------------------------------------------------------------
def build_height(seed: int):
    """Return (height_m, coast_y_per_column, context dict)."""
    size = C.SIZE
    axis = (np.arange(size, dtype=np.float32) - C.CENTER_INDEX) * np.float32(C.QUAD_UU)
    wx = axis[None, :]          # world X varies across columns (north is +X)
    wy = axis[:, None]          # world Y varies down rows      (east is +Y)

    # -- Coastline -----------------------------------------------------------
    coast_wiggle = ns.fbm(1, size, 2.6, seed + 17, octaves=4)[0, :] * 5200.0
    headland = 26000.0 * ns.smoothstep(34000.0, 96000.0, axis)
    southern_bay = -13000.0 * ns.smoothstep(-24000.0, -74000.0, axis)
    coast_y = (C.COAST_Y + coast_wiggle + headland + southern_bay).astype(np.float32)
    coast_row = coast_y[None, :]
    inland = (coast_row - wy).astype(np.float32)     # >0 on land, <0 at sea

    # -- Base land/sea profile ----------------------------------------------
    land_rise = (15.0 * ns.smoothstep(0.0, 13000.0, inland)
                 + 9.0 * ns.smoothstep(13000.0, 95000.0, inland))
    ocean_t = ns.smoothstep(0.0, C.OCEAN_FALLOFF, -inland)
    ocean_drop = C.OCEAN_FLOOR_M * np.power(ocean_t, 1.15, dtype=np.float32)
    height = (land_rise + ocean_drop).astype(np.float32)

    # -- Region masks (boundaries warped so nothing reads as a straight band) --
    mountain_mask = warped(C.MOUNTAIN_START_X, C.MOUNTAIN_FULL_X, wx, seed + 2401, 19000.0)
    tropics_mask = warped(C.TROPICS_START_X, C.TROPICS_FULL_X, wx, seed + 2411, 17000.0)
    farm_mask = warped(C.FARM_START_Y, C.FARM_FULL_Y, wy, seed + 2417, 15000.0)
    land_mask = ns.smoothstep(-1500.0, 3500.0, inland)
    land_mask = np.broadcast_to(land_mask, (size, size)).astype(np.float32)
    coastal_calm = ns.smoothstep(0.0, 20000.0, inland)

    # -- Broad relief so no part of the map is a featureless plain ------------
    lowland = land_mask * (1.0 - mountain_mask * 0.85) * coastal_calm
    height = height + 30.0 * ns.fbm(size, size, 2.2, seed + 2003, octaves=4) * lowland
    height = height + 15.0 * ns.fbm(size, size, 4.6, seed + 2011, octaves=4) * lowland

    # -- Mountains -----------------------------------------------------------
    ridge = ns.ridged(size, size, 3.4, seed + 101, octaves=6, persistence=0.52)
    ridge_detail = ns.fbm(size, size, 11.0, seed + 233, octaves=4)
    massif = ns.fbm(size, size, 1.7, seed + 331, octaves=3) * 0.5 + 0.5
    mountains = np.clip(ridge * 0.82 + massif * 0.18 + ridge_detail * 0.09, 0.0, 1.4)
    height = height + (C.MOUNTAIN_PEAK_M * np.power(mountain_mask, 1.45, dtype=np.float32)
                       * mountains * land_mask)

    # -- Foothills so the mountains do not start abruptly --------------------
    foothill = (warped(2000.0, 44000.0, wx, seed + 2423, 13000.0)
                * (1.0 - mountain_mask * 0.5)).astype(np.float32)
    height = height + 30.0 * foothill * (ns.fbm(size, size, 5.5, seed + 401, octaves=4) * 0.5 + 0.5) * land_mask

    # -- Rolling farmland ----------------------------------------------------
    height = height + 20.0 * farm_mask * ns.fbm(size, size, 6.5, seed + 503, octaves=4) * land_mask
    height = height + 8.0 * farm_mask * ns.fbm(size, size, 14.0, seed + 607, octaves=3) * land_mask

    # -- Tropical lowland: humid flats punctuated by steep karst knolls -------
    height = height * (1.0 - 0.30 * tropics_mask * ns.smoothstep(2.0, 40.0, height))
    height = height + 11.0 * tropics_mask * (ns.fbm(size, size, 9.0, seed + 709, octaves=4) * 0.5 + 0.5) * land_mask
    # Sparse clusters of steep knolls, not a uniform honeycomb: a low-frequency
    # cluster mask decides *where* karst appears, the ridged field decides shape.
    karst = ns.ridged(size, size, 9.0, seed + 2101, octaves=2, persistence=0.4)
    cluster = ns.smoothstep(0.20, 0.64, ns.fbm(size, size, 2.8, seed + 2131, octaves=3) * 0.5 + 0.5)
    karst_blobs = ns.smoothstep(0.80, 0.96, karst) * cluster
    height = height + 38.0 * tropics_mask * karst_blobs * land_mask
    ctx_karst = (tropics_mask * karst_blobs).astype(np.float32)

    # -- General detail ------------------------------------------------------
    detail_scale = (0.35 + 1.5 * mountain_mask + 0.5 * foothill)
    height = height + 9.0 * detail_scale * ns.fbm(size, size, 22.0, seed + 811, octaves=4) * land_mask
    height = height + 3.2 * ns.fbm(size, size, 48.0, seed + 907, octaves=3) * land_mask

    # -- Seafloor texture ----------------------------------------------------
    height = height + 5.0 * (1.0 - land_mask) * ns.fbm(size, size, 12.0, seed + 1009, octaves=4)

    # -- Beach flattening: calm the terrain either side of the waterline ------
    beach_t = 1.0 - ns.smoothstep(0.0, C.BEACH_WIDTH, np.abs(inland))
    height = height * (1.0 - 0.6 * beach_t)

    # -- Lagoon in the south -------------------------------------------------
    lagoon_d = np.hypot(wx - np.float32(C.LAGOON_CENTER[0]), wy - np.float32(C.LAGOON_CENTER[1]))
    lagoon_t = (1.0 - ns.smoothstep(C.LAGOON_RADIUS * 0.45, C.LAGOON_RADIUS, lagoon_d)) * tropics_mask
    lagoon_bed = C.LAGOON_DEPTH_M + 2.0 * ns.fbm(size, size, 20.0, seed + 1103, octaves=3)
    height = height * (1.0 - lagoon_t) + lagoon_bed * lagoon_t

    channel_pts = [(C.LAGOON_CENTER[0], C.LAGOON_CENTER[1], -3.0),
                   (C.LAGOON_CENTER[0] + 12000.0, C.LAGOON_CENTER[1] + 4000.0, -2.5),
                   (-38000.0, 22000.0, -2.0), (-30000.0, 30000.0, -2.0)]
    ch_d, ch_e = geom.polyline_field(wx, wy, channel_pts, attr_index=2)
    ch_t = (1.0 - ns.smoothstep(1400.0, 4200.0, ch_d)) * tropics_mask
    height = height * (1.0 - ch_t) + ch_e * ch_t

    ctx = {"wx": wx, "wy": wy, "inland": inland,
           "mountain_mask": mountain_mask, "tropics_mask": tropics_mask,
           "farm_mask": farm_mask, "land_mask": land_mask, "karst": ctx_karst}
    return height.astype(np.float32), coast_y, ctx


def carve_town(height, ctx, seed):
    """Flatten the town onto a gentle terrace so streets and buildings sit well."""
    wx, wy = ctx["wx"], ctx["wy"]
    d = np.hypot(wx - np.float32(C.TOWN_CENTER[0]), wy - np.float32(C.TOWN_CENTER[1]))
    t = 1.0 - ns.smoothstep(C.TOWN_RADIUS, C.TOWN_RADIUS + C.TOWN_FALLOFF, d)
    terrace = (C.TOWN_HEIGHT_M
               + 2.1 * ns.fbm(C.SIZE, C.SIZE, 7.0, seed + 1301, octaves=3)
               - 1.6 * ns.smoothstep(0.0, C.TOWN_RADIUS, d))
    strength = np.power(t, 0.75, dtype=np.float32)
    return (height * (1.0 - strength) + terrace * strength).astype(np.float32)


def carve_ponds(height, ctx, seed):
    """Sink shallow ponds into the farmland; returns (height, pond records)."""
    wx, wy = ctx["wx"], ctx["wy"]
    records = []
    for i, pond in enumerate(C.PONDS):
        cx, cy = pond["center"]
        local = geom.sample_bilinear(height, cx, cy, C.ORIGIN_UU, C.QUAD_UU)
        surface = local - 1.4
        bed = local - pond["depth_m"]
        d = np.hypot(wx - np.float32(cx), wy - np.float32(cy))
        wobble = ns.fbm(C.SIZE, C.SIZE, 26.0, seed + 3001 + i * 37, octaves=3) * (pond["radius"] * 0.18)
        t = 1.0 - ns.smoothstep(pond["radius"] * 0.35, pond["radius"], d + wobble)
        height = height * (1.0 - t) + bed * t
        records.append({"name": pond["name"], "center": [cx, cy],
                        "radius_uu": pond["radius"],
                        "water_level_m": round(float(surface), 2)})
    return height.astype(np.float32), records


def carve_river(height, ctx):
    """Cut a valley and channel along the river; returns (height, channel mask)."""
    wx, wy = ctx["wx"], ctx["wy"]
    d, elev = geom.polyline_field(wx, wy, C.RIVER_POINTS, attr_index=2)

    valley_t = (1.0 - ns.smoothstep(C.RIVER_VALLEY_WIDTH * 0.25,
                                    C.RIVER_VALLEY_WIDTH, d)) * 0.72
    valley_target = elev + 26.0 * ns.smoothstep(C.RIVER_CHANNEL_WIDTH, C.RIVER_VALLEY_WIDTH, d)
    height = height * (1.0 - valley_t) + np.minimum(height, valley_target) * valley_t

    channel_t = 1.0 - ns.smoothstep(C.RIVER_CHANNEL_WIDTH * 0.55, C.RIVER_CHANNEL_WIDTH * 1.7, d)
    bed = elev - 3.4 - 1.1 * (1.0 - ns.smoothstep(0.0, C.RIVER_CHANNEL_WIDTH, d))
    height = height * (1.0 - channel_t) + bed * channel_t
    return height.astype(np.float32), channel_t.astype(np.float32)


def carve_roads(height, ctx):
    """Cut road corridors that follow the land, and return their final splines."""
    wx, wy = ctx["wx"], ctx["wy"]
    road_mask = np.zeros(height.shape, dtype=np.float32)
    exported = []

    definitions = [(name, width, pts, False) for name, width, pts in C.ROADS]
    definitions += [("TownStreet%d" % i, C.TOWN_STREET_WIDTH, pts, True)
                    for i, pts in enumerate(C.TOWN_STREETS)]

    for name, width, points, is_street in definitions:
        dense = geom.densify(points, 700.0)
        raw = [geom.sample_bilinear(height, p[0], p[1], C.ORIGIN_UU, C.QUAD_UU) for p in dense]
        graded = geom.smooth_profile(raw, 5 if is_street else 13, passes=4)
        spline = [[p[0], p[1], e] for p, e in zip(dense, graded)]

        d, elev = geom.polyline_field(wx, wy, spline, attr_index=2)
        surface_t = 1.0 - ns.smoothstep(width * 0.55, width * 1.15, d)
        shoulder_t = 1.0 - ns.smoothstep(width, width * 4.2, d)

        blend = np.maximum(surface_t, shoulder_t * 0.62)
        height = height * (1.0 - blend) + elev * blend
        road_mask = np.maximum(road_mask, surface_t)

        exported.append({"name": name, "width_uu": width, "is_street": is_street,
                         "points": [[round(v, 1) for v in p] for p in spline]})

    return height.astype(np.float32), road_mask, exported


# ---------------------------------------------------------------------------
# Paint layers
# ---------------------------------------------------------------------------
def field_parcels(wx, wy, seed):
    """Rotated rectangular parcel grid: returns (crop_strength, hedgerow_mask)."""
    ang = C.FIELD_ANGLE
    u = (wx * math.cos(ang) - wy * math.sin(ang)) / C.FIELD_SIZE_U
    v = (wx * math.sin(ang) + wy * math.cos(ang)) / C.FIELD_SIZE_V
    ui = np.floor(u).astype(np.int64)
    vi = np.floor(v).astype(np.int64)

    # Cheap deterministic hash per parcel.
    h = (ui * np.int64(73856093)) ^ (vi * np.int64(19349663)) ^ np.int64(seed)
    h = (h ^ (h >> np.int64(13))) * np.int64(1274126177)
    parcel_rand = ((h >> np.int64(16)) & np.int64(1023)).astype(np.float32) / 1023.0

    fu = (u - ui).astype(np.float32)
    fv = (v - vi).astype(np.float32)
    edge = np.minimum(np.minimum(fu, 1.0 - fu), np.minimum(fv, 1.0 - fv))
    hedge = 1.0 - ns.smoothstep(C.FIELD_HEDGE * 0.5, C.FIELD_HEDGE, edge)

    crop = ns.smoothstep(0.46, 0.52, parcel_rand)      # ~50% of parcels are tilled
    return crop.astype(np.float32), hedge.astype(np.float32)


def build_weights(height, ctx, road_mask, river_mask, seed):
    size = C.SIZE
    wx, wy = ctx["wx"], ctx["wy"]
    inland = ctx["inland"]

    gy, gx = np.gradient(height, C.QUAD_UU / 100.0)
    slope = np.degrees(np.arctan(np.hypot(gx, gy))).astype(np.float32)

    variation = ns.fbm(size, size, 18.0, seed + 1511, octaves=4)
    fine = ns.fbm(size, size, 42.0, seed + 1613, octaves=3)

    town_d = np.hypot(wx - np.float32(C.TOWN_CENTER[0]), wy - np.float32(C.TOWN_CENTER[1]))
    town_t = 1.0 - ns.smoothstep(C.TOWN_RADIUS * 0.7, C.TOWN_RADIUS + C.TOWN_FALLOFF * 0.6, town_d)

    w = {}

    # Sand: beaches, shallow seabed, low river banks (not alpine gorges).
    beach = 1.0 - ns.smoothstep(0.0, C.BEACH_WIDTH * 0.85, np.abs(inland))
    shallow = (1.0 - ns.smoothstep(-9.0, -1.0, height)) * (inland < 0).astype(np.float32)
    sand = np.maximum(beach, shallow * 0.85)
    sand = np.maximum(sand, river_mask * 0.5 * (1.0 - ns.smoothstep(25.0, 70.0, height)))
    sand = sand * (1.0 - ns.smoothstep(18.0, 34.0, slope))
    w["Sand"] = sand

    # Rock: steep ground, exposed mountain faces, karst walls, deep seabed.
    rock = ns.smoothstep(17.0, 34.0, slope)
    rock = np.maximum(rock, ns.smoothstep(150.0, 260.0, height) * (0.45 + 0.4 * variation))
    rock = np.maximum(rock, ns.smoothstep(-20.0, -34.0, height))
    rock = np.maximum(rock, ctx["karst"] * ns.smoothstep(14.0, 28.0, slope))
    w["Rock"] = np.clip(rock, 0.0, 1.0)

    # Snow: high and not vertical, noisy snowline.
    snow = ns.smoothstep(0.0, 62.0, height - (C.SNOW_LINE_M + 26.0 * variation))
    w["Snow"] = snow * (1.0 - ns.smoothstep(38.0, 56.0, slope))

    # Farm: rectangular tilled parcels on gentle western ground.
    crop, hedge = field_parcels(wx, wy, seed)
    farm = ctx["farm_mask"] * (1.0 - ns.smoothstep(7.0, 16.0, slope))
    farm = farm * (1.0 - town_t) * ns.smoothstep(5.0, 14.0, height)
    farm = farm * crop * (1.0 - hedge) * (1.0 - ctx["tropics_mask"] * 0.8)
    w["Farm"] = np.clip(farm, 0.0, 1.0)

    # Jungle floor: dark humid soil in the south.
    jungle = ctx["tropics_mask"] * (1.0 - ns.smoothstep(30.0, 46.0, slope))
    jungle = jungle * ns.smoothstep(0.5, 7.0, height) * (0.7 + 0.3 * variation)
    w["Jungle"] = np.clip(jungle, 0.0, 1.0)

    # Dirt: roads and worn town ground only. A slope term was tried here and
    # removed: it turned every moderate hillside brown. Rock covers slopes.
    dirt = np.maximum(road_mask, town_t * (0.55 + 0.35 * fine))
    w["Dirt"] = np.clip(dirt, 0.0, 1.0)

    # Grass fills whatever is left on land.
    land = ns.smoothstep(-0.5, 2.5, height)
    w["Grass"] = np.clip(land * (0.85 + 0.15 * variation), 0.0, 1.0)

    order = ["Grass", "Jungle", "Farm", "Dirt", "Sand", "Rock", "Snow"]
    remaining = np.ones(height.shape, dtype=np.float32)
    resolved = {}
    for name in reversed(order):
        take = np.clip(w[name], 0.0, 1.0) * remaining
        resolved[name] = take
        remaining = np.clip(remaining - take, 0.0, 1.0)
    resolved["Grass"] = np.clip(resolved["Grass"] + remaining, 0.0, 1.0)

    total = np.zeros(height.shape, dtype=np.float32)
    for name in C.LAYERS:
        total = total + resolved[name]
    total = np.maximum(total, 1e-5)
    for name in C.LAYERS:
        resolved[name] = (resolved[name] / total).astype(np.float32)
    return resolved


# ---------------------------------------------------------------------------
# Previews
# ---------------------------------------------------------------------------
LAYER_PREVIEW_COLOUR = {
    "Sand":   (222, 205, 152),
    "Grass":  (104, 148, 74),
    "Farm":   (176, 148, 84),
    "Jungle": (48, 102, 56),
    "Dirt":   (139, 116, 89),
    "Rock":   (128, 126, 124),
    "Snow":   (238, 242, 248),
}


def write_previews(height, weights, out_dir, roads, ponds):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        log("Pillow not available; skipping previews")
        return

    step = max(1, C.SIZE // 1400)
    h = height[::step, ::step]

    gy, gx = np.gradient(h.astype(np.float32), C.QUAD_UU * step / 100.0)
    az, alt = math.radians(315.0), math.radians(45.0)
    slope = np.arctan(np.hypot(gx, gy))
    aspect = np.arctan2(-gx, gy)
    shade = np.clip(np.sin(alt) * np.cos(slope) + np.cos(alt) * np.sin(slope) * np.cos(az - aspect), 0.0, 1.0)

    land_col = np.stack([
        np.interp(h, [0, 25, 90, 200, 300, 380], [126, 118, 132, 150, 205, 250]),
        np.interp(h, [0, 25, 90, 200, 300, 380], [160, 152, 138, 140, 200, 250]),
        np.interp(h, [0, 25, 90, 200, 300, 380], [104, 96, 104, 132, 210, 255]),
    ], axis=-1)
    sea_col = np.stack([
        np.interp(h, [-46, -12, 0], [16, 34, 96]),
        np.interp(h, [-46, -12, 0], [52, 96, 158]),
        np.interp(h, [-46, -12, 0], [92, 140, 176]),
    ], axis=-1)
    rgb = np.where((h < 0.0)[..., None], sea_col, land_col * (0.42 + 0.58 * shade[..., None]))

    contour = (np.abs((h % 25.0) - 12.5) < 1.1) & (h > 0.5)
    rgb[contour] = rgb[contour] * 0.82

    img = Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB")
    draw = ImageDraw.Draw(img)

    def to_px(px, py):
        return ((px - C.ORIGIN_UU) / C.QUAD_UU / step, (py - C.ORIGIN_UU) / C.QUAD_UU / step)

    for road in roads:
        pts = [to_px(p[0], p[1]) for p in road["points"]]
        draw.line(pts, fill=(226, 196, 140), width=2 if road["is_street"] else 3)
    draw.line([to_px(p[0], p[1]) for p in C.RIVER_POINTS], fill=(90, 170, 220), width=3)
    for pond in ponds:
        px, py = to_px(pond["center"][0], pond["center"][1])
        pr = pond["radius_uu"] / C.QUAD_UU / step
        draw.ellipse([px - pr, py - pr, px + pr, py + pr], outline=(120, 190, 235), width=2)
    tc = to_px(C.TOWN_CENTER[0], C.TOWN_CENTER[1])
    tr = C.TOWN_RADIUS / C.QUAD_UU / step
    draw.ellipse([tc[0] - tr, tc[1] - tr, tc[0] + tr, tc[1] + tr], outline=(255, 90, 90), width=2)
    img.transpose(Image.Transpose.ROTATE_90).save(os.path.join(out_dir, "preview_relief.png"))

    stack = np.stack([weights[name][::step, ::step] for name in C.LAYERS], axis=0)
    dominant = np.argmax(stack, axis=0)
    biome = np.zeros(dominant.shape + (3,), dtype=np.uint8)
    for i, name in enumerate(C.LAYERS):
        biome[dominant == i] = LAYER_PREVIEW_COLOUR[name]
    biome[h < 0.0] = (44, 96, 150)
    Image.fromarray(biome, "RGB").transpose(Image.Transpose.ROTATE_90).save(
        os.path.join(out_dir, "preview_biomes.png"))
    log("previews written to " + out_dir)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="Generate the Fairhaven landscape.")
    parser.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "Output"))
    parser.add_argument("--seed", type=int, default=C.SEED)
    parser.add_argument("--no-previews", action="store_true")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    start = time.time()
    log("seed=%d size=%d extent=%.0f m from centre" % (args.seed, C.SIZE, C.EXTENT_UU / 100.0))

    height, coast_y, ctx = build_height(args.seed)
    log("base terrain built  min=%.1f m  max=%.1f m" % (height.min(), height.max()))

    height = carve_town(height, ctx, args.seed)
    height, ponds = carve_ponds(height, ctx, args.seed)
    height, river_mask = carve_river(height, ctx)
    log("town terrace, %d ponds and river carved" % len(ponds))

    height, road_mask, roads = carve_roads(height, ctx)
    log("%d road corridors carved" % len(roads))

    neighbours = (height
                  + np.roll(height, 1, 0) + np.roll(height, -1, 0)
                  + np.roll(height, 1, 1) + np.roll(height, -1, 1)) / 5.0
    height = (height * 0.35 + neighbours * 0.65).astype(np.float32)
    log("final terrain  min=%.1f m  max=%.1f m" % (height.min(), height.max()))

    weights = build_weights(height, ctx, road_mask, river_mask, args.seed)
    coverage = dict((name, float(weights[name].mean())) for name in C.LAYERS)
    log("layer coverage: " + ", ".join("%s=%.1f%%" % (k, v * 100) for k, v in coverage.items()))

    h16 = np.clip(C.metres_to_h16(height), 0, 65535).astype("<u2")
    h16.tofile(os.path.join(args.out, "heightmap.r16"))
    for name in C.LAYERS:
        np.clip(weights[name] * 255.0 + 0.5, 0, 255).astype(np.uint8).tofile(
            os.path.join(args.out, "weight_%s.r8" % name))
    log("wrote heightmap.r16 (%.1f MB) and %d weightmaps" % (h16.nbytes / 1048576.0, len(C.LAYERS)))

    coast_points = [[float((i - C.CENTER_INDEX) * C.QUAD_UU), float(coast_y[i])]
                    for i in range(0, C.SIZE, 24)]
    features = {
        "seed": args.seed,
        "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "landscape": {
            "size": C.SIZE, "quad_uu": C.QUAD_UU, "z_scale": C.Z_SCALE,
            "origin_uu": C.ORIGIN_UU, "extent_uu": C.EXTENT_UU,
            "quads_per_section": C.QUADS_PER_SECTION,
            "sections_per_component": C.SECTIONS_PER_COMPONENT,
            "component_count": C.COMPONENT_COUNT,
            "layers": C.LAYERS,
        },
        "sea_level_m": C.SEA_LEVEL_M,
        "height_range_m": [float(height.min()), float(height.max())],
        "town": {"center": list(C.TOWN_CENTER), "radius_uu": C.TOWN_RADIUS,
                 "height_m": C.TOWN_HEIGHT_M},
        "coast": coast_points,
        "river": {"width_uu": C.RIVER_CHANNEL_WIDTH,
                  "points": [[round(v, 1) for v in p] for p in geom.densify(C.RIVER_POINTS, 1200.0)]},
        "lagoon": {"center": list(C.LAGOON_CENTER), "radius_uu": C.LAGOON_RADIUS,
                   "depth_m": C.LAGOON_DEPTH_M},
        "ponds": ponds,
        "roads": roads,
        "layer_coverage": coverage,
    }
    with open(os.path.join(args.out, "world_features.json"), "w", encoding="utf-8") as handle:
        json.dump(features, handle, indent=1)
    log("wrote world_features.json")

    if not args.no_previews:
        write_previews(height, weights, args.out, roads, ponds)

    log("done in %.1fs -> %s" % (time.time() - start, args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
