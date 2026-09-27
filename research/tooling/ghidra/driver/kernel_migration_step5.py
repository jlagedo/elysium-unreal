"""Story 5 step 5: `CAI_BaseNPC` stands as `FElysiumNpcBase`, and `FElysiumNpc` is Troika beneath it.

Step 4 left `FElysiumNpc` carrying both retail layers: the base-layer and Troika-layer words, the
bodies of both (the base's often as a `Base*` helper the Troika body calls directly), one binding
class for both datamaps and one abstract descriptor. This check holds the tree to the step's
authored records:

* `moves-step5.tsv` is the move manifest: one row per member `FElysiumNpc` declares (its class body
  and the family `.inl` files it includes, the generated slot surface aside) and one per member of
  the mixed component aggregates (`ScheduleHost`, `Senses`, `Senses.Memory`,
  `Senses.Perception`). Its identity columns (kind, declaring file, defining files, the retail
  addresses the overlay and the definitions cite, and the retail layer those addresses and the
  bound words name) are re-derived from the accepted step-4 tree, so the record cannot drop a row.
  Each row carries a reviewed disposition: `base` onto `FElysiumNpcBase`, `rename:<Name>` onto it
  under the retail slot's name, `pair:<Name>` (a Troika override of a base virtual), `stay:<why>`
  on `FElysiumNpc`, `split:<aggregate>` for a component word, `chain:<Class>` onto the entity
  chain, or `deferred:<step>`;
* `fields-step5.tsv` is the binding manifest: every record of the `CAI_BaseNPC` datamap in the
  pinned replay, keyed by offset + member, with its final storage and the generated registration
  it must have on the base's own descriptor;
* at phase 5, every moved member is declared on `FElysiumNpcBase` (its header or a
  `ElysiumNpcBase<Family>.inl` the class includes) and nowhere on `FElysiumNpc`; a pair's Troika
  side is an `override` of a base virtual; every base binding row is generated in the base's own
  functions and not in the Troika's; the descriptor chain is `CAI_BaseNPC` -> the combat
  character and `CAI_BaseNPCTroika` -> `CAI_BaseNPC`;
* the step-4 checks still hold on their accepted tree, and the step-5 regression comparison,
  expectations and runtime receipts are pinned in `acceptance-step5.json`.

While phase 4 is current the step is in progress: `--check step5` validates the records against
the accepted step-4 tree and the pinned replay, reports the open dispositions, and refuses nothing
about the source.

    uv run elysium research kernel_migration --check step5
"""
from __future__ import annotations

import bisect
import collections
import csv
import json
import re
import tempfile
from pathlib import Path

import kernel_migration as km
from kernel_migration_audit import file_sha
from kernel_migration_inventory import mask_cpp
from kernel_migration_step4 import (_binding_functions, _code, _top_level, METHOD, FIELD, check_identity,
                                    module_definitions, verdict_addresses)
from elysium_pipeline.paths import repo_root, research_root

SOURCE = Path("Source/ElysiumUE")
PRIVATE = SOURCE / "Private"
SUBSTRATE = PRIVATE / "Substrate"
NPC_HEADER = SUBSTRATE / "ElysiumNpc.h"
BASE_HEADER = SUBSTRATE / "ElysiumNpcBase.h"
SHAPE_MAP = SUBSTRATE / "ElysiumNpcKernelShapeMap.cpp"
BINDINGS = SUBSTRATE / "ElysiumNpcKernelBindings.cpp"
REGISTRY = SUBSTRATE / "ElysiumNpcClasses.cpp"
LEDGER = Path("docs/vtmb/npc-kernel")
VERDICTS = Path("research/tooling/ghidra/driver/kernel_verdicts.tsv")
REPLAY = "ghidra/types/datamap_records-vampire.dll.json"
HISTORICAL_PATHS = ("Source/ElysiumUE", str(VERDICTS).replace("\\", "/"), str(LEDGER).replace("\\", "/"))
SLOTS_INL = ("ElysiumNpcKernelSlots.inl", "ElysiumNpcBaseSlots.inl", "ElysiumNpcSlots.inl")

MOVE_COLUMNS = ("member", "kind", "declared_in", "defined_in", "addresses", "anchor",
                "final_owner", "disposition", "packet", "note")
