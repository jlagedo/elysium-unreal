"""Story 5's shared execution-manifest reader and fail-closed preflight checks.

The manifest joins the existing oracle; it is not another retail ledger. Only factory identity
is review-complete at this checkpoint. The whole step-0 gate refuses pending packets.

    uv run elysium research kernel_migration --check factories
    uv run elysium research kernel_migration --check step0
    uv run elysium research kernel_migration --check step1

Once a later phase is current, `--check step0` verifies the accepted receipt against the tree it
accepted (`manifest.json` `history.step0.commit`), not against the edited working tree.
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import re
import subprocess
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path

from elysium_pipeline.paths import repo_root, research_root, vtmb_root

STORY = Path("docs/specs/0019-npc-kernel-rework/story-5")
CLASS_COLUMNS = ("retail_class", "retail_base", "vtable", "port_class", "port_base", "step",
                 "liveness", "evidence")
FACTORY_COLUMNS = ("classname", "retail_class", "factory", "constructor", "constructor_thunks",
                   "allocation_size", "final_vtable", "final_write", "form",
                   "factory_listing_sha256", "constructor_listing_sha256", "current_registry",
                   "prior_census_class")
REQUIRED_PACKETS = {"factories", "field-body-identity", "calls-fixtures-compatibility",
                    "regression-comparison", "composable-codemods", "representative-rehearsals"}
REGISTRY_DISPOSITIONS = {"shared-npc", "controller-leaf", "maker-leaf", "sequence-leaf",
                         "schedule-leaf", "stub-base-entity", "unregistered-base-fallback"}
REQUIRED_PINS = {"module", "corpus", "listing", "datamaps", "census", "verdicts", "registry",
                 "stub_registry", "sequence_registry", "schedule_registry"}


# A phase is listed here only when its checker exists; no later phase is implicitly accepted.
ACCEPTED_PHASES = (0, 1)


class InvalidManifest(ValueError):
    pass


@dataclass(frozen=True)
class FieldIdentity:
    module: str
    declaring_class: str
    offset: int
    member: str

    def __post_init__(self):
        if not self.module or not self.declaring_class or not self.member or self.offset < 0:
            raise InvalidManifest("field identity needs module, declaring class, offset and member")


@dataclass(frozen=True)
class BodyIdentity:
    module: str
    address: str
    introducing_class: str
    slot: int | None
    signature: str
    receiver_class: str

    def __post_init__(self):
        if (not re.fullmatch(r"[0-9a-f]{8}", self.address) or not self.module
                or not self.introducing_class or not self.signature or not self.receiver_class
                or (self.slot is not None and self.slot < 0)):
            raise InvalidManifest("body identity needs module/address, family, signature and receiver")


def read_table(path: Path, columns: tuple[str, ...], identity: str) -> list[dict[str, str]]:
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        if tuple(reader.fieldnames or []) != columns:
            raise InvalidManifest(f"{path.name}: unexpected columns")
        rows = list(reader)
    seen = set()
    for row in rows:
        key = (row.get(identity) or "").casefold()
        if None in row or any(value is None for value in row.values()) or not key or key in seen:
            raise InvalidManifest(f"{path.name}: malformed or duplicate identity {key!r}")
        seen.add(key)
    return rows


def validate(classes: list[dict], factories: list[dict]) -> None:
    by_class = {row["retail_class"]: row for row in classes}
    if len(by_class) != len(classes):
        raise InvalidManifest("duplicate class identity")
    live = [r for r in classes if r["liveness"] == "live"]
    dead = [r for r in classes if r["liveness"] == "dead-census-retained"]
    if (len(live), len(dead), len(factories)) != (56, 21, 74):
        raise InvalidManifest("expected 56 live, 21 retained dead classes and 74 factories")
    ports = {r["port_class"] for r in live}
    if len(ports) != len(live) or "-" in ports:
        raise InvalidManifest("duplicate or missing live port owner")
    for row in classes:
        if not re.fullmatch(r"[0-9a-f]{8}", row["vtable"]) or not row["evidence"]:
            raise InvalidManifest(f"{row['retail_class']}: missing vtable/evidence")
        if row in dead:
            if row["port_class"] != "-" or row["port_base"] != "-" or row["step"] != "1":
                raise InvalidManifest("dead census row may not create a port class")
        elif row["port_base"] not in ports | {"FElysiumScriptedCharacter"}:
            raise InvalidManifest(f"{row['retail_class']}: unknown final port base")
        if row["step"] not in {"1", "2", "5", "7", "8", "9", "10"}:
            raise InvalidManifest("invalid introduction/fold step")
    by_port = {r["port_class"]: r for r in live}
    for port in by_port:
        seen = set()
        while port in by_port:
            if port in seen:
                raise InvalidManifest("cyclic final class tree")
            seen.add(port)
            port = by_port[port]["port_base"]
    aliases, addresses = set(), set()
    for row in factories:
        name = row["classname"].casefold()
        if name in aliases or row["factory"] in addresses:
            raise InvalidManifest("ambiguous factory/alias mapping")
        aliases.add(name)
        addresses.add(row["factory"])
        owner = by_class.get(row["retail_class"])
        if owner is None or owner["vtable"] != row["final_vtable"]:
            raise InvalidManifest(f"{name}: unknown or conflicting final vtable owner")
        if row["current_registry"] not in REGISTRY_DISPOSITIONS:
            raise InvalidManifest(f"{name}: unknown current registry disposition")
        for key in ("factory", "constructor", "final_vtable", "final_write"):
            if not re.fullmatch(r"[0-9a-f]{8}", row[key]):
                raise InvalidManifest(f"{name}: invalid module address {key}")
        for key in ("factory_listing_sha256", "constructor_listing_sha256"):
            if not re.fullmatch(r"[0-9a-f]{64}", row[key]):
                raise InvalidManifest(f"{name}: unpinned {key}")
        if row["form"] not in {"inline-final-write", "constructor-final-write"}:
            raise InvalidManifest(f"{name}: unresolved construction form")


def load(directory: Path | None = None) -> tuple[dict, list[dict], list[dict]]:
    directory = directory or repo_root() / STORY
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8-sig"))
    if manifest.get("schema_version") != 1 or manifest.get("phase") not in ACCEPTED_PHASES:
        raise InvalidManifest("unsupported manifest version/phase; no future phase is implicitly accepted")
    if set(manifest.get("packets", {})) != REQUIRED_PACKETS:
        raise InvalidManifest("missing or unknown step-0 packet")
    if set(manifest.get("inputs", {})) != REQUIRED_PINS:
        raise InvalidManifest("missing or unknown input pin")
    classes = read_table(directory / "classes.tsv", CLASS_COLUMNS, "retail_class")
    factories = read_table(directory / "factories.tsv", FACTORY_COLUMNS, "classname")
    validate(classes, factories)
    deferred = {r["retail_class"] for r in classes if r["step"] in {"7", "8", "9", "10"}}
    if len(manifest.get("deferred_classes", [])) != 10 or set(manifest["deferred_classes"]) != deferred:
        raise InvalidManifest("deferred class list differs from the reviewed fold steps")
    return manifest, classes, factories


def check_pins(manifest: dict) -> None:
    from kernel_factory_map import digest
    roots = {"repo": repo_root(), "research": research_root(), "retail": vtmb_root()}
    for key, pin in manifest["inputs"].items():
        root = roots[pin["root"]].resolve()
        path = (root / pin["path"]).resolve()
        if not path.is_relative_to(root) or not path.is_file() or digest(path) != pin["sha256"]:
            raise InvalidManifest(f"input pin changed: {key}; review before refreshing it")


def factory_projection(observation: dict) -> dict:
    return {"retail_class": observation["final"]["class"], "factory": observation["factory"],
            "constructor": observation["constructor"]["address"],
            "constructor_thunks": ",".join(observation["constructor"]["thunks"]),
            "allocation_size": observation["allocation"]["size"],
            "final_vtable": observation["final"]["vtable"],
            "final_write": observation["final"]["at"],
            "form": "inline-final-write" if observation["inline_writes"] else "constructor-final-write",
            "factory_listing_sha256": observation["factory_listing_sha256"],
            "constructor_listing_sha256": observation["constructor"]["listing_sha256"]}


def check_observations(rows: list[dict], observations: dict) -> None:
    expected = {row["classname"]: row for row in rows}
    if set(expected) != set(observations):
        raise InvalidManifest(f"factory coverage differs: {sorted(set(expected) ^ set(observations))}")
    for name, answers in observations.items():
        if len(answers) != 1:
            raise InvalidManifest(f"{name}: unresolved/multiple factory answers")
        for key, actual in factory_projection(answers[0]).items():
            if expected[name][key] != actual:
                raise InvalidManifest(f"{name}: reviewed {key} differs: {expected[name][key]} != {actual}")


def check_factories(manifest: dict, rows: list[dict]) -> None:
    from kernel_factory_map import Corpus, survey
    check_pins(manifest)
    corpus = Corpus()
    try:
        check_observations(rows, survey(corpus)["observations"])
    finally:
        corpus.db.close()
        corpus.listing.close()


def historical_source(commit: str, destination: Path) -> Path:
    """Materialize `Source/ElysiumUE` exactly as `commit` holds it, for an accepted phase's receipt.

    A later phase edits the source the step-0 inventory hashed; the receipt stays checkable against
    the tree it accepted instead of being refreshed to hide the edit."""
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise InvalidManifest(f"historical commit must be a full hash: {commit!r}")
    archive = subprocess.run(["git", "-C", str(repo_root()), "archive", "--format=tar", commit,
                              "Source/ElysiumUE"], capture_output=True, check=False)
    if archive.returncode != 0:
        raise InvalidManifest(f"cannot read accepted tree {commit}: {archive.stderr.decode(errors='replace').strip()}")
    with tarfile.open(fileobj=io.BytesIO(archive.stdout)) as tar:
        tar.extractall(destination, filter="data")
    return destination


def check_step0(directory: Path | None = None) -> dict:
    """Accept receipts and real source checks, never status strings alone.

    At phase 0 the inventory is checked against the working tree. Once a later phase is current,
    the manifest's `history.step0.commit` names the accepted tree and the check reads that."""
    directory = directory or repo_root() / STORY
    manifest, _, _ = load(directory)
    if manifest["phase"] == 0:
        return _check_step0(directory, manifest, repo_root())
    commit = manifest.get("history", {}).get("step0", {}).get("commit", "")
    with tempfile.TemporaryDirectory(prefix="step0-") as scratch:
        return _check_step0(directory, manifest, historical_source(commit, Path(scratch)))


