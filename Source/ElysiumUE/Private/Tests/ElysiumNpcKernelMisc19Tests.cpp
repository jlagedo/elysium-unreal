// Story 0019/8 (29e under the strict verdict), family **Misc19** -- the family's tests (lane L11).
//
// Test names carry `Elysium.Substrate.NpcKernelMisc19.` and the retail address. Every assertion is
// read off the packet (`families-19-29/Misc19-READING.md`) and, where the packet's walk was judged,
// its second-judge rows; the listing address sits in the assertion text.
//
// Owns (Misc19's `rule` rows): 0x10279a50 SetEnemy, 0x10279dd0 CAI_BaseNPC::ChooseEnemy, 0x102b4f60
// CAI_BaseNPCTroika::FUN_102b4f60, 0x102b4fe0 CAI_BaseNPCTroika::FUN_102b4fe0, 0x1035dc30
// CNPC_VAndreiBlood::Activate, 0x10365a90 FUN_10365a90, 0x103cfc50
// CNPC_VWerewolf::CheckAllMoveHints, 0x1026cdc0 CAI_BaseNPC::EnterGrappleState, 0x1026cec0
// CAI_BaseNPC::FUN_1026cec0, 0x102b4cc0 CAI_BaseNPCTroika::FUN_102b4cc0, 0x10372c50
// CNPC_VCop::vfunc596, 0x10372dd0 CNPC_VCop::vfunc598, 0x10395ce0 FUN_10395ce0, 0x1039ea60
// FUN_1039ea60, 0x103a3850 CNPC_VPedestrian::vfunc27, 0x101a98c0 CCineAISchedule::vfunc586,
// 0x101aade0 CPayphone::EnterGrappleState, 0x102b5c00 CAI_BaseNPCTroika::EnterGrappleState,
// 0x1037b500 CNPC_VGhoulCroucher::EnterGrappleState, 0x1017f4a0 PlayerSupernaturalIncident,
// 0x10274e30 CAI_BaseNPC::HandleAnimEvent, 0x1029b290 CAI_BaseNPCTroika::HandleAnimEvent,
// 0x10374280 CNPC_VDog::HandleAnimEvent, 0x103786c0 CNPC_VGargoyle::HandleAnimEvent, 0x1037fb60
// CNPC_VHengeyokai::HandleAnimEvent, 0x1038e000 CNPC_VManBat::HandleAnimEvent, 0x10392a70
// CNPC_VMingXiao::HandleAnimEvent, 0x103a7000 CNPC_VSabbatLeader::HandleAnimEvent, 0x103ba410
// CNPC_VTzimisce::HandleAnimEvent, 0x103c1540 CNPC_VTzimisceHeadClaw::HandleAnimEvent, 0x103d88e0
// CNPC_VWerewolf::HandleAnimEvent.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Tests/ElysiumTestServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

namespace ElysiumLaw
{
	// `debug_show_cs_acts`, the mutable ConVar word `ElysiumLaw.cpp` defines (default 0).
	extern int32 GLawDebugShowCsActs;
}

static constexpr EAutomationTestFlags GMisc19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One guard of a chosen retail class, one Troika bystander, the player, and a counter wired off
	// every output this family fires. Nothing thinks on its own (`Quiet`); each case drives a body.
	struct FMisc19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FMisc19Fixture(const TCHAR* GuardClass = TEXT("CNPC_VHumanCombatant"))
			: World([GuardClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("misc19_kernel"), 1119);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, GuardClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					for (const TCHAR* Output : { TEXT("OnLostEnemy"), TEXT("OnLostPlayer"),
						TEXT("OnGrappleBegin"), TEXT("OnFedUponBegin") })
					{
						const FString Counter = FString::Printf(TEXT("c_%s"), Output);
						Builder.AddCounter(*Counter);
						Builder.WireOutput(TEXT("guard"), Output, *Counter);
					}
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
		}

		// Let the event queue deliver what a body fired.
		void Flush()
		{
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
			World.World.Tick(World.World.NowSeconds());
		}

		float Counter(const TCHAR* Output)
		{
			return World.Counter(*FString::Printf(TEXT("c_%s"), Output));
		}

		void Hate(FElysiumEntity* Target, int32 Priority = 10)
		{
			Guard->Relationships.SetEntity(Target->Handle, EElysiumRelationship::Hate, Priority);
			Guard->EnemyMemory.Update(*Guard, Target->Handle, World.World.NowSeconds());
		}
	};

	FElysiumEntityHandle Misc19StaleHandle(const FElysiumEntityHandle& Live)
	{
		return FElysiumEntityHandle(Live.Index, Live.Epoch + 1);
	}
}

// =================================================================================================
// 0x10279a50 SetEnemy
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19SetEnemyTest,
	"Elysium.Substrate.NpcKernelMisc19.SetEnemy", GMisc19Flags)
bool FElysiumNpcKernelMisc19SetEnemyTest::RunTest(const FString&)
{
	// `SetEnemy` `0x10279a50`.
	FMisc19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	N.BaseMemory.LastEnemy = FElysiumEntityHandle::Invalid();

	// A change from a NULL old enemy: no last-enemy write, attack conditions cleared, handle written,
	// discipline sweep counted.
	N.Cognition.Conditions.Set(EElysiumNpcCond::CanMeleeAttack1);
	const int32 Sweeps0 = N.SetEnemyDisciplineStripCalls;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	TestTrue(TEXT("0x10279b01 writes m_hEnemy"), N.BaseMemory.Enemy == F.Other->Handle);
	TestFalse(TEXT("0x10279a96 a null old enemy never reaches 0x10279b70"), N.BaseMemory.LastEnemy.IsSet());
	TestFalse(TEXT("0x10279aed slot 560 clears the attack conditions on a change"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::CanMeleeAttack1));
	TestEqual(TEXT("0x10279b0c the break-on-notice sweep runs on a non-null write"),
		N.SetEnemyDisciplineStripCalls, Sweeps0 + 1);

	// The same enemy again: no clear, but the write and the sweep still run.
	N.Cognition.Conditions.Set(EElysiumNpcCond::CanMeleeAttack1);
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	TestTrue(TEXT("0x10279a8b an unchanged enemy skips slot 560"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::CanMeleeAttack1));
	TestEqual(TEXT("0x10279b0c the sweep runs on an unchanged enemy too"),
		N.SetEnemyDisciplineStripCalls, Sweeps0 + 2);

	// A change from a LIVE old enemy: `m_hLastEnemy` takes it.
	ElysiumNpcEnemy::SetEnemy(N, F.Player->Handle);
	TestTrue(TEXT("0x10279ae4 the live old enemy goes to m_hLastEnemy"),
		N.BaseMemory.LastEnemy == F.Other->Handle);
	TestTrue(TEXT("0x10279b01 the new handle is written"), N.BaseMemory.Enemy == F.Player->Handle);

	// A STALE old handle is a null old enemy: the last enemy is left alone.
	N.BaseMemory.Enemy = Misc19StaleHandle(F.Other->Handle);
	N.BaseMemory.LastEnemy = F.Player->Handle;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	TestTrue(TEXT("0x10279a7d a stale old handle skips the last-enemy write"),
		N.BaseMemory.LastEnemy == F.Player->Handle);

	// NULL: `-1` written and no sweep.
	const int32 Sweeps1 = N.SetEnemyDisciplineStripCalls;
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	TestFalse(TEXT("0x10279b16 null writes 0xffffffff"), N.BaseMemory.Enemy.IsSet());
	TestTrue(TEXT("0x10279ae4 the change still transfers the live old enemy"),
		N.BaseMemory.LastEnemy == F.Other->Handle);
	TestEqual(TEXT("0x10279af5 no sweep on a null write"), N.SetEnemyDisciplineStripCalls, Sweeps1);
	return true;
}

