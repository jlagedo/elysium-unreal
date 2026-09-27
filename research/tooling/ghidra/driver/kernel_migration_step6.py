"""Story 5 step 6: every generated slot body stands on the port class of its retail owner.

Step 5 left the generated slot surface split by NPC layer: every slot `CAI_BaseNPC`'s table holds was
declared on `FElysiumNpcBase`, the entity chain's bodies with it. This check holds the tree to the
step's authored records:

* `slots-step6.tsv` is the slot record: one row per generated slot row of the accepted step-5 tree
  (`ElysiumNpcBaseSlots.*` / `ElysiumNpcSlots.*`) and one per chain row step 6 adds (a chain class's
  own body at a slot the NPC refills). Its identity columns -- slot, port name, signature, current
  owner, the retail class whose body the row is, the body address, the emission kind, the verdict
  and the introducing class -- are re-derived from that tree, the pinned verdict overlay and the
  pinned corpus's primary vtables, so the record cannot drop a row. Each row carries a reviewed
  final owner and disposition: `keep`, `move`, `move-hand`, `new-chain-row`, `adapter:<member>`,
  `seam:<member>`, `implemented:<callable>` or `delete-dead`;
* `moves-step6.tsv` is the member record: the members the chain-owned hand bodies reach, each with
  the port class it moves to;
* at phase 6, every row stands where its record says (declared in its owner's generated slot file,
  gone from the NPC layer's), every moved member is declared in its owner's scope and not on
  `FElysiumNpcBase`, a deleted stub is gone and its overlay target is `-`, the static dispatch gate
  equals the re-keyed `surviving_sites`, the retired tables are gone, the live unported-rule set
  equals its pin, the step-5 checks still hold on their accepted tree, and the step-6 regression
  comparison, expectations and runtime receipts are pinned in `acceptance-step6.json`.

While phase 5 is current the step is in progress: `--check step6` validates the records against the
accepted step-5 tree and the pinned corpus and refuses nothing about the source.

    uv run elysium research kernel_migration --check step6
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
from kernel_migration_inventory import definitions, mask_cpp
from kernel_migration_step4 import _top_level, METHOD, FIELD
from elysium_pipeline.paths import repo_root

SOURCE = Path("Source/ElysiumUE")
PUBLIC = SOURCE / "Public"
PRIVATE = SOURCE / "Private"
SUBSTRATE = PRIVATE / "Substrate"
VERDICTS = Path("research/tooling/ghidra/driver/kernel_verdicts.tsv")
LEDGER = Path("docs/vtmb/npc-kernel")
HISTORICAL_PATHS = ("Source/ElysiumUE", str(VERDICTS).replace("\\", "/"), str(LEDGER).replace("\\", "/"))

# The retail chain under the Troika leaf and the port class that stands for each node. The port has
# no `CBaseToggle` node: its bodies stand on `FElysiumAnimating`, the nearest port class below it.
CHAIN_PORT = {
    "CBaseEntity": "FElysiumEntity",
    "CBaseToggle": "FElysiumAnimating",
    "CBaseAnimating": "FElysiumAnimating",
    "CBaseAnimatingOverlay": "FElysiumAnimatingOverlay",
    "CBaseFlex": "FElysiumFlex",
    "CBaseCombatCharacter": "FElysiumCombatCharacter",
    "CAI_BaseNPC": "FElysiumNpcBase",
    "CAI_BaseNPCTroika": "FElysiumNpc",
}
PORT_ORDER = ("FElysiumEntity", "FElysiumAnimating", "FElysiumAnimatingOverlay", "FElysiumFlex",
              "FElysiumCombatCharacter", "FElysiumNpcBase", "FElysiumNpc")
BASE_TABLE_SLOTS = 583
# Each port class's generated slot surface at phase 6: the declarations (`.inl`, included inside the
# class body) and their generated definitions.
SLOT_FILES = {
    "FElysiumEntity": (PUBLIC / "ElysiumEntitySlots.inl", SUBSTRATE / "ElysiumEntitySlots.cpp"),
    "FElysiumAnimating": (PUBLIC / "ElysiumAnimatingSlots.inl", SUBSTRATE / "ElysiumAnimatingSlots.cpp"),
    "FElysiumAnimatingOverlay": (PUBLIC / "ElysiumAnimatingOverlaySlots.inl",
                                 SUBSTRATE / "ElysiumAnimatingOverlaySlots.cpp"),
    "FElysiumFlex": (PUBLIC / "ElysiumFlexSlots.inl", SUBSTRATE / "ElysiumFlexSlots.cpp"),
    "FElysiumCombatCharacter": (PUBLIC / "ElysiumCombatCharacterSlots.inl",
                                SUBSTRATE / "ElysiumCombatCharacterSlots.cpp"),
    "FElysiumNpcBase": (SUBSTRATE / "ElysiumNpcBaseSlots.inl", SUBSTRATE / "ElysiumNpcBaseSlots.cpp"),
    "FElysiumNpc": (SUBSTRATE / "ElysiumNpcSlots.inl", SUBSTRATE / "ElysiumNpcSlots.cpp"),
}
# Where each port class's own body is declared, for the member record: its own header first, then
# `ElysiumPlayer.h`, which declared the three middle chain classes in the accepted step-6 tree
# (`7d63e7fa`) before packet 6r gave each its own header.
CLASS_HEADERS = {
    "FElysiumEntity": (PUBLIC / "ElysiumEntity.h",),
    "FElysiumAnimating": (PUBLIC / "ElysiumAnimating.h", PUBLIC / "ElysiumPlayer.h"),
    "FElysiumAnimatingOverlay": (PUBLIC / "ElysiumAnimatingOverlay.h", PUBLIC / "ElysiumPlayer.h"),
    "FElysiumFlex": (PUBLIC / "ElysiumFlex.h", PUBLIC / "ElysiumPlayer.h"),
    "FElysiumCombatCharacter": (PUBLIC / "ElysiumPlayer.h",),
    "FElysiumNpcBase": (SUBSTRATE / "ElysiumNpcBase.h",),
    "FElysiumNpc": (SUBSTRATE / "ElysiumNpc.h",),
}

SLOT_COLUMNS = ("slot", "port_name", "signature", "current_owner", "row_class", "body", "kind",
                "verdict", "introducer", "final_owner", "disposition", "packet", "note")
SLOT_IDENTITY = SLOT_COLUMNS[:9]
SLOT_DISPOSITION = re.compile(r"^(keep|move|move-hand|new-chain-row|adapter:\w+|seam:\w+|"
                              r"implemented:[\w:]+|delete-dead|transitional:.+|investigate)$")
MOVE_COLUMNS = ("member", "kind", "current_owner", "declared_in", "defined_in", "final_owner",
                "disposition", "packet", "note")
MOVE_DISPOSITION = re.compile(r"^(move|stay:.+|investigate)$")
PACKETS = {"6a", "6b", "6c", "6d", "6e", "6f", "6g", "6h", "6i", "6r", "-"}  # 6r: the step-6 review follow-up

SLOT_HEAD = re.compile(r"^\s*// slot (\d+)\s+(0x[0-9a-f]+|no body)\s+\(")


# ---- the accepted step-5 surface ----------------------------------------------------------------

def _decls(inl: str) -> list[tuple[int, str, str, bool]]:
    """(slot, body, signature, override) per declaration of one generated `.inl`."""
    out, head = [], None
    for line in inl.splitlines():
        match = SLOT_HEAD.match(line)
        if match:
            head = (int(match.group(1)), match.group(2))
            continue
        stripped = line.strip()
        if head and stripped and not stripped.startswith("//"):
            override = stripped.endswith(" override;")
            sig = stripped.removeprefix("virtual ").removesuffix(";").removesuffix(" override").strip()
            out.append((head[0], head[1], sig, override))
            head = None
    return out


def _kinds(cpp: str) -> dict[tuple[int, str], str]:
    """(slot, body) -> stub / default / hand, read off one generated `.cpp`."""
    kinds, head, block = {}, None, []
    for line in cpp.splitlines() + ["// slot 99999 0x0 ("]:
        match = SLOT_HEAD.match(line)
        if match:
            if head:
                text = " ".join(line.strip().removeprefix("//").strip() for line in block)
                kinds[head] = ("hand" if "Declared here, defined there." in text else
                               "default" if "retail's whole body is" in text else "stub")
            head, block = (int(match.group(1)), match.group(2)), []
            continue
        if head:
            block.append(line)
    return kinds


def _verdicts(tree: Path) -> dict[str, str]:
    out = {}
    for line in (tree / VERDICTS).read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if len(cells) >= 2 and re.fullmatch(r"[0-9a-f]{8}", cells[0]):
            out[cells[0]] = cells[1]
    return out


def chain_tables() -> tuple[list[str], dict[str, dict[int, str]]]:
    """The retail chain (base first) and each node's primary vtable, thunk-resolved, from the pinned
    corpus."""
    import kernel_ledger as kl
    ledger = kl.Ledger(kl.MODULE, kl.DEFAULT_DEPTH, repo_root())
    ledger._load_functions()
    ledger._load_thunks()
    chain = list(CHAIN_PORT)
    tables = {}
    for cls in chain:
        rows = ledger.db.execute(
            "SELECT slot, func FROM vtables WHERE module = ? AND sub = 0 AND cls = ?", (ledger.module, cls))
        tables[cls] = {row["slot"]: ledger.resolve(row["func"]) for row in rows}
        if not tables[cls]:
            raise km.InvalidManifest(f"the pinned corpus has no primary vtable for {cls}")
    return chain, tables


def slot_manifest(tree: Path) -> list[dict]:
    """The identity columns of slots-step6.tsv, re-derived from an accepted step-5 tree."""
    chain, tables = chain_tables()
    verdicts = _verdicts(tree)
    rows = []
    for owner in ("FElysiumNpcBase", "FElysiumNpc"):
        inl_path, cpp_path = SLOT_FILES[owner]
        decls = _decls((tree / inl_path).read_text(encoding="utf-8-sig"))
        kinds = _kinds((tree / cpp_path).read_text(encoding="utf-8-sig"))
        for slot, body, sig, _ in decls:
            holders = [c for c in chain if slot in tables[c]]
            address = body.removeprefix("0x")
            if owner == "FElysiumNpcBase":
                row_class = next((c for c in holders if tables[c][slot] == address), "?")
            else:
                row_class = "CAI_BaseNPCTroika"
            rows.append({"slot": str(slot), "port_name": sig.split("(")[0].split()[-1], "signature": sig,
                         "current_owner": owner, "row_class": row_class, "body": body,
                         "kind": kinds.get((slot, body), "?"), "verdict": verdicts.get(address, "-"),
                         "introducer": holders[0] if holders else "CAI_BaseNPCTroika"})
    # The chain rows step 6 adds: a chain class's own body at a slot whose base row is another body.
    base_rows = {int(r["slot"]): r for r in rows if r["current_owner"] == "FElysiumNpcBase"}
    for slot, base in sorted(base_rows.items()):
        if slot >= BASE_TABLE_SLOTS:
            continue
        holders = [c for c in chain if slot in tables[c]]
        by_port: dict[str, tuple[str, str]] = {}
        previous = None
        for cls in holders:
            body = tables[cls][slot]
            if body != previous and CHAIN_PORT[cls] not in ("FElysiumNpcBase", "FElysiumNpc"):
                by_port[CHAIN_PORT[cls]] = (cls, body)
            previous = body
        for port, (cls, body) in by_port.items():
            if f"0x{body}" == base["body"] and CHAIN_PORT[base["row_class"]] == port:
                continue
            rows.append({"slot": str(slot), "port_name": base["port_name"], "signature": base["signature"],
                         "current_owner": "-", "row_class": cls, "body": f"0x{body}", "kind": "new",
                         "verdict": "-", "introducer": holders[0]})
    return rows


# ---- records -------------------------------------------------------------------------------------

def read_records(path: Path, columns: tuple[str, ...], pattern: re.Pattern, key) -> list[dict]:
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    rows = list(csv.DictReader(lines, delimiter="\t"))
    if not rows or tuple(rows[0].keys()) != columns:
        raise km.InvalidManifest(f"{path.name}: unexpected columns")
    seen = set()
    for row in rows:
        if not pattern.match(row["disposition"]) or row["packet"] not in PACKETS:
            raise km.InvalidManifest(f"{path.name}: bad disposition/packet: {row}")
        if key(row) in seen:
            raise km.InvalidManifest(f"{path.name}: duplicate identity {key(row)}")
        seen.add(key(row))
    return rows


def _slot_key(row: dict) -> tuple:
    return (row["slot"], row["current_owner"], row["row_class"], row["body"])


def read_slots(path: Path) -> list[dict]:
    rows = read_records(path, SLOT_COLUMNS, SLOT_DISPOSITION, _slot_key)
    for row in rows:
        final, disposition = row["final_owner"], row["disposition"]
        if final not in PORT_ORDER:
            raise km.InvalidManifest(f"{path.name}: unknown final owner {final}")
        if row["current_owner"] != "-" and row["kind"] != "new":
            expected = CHAIN_PORT.get(row["row_class"], row["current_owner"])
            if row["current_owner"] == "FElysiumNpc":
                expected = "FElysiumNpc"
            if disposition in ("keep", "move", "move-hand") and final != expected:
                raise km.InvalidManifest(f"{path.name}: slot {row['slot']} {row['body']} belongs on "
                                         f"{expected}, not {final}")
            if disposition == "keep" and final != row["current_owner"]:
                raise km.InvalidManifest(f"{path.name}: a kept row stays on its owner: {row}")
            if disposition == "move-hand" and row["kind"] != "hand":
                raise km.InvalidManifest(f"{path.name}: move-hand needs a hand row: {row}")
            if disposition == "delete-dead" and (row["verdict"] != "dead" or row["kind"] != "stub"):
                raise km.InvalidManifest(f"{path.name}: only a dead stub is deleted: {row}")
        elif disposition != "new-chain-row" or final != CHAIN_PORT[row["row_class"]]:
            raise km.InvalidManifest(f"{path.name}: a new chain row stands on its class: {row}")
    return rows


def read_moves(path: Path) -> list[dict]:
    rows = read_records(path, MOVE_COLUMNS, MOVE_DISPOSITION, lambda r: (r["member"], r["current_owner"]))
    for row in rows:
        if row["final_owner"] not in PORT_ORDER:
            raise km.InvalidManifest(f"{path.name}: unknown final owner for {row['member']}")
    return rows


def check_identity(rows: list[dict], derived: list[dict], what: str) -> None:
    have = {_slot_key(r): tuple(r[c] for c in SLOT_IDENTITY) for r in rows}
    want = {_slot_key(r): tuple(r[c] for c in SLOT_IDENTITY) for r in derived}
    missing = sorted(set(want) - set(have))[:10]
    extra = sorted(set(have) - set(want))[:10]
    if missing or extra:
        raise km.InvalidManifest(f"{what}: rows differ from the derivation: missing {missing}, extra {extra}")
    changed = [k for k in want if have[k] != want[k]][:10]
    if changed:
        raise km.InvalidManifest(f"{what}: identity columns differ: " +
                                 "; ".join(f"{k}: {have[k]} != {want[k]}" for k in changed))


# ---- phase-6 source checks -------------------------------------------------------------------------

def _class_start(cls: str, text: str):
    return re.search(rf"\bclass (?:\w+_API )?{cls}\b[^;{{]*\{{", text)


def _class_header(root: Path, cls: str) -> Path | None:
    """The first of `cls`'s candidate headers that declares it, or None."""
    for rel in CLASS_HEADERS[cls]:
        path = root / rel
        if path.is_file() and _class_start(cls, path.read_text(encoding="utf-8-sig")):
            return path
    return None


