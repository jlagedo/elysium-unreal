#!/usr/bin/env python3
"""The strict verdict pass of spec 0019 story 1: its packs, its fold, and the two lists it owes.

The 29-series verdicted the kernel's closure with no term for "can anything observe this?", so
2,205 of 2,545 rows came back `rule` and none `dead`. Story 1 re-judges every overlay row under
one question — *what would notice this body's absence?* — and records the answer as a leading tag
on the row's evidence, so the judgment is checkable instead of remembered:

    [0019/1 obs=<kind>: <what names it>]      a `rule` or `present` row: the observable
    [0019/1 seam=<Unreal service or seam>]    a `mechanism` row: what replaces the body
    [0019/1 dead=<why nothing observes it>]   a `dead` row

with `; was <verdict>` inside the bracket when the pass changed the verdict. `<kind>` is one of
`OBSERVABLE_KINDS`. `via` is the chain form — a helper with no name of its own is observable
through the rule body that calls it, and `--check` refuses a `via` whose named row is not itself
`rule` or `present`.

The target column keeps the PORT target through the pass. A `dead` row whose target still names a
port body is a body owed deletion, and 0019/6 writes `-` there when it removes it; a `mechanism`
row whose target names a port body is a body owed a seam. That keeps `gen_kernel_shape`'s
`hand:` / `default:` emission stable, so the pass itself changes no runtime behaviour.

Story 6 closes rows: a `dead` row at `-`, a `mechanism` row at a service word
(`kernel_ledger.SERVICE_TARGETS`). A closed row the port still cites — a port site, or a test
citation that is not a comment recording the removal — fails the audit, naming the address and
the cites. `--closed` prints the meter: dead rows closed / total, mechanism rows closed / total.

The audit reads the port (`port_cites`, a scan of `Source/ElysiumUE`); the two lists do not. They
describe the overlay, so a code commit leaves them as they were; where the port cites a row today
is `uv run elysium research where <address>`.

Modes::

    uv run elysium research kernel_lists --packs            # judgment packs, out of repo
    uv run elysium research kernel_lists --fold <tsv>...    # judgments -> one overlay batch
    uv run elysium research kernel_lists                    # render delete-list.md, seam-list.md
    uv run elysium research kernel_lists --check            # verify tags and the two lists
    uv run elysium research kernel_lists --closed           # the story-6 meter; writes nothing

A judgment TSV is four columns — `address`, `verdict`, `target`, `tag` — where `target` `=` keeps
the row's present target and `tag` is the bracket's body (`obs=schedule: TASK_FACE_ENEMY`). The
fold writes a five-column batch `merge_verdicts --from` takes, the old evidence kept verbatim
behind the new tag.
"""

from __future__ import annotations

import argparse
import collections
import glob
import os
import re
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

import kernel_ledger as kl  # noqa: E402

from elysium_pipeline.paths import repo_root, research_root  # noqa: E402

