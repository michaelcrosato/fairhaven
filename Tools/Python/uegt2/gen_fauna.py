"""Generated animals: the other half of the population.

Same rules as everything else in the world - boxes and blobs with vertex
colours, one shared opaque material, a few dozen triangles each. There are more
animals than buildings in Fairhaven, so every one of these is built to be cheap
first and characterful second.

**Everything here faces +X.** The NPC actor turns to face its direction of
travel by setting actor yaw, and actor forward is +X, so a duck modelled facing
+Y would spend its life walking sideways. The villager figures in gen_town.py
are symmetric enough not to care; a beak is not.

Scale reference, same as the buildings: the player is 180 uu tall.
"""
from __future__ import annotations

from . import palette as pal
from .meshkit import MeshBuilder, _SmallRng


# ---------------------------------------------------------------------------
# Shared parts
# ---------------------------------------------------------------------------
def _legs(mesh, body_len, body_w, leg_h, leg_w, colour, hoof=None):
    """Four legs under a body box, with an optional darker hoof or paw."""
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            x = sx * body_len * 0.32
            y = sy * (body_w * 0.5 - leg_w * 0.65)
            mesh.box((x, y, leg_h * 0.5), (leg_w, leg_w, leg_h), colour)
            if hoof is not None:
                mesh.box((x, y, leg_h * 0.09), (leg_w * 1.15, leg_w * 1.15, leg_h * 0.18), hoof)


def _ears(mesh, x, z, spread, size, colour, lean=0.0):
    for sy in (-1.0, 1.0):
        mesh.box((x, sy * spread, z), size, colour, roll=sy * lean)


def _tail(mesh, x, z, length, thickness, colour, pitch=-25.0):
    mesh.box((x - length * 0.5, 0.0, z), (length, thickness, thickness), colour, pitch=pitch)


# ---------------------------------------------------------------------------
# Farmyard and town animals
# ---------------------------------------------------------------------------
def dog(seed=1):
    """A working dog: knee high, alert, permanently interested in you."""
    rng = _SmallRng(seed)
    coat = (pal.FUR_TAN, pal.FUR_BROWN, pal.FUR_DARK, pal.FUR_WHITE,
            pal.FUR_GREY)[int(rng.next() * 5) % 5]

    mesh = MeshBuilder()
    leg_h, body_h = 27.0, 26.0
    body_z = leg_h + body_h * 0.5
    mesh.box((0.0, 0.0, body_z), (62.0, 26.0, body_h), coat)
    _legs(mesh, 62.0, 26.0, leg_h, 9.0, coat, pal.FUR_DARK)

    # Neck and head carried above the shoulder line: the difference between a
    # dog and a sheep at this poly count is entirely head height.
    mesh.box((26.0, 0.0, body_z + 14.0), (18.0, 18.0, 24.0), coat, pitch=-22.0)
    mesh.box((38.0, 0.0, body_z + 26.0), (28.0, 22.0, 24.0), coat)
    mesh.box((54.0, 0.0, body_z + 21.0), (18.0, 14.0, 13.0), coat)
    mesh.box((62.0, 0.0, body_z + 21.0), (5.0, 9.0, 7.0), pal.HIDE_BLACK)
    _ears(mesh, 32.0, body_z + 40.0, 8.5, (7.0, 8.0, 14.0), coat, lean=16.0)
    _tail(mesh, -30.0, body_z + 10.0, 26.0, 7.0, coat, pitch=-52.0)
    return mesh


