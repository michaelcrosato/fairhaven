"""Populates Fairhaven and Newhaven: people, animals and the roads they walk.

This stage does three things, in order:

1. **Bakes the route network.** The street polylines in world_features.json are
   sampled into nodes, welded where they meet, and stored on one
   AUEGT2RouteNetwork actor. Node heights come from the heightmap, which is why
   the runtime needs no line traces to follow a path.

2. **Surveys the world for anchors.** Nothing here is a hard-coded coordinate.
   Homes are the doorsteps of houses the town stage placed; work is a warehouse,
   a stall, a field or an office tower that is actually standing there; the
   shore is the exported coastline. Move the town and everyone moves with it.

3. **Spawns the population**, giving each inhabitant a role, a seed and the
   handful of places their routine will ask for. The routine itself lives in
   C++ (UEGT2NPCRoutines) - this stage only decides who is who and where their
   places are.

The whole stage is deterministic: everything is drawn from ``world_data.seed``,
so the same seed produces the same town with the same people in it.
"""
from __future__ import annotations

import math

import unreal

from . import city as city_mod
from . import ctx
from .meshkit import _SmallRng
from .town import HOUSE_DEPTH, SERVICE_NAMES, _coast_y_at, _walk

LABEL_PREFIX = "Life "

# --- Route network sampling -------------------------------------------------
# Streets are sampled finely because that is where the walking happens; country
# roads are sampled coarsely because almost nobody uses them and each node costs
# map size for the whole life of the project.
STREET_SPACING = 700.0
ROAD_SPACING = 1500.0
# Two samples closer than this become one node. Big enough to collapse the
# duplicate points a polyline leaves at a corner, small enough not to swallow
# both sides of a narrow lane.
WELD_RADIUS = 380.0
# After the per-road chains exist, any two nodes this close are joined. This is
# what actually connects a junction: two roads crossing rarely drop a sample on
# exactly the same spot, and without this pass the network is a set of separate
# lines that happen to overlap. The town's own streets are far further apart
# than this, so it never welds two parallel roads into one.
STITCH_RADIUS = 1150.0
ROUTE_Z_LIFT = 8.0

# --- Population sizing ------------------------------------------------------
# Fractions of the available anchors rather than fixed counts: the town grew
# from 2 km to 4 km once already, and a fixed 40 villagers would have quietly
# become a ghost town when it did.
TOWN_HOME_OCCUPANCY = 0.86        # homes that have anyone in them at all
TOWN_SECOND_RESIDENT = 0.34       # of those, how many hold a second adult
TOWN_CHILD_CHANCE = 0.26          # of occupied homes, how many have a child
# Newhaven is the bigger settlement and was reading as the emptier one: 17
# inhabitants within sixty metres of the downtown viewpoint against 140 in the
# town square. A city block holds more people than a cottage does.
CITY_HOME_OCCUPANCY = 1.0
CITY_SECOND_RESIDENT = 0.85
CITY_THIRD_RESIDENT = 0.9
FARMERS_PER_FARM = 2

# --- Names ------------------------------------------------------------------
# Assembled from parts rather than listed, so two hundred people can have
# distinct names without two hundred lines of table.
_GIVEN = ("Mara", "Tolm", "Brey", "Anse", "Cardo", "Ilva", "Nesh", "Orrin",
          "Petra", "Rask", "Sula", "Tem", "Vada", "Wen", "Yarrow", "Bess",
          "Corrin", "Dela", "Eamon", "Fen", "Greta", "Hal", "Isla", "Jory",
          "Kestrel", "Lune", "Marek", "Noa", "Ovid", "Pell", "Quill", "Rue",
          "Saffi", "Tovi", "Ursin", "Vell", "Wick", "Yusa", "Zev", "Alder")
_FAMILY = ("Aldwin", "Barrow", "Coldwater", "Danes", "Elmer", "Farrow",
           "Greening", "Halloway", "Ivers", "Jessop", "Kettle", "Lowe",
           "Marsh", "Netherby", "Oakes", "Pike", "Quarry", "Redding",
           "Salter", "Thatcher", "Underhill", "Vance", "Weld", "Yarrow",
           "Ashby", "Brine", "Chandler", "Dunmore", "Fennick", "Gale")


def _name_for(index, seed):
    rng = _SmallRng(seed * 7919 + index * 131 + 17)
    given = _GIVEN[int(rng.next() * len(_GIVEN)) % len(_GIVEN)]
    family = _FAMILY[int(rng.next() * len(_FAMILY)) % len(_FAMILY)]
    return "%s %s" % (given, family)


# ---------------------------------------------------------------------------
# Enum access
# ---------------------------------------------------------------------------
def _enum(type_name, value_name):
    """unreal.<Type>.<VALUE>, with a readable failure instead of AttributeError.

    Unreal exposes a UENUM as its name minus the leading E, and each enumerator
    upper-snake-cased. Getting either wrong is a very common way for a content
    stage to die three hundred lines into a placement loop.
    """
    enum_type = getattr(unreal, type_name, None)
    if enum_type is None:
        ctx.fail("enum type unreal.%s not found; build the editor target first" % type_name)
    value = getattr(enum_type, value_name, None)
    if value is None:
        ctx.fail("enum value %s.%s not found" % (type_name, value_name))
    return value


class _Enums(object):
    """Resolved once, so the placement loops read like the C++ they mirror."""

    def __init__(self):
        self.role = {name: _enum("UEGT2NPCRole", name.upper()) for name in (
            "Villager", "Farmer", "Fisher", "Merchant", "Baker", "Innkeeper",
            "Priest", "Smith", "Dockhand", "Child", "Elder", "Clerk",
            "Shopkeeper", "Courier", "Officer", "Busker", "Gardener", "Sailor")}
        self.species = {name: _enum("UEGT2NPCSpecies", name.upper()) for name in (
            "Person", "Dog", "Cat", "Chicken", "Duck", "Sheep", "Cow", "Pig",
            "Goat", "Horse", "Seagull", "Rabbit")}
        self.anchor = {name: _enum("UEGT2Anchor", name.upper()) for name in (
            "Home", "Work", "Market", "Square", "Church", "Dock", "Field",
            "Tavern", "Park", "Plaza", "Shore", "Water", "Coop", "Pasture",
            "Shelter", "Wander", "Food", "Washroom", "Seat")}