MOVE_IDENTITY = MOVE_COLUMNS[:6]
MOVE_DISPOSITION = re.compile(r"^(base|rename:\w+|pair:\w+|collapse:\w+|stay:.+|split:\w+|chain:\w+|deferred:(7|8|9|10)|investigate)$")
FIELD_COLUMNS = ("declaring_class", "offset", "member", "type", "flags", "external", "kind",
                 "current", "current_path", "final_owner", "final_path", "disposition", "packet", "note")
FIELD_IDENTITY = FIELD_COLUMNS[:7]
FIELD_DISPOSITION = re.compile(r"^(bind|save|output|input|unbound|investigate)$")
PACKETS = {"5c", "5d", "5e", "5f", "5g", "5h"}

# The mixed aggregates `FElysiumNpc` holds by value, by access path and struct type.
COMPONENTS = {
    "ScheduleHost": "FElysiumNpcScheduleHost",
    "Senses.Memory": "FElysiumNpcMemory",
    "Senses.Perception": "FElysiumNpcPerception",
    "Senses": "FElysiumNpcSenses",
}
COMPONENT_HEADERS = {"FElysiumNpcScheduleHost": "ElysiumNpcScheduleHost.h", "FElysiumNpcMemory": "ElysiumNpcSenses.h",
                     "FElysiumNpcPerception": "ElysiumNpcSenses.h", "FElysiumNpcSenses": "ElysiumNpcSenses.h"}

# The retail layers. Words: `layout.tsv`'s declaring layer. Code: the ledger's class namespace of
# the address, else the namespace both named neighbours agree on (the translation unit).
TROIKA = "CAI_BaseNPCTroika"
BASE = "CAI_BaseNPC"
SPECIES_PREFIXES = ("CNPC_", "CPayphone", "CNPCMaker", "CCine", "CAI_TestHull", "CGeneric", "CScripted",
                    "CAI_BaseHumanoid", "CAI_ExpressiveNPC", "CAI_BaseActor")


# ---- the retail layer of an address or a word ---------------------------------------------------

class Ledger:
    """Namespaces of `functions.md`, the slot table of `slots.md`, the layers of `layout.tsv`."""

    def __init__(self, root: Path):
        self.namespace: dict[str, str] = {}
        for line in (root / LEDGER / "functions.md").read_text(encoding="utf-8").splitlines():
            match = re.match(r"\| `0x(10[0-9a-f]{6})` \| (\S+?) \|", line)
            if match:
                name = match.group(2)
                self.namespace[match.group(1)] = name.split("::")[0] if "::" in name else ""
        named = sorted((int(a, 16), n) for a, n in self.namespace.items() if n)
        self._keys = [a for a, _ in named]
        self._values = [n for _, n in named]
        self.slots: dict[str, list[tuple[int, str]]] = collections.defaultdict(list)
        for line in (root / LEDGER / "slots.md").read_text(encoding="utf-8").splitlines():
            match = re.match(r"\| (\d+) \| ([^|]*)\| ([^|]*)\|", line)
            if not match:
                continue
            for column, layer in ((match.group(2), "base"), (match.group(3), "troika")):
                address = re.search(r"`0x(10[0-9a-f]{6})`", column)
                if address:
                    self.slots[address.group(1)].append((int(match.group(1)), layer))
        self.words: dict[int, str] = {}
        with (root / LEDGER / "layout.tsv").open(encoding="utf-8") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                if row["table"] == TROIKA and not any(c in row["member"] for c in ".[+"):
                    self.words[int(row["offset"], 16)] = row["layer"]

    def code_layer(self, address: str) -> str:
        """`base`, `troika`, `species` or `unknown`. The chain and the base's support objects
        (motor, navigator, senses, hints, sounds) sit at or below the base."""
        space = self.namespace.get(address, "")
        if not space:
            at = bisect.bisect_left(self._keys, int(address, 16))
            low = self._values[at - 1] if at > 0 else ""
            high = self._values[at] if at < len(self._values) else ""
            if not low or low != high:
                return "unknown"
            space = low
        if space == TROIKA:
            return "troika"
        if space.startswith(SPECIES_PREFIXES):
            return "species"
        return "base"

    def word_layer(self, offset: int) -> str:
        layer = self.words.get(offset, "")
        return "base" if layer == BASE else "troika" if layer == TROIKA else "chain" if layer else "unknown"


def _anchor(layers: set[str]) -> str:
    layers = {l for l in layers if l in ("base", "troika")}
    return "mixed" if len(layers) == 2 else next(iter(layers)) if layers else "-"


# ---- a class's declared members ---------------------------------------------------------------

