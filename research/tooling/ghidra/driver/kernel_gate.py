#!/usr/bin/env python3
"""The mechanical family gate of spec 0019 story 8: what a landed port family must show.

A story-8 family ports a list of retail NPC-kernel functions. Whether each body is *right* is a
reader's question; whether the family *claims* to be done is mechanical, and this gate asks only
that half, from the family row file, the authoritative verdicts and the skeleton, against the
tree and the branch diff:

1. **addresses** — every `rule` address of the family is cited (`0x<addr>`, or the bare eight
   digits) in a Substrate or Tests source file that is not a census file (the shape table, the
   shape map, the generated `*Slots.inl` / `*Slots.cpp`, the generated override census). A census
   row is not a port.
2. **arms** — for every `rule` function, every conditional branch, every table jump and every
   call site in its skeleton is cited somewhere under `Source/ElysiumUE/Private/` (the trailing
   `// 0x1029a0f3` body comments, or a test). Threshold per function: `--arms all|N%`.
3. **seams** — every declaration the diff adds to a Substrate header or `.inl` gets the three
   searches of the lessons block printed: the retail address its comment cites, elsewhere in
   `Source/`; the retail offset (`+0x…`) in `ElysiumNpcKernelShapeMap.cpp`; the declared name and
   any `m_…` retail name in the Substrate `.inl` files. A hit is a WARN a reader judges; a hit
   on a line the same diff adds (the new definition citing its own address) is listed, not warned.
   The diff is `git diff <merge-base>` against the working tree, plus untracked files.
   **Opt-in**: it depends on the diff alone, never on the family, and on a wide integration diff
   it prints the same thousand-odd lines for every family; it runs under `--seams` or `--all`
   and is otherwise reported `SKIP seams (--seams to run)`.
4. **hot** — the diff does not touch the frozen hot headers and generated census files.
5. **residue** — a fresh `kernel_shape --unported` carries none of the family's `rule` addresses,
   and no row that the committed `docs/vtmb/npc-kernel/unported.tsv` lacks (nothing rose).
6. **tests** — `Tests/<family_stem>Tests*.cpp` (`ElysiumNpcKernel<Family less 19>`) exist, register at least as many
   automation tests as the family has `rule` rows, and either every test name carries a retail
   address or the tests cite every `rule` address. A citation in another (non-census) test file
   counts and is listed: a rule tested beside its sibling family is still tested, so it also counts
   toward the registration floor when no family test name carries its address.
7. **twins** — no Substrate `.cpp` cites a `rule` address in a comment that also says
   "CHOSEN, NOT RECOVERED", "port-only" or "stand-in" (WARN: the body must replace, not shadow).

Usage::

    uv run elysium research kernel_gate --family Conditions19
    uv run elysium research kernel_gate --family Lifecycle19 --no-residue --arms 0%
    uv run elysium research kernel_gate --family Spawn19 --base main --json out.json --seams
    uv run elysium research kernel_gate --family RunAi19,Think19
    uv run elysium research kernel_gate --all --json-dir gates/
    uv run elysium research kernel_gate --all --lane wave2-integrator

`--all` gates every story-8 family (`ALL_FAMILIES`) in one process; `--family A,B` gates a list.
Several families share one run: the source index and the diff are built once, the seams check
(on by default under `--all`, `--no-seams` to drop it) is computed and printed once, and the
residue table (`kernel_shape --unported`, in process, through the same memoized `build`
attributes `research/tooling/kernel.py` shares) is made once and joined per family. The report
ends in one family x check table; `--json-dir` writes one `<Family>.json` per family in the
`--json` shape, and `--json` then holds that table.

`--lane wave2-integrator` is the one lane allowed to edit the hot headers (the wave-2 integrator
runs alone on `main`'s successor and batches its header pass): it presets `--allow-hot` with no
argument, so every hot touch is `ALLOWED` (WARN) instead of FAIL. Every other lane leaves it off.

Exit 0 only when no check FAILs (WARN and SKIP do not fail the gate); with several families, 1 when
any family fails. Through the `elysium` CLI a failing tool surfaces as exit 7 and the report is
filtered; pass `-v` (`uv run elysium -v research kernel_gate ...`) to see it whole, or `--json` for
the structured result. The tool's own `-v` (`uv run elysium -v research kernel_gate --family X -v`)
appends the time each stage and check took.
"""

from __future__ import annotations

import argparse
import bisect
import contextlib
import dataclasses
import io
import itertools
import json
import re
import subprocess
import sys
import tempfile
import time
import traceback
from pathlib import Path

_HERE = Path(__file__).resolve().parent
DEFAULT_REPO = _HERE.parents[3]

PRIVATE = Path("Source/ElysiumUE/Private")
SUBSTRATE = PRIVATE / "Substrate"
TESTS = PRIVATE / "Tests"
SHAPE_MAP = SUBSTRATE / "ElysiumNpcKernelShapeMap.cpp"
VERDICTS = Path("research/tooling/ghidra/driver/kernel_verdicts.tsv")
KERNEL_SHAPE = Path("research/tooling/ghidra/driver/kernel_shape.py")
UNPORTED_PIN = Path("docs/vtmb/npc-kernel/unported.tsv")
SOURCE_SUFFIXES = frozenset({".cpp", ".h", ".inl", ".hpp", ".c"})

# The census: generated tables that name every retail address whether or not a body stands.
CENSUS_NAMES = frozenset({
    "ElysiumNpcKernelShape.cpp", "ElysiumNpcKernelShapeMap.cpp",
    "ElysiumNpcKernelOverrideCensus.cpp", "ElysiumNpcKernelOverrideCensus.h",
})
CENSUS_SUFFIXES = ("Slots.inl", "Slots.cpp")

