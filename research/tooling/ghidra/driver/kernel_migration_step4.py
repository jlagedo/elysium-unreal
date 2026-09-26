"""Story 5 step 4: species bodies, words and bindings stand on their retail owners.

Step 3 left each introduced species reaching its behaviour through C++ overrides that forward to
`FElysiumNpc` members (declared in the family `.inl` files inside `ElysiumNpc.h`), with the
species' state on the flat NPC and none of the species datamaps bound. This check holds the tree to
the step's authored records:

* `moves-step4.tsv` is the move manifest: one row per `FElysiumNpc` member the species touch,
  re-derived from the accepted step-3 tree: every member only species definitions use (proposed
  owner: the closest class over its users, to a fixed point through other moving members), every
  ported member whose retail body belongs to an introduced species (the direct-call owner of its
  address in the kernel ledger), and every non-moving member a species definition uses. Each row
  carries a reviewed disposition: `move` onto a species class, `collapse` into the one override
  that forwards to it, `stay` on `FElysiumNpc` or `deferred` to its fold step;
* `fields-step4.tsv` is the binding manifest: every datamap record of an introduced species' own
  table in the pinned replay, keyed by declaring class + offset + member, with its port storage,
  input body or output and the generated registration it must have;
* at phase 4, a `move` member is declared on its owner's header and defined in its cpp and nowhere
  on `FElysiumNpc`; a `collapse` member is gone; `stay`/`deferred` members are where they were;
  every bound field is declared on its owner and registered on its own class's descriptor only;
* the step-3 checks still hold on their accepted tree, and the step-4 regression comparison,
  expectations and runtime receipts are pinned in `acceptance-step4.json`.

While phase 3 is current the step is in progress: `--check step4` validates the records against
the accepted step-3 tree and reports the open dispositions, and refuses nothing about the source.

    uv run elysium research kernel_migration --check step4
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
from kernel_migration_inventory import definitions, end_pair, mask_cpp
from elysium_pipeline.paths import repo_root, research_root

SOURCE = Path("Source/ElysiumUE")
PRIVATE = SOURCE / "Private"
SUBSTRATE = PRIVATE / "Substrate"
SHAPE = SUBSTRATE / "ElysiumNpcKernelShape.cpp"
NPC_HEADER = SUBSTRATE / "ElysiumNpc.h"
BINDINGS = SUBSTRATE / "ElysiumNpcKernelBindings.cpp"
SPECIES_SHAPE_MAP = SUBSTRATE / "ElysiumNpcKernelSpeciesShapeMap.cpp"
GRAPH = Path("docs/vtmb/npc-kernel/graph.tsv")
VERDICTS = Path("research/tooling/ghidra/driver/kernel_verdicts.tsv")
REPLAY = "ghidra/types/datamap_records-vampire.dll.json"
GENERATED = {"ElysiumNpcKernelShape.cpp", "ElysiumNpcKernelBindings.cpp", "ElysiumNpcKernelSlots.cpp"}
HISTORICAL_PATHS = ("Source/ElysiumUE", str(VERDICTS).replace("\\", "/"), str(GRAPH).replace("\\", "/"))
LIVE_VERDICTS = {"rule", "present", "mechanism"}

MOVE_COLUMNS = ("member", "kind", "declared_in", "defined_in", "users_owner", "retail_owner",
                "final_owner", "disposition", "packet", "note")
MOVE_IDENTITY = MOVE_COLUMNS[:6]
MOVE_DISPOSITION = re.compile(r"^(move|collapse:\w+|stay:.+|deferred:(7|8|9|10)|investigate)$")
FIELD_COLUMNS = ("declaring_class", "offset", "member", "type", "flags", "external", "kind",
                 "final_owner", "final_path", "disposition", "packet", "note")
FIELD_IDENTITY = FIELD_COLUMNS[:7]
FIELD_DISPOSITION = re.compile(r"^(bind|declare|shadow:\w+|input:\w+|input-seam|output|absent|investigate)$")
PACKETS = {"4b", "4c", "4d", "4e", "4f", "4g", "4h"}


# ---- the class tree ---------------------------------------------------------------------------

def port_tree(classes: list[dict]) -> tuple[dict[str, str], set[str], dict[str, str]]:
    """(port base by live port class, introduced species port classes, port class by retail class)."""
    bases = {r["port_class"]: r["port_base"] for r in classes if r["liveness"] == "live"}
    species = {r["port_class"] for r in classes if r["step"] == "2" and r["port_class"] != "FElysiumNpc"}
    return bases, species, {r["retail_class"]: r["port_class"] for r in classes}


def _chain(cls: str, bases: dict[str, str]) -> list[str]:
    out = []
    while cls in bases:
        out.append(cls)
        cls = bases[cls]
    return out


def closest_common(owners, bases: dict[str, str]) -> str | None:
    owners = sorted(set(o for o in owners if o))
    if not owners:
        return None
    common = _chain(owners[0], bases)
    for owner in owners[1:]:
        seen = set(_chain(owner, bases))
        common = [c for c in common if c in seen]
    return common[0] if common else "FElysiumNpc"


# ---- FElysiumNpc's declared members ----------------------------------------------------------

def _top_level(text: str):
    """Top-level declarations of a class-scope text: (start, end) spans over the masked text."""
    mask = mask_cpp(text)
    spans, start, at = [], 0, 0
    while at < len(mask):
        char = mask[at]
        if char == "{":
            end = end_pair(mask, at)
            if "(" in mask[start:at] and "=" not in mask[start:at].split("(")[0]:
                spans.append((start, end))
                start = end
            at = end
            continue
        if char == ";":
            spans.append((start, at + 1))
            start = at + 1
        at += 1
    return mask, spans


METHOD = re.compile(r"\b(~?\w+)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*(?:const)?\s*(?:override|final)?\s*(?:=\s*0)?\s*[;{]?$")
FIELD = re.compile(r"\b(\w+)\s*(?:\[[^\]]*\])*\s*(?:=[^;]*|\{[^;]*\})?\s*;$")


def npc_members(root: Path) -> dict[str, dict]:
    """Every member `FElysiumNpc` declares in its class body or a family `.inl`: name -> kind/file."""
    header = (root / NPC_HEADER).read_text(encoding="utf-8-sig")
    start = header.index("class FElysiumNpc : public")
    body = header[header.index("{", start) + 1:header.index("\n};", start)]
    sources = [(NPC_HEADER.name, body)] + [(p.name, p.read_text(encoding="utf-8-sig"))
                                          for p in sorted((root / SUBSTRATE).glob("ElysiumNpcKernel*.inl"))]
    members: dict[str, dict] = {}
    for name, text in sources:
        mask, spans = _top_level(text)
        for s, e in spans:
            decl = re.sub(r"\s+", " ", mask[s:e]).strip()
            decl = re.sub(r"^(?:(?:public|private|protected)\s*:\s*)+", "", decl)
            decl = re.sub(r"#\s*\w+[^;{]*?(?=\s[A-Za-z_])", "", decl).strip()
            if not decl or decl.startswith(("using ", "friend ", "typedef ", "static_assert")):
                continue
            kind_type = re.match(r"(?:enum class|enum|struct|class)\s+(\w+)", decl)
            head = decl[:decl.index("{")] if "{" in decl and "(" in decl.split("{")[0] else decl
            method = METHOD.search(head)
            field = FIELD.search(decl)
            if kind_type:
                entry = members.setdefault(kind_type.group(1), {"kind": "type", "file": name})
            elif method and "=" not in head.split("(")[0]:
                entry = members.setdefault(method.group(1), {"kind": "method", "file": name, "virtual": False})
                entry["virtual"] = entry.get("virtual") or "virtual" in decl or " override" in decl
            elif field:
                entry = members.setdefault(field.group(1), {"kind": "field", "file": name})
            else:
                continue
            # Overloads declare one name several times, possibly in several families.
            entry.setdefault("decls", collections.Counter())[name] += 1
            # The retail classes the declaration's own comments name (its doc block and the rest of
            # its line): a field documented as a species word is one even when nothing reads it.
            line_end = text.find("\n", e)
            raw = text[s:line_end if line_end >= 0 else len(text)]
            entry.setdefault("mentions", set()).update(re.findall(r"\b(CNPC_V\w+|CPayphone)\b", raw))
    return members


# ---- users ---------------------------------------------------------------------------------------

def module_definitions(root: Path):
    found = []
    for path in sorted((root / PRIVATE).rglob("*.cpp")):
        rel = path.relative_to(root).as_posix()
        if "/Tests/" in rel or path.name in GENERATED:
            continue
        found.extend(definitions(path.read_text(encoding="utf-8-sig"), rel))
    return found


def member_users(root: Path, members: dict[str, dict], defs) -> dict[str, set[str]]:
    """member -> the definitions (Owner::name) and headers (`<header:file>`) that name it."""
    users: dict[str, set[str]] = collections.defaultdict(set)
    for d in defs:
        for ident in d.identifiers:
            if ident in members:
                users[ident].add(d.symbol)
    # Code outside any `FElysium*::` definition (free functions, file-scope tables, lambdas at file
    # scope) is a user too: `<free:file>`.
    spans: dict[str, list[tuple[int, int]]] = collections.defaultdict(list)
    for d in defs:
        spans[d.path].append((d.start, d.end))
    for path in sorted((root / PRIVATE).rglob("*.cpp")):
        rel = path.relative_to(root).as_posix()
        if "/Tests/" in rel or path.name in GENERATED:
            continue
        mask = list(mask_cpp(path.read_text(encoding="utf-8-sig")))
        for start, end in spans.get(rel, ()):
            mask[start:end] = " " * (end - start)
        for ident in set(re.findall(r"\b[A-Za-z_]\w*\b", "".join(mask))) & set(members):
            users[ident].add(f"<free:{path.name}>")
    for path in sorted((root / PRIVATE).rglob("*")):
        if path.suffix not in {".h", ".inl"} or "/Tests/" in path.as_posix() or path.name in GENERATED:
            continue
        counts = collections.Counter(re.findall(r"\b[A-Za-z_]\w*\b", mask_cpp(path.read_text(encoding="utf-8-sig"))))
        for ident in set(counts) & set(members):
            own = members[ident].get("decls", {}).get(path.name, 0)   # declarations are not users
            if counts[ident] > own:
                users[ident].add(f"<header:{path.name}>")
    return users


def users_owner(members: dict[str, dict], users: dict[str, set[str]], species: set[str],
                bases: dict[str, str], seeds: dict[str, str] | None = None) -> dict[str, str]:
    """Members only species definitions use, to a fixed point through other such members: the
    closest class over the users. A header use, a non-species definition or a virtual refuses.
    `seeds` are members already placed (an unwired body on its retail owner)."""
    moving: dict[str, str] = dict(seeds or {})
    changed = True
    while changed:
        changed = False
        for name, info in members.items():
            if name in moving or info["kind"] == "type" or info.get("virtual"):
                continue
            owners, ok = [], bool(users.get(name))
            for user in users.get(name, ()):
                if user.startswith("<"):
                    ok = False
                    break
                cls, method = user.split("::", 1)
                if cls in species:
                    owners.append(cls)
                elif cls == "FElysiumNpc" and method == name:
                    continue
                elif cls == "FElysiumNpc" and method in moving:
                    owners.append(moving[method])
                else:
                    ok = False
                    break
            owner = closest_common(owners, bases) if ok and owners else None
            if owner and owner != "FElysiumNpc":
                moving[name] = owner
                changed = True
    return moving


# ---- retail ownership --------------------------------------------------------------------------

CLASS_ROW = re.compile(r'\{\s*TEXT\("(\w+)"\),\s*TEXT\("(\w*)"\),\s*TEXT\("0x[0-9a-fA-F]+"\),')
OVERRIDE_ROW = re.compile(r'\{\s*TEXT\("(\w+)"\),\s*(\d+),\s*TEXT\("0x([0-9a-f]+)"\)')


# vampire.dll's link order puts the Troika line and everything beneath it below the V-species
# objects (`CGenericNPC` 0x1034a2d0 up to `CNPC_VYukie` 0x103ddaf0), except `CPayphone`, which the
# linker placed with the entity code. A caller-derived owner outside those ranges is a base body a
# species happens to call directly, not a species body.
SPECIES_CODE = ((0x1035c000, 0x103e2000), (0x101aa000, 0x101ac000))


def species_code(address: str) -> bool:
    value = int(address, 16)
    return any(lo <= value < hi for lo, hi in SPECIES_CODE)


def base_code(address: str) -> bool:
    """Below the V-species objects and not the payphone: the entity, Troika and base-NPC code. An
    address past the last species object is data (a global a comment cites), not a body."""
    return int(address, 16) < SPECIES_CODE[0][0] and not species_code(address)


def retail_owners(root: Path, classes: list[dict]) -> dict[str, str]:
    """address -> retail class owning it: a slot body's introducing class (topmost holding the same
    body), else the closest class over its direct callers in the kernel ledger, to a fixed point.
    Dead classes do not own; a caller-derived owner is a proposal for review, not a verdict."""
    text = re.sub(r"//[^\n]*", "", (root / SHAPE).read_text(encoding="utf-8"))
    rbases = dict(CLASS_ROW.findall(text))
    block = text[text.index("GOverrides[] ="):]
    rows = OVERRIDE_ROW.findall(block[:block.index("};")])
    live = {r["retail_class"] for r in classes if r["liveness"] == "live"}
    by_class: dict[str, dict[int, str]] = collections.defaultdict(dict)
    for cls, slot, address in rows:
        by_class[cls][int(slot)] = address

    def rchain(cls):
        out = []
        while cls:
            out.append(cls)
            cls = rbases.get(cls)
        return out

    def rcommon(owners):
        owners = sorted(set(o for o in owners if o))
        if not owners:
            return None
        common = rchain(owners[0])
        for owner in owners[1:]:
            seen = set(rchain(owner))
            common = [c for c in common if c in seen]
        return common[0] if common else None

    slot_owner: dict[str, set[str]] = collections.defaultdict(set)
    for cls, slot, address in rows:
        if cls not in live:
            continue
        top, base = cls, rbases.get(cls)
        while base and by_class.get(base, {}).get(int(slot)) == address:
            top, base = base, rbases.get(base)
        slot_owner[address].add(top)
    owner = {a: rcommon(cs) for a, cs in slot_owner.items()}
    callers: dict[str, set[str]] = collections.defaultdict(set)
    for line in (root / GRAPH).read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if len(cells) >= 3 and cells[2] == "direct":
            callers[cells[1]].add(cells[0])
    for _ in range(16):
        changed = False
        for callee, cs in callers.items():
            if callee in slot_owner or not species_code(callee):
                continue
            new = rcommon(owner.get(c) for c in cs)
            if new and owner.get(callee) != new:
                owner[callee] = new
                changed = True
        if not changed:
            break
    return {a: o for a, o in owner.items() if o}


def verdict_addresses(root: Path) -> dict[str, set[str]]:
    """FElysiumNpc member -> the live-verdict addresses whose overlay target names it."""
    found: dict[str, set[str]] = collections.defaultdict(set)
    for line in (root / VERDICTS).read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if len(cells) > 3 and re.fullmatch(r"[0-9a-f]{8}", cells[0]) and cells[1] in LIVE_VERDICTS:
            target = cells[3].removeprefix("hand:")
            if re.match(r"^[\w/]+\.(?:cpp|h|inl):", target):
                target = target.rsplit(":", 1)[-1]
            if target.startswith("FElysiumNpc::"):
                found[target.split("::", 1)[1]].add(cells[0])
    return found


# ---- the move manifest -------------------------------------------------------------------------

def move_manifest(root: Path, classes: list[dict]) -> list[dict]:
    """The identity columns of `moves-step4.tsv`, derived from the tree at `root`."""
    bases, species, to_port = port_tree(classes)
    members = npc_members(root)
    defs = module_definitions(root)
    users = member_users(root, members, defs)
    owners = retail_owners(root, classes)
    addresses = verdict_addresses(root)
    defined: dict[str, set[str]] = collections.defaultdict(set)
    for d in defs:
        if d.symbol.startswith("FElysiumNpc::"):
            name = d.symbol.split("::", 1)[1]
            defined[name].add(d.path.rsplit("/", 1)[-1])
            if d.leading_address:
                addresses[name].add(d.leading_address)

    def retail_port_of(name: str) -> str:
        retail = {owners[a] for a in addresses.get(name, ()) if a in owners}
        return to_port.get(next(iter(retail)), "") if len(retail) == 1 else ""

    # A ported body retail places on an introduced species stands on that owner when every port
    # user is a species definition or another such body (the greatest fixed point, so mutually
    # recursive helpers settle together); an unwired body has no user and stands there too. Their
    # helpers follow them through `users_owner`.
    placed = {name: retail_port_of(name) for name, info in members.items()
              if info["kind"] == "method" and not info.get("virtual") and retail_port_of(name) in species}
    # An unwired member without a retail address follows the species state and bodies it reaches
    # (data access): the closest class over the moving members its definitions name.
    reaches: dict[str, set[str]] = collections.defaultdict(set)
    for d in defs:
        if d.symbol.startswith("FElysiumNpc::"):
            reaches[d.symbol.split("::", 1)[1]].update(i for i in d.identifiers if i in members)
    while True:
        moving = users_owner(members, users, species, bases, placed)
        grown = False
        for name, info in members.items():
            if name in moving or info["kind"] != "method" or info.get("virtual") or users.get(name):
                continue
            touched = [moving[i] for i in reaches.get(name, ()) if i in moving and i != name]
            owner = closest_common(touched, bases) if touched else None
            if owner and owner != "FElysiumNpc":
                placed[name] = owner
                grown = True
        if grown:
            continue
        dropped = False
        for name in list(placed):
            for user in users.get(name, ()):
                cls, _, method = user.partition("::")
                user_owner = cls if cls in species else moving.get(method) if cls == "FElysiumNpc" else None
                if method == name and cls == "FElysiumNpc":
                    continue
                # A placed body's users must stand on its owner or beneath it (a sibling cannot call it).
                if user_owner is None or placed[name] not in _chain(user_owner, bases):
                    del placed[name]
                    dropped = True
                    break
        if not dropped:
            break
    # Members a species definition or a moving body names: what the move leaves behind must be
    # reachable from the new owners (a cross-reader or a shared helper).
    species_used: set[str] = set()
    for d in defs:
        cls, _, method = d.symbol.partition("::")
        if cls in species or (cls == "FElysiumNpc" and method in moving):
            species_used.update(i for i in d.identifiers if i in members)
    # Class-keyed members: a definition spelling an introduced species' retail classname (a table
    # row or a lookup) is species dispatch data; step 4 collapses it or lists why it survives.
    introduced = {r["retail_class"] for r in classes if r["port_class"] in species}
    keyed: set[str] = set()
    for d in defs:
        if d.symbol.startswith("FElysiumNpc::"):
            text = (root / d.path).read_text(encoding="utf-8-sig")[d.start:d.end]
            if any(f'"{c}"' in text for c in re.findall(r'TEXT\("(\w+)"\)', text) if c in introduced):
                keyed.add(d.symbol.split("::", 1)[1])
    rows = []
    for name, info in sorted(members.items()):
        # The port names its class-keyed tables, their row types and accessors `*Species*`
        # (`FDamageFlinchSpecies`, `TraceAttackSpeciesOf`, `SpeciesSquadSlotName`): each is listed so
        # a collapse cannot leave one behind.
        table = "Species" in name
        if info["kind"] == "type" and not table:
            continue
        retail = sorted({owners[a] if a in owners else "(base code)" for a in addresses.get(name, ())
                         if a in owners or base_code(a)})
        retail_port = to_port.get(retail[0], "") if len(retail) == 1 else ""
        word = info["kind"] == "field" and any(to_port.get(c) in species for c in info.get("mentions", ()))
        # A ported body at a species-object address is a species body even when neither a port
        # user nor a ledger caller places it.
        body = info["kind"] == "method" and any(species_code(a) for a in addresses.get(name, ()))
        if not (name in moving or retail_port in species or name in species_used or name in keyed
                or table or word or body):
            continue
        rows.append({"member": name, "kind": info["kind"], "declared_in": info["file"],
                     "defined_in": ",".join(sorted(defined.get(name, ()))) or "-",
                     "users_owner": moving.get(name, "-"), "retail_owner": ",".join(retail) or "-"})
    return rows


def read_records(path: Path, columns: tuple[str, ...], pattern: re.Pattern, key) -> list[dict]:
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    rows = list(csv.DictReader(lines, delimiter="\t"))
    if not rows or tuple(rows[0].keys()) != columns:
        raise km.InvalidManifest(f"{path.name}: unexpected columns")
    seen = set()
    for row in rows:
        if not pattern.match(row["disposition"]) or (row["packet"] not in PACKETS and row["packet"] != "-"):
            raise km.InvalidManifest(f"{path.name}: bad disposition/packet: {row}")
        if key(row) in seen:
            raise km.InvalidManifest(f"{path.name}: duplicate identity {key(row)}")
        seen.add(key(row))
    return rows


def read_moves(path: Path) -> list[dict]:
    rows = read_records(path, MOVE_COLUMNS, MOVE_DISPOSITION, lambda r: r["member"])
    for row in rows:
        if row["disposition"].startswith(("stay:", "deferred:")) and row["final_owner"] != "FElysiumNpc":
            raise km.InvalidManifest(f"{path.name}: a staying member stays on FElysiumNpc: {row['member']}")
        if row["disposition"] == "move" and not row["final_owner"].startswith("FElysiumNpc"):
            raise km.InvalidManifest(f"{path.name}: a move names its owner class: {row['member']}")
    return rows


def check_identity(rows: list[dict], expected: list[dict], columns: tuple[str, ...], what: str) -> None:
    have = [tuple(r[c] for c in columns) for r in rows]
    want = [tuple(r[c] for c in columns) for r in expected]
    if have != want:
        missing = sorted(set(want) - set(have))[:10]
        extra = sorted(set(have) - set(want))[:10]
        raise km.InvalidManifest(f"{what} differs from the step-3 tree: missing {missing}, extra {extra}")


# ---- the binding manifest ----------------------------------------------------------------------

def species_datamaps(replay: dict, classes: list[dict]) -> list[dict]:
    """The identity columns of `fields-step4.tsv`: every record of an introduced species' own table."""
    introduced = sorted(r["retail_class"] for r in classes
                        if r["step"] == "2" and r["port_class"] != "FElysiumNpc")
    rows = []
    for cls in introduced:
        for record in (replay.get(cls) or {}).get("records", []):
            flags = set(record.get("flagNames") or [])
            if "FUNCTIONTABLE" in flags or record.get("name") is None:
                continue   # an empty datamap's terminator row names nothing
            kind = ("input" if "INPUT" in flags and (record["typeName"] == "void" or record["offset"] == 0)
                    else "output" if "OUTPUT" in flags else "field")
            rows.append({"declaring_class": cls, "offset": f"0x{record['offset']:04x}", "member": record["name"],
                         "type": record["typeName"], "flags": str(record["flags"]),
                         "external": record.get("external") or "-", "kind": kind})
    return sorted(rows, key=lambda r: (r["declaring_class"], int(r["offset"], 16), r["member"]))


