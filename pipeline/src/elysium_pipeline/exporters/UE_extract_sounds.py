"""Build the patch-first audio mirror and its runtime catalog under ``$ELYSIUM_EXPORT_ROOT/audio``.

P6 6.1 (WAV) + 6.2 (MP3) asset-delivery step. The runtime decodes VtMB's audio *in C++* --
dr_wav for the proprietary WAV encodings (Microsoft ADPCM tag 0x02, IMA ADPCM 0x11, PCM16),
dr_mp3 for the loose dialogue/music/radio MP3s -- so this step does **no transcode**. It only
copies the referenced `.wav`/`.mp3` bytes 1:1 out of the install (patch-first) into a shared,
game-global mirror `$ELYSIUM_EXPORT_ROOT/sound/<relpath>`, matching the engine's `sound/` tree. Sounds are
shared across maps, so one mirror is written, not a per-map copy.

Refs come from the already-exported `.ents` sidecars: every entity keyvalue whose value ends
in `.wav`/`.mp3` (chiefly `ambient_generic.message` -- WAV in practice). The `.ents` must exist
first, so this runs after the map export (export_all.py calls it at the end of a run).

The dialogue MP3s (referenced by `.dlg`/Python) arrive with their own owners in P9. As canonical
MP3 decode test material in the meantime, `--radio` mirrors whatever loose radio loops the install
carries under `sound/radio/*.mp3` (discovered from the index, not a fixed list), which no map
references directly.

**PL5a -- SoundScheme copies (6.3).** Each map's `ambient_soundscheme` entities point at a
`sound/Schemes/*.txt` scheme file (KeyValues). This step also mirrors those `.txt` verbatim into
$ELYSIUM_EXPORT_ROOT/sound/Schemes/ and parses each for its `Filename` refs (Music/Combat/Alert/Ambient/RandomSound
music+ambient WAV/MP3), copying those assets into the same `$ELYSIUM_EXPORT_ROOT/sound/` mirror -- so a scheme has
its bed/music/one-shots on disk for the runtime FElysiumSoundSchemeManager to play. `--no-schemes`
skips it.

Output lives under $ELYSIUM_EXPORT_ROOT/ (gitignored, regenerable) -- same bring-your-own-game posture as
the maps and textures.

**6.4 -- mover soundgroups.** Doors (`func_door*`) and buttons (`func_button*`) carry a
`soundgroup` token (e.g. `standard_door`, `small_metal_switch`) that VtMB resolves *by
directory convention*, not through any data file -- the `.wav`s live under
`sound/usable/<category>/<soundgroup>/<subkey>.wav` (RE-confirmed: `vampire.dll`
`CBaseDoor::Spawn` @0x100ef060 reads the subkeys `open`/`close`/`swing`/`locked`;
`CBaseButton::Spawn` @0x100c8810 reads `on`/`off`). This step mirrors each referenced
soundgroup's `.wav`s into `$ELYSIUM_EXPORT_ROOT/sound/usable/...` and writes a manifest
`$ELYSIUM_EXPORT_ROOT/sound/usable/soundgroups.json` ({category: {group: {subkey: relpath}}}) the runtime
loads to resolve a token -> its subkey WAVs. Explicit button `locked_sound`/`unlocked_sound`
are direct WAV paths, so they arrive via the ordinary `collect_audio_refs` path already.

Usage:
  uv run elysium export bundle audio  # every exported map under $ELYSIUM_EXPORT_ROOT/ (+ schemes)

Focused per-map audio export is coordinated by `uv run elysium export map <map>`.
Radio and scheme switches are internal exporter controls, not public project commands.
"""
import glob
import json
import os
import struct
import sys

from elysium_pipeline.formats import install, kv
from elysium_pipeline.paths import export_root

OUT = os.fspath(export_root())
SOUND_OUT = os.path.join(OUT, "sound")
AUDIO_OUT = os.path.join(OUT, "audio")
AUDIO_CONTRACT_VERSION = 1

