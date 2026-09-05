"""Generated street-building clearance regressions, without Unreal.

    python Tools/Python/test_street_clearance.py
"""
import math
import os
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import test_privy_placement as placement
from uegt2 import ctx, meshbuild, town
from uegt2.meshkit import _SmallRng


def road(name, points, width=470.0, **flags):
    return {"name": name, "points": [(*point, 0.0) for point in points],
            "width_uu": width, "is_street": False, **flags}


def clearance(*roads):
    return town._RoadClearance(SimpleNamespace(roads=list(roads)))


def street_scene(world):
    scene = placement.PlacementScene(world)
    rng = _SmallRng(world.seed + 4242)
    with patch.object(town, "place_room_lights", return_value=0), \
            patch.object(town, "_place_interior", return_value=0):
        town._place_plaza(scene, rng)
        town._place_services(scene, rng)
        town._place_streets(scene, rng)
    return scene, rng


class StreetClearanceTests(unittest.TestCase):
    def setUp(self):
        placement.SquarePrivyTests.setUp(self)
        original = placement.Actor.__init__

        def actor_init(actor, *args):
            original(actor, *args)
            actor.props = {}
            actor.component.set_editor_property = lambda name, value: actor.props.__setitem__(name, value)

        patcher = patch.object(placement.Actor, "__init__", actor_init)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_generated_floor_bounds_include_plinth_and_porch_without_buried_stairs(self):
        bounds = town._house_road_bounds()
        self.assertEqual(set(bounds), set(town.TOWN_HOUSES))
        self.assertEqual(bounds["SM_House_A"], (-395.0, 395.0, -440.0, 305.0))
        factories = {name: factory for name, _, factory, *_ in meshbuild._catalog()}
        for name, (xmin, xmax, ymin, ymax) in bounds.items():
            vertices = factories[name]().vertices
            self.assertLess(min(v[1] for v in vertices), ymin)
            self.assertTrue(all(xmin <= v[0] <= xmax and ymin <= v[1] <= ymax
                                for v in vertices if abs(v[2] - town.gen_town.PLINTH_H) < 1e-6))

    def test_closed_segment_and_endpoint_distances(self):
        box = (-100.0, 100.0, -80.0, 80.0)
        self.assertEqual(town._segment_box_distance_sq((-200, 0), (200, 0), box), 0)
        self.assertEqual(town._segment_box_distance_sq((0, 0), (0, 0), box), 0)
        self.assertEqual(town._segment_box_distance_sq((-200, 180), (200, 180), box), 10000)
        self.assertEqual(town._segment_box_distance_sq((200, 180), (300, 280), box), 20000)
        self.assertEqual(town._segment_box_distance_sq((200, 180), (200, 180), box), 20000)

    def test_parallel_frontage_remains_available_and_rotations_preserve_clearance(self):
        # A circle of radius500 would reject this narrow side-on footprint.
        checker = clearance(road("Frontage", [(-2500, 0), (2500, 0)]))
        self.assertIsNone(checker.blocked_by("SM_House_A", 0, 855, 0))
        self.assertIsNone(checker.blocked_by("SM_House_A", 0, 650, 90))
        self.assertEqual(checker.blocked_by("SM_House_A", 0, 620, 90), "Frontage")
        for degrees in (-73.0, 23.0, 137.0):
            c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
            def transform(x, y):
                return (1700 + x*c-y*s, -400 + x*s+y*c)
            turned = clearance(road("Turned", [transform(-2500, 0), transform(2500, 0)]))
            self.assertIsNone(turned.blocked_by("SM_House_A", *transform(0, 650), 90+degrees))
            self.assertEqual(turned.blocked_by("SM_House_A", *transform(0, 620), 90+degrees), "Turned")

    def test_crossing_endpoints_bends_and_city_roads_are_checked(self):
        checker = clearance(road("Other road", [(1000, 0), (0, 0)], is_city=True))
        self.assertEqual(checker.blocked_by("SM_House_A", 0, 650, 0), "Other road")
        self.assertIsNone(checker.blocked_by("SM_House_A", 0, 700, 0))
        bent = clearance(road("Bent", [(-1500, -1500), (0, -1500), (0, 1500)]))
        self.assertEqual(bent.blocked_by("SM_House_A", 300, 0, 0), "Bent")
        # A repeated authored point must retain endpoint clearance safely.
        repeated = clearance(road("Point", [(0, 0), (0, 0)]))
        self.assertEqual(repeated.blocked_by("SM_House_A", 0, 650, 0), "Point")

    def test_invalid_authored_inputs_fail_loudly(self):
        for width in (0.0, -1.0, float("nan"), float("inf")):
            with self.assertRaises(RuntimeError):
                clearance(road("Bad", [(0, 0), (100, 0)], width))
        with self.assertRaises(RuntimeError):
            clearance(road("Bad", [(0, 0), (float("nan"), 0)]))
        checker = clearance(road("Good", [(0, 0), (100, 0)]))
        with self.assertRaises(RuntimeError):
            checker.blocked_by("SM_House_A", 0, 0, float("inf"))
        with self.assertRaises(RuntimeError):
            checker.blocked_circle(0, 0, -1)

    def test_prop_circles_clear_frontage_and_reject_crossing_endpoints(self):
        checker = clearance(road("Street", [(-2000, 0), (2000, 0)]),
                            road("Crossing", [(0, -2000), (0, 2000)]))
        self.assertIsNone(checker.blocked_circle(1000, 365, 90))
        self.assertIsNone(checker.blocked_circle(1000, 365, 110))
        self.assertEqual(checker.blocked_circle(0, 365, 90), "Crossing")
        self.assertEqual(checker.blocked_circle(2100, 0, 110), "Street")
        self.assertIsNone(checker.blocked_circle(2400, 0, 110))

    def test_required_service_exhaustion_fails(self):
        world = placement.flat_world()
        world.roads = [road("Tiny", [(0, 0), (100, 0)], is_street=True)]
        scene = placement.PlacementScene(world)
        with patch.object(town, "place_room_lights", return_value=0), self.assertRaisesRegex(RuntimeError, "required trades"):
            town._place_services(scene, _SmallRng(1))
        world.roads = []
        with self.assertRaisesRegex(RuntimeError, "required trades"):
            town._place_services(scene, _SmallRng(1))

    @unittest.skipUnless(all(os.path.isfile(ctx.terrain_file(name)) for name in
                            ("world_features.json", "heightmap.r16", "weight_Farm.r8")),
                         "generate terrain to replay actual streets")
    def test_current_town_retains_all_trades_and_every_street_building_clears_all_roads(self):
        world = ctx.WorldData()
        checker = town._RoadClearance(world)
        self.assertEqual(checker.blocked_by("SM_House_A", 3700, -2145, 0), "TownStreet3")
        current, rng = street_scene(world)
        with patch.object(town._RoadClearance, "blocked_by", return_value=None), \
                patch.object(town._RoadClearance, "blocked_circle", return_value=None):
            previous, previous_rng = street_scene(world)
        self.assertEqual(rng.state, previous_rng.state, "house rejection preserves every original random draw")
        shops = [a for a in current.actors if a.label.startswith("Town Shop ")]
        self.assertEqual(len(shops), len(meshbuild.TOWN_SERVICES))
        self.assertEqual([a.label.split()[2] for a in shops], [row[0] for row in meshbuild.TOWN_SERVICES])
        self.assertEqual(len({a.label.split()[2] for a in shops}), len(shops))
        names = {id(value): name for name, value in current.meshes.items()}
        for actor in current.actors:
            if not actor.label.startswith(("Town Shop ", "Town House ")):
                continue
            name = names[id(actor.props["static_mesh"])]
            self.assertIsNone(checker.blocked_by(name, actor.location.x, actor.location.y, actor.rotation.yaw), actor.label)
        bank = next(a for a in shops if a.label == "Town Shop bank 11")
        self.assertNotEqual((bank.location.x, bank.location.y), (3700, -2145))
        again, again_rng = street_scene(world)
        self.assertEqual(placement.actor_snapshot(current.actors), placement.actor_snapshot(again.actors))
        self.assertEqual(rng.state, again_rng.state)

    @unittest.skipUnless(all(os.path.isfile(ctx.terrain_file(name)) for name in
                            ("world_features.json", "heightmap.r16", "weight_Farm.r8")),
                         "generate terrain to replay actual street props")
    def test_current_lamps_clutter_and_original_interactive_lamp_sources(self):
        world = ctx.WorldData()
        checker = town._RoadClearance(world)
        self.assertEqual(checker.blocked_circle(4000, -2635, 90), "TownStreet3")
        self.assertEqual(checker.blocked_circle(-3690.826101228595, -2922.0309331081808, 110), "TownStreet0")
        current, rng = street_scene(world)
        with patch.object(town._RoadClearance, "blocked_by", return_value=None), \
                patch.object(town._RoadClearance, "blocked_circle", return_value=None):
            previous, previous_rng = street_scene(world)
        self.assertEqual(rng.state, previous_rng.state)
        self.assertTrue(any(a.label == "Town Clutter 9" for a in previous.actors))
        self.assertFalse(any(a.label == "Town Clutter 9" for a in current.actors))
        for actor in current.actors:
            radius = 90 if actor.label.startswith("Town Lamp ") else 110 if actor.label.startswith("Town Clutter ") else None
            if radius is not None:
                self.assertIsNone(checker.blocked_circle(actor.location.x, actor.location.y, radius), actor.label)
        previous_clutter = {a.label: a for a in previous.actors if a.label.startswith("Town Clutter ")}
        for actor in current.actors:
            if actor.label in previous_clutter:
                self.assertEqual(placement.actor_snapshot([actor]), placement.actor_snapshot([previous_clutter[actor.label]]))
        cx, cy = world.town["center"]
        old_lamps = [a for a in previous.actors if a.label.startswith("Town Lamp ")
                     and math.hypot(a.location.x-cx, a.location.y-cy) <= 12000][:16]
        new_lamps = {(a.location.x, a.location.y, a.location.z) for a in current.actors if a.label.startswith("Town Lamp ")}
        self.assertEqual(len(old_lamps), 16)
        self.assertTrue(all((a.location.x, a.location.y, a.location.z) in new_lamps for a in old_lamps))


if __name__ == "__main__":
    unittest.main()
