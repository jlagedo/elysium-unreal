"""Synthetic contract tests for the isolated Material GLB exporter."""

from __future__ import annotations

import json
from pathlib import Path
import struct
import tempfile
import unittest

from elysium_pipeline.exporters import material_glb
from elysium_pipeline.formats.material_glb import decode_material
from elysium_pipeline.formats.material_glb import lexer
from elysium_pipeline.formats.material_glb.model import MATERIAL_EXTENSION
from elysium_pipeline.formats.material_glb.source import MaterialSourceClosure, SourceMember
from elysium_pipeline.validation import material_glb as validation

SIMPLE = b'"VertexLitGeneric"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n}\r\n'
#: A family whose selector is transcribed from the binary, so it resolves to a program.
RESOLVED = b'"LightmappedGeneric"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n}\r\n'
#: A family whose selector is not transcribed, so it resolves to no program at all.
UNTRANSCRIBED = b'"Wireframe"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n}\r\n'


def _closure(body: bytes, path: str = "synthetic/material"):
    member = SourceMember("vmt", f"materials/{path}.vmt", body, {"kind": "synthetic"})
    return MaterialSourceClosure(path, f"vtmb:material:{path}", member)


def _decode(body: bytes, *, present=("models/teeth",), path="synthetic/material"):
    return decode_material(_closure(body, path), texture_exists=lambda value: value in present)


def _publish(body: bytes, **kwargs):
    model = _decode(body, **kwargs)
    document, binary = material_glb.build_document(model)
    return model, document, binary


class MaterialLedgerTests(unittest.TestCase):
    def test_every_source_byte_is_claimed_exactly_once(self):
        model = _decode(SIMPLE)
        ledger = model.byte_coverage[0]
        self.assertEqual(ledger["coveragePercent"], 100.0)
        self.assertEqual(ledger["accountedBytes"], len(SIMPLE))
        cursor = 0
        for row in ledger["ranges"]:
            self.assertEqual(row["offset"], cursor)
            cursor += row["length"]
        self.assertEqual(cursor, len(SIMPLE))

    def test_whitespace_is_the_only_omission_and_comments_are_kept(self):
        body = b'"UnlitGeneric"\n{\n\t"$additive" "1" // authored note\n}\n'
        model = _decode(body, present=())
        self.assertEqual([row["text"] for row in model.comments], ["// authored note"])
        states = model.byte_coverage[0]["stateBytes"]
        self.assertEqual(set(states), {"mapped", "omitted-proven"})

    def test_a_tokenizer_run_covers_the_source_gaplessly(self):
        text = lexer.decode_text(SIMPLE)
        cursor = 0
        for token in lexer.tokenize(text):
            self.assertEqual(token.offset, cursor)
            cursor = token.end
        self.assertEqual(cursor, len(SIMPLE))


