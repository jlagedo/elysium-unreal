"""Story 5 step 3: introduced species dispatch is C++ overrides and exact qualified calls.

Step 2 stood the 44 species classes as shells; their behaviour still ran on `FElysiumNpc`, chosen
at run time by census lookups (`OverrideOf`, `BodyOf`, address `Strcmp` arms, member-pointer
tables, class-keyed tables and `IsRetailClass` self-tests). This check holds the tree to the
step's authored records:

* `overrides-step3.tsv` is the override matrix: one row per introduced `(introducing class, slot,
  body)` of the census whose body the port runs (a live verdict with a defined port target) or
  dispatches by address. Its identity columns are re-derived from the accepted step-2 tree, so
  the record cannot drop a row; each row carries a reviewed disposition, and an `override` row
  must be a declared `override` in its class header with a definition in its cpp;
* the static gate: every dispatch token left in the non-test module (lookups, the dispatch guard,
  address arms, member-pointer rows, retail-class tests) is an enumerated surviving site in
  `decisions-step3.json` (deferred-class compatibility, a genuine type test or a census query),
  counted per enclosing definition; a new or grown site refuses;
* each recorded direct call (`decisions-step3.json` `direct_calls`) is spelled in its caller's
  definition, so a guarded base call names the recovered callee owner;
* no overlay target names a port symbol the step-2 tree defined and step 3 removed;
* the accepted step-2 receipt still verifies against its own tree, and the step-3 regression
  comparison, expectations and runtime receipts are pinned in `acceptance-step3.json`.

While phase 2 is current the step is in progress: `--check step3` validates the records against
the accepted step-2 tree and reports the open dispositions, and refuses nothing about the source.

    uv run elysium research kernel_migration --check step3
"""
from __future__ import annotations

import collections
import csv
import json
import re
import tempfile
from pathlib import Path

import kernel_migration as km
from kernel_migration_audit import file_sha
from kernel_migration_inventory import definitions
from elysium_pipeline.paths import repo_root

SOURCE = Path("Source/ElysiumUE")
PRIVATE = SOURCE / "Private"
SUBSTRATE = PRIVATE / "Substrate"
SHAPE = SUBSTRATE / "ElysiumNpcKernelShape.cpp"
VERDICTS = Path("research/tooling/ghidra/driver/kernel_verdicts.tsv")
# Generated files: the census and bindings carry every address as data, the slot surface every
# retail body as a stub; none of them dispatches.
GENERATED = {"ElysiumNpcKernelShape.cpp", "ElysiumNpcKernelBindings.cpp", "ElysiumNpcKernelSlots.cpp",
             "ElysiumNpcKernelSlots.inl"}
HISTORICAL_PATHS = ("Source/ElysiumUE", str(VERDICTS).replace("\\", "/"))
MATRIX_COLUMNS = ("retail_class", "slot", "address", "verdict", "target", "port_class", "port_method",
                  "disposition", "packet", "note")
IDENTITY = MATRIX_COLUMNS[:7]
LIVE_VERDICTS = {"rule", "present", "mechanism"}
PACKETS = {"3b", "3c", "3d", "3e", "3f", "3g", "3h", "3i"}
# override:<PortClass>::<method> (overrides an inherited virtual) | own:<PortClass>::<method> (a
# branch virtual the class itself introduces, slots past a base's table) | data-query:<consumer> |
# step4:<reason> | residue:<reason>; `investigate` only while the step is in progress.
DISPOSITION = re.compile(r"^((override|own):(FElysiumNpc\w+)::(\w+)|data-query:.+|step4:.+|residue:.+|investigate)$")
SURVIVOR_DISPOSITIONS = {"deferred", "type-test", "data-query", "lookup"}

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
GATE = re.compile(r"\b(OverrideOf|BodyOf|SpeciesDispatchRow|SpeciesSlotRowOf|SpeciesSlotRows|SpeciesSlotRow|"
                  r"FSpeciesDispatchScope|SpeciesDispatchingSlot|IsRetailClass|DerivesFrom|OfClassname)\b"
                  r"|&FElysiumNpc::\w+|\bStrcmp\s*\(")
