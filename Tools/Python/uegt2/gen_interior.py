"""Furniture, fittings and the pieces rooms are built out of.

Every function here emits into a MeshBuilder that somebody else owns, at the
origin, so a room can compose a dozen of them through ``place()`` without any of
them knowing where they are. That is the whole reason this module exists
separately from the layout: an armchair does not need to know what room it is in.

Conventions, and they matter because everything here is composed blind:

- An item stands on **z = 0** and is centred on x/y over its own footprint.
- Its **back is -Y**, so ``place(..., yaw)`` with yaw 0 puts the back against a
  wall whose inner face runs along -Y, and the item faces +Y into the room.
- Sizes are centimetres. The player is 180 tall, a table is 76, a doorway is 210.
  Furniture that reads as furniture at this art scale is chunkier than real
  furniture, so these numbers are deliberately a little heavy.
- Colours come from palette.py, like everything else. Nothing here is emissive:
  a single static mesh carries one material, so anything that glows is a
  separate mesh, the way SM_LampPost_Glow already is.
- **Every item takes a seed**, including the ones that do not vary. A uniform
  signature is what lets the layout call any item through the same ``place()``
  without a table of exceptions, and an item that is fixed today may want to
  vary tomorrow.
"""
from __future__ import annotations

import math

from . import gen_town
from . import palette as pal
from .meshkit import MeshBuilder, _SmallRng, rotate_z


# ---------------------------------------------------------------------------
# Composition
# ---------------------------------------------------------------------------
def place(target, item, at, yaw=0.0, *args, **kwargs):
    """Build ``item`` at the origin, then rotate and translate it into ``target``.

    MeshBuilder has no transform of its own - every generator in this project
    bakes world positions straight into the vertex buffer - so composing a room
    out of reusable furniture needs this one helper. It is also the only place
    that touches MeshBuilder's internals, which is why it is careful to copy the
    triangle indices unchanged: meshkit._emit has already swapped their winding
    for Unreal, and swapping it a second time would turn every piece of
    furniture inside out.
    """
    scratch = MeshBuilder()
    item(scratch, *args, **kwargs)

    base = len(target.vertices)
    for vertex in scratch.vertices:
        spun = rotate_z(vertex, yaw) if yaw else vertex
        target.vertices.append((spun[0] + at[0], spun[1] + at[1], spun[2] + at[2]))
    for normal in scratch.normals:
        target.normals.append(rotate_z(normal, yaw) if yaw else normal)
    target.colors.extend(scratch.colors)
    for (i0, i1, i2) in scratch.triangles:
        target.triangles.append((i0 + base, i1 + base, i2 + base))
    return target


def against(wall_axis, wall_sign, room, depth, along=0.0):
    """Position and yaw for an item standing against one wall of a room.

    ``room`` is (centre_x, centre_y, half_x, half_y). ``depth`` is how deep the
    item is, so its back lands on the wall rather than in it. ``along`` slides it
    left or right in centimetres.
    """
    cx, cy, hx, hy = room
    if wall_axis == "x":
        # A wall facing along X: the item's back is against x = cx +/- hx.
        x = cx + wall_sign * (hx - depth * 0.5)
        return (x, cy + along, 0.0), (90.0 if wall_sign > 0 else -90.0)
    y = cy + wall_sign * (hy - depth * 0.5)
    return (cx + along, y, 0.0), (0.0 if wall_sign < 0 else 180.0)


# ---------------------------------------------------------------------------
# Sleeping
# ---------------------------------------------------------------------------
def bed(mesh, length=190.0, width=95.0, seed=1):
    """A cot: frame, mattress, blanket and a pillow at the -Y (head) end."""
    rng = _SmallRng(seed)
    blanket = _pick(rng, [pal.CLOTH_RED, pal.CLOTH_BLUE, pal.CLOTH_GREEN,
                          pal.CLOTH_YELLOW, pal.WALL_TERRACOTTA])

    # Frame and legs. The head board is taller, which is what tells you which
    # way round a bed is from across a dark room.
    mesh.box((0.0, 0.0, 22.0), (width, length, 20.0), pal.WOOD_DARK)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((sx * (width * 0.5 - 9.0), sy * (length * 0.5 - 9.0), 11.0),
                     (16.0, 16.0, 22.0), pal.WOOD_DARK)
    mesh.box((0.0, -length * 0.5 + 6.0, 46.0), (width, 12.0, 68.0), pal.WOOD_DARK)
    mesh.box((0.0, length * 0.5 - 6.0, 34.0), (width, 12.0, 44.0), pal.WOOD_DARK)

    mesh.box((0.0, 0.0, 42.0), (width - 14.0, length - 18.0, 22.0), pal.CLOTH_CREAM)
    mesh.box((0.0, length * 0.16, 45.0), (width - 12.0, length * 0.62, 20.0), blanket)
    mesh.box((0.0, -length * 0.5 + 40.0, 56.0), (width * 0.72, 44.0, 16.0), pal.CANVAS_WHITE)


def bedside(mesh, seed=1):
    """A small table by a bed, with a candle stub on it."""
    mesh.box((0.0, 0.0, 52.0), (46.0, 40.0, 8.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((sx * 17.0, sy * 14.0, 26.0), (9.0, 9.0, 52.0), pal.WOOD_DARK)
    mesh.box((0.0, 0.0, 62.0), (10.0, 10.0, 20.0), pal.CANVAS_WHITE)


def wardrobe(mesh, width=110.0, height=190.0, seed=1):
    """A tall press. Reads as a room's vertical anchor from the doorway."""
    depth = 56.0
    mesh.box((0.0, 0.0, height * 0.5), (width, depth, height), pal.WOOD_PLANK)
    mesh.box((0.0, 0.0, height + 8.0), (width + 16.0, depth + 14.0, 16.0), pal.WOOD_DARK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * width * 0.24, depth * 0.5 + 3.0, height * 0.52),
                 (width * 0.42, 8.0, height * 0.82), pal.WOOD_DARK)
        mesh.box((sx * 12.0, depth * 0.5 + 8.0, height * 0.5), (10.0, 10.0, 10.0),
                 pal.METAL_IRON)


def chest(mesh, width=100.0, seed=1):
    """A banded storage chest. Also the cheapest way to fill a corner."""
    mesh.box((0.0, 0.0, 27.0), (width, 52.0, 54.0), pal.WOOD_PLANK)
    mesh.box((0.0, 0.0, 58.0), (width + 6.0, 58.0, 12.0), pal.WOOD_DARK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * width * 0.32, 0.0, 28.0), (10.0, 56.0, 56.0), pal.METAL_IRON)


