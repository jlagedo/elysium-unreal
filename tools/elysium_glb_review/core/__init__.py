"""Blender-free readers for the Elysium export_v2 GLB corpus.

Nothing under `core/` imports `bpy`. The package is importable by a plain CPython
interpreter so the parsing, resolution and reporting logic is unit-testable without
launching Blender.
"""
