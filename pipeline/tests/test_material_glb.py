"""Synthetic contract tests for the isolated Material GLB exporter."""

from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
from unittest import mock

import pytest

import zlib

from elysium_pipeline.exporters import material_glb
from elysium_pipeline.formats.material_glb import coverage as material_coverage
from elysium_pipeline.formats.material_glb import decode_material
from elysium_pipeline.formats.material_glb import lexer
from elysium_pipeline.formats.material_glb import source as material_source
from elysium_pipeline.formats.material_glb.model import MATERIAL_EXTENSION
from elysium_pipeline.formats.material_glb.source import MaterialSourceClosure, SourceMember
from elysium_pipeline.validation import material_glb as validation

_BSP_HEADER_BYTES = 8 + 64 * 16 + 4


def _zip_member(name: str, payload: bytes) -> tuple[bytes, bytes, int]:
    """One stored ZIP member: its local record, its encoded name, and its length."""

    raw = name.encode("latin-1")
    local = struct.pack(
        "<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, 0, 0, zlib.crc32(payload), len(payload),
        len(payload), len(raw), 0,
    ) + raw + payload
    return local, raw, len(payload)


def _pakfile_zip(members: dict[str, bytes]) -> bytes:
    """A minimal stored-only ZIP, the same layout the compiler writes into BSP lump 40."""

    body = bytearray()
    central = bytearray()
    for name, payload in members.items():
        offset = len(body)
        local, raw, size = _zip_member(name, payload)
        body.extend(local)
        central.extend(
            struct.pack(
                "<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0, 0, 0, zlib.crc32(payload),
                size, size, len(raw), 0, 0, 0, 0, 0, offset,
            )
            + raw
        )
    central_offset = len(body)
    body.extend(central)
    body.extend(
        struct.pack(
            "<IHHHHIIH", 0x06054B50, 0, 0, len(members), len(members), len(central),
            central_offset, 0,
        )
    )
    return bytes(body)


def _bsp_with_pakfile(members: dict[str, bytes]) -> bytes:
    """A synthetic BSP whose only populated lump is PAKFILE (lump 40)."""

    payload = _pakfile_zip(members)
    header = bytearray(struct.pack("<4si", b"VBSP", 17))
    for index in range(64):
        if index == 40:
            header.extend(struct.pack("<iii4s", _BSP_HEADER_BYTES, len(payload), 0, b"\0\0\0\0"))
        else:
            header.extend(struct.pack("<iii4s", 0, 0, 0, b"\0\0\0\0"))
    header.extend(struct.pack("<i", 7))
    assert len(header) == _BSP_HEADER_BYTES
    return bytes(header) + payload
SIMPLE = b'"VertexLitGeneric"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n}\r\n'
#: A family whose selector is transcribed from the binary, so it resolves to a program.
RESOLVED = b'"LightmappedGeneric"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n}\r\n'
#: A family whose selector is not transcribed, so it resolves to no program at all.
UNTRANSCRIBED = b'"Wireframe"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n}\r\n'


def _closure(body: bytes, path: str = "synthetic/material"):
    member = SourceMember("vmt", f"materials/{path}.vmt", body, {"kind": "synthetic"})
    return MaterialSourceClosure(path, f"vtmb:material:{path}", member)


def _decode(body: bytes, *, present=("models/teeth",), path="synthetic/material",
            materials=()):
    return decode_material(
        _closure(body, path),
        texture_exists=lambda value: value in present,
        material_exists=lambda value: value in materials,
    )


def _publish(body: bytes, **kwargs):
    model = _decode(body, **kwargs)
    document, binary = material_glb.build_document(model)
    return model, document, binary


def test_every_source_byte_is_claimed_exactly_once():
    model = _decode(SIMPLE)
    ledger = model.byte_coverage[0]
    assert ledger["coveragePercent"] == 100.0
    assert ledger["accountedBytes"] == len(SIMPLE)
    cursor = 0
    for row in ledger["ranges"]:
        assert row["offset"] == cursor
        cursor += row["length"]
    assert cursor == len(SIMPLE)


