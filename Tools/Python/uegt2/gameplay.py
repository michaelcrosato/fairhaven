"""Player start, interactables, landmarks and the amenities the player lives on.

Placement is driven by world_features.json plus the actors the town stage
already placed, so moving the town or re-rolling the seed moves everything with
it. Nothing here hard-codes a coordinate that is not derived from the world.

Signs, doors, lamps, carryable props and survey landmarks exercise movement,
reach and physics. The town survey contract adds one objective at a generated
signpost; its discoveries, payment and interface remain native runtime code.

The amenities are the exception, and they are not decoration. The player has
the same four needs and the same purse as every inhabitant, and an amenity is
how a need gets answered: each one stands on a point the npc stage already
resolves an anchor to, so the bakehouse the player eats at is the bakehouse the
villagers eat at, and the quay they work is the quay the dockhands work.
"""
from __future__ import annotations

import math

import unreal

from . import ctx
from . import gen_town
from . import npc as npc_mod
from . import town as town_mod
from .meshkit import _SmallRng

PLAYER_START_LABEL = "Fairhaven Player Start"
LABEL_PREFIX = "Play "
# The lamps are the exception to the clear-and-replace pattern; see _place_lamps.
LAMP_PREFIX = LABEL_PREFIX + "Lamp "

# Just off the town square, looking east down the street toward the harbour:
# the first thing a playtester sees should show the town and the water.
START_OFFSET = (-3200.0, -6400.0)
START_YAW = 62.0
START_CLEARANCE = 120.0


def _subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _enum(type_name, value_name):
    """unreal.<Type>.<VALUE>, with a readable failure instead of AttributeError."""
    enum_type = getattr(unreal, type_name, None)
    if enum_type is None:
        ctx.fail("enum type unreal.%s not found; build the editor target first" % type_name)
    value = getattr(enum_type, value_name, None)
    if value is None:
        ctx.fail("enum value %s.%s not found" % (type_name, value_name))
    return value


def _load_class(name):
    cls = unreal.load_class(None, "/Script/UEGT2." + name)
    if cls is None:
        ctx.fail("%s not found; build the editor target first" % name)
    return cls


def _clear(prefixes, keep=()):
    removed = 0
    for actor in _subsystem().get_all_level_actors():
        label = actor.get_actor_label()
        if any(label.startswith(prefix) for prefix in keep):
            continue
        if any(label.startswith(prefix) for prefix in prefixes):
            _subsystem().destroy_actor(actor)
            removed += 1
    return removed


def build_player_start(world, world_data):
    _clear([PLAYER_START_LABEL])

    town = world_data.town["center"]
    wx = town[0] + START_OFFSET[0]
    wy = town[1] + START_OFFSET[1]
    wz = world_data.height_uu(wx, wy) + START_CLEARANCE

    start = _subsystem().spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(wx, wy, wz),
        unreal.Rotator(0.0, 0.0, START_YAW))
    if start is None:
        ctx.fail("could not spawn PlayerStart")
    start.set_actor_label(PLAYER_START_LABEL)
    ctx.log("player start at (%.0f, %.0f, %.0f), yaw %.0f" % (wx, wy, wz, START_YAW))
    return start


def _spawn_interactable(cls, mesh, wx, wy, wz, yaw, label, mobility=None):
    actor = _subsystem().spawn_actor_from_class(
        cls, unreal.Vector(wx, wy, wz), unreal.Rotator(0.0, 0.0, yaw))
    if actor is None:
        return None
    if mesh is not None:
        actor.set_interactable_mesh(mesh)
    component = actor.get_mesh_component()
    if component is not None and mobility is not None:
        component.set_editor_property("mobility", mobility)
    actor.set_actor_label(LABEL_PREFIX + label)
    return actor


