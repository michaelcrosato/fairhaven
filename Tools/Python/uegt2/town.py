"""Places the town, farm buildings, waterfront and landmarks.

Buildings are laid out from the same street data the terrain generator used to
carve the roads, so the town always sits on its own streets. Everything is
rejected against a simple occupancy list, which keeps buildings from growing
into each other without needing a real city generator.

Positions are derived, never hard-coded: move the town in world_config.py and
everything here follows.
"""
from __future__ import annotations

import math

import unreal

from . import ctx
from .meshkit import _SmallRng

LABEL_PREFIX = "Town "

# Footprint radius (cm) used for overlap rejection, per building mesh.
BUILDING_FOOTPRINT = {
    "SM_House_A": 500.0, "SM_House_B": 480.0, "SM_House_C": 550.0,
    "SM_House_D": 470.0, "SM_House_E": 560.0, "SM_Cottage_A": 380.0,
    "SM_Warehouse_A": 640.0, "SM_Shed_A": 270.0, "SM_Barn_A": 740.0,
    "SM_Church_A": 800.0, "SM_Windmill_A": 420.0, "SM_MarketStall_A": 300.0,
    "SM_Lighthouse_A": 340.0,
}

TOWN_HOUSES = ["SM_House_A", "SM_House_B", "SM_House_C", "SM_House_D",
               "SM_House_E", "SM_Cottage_A"]

# Depth (local Y) of each house, so the gameplay stage can find its doorway.
# Keep in step with the sizes passed to town.house() in meshbuild._catalog().
HOUSE_DEPTH = {
    "SM_House_A": 580.0, "SM_House_B": 620.0, "SM_House_C": 560.0,
    "SM_House_D": 640.0, "SM_House_E": 700.0, "SM_Cottage_A": 460.0,
}


class Placer(object):
    """Spawns static mesh actors and keeps them from overlapping."""

    def __init__(self, world_data, meshes, label_prefix=LABEL_PREFIX):
        self.wd = world_data
        self.meshes = meshes
        self.subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        self.occupied = []           # (x, y, radius)
        self.count = 0
        # Parameterised so the Newhaven stage can reuse the same spawner and
        # overlap rejection without clearing the town on the way past.
        self.prefix = label_prefix

    def clear(self):
        removed = 0
        for actor in self.subsystem.get_all_level_actors():
            if actor.get_actor_label().startswith(self.prefix):
                self.subsystem.destroy_actor(actor)
                removed += 1
        return removed

    def is_free(self, wx, wy, radius):
        for (ox, oy, orad) in self.occupied:
            if (wx - ox) ** 2 + (wy - oy) ** 2 < (radius + orad) ** 2:
                return False
        return True

    def reserve(self, wx, wy, radius):
        self.occupied.append((wx, wy, radius))

    def ground(self, wx, wy, footprint=0.0):
        """Lowest terrain height across the footprint, so nothing floats."""
        if footprint <= 0.0:
            return self.wd.height_uu(wx, wy)
        samples = [self.wd.height_uu(wx + dx, wy + dy)
                   for dx, dy in ((0, 0), (footprint, 0), (-footprint, 0),
                                  (0, footprint), (0, -footprint))]
        return min(samples)

    def place(self, mesh_name, wx, wy, yaw, label, radius=0.0, z_offset=0.0,
              scale=1.0, footprint=0.0, check=True):
        if check and radius > 0.0 and not self.is_free(wx, wy, radius):
            return None
        mesh = self.meshes.get(mesh_name)
        if mesh is None:
            ctx.warn("%s: mesh %s missing" % (self.prefix.strip(), mesh_name))
            return None

        z = self.ground(wx, wy, footprint) + z_offset
        actor = self.subsystem.spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(wx, wy, z),
            unreal.Rotator(0.0, 0.0, yaw))
        if actor is None:
            return None

        component = actor.get_editor_property("static_mesh_component")
        component.set_editor_property("static_mesh", mesh)
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        if scale != 1.0:
            actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
        actor.set_actor_label("%s%s" % (self.prefix, label))

        if radius > 0.0:
            self.reserve(wx, wy, radius)
        self.count += 1
        return actor


