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
                     depth=14.0, glass=None):
    """Punch a row of framed windows onto one wall face.

    ``axis`` is 'x' for a wall facing +/-X, 'y' for +/-Y. Windows are proud of
    the wall rather than recessed: cheaper, and it reads well at low poly.

    ``glass`` takes the panes if it is given. They have to leave this mesh
    because a static mesh carries one material and glass is the one thing in
    this project that is not opaque; the panes become their own asset on
    M_Glass, on the same transform.
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
        (glass if glass is not None else mesh).box(pane_pos, pane_size, pane)


def _door(mesh, centre, axis, colour, width=90.0, height=200.0, depth=16.0):
    if axis == "x":
        mesh.box(add(centre, (0.0, 0.0, height * 0.5)), (depth, width + 20.0, height + 14.0), pal.TRIM_WHITE)
        mesh.box(add(centre, (depth * 0.4, 0.0, height * 0.5)), (depth * 0.7, width, height), colour)
    else:
        mesh.box(add(centre, (0.0, 0.0, height * 0.5)), (width + 20.0, depth, height + 14.0), pal.TRIM_WHITE)
        mesh.box(add(centre, (0.0, depth * 0.4, height * 0.5)), (width, depth * 0.7, height), colour)


# Interior geometry that the shell and the fit-out both have to agree on. The
# fit-out is generated separately in gen_interior.py, so these are the contract
# between the two.
WALL_T = 22.0            # exterior wall panel thickness
PLINTH_H = 26.0          # the plinth top is the ground floor
STOREY_H = 320.0
DOOR_W = 120.0           # the pawn is 68 across
DOOR_H = 215.0           # the pawn is 180 tall
WIN_W = 100.0
WIN_H = 118.0

# The plinth is a buried foundation, not a doorstep. town._place_streets sets a
# house so its floor clears the *highest* ground under the footprint, which
# leaves the downhill side hanging; this is the skirt of stone that fills the
# gap. 240 covers the worst lot in the generated town, where the ground falls
# 217 cm across one footprint - measured, not guessed, because a foundation one
# centimetre too shallow is a house you can see under.
FOUNDATION_D = 240.0

# The flight down from the door. The steepest doorstep in town is 148 cm above
# the ground outside it, so seven risers of 26 reach it with room to spare, and
# 26 is well inside the pawn's 45 cm step. On a level plot every step but the
# top one is underground.
STEP_RISE = 26.0
STEP_GOING = 30.0
STEP_COUNT = 7


def _front_windows(width):
    """Where the two front windows sit, kept clear of the door between them.

    The door is 120 wide at the centre and a window is 100 wide, so the nearest a
    window can sit is 60 + 50 + a 30 cm pier. On the narrowest cottage that is
    most of the way to the corner, which is why this is computed rather than
    written as a fraction of the width.
    """
    nearest = DOOR_W * 0.5 + WIN_W * 0.5 + 30.0
    furthest = width * 0.5 - WALL_T - WIN_W * 0.5 - 25.0
    if furthest < nearest:
        return 0.0                     # too narrow for a window beside the door
    return max(nearest, min(furthest, width * 0.27))


def _hollow_walls(mesh, width, depth, height, base_z, colour, front=(), sides=(),
                  back=(), thickness=WALL_T, floor=True, foundation=200.0,
                  offset_y=0.0):
    """Four wall panels and a floor, in place of a solid box.

    The same shape as house(), lifted out so the barn, the church, the warehouse
    and the shed can be walked into as well. ``front`` is the -Y face, which is
    the one every building in this project puts its door on.
    """
    half_w, half_d = width * 0.5, depth * 0.5
    if floor:
        mesh.box((0.0, offset_y, base_z - foundation * 0.5),
                 (width + 30.0, depth + 30.0, foundation), pal.WALL_STONE)
    mesh.wall((0.0, offset_y - half_d + thickness * 0.5, base_z), "x", width,
              height, thickness, colour, openings=front)
    mesh.wall((0.0, offset_y + half_d - thickness * 0.5, base_z), "x", width,
              height, thickness, colour, openings=back)
    for sx in (-1.0, 1.0):
        mesh.wall((sx * (half_w - thickness * 0.5), offset_y, base_z), "y",
                  depth - thickness * 2.0, height, thickness, colour,
                  openings=sides)


def house(seed=1, width=760.0, depth=580.0, storeys=1, wall=None, roof=None,
          door=None, chimney=True, porch=False, glass=None):
    """The workhorse town building - and, since interiors, a hollow one.

    The walls are four panels with real openings punched through them rather
    than one solid box, so the door is a hole you can walk through and the
    windows let Lumen carry daylight in. The inside itself - floors, stairs,
    partitions and furniture - is a separate mesh from gen_interior.fit_out()
    placed on the same transform, so it can be culled at short range while the
    shell stays visible across the valley.
    """
    rng = _SmallRng(seed)
    wall = wall if wall is not None else rng_choice(rng, [
        pal.WALL_CREAM, pal.WALL_WHITE, pal.WALL_OCHRE, pal.WALL_SAGE,
        pal.WALL_BLUE, pal.WALL_TERRACOTTA])
    roof = roof if roof is not None else rng_choice(rng, [
        pal.ROOF_RED, pal.ROOF_SLATE, pal.ROOF_BROWN, pal.ROOF_TEAL])
    door = door if door is not None else rng_choice(rng, [
        pal.DOOR_BLUE, pal.DOOR_RED, pal.DOOR_GREEN, pal.DOOR_WOOD])

    mesh = MeshBuilder()
    wall_h = STOREY_H * storeys
    half_w, half_d = width * 0.5, depth * 0.5

    # The foundation. Its top is the ground floor - solid, so the player stands
    # on it without needing a slab of its own - and it runs deep enough below
    # that to stay buried on the steepest plot in town.
    mesh.box((0.0, 0.0, PLINTH_H - FOUNDATION_D * 0.5),
             (width + 30.0, depth + 30.0, FOUNDATION_D), pal.WALL_STONE)

    # --- openings -----------------------------------------------------------
    win_x = _front_windows(width)
    front = [(0.0, DOOR_W, 0.0, DOOR_H)]
    back = []
    ends = []
    for storey in range(storeys):
        sill = storey * STOREY_H + (110.0 if storey == 0 else 120.0)
        if win_x > 0.0:
            for side in (-1.0, 1.0):
                front.append((side * win_x, WIN_W, sill, WIN_H))
                back.append((side * win_x, WIN_W, sill, WIN_H))
        if depth > 420.0:
            ends.append((0.0, WIN_W, sill, WIN_H))

    # --- the four wall panels -----------------------------------------------
    mesh.wall((0.0, -half_d + WALL_T * 0.5, PLINTH_H), "x", width, wall_h,
              WALL_T, wall, openings=front)
    mesh.wall((0.0, half_d - WALL_T * 0.5, PLINTH_H), "x", width, wall_h,
              WALL_T, wall, openings=back)
    for sx in (-1.0, 1.0):
        mesh.wall((sx * (half_w - WALL_T * 0.5), 0.0, PLINTH_H), "y",
                  depth - WALL_T * 2.0, wall_h, WALL_T, wall, openings=ends)

    # --- glazing and the door -----------------------------------------------
    for (offset, opening_w, sill, opening_h) in front[1:]:
        _glaze(mesh, (offset, -half_d + WALL_T * 0.5, PLINTH_H + sill), "x",
               opening_w, opening_h, glass=glass)
    for (offset, opening_w, sill, opening_h) in back:
        _glaze(mesh, (offset, half_d - WALL_T * 0.5, PLINTH_H + sill), "x",
               opening_w, opening_h, glass=glass)
    for (offset, opening_w, sill, opening_h) in ends:
        for sx in (-1.0, 1.0):
            _glaze(mesh, (sx * (half_w - WALL_T * 0.5), offset, PLINTH_H + sill),
                   "y", opening_w, opening_h, glass=glass)

    _doorway(mesh, (0.0, -half_d + WALL_T * 0.5, PLINTH_H), door)

    # Steps down from the doorway to whatever the ground is doing out there.
    # Each one is a block down to the bottom of the foundation rather than a
    # tread on legs, so nothing shows underneath where the hill falls away.
    # They start beyond the porch on the houses that have one, because the porch
    # deck is already at floor level.
    step_y = -half_d - (150.0 if porch else 0.0)
    for step in range(1, STEP_COUNT + 1):
        top = PLINTH_H - STEP_RISE * step
        bottom = PLINTH_H - FOUNDATION_D
        mesh.box((0.0, step_y - STEP_GOING * (step - 0.5), (top + bottom) * 0.5),
                 (DOOR_W + 120.0, STEP_GOING, top - bottom), pal.WALL_STONE)

    # Gable roof ridged along X, overhanging the walls.
    roof_h = 150.0 + 60.0 * storeys
    mesh.prism((0.0, 0.0, PLINTH_H + wall_h), (width + 70.0, depth + 70.0, roof_h),
               roof)

    if chimney:
        cx = width * rng.uniform(0.2, 0.34)
        mesh.box((cx, depth * 0.16, PLINTH_H + wall_h + roof_h * 0.66),
                 (90.0, 90.0, roof_h * 1.35), pal.BRICK_RED)
        mesh.box((cx, depth * 0.16, PLINTH_H + wall_h + roof_h * 1.34),
                 (110.0, 110.0, 26.0), pal.STONE_DARK)

    if porch:
        porch_d = 150.0
        # Flush with the floor inside, and thick enough downward that the hill
        # does not show under it.
        mesh.box((0.0, -half_d - porch_d * 0.5, PLINTH_H - 60.0),
                 (width * 0.55, porch_d, 120.0), pal.WOOD_PLANK)
        for sx in (-1.0, 1.0):
            mesh.box((sx * width * 0.24, -half_d - porch_d * 0.85, PLINTH_H + 120.0),
                     (28.0, 28.0, 230.0), pal.WOOD_DARK)
        mesh.box((0.0, -half_d - porch_d * 0.5, PLINTH_H + 245.0),
                 (width * 0.6, porch_d + 40.0, 22.0), roof)
    return mesh


def _glaze(mesh, centre, axis, opening_w, opening_h,
           pane=pal.WINDOW_GLASS, frame=pal.WINDOW_FRAME, glass=None):
    """Fill a window opening: a pane in the reveal, a frame on both faces.

    The pane sits *in* the wall rather than standing proud of it, so a window
    reads as a window from inside the room as well as from the street, and it
    goes into ``glass`` when one is given so it can be genuinely transparent.
    """
    cx, cy, cz = centre
    z = cz + opening_h * 0.5
    target = glass if glass is not None else mesh
    if axis == "x":
        target.box((cx, cy, z), (opening_w, 8.0, opening_h), pane)
        for sy in (-1.0, 1.0):
            mesh.box((cx, cy + sy * 13.0, z),
                     (opening_w + 18.0, 6.0, opening_h + 18.0), frame)
        mesh.box((cx, cy - 15.0, cz - 5.0), (opening_w + 34.0, 22.0, 12.0), frame)
    else:
        target.box((cx, cy, z), (8.0, opening_w, opening_h), pane)
        for sx in (-1.0, 1.0):
            mesh.box((cx + sx * 13.0, cy, z),
                     (6.0, opening_w + 18.0, opening_h + 18.0), frame)


def _doorway(mesh, centre, colour):
    """The lining and leaf standing in a real door opening."""
    cx, cy, cz = centre
    # Reveal linings, so the wall does not read as paper thin from the side.
    for sx in (-1.0, 1.0):
        mesh.box((cx + sx * (DOOR_W * 0.5 + 7.0), cy, cz + DOOR_H * 0.5),
                 (14.0, WALL_T + 10.0, DOOR_H + 14.0), pal.TRIM_WHITE)
    mesh.box((cx, cy, cz + DOOR_H + 7.0),
             (DOOR_W + 28.0, WALL_T + 10.0, 14.0), pal.TRIM_WHITE)
    # No leaf here on purpose. The opening has to stay empty, because collision
    # is complex-as-simple and a leaf modelled across it is a house nobody can
    # walk into. Every house now gets an interactable AUEGT2Door hung in this
    # opening by the gameplay stage instead, which swings and can be shut.
    # A threshold, so the join between the boards inside and the stoop outside
    # is not a visible seam.
    mesh.box((cx, cy, cz + 5.0), (DOOR_W + 20.0, WALL_T + 14.0, 12.0), colour)


def rng_choice(rng, items):
    return items[int(rng.next() * len(items)) % len(items)]


def barn(seed=1, width=1150.0, depth=820.0, glass=None):
    rng = _SmallRng(seed)
    mesh = MeshBuilder()
    wall_h = 430.0
    mesh.box((0.0, 0.0, 14.0), (width + 30.0, depth + 30.0, 28.0), pal.STONE_DARK)
    # A barn is a big shed, and now a walk-in one: the doorway is the full
    # height of the sliding doors, which hang slid open against the wall beside
    # it rather than across it.
    _hollow_walls(mesh, width, depth, wall_h, 28.0, pal.WALL_RED,
                  front=[(0.0, 430.0, 0.0, 380.0)],
                  sides=[(0.0, 150.0, wall_h * 0.55, 150.0)])

    # Gambrel-ish roof: two stacked prisms give the barn silhouette.
    mesh.prism((0.0, 0.0, 28.0 + wall_h), (width + 60.0, depth + 60.0, 170.0), pal.ROOF_BROWN)
    mesh.prism((0.0, 0.0, 28.0 + wall_h + 150.0), (width + 20.0, depth * 0.62, 230.0), pal.ROOF_BROWN)

    # Big sliding doors and white trim boards.
    mesh.box((-width * 0.5 + 220.0, -depth * 0.5 - 18.0, 28.0 + 190.0),
             (420.0, 22.0, 380.0), pal.WOOD_DARK)
    mesh.box((-width * 0.5 + 220.0, -depth * 0.5 - 26.0, 28.0 + 190.0),
             (24.0, 14.0, 380.0), pal.TRIM_WHITE)
    mesh.box((0.0, -depth * 0.5 - 16.0, 28.0 + 396.0), (width * 0.86, 26.0, 18.0),
             pal.METAL_IRON)
    for sx in (-1.0, 1.0):
        mesh.box((sx * width * 0.42, -depth * 0.5 - 6.0, 28.0 + wall_h * 0.5),
                 (26.0, 14.0, wall_h), pal.TRIM_WHITE)
    _windows_on_wall(mesh, (width * 0.5, 0.0, 28.0), depth, wall_h, "x", 2,
                     seed, sill_z=wall_h * 0.55, glass=glass)
    return mesh


def church(seed=1, glass=None):
    mesh = MeshBuilder()
    nave_w, nave_d, wall_h = 700.0, 1350.0, 520.0
    mesh.box((0.0, 0.0, 20.0), (nave_w + 40.0, nave_d + 40.0, 40.0), pal.WALL_STONE)
    # The nave is a room. Its door is at the -Y end, under the tower, which is
    # where the tower's own doorway already is.
    _hollow_walls(mesh, nave_w, nave_d, wall_h, 40.0, pal.WALL_STONE,
                  front=[(0.0, 200.0, 0.0, 300.0)], thickness=40.0)
    mesh.prism((0.0, 0.0, 40.0 + wall_h), (nave_w + 60.0, nave_d + 60.0, 300.0), pal.ROOF_SLATE)

    # Tower at the -Y end with a spire.
    tower = 380.0
    tower_h = 900.0
    ty = -nave_d * 0.5 - tower * 0.35
    # The tower is a porch at the bottom and solid above, so the way in runs
    # through it into the nave.
    _tower_shift = ty
    _hollow_walls(mesh, tower, tower, 320.0, 40.0, pal.WALL_STONE,
                  front=[(0.0, 160.0, 0.0, 280.0)],
                  back=[(0.0, 200.0, 0.0, 300.0)], thickness=40.0, floor=False,
                  offset_y=_tower_shift)
    mesh.box((0.0, ty, 40.0 + 320.0 + (tower_h - 320.0) * 0.5),
             (tower, tower, tower_h - 320.0), pal.WALL_STONE)
    mesh.box((0.0, ty, 40.0 + 310.0), (tower, tower, 20.0), pal.WALL_STONE)
    mesh.box((0.0, ty, 40.0 + tower_h + 24.0), (tower + 60.0, tower + 60.0, 48.0), pal.STONE_DARK)
    mesh.cone((0.0, ty, 40.0 + tower_h + 48.0), tower * 0.72, 460.0, pal.ROOF_TEAL, sides=4, yaw=45.0)
    mesh.box((0.0, ty, 40.0 + tower_h + 540.0), (24.0, 24.0, 90.0), pal.METAL_COPPER)

    _door(mesh, (0.0, ty - tower * 0.5, 40.0), "y", pal.DOOR_WOOD, width=140.0, height=260.0)
    for side in (-1.0, 1.0):
        _windows_on_wall(mesh, (side * nave_w * 0.5, 0.0, 40.0), nave_d, wall_h, "x",
                         4, seed, sill_z=wall_h * 0.36, pane=pal.LAMP_GLASS,
                         glass=glass)
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


def warehouse(seed=1, width=1000.0, depth=700.0, glass=None):
    mesh = MeshBuilder()
    wall_h = 420.0
    mesh.box((0.0, 0.0, 16.0), (width + 24.0, depth + 24.0, 32.0), pal.STONE_DARK)
    _hollow_walls(mesh, width, depth, wall_h, 32.0, pal.WALL_TIMBER,
                  front=[(0.0, 390.0, 0.0, 350.0)])
    mesh.prism((0.0, 0.0, 32.0 + wall_h), (width + 50.0, depth + 50.0, 190.0), pal.ROOF_SLATE)
    mesh.box((-width * 0.5 + 200.0, -depth * 0.5 - 20.0, 32.0 + 180.0),
             (380.0, 20.0, 360.0), pal.WOOD_DARK)
    _windows_on_wall(mesh, (0.0, depth * 0.5, 32.0), width, wall_h, "y", 3, seed,
                     sill_z=wall_h * 0.5, glass=glass)
    return mesh


def shed(seed=1, width=380.0, depth=320.0):
    mesh = MeshBuilder()
    wall_h = 250.0
    _hollow_walls(mesh, width, depth, wall_h, 0.0, pal.WOOD_PLANK,
                  front=[(0.0, 120.0, 0.0, 200.0)], thickness=16.0,
                  foundation=120.0)
    # Single-pitch roof: a thin slab tilted about its own centre.
    mesh.box((0.0, 0.0, wall_h + 40.0), (width + 60.0, depth + 60.0, 26.0),
             pal.ROOF_BROWN, roll=-12.0)
    # Lining only. A leaf across the opening is a shed nobody can get into,
    # because collision here is complex-as-simple.
    for sx in (-1.0, 1.0):
        mesh.box((sx * 68.0, -depth * 0.5 + 8.0, 100.0), (16.0, 24.0, 210.0),
                 pal.TRIM_WHITE)
    mesh.box((0.0, -depth * 0.5 + 8.0, 208.0), (152.0, 24.0, 16.0), pal.TRIM_WHITE)
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
