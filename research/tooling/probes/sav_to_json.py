#!/usr/bin/env python3
"""Decode a whole VtMB `.sav` savegame into one JSON document.

Reads a save file the user points it at; touches no game install. Format reference:
`docs/vtmb/savegame_format.md`; container and section decoder:
`pipeline/src/elysium_pipeline/formats/sav.py`.

Usage
  uv run elysium research sav_to_json <file.sav> <out.json>

Every byte of every section is accounted for: a region the decoder cannot type is emitted as
hex under an `_unparsed` key rather than dropped, and the run prints how many such regions
there were.

Field types come from the retail datamaps where the corpus recovered them
(`$ELYSIUM_WORK_ROOT/research/ghidra/corpus/fields-*.jsonl`, `note = "fieldType N"`), keyed by
the class group a field is written under. VtMB's `fieldtype_t` has no QUATERNION or TICK slot,
so its numbering runs: 1 FLOAT, 2 STRING, 3 VECTOR, 4 INTEGER, 5 BOOLEAN, 6 SHORT,
7 CHARACTER, 8 COLOR32, 9 EMBEDDED, 10 CUSTOM, 11 CLASSPTR, 12 EHANDLE, 13 EDICT,
14 POSITION_VECTOR, 15 TIME, 16 MODELNAME, 17 SOUNDNAME, 18 INPUT, 19 FUNCTION.
Records the builder assigns from a register read back `fieldType 0`; those, and fields the
corpus does not hold (vphysics, the client), fall back to the name's Hungarian prefix and the
record size.

Block bodies outside the field-stream convention, from their retail `Save` slots:
  EventQueue  CEQ_SaveRestoreBlockHandler::Save 100cfee0 -> 100cfd00:
              WriteFields("EventQueue") then WriteFields("PEvent") per pending event.
  Physics     CPhysSaveRestoreBlockHandler::Save 10043c70: per object WriteAll(PhysObjectHeader_t),
              StartBlock(), per sub-object StartBlock() + vphysics save, EndBlock().
              vphysics prefixes each object's fields with its raw 32-bit object pointer.
  AI          CAI_SaveRestoreBlockHandler::Save 1030bfd0: short squadCount; per squad
              WriteString(name) + WriteAll(CAI_Squad); short memoryCount; per NPC
              WriteEHandle + WriteAll(CAI_Memory).
  Python      CPython_SaveRestoreBlockHandler::Save 1019adc0: per namespace WriteBool(present),
              WriteInt(len), cPickle protocol-0 dump (G, then G.morgue).
Each block's header area opens with `short version` (WriteSaveHeaders, slot 3).
"""
import argparse
import io
import json
import math
import pickle
import re
import struct
import zlib
from pathlib import Path

from elysium_pipeline.formats import sav
from elysium_pipeline.paths import research_root

FT_NAMES = {1: "float", 2: "string", 3: "vector", 4: "int", 5: "bool", 6: "short",
            7: "char", 8: "color32", 9: "embedded", 10: "custom", 11: "classptr",
            12: "ehandle", 13: "edict", 14: "position", 15: "time", 16: "modelname",
            17: "soundname", 18: "input", 19: "function"}

# Group-header records: a 4-byte record whose name is a datamap/structure name and whose
# value is the count of field records that follow it at the same level.
GROUP_NAMES = {"ADJACENCY", "ASSIGNED_QUEST", "DECALLIST", "ETABLE", "EntityOutput",
               "EventQueue", "GLOBAL", "GameHeader", "LIGHTSTYLE", "PEvent", "PatrolPath",
               "Save Header", "EXPERIENCE_ENTRY", "GLOBAL_EMAIL"}


# Repeated records: always emitted as a list, even with one element.
LIST_GROUPS = {"ADJACENCY", "ASSIGNED_QUEST", "CSAct", "DECALLIST", "EntityOutput",
               "EXPERIENCE_ENTRY", "GLOBAL_EMAIL", "LIGHTSTYLE", "PEvent"}

# Fixed char[N] buffers the writer copies whole (garbage after the NUL).
FIXED_TEXT = {"comment", "fieldName", "landmarkName", "mapName", "modelName", "name",
              "netname", "savedMaterial", "skyName", "style", "szName", "szSchedule",
              "szTitle", "userName"}


def is_group_name(name):
    if name in GROUP_NAMES:
        return True
    if re.match(r"^C_?[A-Z]", name) and not name.startswith("m_"):
        return True
    return name.endswith("_t") and not name.startswith("m_")


class _SafeUnpickler(pickle.Unpickler):
    def find_class(self, module, name):
        raise pickle.UnpicklingError(f"refusing {module}.{name}")


# ---------------------------------------------------------------------------- scalars