def _class_scope(root: Path, cls: str) -> str:
    """The text of `cls`'s class body plus every `.inl` the body includes."""
    header = _class_header(root, cls)
    if header is None:
        raise km.InvalidManifest(f"{cls} is not declared in any of {CLASS_HEADERS[cls]}")
    text = header.read_text(encoding="utf-8-sig")
    start = _class_start(cls, text)
    mask = mask_cpp(text[start.end():])
    depth, end = 1, 0
    for i, ch in enumerate(mask):
        depth += (ch == "{") - (ch == "}")
        if depth == 0:
            end = i
            break
    body = text[start.end():start.end() + end]
    parts = [body]
    for include in re.findall(r'#include "(?:Substrate/)?(\w+\.inl)"', body):
        for folder in (root / SUBSTRATE, root / PUBLIC):
            if (folder / include).is_file():
                parts.append((folder / include).read_text(encoding="utf-8-sig"))
                break
    return "\n".join(parts)


def _scope_members(scope: str) -> set[str]:
    names = set()
    mask, spans = _top_level(scope)
    for s, e in spans:
        decl = re.sub(r"\s+", " ", mask[s:e]).strip()
        decl = re.sub(r"^(?:(?:public|private|protected)\s*:\s*)+", "", decl)
        kind_type = re.match(r"(?:enum class|enum|struct|class)\s+(\w+)", decl)
        head = decl[:decl.index("{")] if "{" in decl and "(" in decl.split("{")[0] else decl
        method, field = METHOD.search(head), FIELD.search(decl)
        if kind_type:
            names.add(kind_type.group(1))
        elif method and "=" not in head.split("(")[0]:
            names.add(method.group(1))
        elif field:
            names.add(field.group(1))
    return names


