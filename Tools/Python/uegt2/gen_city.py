"""Generated buildings and street furniture for Newhaven.

The town is painted render and gable roofs; the city is concrete, glass and flat
tar. Everything here is still box primitives with vertex colours on the same
opaque material, so the city costs no extra draw calls and stays diffable.

Scale reference: the player is 1.8 m (180 uu). A city floor is 360 uu, so a
20-floor tower stands about 72 m and reads clearly over the 11 m terrace it is
built on.
"""
from __future__ import annotations

import math

from . import palette as pal
from .meshkit import MeshBuilder, _SmallRng, add

FLOOR_H = 360.0


def _pick(rng, items):
    return items[int(rng.next() * len(items)) % len(items)]


def _banded_shaft(mesh, centre, width, depth, floors, body, band,
                  floor_h=FLOOR_H, band_inset=18.0):
    """A tower shaft: one solid body plus a thin slab at every floor line.

    Cheaper than modelling windows: the horizontal banding alone is what makes a
    box read as a storeyed building at a distance, which is the only distance a
    tower is ever seen from.
    """
    cx, cy, cz = centre
    height = floors * floor_h
    mesh.box((cx, cy, cz + height * 0.5), (width, depth, height), body)

    for i in range(1, floors):
        z = cz + i * floor_h
        mesh.box((cx, cy, z), (width + band_inset, depth + band_inset, 26.0), band)


def _parapet(mesh, centre, width, depth, colour, height=90.0, thickness=40.0):
    """Roof edge wall, so a flat roof does not read as a cut-off box."""
    cx, cy, cz = centre
    z = cz + height * 0.5
    mesh.box((cx, cy - depth * 0.5, z), (width, thickness, height), colour)
    mesh.box((cx, cy + depth * 0.5, z), (width, thickness, height), colour)
    mesh.box((cx - width * 0.5, cy, z), (thickness, depth, height), colour)
    mesh.box((cx + width * 0.5, cy, z), (thickness, depth, height), colour)


def _rooftop_clutter(mesh, centre, width, depth, rng, mast=True):
    """Plant boxes, a stair head and an optional mast. Breaks the flat top."""
    cx, cy, cz = centre
    mesh.box((cx - width * 0.18, cy + depth * 0.16, cz + 110.0),
             (width * 0.30, depth * 0.28, 220.0), pal.CONCRETE_GREY)
    mesh.box((cx + width * 0.22, cy - depth * 0.18, cz + 70.0),
             (width * 0.22, depth * 0.22, 140.0), pal.METAL_IRON)
    if mast:
        mesh.cylinder((cx + width * 0.05, cy + depth * 0.02, cz),
                      18.0, 420.0 + rng.uniform(0.0, 260.0), pal.METAL_IRON, sides=5)


def _ground_floor(mesh, centre, width, depth, glass, frame, height=300.0):
    """A glazed shopfront band, so the base of a block is not a blank wall."""
    cx, cy, cz = centre
    mesh.box((cx, cy, cz + height * 0.5), (width * 0.96, depth * 0.96, height), glass)
    mesh.box((cx, cy, cz + height + 24.0), (width + 26.0, depth + 26.0, 48.0), frame)
    # Corner columns keep the glazing from reading as a floating band.
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            mesh.box((cx + sx * width * 0.5, cy + sy * depth * 0.5, cz + height * 0.5),
                     (70.0, 70.0, height), frame)


# ---------------------------------------------------------------------------
# Ground floors you can walk into
# ---------------------------------------------------------------------------
# Newhaven used to draw a solid glazed block at the base of every building and
# call it a shopfront. It read well from the pavement and it meant the whole
# city was a set of painted boxes. These are the numbers that turn that base
# into a room, and they are the city's half of the same contract gen_town owns
# for the houses.
CITY_WALL_T = 34.0        # a city wall is thicker than a cottage wall
CITY_DOOR_W = 170.0       # a shop door is wider than a front door
CITY_DOOR_H = 270.0
CITY_FOUNDATION = 300.0   # buried skirt, as deep as the worst city slope
GROUND_CLEAR = 12.0       # floor above the highest ground under the footprint


def _pane(mesh, glass, centre, size, colour):
    """A window pane. Goes to the glass mesh when there is one to go to."""
    (glass if glass is not None else mesh).box(centre, size, colour)


