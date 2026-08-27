"""Shared context for the Fairhaven content build.

Runs inside UnrealEditor-Cmd via the Python plugin. Everything here is small and
dependency-free (no numpy in Unreal's Python) so it stays easy to reason about.
"""
from __future__ import annotations

import array
import json
import os
import struct

import unreal

# --- Content roots ---------------------------------------------------------
ROOT = "/Game/Fairhaven"
MAPS = "/Game/Maps"
P_MATERIAL = ROOT + "/Materials"
P_MESH = ROOT + "/Meshes"
P_LANDSCAPE = ROOT + "/Landscape"
P_AUDIO = ROOT + "/Audio"
P_DATA = ROOT + "/Data"

MAP_NAME = "L_Fairhaven"
MAP_PATH = MAPS + "/" + MAP_NAME

_TAG = "[UEGT2]"


def log(message: str) -> None:
    unreal.log("%s %s" % (_TAG, message))


def warn(message: str) -> None:
    unreal.log_warning("%s %s" % (_TAG, message))


def fail(message: str) -> None:
    """Raise with a marker the build scripts grep for."""
    unreal.log_error("%s FAILED: %s" % (_TAG, message))
    raise RuntimeError(message)


# --- Paths -----------------------------------------------------------------
def project_dir() -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def terrain_output_dir() -> str:
    return os.path.join(project_dir(), "Tools", "Terrain", "Output")


def terrain_file(name: str) -> str:
    return os.path.join(terrain_output_dir(), name)


# --- Asset helpers ---------------------------------------------------------
def asset_exists(path: str) -> bool:
    return unreal.EditorAssetLibrary.does_asset_exist(path)


def load_asset(path: str):
    """Load a project or engine asset. Engine content is not always in the
    asset registry during a headless run, so fall back to a direct load."""
    if asset_exists(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    try:
        return unreal.load_asset(path)
    except Exception:                                           # noqa: BLE001
        return None


def save_asset(path: str) -> bool:
    try:
        return unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    except Exception as exc:                                    # noqa: BLE001
        warn("save_asset(%s) failed: %s" % (path, exc))
        return False


def save_all_dirty() -> None:
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)


