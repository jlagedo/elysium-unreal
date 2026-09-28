"""`research kernel`: the one-process run over the seven NPC kernel generators.

What is tested here is the runner's own contract on stand-in tools — a refusal is a FAIL row with
its reason, an exit code is kept, and the tools after a failure still run. The generators
themselves are covered by their own tests.
"""

from __future__ import annotations

import os
import sys
import tempfile
import types
from pathlib import Path

# The runner imports every generator, and they import `corpus`, which resolves the work root as it
# loads; an existing directory is all the import needs.
os.environ.setdefault("ELYSIUM_WORK_ROOT", tempfile.gettempdir())

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling"))

import kernel  # noqa: E402


def _tool(main):
    return types.SimpleNamespace(main=main)


def test_a_refusal_is_a_fail_row_with_its_reason(capsys):
    def refuse(argv):
        print("census built")
        raise SystemExit("gen_kernel_shape: no declaration reads for the ported override")

    assert kernel.run("gen_kernel_shape", _tool(refuse), check=True, verbose=False) == 1
    out = capsys.readouterr().out
    row = next(line for line in out.splitlines() if "gen_kernel_shape " in line)
    assert "FAIL" in row and row.endswith("no declaration reads for the ported override")
    assert "census built" in out        # a failure prints everything the tool said


def test_an_exit_code_is_kept():
    def stop(argv):
        raise SystemExit(3)

    def done(argv):
        raise SystemExit

    assert kernel.run("stop", _tool(stop), check=True, verbose=False) == 3
    assert kernel.run("done", _tool(done), check=True, verbose=False) == 0


def test_the_tools_after_a_refusal_still_run(monkeypatch):
    ran = []

    def refuse(argv):
        ran.append("first")
        raise SystemExit("why")

    def fine(argv):
        ran.append("second")
        return 0

    monkeypatch.setattr(kernel, "TOOLS", (("first", _tool(refuse)), ("second", _tool(fine))))
    monkeypatch.setattr(kernel, "fresh_builds", lambda: None)
    assert kernel.run_all(check=True, verbose=False) == ["first"]
    assert ran == ["first", "second"]