# ---------------------------------------------------------------------------
# Small geometry helpers
# ---------------------------------------------------------------------------
def _vec(point):
    return unreal.Vector(point[0], point[1], point[2])


def _nearest(points, x, y, fallback=None):
    """The closest of a list of (x, y, z) tuples, or fallback when it is empty."""
    best = None
    best_distance = None
    for p in points:
        d = (p[0] - x) ** 2 + (p[1] - y) ** 2
        if best_distance is None or d < best_distance:
            best_distance = d
            best = p
    return best if best is not None else fallback


def _pick_near(points, x, y, seed, pool=4, fallback=None):
    """One of the ``pool`` closest points, chosen by seed rather than by rank.

    Plain "nearest" put every villager in town at the same market stall,
    because all five stalls stand on the same square and one of them is always
    the closest. Choosing from the closest handful spreads a crowd over the
    places a crowd would actually use, and stays stable per inhabitant.
    """
    if not points:
        return fallback
    ranked = sorted(points, key=lambda p: (p[0] - x) ** 2 + (p[1] - y) ** 2)
    ranked = ranked[:max(1, pool)]
    return ranked[abs(seed) % len(ranked)]


# Trades an inhabitant can eat at, and trades with a washroom in them. Both are
# read off the actor labels the town and city stages write, which is why the
# labels carry the trade at all.
FOOD_TRADES = frozenset((
    "grocer", "baker", "restaurant", "cafe", "bar", "butcher_hardware"))
WASH_TRADES = frozenset(("gym", "apartment_lobby", "office_lobby"))


def _venue_phrase(name):
    """A shop name that reads after a preposition: "Work at the Solicitor".

    The sign tables are written for signs, where "Solicitor" and "Bank" are
    exactly right. In a sentence they are not, and the amenity prompts and the
    HUD both put them in one. Anything already starting with "The" is a proper
    name and is left alone.
    """
    if not name:
        return name
    return name if name.startswith("The ") else "the " + name


def _front_of(location, yaw, depth, clearance):
    """The point in front of a building's door face (local -Y), on the ground."""
    radians = math.radians(yaw)
    cos_y, sin_y = math.cos(radians), math.sin(radians)
    local_y = -(depth * 0.5 + clearance)
    return (location.x - local_y * sin_y, location.y + local_y * cos_y, location.z)


# Door-face depth for the buildings people live and work in but that the town
# module does not already publish a depth for.
_EXTRA_DEPTH = {
    "SM_Barn_A": 820.0, "SM_Shed_A": 380.0, "SM_Warehouse_A": 900.0,
    "SM_Church_A": 1100.0, "SM_MarketStall_A": 300.0,
}


# ---------------------------------------------------------------------------
# Route network
# ---------------------------------------------------------------------------
class _RouteBaker(object):
    """Samples the road polylines into a welded, stitched node graph."""

    def __init__(self, world_data, network):
        self.wd = world_data
        self.net = network
        self.cells = {}                 # (cx, cy) -> [(index, x, y)]
        self.cell_size = STITCH_RADIUS

    def _cell(self, x, y):
        return (int(math.floor(x / self.cell_size)), int(math.floor(y / self.cell_size)))

    def _neighbours(self, x, y):
        cx, cy = self._cell(x, y)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for entry in self.cells.get((cx + dx, cy + dy), ()):
                    yield entry

    def node_at(self, x, y):
        for (index, nx, ny) in self._neighbours(x, y):
            if (nx - x) ** 2 + (ny - y) ** 2 < WELD_RADIUS ** 2:
                return index
        z = self.wd.height_uu(x, y) + ROUTE_Z_LIFT
        index = self.net.add_node(unreal.Vector(x, y, z))
        self.cells.setdefault(self._cell(x, y), []).append((index, x, y))
        return index

    def add_road(self, points, spacing):
        previous = None
        count = 0
        for (x, y, _tx, _ty) in _walk(points, spacing):
            if not self.wd.in_bounds(x, y, 200.0):
                previous = None          # a road that leaves the map breaks its chain
                continue
            index = self.node_at(x, y)
            if previous is not None and previous != index:
                self.net.link_nodes(previous, index)
                count += 1
            previous = index
        return count

    def stitch(self):
        """Join nodes from different chains that ended up on top of each other.

        This is what turns a bundle of separate lines into a network. Without
        it an NPC can walk the length of its own street and never turn a corner.
        """
        joined = 0
        radius_squared = STITCH_RADIUS ** 2
        for bucket in list(self.cells.values()):
            for (index, x, y) in bucket:
                for (other, ox, oy) in self._neighbours(x, y):
                    if other <= index:
                        continue
                    if (ox - x) ** 2 + (oy - y) ** 2 < radius_squared:
                        self.net.link_nodes(index, other)
                        joined += 1
        return joined


