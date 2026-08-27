"""Ocean, river, lagoon and pond surfaces, plus the swimmable volume.

This uses generated surfaces and a stylised translucent material rather than the
experimental Water plugin. The trade-off is deliberate for 0.1:

  + full control over shape and colour, and it costs almost nothing on a
    3060-class GPU
  + the surfaces are ordinary static meshes, so they cook and stream normally
  - no Gerstner waves, water-info textures or buoyancy

The sea is a single plane at Z = 0. Because the terrain generator carves
everything below sea level (ocean shelf, lagoon, river mouth), that one plane
produces the whole coastline for free. The river needs its own ribbon because
its surface slopes from the mountains down to the sea.
"""
from __future__ import annotations

import math

import unreal

from . import ctx
from . import palette as pal
from . import meshkit
from .meshkit import MeshBuilder

LABEL_PREFIX = "Water "

OCEAN_MESH = ctx.P_MESH + "/Water/SM_WaterPlane"
RIVER_MESH = ctx.P_MESH + "/Water/SM_RiverSurface"

# The sea plane is pushed a hair below zero so it never z-fights the beach.
SEA_LEVEL_Z = -4.0
PLANE_SIZE = 1000.0


def _plane_builder(size, colour):
    mesh = MeshBuilder()
    half = size * 0.5
    mesh.add_quad((-half, -half, 0.0), (half, -half, 0.0),
                  (half, half, 0.0), (-half, half, 0.0), colour)
    return mesh


def _river_builder(world_data):
    """A ribbon following the river, widening toward the sea.

    The surface height comes from the ACTUAL carved terrain at the centreline,
    not the river's design elevation. Road carving and the final smoothing pass
    move the bed, and using the design value leaves the ribbon floating above
    the ground near the mouth.
    """
    points = world_data.river["points"]
    if len(points) < 2:
        return None

    mesh = MeshBuilder()
    total = len(points) - 1
    previous = None

    for index, point in enumerate(points):
        t = index / float(max(total, 1))
        wx, wy, elevation_m = point[0], point[1], point[2]
        bed = world_data.height_uu(wx, wy)
        z = bed + 230.0                       # a couple of metres of water over the bed
        if bed < 0.0:
            # Seaward of the shoreline the sea plane takes over; keep the ribbon
            # under it so it does not show as a slab floating on the water.
            z = min(z, SEA_LEVEL_Z - 6.0)

        # Direction along the river, for the perpendicular.
        if index < total:
            nxt = points[index + 1]
            dx, dy = nxt[0] - wx, nxt[1] - wy
        else:
            prv = points[index - 1]
            dx, dy = wx - prv[0], wy - prv[1]
        length = math.hypot(dx, dy) or 1.0
        px, py = -dy / length, dx / length

        # Narrower than the carved channel so the banks always contain it.
        half = (760.0 + 1500.0 * (t ** 1.4)) * 0.5
        left = (wx + px * half, wy + py * half, z)
        right = (wx - px * half, wy - py * half, z)

        if previous is not None:
            mesh.add_quad(previous[0], previous[1], right, left, pal.WATER_RIVER)
        previous = (left, right)

    return mesh


def _clear(subsystem):
    removed = 0
    for actor in subsystem.get_all_level_actors():
        if actor.get_actor_label().startswith(LABEL_PREFIX):
            subsystem.destroy_actor(actor)
            removed += 1
    return removed


def _spawn_surface(subsystem, mesh_asset, location, rotation, scale, label):
    actor = subsystem.spawn_actor_from_class(unreal.StaticMeshActor, location, rotation)
    if actor is None:
        return None
    component = actor.get_editor_property("static_mesh_component")
    component.set_editor_property("static_mesh", mesh_asset)
    component.set_editor_property("mobility", unreal.ComponentMobility.STATIC)
    component.set_editor_property("cast_shadow", False)
    # Water must not block the interaction trace or the player's feet.
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    actor.set_actor_scale3d(scale)
    actor.set_actor_label(LABEL_PREFIX + label)
    return actor


def _build_swim_volume(world, world_data):
    """A physics volume below sea level so the player actually swims."""
    extent = world_data.extent
    builder = MeshBuilder()
    depth = 9000.0
    builder.box((0.0, 0.0, -depth * 0.5), (extent * 2.2, extent * 2.2, depth),
                pal.WATER_OCEAN)
    dynamic = meshkit.build_dynamic_mesh(builder)

    options = unreal.GeometryScriptCreateNewVolumeFromMeshOptions()
    ctx.set_prop(options, "volume_type", unreal.PhysicsVolume)
    ctx.set_prop(options, "auto_simplify", False)

    try:
        result = unreal.GeometryScript_NewAssetUtils.create_new_volume_from_mesh(
            dynamic, world, unreal.Transform(), "WaterVolume", options)
        volume = ctx.unwrap(result)
    except Exception as exc:                                    # noqa: BLE001
        ctx.warn("water: could not create swim volume (%s)" % exc)
        return None

    if volume is None:
        ctx.warn("water: swim volume creation returned nothing")
        return None

    ctx.set_prop(volume, "water_volume", True)
    ctx.set_prop(volume, "terminal_velocity", 1800.0)
    volume.set_actor_label(LABEL_PREFIX + "Swim Volume")
    ctx.log("water: swim volume created (surface at z=0, %.0f m deep)" % (depth / 100.0))
    return volume


def build(world, world_data, water_material):
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = _clear(subsystem)
    if removed:
        ctx.log("water: replaced %d existing actors" % removed)

    ctx.ensure_directory(ctx.P_MESH + "/Water")

    ocean_mesh = meshkit.create_static_mesh(
        _plane_builder(PLANE_SIZE, pal.WATER_OCEAN), OCEAN_MESH, water_material,
        collision="none")

    river_builder = _river_builder(world_data)
    river_mesh = None
    if river_builder is not None:
        river_mesh = meshkit.create_static_mesh(
            river_builder, RIVER_MESH, water_material, collision="none")

    # --- Sea -----------------------------------------------------------------
    # Massively oversized on purpose: it is one quad, and anything smaller shows
    # a hard edge where the plane stops against the sky atmosphere's ground.
    ocean_scale = (world_data.extent * 20.0) / PLANE_SIZE
    _spawn_surface(subsystem, ocean_mesh,
                   unreal.Vector(0.0, 0.0, SEA_LEVEL_Z),
                   unreal.Rotator(0.0, 0.0, 0.0),
                   unreal.Vector(ocean_scale, ocean_scale, 1.0),
                   "Sea")

    # --- River ---------------------------------------------------------------
    if river_mesh is not None:
        _spawn_surface(subsystem, river_mesh, unreal.Vector(0.0, 0.0, 0.0),
                       unreal.Rotator(0.0, 0.0, 0.0),
                       unreal.Vector(1.0, 1.0, 1.0), "River")

    # --- Inland ponds --------------------------------------------------------
    ponds = 0
    for pond in world_data.ponds:
        cx, cy = pond["center"]
        radius = pond["radius_uu"]
        level = pond["water_level_m"] * 100.0
        scale = (radius * 2.1) / PLANE_SIZE
        if _spawn_surface(subsystem, ocean_mesh,
                          unreal.Vector(cx, cy, level),
                          unreal.Rotator(0.0, 0.0, 0.0),
                          unreal.Vector(scale, scale, 1.0),
                          "Pond %s" % pond["name"]):
            ponds += 1

    _build_swim_volume(world, world_data)

    ctx.log("water: sea plane (%.1f km across), river ribbon, %d ponds"
            % (world_data.extent * 20.0 / 100000.0, ponds))
    return ponds