# Audio references the runtime can decode today: WAV (dr_wav) + MP3 (dr_mp3).
AUDIO_EXTS = (".wav", ".mp3")

# Index the sound tree too, so a patch's loose-sound override shadows the VPK copy
# (build_index's default dirs omit `sound`; the VPK sounds are indexed regardless).
SOUND_DIRS = install.ASSET_DIRS + ("sound",)


def _iter_ents(paths):
    """Yield each entity dict from the given `.ents` JSON files."""
    for p in paths:
        try:
            with open(p, encoding="utf-8") as f:
                doc = json.load(f)
        except (OSError, ValueError) as e:
            print(f"  ! {os.path.basename(p)}: unreadable ({e})", flush=True)
            continue
        for ent in doc.get("entities", []):
            yield ent


def collect_audio_refs(ents_paths):
    """Every distinct `.wav`/`.mp3` path referenced by any entity keyvalue, as authored.

    Case is preserved from the data (the install index resolves case-insensitively);
    a set collapses the heavy reuse (one `transformer_hum.wav` across many entities)."""
    refs = set()
    for ent in _iter_ents(ents_paths):
        for v in ent.get("keys", {}).values():
            if isinstance(v, str) and v.lower().endswith(AUDIO_EXTS):
                # A leading slash is authored ("/Area/.../train_bell.wav") and the engine's
                # path layer swallows it; joined raw it turns 49 shipped WAVs into misses.
                refs.add(v.replace("\\", "/").lstrip("/"))
    return refs


# Mover classes that resolve a `soundgroup` token, and which `usable/` subtree they draw from
# (RE: doors -> openable subkeys open/close/swing/locked; buttons -> switches subkeys on/off).
# A soundgroup dir can exist under more than one category (e.g. `manhole_cover` is both an
# openable and a switch); the runtime picks the category from the entity's class, so the manifest
# records every category a referenced group appears in.
SOUNDGROUP_CATEGORIES = ("openable", "switches", "computers")

#: The classes that resolve their `soundgroup` token through `usable/` (doors and
#: containers -> openable, buttons/switches -> switches, hacking/keypads -> computers).
#: NPC classes carry the same key naming their VOICE set under `sound/character/`,
#: which is the dialogue system's to resolve, not this manifest's.
SOUNDGROUP_CLASSES = frozenset((
    "func_button", "func_door", "func_door_rotating",
    "item_container", "item_container_animated", "item_container_one_item_filtered",
    "prop_button", "prop_hacking", "prop_keypad", "prop_switch",
))


def collect_soundgroups(ents_paths):
    """Every distinct `soundgroup` token a usable-class entity carries (case preserved).

    An authored token of `None` is deliberate silence, not a reference."""
    groups = set()
    for ent in _iter_ents(ents_paths):
        if ent.get("classname") not in SOUNDGROUP_CLASSES:
            continue
        sg = ent.get("keys", {}).get("soundgroup")
        if isinstance(sg, str) and sg.strip() and sg.strip().lower() != "none":
            groups.add(sg.strip())
    return groups


def _group_dir_variants(group):
    """Candidate directory names for a soundgroup token. VtMB dirs are lowercase and mostly keep
    the token verbatim (incl. embedded spaces, e.g. `squeaky_metal door`); a few authored tokens
    differ only in space/underscore, so try both."""
    g = group.strip().lower()
    out = [g]
    for v in (g.replace(" ", "_"), g.replace("_", " ")):
        if v not in out:
            out.append(v)
    return out


