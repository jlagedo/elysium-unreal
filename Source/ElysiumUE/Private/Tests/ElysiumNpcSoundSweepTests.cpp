// The sound sweep, `CAI_BaseNPCTroika::FUN_102b1cd0` (`ElysiumNpcCond::GatherSounds`): the two
// clears it owns, the `m_flNextInvestigateSoundTime` gate, its six arms and their last-wins order,
// `HEAR_FLANK_SOUND`, and the `SEE_SOUND_SOURCE` tail with its rate limit.
//
// The records are written directly rather than driven through the game-sound bus: the bus, the
// admission rules and the delayed `HEAR_*` promotion are story 6's contract and have their own
// cohort in `ElysiumNpcSensesTests.cpp`. What is under test here is what the sweep does with a
// record and a raised condition, which is exactly the seam 10a adds.
//
// `docs/vtmb/npc-ai/conditions-and-states.md` -> "The three `GatherConditions` sweeps and the
// interest predicate" owns every fact here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumNpcSoundSweepTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ECond = EElysiumNpcCond;
using EMode = EElysiumInvestigateMode;

namespace
{
	// The sweep reads relations, flags, records, the running program's mask and two clocks. It
	// never touches a body, so nothing here needs a model.
	struct FSweepFixture
	{
		FElysiumNpcWorldFixture Fixture;
		FElysiumRecordingServices& Services;
		FElysiumEntityWorld& World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Owner = nullptr;      // the entity that made the sounds
		FElysiumNpc* Second = nullptr;     // a second owner, for the last-wins case
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__sound_sweep_test__"), 0x534e4453);
			for (const TCHAR* Name : { TEXT("npc"), TEXT("owner"), TEXT("second") })
			{
				Builder.AddNpc(Name);
			}
			return Builder;
		}

		FSweepFixture()
			: Fixture(BuildWorld())
			, Services(Fixture.Services)
			, World(Fixture.World)
		{
			Npc = Fixture.Npc(TEXT("npc"));
			Owner = Fixture.Npc(TEXT("owner"));
			Second = Fixture.Npc(TEXT("second"));
			Player = Fixture.Player();
			if (Npc != nullptr)
			{
				// Facing +X, at the origin: a sound at negative X is behind.
				Npc->Angles = FVector::ZeroVector;
				Npc->Origin = FVector::ZeroVector;
				Npc->InvestigateMode = static_cast<int32>(EMode::Anything);
				Npc->InvestigateModeCombat = static_cast<int32>(EMode::Anything);
			}
		}

		// Fill one of the seven snapshot records the way `OnListened` would have.
		void Record(FElysiumGameSoundEvent& Slot, const FElysiumEntity* SoundOwner,
			const FVector& Position) const
		{
			Slot = FElysiumGameSoundEvent();
			Slot.Source = SoundOwner ? SoundOwner->Handle : FElysiumEntityHandle::Invalid();
			Slot.Position = Position;
			Slot.Time = 0.0;
			Slot.ExpireTime = 1000.0;
		}

		// Put the NPC on a running program whose authored interrupt mask is `Mask`. The sweep's
		// three mask gates read the running program, so without this every one of them is false.
		//
		// NOTE the effective mask is `Mask` PLUS `FElysiumNpc::BuildScheduleTestBits`, which always
		// adds `InvestigateLevel`, the law conditions, `Comfort`, `NpcFreeze`, and -- with no
		// committed enemy -- `HearFlinch`. So an empty `Mask` is not an empty effective mask. That
		// makes the "no Flinch arm" case below STRONGER than it reads: `HearFlinch` genuinely is in
		// the effective mask, and the sweep still raises nothing for it.
		//
		// Returned by value so the caller owns the scope's lifetime; the state lives on the NPC.
		TUniquePtr<ElysiumSchedule::FInterruptMaskScope> RunWithMask(const FElysiumNpcConditions& Mask)
		{
			auto Scope = MakeUnique<ElysiumSchedule::FInterruptMaskScope>(
				EElysiumScheduleId::IdleDisposition, Mask);
			ElysiumSchedule::Start(Npc->Schedule, EElysiumScheduleId::IdleDisposition, *Npc);
			return Scope;
		}

