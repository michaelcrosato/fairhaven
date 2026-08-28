"""Procedural material authoring for Fairhaven.

Design rule: props, buildings and characters all share ONE opaque master
material driven by vertex colour. That keeps draw calls low, removes any texture
dependency, and means the whole palette lives in palette.py. Foliage adds wind,
the landscape uses a layer blend of flat colours, and water is a small
translucent shader used for ponds and the stylised fallback.
"""
from __future__ import annotations

import unreal

from . import ctx
from . import palette as pal

M_PROP = ctx.P_MATERIAL + "/M_Prop"
M_PROP_EMISSIVE = ctx.P_MATERIAL + "/M_PropEmissive"
M_FOLIAGE = ctx.P_MATERIAL + "/M_Foliage"
M_LANDSCAPE = ctx.P_MATERIAL + "/M_Landscape"
M_WATER = ctx.P_MATERIAL + "/M_WaterStylised"
M_GLASS = ctx.P_MATERIAL + "/M_Glass"

_WIND_FUNCTION = "/Engine/Functions/Engine_MaterialFunctions01/WorldPositionOffset/SimpleGrassWind"

MEL = unreal.MaterialEditingLibrary


# --- node helpers -----------------------------------------------------------
def _material(folder: str, name: str):
    """Create or reuse a material asset, clearing any previous node graph."""
    path = folder + "/" + name
    existing = ctx.load_asset(path)
    if existing is not None:
        MEL.delete_all_material_expressions(existing)
        return existing
    ctx.ensure_directory(folder)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        ctx.fail("could not create material %s" % path)
    return material


def _node(material, cls, x=0, y=0):
    return MEL.create_material_expression(material, cls, x, y)


def _const3(material, hex_colour, x, y):
    node = _node(material, unreal.MaterialExpressionConstant3Vector, x, y)
    node.set_editor_property("constant", pal.rgb(hex_colour))
    return node


def _scalar_param(material, name, value, x, y, group="Surface"):
    node = _node(material, unreal.MaterialExpressionScalarParameter, x, y)
    node.set_editor_property("parameter_name", name)
    node.set_editor_property("default_value", value)
    node.set_editor_property("group", group)
    return node