HOT_FILES = tuple(str(SUBSTRATE / n).replace("\\", "/") for n in (
    "ElysiumNpc.h", "ElysiumNpcBase.h", "ElysiumNpcConditions.h", "ElysiumNpcScheduleHost.h",
    "ElysiumNpcKernelTunables.h", "ElysiumNpcKernelShape.h", "ElysiumNpcEnemy.h",
    "ElysiumNpcBaseSlots.inl", "ElysiumNpcSlots.inl", "ElysiumNpcKernelShape.cpp",
    "ElysiumNpcKernelShapeMap.cpp", "ElysiumNpcBaseSlots.cpp", "ElysiumNpcSlots.cpp",
))
HOT_PREFIXES = ("research/tooling/gen_kernel_shape.py",)
# A family `<F>19`'s files dropped the `19` in story 8 pass C (`pass-i/rename-table.md`): they are
# `ElysiumNpcBase<F>`, `ElysiumNpc<F>` (+ `Species`, `_2`) and `ElysiumNpcKernel<F>Tests`, with the
# lowest free numbered part where that stem already held an older concern's file.
FILE_PARTS = {
    ("ElysiumNpcBase", "Boss19"): "Boss2", ("ElysiumNpcBase", "Conditions19"): "Conditions2",
    ("ElysiumNpcBase", "Damage19"): "Damage2", ("ElysiumNpcBase", "Lifecycle19"): "Lifecycle2",
    ("ElysiumNpcBase", "Misc19"): "Misc2",
    ("ElysiumNpc", "Conditions19"): "Conditions2", ("ElysiumNpc", "Damage19"): "Damage3",
    ("ElysiumNpc", "Lifecycle19"): "Lifecycle2", ("ElysiumNpc", "Misc19"): "Misc2",
    ("ElysiumNpc", "Werewolf19"): "Werewolf2",
    ("ElysiumNpcKernel", "Conditions19"): "Conditions2", ("ElysiumNpcKernel", "Damage19"): "Damage2",
    ("ElysiumNpcKernel", "Lifecycle19"): "Lifecycle2", ("ElysiumNpcKernel", "Misc19"): "Misc2",
}


def family_stem(prefix: str, family: str) -> str:
    """The file stem of `family` (`Think19`) under `prefix` (ElysiumNpcBase / ElysiumNpc / ElysiumNpcKernel)."""
    return prefix + FILE_PARTS.get((prefix, family), family.removesuffix("19"))


# Lanes that preset `--allow-hot`: lane -> the allowed hot files ([] = every one).
LANES = {"wave2-integrator": []}

# The story-8 families `--all` gates, in landing order.
ALL_FAMILIES = ("Conditions19", "RunAi19", "StartTask19", "RunTask19", "Select19", "Think19", "Spawn19",
                "Damage19", "Script19", "Boss19", "Werewolf19", "Misc19", "Damaged19")

TWIN_MARKERS = ("chosen, not recovered", "port-only", "stand-in")

TEST_MACRO_RE = re.compile(
    r"\b(?:IMPLEMENT_(?:CUSTOM_)?(?:SIMPLE|COMPLEX)_AUTOMATION_TEST(?:_PRIVATE)?|BEGIN_DEFINE_SPEC)"
    r"\s*\(\s*(\w+)\s*,(?:\s*\w+\s*,)?\s*(?:TEXT\()?\"([^\"]*)\"")
# An address as the port cites it, on lower-cased text: `0x10273ad0`, the bare `10273ad0`, or
# Ghidra's `FUN_10273ad0` / `DAT_…` / `LAB_…` spelling.
HEX8_RE = re.compile(r"(?<![0-9a-z])(?:0x|fun_|dat_|lab_|sub_)?([0-9a-f]{8})(?![0-9a-z_])")
# Every HEX8_RE match is a run of exactly eight hex digits bounded by non-hex characters (a `0x`,
# `fun_` … prefix ends in a non-hex character), so one pass over these runs answers both HEX8_RE
# and the seam address search `(?<![0-9a-f])<addr>(?![0-9a-f])`; `_is_citation` narrows a run to
# HEX8_RE's stricter bounds.
RUN8_RE = re.compile(r"(?<![0-9a-f])[0-9a-f]{8}(?![0-9a-f])")
# The same runs, found as every run of eight or more hex digits and kept at exactly eight: the
# regex no longer tests both bounds at every character (`SourceIndex._add`, ~40% faster).
HEX_RUN_RE = re.compile(r"[0-9a-f]{8,}")
_ALNUM = frozenset("0123456789abcdefghijklmnopqrstuvwxyz")
_CITE_PREFIXES = ("0x", "fun_", "dat_", "lab_", "sub_")
NAMED_RE = re.compile(r"0x[0-9a-f]{8}(?![0-9a-f])")
RETAIL_VA_RE = re.compile(r"(?:0x|FUN_|DAT_|LAB_)(10[0-9a-fA-F]{6})(?![0-9a-fA-F])")
OFFSET_RE = re.compile(r"\+\s*0x([0-9a-fA-F]{1,5})(?![0-9a-fA-F])")
RETAIL_NAME_RE = re.compile(r"\bm_\w+")

PASS, FAIL, WARN, SKIP = "PASS", "FAIL", "WARN", "SKIP"


# ---------------------------------------------------------------------------------------------
# Inputs.


def read_rows(path: Path) -> list[list[str]]:
    """A TSV with `#` comment lines and an optional `address` header; addresses normalised."""
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        addr = cells[0].strip().lower()
        addr = addr.removeprefix("0x")
        if not re.fullmatch(r"[0-9a-f]{8}", addr):
            continue
        out.append([addr, *cells[1:]])
    return out


def load_verdicts(path: Path) -> dict[str, str]:
    return {r[0]: (r[1].strip() if len(r) > 1 else "") for r in read_rows(path)}


@dataclasses.dataclass
class Family:
    name: str
    rows: list[str]                       # every address of the family file, in order
    rules: list[str]                      # the authoritative `rule` subset, in order
    verdict: dict[str, str]               # address -> authoritative verdict ("" when absent)
    skeleton: dict[str, dict]             # address -> skeleton JSON function


def load_family(name: str, families: Path, skeletons: Path, verdicts: Path) -> Family:
    fam_file = families / f"{name}.tsv"
    if not fam_file.is_file():
        raise SystemExit(f"no family file {fam_file}")
    all_verdicts = load_verdicts(verdicts)
    rows = [r[0] for r in read_rows(fam_file)]
    verdict = {a: all_verdicts.get(a, "") for a in rows}
    rules = [a for a in rows if verdict[a] == "rule"]
    skel: dict[str, dict] = {}
    js = skeletons / f"{name}.json"
    if js.is_file():
        for fn in json.loads(js.read_text(encoding="utf-8")):
            skel[fn["addr"].lower().removeprefix("0x")] = fn
    return Family(name, rows, rules, verdict, skel)


