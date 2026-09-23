#!/usr/bin/env python3
"""Pass R's reading side: briefs, walk checks, walk diffs, drills and the reading packets.

`kernel_skeleton` writes the mechanical half of a reading packet (spec 0019 story 8, pass R); this
module is the other half's plumbing, reached as subcommands of the same command::

    uv run elysium research kernel_skeleton brief --band small --batch 10 --out <dir>
    uv run elysium research kernel_skeleton brief --chunk 0x102a1910 --chunks 6 --out <dir>
    uv run elysium research kernel_skeleton check-walk <list.tsv> <walk.md>
    uv run elysium research kernel_skeleton diff <list.tsv> <walkA.md> <walkB.md> [--show]
    uv run elysium research kernel_skeleton drill-brief <list.tsv> <walkA.md> <walkB.md> --out <brief.md>
    uv run elysium research kernel_skeleton packet --family Conditions19 --a <walks…> --b <walks…>
                                                     [--drill <files…>] [--contra <file>] [--readers "A=…, B=…"]
    uv run elysium research kernel_skeleton contra-brief --family Conditions19 --out <brief.md>
    uv run elysium research kernel_skeleton check-packet [--family X | --all]

* **brief** — one reader brief per batch of functions: the compact skeleton (arms, returns, calls,
  globals, operands) and the listing, with the standing instructions (one branch row per arm, one
  argument row per call, effect, unrecovered). Only rows verdicted `rule` in `kernel_verdicts.tsv`
  are briefed; `dead`, `mechanism` and `present` rows get no walk. `--band` picks a size band
  (tiny < 64 B, small < 1 KB, big < 4 KB, giant), `--batch N` groups a family's functions N to a
  brief, `--per-function` writes one brief each, and `--chunk ADDR --chunks N` splits a switch body
  into N briefs by arm address balanced on branch and call weight, each carrying the dispatch
  prologue, its case ids, and the shared continuation blocks its arms jump to.
* **check-walk** — holds a walk to its skeleton: every listed function has a section, every
  skeleton arm has a branch row and every skeleton call an argument row.
* **diff** — pairs two walks row by row on the skeleton's addresses: AGREE, CONFLICT (sense,
  signedness, argument order, swapped consequences), DEPTH (one names what the other leaves open),
  ONE-SIDED. The signal is narrow on purpose so prose differences do not count.
* **drill-brief** — the CONFLICT rows of a diff as one judge brief: the skeleton row, the listing
  window, both readings, and the ask to settle each with the listing lines.
* **packet** — the family's reading packet, `<Family>-READING.md` beside the family files: per
  `rule` row the checklist verdict line verbatim, the skeleton, the merged walk (agreed rows,
  drilled rows with both readings and the settlement, one-sided rows marked), both readers' effect
  paragraphs, the banked wave-1 corrections that cite the address, and the Unrecovered union;
  the rows skipped by verdict; and the packet-versus-checklist list when one was produced.
* **contra-brief** — per family, the checklist row's evidence beside the packet's merged walk,
  asking a reader for every packet claim that contradicts the checklist row.
* **check-packet** — refuses a packet missing any `rule` row of its family, or a row section
  missing a branch row for a skeleton arm or an argument row for a skeleton call.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

from elysium_pipeline.paths import repo_root, research_root  # noqa: E402

BANDS = {"tiny": (0, 64), "small": (64, 1024), "big": (1024, 4096), "giant": (4096, 1 << 30)}
SUBCOMMANDS = ("brief", "check-walk", "diff", "drill-brief", "packet", "contra-brief", "check-packet")


# ---------------------------------------------------------------------------------------------
# Sources.


def families_dir() -> Path:
    return research_root() / "npc-kernel-checklist" / "families-19-29"


def skeletons_dir() -> Path:
    return research_root() / "npc-kernel-checklist" / "skeletons-19-29"


def verdicts(path: Path | None = None) -> dict[str, tuple[str, str, str]]:
    """address (8 hex, no 0x) → (verdict, target, evidence) from the overlay."""
    path = path or repo_root() / "research" / "tooling" / "ghidra" / "driver" / "kernel_verdicts.tsv"
    out: dict[str, tuple[str, str, str]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#") or line.startswith("address\t"):
            continue
        c = line.rstrip("\n").split("\t")
        if len(c) >= 2 and re.fullmatch(r"[0-9a-f]{8}", c[0].lower()):
            out[c[0].lower()] = (c[1], c[3] if len(c) > 3 else "", c[4] if len(c) > 4 else "")
    return out


def checklist(path: Path | None = None) -> dict[str, dict[str, str]]:
    """address → the checklist row's cells (`function`, `size`, `verdict`, `target`, `evidence`)."""
    path = path or repo_root() / "docs" / "vtmb" / "npc-kernel" / "checklist-19-29.md"
    out: dict[str, dict[str, str]] = {}
    if not path.is_file():
        return out
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| `0x"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 11:
            continue
        addr = cells[0].strip("`").lower()[2:]
        out[addr] = {"function": cells[1], "size": cells[2], "layer": cells[3], "slots": cells[4],
                     "writes": cells[5], "reads": cells[6], "callers": cells[7],
                     "verdict": cells[8].strip("`"), "target": cells[9], "evidence": cells[10]}
    return out


def family_rows(fam: str) -> list[str]:
    """The family file's addresses, in file order (8 hex, no 0x)."""
    path = families_dir() / f"{fam}.tsv"
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cell = line.split("\t")[0].strip().lower()
        cell = cell[2:] if cell.startswith("0x") else cell
        if re.fullmatch(r"[0-9a-f]{8}", cell):
            out.append(cell)
    return out


def skeletons(fam: str) -> dict[str, dict]:
    path = skeletons_dir() / f"{fam}.json"
    return {s["addr"].lower()[2:]: s for s in json.loads(path.read_text(encoding="utf-8"))}