def cat(seed=1):
    rng = _SmallRng(seed)
    coat = (pal.FUR_GINGER, pal.FUR_DARK, pal.FUR_GREY, pal.FUR_WHITE)[int(rng.next() * 4) % 4]

    mesh = MeshBuilder()
    leg_h, body_h = 18.0, 17.0
    body_z = leg_h + body_h * 0.5
    mesh.box((0.0, 0.0, body_z), (44.0, 17.0, body_h), coat)
    _legs(mesh, 44.0, 17.0, leg_h, 6.0, coat)
    mesh.box((20.0, 0.0, body_z + 9.0), (12.0, 12.0, 16.0), coat, pitch=-18.0)
    mesh.box((29.0, 0.0, body_z + 18.0), (18.0, 16.0, 15.0), coat)
    mesh.box((38.0, 0.0, body_z + 15.0), (7.0, 8.0, 7.0), coat)
    # Pointed ears, which is most of what makes a small quadruped read as a cat.
    for sy in (-1.0, 1.0):
        mesh.cone((26.0, sy * 6.0, body_z + 25.0), 5.0, 11.0, coat, sides=4)
    _tail(mesh, -22.0, body_z + 6.0, 34.0, 5.0, coat, pitch=-64.0)
    return mesh


def sheep(seed=1):
    """Wool as a blob, legs and head as boxes. Reads at fifty metres."""
    mesh = MeshBuilder()
    leg_h = 30.0
    mesh.icosphere((0.0, 0.0, leg_h + 26.0), 40.0, pal.WOOL_CREAM,
                   subdivisions=0, squash=0.72, jitter=0.16, seed=seed)
    _legs(mesh, 66.0, 34.0, leg_h, 9.0, pal.HIDE_BLACK, pal.HOOF_DARK)
    mesh.box((34.0, 0.0, leg_h + 30.0), (24.0, 18.0, 20.0), pal.HIDE_BLACK)
    mesh.box((47.0, 0.0, leg_h + 26.0), (12.0, 13.0, 13.0), pal.HIDE_BLACK)
    _ears(mesh, 32.0, leg_h + 38.0, 11.0, (5.0, 12.0, 6.0), pal.HIDE_BLACK)
    return mesh


def cow(seed=1):
    """The biggest thing in the fields, and the one that never runs from you."""
    rng = _SmallRng(seed)
    hide = pal.HIDE_BROWN if rng.next() > 0.5 else pal.HIDE_BLACK
    patch = pal.FUR_WHITE

    mesh = MeshBuilder()
    leg_h, body_h = 62.0, 62.0
    body_z = leg_h + body_h * 0.5
    mesh.box((0.0, 0.0, body_z), (148.0, 62.0, body_h), hide)
    # Two patches, placed off centre so the herd does not look printed.
    mesh.box((-24.0, 0.0, body_z + 4.0), (46.0, 63.5, 34.0), patch)
    mesh.box((44.0, 0.0, body_z - 12.0), (30.0, 63.5, 24.0), patch)
    _legs(mesh, 148.0, 62.0, leg_h, 16.0, hide, pal.HOOF_DARK)

    mesh.box((78.0, 0.0, body_z + 14.0), (34.0, 34.0, 40.0), hide, pitch=-16.0)
    mesh.box((100.0, 0.0, body_z + 26.0), (34.0, 28.0, 26.0), hide)
    mesh.box((118.0, 0.0, body_z + 20.0), (14.0, 22.0, 18.0), pal.PIG_PINK)
    _ears(mesh, 92.0, body_z + 36.0, 17.0, (8.0, 16.0, 8.0), hide)
    for sy in (-1.0, 1.0):
        mesh.box((96.0, sy * 11.0, body_z + 44.0), (7.0, 18.0, 7.0), pal.HORN_PALE,
                 roll=sy * 32.0)
    _tail(mesh, -74.0, body_z + 20.0, 46.0, 6.0, hide, pitch=-78.0)
    return mesh


def pig(seed=1):
    rng = _SmallRng(seed)
    hide = pal.PIG_PINK if rng.next() > 0.3 else pal.FUR_DARK

    mesh = MeshBuilder()
    leg_h, body_h = 26.0, 38.0
    body_z = leg_h + body_h * 0.5
    mesh.box((0.0, 0.0, body_z), (82.0, 40.0, body_h), hide)
    _legs(mesh, 82.0, 40.0, leg_h, 11.0, hide, pal.HOOF_DARK)
    mesh.box((46.0, 0.0, body_z + 2.0), (26.0, 30.0, 28.0), hide)
    # A box, not a cylinder: meshkit's cylinder only ever stands on +Z, and a
    # snout has to point along +X with the rest of the animal.
    mesh.box((60.0, 0.0, body_z + 1.0), (12.0, 20.0, 16.0), pal.PIG_PINK)
    _ears(mesh, 44.0, body_z + 18.0, 12.0, (8.0, 10.0, 10.0), hide, lean=24.0)
    _tail(mesh, -42.0, body_z + 12.0, 14.0, 5.0, hide, pitch=-70.0)
    return mesh


