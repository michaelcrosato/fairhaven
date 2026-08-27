"""Generated buildings, props, boats and characters.

Everything is built from the meshkit primitives with vertex colours from
palette.py, so the whole town shares one opaque material. Scale reference: the
player is 1.8 m (180 uu) tall, so a single-storey wall is about 320 uu.
"""
from __future__ import annotations

import math

from . import palette as pal
from .meshkit import MeshBuilder, _SmallRng, add, rotate_z


# ---------------------------------------------------------------------------
# Building pieces
# ---------------------------------------------------------------------------
def _windows_on_wall(mesh, centre, wall_width, wall_height, axis, count, seed,
                     sill_z, pane=pal.WINDOW_GLASS, frame=pal.WINDOW_FRAME,
                     depth=14.0):
    """Punch a row of framed windows onto one wall face.

    ``axis`` is 'x' for a wall facing +/-X, 'y' for +/-Y. Windows are proud of
    the wall rather than recessed: cheaper, and it reads well at low poly.
    """
    if count <= 0:
        return
    rng = _SmallRng(seed)
    win_w = min(wall_width / (count * 2.1), 110.0)
    win_h = min(wall_height * 0.36, 140.0)
    spacing = wall_width / (count + 1.0)

    for i in range(count):
        offset = -wall_width * 0.5 + spacing * (i + 1)
        if axis == "x":
            frame_pos = add(centre, (0.0, offset, sill_z + win_h * 0.5))
            frame_size = (depth, win_w + 22.0, win_h + 22.0)
            pane_pos = add(centre, (depth * 0.35, offset, sill_z + win_h * 0.5))
            pane_size = (depth * 0.7, win_w, win_h)
        else:
            frame_pos = add(centre, (offset, 0.0, sill_z + win_h * 0.5))
            frame_size = (win_w + 22.0, depth, win_h + 22.0)
            pane_pos = add(centre, (offset, depth * 0.35, sill_z + win_h * 0.5))
            pane_size = (win_w, depth * 0.7, win_h)

        mesh.box(frame_pos, frame_size, frame)
        mesh.box(pane_pos, pane_size, pane)


def _door(mesh, centre, axis, colour, width=90.0, height=200.0, depth=16.0):
    if axis == "x":
        mesh.box(add(centre, (0.0, 0.0, height * 0.5)), (depth, width + 20.0, height + 14.0), pal.TRIM_WHITE)
        mesh.box(add(centre, (depth * 0.4, 0.0, height * 0.5)), (depth * 0.7, width, height), colour)
    else:
        mesh.box(add(centre, (0.0, 0.0, height * 0.5)), (width + 20.0, depth, height + 14.0), pal.TRIM_WHITE)
        mesh.box(add(centre, (0.0, depth * 0.4, height * 0.5)), (width, depth * 0.7, height), colour)


