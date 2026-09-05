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
from uegt2.meshkit import MeshBuilder


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


if __name__ == "__main__":
    unittest.main()