def _check_step0(directory: Path, manifest: dict, source_root: Path) -> dict:
    from kernel_migration_audit import check_inventory, maker_shadows, file_sha
    record = json.loads((directory / 'acceptance.json').read_text(encoding='utf-8-sig'))
    if record.get('scope') != 'step-0-only' or record.get('production_migrations') != []:
        raise InvalidManifest('step-0 receipt may not authorize a production migration')
    for relative, digest in record.get('authored_sha256', {}).items():
        path = directory / relative
        if not path.is_file() or file_sha(path) != digest:
            raise InvalidManifest(f'authored migration decisions changed: {relative}')
    if set(record.get('authored_sha256', {})) != {'decisions.json','field-decisions.tsv'}:
        raise InvalidManifest('acceptance must pin the authored decisions')
    artifacts = {}
    for name, ref in record['artifacts'].items():
        path = Path(ref['path'])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref['sha256']:
            raise InvalidManifest(f'acceptance evidence changed/missing: {name}')
        artifacts[name] = path
    required = {'inventory','inventory_audit','source_replay_audit','rehearsal_build',
                'rehearsal_probes','rehearsal_focused','integration_runtime','delta','cheap_checks'}
    if not required <= set(artifacts):
        raise InvalidManifest('missing required acceptance artifact')
    read = lambda name: json.loads(artifacts[name].read_text(encoding='utf-8-sig'))
    counts = check_inventory(read('inventory'), source_root)
    if counts != record['inventory_counts']:
        raise InvalidManifest('inventory coverage/residue differs from reviewed acceptance')
    decisions = json.loads((directory / 'decisions.json').read_text(encoding='utf-8'))
    if maker_shadows(source_root) != sorted(decisions['field_policy']['maker_duplicates']):
        raise InvalidManifest('maker boundary changed')
    body_map = {row['address']: row for row in read('inventory')['bodies']}
    for address, decision in decisions['body_resolutions'].items():
        row = body_map[address]
        if row['actual_symbol'] != decision['current_symbol'] or len(row['definitions']) != 1:
            raise InvalidManifest(f'rehearsed source identity is ambiguous: {address}')
    field_rows = read_table(directory / 'field-decisions.tsv',
        ('module','declaring_class','offset','member','type','width','flags','current_path','final_path','disposition','step'),
        # Identity is checked below; a member may have distinct declaring classes.
        'final_path')
    keys = {(r['module'],r['declaring_class'],r['offset'],r['member']) for r in field_rows}
    if len(keys) != len(field_rows) or len(field_rows) != 67:
        raise InvalidManifest('field disposition coverage/identity changed')
    replay_data = json.loads((research_root() / 'ghidra/types/datamap_records-vampire.dll.json').read_text(encoding='utf-8'))
    for row in field_rows:
        if row['flags'] == 'no-datamap':
            continue
        matches = [r for r in replay_data.get(row['declaring_class'], {}).get('records', [])
                   if r['name'] == row['member'] and r['offset'] == int(row['offset'],16)]
        if len(matches) != 1 or matches[0]['flags'] != int(row['flags']):
            raise InvalidManifest('field decision lost its class-qualified datamap flags')
    if read('rehearsal_build')['exit'] != 0:
        raise InvalidManifest('representative build failed')
    probes = read('rehearsal_probes')
    if probes.get('failed') or len(probes.get('tests', [])) != 6 or any(t['state'] != 'Success' for t in probes['tests']):
        raise InvalidManifest('representative observable probes are incomplete or failing')
    if len(read('rehearsal_focused')) != 4 or any(r['exit'] for r in read('rehearsal_focused')):
        raise InvalidManifest('focused source-fixture gates incomplete/failing')
    runtime = read('integration_runtime')
    if len(runtime) != 4 or any(r['exit'] for r in runtime):
        raise InvalidManifest('integration runtime gate incomplete/failing')
    delta = read('delta')
    if not delta.get('comparison_passed') or delta['differences']:
        raise InvalidManifest('unmatched regression differences')
    checks = read('cheap_checks')
    if len(checks) != 6 or any(r['exit'] for r in checks):
        raise InvalidManifest('cheap/Python gates incomplete/failing')
    replay = read('source_replay_audit')['replay']
    if len(replay['stages']) != 7 or not all(s['reapplication_refused'] for s in replay['stages']):
        raise InvalidManifest('source operations did not compose or failed reapplication checks')
    for relative, digest in replay['files'].items():
        path = Path(record['rehearsal_checkout']) / relative
        if not path.is_file() or __import__('kernel_migration_inventory').sha(path.read_text(encoding='utf-8-sig')) != digest:
            raise InvalidManifest(f'compiled rehearsal source differs from replay: {relative}')
    pending = [name for name, status in manifest['packets'].items() if status != 'accepted']
    if pending:
        raise InvalidManifest('unaccepted step-0 work: ' + ', '.join(pending))
    return counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", choices=("factories", "step0", "step1"), required=True)
    args = parser.parse_args(argv)
    try:
        manifest, classes, factories = load()
        check_factories(manifest, factories)
        print(f"Factory identity: {len(factories)} reviewed mappings; "
              f"{sum(r['liveness'] == 'live' for r in classes)} live / "
              f"{sum(r['liveness'] != 'live' for r in classes)} retained dead classes. "
              "Implemented behaviour is not counted by these identities.")
        if args.check == "step0":
            counts = check_step0()
            print('PASS: step 0 acceptance; inventory, source replay, representative build, focused tests and integration gate verified.')
            print(json.dumps(counts, sort_keys=True))
        if args.check == "step1":
            from kernel_migration_step1 import check_step1
            counts = check_step1()
            print('PASS: step 1 acceptance; accepted step-0 receipt, deletion record, live-definition '
                  'guard, dead census, rule identity, regression comparison and runtime gate verified.')
            print(json.dumps(counts, sort_keys=True))
    except (InvalidManifest, ValueError, OSError) as exc:
        print(f"REFUSED: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
