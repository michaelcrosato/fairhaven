"""One-off diagnostic: report what a generated static mesh actually contains.

    UnrealEditor-Cmd.exe UEGT2.uproject -run=pythonscript
        -script="Tools/Python/diagnose_mesh.py"
"""
from __future__ import annotations

import os
import sys

import unreal

_PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
_PY_ROOT = os.path.join(_PROJECT, "Tools", "Python")
if _PY_ROOT not in sys.path:
    sys.path.insert(0, _PY_ROOT)


def report(path):
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        unreal.log_warning("[DIAG] missing %s" % path)
        return

    unreal.log("[DIAG] ===== %s =====" % path)
    unreal.log("[DIAG] lods=%d tris=%d" % (mesh.get_num_lods(),
                                           unreal.UEGT2AuthoringLibrary.get_mesh_triangle_count(mesh)))

    materials = mesh.get_editor_property("static_materials")
    for i, slot in enumerate(materials):
        mat = slot.get_editor_property("material_interface")
        unreal.log("[DIAG] slot %d -> %s" % (i, mat.get_path_name() if mat else "NONE"))

    # Round-trip through a dynamic mesh so we can inspect the attributes.
    dyn = unreal.new_object(unreal.DynamicMesh)
    options = unreal.GeometryScriptCopyMeshFromAssetOptions()
    lod = unreal.GeometryScriptMeshReadLOD()
    try:
        result = unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(
            mesh, dyn, options, lod)
        if isinstance(result, tuple):
            dyn = result[0]
    except Exception as exc:                                    # noqa: BLE001
        unreal.log_warning("[DIAG] copy_mesh_from_static_mesh failed: %s" % exc)
        return

    try:
        info = unreal.GeometryScript_MeshQueries.get_mesh_info(dyn)
        unreal.log("[DIAG] info: %s" % str(info).replace("\n", " | "))
    except Exception as exc:                                    # noqa: BLE001
        unreal.log_warning("[DIAG] get_mesh_info failed: %s" % exc)

    try:
        has_colors = unreal.GeometryScript_MeshQueries.get_has_vertex_colors(dyn)
        unreal.log("[DIAG] has_vertex_colors=%s" % has_colors)
    except Exception as exc:                                    # noqa: BLE001
        unreal.log_warning("[DIAG] get_has_vertex_colors unavailable: %s" % exc)

    try:
        colors, valid, gaps, _ = unreal.GeometryScript_VertexColors.get_mesh_per_vertex_colors(dyn)
        values = colors.get_editor_property("list") if hasattr(colors, "get_editor_property") else None
        unreal.log("[DIAG] per-vertex colours valid=%s gaps=%s" % (valid, gaps))
        if values:
            sample = [values[i] for i in range(0, min(len(values), 400), 97)]
            unreal.log("[DIAG] samples: %s" % ", ".join(
                "(%.2f,%.2f,%.2f,%.2f)" % (c.r, c.g, c.b, c.a) for c in sample))
    except Exception as exc:                                    # noqa: BLE001
        unreal.log_warning("[DIAG] get_mesh_per_vertex_colors failed: %s" % exc)


def material_report(path):
    mat = unreal.EditorAssetLibrary.load_asset(path)
    if mat is None:
        unreal.log_warning("[DIAG] missing material %s" % path)
        return
    unreal.log("[DIAG] ===== %s =====" % path)
    for prop in ("two_sided", "blend_mode", "shading_model", "used_with_instanced_static_meshes",
                 "used_with_static_lighting", "used_with_nanite", "used_with_static_mesh"):
        try:
            unreal.log("[DIAG]   %s = %s" % (prop, mat.get_editor_property(prop)))
        except Exception:                                       # noqa: BLE001
            pass


report("/Game/Fairhaven/Meshes/Nature/SM_Tree_Oak_A")
report("/Game/Fairhaven/Meshes/Nature/SM_Rock_M")
material_report("/Game/Fairhaven/Materials/M_Prop")
material_report("/Game/Fairhaven/Materials/M_Foliage")
unreal.log("[DIAG] DONE")