def f32(x):
    """A float32 as the shortest decimal that round-trips, JSON-safe."""
    if math.isnan(x) or math.isinf(x):
        return str(x)
    for p in range(6, 10):
        y = float(f"{x:.{p}g}")
        if struct.pack("<f", y) == struct.pack("<f", x):
            return y
    return x


def floats(data):
    return [f32(v) for v in struct.unpack_from(f"<{len(data) // 4}f", data)]


def ints(data):
    return list(struct.unpack_from(f"<{len(data) // 4}i", data))


def one_or_list(v):
    return v[0] if len(v) == 1 else v


def _printable(b):
    return all(32 <= c < 127 or c in (9, 10, 13) for c in b)


def text_of(data, fixed=False):
    """NUL-terminated string(s), or None if the bytes are not text.

    `fixed`: a `char[N]` buffer — the writer copies the whole buffer, so whatever follows the
    first NUL is uninitialised stack memory and is dropped.
    """
    if not data:
        return None
    if fixed:
        head = data.split(b"\x00")[0]
        if b"\x00" in data and _printable(head):
            return head.decode("latin-1")
        return None
    if data[-1] != 0:
        return None
    parts = data.split(b"\x00")
    while parts and parts[-1] == b"":
        parts.pop()
    if not parts:
        return ""
    for p in parts:
        if not _printable(p):
            return None
    s = [p.decode("latin-1") for p in parts]
    return s[0] if len(s) == 1 else s


def prefixed_text(data):
    """`int len; char[len]` — CSave's raw string form; the PyObj ops count without the NUL."""
    if len(data) >= 5 and struct.unpack_from("<i", data)[0] in (len(data) - 4, len(data) - 5):
        return text_of(data[4:])
    return None


def int_or_float(data):
    iv, = struct.unpack_from("<i", data)
    fv, = struct.unpack_from("<f", data)
    if -1000000 < iv < 1000000:
        return iv
    if not (math.isnan(fv) or math.isinf(fv)) and 1e-6 < abs(fv) < 1e12:
        return f32(fv)
    return iv


# --------------------------------------------------------------------------- decoder