def _const(material, value, x, y):
    node = _node(material, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property("r", value)
    return node


def _to(material, from_node, from_output, prop):
    """Connect an expression to a material property, loudly on failure.

    MaterialEditingLibrary returns False for an unknown pin name instead of
    raising, which silently leaves the property at its default. An unconnected
    BaseColor is black, so these warnings matter.
    """
    if not MEL.connect_material_property(from_node, from_output, prop):
        ctx.warn("connect FAILED: %s output '%s' -> %s"
                 % (type(from_node).__name__, from_output, prop))


def _link(from_node, from_output, to_node, to_input):
    if not MEL.connect_material_expressions(from_node, from_output, to_node, to_input):
        ctx.warn("connect FAILED: %s output '%s' -> %s input '%s'"
                 % (type(from_node).__name__, from_output,
                    type(to_node).__name__, to_input))


def _channel_mask(material, source, channel, x, y):
    """Extract one channel. Every VertexColor output pin is named "", so a
    component mask is the only way to reach the alpha."""
    mask = _node(material, unreal.MaterialExpressionComponentMask, x, y)
    for name in ("r", "g", "b", "a"):
        mask.set_editor_property(name, name == channel)
    _link(source, "", mask, "")
    return mask


def _finish(material, nanite=False, instanced=True):
    # (instanced static meshes, nanite, static mesh, skeletal mesh)
    unreal.UEGT2AuthoringLibrary.ensure_material_usage(material, instanced, nanite, True, False)
    MEL.recompile_material(material)
    ctx.save_asset(material.get_path_name().split(".")[0])


MP = unreal.MaterialProperty


# --- master materials -------------------------------------------------------
def build_prop_material():
    """Opaque, vertex-colour driven. Used by every non-foliage generated mesh."""
    mat = _material(ctx.P_MATERIAL, "M_Prop")
    vc = _node(mat, unreal.MaterialExpressionVertexColor, -420, 0)
    rough = _scalar_param(mat, "Roughness", 0.78, -420, 200)
    spec = _scalar_param(mat, "Specular", 0.35, -420, 320)

    _to(mat, vc, "", MP.MP_BASE_COLOR)
    _to(mat, rough, "", MP.MP_ROUGHNESS)
    _to(mat, spec, "", MP.MP_SPECULAR)
    _finish(mat)
    ctx.log("material: M_Prop")
    return mat


def build_prop_emissive_material():
    """Windows, lamps and signal lights. Vertex colour drives the glow."""
    mat = _material(ctx.P_MATERIAL, "M_PropEmissive")
    vc = _node(mat, unreal.MaterialExpressionVertexColor, -620, 0)
    strength = _scalar_param(mat, "EmissiveStrength", 4.0, -620, 240, "Emissive")
    mul = _node(mat, unreal.MaterialExpressionMultiply, -320, 120)
    _link(vc, "", mul, "A")
    _link(strength, "", mul, "B")
    rough = _scalar_param(mat, "Roughness", 0.45, -620, 360)

    _to(mat, vc, "", MP.MP_BASE_COLOR)
    _to(mat, mul, "", MP.MP_EMISSIVE_COLOR)
    _to(mat, rough, "", MP.MP_ROUGHNESS)
    _finish(mat)
    ctx.log("material: M_PropEmissive")
    return mat


def build_foliage_material():
    """Two-sided vertex-colour foliage with stock wind on world position offset.

    Vertex alpha is the wind weight: mesh generators paint 0 at the trunk base
    and up to 1 at leaf tips, so sway comes free with no animation assets.
    """
    mat = _material(ctx.P_MATERIAL, "M_Foliage")
    mat.set_editor_property("two_sided", True)

    vc = _node(mat, unreal.MaterialExpressionVertexColor, -700, 0)
    rough = _scalar_param(mat, "Roughness", 0.88, -700, 220)

    _to(mat, vc, "", MP.MP_BASE_COLOR)
    _to(mat, rough, "", MP.MP_ROUGHNESS)

    wind_function = ctx.load_asset(_WIND_FUNCTION)
    if wind_function is None:
        ctx.warn("SimpleGrassWind not found; foliage will be static")
    else:
        call = _node(mat, unreal.MaterialExpressionMaterialFunctionCall, -340, 420)
        call.set_editor_property("material_function", wind_function)
        intensity = _scalar_param(mat, "WindIntensity", 0.09, -700, 400, "Wind")
        speed = _scalar_param(mat, "WindSpeed", 0.14, -700, 520, "Wind")
        # Wind weight comes from UV1.x, not vertex alpha: VertexColor's only
        # name-addressable output pin is the float3 RGB one.
        wind_uv = _node(mat, unreal.MaterialExpressionTextureCoordinate, -700, 150)
        wind_uv.set_editor_property("coordinate_index", 1)
        wind_weight = _channel_mask(mat, wind_uv, "r", -520, 150)
        # AdditionalWPO has no default inside the function: leaving it
        # unconnected makes the whole material fail to compile and silently
        # fall back to the default (black) material.
        extra_wpo = _node(mat, unreal.MaterialExpressionConstant3Vector, -700, 640)
        extra_wpo.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        try:
            _link(intensity, "", call, "WindIntensity")
            _link(wind_weight, "", call, "WindWeight")
            _link(speed, "", call, "WindSpeed")
            _link(extra_wpo, "", call, "AdditionalWPO")
            _to(mat, call, "", MP.MP_WORLD_POSITION_OFFSET)
        except Exception as exc:                                # noqa: BLE001
            ctx.warn("wind wiring failed (%s); foliage will be static" % exc)

    _finish(mat)
    ctx.log("material: M_Foliage")
    return mat


def build_landscape_material(layers):
    """Flat per-layer colours blended by the painted weightmaps.

    Deliberately texture-free: the terrain reads as clean stylised colour while
    lighting, fog and foliage supply the richness. Cheap on a 3060-class GPU.
    """
    mat = _material(ctx.P_MATERIAL, "M_Landscape")

    colour_blend = _node(mat, unreal.MaterialExpressionLandscapeLayerBlend, -520, 0)
    rough_blend = _node(mat, unreal.MaterialExpressionLandscapeLayerBlend, -520, 420)

    colour_inputs = []
    rough_inputs = []
    for name in layers:
        colour = pal.rgb(pal.LANDSCAPE_COLOURS.get(name, 0x808080))
        entry = unreal.LayerBlendInput()
        entry.set_editor_property("layer_name", name)
        entry.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
        entry.set_editor_property("preview_weight", 1.0 if name == "Grass" else 0.0)
        entry.set_editor_property("const_layer_input",
                                  unreal.Vector(colour.r, colour.g, colour.b))
        colour_inputs.append(entry)

        rough_value = pal.LANDSCAPE_ROUGHNESS.get(name, 0.9)
        rentry = unreal.LayerBlendInput()
        rentry.set_editor_property("layer_name", name)
        rentry.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_WEIGHT_BLEND)
        rentry.set_editor_property("preview_weight", 1.0 if name == "Grass" else 0.0)
        rentry.set_editor_property("const_layer_input",
                                   unreal.Vector(rough_value, rough_value, rough_value))
        rough_inputs.append(rentry)

    colour_blend.set_editor_property("layers", colour_inputs)
    rough_blend.set_editor_property("layers", rough_inputs)

    mask = _node(mat, unreal.MaterialExpressionComponentMask, -260, 420)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", False)
    mask.set_editor_property("b", False)
    mask.set_editor_property("a", False)
    _link(rough_blend, "", mask, "")

    spec = _const(mat, 0.2, -520, 700)

    # Flat layer colours alone read as mush at close range. Two scales of cheap
    # procedural noise break the ground up: a broad one for macro patchiness and
    # a fine one for near-field detail. No textures, so nothing to author.
    macro = _node(mat, unreal.MaterialExpressionNoise, -900, -320)
    ctx.set_prop(macro, "scale", 0.0022)
    ctx.set_prop(macro, "levels", 3)
    ctx.set_prop(macro, "output_min", 0.84)
    ctx.set_prop(macro, "output_max", 1.16)
    ctx.set_prop(macro, "noise_function", unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)

    detail = _node(mat, unreal.MaterialExpressionNoise, -900, -120)
    ctx.set_prop(detail, "scale", 0.035)
    ctx.set_prop(detail, "levels", 2)
    ctx.set_prop(detail, "output_min", 0.93)
    ctx.set_prop(detail, "output_max", 1.07)
    ctx.set_prop(detail, "noise_function", unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_TEX)

    variation = _node(mat, unreal.MaterialExpressionMultiply, -680, -220)
    _link(macro, "", variation, "A")
    _link(detail, "", variation, "B")

    tinted = _node(mat, unreal.MaterialExpressionMultiply, -230, 0)
    _link(colour_blend, "", tinted, "A")
    _link(variation, "", tinted, "B")

    _to(mat, tinted, "", MP.MP_BASE_COLOR)
    _to(mat, mask, "", MP.MP_ROUGHNESS)
    _to(mat, spec, "", MP.MP_SPECULAR)

    _finish(mat, nanite=True)
    ctx.log("material: M_Landscape (%d layers)" % len(layers))
    return mat