ARM_OPERAND = re.compile(r'TEXT\("0x10[0-9a-fA-F]{6}"\)|\bG\w+Body_\w+|(?:->|\.)Address\b')

CLASS_ROW = re.compile(r'\{\s*TEXT\("(\w+)"\),\s*TEXT\("(\w*)"\),\s*TEXT\("0x[0-9a-fA-F]+"\),\s*\d+,')
OVERRIDE_ROW = re.compile(r'\{\s*TEXT\("(\w+)"\),\s*(\d+),\s*TEXT\("(0x[0-9a-f]+)"\),\s*TEXT\("(\w*)"\),'
                          r'\s*TEXT\("(\w*)"\),\s*TEXT\("([^"]*)"\)\s*\}')
SLOT_ROW = re.compile(r'\{\s*(\d+),\s*TEXT\("(\w*)"\),\s*TEXT\("(\w*)"\),\s*TEXT\("[^"]*"\),\s*TEXT\("([\w:]*)"\),')


def _blank(match: re.Match) -> str:
    return re.sub(r"[^\n]", " ", match.group(0))


def hand_sources(root: Path) -> dict[str, str]:
    """Non-test, non-generated module source, comments blanked (offsets kept), by repo path."""
    found = {}
    for path in sorted((root / PRIVATE).rglob("*")):
        rel = path.relative_to(root).as_posix()
        if path.suffix not in {".h", ".cpp", ".inl"} or "/Tests/" in rel or path.name in GENERATED:
            continue
        found[rel] = COMMENT.sub(_blank, path.read_text(encoding="utf-8-sig"))
    return found


def defined_symbols(root: Path) -> set[str]:
    code = []
    for path in sorted((root / PRIVATE).rglob("*.cpp")):
        if "/Tests/" not in path.as_posix():
            code.append(COMMENT.sub("", path.read_text(encoding="utf-8-sig")))
    return set(re.findall(r"\b(FElysium\w+::~?\w+)\s*\(", "\n".join(code)))


def read_verdicts(root: Path) -> dict[str, tuple[str, str]]:
    rows = {}
    for line in (root / VERDICTS).read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if len(cells) > 3 and re.fullmatch(r"[0-9a-f]{8}", cells[0]):
            rows[cells[0]] = (cells[1], cells[3])
    return rows


def _target_symbol(target: str) -> str:
    target = target.removeprefix("hand:")
    return target.rsplit(":", 1)[-1] if re.match(r"^[\w/]+\.(?:cpp|h|inl):", target) else target


def override_matrix(root: Path, classes: list[dict]) -> list[dict]:
    """The introduced override rows of the census at `root`, joined to the verdict overlay.

    A census row repeats a body its base already holds (the vtable copy); the row belongs to the
    topmost class of the chain holding the same body at that slot, which is where the C++ override
    stands. A row enters the matrix when the port runs its body (live verdict, defined target) or
    when hand-written source dispatches on its address."""
    text = COMMENT.sub("", (root / SHAPE).read_text(encoding="utf-8"))
    bases = dict(CLASS_ROW.findall(text))
    block = text[text.index("GOverrides[] ="):]
    block = block[:block.index("};")]
    rows = [(c, int(s), a, *rest) for c, s, a, *rest in OVERRIDE_ROW.findall(block)]
    by_class: dict[str, dict[int, str]] = collections.defaultdict(dict)
    for cls, slot, address, *_ in rows:
        by_class[cls][slot] = address
    slot_block = text[text.index("GSlots[] ="):text.index("GClasses[] =")]
    # The port's callable for the slot, unqualified: the override is declared on the species class.
    port_methods = {(int(s), c): pm.rsplit("::", 1)[-1] for s, c, _m, pm in SLOT_ROW.findall(slot_block)}
    introduced = {r["retail_class"]: r for r in classes if r["step"] == "2" and r["port_class"] != "FElysiumNpc"}
    verdicts = read_verdicts(root)
    defined = defined_symbols(root)
    arms = set(a.lower() for code in hand_sources(root).values()
               for a in re.findall(r'"(0x10[0-9a-fA-F]{6})"', code))

    def introducing(cls: str, slot: int, address: str) -> str:
        top, base = cls, bases.get(cls)
        while base and by_class.get(base, {}).get(slot) == address:
            top, base = base, bases.get(base)
        return top

    def port_method(cls: str, slot: int) -> str:
        owner = cls
        while owner:
            if (slot, owner) in port_methods:
                return port_methods[slot, owner]
            owner = bases.get(owner, "")
        return port_methods.get((slot, ""), f"Slot{slot}")

    matrix, seen = [], set()
    for cls, slot, address, _method, _verdict, _default in rows:
        top = introducing(cls, slot, address)
        if top not in introduced or (top, slot, address) in seen:
            continue
        seen.add((top, slot, address))
        verdict, target = verdicts.get(address[2:], ("", "-"))
        live = verdict in LIVE_VERDICTS and _target_symbol(target) in defined
        if not live and address not in arms:
            continue
        matrix.append({"retail_class": top, "slot": str(slot), "address": address, "verdict": verdict or "-",
                       "target": target, "port_class": introduced[top]["port_class"],
                       "port_method": port_method(top, slot)})
    return sorted(matrix, key=lambda r: (int(r["slot"]), r["retail_class"], r["address"]))