# ---------------------------------------------------------------------------
def _place_landmarks(world_data, meshes):
    """Survey points at the places worth walking to."""
    cls = _load_class("UEGT2Landmark")
    marker = meshes.get("SM_SignPost_A")
    cx, cy = world_data.town["center"]

    def coast_y(wx):
        best = None
        for (x, y) in world_data.coast:
            if best is None or abs(x - wx) < abs(best[0] - wx):
                best = (x, y)
        return best[1] if best else 30000.0

    # These IDs are save data. Renaming a sign or moving its placement must
    # not lose the player's discovery; changing an ID requires a migration.
    spots = [
        ("fairhaven_square", "Fairhaven Square", cx + 900.0, cy - 900.0),
        ("fairhaven_harbour", "The Harbour", cx - 1200.0, coast_y(cx) - 1500.0),
        ("fairhaven_light", "Fairhaven Light", cx + 16000.0, coast_y(cx + 16000.0) - 1600.0),
        ("mill_rise", "Mill Rise", cx - 6000.0, cy - 26000.0),
        ("high_road", "The High Road", cx + 52000.0, cy + 15000.0),
        ("green_lagoon", "Green Lagoon", -60000.0, 9000.0),
        ("southern_palms", "Southern Palms", -46000.0, 17000.0),
        ("summit_road", "The Summit Road", 148000.0, -2500.0),
    ]

    # Newhaven, derived from the city block so the markers follow if it moves.
    city = getattr(world_data, "city", None)
    if city:
        ncx, ncy = city["center"]
        spots.extend([
            ("newhaven_plaza", "Newhaven Plaza", ncx + 1200.0, ncy - 1200.0),
            ("newhaven_wharf", "Newhaven Wharf", ncx, coast_y(ncx) - 2200.0),
            ("newhaven_heights", "Newhaven Heights", ncx - 14000.0, ncy + 12000.0),
        ])

    placed = 0
    for persistent_id, name, wx, wy in spots:
        wz = world_data.height_uu(wx, wy) - 10.0
        actor = _spawn_interactable(cls, marker, wx, wy, wz, 0.0,
                                    "Landmark %s" % name,
                                    unreal.ComponentMobility.STATIC)
        if actor is not None:
            actor.set_editor_property("persistent_id", unreal.Name(persistent_id))
            actor.set_landmark_name(unreal.Text(name) if hasattr(unreal, "Text") else name)
            placed += 1
    ctx.log("gameplay: %d landmarks" % placed)
    return placed


def _place_signs(world_data, meshes):
    """Signposts where roads leave town, telling the player what is that way."""
    cls = _load_class("UEGT2Sign")
    marker = meshes.get("SM_SignPost_A")

    messages = {
        "CoastRoad": "Coast Road  -  the shore, north and south",
        "MountainRoad": "High Road  -  the river valley and the mountains",
        "FarmRoad": "Farm Road  -  the western fields",
        "NorthLane": "North Lane  -  upland pasture",
        "SouthRoad": "South Road  -  the warm country and the lagoon",
        "LagoonSpur": "Lagoon Track",
    }

    placed = 0
    for road in world_data.roads:
        if road["is_street"]:
            continue
        text = messages.get(road["name"])
        if text is None:
            continue
        points = road["points"]
        # A little way along the road from the town end.
        index = min(6, len(points) - 1)
        wx, wy = points[index][0], points[index][1]
        wx += 420.0
        wz = world_data.height_uu(wx, wy) - 10.0
        actor = _spawn_interactable(cls, marker, wx, wy, wz, 0.0,
                                    "Sign %s" % road["name"],
                                    unreal.ComponentMobility.STATIC)
        if actor is not None:
            actor.set_sign_text(text)
            placed += 1

    ctx.log("gameplay: %d signposts" % placed)
    return placed