def read_fields(path: Path) -> list[dict]:
    rows = read_records(path, FIELD_COLUMNS, FIELD_DISPOSITION,
                        lambda r: (r["declaring_class"], r["offset"], r["member"]))
    for row in rows:
        kind, disposition = row["kind"], row["disposition"]
        if kind == "input" and not disposition.startswith(("input", "investigate")):
            raise km.InvalidManifest(f"{path.name}: an input binds a body or a seam: {row['member']}")
        if kind == "output" and disposition not in {"output", "investigate"}:
            raise km.InvalidManifest(f"{path.name}: an output binds an output member: {row['member']}")
        if kind == "field" and disposition.startswith(("input", "output")):
            raise km.InvalidManifest(f"{path.name}: a field binds storage: {row['member']}")
    return rows


# ---- phase-4 source checks ---------------------------------------------------------------------

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def _code(root: Path, relative: Path) -> str:
    path = root / relative
    return COMMENT.sub("", path.read_text(encoding="utf-8-sig")) if path.is_file() else ""


def check_moves(rows: list[dict], root: Path) -> collections.Counter:
    members = npc_members(root)
    counts: collections.Counter = collections.Counter()
    for row in rows:
        name, disposition = row["member"], row["disposition"]
        if disposition == "investigate":
            raise km.InvalidManifest(f"move row still under investigation: {name}")
        if disposition == "move":
            stem = row["final_owner"][1:]
            if name in members:
                raise km.InvalidManifest(f"{name} is still declared on FElysiumNpc ({members[name]['file']})")
            if not re.search(rf"\b{name}\b", _code(root, SUBSTRATE / f"{stem}.h")):
                raise km.InvalidManifest(f"{name} is not declared on {row['final_owner']}")
            if row["kind"] == "method" and row["defined_in"] != "-" and not re.search(
                    rf"\b{row['final_owner']}::{name}\s*\(", _code(root, SUBSTRATE / f"{stem}.cpp")):
                raise km.InvalidManifest(f"{row['final_owner']}::{name} is not defined in {stem}.cpp")
        elif disposition.startswith("collapse:"):
            if name in members:
                raise km.InvalidManifest(f"collapsed {name} is still declared on FElysiumNpc")
        elif name not in members:
            raise km.InvalidManifest(f"{name} ({disposition}) left FElysiumNpc")
        counts[disposition.split(":")[0]] += 1
    return counts