		FElysiumNpcConditions Sweep(const FElysiumNpcConditions& Raised, double Now = 0.0) const
		{
			FElysiumNpcConditions Out = Raised;
			ElysiumNpcCond::GatherSounds(*Npc, Now, Out);
			return Out;
		}
	};
}

// --- The gate, the two clears, and the arms' two admission routes -------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSoundSweepArmsTest,
	"Elysium.Substrate.NpcConditions.SoundSweepArms", GElysiumTestFlags)
bool FElysiumNpcSoundSweepArmsTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("owner"), F.Owner))
	{
		return false;
	}
	F.Record(F.Npc->Senses.Memory.LastSoundPlayer, F.Owner, FVector(100.0, 0.0, 0.0));

	// --- The predicate route: no mask, but `investigate_mode` takes an interest -----------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPlayer }));
		TestTrue(TEXT("HEAR_PLAYER plus an interested predicate raises INVESTIGATE_SOUND"),
			Out.Has(ECond::InvestigateSound));
	}

	// --- Mode 0 refuses, and with no mask that is the whole answer ------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		F.Npc->InvestigateMode = static_cast<int32>(EMode::Never);
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPlayer }));
		TestFalse(TEXT("mode 0 and an empty mask raise nothing"), Out.Has(ECond::InvestigateSound));
	}

	// --- The mask route: the predicate is not even consulted ------------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions::Of({ ECond::HearPlayer }));
		F.Npc->InvestigateMode = static_cast<int32>(EMode::Never);
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPlayer }));
		TestTrue(TEXT("a program that already interrupts on HEAR_PLAYER admits without the predicate"),
			Out.Has(ECond::InvestigateSound));
		F.Npc->InvestigateMode = static_cast<int32>(EMode::Anything);
	}

	// --- The raw condition is required on both routes --------------------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions::Of({ ECond::HearPlayer }));
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions());
		TestFalse(TEXT("the mask alone, with the condition unset, raises nothing"),
			Out.Has(ECond::InvestigateSound));
	}

	// --- `HEAR_DANGER` tests NEITHER, and takes an ownerless sound -------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		F.Npc->InvestigateMode = static_cast<int32>(EMode::Never);
		F.Record(F.Npc->Senses.Memory.LastSoundDanger, nullptr, FVector(100.0, 0.0, 0.0));
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::HearDanger }));
		TestTrue(TEXT("HEAR_DANGER skips the predicate and the mask entirely"),
			Out.Has(ECond::InvestigateSound));
		F.Npc->InvestigateMode = static_cast<int32>(EMode::Anything);
	}

	// --- An ownerless sound on a predicate arm cannot be investigated ----------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		F.Record(F.Npc->Senses.Memory.LastSoundWorld, nullptr, FVector(100.0, 0.0, 0.0));
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::HearWorld }));
		TestFalse(TEXT("a null owner fails ShouldInvestigate even under mode 6"),
			Out.Has(ECond::InvestigateSound));
	}

	// --- There is NO Flinch arm -------------------------------------------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		F.Record(F.Npc->Senses.Memory.LastSoundFlinch, F.Owner, FVector(100.0, 0.0, 0.0));
		const FElysiumNpcConditions Out = F.Sweep(FElysiumNpcConditions::Of({ ECond::HearFlinch }));
		TestFalse(TEXT("HEAR_FLINCH is never swept: +0x6210 has no arm"),
			Out.Has(ECond::InvestigateSound));
	}

	// --- The gate suppresses the whole body, but not the two clears ------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		F.Npc->Senses.Memory.NextInvestigateSoundTime = 5.0;
		FElysiumNpcConditions Raised = FElysiumNpcConditions::Of({ ECond::HearPlayer });
		// Standing from a previous pass: the sweep owns these two and clears them regardless.
		Raised.Set(ECond::InvestigateSound);
		Raised.Set(ECond::HearFlankSound);
		const FElysiumNpcConditions Out = F.Sweep(Raised, 4.9);
		TestFalse(TEXT("the gate suppresses the six arms"), Out.Has(ECond::InvestigateSound));
		TestFalse(TEXT("...and HEAR_FLANK_SOUND is cleared anyway"), Out.Has(ECond::HearFlankSound));

		// Retail's test is `<= curtime`, so the gate opens ON equality.
		const FElysiumNpcConditions AtEquality =
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPlayer }), 5.0);
		TestTrue(TEXT("the gate opens at exact equality"), AtEquality.Has(ECond::InvestigateSound));
		F.Npc->Senses.Memory.NextInvestigateSoundTime = 0.0;
	}

	// --- `bCombatMode` is per arm, not per sense --------------------------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		// The two modes answer differently, so which one an arm consults is observable.
		F.Npc->InvestigateMode = static_cast<int32>(EMode::Never);
		F.Npc->InvestigateModeCombat = static_cast<int32>(EMode::Anything);
		F.Record(F.Npc->Senses.Memory.LastSoundCombat, F.Owner, FVector(100.0, 0.0, 0.0));
		F.Record(F.Npc->Senses.Memory.LastSoundBulletImpact, F.Owner, FVector(100.0, 0.0, 0.0));

		TestTrue(TEXT("the combat arm reads investigate_mode_combat"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearCombat })).Has(ECond::InvestigateSound));
		TestTrue(TEXT("the bullet-impact arm reads investigate_mode_combat"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearBulletImpact })).Has(ECond::InvestigateSound));
		TestFalse(TEXT("the player arm reads plain investigate_mode"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPlayer })).Has(ECond::InvestigateSound));
		TestFalse(TEXT("the world arm reads plain investigate_mode"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearWorld })).Has(ECond::InvestigateSound));
		TestFalse(TEXT("the physics-danger arm reads plain investigate_mode"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPhysicsDanger })).Has(ECond::InvestigateSound));
	}
	return true;
}