def check_slots(rows: list[dict], root: Path) -> collections.Counter:
    declared = {}
    for cls, (inl, _) in SLOT_FILES.items():
        path = root / inl
        declared[cls] = {(slot, body) for slot, body, _, _ in _decls(path.read_text(encoding="utf-8-sig"))} \
            if path.is_file() else set()
    overlay = _verdict_targets(root)
    counts: collections.Counter = collections.Counter()
    for row in rows:
        key = (int(row["slot"]), row["body"])
        disposition, final = row["disposition"], row["final_owner"]
        if disposition in ("keep", "move", "move-hand", "new-chain-row"):
            if key not in declared[final]:
                raise km.InvalidManifest(f"slot {row['slot']} {row['body']} is not declared on {final}")
            for other, keys in declared.items():
                if other != final and key in keys:
                    raise km.InvalidManifest(f"slot {row['slot']} {row['body']} is still declared on {other}")
        elif disposition == "delete-dead":
            if any(key in keys for keys in declared.values()):
                raise km.InvalidManifest(f"deleted slot {row['slot']} {row['body']} is still declared")
            if overlay.get(row["body"].removeprefix("0x"), "-") != "-":
                raise km.InvalidManifest(f"deleted slot {row['slot']}: its overlay target is not `-`")
        elif disposition.startswith(("adapter:", "seam:")):
            member = disposition.split(":", 1)[1]
            if member not in _scope_members(_class_scope(root, final)) and \
                    member not in _scope_members(_class_scope(root, "FElysiumEntity")):
                raise km.InvalidManifest(f"slot {row['slot']}: {member} is not a member of {final}")
        elif disposition.startswith("implemented:"):
            if any(key in keys for keys in declared.values()):
                raise km.InvalidManifest(f"implemented slot {row['slot']} still has a generated declaration")
        elif disposition == "investigate":
            raise km.InvalidManifest(f"slot {row['slot']} {row['body']} is still under investigation")
        counts[disposition.split(":")[0]] += 1
    return counts


