"""Identity and stable-name rules for the ui-resource GLB seam.

`docs/architecture/seam_map_ui_resource.md` owns the facts; this module is their code. A unit is
one VGUI2 `.res` layout/scheme, one VGUI1 dialog script, one HUD sprite table, one key-binding
table, the launcher/options scripts, the localized string table or the main-menu particle scene.
The key is the install-relative path *with its extension*, because `scripts/` mixes extensions and
eleven of its members have none.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import PurePosixPath
from typing import Any

from elysium_pipeline.formats.unit_contract import extension_name
from elysium_pipeline.formats.unit_contract import asset_id as _asset_id
from elysium_pipeline.formats.unit_contract.origin import SourceMember

KIND = "ui-resource"
KIND_TITLE = "Ui-resource"
SCHEMA_VERSION = "1.0.0"
UI_RESOURCE_EXTENSION = extension_name(KIND)

#: The output family directory every unit is written below.
FAMILY_DIR = "ui-resources"

#: The seven grammars a ui-resource unit may declare (`seam_map_ui_resource.md`, "GLB structure").
GRAMMARS = frozenset(
    {
        "keyvalues",
        "tab-rows",
        "titles",
        "settings-scr",
        "line-list",
        "won-lists",
        "key-value-lines",
    }
)

#: The typed-projection category a source member decodes to. Several categories share the
#: `keyvalues` grammar; the category picks which projection field the decode fills in.
CATEGORIES = frozenset(
    {
        "scheme",
        "layout",
        "menu",
        "hud",
        "titles",
        "rows",
        "substitutions",
        "strings",
        "options",
        "menuScene",
    }
)

#: The members read as UTF-16 LE with a byte-order mark; every other member is Latin-1
#: (`seam_map_ui_resource.md`, "Encoding").
UTF16_KEYS = frozenset({"resource/gameui_english.txt", "scripts/kb_trans.lst"})

#: `resource/*.res` scheme files, plus the one scheme below `scripts/`.
SCHEME_KEYS = frozenset(
    {
        "resource/vampirescheme.res",
        "resource/trackerscheme.res",
        "resource/trackerscheme-uhd.res",
        "resource/vampirece2scheme.res",
        "scripts/launcherscheme.res",
    }
)
MENU_KEYS = frozenset({"resource/gamemenu.res"})
HUD_KEYS = frozenset({"scripts/320_hud.txt", "scripts/640_hud.txt"})
TITLES_KEYS = frozenset({"scripts/titles.txt"})
SETTINGS_SCR_KEYS = frozenset({"scripts/settings.scr"})
SUBSTITUTION_KEYS = frozenset({"scripts/launcher.txt", "scripts/game.txt"})
STRINGS_KEYS = frozenset({"resource/gameui_english.txt"})
MENU_SCENE_KEYS = frozenset({"resource/mainmenuparticles.txt"})
TAB_ROW_KEYS = frozenset(
    {
        "scripts/kb_act.lst",
        "scripts/kb_act - hunter.lst",
        "scripts/kb_act - vampire.lst",
        "scripts/kb_def.lst",
        "scripts/kb_keys.lst",
        "scripts/kb_trans.lst",
    }
)
KEY_VALUE_LINE_KEYS = frozenset({"scripts/liblist.gam"})
LINE_LIST_KEYS = frozenset({"scripts/rooms.lst"})
WON_LIST_KEYS = frozenset({"scripts/woncomm.lst"})

#: The install-relative keys the source member table's "Live" column names as unloaded, keyed to
#: the owning document's own evidence (`seam_map_ui_resource.md`, "Source closure": "A unit whose
#: file the shipped binaries never read carries `identity.dormant: true` with the owning
#: document's evidence").
_DORMANT_EVIDENCE = {
    "resource/vampirece2scheme.res": (
        "seam_map_ui_resource.md's source member table: 'vampirece2scheme.res is not loaded'."
    ),
    "scripts/320_hud.txt": (
        "seam_map_ui_resource.md's source member table names "
        "'scripts/320_hud.txt, 640_hud.txt' Live as 'dormant'."
    ),
    "scripts/640_hud.txt": (
        "seam_map_ui_resource.md's source member table names "
        "'scripts/320_hud.txt, 640_hud.txt' Live as 'dormant'."
    ),
    "scripts/titles.txt": (
        "seam_map_ui_resource.md's source member table names 'scripts/titles.txt' Live as "
        "'dormant'."
    ),
    "scripts/settings.scr": (
        "seam_map_ui_resource.md's source member table names 'scripts/settings.scr' Live as "
        "'dormant'."
    ),
    "scripts/rooms.lst": (
        "seam_map_ui_resource.md's source member table names "
        "'scripts/rooms.lst, scripts/woncomm.lst' Live as 'dormant'."
    ),
    "scripts/woncomm.lst": (
        "seam_map_ui_resource.md's source member table names "
        "'scripts/rooms.lst, scripts/woncomm.lst' Live as 'dormant'."
    ),
}
_DORMANT_DIALOG_EVIDENCE = (
    "seam_map_ui_resource.md's source member table names 'scripts/dialog_*' Live as 'dormant'."
)


class UiResourceModelError(ValueError):
    """A name or identity does not fit the seam's rules."""