def test_the_ranges_digest_delegates_to_the_shared_contract_helper():
    # `unit_contract.ranges_sha256` is the single owner of the range-table digest formula; a
    # reimplementation local to this seam would silently diverge if the contract's canonical form
    # ever changed. Patching the shared helper and observing its sentinel land in the published
    # row proves `coverage.ByteLedger.finish` actually calls it rather than recomputing the digest
    # itself.
    with mock.patch.object(material_coverage, "ranges_sha256", return_value="patched-digest"):
        model = _decode(SIMPLE)
    assert model.byte_coverage[0]["rangesSha256"] == "patched-digest"


def test_whitespace_is_the_only_omission_and_comments_are_kept():
    body = b'"UnlitGeneric"\n{\n\t"$additive" "1" // authored note\n}\n'
    model = _decode(body, present=())
    assert [row["text"] for row in model.comments] == ["// authored note"]
    states = model.byte_coverage[0]["stateBytes"]
    assert set(states) == {"mapped", "omitted-proven"}


def test_a_tokenizer_run_covers_the_source_gaplessly():
    text = lexer.decode_text(SIMPLE)
    cursor = 0
    for token in lexer.tokenize(text):
        assert token.offset == cursor
        cursor = token.end
    assert cursor == len(SIMPLE)


def test_parameters_keep_source_order_casing_and_offsets():
    body = b'"VertexLitGeneric"\n{\n"$baseTexture" "models/teeth"\n"$surfaceProp" "flesh"\n}\n'
    model = _decode(body)
    assert [p.key for p in model.parameters] == ["$basetexture", "$surfaceprop"]
    assert [p.source_key for p in model.parameters] == ["$baseTexture", "$surfaceProp"]
    assert [p.index for p in model.parameters] == [0, 1]
    assert model.surface_property == "flesh"
    assert {"role": "surface-property", "asset": "vtmb:surface-property:flesh",
        "sourcePath": "scripts/surfaceproperties.txt#flesh"} in model.dependencies