class MaterialDecodeTests(unittest.TestCase):
    def test_parameters_keep_source_order_casing_and_offsets(self):
        body = b'"VertexLitGeneric"\n{\n"$baseTexture" "models/teeth"\n"$surfaceProp" "flesh"\n}\n'
        model = _decode(body)
        self.assertEqual([p.key for p in model.parameters], ["$basetexture", "$surfaceprop"])
        self.assertEqual([p.source_key for p in model.parameters],
                         ["$baseTexture", "$surfaceProp"])
        self.assertEqual([p.index for p in model.parameters], [0, 1])
        self.assertEqual(model.surface_property, "flesh")
        self.assertIn(
            {"role": "surface-property", "asset": "vtmb:surface-property:flesh",
             "sourcePath": "scripts/surfaceproperties.txt#flesh"},
            model.dependencies,
        )

    def test_a_repeated_proxy_stays_three_ordered_records(self):
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
        self.assertEqual([proxy.name for proxy in model.proxies], ["globalwetness"] * 3)
        channels = [proxy.parameters[0].value for proxy in model.proxies]
        self.assertEqual(channels, ["$envmaptint[0]", "$envmaptint[1]", "$envmaptint[2]"])

    def test_the_env_cubemap_placeholder_is_a_symbol_not_a_binding(self):
        body = b'"LightmappedGeneric"\n{\n"$envmap" "env_cubemap"\n}\n'
        model = _decode(body, present=())
        self.assertEqual(model.environment, {"parameter": "$envmap", "symbol": "env_cubemap"})
        self.assertEqual(model.texture_bindings, [])

    def test_an_absent_texture_binds_unresolved_without_a_dependency(self):
        body = b'"VertexLitGeneric"\n{\n"$basetexture" "models/gone"\n}\n'
        model = _decode(body, present=())
        self.assertEqual(len(model.texture_bindings), 1)
        self.assertFalse(model.texture_bindings[0]["resolved"])
        self.assertEqual(model.dependencies, [])

    def test_a_render_target_value_is_not_a_texture_dependency(self):
        body = b'"Water"\n{\n"$refracttexture" "_rt_WaterRefraction"\n}\n'
        model = _decode(body, present=())
        self.assertEqual(model.texture_bindings[0]["kind"], "render-target")
        self.assertIsNone(model.texture_bindings[0]["asset"])
        self.assertEqual(model.dependencies, [])

    def test_a_proxy_operand_is_not_mistaken_for_a_material_input(self):
        body = (
            b'"UnlitGeneric"\n{\n"Proxies"\n{\n"TextureScroll"\n'
            b'{"texturescrollvar" "$basetexture"}\n}\n}\n'
        )
        model = _decode(body, present=("models/teeth",))
        self.assertEqual(model.texture_bindings, [])
        self.assertEqual(model.dependencies, [])


class MalformedSourceTests(unittest.TestCase):
    """Eleven shipped VMTs are not well-formed. Each departure publishes with its evidence."""

    def _roles(self, model):
        return sorted({row["role"] for row in model.anomalies})

    def test_a_valueless_key_publishes_as_a_flag(self):
        body = b'"VertexLitGeneric"\r\n{\r\n\t"$basetexture" "models/teeth"\r\n\t"nomip"\r\n}\r\n'
        model = _decode(body)
        self.assertEqual(self._roles(model), ["valueless-key"])
        flag = model.parameters[-1]
        self.assertEqual((flag.key, flag.value, flag.value_type), ("nomip", "", "none"))
        self.assertEqual(model.byte_coverage[0]["coveragePercent"], 100.0)

    def test_a_block_unclosed_at_end_of_file_still_publishes(self):
        body = b'"Sprite"\n{\n"$basetexture" "models/teeth"\n"Proxies"\n{\n"Sine"\n{\n"rate" "1"\n}'
        model = _decode(body)
        self.assertIn("unclosed-block-at-end-of-file", self._roles(model))
        self.assertEqual(model.byte_coverage[0]["accountedBytes"], len(body))

    def test_a_stray_close_brace_after_the_shader_block_is_claimed(self):
        body = b'"UnlitGeneric"\n{\n"$basetexture" "models/teeth"\n}\n}\n'
        model = _decode(body)
        self.assertEqual(self._roles(model), ["content-after-shader-block"])
        self.assertEqual(model.byte_coverage[0]["accountedBytes"], len(body))

    def test_a_stray_quote_runs_to_end_of_file_without_losing_a_byte(self):
        body = b'"VertexLitGeneric"\n{\n"$nocull" 1"\t// added by psycho-a\n}\n'
        model = _decode(body, present=())
        self.assertIn("unterminated-quoted-string", self._roles(model))
        self.assertEqual(model.byte_coverage[0]["accountedBytes"], len(body))

    def test_a_commented_out_block_header_leaves_an_anonymous_block(self):
        body = (
            b'"LightmappedGeneric"\n{\n"Proxies"\n{\n'
            b'//\t"GaussianNoise"\n\t{\n\t"resultVar" "$temp"\n\t}\n}\n}\n'
        )
        model = _decode(body, present=())
        self.assertIn("anonymous-block", self._roles(model))
        self.assertEqual(model.byte_coverage[0]["coveragePercent"], 100.0)


