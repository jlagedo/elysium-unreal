"""Bounded independent public retail mixed-material geometry/event/response proof."""
from __future__ import annotations

import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

RESEARCH_NOT_A_QUERY = "Compiles and runs one bounded independent public retail material observer."
IMMUTABLE = {
    "vphysics_contacts.py": "a6772d8fbda3c6bfb4f602464c9427b30062b33e0469ccf22e865bbd4bcca18e",
    "vphysics_contacts_probe.cpp": "fe46597e1469fcb9085e997b6fb24f65714bcf86c16b3ee0037748f125d68dd2",
}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    tooling = Path(__file__).resolve().parent
    for name, expected in IMMUTABLE.items():
        if sha(tooling / name) != expected:
            raise RuntimeError(f"Protected original contact probe changed: {name}")
    work = Path(os.environ["ELYSIUM_WORK_ROOT"]).resolve()
    game_bin = Path(os.environ["ELYSIUM_VTMB_ROOT"]).resolve() / "Bin"
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    scratch = work / "research/vphysics-material-selection" / stamp
    retained = work / "codex/l0-retail-material-selection-20261011/runs" / stamp
    scratch.mkdir(parents=True)
    retained.mkdir(parents=True)
    installed_evidence = work / "codex/l0-retail-material-selection-20261011/installed-public-api-evidence.json"
    retained_api = json.loads(installed_evidence.read_text())
    expected_dll = next(item["sha256"] for item in retained_api["sources"] if item["path"].endswith("vphysics.dll"))
    if sha(game_bin / "vphysics.dll") != expected_dll:
        raise RuntimeError("Installed retail DLL changed since precode ABI review")
    source = tooling / "vphysics_material_selection_probe.cpp"
    for path in (Path(__file__).resolve(), source):
        shutil.copy2(path, retained / path.name)
    fixture = {
        "provenance": "Independent project-authored public convex cube and public cached bbox; no shipped geometry claim.",
        "units": "Geometry manifest cm; retail Source inches=cm/2.54, Yreflection; zero angles.",
        "cubeCornersCm": [[x, y, z] for x in (-100, 100) for y in (-100, 100) for z in (-5, 5)],
        "topTrianglesCm": [
            {"vertices": [[-100, -100, 5], [100, -100, 5], [100, 100, 5]], "selector": 7},
            {"vertices": [[-100, -100, 5], [-100, 100, 5], [100, 100, 5]], "selector": 11},
        ],
        "otherTriangleSelector": 0,
        "worldTable": list(range(128)),
        "floorCurrentRow": 0, "bodyCurrentRow": 12,
        "authoredRows": 13, "totalStoredRows": 14,
        "reservedShadow": {"ordinaryRow": 13, "nameIndex": 61440, "friction": 0.8, "elasticity": 0},
        "bboxHalfExtentCm": [10, 10, 10], "positionCm": [0, 0, 40], "velocityCmPerSec": [0, 0, -600],
        "contents": 2, "callbackFlags": 33, "mass": 1, "damping": 0,
        "gravity": [0, 0, 0], "dragEnabled": False,
        "hostDtExpression": "FLOAT1/60", "admission": "min(hostDt,.1)",
        "configuredRetailStep": ".015f", "maxFrames": 12,
        "geometryFloatTolerance": "8*FLT_EPSILON*max(1,abs(input Source component)); post-setter byte-identical",
        "products": "Computed from separately parsed public-read rows; no native coefficient readback.",
        "callbackNormal": "Source normal points toward event object1; copied Source inches/Zup, not Chaos normal orientation.",
        "stop": "Unexpected geometry/split, missing matched event/contact44/solved branch, observer interference.",
    }
    (scratch / "fixture.json").write_text(json.dumps(fixture, indent=2) + "\n")
    names = ["floor", *(f"filler{i}" for i in range(1, 7)), "seven",
             *(f"filler{i}" for i in range(8, 11)), "eleven", "body"]
    coefficients = {"floor": ("0.7", "0.3"), "seven": ("0.2", "0.25"), "eleven": ("0.9", "2"), "body": ("0.6", "1")}
    recipe = scratch / "independent-surfaceproperties.txt"
    recipe.write_text("\n".join(
        f'"{name}"\n{{\n "friction" "{coefficients.get(name, ("0.1", "0"))[0]}"\n'
        f' "elasticity" "{coefficients.get(name, ("0.1", "0"))[1]}"\n}}'
        for name in names) + "\n")
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    install = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
    if not install:
        raise RuntimeError("Visual Studio x86 tools were not found")
    executable = scratch / "material_selection.exe"
    batch = scratch / "compile.cmd"
    batch.write_text(f'@call "{Path(install) / "VC/Auxiliary/Build/vcvarsall.bat"}" x86\n'
        f'if errorlevel 1 exit /b %errorlevel%\n@cl /nologo /EHsc /W4 "{source}" '
        f'/Fo"{scratch / "probe.obj"}" /Fe"{executable}"\n', encoding="utf-8")
    summary = {"scope": "Bounded diagnostic only; no homogeneous controls or production binding.",
               "originalPackagesNewlyClosed": 0, "originalComplete": 23, "originalRemaining": 65,
               "scratch": str(scratch), "retained": str(retained), "runs": [],
               "provenance": [{"path": str(p), "sha256": sha(p)} for p in
                              (Path(__file__).resolve(), source, recipe, scratch / "fixture.json", game_bin / "vphysics.dll")],
               "protected": IMMUTABLE, "exitCode": None}
    def retain() -> None:
        # Every prunable output is copied before return, including failed stimulus data.
        receipts = []
        for original in scratch.iterdir():
            if original.is_file():
                copied = retained / original.name
                shutil.copy2(original, copied)
                receipts.append({"original": str(original), "copied": str(copied),
                                 "originalSha256": sha(original), "copiedSha256": sha(copied)})
        (retained / "retained-receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")

    try:
        compile_result = subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=scratch,
                                        capture_output=True, text=True, timeout=60)
        (scratch / "compile.stdout.txt").write_text(compile_result.stdout)
        (scratch / "compile.stderr.txt").write_text(compile_result.stderr)
        summary["compileExitCode"] = compile_result.returncode
        summary["exitCode"] = compile_result.returncode
        if not compile_result.returncode:
            for mode in ("off", "on"):
                prefix = scratch / mode
                command = [str(executable), str(game_bin), str(recipe), str(prefix), mode]
                result = subprocess.run(command, cwd=scratch, capture_output=True, text=True, timeout=30)
                (scratch / f"{mode}.stdout.txt").write_text(result.stdout)
                (scratch / f"{mode}.stderr.txt").write_text(result.stderr)
                summary["runs"].append({"observer": mode, "command": command, "exitCode": result.returncode})
                summary["exitCode"] = result.returncode
                retain()
                if result.returncode:
                    summary["classification"] = "INVALID stimulus/ABI branch; retained unchanged source and outputs."
                    break
            if not summary["exitCode"]:
                summary["observerPreservedAllTrajectoryColumns"] = (scratch / "off.frames.csv").read_bytes() == (scratch / "on.frames.csv").read_bytes()
                summary["geometryIdenticalBetweenProcesses"] = (scratch / "off.geometry.csv").read_bytes() == (scratch / "on.geometry.csv").read_bytes()
                with (scratch / "on.events.csv").open(newline="") as handle:
                    event_rows = list(csv.DictReader(handle))
                summary["eventCopies"] = len(event_rows)
                summary["directSelectedEventRowPairs"] = sorted({(int(row["row0"]), int(row["row1"])) for row in event_rows})
                summary["computedProductPairs"] = sorted({(row["computed_mu"], row["computed_elasticity"]) for row in event_rows})
                summary["productObservation"] = "COMPUTED only from independently verified public surface rows and retained retail product formula."
                # Root-reviewed installed-fixture regression from the retained first proof.
                # These literal event/clock values do not prescribe Chaos trajectories or topology.
                observed_events = [tuple(row[key] for key in
                    ("frame", "phase", "id0", "id1", "row0", "row1", "is_collision", "is_shadow"))
                    for row in event_rows]
                expected_events = [("3", phase, "1", "2", "12", floor, "1", "0")
                                   for floor in ("7", "11") for phase in ("pre", "post")]
                with (scratch / "on.frames.csv").open(newline="") as handle:
                    frames = list(csv.DictReader(handle))
                contacts = [row for row in frames if row["contact"] == "1"]
                checks = {
                    "twoOrderedMatchedCollisionEvents": observed_events == expected_events,
                    "literalComputedProducts": [(row["computed_mu"], row["computed_elasticity"]) for row in event_rows] ==
                        [("0.12000000476837158", "0.25")] * 2 + [("0.54000002145767212", "2")] * 2,
                    "literalValidPostSpeeds": [row["post_speed"] for row in event_rows if row["phase"] == "post"] ==
                        ["236.22047424316406", "445.83184814453125"],
                    "realSampledContact44": len(contacts) == 1 and contacts[0]["frame"] == "3" and contacts[0]["partner"] == "2",
                    "literalFirstContactClock": bool(contacts) and contacts[0]["before"] == "0.033333335071802139" and
                        contacts[0]["after"] == "0.050000004470348358",
                    "configuredClockAndCurrentRows": len(frames) == 12 and all(row["host_dt"] == "0.01666666753590107" and
                        row["admitted_dt"] == row["host_dt"] and row["timestep"] == "0.014999999664723873" and
                        row["floor_row"] == "0" and row["body_row"] == "12" for row in frames),
                    "observerNoninterference": summary["observerPreservedAllTrajectoryColumns"],
                    "independentGeometryPreserved": summary["geometryIdenticalBetweenProcesses"],
                }
                summary["installedFixtureRegression"] = checks
                summary["preSpeedValidity"] = "Pre collisionSpeed is invalid and serialized0; pre RelativeSpeed is borrowed state, not the current impact. Only POST speeds are interpreted."
                summary["classification"] = "ANSWERED: two selected material event products in one admitted frame; no single frame-wide representative/native-point0 equivalence claim."
                summary["nextConsumingClosure"] = "Supported production authored binding preserving observable material/response/event behavior; compatible shipped model/solid/contents association and representative policy remain unresolved."
                if not summary["observerPreservedAllTrajectoryColumns"] or not summary["geometryIdenticalBetweenProcesses"]:
                    summary["exitCode"] = 24
                    summary["classification"] = "INVALID observer noninterference/geometry comparison."
                elif not all(checks.values()):
                    summary["exitCode"] = 25
                    summary["classification"] = "Installed-fixture regression diverged; preserve literal expectations and report."
    except Exception as error:
        summary["exception"] = f"{type(error).__name__}: {error}"
        summary["exitCode"] = -1
    finally:
        (scratch / "index.json").write_text(json.dumps(summary, indent=2) + "\n")
        retain()
    print(json.dumps(summary, indent=2))
    print(f"Retained report {retained / 'index.json'}")
    if summary["exitCode"]:
        raise RuntimeError(f"Bounded material diagnostic stopped; retained report {retained / 'index.json'}")


if __name__ == "__main__":
    main()