def _build_routes(world_data, subsystem, route_class):
    network = subsystem.spawn_actor_from_class(
        route_class, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if network is None:
        ctx.fail("could not spawn UEGT2RouteNetwork")
    network.set_actor_label(LABEL_PREFIX + "Routes")

    baker = _RouteBaker(world_data, network)
    links = 0
    for road in world_data.roads:
        spacing = STREET_SPACING if road.get("is_street") else ROAD_SPACING
        links += baker.add_road(road["points"], spacing)

    stitched = baker.stitch()
    network.finalise_network()
    ctx.log("npc: route network %d nodes, %d chain links, %d junction links"
            % (network.get_node_count(), links, stitched))
    return network


# ---------------------------------------------------------------------------
# Survey: what is actually standing in the world
# ---------------------------------------------------------------------------
class _Survey(object):
    """Every anchor the population can be given, read off the placed actors."""

    def __init__(self, world_data, subsystem):
        self.wd = world_data
        self.town_homes = []          # (x, y, z) doorsteps
        self.town_work = []           # warehouses, sheds, the smithy
        self.town_stalls = []
        self.town_benches = []
        self.town_docks = []
        self.town_farms = []          # barns and sheds out in the fields
        self.town_church = None
        self.town_shelters = []
        # The three the needs use. Every one of them is a place an inhabitant
        # can walk to and have the need answered: somewhere to eat, somewhere
        # to sit, somewhere to wash. They are lists rather than single points
        # because a need answered in exactly one place is a need that sends the
        # whole town to the same doorstep.
        self.town_food = []
        self.town_washrooms = []
        self.town_seats = []
        # Named places, for the amenities the gameplay stage puts down so the
        # player can use the same food, work and washrooms the routines send
        # everybody else to. Same points as the anchor lists above; the extra
        # field is what the interaction prompt says out loud. Town and city are
        # kept apart for the reason every other pair in this class is: the town
        # has three food shops and Newhaven has forty, so one shared list under
        # a sensible cap is a cap that falls entirely on the town.
        self.food_venues = []         # (x, y, z, name)
        self.trade_venues = []        # (x, y, z, name, role name)
        self.city_food_venues = []
        self.city_trade_venues = []
        # Benches and privies get the same treatment as buildings: the amenity
        # stands in *front* of the thing, not inside it. A bench has a 155 cm
        # backrest and a privy is a shed with one door, and an interaction
        # volume buried in either is one the player's probe can never reach -
        # it stops at the first thing that blocks Visibility, and that is the
        # bench. The NPC anchors above keep using the prop's own position,
        # which is what they want: they walk to the bench, not to the seat.
        self.seat_venues = []              # (x, y, z) in front of a bench
        self.town_wash_venues = []         # (x, y, z) outside a privy door
        self.city_wash_venues = []
        # Everywhere it is reasonable to stand "in the square": the benches, the
        # stall fronts and the well. A single square anchor put seventy-five
        # people on one coordinate.
        self.town_square_spots = []
        self.city_plaza_spots = []
        # Labels the classifier did not recognise. A stage that renames an
        # actor silently empties an anchor set, and the only symptom is a part
        # of the world that stops being visited.
        self.unclassified = {}
        # The fallback for every city anchor. Never the town square: a missing
        # city landmark used to send couriers and buskers on a 130 km walk to
        # Fairhaven, which reads as "the city is empty" and is very hard to see
        # from inside the city.
        city = getattr(world_data, "city", None)
        self.city_centre = ((city["center"][0], city["center"][1],
                             world_data.height_uu(city["center"][0], city["center"][1]))
                            if city else None)
        self.city_homes = []
        self.city_work = []
        self.city_shops = []
        self.city_parks = []
        self.city_shelters = []
        self.city_docks = []
        self.city_plaza = None
        self.city_food = []
        self.city_washrooms = []
        self.city_seats = []

        cx, cy = world_data.town["center"]
        self.town_square = (cx, cy, world_data.height_uu(cx, cy))
        self.shore_y = _coast_y_at(world_data, cx)
        self.town_shore = (cx, self.shore_y - 900.0,
                           world_data.height_uu(cx, self.shore_y - 900.0))

        for actor in subsystem.get_all_level_actors():
            label = actor.get_actor_label()
            if not (label.startswith("Town ") or label.startswith("City ")):
                continue
            self._classify(actor, label)

        # Ponds and the lagoon: where ducks live.
        self.water = []
        for pond in world_data.ponds:
            px, py = pond["center"][0], pond["center"][1]
            radius = float(pond.get("radius_uu", 2000.0))
            self.water.append((px + radius * 0.8, py, world_data.height_uu(px + radius * 0.8, py)))
        self.water.append(self.town_shore)

        # The square itself is always a valid place to stand, and is the
        # fallback when the town stage placed no furniture at all.
        self.town_square_spots.append(self.town_square)
        if self.city_plaza:
            self.city_plaza_spots.append(self.city_plaza)

        # One building becomes the inn. Derived, not hard-coded: it is whichever
        # town house stands closest to the square, which is where an inn would be.
        self.town_tavern = _nearest(self.town_homes, cx, cy, self.town_square)
        if self.city_plaza:
            self.city_tavern = _nearest(self.city_shops, self.city_plaza[0],
                                        self.city_plaza[1], self.city_plaza)
        else:
            self.city_tavern = None

    def _mesh_name(self, actor):
        try:
            component = actor.get_editor_property("static_mesh_component")
            mesh = component.get_editor_property("static_mesh") if component else None
            return mesh.get_name() if mesh else ""
        except Exception:                                       # noqa: BLE001
            return ""

    def _classify(self, actor, label):
        location = actor.get_actor_location()
        yaw = actor.get_actor_rotation().yaw
        name = self._mesh_name(actor)
        point = (location.x, location.y, location.z)

        if label.startswith("Town Privy "):
            self.town_washrooms.append(point)
            self.town_wash_venues.append(_front_of(location, yaw, 190.0, 130.0))
        elif label.startswith("City WC "):
            self.city_washrooms.append(point)
            self.city_wash_venues.append(_front_of(location, yaw, 210.0, 130.0))
        elif label.startswith("Town Shop "):
            trade = label.split()[2] if len(label.split()) > 2 else ""
            front = _front_of(location, yaw, 560.0, 190.0)
            venue = _venue_phrase(SERVICE_NAMES.get(trade, trade.replace("_", " ")
                                                     or "shop"))
            if trade in FOOD_TRADES:
                self.town_food.append(front)
                self.food_venues.append(front + (venue,))
            else:
                # Somewhere with a counter and somebody behind it is somewhere
                # a person can be taken on for the afternoon.
                self.trade_venues.append(front + (venue, "Shopkeeper"))
        elif label.startswith("City Interior "):
            parts = label.split()
            trade = parts[3] if len(parts) > 3 else ""
            venue = _venue_phrase(city_mod.VENUE_NAMES.get(
                trade, trade.replace("_", " ") or "shop"))
            if trade in FOOD_TRADES:
                self.city_food.append(point)
                self.city_food_venues.append(point + (venue,))
            elif trade not in WASH_TRADES:
                self.city_trade_venues.append(point + (venue, "Shopkeeper"))
            if trade in WASH_TRADES:
                self.city_washrooms.append(point)
        elif label.startswith("Town House "):
            depth = HOUSE_DEPTH.get(name, 560.0)
            self.town_homes.append(_front_of(location, yaw, depth, 190.0))
        elif label.startswith("Town Farm "):
            depth = _EXTRA_DEPTH.get(name, 500.0)
            front = _front_of(location, yaw, depth, 260.0)
            self.town_farms.append(front)
            self.trade_venues.append(front + ("the farm", "Farmer"))
        elif label.startswith("Town Warehouse "):
            front = _front_of(location, yaw, 900.0, 260.0)
            self.town_work.append(front)
            self.trade_venues.append(front + ("the warehouse", "Dockhand"))
            self.town_shelters.append(point)
        elif label.startswith("Town Stall "):
            front = _front_of(location, yaw, 300.0, 170.0)
            self.town_stalls.append(front)
            self.town_square_spots.append(front)
            self.town_shelters.append(point)
        elif label.startswith("Town Bench "):
            self.town_benches.append(point)
            self.town_seats.append(point)
            self.seat_venues.append(_front_of(location, yaw, 140.0, 90.0))
            self.town_square_spots.append(point)
        elif label.startswith("Town Dock "):
            self.town_docks.append(point)
        elif label == "Town Church":
            self.town_church = _front_of(location, yaw, 1100.0, 340.0)
            self.town_shelters.append(point)
        elif label.startswith("City B"):
            if name.startswith("SM_Apartment") or name.startswith("SM_Shophouse"):
                self.city_homes.append(_front_of(location, yaw, 1500.0, 320.0))
                if name.startswith("SM_Shophouse"):
                    self.city_shops.append(_front_of(location, yaw, 1500.0, 320.0))
            elif (name.startswith("SM_Tower") or name.startswith("SM_Office")
                  or name.startswith("SM_ParkingDeck")):
                front = _front_of(location, yaw, 1800.0, 340.0)
                self.city_work.append(front)
                self.city_trade_venues.append(front + ("the offices", "Clerk"))
        elif label == "City CityHall":
            front = _front_of(location, yaw, 3410.0, 420.0)
            self.city_work.append(front)
            self.city_trade_venues.append(front + ("the City Hall", "Clerk"))
        elif label == "City Fountain":
            self.city_plaza = point
        elif label.startswith("City ParkBench ") or label.startswith("City ParkTree "):
            self.city_parks.append(point)
            # Only the benches are somewhere to sit. A tree is somewhere to
            # stand about, which is what city_parks is for.
            if label.startswith("City ParkBench "):
                self.city_seats.append(point)
                self.seat_venues.append(_front_of(location, yaw, 140.0, 90.0))
        elif label.startswith("City PlazaBench ") or label.startswith("City PlazaPlanter "):
            self.city_plaza_spots.append(point)
            if label.startswith("City PlazaBench "):
                self.city_seats.append(point)
                self.seat_venues.append(_front_of(location, yaw, 140.0, 90.0))
        elif label.startswith("City Corner ") or label.startswith("City Warehouse "):
            self.city_shelters.append(point)
        elif label.startswith("City Quay "):
            self.city_docks.append(point)
        else:
            key = label.rstrip("0123456789 -")
            self.unclassified[key] = self.unclassified.get(key, 0) + 1

    def warn_gaps(self):
        """Shout about anchor sets that came out empty, and labels nobody claimed."""
        for name, values in (("town food", self.town_food),
                             ("town washrooms", self.town_washrooms),
                             ("city food", self.city_food),
                             ("city washrooms", self.city_washrooms),
                             ("town homes", self.town_homes),
                             ("town stalls", self.town_stalls),
                             ("square spots", self.town_square_spots),
                             ("town docks", self.town_docks),
                             ("city homes", self.city_homes),
                             ("city workplaces", self.city_work),
                             ("plaza spots", self.city_plaza_spots)):
            if not values:
                ctx.warn("npc: no %s found - the stage that places them may not have run"
                         % name)
        if self.unclassified:
            top = sorted(self.unclassified.items(), key=lambda kv: -kv[1])[:8]
            ctx.log("npc: unclaimed labels - %s"
                    % ", ".join("'%s' x%d" % (k, v) for k, v in top))

    def summary(self):
        return ("npc: survey - %d town homes, %d farms, %d stalls, %d docks, "
                "%d square spots, %d city homes, %d city workplaces, %d parks, "
                "%d plaza spots"
                % (len(self.town_homes), len(self.town_farms), len(self.town_stalls),
                   len(self.town_docks), len(self.town_square_spots),
                   len(self.city_homes), len(self.city_work),
                   len(self.city_parks), len(self.city_plaza_spots)))


def survey_world(world_data, subsystem):
    """Read the placed world back into anchor sets.

    Public because the gameplay stage needs it too: the amenities the player
    uses have to stand on the same points the population's anchors resolve to,
    or the player would be eating somewhere the town has never heard of.
    """
    return _Survey(world_data, subsystem)


# ---------------------------------------------------------------------------
# Spawning
# ---------------------------------------------------------------------------
class _Populator(object):
    def __init__(self, world_data, survey, enums, meshes, subsystem, npc_class):
        self.wd = world_data
        self.survey = survey
        self.e = enums
        self.meshes = meshes
        self.subsystem = subsystem
        self.npc_class = npc_class
        self.count = 0
        self.people = 0
        self.animals = 0
        self.by_role = {}
        self.by_species = {}
        self.missing_meshes = set()

    # -- spawning -----------------------------------------------------------
    def spawn(self, mesh_name, x, y, z, role, species, label, seed,
              anchors, speed=155.0, wander=500.0, display_name=None):
        mesh = self.meshes.get(mesh_name)
        if mesh is None:
            self.missing_meshes.add(mesh_name)
            return None

        actor = self.subsystem.spawn_actor_from_class(
            self.npc_class, unreal.Vector(x, y, z),
            unreal.Rotator(0.0, 0.0, (seed * 37) % 360))
        if actor is None:
            return None

        actor.set_actor_label(LABEL_PREFIX + label)
        actor.configure_npc(display_name or label, role, species, seed)
        actor.set_npc_mesh(mesh)
        actor.set_base_speed(speed)
        actor.set_wander_radius(wander)
        for anchor_type, point in anchors:
            if point is not None:
                actor.add_anchor(anchor_type, _vec(point))

        self.count += 1
        return actor

    def note(self, role_name=None, species_name=None):
        if role_name:
            self.people += 1
            self.by_role[role_name] = self.by_role.get(role_name, 0) + 1
        if species_name:
            self.animals += 1
            self.by_species[species_name] = self.by_species.get(species_name, 0) + 1

    # -- anchor sets --------------------------------------------------------
    def town_anchors(self, home, work, seed=0):
        """Every place a townsperson's routine can ask for.

        Shared destinations (the stalls, the benches, the quay) are picked from
        the closest handful by seed, not by rank: the town has five market
        stalls and ninety villagers, and "nearest" sends all ninety to one of
        them.
        """
        s = self.survey
        a = self.e.anchor
        hx, hy = home[0], home[1]
        square = s.town_square
        shore = (hx, s.shore_y - 900.0, self.wd.height_uu(hx, s.shore_y - 900.0))
        return [
            (a["Home"], home),
            (a["Work"], work or _nearest(s.town_work, hx, hy, square)),
            (a["Market"], _pick_near(s.town_stalls, hx, hy, seed, 13, square)),
            (a["Square"], _pick_near(s.town_square_spots, hx, hy, seed, 16, square)),
            (a["Church"], s.town_church or square),
            (a["Dock"], _pick_near(s.town_docks, hx, hy, seed, 14, shore)),
            (a["Field"], _pick_near(s.town_farms, hx, hy, seed, 3, square)),
            (a["Tavern"], s.town_tavern or square),
            (a["Park"], _pick_near(s.town_benches, hx, hy, seed, 15, square)),
            # A different salt from Square: in the town the two mean the same
            # place, and resolving them to the same spot would waste half the
            # square's capacity.
            (a["Plaza"], _pick_near(s.town_square_spots, hx, hy, seed + 7, 16, square)),
            (a["Shore"], shore),
            (a["Shelter"], _pick_near(s.town_shelters, hx, hy, seed, 3, home)),
            # The needs. Home is the fallback for all three because a house has
            # a chair, a kitchen and a washroom in it - so an inhabitant who
            # cannot find a public one goes home, which is what a person does.
            (a["Food"], _pick_near(s.town_food + s.town_stalls, hx, hy, seed, 6, home)),
            (a["Washroom"], _pick_near(s.town_washrooms, hx, hy, seed, 4, home)),
            (a["Seat"], _pick_near(s.town_benches or s.town_square_spots,
                                   hx, hy, seed, 8, home)),
        ]

    def city_anchors(self, home, work, seed=0):
        s = self.survey
        a = self.e.anchor
        hx, hy = home[0], home[1]
        plaza = s.city_plaza or s.city_centre or s.town_square
        return [
            (a["Home"], home),
            (a["Work"], work or _pick_near(s.city_work, hx, hy, seed, 3, plaza)),
            (a["Market"], _pick_near(s.city_shops, hx, hy, seed, 6, plaza)),
            (a["Square"], _pick_near(s.city_plaza_spots, hx, hy, seed, 14, plaza)),
            (a["Church"], plaza),
            (a["Dock"], _pick_near(s.city_docks, hx, hy, seed, 30, plaza)),
            (a["Field"], _pick_near(s.city_parks, hx, hy, seed, 6, plaza)),
            (a["Tavern"], s.city_tavern or plaza),
            (a["Park"], _pick_near(s.city_parks, hx, hy, seed, 8, plaza)),
            (a["Plaza"], _pick_near(s.city_plaza_spots, hx, hy, seed + 7, 14, plaza)),
            (a["Shore"], _pick_near(s.city_docks, hx, hy, seed, 8, plaza)),
            (a["Shelter"], _pick_near(s.city_shelters, hx, hy, seed, 3, home)),
            (a["Food"], _pick_near(s.city_food or s.city_shops, hx, hy, seed, 6, home)),
            (a["Washroom"], _pick_near(s.city_washrooms, hx, hy, seed, 4, home)),
            (a["Seat"], _pick_near(s.city_parks or s.city_plaza_spots,
                                   hx, hy, seed, 8, home)),
        ]

    def animal_anchors(self, home, extras):
        a = self.e.anchor
        base = [(a["Home"], home), (a["Shelter"], home)]
        base.extend(extras)
        return base


# --- role selection ---------------------------------------------------------
# Weighted, and biased by where the house is: a town where the fishers live
# inland and the farmers live on the quay reads as randomly assigned, which is
# exactly what it would be.
# Key trades (priest, innkeeper, baker, smith) are placed separately and are
# deliberately absent here: see _place_key_trades.
_TOWN_ROLES = (
    ("Villager", 32), ("Merchant", 8), ("Elder", 9), ("Farmer", 7),
    ("Fisher", 6), ("Dockhand", 6), ("Sailor", 4), ("Busker", 2),
)
_TOWN_ROLES_COASTAL = (
    ("Fisher", 16), ("Dockhand", 12), ("Sailor", 8), ("Villager", 16),
    ("Merchant", 5), ("Elder", 4),
)
# Weighted toward the trades that spend the day on the pavement rather than
# inside a tower. A city of nothing but clerks is empty between nine and five,
# which is exactly when you are most likely to be walking through it.
_CITY_ROLES = (
    ("Clerk", 17), ("Shopkeeper", 15), ("Courier", 10), ("Officer", 5),
    ("Villager", 12), ("Elder", 8), ("Gardener", 5), ("Busker", 4),
    ("Sailor", 3), ("Dockhand", 4),
)


def _weighted(rng, table):
    total = sum(weight for _name, weight in table)
    roll = rng.next() * total
    for name, weight in table:
        roll -= weight
        if roll <= 0.0:
            return name
    return table[0][0]


# Which mesh each role wears, and how fast it walks. Speed is the one number
# that separates a child sprinting across the square from an elder crossing it.
_ROLE_LOOK = {
    "Villager":   (("SM_Villager_A", "SM_Villager_B", "SM_Villager_C",
                    "SM_Villager_D", "SM_Villager_E"), 155.0),
    "Farmer":     (("SM_Farmer_A", "SM_Farmer_B"), 148.0),
    "Fisher":     (("SM_Fisher_A", "SM_Fisher_B"), 152.0),
    "Merchant":   (("SM_Merchant_A", "SM_Merchant_B"), 142.0),
    "Baker":      (("SM_Merchant_A", "SM_Villager_F"), 150.0),
    "Innkeeper":  (("SM_Merchant_B", "SM_Villager_G"), 140.0),
    "Priest":     (("SM_Priest_A",), 124.0),
    "Smith":      (("SM_Villager_H", "SM_Dockhand_A"), 150.0),
    "Dockhand":   (("SM_Dockhand_A", "SM_Villager_H"), 158.0),
    "Child":      (("SM_Child_A", "SM_Child_B", "SM_Child_C"), 190.0),
    "Elder":      (("SM_Elder_A", "SM_Elder_B"), 96.0),
    "Clerk":      (("SM_Citizen_A", "SM_Citizen_B", "SM_Citizen_C"), 168.0),
    "Shopkeeper": (("SM_Shopkeeper_A", "SM_Citizen_D"), 150.0),
    "Courier":    (("SM_Courier_A",), 205.0),
    "Officer":    (("SM_Officer_A",), 140.0),
    "Busker":     (("SM_Villager_G", "SM_Citizen_E"), 138.0),
    "Gardener":   (("SM_Farmer_A", "SM_Villager_F"), 140.0),
    "Sailor":     (("SM_Fisher_A", "SM_Dockhand_A"), 160.0),
}


def _look_for(rng, role_name):
    meshes, speed = _ROLE_LOOK.get(role_name, _ROLE_LOOK["Villager"])
    mesh = meshes[int(rng.next() * len(meshes)) % len(meshes)]
    return mesh, speed * (0.9 + rng.next() * 0.2)


# ---------------------------------------------------------------------------
# The town
# ---------------------------------------------------------------------------
# The trades a town has exactly one or two of. Drawing them from a weighted
# table means a run where nobody is the priest, which leaves the church empty
# and the bell unrung - so they are placed first, by hand, and then struck off
# the table the rest of the town draws from.
_KEY_TRADES = (
    ("Priest", 1, "Church"),
    ("Innkeeper", 1, "Tavern"),
    ("Baker", 2, "Market"),
    ("Smith", 2, "Work"),
)


def _place_key_trades(pop, rng, world_data, homes, index):
    """One priest, one innkeeper, two bakers, two smiths, before anyone else."""
    s = pop.survey
    e = pop.e
    placed = []

    for role_name, count, near in _KEY_TRADES:
        # Live near the thing they run: the priest by the church, the innkeeper
        # at the inn. The house is then struck off so nobody else gets it.
        if near == "Church":
            focus = s.town_church or s.town_square
        elif near == "Tavern":
            focus = s.town_tavern or s.town_square
        elif near == "Market":
            focus = _nearest(s.town_stalls, s.town_square[0], s.town_square[1], s.town_square)
        else:
            focus = _nearest(s.town_work, s.town_square[0], s.town_square[1], s.town_square)

        for _ in range(count):
            home = _nearest(homes, focus[0], focus[1])
            if home is None:
                break
            homes.remove(home)
            mesh, speed = _look_for(rng, role_name)
            seed = world_data.seed + 300 + index * 17
            pop.spawn(mesh, home[0], home[1], home[2],
                      e.role[role_name], e.species["Person"],
                      "%s %d" % (role_name, index), seed,
                      pop.town_anchors(home, _town_work_for(pop, role_name, home), seed),
                      speed=speed, wander=430.0,
                      display_name=_name_for(index, world_data.seed))
            pop.note(role_name=role_name)
            placed.append(role_name)
            index += 1

    ctx.log("npc: key trades - %s" % ", ".join(placed))
    return index


def _populate_town(pop, rng, world_data):
    s = pop.survey
    e = pop.e
    index = 0

    # A copy, so the key trades can claim their houses without the general
    # loop handing the same doorstep to a second household.
    homes = list(s.town_homes)
    index = _place_key_trades(pop, rng, world_data, homes, index)

    for home in homes:
        if rng.next() > TOWN_HOME_OCCUPANCY:
            continue

        coastal = abs(home[1] - s.shore_y) < 9000.0
        residents = 1 + (1 if rng.next() < TOWN_SECOND_RESIDENT else 0)

        for _ in range(residents):
            role_name = _weighted(rng, _TOWN_ROLES_COASTAL if coastal else _TOWN_ROLES)
            mesh, speed = _look_for(rng, role_name)
            work = _town_work_for(pop, role_name, home, rng)
            seed = world_data.seed + 1000 + index * 17
            pop.spawn(mesh, home[0], home[1], home[2],
                      e.role[role_name], e.species["Person"],
                      "Villager %d" % index, seed,
                      pop.town_anchors(home, work, seed), speed=speed, wander=900.0,
                      display_name=_name_for(index, world_data.seed))
            pop.note(role_name=role_name)
            index += 1

        if rng.next() < TOWN_CHILD_CHANCE:
            mesh, speed = _look_for(rng, "Child")
            seed = world_data.seed + 1000 + index * 17
            pop.spawn(mesh, home[0], home[1], home[2],
                      e.role["Child"], e.species["Person"],
                      "Child %d" % index, seed,
                      pop.town_anchors(home, s.town_square, seed), speed=speed, wander=900.0,
                      display_name=_name_for(index, world_data.seed))
            pop.note(role_name="Child")
            index += 1

    # Farms get their own households, living where they work.
    for farm in s.town_farms:
        for _ in range(FARMERS_PER_FARM):
            role_name = "Farmer"
            mesh, speed = _look_for(rng, role_name)
            seed = world_data.seed + 5000 + index * 17
            anchors = pop.town_anchors(farm, farm, seed)
            pop.spawn(mesh, farm[0], farm[1], farm[2],
                      e.role[role_name], e.species["Person"],
                      "Farmer %d" % index, seed, anchors, speed=speed, wander=1500.0,
                      display_name=_name_for(index, world_data.seed))
            pop.note(role_name=role_name)
            index += 1

    return index


def _town_work_for(pop, role_name, home, rng=None):
    """Where this trade actually works, as opposed to where the routine points."""
    s = pop.survey
    hx, hy = home[0], home[1]
    if role_name in ("Fisher", "Dockhand", "Sailor"):
        return _nearest(s.town_docks, hx, hy, s.town_shore)
    if role_name in ("Merchant", "Baker"):
        return _nearest(s.town_stalls, hx, hy, s.town_square)
    if role_name == "Priest":
        return s.town_church or s.town_square
    if role_name == "Farmer":
        return _nearest(s.town_farms, hx, hy, s.town_square)
    if role_name == "Innkeeper":
        return s.town_tavern or s.town_square
    if role_name == "Smith":
        return _nearest(s.town_work, hx, hy, home)
    if role_name == "Villager":
        # "Villager" is not a trade, it is the absence of one, and sending all
        # ninety of them to the nearest warehouse produced a mob of fifty
        # people standing on the quay and a town with nobody in it. Spread them:
        # most work from home, the rest fill the warehouses, stalls and docks.
        roll = rng.next() if rng is not None else 0.0
        if roll < 0.45:
            return home                                   # a cottage trade
        if roll < 0.65:
            return _nearest(s.town_work, hx, hy, home)
        if roll < 0.80:
            return _nearest(s.town_stalls, hx, hy, s.town_square)
        if roll < 0.92:
            return _nearest(s.town_docks, hx, hy, s.town_shore)
        return _nearest(s.town_farms, hx, hy, s.town_square)
    return None


# ---------------------------------------------------------------------------
# Newhaven
# ---------------------------------------------------------------------------
def _populate_city(pop, rng, world_data, start_index):
    s = pop.survey
    e = pop.e
    if not s.city_homes:
        ctx.warn("npc: no city homes found; is the city stage built?")
        return start_index

    index = start_index
    for home in s.city_homes:
        if rng.next() > CITY_HOME_OCCUPANCY:
            continue
        residents = 1
        if rng.next() < CITY_SECOND_RESIDENT:
            residents += 1
        if rng.next() < CITY_THIRD_RESIDENT:
            residents += 1
        for _ in range(residents):
            role_name = _weighted(rng, _CITY_ROLES)
            mesh, speed = _look_for(rng, role_name)
            work = _city_work_for(pop, role_name, home)
            seed = world_data.seed + 20000 + index * 17
            pop.spawn(mesh, home[0], home[1], home[2],
                      e.role[role_name], e.species["Person"],
                      "Citizen %d" % index, seed,
                      pop.city_anchors(home, work, seed), speed=speed, wander=1700.0,
                      display_name=_name_for(index, world_data.seed))
            pop.note(role_name=role_name)
            index += 1
    return index


def _city_work_for(pop, role_name, home):
    s = pop.survey
    hx, hy = home[0], home[1]
    plaza = s.city_plaza or s.town_square
    if role_name in ("Clerk", "Villager"):
        return _nearest(s.city_work, hx, hy, plaza)
    if role_name == "Shopkeeper":
        return _nearest(s.city_shops, hx, hy, home)
    if role_name in ("Dockhand", "Sailor"):
        return _nearest(s.city_docks, hx, hy, plaza)
    if role_name == "Gardener":
        return _nearest(s.city_parks, hx, hy, plaza)
    if role_name in ("Officer", "Busker", "Courier"):
        return plaza
    return None


# ---------------------------------------------------------------------------
# Animals
# ---------------------------------------------------------------------------
# (species, mesh options, count as a fraction of the anchor list, speed, wander)
def _populate_animals(pop, rng, world_data, start_index):
    s = pop.survey
    e = pop.e
    a = e.anchor
    index = start_index

    def place(species_name, meshes, point, label, speed, wander, extras):
        nonlocal index
        mesh = meshes[int(rng.next() * len(meshes)) % len(meshes)]
        seed = world_data.seed + 40000 + index * 17
        # Animals stand a little way off their anchor so a coop does not become
        # a stack of chickens sharing one coordinate.
        angle = rng.uniform(0.0, 360.0)
        radius = rng.uniform(120.0, wander)
        x = point[0] + math.cos(math.radians(angle)) * radius
        y = point[1] + math.sin(math.radians(angle)) * radius
        z = world_data.height_uu(x, y)
        pop.spawn(mesh, x, y, z, e.role["Villager"], e.species[species_name],
                  "%s %d" % (label, index), seed,
                  pop.animal_anchors((x, y, z), extras),
                  speed=speed, wander=wander,
                  display_name=species_name)
        pop.note(species_name=species_name)
        index += 1

    # -- farmyards: the animals that make the fields read as worked ----------
    for farm in s.town_farms:
        coop = farm
        pasture = (farm[0] + rng.uniform(-3200.0, 3200.0),
                   farm[1] + rng.uniform(-3200.0, 3200.0), 0.0)
        pasture = (pasture[0], pasture[1], world_data.height_uu(pasture[0], pasture[1]))
        extras = [(a["Coop"], coop), (a["Pasture"], pasture),
                  (a["Field"], pasture), (a["Wander"], pasture)]

        for _ in range(int(rng.uniform(2.0, 5.0))):
            place("Chicken", ("SM_Chicken_A", "SM_Chicken_B"), coop, "Chicken",
                  76.0, 620.0, extras)
        for _ in range(int(rng.uniform(1.0, 4.0))):
            place("Sheep", ("SM_Sheep_A", "SM_Sheep_B"), pasture, "Sheep",
                  70.0, 1500.0, extras)
        if rng.next() < 0.55:
            for _ in range(int(rng.uniform(1.0, 3.0))):
                place("Cow", ("SM_Cow_A", "SM_Cow_B"), pasture, "Cow",
                      62.0, 1500.0, extras)
        if rng.next() < 0.4:
            place("Pig", ("SM_Pig_A",), coop, "Pig", 74.0, 700.0, extras)
        if rng.next() < 0.4:
            place("Goat", ("SM_Goat_A",), pasture, "Goat", 82.0, 1200.0, extras)
        if rng.next() < 0.45:
            place("Horse", ("SM_Horse_A", "SM_Horse_B"), pasture, "Horse",
                  110.0, 1600.0, extras)
        if rng.next() < 0.7:
            place("Dog", ("SM_Dog_A", "SM_Dog_B"), farm, "FarmDog",
                  190.0, 1400.0, extras)

    # -- the town: dogs in the lanes, cats on the walls, chickens in the yards
    homes = s.town_homes
    square = s.town_square
    for home in homes:
        if rng.next() < 0.16:
            extras = [(a["Wander"], home), (a["Square"], square),
                      (a["Market"], _nearest(s.town_stalls, home[0], home[1], square)),
                      (a["Coop"], home)]
            place("Dog", ("SM_Dog_A", "SM_Dog_B"), home, "Dog", 196.0, 1300.0, extras)
        if rng.next() < 0.18:
            extras = [(a["Wander"], home), (a["Coop"], home)]
            place("Cat", ("SM_Cat_A", "SM_Cat_B"), home, "Cat", 130.0, 900.0, extras)
        if rng.next() < 0.12:
            extras = [(a["Coop"], home), (a["Wander"], home)]
            place("Chicken", ("SM_Chicken_A", "SM_Chicken_B"), home, "Chicken",
                  76.0, 460.0, extras)

    # -- the shore: gulls on the docks, ducks on the water ------------------
    shore_points = s.town_docks + s.city_docks
    for point in shore_points:
        if rng.next() < 0.5:
            extras = [(a["Shore"], point), (a["Dock"], point),
                      (a["Water"], point), (a["Wander"], point),
                      (a["Market"], _nearest(s.town_stalls, point[0], point[1], square))]
            place("Seagull", ("SM_Seagull_A",), point, "Gull", 120.0, 1400.0, extras)

    for water in s.water:
        for _ in range(int(rng.uniform(2.0, 6.0))):
            extras = [(a["Water"], water), (a["Shore"], water), (a["Wander"], water)]
            place("Duck", ("SM_Duck_A", "SM_Duck_B"), water, "Duck", 68.0, 900.0, extras)

    # -- Newhaven keeps cats and gulls, and somebody always has a dog --------
    for home in s.city_homes:
        if rng.next() < 0.07:
            plaza = s.city_plaza or square
            extras = [(a["Wander"], home), (a["Square"], plaza), (a["Coop"], home)]
            place("Dog", ("SM_Dog_A", "SM_Dog_B"), home, "CityDog", 190.0, 1200.0, extras)
        elif rng.next() < 0.09:
            extras = [(a["Wander"], home), (a["Coop"], home)]
            place("Cat", ("SM_Cat_A", "SM_Cat_B"), home, "CityCat", 130.0, 800.0, extras)

    # -- rabbits in the hedgerows, well away from anybody's door ------------
    extent = world_data.extent - 8000.0
    attempts = int(420 * (world_data.extent / 100800.0) ** 2)
    rabbits = 0
    for _ in range(attempts):
        if rabbits >= 26:
            break
        x = rng.uniform(-extent, extent)
        y = rng.uniform(-extent, extent)
        if world_data.weight_at("Farm", x, y) < 0.3 and world_data.weight_at("Grass", x, y) < 0.5:
            continue
        if world_data.slope_deg(x, y) > 12.0:
            continue
        z = world_data.height_uu(x, y)
        home = (x, y, z)
        place("Rabbit", ("SM_Rabbit_A",), home, "Rabbit", 150.0, 1100.0,
              [(a["Wander"], home), (a["Coop"], home)])
        rabbits += 1

    return index


# ---------------------------------------------------------------------------
def build(world, world_data, meshes=None):
    from . import meshbuild

    if meshes is None:
        meshes = meshbuild.load_all()
    if not meshes:
        ctx.fail("no meshes available; run the 'meshes' stage first")

    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    npc_class = unreal.load_class(None, "/Script/UEGT2.UEGT2NPCActor")
    route_class = unreal.load_class(None, "/Script/UEGT2.UEGT2RouteNetwork")
    if npc_class is None or route_class is None:
        ctx.fail("UEGT2NPCActor/UEGT2RouteNetwork not found; build the editor target first")

    removed = 0
    for actor in subsystem.get_all_level_actors():
        if actor.get_actor_label().startswith(LABEL_PREFIX):
            subsystem.destroy_actor(actor)
            removed += 1
    if removed:
        ctx.log("npc: replaced %d existing inhabitants" % removed)

    _build_routes(world_data, subsystem, route_class)

    survey = _Survey(world_data, subsystem)
    ctx.log(survey.summary())
    survey.warn_gaps()
    if not survey.town_homes:
        ctx.fail("npc: no town houses found; run the 'town' stage first")

    enums = _Enums()
    pop = _Populator(world_data, survey, enums, meshes, subsystem, npc_class)
    rng = _SmallRng(world_data.seed + 60607)

    index = _populate_town(pop, rng, world_data)
    index = _populate_city(pop, rng, world_data, index)
    _populate_animals(pop, rng, world_data, index)

    if pop.missing_meshes:
        ctx.warn("npc: %d meshes missing (run the 'meshes' stage): %s"
                 % (len(pop.missing_meshes), ", ".join(sorted(pop.missing_meshes)[:8])))

    roles = ", ".join("%s %d" % (name, count)
                      for name, count in sorted(pop.by_role.items(), key=lambda kv: -kv[1]))
    species = ", ".join("%s %d" % (name, count)
                        for name, count in sorted(pop.by_species.items(), key=lambda kv: -kv[1]))
    ctx.log("npc: %d people - %s" % (pop.people, roles))
    ctx.log("npc: %d animals - %s" % (pop.animals, species))
    ctx.log("npc: %d inhabitants placed" % pop.count)
    return pop.count
