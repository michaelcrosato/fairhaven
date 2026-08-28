"""Sky, sun, fog and post processing for Fairhaven.

The look target is a believable natural world around stylised objects: a warm
directional sun, physical sky atmosphere, gentle volumetric fog for depth, and
constrained auto exposure so screenshots and playtests stay comparable.
"""
from __future__ import annotations

import unreal

from . import ctx
from . import palette as pal

SUN_LABEL = "Fairhaven Sun"
SKYLIGHT_LABEL = "Fairhaven Sky Light"
ATMOSPHERE_LABEL = "Fairhaven Atmosphere"
CLOUD_LABEL = "Fairhaven Clouds"
FOG_LABEL = "Fairhaven Fog"
POST_LABEL = "Fairhaven Post Process"
SKY_CONTROLLER_LABEL = "Fairhaven Sky Controller"

DEFAULT_TIME_OF_DAY = 10.25


def _set_movable(actor):
    """Mobility lives on the root component, not the actor."""
    root = actor.get_editor_property("root_component")
    if root is not None:
        ctx.set_prop(root, "mobility", unreal.ComponentMobility.MOVABLE)


def _spawn(actor_class, location, label, rotation=None):
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = subsystem.spawn_actor_from_class(actor_class, location,
                                             rotation or unreal.Rotator(0, 0, 0))
    if actor is None:
        ctx.fail("could not spawn %s" % label)
    actor.set_actor_label(label)
    return actor


def _clear_existing(world):
    """Remove previously generated lighting actors so the stage is re-runnable."""
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    labels = {SUN_LABEL, SKYLIGHT_LABEL, ATMOSPHERE_LABEL, CLOUD_LABEL,
              FOG_LABEL, POST_LABEL, SKY_CONTROLLER_LABEL}
    removed = 0
    for actor in subsystem.get_all_level_actors():
        if actor.get_actor_label() in labels:
            subsystem.destroy_actor(actor)
            removed += 1
    if removed:
        ctx.log("lighting: replaced %d existing actors" % removed)


