#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Senses10** — what the NPC perceives and who its enemy is.
//
// Every assertion is read off the decompiled C or the listing, and the address it came from is named
// beside it. The three corrections this family's reading made to the checklist's one-line walks are
// each pinned by a case: slot 594 reads `m_bEnemyWentOccluded` and not the ten-failure debounce,
// slot 573's two condition raises are gated on `bSetConditions` and not on a trace byte, and both
// `BestEnemy` distance keys are `__ftol` of the SUM OF SQUARES with no root.

static constexpr EAutomationTestFlags GElysiumNpcKernelSenses10Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Prefixed because the module builds adaptive-unity and this anonymous namespace is merged with
	// the other suites'.
	struct FSenses10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		FSenses10Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("senses10_kernel"), 909);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					FElysiumEntityDef& G = Builder.AddNpc(TEXT("guard"), FVector::ZeroVector,
						TEXT("npc_VHumanCombatant"));
					// `investigate_mode 6` (`Anything`) so `ShouldInvestigate` (`0x102b3270`) admits
					// and slot 472's outer gate is reachable at all — the authored default is
					// `Never`, which refuses the whole body.
					G.Keys.Add(TEXT("investigate_mode"), TEXT("6"));
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f),
						TEXT("npc_VHumanCombatant"));
					Builder.AddCounter(TEXT("unknowns"));
					Builder.WireOutput(TEXT("guard"), TEXT("OnUnknownVisionPlayer"),
						TEXT("unknowns"));
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
			FElysiumNpc::ResetSpeciesSuspectGlobals();
		}
	};

	float Senses10Cm(float Units) { return Units * ElysiumMove::U; }
}

// =================================================================================================
// Slot 201 `FVisible` — `0x102b4630`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10FVisibleTest,
	"Elysium.Substrate.NpcKernelSenses10.FVisible", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10FVisibleTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}
	F.Guard->Senses.Perception.VisionDistanceCm = Senses10Cm(1000.f);
	F.Guard->Senses.Perception.bResolved = true;

	// `102b4688` then `102b46b1`: with slot 594 admitting and no `Dominate_BrainWipe`, the answer is
	// the base trace, which a headless embodiment reports clear.
	TestTrue(TEXT("0x102b4630 admits a near, unconcealed target"),
		F.Guard->FVisible(F.Other, 0x2804091, nullptr, 0));

	// `102b4655`: a null target answers false and does NOT write the blocker. Retail's asymmetry.
	const int32 Before = F.Guard->FVisibleBlockerWrites;
	TestFalse(TEXT("a null target answers false"), F.Guard->FVisible(nullptr, 0x2804091, F.Other, 0));
	TestEqual(TEXT("...and does NOT write the blocker (102b4655)"),
		F.Guard->FVisibleBlockerWrites, Before);

	// `102b4688`: the Troika body hands slot 594 BOTH trailing arguments as `0`, so a blocker the
	// CALLER passed never reaches slot 594 and the range refusal cannot write it through this path.
	F.Guard->Senses.Perception.VisionDistanceCm = Senses10Cm(1.f);
	TestFalse(TEXT("beyond m_flSeekDistInspection slot 594 refuses"),
		F.Guard->FVisible(F.Other, 0x2804091, F.Other, 0));
	TestEqual(TEXT("...and 102b4688's forced zeros mean slot 594 writes no blocker"),
		F.Guard->FVisibleBlockerWrites, Before);

	// `102b4820` / `102b492a`: slot 594 DOES write it when a caller hands it a cell directly, which
	// `CNPC_VYukie::FVisible` (`0x103ddaf0`) is the one body in the census that does.
	TestFalse(TEXT("0x102b482e slot 594's range refusal writes the blocker when one is passed"),
		F.Guard->Slot594(F.Other, 0x2804091, F.Other, 0));
	TestEqual(TEXT("...once"), F.Guard->FVisibleBlockerWrites, Before + 1);
	TestFalse(TEXT("...and with no cell nothing is written"),
		F.Guard->Slot594(F.Other, 0x2804091, nullptr, 0));
	TestEqual(TEXT("...the counter is unchanged"), F.Guard->FVisibleBlockerWrites, Before + 1);

	// `102b4660`: the `npc_ignore_senses` arm DOES write it, and the null-target arm does not —
	// retail's asymmetry, on the one path where both are reachable from slot 201.
	TestFalse(TEXT("a null target still writes nothing"),
		F.Guard->FVisible(nullptr, 0x2804091, F.Other, 0));
	TestEqual(TEXT("...(102b4655)"), F.Guard->FVisibleBlockerWrites, Before + 1);
	return true;
}

// =================================================================================================
// Slot 594 — `0x102b4760`, the range/concealment test and the `+0x6081` far byte.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10Slot594Test,
	"Elysium.Substrate.NpcKernelSenses10.Slot594", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10Slot594Test::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}
	F.Guard->Senses.Perception.VisionDistanceCm = Senses10Cm(1000.f);
	F.Guard->Senses.Perception.bResolved = true;
	F.Guard->Senses.Memory.bPlayerInOuterBand = true;

	// `102b4770`: the byte is cleared FIRST, before anything is read — so a near target leaves it
	// clear whatever it was.
	F.Other->Origin = FVector(Senses10Cm(100.f), 0.0, 0.0);
	TestTrue(TEXT("0x102b4760 admits a target inside the radius"),
		F.Guard->Slot594(F.Other, 0x2804091, nullptr, 0));
	TestFalse(TEXT("...and 102b4770 cleared m_bSeenInOuterBand"),
		F.Guard->Senses.Memory.bPlayerInOuterBand);

	// `102b483e`: beyond `_DAT_10457f54` (0.7) of the radius the far byte is SET and the body still
	// answers true.
	F.Other->Origin = FVector(Senses10Cm(800.f), 0.0, 0.0);
	TestTrue(TEXT("a target in the outer band is still visible"),
		F.Guard->Slot594(F.Other, 0x2804091, nullptr, 0));
	TestTrue(TEXT("...and 102b4857 sets m_bSeenInOuterBand at 0.7x the radius"),
		F.Guard->Senses.Memory.bPlayerInOuterBand);

	// `102b4819`: past the radius the body refuses.
	F.Other->Origin = FVector(Senses10Cm(2000.f), 0.0, 0.0);
	TestFalse(TEXT("beyond the radius slot 594 refuses"),
		F.Guard->Slot594(F.Other, 0x2804091, nullptr, 0));

	// `102b4790`: **the correction.** The range block is skipped for a COMBAT body whose
	// `m_bEnemyWentOccluded` (`+0x5bc5`) is clear — NOT its ten-failure debounce (`bEnemyOccluded`),
	// which is what the port read. The fixture's guard is IDLE, so the combat half of the gate is
	// false either way; what this pins is that the DEBOUNCE alone changes nothing, which is exactly
	// what the old reader made it do.
	F.Guard->Senses.Memory.bEnemyOccluded = true;
	F.Guard->Senses.Memory.bEnemyWentOccluded = false;
	TestFalse(TEXT("0x102b479b the ten-failure debounce alone does not bypass the range block"),
		F.Guard->Slot594(F.Other, 0x2804091, nullptr, 0));
	F.Guard->Senses.Memory.bEnemyOccluded = false;

	// `102b47a9`: the other half of the same gate — `m_flStealthVisionOverrideTime` ABOVE curtime
	// skips the range block outright, and it is testable on an idle body.
	F.Guard->Senses.Memory.StealthVisionOverrideUntil = F.World.World.NowSeconds() + 100.0;
	TestTrue(TEXT("0x102b47b2 a live stealth-vision override skips the range block"),
		F.Guard->Slot594(F.Other, 0x2804091, nullptr, 0));
	TestFalse(TEXT("...and the far byte is NOT set on that path either (102b4770 is the only clear)"),
		F.Guard->Senses.Memory.bPlayerInOuterBand);
	F.Guard->Senses.Memory.StealthVisionOverrideUntil = -1.0;
	return true;
}

