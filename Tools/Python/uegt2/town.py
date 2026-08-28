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
from . import gen_interior
from . import gen_town
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

def _interior_sizes():
    """Footprints of the fit-out plans, read from the catalog rather than copied.

    The lights have to be laid out by exactly the arithmetic that laid out the
    rooms, and the only way to be sure of that is to ask the same table.
    """
    from . import meshbuild
    return {name: (width, depth, storeys)
            for (name, width, depth, storeys) in meshbuild.INTERIOR_PLANS}


_INTERIOR_SIZE = None


# Which interior fit-out belongs to which house archetype. The footprints live
# in meshbuild.INTERIOR_PLANS; this is only the name mapping.
HOUSE_INTERIOR = {
    "SM_House_A": "HouseA", "SM_House_B": "HouseB", "SM_House_C": "HouseC",
    "SM_House_D": "HouseD", "SM_House_E": "HouseE", "SM_Cottage_A": "Cottage",
}

# An interior is ~1,600 triangles that cannot be seen from outside the building
# it is in, so it is drawn only close up. Collision is not affected by a draw
# distance, which is what lets an NPC keep standing on an upper floor that is
# not being rendered.
INTERIOR_CULL = 9000.0
GLOW_CULL = 14000.0

# Window panes are their own asset on M_Glass, because translucency is the one
# thing the shared opaque material cannot do. They are a few dozen triangles and
# they are part of how a building reads from across a street, so they are drawn
# much further out than an interior - but they cast no shadow, which is the
# whole point: it is what lets daylight through a window and into a room.
GLASS_CULL = 30000.0

# A room with no lamp in it is a black box. There is no static lighting here,
# the window panes are opaque, and Lumen has nothing to carry through a sealed
# shell, so every room gets a real point light hung where its emissive bulb is.
# About three hundred of them over the town, which sounds like a lot until you
# notice the draw distance: a light past forty metres is not submitted at all,
# so two or three are ever actually lit. They cast shadows on purpose - a
# shadowless light inside a house lights the street through the wall.
INTERIOR_LIGHT_CULL = 4200.0
INTERIOR_LIGHT_FADE = 900.0
# These are not domestic numbers and they are not meant to be. Exposure is a
# single global setting shared with the outdoors, and it is floored at EV 7 so
# that night still looks like night - so a room has to be lit to somewhere near
# EV 10 to read at all, and EV 10 at two and a half metres from the lamp is
# about 34,000 lumens once the light hangs below the shade instead of inside it.
# Measured off the screenshots, not guessed: at 3,000 the room was black, and at
# 12,000 you could make out a window and nothing else.
#
# IndirectLightingIntensity is what fills the corners. Doubling the bounce is
# far cheaper, and much better looking, than the raw intensity it would take to
# reach a far corner directly.
INTERIOR_LIGHT_LUMENS = 34000.0
INTERIOR_LIGHT_RADIUS = 900.0
INTERIOR_LIGHT_SOURCE = 30.0
INTERIOR_LIGHT_BOUNCE = 2.5
INTERIOR_LIGHT_COLOUR = (1.0, 0.86, 0.68)

# Depth (local Y) of each house, so the gameplay stage can find its doorway.
# Keep in step with the sizes passed to town.house() in meshbuild._catalog().
HOUSE_DEPTH = {
    "SM_House_A": 580.0, "SM_House_B": 620.0, "SM_House_C": 560.0,
    "SM_House_D": 640.0, "SM_House_E": 700.0, "SM_Cottage_A": 460.0,
}

# Local X of each house, needed to sample the terrain under the whole footprint.
HOUSE_WIDTH = {
    "SM_House_A": 760.0, "SM_House_B": 700.0, "SM_House_C": 900.0,
    "SM_House_D": 640.0, "SM_House_E": 820.0, "SM_Cottage_A": 560.0,
}

# How far the ground floor is set above the highest ground under the house.
# Small on purpose: it is the step up through the front door on level ground,
# and the pawn steps 45 cm without noticing.
FLOOR_CLEAR = 12.0


# Outbuildings whose interior is named after their shell, so place() can attach
# it without a call site knowing anything about it.
_EXTRA_INTERIOR = None


