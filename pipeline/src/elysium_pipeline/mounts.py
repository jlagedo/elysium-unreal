"""The Unreal virtual mount roots every generator, bake and corpus module builds paths under.

A generator's own package (`/Game/ElysiumGenerated/Materials`, `/Game/ElysiumGenerated/Input`, ...)
and the `/ElysiumBaked` bake mount are contracts shared by several independent modules under
`pipeline/unreal/` and `elysium_pipeline`. This module holds each root once so every declaration
site derives from the same value instead of restating it.
"""
from __future__ import annotations

MATERIALS = "/Game/ElysiumGenerated/Materials"
# The V2 material-import masters (SF-4.3), a package below MATERIALS rather than beside the
# legacy make_*_materials.py masters -- the two sets are never mixed on one asset.
MATERIALS_V2 = MATERIALS + "/V2"
AUDIO = "/Game/ElysiumGenerated/Audio"
UI_FONTS = "/Game/ElysiumGenerated/UI/Fonts"
INPUT = "/Game/ElysiumGenerated/Input"
BOOT_MAP = "/Game/ElysiumGenerated/Boot"
CAMERA = "/Game/ElysiumGenerated/Camera"
ANIMATION = "/Game/ElysiumGenerated/Animation"
# One `NS_<root>` per VtMB particle root, composed by `pipeline/unreal/make_root_systems.py` from
# an authored base emitter (`docs/project/niagara_authoring_strategy.md` 4.1). Generated, so
# gitignored: the readable artifact is the handful of authored modules under
# `Content/ElysiumAuthored/VFX/`, not the 1,698 systems built from them.
VFX = "/Game/ElysiumGenerated/VFX"
BAKED = "/ElysiumBaked"
# Decoded from the user's own VtMB install (`mdl_cloth` sidecars), so it belongs with the rest of
# the baked corpus rather than the pre-cooked `Content/` packages above.
CLOTH = BAKED + "/Characters/Cloth"