// =================================================================================================
// Slot 467 `QueryHearSound` — `0x102b35b0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10QueryHearSoundTest,
	"Elysium.Substrate.NpcKernelSenses10.QueryHearSound", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10QueryHearSoundTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	F.Guard->Senses.Perception.HearingScalar = 1.f;

	FElysiumGameSoundEvent Sound;
	Sound.TypeMask = ElysiumGameSounds::World;
	Sound.Source = F.Other->Handle;
	Sound.Position = F.Other->Origin;
	Sound.UnadjustedRadiusCm = Senses10Cm(1000.f);

	// `102b35bd`: a null `CSound*` answers false.
	TestFalse(TEXT("0x102b35bd a null sound answers false"), F.Guard->QueryHearSound(nullptr));

	// `102b3801`: everything that is not cowering or sleeping falls through to true — the ordinary
	// radius test is `CanHearSound`'s (`0x1030f7b0`), at the Listen level.
	TestTrue(TEXT("0x102b3801 an ordinary sound from a combat character is heard"),
		F.Guard->QueryHearSound(&Sound));

	// `102b3655`: my own sound is refused.
	FElysiumGameSoundEvent Mine = Sound;
	Mine.Source = F.Guard->Handle;
	TestFalse(TEXT("0x102b3655 my own sound is refused"), F.Guard->QueryHearSound(&Mine));

	// `102b368d`: an owner that resolves but is NOT a combat character refuses outright. The world
	// entity is exactly that shape.
	if (FElysiumEntity* WorldSpawn = F.World.World.FindByName(TEXT("world")))
	{
		FElysiumGameSoundEvent FromProp = Sound;
		FromProp.Source = WorldSpawn->Handle;
		TestFalse(TEXT("0x102b36c3 an owner with no CBaseCombatCharacter refuses"),
			F.Guard->QueryHearSound(&FromProp));
	}
	// A sound with NO owner handle skips that block entirely — which is how world sounds get through.
	FElysiumGameSoundEvent Ownerless = Sound;
	Ownerless.Source = FElysiumEntityHandle::Invalid();
	TestTrue(TEXT("...but an ownerless sound skips the block (102b368d)"),
		F.Guard->QueryHearSound(&Ownerless));

	// `102b361b`: the frenzy-friend veto — the arm the port did not have.
	F.Guard->FriendPlayer = F.Other->Handle;
	F.Guard->NpcFlags.SetFrenziedWord(FElysiumNpcFlags::FrenziedFriendPlayer);
	TestFalse(TEXT("0x102b3621 a frenzied body refuses its friend's sound"),
		F.Guard->QueryHearSound(&Sound));
	F.Guard->NpcFlags.SetFrenziedWord(0);
	TestTrue(TEXT("...and without the 0x800 bit it hears it again"),
		F.Guard->QueryHearSound(&Sound));
	F.Guard->FriendPlayer = FElysiumEntityHandle::Invalid();

	// `102b3734`: the distance arm runs ONLY for a cowering or sleeping body, and its limit is
	// `HearingSensitivity() * volume * 0.25` (`_DAT_1044bef8`).
	F.Other->Origin = FVector(Senses10Cm(500.f), 0.0, 0.0);
	Sound.Position = F.Other->Origin;
	TestTrue(TEXT("at 500 units an upright body still hears a 1000-unit sound"),
		F.Guard->QueryHearSound(&Sound));
	F.Guard->NpcFlags.Set(EElysiumNpcFlag::COWERING);
	TestFalse(TEXT("0x102b3795 a COWERING body applies the 0.25 scale and refuses it"),
		F.Guard->QueryHearSound(&Sound));
	F.Other->Origin = FVector(Senses10Cm(100.f), 0.0, 0.0);
	Sound.Position = F.Other->Origin;
	TestTrue(TEXT("...and inside 250 units it hears it"), F.Guard->QueryHearSound(&Sound));
	F.Guard->NpcFlags.Clear(EElysiumNpcFlag::COWERING);
	return true;
}

// =================================================================================================
// Slot 468 `QuerySeeEntity` — `0x102b38b0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10QuerySeeEntityTest,
	"Elysium.Substrate.NpcKernelSenses10.QuerySeeEntity", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10QuerySeeEntityTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	// `102b3930`: a player candidate answers TRUE unconditionally, before any relationship test —
	// with the guard holding no relation toward the player at all.
	TestTrue(TEXT("0x102b3930 a player is admitted before any relation test"),
		F.Guard->QuerySeeEntity(F.Player));

	// `102b393c`: a non-player needs D_HT or D_FR.
	TestFalse(TEXT("0x102b3942 a neutral non-player is refused"), F.Guard->QuerySeeEntity(F.Other));
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 5);
	TestTrue(TEXT("...D_HT admits it"), F.Guard->QuerySeeEntity(F.Other));
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Fear, 5);
	TestTrue(TEXT("...D_FR admits it"), F.Guard->QuerySeeEntity(F.Other));
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Like, 5);
	TestFalse(TEXT("...D_LI does not"), F.Guard->QuerySeeEntity(F.Other));

	// `102b38f1`: the frenzy-friend veto beats even the unconditional player arm.
	F.Guard->FriendPlayer = F.Player->Handle;
	F.Guard->NpcFlags.SetFrenziedWord(FElysiumNpcFlags::FrenziedFriendPlayer);
	TestFalse(TEXT("0x102b38f1 a frenzied body refuses its friend before the player arm"),
		F.Guard->QuerySeeEntity(F.Player));
	F.Guard->NpcFlags.SetFrenziedWord(0);
	TestTrue(TEXT("...and without the bit the player arm answers again"),
		F.Guard->QuerySeeEntity(F.Player));
	return true;
}