def _face_yaw(normal_x, normal_y):
    """Yaw so the building's door face (-Y) points back along the given normal."""
    return math.degrees(math.atan2(-normal_x, normal_y))


def _walk(points, step):
    """Yield (x, y, tangent_x, tangent_y) along a polyline at fixed spacing."""
    carry = 0.0
    for a, b in zip(points, points[1:]):
        ax, ay = a[0], a[1]
        bx, by = b[0], b[1]
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        if length < 1e-3:
            continue
        tx, ty = dx / length, dy / length
        distance = carry
        while distance < length:
            yield (ax + tx * distance, ay + ty * distance, tx, ty)
            distance += step
        carry = distance - length


# ---------------------------------------------------------------------------
# Districts
# ---------------------------------------------------------------------------
def _place_streets(placer, rng):
    """Houses along both sides of every town street, plus lamps and benches."""
    # is_city matters: the city grid is also flagged is_street, and without
    # this the town stage would line Newhaven avenues with thatched cottages.
    streets = [r for r in placer.wd.roads
               if r["is_street"] and not r.get("is_city")]
    houses = 0
    lamps = 0

    for street in streets:
        half = street["width_uu"] * 0.5
        setback = half + 620.0

        for (x, y, tx, ty) in _walk(street["points"], 2050.0):
            for side in (-1.0, 1.0):
                # Gaps between plots: a solid wall of houses reads as a city.
                if rng.next() < 0.28:
                    continue
                nx, ny = -ty * side, tx * side       # perpendicular to the street
                wx = x + nx * setback
                wy = y + ny * setback

                name = TOWN_HOUSES[int(rng.next() * len(TOWN_HOUSES)) % len(TOWN_HOUSES)]
                radius = BUILDING_FOOTPRINT.get(name, 480.0)
                jitter_x = rng.uniform(-90.0, 90.0)
                jitter_y = rng.uniform(-90.0, 90.0)
                yaw = _face_yaw(nx, ny) + rng.uniform(-4.0, 4.0)

                if placer.place(name, wx + jitter_x, wy + jitter_y, yaw,
                                "House %d" % houses, radius=radius,
                                z_offset=-20.0, footprint=radius * 0.7):
                    houses += 1

        # Street furniture on alternating sides.
        for index, (x, y, tx, ty) in enumerate(_walk(street["points"], 1500.0)):
            side = 1.0 if index % 2 == 0 else -1.0
            nx, ny = -ty * side, tx * side
            wx, wy = x + nx * (half + 130.0), y + ny * (half + 130.0)
            if placer.place("SM_LampPost_A", wx, wy, 0.0, "Lamp %d" % lamps,
                            radius=90.0, z_offset=-10.0):
                placer.place("SM_LampPost_Glow", wx, wy, 0.0, "LampGlow %d" % lamps,
                             z_offset=-10.0, check=False)
                lamps += 1

    ctx.log("town: %d houses, %d lamp posts along %d streets"
            % (houses, lamps, len(streets)))
    return houses


def _place_plaza(placer, rng):
    """The town square: a well, market stalls, benches and a few villagers."""
    cx, cy = placer.wd.town["center"]
    placer.place("SM_Well_A", cx, cy, 0.0, "Well", radius=260.0, z_offset=-15.0)

    for i in range(5):
        angle = 72.0 * i + 18.0
        r = 1150.0
        wx = cx + math.cos(math.radians(angle)) * r
        wy = cy + math.sin(math.radians(angle)) * r
        placer.place("SM_MarketStall_A", wx, wy, angle + 180.0,
                     "Stall %d" % i, radius=300.0, z_offset=-10.0)

    for i in range(6):
        angle = 60.0 * i + 30.0
        r = 700.0
        wx = cx + math.cos(math.radians(angle)) * r
        wy = cy + math.sin(math.radians(angle)) * r
        placer.place("SM_Bench_A", wx, wy, angle + 90.0, "Bench %d" % i,
                     radius=170.0, z_offset=-8.0)

    villagers = ["SM_Villager_A", "SM_Villager_B", "SM_Villager_C", "SM_Villager_D"]
    for i in range(14):
        angle = rng.uniform(0.0, 360.0)
        r = rng.uniform(400.0, 4200.0)
        wx = cx + math.cos(math.radians(angle)) * r
        wy = cy + math.sin(math.radians(angle)) * r
        name = villagers[int(rng.next() * len(villagers)) % len(villagers)]
        placer.place(name, wx, wy, rng.uniform(0.0, 360.0), "Villager %d" % i,
                     radius=130.0, z_offset=-4.0)

    for i in range(10):
        wx = cx + rng.uniform(-3800.0, 3800.0)
        wy = cy + rng.uniform(-3800.0, 3800.0)
        name = "SM_Crate_A" if rng.next() > 0.5 else "SM_Barrel_A"
        placer.place(name, wx, wy, rng.uniform(0.0, 360.0), "Clutter %d" % i,
                     radius=110.0, z_offset=-6.0)

    for i in range(6):
        wx = cx + rng.uniform(-2600.0, 2600.0)
        wy = cy + rng.uniform(-2600.0, 2600.0)
        placer.place("SM_Planter_A", wx, wy, rng.uniform(0.0, 360.0),
                     "Planter %d" % i, radius=130.0, z_offset=-6.0)


