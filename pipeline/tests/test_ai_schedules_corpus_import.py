"""`uv run elysium import ai-schedules`, over hand-built units.

The lane is the first to deploy a **derived product** -- a file that is a function of the unit
rather than a capsule of a source member -- so these check both kinds of byte through the same
stamp, read-back and prune the shared machinery gives every lane.
"""

from __future__ import annotations

import json

import pytest

from elysium_pipeline.formats.ai_schedule_glb import AI_SCHEDULE_EXTENSION
from elysium_pipeline.formats.unit_contract import (
    asset_block,
    buffer_table,
    coverage_block,
    encapsulate,
    extension_root,
    identity_block,
    plain,
    source_resolution,
    write_glb,
)
from elysium_pipeline.formats.unit_contract.origin import Origin, SourceMember
from elysium_pipeline.importers import ai_schedules
from elysium_pipeline.importers.ai_schedules import import_ai_schedules, target_of

ORIGIN = Origin(kind="loose", root="Vampire")
IMAGE = "dlls/vampire.dll"


def _space_unit(root_dir, key: str, texts: dict[str, bytes]) -> None:
    members = [
        SourceMember(
            role="schedule-text",
            path=f"{IMAGE}#ai/schedules/{key}/{name}.sch",
            data=body,
            origin=ORIGIN,
            span=(offset * 1000, len(body)),
        )
        for offset, (name, body) in enumerate(sorted(texts.items()))
    ]
    resolution, views, binary = encapsulate(members)
    root = extension_root(
        schema_version="1.0.0",
        identity=identity_block(
            f"vtmb:ai-schedule:{key}",
            IMAGE,
            className=key.upper(),
            classNames=[key.upper()],
            initBody="0x10367a40",
        ),
        source_resolution=resolution,
        dependencies=[],
        coverage=coverage_block(mapped=["identity"]),
        spaces={"schedule": {"address": "0x1093a740", "parentUnit": "cnpc_vvampire"}},
        registrations={"schedule": [{"name": "SCHED_X", "localId": 344}]},
        texts=[{"order": 0, "name": "SCHED_X"}],
        records=[],
    )
    document = {
        "asset": asset_block("AI Schedule"),
        "extensionsUsed": [AI_SCHEDULE_EXTENSION],
        "extensionsRequired": [AI_SCHEDULE_EXTENSION],
    }
    if binary:
        document["buffers"] = buffer_table(binary)
        document["bufferViews"] = views
    document["extensions"] = {AI_SCHEDULE_EXTENSION: plain(root)}
    write_glb(document, binary, root_dir / "ai-schedules" / f"{key}.glb")


def _root_unit(root_dir) -> None:
    """The vocabulary unit: an image member, NO capsule, everything it deploys derived."""

    member = SourceMember(role="image", path=IMAGE, data=b"MZ" + b"\0" * 64, origin=ORIGIN)
    root = extension_root(
        schema_version="1.0.0",
        identity=identity_block("vtmb:ai-schedule:vocabulary", IMAGE, role="vocabulary"),
        source_resolution=source_resolution([member]),
        dependencies=[],
        coverage=coverage_block(mapped=["identity"]),
        partition=[],
        namespaces=[{"category": "schedule", "idSeed": 1000000000}],
        squadSlots=[{"name": "SQUAD_SLOT_ATTACK1", "globalId": 1000000000}],
        vocabulary={"state": {"values": {"IDLE": 1}}},
        classes=[{"className": "CNPC_VBrujah", "unit": "cnpc_vbrujah"}],
        order={"rule": "base, then Troika, then the species"},
        deadDoors=[],
        census={"texts": 2},
    )
    document = {
        "asset": asset_block("AI Schedule"),
        "extensionsUsed": [AI_SCHEDULE_EXTENSION],
        "extensionsRequired": [AI_SCHEDULE_EXTENSION],
        "extensions": {AI_SCHEDULE_EXTENSION: plain(root)},
    }
    write_glb(document, b"", root_dir / "ai-schedules" / "vocabulary.glb")


@pytest.fixture
def exports(tmp_path):
    root = tmp_path / "exports_v2"
    _space_unit(root, "cnpc_vbrujah", {"sched_a": b"Schedule SCHED_A Tasks TASK_WAIT 1"})
    _root_unit(root)
    return root


def test_a_member_path_deploys_to_the_tail_after_the_hash():
    assert target_of(f"{IMAGE}#ai/schedules/cnpc_vbrujah/sched_a.sch") == (
        "ai/schedules/cnpc_vbrujah/sched_a.sch",
    )


def test_the_image_member_deploys_nowhere():
    # The root unit names the whole image; it is not a file the runtime opens.
    assert target_of(IMAGE) == ()


def test_the_lane_deploys_texts_and_both_derived_products(exports, tmp_path):
    corpus = tmp_path / "corpus"
    result = import_ai_schedules(exports, corpus)
    assert not result.failures
    assert result.units == 2

    text = corpus / "ai" / "schedules" / "cnpc_vbrujah" / "sched_a.sch"
    assert text.read_bytes() == b"Schedule SCHED_A Tasks TASK_WAIT 1"

    sidecar = json.loads((corpus / "ai" / "schedules" / "cnpc_vbrujah" / "space.json").read_text())
    assert sidecar["registrations"]["schedule"][0]["name"] == "SCHED_X"
    assert sidecar["spaces"]["schedule"]["parentUnit"] == "cnpc_vvampire"

    vocabulary = json.loads((corpus / "ai" / "schedules" / "vocabulary.json").read_text())
    assert vocabulary["classes"][0]["unit"] == "cnpc_vbrujah"
    assert vocabulary["namespaces"][0]["idSeed"] == 1000000000


def test_the_root_unit_deploys_although_it_carries_no_capsule(exports, tmp_path):
    # It names an executable image as its member, so the contract's executable-image rule lets it
    # publish without one. Everything it deploys is derived.
    corpus = tmp_path / "corpus"
    import_ai_schedules(exports, corpus)
    assert (corpus / "ai" / "schedules" / "vocabulary.json").is_file()


def test_a_derived_product_is_a_pure_function_of_the_unit(exports, tmp_path):
    first = import_ai_schedules(exports, tmp_path / "a")
    second = import_ai_schedules(exports, tmp_path / "b")
    assert not first.failures and not second.failures
    left = (tmp_path / "a" / "ai" / "schedules" / "vocabulary.json").read_bytes()
    right = (tmp_path / "b" / "ai" / "schedules" / "vocabulary.json").read_bytes()
    assert left == right


def test_a_second_run_over_an_unchanged_corpus_writes_nothing(exports, tmp_path):
    corpus = tmp_path / "corpus"
    import_ai_schedules(exports, corpus)
    again = import_ai_schedules(exports, corpus)
    assert again.written == 0
    assert again.units_current == 2


def test_an_orphaned_text_is_pruned(exports, tmp_path):
    corpus = tmp_path / "corpus"
    import_ai_schedules(exports, corpus)
    orphan = corpus / "ai" / "schedules" / "cnpc_vbrujah" / "sched_retired.sch"
    orphan.write_bytes(b"Schedule SCHED_RETIRED Tasks TASK_WAIT 1")
    result = import_ai_schedules(exports, corpus)
    assert result.pruned >= 1
    assert not orphan.exists()


def test_the_lane_owns_only_its_own_subtree(tmp_path):
    # `ai/schedules`, not `ai`, so the rest of a future `ai/` tree stays another lane's.
    assert ai_schedules.LANE_SPEC.owned_directories == ("ai/schedules",)