// =================================================================================================
// Slot 469 `OnLooked` — `0x102b39a0` — and the base body `0x1026a2c0` beneath it.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10OnLookedTest,
	"Elysium.Substrate.NpcKernelSenses10.OnLooked", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10OnLookedTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	// `102b39a0`, two statements: the base body first, then ONE increment while `COND_NEW_ENEMY`
	// (`0x54`) still stands after it.
	const int32 Before = F.Guard->EnemySightings;
	F.Guard->Cognition.Conditions.Clear(EElysiumNpcCond::NewEnemy);
	F.Guard->OnLooked(0);
	TestEqual(TEXT("0x102b39a0 without COND_NEW_ENEMY nothing is counted"),
		F.Guard->EnemySightings, Before);

	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	F.Guard->OnLooked(0);
	// The base body rebuilds the SEE family, and `GatherSight` does not clear `NEW_ENEMY`, so the
	// condition still stands when the increment is reached.
	TestEqual(TEXT("0x102b39b4 with COND_NEW_ENEMY m_iEnemySightings goes up by exactly one"),
		F.Guard->EnemySightings, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10BaseOnLookedTest,
	"Elysium.Substrate.NpcKernelSenses10.BaseOnLooked", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10BaseOnLookedTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	// The two gates story 29d added to `ElysiumNpcCond::GatherSight` (`0x1026a2c0`).
	F.Guard->Senses.Perception.VisionDistanceCm = Senses10Cm(4000.f);
	F.Guard->Senses.Perception.bResolved = true;
	F.Other->Origin = FVector(Senses10Cm(100.f), 0.0, 0.0);
	F.Guard->Senses.Memory.Enemy = F.Other->Handle;

	// `1026a3d1`: a NEUTRAL committed enemy raises no `SEE_ENEMY`, however plainly it is seen.
	FElysiumNpcConditions Out;
	ElysiumNpcCond::GatherSight(*F.Guard, 10.0, Out);
	TestFalse(TEXT("0x1026a3d1 a D_NU enemy raises no SEE_ENEMY"),
		Out.Has(EElysiumNpcCond::SeeEnemy));

	// `1026a3dd`: with a non-neutral relation the committed enemy raises it.
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 5);
	Out.Reset();
	F.Guard->Senses.TickSight(*F.Guard, 11.0);
	ElysiumNpcCond::GatherSight(*F.Guard, 11.0, Out);
	TestTrue(TEXT("0x1026a3dd a D_HT committed enemy raises SEE_ENEMY"),
		Out.Has(EElysiumNpcCond::SeeEnemy));
	TestTrue(TEXT("...and the D_HT priority arms still raise SEE_HATE"),
		Out.Has(EElysiumNpcCond::SeeHate));

	// `GatherCommittedEnemy` no longer raises it: the condition is `OnLooked`'s alone.
	FElysiumNpcConditions Committed;
	ElysiumNpcCond::GatherCommittedEnemy(*F.Guard, Committed);
	TestFalse(TEXT("GatherCommittedEnemy raises no SEE_ENEMY of its own"),
		Committed.Has(EElysiumNpcCond::SeeEnemy));
	TestTrue(TEXT("...but still answers the LOS debounce"),
		Committed.Has(EElysiumNpcCond::HaveEnemyLos)
			|| Committed.Has(EElysiumNpcCond::EnemyOccluded));
	return true;
}