STORY = "0019/1"
TAG_RE = re.compile(r"^\[0019/1 (obs|seam|dead)=([^\]]+)\]\s*")
WAS_RE = re.compile(r";\s*was (rule|mechanism|present|dead|unsettled)\s*$")
VIA_RE = re.compile(r"^via:\s*(?:0x([0-9a-f]{8})|slot (\d+))\b")
BASE_CLASSES = ("CAI_BaseNPCTroika", "CAI_BaseNPC")
OBSERVABLE_KINDS = (
    "keyfield",   # an authored keyfield a map or template sets
    "input",      # an entity input map I/O or a script fires
    "output",     # an entity output a map wires
    "schedule",   # a schedule text names it: a TASK_*, COND_*, SCHED_*, activity or operand
    "script",     # a Python-visible name
    "dlg",        # a `.dlg` condition or a dialogue line's gate
    "rulebook",   # a vdata / rulebook row
    "save",       # a save field something reads back
    "timing",     # a timing, a number or a state the player can see or hear
    "witness",    # an automation test of the witness that would go red
    "via",        # a helper of a body that carries one of the above
)
TAG_FOR = {"rule": "obs", "present": "obs", "mechanism": "seam", "dead": "dead"}
# A target naming a port body, as against one naming an Unreal service (the grammar is the ledger's).
PORT_TARGET_RE = kl.PORT_TARGET_RE
# A `Tests/` citation that only records a removal (`// 0x10…: dead, deleted in 0019/6`) is a
# deprecation note, not a test of the body: the closed-row meter lets it stand.
DEPRECATION_NOTE_RE = re.compile(r"^\s*(//|/?\*).*\b(dead|deleted|removed|retired|closed)\b", re.I)
# The rows already closed (`-` or a service word) that the port still cited when the closed-row
# meter landed (HEAD b6207c07, spec 0019 story 6): mostly comments naming a body the port does not
# carry, plus a few code sites (a dev print, forwarder call-site comments, table rows). They are
# exempt from the meter so it could land green; the set only falls — story 6's owner clears each
# cite and drops the address here. `--closed` counts the ones still cited.
# 102d72b0 (`CAI_TestHull::Spawn`, closed at 0018/3): cited by the bake's `ElysiumRetailHullTable.h`
# and `ElysiumContentsSignature.h` as the PROVENANCE of the test hull's 40-unit step -- a data
# comment, not a port body (added 0019/6 wave 3).
# 102ef510 (navigator slot 16, the arrival test, closed at `UPathFollowingComponent`): cited by
# `ElysiumNpcBody.h` and `ElysiumNpcMoveScript.h` as the PROVENANCE of the 0.0625-unit arrival
# tolerance the follower's 1.0 cm floor diverges from (K1, pending the judge) -- a data comment.
# 1027d9f0 (slot 526 `OverrideMoveFacing`, dead at `-`): cited by `MotorMoveFacing` (0x102e19e0)
# and its test as the REASON the port asks nothing at `102e19f9` -- a constant false, no body.
# (both added spec 0002 V4b.)
CITED_WHEN_CLOSED = frozenset("""
    101ab060 101ab0e0 1025e510 1025e780 1025e8e0 1025ea00 1025f1a0 10260540 10260670 10260750
    10260f40 10262430 102624b0 1027efb0 102d77d0 10312cd0 1034d6e0 1034ddf0 1034e070 1034e320
    1034e370 10356f60 10357440 10357680 10357760 103577d0 10357800 10357b30 10357ba0 10357be0
    10358330 103587b0 10358c60 10358f90 1035a090 1035a810 1035ae80 1035b080 1035b700 1035be80
    10360160 103673b0 103675e0 10367610 10367740 1036fb10 10376f20 10376f50 10377070 10385a10
    10399c70 103a0270 103a4870 103b2360 103b2590 103b25c0 103b26f0 103b4ff0 103dcf20
    102d72b0 102ef510 1027d9f0
""".split())
GENERATOR_SPELLINGS = ("hand:", "default:", "registry:")
PACK_DIR = "npc-kernel-verdict-pass"
LIST_BANNER = ("<!-- generated by `uv run elysium research kernel_lists`; do not hand-edit -->")
PORT_SOURCE = ("Source", "ElysiumUE")


def port_cites(ledger: kl.Ledger) -> dict[str, list[kl.Citation]]:
    """Closure-function address -> the `Source/ElysiumUE` lines that cite it, at its start or at a
    site inside its body: the closed-row audit's input and the packs' `port:` line, never a table.

    The generated census and data tables transcribe the ledger back into source by address, so
    they are not citations (`kernel_ledger.GENERATED_CENSUS`, `GENERATED_DATA_TABLES`). Each file's
    scan is kept on its size and mtime, like the ledger's oracle scan."""
    scans = kl.kernel_cache.FileScans("port-citations", kl.kernel_cache.digest(
        kl.kernel_cache.checkout(ledger.repo), kl.kernel_cache.code("kernel_lists.py",
                                                                    "kernel_ledger.py")))
    base = str(ledger.repo)
    found: dict[str, list[kl.Citation]] = collections.defaultdict(list)
    for path in sorted(ledger.repo.joinpath(*PORT_SOURCE).rglob("*")):
        if path.suffix not in (".h", ".cpp") or path.name in kl.GENERATED_CENSUS \
                or path.name in kl.GENERATED_DATA_TABLES:
            continue
        full = str(path)
        rel = (full[len(base) + 1:].replace(os.sep, "/") if full.startswith(base + os.sep)
               else path.relative_to(ledger.repo).as_posix())
        for number, text, addrs, _, _ in scans.get(path, rel, False,
                                                   lambda: kl._scan_file(path, False)):
            cite = kl.Citation(rel, number, text)
            for addr in addrs:
                kind, owner = ledger.locate(addr)
                if kind in ("function", "inside") and cite not in found[owner]:
                    found[owner].append(cite)
    scans.save()
    return dict(found)


