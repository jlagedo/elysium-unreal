"""What the v2531 `studiohdr` carries that nothing decodes yet.

Usage:
    uv run elysium research studiohdr_unclaimed_fields [--limit N] [--report PATH]

The +396/+400 bone-chain table was found as an unlabeled count/index pair: two
ints in a slot modern Source assigns other meanings to, whose values behaved like
an array address on every model that carried them. `docs/vtmb/mdl_v2531.md` maps
the rest of the header but leaves gaps, and a Troika extension would look exactly
like one of them.

This sweeps every 4-byte slot in the header that the owning document does not
claim and asks, of each, whether it behaves like a **count/index pair**:

    * the second int resolves inside the file image;
    * the first is a small non-negative number;
    * `index + count * stride` fits the image for some plausible stride;
    * both are zero together, or non-zero together, across the corpus.

A slot passing all four is not thereby a table -- padding reads as zero and a
float reads as garbage that sometimes lands in range. What it is, is a candidate
worth a disassembler, ranked by how many models exercise it. A slot that is zero
on all 4,445 models is reported as such and closed.

Nothing here names a field. The output is the ranked shortlist and the evidence
for each entry, in the same shape the chain table's own discovery had before its
constructor was located.
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

REPORT_NAME = "studiohdr-unclaimed-fields.json"

MDL_VERSION = 2531

#: Where the header's own fields stop and the first array can begin. Every
#: claimed field lies below this; `NumIncludeModels`@404 / `IncludeModelIndex`@408
#: are the last pair the document names.
HEADER_END = 412

#: Offsets `docs/vtmb/mdl_v2531.md` claims, as 4-byte slots. `Name` occupies
#: 12..139 as a char[128] and the Vectors span three floats each, so those are
#: expanded rather than listed one-per-field.
def _claimed() -> set[int]:
    claimed = {0, 4, 8, 140}
    claimed |= set(range(12, 140, 4))                  # Name char[128]
    for base in (144, 156, 168, 180, 192, 204, 216):   # the Vectors
        claimed |= {base, base + 4, base + 8}
    claimed |= {228, 232, 236}                         # Flags, phoneme filter
    claimed |= set(range(240, 336, 4))                 # NumBones .. LocalAttachmentIndex
    claimed |= {344, 348, 352, 356, 360, 364}          # flexdesc / flexctrl / flexrule
    claimed |= {376, 380}                              # mouths
    claimed |= {384, 388}                              # local pose parameters
    claimed |= {392}                                   # surfacepropindex
    claimed |= {396, 400}                              # the bone-chain table
    claimed |= {404, 408}                              # include models
    return claimed


#: Strides worth testing an array against. Every one is a record size the format
#: already uses somewhere, plus the small powers a fixed-size record tends to take.
STRIDES = (4, 8, 12, 16, 20, 24, 28, 32, 40, 48, 56, 60, 64, 100, 140, 160, 176, 224)


def _i32(d: bytes, o: int) -> int:
    return struct.unpack_from("<i", d, o)[0]


def sweep(limit: int | None = None) -> dict[str, Any]:
    idx = install.build_index()
    keys = sorted(
        key for key in idx if key.startswith("models/") and key.endswith(".mdl")
    )
    if limit is not None:
        keys = keys[:limit]

    claimed = _claimed()
    candidates = [o for o in range(0, HEADER_END, 4) if o not in claimed]

    counts = {"models_seen": 0, "models_unreadable": 0, "models_wrong_version": 0}
    # Per slot: how it behaves across the corpus.
    nonzero: Counter = Counter()
    in_image: Counter = Counter()
    values: dict[int, Counter] = {o: Counter() for o in candidates}
    # Per adjacent pair: how often it behaves like count/index, and for which strides.
    pair_both_zero: Counter = Counter()
    pair_both_live: Counter = Counter()
    pair_disagree: Counter = Counter()
    pair_fits: dict[int, Counter] = {}
    pair_models: dict[int, list[str]] = {}

    for key in keys:
        data = install.read(idx, key)
        if not data or len(data) < HEADER_END:
            counts["models_unreadable"] += 1
            continue
        counts["models_seen"] += 1
        if _i32(data, 4) != MDL_VERSION:
            counts["models_wrong_version"] += 1
            continue
        size = len(data)

        for offset in candidates:
            value = _i32(data, offset)
            values[offset][value] += 1
            if value != 0:
                nonzero[offset] += 1
            if 0 < value < size:
                in_image[offset] += 1

        for offset in candidates:
            if offset + 4 not in values:
                continue
            count = _i32(data, offset)
            index = _i32(data, offset + 4)
            if count == 0 and index == 0:
                pair_both_zero[offset] += 1
                continue
            if count <= 0 or not (0 < index < size):
                pair_disagree[offset] += 1
                continue
            pair_both_live[offset] += 1
            pair_models.setdefault(offset, []).append(key)
            fits = pair_fits.setdefault(offset, Counter())
            for stride in STRIDES:
                if index + count * stride <= size:
                    fits[stride] += 1

    slots = []
    for offset in candidates:
        common = values[offset].most_common(4)
        slots.append(
            {
                "offset": offset,
                "models_nonzero": nonzero[offset],
                "models_resolving_inside_the_image": in_image[offset],
                "most_common_values": [
                    {"value": value, "models": total} for value, total in common
                ],
            }
        )

    pairs = []
    for offset in candidates:
        if offset + 4 not in values:
            continue
        live = pair_both_live[offset]
        fits = pair_fits.get(offset, Counter())
        pairs.append(
            {
                "offset": offset,
                "models_where_both_are_zero": pair_both_zero[offset],
                "models_behaving_like_count_index": live,
                "models_where_the_two_disagree": pair_disagree[offset],
                "strides_that_fit_the_image": [
                    {"stride": stride, "models": total}
                    for stride, total in sorted(fits.items())
                    if total == live and live > 0
                ],
                "example_models": sorted(pair_models.get(offset, []))[:8],
            }
        )
    pairs.sort(key=lambda row: -row["models_behaving_like_count_index"])

    return {"counts": counts, "candidate_slots": slots, "candidate_pairs": pairs}


def summarize(report: dict[str, Any]) -> str:
    counts = report["counts"]
    lines = [f"models {counts['models_seen']:,} seen ({MDL_VERSION} only)", ""]
    lines.append("unclaimed 4-byte slots:")
    for slot in report["candidate_slots"]:
        common = ", ".join(
            f"{row['value']}x{row['models']}" for row in slot["most_common_values"][:3]
        )
        lines.append(
            f"    +{slot['offset']:<4d} nonzero on {slot['models_nonzero']:5,}, "
            f"in-image on {slot['models_resolving_inside_the_image']:5,}   [{common}]"
        )
    lines.append("")
    lines.append("adjacent pairs behaving like count/index:")
    for pair in report["candidate_pairs"]:
        if not pair["models_behaving_like_count_index"]:
            continue
        strides = ", ".join(
            str(row["stride"]) for row in pair["strides_that_fit_the_image"]
        )
        lines.append(
            f"    +{pair['offset']}/+{pair['offset'] + 4}: "
            f"{pair['models_behaving_like_count_index']:,} models, "
            f"{pair['models_where_both_are_zero']:,} zero-zero, "
            f"{pair['models_where_the_two_disagree']:,} disagree; "
            f"strides fitting every carrier: {strides or 'none'}"
        )
        for model in pair["example_models"][:4]:
            lines.append(f"        {model}")
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
