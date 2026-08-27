"""Single source of truth for Fairhaven's world layout.

Every distance is in Unreal units (1 uu = 1 cm) unless a name ends in ``_M``.
Axis convention used everywhere in this project:

    +X = North   (mountains)      -X = South (tropical lowland, then Newhaven)
    +Y = East    (ocean)          -Y = West  (farmland)

Change values here, re-run ``Tools/Terrain/generate_terrain.py``, then re-run the
content build. Nothing else hard-codes world geometry.
"""
from __future__ import annotations

import math

# ----------------------------------------------------------------------------
# Landscape resolution
# ----------------------------------------------------------------------------
# 32x32 components, 2 sections per component, 63 quads per section
# -> 4032 quads -> 4033 vertices per side -> 4.032 km square.
#
# Landscape sizes are not free-form: SIZE must be
# COMPONENT_COUNT * SECTIONS_PER_COMPONENT * QUADS_PER_SECTION + 1, which is why
# this grows by component count rather than by an arbitrary number. Doubling the
# side quadruples the area, the heightmap file, the scatter counts and the
# cooked map, so this is the knob to turn back first if a build gets too heavy.
SIZE = 4033
QUADS_PER_SECTION = 63
SECTIONS_PER_COMPONENT = 2
COMPONENT_COUNT = 32

QUAD_UU = 100.0          # horizontal unreal units per landscape quad
Z_SCALE = 200.0          # landscape actor Z scale; gives +/- 512 m of range
CENTER_INDEX = (SIZE - 1) // 2
EXTENT_UU = CENTER_INDEX * QUAD_UU          # 201600 uu from centre to edge
ORIGIN_UU = -EXTENT_UU                      # landscape actor XY location

SEED = 20260826

# Height in metres -> uint16 landscape sample.
#   height_uu = (h16 - 32768) / 128 * Z_SCALE  =>  h16 = 32768 + h_m * 64
H16_PER_METRE = 100.0 * 128.0 / Z_SCALE      # == 64.0

# Anything above this clips to white in the uint16 heightmap: (65535-32768)/64.
# Mountain peak plus ridge noise has to stay under it.
MAX_ENCODABLE_M = 512.0


def metres_to_h16(value_m: float) -> float:
    return 32768.0 + value_m * H16_PER_METRE


# ----------------------------------------------------------------------------
# Regions
# ----------------------------------------------------------------------------
TOWN_CENTER = (0.0, 0.0)
TOWN_RADIUS = 21000.0        # dense town out to here
TOWN_FALLOFF = 11000.0       # terrace blends out over this extra distance
TOWN_HEIGHT_M = 16.0

SEA_LEVEL_M = 0.0
COAST_Y = 30000.0            # base shoreline, before headland/shelf shaping
BEACH_WIDTH = 4200.0

# Mountains now build across the whole northern half rather than topping out a
# fifth of the way over. The peak stays well under MAX_ENCODABLE_M because
# ridge noise and detail stack on top of it.
MOUNTAIN_START_X = 22000.0
MOUNTAIN_FULL_X = 168000.0
MOUNTAIN_PEAK_M = 440.0
SNOW_LINE_M = 300.0

FARM_START_Y = -16000.0
FARM_FULL_Y = -112000.0

TROPICS_START_X = -26000.0
TROPICS_FULL_X = -150000.0

LAGOON_CENTER = (-64000.0, 14000.0)
LAGOON_RADIUS = 15000.0
LAGOON_DEPTH_M = -4.0

OCEAN_FLOOR_M = -70.0
OCEAN_FALLOFF = 120000.0

# The far south swings the coastline back out to the east, which is what makes
# the flat coastal shelf Newhaven stands on. Without it the southern half of the
# larger map would be open water.
SOUTH_SHELF_AMOUNT = 46000.0
SOUTH_SHELF_START_X = -88000.0
SOUTH_SHELF_FULL_X = -152000.0

# ----------------------------------------------------------------------------
# Newhaven: the procedural city on the southern shelf.
#
# Unlike the town, none of this is a hand-drawn street list. The grid, the
# blocks and the building heights all fall out of the numbers below, so moving
# the city is a one-line change and everything follows.
# ----------------------------------------------------------------------------
CITY_CENTER = (-125000.0, 17000.0)
CITY_RADIUS = 27000.0        # the street grid is clipped to this circle
CITY_FALLOFF = 16000.0       # terrace blends out over this extra distance
CITY_HEIGHT_M = 11.0
CITY_ANGLE = 0.16            # grid rotation, radians
CITY_BLOCK_U = 5200.0
CITY_BLOCK_V = 4200.0
CITY_STREET_WIDTH = 520.0
CITY_AVENUE_WIDTH = 900.0    # every third grid line is a wide avenue
CITY_AVENUE_EVERY = 3
CITY_CORE_RADIUS = 9500.0    # downtown: towers
CITY_MID_RADIUS = 18000.0    # mid-rise ring outside the core


