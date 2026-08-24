"""Reader for the original VtMB .vpk package format.

Layout (derived from the files themselves):
  * File data is concatenated from offset 0 (no header).
  * A flat directory sits near the tail; each entry is:
        uint32 name_len            # exact length, no null terminator
        char   name[name_len]      # forward-slash path, mixed case
        uint32 file_offset         # data offset within THIS vpk
        uint32 file_size
  * A small footer ends the file; its last bytes point at the directory start.

Each pack*.vpk is self-contained (its directory references its own data).
Indexing reads only the directory extent at the tail, never the data half;
extraction reuses one read-binary handle per pack per thread.
"""
import collections, struct, glob, os, sys, threading

def _u32(b, o): return struct.unpack_from("<I", b, o)[0]

#: Bytes at EOF that cover every footer pointer position the probes use.
_FOOTER_PROBE = 16

def _entry_plausible(head):
    """Whether `head` (the bytes at a candidate directory start) opens like a real
    entry: a sane name length followed by printable-ASCII name bytes."""
    name_len = _u32(head, 0)
    return 1 <= name_len <= 256 and all(32 <= c < 127 for c in head[4:4 + name_len])

def _find_dir_offset(data):
    n = len(data)
    # The footer stores the directory start offset near EOF. Observed at n-5
    # (4-byte offset followed by a trailing 0x00), but probe a few positions
    # and accept the one that lands on a plausible first entry.
    for ptr in (n - 5, n - 4, n - 8, n - 9):
        if ptr < 0:
            continue
        cand = _u32(data, ptr)
        if 0 < cand < n - 8 and _entry_plausible(data[cand:cand + 4 + 256]):
            return cand
    raise ValueError("could not locate VPK directory")

def _find_dir_offset_tail(f, n):
    """`_find_dir_offset` against an open pack of size `n`, reading only the footer
    plus up to four 260-byte candidate probes. Probes the same pointer positions and
    applies the same plausibility test to the same bytes as the full-buffer probe."""
    f.seek(max(0, n - _FOOTER_PROBE))
    footer = f.read()
    base = n - len(footer)
    for ptr in (n - 5, n - 4, n - 8, n - 9):
        if ptr < base:
            continue
        cand = _u32(footer, ptr - base)
        if not (0 < cand < n - 8):
            continue
        f.seek(cand)
        if _entry_plausible(f.read(4 + 256)):
            return cand
    raise ValueError("could not locate VPK directory")

def index_vpk(path):
    """Return {lowercase_name: (path, offset, size)} for one .vpk.

    Reads only the directory extent: the footer names where the directory starts,
    so the file data ahead of it never enters memory."""
    with open(path, "rb") as f:
        n = f.seek(0, os.SEEK_END)
        try:
            pos = _find_dir_offset_tail(f, n)
            f.seek(pos)
            data = f.read()                     # directory plus footer only
            pos = 0
        except ValueError:
            # The tail probes found no directory. Re-probe on the whole file so a pack
            # with an unexpected footer still indexes -- loudly, because the tail read
            # covers every layout the full-buffer probe accepts, and a pack that needs
            # this path is a layout worth knowing about. When the full probe fails too,
            # this raises exactly as the tail probe did.
            f.seek(0)
            data = f.read()
            pos = _find_dir_offset(data)
            print(f"  warning: {os.path.basename(path)}: VPK directory located only by "
                  f"a full-file probe, not from the tail", file=sys.stderr)
    entries = {}
    end = len(data) - 8
    while pos < end:
        name_len = _u32(data, pos)
        if name_len == 0 or name_len > 256:
            break  # ran into the footer
        name = data[pos + 4: pos + 4 + name_len]
        if not all(32 <= c < 127 for c in name):
            break
        pos += 4 + name_len
        offset, size = _u32(data, pos), _u32(data, pos + 4)
        pos += 8
        entries[name.decode("ascii").lower().replace("\\", "/")] = (path, offset, size)
    return entries

def index_all(vpk_dir, verbose=False):
    """Index every pack*.vpk in a folder into one merged table.

    Packs that are empty or use an incompatible layout (e.g. the tiny
    savegame-template pack) are skipped rather than aborting the whole index.
    """
    merged = {}
    for vpk in sorted(glob.glob(os.path.join(vpk_dir, "pack*.vpk"))):
        try:
            merged.update(index_vpk(vpk))
        except Exception as e:
            if verbose:
                print(f"  skip {os.path.basename(vpk)}: {e}")
    return merged

#: How many pack handles one thread keeps open; covers every pack*.vpk an install ships.
_HANDLE_CAP = 16
#: Cached read-binary pack handles, LRU-ordered. Per-thread because a handle's
#: seek/read pair is not atomic and export tasks run in a thread pool.
_handles = threading.local()

def _pack_handle(path):
    """This thread's open read-binary handle for one pack, opened on first use."""
    cache = getattr(_handles, "cache", None)
    if cache is None:
        cache = _handles.cache = collections.OrderedDict()
    f = cache.get(path)
    if f is not None and not f.closed:
        cache.move_to_end(path)
        return f
    f = open(path, "rb")
    cache[path] = f
    if len(cache) > _HANDLE_CAP:
        cache.popitem(last=False)[1].close()
    return f

def extract(entry):
    """entry = (vpk_path, offset, size) -> bytes.

    Reuses this thread's cached handle for the pack: a corpus decode extracts tens of
    thousands of entries, so the pack is not reopened per entry."""
    path, offset, size = entry
    f = _pack_handle(path)
    f.seek(offset)
    return f.read(size)

def close_handles():
    """Close every pack handle this thread holds; a later extract reopens on demand.
    An open handle blocks a directory delete on Windows, so anything extracting from
    a temporary directory calls this before cleanup."""
    cache = getattr(_handles, "cache", None)
    while cache:
        cache.popitem(last=False)[1].close()

if __name__ == "__main__":
    import sys, collections
    vpk_dir = sys.argv[1]
    idx = index_all(vpk_dir)
    print(f"indexed {len(idx):,} files across all pack*.vpk\n")

    # Count by extension.
    ext = collections.Counter(os.path.splitext(k)[1] for k in idx)
    print("by extension (top 12):")
    for e, c in ext.most_common(12):
        print(f"  {e or '<none>':<8} {c:>7}")

    # Show a few materials/textures.
    print("\nsample .vtf entries:")
    for k in list(k for k in idx if k.endswith(".vtf"))[:5]:
        p, o, s = idx[k]
        print(f"  {k}   ({s:,} B in {os.path.basename(p)})")
    print("\nsample .vmt entries:")
    for k in list(k for k in idx if k.endswith(".vmt"))[:5]:
        print(f"  {k}")