// --- Last-wins, and `HEAR_FLANK_SOUND` ----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSoundSweepFlankTest,
	"Elysium.Substrate.NpcConditions.SoundSweepFlank", GElysiumTestFlags)
bool FElysiumNpcSoundSweepFlankTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("owner"), F.Owner)
		|| !TestNotNull(TEXT("second"), F.Second))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;
	// The flank test only fires for the committed enemy, so it doubles as a probe for WHICH record
	// the arms left as the winner.
	Memory.Enemy = F.Owner->Handle;
	const FElysiumNpcConditions FlankMask = FElysiumNpcConditions::Of({ ECond::HearFlankSound });

	// --- Last passing arm wins: combat is evaluated after world --------------------------------
	{
		auto Scope = F.RunWithMask(FlankMask);
		// World is owned by the enemy and BEHIND; combat is owned by someone else. If world had
		// won, the flank arm would fire.
		F.Record(Memory.LastSoundWorld, F.Owner, FVector(-100.0, 0.0, 0.0));
		F.Record(Memory.LastSoundCombat, F.Second, FVector(-100.0, 0.0, 0.0));
		const FElysiumNpcConditions Out =
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearWorld, ECond::HearCombat }));
		TestTrue(TEXT("both arms passed"), Out.Has(ECond::InvestigateSound));
		TestFalse(TEXT("the LAST arm (combat) is the winner, so the enemy's world sound did not flank"),
			Out.Has(ECond::HearFlankSound));
	}

	// --- ...and with the ownership the other way round, it does fire ----------------------------
	{
		auto Scope = F.RunWithMask(FlankMask);
		F.Record(Memory.LastSoundWorld, F.Second, FVector(-100.0, 0.0, 0.0));
		F.Record(Memory.LastSoundCombat, F.Owner, FVector(-100.0, 0.0, 0.0));
		TestTrue(TEXT("the enemy owning the LAST arm's record flanks"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearWorld, ECond::HearCombat }))
				.Has(ECond::HearFlankSound));
	}

	// --- Each of the four terms is required -------------------------------------------------------
	{
		auto Scope = F.RunWithMask(FlankMask);
		F.Record(Memory.LastSoundCombat, F.Owner, FVector(-100.0, 0.0, 0.0));
		const FElysiumNpcConditions Combat = FElysiumNpcConditions::Of({ ECond::HearCombat });
		TestTrue(TEXT("behind, enemy-owned, masked, with a winner"),
			F.Sweep(Combat).Has(ECond::HearFlankSound));

		F.Record(Memory.LastSoundCombat, F.Owner, FVector(100.0, 0.0, 0.0));
		TestFalse(TEXT("a sound in FRONT does not flank"), F.Sweep(Combat).Has(ECond::HearFlankSound));

		// Exactly abeam: the comparison is `< 0`, strictly.
		F.Record(Memory.LastSoundCombat, F.Owner, FVector(0.0, 100.0, 0.0));
		TestFalse(TEXT("a sound exactly abeam does not flank"),
			F.Sweep(Combat).Has(ECond::HearFlankSound));

		F.Record(Memory.LastSoundCombat, F.Second, FVector(-100.0, 0.0, 0.0));
		TestFalse(TEXT("a NON-enemy behind me does not flank"),
			F.Sweep(Combat).Has(ECond::HearFlankSound));

		F.Record(Memory.LastSoundCombat, F.Owner, FVector(-100.0, 0.0, 0.0));
		Memory.Enemy = FElysiumEntityHandle::Invalid();
		TestFalse(TEXT("with no committed enemy there is nothing to flank me"),
			F.Sweep(Combat).Has(ECond::HearFlankSound));
		Memory.Enemy = F.Owner->Handle;

		// No winner at all: the raw condition is absent, so no arm claimed one.
		TestFalse(TEXT("no winner, no flank"),
			F.Sweep(FElysiumNpcConditions()).Has(ECond::HearFlankSound));
	}

	// --- `FUN_101b99d0`: bullet-impact and physics-danger measure to the OWNER's live origin -------
	{
		auto Scope = F.RunWithMask(FlankMask);
		// The record says the sound happened in FRONT of the NPC, but its owner -- my enemy -- is
		// standing BEHIND me. Retail's `FUN_101b99d0` substitutes the owner's `GetAbsOrigin()` for
		// raw types 0x10 and 0x400 only, so these two arms must flank and the other four must not.
		F.Owner->Origin = FVector(-100.0, 0.0, 0.0);

		F.Record(Memory.LastSoundBulletImpact, F.Owner, FVector(100.0, 0.0, 0.0));
		Memory.LastSoundBulletImpact.TypeMask = ElysiumGameSounds::BulletImpact;
		TestTrue(TEXT("bullet impact measures to the owner, who is behind me"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearBulletImpact }))
				.Has(ECond::HearFlankSound));

		F.Record(Memory.LastSoundPhysicsDanger, F.Owner, FVector(100.0, 0.0, 0.0));
		Memory.LastSoundPhysicsDanger.TypeMask = ElysiumGameSounds::PhysicsDanger;
		TestTrue(TEXT("physics danger does too"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPhysicsDanger }))
				.Has(ECond::HearFlankSound));

		// A combat sound keeps the stored origin, which is in front: no flank.
		F.Record(Memory.LastSoundCombat, F.Owner, FVector(100.0, 0.0, 0.0));
		Memory.LastSoundCombat.TypeMask = ElysiumGameSounds::Combat;
		TestFalse(TEXT("a combat sound keeps the record's own origin, which is in front"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearCombat })).Has(ECond::HearFlankSound));

		// And the substitution is not unconditional the other way either: an owner in FRONT with the
		// impact recorded behind must NOT flank.
		F.Owner->Origin = FVector(100.0, 0.0, 0.0);
		F.Record(Memory.LastSoundBulletImpact, F.Owner, FVector(-100.0, 0.0, 0.0));
		Memory.LastSoundBulletImpact.TypeMask = ElysiumGameSounds::BulletImpact;
		TestFalse(TEXT("...and an impact behind me whose owner is in front does not flank"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearBulletImpact }))
				.Has(ECond::HearFlankSound));
		F.Owner->Origin = FVector::ZeroVector;
	}

	// --- A committed enemy handle whose entity is gone is not an enemy -------------------------------
	{
		auto Scope = F.RunWithMask(FlankMask);
		F.Record(Memory.LastSoundCombat, F.Owner, FVector(-100.0, 0.0, 0.0));
		Memory.LastSoundCombat.TypeMask = ElysiumGameSounds::Combat;
		// Structurally set, but the epoch no longer resolves -- retail's `GetEnemy()` answers null.
		Memory.Enemy = F.Owner->Handle;
		Memory.Enemy.Epoch += 1;
		Memory.LastSoundCombat.Source = Memory.Enemy;
		TestTrue(TEXT("the stale handle still reads as set"), Memory.Enemy.IsSet());
		TestFalse(TEXT("...but a stale enemy cannot flank: retail compares resolved pointers"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearCombat })).Has(ECond::HearFlankSound));
		Memory.Enemy = F.Owner->Handle;
	}

	// --- The mask term ------------------------------------------------------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		F.Record(Memory.LastSoundCombat, F.Owner, FVector(-100.0, 0.0, 0.0));
		TestFalse(TEXT("a program that does not list HEAR_FLANK_SOUND never gets it"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearCombat })).Has(ECond::HearFlankSound));
	}
	return true;
}

