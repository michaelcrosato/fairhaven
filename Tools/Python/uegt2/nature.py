"""Procedural vegetation and rock placement.

Everything is instanced: a handful of AUEGT2ScatterField actors hold one
hierarchical instanced mesh component per species, so a quarter of a million
plants cost a few dozen draw calls.

Placement is a jittered grid filtered by the same data the terrain generator
produced - paint weights, height and slope - so vegetation always agrees with
the ground it stands on. Change the rules in SPECIES below; nothing else needs
to know.
"""
from __future__ import annotations

import math
import time

import unreal

from . import ctx
from .meshkit import _SmallRng


# ---------------------------------------------------------------------------
# Placement rules
# ---------------------------------------------------------------------------
class Species(object):
    """One scatter rule: which meshes, where they go, and how they look."""

    def __init__(self, name, meshes, spacing, accept, scale=(0.85, 1.25),
                 cull=(40000.0, 52000.0), collision=False, shadow=True,
                 sink=6.0, align_to_slope=0.0, max_slope=32.0):
        self.name = name
        self.meshes = meshes
        self.spacing = spacing
        self.accept = accept
        self.scale = scale
        self.cull = cull
        self.collision = collision
        self.shadow = shadow
        self.sink = sink                 # push into the ground, cm
        self.align_to_slope = align_to_slope
        self.max_slope = max_slope