def _shopfront(mesh, width, depth, height, wall, frame, pane, glass=None,
               door=pal.DOOR_WOOD, base_z=0.0, sides=True):
    """A walkable ground floor: foundation, four walls, a door and a ceiling.

    The frontage is divided into bays; the middle one is the door and the rest
    are glazing. Panes go to ``glass`` so they end up on M_Glass and you can see
    both through them and, more to the point, so daylight can get past them.

    Returns the ceiling height in local Z, which is where the solid mass of the
    building above has to start.
    """
    half_w, half_d = width * 0.5, depth * 0.5
    top = base_z + height

    # Foundation. Solid, and its top is the floor, exactly as in a house.
    mesh.box((0.0, 0.0, base_z - CITY_FOUNDATION * 0.5),
             (width + 40.0, depth + 40.0, CITY_FOUNDATION), pal.CONCRETE_DARK)

    span = width - CITY_WALL_T * 2.0
    bays = max(3, int(span / 620.0))
    if bays % 2 == 0:
        bays += 1
    bay = span / bays
    win_w = max(160.0, bay - 110.0)
    sill = 95.0
    glass_h = max(120.0, height - sill - 95.0)

    front = [(0.0, CITY_DOOR_W, 0.0, CITY_DOOR_H)]
    for i in range(bays):
        offset = -span * 0.5 + bay * (i + 0.5)
        if abs(offset) < (CITY_DOOR_W + win_w) * 0.5 + 40.0:
            continue
        front.append((offset, win_w, sill, glass_h))

    back = [(o, w, s, h) for (o, w, s, h) in front[1:]]
    ends = []
    if sides:
        run = depth - CITY_WALL_T * 2.0
        lanes = max(1, int(run / 700.0))
        for i in range(lanes):
            ends.append((-run * 0.5 + run * (i + 0.5) / lanes,
                         min(win_w, run / lanes - 120.0), sill, glass_h))

    mesh.wall((0.0, -half_d + CITY_WALL_T * 0.5, base_z), "x", width, height,
              CITY_WALL_T, wall, openings=front)
    mesh.wall((0.0, half_d - CITY_WALL_T * 0.5, base_z), "x", width, height,
              CITY_WALL_T, wall, openings=back)
    for sx in (-1.0, 1.0):
        mesh.wall((sx * (half_w - CITY_WALL_T * 0.5), 0.0, base_z), "y",
                  depth - CITY_WALL_T * 2.0, height, CITY_WALL_T, wall,
                  openings=ends)

    target = glass if glass is not None else mesh
    for (offset, opening_w, s, h) in front[1:]:
        target.box((offset, -half_d + CITY_WALL_T * 0.5, base_z + s + h * 0.5),
                   (opening_w, 10.0, h), pane)
    for (offset, opening_w, s, h) in back:
        target.box((offset, half_d - CITY_WALL_T * 0.5, base_z + s + h * 0.5),
                   (opening_w, 10.0, h), pane)
    for (offset, opening_w, s, h) in ends:
        for sx in (-1.0, 1.0):
            target.box((sx * (half_w - CITY_WALL_T * 0.5), offset,
                        base_z + s + h * 0.5), (10.0, opening_w, h), pane)

    # Door reveal, threshold and a fascia over the frontage.
    for sx in (-1.0, 1.0):
        mesh.box((sx * (CITY_DOOR_W * 0.5 + 9.0), -half_d + CITY_WALL_T * 0.5,
                  base_z + CITY_DOOR_H * 0.5),
                 (18.0, CITY_WALL_T + 12.0, CITY_DOOR_H + 18.0), frame)
    mesh.box((0.0, -half_d + CITY_WALL_T * 0.5, base_z + CITY_DOOR_H + 9.0),
             (CITY_DOOR_W + 36.0, CITY_WALL_T + 12.0, 18.0), frame)
    mesh.box((0.0, -half_d + CITY_WALL_T * 0.5, base_z + 6.0),
             (CITY_DOOR_W + 24.0, CITY_WALL_T + 16.0, 14.0), frame)
    mesh.box((0.0, 0.0, top + 26.0), (width + 30.0, depth + 30.0, 52.0), frame)

    # Steps down to the pavement, buried on the flat like the town's.
    for step in range(1, 8):
        tread = base_z - 30.0 * step
        mesh.box((0.0, -half_d - 34.0 * (step - 0.5),
                  (tread + base_z - CITY_FOUNDATION) * 0.5),
                 (CITY_DOOR_W + 160.0, 34.0, tread - base_z + CITY_FOUNDATION),
                 pal.CONCRETE_DARK)

    # Ceiling slab. The mass above starts on top of this.
    mesh.box((0.0, 0.0, top - 9.0), (width, depth, 18.0), pal.CONCRETE_PALE)
    return top


