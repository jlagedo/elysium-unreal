"""Retiring expressions cannot change or remove the retained scene/lip mirrors."""
from elysium_pipeline.exporters import UE_extract_scenes as lane


def test_scenes_and_lip_keep_patch_precedence_and_bytes_without_expression_writes(tmp_path, monkeypatch):
    retail, patch, out = (tmp_path / name for name in ("retail", "patch", "out"))
    sources = {
        retail / "sound/scene.vcd": b"retail scene\r\n",
        patch / "sound/scene.vcd": b"patch scene\r\n\x80",
        retail / "sound/line.lip": b"phonemes\r\n\x00",
        patch / "expressions/phonemes.txt": b"retired loose expression",
    }
    for path, value in sources.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)
    old = out / "expressions/retained.txt"
    old.parent.mkdir(parents=True)
    old.write_bytes(b"deployed evidence")
    monkeypatch.setattr(lane.install, "GAME", str(retail))
    monkeypatch.setattr(lane.install, "PATCH", str(patch))
    monkeypatch.setattr(lane, "OUT", str(out))
    lane.main(force=True, index={"expressions/phonemes.txt": ("vpk", object())})
    assert (out / "scenes/scene.vcd").read_bytes() == sources[patch / "sound/scene.vcd"]
    assert (out / "lip/line.lip").read_bytes() == sources[retail / "sound/line.lip"]
    assert old.read_bytes() == b"deployed evidence"
    assert not (out / "expressions/phonemes.txt").exists()
