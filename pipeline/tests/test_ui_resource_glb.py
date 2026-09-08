"""Synthetic contract tests for the isolated ui-resource GLB exporter.

Every fixture below is hand-built bytes plus a fake install index and a `read_bytes` lambda that
serves them -- never the real VtMB install, per the seam's own test-authoring rule.
"""

from __future__ import annotations

import copy

import pytest

from elysium_pipeline.exporters import ui_resource_glb as exporter
from elysium_pipeline.formats.unit_contract import ByteLedgerError, UnitValidationError
from elysium_pipeline.formats.ui_resource_glb import lexer, model as model_module
from elysium_pipeline.formats.ui_resource_glb.decode import Resolvers, decode_ui_resource
from elysium_pipeline.formats.ui_resource_glb.source import (
    UiResourceSourceError,
    load_source_closure,
    source_keys,
)
from elysium_pipeline.validation import ui_resource_glb as validation


def _index(*keys: str) -> dict:
    return {key: ("loose", f"C:/game/Vampire/{key}") for key in keys}


def _closure(key: str, body: bytes, *, index_extra: dict[str, bytes] | None = None):
    bodies = {key: body, **(index_extra or {})}
    index = _index(*bodies)
    return load_source_closure(
        index, key, read_bytes=lambda idx, k: bodies.get(k)
    )


def _decode(key: str, body: bytes, *, index_extra=None, resolvers: Resolvers | None = None):
    return decode_ui_resource(_closure(key, body, index_extra=index_extra), resolvers=resolvers)


def _ledger_ranges(model) -> list[dict]:
    return model.ledger_row["ranges"]


def _assert_gapless(model, byte_length: int) -> None:
    ranges = sorted(_ledger_ranges(model), key=lambda row: row["offset"])
    cursor = 0
    for row in ranges:
        assert row["offset"] == cursor, model.key
        cursor += row["length"]
    assert cursor == byte_length == model.ledger_row["byteLength"]
    assert model.ledger_row["coveragePercent"] == 100.0


# --- identity / key rule -------------------------------------------------------------------


def test_normalize_key_tolerates_the_family_prefix_and_a_trailing_glb_extension():
    assert model_module.normalize_key("resource/vampirescheme.res") == "resource/vampirescheme.res"
    assert (
        model_module.normalize_key("ui-resources/resource/vampirescheme.res.glb")
        == "resource/vampirescheme.res"
    )
    assert model_module.normalize_key("SCRIPTS/DIALOG_MAIN") == "scripts/dialog_main"


def test_asset_id_keeps_the_extension_and_the_output_path_sits_below_the_family_dir():
    assert model_module.asset_id("resource/vampirescheme.res") == "vtmb:ui-resource:resource/vampirescheme.res"
    assert model_module.asset_id("scripts/dialog_main") == "vtmb:ui-resource:scripts/dialog_main"
    assert (
        model_module.output_relative_path("resource/vampirescheme.res").as_posix()
        == "ui-resources/resource/vampirescheme.res.glb"
    )
    assert (
        model_module.output_relative_path("scripts/dialog_main").as_posix()
        == "ui-resources/scripts/dialog_main.glb"
    )


def test_classify_excludes_authoring_residue_and_other_seams_files():
    assert model_module.classify("scripts/liblist.gam~") is None
    assert model_module.classify("materials/fonts/fontlist.txt") is None
    assert model_module.classify("scripts/surfaceproperties.txt") is None
    assert model_module.classify("scripts/liblist.gam") == "rows"
    assert model_module.classify("resource/vampirescheme.res") == "scheme"
    assert model_module.classify("resource/confirmdialog.res") == "layout"
    assert model_module.classify("scripts/dialog_main") == "layout"
    assert model_module.classify("scripts/dialog_main.dsp") is None


def test_source_keys_enumerates_only_this_seams_members_from_a_mixed_index():
    index = _index(
        "resource/vampirescheme.res",
        "resource/gamemenu.res",
        "scripts/dialog_main",
        "scripts/liblist.gam",
        "scripts/liblist.gam~",
        "scripts/surfaceproperties.txt",
        "materials/fonts/fontlist.txt",
        "vdata/items/foo.txt",
    )
    assert source_keys(index) == sorted(
        {"resource/vampirescheme.res", "resource/gamemenu.res", "scripts/dialog_main",
         "scripts/liblist.gam"}
    )


def test_load_source_closure_raises_for_a_key_outside_the_seam():
    index = _index("scripts/surfaceproperties.txt")
    with pytest.raises(UiResourceSourceError):
        load_source_closure(
            index, "scripts/surfaceproperties.txt", read_bytes=lambda idx, k: b"x"
        )


# --- keyvalues tokenizer -----------------------------------------------------------------------


def test_the_tokenizer_claims_every_byte_of_a_keyvalues_file_exactly_once():
    body = b'Foo\r\n{\r\n\t"a"\t"1" // trailing\r\n}\r\n'
    text = lexer.decode_text(body, "latin-1")
    tokens = lexer.tokenize(text, "latin-1")
    cursor = 0
    for token in tokens:
        assert token.offset == cursor
        cursor += token.length
    assert cursor == len(body)


def test_a_literal_backslash_key_does_not_escape_the_closing_quote_in_row_grammars():
    # `scripts/kb_keys.lst` names the backslash key as `"\"`: with Source KeyValues' `\"` escape
    # convention this reads as an escaped quote, corrupting every cell after it. The row grammars
    # disable that escape (`lexer.tokenize(..., escape_quotes=False)`).
    body = '92\t"\\" "\\" DEFAULTCOLOR\r\n93\t"]" "]" DEFAULTCOLOR\r\n'.encode("latin-1")
    model = _decode("scripts/kb_keys.lst", body)
    _assert_gapless(model, len(body))
    entries = model.rows["entries"]
    assert entries[0]["keynum"] == 92
    assert entries[0]["name1"] == "\\"
    assert entries[0]["name2"] == "\\"
    assert entries[0]["colorToken"] == "DEFAULTCOLOR"
    assert entries[1]["name1"] == "]"


