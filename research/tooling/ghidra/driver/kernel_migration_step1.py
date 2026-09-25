"""Story 5 step 1: the dead species subset is gone and nothing live went with it.

`deletions-step1.tsv` is the authored record of every definition, arm, row, storage member and
test the step removed, kept or edited. This check holds the current tree to that record, and the
record to the tree:

* every row existed in the accepted step-0 tree; a deleted row is absent now, a kept row is still
  present, an edited test still exists and an edited comment no longer reads as it did;
* every definition and every automation test the step removed has a `deleted` row — the record is
  complete for those two kinds (arms, table rows and storage are held row by row only);
* no step-0 definition of a body outside the no-instance rows was lost (matched by symbol AND
  parameter list), except through a reviewed exemption whose evidence is re-derived: a named live
  home still defined, or a retail caller set equal to the ledger's and dead or explained;
* the dead classes stay in the census and the live rule inventory keeps its identity.

    uv run elysium research kernel_migration --check step1
"""
from __future__ import annotations

import json
import re
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Callable

import kernel_migration as km
from kernel_migration_audit import check_inventory, file_sha
from kernel_migration_inventory import definitions
from elysium_pipeline.paths import repo_root

DELETION_COLUMNS = ("packet", "kind", "file", "symbol", "fingerprint", "retail_addresses",
                    "disposition", "evidence")
KINDS = {"definition", "declaration", "arm", "row", "storage", "constant", "binding", "test",
         "assertion", "comment"}
DISPOSITIONS = {"deleted", "kept-live", "kept-contract", "edited"}
EDITABLE = {"test", "comment"}
PACKETS = {"1b", "1c", "1d", "1e", "1f", "1r"}
SOURCE = "Source/ElysiumUE"
SHAPE = "Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelShape.cpp"
TEST_NAME = re.compile(r'IMPLEMENT_(?:SIMPLE|COMPLEX)_AUTOMATION_TEST\(\s*\w+\s*,\s*"([^"]+)"')

DefinitionKey = tuple[str, str]   # (qualified symbol, normalised parameter list)


def read_deletions(path: Path) -> list[dict[str, str]]:
    rows = _read(path)
    for row in rows:
        if row["kind"] not in KINDS or row["disposition"] not in DISPOSITIONS or row["packet"] not in PACKETS:
            raise km.InvalidManifest(f"deletion row has an unknown kind/disposition/packet: {row}")
        if not row["file"] or not row["fingerprint"] or not row["evidence"]:
            raise km.InvalidManifest(f"deletion row needs file, fingerprint and evidence: {row}")
        if row["disposition"] == "edited" and row["kind"] not in EDITABLE:
            raise km.InvalidManifest(f"only tests and comments may be recorded as edited: {row}")
    keys = [(r["file"], r["kind"], r["fingerprint"]) for r in rows]
    if len(keys) != len(set(keys)):
        raise km.InvalidManifest("duplicate deletion row")
    return rows