def test_a_repeated_proxy_stays_three_ordered_records():
    """The GlobalWetness triple is three blocks, one per `$envmaptint` channel.

    A reader that collapsed duplicate keys would keep one and silently recolour the material.
    """
    body = (
        b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n"Proxies"\n{\n'
        b'"GlobalWetness"{"resultVar" "$envmaptint[0]" "scale" "0.56"}\n'
        b'"GlobalWetness"{"resultVar" "$envmaptint[1]" "scale" "0.56"}\n'
        b'"GlobalWetness"{"resultVar" "$envmaptint[2]" "scale" "0.56"}\n}\n}\n'
    )
    model = _decode(body)
    assert [proxy.name for proxy in model.proxies] == ["globalwetness"] * 3
    channels = [proxy.parameters[0].value for proxy in model.proxies]
    assert channels == ["$envmaptint[0]", "$envmaptint[1]", "$envmaptint[2]"]


def test_the_env_cubemap_placeholder_is_a_symbol_not_a_binding():
    body = b'"LightmappedGeneric"\n{\n"$envmap" "env_cubemap"\n}\n'
    model = _decode(body, present=())
    assert model.environment == {"parameter": "$envmap", "symbol": "env_cubemap"}
    assert model.texture_bindings == []


def test_an_absent_texture_binds_unresolved_without_a_dependency():
    body = b'"VertexLitGeneric"\n{\n"$basetexture" "models/gone"\n}\n'
    model = _decode(body, present=())
    assert len(model.texture_bindings) == 1
    assert not model.texture_bindings[0]["resolved"]
    assert model.dependencies == []


def test_a_texture_dependency_states_that_the_install_answered_it():
    """The row exists only where the binding resolved, so it publishes `resolved: true`; the
    corpus index reads that key to tell a dangling reference from an answered one."""

    body = b'"VertexLitGeneric"\n{\n"$basetexture" "models/wall"\n}\n'
    model = _decode(body, present=("models/wall",))
    assert [row["role"] for row in model.dependencies] == ["texture"]
    assert model.dependencies[0]["resolved"] is True


def test_a_render_target_value_is_not_a_texture_dependency():
    body = b'"Water"\n{\n"$refracttexture" "_rt_WaterRefraction"\n}\n'
    model = _decode(body, present=())
    assert model.texture_bindings[0]["kind"] == "render-target"
    assert model.texture_bindings[0]["asset"] is None
    assert model.dependencies == []


def test_a_material_shaped_value_is_a_material_reference_and_not_a_texture_binding():
    """G16: `$bottommaterial` names what VBSP paints on the inward side of a water brush -- a
    *material*. Read as a texture it resolved against `materials/**.tth` and never answered, so
    all 24 authors carried the only unresolved bindings in the water cast; in the material
    namespace it resolves (`PHASE0_VERDICT.md` A6)."""

    body = b'"Water"\n{\n"$bottommaterial" "dev\\dev_waterbeneath2"\n}\n'
    model = _decode(body, present=(), materials=("dev/dev_waterbeneath2",))
    assert model.texture_bindings == []
    assert model.material_references == [{
        "parameter": "$bottommaterial",
        "value": "dev/dev_waterbeneath2",
        "asset": "vtmb:material:dev/dev_waterbeneath2",
        "resolved": True,
        "selfReference": False,
    }]
    assert [(row["role"], row.get("parameter")) for row in model.dependencies] == [
        ("material", "$bottommaterial")]
    assert model.dependencies[0]["sourcePath"] == "materials/dev/dev_waterbeneath2.vmt"


def test_a_unit_naming_itself_as_its_own_underside_says_so():
    """`dev/dev_waterbeneath2` and `dev/oceanbeneath` are the faces VBSP paints on the inward
    side, so they name themselves; the stage reads that join to tell an underside sheet from the
    surface above it."""

    body = b'"Water"\n{\n"$bottommaterial" "dev/dev_waterbeneath2.vmt"\n}\n'
    model = _decode(body, present=(), materials=("dev/dev_waterbeneath2",),
                    path="dev/dev_waterbeneath2")
    assert model.material_references[0]["selfReference"] is True
    assert model.material_references[0]["asset"] == "vtmb:material:dev/dev_waterbeneath2"


def test_an_absent_material_reference_publishes_unresolved_without_a_dependency():
    body = b'"Water"\n{\n"$bottommaterial" "dev/gone"\n}\n'
    model = _decode(body, present=(), materials=())
    assert model.material_references[0]["resolved"] is False
    assert model.dependencies == []


@pytest.mark.parametrize("key", ["$bottommaterial", "$crackmaterial", "$modelmaterial",
                                 "$leaknoise"])
def test_every_material_shaped_key_publishes_in_the_material_namespace(key):
    body = b'"LightmappedGeneric"\n{\n"' + key.encode() + b'" "glass/glassb"\n}\n'
    model = _decode(body, present=("glass/glassb",), materials=("glass/glassb",))
    # `glass/glassb` is a name the install answers as BOTH a `.tth` and a `.vmt`; the value here
    # names the material, so a texture binding would have been the wrong reading even though it
    # would have resolved.
    assert model.texture_bindings == []
    assert model.material_references[0]["asset"] == "vtmb:material:glass/glassb"


def test_a_published_unit_carries_its_material_references_and_they_validate():
    body = b'"Water"\n{\n"$bottommaterial" "dev/dev_waterbeneath2"\n}\n'
    model, document, binary = _publish(body, present=(), materials=("dev/dev_waterbeneath2",))
    validation.validate_document(document, binary)
    root = document["extensions"][material_glb.MATERIAL_EXTENSION]
    assert root["materialReferences"][0]["parameter"] == "$bottommaterial"
    assert "materialReferences" in root["coverage"]["mapped"]


def test_a_proxy_operand_is_not_mistaken_for_a_material_input():
    body = (
        b'"UnlitGeneric"\n{\n"Proxies"\n{\n"TextureScroll"\n'
        b'{"texturescrollvar" "$basetexture"}\n}\n}\n'
    )
    model = _decode(body, present=("models/teeth",))
    assert model.texture_bindings == []
    assert model.dependencies == []


# MalformedSourceTests
# Eleven shipped VMTs are not well-formed. Each departure publishes with its evidence.

def _roles(model):
    return sorted({row["role"] for row in model.anomalies})


def test_a_valueless_key_publishes_as_a_flag():
    body = b'"VertexLitGeneric"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n\t"nomip"\r\n}\r\n'
    model = _decode(body)
    assert _roles(model) == ["valueless-key"]
    flag = model.parameters[-1]
    assert (flag.key, flag.value, flag.value_type) == ("nomip", "", "none")
    assert model.byte_coverage[0]["coveragePercent"] == 100.0


def test_a_block_unclosed_at_end_of_file_still_publishes():
    body = b'"Sprite"\n{\n"$basetexture" "models/teeth"\n"Proxies"\n{\n"Sine"\n{\n"rate" "1"\n}'
    model = _decode(body)
    assert "unclosed-block-at-end-of-file" in _roles(model)
    assert model.byte_coverage[0]["accountedBytes"] == len(body)


def test_a_stray_close_brace_after_the_shader_block_is_claimed():
    body = b'"UnlitGeneric"\n{\n"$basetexture" "models/teeth"\n}\n}\n'
    model = _decode(body)
    assert _roles(model) == ["content-after-shader-block"]
    assert model.byte_coverage[0]["accountedBytes"] == len(body)


def test_a_stray_quote_runs_to_end_of_file_without_losing_a_byte():
    body = b'"VertexLitGeneric"\n{\n"$nocull" 1"\t// added by psycho-a\n}\n'
    model = _decode(body, present=())
    assert "unterminated-quoted-string" in _roles(model)
    assert model.byte_coverage[0]["accountedBytes"] == len(body)


def test_a_commented_out_block_header_leaves_an_anonymous_block():
    body = (
        b'"LightmappedGeneric"\n{\n"Proxies"\n{\n'
        b'//\t"GaussianNoise"\n\t{\n\t"resultVar" "$temp"\n\t}\n}\n}\n'
    )
    model = _decode(body, present=())
    assert "anonymous-block" in _roles(model)
    assert model.byte_coverage[0]["coveragePercent"] == 100.0


# ShaderResolutionTests
# Combo rules transcribed from `stdshader_dx8.dll`; see `docs/vtmb/shader_combos.md`.

def _resolution(body, present=("models/teeth", "models/mask")):
    return _decode(body, present=present).shader_resolution


def _only(body, field, present=("models/teeth", "models/mask")):
    """The single program a family with no render-config axis resolves to."""
    programs = _resolution(body, present=present)["programs"]
    assert len(programs) == 1
    return programs[0][field]


def test_a_family_with_no_transcribed_selector_resolves_unresolved():
    resolution = _resolution(UNTRANSCRIBED)
    assert resolution["family"] == "wireframe"
    assert not resolution["resolved"]
    assert resolution["programs"] == []
    assert resolution["reason"] == "selector-not-transcribed"


def test_a_plain_lightmapped_material_selects_the_base_program():
    resolution = _resolution(RESOLVED)
    assert resolution["resolved"]
    assert len(resolution["programs"]) == 1
    assert resolution["programs"][0]["vertexShader"] == "LightmappedGeneric"
    assert resolution["programs"][0]["pixelShader"] == "LightmappedGeneric"
    assert resolution["programs"][0]["condition"] == ""


def test_the_eyes_family_leaves_the_overbright_choice_to_render_config():
    """`$vampire` picks the pair; the remaining choice is engine state, not the VMT."""
    body = b'"Eyes"\n{\n"$basetexture" "models/teeth"\n"$vampire" "1"\n}\n'
    programs = _resolution(body)["programs"]
    assert {(p["condition"], p["pixelShader"]) for p in programs} == {("overbright==2", "Eyes_Vampire_Overbright2"), ("", "Eyes_Vampire")}
    assert all(p["vertexShader"] == "Eyes" for p in programs)


def test_eyes_without_the_vampire_flag_selects_the_stock_pair():
    body = b'"Eyes"\n{\n"$basetexture" "models/teeth"\n"$iris" "models/mask"\n}\n'
    programs = _resolution(body)["programs"]
    assert {p["pixelShader"] for p in programs} == {"Eyes", "Eyes_Overbright2"}


def test_teeth_draws_with_a_pixel_program_outside_its_own_family():
    """Teeth ships a vertex program but no pixel program; it reuses VertexLitTexture."""
    body = b'"Teeth"\n{\n"$basetexture" "models/teeth"\n}\n'
    programs = _resolution(body)["programs"]
    assert {p["pixelShader"] for p in programs} == {"VertexLitTexture", "VertexLitTexture_Overbright2"}
    assert all(p["vertexShader"] == "Teeth" for p in programs)


def test_an_envmap_mask_texture_overrides_base_alpha_env_map_mask():
    """Pixel table 0x10021620 indexes 6 and 7: a bound `$envmapmask` wins.

    `_BaseAlphaMaskedEnvMapV2` is therefore unreachable whenever both are authored -- a
    precedence rule the combo filenames do not state.
    """
    body = (
        b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$envmapmask" "models/mask"\n'
        b'"$basealphaenvmapmask" "1"\n}\n'
    )
    assert _only(body, "pixelShader") == "LightmappedGeneric_MaskedEnvMapV2"


def test_base_alpha_env_map_mask_applies_when_no_mask_texture_is_bound():
    body = (
        b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$basealphaenvmapmask" "1"\n}\n'
    )
    assert _only(body, "pixelShader") == "LightmappedGeneric_BaseAlphaMaskedEnvMapV2"


def test_the_env_cubemap_placeholder_still_counts_as_a_bound_environment():
    """VBSP patches the face to a baked cube, so the selector sees a texture."""
    body = (
        b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n}\n'
    )
    assert _only(body, "pixelShader") == "LightmappedGeneric_EnvMapV2"


