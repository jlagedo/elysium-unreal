"""The opt-in Arm tier is compiled on demand (spec 0002 T6).

One generated header, `Source/ElysiumUE/Private/Tests/ElysiumArmTier.h`, says whether a build carries
the `Elysium.Arm.*` cases. A plain build switches it off, `build --arm` and a test selection that
reaches an arm case switch it on, and the test command builds before an arm run, so what runs is what
the tree says. Only the value is compared, and an unchanged value is never rewritten, so nothing
recompiles for a header that already says the right thing.
"""
from __future__ import annotations

from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest import mock

from elysium_pipeline import unreal


def _config(root: Path) -> SimpleNamespace:
    return SimpleNamespace(repo_root=root, project=root / "ElysiumUE.uproject", work_root=root / "work",
                           export_root=root / "exports")


def _header(root: Path) -> Path:
    return root / unreal.ARM_TIER_HEADER


def test_the_default_tier_and_the_other_opt_in_tiers_do_not_reach_the_arm_tier() -> None:
    assert not unreal.selection_needs_arm_tier(unreal.DEFAULT_TEST_FILTER)
    assert not unreal.selection_needs_arm_tier(["Elysium.Content.", "Elysium.Slow."])
    assert not unreal.selection_needs_arm_tier(["Elysium.Substrate.NpcKernelShape."])


def test_an_arm_prefix_the_whole_suite_or_a_bare_substring_reaches_it() -> None:
    assert unreal.selection_needs_arm_tier(["Elysium.Arm."])
    assert unreal.selection_needs_arm_tier(["Elysium.Arm.NpcKernelSelect19."])
    assert unreal.selection_needs_arm_tier(["Elysium."])
    assert unreal.selection_needs_arm_tier(unreal.resolve_test_filters(["all"]))
    # `Automation RunTest` matches by substring, so a filter outside `Elysium.` can name an arm case.
    assert unreal.selection_needs_arm_tier(["NpcKernelSelect19."])


def test_switching_writes_the_header_once_and_reads_back() -> None:
    with TemporaryDirectory() as temp:
        root = Path(temp)
        config = _config(root)
        assert unreal.arm_tier_compiled(config) is None
        assert unreal.set_arm_tier(config, False) is True
        assert unreal.arm_tier_compiled(config) is False
        stamp = _header(root).stat().st_mtime_ns
        assert unreal.set_arm_tier(config, False) is False, "an unchanged value is not rewritten"
        assert _header(root).stat().st_mtime_ns == stamp
        assert unreal.set_arm_tier(config, True) is True
        assert unreal.arm_tier_compiled(config) is True
        assert "#define ELYSIUM_WITH_ARM_TESTS 1" in _header(root).read_text(encoding="utf-8")


def test_a_header_written_by_another_hand_is_judged_by_its_value_alone() -> None:
    with TemporaryDirectory() as temp:
        root = Path(temp)
        config = _config(root)
        _header(root).parent.mkdir(parents=True)
        _header(root).write_text("#pragma once\n#define ELYSIUM_WITH_ARM_TESTS 0\n", encoding="utf-8")
        assert unreal.arm_tier_compiled(config) is False
        assert unreal.set_arm_tier(config, False) is False


class _Builds:
    def __init__(self) -> None:
        self.calls: list[str] = []

    def __call__(self, config, runner, mode="", extra=()) -> None:
        self.calls.append(mode)


def test_a_selection_reaching_the_tier_switches_it_on_and_rebuilds_first() -> None:
    with TemporaryDirectory() as temp:
        root = Path(temp)
        config = _config(root)
        unreal.set_arm_tier(config, False)
        builds = _Builds()
        with mock.patch.object(unreal, "build", side_effect=builds):
            assert unreal.prepare_arm_tier(config, None, ["Elysium.Arm."]) == (True, True)
            assert unreal.arm_tier_compiled(config) is True
            assert builds.calls == [""], "one incremental build before the run"
            # Already on: nothing to switch, and the build is still run, so an arm test edited since
            # the last build is the one that runs (a build with nothing to do is two seconds).
            assert unreal.prepare_arm_tier(config, None, ["Elysium."]) == (False, True)
            assert builds.calls == ["", ""]


def test_a_selection_outside_the_tier_leaves_the_header_and_builds_nothing() -> None:
    with TemporaryDirectory() as temp:
        root = Path(temp)
        config = _config(root)
        unreal.set_arm_tier(config, False)
        builds = _Builds()
        with mock.patch.object(unreal, "build", side_effect=builds):
            assert unreal.prepare_arm_tier(config, None, list(unreal.DEFAULT_TEST_FILTER)) == (False, False)
        assert builds.calls == []
        assert unreal.arm_tier_compiled(config) is False


def test_a_failed_build_puts_the_tier_back_so_the_header_never_claims_a_build_that_did_not_happen() -> None:
    with TemporaryDirectory() as temp:
        root = Path(temp)
        config = _config(root)
        unreal.set_arm_tier(config, False)
        with mock.patch.object(unreal, "build", side_effect=unreal.UnrealFailure("compile error")):
            try:
                unreal.prepare_arm_tier(config, None, ["Elysium.Arm."])
            except unreal.UnrealFailure:
                pass
            else:
                raise AssertionError("the build failure must reach the caller")
        assert unreal.arm_tier_compiled(config) is False