class Decoder:
    def __init__(self, types_by_class, types_by_name):
        self.types_by_class = types_by_class
        self.types_by_name = types_by_name
        self.tokens = []
        self.audit = None
        self.unparsed = 0
        self.unparsed_bytes = 0

    # -- records
    def records(self, buf, start=0, end=None, strict=True):
        """Split a field stream. Strict: every token valid and the stream consumed exactly."""
        end = len(buf) if end is None else end
        out, off = [], start
        while off < end:
            if off + 4 > end:
                return None if strict else (out, off)
            size, token = struct.unpack_from("<HH", buf, off)
            if off + 4 + size > end or token >= len(self.tokens):
                return None if strict else (out, off)
            out.append((self.tokens[token], buf[off + 4:off + 4 + size]))
            off += 4 + size
        return out if strict else (out, off)

    def hexdump(self, data):
        self.unparsed += 1
        self.unparsed_bytes += len(data)
        return {"_unparsed": data.hex()}

    # -- a whole stream into an ordered mapping
    def stream(self, recs, cls=None):
        out = {}
        i = 0
        while i < len(recs):
            name, data = recs[i]
            if len(data) == 4 and is_group_name(name):
                n, = struct.unpack("<i", data)
                if 0 <= n <= len(recs) - i - 1:
                    group = self.fields(recs[i + 1:i + 1 + n], name)
                    group = {"_fields": n, **group} if name[0] == "C" else group
                    self._put_group(out, name, group)
                    i += 1 + n
                    continue
            key, val = self.field(name, data, cls)
            self._put(out, key, val)
            i += 1
        return out

    def fields(self, recs, cls):
        out = {}
        for name, data in recs:
            key, val = self.field(name, data, cls)
            self._put(out, key, val)
        return out

    @staticmethod
    def _put(out, key, val):
        k, n = key, 2
        while k in out:
            k = f"{key} ({n})"
            n += 1
        out[k] = val

    @staticmethod
    def _put_group(out, key, val):
        if key in LIST_GROUPS:
            out.setdefault(key, []).append(val)
        elif key not in out:
            out[key] = val
        elif isinstance(out[key], list) and out.get("_list_" + key):
            out[key].append(val)
        else:
            out[key] = [out[key], val]
            out["_list_" + key] = True

    # -- nested content
    def nested(self, data, cls=None, require_named=True):
        recs = self.records(data)
        if recs is None or not recs:
            return None
        if require_named and not recs[0][0]:
            return None
        return self.stream(recs, cls)

    def block(self, data, cls=None):
        """An unnamed StartBlock()/EndBlock() record's content."""
        if not data:
            return {}
        v = self.nested(data, cls, require_named=False)
        if v is not None:
            return v
        if len(data) >= 4:
            v = self.nested(data[4:], cls, require_named=False)
            if v is not None:
                return {"objectPtr": "0x%08x" % struct.unpack_from("<I", data)[0], **v}
        return self.hexdump(data)

    def custom(self, name, data, cls):
        """CUSTOM/EMBEDDED payloads: a nested stream, an int-prefixed one, or a string list."""
        known = CUSTOM_OPS.get(name.split("[")[0].strip())
        if known is not None:
            try:
                r = _Raw(data)
                v = known(r)
                if not r.left():
                    return v
            except struct.error:
                pass
        v = self.nested(data, cls)
        if v is not None:
            return v
        if len(data) == 4:
            return struct.unpack("<i", data)[0]
        if len(data) >= 4:
            v = self.variant(name.split("[")[0].strip(), data)
            if v is not None:
                return v
        if data[0] in (0, 1):
            # a WriteData(bool present) guard, e.g. CAI_NPCPatrolPathSaveRestoreOps 1028cc00
            if len(data) == 1:
                return {"present": bool(data[0])}
            v = self.nested(data[1:], cls)
            if v is not None:
                return {"present": bool(data[0]), **v}
            # CPyObjStrSaveRestoreDataOps: bool present; int len; source string
            t = prefixed_text(data[1:])
            if t is not None:
                return {"present": bool(data[0]), "value": t}
        if len(data) >= 4:
            n, = struct.unpack_from("<i", data)
            v = self.nested(data[4:], cls)
            if v is not None:
                return {"count": n, **v}
            lst = self.string_list(data)
            if lst is not None:
                return lst
            t = prefixed_text(data)
            if t is not None:
                return t
            if len(data) == 4:
                return n
        return None

    @staticmethod
    def string_list(data):
        n, = struct.unpack_from("<i", data)
        if not 1 <= n <= 4096:
            return None
        off, out = 4, []
        for _ in range(n):
            if off + 4 > len(data):
                return None
            ln, = struct.unpack_from("<i", data, off)
            if ln < 0 or off + 4 + ln > len(data):
                return None
            s = data[off + 4:off + 4 + ln].split(b"\x00")[0]
            if not s or not _printable(s):
                return None
            out.append(s.decode("latin-1"))
            off += 4 + ln
        return out if off == len(data) else None

    # -- one field
    def field_type(self, name, cls):
        # VtMB spells array elements as records of their own (`m_nCase[0]`,
        # `m_iVAttributesBase[ v_attribute_strength ]`), so the exact name is tried first.
        base = name.split("[")[0].strip()
        for key in ((cls, name), (cls, base)):
            if key in self.types_by_class:
                return self.types_by_class[key]
        return self.types_by_name.get(name) or self.types_by_name.get(base) or 0

    def field(self, name, data, cls):
        if name == "":
            return "_block", self.block(data, cls)
        if not data:
            return name, None
        ft = self.field_type(name, cls)
        return name, self.value(name, data, ft, cls)

    def value(self, name, data, ft, cls):
        n = len(data)
        if ft in (9, 10, 18):
            v = self.custom(name, data, cls)
            if self.audit is not None:
                self.audit.setdefault((f"{cls} [{FT_NAMES[ft]}]", name), (n, v))
            if v is not None:
                return v
            ft = 0
        if ft in (2, 16, 17):
            t = text_of(data)
            if t is None:
                t = text_of(data, fixed=True)
            if t is None and data.endswith(b"\x00"):
                t = data[:-1].decode("latin-1")     # a string_t with non-printable bytes
            if t is not None:
                return t
        if ft in (1, 15) and n % 4 == 0:
            return one_or_list(floats(data))
        if ft in (4, 11, 12, 13) and n % 4 == 0:
            return one_or_list(ints(data))
        if ft in (3, 14) and n % 12 == 0:
            v = floats(data)
            vs = [v[i:i + 3] for i in range(0, len(v), 3)]
            return vs[0] if len(vs) == 1 else vs
        if ft == 5:
            return one_or_list([bool(b) for b in data])
        if ft == 6 and n % 2 == 0:
            return one_or_list(list(struct.unpack_from(f"<{n // 2}h", data)))
        if ft == 7:
            t = text_of(data, fixed=True) if n > 1 else None
            return t if t else one_or_list(list(data))
        if ft == 8 and n % 4 == 0:
            v = [list(data[i:i + 4]) for i in range(0, n, 4)]
            return v[0] if len(v) == 1 else v
        if ft == 19:
            t = text_of(data)
            if t is not None:
                return t
            if n == 4:
                return "0x%08x" % struct.unpack("<I", data)[0]
        return self.guess(name, data, cls)

    def guess(self, name, data, cls):
        v = self._guess(name, data, cls)
        if self.audit is not None:
            self.audit.setdefault((cls, name), (len(data), v))
        return v

    def variant(self, name, data):
        """variant_t (CVariantSaveDataOps::Save 100d0c90): raw `int fieldType`, then
        WriteFields(<fieldName>) holding the one value typed by it (omitted when zero)."""
        t, = struct.unpack_from("<i", data)
        if not 0 <= t <= 19:
            return None
        if len(data) == 4:
            return {"variantType": FT_NAMES.get(t, "void")} \
                if re.search(r"(^|_)(Value|VariantValue)$", name) else None
        recs = self.records(data[4:])
        if not recs or recs[0][0] != name or len(recs[0][1]) != 4:
            return None
        c, = struct.unpack("<i", recs[0][1])
        vals = recs[1:1 + c]
        v = self.value(name, vals[0][1], t, None) if vals and vals[0][1] else 0
        return {"variantType": FT_NAMES.get(t, "void"), "value": v}

    def _guess(self, name, data, cls):
        n = len(data)
        base = name.split("[")[0].strip()
        stem = base[2:] if base.startswith("m_") else base
        stem = stem.split(".")[-1]
        if base in FIXED_TEXT:
            t = text_of(data, fixed=True)
            if t is not None:
                return t
        # self-describing payloads first: they cannot be mistaken for a scalar
        if n >= 4:
            v = self.variant(base, data)
            if v is not None:
                return v
        t = prefixed_text(data)
        if t is not None:
            return t
        if n >= 5 and data[0] in (0, 1):
            t = prefixed_text(data[1:])
            if t is not None:
                return {"present": bool(data[0]), "value": t}
        if n >= 8:
            v = self.nested(data, cls)
            if v is not None:
                return v
            v = self.nested(data[4:], cls)
            if v is not None:
                return {"count": struct.unpack_from("<i", data)[0], **v}
            lst = self.string_list(data)
            if lst is not None:
                return lst
        if re.match(r"^(vec|ang|v[A-Z]|pos|origin|angles|velocity|angVelocity|world|target)",
                    stem) and n % 12 == 0:
            v = floats(data)
            vs = [v[i:i + 3] for i in range(0, len(v), 3)]
            return vs[0] if len(vs) == 1 else vs
        t = text_of(data)
        if t is not None and n > 4 and all(isinstance(t, str) or p for p in t) \
                and len(t if isinstance(t, str) else t[0]) >= 2:
            return t
        if n >= 8:
            head = data.split(b"\x00")[0]
            if len(head) >= 3 and _printable(head) and b"\x00" in data:
                return head.decode("latin-1")      # a char[N] buffer, stack garbage after NUL
        if re.search(r"^(clr|colou?r)|_colou?r$", stem) and n % 4 == 0:
            v = [list(data[i:i + 4]) for i in range(0, n, 4)]
            return v[0] if len(v) == 1 else v
        if re.match(r"^(fl|rgfl|f[A-Z]|time|mass|damp|drag|speed|strength|volume|max|dmg_|dl_"
                    r"|shk_|snd_dist)", stem) and n % 4 == 0:
            v = floats(data)
            if all(x == 0 or not isinstance(x, float) or abs(x) > 1e-30 for x in v):
                return one_or_list(v)
        if re.match(r"^p[A-Z]", stem) and n == 4 and cls and ("phys" in cls.lower()):
            return "0x%08x" % struct.unpack("<I", data)[0]  # a vphysics object pointer
        if re.match(r"^(h|p)[A-Z]", stem) and n % 4 == 0:
            return one_or_list(ints(data))
        if re.match(r"^(b|is|has)[A-Z]", stem):
            return one_or_list([bool(b) for b in data]) if n <= 32 else one_or_list(list(data))
        if re.match(r"^(i|n|e|f|bits|idx|l|us)[A-Z]", stem) and n % 4 == 0:
            return one_or_list(ints(data))
        if n == 1:
            return data[0]
        if n == 2:
            return struct.unpack("<h", data)[0]
        if n == 4:
            return int_or_float(data)
        if n == 12:
            return floats(data)
        if n % 4 == 0:
            return [int_or_float(data[i:i + 4]) for i in range(0, n, 4)]
        if t is not None:
            return t
        v = self.custom(name, data, cls)
        if v is not None:
            return v
        return self.hexdump(data)