def skeleton_sections(fam: str) -> dict[str, str]:
    """address → the skeleton's own markdown section, verbatim."""
    path = skeletons_dir() / f"{fam}.md"
    return sections(path.read_text(encoding="utf-8")) if path.is_file() else {}


def all_families() -> list[str]:
    return sorted(p.stem for p in families_dir().glob("*.tsv"))


def listing_of(addr: str) -> str:
    import corpus  # noqa: E402  (the listing database lives behind the corpus module)
    conn = corpus._listing_connection()
    row = conn.execute("SELECT asm FROM listing WHERE module='vampire.dll' AND addr=?", (addr,)).fetchone()
    return row["asm"] if row else "// no listing"


# ---------------------------------------------------------------------------------------------
# Briefs.

HEAD = """\
# Walks on skeletons: {n} retail functions of vampire.dll (VtMB, 2004), one brief

Each function below comes with a MECHANICAL SKELETON extracted by a script from the listing
(every conditional branch with both targets, every call with thunks resolved and virtual calls as
slot numbers, every global with its value read from the image, every memory operand with its
datamap name) and the LISTING itself. Treat the skeleton as correct and complete for what it
lists; do not re-list it. Your job is the WALK, the part a script cannot do.

For EVERY function, in the order given, answer with a section headed exactly
`## <address> <label>` (copy the header line) holding:

1. **Branches** — one row per branch address in the skeleton, in order:
   `| branch | what is compared with what (object+offset or callee result, sense ==, !=, >, <=, signed/unsigned, float) | taken means | not-taken means |`.
   A function with no branches writes `Branches: none`.
2. **Arguments** — one row per call in the skeleton: `| call | receiver (ECX) | stack args in order | return used as |`.
   Settle which pushes belong to which call from the callee's `RET n` (use vtmb_asm on the callee
   only when the skeleton's listing does not settle it). Name a stack local passed by address
   (`&local`) as such, since the callee may rewrite it. A function with no calls writes
   `Arguments: none`.
3. **Effect** — one or two sentences: what the function returns and what it writes, in retail
   terms (field names from the skeleton), including "chains to <callee> and returns its result"
   when that is the whole body.
4. **Unrecovered** — anything not settled, with the address, or `none`.

Rules: cite an address on every row; never guess a datamap name the skeleton does not give
(vtmb_fields on the class if you need one); do not open docs/ or Source/ in the repository; edit
and create no file; the answer is your final message and must contain all {n} sections.
Tools available: the vtmb-corpus MCP (vtmb_code, vtmb_asm, vtmb_fields, vtmb_slot, vtmb_callees).

"""

CHUNK_HEAD = """\
# Walk on a skeleton, chunk {k} of {n}: {label} at {addr} (vampire.dll, VtMB 2004)

This function is a switch over task ids: {ncases} case ids dispatch through the jump table at
{table} onto {narms} arm bodies. It is read in {n} chunks by ARM ADDRESS RANGE; this chunk covers
the arms from {lo} to {hi} ({carms} arms, serving the case ids listed under "Cases in this chunk").
The prologue (dispatch) is included for context and is NOT to be walked again. Everything else is
exactly as in a one-function brief: the skeleton rows and listing lines restricted to this range.

Answer with ONE section headed `## {addr} {label} — chunk {k}` holding, for the arms in this chunk:
1. **Cases** — one row per arm start in this chunk: `| arm | case ids | what the arm does, in retail terms |`.
2. **Branches** — one row per branch address in this chunk's skeleton rows:
   `| branch | what is compared with what (sense ==, !=, >, <=, signed/unsigned, float) | taken means | not-taken means |`.
3. **Arguments** — one row per call in this chunk: `| call | receiver (ECX) | stack args in order | return used as |`,
   pushes settled by the callee's `RET n` (vtmb_asm on the callee only when the listing does not settle it).
4. **Effect** — what the arms of this chunk write and how each ends (complete, fail, running).
5. **Unrecovered** — with the address, or `none`.
Cite an address on every row. Do not open docs/ or Source/ in the repository. Edit and create no
file; the answer is your final message.
Tools: the vtmb-corpus MCP (vtmb_code, vtmb_asm, vtmb_fields, vtmb_slot, vtmb_callees).

"""


def compact(s: dict, asm: str) -> str:
    out = [f"## {s['addr']} {s['label']}", "",
           f"class `{s['class']}` · {s['size']} bytes · {s['instructions']} instructions"
           + (f" · {s['damaged']}" if s.get("damaged") else ""), ""]
    out += ["Arms: " + ("; ".join(f"{a['at']} {a['op']} taken→{a['taken']} else→{a['next']}" for a in s["arms"])
                        if s["arms"] else "none")]
    out += ["Returns: " + (", ".join(s["rets"]) or "none")]
    for t in s["tables"]:
        out += [f"Jump table {t['at']}: {len(t['entries'])} entries " + ", ".join(t["entries"])]
        if t.get("cases"):
            by_arm: dict[str, list[int]] = {}
            for c in t["cases"]:
                by_arm.setdefault(c["arm"], []).append(c["id"])
            out += ["  cases: " + "; ".join(f"{arm} ← " + ",".join(f"0x{i:x}" for i in ids)
                                            for arm, ids in by_arm.items())]
    if s["calls"]:
        out += ["Calls:"] + [f"  {c['at']} {c['what']}" for c in s["calls"]]
    if s["globals"]:
        out += ["Globals:"] + [f"  {g['at']} {g['op']} {g['rw']} {g['va']} ({g['width']}) = {g['value']}"
                               for g in s["globals"]]
    ops = [("R", r) for r in s["reads"]] + [("W", w) for w in s["writes"]]
    ops.sort(key=lambda x: x[1]["at"])
    if ops:
        out += ["Operands:"] + [f"  {k} {o['at']} {o['op']} {o['width']} {o['what']}" for k, o in ops]
    out += ["", "```", asm.strip(), "```", ""]
    return "\n".join(out)