# ---------------------------------------------------------------------------
# Buildings
# ---------------------------------------------------------------------------
def tower(seed=1, width=1800.0, depth=1600.0, floors=20, setback=True,
          glass=None):
    """Downtown high rise: glazed shaft, a setback near the top, roof plant."""
    rng = _SmallRng(seed)
    glazing = _pick(rng, [pal.GLASS_BLUE, pal.GLASS_TEAL, pal.GLASS_DARK,
                          pal.CURTAIN_WALL])
    band = _pick(rng, [pal.CONCRETE_PALE, pal.CONCRETE_GREY])

    mesh = MeshBuilder()

    # Podium: wider than the shaft, which is what stops a tower looking like a
    # rod stuck in the ground.
    podium_h = 380.0
    _shopfront(mesh, width + 260.0, depth + 260.0, podium_h, pal.CONCRETE_GREY,
               pal.CONCRETE_DARK, pal.GLASS_DARK, glass=glass)

    lower_floors = floors if not setback else int(floors * 0.68)
    _banded_shaft(mesh, (0.0, 0.0, podium_h), width, depth, lower_floors,
                  glazing, band)
    top_z = podium_h + lower_floors * FLOOR_H

    if setback:
        upper_floors = max(floors - lower_floors, 2)
        uw, ud = width * 0.68, depth * 0.68
        _parapet(mesh, (0.0, 0.0, top_z), width, depth, band, height=70.0)
        _banded_shaft(mesh, (0.0, 0.0, top_z), uw, ud, upper_floors, glazing, band)
        top_z = top_z + upper_floors * FLOOR_H
        _parapet(mesh, (0.0, 0.0, top_z), uw, ud, band)
        _rooftop_clutter(mesh, (0.0, 0.0, top_z), uw, ud, rng)
    else:
        _parapet(mesh, (0.0, 0.0, top_z), width, depth, band)
        _rooftop_clutter(mesh, (0.0, 0.0, top_z), width, depth, rng)

    return mesh


def office_block(seed=1, width=2100.0, depth=1700.0, floors=9, glass=None):
    """Mid-rise slab: solid facade with punched window bands."""
    rng = _SmallRng(seed)
    facade = _pick(rng, [pal.CONCRETE_PALE, pal.FACADE_SAND, pal.CONCRETE_GREY])
    glazing = _pick(rng, [pal.GLASS_BLUE, pal.GLASS_TEAL])

    mesh = MeshBuilder()
    base_h = 380.0
    _shopfront(mesh, width + 120.0, depth + 120.0, base_h, pal.CONCRETE_PALE,
               pal.CONCRETE_DARK, pal.GLASS_DARK, glass=glass)

    body_h = floors * FLOOR_H
    mesh.box((0.0, 0.0, base_h + body_h * 0.5), (width, depth, body_h), facade)

    # Continuous glazing bands on the long faces, recessed columns between.
    for i in range(floors):
        z = base_h + i * FLOOR_H + FLOOR_H * 0.58
        for sy in (-1.0, 1.0):
            _pane(mesh, glass, (0.0, sy * depth * 0.5, z),
                  (width * 0.86, 34.0, FLOOR_H * 0.44), glazing)
        for sx in (-1.0, 1.0):
            _pane(mesh, glass, (sx * width * 0.5, 0.0, z),
                  (34.0, depth * 0.8, FLOOR_H * 0.44), glazing)

    top_z = base_h + body_h
    _parapet(mesh, (0.0, 0.0, top_z), width, depth, facade, height=80.0)
    _rooftop_clutter(mesh, (0.0, 0.0, top_z), width, depth, rng, mast=False)
    return mesh