def _extra_interiors():
    from . import meshbuild
    return {"SM_" + name: (width, depth, height, base, wall)
            for (name, width, depth, height, base, wall, _kind)
            in meshbuild.EXTRA_INTERIORS}


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

    def ground_range(self, wx, wy, yaw, half_w, half_d, margin=50.0):
        """Lowest and highest terrain under a rotated rectangular footprint.

        The five point cross ``ground`` uses is right for a barrel and wrong for
        a house: it never samples a corner, which is exactly where a footprint
        on a slope is highest. Setting a house by the lowest of five badly
        chosen samples is how seventy of a hundred and fourteen of them ended up
        with the hillside coming through the ground floor - which nobody could
        see while a house was a solid block, and everybody can see now.
        """
        radians = math.radians(yaw)
        cos_y, sin_y = math.cos(radians), math.sin(radians)
        span_x = half_w + margin
        span_y = half_d + margin
        heights = []
        for i in range(5):
            lx = -span_x + span_x * 0.5 * i
            for j in range(5):
                ly = -span_y + span_y * 0.5 * j
                heights.append(self.wd.height_uu(wx + lx * cos_y - ly * sin_y,
                                                 wy + lx * sin_y + ly * cos_y))
        return min(heights), max(heights)

    def ground(self, wx, wy, footprint=0.0):
        """Lowest terrain height across the footprint, so nothing floats."""
        if footprint <= 0.0:
            return self.wd.height_uu(wx, wy)
        samples = [self.wd.height_uu(wx + dx, wy + dy)
                   for dx, dy in ((0, 0), (footprint, 0), (-footprint, 0),
                                  (0, footprint), (0, -footprint))]
        return min(samples)

    def place_at(self, mesh_name, location, yaw, label, cull=0.0, shadow=True,
                 collision=True):
        """Spawn a mesh on an exact transform, off the occupancy list.

        Interiors go on the same transform as the shell they belong to, so they
        must not re-derive their height from the terrain - a fit-out half a
        metre out of register with its own walls is worse than none.

        ``cull`` is a draw distance in centimetres. An interior is 1,600
        triangles that nobody can see from outside, so it is switched off at
        range; collision is unaffected by it, which is what lets an NPC keep
        standing on an upper floor that is not being drawn.
        """
        mesh = self.meshes.get(mesh_name)
        if mesh is None:
            ctx.warn("%s: mesh %s missing" % (self.prefix.strip(), mesh_name))
            return None

        actor = self.subsystem.spawn_actor_from_class(
            unreal.StaticMeshActor, location, unreal.Rotator(0.0, 0.0, yaw))
        if actor is None:
            return None

        component = actor.get_editor_property("static_mesh_component")
        component.set_editor_property("static_mesh", mesh)
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        if cull > 0.0:
            ctx.set_prop(component, "ld_max_draw_distance", cull)
        if not shadow:
            ctx.set_prop(component, "cast_shadow", False)
        if not collision:
            # A method rather than a property, so it cannot go through
            # ctx.set_prop; guarded the same way, because an engine rename here
            # should cost a warning and not the whole content build.
            try:
                component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            except Exception as exc:                            # noqa: BLE001
                ctx.warn("could not disable collision on %s: %s" % (label, exc))
        actor.set_actor_label("%s%s" % (self.prefix, label))
        self.count += 1
        return actor

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
        self._glaze(actor, mesh_name, label)
        self._fit_out(actor, mesh_name, label)
        return actor

    def _fit_out(self, actor, mesh_name, label):
        """Attach an outbuilding's interior, if it has one of its own.

        Houses and city blocks pick an interior by variant or by trade and are
        handled at their call sites. A barn only ever has one inside, so it can
        be found by name and hung here, which means nothing has to remember to
        do it.
        """
        global _EXTRA_INTERIOR
        if _EXTRA_INTERIOR is None:
            _EXTRA_INTERIOR = _extra_interiors()
        entry = _EXTRA_INTERIOR.get(mesh_name)
        if entry is None:
            return
        width, depth, height, base, wall = entry
        location = actor.get_actor_location()
        yaw = actor.get_actor_rotation().yaw
        # The companion's label goes in FRONT of the building's, never after it.
        # npc.py classifies actors by label prefix, so "Town House 3 Glass" is
        # another house as far as the survey is concerned - which quietly
        # doubled the population of the town and put two families in every
        # building.
        self.place_at("SM_Int_" + mesh_name[3:], location, yaw,
                      "Fitout " + label, cull=INTERIOR_CULL)
        self.place_at("SM_Glow_" + mesh_name[3:], location, yaw,
                      "FitoutGlow " + label, cull=GLOW_CULL,
                      shadow=False, collision=False)
        place_room_lights(self, location, yaw, width, depth, 1,
                          8300 + len(mesh_name[3:]) * 29, "FitoutLight " + label,
                          base_z=base, wall_t=wall, storey_h=height)

    def _glaze(self, actor, mesh_name, label):
        """Hang a building's window panes on the transform it just landed on.

        Done here rather than at each call site so that every glazed building in
        the world gets its glass without anybody having to remember - the town's
        houses, the barn, the church, and every block in Newhaven go through
        this one function.
        """
        glass_name = "SM_Glass_" + mesh_name[3:]
        if glass_name not in self.meshes:
            return
        self.place_at(glass_name, actor.get_actor_location(),
                      actor.get_actor_rotation().yaw, "Glass " + label,
                      cull=GLASS_CULL, shadow=False)


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
    global _INTERIOR_SIZE
    if _INTERIOR_SIZE is None:
        _INTERIOR_SIZE = _interior_sizes()
    houses = 0
    lamps = 0
    interiors = 0

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

                # Stand the house on the *highest* ground under its footprint
                # rather than the lowest, so the floor is a floor everywhere
                # inside. The deep foundation in gen_town.house() takes up the
                # slack on the downhill side, and the flight of steps at the
                # door reaches down to meet the ground.
                hx = wx + jitter_x
                hy = wy + jitter_y
                _low, high = placer.ground_range(
                    hx, hy, yaw,
                    HOUSE_WIDTH.get(name, radius) * 0.5,
                    HOUSE_DEPTH.get(name, radius) * 0.5)
                lift = (high + FLOOR_CLEAR - gen_town.PLINTH_H
                        - placer.wd.height_uu(hx, hy))
                built = placer.place(name, hx, hy, yaw,
                                     "House %d" % houses, radius=radius,
                                     z_offset=lift, footprint=0.0)
                if built:
                    interiors += _place_interior(placer, built, name, houses)
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

    ctx.log("town: %d houses (%d interior meshes), %d lamp posts along %d streets"
            % (houses, interiors, lamps, len(streets)))
    return houses


