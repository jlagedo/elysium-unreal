// The see-unknown sweep, `CAI_BaseNPCTroika::FUN_102b15c0` (`ElysiumNpcCond::GatherSeeUnknown`):
// the "stopped seeing it" grace and `LOST_UNKNOWN`, the player-only gate, the two one-shot rolls
// (`ATTACK_UNKNOWN` / `IGNORE_UNKNOWN`), and the 2-D closing-speed classification with its mid-sweep
// `SEE_UNKNOWN` retraction.
//
// `SEE_UNKNOWN` itself is written by `TickSight`'s outer-band admission (story 6a) and read by
// `GatherSight`; that cohort lives in `ElysiumNpcSensesTests.cpp`. What is under test here is what
// this sweep does with the memory record and the raised condition, which is the seam 10b adds.
//
// `docs/vtmb/npc-ai-reverse-engineering.md` -> "The three `GatherConditions` sweeps and the
// interest predicate" owns every fact here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"          // ElysiumMove::U — the velocity is cm/s, the threshold Source units
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumNpcSeeUnknownSweepTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ECond = EElysiumNpcCond;
using EMode = EElysiumInvestigateMode;
using EFlag = EElysiumNpcFlag;

namespace
{
	// The sweep reads flags, memory, the player's posture and velocity, and the running program's
	// mask. It never touches a body, so nothing here needs a model.
	struct FSweepFixture
	{
		FElysiumNpcWorldFixture Fixture;
		FElysiumEntityWorld& World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Bystander = nullptr;   // a non-player candidate, for the player-only gate
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__see_unknown_sweep_test__"), 0x53455553);
			Builder.AddNpc(TEXT("npc"));
			Builder.AddNpc(TEXT("bystander"));
			return Builder;
		}

		FSweepFixture()
			: Fixture(BuildWorld())
			, World(Fixture.World)
		{
			Npc = Fixture.Npc(TEXT("npc"));
			Bystander = Fixture.Npc(TEXT("bystander"));
			Player = Fixture.Player();
			if (Npc != nullptr)
			{
				Npc->Origin = FVector::ZeroVector;
				Npc->InvestigateMode = static_cast<int32>(EMode::Anything);
				Npc->InvestigateModeCombat = static_cast<int32>(EMode::Anything);
			}
			if (Player != nullptr)
			{
				Player->Origin = FVector::ZeroVector;
				Player->Velocity = FVector::ZeroVector;
			}
		}

		// Put the NPC on a running program whose authored interrupt mask is `Mask`.
		TUniquePtr<ElysiumSchedule::FInterruptMaskScope> RunWithMask(const FElysiumNpcConditions& Mask)
		{
			auto Scope = MakeUnique<ElysiumSchedule::FInterruptMaskScope>(
				EElysiumScheduleId::IdleDisposition, Mask);
			ElysiumSchedule::Start(Npc->Schedule, EElysiumScheduleId::IdleDisposition, *Npc);
			return Scope;
		}

		// Forces `IsInStealthPosture()` true through the grapple clause -- the one route settable
		// with no embodiment and no discipline state in a headless fixture.
		void MakePlayerStealthy(bool bStealthy) const
		{
			if (bStealthy)
			{
				Player->Grapple.Partner = Bystander->Handle;
				Player->Grapple.Role = EElysiumGrappleRole::Victim;
				Player->Grapple.Type = EElysiumGrappleType::StealthKill;
			}
			else
			{
				Player->Grapple = FElysiumGrappleState();
			}
		}

		FElysiumNpcConditions Sweep(const FElysiumNpcConditions& Raised, double Now = 0.0) const
		{
			FElysiumNpcConditions Out = Raised;
			ElysiumNpcCond::GatherSeeUnknown(*Npc, Now, Out);
			return Out;
		}
	};
}

// --- No candidate, a stale handle, and the 1.5 s grace -------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSeeUnknownSweepLostTest,
	"Elysium.Substrate.NpcConditions.SeeUnknownSweepLost", GElysiumTestFlags)
