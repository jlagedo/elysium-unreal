"""R8 oracle input independence, using small synthetic v2531 installed members."""

import ast
import json
from pathlib import Path
import struct

import pytest

from elysium_pipeline.formats import mdl_skel as S
from elysium_pipeline.validation import compose_diff as cd, graph_identity as gi
from elysium_pipeline.validation import oracle_source as src, retail_compositor as rc


def mdl_image(labels=("idle",), includes=(), weights=((1.0, 0.0, 1.0),), cells=None):
    """Three bones; one or more uncompressed held-pose animation records."""
    bone_base = 512
    anim_base = bone_base + 3 * 160
    seq_base = anim_base + len(weights) * 72
    group_base = seq_base + len(labels) * 764
    records_base = group_base + len(includes) * 116
    blob = bytearray(records_base + len(weights) * 3 * 32)
    struct.pack_into("<4sI", blob, 0, b"IDST", 2531)
    struct.pack_into("<ii", blob, 240, 3, bone_base)
    struct.pack_into("<ii", blob, 264, len(weights), anim_base)
    struct.pack_into("<ii", blob, 272, len(labels), seq_base)
    struct.pack_into("<ii", blob, 404, len(includes), group_base)

    def string(at, value):
        struct.pack_into("<i", blob, at, len(blob) - at)
        blob.extend(value.encode("ascii") + b"\0")

    for i, name in enumerate(("Bip01", "Hair", "Held")):
        at = bone_base + i * 160
        string(at, name)
        struct.pack_into("<i", blob, at + 4, -1 if i == 0 else 0)
        struct.pack_into("<3f", blob, at + 32, float(i), 0.0, 0.0)
        struct.pack_into("<4f", blob, at + 44, 0.0, 0.0, 0.0, 1.0)
    for i, row in enumerate(weights):
        at = anim_base + i * 72
        string(at, f"@sample{i}")
        struct.pack_into("<f", blob, at + 4, 30.0)
        struct.pack_into("<i", blob, at + 12, 2)
        records = records_base + i * 3 * 32
        struct.pack_into("<i", blob, at + 48, records - at)
        for bi, weight in enumerate(row):
            struct.pack_into("<f", blob, records + bi * 32, weight)
    for i, label in enumerate(labels):
        at = seq_base + i * 764
        if label:
            string(at, label)
        struct.pack_into("<i", blob, at + 52, 1)
        struct.pack_into("<2i", blob, at + 572, 1, 1)
        struct.pack_into("<2i", blob, at + 580, -1, -1)
        struct.pack_into("<h", blob, at + 56, (cells or {}).get(i, 0))
        struct.pack_into("<i", blob, at + 740, -1)
    for i, inc in enumerate(includes):
        string(group_base + i * 116, inc)
    return bytes(blob)


def source_for(images):
    reads = []

    def read(index, key):
        reads.append(key)
        return index.get(key)

    return src.OracleSource(index=images, read=read), reads


def test_capture_aliases_and_raw_paths_do_not_mix_male_and_female():
    male = "models/character/pc/male/malkavian/malkavian_male_armor_0.mdl"
    female = "models/character/pc/female/malkavian/malkavian_female_armor_0.mdl"
    bank = "models/character/shared/male/move_and_ranged.mdl"
    source, reads = source_for(dict.fromkeys((male, female, bank), mdl_image()))
    assert source.model_key("malkavian_male_armor_0") == male
    assert source.model_key("MALKAVIAN_FEMALE_ARMOR_0") == female
    assert source.model_key("character_shared_male_move_and_ranged") == bank
    assert source.model_key(bank.upper().replace("/", "\\")) == bank
    assert source.model_key("vtmb:model:" + bank[7:-4]) == bank
    assert reads == []  # Name resolution never reads a catalogue or a payload.


def test_collisions_refuse_but_full_paths_and_path_folded_names_resolve():
    source, _ = source_for({"models/a/body.mdl": mdl_image(), "models/b/body.mdl": mdl_image()})
    with pytest.raises(KeyError, match="ambiguous.*models/a/body.mdl.*models/b/body.mdl"):
        source.model_key("body")
    assert source.model_key("a_body") == "models/a/body.mdl"
    with pytest.raises(KeyError, match="no installed MDL"):
        source.model_key("body_extra")


