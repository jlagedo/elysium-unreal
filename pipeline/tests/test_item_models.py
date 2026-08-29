"""Item ground-model export contract: enumeration, the landing stem, and skip-on-missing.

Every `vdata/items` definition and every `.mdl` here is synthesised in-code, so nothing depends on
the user's game install.
"""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from elysium_pipeline.exporters import UE_extract_items as items
from elysium_pipeline.exporters.UE_bsp_to_scene import decode_prop_models
from elysium_pipeline.formats import mdl


REPO = Path(__file__).resolve().parents[2]


def _definition(playermodel: str, *, sole_root: bool = True) -> str:
    """One item definition. `sole_root` is the shipped shape, which `kv.parse` unwraps to the
    WeaponData contents; the other keeps `WeaponData` as a named child."""
    body = f'\t"viewmodel"\t"models/weapons/v_ignored.mdl"\n\t"playermodel"\t"{playermodel}"\n'
    block = f"WeaponData\n{{\n{body}}}\n"
    return block if sole_root else f'"schema"\t"1"\n{block}'


class _Install:
    """A synthetic patch-first index: install key -> file on disk, read through install.read."""

    def __init__(self, root: Path):
        self.root = root
        self.index: dict[str, tuple[str, str]] = {}

    def add(self, key: str, text: str = "") -> None:
        path = self.root / key.replace("/", "_")
        path.write_text(text, encoding="ascii")
        self.index[key] = ("loose", str(path))

    def item(self, classname: str, playermodel: str, *, sole_root: bool = True) -> None:
        self.add(f"vdata/items/{classname}.txt", _definition(playermodel, sole_root=sole_root))


def test_distinct_models_dedupe_and_carry_every_classname() -> None:
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item("item_k_gimble_key", "models/items/Key/Ground/Key.mdl")
        install.item("item_k_malcolm_office_key", "models\\items\\key\\ground\\key.mdl")
        install.item("item_g_ring_gold", "models/items/Rings/Ground/Ring01.mdl")

        found = items.ground_models(install.index)

        assert found == {
                "models/items/key/ground/key.mdl": [
                    "item_k_gimble_key",
                    "item_k_malcolm_office_key",
                ],
                "models/items/rings/ground/ring01.mdl": ["item_g_ring_gold"],
            }


def test_an_extensionless_playermodel_resolves_as_a_model() -> None:
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item(
            "item_w_throwing_star", "models/weapons/throwing_star/ground/g_throwing_star"
        )

        assert list(items.ground_models(install.index)) == ["models/weapons/throwing_star/ground/g_throwing_star.mdl"]


def test_a_definition_with_no_ground_model_contributes_nothing() -> None:
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item("item_a_body_armor", "")
        install.item("item_d_dominate", "   ")
        install.add("vdata/items/notes.dat", "ignored")
        install.add("vdata/system/feats.txt", _definition("models/items/x/y.mdl"))

        assert items.ground_models(install.index) == {}


def test_weapondata_reads_whether_or_not_the_parser_unwrapped_it() -> None:
    # kv.parse unwraps a single leading root key, so a shipped file arrives already AS the
    # WeaponData contents. A file with a second top-level key does not unwrap, and the same
    # definition still has to be found.
    with tempfile.TemporaryDirectory() as root:
        install = _Install(Path(root))
        install.item("item_g_stake", "models/items/stake/ground/stake.mdl", sole_root=True)
        install.item("item_g_watch", "models/items/watch/ground/watch.mdl", sole_root=False)

        assert sorted(items.ground_models(install.index)) == [
                "models/items/stake/ground/stake.mdl",
                "models/items/watch/ground/watch.mdl",
            ]