def _rule_rows(fams: list[str], band: str | None, only_verdict: str = "rule") -> list[tuple[str, dict]]:
    v = verdicts()
    lo, hi = BANDS[band] if band else (0, 1 << 30)
    picked = []
    for fam in fams:
        sk = skeletons(fam)
        for addr in family_rows(fam):
            s = sk.get(addr)
            if s is None or v.get(addr, ("?",))[0] != only_verdict:
                continue
            if lo <= s["size"] < hi:
                picked.append((fam, s))
    return picked


def cmd_brief(a) -> int:
    fams = [a.family] if a.family else all_families()
    if a.chunk:
        return _chunked(a, fams)
    picked = _rule_rows(fams, a.band, a.verdict)
    if a.addr:
        picked = [(f, s) for f, s in picked if s["addr"].lower() == a.addr.lower()]
    secs = [compact(s, listing_of(s["addr"][2:])) for _, s in picked]
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    n = 0
    if a.per_function:
        for (fam, s), sec in zip(picked, secs):
            (out / f"{s['addr']}.md").write_text(HEAD.format(n=1) + sec, encoding="utf-8")
            (out / f"{s['addr']}.tsv").write_text(f"{fam}\t{s['addr']}\t{s['label']}\t{s['size']}\n", encoding="utf-8")
            n += 1
    else:
        byfam: dict[str, list] = {}
        for (fam, s), sec in zip(picked, secs):
            byfam.setdefault(fam, []).append((s, sec))
        size = a.batch or 10 ** 6
        for fam, items in byfam.items():
            for k in range(0, len(items), size):
                part = items[k:k + size]
                stem = f"{fam}-{k // size + 1}" if a.batch else fam
                (out / f"{stem}.md").write_text(HEAD.format(n=len(part)) + "\n".join(sec for _, sec in part),
                                                encoding="utf-8")
                (out / f"{stem}.tsv").write_text(
                    "".join(f"{fam}\t{s['addr']}\t{s['label']}\t{s['size']}\n" for s, _ in part), encoding="utf-8")
                n += 1
    print(f"{len(picked)} functions in {n} briefs under {out}")
    return 0


def _chunked(a, fams: list[str]) -> int:
    target = a.chunk.lower()
    target = target if target.startswith("0x") else "0x" + target
    s = None
    for fam in fams:
        s = skeletons(fam).get(target[2:]) or s
    if s is None or not s["tables"]:
        raise SystemExit(f"{target} is not a switch body in the skeletons")
    t = s["tables"][0]
    arms = sorted({int(e, 16) for e in t["entries"]})
    start, body_end = int(s["addr"], 16), int(s["addr"], 16) + s["size"]
    prologue_end = arms[0]
    asm_lines = [(int(l[:8], 16), l) for l in listing_of(s["addr"][2:]).splitlines()
                 if len(l) > 8 and re.match(r"[0-9a-f]{8}", l)]
    # Balance the chunks on weight: a branch counts 1, a call 1/4 (the pilot's ~40 branches /
    # ~150 calls per chunk), cut only at an arm start.
    bounds = arms + [body_end]
    weight = []
    for i, lo in enumerate(arms):
        hi = bounds[i + 1]
        w = sum(1 for x in s["arms"] if lo <= int(x["at"], 16) < hi) \
            + sum(1 for x in s["calls"] if lo <= int(x["at"], 16) < hi) / 4
        weight.append(w)
    total = sum(weight)
    per = total / a.chunks
    cuts, acc = [arms[0]], 0.0
    for i, w in enumerate(weight):
        if acc >= per and len(cuts) < a.chunks:
            cuts.append(arms[i])
            acc = 0.0
        acc += w
    cuts.append(body_end)
    # The dispatch prologue has arms of its own (the `JA default` bound, an early return); they are
    # walked as chunk 0 so the packet holds every arm, and stay context in every other chunk.
    if any(start <= int(x["at"], 16) < prologue_end for x in s["arms"] + s["calls"]):
        cuts = [start] + cuts
    prologue = "\n".join(l for aa, l in asm_lines if aa < prologue_end)
    ids_by_arm: dict[str, list[int]] = {}
    for c in t.get("cases", []):
        ids_by_arm.setdefault(c["arm"], []).append(c["id"])
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    n = len(cuts) - 1
    first = 0 if cuts[0] == start else 1
    for k in range(n):
        lo, hi = cuts[k], cuts[k + 1]
        kk = k + first                                   # chunk 0 is the prologue when present
        inr = lambda at: lo <= int(at, 16) < hi  # noqa: E731
        chunk_arms = [x for x in arms if lo <= x < hi]
        sub = dict(s)
        for key in ("arms", "calls", "globals", "reads", "writes", "tables"):
            sub[key] = [x for x in s[key] if inr(x["at"])]
        sub["rets"] = [x for x in s["rets"] if inr(x)]
        lines = "\n".join(l for aa, l in asm_lines if lo <= aa < hi)
        outs = set()
        for aa, l in asm_lines:
            if lo <= aa < hi:
                m = re.search(r"\bJ\w+ 0x([0-9a-f]{8})\b", l)
                if m:
                    tgt = int(m.group(1), 16)
                    if not (lo <= tgt < hi) and tgt >= prologue_end:
                        outs.add(tgt)
        arm_starts = set(arms)
        cont = []
        for tgt in sorted(outs):
            block = []
            for aa, l in asm_lines:
                if aa < tgt:
                    continue
                if aa != tgt and aa in arm_starts:
                    break
                block.append(l)
                if " RET" in l or len(block) >= 40:
                    break
            cont.append("\n".join(block))
        cases = "\n".join(f"- arm 0x{x:08x}: case ids "
                          + ", ".join(f"0x{i:x}" for i in sorted(ids_by_arm.get(f"0x{x:08x}", [])))
                          for x in chunk_arms)
        text = CHUNK_HEAD.format(k=kk, n=n - 1 + first, label=s["label"], addr=s["addr"], ncases=len(t.get("cases", [])),
                                 table=t["table"], narms=len(arms), lo=f"0x{lo:08x}", hi=f"0x{hi:08x}",
                                 carms=len(chunk_arms))
        text += "## Cases in this chunk\n\n" + cases + "\n\n## Prologue (context only)\n\n```\n" + prologue + "\n```\n\n"
        text += compact(sub, lines)
        if cont:
            text += ("\n## Shared continuations (context only: blocks this chunk's arms jump to, outside "
                     "its range)\n\n```\n" + "\n\n".join(cont) + "\n```\n")
        if kk == 0:
            text = text.replace("The prologue (dispatch) is included for context and is NOT to be walked again.",
                                "THIS IS THE PROLOGUE CHUNK: walk the dispatch itself (its bound test, its early "
                                "returns, its calls) and nothing past the first arm.")
        path = out / f"{s['addr']}-chunk{kk}.md"
        path.write_text(text, encoding="utf-8")
        (out / f"{s['addr']}-chunk{kk}.tsv").write_text(
            f"{fams[0] if len(fams) == 1 else '?'}\t{s['addr']}\t{s['label']}\t{s['size']}\n", encoding="utf-8")
        print(f"chunk {kk}: arms 0x{lo:08x}..0x{hi:08x}, {len(chunk_arms)} arms, {len(sub['arms'])} branches, "
              f"{len(sub['calls'])} calls, {len(text)} chars → {path}")
    return 0