// --- The `SEE_SOUND_SOURCE` tail ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSoundSweepSeeSourceTest,
	"Elysium.Substrate.NpcConditions.SoundSweepSeeSource", GElysiumTestFlags)
bool FElysiumNpcSoundSweepSeeSourceTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("owner"), F.Owner)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;
	const FElysiumNpcConditions TailMask = FElysiumNpcConditions::Of({ ECond::SeeSoundSource });

	// --- No mask: cleared outright ----------------------------------------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions());
		Memory.BestSoundSource = F.Owner->Handle;
		FElysiumNpcConditions Raised;
		Raised.Set(ECond::SeeSoundSource);
		TestFalse(TEXT("a program that does not list SEE_SOUND_SOURCE has it cleared"),
			F.Sweep(Raised).Has(ECond::SeeSoundSource));
	}

	// --- `FUN_102b8cd0`: a hated or feared source returns WITHOUT touching the condition ----------
	for (const EElysiumRelationship Blocking : { EElysiumRelationship::Hate, EElysiumRelationship::Fear })
	{
		auto Scope = F.RunWithMask(TailMask);
		Memory.BestSoundSource = F.Owner->Handle;
		F.Npc->Relationships.SetEntity(F.Owner->Handle, Blocking, 5);
		FElysiumNpcConditions Raised;
		Raised.Set(ECond::SeeSoundSource);
		TestTrue(TEXT("a D_HT/D_FR source leaves a standing SEE_SOUND_SOURCE alone"),
			F.Sweep(Raised).Has(ECond::SeeSoundSource));
		TestFalse(TEXT("...and does not raise one that was not standing"),
			F.Sweep(FElysiumNpcConditions()).Has(ECond::SeeSoundSource));
	}

	// --- An unresolvable source is the same sticky return -----------------------------------------
	{
		auto Scope = F.RunWithMask(TailMask);
		Memory.BestSoundSource = FElysiumEntityHandle::Invalid();
		FElysiumNpcConditions Raised;
		Raised.Set(ECond::SeeSoundSource);
		TestTrue(TEXT("no committed source leaves the condition untouched"),
			F.Sweep(Raised).Has(ECond::SeeSoundSource));
	}

	// --- The closest-player rung ------------------------------------------------------------------
	{
		auto Scope = F.RunWithMask(TailMask);
		F.Npc->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Neutral, 5);
		Memory.BestSoundSource = F.Player->Handle;
		Memory.ClosestPlayer = F.Player->Handle;
		TestTrue(TEXT("the source IS the closest player and SEE_PLAYER stands"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::SeePlayer })).Has(ECond::SeeSoundSource));
		TestFalse(TEXT("the same rung without SEE_PLAYER clears rather than falling through"),
			F.Sweep(FElysiumNpcConditions()).Has(ECond::SeeSoundSource));
	}

	// --- The last-seen rungs are live ------------------------------------------------------------
	{
		auto Scope = F.RunWithMask(TailMask);
		// `CAI_BaseNPC::OnLooked` (`0x1026a2c0`) writes `m_hLastSeenHateEnt` on a `D_HT` sighting
		// with `IRelationPriority < 0xb`; `Memory.LastSeen[Hate]` is this runtime's copy of it.
		Memory.LastSeen[static_cast<int32>(FElysiumNpcMemory::ESeen::Hate)] = F.Owner->Handle;
		// The relation must have LEFT D_HT since that sighting, or step 1's `FUN_102b8cd0` gate
		// rejects the source before the chain is reached at all.
		F.Npc->Relationships.SetEntity(F.Owner->Handle, EElysiumRelationship::Neutral, 5);
		Memory.BestSoundSource = F.Owner->Handle;
		Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
		Memory.Enemy = FElysiumEntityHandle::Invalid();
		Memory.NextSeeSoundSourceTime = 0.0;
		// Put the source BEHIND the NPC so the stranger arm below would CLEAR. That makes this a
		// statement about the rung rather than about whatever the fallback happens to answer.
		F.Owner->Origin = FVector(-100.0, 0.0, 0.0);

		FElysiumNpcConditions Raised;
		Raised.Set(ECond::SeeHate);
		TestTrue(TEXT("the last-seen-hate rung admits when SEE_HATE stands"),
			F.Sweep(Raised, 0.0).Has(ECond::SeeSoundSource));
		TestTrue(TEXT("...and the rung, not the stranger arm, is what answered"),
			FMath::IsNearlyEqual(Memory.NextSeeSoundSourceTime, 0.0));

		// Matching the rung without its condition clears rather than falling through to the
		// stranger arm.
		TestFalse(TEXT("the same rung without SEE_HATE clears"),
			F.Sweep(FElysiumNpcConditions()).Has(ECond::SeeSoundSource));

		// A rung that was never written resolves to null and cannot match a non-null source.
		Memory.LastSeen[static_cast<int32>(FElysiumNpcMemory::ESeen::Hate)] =
			FElysiumEntityHandle::Invalid();
		FElysiumNpcConditions StillHate;
		StillHate.Set(ECond::SeeHate);
		TestFalse(TEXT("an unwritten rung never matches, so control reaches the stranger arm"),
			F.Sweep(StillHate, 0.0).Has(ECond::SeeSoundSource));
		TestTrue(TEXT("...which is observable: it re-armed the limit"),
			FMath::IsNearlyEqual(Memory.NextSeeSoundSourceTime, 0.5));
		Memory.NextSeeSoundSourceTime = 0.0;
		F.Owner->Origin = FVector::ZeroVector;
	}

	// --- Every rung, with its own condition ---------------------------------------------------------
	// The chain's six (comparand, condition) pairs come straight from the decompilation; a swapped
	// pairing would be invisible if only one rung were exercised.
	{
		using ESeen = FElysiumNpcMemory::ESeen;
		const TPair<ESeen, ECond> Rungs[] = {
			{ ESeen::Hate,    ECond::SeeHate    },
			{ ESeen::Fear,    ECond::SeeFear    },
			{ ESeen::Dislike, ECond::SeeDislike },
			{ ESeen::Nemesis, ECond::SeeNemesis },
		};
		for (const TPair<ESeen, ECond>& Rung : Rungs)
		{
			auto Scope = F.RunWithMask(TailMask);
			F.Npc->Relationships.SetEntity(F.Owner->Handle, EElysiumRelationship::Neutral, 5);
			Memory.BestSoundSource = F.Owner->Handle;
			Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
			Memory.Enemy = FElysiumEntityHandle::Invalid();
			for (int32 i = 0; i < static_cast<int32>(ESeen::Count); ++i)
			{
				Memory.LastSeen[i] = FElysiumEntityHandle::Invalid();
			}
			Memory.LastSeen[static_cast<int32>(Rung.Key)] = F.Owner->Handle;
			// Behind the NPC, so the stranger-arm fallback would answer FALSE: a pass can only come
			// from the rung itself.
			F.Owner->Origin = FVector(-100.0, 0.0, 0.0);
			Memory.NextSeeSoundSourceTime = 0.0;

			FElysiumNpcConditions Raised;
			Raised.Set(Rung.Value);
			TestTrue(*FString::Printf(TEXT("rung %s admits on its own condition"),
				ElysiumNpcCondName(Rung.Value)), F.Sweep(Raised, 0.0).Has(ECond::SeeSoundSource));

			// The pairing is exclusive: a DIFFERENT sight condition must not satisfy this rung.
			FElysiumNpcConditions Wrong;
			Wrong.Set(Rung.Value == ECond::SeeHate ? ECond::SeeFear : ECond::SeeHate);
			TestFalse(*FString::Printf(TEXT("rung %s refuses another family's condition"),
				ElysiumNpcCondName(Rung.Value)), F.Sweep(Wrong, 0.0).Has(ECond::SeeSoundSource));
		}
		F.Owner->Origin = FVector::ZeroVector;
	}

	// --- The committed-enemy rung -------------------------------------------------------------------
	{
		auto Scope = F.RunWithMask(TailMask);
		// Reachable only for an enemy the NPC does NOT hate or fear -- step 1 rejects the rest.
		F.Npc->Relationships.SetEntity(F.Owner->Handle, EElysiumRelationship::Neutral, 5);
		Memory.BestSoundSource = F.Owner->Handle;
		Memory.Enemy = F.Owner->Handle;
		Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
		for (int32 i = 0; i < static_cast<int32>(FElysiumNpcMemory::ESeen::Count); ++i)
		{
			Memory.LastSeen[i] = FElysiumEntityHandle::Invalid();
		}
		F.Owner->Origin = FVector(-100.0, 0.0, 0.0);
		TestTrue(TEXT("the enemy rung admits on SEE_ENEMY"),
			F.Sweep(FElysiumNpcConditions::Of({ ECond::SeeEnemy }), 0.0).Has(ECond::SeeSoundSource));
		TestFalse(TEXT("...and clears without it"),
			F.Sweep(FElysiumNpcConditions(), 0.0).Has(ECond::SeeSoundSource));
		Memory.Enemy = FElysiumEntityHandle::Invalid();
		F.Owner->Origin = FVector::ZeroVector;
	}

	// --- The tail reads the COMMITTED source, not this sweep's winner --------------------------------
	{
		auto Scope = F.RunWithMask(FElysiumNpcConditions::Of({ ECond::SeeSoundSource,
			ECond::HearPlayer }));
		// This pass hears a sound owned by `Second`, but the committed source is still `Owner`.
		// If the tail read the winner, the ClosestPlayer rung below could not answer.
		F.Record(Memory.LastSoundPlayer, F.Second, FVector(100.0, 0.0, 0.0));
		F.Npc->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Neutral, 5);
		Memory.BestSoundSource = F.Player->Handle;
		Memory.ClosestPlayer = F.Player->Handle;
		Memory.Enemy = FElysiumEntityHandle::Invalid();
		for (int32 i = 0; i < static_cast<int32>(FElysiumNpcMemory::ESeen::Count); ++i)
		{
			Memory.LastSeen[i] = FElysiumEntityHandle::Invalid();
		}
		const FElysiumNpcConditions Out =
			F.Sweep(FElysiumNpcConditions::Of({ ECond::HearPlayer, ECond::SeePlayer }), 0.0);
		TestTrue(TEXT("the sweep picked a winner owned by someone else"),
			Out.Has(ECond::InvestigateSound));
		TestTrue(TEXT("...and the tail still answered about the COMMITTED source"),
			Out.Has(ECond::SeeSoundSource));
	}

	// --- A hated source never reaches the chain at all ---------------------------------------------
	{
		auto Scope = F.RunWithMask(TailMask);
		Memory.LastSeen[static_cast<int32>(FElysiumNpcMemory::ESeen::Hate)] = F.Owner->Handle;
		F.Npc->Relationships.SetEntity(F.Owner->Handle, EElysiumRelationship::Hate, 5);
		Memory.BestSoundSource = F.Owner->Handle;
		FElysiumNpcConditions Raised;
		Raised.Set(ECond::SeeHate);
		TestFalse(TEXT("step 1 rejects a hated source before the last-seen-hate rung is reached"),
			F.Sweep(Raised, 0.0).Has(ECond::SeeSoundSource));
		F.Npc->Relationships.SetEntity(F.Owner->Handle, EElysiumRelationship::Neutral, 5);
	}

	// --- The stranger arm and its rate limit -------------------------------------------------------
	{
		auto Scope = F.RunWithMask(TailMask);
		F.Npc->Relationships.SetEntity(F.Owner->Handle, EElysiumRelationship::Neutral, 5);
		Memory.BestSoundSource = F.Owner->Handle;
		Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
		Memory.Enemy = FElysiumEntityHandle::Invalid();
		// Every earlier rung must miss, or the chain returns before the stranger arm is reached.
		for (int32 i = 0; i < static_cast<int32>(FElysiumNpcMemory::ESeen::Count); ++i)
		{
			Memory.LastSeen[i] = FElysiumEntityHandle::Invalid();
		}
		Memory.NextSeeSoundSourceTime = 0.0;
		// In the cone, in range, unoccluded.
		F.Owner->Origin = FVector(100.0, 0.0, 0.0);

		TestTrue(TEXT("a visible stranger raises SEE_SOUND_SOURCE"),
			F.Sweep(FElysiumNpcConditions(), 10.0).Has(ECond::SeeSoundSource));
		TestTrue(TEXT("...and re-arms the 0.5 s limit"),
			FMath::IsNearlyEqual(Memory.NextSeeSoundSourceTime, 10.5));

		// Inside the limit the arm returns early, leaving whatever stood.
		F.Owner->Origin = FVector(-100.0, 0.0, 0.0);   // now behind: it would fail the cone
		FElysiumNpcConditions Standing;
		Standing.Set(ECond::SeeSoundSource);
		TestTrue(TEXT("inside the rate limit the condition is left alone, not re-tested"),
			F.Sweep(Standing, 10.4).Has(ECond::SeeSoundSource));
		TestTrue(TEXT("...and the limit is not pushed out"),
			FMath::IsNearlyEqual(Memory.NextSeeSoundSourceTime, 10.5));

		// Past it, the failing cone clears.
		TestFalse(TEXT("past the limit a source behind me clears SEE_SOUND_SOURCE"),
			F.Sweep(Standing, 10.5).Has(ECond::SeeSoundSource));
	}
	return true;
}