def check_fields(rows: list[dict], root: Path) -> collections.Counter:
    bindings = _code(root, BINDINGS)
    shape_map = _code(root, SPECIES_SHAPE_MAP)
    staying = npc_members(root)
    counts: collections.Counter = collections.Counter()
    for row in rows:
        disposition, who = row["disposition"], f"{row['declaring_class']}::{row['member']}"
        if disposition == "investigate":
            raise km.InvalidManifest(f"binding row still under investigation: {row['member']}")
        offset = f"0x{int(row['offset'], 16):04x}"
        if disposition == "absent":
            # A recorded gap: the class-qualified map says why, and nothing is generated for it.
            if not re.search(rf"ELYSIUM_NPC_SPECIES_WORD_ABSENT\(\s*{row['declaring_class']}\s*,\s*{offset}\b",
                             shape_map):
                raise km.InvalidManifest(f"{who} is absent but the species shape map does not say why")
        elif row["kind"] == "field":
            owner, member = row["final_path"].split("::", 1)
            if disposition.startswith("shadow:") or owner == "FElysiumNpc":
                # Inherited storage (a Troika word, or a deferred/Troika-read word that stays).
                if owner != "FElysiumNpc" or member not in staying:
                    raise km.InvalidManifest(f"{who} binds FElysiumNpc storage that is not there")
            elif owner != row["final_owner"] or not re.search(
                    rf"\b{member.split('.')[0]}\b", _code(root, SUBSTRATE / f"{owner[1:]}.h")):
                raise km.InvalidManifest(f"{who} has no storage on {owner}")
            name = row["external"] if row["external"] != "-" else row["member"]
            if f'TEXT("{name}' not in bindings:
                raise km.InvalidManifest(f"{who} ({name}) is not generated")
        elif row["kind"] == "output" and f'TEXT("{row["external"]}")' not in bindings:
            raise km.InvalidManifest(f"{row['declaring_class']} {row['external']} is not generated")
        counts[disposition.split(":")[0]] += 1
    return counts