def test_sphere_and_camera_space_flags_do_nothing_without_an_envmap():
    """Vertex table 0x100214f0 indexes 2, 4 and 6 all fall back to the plain program."""
    body = (
        b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmapsphere" "1"\n"$envmapcameraspace" "1"\n}\n'
    )
    assert _only(body, "vertexShader") == "LightmappedGeneric"


def test_sphere_overrides_camera_space_when_an_envmap_is_present():
    """Vertex table indexes 14 and 15 pick the sphere program."""
    body = (
        b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$envmapsphere" "1"\n"$envmapcameraspace" "1"\n}\n'
    )
    assert _only(body, "vertexShader") == "LightmappedGeneric_EnvMapSphere"


def test_a_material_with_no_base_texture_selects_a_no_texture_program():
    body = b'"LightmappedGeneric"\n{\n"$selfillum" "1"\n}\n'
    assert _only(body, "pixelShader", present=()) == "LightmappedGeneric_NoTexture"


def test_a_bumpmapped_material_draws_the_envmap_in_a_second_pass():
    """VertexLitGeneric suppresses the envmap in pass 0 and adds it back in pass 1.

    The `_ps14` row is the same pass on hardware that supports ps.1.4.
    """
    body = (
        b'"VertexLitGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$bumpmap" "models/mask"\n}\n'
    )
    programs = _resolution(body)["programs"]
    base = [p for p in programs if p["condition"] == "bumpmapping" and p["drawPass"] == 0]
    second = [p for p in programs if p["drawPass"] == 1]
    assert base[0]["pixelShader"] == "VertexLitGeneric"
    assert {p["pixelShader"] for p in second} == {"VertexLitGeneric_EnvmappedBumpmapV2",
        "VertexLitGeneric_EnvmappedBumpmapV2_ps14"}