def goat(seed=1):
    rng = _SmallRng(seed)
    coat = (pal.FUR_TAN, pal.FUR_WHITE, pal.FUR_DARK)[int(rng.next() * 3) % 3]

    mesh = MeshBuilder()
    leg_h, body_h = 34.0, 30.0
    body_z = leg_h + body_h * 0.5
    mesh.box((0.0, 0.0, body_z), (68.0, 28.0, body_h), coat)
    _legs(mesh, 68.0, 28.0, leg_h, 8.0, coat, pal.HOOF_DARK)
    mesh.box((36.0, 0.0, body_z + 14.0), (20.0, 18.0, 24.0), coat, pitch=-24.0)
    mesh.box((48.0, 0.0, body_z + 26.0), (24.0, 17.0, 17.0), coat)
    # Swept horns and a beard: the two things that stop it being a small sheep.
    for sy in (-1.0, 1.0):
        mesh.box((40.0, sy * 6.0, body_z + 38.0), (22.0, 5.0, 6.0), pal.HORN_PALE, pitch=32.0)
    mesh.box((56.0, 0.0, body_z + 16.0), (7.0, 8.0, 14.0), pal.FUR_WHITE)
    _tail(mesh, -34.0, body_z + 12.0, 12.0, 6.0, coat, pitch=-70.0)
    return mesh


def horse(seed=1):
    rng = _SmallRng(seed)
    hide = (pal.HIDE_BROWN, pal.FUR_DARK, pal.FUR_TAN)[int(rng.next() * 3) % 3]
    mane = pal.HIDE_BLACK if hide != pal.HIDE_BLACK else pal.FUR_DARK

    mesh = MeshBuilder()
    leg_h, body_h = 88.0, 58.0
    body_z = leg_h + body_h * 0.5
    mesh.box((0.0, 0.0, body_z), (152.0, 52.0, body_h), hide)
    _legs(mesh, 152.0, 52.0, leg_h, 14.0, hide, pal.HOOF_DARK)

    # A near vertical neck is what makes a horse a horse from a distance.
    mesh.box((74.0, 0.0, body_z + 32.0), (30.0, 26.0, 62.0), hide, pitch=-14.0)
    mesh.box((92.0, 0.0, body_z + 62.0), (44.0, 22.0, 22.0), hide, pitch=18.0)
    mesh.box((112.0, 0.0, body_z + 58.0), (16.0, 18.0, 16.0), hide)
    mesh.box((66.0, 0.0, body_z + 44.0), (10.0, 8.0, 66.0), mane, pitch=-14.0)
    _ears(mesh, 92.0, body_z + 76.0, 8.0, (6.0, 7.0, 14.0), hide)
    _tail(mesh, -76.0, body_z + 22.0, 56.0, 10.0, mane, pitch=-72.0)
    return mesh


def rabbit(seed=1):
    mesh = MeshBuilder()
    leg_h = 8.0
    mesh.icosphere((0.0, 0.0, leg_h + 12.0), 15.0, pal.FUR_GREY,
                   subdivisions=0, squash=0.85, jitter=0.12, seed=seed)
    mesh.box((13.0, 0.0, leg_h + 16.0), (13.0, 11.0, 12.0), pal.FUR_GREY)
    _ears(mesh, 12.0, leg_h + 32.0, 4.5, (4.0, 5.0, 22.0), pal.FUR_GREY, lean=9.0)
    for sy in (-1.0, 1.0):
        mesh.box((-6.0, sy * 8.0, leg_h * 0.5), (16.0, 7.0, leg_h), pal.FUR_GREY)
    mesh.icosphere((-16.0, 0.0, leg_h + 12.0), 6.0, pal.FUR_WHITE, subdivisions=0, seed=seed)
    return mesh