def split_tag(evidence: str) -> tuple[str, str, str, str]:
    """(`obs`|`seam`|`dead`|'', the tag's text, the verdict it replaced or '', the rest)."""
    match = TAG_RE.match(evidence)
    if not match:
        return "", "", "", evidence
    text = match.group(2).strip()
    was = WAS_RE.search(text)
    if was:
        text = text[:was.start()].strip()
    return match.group(1), text, was.group(1) if was else "", evidence[match.end():]


# --- packs --------------------------------------------------------------------------------------


def packs(ledger: kl.Ledger, per_pack: int, port: dict[str, list[kl.Citation]]) -> dict[str, str]:
    """One sheet per overlay row, in address order: a translation unit reads as one topic."""
    counts = ledger.caller_counts()
    callers: dict[str, list[str]] = collections.defaultdict(list)
    for caller, callee, kind in sorted(ledger.closure_edges):
        if kind in ("direct", "virtual"):
            callers[callee].append(caller)
    rows = sorted(ledger.verdicts)
    out: dict[str, str] = {}
    total = (len(rows) + per_pack - 1) // per_pack
    for number in range(total):
        chunk = rows[number * per_pack:(number + 1) * per_pack]
        lines = [f"# Verdict pass {STORY}, pack {number + 1} of {total} "
                 f"({len(chunk)} rows, 0x{chunk[0]} - 0x{chunk[-1]})", ""]
        for addr in chunk:
            fn = ledger.functions.get(addr)
            verdict = ledger.verdicts[addr]
            if fn is None:
                lines += [f"## 0x{addr}  (not in the corpus closure)", ""]
            else:
                cnt = counts[addr]
                outside = len(ledger.all_callers.get(addr, ())) - cnt["direct"] - cnt["virtual"]
                lines.append(f"## 0x{addr}  {fn.label}{'  ‼' if fn.damaged else ''}  "
                             f"L{ledger.layer_of.get(addr, '?')} size {fn.size}")
                slot_cells = []
                for slot in sorted({s for c, s in fn.slots if c in ledger.family}):
                    bodies = ledger.slot_bodies.get(slot, {})
                    fills = sorted(c for c, a in bodies.items() if a == addr)
                    distinct = len(set(bodies.values()))
                    slot_cells.append(f"#{slot} for {fills[0]}"
                                      + (f" +{len(fills) - 1}" if len(fills) > 1 else "")
                                      + f" ({distinct} distinct bodies in the family)")
                helper_slots = sorted({(c, s) for c, s in fn.slots if c in ledger.helpers})
                slot_cells += [f"{c}#{s}" for c, s in helper_slots[:3]]
                if slot_cells:
                    lines.append("- slots: " + "; ".join(slot_cells))
                called = [f"0x{c} {ledger.functions[c].label}" for c in callers[addr][:6]
                          if c in ledger.functions]
                lines.append(f"- callers {cnt['direct']}d/{cnt['virtual']}v/"
                             f"{cnt['slot-candidate']}c"
                             + (f" +{outside} outside" if outside > 0 else "")
                             + (": " + "; ".join(called) if called else ""))
                if fn.strings:
                    lines.append("- strings: " + " | ".join(kl._short(s, 70)
                                                            for s in fn.strings[:4]))
                lines.append(f"- port: {ledger._cite_cell(port.get(addr, []), 4)}")
                lines.append(f"- oracle: {ledger._cite_cell(ledger.cites(ledger.oracle_addr, addr), 3)}")
            lines.append(f"- NOW: {verdict.verdict} | {verdict.target} | band {verdict.band}")
            lines.append(f"- EVIDENCE: {verdict.evidence}")
            lines.append("")
        out[f"pack-{number + 1:02d}.md"] = "\n".join(lines) + "\n"
    return out


# --- fold ---------------------------------------------------------------------------------------