def build(world, world_data):
    _clear_existing(world)

    # --- Sun ---------------------------------------------------------------
    sun = _spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 40000), SUN_LABEL,
                 unreal.Rotator(0.0, -42.0, -35.0))
    _set_movable(sun)
    sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
    ctx.set_prop(sun_component, "intensity", 75000.0)
    ctx.set_prop(sun_component, "light_color", unreal.Color(255, 244, 226, 255))
    ctx.set_prop(sun_component, "atmosphere_sun_light", True)
    ctx.set_prop(sun_component, "dynamic_shadow_distance_movable_light", 45000.0)
    ctx.set_prop(sun_component, "cascade_distribution_exponent", 3.2)
    ctx.set_prop(sun_component, "light_source_angle", 0.6)
    ctx.set_prop(sun_component, "cast_shadows", True)
    ctx.set_prop(sun_component, "volumetric_scattering_intensity", 1.1)

    # --- Sky light ---------------------------------------------------------
    sky = _spawn(unreal.SkyLight, unreal.Vector(0, 0, 30000), SKYLIGHT_LABEL)
    _set_movable(sky)
    sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
    ctx.set_prop(sky_component, "real_time_capture", True)
    ctx.set_prop(sky_component, "intensity", 1.0)
    ctx.set_prop(sky_component, "volumetric_scattering_intensity", 1.0)

    # --- Atmosphere and clouds ---------------------------------------------
    atmosphere = _spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), ATMOSPHERE_LABEL)
    _set_movable(atmosphere)

    try:
        clouds = _spawn(unreal.VolumetricCloud, unreal.Vector(0, 0, 0), CLOUD_LABEL)
        _set_movable(clouds)
    except Exception as exc:                                    # noqa: BLE001
        ctx.warn("volumetric clouds unavailable (%s)" % exc)

    # --- Height fog ---------------------------------------------------------
    fog = _spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -2000), FOG_LABEL)
    _set_movable(fog)
    fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    ctx.set_prop(fog_component, "fog_density", 0.012)
    ctx.set_prop(fog_component, "fog_height_falloff", 0.09)
    ctx.set_prop(fog_component, "start_distance", 1500.0)
    ctx.set_prop(fog_component, "fog_max_opacity", 0.92)
    ctx.set_prop(fog_component, "enable_volumetric_fog", True)
    ctx.set_prop(fog_component, "volumetric_fog_distance", 22000.0)
    ctx.set_prop(fog_component, "volumetric_fog_extinction_scale", 0.85)
    ctx.set_prop(fog_component, "second_fog_data", unreal.ExponentialHeightFogData(
        fog_density=0.02, fog_height_falloff=0.6, fog_height_offset=-1200.0))

    # --- Post process -------------------------------------------------------
    post = _spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), POST_LABEL)
    post.set_editor_property("unbound", True)
    post.set_editor_property("priority", 1.0)
    settings = post.get_editor_property("settings")

    def override(name, value):
        ctx.set_prop(settings, "override_" + name, True)
        ctx.set_prop(settings, name, value)

    # Auto exposure, constrained so brightness stays comparable across biomes.
    #
    # IMPORTANT: DefaultEngine.ini sets
    # r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True, which means
    # min/max brightness are EV100 stops, NOT linear multipliers. Daylight sits
    # around EV 12-15. Small values here (0.5-3) read as "almost pitch dark" and
    # blow the whole image out to white.
    #
    # The floor moved from 10.5 to 7.0 when the houses got insides. 10.5 is
    # roughly an overcast afternoon, and a room lit by a lamp is nearer EV 6, so
    # the old floor rendered every interior four stops under - which on screen
    # is black with a white-hot rectangle where the door is. 9.0 buys back most
    # of that without letting the night outside come up to daylight, and the
    # interior lights in town._place_interior_lights carry the rest.
    override("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    override("auto_exposure_min_brightness", 7.0)
    override("auto_exposure_max_brightness", 14.5)
    override("auto_exposure_speed_up", 3.0)
    # Walking in through a front door is the fastest brightness change in the
    # game; at 1.6 the room was still adapting several seconds after you got
    # there.
    override("auto_exposure_speed_down", 3.0)
    override("auto_exposure_bias", 0.0)

    override("bloom_intensity", 0.5)
    override("bloom_threshold", 0.9)
    override("vignette_intensity", 0.32)

    # A gentle push toward the stylised palette without going cartoon.
    override("color_saturation", unreal.Vector4(1.06, 1.06, 1.06, 1.0))
    override("color_contrast", unreal.Vector4(1.04, 1.04, 1.04, 1.0))
    override("color_gamma", unreal.Vector4(1.0, 1.0, 1.0, 1.0))

    override("ambient_occlusion_intensity", 0.55)
    override("ambient_occlusion_radius", 120.0)
    override("motion_blur_amount", 0.0)

    override("lumen_scene_lighting_quality", 1.0)
    override("lumen_scene_detail", 1.0)
    override("lumen_final_gather_quality", 1.0)
    override("lumen_max_trace_distance", 60000.0)
    override("reflection_method", unreal.ReflectionMethod.LUMEN)

    post.set_editor_property("settings", settings)

    # --- Controller ---------------------------------------------------------
    controller_class = unreal.load_class(None, "/Script/UEGT2.UEGT2SkyController")
    if controller_class is None:
        ctx.fail("UEGT2SkyController class not found; build the editor target first")
    controller = _spawn(controller_class, unreal.Vector(0, 0, 0), SKY_CONTROLLER_LABEL)
    controller.set_editor_property("time_of_day", DEFAULT_TIME_OF_DAY)
    controller.set_editor_property("day_length_minutes", 0.0)

    ctx.log("lighting: sun, sky light, atmosphere, clouds, fog, post process, sky controller")
    return {"sun": sun, "sky": sky, "fog": fog, "post": post, "controller": controller}
