#!/usr/bin/env python3
"""Fetch + vendor the embedded CPython 2.7 SDK for the runtime (P5 5.5 / 9.3).

The Elysium runtime embeds a maintained CPython 2.7.18 (qnox/python-2.7) to run VtMB's own
loose Python 2.1 level scripts 1:1 (see docs/project/roadmap.md decision log, 2026-07-22 5.5 entry).
The interpreter is open source (PSF), not game-sourced -- but it is ~37 MB, so like the rest
of $ELYSIUM_EXPORT_ROOT it is regenerable, not committed. This script downloads the pinned release, verifies
its SHA-256, and lays it out under Source/ElysiumUE/ThirdParty/CPython27/ exactly as ElysiumUE.Build.cs
+ ElysiumPythonVM.cpp expect:

    ThirdParty/CPython27/include/          (Python.h + headers)
    ThirdParty/CPython27/libs/python27.lib (import lib linked by the module)
    ThirdParty/CPython27/bin/python27.dll  (staged next to the module binary at build)
    ThirdParty/CPython27/PythonHome/Lib/   (stdlib for Py_Initialize bootstrap)

Windows x64 only (the embed is Win64-only; elsewhere ELYSIUM_WITH_CPYTHON=0).
Run through ``dev/elysium.ps1 bootstrap``.
"""

import hashlib
import io
import json
import os
import shutil
import sys
import tarfile
import urllib.request
from elysium_pipeline.paths import repo_root

REPO = os.fspath(repo_root())
with open(os.path.join(REPO, "dev", "dependencies.lock.json"), encoding="utf-8") as handle:
    LOCK = json.load(handle)
DEPENDENCY = next(item for item in LOCK["artifacts"] if item["name"] == "CPython27")
URL = DEPENDENCY["url"]
SHA256 = DEPENDENCY["sha256"]
VERSION = DEPENDENCY["version"]
ASSET = URL.rsplit("/", 1)[-1].replace("%2B", "+")
DEST = os.path.join(REPO, *DEPENDENCY["destination"].split("/"))


def main():
    if os.path.isfile(os.path.join(DEST, "bin", "python27.dll")) and "--force" not in sys.argv:
        print(f"[fetch] already present at {DEST} (pass --force to re-fetch)")
        return 0

    print(f"[fetch] downloading {ASSET} ...")
    blob = urllib.request.urlopen(URL).read()
    got = hashlib.sha256(blob).hexdigest()
    if got != SHA256:
        print(f"[fetch] SHA-256 MISMATCH\n  expected {SHA256}\n  got      {got}", file=sys.stderr)
        return 1
    print(f"[fetch] sha256 ok ({len(blob)} bytes)")

    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:gz") as tar:
        tmp = os.path.join(DEST, "_extract")
        shutil.rmtree(tmp, ignore_errors=True)
        tar.extractall(tmp)  # -> _extract/python/{include,libs,python27.dll,Lib,...}
    src = os.path.join(tmp, "python")

    def place(rel_src, rel_dst):
        s = os.path.join(src, rel_src)
        d = os.path.join(DEST, rel_dst)
        shutil.rmtree(d, ignore_errors=True) if os.path.isdir(s) else None
        os.makedirs(os.path.dirname(d), exist_ok=True)
        (shutil.copytree if os.path.isdir(s) else shutil.copy2)(s, d)

    print("[fetch] laying out include / libs / bin / PythonHome/Lib ...")
    place("include", "include")
    place("libs/python27.lib", "libs/python27.lib")
    place("python27.dll", "bin/python27.dll")
    place("Lib", "PythonHome/Lib")
    shutil.rmtree(tmp, ignore_errors=True)

    with open(os.path.join(DEST, "VERSION.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"cpython {VERSION} (qnox/python-2.7), x86_64-pc-windows-msvc\n"
                f"sha256 {SHA256}\n{URL}\n")
    print(f"[fetch] done -> {DEST}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
