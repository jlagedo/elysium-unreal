"""The C++ automation tiers: every top-level test group is in exactly one of the two lists.

A test's tier is its name's prefix (`Elysium.<Group>.<Rest>`), so `uv run elysium test` with no
argument runs the explicit list of default groups and `--all` runs `Elysium.`. This reads the test
sources, not an editor report, so it fails the moment a group appears that neither list names:
otherwise a new group would silently be in no tier at all (the default run would never reach it and
the opt-in run would not name it), or the default run would name a group that holds no test and fail
on "a prefix matched no test".
"""

from __future__ import annotations

import re
from pathlib import Path

from elysium_pipeline import unreal

TESTS = Path(__file__).resolve().parents[2] / "Source" / "ElysiumUE" / "Private" / "Tests"

# `IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClass, "Elysium.Group.Rest", Flags)`, also the complex and
# custom forms and a `TEXT(...)` literal; the name is the first string literal after the class.
_DECLARATION = re.compile(
    r"\bIMPLEMENT_(?:CUSTOM_)?(?:SIMPLE|COMPLEX)_AUTOMATION_TEST(?:_PRIVATE)?"
    r"\s*\(\s*\w+\s*,\s*(?:TEXT\()?\s*\"(Elysium\.[^\"]*)\"")


def _declared_tests() -> list[str]:
    names: list[str] = []
    for path in sorted(TESTS.glob("*.cpp")) + sorted(TESTS.glob("*.h")):
        names.extend(_DECLARATION.findall(path.read_text(encoding="utf-8", errors="replace")))
    return names


def _groups() -> dict[str, int]:
    counts: dict[str, int] = {}
    for name in _declared_tests():
        parts = name.split(".")
        assert len(parts) >= 3, f"{name!r} is not Elysium.<Group>.<Rest>"
        counts[parts[1]] = counts.get(parts[1], 0) + 1
    return counts


