# -*- coding: utf-8 -*-
"""Name proposals from evidence the documentation harvest does not read.

`corpus harvest` pairs addresses with names the docs write beside them. These passes read three
things the image and the SDK state instead, and each proposal carries the argument that made it:

* **slot order** -- a vtable's slot order is the header's virtual order. Retail names that sit
  in a class's table (from the image's own strings, or the overlay) are paired with the slot the
  SDK declares under that name; where two consecutive pairs are the same distance apart on both
  sides, every slot between them names itself. A stretch whose lengths disagree changed between
  the SDK and the retail tree, and nothing inside it is named. Class boundaries (a class's table
  length on both sides) are pairs too.
* **translation-unit order** -- a `E:\\Vampire\\main\\dlls\\<file>.cpp` stamp pins a body to a
  source file, and a translation unit's bodies are emitted in definition order. Between two named
  neighbours of that unit, a run of unnamed bodies exactly as long as the SDK `.cpp`'s run of
  definitions between the same two names takes those names.
* **message prefix** -- a `DevMsg` / `Warning` string that opens with `Class::Method:` or
  `Method(` and is referenced by exactly one body, where the SDK declares that method on that
  class (or the body's own class chain).
* **slot identity** -- a slot is one virtual across the hierarchy. Where the named bodies at a
  slot (the image's, the overlay's, the passes above) state one method, every unnamed override
  there takes it: `CNPC_VDog::StartTask` because eleven other bodies at slot 442 are `StartTask`.
* **CRT bytes** (`crt_match.py`) -- a body whose bytes are a symbol's in the VC6 SP5 archive the
  game links, relocation sites masked. The archive's statement, so tier `binary`.
* **accessor** -- a leaf body that reads or writes one word of the recovered layout
  (`docs/vtmb/npc-kernel/layout.tsv`) and nothing else. The member's name is the record; the
  method name is convention (`Get<Member>`, `Set<Member>`, `Is<Member>` for a zero test), so the
  row carries its own tier, `accessor`, unless an SDK header declares that very inline body under
  a name, in which case the name is the SDK's and the tier `inferred`.

The SDK passes are held to the image's arities: a `__thiscall` body's `RET n` is how many stack
words it pops, and a declaration states how many it should. A proposal the word count does not
confirm is not made. The SDK is 2013's, not the 2003 tree VtMB was built on, so a name can be
positionally right and a later rename (`SelectFailSchedule` for 2003's spelling); the evidence
column says which tree answered.

Nothing here writes the overlay. The rows go to the proposal file beside the docs harvest, to be
read as questions. Identity only: no body is read beyond its strings and its `RET` operands.
"""

from __future__ import annotations

import bisect
import collections
import re
import sqlite3
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import sdk_layout

PLACEHOLDER = re.compile(r"^(thunk_)?(FUN_[0-9a-fA-F]+|vfunc\d+)$")
STAMP = re.compile(r"\\dlls\\(?:[\w]+\\)*([\w]+\.cpp)$", re.I)
MESSAGE = re.compile(r"^\s*(?:([A-Z]\w*)\s*::\s*)?(~?[A-Za-z_]\w*)\s*(?:\(\s*\)|\s?:(?!:))")
# A run of unnamed bodies past the last evidence of a translation unit before its region stops.
TU_SLACK = 12


def is_placeholder(name: str) -> bool:
    return bool(PLACEHOLDER.match(name or ""))


@dataclass
class Proposal:
    module: str
    addr: str
    name: str
    tier: str
    evidence: str
    source: str            # which pass: slots / tu / message


# -- RTTI ------------------------------------------------------------------------------------------

def primary_chains(binary: Path) -> dict[str, list[str]]:
    """Every RTTI class's primary-base chain, most-derived first, from the image's hierarchy
    descriptors. The base-class array is a pre-order walk, so a class's primary base is the entry
    after it whenever it contains any base at all."""
    repo = Path(__file__).resolve().parents[4]
    if str(repo) not in sys.path:
        sys.path.insert(0, str(repo))
    from research.tooling.probes.weapon_activity_survey import (  # noqa: WPS433
        PEImage, TYPE_DESCRIPTOR_RE, _find_all, _undecorate_type)
    image = PEImage(binary.read_bytes())
    rdata_offset, rdata = image.section_bytes(".rdata")
    chains: dict[str, list[str]] = {}
    for match in TYPE_DESCRIPTOR_RE.finditer(image.data):
        name = _undecorate_type(match.group(0)[:-1].decode("ascii"))
        if name in chains:
            continue
        type_va = image.offset_to_va(match.start() - 8)
        if type_va is None:
            continue
        for ref in _find_all(rdata, struct.pack("<I", type_va)):
            if ref < 12:
                continue
            locator = rdata_offset + ref - 12
            signature, offset, cd = struct.unpack_from("<III", image.data, locator)
            if signature or offset or cd >= 0x1000:
                continue
            hierarchy = image.read_u32_va(image.offset_to_va(locator) + 16)
            count = image.read_u32_va(hierarchy + 8) if hierarchy else None
            array = image.read_u32_va(hierarchy + 12) if hierarchy else None
            if not count or not array or count > 256:
                continue
            entries = []
            for index in range(count):
                descriptor = image.read_u32_va(array + index * 4)
                tva = image.read_u32_va(descriptor) if descriptor else None
                contained = image.read_u32_va(descriptor + 4) if descriptor else None
                label = image.read_cstring_va(tva + 8) if tva else ""
                if not label or contained is None:
                    entries = []
                    break
                entries.append((_undecorate_type(label), contained))
            if not entries:
                continue
            chain = [entries[0][0]]
            i = 0
            while entries[i][1] > 0 and i + 1 < len(entries):
                i += 1
                chain.append(entries[i][0])
            chains[name] = chain
            break
    return chains


# -- alignment -------------------------------------------------------------------------------------