def house(seed=1, width=760.0, depth=580.0, storeys=1, wall=None, roof=None,
          door=None, chimney=True, porch=False):
    """The workhorse town building: box walls, gable roof, door and windows."""
    rng = _SmallRng(seed)
    wall = wall if wall is not None else rng_choice(rng, [
        pal.WALL_CREAM, pal.WALL_WHITE, pal.WALL_OCHRE, pal.WALL_SAGE,
        pal.WALL_BLUE, pal.WALL_TERRACOTTA])
    roof = roof if roof is not None else rng_choice(rng, [
        pal.ROOF_RED, pal.ROOF_SLATE, pal.ROOF_BROWN, pal.ROOF_TEAL])
    door = door if door is not None else rng_choice(rng, [
        pal.DOOR_BLUE, pal.DOOR_RED, pal.DOOR_GREEN, pal.DOOR_WOOD])

    mesh = MeshBuilder()
    wall_h = 320.0 * storeys
    plinth_h = 26.0

    # Stone plinth grounds the building on uneven terrain.
    mesh.box((0.0, 0.0, plinth_h * 0.5), (width + 30.0, depth + 30.0, plinth_h), pal.WALL_STONE)
    mesh.box((0.0, 0.0, plinth_h + wall_h * 0.5), (width, depth, wall_h), wall)

    # Gable roof ridged along X, overhanging the walls.
    roof_h = 150.0 + 60.0 * storeys
    mesh.prism((0.0, 0.0, plinth_h + wall_h), (width + 70.0, depth + 70.0, roof_h), roof)

    # Door on the -Y face, windows on -Y and both ends.
    _door(mesh, (0.0, -depth * 0.5, plinth_h), "y", door)
    _windows_on_wall(mesh, (0.0, -depth * 0.5, plinth_h), width, wall_h, "y",
                     2, seed, sill_z=wall_h * 0.34)
    _windows_on_wall(mesh, (0.0, depth * 0.5, plinth_h), width, wall_h, "y",
                     2, seed + 1, sill_z=wall_h * 0.34)
    _windows_on_wall(mesh, (width * 0.5, 0.0, plinth_h), depth, wall_h, "x",
                     1, seed + 2, sill_z=wall_h * 0.34)
    if storeys > 1:
        _windows_on_wall(mesh, (0.0, -depth * 0.5, plinth_h), width, wall_h, "y",
                         2, seed + 3, sill_z=wall_h * 0.68)
        _windows_on_wall(mesh, (0.0, depth * 0.5, plinth_h), width, wall_h, "y",
                         2, seed + 4, sill_z=wall_h * 0.68)

    if chimney:
        cx = width * rng.uniform(0.2, 0.34)
        mesh.box((cx, depth * 0.16, plinth_h + wall_h + roof_h * 0.66),
                 (90.0, 90.0, roof_h * 1.35), pal.BRICK_RED)
        mesh.box((cx, depth * 0.16, plinth_h + wall_h + roof_h * 1.34),
                 (110.0, 110.0, 26.0), pal.STONE_DARK)

    if porch:
        porch_d = 150.0
        mesh.box((0.0, -depth * 0.5 - porch_d * 0.5, plinth_h + 8.0),
                 (width * 0.55, porch_d, 16.0), pal.WOOD_PLANK)
        for sx in (-1.0, 1.0):
            mesh.box((sx * width * 0.24, -depth * 0.5 - porch_d * 0.85, plinth_h + 120.0),
                     (28.0, 28.0, 230.0), pal.WOOD_DARK)
        mesh.box((0.0, -depth * 0.5 - porch_d * 0.5, plinth_h + 245.0),
                 (width * 0.6, porch_d + 40.0, 22.0), roof)
    return mesh


def rng_choice(rng, items):
    return items[int(rng.next() * len(items)) % len(items)]


def barn(seed=1, width=1150.0, depth=820.0):
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    wall_h = 430.0
    mesh.box((0.0, 0.0, 14.0), (width + 30.0, depth + 30.0, 28.0), pal.STONE_DARK)
    mesh.box((0.0, 0.0, 28.0 + wall_h * 0.5), (width, depth, wall_h), pal.WALL_RED)

    # Gambrel-ish roof: two stacked prisms give the barn silhouette.
    mesh.prism((0.0, 0.0, 28.0 + wall_h), (width + 60.0, depth + 60.0, 170.0), pal.ROOF_BROWN)
    mesh.prism((0.0, 0.0, 28.0 + wall_h + 150.0), (width + 20.0, depth * 0.62, 230.0), pal.ROOF_BROWN)

    # Big sliding doors and white trim boards.
    mesh.box((0.0, -depth * 0.5 - 8.0, 28.0 + 180.0), (420.0, 22.0, 360.0), pal.WOOD_DARK)
    mesh.box((0.0, -depth * 0.5 - 14.0, 28.0 + 180.0), (24.0, 14.0, 360.0), pal.TRIM_WHITE)
    for sx in (-1.0, 1.0):
        mesh.box((sx * width * 0.42, -depth * 0.5 - 6.0, 28.0 + wall_h * 0.5),
                 (26.0, 14.0, wall_h), pal.TRIM_WHITE)
    _windows_on_wall(mesh, (width * 0.5, 0.0, 28.0), depth, wall_h, "x", 2,
                     seed, sill_z=wall_h * 0.55)
    return mesh