def _coast_y_at(world_data, wx):
    """Shoreline Y for a given world X, from the exported coast samples."""
    best = None
    for (x, y) in world_data.coast:
        if best is None or abs(x - wx) < abs(best[0] - wx):
            best = (x, y)
    return best[1] if best else 30000.0


def _place_waterfront(placer, rng):
    """Docks, boats and the harbour buildings east of town."""
    cx, cy = placer.wd.town["center"]
    shore = _coast_y_at(placer.wd, cx)

    # Two piers running out from the shoreline into the water.
    piers = 0
    for pier_index, offset_x in enumerate((-2600.0, 2400.0)):
        base_x = cx + offset_x
        for step in range(7):
            wy = shore - 900.0 + step * 380.0
            if placer.place("SM_Dock_A", base_x, wy, 90.0,
                            "Dock %d-%d" % (pier_index, step),
                            z_offset=140.0, check=False):
                piers += 1
        for step in range(4):
            wy = shore - 900.0 + step * 760.0
            for side in (-1.0, 1.0):
                placer.place("SM_DockPost_A", base_x + side * 150.0, wy, 0.0,
                             "DockPost %d-%d-%d" % (pier_index, step, int(side)),
                             z_offset=170.0, check=False)

        boat = "SM_FishingBoat_A" if pier_index == 0 else "SM_Rowboat_A"
        placer.place(boat, base_x + 420.0, shore + 700.0, rng.uniform(70.0, 110.0),
                     "Boat %d" % pier_index, z_offset=-30.0, check=False)

    # Harbour buildings set back from the water.
    for i, offset_x in enumerate((-5200.0, 4800.0)):
        wx = cx + offset_x
        wy = shore - 2600.0
        placer.place("SM_Warehouse_A", wx, wy, _face_yaw(0.0, 1.0),
                     "Warehouse %d" % i, radius=660.0, z_offset=-25.0, footprint=460.0)

    for i in range(8):
        wx = cx + rng.uniform(-4200.0, 4200.0)
        wy = shore - rng.uniform(1400.0, 2400.0)
        name = "SM_Crate_A" if rng.next() > 0.4 else "SM_Barrel_A"
        placer.place(name, wx, wy, rng.uniform(0.0, 360.0), "Cargo %d" % i,
                     radius=110.0, z_offset=-6.0)

    ctx.log("town: waterfront at shore y=%.0f, %d dock sections" % (shore, piers))
    return shore


