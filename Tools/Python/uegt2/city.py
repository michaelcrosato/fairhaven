"""Places Newhaven: the procedural city on the southern shelf.

The terrain generator owns the layout. It carves the street grid and exports the
buildable blocks into world_features.json, and this stage fills them in. That
split is the same contract the town uses, and it is what keeps the streets and
the buildings agreeing about where the roadway is.

Blocks are filled from the edges inward, facing the street, and thinned by the
same overlap rejection the town uses. That single rule is what produces the
shape of the city without any special cases: downtown blocks are deep enough for
only one row of towers, so they get one; outer blocks hold shophouses on all
four sides, so they get a full perimeter.
"""
from __future__ import annotations

import math

import unreal

from . import ctx
from .meshkit import _SmallRng
from .town import Placer, _coast_y_at, _face_yaw, _walk

LABEL_PREFIX = "City "

# (width along the frontage, depth back from the street, rejection radius).
# Depth includes anything that sticks out: balconies, awnings, podiums.
CITY_BUILDINGS = {
    "SM_Tower_A":       (2060.0, 1860.0, 890.0),
    "SM_Tower_B":       (2260.0, 2010.0, 960.0),
    "SM_Tower_C":       (1860.0, 1760.0, 800.0),
    "SM_Tower_D":       (2460.0, 1760.0, 1010.0),
    "SM_Office_A":      (2220.0, 1820.0, 900.0),
    "SM_Office_B":      (2520.0, 1720.0, 1010.0),
    "SM_Apartment_A":   (1790.0, 1940.0, 830.0),
    "SM_Apartment_B":   (1990.0, 1840.0, 870.0),
    "SM_Apartment_C":   (1590.0, 1740.0, 750.0),
    "SM_Shophouse_A":   (1160.0, 1600.0, 620.0),
    "SM_Shophouse_B":   (1060.0, 1500.0, 580.0),
    "SM_ParkingDeck_A": (2260.0, 1860.0, 910.0),
    # Depth is 3410 not 1900: the portico and the entrance steps stand well
    # proud of the main block, and measuring the body alone put the columns
    # out in the avenue.
    "SM_CityHall_A":    (6000.0, 3410.0, 2600.0),
}

# What each ring is allowed to build. Ring 0 is downtown.
RING_CHOICES = {
    0: ["SM_Tower_B", "SM_Tower_D", "SM_Tower_A", "SM_Tower_A", "SM_Office_B",
        "SM_Tower_C"],
    1: ["SM_Office_A", "SM_Office_B", "SM_Apartment_B", "SM_Tower_C",
        "SM_Apartment_A", "SM_ParkingDeck_A"],
    2: ["SM_Shophouse_A", "SM_Shophouse_B", "SM_Apartment_C", "SM_Apartment_A",
        "SM_Shophouse_A", "SM_Shophouse_B"],
}


def _face_out(out_x, out_y):
    """Yaw so the building front (-Y) looks outward along the given normal.

    town._face_yaw turns the front to look back *along* the normal, which is
    what you want when the normal points from the street to the building. Here
    the normal points from the block out to the street, so it is negated.
    """
    return _face_yaw(-out_x, -out_y)


class _Block(object):
    """One buildable block, with the local-to-world transform it was laid out in."""

    def __init__(self, record):
        self.center = record["center"]
        self.half_u = float(record["u"]) * 0.5
        self.half_v = float(record["v"]) * 0.5
        self.angle = math.radians(float(record["angle_deg"]))
        self.ring = int(record["ring"])
        self.distance = float(record["distance_uu"])
        self._ca = math.cos(self.angle)
        self._sa = math.sin(self.angle)

    def to_world(self, lu, lv):
        return (self.center[0] + lu * self._ca - lv * self._sa,
                self.center[1] + lu * self._sa + lv * self._ca)

    def dir_world(self, du, dv):
        return (du * self._ca - dv * self._sa, du * self._sa + dv * self._ca)


def _edges(block):
    """The four frontages: (fixed axis, offset sign, run half-length, outward)."""
    return [
        ("v", 1.0, block.half_u, (0.0, 1.0)),
        ("v", -1.0, block.half_u, (0.0, -1.0)),
        ("u", 1.0, block.half_v, (1.0, 0.0)),
        ("u", -1.0, block.half_v, (-1.0, 0.0)),
    ]


def _fill_block(placer, rng, block, index):
    """Buildings around one block, facing out. Returns how many landed."""
    choices = RING_CHOICES.get(block.ring, RING_CHOICES[2])
    placed = 0

    for axis, sign, run_half, (ou, ov) in _edges(block):
        out_x, out_y = block.dir_world(ou, ov)
        yaw_base = _face_out(out_x, out_y)

        cursor = -run_half
        guard = 0
        while cursor < run_half and guard < 40:
            guard += 1
            name = choices[int(rng.next() * len(choices)) % len(choices)]
            width, depth, radius = CITY_BUILDINGS[name]

            if cursor + width > run_half:
                break
            along = cursor + width * 0.5
            cursor += width + rng.uniform(60.0, 260.0)

            # Sit the building so its front is on the frontage line.
            setback = depth * 0.5 + 40.0
            if axis == "v":
                lu, lv = along, sign * (block.half_v - setback)
            else:
                lu, lv = sign * (block.half_u - setback), along

            wx, wy = block.to_world(lu, lv)
            if placer.place(name, wx, wy, yaw_base + rng.uniform(-0.6, 0.6),
                            "B%03d %s" % (index, name[3:]), radius=radius,
                            z_offset=-40.0, footprint=radius * 0.7):
                placed += 1

    return placed


