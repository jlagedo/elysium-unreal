"""Story 5 step 1: the dead species subset is gone and nothing live went with it.

`deletions-step1.tsv` is the authored record of every definition, arm, row, storage member and
test the step removed, kept or edited. This check holds the current tree to that record and the
step-0 inventory to three invariants it cannot express row by row:

* every removed row existed in the accepted step-0 tree and is absent now, every kept row is
  still present;
* no definition that implements a live or non-dead retail body at step 0 was deleted, whatever
  the record says;
* the dead classes stay in the census and the live rule inventory keeps its identity.

    uv run elysium research kernel_migration --check step1
"""
from __future__ import annotations

import json
import re
import tempfile
from collections import defaultdict
from pathlib import Path

import kernel_migration as km
from kernel_migration_audit import check_inventory, file_sha
from kernel_migration_inventory import definitions
from elysium_pipeline.paths import repo_root

DELETION_COLUMNS = ("packet", "kind", "file", "symbol", "fingerprint", "retail_addresses",
                    "disposition", "evidence")
KINDS = {"definition", "declaration", "arm", "row", "storage", "constant", "binding", "test",
         "assertion", "comment"}
DISPOSITIONS = {"deleted", "kept-live", "kept-contract", "edited"}
PACKETS = {"1b", "1c", "1d", "1e", "1f"}
SOURCE = "Source/ElysiumUE"
SHAPE = "Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelShape.cpp"


def read_deletions(path: Path) -> list[dict[str, str]]:
    rows = _read(path)
    for row in rows:
        if row["kind"] not in KINDS or row["disposition"] not in DISPOSITIONS or row["packet"] not in PACKETS:
            raise km.InvalidManifest(f"deletion row has an unknown kind/disposition/packet: {row}")
        if not row["file"] or not row["fingerprint"] or not row["evidence"]:
            raise km.InvalidManifest(f"deletion row needs file, fingerprint and evidence: {row}")
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


def defined_symbols(root: Path) -> dict[str, list[str]]:
    """Qualified symbol -> signatures defined anywhere under the module, tests included."""
    found = defaultdict(list)
    for path in sorted((root / SOURCE).rglob("*")):
        if path.suffix in {".h", ".cpp", ".inl"}:
            for definition in definitions(path.read_text(encoding="utf-8-sig"), path.relative_to(root).as_posix()):
                found[definition.symbol].append(definition.signature)
    return found


def _present(root: Path, row: dict[str, str], symbols: dict[str, list[str]]) -> bool:
    if row["kind"] == "definition":
        symbol, _, params = row["fingerprint"].partition("(")
        params = re.sub(r"\s+", " ", params.rstrip(")").strip())
        return any(re.sub(r"\s+", " ", sig.split("(", 1)[1]).startswith(params) if params else True
                   for sig in symbols.get(symbol.strip(), []))
    path = root / row["file"]
    if not path.is_file():
        return False
    text = path.read_text(encoding="utf-8-sig")
    if row["kind"] in {"test", "assertion", "comment"}:
        return row["fingerprint"] in text
    # Masking literals would hide string-keyed table rows, so the raw line is compared, normalised
    # for whitespace. A fingerprint may omit a trailing comment, so it is a line PREFIX; a collision
    # can only report a deleted row as still present, which fails closed.
    wanted = re.sub(r"\s+", " ", row["fingerprint"]).strip()
    return any(re.sub(r"\s+", " ", line).strip().startswith(wanted) for line in text.splitlines())


def check_rows(rows: list[dict[str, str]], before: Path, after: Path) -> dict[str, int]:
    before_symbols, after_symbols = defined_symbols(before), defined_symbols(after)
    counts = defaultdict(int)
    for row in rows:
        existed = _present(before, row, before_symbols)
        exists = _present(after, row, after_symbols)
        if row["disposition"] in {"deleted", "edited", "kept-live", "kept-contract"} and not existed:
            raise km.InvalidManifest(f"row names something the accepted step-0 tree does not hold: {row['fingerprint']}")
        if row["disposition"] == "deleted" and exists:
            raise km.InvalidManifest(f"deleted row is still present: {row['file']}: {row['fingerprint']}")
        if row["disposition"] in {"kept-live", "kept-contract"} and not exists:
            raise km.InvalidManifest(f"kept row is gone: {row['file']}: {row['fingerprint']}")
        counts[f"{row['kind']}:{row['disposition']}"] += 1
    return dict(sorted(counts.items()))


def check_live_definitions(inventory: dict, after: Path, exemptions: dict | None = None,
                           verdicts: dict[str, str] | None = None) -> int:
    """No step-0 definition of a live, non-dead or live-receiver body may disappear.

    The step-0 join attaches a definition to a body by its leading citation, so a deleted dead
    helper can be joined to a live address it merely cites. Such a loss passes only through a
    reviewed exemption whose evidence the check re-verifies: either the named `live_home` is still
    defined, or every listed `retail_callers` address is a `dead` overlay row."""
    exemptions = exemptions or {}
    verdicts = verdicts or {}
    symbols = defined_symbols(after)
    guarded = set()
    for body in inventory["bodies"]:
        if body["verdict"] == "dead" and not body["live_receivers"]:
            continue
        for definition in body["definitions"]:
            guarded.add(definition["symbol"])
    lost = []
    for symbol in sorted(guarded - set(symbols)):
        exemption = exemptions.get(symbol)
        if exemption is None:
            lost.append(symbol)
        elif exemption.get("live_home"):
            if exemption["live_home"] not in symbols:
                lost.append(f"{symbol} (live home {exemption['live_home']} is gone)")
        elif not exemption.get("retail_callers") or any(
                verdicts.get(caller) != "dead" for caller in exemption["retail_callers"]):
            lost.append(f"{symbol} (a listed retail caller is not a dead row)")
    if lost:
        raise km.InvalidManifest("step 1 deleted definitions of live bodies: " + ", ".join(lost))
    return len(guarded)


def overlay_verdicts(root: Path) -> dict[str, str]:
    rows = {}
    for line in (root / "research/tooling/ghidra/driver/kernel_verdicts.tsv").read_text(encoding="utf-8").splitlines():
        cells = line.split("	")
        if len(cells) > 1 and re.fullmatch(r"[0-9a-f]{8}", cells[0]):
            rows[cells[0]] = cells[1]
    return rows


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
    decisions = json.loads((directory / "decisions-step1.json").read_text(encoding="utf-8"))
    guarded = check_live_definitions(inventory, repo_root(), decisions.get("guard_exemptions", {}),
                                     overlay_verdicts(repo_root()))
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
    return {"rows": counts, "guarded_live_definitions": guarded, "dead_census_classes": dead_classes,
            "rule_identity_sha256": current["rule_identity_sha256"], "step0": step0}