def test_kb_trans_utf16_rows_carry_byte_offsets_and_are_not_scrambled_by_the_bom():
    # The same two rows as the kb_keys.lst test above, UTF-16 LE with a BOM this time: proves
    # `decode_row_grammar` scales its line spans to source bytes rather than mixing them with the
    # tokenizer's own byte offsets.
    text = '92\t"\\" "\\" DEFAULTCOLOR\r\n93\t"]" "]" DEFAULTCOLOR\r\n'
    body = b"\xff\xfe" + text.encode("utf-16-le")
    model = _decode("scripts/kb_trans.lst", body)
    _assert_gapless(model, len(body))
    entries = model.rows["entries"]
    assert len(entries) == 2
    assert entries[0]["keynum"] == 92
    assert entries[0]["name1"] == "\\"
    assert entries[0]["name2"] == "\\"
    assert entries[0]["colorToken"] == "DEFAULTCOLOR"
    assert entries[1]["keynum"] == 93
    assert entries[1]["name1"] == "]"
    for entry in entries:
        assert entry["offset"] % 2 == 0
        for cell in entry["cells"]:
            assert cell["offset"] % 2 == 0


# --- scheme --------------------------------------------------------------------------------


_SCHEME_BODY = b"""Scheme
{
\tColors
\t{
\t\t"BaseText"\t"216 222 211 255"
\t}
\tBaseSettings
\t{
\t\t"FgColor"\t"BaseText"
\t}
\tFonts
\t{
\t\t"Default"
\t\t{
\t\t\t"1"
\t\t\t{
\t\t\t\t"name"\t"Tahoma"
\t\t\t\t"tall"\t"16"
\t\t\t\t"weight"\t"500"
\t\t\t}
\t\t}
\t}
\tBorders
\t{
\t\tBaseBorder
\t\t{
\t\t\t"inset"\t"0 0 1 1"
\t\t}
\t}
}
"""


def test_scheme_projects_colors_basesettings_resolved_and_a_font_dependency():
    model = _decode("resource/vampirescheme.res", _SCHEME_BODY)
    _assert_gapless(model, len(_SCHEME_BODY))
    assert model.scheme["colors"][0] == {
        "name": "BaseText", "raw": "216 222 211 255", "rgba": [216, 222, 211, 255],
        "offset": model.scheme["colors"][0]["offset"],
    }
    resolved_to = model.scheme["baseSettings"][0]["resolvesTo"]
    assert resolved_to["name"] == "BaseText"
    font_deps = [d for d in model.dependencies if d["role"] == "font"]
    assert len(font_deps) == 1
    # The identity is the font seam's own key rule (`<face>_<size>_<weight>_<flags>`), so it can
    # name a real `vtmb:font:` unit; a colon-joined tier spelling never could.
    assert font_deps[0]["asset"] == "vtmb:font:tahoma_16_500_000"
    assert font_deps[0]["sourcePath"] == "materials/fonts/tahoma_16_500_000.fnt"
    assert font_deps[0]["resolved"] is False
    assert model.scheme["fonts"][0]["tiers"][0]["joinKey"] == "tahoma:16:500:0"
    assert model.scheme["borders"][0]["name"] == "BaseBorder"


def test_scheme_font_dependency_resolves_when_a_resolver_says_so():
    model = _decode(
        "resource/vampirescheme.res", _SCHEME_BODY,
        resolvers=Resolvers(font_exists=lambda key: key == "tahoma_16_500_000"),
    )
    font_deps = [d for d in model.dependencies if d["role"] == "font"]
    assert font_deps[0]["resolved"] is True


_SYMBOL_SCHEME_BODY = b"""Scheme
{
	Fonts
	{
		"Marlett"
		{
			"1"
			{
				"name"	"Marlett"
				"tall"	"8"
				"weight"	"0"
				"symbol"	"1"
			}
		}
		"DefaultUnderline"
		{
			"1"
			{
				"name"	"Tahoma"
				"tall"	"10"
				"weight"	"500"
				"underline"	"1"
			}
		}
	}
}
"""


def test_scheme_font_flags_count_in_vguis_own_fontflag_bits():
    """`symbol` is 0x8 and `underline` is 0x2, the bits the shipped `.fnt` stems spell:
    `marlett_08_000_008`, `tahoma_10_500_002`."""

    model = _decode("resource/vampirescheme.res", _SYMBOL_SCHEME_BODY)
    assets = [d["asset"] for d in model.dependencies if d["role"] == "font"]
    assert assets == ["vtmb:font:marlett_08_000_008", "vtmb:font:tahoma_10_500_002"]


_UNKEYABLE_SCHEME_BODY = b"""Scheme
{
	Fonts
	{
		"Default"
		{
			"1"
			{
				"name"	"Tahoma"
				"tall"	"large"
				"weight"	"500"
			}
		}
	}
}
"""


def test_a_font_tier_the_font_seams_key_rule_cannot_compose_keeps_a_missing_sentinel():
    model = _decode("resource/vampirescheme.res", _UNKEYABLE_SCHEME_BODY)
    tier = model.scheme["fonts"][0]["tiers"][0]
    assert tier["asset"] == "vtmb:missing-font:tahoma:large:500:0"
    assert tier["resolved"] is False
    assert [d for d in model.dependencies if d["role"] == "font"] == []
    assert {"role": "fonts", "reason": "unkeyable-font-tier",
            "joinKey": "tahoma:large:500:0"} in model.omitted_proven


# --- layout ----------------------------------------------------------------------------------


_LAYOUT_BODY = b"""\"Resource\\ConfirmDialog.res\"
{
\t\"OK\"
\t{
\t\t\"ControlName\"\t\t\"Button\"
\t\t\"fieldName\"\t\t\"OK\"
\t\t\"xpos\"\t\t\"330\"
\t\t\"ypos\"\t\t\"162\"
\t\t\"wide\"\t\t\"90\"
\t\t\"tall\"\t\t\"24\"
\t\t\"labelText\"\t\t\"#GameUI_OK\"
\t\t\"material\"\t\t\"launcher/background\"
\t}
}
"""


def test_layout_projects_a_control_with_position_size_and_a_material_dependency():
    model = _decode("resource/confirmdialog.res", _LAYOUT_BODY)
    _assert_gapless(model, len(_LAYOUT_BODY))
    control = model.layout["controls"][0]
    assert control["fieldName"] == "OK"
    assert control["controlName"] == "Button"
    assert control["position"] == {"x": 330, "y": 162}
    assert control["size"] == {"wide": 90, "tall": 24}
    material_deps = [d for d in model.dependencies if d["role"] == "material"]
    assert material_deps == [
        {"role": "material", "asset": "vtmb:material:launcher/background",
         "sourcePath": "launcher/background", "resolved": False}
    ]


_VMT_SPELLED_LAYOUT_BODY = b"""\"Resource/Spelled.res\"
{
	\"Panel\"
	{
		\"image\"		\"materials//vgui/hud/Foo.vmt\"
	}
}
"""