# ---------------------------------------------------------------------------------------------
# Walks: sections and rows.

SECTION_RE = re.compile(r"(?ms)^##\s+`?(0x[0-9a-fA-F]{8})\b.*?(?=^##\s+`?0x[0-9a-fA-F]{8}\b|\Z)")


def sections(text: str) -> dict[str, str]:
    """address (8 hex) → section text. Chunk sections of one function are concatenated."""
    out: dict[str, str] = {}
    for m in SECTION_RE.finditer(text):
        key = m.group(1).lower()[2:]
        out[key] = (out[key] + "\n" + m.group(0)) if key in out else m.group(0)
    return out


def read_sections(paths) -> dict[str, str]:
    out: dict[str, str] = {}
    for p in paths:
        for k, v in sections(Path(p).read_text(encoding="utf-8")).items():
            out[k] = (out[k] + "\n" + v) if k in out else v
    return out


def rows(section: str, kind: str) -> dict[str, list[str]]:
    """kind 'branch', 'call' or 'case': address → the row's cells."""
    out: dict[str, list[str]] = {}
    in_block = False
    for line in section.splitlines():
        low = re.sub(r"^[#*\d.\s]+", "", line.lower()).strip("*: ")
        if low.startswith("branch") or line.lower().startswith("| branch"):
            in_block = kind == "branch"
        elif low.startswith("argument") or line.lower().startswith("| call"):
            in_block = kind == "call"
        elif low.startswith("case") or line.lower().startswith("| arm"):
            in_block = kind == "case"
        elif low.startswith(("effect", "unrecovered", "settled", "shared", "prologue", "range", "recurring")):
            in_block = False
        if in_block and line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            m = re.match(r"`?(0x[0-9a-fA-F]{8})", cells[0])
            if m and not set(cells[0]) <= set("-: "):
                out[m.group(1).lower()] = cells
    return out


def cmd_check_walk(a) -> int:
    wanted = _list(a.list)
    skel = _skeleton_index()
    have = read_sections(_expand(a.walk.split(",")))
    missing, short_arms, short_calls, ok = [], [], [], 0
    for fam, addr, label, size in wanted:
        body = have.get(addr[2:].lower())
        if body is None:
            missing.append(addr)
            continue
        s = skel[addr.lower()]
        found = set(re.findall(r"0x[0-9a-f]{8}", body.lower()))
        ma = sorted({x["at"] for x in s["arms"]} - found)
        mc = sorted({x["at"] for x in s["calls"]} - found)
        if ma:
            short_arms.append((addr, ma))
        if mc:
            short_calls.append((addr, mc))
        if not ma and not mc:
            ok += 1
    print(f"{len(wanted)} wanted, {len(have)} sections found, {ok} complete")
    if missing:
        print(f"missing sections ({len(missing)}): " + ", ".join(missing))
    for addr, ma in short_arms:
        print(f"{addr}: arms not mentioned: " + ", ".join(ma))
    for addr, mc in short_calls:
        print(f"{addr}: calls not mentioned: " + ", ".join(mc))
    return 0 if not (missing or short_arms or short_calls) else 1


def _list(path: str) -> list[list[str]]:
    return [l.split("\t")[:4] for l in Path(path).read_text(encoding="utf-8").splitlines() if l.strip()]


def _skeleton_index() -> dict[str, dict]:
    out = {}
    for f in skeletons_dir().glob("*.json"):
        for s in json.loads(f.read_text(encoding="utf-8")):
            out[s["addr"].lower()] = s
    return out


# ---------------------------------------------------------------------------------------------
# Diff.

SENSE = re.compile(r"(?<![<>=!-])(==|!=|>=|<=|>|<)(?![<>=])")   # `->` is not a sense
LIT = re.compile(r"0x[0-9a-f]+|\b\d+(?:\.\d+)?\b")
RETN = re.compile(r"\bRET\s+(?:0x[0-9a-f]+|\d+)\b", re.I)               # a callee's cleanup, not an argument
IDENT = re.compile(r"\b(?:m_\w+|slot \d+|vslot \d+|RET 0x[0-9a-f]+|RET \d+)\b")
SIGN = re.compile(r"\b(signed|unsigned|float|double|ordered|unordered)\b", re.I)
CITE = re.compile(r"\((?:[^()]*0x[0-9a-f]{8}[^()]*)\)")
ADDR = re.compile(r"0x[0-9a-f]{8}", re.I)


def strip_cites(s: str) -> str:
    return CITE.sub("", s)