def _place_survey_contract(world, world_data, meshes):
    """One clear sign beside the square; reruns choose the same first free spot."""
    marker = meshes.get("SM_SignPost_A")
    if marker is None:
        ctx.fail("gameplay: survey contract requires SM_SignPost_A")
    cls = _load_class("UEGT2SurveyContract")
    cx, cy = world_data.town["center"]
    # The plaza's outer stalls are on a 2150 cm ring. Start beyond that ring,
    # toward the player's arrival street, and try fixed nearby alternatives.
    # Collision queries include instanced fences/trees and invisible amenities,
    # rather than treating a whole scatter field's bounds as one obstacle.
    candidates = ((-1200.0, -2800.0), (-1800.0, -2800.0), (-600.0, -2800.0),
                  (0.0, -2800.0), (600.0, -2800.0), (-2400.0, -2800.0),
                  (-2800.0, -1800.0), (-2800.0, -1200.0), (-2800.0, -600.0))
    # The NPC stage follows gameplay in an all-stage build, but old inhabitants
    # still exist in a gameplay-only rebuild. They must not change placement.
    ignored = [actor for actor in _subsystem().get_all_level_actors()
               if actor.get_actor_label().startswith(npc_mod.LABEL_PREFIX)]
    object_types = [unreal.ObjectTypeQuery.ECC_WORLD_STATIC,
                    unreal.ObjectTypeQuery.ECC_WORLD_DYNAMIC,
                    unreal.ObjectTypeQuery.ECC_PHYSICS_BODY]
    for dx, dy in candidates:
        wx, wy = cx + dx, cy + dy
        wz = world_data.height_uu(wx, wy)
        heights = [world_data.height_uu(wx + sx, wy + sy)
                   for sx in (-260.0, 0.0, 260.0)
                   for sy in (-260.0, 0.0, 260.0)]
        if not all(math.isfinite(h) for h in heights) or max(heights) - min(heights) > 15.0:
            continue
        occupied = unreal.SystemLibrary.box_overlap_actors(
            world, unreal.Vector(wx, wy, wz + 150.0),
            unreal.Vector(280.0, 280.0, 130.0), object_types, None, ignored)
        if occupied:
            continue
        # The broad faces lie in local Y/Z. Face the arrival street (-X).
        actor = _spawn_interactable(cls, marker, wx, wy, wz - 10.0, 0.0,
                                    "Town Survey Contract", unreal.ComponentMobility.STATIC)
        if actor is None:
            ctx.fail("gameplay: could not spawn town survey contract")
        actor.set_use_range(340.0)
        ctx.log("gameplay: town survey contract at (%.0f, %.0f, %.0f), clear approach" % (wx, wy, wz - 10.0))
        return 1
    ctx.fail("gameplay: no clear town survey contract position near the square")


def _place_doors(world_data, meshes):
    """A working front door on every town house.

    It used to be a fifth of them, to keep the actor count down, and that was
    fine while a door opened onto a solid wall. Now the wall behind it has a
    hole in it and a furnished room beyond, so a house without a door is a house
    standing permanently open - and the door is the only thing between the
    street and the inside.
    """
    cls = _load_class("UEGT2Door")
    leaf = meshes.get("SM_Door_A")
    if leaf is None:
        ctx.warn("gameplay: SM_Door_A missing, skipping doors")
        return 0

    placed = 0
    for actor in _subsystem().get_all_level_actors():
        label = actor.get_actor_label()
        if not label.startswith("Town House "):
            continue
        component = actor.get_editor_property("static_mesh_component")
        mesh = component.get_editor_property("static_mesh") if component else None
        if mesh is None:
            continue
        depth = town_mod.HOUSE_DEPTH.get(mesh.get_name())
        if depth is None:
            continue

        location = actor.get_actor_location()
        yaw = actor.get_actor_rotation().yaw
        radians = math.radians(yaw)
        cos_y, sin_y = math.cos(radians), math.sin(radians)

        # Hinge at the left edge of the opening, in the plane of the wall.
        # gen_town.DOOR_W is the opening; the leaf is hung from its left jamb and
        # extends along +X from its own origin, so the actor's yaw swings it.
        local_x = -gen_town.DOOR_W * 0.5
        local_y = -(depth * 0.5) + gen_town.WALL_T * 0.5
        wx = location.x + local_x * cos_y - local_y * sin_y
        wy = location.y + local_x * sin_y + local_y * cos_y
        wz = location.z + gen_town.PLINTH_H

        if _spawn_interactable(cls, leaf, wx, wy, wz, yaw, "Door %d" % placed,
                               unreal.ComponentMobility.MOVABLE):
            placed += 1

    ctx.log("gameplay: %d working doors" % placed)
    return placed


