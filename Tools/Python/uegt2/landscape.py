"""Create the Fairhaven landscape from the offline terrain build."""
from __future__ import annotations

import unreal

from . import ctx
from . import palette as pal

# Flip to True to render the landscape as Nanite. Costs ~150 MB of map size at
# the current resolution, so it is off for the 0.1 foundation.
ENABLE_NANITE = False

# Debug colours shown in the landscape target-layer list.
_LAYER_DEBUG = {
    "Sand": 0xD8C48E, "Grass": 0x6E9448, "Farm": 0xA98B52, "Jungle": 0x3D7040,
    "Dirt": 0x8A7157, "Rock": 0x86837E, "Snow": 0xEEF2F7,
}


def existing_landscape(world):
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Landscape):
        return actor
    return None


def build(world, world_data, landscape_material):
    """Import heightmap + weightmaps into a new ALandscape. Returns the actor."""
    ctx.ensure_directory(ctx.P_LANDSCAPE)

    layer_infos = []
    for name in world_data.layers:
        info = unreal.UEGT2LandscapeTools.create_or_load_layer_info(
            ctx.P_LANDSCAPE, name, pal.rgb(_LAYER_DEBUG.get(name, 0x808080)))
        if info is None:
            ctx.fail("could not create layer info for %s" % name)
        ctx.save_asset("%s/LI_%s" % (ctx.P_LANDSCAPE, name))
        layer_infos.append(info)

    params = unreal.UEGT2LandscapeImportParams()
    params.set_editor_property("heightmap_path", ctx.terrain_file("heightmap.r16"))
    params.set_editor_property("layer_names", list(world_data.layers))
    params.set_editor_property("weightmap_paths",
                               [ctx.terrain_file("weight_%s.r8" % n) for n in world_data.layers])
    params.set_editor_property("layer_infos", layer_infos)
    params.set_editor_property("size_x", world_data.size)
    params.set_editor_property("size_y", world_data.size)
    params.set_editor_property("quads_per_section",
                               int(world_data.data["landscape"]["quads_per_section"]))
    params.set_editor_property("sections_per_component",
                               int(world_data.data["landscape"]["sections_per_component"]))
    params.set_editor_property("location",
                               unreal.Vector(world_data.origin, world_data.origin, 0.0))
    params.set_editor_property("scale",
                               unreal.Vector(world_data.quad, world_data.quad, world_data.z_scale))
    params.set_editor_property("landscape_material", landscape_material)
    # Nanite landscape bakes an ~8M triangle static mesh into the map (about
    # 150 MB at this resolution) for little gain at 2 km / 1080p. Standard
    # landscape LOD is cheaper to iterate on and to keep in source control.
    params.set_editor_property("enable_nanite", ENABLE_NANITE)

    landscape = unreal.UEGT2LandscapeTools.create_landscape_from_raw(world, params)
    if landscape is None:
        ctx.fail("landscape import returned None")

    landscape.set_actor_label("Fairhaven Landscape")
    ctx.log("landscape created: %d x %d verts, %d layers, %.2f km across"
            % (world_data.size, world_data.size, len(world_data.layers),
               world_data.size * world_data.quad / 100000.0))
    return landscape