def _place_interior(placer, house_actor, mesh_name, index):
    """Drop a house's fit-out and its fires onto the house's own transform.

    Read off the placed actor rather than recomputed: place() derives its height
    from the lowest corner of the footprint and a z offset, and a fit-out that
    re-derived that itself would end up a few centimetres out of register with
    its own walls.
    """
    plan = HOUSE_INTERIOR.get(mesh_name)
    if plan is None:
        return 0

    location = house_actor.get_actor_location()
    yaw = house_actor.get_actor_rotation().yaw
    variant = index % 2
    placed = 0

    if placer.place_at("SM_Int_%s_%d" % (plan, variant), location, yaw,
                       "Interior %d" % index, cull=INTERIOR_CULL):
        placed += 1
    # The fires and lamp bulbs: emissive, so no shadow and no collision - they
    # are light, not furniture.
    if placer.place_at("SM_Glow_%s_%d" % (plan, variant), location, yaw,
                       "InteriorGlow %d" % index, cull=GLOW_CULL,
                       shadow=False, collision=False):
        placed += 1
    placed += _place_interior_lights(placer, location, yaw, plan, variant, index)
    return placed


def place_room_lights(placer, location, yaw, width, depth, storeys, seed, label,
                      base_z=gen_town.PLINTH_H, wall_t=None, storey_h=None):
    """A point light in every room of a fit-out, at the lamp hanging there.

    Shared by the town and the city because the reason is the same in both: no
    static lighting, opaque interiors, and Lumen with nothing to carry in. The
    room layout is recomputed from the seed rather than passed in, which is safe
    because plan() is a pure function of it - the same call the fit-out itself
    made.
    """
    kwargs = {"base_z": base_z}
    if wall_t is not None:
        kwargs["wall_t"] = wall_t
    if storey_h is not None:
        kwargs["storey_h"] = storey_h

    radians = math.radians(yaw)
    cos_y, sin_y = math.cos(radians), math.sin(radians)
    placed = 0
    for (lx, ly, lz, _storey) in gen_interior.lamp_points(
            width, depth, storeys, seed, **kwargs):
        actor = placer.subsystem.spawn_actor_from_class(
            unreal.PointLight,
            unreal.Vector(location.x + lx * cos_y - ly * sin_y,
                          location.y + lx * sin_y + ly * cos_y,
                          location.z + lz),
            unreal.Rotator(0.0, 0.0, 0.0))
        if actor is None:
            continue
        component = actor.get_component_by_class(unreal.PointLightComponent)
        ctx.set_prop(component, "mobility", unreal.ComponentMobility.MOVABLE)
        ctx.set_prop(component, "intensity_units", unreal.LightUnits.LUMENS)
        ctx.set_prop(component, "intensity", INTERIOR_LIGHT_LUMENS)
        ctx.set_prop(component, "attenuation_radius", INTERIOR_LIGHT_RADIUS)
        ctx.set_prop(component, "source_radius", INTERIOR_LIGHT_SOURCE)
        ctx.set_prop(component, "indirect_lighting_intensity", INTERIOR_LIGHT_BOUNCE)
        ctx.set_prop(component, "light_color", unreal.Color(
            int(INTERIOR_LIGHT_COLOUR[0] * 255), int(INTERIOR_LIGHT_COLOUR[1] * 255),
            int(INTERIOR_LIGHT_COLOUR[2] * 255), 255))
        ctx.set_prop(component, "max_draw_distance", INTERIOR_LIGHT_CULL)
        ctx.set_prop(component, "max_distance_fade_range", INTERIOR_LIGHT_FADE)
        ctx.set_prop(component, "volumetric_scattering_intensity", 0.0)
        actor.set_actor_label("%s%s.%d" % (placer.prefix, label, placed))
        placer.count += 1
        placed += 1
    return placed


