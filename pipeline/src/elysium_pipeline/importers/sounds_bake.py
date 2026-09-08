"""Stage the V2 sound corpus as importable audio files for `uv run elysium bake sounds`.

The lane lands every `$ELYSIUM_EXPORT_V2_ROOT/sounds/**.glb` unit as one `USoundWave` below
`/ElysiumBaked/Sounds`. This module is the offline half; `pipeline/unreal/import_sounds.py` is
the editor half and reads the `manifest.json` this writes.

**Why a bake at all.** Owner call 2026-09-08 (`docs/decisions.md`, Audio): audio rendering moves
onto Unreal sound wave assets and the hand-rolled dr_wav/dr_mp3 decode path is deleted rather
than finished. The corpus is twenty years old and never changes, so this is a one-shot lane: no
index asset, no verify pass, and no incremental machinery beyond the recipe stamp every bake lane
already gets from `bake_lib`.

**What each unit stages.**

* A `.wav` unit publishes its decode as interleaved 16-bit PCM in the GLB payload. The install
  member itself is usually MS-ADPCM, which is not an Unreal import format, so the stage writes a
  fresh 16-bit PCM `.wav` from the payload rather than passing the member through.
* An `.mp3` unit's payload is the bare MPEG frame stream and no Python decoder exists, so the
  stage writes the **original member bytes** out of the unit's source capsule. UE 5.8's
  `USoundFactory` imports `.mp3` (`SoundFactory.cpp:160`, behind `WITH_SNDFILE_IO`).
* A unit with no samples bakes nothing and is listed under `empty` in the manifest and the
  import report: 11 whose member is zero bytes (`omissions[].role == "empty-member"`) and one,
  `area/santa_monica/clinic/clinic drip flr light loop.wav`, that carries a whole MS-ADPCM
  header over a zero-length `data` chunk.

**Loop regions.** VtMB carries loop points in the RIFF `smpl` chunk and in the `cue ` chunk.
Both state one thing and one only: a loop **start**. `CAudioSourceWave`'s ctor
(`engine.dll 0x20139d60`) sets `this+0x28` to `-1`, the dispatcher `FUN_2013a030` (`0x2013a030`)
overwrites it with the loop start out of either chunk and never reads a loop end, and `IsLooped`
(vtable slot 8, `0x2013a150`) is `this+0x28 >= 0`. **Carrying either chunk is what makes a source
loop**, offset 0 included, and retail wraps at end of file. So does this bake
(`docs/vtmb/audio_pipeline.md` §12):

* a loop starting at sample 0 -> one whole-file asset with `bLooping` set, nothing trimmed
  (217 of the corpus's 220 looping units, most of them a bare `cue ` point at 0);
* a loop starting past 0 -> two assets, `SW_<name>_intro` = `[0, start)` not looping and
  `SW_<name>_loop` = `[start, last sample]` looping, which the runtime chains;
* a unit carrying neither chunk is one plain asset with `bLooping` clear.

The `smpl` chunk's own `end` field is carried through to the manifest as `authoredEndSample` for
the record and is never cut at: trimming there would end the loop body 7 ms early on
`rain_light_loop.wav` and 29 ms early on `flow_on.wav`, which is not what retail plays.

The split is a sample-range slice of the decoded payload, so it only applies to `.wav` units; an
`.mp3` unit cannot carry a RIFF chunk and is always staged whole.

Staging is idempotent (a file is rewritten only when its bytes change) and prunes: anything under
the staging root this run did not produce, other than the root reports, is deleted -- unless the
run was scoped to a key selection, which stages and prunes nothing outside the selection.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import struct
from typing import Any, Iterable, Mapping, Sequence

from elysium_pipeline.asset_paths import assert_unique_paths, baked_unit
from elysium_pipeline.formats.sound_glb.model import (
    SOUND_EXTENSION,
    key_extension,
    normalize_key,
)
from elysium_pipeline.formats.unit_contract.capsule import extract_source_member
from elysium_pipeline.formats.unit_contract.container import GlbContainerError, decode_glb

#: The family directory this lane reads below the export_v2 root.
FAMILY = "sounds"
#: The package every asset lands under; the twin of `FElysiumContentPaths::BakedUnit`'s root.
PACKAGE_ROOT = "/ElysiumBaked/Sounds"
#: The Unreal class every asset is imported as.
ASSET_CLASS = "SoundWave"
#: The class prefix `asset_paths` products carry.
ASSET_PREFIX = "SW"
#: Bumped whenever the staged bytes or the settings mapping change in a way that must re-import.
#: v2: a loop body runs to the last sample instead of the `smpl` chunk's `end`, because retail
#: reads no loop end at all (`FUN_2013a030`) and wraps at end of file.
#: v3: carrying a `cue ` chunk is itself a loop whatever its offset, because `IsLooped`
#: (`0x2013a150`) is `this+0x28 >= 0` against a `-1` the ctor (`0x20139d60`) wrote.
#: v4: the sound fold maps a space to `-` and lets a stem keep a leading underscore
#: (`asset_names.sound_safe_name`), so every key addresses one asset and no two share one.
RECIPE_VERSION = "elysium-sound-bake-v4"
MANIFEST_SCHEMA = "1.0.0"
MANIFEST_NAME = "manifest.json"
#: Written by the editor phase; read back by the CLI for its summary.
IMPORT_REPORT_NAME = "import_report.json"
#: The recipe ledger `bake_lib.RecipeLedger` keeps beside the manifest.
RECIPES_NAME = "recipes.json"
#: Root files that are never pruned from the staging tree.
ROOT_FILES = frozenset({MANIFEST_NAME, IMPORT_REPORT_NAME, RECIPES_NAME})

#: Compression for every baked wave: the project's own default, set once in project settings
#: rather than per asset. Everything else is left at the engine default by owner call.
COMPRESSION = "ProjectDefined"

#: The role suffixes a split loop publishes. A whole-file or trimmed unit has no role.
INTRO_ROLE = "intro"
LOOP_ROLE = "loop"


class SoundBakeError(RuntimeError):
    """A sound unit cannot be staged as written."""


# --- roots and selection --------------------------------------------------------------------------


def staging_root(work_root: Path) -> Path:
    """`$ELYSIUM_WORK_ROOT/_sounds_stage/` -- the staged audio, manifest and import report."""

    return Path(work_root) / "_sounds_stage"


def unit_root(export_v2_root: Path) -> Path:
    return Path(export_v2_root) / FAMILY


def units(export_v2_root: Path) -> list[Path]:
    """Every published sound unit, in a stable order."""

    root = unit_root(export_v2_root)
    return sorted(root.rglob("*.glb")) if root.is_dir() else []


def unit_key(unit: Path, export_v2_root: Path) -> str:
    """`<root>/sounds/music/theme.mp3.glb` -> `music/theme.mp3`."""

    relative = Path(unit).resolve().relative_to(unit_root(export_v2_root).resolve())
    return normalize_key(relative.as_posix()[: -len(".glb")])


def unit_path(export_v2_root: Path, key: str) -> Path:
    return unit_root(export_v2_root) / (normalize_key(key) + ".glb")


def parse_keys(values: Iterable[str] | None) -> list[str] | None:
    """The `--keys` selection, normalized; `None` when the run is the whole corpus.

    Each value may itself be a comma-separated list, so one flag can carry a sample set.
    """

    if values is None:
        return None
    keys: list[str] = []
    for value in values:
        for token in str(value).split(","):
            token = token.strip().strip('"')
            if token:
                keys.append(normalize_key(token))
    if not keys:
        return None
    seen: dict[str, None] = {}
    for key in keys:
        seen.setdefault(key, None)
    return list(seen)


def asset_path_for(key: str, role: str | None = None) -> str:
    """The package path one unit's product lands at, the twin of the runtime's own resolver."""

    return baked_unit("vtmb:sound:" + normalize_key(key), ASSET_PREFIX, role=role)


def staged_relative_path(asset_path: str, extension: str) -> str:
    """Where a product's importable file sits below the staging root.

    Named after the asset rather than the key, so the on-disk name is already folded (the corpus
    has keys with spaces and parentheses) and is unique for exactly the reason the asset path is.
    """

    if not asset_path.startswith(PACKAGE_ROOT + "/"):
        raise SoundBakeError(f"{asset_path} is outside {PACKAGE_ROOT}")
    return asset_path[len(PACKAGE_ROOT) + 1:] + extension


def prune_scope(selected: bool) -> str | None:
    """The package folder the editor phase may prune, or `None` for a scoped run.

    A run over a key selection knows nothing about the assets it did not stage, so it prunes
    nothing: pruning on a subset would delete the rest of the corpus.
    """

    return None if selected else PACKAGE_ROOT + "/"


# --- reading one unit -------------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Unit:
    """One decoded sound unit: what the stage needs off it, read once."""

    key: str
    path: Path
    sha256: str
    document: Mapping[str, Any]
    binary: bytes
    extension: Mapping[str, Any]


def read_unit(key: str, data: bytes, path: Path | str = "the unit") -> Unit:
    """Decode one unit's GLB into the record the planner reads."""

    document, binary = decode_glb(data, str(path))
    extension = (document.get("extensions") or {}).get(SOUND_EXTENSION)
    if not isinstance(extension, Mapping):
        raise SoundBakeError(f"not a {SOUND_EXTENSION} unit")
    identity = extension.get("identity") or {}
    asset_id = identity.get("asset")
    if not isinstance(asset_id, str) or not asset_id.startswith("vtmb:sound:"):
        raise SoundBakeError("unit declares no sound identity")
    return Unit(key=normalize_key(key), path=Path(path), sha256=hashlib.sha256(data).hexdigest(),
                document=document, binary=binary, extension=extension)