// --- The two clocks and the committed source survive a save --------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcSoundSweepSaveTest,
	"Elysium.Substrate.NpcConditions.SoundSweepSave", GElysiumTestFlags)
bool FElysiumNpcSoundSweepSaveTest::RunTest(const FString&)
{
	FSweepFixture F;
	if (!TestNotNull(TEXT("npc"), F.Npc) || !TestNotNull(TEXT("owner"), F.Owner))
	{
		return false;
	}
	FElysiumNpcMemory& Memory = F.Npc->Senses.Memory;

	// `CommitBestSound` is what writes the source the tail reads. It has no runtime caller until
	// 10d installs the selectors, so this is also the assertion that it writes the field at all.
	F.Record(Memory.LastSoundCombat, F.Owner, FVector(100.0, 0.0, 0.0));
	F.Npc->Senses.CommitBestSound(FElysiumNpcConditions::Of({ ECond::HearCombat }));
	TestTrue(TEXT("CommitBestSound copies the winner's owner into the committed source"),
		Memory.BestSoundSource == F.Owner->Handle);

	Memory.NextInvestigateSoundTime = 12.25;
	Memory.NextSeeSoundSourceTime = 3.5;

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
	TestTrue(TEXT("the investigate gate round-trips"),
		FMath::IsNearlyEqual(Restored.NextInvestigateSoundTime, 12.25));
	TestTrue(TEXT("the see-source limit round-trips"),
		FMath::IsNearlyEqual(Restored.NextSeeSoundSourceTime, 3.5));

	// The archive writes a handle as `{Index, bWasValid}` and DROPS the epoch by design
	// (`ElysiumSaveArchive.h`): re-stamping belongs to the applier, so the committed source is only
	// comparable after `Rebase`. Asserting equality before it would be asserting against the
	// save format rather than against this field.
	TestTrue(TEXT("the committed source keeps its index across the round trip"),
		Restored.BestSoundSource.Index == F.Owner->Handle.Index);
	Restored.Rebase(F.World);
	TestTrue(TEXT("...and rebases to the live handle"),
		Restored.BestSoundSource == F.Owner->Handle);

	// An index the restored world has no slot for drops rather than pointing at whatever now
	// occupies it. (The epoch cannot carry staleness here — the archive never wrote one.)
	Restored.BestSoundSource.Index = F.World.Entities().Num() + 64;
	Restored.Rebase(F.World);
	TestFalse(TEXT("an unresolvable committed source rebases to invalid"),
		Restored.BestSoundSource.IsSet());
	return true;
}

}   // namespace ElysiumNpcSoundSweepTests

#endif // WITH_DEV_AUTOMATION_TESTS