def read_matrix(path: Path) -> list[dict]:
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    rows = list(csv.DictReader(lines, delimiter="\t"))
    if not rows or tuple(rows[0].keys()) != MATRIX_COLUMNS:
        raise km.InvalidManifest(f"{path.name}: unexpected columns")
    for row in rows:
        if not DISPOSITION.match(row["disposition"]) or row["packet"] not in PACKETS:
            raise km.InvalidManifest(f"{path.name}: bad disposition/packet: {row}")
        if not row["disposition"].startswith(("override:", "own:", "investigate")) and not row["note"]:
            raise km.InvalidManifest(f"{path.name}: a non-override row needs its note: {row}")
    return rows


def check_matrix_identity(rows: list[dict], expected: list[dict]) -> None:
    have = [tuple(r[c] for c in IDENTITY) for r in rows]
    want = [tuple(r[c] for c in IDENTITY) for r in expected]
    if have != want:
        missing = sorted(set(want) - set(have))[:10]
        extra = sorted(set(have) - set(want))[:10]
        raise km.InvalidManifest(f"override matrix differs from the step-2 census: missing {missing}, extra {extra}")


def _declares_override(header: str, method: str) -> bool:
    return bool(re.search(rf"\b{method}\s*\([^;{{]*\)[^;{{]*\boverride\b", header))


def check_overrides(rows: list[dict], root: Path) -> int:
    """Every `override` row is a declared override on its class and every `own` row a virtual
    the class declares, each defined in the class's cpp."""
    found = 0
    for row in rows:
        match = DISPOSITION.match(row["disposition"])
        if not match or not match.group(3):
            continue
        kind, cls, method = match.group(2), match.group(3), match.group(4)
        if cls != row["port_class"]:
            raise km.InvalidManifest(f"{row['retail_class']} {row['slot']}: {kind} on {cls}, "
                                     f"not the introducing class {row['port_class']}")
        stem = cls[1:]
        header = COMMENT.sub("", (root / SUBSTRATE / f"{stem}.h").read_text(encoding="utf-8-sig"))
        source = COMMENT.sub("", (root / SUBSTRATE / f"{stem}.cpp").read_text(encoding="utf-8-sig"))
        declared = (_declares_override(header, method) if kind == "override" else
                    bool(re.search(rf"\bvirtual\b[^;{{()]*\b{method}\s*\(", header)))
        if not declared:
            raise km.InvalidManifest(f"{cls} does not declare {method} as {kind} (slot {row['slot']})")
        if not re.search(rf"\b{cls}::{method}\s*\(", source):
            raise km.InvalidManifest(f"{cls}::{method} is not defined in {stem}.cpp")
        found += 1
    return found


