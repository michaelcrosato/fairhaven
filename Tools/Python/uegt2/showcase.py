"""Asset showcase: one of every generated mesh, lined up for visual review.

This is a development aid, not shipped content. It drops a labelled grid of
every mesh in meshbuild's catalog onto a flat pad in an empty corner of the map
so a screenshot tour can check all of them at once - colours, silhouettes,
scale against a reference figure, and whether anything is inside out.

Run with:  ./Scripts/Build-Content.ps1 -Stages showcase
Remove with: the stage clears its own actors each run, and 'level' wipes it.
"""
from __future__ import annotations

import math

import unreal

from . import ctx

# The flattest patch of farmland in the generated terrain (found by scanning the
# heightmap), so the grid sits level and every silhouette reads clearly.
ORIGIN = (27200.0, -52800.0)
SPACING = 1000.0
COLUMNS = 9
LABEL_PREFIX = "Showcase "


def _clear(actor_subsystem):
    removed = 0
    for actor in actor_subsystem.get_all_level_actors():
        if actor.get_actor_label().startswith(LABEL_PREFIX):
            actor_subsystem.destroy_actor(actor)
            removed += 1
    return removed


def build(world, world_data, meshes=None):
    from . import meshbuild

    if meshes is None:
        meshes = meshbuild.load_all()
    if not meshes:
        ctx.fail("no meshes available; run the 'meshes' stage first")

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = _clear(actor_subsystem)
    if removed:
        ctx.log("showcase: replaced %d actors" % removed)

    names = [name for name, _f, _b, _m, _c in meshbuild._catalog() if name in meshes]
    base_z = world_data.height_uu(ORIGIN[0], ORIGIN[1])

    placed = 0
    for index, name in enumerate(names):
        column = index % COLUMNS
        row = index // COLUMNS
        wx = ORIGIN[0] + (column - (COLUMNS - 1) * 0.5) * SPACING
        wy = ORIGIN[1] + row * SPACING

        actor = actor_subsystem.spawn_actor_from_class(
            unreal.StaticMeshActor, unreal.Vector(wx, wy, base_z),
            unreal.Rotator(0.0, 0.0, 0.0))
        if actor is None:
            continue
        component = actor.get_editor_property("static_mesh_component")
        component.set_editor_property("static_mesh", meshes[name])
        component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
        actor.set_actor_label(LABEL_PREFIX + name)
        placed += 1

    ctx.log("showcase: %d meshes placed at (%.0f, %.0f), ground z=%.0f"
            % (placed, ORIGIN[0], ORIGIN[1], base_z))
    return placed