def class_members(root: Path, header: Path, cls: str) -> dict[str, dict]:
    """Every member `cls` declares in its class body or a `Substrate/*.inl` the body includes:
    name -> kind/file/virtual/override. The generated slot surface is reported as its own file."""
    text = (root / header).read_text(encoding="utf-8-sig")
    start = re.search(rf"\bclass {cls}\b[^;{{]*\{{", text)
    if not start:
        return {}
    body_start = start.end()
    body = text[body_start:body_start + _class_end(mask_cpp(text[body_start:]))]
    sources = [(header.name, body)]
    for include in re.findall(r'#include "Substrate/(\w+\.inl)"', body):
        path = root / SUBSTRATE / include
        if path.is_file():
            sources.append((include, path.read_text(encoding="utf-8-sig")))
    members: dict[str, dict] = {}
    for name, source in sources:
        mask, spans = _top_level(source)
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
                entry = members.setdefault(method.group(1), {"kind": "method", "file": name})
                entry["virtual"] = entry.get("virtual") or "virtual" in decl or " override" in decl
                entry["override"] = entry.get("override") or " override" in head
            elif field:
                entry = members.setdefault(field.group(1), {"kind": "field", "file": name})
                entry.setdefault("type", _decl_type(decl, field.group(1)))
            else:
                continue
            entry.setdefault("files", set()).add(name)
    return members


DECL_QUALIFIERS = {"const", "mutable", "static", "inline", "constexpr", "volatile"}


def _decl_type(decl: str, name: str) -> str:
    """The last type identifier before `name` in a field declaration (`TArray<X>` -> `TArray`)."""
    head = decl.split("=")[0]
    head = head[:head.rfind(name)] if name in head else head
    head = re.sub(r"<[^<>]*(?:<[^<>]*>[^<>]*)*>", "", head)
    types = [t for t in re.findall(r"\b([A-Za-z_]\w*)\b", head) if t not in DECL_QUALIFIERS]
    return types[-1] if types else ""


def _class_end(mask: str) -> int:
    depth = 1
    for at, char in enumerate(mask):
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return at
    raise ValueError("unbalanced class body")


def find_struct(root: Path, struct: str) -> str:
    """The Substrate header that defines `struct`, or ''."""
    for path in sorted((root / SUBSTRATE).glob("*.h")):
        if re.search(rf"\b(?:struct|class) {struct}\b[^;{{]*\{{", path.read_text(encoding="utf-8-sig")):
            return path.name
    return ""


def struct_fields(root: Path, struct: str) -> dict[str, str]:
    """Field name -> declared type of `struct`, wherever in Substrate it is defined."""
    header = find_struct(root, struct)
    if not header:
        return {}
    text = mask_cpp((root / SUBSTRATE / header).read_text(encoding="utf-8-sig"))
    start = re.search(rf"\b(?:struct|class) {struct}\b[^;{{]*\{{", text)
    body = text[start.end():start.end() + _class_end(text[start.end():])]
    out = {}
    for name in struct_members(root, header, struct):
        match = re.search(rf"([A-Za-z_]\w*)(?:<[^;]*?>)?\s+{name}\s*(?:\[|=|;|\{{)", body)
        out[name] = match.group(1) if match else ""
    return out


def struct_members(root: Path, header: str, struct: str) -> set[str]:
    text = (root / SUBSTRATE / header).read_text(encoding="utf-8-sig")
    start = re.search(rf"\b(?:struct|class) {struct}\b[^;{{]*\{{", text)
    if not start:
        return set()
    mask = mask_cpp(text[start.end():])
    body = mask[:_class_end(mask)]
    found = set()
    depth = 0
    statement = []
    for char in body:
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            head = re.sub(r"\[[^\[\]]*(?:\[[^\[\]]*\][^\[\]]*)*\]", "[]", "".join(statement))
            if depth == 0 and "(" in head.split("=")[0]:
                # An inline method body ends without a `;`: it is its own declaration, and the
                # next member starts afresh.
                statement = []
                continue
        if depth == 0:
            statement.append(char)
            if char == ";":
                line = re.sub(r"\[[^\[\]]*(?:\[[^\[\]]*\][^\[\]]*)*\]", "[]", "".join(statement))
                statement = []
                if "(" in line.split("=")[0]:
                    continue
                match = re.search(r"\b(\w+)\s*(?:\[[^\]]*\])*\s*(?:=[^;]*|\{[^;]*\})?\s*;$", line.strip())
                if match:
                    found.add(match.group(1))
    return found