bool FElysiumNpcSeeUnknownSweepLostTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("bystander"), F.Bystander))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;
	const FElysiumNpcConditions LostMask = FElysiumNpcConditions::Of({ ECond::LostUnknown });

	// --- Nothing tracked: LOST_UNKNOWN fires immediately, gated by the mask --------------------
	{
		auto Scope = F.RunWithMask(LostMask);
		Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
		TestTrue(TEXT("no best-see-unknown and the mask lists it raises LOST_UNKNOWN"),
			F.Sweep(FElysiumNpcConditions()).Has(ECond::LostUnknown));
	}
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		Memory.BestSeeUnknown = FElysiumEntityHandle::Invalid();
		TestFalse(TEXT("...but not without the mask"),
			F.Sweep(FElysiumNpcConditions()).Has(ECond::LostUnknown));
	}

	// --- A stale handle (the entity is gone) is the same immediate answer ----------------------
	{
		auto Scope = F.RunWithMask(LostMask);
		Memory.BestSeeUnknown = F.Bystander->Handle;
		Memory.BestSeeUnknown.Epoch += 1;
		Memory.SeeUnknownGraceUntil = -1.0;
		TestTrue(TEXT("a handle that no longer resolves skips the grace entirely"),
			F.Sweep(FElysiumNpcConditions()).Has(ECond::LostUnknown));
	}

	// --- A live handle arms the grace, then holds, then elapses ---------------------------------
	{
		auto Scope = F.RunWithMask(LostMask);
		Memory.BestSeeUnknown = F.Bystander->Handle;
		Memory.LastSeeUnknown = F.Bystander->Handle;
		F.Bystander->Origin = FVector(50.0, 0.0, 0.0);
		Memory.SeeUnknownGraceUntil = -1.0;

		const FElysiumNpcConditions Armed = F.Sweep(FElysiumNpcConditions(), 10.0);
		TestFalse(TEXT("the first miss arms the grace and answers nothing"),
			Armed.Has(ECond::LostUnknown));
		TestTrue(TEXT("...at curtime + 1.5"),
			FMath::IsNearlyEqual(Memory.SeeUnknownGraceUntil, 11.5));
		TestTrue(TEXT("the handle is still tracked while the grace holds"), Memory.BestSeeUnknown.IsSet());

		TestFalse(TEXT("still within the grace, nothing fires"),
			F.Sweep(FElysiumNpcConditions(), 11.4).Has(ECond::LostUnknown));
		TestTrue(TEXT("...and the handle is untouched"), Memory.BestSeeUnknown.IsSet());

		TestTrue(TEXT("at the deadline LOST_UNKNOWN fires"),
			F.Sweep(FElysiumNpcConditions(), 11.5).Has(ECond::LostUnknown));
		TestFalse(TEXT("...and the tracked handle drops"), Memory.BestSeeUnknown.IsSet());
		TestTrue(TEXT("...remembering the last-seen-unknown's position"),
			Memory.LastSeeUnknownPosition.Equals(FVector(50.0, 0.0, 0.0)));
	}
	return true;
}

// --- Player-only by construction, and the grace sentinel resets on a fresh sighting ---------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSeeUnknownSweepPlayerOnlyTest,
	"Elysium.Substrate.NpcConditions.SeeUnknownSweepPlayerOnly", GElysiumTestFlags)
bool FElysiumNpcSeeUnknownSweepPlayerOnlyTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("bystander"), F.Bystander))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;
	auto Scope = F.RunWithMask(FElysiumNpcConditions());

	Memory.BestSeeUnknown = F.Bystander->Handle;
	Memory.SeeUnknownGraceUntil = 999.0;   // proves the sentinel resets even when nothing classifies
	const FElysiumNpcConditions Out =
		F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 5.0);
	TestFalse(TEXT("a non-player best-see-unknown raises nothing"), Out.Has(ECond::InvestigateSight));
	TestTrue(TEXT("SEE_UNKNOWN itself is left standing -- only the ignore arm retracts it"),
		Out.Has(ECond::SeeUnknown));
	TestTrue(TEXT("the grace sentinel resets on any sighting, classified or not"),
		Memory.SeeUnknownGraceUntil < 0.0);
	return true;
}

// --- Clearly visible: the one-shot ATTACK_UNKNOWN roll, and UNKNOWN_RUN_TIMER ---------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSeeUnknownSweepVisibleTest,
	"Elysium.Substrate.NpcConditions.SeeUnknownSweepVisible", GElysiumTestFlags)