def test_numbering_counts_repeated_blocks_and_raw_local_positions():
    source, reads = source_for({
        "models/body.mdl": mdl_image(("root",), ("models/a.mdl", "models/a.mdl", "models/b.mdl")),
        "models/a.mdl": mdl_image(("Run", "RUN", "", "broken", "walk"), cells={3: -1}),
        "models/b.mdl": mdl_image(("run", "last")),
    })
    assert source.resolve_global("body", 1) == ("a", "Run")
    assert source.resolve_global("body", 2) == ("a", "RUN")
    assert source.resolve_global("body", 3) == (None, None)
    assert source.resolve_global("body", 4) == ("a", "broken")
    assert source.resolve_global("body", 11) == ("b", "run")
    assert source.resolve_global("body", 12) == ("b", "last")
    assert source.resolve_global("body", 6) == (None, None)  # Repeat is not a lookup identity.
    assert source.find_sequence("body", "rUn") == ("a", "Run")
    assert source.sequence_labels("body")[11] == "Run"
    with pytest.raises(KeyError, match="does not play"):
        source.find_sequence("body", "broken")
    assert sorted(reads) == ["models/a.mdl", "models/b.mdl", "models/body.mdl"]


def test_include_cycles_and_missing_banks_do_not_consume_extra_numbers():
    source, reads = source_for({
        "models/body.mdl": mdl_image(("root",), ("models/missing.mdl", "models/a.mdl")),
        "models/a.mdl": mdl_image(("run",), ("models/body.mdl",)),
    })
    assert source.sequence_labels("body") == {0: "root", 1: "run"}
    assert source.find_sequence("body", "run") == ("a", "run")
    assert len(reads) == 3


def test_header_tracks_keep_owned_bind_bones_and_exclude_zero_weight(monkeypatch):
    source, reads = source_for({"models/bank.mdl": mdl_image(
        weights=((1.0, 0.0, 0.5), (0.0, 1.0, 0.0)))})
    def no_decode(*args):
        pytest.fail("track census decoded poses")
    monkeypatch.setattr(S, "read_anim", no_decode)
    assert source.tracked("bank", "IDLE") == {"Bip01", "Held"}
    assert source.tracked("bank", "@sample1") == {"Hair"}
    assert source.tracked("bank", "missing") is None
    assert source.tracked("bank", "idle@invented-host") is None
    assert source.tracked("bank", "idle") == {"Bip01", "Held"}
    assert reads == ["models/bank.mdl"]


def test_header_ownership_agrees_with_low_level_decode():
    data = mdl_image()
    source, _ = source_for({"models/bank.mdl": data})
    bones = S.read_bones(data)
    anim = S.find_anim(data, "sample0")
    frame = S.read_anim(data, bones, anim[0], 1)[0]
    assert source.tracked("bank", "idle") == {
        bone.name for bone, (_pos, quat) in zip(bones, frame) if any(quat)}


def test_sequence_track_set_uses_its_declared_base_cell():
    source, _ = source_for({"models/bank.mdl": mdl_image(
        weights=((1.0, 0.0, 1.0), (0.0, 1.0, 0.0)), cells={0: 1})})
    assert source.tracked("bank", "idle") == {"Hair"}
    assert source.tracked("bank", "sample0") == {"Bip01", "Held"}


def test_empty_mask_is_known_empty_not_unknown():
    source, _ = source_for({"models/bank.mdl": mdl_image(weights=((0.0, 0.0, 0.0),))})
    banks = cd._Banks(source)
    assert banks.tracked("bank", "idle") == set()
    assert banks.tracked("bank", "absent") is None
    assert banks.tracked("absent", "idle") is None


def test_corrupt_member_is_refused():
    source, _ = source_for({"models/bad.mdl": b"not a model"})
    with pytest.raises(ValueError, match="not a v2531"):
        source.data("bad")


def test_default_source_uses_winning_install_index(tmp_path, monkeypatch):
    monkeypatch.setenv("ELYSIUM_VTMB_ROOT", str(tmp_path))
    from elysium_pipeline.formats import install
    game, patch = tmp_path / "Vampire", tmp_path / "Unofficial_Patch"
    game.mkdir()
    (patch / "models").mkdir(parents=True)
    (patch / "models" / "body.mdl").write_bytes(mdl_image(("patched",)))
    monkeypatch.setattr(install, "GAME", str(game))
    monkeypatch.setattr(install, "LOOSE_ROOTS", [str(patch)])
    monkeypatch.setattr(install.vpk, "index_all", lambda game: {"models/body.mdl": "packed"})
    monkeypatch.setattr(install.vpk, "extract", lambda entry: pytest.fail("read shadowed VPK"))
    install.invalidate_index_cache()
    try:
        source = src.OracleSource()
        assert source.index["models/body.mdl"][0] == "loose"
        assert source.find_sequence("body", "patched") == ("body", "patched")
    finally:
        install.invalidate_index_cache()


