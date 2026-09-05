"""Regression checks for content selection and mesh validation, without Unreal.

    python Tools/Python/test_pipeline.py
"""
from __future__ import annotations

import contextlib
import io
import unittest

import check_meshes

check_meshes._install_unreal_stub()

import build_content
from uegt2.meshkit import MeshBuilder, _mesh_uvs, cross, dot, sub


class StageSelectionTests(unittest.TestCase):
    def test_default_build_excludes_showcase(self):
        self.assertEqual(build_content._requested_stages([]),
                         build_content._requested_stages(["all"]))
        self.assertNotIn("showcase", build_content._requested_stages([]))

    def test_stages_are_normalised_and_run_in_dependency_order(self):
        self.assertEqual(build_content._requested_stages([" NPC, nature", "NATURE, Town "]),
                         ["town", "nature", "npc"])

    def test_all_does_not_hide_unknown_stages(self):
        for argv in (["small"], ["all,typo"], ["all", "typo"]):
            with self.subTest(argv=argv), self.assertRaisesRegex(RuntimeError, "unknown stage"):
                build_content._requested_stages(argv)

    def test_blank_stage_list_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "no content stages"):
            build_content._requested_stages([" , "])

    def test_showcase_must_be_explicit(self):
        self.assertEqual(build_content._requested_stages(["showcase"]), ["showcase"])
        self.assertEqual(build_content._requested_stages(["all,showcase"]),
                         build_content.ALL_STAGES)