def _read(path: Path) -> list[dict[str, str]]:
    lines = [line for line in path.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#")]
    header = tuple(lines[0].split("\t"))
    if header != DELETION_COLUMNS:
        raise km.InvalidManifest(f"{path.name}: columns differ: {header}")
    rows = []
    for number, line in enumerate(lines[1:], 2):
        cells = line.split("\t")
        if len(cells) != len(DELETION_COLUMNS):
            raise km.InvalidManifest(f"{path.name}:{number}: {len(cells)} cells")
        rows.append(dict(zip(DELETION_COLUMNS, cells)))
    return rows


def _space(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def parameter_list(signature: str) -> str:
    """The text between a signature's outermost parentheses, whitespace-normalised."""
    start = signature.find("(")
    if start < 0:
        raise km.InvalidManifest(f"no parameter list in {signature!r}")
    depth = 0
    for at in range(start, len(signature)):
        depth += {"(": 1, ")": -1}.get(signature[at], 0)
        if depth == 0:
            return _space(signature[start + 1:at])
    raise km.InvalidManifest(f"unbalanced parameter list in {signature!r}")


def definition_key(symbol: str, signature_or_fingerprint: str) -> DefinitionKey:
    return symbol.strip(), parameter_list(signature_or_fingerprint)


def defined(root: Path) -> dict[DefinitionKey, str]:
    """(symbol, parameters) -> repo path of every definition under the module, tests included."""
    found = {}
    for path in sorted((root / SOURCE).rglob("*")):
        if path.suffix in {".h", ".cpp", ".inl"}:
            relative = path.relative_to(root).as_posix()
            for definition in definitions(path.read_text(encoding="utf-8-sig"), relative):
                found[definition_key(definition.symbol, definition.signature)] = relative
    return found


def test_names(root: Path) -> dict[str, str]:
    names = {}
    for path in sorted((root / SOURCE).rglob("*.cpp")):
        for name in TEST_NAME.findall(path.read_text(encoding="utf-8-sig")):
            names[name] = path.relative_to(root).as_posix()
    return names


def _line_matches(line: str, fingerprint: str, prefix: bool) -> bool:
    line, wanted = _space(line), _space(fingerprint)
    if line == wanted:
        return True
    if not line.startswith(wanted):
        return False
    # A fingerprint may omit a trailing comment. As a PREFIX match (used only to prove a deleted
    # row absent) any extension counts, which can only fail closed.
    return prefix or line[len(wanted):].lstrip().startswith("//")


def _present(root: Path, row: dict[str, str], defs: dict[DefinitionKey, str], prefix: bool = False) -> bool:
    if row["kind"] == "definition":
        return definition_key(row["fingerprint"].partition("(")[0], row["fingerprint"]) in defs
    path = root / row["file"]
    if not path.is_file():
        return False
    text = path.read_text(encoding="utf-8-sig")
    if row["kind"] in {"test", "assertion", "comment"}:
        return row["fingerprint"] in text
    return any(_line_matches(line, row["fingerprint"], prefix) for line in text.splitlines())


def _comment_changed(before: Path, after: Path, row: dict[str, str]) -> bool:
    """An edited comment changed the lines that carried its fingerprint (an edit may extend one)."""
    old_text = (before / row["file"]).read_text(encoding="utf-8-sig")
    path = after / row["file"]
    new_text = path.read_text(encoding="utf-8-sig") if path.is_file() else ""
    if "\n" in row["fingerprint"]:
        return row["fingerprint"] not in new_text
    carrying = lambda text: sorted(_space(line) for line in text.splitlines() if row["fingerprint"] in line)
    return carrying(old_text) != carrying(new_text)


def check_rows(rows: list[dict[str, str]], before: Path, after: Path) -> dict[str, int]:
    before_defs, after_defs = defined(before), defined(after)
    counts = defaultdict(int)
    for row in rows:
        if not _present(before, row, before_defs):
            raise km.InvalidManifest(f"row names something the accepted step-0 tree does not hold: {row['fingerprint']}")
        disposition = row["disposition"]
        if disposition == "deleted" and _present(after, row, after_defs, prefix=True):
            raise km.InvalidManifest(f"deleted row is still present: {row['file']}: {row['fingerprint']}")
        if disposition in {"kept-live", "kept-contract"} and not _present(after, row, after_defs):
            raise km.InvalidManifest(f"kept row is gone: {row['file']}: {row['fingerprint']}")
        if disposition == "edited":
            if row["kind"] == "test" and not _present(after, row, after_defs):
                raise km.InvalidManifest(f"edited test no longer exists: {row['fingerprint']}")
            if row["kind"] == "comment" and not _comment_changed(before, after, row):
                raise km.InvalidManifest(f"edited comment still reads as it did: {row['file']}: {row['fingerprint'][:60]}")
        counts[f"{row['kind']}:{disposition}"] += 1
    return dict(sorted(counts.items()))


def check_complete(rows: list[dict[str, str]], before: Path, after: Path) -> dict[str, int]:
    """Every removed production definition and every removed test has a `deleted` row."""
    before_defs, after_defs = defined(before), defined(after)
    removed = {key for key, path in before_defs.items() if key not in after_defs and "/Tests/" not in path}
    recorded = {definition_key(r["fingerprint"].partition("(")[0], r["fingerprint"])
                for r in rows if r["kind"] == "definition" and r["disposition"] == "deleted"}
    missing = sorted(f"{symbol}({params})" for symbol, params in removed - recorded)
    if missing:
        raise km.InvalidManifest("definitions removed without a record row: " + ", ".join(missing))
    removed_tests = set(test_names(before)) - set(test_names(after))
    recorded_tests = {r["fingerprint"] for r in rows if r["kind"] == "test" and r["disposition"] == "deleted"}
    missing = sorted(removed_tests - recorded_tests)
    if missing:
        raise km.InvalidManifest("tests removed without a record row: " + ", ".join(missing))
    return {"removed_definitions": len(removed), "removed_tests": len(removed_tests)}


def check_live_definitions(inventory: dict, after: Path, exemptions: dict | None = None,
                           verdicts: dict[str, str] | None = None,
                           callers_of: Callable[[str], set[str]] | None = None) -> int:
    """No step-0 definition of a body outside the no-instance rows may disappear.

    Only step 1's own scope (`dead=class … no instance`, no live receiver) is unguarded. A body
    judged dead for another reason is still guarded, and the step-0 join attaches a definition to
    a body by its leading citation, so a removed dead helper can be joined to a live address it
    merely cites. Such a loss passes only through a reviewed exemption whose evidence is
    re-derived here:

    * `live_home`: the named symbol is still defined — the body lives there; or
    * `retail_callers`: the listed set EQUALS the ledger's callers of `address`, and every caller
      is a `dead` overlay row or carries a reviewed note in `caller_notes`."""
    exemptions = exemptions or {}
    verdicts = verdicts or {}
    after_defs = defined(after)
    after_symbols = {symbol for symbol, _ in after_defs}
    guarded = set()
    for body in inventory["bodies"]:
        if body.get("step1_scope") and not body["live_receivers"]:
            continue
        for definition in body["definitions"]:
            guarded.add(definition_key(definition["symbol"], definition["signature"]))
    lost = []
    for symbol, params in sorted(guarded - set(after_defs)):
        label = f"{symbol}({params})"
        exemption = exemptions.get(symbol)
        if exemption is None:
            lost.append(label)
        elif exemption.get("live_home"):
            if exemption["live_home"] not in after_symbols:
                lost.append(f"{label} (live home {exemption['live_home']} is gone)")
        else:
            listed = set(exemption.get("retail_callers", []))
            notes = exemption.get("caller_notes", {})
            actual = callers_of(exemption["address"]) if callers_of else None
            if actual is None or listed != actual:
                lost.append(f"{label} (listed callers {sorted(listed)} != ledger {sorted(actual or [])})")
            elif any(verdicts.get(caller) != "dead" and not notes.get(caller) for caller in listed):
                lost.append(f"{label} (a retail caller is neither a dead row nor explained)")
    if lost:
        raise km.InvalidManifest("step 1 deleted definitions of live bodies: " + ", ".join(lost))
    return len(guarded)


def overlay_verdicts(root: Path) -> dict[str, str]:
    rows = {}
    for line in (root / "research/tooling/ghidra/driver/kernel_verdicts.tsv").read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if len(cells) > 1 and re.fullmatch(r"[0-9a-f]{8}", cells[0]):
            rows[cells[0]] = cells[1]
    return rows


def check_overlay_targets(before: Path, after: Path) -> int:
    """No overlay target may name a port symbol the step-0 tree defined and the step removed."""
    then = {symbol for symbol, _ in defined(before)}
    now = {symbol for symbol, _ in defined(after)}
    stale = []
    for line in (after / "research/tooling/ghidra/driver/kernel_verdicts.tsv").read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if len(cells) > 3 and re.fullmatch(r"[0-9a-f]{8}", cells[0]):
            target = cells[3].removeprefix("hand:")
            if target in then and target not in now:
                stale.append(f"{cells[0]} {cells[3]}")
    if stale:
        raise km.InvalidManifest("overlay targets name removed port symbols: " + ", ".join(stale))
    return len(then - now)


def ledger_callers(root: Path) -> Callable[[str], set[str]]:
    """Direct retail callers from the kernel ledger, with thunks resolved to their target."""
    import kernel_ledger as kl
    ledger = kl.Ledger(kl.MODULE, kl.DEFAULT_DEPTH, root)
    ledger.load()
    incoming = defaultdict(set)
    for caller, edges in ledger.edges.items():
        for callee, _ in edges:
            incoming[ledger.resolve(callee)].add(ledger.resolve(caller))
    return lambda address: set(incoming.get(address, set()))


def check_census(classes: list[dict], after: Path) -> int:
    shape = (after / SHAPE).read_text(encoding="utf-8")
    dead = [row["retail_class"] for row in classes if row["liveness"] != "live"]
    missing = [name for name in dead if not re.search(r'\{\s*TEXT\("' + re.escape(name) + r'"\),', shape)]
    if missing:
        raise km.InvalidManifest("dead census classes lost their class row: " + ", ".join(missing))
    return len(dead)


def check_step1(directory: Path | None = None) -> dict:
    directory = directory or repo_root() / km.STORY
    manifest, classes, _ = km.load(directory)
    if manifest["phase"] != 1:
        raise km.InvalidManifest("step 1 is checked only while phase 1 is current")
    step0 = km.check_step0(directory)
    record = json.loads((directory / "acceptance-step1.json").read_text(encoding="utf-8"))
    if record.get("scope") != "step-1-dead-species-deletion":
        raise km.InvalidManifest("acceptance-step1.json does not record step 1")
    inventory_path = Path(json.loads((directory / "acceptance.json").read_text(encoding="utf-8"))
                          ["artifacts"]["inventory"]["path"])
    inventory = json.loads(inventory_path.read_text(encoding="utf-8-sig"))
    rows = read_deletions(directory / "deletions-step1.tsv")
    commit = manifest["history"]["step0"]["commit"]
    with tempfile.TemporaryDirectory(prefix="step1-") as scratch:
        before = km.historical_source(commit, Path(scratch))
        counts = check_rows(rows, before, repo_root())
        completeness = check_complete(rows, before, repo_root())
        check_overlay_targets(before, repo_root())
    decisions = json.loads((directory / "decisions-step1.json").read_text(encoding="utf-8"))
    guarded = check_live_definitions(inventory, repo_root(), decisions.get("guard_exemptions", {}),
                                     overlay_verdicts(repo_root()), ledger_callers(repo_root()))
    dead_classes = check_census(classes, repo_root())
    artifacts = {}
    for name in ("inventory", "delta", "runtime", "cheap_checks"):
        ref = record["artifacts"][name]
        path = Path(ref["path"])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref["sha256"]:
            raise km.InvalidManifest(f"step-1 evidence changed/missing: {name}")
        artifacts[name] = json.loads(path.read_text(encoding="utf-8-sig"))
    # The step-1 inventory is regenerated on the current tree; `check_inventory` holds its source
    # hashes to that tree, and its rule identity must equal the accepted step-0 identity: deleting
    # dead port code removes no live retail rule contract.
    current = check_inventory(artifacts["inventory"], repo_root())
    if current["rule_identity_sha256"] != step0["rule_identity_sha256"]:
        raise km.InvalidManifest("live rule inventory identity changed at step 1")
    delta = artifacts["delta"]
    if not delta.get("comparison_passed") or delta.get("differences"):
        raise km.InvalidManifest("step-1 regression comparison has unmatched differences")
    expectations = json.loads((directory / "expectations/step-1.json").read_text(encoding="utf-8"))
    unused = {c["id"] for c in expectations["changes"]} - set(delta.get("applied_expectations", []))
    if unused:
        raise km.InvalidManifest("unconsumed step-1 expectations: " + ", ".join(sorted(unused)))
    if len(artifacts["runtime"]) != 4 or any(r["exit"] for r in artifacts["runtime"]):
        raise km.InvalidManifest("step-1 runtime gate incomplete/failing")
    if not artifacts["cheap_checks"] or any(r["exit"] for r in artifacts["cheap_checks"]):
        raise km.InvalidManifest("step-1 cheap/Python gates incomplete/failing")
    return {"rows": counts, "completeness": completeness, "guarded_live_definitions": guarded,
            "dead_census_classes": dead_classes,
            "rule_identity_sha256": current["rule_identity_sha256"], "step0": step0}