def test_the_corpus_stem_is_the_whole_model_path_folded() -> None:
    # The stem is the model path folded, not its base filename: two `pendant.mdl` under
    # different directories are different items and must not collide. Reached through the map
    # exporter's own decoder, which is where the rule lives.
    keys = [
        "models/items/rings/ground/ring03.mdl",
        "models/items/occult/ground/pendant.mdl",
        "models/items/occult_gargoyle/ground/pendant.mdl",
    ]
    expected = {key: mdl.sanitize(key[:-4]) for key in keys}
    with tempfile.TemporaryDirectory() as propdir:
        # Every stem pre-declared, so the decoder answers with its naming and reads no model.
        resolved, ok, missing = decode_prop_models(
            {}, keys, propdir, {}, set(expected.values())
        )

    assert resolved == expected
    assert (ok, missing) == (len(keys), 0)
    assert len(set(expected.values())) == len(keys)
    assert expected["models/items/rings/ground/ring03.mdl"] == "models_items_rings_ground_ring03"


class ExportRunTests(unittest.TestCase):
    def _run(self, install: _Install, out: Path, decoded: dict[str, str], faces: int = 3):
        """Run main() against a corpus holding `decoded`, returning the manifest it wrote.

        The exporter decodes nothing now -- `UE_extract_corpus` does -- so the fixture stands the
        corpus meshes up on disk and this asserts the join it writes over them.
        """
        props = out / "shared" / "props"
        props.mkdir(parents=True, exist_ok=True)
        for stem in decoded.values():
            (props / f"{stem}.obj").write_text(
                "v 0 0 0\n" + "f 1 1 1\n" * faces, encoding="ascii")

        with mock.patch.object(items, "OUT", str(out)):
            items.main(index=install.index)
        return json.loads((out / "items" / "ground_models.json").read_text(encoding="utf-8"))

    def test_a_model_the_install_lacks_is_recorded_rather_than_fatal(self) -> None:
        with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
            install = _Install(Path(root))
            install.item("item_g_stake", "models/items/stake/ground/stake.mdl")
            install.item("item_w_pistol", "models/weapons/pistol/world/w_pistol.mdl")
            install.item("item_w_pistol-null", "models/weapons/pistol/world/w_pistol.mdl")
            install.add("models/items/stake/ground/stake.mdl", "IDST")

            manifest = self._run(
                install,
                Path(out),
                {"models/items/stake/ground/stake.mdl": "models_items_stake_ground_stake"},
            )

            assert list(manifest["models"]) == ["models/items/stake/ground/stake.mdl"]
            assert manifest["skipped"] == [
                    {
                        "model": "models/weapons/pistol/world/w_pistol.mdl",
                        "reason": "not in the install",
                        "classes": ["item_w_pistol", "item_w_pistol-null"],
                    }
                ]

    def test_a_model_present_but_undecodable_is_named_as_such(self) -> None:
        with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
            install = _Install(Path(root))
            install.item("item_g_stake", "models/items/stake/ground/stake.mdl")
            install.add("models/items/stake/ground/stake.mdl", "not a model")

            manifest = self._run(install, Path(out), {})

            assert manifest["models"] == {}
            assert [row["reason"] for row in manifest["skipped"]] == ["decode failed"]

    def test_a_geometry_free_model_is_recorded_with_no_faces(self) -> None:
        # `models/weapons/w_null.mdl` decodes cleanly and writes no triangles; the bake authors no
        # asset for it, so a consumer must be able to tell it apart from a decode that failed.
        with tempfile.TemporaryDirectory() as root, tempfile.TemporaryDirectory() as out:
            install = _Install(Path(root))
            install.item("item_a_body_armor_slot", "models/weapons/w_null.mdl")
            install.add("models/weapons/w_null.mdl", "IDST")

            manifest = self._run(
                install,
                Path(out),
                {"models/weapons/w_null.mdl": "models_weapons_w_null"},
                faces=0,
            )

            row = manifest["models"]["models/weapons/w_null.mdl"]
            assert row["faces"] == 0
            assert row["stem"] == "models_weapons_w_null"