# The smoke set: the arm tests that stay in the default tier beside the census and scenario tests.
# At most three per family (six for the NPC kernel), each the arm whose absence a shipped program
# would notice, chosen by reading; the name is the C++ test's, the text is why it is here.
SMOKE_SET: dict[str, tuple[str, str]] = {
    "Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.FullPass": (
        "npc-kernel", "0x10292de0, the per-think pass every NPC runs: gates, enemy triple, tail"),
    "Elysium.Substrate.NpcKernelRunAi19.BaseRunAI.FullPass": (
        "npc-kernel", "RunAI's full pass: the arm order a schedule's timing rides on"),
    "Elysium.Substrate.NpcKernelConditions19.BaseGatherConditions.StateSkipAndFlush": (
        "npc-kernel", "GatherConditions' state skip and flush: what an interrupt mask reads"),
    "Elysium.Substrate.NpcKernelSelect19.TroikaSelectSchedule.Case2Combat": (
        "npc-kernel", "SelectSchedule's combat case: which program a hostile NPC starts"),
    "Elysium.Substrate.NpcKernelStartTask19.ActivityArms_0x102a1c0f": (
        "npc-kernel", "StartTask's activity arms: the tasks that play sequences"),
    "Elysium.Substrate.NpcKernelRunTask19.Base.ActivityFinishedArms": (
        "npc-kernel", "RunTask's IsActivityFinished arms: the wait the walk-then-animate defect froze"),
    "Elysium.Substrate.NpcSenses.Sight": (
        "npc-world", "who sees whom: every stealth and combat program starts here"),
    "Elysium.Substrate.NpcEnemy.BestEnemy": ("npc-world", "enemy arbitration: whom an NPC fights"),
    "Elysium.Substrate.StealthKill.TutorialOutput": (
        "npc-world", "the output the tutorial's stealth-kill step waits for"),
    "Elysium.Substrate.RecordServiceOrder": (
        "entity-substrate", "entity I/O record order: every map script's event sequencing"),
    "Elysium.Substrate.OutputTimes": (
        "entity-substrate", "output times and delays: the scripted one-shots"),
    "Elysium.Substrate.PythonCheckTruthiness": (
        "entity-substrate", "the truthiness of the Python checks that gate map logic"),
    "Elysium.Substrate.PlaceSet.Counter": (
        "map-world", "`CNodeEnt::Spawn`'s counter: how every hint binds to its place in the network"),
    "Elysium.Substrate.PlaceSeams.NearestNode": (
        "map-world", "the nearest-node query behind hint search and wander"),
    "Elysium.Substrate.PlaceSeams.Wander.Order200": (
        "map-world", "the order retail's wander walks its candidates"),
    "Elysium.Substrate.CharacterModelAdmission.Replacement": (
        "bake-assets", "a character model swap keeps its admission"),
    "Elysium.Substrate.ExpressionData.Resolver": (
        "bake-assets", "facial expression resolution: every dialogue face"),
    "Elysium.Substrate.FlexRules": ("bake-assets", "the flex rules behind lip and eye motion"),
    "Elysium.Substrate.Damage.Apply": ("combat-player", "damage application"),
    "Elysium.Substrate.Weapons.Rules": ("combat-player", "weapon rules, 73 assertions"),
    "Elysium.Substrate.ComboChain.Sequence": ("combat-player", "the melee combo chain"),
    "Elysium.Substrate.DlgParse": ("dialogue", "the dialogue file parse"),
    "Elysium.Substrate.DlgDependency": (
        "dialogue", "choice dependency gates: what a conversation offers, 87 assertions"),
    "Elysium.Substrate.DlgBranch": ("dialogue", "conversation branching"),
    "Elysium.Substrate.CameraRig": ("camera", "the camera rig, 105 assertions"),
    "Elysium.Substrate.CameraShots": ("camera", "shot selection"),
    "Elysium.Substrate.CameraOverride": ("camera", "the script camera override"),
    "Elysium.Substrate.TerminalEmail": ("terminal", "the terminal e-mail screens, 166 assertions"),
    "Elysium.Substrate.TerminalPin": ("terminal", "PIN entry"),
    "Elysium.Substrate.Terminal.Keys": ("terminal", "terminal key handling"),
    "Elysium.Substrate.UI.HUDModelProjection": ("ui-content", "the HUD model projection"),
    "Elysium.Substrate.UiStrings": ("ui-content", "the UI strings"),
    "Elysium.Substrate.UI.HUDBloodRail": ("ui-content", "the blood rail"),
    "Elysium.Substrate.SaveRoundTrip": ("save", "a save and restore round trip"),
    "Elysium.Substrate.SaveSchema": ("save", "the save layout"),
    "Elysium.Session.SaveNamedSlot": ("save", "a named slot written and read back"),
    "Elysium.Substrate.Movement": ("other", "player movement constants and solve"),
    "Elysium.Substrate.PlayerPlacementSpace": ("other", "player placement coordinate spaces"),
    "Elysium.Substrate.Footsteps.PlayerHearing": (
        "other", "the player's footsteps as NPCs hear them"),
}
SMOKE_PER_FAMILY = {"npc-kernel": 6}


def test_the_test_sources_are_found() -> None:
    assert TESTS.is_dir(), TESTS
    assert len(_declared_tests()) > 100


def test_every_group_is_a_default_group_or_an_opt_in_one() -> None:
    known = set(unreal.DEFAULT_TEST_GROUPS) | set(unreal.OPT_IN_TEST_GROUPS)
    unlisted = sorted(set(_groups()) - known)
    assert not unlisted, (
        f"test group(s) {unlisted} are in neither `DEFAULT_TEST_GROUPS` {unreal.DEFAULT_TEST_GROUPS} "
        f"nor `OPT_IN_TEST_GROUPS` {unreal.OPT_IN_TEST_GROUPS} (elysium_pipeline/unreal.py): put "
        f"each in one, so a tier selects it")


def test_the_two_lists_are_disjoint() -> None:
    assert not set(unreal.DEFAULT_TEST_GROUPS) & set(unreal.OPT_IN_TEST_GROUPS)


def test_every_default_group_holds_a_test() -> None:
    # An empty prefix fails the whole run ("N prefix(es) matched no test"), so a default group that
    # lost its last test would turn the default run red for a reason that is not a test.
    empty = [group for group in unreal.DEFAULT_TEST_GROUPS if group not in _groups()]
    assert not empty, f"default group(s) {empty} hold no test: drop them from DEFAULT_TEST_GROUPS"


def test_every_opt_in_group_has_a_tier_word_and_a_test() -> None:
    for group in unreal.OPT_IN_TEST_GROUPS:
        assert unreal.TEST_TIERS.get(group.lower()) == (f"Elysium.{group}.",), group
    empty = [group for group in unreal.OPT_IN_TEST_GROUPS if group not in _groups()]
    assert not empty, f"opt-in group(s) {empty} hold no test: `elysium test` would match nothing"