def normalize_key(raw: str) -> str:
    """The unit key: install-relative, lower case, forward-slashed, extension kept.

    The singular export command "tolerates the root prefix and the source extension on its
    argument" (`seam_map_unit_contract.md`): a caller may spell the family directory prefix or a
    trailing `.glb`, and both fold to the same key as the bare install-relative path.
    """

    if not raw:
        raise UiResourceModelError("a ui-resource key is empty")
    normalized = str(raw).strip().replace("\\", "/").lower()
    prefix = FAMILY_DIR + "/"
    if normalized.startswith(prefix):
        normalized = normalized[len(prefix):]
    if normalized.endswith(".glb"):
        normalized = normalized[: -len(".glb")]
    normalized = normalized.strip("/")
    if not normalized:
        raise UiResourceModelError(f"invalid ui-resource key {raw!r}")
    return normalized


def classify(key: str) -> str | None:
    """The typed-projection category for one normalized key, or `None` outside the seam."""

    if key in SCHEME_KEYS:
        return "scheme"
    if key in MENU_KEYS:
        return "menu"
    if key in HUD_KEYS:
        return "hud"
    if key in TITLES_KEYS:
        return "titles"
    if key in SETTINGS_SCR_KEYS:
        return "options"
    if key in SUBSTITUTION_KEYS:
        return "substitutions"
    if key in STRINGS_KEYS:
        return "strings"
    if key in MENU_SCENE_KEYS:
        return "menuScene"
    if key in TAB_ROW_KEYS:
        return "rows"
    if key in KEY_VALUE_LINE_KEYS:
        return "rows"
    if key in LINE_LIST_KEYS:
        return "rows"
    if key in WON_LIST_KEYS:
        return "rows"
    if key.startswith("resource/") and key.endswith(".res"):
        return "layout"
    if key.startswith("scripts/dialog_") and "." not in key.rsplit("/", 1)[-1]:
        return "layout"
    return None


def grammar_of(key: str) -> str | None:
    """The `grammar` field value for one normalized key, or `None` outside the seam."""

    if key in TITLES_KEYS:
        return "titles"
    if key in SETTINGS_SCR_KEYS:
        return "settings-scr"
    if key in TAB_ROW_KEYS:
        return "tab-rows"
    if key in KEY_VALUE_LINE_KEYS:
        return "key-value-lines"
    if key in LINE_LIST_KEYS:
        return "line-list"
    if key in WON_LIST_KEYS:
        return "won-lists"
    if classify(key) is not None:
        return "keyvalues"
    return None


def encoding_of(key: str) -> str:
    return "utf-16-le" if key in UTF16_KEYS else "latin-1"


def dormant_evidence(key: str) -> str | None:
    """The owning document's evidence for one dormant key, or `None` when the key is live."""

    if key in _DORMANT_EVIDENCE:
        return _DORMANT_EVIDENCE[key]
    if key.startswith("scripts/dialog_") and "." not in key.rsplit("/", 1)[-1]:
        return _DORMANT_DIALOG_EVIDENCE
    return None


def is_dormant(key: str) -> bool:
    return dormant_evidence(key) is not None


def asset_id(key: str) -> str:
    return _asset_id(KIND, key)


def output_relative_path(key: str) -> PurePosixPath:
    normalized = normalize_key(key)
    return PurePosixPath(FAMILY_DIR) / (normalized + ".glb")


@dataclass(slots=True)
class UiResourceModel:
    """The complete decode of one ui-resource source member."""

    key: str
    asset: str
    category: str
    grammar: str
    encoding: str
    member: SourceMember
    tree: dict[str, Any] | None
    scheme: dict[str, Any] | None
    layout: dict[str, Any] | None
    menu: dict[str, Any] | None
    hud: dict[str, Any] | None
    titles: dict[str, Any] | None
    rows: dict[str, Any] | None
    substitutions: dict[str, Any] | None
    strings: dict[str, Any] | None
    options: dict[str, Any] | None
    menu_scene: dict[str, Any] | None
    dependencies: list[dict[str, Any]]
    comments: list[dict[str, Any]]
    anomalies: list[dict[str, Any]]
    omissions: list[dict[str, Any]]
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    unresolved: list[dict[str, Any]] = field(default_factory=list)
    unsupported: list[dict[str, Any]] = field(default_factory=list)
    ledger_row: dict[str, Any] | None = None
    omitted_proven: list[dict[str, Any]] = field(default_factory=list)
    dormant: bool = False
    dormant_evidence: str | None = None