# ---- the move manifest ------------------------------------------------------------------------

SHAPE_ROW = re.compile(r"ELYSIUM_NPC_WORD(_NOTED|_PRIVATE|_CHAIN|_IMPLICIT|_ABSENT)?\(\s*(0x[0-9a-f]+)\s*,\s*([^\n]+)")


def shape_paths(root: Path) -> dict[str, list[int]]:
    """Port path (`Type::Member`) -> the offsets the NPC shape map binds it to."""
    out: dict[str, list[int]] = collections.defaultdict(list)
    for match in SHAPE_ROW.finditer((root / SHAPE_MAP).read_text(encoding="utf-8")):
        form, offset, arguments = match.group(1) or "", int(match.group(2), 16), match.group(3)
        if form in ("_IMPLICIT", "_ABSENT"):
            continue
        if form in ("_PRIVATE", "_CHAIN"):
            path = re.match(r'\s*"([^"]+)"', arguments).group(1)
        else:
            owner, member = [x.strip() for x in arguments.split(",")[:2]]
            path = f"{owner}::{member.rstrip(')')}"
        out[path].append(offset)
    return out


SUB_ACCESS = re.compile(r"\b(ScheduleHost|Senses\.Memory|Senses\.Perception|Senses)\.(\w+)")


def move_manifest(root: Path) -> list[dict]:
    """The identity columns of `moves-step5.tsv`, derived from the tree at `root`."""
    ledger = Ledger(root)
    members = class_members(root, NPC_HEADER, "FElysiumNpc")
    defs = module_definitions(root)
    addresses = verdict_addresses(root)
    defined: dict[str, set[str]] = collections.defaultdict(set)
    for d in defs:
        if d.symbol.startswith("FElysiumNpc::"):
            name = d.symbol.split("::", 1)[1]
            defined[name].add(d.path.rsplit("/", 1)[-1])
            if d.leading_address:
                addresses[name].add(d.leading_address)
    paths = shape_paths(root)
    rows = []
    for name, info in sorted(members.items()):
        if info["file"] in SLOTS_INL:
            continue   # the generated slot surface: `gen_kernel_shape` splits it by slot
        cited = sorted(a for a in addresses.get(name, ()) if ledger.code_layer(a) != "species")
        layers = {ledger.code_layer(a) for a in cited}
        words = paths.get(f"FElysiumNpc::{name}", [])
        layers |= {ledger.word_layer(o) for o in words}
        rows.append({"member": name, "kind": info["kind"], "declared_in": info["file"],
                     "defined_in": ",".join(sorted(defined.get(name, ()))) or "-",
                     "addresses": ",".join(cited) or "-", "anchor": _anchor(layers)})
    for access, struct in COMPONENTS.items():
        fields = struct_members(root, COMPONENT_HEADERS[struct], struct)
        if access == "Senses":
            fields -= {"Memory", "Perception"}
        for field in sorted(fields):
            words = paths.get(f"{struct}::{field}", [])
            rows.append({"member": f"{access}.{field}", "kind": "component-field",
                         "declared_in": COMPONENT_HEADERS[struct], "defined_in": "-",
                         "addresses": ",".join(f"+0x{o:04x}" for o in sorted(words)) or "-",
                         "anchor": _anchor({ledger.word_layer(o) for o in words})})
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
        disposition, owner = row["disposition"], row["final_owner"]
        if disposition.startswith(("stay:", "deferred:", "pair:")) and owner != "FElysiumNpc" \
                and not owner.startswith("FElysiumNpc::"):
            raise km.InvalidManifest(f"{path.name}: a staying member stays on FElysiumNpc: {row['member']}")
        if disposition in ("base",) or disposition.startswith("rename:"):
            if owner != "FElysiumNpcBase" and not owner.startswith("FElysiumNpcBase::"):
                raise km.InvalidManifest(f"{path.name}: a base move names FElysiumNpcBase: {row['member']}")
        if disposition.startswith("split:") and "::" not in owner:
            raise km.InvalidManifest(f"{path.name}: a split names its aggregate: {row['member']}")
    return rows


# ---- the binding manifest ----------------------------------------------------------------------

