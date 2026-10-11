"""Ask original world PHY shapes about exported brush interiors; never writes retail files."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import csv
import json

RESEARCH_NOT_A_QUERY = "Compiles a read-only x86 world collision input instrument."

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", nargs="+", choices=("sp_tutorial_1", "sm_hub_1"))
    args = parser.parse_args()
    work = Path(os.environ["ELYSIUM_WORK_ROOT"]).resolve()
    game = Path(os.environ["ELYSIUM_VTMB_ROOT"]).resolve()
    scratch = work / "research" / "vphysics-world-input" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    scratch.mkdir(parents=True)
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    if not install:
        raise RuntimeError("Visual Studio x86 tools not found")
    source = Path(__file__).with_name("vphysics_world_input_probe.cpp")
    exe = scratch / "world_input.exe"
    batch = scratch / "compile.cmd"
    batch.write_text(f'@call "{Path(install) / "VC/Auxiliary/Build/vcvarsall.bat"}" x86\nif errorlevel 1 exit /b %errorlevel%\n@cl /nologo /EHsc /W4 "{source}" /Fo"{scratch / "probe.obj"}" /Fe"{exe}"\n', encoding="utf-8")
    subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=scratch, check=True)
    summary = {"probe": str(source), "probeSha256": hashlib.sha256(source.read_bytes()).hexdigest(), "dllSha256": hashlib.sha256((game / "Bin/vphysics.dll").read_bytes()).hexdigest(), "maps": []}
    for name in args.maps:
        bsp = game / "Unofficial_Patch/maps" / f"{name}.bsp"
        if not bsp.is_file(): bsp = game / "Vampire/maps" / f"{name}.bsp"
        raw = bsp.read_bytes()
        offset, size = struct.unpack_from("<2i", raw, 8 + 29*16)
        model, data_size, key_size, solids = struct.unpack_from("<4i", raw, offset)
        if model != 0 or min(data_size, key_size, solids) <= 0 or 16+data_size+key_size > size:
            raise ValueError(f"{name} invalid first lump29 model")
        blob = scratch / f"{name}.phy.bin"
        blob.write_bytes(raw[offset+16:offset+16+data_size+key_size])
        hulls = work / "exports_v2/_sidecars" / name / f"{name}.hulls"
        samples = bytearray()
        for line in hulls.read_text().splitlines():
            words = line.split()
            if not words or words[0].startswith("#"): continue
            contents = int(words[0], 16)
            vertices = [float(word) for word in words[1:]]
            if len(vertices) < 12 or len(vertices) % 3: raise ValueError("Malformed hull")
            count = len(vertices)//3
            point = [sum(vertices[axis::3])/count/2.54 for axis in range(3)]
            point[1] = -point[1]
            samples.extend(struct.pack("<I3f", contents, *point))
        sample_path = scratch / f"{name}.samples.bin"; sample_path.write_bytes(samples)
        output = scratch / f"{name}.csv"
        subprocess.run([str(exe), str(game / "Bin"), str(blob), str(sample_path), str(solids), str(output)], cwd=scratch, check=True)
        counts = {}
        with output.open(newline="") as stream:
            for row in csv.DictReader(stream):
                contents = int(row["contents"],16)
                kind = "common" if contents & 0x400b else "clipOnly" if contents & 0x30000 else "other"
                result = "insidePHY" if int(row["startsolid"]) or float(row["fraction"]) < 1 else "outsidePHY"
                key = f"solid{row['solid']}:{kind}:{result}"; counts[key] = counts.get(key, 0)+1
        summary["maps"].append({"name":name,"bsp":str(bsp),"bspSha256":hashlib.sha256(raw).hexdigest(),"hulls":str(hulls),"hullsSha256":hashlib.sha256(hulls.read_bytes()).hexdigest(),"solids":solids,"keydata":raw[offset+16+data_size:offset+16+data_size+key_size].decode("latin-1"),"csv":str(output),"counts":counts})
    result = scratch / "index.json"; result.write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2)); print(f"Report {result}")

if __name__ == "__main__": main()