// =================================================================================================
// Slot 472 `OnSeeEntity` — `0x102b3e00`. THE ORDER IS THE ROW.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10OnSeeEntityTest,
	"Elysium.Substrate.NpcKernelSenses10.OnSeeEntity", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10OnSeeEntityTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	FElysiumNpcMemory& Memory = F.Guard->Senses.Memory;
	// The outer gate: `NO_UNKNOWN_VISION` clear, `m_bSeenInOuterBand` SET, `ShouldInvestigate` true,
	// and inside it a player record in stealth posture.
	Memory.bPlayerInOuterBand = false;
	F.Guard->OnSeeEntity(F.Player);
	TestFalse(TEXT("0x102b3e0c with the far byte clear nothing is remembered"),
		Memory.BestSeeUnknown.IsSet());

	Memory.bPlayerInOuterBand = true;
	Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
	Memory.LastSeeUnknown = FElysiumEntityHandle::Invalid();
	Memory.SeeUnknownRepeatSightings = 7;
	const int32 SightingsBefore = F.Guard->EnemySightings;
	const float CounterBefore = F.World.Counter(TEXT("unknowns"));
	F.Guard->OnSeeEntity(F.Player);

	if (Memory.BestSeeUnknown == F.Player->Handle)
	{
		// `102b3e8c` .. `102b3f4d`, the new-unknown arm.
		TestEqual(TEXT("0x102b3e9d m_iEnemySightings goes up by one"),
			F.Guard->EnemySightings, SightingsBefore + 1);
		TestTrue(TEXT("0x102b3f0b m_hLastSeeUnknown follows m_hBestSeeUnknown"),
			Memory.LastSeeUnknown == F.Player->Handle);
		TestEqual(TEXT("0x102b3f4d the repeat counter is reset to zero"),
			Memory.SeeUnknownRepeatSightings, 0);
		TestTrue(TEXT("0x102b3f5a the run timer is armed 10..20 s ahead"),
			Memory.SeeUnknownRunTimer > 0.0);
		TestTrue(TEXT("0x102b3f8a the start timer is armed 5..10 s ahead"),
			Memory.SeeUnknownStartTimer > 0.0);
		// `102b3ee6`: the output fires FIRST, before those five writes. It fired at all is what the
		// counter proves; the ORDER is what the port had wrong and what the body now spells.
		TestTrue(TEXT("0x102b3ee6 OnUnknownVisionPlayer fired"),
			F.World.Counter(TEXT("unknowns")) > CounterBefore);

		// `102b3e6b`: a SECOND sighting of the same entity while it still holds
		// `m_hBestSeeUnknown` returns having written NOTHING — not even the tail's flag clear.
		const int32 SightingsAfter = F.Guard->EnemySightings;
		const float CounterAfter = F.World.Counter(TEXT("unknowns"));
		F.Guard->NpcFlags.Set(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
		F.Guard->OnSeeEntity(F.Player);
		TestEqual(TEXT("0x102b3e6b the same best-see-unknown writes nothing"),
			F.Guard->EnemySightings, SightingsAfter);
		TestEqual(TEXT("...and fires nothing"), F.World.Counter(TEXT("unknowns")), CounterAfter);
		TestTrue(TEXT("...and does not even clear LOOKED_AT_UNKNOWN"),
			F.Guard->NpcFlags.Has(EElysiumNpcFlag::LOOKED_AT_UNKNOWN));

		// `102b3ea5`: with `m_hBestSeeUnknown` released but `m_hLastSeeUnknown` still naming the
		// entity, the REPEAT arm runs: the counter goes up, two flags clear, and no output fires.
		Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
		Memory.SeeUnknownRepeatSightings = 0;
		F.Guard->NpcFlags.Set(EElysiumNpcFlag::IGNORE_UNKNOWN);
		F.Guard->NpcFlags.Set(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
		const float CounterBeforeRepeat = F.World.Counter(TEXT("unknowns"));
		F.Guard->OnSeeEntity(F.Player);
		TestEqual(TEXT("0x102b3eb9 the repeat arm counts one repeat sighting"),
			Memory.SeeUnknownRepeatSightings, 1);
		TestFalse(TEXT("0x102b3ecb the repeat arm clears IGNORE_UNKNOWN"),
			F.Guard->NpcFlags.Has(EElysiumNpcFlag::IGNORE_UNKNOWN));
		TestTrue(TEXT("...and returns BEFORE the tail that clears LOOKED_AT_UNKNOWN"),
			F.Guard->NpcFlags.Has(EElysiumNpcFlag::LOOKED_AT_UNKNOWN));
		TestEqual(TEXT("...and fires no output"),
			F.World.Counter(TEXT("unknowns")), CounterBeforeRepeat);
	}
	else
	{
		// The player is not in a stealth posture in this fixture, which is retail's refused-inside-
		// the-gate path: `ATTACK_UNKNOWN` is set and nothing is remembered.
		TestTrue(TEXT("0x102b3f9c the refused path sets ATTACK_UNKNOWN"),
			F.Guard->NpcFlags.Has(EElysiumNpcFlag::ATTACK_UNKNOWN));
	}

	// `102b3fa5`: the release arm only fires for the entity that HOLDS `m_hBestSeeUnknown`.
	Memory.BestSeeUnknown = F.Player->Handle;
	F.Guard->NpcFlags.Set(EElysiumNpcFlag::LOOKED_AT_UNKNOWN);
	Memory.bPlayerInOuterBand = false;
	F.Guard->OnSeeEntity(F.Other);
	TestTrue(TEXT("0x102b3fa5 another entity does not release the handle"),
		Memory.BestSeeUnknown == F.Player->Handle);
	F.Guard->OnSeeEntity(F.Player);
	TestFalse(TEXT("...but the holder does"), Memory.BestSeeUnknown.IsSet());
	TestFalse(TEXT("0x102b3fd4 and the shared tail clears LOOKED_AT_UNKNOWN"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::LOOKED_AT_UNKNOWN));
	return true;
}

// =================================================================================================
// Slot 478 `BestEnemy` — `0x102743c0`, and its three missing gates.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10BestEnemyTest,
	"Elysium.Substrate.NpcKernelSenses10.BestEnemy", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10BestEnemyTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	// `102743eb`: an empty memory list answers null immediately.
	TestNull(TEXT("0x102743fa an empty CAI_Memory answers null"), F.Guard->BestEnemy());

	// `10274483`: only D_HT and D_FR are eligible.
	F.Guard->EnemyMemory.Update(*F.Guard, F.Other->Handle, 1.0);
	TestNull(TEXT("0x1027448e a D_NU record is not a candidate"), F.Guard->BestEnemy());
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 5);
	TestEqual(TEXT("...and a D_HT record is"), F.Guard->BestEnemy(),
		static_cast<FElysiumEntity*>(F.Other));

	// `102744a7`: an eluded record is refused.
	F.Guard->EnemyMemory.MarkEluded(F.Other->Handle, true);
	TestNull(TEXT("0x102744bb HasEludedMe refuses the record"), F.Guard->BestEnemy());
	F.Guard->EnemyMemory.MarkEluded(F.Other->Handle, false);

	// `10274572` / `102744fe` / `102746d6`: slot 479 `IsValidEnemy` must pass. Its Troika-line body
	// is `return 1`, which is what makes every arm above reachable at all — asserted so the gate's
	// presence is pinned rather than assumed.
	TestTrue(TEXT("0x101a6820 slot 479's whole body is `return 1`"),
		F.Guard->IsValidEnemy(F.Other));

	// `10274469`: never self. The guard's own record is dropped even at D_HT.
	F.Guard->EnemyMemory.Update(*F.Guard, F.Guard->Handle, 1.0);
	F.Guard->Relationships.SetEntity(F.Guard->Handle, EElysiumRelationship::Hate, 99);
	TestEqual(TEXT("0x1027446b self is never a candidate however high its priority"),
		F.Guard->BestEnemy(), static_cast<FElysiumEntity*>(F.Other));

	// `10274475`: slot 158 `IsAlive` on the candidate.
	F.Other->bDead = true;
	TestNull(TEXT("0x1027447d a dead candidate is refused"), F.Guard->BestEnemy());
	F.Other->bDead = false;

	// `1027452b`: the distance key is `__ftol` of the SUM OF SQUARES — no root. At 400 units the key
	// is 160000, not 400. This is the correction to `CNPC_VFrenzyShadow`'s walk, and the base body
	// is where it is settled.
	F.Other->Origin = FVector(Senses10Cm(400.f), 0.0, 0.0);
	TestEqual(TEXT("0x10431320 is plain __ftol: the key is the SQUARED distance"),
		F.Guard->BestEnemyDistanceKey(*F.Other), 160000);

	// `10274483` for the player, `102744e6` for the priority ladder: a higher `IRelationPriority`
	// replaces the incumbent whatever the distance.
	F.Player->Origin = FVector(Senses10Cm(3000.f), 0.0, 0.0);
	F.Guard->EnemyMemory.Update(*F.Guard, F.Player->Handle, 1.0);
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 20);
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 1);
	TestEqual(TEXT("0x102744f1 a higher IRelationPriority wins whatever the distance"),
		F.Guard->BestEnemy(), static_cast<FElysiumEntity*>(F.Player));
	return true;
}

// =================================================================================================
// Slot 544 `UpdateEnemyMemory` — `0x102709c0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10UpdateEnemyMemoryTest,
	"Elysium.Substrate.NpcKernelSenses10.UpdateEnemyMemory", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10UpdateEnemyMemoryTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	// `10270a55`: the forward answers TRUE on the target's FIRST record and false on a refresh.
	TestTrue(TEXT("0x102709c0 the first record answers true"),
		F.Guard->UpdateEnemyMemory(F.Other, F.Other->Origin, nullptr));
	TestNotNull(TEXT("...and the record exists"), F.Guard->EnemyMemory.Find(F.Other->Handle));
	TestFalse(TEXT("...and a refresh answers false"),
		F.Guard->UpdateEnemyMemory(F.Other, F.Other->Origin, nullptr));

	// `102709df`: the SQUADMATE gate. `SquadWord()` is a seam answering 0 — retail's own "no squad"
	// value — so the gate's third term is false and the record is never refused. Asserted here so
	// the gate is on record as the admitting arm rather than as a silent refusal.
	TestEqual(TEXT("the squad word seam answers retail's no-squad value"),
		static_cast<int32>(F.Guard->SquadWord()), 0);
	TestEqual(TEXT("m_iSquadDisconnected ships at retail's connected value"),
		F.Guard->SquadDisconnected, 0);

	// `10270a2b`: an eluded record fires slot 494 `FoundEnemySound` before the forward.
	F.Guard->EnemyMemory.MarkEluded(F.Other->Handle, true);
	F.Guard->UpdateEnemyMemory(F.Other, F.Other->Origin, nullptr);
	TestTrue(TEXT("0x10270a3f an eluded record still forwards to CAI_Memory"),
		F.Guard->EnemyMemory.Find(F.Other->Handle) != nullptr);

	// A null enemy takes `UpdateMemory`'s position-only arm and answers false.
	TestFalse(TEXT("a null enemy answers false and records a position only"),
		F.Guard->UpdateEnemyMemory(nullptr, FVector(1.0, 2.0, 3.0), nullptr));
	return true;
}

