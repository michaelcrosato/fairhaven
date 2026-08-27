"""Imports the generated audio and lays out the ambient soundscape.

Sound classes give the settings menu something to drive: every cue belongs to
Effects, Ambience, Music or UI, all of which sit under Master. Ambient beds are
placed as world actors with attenuation, so the mix changes as the player walks
between the town, the shore, the forest and the river.
"""
from __future__ import annotations

import math
import os

import unreal

from . import ctx

LABEL_PREFIX = "Ambience "

SOUND_CLASSES = ["SC_Master", "SC_Effects", "SC_Ambience", "SC_Music", "SC_UI"]

# name -> (sound class, looping)
SOUND_SETUP = {
    "S_FootstepGround": ("SC_Effects", False),
    "S_FootstepWater": ("SC_Effects", False),
    "S_Jump": ("SC_Effects", False),
    "S_Land": ("SC_Effects", False),
    "S_Interact": ("SC_Effects", False),
    "S_UIClick": ("SC_UI", False),
    "S_UIConfirm": ("SC_UI", False),
    "A_Wind": ("SC_Ambience", True),
    "A_Ocean": ("SC_Ambience", True),
    "A_Birds": ("SC_Ambience", True),
    "A_Town": ("SC_Ambience", True),
    "A_Stream": ("SC_Ambience", True),
}


def _audio_source_dir():
    return os.path.join(ctx.project_dir(), "Tools", "Audio", "Output")


def _create_sound_classes():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    created = {}
    for name in SOUND_CLASSES:
        path = "%s/%s" % (ctx.P_AUDIO, name)
        existing = ctx.load_asset(path)
        if existing is not None:
            created[name] = existing
            continue
        try:
            asset = tools.create_asset(name, ctx.P_AUDIO, unreal.SoundClass,
                                       unreal.SoundClassFactory())
        except Exception as exc:                                # noqa: BLE001
            ctx.warn("audio: could not create %s (%s)" % (name, exc))
            continue
        if asset is not None:
            created[name] = asset
            ctx.save_asset(path)

    # Parent everything under Master so one slider scales the whole mix.
    master = created.get("SC_Master")
    if master is not None:
        children = [created[n] for n in SOUND_CLASSES
                    if n != "SC_Master" and n in created]
        ctx.set_prop(master, "child_classes", children)
        ctx.save_asset("%s/%s" % (ctx.P_AUDIO, "SC_Master"))

    ctx.log("audio: %d sound classes" % len(created))
    return created


def _import_sounds(sound_classes):
    source = _audio_source_dir()
    if not os.path.isdir(source):
        ctx.fail("audio source folder missing: %s (run Tools/Audio/generate_audio.py)" % source)

    tasks = []
    for name in SOUND_SETUP:
        wav = os.path.join(source, name + ".wav")
        if not os.path.exists(wav):
            ctx.warn("audio: %s.wav not found" % name)
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", wav)
        task.set_editor_property("destination_path", ctx.P_AUDIO)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tasks.append(task)

    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    imported = {}
    for name, (class_name, looping) in SOUND_SETUP.items():
        path = "%s/%s" % (ctx.P_AUDIO, name)
        wave_asset = ctx.load_asset(path)
        if wave_asset is None:
            ctx.warn("audio: %s did not import" % name)
            continue
        ctx.set_prop(wave_asset, "looping", looping)
        sound_class = sound_classes.get(class_name)
        if sound_class is not None:
            ctx.set_prop(wave_asset, "sound_class_object", sound_class)
        ctx.save_asset(path)
        imported[name] = wave_asset

    ctx.log("audio: %d sounds imported" % len(imported))
    return imported