def _lits(text: str) -> list[str]:
    """Numeric literals normalised: `0x0a`, `0xa` and `10` are one value; `RET n` and 8-hex
    addresses (citations) are not literals."""
    text = RETN.sub("", ADDR.sub("", text))
    out = []
    for m in LIT.findall(text.lower()):
        try:
            out.append(str(int(m, 16) if m.startswith("0x") else float(m) if "." in m else int(m)))
        except ValueError:
            out.append(m)
    return out


def _subseq(short, long) -> bool:
    it = iter(long)
    return all(any(x == y for y in it) for x in short)


def signal(cells):
    cells = [strip_cites(c) for c in cells]
    text = " ".join(cells[1:])
    sense = set(SENSE.findall(cells[1])) if len(cells) > 1 else set()
    lits = set(_lits(text))
    idents = set(i.lower() for i in IDENT.findall(text))
    signs = set(s.lower() for s in SIGN.findall(text))
    return sense, lits, idents, signs


def judge(a, b, kind) -> tuple[str, str]:
    sa, la, ia, ga = signal(a)
    sb, lb, ib, gb = signal(b)
    if kind == "branch" and sa and sb and sa != sb:
        return "CONFLICT", f"sense {sorted(sa)} vs {sorted(sb)}"
    if kind == "branch" and ga and gb and ga != gb and not (ga & gb):
        return "CONFLICT", f"signedness {sorted(ga)} vs {sorted(gb)}"
    if kind == "call" and len(a) > 2 and len(b) > 2:
        # The argument cell is the third (call, receiver, args, return); an 8-hex address in it is
        # a citation, never an argument.
        oa, ob = _lits(strip_cites(a[2])), _lits(strip_cites(b[2]))
        # A different ORDER of the shared literals is a conflict; one reader naming an extra
        # literal (a callee's cleanup, a constant's name) beside the same sequence is depth.
        if oa and ob and oa != ob and set(oa) & set(ob) and not (_subseq(oa, ob) or _subseq(ob, oa)):
            return "CONFLICT", f"args {oa} vs {ob}"
    if kind == "branch" and len(a) > 3 and len(b) > 3:
        ta, na = set(_lits(a[2])), set(_lits(a[3]))
        tb, nb = set(_lits(b[2])), set(_lits(b[3]))
        if ta and nb and ta == nb and na == tb and ta != na:
            return "CONFLICT", "taken/not-taken swapped"
    if (ia ^ ib) or (la ^ lb):
        return "DEPTH", f"only one names {sorted((ia ^ ib) | (la ^ lb))[:4]}"
    return "AGREE", ""


def diff_rows(addrs, A: dict[str, str], B: dict[str, str]):
    """Yield (addr, kind, at, verdict, why, rowA, rowB) over both walks."""
    for addr in addrs:
        key = addr.lower()[2:] if addr.lower().startswith("0x") else addr.lower()
        sa, sb = A.get(key), B.get(key)
        if sa is None or sb is None:
            yield addr, "section", None, "MISSING", "A" if sa is None else "B", None, None
            continue
        for kind in ("branch", "call"):
            ra, rb = rows(sa, kind), rows(sb, kind)
            for at in sorted(set(ra) | set(rb)):
                if at not in ra or at not in rb:
                    yield addr, kind, at, "ONE-SIDED", "A" if at not in ra else "B", ra.get(at), rb.get(at)
                    continue
                verdict, why = judge(ra[at], rb[at], kind)
                yield addr, kind, at, verdict, why, ra[at], rb[at]


def cmd_diff(a) -> int:
    wanted = [r[1] for r in _list(a.list)]
    A, B = read_sections(_expand(a.a.split(","))), read_sections(_expand(a.b.split(",")))
    tally = {"AGREE": 0, "CONFLICT": 0, "DEPTH": 0, "ONE-SIDED": 0, "MISSING": 0}
    for addr, kind, at, verdict, why, ra, rb in diff_rows(wanted, A, B):
        tally[verdict] += 1
        if verdict == "MISSING":
            print(f"{addr}: section missing in {why}")
        elif verdict != "AGREE" and (a.show or verdict == "CONFLICT"):
            print(f"{addr} {kind} {at}: {verdict} {why}")
            if verdict == "CONFLICT":
                print(f"    A: {' | '.join(ra[1:])[:220]}")
                print(f"    B: {' | '.join(rb[1:])[:220]}")
    total = sum(tally.values())
    print(f"\nrows {total}: " + ", ".join(f"{k} {v}" for k, v in tally.items()))
    return 0


# ---------------------------------------------------------------------------------------------
# Drill brief.

DRILL_HEAD = """\
# Drill: settle {n} disputed rows of retail vampire.dll (VtMB, 2004) walks

Two readers walked the same skeleton and disagreed on the rows below. For EACH row you get the
skeleton's own line, the listing window around the instruction, and both readings. Settle it from
the listing and, where a callee's `RET n` or a field name is needed, from the vtmb-corpus MCP
(vtmb_asm, vtmb_code, vtmb_fields, vtmb_slot).

Answer with two tables and nothing else:

### Settled branches
| branch | what is compared with what (sense, signedness, float) | taken means | not-taken means | settlement (A / B / neither, and the listing lines that decide it) |

### Settled calls
| call | receiver (ECX) | stack args in order | return used as | settlement (A / B / neither, and the listing lines that decide it) |

Put the FULL corrected row in the first four cells, not just the difference. Cite an address on
every row. Do not open docs/ or Source/ in the repository; edit and create no file.

"""