# ---- the step ----------------------------------------------------------------------------------

def check_step4(directory: Path | None = None) -> dict:
    directory = directory or repo_root() / km.STORY
    manifest, classes, _ = km.load(directory)
    commit = manifest.get("history", {}).get("step3", {}).get("commit", "")
    if not commit:
        raise km.InvalidManifest("step 4 needs the accepted step-3 tree (history.step3.commit)")
    moves = read_moves(directory / "moves-step4.tsv")
    fields = read_fields(directory / "fields-step4.tsv")
    replay = json.loads((research_root() / REPLAY).read_text(encoding="utf-8"))
    check_identity(fields, species_datamaps(replay, classes), FIELD_IDENTITY, "binding manifest")
    with tempfile.TemporaryDirectory(prefix="step4-before-") as scratch:
        before = km.historical_source(commit, Path(scratch), HISTORICAL_PATHS)
        check_identity(moves, move_manifest(before, classes), MOVE_IDENTITY, "move manifest")
        if manifest["phase"] < 4:
            return {"pending": True, "move_rows": len(moves), "field_rows": len(fields),
                    "moves": dict(collections.Counter(r["disposition"].split(":")[0] for r in moves)),
                    "fields": dict(collections.Counter(r["disposition"].split(":")[0] for r in fields))}
        from kernel_migration_step1 import check_overlay_targets
        removed = check_overlay_targets(before, repo_root())
    from kernel_migration_step3 import check_step3
    step3 = check_step3(directory)
    root = repo_root()
    counts = {"moves": dict(check_moves(moves, root)), "fields": dict(check_fields(fields, root)),
              "removed_symbols": removed}
    record = json.loads((directory / "acceptance-step4.json").read_text(encoding="utf-8"))
    if record.get("scope") != "step-4-species-bodies-words-bindings":
        raise km.InvalidManifest("acceptance-step4.json does not record step 4")
    artifacts = {}
    for name in ("delta", "runtime", "cheap_checks"):
        ref = record["artifacts"][name]
        path = Path(ref["path"])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref["sha256"]:
            raise km.InvalidManifest(f"step-4 evidence changed/missing: {name}")
        artifacts[name] = json.loads(path.read_text(encoding="utf-8-sig"))
    delta = artifacts["delta"]
    if not delta.get("comparison_passed") or delta.get("differences"):
        raise km.InvalidManifest("step-4 regression comparison has unmatched differences")
    expectations = json.loads((directory / "expectations/step-4.json").read_text(encoding="utf-8"))
    unused = {c["id"] for c in expectations["changes"]} - set(delta.get("applied_expectations", []))
    if unused:
        raise km.InvalidManifest("unconsumed step-4 expectations: " + ", ".join(sorted(unused)))
    if len(artifacts["runtime"]) != 4 or any(r["exit"] for r in artifacts["runtime"]):
        raise km.InvalidManifest("step-4 runtime gate incomplete/failing")
    if not artifacts["cheap_checks"] or any(r["exit"] for r in artifacts["cheap_checks"]):
        raise km.InvalidManifest("step-4 cheap/Python gates incomplete/failing")
    return {**counts, "step3": step3}


