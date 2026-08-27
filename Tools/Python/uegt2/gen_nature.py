"""Generated vegetation and rock meshes.

Scale reference: the player capsule is 1.8 m tall and the camera sits at 1.6 m,
so a 10 m oak is 1000 uu. Every function returns a MeshBuilder; meshbuild.py
turns them into assets.

Foliage meshes paint vertex alpha as the wind weight (0 at the trunk base,
approaching 1 at leaf tips) which the foliage material feeds into world position
offset. No animation assets are involved.
"""
from __future__ import annotations

import math

from . import palette as pal
from .meshkit import MeshBuilder, _SmallRng, add, rotate_z


# ---------------------------------------------------------------------------
# Trees
# ---------------------------------------------------------------------------
def broadleaf_tree(seed=1, height=1000.0, leaf=None, trunk=pal.TRUNK_BROWN,
                   blobs=4, lean=0.0):
    """Oak/maple style: tapered trunk, a few branches, clustered canopy blobs."""
    rng = _SmallRng(seed)
    leaf = leaf if leaf is not None else pal.LEAF_TEMPERATE
    mesh = MeshBuilder()

    trunk_h = height * 0.46
    base_r = height * 0.042
    top_r = base_r * 0.55

    # Trunk in two segments so it can lean without shearing.
    mesh.cylinder((0.0, 0.0, 0.0), base_r, trunk_h * 0.6, trunk, wind=0.0,
                  sides=6, top_radius=base_r * 0.78)
    mid = (lean * 0.4, 0.0, trunk_h * 0.6)
    mesh.cylinder(mid, base_r * 0.78, trunk_h * 0.4, trunk, wind=0.12,
                  sides=6, top_radius=top_r)

    crown = (lean, 0.0, trunk_h)
    canopy_r = height * 0.30

    # A few upward branches read as structure without costing many triangles.
    for i in range(3):
        angle = rng.uniform(0.0, 360.0)
        reach = canopy_r * rng.uniform(0.35, 0.6)
        branch_base = add(crown, (0.0, 0.0, -height * 0.05))
        mesh.frustum(branch_base,
                     (base_r * 0.55, base_r * 0.55),
                     (base_r * 0.20, base_r * 0.20),
                     height * 0.13, trunk, wind=0.25, yaw=angle,
                     top_offset=(reach, 0.0))

    # Canopy: overlapping faceted blobs, biggest at the centre.
    mesh.icosphere(add(crown, (0.0, 0.0, canopy_r * 0.55)), canopy_r, leaf,
                   wind=0.62, subdivisions=1, squash=0.82, jitter=0.16, seed=seed)
    for i in range(max(blobs - 1, 0)):
        angle = 360.0 * i / max(blobs - 1, 1) + rng.uniform(-30.0, 30.0)
        offset = rotate_z((canopy_r * rng.uniform(0.5, 0.78), 0.0, 0.0), angle)
        centre = add(crown, add(offset, (0.0, 0.0, canopy_r * rng.uniform(0.3, 0.95))))
        shade = leaf if rng.next() > 0.35 else pal.LEAF_DARK
        mesh.icosphere(centre, canopy_r * rng.uniform(0.52, 0.72), shade,
                       wind=0.85, subdivisions=0, squash=0.9, jitter=0.2,
                       seed=seed * 7 + i)
    return mesh


def conifer_tree(seed=1, height=1500.0, leaf=pal.LEAF_PINE, trunk=pal.TRUNK_DARK):
    """Pine/spruce: straight trunk with stacked cone skirts."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()

    base_r = height * 0.026
    mesh.cylinder((0.0, 0.0, 0.0), base_r, height * 0.95, trunk, wind=0.0,
                  sides=6, top_radius=base_r * 0.28)

    tiers = 5
    for i in range(tiers):
        t = i / float(tiers - 1)
        z = height * (0.17 + 0.70 * t)
        radius = height * (0.20 - 0.135 * t)
        tier_h = height * (0.26 - 0.10 * t)
        shade = leaf if i % 2 == 0 else pal.LEAF_DARK
        mesh.cone((0.0, 0.0, z), radius, tier_h, shade,
                  wind=0.25 + 0.6 * t, sides=7,
                  yaw=rng.uniform(0.0, 50.0))
    return mesh


def birch_tree(seed=1, height=900.0):
    """Slender pale trunk, sparse airy canopy."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    base_r = height * 0.022
    mesh.cylinder((0.0, 0.0, 0.0), base_r, height * 0.62, pal.TRUNK_PALE,
                  wind=0.0, sides=6, top_radius=base_r * 0.7)
    crown = (0.0, 0.0, height * 0.60)
    radius = height * 0.24
    for i in range(4):
        angle = 90.0 * i + rng.uniform(-25.0, 25.0)
        offset = rotate_z((radius * rng.uniform(0.3, 0.6), 0.0, 0.0), angle)
        centre = add(crown, add(offset, (0.0, 0.0, radius * rng.uniform(0.4, 1.1))))
        mesh.icosphere(centre, radius * rng.uniform(0.5, 0.68),
                       pal.LEAF_TEMPERATE_2 if i % 2 else pal.LEAF_TEMPERATE,
                       wind=0.9, subdivisions=0, squash=0.85, jitter=0.24,
                       seed=seed * 13 + i)
    return mesh


