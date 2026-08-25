"""The include tree's flat sequence numbering, on synthetic images.

The engine builds one flat sequence space per body: it walks the include tree depth-first and
concatenates each model's `NumLocalSeq`@272 descriptors, and a model reachable by several include
paths contributes its block at every reference. `LookupSequence` scans that space and answers the
first match, so the number a clip carries -- and which bank owns a label several banks declare --
falls out of the walk rather than out of any single file.

Every image here is synthesised in-code, so nothing depends on the user's game install.
"""

from __future__ import annotations

import os
import struct
import tempfile
import unittest

# `mdl_skel` imports cleanly without an install, but the exporters beside it resolve the VtMB
# root as they load; an existing directory is all the import needs and nothing here reads it.
os.environ.setdefault("ELYSIUM_VTMB_ROOT", tempfile.gettempdir())

from elysium_pipeline.formats import mdl_skel as S  # noqa: E402

SEQ_STRIDE = 764
GROUP_STRIDE = 116

_SEQ_BASE = 512
_GROUP_BASE = 8192


def _image(labels=(), includes=()):
    """A v2531 studiohdr carrying `labels` as its own descriptors and `includes` as its banks."""
    strings_at = _GROUP_BASE + max(len(includes), 1) * GROUP_STRIDE
    image = bytearray(strings_at)
    struct.pack_into("<4sI", image, 0, b"IDST", 2531)
    struct.pack_into("<ii", image, 272, len(labels), _SEQ_BASE)
    struct.pack_into("<ii", image, 404, len(includes), _GROUP_BASE)

    strings = bytearray()

    def intern(text):
        offset = strings_at + len(strings)
        strings.extend(text.encode("ascii") + b"\0")
        return offset

    for index, label in enumerate(labels):
        seq_at = _SEQ_BASE + index * SEQ_STRIDE
        struct.pack_into("<i", image, seq_at, intern(label) - seq_at)

    for index, path in enumerate(includes):
        group_at = _GROUP_BASE + index * GROUP_STRIDE
        struct.pack_into("<i", image, group_at, intern(path) - group_at)

    return bytes(image + strings)


def _loader(models):
    """`load(key)` over a dict of `models/`-rooted keys, counting each read."""
    reads = []

    def load(key):
        reads.append(key)
        return models.get(key.lower())

    return load, reads