# ---- drafting ----------------------------------------------------------------------------------

def draft_moves(root: Path, classes: list[dict]) -> list[dict]:
    """A first disposition per move row, for review. A member only species use moves to their
    closest class; if that class's single override is its only user it collapses into it. A
    member only deferred classes' retail bodies own stays deferred. Everything else is investigated."""
    bases, species, to_port = port_tree(classes)
    deferred = {r["retail_class"]: r["step"] for r in classes if r["step"] in {"7", "8", "9", "10"}}
    defs = module_definitions(root)
    members = npc_members(root)
    users = member_users(root, members, defs)
    overrides = {d.symbol for d in defs if d.symbol.split("::", 1)[0] in species}
    out = []
    for row in move_manifest(root, classes):
        name, owner = row["member"], row["users_owner"]
        retail = row["retail_owner"].split(",") if row["retail_owner"] != "-" else []
        retail_port = to_port.get(retail[0], "") if len(retail) == 1 else ""
        disposition, final, note = "investigate", "", ""
        if retail and all(r == "(base code)" or to_port.get(r) in {"FElysiumNpc", "FElysiumNpcBase"}
                          for r in retail) and not members[name].get("virtual"):
            disposition, final, note = "stay:troika body", "FElysiumNpc", f"retail {'/'.join(retail)}"
        elif owner != "-" and (not retail_port or retail_port in species or retail_port == "-"):
            user_set = users.get(name, set())
            if (row["kind"] == "method" and len(user_set) == 1 and next(iter(user_set)) in overrides
                    and next(iter(user_set)).split("::")[0] == owner):
                disposition, final = f"collapse:{next(iter(user_set)).split('::')[1]}", owner
            else:
                disposition, final = "move", owner
        elif owner == "-" and len(retail) == 1 and retail[0] in deferred:
            disposition, final, note = f"deferred:{deferred[retail[0]]}", "FElysiumNpc", f"{retail[0]} body"
        elif members[name].get("virtual"):
            disposition, final, note = "stay:virtual surface", "FElysiumNpc", "species override it"
        elif owner == "-" and not users.get(name) and retail_port in species:
            disposition, final, note = "move", retail_port, "no port caller (unwired); retail owner"
        elif owner == "-" and retail and all(to_port.get(r) in {"FElysiumNpc", "FElysiumNpcBase"} for r in retail):
            disposition, final, note = "stay:troika body", "FElysiumNpc", f"retail {'/'.join(retail)}"
        out.append({**row, "final_owner": final, "disposition": disposition, "packet": "-", "note": note})
    return out


def write_records(path: Path, columns: tuple[str, ...], rows: list[dict], title: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write(f"# {title}\n")
        writer = csv.DictWriter(out, columns, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