def _verdict_targets(root: Path) -> dict[str, str]:
    out = {}
    for line in (root / VERDICTS).read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if len(cells) >= 4 and re.fullmatch(r"[0-9a-f]{8}", cells[0]):
            out[cells[0]] = cells[3]
    return out


def check_moves(rows: list[dict], root: Path) -> collections.Counter:
    scopes = {cls: _scope_members(_class_scope(root, cls)) for cls in PORT_ORDER
              if _class_header(root, cls) is not None}
    counts: collections.Counter = collections.Counter()
    for row in rows:
        if row["disposition"] == "investigate":
            raise km.InvalidManifest(f"member {row['member']} is still under investigation")
        if row["disposition"] != "move":
            counts["stay"] += 1
            continue
        member, final = row["member"], row["final_owner"]
        if member not in scopes.get(final, set()):
            raise km.InvalidManifest(f"{member} is not declared on {final}")
        if final != "FElysiumNpcBase" and member in scopes["FElysiumNpcBase"]:
            raise km.InvalidManifest(f"{member} is still declared on FElysiumNpcBase")
        counts[final] += 1
    return counts


def check_gate(decisions: dict, root: Path) -> int:
    from kernel_migration_step3 import SURVIVOR_DISPOSITIONS, gate_sites
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


def check_retired(root: Path, symbols: list[str]) -> int:
    """No source file names a symbol the step retired (`decisions-step6.json` `retired_symbols`)."""
    if not symbols:
        return 0
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, symbols)) + r")\b")
    hits = []
    for path in sorted((root / SOURCE).rglob("*")):
        if path.suffix in {".cpp", ".h", ".inl"}:
            text = mask_cpp(path.read_text(encoding="utf-8-sig"))
            if pattern.search(text):
                hits.append(path.relative_to(root).as_posix())
    if hits:
        raise km.InvalidManifest("retired tables still referenced: " + ", ".join(hits[:10]))
    return 0