def church(seed=1):
    mesh = MeshBuilder()
    nave_w, nave_d, wall_h = 700.0, 1350.0, 520.0
    mesh.box((0.0, 0.0, 20.0), (nave_w + 40.0, nave_d + 40.0, 40.0), pal.WALL_STONE)
    mesh.box((0.0, 0.0, 40.0 + wall_h * 0.5), (nave_w, nave_d, wall_h), pal.WALL_STONE)
    mesh.prism((0.0, 0.0, 40.0 + wall_h), (nave_w + 60.0, nave_d + 60.0, 300.0), pal.ROOF_SLATE)

    # Tower at the -Y end with a spire.
    tower = 380.0
    tower_h = 900.0
    ty = -nave_d * 0.5 - tower * 0.35
    mesh.box((0.0, ty, 40.0 + tower_h * 0.5), (tower, tower, tower_h), pal.WALL_STONE)
    mesh.box((0.0, ty, 40.0 + tower_h + 24.0), (tower + 60.0, tower + 60.0, 48.0), pal.STONE_DARK)
    mesh.cone((0.0, ty, 40.0 + tower_h + 48.0), tower * 0.72, 460.0, pal.ROOF_TEAL, sides=4, yaw=45.0)
    mesh.box((0.0, ty, 40.0 + tower_h + 540.0), (24.0, 24.0, 90.0), pal.METAL_COPPER)

    _door(mesh, (0.0, ty - tower * 0.5, 40.0), "y", pal.DOOR_WOOD, width=140.0, height=260.0)
    for side in (-1.0, 1.0):
        _windows_on_wall(mesh, (side * nave_w * 0.5, 0.0, 40.0), nave_d, wall_h, "x",
                         4, seed, sill_z=wall_h * 0.36, pane=pal.LAMP_GLASS)
    _windows_on_wall(mesh, (0.0, ty, 40.0 + tower_h * 0.62), tower, 200.0, "y",
                     1, seed + 9, sill_z=0.0, pane=pal.LAMP_GLASS)
    return mesh


def lighthouse(seed=1):
    """The coastal landmark: visible from most of the map."""
    mesh = MeshBuilder()
    base_r, height = 210.0, 1500.0
    mesh.cylinder((0.0, 0.0, 0.0), base_r + 60.0, 70.0, pal.WALL_STONE, sides=10)

    # Alternating bands read as the classic painted tower.
    bands = 6
    for i in range(bands):
        t0 = i / float(bands)
        t1 = (i + 1) / float(bands)
        r0 = base_r * (1.0 - 0.42 * t0)
        r1 = base_r * (1.0 - 0.42 * t1)
        mesh.cylinder((0.0, 0.0, 70.0 + height * t0), r0, height * (t1 - t0),
                      pal.WALL_WHITE if i % 2 == 0 else pal.CLOTH_RED,
                      sides=10, top_radius=r1)

    top_r = base_r * 0.58
    mesh.cylinder((0.0, 0.0, 70.0 + height), top_r + 40.0, 34.0, pal.METAL_IRON, sides=10)
    mesh.cylinder((0.0, 0.0, 70.0 + height + 34.0), top_r, 190.0, pal.LAMP_GLASS, sides=8)
    mesh.cone((0.0, 0.0, 70.0 + height + 224.0), top_r + 30.0, 150.0, pal.ROOF_SLATE, sides=8)
    _door(mesh, (0.0, -base_r * 0.98, 70.0), "y", pal.DOOR_RED)
    return mesh


def lighthouse_glow(seed=1):
    """Emissive lamp room, placed on the lighthouse transform."""
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 70.0 + 1500.0 + 40.0), 210.0 * 0.5, 176.0,
                  pal.LAMP_GLASS, sides=8)
    return mesh