def fold(ledger: kl.Ledger, sources: list[str], port: dict[str, list[kl.Citation]]) -> list[str]:
    """Judgment rows -> overlay rows, the old evidence kept verbatim behind the new tag."""
    rows: dict[str, str] = {}
    for pattern in sources:
        names = sorted(glob.glob(pattern))
        if not names:
            raise SystemExit(f"kernel_lists --fold: nothing matches {pattern}")
        for name in names:
            for number, line in enumerate(
                    Path(name).read_text(encoding="utf-8").splitlines(), 1):
                if not line.strip() or line.startswith("#"):
                    continue
                cells = [c.strip() for c in line.split("\t")]
                if cells[0] == "address":
                    continue
                if len(cells) != 4:
                    raise SystemExit(f"{name}:{number}: {len(cells)} cells, expected 4")
                addr, verdict, target, tag = cells
                addr = addr.lower().removeprefix("0x")
                old = ledger.verdicts.get(addr)
                if old is None:
                    raise SystemExit(f"{name}:{number}: {addr} is not an overlay row")
                if verdict not in TAG_FOR:
                    raise SystemExit(f"{name}:{number}: {verdict!r} is not a verdict")
                head, _, text = tag.partition("=")
                if head != TAG_FOR[verdict] or not text.strip():
                    raise SystemExit(f"{name}:{number}: a `{verdict}` row takes a "
                                     f"`{TAG_FOR[verdict]}=` tag, not `{tag}`")
                if "]" in tag or "[" in tag:
                    raise SystemExit(f"{name}:{number}: a tag may not hold a bracket")
                if head == "obs":
                    kind = text.split(":", 1)[0].strip()
                    if kind not in OBSERVABLE_KINDS:
                        raise SystemExit(f"{name}:{number}: `{kind}` is not an observable kind")
                _, _, was, rest = split_tag(old.evidence)
                before = was or (old.verdict if old.verdict != verdict else "")
                if target in ("=", ""):
                    target = old.target
                # A `dead` row the port never carried has nothing to delete: `-` at once. One the
                # port does carry keeps its target until 0019/6 removes the body.
                if verdict == "dead" and not target.startswith(GENERATOR_SPELLINGS) and (
                        not PORT_TARGET_RE.match(target)
                        or not port.get(addr)):
                    target = "-"
                note = f"; was {before}" if before and before != verdict else ""
                rows[addr] = "\t".join((addr, verdict, old.band, target or "-",
                                        f"[{STORY} {head}={text.strip()}{note}] {rest}".rstrip()))
    return [rows[a] for a in sorted(rows)]


# --- the audit and the two lists ----------------------------------------------------------------


def audit(ledger: kl.Ledger) -> list[str]:
    problems: list[str] = []
    for addr, row in sorted(ledger.verdicts.items()):
        if row.verdict == kl.UNSETTLED_VERDICT:
            continue   # a recorded failure to reach a verdict; `coverage.md` counts it apart
        head, text, _, _ = split_tag(row.evidence)
        want = TAG_FOR[row.verdict]
        if head != want:
            problems.append(f"0x{addr}: `{row.verdict}` row carries "
                            f"{('a `' + head + '=` tag') if head else 'no tag'}")
            continue
        if head == "obs":
            kind = text.split(":", 1)[0].strip()
            if kind not in OBSERVABLE_KINDS:
                problems.append(f"0x{addr}: `{kind}` is not an observable kind")
            via = VIA_RE.match(text)
            if kind == "via":
                if not via:
                    problems.append(f"0x{addr}: a `via` names what it leans on as `0x…` or "
                                    f"`slot N`")
                    continue
                if via.group(1):
                    leaned = ledger.verdicts.get(via.group(1))
                    if leaned is not None and leaned.verdict not in ("rule", "present"):
                        problems.append(f"0x{addr}: observable only via 0x{via.group(1)}, "
                                        f"which is `{leaned.verdict}`")
                    continue
                # A dispatch slot is observable while ANY other family body at it is a rule: the
                # empty base of a hook one species answers is the declaration that hook stands on.
                slot = int(via.group(2))
                others = {a for a in ledger.slot_bodies.get(slot, {}).values() if a != addr}
                live = [a for a in others if a in ledger.verdicts
                        and ledger.verdicts[a].verdict in ("rule", "present")]
                if others and not live:
                    problems.append(f"0x{addr}: observable only via slot {slot}, where no other "
                                    f"body is `rule` or `present`")
    return problems