def cmd_drill_brief(a) -> int:
    wanted = [r[1] for r in _list(a.list)]
    A, B = read_sections(_expand(a.a.split(","))), read_sections(_expand(a.b.split(",")))
    skel = _skeleton_index()
    parts, n = [], 0
    for addr, kind, at, verdict, why, ra, rb in diff_rows(wanted, A, B):
        if verdict != "CONFLICT":
            continue
        s = skel[addr.lower()]
        row = next((x for x in (s["arms"] if kind == "branch" else s["calls"]) if x["at"] == at), None)
        asm = listing_of(addr.lower()[2:]).splitlines()
        idx = next((i for i, l in enumerate(asm) if l.startswith(at[2:])), None)
        window = "\n".join(asm[max(0, (idx or 0) - 14):(idx or 0) + 8]) if idx is not None else "(not in the listing)"
        parts.append(f"## {addr} {s['label']} — {kind} {at}\n\n"
                     f"Diff: {why}\n\nSkeleton: `{json.dumps(row)}`\n\n```\n{window}\n```\n\n"
                     f"Reading A: | {' | '.join(ra[1:])} |\n\nReading B: | {' | '.join(rb[1:])} |\n")
        n += 1
    if not n:
        print("no CONFLICT rows; no drill brief written")
        return 0
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(DRILL_HEAD.format(n=n) + "\n".join(parts), encoding="utf-8")
    print(f"{n} disputed rows → {a.out}")
    return 0


# ---------------------------------------------------------------------------------------------
# Packet.


def wave1_corrections() -> dict[str, list[str]]:
    """address → the wave-1 READING.md paragraphs that cite it, verbatim."""
    path = research_root() / "npc-kernel-checklist" / "cancelled-wave1" / "READING.md"
    out: dict[str, list[str]] = {}
    if not path.is_file():
        return out
    for para in re.split(r"\n(?=\d+\.\s|\n)", path.read_text(encoding="utf-8")):
        for addr in set(m.lower() for m in re.findall(r"0x([0-9a-fA-F]{8})", para)):
            out.setdefault(addr, []).append(para.strip())
    return out


def _block(section: str, name: str) -> str:
    """The prose under a `**Effect**` / `Effect:` / `### Effect` label up to the next label."""
    m = re.search(rf"(?ims)^(?:#+\s*|\*\*|\d+\.\s*\*\*)?{name}\**[.:]?\**[.:]?\s*(.*?)(?=^(?:#+\s*|\*\*|\d+\.\s*\*\*)?"
                  r"(?:branches|arguments|effect|unrecovered|cases)\b|\Z)", section)
    return m.group(1).strip() if m else ""


def _prose(section: str, name: str) -> str:
    """The `name` block of a section that may hold several `## 0x…` parts: the chunk walks of one
    function are joined in order; a re-read (a bounce) replaces the read before it."""
    parts = [p for p in re.split(r"(?m)^(?=##\s+`?0x[0-9a-fA-F]{8})", section) if p.strip()]
    if len(parts) <= 1:
        return _block(section, name)
    if any(re.search(r"chunk \d+", p.splitlines()[0]) for p in parts):
        out = []
        for p in parts:
            m = re.search(r"chunk (\d+)", p.splitlines()[0])
            text = _block(p, name)
            if text and text.lower() != "none":
                out.append((f"chunk {m.group(1)}: " if m else "") + text)
        return "\n  ".join(out) if out else "none"
    return _block(parts[-1], name)


def _settled(drills: dict[str, str]) -> dict[tuple[str, str], list[str]]:
    """(kind, at) → the drill's corrected row (cells) from every drill file given."""
    out: dict[tuple[str, str], list[str]] = {}
    for text in drills.values():
        block = None
        for line in text.splitlines():
            low = line.lower().strip("# ").strip()
            if low.startswith("settled branches") or line.lower().startswith("| branch"):
                block = "branch"
            elif low.startswith("settled calls") or line.lower().startswith("| call"):
                block = "call"
            elif line.startswith("|") and block:
                cells = [c.strip() for c in line.strip().strip("|").split("|")]
                m = re.match(r"`?(0x[0-9a-fA-F]{8})", cells[0])
                if m and not set(cells[0]) <= set("-: "):
                    out[(block, m.group(1).lower())] = cells
    return out


def merge_row(kind, at, verdict, ra, rb, settled, ra_name, rb_name):
    """One merged table row with its provenance cell."""
    width = 4
    if verdict == "ONE-SIDED":
        cells = (ra or rb)[:width]
        prov = f"{ra_name} only" if ra else f"{rb_name} only"
    elif verdict == "CONFLICT":
        s = settled.get((kind, at))
        if s:
            cells = s[:width]
            prov = "drill: " + (s[4] if len(s) > 4 else "settled") + f" — {ra_name}: " + " / ".join(ra[1:width]) \
                   + f" — {rb_name}: " + " / ".join(rb[1:width])
        else:
            cells = ra[:width]
            prov = f"CONFLICT unsettled — {ra_name}: " + " / ".join(ra[1:width]) + f" — {rb_name}: " + " / ".join(rb[1:width])
    elif verdict == "DEPTH":
        _, la, ia, _ = signal(ra)
        _, lb, ib, _ = signal(rb)
        deeper_b = len(lb) + len(ib) > len(la) + len(ia)
        cells = (rb if deeper_b else ra)[:width]
        prov = f"depth: {rb_name if deeper_b else ra_name} names more; other: " + " / ".join((ra if deeper_b else rb)[1:width])
    else:
        cells = ra[:width]
        prov = "agree"
    cells = list(cells) + [""] * (width - len(cells))
    return "| " + " | ".join(c.replace("|", "\\|") if i else c for i, c in enumerate(cells)) + f" | {prov} |"