def _is_citation(low: str, s: int, e: int) -> bool:
    """Whether the eight-digit run `low[s:e]` is a HEX8_RE match (a port's citation)."""
    if e < len(low) and (low[e] in _ALNUM or low[e] == "_"):
        return False
    if s == 0 or low[s - 1] not in _ALNUM:
        return True
    for p in _CITE_PREFIXES:
        at = s - len(p)
        if at >= 0 and low.startswith(p, at) and (at == 0 or low[at - 1] not in _ALNUM):
            return True
    return False


class SourceIndex:
    """Every source file under a root, read once, and one address index built in one regex pass.

    `tokens` is each file's set of HEX8_RE citations; `where` the files citing an address and
    `cites` its (file, line) sites, both in file order; `runs` every (file, line) holding the
    address as a bare eight-hex-digit run (the seam search's looser bound). Lines are 1-based and
    counted as `str.splitlines` counts them; a line appears once per address however often it
    repeats it.
    """

    def __init__(self, repo: Path, root: Path):
        self.repo = repo
        self.files: dict[str, str] = {}          # repo-relative posix path -> original text
        self.tokens: dict[str, set[str]] = {}
        self.where: dict[str, list[str]] = {}
        self.cites: dict[str, list[tuple[str, int]]] = {}
        self.runs: dict[str, list[tuple[str, int]]] = {}
        self.order: dict[str, int] = {}
        self._lines: dict[str, list[str]] = {}
        self._lower: dict[str, str] = {}
        base = repo / root
        if base.is_dir():
            for p in sorted(base.rglob("*")):
                if p.suffix.lower() in SOURCE_SUFFIXES and p.is_file():
                    rel = p.relative_to(repo).as_posix()
                    self._add(rel, p.read_text(encoding="utf-8", errors="replace"))

    def _add(self, rel: str, text: str) -> None:
        self.order[rel] = len(self.files)
        self.files[rel] = text
        low = text.lower()
        self._lower[rel] = low
        toks: set[str] = set()
        # Where each line starts, as `str.splitlines` breaks them, summed in C.
        starts = [0, *itertools.accumulate(map(len, low.splitlines(keepends=True)))]
        for m in HEX_RUN_RE.finditer(low):
            if m.end() - m.start() != 8:
                continue
            tok = m.group()
            site = (rel, bisect.bisect_right(starts, m.start()))
            runs = self.runs.setdefault(tok, [])
            if not runs or runs[-1] != site:
                runs.append(site)
            if not _is_citation(low, m.start(), m.end()):
                continue
            if tok not in toks:
                toks.add(tok)
                self.where.setdefault(tok, []).append(rel)
            cites = self.cites.setdefault(tok, [])
            if not cites or cites[-1] != site:
                cites.append(site)
        self.tokens[rel] = toks

    def lines(self, rel: str) -> list[str]:
        if rel not in self._lines:
            self._lines[rel] = self.files[rel].splitlines()
        return self._lines[rel]

    def lower(self, rel: str) -> str:
        return self._lower[rel]

    def cited(self, addr: str, keep=lambda rel: True) -> list[str]:
        return [rel for rel in self.where.get(addr, ()) if keep(rel)]


def is_census(rel: str) -> bool:
    name = rel.rsplit("/", 1)[-1]
    return name in CENSUS_NAMES or name.endswith(CENSUS_SUFFIXES)


def under(rel: str, root: Path) -> bool:
    return rel.startswith(root.as_posix().rstrip("/") + "/")


# ---------------------------------------------------------------------------------------------
# Results.


@dataclasses.dataclass
class Check:
    key: str
    title: str
    status: str = PASS
    lines: list[str] = dataclasses.field(default_factory=list)
    data: dict = dataclasses.field(default_factory=dict)

    def fail(self, line: str | None = None) -> None:
        self.status = FAIL
        if line:
            self.lines.append(line)

    def warn(self, line: str) -> None:
        if self.status == PASS:
            self.status = WARN
        self.lines.append(line)

    def render(self) -> str:
        head = f"== [{self.status}] {self.key}: {self.title}"
        return "\n".join([head, *("  " + ln for ln in self.lines)])


# ---------------------------------------------------------------------------------------------
# 1. Addresses.


def check_addresses(fam: Family, src: SourceIndex) -> Check:
    c = Check("addresses", "every rule address cited outside the census (Substrate/, Tests/)")
    keep = lambda rel: (under(rel, SUBSTRATE) or under(rel, TESTS)) and not is_census(rel)
    missing = []
    for a in fam.rules:
        if not src.cited(a, keep):
            missing.append(a)
    c.data = {"rules": len(fam.rules), "missing": [f"0x{a}" for a in missing]}
    c.lines.append(f"{len(fam.rules) - len(missing)}/{len(fam.rules)} rule addresses cited")
    unknown = [a for a in fam.rows if not fam.verdict[a]]
    if unknown:
        c.lines.append("not in kernel_verdicts.tsv (treated as non-rule): "
                       + ", ".join(f"0x{a}" for a in unknown))
    for a in missing:
        where = src.cited(a, lambda rel: is_census(rel))
        c.fail(f"MISSING 0x{a}" + (f"  (census only: {', '.join(where)})" if where else ""))
    if not fam.rules:
        c.lines.append("family has no rule rows")
    return c


# ---------------------------------------------------------------------------------------------
# 2. Arms.


def parse_threshold(text: str) -> float:
    t = text.strip().lower()
    if t == "all":
        return 100.0
    m = re.fullmatch(r"(\d+(?:\.\d+)?)%?", t)
    if not m:
        raise argparse.ArgumentTypeError(f"--arms wants all or N%, got {text!r}")
    return float(m.group(1))


def skeleton_sites(fn: dict) -> list[tuple[str, str]]:
    """(address, kind) for every branch, table jump and call site of one skeleton function."""
    out = []
    for arm in fn.get("arms", ()):
        out.append((arm["at"].lower().removeprefix("0x"), f"branch {arm.get('op', '')}".strip()))
    for tbl in fn.get("tables", ()):
        out.append((tbl["at"].lower().removeprefix("0x"), "table jump"))
    for call in fn.get("calls", ()):
        out.append((call["at"].lower().removeprefix("0x"), "call"))
    seen: set[str] = set()
    uniq = []
    for a, k in out:
        if a not in seen:
            seen.add(a)
            uniq.append((a, k))
    return uniq