def consistent(pairs: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """The longest chain of pairs increasing on both sides (patience LIS)."""
    pairs = sorted(set(pairs))
    tails: list[int] = []
    tail_at: list[int] = []
    parent = [-1] * len(pairs)
    for k, (_, right) in enumerate(pairs):
        # strictly increasing on the left too: equal lefts are sorted by right, so bisect_left on
        # right with a same-left guard is enough once duplicates on the left are removed
        pos = bisect.bisect_left(tails, right)
        if pos > 0 and pairs[tail_at[pos - 1]][0] == pairs[k][0]:
            continue
        if pos == len(tails):
            tails.append(right)
            tail_at.append(k)
        else:
            tails[pos] = right
            tail_at[pos] = k
        parent[k] = tail_at[pos - 1] if pos > 0 else -1
    out = []
    k = tail_at[-1] if tail_at else -1
    while k >= 0:
        out.append(pairs[k])
        k = parent[k]
    return out[::-1]


def fills(anchors: list[tuple[int, int]]) -> tuple[dict[int, tuple[int, tuple, tuple]],
                                                    list[tuple[tuple, tuple]]]:
    """Left position → (right position, left anchor, right anchor) wherever consecutive anchors
    bracket equal-length runs; and the stretches whose lengths disagree."""
    named: dict[int, tuple[int, tuple, tuple]] = {}
    broken = []
    for a, b in zip(anchors, anchors[1:]):
        if b[0] - a[0] == b[1] - a[1]:
            for k in range(1, b[0] - a[0]):
                named[a[0] + k] = (a[1] + k, a, b)
        elif b[0] - a[0] > 1:
            broken.append((a, b))
    return named, broken


def screen(anchors: list[tuple[int, int]], named: dict, broken: list, arity) -> None:
    """Hold each equal run to the image's arities. A position whose body pops a different word
    count than the declaration (or already carries a different retail name) is a substitution
    and stays unnamed; a run with at least as many substitutions as confirmed positions is not
    a run. `arity(k, i)` answers (retail words, declared words), None where unread."""
    for a, b in zip(anchors, anchors[1:]):
        run = [k for k in range(a[0] + 1, b[0]) if k in named]
        if not run:
            continue
        verdicts = [arity(k, named[k][0]) for k in run]
        wrong = [k for k, (x, y) in zip(run, verdicts) if x is not None and y is not None and x != y]
        right = [k for k, (x, y) in zip(run, verdicts) if x is not None and x == y]
        if not wrong:
            continue
        if len(wrong) >= len(right):
            for k in run:
                named.pop(k)
            broken.append((a, b))
        else:
            for k in wrong:
                named.pop(k)


# -- the corpus, read once -------------------------------------------------------------------------

RET = re.compile(r"\bRET(?:N)?(?:\s+0x([0-9a-fA-F]+)|\s+(\d+))?\s*$", re.M)


class Corpus:
    def __init__(self, connection: sqlite3.Connection, module: str,
                 listing: sqlite3.Connection | None = None):
        self.module = module
        self.db = connection
        self.listing = listing
        self._words: dict[str, int | None] = {}
        self.name: dict[str, str] = {}
        self.ns: dict[str, str] = {}
        self.size: dict[str, int] = {}
        self.cc: dict[str, str] = {}
        self.thunk: set[str] = set()
        for row in connection.execute(
                "SELECT addr, name, ns, size, thunk, cc FROM functions WHERE module = ?", (module,)):
            self.name[row[0]] = row[1] or ""
            self.ns[row[0]] = row[2] or ""
            self.size[row[0]] = row[3] or 0
            self.cc[row[0]] = row[5] or ""
            if row[4]:
                self.thunk.add(row[0])
        self.tables: dict[str, list[str]] = collections.defaultdict(list)
        for cls, slot, func in connection.execute(
                "SELECT cls, slot, func FROM vtables WHERE module = ? AND sub = 0 ORDER BY cls, slot",
                (module,)):
            table = self.tables[cls]
            while len(table) < slot:
                table.append("")
            table.append(func)
        # every (class, slot) a body fills, module-wide: how folding is recognised
        self.fills: dict[str, set[tuple[str, int]]] = collections.defaultdict(set)
        for cls, table in self.tables.items():
            for slot, func in enumerate(table):
                if func:
                    self.fills[func].add((cls, slot))

    def words(self, addr: str) -> int | None:
        """Stack words the body pops on return, from its `RET n` instructions -- the image's own
        statement of a `__thiscall` virtual's arity. None when the body never returns through a
        `RET` (a tail jump) or its returns disagree."""
        if addr in self._words:
            return self._words[addr]
        found: set[int] = set()
        if self.listing is not None:
            row = self.listing.execute("SELECT asm FROM listing WHERE module = ? AND addr = ?",
                                       (self.module, addr)).fetchone()
            for m in RET.finditer(row[0] if row else ""):
                value = int(m.group(1), 16) if m.group(1) else int(m.group(2) or 0)
                found.add(value)
        words = next(iter(found)) // 4 if len(found) == 1 and next(iter(found)) % 4 == 0 else None
        self._words[addr] = words
        return words

    def label(self, addr: str) -> str:
        ns = self.ns.get(addr, "")
        name = self.name.get(addr, "")
        return name if ns in ("", "Global") else f"{ns}::{name}"

    def code(self, addr: str) -> str:
        row = self.db.execute("SELECT code FROM functions WHERE module = ? AND addr = ?",
                              (self.module, addr)).fetchone()
        return (row[0] if row else "") or ""


# -- slot order ------------------------------------------------------------------------------------

def slot_pass(corpus: Corpus, sdk: sdk_layout.Sdk, chains: dict[str, list[str]],
              focus: str = "CAI_BaseNPC") -> tuple[list[Proposal], dict]:
    """Name slot bodies from the SDK's virtual order. Returns the proposals and a report: the
    leave-one-out verification against the names already in the tables, and, for `focus`, why
    each slot still unnamed stayed so."""
    header_of = {cls: sdk.header(cls) for cls in chains}
    # Classes whose own table is present and whose chain the SDK declares at least in part.
    layouts: dict[tuple[str, ...], tuple[list[sdk_layout.Slot], dict[str, int]]] = {}

    def layout(chain_root_first: tuple[str, ...]):
        if chain_root_first not in layouts:
            known = tuple(c for c in chain_root_first if header_of.get(c) or sdk.header(c))
            layouts[chain_root_first] = sdk.vtable(known)
        return layouts[chain_root_first]

    descendants: dict[str, list[str]] = collections.defaultdict(list)
    for cls, chain in chains.items():
        if cls in corpus.tables:
            for ancestor in chain:
                descendants[ancestor].append(cls)

    proposals: dict[str, Proposal] = {}
    disputed: dict[str, set[str]] = collections.defaultdict(set)
    verify = collections.Counter()
    verify_misses: list[str] = []
    verified: set[tuple[str, int]] = set()
    focus_report: dict[int, str] = {}

    # The deepest classes carry the longest tables; each class is aligned once with the names its
    # own table and every descendant's table hold at its slots.
    for cls in sorted(corpus.tables, key=lambda c: (-len(chains.get(c, [])), c)):
        chain = chains.get(cls)
        if not chain or sdk.header(cls) is None:
            continue
        root_first = tuple(reversed(chain))
        slots, ends = layout(root_first)
        table = corpus.tables[cls]
        if not slots or not table:
            continue
        sdk_index: dict[str, list[int]] = collections.defaultdict(list)
        for i, slot in enumerate(slots):
            sdk_index[slot.name].append(i)

        # Class boundaries: after each ancestor's last slot on both sides.
        boundaries = []
        for ancestor in root_first:
            if ancestor in ends and ancestor in corpus.tables and sdk.header(ancestor):
                boundaries.append((len(corpus.tables[ancestor]), ends[ancestor]))
        boundaries.append((0, 0))   # the destructor opens every table on both sides

        def named_pairs(exclude: int | None = None) -> list[tuple[int, int]]:
            per_slot: dict[int, set[int]] = collections.defaultdict(set)
            for holder in descendants.get(cls, [cls]):
                other = corpus.tables.get(holder, [])
                for s in range(1, min(len(table), len(other))):
                    if s == exclude:
                        continue
                    func = other[s]
                    name = corpus.name.get(func, "")
                    if not func or is_placeholder(name):
                        continue
                    hits = sdk_index.get(name, [])
                    if len(hits) == 1:
                        per_slot[s].add(hits[0])
            return [(s, next(iter(i))) for s, i in per_slot.items() if len(i) == 1]

        sdk_words = [sdk.stack_words(slot.key[1], slot.ret) for slot in slots]
        holders = descendants.get(cls, [cls])

        def retail_words(s: int) -> int | None:
            for holder in [cls] + holders[:12]:
                other = corpus.tables.get(holder, [])
                if s < len(other) and other[s]:
                    words = corpus.words(other[s])
                    if words is not None:
                        return words
            return None

        def stated(s: int) -> set[str]:
            names = set()
            for holder in [cls] + holders:
                other = corpus.tables.get(holder, [])
                if s < len(other) and other[s] and not is_placeholder(corpus.name.get(other[s], "")):
                    names.add(corpus.name[other[s]])
            return {n for n in names if not n.startswith("~")}

        def agrees(s: int, i: int) -> bool:
            if s == 0 or s >= len(table) or i >= len(slots):
                return True
            left, right = retail_words(s), sdk_words[i]
            return left is None or right is None or left == right

        def align(exclude: int | None = None):
            pairs = [p for p in named_pairs(exclude) if agrees(*p)]
            anchors = consistent(pairs + boundaries)
            named, broken = fills(anchors)
            def arity(s: int, i: int) -> tuple[int | None, int | None]:
                if i < len(slots) and stated(s) and slots[i].name not in stated(s):
                    return (-1, -2)   # the image already names this slot something else
                return (retail_words(s) if 0 < s < len(table) else None,
                        sdk_words[i] if i < len(slots) else None)

            screen(anchors, named, broken, arity)
            return anchors, named, broken

        anchors, named, broken = align()

        # Verification: drop each named anchor in turn and ask whether the rest put it back.
        for s, i in sorted(a for a in anchors if a not in boundaries):
            body = table[s] if s < len(table) else ""
            if not body or is_placeholder(corpus.name.get(body, "")) or (body, s) in verified:
                continue
            verified.add((body, s))
            _, back, _ = align(exclude=s)
            if s not in back:
                verify["unreachable"] += 1
            elif back[s][0] == i:
                verify["agree"] += 1
            else:
                verify["disagree"] += 1
                verify_misses.append(f"{cls}#{s} {corpus.label(body)} -> "
                                     f"{slots[back[s][0]].introduced}::{slots[back[s][0]].name}")

        for s in range(len(table)):
            func = table[s]
            if not func:
                continue
            current = corpus.name.get(func, "")
            if s not in named:
                if cls == focus and is_placeholder(current):
                    focus_report[s] = _why_not(s, anchors, broken, len(slots))
                continue
            i, left, right = named[s]
            sdk_slot = slots[i]
            owner = _owner(corpus, func, s, chains)
            if owner is None:
                if cls == focus and is_placeholder(current):
                    folded = sorted(f"{c}#{t}" for c, t in corpus.fills[func] if t != s)[:3]
                    focus_report[s] = ("folded: the same body also fills " + ", ".join(folded)
                                       + f" -- one body cannot carry {sdk_slot.name}")
                continue
            if not is_placeholder(current):
                continue
            # A position is only as good as the arity that confirms it: an SDK virtual added after
            # 2003 can sit in an equal run by coincidence, and only the word count tells.
            if sdk_words[i] is None or retail_words(s) is None:
                if cls == focus:
                    focus_report[s] = (f"no SDK twin confirmed: #{s} lines up with "
                                       f"{sdk_slot.introduced}::{sdk_slot.name} but its arity is "
                                       "unread on one side")
                continue
            name = f"{owner}::{sdk_slot.name}"
            left_label = _anchor_label(corpus, [cls] + holders, left, boundaries)
            right_label = _anchor_label(corpus, [cls] + holders, right, boundaries)
            header = sdk.header(sdk_slot.introduced)
            evidence = (f"slot order: {cls}#{s} is SDK {sdk_slot.introduced}::{sdk_slot.name} "
                        f"({header.name if header else '?'} virtual #{i}); bracketed by "
                        f"{left_label} and {right_label}, equal run; "
                        + (f"pops {sdk_words[i]} word(s) as declared"
                           if sdk_words[i] is not None and retail_words(s) is not None
                           else "arity unread"))
            previous = proposals.get(func)
            if previous is not None and previous.name != name:
                disputed[func].update({previous.name, name})
                continue
            proposals.setdefault(func, Proposal(corpus.module, func, name, "inferred", evidence,
                                                "slots"))
        if cls == focus:
            for s in range(len(table)):
                func = table[s]
                if func in proposals and s in focus_report:
                    focus_report.pop(s)

    for func in disputed:
        proposals.pop(func, None)
    report = {"verify": verify, "verify_misses": verify_misses, "focus": focus_report,
              "disputed": {f: sorted(n) for f, n in disputed.items()}}
    return list(proposals.values()), report


def _owner(corpus: Corpus, func: str, slot: int, chains: dict[str, list[str]]) -> str | None:
    """The class a slot body belongs to: the most-base class holding it at this slot, provided
    every other holder descends from it and the body fills no other slot anywhere."""
    holders = [c for c, s in corpus.fills[func] if s == slot]
    if any(s != slot for _, s in corpus.fills[func]) or not holders:
        return None
    root = min(holders, key=lambda c: (len(chains.get(c, [])), c))
    for holder in holders:
        if root not in chains.get(holder, [holder]):
            return None
    return root


def _anchor_label(corpus: Corpus, holders: list[str], anchor: tuple[int, int],
                  boundaries: list[tuple[int, int]]) -> str:
    s, i = anchor
    for holder in holders:
        table = corpus.tables.get(holder, [])
        if s < len(table) and table[s] and not is_placeholder(corpus.name.get(table[s], "")):
            return f"#{s} {corpus.name[table[s]]}"
    if anchor == (0, 0):
        return "#0 the table's start"
    if anchor in boundaries:
        return f"#{s} the class boundary (SDK #{i})"
    return f"#{s} (SDK #{i})"


def _why_not(slot: int, anchors: list[tuple[int, int]], broken: list, sdk_len: int) -> str:
    for a, b in broken:
        if a[0] < slot < b[0]:
            return (f"no SDK twin: stretch #{a[0]}-#{b[0]} holds {b[0] - a[0] - 1} retail slots "
                    f"against {max(b[1] - a[1] - 1, 0)} SDK 2013 virtuals")
    if anchors and slot > anchors[-1][0]:
        return f"no SDK twin: past the last aligned slot #{anchors[-1][0]} (SDK has {sdk_len})"
    return "no SDK twin: no bracketing names"


# -- slot identity ---------------------------------------------------------------------------------

def identity_pass(corpus: Corpus, sdk: sdk_layout.Sdk | None, chains: dict[str, list[str]],
                  extra: dict[str, str | tuple[str, str]]) -> tuple[list[Proposal], dict]:
    """A slot is one virtual across the hierarchy: every body at slot `s` of every class that
    inherits the slot from the same introducer overrides the same method. Where the named bodies
    at a slot (the dump's, the overlay's, and `extra` -- the other passes' proposals, bare method
    names or `(name, tier)`) state one method and nothing else, every unnamed body there takes
    it. A method every witness of which is a coined `accessor` proposal propagates as `accessor`:
    the slot argument is sound, the spelling is still convention.

    Refused: a slot whose named bodies disagree; a slot whose bodies pop different stack word
    counts (one of the names, or the table, is not what it seems); a slot whose one name the SDK
    declares with a different arity; a body folded into another slot."""
    extra_name = {a: (v[0] if isinstance(v, tuple) else v) for a, v in extra.items()}
    extra_tier = {a: (v[1] if isinstance(v, tuple) else "inferred") for a, v in extra.items()}
    def introducer(cls: str, s: int) -> str:
        for ancestor in reversed(chains.get(cls, [cls])):
            if ancestor in corpus.tables and len(corpus.tables[ancestor]) > s:
                return ancestor
        return cls

    groups: dict[tuple[str, int], dict[str, str]] = collections.defaultdict(dict)
    for cls, table in corpus.tables.items():
        if cls not in chains:
            continue
        for s, func in enumerate(table):
            if func:
                groups[(introducer(cls, s), s)][cls] = func

    layouts: dict[str, tuple[dict[str, list[int]], list[int | None]]] = {}

    def sdk_words(intro: str, method: str) -> int | None:
        if sdk is None:
            return None
        if intro not in layouts:
            chain = tuple(c for c in reversed(chains.get(intro, [intro])) if sdk.header(c))
            slots, _ = sdk.vtable(chain) if chain else ([], {})
            index: dict[str, list[int]] = collections.defaultdict(list)
            for i, slot in enumerate(slots):
                index[slot.name].append(i)
            layouts[intro] = (index, [sdk.stack_words(x.key[1], x.ret) for x in slots])
        index, words = layouts[intro]
        hits = index.get(method, [])
        return words[hits[0]] if len(hits) == 1 else None

    proposals: dict[str, Proposal] = {}
    offered: dict[str, set[str]] = collections.defaultdict(set)
    report = collections.Counter()
    disagreements: list[str] = []
    reasons: dict[str, str] = {}
    for (intro, s), members in sorted(groups.items()):
        bodies = sorted(set(members.values()))
        named: dict[str, str] = {}
        for body in bodies:
            current = corpus.name.get(body, "")
            if not is_placeholder(current):
                named[body] = current
            elif body in extra_name:
                named[body] = extra_name[body]
        unnamed = [b for b in bodies if b not in named]
        coined = bool(named) and all(extra_tier.get(b) == "accessor" for b in named)
        # A destructor's name is its class's; it says nothing about another class's body.
        methods = {m for m in named.values() if not m.startswith("~")}
        if not unnamed:
            continue

        def refuse(why: str) -> None:
            for body in unnamed:
                reasons.setdefault(body, f"{intro}#{s}: {why}")

        if not methods:
            refuse("destructor slot" if named else
                   f"no body at this slot carries a name in any of {len(members)} classes")
            continue
        if len(methods) > 1:
            report["disagree"] += 1
            disagreements.append(f"{intro}#{s}: " + ", ".join(sorted(methods)))
            refuse("the named bodies disagree: " + ", ".join(sorted(methods)))
            continue
        method = next(iter(methods))
        words = {corpus.words(b) for b in bodies} - {None}
        if len(words) > 1:
            report["arity"] += 1
            refuse(f"{method} named, but the bodies pop {sorted(words)} words")
            continue
        declared = sdk_words(intro, method)
        if words and declared is not None and declared not in words:
            report["sdk arity"] += 1
            refuse(f"{method} named, but the bodies pop {next(iter(words))} words and the SDK "
                   f"declares {declared}")
            continue
        witnesses = sorted(corpus.label(b) if b not in extra_name else f"{extra_name[b]} (proposed)"
                           for b in named)
        for body in unnamed:
            owner = _owner(corpus, body, s, chains)
            if owner is None:
                report["folded"] += 1
                folded = sorted(f"{c}#{t}" for c, t in corpus.fills[body] if t != s)[:2]
                reasons.setdefault(body, f"{intro}#{s}: {method}, but the body is folded "
                                         f"(also fills {', '.join(folded) or 'unrelated classes'})")
                continue
            name = f"{owner}::{method}"
            offered[body].add(name)
            proposals.setdefault(body, Proposal(
                corpus.module, body, name, "accessor" if coined else "inferred",
                f"slot identity: {intro}#{s} is {method} in {len(named)} named body(ies) "
                f"({', '.join(witnesses[:3])}{', …' if len(witnesses) > 3 else ''}); "
                + (f"all {len(bodies)} bodies pop {next(iter(words))} word(s)" if words
                   else "arity unread"), "identity"))
    for body, names in offered.items():
        if len(names) > 1:
            proposals.pop(body, None)
            report["two names"] += 1
            reasons[body] = "offered two names by two slots: " + ", ".join(sorted(names))
    report_out = dict(report)
    report_out["reasons"] = {b: r for b, r in reasons.items() if b not in proposals}
    report_out["proposed"] = len(proposals)
    report_out["disagreements"] = disagreements
    return list(proposals.values()), report_out


# -- translation-unit order ------------------------------------------------------------------------

def tu_pass(corpus: Corpus, sdk: sdk_layout.Sdk, taken: set[str]) -> tuple[list[Proposal], dict]:
    stamped: dict[str, set[str]] = collections.defaultdict(set)
    for func, text in corpus.db.execute(
            "SELECT r.func_addr, s.text FROM string_refs r JOIN strings s "
            "ON s.module = r.module AND s.addr = r.str_addr WHERE r.module = ?", (corpus.module,)):
        m = STAMP.search(text or "")
        if m:
            stamped[m.group(1).lower()].add(func)
    order = sorted((a for a in corpus.name if a not in corpus.thunk), key=lambda a: int(a, 16))
    position = {a: k for k, a in enumerate(order)}
    tu_of: dict[str, str] = {}
    for tu, funcs in stamped.items():
        for f in funcs:
            tu_of.setdefault(f, tu)

    proposals: list[Proposal] = []
    report = {"units": {}}
    for tu, funcs in sorted(stamped.items()):
        source = sdk.find_source(tu)
        if source is None:
            report["units"][tu] = "no SDK source"
            continue
        definitions = sdk.definitions(source)
        qualified = collections.Counter(f"{c}::{m}" for c, m, _, _ in definitions)
        index = {f"{c}::{m}": k for k, (c, m, _, _) in enumerate(definitions)
                 if qualified[f"{c}::{m}"] == 1}
        # The region: out from the stamped bodies while the neighbours are this unit's names or
        # unnamed, stopping at another unit's stamp, a name this source does not define, or
        # TU_SLACK unnamed bodies past the last evidence.
        seeds = sorted(position[f] for f in funcs if f in position)
        if not seeds:
            continue
        lo, hi = seeds[0], seeds[-1]
        for direction in (-1, 1):
            k = lo if direction < 0 else hi
            slack = 0
            while 0 <= k + direction < len(order) and slack < TU_SLACK:
                k += direction
                addr = order[k]
                if tu_of.get(addr, tu) != tu:
                    break
                label = corpus.label(addr)
                if label in index or addr in funcs:
                    slack = 0
                    if direction < 0:
                        lo = k
                    else:
                        hi = k
                elif is_placeholder(corpus.name.get(addr, "")):
                    slack += 1
                else:
                    break
        # inside the region, a body named for another source ends the stretch it sits in
        region = order[lo:hi + 1]
        def agrees(k: int, d: int) -> bool:
            retail, declared = corpus.words(region[k]), definitions[d][3]
            return retail is None or declared is None or retail == declared

        pairs = [(k, index[corpus.label(a)]) for k, a in enumerate(region)
                 if corpus.label(a) in index and agrees(k, index[corpus.label(a)])]
        anchors = consistent(pairs)
        named, broken = fills(anchors)
        def arity(k: int, d: int) -> tuple[int | None, int | None]:
            addr = region[k]
            if not is_placeholder(corpus.name.get(addr, "")) \
                    and corpus.label(addr) != f"{definitions[d][0]}::{definitions[d][1]}":
                return (-1, -2)   # already named for another definition: a substitution
            return (corpus.words(addr), definitions[d][3])

        screen(anchors, named, broken, arity)
        count = 0
        for k, (d, left, right) in sorted(named.items()):
            addr = region[k]
            if not is_placeholder(corpus.name.get(addr, "")) or addr in taken:
                continue
            cls, method, line, words = definitions[d]
            if method.startswith("~") or words is None or corpus.words(addr) is None:
                continue
            evidence = (f"unit order: {tu} (stamp in {len(funcs)} bodies); SDK 2013 "
                        f"{source.name}:{line} between {corpus.label(region[left[0]])} and "
                        f"{corpus.label(region[right[0]])}, equal run; "
                        + (f"pops {words} word(s) as defined"
                           if words is not None and corpus.words(addr) is not None
                           else "arity unread"))
            proposals.append(Proposal(corpus.module, addr, f"{cls}::{method}", "inferred",
                                      evidence, "tu"))
            count += 1
        report["units"][tu] = (f"{len(region)} bodies, {len(anchors)} aligned names, "
                               f"{count} proposed, {len(broken)} unequal stretches")
    return proposals, report


# -- message prefixes ------------------------------------------------------------------------------

def message_pass(corpus: Corpus, sdk: sdk_layout.Sdk, chains: dict[str, list[str]],
                 taken: set[str]) -> tuple[list[Proposal], dict]:
    referrers: dict[str, set[str]] = collections.defaultdict(set)
    texts: dict[str, str] = {}
    for func, str_addr, text in corpus.db.execute(
            "SELECT r.func_addr, s.addr, s.text FROM string_refs r JOIN strings s "
            "ON s.module = r.module AND s.addr = r.str_addr WHERE r.module = ?", (corpus.module,)):
        referrers[str_addr].add(func)
        texts[str_addr] = text or ""
    declared: dict[str, set[str]] = {}

    def methods(cls: str) -> set[str]:
        if cls not in declared:
            declared[cls] = {m.name for m in sdk.methods(cls)[1]}
        return declared[cls]

    wanted: dict[str, set[tuple[str, str]]] = collections.defaultdict(set)
    for str_addr, text in texts.items():
        funcs = referrers[str_addr]
        if len(funcs) != 1:
            continue
        func = next(iter(funcs))
        if func in taken or not is_placeholder(corpus.name.get(func, "")):
            continue
        m = MESSAGE.match(text)
        if not m:
            continue
        cls, method = m.group(1), m.group(2)
        if cls:
            if sdk.header(cls) is None or method not in methods(cls):
                continue
            wanted[func].add((f"{cls}::{method}", text))
            continue
        owner = corpus.ns.get(func, "")
        chain = chains.get(owner)
        if not chain:
            continue
        if any(sdk.header(c) and method in methods(c) for c in chain):
            wanted[func].add((f"{owner}::{method}", text))
    proposals = []
    by_name = collections.Counter(next(iter({n for n, _ in v})) for v in wanted.values()
                                  if len({n for n, _ in v}) == 1)
    dropped = 0
    for func, found in sorted(wanted.items()):
        names = {n for n, _ in found}
        if len(names) != 1 or by_name[next(iter(names))] != 1:
            dropped += 1
            continue
        name = next(iter(names))
        owner, method = name.split("::")
        # The declaration's arity, when the chain declares the method exactly once.
        declarations = [m for c in chains.get(owner, [owner]) if sdk.header(c)
                        for m in sdk.methods(c)[1] if m.name == method]
        words = {sdk.stack_words(m.params, m.ret) for m in declarations}
        retail = corpus.words(func)
        arity = ""
        if len(words) == 1 and None not in words and retail is not None:
            if retail not in words:
                dropped += 1
                continue
            arity = f"; pops {retail} word(s) as declared"
        quoted = "; ".join(sorted(repr(t[:60]) for _, t in found)[:2])
        proposals.append(Proposal(corpus.module, func, name, "inferred",
                                  f"message prefix: sole referrer of {quoted}; SDK 2013 declares "
                                  f"{method} on {owner}'s chain{arity}", "message"))
    return proposals, {"proposed": len(proposals), "ambiguous": dropped}


# -- accessors -------------------------------------------------------------------------------------

LAYOUT_TSV = Path(__file__).resolve().parents[4] / "docs" / "vtmb" / "npc-kernel" / "layout.tsv"
ACCESSOR_MAX_BYTES = 48
# The table the whole base line is flattened into; a species table sits beside it.
BASE_TABLE = "CAI_BaseNPCTroika"
HUNGARIAN = re.compile(r"^m_(?:fl|f|b|i|n|h|p|v|vec|ang|sz|str|e|ul|us|uc|ch|a|q|c|s|t|d|w)?(?=[A-Z])")
# An SDK header's one-line accessor: `Foo() const { return m_x; }` / `SetFoo(int x) { m_x = x; }`.
SDK_GETTER = re.compile(r"\b(\w+)\s*\(\s*(?:void)?\s*\)\s*(?:const)?\s*\{\s*return\s+"
                        r"(?:\([^;{}]*?\)\s*)?&?\s*(?:this->)?(m_\w+)(?:\.Get\(\))?\s*;\s*\}")
SDK_SETTER = re.compile(r"\b(\w+)\s*\(\s*(?:const\s+)?[\w:<>]+\s*[&*]?\s*(\w+)\s*\)\s*\{\s*"
                        r"(?:this->)?(m_\w+)(?:\s*=\s*\2|\.Set\(\s*\2\s*\))\s*;\s*\}")
_LOCAL_DECL = re.compile(r"^(?:[\w:]+\s+)+\**\w+(?:\s*\[\d+\])?;$")
_CAST = re.compile(r"\((?:[\w ]+?\s*\**)\)")
_SIGNATURE = re.compile(r"^[^\n]*?\b(?:FUN_|vfunc|\w+::|\w+)[\w:~]*\s*\(([^)]*)\)\s*$", re.M)


@dataclass
class Word:
    table: str
    off: int
    end: int
    member: str
    type: str
    layer: str
    tier: str


def read_layout(path: Path = LAYOUT_TSV) -> dict[str, list[Word]]:
    """`layout.tsv` as one sorted word list per table; a word runs to the next word's offset."""
    tables: dict[str, list[Word]] = collections.defaultdict(list)
    if not path.is_file():
        return {}
    lines = path.read_text(encoding="utf-8").splitlines()
    header = lines[0].split("\t")
    col = {name: k for k, name in enumerate(header)}
    for line in lines[1:]:
        if not line.strip() or line.startswith("#"):
            continue
        cells = line.split("\t")
        get = lambda name: cells[col[name]].strip() if col.get(name, 99) < len(cells) else ""  # noqa: E731
        tables[get("table")].append(Word(get("table"), int(get("offset"), 16), 0, get("member"),
                                        get("type"), get("layer"), get("tier")))
    for table, words in tables.items():
        words.sort(key=lambda w: w.off)
        for k, word in enumerate(words):
            word.end = words[k + 1].off if k + 1 < len(words) else word.off + 4
    return dict(tables)


def stem(member: str) -> str:
    m = HUNGARIAN.match(member)
    rest = member[m.end():] if m else (member[2:] if member.startswith("m_") else member)
    return (rest[:1].upper() + rest[1:]) if rest else member


def _statements(code: str) -> tuple[str | None, list[str], list[str]]:
    """The receiver's name, the other parameters, and the body's statements, from decompiled C."""
    head, _, rest = code.partition("{")
    body = rest.rsplit("}", 1)[0]
    sig = _SIGNATURE.search(head.strip().splitlines()[-1] if head.strip() else "")
    params = [p.strip().split()[-1].lstrip("*") for p in (sig.group(1).split(",") if sig else [])
              if p.strip() and p.strip() != "void"]
    lines = []
    buffer = ""
    for raw in body.splitlines():
        line = raw.strip()
        # a comment line, not a store through a pointer (`*(int *)(this + 0x268) = param_1;`)
        if not line or line.startswith(("/*", "//")) or re.match(r"^\*(?:/|\s|$)", line):
            continue
        buffer = (buffer + " " + line).strip()
        if buffer.endswith(";"):
            if not _LOCAL_DECL.match(buffer):
                lines.append(buffer)
            buffer = ""
    return (params[0] if params else None), params[1:], lines


SCALARS = set(sdk_layout.WORD_TYPES) | {"string_t", "float", "double", "bool", "byte", "int",
                                         "short", "char", "uint", "undefined4", "undefined1",
                                         "undefined2", "EHANDLE", "CBaseHandle", "Activity"}


def _access_only(expr: str, recv: str) -> tuple[str, str | None] | None:
    """`(kind, test)` when `expr` is nothing but one access through `recv`: kind `value` (a raw
    word read), `ref` (the address, no dereference), `member` (the typed `recv->m_x` form, which
    is a value for a scalar member and an address for a struct one); `test` is the `==` / `!=`
    the access is compared against zero with, else None. None for a derived expression."""
    text = _CAST.sub("", expr).replace("(", "").replace(")", "").strip()
    test = None
    m = re.match(r"^(.*?)\s*(!=|==)\s*(?:0|0\.0|'\\0'|false)$", text)
    if m:
        text, test = m.group(1).strip(), m.group(2)
    address = text.startswith("&")
    deref = text.startswith("*") or "[" in text
    typed = "->" in text
    text = text.lstrip("*&").strip()
    if not (re.fullmatch(rf"{recv}\s*\+\s*0x[0-9a-f]+", text) or re.fullmatch(rf"{recv}->\w+", text)
            or re.fullmatch(rf"{recv}\[\d+\]", text) or text == recv):
        return None
    if text == recv and not deref:
        return None                   # `return this;`
    if address:
        return "ref", test
    if typed:
        return "member", test
    return ("value" if deref else "ref"), test


def accessor_pass(corpus: Corpus, sdk: sdk_layout.Sdk | None, chains: dict[str, list[str]],
                  taken: set[str], layout: dict[str, list[Word]] | None = None
                  ) -> tuple[list[Proposal], dict]:
    """Name leaf bodies that touch exactly one recovered word. See the module docstring."""
    layout = read_layout() if layout is None else layout
    callees = collections.Counter()
    for (caller,) in corpus.db.execute("SELECT caller FROM edges WHERE module = ?", (corpus.module,)):
        callees[caller] += 1
    stringy = {a for (a,) in corpus.db.execute(
        "SELECT DISTINCT func_addr FROM string_refs WHERE module = ?", (corpus.module,))}
    accesses: dict[str, list[tuple[str, int, str, str]]] = collections.defaultdict(list)
    for func, cls, off, field, kind in corpus.db.execute(
            "SELECT func_addr, cls, off, field, kind FROM accesses WHERE module = ?", (corpus.module,)):
        accesses[func].append((cls or "", off, field or "", kind or ""))

    base_chain = chains.get(BASE_TABLE, [BASE_TABLE])

    def word_at(cls: str, off: int) -> Word | None:
        """The recorded word a class touches at `off`: its own table, else the base line's where
        the class descends from it, else -- for a class off the NPC line (`CAISound` typed as the
        receiver of a `CBaseEntity` accessor) -- the base line's word when the word was introduced
        at or above the deepest ancestor the two share."""
        candidates = [cls] if cls in layout else []
        candidates += [t for t in layout if t != cls and cls in chains.get(t, [])]
        if BASE_TABLE in candidates:
            candidates.remove(BASE_TABLE)
            candidates.append(BASE_TABLE)
        for table in candidates:
            for word in layout[table]:
                if word.off <= off < word.end:
                    return word
        if not candidates and cls and cls not in base_chain:
            common = next((c for c in chains.get(cls, []) if c in base_chain), None)
            if common:
                for word in layout.get(BASE_TABLE, []):
                    if word.off <= off < word.end:
                        return word if word.layer in chains.get(common, [common]) else None
        return None

    header_text: dict[str, str] = {}

    def sdk_text(cls: str) -> tuple[Path, str] | None:
        path = sdk.header(cls) if sdk is not None else None
        if path is None:
            return None
        if cls not in header_text:
            text = path.read_text(encoding="utf-8", errors="replace")
            # the class's own unit and its shared half (`baseentity_shared.cpp` holds BloodColor)
            for name in (path.with_suffix(".cpp").name, f"{path.stem}_shared.cpp"):
                source = sdk.find_source(name)
                if source is not None:
                    text += "\n" + source.read_text(encoding="utf-8", errors="replace")
            header_text[cls] = text
        return path, header_text[cls]

    def sdk_inline(owner: str, member: str, setter: bool, in_slot: bool
                   ) -> tuple[str, str] | tuple[None, str] | None:
        """The SDK's name for a one-line body over `member` on `owner`'s chain: `(name, where)`;
        `(None, why)` when the SDK declares two such bodies; None when it declares none. Two
        bodies where one is a virtual of the chain and the body fills a slot: the virtual."""
        for cls in chains.get(owner, [owner]):
            found = sdk_text(cls)
            if found is None:
                continue
            path, text = found
            names = {m.group(1) for m in (SDK_SETTER if setter else SDK_GETTER).finditer(text)
                     if (m.group(3) if setter else m.group(2)) == member}
            if len(names) == 1:
                return next(iter(names)), f"{path.name} ({cls})"
            if names and in_slot:
                virtuals = {m.name for c in chains.get(owner, [owner]) for m in sdk.methods(c)[1]
                            if m.virtual} & names
                if len(virtuals) == 1:
                    return next(iter(virtuals)), (f"{path.name} ({cls}); of {len(names)} such bodies "
                                                  f"({', '.join(sorted(names))}) it is the virtual")
            if names:
                return None, f"SDK 2013 {path.name} declares {len(names)} such bodies ({', '.join(sorted(names))})"
        return None

    zero_names: set[str] | None = None

    def is_zero(token: str) -> bool:
        """`0`, `false`, or an SDK identifier defined as zero (`LIFE_ALIVE = 0`, the first
        enumerator, a `#define X 0`)."""
        nonlocal zero_names
        if token in ("0", "false", "NULL"):
            return True
        if sdk is None:
            return False
        if zero_names is None:
            zero_names = set()
            for sub in sdk_layout.HEADER_DIRS:
                base = sdk.root / sub
                if not base.is_dir():
                    continue
                for path in base.rglob("*.h"):
                    text = path.read_text(encoding="utf-8", errors="replace")
                    zero_names.update(re.findall(r"\b([A-Z_][A-Z0-9_]*)\s*=\s*0\b(?![x.])", text))
                    zero_names.update(re.findall(r"#define\s+([A-Z_][A-Z0-9_]*)[ \t]+0\b(?![x.])", text))
                    zero_names.update(re.findall(r"\benum\s+\w*\s*\{\s*([A-Z_][A-Z0-9_]*)\s*[,}]", text))
        return token in zero_names

    SDK_TEST = re.compile(r"\b(\w+)\s*\(\s*(?:void)?\s*\)\s*(?:const)?\s*\{\s*return\s+(?:\(\s*)?"
                          r"(?:this->)?(m_\w+)\s*(==|!=)\s*(\w+)\s*\)?\s*;\s*\}")

    def sdk_test(owner: str, member: str, op: str) -> tuple[str, str] | None:
        """The SDK's name for `return m_x == ZERO;` on `owner`'s chain, polarity kept."""
        for cls in chains.get(owner, [owner]):
            found = sdk_text(cls)
            if found is None:
                continue
            path, text = found
            names = {m.group(1) for m in SDK_TEST.finditer(text)
                     if m.group(2) == member and m.group(3) == op and is_zero(m.group(4))}
            if len(names) == 1:
                return next(iter(names)), f"{path.name} ({cls})"
        return None

    # Callers, thunks folded in: who passes what as the receiver of an untyped leaf.
    callers: dict[str, set[str]] = collections.defaultdict(set)
    thunk_of: dict[str, set[str]] = collections.defaultdict(set)
    for caller, callee in corpus.db.execute("SELECT caller, callee FROM edges WHERE module = ?",
                                            (corpus.module,)):
        callers[callee].add(caller)
        if caller in corpus.thunk:
            thunk_of[callee].add(caller)
    _RECEIVER = re.compile(r"\b(\w+)::~?\w+\s*\(\s*(\w+)\s*\*\s*this\b")

    def receiver_from_callers(addr: str) -> tuple[str | None, str]:
        """The class every typed call site passes its own `this` to; None with the reason."""
        sites = set(callers.get(addr, set()))
        for thunk in thunk_of.get(addr, set()):
            sites |= callers.get(thunk, set())
        call = re.compile(rf"\b(?:thunk_)?FUN_{addr}\(\s*(?:\([^()]*\)\s*)?(\w+)\s*(?:\+\s*0x[0-9a-f]+)?\s*[,)]")
        classes: set[str] = set()
        typed_sites = 0
        for caller in sites:
            if caller in corpus.thunk:
                continue
            code = corpus.code(caller)
            head, _, body = code.partition("{")
            m = _RECEIVER.search(head)
            cls = m.group(2) if m else None
            for site in call.finditer(body):
                if site.group(1) != "this" or "+ 0x" in site.group(0):
                    return None, "a call site passes something other than its own this"
                if cls:
                    classes.add(cls)
                    typed_sites += 1
        if not typed_sites:
            return None, "no typed call site"
        return min(classes, key=lambda c: (len(chains.get(c, [c])), c)), \
            f"{typed_sites} call site(s) passing this ({', '.join(sorted(classes)[:4])})"

    report = collections.Counter()
    reasons: dict[str, str] = {}
    proposals: list[Proposal] = []
    for addr in sorted(corpus.name):
        if addr in taken or addr in corpus.thunk or not is_placeholder(corpus.name[addr]):
            continue
        if corpus.size.get(addr, 0) > ACCESSOR_MAX_BYTES or callees[addr] or addr in stringy:
            continue
        touched = [a for a in accesses.get(addr, []) if a[3] in ("named", "this") and a[1] is not None]
        if not touched:
            continue
        if corpus.cc.get(addr) != "__thiscall":
            report["not thiscall"] += 1
            continue
        # An untyped `this` names no class; a slot body's class is the table that holds it, and
        # a leaf every typed caller hands its own `this` is laid out as the most-base of them.
        slots = corpus.fills.get(addr, set())
        holder = min((c for c, _ in slots), key=lambda c: (len(chains.get(c, [])), c)) if slots else ""
        typed_by = ""
        if not holder and any(not cls for cls, _, _, _ in touched):
            holder, typed_by = receiver_from_callers(addr)
            if holder is None:
                report["receiver untyped"] += 1
                reasons[addr] = f"accessor: receiver untyped, {typed_by}"
                continue
            typed_by = f"; receiver typed from {typed_by}"
        touched = [(cls or holder, off, field, kind) for cls, off, field, kind in touched]
        words = {(cls, w.table, w.off) if (w := word_at(cls, off)) else None for cls, off, _, _ in touched}
        if None in words:
            report["no layout word"] += 1
            reasons[addr] = "accessor: touches a word the layout does not record"
            continue
        if len({(t, o) for _, t, o in words}) != 1:
            report["two words"] += 1
            continue
        cls, table, off = next(iter(words))
        word = word_at(cls, off)
        if "." in word.member or word.tier == "interior":
            report["interior word"] += 1
            reasons[addr] = f"accessor: {word.member} is an interior word, no name is coined for it"
            continue
        recv, params, statements = _statements(corpus.code(addr))
        if recv is None or not statements:
            report["unreadable"] += 1
            continue
        scalar = (word.type.endswith("*") or word.type in SCALARS
                  or word.type.startswith(("CHandle<", "EHANDLE", "CBaseHandle"))
                  or (sdk is not None and word.type in sdk.scalars()))
        shape = None
        why = f"accessor: reads {word.member} inside a derived expression"
        if len(statements) == 1 and statements[0].startswith("return ") and statements[0] != "return;":
            probe = _access_only(statements[0][len("return "):-1], recv)
            if probe:
                kind, test = probe
                if kind == "member":
                    kind = "value" if scalar else "ref"
                if kind == "value" and not scalar:
                    why = f"accessor: reads one word of the struct member {word.member}"
                elif test and kind == "ref":
                    why = f"accessor: tests the address of {word.member}"
                elif test:
                    shape = ("test", test)
                else:
                    shape = (kind, None)
        elif params and all(re.match(r"^.+?\s=\s.+;$", s) for s in statements[:-1] or statements) \
                and (statements[-1] == "return;" or "=" in statements[-1]):
            value = params[0]
            assigns = [s for s in statements if s != "return;"]
            ok = bool(assigns)
            for s in assigns:
                lhs, _, rhs = s[:-1].partition(" = ")
                probe = _access_only(lhs, recv)
                rhs_text = _CAST.sub("", rhs).replace("(", "").replace(")", "").strip().lstrip("*")
                if not probe or probe[0] not in ("value", "member") \
                        or not re.fullmatch(rf"{value}(?:\[\d+\]|\s*!=\s*0)?", rhs_text):
                    ok = False
            if ok:
                shape = ("set", value)
        if shape is None:
            report["derived"] += 1
            reasons[addr] = why
            continue
        popped = corpus.words(addr)
        if popped is None or (shape[0] == "set") != (popped >= 1):
            report["arity"] += 1
            continue
        if slots:
            owner = _owner(corpus, addr, next(iter(slots))[1], chains) if len({s for _, s in slots}) == 1 else None
            if owner is None:
                report["folded"] += 1
                continue
        else:
            owner = word.layer or cls
        tier = "accessor"
        method = None
        source_note = "; the name is coined from the member, no SDK header declares the body"
        if shape[0] == "test":
            sdk_name = sdk_test(owner, word.member, shape[1])
            if sdk_name:
                method, where = sdk_name
                tier = "inferred"
                source_note = f"; SDK 2013 {where} defines that test as {method}"
            elif word.type != "bool":
                report["derived"] += 1
                reasons[addr] = (f"accessor: tests {word.member} {shape[1]} 0 and no SDK body "
                                 "states the name; the enum it compares against is needed")
                continue
        else:
            sdk_name = sdk_inline(owner, word.member, shape[0] == "set", bool(slots))
            if sdk_name and sdk_name[0]:
                method, where = sdk_name
                tier = "inferred"
                source_note = f"; SDK 2013 {where} declares that inline body as {method}"
            elif sdk_name:
                source_note = f"; the name is coined from the member: {sdk_name[1]}"
        if method is None:
            verb = {"set": "Set", "test": "Is" if shape[1] == "!=" else "IsNot"}.get(shape[0], "Get")
            method = f"{verb}{stem(word.member)}"
        what = {"value": f"returns {word.member}", "ref": f"returns the address of {word.member}",
                "test": f"returns {word.member} {shape[1]} 0",
                "set": f"stores {shape[1]} into {word.member}"}[shape[0]]
        via = f"{cls}" + (f" (slot {sorted(s for _, s in slots)[0]})" if slots else "")
        proposals.append(Proposal(
            corpus.module, addr, f"{owner}::{method}", tier,
            f"accessor: {corpus.size[addr]}-byte leaf {what} at +0x{off:x} ({word.type}, "
            f"{table} {word.tier} row, introduced by {word.layer}), touches nothing else, via {via}"
            f"{typed_by}; pops {popped} word(s)" + source_note, "accessor"))
        report[shape[0]] += 1
    out = dict(report)
    out["proposed"] = len(proposals)
    out["reasons"] = reasons
    return proposals, out


def run(connection: sqlite3.Connection, listing: sqlite3.Connection | None, module: str,
        binary: Path, research: Path,
        passes: tuple[str, ...] = ("slots", "tu", "message", "crt", "accessor", "identity")
        ) -> tuple[list[Proposal], dict]:
    root = sdk_layout.sdk_root(research)
    if root is None:
        return [], {"error": "no SDK class declarations under " + str(research)}
    sdk = sdk_layout.Sdk(root)
    corpus = Corpus(connection, module, listing)
    chains = primary_chains(binary)
    out: list[Proposal] = []
    report: dict = {"sdk": str(root)}
    taken: set[str] = set()
    if "slots" in passes:
        rows, report["slots"] = slot_pass(corpus, sdk, chains)
        out += rows
        taken |= {r.addr for r in rows}
    if "tu" in passes:
        rows, report["tu"] = tu_pass(corpus, sdk, taken)
        out += rows
        taken |= {r.addr for r in rows}
    if "message" in passes:
        rows, report["message"] = message_pass(corpus, sdk, chains, taken)
        out += rows
        taken |= {r.addr for r in rows}
    if "crt" in passes:
        import crt_match  # noqa: WPS433 -- beside this file
        archives = crt_match.crt_root(research)
        if archives is None:
            report["crt"] = {"error": "no staged VC6 SP5 archives; run `crt_fid stage`"}
        else:
            rows, report["crt"] = crt_match.crt_pass(corpus, binary, archives)
            out += [r for r in rows if r.addr not in taken]
            taken |= {r.addr for r in rows}
    if "accessor" in passes:
        rows, report["accessor"] = accessor_pass(corpus, sdk, chains, taken)
        out += rows
        taken |= {r.addr for r in rows}
    if "identity" in passes:
        extra = {r.addr: (r.name.split("::")[-1], r.tier) for r in out if r.tier != "unsettled"}
        rows, report["identity"] = identity_pass(corpus, sdk, chains, extra)
        out += [r for r in rows if r.addr not in extra]
    # A name two passes hand to two bodies is nobody's.
    holders = collections.Counter(r.name for r in out)
    clash = {n for n, c in holders.items() if c > 1}
    report["clashes"] = sorted(clash)
    return [r for r in out if r.name not in clash], report