def _rules(wd):
    """Build the species table. ``wd`` is the WorldData for terrain queries."""
    town_x, town_y = wd.town["center"]
    town_r = wd.town["radius_uu"]

    def town_distance(wx, wy):
        return math.hypot(wx - town_x, wy - town_y)

    def outside_town(wx, wy, margin=2500.0):
        return town_distance(wx, wy) > town_r + margin

    # --- acceptance functions ---------------------------------------------
    def temperate_forest(wx, wy, h, slope, w):
        if h < 220.0 or slope > 26.0:
            return 0.0
        if not outside_town(wx, wy):
            return 0.0
        if w("Dirt") > 0.35 or w("Farm") > 0.3 or w("Sand") > 0.3:
            return 0.0
        if w("Jungle") > 0.35:
            return 0.0
        # Denser away from the coast, thinning out high up.
        band = 1.0 if h < 12000.0 else max(0.0, 1.0 - (h - 12000.0) / 9000.0)
        return 0.62 * band * min(w("Grass") * 1.6, 1.0)

    def conifer(wx, wy, h, slope, w):
        if h < 5500.0 or slope > 34.0:
            return 0.0
        if w("Snow") > 0.5 or w("Dirt") > 0.4:
            return 0.0
        band = min(max((h - 5500.0) / 7000.0, 0.0), 1.0)
        thin_out = max(0.0, 1.0 - max(0.0, h - 21000.0) / 8000.0)
        return 0.7 * band * thin_out

    def jungle(wx, wy, h, slope, w):
        if h < 180.0 or slope > 30.0 or w("Jungle") < 0.28:
            return 0.0
        return 0.8 * w("Jungle")

    def palms(wx, wy, h, slope, w):
        # Warm coastal fringe: the southern half, near sand or jungle edge.
        if h < 40.0 or h > 2600.0 or slope > 20.0:
            return 0.0
        # Southern half only: palms next to the temperate town looked wrong.
        if wx > -12000.0:
            return 0.0
        warmth = 1.0 if wx < -40000.0 else max(0.0, (-12000.0 - wx) / 28000.0)
        coastal = max(w("Sand"), w("Jungle") * 0.7)
        return 0.6 * warmth * min(coastal * 1.7, 1.0)

    def undergrowth(wx, wy, h, slope, w):
        if h < 160.0 or slope > 30.0 or not outside_town(wx, wy, 1200.0):
            return 0.0
        if w("Dirt") > 0.4 or w("Farm") > 0.4:
            return 0.0
        return 0.45 * max(w("Grass"), w("Jungle"))

    def jungle_floor(wx, wy, h, slope, w):
        if h < 150.0 or slope > 34.0 or w("Jungle") < 0.25:
            return 0.0
        return 0.75 * w("Jungle")

    def grass(wx, wy, h, slope, w):
        if h < 120.0 or slope > 30.0:
            return 0.0
        if w("Dirt") > 0.45 or w("Sand") > 0.55:
            return 0.0
        return 0.85 * max(w("Grass"), w("Farm") * 0.35)

    def crops(wx, wy, h, slope, w):
        if w("Farm") < 0.45 or slope > 12.0:
            return 0.0
        return 0.95 * w("Farm")

    def reeds(wx, wy, h, slope, w):
        # A narrow band at the waterline. Kept out of open water: reeds
        # standing in the middle of the sea read as a bug.
        if h < -55.0 or h > 260.0 or slope > 12.0:
            return 0.0
        return 0.5

    def rocks_mountain(wx, wy, h, slope, w):
        if w("Rock") < 0.3 and slope < 22.0:
            return 0.0
        if h < 400.0:
            return 0.0
        return 0.5 * max(w("Rock"), min(slope / 40.0, 1.0))

    def rocks_scattered(wx, wy, h, slope, w):
        if h < 200.0 or slope > 34.0:
            return 0.0
        return 0.13

    def beach_rocks(wx, wy, h, slope, w):
        if h < -300.0 or h > 700.0 or slope > 26.0:
            return 0.0
        return 0.32 * min(w("Sand") * 1.5, 1.0)

    def cliffs(wx, wy, h, slope, w):
        if slope < 26.0 or h < 2500.0:
            return 0.0
        return 0.35

    return [
        Species("Grass", ["SM_Grass_A", "SM_Grass_B"], 300.0, grass,
                scale=(0.75, 1.5), cull=(7000.0, 9000.0), shadow=False, sink=8.0),
        Species("Crops", ["SM_Crop_Wheat_A", "SM_Crop_Green_A"], 300.0, crops,
                scale=(0.8, 1.3), cull=(9000.0, 12000.0), shadow=False, sink=6.0),
        Species("Reeds", ["SM_Reeds_A"], 480.0, reeds,
                scale=(0.8, 1.4), cull=(10000.0, 13000.0), shadow=False, sink=20.0),
        Species("Undergrowth", ["SM_Bush_A", "SM_Bush_B", "SM_Fern_A"], 780.0, undergrowth,
                scale=(0.75, 1.4), cull=(16000.0, 21000.0), shadow=True, sink=12.0),
        Species("JungleFloor", ["SM_Fern_A", "SM_Fern_B", "SM_Bush_Jungle_A"], 600.0, jungle_floor,
                scale=(0.8, 1.5), cull=(14000.0, 18000.0), shadow=True, sink=10.0),
        Species("Forest", ["SM_Tree_Oak_A", "SM_Tree_Oak_B", "SM_Tree_Oak_C",
                           "SM_Tree_Birch_A", "SM_Tree_Birch_B", "SM_Tree_Autumn_A"],
                950.0, temperate_forest, scale=(0.8, 1.35),
                cull=(70000.0, 90000.0), collision=True, sink=25.0),
        Species("Conifers", ["SM_Tree_Pine_A", "SM_Tree_Pine_B", "SM_Tree_Pine_C"],
                1050.0, conifer, scale=(0.75, 1.3),
                cull=(70000.0, 90000.0), collision=True, sink=30.0),
        Species("Jungle", ["SM_Tree_Jungle_A", "SM_Tree_Jungle_B", "SM_Tree_Palm_A"],
                1000.0, jungle, scale=(0.8, 1.3),
                cull=(60000.0, 80000.0), collision=True, sink=25.0),
        Species("Palms", ["SM_Tree_Palm_A", "SM_Tree_Palm_B"], 1150.0, palms,
                scale=(0.8, 1.35), cull=(60000.0, 80000.0), collision=True, sink=20.0),
        Species("Rocks", ["SM_Rock_S", "SM_Rock_M", "SM_Rock_L"], 1600.0, rocks_scattered,
                scale=(0.7, 1.6), cull=(30000.0, 40000.0), collision=True,
                sink=25.0, align_to_slope=0.6, max_slope=40.0),
        Species("MountainRocks", ["SM_Rock_M", "SM_Rock_L", "SM_Boulder_A"], 1300.0, rocks_mountain,
                scale=(0.8, 1.9), cull=(45000.0, 60000.0), collision=True,
                sink=40.0, align_to_slope=0.5, max_slope=48.0),
        Species("BeachRocks", ["SM_Rock_S", "SM_Rock_M", "SM_Driftwood_A"], 1250.0, beach_rocks,
                scale=(0.6, 1.2), cull=(24000.0, 32000.0), collision=True, sink=18.0),
        Species("Cliffs", ["SM_Cliff_A", "SM_Cliff_B"], 2000.0, cliffs,
                scale=(0.8, 1.8), cull=(60000.0, 80000.0), collision=True,
                sink=90.0, align_to_slope=0.35, max_slope=55.0),
    ]