def check_arms(fam: Family, src: SourceIndex, threshold: float) -> Check:
    label = "all" if threshold >= 100.0 else f"{threshold:g}%"
    c = Check("arms", f"skeleton branch and call sites cited under Source/ElysiumUE/Private/ "
                      f"(threshold {label} per function)")
    private = lambda rel: under(rel, PRIVATE)
    per_fn = {}
    total_cov = total_all = 0
    for a in fam.rules:
        fn = fam.skeleton.get(a)
        if fn is None:
            c.fail(f"0x{a}: no skeleton function (run kernel_skeleton --family {fam.name})")
            per_fn[f"0x{a}"] = {"covered": 0, "total": None, "missing": []}
            continue
        sites = skeleton_sites(fn)
        missing = [(s, k) for s, k in sites if not src.cited(s, private)]
        covered = len(sites) - len(missing)
        total_cov += covered
        total_all += len(sites)
        pct = 100.0 if not sites else 100.0 * covered / len(sites)
        ok = pct + 1e-9 >= threshold
        per_fn[f"0x{a}"] = {"label": fn.get("label", ""), "covered": covered, "total": len(sites),
                            "missing": [f"0x{s}" for s, _ in missing], "pass": ok}
        line = f"0x{a} {fn.get('label', '')}: {covered}/{len(sites)}"
        if fn.get("damaged"):
            line += f"  [skeleton damaged: {fn['damaged']}]"
        if not ok:
            c.fail(line)
        else:
            c.lines.append(line)
        if missing and (not ok or threshold >= 100.0):
            cells = [f"0x{s}({k})" for s, k in missing]
            for i in range(0, len(cells), 8):
                c.lines.append(("    missing: " if i == 0 else "             ") + " ".join(cells[i:i + 8]))
    c.lines.insert(0, f"family total {total_cov}/{total_all} sites cited")
    c.data = {"threshold": threshold, "covered": total_cov, "total": total_all, "functions": per_fn}
    return c


# ---------------------------------------------------------------------------------------------
# Git diff.


def git(repo: Path, *args: str) -> str:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True,
                          text=True, encoding="utf-8", errors="replace").stdout


def resolve_base(repo: Path, base: str | None) -> str:
    ref = base or "main"
    return git(repo, "merge-base", ref, "HEAD").strip()


@dataclasses.dataclass
class Diff:
    base: str
    changed: list[str]                          # repo-relative posix paths (both sides of a rename)
    added: dict[str, list[tuple[int, str]]]     # path -> (post-image line number, text)


def collect_diff(repo: Path, base: str | None) -> Diff:
    mb = resolve_base(repo, base)
    changed = [ln.strip() for ln in git(repo, "diff", "--name-only", "--no-renames", mb).splitlines()
               if ln.strip()]
    untracked = [ln.strip() for ln in git(repo, "ls-files", "--others", "--exclude-standard").splitlines()
                 if ln.strip()]
    added: dict[str, list[tuple[int, str]]] = {}
    path = None
    lineno = 0
    for raw in git(repo, "diff", "--no-color", "--no-renames", "-U0", mb).splitlines():
        if raw.startswith("+++ "):
            tgt = raw[4:].strip()
            path = None if tgt == "/dev/null" else tgt.removeprefix("b/")
            continue
        if raw.startswith(("--- ", "diff --git", "index ")):
            continue
        m = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@", raw)
        if m:
            lineno = int(m.group(1))
            continue
        if path is None:
            continue
        if raw.startswith("+"):
            added.setdefault(path, []).append((lineno, raw[1:]))
            lineno += 1
        elif raw.startswith(" "):
            lineno += 1
    for rel in untracked:
        p = repo / rel
        if p.suffix.lower() in SOURCE_SUFFIXES and p.is_file():
            text = p.read_text(encoding="utf-8", errors="replace").splitlines()
            added[rel] = [(i + 1, t) for i, t in enumerate(text)]
    return Diff(mb, sorted(set(changed) | set(untracked)), added)


# ---------------------------------------------------------------------------------------------
# 3. Seams.

_KEYWORDS = frozenset({
    "return", "if", "else", "for", "while", "switch", "case", "do", "break", "continue", "goto",
    "delete", "new", "throw", "using", "typedef", "namespace", "friend", "template", "sizeof",
    "default", "public", "private", "protected", "operator", "co_return", "static_assert",
    "check", "ensure", "verify", "checkf", "ensureMsgf", "UE_LOG", "TEXT", "DECLARE_LOG_CATEGORY_EXTERN",
    "GENERATED_BODY", "UPROPERTY", "UFUNCTION", "enum", "struct", "class", "union",
})
_QUAL = r"(?:(?:static|virtual|inline|FORCEINLINE|FORCENOINLINE|constexpr|explicit|mutable|const|volatile|" \
        r"UE_NODISCARD|\[\[nodiscard\]\])\s+)*"
_TYPE = r"(?:[A-Za-z_][\w:]*(?:\s*<[^;(){}]*>)?(?:\s*::\s*\w+)*)(?:\s+const)?[\s\*&]+"
METHOD_RE = re.compile(r"^\s*" + _QUAL + _TYPE + r"(~?[A-Za-z_]\w*)\s*\(")
FIELD_RE = re.compile(r"^\s*" + _QUAL + _TYPE + r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*|\{[^;]*\})?\s*;")


def _strip_trailing_comment(line: str) -> tuple[str, str]:
    i = line.find("//")
    if i < 0:
        return line, ""
    return line[:i], line[i + 2:]


def _is_comment(line: str) -> bool:
    s = line.strip()
    return s.startswith(("//", "/*", "*", "*/"))


def declared_name(line: str) -> str | None:
    code, _ = _strip_trailing_comment(line)
    s = code.strip()
    if not s or _is_comment(line) or s.startswith("#") or s.endswith(":") and "::" not in s[-2:]:
        return None
    first = re.match(r"[A-Za-z_]\w*", s)
    if first and first.group(0) in _KEYWORDS:
        return None
    m = METHOD_RE.match(code) or FIELD_RE.match(code)
    if not m:
        return None
    name = m.group(1)
    if name in _KEYWORDS:
        return None
    return name


def comment_context(lines: list[str], idx: int) -> str:
    """The declaration's trailing `//` comment plus the contiguous comment block right above it."""
    _, trailing = _strip_trailing_comment(lines[idx])
    block = []
    j = idx - 1
    while j >= 0 and _is_comment(lines[j]):
        block.append(lines[j])
        j -= 1
    return "\n".join(reversed(block)) + ("\n" + trailing if trailing else "")


