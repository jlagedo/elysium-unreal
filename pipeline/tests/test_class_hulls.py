"""The recovered class -> hull table, and the generator that emits it.

`docs/vtmb/data/class_hulls.json` records which hull each retail class STANDS on (`m_eHull`,
+0x1568) and PATHS with (+0x156c), recovered 2026-09-20 by four independent walks over
vampire.dll. The port's NavMesh agent follows the pathing word and its capsule the standing one,
so a wrong row here is a body that paths on a mesh it does not fit.
"""

import json
import subprocess
import sys

from elysium_pipeline.paths import repo_root


def _rows():
    path = repo_root() / "docs" / "vtmb" / "data" / "class_hulls.json"
    return json.loads(path.read_text(encoding="utf-8"))["rows"]


def _hull_count():
    path = repo_root() / "docs" / "vtmb" / "data" / "hull_table.json"
    return len(json.loads(path.read_text(encoding="utf-8"))["rows"])


def test_every_named_hull_is_a_row_of_the_table():
    # The accessors index the pointer table raw and bounds-check nothing, so a hull outside the
    # table would fault rather than answer -- which is exactly what retail's unassigned 23 does.
    count = _hull_count()
    for row in _rows():
        if row["class"] == "CBaseCombatCharacter":
            continue
        assert 0 <= row["standing"] < count, row
        assert 0 <= row["pathing"] < count, row


def test_the_base_combat_character_sentinel_is_out_of_range_on_purpose():
    base = next(r for r in _rows() if r["class"] == "CBaseCombatCharacter")
    assert base["standing"] == base["pathing"] == 23
    assert base["standing"] >= _hull_count()


def test_the_three_species_whose_two_words_differ():
    # The whole reason the port carries two words. Everything else agrees, and a row that starts
    # disagreeing is a recovery change that has to be argued rather than absorbed.
    split = {r["class"]: (r["standing"], r["pathing"])
             for r in _rows() if r["standing"] != r["pathing"]
             and r["class"] != "CBaseCombatCharacter"}
    assert split == {
        "CNPC_VSheriffMan": (21, 0),     # stands SHERIFF, routes on the human mesh
        "CNPC_VHengeyokai": (0, 18),     # the inverse
        "CNPC_VMingXiao": (15, 16),      # what MING_XIAO_PATHING_HULL exists for
    }


def test_the_rat_inherits_its_hull_and_has_no_row_of_its_own():
    classes = [r["class"] for r in _rows()]
    assert "CNPC_VRat" not in classes
    scurrying = next(r for r in _rows() if r["class"] == "CNPC_VScurrying")
    assert (scurrying["standing"], scurrying["pathing"]) == (19, 19)


def test_rows_are_ordered_most_derived_first():
    # The runtime takes the FIRST row whose class is in the body's chain, so a base listed before
    # a species would claim every one of its descendants.
    classes = [r["class"] for r in _rows()]
    for base, derived in (("CAI_BaseNPC", "CAI_BaseNPCTroika"),
                          ("CAI_BaseNPCTroika", "CNPC_VHuman"),
                          ("CNPC_VHuman", "CNPC_VSheriffMan"),
                          ("CBaseCombatCharacter", "CAI_BaseNPC")):
        assert classes.index(derived) < classes.index(base), (derived, base)


def test_every_row_cites_the_instruction_that_writes_it():
    for row in _rows():
        assert row["ctor"].startswith("0x") and row["store"].startswith("0x"), row


def test_generator_check_mode_matches_the_committed_header():
    result = subprocess.run(
        [sys.executable, "-m", "elysium_pipeline.cli", "research", "gen_hull_table", "--check"],
        cwd=repo_root(), capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