def build_water_material():
    """Translucent stylised water for ponds and as the ocean fallback."""
    mat = _material(ctx.P_MATERIAL, "M_WaterStylised")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Per-pixel surface lighting gives water proper specular; fall back quietly
    # if the enum is renamed in a future engine version.
    for mode_name in ("TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"):
        mode = getattr(unreal.TranslucencyLightingMode, mode_name, None)
        if mode is not None:
            mat.set_editor_property("translucency_lighting_mode", mode)
            break

    deep = _const3(mat, pal.WATER_OCEAN, -760, -120)
    shallow = _const3(mat, pal.WATER_SHALLOW, -760, 40)
    fresnel = _node(mat, unreal.MaterialExpressionFresnel, -760, 220)
    fresnel.set_editor_property("exponent", 4.0)
    fresnel.set_editor_property("base_reflect_fraction", 0.06)

    blend = _node(mat, unreal.MaterialExpressionLinearInterpolate, -420, 0)
    _link(deep, "", blend, "A")
    _link(shallow, "", blend, "B")
    _link(fresnel, "", blend, "Alpha")

    opacity = _scalar_param(mat, "Opacity", 0.82, -760, 400, "Water")
    rough = _scalar_param(mat, "Roughness", 0.06, -760, 520, "Water")
    spec = _const(mat, 1.0, -760, 640)

    _to(mat, blend, "", MP.MP_BASE_COLOR)
    _to(mat, opacity, "", MP.MP_OPACITY)
    _to(mat, rough, "", MP.MP_ROUGHNESS)
    _to(mat, spec, "", MP.MP_SPECULAR)
    _finish(mat)
    ctx.log("material: M_WaterStylised")
    return mat


def build_glass_material():
    """Window glass you can see through, and light can get through.

    The one exception to the opaque-master rule, and it earns it twice over.
    Windows used to be opaque panels painted the colour of glass, which read as
    a blank canvas from inside a room and sealed the building against daylight.
    A translucent material is see-through *and* absent from the Lumen scene, so
    the sun reaches an interior through a window the way it should.

    Vertex colour still drives the tint, so a shopfront and a cottage casement
    can be different colours out of the same palette.
    """
    mat = _material(ctx.P_MATERIAL, "M_Glass")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Seen from inside as often as from outside, and a pane has no back.
    mat.set_editor_property("two_sided", True)
    for mode_name in ("TLM_SURFACE_PER_PIXEL_LIGHTING", "TLM_SURFACE"):
        mode = getattr(unreal.TranslucencyLightingMode, mode_name, None)
        if mode is not None:
            mat.set_editor_property("translucency_lighting_mode", mode)
            break

    vc = _node(mat, unreal.MaterialExpressionVertexColor, -620, 0)
    opacity = _scalar_param(mat, "Opacity", 0.30, -620, 240, "Glass")
    rough = _scalar_param(mat, "Roughness", 0.08, -620, 360, "Glass")
    spec = _const(mat, 1.0, -620, 480)

    _to(mat, vc, "", MP.MP_BASE_COLOR)
    _to(mat, opacity, "", MP.MP_OPACITY)
    _to(mat, rough, "", MP.MP_ROUGHNESS)
    _to(mat, spec, "", MP.MP_SPECULAR)
    _finish(mat)
    ctx.log("material: M_Glass")
    return mat


def build_all(layers):
    ctx.ensure_directory(ctx.P_MATERIAL)
    built = {
        "prop": build_prop_material(),
        "emissive": build_prop_emissive_material(),
        "glass": build_glass_material(),
        "foliage": build_foliage_material(),
        "landscape": build_landscape_material(layers),
        "water": build_water_material(),
    }
    ctx.log("materials built: %d" % len(built))
    return built