# ----------------------------------------------------------------------- the datamaps

def load_types():
    """(class, field) -> fieldType, and field -> fieldType where unambiguous.

    Primary: `datamap_records-vampire.dll.json` and the `named_records-<module>.json` files
    `research/tooling/probes/datamap_records.py` writes (every record typed, builder stores
    replayed). Fallback: the corpus field ledger's non-zero `fieldType N` notes.
    """
    types = research_root() / "ghidra" / "types"
    by_class, by_name_votes = {}, {}
    maps = types / "datamap_records-vampire.dll.json"
    if maps.is_file():
        for cls, m in json.loads(maps.read_text(encoding="utf-8")).items():
            for r in m["records"]:
                if r["name"] and r["type"]:
                    by_class.setdefault((cls, r["name"]), r["type"])
                    by_name_votes.setdefault(r["name"], set()).add(r["type"])
    for path in sorted(types.glob("named_records-*.json")):
        for nm, recs in json.loads(path.read_text(encoding="utf-8")).items():
            for r in recs:
                by_name_votes.setdefault(nm, set()).add(r["type"])
    corpus = research_root() / "ghidra" / "corpus"
    for module in ("vampire.dll", "client.dll", "engine.dll"):
        path = corpus / f"fields-{module}.jsonl"
        if not path.is_file():
            continue
        with path.open(encoding="utf-8") as stream:
            for line in stream:
                r = json.loads(line)
                m = re.match(r"fieldType (\d+)", r.get("note", ""))
                if not m or m.group(1) == "0":
                    continue
                t = int(m.group(1))
                by_class.setdefault((r["cls"], r["name"]), t)
                if r["name"] not in by_name_votes:          # the ledger only fills gaps
                    by_name_votes[r["name"]] = {t}
    by_name = {k: next(iter(v)) for k, v in by_name_votes.items() if len(v) == 1}
    return by_class, by_name