def apartment(seed=1, width=1700.0, depth=1500.0, floors=6, glass=None):
    """Residential block: brick or render, with balconies down the long faces."""
    rng = _SmallRng(seed)
    facade = _pick(rng, [pal.FACADE_BRICK, pal.FACADE_TERRA, pal.FACADE_SAND,
                         pal.WALL_CREAM, pal.CONCRETE_PALE])

    mesh = MeshBuilder()
    # The bottom storey is a lobby you can walk into, so the brick mass starts
    # on top of it rather than at the pavement.
    plinth = _shopfront(mesh, width + 90.0, depth + 90.0, FLOOR_H, facade,
                        pal.CONCRETE_DARK, pal.WINDOW_GLASS, glass=glass,
                        door=pal.DOOR_WOOD)

    floors = max(1, floors - 1)
    body_h = floors * FLOOR_H
    mesh.box((0.0, 0.0, plinth + body_h * 0.5), (width, depth, body_h), facade)

    for i in range(floors):
        z = plinth + i * FLOOR_H + FLOOR_H * 0.5
        for sy in (-1.0, 1.0):
            # Balcony slab plus its rail, alternating along the facade so the
            # elevation is not a perfect grid.
            if (i + (1 if sy > 0 else 0)) % 2 == 0:
                mesh.box((0.0, sy * (depth * 0.5 + 110.0), z - FLOOR_H * 0.28),
                         (width * 0.62, 220.0, 26.0), pal.CONCRETE_PALE)
                mesh.box((0.0, sy * (depth * 0.5 + 210.0), z - FLOOR_H * 0.15),
                         (width * 0.62, 22.0, 130.0), pal.METAL_IRON)
            _pane(mesh, glass, (0.0, sy * depth * 0.5, z),
                  (width * 0.72, 30.0, FLOOR_H * 0.34), pal.WINDOW_GLASS)
        for sx in (-1.0, 1.0):
            _pane(mesh, glass, (sx * width * 0.5, 0.0, z),
                  (30.0, depth * 0.5, FLOOR_H * 0.34), pal.WINDOW_GLASS)

    top_z = plinth + body_h
    _parapet(mesh, (0.0, 0.0, top_z), width, depth, facade, height=70.0)
    mesh.box((0.0, 0.0, top_z + 20.0), (width + 40.0, depth + 40.0, 40.0), pal.ROOF_TAR)
    return mesh


def shophouse(seed=1, width=1050.0, depth=1250.0, floors=3, glass=None):
    """Two or three storey street frontage: shop below, windows above, awning."""
    rng = _SmallRng(seed)
    facade = _pick(rng, [pal.WALL_OCHRE, pal.WALL_SAGE, pal.WALL_CREAM,
                         pal.FACADE_TERRA, pal.WALL_BLUE, pal.FACADE_SAND])
    awning = _pick(rng, [pal.AWNING_RED, pal.AWNING_GREEN, pal.CLOTH_BLUE, pal.CLOTH_YELLOW])

    mesh = MeshBuilder()
    shop_h = 330.0
    body_h = (floors - 1) * FLOOR_H

    mesh.box((0.0, 0.0, shop_h + body_h * 0.5), (width, depth, body_h), facade)
    _shopfront(mesh, width, depth, shop_h, facade, pal.WOOD_DARK,
               pal.GLASS_DARK, glass=glass)

    # Awning over the -Y frontage, which is the side facing the street.
    mesh.box((0.0, -depth * 0.5 - 150.0, shop_h + 40.0), (width * 0.9, 300.0, 26.0), awning)
    mesh.box((0.0, -depth * 0.5 - 290.0, shop_h - 40.0), (width * 0.9, 26.0, 120.0), awning)

    for i in range(floors - 1):
        z = shop_h + i * FLOOR_H + FLOOR_H * 0.52
        for sy in (-1.0, 1.0):
            mesh.box((0.0, sy * (depth * 0.5 + 8.0), z),
                     (width * 0.72, 22.0, FLOOR_H * 0.44), pal.WINDOW_FRAME)
            _pane(mesh, glass, (0.0, sy * depth * 0.5, z),
                  (width * 0.68, 30.0, FLOOR_H * 0.4), pal.WINDOW_GLASS)

    top_z = shop_h + body_h
    mesh.box((0.0, 0.0, top_z + 45.0), (width + 110.0, depth + 110.0, 90.0), pal.CONCRETE_PALE)
    return mesh