class FirstReferenceBasesTests(unittest.TestCase):
    def test_a_lone_model_starts_at_zero(self) -> None:
        load, _ = _loader({"models/body.mdl": _image(labels=("idle", "walk"))})
        self.assertEqual([(k, b) for k, _, b in S.first_reference_bases(load, "models/body.mdl")],
                         [("models/body.mdl", 0)])

    def test_each_bank_starts_where_the_one_before_it_ended(self) -> None:
        load, _ = _loader({
            "models/body.mdl": _image(labels=("ragdoll",),
                                      includes=("models/a.mdl", "models/b.mdl")),
            "models/a.mdl": _image(labels=("run", "walk")),
            "models/b.mdl": _image(labels=("idle",)),
        })
        self.assertEqual(
            [(k, b) for k, _, b in S.first_reference_bases(load, "models/body.mdl")],
            [("models/body.mdl", 0), ("models/a.mdl", 1), ("models/b.mdl", 3)])

    def test_the_walk_is_depth_first_so_a_banks_own_banks_precede_its_sibling(self) -> None:
        load, _ = _loader({
            "models/body.mdl": _image(includes=("models/a.mdl", "models/b.mdl")),
            "models/a.mdl": _image(labels=("run",), includes=("models/deep.mdl",)),
            "models/deep.mdl": _image(labels=("crouch", "crawl")),
            "models/b.mdl": _image(labels=("idle",)),
        })
        self.assertEqual(
            [(k, b) for k, _, b in S.first_reference_bases(load, "models/body.mdl")],
            [("models/body.mdl", 0), ("models/a.mdl", 0), ("models/deep.mdl", 1),
             ("models/b.mdl", 3)])

    def test_a_model_reached_twice_records_its_first_appearance(self) -> None:
        # The whole reason this cannot be `resolve_tree`: the counter advances over the repeat,
        # so everything numbered after it sits past both blocks, while the entry the walk keeps
        # is the first one -- which is the block a first-match lookup answers.
        load, _ = _loader({
            "models/body.mdl": _image(includes=("models/a.mdl", "models/b.mdl")),
            "models/a.mdl": _image(labels=("run",), includes=("models/shared.mdl",)),
            "models/shared.mdl": _image(labels=("x", "y", "z")),
            "models/b.mdl": _image(labels=("idle",), includes=("models/shared.mdl",)),
        })
        bases = {k: b for k, _, b in S.first_reference_bases(load, "models/body.mdl")}
        self.assertEqual(bases["models/shared.mdl"], 1)
        # body(0) + a(1) + shared(3) + b(1) puts the second `shared` block at 5, so `models/b.mdl`
        # is numbered behind the repeat rather than behind one copy of it.
        self.assertEqual(bases["models/b.mdl"], 4)

    def test_a_repeat_is_counted_even_though_it_yields_no_entry(self) -> None:
        load, _ = _loader({
            "models/body.mdl": _image(includes=("models/shared.mdl", "models/shared.mdl",
                                                "models/last.mdl")),
            "models/shared.mdl": _image(labels=("x", "y")),
            "models/last.mdl": _image(labels=("z",)),
        })
        bases = {k: b for k, _, b in S.first_reference_bases(load, "models/body.mdl")}
        self.assertEqual(bases["models/shared.mdl"], 0)
        self.assertEqual(bases["models/last.mdl"], 4)

    def test_a_cycle_is_refused_rather_than_walked_forever(self) -> None:
        load, _ = _loader({
            "models/a.mdl": _image(labels=("one",), includes=("models/b.mdl",)),
            "models/b.mdl": _image(labels=("two",), includes=("models/a.mdl",)),
        })
        self.assertEqual([(k, b) for k, _, b in S.first_reference_bases(load, "models/a.mdl")],
                         [("models/a.mdl", 0), ("models/b.mdl", 1)])

    def test_a_bank_the_loader_cannot_read_numbers_nothing(self) -> None:
        # A missing bank contributes neither an entry nor a count, so the numbering of everything
        # after it is what it would be if the include were not authored at all.
        load, _ = _loader({
            "models/body.mdl": _image(labels=("ragdoll",),
                                      includes=("models/absent.mdl", "models/b.mdl")),
            "models/b.mdl": _image(labels=("idle",)),
        })
        self.assertEqual(
            [(k, b) for k, _, b in S.first_reference_bases(load, "models/body.mdl")],
            [("models/body.mdl", 0), ("models/b.mdl", 1)])

    def test_the_order_projection_answers_the_same_walk(self) -> None:
        models = {
            "models/body.mdl": _image(includes=("models/a.mdl", "models/b.mdl")),
            "models/a.mdl": _image(labels=("run",), includes=("models/shared.mdl",)),
            "models/shared.mdl": _image(labels=("x",)),
            "models/b.mdl": _image(labels=("idle",), includes=("models/shared.mdl",)),
        }
        load, _ = _loader(models)
        ordered = S.first_reference_order(load, "models/body.mdl")
        load, _ = _loader(models)
        based = S.first_reference_bases(load, "models/body.mdl")
        self.assertEqual([k for k, _ in ordered], [k for k, _, _ in based])

    def test_a_model_is_read_once_however_many_paths_reach_it(self) -> None:
        load, reads = _loader({
            "models/body.mdl": _image(includes=("models/a.mdl", "models/b.mdl")),
            "models/a.mdl": _image(includes=("models/shared.mdl",)),
            "models/b.mdl": _image(includes=("models/shared.mdl",)),
            "models/shared.mdl": _image(labels=("x",)),
        })
        S.first_reference_bases(load, "models/body.mdl")
        self.assertEqual(reads.count("models/shared.mdl"), 1)


class GlobalNumberTests(unittest.TestCase):
    """A clip's number is its owner's base plus its own on-disk position."""

    def test_a_descriptors_number_is_its_base_plus_its_position(self) -> None:
        load, _ = _loader({
            "models/body.mdl": _image(labels=("ragdoll",), includes=("models/bank.mdl",)),
            "models/bank.mdl": _image(labels=("run", "walk", "sneak")),
        })
        numbers = {}
        for key, data, base in S.first_reference_bases(load, "models/body.mdl"):
            for index, label in enumerate(S.local_sequence_labels(data)):
                numbers.setdefault(label.lower(), base + index)
        self.assertEqual(numbers, {"ragdoll": 0, "run": 1, "walk": 2, "sneak": 3})

    def test_a_label_a_model_declares_twice_answers_its_first_descriptor(self) -> None:
        load, _ = _loader({"models/bank.mdl": _image(labels=("run", "walk", "run"))})
        _, data, base = S.first_reference_bases(load, "models/bank.mdl")[0]
        positions = {}
        for index, label in enumerate(S.local_sequence_labels(data)):
            positions.setdefault(label.lower(), index)
        self.assertEqual(base + positions["run"], 0)

    def test_the_positional_list_keeps_a_label_the_playable_read_would_drop(self) -> None:
        # `local_sequence_labels` is positional and unskipped, which is what a global sequence
        # number indexes; `local_sequences` dedups by lowercased label and skips a descriptor
        # whose base cell is out of range, so a number read off it would name the wrong clip.
        load, _ = _loader({"models/bank.mdl": _image(labels=("run", "RUN", "walk"))})
        _, data, _ = S.first_reference_bases(load, "models/bank.mdl")[0]
        self.assertEqual(S.local_sequence_labels(data), ["run", "RUN", "walk"])


if __name__ == "__main__":
    unittest.main()
