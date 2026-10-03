"""The kernel tools' shared build, kept on disk between processes.

`kernel_ledger`, `kernel_lists`, `kernel_shape`, `gen_kernel_shape`, `kernel_story8_shape`,
`gen_kernel_bindings` and `kernel_gate`'s residue all stand on the same three builds: the ledger's
corpus stage (the SQLite load, the closure walk, the read/write directions, the build order), the
SDK 2013 index, and the shape's corpus half (the access evidence, the slot signatures). Built from
scratch they are seconds of SQLite reads and header parsing in every process; here each is pickled
under `$ELYSIUM_WORK_ROOT/cache/kernel/` on a hash of its inputs -- the data files by size and
mtime, the overlays and the code of the modules that build it by content. A key that no longer
matches is built once and stored beside the others: files are named by their key, so the checkouts
sharing one work root never evict each other's build on every switch.

Two more layers sit on top:

* `FileScans`, a per-file cache of a scan over the checkout (the ledger's citation scan), keyed on
  each file's size and mtime: an edit rescans one file, not 1,639.
* `stamped`, the `--check` short-circuit: one fingerprint of every file the generators read or
  compare (the source tree, the docs, the tooling, the corpus, the datamap records, the SDK
  headers, the family files). A `--check` that passed on the same fingerprint replays its recorded
  output instead of running; anything else runs, and a pass is recorded.

The cache is a convenience, never an input: a missing, torn or foreign file is a miss, a failed
write is ignored, and `ELYSIUM_KERNEL_CACHE=off` builds everything from scratch and stores nothing.
"""

from __future__ import annotations

import contextlib
import hashlib
import io
import json
import os
import pickle
import sqlite3
import sys
import time
from pathlib import Path
from typing import Callable, Iterable

_HERE = Path(__file__).resolve().parent
# Bump when a pickled shape changes meaning without a code change the digests would see.
VERSION = "1"
# Files kept per stage: one per checkout or corpus a work root serves at a time.
KEEP = 4

_content: dict[str, str] = {}
_fingerprints: dict[tuple, str] = {}


def enabled() -> bool:
    return os.environ.get("ELYSIUM_KERNEL_CACHE", "").strip().lower() not in ("off", "0", "no", "false")


def root() -> Path | None:
    """`$ELYSIUM_WORK_ROOT/cache/kernel`, or None when caching is off or no work root is set."""
    if not enabled():
        return None
    try:
        from elysium_pipeline.paths import cache_root
        return cache_root() / "kernel"
    except Exception:  # noqa: BLE001 -- no work root: build, store nothing
        return None


# -- keys ------------------------------------------------------------------------------------------

def digest(*parts) -> str:
    return hashlib.sha1(repr((VERSION, parts)).encode("utf-8")).hexdigest()[:24]


def content(path: Path | str) -> str:
    """The sha1 of a file's bytes ('' when it is absent), read once per process."""
    key = str(path)
    if key not in _content:
        try:
            _content[key] = hashlib.sha1(Path(path).read_bytes()).hexdigest()
        except OSError:
            _content[key] = ""
    return _content[key]


def stat(path: Path | str) -> tuple:
    """A data file as (path, size, mtime): the corpus and the records are hundreds of MB, and they
    live in the one work root every checkout shares, so their path is the same everywhere."""
    try:
        st = os.stat(path)
        return (str(path), st.st_size, st.st_mtime_ns)
    except OSError:
        return (str(path), None, None)


def code(*names: str) -> tuple:
    """Content digests of the modules a stage is built by, by path relative to this directory."""
    return tuple((name, content(_HERE / name)) for name in names)


def forget() -> None:
    """Drop this process's digests and fingerprints: the tree was just written."""
    _content.clear()
    _fingerprints.clear()


# -- pickled stages --------------------------------------------------------------------------------

def load(stage: str, key: str | None):
    base = root()
    if base is None or key is None:
        return None
    path = base / f"{stage}-{key}.pickle"
    try:
        with open(path, "rb") as stream:
            value = pickle.load(stream)
    except Exception:  # noqa: BLE001 -- absent, torn by a concurrent writer, or another version's
        return None
    with contextlib.suppress(OSError):
        os.utime(path)   # kept: `_prune` drops the least recently used
    return value


