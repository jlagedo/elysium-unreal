"""Read-only SurfaceProps001 whole payload and real constructor-material capture."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import struct
import os
from pathlib import Path
import subprocess

RESEARCH_NOT_A_QUERY = "Compiles an x86 read-only retail material/constructor capture."


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    work = Path(os.environ["ELYSIUM_WORK_ROOT"]).resolve()
    game_bin = Path(os.environ["ELYSIUM_VTMB_ROOT"]).resolve() / "Bin"
    scratch = work / "research" / "vphysics-surfaces" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    scratch.mkdir(parents=True)
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    if not install:
        raise RuntimeError("Visual Studio x86 C++ tools were not found")
    environment = Path(install) / "VC/Auxiliary/Build/vcvarsall.bat"
    source = Path(__file__).with_name("vphysics_surfaces_probe.cpp")
    executable = scratch / "surfaces_probe.exe"
    batch = scratch / "compile.cmd"
    batch.write_text(f'@call "{environment}" x86\nif errorlevel 1 exit /b %errorlevel%\n'
        f'@cl /nologo /EHsc /W4 "{source}" /Fo"{scratch / "probe.obj"}" /Fe"{executable}"\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", str(batch)], check=True, cwd=scratch)
    output = scratch / "surfaces.tsv"
    subprocess.run([str(executable), str(game_bin), str(args.source.resolve()), str(output)], check=True, cwd=scratch)
    from elysium_pipeline.formats.physics_surfaces import FLOAT_KEYS, SOUND_KEYS
    offsets = (0,4,8,12,16,20,24,28,32,36,40,104,108)
    rows, sounds = [], {}
    for line in output.read_text(encoding="latin-1").splitlines():
        fields = line.split("\t")
        if fields[0] == "sounds":
            sounds[fields[1]] = dict(zip(SOUND_KEYS, fields[2:], strict=True))
        elif len(fields) == 5 and len(fields[1]) == 232:
            payload = bytes.fromhex(fields[1])
            rows.append({"name": fields[0], "physics": {key: struct.unpack_from("<f", payload, offset)[0]
                for key, offset in zip(FLOAT_KEYS, offsets, strict=True)},
                "gamematerial": struct.unpack_from("<H", payload,112)[0], "climbable": payload[114]})
    for row in rows:
        row["sounds"] = sounds[row["name"]]
    semantics = scratch / "semantics.json"
    semantics.write_text(json.dumps({"schemaVersion":"1.0.0", "rows":rows}, ensure_ascii=True, indent=2)+"\n")
    print(f"semantic capture: {semantics}")
    for path in (game_bin / "vphysics.dll", source, args.source, output, semantics):
        print(f"{path} SHA256 {hashlib.sha256(path.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
