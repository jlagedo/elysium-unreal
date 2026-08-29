"""Elysium GLB Review.

Opens the export_v2 corpus in Blender and reports on it. See `blender_manifest.toml`
for the extension metadata; this module holds registration and the one symbol the glTF
importer looks for by name.

The `core` subpackage imports no `bpy` and is exercised by a plain CPython test run, so
this module tolerates its absence rather than failing at import time.
"""

from __future__ import annotations

_needs_reload = "bpy" in locals()

try:
    import bpy
except ImportError:  # imported by the Blender-free test run
    bpy = None

if bpy is not None:
    from . import prefs
    from .adapters import hooks
    from .ui import browser, clips, operators, panels

    if _needs_reload:
        import importlib

        prefs = importlib.reload(prefs)
        hooks = importlib.reload(hooks)
        operators = importlib.reload(operators)
        browser = importlib.reload(browser)
        clips = importlib.reload(clips)
        panels = importlib.reload(panels)

    #: The glTF importer scans every enabled add-on for this exact name at module level.
    #: Without it the importer never learns the ELYSIUM extensions are handled, and every
    #: unit in the corpus fails its extensionsRequired check.
    glTF2ImportUserExtension = hooks.glTF2ImportUserExtension

    _CLASSES = (
        *prefs.CLASSES,
        *operators.CLASSES,
        *browser.CLASSES,
        *clips.CLASSES,
        *panels.CLASSES,
    )

    def register() -> None:
        for cls in _CLASSES:
            bpy.utils.register_class(cls)
        browser.register_properties()
        clips.register_properties()
        operators.register_menus()

    def unregister() -> None:
        operators.unregister_menus()
        clips.unregister_properties()
        browser.unregister_properties()
        for cls in reversed(_CLASSES):
            # Reinstalling reloads the module, so a class here may not be the one
            # Blender holds. Leaving the rest registered would be worse than skipping.
            try:
                bpy.utils.unregister_class(cls)
            except RuntimeError:
                pass