def test_a_material_value_is_keyed_by_the_material_seams_own_rule():
    """`materials//vgui/hud/Foo.vmt` and a bare `vgui/hud/foo` name one `vtmb:material:` unit:
    the material seam's `normalize_material_path` collapses the doubled slash and drops `.vmt`.
    """

    model = _decode("resource/spelled.res", _VMT_SPELLED_LAYOUT_BODY)
    material_deps = [d for d in model.dependencies if d["role"] == "material"]
    assert [d["asset"] for d in material_deps] == ["vtmb:material:vgui/hud/foo"]
    # `sourcePath` keeps the referrer's authored spelling.
    assert material_deps[0]["sourcePath"] == "materials//vgui/hud/Foo.vmt"


_UNKEYABLE_MATERIAL_BODY = b"""\"Resource/Escape.res\"
{
	\"Panel\"
	{
		\"image\"	\"materials/../foo\"
	}
}
"""


def test_a_material_value_the_material_seam_cannot_key_produces_no_row():
    """`materials/../foo` escapes the material root, so the material seam's key rule rejects it;
    a value that seam can never own names no unit and publishes no dependency row."""

    model = _decode("resource/escape.res", _UNKEYABLE_MATERIAL_BODY)
    assert [d for d in model.dependencies if d["role"] == "material"] == []

def test_a_vmt_spelled_material_value_resolves_through_the_exporters_own_resolver(tmp_path):
    index = _index("resource/spelled.res", "materials/vgui/hud/foo.vmt")
    path = exporter.export(
        index, "resource/spelled.res", tmp_path,
        read_bytes=lambda idx, k: _VMT_SPELLED_LAYOUT_BODY if k == "resource/spelled.res" else None,
    )
    root = validation.read_glb(path)[0]["extensions"]["ELYSIUM_vtmb_ui_resource"]
    material_deps = [d for d in root["dependencies"] if d["role"] == "material"]
    assert material_deps[0]["resolved"] is True


def test_layout_control_label_token_produces_a_ui_resource_dependency_on_the_string_table():
    model = _decode("resource/confirmdialog.res", _LAYOUT_BODY)
    ui_deps = [d for d in model.dependencies if d["role"] == "ui-resource"]
    assert ui_deps == [
        {"role": "ui-resource", "asset": "vtmb:ui-resource:resource/gameui_english.txt",
         "sourcePath": "#GameUI_OK", "resolved": False}
    ]


def test_layout_control_nests_child_controls_arbitrarily_deep():
    body = (
        b'"wrapper"\n{\n\t"outer"\n\t{\n\t\t"ControlName" "Panel"\n\t\t"inner"\n\t\t{\n\t\t\t'
        b'"ControlName" "Label"\n\t\t}\n\t}\n}\n'
    )
    model = _decode("resource/dialogoptionsingame.res", body)
    _assert_gapless(model, len(body))
    outer = model.layout["controls"][0]
    assert outer["controlName"] == "Panel"
    assert outer["children"][0]["controlName"] == "Label"


# --- menu ------------------------------------------------------------------------------------


_MENU_BODY = b"""\"GameMenu\"
{
\t\"1\"
\t{
\t\t\"label\"\t\"#GameUI_GameMenu_NewGame\"
\t\t\"command\"\t\"OpenNewGameDialog\"
\t}
\t\"2\"
\t{
\t\t\"label\"\t\"#GameUI_GameMenu_Multiplayer\"
\t\t\"SubMenu\"
\t\t{
\t\t\t\"1\"
\t\t\t{
\t\t\t\t\"label\"\t\"#GameUI_GameMenu_FindServers\"
\t\t\t\t\"command\"\t\"OpenServerBrowser\"
\t\t\t}
\t\t}
\t}
}
"""


def test_menu_projects_items_and_a_submenu_with_gameui_token_dependencies():
    model = _decode("resource/gamemenu.res", _MENU_BODY)
    _assert_gapless(model, len(_MENU_BODY))
    items = model.menu["items"]
    assert items[0]["command"] == "OpenNewGameDialog"
    assert items[1]["subMenu"][0]["command"] == "OpenServerBrowser"
    ui_deps = {d["sourcePath"] for d in model.dependencies if d["role"] == "ui-resource"}
    assert "#GameUI_GameMenu_FindServers" in ui_deps


def test_menu_gameui_token_resolves_against_the_string_table_when_a_resolver_is_given():
    model = _decode(
        "resource/gamemenu.res", _MENU_BODY,
        resolvers=Resolvers(gameui_token_exists=lambda name: name == "GameUI_GameMenu_NewGame"),
    )
    by_path = {d["sourcePath"]: d for d in model.dependencies if d["role"] == "ui-resource"}
    assert by_path["#GameUI_GameMenu_NewGame"]["resolved"] is True
    assert by_path["#GameUI_GameMenu_Multiplayer"]["resolved"] is False


# --- hud -------------------------------------------------------------------------------------


_HUD_BODY = b"""\"320_hud\"
{
\tSpriteData
\t{
\t\t\"selection\"
\t\t{
\t\t\t\"320\"
\t\t\t{
\t\t\t\t\"file\"\t\t\"320hud3\"
\t\t\t\t\"x\"\t\t\t\"0\"
\t\t\t\t\"y\"\t\t\t\"180\"
\t\t\t\t\"width\"\t\t\"170\"
\t\t\t\t\"height\"\t\"45\"
\t\t\t}
\t\t}
\t}
}
"""


def test_hud_projects_a_sprite_with_its_rectangle_and_a_material_dependency():
    model = _decode("scripts/320_hud.txt", _HUD_BODY)
    _assert_gapless(model, len(_HUD_BODY))
    sprite = model.hud["sprites"][0]
    assert sprite["name"] == "selection"
    assert sprite["resolution"] == "320"
    assert sprite["file"] == "320hud3"
    assert sprite["x"] == 0 and sprite["y"] == 180
    assert sprite["width"] == 170 and sprite["height"] == 45
    material_deps = [d for d in model.dependencies if d["role"] == "material"]
    assert material_deps[0]["asset"] == "vtmb:material:320hud3"