# -------------------------------------------------------------------------- sections

def decode_server(dec, name, data):
    sec = sav.HLSection(name, data)
    dec.tokens = sec.tokens
    out = {"kind": "server", "rawSize": len(data), "version": sec.version,
           "symbols": {"slots": len(sec.tokens), "used": sum(1 for t in sec.tokens if t)},
           "headersSize": len(sec.headers), "bodySize": len(sec.body),
           "baseFilePos": sec.base_file_pos}

    # the global preamble: Save Header, ADJACENCY x n, LIGHTSTYLE x n
    out["preamble"] = dec.stream(dec.records(sec.body, 0, sec.base_file_pos))

    self_size, body_span, count = struct.unpack_from("<3i", sec.headers, 0)
    out["blockSet"] = {"selfSize": self_size, "bodySpan": body_span, "blockCount": count,
                       "blocks": [{"szName": b.name, "locHeader": b.loc_header,
                                   "locBody": b.loc_body} for b in sec.blocks]}

    hdr_ends = [b.loc_header for b in sec.blocks[1:]] + [len(sec.headers)]
    body_ends = [sec.base_file_pos + b.loc_body for b in sec.blocks[1:]] + [len(sec.body)]
    blocks = {}
    for b, hend, bend in zip(sec.blocks, hdr_ends, body_ends):
        hdr = sec.headers[b.loc_header:hend]
        bstart = sec.base_file_pos + b.loc_body
        body = sec.body[bstart:bend]
        if b.name == "Entities":
            blocks["Entities"] = decode_entities(dec, sec)
        elif b.name == "EventQueue":
            blocks["EventQueue"] = {"version": struct.unpack_from("<h", hdr)[0],
                                    **stream_or_hex(dec, body)}
        elif b.name == "Physics":
            v = {"version": struct.unpack_from("<h", hdr)[0]}
            v.update(stream_or_hex(dec, hdr[2:]))
            v["objects"] = decode_physics(dec, body)
            blocks["Physics"] = v
        elif b.name == "AI":
            blocks["AI"] = {"version": struct.unpack_from("<h", hdr)[0],
                            **decode_ai(dec, body)}
        elif b.name == "Python":
            blocks["Python"] = {"version": struct.unpack_from("<h", hdr)[0],
                                **decode_python(body)}
        else:
            blocks[b.name] = {"header": dec.hexdump(hdr), "body": dec.hexdump(body)}
    out["blocks"] = blocks
    return out, sec


def stream_or_hex(dec, data):
    recs = dec.records(data)
    if recs is None:
        recs, off = dec.records(data, strict=False)
        v = dec.stream(recs)
        v["_trailing"] = dec.hexdump(data[off:])
        return v
    return dec.stream(recs)


def decode_entities(dec, sec):
    ents = []
    for e in sec.entity_table():
        row = {}
        for k, f in e.items():
            if k == "classname":
                row[k] = f.s()
            elif k in ("flags_upper32", "flags_lower32"):
                row[k] = "0x%08x" % (f.i() & 0xFFFFFFFF)
            else:
                row[k] = f.i()
        # zero-omission: an absent id / location / size is 0 (the client's entity 0 sits at 0)
        for k in ("id", "location", "size"):
            row.setdefault(k, 0)
        loc, size = row["location"], row["size"]
        if size > 0:
            recs, off = entity_groups(dec, sec.body, loc, loc + size)
            row["data"] = dec.stream(recs)
            if off < loc + size:
                row["saveTrailer"] = decode_trailer(dec, row["data"], sec.body[off:loc + size])
        else:
            row["data"] = None
        ents.append(row)
    return {"count": len(ents), "entities": ents}