def test_envmapoptional_deletes_the_envmap_outright():
    """The help text says "dx9 and higher", but VtMB ships no dx9 VertexLitGeneric."""
    body = (
        b'"VertexLitGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$envmapoptional" "1"\n}\n'
    )
    assert _only(body, "pixelShader") == "VertexLitGeneric"


def test_a_normal_map_alpha_mask_without_a_bumpmap_deletes_the_envmap():
    body = (
        b'"VertexLitGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$normalmapalphaenvmapmask" "1"\n}\n'
    )
    assert _only(body, "pixelShader") == "VertexLitGeneric"


def test_unlit_generic_base_alpha_mask_yields_to_a_bound_mask_texture():
    """The base-alpha branch sits ahead of the table and requires the mask to be absent."""
    masked = (
        b'"UnlitGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$envmapmask" "models/mask"\n'
        b'"$basealphaenvmapmask" "1"\n}\n'
    )
    assert _only(masked, "pixelShader") == "UnlitGeneric_EnvMapMask"
    unmasked = (
        b'"UnlitGeneric"\n{\n"$basetexture" "models/teeth"\n'
        b'"$envmap" "env_cubemap"\n"$basealphaenvmapmask" "1"\n}\n'
    )
    assert _only(unmasked, "pixelShader") == "UnlitGeneric_BaseAlphaMaskedEnvMap"


def test_a_sprite_render_mode_selects_its_program_pair():
    body = b'"Sprite"\n{\n"$basetexture" "models/teeth"\n"$spriterendermode" "5"\n}\n'
    assert _only(body, "pixelShader") == "SpriteRenderTransAdd"
    assert _only(body, "vertexShader") == "unlitgeneric_vertexcolor"


