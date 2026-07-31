#!/usr/bin/env python3
"""Decoder for VtMB savegames — the `.sav` container and its embedded `.HL1/.HL2/.HL3`
per-map state sections.

VtMB saves are early-Source `CSaveRestore` output with two Troika changes: every
embedded section is zlib-deflated in <=512 KiB chunks, and a fifth save-restore block
handler named `Python` carries the script layer's global namespace.

Nothing here reads the user's game install; it reads a save file the user points it at.

Format
------
`.sav` container (little-endian throughout)::

    'JSAV'                      magic
    int      version            117 for VtMB
    int      dataSize           bytes of the trailing global field stream
    int      tokenCount         symbol-table slots (16383 — a sparse hash table)
    int      tokenSize          bytes of the symbol blob
    char[tokenSize]             tokenCount NUL-terminated slots ('' = unused slot)
    byte[dataSize]              CSave field stream: GameHeader + GLOBAL
    section*                    mapCount sections, ASCII-sorted by name

    section := char name[260]; int rawLen; chunk*
    chunk   := int compLen; byte zlib[compLen]      (each inflates to <=512 KiB)

`.HL1`/`.HL2` section (`.HL3` is a bare `int count; int ids[count]`)::

    'VALV'                      magic
    int      version            117
    int      tokenSize
    int      tokenCount         16383
    int      headersSize        block-handler header area (includes its own length int)
    int      dataSize           block-handler body area
    char[tokenSize]             symbol table
    byte[headersSize]           int selfSize; int bodyBase; int blockCount;
                                then one `SaveRestoreBlockHeader_t` field group per block,
                                then each block's header area at its `locHeader`
    byte[dataSize]              each block's body at its `locBody`

A CSave field stream is a flat run of `short size; short token; byte data[size]`
records, `token` indexing the symbol table. Fields whose value is all-zero are
omitted by the writer, so a group's field set is sparse — readers match by name.
"""
import struct
import zlib
from pathlib import Path

SAV_MAGIC = b"JSAV"
HL_MAGIC = b"VALV"
VTMB_VERSION = 117
NAME_STRIDE = 260          # char name[MAX_OSPATH]
CHUNK_MAX = 512 * 1024


# --------------------------------------------------------------------------- fields

class Field:
    __slots__ = ("off", "size", "token", "name", "data")

    def __init__(self, off, size, token, name, data):
        self.off, self.size, self.token, self.name, self.data = off, size, token, name, data

    # --- typed views (the writer stores raw bytes; the datamap decides the type)
    def i(self):
        return struct.unpack_from("<i", self.data)[0]

    def f(self):
        return struct.unpack_from("<f", self.data)[0]

    def s(self):
        return self.data.split(b"\x00")[0].decode("latin-1")

    def v(self):
        return struct.unpack_from("<3f", self.data)

    def guess(self):
        """Best-effort rendering for a dump, without a datamap to consult."""
        txt = self.data.split(b"\x00")[0]
        if len(txt) > 1 and all(32 <= c < 127 for c in txt):
            return repr(txt.decode("latin-1"))
        if self.size == 4:
            iv = self.i()
            fv = self.f()
            if -1e6 < fv < 1e6 and fv != 0 and abs(fv) > 1e-6:
                return f"{iv} / {fv:g}f"
            return str(iv)
        if self.size == 12:
            return "(%g %g %g)" % self.v()
        if self.size == 2:
            return str(struct.unpack_from("<h", self.data)[0])
        if self.size == 1:
            return str(self.data[0])
        return repr(self.data[:64]) + ("..." if self.size > 64 else "")

    def __repr__(self):
        return f"<{self.name} {self.size}B {self.guess()}>"


def read_field(buf, off, tokens=()):
    """Read one `short size; short token; data` record. Returns (Field, next_off)."""
    size, token = struct.unpack_from("<HH", buf, off)
    name = tokens[token] if token < len(tokens) else ""
    return Field(off, size, token, name, buf[off + 4: off + 4 + size]), off + 4 + size


def read_fields(buf, start=0, end=None, tokens=()):
    """Walk a CSave field stream. Returns [Field]."""
    end = len(buf) if end is None else end
    out = []
    off = start
    while off + 4 <= end:
        size, token = struct.unpack_from("<HH", buf, off)
        if off + 4 + size > end:
            break
        name = tokens[token] if token < len(tokens) else ""
        out.append(Field(off, size, token, name, buf[off + 4: off + 4 + size]))
        off += 4 + size
    return out


def split_tokens(blob):
    toks = blob.split(b"\x00")
    if toks and toks[-1] == b"":
        toks.pop()
    return [t.decode("latin-1") for t in toks]


# ------------------------------------------------------------------------ sections

class Block:
    """One save-restore block handler's slice of an HL section."""

    def __init__(self, name, loc_header, loc_body):
        self.name = name
        self.loc_header = loc_header
        self.loc_body = loc_body


