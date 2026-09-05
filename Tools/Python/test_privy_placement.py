"""Square convenience placement regressions; run with system Python, no Unreal.

    python Tools/Python/test_privy_placement.py
"""
import math
import os
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import check_meshes
check_meshes._install_unreal_stub()
from uegt2 import ctx, gameplay, meshbuild, town
from uegt2.meshkit import _SmallRng


class Actor:
    def __init__(self, location, rotation, occupied):
        self.location = SimpleNamespace(x=location[0], y=location[1], z=location[2])
        self.rotation = SimpleNamespace(yaw=rotation[2])
        self.label = ""
        self.tags = ["Retained.Placement.Tag"]
        self.occupied_before = list(occupied)
        self.component = SimpleNamespace(set_editor_property=lambda *args: None)

    def get_editor_property(self, name):
        return self.tags if name == "tags" else self.component

    def set_editor_property(self, name, value):
        if name == "tags":
            self.tags = value

    def set_actor_label(self, label):
        self.label = label

    def get_actor_label(self):
        return self.label

    def get_actor_location(self):
        return self.location

    def get_actor_rotation(self):
        return self.rotation


class PlacementScene(town.Placer):
    """Replace the spawn boundary, retaining production placement/reservations."""
    def __init__(self, world):
        self.wd, self.occupied, self.count = world, [], 0
        self.prefix = town.LABEL_PREFIX
        self.meshes = {name: object() for name, *_ in meshbuild._catalog()}
        self.actors = []
        self.reject_spawn = False
        self.subsystem = SimpleNamespace(spawn_actor_from_class=self.spawn,
                                         get_all_level_actors=lambda: list(self.actors))

    def spawn(self, cls, location, rotation):
        if self.reject_spawn:
            return None
        actor = Actor(location, rotation, self.occupied)
        self.actors.append(actor)
        return actor

    # Attached fit-outs and lights do not reserve space or consume the stage's
    # shared RNG. Their engine work is outside this placement regression.
    def place_at(self, *args, **kwargs):
        return None

    def _glaze(self, *args):
        pass

    def _fit_out(self, *args):
        pass


def flat_world(height=lambda x, y: 1600.0):
    return SimpleNamespace(town={"center": (0.0, 0.0)}, seed=20260826,
                           height_uu=height, coast=[(0.0, 30000.0)])


def actor_snapshot(actors):
    return [(a.label, a.location.x, a.location.y, a.location.z, a.rotation.yaw) for a in actors]


def prior_town():
    scene = PlacementScene(ctx.WorldData())
    rng = _SmallRng(scene.wd.seed+4242)
    with patch.object(town, "place_room_lights", return_value=0), \
            patch.object(town, "_place_interior", return_value=0):
        town._place_plaza(scene, rng)
        town._place_services(scene, rng)
        town._place_streets(scene, rng)
        shore = town._place_waterfront(scene, rng)
        town._place_landmarks(scene, rng, shore)
        town._place_farms(scene, rng)
    return scene, rng


def pickup_run(world, actors):
    records, generators, logs = {}, [], []
    def generator(seed):
        rng = _SmallRng(seed)
        generators.append(rng)
        return rng
    def spawn(cls, mesh, x, y, z, yaw, label, mobility):
        records[label] = (mesh, x, y, z, yaw)
        return object()
    subsystem = SimpleNamespace(get_all_level_actors=lambda: list(actors))
    with patch.object(gameplay, "_subsystem", return_value=subsystem), \
            patch.object(gameplay, "_load_class", return_value=object()), \
            patch.object(gameplay, "_spawn_interactable", side_effect=spawn), \
            patch.object(gameplay, "_SmallRng", side_effect=generator), \
            patch.object(ctx, "log", side_effect=logs.append):
        count = gameplay._place_pickups(world, {name: name for name in ("SM_Crate_A", "SM_Barrel_A")})
    return records, generators[0].state, logs, count


