"""Fairhaven content build entry point.

Runs inside UnrealEditor-Cmd:

    UnrealEditor-Cmd.exe UEGT2.uproject -run=pythonscript
        -script="Tools/Python/build_content.py <stages>"

``<stages>`` is a comma separated list, or ``all``. Stages run in dependency
order regardless of the order given. Use ``Scripts/Build-Content.ps1`` rather
than invoking this directly.

Stage list:
    materials  master materials
    meshes     generated static mesh assets
    audio      import generated sounds, sound classes and ambience
    level      create/reset the map
    landscape  import terrain
    water      ocean, river, lake and pond bodies
    lighting   sun, sky, fog, post process
    town       buildings, streets, props, docks
    city       Newhaven: blocks, towers, street furniture, wharf
    nature     vegetation and rock scatter
    gameplay   player start, interactables, landmarks
    npc        route network, townsfolk, citizens and animals
"""
from __future__ import annotations

import os
import sys
import time
import traceback

import unreal

# --- bootstrap sys.path so the uegt2 package is importable -------------------
_PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
_PY_ROOT = os.path.join(_PROJECT, "Tools", "Python")
if _PY_ROOT not in sys.path:
    sys.path.insert(0, _PY_ROOT)

from uegt2 import ctx                                            # noqa: E402

ALL_STAGES = ["materials", "meshes", "audio", "level", "landscape", "water",
              "lighting", "town", "city", "nature", "gameplay", "npc", "showcase"]

# Stages that need a level open (and therefore a level save at the end).
WORLD_STAGES = {"level", "landscape", "water", "lighting", "town", "city",
                "nature", "gameplay", "npc", "showcase", "audio"}

SUCCESS_MARKER = "UEGT2_CONTENT_BUILD_SUCCEEDED"


def _requested_stages(argv):
    if not argv:
        return list(ALL_STAGES)
    raw = ",".join(argv).lower()
    if "all" in raw:
        # showcase is a development aid, never part of a full build.
        return [stage for stage in ALL_STAGES if stage != "showcase"]
    wanted = set(part.strip() for part in raw.split(",") if part.strip())
    unknown = wanted - set(ALL_STAGES)
    if unknown:
        ctx.fail("unknown stage(s): %s" % ", ".join(sorted(unknown)))
    return [stage for stage in ALL_STAGES if stage in wanted]


def _editor_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


# Actors the level owns that must survive a reset.
_PERSISTENT_ACTOR_CLASSES = {"WorldDataLayers", "WorldPartitionMiniMap", "WorldSettings"}


def _open_or_create_level(reset: bool):
    """Open the map, creating it if needed. ``reset`` empties it of content.

    The map asset is reused rather than recreated: new_level refuses to
    overwrite an existing asset, and reusing it keeps the package's identity
    stable for source control.
    """
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

    if not ctx.asset_exists(ctx.MAP_PATH):
        if not subsystem.new_level(ctx.MAP_PATH):
            ctx.fail("could not create %s" % ctx.MAP_PATH)
        ctx.log("created level %s" % ctx.MAP_PATH)
        return

    if not subsystem.load_level(ctx.MAP_PATH):
        ctx.fail("could not load %s" % ctx.MAP_PATH)

    if not reset:
        ctx.log("opened existing level %s" % ctx.MAP_PATH)
        return

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for actor in actor_subsystem.get_all_level_actors():
        if actor.get_class().get_name() in _PERSISTENT_ACTOR_CLASSES:
            continue
        if actor_subsystem.destroy_actor(actor):
            removed += 1
    ctx.log("reset level %s: removed %d actors" % (ctx.MAP_PATH, removed))


def main(argv):
    start = time.time()
    stages = _requested_stages(argv)
    ctx.log("content build starting: stages=%s" % ",".join(stages))

    world_data = ctx.WorldData()
    ctx.log("world features loaded: seed=%d size=%d layers=%s"
            % (world_data.seed, world_data.size, ",".join(world_data.layers)))

    from uegt2 import materials as materials_mod
    from uegt2 import landscape as landscape_mod

    built_materials = None
    if "materials" in stages:
        built_materials = materials_mod.build_all(world_data.layers)

    def material(key, path):
        if built_materials and key in built_materials:
            return built_materials[key]
        asset = ctx.load_asset(path)
        if asset is None:
            ctx.fail("material %s missing; run the 'materials' stage first" % path)
        return asset

    meshes = {}
    if "meshes" in stages:
        from uegt2 import meshbuild
        meshes = meshbuild.build_all(
            material("prop", materials_mod.M_PROP),
            material("emissive", materials_mod.M_PROP_EMISSIVE),
            material("foliage", materials_mod.M_FOLIAGE),
            world_data)

    world = None
    if set(stages) & WORLD_STAGES:
        _open_or_create_level(reset=("level" in stages))
        world = _editor_world()
        if world is None:
            ctx.fail("no editor world after opening the level")

    if "audio" in stages:
        from uegt2 import audio as audio_mod
        audio_mod.build(world, world_data)

    if "landscape" in stages:
        landscape_mod.build(world, world_data, material("landscape", materials_mod.M_LANDSCAPE))

    if "water" in stages:
        from uegt2 import water as water_mod
        water_mod.build(world, world_data, material("water", materials_mod.M_WATER))

    if "lighting" in stages:
        from uegt2 import lighting as lighting_mod
        lighting_mod.build(world, world_data)

    if "town" in stages:
        from uegt2 import town as town_mod
        town_mod.build(world, world_data)

    # City before nature: the scatter reads the city hole out of world_features,
    # not out of what is already placed, but keeping build order the same as
    # stage order keeps the logs readable.
    if "city" in stages:
        from uegt2 import city as city_mod
        city_mod.build(world, world_data)

    if "nature" in stages:
        from uegt2 import nature as nature_mod
        nature_mod.build(world, world_data)

    if "gameplay" in stages:
        from uegt2 import gameplay as gameplay_mod
        gameplay_mod.build(world, world_data)

    # npc last of the world stages: it reads the buildings, stalls, docks and
    # park benches the earlier stages placed and turns them into anchors, so it
    # has to run after everything it surveys.
    if "npc" in stages:
        from uegt2 import npc as npc_mod
        npc_mod.build(world, world_data)

    if "showcase" in stages:
        from uegt2 import showcase as showcase_mod
        showcase_mod.build(world, world_data)

    # Only save the map when a stage actually touched the world; an asset-only
    # run (materials, meshes) never opens a level.
    if set(stages) & WORLD_STAGES:
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if not subsystem.save_current_level():
            ctx.fail("could not save %s" % ctx.MAP_PATH)
        ctx.log("saved %s" % ctx.MAP_PATH)
    ctx.save_all_dirty()

    ctx.log("content build finished in %.1fs" % (time.time() - start))
    unreal.log(SUCCESS_MARKER)


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except Exception:                                            # noqa: BLE001
        unreal.log_error("[UEGT2] content build FAILED\n%s" % traceback.format_exc())
        raise