@dataclasses.dataclass
class Hit:
    rel: str
    line: int
    text: str


def grep(src: SourceIndex, pattern: re.Pattern, keep, exclude: set[tuple[str, int]],
         needle: str | None = None) -> list[Hit]:
    """Every line of a kept file `pattern` finds. `needle` is a substring every hit must hold
    (lower-cased when the pattern ignores case): a file without it is not split into lines."""
    out = []
    folded = bool(pattern.flags & re.IGNORECASE)
    for rel, text in src.files.items():
        if not keep(rel):
            continue
        if needle is not None and needle not in (src.lower(rel) if folded else text):
            continue
        for i, ln in enumerate(src.lines(rel), 1):
            if (rel, i) in exclude:
                continue
            if pattern.search(ln):
                out.append(Hit(rel, i, ln.strip()))
    return out


def grep_address(src: SourceIndex, addr: str, keep, exclude: set[tuple[str, int]]) -> list[Hit]:
    """`grep` for `(?<![0-9a-f])<addr>(?![0-9a-f])`, case-folded, answered from the run index."""
    return [Hit(rel, i, src.lines(rel)[i - 1].strip()) for rel, i in src.runs.get(addr, ())
            if keep(rel) and (rel, i) not in exclude]


SEAMS_TITLE = ("the three lessons-block searches for every added Substrate declaration "
               "(address in Source/, offset in the shape map, name in the .inl files)")


def skip_seams() -> Check:
    c = Check("seams", SEAMS_TITLE, status=SKIP)
    c.lines.append("SKIP seams (--seams to run)")
    return c


def check_seams(diff: Diff | None, repo: Path, src_all: SourceIndex, limit: int = 8) -> Check:
    c = Check("seams", SEAMS_TITLE)
    if diff is None:
        c.status = SKIP
        c.lines.append("no diff (git unavailable)")
        return c
    ours = {(rel, n) for rel, added in diff.added.items() for n, _ in added}
    decls = []
    for rel, added in sorted(diff.added.items()):
        if not under(rel, SUBSTRATE) or Path(rel).suffix.lower() not in (".h", ".inl"):
            continue
        p = repo / rel
        if rel in src_all.files:
            post = src_all.lines(rel)
        else:
            post = p.read_text(encoding="utf-8", errors="replace").splitlines() if p.is_file() else []
        for lineno, text in added:
            name = declared_name(text)
            if not name:
                continue
            ctx = comment_context(post, lineno - 1) if 0 < lineno <= len(post) else ""
            decls.append((rel, lineno, text.strip(), name, ctx))
    c.data["declarations"] = []
    if not decls:
        c.lines.append("no added declarations in Substrate .h/.inl")
        return c
    shape_map = SHAPE_MAP.as_posix()
    for rel, lineno, text, name, ctx in decls:
        # The declaration's own lines are not evidence against it.
        own_lo = lineno - ctx.count("\n") - 1
        exclude = {(rel, n) for n in range(max(1, own_lo), lineno + 1)}
        addrs = sorted({a.lower() for a in RETAIL_VA_RE.findall(ctx + "\n" + text)})
        offs = sorted({o.lower().lstrip("0") or "0" for o in OFFSET_RE.findall(ctx + "\n" + text)})
        names = [name] + sorted(set(RETAIL_NAME_RE.findall(ctx + "\n" + text)) - {name})
        rec = {"file": rel, "line": lineno, "decl": text, "name": name, "searches": []}
        c.lines.append(f"{rel}:{lineno}  {name}  <- {text[:110]}")
        # (command, search) -- each search answers the command's `rg` over the one in-memory tree.
        searches = []
        for a in addrs:
            searches.append((f'rg -n -i "(0x|FUN_|DAT_)?{a}" Source/',
                             lambda a=a: grep_address(src_all, a, lambda r: r.startswith("Source/"),
                                                      exclude)))
        if not addrs:
            c.lines.append("    (a) no retail address in the declaration's comment")
        for o in offs:
            searches.append((f'rg -n -i "0x0*{o}\\b" {shape_map}',
                             lambda o=o: grep(src_all, re.compile(rf"0x0*{o}(?![0-9a-f])", re.IGNORECASE),
                                              lambda r: r == shape_map, exclude, needle=o)))
        if not offs:
            c.lines.append("    (b) no +0x offset in the declaration's comment")
        for n in names:
            searches.append((f'rg -n -w "{n}" {SUBSTRATE.as_posix()} -g "*.inl"',
                             lambda n=n: grep(src_all, re.compile(rf"\b{re.escape(n)}\b"),
                                              lambda r: under(r, SUBSTRATE) and r.endswith(".inl"),
                                              exclude, needle=n)))
        for cmd, search in searches:
            found = search()
            # A hit on a line this same diff adds is the port itself (the definition citing its
            # own address), not a prior accessor; it is listed but does not raise the WARN.
            hits = [h for h in found if (h.rel, h.line) not in ours]
            mine = [h for h in found if (h.rel, h.line) in ours]
            rec["searches"].append({"command": cmd, "hits": [f"{h.rel}:{h.line}" for h in hits],
                                    "this_diff": [f"{h.rel}:{h.line}" for h in mine]})
            also = f"  (+{len(mine)} on lines this diff adds)" if mine else ""
            if hits:
                c.warn(f"    WARN {cmd}  -> {len(hits)} hit(s){also}")
                for h in hits[:limit]:
                    c.lines.append(f"        {h.rel}:{h.line}: {h.text[:140]}")
                if len(hits) > limit:
                    c.lines.append(f"        ... {len(hits) - limit} more")
            else:
                c.lines.append(f"    {cmd}  -> (empty){also}")
        c.data["declarations"].append(rec)
    return c


# ---------------------------------------------------------------------------------------------
# 4. Hot headers.


def is_hot(rel: str) -> bool:
    return rel in HOT_FILES or any(rel == p or rel.startswith(p.rstrip("/") + "/") for p in HOT_PREFIXES)


