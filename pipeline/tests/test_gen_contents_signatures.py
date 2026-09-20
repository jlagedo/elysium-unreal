"""The contents-signature generator and the three files it keeps in step.

The runtime header, the offline module and the engine's collision profiles are one fact written
three ways. What is asserted here is that they cannot drift apart, that the derivations the
generator makes are the ones story 3's design argues for, and that the offline module partitions
real shipped brushes exactly as the independent census probe does.

The profile rules carry weight beyond tidiness, so each has its own test:
`Pawn` is what makes a body cut the NavMesh, `Vehicle` is what would silently make a sight-only
body cut it by accident, and only a profile NAME survives a `.umap` save, which is why the
signature has to reach Unreal as one.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "research" / "tooling"))
sys.path.insert(0, str(REPO / "research" / "tooling" / "probes"))

import gen_contents_signatures as gen  # noqa: E402

from elysium_pipeline.formats import contents_signature as mirror  # noqa: E402

LETTERS = "PNSp"


def _have_install() -> bool:
    root = os.environ.get("ELYSIUM_VTMB_ROOT")
    return bool(root) and Path(root).is_dir()


def test_the_committed_files_match_the_masks():
    assert gen.main(["--check"]) == 0


# ---------------------------------------------------------------- derivations

def test_a_signature_spells_its_answers_in_order():
    assert gen.spell(0b0000, LETTERS) == "----"
    assert gen.spell(0b0111, LETTERS) == "PNS-"
    assert gen.spell(0b1111, LETTERS) == "PNSp"
    assert gen.spell(0b0010, LETTERS) == "-N--"


def test_the_profile_name_keeps_only_the_answers_given():
    assert gen.profile_name(0b0111, LETTERS) == "ElysiumSig_PNS"
    assert gen.profile_name(0b0010, LETTERS) == "ElysiumSig_N"
    assert gen.profile_name(0b1000, LETTERS) == "ElysiumSig_p"
    # The word that answers nothing still needs a name rather than an empty one.
    assert gen.profile_name(0b0000, LETTERS) == "ElysiumSig_None"


def test_pawn_blocks_exactly_when_an_npc_is_blocked():
    """This is the whole mechanism: Unreal reads navigation relevance off ECC_Pawn."""

    for bits in range(16):
        blocks_npc = bool(bits & 0b0010)
        expected = "ECR_Block" if blocks_npc else "ECR_Ignore"
        assert gen.responses(bits, LETTERS)["Pawn"] == expected


def test_vehicle_is_ignored_for_every_signature():
    """`IsNavigationRelevant` also accepts a Block on ECC_Vehicle, and VtMB ships no vehicle.

    A stray Block here would make a sight-only or pedestrian body cut the NavMesh without
    blocking a single NPC -- a defect with no symptom until a route goes missing.
    """

    for bits in range(16):
        assert gen.responses(bits, LETTERS)["Vehicle"] == "ECR_Ignore"


def test_the_two_pawns_are_asked_separately():
    npc_only, player_only = 0b0010, 0b0001
    assert gen.responses(npc_only, LETTERS)["Pawn"] == "ECR_Block"
    assert gen.responses(npc_only, LETTERS)["ElysiumPlayer"] == "ECR_Ignore"
    assert gen.responses(player_only, LETTERS)["Pawn"] == "ECR_Ignore"
    assert gen.responses(player_only, LETTERS)["ElysiumPlayer"] == "ECR_Block"


def test_sight_is_answered_by_contents_and_not_by_solidity():
    """A window stops both pawns and no sight trace; an unsolid OPAQUE brush does the reverse."""

    window = mirror.signature_of(0x10000002)          # SOLID absent, WINDOW present
    assert gen.spell(window, LETTERS) == "PN--"
    assert gen.responses(window, LETTERS)["ElysiumSight"] == "ECR_Ignore"

    shadow = mirror.signature_of(0x08000080)          # OPAQUE | DETAIL, no SOLID
    assert gen.spell(shadow, LETTERS) == "--S-"
    assert gen.responses(shadow, LETTERS)["ElysiumSight"] == "ECR_Block"
    assert gen.responses(shadow, LETTERS)["Pawn"] == "ECR_Ignore"
    assert gen.responses(shadow, LETTERS)["ElysiumPlayer"] == "ECR_Ignore"


def test_the_world_never_takes_the_use_ray_or_the_debug_pick():
    for bits in range(16):
        table = gen.responses(bits, LETTERS)
        assert table["ElysiumUse"] == "ECR_Ignore"
        assert table["ElysiumPick"] == "ECR_Ignore"


def test_physics_follows_the_player_answer_not_the_npc_one():
    """A monster clip is not a physics surface: a dropped prop falls straight through it."""

    npc_only = 0b0010
    assert gen.responses(npc_only, LETTERS)["PhysicsBody"] == "ECR_Ignore"
    assert gen.collision_enabled(npc_only, LETTERS) == "QueryOnly"
    assert gen.collision_enabled(0b0111, LETTERS) == "QueryAndPhysics"
    # A pedestrian volume is rows for a nav modifier and nothing else.
    assert gen.collision_enabled(0b1000, LETTERS) == "NoCollision"


def test_every_profile_lists_every_channel():
    """An omitted channel falls back to its default, and ElysiumUse defaults to Block."""

    for bits in range(16):
        assert set(gen.responses(bits, LETTERS)) == set(gen.PROFILE_CHANNELS)


# ---------------------------------------------------------------- the emitted artefacts

def test_the_ini_block_declares_both_channels_and_the_shipped_profiles():
    block = gen.emit_ini_block(LETTERS)
    assert "ElysiumSight" in block and "bTraceType=True" in block
    assert "ElysiumPlayer" in block and "bTraceType=False" in block   # an object channel
    for mark in gen.SHIPPED:
        assert f'Name="{gen.profile_name(_bits(mark), LETTERS)}"' in block
    # `P---` never ships, so it gets no profile.
    assert 'Name="ElysiumSig_P"' not in block
    assert block.isascii(), "the engine ini stays ASCII"


def test_every_engine_profile_answers_the_player_as_it_answers_a_pawn():
    """The one rule behind the player's new channel, checked against the engine's own table.

    `UCollisionProfile::FillProfileData` seeds every profile from the default response container
    and then applies its `CustomResponses`, so a profile that never names `ElysiumPlayer` falls
    back to that channel's DefaultResponse -- Block. The nine engine profiles that leave `Pawn` at
    Block therefore need nothing; the ten that say Overlap or Ignore about `Pawn` must say the
    same about `ElysiumPlayer`, or a trigger volume becomes a wall the player cannot walk through.
    """

    engine_ini = _engine_base_ini()
    if engine_ini is None:
        pytest.skip("the engine install is not on this machine")

    import re

    expected = {}
    for line in engine_ini.splitlines():
        if not line.startswith("+Profiles="):
            continue
        name = re.search(r'Name="([^"]+)"', line)
        pawn = re.search(r'\(Channel="?Pawn"?,\s*Response=(\w+)\)', line)
        if name and pawn and pawn.group(1) != "ECR_Block":
            expected[name.group(1)] = pawn.group(1)

    assert dict(gen.PAWN_MIRROR_PROFILES) == expected, (
        "the engine's profile table changed; regenerate PAWN_MIRROR_PROFILES from it")


def test_the_mirror_entries_reach_the_generated_block():
    block = gen.emit_ini_block(LETTERS)
    for name, response in gen.PAWN_MIRROR_PROFILES:
        assert f'+EditProfiles=(Name="{name}",' in block
        assert f'(Channel="ElysiumPlayer",Response={response})' in block
    # The trigger family is the one that would fail loudest, so name it outright.
    assert '+EditProfiles=(Name="Trigger",CustomResponses=((Channel="ElysiumPlayer",' \
           'Response=ECR_Overlap)))' in block


def test_the_player_profile_is_a_pawn_on_the_players_own_channel():
    block = gen.emit_ini_block(LETTERS)
    assert f'+Profiles=(Name="{gen.PLAYER_PROFILE}",CollisionEnabled=QueryAndPhysics,' in block
    assert 'ObjectTypeName="ElysiumPlayer"' in block
    # The player occludes an NPC's sight, as MONSTER does in retail's own sight mask.
    assert '(Channel="ElysiumSight",Response=ECR_Block)' in block


def test_the_channel_header_matches_the_declared_channels():
    header = (REPO / "Source" / "ElysiumUE" / "Public" / "ElysiumCollisionChannels.h").read_text(
        encoding="utf-8")
    for channel, name, _default, _trace in gen.NEW_CHANNELS:
        constant = f"{name.removeprefix('Elysium')}Channel"
        assert f"{constant} = {channel};" in header, f"{name} is not bound to {channel}"
    assert f'PlayerPawnProfile = FName(TEXT("{gen.PLAYER_PROFILE}"))' in header
    # The two existing channels keep their numbers: a renumbering would silently repoint
    # every +use and pick trace in the project.
    ini = (REPO / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
    assert 'Channel=ECC_GameTraceChannel1,DefaultResponse=ECR_Block' in ini and '"ElysiumUse"' in ini
    assert 'Channel=ECC_GameTraceChannel2,DefaultResponse=ECR_Ignore' in ini


def test_the_projects_own_profiles_name_both_new_channels():
    """A project profile that omits them inherits Block, which would wall the player off."""

    ini = (REPO / "Config" / "DefaultEngine.ini").read_text(encoding="utf-8")
    for line in ini.splitlines():
        if line.startswith('+Profiles=(Name="ElysiumPickOnly"') or \
                line.startswith('+Profiles=(Name="ElysiumBrushPassable"'):
            assert '(Channel="ElysiumPlayer",Response=ECR_Ignore)' in line
            assert '(Channel="ElysiumSight",Response=ECR_Ignore)' in line
        if line.startswith('+Profiles=(Name="ElysiumPropSolid"'):
            # A solid prop is a model entity and the sight mask names SOLID.
            assert '(Channel="ElysiumSight",Response=ECR_Block)' in line


def _engine_base_ini() -> str | None:
    root = os.environ.get("ELYSIUM_UE_ROOT")
    if not root:
        return None
    path = Path(root) / "Engine" / "Config" / "BaseEngine.ini"
    if not path.is_file():
        return None
    return path.read_text(encoding="utf-8", errors="replace")


def test_the_marked_block_round_trips_in_place():
    block = gen.emit_ini_block(LETTERS)
    first = gen.splice_ini("[/Script/Engine.CollisionProfile]\n+Profiles=(Name=\"X\")\n\n[Other]\n",
                           block)
    assert first.count(gen.BEGIN_MARKER) == 1
    # Splicing again replaces rather than appends, which is what keeps --check meaningful.
    assert gen.splice_ini(first, block) == first


def test_the_mirror_and_the_header_agree_on_the_masks():
    header = (REPO / "Source" / "ElysiumUE" / "Public" / "ElysiumContentsSignature.h").read_text(
        encoding="utf-8")
    for key, _letter, mask in mirror.MASKS:
        assert f"{key.capitalize()}Mask = {mask:#x};" in header
    assert f"StandableNormalZ = {mirror.STANDABLE_NORMAL_Z}f;" in header
    assert f"StepHeightUnits = {mirror.STEP_HEIGHT_UNITS}f;" in header
    assert f"GraphBuildStepHeightUnits = {mirror.GRAPH_BUILD_STEP_HEIGHT_UNITS}f;" in header


def test_the_mirror_answers_navigation_relevance_by_the_npc_bit():
    assert mirror.affects_navigation(mirror.signature_of(0x08020000)) is True    # NPC clip
    assert mirror.affects_navigation(mirror.signature_of(0x08000080)) is False   # sight only
    assert mirror.affects_navigation(mirror.signature_of(0x08002000)) is False   # pedestrian only
    assert mirror.affects_navigation(mirror.signature_of(0x1)) is True           # solid


# ---------------------------------------------------------------- against real brushes

@pytest.mark.skipif(not _have_install(), reason="VtMB install not configured")
def test_the_mirror_partitions_shipped_brushes_exactly_as_the_probe_does():
    """Two independent readers of the same masks, over every brush of both witnesses."""

    import contents_signatures as probe

    masks = probe.load_masks()
    for name in ("sp_tutorial_1", "sm_hub_1"):
        path = f"{probe.install.PATCH}/maps/{name}.bsp"
        for contents in probe.brush_contents(path):
            assert probe.signature(contents, masks) == mirror.signature_str(contents)


def _bits(mark: str) -> int:
    return sum(1 << index for index, letter in enumerate(LETTERS) if letter in mark)