class ShaderResolutionTests(unittest.TestCase):
    """Combo rules transcribed from `stdshader_dx8.dll`; see `docs/vtmb/shader_combos.md`."""

    def _resolution(self, body, present=("models/teeth", "models/mask")):
        return _decode(body, present=present).shader_resolution

    def _only(self, body, field, present=("models/teeth", "models/mask")):
        """The single program a family with no render-config axis resolves to."""
        programs = self._resolution(body, present=present)["programs"]
        self.assertEqual(len(programs), 1)
        return programs[0][field]

    def test_a_family_with_no_transcribed_selector_resolves_unresolved(self):
        resolution = self._resolution(UNTRANSCRIBED)
        self.assertEqual(resolution["family"], "wireframe")
        self.assertFalse(resolution["resolved"])
        self.assertEqual(resolution["programs"], [])
        self.assertEqual(resolution["reason"], "selector-not-transcribed")

    def test_a_plain_lightmapped_material_selects_the_base_program(self):
        resolution = self._resolution(RESOLVED)
        self.assertTrue(resolution["resolved"])
        self.assertEqual(len(resolution["programs"]), 1)
        self.assertEqual(resolution["programs"][0]["vertexShader"], "LightmappedGeneric")
        self.assertEqual(resolution["programs"][0]["pixelShader"], "LightmappedGeneric")
        self.assertEqual(resolution["programs"][0]["condition"], "")

    def test_the_eyes_family_leaves_the_overbright_choice_to_render_config(self):
        """`$vampire` picks the pair; the remaining choice is engine state, not the VMT."""
        body = b'"Eyes"\n{\n"$basetexture" "models/teeth"\n"$vampire" "1"\n}\n'
        programs = self._resolution(body)["programs"]
        self.assertEqual(
            {(p["condition"], p["pixelShader"]) for p in programs},
            {("overbright==2", "Eyes_Vampire_Overbright2"), ("", "Eyes_Vampire")},
        )
        self.assertTrue(all(p["vertexShader"] == "Eyes" for p in programs))

    def test_eyes_without_the_vampire_flag_selects_the_stock_pair(self):
        body = b'"Eyes"\n{\n"$basetexture" "models/teeth"\n"$iris" "models/mask"\n}\n'
        programs = self._resolution(body)["programs"]
        self.assertEqual({p["pixelShader"] for p in programs}, {"Eyes", "Eyes_Overbright2"})

    def test_teeth_draws_with_a_pixel_program_outside_its_own_family(self):
        """Teeth ships a vertex program but no pixel program; it reuses VertexLitTexture."""
        body = b'"Teeth"\n{\n"$basetexture" "models/teeth"\n}\n'
        programs = self._resolution(body)["programs"]
        self.assertEqual(
            {p["pixelShader"] for p in programs},
            {"VertexLitTexture", "VertexLitTexture_Overbright2"},
        )
        self.assertTrue(all(p["vertexShader"] == "Teeth" for p in programs))

    def test_an_envmap_mask_texture_overrides_base_alpha_env_map_mask(self):
        """Pixel table 0x10021620 indexes 6 and 7: a bound `$envmapmask` wins.

        `_BaseAlphaMaskedEnvMapV2` is therefore unreachable whenever both are authored -- a
        precedence rule the combo filenames do not state.
        """
        body = (
            b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$envmapmask" "models/mask"\n'
            b'"$basealphaenvmapmask" "1"\n}\n'
        )
        self.assertEqual(
            self._only(body, "pixelShader"), "LightmappedGeneric_MaskedEnvMapV2"
        )

    def test_base_alpha_env_map_mask_applies_when_no_mask_texture_is_bound(self):
        body = (
            b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$basealphaenvmapmask" "1"\n}\n'
        )
        self.assertEqual(
            self._only(body, "pixelShader"),
            "LightmappedGeneric_BaseAlphaMaskedEnvMapV2",
        )

    def test_the_env_cubemap_placeholder_still_counts_as_a_bound_environment(self):
        """VBSP patches the face to a baked cube, so the selector sees a texture."""
        body = (
            b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n}\n'
        )
        self.assertEqual(
            self._only(body, "pixelShader"), "LightmappedGeneric_EnvMapV2")

    def test_sphere_and_camera_space_flags_do_nothing_without_an_envmap(self):
        """Vertex table 0x100214f0 indexes 2, 4 and 6 all fall back to the plain program."""
        body = (
            b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmapsphere" "1"\n"$envmapcameraspace" "1"\n}\n'
        )
        self.assertEqual(self._only(body, "vertexShader"), "LightmappedGeneric")

    def test_sphere_overrides_camera_space_when_an_envmap_is_present(self):
        """Vertex table indexes 14 and 15 pick the sphere program."""
        body = (
            b'"LightmappedGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$envmapsphere" "1"\n"$envmapcameraspace" "1"\n}\n'
        )
        self.assertEqual(
            self._only(body, "vertexShader"), "LightmappedGeneric_EnvMapSphere"
        )

    def test_a_material_with_no_base_texture_selects_a_no_texture_program(self):
        body = b'"LightmappedGeneric"\n{\n"$selfillum" "1"\n}\n'
        self.assertEqual(
            self._only(body, "pixelShader", present=()), "LightmappedGeneric_NoTexture"
        )

    def test_a_bumpmapped_material_draws_the_envmap_in_a_second_pass(self):
        """VertexLitGeneric suppresses the envmap in pass 0 and adds it back in pass 1.

        The `_ps14` row is the same pass on hardware that supports ps.1.4.
        """
        body = (
            b'"VertexLitGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$bumpmap" "models/mask"\n}\n'
        )
        programs = self._resolution(body)["programs"]
        base = [p for p in programs if p["condition"] == "bumpmapping" and p["drawPass"] == 0]
        second = [p for p in programs if p["drawPass"] == 1]
        self.assertEqual(base[0]["pixelShader"], "VertexLitGeneric")
        self.assertEqual(
            {p["pixelShader"] for p in second},
            {"VertexLitGeneric_EnvmappedBumpmapV2",
             "VertexLitGeneric_EnvmappedBumpmapV2_ps14"},
        )

    def test_envmapoptional_deletes_the_envmap_outright(self):
        """The help text says "dx9 and higher", but VtMB ships no dx9 VertexLitGeneric."""
        body = (
            b'"VertexLitGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$envmapoptional" "1"\n}\n'
        )
        self.assertEqual(self._only(body, "pixelShader"), "VertexLitGeneric")

    def test_a_normal_map_alpha_mask_without_a_bumpmap_deletes_the_envmap(self):
        body = (
            b'"VertexLitGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$normalmapalphaenvmapmask" "1"\n}\n'
        )
        self.assertEqual(self._only(body, "pixelShader"), "VertexLitGeneric")

    def test_unlit_generic_base_alpha_mask_yields_to_a_bound_mask_texture(self):
        """The base-alpha branch sits ahead of the table and requires the mask to be absent."""
        masked = (
            b'"UnlitGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$envmapmask" "models/mask"\n'
            b'"$basealphaenvmapmask" "1"\n}\n'
        )
        self.assertEqual(self._only(masked, "pixelShader"), "UnlitGeneric_EnvMapMask")
        unmasked = (
            b'"UnlitGeneric"\n{\n"$basetexture" "models/teeth"\n'
            b'"$envmap" "env_cubemap"\n"$basealphaenvmapmask" "1"\n}\n'
        )
        self.assertEqual(
            self._only(unmasked, "pixelShader"), "UnlitGeneric_BaseAlphaMaskedEnvMap"
        )

    def test_a_sprite_render_mode_selects_its_program_pair(self):
        body = b'"Sprite"\n{\n"$basetexture" "models/teeth"\n"$spriterendermode" "5"\n}\n'
        self.assertEqual(self._only(body, "pixelShader"), "SpriteRenderTransAdd")
        self.assertEqual(self._only(body, "vertexShader"), "unlitgeneric_vertexcolor")

    def test_ignore_vertex_colors_only_changes_the_additive_sprite_mode(self):
        additive = (
            b'"Sprite"\n{\n"$spriterendermode" "5"\n"$ignorevertexcolors" "1"\n}\n'
        )
        self.assertEqual(self._only(additive, "vertexShader", present=()), "unlitgeneric")
        animated = (
            b'"Sprite"\n{\n"$spriterendermode" "7"\n"$ignorevertexcolors" "1"\n}\n'
        )
        self.assertEqual(
            self._only(animated, "vertexShader", present=()), "unlitgeneric_vertexcolor"
        )

    def test_the_one_unimplemented_sprite_mode_binds_no_program(self):
        """Mode 6 is `kRenderEnvironmental`; the shader warns and binds nothing."""
        body = b'"Sprite"\n{\n"$spriterendermode" "6"\n}\n'
        resolution = self._resolution(body, present=())
        self.assertFalse(resolution["resolved"])
        self.assertEqual(resolution["programs"], [])
        self.assertIn("binds-no-program", resolution["reason"])

    def test_the_validator_refuses_conditional_programs_with_no_default(self):
        _, document, binary = _publish(RESOLVED)
        resolution = document["extensions"][MATERIAL_EXTENSION]["shaderResolution"]
        resolution["programs"] = [
            {"pixelShader": "A", "vertexShader": "V", "condition": "overbright==2"},
            {"pixelShader": "B", "vertexShader": "V", "condition": "other"},
        ]
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)

    def test_the_validator_refuses_a_repeated_program_condition(self):
        _, document, binary = _publish(RESOLVED)
        resolution = document["extensions"][MATERIAL_EXTENSION]["shaderResolution"]
        resolution["programs"] = [
            {"pixelShader": "A", "vertexShader": "V", "condition": ""},
            {"pixelShader": "B", "vertexShader": "V", "condition": ""},
        ]
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)

    def test_the_validator_refuses_an_unresolved_shader_that_names_a_program(self):
        _, document, binary = _publish(UNTRANSCRIBED)
        document["extensions"][MATERIAL_EXTENSION]["shaderResolution"]["programs"] = [
            {"pixelShader": "invented", "vertexShader": "invented", "condition": ""}
        ]
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)