def store(stage: str, key: str | None, value, keep: int = KEEP) -> None:
    base = root()
    if base is None or key is None:
        return
    path = base / f"{stage}-{key}.pickle"
    tmp = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    try:
        base.mkdir(parents=True, exist_ok=True)
        with open(tmp, "wb") as stream:
            pickle.dump(value, stream, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, path)
    except OSError:
        # Another process holds the file open (Windows refuses the replace): its copy serves.
        with contextlib.suppress(OSError):
            tmp.unlink()
        return
    _prune(base, stage, path, keep)


def _prune(base: Path, stage: str, written: Path, keep: int) -> None:
    files = []
    for path in base.glob(f"{stage}-*.pickle"):
        with contextlib.suppress(OSError):
            files.append((path.stat().st_mtime, path))
    files.sort(reverse=True)
    for _, path in files[keep:]:
        if path != written:
            with contextlib.suppress(OSError):
                path.unlink()
    for path in base.glob(f"{stage}-*.tmp"):
        with contextlib.suppress(OSError):
            if time.time() - path.stat().st_mtime > 3600:
                path.unlink()


class FileScans:
    """Per-file results of one scan, kept across processes on each file's (size, mtime).

    `get` answers a file from the cache when its size and mtime are the ones recorded, and calls
    `scan` otherwise; `save` drops the files this run did not ask about and writes the table back
    when anything changed. One table per checkout and scanner version (`key` carries both), and
    `keep` of them per stage, so every checkout a work root serves keeps its own."""

    def __init__(self, stage: str, key: str, keep: int = 2 * KEEP):
        self.stage, self.key, self.keep = stage, key, keep
        loaded = load(stage, key)
        self.entries: dict[str, tuple] = loaded if isinstance(loaded, dict) else {}
        self.asked: set[str] = set()
        self.dirty = False

    def get(self, path: Path, rel: str, flags, scan: Callable[[], object]):
        st = path.stat()
        sig = (st.st_size, st.st_mtime_ns, flags)
        self.asked.add(rel)
        hit = self.entries.get(rel)
        if hit is not None and hit[0] == sig:
            return hit[1]
        value = scan()
        self.entries[rel] = (sig, value)
        self.dirty = True
        return value

    def save(self) -> None:
        gone = [rel for rel in self.entries if rel not in self.asked]
        for rel in gone:
            del self.entries[rel]
        if self.dirty or gone:
            store(self.stage, self.key, self.entries, self.keep)
            self.dirty = False


def checkout(repo: Path) -> str:
    """A short tag for one checkout: per-file tables and stamps are per tree."""
    return hashlib.sha1(str(Path(repo).resolve()).lower().encode("utf-8")).hexdigest()[:10]


# -- the ledger, the SDK index, the shape: their keys ------------------------------------------------

# The modules each stage's state is computed by. `corpus.py` is not one: the ledger reads the SQLite
# itself and asks `corpus` only for the directory and, at render time, the damage words.
LEDGER_CODE = ("kernel_ledger.py", "datamap_layout.py", "kernel_cache.py",
               "../../probes/npc_translation_survey.py", "../../probes/weapon_activity_survey.py")
SHAPE_CODE = ("kernel_shape.py", "name_passes.py", "datamap_layout.py", "sdk_layout.py",
              "kernel_cache.py")
SDK_CODE = ("sdk_layout.py", "kernel_cache.py")


def ledger_key(ledger) -> str:
    """The corpus stage's inputs: both SQLite files, the datamap records (field types), the image
    the RTTI walk reads for the bases (and the committed table it falls back to), the modules."""
    import corpus
    from elysium_pipeline.paths import research_root

    base = corpus._corpus_dir()
    binary = (ledger.meta.get("binary") or "").strip()
    fallback = ledger.repo / "Source/ElysiumUE/Private/Visual/ElysiumNpcActivityTables.cpp"
    return digest("ledger", ledger.module, ledger.depth,
                  stat(base / "corpus.sqlite"), stat(base / "listing.sqlite"),
                  stat(research_root() / "ghidra" / "types" / f"datamap_records-{ledger.module}.json"),
                  stat(binary) if binary else None, content(fallback), code(*LEDGER_CODE))