def base_datamap(replay: dict) -> list[dict]:
    """The identity columns of `fields-step5.tsv`: every named record of `CAI_BaseNPC`'s table."""
    rows = []
    for record in replay[BASE]["records"]:
        flags = record.get("flagNames") or []
        if "FUNCTIONTABLE" in flags or record.get("name") is None:
            continue
        kind = ("input" if "INPUT" in flags and (record["typeName"] == "void" or record["offset"] == 0)
                else "output" if "OUTPUT" in flags else "field")
        rows.append({"declaring_class": BASE, "offset": f"0x{record['offset']:04x}", "member": record["name"],
                     "type": record["typeName"], "flags": "|".join(flags),
                     "external": record.get("external") or "-", "kind": kind})
    return sorted(rows, key=lambda r: (int(r["offset"], 16), r["member"], r["external"]))


def read_fields(path: Path) -> list[dict]:
    rows = read_records(path, FIELD_COLUMNS, FIELD_DISPOSITION,
                        lambda r: (r["offset"], r["member"], r["external"]))
    for row in rows:
        kind, disposition = row["kind"], row["disposition"]
        if kind == "input" and disposition not in {"input", "investigate"}:
            raise km.InvalidManifest(f"{path.name}: an input binds an input body: {row['member']}")
        if kind == "output" and disposition not in {"output", "investigate"}:
            raise km.InvalidManifest(f"{path.name}: an output binds an output: {row['member']}")
        if kind == "field" and disposition in {"input", "output"}:
            raise km.InvalidManifest(f"{path.name}: a field binds storage: {row['member']}")
    return rows


# ---- phase-5 source checks ---------------------------------------------------------------------

def check_moves(rows: list[dict], root: Path) -> collections.Counter:
    troika = class_members(root, NPC_HEADER, "FElysiumNpc")
    base = class_members(root, BASE_HEADER, "FElysiumNpcBase")
    base_code = "\n".join(_code(root, p.relative_to(root)) for p in sorted((root / SUBSTRATE).glob("ElysiumNpcBase*.cpp")))
    holders = {"FElysiumNpc": troika, "FElysiumNpcBase": base}
    counts: collections.Counter = collections.Counter()
    for row in rows:
        name, disposition, owner = row["member"], row["disposition"], row["final_owner"]
        if disposition == "investigate":
            raise km.InvalidManifest(f"move row still under investigation: {name}")
        family = disposition.split(":")[0]
        if row["kind"] == "component-field":
            # final_owner is the word's access path from its holder: `FElysiumNpcBase::BaseMemory.Enemy`.
            if not resolve_path(root, holders, owner):
                raise km.InvalidManifest(f"{name}: {owner} does not resolve")
            if family == "split" and resolve_path(root, holders, f"FElysiumNpc::{name}"):
                raise km.InvalidManifest(f"{name} is still declared at its old path on FElysiumNpc")
        elif family in ("base", "rename"):
            new = disposition.split(":", 1)[1] if family == "rename" else name
            if new not in base:
                raise km.InvalidManifest(f"{new} is not declared on FElysiumNpcBase")
            if name in troika:
                raise km.InvalidManifest(f"{name} is still declared on FElysiumNpc ({troika[name]['file']})")
            if family == "rename" and name != new and name in base:
                raise km.InvalidManifest(f"renamed {name} is still declared on FElysiumNpcBase")
            if row["kind"] == "method" and row["defined_in"] != "-" and not re.search(
                    rf"\bFElysiumNpcBase::{new}\s*\(", base_code):
                raise km.InvalidManifest(f"FElysiumNpcBase::{new} is not defined in an ElysiumNpcBase*.cpp")
        elif family == "pair":
            new = disposition.split(":", 1)[1]
            if not base.get(new, {}).get("virtual"):
                raise km.InvalidManifest(f"pair {name}: FElysiumNpcBase declares no virtual {new}")
            if not troika.get(new, {}).get("override"):
                raise km.InvalidManifest(f"pair {name}: FElysiumNpc::{new} is not an override")
        elif family == "collapse":
            # A second port body folded into its survivor: gone from both layers, the survivor
            # declared on the base or up the entity chain (`AsNpc`, the `+0x98` word).
            survivor = disposition.split(":", 1)[1]
            if name in troika or name in base:
                raise km.InvalidManifest(f"collapsed {name} is still declared")
            if survivor not in base and not re.search(
                    rf"\b{survivor}\s*\(", _code(root, PRIVATE.parent / "Public/ElysiumEntity.h")):
                raise km.InvalidManifest(f"{name}'s survivor {survivor} is not declared")
        elif family == "chain":
            header = {"FElysiumCombatCharacter": PRIVATE.parent / "Public/ElysiumPlayer.h"}.get(owner)
            if header is None or not re.search(rf"\b{name}\b", _code(root, header)):
                raise km.InvalidManifest(f"{name} is not declared on {owner}")
            if name in troika:
                raise km.InvalidManifest(f"{name} is still declared on FElysiumNpc")
        elif name not in troika:
            raise km.InvalidManifest(f"{name} ({disposition}) left FElysiumNpc")
        counts[family] += 1
    return counts


