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
from . import town as town_mod
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


# ---------------------------------------------------------------------------
# Trades
# ---------------------------------------------------------------------------
# Which archetype hosts which business, and the mesh footprints their fit-outs
# were built to. Read straight off meshbuild.CITY_INTERIORS so the two cannot
# drift: if a trade is added there it appears in the city here, with no edit.
def _venue_table():
    from . import meshbuild
    table = {}
    for (arch, width, depth, height, venues) in meshbuild.CITY_INTERIORS:
        table["SM_" + arch] = (width, depth, height, list(venues))
    return table


_VENUES = None

# What the sign outside says. The player has no way to know a room full of
# filing cabinets is a solicitor until something says so.
VENUE_NAMES = {
    "grocer": "Newhaven Grocery", "clothier": "Tailor and Outfitter",
    "baker": "Bakery", "pharmacy": "Pharmacy", "bookshop": "Bookshop",
    "hardware": "Hardware and Ironmonger", "furniture": "Furniture Showroom",
    "electronics": "Electrical Goods", "restaurant": "Restaurant",
    "cafe": "Coffee House", "bar": "Tavern", "barber": "Barber and Hairdresser",
    "optician": "Optometrist", "post": "Post Office", "gym": "Gymnasium",
    "office_lobby": "Offices", "lawyer": "Solicitors", "doctor": "Doctors Surgery",
    "dentist": "Dental Surgery", "bank": "Bank", "police": "Police Station",
    "library": "Public Library", "museum": "Museum", "school": "School",
    "civic_hall": "Civic Hall", "apartment_lobby": "Apartments",
    "workshop": "Workshop",
}


_ARCH_TURN = {}


def _venue_for(mesh_name):
    """The trade this particular building carries on.

    The count is kept per archetype, not across the city. On a global counter an
    archetype with four trades and an unlucky placement order can skip one
    entirely - which is how Newhaven ended up with no dentist in it, in a build
    whose whole point was that it should have one.
    """
    entry = _VENUES.get(mesh_name)
    if entry is None:
        return None
    venues = entry[3]
    turn = _ARCH_TURN.get(mesh_name, 0)
    _ARCH_TURN[mesh_name] = turn + 1
    return venues[turn % len(venues)]


def _place_interior(placer, actor, mesh_name, index, seeds):
    """The fit-out, its fires and bulbs, and a light in every room."""
    entry = _VENUES.get(mesh_name)
    if entry is None:
        return 0
    width, depth, height, _venues = entry
    venue = _venue_for(mesh_name)
    arch = mesh_name[3:]

    location = actor.get_actor_location()
    yaw = actor.get_actor_rotation().yaw
    placed = 0
    if placer.place_at("SM_Int_%s_%s" % (arch, venue), location, yaw,
                       "Interior %d" % index, cull=town_mod.INTERIOR_CULL):
        placed += 1
    if placer.place_at("SM_Glow_%s_%s" % (arch, venue), location, yaw,
                       "InteriorGlow %d" % index, cull=town_mod.GLOW_CULL,
                       shadow=False, collision=False):
        placed += 1
    placed += town_mod.place_room_lights(
        placer, location, yaw, width, depth, 1, seeds[(arch, venue)],
        "InteriorLight %d" % index, base_z=0.0, wall_t=34.0, storey_h=height)
    return placed


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


_COUNTER = [0]
_INTERIORS = [0]
_SEEDS = {}


def _reset_venues():
    """Bind the venue table and the seeds the mesh catalog actually used."""
    global _VENUES
    from . import meshbuild
    _VENUES = _venue_table()
    _SEEDS.clear()
    for (arch, _w, _d, _h, venues) in meshbuild.CITY_INTERIORS:
        for i, venue in enumerate(venues):
            _SEEDS[(arch, venue)] = 7100 + i * 97 + len(arch) * 13
    _ARCH_TURN.clear()
    _COUNTER[0] = 0
    _INTERIORS[0] = 0


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
            yaw = yaw_base + rng.uniform(-0.6, 0.6)
            # Same rule as the town: the floor clears the highest ground under
            # the footprint, and the buried skirt in gen_city._shopfront takes
            # up the slack downhill. A city ground floor you can walk into has
            # the same intolerance for a hillside coming through it.
            _low, high = placer.ground_range(wx, wy, yaw, width * 0.5, depth * 0.5)
            lift = high + town_mod.FLOOR_CLEAR - placer.wd.height_uu(wx, wy)
            built = placer.place(name, wx, wy, yaw,
                                 "B%03d %s" % (index, name[3:]), radius=radius,
                                 z_offset=lift, footprint=0.0)
            if built:
                _INTERIORS[0] += _place_interior(placer, built, name,
                                                 _COUNTER[0], _SEEDS)
                _COUNTER[0] += 1
                placed += 1

    return placed