def test_ignore_vertex_colors_only_changes_the_additive_sprite_mode():
    additive = (
        b'"Sprite"\n{\n"$spriterendermode" "5"\n"$ignorevertexcolors" "1"\n}\n'
    )
    assert _only(additive, "vertexShader", present=()) == "unlitgeneric"
    animated = (
        b'"Sprite"\n{\n"$spriterendermode" "7"\n"$ignorevertexcolors" "1"\n}\n'
    )
    assert _only(animated, "vertexShader", present=()) == "unlitgeneric_vertexcolor"


def test_the_one_unimplemented_sprite_mode_binds_no_program():
    """Mode 6 is `kRenderEnvironmental`; the shader warns and binds nothing."""
    body = b'"Sprite"\n{\n"$spriterendermode" "6"\n}\n'
    resolution = _resolution(body, present=())
    assert not resolution["resolved"]
    assert resolution["programs"] == []
    assert "binds-no-program" in resolution["reason"]


def test_the_validator_refuses_conditional_programs_with_no_default():
    _, document, binary = _publish(RESOLVED)
    resolution = document["extensions"][MATERIAL_EXTENSION]["shaderResolution"]
    resolution["programs"] = [
        {"pixelShader": "A", "vertexShader": "V", "condition": "overbright==2"},
        {"pixelShader": "B", "vertexShader": "V", "condition": "other"},
    ]
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_repeated_program_condition():
    _, document, binary = _publish(RESOLVED)
    resolution = document["extensions"][MATERIAL_EXTENSION]["shaderResolution"]
    resolution["programs"] = [
        {"pixelShader": "A", "vertexShader": "V", "condition": ""},
        {"pixelShader": "B", "vertexShader": "V", "condition": ""},
    ]
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_refuses_an_unresolved_shader_that_names_a_program():
    _, document, binary = _publish(UNTRANSCRIBED)
    document["extensions"][MATERIAL_EXTENSION]["shaderResolution"]["programs"] = [
        {"pixelShader": "invented", "vertexShader": "invented", "condition": ""}
    ]
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_a_published_unit_validates_and_carries_no_scene_core():
    _, document, binary = _publish(SIMPLE)
    summary = validation.validate_document(document, binary)
    assert summary["asset"] == "vtmb:material:synthetic/material"
    assert summary["byteCoveragePercent"] == 100.0
    for absent in ("images", "textures", "samplers", "scenes", "nodes", "meshes"):
        assert absent not in document
    assert len(document["materials"]) == 1
    assert document["extensionsRequired"] == [MATERIAL_EXTENSION]


def test_the_unit_carries_no_bin_chunk_and_no_buffer():
    _, document, binary = _publish(SIMPLE)
    assert binary == b""
    assert "buffers" not in document
    assert "bufferViews" not in document


def test_an_unlit_shader_declares_the_khronos_unlit_extension():
    _, document, binary = _publish(b'"UnlitGeneric"\n{\n"$additive" "1"\n}\n', present=())
    assert "KHR_materials_unlit" in document["extensionsUsed"]
    assert document["materials"][0]["alphaMode"] == "BLEND"
    validation.validate_document(document, binary)


def test_alpha_test_becomes_a_masked_core_material():
    _, document, _ = _publish(
        b'"VertexLitGeneric"\n{\n"$alphatest" "1"\n"$nocull" "1"\n}\n', present=()
    )
    assert document["materials"][0]["alphaMode"] == "MASK"
    assert document["materials"][0]["doubleSided"]


def test_the_validator_refuses_a_byte_ledger_gap():
    _, document, binary = _publish(SIMPLE)
    ledger = document["extensions"][MATERIAL_EXTENSION]["coverage"]["byteLedger"][0]
    ledger["ranges"][0]["length"] = int(ledger["ranges"][0]["length"]) - 1
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_refuses_an_opaque_source_blob():
    _, document, binary = _publish(SIMPLE)
    document["extensions"][MATERIAL_EXTENSION]["sourceText"] = "..."
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_dependency_without_a_binding():
    _, document, binary = _publish(SIMPLE)
    document["extensions"][MATERIAL_EXTENSION]["dependencies"].append(
        {"role": "texture", "parameter": "$bumpmap", "asset": "vtmb:texture:invented"}
    )
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_refuses_an_unknown_source_anomaly():
    _, document, binary = _publish(SIMPLE)
    document["extensions"][MATERIAL_EXTENSION]["anomalies"] = [{"role": "invented"}]
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_the_validator_refuses_a_proxy_naming_a_missing_parameter():
    _, document, binary = _publish(SIMPLE)
    document["extensions"][MATERIAL_EXTENSION]["proxies"] = [
        {"index": 0, "name": "sine", "sourceName": "Sine", "parameters": [99]}
    ]
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary)


