"""Story 5 step 2: the species classes stand, and every classname builds the class retail's factory
builds.

This check holds the tree to the reviewed factory map (`factories.tsv`) and class table
(`classes.tsv`), and the step's authored records to the tree:

* the census's classnames (`ElysiumNpcKernelShape.cpp`) are exactly the factories: one class per
  classname, a class claiming only the names its own factory builds (no proximity claimant);
* each of the 44 step-2 species classes has its header and cpp, derives from its Appendix A parent
  and answers its own census row;
* the registrar registers exactly the 45 step-2 classnames against their typed factories and the
  44 retail classes as abstract descriptors chained as retail derives them;
* `registrations-step2.tsv` covers the 45 names with dispositions re-derived from `factories.tsv`
  (30 newly active, 6 corrected identities) and a recorded smoke result for each;
* `SetRetailClassForTests` survives only at the enumerated deferred-class sites;
* the accepted step-1 receipt still verifies against its own tree, and the step-2 regression
  comparison, expectations and runtime receipts are pinned in `acceptance-step2.json`.

    uv run elysium research kernel_migration --check step2
"""
from __future__ import annotations

import collections
import csv
import json
import re
from pathlib import Path

import kernel_migration as km
from kernel_migration_audit import file_sha
from elysium_pipeline.paths import repo_root

SUBSTRATE = Path("Source/ElysiumUE/Private/Substrate")
TESTS = Path("Source/ElysiumUE/Private/Tests")
SHAPE = SUBSTRATE / "ElysiumNpcKernelShape.cpp"
REGISTRAR = SUBSTRATE / "ElysiumNpcClasses.cpp"
REGISTRATION_COLUMNS = ("classname", "retail_class", "port_class", "prior_registry",
                        "prior_census_class", "registry_change", "identity_change", "placed",
                        "placed_maps", "maker_rows", "maker_maps", "script_files", "smoke")
SMOKE = re.compile(r"^(stood|stood-unported|unexercised): .+")
DEFERRED_LATCH = {"CNPC_VPlayerController", "CNPC_VFrenzyShadow", "CNPC_VWolfMorph"}
CLASSNAMES_ARRAY = re.compile(r"constexpr const TCHAR\* (\w+)_Classnames\[\] =\s*\{([^}]*)\}", re.S)
TEXT = re.compile(r'TEXT\("([^"]+)"\)')
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def step2_classes(classes: list[dict]) -> list[dict]:
    return [r for r in classes if r["step"] == "2" and r["port_class"] != "FElysiumNpc"]


CENSUS_ROW = re.compile(r'\{\s*TEXT\("(\w+)"\),\s*TEXT\("(\w*)"\),\s*TEXT\("0x[0-9a-fA-F]+"\),\s*\d+,'
                        r'\s*\d+,\s*(\w+),\s*(\d+)\s*\}')


def check_census(factories: list[dict], root: Path) -> int:
    """Every class's census classnames equal the classnames its factory builds, exactly, and each
    class row binds its own array with the matching count."""
    text = COMMENT.sub("", (root / SHAPE).read_text(encoding="utf-8"))
    claimed = {cls: sorted(TEXT.findall(body), key=str.casefold)
               for cls, body in CLASSNAMES_ARRAY.findall(text)}
    rows = CENSUS_ROW.findall(text)
    if not rows:
        raise km.InvalidManifest("no census class rows found")
    for cls, _base, array, count in rows:
        names = claimed.get(cls, [])
        bound = (f"{cls}_Classnames", str(len(names))) if names else ("nullptr", "0")
        if (array, count) != bound:
            raise km.InvalidManifest(f"census row {cls} binds {array}/{count}, not {bound[0]}/{bound[1]}")
    expected: dict[str, list[str]] = {}
    for row in factories:
        expected.setdefault(row["retail_class"], []).append(row["classname"])
    expected = {cls: sorted(names, key=str.casefold) for cls, names in expected.items()}
    if claimed != expected:
        diff = sorted(cls for cls in set(claimed) | set(expected) if claimed.get(cls) != expected.get(cls))
        raise km.InvalidManifest("census classnames differ from the factory map: " + ", ".join(diff))
    return sum(len(names) for names in claimed.values())


def check_shells(classes: list[dict], root: Path) -> int:
    for row in step2_classes(classes):
        stem = row["port_class"][1:]
        header = root / SUBSTRATE / f"{stem}.h"
        source = root / SUBSTRATE / f"{stem}.cpp"
        if not header.is_file() or not source.is_file():
            raise km.InvalidManifest(f"{row['port_class']}: missing {stem}.h/.cpp")
        code = COMMENT.sub("", header.read_text(encoding="utf-8-sig"))
        if not re.search(rf"\bclass {row['port_class']} : public {row['port_base']}\b", code):
            raise km.InvalidManifest(f"{row['port_class']} does not derive from {row['port_base']}")
        body = COMMENT.sub("", source.read_text(encoding="utf-8-sig"))
        own = re.search(rf"{row['port_class']}::OwnRetailClass\(\) const\s*\{{(.*?)\n\}}", body, re.S)
        if own is None or f'Find(TEXT("{row["retail_class"]}"))' not in own.group(1):
            raise km.InvalidManifest(f"{row['port_class']} does not answer {row['retail_class']}")
    return len(step2_classes(classes))