def is_empty(extension: Mapping[str, Any]) -> bool:
    """Whether the unit's audio member is the zero-byte file 11 corpus entries ship."""

    for omission in extension.get("omissions") or ():
        if isinstance(omission, Mapping) and omission.get("role") == "empty-member":
            return True
    payload = extension.get("payload") or {}
    return not int(payload.get("byteLength") or 0)


def payload_bytes(unit: Unit) -> bytes:
    """The decoded payload the unit published, addressed through its accessor and checked."""

    payload = unit.extension.get("payload") or {}
    index = payload.get("accessor")
    if index is None:
        raise SoundBakeError("unit publishes no payload accessor")
    accessors = list(unit.document.get("accessors") or ())
    if not isinstance(index, int) or isinstance(index, bool) or not 0 <= index < len(accessors):
        raise SoundBakeError(f"payload accessor {index!r} is not one of this unit's accessors")
    view_index = accessors[index].get("bufferView")
    views = list(unit.document.get("bufferViews") or ())
    if not isinstance(view_index, int) or not 0 <= view_index < len(views):
        raise SoundBakeError("the payload accessor names no bufferView")
    view = views[view_index]
    offset = int(view.get("byteOffset", 0)) + int(accessors[index].get("byteOffset", 0))
    length = int(view.get("byteLength", 0))
    data = bytes(unit.binary[offset:offset + length])
    declared = int(payload.get("byteLength", -1))
    if len(data) != declared:
        raise SoundBakeError(f"payload holds {len(data)} of {declared} declared bytes")
    digest = payload.get("sha256")
    if isinstance(digest, str) and hashlib.sha256(data).hexdigest() != digest:
        raise SoundBakeError("the payload bytes are not the payload the unit published")
    return data


