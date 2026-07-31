"""Generate or verify the tracked FlatBuffers smoke bindings."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
SCHEMA = ROOT / "flatbuffers_smoke.fbs"
CPP_OUTPUT = ROOT.parent / "native" / "flatbuffers_smoke_generated.h"
PYTHON_OUTPUT = ROOT.parent / "generated_flatbuffers_smoke"


def _generate(flatc: Path, root: Path) -> tuple[Path, Path]:
    cpp = root / "cpp"
    python = root / "python"
    cpp.mkdir(parents=True)
    python.mkdir(parents=True)
    subprocess.run(
        [str(flatc), "--cpp", "-o", str(cpp), str(SCHEMA)],
        check=True,
    )
    subprocess.run(
        [str(flatc), "--python", "-o", str(python), str(SCHEMA)],
        check=True,
    )
    return cpp / "flatbuffers_smoke_generated.h", python


def _tree(root: Path) -> dict[str, bytes]:
    if not root.is_dir():
        return {}
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in root.rglob("*")
        if path.is_file()
        and "__pycache__" not in path.parts
        and path.suffix != ".pyc"
    }


def _check(generated_cpp: Path, generated_python: Path) -> None:
    failures: list[str] = []
    if not CPP_OUTPUT.is_file():
        failures.append(f"missing {CPP_OUTPUT}")
    elif CPP_OUTPUT.read_bytes() != generated_cpp.read_bytes():
        failures.append(f"stale {CPP_OUTPUT}")
    if _tree(PYTHON_OUTPUT) != _tree(generated_python):
        failures.append(f"stale or missing {PYTHON_OUTPUT}")
    if failures:
        raise RuntimeError("; ".join(failures))


def _write(generated_cpp: Path, generated_python: Path) -> None:
    CPP_OUTPUT.write_bytes(generated_cpp.read_bytes())
    if PYTHON_OUTPUT.exists():
        shutil.rmtree(PYTHON_OUTPUT)
    shutil.copytree(generated_python, PYTHON_OUTPUT)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flatc", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    flatc = args.flatc.resolve()
    if not flatc.is_file():
        raise FileNotFoundError(f"pinned flatc is missing: {flatc}")
    with tempfile.TemporaryDirectory() as temporary:
        generated_cpp, generated_python = _generate(flatc, Path(temporary))
        if args.check:
            _check(generated_cpp, generated_python)
        else:
            _write(generated_cpp, generated_python)
    print("flatbuffers-smoke-generated-v1 state=current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