// =================================================================================================
// 0x10279dd0 CAI_BaseNPC::ChooseEnemy
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19ChooseEnemyAcquireTest,
	"Elysium.Substrate.NpcKernelMisc19.ChooseEnemy.Acquire", GMisc19Flags)
bool FElysiumNpcKernelMisc19ChooseEnemyAcquireTest::RunTest(const FString&)
{
	// `CAI_BaseNPC::ChooseEnemy` `0x10279dd0`: no running program forces all three interrupt answers
	// (`0x10279eb9`), slot 480 asks, slot 478 answers, the change work runs.
	FMisc19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.Schedule.Clear();
	N.BaseScheduleHost.MemoryBits = 0x20000u;
	F.Hate(F.Other);
	TestTrue(TEXT("0x1027a105 answers whether an enemy is now held"), ElysiumNpcEnemy::ChooseEnemy(N));
	TestTrue(TEXT("0x1027a077 SetEnemy(best)"), N.BaseMemory.Enemy == F.Other->Handle);
	TestTrue(TEXT("0x1027a06f NEW_ENEMY set for a non-null choice"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));
	TestEqual(TEXT("0x1027a0ff a non-player enemy ORs 0x8000"), N.BaseScheduleHost.MemoryBits & 0x18000u, 0x8000u);
	TestEqual(TEXT("0x1027a082 no entry enemy bit: the squad slot word 0x20000 is kept"),
		N.BaseScheduleHost.MemoryBits & 0x20000u, 0x20000u);

	// Second pass: slot 478 answers the same actor and the entry byte is clear -> nothing changes.
	N.Cognition.Conditions.Clear(EElysiumNpcCond::NewEnemy);
	TestTrue(TEXT("0x1027a013 the same actor with no went-null byte returns held"),
		ElysiumNpcEnemy::ChooseEnemy(N));
	TestFalse(TEXT("0x1027a013 ...and skips the change work"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19ChooseEnemyGateTest,
	"Elysium.Substrate.NpcKernelMisc19.ChooseEnemy.Gate", GMisc19Flags)
bool FElysiumNpcKernelMisc19ChooseEnemyGateTest::RunTest(const FString&)
{
	// `0x10279f30`-`0x10279f44`: a running program that lists neither NEW_ENEMY nor (with nothing
	// lost) LOST_ENEMY keeps ownership: the current enemy is answered and nothing is written.
	FMisc19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	F.Hate(F.Player, 99);
	const FElysiumNpcConditions Empty;
	const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Empty);
	TestTrue(TEXT("the uninterested program installs"),
		ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N));
	N.BaseScheduleHost.MemoryBits = 0x8000u;
	TestTrue(TEXT("0x10279f5d the gate answers current != null"), ElysiumNpcEnemy::ChooseEnemy(N));
	TestTrue(TEXT("0x10279f44 the enemy is kept"), N.BaseMemory.Enemy == F.Other->Handle);
	TestEqual(TEXT("0x10279f44 m_afMemory untouched"), N.BaseScheduleHost.MemoryBits, 0x8000u);

	// The same program listing NEW_ENEMY lets the choice run: the hated player replaces the enemy.
	N.Schedule.Clear();
	FElysiumNpcConditions Mask;
	Mask.Set(EElysiumNpcCond::NewEnemy);
	const ElysiumSchedule::FInterruptMaskScope Listening(ElysiumSched::IDLE_STAND, Mask);
	TestTrue(TEXT("the listening program installs"),
		ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N));
	ElysiumNpcEnemy::ChooseEnemy(N);
	TestTrue(TEXT("0x10279ebf NEW_ENEMY interrupting opens the choice"), N.BaseMemory.Enemy.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19ChooseEnemyLostTest,
	"Elysium.Substrate.NpcKernelMisc19.ChooseEnemy.Lost", GMisc19Flags)
bool FElysiumNpcKernelMisc19ChooseEnemyLostTest::RunTest(const FString&)
{
	// The went-null arm (`0x10279e85`-`0x10279e8b`) and the lost outputs (`0x1027a097`-`0x1027a0da`).
	{
		FMisc19Fixture F;
		if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		N.Schedule.Clear();
		N.BaseMemory.Enemy = Misc19StaleHandle(F.Other->Handle);
		N.BaseScheduleHost.MemoryBits = 0x8000u | 0x20000u;
		TestFalse(TEXT("0x1027a105 nothing held after the enemy went null"), ElysiumNpcEnemy::ChooseEnemy(N));
		TestTrue(TEXT("0x1027a0b0 LOST_ENEMY set"), N.Cognition.Conditions.Has(EElysiumNpcCond::LostEnemy));
		TestFalse(TEXT("0x1027a059 NEW_ENEMY cleared for a null choice"),
			N.Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));
		TestEqual(TEXT("0x1027a027 / 0x1027a08b the enemy and squad-slot bits cleared"),
			N.BaseScheduleHost.MemoryBits & 0x38000u, 0u);
		F.Flush();
		TestEqual(TEXT("0x1027a0da OnLostEnemy when the entry word lacked 0x10000"),
			F.Counter(TEXT("OnLostEnemy")), 1.f);
		TestEqual(TEXT("...and not OnLostPlayer"), F.Counter(TEXT("OnLostPlayer")), 0.f);
	}
	{
		FMisc19Fixture F;
		if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		N.Schedule.Clear();
		// An ELUDED player enemy: slot 478 excludes it, so the choice is null.
		ElysiumNpcEnemy::SetEnemy(N, F.Player->Handle);
		N.EnemyMemory.Update(N, F.Player->Handle, 0.0);
		N.EnemyMemory.MarkEluded(F.Player->Handle);
		N.BaseScheduleHost.MemoryBits = 0x10000u;
		TestFalse(TEXT("0x10279efa an eluded enemy is lost"), ElysiumNpcEnemy::ChooseEnemy(N));
		TestTrue(TEXT("0x1027a0b0 LOST_ENEMY for the eluded arm"),
			N.Cognition.Conditions.Has(EElysiumNpcCond::LostEnemy));
		F.Flush();
		TestEqual(TEXT("0x1027a0cd OnLostPlayer when the entry word carried 0x10000"),
			F.Counter(TEXT("OnLostPlayer")), 1.f);
		TestEqual(TEXT("0x1027a0ff a player choice would OR 0x10000; this null one ORs nothing"),
			N.BaseScheduleHost.MemoryBits & 0x18000u, 0u);
	}
	{
		// The went-null WARNING arm falls through (`0x10279fd7`): a program interested in neither
		// NEW_ENEMY nor LOST_ENEMY still gets the change work.
		FMisc19Fixture F;
		if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		const FElysiumNpcConditions Empty;
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Empty);
		TestTrue(TEXT("the uninterested program installs"),
			ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N));
		N.BaseMemory.Enemy = Misc19StaleHandle(F.Other->Handle);
		N.BaseScheduleHost.MemoryBits = 0x8000u;
		ElysiumNpcEnemy::ChooseEnemy(N);
		TestTrue(TEXT("0x10279f28 went-null falls through to the change work"),
			N.Cognition.Conditions.Has(EElysiumNpcCond::LostEnemy));
		TestEqual(TEXT("0x1027a027 the enemy bits cleared"), N.BaseScheduleHost.MemoryBits & 0x18000u, 0u);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19ChooseEnemyDeadTest,
	"Elysium.Substrate.NpcKernelMisc19.ChooseEnemy.Dead", GMisc19Flags)