def city_to_world(u: float, v: float):
    """Local city grid coordinates -> world XY."""
    ca, sa = math.cos(CITY_ANGLE), math.sin(CITY_ANGLE)
    return (CITY_CENTER[0] + u * ca - v * sa,
            CITY_CENTER[1] + u * sa + v * ca)


def _grid_counts():
    return (int(CITY_RADIUS / CITY_BLOCK_U), int(CITY_RADIUS / CITY_BLOCK_V))


def city_streets():
    """The street grid as (name, width_uu, [(x, y), ...]), clipped to a circle.

    The grid rotation is a pure rotation about the centre, so clipping a line to
    the city circle is just a chord length in local coordinates.
    """
    out = []
    nu, nv = _grid_counts()

    for i in range(-nu, nu + 1):
        u = i * CITY_BLOCK_U
        span = CITY_RADIUS * CITY_RADIUS - u * u
        if span <= 1.0:
            continue
        half = math.sqrt(span)
        width = CITY_AVENUE_WIDTH if i % CITY_AVENUE_EVERY == 0 else CITY_STREET_WIDTH
        out.append(("CityAve%02d" % (i + nu), width,
                    [city_to_world(u, -half), city_to_world(u, half)]))

    for j in range(-nv, nv + 1):
        v = j * CITY_BLOCK_V
        span = CITY_RADIUS * CITY_RADIUS - v * v
        if span <= 1.0:
            continue
        half = math.sqrt(span)
        width = CITY_AVENUE_WIDTH if j % CITY_AVENUE_EVERY == 0 else CITY_STREET_WIDTH
        out.append(("CitySt%02d" % (j + nv), width,
                    [city_to_world(-half, v), city_to_world(half, v)]))

    return out


def city_blocks():
    """The buildable land between the streets.

    ``ring`` is what decides how tall a block builds: 0 downtown, 1 mid-rise,
    2 outer. Keeping that here rather than in the placement stage makes the
    skyline a property of the layout instead of the spawner.
    """
    blocks = []
    nu, nv = _grid_counts()

    for i in range(-nu, nu):
        for j in range(-nv, nv):
            u = (i + 0.5) * CITY_BLOCK_U
            v = (j + 0.5) * CITY_BLOCK_V
            local_d = math.hypot(u, v)
            if local_d > CITY_RADIUS - CITY_BLOCK_V * 0.5:
                continue
            wx, wy = city_to_world(u, v)

            u_width = CITY_AVENUE_WIDTH if i % CITY_AVENUE_EVERY == 0 else CITY_STREET_WIDTH
            v_width = CITY_AVENUE_WIDTH if j % CITY_AVENUE_EVERY == 0 else CITY_STREET_WIDTH

            if local_d <= CITY_CORE_RADIUS:
                ring = 0
            elif local_d <= CITY_MID_RADIUS:
                ring = 1
            else:
                ring = 2

            blocks.append({
                "center": [round(wx, 1), round(wy, 1)],
                "u": round(CITY_BLOCK_U - u_width, 1),
                "v": round(CITY_BLOCK_V - v_width, 1),
                "angle_deg": round(math.degrees(CITY_ANGLE), 3),
                "ring": ring,
                "distance_uu": round(local_d, 1),
            })
    return blocks


# ----------------------------------------------------------------------------
# River: (worldX, worldY, elevation_m) from mountain source to the sea.
# ----------------------------------------------------------------------------
RIVER_POINTS = [
    (172000.0, 9000.0, 300.0),
    (156000.0, 6500.0, 246.0),
    (138000.0, 4000.0, 205.0),
    (120000.0, 3000.0, 178.0),
    (98000.0, 5000.0, 152.0),
    (86000.0, 2500.0, 106.0),
    (72000.0, 6000.0, 71.0),
    (58000.0, 11000.0, 47.0),
    (45000.0, 13000.0, 33.0),
    (33000.0, 14000.0, 25.0),
    (22000.0, 16000.0, 19.5),
    (12000.0, 19000.0, 15.0),
    (4000.0, 24000.0, 9.0),
    (-1000.0, 30500.0, 2.0),
    (-5000.0, 42000.0, -7.0),
]
RIVER_CHANNEL_WIDTH = 1500.0
RIVER_VALLEY_WIDTH = 13000.0