def _place_blocks(placer, rng, blocks, plaza_index, park_indices):
    buildings = 0
    for index, block in enumerate(blocks):
        if index == plaza_index or index in park_indices:
            continue
        buildings += _fill_block(placer, rng, block, index)
    ctx.log("city: %d buildings across %d blocks" % (buildings, len(blocks)))
    return buildings


def _place_plaza(placer, block):
    """Civic centre: the city hall behind a fountain, benches and planters."""
    cx, cy = block.center
    hall_depth = CITY_BUILDINGS["SM_CityHall_A"][1]
    hall_x, hall_y = block.to_world(0.0, block.half_v - hall_depth * 0.5 - 60.0)
    out_x, out_y = block.dir_world(0.0, 1.0)
    placer.place("SM_CityHall_A", hall_x, hall_y, _face_out(out_x, out_y),
                 "CityHall", radius=2600.0, z_offset=-45.0, footprint=1400.0)

    fx, fy = block.to_world(0.0, -block.half_v * 0.25)
    placer.place("SM_Fountain_A", fx, fy, 0.0, "Fountain", radius=700.0, z_offset=-20.0)

    for i in range(8):
        angle = 45.0 * i + 22.5
        r = 1500.0
        wx = fx + math.cos(math.radians(angle)) * r
        wy = fy + math.sin(math.radians(angle)) * r
        placer.place("SM_Bench_A", wx, wy, angle + 90.0, "PlazaBench %d" % i,
                     radius=180.0, z_offset=-10.0)

    for i in range(6):
        angle = 60.0 * i
        r = 2300.0
        wx = fx + math.cos(math.radians(angle)) * r
        wy = fy + math.sin(math.radians(angle)) * r
        placer.place("SM_PlanterLong_A", wx, wy, angle, "PlazaPlanter %d" % i,
                     radius=520.0, z_offset=-10.0)

    # Citizens are spawned by the npc stage, which gives them somewhere to be
    # at each hour rather than standing them in the plaza forever.

    ctx.log("city: civic plaza at (%.0f, %.0f)" % (cx, cy))


def _place_parks(placer, rng, blocks, park_indices):
    """Green blocks. A city with no gaps in it reads as a wall, not a place."""
    trees = 0
    for index in sorted(park_indices):
        block = blocks[index]
        cx, cy = block.center
        for i in range(14):
            lu = rng.uniform(-block.half_u * 0.9, block.half_u * 0.9)
            lv = rng.uniform(-block.half_v * 0.9, block.half_v * 0.9)
            wx, wy = block.to_world(lu, lv)
            name = ["SM_Tree_Oak_A", "SM_Tree_Oak_C", "SM_Tree_Palm_A",
                    "SM_Tree_Birch_A"][i % 4]
            if placer.place(name, wx, wy, rng.uniform(0.0, 360.0),
                            "ParkTree %d-%d" % (index, i), radius=420.0,
                            z_offset=-25.0):
                trees += 1
        for i in range(4):
            lu = rng.uniform(-block.half_u * 0.7, block.half_u * 0.7)
            lv = rng.uniform(-block.half_v * 0.7, block.half_v * 0.7)
            wx, wy = block.to_world(lu, lv)
            placer.place("SM_Bench_A", wx, wy, rng.uniform(0.0, 360.0),
                         "ParkBench %d-%d" % (index, i), radius=200.0, z_offset=-10.0)
    ctx.log("city: %d park blocks, %d trees" % (len(park_indices), trees))


def _place_street_furniture(placer, rng, streets):
    """Lamps down every street, and traffic lights where the avenues cross."""
    lamps = 0
    for street in streets:
        half = street["width_uu"] * 0.5
        is_avenue = street["width_uu"] > 700.0
        step = 2600.0 if is_avenue else 3400.0

        for index, (x, y, tx, ty) in enumerate(_walk(street["points"], step)):
            side = 1.0 if index % 2 == 0 else -1.0
            nx, ny = -ty * side, tx * side
            wx, wy = x + nx * (half + 190.0), y + ny * (half + 190.0)
            # Face the lamp head out over the roadway.
            yaw = _face_yaw(nx, ny) + 180.0
            if placer.place("SM_CityLamp_A", wx, wy, yaw, "Lamp %d" % lamps,
                            radius=150.0, z_offset=-12.0):
                placer.place("SM_CityLamp_Glow", wx, wy, yaw, "LampGlow %d" % lamps,
                             z_offset=-12.0, check=False)
                lamps += 1

    ctx.log("city: %d street lamps along %d streets" % (lamps, len(streets)))
    return lamps