bool FElysiumNpcSeeUnknownSweepVisibleTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;
	auto Scope = F.RunWithMask(FElysiumNpcConditions());
	F.MakePlayerStealthy(false);
	Memory.BestSeeUnknown = F.Player->Handle;

	// The chance is `min(100, (repeat_sightings + 5) * 20)`, which is >= 100 for every reachable
	// `repeat_sightings >= 0` -- the roll's "clear ATTACK_UNKNOWN" arm is retail dead code, and the
	// set arm is therefore deterministic.
	Memory.SeeUnknownRepeatSightings = 0;
	const FElysiumNpcConditions Out =
		F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
	TestTrue(TEXT("MADE_INITIAL_RESPONSE latches the roll"),
		F.Npc->NpcFlags.Has(EFlag::MADE_INITIAL_RESPONSE));
	TestTrue(TEXT("the deterministic roll sets ATTACK_UNKNOWN"),
		F.Npc->NpcFlags.Has(EFlag::ATTACK_UNKNOWN));
	TestTrue(TEXT("ATTACK_UNKNOWN standing raises UNKNOWN_RUN_TIMER"), Out.Has(ECond::UnknownRunTimer));
	TestTrue(TEXT("investigate_mode Anything admits INVESTIGATE_SIGHT"),
		Out.Has(ECond::InvestigateSight));

	// --- The roll does not re-run once latched ---------------------------------------------------
	F.Npc->NpcFlags.Clear(EFlag::ATTACK_UNKNOWN);
	const FElysiumNpcConditions Second =
		F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 1.0);
	TestFalse(TEXT("MADE_INITIAL_RESPONSE already stands, so the roll is skipped"),
		F.Npc->NpcFlags.Has(EFlag::ATTACK_UNKNOWN));
	TestFalse(TEXT("...and UNKNOWN_RUN_TIMER reads the CURRENT flag, not a re-rolled one"),
		Second.Has(ECond::UnknownRunTimer));
	return true;
}

// --- Hidden/grappled: the one-shot IGNORE_UNKNOWN roll and `full_investigate` --------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSeeUnknownSweepHiddenRollTest,
	"Elysium.Substrate.NpcConditions.SeeUnknownSweepHiddenRoll", GElysiumTestFlags)
bool FElysiumNpcSeeUnknownSweepHiddenRollTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("player"), F.Player)
		|| !TestNotNull(TEXT("bystander"), F.Bystander))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;
	auto Scope = F.RunWithMask(FElysiumNpcConditions());
	F.MakePlayerStealthy(true);
	Memory.BestSeeUnknown = F.Player->Handle;

	// The chance is `max(0, 50 - repeat_sightings * 20)`, zero from `repeat_sightings == 3` on --
	// deterministically never sets. Pre-set the flag so a no-op roll would be caught, not just an
	// already-false one.
	Memory.SeeUnknownRepeatSightings = 3;
	F.Npc->FullInvestigate = 0;
	F.Npc->NpcFlags.Set(EFlag::IGNORE_UNKNOWN);
	F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
	TestTrue(TEXT("the roll still latches"), F.Npc->NpcFlags.Has(EFlag::MADE_INITIAL_RESPONSE));
	TestFalse(TEXT("a zero chance deterministically clears IGNORE_UNKNOWN"),
		F.Npc->NpcFlags.Has(EFlag::IGNORE_UNKNOWN));

	// --- `full_investigate` forces the skip regardless of the chance ---------------------------
	F.Npc->NpcFlags.Clear(EFlag::MADE_INITIAL_RESPONSE);
	F.Npc->NpcFlags.Set(EFlag::IGNORE_UNKNOWN);
	Memory.SeeUnknownRepeatSightings = 0;   // chance would otherwise be 50%
	F.Npc->FullInvestigate = 1;
	F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
	TestFalse(TEXT("full_investigate suppresses IGNORE_UNKNOWN even at a 50% chance"),
		F.Npc->NpcFlags.Has(EFlag::IGNORE_UNKNOWN));
	return true;
}

// --- The 2-D closing speed classification, and the mid-sweep SEE_UNKNOWN retraction --------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSeeUnknownSweepClosingSpeedTest,
	"Elysium.Substrate.NpcConditions.SeeUnknownSweepClosingSpeed", GElysiumTestFlags)