class MaterialGlbWriterTests(unittest.TestCase):
    def test_a_published_unit_validates_and_carries_no_scene_core(self):
        _, document, binary = _publish(SIMPLE)
        summary = validation.validate_document(document, binary)
        self.assertEqual(summary["asset"], "vtmb:material:synthetic/material")
        self.assertEqual(summary["byteCoveragePercent"], 100.0)
        for absent in ("images", "textures", "samplers", "scenes", "nodes", "meshes"):
            self.assertNotIn(absent, document)
        self.assertEqual(len(document["materials"]), 1)
        self.assertEqual(document["extensionsRequired"], [MATERIAL_EXTENSION])

    def test_the_unit_carries_no_bin_chunk_and_no_buffer(self):
        _, document, binary = _publish(SIMPLE)
        self.assertEqual(binary, b"")
        self.assertNotIn("buffers", document)
        self.assertNotIn("bufferViews", document)

    def test_an_unlit_shader_declares_the_khronos_unlit_extension(self):
        _, document, binary = _publish(b'"UnlitGeneric"\n{\n"$additive" "1"\n}\n', present=())
        self.assertIn("KHR_materials_unlit", document["extensionsUsed"])
        self.assertEqual(document["materials"][0]["alphaMode"], "BLEND")
        validation.validate_document(document, binary)

    def test_alpha_test_becomes_a_masked_core_material(self):
        _, document, _ = _publish(
            b'"VertexLitGeneric"\n{\n"$alphatest" "1"\n"$nocull" "1"\n}\n', present=()
        )
        self.assertEqual(document["materials"][0]["alphaMode"], "MASK")
        self.assertTrue(document["materials"][0]["doubleSided"])

    def test_the_validator_refuses_a_byte_ledger_gap(self):
        _, document, binary = _publish(SIMPLE)
        ledger = document["extensions"][MATERIAL_EXTENSION]["coverage"]["byteLedger"][0]
        ledger["ranges"][0]["length"] = int(ledger["ranges"][0]["length"]) - 1
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)

    def test_the_validator_refuses_an_opaque_source_blob(self):
        _, document, binary = _publish(SIMPLE)
        document["extensions"][MATERIAL_EXTENSION]["sourceText"] = "..."
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)

    def test_the_validator_refuses_a_dependency_without_a_binding(self):
        _, document, binary = _publish(SIMPLE)
        document["extensions"][MATERIAL_EXTENSION]["dependencies"].append(
            {"role": "texture", "parameter": "$bumpmap", "asset": "vtmb:texture:invented"}
        )
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)

    def test_the_validator_refuses_an_unknown_source_anomaly(self):
        _, document, binary = _publish(SIMPLE)
        document["extensions"][MATERIAL_EXTENSION]["anomalies"] = [{"role": "invented"}]
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)

    def test_the_validator_refuses_a_proxy_naming_a_missing_parameter(self):
        _, document, binary = _publish(SIMPLE)
        document["extensions"][MATERIAL_EXTENSION]["proxies"] = [
            {"index": 0, "name": "sine", "sourceName": "Sine", "parameters": [99]}
        ]
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary)

    def test_prepublication_validation_rechecks_source_bytes(self):
        closure = _closure(SIMPLE)
        model = decode_material(closure, texture_exists=lambda value: True)
        document, binary = material_glb.build_document(model)
        tampered = MaterialSourceClosure(
            closure.material_path,
            closure.asset_id,
            SourceMember("vmt", closure.vmt.path, SIMPLE + b" ", {"kind": "synthetic"}),
        )
        with self.assertRaises(validation.MaterialGlbValidationError):
            validation.validate_document(document, binary, source_members=tampered.members())

    def test_the_public_exporter_publishes_the_expected_relative_path(self):
        index = {"materials/synthetic/material.vmt": ("loose", "synthetic"),
                 "materials/models/teeth.tth": ("loose", "synthetic")}

        def read_bytes(_index, key):
            return SIMPLE if key == "materials/synthetic/material.vmt" else None

        with tempfile.TemporaryDirectory() as root:
            destination = material_glb.export(
                index, "materials/synthetic/material.vmt", Path(root), read_bytes=read_bytes
            )
            self.assertEqual(
                destination.relative_to(root).as_posix(), "synthetic/material.glb"
            )
            summary = validation.validate(destination)
            self.assertEqual(summary["asset"], "vtmb:material:synthetic/material")
            self.assertEqual(summary["dependencies"], 1)

    def test_a_published_container_is_one_json_chunk(self):
        with tempfile.TemporaryDirectory() as root:
            destination = Path(root) / "unit.glb"
            document, binary = material_glb.build_document(_decode(SIMPLE))
            material_glb.write_glb(document, binary, destination)
            raw = destination.read_bytes()
            magic, version, total = struct.unpack_from("<III", raw)
            self.assertEqual((magic, version, total), (0x46546C67, 2, len(raw)))
            size, kind = struct.unpack_from("<II", raw, 12)
            self.assertEqual(kind, 0x4E4F534A)
            self.assertEqual(20 + size, len(raw))
            json.loads(raw[20:20 + size].decode("utf-8").rstrip(" "))


class MaterialWarningTests(unittest.TestCase):
    def test_an_unresolved_texture_and_a_grammar_departure_both_warn(self):
        _, document, binary = _publish(
            b'"LightmappedGeneric"\n{\n"$basetexture" "models/gone"\n"nomip"\n}\n', present=()
        )
        summary = validation.validate_document(document, binary)
        warnings = validation.warnings_for(summary)
        self.assertEqual(len(warnings), 2)
        self.assertIn("models/gone", warnings[0])
        self.assertIn("valueless-key", warnings[1])

    def test_a_complete_unit_warns_about_nothing(self):
        _, document, binary = _publish(RESOLVED)
        self.assertEqual(
            validation.warnings_for(validation.validate_document(document, binary)), []
        )

    def test_an_untranscribed_selector_warns(self):
        """A family whose rule is not recovered says so rather than guessing a program."""
        _, document, binary = _publish(UNTRANSCRIBED)
        warnings = validation.warnings_for(validation.validate_document(document, binary))
        self.assertEqual(len(warnings), 1)
        self.assertIn("no transcribed selector", warnings[0])


if __name__ == "__main__":
    unittest.main()
