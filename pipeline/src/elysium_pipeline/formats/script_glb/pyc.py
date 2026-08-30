"""The Python 2.1 marshal stream and bytecode of a compiled script companion.

A `.pyc` is a four-byte magic word, a four-byte source timestamp and one marshalled code object.
The reader below decodes that stream field by field and records a byte claim for every field it
read, including the field's own type-code byte, so the unit's ledger and the record it published
name the same place. Nothing is skipped: a type code outside the 2.1 set stops the decode with
the offset that carried it rather than resynchronising past it.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import struct
from typing import Any

from elysium_pipeline.formats.script_glb import opcodes

#: Marshal type codes, as Python 2.1's `marshal.c` writes them.
TYPE_NULL = 0x30          # '0'
TYPE_NONE = 0x4E          # 'N'
TYPE_ELLIPSIS = 0x2E      # '.'
TYPE_INT = 0x69           # 'i'
TYPE_INT64 = 0x49         # 'I'
TYPE_FLOAT = 0x66         # 'f'
TYPE_COMPLEX = 0x78       # 'x'
TYPE_LONG = 0x6C          # 'l'
TYPE_STRING = 0x73        # 's'
TYPE_INTERNED = 0x74      # 't'
TYPE_STRINGREF = 0x52     # 'R'
TYPE_UNICODE = 0x75       # 'u'
TYPE_TUPLE = 0x28         # '('
TYPE_LIST = 0x5B          # '['
TYPE_DICT = 0x7B          # '{'
TYPE_CODE = 0x63          # 'c'
TYPE_UNKNOWN = 0x3F       # '?'

#: The whole 2.1 set, so a code outside it is recognisable as outside it.
KNOWN_TYPES = frozenset(
    {
        TYPE_NULL, TYPE_NONE, TYPE_ELLIPSIS, TYPE_INT, TYPE_INT64, TYPE_FLOAT, TYPE_COMPLEX,
        TYPE_LONG, TYPE_STRING, TYPE_INTERNED, TYPE_STRINGREF, TYPE_UNICODE, TYPE_TUPLE, TYPE_LIST,
        TYPE_DICT, TYPE_CODE, TYPE_UNKNOWN,
    }
)

#: The marker `r_object` returns for `TYPE_NULL`; it ends a marshalled dictionary.
_NULL = object()

#: A marshalled string is its type-code byte and a four-byte length before its payload.
_STRING_HEADER = 5


class PycDecodeError(ValueError):
    """The companion is not a Python 2.1 marshal stream."""


@dataclass(frozen=True, slots=True)
class Claim:
    """One byte range of the companion and the record that paid for it."""

    offset: int
    length: int
    state: str
    owner: str


@dataclass(slots=True)
class PycFile:
    """One decoded companion: its header, its code tree and the proof of its bytes."""

    magic: int
    magic_bytes: str
    magic_expected: bool
    mtime: int
    code: dict[str, Any]
    claims: list[Claim] = field(default_factory=list)
    anomalies: list[dict[str, Any]] = field(default_factory=list)
    typed_unidentified: list[dict[str, Any]] = field(default_factory=list)
    omitted_proven: list[dict[str, Any]] = field(default_factory=list)


def _latin1(raw: bytes) -> str:
    """One character per byte, so a marshalled 8-bit string survives JSON unchanged."""

    return raw.decode("latin-1")


class _Reader:
    """A cursor over the marshal stream that claims every byte it consumes."""

    def __init__(self, data: bytes, path: str) -> None:
        self.data = data
        self.path = path
        self.position = 0
        self.claims: list[Claim] = []
        self.strings: list[str] = []
        self.typed_unidentified: list[dict[str, Any]] = []
        self.omitted_proven: list[dict[str, Any]] = []

    # -- primitives -------------------------------------------------------------------

    def _take(self, count: int, owner: str) -> bytes:
        end = self.position + count
        if count < 0 or end > len(self.data):
            raise PycDecodeError(
                f"{self.path}: {owner} needs {count} bytes at {self.position}, "
                f"the member holds {len(self.data) - self.position}"
            )
        raw = self.data[self.position:end]
        self.position = end
        return raw

    def claim(self, offset: int, length: int, owner: str, state: str = "mapped") -> None:
        if length:
            self.claims.append(Claim(offset, length, state, owner))

    def _short(self, owner: str) -> int:
        start = self.position
        value = struct.unpack("<H", self._take(2, owner))[0]
        self.claim(start, 2, owner)
        return value - 0x10000 if value & 0x8000 else value

    def _long(self, owner: str) -> int:
        value = struct.unpack("<I", self._take(4, owner))[0]
        return value - 0x100000000 if value & 0x80000000 else value

    # -- objects ----------------------------------------------------------------------

    def read_object(self, owner: str) -> Any:
        """One marshalled object, claiming its type code and every byte beneath it."""

        start = self.position
        code = self._take(1, owner)[0]
        if code not in KNOWN_TYPES:
            raise PycDecodeError(
                f"{self.path}: byte {start} carries marshal type code "
                f"0x{code:02x} ({chr(code)!r}), which Python 2.1 does not write"
            )
        if code == TYPE_NULL:
            self.claim(start, 1, owner)
            return _NULL
        if code == TYPE_NONE:
            self.claim(start, 1, owner)
            return None
        if code == TYPE_ELLIPSIS:
            self.claim(start, 1, owner)
            return {"marshalType": "ellipsis"}
        if code == TYPE_UNKNOWN:
            # `marshal` writes '?' for a value it could not serialise at all; the object it
            # stood for is gone from the stream, so it is carried as what it is.
            self.claim(start, 1, owner)
            self.typed_unidentified.append(
                {"field": owner, "sourceOffset": start, "marshalType": "unknown", "reason":
                 "the compiler marshalled a value it could not serialise"}
            )
            return {"marshalType": "unknown"}
        if code == TYPE_INT:
            value = self._long(owner)
            self.claim(start, 5, owner)
            return value
        if code == TYPE_INT64:
            low = struct.unpack("<I", self._take(4, owner))[0]
            high = struct.unpack("<i", self._take(4, owner))[0]
            self.claim(start, 9, owner)
            return (high << 32) | low
        if code == TYPE_FLOAT:
            size = self._take(1, owner)[0]
            text = _latin1(self._take(size, owner))
            self.claim(start, 2 + size, owner)
            try:
                return float(text)
            except ValueError as error:
                raise PycDecodeError(f"{self.path}: {owner} holds float {text!r}") from error
        if code == TYPE_COMPLEX:
            size = self._take(1, owner)[0]
            real = _latin1(self._take(size, owner))
            size2 = self._take(1, owner)[0]
            imaginary = _latin1(self._take(size2, owner))
            self.claim(start, 3 + size + size2, owner)
            return {"marshalType": "complex", "real": real, "imaginary": imaginary}
        if code == TYPE_LONG:
            count = self._long(owner)
            digits = abs(count)
            raw = self._take(2 * digits, owner)
            self.claim(start, 5 + 2 * digits, owner)
            value = 0
            for index in range(digits):
                value |= struct.unpack_from("<H", raw, 2 * index)[0] << (15 * index)
            return -value if count < 0 else value
        if code in (TYPE_STRING, TYPE_INTERNED):
            size = self._long(owner)
            if size < 0:
                raise PycDecodeError(f"{self.path}: {owner} declares {size} string bytes")
            text = _latin1(self._take(size, owner))
            self.claim(start, 5 + size, owner)
            if code == TYPE_INTERNED:
                self.strings.append(text)
            return text
        if code == TYPE_UNICODE:
            size = self._long(owner)
            if size < 0:
                raise PycDecodeError(f"{self.path}: {owner} declares {size} unicode bytes")
            raw = self._take(size, owner)
            self.claim(start, 5 + size, owner)
            try:
                return {"marshalType": "unicode", "value": raw.decode("utf-8")}
            except UnicodeDecodeError as error:
                raise PycDecodeError(
                    f"{self.path}: {owner} is not the UTF-8 marshal writes for unicode"
                ) from error
        if code == TYPE_STRINGREF:
            index = self._long(owner)
            self.claim(start, 5, owner)
            if not 0 <= index < len(self.strings):
                raise PycDecodeError(
                    f"{self.path}: {owner} refers to interned string {index} of "
                    f"{len(self.strings)}"
                )
            return self.strings[index]
        if code in (TYPE_TUPLE, TYPE_LIST):
            size = self._long(owner)
            if size < 0:
                raise PycDecodeError(f"{self.path}: {owner} declares {size} elements")
            self.claim(start, 5, owner)
            return [self.read_object(f"{owner}[{index}]") for index in range(size)]
        if code == TYPE_DICT:
            self.claim(start, 1, owner)
            pairs: dict[str, Any] = {}
            ordinal = 0
            while True:
                key = self.read_object(f"{owner}.key[{ordinal}]")
                if key is _NULL:
                    break
                pairs[str(key)] = self.read_object(f"{owner}[{key!s}]")
                ordinal += 1
            return {"marshalType": "dict", "entries": pairs}
        if code == TYPE_CODE:
            self.claim(start, 1, owner)
            return self._read_code(owner, start)
        raise PycDecodeError(                                   # defensive: KNOWN_TYPES guards
            f"{self.path}: byte {start} carries marshal type code 0x{code:02x} unread"
        )

    def _read_code(self, owner: str, type_offset: int) -> dict[str, Any]:
        argcount = self._short(f"{owner}.argcount")
        nlocals = self._short(f"{owner}.nlocals")
        stacksize = self._short(f"{owner}.stacksize")
        flags = self._short(f"{owner}.flags")
        code_field = self.position
        bytecode = self.read_object(f"{owner}.code")
        consts = self.read_object(f"{owner}.consts")
        names = self.read_object(f"{owner}.names")
        varnames = self.read_object(f"{owner}.varnames")
        freevars = self.read_object(f"{owner}.freevars")
        cellvars = self.read_object(f"{owner}.cellvars")
        filename = self.read_object(f"{owner}.filename")
        name = self.read_object(f"{owner}.name")
        firstlineno = self._short(f"{owner}.firstlineno")
        lnotab = self.read_object(f"{owner}.lnotab")
        for label, value in (("code", bytecode), ("lnotab", lnotab)):
            if not isinstance(value, str):
                raise PycDecodeError(f"{self.path}: {owner}.{label} is not a marshalled string")
        for label, value in (
            ("consts", consts), ("names", names), ("varnames", varnames),
            ("freevars", freevars), ("cellvars", cellvars),
        ):
            if not isinstance(value, list):
                raise PycDecodeError(f"{self.path}: {owner}.{label} is not a marshalled tuple")
        line_table = _decode_lnotab(lnotab, firstlineno)
        return {
            "sourceOffset": type_offset,
            "path": owner,
            "argcount": argcount,
            "nlocals": nlocals,
            "stacksize": stacksize,
            "flags": flags,
            "codeLength": len(bytecode),
            "codeOffset": code_field + _STRING_HEADER,
            "consts": consts,
            "names": names,
            "varnames": varnames,
            "freevars": freevars,
            "cellvars": cellvars,
            "filename": filename if isinstance(filename, str) else None,
            "name": name if isinstance(name, str) else None,
            "firstlineno": firstlineno,
            "lnotab": line_table["pairs"],
            "instructions": self._disassemble(
                owner, bytecode, code_field + _STRING_HEADER, consts, names, varnames,
                freevars, cellvars, line_table["lines"]
            ),
        }

    def _disassemble(
        self,
        owner: str,
        bytecode: str,
        code_offset: int,
        consts: list[Any],
        names: list[Any],
        varnames: list[Any],
        freevars: list[Any],
        cellvars: list[Any],
        lines: dict[int, int],
    ) -> list[dict[str, Any]]:
        """The 2.1 opcode table applied to one code string, instruction by instruction."""

        raw = bytecode.encode("latin-1")
        free = list(cellvars) + list(freevars)
        out: list[dict[str, Any]] = []
        position = 0
        current = lines.get(0)
        extended = 0
        while position < len(raw):
            offset = position
            opcode = raw[position]
            position += 1
            name = opcodes.OPNAME.get(opcode)
            argument: int | None = None
            if opcode >= opcodes.HAVE_ARGUMENT:
                if position + 2 > len(raw):
                    raise PycDecodeError(
                        f"{self.path}: {owner}.code ends inside the argument at {offset}"
                    )
                argument = raw[position] | (raw[position + 1] << 8)
                position += 2
            if name is None:
                # The instruction is typed -- its number and its argument width are what the
                # format states -- but 2.1 gives the number no name, so it is carried as such.
                self.typed_unidentified.append(
                    {
                        "field": f"{owner}.code.instruction[{offset}]",
                        "sourceOffset": offset,
                        "opcode": opcode,
                        "reason": "the opcode is outside the Python 2.1 table",
                    }
                )
            current = lines.get(offset, current)
            value = self._argument_value(
                opcode, argument, extended, consts, names, varnames, free
            )
            out.append(
                {
                    "offset": offset,
                    "sourceOffset": code_offset + offset,
                    "opcode": opcode,
                    "name": name,
                    "arg": argument,
                    "argValue": value,
                    "line": current,
                }
            )
            extended = (extended | argument) << 16 if name == "EXTENDED_ARG" else 0
        return out

    def _argument_value(
        self,
        opcode: int,
        argument: int | None,
        extended: int,
        consts: list[Any],
        names: list[Any],
        varnames: list[Any],
        free: list[Any],
    ) -> Any:
        if argument is None:
            return None
        full = argument | extended
        if opcode in opcodes.HAS_CONST:
            if not 0 <= full < len(consts):
                return None
            const = consts[full]
            if isinstance(const, dict) and "instructions" in const:
                return {"code": const.get("name"), "firstlineno": const.get("firstlineno")}
            return const
        if opcode in opcodes.HAS_NAME:
            return names[full] if 0 <= full < len(names) else None
        if opcode in opcodes.HAS_LOCAL:
            return varnames[full] if 0 <= full < len(varnames) else None
        if opcode in opcodes.HAS_FREE:
            return free[full] if 0 <= full < len(free) else None
        if opcode in opcodes.HAS_COMPARE:
            return opcodes.CMP_OP[full] if 0 <= full < len(opcodes.CMP_OP) else None
        if opcode in opcodes.HAS_LINE:
            # The operand *is* the line, and the instruction already publishes `line`; saying
            # it twice would put one datum in two places.
            return None
        return None


def _decode_lnotab(lnotab: str, firstlineno: int) -> dict[str, Any]:
    """The compiler's line table: `(byteIncrement, lineIncrement)` pairs and the map they make."""

    raw = lnotab.encode("latin-1")
    if len(raw) % 2:
        raise PycDecodeError(f"lnotab holds {len(raw)} bytes, which is not a pair table")
    pairs: list[dict[str, int]] = []
    lines: dict[int, int] = {0: firstlineno}
    address, line = 0, firstlineno
    for index in range(0, len(raw), 2):
        byte_increment, line_increment = raw[index], raw[index + 1]
        pairs.append({"byteIncrement": byte_increment, "lineIncrement": line_increment})
        address += byte_increment
        line += line_increment
        lines[address] = line
    return {"pairs": pairs, "lines": lines}