def windmill(seed=1):
    mesh = MeshBuilder()
    base_r, height = 260.0, 820.0
    mesh.cylinder((0.0, 0.0, 0.0), base_r + 40.0, 50.0, pal.WALL_STONE, sides=8)
    mesh.cylinder((0.0, 0.0, 50.0), base_r, height, pal.WALL_CREAM, sides=8,
                  top_radius=base_r * 0.62)
    mesh.cone((0.0, 0.0, 50.0 + height), base_r * 0.74, 220.0, pal.ROOF_BROWN, sides=8)
    _door(mesh, (0.0, -base_r * 0.95, 50.0), "y", pal.DOOR_WOOD)

    # Sails on the -Y face, static: motion would need an animated component and
    # this milestone deliberately avoids bespoke animation.
    hub = (0.0, -base_r * 0.72, 50.0 + height * 0.92)
    mesh.cylinder(add(hub, (0.0, -30.0, 0.0)), 34.0, 60.0, pal.METAL_IRON, sides=6)
    for i in range(4):
        angle = 90.0 * i + 20.0
        length = 420.0
        rad = math.radians(angle)
        tip = (math.cos(rad) * length, 0.0, math.sin(rad) * length)
        mid = (tip[0] * 0.5, -46.0, tip[2] * 0.5)
        mesh.box(add(hub, mid), (length, 20.0, 46.0), pal.WOOD_DARK,
                 pitch=-angle)
        mesh.box(add(hub, (tip[0] * 0.62, -58.0, tip[2] * 0.62)),
                 (length * 0.5, 12.0, 130.0), pal.CANVAS_WHITE, pitch=-angle)
    return mesh


def warehouse(seed=1, width=1000.0, depth=700.0):
    mesh = MeshBuilder()
    wall_h = 420.0
    mesh.box((0.0, 0.0, 16.0), (width + 24.0, depth + 24.0, 32.0), pal.STONE_DARK)
    mesh.box((0.0, 0.0, 32.0 + wall_h * 0.5), (width, depth, wall_h), pal.WALL_TIMBER)
    mesh.prism((0.0, 0.0, 32.0 + wall_h), (width + 50.0, depth + 50.0, 190.0), pal.ROOF_SLATE)
    mesh.box((0.0, -depth * 0.5 - 8.0, 32.0 + 170.0), (380.0, 20.0, 340.0), pal.WOOD_DARK)
    _windows_on_wall(mesh, (0.0, depth * 0.5, 32.0), width, wall_h, "y", 3, seed,
                     sill_z=wall_h * 0.5)
    return mesh


def shed(seed=1, width=380.0, depth=320.0):
    mesh = MeshBuilder()
    wall_h = 250.0
    mesh.box((0.0, 0.0, wall_h * 0.5), (width, depth, wall_h), pal.WOOD_PLANK)
    # Single-pitch roof: a thin slab tilted about its own centre.
    mesh.box((0.0, 0.0, wall_h + 40.0), (width + 60.0, depth + 60.0, 26.0),
             pal.ROOF_BROWN, roll=-12.0)
    _door(mesh, (0.0, -depth * 0.5, 0.0), "y", pal.DOOR_WOOD, width=110.0, height=180.0)
    return mesh


def market_stall(seed=1):
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    w, d, h = 420.0, 300.0, 230.0
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((sx * w * 0.45, sy * d * 0.45, h * 0.5), (24.0, 24.0, h), pal.WOOD_DARK)
    mesh.box((0.0, 0.0, h + 10.0), (w + 60.0, d + 70.0, 20.0),
             rng_choice(rng, [pal.CLOTH_RED, pal.CLOTH_BLUE, pal.CLOTH_GREEN, pal.CLOTH_YELLOW]),
             roll=8.0)
    mesh.box((0.0, 0.0, h * 0.62), (w, d * 0.8, 22.0), pal.WOOD_PLANK)
    for i in range(3):
        mesh.box((-w * 0.3 + i * w * 0.3, 0.0, h * 0.62 + 30.0),
                 (70.0, 70.0, 44.0),
                 rng_choice(rng, [pal.CROP_WHEAT, pal.CLOTH_GREEN, pal.WALL_TERRACOTTA]))
    return mesh