def source_bytes(unit: Unit) -> bytes:
    """The exact install member the unit capsuled -- what an `.mp3` unit stages verbatim."""

    # By `role`, not by `identity.sourcePath`: a unit that also resolved a `.lip` companion
    # publishes `sourcePaths` (plural) and no singular key, and the audio member is the one whose
    # role is the key's own extension -- `wav` or `mp3`, never `lip`.
    wanted = key_extension(unit.key).lstrip(".")
    resolution = unit.extension.get("sourceResolution") or {}
    for member in resolution.get("members") or ():
        if not isinstance(member, Mapping) or str(member.get("role", "")) != wanted:
            continue
        data = extract_source_member(unit.document, unit.binary, member)
        digest = member.get("sha256")
        if isinstance(digest, str) and hashlib.sha256(data).hexdigest() != digest:
            raise SoundBakeError(
                f"{member.get('path')}: the capsule's bytes are not the member it names")
        return data
    raise SoundBakeError(f"the unit carries no capsuled {wanted!r} member")


# --- the loop region ---------------------------------------------------------------------------------


#: The only `smpl` loop type retail honours. `FUN_2013a030`'s `smpl` arm copies `0x3c` bytes of
#: the chunk and returns early when the dword at `+0x28` of that copy is nonzero -- which, in the
#: `smpl` layout, is `loops[0].type`. So a ping-pong (1) or reverse (2) loop is refused outright
#: and the file does not loop at all; only a forward loop reaches `this+0x28`.
FORWARD_LOOP = 0


@dataclass(frozen=True, slots=True)
class LoopRegion:
    """One authored loop: where it starts, who said so, and the end the source states.

    `end` is always the member's **last sample**, because that is what retail plays.
    `CAudioSourceWave`'s RIFF dispatcher `FUN_2013a030` (`engine.dll 0x2013a030`) keeps exactly
    one dword out of an `smpl` or a `cue ` chunk -- the loop **start**, at `this+0x28` -- and
    wraps at end of file. `authored_end` is the `smpl` chunk's own end field, carried for the
    record only; no product is trimmed at it.
    """

    source: str
    start: int
    end: int
    authored_end: int | None = None