def _place_landmarks(placer, rng, shore):
    """The church, the lighthouse and the windmill: the navigation anchors."""
    cx, cy = placer.wd.town["center"]

    placer.place("SM_Church_A", cx - 5200.0, cy + 3400.0, 0.0, "Church",
                 radius=820.0, z_offset=-30.0, footprint=600.0)

    # Lighthouse on the shoreline north of the harbour, where it is visible
    # from the town, the coast road and the sea.
    light_x = cx + 16000.0
    light_y = _coast_y_at(placer.wd, light_x) - 500.0
    placer.place("SM_Lighthouse_A", light_x, light_y, 0.0, "Lighthouse",
                 radius=360.0, z_offset=-20.0, footprint=240.0)
    placer.place("SM_Lighthouse_Glow", light_x, light_y, 0.0, "LighthouseGlow",
                 z_offset=-20.0, check=False)

    # Windmill on a rise in the farmland.
    placer.place("SM_Windmill_A", cx - 6000.0, cy - 27000.0, 0.0, "Windmill",
                 radius=440.0, z_offset=-25.0, footprint=300.0)

    ctx.log("town: landmarks placed (church, lighthouse at x=%.0f, windmill)" % light_x)


def _place_farms(placer, rng):
    """Barns, sheds, haybales, scarecrows and fences out in the fields."""
    farm_roads = [r for r in placer.wd.roads
                  if not r["is_street"] and r["name"] in ("FarmRoad", "NorthLane", "WestLane")]
    barns = 0
    props = 0

    for road in farm_roads:
        for index, (x, y, tx, ty) in enumerate(_walk(road["points"], 9000.0)):
            if index == 0:
                continue
            side = 1.0 if index % 2 == 0 else -1.0
            nx, ny = -ty * side, tx * side
            wx = x + nx * 2600.0
            wy = y + ny * 2600.0
            if placer.wd.weight_at("Farm", wx, wy) < 0.15 and rng.next() > 0.4:
                continue
            if placer.wd.slope_deg(wx, wy) > 11.0:
                continue

            name = "SM_Barn_A" if rng.next() > 0.45 else "SM_Shed_A"
            radius = BUILDING_FOOTPRINT.get(name, 400.0)
            if placer.place(name, wx, wy, _face_yaw(nx, ny), "Farm %d" % barns,
                            radius=radius, z_offset=-25.0, footprint=radius * 0.7):
                barns += 1
                # A little farmyard clutter beside each building.
                for k in range(3):
                    ox = wx + rng.uniform(-1100.0, 1100.0)
                    oy = wy + rng.uniform(-1100.0, 1100.0)
                    clutter = ("SM_Haybale_A", "SM_Cart_A", "SM_Crate_A")[k % 3]
                    if placer.place(clutter, ox, oy, rng.uniform(0.0, 360.0),
                                    "FarmProp %d-%d" % (barns, k),
                                    radius=140.0, z_offset=-8.0):
                        props += 1

    # Scarecrows in the middle of tilled fields.
    scarecrows = 0
    extent = placer.wd.extent - 6000.0
    # Fixed attempt counts silently thin out when the map grows, so scale with
    # area against the 2 km world these numbers were tuned on.
    area_scale = (placer.wd.extent / 100800.0) ** 2
    attempts = int(900 * area_scale)
    for i in range(attempts):
        wx = rng.uniform(-extent, extent)
        wy = rng.uniform(-extent, extent)
        if placer.wd.weight_at("Farm", wx, wy) < 0.35:
            continue
        if placer.wd.slope_deg(wx, wy) > 10.0:
            continue
        if placer.place("SM_Scarecrow_A", wx, wy, rng.uniform(0.0, 360.0),
                        "Scarecrow %d" % scarecrows, radius=300.0, z_offset=-10.0):
            scarecrows += 1

    ctx.log("town: %d farm buildings, %d farmyard props, %d scarecrows"
            % (barns, props, scarecrows))