_HUD_REPEATED_BODY = b"""\"320_hud\"
{
\tSpriteData
\t{
\t\t\"selection\"
\t\t{
\t\t\t\"320\"
\t\t\t{
\t\t\t\t\"file\"\t\t\"320hud3\"
\t\t\t\t\"x\"\t\t\t\"0\"
\t\t\t\t\"y\"\t\t\t\"180\"
\t\t\t\t\"width\"\t\t\"170\"
\t\t\t\t\"height\"\t\"45\"
\t\t\t}
\t\t\t\"640\"
\t\t\t{
\t\t\t\t\"file\"\t\t\"320hud3\"
\t\t\t\t\"x\"\t\t\t\"0\"
\t\t\t\t\"y\"\t\t\t\"180\"
\t\t\t\t\"width\"\t\t\"170\"
\t\t\t\t\"height\"\t\"45\"
\t\t\t}
\t\t}
\t}
}
"""


def test_a_reference_named_more_than_once_publishes_one_dependency_row():
    # `file "320hud3"` appears twice: once for `_scan_material_and_token_references`'s whole-tree
    # scan, and would once more for `_project_hud`'s own sprite loop if that projection still
    # appended its own row for the same reference.
    model = _decode("scripts/320_hud.txt", _HUD_REPEATED_BODY)
    _assert_gapless(model, len(_HUD_REPEATED_BODY))
    material_deps = [d for d in model.dependencies if d["role"] == "material"]
    assert len(material_deps) == 1
    assert material_deps[0]["asset"] == "vtmb:material:320hud3"
    assert len(model.hud["sprites"]) == 2


# --- menuScene ---------------------------------------------------------------------------------


_MENU_SCENE_BODY = b"""MainMenuParticles
{
\tcamera_fov\t\t\"50\"
\tdefault_skybox\t\t\"MM_Skybox\"
\tmusic\t\t\t\"music/Vampire_Theme.mp3\"

\tParticle
\t{
\t\temitter\t\t\"M_Clouds_Emmiter\"
\t\torigin\t\t\"[0,0,-30]\"
\t\tangle\t\t\"[0,0,0]\"
\t}
}
"""


def test_menu_scene_projects_skybox_music_and_particle_dependencies():
    model = _decode("resource/mainmenuparticles.txt", _MENU_SCENE_BODY)
    _assert_gapless(model, len(_MENU_SCENE_BODY))
    scene = model.menu_scene
    assert scene["defaultSkybox"] == "MM_Skybox"
    assert len(scene["skyboxFaces"]) == 6
    assert scene["music"]["path"] == "music/Vampire_Theme.mp3"
    assert scene["particles"][0]["emitter"] == "M_Clouds_Emmiter"

    roles = {d["role"] for d in model.dependencies}
    assert {"texture", "sound", "particle"} <= roles
    texture_deps = [d for d in model.dependencies if d["role"] == "texture"]
    assert len(texture_deps) == 6
    sound_deps = [d for d in model.dependencies if d["role"] == "sound"]
    assert sound_deps[0]["asset"] == "vtmb:sound:music/vampire_theme.mp3"
    particle_deps = [d for d in model.dependencies if d["role"] == "particle"]
    assert particle_deps[0]["asset"] == "vtmb:particle:m_clouds_emmiter"


def test_menu_scene_dependencies_resolve_when_resolvers_say_so():
    model = _decode(
        "resource/mainmenuparticles.txt", _MENU_SCENE_BODY,
        resolvers=Resolvers(
            texture_exists=lambda p: True, sound_exists=lambda p: True, particle_exists=lambda p: True
        ),
    )
    assert all(d["resolved"] for d in model.dependencies)


# --- #include / #base -----------------------------------------------------------------------


def test_an_include_directive_produces_a_ui_resource_dependency():
    body = b'#include "scripts/game.txt"\n\n"loading"\t"Loading $game..."\n'
    model = _decode(
        "scripts/launcher.txt", body,
        index_extra={"scripts/game.txt": b'"$game"\t"Vampire"\n'},
        resolvers=Resolvers(ui_resource_exists=lambda p: p == "scripts/game.txt"),
    )
    _assert_gapless(model, len(body))
    include_deps = [d for d in model.dependencies if d["role"] == "ui-resource"
                    and d["sourcePath"] == "scripts/game.txt"]
    assert include_deps == [
        {"role": "ui-resource", "asset": "vtmb:ui-resource:scripts/game.txt",
         "sourcePath": "scripts/game.txt", "resolved": True}
    ]
    assert model.unresolved == []


def test_a_missing_include_target_is_the_units_own_structure_and_enters_unresolved():
    body = b'#include "scripts/missing.txt"\n\n"loading"\t"Loading..."\n'
    model = _decode(
        "scripts/launcher.txt", body,
        resolvers=Resolvers(ui_resource_exists=lambda p: False),
    )
    assert model.unresolved
    assert model.unresolved[0]["target"] == "scripts/missing.txt"


def test_an_unrecognized_directive_is_carried_as_typed_unidentified():
    body = b'#weird "argument"\n\n"a"\t"b"\n'
    model = _decode("scripts/launcher.txt", body)
    _assert_gapless(model, len(body))
    reasons = {row.get("reason") for row in model.typed_unidentified}
    assert "unrecognized-directive" in reasons


# --- substitutions -----------------------------------------------------------------------------


def test_substitutions_apply_dollar_definitions_from_an_included_file():
    body = b'#include "scripts/game.txt"\n\n"loading"\t"Loading $game..."\n'
    model = _decode(
        "scripts/launcher.txt", body,
        index_extra={"scripts/game.txt": b'"$game"\t"Vampire"\n'},
        resolvers=Resolvers(substitution_defs=lambda p: {"$game": "Vampire"} if p == "scripts/game.txt" else None),
    )
    strings = model.substitutions["strings"]
    assert strings[0]["raw"] == "Loading $game..."
    assert strings[0]["substituted"] == "Loading Vampire..."


def test_substitutions_capture_local_dollar_definitions_in_file_order():
    body = b'"$game"\t"Vampire"\n'
    model = _decode("scripts/game.txt", body)
    _assert_gapless(model, len(body))
    assert model.substitutions["definitions"] == [{"key": "$game", "value": "Vampire"}]
    assert model.substitutions["strings"] == []


# --- strings (UTF-16 LE) -----------------------------------------------------------------------


def test_strings_projects_utf16_tokens_with_code_unit_offsets_and_a_repeated_key_anomaly():
    text = (
        '"lang"\r\n{\r\n"Language" "English"\r\n"Tokens"\r\n{\r\n'
        '"GameUI_OK" "OK"\r\n"GameUI_OK" "Okay"\r\n}\r\n}\r\n'
    )
    body = b"\xff\xfe" + text.encode("utf-16-le")
    model = _decode("resource/gameui_english.txt", body)
    _assert_gapless(model, len(body))
    assert model.encoding == "utf-16-le"
    tokens = model.strings["tokens"]
    names = [t["name"] for t in tokens]
    assert names.count("GameUI_OK") == 2
    for token in tokens:
        assert token["byteOffset"] % 2 == 0
        assert token["byteLength"] == token["codeUnitLength"] * 2
        assert token["byteOffset"] == token["codeUnitOffset"] * 2
    anomaly_roles = {row["role"] for row in model.anomalies}
    assert "repeated-key" in anomaly_roles


