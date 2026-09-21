# -*- coding: utf-8 -*-
"""Cross the generated SAVE walk against the NPC's hand-written component serializers.

0019 story 2 pass C.  `AddNpcSaveFields` (``ElysiumNpcKernelBindings.cpp``) registers every
retail ``SAVE``-only row under its retail member name, and `ApplyEntityRecord` restores those
by name BEFORE it replays the leaf blob -- so any word a component ``Serialize`` also writes is
persisted twice and the blob silently wins.  This probe names the overlap so the deletion is a
measured list rather than an eyeball.

    uv run elysium research save_walk_overlap
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from elysium_pipeline.paths import repo_root

SUBSTRATE = ("Source", "ElysiumUE", "Private", "Substrate")

# One entry per component serializer that writes words the walk also carries.  `prefix` is the
# member path `AddNpcSaveFields` reaches the component through; `span` is the function to scan.
SERIALIZERS = [
    ("Senses.Memory.", "ElysiumNpcSenses.cpp", "void FElysiumNpcMemory::Serialize"),
    ("Senses.", "ElysiumNpcSenses.cpp", "void FElysiumNpcSenses::Serialize"),
    ("ScheduleHost.", "ElysiumNpcScheduleHost.cpp", "void FElysiumNpcScheduleHost::Serialize"),
    ("Witness.", "ElysiumNpcWitness.cpp", "void FElysiumNpcWitness::Serialize"),
]

VIA = re.compile(
    r'ElysiumAddClassFieldVia<FElysiumNpc>\(D,\s*TEXT\("([^"]+)"\),\s*'
    r'\[\]\(auto&\s*E\)\s*->\s*auto&\{\s*return\s+E\.([^;]+);\s*\}',
    re.S,
)


def _body(text: str, signature: str) -> str:
    start = text.index(signature)
    depth, i = 0, text.index("{", start)
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[start : j + 1]
    raise SystemExit(f"unterminated body for {signature}")


def main(argv: list[str] | None = None) -> int:
    root = repo_root()
    sub = root.joinpath(*SUBSTRATE)
    bindings = (sub / "ElysiumNpcKernelBindings.cpp").read_text(encoding="utf-8")
    walk = _body(bindings, "void AddNpcSaveFields")
    bound = {path: name for name, path in VIA.findall(walk)}

    total = 0
    for prefix, filename, signature in SERIALIZERS:
        text = (sub / filename).read_text(encoding="utf-8")
        if signature not in text:
            # Pass C retired the whole serializer: every word it wrote is a registered row now.
            print(f"===== {signature.split('::')[0].split()[-1]} -- retired, the walk carries it")
            continue
        body = _body(text, signature)
        # `Ar << X;` and `Ar << X << Y << Z;` alike, plus the `Sound.Field` lambda members.
        written: list[str] = []
        for line in body.splitlines():
            m = re.match(r"\s*Ar\s*<<\s*(.+);\s*$", line)
            if not m:
                continue
            for term in m.group(1).split("<<"):
                written.append(term.strip())
        # A bool round-trips through a `uint8` local; the load branch says which member it is.
        temps = {temp: member for member, temp in
                 re.findall(r"^\s*(\w+)\s*=\s*(\w+)\s*!=\s*0;", body, re.M)}
        hits, misses = [], []
        for term in written:
            term = temps.get(term, term)
            # An array element is written through a loop index; the walk names it per element.
            keys = ([prefix + term.replace("[i]", f"[{n}]") for n in range(8)]
                    if "[i]" in term else [prefix + term])
            found = [k for k in keys if k in bound]
            (hits if found else misses).append((term, found))
        dup = [t for t, f in hits for _ in f]
        total += len(dup)
        print(f"===== {signature.split('::')[0].split()[-1]} -- {len(dup)} duplicated, "
              f"{len(misses)} port-only")
        for term, found in hits:
            for key in found:
                print(f"  DUP  {key:48s} <- {bound[key]}")
        for term, _ in misses:
            print(f"  keep {prefix + term}")
    print(f"\ntotal duplicated words: {total}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