def entity_groups(dec, buf, start, end):
    """The datamap part of an entity stream: consecutive (class header + N fields) groups.

    `CBaseEntity::Save` (100a9f70) writes the datamap chain through `SaveDataDescBlock`; a
    class's own `Save` override may then append raw, unnamed writes, which begin where the
    next record is no longer a class header.
    """
    out, off = [], start
    while off + 4 <= end:
        size, token = struct.unpack_from("<HH", buf, off)
        name = dec.tokens[token] if token < len(dec.tokens) else ""
        if size != 4 or not is_group_name(name):
            break
        n, = struct.unpack_from("<i", buf, off + 4)
        probe, cur = [], off + 8
        for _ in range(n):
            if cur + 4 > end:
                break
            s2, t2 = struct.unpack_from("<HH", buf, cur)
            if cur + 4 + s2 > end or t2 >= len(dec.tokens):
                break
            probe.append((dec.tokens[t2], buf[cur + 4:cur + 4 + s2]))
            cur += 4 + s2
        if len(probe) != n:
            break
        out.append((name, buf[off + 4:off + 8]))
        out.extend(probe)
        off = cur
    return out, off


class _Raw:
    def __init__(self, data):
        self.data, self.off = data, 0

    def left(self):
        return len(self.data) - self.off

    def take(self, n):
        v = self.data[self.off:self.off + n]
        self.off += n
        return v

    def i(self):
        return struct.unpack("<i", self.take(4))[0]

    def f(self):
        return f32(struct.unpack("<f", self.take(4))[0])


def _vec(r):
    return [r.f(), r.f(), r.f()]


def _relationships(r):
    # CRelationshipSaveDataOps::Save 10349100: int count; then, per non-null slot only,
    # WriteEHandle(entity) + four WriteInt (+4, +8, +0xc, +0x10 of the relationship record).
    n = r.i()
    rows = []
    while r.left() >= 20:
        rows.append({"entity": r.i(), "+0x4": r.i(), "+0x8": r.i(), "+0xc": r.i(),
                     "+0x10": r.i()})
    return {"count": n, "relationships": rows}


def _markers(r):
    # CAI_InterestingPlaceMarkersSaveRestoreOps 102d9240: int (+0x584); int count;
    # per marker WriteEHandle + WriteVector(+4) + WriteVector(+0x10).
    cap, n = r.i(), r.i()
    rows = [{"entity": r.i(), "+0x4": _vec(r), "+0x10": _vec(r)} for _ in range(n)]
    return {"field_584": cap, "count": n, "markers": rows}


CUSTOM_OPS = {"m_Relationship": _relationships, "m_pMarkers": _markers}


def decode_trailer(dec, data, raw):
    """Raw writes a class's `Save` override appends after the datamap chain."""
    r = _Raw(raw)
    out = {}
    try:
        if "CBasePlayer" in data:
            # CBasePlayer::Save 1016ea00 -> FUN_10299c60: WriteData(bool CAI_CsActList present);
            # CAI_CsActList::Save 102ca130 (int n; n x WriteFields("CSAct"); int);
            # FUN_103707e0: WriteEHandle(1093ac3c), WriteTime(1093aca8), WriteInt(1093acac),
            # WriteInt(1093acb0); then WriteInt(1092053c).
            present = r.take(1)[0]
            out["csActListPresent"] = bool(present)
            if present:
                n = r.i()
                acts = []
                for _ in range(n):
                    recs, off = dec.records(r.data, r.off, strict=False)
                    size, token = struct.unpack_from("<HH", r.data, r.off)
                    cnt, = struct.unpack_from("<i", r.data, r.off + 4)
                    take = recs[:1 + cnt]
                    r.off += sum(4 + len(d) for _, d in take)
                    acts.append(dec.stream(take, "CAI_CsAct"))
                out["csActCount"] = n
                out["csActs"] = acts
                out["csActListTail"] = r.i()
            out["g_1093ac3c_ehandle"] = r.i()
            out["g_1093aca8_time"] = r.f()
            out["g_1093acac"] = r.i()
            out["g_1093acb0"] = r.i()
            out["g_1092053c"] = r.i()
        elif "CAI_BaseNPCTroika" in data:
            # CAI_BaseNPCTroika::Save 102993c0: WriteData(bool this+0x630c != 0);
            # if set, WriteInt((*(this+0x630c))+4), WriteInt((*(this+0x630c))+8).
            present = r.take(1)[0]
            out["field_630c_present"] = bool(present)
            if present:
                out["field_630c_plus4"] = r.i()
                out["field_630c_plus8"] = r.i()
    except struct.error:
        pass
    if r.left():
        out["_rest"] = dec.hexdump(r.data[r.off:])
    return out