# ---------------------------------------------------------------------------
# Placement
# ---------------------------------------------------------------------------
def _road_avoidance(wd):
    """Grid of road segments so vegetation can keep off the carriageway."""
    cell = 6000.0
    buckets = {}
    for road in wd.roads:
        clearance = road["width_uu"] * (1.5 if road["is_street"] else 1.25)
        points = road["points"]
        for a, b in zip(points, points[1:]):
            key_a = (int(a[0] // cell), int(a[1] // cell))
            key_b = (int(b[0] // cell), int(b[1] // cell))
            for kx in range(min(key_a[0], key_b[0]) - 1, max(key_a[0], key_b[0]) + 2):
                for ky in range(min(key_a[1], key_b[1]) - 1, max(key_a[1], key_b[1]) + 2):
                    buckets.setdefault((kx, ky), []).append((a[0], a[1], b[0], b[1], clearance))

    def near_road(wx, wy):
        key = (int(wx // cell), int(wy // cell))
        for (ax, ay, bx, by, clearance) in buckets.get(key, ()):
            dx, dy = bx - ax, by - ay
            length_sq = dx * dx + dy * dy
            if length_sq < 1e-3:
                continue
            t = max(0.0, min(1.0, ((wx - ax) * dx + (wy - ay) * dy) / length_sq))
            px, py = ax + t * dx, ay + t * dy
            if (wx - px) ** 2 + (wy - py) ** 2 < clearance * clearance:
                return True
        return False

    return near_road


def _place_species(wd, species, near_road, seed):
    """Return a list of unreal.Transform for one species."""
    rng = _SmallRng(seed)
    extent = wd.extent - 2000.0

    # Newhaven is paved, so the layer weights already thin scatter there, but
    # not to zero at the edges. An explicit hole stops a jungle tree growing up
    # through the middle of a tower.
    city = getattr(wd, "city", None)
    city_x, city_y, city_r = 0.0, 0.0, -1.0
    if city:
        city_x, city_y = city["center"]
        city_r = float(city["radius_uu"]) * 0.94
    spacing = species.spacing
    steps = int((extent * 2.0) / spacing)

    per_mesh = dict((name, []) for name in species.meshes)
    mesh_count = len(species.meshes)

    for iy in range(steps):
        wy = -extent + (iy + 0.5) * spacing
        for ix in range(steps):
            wx = -extent + (ix + 0.5) * spacing

            jx = wx + rng.uniform(-spacing * 0.45, spacing * 0.45)
            jy = wy + rng.uniform(-spacing * 0.45, spacing * 0.45)

            # Cheapest rejections first.
            height = wd.height_uu(jx, jy)
            if height < -400.0:
                continue

            weight_cache = {}

            def weight(layer):
                if layer not in weight_cache:
                    weight_cache[layer] = wd.weight_at(layer, jx, jy)
                return weight_cache[layer]

            slope = wd.slope_deg(jx, jy)
            if slope > species.max_slope:
                continue

            probability = species.accept(jx, jy, height, slope, weight)
            if probability <= 0.0 or rng.next() > probability:
                continue
            if near_road(jx, jy):
                continue
            if city_r > 0.0 and (jx - city_x) ** 2 + (jy - city_y) ** 2 < city_r * city_r:
                continue

            name = species.meshes[int(rng.next() * mesh_count) % mesh_count]
            scale = rng.uniform(species.scale[0], species.scale[1])
            yaw = rng.uniform(0.0, 360.0)

            pitch = roll = 0.0
            if species.align_to_slope > 0.0:
                pitch = rng.uniform(-slope, slope) * species.align_to_slope
                roll = rng.uniform(-slope, slope) * species.align_to_slope

            per_mesh[name].append(unreal.Transform(
                unreal.Vector(jx, jy, height - species.sink * scale),
                unreal.Rotator(roll, pitch, yaw),
                unreal.Vector(scale, scale, scale)))

    return per_mesh


def build(world, world_data, meshes=None):
    """Scatter every species into instanced mesh components."""
    from . import meshbuild

    if meshes is None:
        meshes = meshbuild.load_all()
    if not meshes:
        ctx.fail("no meshes available; run the 'meshes' stage first")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Remove previous scatter so the stage is re-runnable.
    scatter_class = unreal.load_class(None, "/Script/UEGT2.UEGT2ScatterField")
    if scatter_class is None:
        ctx.fail("UEGT2ScatterField class not found; build the editor target first")
    removed = 0
    for actor in actor_subsystem.get_all_level_actors():
        if actor.get_class() == scatter_class or actor.get_actor_label().startswith("Scatter "):
            actor_subsystem.destroy_actor(actor)
            removed += 1
    if removed:
        ctx.log("nature: replaced %d scatter fields" % removed)

    near_road = _road_avoidance(world_data)
    total = 0
    started = time.time()

    for index, species in enumerate(_rules(world_data)):
        begin = time.time()
        per_mesh = _place_species(world_data, species, near_road,
                                  world_data.seed + index * 7919)
        placed = sum(len(v) for v in per_mesh.values())
        if placed == 0:
            ctx.warn("nature: species '%s' placed nothing" % species.name)
            continue

        field = actor_subsystem.spawn_actor_from_class(
            scatter_class, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        field.set_actor_label("Scatter %s" % species.name)

        for mesh_name, transforms in per_mesh.items():
            if not transforms:
                continue
            mesh_asset = meshes.get(mesh_name)
            if mesh_asset is None:
                ctx.warn("nature: mesh %s missing" % mesh_name)
                continue
            component = field.add_layer(mesh_asset, mesh_name,
                                        species.cull[0], species.cull[1],
                                        species.shadow, species.collision)
            if component is None:
                continue
            component.add_instances(transforms, False)

        total += placed
        ctx.log("nature: %-14s %7d instances  (%.1fs)"
                % (species.name, placed, time.time() - begin))

    ctx.log("nature: %d instances placed in %.1fs" % (total, time.time() - started))
    return total