def extract_soundgroups(groups, idx):
    """Mirror each referenced soundgroup's WAVs from `sound/usable/<cat>/<group>/*.wav` and build
    the manifest {category: {group_key: {subkey: relpath}}}. Returns (manifest, refs, unresolved)
    where refs are sound-relative WAV paths (fed into extract()) and group_key is the lowercased
    token. A token unresolved in every category is reported (a soundgroup with no shipped dir)."""
    manifest = {cat: {} for cat in SOUNDGROUP_CATEGORIES}
    refs = set()
    unresolved = []
    for group in sorted(groups):
        key = group.strip().lower()
        found_any = False
        for cat in SOUNDGROUP_CATEGORIES:
            subs = {}
            for variant in _group_dir_variants(group):
                prefix = f"sound/usable/{cat}/{variant}/"
                for k in idx:
                    if k.startswith(prefix) and k.endswith(".wav"):
                        sub = k[len(prefix):-len(".wav")]
                        rel = k[len("sound/"):]         # sound-relative (extract() prepends sound/)
                        subs[sub] = rel
                        refs.add(rel)
                if subs:
                    break                                # first matching dir variant wins
            if subs:
                manifest[cat][key] = subs
                found_any = True
        if not found_any:
            unresolved.append(group)
    manifest = {cat: g for cat, g in manifest.items() if g}   # drop empty categories
    return manifest, refs, unresolved


def collect_scheme_files(ents_paths):
    """Every distinct `scheme_file` an `ambient_soundscheme` entity points at (engine-relative,
    e.g. "sound/Schemes/SP_Tutorial_City.txt"). Case preserved; the index resolves case-insensitively."""
    schemes = set()
    for ent in _iter_ents(ents_paths):
        if (ent.get("classname") or "").lower() == "ambient_soundscheme":
            sf = ent.get("keys", {}).get("scheme_file")
            if isinstance(sf, str) and sf.lower().endswith(".txt"):
                schemes.add(sf.replace("\\", "/"))
    return schemes


def _collect_filenames(node, out):
    """Recursively gather every `Filename` value in a parsed scheme (Music/Combat/Alert/Ambient/
    RandomSound). kv.parse lowercases keys and collapses repeated blocks into lists."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "filename" and isinstance(v, str):
                if v.lower().endswith(AUDIO_EXTS):
                    out.add(v.replace("\\", "/"))
            else:
                _collect_filenames(v, out)
    elif isinstance(node, list):
        for item in node:
            _collect_filenames(item, out)


def extract_schemes(scheme_rels, idx):
    """PL5a: copy each scheme `.txt` verbatim into $ELYSIUM_EXPORT_ROOT/<scheme_rel> and parse it for the music/
    ambient/random `Filename` assets it references. Returns (written, cached, missing, asset_refs)
    where asset_refs are sound-relative paths (no `sound/` prefix) to feed into extract()."""
    written = cached = missing = 0
    asset_refs = set()
    misses = []
    for rel in sorted(scheme_rels):
        # scheme_file already includes the leading "sound/", so it maps straight under $ELYSIUM_EXPORT_ROOT/.
        data = install.read(idx, rel.lower())
        if data is None:
            missing += 1
            misses.append(rel)
            continue
        dest = os.path.join(OUT, rel.replace("/", os.sep))
        if os.path.exists(dest):
            cached += 1
        else:
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as f:
                f.write(data)
            written += 1
        # Parse for referenced assets regardless of whether the .txt was already on disk.
        try:
            parsed = kv.parse(data.decode("latin-1"))
            _collect_filenames(parsed, asset_refs)
        except Exception as e:                                   # noqa: BLE001 (parse is best-effort)
            print(f"  ! scheme parse failed {rel}: {e}", flush=True)

    if misses:
        print(f"  ! {len(misses)} scheme file(s) not in the install: "
              f"{', '.join(sorted(misses)[:6])}{' ...' if len(misses) > 6 else ''}", flush=True)
    return written, cached, missing, asset_refs


def extract(refs, idx=None):
    """Copy each referenced sound (WAV/MP3) verbatim into $ELYSIUM_EXPORT_ROOT/sound/<rel>. Returns (written,
    cached, missing). A ref already on disk is left untouched (cheap re-runs)."""
    if idx is None:
        idx = install.build_index(SOUND_DIRS)

    written = cached = missing = 0
    misses = []
    for rel in sorted(ref.lstrip("/") for ref in refs):
        dest = os.path.join(SOUND_OUT, rel)
        if os.path.exists(dest):
            cached += 1
            continue
        data = install.read(idx, "sound/" + rel)
        if data is None:
            missing += 1
            misses.append(rel)
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as f:
            f.write(data)
        written += 1

    if misses:
        print(f"  ! {len(misses)} referenced sound(s) not in the install: "
              f"{', '.join(misses[:6])}{' ...' if len(misses) > 6 else ''}", flush=True)
    return written, cached, missing


def _normalise(path):
    """The offline half of the runtime's one logical-path rule."""
    path = path.strip().replace("\\", "/").lstrip("/")
    if path.lower().startswith("sound/"):
        path = path[6:]
    return os.path.normpath(path).replace("\\", "/").lstrip("./").lower()


