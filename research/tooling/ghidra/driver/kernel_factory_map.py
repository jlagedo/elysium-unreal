"""Story 5 preflight: bounded factory evidence, never the proximity alias survey.

All inputs are read-only. Reports/listings belong below the work root. This command does not
activate new census or runtime registrations (story 5 step 2).

    uv run elysium research kernel_factory_map --inspect 103704f0
    uv run elysium research kernel_factory_map --out <work-root report.json>
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from kernel_skeleton import INS_RE  # noqa: E402
from probes.weapon_activity_survey import PEImage, PINNED_SHA256  # noqa: E402
from elysium_pipeline.paths import research_root, vtmb_root  # noqa: E402

MODULE = "vampire.dll"
ALLOCATOR = "100aa720"
DIRECTORS = {"scripted_sequence", "aiscripted_sequence", "aiscripted_schedule"}
REGISTERS = {"EAX", "EBX", "ECX", "EDX", "ESI", "EDI", "EBP", "ESP"}
MEMORY = re.compile(r"(?:(?:byte|word|dword|qword) ptr )?\[(EAX|EBX|ECX|EDX|ESI|EDI|EBP|ESP)(?: \+ (0x[0-9a-f]+))?\]")
PARTIAL = {"AL": "EAX", "AH": "EAX", "AX": "EAX", "BL": "EBX", "BH": "EBX", "BX": "EBX",
           "CL": "ECX", "CH": "ECX", "CX": "ECX", "DL": "EDX", "DH": "EDX", "DX": "EDX",
           "SI": "ESI", "DI": "EDI", "BP": "EBP", "SP": "ESP"}


class Unresolved(ValueError):
    """An evidence gap; never fall back to a nearby classname/vtable."""


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


@dataclass(frozen=True)
class Instruction:
    at: int
    op: str
    args: str


@dataclass
class Function:
    address: str
    size: int
    thunk: bool
    instructions: list[Instruction]
    listing: str


class Corpus:
    def __init__(self):
        root = research_root() / "ghidra" / "corpus"
        self.paths = {"corpus": root / "corpus.sqlite", "listing": root / "listing.sqlite",
                      "module": vtmb_root() / "Vampire" / "dlls" / MODULE}
        if digest(self.paths["module"]) != PINNED_SHA256:
            raise Unresolved("module hash differs from pinned vampire.dll")
        self.image = PEImage(self.paths["module"].read_bytes())
        self.db = sqlite3.connect(self.paths["corpus"].as_uri() + "?mode=ro", uri=True)
        self.db.row_factory = sqlite3.Row
        self.listing = sqlite3.connect(self.paths["listing"].as_uri() + "?mode=ro", uri=True)
        row = self.db.execute("SELECT * FROM meta WHERE module=?", (MODULE,)).fetchone()
        if row is None or row["sha256"] != PINNED_SHA256:
            raise Unresolved("corpus module provenance differs from pinned image")
        self.meta = dict(row)
        self.tables = {int(r["table_addr"], 16): r["cls"] for r in self.db.execute(
            "SELECT DISTINCT table_addr, cls FROM vtables WHERE module=? AND sub=0",
            (MODULE,))}

    def function(self, address: str) -> Function:
        address = address.lower().removeprefix("0x")
        row = self.db.execute("SELECT * FROM functions WHERE module=? AND addr=?",
                              (MODULE, address)).fetchone()
        listing = self.listing.execute("SELECT asm FROM listing WHERE module=? AND addr=?",
                                       (MODULE, address)).fetchone()
        if row is None or listing is None:
            raise Unresolved(f"{address}: absent function boundary or listing")
        instructions = []
        for line in listing[0].splitlines():
            match = INS_RE.fullmatch(line.strip())
            if not match:
                raise Unresolved(f"{address}: unrecognized listing line {line!r}")
            # The listing annotates literal string PUSHes. Its operand is still the address;
            # validate the string from the pinned image, not this annotation.
            args = (match[3] or "").split('  "', 1)[0]
            instructions.append(Instruction(int(match[1], 16), match[2], args))
        lo = int(address, 16)
        if not instructions or instructions[0].at != lo or any(
                not lo <= i.at < lo + row["size"] for i in instructions):
            raise Unresolved(f"{address}: listing/function extent mismatch")
        if [i.at for i in instructions] != sorted({i.at for i in instructions}):
            raise Unresolved(f"{address}: unordered/duplicate instruction boundaries")
        return Function(address, row["size"], bool(row["thunk"]), instructions, listing[0])

    def resolve(self, address: str) -> tuple[Function, list[str]]:
        hops = []
        while address not in hops and len(hops) < 16:
            function = self.function(address)
            if not function.thunk:
                return function, hops
            ins = function.instructions
            if len(ins) != 1 or ins[0].op != "JMP" or not re.fullmatch(r"0x[0-9a-f]{8}", ins[0].args):
                raise Unresolved(f"{address}: not a single direct-jump thunk")
            hops.append(address)
            address = ins[0].args[2:]
        raise Unresolved(f"cyclic/overlong thunk chain: {hops}")

    def candidates(self) -> dict[str, list[str]]:
        """Reference inventory only; a string reference is not a factory or liveness verdict."""
        found: dict[str, list[str]] = {}
        for row in self.db.execute(
                "SELECT DISTINCT s.text, r.func_addr FROM strings s JOIN string_refs r "
                "ON s.module=r.module AND s.addr=r.str_addr WHERE s.module=?", (MODULE,)):
            name = row["text"]
            if name.startswith(("npc_", "monster_")) or name == "scripted_target" or name in DIRECTORS:
                found.setdefault(name, []).append(row["func_addr"])
        return {name: sorted(set(addresses)) for name, addresses in sorted(found.items())}


def receiver(operand: str, registers: dict[str, int]) -> int | None:
    match = MEMORY.fullmatch(operand)
    if match and match[1] in registers:
        return registers[match[1]] + int(match[2] or "0", 16)
    return None


def move(ins: Instruction, registers: dict[str, int]) -> None:
    """Track only receiver aliases; loading a word is not copying a receiver pointer."""
    operands = ins.args.split(",")
    dest = operands[0]
    if dest in PARTIAL and ins.op not in ("TEST", "CMP", "PUSH"):
        registers.pop(PARTIAL[dest], None)
    if ins.op == "XCHG":
        for operand in operands:
            registers.pop(PARTIAL.get(operand, operand), None)
    if dest not in REGISTERS:
        return
    value = None
    if ins.op == "MOV" and len(operands) == 2:
        value = registers.get(operands[1])
    elif ins.op == "LEA" and len(operands) == 2:
        value = receiver(operands[1], registers)
    elif ins.op in ("ADD", "SUB") and len(operands) == 2 and dest in registers:
        if re.fullmatch(r"0x[0-9a-f]+", operands[1]):
            value = registers[dest] + int(operands[1], 16) * (1 if ins.op == "ADD" else -1)
    elif ins.op in ("TEST", "CMP", "PUSH"):
        return
    registers.pop(dest, None)
    if value is not None:
        registers[dest] = value


def check_null_arm(corpus: Corpus, instructions: list[Instruction], target: int, classname: str) -> None:
    tail = [(ins.op, ins.args) for ins in instructions if ins.at >= target]
    if len(tail) != 8 or tail[0] != ("XOR", "ESI,ESI"):
        raise Unresolved("unreviewed allocation-failure arm")
    if tail[1][0] != "PUSH" or not re.fullmatch(r"0x[0-9a-f]{8}", tail[1][1]):
        raise Unresolved("missing null-arm classname")
    if corpus.image.read_cstring_va(int(tail[1][1], 16)) != classname:
        raise Unresolved("null arm names a different class")
    if tail[2] != ("MOV", "ECX,ESI") or tail[3][0] != "MOV":
        raise Unresolved("null-arm receiver differs")
    register = tail[3][1].split(",")[0]
    if register not in {"EAX", "EDX"} or tail[3][1] != f"{register},dword ptr [ESI]":
        raise Unresolved("null-arm vtable receiver differs")
    if tail[4:] != [("CALL", f"dword ptr [{register} + 0x1a8]"),
                   ("MOV", "EAX,ESI"), ("POP", "ESI"), ("RET", "")]:
        raise Unresolved("unreviewed allocation-failure continuation")


def constructor(corpus: Corpus, address: str) -> dict:
    """Propose a constructor summary with explicit review obligations, not a new RE oracle.

    A final root write must occur unconditionally, return the original receiver and have no
    later call on that receiver. Branches crossing the write or alias changes fail closed.
    Calls before the final write cannot decide the final primary vtable.
    """
    function, hops = corpus.resolve(address)
    registers = {"ECX": 0}
    writes, root_calls, returns, branches = [], [], [], []
    for ins in function.instructions:
        if ins.op == "MOV":
            left, right = ins.args.split(",")
            if receiver(left, registers) == 0:
                if (not left.startswith("dword ptr") or not re.fullmatch(r"0x[0-9a-f]{8}", right)
                        or int(right, 16) not in corpus.tables):
                    raise Unresolved(f"{function.address}: unknown write to primary vtable at {ins.at:08x}")
                writes.append({"at": f"{ins.at:08x}", "vtable": right[2:],
                               "class": corpus.tables[int(right, 16)], "receiver": left})
        if ins.op == "CALL":
            if registers.get("ECX") == 0:
                root_calls.append(f"{ins.at:08x}")
            for reg in ("EAX", "ECX", "EDX"):
                registers.pop(reg, None)
        elif ins.op == "RET":
            returns.append((ins.at, registers.get("EAX")))
        elif ins.op.startswith("J"):
            if not re.fullmatch(r"0x[0-9a-f]{8}", ins.args):
                raise Unresolved(f"{function.address}: indirect constructor branch")
            branches.append((ins.at, int(ins.args, 16)))
        else:
            move(ins, registers)
    if not writes or not returns or any(offset != 0 for _, offset in returns):
        raise Unresolved(f"{function.address}: missing root vtable write or same-receiver return")
    final = writes[-1]
    at = int(final["at"], 16)
    # Branches must stay wholly before or after the final write, and must not change its
    # receiver alias. This admits the director's member-array loop without inventing a CFG join.
    if any(start < at < target or target <= at < start for start, target in branches):
        raise Unresolved(f"{function.address}: branch crosses primary-vtable construction")
    if any(ret < at for ret, _ in returns):
        raise Unresolved(f"{function.address}: return bypasses final write")
    root_reg = MEMORY.fullmatch(final["receiver"])[1]
    for start, target in branches:
        for ins in function.instructions:
            if min(start, target) <= ins.at <= max(start, target) and ins.op not in (
                    "TEST", "CMP", "PUSH") and ins.args.split(",")[0] == root_reg:
                raise Unresolved(f"{function.address}: branch mutates final receiver alias")
    return {"address": function.address, "thunks": hops, "size": function.size,
            "listing_sha256": hashlib.sha256(function.listing.encode()).hexdigest(),
            "primary_writes": writes, "final": final,
            "later_receiver_calls": [call for call in root_calls if int(call, 16) > at],
            "returns": [f"{ret:08x}" for ret, _ in returns]}


def factory(corpus: Corpus, address: str, classname: str) -> dict:
    function, hops = corpus.resolve(address)
    registers: dict[str, int] = {}
    vtables: set[str] = set()
    allocation, ctor, final, name_push, virtual_call = None, None, None, None, None
    null_target = None
    inline_writes, member_calls = [], []
    previous = None
    for ins in function.instructions:
        args = ins.args.split(",")
        if ins.op == "CALL" and re.fullmatch(r"0x[0-9a-f]{8}", ins.args):
            target, _ = corpus.resolve(ins.args[2:])
            if target.address == ALLOCATOR:
                if allocation or previous is None or previous.op != "PUSH" or not re.fullmatch(
                        r"0x[0-9a-f]+", previous.args):
                    raise Unresolved("not a single literal allocation")
                allocation = {"at": f"{ins.at:08x}", "size": previous.args,
                              "allocator": target.address}
                registers["EAX"] = 0
            elif allocation and ctor and registers.get("ECX", 0) > 0:
                member_calls.append({"at": f"{ins.at:08x}", "callee": target.address,
                                     "receiver_offset": registers["ECX"]})
                for reg in ("EAX", "ECX", "EDX"):
                    registers.pop(reg, None)
            elif allocation and registers.get("ECX") == 0 and ctor is None:
                ctor = constructor(corpus, ins.args[2:])
                final = ctor["final"]
                for reg in ("EAX", "ECX", "EDX"):
                    registers.pop(reg, None)
                registers["EAX"] = 0
            else:
                raise Unresolved(f"unexpected direct call {ins.at:08x} -> {target.address}")
            vtables.clear()
        elif ins.op == "CALL":
            match = re.fullmatch(r"dword ptr \[(\w+) \+ 0x1a8\]", ins.args)
            if not match or match[1] not in vtables or registers.get("ECX") != 0 or name_push is None:
                raise Unresolved(f"{ins.at:08x}: classname dispatch is not on allocated receiver "
                                 f"(registers={registers}, tables={sorted(vtables)}, push={name_push})")
            if virtual_call:
                raise Unresolved("multiple classname dispatches on successful path")
            virtual_call = f"{ins.at:08x}"
            for reg in ("EAX", "ECX", "EDX"):
                registers.pop(reg, None)
            vtables.clear()
        elif ins.op == "JZ":
            if null_target or not allocation or previous is None or previous.op != "TEST":
                raise Unresolved("unrecognized factory branch")
            tested = previous.args.split(",")
            if len(tested) != 2 or tested[0] != tested[1] or registers.get(tested[0]) != 0:
                raise Unresolved("factory branch is not allocation-null check")
            null_target = int(ins.args, 16)
        elif ins.op.startswith("J"):
            raise Unresolved("non-null-check factory branch")
        elif ins.op == "PUSH" and re.fullmatch(r"0x[0-9a-f]{8}", ins.args):
            name = corpus.image.read_cstring_va(int(ins.args, 16))
            if name == classname:
                name_push = f"{ins.at:08x}"
            elif allocation:
                raise Unresolved(f"unexpected post-allocation literal PUSH at {ins.at:08x}")
        elif ins.op == "RET":
            if not (allocation and ctor and final and virtual_call and null_target and registers.get("EAX") == 0):
                raise Unresolved("incomplete factory construction/receiver evidence")
            if null_target <= ins.at or null_target not in {i.at for i in function.instructions}:
                raise Unresolved("null branch does not target a separate instruction-bounded failure arm")
            if ctor["later_receiver_calls"] and not inline_writes:
                raise Unresolved("constructor calls receiver after final primary write; review needed")
            check_null_arm(corpus, function.instructions, null_target, classname)
            # Retail's failed-allocation arm still dereferences the null receiver. Preserve
            # this observation; it cannot identify a constructed object or a second class.
            return {"classname": classname, "factory": function.address, "factory_thunks": hops,
                    "factory_size": function.size, "factory_listing_sha256": hashlib.sha256(
                        function.listing.encode()).hexdigest(), "allocation": allocation,
                    "constructor": ctor, "final": final, "classname_push": name_push,
                    "classname_call": virtual_call, "null_arm": f"{null_target:08x}",
                    "inline_writes": inline_writes, "member_calls": member_calls}
        else:
            if ins.op == "MOV" and len(args) == 2:
                left, right = args
                if receiver(left, registers) == 0:
                    if (not left.startswith("dword ptr") or not re.fullmatch(r"0x[0-9a-f]{8}", right)
                            or int(right, 16) not in corpus.tables):
                        raise Unresolved("unknown inline primary vtable")
                    final = {"at": f"{ins.at:08x}", "vtable": right[2:],
                             "class": corpus.tables[int(right, 16)], "receiver": left}
                    inline_writes.append(final)
                if left in REGISTERS:
                    vtables.discard(left)
                    if receiver(right, registers) == 0:
                        vtables.add(left)
            elif ins.op not in ("CMP", "TEST", "PUSH"):
                for operand in args[:2] if ins.op == "XCHG" else args[:1]:
                    vtables.discard(PARTIAL.get(operand, operand))
            move(ins, registers)
        previous = ins
    raise Unresolved("factory has no supported successful return")


def survey(corpus: Corpus) -> dict:
    observations, unresolved = {}, {}
    for classname, candidates in corpus.candidates().items():
        for address in candidates:
            try:
                result = factory(corpus, address, classname)
                observations.setdefault(classname, []).append(result)
            except Unresolved as exc:
                unresolved.setdefault(classname, []).append({"address": address, "reason": str(exc)})
    return {"observations": observations, "unresolved_references": unresolved}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    corpus = Corpus()
    if args.inspect:
        function, hops = corpus.resolve(args.inspect.removeprefix("0x"))
        print(json.dumps({"address": function.address, "size": function.size, "thunks": hops}))
        print(function.listing)
        for ins in function.instructions:
            if ins.op == "PUSH" and re.fullmatch(r"0x[0-9a-f]{8}", ins.args):
                print(ins.args, repr(corpus.image.read_cstring_va(int(ins.args, 16))))
        return 0
    report = {"module": MODULE, "provenance": corpus.meta, "inputs": {
        key: {"path": str(path), "sha256": digest(path)} for key, path in corpus.paths.items()},
        **survey(corpus)}
    if args.out:
        out = args.out.resolve()
        if not out.is_relative_to(research_root().resolve()):
            raise Unresolved("generated report must stay under the work research root")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        addresses = set()
        for answers in report["observations"].values():
            for answer in answers:
                addresses.update([answer["factory"], answer["constructor"]["address"]])
                addresses.update(answer["constructor"]["thunks"])
                addresses.update(row["callee"] for row in answer["member_calls"])
        listings = []
        for address in sorted(addresses):
            function = corpus.function(address)
            listings.append(f"// {MODULE}:{address} size={function.size}\n{function.listing}")
        out.with_suffix(".listings.txt").write_text("\n".join(listings), encoding="utf-8")
        print(f"{len(report['observations'])} factory proposals; review required: {out}")
    else:
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