def _spawn_ambient(subsystem, sound, wx, wy, wz, label, radius, volume=1.0):
    """One ambient emitter. radius <= 0 means a global, unattenuated bed."""
    actor = subsystem.spawn_actor_from_class(
        unreal.AmbientSound, unreal.Vector(wx, wy, wz), unreal.Rotator(0, 0, 0))
    if actor is None:
        return None
    component = actor.get_editor_property("audio_component")
    ctx.set_prop(component, "sound", sound)
    ctx.set_prop(component, "volume_multiplier", volume)
    ctx.set_prop(component, "auto_activate", True)

    if radius > 0.0:
        ctx.set_prop(component, "override_attenuation", True)
        settings = component.get_editor_property("attenuation_overrides")
        ctx.set_prop(settings, "attenuation_shape_extents",
                     unreal.Vector(radius, radius, radius))
        ctx.set_prop(settings, "falloff_distance", radius * 1.2)
        ctx.set_prop(settings, "spatialize", True)
        ctx.set_prop(settings, "attenuate", True)
        ctx.set_prop(component, "attenuation_overrides", settings)
    else:
        ctx.set_prop(component, "override_attenuation", True)
        settings = component.get_editor_property("attenuation_overrides")
        ctx.set_prop(settings, "attenuate", False)
        ctx.set_prop(settings, "spatialize", False)
        ctx.set_prop(component, "attenuation_overrides", settings)

    actor.set_actor_label(LABEL_PREFIX + label)
    return actor


def _clear(subsystem):
    removed = 0
    for actor in subsystem.get_all_level_actors():
        if actor.get_actor_label().startswith(LABEL_PREFIX):
            subsystem.destroy_actor(actor)
            removed += 1
    return removed


def build(world, world_data):
    sound_classes = _create_sound_classes()
    sounds = _import_sounds(sound_classes)

    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = _clear(subsystem)
    if removed:
        ctx.log("audio: replaced %d ambient emitters" % removed)

    placed = 0
    cx, cy = world_data.town["center"]

    # A global wind bed everywhere.
    if "A_Wind" in sounds:
        if _spawn_ambient(subsystem, sounds["A_Wind"], cx, cy,
                          world_data.height_uu(cx, cy) + 400.0,
                          "Wind", radius=0.0, volume=0.32):
            placed += 1

    # Surf along the shoreline.
    if "A_Ocean" in sounds:
        for index, (x, y) in enumerate(world_data.coast):
            if index % 10 != 0:
                continue
            wz = world_data.height_uu(x, y) + 200.0
            if _spawn_ambient(subsystem, sounds["A_Ocean"], x, y, wz,
                              "Surf %d" % index, radius=17000.0, volume=0.75):
                placed += 1

    # Town murmur over the square.
    if "A_Town" in sounds:
        if _spawn_ambient(subsystem, sounds["A_Town"], cx, cy,
                          world_data.height_uu(cx, cy) + 250.0,
                          "Town", radius=16000.0, volume=0.55):
            placed += 1

    # Birds in the wooded country, sampled off the grass layer.
    if "A_Birds" in sounds:
        birds = 0
        extent = world_data.extent - 12000.0
        step = 26000.0
        x = -extent
        while x <= extent:
            y = -extent
            while y <= extent:
                if world_data.weight_at("Grass", x, y) > 0.55 or \
                        world_data.weight_at("Jungle", x, y) > 0.4:
                    wz = world_data.height_uu(x, y) + 500.0
                    if _spawn_ambient(subsystem, sounds["A_Birds"], x, y, wz,
                                      "Birds %d" % birds, radius=15000.0, volume=0.4):
                        birds += 1
                        placed += 1
                y += step
            x += step

    # Running water along the river.
    if "A_Stream" in sounds:
        for index, point in enumerate(world_data.river["points"]):
            if index % 6 != 0:
                continue
            wz = point[2] * 100.0 + 150.0
            if _spawn_ambient(subsystem, sounds["A_Stream"], point[0], point[1], wz,
                              "Stream %d" % index, radius=9000.0, volume=0.6):
                placed += 1

    ctx.log("audio: %d ambient emitters placed" % placed)
    return placed