def test_a_utf16_tokens_escaped_quote_decodes_to_a_literal_quote():
    # `Token.escapes` used to be scaled by `unit_width` (2 for UTF-16) and then indexed into
    # `raw`, a character string -- decoding this the wrong way corrupts the string past the
    # escape.
    text = (
        '"lang"\r\n{\r\n"Language" "English"\r\n"Tokens"\r\n{\r\n'
        '"GameUI_Quote" "say \\"hi\\" now"\r\n}\r\n}\r\n'
    )
    body = b"\xff\xfe" + text.encode("utf-16-le")
    model = _decode("resource/gameui_english.txt", body)
    _assert_gapless(model, len(body))
    tokens = {t["name"]: t["text"] for t in model.strings["tokens"]}
    assert tokens["GameUI_Quote"] == 'say "hi" now'


# --- titles ------------------------------------------------------------------------------------


_TITLES_BODY = b"""// header comment
$position -1 -1
$effect 2
$color 100 100 100
$holdtime 3.5

GAMEOVER
{
GAMEOVER TEST
}

$effect0
GAMESAVED
{
Saved...
}
"""


def test_titles_projects_directives_and_a_captions_directive_state_in_force():
    model = _decode("scripts/titles.txt", _TITLES_BODY)
    _assert_gapless(model, len(_TITLES_BODY))
    directives = model.titles["directives"]
    assert [row["name"] for row in directives[:4]] == ["position", "effect", "color", "holdtime"]
    captions = model.titles["captions"]
    first = next(c for c in captions if c["name"] == "GAMEOVER")
    assert first["text"] == "GAMEOVER TEST"
    assert first["directives"]["holdtime"]["args"] == "3.5"
    assert first["directives"]["effect"]["args"] == "2"


def test_titles_unknown_directive_is_typed_unidentified_not_a_failure():
    model = _decode("scripts/titles.txt", _TITLES_BODY)
    reasons = [row for row in model.typed_unidentified if row.get("name") == "effect0"]
    assert reasons and reasons[0]["reason"] == "unrecognized-directive"
    assert model.unresolved == []
    assert model.unsupported == []


# --- settings-scr --------------------------------------------------------------------------


_SETTINGS_BODY = b"""VERSION 1.0

DESCRIPTION SERVER_OPTIONS
{
\t"mp_teamplay"
\t{
\t\t"Teamplay"
\t\t{ BOOL }
\t\t{ "0" }
\t}
\t"mp_fraglimit"
\t{
\t\t"Frag Limit"
\t\t{ NUMBER 0.000000 -1.000000 }
\t\t{ "0.000000" }
\t}
\t"mp_falldamage"
\t{
\t\t"Falling Damage"
\t\t{
\t\t\tLIST
\t\t\t"Normal" "0"
\t\t\t"Realistic" "1"
\t\t}
\t\t{ "0" }
\t}
}
"""


def test_settings_scr_projects_bool_number_and_list_option_types():
    model = _decode("scripts/settings.scr", _SETTINGS_BODY)
    _assert_gapless(model, len(_SETTINGS_BODY))
    assert model.options["version"] == "1.0"
    options = model.options["sections"][0]["options"]
    by_cvar = {row["cvar"]: row for row in options}
    assert by_cvar["mp_teamplay"]["type"] == "BOOL"
    assert by_cvar["mp_fraglimit"]["type"] == "NUMBER"
    assert by_cvar["mp_fraglimit"]["typeInfo"]["min"] == 0.0
    assert by_cvar["mp_fraglimit"]["typeInfo"]["max"] == -1.0
    assert by_cvar["mp_falldamage"]["type"] == "LIST"
    assert by_cvar["mp_falldamage"]["typeInfo"]["options"] == [
        {"label": "Normal", "value": "0"}, {"label": "Realistic", "value": "1"}
    ]
    assert model.unresolved == []
    assert model.unsupported == []


def test_settings_scr_unknown_option_type_is_typed_unidentified():
    body = b'VERSION 1.0\n\nDESCRIPTION X\n{\n\t"cvar"\n\t{\n\t\t"Prompt"\n\t\t{ WEIRD }\n\t\t{ "0" }\n\t}\n}\n'
    model = _decode("scripts/settings.scr", body)
    _assert_gapless(model, len(body))
    reasons = [row for row in model.typed_unidentified if row.get("reason") == "unknown-option-type"]
    assert reasons


# --- tab-rows / key-value-lines ------------------------------------------------------------


def test_tab_rows_projects_action_and_description_cells():
    body = b'"+forward"\t\t"Move Forward"\r\n"+back"\t\t"Move Backward"\t// comment\r\n'
    model = _decode("scripts/kb_act.lst", body)
    _assert_gapless(model, len(body))
    entries = model.rows["entries"]
    assert entries[0]["action"] == "+forward"
    assert entries[0]["description"] == "Move Forward"
    assert entries[1]["description"] == "Move Backward"
    assert len(model.comments) == 1


def test_key_value_lines_projects_a_key_and_a_quoted_value():
    body = b'// Valve Game Info file\r\ngame "Vampire"\r\nstartmap "alley"\r\n'
    model = _decode("scripts/liblist.gam", body)
    _assert_gapless(model, len(body))
    entries = [row for row in model.rows["entries"] if row["cells"]]
    assert entries[0]["entryKey"] == "game"
    assert entries[0]["entryValue"] == "Vampire"
    assert len(model.comments) == 1


# --- line-list / won-lists --------------------------------------------------------------------


def test_line_list_projects_one_plain_text_row_per_line():
    body = b"Lobby\r\nTechnical Support\r\nFrench"
    model = _decode("scripts/rooms.lst", body)
    _assert_gapless(model, len(body))
    texts = [row["text"] for row in model.rows["entries"]]
    assert texts == ["Lobby", "Technical Support", "French"]