bool FElysiumNpcSeeUnknownSweepClosingSpeedTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;
	auto Scope = F.RunWithMask(FElysiumNpcConditions());
	F.MakePlayerStealthy(true);
	Memory.BestSeeUnknown = F.Player->Handle;
	// Latch the roll now, with the flag preset by hand rather than drawn, so every case below is a
	// statement about the classification alone.
	F.Npc->NpcFlags.Set(EFlag::MADE_INITIAL_RESPONSE);

	// The NPC sits at +X from the player, so "toward the NPC" is +X: a positive player velocity.X
	// is closing.
	F.Npc->Origin = FVector(100.0, 0.0, 0.0);
	F.Player->Origin = FVector::ZeroVector;

	// --- Not ignoring: retreating, holding (exact equality, dead), advancing --------------------
	F.Npc->NpcFlags.Clear(EFlag::IGNORE_UNKNOWN);
	// The entity's velocity is world cm/s; the threshold is 20 Source units per second.
	F.Player->Velocity = FVector(10.0 * ElysiumMove::U, 0.0, 0.0);   // 10 u/s < 20
	{
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
		TestTrue(TEXT("below the threshold raises INVESTIGATE_SIGHT"), Out.Has(ECond::InvestigateSight));
		TestTrue(TEXT("...and UNKNOWN_RETREATING"), Out.Has(ECond::UnknownRetreating));
		TestTrue(TEXT("SEE_UNKNOWN is untouched outside the ignore arm"), Out.Has(ECond::SeeUnknown));
	}
	F.Player->Velocity = FVector(20.0 * ElysiumMove::U, 0.0, 0.0);   // exactly the threshold
	{
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
		TestTrue(TEXT("exact equality is the dead UNKNOWN_HOLDING arm, reproduced"),
			Out.Has(ECond::UnknownHolding));
	}
	F.Player->Velocity = FVector(30.0 * ElysiumMove::U, 0.0, 0.0);   // 30 u/s > 20
	{
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
		TestTrue(TEXT("above the threshold raises UNKNOWN_ADVANCING"), Out.Has(ECond::UnknownAdvancing));
	}
	F.Player->Velocity = FVector(30.0, 0.0, 0.0);   // 30 cm/s is ~11.8 u/s: below the threshold
	{
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
		TestTrue(TEXT("the threshold is read in Source units, not cm"), Out.Has(ECond::UnknownRetreating));
	}

	// --- Ignoring: the retraction, and the advancing override ------------------------------------
	F.Npc->NpcFlags.Set(EFlag::IGNORE_UNKNOWN);
	F.Npc->NpcFlags.Clear(EFlag::LOOKED_AT_UNKNOWN);
	F.Npc->NpcFlags.Clear(EFlag::FINISHED_IGNORE_UNKNOWN);
	Memory.SeeUnknownStartTimer = 100.0;   // in the future: the advancing override cannot fire yet
	F.Player->Velocity = FVector(10.0 * ElysiumMove::U, 0.0, 0.0);   // not closing fast enough either
	{
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
		TestFalse(TEXT("the ignore arm's retraction clears SEE_UNKNOWN mid-sweep"),
			Out.Has(ECond::SeeUnknown));
		TestTrue(TEXT("...and raises IGNORE_UNKNOWN with neither suppressing flag set"),
			Out.Has(ECond::IgnoreUnknown));
	}
	F.Npc->NpcFlags.Set(EFlag::LOOKED_AT_UNKNOWN);
	{
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 0.0);
		TestFalse(TEXT("SEE_UNKNOWN still retracts under LOOKED_AT_UNKNOWN"), Out.Has(ECond::SeeUnknown));
		TestFalse(TEXT("...but IGNORE_UNKNOWN itself is suppressed"), Out.Has(ECond::IgnoreUnknown));
	}
	F.Npc->NpcFlags.Clear(EFlag::LOOKED_AT_UNKNOWN);

	// Closing past the threshold AND the start timer has elapsed overrides the retraction.
	Memory.SeeUnknownStartTimer = 0.0;
	F.Player->Velocity = FVector(30.0 * ElysiumMove::U, 0.0, 0.0);
	{
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeUnknown }), 5.0);
		TestTrue(TEXT("closing past the threshold with the timer elapsed raises UNKNOWN_ADVANCING"),
			Out.Has(ECond::UnknownAdvancing));
		TestTrue(TEXT("...and INVESTIGATE_SIGHT"), Out.Has(ECond::InvestigateSight));
		TestTrue(TEXT("...and SEE_UNKNOWN is NOT retracted on this arm"), Out.Has(ECond::SeeUnknown));
	}
	return true;
}

// --- The grace timer survives a save ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSeeUnknownSweepSaveTest,
	"Elysium.Substrate.NpcConditions.SeeUnknownSweepSave", GElysiumTestFlags)
bool FElysiumNpcSeeUnknownSweepSaveTest::RunTest(const FString&)
{
	FElysiumNpcMemory Memory;
	Memory.SeeUnknownGraceUntil = 42.5;

	TArray<uint8> Payload;
	{
		FMemoryWriter Writer(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Memory.Serialize(Ar);
	}
	FElysiumNpcMemory Restored;
	{
		FMemoryReader Reader(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Restored.Serialize(Ar);
	}
	TestTrue(TEXT("the grace timer round-trips"),
		FMath::IsNearlyEqual(Restored.SeeUnknownGraceUntil, 42.5));
	return true;
}

}   // namespace ElysiumNpcSeeUnknownSweepTests

#endif // WITH_DEV_AUTOMATION_TESTS