def resolve_path(root: Path, holders: dict[str, dict], path: str) -> bool:
    """`Holder::A.B.C` names a declared field chain: A on the holder class, B on A's struct..."""
    holder, _, chain = path.partition("::")
    members = holders.get(holder)
    if members is None or not chain:
        return False
    parts = chain.split(".")
    entry = members.get(parts[0])
    if entry is None or entry["kind"] != "field":
        return False
    struct = entry.get("type", "")
    for part in parts[1:]:
        fields = struct_fields(root, struct)
        if part not in fields:
            return False
        struct = fields[part]
    return True


def check_fields(rows: list[dict], root: Path) -> collections.Counter:
    bindings = _code(root, BINDINGS)
    functions = _binding_functions(bindings)
    holders = {"FElysiumNpcBase": class_members(root, BASE_HEADER, "FElysiumNpcBase")}
    counts: collections.Counter = collections.Counter()
    for row in rows:
        disposition = row["disposition"]
        if disposition == "investigate":
            raise km.InvalidManifest(f"binding row still under investigation: {row['member']}")
        if disposition in ("bind", "save"):
            if row["final_owner"] != "FElysiumNpcBase" or not resolve_path(
                    root, holders, re.sub(r"\[\d+\]$", "", row["final_path"])):
                raise km.InvalidManifest(f"{BASE}::{row['member']} has no storage at {row['final_path']}")
            name = row["external"] if disposition == "bind" else row["member"]
            own = "AddNpcBaseFields" if disposition == "bind" else "AddNpcBaseSaveFields"
            other = "AddNpcFields" if disposition == "bind" else "AddNpcSaveFields"
            pattern = rf'TEXT\("{re.escape(name)}(?:\[\d+\])?"\)'
            if not re.search(pattern, functions.get(own, "")):
                raise km.InvalidManifest(f"{BASE}::{row['member']} is not generated in {own}")
            if re.search(pattern, functions.get(other, "")):
                raise km.InvalidManifest(f"{BASE}::{row['member']} is still generated on the Troika in {other}")
        elif disposition == "output" and f'TEXT("{row["external"]}")' not in bindings:
            raise km.InvalidManifest(f"{BASE} output {row['external']} is not generated")
        counts[disposition] += 1
    return counts


CONSUMER_COLUMNS = ("path", "consumer", "sites", "disposition", "evidence", "note")
CONSUMER_IDENTITY = CONSUMER_COLUMNS[:3]
CONSUMER_DISPOSITION = re.compile(r"^(as-npc-base|as-npc|as-npc:(port|unrecovered)|mixed:\d+/\d+|test|investigate)$")


def consumer_manifest(root: Path) -> list[dict]:
    """The identity columns of `consumers-step5.tsv`: every `AsNpc()` call site, by enclosing
    definition (declarations aside)."""
    from kernel_migration_inventory import source_inventory
    _, sites, _, _ = source_inventory(root)
    counts: collections.Counter = collections.Counter()
    for site in sites:
        if site["kind"] == "participation" and not site["path"].endswith(("/ElysiumEntity.h", "/ElysiumNpc.h")):
            counts[(site["path"].removeprefix("Source/ElysiumUE/"), site["consumer"])] += 1
    return [{"path": p, "consumer": c, "sites": str(n)} for (p, c), n in sorted(counts.items())]


def read_consumers(path: Path) -> list[dict]:
    lines = [l for l in path.read_text(encoding="utf-8").splitlines() if l and not l.startswith("#")]
    rows = list(csv.DictReader(lines, delimiter="\t"))
    if not rows or tuple(rows[0].keys()) != CONSUMER_COLUMNS:
        raise km.InvalidManifest(f"{path.name}: unexpected columns")
    for row in rows:
        if not CONSUMER_DISPOSITION.match(row["disposition"]):
            raise km.InvalidManifest(f"{path.name}: bad disposition: {row}")
    return rows