def sdk_headers(sdk_root: Path) -> list[tuple]:
    """(path, size, mtime) of every header the index reads, in a stable order."""
    import sdk_layout

    out = []
    for sub in sdk_layout.HEADER_DIRS:
        out += [entry for entry in _walk(sdk_root / sub) if entry[0].endswith(".h")]
    return sorted(out)


def sdk_index(research: Path):
    """The SDK 2013 index, unpickled when its headers and parser are the ones it was built from.
    None when no SDK tree is on the machine."""
    import sdk_layout

    sdk_root = sdk_layout.sdk_root(research)
    if sdk_root is None:
        return None
    key = digest("sdk", str(sdk_root), sdk_headers(sdk_root), code(*SDK_CODE))
    sdk = load("sdk", key)
    if not isinstance(sdk, sdk_layout.Sdk):
        sdk = sdk_layout.Sdk(sdk_root)
    sdk.cache_key = key
    sdk.cache_size = sdk.answers()
    return sdk


def keep_sdk(sdk) -> None:
    """Store the index when this run taught it answers it did not carry (the per-class memos)."""
    if sdk is not None and getattr(sdk, "cache_key", None) and sdk.answers() != sdk.cache_size:
        store("sdk", sdk.cache_key, sdk)
        sdk.cache_size = sdk.answers()


def shape_key(ledger, sdk, records) -> str | None:
    """The shape's corpus half: the ledger stage, the SDK index, the datamap records and the
    modules. The two reading overlays are applied after it, so a recorded reading keeps it. None
    (nothing kept) for a ledger `kernel_ledger.build` did not key."""
    if getattr(ledger, "cache_key", None) is None:
        return None
    return digest("shape", ledger.cache_key, getattr(sdk, "cache_key", None),
                  stat(records.path), code(*SHAPE_CODE))


# -- the `--check` stamp ---------------------------------------------------------------------------

# Under the checkout: everything a kernel generator reads or compares, and the code it runs (this
# directory with its overlays, the generators beside it, the two RTTI probes, the pipeline modules
# the reach cut and the tunables import). `docs/specs` is scanned by the ledger but reaches no
# output -- a spec citation feeds only `Ledger.interior`, which `cites` filters to a table's own
# members and `_render_index` to `docs/vtmb/` -- so a spec edit does not unseal a stamp.
REPO_ROOTS = ("Source/ElysiumUE", "docs/vtmb", "research/tooling/ghidra/driver",
              "research/tooling/kernel.py", "research/tooling/gen_kernel_shape.py",
              "research/tooling/gen_kernel_bindings.py", "research/tooling/gen_kernel_tunables.py",
              "research/tooling/probes/npc_translation_survey.py",
              "research/tooling/probes/weapon_activity_survey.py",
              "pipeline/src/elysium_pipeline/formats", "pipeline/src/elysium_pipeline/paths.py")


def _walk(base: Path) -> list[tuple[str, int, int]]:
    """(path, size, mtime) of every file under `base` (or of `base` itself), `__pycache__` aside.
    `os.scandir` answers the stat from the directory listing on Windows: 16k files in ~0.1 s."""
    if base.is_file():
        st = base.stat()
        return [(str(base), st.st_size, st.st_mtime_ns)]
    out = []
    stack = [str(base)]
    while stack:
        folder = stack.pop()
        try:
            entries = os.scandir(folder)
        except OSError:
            continue
        with entries:
            for entry in entries:
                if entry.is_dir(follow_symlinks=False):
                    if entry.name != "__pycache__":
                        stack.append(entry.path)
                    continue
                with contextlib.suppress(OSError):
                    st = entry.stat(follow_symlinks=False)
                    out.append((entry.path, st.st_size, st.st_mtime_ns))
    return out