def palm_tree(seed=1, height=1100.0):
    """Curved trunk built from stacked segments, plus radiating frond planes."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()

    segments = 6
    lean_dir = rng.uniform(0.0, 360.0)
    pos = (0.0, 0.0, 0.0)
    radius = height * 0.028
    for i in range(segments):
        t = i / float(segments)
        seg_h = height * 0.72 / segments
        lean = rotate_z((height * 0.035 * t, 0.0, 0.0), lean_dir)
        mesh.cylinder(pos, radius * (1.0 - 0.4 * t), seg_h, pal.TRUNK_PALM,
                      wind=0.05 + 0.25 * t, sides=6,
                      top_radius=radius * (1.0 - 0.4 * (t + 1.0 / segments)))
        pos = add(pos, (lean[0], lean[1], seg_h))

    # Fronds: long tapered wedges drooping from the crown.
    crown = pos
    frond_count = 7
    for i in range(frond_count):
        angle = 360.0 * i / frond_count + rng.uniform(-12.0, 12.0)
        length = height * rng.uniform(0.34, 0.46)
        droop = -height * 0.16
        tip = add(crown, rotate_z((length, 0.0, droop), angle))
        mid = add(crown, rotate_z((length * 0.5, 0.0, height * 0.05), angle))
        half = height * 0.035
        left = rotate_z((0.0, half, 0.0), angle)
        right = rotate_z((0.0, -half, 0.0), angle)
        shade = pal.LEAF_PALM if i % 2 == 0 else pal.LEAF_JUNGLE_2
        mesh.add_triangle(add(crown, left), add(mid, left), tip, shade, 0.95)
        mesh.add_triangle(tip, add(mid, right), add(crown, right), shade, 0.95)
        mesh.add_triangle(tip, add(mid, left), add(mid, right), shade, 1.0)
    mesh.icosphere(crown, height * 0.045, pal.TRUNK_PALM, wind=0.3,
                   subdivisions=0, jitter=0.15, seed=seed)
    return mesh


def jungle_tree(seed=1, height=1700.0):
    """Tall buttressed trunk with a wide flat canopy."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    base_r = height * 0.036

    for i in range(4):
        angle = 90.0 * i + 45.0
        offset = rotate_z((base_r * 0.8, 0.0, 0.0), angle)
        mesh.frustum(offset, (base_r * 0.7, base_r * 0.5), (base_r * 0.2, base_r * 0.2),
                     height * 0.13, pal.TRUNK_DARK, wind=0.0, yaw=angle)

    mesh.cylinder((0.0, 0.0, 0.0), base_r, height * 0.74, pal.TRUNK_DARK,
                  wind=0.0, sides=7, top_radius=base_r * 0.55)

    crown = (0.0, 0.0, height * 0.72)
    radius = height * 0.30
    mesh.icosphere(add(crown, (0.0, 0.0, radius * 0.35)), radius,
                   pal.LEAF_JUNGLE, wind=0.55, subdivisions=1, squash=0.55,
                   jitter=0.18, seed=seed)
    for i in range(3):
        angle = 120.0 * i + rng.uniform(-30.0, 30.0)
        offset = rotate_z((radius * 0.7, 0.0, 0.0), angle)
        mesh.icosphere(add(crown, add(offset, (0.0, 0.0, radius * rng.uniform(0.2, 0.6)))),
                       radius * rng.uniform(0.45, 0.62), pal.LEAF_JUNGLE_2,
                       wind=0.85, subdivisions=0, squash=0.6, jitter=0.22,
                       seed=seed * 3 + i)
    return mesh