def door_leaf(seed=1, width=92.0, height=200.0, thickness=16.0,
              colour=pal.DOOR_BLUE):
    """A door leaf hinged at its own origin, so rotating the actor swings it.

    The mesh extends along +X from x=0, which is the hinge edge.
    """
    mesh = MeshBuilder()
    mesh.box((width * 0.5, 0.0, height * 0.5), (width, thickness, height), colour)
    mesh.box((width * 0.5, 0.0, height * 0.5),
             (width * 0.78, thickness * 1.25, height * 0.42), pal.WOOD_DARK)
    # Handle on the free edge.
    mesh.box((width * 0.82, thickness * 0.7, height * 0.52),
             (16.0, 12.0, 12.0), pal.METAL_COPPER)
    return mesh


# ---------------------------------------------------------------------------
# Small props
# ---------------------------------------------------------------------------
def fence_section(seed=1, length=320.0):
    mesh = MeshBuilder()
    mesh.box((-length * 0.5, 0.0, 70.0), (18.0, 18.0, 140.0), pal.WOOD_DARK)
    mesh.box((length * 0.5, 0.0, 70.0), (18.0, 18.0, 140.0), pal.WOOD_DARK)
    for z in (56.0, 108.0):
        mesh.box((0.0, 0.0, z), (length, 12.0, 20.0), pal.WOOD_PLANK)
    return mesh


def stone_wall_section(seed=1, length=300.0):
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    courses = 3
    for c in range(courses):
        z = 26.0 + c * 44.0
        offset = rng.uniform(-14.0, 14.0)
        mesh.box((offset, 0.0, z), (length - abs(offset), 78.0 - c * 8.0, 44.0),
                 pal.STONE_PALE if c % 2 == 0 else pal.STONE_DARK)
    return mesh


def lamp_post(seed=1, height=440.0):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 34.0, 26.0, pal.STONE_DARK, sides=8)
    mesh.cylinder((0.0, 0.0, 26.0), 15.0, height, pal.METAL_IRON, sides=6,
                  top_radius=11.0)
    mesh.box((0.0, 0.0, 26.0 + height + 34.0), (86.0, 86.0, 74.0), pal.METAL_IRON)
    mesh.cone((0.0, 0.0, 26.0 + height + 71.0), 58.0, 52.0, pal.METAL_IRON, sides=4, yaw=45.0)
    return mesh


def lamp_glow(seed=1, height=440.0):
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, 26.0 + height + 34.0), (66.0, 66.0, 58.0), pal.LAMP_GLASS)
    return mesh


def sign_post(seed=1):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 16.0, 250.0, pal.WOOD_DARK, sides=6)
    mesh.box((0.0, 0.0, 232.0), (30.0, 230.0, 90.0), pal.WOOD_PLANK)
    mesh.box((0.0, 0.0, 232.0), (34.0, 210.0, 70.0), pal.PAPER_CREAM)
    return mesh


def bench(seed=1):
    mesh = MeshBuilder()
    for sx in (-1.0, 1.0):
        mesh.box((sx * 90.0, 0.0, 25.0), (22.0, 130.0, 50.0), pal.STONE_DARK)
    mesh.box((0.0, 0.0, 58.0), (240.0, 140.0, 18.0), pal.WOOD_PLANK)
    mesh.box((0.0, 60.0, 110.0), (240.0, 18.0, 90.0), pal.WOOD_PLANK)
    return mesh


def crate(seed=1, size=90.0):
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, size * 0.5), (size, size, size), pal.WOOD_PLANK)
    for axis in range(2):
        for sign in (-1.0, 1.0):
            if axis == 0:
                mesh.box((sign * size * 0.5, 0.0, size * 0.5), (4.0, size, 14.0), pal.WOOD_DARK)
            else:
                mesh.box((0.0, sign * size * 0.5, size * 0.5), (size, 4.0, 14.0), pal.WOOD_DARK)
    return mesh