def gate_sites(root: Path) -> collections.Counter:
    """(file, enclosing definition, token) -> count of dispatch tokens in the non-test module."""
    sites: collections.Counter = collections.Counter()
    for rel, code in hand_sources(root).items():
        spans = [(d.start, d.end, d.symbol) for d in definitions(code, rel)]
        for match in GATE.finditer(code):
            token = match.group(1) or ("&FElysiumNpc::" if match.group(0).startswith("&") else "Strcmp")
            if token == "Strcmp":
                end = code.find(";", match.start())
                if not ARM_OPERAND.search(code[match.start():end if end >= 0 else len(code)]):
                    continue
            if rel.endswith("ElysiumNpcKernelBindings.cpp") and token == "&FElysiumNpc::":
                continue
            symbol = next((s for a, b, s in spans if a <= match.start() < b), "<file scope>")
            sites[rel, symbol, token] += 1
    return sites


def check_gate(decisions: dict, root: Path) -> int:
    listed: collections.Counter = collections.Counter()
    for site in decisions["surviving_sites"]:
        if site["disposition"] not in SURVIVOR_DISPOSITIONS or not site.get("reason"):
            raise km.InvalidManifest(f"a surviving site needs a survivor disposition and reason: {site}")
        if site["disposition"] == "deferred" and site.get("removal_step") not in {7, 8, 9, 10}:
            raise km.InvalidManifest(f"a deferred surviving site names its fold step: {site}")
        listed[site["file"], site["symbol"], site["token"]] += int(site["count"])
    found = gate_sites(root)
    if found != listed:
        unlisted = sorted((found - listed).items())[:20]
        gone = sorted((listed - found).items())[:20]
        raise km.InvalidManifest(f"dispatch sites differ from the surviving list: unlisted {unlisted}, gone {gone}")
    return sum(found.values())


def check_direct_calls(decisions: dict, root: Path) -> int:
    """Each recorded direct call is spelled in its caller: the recovered owner, qualified."""
    bodies: dict[str, list[str]] = collections.defaultdict(list)
    for path in sorted((root / PRIVATE).rglob("*")):
        rel = path.relative_to(root).as_posix()
        if path.suffix in {".cpp", ".inl", ".h"} and "/Tests/" not in rel:
            text = COMMENT.sub(_blank, path.read_text(encoding="utf-8-sig"))
            for d in definitions(text, rel):
                bodies[d.symbol].append(text[d.start:d.end])
    for call in decisions["direct_calls"]:
        found = bodies.get(call["caller"], [])
        if len(found) != 1:
            raise km.InvalidManifest(f"direct-call caller {call['caller']} has {len(found)} definitions")
        if call["required"] not in found[0]:
            raise km.InvalidManifest(f"{call['caller']} does not call {call['required']} "
                                     f"(retail {call['retail']['site']} -> {call['retail']['callee']})")
        for forbidden in call.get("forbidden", []):
            if forbidden in found[0]:
                raise km.InvalidManifest(f"{call['caller']} still calls {forbidden}")
    return len(decisions["direct_calls"])