def check_hot(changed: list[str] | None, allow: list[str] | None) -> Check:
    c = Check("hot", "the diff does not touch the frozen hot headers / census generator")
    if changed is None:
        c.status = SKIP
        c.lines.append("no diff (git unavailable)")
        return c
    touched = [rel for rel in changed if is_hot(rel)]
    allow_all = allow is not None and len(allow) == 0
    allowed = set(allow or ())
    c.data = {"touched": touched}
    if not touched:
        c.lines.append(f"{len(changed)} changed file(s), none hot")
    for rel in touched:
        name = rel.rsplit("/", 1)[-1]
        if allow_all or rel in allowed or name in allowed:
            c.warn(f"ALLOWED {rel}")
        else:
            c.fail(f"TOUCHED {rel}")
    return c


# ---------------------------------------------------------------------------------------------
# 5. Residue.


def read_unported(path: Path) -> list[tuple[str, ...]]:
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith(("#", "class\t")):
            continue
        out.append(tuple(line.split("\t")))
    return out


RESIDUE_TITLE = ("fresh kernel_shape --unported: no family rule rows, nothing rose "
                 "over docs/vtmb/npc-kernel/unported.tsv")


@dataclasses.dataclass
class Residue:
    """The fresh unported table against the pin, made once and joined per family."""
    head: list[str]                          # lines every family's check opens with
    failure: list[str] | None = None         # the table could not be made: FAIL with these lines
    now: list[tuple[str, ...]] = dataclasses.field(default_factory=list)
    before: list[tuple[str, ...]] = dataclasses.field(default_factory=list)


def _share_kernel_builds() -> None:
    """Memoize the ledger/shape/census builds exactly as `research/tooling/kernel.py` does."""
    tooling = str(_HERE.parents[1])
    if tooling not in sys.path:
        sys.path.insert(0, tooling)
    import kernel
    if not all(hasattr(m.build, "cache_clear") for m in kernel.SHARED_BUILDS):
        kernel.share_builds()


def write_unported(path: Path) -> tuple[int, str, str]:
    """`kernel_shape --unported <path>` in this process, on the shared builds: (exit, out, err)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            _share_kernel_builds()
            import kernel_shape
            code = kernel_shape.main(["--unported", str(path)]) or 0
        except SystemExit as stop:
            code = stop.code if isinstance(stop.code, int) else (0 if stop.code is None else 1)
            if not isinstance(stop.code, (int, type(None))):
                err.write(f"{stop.code}\n")
        except Exception:                       # noqa: BLE001 -- a failed build is the check's FAIL
            code = 1
            err.write(traceback.format_exc())
    return code, out.getvalue(), err.getvalue()


def make_residue(repo: Path, fresh: Path | None) -> Residue:
    pin = repo / UNPORTED_PIN
    if not pin.is_file():
        return Residue([], [f"no committed pin {UNPORTED_PIN.as_posix()}"])
    head: list[str] = []
    tmp_dir = None
    if fresh is None:
        tmp_dir = tempfile.TemporaryDirectory()
        fresh = Path(tmp_dir.name) / "unported.tsv"
        code, stdout, stderr = write_unported(fresh)
        if code or not fresh.is_file():
            tmp_dir.cleanup()
            return Residue([], [f"kernel_shape --unported failed (exit {code})",
                                *("    " + ln for ln in (stdout + stderr).strip().splitlines()[-12:])])
        head.append(stdout.strip())
    try:
        now = read_unported(fresh)
        before = read_unported(pin)
    finally:
        if tmp_dir is not None:
            tmp_dir.cleanup()
    return Residue(head, None, now, before)


def check_residue(fam: Family, residue: Residue | None) -> Check:
    """`residue` is None under `--no-residue`."""
    c = Check("residue", RESIDUE_TITLE)
    if residue is None:
        c.status = SKIP
        c.lines.append("--no-residue")
        return c
    if residue.failure:
        c.fail(residue.failure[0])
        c.lines += residue.failure[1:]
        return c
    c.lines += residue.head
    now, before = residue.now, set(residue.before)
    rules = {f"0x{a}" for a in fam.rules}
    mine = [r for r in now if len(r) > 2 and r[2].lower() in rules]
    rose = [r for r in now if r not in before]
    now_set = set(now)
    fell = [r for r in dict.fromkeys(residue.before) if r not in now_set]     # in pin order
    c.data = {"fresh": len(now), "pinned": len(before), "family_rows": ["\t".join(r) for r in mine],
              "rose": ["\t".join(r) for r in rose], "fell": ["\t".join(r) for r in fell]}
    c.lines.append(f"fresh {len(now)} rows, pinned {len(before)}; fell {len(fell)}, rose {len(rose)}")
    for r in mine:
        c.fail("FAMILY ROW " + "\t".join(r))
    for r in rose:
        c.fail("ROSE " + "\t".join(r))
    for r in fell[:40]:
        c.lines.append("fell " + "\t".join(r))
    if len(fell) > 40:
        c.lines.append(f"... {len(fell) - 40} more fell")
    if fell and not mine and not rose:
        c.lines.append("(refresh the pin with kernel_shape --unported docs/vtmb/npc-kernel/unported.tsv)")
    return c


# ---------------------------------------------------------------------------------------------
# 6. Tests.


def check_tests(fam: Family, src: SourceIndex) -> Check:
    prefix = family_stem("ElysiumNpcKernel", fam.name) + "Tests"
    c = Check("tests", f"Tests/{prefix}*.cpp: registrations >= rule rows; "
                       "names carry 0x addresses or the tests cite every rule address")
    files = [rel for rel in src.files
             if under(rel, TESTS) and rel.rsplit("/", 1)[-1].startswith(prefix) and rel.endswith(".cpp")]
    if not files:
        c.fail(f"no test file {TESTS.as_posix()}/{prefix}*.cpp")
        c.data = {"files": [], "tests": 0, "rules": len(fam.rules)}
        return c
    tests = []
    for rel in files:
        for m in TEST_MACRO_RE.finditer(src.files[rel]):
            tests.append((rel, m.group(1), m.group(2)))
    fam_toks = set().union(*(src.tokens[rel] for rel in files))
    other = lambda rel: under(rel, TESTS) and rel not in files and not is_census(rel)
    uncited = [a for a in fam.rules if a not in fam_toks]
    elsewhere = {a: src.cited(a, other) for a in uncited}
    unresolved = [a for a in uncited if not elsewhere[a]]
    unnamed = [n for _, _, n in tests if not NAMED_RE.search(n.lower())]
    names_ok = bool(tests) and not unnamed
    # A rule whose tests live beside its sibling family (cited in another non-census test file) and
    # that no family test name carries counts toward the floor: the family file need not re-register it.
    names = " ".join(n.lower() for _, _, n in tests)
    beside = [a for a in fam.rules if a not in names and src.cited(a, other)]
    c.data = {"files": files, "tests": len(tests), "rules": len(fam.rules),
              "tested_beside": [f"0x{a}" for a in beside],
              "names_with_address": len(tests) - len(unnamed),
              "uncited_in_family_files": [f"0x{a}" for a in uncited],
              "uncited_anywhere": [f"0x{a}" for a in unresolved]}
    c.lines.append("files: " + ", ".join(f.rsplit("/", 1)[-1] for f in files))
    line = f"{len(tests)} test registrations for {len(fam.rules)} rule rows"
    if beside:
        line += f" (+{len(beside)} rule(s) tested in another test file)"
    if len(tests) + len(beside) < len(fam.rules):
        c.fail("TOO FEW " + line)
    else:
        c.lines.append(line)
    c.lines.append(f"{len(tests) - len(unnamed)}/{len(tests)} test names carry a 0x address")
    for a in uncited:
        if elsewhere[a]:
            c.lines.append(f"0x{a} cited only in another test file: {', '.join(elsewhere[a])}")
    if names_ok or not unresolved:
        c.lines.append("citation: " + ("every test name carries an address" if names_ok
                                       else f"the tests cite all {len(fam.rules)} rule addresses"))
    else:
        c.fail(f"citation: {len(unnamed)} test name(s) without an address AND "
               f"{len(unresolved)} rule address(es) no test cites:")
        c.lines.append("    uncited: " + " ".join(f"0x{a}" for a in unresolved))
    return c


# ---------------------------------------------------------------------------------------------
# 7. Port-only twins.


def _comment_block(lines: list[str], idx: int) -> str:
    """The citing line's comment: its trailing `//` part plus the contiguous comment lines around it."""
    parts = [lines[idx]]
    j = idx - 1
    while j >= 0 and _is_comment(lines[j]):
        parts.insert(0, lines[j])
        j -= 1
    j = idx + 1
    while j < len(lines) and _is_comment(lines[j]) and _is_comment(lines[idx]):
        parts.append(lines[j])
        j += 1
    if not _is_comment(lines[idx]):
        _, trailing = _strip_trailing_comment(lines[idx])
        parts[-1] = trailing
    return "\n".join(parts)