def closed_cites(port: dict[str, list[kl.Citation]], addr: str) -> list[str]:
    """What still cites a closed row: every port site, and every test citation except a comment
    that records the removal (`DEPRECATION_NOTE_RE`)."""
    keys = sorted({(c.path, c.line, c.text) for c in port.get(addr, [])})
    return [f"{p.rsplit('/', 1)[-1]}:{n}" for p, n, text in keys
            if "/Tests/" not in p or not DEPRECATION_NOTE_RE.match(text)]


def closed_problems(ledger: kl.Ledger, port: dict[str, list[kl.Citation]]) -> list[str]:
    """A body-closed row (`dead` at `-`, `mechanism` at a service word) whose body the port still cites.
    A `seam:` / `default:` / `registry:` row keeps a body or a literal, so its cites are expected.

    The rows `CITED_WHEN_CLOSED` pins were closed before this meter existed and are exempt; every
    row closed after it is held to the rule."""
    problems: list[str] = []
    for addr, row in sorted(ledger.verdicts.items()):
        if not kl.body_closed(row.verdict, row.target) or addr in CITED_WHEN_CLOSED:
            continue
        cites = closed_cites(port, addr)
        if cites:
            problems.append(f"0x{addr}: `{row.verdict}` row closed at `{row.target}` is still cited "
                            f"by {_cell(cites, 8)}")
    return problems


def closed_meter(ledger: kl.Ledger) -> dict[str, tuple[int, int]]:
    """verdict -> (rows closed, rows total), for `dead` and `mechanism`."""
    out: dict[str, tuple[int, int]] = {}
    for verdict in ("dead", "mechanism"):
        rows = [r for r in ledger.verdicts.values() if r.verdict == verdict]
        out[verdict] = (sum(1 for r in rows if kl.closed_target(r.verdict, r.target)), len(rows))
    return out


def _cell(items: list[str], limit: int = 6) -> str:
    if not items:
        return "—"
    shown = ", ".join(items[:limit])
    return shown + (f" … +{len(items) - limit}" if len(items) > limit else "")


def render(ledger: kl.Ledger) -> dict[str, str]:
    head = (f"_vampire.dll sha256 `{str(ledger.meta.get('sha256', ''))[:16]}…`; from "
            f"`research/tooling/ghidra/driver/{kl.VERDICTS_TSV}`. Rebuild: "
            f"`uv run elysium research kernel_lists`._")
    dead = [(a, r) for a, r in sorted(ledger.verdicts.items()) if r.verdict == "dead"]
    mech = [(a, r) for a, r in sorted(ledger.verdicts.items()) if r.verdict == "mechanism"]

    out = [LIST_BANNER, "# NPC kernel — the delete list", "", head, "",
           "Every `dead` row of the verdict overlay: a retail body nothing can observe — no "
           "keyfield, schedule text, script name, output, save field, player-visible timing or "
           "witness test would notice its absence. Spec 0019 story 1 judged them; story 6 removes "
           "the port body and its tests and writes `-` in the row's target. **Standing** is a row "
           "whose target still names a port body; **gone** is a row at `-`. Where the port cites "
           "a row today is `uv run elysium research where <address>`.", "",
           f"{len(dead)} rows, {sum(1 for _, r in dead if r.target != '-')} standing.", "",
           "| Address | Function | Layer | Port target | Why |",
           "|---|---|---|---|---|"]
    for addr, row in dead:
        fn = ledger.functions.get(addr)
        _, text, _, _ = split_tag(row.evidence)
        out.append(f"| `0x{addr}` | {fn.label if fn else ''} | {ledger.layer_of.get(addr, '')} "
                   f"| {kl._cell(row.target)} | {kl._cell(text)} |")
    delete_list = "\n".join(out) + "\n"

    ported = [(a, r) for a, r in mech if PORT_TARGET_RE.match(r.target)]
    service = [(a, r) for a, r in mech if not PORT_TARGET_RE.match(r.target)]
    out = [LIST_BANNER, "# NPC kernel — the seam list", "", head, "",
           "Every `mechanism` row of the verdict overlay: a retail body the world merely needs, "
           "which Unreal supplies. The first table is the rows the port carries a body for — "
           "story 6 replaces each with a call into the named service, keeping the row's retail "
           "thresholds as tunables (story 4). The second is the rows that were never ported, "
           "recorded so nobody does.", "",
           f"{len(mech)} rows: {len(ported)} with a port body, {len(service)} service-only.", "",
           "## Port bodies owed a seam", "",
           "| Address | Function | Layer | Port target | Unreal service |",
           "|---|---|---|---|---|"]
    for addr, row in ported:
        fn = ledger.functions.get(addr)
        _, text, _, _ = split_tag(row.evidence)
        out.append(f"| `0x{addr}` | {fn.label if fn else ''} | {ledger.layer_of.get(addr, '')} "
                   f"| {kl._cell(row.target)} | {kl._cell(text)} |")
    out += ["", "## Service only, nothing ported", "",
            "| Address | Function | Layer | Target | Unreal service |", "|---|---|---|---|---|"]
    for addr, row in service:
        fn = ledger.functions.get(addr)
        _, text, _, _ = split_tag(row.evidence)
        out.append(f"| `0x{addr}` | {fn.label if fn else ''} | {ledger.layer_of.get(addr, '')} "
                   f"| {kl._cell(row.target)} | {kl._cell(text)} |")
    return {"delete-list.md": delete_list, "seam-list.md": "\n".join(out) + "\n"}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--packs", action="store_true",
                        help=f"write the judgment packs under $ELYSIUM_WORK_ROOT/research/{PACK_DIR}")
    parser.add_argument("--per-pack", type=int, default=170)
    parser.add_argument("--fold", nargs="+", metavar="TSV",
                        help="judgment TSVs to turn into one `merge_verdicts` batch")
    parser.add_argument("--out", help="where `--fold` writes its batch")
    parser.add_argument("--check", action="store_true",
                        help="verify every row's tag and the two committed lists; write nothing")
    parser.add_argument("--closed", action="store_true",
                        help="print the story-6 meter (dead and mechanism rows closed / total); "
                             "write nothing")
    parser.add_argument("--module", default=kl.MODULE)
    parser.add_argument("--depth", type=int, default=kl.DEFAULT_DEPTH)
    args = parser.parse_args(argv)

    repo = repo_root()
    if args.check and not (args.packs or args.fold or args.closed):
        return kl.kernel_cache.stamped("kernel_lists", vars(args), repo, lambda: _main(args, repo))
    return _main(args, repo)


