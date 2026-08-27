"""Single source of truth for Fairhaven's world layout.

Every distance is in Unreal units (1 uu = 1 cm) unless a name ends in ``_M``.
Axis convention used everywhere in this project:

    +X = North   (mountains)      -X = South (tropical lowland)
    +Y = East    (ocean)          -Y = West  (farmland)

Change values here, re-run ``Tools/Terrain/generate_terrain.py``, then re-run the
content build. Nothing else hard-codes world geometry.
"""
from __future__ import annotations

# ----------------------------------------------------------------------------
# Landscape resolution
# ----------------------------------------------------------------------------
# 16x16 components, 2 sections per component, 63 quads per section
# -> 2016 quads -> 2017 vertices per side -> 2.016 km square.
SIZE = 2017
QUADS_PER_SECTION = 63
SECTIONS_PER_COMPONENT = 2
COMPONENT_COUNT = 16

QUAD_UU = 100.0          # horizontal unreal units per landscape quad
Z_SCALE = 200.0          # landscape actor Z scale; gives +/- 512 m of range
CENTER_INDEX = (SIZE - 1) // 2
EXTENT_UU = CENTER_INDEX * QUAD_UU          # 100800 uu from centre to edge
ORIGIN_UU = -EXTENT_UU                      # landscape actor XY location

SEED = 20260826

# Height in metres -> uint16 landscape sample.
#   height_uu = (h16 - 32768) / 128 * Z_SCALE  =>  h16 = 32768 + h_m * 64
H16_PER_METRE = 100.0 * 128.0 / Z_SCALE      # == 64.0


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
COAST_Y = 30000.0            # base shoreline, before headland/lagoon shaping
BEACH_WIDTH = 4200.0

MOUNTAIN_START_X = 22000.0
MOUNTAIN_FULL_X = 92000.0
MOUNTAIN_PEAK_M = 372.0
SNOW_LINE_M = 238.0

FARM_START_Y = -16000.0
FARM_FULL_Y = -58000.0

TROPICS_START_X = -26000.0
TROPICS_FULL_X = -78000.0

LAGOON_CENTER = (-64000.0, 14000.0)
LAGOON_RADIUS = 15000.0
LAGOON_DEPTH_M = -4.0

OCEAN_FLOOR_M = -46.0
OCEAN_FALLOFF = 62000.0

# ----------------------------------------------------------------------------
# River: (worldX, worldY, elevation_m) from mountain source to the sea.
# ----------------------------------------------------------------------------
RIVER_POINTS = [
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
        (74000.0, 9000.0), (86000.0, 6000.0), (95000.0, 2500.0),
    ]),
    ("FarmRoad", 540.0, [
        (-3000.0, -8000.0), (-9000.0, -24000.0), (-15000.0, -42000.0),
        (-22000.0, -60000.0), (-30000.0, -80000.0), (-36000.0, -97000.0),
    ]),
    ("NorthLane", 460.0, [
        (9000.0, -9000.0), (20000.0, -24000.0), (30000.0, -40000.0),
        (40000.0, -56000.0), (50000.0, -70000.0),
    ]),
    ("SouthRoad", 540.0, [
        (-11000.0, 3000.0), (-27000.0, -1000.0), (-43000.0, -4000.0),
        (-59000.0, -6000.0), (-75000.0, -7000.0), (-92000.0, -9000.0),
    ]),
    ("LagoonSpur", 440.0, [
        (-50000.0, 18800.0), (-57000.0, 16200.0), (-64000.0, 14500.0),
    ]),
]

# Inland ponds that break up the farmland. Water level is derived from the
# terrain at the centre when the heightmap is generated.
PONDS = [
    {"name": "MillPond", "center": (-13000.0, -37000.0), "radius": 4600.0, "depth_m": 4.0},
    {"name": "HollowPond", "center": (27000.0, -53000.0), "radius": 5400.0, "depth_m": 4.5},
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