def decode_physics(dec, body):
    recs = dec.records(body)
    if recs is None:
        return stream_or_hex(dec, body)
    objs, cur = [], None
    for name, data in recs:
        if name == "PhysObjectHeader_t" and len(data) == 4:
            cur = {"PhysObjectHeader_t": {}, "_n": struct.unpack("<i", data)[0]}
            objs.append(cur)
            continue
        if cur is not None and cur["_n"] > 0:
            k, v = dec.field(name, data, "PhysObjectHeader_t")
            cur["PhysObjectHeader_t"][k] = v
            cur["_n"] -= 1
            continue
        if name == "" and cur is not None:
            # the object's StartBlock(): one inner StartBlock() per sub-object
            inner = dec.records(data)
            subs = []
            if inner is None:
                subs = dec.hexdump(data)
            else:
                for iname, idata in inner:
                    subs.append(dec.block(idata) if iname == "" else
                                {iname: dec.field(iname, idata, None)[1]})
            cur["physicsObjects"] = subs
            continue
        objs.append({name: dec.field(name, data, None)[1]})
    for o in objs:
        o.pop("_n", None)
    return objs


def decode_ai(dec, body):
    off = 0
    out = {}

    def write_all(cls_hint):
        nonlocal off
        recs, _ = dec.records(body, off, strict=False)
        # WriteAll: one group header then its fields (CAI_Squad / CAI_Memory have no base)
        if not recs:
            return None
        name, data = recs[0]
        n = struct.unpack("<i", data)[0] if len(data) == 4 else 0
        take = recs[:1 + n]
        off += sum(4 + len(d) for _, d in take)
        return dec.stream(take, cls_hint)

    n_squads, = struct.unpack_from("<h", body, off)
    off += 2
    squads = []
    for _ in range(n_squads):
        ln, = struct.unpack_from("<i", body, off)
        sname = body[off + 4:off + 4 + ln].split(b"\x00")[0].decode("latin-1")
        off += 4 + ln
        squads.append({"name": sname, **(write_all("CAI_Squad") or {})})
    out["squadCount"] = n_squads
    out["squads"] = squads
    n_mem, = struct.unpack_from("<h", body, off)
    off += 2
    mems = []
    for _ in range(n_mem):
        h, = struct.unpack_from("<i", body, off)
        off += 4
        mems.append({"npc": h, **(write_all("CAI_Memory") or {})})
    out["memoryCount"] = n_mem
    out["memories"] = mems
    if off < len(body):
        out["_trailing"] = dec.hexdump(body[off:])
    return out


def decode_python(body):
    labels = ["G", "G.morgue"]
    out, off, i = {}, 0, 0
    while off < len(body):
        present = body[off]
        off += 1
        label = labels[i] if i < len(labels) else f"namespace_{i}"
        i += 1
        if not present:
            out[label] = None
            continue
        n, = struct.unpack_from("<i", body, off)
        payload = body[off + 4:off + 4 + n]
        off += 4 + n
        try:
            ns = _SafeUnpickler(io.BytesIO(payload), encoding="latin-1").load()
            out[label] = dict(sorted(ns.items())) if isinstance(ns, dict) else repr(ns)
        except Exception as e:                                           # noqa: BLE001
            out[label] = {"_undecodable": str(e), "_pickle": payload.decode("latin-1")}
    return out


def decode_client(dec, name, data):
    sec = sav.HLSection(name, data)
    dec.tokens = sec.tokens
    out = {"kind": "client", "rawSize": len(data), "version": sec.version,
           "symbols": {"slots": len(sec.tokens), "used": sum(1 for t in sec.tokens if t)},
           "headersSize": len(sec.headers), "bodySize": len(sec.body),
           "decalSize": len(sec.decals), "decalCount": sec.decal_count}
    self_size, body_span, count = struct.unpack_from("<3i", sec.headers, 0)
    out["blockSet"] = {"selfSize": self_size, "bodySpan": body_span, "blockCount": count,
                       "blocks": [{"szName": b.name, "locHeader": b.loc_header,
                                   "locBody": b.loc_body} for b in sec.blocks]}
    out["blocks"] = {"Entities": decode_entities(dec, sec)}
    decals = []
    for rec in sec.decal_list():
        d = {}
        for k, f in rec.items():
            if k == "DECALLIST":
                continue
            if k == "position":
                d[k] = floats(f.data)
            elif k == "name":
                d[k] = f.s()
            elif k == "entityIndex":
                d[k] = struct.unpack("<h", f.data)[0]
            elif k == "flags":
                d[k] = f.data[0]
            else:
                d[k] = dec.field(k, f.data, "DECALLIST")[1]
        decals.append(d)
    out["decals"] = decals
    out["playerName"] = sec.player_name
    return out


# ---------------------------------------------------------------------------- driver