def test_line_list_extracts_a_comment_line_instead_of_swallowing_it_into_a_row():
    body = b"// header\r\nLobby\r\nFrench\r\n"
    model = _decode("scripts/rooms.lst", body)
    _assert_gapless(model, len(body))
    assert [row["text"] for row in model.comments] == ["// header"]
    assert [row["text"] for row in model.rows["entries"]] == ["Lobby", "French"]


def test_line_list_claims_a_blank_line_as_whitespace_instead_of_fabricating_an_empty_row():
    body = b"Lobby\r\n\r\nFrench\r\n"
    model = _decode("scripts/rooms.lst", body)
    _assert_gapless(model, len(body))
    assert [row["text"] for row in model.rows["entries"]] == ["Lobby", "French"]
    assert "" not in [row["text"] for row in model.rows["entries"]]
    assert model.omitted_proven and model.omitted_proven[0]["byteLength"] > 0


def test_tab_rows_skips_a_comment_only_line_instead_of_fabricating_an_empty_cells_row():
    body = b'"+forward"\t\t"Move Forward"\r\n// header only\r\n"+back"\t\t"Move Backward"\r\n'
    model = _decode("scripts/kb_act.lst", body)
    _assert_gapless(model, len(body))
    entries = model.rows["entries"]
    assert all(entry["cells"] for entry in entries)
    assert [entry["action"] for entry in entries] == ["+forward", "+back"]


def test_won_lists_projects_a_named_block_of_server_addresses():
    body = (
        b"// Server Lists\r\nTitan\r\n{\r\n\thalf-life.east.won.net:6003\r\n"
        b"\thalf-life.west.won.net:6003\r\n}\r\n"
    )
    model = _decode("scripts/woncomm.lst", body)
    _assert_gapless(model, len(body))
    entry = model.rows["entries"][0]
    assert entry["name"] == "Titan"
    assert [s["address"] for s in entry["servers"]] == [
        "half-life.east.won.net:6003", "half-life.west.won.net:6003"
    ]


def test_won_lists_unterminated_block_is_an_anomaly_not_a_crash():
    body = b"Titan\r\n{\r\n\thalf-life.east.won.net:6003\r\n"
    model = _decode("scripts/woncomm.lst", body)
    _assert_gapless(model, len(body))
    assert any(row["role"] == "unterminated-block" for row in model.anomalies)


def test_keyvalues_unterminated_block_is_an_anomaly_not_a_ledger_crash():
    body = b'"Foo"\n{\n  "a" "1"\n'
    model = _decode("resource/vampirescheme.res", body)
    _assert_gapless(model, len(body))
    assert any(row["role"] == "unterminated-block" for row in model.anomalies)


def test_keyvalues_unterminated_block_after_a_stray_close_brace_does_not_crash():
    body = b'}\n"Foo"\n{\n "a" "1"\n'
    model = _decode("resource/vampirescheme.res", body)
    _assert_gapless(model, len(body))
    assert any(row["role"] == "unterminated-block" for row in model.anomalies)


def test_keyvalues_unterminated_quote_swallowing_the_close_brace_does_not_crash():
    body = b'"Foo"\n{\n "a" "1\n}\n'
    model = _decode("resource/vampirescheme.res", body)
    _assert_gapless(model, len(body))
    assert any(row["role"] == "unterminated-block" for row in model.anomalies)


# --- anomalies named by the spec ------------------------------------------------------------


def test_mixed_line_endings_is_a_named_anomaly():
    body = b'"a"\t"1"\r\n"b"\t"2"\n'
    model = _decode("resource/vampirescheme.res", body)
    _assert_gapless(model, len(body))
    assert any(row["role"] == "mixed-line-endings" for row in model.anomalies)


def test_non_ascii_latin1_bytes_are_a_named_anomaly_with_the_decoded_text():
    body = b'"a"\t"caf\xe9"\r\n'
    model = _decode("resource/vampirescheme.res", body)
    _assert_gapless(model, len(body))
    rows = [row for row in model.anomalies if row["role"] == "non-ascii-latin1"]
    assert rows and rows[0]["text"] == "\xe9"


def test_mixed_line_endings_is_a_named_anomaly_in_a_row_grammar_too():
    body = b'"+forward"\t\t"Move Forward"\r\n"+back"\t\t"Move Backward"\n'
    model = _decode("scripts/kb_act.lst", body)
    _assert_gapless(model, len(body))
    assert any(row["role"] == "mixed-line-endings" for row in model.anomalies)


def test_non_ascii_latin1_is_a_named_anomaly_in_line_list_too():
    body = b"Lobby\r\nCaf\xe9\r\n"
    model = _decode("scripts/rooms.lst", body)
    _assert_gapless(model, len(body))
    rows = [row for row in model.anomalies if row["role"] == "non-ascii-latin1"]
    assert rows and rows[0]["text"] == "\xe9"


def test_empty_member_publishes_an_empty_member_omission():
    model = _decode("resource/vampirescheme.res", b"")
    assert model.omissions == [{"role": "empty-member"}]
    assert model.ledger_row["byteLength"] == 0
    assert model.ledger_row["coveragePercent"] == 100.0


# --- zero-state proof: this is a text-only seam, so no `-zero` claim is ever made -------------


def test_no_grammar_ever_claims_a_reserved_or_padding_zero_state():
    fixtures = [
        ("resource/vampirescheme.res", _SCHEME_BODY),
        ("resource/confirmdialog.res", _LAYOUT_BODY),
        ("resource/gamemenu.res", _MENU_BODY),
        ("scripts/320_hud.txt", _HUD_BODY),
        ("resource/mainmenuparticles.txt", _MENU_SCENE_BODY),
        ("scripts/titles.txt", _TITLES_BODY),
        ("scripts/settings.scr", _SETTINGS_BODY),
    ]
    for key, body in fixtures:
        model = _decode(key, body)
        states = set(model.ledger_row["stateBytes"])
        assert not states & {"reserved-zero", "padding-zero"}, key


# --- container / round trip ------------------------------------------------------------------


