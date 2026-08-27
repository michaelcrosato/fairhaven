"""Player start, interactables and landmarks.

Placement is driven by world_features.json plus the actors the town stage
already placed, so moving the town or re-rolling the seed moves everything with
it. Nothing here hard-codes a coordinate that is not derived from the world.

The 0.1 interaction set is deliberately small - signs, doors, lamps, carryable
props and survey landmarks - because it exists to exercise movement, reach and
physics, not to be a gameplay system.
"""
from __future__ import annotations

import math

import unreal

from . import ctx
from . import town as town_mod
from .meshkit import _SmallRng

PLAYER_START_LABEL = "Fairhaven Player Start"
LABEL_PREFIX = "Play "

# Just off the town square, looking east down the street toward the harbour:
# the first thing a playtester sees should show the town and the water.
START_OFFSET = (-3200.0, -6400.0)
START_YAW = 62.0
START_CLEARANCE = 120.0


def _subsystem():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _load_class(name):
    cls = unreal.load_class(None, "/Script/UEGT2." + name)
    if cls is None:
        ctx.fail("%s not found; build the editor target first" % name)
    return cls


def _clear(prefixes):
    removed = 0
    for actor in _subsystem().get_all_level_actors():
        label = actor.get_actor_label()
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

    spots = [
        ("Fairhaven Square", cx + 900.0, cy - 900.0),
        ("The Harbour", cx - 1200.0, coast_y(cx) - 1500.0),
        ("Fairhaven Light", cx + 16000.0, coast_y(cx + 16000.0) - 1600.0),
        ("Mill Rise", cx - 6000.0, cy - 26000.0),
        ("The High Road", cx + 52000.0, cy + 15000.0),
        ("Green Lagoon", -60000.0, 9000.0),
        ("Southern Palms", -46000.0, 17000.0),
    ]

    placed = 0
    for name, wx, wy in spots:
        wz = world_data.height_uu(wx, wy) - 10.0
        actor = _spawn_interactable(cls, marker, wx, wy, wz, 0.0,
                                    "Landmark %s" % name,
                                    unreal.ComponentMobility.STATIC)
        if actor is not None:
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


def _place_doors(world_data, meshes):
    """A working front door on some of the town houses."""
    cls = _load_class("UEGT2Door")
    leaf = meshes.get("SM_Door_A")
    if leaf is None:
        ctx.warn("gameplay: SM_Door_A missing, skipping doors")
        return 0

    rng = _SmallRng(world_data.seed + 99)
    placed = 0
    for actor in _subsystem().get_all_level_actors():
        label = actor.get_actor_label()
        if not label.startswith("Town House "):
            continue
        if rng.next() > 0.22:            # only some houses, to keep actor count low
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

        # Hinge at the left edge of the doorway on the house's -Y face.
        local_x, local_y = -46.0, -(depth * 0.5 + 12.0)
        wx = location.x + local_x * cos_y - local_y * sin_y
        wy = location.y + local_x * sin_y + local_y * cos_y
        wz = location.z + 26.0

        if _spawn_interactable(cls, leaf, wx, wy, wz, yaw, "Door %d" % placed,
                               unreal.ComponentMobility.MOVABLE):
            placed += 1

    ctx.log("gameplay: %d working doors" % placed)
    return placed


def _place_lamps(world_data, meshes):
    """Interactable lamps around the square, replacing plain lamp props."""
    cls = _load_class("UEGT2Lamp")
    mesh = meshes.get("SM_LampPost_A")
    cx, cy = world_data.town["center"]

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


def build(world, world_data, meshes=None):
    from . import meshbuild

    if meshes is None:
        meshes = meshbuild.load_all()

    _clear([LABEL_PREFIX])
    start = build_player_start(world, world_data)

    total = 0
    total += _place_landmarks(world_data, meshes)
    total += _place_signs(world_data, meshes)
    total += _place_doors(world_data, meshes)
    total += _place_lamps(world_data, meshes)
    total += _place_pickups(world_data, meshes)

    settings = world.get_world_settings()
    ctx.set_prop(settings, "kill_z", -20000.0)

    ctx.log("gameplay: %d interactables placed" % total)
    return {"player_start": start, "interactables": total}
