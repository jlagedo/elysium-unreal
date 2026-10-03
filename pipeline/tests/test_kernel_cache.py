"""`kernel_cache`: the kernel tools' build kept between processes, and the `--check` stamp.

Everything here runs on a temporary work root and a temporary checkout: a stage is stored and read
back on its key, a per-file scan is answered from the table until the file changes, and a passing
`--check` is replayed byte for byte until anything it reads changes -- while a failure is never
recorded and `ELYSIUM_KERNEL_CACHE=off` runs every time.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import kernel_cache as kc  # noqa: E402


@pytest.fixture
def work(tmp_path, monkeypatch):
    """A throwaway work root (the cache lives under it) and a checkout with one source file."""
    monkeypatch.setenv("ELYSIUM_WORK_ROOT", str(tmp_path / "work"))
    monkeypatch.delenv("ELYSIUM_KERNEL_CACHE", raising=False)
    repo = tmp_path / "repo"
    (repo / "Source" / "ElysiumUE").mkdir(parents=True)
    (repo / "Source" / "ElysiumUE" / "Npc.cpp").write_text("// 0x1029a0b0\n", encoding="utf-8")
    kc.forget()
    yield repo
    kc.forget()


def _touch(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    stat = path.stat()
    os.utime(path, ns=(stat.st_atime_ns, stat.st_mtime_ns + 10_000_000))   # a later mtime, surely


def test_a_stage_reads_back_on_its_key_and_misses_on_another(work):
    kc.store("ledger", "k1", {"closure": [1, 2]})
    assert kc.load("ledger", "k1") == {"closure": [1, 2]}
    assert kc.load("ledger", "k2") is None
    assert kc.load("ledger", None) is None


def test_a_torn_file_is_a_miss(work):
    kc.store("shape", "k1", [1])
    path = next(kc.root().glob("shape-k1.pickle"))
    path.write_bytes(path.read_bytes()[:3])
    assert kc.load("shape", "k1") is None


def test_old_keys_are_pruned_past_the_limit(work, monkeypatch):
    monkeypatch.setattr(kc, "KEEP", 2)
    for n in range(4):
        kc.store("sdk", f"k{n}", n, keep=kc.KEEP)
        os.utime(kc.root() / f"sdk-k{n}.pickle", (n + 1, n + 1))
    kept = sorted(p.name for p in kc.root().glob("sdk-*.pickle"))
    assert kept == ["sdk-k2.pickle", "sdk-k3.pickle"]


def test_off_stores_and_reads_nothing(work, monkeypatch):
    monkeypatch.setenv("ELYSIUM_KERNEL_CACHE", "off")
    kc.store("ledger", "k1", 1)
    assert kc.load("ledger", "k1") is None
    assert kc.root() is None


def test_file_scans_rescan_only_a_changed_file(work):
    source = work / "Source" / "ElysiumUE" / "Npc.cpp"
    scanned = []

    def scan():
        scanned.append(1)
        return source.read_text(encoding="utf-8")

    first = kc.FileScans("citations", "k")
    assert first.get(source, "Npc.cpp", (True,), scan) == "// 0x1029a0b0\n"
    first.save()
    again = kc.FileScans("citations", "k")
    assert again.get(source, "Npc.cpp", (True,), scan) == "// 0x1029a0b0\n"
    assert len(scanned) == 1                      # answered from the table
    assert again.get(source, "Npc.cpp", (False,), scan) and len(scanned) == 2   # other flags: rescan
    _touch(source, "// 0x10273390\n")
    assert again.get(source, "Npc.cpp", (False,), scan) == "// 0x10273390\n"
    assert len(scanned) == 3


def test_file_scans_forget_a_file_no_run_asks_for(work):
    source = work / "Source" / "ElysiumUE" / "Npc.cpp"
    table = kc.FileScans("citations", "k")
    table.get(source, "Gone.cpp", (), lambda: 1)
    table.get(source, "Npc.cpp", (), lambda: 2)
    table.save()
    later = kc.FileScans("citations", "k")
    later.get(source, "Npc.cpp", (), lambda: 2)
    later.save()
    assert set(kc.FileScans("citations", "k").entries) == {"Npc.cpp"}


def _check(calls: list, status: int = 0, text: str = "kernel_x --check: 3 files match\n"):
    def run() -> int:
        calls.append(1)
        print(text, end="")
        return status
    return run


def test_a_passing_check_is_replayed_until_an_input_changes(work, capsys):
    calls: list = []
    assert kc.stamped("kernel_x", {"check": True}, work, _check(calls)) == 0
    assert kc.stamped("kernel_x", {"check": True}, work, _check(calls)) == 0
    assert len(calls) == 1                         # the second answer is the replay
    out = capsys.readouterr().out
    assert out == "kernel_x --check: 3 files match\n" * 2
    _touch(work / "Source" / "ElysiumUE" / "Npc.cpp", "// 0x10273390\n")
    kc.forget()                                    # a new process
    assert kc.stamped("kernel_x", {"check": True}, work, _check(calls)) == 0
    assert len(calls) == 2


def test_options_are_part_of_the_stamp(work):
    calls: list = []
    kc.stamped("kernel_x", {"check": True}, work, _check(calls))
    kc.stamped("kernel_x", {"check": True, "reach": ["sp_tutorial_1"]}, work, _check(calls))
    kc.stamped("kernel_y", {"check": True}, work, _check(calls))
    assert len(calls) == 3


def test_a_failing_check_is_never_recorded(work):
    calls: list = []
    assert kc.stamped("kernel_x", {"check": True}, work, _check(calls, status=1)) == 1
    assert kc.stamped("kernel_x", {"check": True}, work, _check(calls, status=1)) == 1
    assert len(calls) == 2


def test_a_refusal_propagates_and_records_nothing(work):
    def refuse() -> int:
        raise SystemExit("kernel_x: no corpus")

    with pytest.raises(SystemExit):
        kc.stamped("kernel_x", {"check": True}, work, refuse)
    calls: list = []
    kc.stamped("kernel_x", {"check": True}, work, _check(calls))
    assert len(calls) == 1


def test_an_extra_input_unseals_the_stamp(work, tmp_path):
    image = tmp_path / "vampire.dll"
    image.write_bytes(b"MZ")
    calls: list = []
    kc.stamped("kernel_x", {}, work, _check(calls), extra=[image])
    kc.stamped("kernel_x", {}, work, _check(calls), extra=[image])
    assert len(calls) == 1
    image.write_bytes(b"MZ\x90")
    kc.forget()
    kc.stamped("kernel_x", {}, work, _check(calls), extra=[image])
    assert len(calls) == 2


def test_off_runs_every_check(work, monkeypatch):
    monkeypatch.setenv("ELYSIUM_KERNEL_CACHE", "off")
    calls: list = []
    kc.stamped("kernel_x", {}, work, _check(calls))
    kc.stamped("kernel_x", {}, work, _check(calls))
    assert len(calls) == 2