def check_registrar(classes: list[dict], factories: list[dict], root: Path) -> int:
    code = COMMENT.sub("", (root / REGISTRAR).read_text(encoding="utf-8-sig"))
    port = {r["retail_class"]: r["port_class"] for r in step2_classes(classes)}
    table = re.search(r"GNpcClassnames\[\] =\s*\{(.*?)\n\};", code, re.S)
    names = set(re.findall(r'\{ TEXT\("(\w+)"\), TEXT\("(\w+)"\),\s*&MakeNpcOf<(\w+)> \}',
                           table.group(1) if table else ""))
    expected = {(f["classname"], f["retail_class"], port[f["retail_class"]])
                for f in factories if f["retail_class"] in port}
    if names != expected:
        raise km.InvalidManifest(f"registered classnames differ: {sorted(names ^ expected)}")
    block = re.search(r"GNpcRetailClasses\[\] =\s*\{(.*?)\n\};", code, re.S)
    bases = set(re.findall(r'\{ TEXT\("(\w+)"\), TEXT\("(\w+)"\) \}', block.group(1) if block else ""))
    expected_bases = {(r["retail_class"], r["retail_base"]) for r in step2_classes(classes)}
    if bases != expected_bases:
        raise km.InvalidManifest(f"abstract retail descriptors differ: {sorted(bases ^ expected_bases)}")
    loops = (r"for \(const FElysiumNpcRetailClassRow& Row : GNpcRetailClasses\)\s*\{[^}]*"
             r"Reg\.RegisterAbstract\(FName\(Row\.RetailClass\), FName\(Row\.RetailBase\)\)",
             r"for \(const FElysiumNpcClassnameRow& Row : GNpcClassnames\)\s*\{\s*"
             r"Reg\.Register\(FName\(Row\.Classname\), FName\(Row\.RetailClass\), Row\.Factory\);")
    if not all(re.search(loop, code) for loop in loops):
        raise km.InvalidManifest("the registrar does not register the retail rows abstract and the "
                                 "classnames under their retail class")
    if 'RegisterAbstract(TEXT("CAI_BaseNPCTroika")' not in code:
        raise km.InvalidManifest("the combined base projection is not an abstract descriptor")
    if re.search(r"Register\([^;]*&MakeNpc\)", code):
        raise km.InvalidManifest("a classname still registers the untyped shared NPC leaf")
    return len(names)


def check_registrations(classes: list[dict], factories: list[dict], path: Path,
                        require_smoke: bool = True) -> dict[str, int]:
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    rows = list(csv.DictReader(lines, delimiter="\t"))
    if not rows or tuple(rows[0].keys()) != REGISTRATION_COLUMNS:
        raise km.InvalidManifest(f"{path.name}: unexpected columns")
    port = {r["retail_class"]: r["port_class"] for r in step2_classes(classes)}
    wanted = {f["classname"]: f for f in factories if f["retail_class"] in port}
    if {r["classname"] for r in rows} != set(wanted) or len(rows) != len(wanted):
        raise km.InvalidManifest(f"{path.name} does not cover exactly the step-2 classnames")
    counts = {"newly-active": 0, "corrected": 0}
    for row in rows:
        f = wanted[row["classname"]]
        change = "unchanged" if f["current_registry"] == "shared-npc" else "newly-active"
        identity = "unchanged" if f["prior_census_class"] == f["retail_class"] else "corrected"
        if (row["retail_class"], row["port_class"], row["prior_registry"], row["prior_census_class"],
                row["registry_change"], row["identity_change"]) != (
                f["retail_class"], port[f["retail_class"]], f["current_registry"],
                f["prior_census_class"], change, identity):
            raise km.InvalidManifest(f"{row['classname']}: registration row disagrees with factories.tsv")
        counts["newly-active"] += change == "newly-active"
        counts["corrected"] += identity == "corrected"
        if require_smoke:
            if not SMOKE.match(row["smoke"]):
                raise km.InvalidManifest(f"{row['classname']}: no recorded smoke result")
            # A newly active or corrected classname must have stood in the game, not only in tests.
            if (change != "unchanged" or identity != "unchanged") and row["smoke"].startswith("unexercised"):
                raise km.InvalidManifest(f"{row['classname']}: newly active/corrected but never stood")
    # Six: `npc_VVampireBoss` was already on its own census row (only its registration was a stub).
    if counts != {"newly-active": 30, "corrected": 6}:
        raise km.InvalidManifest(f"expected 30 newly active and 6 corrected names, found {counts}")
    return counts


