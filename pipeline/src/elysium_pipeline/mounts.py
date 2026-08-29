"""The Unreal virtual mount roots every generator, bake and corpus module builds paths under.

A generator's own package (`/Game/ElysiumGenerated/Materials`, `/Game/ElysiumGenerated/Input`, ...)
and the `/ElysiumBaked` bake mount are contracts shared by several independent modules under
`pipeline/unreal/` and `elysium_pipeline`. This module holds each root once so every declaration
site derives from the same value instead of restating it.
"""
from __future__ import annotations

MATERIALS = "/Game/ElysiumGenerated/Materials"
AUDIO = "/Game/ElysiumGenerated/Audio"
UI_FONTS = "/Game/ElysiumGenerated/UI/Fonts"
INPUT = "/Game/ElysiumGenerated/Input"
BOOT_MAP = "/Game/ElysiumGenerated/Boot"
CAMERA = "/Game/ElysiumGenerated/Camera"
ANIMATION = "/Game/ElysiumGenerated/Animation"
BAKED = "/ElysiumBaked"
# Decoded from the user's own VtMB install (`mdl_cloth` sidecars), so it belongs with the rest of
# the baked corpus rather than the pre-cooked `Content/` packages above.
CLOTH = BAKED + "/Characters/Cloth"