// =================================================================================================
// Slot 402 `Event_Gibbed`, slot 223 `CreateVPhysics`, slot 538 `AimGun`, slot 445
// `StartTaskOverlay`, `0x1026ab50` `HeadProbe`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10BodyGatesTest,
	"Elysium.Substrate.NpcKernelSenses10.BodyGates", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10BodyGatesTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	// --- Slot 402 `Event_Gibbed` (`0x102658f0`) ---------------------------------------------------
	// Slot 394 `CorpseGib`'s answer IS this body's answer on every path, and slot 395 `CorpseFade`
	// fires on every path except the remove one.
	const int32 RemovesBefore = F.Guard->UtilRemoveCalls;
	const int32 BurstsBefore = F.Guard->SecondaryDiscParticleBursts;
	const bool bGib = F.Guard->CorpseGib();
	const bool bAnswer = F.Guard->Event_Gibbed();
	TestEqual(TEXT("0x102658f0 answers slot 394's own answer"), bAnswer, bGib);
	if (bGib && !F.Guard->HasExplosiveGibs())
	{
		TestEqual(TEXT("0x10265922 the no-explosive-gibs arm is the ONE that removes the body"),
			F.Guard->UtilRemoveCalls, RemovesBefore + 1);
		TestEqual(TEXT("...and it creates no disc particles"),
			F.Guard->SecondaryDiscParticleBursts, BurstsBefore);
	}

	// --- Slot 223 `CreateVPhysics` (`0x10273720`) -------------------------------------------------
	// Both gates are required before the shadow is built, and the slot answers TRUE unconditionally
	// — including when nothing was created.
	F.Guard->bHasPhysicsObject = true;
	F.Guard->VPhysicsShadow = FElysiumNpc::FVPhysicsShadowBuild();
	TestTrue(TEXT("0x10273720 answers true even when nothing is created"),
		F.Guard->CreateVPhysics());
	TestFalse(TEXT("...and an existing physics object blocks the build"),
		F.Guard->VPhysicsShadow.bBuilt);
	F.Guard->bHasPhysicsObject = false;
	TestTrue(TEXT("...and with no object and a live body it still answers true"),
		F.Guard->CreateVPhysics());
	if (F.Guard->GetMoveType() != 7)
	{
		TestTrue(TEXT("0x10272f40 builds the shadow"), F.Guard->VPhysicsShadow.bBuilt);
		// The mass default: `90` for a male body and `65` for a female one when the model's own
		// `mass` keyvalue is at or below `_DAT_104454c4` (0.0).
		TestEqual(TEXT("...with retail's gender mass default"), F.Guard->VPhysicsShadow.MassKg,
			F.Guard->IsFemaleBody() ? 65.f : 90.f);
	}

	// --- Slot 538 `AimGun` (`0x1026b4f0`) ---------------------------------------------------------
	// The whole body is gated on slot 167 `GetEnemy()` and does nothing without an enemy. No member
	// is written here, so "does nothing" is the assertion the row supports.
	F.Guard->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	TestNull(TEXT("0x1026b4fd with no enemy AimGun does nothing"), F.Guard->GetEnemy());
	F.Guard->AimGun();

	// --- Slot 445 `StartTaskOverlay` (`0x10288710`) -----------------------------------------------
	F.Guard->MoveAndShootOverlay = FElysiumNpc::FMoveAndShootOverlay();
	if (!F.Guard->IsCurTaskContinuousMove())
	{
		F.Guard->StartTaskOverlay();
		TestEqual(TEXT("0x10288721 a non-continuous task leaves the overlay untouched"),
			F.Guard->MoveAndShootOverlay.Disables, 0);
		TestEqual(TEXT("...and arms nothing"), F.Guard->MoveAndShootOverlay.Arms, 0);
	}
	// `0x102e8250` is the disable, and `FLT_MAX` is retail's own disabled value.
	F.Guard->DisableMoveAndShootOverlay();
	TestEqual(TEXT("0x102e8250 stores FLT_MAX into overlay+0x18"),
		F.Guard->MoveAndShootOverlay.NextShotTime, MAX_flt);
	TestEqual(TEXT("...and counts one disable"), F.Guard->MoveAndShootOverlay.Disables, 1);
	// `0x102e8270` falls back to that same disable for a body with no weapon.
	F.Guard->ArmMoveAndShootOverlay(1.f, 2.f);
	TestEqual(TEXT("0x102e8270 falls back to the disable with no active weapon"),
		F.Guard->MoveAndShootOverlay.Disables, 2);
	TestEqual(TEXT("...and arms nothing"), F.Guard->MoveAndShootOverlay.Arms, 0);

	// --- `0x1026ab50` `HeadProbe` -----------------------------------------------------------------
	// Gated on BOTH `+0x5f2d` and `+0x5f2c`; with the hull never shrunk it does nothing, which is
	// retail's own answer.
	F.Guard->bIsUsingSmallHull = false;
	F.Guard->bWantsLargeHull = false;
	F.Guard->bHasPhysicsObject = false;
	F.Guard->VPhysicsShadow = FElysiumNpc::FVPhysicsShadowBuild();
	F.Guard->HeadProbe();
	TestFalse(TEXT("0x1026ab5a with the hull latches clear the probe does nothing"),
		F.Guard->VPhysicsShadow.bBuilt);
	// With both latches set the clean-trace tail restores the normal hull, which clears `+0x5f2d`.
	F.Guard->bIsUsingSmallHull = true;
	F.Guard->bWantsLargeHull = true;
	F.Guard->HeadProbe();
	TestFalse(TEXT("0x10273070 the clean-trace tail clears m_fIsUsingSmallHull"),
		F.Guard->bIsUsingSmallHull);
	return true;
}

