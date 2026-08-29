"""Which banks can answer a label, and which declared bank reaches nobody.

A bank's decode succeeding is not the same as a bank being playable. The bake reads
`npc/banks/<stem>.eskm` and writes `_banks/<stem>/A_<label>` from it, so a bank the
container plan never wrote answers nothing at runtime however completely it decoded --
the twelve per-clan PC banks decode `ragdoll` and every clan charsheet fidget, uniquely
own none of them, and so are planned by nothing. A clip map that named one handed the
runtime an owner whose asset does not exist.

Nothing here reads the user's install: both decisions are pure, and the container census
runs against a temporary directory.
"""

from __future__ import annotations

import os
import tempfile
import unittest

# `npc_export` imports `install`, which resolves ELYSIUM_VTMB_ROOT as it loads; an existing
# directory is all the import needs, and nothing here reads it.
os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

from elysium_pipeline.exporters.npc_export import (  # noqa: E402
    answerable_owner,
    bank_containers_present,
    unreferenced_banks,
)


def test_an_absent_directory_is_an_empty_census_rather_than_an_error() -> None:
    with tempfile.TemporaryDirectory() as npc_dir:
        assert bank_containers_present(npc_dir) == set()


class AnswerableOwnerTests(unittest.TestCase):
    CONTAINERS = {"fists", "katana"}

    def test_a_bank_with_a_container_answers_and_one_without_does_not(self) -> None:
        assert answerable_owner("body", "fists", self.CONTAINERS, True)
        assert not answerable_owner("body", "pc_br", self.CONTAINERS, True)

    def test_a_body_always_answers_its_own_clips(self) -> None:
        # The body's own container is a different file and a different stage's failure; a body
        # missing from the bank census is not a body that cannot play its own dialogue.
        assert answerable_owner("body", "body", self.CONTAINERS, True)
        assert answerable_owner("body", "body", set(), False)

    def test_no_census_admits_every_owner(self) -> None:
        """The container stage not having run is an ordering fact, not a coverage failure.

        Filtering against an empty census would drop every shared clip in the cast, which is
        the whole vocabulary of every body.
        """
        assert answerable_owner("body", "pc_br", set(), False)


class UnreferencedBankTests(unittest.TestCase):
    CINEMATICS: dict = {}

    @staticmethod
    def _body(clips):
        return {"clips": clips}

    def test_a_container_backed_bank_nobody_names_is_orphaned(self) -> None:
        """The defect this census exists to catch: work the bake did for no reader."""
        orphaned, containerless = unreferenced_banks(
            {"fists": {}, "katana": {}},
            {"body": self._body({"jab": ["fists"]})},
            self.CINEMATICS,
            {"fists", "katana"},
            True,
        )
        assert orphaned == ["katana"]
        assert containerless == []

    def test_a_containerless_bank_nobody_names_is_the_filter_working(self) -> None:
        orphaned, containerless = unreferenced_banks(
            {"fists": {}, "pc_br": {}},
            {"body": self._body({"jab": ["fists"]})},
            self.CINEMATICS,
            {"fists"},
            True,
        )
        assert orphaned == []
        assert containerless == ["pc_br"]

    def test_a_bank_named_by_any_body_is_referenced(self) -> None:
        orphaned, containerless = unreferenced_banks(
            {"fists": {}},
            {"a": self._body({"jab": ["fists"]}), "b": self._body({})},
            self.CINEMATICS,
            {"fists"},
            True,
        )
        assert (orphaned, containerless) == ([], [])

    def test_a_non_first_owner_still_counts_as_a_reference(self) -> None:
        """A label several banks declare names every one of them, not just the first."""
        orphaned, containerless = unreferenced_banks(
            {"baseball": {}, "fists": {}},
            {"body": self._body({"stealth": ["baseball", "fists"]})},
            self.CINEMATICS,
            {"baseball", "fists"},
            True,
        )
        assert (orphaned, containerless) == ([], [])

    def test_a_pre_multi_owner_record_reads_as_a_single_reference(self) -> None:
        orphaned, _ = unreferenced_banks(
            {"fists": {}},
            {"body": self._body({"jab": "fists"})},
            self.CINEMATICS,
            {"fists"},
            True,
        )
        assert orphaned == []

    def test_a_cinematic_root_bank_is_referenced_by_its_scene(self) -> None:
        """No body's clip map names a cinematic bank; the scene's root list is its only reader."""
        orphaned, _ = unreferenced_banks(
            {"scene_bip01": {}},
            {"body": self._body({})},
            {"scene": {"stem": "scene", "roots": [{"root": "Bip01", "bank": "scene_bip01"}]}},
            {"scene_bip01"},
            True,
        )
        assert orphaned == []

    def test_without_a_census_an_unreferenced_bank_is_reported_as_orphaned(self) -> None:
        """Cannot-tell resolves toward the loud answer: an unread bank is still a defect."""
        orphaned, containerless = unreferenced_banks(
            {"fists": {}}, {"body": self._body({})}, self.CINEMATICS, set(), False)
        assert orphaned == ["fists"]
        assert containerless == []