def check_unported(directory: Path) -> int:
    """The live unported-rule set equals its pin (`kernel_shape --unported`)."""
    import kernel_shape as ks
    pin = directory / "unported-step6.tsv"
    if not pin.is_file():
        raise km.InvalidManifest("unported-step6.tsv is missing")
    pinned = {tuple(l.split("\t")[:3]) for l in pin.read_text(encoding="utf-8").splitlines()
              if l and not l.startswith(("#", "class\t"))}
    current = {tuple(r[:3]) for r in ks.unported_rows(repo_root())}
    if pinned != current:
        added = sorted(current - pinned)[:10]
        gone = sorted(pinned - current)[:10]
        raise km.InvalidManifest(f"the unported rule set differs from its pin: added {added}, gone {gone}")
    return len(current)


# ---- the check -----------------------------------------------------------------------------------

def check_step6(directory: Path | None = None) -> dict:
    """While step 6 is the working phase the current tree is checked. Once its commit is recorded
    (`manifest.json` `history.step6.commit`), the accepted tree is."""
    directory = directory or repo_root() / km.STORY
    manifest, _, _ = km.load(directory)
    accepted = manifest.get("history", {}).get("step6", {}).get("commit", "")
    if not accepted:
        if manifest["phase"] > 6:
            raise km.InvalidManifest("a later phase needs history.step6.commit")
        return _check_step6(directory, repo_root())
    with tempfile.TemporaryDirectory(prefix="step6-accepted-") as scratch:
        return _check_step6(directory, km.historical_source(accepted, Path(scratch), HISTORICAL_PATHS))