def _place_service_signs(world_data, meshes):
    """A signpost outside every shop, saying what it is.

    Without one a bakery is a room with a counter in it. The trade is read back
    off the actor label the town stage wrote, so there is one list of trades and
    one list of names, and they are in the module that places the shops.
    """
    cls = _load_class("UEGT2Sign")
    marker = meshes.get("SM_SignPost_A")

    placed = 0
    for actor in _subsystem().get_all_level_actors():
        label = actor.get_actor_label()
        if not label.startswith("Town Shop "):
            continue
        parts = label.split()
        if len(parts) < 4:
            continue
        venue = parts[2]
        name = town_mod.SERVICE_NAMES.get(venue)
        if name is None:
            continue

        location = actor.get_actor_location()
        yaw = actor.get_actor_rotation().yaw
        radians = math.radians(yaw)
        cos_y, sin_y = math.cos(radians), math.sin(radians)
        depth = town_mod.HOUSE_DEPTH.get("SM_House_A", 580.0)
        mesh_comp = actor.get_editor_property("static_mesh_component")
        mesh = mesh_comp.get_editor_property("static_mesh") if mesh_comp else None
        if mesh is not None:
            depth = town_mod.HOUSE_DEPTH.get(mesh.get_name(), depth)
        # Beside the door, clear of the steps.
        local_x, local_y = 210.0, -(depth * 0.5 + 250.0)
        wx = location.x + local_x * cos_y - local_y * sin_y
        wy = location.y + local_x * sin_y + local_y * cos_y
        wz = world_data.height_uu(wx, wy) - 10.0
        sign = _spawn_interactable(cls, marker, wx, wy, wz, yaw + 180.0,
                                   "Shop %s" % name, unreal.ComponentMobility.STATIC)
        if sign is not None:
            sign.set_sign_text(name)
            placed += 1

    ctx.log("gameplay: %d shop signs" % placed)
    return placed


def _place_lamps(world_data, meshes):
    """Interactable lamps around the square, replacing plain lamp props.

    This is the one thing in the stage that *consumes* an actor another stage
    placed: the plain lamp is destroyed and an interactable one takes its spot.
    That makes it the one thing here that must not be undone and redone, which
    is why the Play Lamp actors survive the clear at the top of build() and why
    this returns early when they are already standing. Rebuilding the stage on
    its own used to cost the square sixteen lamp posts a time - they went into
    the clear and the town lamps that had paid for them were long gone.
    """
    cls = _load_class("UEGT2Lamp")
    mesh = meshes.get("SM_LampPost_A")
    cx, cy = world_data.town["center"]

    kept = [actor for actor in _subsystem().get_all_level_actors()
            if actor.get_actor_label().startswith(LAMP_PREFIX)]
    if kept:
        ctx.log("gameplay: %d interactable lamps (already placed)" % len(kept))
        return len(kept)

    placed = 0
    replaced = []
    for actor in _subsystem().get_all_level_actors():
        if not actor.get_actor_label().startswith("Town Lamp "):
            continue
        location = actor.get_actor_location()
        if math.hypot(location.x - cx, location.y - cy) > 12000.0:
            continue
        replaced.append((actor, location))

    for actor, location in replaced[:16]:
        if _spawn_interactable(cls, mesh, location.x, location.y, location.z, 0.0,
                               "Lamp %d" % placed, unreal.ComponentMobility.MOVABLE):
            _subsystem().destroy_actor(actor)
            placed += 1

    ctx.log("gameplay: %d interactable lamps" % placed)
    return placed


def _place_pickups(world_data, meshes):
    """Carryable crates and barrels around the square and the docks."""
    cls = _load_class("UEGT2Pickup")
    rng = _SmallRng(world_data.seed + 771)
    cx, cy = world_data.town["center"]

    def coast_y(wx):
        best = None
        for (x, y) in world_data.coast:
            if best is None or abs(x - wx) < abs(best[0] - wx):
                best = (x, y)
        return best[1] if best else 30000.0

    spots = []
    for i in range(10):
        angle = rng.uniform(0.0, 360.0)
        radius = rng.uniform(900.0, 3400.0)
        spots.append((cx + math.cos(math.radians(angle)) * radius,
                      cy + math.sin(math.radians(angle)) * radius))
    for i in range(8):
        spots.append((cx + rng.uniform(-3600.0, 3600.0),
                      coast_y(cx) - rng.uniform(1500.0, 2600.0)))

    placed = 0
    for wx, wy in spots:
        name = "SM_Crate_A" if rng.next() > 0.45 else "SM_Barrel_A"
        mesh = meshes.get(name)
        wz = world_data.height_uu(wx, wy) + 70.0
        if _spawn_interactable(cls, mesh, wx, wy, wz, rng.uniform(0.0, 360.0),
                               "Pickup %d" % placed, unreal.ComponentMobility.MOVABLE):
            placed += 1

    ctx.log("gameplay: %d carryable props" % placed)
    return placed