def check_twins(fam: Family, src: SourceIndex) -> Check:
    c = Check("twins", "no Substrate .cpp cites a rule address in a CHOSEN, NOT RECOVERED / "
                       "port-only / stand-in comment")
    # Every line of a Substrate .cpp (outside the census) that cites a rule address, from the index.
    sites: dict[tuple[str, int], set[str]] = {}
    for a in set(fam.rules):
        for rel, n in src.cites.get(a, ()):
            if under(rel, SUBSTRATE) and rel.endswith(".cpp") and not is_census(rel):
                sites.setdefault((rel, n), set()).add(a)
    found = []
    for rel, n in sorted(sites, key=lambda site: (src.order[site[0]], site[1])):
        lines = src.lines(rel)
        i, ln = n - 1, lines[n - 1]
        block = _comment_block(lines, i).lower()
        marks = [m for m in TWIN_MARKERS if m in block]
        if marks:
            for a in sorted(sites[(rel, n)]):
                found.append(f"{rel}:{i + 1}")
                c.warn(f"0x{a} {rel}:{i + 1} [{', '.join(marks)}]: {ln.strip()[:120]}")
    c.data = {"hits": found}
    if not found:
        c.lines.append("none")
    return c


# ---------------------------------------------------------------------------------------------
# Driver.


class Clock:
    """Seconds per stage and check, summed over families (`-v`)."""

    def __init__(self) -> None:
        self.times: dict[str, float] = {}

    @contextlib.contextmanager
    def __call__(self, label: str):
        start = time.perf_counter()
        try:
            yield
        finally:
            self.times[label] = self.times.get(label, 0.0) + time.perf_counter() - start

    def render(self, total: float) -> str:
        cells = "  ".join(f"{k} {v:.2f}" for k, v in self.times.items())
        return f"-- timings (s): {cells}  | total {total:.2f}"


@dataclasses.dataclass
class Shared:
    """What every family's gate reads from one run: the tree, the diff's two checks, the residue."""
    src: SourceIndex
    seams: Check
    hot: Check
    residue: Residue | None


def hot_allowance(args: argparse.Namespace) -> list[str] | None:
    """`--allow-hot` joined with the `--lane` preset ([] = every hot file allowed)."""
    if args.lane is None:
        return args.allow_hot
    preset = LANES[args.lane]
    if args.allow_hot is None:
        return list(preset)
    if not preset or not args.allow_hot:
        return []
    return [*preset, *args.allow_hot]


def prepare(args: argparse.Namespace, clock: Clock) -> Shared:
    repo = Path(args.repo).resolve()
    with clock("index"):
        src = SourceIndex(repo, Path("Source"))   # every check filters by path
    diff: Diff | None
    with clock("diff"):
        try:
            diff = collect_diff(repo, args.base)
            diff_note = f"diff base {diff.base[:12]} ({args.base or 'merge-base with main'}), " \
                        f"{len(diff.changed)} changed file(s)"
        except (subprocess.CalledProcessError, FileNotFoundError) as exc:
            diff = None
            diff_note = f"no diff: {exc}"
    with clock("seams"):
        seams = check_seams(diff, repo, src) if args.seams else skip_seams()
    seams.lines.insert(0, diff_note)
    with clock("hot"):
        hot = check_hot(diff.changed if diff else None, hot_allowance(args))
    with clock("residue"):
        residue = None if args.no_residue else make_residue(
            repo, Path(args.fresh_unported) if args.fresh_unported else None)
    return Shared(src, seams, hot, residue)


def gate_family(fam: Family, shared: Shared, args: argparse.Namespace, clock: Clock) -> list[Check]:
    src = shared.src
    with clock("addresses"):
        addresses = check_addresses(fam, src)
    with clock("arms"):
        arms = check_arms(fam, src, args.arms)
    with clock("residue-join"):
        residue = check_residue(fam, shared.residue)
    with clock("tests"):
        tests = check_tests(fam, src)
    with clock("twins"):
        twins = check_twins(fam, src)
    return [addresses, arms, shared.seams, shared.hot, residue, tests, twins]