def barrel(seed=1, radius=52.0, height=120.0):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), radius * 0.86, height * 0.2, pal.WOOD_DARK,
                  sides=8, top_radius=radius)
    mesh.cylinder((0.0, 0.0, height * 0.2), radius, height * 0.6, pal.WOOD_PLANK, sides=8)
    mesh.cylinder((0.0, 0.0, height * 0.8), radius, height * 0.2, pal.WOOD_DARK,
                  sides=8, top_radius=radius * 0.86)
    for z in (height * 0.28, height * 0.66):
        mesh.cylinder((0.0, 0.0, z), radius + 4.0, 12.0, pal.METAL_IRON, sides=8)
    return mesh


def cart(seed=1):
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, 96.0), (280.0, 170.0, 20.0), pal.WOOD_PLANK)
    for sy in (-1.0, 1.0):
        mesh.box((0.0, sy * 85.0, 140.0), (280.0, 16.0, 80.0), pal.WOOD_PLANK)
    mesh.box((-150.0, 0.0, 150.0), (16.0, 150.0, 100.0), pal.WOOD_PLANK)
    for sy in (-1.0, 1.0):
        mesh.cylinder((40.0, sy * 96.0, 66.0), 66.0, 18.0, pal.WOOD_DARK, sides=8)
    mesh.box((190.0, 0.0, 60.0), (200.0, 20.0, 14.0), pal.WOOD_DARK, pitch=-8.0)
    return mesh


def well(seed=1):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 130.0, 110.0, pal.STONE_PALE, sides=10)
    mesh.cylinder((0.0, 0.0, 110.0), 130.0, 22.0, pal.STONE_DARK, sides=10)
    for sy in (-1.0, 1.0):
        mesh.box((0.0, sy * 110.0, 240.0), (22.0, 22.0, 260.0), pal.WOOD_DARK)
    mesh.prism((0.0, 0.0, 366.0), (200.0, 300.0, 110.0), pal.ROOF_BROWN)
    mesh.cylinder((0.0, 0.0, 300.0), 22.0, 200.0, pal.WOOD_PLANK, sides=6, yaw=90.0)
    mesh.box((0.0, 0.0, 250.0), (70.0, 70.0, 60.0), pal.WOOD_DARK)
    return mesh


def haybale(seed=1):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 90.0, 150.0, pal.CROP_WHEAT, sides=10)
    return mesh


def scarecrow(seed=1):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 12.0, 280.0, pal.WOOD_DARK, sides=5)
    mesh.box((0.0, 0.0, 210.0), (16.0, 250.0, 16.0), pal.WOOD_DARK)
    mesh.box((0.0, 0.0, 190.0), (90.0, 110.0, 130.0), pal.CLOTH_RED)
    mesh.icosphere((0.0, 0.0, 292.0), 55.0, pal.CROP_WHEAT, subdivisions=0,
                   squash=0.9, jitter=0.15, seed=seed)
    mesh.cone((0.0, 0.0, 320.0), 90.0, 70.0, pal.CROP_WHEAT, sides=7)
    return mesh


def planter(seed=1):
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, 40.0), (140.0, 90.0, 80.0), pal.WALL_TERRACOTTA)
    mesh.box((0.0, 0.0, 82.0), (150.0, 100.0, 10.0), pal.DIRT_BROWN)
    mesh.icosphere((0.0, 0.0, 118.0), 58.0, pal.BUSH_GREEN, wind=0.6,
                   subdivisions=0, squash=0.7, jitter=0.25, seed=seed)
    return mesh


# ---------------------------------------------------------------------------
# Waterfront
# ---------------------------------------------------------------------------
def dock_section(seed=1, length=400.0, width=260.0):
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, -12.0), (length, width, 24.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((sx * length * 0.42, sy * width * 0.4, -120.0),
                     (30.0, 30.0, 220.0), pal.WOOD_DARK)
    return mesh


def dock_post(seed=1, height=260.0):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, -140.0), 30.0, height, pal.WOOD_DARK, sides=6)
    mesh.cylinder((0.0, 0.0, height - 140.0), 34.0, 16.0, pal.METAL_RUST, sides=6)
    return mesh