def _place_blocks(placer, rng, blocks, reserved):
    _reset_venues()
    buildings = 0
    for index, block in enumerate(blocks):
        if index in reserved:
            continue
        buildings += _fill_block(placer, rng, block, index)
    ctx.log("city: %d buildings across %d blocks" % (buildings, len(blocks)))
    return buildings


def _place_civic(placer, hall_block, square_block):
    """The city hall on one block, the civic square on the one facing it.

    The hall's footprint is 6000 x 3410 and every block in Newhaven is at most
    4680 x 3680, so the hall does not fit on a block with anything else - it
    does not quite fit on a block at all. Putting the fountain on the same
    block meant putting it inside the building, which is why overlap rejection
    threw it away and left the civic centre as an empty rectangle. A square
    across the street from the city hall is what a city would have done.
    """
    hx, hy = hall_block.center
    hall_depth = CITY_BUILDINGS["SM_CityHall_A"][1]
    hall_x, hall_y = hall_block.to_world(0.0, hall_block.half_v - hall_depth * 0.5 - 60.0)
    out_x, out_y = hall_block.dir_world(0.0, 1.0)
    placer.place("SM_CityHall_A", hall_x, hall_y, _face_out(out_x, out_y),
                 "CityHall", radius=2600.0, z_offset=-45.0, footprint=1400.0,
                 check=False)

    # The square. check=False throughout: these are laid out from the block's
    # own geometry and are meant to sit inside it, not to compete for space.
    fx, fy = square_block.center
    placer.place("SM_Fountain_A", fx, fy, 0.0, "Fountain", radius=700.0,
                 z_offset=-20.0, check=False)

    benches = 0
    for ring_radius, count, offset in ((1150.0, 8, 22.5), (1900.0, 10, 18.0)):
        for i in range(count):
            angle = 360.0 / count * i + offset
            wx = fx + math.cos(math.radians(angle)) * ring_radius
            wy = fy + math.sin(math.radians(angle)) * ring_radius
            placer.place("SM_Bench_A", wx, wy, angle + 90.0, "PlazaBench %d" % benches,
                         radius=180.0, z_offset=-10.0, check=False)
            benches += 1

    planters = 0
    for ring_radius, count, offset in ((1550.0, 6, 0.0), (2300.0, 8, 22.0)):
        for i in range(count):
            angle = 360.0 / count * i + offset
            wx = fx + math.cos(math.radians(angle)) * ring_radius
            wy = fy + math.sin(math.radians(angle)) * ring_radius
            placer.place("SM_PlanterLong_A", wx, wy, angle, "PlazaPlanter %d" % planters,
                         radius=520.0, z_offset=-10.0, check=False)
            planters += 1

    ctx.log("city: city hall at (%.0f, %.0f), civic square at (%.0f, %.0f) "
            "with a fountain, %d benches and %d planters"
            % (hall_x, hall_y, fx, fy, benches, planters))
    return (fx, fy)


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

    # The block closest to the centre takes the city hall; the block facing it
    # takes the civic square. Two blocks, because the hall does not fit on one
    # with anything else. A scattering of the rest become parks so the grid is
    # not wall to wall masonry.
    hall_index = min(range(len(blocks)), key=lambda i: blocks[i].distance)
    hall_centre = blocks[hall_index].center
    square_index = min((i for i in range(len(blocks)) if i != hall_index),
                       key=lambda i: ((blocks[i].center[0] - hall_centre[0]) ** 2
                                      + (blocks[i].center[1] - hall_centre[1]) ** 2))
    park_indices = set(i for i, b in enumerate(blocks)
                       if b.ring >= 1 and i % 9 == 4)
    park_indices.discard(hall_index)
    park_indices.discard(square_index)

    streets = [r for r in world_data.roads
               if r.get("is_city") and r.get("is_street")]

    _place_civic(placer, blocks[hall_index], blocks[square_index])
    _place_blocks(placer, rng, blocks,
                  park_indices | {hall_index, square_index})
    _place_parks(placer, rng, blocks, park_indices)
    _place_street_furniture(placer, rng, streets)
    _place_corners(placer, rng, blocks)
    _place_waterfront(placer, rng, city)

    ctx.log("city: %s built, %d actors placed"
            % (city.get("name", "Newhaven"), placer.count))
    return placer.count
