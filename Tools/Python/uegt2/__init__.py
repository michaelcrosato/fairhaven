"""Fairhaven (UEGT2) editor-side content generation package.

Import order matters only in that `ctx` must be importable without a world.
Every module here runs inside UnrealEditor-Cmd via the Python plugin.
"""
__all__ = [
    "ctx", "palette", "materials", "meshkit", "meshbuild",
    "landscape", "water", "lighting", "town", "nature", "gameplay",
]