def loop_region(extension: Mapping[str, Any], frames: int) -> LoopRegion | None:
    """The loop a unit's RIFF chunks author, or `None`.

    **Presence of the chunk is the loop.** `CAudioSourceWave::CAudioSourceWave`
    (`engine.dll 0x20139d60`) initialises `this+0x28` to `-1`, `FUN_2013a030` overwrites it with
    the `smpl` loop start or the `cue ` point's offset -- the `cue ` arm unconditionally -- and
    `IsLooped` (vtable slot 8, `0x2013a150`) is exactly `this+0x28 >= 0`. So a `cue ` chunk whose
    point sits at sample 0 loops the whole file just as loudly as one at 6077, and a source with
    neither chunk is the only one that does not loop.

    The chunks are walked in **file order** and the last writer wins, because that is what a
    per-chunk dispatcher does: a second `smpl` overwrites the first, and a `cue ` after an `smpl`
    overwrites that. Only `loops[0]` of each `smpl` is reachable -- the fixed `0x3c`-byte copy
    stops there -- and a non-forward loop type makes the arm return without writing, leaving
    whatever the previous chunk left.

    A degenerate `start 0 end 0` loop is not a special case: it is a start of 0, which is a
    whole-file loop. A start past the decode is refused rather than clamped -- it would name a
    loop body with no samples in it. No corpus unit ships one, and retail has no such guard.
    """

    region: LoopRegion | None = None
    for chunk in extension.get("chunks") or ():
        if not isinstance(chunk, Mapping):
            continue
        body = chunk.get("body") or {}
        if chunk.get("id") == "smpl":
            loops = body.get("loops") or ()
            loop = loops[0] if loops and isinstance(loops[0], Mapping) else None
            if loop is None or int(loop.get("type", FORWARD_LOOP)) != FORWARD_LOOP:
                continue
            start = int(loop.get("start", 0))
            if 0 <= start < frames:
                region = LoopRegion("smpl", start, frames - 1, int(loop.get("end", 0)))
        elif chunk.get("id") == "cue ":
            points = body.get("points") or ()
            first = points[0] if points and isinstance(points[0], Mapping) else {}
            start = int(first.get("sampleOffset", 0))
            if 0 <= start < frames:
                region = LoopRegion("cue", start, frames - 1, None)
    return region


# --- writing a PCM wav -------------------------------------------------------------------------------


def pcm_wav(payload: bytes, *, channels: int, sample_rate: int,
            first: int = 0, count: int | None = None) -> bytes:
    """A 16-bit PCM RIFF/WAVE file over `[first, first + count)` frames of `payload`.

    `payload` is the unit's interleaved int16 decode, so a frame is `2 * channels` bytes; the
    slice is a frame slice, never a byte slice, or a stereo trim would swap the channels.
    """

    if channels < 1:
        raise SoundBakeError(f"a wav cannot have {channels} channels")
    if sample_rate < 1:
        raise SoundBakeError(f"a wav cannot play at {sample_rate} Hz")
    stride = 2 * channels
    if len(payload) % stride:
        raise SoundBakeError(
            f"payload of {len(payload)} bytes is not whole frames of {stride} bytes")
    total = len(payload) // stride
    first = max(0, min(int(first), total))
    count = total - first if count is None else max(0, min(int(count), total - first))
    samples = payload[first * stride:(first + count) * stride]
    fmt = struct.pack("<HHIIHH", 1, channels, sample_rate, sample_rate * stride, stride, 16)
    body = (b"WAVE"
            + b"fmt " + struct.pack("<I", len(fmt)) + fmt
            + b"data" + struct.pack("<I", len(samples)) + samples
            + (b"\0" if len(samples) % 2 else b""))
    return b"RIFF" + struct.pack("<I", len(body)) + body


# --- planning one unit's products -----------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class Product:
    """One asset a unit stages: the file to import and everything the manifest states about it."""

    asset_path: str
    role: str | None
    data: bytes
    extension: str
    looping: bool
    loop: dict[str, Any]