class HLSection:
    """A parsed `.HL1` (server) or `.HL2` (client) per-map state section.

    The two use different headers — the server and client write their own — so the
    token count (always 16383) is what locates the layout::

        .HL1 (24 B):  'VALV' ver tokenSize tokenCount headersSize dataSize
        .HL2 (32 B):  'VALV' ver dataSize headersSize decalSize tokenSize
                      decalCount tokenCount

    `.HL2` additionally carries the decal list after the block data and an 8-byte
    `int len; char name[len]` player-name trailer.
    """

    def __init__(self, name, data):
        self.name = name
        self.raw = data
        magic, self.version = struct.unpack_from("<4si", data, 0)
        if magic != HL_MAGIC:
            raise ValueError(f"{name}: bad magic {magic!r}")
        ints = struct.unpack_from("<7i", data, 8)
        self.decals = b""
        if ints[1] == 16383:                          # .HL1 server layout
            self.kind = "server"
            tok_size, self.token_count, hdr_size, data_size = ints[0:4]
            off = 24
            decal_size = decal_count = 0
        elif ints[5] == 16383:                        # .HL2 client layout
            self.kind = "client"
            data_size, hdr_size, decal_size, tok_size, decal_count, self.token_count = ints[0:6]
            off = 32
        else:
            raise ValueError(f"{name}: unrecognised HL header {ints}")
        self.decal_count = decal_count
        self.tokens = split_tokens(data[off:off + tok_size])
        off += tok_size
        self.headers = data[off:off + hdr_size]
        off += hdr_size
        self.body = data[off:off + data_size]
        off += data_size
        if self.kind == "client":
            self.decals = data[off:off + decal_size]
            off += decal_size
            n, = struct.unpack_from("<i", data, off)
            self.player_name = data[off + 4:off + 4 + n].split(b"\x00")[0].decode("latin-1")
        self.blocks = self._read_block_table()

    def decal_list(self):
        """Client-section decal list.

        Each record holds `position` (Vector), `name` (char[128] material), `entityIndex`
        (short) and `flags` (byte), in that order. `entityIndex` is dropped by the
        zero-omission rule on world decals, so records are 161 bytes for a world decal and
        167 for one stuck to a brush entity — where `position` is entity-local and
        `entityIndex` is that entity's `saveentityindex`.
        """
        out = []
        off = 0
        for _ in range(self.decal_count):
            n_fields, off = read_field(self.decals, off, self.tokens)   # 'DECALLIST'
            rec = {}
            for _ in range(n_fields.i()):
                f, off = read_field(self.decals, off, self.tokens)
                rec[f.name] = f
            out.append(rec)
        return out

    def _read_block_table(self):
        self_size, body_span, count = struct.unpack_from("<3i", self.headers, 0)
        # Block bodies record positions relative to the block set's own start
        # (Source's `baseFilePos`) — i.e. after the global Save Header / ADJACENCY /
        # LIGHTSTYLE preamble the server writes ahead of them. `body_span` is the
        # body length measured from there; the client writes no preamble.
        self.base_file_pos = len(self.body) - body_span
        blocks = []
        off = 12
        for _ in range(count):
            # each entry: a `uv` (CUtlVector element count) field, then an `elems` group
            _uv, off = read_field(self.headers, off, self.tokens)
            grp, off = read_field(self.headers, off, self.tokens)
            inner = {f.name: f for f in read_fields(grp.data, 0, None, self.tokens)}
            blocks.append(Block(
                inner["szName"].s() if "szName" in inner else "?",
                inner["locHeader"].i() if "locHeader" in inner else 0,
                inner["locBody"].i() if "locBody" in inner else 0,
            ))
        return blocks

    def block_body(self, name):
        """Body slice for a named block, resolved through `base_file_pos`."""
        b = self.block(name)
        if b is None or not b.loc_body:
            return b'', 0
        start = self.base_file_pos + b.loc_body
        return self.body[start:], start

    def block(self, name):
        for b in self.blocks:
            if b.name == name:
                return b
        return None

    # --- Entities block ---------------------------------------------------
    def entity_table(self):
        """Return [dict] — the ENTITYTABLE, one entry per saved entity."""
        b = self.block("Entities")
        if b is None:
            return []
        count, = struct.unpack_from("<i", self.headers, b.loc_header)
        ents = []
        off = b.loc_header + 4
        for _ in range(count):
            n_fields, off = read_field(self.headers, off, self.tokens)   # 'ETABLE'
            ent = {}
            for _ in range(n_fields.i()):
                f, off = read_field(self.headers, off, self.tokens)
                ent[f.name] = f
            ents.append(ent)
        return ents

    def entity_fields(self, ent):
        """Field stream for one ENTITYTABLE entry, sliced out of the body block."""
        if "location" not in ent or "size" not in ent:
            return []
        loc, size = ent["location"].i(), ent["size"].i()
        return read_fields(self.body, loc, loc + size, self.tokens)


def parse_sav(path):
    """Parse a `.sav` container -> (header fields, tokens, [(name, bytes)])."""
    d = Path(path).read_bytes()
    if len(d) < 20:
        # `Vampire-999.sav` ships as a zero-byte placeholder slot.
        raise ValueError(f"{path}: {len(d)} bytes — not a savegame")
    magic, version, data_size, tok_count, tok_size = struct.unpack_from("<4s4i", d, 0)
    if magic != SAV_MAGIC:
        raise ValueError(f"{path}: bad magic {magic!r}")
    off = 20
    tokens = split_tokens(d[off:off + tok_size])
    off += tok_size
    header = read_fields(d, off, off + data_size, tokens)
    off += data_size
    sections = []
    while off + NAME_STRIDE + 4 <= len(d):
        name = d[off:off + NAME_STRIDE].split(b"\x00")[0].decode("latin-1")
        off += NAME_STRIDE
        raw_len, = struct.unpack_from("<i", d, off)
        off += 4
        out = bytearray()
        while len(out) < raw_len:
            comp_len, = struct.unpack_from("<i", d, off)
            off += 4
            out += zlib.decompress(d[off:off + comp_len])
            off += comp_len
        sections.append((name, bytes(out)))
    return {"version": version, "fields": header, "tokens": tokens,
            "sections": sections}