def test_prepublication_validation_rechecks_source_bytes():
    closure = _closure(SIMPLE)
    model = decode_material(closure, texture_exists=lambda value: True)
    document, binary = material_glb.build_document(model)
    tampered = MaterialSourceClosure(
        closure.material_path,
        closure.asset_id,
        SourceMember("vmt", closure.vmt.path, SIMPLE + b" ", {"kind": "synthetic"}),
    )
    with pytest.raises(validation.MaterialGlbValidationError):
        validation.validate_document(document, binary, source_members=tampered.members())


def test_the_public_exporter_publishes_the_expected_relative_path():
    index = {"materials/synthetic/material.vmt": ("loose", "synthetic"),
             "materials/models/teeth.tth": ("loose", "synthetic")}

    def read_bytes(_index, key):
        return SIMPLE if key == "materials/synthetic/material.vmt" else None

    with tempfile.TemporaryDirectory() as root:
        destination = material_glb.export(
            index, "materials/synthetic/material.vmt", Path(root), read_bytes=read_bytes
        )
        assert destination.relative_to(root).as_posix() == "synthetic/material.glb"
        summary = validation.validate(destination)
        assert summary["asset"] == "vtmb:material:synthetic/material"
        assert summary["dependencies"] == 1


def test_a_published_container_is_one_json_chunk():
    with tempfile.TemporaryDirectory() as root:
        destination = Path(root) / "unit.glb"
        document, binary = material_glb.build_document(_decode(SIMPLE))
        material_glb.write_glb(document, binary, destination)
        raw = destination.read_bytes()
        magic, version, total = struct.unpack_from("<III", raw)
        assert (magic, version, total) == (0x46546C67, 2, len(raw))
        size, kind = struct.unpack_from("<II", raw, 12)
        assert kind == 0x4E4F534A
        assert 20 + size == len(raw)
        json.loads(raw[20:20 + size].decode("utf-8").rstrip(" "))


def test_an_unresolved_texture_and_a_grammar_departure_both_warn():
    _, document, binary = _publish(
        b'"LightmappedGeneric"\n{\n"$basetexture" "models/gone"\n"nomip"\n}\n', present=()
    )
    summary = validation.validate_document(document, binary)
    warnings = validation.warnings_for(summary)
    assert len(warnings) == 2
    assert "models/gone" in warnings[0]
    assert "valueless-key" in warnings[1]


def test_a_complete_unit_warns_about_nothing():
    _, document, binary = _publish(RESOLVED)
    assert validation.warnings_for(validation.validate_document(document, binary)) == []


def test_an_untranscribed_selector_warns():
    """A family whose rule is not recovered says so rather than guessing a program."""
    _, document, binary = _publish(UNTRANSCRIBED)
    warnings = validation.warnings_for(validation.validate_document(document, binary))
    assert len(warnings) == 1
    assert "no transcribed selector" in warnings[0]


# --------------------------------------------------------------------------------------------
# SF-1.4: PAKFILE patched map materials as units resolving $envmap to their probe.
# --------------------------------------------------------------------------------------------

#: The exact shape every compiler-generated patched map material carries in the real corpus
#: (confirmed against `sp_tutorial_1`'s PAKFILE): a real `Patch` shader, `include` the base, and a
#: `replace` block overriding `$envmap` to the baked probe's texture identity.
PATCHED_BODY = (
    b'"patch"\r\n{\r\n\t"include"\t\t"materials/plaster/wall.vmt"\r\n\t"replace"\r\n\t{\r\n'
    b'\t\t"$envmap"\t\t"maps/testmap/c1_2_3"\r\n\t}\r\n}\r\n'
)
BASE_BODY = b'"LightmappedGeneric"\r\n{\r\n\t"$basetexture" "plaster/wall"\r\n}\r\n'