def test_the_default_tier_is_small_and_the_arm_tier_is_the_bulk() -> None:
    # The budget the tiers exist for: the default run is the census, the scenarios and a smoke set
    # (under four hundred tests), and the per-function arm and unit tests are the opt-in bulk.
    counts = _groups()
    default = sum(counts.get(group, 0) for group in unreal.DEFAULT_TEST_GROUPS)
    assert 0 < default <= 400, f"{default} default-tier tests: the default run is over budget"
    assert counts.get("Arm", 0) > default


_ARM_GUARD = unreal.ARM_TIER_MACRO
_DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$")


def _guarded_lines(text: str) -> list[bool]:
    """Per line, whether it sits inside an `#if` whose live branch requires the arm-tier macro."""

    stack: list[bool] = []
    guarded: list[bool] = []
    for line in text.splitlines():
        match = _DIRECTIVE.match(line)
        if match:
            kind, condition = match.group(1), match.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                stack.append(kind != "ifndef" and _ARM_GUARD in condition
                             and "!" not in condition.split(_ARM_GUARD)[0][-2:])
            elif kind == "elif" and stack:
                stack[-1] = False
            elif kind == "else" and stack:
                stack[-1] = False
            elif kind == "endif" and stack:
                stack.pop()
        guarded.append(any(stack))
    return guarded


def _cases_by_guard() -> tuple[list[str], list[str], list[str]]:
    """(arm cases outside the guard, other cases inside it, guarded files missing the header)."""

    loose, hidden, headerless = [], [], []
    for path in sorted(TESTS.glob("*.cpp")):
        text = path.read_text(encoding="utf-8", errors="replace")
        guarded = _guarded_lines(text)
        if _ARM_GUARD in text and f'#include "Tests/{unreal.ARM_TIER_HEADER.name}"' not in text:
            headerless.append(path.name)
        for match in _DECLARATION.finditer(text):
            line = text.count("\n", 0, match.start())
            name = match.group(1)
            is_arm = name.startswith(unreal.ARM_TIER_GROUP)
            if is_arm and not guarded[line]:
                loose.append(f"{path.name}: {name}")
            elif not is_arm and guarded[line]:
                hidden.append(f"{path.name}: {name}")
    return loose, hidden, headerless


def test_every_arm_case_compiles_only_with_the_arm_tier() -> None:
    # Spec 0002 T6: the arm tier is compiled on demand, so a build that runs no arm test does not pay
    # for one. A new `Elysium.Arm.` case goes inside `#if ELYSIUM_WITH_ARM_TESTS` (the whole file's
    # automation guard for a file of arm cases only) with `Tests/ElysiumArmTier.h` included first.
    loose, _hidden, headerless = _cases_by_guard()
    assert not loose, f"arm case(s) compiled without the arm tier: {loose[:10]}"
    assert not headerless, f"file(s) test {_ARM_GUARD} without including its header: {headerless}"


def test_no_default_or_other_opt_in_case_hides_behind_the_arm_guard() -> None:
    # The other way round is a silent loss: a default-tier case behind the guard is absent from every
    # plain build, so the default run would stop running it and still pass.
    _loose, hidden, _headerless = _cases_by_guard()
    assert not hidden, f"non-arm case(s) compiled only with the arm tier: {hidden[:10]}"


def test_the_smoke_set_is_in_the_default_tier_and_within_its_cap() -> None:
    declared = set(_declared_tests())
    gone = sorted(set(SMOKE_SET) - declared)
    assert not gone, f"smoke test(s) renamed or deleted: {gone}"
    default_groups = {f"Elysium.{group}." for group in unreal.DEFAULT_TEST_GROUPS}
    outside = sorted(name for name in SMOKE_SET
                     if not any(name.startswith(prefix) for prefix in default_groups))
    assert not outside, f"smoke test(s) outside the default tier: {outside}"

    per_family: dict[str, int] = {}
    for family, _why in SMOKE_SET.values():
        per_family[family] = per_family.get(family, 0) + 1
    over = {family: n for family, n in per_family.items() if n > SMOKE_PER_FAMILY.get(family, 3)}
    assert not over, f"more smoke tests than a family may keep: {over}"
    assert all(why.strip() for _family, why in SMOKE_SET.values())