def _wav_metadata(path):
    """Read the RIFF fmt/data facts without asking Python's PCM-only wave module."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not RIFF/WAVE")
    pos = 12
    fmt = None
    data_bytes = 0
    fact_frames = 0
    while pos + 8 <= len(data):
        chunk, size = struct.unpack_from("<4sI", data, pos)
        payload = pos + 8
        if chunk == b"fmt " and size >= 16:
            fmt = struct.unpack_from("<HHIIHH", data, payload)
        elif chunk == b"data":
            data_bytes += min(size, max(0, len(data) - payload))
        elif chunk == b"fact" and size >= 4:
            fact_frames = struct.unpack_from("<I", data, payload)[0]
        pos = payload + size + (size & 1)
    if not fmt:
        raise ValueError("missing fmt chunk")
    tag, channels, rate, avg_bytes, block_align, bits = fmt
    frames = fact_frames or (data_bytes // block_align if block_align else 0)
    duration = (data_bytes / avg_bytes) if avg_bytes else (frames / rate if rate else 0)
    return {
        "codec": "wav",
        "format_tag": tag,
        "channels": channels,
        "sample_rate": rate,
        "bits_per_sample": bits,
        "frame_count": frames,
        "duration_seconds": round(duration, 6),
    }


def _mp3_metadata(path):
    """Find the first MPEG audio frame. Duration is byte-rate based and marked estimated."""
    with open(path, "rb") as f:
        data = f.read()
    pos = 0
    if data[:3] == b"ID3" and len(data) >= 10:
        size = ((data[6] & 0x7f) << 21) | ((data[7] & 0x7f) << 14) | \
               ((data[8] & 0x7f) << 7) | (data[9] & 0x7f)
        pos = 10 + size
    bitrates = {
        3: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
        2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160],
    }
    rates = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}
    while pos + 4 <= len(data):
        word = struct.unpack_from(">I", data, pos)[0]
        if word & 0xffe00000 == 0xffe00000:
            version_bits = (word >> 19) & 3
            layer_bits = (word >> 17) & 3
            bitrate_i = (word >> 12) & 0xf
            rate_i = (word >> 10) & 3
            if version_bits != 1 and layer_bits == 1 and bitrate_i not in (0, 15) and rate_i != 3:
                version = 3 if version_bits == 3 else (2 if version_bits == 2 else 0)
                table = 3 if version == 3 else 2
                kbps = bitrates[table][bitrate_i]
                rate = rates[version][rate_i]
                channels = 1 if ((word >> 6) & 3) == 3 else 2
                duration = max(0, len(data) - pos) * 8 / (kbps * 1000)
                return {
                    "codec": "mp3",
                    "channels": channels,
                    "sample_rate": rate,
                    "bits_per_sample": 16,
                    "frame_count": round(duration * rate),
                    "duration_seconds": round(duration, 6),
                    "duration_estimated": True,
                    "bit_rate_kbps": kbps,
                }
        pos += 1
    raise ValueError("no MPEG audio frame")


def _category(rel):
    low = rel.lower()
    if low.startswith(("character/dlg/", "dialogue/")):
        return "dialogue"
    if low.startswith(("music/", "licensed/", "radio/")) or low.endswith(".mp3"):
        return "music"
    if low.startswith(("ui/", "interface/")):
        return "ui"
    if low.startswith(("environmental/", "ambient/", "schemes/")):
        return "ambience"
    return "sfx"


def write_audio_contract(refs, idx, map_refs, scheme_rels, soundgroups):
    """Write catalog + typed reference sidecars. Every source path is patch-first because ``idx``
    has already collapsed base/VPK/patch candidates to the winning install entry."""
    os.makedirs(AUDIO_OUT, exist_ok=True)
    entries = []
    spellings = {}
    for authored in refs:
        spellings.setdefault(_normalise(authored), set()).add(authored)
    case_collisions = []
    for canonical, authored_set in sorted(spellings.items()):
        if len(authored_set) > 1:
            case_collisions.append(
                {"canonical": canonical, "spellings": sorted(authored_set, key=str.lower)})
        authored = sorted(authored_set, key=lambda value: (value.lower() != canonical, value))[0]
        path = os.path.join(SOUND_OUT, canonical.replace("/", os.sep))
        item = {
            "canonical_path": canonical,
            "actual_path": canonical,
            "authored_path": authored,
            "category": _category(canonical),
            "decode_policy": "stream" if canonical.endswith(".mp3") else "pcm_lru",
            "provenance": "patch_first_install_index",
            "references": sorted(name for name, values in map_refs.items()
                                 if canonical in {_normalise(v) for v in values}),
            "aliases": [f"sound/{canonical}"],
            "fallbacks": ([canonical[:-4] + ".wav"] if canonical.endswith(".mp3") else []),
            "disposition": "present" if os.path.exists(path) else "missing_content",
            "validation": {"present": os.path.exists(path), "error": ""},
        }
        try:
            item.update(_mp3_metadata(path) if canonical.endswith(".mp3") else _wav_metadata(path))
        except (OSError, ValueError) as exc:
            item["validation"]["error"] = str(exc)
        entries.append(item)
    catalog = {
        "contract": "elysium.audio.catalog",
        "version": AUDIO_CONTRACT_VERSION,
        "generation": {"entry_count": len(entries)},
        "case_collisions": case_collisions,
        "entries": entries,
    }
    with open(os.path.join(AUDIO_OUT, "catalog.json"), "w", encoding="utf-8") as f:
        json.dump(catalog, f, indent=1, sort_keys=True)

    maps_dir = os.path.join(AUDIO_OUT, "maps")
    os.makedirs(maps_dir, exist_ok=True)
    for map_name, values in sorted(map_refs.items()):
        with open(os.path.join(maps_dir, map_name + ".json"), "w", encoding="utf-8") as f:
            json.dump({"version": AUDIO_CONTRACT_VERSION, "map": map_name,
                       "references": sorted({_normalise(v) for v in values})},
                      f, indent=1, sort_keys=True)
    with open(os.path.join(AUDIO_OUT, "schemes.json"), "w", encoding="utf-8") as f:
        json.dump({"version": AUDIO_CONTRACT_VERSION, "schemes": sorted(scheme_rels)},
                  f, indent=1, sort_keys=True)
    with open(os.path.join(AUDIO_OUT, "entity_events.json"), "w", encoding="utf-8") as f:
        json.dump({"version": AUDIO_CONTRACT_VERSION, "soundgroups": soundgroups},
                  f, indent=1, sort_keys=True)
    print(f"[audio-contract] v{AUDIO_CONTRACT_VERSION}: {len(entries)} catalog entries, "
          f"{len(map_refs)} map reference sets, {len(case_collisions)} case collision(s) -> "
          f"{os.path.relpath(AUDIO_OUT, OUT)}/", flush=True)


def main(maps=None, radio=False, schemes=True):
    """Extract the sounds referenced by the named maps (default: every exported map). With
    radio=True, also mirror the loose radio_loop_*.mp3 set as MP3 decode test material. With
    schemes=True (default, PL5a), also copy the maps' ambient_soundscheme .txt files and the
    music/ambient assets they reference."""
    if maps:
        ents_paths = [os.path.join(OUT, m, m + ".ents") for m in maps]
        ents_paths = [p for p in ents_paths if os.path.exists(p)]
    else:
        ents_paths = sorted(glob.glob(os.path.join(OUT, "*", "*.ents")))

    if not ents_paths:
        print("[sound] no .ents sidecars found -- export maps first", flush=True)
        return

    idx = install.build_index(SOUND_DIRS)
    map_refs = {}
    for path in ents_paths:
        map_name = os.path.splitext(os.path.basename(path))[0]
        map_refs[map_name] = collect_audio_refs([path])
    refs = set().union(*map_refs.values()) if map_refs else set()

    # 6.4 — mover soundgroups: mirror the referenced usable/<cat>/<group>/*.wav sets and write the
    # manifest the runtime resolves door/button `soundgroup` tokens through.
    groups = collect_soundgroups(ents_paths)
    sg_manifest, sg_refs, sg_unresolved = extract_soundgroups(groups, idx)
    refs.update(sg_refs)
    manifest_path = os.path.join(SOUND_OUT, "usable", "soundgroups.json")
    os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(sg_manifest, f, indent=1, sort_keys=True)
    n_groups = sum(len(g) for g in sg_manifest.values())
    print(f"[soundgroup] {len(groups)} token(s) referenced -> {n_groups} group ent(y/ies) across "
          f"{len(sg_manifest)} categor(y/ies), {len(sg_refs)} WAVs; manifest -> "
          f"{os.path.relpath(manifest_path, OUT)}", flush=True)
    if sg_unresolved:
        print(f"  ! {len(sg_unresolved)} soundgroup(s) with no shipped usable/ dir: "
              f"{', '.join(sg_unresolved[:8])}", flush=True)

    if schemes:
        # PL5a — copy the scheme .txt files and fold their referenced music/ambient assets into refs.
        scheme_rels = collect_scheme_files(ents_paths)
        s_written, s_cached, s_missing, s_assets = extract_schemes(scheme_rels, idx)
        refs.update(s_assets)
        print(f"[scheme] {len(scheme_rels)} scheme(s): {s_written} copied, {s_cached} present, "
              f"{s_missing} missing; {len(s_assets)} referenced assets folded in", flush=True)
    else:
        scheme_rels = set()

    if radio:
        # Whatever radio loops this install actually carries (retail lists 5; installs vary).
        radio_refs = sorted(k[len("sound/"):] for k in idx
                            if k.startswith("sound/radio/") and k.endswith(".mp3"))
        refs.update(radio_refs)
        print(f"[sound] --radio: {len(radio_refs)} loose radio loop(s) found", flush=True)
    # Dynamic Python constructs filenames that cannot be reduced to a static reference list.
    # Mirror the whole supported patch-first tree so those paths resolve through the same catalog.
    refs.update(k[len("sound/"):] for k in idx
                if k.startswith("sound/") and k.endswith(AUDIO_EXTS))
    n_mp3 = sum(1 for r in refs if r.lower().endswith(".mp3"))
    print(f"[sound] {len(refs)} supported patch-first files for {len(ents_paths)} map(s) "
          f"({len(refs) - n_mp3} WAV, {n_mp3} MP3)", flush=True)
    written, cached, missing = extract(refs, idx)
    print(f"[sound] {written} copied, {cached} already present, {missing} missing "
          f"-> {os.path.relpath(SOUND_OUT, OUT)}/", flush=True)
    write_audio_contract(refs, idx, map_refs, scheme_rels, sg_manifest)


if __name__ == "__main__":
    cli = sys.argv[1:]
    want_radio = "--radio" in cli
    want_schemes = "--no-schemes" not in cli
    map_args = [a for a in cli if not a.startswith("--")]
    main(map_args or None, radio=want_radio, schemes=want_schemes)