def dead_tree(seed=1, height=700.0):
    """Bare trunk and branches; good for the shoreline and high ground."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    base_r = height * 0.035
    mesh.cylinder((0.0, 0.0, 0.0), base_r, height * 0.8, pal.TRUNK_DARK,
                  wind=0.0, sides=5, top_radius=base_r * 0.3)
    for i in range(4):
        angle = rng.uniform(0.0, 360.0)
        z = height * rng.uniform(0.35, 0.7)
        reach = height * rng.uniform(0.16, 0.3)
        tip = rotate_z((reach, 0.0, height * 0.16), angle)
        mesh.frustum((0.0, 0.0, z), (base_r * 0.5, base_r * 0.5),
                     (base_r * 0.12, base_r * 0.12), reach, pal.TRUNK_DARK,
                     wind=0.25, yaw=angle, top_offset=(tip[0] * 0.6, 0.0))
    return mesh


# ---------------------------------------------------------------------------
# Undergrowth
# ---------------------------------------------------------------------------
def bush(seed=1, size=130.0, leaf=pal.BUSH_GREEN):
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    for i in range(3):
        angle = 120.0 * i + rng.uniform(-40.0, 40.0)
        offset = rotate_z((size * rng.uniform(0.0, 0.34), 0.0, 0.0), angle)
        centre = add(offset, (0.0, 0.0, size * rng.uniform(0.42, 0.62)))
        shade = leaf if i % 2 == 0 else pal.LEAF_DARK
        mesh.icosphere(centre, size * rng.uniform(0.42, 0.58), shade,
                       wind=0.7, subdivisions=0, squash=0.78, jitter=0.24,
                       seed=seed * 5 + i)
    return mesh


def fern(seed=1, size=90.0):
    """Radiating fronds for the jungle floor."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    for i in range(6):
        angle = 60.0 * i + rng.uniform(-15.0, 15.0)
        length = size * rng.uniform(0.8, 1.2)
        tip = rotate_z((length, 0.0, size * 0.45), angle)
        half = size * 0.13
        left = rotate_z((0.0, half, 0.0), angle)
        right = rotate_z((0.0, -half, 0.0), angle)
        base = (0.0, 0.0, size * 0.06)
        mesh.add_triangle(add(base, left), tip, add(base, right),
                          pal.LEAF_JUNGLE_2 if i % 2 else pal.LEAF_JUNGLE, 0.9)
        mesh.add_triangle(add(base, right), tip, add(base, left),
                          pal.LEAF_JUNGLE_2 if i % 2 else pal.LEAF_JUNGLE, 0.9)
    return mesh


def grass_clump(seed=1, height=44.0, colour=pal.GRASS_BLADE, blades=7,
                width_ratio=0.22, spread=0.55):
    """A tuft of tapered blades leaning in different directions."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    for i in range(blades):
        blade_h = height * rng.uniform(0.65, 1.3)
        blade_w = height * width_ratio * rng.uniform(0.7, 1.15)
        offset = rotate_z((height * rng.uniform(0.0, spread), 0.0, 0.0),
                          rng.uniform(0.0, 360.0))
        mesh.blade(offset, blade_w, blade_h, colour,
                   wind_top=1.0, yaw=rng.uniform(0.0, 360.0),
                   lean=blade_h * rng.uniform(-0.30, 0.30))
    return mesh


def reeds(seed=1, height=150.0):
    return grass_clump(seed=seed, height=height, colour=pal.REED_GREEN,
                       blades=9, width_ratio=0.10, spread=0.30)


def crop_row(seed=1, height=105.0, colour=pal.CROP_WHEAT):
    """Wheat or greens: taller, narrower and more upright than wild grass."""
    return grass_clump(seed=seed, height=height, colour=colour, blades=9,
                       width_ratio=0.11, spread=0.26)


# ---------------------------------------------------------------------------
# Rocks
# ---------------------------------------------------------------------------
def rock(seed=1, size=120.0, colour=pal.ROCK_GREY, squash=0.7):
    mesh = MeshBuilder()
    mesh.icosphere((0.0, 0.0, size * squash * 0.42), size, colour, wind=0.0,
                   subdivisions=0, squash=squash, jitter=0.34, seed=seed)
    return mesh


def boulder_cluster(seed=1, size=260.0):
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    mesh.icosphere((0.0, 0.0, size * 0.34), size, pal.ROCK_GREY, wind=0.0,
                   subdivisions=1, squash=0.66, jitter=0.3, seed=seed)
    for i in range(2):
        angle = rng.uniform(0.0, 360.0)
        offset = rotate_z((size * rng.uniform(0.7, 1.0), 0.0, 0.0), angle)
        mesh.icosphere(add(offset, (0.0, 0.0, size * 0.2)),
                       size * rng.uniform(0.4, 0.6),
                       pal.ROCK_DARK if i else pal.ROCK_WARM,
                       wind=0.0, subdivisions=0, squash=0.7, jitter=0.32,
                       seed=seed * 11 + i)
    return mesh


def cliff_slab(seed=1, size=420.0):
    """Angular outcrop for mountain slopes and the headland."""
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    for i in range(3):
        angle = rng.uniform(0.0, 360.0)
        offset = rotate_z((size * rng.uniform(0.0, 0.35), 0.0, 0.0), angle)
        height = size * rng.uniform(0.9, 1.5)
        mesh.frustum(offset, (size * rng.uniform(0.7, 1.0), size * rng.uniform(0.6, 0.9)),
                     (size * rng.uniform(0.3, 0.6), size * rng.uniform(0.3, 0.5)),
                     height, pal.CLIFF_GREY if i % 2 == 0 else pal.ROCK_DARK,
                     wind=0.0, yaw=angle,
                     top_offset=(size * rng.uniform(-0.2, 0.2), size * rng.uniform(-0.2, 0.2)))
    return mesh


def driftwood(seed=1, size=200.0):
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, size * 0.12), size * 0.12, size, pal.TRUNK_PALE,
                  wind=0.0, sides=5, top_radius=size * 0.08, yaw=rng.uniform(0, 60))
    return mesh