def decode_pyc(data: bytes, path: str) -> PycFile:
    """Decode one companion completely, or say which byte stopped the decode."""

    if len(data) < 9:
        raise PycDecodeError(f"{path} is {len(data)} bytes; a companion holds a header and a code")
    magic = struct.unpack_from("<H", data, 0)[0]
    magic_bytes = data[:4]
    mtime = struct.unpack_from("<I", data, 4)[0]
    reader = _Reader(data, path)
    reader.position = 8
    reader.claim(0, 4, "pyc.magic")
    reader.claim(4, 4, "pyc.mtime")
    code = reader.read_object("pyc.code")
    if not isinstance(code, dict) or "instructions" not in code:
        raise PycDecodeError(f"{path}: the marshal stream does not open with a code object")
    anomalies: list[dict[str, Any]] = []
    expected = magic_bytes == opcodes.PYTHON_21_MAGIC_BYTES
    if not expected:
        anomalies.append(
            {
                "role": "pyc-magic-mismatch",
                "sourceOffset": 0,
                "magic": magic,
                "magicBytes": magic_bytes.hex(),
                "expected": opcodes.PYTHON_21_MAGIC,
            }
        )
    if reader.position != len(data):
        trailing = len(data) - reader.position
        reader.claim(reader.position, trailing, "pyc.trailing", "omitted-proven")
        anomalies.append(
            {
                "role": "trailing-bytes-after-code",
                "sourceOffset": reader.position,
                "length": trailing,
                "bytes": data[reader.position:reader.position + 32].hex(),
            }
        )
        # `import` unmarshals one code object and stops, so a byte after it is storage the
        # interpreter never reads. That is a proven omission, and the anomaly above carries the
        # bytes themselves as the evidence for it.
        reader.omitted_proven.append(
            {
                "field": "pyc.trailing",
                "sourceOffset": reader.position,
                "length": trailing,
                "reason": (
                    f"{trailing} byte(s) follow the marshalled code object; the 2.1 loader "
                    "stops at the end of the code object and never reads them"
                ),
            }
        )
    return PycFile(
        magic=magic,
        magic_bytes=magic_bytes.hex(),
        magic_expected=expected,
        mtime=mtime,
        code=code,
        claims=reader.claims,
        anomalies=anomalies,
        typed_unidentified=reader.typed_unidentified,
        omitted_proven=reader.omitted_proven,
    )


def walk_codes(code: dict[str, Any]):
    """Every code object of the tree, the root first, then each nested one in source order."""

    yield code
    for const in code.get("consts") or []:
        if isinstance(const, dict) and "instructions" in const:
            yield from walk_codes(const)