# ---------------------------------------------------------------------------
# Eating and sitting
# ---------------------------------------------------------------------------
def table(mesh, width=150.0, depth=88.0, height=76.0, seed=1):
    mesh.box((0.0, 0.0, height - 5.0), (width, depth, 12.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((sx * (width * 0.5 - 14.0), sy * (depth * 0.5 - 12.0), height * 0.5),
                     (16.0, 16.0, height - 10.0), pal.WOOD_DARK)
    # A stretcher between the legs: without it a table reads as a floating slab.
    mesh.box((0.0, 0.0, 22.0), (width - 30.0, 12.0, 10.0), pal.WOOD_DARK)


def chair(mesh, seed=1):
    seat_h = 46.0
    mesh.box((0.0, 0.0, seat_h), (44.0, 42.0, 9.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((sx * 17.0, sy * 16.0, seat_h * 0.5), (8.0, 8.0, seat_h),
                     pal.WOOD_DARK)
    mesh.box((0.0, -19.0, seat_h + 30.0), (44.0, 8.0, 52.0), pal.WOOD_DARK)


def stool(mesh, seed=1):
    mesh.box((0.0, 0.0, 44.0), (36.0, 34.0, 8.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((sx * 13.0, sy * 12.0, 22.0), (7.0, 7.0, 44.0), pal.WOOD_DARK)


def settle(mesh, length=170.0, seed=1):
    """A high-backed bench, the fireside seat in a cottage."""
    mesh.box((0.0, 0.0, 44.0), (length, 46.0, 10.0), pal.WOOD_PLANK)
    mesh.box((0.0, -21.0, 78.0), (length, 10.0, 78.0), pal.WOOD_DARK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * (length * 0.5 - 8.0), 0.0, 22.0), (14.0, 44.0, 44.0), pal.WOOD_DARK)


# ---------------------------------------------------------------------------
# Working
# ---------------------------------------------------------------------------
def counter(mesh, length=180.0, height=92.0, seed=1):
    """A kitchen or shop counter with a stone top."""
    mesh.box((0.0, 0.0, height * 0.5 - 6.0), (length, 62.0, height - 12.0), pal.WOOD_PLANK)
    mesh.box((0.0, 0.0, height - 5.0), (length + 12.0, 70.0, 12.0), pal.STONE_PALE)
    for i in (-1.0, 1.0):
        mesh.box((i * length * 0.26, 32.0, height * 0.5), (length * 0.4, 6.0, height * 0.5),
                 pal.WOOD_DARK)


def shelf(mesh, width=120.0, height=170.0, seed=1):
    """An open shelf unit with a few crocks and bolts of cloth on it."""
    rng = _SmallRng(seed)
    depth = 36.0
    for sx in (-1.0, 1.0):
        mesh.box((sx * (width * 0.5 - 6.0), 0.0, height * 0.5), (12.0, depth, height),
                 pal.WOOD_DARK)
    for i in range(4):
        z = 34.0 + i * (height - 44.0) / 3.0
        mesh.box((0.0, 0.0, z), (width, depth, 8.0), pal.WOOD_PLANK)
        for k in range(3):
            if rng.next() < 0.45:
                continue
            colour = _pick(rng, [pal.WALL_TERRACOTTA, pal.CLOTH_CREAM, pal.CROP_WHEAT,
                                 pal.METAL_COPPER, pal.CLOTH_GREEN])
            mesh.box((-width * 0.3 + k * width * 0.3, 0.0, z + 16.0),
                     (24.0, 22.0, 24.0), colour)


def dresser(mesh, width=130.0, seed=1):
    """A chest of drawers with a plate rack over it."""
    mesh.box((0.0, 0.0, 46.0), (width, 52.0, 92.0), pal.WOOD_PLANK)
    for i in range(3):
        mesh.box((0.0, 27.0, 24.0 + i * 26.0), (width - 18.0, 6.0, 20.0), pal.WOOD_DARK)
        mesh.box((0.0, 30.0, 24.0 + i * 26.0), (24.0, 6.0, 6.0), pal.METAL_IRON)
    for i in range(2):
        mesh.box((0.0, -8.0, 104.0 + i * 34.0), (width - 16.0, 30.0, 7.0), pal.WOOD_DARK)


def desk(mesh, width=160.0, seed=1):
    """Newhaven's contribution: a desk with a blotter and a stack of paper."""
    mesh.box((0.0, 0.0, 70.0), (width, 76.0, 10.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * (width * 0.5 - 12.0), 0.0, 35.0), (18.0, 70.0, 70.0), pal.WOOD_DARK)
    mesh.box((0.0, 0.0, 77.0), (width * 0.5, 46.0, 4.0), pal.CLOTH_GREEN)
    mesh.box((width * 0.28, 12.0, 80.0), (34.0, 26.0, 10.0), pal.PAPER_CREAM)


def bookshelf(mesh, width=130.0, height=200.0, seed=1):
    rng = _SmallRng(seed)
    depth = 34.0
    mesh.box((0.0, -depth * 0.5 + 3.0, height * 0.5), (width, 8.0, height), pal.WOOD_DARK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * (width * 0.5 - 6.0), 0.0, height * 0.5), (12.0, depth, height),
                 pal.WOOD_DARK)
    for i in range(5):
        z = 26.0 + i * (height - 40.0) / 4.0
        mesh.box((0.0, 0.0, z), (width, depth, 7.0), pal.WOOD_PLANK)
        # A run of books: one box per group, not per book.
        run = 0.0
        while run < width - 24.0:
            span = 18.0 + rng.next() * 34.0
            if run + span > width - 24.0:
                break
            colour = _pick(rng, [pal.DOOR_RED, pal.DOOR_BLUE, pal.DOOR_GREEN,
                                 pal.WOOD_DARK, pal.FACADE_BRICK])
            mesh.box((-width * 0.5 + 12.0 + run + span * 0.5, 0.0, z + 20.0),
                     (span, depth - 10.0, 34.0), colour)
            run += span + 4.0


def shop_stand(mesh, width=140.0, seed=1):
    """A display table for a shophouse: goods stacked on a low platform."""
    rng = _SmallRng(seed)
    mesh.box((0.0, 0.0, 34.0), (width, 70.0, 12.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * (width * 0.5 - 10.0), 0.0, 17.0), (14.0, 62.0, 34.0), pal.WOOD_DARK)
    for i in range(3):
        colour = _pick(rng, [pal.CROP_WHEAT, pal.CLOTH_RED, pal.AWNING_GREEN,
                             pal.WALL_OCHRE, pal.METAL_COPPER])
        mesh.box((-width * 0.3 + i * width * 0.3, 0.0, 54.0), (32.0, 44.0, 28.0), colour)


# ---------------------------------------------------------------------------
# Heat, light and clutter
# ---------------------------------------------------------------------------
def hearth(mesh, width=170.0, seed=1):
    """A stone fireplace. The fire itself is a separate emissive mesh - one
    material per static mesh - so this is the cold half only."""
    mesh.box((0.0, -22.0, 108.0), (width, 46.0, 216.0), pal.STONE_PALE)
    # The opening, cut by building the surround as three blocks rather than by
    # subtracting anything: meshkit has no boolean operations and does not need
    # any.
    for sx in (-1.0, 1.0):
        mesh.box((sx * (width * 0.5 - 34.0), 8.0, 62.0), (68.0, 62.0, 124.0), pal.STONE_PALE)
    mesh.box((0.0, 8.0, 148.0), (width, 62.0, 44.0), pal.STONE_DARK)
    mesh.box((0.0, 14.0, 6.0), (width - 40.0, 66.0, 12.0), pal.STONE_DARK)
    # Logs in the grate.
    for i in range(3):
        mesh.box((-28.0 + i * 28.0, 12.0, 24.0), (18.0, 52.0, 18.0), pal.WOOD_DARK,
                 roll=18.0 * (i - 1))


def hearth_fire(mesh, width=170.0, seed=1):
    """The emissive half of a hearth: the fire in the grate. Built into the
    emissive material's own mesh so it lights the room under Lumen."""
    mesh.box((0.0, 12.0, 26.0), (width * 0.52, 46.0, 34.0), pal.LAMP_GLASS)
    mesh.box((0.0, 12.0, 48.0), (width * 0.34, 32.0, 26.0), pal.LAMP_GLASS)


def ceiling_lamp(mesh, drop=42.0, seed=1):
    """The fitting: a cord and a shade. Hung from z = 0, going *down*."""
    mesh.box((0.0, 0.0, -drop * 0.5), (7.0, 7.0, drop), pal.METAL_IRON)
    mesh.cone((0.0, 0.0, -drop - 30.0), 34.0, 32.0, pal.METAL_IRON, sides=8)


def ceiling_lamp_glow(mesh, drop=42.0, seed=1):
    """The emissive half of a ceiling lamp."""
    mesh.icosphere((0.0, 0.0, -drop - 16.0), 17.0, pal.LAMP_GLASS, subdivisions=0)


def rug(mesh, width=190.0, depth=130.0, seed=1):
    rng = _SmallRng(seed)
    colour = _pick(rng, [pal.CLOTH_RED, pal.CLOTH_BLUE, pal.WALL_TERRACOTTA,
                         pal.CLOTH_GREEN])
    mesh.box((0.0, 0.0, 2.0), (width, depth, 4.0), colour)
    mesh.box((0.0, 0.0, 3.0), (width - 34.0, depth - 34.0, 4.0), pal.CLOTH_CREAM)


def barrel_small(mesh, seed=1):
    mesh.cylinder((0.0, 0.0, 0.0), 26.0, 76.0, pal.WOOD_PLANK, sides=8)
    for z in (18.0, 56.0):
        mesh.cylinder((0.0, 0.0, z), 28.0, 8.0, pal.METAL_IRON, sides=8)


def crate_small(mesh, seed=1):
    mesh.box((0.0, 0.0, 30.0), (62.0, 58.0, 60.0), pal.WOOD_PLANK)
    mesh.box((0.0, 0.0, 61.0), (66.0, 62.0, 6.0), pal.WOOD_DARK)


def sack(mesh, seed=1):
    mesh.frustum((0.0, 0.0, 0.0), (54.0, 50.0), (36.0, 34.0), 62.0, pal.CROP_WHEAT)
    mesh.box((0.0, 0.0, 68.0), (30.0, 28.0, 14.0), pal.ROPE_TAN)


def pot(mesh, seed=1):
    mesh.cylinder((0.0, 0.0, 0.0), 22.0, 30.0, pal.METAL_IRON, sides=8)
    mesh.cylinder((0.0, 0.0, 30.0), 24.0, 6.0, pal.METAL_IRON, sides=8)


def firewood(mesh, seed=1):
    rng = _SmallRng(seed)
    for i in range(6):
        z = 9.0 + (i // 3) * 20.0
        mesh.box((-24.0 + (i % 3) * 24.0, 0.0, z), (20.0, 62.0, 20.0), pal.WOOD_DARK,
                 pitch=rng.uniform(-6.0, 6.0))


def washstand(mesh, seed=1):
    mesh.box((0.0, 0.0, 80.0), (66.0, 46.0, 10.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * 26.0, 0.0, 40.0), (12.0, 40.0, 80.0), pal.WOOD_DARK)
    mesh.cylinder((0.0, 0.0, 85.0), 20.0, 16.0, pal.CANVAS_WHITE, sides=8)


# ---------------------------------------------------------------------------
# Structure
# ---------------------------------------------------------------------------
def stair_run(mesh, rise, width=110.0, steps=8, run=30.0, seed=1):
    """A straight flight from z = 0 up to z = rise, climbing toward +Y.

    Riser height is what makes this walkable or not: the pawn's MaxStepHeight is
    45 cm, so eight steps carry a 346 cm storey at 43 cm a tread - chunky, which
    suits the art, and inside the limit with room to spare.
    """
    step_h = rise / steps
    for i in range(steps):
        # Each tread is a solid block from the floor to its own top rather than a
        # slab on a stringer. It is one box instead of three, it cannot be walked
        # under, and the pawn's step-up handles it without a ramp.
        top = step_h * (i + 1)
        y = -run * steps * 0.5 + run * (i + 0.5)
        mesh.box((0.0, y, top * 0.5), (width, run, top), pal.WOOD_PLANK)
    # A rail down the open side, at the top where the drop is.
    mesh.box((width * 0.5 + 8.0, run * steps * 0.28, rise * 0.62),
             (10.0, run * steps * 0.44, 90.0), pal.WOOD_DARK)


def partition(mesh, length, height, thickness=18.0, door_at=None, door_w=110.0,
              door_h=210.0, colour=None):
    """An internal wall running along X, optionally with a doorway gap in it."""
    colour = colour if colour is not None else pal.WALL_WHITE
    if door_at is None:
        mesh.box((0.0, 0.0, height * 0.5), (length, thickness, height), colour)
        return

    left_edge = -length * 0.5
    right_edge = length * 0.5
    door_l = door_at - door_w * 0.5
    door_r = door_at + door_w * 0.5

    if door_l - left_edge > 4.0:
        span = door_l - left_edge
        mesh.box((left_edge + span * 0.5, 0.0, height * 0.5), (span, thickness, height),
                 colour)
    if right_edge - door_r > 4.0:
        span = right_edge - door_r
        mesh.box((right_edge - span * 0.5, 0.0, height * 0.5), (span, thickness, height),
                 colour)
    if height - door_h > 4.0:
        mesh.box((door_at, 0.0, door_h + (height - door_h) * 0.5),
                 (door_w, thickness, height - door_h), colour)
    # A lintel, so the opening reads as a doorway rather than a hole.
    mesh.box((door_at, 0.0, door_h - 6.0), (door_w + 22.0, thickness + 8.0, 16.0),
             pal.WOOD_DARK)


def _pick(rng, items):
    return items[int(rng.next() * len(items)) % len(items)]


# ---------------------------------------------------------------------------
# Room layout
# ---------------------------------------------------------------------------
# A room smaller than this in either direction is not a room, it is a cupboard:
# the player capsule is 68 across and needs somewhere to stand that is not
# inside the furniture.
# ---------------------------------------------------------------------------
# Shop, clinic and civic fittings
# ---------------------------------------------------------------------------
# Newhaven's ground floors are businesses, and a business is legible from its
# fittings alone: a chair with a basin behind it is a barber, a long glass case
# is an optician, benches facing a table is a church hall. Same rules as the
# domestic kit above - origin on the floor, centred on x, back to -Y, and a seed
# on every one so place() can call any of them the same way.
def display_case(mesh, length=170.0, seed=1):
    """A glass topped case. Opticians, bakers, pharmacies, jewellers."""
    mesh.box((0.0, 0.0, 42.0), (length, 58.0, 84.0), pal.WOOD_DARK)
    mesh.box((0.0, 0.0, 93.0), (length - 12.0, 50.0, 18.0), pal.WINDOW_GLASS)
    mesh.box((0.0, 0.0, 104.0), (length, 58.0, 8.0), pal.WOOD_PLANK)


def clothes_rail(mesh, length=200.0, seed=1):
    """A rail of garments. On its own it says clothing shop."""
    rng = _SmallRng(seed)
    shirts = [pal.CLOTH_RED, pal.CLOTH_BLUE, pal.CLOTH_GREEN,
              pal.CLOTH_YELLOW, pal.CLOTH_CREAM]
    for sx in (-1.0, 1.0):
        mesh.box((sx * (length * 0.5 - 8.0), 0.0, 82.0), (10.0, 10.0, 164.0),
                 pal.METAL_IRON)
    mesh.box((0.0, 0.0, 162.0), (length, 8.0, 8.0), pal.METAL_IRON)
    for i in range(6):
        x = -length * 0.5 + 22.0 + i * (length - 44.0) / 5.0
        mesh.box((x, 0.0, 116.0), (20.0, 34.0, 82.0),
                 shirts[int(rng.next() * len(shirts)) % len(shirts)])


def produce_bin(mesh, length=150.0, seed=1):
    """A sloped grocery bin with something in it."""
    rng = _SmallRng(seed)
    crop = [pal.CROP_GREEN, pal.LEAF_AUTUMN, pal.BEAK_ORANGE, pal.CROP_WHEAT]
    mesh.box((0.0, 0.0, 34.0), (length, 78.0, 68.0), pal.WOOD_PLANK)
    mesh.box((0.0, -6.0, 76.0), (length - 14.0, 62.0, 20.0),
             crop[int(rng.next() * len(crop)) % len(crop)])
    mesh.box((0.0, 34.0, 88.0), (length, 8.0, 44.0), pal.WOOD_DARK)


def checkout(mesh, length=160.0, seed=1):
    """A till counter with a register on it."""
    mesh.box((0.0, 0.0, 46.0), (length, 72.0, 92.0), pal.WALL_WHITE)
    mesh.box((0.0, 0.0, 96.0), (length + 10.0, 80.0, 10.0), pal.STONE_PALE)
    mesh.box((length * 0.3, 0.0, 118.0), (44.0, 40.0, 34.0), pal.METAL_IRON)
    mesh.box((length * 0.3, -14.0, 138.0), (36.0, 6.0, 22.0), pal.GLASS_DARK)


def fridge_cabinet(mesh, width=150.0, seed=1):
    """A tall glass fronted cabinet: chilled goods, or a display of stock."""
    mesh.box((0.0, 6.0, 96.0), (width, 68.0, 192.0), pal.WALL_WHITE)
    mesh.box((0.0, -26.0, 104.0), (width - 18.0, 10.0, 150.0), pal.WINDOW_GLASS)
    for z in (56.0, 106.0, 156.0):
        mesh.box((0.0, -6.0, z), (width - 22.0, 46.0, 8.0), pal.METAL_IRON)


def tool_rack(mesh, width=170.0, seed=1):
    """Pegboard and hanging tools: the hardware shop in one object."""
    rng = _SmallRng(seed)
    mesh.box((0.0, 12.0, 110.0), (width, 16.0, 200.0), pal.WOOD_DARK)
    for i in range(5):
        x = -width * 0.5 + 20.0 + i * (width - 40.0) / 4.0
        length = 40.0 + rng.next() * 60.0
        mesh.box((x, -2.0, 190.0 - length * 0.5), (12.0, 12.0, length),
                 pal.METAL_IRON if i % 2 else pal.WOOD_PLANK)
    mesh.box((0.0, -4.0, 44.0), (width, 44.0, 88.0), pal.WOOD_PLANK)


def sofa(mesh, length=210.0, seed=1):
    rng = _SmallRng(seed)
    cloth = [pal.CLOTH_BLUE, pal.CLOTH_GREEN, pal.CLOTH_RED, pal.CLOTH_CREAM]
    body = cloth[int(rng.next() * len(cloth)) % len(cloth)]
    mesh.box((0.0, 0.0, 22.0), (length, 86.0, 44.0), pal.WOOD_DARK)
    mesh.box((0.0, 4.0, 52.0), (length - 20.0, 78.0, 22.0), body)
    mesh.box((0.0, 34.0, 78.0), (length, 18.0, 76.0), body)
    for sx in (-1.0, 1.0):
        mesh.box((sx * (length * 0.5 - 10.0), 4.0, 68.0), (20.0, 82.0, 52.0), body)


def appliance(mesh, width=110.0, seed=1):
    """A boxed white good, or a screen on a stand: the electronics shop."""
    rng = _SmallRng(seed)
    if rng.next() < 0.5:
        mesh.box((0.0, 0.0, 84.0), (width, 74.0, 168.0), pal.WALL_WHITE)
        mesh.box((0.0, -38.0, 110.0), (width - 30.0, 6.0, 60.0), pal.GLASS_DARK)
        mesh.box((0.0, -38.0, 156.0), (width - 44.0, 6.0, 10.0), pal.METAL_IRON)
    else:
        mesh.box((0.0, 0.0, 34.0), (width, 60.0, 68.0), pal.WOOD_DARK)
        mesh.box((0.0, 0.0, 74.0), (26.0, 26.0, 14.0), pal.METAL_IRON)
        mesh.box((0.0, 0.0, 120.0), (width + 20.0, 12.0, 78.0), pal.GLASS_DARK)


def filing_cabinet(mesh, width=90.0, seed=1):
    mesh.box((0.0, 0.0, 68.0), (width, 62.0, 136.0), pal.METAL_IRON)
    for z in (28.0, 62.0, 96.0, 130.0):
        mesh.box((0.0, -32.0, z), (width - 12.0, 4.0, 26.0), pal.CONCRETE_GREY)
        mesh.box((0.0, -35.0, z), (22.0, 4.0, 6.0), pal.METAL_COPPER)


def waiting_bench(mesh, length=220.0, seed=1):
    """A run of seats against a wall: clinics, offices, waiting rooms."""
    mesh.box((0.0, 0.0, 40.0), (length, 52.0, 12.0), pal.WOOD_PLANK)
    mesh.box((0.0, 22.0, 74.0), (length, 10.0, 56.0), pal.WOOD_PLANK)
    for sx in (-1.0, 0.0, 1.0):
        mesh.box((sx * (length * 0.5 - 14.0), 0.0, 18.0), (12.0, 46.0, 36.0),
                 pal.METAL_IRON)


def reception_desk(mesh, length=250.0, seed=1):
    """The desk you meet on the way in. Lobbies, clinics, stations."""
    mesh.box((0.0, 0.0, 54.0), (length, 76.0, 108.0), pal.WOOD_DARK)
    mesh.box((0.0, -6.0, 114.0), (length + 16.0, 92.0, 12.0), pal.STONE_PALE)
    mesh.box((length * 0.28, 10.0, 132.0), (44.0, 8.0, 30.0), pal.GLASS_DARK)


def potted_plant(mesh, height=150.0, seed=1):
    rng = _SmallRng(seed)
    mesh.cylinder((0.0, 0.0, 26.0), 26.0, 52.0, pal.FACADE_TERRA, sides=8)
    mesh.box((0.0, 0.0, height * 0.5 + 40.0), (10.0, 10.0, height - 60.0),
             pal.TRUNK_BROWN)
    for i in range(4):
        angle = 90.0 * i + rng.next() * 40.0
        mesh.box((_cos(angle) * 26.0, _sin(angle) * 26.0, height * 0.72),
                 (58.0, 20.0, 12.0), pal.LEAF_JUNGLE, yaw=angle)


def pew(mesh, length=280.0, seed=1):
    mesh.box((0.0, 0.0, 42.0), (length, 44.0, 10.0), pal.WOOD_DARK)
    mesh.box((0.0, 20.0, 74.0), (length, 10.0, 74.0), pal.WOOD_DARK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * (length * 0.5 - 12.0), 0.0, 20.0), (14.0, 40.0, 40.0),
                 pal.WOOD_DARK)


def altar(mesh, length=200.0, seed=1):
    mesh.box((0.0, 0.0, 48.0), (length, 90.0, 96.0), pal.STONE_PALE)
    mesh.box((0.0, 0.0, 100.0), (length + 20.0, 100.0, 12.0), pal.STONE_DARK)
    mesh.box((0.0, 0.0, 106.0), (length - 40.0, 70.0, 4.0), pal.CLOTH_CREAM)
    mesh.box((0.0, 10.0, 140.0), (14.0, 14.0, 64.0), pal.METAL_COPPER)
    mesh.box((0.0, 10.0, 160.0), (46.0, 12.0, 14.0), pal.METAL_COPPER)


def lectern(mesh, width=70.0, seed=1):
    mesh.box((0.0, 0.0, 56.0), (26.0, 26.0, 112.0), pal.WOOD_DARK)
    mesh.box((0.0, 0.0, 14.0), (width, 60.0, 28.0), pal.WOOD_DARK)
    mesh.box((0.0, -6.0, 122.0), (width, 54.0, 12.0), pal.WOOD_PLANK, pitch=14.0)
    mesh.box((0.0, -8.0, 132.0), (width - 22.0, 40.0, 4.0), pal.PAPER_CREAM,
             pitch=14.0)


def barber_chair(mesh, seed=1):
    mesh.cylinder((0.0, 0.0, 16.0), 34.0, 32.0, pal.METAL_IRON, sides=8)
    mesh.box((0.0, 0.0, 40.0), (18.0, 18.0, 48.0), pal.METAL_IRON)
    mesh.box((0.0, 0.0, 70.0), (74.0, 74.0, 20.0), pal.CLOTH_RED)
    mesh.box((0.0, 30.0, 112.0), (74.0, 18.0, 84.0), pal.CLOTH_RED)
    mesh.box((0.0, 34.0, 162.0), (52.0, 16.0, 26.0), pal.CLOTH_RED)
    for sx in (-1.0, 1.0):
        mesh.box((sx * 42.0, 2.0, 92.0), (10.0, 60.0, 12.0), pal.METAL_IRON)


def dental_chair(mesh, seed=1):
    mesh.box((0.0, 0.0, 18.0), (60.0, 90.0, 36.0), pal.METAL_IRON)
    mesh.box((0.0, -10.0, 58.0), (74.0, 150.0, 24.0), pal.CLOTH_GREEN)
    mesh.box((0.0, 54.0, 104.0), (74.0, 22.0, 76.0), pal.CLOTH_GREEN)
    mesh.box((0.0, 40.0, 180.0), (16.0, 16.0, 80.0), pal.METAL_IRON)
    mesh.box((0.0, 10.0, 214.0), (40.0, 74.0, 16.0), pal.WALL_WHITE)
    mesh.box((0.0, 10.0, 204.0), (30.0, 60.0, 8.0), pal.LAMP_GLASS)


def exam_couch(mesh, length=190.0, seed=1):
    mesh.box((0.0, 0.0, 30.0), (length - 30.0, 62.0, 60.0), pal.WALL_WHITE)
    mesh.box((0.0, 0.0, 70.0), (length, 72.0, 22.0), pal.CLOTH_BLUE)
    mesh.box((-length * 0.36, 0.0, 92.0), (length * 0.28, 72.0, 22.0),
             pal.CLOTH_BLUE, pitch=-18.0)
    mesh.box((0.0, 0.0, 12.0), (length - 60.0, 46.0, 24.0), pal.METAL_IRON)


def gym_bench(mesh, length=170.0, seed=1):
    mesh.box((0.0, 0.0, 48.0), (length, 44.0, 16.0), pal.CLOTH_RED)
    for sx in (-1.0, 1.0):
        mesh.box((sx * (length * 0.5 - 16.0), 0.0, 22.0), (16.0, 60.0, 44.0),
                 pal.METAL_IRON)
    mesh.box((0.0, 34.0, 100.0), (24.0, 24.0, 104.0), pal.METAL_IRON)
    for z in (76.0, 132.0):
        mesh.box((0.0, 34.0, z), (120.0, 26.0, 22.0), pal.STONE_DARK)


def bar_counter(mesh, length=300.0, seed=1):
    """A long service counter with a footrail: bars, cafes, canteens."""
    mesh.box((0.0, 0.0, 52.0), (length, 74.0, 104.0), pal.WOOD_DARK)
    mesh.box((0.0, -8.0, 110.0), (length + 24.0, 96.0, 14.0), pal.STONE_DARK)
    mesh.box((0.0, -46.0, 18.0), (length, 10.0, 10.0), pal.METAL_COPPER)
    mesh.box((0.0, 30.0, 158.0), (length - 40.0, 24.0, 10.0), pal.WOOD_PLANK)


MIN_ROOM = 250.0
# Above this floor area a room gets split. 5 m x 5 m is a generous single room
# at this art scale; a cottage stays one room and a farmhouse becomes two.
MAX_ROOM_AREA = 250000.0
# The shell's numbers, taken from the shell rather than written down again.
# Two copies of a contract is one copy and one guess, and this one is checked
# only by whether the furniture comes through the wall.
WALL_T = gen_town.WALL_T          # exterior wall panel thickness
STOREY_H = gen_town.STOREY_H      # floor to floor

# These are the fit-out's own and deliberately not the shell's: an internal
# doorway is a cased opening in a stud partition, not a front door in a masonry
# wall, so it is shorter than gen_town.DOOR_H.
PARTITION_T = 16.0       # internal partition thickness
DOOR_W = 120.0           # internal doorway width; the pawn is 68 across
DOOR_H = 210.0           # internal doorway height; the pawn is 180 tall
FLOOR_T = 16.0           # thickness of an upper floor slab

# How far a floor or ceiling is pushed into the wall around it. Anything within
# this distance of the inner wall face is buried in the wall and cannot be seen,
# which is also why the offline checker tolerates exactly this much overhang.
TUCK = 12.0


class Room(object):
    """One rectangular room on one storey, in building-local coordinates."""

    def __init__(self, x, y, hx, hy, storey):
        self.x = x
        self.y = y
        self.hx = hx
        self.hy = hy
        self.storey = storey
        self.kind = "living"

    @property
    def area(self):
        return (self.hx * 2.0) * (self.hy * 2.0)

    def rect(self):
        return (self.x, self.y, self.hx, self.hy)

    def __repr__(self):
        return "Room(%s %.0fx%.0f at %.0f,%.0f)" % (
            self.kind, self.hx * 2, self.hy * 2, self.x, self.y)


class Partition(object):
    """An internal wall between two rooms, with one doorway in it."""

    def __init__(self, x, y, axis, length, storey, door_offset):
        self.x = x
        self.y = y
        self.axis = axis            # 'x': runs along X. 'y': runs along Y.
        self.length = length
        self.storey = storey
        self.door_offset = door_offset


def plan(width, depth, storeys, seed, door_x=0.0, wall_t=WALL_T,
         stair_keep=None):
    """Split a building footprint into rooms, one set per storey.

    A binary split, longest axis first, recursing while a room is bigger than
    MAX_ROOM_AREA and both halves would still clear MIN_ROOM. Two levels deep at
    most, which on this project's footprints means a cottage is one room, a
    house is two and the biggest farmhouse is three or four. Deeper than that
    and a 7 m house becomes a rabbit warren of corridors nobody can turn around
    in.

    Returns (rooms, partitions).
    """
    rng = _SmallRng(seed * 31 + 7)
    inner_hx = width * 0.5 - wall_t
    inner_hy = depth * 0.5 - wall_t

    rooms = []
    partitions = []
    for storey in range(storeys):
        cells = _split(0.0, 0.0, inner_hx, inner_hy, storey, rng, partitions, 0,
                       door_x if storey == 0 else None, stair_keep)
        rooms.extend(cells)
    return rooms, partitions


def _split(x, y, hx, hy, storey, rng, partitions, depth, door_x=None,
           stair_keep=None):
    area = (hx * 2.0) * (hy * 2.0)
    if depth >= 2 or area <= MAX_ROOM_AREA:
        return [Room(x, y, hx, hy, storey)]

    along_x = hx >= hy
    span = (hx if along_x else hy) * 2.0
    # Both halves have to clear MIN_ROOM once the partition has taken its cut.
    if span - PARTITION_T < MIN_ROOM * 2.0:
        return [Room(x, y, hx, hy, storey)]

    # Off centre, so a house does not read as two identical halves.
    ratio = 0.5 + (rng.next() - 0.5) * 0.22
    cut = -span * 0.5 + span * ratio
    low_span = cut + span * 0.5 - PARTITION_T * 0.5
    high_span = span * 0.5 - cut - PARTITION_T * 0.5
    if low_span < MIN_ROOM or high_span < MIN_ROOM:
        cut = 0.0
        low_span = high_span = span * 0.5 - PARTITION_T * 0.5

    # A partition running back from the front wall must not end in the front
    # doorway. The opening is DOOR_W across and the pawn is 68; a 16 cm
    # partition down the middle of it leaves 52 either side, which is a house
    # with a door nobody can walk through. The split is nudged clear, and if
    # neither side leaves two usable rooms the storey stays open plan.
    if along_x and door_x is not None:
        clearance = DOOR_W * 0.5 + PARTITION_T * 0.5 + 20.0
        local_door = door_x - x
        if abs(cut - local_door) < clearance:
            for candidate in (local_door + clearance, local_door - clearance):
                low = candidate + span * 0.5 - PARTITION_T * 0.5
                high = span * 0.5 - candidate - PARTITION_T * 0.5
                if low >= MIN_ROOM and high >= MIN_ROOM:
                    cut, low_span, high_span = candidate, low, high
                    break
            else:
                return [Room(x, y, hx, hy, storey)]

    # Nor may a partition be built through the stairwell. The flight is in the
    # same place on every floor so that the flights stack into a climbable
    # column, which means it is also in the same place as whatever the layout
    # felt like doing there.
    if stair_keep is not None:
        lo, hi = (stair_keep[0], stair_keep[1]) if along_x else (stair_keep[2], stair_keep[3])
        origin = x if along_x else y
        if lo - PARTITION_T < origin + cut < hi + PARTITION_T:
            for edge in (hi + PARTITION_T, lo - PARTITION_T):
                candidate = edge - origin
                low = candidate + span * 0.5 - PARTITION_T * 0.5
                high = span * 0.5 - candidate - PARTITION_T * 0.5
                if low >= MIN_ROOM and high >= MIN_ROOM:
                    cut, low_span, high_span = candidate, low, high
                    break
            else:
                return [Room(x, y, hx, hy, storey)]

    cross = (hy if along_x else hx) * 2.0
    door_offset = (rng.next() - 0.5) * max(cross - DOOR_W - 80.0, 0.0)

    if along_x:
        partitions.append(Partition(x + cut, y, "y", hy * 2.0, storey, door_offset))
        low = _split(x + cut - PARTITION_T * 0.5 - low_span * 0.5, y,
                     low_span * 0.5, hy, storey, rng, partitions, depth + 1, door_x,
                     stair_keep)
        high = _split(x + cut + PARTITION_T * 0.5 + high_span * 0.5, y,
                      high_span * 0.5, hy, storey, rng, partitions, depth + 1, door_x,
                      stair_keep)
    else:
        partitions.append(Partition(x, y + cut, "x", hx * 2.0, storey, door_offset))
        low = _split(x, y + cut - PARTITION_T * 0.5 - low_span * 0.5,
                     hx, low_span * 0.5, storey, rng, partitions, depth + 1, door_x,
                     stair_keep)
        high = _split(x, y + cut + PARTITION_T * 0.5 + high_span * 0.5,
                      hx, high_span * 0.5, storey, rng, partitions, depth + 1, door_x,
                      stair_keep)
    return low + high


def assign_kinds(rooms, storeys, door_x=0.0, upper_kind=None):
    """Name each room, so the furniture kit knows what to put in it.

    The room the front door opens into is the living room - that is the one the
    player sees first and it gets the hearth. Ground floor rooms after that are
    the kitchen and then a store. Everything upstairs is a bedroom except the
    one the stairs land in.
    """
    ground = sorted([r for r in rooms if r.storey == 0],
                    key=lambda r: abs(r.x - door_x) + abs(r.y + r.hy) * 0.1)
    for index, room in enumerate(ground):
        room.kind = ("living" if index == 0 else
                     "kitchen" if index == 1 else
                     "store")

    for storey in range(1, storeys):
        upper = sorted([r for r in rooms if r.storey == storey],
                       key=lambda r: -r.area)
        for index, room in enumerate(upper):
            if upper_kind:
                room.kind = upper_kind
            else:
                room.kind = "bedroom" if index < 2 else "store"
    return rooms


# ---------------------------------------------------------------------------
# Furnishing
# ---------------------------------------------------------------------------
# What goes in each kind of room, in the order it gets first pick of the walls.
# Just a name and a count: the size of a thing is a property of its geometry, and
# a hand-maintained table of dimensions beside it is a table that drifts. It did
# drift, and the result was a kitchen counter poking out through the front wall.
# _measure() reads the real bounds instead.
RECIPES = {
    "living":  [("hearth", 1), ("settle", 1), ("shelf", 1), ("chest", 1),
                ("firewood", 1), ("table", 1), ("chair", 2), ("rug", 1)],
    "kitchen": [("counter", 1), ("dresser", 1), ("shelf", 1), ("barrel_small", 1),
                ("sack", 2), ("table", 1), ("stool", 2), ("pot", 1)],
    "bedroom": [("bed", 1), ("wardrobe", 1), ("chest", 1), ("bedside", 1),
                ("washstand", 1), ("rug", 1)],
    "store":   [("shelf", 1), ("crate_small", 3), ("barrel_small", 2), ("sack", 2)],
    "shop":    [("counter", 1), ("shop_stand", 2), ("shelf", 2), ("crate_small", 2)],
    "office":  [("desk", 2), ("bookshelf", 2), ("shelf", 1), ("chair", 2)],
    "lobby":   [("settle", 2), ("shop_stand", 1), ("bookshelf", 1), ("rug", 1)],
}

# Newhaven's trades. Every one of these is somewhere in the city, because
# city._assign_venues hands the critical ones out before it hands out anything
# else - a city you can walk around ought to have a grocer and a dentist in it
# whether or not the dice felt like it.
VENUE_RECIPES = {
    "grocer":      [("produce_bin", 3), ("fridge_cabinet", 1), ("shelf", 2),
                    ("checkout", 1), ("crate_small", 2), ("sack", 2)],
    "clothier":    [("clothes_rail", 3), ("display_case", 1), ("shelf", 1),
                    ("checkout", 1), ("stool", 1)],
    "baker":       [("display_case", 2), ("counter", 1), ("shelf", 2),
                    ("sack", 2), ("checkout", 1)],
    "pharmacy":    [("display_case", 1), ("shelf", 3), ("counter", 1),
                    ("filing_cabinet", 1), ("checkout", 1)],
    "bookshop":    [("bookshelf", 4), ("table", 1), ("chair", 2), ("counter", 1)],
    "hardware":    [("tool_rack", 3), ("shelf", 2), ("crate_small", 3),
                    ("barrel_small", 2), ("checkout", 1)],
    "furniture":   [("sofa", 2), ("table", 1), ("chair", 2), ("wardrobe", 1),
                    ("dresser", 1), ("rug", 2)],
    "electronics": [("appliance", 4), ("shelf", 2), ("display_case", 1),
                    ("checkout", 1)],
    "restaurant":  [("table", 3), ("chair", 6), ("bar_counter", 1), ("shelf", 1),
                    ("potted_plant", 1)],
    "cafe":        [("bar_counter", 1), ("display_case", 1), ("table", 2),
                    ("chair", 4), ("shelf", 1)],
    "bar":         [("bar_counter", 1), ("stool", 4), ("shelf", 2),
                    ("barrel_small", 2), ("table", 1)],
    "barber":      [("barber_chair", 2), ("washstand", 1), ("display_case", 1),
                    ("waiting_bench", 1), ("shelf", 1)],
    "dentist":     [("dental_chair", 1), ("display_case", 1), ("washstand", 1),
                    ("waiting_bench", 1), ("filing_cabinet", 1)],
    "doctor":      [("exam_couch", 1), ("desk", 1), ("chair", 2),
                    ("filing_cabinet", 1), ("waiting_bench", 1), ("shelf", 1)],
    "optician":    [("display_case", 2), ("desk", 1), ("chair", 2),
                    ("shelf", 1), ("waiting_bench", 1)],
    "lawyer":      [("desk", 2), ("bookshelf", 3), ("filing_cabinet", 2),
                    ("chair", 2), ("waiting_bench", 1)],
    "bank":        [("counter", 2), ("filing_cabinet", 2), ("desk", 1),
                    ("chair", 2), ("potted_plant", 1)],
    "gym":         [("gym_bench", 3), ("waiting_bench", 1), ("shelf", 1),
                    ("washstand", 1)],
    "post":        [("counter", 1), ("shelf", 3), ("crate_small", 3),
                    ("filing_cabinet", 1)],
    "library":     [("bookshelf", 5), ("table", 2), ("chair", 4)],
    "school":      [("desk", 4), ("chair", 4), ("bookshelf", 2), ("lectern", 1)],
    "police":      [("reception_desk", 1), ("filing_cabinet", 2), ("desk", 1),
                    ("chair", 2), ("waiting_bench", 1)],
    "museum":      [("display_case", 3), ("potted_plant", 2), ("waiting_bench", 1)],
    "chapel":      [("pew", 4), ("altar", 1), ("lectern", 1)],
    "office_lobby": [("reception_desk", 1), ("sofa", 2), ("potted_plant", 2),
                     ("rug", 1)],
    "apartment_lobby": [("filing_cabinet", 1), ("sofa", 1), ("waiting_bench", 1),
                        ("potted_plant", 2), ("rug", 1)],
    "civic_hall":  [("lectern", 1), ("pew", 4), ("potted_plant", 2),
                    ("reception_desk", 1)],
    "workshop":    [("tool_rack", 2), ("counter", 1), ("crate_small", 3),
                    ("barrel_small", 2), ("shelf", 1)],
    "barn":        [("crate_small", 3), ("barrel_small", 3), ("sack", 4),
                    ("firewood", 2), ("shelf", 1), ("tool_rack", 1)],
}

# The floors above the ground floor. A tower has twenty-one of them and only one
# of them is ever on screen, so they are furnished to say what the floor is for
# and no further: dressing them like the ground floor cost 87,000 triangles a
# tower and looked no different from the one landing you can see.
SPARSE_RECIPES = {
    "office":  [("desk", 2), ("chair", 2), ("filing_cabinet", 1)],
    "bedroom": [("bed", 1), ("wardrobe", 1), ("bedside", 1)],
    "store":   [("crate_small", 2), ("barrel_small", 1)],
}

# What sits behind the shop floor. A restaurant needs a kitchen; a solicitor
# needs another office; everything else needs a stock room.
BACK_OF_HOUSE = {
    "restaurant": "kitchen", "cafe": "kitchen", "baker": "kitchen",
    "bar": "kitchen", "grocer": "store", "hardware": "store",
    "lawyer": "office", "bank": "office", "police": "office",
    "doctor": "doctor", "dentist": "doctor", "optician": "office",
    "office_lobby": "office", "apartment_lobby": "store",
    "civic_hall": "office", "library": "office", "school": "office",
    "museum": "store", "chapel": "store",
}

RECIPES.update(VENUE_RECIPES)

# Items that belong out on the floor rather than against a wall.
_FREESTANDING = ("table", "rug", "chair", "stool", "pot")

_MEASURED = {}


def _measure(item, seed=1):
    """The real bounding box of a piece of furniture, cached.

    Everything downstream - how far off the wall a thing sits, whether it fits
    between a doorway and a corner - is derived from this rather than declared,
    so geometry and layout cannot disagree.
    """
    key = item.__name__
    if key not in _MEASURED:
        scratch = MeshBuilder()
        item(scratch, seed=seed)
        xs = [v[0] for v in scratch.vertices]
        ys = [v[1] for v in scratch.vertices]
        zs = [v[2] for v in scratch.vertices]
        _MEASURED[key] = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
    return _MEASURED[key]


def _wall_slots(room, seed):
    """Positions around a room's walls, in a stable but not obvious order.

    Yields (along_axis, sign, offset) triples. The offsets march out from the
    middle of each wall so the first item placed on a wall is centred on it,
    which is what a hearth or a dresser wants.
    """
    rng = _SmallRng(seed * 17 + 3)
    walls = [("y", -1.0), ("x", 1.0), ("y", 1.0), ("x", -1.0)]
    # Rotate the starting wall per room so every house is not laid out the same.
    start = int(rng.next() * 4) % 4
    walls = walls[start:] + walls[:start]

    for axis, sign in walls:
        span = (room.hx if axis == "y" else room.hy) * 2.0
        for step in (0.0, -0.28, 0.28, -0.5, 0.5):
            offset = span * step
            if abs(offset) > span * 0.5 - 60.0:
                continue
            yield axis, sign, offset


def _wall_pose(room, axis, sign, offset, back):
    """Where an item sits with its back on one of a room's walls.

    ``back`` is how far the item's back face is behind its own origin, measured
    off the geometry - which is the only way to get a hearth, whose origin is not
    in the middle of it, to sit flush.
    """
    if axis == "x":
        if sign > 0:
            return room.x + room.hx - back, room.y + offset, 90.0
        return room.x - room.hx + back, room.y + offset, -90.0
    if sign < 0:
        return room.x + offset, room.y - room.hy + back, 0.0
    return room.x + offset, room.y + room.hy - back, 180.0


def furnish(mesh, room, seed, blocked=(), z=0.0, kind=None, glow=None,
            sparse=False):
    """Fill one room from its recipe, keeping clear of doorways and stairs.

    ``blocked`` is a list of (x, y, radius) circles in building-local
    coordinates. Anything whose slot falls inside one is skipped rather than
    nudged: a chair half in a doorway looks worse than a missing chair, and the
    player has to be able to get through.

    ``glow`` is a second MeshBuilder collecting emissive halves - the fire in a
    hearth. It has to be separate because a static mesh in this project carries
    exactly one material.
    """
    name = kind or room.kind
    recipe = (SPARSE_RECIPES.get(name) if sparse else None) or         RECIPES.get(name, RECIPES["store"])
    rng = _SmallRng(seed * 131 + 11)
    slots = list(_wall_slots(room, seed))
    used = []
    placed = 0

    def clear(px, py, radius):
        for (bx, by, br) in blocked:
            if (px - bx) ** 2 + (py - by) ** 2 < (radius + br) ** 2:
                return False
        for (ux, uy, ur) in used:
            if (px - ux) ** 2 + (py - uy) ** 2 < (radius + ur) ** 2:
                return False
        return True

    for (name, count) in recipe:
        item = globals().get(name)
        if item is None:
            continue
        min_x, max_x, min_y, max_y = _measure(item, seed)[:4]
        half_along = max(abs(min_x), abs(max_x))
        back = -min_y
        depth = max_y - min_y
        footprint = max(half_along, depth * 0.5)

        for copy in range(count):
            if name in _FREESTANDING:
                px, py = room.x, room.y
                if name in ("chair", "stool"):
                    px = room.x + (78.0 if copy % 2 == 0 else -78.0)
                elif count > 1:
                    px = room.x + (rng.next() - 0.5) * room.hx
                    py = room.y + (rng.next() - 0.5) * room.hy
                if (abs(px - room.x) + half_along > room.hx
                        or abs(py - room.y) + depth * 0.5 > room.hy):
                    continue
                if not clear(px, py, footprint * 0.6):
                    continue
                yaw = rng.next() * 360.0 if name == "rug" else (0.0 if copy % 2 else 180.0)
                place(mesh, item, (px, py, z), yaw, seed=seed + placed * 7)
                used.append((px, py, footprint * 0.5))
                placed += 1
                continue

            for slot in list(slots):
                axis, sign, offset = slot
                span = (room.hx if axis == "y" else room.hy) * 2.0
                limit = span * 0.5 - half_along - 14.0
                if limit < 0.0:
                    break                       # too wide for this room's walls
                clamped = max(-limit, min(limit, offset))
                px, py, yaw = _wall_pose(room, axis, sign, clamped, back)
                if not clear(px, py, footprint * 0.72):
                    continue
                place(mesh, item, (px, py, z), yaw, seed=seed + placed * 7)
                if name == "hearth" and glow is not None:
                    place(glow, hearth_fire, (px, py, z), yaw, seed=seed)
                used.append((px, py, footprint * 0.66))
                slots.remove(slot)
                placed += 1
                break
    return placed


def _cos(degrees):
    import math
    return math.cos(math.radians(degrees))


def _sin(degrees):
    import math
    return math.sin(math.radians(degrees))


# ---------------------------------------------------------------------------
# Assembly
# ---------------------------------------------------------------------------
def stair_head(mesh, stair, z, height=250.0):
    """The little hut on a flat roof that the stair comes up inside."""
    x0, x1, y0, y1 = stair.hole
    cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
    w = x1 - x0 + 44.0
    d = y1 - y0 + 44.0
    thick = 18.0
    # Three walls and a lid; the fourth side is the way out onto the roof.
    mesh.box((cx, cy + d * 0.5, z + height * 0.5), (w, thick, height),
             pal.CONCRETE_PALE)
    for sx in (-1.0, 1.0):
        mesh.box((cx + sx * w * 0.5, cy, z + height * 0.5), (thick, d, height),
                 pal.CONCRETE_PALE)
    mesh.box((cx, cy - d * 0.5, z + height - 30.0), (w, thick, 60.0),
             pal.CONCRETE_PALE)
    mesh.box((cx, cy, z + height + 10.0), (w + 30.0, d + 30.0, 20.0),
             pal.CONCRETE_GREY)


def floor_slab(mesh, half_x, half_y, z, colour, hole=None, thickness=FLOOR_T):
    """A floor at height z, optionally with a rectangular stairwell hole in it.

    The hole is cut the way a doorway is: by emitting the four rectangles that
    surround it rather than by subtracting anything.
    """
    if hole is None:
        mesh.box((0.0, 0.0, z - thickness * 0.5),
                 (half_x * 2.0, half_y * 2.0, thickness), colour)
        return

    x0, x1, y0, y1 = hole
    zc = z - thickness * 0.5
    for (ax, bx, ay, by) in ((-half_x, x0, -half_y, half_y),
                             (x1, half_x, -half_y, half_y),
                             (x0, x1, -half_y, y0),
                             (x0, x1, y1, half_y)):
        if bx - ax <= 1.0 or by - ay <= 1.0:
            continue
        mesh.box(((ax + bx) * 0.5, (ay + by) * 0.5, zc),
                 (bx - ax, by - ay, thickness), colour)


def _door_keepout(wall, door_w):
    """The keep-clear circle in front of an internal doorway."""
    if wall.axis == "x":
        return (wall.x + wall.door_offset, wall.y, door_w * 0.75)
    return (wall.x, wall.y + wall.door_offset, door_w * 0.75)


def lamp_points(width, depth, storeys, seed, base_z=gen_town.PLINTH_H,
                drop=115.0, wall_t=WALL_T, storey_h=STOREY_H):
    """Where the ceiling lamps hang, so the world can put real lights in them.

    fit_out() builds the fixtures but cannot place an actor, and a room lit by
    nothing is a black box: there is no static lighting in this project, the
    window panes are opaque, and Lumen has nothing to carry indoors through a
    sealed shell. So the town stage hangs a point light in each of these, and
    the emissive bulb above it is what the light appears to come from.

    Deterministic on the same seed as fit_out, so the two agree without fit_out
    having to hand anything back.

    ``drop`` puts the light *below* the shade, not in it. ceiling_lamp hangs a
    cord from 16 below the ceiling and a cone from 56 to 88 below that; a light
    inside that cone lights the cone, and forty-five thousand lumens twenty
    centimetres from an eight sided cone renders as a white jagged shape stuck
    to the ceiling that reads, convincingly, as a hole through to a snowy
    mountain. 115 clears the shade by about ten centimetres, so the shade is lit
    from underneath and throws its light down into the room, which is what a
    pendant does.
    """
    rooms, _partitions = plan(width, depth, storeys, seed, 0.0, wall_t)
    return [(room.x, room.y,
             base_z + (room.storey + 1) * storey_h - drop, room.storey)
            for room in rooms]


class _Stair(object):
    """Where a building's stair column stands, and what it displaces."""

    def __init__(self, x, y, width, steps, run, hole, keep_radius):
        self.x, self.y = x, y
        self.width, self.steps, self.run = width, steps, run
        self.hole = hole
        self.keep_radius = keep_radius


def stair_column(inner_hx, inner_hy, storey_h, seed):
    """Plant the stair against the +X wall, centred, and measure what it takes.

    Deterministic on the footprint alone. It has to be, because plan() needs to
    know where the stairwell is before it can lay rooms out around it, and the
    stair used to be positioned from the rooms - which only worked while there
    was exactly one flight and nothing above it.

    Risers are kept at or under 40 cm; the pawn steps 45.
    """
    steps = max(8, int(math.ceil(storey_h / 40.0)))
    run = 30.0
    width = 110.0
    scratch = MeshBuilder()
    stair_run(scratch, storey_h, width=width, steps=steps, run=run, seed=seed)
    min_x = min(v[0] for v in scratch.vertices)
    max_x = max(v[0] for v in scratch.vertices)
    min_y = min(v[1] for v in scratch.vertices)
    max_y = max(v[1] for v in scratch.vertices)

    # Far enough off the wall that the stair head - which is wider than the
    # stairwell, because it has walls of its own - still fits inside it.
    x = inner_hx - max_x - 58.0
    y = max(min(0.0, inner_hy - max_y), -inner_hy - min_y)
    # The hole clears the flight, not the handrail: a hole as wide as the rail
    # would eat the landing you step onto at the top.
    hole = (x - width * 0.5 - 14.0, x + width * 0.5 + 14.0,
            y + min_y, y + max_y)
    keep = (x + min_x - 30.0, x + max_x + 30.0, y + min_y - 30.0, y + max_y + 30.0)
    return _Stair(x, y, width, steps, run, hole,
                  max(width, steps * run) * 0.58), keep


def fit_out(width, depth, storeys, seed, base_z=gen_town.PLINTH_H, door_x=0.0,
            ground_kind=None, floor_colour=None, wall_colour=None,
            wall_t=WALL_T, storey_h=STOREY_H, ceiling=True, roof_hatch=False,
            upper_kind=None, stair_up=False, floor_hole=False):
    """The whole inside of one building, as (solid, emissive) mesh builders.

    Built in the building's own local frame with ``base_z`` at the top of its
    plinth, so the result drops straight onto the same actor transform as the
    shell it belongs in.

    Two meshes come back because a static mesh carries one material: fires and
    lamp bulbs go in the emissive one. The glow may come back empty for a
    building with no hearth, and the caller has to check before making an asset
    of it - meshkit refuses to build a mesh with no triangles.
    """
    floor_colour = floor_colour if floor_colour is not None else pal.FLOOR_BOARD
    wall_colour = wall_colour if wall_colour is not None else pal.PLASTER_WHITE
    # wall_t and storey_h are the shell's, not this module's: a Newhaven
    # shopfront has 34 cm walls and a 3.8 m ceiling, a cottage has 22 and 3.2,
    # and a fit-out that assumed the cottage's numbers would stand its furniture
    # a foot inside the city's walls.
    inner_hx = width * 0.5 - wall_t
    inner_hy = depth * 0.5 - wall_t

    solid = MeshBuilder()
    glow = MeshBuilder()

    stair, stair_keep = stair_column(inner_hx, inner_hy, storey_h, seed)
    rooms, partitions = plan(width, depth, storeys, seed, door_x, wall_t,
                             stair_keep if (storeys > 1 or roof_hatch or stair_up)
                             else None)
    assign_kinds(rooms, storeys, door_x, upper_kind)
    if ground_kind:
        # The business is the room the street door opens into. Rooms behind it
        # are its back of house - a kitchen behind a restaurant, a stock room
        # behind a grocer - because a shop with four identical shop floors in a
        # row reads as a warehouse, not a shop.
        behind = BACK_OF_HOUSE.get(ground_kind, "store")
        ground = sorted([r for r in rooms if r.storey == 0],
                        key=lambda r: abs(r.x - door_x) + abs(r.y + r.hy) * 0.1)
        for index, room in enumerate(ground):
            room.kind = ground_kind if index == 0 else behind

    # --- the stair column ----------------------------------------------------
    # One flight per storey, all in the same place, so they stack into
    # something you can actually climb from the street to the roof. The
    # position comes from the footprint rather than from the largest room,
    # because the rooms are laid out around the stair and not the other way
    # about - plan() is given the stairwell to keep its partitions out of.
    stair_hole = None
    blocked = {}
    if stair_up:
        # One floor of a tall building, built once and placed on every storey.
        # A twenty-two storey interior as a single mesh was 43,000 triangles and
        # a 48 MB build, and the editor died somewhere around the twentieth
        # tower; a floor is 1,500 triangles and gets culled on its own.
        place(solid, stair_run, (stair.x, stair.y, base_z), 0.0,
              rise=storey_h, width=stair.width, steps=stair.steps,
              run=stair.run, seed=seed)
        keep = (stair.x, stair.y, stair.keep_radius)
        blocked.setdefault(0, []).append(keep)
        stair_hole = stair.hole
    elif storeys > 1 or roof_hatch:
        for storey in range(storeys):
            z = base_z + storey * storey_h
            if storey == storeys - 1 and not roof_hatch:
                break
            place(solid, stair_run, (stair.x, stair.y, z), 0.0,
                  rise=storey_h, width=stair.width, steps=stair.steps,
                  run=stair.run, seed=seed + storey)
            keep = (stair.x, stair.y, stair.keep_radius)
            blocked.setdefault(storey, []).append(keep)
            blocked.setdefault(storey + 1, []).append(keep)
        stair_hole = stair.hole

    # --- floors and the ceiling ---------------------------------------------
    # Storey 0 is a board finish laid on the plinth, which is already solid, so
    # this is a surface rather than structure.
    # Every floor and ceiling runs TUCK past the inner face of the wall and dies
    # inside it. Ending one exactly on that face leaves a shared edge between
    # two perpendicular surfaces, and a shared edge rasterises as a hairline
    # that light comes through - at the top corners of a room, where the roof
    # space is on the other side, that reads as a sliver of sky in the ceiling.
    if floor_hole and stair_hole:
        floor_slab(solid, inner_hx + TUCK, inner_hy + TUCK, base_z + 8.0,
                   floor_colour, hole=stair_hole, thickness=8.0)
    else:
        solid.box((0.0, 0.0, base_z + 4.0),
                  ((inner_hx + TUCK) * 2.0, (inner_hy + TUCK) * 2.0, 8.0),
                  floor_colour)
    for storey in range(1, storeys):
        floor_slab(solid, inner_hx + TUCK, inner_hy + TUCK, base_z + storey * storey_h,
                   floor_colour, hole=stair_hole)
    if ceiling:
        solid.box((0.0, 0.0, base_z + storeys * storey_h - 7.0),
                  ((inner_hx + TUCK) * 2.0, (inner_hy + TUCK) * 2.0, 14.0),
                  pal.CEILING_PLASTER)
    elif roof_hatch:
        # The top slab is the roof, and the stair comes up through it into a
        # stair head that stands on it - which is what makes a roof somewhere
        # you can walk out onto rather than something you can see.
        floor_slab(solid, inner_hx + TUCK, inner_hy + TUCK,
                   base_z + storeys * storey_h, pal.CONCRETE_PALE, hole=stair_hole)
        stair_head(solid, stair, base_z + storeys * storey_h)

    # --- partitions ----------------------------------------------------------
    for wall in partitions:
        z = base_z + wall.storey * storey_h
        yaw = 0.0 if wall.axis == "x" else 90.0
        place(solid, partition, (wall.x, wall.y, z), yaw,
              length=wall.length, height=storey_h - 20.0, thickness=PARTITION_T,
              door_at=wall.door_offset, door_w=DOOR_W, door_h=DOOR_H,
              colour=wall_colour)
        blocked.setdefault(wall.storey, []).append(_door_keepout(wall, DOOR_W))

    # The front doorway has to stay walkable too, and one circle on the wall is
    # not enough: it keeps a chest off the threshold and lets a tool rack stand
    # a metre inside it, which in a room as small as a shed is the same thing as
    # a blocked door. Three overlapping circles make a corridor from the
    # threshold into the room, as wide as the opening and as long as the pawn
    # needs to get through and turn.
    for step in (0.0, 0.45, 0.9):
        blocked.setdefault(0, []).append(
            (door_x, -inner_hy + step * 200.0, 115.0))

    # --- furniture and lamps -------------------------------------------------
    for room in rooms:
        z = base_z + room.storey * storey_h
        furnish(solid, room, seed + room.storey * 13, blocked.get(room.storey, []),
                z, glow=glow, sparse=room.storey > 0 and storeys > 2)
        # The pendant hangs as far as the room can spare. ceiling_lamp is 46 cm
        # of shade below however far its cord drops, and the pawn is 180 tall:
        # in a 2.5 m shed the full 42 cm cord put the shade at head height, in
        # the middle of the floor, where you walk.
        lamp_z = base_z + (room.storey + 1) * storey_h - 16.0
        drop = max(2.0, min(42.0, storey_h - 16.0 - 46.0 - 200.0))
        place(solid, ceiling_lamp, (room.x, room.y, lamp_z), 0.0, seed=seed,
              drop=drop)
        place(glow, ceiling_lamp_glow, (room.x, room.y, lamp_z), 0.0, seed=seed,
              drop=drop)

    return solid, glow