def rowboat(seed=1):
    mesh = MeshBuilder()
    length, width, depth = 330.0, 130.0, 70.0
    mesh.frustum((0.0, 0.0, 0.0), (length * 0.55, width * 0.5),
                 (length, width), depth, pal.WOOD_PLANK)
    mesh.box((0.0, 0.0, depth * 0.62), (length * 0.86, width * 0.7, 14.0), pal.WOOD_DARK)
    for x in (-60.0, 60.0):
        mesh.box((x, 0.0, depth * 0.78), (26.0, width * 0.86, 12.0), pal.WOOD_DARK)
    return mesh


def fishing_boat(seed=1):
    mesh = MeshBuilder()
    length, width, depth = 700.0, 240.0, 150.0
    mesh.frustum((0.0, 0.0, -40.0), (length * 0.5, width * 0.45),
                 (length, width), depth, pal.WALL_BLUE)
    mesh.box((0.0, 0.0, depth - 46.0), (length * 0.9, width * 0.82, 16.0), pal.WOOD_PLANK)
    mesh.box((-length * 0.18, 0.0, depth + 40.0), (200.0, width * 0.6, 130.0), pal.WALL_CREAM)
    mesh.box((-length * 0.18, 0.0, depth + 112.0), (215.0, width * 0.65, 16.0), pal.ROOF_RED)
    mesh.cylinder((length * 0.12, 0.0, depth - 30.0), 12.0, 380.0, pal.WOOD_DARK, sides=5)
    return mesh


def bridge_section(seed=1, span=700.0, width=520.0):
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, 0.0), (span, width, 34.0), pal.WOOD_PLANK)
    for sy in (-1.0, 1.0):
        mesh.box((0.0, sy * width * 0.46, 60.0), (span, 16.0, 90.0), pal.WOOD_DARK)
        for i in range(4):
            x = -span * 0.4 + i * span * 0.266
            mesh.box((x, sy * width * 0.46, 60.0), (24.0, 26.0, 100.0), pal.WOOD_DARK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * span * 0.44, 0.0, -130.0), (60.0, width * 0.9, 260.0), pal.STONE_DARK)
    return mesh


# ---------------------------------------------------------------------------
# Characters: blocky standing figures, no skeleton and no animation
# ---------------------------------------------------------------------------
def _scale_mesh(mesh, factor):
    """Uniformly scale a finished mesh in place.

    Cheaper than threading a scale factor through every literal in person(),
    and safe because the scale is uniform: face normals are unchanged, so the
    flat shading and the winding both survive.
    """
    if factor == 1.0:
        return mesh
    mesh.vertices = [(v[0] * factor, v[1] * factor, v[2] * factor) for v in mesh.vertices]
    return mesh


# What each figure wears. Adding an entry here and a catalog line is the whole
# cost of a new kind of inhabitant.
PERSON_KINDS = ("plain", "hat", "apron", "coat", "pack", "robe", "suit",
                "uniform", "hivis", "child")