bool FElysiumNpcKernelMisc19ChooseEnemyDeadTest::RunTest(const FString&)
{
	// `0x1027a02d`-`0x1027a04c`: a dead OLD enemy SETS ENEMY_DEAD in the change work (second judge).
	FMisc19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.Schedule.Clear();
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	// Retail's dead enemy is `m_lifeState != 0` (slot 158 false) while its handle still resolves.
	// The port's `Kill()` is `UTIL_Remove` (the handle stops resolving); the life-state word is what
	// slot 158 reads (`FElysiumEntity::IsAlive`), so the case stands the word (LIFE_DYING, as
	// `Event_Killed` `0x1032b9b0` writes it).
	F.Other->AnimEventLifeStateWord = 1;
	if (!TestNotNull(TEXT("the dead enemy still resolves this frame"),
			static_cast<const FElysiumNpcBase&>(N).GetEnemy()))
	{
		return false;
	}
	F.Hate(F.Player);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::EnemyDead);
	ElysiumNpcEnemy::ChooseEnemy(N);
	TestTrue(TEXT("0x1027a04c ENEMY_DEAD set for a dead old enemy"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::EnemyDead));
	TestTrue(TEXT("0x1027a077 the new enemy replaces it"), N.BaseMemory.Enemy == F.Player->Handle);
	TestEqual(TEXT("0x1027a0ff the PLAYER ORs 0x10000"), N.BaseScheduleHost.MemoryBits & 0x18000u, 0x10000u);
	return true;
}

// =================================================================================================
// Slots 595 / 596 / 598
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19Slot596Test,
	"Elysium.Substrate.NpcKernelMisc19.Slot596", GMisc19Flags)
bool FElysiumNpcKernelMisc19Slot596Test::RunTest(const FString&)
{
	// `CAI_BaseNPCTroika::FUN_102b4f60` `0x102b4f60`.
	FMisc19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	N.Slot596(nullptr);
	TestFalse(TEXT("0x102b4f69 a null argument does nothing"), N.BaseMemory.Enemy.IsSet());
	N.Slot596(F.Other);
	TestTrue(TEXT("0x102b4f78 SetEnemy on the (unredirected) entity"), N.BaseMemory.Enemy == F.Other->Handle);
	TestNotNull(TEXT("0x102b4f94 slot 544 wrote the memory row"), N.EnemyMemory.Find(F.Other->Handle));
	TestTrue(TEXT("0x102707d0 a non-summoned entity passes the redirect unchanged"),
		FElysiumNpcBase::SummonerRedirect(F.Other) == F.Other);
	TestNull(TEXT("0x102707d0 null passes through"), FElysiumNpcBase::SummonerRedirect(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19Slot598Test,
	"Elysium.Substrate.NpcKernelMisc19.Slot598", GMisc19Flags)
bool FElysiumNpcKernelMisc19Slot598Test::RunTest(const FString&)
{
	// `CAI_BaseNPCTroika::FUN_102b4fe0` `0x102b4fe0`.
	FMisc19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other)
		|| !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.BaseMemory.LastEnemy = F.Other->Handle;
	const int32 Clears0 = N.ClearEnemyMemoryRecordCalls;
	N.Slot598(F.Other);
	TestTrue(TEXT("0x102b4fed AddEntityRelationship(entity, D_NU, 0) writes an entity row"),
		N.Relationships.HasEntity(F.Other->Handle));
	TestFalse(TEXT("0x102b5004 the entity WAS the enemy: SetEnemy(NULL)"), N.BaseMemory.Enemy.IsSet());
	TestFalse(TEXT("0x102b503d the entity WAS the last enemy: 0x10279b70(NULL)"), N.BaseMemory.LastEnemy.IsSet());
	TestEqual(TEXT("0x102b5067 ClearMemory on slot 541"), N.ClearEnemyMemoryRecordCalls, Clears0 + 1);
	TestTrue(TEXT("0x102b5067 ...for that entity"), N.LastClearedEnemyMemoryRecord == F.Other->Handle);

	// A different entity leaves enemy and last enemy alone.
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.BaseMemory.LastEnemy = F.Other->Handle;
	N.Slot598(F.Player);
	TestTrue(TEXT("0x102b4ffe a non-matching entity keeps the enemy"), N.BaseMemory.Enemy == F.Other->Handle);
	TestTrue(TEXT("0x102b5037 ...and the last enemy"), N.BaseMemory.LastEnemy == F.Other->Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19Slot595Test,
	"Elysium.Substrate.NpcKernelMisc19.AcquireNearestHatedTarget", GMisc19Flags)
bool FElysiumNpcKernelMisc19Slot595Test::RunTest(const FString&)
{
	// `CAI_BaseNPCTroika::FUN_102b4cc0` `0x102b4cc0`, slot 595.
	FMisc19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	N.AcquireNearestHatedTarget();
	TestFalse(TEXT("0x102b4e40 nobody D_HT: slot 596 is never called"), N.BaseMemory.Enemy.IsSet());

	N.Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 10);
	const TArray<FElysiumEntity*> Box = N.AcquireTargetBoxQuery(
		N.Origin - FVector(1024.f * ElysiumMove::U), N.Origin + FVector(1024.f * ElysiumMove::U), 0x20);
	TestTrue(TEXT("0x101ccc80 the Troika bystander is in the box"), Box.Contains(F.Other));
	TestFalse(TEXT("0x101ccbf0 m_edtDerivedType 0x40: the player is never a candidate"),
		F.Player != nullptr && Box.Contains(F.Player));
	N.AcquireNearestHatedTarget();
	TestTrue(TEXT("0x102b4ea8 the nearest D_HT candidate goes to slot 596"), N.BaseMemory.Enemy == F.Other->Handle);
	return true;
}

// =================================================================================================
// Slot 379: 0x1026cdc0 base, 0x102b5c00 Troika, 0x101aade0 payphone, 0x1037b500 ghoul croucher
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19BaseEnterGrappleStateTest,
	"Elysium.Substrate.NpcKernelMisc19.BaseEnterGrappleState", GMisc19Flags)