def city_hall(seed=1, glass=None):
    """The civic landmark on the central plaza: portico, wings and a dome."""
    rng = _SmallRng(seed)
    stone = pal.STONE_PALE
    mesh = MeshBuilder()

    width, depth = 3200.0, 1900.0
    steps_h = 130.0
    for i in range(3):
        inset = i * 90.0
        mesh.box((0.0, -depth * 0.5 - 330.0 + i * 110.0, steps_h * (i + 0.5) / 3.0),
                 (width * 0.7 - inset, 340.0 - i * 100.0, steps_h / 3.0), pal.STONE_DARK)

    body_h = 3.0 * FLOOR_H
    # The ground floor is the public hall. The two storeys of offices above it
    # stay solid, which is the same bargain the towers make: you can walk into
    # the building, not up it.
    hall_h = 460.0
    _shopfront(mesh, width, depth, hall_h, stone, pal.TRIM_WHITE,
               pal.WINDOW_GLASS, glass=glass, door=pal.DOOR_WOOD,
               base_z=steps_h)
    mesh.box((0.0, 0.0, steps_h + hall_h + (body_h - hall_h) * 0.5),
             (width, depth, body_h - hall_h), stone)

    # Wings, lower than the centre so the massing steps down.
    for sx in (-1.0, 1.0):
        mesh.box((sx * (width * 0.5 + 700.0), 0.0, steps_h + body_h * 0.34),
                 (1400.0, depth * 0.85, body_h * 0.68), pal.STONE_DARK)

    # Portico columns across the -Y front.
    for i in range(6):
        x = -width * 0.34 + i * (width * 0.68 / 5.0)
        mesh.cylinder((x, -depth * 0.5 - 240.0, steps_h), 74.0, body_h * 0.82,
                      pal.TRIM_WHITE, sides=8)
    mesh.box((0.0, -depth * 0.5 - 240.0, steps_h + body_h * 0.86),
             (width * 0.78, 520.0, body_h * 0.14), pal.TRIM_WHITE)
    mesh.prism((0.0, -depth * 0.5 - 240.0, steps_h + body_h),
               (width * 0.78, 520.0, 260.0), pal.TRIM_WHITE, yaw=90.0)

    top_z = steps_h + body_h
    _parapet(mesh, (0.0, 0.0, top_z), width, depth, stone, height=110.0)

    # Drum and dome.
    mesh.cylinder((0.0, 0.0, top_z), 620.0, 420.0, stone, sides=10)
    mesh.cylinder((0.0, 0.0, top_z + 420.0), 620.0, 90.0, pal.TRIM_WHITE, sides=10)
    mesh.cone((0.0, 0.0, top_z + 510.0), 600.0, 620.0, pal.METAL_COPPER, sides=10)
    mesh.cylinder((0.0, 0.0, top_z + 1130.0), 34.0, 200.0, pal.METAL_COPPER, sides=6)
    return mesh


def parking_deck(seed=1, width=2200.0, depth=1800.0, floors=4, glass=None):
    """Open-sided parking structure: cheap block filler that is clearly not housing."""
    mesh = MeshBuilder()
    plinth = 90.0
    mesh.box((0.0, 0.0, plinth * 0.5), (width + 60.0, depth + 60.0, plinth), pal.CONCRETE_DARK)

    for i in range(floors):
        z = plinth + i * FLOOR_H
        # Deck slab.
        mesh.box((0.0, 0.0, z + 40.0), (width, depth, 80.0), pal.CONCRETE_GREY)
        # Spandrel rail around the open edges.
        for sy in (-1.0, 1.0):
            mesh.box((0.0, sy * depth * 0.5, z + FLOOR_H * 0.42),
                     (width, 46.0, 150.0), pal.CONCRETE_PALE)
        for sx in (-1.0, 1.0):
            mesh.box((sx * width * 0.5, 0.0, z + FLOOR_H * 0.42),
                     (46.0, depth, 150.0), pal.CONCRETE_PALE)
        # Corner cores.
        for sx in (-1.0, 1.0):
            mesh.box((sx * width * 0.44, depth * 0.42, z + FLOOR_H * 0.5),
                     (240.0, 240.0, FLOOR_H), pal.CONCRETE_DARK)

    top_z = plinth + floors * FLOOR_H
    _parapet(mesh, (0.0, 0.0, top_z), width, depth, pal.CONCRETE_PALE, height=120.0)
    return mesh