def test_compositor_and_emit_work_without_any_export_inputs(tmp_path, monkeypatch):
    from elysium_pipeline import paths
    def no_exports():
        pytest.fail("oracle asked for export root")
    monkeypatch.setattr(paths, "export_root", no_exports)
    source, _ = source_for({"models/body.mdl": mdl_image()})
    corpus = rc.Corpus(source)
    assert rc._find_sequence(corpus, "body", "IDLE")[0] == "body"
    assert corpus.resolve_global("body", 0) == ("body", "idle")
    assert cd._sequence_labels("body", source) == {0: "idle"}
    output = rc.emit_oracle(corpus, "body", ["idle"], tmp_path)
    report = json.loads(output.read_text())
    assert report["schema"] == "elysium-oracle-1"
    assert report["frames"][0]["owner"] == "body"
    assert report["frames"][0]["bones"]["Held"] == [5.08, 0.0, 0.0]


def test_capture_join_keeps_exact_stem_clock_crossfade_and_motion_rules(tmp_path, monkeypatch):
    source, _ = source_for({"models/female.mdl": mdl_image()})
    matrix = [1, 0, 0, 2, 0, 1, 0, 0, 0, 0, 1, 0]
    pose = {"curtime": 1.000001, "stem": "female", "bones": {"Bip01": matrix},
            "player_state": {"sequence": 0, "velocity": [10, 0, 0]}}
    (tmp_path / "pose_oracle.json").write_text(json.dumps({"frames": [
        pose, dict(pose, stem="male"), dict(pose, curtime=2)]}))
    (tmp_path / "layer_oracle.json").write_text(json.dumps({"frames": [
        {"curtime": 1.000002, "stem": "female", "channels": [
            {"label": "idle", "weight": 0.5, "additive": False},
            {"label": "gun_attack_layer", "weight": 1, "additive": False}]}]}))
    monkeypatch.setattr(cd, "_sessions", lambda: [tmp_path])
    frames, unjoined = cd._retail_frames("female", source)
    assert len(frames) == 1 and unjoined == 1
    assert frames[0]["base"] == "idle"
    assert frames[0]["overlays"] == ["gun_attack_layer"]
    assert frames[0]["cross_fading"] and frames[0]["moving"]
    assert frames[0]["positions"]["Bip01"].tolist() == [5.08, 0, 0]


@pytest.mark.parametrize("module,args", [(rc, ["--validate", "capture"]), (gi, ["--run", "run"])])
def test_entrypoints_no_longer_require_export_root(monkeypatch, module, args):
    marker = object()
    monkeypatch.setattr(rc, "Corpus", lambda: marker)
    monkeypatch.setattr(rc, "validate_session", lambda corpus, *a, **kw: 0 if corpus is marker else 1)
    monkeypatch.setattr(gi, "score", lambda corpus, path: 0 if corpus is marker else 1)
    assert module.main(args) == 0
    with pytest.raises(SystemExit) as exc:
        module.main([*args, "--export-root", "forbidden"])
    assert exc.value.code == 2


@pytest.mark.parametrize("module", [src, rc, gi, cd])
def test_oracle_modules_do_not_import_producers_or_read_legacy_products(module):
    tree = ast.parse(Path(module.__file__).read_text(encoding="utf-8"))
    for node in ast.walk(tree):
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            names = ([node.module or ""] if isinstance(node, ast.ImportFrom) else [])
            names += [alias.name for alias in node.names]
            assert not any(any(part in name for part in (
                "exporters", "npc_export", "eskm", "skeletal_stage", "importers", "model_glb"))
                for name in names)
        if isinstance(node, ast.Call):
            name = node.func.attr if isinstance(node.func, ast.Attribute) else getattr(node.func, "id", "")
            assert name != "export_root"
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            assert node.value not in {"npc", "items", "--export-root"}
            assert not node.value.endswith((".body.json", ".eskm", ".glb"))
