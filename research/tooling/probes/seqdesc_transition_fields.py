"""Whether VtMB's v2531 `StudioSeqDesc` carries a per-sequence transition-node quad.

Usage:
    uv run elysium research seqdesc_transition_fields [--limit N] [--report PATH]

LIFE2 S5 (`docs/project/plans/life2-action-catalog.md`) asks whether the 764-byte
`StudioSeqDesc` carries HL1's `entrynode`/`exitnode`/`nodeflags`/`nextseq` quad, and
whether the header carries a model-level `numlocalnodes`/`localnodeindex` pair
addressing an n x n adjacency matrix.

The header question is already closed by `docs/vtmb/mdl_v2531.md`: the slot modern
Source spends on `numlocalnodes`/`localnodeindex`/`localnodenameindex` is occupied by
VtMB's own `SequencesIndexed`@280/`NumSeqGroups`@284/`SeqGroupIndex`@288 (a demand-load
sequence-group record, not a node table), and `NumTransitions`@336 is 0 on all 4,445
models. This probe covers the descriptor question only.

`docs/vtmb/animation_and_movers.md` A.3 claims every StudioSeqDesc field except three
byte spans left over inside the 764-byte stride: [604,612), [624,660) and [668,764) --
the gaps between `paramend`@596..604, the transition-fade triple@612..624, the
autolayer pair@660..668 and the descriptor's own end. This sweeps every 4-byte slot in
those three spans, across all v2531 sequence descriptors on every installed model, and
reports whether any of them behaves like a bounded small-integer node index (nonzero,
in-range across the corpus, and correlated with the sequences whose label carries
`_to_`, which HL1 content author as literal transition clips).
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path
from typing import Any

from elysium_pipeline.formats import install
from elysium_pipeline.paths import research_root

REPORT_NAME = "seqdesc-transition-fields.json"

MDL_VERSION = 2531
SEQ_STRIDE = 764

#: The three byte spans A.3's field table leaves unclaimed inside StudioSeqDesc.
UNCLAIMED_SPANS = ((604, 612), (624, 660), (668, 764))


def _i32(d: bytes, o: int) -> int:
    return struct.unpack_from("<i", d, o)[0]


def _candidate_offsets() -> list[int]:
    offsets: list[int] = []
    for start, end in UNCLAIMED_SPANS:
        offsets.extend(range(start, end, 4))
    return offsets


def sweep(limit: int | None = None) -> dict[str, Any]:
    idx = install.build_index()
    keys = sorted(
        key for key in idx if key.startswith("models/") and key.endswith(".mdl")
    )
    if limit is not None:
        keys = keys[:limit]

    offsets = _candidate_offsets()
    counts = {
        "models_seen": 0,
        "models_unreadable": 0,
        "models_wrong_version": 0,
        "sequences_seen": 0,
        "sequences_with_to_in_label": 0,
    }
    nonzero: Counter = Counter()
    in_small_range: Counter = Counter()  # 0 < value < 64 -- a plausible node count
    values: dict[int, Counter] = {o: Counter() for o in offsets}
    to_examples: list[dict[str, Any]] = []

    for key in keys:
        data = install.read(idx, key)
        if not data or len(data) < 424:
            counts["models_unreadable"] += 1
            continue
        if _i32(data, 4) != MDL_VERSION:
            counts["models_wrong_version"] += 1
            continue
        counts["models_seen"] += 1

        num_seq = _i32(data, 272)
        seq_index = _i32(data, 276)
        if num_seq <= 0 or seq_index <= 0:
            continue
        size = len(data)

        for i in range(num_seq):
            sb = seq_index + i * SEQ_STRIDE
            if sb + SEQ_STRIDE > size:
                break
            counts["sequences_seen"] += 1

            label_end = data.find(b"\0", sb)
            label = data[sb:label_end].decode("ascii", "replace") if label_end != -1 else ""
            has_to = "_to_" in label.lower()
            if has_to:
                counts["sequences_with_to_in_label"] += 1

            row: dict[str, Any] = {}
            for offset in offsets:
                value = _i32(data, sb + offset)
                values[offset][value] += 1
                if value != 0:
                    nonzero[offset] += 1
                if 0 < value < 64:
                    in_small_range[offset] += 1
                row[offset] = value

            if has_to and len(to_examples) < 32:
                to_examples.append({"model": key, "label": label, "nonzero_fields": {
                    str(o): v for o, v in row.items() if v != 0
                }})

    slots = []
    for offset in offsets:
        common = values[offset].most_common(4)
        slots.append(
            {
                "offset": offset,
                "sequences_nonzero": nonzero[offset],
                "sequences_in_small_range": in_small_range[offset],
                "most_common_values": [
                    {"value": value, "sequences": total} for value, total in common
                ],
            }
        )

    return {
        "counts": counts,
        "unclaimed_spans": list(UNCLAIMED_SPANS),
        "candidate_slots": slots,
        "to_labeled_examples": to_examples,
    }


def summarize(report: dict[str, Any]) -> str:
    counts = report["counts"]
    lines = [
        f"models {counts['models_seen']:,} seen ({MDL_VERSION} only), "
        f"sequences {counts['sequences_seen']:,}, "
        f"{counts['sequences_with_to_in_label']:,} with '_to_' in the label",
        "",
        "unclaimed 4-byte slots (spans %s):" % (report["unclaimed_spans"],),
    ]
    any_nonzero = False
    for slot in report["candidate_slots"]:
        if slot["sequences_nonzero"]:
            any_nonzero = True
        common = ", ".join(
            f"{row['value']}x{row['sequences']}" for row in slot["most_common_values"][:3]
        )
        lines.append(
            f"    +{slot['offset']:<4d} nonzero on {slot['sequences_nonzero']:6,}, "
            f"small-range on {slot['sequences_in_small_range']:6,}   [{common}]"
        )
    if not any_nonzero:
        lines.append("")
        lines.append("every unclaimed slot is 0 on every sequence descriptor in the install.")
    if report["to_labeled_examples"]:
        lines.append("")
        lines.append("'_to_' labeled sequences with a nonzero unclaimed field:")
        for ex in report["to_labeled_examples"]:
            lines.append(f"    {ex['model']} :: {ex['label']} -> {ex['nonzero_fields']}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    report = sweep(limit=args.limit)
    destination = args.report or (research_root() / REPORT_NAME)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(summarize(report))
    print(f"\n  report: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