def test_export_builds_a_valid_scene_less_glb_and_validates(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    assert path.exists()
    summary = validation.validate(path)
    assert summary["category"] == "scheme"
    assert summary["byteCoveragePercent"] == 100.0
    assert summary["unresolved"] == 0
    assert summary["unsupported"] == 0

    document, binary = validation.read_glb(path)
    assert binary == b""
    assert document["extensionsUsed"] == ["ELYSIUM_vtmb_ui_resource"]
    assert document["extensionsRequired"] == ["ELYSIUM_vtmb_ui_resource"]
    assert document["asset"]["generator"] == "Elysium Ui-resource GLB Exporter"
    root = document["extensions"]["ELYSIUM_vtmb_ui_resource"]
    assert list(root)[:5] == ["schemaVersion", "identity", "sourceResolution", "dependencies", "coverage"]
    for core_key in ("scenes", "nodes", "meshes", "images", "textures", "samplers"):
        assert core_key not in document


def test_export_answers_a_font_tier_from_the_index_it_already_holds(tmp_path):
    """The production wiring, not an injected resolver: `export` builds the font resolver from the
    UP-first index, so a tier whose `.fnt` the install holds publishes `resolved: true`."""

    index = _index("resource/vampirescheme.res", "materials/fonts/tahoma_16_500_000.fnt")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    root = validation.read_glb(path)[0]["extensions"]["ELYSIUM_vtmb_ui_resource"]
    font_deps = [row for row in root["dependencies"] if row["role"] == "font"]
    assert font_deps == [
        {"role": "font", "asset": "vtmb:font:tahoma_16_500_000",
         "sourcePath": "materials/fonts/tahoma_16_500_000.fnt", "resolved": True}
    ]
    assert root["scheme"]["fonts"][0]["tiers"][0]["resolved"] is True
    assert validation.warnings_for(validation.validate(path)) == []


def test_export_leaves_a_font_tier_the_install_lacks_unresolved_and_warns(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    summary = validation.validate(path)
    assert summary["unresolved"] == 0
    warnings = validation.warnings_for(summary)
    assert "unresolved reference: font 'materials/fonts/tahoma_16_500_000.fnt'" in warnings


def test_export_answers_a_menu_scene_particle_from_the_index_it_already_holds(tmp_path):
    index = _index("resource/mainmenuparticles.txt", "particles/m_clouds_emmiter.txt")
    path = exporter.export(
        index, "resource/mainmenuparticles.txt", tmp_path,
        read_bytes=lambda idx, k: _MENU_SCENE_BODY if k == "resource/mainmenuparticles.txt" else None,
    )
    root = validation.read_glb(path)[0]["extensions"]["ELYSIUM_vtmb_ui_resource"]
    particle_deps = [row for row in root["dependencies"] if row["role"] == "particle"]
    assert particle_deps == [
        {"role": "particle", "asset": "vtmb:particle:m_clouds_emmiter",
         "sourcePath": "M_Clouds_Emmiter", "resolved": True}
    ]


def test_export_leaves_a_menu_scene_particle_the_install_lacks_unresolved(tmp_path):
    index = _index("resource/mainmenuparticles.txt")
    path = exporter.export(
        index, "resource/mainmenuparticles.txt", tmp_path,
        read_bytes=lambda idx, k: _MENU_SCENE_BODY if k == "resource/mainmenuparticles.txt" else None,
    )
    root = validation.read_glb(path)[0]["extensions"]["ELYSIUM_vtmb_ui_resource"]
    particle_deps = [row for row in root["dependencies"] if row["role"] == "particle"]
    assert particle_deps[0]["resolved"] is False


def test_comments_are_claimed_mapped_text_not_mapped():
    body = b'// a comment\r\n"a"\t"1"\r\n'
    model = _decode("resource/vampirescheme.res", body)
    _assert_gapless(model, len(body))
    comment_ranges = [r for r in model.ledger_row["ranges"] if r["owner"].startswith("comments[")]
    assert comment_ranges
    assert all(r["state"] == "mapped-text" for r in comment_ranges)


def test_coverage_mapped_names_only_the_projection_field_the_unit_populates(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    mapped = document["extensions"]["ELYSIUM_vtmb_ui_resource"]["coverage"]["mapped"]
    assert "tree" in mapped and "scheme" in mapped
    for absent in ("layout", "menu", "hud", "titles", "rows", "substitutions", "strings",
                   "options", "menuScene"):
        assert absent not in mapped


def test_coverage_omitted_proven_carries_evidence_for_insignificant_whitespace(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    omitted = document["extensions"]["ELYSIUM_vtmb_ui_resource"]["coverage"]["omittedProven"]
    assert omitted == [
        {"role": "whitespace", "reason": "insignificant-separator-bytes",
         "byteLength": omitted[0]["byteLength"]}
    ]
    assert omitted[0]["byteLength"] > 0


def test_validator_rejects_an_omitted_proven_ledger_range_with_no_evidence(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    tampered = copy.deepcopy(document)
    tampered_root = tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]
    ranges = tampered_root["coverage"]["byteLedger"][0]["ranges"]
    assert any(row["state"] == "omitted-proven" for row in ranges)
    tampered_root["coverage"]["omittedProven"] = []
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary)


def test_validator_rejects_a_bin_chunk_on_a_scene_less_ui_resource_unit(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    tampered = copy.deepcopy(document)
    tampered["buffers"] = [{"byteLength": 16}]
    tampered["bufferViews"] = [{"buffer": 0, "byteOffset": 0, "byteLength": 16}]
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, b"\x00" * 16)


def test_validator_rejects_a_grammar_that_disagrees_with_its_own_source_path(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    tampered = copy.deepcopy(document)
    tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]["grammar"] = "won-lists"
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary)


def test_validator_rejects_an_encoding_that_disagrees_with_its_own_source_path(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    tampered = copy.deepcopy(document)
    tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]["encoding"] = "utf-16-le"
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary)


# --- dormancy --------------------------------------------------------------------------------


def test_is_dormant_names_the_source_member_tables_dormant_keys():
    assert model_module.is_dormant("scripts/titles.txt") is True
    assert model_module.is_dormant("scripts/settings.scr") is True
    assert model_module.is_dormant("scripts/rooms.lst") is True
    assert model_module.is_dormant("scripts/woncomm.lst") is True
    assert model_module.is_dormant("scripts/320_hud.txt") is True
    assert model_module.is_dormant("scripts/640_hud.txt") is True
    assert model_module.is_dormant("resource/vampirece2scheme.res") is True
    assert model_module.is_dormant("scripts/dialog_main") is True
    assert model_module.is_dormant("resource/vampirescheme.res") is False
    assert model_module.is_dormant("resource/confirmdialog.res") is False
    assert model_module.dormant_evidence("resource/vampirescheme.res") is None
    assert model_module.dormant_evidence("scripts/titles.txt")


def test_export_publishes_identity_dormant_true_with_evidence_for_a_dormant_unit(tmp_path):
    index = _index("scripts/titles.txt")
    path = exporter.export(
        index, "scripts/titles.txt", tmp_path,
        read_bytes=lambda idx, k: _TITLES_BODY if k == "scripts/titles.txt" else None,
    )
    document, binary = validation.read_glb(path)
    identity = document["extensions"]["ELYSIUM_vtmb_ui_resource"]["identity"]
    assert identity["dormant"] is True
    assert identity["dormantEvidence"]
    summary = validation.validate(path)
    assert summary["byteCoveragePercent"] == 100.0


def test_export_publishes_identity_dormant_false_with_no_evidence_for_a_live_unit(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    identity = document["extensions"]["ELYSIUM_vtmb_ui_resource"]["identity"]
    assert identity["dormant"] is False
    assert "dormantEvidence" not in identity


def test_validator_rejects_an_identity_whose_dormant_flag_disagrees_with_the_source_closure(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    tampered = copy.deepcopy(document)
    tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]["identity"]["dormant"] = True
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary)


# --- dependency / UTF-16 spot-check tamper proofs --------------------------------------------


def test_validator_rejects_a_fabricated_dependency_row(tmp_path):
    index = _index("resource/confirmdialog.res")
    path = exporter.export(
        index, "resource/confirmdialog.res", tmp_path,
        read_bytes=lambda idx, k: _LAYOUT_BODY if k == "resource/confirmdialog.res" else None,
    )
    document, binary = validation.read_glb(path)
    closure = load_source_closure(
        index, "resource/confirmdialog.res",
        read_bytes=lambda idx, k: _LAYOUT_BODY if k == "resource/confirmdialog.res" else None,
    )
    tampered = copy.deepcopy(document)
    tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]["dependencies"] = [
        {"role": "material", "asset": "vtmb:material:totally/fabricated",
         "sourcePath": "nope", "resolved": True}
    ]
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary, source_members=closure.members())