def check_consumers(rows: list[dict], root: Path) -> collections.Counter:
    """A retail `+0x94 m_pBaseNPC` test calls `AsNpcBase()`: its definition (possibly moved to the
    base, under the same method name) spells it as many times as the record says."""
    from kernel_migration_inventory import definitions
    defs: dict[str, list] = collections.defaultdict(list)
    for path in sorted((root / PRIVATE).rglob("*.cpp")):
        rel = path.relative_to(root).as_posix()
        for d in definitions(path.read_text(encoding="utf-8-sig"), rel):
            defs[d.symbol.split("::", 1)[1]].append((d, path))
    counts: collections.Counter = collections.Counter()
    for row in rows:
        disposition = row["disposition"]
        if disposition == "investigate":
            raise km.InvalidManifest(f"consumer still under investigation: {row['consumer']}")
        wanted = (int(row["sites"]) if disposition == "as-npc-base"
                  else int(disposition.split(":")[1].split("/")[0]) if disposition.startswith("mixed:") else 0)
        if wanted and row["consumer"].startswith("declaration/table in "):
            # A site outside any named definition (a lambda, a free helper): its file spells it.
            text = _code(root, Path("Source/ElysiumUE") / row["path"])
            if len(re.findall(r"\bAsNpcBase\s*\(", text)) < wanted:
                raise km.InvalidManifest(f"consumer {row['consumer']} does not test AsNpcBase()")
            counts[disposition.split(":")[0]] += 1
            continue
        if wanted:
            owner, _, method = row["consumer"].partition("::")
            found = [(d, p) for d, p in defs.get(method, ()) if d.symbol.split("::")[0] in (owner, "FElysiumNpcBase")]
            if len(found) != 1:
                raise km.InvalidManifest(f"consumer {row['consumer']}: {len(found)} definitions")
            d, p = found[0]
            body = mask_cpp(p.read_text(encoding="utf-8-sig"))[d.start:d.end]
            if len(re.findall(r"\bAsNpcBase\s*\(", body)) < wanted:
                raise km.InvalidManifest(f"consumer {row['consumer']} does not test AsNpcBase() {wanted}x")
        counts[disposition.split(":")[0]] += 1
    return counts


def check_registry(root: Path) -> None:
    text = _code(root, REGISTRY)
    if not re.search(r'RegisterAbstract\(\s*(?:TEXT\()?"CAI_BaseNPC"\)?\s*,\s*ElysiumCombatCharacterClassName\(\)', text):
        raise km.InvalidManifest("CAI_BaseNPC is not registered on the combat character")
    if not re.search(r'RegisterAbstract\(\s*(?:TEXT\()?"CAI_BaseNPCTroika"\)?\s*,\s*(?:TEXT\()?"CAI_BaseNPC"', text):
        raise km.InvalidManifest("CAI_BaseNPCTroika is not registered under CAI_BaseNPC")
    header = _code(root, NPC_HEADER)
    if not re.search(r"\bclass FElysiumNpc\s*:\s*public FElysiumNpcBase\b", header):
        raise km.InvalidManifest("FElysiumNpc does not derive from FElysiumNpcBase")
    if not re.search(r"\bclass FElysiumNpcBase\s*:\s*public FElysiumScriptedCharacter\b", _code(root, BASE_HEADER)):
        raise km.InvalidManifest("FElysiumNpcBase does not derive from FElysiumScriptedCharacter")


def check_overlay_owners(rows: list[dict], root: Path) -> int:
    """No overlay target names `FElysiumNpc::<member>` for a member this step moved to the base."""
    moved = {r["member"] for r in rows if r["disposition"] == "base" or r["disposition"].startswith("rename:")}
    stale = []
    for line in (root / VERDICTS).read_text(encoding="utf-8").splitlines():
        cells = line.split("\t")
        if len(cells) > 3 and re.fullmatch(r"[0-9a-f]{8}", cells[0]):
            target = cells[3].removeprefix("hand:")
            target = target.rsplit(":", 1)[-1] if re.match(r"^[\w/]+\.(?:cpp|h|inl):", target) else target
            if target.startswith("FElysiumNpc::") and target.split("::", 1)[1] in moved:
                stale.append(f"{cells[0]} {cells[3]}")
    if stale:
        raise km.InvalidManifest("overlay targets name a member moved to FElysiumNpcBase: " + ", ".join(stale[:20]))
    return len(moved)


# ---- the step ----------------------------------------------------------------------------------