def fingerprint(repo: Path, extra: Iterable[Path] = ()) -> str:
    """One hash over every file the kernel generators read or compare, by (path, size, mtime):
    the checkout's sources, docs and tooling; the corpus, the datamap records and the image the
    corpus was dumped from; the SDK headers; the story-8 family files; and `extra` (a tool's own
    inputs: the reach cut's maps and schedule corpus, the tunables' image)."""
    extra = tuple(str(p) for p in extra)
    memo = (str(repo), extra)
    if memo in _fingerprints:
        return _fingerprints[memo]
    from elysium_pipeline.paths import research_root

    import sdk_layout

    research = research_root()
    corpus_dir = research / "ghidra" / "corpus"
    files: list[tuple] = []
    for rel in REPO_ROOTS:
        files += _walk(repo / rel)
    for path in (corpus_dir / "corpus.sqlite", corpus_dir / "listing.sqlite"):
        files.append(stat(path))
    files += _walk(research / "ghidra" / "types")
    files += _walk(research / "npc-kernel-checklist" / "families-19-29")
    with contextlib.suppress(Exception):
        db = sqlite3.connect(f"file:{corpus_dir / 'corpus.sqlite'}?mode=ro", uri=True)
        try:
            files += [stat(row[0]) for row in db.execute("SELECT DISTINCT binary FROM meta") if row[0]]
        finally:
            db.close()
    sdk_root = sdk_layout.sdk_root(research)
    if sdk_root is not None:
        files += sdk_headers(sdk_root)
    for path in extra:
        files += _walk(Path(path)) if Path(path).exists() else [stat(path)]
    blob = "\n".join(f"{p}|{s}|{m}" for p, s, m in sorted(files, key=lambda f: f[0]))
    head = repr((VERSION, sys.version, str(Path(repo).resolve()), str(research), extra))
    _fingerprints[memo] = hashlib.sha1((head + "\n" + blob).encode("utf-8", "surrogatepass")).hexdigest()
    return _fingerprints[memo]


class _Tee(io.TextIOBase):
    """stdout as it was, and a copy of every write."""

    def __init__(self, inner):
        self.inner = inner
        self.copy = io.StringIO()

    @property
    def encoding(self):
        return getattr(self.inner, "encoding", "utf-8")

    def write(self, text: str) -> int:
        self.copy.write(text)
        return self.inner.write(text)

    def flush(self) -> None:
        self.inner.flush()


def _stamps_path(base: Path, repo: Path) -> Path:
    return base / f"stamps-{checkout(repo)}.json"


def _read_stamps(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001 -- absent or torn: no stamps
        return {}


def stamped(tool: str, options: dict, repo: Path, run: Callable[[], int],
            extra: Iterable[Path] = ()) -> int:
    """`run` a `--check`, or replay the last passing one when nothing it reads has changed.

    The record is the tool, its options and the fingerprint (`fingerprint`) taken before the run,
    with the run's stdout; only a pass (`run` answers 0) is recorded. A replay prints that stdout
    byte for byte and answers 0."""
    base = root()
    if base is None:
        return run()
    name = f"{tool} {json.dumps(options, sort_keys=True, default=str)}"
    seal = fingerprint(repo, extra)
    path = _stamps_path(base, repo)
    hit = _read_stamps(path).get(name)
    if hit and hit.get("fingerprint") == seal:
        sys.stdout.write(hit.get("stdout", ""))
        sys.stdout.flush()
        return 0
    tee = _Tee(sys.stdout)
    with contextlib.redirect_stdout(tee):
        status = run()
    if status == 0:
        stamps = _read_stamps(path)
        stamps[name] = {"fingerprint": seal, "stdout": tee.copy.getvalue(),
                        "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
        tmp = path.with_name(f"{path.name}.{os.getpid()}.tmp")
        try:
            base.mkdir(parents=True, exist_ok=True)
            tmp.write_text(json.dumps(stamps, indent=1, sort_keys=True), encoding="utf-8")
            os.replace(tmp, path)
        except OSError:
            with contextlib.suppress(OSError):
                tmp.unlink()
    return status