# ---------------------------------------------------------------------------
# Street furniture
# ---------------------------------------------------------------------------
def traffic_light(seed=1):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 26.0, 620.0, pal.METAL_IRON, sides=6)
    mesh.box((0.0, 170.0, 640.0), (44.0, 340.0, 34.0), pal.METAL_IRON)
    housing = (0.0, 330.0, 560.0)
    mesh.box(housing, (110.0, 90.0, 300.0), pal.CONCRETE_DARK)
    for i, colour in enumerate((0xD0453A, 0xD8B23F, 0x4CA45C)):
        mesh.box(add(housing, (0.0, -50.0, 90.0 - i * 90.0)), (66.0, 26.0, 66.0), colour)
    return mesh


def city_lamp(seed=1):
    """Taller and plainer than the town lamp: a steel column with a curved head."""
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 34.0, 90.0, pal.CONCRETE_DARK, sides=8)
    mesh.cylinder((0.0, 0.0, 60.0), 22.0, 860.0, pal.METAL_IRON, sides=6)
    mesh.box((0.0, 120.0, 930.0), (36.0, 260.0, 30.0), pal.METAL_IRON, pitch=-12.0)
    mesh.box((0.0, 250.0, 900.0), (150.0, 300.0, 44.0), pal.METAL_IRON)
    return mesh


def city_lamp_glow(seed=1):
    mesh = MeshBuilder()
    mesh.box((0.0, 250.0, 872.0), (130.0, 270.0, 26.0), pal.LAMP_GLASS)
    return mesh


def kiosk(seed=1, glass=None):
    rng = _SmallRng(seed)
    roof = _pick(rng, [pal.AWNING_RED, pal.AWNING_GREEN, pal.CLOTH_BLUE])
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, 130.0), (420.0, 380.0, 260.0), pal.WOOD_DARK)
    (glass if glass is not None else mesh).box(
        (0.0, -190.0, 300.0), (360.0, 40.0, 200.0), pal.GLASS_DARK)
    mesh.box((0.0, 0.0, 420.0), (460.0, 420.0, 40.0), roof)
    mesh.box((0.0, -250.0, 400.0), (440.0, 140.0, 26.0), roof, pitch=8.0)
    return mesh


def bus_shelter(seed=1, glass=None):
    mesh = MeshBuilder()
    for sx in (-1.0, 1.0):
        mesh.cylinder((sx * 480.0, 130.0, 0.0), 22.0, 500.0, pal.METAL_IRON, sides=6)
    (glass if glass is not None else mesh).box(
        (0.0, 130.0, 200.0), (1020.0, 30.0, 400.0), pal.GLASS_DARK)
    mesh.box((0.0, 0.0, 520.0), (1080.0, 320.0, 34.0), pal.METAL_IRON)
    mesh.box((0.0, -110.0, 210.0), (900.0, 190.0, 40.0), pal.WOOD_PLANK)
    for sx in (-1.0, 1.0):
        mesh.box((sx * 400.0, -110.0, 100.0), (60.0, 160.0, 200.0), pal.METAL_IRON)
    return mesh


def fountain(seed=1):
    mesh = MeshBuilder()
    mesh.cylinder((0.0, 0.0, 0.0), 620.0, 90.0, pal.STONE_PALE, sides=12)
    mesh.cylinder((0.0, 0.0, 90.0), 560.0, 40.0, pal.WATER_POND, sides=12)
    mesh.cylinder((0.0, 0.0, 90.0), 150.0, 260.0, pal.STONE_DARK, sides=8)
    mesh.cylinder((0.0, 0.0, 350.0), 320.0, 50.0, pal.STONE_PALE, sides=10)
    mesh.cylinder((0.0, 0.0, 400.0), 60.0, 220.0, pal.STONE_DARK, sides=6)
    mesh.icosphere((0.0, 0.0, 640.0), 110.0, pal.STONE_PALE, subdivisions=1)
    return mesh


def planter_long(seed=1):
    mesh = MeshBuilder()
    mesh.box((0.0, 0.0, 90.0), (900.0, 300.0, 180.0), pal.CONCRETE_PALE)
    mesh.box((0.0, 0.0, 190.0), (820.0, 230.0, 40.0), pal.DIRT_BROWN)
    rng = _SmallRng(seed)
    for i in range(4):
        x = -300.0 + i * 200.0
        mesh.icosphere((x, rng.uniform(-40.0, 40.0), 250.0), 90.0, pal.BUSH_GREEN,
                       subdivisions=0, squash=0.75)
    return mesh
