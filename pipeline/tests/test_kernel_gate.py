"""`kernel_gate` on a synthetic family: a fake row file, verdicts, skeleton and source tree.

Checks 1 (addresses), 2 (arms), 4 (hot headers) and 6 (tests) are exercised pass and fail on the
pure check functions; the seam searches (3), the residue delta (5) and the driver's exit code run
end to end on a throwaway git repository.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling" / "ghidra" / "driver"))

import kernel_gate as kg  # noqa: E402

SUB = "Source/ElysiumUE/Private/Substrate"
TST = "Source/ElysiumUE/Private/Tests"

VERDICTS = """\
# overlay
address\tverdict\tband\ttarget\tevidence
10000010\trule\t19-29\tFElysiumNpc::Alpha\tx
10000020\trule\t19-29\tFElysiumNpc::Beta\tx
10000030\tdead\t19-29\t-\tx
"""
FAMILY = """\
# Fake19
10000010\trule\t19-29\tFElysiumNpc::Alpha\tx
10000020\trule\t19-29\tFElysiumNpc::Beta\tx
10000030\trule\t19-29\tFElysiumNpc::Gamma\tx
"""
SKELETON = [
    {"addr": "0x10000010", "label": "C::Alpha", "damaged": "",
     "arms": [{"at": "0x10000014", "op": "JZ", "taken": "0x1000001a", "next": "0x10000016"}],
     "tables": [], "calls": [{"at": "0x10000018", "kind": "direct", "target": "0x10009999", "what": ""}]},
    {"addr": "0x10000020", "label": "C::Beta", "damaged": "",
     "arms": [{"at": "0x10000024", "op": "JNZ", "taken": "0x1000002a", "next": "0x10000026"}],
     "tables": [{"at": "0x10000028"}], "calls": []},
    {"addr": "0x10000030", "label": "C::Gamma", "damaged": "", "arms": [], "tables": [], "calls": []},
]
PORT = """\
// `0x10000010` Alpha
void FElysiumNpc::Alpha()
{
\tif (X) // 0x10000014
\t{
\t\tBar(); // 0x10000018
\t}
}
"""
CENSUS = '{ 433, TEXT("0x10000020"), TEXT("C"), TEXT("Beta") },\n'
TESTS = """\
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAlphaTest,
\t"Elysium.Substrate.NpcKernelFake19.Alpha 0x10000010", Flags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FBetaTest, FBase,
\t"Elysium.Substrate.NpcKernelFake19.Beta", Flags)
"""


def write(root: Path, rel: str, text: str) -> Path:
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8", newline="\n")
    return p


@pytest.fixture()
def tree(tmp_path: Path) -> dict:
    repo = tmp_path / "repo"
    data = tmp_path / "data"
    write(repo, "research/tooling/ghidra/driver/kernel_verdicts.tsv", VERDICTS)
    write(data, "families-19-29/Fake19.tsv", FAMILY)
    write(data, "skeletons-19-29/Fake19.json", json.dumps(SKELETON))
    write(repo, f"{SUB}/ElysiumNpcFake19.cpp", PORT)
    write(repo, f"{SUB}/ElysiumNpcKernelShape.cpp", CENSUS)
    write(repo, f"{SUB}/ElysiumNpcBase.h", "class FElysiumNpcBase {};\n")
    write(repo, f"{TST}/ElysiumNpcKernelFake19Tests.cpp", TESTS)
    return {"repo": repo, "families": data / "families-19-29", "skeletons": data / "skeletons-19-29",
            "verdicts": repo / "research/tooling/ghidra/driver/kernel_verdicts.tsv"}


def family(t: dict) -> kg.Family:
    return kg.load_family("Fake19", t["families"], t["skeletons"], t["verdicts"])


def index(t: dict) -> kg.SourceIndex:
    return kg.SourceIndex(t["repo"], Path("Source"))


def test_family_joins_authoritative_verdicts(tree):
    fam = family(tree)
    assert fam.rows == ["10000010", "10000020", "10000030"]
    assert fam.rules == ["10000010", "10000020"]     # 10000030 is `dead` in the overlay


# 1. Addresses --------------------------------------------------------------------------------


def test_addresses_fail_when_only_the_census_cites(tree):
    c = kg.check_addresses(family(tree), index(tree))
    assert c.status == kg.FAIL
    assert c.data["missing"] == ["0x10000020"]
    assert any("census only" in ln and "ElysiumNpcKernelShape.cpp" in ln for ln in c.lines)


def test_addresses_pass_when_a_body_cites(tree):
    write(tree["repo"], f"{SUB}/ElysiumNpcFake19b.cpp", "// 10000020 Beta, bare digits\n")
    c = kg.check_addresses(family(tree), index(tree))
    assert c.status == kg.PASS, c.lines


# 2. Arms -------------------------------------------------------------------------------------


def test_arms_fail_on_uncited_sites(tree):
    c = kg.check_arms(family(tree), index(tree), 100.0)
    assert c.status == kg.FAIL
    fn = c.data["functions"]
    assert fn["0x10000010"]["covered"] == 2 and fn["0x10000010"]["pass"]
    assert fn["0x10000020"]["missing"] == ["0x10000024", "0x10000028"]
    assert "0x10000030" not in fn                 # not a rule row


def test_arms_threshold_and_full_pass(tree):
    assert kg.check_arms(family(tree), index(tree), kg.parse_threshold("0%")).status == kg.PASS
    write(tree["repo"], f"{TST}/Extra.cpp", "// 0x10000024 0x10000028\n")
    assert kg.check_arms(family(tree), index(tree), kg.parse_threshold("all")).status == kg.PASS


def test_arms_fail_without_a_skeleton(tree):
    (tree["skeletons"] / "Fake19.json").write_text("[]", encoding="utf-8")
    c = kg.check_arms(family(tree), index(tree), 0.0)
    assert c.status == kg.FAIL


def test_parse_threshold():
    assert kg.parse_threshold("all") == 100.0
    assert kg.parse_threshold("75%") == 75.0
    assert kg.parse_threshold("50") == 50.0


# 4. Hot headers ------------------------------------------------------------------------------


def test_hot_pass_and_fail():
    ok = kg.check_hot([f"{SUB}/ElysiumNpcFake19.cpp"], None)
    assert ok.status == kg.PASS
    bad = kg.check_hot([f"{SUB}/ElysiumNpcBase.h", "research/tooling/gen_kernel_shape.py"], None)
    assert bad.status == kg.FAIL and bad.data["touched"] == [
        f"{SUB}/ElysiumNpcBase.h", "research/tooling/gen_kernel_shape.py"]


def test_hot_allow():
    assert kg.check_hot([f"{SUB}/ElysiumNpcBase.h"], []).status == kg.WARN
    assert kg.check_hot([f"{SUB}/ElysiumNpcBase.h"], ["ElysiumNpcBase.h"]).status == kg.WARN
    assert kg.check_hot([f"{SUB}/ElysiumNpcBase.h"], ["ElysiumNpc.h"]).status == kg.FAIL


# 6. Tests ------------------------------------------------------------------------------------


def test_tests_pass_on_count_and_citation(tree):
    write(tree["repo"], f"{TST}/ElysiumNpcKernelFake19Tests2.cpp", "// covers 0x10000020\n")
    c = kg.check_tests(family(tree), index(tree))
    assert c.status == kg.PASS, c.lines
    assert c.data["tests"] == 2 and len(c.data["files"]) == 2


def test_tests_fail_on_uncited_rule_and_unnamed_test(tree):
    c = kg.check_tests(family(tree), index(tree))
    assert c.status == kg.FAIL                    # Beta's name has no address, nothing cites 0x10000020
    assert c.data["uncited_anywhere"] == ["0x10000020"]


def test_tests_pass_when_every_name_carries_an_address(tree):
    write(tree["repo"], f"{TST}/ElysiumNpcKernelFake19Tests.cpp",
          TESTS.replace("NpcKernelFake19.Beta", "NpcKernelFake19.Beta 0x10000020"))
    assert kg.check_tests(family(tree), index(tree)).status == kg.PASS


def test_tests_fail_on_count_and_missing_file(tree):
    write(tree["repo"], f"{TST}/ElysiumNpcKernelFake19Tests.cpp", TESTS.split("IMPLEMENT_CUSTOM")[0]
          + "// 0x10000020\n")
    c = kg.check_tests(family(tree), index(tree))
    assert c.status == kg.FAIL and c.data["tests"] == 1
    os.remove(tree["repo"] / f"{TST}/ElysiumNpcKernelFake19Tests.cpp")
    assert kg.check_tests(family(tree), index(tree)).status == kg.FAIL


# 7. Twins ------------------------------------------------------------------------------------


def test_twin_warns(tree):
    write(tree["repo"], f"{SUB}/ElysiumNpcOld.cpp",
          "// A port-only stand-in for `0x10000010` until the body lands.\nvoid Old() {}\n")
    c = kg.check_twins(family(tree), index(tree))
    assert c.status == kg.WARN and c.data["hits"] == [f"{SUB}/ElysiumNpcOld.cpp:1"]


# 3 / 4 / 5 end to end on a git repository ------------------------------------------------------


def git(repo: Path, *args: str) -> None:
    subprocess.run(["git", "-C", str(repo), "-c", "user.email=t@t", "-c", "user.name=t",
                    "-c", "commit.gpgsign=false", *args], check=True, capture_output=True)


@pytest.mark.skipif(shutil.which("git") is None, reason="git not on PATH")
def test_driver_end_to_end(tree, capsys):
    repo = tree["repo"]
    write(repo, f"{SUB}/ElysiumNpcFake19.inl", "int32 Existing = 0; // m_iExisting +0x5bb0\n")
    write(repo, f"{SUB}/ElysiumNpcKernelShapeMap.cpp", "\tELYSIUM_NPC_WORD(0x5bb0, FElysiumNpcBase, Existing),\n")
    write(repo, "docs/vtmb/npc-kernel/unported.tsv",
          "# pin\nclass\tslot\taddress\tverdict\tkind\tport_class\n"
          "C\t433\t0x10000020\trule\tstub\tFElysiumNpc\nC\t5\t0x10009990\tmechanism\tstub\tFElysiumNpc\n")
    git(repo, "init", "-q", "-b", "main")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "base")
    git(repo, "checkout", "-q", "-b", "story")
    # The branch: a seam beside the existing accessor, a hot-header touch, Beta ported and cited.
    write(repo, f"{SUB}/ElysiumNpcFake19.inl",
          "int32 Existing = 0; // m_iExisting +0x5bb0\n"
          "/** `m_iExisting` (`+0x5bb0`), read by `0x10000010`; its body is `0x10000099`. */\n"
          "int32 ExistingAgain() const;\n")
    write(repo, f"{SUB}/ElysiumNpcBase.h", "class FElysiumNpcBase { int32 X = 0; };\n")
    write(repo, f"{SUB}/ElysiumNpcFake19b.cpp", "// 0x10000020 0x10000024 0x10000028\n// 0x10000099\n")
    write(repo, f"{TST}/ElysiumNpcKernelFake19Tests.cpp",
          TESTS.replace("NpcKernelFake19.Beta", "NpcKernelFake19.Beta 0x10000020"))
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "story")
    fresh = write(repo.parent, "fresh.tsv",
                  "class\tslot\taddress\tverdict\tkind\tport_class\n"
                  "C\t5\t0x10009990\tmechanism\tstub\tFElysiumNpc\n")
    out = repo.parent / "gate.json"
    base = ["--family", "Fake19", "--repo", str(repo), "--families", str(tree["families"]),
            "--skeletons", str(tree["skeletons"]), "--verdicts", str(tree["verdicts"]),
            "--fresh-unported", str(fresh), "--json", str(out)]

    assert kg.main(base) == 1                     # the hot header fails the gate
    result = json.loads(out.read_text(encoding="utf-8"))
    status = {c["key"]: c["status"] for c in result["checks"]}
    assert status == {"addresses": "PASS", "arms": "PASS", "seams": "WARN", "hot": "FAIL",
                      "residue": "PASS", "tests": "PASS", "twins": "PASS"}
    seams = next(c for c in result["checks"] if c["key"] == "seams")
    decl = next(d for d in seams["data"]["declarations"] if d["name"] == "ExistingAgain")
    hits = {s["command"]: s["hits"] for s in decl["searches"]}
    assert any("10000010" in cmd and hits[cmd] for cmd in hits)          # (a) the address elsewhere
    ours = next(s for s in decl["searches"] if "10000099" in s["command"])
    assert ours["hits"] == [] and ours["this_diff"] == [f"{SUB}/ElysiumNpcFake19b.cpp:2"]
    assert any("5bb0" in cmd and hits[cmd] == [f"{SUB}/ElysiumNpcKernelShapeMap.cpp:1"]
               for cmd in hits)                                          # (b) the offset in the map
    assert any("m_iExisting" in cmd and hits[cmd] == [f"{SUB}/ElysiumNpcFake19.inl:1"]
               for cmd in hits)                                          # (c) the name in the .inl
    report = capsys.readouterr().out
    assert "TOUCHED Source/ElysiumUE/Private/Substrate/ElysiumNpcBase.h" in report

    assert kg.main([*base, "--allow-hot"]) == 0

    # A rule row still unported, and a row the pin lacks, both fail the residue.
    write(repo.parent, "fresh.tsv", "C\t433\t0x10000020\trule\tstub\tFElysiumNpc\n"
                                    "D\t7\t0x10008880\trule\tstub\tFElysiumNpc\n")
    assert kg.main([*base, "--allow-hot"]) == 1
    residue = next(c for c in json.loads(out.read_text(encoding="utf-8"))["checks"] if c["key"] == "residue")
    assert residue["status"] == "FAIL"
    assert len(residue["data"]["family_rows"]) == 1 and len(residue["data"]["rose"]) == 1
    assert kg.main([*base, "--allow-hot", "--no-residue"]) == 0