# ----------------------------------------------------------------------------
# Roads. Each entry is (name, width_uu, [(worldX, worldY), ...]).
# Elevation is sampled from the generated terrain and smoothed, so roads follow
# the land instead of fighting it.
# ----------------------------------------------------------------------------
ROADS = [
    ("CoastRoad", 620.0, [
        (52000.0, 46000.0), (40000.0, 38500.0), (28000.0, 31500.0),
        (17000.0, 26500.0), (7000.0, 24000.0), (-5000.0, 23000.0),
        (-19000.0, 22000.0), (-35000.0, 20500.0), (-51000.0, 18500.0),
        (-67000.0, 16500.0), (-84000.0, 14000.0),
    ]),
    ("MountainRoad", 560.0, [
        (3000.0, 7000.0), (14000.0, 13000.0), (26000.0, 16500.0),
        (38000.0, 17000.0), (50000.0, 15000.0), (62000.0, 12000.0),
        (74000.0, 9000.0), (86000.0, 6000.0), (98000.0, 3000.0),
        (112000.0, 1000.0), (128000.0, -1500.0), (146000.0, -3000.0),
        (164000.0, -2000.0),
    ]),
    ("FarmRoad", 540.0, [
        (-3000.0, -8000.0), (-9000.0, -24000.0), (-15000.0, -42000.0),
        (-22000.0, -60000.0), (-30000.0, -80000.0), (-36000.0, -97000.0),
        (-42000.0, -118000.0), (-48000.0, -140000.0), (-54000.0, -162000.0),
    ]),
    ("NorthLane", 460.0, [
        (9000.0, -9000.0), (20000.0, -24000.0), (30000.0, -40000.0),
        (40000.0, -56000.0), (50000.0, -70000.0), (62000.0, -88000.0),
        (74000.0, -108000.0),
    ]),
    ("SouthRoad", 540.0, [
        (-11000.0, 3000.0), (-27000.0, -1000.0), (-43000.0, -4000.0),
        (-59000.0, -6000.0), (-75000.0, -7000.0), (-92000.0, -9000.0),
        (-112000.0, -12000.0), (-134000.0, -14000.0), (-158000.0, -15000.0),
    ]),
    ("LagoonSpur", 440.0, [
        (-50000.0, 18800.0), (-57000.0, 16200.0), (-64000.0, 14500.0),
    ]),
    # The road that makes Newhaven reachable on foot from Fairhaven.
    ("CityHighway", 760.0, [
        (-84000.0, 14000.0), (-96000.0, 14500.0), (-108000.0, 15500.0),
        (-118000.0, 16500.0), (-125000.0, 17000.0),
    ]),
    ("CityCoastRoad", 620.0, [
        (-104000.0, 34000.0), (-116000.0, 38000.0), (-128000.0, 41000.0),
        (-142000.0, 43000.0), (-158000.0, 43000.0),
    ]),
]

# Inland ponds that break up the farmland. Water level is derived from the
# terrain at the centre when the heightmap is generated.
PONDS = [
    {"name": "MillPond", "center": (-13000.0, -37000.0), "radius": 4600.0, "depth_m": 4.0},
    {"name": "HollowPond", "center": (27000.0, -53000.0), "radius": 5400.0, "depth_m": 4.5},
    {"name": "WestMere", "center": (-46000.0, -104000.0), "radius": 7200.0, "depth_m": 5.5},
    {"name": "NorthTarn", "center": (58000.0, -96000.0), "radius": 5800.0, "depth_m": 4.8},
]

# Farmland parcel grid (rotated), used to paint rectangular fields.
FIELD_ANGLE = 0.34
FIELD_SIZE_U = 9000.0
FIELD_SIZE_V = 7000.0
FIELD_HEDGE = 0.045

# Town street grid, generated inside TOWN_RADIUS.
TOWN_STREET_WIDTH = 470.0
TOWN_STREETS = [
    [(-17000.0, -3000.0), (17000.0, -3000.0)],
    [(-15000.0, 9000.0), (15000.0, 9000.0)],
    [(-9000.0, -16000.0), (-9000.0, 17000.0)],
    [(4000.0, -17000.0), (4000.0, 19000.0)],
    [(-17000.0, 20000.0), (14000.0, 21500.0)],
]

# ----------------------------------------------------------------------------
# Landscape paint layers, in blend order.
# ----------------------------------------------------------------------------
LAYERS = ["Sand", "Grass", "Farm", "Jungle", "Dirt", "Rock", "Snow"]
