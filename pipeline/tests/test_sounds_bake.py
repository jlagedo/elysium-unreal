"""The offline half of `uv run elysium bake sounds` (`importers/sounds_bake.py`).

Every fixture is a synthetic `.wav`/`.mp3` published into a temporary `export_v2` root through
the real sound exporter, from hand-built bytes and a fake install index -- never the real VtMB
install and never the machine's own export tree. So the units these tests stage carry exactly
what a corpus unit carries: the decoded payload, the RIFF chunks and the source capsule.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import struct
import wave

from unittest import mock

import pytest

from elysium_pipeline.asset_paths import AssetPathError
from elysium_pipeline.exporters import sound_glb as exporter
from elysium_pipeline.formats.sound_glb import mpeg
from elysium_pipeline.formats.sound_glb.model import SOUND_EXTENSION
from elysium_pipeline.importers import sounds_bake as bake


# --- fixtures ------------------------------------------------------------------------------------


def _chunk(identifier: bytes, body: bytes) -> bytes:
    return identifier + struct.pack("<I", len(body)) + body + (b"\0" if len(body) % 2 else b"")


def _riff(*chunks: bytes) -> bytes:
    body = b"WAVE" + b"".join(chunks)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def _fmt(channels: int = 1, rate: int = 22050) -> bytes:
    align = channels * 2
    return struct.pack("<HHIIHH", 1, channels, rate, rate * align, align, 16)


def _samples(frames: int, channels: int = 1) -> bytes:
    return b"".join(struct.pack("<h", (index % 4096) - 2048)
                    for index in range(frames * channels))


def _smpl(start: int, end: int) -> bytes:
    body = struct.pack("<9I", 0, 0, 22675, 60, 0, 0, 0, 1, 0)
    body += struct.pack("<6I", 0, 0, start, end, 0, 0)
    return _chunk(b"smpl", body)


def _cue(offset: int) -> bytes:
    body = struct.pack("<I", 1) + struct.pack("<I", 1) + struct.pack("<I", offset)
    body += b"data" + struct.pack("<III", 0, 0, offset)
    return _chunk(b"cue ", body)


def _wav(frames: int, *, channels: int = 1, rate: int = 22050, extra: bytes = b"") -> bytes:
    return _riff(_chunk(b"fmt ", _fmt(channels, rate)),
                 _chunk(b"data", _samples(frames, channels)), extra)


def _mp3_frame() -> bytes:
    header = bytearray(4)
    header[0] = 0xFF
    header[1] = 0b11111011                       # MPEG-1 Layer III, no CRC
    header[2] = (9 << 4)                         # 128 kbit/s, no padding
    header[3] = (3 << 6)                         # mono
    length = 144 * mpeg.BITRATES_V1_L3[9] * 1000 // 44100
    frame = bytes(header) + bytes(range(1, 18))
    return frame + bytes(length - len(frame))


MP3 = _mp3_frame() * 3


def _publish(members: dict[str, bytes], root: Path) -> Path:
    """Export every audio member of `members` into `<root>/sounds/**.glb`."""

    index = {path: ("loose", "C:/game/Vampire/" + path) for path in members}
    read = lambda idx, name: members.get(name)                       # noqa: E731
    for path in members:
        if path.endswith((".wav", ".mp3")):
            exporter.export(index, path[len("sound/"):], Path(root) / "sounds", read_bytes=read)
    return Path(root)


def _stage(members: dict[str, bytes], tmp_path: Path, **kwargs) -> tuple[bake.StageResult, dict]:
    export_root = _publish(members, tmp_path / "export")
    result = bake.stage_sounds(export_root, tmp_path / "stage", **kwargs)
    manifest = json.loads((tmp_path / "stage" / "manifest.json").read_text(encoding="utf-8"))
    return result, manifest


def _entry(manifest: dict, asset_path: str) -> dict:
    return next(entry for entry in manifest["assets"] if entry["assetPath"] == asset_path)


# --- the PCM writer ---------------------------------------------------------------------------------


def test_a_staged_wav_states_the_payload_format_and_is_frames_times_channels_times_two():
    payload = _samples(1000, channels=2)
    data = bake.pcm_wav(payload, channels=2, sample_rate=44100)
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    assert struct.unpack("<I", data[4:8])[0] == len(data) - 8
    tag, channels, rate, byte_rate, align, bits = struct.unpack("<HHIIHH", data[20:36])
    assert (tag, channels, rate, byte_rate, align, bits) == (1, 2, 44100, 44100 * 4, 4, 16)
    assert data[36:40] == b"data"
    assert struct.unpack("<I", data[40:44])[0] == 1000 * 2 * 2 == len(data) - 44


def test_a_trim_slices_frames_not_bytes_so_a_stereo_trim_keeps_its_channels():
    payload = _samples(64, channels=2)
    data = bake.pcm_wav(payload, channels=2, sample_rate=22050, first=10, count=20)
    assert data[44:] == payload[10 * 4:30 * 4]
    assert wave.open(__import__("io").BytesIO(data)).getnframes() == 20


def test_a_trim_past_the_end_clamps_rather_than_writing_a_short_data_chunk():
    payload = _samples(16)
    data = bake.pcm_wav(payload, channels=1, sample_rate=22050, first=8, count=999)
    assert struct.unpack("<I", data[40:44])[0] == 8 * 2


# --- the loop rule ----------------------------------------------------------------------------------


def test_a_smpl_loop_at_sample_zero_keeps_the_whole_file_and_sets_looping(tmp_path):
    members = {"sound/area/rain_loop.wav": _wav(400, extra=_smpl(0, 299))}
    _, manifest = _stage(members, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/area/SW_rain_loop_wav")
    assert entry["looping"] is True
    # The `smpl` end (299) is carried for the record and never cut at: retail reads no loop end
    # (`FUN_2013a030` keeps only the start) and wraps at end of file, so the asset is all 400.
    assert entry["loop"] == {"decision": "whole", "source": "smpl", "startSample": 0,
                             "endSample": 399, "authoredEndSample": 299,
                             "firstSample": 0, "sampleCount": 400}
    assert entry["byteLength"] == 44 + 400 * 2


def test_a_smpl_loop_past_zero_splits_into_an_intro_and_a_body_that_runs_to_the_end(tmp_path):
    members = {"sound/env/flow.wav": _wav(400, extra=_smpl(120, 380))}
    _, manifest = _stage(members, tmp_path)
    intro = _entry(manifest, "/ElysiumBaked/Sounds/env/SW_flow_wav_intro")
    loop = _entry(manifest, "/ElysiumBaked/Sounds/env/SW_flow_wav_loop")
    assert (intro["looping"], loop["looping"]) == (False, True)
    assert intro["loop"]["sampleCount"] == 120 and intro["byteLength"] == 44 + 120 * 2
    # 280, not 261: the body runs to the last sample, not to the `smpl` end of 380.
    assert loop["loop"]["sampleCount"] == 280 and loop["byteLength"] == 44 + 280 * 2
    assert loop["loop"]["authoredEndSample"] == 380 and loop["loop"]["endSample"] == 399
    assert {entry["assetPath"] for entry in manifest["assets"]} == {
        intro["assetPath"], loop["assetPath"]}


def test_a_cue_point_is_a_loop_start_and_its_end_is_the_last_sample(tmp_path):
    members = {"sound/env/steam.wav": _wav(400, extra=_cue(150))}
    _, manifest = _stage(members, tmp_path)
    loop = _entry(manifest, "/ElysiumBaked/Sounds/env/SW_steam_wav_loop")
    assert loop["loop"] == {"decision": "loop", "source": "cue", "startSample": 150,
                            "endSample": 399, "authoredEndSample": None,
                            "firstSample": 150, "sampleCount": 250}
    assert _entry(manifest, "/ElysiumBaked/Sounds/env/SW_steam_wav_intro")["looping"] is False


def test_a_cue_point_at_sample_zero_is_a_whole_file_loop(tmp_path):
    """`CAudioSourceWave`'s ctor (0x20139d60) writes -1 into `this+0x28`, the `cue ` arm of the
    dispatcher (0x2013a030) overwrites it unconditionally, and `IsLooped` (0x2013a150) is
    `this+0x28 >= 0` -- so carrying the chunk at all is the loop. 161 corpus units are exactly
    this shape (`epic/wind.wav`, `area/downtown/downtown_main.wav`, ...)."""
    region = bake.loop_region(
        {"chunks": [{"id": "cue ", "body": {"points": [{"sampleOffset": 0}]}}]}, 400)
    assert (region.source, region.start, region.end) == ("cue", 0, 399)

    members = {"sound/epic/wind.wav": _wav(400, extra=_cue(0))}
    _, manifest = _stage(members, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/epic/SW_wind_wav")
    assert entry["looping"] is True
    assert entry["loop"]["decision"] == "whole" and entry["loop"]["sampleCount"] == 400
    assert entry["byteLength"] == 44 + 400 * 2


def test_a_unit_with_neither_chunk_is_the_only_one_that_does_not_loop(tmp_path):
    members = {"sound/interface/click.wav": _wav(400)}
    _, manifest = _stage(members, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/interface/SW_click_wav")
    assert entry["looping"] is False and entry["loop"]["decision"] == "none"
    assert bake.loop_region({"chunks": []}, 400) is None


def test_a_non_forward_smpl_loop_writes_nothing_because_the_arm_returns_early():
    """The `smpl` arm returns before the store when the dword at +0x28 of its 0x3c-byte copy --
    `loops[0].type` -- is nonzero, so a ping-pong or reverse loop leaves the field alone."""
    assert bake.loop_region({"chunks": [
        {"id": "smpl", "body": {"loops": [{"type": 1, "start": 100, "end": 300}]}}]}, 400) is None
    # ... and it does not clear what an earlier chunk wrote.
    region = bake.loop_region({"chunks": [
        {"id": "cue ", "body": {"points": [{"sampleOffset": 40}]}},
        {"id": "smpl", "body": {"loops": [{"type": 2, "start": 100, "end": 300}]}},
    ]}, 400)
    assert (region.source, region.start) == ("cue", 40)


def test_the_chunks_are_walked_in_file_order_and_the_last_writer_wins():
    """One dispatcher call per chunk, one field: a later chunk overwrites an earlier one."""
    region = bake.loop_region({"chunks": [
        {"id": "smpl", "body": {"loops": [{"start": 10, "end": 300}]}},
        {"id": "cue ", "body": {"points": [{"sampleOffset": 40}]}},
    ]}, 400)
    assert (region.source, region.start) == ("cue", 40)
    region = bake.loop_region({"chunks": [
        {"id": "cue ", "body": {"points": [{"sampleOffset": 40}]}},
        {"id": "smpl", "body": {"loops": [{"start": 10, "end": 300}]}},
    ]}, 400)
    assert (region.source, region.start) == ("smpl", 10)


def test_the_loop_end_is_the_last_sample_whatever_the_smpl_chunk_claims():
    for authored in (0, 100, 9999):
        region = bake.loop_region(
            {"chunks": [{"id": "smpl", "body": {"loops": [{"start": 0, "end": authored}]}}]}, 400)
        assert (region.source, region.start, region.end) == ("smpl", 0, 399)
        assert region.authored_end == authored


def test_a_degenerate_start_zero_end_zero_smpl_loop_is_simply_a_whole_file_loop(tmp_path):
    # `environmental/music/music_rock2 less muffled.wav` ships exactly this as a second `smpl`
    # chunk. It is not a special case: a start of 0 wraps the whole file, which is what retail
    # does with the one dword it keeps.
    members = {"sound/env/rock.wav": _wav(400, extra=_smpl(0, 0))}
    _, manifest = _stage(members, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/env/SW_rock_wav")
    assert entry["looping"] is True and entry["loop"]["sampleCount"] == 400


def test_a_smpl_start_past_the_decode_is_no_loop_rather_than_an_empty_body():
    assert bake.loop_region(
        {"chunks": [{"id": "smpl", "body": {"loops": [{"start": 9999, "end": 0}]}}]}, 400) is None




# --- what each container stages -------------------------------------------------------------------


def test_a_wav_unit_stages_a_readable_pcm_wav_of_the_units_own_payload(tmp_path):
    members = {"sound/interface/click.wav": _wav(128, channels=2, rate=44100)}
    _, manifest = _stage(members, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/interface/SW_click_wav")
    staged = tmp_path / "stage" / entry["file"]
    assert entry["format"] == "wav"
    with wave.open(str(staged)) as handle:
        assert (handle.getnchannels(), handle.getframerate(), handle.getsampwidth()) == (2, 44100, 2)
        assert handle.getnframes() == 128
        assert handle.readframes(128) == _samples(128, channels=2)


def test_an_mp3_unit_stages_the_original_member_bytes_because_nothing_decodes_them(tmp_path):
    members = {"sound/music/theme.mp3": MP3}
    _, manifest = _stage(members, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/music/SW_theme_mp3")
    assert entry["format"] == "mp3" and entry["looping"] is False
    assert (tmp_path / "stage" / entry["file"]).read_bytes() == MP3


def test_a_zero_byte_member_bakes_nothing_and_is_listed_as_empty(tmp_path):
    members = {"sound/area/clinic bg.wav": b"", "sound/music/theme.mp3": MP3}
    result, manifest = _stage(members, tmp_path)
    assert not result.failures
    assert manifest["empty"] == [{"unit": "area/clinic bg.wav",
                                 "sourcePath": "sound/area/clinic bg.wav",
                                 "reason": "empty-member"}]
    assert [entry["unit"] for entry in manifest["assets"]] == ["music/theme.mp3"]


# --- addressing ----------------------------------------------------------------------------------


def test_the_key_keeps_its_extension_so_a_wav_and_mp3_stem_pair_stay_distinct(tmp_path):
    members = {"sound/dlg/line1.wav": _wav(64), "sound/dlg/line1.mp3": MP3}
    _, manifest = _stage(members, tmp_path)
    assert sorted(entry["assetPath"] for entry in manifest["assets"]) == [
        "/ElysiumBaked/Sounds/dlg/SW_line1_mp3",
        "/ElysiumBaked/Sounds/dlg/SW_line1_wav",
    ]


def test_spaces_and_parentheses_fold_without_two_keys_aliasing_one_asset(tmp_path):
    """A space becomes a hyphen, so a spelling that differs only by space-vs-underscore stays a
    different asset. Parentheses have no legal equivalent and still fold to `_`."""
    members = {
        "sound/area/rain_moderate _loop.wav": _wav(64),
        "sound/area/rain_moderate_loop.wav": _wav(64),
        "sound/character/female/patron diner/float_1.wav": _wav(64),
        "sound/character/female/patron_diner/float_1.wav": _wav(64),
        "sound/music/all that could ever be (unused).mp3": MP3,
    }
    _, manifest = _stage(members, tmp_path)
    assert sorted(entry["assetPath"] for entry in manifest["assets"]) == [
        "/ElysiumBaked/Sounds/area/SW_rain_moderate-_loop_wav",
        "/ElysiumBaked/Sounds/area/SW_rain_moderate_loop_wav",
        "/ElysiumBaked/Sounds/character/female/patron-diner/SW_float_1_wav",
        "/ElysiumBaked/Sounds/character/female/patron_diner/SW_float_1_wav",
        "/ElysiumBaked/Sounds/music/SW_all-that-could-ever-be-_unused_mp3",
    ]


def test_the_asset_path_is_the_shared_baked_unit_resolver_not_a_second_spelling():
    from elysium_pipeline.asset_paths import baked_unit

    key = "character/monster/ming xiao/movement.wav"
    assert bake.asset_path_for(key) == baked_unit("vtmb:sound:" + key, "SW")
    assert bake.asset_path_for(key) == (
        "/ElysiumBaked/Sounds/character/monster/ming-xiao/SW_movement_wav")
    assert bake.asset_path_for(key, "loop").endswith("/SW_movement_wav_loop")


# --- the manifest the editor phase executes -----------------------------------------------------------


def test_the_manifest_carries_everything_the_editor_phase_requires(tmp_path):
    members = {"sound/interface/click.wav": _wav(64), "sound/music/theme.mp3": MP3}
    result, manifest = _stage(members, tmp_path)
    assert manifest["schemaVersion"] == bake.MANIFEST_SCHEMA
    assert manifest["packageRoot"] == "/ElysiumBaked/Sounds"
    assert manifest["pruneScope"] == "/ElysiumBaked/Sounds/"
    assert manifest["select"] is None and manifest["keep"] == []
    for entry in manifest["assets"]:
        assert set(entry) >= {"assetPath", "class", "unit", "role", "file", "format",
                              "byteLength", "looping", "loop", "settings", "recipe"}
        assert entry["class"] == "SoundWave"
        assert entry["settings"] == {"compression": "ProjectDefined",
                                     "looping": entry["looping"]}
        assert entry["recipe"]["version"] == bake.RECIPE_VERSION
        assert (tmp_path / "stage" / entry["file"]).is_file()
    assert result.assets == 2 and result.staged == 2 and not result.failures


def test_the_recipe_hashes_the_staged_bytes_so_a_moved_loop_start_re_imports(tmp_path):
    first = _stage({"sound/env/flow.wav": _wav(400, extra=_smpl(100, 399))}, tmp_path / "a")[1]
    second = _stage({"sound/env/flow.wav": _wav(400, extra=_smpl(200, 399))}, tmp_path / "b")[1]
    assert (_entry(first, "/ElysiumBaked/Sounds/env/SW_flow_wav_loop")["recipe"]["sha256"]
            != _entry(second, "/ElysiumBaked/Sounds/env/SW_flow_wav_loop")["recipe"]["sha256"])


def test_a_second_run_over_the_same_units_rewrites_nothing(tmp_path):
    members = {"sound/interface/click.wav": _wav(64), "sound/music/theme.mp3": MP3}
    export_root = _publish(members, tmp_path / "export")
    bake.stage_sounds(export_root, tmp_path / "stage")
    again = bake.stage_sounds(export_root, tmp_path / "stage")
    assert (again.staged, again.unchanged, again.pruned) == (0, 2, 0)


def test_a_full_run_prunes_a_staged_file_no_unit_produces(tmp_path):
    members = {"sound/interface/click.wav": _wav(64)}
    export_root = _publish(members, tmp_path / "export")
    bake.stage_sounds(export_root, tmp_path / "stage")
    stray = tmp_path / "stage" / "interface" / "SW_stale_wav.wav"
    stray.write_bytes(b"stale")
    assert bake.stage_sounds(export_root, tmp_path / "stage").pruned == 1
    assert not stray.exists()


# --- a scoped run ------------------------------------------------------------------------------------


def test_a_key_selection_stages_only_those_units_and_prunes_nothing(tmp_path):
    members = {"sound/interface/click.wav": _wav(64), "sound/music/theme.mp3": MP3}
    export_root = _publish(members, tmp_path / "export")
    bake.stage_sounds(export_root, tmp_path / "stage")
    result = bake.stage_sounds(export_root, tmp_path / "stage",
                               keys=["sound/MUSIC/Theme.MP3"])
    manifest = json.loads((tmp_path / "stage" / "manifest.json").read_text(encoding="utf-8"))
    assert result.assets == 1 and result.pruned == 0
    assert manifest["pruneScope"] is None
    assert manifest["select"] == ["music/theme.mp3"]
    # The unselected unit's staged file is still there: a subset run deletes nothing.
    assert (tmp_path / "stage" / "interface" / "SW_click_wav.wav").is_file()


def test_parse_keys_folds_normalizes_and_dedupes_and_answers_none_for_the_whole_corpus():
    assert bake.parse_keys(None) is None
    assert bake.parse_keys([]) is None
    assert bake.parse_keys(["sound/Interface/Click.WAV,music/theme.mp3", " music/theme.mp3 "]) == [
        "interface/click.wav", "music/theme.mp3"]


def test_a_selected_key_with_no_published_unit_is_one_failure_not_a_crash(tmp_path):
    members = {"sound/interface/click.wav": _wav(64)}
    export_root = _publish(members, tmp_path / "export")
    result = bake.stage_sounds(export_root, tmp_path / "stage", keys=["music/missing.mp3"])
    assert result.assets == 0
    assert [key for key, _ in result.failures] == ["music/missing.mp3"]


def test_an_export_root_with_no_units_refuses_rather_than_writing_an_empty_manifest(tmp_path):
    with pytest.raises(bake.SoundBakeError):
        bake.stage_sounds(tmp_path / "export", tmp_path / "stage")


# --- refusals the whole corpus found ------------------------------------------------------------


def test_the_fourteen_corpus_twins_each_address_their_own_asset(tmp_path):
    """The pairs that shared one path before the sound fold: a space is a hyphen now, so both
    spellings survive as the distinct install members they are."""
    members = {
        "sound/character/female/asian/target_giveup 1.wav": _wav(64),
        "sound/character/female/asian/target_giveup_1.wav": _wav(96),
        "sound/character/male/officer/float_1 .wav": _wav(64),
        "sound/character/male/officer/float_1.wav": _wav(96),
        "sound/whispers/moaning/child_moan alt3.wav": _wav(64),
        "sound/whispers/moaning/child_moan_alt3.wav": _wav(96),
    }
    result, manifest = _stage(members, tmp_path)
    assert not result.failures and result.assets == 6
    assert sorted(entry["assetPath"] for entry in manifest["assets"]) == [
        "/ElysiumBaked/Sounds/character/female/asian/SW_target_giveup-1_wav",
        "/ElysiumBaked/Sounds/character/female/asian/SW_target_giveup_1_wav",
        "/ElysiumBaked/Sounds/character/male/officer/SW_float_1-_wav",
        "/ElysiumBaked/Sounds/character/male/officer/SW_float_1_wav",
        "/ElysiumBaked/Sounds/whispers/moaning/SW_child_moan-alt3_wav",
        "/ElysiumBaked/Sounds/whispers/moaning/SW_child_moan_alt3_wav",
    ]


def test_a_stem_may_begin_with_an_underscore_and_keeps_it(tmp_path):
    """`character/monster/{ming xiao,spiderchick}/_period.wav`. The stem becomes the object name
    after the `SW_` prefix, so a leading underscore is addressable; a directory still reserves
    one."""
    members = {"sound/character/monster/ming xiao/_period.wav": _wav(64),
               "sound/character/monster/spiderchick/_period.wav": _wav(96)}
    result, manifest = _stage(members, tmp_path)
    assert not result.failures
    assert sorted(entry["assetPath"] for entry in manifest["assets"]) == [
        "/ElysiumBaked/Sounds/character/monster/ming-xiao/SW__period_wav",
        "/ElysiumBaked/Sounds/character/monster/spiderchick/SW__period_wav",
    ]


def test_a_directory_that_begins_with_an_underscore_is_still_reserved():
    with pytest.raises(AssetPathError):
        bake.asset_path_for("_private/click.wav")


def test_two_keys_that_still_fold_onto_one_asset_refuse_the_run_naming_every_pair(tmp_path):
    """The fold separates space from underscore but cannot separate everything: a `.` and a `_`
    in the same position still land on one name. No corpus key pair does this -- the whole-corpus
    assert below proves it -- and if one ever appears the manifest is refused rather than
    published with one unit silently overwriting the other."""
    members = {
        "sound/dlg/line.a.wav": _wav(64),
        "sound/dlg/line_a.wav": _wav(96),
        "sound/dlg/beat.1.wav": _wav(64),
        "sound/dlg/beat_1.wav": _wav(96),
    }
    export_root = _publish(members, tmp_path / "export")
    with pytest.raises(bake.SoundBakeError) as raised:
        bake.stage_sounds(export_root, tmp_path / "stage")
    message = str(raised.value)
    assert "2 baked sound path(s) are claimed by more than one unit" in message
    for key in ("line.a.wav", "line_a.wav", "beat.1.wav", "beat_1.wav"):
        assert key in message


def test_a_space_and_an_authored_hyphen_are_the_one_pairing_the_fold_cannot_split(tmp_path):
    """The hazard the whole-corpus assert guards: a space becomes a hyphen, so a key that authors
    the hyphen itself would land on the same name as its spaced twin."""
    members = {"sound/area/wind gust.wav": _wav(64), "sound/area/wind-gust.wav": _wav(96)}
    export_root = _publish(members, tmp_path / "export")
    with pytest.raises(bake.SoundBakeError, match="claimed by more than one unit"):
        bake.stage_sounds(export_root, tmp_path / "stage")


# --- the address contract the C++ twin has to reproduce -------------------------------------------


FIXTURE = Path(__file__).resolve().parent / "fixtures" / "sound_asset_paths.json"


def test_every_fixture_key_lands_where_the_fixture_says():
    """`fixtures/sound_asset_paths.json` is the sound key -> asset address contract, written for
    both sides: this asserts the Python half, and `FElysiumContentPaths::BakedUnit` is made to
    reproduce the same file. Every row is a key the corpus actually ships."""
    document = json.loads(FIXTURE.read_text(encoding="utf-8"))
    assert document["kind"] == "sound" and document["prefix"] == "SW"
    entries = document["entries"]
    assert len(entries) >= 40
    for entry in entries:
        package = bake.asset_path_for(entry["key"], entry["role"])
        assert package == entry["packagePath"], entry["key"]
        name = package.rsplit("/", 1)[-1]
        assert entry["objectPath"] == f"{package}.{name}", entry["key"]


def test_the_fixture_itself_names_no_asset_twice():
    entries = json.loads(FIXTURE.read_text(encoding="utf-8"))["entries"]
    owners: dict[str, str] = {}
    for entry in entries:
        prior = owners.setdefault(entry["objectPath"].casefold(), entry["key"])
        assert prior == entry["key"], f"{prior!r} and {entry['key']!r} share one address"


def test_the_fixture_covers_every_odd_character_the_corpus_ships():
    """A survey of the corpus found exactly five characters outside `[a-z0-9/._]`: space, `(`,
    `)`, `-` and `'`. Each must appear in a fixture key, or the contract is untested for it."""
    keys = " ".join(entry["key"] for entry
                    in json.loads(FIXTURE.read_text(encoding="utf-8"))["entries"])
    for character in (" ", "(", ")", "-", "'"):
        assert character in keys, character
    assert ".wav.wav" in keys and "/_period.wav" in keys


# --- the whole corpus, when this machine has one --------------------------------------------------


def _corpus_keys() -> list[str]:
    """Every published key, read out of the JSON chunk alone; skips when there is no export."""
    root = os.environ.get("ELYSIUM_EXPORT_V2_ROOT")
    if not root:
        work = os.environ.get("ELYSIUM_WORK_ROOT")
        root = str(Path(work) / "exports_v2") if work else ""
    units = Path(root) / "sounds" if root else None
    if units is None or not units.is_dir():
        pytest.skip("no export_v2 sound units on this machine")
    keys = []
    for unit in units.rglob("*.glb"):
        with open(unit, "rb") as handle:
            header = handle.read(20)
            length = struct.unpack("<I", header[12:16])[0]
            document = json.loads(handle.read(length).rstrip(b" "))
        keys.append(document["extensions"][SOUND_EXTENSION]["identity"]["key"])
    return keys


def test_every_corpus_key_addresses_its_own_asset():
    """The injectivity the sound fold exists for, asserted over all 10,892 keys rather than a
    sample: no two address one asset, and none is refused. This is also what guards the fold's one
    residual hazard -- a space becomes a hyphen, so a key authoring a hyphen where another has a
    space would collide. 17 corpus keys carry a hyphen and none pairs off that way."""
    owners: dict[str, str] = {}
    for key in _corpus_keys():
        package = bake.asset_path_for(key)          # raises if the key cannot be addressed
        prior = owners.setdefault(package.casefold(), key)
        assert prior == key, f"{prior!r} and {key!r} both address {package}"
    assert len(owners) > 10000


# --- what actually reaches the importer -----------------------------------------------------------


def _id3v2(padding: int = 200) -> bytes:
    """An ID3v2 header with nothing in it but zero padding -- the shape of the 208 bytes in front
    of `character/dlg/generic/doll3/line341_col_f.mp3`."""
    size = padding
    synchsafe = bytes([(size >> 21) & 0x7F, (size >> 14) & 0x7F, (size >> 7) & 0x7F, size & 0x7F])
    return b"ID3" + b"\x03\x00" + b"\0" + synchsafe + bytes(padding)


def _id3v1() -> bytes:
    """The 128-byte trailer 151 corpus members carry after their last frame."""
    return b"TAG" + b"line".ljust(30, b"\0") + bytes(128 - 33)


def test_an_mp3_stages_the_frame_stream_not_the_member_so_leading_junk_never_reaches_unreal(
        tmp_path):
    """`USoundFactory` hands the bytes to libsndfile, which answers "Format not recognised" when
    the file does not begin on a frame header. `doll3/line341_col_f.mp3` carries 208 zero bytes of
    `leading-zero-padding` and failed the first full-corpus run for exactly this."""
    member = _id3v2() + MP3 + _id3v1()
    _, manifest = _stage({"sound/dlg/padded.mp3": member}, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/dlg/SW_padded_mp3")
    staged = (tmp_path / "stage" / entry["file"]).read_bytes()
    assert entry["format"] == "mp3" and entry["disposition"] is None
    assert staged == MP3, "the staged file is the frame stream, junk at neither end"
    assert staged[0] == 0xFF and (staged[1] & 0xE0) == 0xE0, "it begins on a frame sync word"


def test_an_mp3_with_no_junk_stages_the_member_byte_for_byte_and_keeps_its_stamp(tmp_path):
    """The payload IS the member when there is nothing to trim, so 5,191 of the 5,342 mp3 units
    stage the same bytes as before this rule and their recipe stamps still match."""
    first = _stage({"sound/music/theme.mp3": MP3}, tmp_path / "a")[1]
    entry = _entry(first, "/ElysiumBaked/Sounds/music/SW_theme_mp3")
    assert (tmp_path / "a" / "stage" / entry["file"]).read_bytes() == MP3
    # The same member behind an ID3 wrapper stages the same bytes, so it stamps the same recipe.
    second = _stage({"sound/music/theme.mp3": _id3v2() + MP3 + _id3v1()}, tmp_path / "b")[1]
    other = _entry(second, "/ElysiumBaked/Sounds/music/SW_theme_mp3")
    assert other["recipe"]["sha256"] == entry["recipe"]["sha256"]


def test_a_payload_that_does_not_begin_on_a_sync_word_is_one_failure(tmp_path):
    """A unit whose frame table and payload disagree is a decode defect, not something to hand to
    the importer and hope."""
    unit = bake.read_unit("dlg/a.mp3", (Path(_publish({"sound/dlg/a.mp3": MP3}, tmp_path)
                                             / "sounds" / "dlg" / "a.mp3.glb").read_bytes()))
    broken = dict(unit.extension)
    with mock.patch.object(bake, "payload_bytes", return_value=b"\x00\x00\x00\x00"):
        with pytest.raises(bake.SoundBakeError, match="frame sync word"):
            bake.plan(bake.Unit(unit.key, unit.path, unit.sha256, unit.document, unit.binary,
                                broken))


# --- the placeholder --------------------------------------------------------------------------------


def test_a_single_frame_mp3_stages_a_silent_wave_of_retail_s_own_duration(tmp_path):
    """`character/dlg/hollywood/ash/line571_col_e.mp3` is one 157-byte frame, 1152 samples at
    44100 Hz. libsndfile refuses it outright, so the bake substitutes silence of exactly that
    length: the line service schedules the dialogue turn on the wave's duration, and a turn that
    keeps retail's timing and plays nothing is a bounded defect where a missing asset would also
    shorten the scene."""
    _, manifest = _stage({"sound/dlg/short.mp3": _mp3_frame()}, tmp_path)
    entry = _entry(manifest, "/ElysiumBaked/Sounds/dlg/SW_short_mp3")

    # The asset keeps its `_mp3` name: the runtime addresses by key and must find it there.
    assert entry["assetPath"].endswith("/SW_short_mp3")
    assert entry["disposition"] == bake.PLACEHOLDER_SILENCE
    assert "1 mpeg frame(s)" in entry["reason"]
    assert entry["format"] == "wav" and entry["looping"] is False

    staged = tmp_path / "stage" / entry["file"]
    with wave.open(str(staged)) as handle:
        assert handle.getnframes() == 1152
        assert (handle.getframerate(), handle.getnchannels(), handle.getsampwidth()) == (44100, 1, 2)
        assert handle.readframes(1152) == bytes(1152 * 2), "silent, not noise"

    assert manifest["placeholders"] == [{
        "unit": "dlg/short.mp3", "assetPath": "/ElysiumBaked/Sounds/dlg/SW_short_mp3",
        "disposition": bake.PLACEHOLDER_SILENCE,
        "reason": entry["reason"], "sampleCount": 1152}]


def test_a_placeholder_is_reported_not_silently_substituted(tmp_path):
    result, _ = _stage({"sound/dlg/short.mp3": _mp3_frame(), "sound/music/theme.mp3": MP3},
                       tmp_path)
    assert not result.failures
    assert [row["unit"] for row in result.placeholders] == ["dlg/short.mp3"]
    assert "1 placeholder" in result.summary()


def test_a_unit_that_declares_no_duration_cannot_be_stood_in_for(tmp_path):
    """Silence of an unknown length would be a guess at the schedule, so it refuses instead."""
    export_root = _publish({"sound/dlg/short.mp3": _mp3_frame()}, tmp_path / "export")
    unit_file = export_root / "sounds" / "dlg" / "short.mp3.glb"
    unit = bake.read_unit("dlg/short.mp3", unit_file.read_bytes(), unit_file)
    extension = dict(unit.extension)
    extension["codec"] = dict(extension["codec"], durationSamples=0)
    with pytest.raises(bake.SoundBakeError, match="cannot stand in for this unit"):
        bake.plan(bake.Unit(unit.key, unit.path, unit.sha256, unit.document, unit.binary,
                            extension))