def _place_fences(placer, rng, world):
    """Fence runs along the field grid, which is what makes farmland read.

    These are instanced rather than spawned as actors: there are hundreds of
    them and they are all the same mesh.
    """
    angle = 0.34                     # matches world_config.FIELD_ANGLE
    size_u, size_v = 9000.0, 7000.0
    cos_a, sin_a = math.cos(angle), math.sin(angle)
    extent = placer.wd.extent - 8000.0
    section = 300.0

    def to_world(u, v):
        return (u * cos_a + v * sin_a, -u * sin_a + v * cos_a)

    transforms = []
    u_steps = int(extent * 2.0 / size_u) + 1
    v_steps = int(extent * 2.0 / size_v) + 1

    for iu in range(-u_steps, u_steps + 1):
        for iv in range(-v_steps, v_steps + 1):
            # Test the parcel CENTRE: the boundaries themselves are hedgerow,
            # where the Farm weight is deliberately zero.
            cx, cy = to_world((iu + 0.5) * size_u, (iv + 0.5) * size_v)
            if abs(cx) > extent or abs(cy) > extent:
                continue
            if placer.wd.weight_at("Farm", cx, cy) < 0.3:
                continue
            if placer.wd.slope_deg(cx, cy) > 12.0:
                continue

            # Two of the four edges, so neighbouring parcels do not double up.
            for (start_u, start_v, du, dv, length, yaw) in (
                    (iu * size_u, iv * size_v, 0.0, 1.0, size_v, math.degrees(angle)),
                    (iu * size_u, iv * size_v, 1.0, 0.0, size_u, math.degrees(angle) + 90.0)):
                steps = int(length / section)
                for step in range(steps):
                    t = (step + 0.5) * section
                    px, py = to_world(start_u + du * t, start_v + dv * t)
                    if abs(px) > extent or abs(py) > extent:
                        continue
                    # Sample just INSIDE the parcel: the boundary itself is
                    # hedgerow, where the Farm weight is zero by construction.
                    inside_x, inside_y = to_world(
                        start_u + du * t + (1.0 - du) * size_u * 0.15,
                        start_v + dv * t + (1.0 - dv) * size_v * 0.15)
                    if placer.wd.weight_at("Farm", inside_x, inside_y) < 0.08:
                        continue
                    if placer.wd.slope_deg(px, py) > 16.0:
                        continue
                    z = placer.wd.height_uu(px, py) - 14.0
                    transforms.append(unreal.Transform(
                        unreal.Vector(px, py, z),
                        unreal.Rotator(0.0, 0.0, yaw),
                        unreal.Vector(1.0, 1.0, 1.0)))

    if not transforms:
        ctx.warn("town: no fence sections placed")
        return

    mesh = placer.meshes.get("SM_Fence_A")
    if mesh is None:
        return
    scatter_class = unreal.load_class(None, "/Script/UEGT2.UEGT2ScatterField")
    field = placer.subsystem.spawn_actor_from_class(
        scatter_class, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    field.set_actor_label(LABEL_PREFIX + "Fences")
    component = field.add_layer(mesh, "Fences", 24000.0, 30000.0, True, True)
    if component is not None:
        component.add_instances(transforms, False)
    ctx.log("town: %d fence sections along field boundaries" % len(transforms))


def _place_bridge(placer, rng):
    """Bridge where the mountain road crosses the river."""
    river = placer.wd.river["points"]
    roads = [r for r in placer.wd.roads if r["name"] == "MountainRoad"]
    if not roads:
        return

    best = None
    for (rx, ry, _rz) in river:
        for point in roads[0]["points"]:
            distance = math.hypot(point[0] - rx, point[1] - ry)
            if best is None or distance < best[0]:
                best = (distance, point[0], point[1], rx, ry)

    if best is None or best[0] > 4000.0:
        ctx.warn("town: no road/river crossing found for a bridge")
        return

    _distance, bx, by, _rx, _ry = best
    placer.place("SM_Bridge_A", bx, by, 0.0, "Bridge", z_offset=180.0, check=False)
    ctx.log("town: bridge at (%.0f, %.0f)" % (bx, by))


# ---------------------------------------------------------------------------
def build(world, world_data, meshes=None):
    from . import meshbuild

    if meshes is None:
        meshes = meshbuild.load_all()
    if not meshes:
        ctx.fail("no meshes available; run the 'meshes' stage first")

    placer = Placer(world_data, meshes)
    removed = placer.clear()
    if removed:
        ctx.log("town: replaced %d existing actors" % removed)

    rng = _SmallRng(world_data.seed + 4242)

    _place_plaza(placer, rng)
    _place_streets(placer, rng)
    shore = _place_waterfront(placer, rng)
    _place_landmarks(placer, rng, shore)
    _place_farms(placer, rng)
    _place_fences(placer, rng, world)
    _place_bridge(placer, rng)

    ctx.log("town: %d actors placed" % placer.count)
    return placer.count