def plan(unit: Unit) -> list[Product]:
    """Every asset one unit publishes. An empty-member unit publishes none."""

    if is_empty(unit.extension):
        return []
    key = unit.key
    codec = unit.extension.get("codec") or {}
    sample_format = str((unit.extension.get("payload") or {}).get("sampleFormat") or "")

    if sample_format == "mpeg-frames":
        # No Python MPEG decoder exists and the frame stream is not a container Unreal reads, so
        # the install member itself is what gets imported. A frame stream carries no RIFF chunk,
        # so an mp3 unit never has an authored loop to split on.
        data = source_bytes(unit)
        return [Product(asset_path_for(key), None, data, ".mp3", False,
                        {"decision": "none", "source": None, "startSample": None,
                         "endSample": None, "firstSample": 0, "sampleCount": None})]

    if sample_format != "int16-interleaved":
        raise SoundBakeError(f"unknown payload sample format {sample_format!r}")

    payload = payload_bytes(unit)
    channels = int(codec.get("channels") or 0)
    sample_rate = int(codec.get("sampleRate") or 0)
    stride = 2 * max(channels, 1)
    frames = len(payload) // stride if channels else 0
    if not frames:
        return []
    region = loop_region(unit.extension, frames)

    def wav(first: int, count: int) -> bytes:
        return pcm_wav(payload, channels=channels, sample_rate=sample_rate,
                       first=first, count=count)

    if region is None:
        return [Product(asset_path_for(key), None, wav(0, frames), ".wav", False,
                        {"decision": "none", "source": None, "startSample": None,
                         "endSample": None, "authoredEndSample": None,
                         "firstSample": 0, "sampleCount": frames})]
    # The loop body always runs to the last sample: retail keeps only the loop start and wraps at
    # end of file, so nothing is ever trimmed off the tail.
    if region.start == 0:
        return [Product(asset_path_for(key), None, wav(0, frames), ".wav", True,
                        {"decision": "whole", "source": region.source, "startSample": 0,
                         "endSample": region.end, "authoredEndSample": region.authored_end,
                         "firstSample": 0, "sampleCount": frames})]
    tail = frames - region.start
    return [
        Product(asset_path_for(key, INTRO_ROLE), INTRO_ROLE, wav(0, region.start), ".wav", False,
                {"decision": "intro", "source": region.source, "startSample": region.start,
                 "endSample": region.end, "authoredEndSample": region.authored_end,
                 "firstSample": 0, "sampleCount": region.start}),
        Product(asset_path_for(key, LOOP_ROLE), LOOP_ROLE, wav(region.start, tail), ".wav", True,
                {"decision": "loop", "source": region.source, "startSample": region.start,
                 "endSample": region.end, "authoredEndSample": region.authored_end,
                 "firstSample": region.start, "sampleCount": tail}),
    ]


def entry_for(unit: Unit, product: Product) -> dict[str, Any]:
    """One manifest row: the staged file, the settings and the recipe the stamp hashes."""

    file = staged_relative_path(product.asset_path, product.extension)
    settings = {"compression": COMPRESSION, "looping": product.looping}
    recipe = {
        "version": RECIPE_VERSION,
        "sha256": hashlib.sha256(product.data).hexdigest(),
        "settings": settings,
    }
    return {
        "assetPath": product.asset_path,
        "class": ASSET_CLASS,
        "unit": unit.key,
        "role": product.role,
        "file": file,
        "format": product.extension.lstrip("."),
        "byteLength": len(product.data),
        "looping": product.looping,
        "loop": dict(product.loop),
        "settings": settings,
        "recipe": recipe,
    }


# --- the run ------------------------------------------------------------------------------------------


def report_collisions(entries: Sequence[Mapping[str, Any]]) -> None:
    """Refuse the whole run, naming **every** pair of units that folds onto one asset path.

    `assert_unique_paths` is the contract check and raises on the first collision it meets, which
    is the right answer for a two-unit lane and a useless one for an 11k-unit corpus: the run has
    to be told what the whole set is before anyone can decide what to do about it. This raises
    first, listing them all, and the contract check still runs behind it.
    """

    owners: dict[str, list[str]] = {}
    for entry in entries:
        identity = str(entry["unit"]) + (entry["role"] or "")
        owners.setdefault(str(entry["assetPath"]).casefold(), []).append(identity)
    collisions = {path: sorted(set(names)) for path, names in owners.items()
                  if len(set(names)) > 1}
    if not collisions:
        return
    lines = [f"{len(collisions)} baked sound path(s) are claimed by more than one unit; the fold "
             "cannot address them and the manifest is refused:"]
    for path, names in sorted(collisions.items()):
        lines.append(f"  {path}")
        lines.extend(f"      {name!r}" for name in names)
    raise SoundBakeError("\n".join(lines))