def _main(args: argparse.Namespace, repo: Path) -> int:
    ledger = kl.build(args.module, args.depth, repo)
    work = research_root() / PACK_DIR
    if args.packs:
        work.mkdir(parents=True, exist_ok=True)
        written = packs(ledger, args.per_pack, port_cites(ledger))
        for name, text in written.items():
            (work / name).write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {len(written)} judgment packs to {work}")
        return 0
    if args.fold:
        rows = fold(ledger, args.fold, port_cites(ledger))
        target = Path(args.out) if args.out else work / "batch-0019-1.tsv"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("\n".join(["\t".join(kl.VERDICT_COLUMNS), *rows]) + "\n",
                          encoding="utf-8", newline="\n")
        print(f"wrote {len(rows)} rows to {target}")
        return 0

    if args.closed:
        meter = closed_meter(ledger)
        print(f"closed: dead {meter['dead'][0]} / {meter['dead'][1]}, "
              f"mechanism {meter['mechanism'][0]} / {meter['mechanism'][1]}")
        port = port_cites(ledger)
        pinned = [a for a in sorted(CITED_WHEN_CLOSED) if closed_cites(port, a)]
        print(f"pinned: {len(pinned)} of {len(CITED_WHEN_CLOSED)} `CITED_WHEN_CLOSED` rows still "
              "cited" + (f" ({', '.join('0x' + a for a in pinned[:6])}"
                          f"{' …' if len(pinned) > 6 else ''})" if pinned else ""))
        return 0

    problems = audit(ledger) + closed_problems(ledger, port_cites(ledger))
    for line in problems[:60]:
        print("  " + line)
    if len(problems) > 60:
        print(f"  … {len(problems) - 60} more")
    counts = collections.Counter(r.verdict for r in ledger.verdicts.values())
    print("verdicts: " + ", ".join(f"{counts[w]} {w}" for w in (*kl.VERDICT_WORDS, "unsettled")))
    status = kl.emit(render(ledger), repo.joinpath(*kl.DEFAULT_OUTPUT), args.check)
    if problems:
        print(f"kernel_lists: {len(problems)} rows fail the strict rule or are closed but still cited")
        return 1
    return status


if __name__ == "__main__":
    sys.exit(main())