def build_packet(fam: str, A: dict[str, str], B: dict[str, str], drills: dict[str, str], contra: str,
                 readers: str, note: str = "") -> tuple[str, dict]:
    v, cl, sk, skmd, w1 = verdicts(), checklist(), skeletons(fam), skeleton_sections(fam), wave1_corrections()
    settled = _settled(drills)
    ra_name, rb_name = "A", "B"
    m = re.match(r"\s*A\s*=\s*([^,;]+)[,;]\s*B\s*=\s*([^,;]+)", readers or "")
    if m:
        ra_name, rb_name = m.group(1).strip(), m.group(2).strip()
    rows_all = family_rows(fam)
    rule = [a for a in rows_all if v.get(a, ("?",))[0] == "rule"]
    skipped = [a for a in rows_all if a not in rule]
    stats = {"rule": len(rule), "skipped": len(skipped), "AGREE": 0, "CONFLICT": 0, "DEPTH": 0, "ONE-SIDED": 0,
             "drilled": 0, "unsettled": 0, "missing_a": 0, "missing_b": 0}
    out = [f"<!-- generated by `uv run elysium research kernel_skeleton packet --family {fam}`; do not hand-edit -->",
           f"# Reading packet — {fam} (spec 0019 story 8, pass R)", "",
           f"Readers: {readers or 'A, B'}{(' — ' + note) if note else ''}. Verdicts are the checklist's and do not change here; a packet claim that "
           "contradicts its checklist row is listed under *Packet versus checklist* for the owner.", "",
           f"{len(rule)} `rule` rows walked; {len(skipped)} rows skipped by verdict.", "",
           "## Rows", "", "| address | function | size | verdict | walked |", "|---|---|---|---|---|"]
    for a in rows_all:
        s = sk.get(a, {})
        vv = v.get(a, ("?", "", ""))[0]
        out.append(f"| `0x{a}` | {s.get('label', cl.get(a, {}).get('function', '?'))} | {s.get('size', '?')} | "
                   f"`{vv}` | {'yes' if a in rule else 'no — ' + vv} |")
    out += ["", "## Packet versus checklist", "",
            contra.strip() if contra else "_Not yet produced (contra-brief → reader → `--contra`)._", ""]
    for a in rule:
        s = sk[a]
        row = cl.get(a, {})
        out += [f"## 0x{a} {s['label']}", "",
                f"**Verdict (checklist-19-29, verbatim):** `{row.get('verdict', v[a][0])}` · {row.get('target', v[a][1])} · "
                f"{row.get('evidence', v[a][2])}", ""]
        out += ["### Skeleton (tier 0)", "",
                re.sub(r"^## .*\n", "", skmd.get(a, "_(no skeleton section)_"), count=1).strip(), ""]
        sa, sb = A.get(a), B.get(a)
        out += ["### Walk (merged)", ""]
        if sa is None or sb is None:
            stats["missing_a" if sa is None else "missing_b"] += 1
            only = sb if sa is None else sa
            out += [f"_Only one reader covered this row ({rb_name if sa is None else ra_name}); its walk verbatim:_", "",
                    only.strip() if only else "_no walk_", ""]
            continue
        for kind, title, head in (("case", "Cases", "| arm | case ids | what the arm does |"),
                                  ("branch", "Branches", "| branch | compared | taken means | not-taken means |"),
                                  ("call", "Arguments", "| call | receiver (ECX) | stack args in order | return used as |")):
            ra_rows, rb_rows = rows(sa, kind), rows(sb, kind)
            if kind == "case" and not (ra_rows or rb_rows):
                continue
            lines = [f"**{title}**", "", head + " provenance |", "|---" * head.count("|") + "|"]
            for at in sorted(set(ra_rows) | set(rb_rows)):
                if at in ra_rows and at in rb_rows:
                    verdict, why = judge(ra_rows[at], rb_rows[at], kind) if kind != "case" else ("AGREE", "")
                else:
                    verdict = "ONE-SIDED"
                if kind != "case":
                    stats[verdict] += 1
                    if verdict == "CONFLICT":
                        if (kind, at) in settled:
                            stats["drilled"] += 1
                        else:
                            stats["unsettled"] += 1
                if kind == "case":
                    cells = (ra_rows.get(at) or rb_rows.get(at))[:3]
                    prov = "both" if at in ra_rows and at in rb_rows else (f"{ra_name} only" if at in ra_rows else f"{rb_name} only")
                    lines.append("| " + " | ".join(cells + [""] * (3 - len(cells))) + f" | {prov} |")
                else:
                    lines.append(merge_row(kind, at, verdict, ra_rows.get(at), rb_rows.get(at), settled, ra_name, rb_name))
            if len(lines) == 4:
                lines = [f"**{title}:** none"]
            out += lines + [""]
        ea, eb = _prose(sa, "effect"), _prose(sb, "effect")
        out += ["**Effect**", "", f"- {ra_name}: {ea or '—'}", f"- {rb_name}: {eb or '—'}", ""]
        if a in w1:
            out += ["**Banked wave-1 corrections that cite this address**", ""] + [f"> {p}" for p in w1[a]] + [""]
        ua, ub = _prose(sa, "unrecovered"), _prose(sb, "unrecovered")
        out += ["**Unrecovered**", "", f"- {ra_name}: {ua or 'none'}", f"- {rb_name}: {ub or 'none'}", ""]
    return "\n".join(out), stats


def cmd_packet(a) -> int:
    A, B = read_sections(_expand(a.a)), read_sections(_expand(a.b))
    drills = {p: Path(p).read_text(encoding="utf-8") for p in _expand(a.drill or [])}
    contra = Path(a.contra).read_text(encoding="utf-8") if a.contra else ""
    text, stats = build_packet(a.family, A, B, drills, contra, a.readers, a.note or "")
    out = Path(a.out) if a.out else families_dir() / f"{a.family}-READING.md"
    out.write_text(text, encoding="utf-8")
    print(f"{a.family}: {stats} → {out}")
    return 0


def _expand(patterns) -> list[str]:
    """Globs expanded; a pattern that matches nothing is dropped (a reader tier not yet run)."""
    out = []
    for p in patterns:
        hits = sorted(glob.glob(p))
        out += hits if hits or any(ch in p for ch in "*?[") else [p]
    return out


# ---------------------------------------------------------------------------------------------
# Contra brief.

CONTRA_HEAD = """\
# Packet versus checklist: {fam} ({n} rows) — retail vampire.dll (VtMB, 2004)

For each function below you get (1) the CHECKLIST row's evidence, written earlier by one reader
from the decompile, and (2) the PACKET's merged walk, written now by two readers on the listing
skeleton. List every claim where the two CONTRADICT each other: a different comparison sense or
threshold, a different field or offset, a different callee or argument, a different order of
arms, a different write, a different early-return. A packet row that merely says MORE than the
checklist is not a contradiction. Where the two disagree, decide from the listing (vtmb_asm) and
say which is right.

Answer with ONE table and nothing else:

| address | checklist says | packet says | which is right, and the listing line that decides it |

Write `| none | | | |` if no row contradicts. Cite an address on every row. Do not open docs/ or
Source/ in the repository; edit and create no file.

"""