def decode(path, audit=None):
    by_class, by_name = load_types()
    dec = Decoder(by_class, by_name)
    dec.audit = audit
    raw = Path(path).read_bytes()
    s = sav.parse_sav(path)
    dec.tokens = s["tokens"]
    magic, version, data_size, tok_count, tok_size = struct.unpack_from("<4s4i", raw, 0)

    # the container's own section framing, for the record
    framing, off = [], 20 + tok_size + data_size
    while off + sav.NAME_STRIDE + 4 <= len(raw):
        nm = raw[off:off + sav.NAME_STRIDE].split(b"\x00")[0].decode("latin-1")
        off += sav.NAME_STRIDE
        raw_len, = struct.unpack_from("<i", raw, off)
        off += 4
        chunks, got = [], 0
        while got < raw_len:
            cl, = struct.unpack_from("<i", raw, off)
            got += len(zlib.decompress(raw[off + 4:off + 4 + cl]))
            chunks.append(cl)
            off += 4 + cl
        framing.append({"name": nm, "rawLen": raw_len, "zlibChunks": chunks})

    doc = {
        "_about": {
            "source": Path(path).name,
            "format": "docs/vtmb/savegame_format.md",
            "decoder": "research/tooling/probes/sav_to_json.py over "
                       "pipeline/src/elysium_pipeline/formats/sav.py",
            "notes": [
                "Zero-valued fields are omitted by the retail writer; an absent key is 0.",
                "Group headers (class / structure names) map to their fields; on class "
                "groups '_fields' is the field count the writer recorded.",
                "EHANDLE / CLASSPTR values are entity 'id's in the same map's entity table; "
                "-1 is a null handle.",
                "FIELD_TIME values are stored relative to the section's 'Save Header.time' "
                "(CSave::WriteTime subtracts the base time); a far-future 1e11 is 'never'.",
                "char[N] buffers are cut at the first NUL; the writer copies the rest of the "
                "buffer, which is uninitialised memory.",
                "'saveTrailer' is the raw data a class Save override appends after its "
                "datamap chain (player, Troika NPCs); keys name the retail source.",
                "'_block' is an unnamed StartBlock()/EndBlock() record; 'objectPtr' is the "
                "save-time vphysics object address used for handle fix-up.",
                "Field types come from recovered retail datamaps where available, otherwise "
                "from the field's Hungarian prefix and size.",
                "'_unparsed' is hex the decoder could not type.",
            ],
        },
        "container": {"magic": magic.decode("latin-1"), "version": version,
                      "dataSize": data_size, "tokenCount": tok_count, "tokenSize": tok_size,
                      "fileSize": len(raw), "sections": framing},
        "header": dec.stream(dec.records(raw, 20 + tok_size, 20 + tok_size + data_size)),
        "maps": {},
    }

    servers = {}
    for name, data in s["sections"]:
        stem, ext = name.rsplit(".", 1)
        m = doc["maps"].setdefault(stem, {})
        if ext == "HL1":
            m["HL1"], servers[stem] = decode_server(dec, name, data)
        elif ext == "HL2":
            m["HL2"] = decode_client(dec, name, data)
        elif ext == "HL3":
            n, = struct.unpack_from("<i", data, 0)
            ids = list(struct.unpack_from(f"<{n}i", data, 4))
            m["HL3"] = {"kind": "transition", "count": n, "ids": ids}

    # name the transitioned entities from their own map's table
    for stem, m in doc["maps"].items():
        if "HL3" in m and "HL1" in m:
            by_id = {e["id"]: e.get("classname", "")
                     for e in m["HL1"]["blocks"]["Entities"]["entities"]}
            m["HL3"]["entities"] = [{"id": i, "classname": by_id.get(i)} for i in m["HL3"]["ids"]]

    doc["_about"]["unparsedRegions"] = dec.unparsed
    doc["_about"]["unparsedBytes"] = dec.unparsed_bytes
    return doc


def _strip_markers(o):
    if isinstance(o, dict):
        return {k: _strip_markers(v) for k, v in o.items() if not k.startswith("_list_")}
    if isinstance(o, list):
        return [_strip_markers(v) for v in o]
    return o


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("out")
    ap.add_argument("--audit", action="store_true",
                    help="list every field typed by the name/size fallback, not a datamap")
    a = ap.parse_args()
    audit = {} if a.audit else None
    doc = _strip_markers(decode(a.file, audit))
    if audit is not None:
        for (cls, name), (size, v) in sorted(audit.items(), key=lambda kv: (str(kv[0][0]),
                                                                          kv[0][1])):
            print(f"  {str(cls):<34} {name:<44} {size:>4}B  {json.dumps(v)[:70]}")
    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding="utf-8")
    ab = doc["_about"]
    print(f"wrote {out} ({out.stat().st_size:,} bytes); "
          f"unparsed regions {ab['unparsedRegions']} ({ab['unparsedBytes']} bytes)")


if __name__ == "__main__":
    main()
