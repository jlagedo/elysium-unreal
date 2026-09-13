#!/usr/bin/env python3
"""Fold reading batches into `kernel_verdicts.tsv`, and say what a band still owes.

Story 29c reads layers 0-9 of the kernel in packs (`kernel_ledger --bodies <band>` writes them
under `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/`, out of the repository, because they
carry decompiled bodies). Each pack comes back as a TSV of the same five columns the overlay has.
This folds them in:

* later sources win, so a reviewed correction lands on top of the batch it corrects;
* a row for an address outside the band is refused, and so is a duplicate inside one source;
* the merge is idempotent — it reads the committed overlay first and rewrites it whole, sorted by
  the `order.md` layer then the address, so a re-run with no new batches changes nothing.

`--audit` writes nothing and prints what each band has and has not: the acceptance measure of
29c/29d/29e, from the same join `coverage.md` renders.

Usage::

    uv run elysium research merge_verdicts --band 0-9 --from <dir>/verdicts-pack-*.tsv
    uv run elysium research merge_verdicts --audit
"""

from __future__ import annotations

import argparse
import collections
import glob
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

import kernel_ledger as kl  # noqa: E402

from elysium_pipeline.paths import repo_root  # noqa: E402

HEADER_END = "address\tverdict\tband\ttarget\tevidence"
# The `rule` target spellings `gen_kernel_shape` reads, which a reader sometimes writes in the
# verdict column instead. See `_read_batch`.
TARGET_PREFIXES = ("registry", "default", "hand")


def _preamble(path: Path) -> list[str]:
    """The overlay's comment block, kept verbatim across a rewrite."""
    lines = []
    for line in path.read_text(encoding="utf-8").splitlines():
        lines.append(line)
        if line == HEADER_END:
            return lines
    raise SystemExit(f"{path}: no `{HEADER_END}` header row")


def _read_batch(path: Path) -> list[kl.Verdict]:
    rows: list[kl.Verdict] = []
    seen: set[str] = set()
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        if tuple(c.strip() for c in cells) == kl.VERDICT_COLUMNS:
            continue
        if len(cells) != len(kl.VERDICT_COLUMNS):
            raise SystemExit(f"{path}:{number}: {len(cells)} cells, expected 5")
        addr, verdict, band, target, evidence = (c.strip() for c in cells)
        addr = addr.lower().removeprefix("0x")
        # `registry:`, `default:` and `hand:` are `rule` *targets*, and a reader who writes one in
        # the verdict column has still reached the verdict `rule` — the two columns say the same
        # thing. Normalised here rather than refused, but only when the target agrees, so a row
        # that meant something else still fails.
        if verdict in TARGET_PREFIXES and target.startswith(f"{verdict}:"):
            verdict = "rule"
        if verdict not in kl.VERDICT_WORDS and verdict != kl.UNSETTLED_VERDICT:
            raise SystemExit(f"{path}:{number}: {verdict!r} is not a verdict")
        if addr in seen:
            raise SystemExit(f"{path}:{number}: {addr} appears twice in this batch")
        seen.add(addr)
        rows.append(kl.Verdict(addr, verdict, band, target or "-", evidence))
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--band", default="0-9", help="the layer band the batches cover")
    parser.add_argument("--from", dest="sources", nargs="*", default=[],
                        help="batch TSVs, in precedence order (later wins)")
    parser.add_argument("--audit", action="store_true",
                        help="print every band's standing and write nothing")
    parser.add_argument("--module", default=kl.MODULE)
    parser.add_argument("--depth", type=int, default=kl.DEFAULT_DEPTH)
    args = parser.parse_args(argv)

    repo = repo_root()
    ledger = kl.build(args.module, args.depth, repo)
    overlay = _HERE / kl.VERDICTS_TSV

    if args.sources:
        low, high = kl.parse_band(args.band)
        in_band = {a: ledger.layer_of.get(a, -1) for a in ledger.band_core(low, high)}
        merged: dict[str, kl.Verdict] = dict(ledger.verdicts)
        counts: dict[str, int] = collections.Counter()
        for pattern in args.sources:
            for name in sorted(glob.glob(pattern)):
                for row in _read_batch(Path(name)):
                    if row.address not in in_band and row.address not in merged:
                        raise SystemExit(f"{name}: {row.address} is not a core function of "
                                         f"layers {low}-{high}")
                    merged[row.address] = row
                    counts[Path(name).name] += 1
        for name, count in sorted(counts.items()):
            print(f"  {name:<28} {count:5d}")
        order = sorted(merged.values(), key=lambda r: (ledger.layer_of.get(r.address, 99),
                                                       r.address))
        lines = _preamble(overlay)
        lines += ["\t".join((r.address, r.verdict, r.band, r.target, r.evidence)) for r in order]
        overlay.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {len(order)} verdicts to {overlay}")
        ledger.verdicts = kl.load_verdicts(overlay)

    print(f"{'band':>8}  {'core':>5}  {'rule':>5}  {'mech':>5}  {'pres':>5}  {'dead':>5} "
          f" {'unset':>5}  {'none':>5}  {'NEITHER':>7}")
    for low, high in kl.CORE_BANDS:
        s = ledger.band_stats(low, high)
        print(f"{f'{low}-{high}':>8}  {s['core']:5d}  {s['rule']:5d}  {s['mechanism']:5d}  "
              f"{s['present']:5d}  {s['dead']:5d}  {s['unsettled']:5d}  {s['empty']:5d}  "
              f"{s['neither']:7d}")
    if args.audit:
        low, high = kl.parse_band(args.band)
        missing = [a for a in ledger.band_core(low, high) if a not in ledger.verdicts]
        print(f"\nlayers {low}-{high}: {len(missing)} core functions still have no verdict")
        for addr in missing[:40]:
            print(f"  0x{addr}  L{ledger.layer_of.get(addr, -1):<3} "
                  f"{ledger.functions[addr].label}")
        if len(missing) > 40:
            print(f"  … {len(missing) - 40} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