def failed(checks: list[Check]) -> bool:
    return any(c.status == FAIL for c in checks)


def family_json(fam: Family, checks: list[Check]) -> dict:
    return {
        "family": fam.name, "rows": len(fam.rows), "rules": [f"0x{a}" for a in fam.rules],
        "pass": not failed(checks),
        "checks": [{"key": c.key, "title": c.title, "status": c.status, "lines": c.lines,
                    "data": c.data} for c in checks],
    }


def family_head(fam: Family) -> str:
    return f"kernel_gate {fam.name}: {len(fam.rows)} rows, {len(fam.rules)} rule (kernel_verdicts.tsv)"


def family_summary(checks: list[Check]) -> str:
    return f"== {'FAIL' if failed(checks) else 'PASS'}  " + "  ".join(f"{c.key}={c.status}" for c in checks)


def summary_table(results: list[tuple[Family, list[Check]]]) -> list[str]:
    """One row per family, one column per check, and the family's verdict."""
    keys = [c.key for c in results[0][1]]
    width = max(len("family"), *(len(fam.name) for fam, _ in results))
    cols = [max(len(k), 4) for k in keys]
    out = ["  ".join([f"{'family':<{width}}", *(f"{k:<{w}}" for k, w in zip(keys, cols)), "gate"])]
    for fam, checks in results:
        out.append("  ".join([f"{fam.name:<{width}}", *(f"{c.status:<{w}}" for c, w in zip(checks, cols)),
                              FAIL if failed(checks) else PASS]))
    return out


def _default_families() -> Path:
    from elysium_pipeline.paths import research_root
    return research_root() / "npc-kernel-checklist" / "families-19-29"


def family_names(args: argparse.Namespace) -> list[str]:
    if args.all:
        return list(ALL_FAMILIES)
    return list(dict.fromkeys(n.strip() for n in args.family.split(",") if n.strip()))


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    which = p.add_mutually_exclusive_group(required=True)
    which.add_argument("--family", help="family stem (Conditions19), or several comma-separated")
    which.add_argument("--all", action="store_true",
                       help="every story-8 family (ALL_FAMILIES) in one process; seams on unless --no-seams")
    p.add_argument("--base", help="git rev the branch is diffed against (merge-base; default main)")
    p.add_argument("--json", help="write the structured result here (several families: the summary table)")
    p.add_argument("--json-dir", help="write one <Family>.json per family here, in the --json shape")
    p.add_argument("--arms", type=parse_threshold, default=100.0, help="all | N%% per function (default all)")
    p.add_argument("--seams", action=argparse.BooleanOptionalAction, default=None,
                   help="run check 3, the seam searches (default: only under --all)")
    p.add_argument("--allow-hot", nargs="*", default=None, metavar="PATH",
                   help="downgrade hot-file touches to WARN: every one (no argument) or the named ones")
    p.add_argument("--lane", choices=sorted(LANES),
                   help="preset --allow-hot for the one lane that may edit the hot headers")
    p.add_argument("--no-residue", action="store_true", help="skip the kernel_shape --unported check")
    p.add_argument("-v", "--verbose", action="store_true", help="print the time each stage and check took")
    p.add_argument("--fresh-unported", help=argparse.SUPPRESS)   # a pre-made fresh table (tests)
    p.add_argument("--repo", default=str(DEFAULT_REPO), help=argparse.SUPPRESS)
    p.add_argument("--families", help="directory of family row files (default families-19-29)")
    p.add_argument("--skeletons", help="directory of skeleton JSON (default skeletons-19-29)")
    p.add_argument("--verdicts", help="the verdict overlay (default kernel_verdicts.tsv)")
    return p


def main(argv: list[str] | None = None) -> int:
    start = time.perf_counter()
    args = build_parser().parse_args(sys.argv[1:] if argv is None else argv)
    if args.seams is None:
        args.seams = args.all
    repo = Path(args.repo).resolve()
    families = Path(args.families) if args.families else _default_families()
    skeletons = Path(args.skeletons) if args.skeletons else families.parent / "skeletons-19-29"
    verdicts = Path(args.verdicts) if args.verdicts else repo / VERDICTS
    clock = Clock()
    with clock("families"):
        fams = [load_family(n, families, skeletons, verdicts) for n in family_names(args)]
    if not fams:
        raise SystemExit("--family names no family")
    shared = prepare(args, clock)
    results = [(fam, gate_family(fam, shared, args, clock)) for fam in fams]

    if len(results) == 1:
        fam, checks = results[0]
        print(family_head(fam))
        for c in checks:
            print()
            print(c.render())
        print()
        print(family_summary(checks))
    else:
        print(f"kernel_gate: {len(results)} families: {', '.join(f.name for f, _ in results)}")
        print("\n-- the diff's checks, computed once for every family")
        for c in (shared.seams, shared.hot):
            print()
            print(c.render())
        for fam, checks in results:
            print()
            print("-" * 100)
            print(family_head(fam))
            for c in checks:
                if c is not shared.seams and c is not shared.hot:
                    print()
                    print(c.render())
            print()
            print(family_summary(checks))
        print()
        print("=" * 100)
        for line in summary_table(results):
            print(line)
        bad = [fam.name for fam, checks in results if failed(checks)]
        print()
        print(f"== {'FAIL' if bad else 'PASS'}  {len(results) - len(bad)}/{len(results)} families pass"
              + (f"; failing: {', '.join(bad)}" if bad else ""))

    if args.json_dir:
        out_dir = Path(args.json_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        for fam, checks in results:
            (out_dir / f"{fam.name}.json").write_text(json.dumps(family_json(fam, checks), indent=1),
                                                      encoding="utf-8")
    if args.json:
        if len(results) == 1:
            payload = family_json(*results[0])
        else:
            payload = {"pass": not any(failed(cs) for _, cs in results),
                       "families": {f.name: {"pass": not failed(cs), "checks": {c.key: c.status for c in cs}}
                                    for f, cs in results}}
        Path(args.json).write_text(json.dumps(payload, indent=1), encoding="utf-8")
    if args.verbose:
        print(clock.render(time.perf_counter() - start))
    return 1 if any(failed(checks) for _, checks in results) else 0


if __name__ == "__main__":
    sys.exit(main())