# ---------------------------------------------------------------------------
# Birds
# ---------------------------------------------------------------------------
def chicken(seed=1):
    rng = _SmallRng(seed)
    plume = (pal.FEATHER_WHITE, pal.FEATHER_BROWN, pal.FUR_DARK)[int(rng.next() * 3) % 3]

    mesh = MeshBuilder()
    leg_h = 11.0
    mesh.icosphere((0.0, 0.0, leg_h + 14.0), 17.0, plume,
                   subdivisions=0, squash=0.86, jitter=0.14, seed=seed)
    for sy in (-1.0, 1.0):
        mesh.box((0.0, sy * 6.0, leg_h * 0.5), (4.0, 4.0, leg_h), pal.BEAK_ORANGE)
    mesh.box((11.0, 0.0, leg_h + 27.0), (10.0, 9.0, 12.0), plume)
    mesh.box((18.0, 0.0, leg_h + 26.0), (7.0, 4.0, 4.0), pal.BEAK_ORANGE)
    mesh.box((10.0, 0.0, leg_h + 35.0), (9.0, 3.0, 6.0), pal.COMB_RED)
    mesh.box((12.0, 0.0, leg_h + 21.0), (4.0, 4.0, 6.0), pal.COMB_RED)
    # Tail feathers, angled up: a chicken with a flat back looks like a loaf.
    mesh.box((-16.0, 0.0, leg_h + 22.0), (16.0, 5.0, 14.0), plume, pitch=-42.0)
    return mesh


def duck(seed=1):
    rng = _SmallRng(seed)
    body = pal.FEATHER_WHITE if rng.next() > 0.55 else pal.FEATHER_BROWN

    mesh = MeshBuilder()
    leg_h = 7.0
    mesh.icosphere((0.0, 0.0, leg_h + 11.0), 16.0, body,
                   subdivisions=0, squash=0.62, jitter=0.1, seed=seed)
    for sy in (-1.0, 1.0):
        mesh.box((0.0, sy * 5.0, leg_h * 0.5), (4.0, 4.0, leg_h), pal.BEAK_ORANGE)
    mesh.box((10.0, 0.0, leg_h + 24.0), (10.0, 9.0, 11.0),
             pal.FEATHER_GREEN if body == pal.FEATHER_BROWN else body)
    mesh.box((19.0, 0.0, leg_h + 21.0), (11.0, 7.0, 3.0), pal.BEAK_ORANGE)
    mesh.box((-16.0, 0.0, leg_h + 14.0), (14.0, 6.0, 5.0), body, pitch=-22.0)
    return mesh


def seagull(seed=1):
    mesh = MeshBuilder()
    leg_h = 9.0
    mesh.icosphere((0.0, 0.0, leg_h + 12.0), 16.0, pal.FEATHER_WHITE,
                   subdivisions=0, squash=0.6, jitter=0.08, seed=seed)
    # Folded grey wings along the flanks: the gull's whole silhouette.
    for sy in (-1.0, 1.0):
        mesh.box((-2.0, sy * 11.0, leg_h + 13.0), (30.0, 5.0, 9.0), pal.FEATHER_GREY,
                 roll=sy * 8.0)
        mesh.box((0.0, sy * 5.0, leg_h * 0.5), (3.5, 3.5, leg_h), pal.BEAK_ORANGE)
    mesh.box((12.0, 0.0, leg_h + 24.0), (11.0, 10.0, 11.0), pal.FEATHER_WHITE)
    mesh.box((21.0, 0.0, leg_h + 22.0), (10.0, 4.0, 4.0), pal.BEAK_ORANGE)
    mesh.box((-18.0, 0.0, leg_h + 14.0), (16.0, 8.0, 4.0), pal.FEATHER_WHITE, pitch=-14.0)
    return mesh