class MeshValidationTests(unittest.TestCase):
    @staticmethod
    def triangle():
        mesh = MeshBuilder()
        mesh.add_triangle((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 0xFFFFFF)
        return mesh

    def test_bad_triangles_are_reported_without_stopping_later_checks(self):
        for triangle in ((0, 1, 99), (-1, 1, 2), (0, 1), (0, 1, 2, 0), (0, 1, 1.5)):
            with self.subTest(triangle=triangle), contextlib.redirect_stdout(io.StringIO()):
                report = check_meshes.Report()
                mesh = self.triangle()
                mesh.triangles = [triangle]
                self.assertFalse(report.mesh("bad", mesh))
                self.assertTrue(report.mesh("next", self.triangle()))
                self.assertEqual(report.checked, 2)
                self.assertEqual(report.failures, 1)

    def test_nonfinite_attributes_cannot_pass_geometry_checks(self):
        for field in ("vertices", "normals", "colors"):
            for value in (float("nan"), float("inf")):
                with self.subTest(field=field, value=value), contextlib.redirect_stdout(io.StringIO()):
                    mesh = self.triangle()
                    buffer = getattr(mesh, field)
                    buffer[0] = (value,) + buffer[0][1:]
                    self.assertFalse(check_meshes.Report().mesh(field, mesh))


class MeshUVTests(unittest.TestCase):
    def test_vertical_and_rotated_faces_have_uv_area(self):
        for yaw, pitch, roll in ((0.0, 0.0, 0.0), (27.0, 61.0, 13.0)):
            with self.subTest(rotation=(yaw, pitch, roll)):
                mesh = MeshBuilder()
                mesh.box((100.0, 200.0, 300.0), (100.0, 200.0, 300.0),
                         0xFFFFFF, yaw=yaw, pitch=pitch, roll=roll)
                before = (list(mesh.vertices), list(mesh.normals), list(mesh.colors),
                          list(mesh.triangles))
                uv0, _uv1 = _mesh_uvs(mesh)
                self.assertEqual(len(uv0), len(mesh.vertices))
                for triangle in mesh.triangles:
                    self.assertGreater(check_meshes._uv_area(uv0, triangle), 1e-12)
                self.assertEqual(_mesh_uvs(mesh)[0], uv0)
                self.assertEqual((mesh.vertices, mesh.normals, mesh.colors, mesh.triangles), before)

    def test_wind_weights_remain_in_second_uv_channel(self):
        mesh = MeshBuilder()
        expected = []
        for index, weight in enumerate((0.0, 0.25, 0.75, 1.0)):
            count_before = len(mesh.vertices)
            mesh.box((index * 200.0, 0.0, 0.0), (100.0, 100.0, 100.0),
                     0xFFFFFF, wind=weight)
            expected.extend([(weight, 0.0)] * (len(mesh.vertices) - count_before))
        _uv0, uv1 = _mesh_uvs(mesh)
        self.assertEqual(uv1, expected)


class WallOpeningTests(unittest.TestCase):
    def test_wall_matches_rectangular_opening_union(self):
        cases = {
            "blank": [],
            "door": [(0.0, 40.0, 0.0, 200.0)],
            "stacked": [(0.0, 40.0, 100.0, 100.0), (0.0, 40.0, 400.0, 100.0)],
            "overlap": [(-10.0, 50.0, 100.0, 150.0), (10.0, 50.0, 180.0, 150.0)],
            "nested": [(0.0, 60.0, 100.0, 300.0), (0.0, 40.0, 180.0, 100.0)],
            "touching horizontal": [(-20.0, 40.0, 100.0, 100.0), (20.0, 40.0, 100.0, 100.0)],
            "touching vertical": [(0.0, 40.0, 100.0, 100.0), (0.0, 40.0, 200.0, 100.0)],
            "duplicate": [(0.0, 40.0, 100.0, 100.0)] * 2,
            "clipped": [(-50.0, 30.0, -20.0, 100.0), (50.0, 30.0, 550.0, 100.0)],
            "outside": [(200.0, 40.0, 100.0, 100.0), (0.0, 40.0, 1000.0, 100.0)],
            "empty": [(0.0, 0.0, 100.0, 100.0), (0.0, 40.0, 100.0, 0.0)],
            "whole wall": [(0.0, 120.0, -20.0, 640.0)],
        }
        for name, openings in cases.items():
            for axis in ("x", "y"):
                with self.subTest(case=name, axis=axis):
                    mesh = MeshBuilder()
                    mesh.wall((100.0, 200.0, 30.0), axis, 100.0, 600.0, 20.0,
                              0xFFFFFF, openings)
                    for along in (-49.0, -36.0, -21.0, -6.0, 9.0, 24.0, 39.0, 49.0):
                        for z in (18.0, 83.0, 148.0, 213.0, 278.0, 343.0, 408.0, 473.0, 538.0, 598.0):
                            hole = any(offset - width * 0.5 < along < offset + width * 0.5
                                       and sill < z < sill + height
                                       for offset, width, sill, height in openings)
                            if axis == "x":
                                start, end = (100.0 + along, 185.0, z + 30.0), (100.0 + along, 215.0, z + 30.0)
                            else:
                                start, end = (85.0, 200.0 + along, z + 30.0), (115.0, 200.0 + along, z + 30.0)
                            self.assertEqual(check_meshes._ray_hits(mesh, start, end), not hole,
                                             "%s/%s at (%s, %s)" % (name, axis, along, z))
                    # Reordering the same openings cannot change the world.
                    reversed_mesh = MeshBuilder()
                    reversed_mesh.wall((100.0, 200.0, 30.0), axis, 100.0, 600.0, 20.0,
                                       0xFFFFFF, reversed(openings))
                    self.assertEqual(mesh.vertices, reversed_mesh.vertices)
                    self.assertEqual(mesh.triangles, reversed_mesh.triangles)
                    for a, b, c in mesh.triangles:
                        normal = cross(sub(mesh.vertices[b], mesh.vertices[a]),
                                       sub(mesh.vertices[c], mesh.vertices[a]))
                        self.assertLess(dot(normal, mesh.normals[a]), 0.0)
                    for x, y, z in mesh.vertices:
                        self.assertTrue(30.0 <= z <= 630.0)
                        self.assertTrue((50.0 <= x <= 150.0 and 190.0 <= y <= 210.0)
                                        if axis == "x" else
                                        (90.0 <= x <= 110.0 and 150.0 <= y <= 250.0))

    def test_outbuilding_entries_remain_clear(self):
        from uegt2 import meshbuild

        factories = {name: factory for name, _folder, factory, _material, _collision
                     in meshbuild._catalog()}
        # The church route crosses both the tower porch and the nave wall.
        for name, front, inside, floor in (("SM_Barn_A", -450.0, -338.0, 28.0),
                                           ("SM_Church_A", -1188.0, -725.0, 40.0),
                                           ("SM_Warehouse_A", -390.0, -278.0, 32.0)):
            with self.subTest(building=name):
                pawn = (-34.0, 34.0, front, inside, floor + 50.0, floor + 180.0)
                self.assertEqual(check_meshes._blocking(factories[name](), pawn), 0)


if __name__ == "__main__":
    unittest.main()