# ---------------------------------------------------------------------------
# Amenities
# ---------------------------------------------------------------------------
# How many of a kind are worth placing. A town has four privies and everybody
# should be able to use all four; it has ninety office doorways and the
# ninetieth adds nothing but an actor. The caps only ever bite on the sets that
# come from a loop over a city block.
# Caps apply to the city sets and never to the town ones. Fairhaven has four
# privies and three food shops and every one of them should work; Newhaven has
# a lobby with a washroom in it on every block, and the hundred and sixtieth
# invisible prompt in a doorway adds nothing but an actor.
_AMENITY_CAPS = {
    "city_washrooms": 20,
    "city_food": 20,
    "town_trade": 20,
    "city_trade": 16,
    "docks": 8,
    "city_work": 16,
    "city_parks": 6,
}


def _closest(points, x, y):
    """The nearest of a list of (x, y, z, ...) points, or None."""
    best = None
    best_distance = None
    for point in points:
        distance = (point[0] - x) ** 2 + (point[1] - y) ** 2
        if best_distance is None or distance < best_distance:
            best_distance = distance
            best = point
    return best


def _spread(points, limit):
    """At most ``limit`` of these, evenly spaced through the list.

    Evenly spaced rather than the first N: the survey walks the level in
    placement order, so the first twenty city workplaces are all on one block.
    """
    if limit <= 0 or not points:
        return []
    if len(points) <= limit:
        return list(points)
    step = len(points) / float(limit)
    return [points[int(index * step)] for index in range(limit)]