// =================================================================================================
// Slots 562 / 573 / 574 — the weapon-LOS pair and the shoot direction.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10WeaponLosTest,
	"Elysium.Substrate.NpcKernelSenses10.WeaponLos", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10WeaponLosTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	const FVector OwnerCm = F.Guard->Origin;
	const FVector TargetCm = FVector(Senses10Cm(1000.f), 0.0, 0.0);

	// --- Slot 573 `InnateWeaponLOSCondition` (`0x1026fcf0`) ----------------------------------------
	// `1026fdbd`: a clear trace (`fraction == _DAT_10449280`, a DOUBLE 1.0) answers TRUE.
	TestTrue(TEXT("0x1026fdc9 a clear trace answers true"),
		F.Guard->InnateWeaponLOSCondition(OwnerCm, TargetCm, false));
	// **The correction**: `1026fe25` and `1026fe51` read `[ESP+0xb8]`, the THIRD ARGUMENT
	// `bSetConditions` — not a trace byte. With a clear trace neither raise is reached, so the
	// condition set is untouched either way.
	F.Guard->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x66));
	F.Guard->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x63));
	F.Guard->InnateWeaponLOSCondition(OwnerCm, TargetCm, true);
	TestFalse(TEXT("0x1026fe67 a clear trace raises no COND 0x66"),
		F.Guard->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x66)));
	TestFalse(TEXT("...and no COND 0x63"),
		F.Guard->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x63)));

	// --- `0x10266b10`, the player-in-line-of-fire cone -------------------------------------------
	// `10266bd4`: `dot > 0.92` AND `distToTarget > distToPlayer`, both STRICT.
	F.Player->Origin = FVector(Senses10Cm(500.f), 0.0, 0.0);
	TestTrue(TEXT("0x10266b10 a player directly between owner and target is in the line of fire"),
		F.Guard->PlayerInLineOfFire(OwnerCm, TargetCm));
	// Behind the target: closer test fails.
	F.Player->Origin = FVector(Senses10Cm(2000.f), 0.0, 0.0);
	TestFalse(TEXT("...a player BEYOND the target is not (the distance term is strict)"),
		F.Guard->PlayerInLineOfFire(OwnerCm, TargetCm));
	// Off to the side: the 0.92 cosine fails.
	F.Player->Origin = FVector(Senses10Cm(100.f), Senses10Cm(500.f), 0.0);
	TestFalse(TEXT("...and a player off the 0.92 cone is not"),
		F.Guard->PlayerInLineOfFire(OwnerCm, TargetCm));

	// --- Slot 562 `WeaponLOSCondition` (`0x1026fbe0`) ---------------------------------------------
	// `1026fc00`: with no weapon and no `0x20000` capability the answer is 0, and only under
	// `bSetConditions` is COND `0x42` raised.
	F.Guard->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x42));
	if ((F.Guard->CapabilitiesGet() & 0x20000) == 0 && !F.Guard->Inventory.ActiveWeapon.IsSet())
	{
		TestFalse(TEXT("0x1026fc0e no weapon and no innate capability answers false"),
			F.Guard->WeaponLOSCondition(OwnerCm, TargetCm, false));
		TestFalse(TEXT("...and without bSetConditions raises nothing"),
			F.Guard->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x42)));
		F.Guard->WeaponLOSCondition(OwnerCm, TargetCm, true);
		TestTrue(TEXT("0x1026fc26 ...and with it raises COND 0x42"),
			F.Guard->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x42)));
	}

	// --- Slot 574 `GetShootEnemyDir` (`0x10278900`) ----------------------------------------------
	// `10278959`: the slot answers a UNIT direction — the listing stores the three components AFTER
	// `VectorNormalize`, which the decompiled C hides.
	F.Guard->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	F.Guard->ShootTargetOverride = F.Player->Handle;
	F.Player->Origin = FVector(Senses10Cm(300.f), Senses10Cm(400.f), 0.0);
	const FVector Direction = F.Guard->GetShootEnemyDir(FVector::ZeroVector, 0, 0);
	TestTrue(TEXT("0x10278969 the slot answers a UNIT direction"),
		FMath::IsNearlyEqual(static_cast<float>(Direction.Size()), 1.f, 1.e-3f));
	TestTrue(TEXT("...pointing at the shoot-target override (0x10278654)"),
		Direction.X > 0.0 && Direction.Y > 0.0);
	return true;
}

