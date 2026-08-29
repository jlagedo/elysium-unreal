"""Synthetic contract tests for the isolated Surface-property GLB exporter."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from elysium_pipeline.exporters import surface_property_glb
from elysium_pipeline.formats.surface_property_glb import (
    SURFACE_PROPERTY_EXTENSION,
    lexer,
    source,
)
from elysium_pipeline.formats.surface_property_glb.decode import decode_surface_property
from elysium_pipeline.validation import surface_property_glb as validation

TABLE = (
    b'// the table\r\n'
    b'"default"\r\n{\r\n'
    b'\t"density"\t"2000"\r\n'
    b'\t"climbable"\t"0"\r\n'
    b'\t"stepleft"\t"Surfaces/Concrete/StepLeft1.wav"\r\n'
    b'\t"stepleft"\t"Surfaces/Concrete/StepLeft2.wav"\r\n'
    b'\t"bullet_norm_impact"\t"Surfaces/norm1.wav"\r\n'
    b'\t"impact"\t"Default.Impact"\r\n'
    b'\t"gamematerial"\t"C"\r\n'
    b'}\r\n\r\n'
    b'// a derived surface\r\n'
    b'"Flesh"\r\n{\r\n'
    b'\t"base"\t"default"\r\n'
    b'\t"bulletimpact"\t"Surfaces/legacy.wav"\r\n'
    b'}\r\n'
    b'"weapon"\r\n{\r\n}\r\n'
)

INDEX = {source.TABLE_PATH: ("loose", "C:/game/Vampire/scripts/surfaceproperties.txt")}


def _table(body: bytes = TABLE):
    return source.load_table(INDEX, read_bytes=lambda index, key: body)


def _decode(name: str, body: bytes = TABLE, *, scripts=("default.impact",)):
    table = _table(body)
    return decode_surface_property(
        table.closure(name),
        base_exists=lambda candidate: candidate in table.spans,
        sound_script_exists=lambda candidate: candidate.lower() in scripts,
    )


def _publish(name: str, body: bytes = TABLE, **kwargs):
    model = _decode(name, body, **kwargs)
    document, binary = surface_property_glb.build_document(model)
    return model, document, binary


class SurfacePropertyTableTests(unittest.TestCase):
    def test_the_table_partitions_into_entries_comments_and_whitespace(self):
        table = _table()
        self.assertEqual(table.names, ("default", "flesh", "weapon"))
        self.assertEqual(table.spans["flesh"][0], "Flesh")
        for name in table.names:
            offset, end = table.spans[name][1], table.spans[name][2]
            self.assertEqual(table.data[offset:offset + 1], b'"')
            self.assertEqual(table.data[end - 1:end], b"}")

    def test_a_token_outside_every_entry_refuses_the_table(self):
        with self.assertRaises(source.SurfacePropertySourceError):
            _table(TABLE + b'"stray"\r\n')

    def test_a_name_declared_twice_refuses_the_table(self):
        with self.assertRaises(source.SurfacePropertySourceError):
            _table(TABLE + b'"DEFAULT"\r\n{\r\n}\r\n')

    def test_a_nested_block_refuses_the_table(self):
        with self.assertRaises(source.SurfacePropertySourceError):
            _table(b'"default"\r\n{\r\n\t"sounds"\r\n\t{\r\n\t}\r\n}\r\n')

    def test_sound_script_names_come_from_the_scripts_own_depth(self):
        script_table = (
            b'"Bottle.Impact"\r\n{\r\n\t"rndwave"\r\n\t{\r\n\t\t"wave" "a.wav"\r\n\t}\r\n}\r\n'
            b'"Default.Scrape"\r\n{\r\n\t"volume" "1"\r\n}\r\n'
        )
        index = {source.SOUND_SCRIPT_PATH: ("loose", "C:/game/scripts/x.txt")}
        names = source.load_sound_script_names(
            index, read_bytes=lambda idx, key: script_table
        )
        self.assertEqual(names, frozenset({"bottle.impact", "default.scrape"}))


class SurfacePropertyLedgerTests(unittest.TestCase):
    def test_every_entry_byte_is_claimed_exactly_once(self):
        model = _decode("default")
        ledger = model.byte_coverage[0]
        self.assertEqual(ledger["coveragePercent"], 100.0)
        cursor = 0
        for row in ledger["ranges"]:
            self.assertEqual(row["offset"], cursor)
            cursor += row["length"]
        self.assertEqual(cursor, ledger["byteLength"])
        self.assertEqual(ledger["byteLength"], model.sources[0].byte_length)

    def test_whitespace_is_the_only_omission(self):
        model = _decode("default")
        self.assertEqual(set(model.byte_coverage[0]["stateBytes"]), {"mapped", "omitted-proven"})

    def test_a_tokenizer_run_covers_the_source_gaplessly(self):
        cursor = 0
        for token in lexer.tokenize(lexer.decode_text(TABLE)):
            self.assertEqual(token.offset, cursor)
            cursor = token.end
        self.assertEqual(cursor, len(TABLE))


class SurfacePropertyDecodeTests(unittest.TestCase):
    def test_the_unit_key_folds_case_and_keeps_the_source_spelling(self):
        model = _decode("FLESH")
        self.assertEqual(model.name, "flesh")
        self.assertEqual(model.source_name, "Flesh")
        self.assertEqual(model.asset_id, "vtmb:surface-property:flesh")

    def test_a_repeated_footstep_key_stays_a_variation_pool(self):
        model = _decode("default")
        self.assertEqual(
            [record["path"] for record in model.footsteps["left"]],
            ["Surfaces/Concrete/StepLeft1.wav", "Surfaces/Concrete/StepLeft2.wav"],
        )
        self.assertNotIn("right", model.footsteps)

    def test_physics_movement_and_game_material_land_in_their_own_fields(self):
        model = _decode("default")
        self.assertEqual(model.physics, {"density": 2000.0})
        self.assertEqual(model.movement, {"climbable": False})
        self.assertEqual(model.game_material, "C")

    def test_the_impact_matrix_is_indexed_by_weapon_and_outcome(self):
        model = _decode("default")
        self.assertEqual(
            [record["path"] for record in model.impacts["bullet"]["norm"]],
            ["Surfaces/norm1.wav"],
        )
        legacy = _decode("flesh")
        self.assertEqual([record["path"] for record in legacy.impacts["legacy"]],
                         ["Surfaces/legacy.wav"])

    def test_base_resolves_against_the_table_and_names_a_dependency(self):
        model = _decode("flesh")
        self.assertEqual(model.base["name"], "default")
        self.assertTrue(model.base["resolved"])
        self.assertIn(
            {
                "role": "surface-property",
                "asset": "vtmb:surface-property:default",
                "sourcePath": "scripts/surfaceproperties.txt#default",
            },
            model.dependencies,
        )
        self.assertEqual(model.unresolved, [])

    def test_a_base_no_entry_defines_leaves_the_unit_incomplete(self):
        table = _table()
        model = decode_surface_property(table.closure("flesh"), base_exists=lambda name: False)
        self.assertEqual([row["reason"] for row in model.unresolved],
                         ["base-names-no-defined-surface"])

    def test_a_sound_script_absent_from_the_install_publishes_unresolved(self):
        model = _decode("default", scripts=())
        self.assertFalse(model.sounds["impact"][0]["resolved"])
        self.assertEqual(model.unresolved, [])

    def test_an_entry_that_declares_only_its_name_is_a_complete_unit(self):
        model = _decode("weapon")
        self.assertEqual(model.parameters, [])
        self.assertEqual(model.dependencies, [])
        self.assertEqual(model.unsupported, [])

    def test_a_key_outside_the_vocabulary_is_reported_rather_than_dropped(self):
        model = _decode("default", b'"default"\r\n{\r\n\t"bulletdecal"\t"x"\r\n}\r\n')
        self.assertEqual([row["key"] for row in model.unsupported], ["bulletdecal"])


class SurfacePropertyDocumentTests(unittest.TestCase):
    def test_the_published_document_carries_no_scene_and_no_binary(self):
        _, document, binary = _publish("default")
        self.assertEqual(binary, b"")
        for name in validation.FORBIDDEN_CORE:
            self.assertNotIn(name, document)
        self.assertEqual(document["extensionsRequired"], [SURFACE_PROPERTY_EXTENSION])

    def test_every_shipped_shape_validates(self):
        for name in ("default", "flesh", "weapon"):
            _, document, binary = _publish(name)
            summary = validation.validate_document(document, binary)
            self.assertEqual(summary["byteCoveragePercent"], 100.0)

    def test_an_unsupported_key_fails_validation(self):
        _, document, binary = _publish("default", b'"default"\r\n{\r\n\t"bulletdecal"\t"x"\r\n}\r\n')
        with self.assertRaises(validation.SurfacePropertyGlbValidationError):
            validation.validate_document(document, binary)

    def test_a_dependency_no_reference_produced_fails_validation(self):
        _, document, binary = _publish("default")
        extension = document["extensions"][SURFACE_PROPERTY_EXTENSION]
        extension["dependencies"].append(
            {"role": "sound", "asset": "vtmb:sound:invented.wav", "sourcePath": "invented.wav"}
        )
        with self.assertRaises(validation.SurfacePropertyGlbValidationError):
            validation.validate_document(document, binary)

    def test_an_unresolved_sound_script_warns_the_operator(self):
        _, document, binary = _publish("default", scripts=())
        summary = validation.validate_document(document, binary)
        self.assertEqual(summary["missingSoundScripts"], ["Default.Impact"])
        self.assertTrue(validation.warnings_for(summary))

    def test_the_written_unit_reads_back_through_the_file_validator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            destination = surface_property_glb.export(
                INDEX,
                "Flesh",
                root,
                read_bytes=lambda index, key: TABLE,
                sound_scripts=frozenset({"default.impact"}),
            )
            self.assertEqual(destination, root / "flesh.glb")
            summary = validation.validate(destination)
            self.assertEqual(summary["asset"], "vtmb:surface-property:flesh")
            self.assertEqual(summary["base"], "default")


if __name__ == "__main__":
    unittest.main()
