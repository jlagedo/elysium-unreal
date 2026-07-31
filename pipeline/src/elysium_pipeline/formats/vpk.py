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
"""
import struct, glob, os

def _u32(b, o): return struct.unpack_from("<I", b, o)[0]

def _find_dir_offset(data):
    n = len(data)
    # The footer stores the directory start offset near EOF. Observed at n-5
    # (4-byte offset followed by a trailing 0x00), but probe a few positions
    # and accept the one that lands on a plausible first entry.
    for ptr in (n - 5, n - 4, n - 8, n - 9):
        if ptr < 0:
            continue
        cand = _u32(data, ptr)
        if 0 < cand < n - 8:
            name_len = _u32(data, cand)
            if 1 <= name_len <= 256:
                name = data[cand + 4: cand + 4 + name_len]
                if all(32 <= c < 127 for c in name):
                    return cand
    raise ValueError("could not locate VPK directory")

def index_vpk(path):
    """Return {lowercase_name: (path, offset, size)} for one .vpk."""
    with open(path, "rb") as f:
        data = f.read()
    n = len(data)
    pos = _find_dir_offset(data)
    entries = {}
    while pos < n - 8:
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

def extract(entry):
    """entry = (vpk_path, offset, size) -> bytes."""
    path, offset, size = entry
    with open(path, "rb") as f:
        f.seek(offset)
        return f.read(size)

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