def _place_corners(placer, rng, blocks):
    """Traffic lights, shelters and kiosks at block corners, which is where the
    intersections are. Deduplicated by rounding, since four blocks share one."""
    seen = set()
    lights = 0
    extras = 0

    for block in blocks:
        for su in (-1.0, 1.0):
            for sv in (-1.0, 1.0):
                wx, wy = block.to_world(su * (block.half_u + 260.0),
                                        sv * (block.half_v + 260.0))
                key = (int(wx / 900.0), int(wy / 900.0))
                if key in seen:
                    continue
                seen.add(key)

                out_x, out_y = block.dir_world(su, sv)
                yaw = _face_out(out_x, out_y)
                if block.ring <= 1:
                    if placer.place("SM_TrafficLight_A", wx, wy, yaw,
                                    "Signal %d" % lights, radius=140.0, z_offset=-10.0):
                        lights += 1
                elif rng.next() < 0.35:
                    name = "SM_Kiosk_A" if rng.next() < 0.5 else "SM_BusShelter_A"
                    if placer.place(name, wx, wy, yaw, "Corner %d" % extras,
                                    radius=420.0, z_offset=-12.0):
                        extras += 1

    ctx.log("city: %d traffic signals, %d kiosks and shelters" % (lights, extras))


def _place_waterfront(placer, rng, city):
    """Container wharf on the seaward edge: the reason the city is here at all."""
    cx, cy = city["center"]
    shore = _coast_y_at(placer.wd, cx)
    quays = 0

    for pier_index, offset_x in enumerate((-7000.0, -1000.0, 5000.0)):
        base_x = cx + offset_x
        for step in range(10):
            wy = shore - 1400.0 + step * 380.0
            if placer.place("SM_Dock_A", base_x, wy, 90.0,
                            "Quay %d-%d" % (pier_index, step),
                            z_offset=140.0, check=False):
                quays += 1
        for step in range(5):
            wy = shore - 1400.0 + step * 760.0
            for side in (-1.0, 1.0):
                placer.place("SM_DockPost_A", base_x + side * 150.0, wy, 0.0,
                             "QuayPost %d-%d-%d" % (pier_index, step, int(side)),
                             z_offset=170.0, check=False)

    # Warehouses and stacked cargo behind the quays.
    for i, offset_x in enumerate((-9500.0, -4000.0, 2000.0, 7500.0)):
        wx = cx + offset_x
        wy = shore - 3600.0
        placer.place("SM_Warehouse_A", wx, wy, _face_yaw(0.0, 1.0),
                     "Warehouse %d" % i, radius=700.0, z_offset=-30.0, footprint=460.0)

    cargo = 0
    for i in range(40):
        wx = cx + rng.uniform(-9000.0, 8000.0)
        wy = shore - rng.uniform(1800.0, 3200.0)
        name = "SM_Crate_A" if rng.next() > 0.35 else "SM_Barrel_A"
        if placer.place(name, wx, wy, rng.uniform(0.0, 360.0), "Cargo %d" % i,
                        radius=120.0, z_offset=-6.0):
            cargo += 1

    placer.place("SM_FishingBoat_A", cx + 3000.0, shore + 900.0, 95.0,
                 "Freighter", z_offset=-30.0, check=False)

    ctx.log("city: waterfront at shore y=%.0f, %d quay sections, %d cargo"
            % (shore, quays, cargo))


# ---------------------------------------------------------------------------
def build(world, world_data, meshes=None):
    from . import meshbuild

    city = getattr(world_data, "city", None)
    if not city:
        ctx.warn("city: world_features.json has no city block; "
                 "re-run Tools/Terrain/generate_terrain.py")
        return 0

    if meshes is None:
        meshes = meshbuild.load_all()
    if not meshes:
        ctx.fail("no meshes available; run the 'meshes' stage first")

    placer = Placer(world_data, meshes, LABEL_PREFIX)
    removed = placer.clear()
    if removed:
        ctx.log("city: replaced %d existing actors" % removed)

    rng = _SmallRng(world_data.seed + 8181)

    blocks = [_Block(record) for record in city["blocks"]]
    if not blocks:
        ctx.warn("city: no blocks in world_features.json")
        return 0

    # The block closest to the centre becomes the civic plaza; a scattering of
    # the rest become parks so the grid is not wall to wall masonry.
    plaza_index = min(range(len(blocks)), key=lambda i: blocks[i].distance)
    park_indices = set(i for i, b in enumerate(blocks)
                       if b.ring >= 1 and i % 9 == 4)

    streets = [r for r in world_data.roads
               if r.get("is_city") and r.get("is_street")]

    _place_plaza(placer, blocks[plaza_index])
    _place_blocks(placer, rng, blocks, plaza_index, park_indices)
    _place_parks(placer, rng, blocks, park_indices)
    _place_street_furniture(placer, rng, streets)
    _place_corners(placer, rng, blocks)
    _place_waterfront(placer, rng, city)

    ctx.log("city: %s built, %d actors placed"
            % (city.get("name", "Newhaven"), placer.count))
    return placer.count