LATCH = re.compile(r"(void\s+(?:FElysiumNpc::)?)?\bSetRetailClassForTests\s*\((.*?)\)\s*[;{]", re.S)
SOURCE = Path("Source/ElysiumUE")


def latch_sites(root: Path) -> list[dict]:
    """Every `SetRetailClassForTests(...)` CALL anywhere in the module (comments blanked, calls
    spanning lines included), with its argument; the declaration and definition are not calls."""
    sites = []
    for path in sorted((root / SOURCE).rglob("*")):
        if path.suffix not in {".h", ".cpp", ".inl"}:
            continue
        code = COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)),
                           path.read_text(encoding="utf-8-sig"))
        for match in LATCH.finditer(code):
            if match.group(1):
                continue   # `void [FElysiumNpc::]SetRetailClassForTests(const TCHAR* ...)`
            sites.append({"file": path.relative_to(root / SOURCE).as_posix(),
                          "line": code.count("\n", 0, match.start()) + 1,
                          "argument": re.sub(r"\s+", " ", match.group(2)).strip()})
    return sites


def check_latches(decisions: dict, root: Path) -> int:
    """Only the enumerated deferred-class sites keep the test latch."""
    allowed = decisions["compatibility"]["set_retail_class_for_tests"]
    for entry in allowed:
        if not set(entry["classes"]) <= DEFERRED_LATCH | {"nullptr"}:
            raise km.InvalidManifest(f"a latch site names a non-deferred class: {entry}")
    sites = latch_sites(root)
    for site in sites:
        if not site["file"].startswith("Private/Tests/"):
            raise km.InvalidManifest(f"production code calls SetRetailClassForTests: {site}")
        literal = re.fullmatch(r'TEXT\("(\w+)"\)', site["argument"])
        if literal and literal.group(1) not in DEFERRED_LATCH:
            raise km.InvalidManifest(f"a latch site stands a non-deferred class: {site}")
    # A multiset: a second call with the same argument is a new site, not the listed one.
    found = collections.Counter((s["file"], s["argument"]) for s in sites)
    listed = collections.Counter((e["file"], e["argument"]) for e in allowed)
    if found != listed:
        raise km.InvalidManifest(f"SetRetailClassForTests sites differ from the reviewed list: "
                                 f"unlisted {sorted((found - listed).elements())}, "
                                 f"gone {sorted((listed - found).elements())}")
    return sum(found.values())


def check_step2(directory: Path | None = None) -> dict:
    directory = directory or repo_root() / km.STORY
    manifest, classes, factories = km.load(directory)
    if manifest["phase"] != 2:
        raise km.InvalidManifest("step 2 is checked only while phase 2 is current")
    from kernel_migration_step1 import check_step1
    step1 = check_step1(directory)
    root = repo_root()
    decisions = json.loads((directory / "decisions-step2.json").read_text(encoding="utf-8"))
    counts = {
        "census_classnames": check_census(factories, root),
        "species_classes": check_shells(classes, root),
        "registered_classnames": check_registrar(classes, factories, root),
        "registrations": check_registrations(classes, factories, directory / "registrations-step2.tsv"),
        "deferred_latch_sites": check_latches(decisions, root),
    }
    record = json.loads((directory / "acceptance-step2.json").read_text(encoding="utf-8"))
    if record.get("scope") != "step-2-species-shells-and-factories":
        raise km.InvalidManifest("acceptance-step2.json does not record step 2")
    for name in ("smoke", "creation_paths"):
        ref = record[name]
        path = Path(ref["path"])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref["sha256"]:
            raise km.InvalidManifest(f"step-2 evidence changed/missing: {name}")
    artifacts = {}
    for name in ("delta", "runtime", "cheap_checks"):
        ref = record["artifacts"][name]
        path = Path(ref["path"])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref["sha256"]:
            raise km.InvalidManifest(f"step-2 evidence changed/missing: {name}")
        artifacts[name] = json.loads(path.read_text(encoding="utf-8-sig"))
    delta = artifacts["delta"]
    if not delta.get("comparison_passed") or delta.get("differences"):
        raise km.InvalidManifest("step-2 regression comparison has unmatched differences")
    expectations = json.loads((directory / "expectations/step-2.json").read_text(encoding="utf-8"))
    unused = {c["id"] for c in expectations["changes"]} - set(delta.get("applied_expectations", []))
    if unused:
        raise km.InvalidManifest("unconsumed step-2 expectations: " + ", ".join(sorted(unused)))
    if len(artifacts["runtime"]) != 4 or any(r["exit"] for r in artifacts["runtime"]):
        raise km.InvalidManifest("step-2 runtime gate incomplete/failing")
    if not artifacts["cheap_checks"] or any(r["exit"] for r in artifacts["cheap_checks"]):
        raise km.InvalidManifest("step-2 cheap/Python gates incomplete/failing")
    return {**counts, "step1": step1["rule_identity_sha256"]}