// =================================================================================================
// The species arms of slots 201, 363, 472, 478 and 574.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10SpeciesArmsTest,
	"Elysium.Substrate.NpcKernelSenses10.SpeciesArms", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10SpeciesArmsTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	F.Guard->Senses.Perception.VisionDistanceCm = Senses10Cm(4000.f);
	F.Guard->Senses.Perception.bResolved = true;

	// The census IS the dispatcher: each arm is proved by the address the census says fills the slot
	// for that retail class.
	TestEqual(TEXT("the census puts 0x10369ff0 on CNPC_VCameraSecurity#201"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity")), 201)),
		FString(TEXT("0x10369ff0")));
	TestEqual(TEXT("...0x103ba290 on CNPC_VTzimisce#201"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisce")), 201)),
		FString(TEXT("0x103ba290")));
	TestEqual(TEXT("...0x103e0bc0 on CNPC_VZombie#201"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VZombie")), 201)),
		FString(TEXT("0x103e0bc0")));
	TestEqual(TEXT("...0x10371ae0 on CNPC_VCop#472"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VCop")), 472)),
		FString(TEXT("0x10371ae0")));
	TestEqual(TEXT("...0x103887d0 on CNPC_VHunter#472"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VHunter")), 472)),
		FString(TEXT("0x103887d0")));
	TestEqual(TEXT("...0x103766d0 on CNPC_VFrenzyShadow#478"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VFrenzyShadow")), 478)),
		FString(TEXT("0x103766d0")));
	TestEqual(TEXT("...0x10395d00 on CNPC_VMingXiao#574"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VMingXiao")), 574)),
		FString(TEXT("0x10395d00")));
	TestEqual(TEXT("...0x10369fb0 on CNPC_VCameraSecurity#363"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity")), 363)),
		FString(TEXT("0x10369fb0")));

	// A plain `npc_VCop` has a NULL census classname list, so `RetailClass()` is null and every
	// species lookup correctly falls through to the Troika line. Story 29c-1's cleanup recorded it;
	// this family relies on it at five dispatchers.
	TestNull(TEXT("a spawned npc_VCop resolves to no retail class"),
		ElysiumNpcKernelClass::OfClassname(TEXT("npc_VCop")));

	// --- `CNPC_VCameraSecurity#201` (`0x10369ff0`) and `#363` (`0x10369fb0`) ----------------------
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VCameraSecurity"));
	TestFalse(TEXT("0x10369ff0 a security NPC with no camera link sees nothing"),
		F.Guard->FVisible(F.Player, 0x2804091, nullptr, 0));
	TestFalse(TEXT("...not even a plainly-visible NPC (its own eyes are never consulted)"),
		F.Guard->FVisible(F.Other, 0x2804091, nullptr, 0));
	TestFalse(TEXT("0x10369fb0 and its cone answers the camera's, which is false with no link"),
		F.Guard->CameraSecurityFInViewCone(F.Player));

	// --- `CNPC_VTzimisce#201` (`0x103ba290`) ------------------------------------------------------
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VTzimisce"));
	F.Other->Origin = FVector(Senses10Cm(100.f), 0.0, 0.0);
	TestTrue(TEXT("0x103ba290 forwards to the Troika base with the fourth argument clamped"),
		F.Guard->FVisible(F.Other, 0x2804091, nullptr, 12345));

	// --- `CNPC_VZombie#201` (`0x103e0bc0`) --------------------------------------------------------
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VZombie"));
	F.Guard->Senses.Memory.Enemy = F.Other->Handle;
	TestTrue(TEXT("0x103e0bef the zombie's own enemy is answered by the obfuscate test, not sight"),
		F.Guard->FVisible(F.Other, 0x2804091, nullptr, 0));
	// A DIFFERENT entity falls through to the base, which at 4000 units is out of range.
	F.Player->Origin = FVector(Senses10Cm(9000.f), 0.0, 0.0);
	TestFalse(TEXT("0x103e0c2e every other entity takes the Troika base"),
		F.Guard->FVisible(F.Player, 0x2804091, nullptr, 0));

	// --- `CNPC_VCop#472` (`0x10371ae0`) and `CNPC_VHunter#472` (`0x103887d0`) ---------------------
	FElysiumNpc::ResetSpeciesSuspectGlobals();
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VCop"));
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);
	F.Guard->Senses.Memory.bPlayerInOuterBand = false;
	F.Guard->OnSeeEntity(F.Player);
	TestTrue(TEXT("0x10370560 the cop stamps DAT_1093ac3c with the hated player"),
		FElysiumNpc::CopSuspectHandle() == F.Player->Handle);
	TestTrue(TEXT("...and _DAT_1093aca8 is curtime + 30.0"),
		FElysiumNpc::CopSuspectExpiry() > 0.0);
	// The cop's stamp is guarded on the seen entity carrying a player record; the hunter's is NOT.
	FElysiumNpc::ResetSpeciesSuspectGlobals();
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 5);
	F.Guard->OnSeeEntity(F.Other);
	TestFalse(TEXT("0x10370560 the cop's stamp needs a +0xa8 player record"),
		FElysiumNpc::CopSuspectHandle().IsSet());
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VHunter"));
	F.Guard->OnSeeEntity(F.Other);
	TestTrue(TEXT("0x10387fd0 the hunter's twin has NO such guard"),
		FElysiumNpc::HunterSuspectHandle() == F.Other->Handle);
	// `10371aea`: with the far byte SET neither stamps.
	FElysiumNpc::ResetSpeciesSuspectGlobals();
	F.Guard->Senses.Memory.bPlayerInOuterBand = true;
	F.Guard->OnSeeEntity(F.Other);
	TestFalse(TEXT("0x103887d4 the far byte set skips the stamp on both twins"),
		FElysiumNpc::HunterSuspectHandle().IsSet());
	F.Guard->Senses.Memory.bPlayerInOuterBand = false;

	// --- `CNPC_VMingXiao#574` (`0x10395d00`) ------------------------------------------------------
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VMingXiao"));
	F.Guard->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	F.Guard->ShootTargetOverride = F.Other->Handle;
	F.Other->Origin = FVector(Senses10Cm(400.f), 0.0, 0.0);
	const FVector MingXiao = F.Guard->GetShootEnemyDir(FVector::ZeroVector, 0, 0);
	TestTrue(TEXT("0x10395d1c Ming Xiao's arm is still a unit direction"),
		FMath::IsNearlyEqual(static_cast<float>(MingXiao.Size()), 1.f, 1.e-3f));
	TestTrue(TEXT("...and _DAT_1044eb0c raises its Z above the base body's"), MingXiao.Z > 0.0);
	F.Guard->SetRetailClassForTests(nullptr);

	// --- `CNPC_VFrenzyShadow#478` (`0x103766d0`) --------------------------------------------------
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VFrenzyShadow"));
	F.Guard->EnemyMemory.Update(*F.Guard, F.Other->Handle, 1.0);
	F.Guard->FrenzyShadowHostileEnemyCount = 0;
	F.Guard->bFrenzyShadowFailedGrapple = true;
	FElysiumEntity* Chosen = F.Guard->BestEnemy();
	TestEqual(TEXT("0x103766d0 the score rescan picks the one eligible candidate"),
		Chosen, static_cast<FElysiumEntity*>(F.Other));
	TestFalse(TEXT("0x103769d9 a winner that differs from GetEnemy clears m_bFailedGrapple"),
		F.Guard->bFrenzyShadowFailedGrapple);
	F.Guard->SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// `CNPC_VScurrying` — `0x103acac0` and `0x103acba0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10ScurryingTest,
	"Elysium.Substrate.NpcKernelSenses10.Scurrying", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10ScurryingTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no fixture"));
		return false;
	}
	// `103acac0`: a null target answers false.
	TestFalse(TEXT("0x103acac4 a null target answers false"),
		F.Guard->ScurryingShouldDetect(nullptr));

	// `103acad4`: the distance must be STRICTLY below `m_flDetectionDistance`.
	F.Guard->ScurryingDetectionDistanceUnits = 500.f;
	F.Other->Origin = FVector(Senses10Cm(100.f), 0.0, 0.0);
	TestTrue(TEXT("0x103acb0a inside m_flDetectionDistance a target is detected"),
		F.Guard->ScurryingShouldDetect(F.Other));
	F.Other->Origin = FVector(Senses10Cm(600.f), 0.0, 0.0);
	TestFalse(TEXT("...and outside it is not"), F.Guard->ScurryingShouldDetect(F.Other));
	F.Other->Origin = FVector(Senses10Cm(500.f), 0.0, 0.0);
	TestFalse(TEXT("...and the compare is STRICT, so exactly at the distance is not"),
		F.Guard->ScurryingShouldDetect(F.Other));

	// `103acb3c`: `m_fMustDetect` rejects a PLAYER target unless COND 0x5a or 0x6f stands, and
	// admits any non-player unconditionally.
	F.Guard->bScurryingMustDetect = true;
	F.Player->Origin = FVector(Senses10Cm(100.f), 0.0, 0.0);
	F.Guard->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x5a));
	F.Guard->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x6f));
	TestFalse(TEXT("0x103ad0a0 m_fMustDetect refuses an unseen player"),
		F.Guard->ScurryingShouldDetect(F.Player));
	F.Guard->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x5a));
	TestTrue(TEXT("...and admits one under COND 0x5a SEE_PLAYER"),
		F.Guard->ScurryingShouldDetect(F.Player));
	F.Other->Origin = FVector(Senses10Cm(100.f), 0.0, 0.0);
	TestTrue(TEXT("...and admits any non-player whatever the conditions"),
		F.Guard->ScurryingShouldDetect(F.Other));

	// `103acbb0` / `103acbd0`: with no AI network the node search fails and the MARCH is the arm
	// taken — which still produces a destination, away from the threat.
	FVector Destination = FVector::ZeroVector;
	const FVector ThreatCm = FVector(Senses10Cm(100.f), 0.0, 0.0);
	TestTrue(TEXT("0x103acbd0 the march answers a destination on a clear trace"),
		F.Guard->ScurryingFindFleeDestination(ThreatCm, 300.f, &Destination));
	TestTrue(TEXT("...and it leads AWAY from the threat"), Destination.X < 0.0);
	// `103acd34`: with no out-vector the body still answers 1.
	TestTrue(TEXT("...and with no out-vector it still answers 1"),
		F.Guard->ScurryingFindFleeDestination(ThreatCm, 300.f, nullptr));
	// `103acd0c`: a distance at or below `_DAT_104454c0` (1.0) gives up at once on a blocked trace.
	return true;
}

// =================================================================================================
// `CNPC_VWerewolf` — the hint bodies and `CheckStuck`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSenses10WerewolfTest,
	"Elysium.Substrate.NpcKernelSenses10.Werewolf", GElysiumNpcKernelSenses10Flags)