@dataclass(slots=True)
class StageResult:
    staging_root: Path
    manifest_path: Path | None = None
    staged: int = 0
    unchanged: int = 0
    pruned: int = 0
    assets: int = 0
    read: int = 0
    empty: list[dict[str, Any]] = field(default_factory=list)
    loops: dict[str, int] = field(default_factory=dict)
    failures: list[tuple[str, str]] = field(default_factory=list)

    def summary(self) -> str:
        loops = ", ".join(f"{count} {name}" for name, count in sorted(self.loops.items())) or "none"
        return (
            f"sound staging: {self.assets} assets from {self.read} unit(s) "
            f"({self.staged} written, {self.unchanged} unchanged, {self.pruned} pruned, "
            f"{len(self.empty)} empty, {len(self.failures)} failed); loops: {loops} "
            f"-> {self.staging_root}"
        )


def _write(path: Path, data: bytes) -> bool:
    """Write `data` at `path` only when it differs; returns whether bytes were written."""

    if path.is_file() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)
    return True


def _prune(root: Path, produced: set[Path]) -> int:
    """Delete every staged file this run did not produce, then every emptied directory."""

    removed = 0
    for path in sorted(root.rglob("*")):
        if path.is_dir() or (path.parent == root and path.name in ROOT_FILES):
            continue
        if path.resolve() not in produced:
            path.unlink()
            removed += 1
    for path in sorted(root.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        if path.is_dir() and not any(path.iterdir()):
            path.rmdir()
    return removed


def stage_sounds(export_v2_root: Path, root: Path, *,
                 keys: Sequence[str] | None = None) -> StageResult:
    """Stage every selected sound unit and write the manifest the editor phase executes."""

    export_v2_root = Path(export_v2_root)
    root = Path(root)
    selected = keys is not None
    if selected:
        pairs = [(normalize_key(key), unit_path(export_v2_root, key)) for key in keys]
    else:
        pairs = [(unit_key(path, export_v2_root), path) for path in units(export_v2_root)]
    if not pairs:
        raise SoundBakeError(
            f"no sound units below {unit_root(export_v2_root)}; run "
            "`uv run elysium export_v2 sounds-glb` first"
        )
    root.mkdir(parents=True, exist_ok=True)
    result = StageResult(staging_root=root)
    entries: list[dict[str, Any]] = []
    produced: set[Path] = set()

    for key, path in pairs:
        try:
            if not path.is_file():
                raise SoundBakeError(f"no published unit at {path}")
            unit = read_unit(key, path.read_bytes(), path)
            result.read += 1
            if is_empty(unit.extension):
                identity = unit.extension.get("identity") or {}
                result.empty.append({"unit": unit.key,
                                     "sourcePath": identity.get("sourcePath"),
                                     "reason": "empty-member"})
                continue
            products = plan(unit)
            if not products:
                result.empty.append({"unit": unit.key, "sourcePath": None,
                                     "reason": "no samples"})
                continue
            for product in products:
                entry = entry_for(unit, product)
                destination = root / Path(entry["file"].replace("/", os.sep))
                if _write(destination, product.data):
                    result.staged += 1
                else:
                    result.unchanged += 1
                produced.add(destination.resolve())
                entries.append(entry)
                decision = str(entry["loop"]["decision"])
                result.loops[decision] = result.loops.get(decision, 0) + 1
        except (SoundBakeError, GlbContainerError, OSError, ValueError) as error:
            result.failures.append((key, f"{error}"))

    report_collisions(entries)
    assert_unique_paths((entry["unit"] + (entry["role"] or ""), entry["assetPath"])
                        for entry in entries)
    entries.sort(key=lambda entry: entry["assetPath"].casefold())
    result.assets = len(entries)
    if not selected:
        result.pruned = _prune(root, produced)

    manifest = {
        "schemaVersion": MANIFEST_SCHEMA,
        "packageRoot": PACKAGE_ROOT,
        "pruneScope": prune_scope(selected),
        "select": [key for key, _ in pairs] if selected else None,
        "keep": [],
        "empty": sorted(result.empty, key=lambda row: str(row["unit"])),
        "assets": entries,
    }
    manifest_path = root / MANIFEST_NAME
    _write(manifest_path, json.dumps(manifest, indent=1, sort_keys=True).encode("utf-8"))
    result.manifest_path = manifest_path
    return result