def ensure_directory(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def unwrap(value):
    """GeometryScript nodes with ExpandEnumAsExecs return (result, outcome)."""
    if isinstance(value, tuple):
        return value[0]
    return value


_WARNED_PROPERTIES = set()


def set_prop(obj, name: str, value) -> bool:
    """Set an editor property, warning instead of aborting when it is missing.

    Engine property names drift between versions. Failing the whole content
    build over one renamed knob is worse than logging it and carrying on, so
    every warning here is a real TODO but never a blocker.
    """
    if obj is None:
        warn("set_prop on None: %s" % name)
        return False
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as exc:                                    # noqa: BLE001
        # Warn once per (type, property): a bad name inside a placement loop
        # would otherwise bury the log in thousands of identical lines.
        key = (type(obj).__name__, name)
        if key not in _WARNED_PROPERTIES:
            _WARNED_PROPERTIES.add(key)
            warn("property '%s' not set on %s (%s)"
                 % (name, type(obj).__name__, str(exc).split(":")[-1].strip()))
        return False


# --- World feature data ----------------------------------------------------
class WorldData(object):
    """Reads world_features.json plus the raw heightmap for placement queries."""

    def __init__(self):
        path = terrain_file("world_features.json")
        if not os.path.exists(path):
            fail("world_features.json not found at %s. Run Tools/Terrain/generate_terrain.py." % path)
        with open(path, "r", encoding="utf-8") as handle:
            self.data = json.load(handle)

        ls = self.data["landscape"]
        self.size = int(ls["size"])
        self.quad = float(ls["quad_uu"])
        self.z_scale = float(ls["z_scale"])
        self.origin = float(ls["origin_uu"])
        self.extent = float(ls["extent_uu"])
        self.layers = list(ls["layers"])
        self.seed = int(self.data["seed"])
        self.town = self.data["town"]
        self.roads = self.data["roads"]
        self.river = self.data["river"]
        self.ponds = self.data.get("ponds", [])
        self.lagoon = self.data["lagoon"]
        self.coast = self.data["coast"]

        self._heights = None
        self._weights = {}

    # -- raw sampling -------------------------------------------------------
    def _load_heights(self):
        if self._heights is None:
            path = terrain_file("heightmap.r16")
            if not os.path.exists(path):
                fail("heightmap.r16 not found at %s" % path)
            data = array.array("H")
            with open(path, "rb") as handle:
                data.fromfile(handle, self.size * self.size)
            if struct.pack("=H", 1) != struct.pack("<H", 1):
                data.byteswap()
            self._heights = data
        return self._heights

    def load_weight(self, layer: str):
        """uint8 weight grid for a paint layer, indexed [row * size + col]."""
        if layer not in self._weights:
            path = terrain_file("weight_%s.r8" % layer)
            if not os.path.exists(path):
                fail("weight_%s.r8 not found" % layer)
            data = array.array("B")
            with open(path, "rb") as handle:
                data.fromfile(handle, self.size * self.size)
            self._weights[layer] = data
        return self._weights[layer]

    def _grid_coords(self, wx: float, wy: float):
        fx = (wx - self.origin) / self.quad
        fy = (wy - self.origin) / self.quad
        fx = min(max(fx, 0.0), self.size - 1.001)
        fy = min(max(fy, 0.0), self.size - 1.001)
        return fx, fy

    def height_uu(self, wx: float, wy: float) -> float:
        """Bilinear terrain height in world Z (unreal units)."""
        heights = self._load_heights()
        fx, fy = self._grid_coords(wx, wy)
        x0, y0 = int(fx), int(fy)
        tx, ty = fx - x0, fy - y0
        s = self.size
        h00 = heights[y0 * s + x0]
        h10 = heights[y0 * s + x0 + 1]
        h01 = heights[(y0 + 1) * s + x0]
        h11 = heights[(y0 + 1) * s + x0 + 1]
        top = h00 + (h10 - h00) * tx
        bottom = h01 + (h11 - h01) * tx
        raw = top + (bottom - top) * ty
        return (raw - 32768.0) / 128.0 * self.z_scale

    def weight_at(self, layer: str, wx: float, wy: float) -> float:
        """Nearest-sample paint weight in 0..1."""
        grid = self.load_weight(layer)
        fx, fy = self._grid_coords(wx, wy)
        return grid[int(fy + 0.5) * self.size + int(fx + 0.5)] / 255.0

    def slope_deg(self, wx: float, wy: float) -> float:
        """Approximate slope in degrees from a small central difference."""
        step = self.quad
        import math
        dzdx = (self.height_uu(wx + step, wy) - self.height_uu(wx - step, wy)) / (2.0 * step)
        dzdy = (self.height_uu(wx, wy + step) - self.height_uu(wx, wy - step)) / (2.0 * step)
        return math.degrees(math.atan(math.hypot(dzdx, dzdy)))

    def in_bounds(self, wx: float, wy: float, margin: float = 0.0) -> bool:
        limit = self.extent - margin
        return -limit <= wx <= limit and -limit <= wy <= limit


# --- Deterministic RNG -----------------------------------------------------
class Rng(object):
    """Small xorshift PRNG so placement is reproducible across runs/platforms."""

    def __init__(self, seed: int):
        self.state = (seed ^ 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF or 0x2545F4914F6CDD1D

    def next_u64(self) -> int:
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
        x ^= x >> 7
        x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
        self.state = x
        return x

    def random(self) -> float:
        return (self.next_u64() >> 11) / float(1 << 53)

    def uniform(self, lo: float, hi: float) -> float:
        return lo + (hi - lo) * self.random()

    def randint(self, lo: int, hi: int) -> int:
        return lo + int(self.random() * (hi - lo + 1)) % (hi - lo + 1)

    def choice(self, items):
        return items[int(self.random() * len(items)) % len(items)]

    def chance(self, probability: float) -> bool:
        return self.random() < probability