bool FElysiumNpcKernelMisc19BaseEnterGrappleStateTest::RunTest(const FString&)
{
	// `CAI_BaseNPC::EnterGrappleState` `0x1026cdc0`.
	FMisc19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	const int32 Oblivious0 = N.ObliviousCount;
	const int32 Disconnected0 = N.BaseScheduleHost.SquadDisconnected;
	N.FElysiumNpcBase::EnterGrappleState(F.Other->Handle, EElysiumGrappleRole::Victim,
		EElysiumGrappleType::Feed);
	TestFalse(TEXT("0x1026cdc4 -> 0x1026d130 SetEnemy(NULL)"), N.BaseMemory.Enemy.IsSet());
	TestEqual(TEXT("0x1026d130 ++m_iIsOblivious"), N.ObliviousCount, Oblivious0 + 1);
	TestEqual(TEXT("0x1026d050 ++m_iSquadDisconnected"), N.BaseScheduleHost.SquadDisconnected, Disconnected0 + 1);
	TestTrue(TEXT("0x1026cdfd CBaseCombatCharacter::EnterGrappleState wrote the pair"),
		N.Grapple.Partner == F.Other->Handle);
	F.Flush();
	TestEqual(TEXT("0x1026cdd7 m_OnGrappleBegin, for a non-stealth type too"),
		F.Counter(TEXT("OnGrappleBegin")), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19TroikaEnterGrappleStateTest,
	"Elysium.Substrate.NpcKernelMisc19.TroikaEnterGrappleState", GMisc19Flags)
bool FElysiumNpcKernelMisc19TroikaEnterGrappleStateTest::RunTest(const FString&)
{
	// `CAI_BaseNPCTroika::EnterGrappleState` `0x102b5c00`.
	{
		FMisc19Fixture F(TEXT("CAI_BaseNPCTroika"));
		if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		N.QueuedBurnDamage.AddDefaulted(2);
		const int32 Discharges0 = N.GrappleBurnDischarges;
		TestFalse(TEXT("0x102b5c5b a queued burn REFUSES the grapple"),
			N.EnterGrappleState(F.Other->Handle, EElysiumGrappleRole::Victim, EElysiumGrappleType::Feed));
		TestEqual(TEXT("0x102b5c45 each record is discharged into the partner"),
			N.GrappleBurnDischarges, Discharges0 + 2);
		TestEqual(TEXT("0x102b5c56 the list is left standing"), N.QueuedBurnDamage.Num(), 2);
		TestTrue(TEXT("0x102b5c23 the record is written in place (attacker = this)"),
			N.QueuedBurnDamage[0].Source == N.Handle);
		TestFalse(TEXT("0x102b5c5b the base never ran"), N.Grapple.Partner.IsSet());
	}
	{
		FMisc19Fixture F(TEXT("CAI_BaseNPCTroika"));
		if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
		{
			return false;
		}
		FElysiumNpc& N = *F.Guard;
		TestTrue(TEXT("idle program installs"), ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N));
		TestTrue(TEXT("0x102b5d26 an empty queue enters and answers TRUE"),
			N.EnterGrappleState(F.Other->Handle, EElysiumGrappleRole::Victim, EElysiumGrappleType::Feed));
		TestFalse(TEXT("0x102b5d1c ClearSchedule"), N.Schedule.IsRunning());
		TestTrue(TEXT("0x102b5c86 the base body ran"), N.Grapple.Partner == F.Other->Handle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19PayphoneEnterGrappleStateTest,
	"Elysium.Substrate.NpcKernelMisc19.PayphoneEnterGrappleState", GMisc19Flags)
bool FElysiumNpcKernelMisc19PayphoneEnterGrappleStateTest::RunTest(const FString&)
{
	// `CPayphone::EnterGrappleState` `0x101aade0`: the base body only.
	FMisc19Fixture F(TEXT("CPayphone"));
	FElysiumNpcPayphone* Phone = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcPayphone>() : nullptr;
	if (!TestNotNull(TEXT("payphone"), Phone) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	Phone->QueuedBurnDamage.AddDefaulted(1);
	TestTrue(TEXT("idle program installs"), ElysiumSchedule::Start(Phone->Schedule, ElysiumSched::IDLE_STAND, *Phone));
	const bool bEntered = Phone->EnterGrappleState(F.Other->Handle, EElysiumGrappleRole::Victim,
		EElysiumGrappleType::Payphone);
	TestTrue(TEXT("0x101aae03 no queued-burn refusal: the Troika half never runs"), bEntered);
	TestTrue(TEXT("0x101aae03 no ClearSchedule either"), Phone->Schedule.IsRunning());
	TestTrue(TEXT("0x1026cdfd the base wrote the pair"), Phone->Grapple.Partner == F.Other->Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19GhoulCroucherEnterGrappleStateTest,
	"Elysium.Substrate.NpcKernelMisc19.GhoulCroucherEnterGrappleState", GMisc19Flags)
bool FElysiumNpcKernelMisc19GhoulCroucherEnterGrappleStateTest::RunTest(const FString&)
{
	// `CNPC_VGhoulCroucher::EnterGrappleState` `0x1037b500`.
	FMisc19Fixture F(TEXT("CNPC_VGhoulCroucher"));
	FElysiumNpcGhoulCroucher* Ghoul =
		F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcGhoulCroucher>() : nullptr;
	if (!TestNotNull(TEXT("croucher"), Ghoul) || !TestNotNull(TEXT("player"), F.Player)
		|| !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	Ghoul->bGhoulSpawnBurning = true;
	TestFalse(TEXT("0x1037b527 a burning croucher refuses the PLAYER's grapple"),
		Ghoul->EnterGrappleState(F.Player->Handle, EElysiumGrappleRole::Victim, EElysiumGrappleType::Feed));
	TestFalse(TEXT("0x1037b522 ...and burns instead of entering"), Ghoul->Grapple.Partner.IsSet());
	TestTrue(TEXT("0x1037b51a a non-player partner goes to the Troika body"),
		Ghoul->EnterGrappleState(F.Other->Handle, EElysiumGrappleRole::Victim, EElysiumGrappleType::Feed));
	Ghoul->LeaveGrappleState();
	Ghoul->bGhoulSpawnBurning = false;
	TestTrue(TEXT("0x1037b50c not burning: the Troika body enters the player's grapple"),
		Ghoul->EnterGrappleState(F.Player->Handle, EElysiumGrappleRole::Victim, EElysiumGrappleType::Feed));
	return true;
}

// =================================================================================================
// 0x1026cec0 CAI_BaseNPC slot 354
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19Slot354Test,
	"Elysium.Substrate.NpcKernelMisc19.Slot354", GMisc19Flags)
bool FElysiumNpcKernelMisc19Slot354Test::RunTest(const FString&)
{
	// `CAI_BaseNPC::FUN_1026cec0` `0x1026cec0`, the victim's feed-begin callback.
	FMisc19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	const int32 Oblivious0 = N.ObliviousCount;

	N.Grapple.Partner = F.Other->Handle;
	N.Grapple.Role = EElysiumGrappleRole::Victim;
	N.Grapple.Type = EElysiumGrappleType::Feed;
	N.Slot354();
	TestFalse(TEXT("0x1026cec3 0x1026d130 SetEnemy(NULL)"), N.BaseMemory.Enemy.IsSet());
	TestEqual(TEXT("0x1026cec3 ++m_iIsOblivious"), N.ObliviousCount, Oblivious0 + 1);
	F.Flush();
	TestEqual(TEXT("0x1026cf43 a live partner with a role fires OnFedUponBegin"),
		F.Counter(TEXT("OnFedUponBegin")), 1.f);

	N.Grapple.Type = EElysiumGrappleType::ZombieFeedsPlayer;
	N.Slot354();
	F.Flush();
	TestEqual(TEXT("0x1026cf0a grapple type 8 returns having fired nothing"),
		F.Counter(TEXT("OnFedUponBegin")), 1.f);
	TestEqual(TEXT("0x1026cec3 ...after the oblivious triple"), N.ObliviousCount, Oblivious0 + 2);

	N.Grapple.Role = EElysiumGrappleRole::None;
	N.Slot354();
	F.Flush();
	TestEqual(TEXT("0x1026cf01 no role fires with a null activator"), F.Counter(TEXT("OnFedUponBegin")), 2.f);

	N.Grapple.Partner = FElysiumEntityHandle::Invalid();
	N.Grapple.Role = EElysiumGrappleRole::Victim;
	N.Grapple.Type = EElysiumGrappleType::ZombieFeedsPlayer;
	N.Slot354();
	F.Flush();
	TestEqual(TEXT("0x1026ced7 no live partner skips the type-8 gate and fires"),
		F.Counter(TEXT("OnFedUponBegin")), 3.f);
	return true;
}

// =================================================================================================
// Species rows: Cop 596 / 598, Pedestrian 27, AndreiBlood Activate
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19CopSlot596Test,
	"Elysium.Substrate.NpcKernelMisc19.CopSlot596", GMisc19Flags)
bool FElysiumNpcKernelMisc19CopSlot596Test::RunTest(const FString&)
{
	// `CNPC_VCop::vfunc596` `0x10372c50`.
	FMisc19Fixture F(TEXT("CNPC_VCop"));
	FElysiumNpcCop* Cop = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcCop>() : nullptr;
	if (!TestNotNull(TEXT("cop"), Cop) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	ElysiumNpcEnemy::SetEnemy(*Cop, FElysiumEntityHandle::Invalid());
	const int32 Sweeps0 = Cop->SetEnemyDisciplineStripCalls;
	Cop->Slot596(nullptr);
	TestFalse(TEXT("0x10372c5a a null argument reaches the Troika body with null"), Cop->BaseMemory.Enemy.IsSet());
	Cop->Slot596(F.Other);
	TestTrue(TEXT("0x10372c68 SetEnemy(resolved)"), Cop->BaseMemory.Enemy == F.Other->Handle);
	TestNotNull(TEXT("0x10372c84 slot 544"), Cop->EnemyMemory.Find(F.Other->Handle));
	TestEqual(TEXT("0x10372c8e the Troika body repeats SetEnemy (two sweeps)"),
		Cop->SetEnemyDisciplineStripCalls, Sweeps0 + 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19CopSlot598Test,
	"Elysium.Substrate.NpcKernelMisc19.CopSlot598", GMisc19Flags)
bool FElysiumNpcKernelMisc19CopSlot598Test::RunTest(const FString&)
{
	// `CNPC_VCop::vfunc598` `0x10372dd0`.
	FMisc19Fixture F(TEXT("CNPC_VCop"));
	FElysiumNpcCop* Cop = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcCop>() : nullptr;
	if (!TestNotNull(TEXT("cop"), Cop) || !TestNotNull(TEXT("player"), F.Player)
		|| !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	// The closest player IS the argument: the forgive arm.
	Cop->Senses.Memory.ClosestPlayer = F.Player->Handle;
	ElysiumNpcEnemy::SetEnemy(*Cop, F.Player->Handle);
	Cop->BaseMemory.LastEnemy = F.Player->Handle;
	Cop->CopOldPlayerRelationType = 7;
	const int32 Clears0 = Cop->ClearEnemyMemoryRecordCalls;
	Cop->Slot598(F.Player);
	TestEqual(TEXT("0x10372e1e m_eOldPlayerRelationType := 0"), Cop->CopOldPlayerRelationType, 0);
	TestFalse(TEXT("0x10372e36 SetEnemy(NULL)"), Cop->BaseMemory.Enemy.IsSet());
	TestFalse(TEXT("0x10372e71 last enemy cleared"), Cop->BaseMemory.LastEnemy.IsSet());
	TestEqual(TEXT("0x10372e9b ClearMemory"), Cop->ClearEnemyMemoryRecordCalls, Clears0 + 1);
	TestTrue(TEXT("0x10372ea9 InputSetRelationship(\"Player D_NU 10\") wrote the player row"),
		Cop->Relationships.HasEntity(F.Player->Handle));

	// Any other entity falls through to the Troika body `0x102b4fe0` with none of that run.
	Cop->CopOldPlayerRelationType = 7;
	ElysiumNpcEnemy::SetEnemy(*Cop, F.Other->Handle);
	Cop->Slot598(F.Other);
	TestEqual(TEXT("0x10372eb4 the Troika route leaves +0x6668"), Cop->CopOldPlayerRelationType, 7);
	TestFalse(TEXT("0x102b5004 ...and the Troika route cleared the enemy"), Cop->BaseMemory.Enemy.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19PedestrianSlot27Test,
	"Elysium.Substrate.NpcKernelMisc19.PedestrianSlot27", GMisc19Flags)
bool FElysiumNpcKernelMisc19PedestrianSlot27Test::RunTest(const FString&)
{
	// `CNPC_VPedestrian::vfunc27` `0x103a3850`.
	FMisc19Fixture F(TEXT("CNPC_VPedestrian"));
	FElysiumNpcPedestrian* Ped = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcPedestrian>() : nullptr;
	if (!TestNotNull(TEXT("pedestrian"), Ped) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	ElysiumNpcEnemy::SetEnemy(*Ped, FElysiumEntityHandle::Invalid());
	const int32 State0 = Ped->NpcStateRetail();
	Ped->Slot27(F.Other);
	TestTrue(TEXT("0x103a3863 slot 596 adopts the entity as enemy"), Ped->BaseMemory.Enemy == F.Other->Handle);
	TestEqual(TEXT("0x103a3884 m_IdealNPCState := 8 by direct write"), Ped->IdealStateRetail(), 8);
	TestEqual(TEXT("0x103a3884 ...and m_NPCState is untouched"), Ped->NpcStateRetail(), State0);
	TestEqual(TEXT("0x103a388e m_iForcedSchedule := 0x15a"), Ped->ScheduleHost.ForcedSchedule, 0x15a);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19AndreiBloodActivateTest,
	"Elysium.Substrate.NpcKernelMisc19.AndreiBloodActivate", GMisc19Flags)
bool FElysiumNpcKernelMisc19AndreiBloodActivateTest::RunTest(const FString&)
{
	// `CNPC_VAndreiBlood::Activate` `0x1035dc30`.
	FMisc19Fixture F(TEXT("CNPC_VAndreiBlood"));
	FElysiumNpcAndreiBlood* Blood =
		F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcAndreiBlood>() : nullptr;
	if (!TestNotNull(TEXT("andrei blood"), Blood))
	{
		return false;
	}
	Blood->WriteNpcStateRetail(1);
	Blood->WriteIdealStateRetail(1);
	const int32 SetSchedule0 = Blood->SetScheduleRetailCalls;
	Blood->Activate();
	TestEqual(TEXT("0x1035dc93 m_NPCState := 2 by direct write"), Blood->NpcStateRetail(), 2);
	TestEqual(TEXT("0x1035dc99 m_IdealNPCState := 2 by direct write"), Blood->IdealStateRetail(), 2);
	(void)SetSchedule0;
	return true;
}

// =================================================================================================
// 0x1017f4a0 PlayerSupernaturalIncident
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19PlayerSupernaturalIncidentTest,
	"Elysium.Substrate.NpcKernelMisc19.PlayerSupernaturalIncident", GMisc19Flags)
bool FElysiumNpcKernelMisc19PlayerSupernaturalIncidentTest::RunTest(const FString&)
{
	// `PlayerSupernaturalIncident` `0x1017f4a0`, arm 0 (the debug-gated witness stamp).
	FMisc19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	F.Guard->Witness.SupernaturalWitnessedTime = -1.0;
	ElysiumLaw::GLawDebugShowCsActs = 0;
	ElysiumLaw::PlayerSupernaturalIncident(*F.Player, 1, F.Guard->Handle, F.Guard->Origin);
	TestEqual(TEXT("0x1017f4b5 shipped debug_show_cs_acts 0: the witness stamp never happens"),
		F.Guard->Witness.SupernaturalWitnessedTime, -1.0);

	ElysiumLaw::GLawDebugShowCsActs = 1;
	const double Now = F.World.World.NowSeconds();
	ElysiumLaw::PlayerSupernaturalIncident(*F.Player, 1, F.Guard->Handle, F.Guard->Origin);
	ElysiumLaw::GLawDebugShowCsActs = 0;
	TestTrue(TEXT("0x1017f4d9 enabled: +0x63a8 := curtime + 20.0"),
		FMath::IsNearlyEqual(F.Guard->Witness.SupernaturalWitnessedTime, Now + 20.0, 0.01));
	return true;
}


// =================================================================================================
// 0x1029b290 CAI_BaseNPCTroika::HandleAnimEvent
// =================================================================================================

namespace
{
	FElysiumAnimEvent Misc19Ev(int32 Id, const TCHAR* Options = TEXT(""))
	{
		FElysiumAnimEvent Event;
		Event.Event = Id;
		Event.Options = Options;
		return Event;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19TroikaHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.TroikaHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19TroikaHandleAnimEventTest::RunTest(const FString&)
{
	// `CAI_BaseNPCTroika::HandleAnimEvent` `0x1029b290`.
	FMisc19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;

	// 0x7e5 -> 0x10293d70: m_afMemory &= ~0x2000.
	N.BaseScheduleHost.MemoryBits = 0x2000u | 0x8000u;
	TestTrue(TEXT("0x1029b2da 0x7e5 is claimed"), N.HandleAnimEvent(Misc19Ev(0x7e5)));
	TestEqual(TEXT("0x10293d70 clears only 0x2000"), N.BaseScheduleHost.MemoryBits, 0x8000u);

	// 0x7d5: the option is the sample, CHAN_AUTO, level 0x42.
	Sounds.Reset();
	TestTrue(TEXT("0x1029b3c6 0x7d5 is claimed"), N.HandleAnimEvent(Misc19Ev(0x7d5, TEXT("npc/foo.wav"))));
	if (TestEqual(TEXT("0x1029b46c one EmitSound"), Sounds.Num(), 1))
	{
		TestEqual(TEXT("0x1029b46c the option is the sample"), Sounds[0].Rel, FString(TEXT("npc/foo.wav")));
		TestEqual(TEXT("0x1029b46c soundlevel 0x42"), Sounds[0].SoundLevelDb, 0x42);
		TestTrue(TEXT("0x1029b46c CHAN_AUTO"), Sounds[0].Channel == EElysiumSoundChannel::Auto);
	}

	// 0x7f8 with no m_hTargetEnt: "Weapon stolen by someone else".
	N.SetTarget(FElysiumEntityHandle::Invalid());
	N.Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	TestTrue(TEXT("0x1029b2ee 0x7f8 is claimed"), N.HandleAnimEvent(Misc19Ev(0x7f8)));
	TestTrue(TEXT("0x1029b3b3 slot 448 TaskFail"), N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestEqual(TEXT("0x1029b3b3 ...with the stolen text"), N.LastTaskFailText,
		FString(TEXT("Weapon stolen by someone else")));

	// 0x80c: sscanf count <= 1 or total <= 0 return; otherwise SetExpression.
	const int32 Expr0 = N.SetExpressionMisc19Calls;
	N.HandleAnimEvent(Misc19Ev(0x80c, TEXT("smile")));
	TestEqual(TEXT("0x1029b5c2 one field is not enough"), N.SetExpressionMisc19Calls, Expr0);
	N.HandleAnimEvent(Misc19Ev(0x80c, TEXT("smile 0")));
	TestEqual(TEXT("0x1029b5d9 a total <= 0 returns"), N.SetExpressionMisc19Calls, Expr0);
	TestTrue(TEXT("0x1029b2b6 0x80c is claimed"), N.HandleAnimEvent(Misc19Ev(0x80c, TEXT("smile 1.0 0.8 0.8"))));
	TestEqual(TEXT("0x1029b656 SetExpression"), N.SetExpressionMisc19Calls, Expr0 + 1);

	// 0x80d: an empty option returns; a name sets the expression.
	N.HandleAnimEvent(Misc19Ev(0x80d));
	TestEqual(TEXT("0x1029b6bb an empty option returns"), N.SetExpressionMisc19Calls, Expr0 + 1);
	N.HandleAnimEvent(Misc19Ev(0x80d, TEXT("frown")));
	TestEqual(TEXT("0x1029b6f1 SetExpression"), N.SetExpressionMisc19Calls, Expr0 + 2);

	// 0x1036..0x103b outside TASK_DO_INTEREST_ACTIVITY: swallowed, silent.
	N.Schedule.Clear();
	Sounds.Reset();
	TestTrue(TEXT("0x1029b80b 0x1036 is claimed without the task"), N.HandleAnimEvent(Misc19Ev(0x1036, TEXT("bar"))));
	TestTrue(TEXT("0x1029b705 0x1038 is claimed without the task"), N.HandleAnimEvent(Misc19Ev(0x1038)));
	TestEqual(TEXT("0x1029b817 ...and nothing plays"), Sounds.Num(), 0);

	// 0x7d6: wounds taken := max health, then slots 144 / 403.
	TestTrue(TEXT("0x1029b474 0x7d6 is claimed"), N.HandleAnimEvent(Misc19Ev(0x7d6)));
	TestEqual(TEXT("0x1029b546 SetBaseToStatValue(0xf, 0x11)"),
		N.TypedStatValue(0, ElysiumSlot::Health), N.TypedStatValue(0, ElysiumSlot::MaxHealth));
	return true;
}

// =================================================================================================
// 0x10274e30 CAI_BaseNPC::HandleAnimEvent
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19BaseHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.BaseHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19BaseHandleAnimEventTest::RunTest(const FString&)
{
	// `CAI_BaseNPC::HandleAnimEvent` `0x10274e30`, driven directly (`N.FElysiumNpcBase::`).
	FMisc19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	TestTrue(TEXT("0x10274e50 0x3fc Bodygroup! is claimed"), N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x3fc)));

	// 1000 / 0x3f2 only in m_NPCState 4.
	N.Health = 50;
	N.MaxHealth = 100;
	N.WriteNpcStateRetail(1);
	TestTrue(TEXT("0x10274e72 1000 outside SCRIPT is claimed"), N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(1000)));
	TestEqual(TEXT("0x10274e72 ...and writes nothing"), N.Health, 50);
	N.WriteNpcStateRetail(4);
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(1000));
	TestEqual(TEXT("0x10274e82 m_iHealth := 0"), N.Health, 0);
	TestEqual(TEXT("0x10274e78 m_lifeState := 1"), N.AnimEventLifeStateWord, 1);
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x3f2));
	TestEqual(TEXT("0x10274eb2 m_iHealth := m_iMaxHealth"), N.Health, 100);
	TestEqual(TEXT("0x10274ea8 m_lifeState := 0"), N.AnimEventLifeStateWord, 0);
	N.WriteNpcStateRetail(1);

	// Sound scripts.
	N.EmittedSoundScripts.Reset();
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x3ec, TEXT("NPC.Script")));
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x7da));
	N.Flags &= ~1;
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x7d1));
	N.Flags |= 1;
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x7d2));
	if (TestEqual(TEXT("three sound scripts (0x7d1 off the ground is silent)"), N.EmittedSoundScripts.Num(), 3))
	{
		TestEqual(TEXT("0x10274ec7 0x3ec EmitSound(options)"), N.EmittedSoundScripts[0], FString(TEXT("NPC.Script")));
		TestEqual(TEXT("0x10275198 0x7da swish"), N.EmittedSoundScripts[1], FString(TEXT("AI_BaseNPC.SwishSound")));
		TestEqual(TEXT("0x1027515f 0x7d2 is the HEAVY drop"), N.EmittedSoundScripts[2],
			FString(TEXT("AI_BaseNPC.BodyDrop_Heavy")));
	}

	// 0x7e4: turn bit cleared, EF_NOINTERP set.
	N.BaseScheduleHost.MemoryBits = 0x2000u;
	N.EffectsWord = 0;
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x7e4));
	TestEqual(TEXT("0x102751bf m_afMemory &= ~0x2000"), N.BaseScheduleHost.MemoryBits, 0u);
	TestEqual(TEXT("0x102751e1 m_fEffects |= 0x10"), N.EffectsWord & 0x10u, 0x10u);

	// 0x7f8 with no option and no target: stolen.
	N.SetTarget(FElysiumEntityHandle::Invalid());
	N.Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x7f8));
	TestEqual(TEXT("0x10275384 stolen"), N.LastTaskFailText, FString(TEXT("Weapon stolen by someone else")));

	// 0x3e9 with no cine is claimed and reaches nothing; 0x3eb with no cine, no hint and no
	// interesting place (`0x10274fe0` JZ) is claimed and fires nothing.
	TestTrue(TEXT("0x1027500d no live cine: 0x3e9 is still claimed"),
		N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x3e9)));
	N.BaseScheduleHost.HintNode = INDEX_NONE;
	TestTrue(TEXT("0x10274fe0 no target: 0x3eb is still claimed"),
		N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(0x3eb, TEXT("3"))));
	TestFalse(TEXT("0x102d09b0 refuses a hint index that is no hint"), N.FireHintAnimEvent(INDEX_NONE, 3));

	// An unclaimed id goes on to CBaseCombatCharacter::HandleAnimEvent.
	TestFalse(TEXT("0x102754bb 2070 is default-routed and unclaimed"), N.FElysiumNpcBase::HandleAnimEvent(Misc19Ev(2070)));
	return true;
}