def cmd_contra_brief(a) -> int:
    packet = Path(a.packet) if a.packet else families_dir() / f"{a.family}-READING.md"
    secs = sections(packet.read_text(encoding="utf-8"))
    cl, v = checklist(), verdicts()
    parts = []
    for addr in family_rows(a.family):
        if v.get(addr, ("?",))[0] != "rule" or addr not in secs:
            continue
        sec = secs[addr]
        walk = sec.split("### Walk (merged)", 1)[-1] if "### Walk (merged)" in sec else sec
        walk = re.sub(r"\n\*\*Banked wave-1.*?(?=\n\*\*Unrecovered)", "", walk, flags=re.S)
        row = cl.get(addr, {})
        parts.append(f"## 0x{addr} {row.get('function', '')}\n\n**Checklist evidence:** {row.get('evidence', v[addr][2])}\n\n"
                     f"**Packet walk:**\n{walk.strip()}\n")
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(CONTRA_HEAD.format(fam=a.family, n=len(parts)) + "\n".join(parts), encoding="utf-8")
    print(f"{len(parts)} rows → {a.out}")
    return 0


# ---------------------------------------------------------------------------------------------
# Check packet.


def check_packet(fam: str) -> list[str]:
    v, sk = verdicts(), skeletons(fam)
    path = families_dir() / f"{fam}-READING.md"
    if not path.is_file():
        return [f"{fam}: no packet {path.name}"]
    secs = sections(path.read_text(encoding="utf-8"))
    problems = []
    for addr in family_rows(fam):
        if v.get(addr, ("?",))[0] != "rule":
            continue
        sec = secs.get(addr)
        if sec is None:
            problems.append(f"{fam} 0x{addr}: no section")
            continue
        walk = sec.split("### Walk (merged)", 1)[-1]
        s = sk[addr]
        br, ca = rows(walk, "branch"), rows(walk, "call")
        if "Only one reader covered" in walk:
            found = set(re.findall(r"0x[0-9a-f]{8}", walk.lower()))
            br = {x: [] for x in found}
            ca = br
        ma = [x["at"] for x in s["arms"] if x["at"] not in br]
        mc = [x["at"] for x in s["calls"] if x["at"] not in ca]
        if ma:
            problems.append(f"{fam} 0x{addr}: {len(ma)} arms without a branch row: " + ", ".join(ma[:6]))
        if mc:
            problems.append(f"{fam} 0x{addr}: {len(mc)} calls without an argument row: " + ", ".join(mc[:6]))
    return problems


# The four families ported before pass R from the cancelled wave-1 reading; they get no packet.
LANDED = {"State19", "Translate19", "Lifecycle19", "Maintain19"}


def cmd_check_packet(a) -> int:
    fams = [a.family] if a.family else [f for f in all_families() if f not in LANDED
                                        and ((families_dir() / f"{f}-READING.md").is_file() or not a.existing)]
    bad = 0
    for fam in fams:
        problems = check_packet(fam)
        if problems:
            bad += 1
            print("\n".join(problems))
        else:
            n = sum(1 for x in family_rows(fam) if verdicts().get(x, ("?",))[0] == "rule")
            print(f"{fam}: {n} rule rows, every arm and call held")
    return 1 if bad else 0


# ---------------------------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="kernel_skeleton", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("brief")
    b.add_argument("--family")
    b.add_argument("--band", choices=list(BANDS))
    b.add_argument("--batch", type=int)
    b.add_argument("--per-function", action="store_true")
    b.add_argument("--addr")
    b.add_argument("--chunk", help="a switch body to split")
    b.add_argument("--chunks", type=int, default=4)
    b.add_argument("--verdict", default="rule")
    b.add_argument("--out", required=True)
    b.set_defaults(fn=cmd_brief)
    c = sub.add_parser("check-walk")
    c.add_argument("list")
    c.add_argument("walk", help="walk file(s): comma-separated globs; chunk walks of one function are joined")
    c.set_defaults(fn=cmd_check_walk)
    d = sub.add_parser("diff")
    d.add_argument("list")
    d.add_argument("a", help="walk file(s): comma-separated globs")
    d.add_argument("b", help="walk file(s): comma-separated globs")
    d.add_argument("--show", action="store_true")
    d.set_defaults(fn=cmd_diff)
    e = sub.add_parser("drill-brief")
    e.add_argument("list")
    e.add_argument("a")
    e.add_argument("b")
    e.add_argument("--out", required=True)
    e.set_defaults(fn=cmd_drill_brief)
    p = sub.add_parser("packet")
    p.add_argument("--family", required=True)
    p.add_argument("--a", nargs="+", required=True, help="reader A walks (globs)")
    p.add_argument("--b", nargs="+", required=True, help="reader B walks (globs)")
    p.add_argument("--drill", nargs="*", help="judge outputs (globs)")
    p.add_argument("--contra", help="the packet-versus-checklist table")
    p.add_argument("--readers", default="A=luna; B=Sol", help='short labels, "A=<name>; B=<name>"')
    p.add_argument("--note", help="a sentence on the readers and tiers, printed in the header")
    p.add_argument("--out")
    p.set_defaults(fn=cmd_packet)
    q = sub.add_parser("contra-brief")
    q.add_argument("--family", required=True)
    q.add_argument("--packet")
    q.add_argument("--out", required=True)
    q.set_defaults(fn=cmd_contra_brief)
    r = sub.add_parser("check-packet")
    r.add_argument("--family")
    r.add_argument("--all", action="store_true")
    r.add_argument("--existing", action="store_true", help="only families that have a packet")
    r.set_defaults(fn=cmd_check_packet)
    a = ap.parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