def person(seed=1, kind="plain", scale=1.0):
    """One blocky inhabitant.

    Proportions are deliberately toy-like: big head, short limbs, no neck to
    speak of. ``kind`` swaps the clothing and adds one silhouette-changing
    prop - a brim, a pack, a long coat - because at this scale the silhouette
    is the only thing that survives being fifteen metres away.

    Built facing +X, like everything in gen_fauna: the NPC actor sets yaw from
    its direction of travel, and actor forward is +X.
    """
    rng = _SmallRng(seed)
    skin = rng_choice(rng, pal.SKIN_TONES)
    shirt = rng_choice(rng, pal.SHIRT_COLOURS)
    trousers = rng_choice(rng, pal.TROUSER_COLOURS)
    boots = pal.WOOD_DARK

    if kind == "suit":
        shirt = rng_choice(rng, pal.SUIT_COLOURS)
        trousers = shirt
        boots = pal.HIDE_BLACK
    elif kind == "uniform":
        shirt = pal.UNIFORM_NAVY
        trousers = pal.UNIFORM_NAVY
        boots = pal.HIDE_BLACK
    elif kind == "hivis":
        shirt = pal.HIVIS_AMBER
    elif kind == "robe":
        shirt = pal.ROBE_CREAM
        trousers = pal.ROBE_CREAM
    elif kind == "child":
        shirt = rng_choice(rng, [pal.CLOTH_RED, pal.CLOTH_YELLOW, pal.CLOTH_BLUE,
                                 pal.CLOTH_GREEN])

    mesh = MeshBuilder()
    for sy in (-1.0, 1.0):
        mesh.box((0.0, sy * 20.0, 33.0), (34.0, 32.0, 66.0), trousers)
        mesh.box((0.0, sy * 20.0, 4.0), (44.0, 34.0, 16.0), boots)
    mesh.box((0.0, 0.0, 96.0), (46.0, 74.0, 62.0), shirt)
    for sy in (-1.0, 1.0):
        mesh.box((0.0, sy * 48.0, 92.0), (28.0, 24.0, 58.0), shirt)
        mesh.box((0.0, sy * 48.0, 60.0), (26.0, 22.0, 22.0), skin)
    mesh.box((0.0, 0.0, 134.0), (34.0, 40.0, 18.0), skin)
    mesh.box((0.0, 0.0, 164.0), (52.0, 56.0, 46.0), skin)

    # --- what makes each one different --------------------------------------
    if kind == "hat":
        # Wide straw brim: the farm silhouette, readable across a field.
        mesh.box((0.0, 0.0, 188.0), (92.0, 96.0, 8.0), pal.CROP_WHEAT)
        mesh.box((0.0, 0.0, 200.0), (48.0, 52.0, 22.0), pal.CROP_WHEAT)
    elif kind == "apron":
        mesh.box((25.0, 0.0, 88.0), (10.0, 62.0, 78.0), pal.APRON_CREAM)
        mesh.box((0.0, 0.0, 190.0), (58.0, 62.0, 14.0), pal.CLOTH_CREAM)
    elif kind == "coat":
        # A long coat: one taller, wider box over the torso and hips.
        mesh.box((0.0, 0.0, 92.0), (52.0, 80.0, 96.0), rng_choice(rng, pal.COAT_COLOURS))
        mesh.box((0.0, 0.0, 190.0), (62.0, 66.0, 16.0), pal.CLOTH_BLUE)
    elif kind == "pack":
        mesh.box((-30.0, 0.0, 104.0), (26.0, 54.0, 52.0), pal.ROPE_TAN)
        mesh.box((-30.0, 0.0, 132.0), (28.0, 30.0, 12.0), pal.WOOD_DARK)
    elif kind == "robe":
        # Skirted, so the priest reads as a priest and not as a pale villager.
        mesh.frustum((0.0, 0.0, 0.0), (74.0, 84.0), (52.0, 62.0), 70.0, pal.ROBE_CREAM)
        mesh.box((22.0, 0.0, 104.0), (8.0, 12.0, 46.0), pal.CLOTH_YELLOW)
    elif kind == "suit":
        mesh.box((24.0, 0.0, 104.0), (6.0, 12.0, 40.0), pal.CLOTH_RED)
        mesh.box((14.0, 52.0, 52.0), (30.0, 16.0, 34.0), pal.WOOD_DARK)   # case
    elif kind == "uniform":
        mesh.box((0.0, 0.0, 190.0), (56.0, 60.0, 18.0), pal.UNIFORM_NAVY)
        mesh.box((26.0, 0.0, 190.0), (18.0, 52.0, 6.0), pal.UNIFORM_NAVY)  # peak
    elif kind == "hivis":
        mesh.box((0.0, 0.0, 190.0), (58.0, 62.0, 16.0), pal.HIVIS_AMBER)
    elif rng.next() > 0.45:
        # Everyone else: a hat about half the time, as before.
        mesh.box((0.0, 0.0, 190.0), (62.0, 66.0, 16.0),
                 rng_choice(rng, [pal.CLOTH_BLUE, pal.CROP_WHEAT, pal.CLOTH_GREEN]))

    return _scale_mesh(mesh, scale)


def villager(seed=1):
    """The plain townsfolk figure. Kept as its own name: the mesh catalog and
    the 0.1 screenshots both refer to SM_Villager_*."""
    return person(seed, "plain")