def check_step3(directory: Path | None = None) -> dict:
    directory = directory or repo_root() / km.STORY
    manifest, classes, _ = km.load(directory)
    commit = manifest.get("history", {}).get("step2", {}).get("commit", "")
    if not commit:
        raise km.InvalidManifest("step 3 needs the accepted step-2 tree (history.step2.commit)")
    rows = read_matrix(directory / "overrides-step3.tsv")
    decisions = json.loads((directory / "decisions-step3.json").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="step3-before-") as scratch:
        before = km.historical_source(commit, Path(scratch), HISTORICAL_PATHS)
        check_matrix_identity(rows, override_matrix(before, classes))
        if manifest["phase"] < 3:
            open_rows = collections.Counter(r["packet"] for r in rows if r["disposition"] == "investigate")
            return {"pending": True, "matrix_rows": len(rows),
                    "dispositions": dict(collections.Counter(r["disposition"].split(":")[0] for r in rows)),
                    "open_by_packet": dict(sorted(open_rows.items()))}
        from kernel_migration_step1 import check_overlay_targets
        removed = check_overlay_targets(before, repo_root())
    from kernel_migration_step2 import check_step2
    step2 = check_step2(directory)
    root = repo_root()
    if any(r["disposition"] == "investigate" for r in rows):
        raise km.InvalidManifest("override matrix rows are still under investigation")
    counts = {
        "matrix_rows": len(rows),
        "overrides": check_overrides(rows, root),
        "surviving_sites": check_gate(decisions, root),
        "direct_calls": check_direct_calls(decisions, root),
        "removed_symbols": removed,
    }
    record = json.loads((directory / "acceptance-step3.json").read_text(encoding="utf-8"))
    if record.get("scope") != "step-3-species-dispatch":
        raise km.InvalidManifest("acceptance-step3.json does not record step 3")
    artifacts = {}
    for name in ("delta", "runtime", "cheap_checks"):
        ref = record["artifacts"][name]
        path = Path(ref["path"])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref["sha256"]:
            raise km.InvalidManifest(f"step-3 evidence changed/missing: {name}")
        artifacts[name] = json.loads(path.read_text(encoding="utf-8-sig"))
    delta = artifacts["delta"]
    if not delta.get("comparison_passed") or delta.get("differences"):
        raise km.InvalidManifest("step-3 regression comparison has unmatched differences")
    expectations = json.loads((directory / "expectations/step-3.json").read_text(encoding="utf-8"))
    unused = {c["id"] for c in expectations["changes"]} - set(delta.get("applied_expectations", []))
    if unused:
        raise km.InvalidManifest("unconsumed step-3 expectations: " + ", ".join(sorted(unused)))
    if len(artifacts["runtime"]) != 4 or any(r["exit"] for r in artifacts["runtime"]):
        raise km.InvalidManifest("step-3 runtime gate incomplete/failing")
    if not artifacts["cheap_checks"] or any(r["exit"] for r in artifacts["cheap_checks"]):
        raise km.InvalidManifest("step-3 cheap/Python gates incomplete/failing")
    return {**counts, "step2": step2}


def draft_matrix(root: Path, classes: list[dict]) -> list[dict]:
    """A first disposition per matrix row, for review: a body source dispatches by address
    becomes an override of the slot's port method on the introducing class; a `registry:`
    constant nothing dispatches moves with the bodies in step 4; the rest is investigated."""
    arms = set(a.lower() for code in hand_sources(root).values()
               for a in re.findall(r'"(0x10[0-9a-fA-F]{6})"', code))
    text = COMMENT.sub("", (root / SHAPE).read_text(encoding="utf-8"))
    own_slots = {(int(s), c) for s, c, _m, _pm in
                 SLOT_ROW.findall(text[text.index("GSlots[] ="):text.index("GClasses[] =")])
                 if c and c != "CAI_BaseNPCTroika"}
    rows = []
    for row in override_matrix(root, classes):
        slot = int(row["slot"])
        packet = ("3c" if slot in {482, 510, 593, 606} else
                  "3g" if 487 <= slot <= 509 or slot in {620, 621} else
                  "3d" if slot in {21, 22, 23, 25, 26, 27, 35, 363, 588, 596, 597, 598, 599, 600, 601, 602, 609} else
                  "3e" if slot in {104, 105, 126, 127, 130, 180, 310, 375, 420, 422, 461} else "3f")
        kind = "own" if (slot, row["retail_class"]) in own_slots else "override"
        if row["address"] in arms:
            disposition, note = f"{kind}:{row['port_class']}::{row['port_method']}", ""
        elif row["target"].startswith("registry:"):
            disposition, note = "step4:registry constant", "GOverrides.Default has no production reader"
        else:
            disposition, note = "investigate", ""
        rows.append({**row, "disposition": disposition, "packet": packet, "note": note})
    return rows


def write_matrix(path: Path, rows: list[dict]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write("# Story 5 step 3 override matrix; identity columns re-derived by "
                  "`kernel_migration --check step3` from the accepted step-2 tree.\n")
        writer = csv.DictWriter(out, MATRIX_COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