// =================================================================================================
// Species slot 259 bodies
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19DogHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.DogHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19DogHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VDog::HandleAnimEvent` `0x10374280`: 0xbb9 with no active weapon is swallowed.
	FMisc19Fixture F(TEXT("CNPC_VDog"));
	if (TestNotNull(TEXT("dog"), F.Guard) && F.Guard->ActiveWeaponEntity() == nullptr)
	{
		TestTrue(TEXT("0x103742a4 swallowed"), F.Guard->HandleAnimEvent(Misc19Ev(0xbb9)));
		TestEqual(TEXT("0x103742a4 ...with no bite"), F.Guard->DogBiteCalls, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19ManBatHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.ManBatHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19ManBatHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VManBat::HandleAnimEvent` `0x1038e000`.
	FMisc19Fixture F(TEXT("CNPC_VManBat"));
	if (TestNotNull(TEXT("manbat"), F.Guard))
	{
		TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;
		Sounds.Reset();
		TestTrue(TEXT("0x1038e110 event 2 claimed"), F.Guard->HandleAnimEvent(Misc19Ev(2)));
		if (TestEqual(TEXT("one sound"), Sounds.Num(), 1))
		{
			TestEqual(TEXT("0x1038e110 event 2 is the screech"), Sounds[0].Rel,
				FString(TEXT("character/male/sheriff_manbat/screech.wav")));
		}
		Sounds.Reset();
		F.Guard->HandleAnimEvent(Misc19Ev(1));
		TestTrue(TEXT("0x1038e1b0 event 1 draws a wingflap"),
			Sounds.Num() == 1 && Sounds[0].Rel.Contains(TEXT("wingflap_")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19GargoyleHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.GargoyleHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19GargoyleHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VGargoyle::HandleAnimEvent` `0x103786c0`.
	FMisc19Fixture F(TEXT("CNPC_VGargoyle"));
	if (TestNotNull(TEXT("gargoyle"), F.Guard))
	{
		TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;
		Sounds.Reset();
		TestTrue(TEXT("0x103786d8 event 1 is swallowed"), F.Guard->HandleAnimEvent(Misc19Ev(1)));
		TestEqual(TEXT("0x103786d8 ...silently"), Sounds.Num(), 0);
		F.Guard->HandleAnimEvent(Misc19Ev(0x802));
		TestEqual(TEXT("0x1037881b the stomp shakes"), F.Guard->AnimEventShakeCalls.Num(), 1);
		TestTrue(TEXT("0x10378848 slot 617 stomp"), Sounds.Num() == 1 && Sounds[0].Rel.Contains(TEXT("gargoyle/stomp_")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19TzimisceHeadClawHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.TzimisceHeadClawHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19TzimisceHeadClawHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VTzimisceHeadClaw::HandleAnimEvent` `0x103c1540`: slot 619 argument 1 on 0x802.
	FMisc19Fixture F(TEXT("CNPC_VTzimisceHeadClaw"));
	if (TestNotNull(TEXT("head claw"), F.Guard))
	{
		TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;
		Sounds.Reset();
		TestTrue(TEXT("0x103c159a 0x802 claimed"), F.Guard->HandleAnimEvent(Misc19Ev(0x802)));
		if (TestEqual(TEXT("0x103c15cb one sound, no breath"), Sounds.Num(), 1))
		{
			TestTrue(TEXT("0x103c15c9 argument 1 draws Foot_Step3/4"),
				Sounds[0].Rel.Contains(TEXT("Foot_Step3")) || Sounds[0].Rel.Contains(TEXT("Foot_Step4")));
		}
		if (TestEqual(TEXT("one shake"), F.Guard->AnimEventShakeCalls.Num(), 1))
		{
			TestEqual(TEXT("0x103c15a0 amplitude 1.3"), F.Guard->AnimEventShakeCalls[0].Amplitude, 1.3f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19WerewolfHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.WerewolfHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19WerewolfHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VWerewolf::HandleAnimEvent` `0x103d88e0`.
	FMisc19Fixture F(TEXT("CNPC_VWerewolf"));
	if (TestNotNull(TEXT("werewolf"), F.Guard))
	{
		TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;
		Sounds.Reset();
		TestTrue(TEXT("0x103d8c10 a footfall is claimed"), F.Guard->HandleAnimEvent(Misc19Ev(0x802)));
		TestEqual(TEXT("werewolf_footstep_sounds ships 0: silent"), Sounds.Num(), 0);
		F.Guard->HandleAnimEvent(Misc19Ev(0x834));
		if (TestEqual(TEXT("0x103d8a1d the slam shakes"), F.Guard->AnimEventShakeCalls.Num(), 1))
		{
			TestTrue(TEXT("0x103d8a1d ...as an air shake"), F.Guard->AnimEventShakeCalls[0].bAirShake);
		}
		F.Guard->HandleAnimEvent(Misc19Ev(0x835));
		TestEqual(TEXT("0x103d8df0 the activity voice"), F.Guard->WerewolfActivityVoiceCalls, 1);
		TestTrue(TEXT("0x836..0x83d swallowed"), F.Guard->HandleAnimEvent(Misc19Ev(0x838)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19SabbatLeaderHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.SabbatLeaderHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19SabbatLeaderHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VSabbatLeader::HandleAnimEvent` `0x103a7000`.
	FMisc19Fixture F(TEXT("CNPC_VSabbatLeader"));
	if (TestNotNull(TEXT("sabbat leader"), F.Guard))
	{
		TestTrue(TEXT("0x103a7066 0x802 is the footstep slot"), F.Guard->HandleAnimEvent(Misc19Ev(0x802)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19MingXiaoHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.MingXiaoHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19MingXiaoHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VMingXiao::HandleAnimEvent` `0x10392a70`.
	FMisc19Fixture F(TEXT("CNPC_VMingXiao"));
	if (TestNotNull(TEXT("ming xiao"), F.Guard))
	{
		TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;
		Sounds.Reset();
		TestTrue(TEXT("0x10392ba0 0x802 swallowed"), F.Guard->HandleAnimEvent(Misc19Ev(0x802)));
		TestEqual(TEXT("0x10392ba0 ...silently"), Sounds.Num(), 0);
		F.Guard->HandleAnimEvent(Misc19Ev(0x835));
		if (TestEqual(TEXT("0x10392afe the slam shake"), F.Guard->AnimEventShakeCalls.Num(), 1))
		{
			TestEqual(TEXT("0x10392afe amplitude 15"), F.Guard->AnimEventShakeCalls[0].Amplitude, 15.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19HengeyokaiHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.HengeyokaiHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19HengeyokaiHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VHengeyokai::HandleAnimEvent` `0x1037fb60`.
	FMisc19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* const H = static_cast<FElysiumNpcHengeyokai*>(F.Guard);
	if (!TestNotNull(TEXT("hengeyokai"), F.Guard))
	{
		return false;
	}
	TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;
	H->bHengeyokaiInSharkForm = false;
	Sounds.Reset();
	TestTrue(TEXT("0x1037fbe4 0x802 out of shark form is swallowed"), H->HandleAnimEvent(Misc19Ev(0x802)));
	TestEqual(TEXT("0x1037fbe4 ...silently"), Sounds.Num(), 0);
	TestEqual(TEXT("0x1037fbe4 ...with no shake"), H->AnimEventShakeCalls.Num(), 0);

	H->bHengeyokaiInSharkForm = true;
	TestTrue(TEXT("0x1037fbe4 0x803 in shark form is claimed"), H->HandleAnimEvent(Misc19Ev(0x803)));
	if (TestEqual(TEXT("0x1037fc09 one shake"), H->AnimEventShakeCalls.Num(), 1))
	{
		TestEqual(TEXT("0x1037fbfb amplitude 2.0"), H->AnimEventShakeCalls[0].Amplitude, 2.0f);
		TestEqual(TEXT("0x1037fbec radius 1024"), H->AnimEventShakeCalls[0].Radius, 1024.0f);
	}
	TestTrue(TEXT("0x1037fc15 slot 617 draws a stomp"),
		Sounds.Num() == 1 && Sounds[0].Rel.Contains(TEXT("hengeyokai/stomp_")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19TzimisceHandleAnimEventTest,
	"Elysium.Substrate.NpcKernelMisc19.TzimisceHandleAnimEvent", GMisc19Flags)
bool FElysiumNpcKernelMisc19TzimisceHandleAnimEventTest::RunTest(const FString&)
{
	// `CNPC_VTzimisce::HandleAnimEvent` `0x103ba410`.
	FMisc19Fixture F(TEXT("CNPC_VTzimisce"));
	if (!TestNotNull(TEXT("tzimisce"), F.Guard))
	{
		return false;
	}
	TArray<FElysiumBodySound>& Sounds = F.World.Services.BodySounds;
	Sounds.Reset();
	TestTrue(TEXT("0x103ba532 0x802 claimed"), F.Guard->HandleAnimEvent(Misc19Ev(0x802)));
	if (TestEqual(TEXT("0x103ba557 one shake"), F.Guard->AnimEventShakeCalls.Num(), 1))
	{
		TestEqual(TEXT("0x103ba549 amplitude 2.0"), F.Guard->AnimEventShakeCalls[0].Amplitude, 2.0f);
	}
	TestTrue(TEXT("0x103ba563 slot 619 draws a spider footstep"),
		Sounds.Num() == 1 && Sounds[0].Rel.Contains(TEXT("spiderchick/spi_footstep_indiv_")));

	Sounds.Reset();
	TestTrue(TEXT("0x103ba524 0xbbb claimed"), F.Guard->HandleAnimEvent(Misc19Ev(0xbbb)));
	TestTrue(TEXT("0x103ba571 slot 620 draws a swish"),
		Sounds.Num() == 1 && Sounds[0].Rel.Contains(TEXT("spiderchick/spi_attack_swish_")));

	const int32 ExpressionsBefore = F.Guard->SetExpressionMisc19Calls;
	TestTrue(TEXT("0x103ba436 event 6 claimed"), F.Guard->HandleAnimEvent(Misc19Ev(6)));
	TestEqual(TEXT("event 6 sets the 'scream' expression (0x103b9f90(2, 0.5) -> 0x10106580)"),
		F.Guard->SetExpressionMisc19Calls, ExpressionsBefore + 1);
	return true;
}

// =================================================================================================
// 0x10365a90 Bach camper pass, 0x103cfc50 CNPC_VWerewolf::CheckAllMoveHints
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19BachCamperTest,
	"Elysium.Substrate.NpcKernelMisc19.BachGatherCamperConditions", GMisc19Flags)
bool FElysiumNpcKernelMisc19BachCamperTest::RunTest(const FString&)
{
	// `0x10365a90`.
	FMisc19Fixture F(TEXT("CNPC_VBach"));
	FElysiumNpcBach* Bach = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcBach>() : nullptr;
	if (!TestNotNull(TEXT("bach"), Bach) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	ElysiumNpcEnemy::SetEnemy(*Bach, FElysiumEntityHandle::Invalid());
	const bool bVisible = Bach->FVisible(F.Player, 0x2804091, Bach, 0);
	Bach->BachWasOccluded = 0;
	Bach->bBachCamperFlag = false;
	Bach->BachGrenadeActive = 10;
	Bach->BachGatherCamperConditions();
	if (bVisible)
	{
		TestEqual(TEXT("0x10365f72 visible with nothing occluded returns"), Bach->BachWasOccluded, 0);
		TestFalse(TEXT("0x10365f72 ...before the grenade zones"), Bach->bBachCamperFlag);
	}
	else
	{
		TestEqual(TEXT("0x10365afb the first occluded pass marks m_iWasOccluded"), Bach->BachWasOccluded, 1);
		TestTrue(TEXT("0x10365c74 zone 10 forces the camper flag"), Bach->bBachCamperFlag);
		TestTrue(TEXT("0x10365c84 the occlusion origin is the target origin"),
			Bach->BachLastOccludeOriginUnits.Equals(F.Player->Origin / ElysiumMove::U, 0.01));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMisc19CheckAllMoveHintsTest,
	"Elysium.Substrate.NpcKernelMisc19.CheckAllMoveHints", GMisc19Flags)
bool FElysiumNpcKernelMisc19CheckAllMoveHintsTest::RunTest(const FString&)
{
	// `CNPC_VWerewolf::CheckAllMoveHints` `0x103cfc50`: with no program running and no hint in the
	// world, either the pursuit test refuses (FALSE) or the walk finds nothing (`0x103d0073`
	// DevWarning, FALSE); the held hint is untouched either way.
	FMisc19Fixture F(TEXT("CNPC_VWerewolf"));
	FElysiumNpcWerewolf* Wolf = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcWerewolf>() : nullptr;
	if (!TestNotNull(TEXT("werewolf"), Wolf))
	{
		return false;
	}
	Wolf->Schedule.Clear();
	Wolf->MoveHintNode = INDEX_NONE;
	TestFalse(TEXT("0x103d008a an empty walk reports FALSE"), Wolf->CheckAllMoveHints());
	TestEqual(TEXT("0x103d00c8 no hint was set"), Wolf->MoveHintNode, static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("0x103d235b IsImperativeMoveHint: no +0x66e8 gate bit, not imperative"),
		Wolf->IsImperativeMoveHint(FElysiumNpcBase::FHintWords()));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