def _place_interior_lights(placer, location, yaw, plan, variant, index):
    """A point light in every room, at the lamp the fit-out already hung there."""
    size = _INTERIOR_SIZE.get(plan)
    if size is None:
        return 0
    width, depth, storeys = size
    return place_room_lights(placer, location, yaw, width, depth, storeys,
                             9001 + variant * 131, "InteriorLight %d" % index)


def _place_plaza(placer, rng):
    """The town square: the well, the market, benches and clutter.

    The stall and bench counts matter beyond decoration: the npc stage uses
    them as the anchor points the town's crowd spreads across.
    """
    cx, cy = placer.wd.town["center"]
    placer.place("SM_Well_A", cx, cy, 0.0, "Well", radius=260.0, z_offset=-15.0)

    # A market of two rings rather than one.
    #
    # Five stalls was enough to read as a market and nowhere near enough to
    # hold one: every villager in town picks a stall to stand at, and with five
    # of them in a twelve metre circle the square became one solid mass of
    # people. Stalls are the anchor points the crowd spreads across, so the
    # market needs to be the size of the crowd it serves.
    stalls = 0
    for ring_radius, count, offset in ((1250.0, 7, 12.0), (2150.0, 6, 40.0)):
        for i in range(count):
            angle = 360.0 / count * i + offset
            wx = cx + math.cos(math.radians(angle)) * ring_radius
            wy = cy + math.sin(math.radians(angle)) * ring_radius
            if placer.place("SM_MarketStall_A", wx, wy, angle + 180.0,
                            "Stall %d" % stalls, radius=300.0, z_offset=-10.0):
                stalls += 1

    benches = 0
    for ring_radius, count, offset in ((760.0, 6, 30.0), (1800.0, 9, 20.0)):
        for i in range(count):
            angle = 360.0 / count * i + offset
            wx = cx + math.cos(math.radians(angle)) * ring_radius
            wy = cy + math.sin(math.radians(angle)) * ring_radius
            if placer.place("SM_Bench_A", wx, wy, angle + 90.0,
                            "Bench %d" % benches, radius=170.0, z_offset=-8.0):
                benches += 1

    # No villagers here any more. Characters belong to the npc stage, which
    # spawns inhabitants with routines instead of props; two stages both placing
    # people would leave a permanent crowd of statues standing among the ones
    # that move. Dropping the loop shortens this stage's draw on rng, so the
    # crates and planters below land in different spots than they did in 0.1 -
    # they are decoration, and the map is a build artifact either way.

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

    ctx.log("town: plaza with %d market stalls and %d benches" % (stalls, benches))


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