def test_validator_rejects_an_emptied_dependency_table():
    index = _index("resource/confirmdialog.res")
    closure = load_source_closure(
        index, "resource/confirmdialog.res",
        read_bytes=lambda idx, k: _LAYOUT_BODY if k == "resource/confirmdialog.res" else None,
    )
    model = decode_ui_resource(closure)
    document, binary = exporter.build_document(model)
    tampered = copy.deepcopy(document)
    tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]["dependencies"] = []
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary, source_members=closure.members())


def test_verify_utf16_offsets_rejects_a_token_whose_byte_span_does_not_match_its_text():
    from elysium_pipeline.validation.ui_resource_glb import _verify_utf16_offsets

    # `byteOffset`/`byteLength` span the whole `"Key" "Value"` record, matching
    # `_project_strings`'s own `node["offset"]`/`node["length"]`.
    record = '"GameUI_Label" "OK"'
    text = f'"Tokens"\r\n{{\r\n{record}\r\n}}\r\n'
    data = text.encode("utf-16-le")
    offset = text.index(record) * 2
    root = {"strings": {"tokens": [
        {"name": "GameUI_Label", "text": "WRONG", "byteOffset": offset,
         "byteLength": len(record) * 2}
    ]}}
    with pytest.raises(UnitValidationError):
        _verify_utf16_offsets(root, data)


def test_export_output_path_preserves_the_install_relative_directory(tmp_path):
    index = _index("scripts/dialog_main")
    path = exporter.export(
        index, "scripts/dialog_main", tmp_path,
        read_bytes=lambda idx, k: _LAYOUT_BODY if k == "scripts/dialog_main" else None,
    )
    assert path == tmp_path / "ui-resources" / "scripts" / "dialog_main.glb"


def test_export_is_deterministic_byte_for_byte(tmp_path):
    index = _index("resource/vampirescheme.res")
    read_bytes = lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None
    first = exporter.export(index, "resource/vampirescheme.res", tmp_path / "a", read_bytes=read_bytes)
    second = exporter.export(index, "resource/vampirescheme.res", tmp_path / "b", read_bytes=read_bytes)
    assert first.read_bytes() == second.read_bytes()


def test_validator_rejects_a_tampered_byte_ledger(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    tampered = copy.deepcopy(document)
    tampered_root = tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]
    tampered_root["coverage"]["byteLedger"][0]["ranges"][0]["length"] += 1
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary)


def test_validator_rejects_a_ledger_whose_owner_is_missing(tmp_path):
    index = _index("resource/vampirescheme.res")
    path = exporter.export(
        index, "resource/vampirescheme.res", tmp_path,
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    document, binary = validation.read_glb(path)
    tampered = copy.deepcopy(document)
    tampered_root = tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]
    tampered_root["coverage"]["byteLedger"][0]["ranges"][0]["owner"] = ""
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary)


def test_validator_rejects_a_byte_ledger_that_is_internally_consistent_but_disagrees_with_the_redecode():
    """A ledger row can pass `validate_ledgers` (internally consistent, right hash, right length)
    while naming a partition that has nothing to do with what the source actually decodes to --
    export-time validation must also weigh the published ledger against the independent re-decode."""

    from elysium_pipeline.formats.unit_contract import ranges_sha256

    index = _index("resource/vampirescheme.res")
    closure = load_source_closure(
        index, "resource/vampirescheme.res",
        read_bytes=lambda idx, k: _SCHEME_BODY if k == "resource/vampirescheme.res" else None,
    )
    model = decode_ui_resource(closure)
    document, binary = exporter.build_document(model)
    tampered = copy.deepcopy(document)
    tampered_root = tampered["extensions"]["ELYSIUM_vtmb_ui_resource"]
    ledger_row = tampered_root["coverage"]["byteLedger"][0]
    byte_length = ledger_row["byteLength"]
    fake_ranges = [{"offset": 0, "length": byte_length, "state": "mapped-text",
                     "owner": "made.up.owner"}]
    ledger_row["ranges"] = fake_ranges
    ledger_row["stateBytes"] = {"mapped-text": byte_length}
    ledger_row["rangesSha256"] = ranges_sha256(ledger_row["sourcePath"], byte_length, fake_ranges)
    with pytest.raises(UnitValidationError):
        validation.validate_document(tampered, binary, source_members=closure.members())


def test_byte_ledger_claim_over_a_source_offset_out_of_range_raises():
    from elysium_pipeline.formats.ui_resource_glb.coverage import new_ledger
    from elysium_pipeline.formats.unit_contract.origin import Origin, SourceMember

    member = SourceMember(
        role="unit-selecting", path="x", data=b"abc", origin=Origin(kind="loose", root="test")
    )
    ledger = new_ledger(member)
    with pytest.raises(ByteLedgerError):
        ledger.claim(0, 10, "mapped-text", "owner")
