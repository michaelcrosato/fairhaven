"""Fairhaven colour palette - the single source of truth for the art direction.

The look is "stylised low-poly objects in a believable natural world": clean,
slightly desaturated primaries on props and buildings, naturalistic ground and
foliage. Every mesh carries vertex colours and shares one master material, so
changing a colour here changes it everywhere and costs no extra draw calls.

Values are linear sRGB-ish floats in 0..1. Use ``rgb()`` for hex convenience.
"""
from __future__ import annotations


def rgb(hex_value: int, alpha: float = 1.0):
    """0xRRGGBB -> unreal.LinearColor, converting sRGB to linear."""
    import unreal
    r = ((hex_value >> 16) & 0xFF) / 255.0
    g = ((hex_value >> 8) & 0xFF) / 255.0
    b = (hex_value & 0xFF) / 255.0
    return unreal.LinearColor(_srgb_to_linear(r), _srgb_to_linear(g), _srgb_to_linear(b), alpha)


def _srgb_to_linear(c: float) -> float:
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


# --- Vegetation -------------------------------------------------------------
LEAF_TEMPERATE = 0x5C8C3A
LEAF_TEMPERATE_2 = 0x6FA046
LEAF_DARK = 0x3F6B2E
LEAF_AUTUMN = 0xC07A2A
LEAF_PINE = 0x35573A
LEAF_PALM = 0x4E9A54
LEAF_JUNGLE = 0x2F6B3B
LEAF_JUNGLE_2 = 0x407F45
BUSH_GREEN = 0x5A8340
GRASS_BLADE = 0x76A24C
REED_GREEN = 0x8AA355
CROP_WHEAT = 0xC9A94F
CROP_GREEN = 0x8FA84A

TRUNK_BROWN = 0x6B4B32
TRUNK_DARK = 0x4E3624
TRUNK_PALE = 0x8A7150
TRUNK_PALM = 0x8A6F4A

# --- Rock and ground --------------------------------------------------------
ROCK_GREY = 0x8C8A85
ROCK_DARK = 0x6E6C68
ROCK_WARM = 0x9B9086
CLIFF_GREY = 0x7E7C79
SNOW_WHITE = 0xEDF1F6
SAND_PALE = 0xDCC894
DIRT_BROWN = 0x8B7359
GRAVEL_GREY = 0x9A958C

# --- Landscape layer base colours (used by the landscape material) ----------
LANDSCAPE_COLOURS = {
    "Sand":   0xD8C48E,
    "Grass":  0x6B8A4C,
    "Farm":   0xA98B52,
    "Jungle": 0x3D7040,
    "Dirt":   0x7E6E58,
    "Rock":   0x8A8781,
    "Snow":   0xEEF2F7,
}
LANDSCAPE_ROUGHNESS = {
    "Sand": 0.92, "Grass": 0.94, "Farm": 0.95, "Jungle": 0.93,
    "Dirt": 0.95, "Rock": 0.82, "Snow": 0.70,
}

# --- Buildings --------------------------------------------------------------
WALL_CREAM = 0xE2D4B7
WALL_WHITE = 0xF0E9DC
WALL_OCHRE = 0xD9B072
WALL_TERRACOTTA = 0xC07A5B
WALL_SAGE = 0xA9B79A
WALL_BLUE = 0x8CA6BC
WALL_RED = 0xB55B4C
WALL_TIMBER = 0x8A6242
WALL_STONE = 0xB4ADA0

ROOF_RED = 0xB1503C
ROOF_SLATE = 0x53606B
ROOF_TEAL = 0x3E7A76
ROOF_BROWN = 0x77503A
ROOF_THATCH = 0xC0A059
ROOF_BLUE = 0x466A8C

DOOR_BLUE = 0x38607F
DOOR_RED = 0x9E4436
DOOR_GREEN = 0x3F6B4E
DOOR_WOOD = 0x7A5638
WINDOW_GLASS = 0x9FC4D6
WINDOW_FRAME = 0xF2ECDF
TRIM_WHITE = 0xF3EDE1

# --- Props and materials ----------------------------------------------------
WOOD_PLANK = 0x9C7648
WOOD_DARK = 0x6D4F32
WOOD_PALE = 0xC0A176
METAL_IRON = 0x5A5F63
METAL_RUST = 0x8B5A3C
METAL_COPPER = 0x7F9E86
CLOTH_RED = 0xB6503F
CLOTH_BLUE = 0x40688E
CLOTH_CREAM = 0xE6DAC0
CLOTH_GREEN = 0x4F7A50
CLOTH_YELLOW = 0xDCB055
LAMP_GLASS = 0xFFE9B0
# --- Newhaven (the city) ----------------------------------------------------
# Cooler and greyer than the town on purpose: Fairhaven is warm render and
# painted timber, Newhaven is concrete, glass and tar. Putting them side by side
# is what makes the drive south read as going somewhere else.
CONCRETE_PALE = 0xCFC9BE
CONCRETE_GREY = 0xA8A49C
CONCRETE_DARK = 0x7C7973
GLASS_BLUE = 0x6E93AE
GLASS_TEAL = 0x5F9698
GLASS_DARK = 0x445C6E
CURTAIN_WALL = 0x8FA8B8
FACADE_SAND = 0xC8B394
FACADE_BRICK = 0x9C6350
FACADE_TERRA = 0xB0705A
ROOF_TAR = 0x4A4A4C
ASPHALT = 0x53535A
KERB_GREY = 0xB8B4AC
NEON_SIGN = 0xE86A4C
AWNING_RED = 0xB4483C
AWNING_GREEN = 0x3F7A5C

STONE_PALE = 0xC3BBA9
STONE_DARK = 0x8D877A
BRICK_RED = 0xA35A48
ROPE_TAN = 0xBFA073
CANVAS_WHITE = 0xEDE6D4
PAPER_CREAM = 0xF0E6CE

# --- Water (used when the stylised fallback water is active) ----------------
WATER_OCEAN = 0x1E5F7A
WATER_SHALLOW = 0x3E93A8
WATER_RIVER = 0x2E7286
WATER_POND = 0x37707A

# --- Character (simple blocky figures) --------------------------------------
SKIN_TONES = [0xF2CDA7, 0xE0B183, 0xC28E63, 0x8D5A3C, 0x63412C]
SHIRT_COLOURS = [0xB6503F, 0x40688E, 0x4F7A50, 0xDCB055, 0x7A5C8E, 0xE6DAC0]
TROUSER_COLOURS = [0x3B4A5C, 0x5C4A38, 0x44523F, 0x6B5B4A, 0x2F3742]


def leaf_set(biome: str):
    """Leaf colour choices for a biome, used by the tree generators."""
    return {
        "temperate": [LEAF_TEMPERATE, LEAF_TEMPERATE_2, LEAF_DARK, LEAF_AUTUMN],
        "pine": [LEAF_PINE, LEAF_DARK, LEAF_TEMPERATE],
        "jungle": [LEAF_JUNGLE, LEAF_JUNGLE_2, LEAF_PALM],
        "palm": [LEAF_PALM, LEAF_JUNGLE_2],
    }.get(biome, [LEAF_TEMPERATE])