def _patched_material_install():
    """A synthetic install: a loose base material and one map BSP with its patched copy.

    The PAKFILE's probe `c1_2_3.tth` is present so `$envmap` has something to resolve against;
    its bytes are never decoded by the material seam, only its presence is asked about.
    """

    bsp = _bsp_with_pakfile(
        {
            "materials/maps/testmap/plaster/wall_1_2_3.vmt": PATCHED_BODY,
            "materials/maps/testmap/c1_2_3.tth": b"probe-bytes",
        }
    )
    index = {
        "maps/testmap.bsp": ("loose", "synthetic/maps/testmap.bsp"),
        "materials/plaster/wall.vmt": ("loose", "synthetic/plaster/wall.vmt"),
    }
    files = {
        "maps/testmap.bsp": bsp,
        "materials/plaster/wall.vmt": BASE_BODY,
    }
    return index, files


def test_source_keys_lists_install_materials_and_pakfile_patched_copies():
    index, files = _patched_material_install()
    keys = material_source.source_keys(index, read_bytes=lambda _index, key: files.get(key))
    assert "plaster/wall" in keys
    assert "maps/testmap/plaster/wall_1_2_3" in keys
    assert keys == sorted(keys)


def test_patched_material_round_trips_and_resolves_its_envmap(tmp_path):
    index, files = _patched_material_install()
    destination = material_glb.export(
        index,
        "maps/testmap/plaster/wall_1_2_3",
        tmp_path,
        read_bytes=lambda _index, key: files.get(key),
    )
    summary = validation.validate(destination)
    assert summary["asset"] == "vtmb:material:maps/testmap/plaster/wall_1_2_3"
    assert summary["missingTextures"] == []

    raw = destination.read_bytes()
    size = struct.unpack_from("<I", raw, 12)[0]
    document = json.loads(raw[20:20 + size].decode("utf-8").rstrip(" "))
    extension = document["extensions"][MATERIAL_EXTENSION]

    member = extension["sourceResolution"]["members"][0]
    assert member["origin"]["kind"] == "bsp-pakfile"
    assert member["origin"]["map"] == "testmap"
    assert member["origin"]["member"] == "materials/maps/testmap/plaster/wall_1_2_3.vmt"
    assert member["origin"]["origin"] == {"kind": "loose", "root": "loose"}

    envmap = next(
        row for row in extension["textureBindings"] if row["parameter"] == "$envmap"
    )
    assert envmap["resolved"] is True
    assert envmap["asset"] == "vtmb:texture:maps/testmap/c1_2_3"

    envmap_dependency = next(
        row for row in extension["dependencies"]
        if row.get("role") == "texture" and row.get("parameter") == "$envmap"
    )
    assert envmap_dependency["asset"] == "vtmb:texture:maps/testmap/c1_2_3"
    assert envmap_dependency["resolved"] is True

    assert extension["patchOf"] == {
        "asset": "vtmb:material:plaster/wall",
        "cubemapOrigin": [1, 2, 3],
    }
    assert any(
        row.get("role") == "material" and row.get("asset") == "vtmb:material:plaster/wall"
        for row in extension["dependencies"]
    )
    # The patched copy is a real `Patch` shader, so the base edge is stated once, not twice.
    material_rows = [row for row in extension["dependencies"] if row.get("role") == "material"]
    assert len(material_rows) == 1


def test_load_source_closure_prefers_an_install_member_over_the_pakfile_patched_copy():
    """Finding 5: an install member under `materials/maps/**` wins over the PAKFILE patched
    copy of the same spelling."""

    index, files = _patched_material_install()
    index = dict(index)
    files = dict(files)
    key = "materials/maps/testmap/plaster/wall_1_2_3.vmt"
    index[key] = ("loose", "synthetic/install-patch.vmt")
    files[key] = b'"LightmappedGeneric"\r\n{\r\n\t"$basetexture" "install/wins"\r\n}\r\n'
    closure = material_source.load_source_closure(
        index, "maps/testmap/plaster/wall_1_2_3", read_bytes=lambda _index, k: files.get(k)
    )
    assert closure.vmt.data == files[key]
    assert closure.vmt.path == key
    assert closure.vmt.origin["kind"] == "loose"


def test_load_source_closure_rejects_a_flat_maps_key_with_no_stem():
    """Finding 5: `maps/<name>` with nothing after it names no map-relative stem to resolve."""

    index, files = _patched_material_install()
    with pytest.raises(material_source.MaterialSourceError, match="stem"):
        material_source.load_source_closure(
            index, "maps/testmap", read_bytes=lambda _index, key: files.get(key)
        )