class SquarePrivyTests(unittest.TestCase):
    def setUp(self):
        for name, value in (("StaticMeshActor", object()),
                            ("ComponentMobility", SimpleNamespace(STATIC=0, MOVABLE=1)),
                            ("Name", lambda value: value)):
            patcher = patch.object(town.unreal, name, value, create=True)
            patcher.start()
            self.addCleanup(patcher.stop)

    def assert_clear_sites(self, scene):
        privies = [a for a in scene.actors if a.label in {"Town Privy %d" % i for i in range(4)}]
        self.assertEqual(len(privies), 4)
        for actor in privies:
            index = int(actor.label.rsplit(" ", 1)[-1])
            self.assertEqual(actor.tags, ["Retained.Placement.Tag", "UEGT2.SquarePrivy.%d" % index])
            x, y, z = actor.location.x, actor.location.y, actor.location.z
            yaw = math.radians(actor.rotation.yaw)
            cx, cy = scene.wd.town["center"]
            towards = math.hypot(cx-x, cy-y)
            facing = (math.sin(yaw)*(cx-x)-math.cos(yaw)*(cy-y))/towards
            self.assertGreaterEqual(facing, math.cos(math.pi/4)-1e-6)
            reservations = [(x, y, 220.0)] + [
                (x+math.sin(yaw)*distance, y-math.cos(yaw)*distance, 120.0)
                for distance in (225.0, 350.0)]
            for px, py, radius in reservations:
                for ox, oy, other in actor.occupied_before:
                    self.assertGreaterEqual(math.hypot(px-ox, py-oy), radius+other-1e-6)
                self.assertIn((px, py, radius), scene.occupied)
            self.assertGreater(z+24.0, scene.wd.height_uu(x, y))
        return privies

    def test_rejected_original_sites_relocate_all_four_and_reserve_their_entrances(self):
        scene = PlacementScene(flat_world())
        rng = _SmallRng(scene.wd.seed+4242)
        town._place_plaza(scene, rng)
        preferred = ((1450, -1250), (-1500, 1350), (1600, 1500), (-1350, -1500))
        self.assertTrue(all(not scene.is_free(x, y, 220) for x, y in preferred))
        prior = actor_snapshot(scene.actors)
        self.assertEqual(town._place_square_conveniences(scene, rng), 4)
        self.assertEqual(actor_snapshot(scene.actors[:len(prior)]), prior)
        for actor, origin in zip(self.assert_clear_sites(scene), preferred):
            self.assertLessEqual(math.hypot(actor.location.x-origin[0], actor.location.y-origin[1]), 1300.001)
            self.assertAlmostEqual(actor.location.z+24, 1608)

    def test_repeatability_and_legacy_rng_consumption(self):
        first, second = PlacementScene(flat_world()), PlacementScene(flat_world())
        a, b, legacy = _SmallRng(193), _SmallRng(193), _SmallRng(193)
        town._place_square_conveniences(first, a)
        town._place_square_conveniences(second, b)
        self.assertEqual(actor_snapshot(first.actors), actor_snapshot(second.actors))
        self.assertEqual(first.occupied, second.occupied)
        for _ in range(4):
            legacy.uniform(0.0, 360.0)
        self.assertEqual([a.next() for _ in range(8)], [legacy.next() for _ in range(8)])

    def test_translated_town_retains_relative_positions_and_facing(self):
        first = PlacementScene(flat_world())
        moved_world = flat_world()
        moved_world.town["center"] = (45000.0, -23000.0)
        second = PlacementScene(moved_world)
        town._place_square_conveniences(first, _SmallRng(37))
        town._place_square_conveniences(second, _SmallRng(37))
        self.assert_clear_sites(second)
        for a, b in zip(first.actors, second.actors):
            self.assertAlmostEqual(b.location.x-a.location.x, 45000)
            self.assertAlmostEqual(b.location.y-a.location.y, -23000)
            self.assertAlmostEqual(b.rotation.yaw, a.rotation.yaw)

    def test_clear_body_does_not_allow_an_occupied_amenity_or_approach(self):
        for distance in (225.0, 350.0):
            with self.subTest(distance=distance):
                scene = PlacementScene(flat_world())
                scene.reserve(0.0, -distance, 1.0)
                self.assertTrue(scene.is_free(0.0, 0.0, 220.0))
                self.assertIsNone(town._square_privy_site(scene, 0.0, 0.0, 0.0))

    def test_level_floor_fit_and_steep_or_nonfinite_approaches(self):
        gentle = PlacementScene(flat_world(lambda x, y: 1600+0.02*x))
        origin, fronts = town._square_privy_site(gentle, 0.0, 0.0, 0.0)
        self.assertAlmostEqual(origin+24, 1600+135*0.02+8)
        self.assertEqual(fronts, [(0.0, -225.0), (0.0, -350.0)])
        for bad in (30.0, math.nan, math.inf):
            with self.subTest(bad=bad):
                scene = PlacementScene(flat_world(lambda x, y: 1600 if y >= -200 else 1600+bad))
                self.assertIsNone(town._square_privy_site(scene, 0.0, 0.0, 0.0))

    def test_exhausted_search_fails_without_forced_overlap(self):
        scene = PlacementScene(flat_world())
        scene.reserve(0.0, 0.0, 100000.0)
        with patch.object(scene, "is_free", wraps=scene.is_free) as checks:
            with self.assertRaisesRegex(RuntimeError, "required square privy 0"):
                town._place_square_conveniences(scene, _SmallRng(19))
            self.assertLessEqual(checks.call_count, 100)
        self.assertEqual(scene.actors, [])
        self.assertEqual(scene.occupied, [(0.0, 0.0, 100000.0)])

    def test_spawn_failure_is_not_treated_as_an_optional_missing_washroom(self):
        scene = PlacementScene(flat_world())
        scene.reject_spawn = True
        with self.assertRaisesRegex(RuntimeError, "could not spawn required square privy 0"):
            town._place_square_conveniences(scene, _SmallRng(19))
        self.assertEqual(scene.occupied, [])

    def test_pickups_respect_tagged_fronts_without_reordering_clear_spots_or_rng(self):
        world = flat_world()
        baseline, state, _, count = pickup_run(world, [])
        self.assertEqual(count, 18)
        _, x, y, _, _ = baseline["Pickup 0"]
        privy = Actor((x, y+350, 1600), (0, 0, 0), [])
        privy.tags = ["Other.Tag", "UEGT2.SquarePrivy.0"]
        # A similarly named but unrecognised tag must not create a keepout.
        _, other_x, other_y, _, _ = baseline["Pickup 17"]
        unrelated = Actor((other_x, other_y, 1600), (0, 0, 0), [])
        unrelated.tags = ["UEGT2.SquarePrivy.99"]
        current, new_state, logs, kept = pickup_run(world, [privy, unrelated])
        self.assertNotIn("Pickup 0", current)
        self.assertIn("Pickup 17", current)
        self.assertEqual(kept, len(current))
        self.assertLess(kept, count)
        self.assertTrue(any("skipped pickup 0" in line for line in logs))
        self.assertEqual(new_state, state)
        for label, record in current.items():
            self.assertEqual(record, baseline[label])

    def test_pickup_footprint_encloses_the_actual_simple_collision_box_at_any_yaw(self):
        for name, _, factory, _, collision in meshbuild._catalog():
            if name not in ("SM_Crate_A", "SM_Barrel_A"):
                continue
            self.assertEqual(collision, "simple")
            mesh = factory()
            half_x = (max(v[0] for v in mesh.vertices)-min(v[0] for v in mesh.vertices))/2
            half_y = (max(v[1] for v in mesh.vertices)-min(v[1] for v in mesh.vertices))/2
            self.assertLessEqual(math.hypot(half_x, half_y), gameplay.PICKUP_FOOTPRINT)

    @unittest.skipUnless(all(os.path.isfile(ctx.terrain_file(name)) for name in
                            ("world_features.json", "heightmap.r16", "weight_Farm.r8")),
                         "generate terrain to check the full current town placement")
    def test_current_world_and_later_shore_farm_placements_remain_stable(self):
        def legacy_square(scene, rng):
            cx, cy = scene.wd.town["center"]
            for i, (x, y) in enumerate(((1450, -1250), (-1500, 1350), (1600, 1500), (-1350, -1500))):
                scene.place("SM_Privy_A", cx+x, cy+y, rng.uniform(0, 360), "Privy %d" % i,
                            radius=220, z_offset=-8, check=False)
            return 4

        current, rng = prior_town()
        legacy, old_rng = prior_town()
        self.assertEqual(actor_snapshot(current.actors), actor_snapshot(legacy.actors))
        before = len(current.actors)
        count = town._place_conveniences(current, rng)
        with patch.object(town, "_place_square_conveniences", side_effect=legacy_square):
            self.assertEqual(town._place_conveniences(legacy, old_rng), count)
        self.assert_clear_sites(current)
        self.assertEqual(actor_snapshot(current.actors[before+4:]), actor_snapshot(legacy.actors[before+4:]))
        self.assertEqual(rng.next(), old_rng.next())

    @unittest.skipUnless(all(os.path.isfile(ctx.terrain_file(name)) for name in
                            ("world_features.json", "heightmap.r16", "weight_Farm.r8")),
                         "generate terrain to check pickups against the actual town")
    def test_current_world_pickups_keep_all_four_body_and_entrance_reservations_clear(self):
        scene, rng = prior_town()
        town._place_conveniences(scene, rng)
        baseline, state, _, _ = pickup_run(scene.wd, [])
        current, new_state, logs, count = pickup_run(scene.wd, scene.actors)
        self.assertEqual(state, new_state)
        self.assertLess(count, len(baseline))
        self.assertEqual(count, len(current))
        for label, record in current.items():
            self.assertEqual(record, baseline[label])
            _, x, y, _, _ = record
            for actor in self.assert_clear_sites(scene):
                angle = math.radians(actor.rotation.yaw)
                keepouts = [(actor.location.x, actor.location.y, 220.0)] + [
                    (actor.location.x+math.sin(angle)*distance,
                     actor.location.y-math.cos(angle)*distance, 120.0) for distance in (225.0, 350.0)]
                for ox, oy, radius in keepouts:
                    self.assertGreaterEqual(math.hypot(x-ox, y-oy), radius+gameplay.PICKUP_FOOTPRINT)
        self.assertTrue(any("skipped pickup" in line for line in logs))


if __name__ == "__main__":
    unittest.main()