bool FElysiumNpcKernelSenses10WerewolfTest::RunTest(const FString&)
{
	FSenses10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	// Slot 566 `FValidateHintType` is the first gate of both hint twins, so the fixture has to BE a
	// werewolf for either body to reach its own type ladder: `CNPC_VWerewolf`'s row (`0x103d7ce0`)
	// accepts 15000..15018 except 15007, which is where every type below lives.
	F.Guard->SetRetailClassForTests(TEXT("CNPC_VWerewolf"));

	FElysiumNpc::FHintWords Hint;
	Hint.bValid = true;
	Hint.HintIndex = 7;
	Hint.HintType = 0x3aa8;
	Hint.OriginCm = FVector(Senses10Cm(200.f), 0.0, 0.0);
	Hint.Angles = FVector(0.0, 45.0, 0.0);

	// --- `0x103d68d0` `GetHintTargetGroundpoint` --------------------------------------------------
	// The hit answers the row's `+0x14` — the TARGET groundpoint, not the hint's own `+0x08`.
	F.Guard->WerewolfHintGroundpoints.Reset();
	FElysiumNpc::FWerewolfHintGroundpoint Row;
	Row.HintNode = 7;
	Row.GroundpointUnits = FVector(11.0, 22.0, 33.0);
	F.Guard->WerewolfHintGroundpoints.Add(Row);
	TestEqual(TEXT("0x103d6939 the scan answers the row's target groundpoint"),
		F.Guard->GetHintTargetGroundpoint(Hint), FVector(11.0, 22.0, 33.0));
	// `103d698c`: a MISS still answers a point — the `GetGroundpoint` fallback, not a refusal.
	FElysiumNpc::FHintWords Missing = Hint;
	Missing.HintIndex = 99;
	const FVector Fallback = F.Guard->GetHintTargetGroundpoint(Missing);
	TestFalse(TEXT("0x103d69ad a miss still answers a point rather than refusing"),
		Fallback.ContainsNaN());

	// --- `0x103d7210` `GetForwardYawForHint` ------------------------------------------------------
	// **Read off the listing**: the tail is a single-step WRAP against `_DAT_10450568` (360.0), not
	// a selection — `103d7347` subtracts above it and `103d737a` adds below zero.
	const float Yaw = F.Guard->GetForwardYawForHint(Hint);
	TestTrue(TEXT("0x103d7347 the answer is wrapped into [0, 360]"), Yaw >= 0.f && Yaw <= 360.f);
	// `103d7328`: types `0x3aa3` and `0x3aa5` DISCARD the computed yaw and take the hint's own.
	FElysiumNpc::FHintWords OwnYaw = Hint;
	OwnYaw.HintType = 0x3aa3;
	TestEqual(TEXT("0x103d7328 type 0x3aa3 takes the hint's own slot-219 yaw"),
		F.Guard->GetForwardYawForHint(OwnYaw), 45.f);
	OwnYaw.HintType = 0x3aa5;
	TestEqual(TEXT("...and so does 0x3aa5"), F.Guard->GetForwardYawForHint(OwnYaw), 45.f);

	// --- `0x103d7710` `InitializeHintData` --------------------------------------------------------
	// The build runs only while the count is zero, and the exponent test is what decides a fallback.
	// `FLT_MAX` (`vec3_invalid`) PASSES the `0x7f800000` test and is stored — the surprising half.
	TestTrue(TEXT("0x103d7a1e FLT_MAX passes the 0x7f800000 exponent test"),
		FElysiumNpc::IsGroundpointExponentValid(FVector(MAX_flt, MAX_flt, MAX_flt)));
	TestFalse(TEXT("...and an infinity does not"),
		FElysiumNpc::IsGroundpointExponentValid(
			FVector(std::numeric_limits<double>::infinity(), 0.0, 0.0)));
	const FElysiumNpc::FWerewolfHintGroundpoint Built = F.Guard->InitializeHintDataRow(Hint);
	TestEqual(TEXT("0x103d7817 the row carries the hint it was built from"), Built.HintNode, 7);
	F.Guard->WerewolfHintGroundpoints.Reset();
	F.Guard->InitializeHintData();
	TestEqual(TEXT("0x103d77fa with no hint chain the array stays empty"),
		F.Guard->WerewolfHintGroundpoints.Num(), 0);
	F.Guard->WerewolfHintGroundpoints.Add(Row);
	F.Guard->InitializeHintData();
	TestEqual(TEXT("0x103d77e8 and a non-zero count makes the build a no-op"),
		F.Guard->WerewolfHintGroundpoints.Num(), 1);

	// --- `0x103d7dc0` / `0x103d8060`, the two twins -----------------------------------------------
	// `103d7dfa`: an INVALID hint is false for both.
	FElysiumNpc::FHintWords Invalid;
	TestFalse(TEXT("0x103d7dfa a null hint is false"),
		F.Guard->IsValidRandomMoveHint(Invalid, 10.0));
	TestFalse(TEXT("0x103d8113 ...for the move twin too"), F.Guard->IsValidMoveHint(Invalid, 10.0));

	// `103d7f4c`: the random-move twin's always-false set — and note `0x3aa3` is IN it, where the
	// move twin refuses `0x3aa3` and `0x3aa9` by name. The two sets differ in membership AND sense.
	for (const int32 Type : { 0x3aa7, 0x3aa5, 15000, 0x3aa3 })
	{
		FElysiumNpc::FHintWords Typed = Hint;
		Typed.HintType = Type;
		TestFalse(*FString::Printf(TEXT("0x103d7f4c random-move type 0x%x is always false"), Type),
			F.Guard->IsValidRandomMoveHint(Typed, 10.0));
	}
	// `103d820c`: `0x3aa5` on the MOVE twin needs `m_DoorState` to be exactly 2 — so it is NOT
	// always false there.
	FElysiumNpc::FHintWords DoorHint = Hint;
	DoorHint.HintType = 0x3aa5;
	F.Guard->WerewolfDoorState = 1;
	TestFalse(TEXT("0x103d820c the move twin refuses 0x3aa5 with m_DoorState != 2"),
		F.Guard->IsValidMoveHint(DoorHint, 10.0));
	F.Guard->WerewolfDoorState = 2;
	TestTrue(TEXT("...and admits it at exactly 2"), F.Guard->IsValidMoveHint(DoorHint, 10.0));

	// `103d81a5`: the move twin's `0x3aa8` needs `HasCondition(0x77)` SET; the random-move twin's
	// does not.
	FElysiumNpc::FHintWords JumpHint = Hint;
	JumpHint.HintType = 0x3aa8;
	F.Guard->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x77));
	TestFalse(TEXT("0x103d81b2 the move twin refuses 0x3aa8 without COND 0x77"),
		F.Guard->IsValidMoveHint(JumpHint, 10.0));

	// `103d7ea8`: the random-move twin's `0x3a9a` group is true while the last-seen elapsed time is
	// below `_DAT_10452dc4` (2.0 s).
	FElysiumNpc::FHintWords RandomHint = Hint;
	RandomHint.HintType = 0x3a9a;
	F.Guard->WerewolfLastSeenTime = 10.0;
	TestTrue(TEXT("0x103d7ed8 within 2.0 s of the last sighting the hint is valid"),
		F.Guard->IsValidRandomMoveHint(RandomHint, 11.0));
	TestFalse(TEXT("...and past it the node-zone arm refuses with no node graph"),
		F.Guard->IsValidRandomMoveHint(RandomHint, 20.0));

	// --- `0x103cb920` `CheckStuck` ----------------------------------------------------------------
	// Every exit but the teleport ends in `SetHullSizeSmall(1)`, and with the probes reporting
	// CLEAR the body takes the not-stuck arm.
	F.Guard->bIsUsingSmallHull = false;
	F.Guard->WerewolfTeleportOutCalls = 0;
	F.Guard->WerewolfCheckStuck();
	TestTrue(TEXT("0x103cbf1c every non-teleport exit ends in SetHullSizeSmall(1)"),
		F.Guard->bIsUsingSmallHull);
	TestEqual(TEXT("...and a clear probe does not teleport"), F.Guard->WerewolfTeleportOutCalls, 0);
	F.Guard->SetRetailClassForTests(nullptr);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