def _check_step6(directory: Path, tree: Path) -> dict:
    manifest, _, _ = km.load(directory)
    commit = manifest.get("history", {}).get("step5", {}).get("commit", "")
    if not commit:
        raise km.InvalidManifest("step 6 needs the accepted step-5 tree (history.step5.commit)")
    slots = read_slots(directory / "slots-step6.tsv")
    moves = read_moves(directory / "moves-step6.tsv")
    decisions = json.loads((directory / "decisions-step6.json").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory(prefix="step6-before-") as scratch:
        before = km.historical_source(commit, Path(scratch), HISTORICAL_PATHS)
        check_identity(slots, slot_manifest(before), "slot record")
    if manifest["phase"] < 6:
        return {"pending": True, "slot_rows": len(slots), "move_rows": len(moves),
                "slots": dict(collections.Counter(r["disposition"].split(":")[0] for r in slots)),
                "moves": dict(collections.Counter(r["final_owner"] for r in moves)),
                "surviving_sites": sum(int(s["count"]) for s in decisions["surviving_sites"])}
    from kernel_migration_step5 import check_step5
    step5 = check_step5(directory)
    counts = {"slots": dict(check_slots(slots, tree)), "moves": dict(check_moves(moves, tree)),
              "gate_tokens": check_gate(decisions, tree),
              "retired": check_retired(tree, decisions.get("retired_symbols", [])),
              "unported": check_unported(directory)}
    record = json.loads((directory / "acceptance-step6.json").read_text(encoding="utf-8"))
    if record.get("scope") != "step-6-slot-owners":
        raise km.InvalidManifest("acceptance-step6.json does not record step 6")
    artifacts = {}
    for name in ("delta", "runtime", "cheap_checks"):
        ref = record["artifacts"][name]
        path = Path(ref["path"])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref["sha256"]:
            raise km.InvalidManifest(f"step-6 evidence changed/missing: {name}")
        artifacts[name] = json.loads(path.read_text(encoding="utf-8-sig"))
    delta = artifacts["delta"]
    if not delta.get("comparison_passed") or delta.get("differences"):
        raise km.InvalidManifest("step-6 regression comparison has unmatched differences")
    expectations = json.loads((directory / "expectations/step-6.json").read_text(encoding="utf-8"))
    unused = {c["id"] for c in expectations["changes"]} - set(delta.get("applied_expectations", []))
    if unused:
        raise km.InvalidManifest("unconsumed step-6 expectations: " + ", ".join(sorted(unused)))
    if len(artifacts["runtime"]) != 4 or any(r["exit"] for r in artifacts["runtime"]):
        raise km.InvalidManifest("step-6 runtime gate incomplete/failing")
    if not artifacts["cheap_checks"] or any(r["exit"] for r in artifacts["cheap_checks"]):
        raise km.InvalidManifest("step-6 cheap/Python gates incomplete/failing")
    review = check_review_receipt(directory)
    return {**counts, "step5": {k: v for k, v in step5.items() if k != "step4"},
            **({"review_6r": review} if review else {})}


def check_review_receipt(directory: Path) -> dict:
    """The step-6 review follow-up (packet 6r), compared against step 6's own accepted gate: its
    pinned evidence, a passing regression comparison that consumed every 6r expectation, and a
    green runtime and cheap-check gate. Absent before the follow-up lands."""
    path = directory / "acceptance-step6r.json"
    if not path.is_file():
        return {}
    record = json.loads(path.read_text(encoding="utf-8"))
    if record.get("scope") != "step-6r-review-follow-up":
        raise km.InvalidManifest("acceptance-step6r.json does not record the step-6 review follow-up")
    artifacts = {}
    for name in ("delta", "runtime", "cheap_checks"):
        ref = record["artifacts"][name]
        artifact = Path(ref["path"])
        if not artifact.is_absolute() or not artifact.is_file() or file_sha(artifact) != ref["sha256"]:
            raise km.InvalidManifest(f"step-6r evidence changed/missing: {name}")
        artifacts[name] = json.loads(artifact.read_text(encoding="utf-8-sig"))
    delta = artifacts["delta"]
    if not delta.get("comparison_passed") or delta.get("differences"):
        raise km.InvalidManifest("step-6r regression comparison has unmatched differences")
    expectations = json.loads((directory / "expectations/step-6r.json").read_text(encoding="utf-8"))
    unused = {c["id"] for c in expectations["changes"]} - set(delta.get("applied_expectations", []))
    if unused:
        raise km.InvalidManifest("unconsumed step-6r expectations: " + ", ".join(sorted(unused)))
    if len(artifacts["runtime"]) != 4 or any(r["exit"] for r in artifacts["runtime"]):
        raise km.InvalidManifest("step-6r runtime gate incomplete/failing")
    if not artifacts["cheap_checks"] or any(r["exit"] for r in artifacts["cheap_checks"]):
        raise km.InvalidManifest("step-6r cheap/Python gates incomplete/failing")
    return {"expectations": len(expectations["changes"]), "results": record.get("results", {})}