def check_step5(directory: Path | None = None) -> dict:
    """While step 5 is the working phase the current tree is checked. Once its commit is recorded
    (`manifest.json` `history.step5.commit`), the accepted tree is."""
    directory = directory or repo_root() / km.STORY
    manifest, _, _ = km.load(directory)
    accepted = manifest.get("history", {}).get("step5", {}).get("commit", "")
    if not accepted:
        if manifest["phase"] > 5:
            raise km.InvalidManifest("a later phase needs history.step5.commit")
        return _check_step5(directory, repo_root())
    with tempfile.TemporaryDirectory(prefix="step5-accepted-") as scratch:
        return _check_step5(directory, km.historical_source(accepted, Path(scratch), HISTORICAL_PATHS))


def _check_step5(directory: Path, tree: Path) -> dict:
    manifest, _, _ = km.load(directory)
    commit = manifest.get("history", {}).get("step4", {}).get("commit", "")
    if not commit:
        raise km.InvalidManifest("step 5 needs the accepted step-4 tree (history.step4.commit)")
    moves = read_moves(directory / "moves-step5.tsv")
    fields = read_fields(directory / "fields-step5.tsv")
    consumers = read_consumers(directory / "consumers-step5.tsv")
    replay = json.loads((research_root() / REPLAY).read_text(encoding="utf-8"))
    check_identity(fields, base_datamap(replay), FIELD_IDENTITY, "binding manifest")
    with tempfile.TemporaryDirectory(prefix="step5-before-") as scratch:
        before = km.historical_source(commit, Path(scratch), HISTORICAL_PATHS)
        check_identity(moves, move_manifest(before), MOVE_IDENTITY, "move manifest")
        check_identity(consumers, consumer_manifest(before), CONSUMER_IDENTITY, "consumer record")
        if manifest["phase"] < 5:
            return {"pending": True, "move_rows": len(moves), "field_rows": len(fields),
                    "consumer_rows": len(consumers),
                    "moves": dict(collections.Counter(r["disposition"].split(":")[0] for r in moves)),
                    "fields": dict(collections.Counter(r["disposition"] for r in fields)),
                    "consumers": dict(collections.Counter(r["disposition"].split(":")[0] for r in consumers))}
        from kernel_migration_step1 import check_overlay_targets
        from kernel_migration_step4 import check_qualified_overlay_targets
        removed = check_overlay_targets(before, tree)
        qualified = check_qualified_overlay_targets(before, tree)
    from kernel_migration_step4 import check_step4
    step4 = check_step4(directory)
    check_registry(tree)
    counts = {"moves": dict(check_moves(moves, tree)), "fields": dict(check_fields(fields, tree)),
              "consumers": dict(check_consumers(consumers, tree)),
              "moved_overlay_members": check_overlay_owners(moves, tree),
              "removed_symbols": removed, "qualified_overlay_targets": qualified}
    record = json.loads((directory / "acceptance-step5.json").read_text(encoding="utf-8"))
    if record.get("scope") != "step-5-base-troika-split":
        raise km.InvalidManifest("acceptance-step5.json does not record step 5")
    artifacts = {}
    for name in ("delta", "runtime", "cheap_checks"):
        ref = record["artifacts"][name]
        path = Path(ref["path"])
        if not path.is_absolute() or not path.is_file() or file_sha(path) != ref["sha256"]:
            raise km.InvalidManifest(f"step-5 evidence changed/missing: {name}")
        artifacts[name] = json.loads(path.read_text(encoding="utf-8-sig"))
    delta = artifacts["delta"]
    if not delta.get("comparison_passed") or delta.get("differences"):
        raise km.InvalidManifest("step-5 regression comparison has unmatched differences")
    expectations = json.loads((directory / "expectations/step-5.json").read_text(encoding="utf-8"))
    unused = {c["id"] for c in expectations["changes"]} - set(delta.get("applied_expectations", []))
    if unused:
        raise km.InvalidManifest("unconsumed step-5 expectations: " + ", ".join(sorted(unused)))
    if len(artifacts["runtime"]) != 4 or any(r["exit"] for r in artifacts["runtime"]):
        raise km.InvalidManifest("step-5 runtime gate incomplete/failing")
    if not artifacts["cheap_checks"] or any(r["exit"] for r in artifacts["cheap_checks"]):
        raise km.InvalidManifest("step-5 cheap/Python gates incomplete/failing")
    return {**counts, "step4": {k: v for k, v in step4.items() if k != "step3"}}


def write_records(path: Path, columns: tuple[str, ...], rows: list[dict], title: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write(f"# {title}\n")
        writer = csv.DictWriter(out, columns, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
