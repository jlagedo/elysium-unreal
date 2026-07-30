"""Archive every authored skeletal channel in the seven courtroom cinematics.

The live StudioRender trace is necessarily visibility-gated.  This companion
archive preserves every source frame, including actors that retail culls or
never places in a camera shot, so later analysis can join complete authored
timelines to the rendered matrix oracle.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np

import install
import mdl_skel


ROOT = Path(__file__).resolve().parent
DEFAULT_OUTPUT = ROOT / "out" / "_live_pose" / "courtroom_authored"
MAP_ENTITIES = ROOT / "out" / "sp_theatre" / "sp_theatre.ents"
SCENE_ROOT = (
    ROOT / "out" / "scenes" / "cinematic" / "santa_monica" / "courtroom"
)


def actor_blocks(text: str) -> list[tuple[str, str]]:
    lines = text.splitlines()
    blocks: list[tuple[str, str]] = []
    index = 0
    while index < len(lines):
        match = re.match(r'\s*actor\s+"([^"]+)"', lines[index])
        if not match:
            index += 1
            continue
        actor = match.group(1)
        begin = index
        depth = 0
        opened = False
        while index < len(lines):
            depth += lines[index].count("{")
            if "{" in lines[index]:
                opened = True
            depth -= lines[index].count("}")
            index += 1
            if opened and depth == 0:
                break
        blocks.append((actor, "\n".join(lines[begin:index])))
    return blocks


def scene_actors(vcd: Path, models_by_name: dict[str, str]) -> list[dict[str, object]]:
    actors = []
    for actor, block in actor_blocks(vcd.read_text(encoding="utf-8")):
        rename = re.search(
            r'bonerename\s+"([^"]+)"\s+"([^"]+)"', block, re.IGNORECASE
        )
        sequence = re.search(
            r'event\s+sequence.*?param\s+"([^"]+)"',
            block,
            re.IGNORECASE | re.DOTALL,
        )
        actors.append(
            {
                "actor": actor,
                "target_model": models_by_name.get(actor),
                "source_bone_root": rename.group(1) if rename else None,
                "target_bone_root": rename.group(2) if rename else None,
                "sequence": sequence.group(1) if sequence else None,
            }
        )
    return actors


def archive_model(index, model_key: str, output: Path) -> dict[str, object]:
    data = install.read(index, model_key)
    if data is None:
        raise FileNotFoundError(model_key)
    bones = mdl_skel.read_bones(data)
    sequences = [
        sequence
        for sequence in mdl_skel.local_sequences(data)
        if sequence.label.lower() == "entire_scene"
    ]
    if len(sequences) != 1:
        raise ValueError(f"{model_key}: expected one entire_scene sequence")
    sequence = sequences[0]
    print(
        f"Decoding {model_key}: {sequence.frames} frames x {len(bones)} bones",
        flush=True,
    )
    decoded = mdl_skel.read_anim(data, bones, sequence.base, sequence.frames)
    positions = np.empty((sequence.frames, len(bones), 3), dtype="<f4")
    quaternions = np.empty((sequence.frames, len(bones), 4), dtype="<f4")
    for frame_index, frame in enumerate(decoded):
        for bone_index, (position, quaternion) in enumerate(frame):
            positions[frame_index, bone_index] = position
            quaternions[frame_index, bone_index] = quaternion
    del decoded

    checksum = struct.unpack_from("<I", data, 8)[0]
    archive_name = Path(model_key).stem.lower() + ".npz"
    archive_path = output / archive_name
    np.savez(
        archive_path,
        local_position=positions,
        local_quaternion=quaternions,
        bone_name=np.asarray([bone.name for bone in bones]),
        parent=np.asarray([bone.parent for bone in bones], dtype="<i4"),
        flags=np.asarray([bone.flags for bone in bones], dtype="<u4"),
        bind_position=np.asarray([bone.pos for bone in bones], dtype="<f4"),
        bind_quaternion=np.asarray([bone.quat for bone in bones], dtype="<f4"),
        pose_to_bone=np.asarray(
            [bone.pose_to_bone for bone in bones], dtype="<f4"
        ).reshape(len(bones), 3, 4),
        fps=np.asarray(sequence.fps, dtype="<f4"),
    )
    return {
        "model": model_key,
        "model_checksum": f"0x{checksum:08x}",
        "source_mdl_sha256": hashlib.sha256(data).hexdigest(),
        "sequence": sequence.label,
        "frames": sequence.frames,
        "fps": sequence.fps,
        "bone_count": len(bones),
        "archive": archive_path.name,
        "archive_bytes": archive_path.stat().st_size,
        "archive_sha256": hashlib.sha256(archive_path.read_bytes()).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    map_data = json.loads(MAP_ENTITIES.read_text(encoding="utf-8"))
    models_by_name = {
        entity["targetname"]: entity.get("keys", {}).get("model")
        for entity in map_data["entities"]
        if entity.get("targetname")
    }
    index = install.build_index(verbose=False)
    scenes = []
    for scene_index in range(1, 8):
        vcd = SCENE_ROOT / f"courtroom_bip{scene_index}_scene.vcd"
        model_key = (
            "models/cinematic/Santa_Monica/Courtroom/"
            f"Courtroom_bip{scene_index}.mdl"
        )
        archived = archive_model(index, model_key, output)
        scenes.append(
            {
                "scene": f"courtroom_bip{scene_index}",
                "vcd": str(vcd.resolve()),
                "vcd_sha256": hashlib.sha256(vcd.read_bytes()).hexdigest(),
                "actors": scene_actors(vcd, models_by_name),
                **archived,
            }
        )

    manifest = {
        "version": 1,
        "map": "sp_theatre",
        "purpose": (
            "Complete authored companion to visibility-gated retail "
            "StudioRender traces"
        ),
        "scene_count": len(scenes),
        "total_frames_per_scene": 4701,
        "scenes": scenes,
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(manifest_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