def _place_amenities(world_data, start_xy):
    """The places the player eats, washes, sits, sleeps and earns.

    Every point here comes out of the same survey the npc stage populates from,
    which is the whole point: an amenity the town does not use is a prop, and a
    need the player answers somewhere nobody else goes is a different game.
    """
    cls = _load_class("UEGT2Amenity")
    survey = npc_mod.survey_world(world_data, _subsystem())

    kind = {name: _enum("UEGT2AmenityKind", name.upper()) for name in (
        "Food", "Tavern", "Washroom", "Seat", "Bed", "Larder", "Work", "Market",
        "Worship")}
    role = {name: _enum("UEGT2NPCRole", name.upper()) for name in (
        "Villager", "Farmer", "Fisher", "Merchant", "Innkeeper", "Priest",
        "Dockhand", "Clerk", "Shopkeeper", "Gardener")}

    counts = {}

    def add(kind_name, point, venue="", role_name="Villager", use_range=420.0):
        wx, wy, wz = point[0], point[1], point[2]
        actor = _subsystem().spawn_actor_from_class(
            cls, unreal.Vector(wx, wy, wz), unreal.Rotator(0.0, 0.0, 0.0))
        if actor is None:
            return False
        index = counts.get(kind_name, 0)
        actor.set_actor_label(LABEL_PREFIX + "Amenity %s %d" % (kind_name, index))
        actor.configure_amenity(kind[kind_name], venue, role[role_name])
        actor.set_use_range(use_range)
        counts[kind_name] = index + 1
        return True

    # --- somewhere to go ---------------------------------------------------
    # The *_venues lists, not the anchor lists: these stand in front of the
    # prop rather than inside it. See _Survey.
    for point in survey.town_wash_venues:
        add("Washroom", point, use_range=320.0)
    for point in _spread(survey.city_wash_venues, _AMENITY_CAPS["city_washrooms"]):
        add("Washroom", point, use_range=320.0)

    # --- somewhere to sit --------------------------------------------------
    for point in survey.seat_venues:
        add("Seat", point, use_range=280.0)

    # --- somewhere to eat --------------------------------------------------
    for point in survey.food_venues:
        add("Food", point, venue=point[3])
    for point in _spread(survey.city_food_venues, _AMENITY_CAPS["city_food"]):
        add("Food", point, venue=point[3])

    # Half the market stalls sell food and half want somebody behind them. Both
    # stand on the Food and Market anchors the routines already use, so this is
    # the square doing what the square does, from either side of the counter.
    for index, point in enumerate(survey.town_stalls):
        if index % 2 == 0:
            add("Food", point, venue="a market stall")
        else:
            add("Market", point, venue="a market stall", role_name="Merchant")

    # --- somewhere to drink ------------------------------------------------
    for point, name in ((survey.town_tavern, "The Fairhaven Inn"),
                        (getattr(survey, "city_tavern", None), "the Newhaven tavern")):
        if point:
            add("Tavern", point, venue=name, role_name="Innkeeper")

    # --- somewhere to sit quietly ------------------------------------------
    if survey.town_church:
        add("Worship", survey.town_church, venue="the church", role_name="Priest",
            use_range=520.0)

    # --- somewhere to earn -------------------------------------------------
    for point in _spread(survey.trade_venues, _AMENITY_CAPS["town_trade"]):
        add("Work", point, venue=point[3], role_name=point[4], use_range=560.0)

    for point in _spread(survey.city_trade_venues, _AMENITY_CAPS["city_trade"]):
        add("Work", point, venue=point[3], role_name=point[4], use_range=560.0)

    for point in _spread(survey.town_docks, _AMENITY_CAPS["docks"]):
        add("Work", point, venue="the harbour", role_name="Fisher", use_range=560.0)

    for point in _spread(survey.city_docks, _AMENITY_CAPS["docks"]):
        add("Work", point, venue="the Newhaven wharf", role_name="Dockhand",
            use_range=560.0)

    for point in _spread(survey.city_work, _AMENITY_CAPS["city_work"]):
        add("Work", point, venue="the offices", role_name="Clerk", use_range=560.0)

    for point in _spread(survey.city_parks, _AMENITY_CAPS["city_parks"]):
        add("Work", point, venue="the parks", role_name="Gardener", use_range=560.0)

    # --- somewhere to sleep ------------------------------------------------
    # Whichever house is nearest where the player wakes up is the one they came
    # out of. Derived, like everything else here, so re-rolling the seed moves
    # the player's bed along with their front door.
    home = _closest(survey.town_homes, start_xy[0], start_xy[1])
    if home:
        # Both stand to one side of the doorstep rather than on it. The survey
        # point is 190 cm out from the wall, which is exactly where you stand to
        # open the front door, and an amenity volume there would answer the
        # interaction probe before the door did.
        add("Bed", (home[0] - 190.0, home[1], home[2]),
            venue="your lodgings", use_range=340.0)
        # And a kitchen on the other side. Everybody in the town eats three free
        # meals a day at home; without this the player is the only person in
        # Fairhaven who has to buy every single one.
        add("Larder", (home[0] + 190.0, home[1], home[2]),
            venue="your lodgings", use_range=340.0)
    else:
        ctx.warn("gameplay: no town homes surveyed - the player has nowhere to sleep")

    total = sum(counts.values())
    ctx.log("gameplay: %d amenities - %s" % (total, ", ".join(
        "%d %s" % (value, name.lower()) for name, value in sorted(counts.items()))))
    for name in ("Food", "Washroom", "Seat", "Work", "Bed"):
        if counts.get(name, 0) == 0:
            ctx.warn("gameplay: no %s amenities placed - the player cannot answer "
                     "that need at all" % name.lower())
    return total


def build(world, world_data, meshes=None):
    from . import meshbuild

    if meshes is None:
        meshes = meshbuild.load_all()

    # The lamps are kept: each one cost a town lamp post to make, and that
    # town lamp post is not coming back. See _place_lamps.
    _clear([LABEL_PREFIX], keep=[LAMP_PREFIX])
    start = build_player_start(world, world_data)

    start_location = start.get_actor_location()

    total = 0
    total += _place_landmarks(world_data, meshes)
    total += _place_signs(world_data, meshes)
    total += _place_doors(world_data, meshes)
    total += _place_service_signs(world_data, meshes)
    total += _place_lamps(world_data, meshes)
    total += _place_pickups(world_data, meshes)
    total += _place_amenities(world_data, (start_location.x, start_location.y))
    total += _place_survey_contract(world, world_data, meshes)

    settings = world.get_world_settings()
    ctx.set_prop(settings, "kill_z", -20000.0)

    ctx.log("gameplay: %d interactables placed" % total)
    return {"player_start": start, "interactables": total}
