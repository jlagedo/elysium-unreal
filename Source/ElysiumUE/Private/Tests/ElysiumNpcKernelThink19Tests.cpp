// Story 0019/8 (29e under the strict verdict), family **Think19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelThink19.` and the retail address. Lane L13b wrote
// them unrun (the pass-I porter builds nothing); every assertion cites the listing line it reads.
// The Troika cases stand a bare `CAI_BaseNPCTroika` whose slot 432 is a counting probe, so the AI
// block's dispatch is observable without running `RunAI` (lane L13a's body).
//
// Owns (Think19's `rule` rows): 0x10298070 CAI_BaseNPCTroika::UpdateCharacter, 0x1026ca80
// CAI_BaseNPC::NPCThink, 0x10292de0 CAI_BaseNPCTroika::NPCThink, 0x10369120 CNPC_VCamera::NPCThink,
// 0x1037b3f0 CNPC_VGhoulCroucher::NPCThink, 0x10394990 CNPC_VMingXiao::NPCThink, 0x103a05b0
// CNPC_VNewscaster::NPCThink, 0x103b9040 CNPC_VTzimisce::NPCThink, 0x103c6000
// CNPC_VVampireBoss::NPCThink, 0x103dfa20 CNPC_VZombie::NPCThink, 0x1035db20
// CNPC_VAndreiBlood::NPCThink, 0x10361490 CNPC_VAsianVampire::NPCThink, 0x1036c6c0
// CNPC_VChangBros::NPCThink, 0x10375e50 CNPC_VFrenzyShadow::NPCThink, 0x103af830
// CNPC_VSheriffMan::NPCThink; Damaged19's 0x103cb590 CNPC_VWerewolf::NPCThink.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GThink19TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A bare Troika body whose slot 432 counts: `NPCThink` dispatches it virtually (`0x1029357c`,
	// `0x1026cc2a`), so the probe sees every call and runs no `RunAI` body.
	class FThink19RunAiProbe final : public FElysiumNpc
	{
	public:
		virtual void RunAI(bool bReduced) override
		{
			++RunAiCalls;
			bLastReduced = bReduced;
		}
		int32 RunAiCalls = 0;
		bool bLastReduced = false;
	};

	struct FThink19Fixture
	{
		FElysiumNpcWorldFixture W;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;
		int32 HintIndex = INDEX_NONE;

		// `GuardClass == nullptr` stands the counting probe as a bare `CAI_BaseNPCTroika`. `bHint`
		// stands an `info_node_hint` of type 0x2774 (slot 566's `MOV AL,1` arm), all groups ("hint").
		explicit FThink19Fixture(const TCHAR* GuardClass = nullptr, bool bHint = false)
			: W([GuardClass, bHint]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("think19_kernel"), 1931);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					if (GuardClass == nullptr)
					{
						FElysiumEntityDef& Def = Builder.AddTroikaNpc(TEXT("guard"), FVector::ZeroVector);
						Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity>
						{
							return MakeUnique<FThink19RunAiProbe>();
						};
					}
					else
					{
						Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, GuardClass);
					}
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					if (bHint)
					{
						FElysiumEntityDef& Hint = Builder.AddEntity(TEXT("info_node_hint"), TEXT("hint"),
							FVector(128.f, 0.f, 0.f));
						Hint.Keys.Add(TEXT("hinttype"), TEXT("10100"));
					}
					return Builder;
				}())
		{
			Guard = W.Npc(TEXT("guard"));
			if (Guard == nullptr && GuardClass != nullptr)
			{
				Guard = W.NpcOfClass(GuardClass);   // a `Spawn` that renames the body
			}
			Other = W.Npc(TEXT("other"));
			Player = W.Player();
			if (FElysiumEntity* Hint = W.World.FindByName(TEXT("hint")))
			{
				HintIndex = Hint->Handle.Index;
				// Pin the two words slot 566 reads, whatever the key parse and `CAI_Hint::Spawn`'s
				// group fold made of them: type 0x2774 (the `MOV AL,1` arm) and the all-groups mask.
				if (FElysiumHint* AsHint = FElysiumHint::Cast(Hint))
				{
					AsHint->HintType = 10100;
					AsHint->GroupId = -1;
				}
			}
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
			FElysiumNpc::Think19ResetBossRegistry();
		}

		~FThink19Fixture()
		{
			FElysiumNpc::Think19ResetBossRegistry();
			ElysiumNpcTunables::ResetConVars();
		}

		double Now() const { return W.World.NowSeconds(); }
		FThink19RunAiProbe* Probe() const { return static_cast<FThink19RunAiProbe*>(Guard); }
	};
}

// --- 0x10292de0 CAI_BaseNPCTroika::NPCThink ------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19TroikaDisableTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.FlagsThenDisable", GThink19TestFlags)
bool FElysiumNpcKernelThink19TroikaDisableTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19RunAiProbe& N = *F.Probe();
	N.NpcFlags.Set(EElysiumNpcFlag2::SCHEDULE_CHANGED);
	N.NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
	N.NextThink = 123.f;
	N.NPCThink();   // quiet: `m_bDisableAI` is set
	TestFalse(TEXT("0x10292e5e clears SCHEDULE_CHANGED before the disable test"),
		N.NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED));
	TestFalse(TEXT("0x10292e5e AND 0x7ffffffb clears bit 31 too"),
		N.NpcFlags.HasRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31));
	TestEqual(TEXT("0x10292e6c m_bDisableAI returns with m_flNextThink untouched"), N.NextThink, 123.f);
	TestEqual(TEXT("and no slot 432"), N.RunAiCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19TroikaFullPassTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.FullPass", GThink19TestFlags)
bool FElysiumNpcKernelThink19TroikaFullPassTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19RunAiProbe& N = *F.Probe();
	N.SetDisableAi(false);   // slot 614: all four clocks due now
	const double Now = F.Now();
	N.ScheduleHost.EnemyDistUnits = 1.f;
	const int32 MovesBefore = N.MotorSeams.PerformMovement;
	const int32 PostRunsBefore = N.MotorSeams.PostRunWeaponUpdates;
	const int32 OverlaysBefore = N.Think19DebugDistanceOverlayCalls;
	const int32 UpdatesBefore = N.Think19CombatUpdateCharacterCalls;
	N.NPCThink();
	TestEqual(TEXT("0x10292e97 normal due: Set1 wrote the no-enemy triple (0x1029305e)"),
		N.ScheduleHost.EnemyDistUnits, 20000.f);
	TestEqual(TEXT("0x1029357c slot 432 ran once"), N.RunAiCalls, 1);
	TestFalse(TEXT("0x10293568 AI clock due: RunAI(false)"), N.bLastReduced);
	TestEqual(TEXT("0x10293584 PostRun"), N.MotorSeams.PostRunWeaponUpdates, PostRunsBefore + 1);
	TestEqual(TEXT("0x1029359e PerformMovement"), N.MotorSeams.PerformMovement, MovesBefore + 1);
	TestTrue(TEXT("0x10293632 CalcNextMoveThink moved the move clock"), N.ScheduleHost.NextMove > Now);
	TestEqual(TEXT("0x1029365b slot 312 on the due update clock"),
		N.Think19CombatUpdateCharacterCalls, UpdatesBefore + 1);
	TestEqual(TEXT("0x102936c0 m_flNextThink = min(update, normal)"), N.NextThink,
		static_cast<float>(FMath::Min(N.ScheduleHost.NextUpdate, N.ScheduleHost.NextNormal)));
	TestEqual(TEXT("0x102936e0 the debug overlay closes the tail"),
		N.Think19DebugDistanceOverlayCalls, OverlaysBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19TroikaNotDueTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.NormalNotDue", GThink19TestFlags)
bool FElysiumNpcKernelThink19TroikaNotDueTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19RunAiProbe& N = *F.Probe();
	N.SetDisableAi(false);
	const double Now = F.Now();
	N.ScheduleHost.NextNormal = Now + 5.0;
	N.ScheduleHost.NextUpdate = Now + 4.0;
	N.ScheduleHost.EnemyDistUnits = 1.f;
	const int32 UpdatesBefore = N.Think19CombatUpdateCharacterCalls;
	N.NPCThink();
	TestEqual(TEXT("0x10292e97 not due: Set1 skipped"), N.ScheduleHost.EnemyDistUnits, 1.f);
	TestEqual(TEXT("0x102932aa not due: no AI block"), N.RunAiCalls, 0);
	TestEqual(TEXT("0x10293650 update not due: no slot 312"), N.Think19CombatUpdateCharacterCalls,
		UpdatesBefore);
	TestEqual(TEXT("0x102936aa the earlier (update) stamp wins"), N.NextThink,
		static_cast<float>(Now + 4.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19TroikaGateRefusedTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.GateRefused", GThink19TestFlags)
bool FElysiumNpcKernelThink19TroikaGateRefusedTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19RunAiProbe& N = *F.Probe();
	N.SetDisableAi(false);
	F.W.World.SetAiEnabled(false);
	N.NextThink = 77.f;
	const int32 MovesBefore = N.MotorSeams.PerformMovement;
	const int32 OverlaysBefore = N.Think19DebugDistanceOverlayCalls;
	N.NPCThink();
	TestEqual(TEXT("Set1 still ran (0x1029305e)"), N.ScheduleHost.EnemyDistUnits, 20000.f);
	TestEqual(TEXT("0x10293402 refused: no slot 432"), N.RunAiCalls, 0);
	TestEqual(TEXT("no PerformMovement"), N.MotorSeams.PerformMovement, MovesBefore);
	TestEqual(TEXT("0x1029340f graph built: m_flNextThink not re-armed"), N.NextThink, 77.f);
	TestEqual(TEXT("0x10293447 returns WITHOUT the tail"), N.Think19DebugDistanceOverlayCalls,
		OverlaysBefore);
	F.W.World.SetAiEnabled(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19EnemyTripleTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.EnemyTriple", GThink19TestFlags)
bool FElysiumNpcKernelThink19EnemyTripleTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.Think19EnemyTriple();
	TestEqual(TEXT("0x1029305e no enemy: dist 20000"), N.ScheduleHost.EnemyDistUnits, 20000.f);
	TestEqual(TEXT("0x10293064 height 20000"), N.ScheduleHost.EnemyHeightDiffUnits, 20000.f);
	TestEqual(TEXT("0x1029306a last-known 20000"), N.ScheduleHost.EnemyLastKnownDistUnits, 20000.f);

	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	const int32 FacingBefore = N.FacingTargetRequests.Num();
	N.Think19EnemyTriple();
	TestEqual(TEXT("0x10292fb0 dist to the enemy"), N.ScheduleHost.EnemyDistUnits,
		static_cast<float>(FVector::Dist(N.Origin, F.Other->Origin)) / ElysiumMove::U, 0.01f);
	TestEqual(TEXT("0x10292fc1 |dz|"), N.ScheduleHost.EnemyHeightDiffUnits,
		static_cast<float>(FMath::Abs(N.Origin.Z - F.Other->Origin.Z)) / ElysiumMove::U, 0.01f);
	const FVector LastKnown = N.Conditions19LastKnownPosition(F.Other);
	TestEqual(TEXT("0x10293003 dist to 0x102dfed0's last-known position"),
		N.ScheduleHost.EnemyLastKnownDistUnits,
		static_cast<float>(FVector::Dist(N.Origin, LastKnown)) / ElysiumMove::U, 0.01f);
	TestEqual(TEXT("0x10293039 no MOVE_FACE_ENEMY: no slot 517"), N.FacingTargetRequests.Num(),
		FacingBefore);

	N.NpcFlags.Set(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	N.Think19EnemyTriple();
	if (TestEqual(TEXT("0x10293051 slot 517 under the cvar and the flag"), N.FacingTargetRequests.Num(),
		FacingBefore + 1))
	{
		const FElysiumNpcBase::FFacingTargetRequest& Request = N.FacingTargetRequests.Last();
		TestEqual(TEXT("1.0 (0x10293048)"), Request.Duration, 1.0f);
		TestEqual(TEXT("0.8 (0x1029303f)"), Request.Ramp, 0.8f, 0.0001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19HintUpkeepTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.HintUpkeep", GThink19TestFlags)
bool FElysiumNpcKernelThink19HintUpkeepTest::RunTest(const FString&)
{
	FThink19Fixture F(nullptr, true);
	if (!TestNotNull(TEXT("the probe stands"), F.Guard) || !TestTrue(TEXT("the hint stands"),
		F.HintIndex != INDEX_NONE))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.OccludedDelayNormal = 3.f;
	N.OccludedDelayCover = 7.f;

	N.BaseScheduleHost.HintNode = INDEX_NONE;
	N.Think19HintUpkeep();
	TestEqual(TEXT("0x10293171 no hint: the normal delay"), N.OccludedDelay, 3.f);

	// Not a hint at all: slot 566 refuses -> the clear arm.
	N.Cognition.Conditions.Reset();
	N.BaseScheduleHost.HintNode = F.Other->Handle.Index;
	N.Think19HintUpkeep();
	TestEqual(TEXT("0x102930d1 a hint: the cover delay"), N.OccludedDelay, 7.f);
	TestTrue(TEXT("0x10293164 an invalid hint sets condition 0x29"),
		N.Cognition.Conditions.HasOrdinal(0x29));

	// A valid hint, entrenched: kept.
	N.Cognition.Conditions.Reset();
	N.BaseScheduleHost.HintNode = F.HintIndex;
	N.bStayEntrenched = true;
	N.Cognition.Conditions.SetOrdinal(0x48);
	N.Think19HintUpkeep();
	TestFalse(TEXT("0x102930ed entrenched keeps the hint"), N.Cognition.Conditions.HasOrdinal(0x29));

	// Un-entrenched, no cover object and no enemy: the two compare EQUAL (retail defect kept), and
	// condition 0x48 clears the hint.
	N.bStayEntrenched = false;
	N.BaseScheduleHost.HintNode = F.HintIndex;
	N.Think19HintUpkeep();
	TestTrue(TEXT("0x10293147 null cover == null enemy with 0x48 -> 0x29"),
		N.Cognition.Conditions.HasOrdinal(0x29));

	// The same with neither 0x2e nor 0x48: kept.
	N.Cognition.Conditions.Reset();
	N.BaseScheduleHost.HintNode = F.HintIndex;
	N.Think19HintUpkeep();
	TestFalse(TEXT("0x10293147 no 0x2e/0x48 keeps the hint"), N.Cognition.Conditions.HasOrdinal(0x29));

	// Un-entrenched, cover object is not my enemy: kept even with 0x2e.
	N.ScheduleHost.HintCoverObject = F.Other->Handle;
	N.Cognition.Conditions.SetOrdinal(0x2e);
	N.Think19HintUpkeep();
	TestFalse(TEXT("0x1029312d cover != enemy keeps the hint"), N.Cognition.Conditions.HasOrdinal(0x29));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19ShootAtHintTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.ShootAtHint", GThink19TestFlags)
bool FElysiumNpcKernelThink19ShootAtHintTest::RunTest(const FString&)
{
	FThink19Fixture F(nullptr, true);
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.ShootTargetOverride = F.Other->Handle;
	N.ScheduleHost.ShootAtHintNode = 0;
	N.Think19ShootAtHint();
	TestTrue(TEXT("0x1029317f no shoot-at hint: override untouched"), N.ShootTargetOverride == F.Other->Handle);

	N.ScheduleHost.ShootAtHintNode = F.Other->Handle.Index;   // not a hint: slot 566 refuses
	N.Think19ShootAtHint();
	TestFalse(TEXT("0x102931b5 refused: override -1"), N.ShootTargetOverride.IsSet());
	TestEqual(TEXT("0x102931bf refused: m_pShootAtHint = NULL"), N.ScheduleHost.ShootAtHintNode, 0);

	if (F.HintIndex != INDEX_NONE)
	{
		N.ScheduleHost.ShootAtHintNode = F.HintIndex;
		N.Think19ShootAtHint();
		TestEqual(TEXT("0x102931a1 valid: the hint's own handle"), N.ShootTargetOverride.Index, F.HintIndex);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19ScreamDeathTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.ScreamDeath", GThink19TestFlags)
bool FElysiumNpcKernelThink19ScreamDeathTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	auto Screams = [&N]()
	{
		int32 Count = 0;
		for (const FElysiumNpc::FVSoundSpeak& Call : N.VSoundSpeakCalls)
		{
			Count += (Call.Concept != nullptr && FCString::Strcmp(Call.Concept, TEXT("Scream_Death")) == 0) ? 1 : 0;
		}
		return Count;
	};
	N.SetFrenziedWord(0);
	for (int32 Roll = 0; Roll < 300; ++Roll)
	{
		N.Think19ScreamDeathRoll();
	}
	TestEqual(TEXT("0x102931db no frenzied 0x8000: no roll"), Screams(), 0);

	N.SetFrenziedWord(0x8000);
	for (int32 Roll = 0; Roll < 2000; ++Roll)
	{
		N.Think19ScreamDeathRoll();
	}
	const int32 Count = Screams();
	TestTrue(TEXT("0x102931f3 RandomInt(0,99) < 1 plays sometimes"), Count > 0);
	TestTrue(TEXT("and not every think"), Count < 500);
	if (Count > 0)
	{
		const FElysiumNpc::FVSoundSpeak& Call = N.VSoundSpeakCalls.Last();
		TestEqual(TEXT("0x10293263 channel 2"), Call.Channel, 2);
		TestEqual(TEXT("0x1029325e volume 1.0"), Call.Volume, 1.0f);
		TestEqual(TEXT("0x10293259 1.25"), Call.Attenuation, 1.25f);
		TestEqual(TEXT("0x1029324b the concept list is empty: -1 cached"), Call.ConceptId, int32(INDEX_NONE));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19Set2Test,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.Set2AndAiBlock", GThink19TestFlags)
bool FElysiumNpcKernelThink19Set2Test::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19RunAiProbe& N = *F.Probe();
	const double Now = F.Now();
	const int32 TrackBefore = N.Think19TrackUnderGroundCalls;
	N.ScheduleHost.NextAI = Now + 30.0;   // the AI clock declines
	TestTrue(TEXT("0x10293402 accepted"), N.Think19NormalSet2(Now, 0.1f));
	TestEqual(TEXT("0x1029357c slot 432"), N.RunAiCalls, 1);
	TestTrue(TEXT("0x10293568 AI clock not due: RunAI(true)"), N.bLastReduced);
	TestEqual(TEXT("0x10293333 debug_track_under_ground \"0\": no ground check"),
		N.Think19TrackUnderGroundCalls, TrackBefore);
	TestFalse(TEXT("0x1029335b no DISAPPEAR: alive"), N.IsDead());

	F.W.World.SetAiEnabled(false);
	TestFalse(TEXT("0x10293402 refused answers false"), N.Think19NormalSet2(Now, 0.1f));
	TestEqual(TEXT("and runs no slot 432"), N.RunAiCalls, 1);
	F.W.World.SetAiEnabled(true);

	// DISAPPEAR with no closest player: the PVS test answers false -> UTIL_Remove, and the think goes on.
	N.Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	N.NpcFlags.Set(EElysiumNpcFlag2::DISAPPEAR);
	TestTrue(TEXT("the pass continues past UTIL_Remove"), N.Think19NormalSet2(Now, 0.1f));
	TestTrue(TEXT("0x102933f1 UTIL_Remove(this)"), N.IsDead());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19TailTest,
	"Elysium.Substrate.NpcKernelThink19.TroikaNPCThink.Tail", GThink19TestFlags)
bool FElysiumNpcKernelThink19TailTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.Now();
	N.ScheduleHost.NextUpdate = Now + 3.0;
	N.ScheduleHost.NextNormal = Now + 2.0;
	N.Think19Tail(Now, false, 0.f);
	TestEqual(TEXT("0x102936ac normal earlier: m_flNextThink = normal"), N.NextThink,
		static_cast<float>(Now + 2.0));

	N.bJumping = true;
	N.Think19Tail(Now, false, 0.f);
	TestEqual(TEXT("0x102936d8 m_bJumping: curtime + 0.01"), N.NextThink,
		static_cast<float>(Now) + 0.01f);
	N.bJumping = false;

	N.bIsTalking = true;
	N.TalkingUntil = -1.0;
	const int32 UpdatesBefore = N.Think19CombatUpdateCharacterCalls;
	N.Think19Tail(Now, true, 0.25f);
	TestEqual(TEXT("0x1029365b slot 312 with the update interval"), N.Think19CombatUpdateCharacterCalls,
		UpdatesBefore + 1);
	TestEqual(TEXT("the interval passed through"), N.Think19LastUpdateCharacterInterval, 0.25f);
	TestFalse(TEXT("0x10293678 FinishTalking once 0x102c0aa0 says done"), N.bIsTalking);
	return true;
}

// --- 0x10298070 CAI_BaseNPCTroika::UpdateCharacter ---------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19UpdateCharacterTest,
	"Elysium.Substrate.NpcKernelThink19.UpdateCharacter.BossRegistry", GThink19TestFlags)
bool FElysiumNpcKernelThink19UpdateCharacterTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard) || !TestNotNull(TEXT("other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const int32 TailBefore = N.Think19CombatUpdateCharacterCalls;
	N.UpdateCharacterRetail(0.5f);
	TestEqual(TEXT("0x1029829a the base tail on every path"), N.Think19CombatUpdateCharacterCalls, TailBefore + 1);
	TestEqual(TEXT("0x102980e6 not a boss: no registry"), int32(FElysiumNpc::Think19BossRegistryCount), 0);

	N.bIsBossMonster = true;
	N.SetState(1);
	N.UpdateCharacterRetail(0.5f);
	TestFalse(TEXT("0x10298109 state != COMBAT: not registered"), N.bInBossRegistry);

	N.SetState(2);
	N.UpdateCharacterRetail(0.5f);
	TestTrue(TEXT("0x1029812d registered"), N.bInBossRegistry);
	TestEqual(TEXT("0x10298115 count 1"), int32(FElysiumNpc::Think19BossRegistryCount), 1);
	TestTrue(TEXT("0x1029812b slot 0 is my handle"), FElysiumNpc::Think19BossRegistry[0] == N.Handle);

	N.UpdateCharacterRetail(0.5f);
	TestEqual(TEXT("0x102980f0 already registered: no second entry"),
		int32(FElysiumNpc::Think19BossRegistryCount), 1);

	// No longer a boss: the rebuild keeps the other boss and drops me.
	FElysiumNpc::Think19BossRegistry[1] = FElysiumNpc::Think19BossRegistry[0];
	FElysiumNpc::Think19BossRegistry[0] = F.Other->Handle;
	FElysiumNpc::Think19BossRegistryCount = 2;
	N.bIsBossMonster = false;
	N.UpdateCharacterRetail(0.5f);
	TestEqual(TEXT("0x10298234 one survivor"), int32(FElysiumNpc::Think19BossRegistryCount), 1);
	TestTrue(TEXT("0x1029824d the survivor copied back"), FElysiumNpc::Think19BossRegistry[0] == F.Other->Handle);
	TestFalse(TEXT("slot 1 past the survivor count (retail over-read; guarded)"),
		FElysiumNpc::Think19BossRegistry[1].IsSet());
	TestFalse(TEXT("0x10298260 the latch cleared"), N.bInBossRegistry);

	// Full table: a new boss cannot register.
	FElysiumNpc::Think19BossRegistryCount = 2;
	N.bIsBossMonster = true;
	N.UpdateCharacterRetail(0.5f);
	TestFalse(TEXT("0x102980f9 count >= 2: not registered"), N.bInBossRegistry);

	FElysiumNpc::Think19ResetBossRegistry();
	TestEqual(TEXT("0x1023bc20 world activate resets the count"), int32(FElysiumNpc::Think19BossRegistryCount), 0);
	return true;
}

// --- 0x1026ca80 CAI_BaseNPC::NPCThink (and the gate 0x1026c3d0) ----------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19BaseThinkTest,
	"Elysium.Substrate.NpcKernelThink19.BaseNPCThink", GThink19TestFlags)
bool FElysiumNpcKernelThink19BaseThinkTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19RunAiProbe& N = *F.Probe();
	const double Now = F.Now();
	const int32 CacheBefore = N.Think19BaseCacheInterruptCalls;
	const int32 PostBefore = N.MotorSeams.PostRunWeaponUpdates;
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026cb0a CacheInterruptConditions"), N.Think19BaseCacheInterruptCalls, CacheBefore + 1);
	TestEqual(TEXT("0x1026cb1d m_flNextThink = curtime + 0.1 up front"), N.NextThink,
		static_cast<float>(Now + 0.1));
	TestEqual(TEXT("0x1026cc2a RunAI(false)"), N.RunAiCalls, 1);
	TestFalse(TEXT("with false"), N.bLastReduced);
	TestEqual(TEXT("0x1026cc32 PostRun"), N.MotorSeams.PostRunWeaponUpdates, PostBefore + 1);

	// A grapple victim with a live partner: slot 432 skipped, PostRun still runs.
	N.Grapple.Partner = F.Other->Handle;
	N.Grapple.Role = EElysiumGrappleRole::Victim;
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026cc23 role 1 with a partner skips slot 432"), N.RunAiCalls, 1);
	TestEqual(TEXT("0x1026cc32 PostRun regardless"), N.MotorSeams.PostRunWeaponUpdates, PostBefore + 2);
	N.Grapple.Role = EElysiumGrappleRole::Attacker;
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026cc23 role 0: slot 432 runs"), N.RunAiCalls, 2);
	N.Grapple.Role = EElysiumGrappleRole::None;
	N.Grapple.Partner = FElysiumEntityHandle::Invalid();

	F.W.World.SetAiEnabled(false);
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026cb92 gate refused: no slot 432"), N.RunAiCalls, 2);
	TestEqual(TEXT("and no PostRun"), N.MotorSeams.PostRunWeaponUpdates, PostBefore + 3);
	TestEqual(TEXT("but the stamp was already written"), N.NextThink, static_cast<float>(Now + 0.1));
	F.W.World.SetAiEnabled(true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19AiGateTest,
	"Elysium.Substrate.NpcKernelThink19.AiConsoleGate", GThink19TestFlags)
bool FElysiumNpcKernelThink19AiGateTest::RunTest(const FString&)
{
	FThink19Fixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	TestTrue(TEXT("0x1026c429 AI on, graph built, no step: true"), N.Think19AiConsoleGate());
	F.W.World.SetAiEnabled(false);
	TestFalse(TEXT("0x1026c4e8 g_AIDisabled bit 0: false after SetActivity(1)"), N.Think19AiConsoleGate());
	F.W.World.SetAiEnabled(true);

	F.W.World.SetAiStepMode(true);
	N.MaintainDebugTaskIndex = -5;
	N.SequencePlaybackRate = 0.f;
	TestTrue(TEXT("ai_step, index below DAT_105c9798: true"), N.Think19AiConsoleGate());
	TestEqual(TEXT("and m_flPlaybackRate = 1.0"), N.SequencePlaybackRate, 1.f);
	N.MaintainDebugTaskIndex = 0;
	TestFalse(TEXT("ai_step, index >= -1: false"), N.Think19AiConsoleGate());
	TestEqual(TEXT("no active navigator goal: m_flPlaybackRate = 0"), N.SequencePlaybackRate, 0.f);
	F.W.World.SetAiStepMode(false);
	return true;
}

// --- The species bodies ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19BossCadenceTest,
	"Elysium.Substrate.NpcKernelThink19.Species.BossCadence", GThink19TestFlags)
bool FElysiumNpcKernelThink19BossCadenceTest::RunTest(const FString&)
{
	// `0x103c6000` VampireBoss / SabbatLeader, `0x1035db20` AndreiBlood (the boss body alone),
	// `0x10361490` AsianVampire, `0x1036c6c0` ChangBros, `0x103af830` SheriffMan: after the base pass,
	// `m_flNextThink = curtime + 0.1f`.
	for (const TCHAR* Cls : { TEXT("CNPC_VVampireBoss"), TEXT("CNPC_VSabbatLeader"), TEXT("CNPC_VAndreiBlood"),
			TEXT("CNPC_VAsianVampire"), TEXT("CNPC_VChangBros"), TEXT("CNPC_VSheriffMan") })
	{
		FThink19Fixture F(Cls);
		if (!TestNotNull(FString::Printf(TEXT("%s stands"), Cls), F.Guard))
		{
			continue;
		}
		F.Guard->NextThink = 55.f;
		F.Guard->NPCThink();   // quiet: the Troika body returns at 0x10292e6c
		TestEqual(FString::Printf(TEXT("%s overwrites m_flNextThink = curtime + 0.1"), Cls),
			F.Guard->NextThink, static_cast<float>(F.Now()) + 0.1f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19TzimisceTest,
	"Elysium.Substrate.NpcKernelThink19.Species.Tzimisce", GThink19TestFlags)
bool FElysiumNpcKernelThink19TzimisceTest::RunTest(const FString&)
{
	// `0x103b9040`: `0x102c43f0` (the ignore-collision expiry) BEFORE the Troika body.
	FThink19Fixture F(TEXT("CNPC_VTzimisce"));
	if (!TestNotNull(TEXT("the Tzimisce stands"), F.Guard))
	{
		return false;
	}
	F.Guard->IgnoreCollisionUntil = F.Now() - 1.0;
	F.Guard->NpcFlags.Set(EElysiumNpcFlag2::SCHEDULE_CHANGED);
	F.Guard->NPCThink();
	TestTrue(TEXT("0x103b9043 an expired timer is parked at FLT_MAX"), F.Guard->IgnoreCollisionUntil > 1.0e30);
	TestFalse(TEXT("0x103b904b then the Troika body"), F.Guard->NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19CameraTest,
	"Elysium.Substrate.NpcKernelThink19.Species.Camera", GThink19TestFlags)
bool FElysiumNpcKernelThink19CameraTest::RunTest(const FString&)
{
	// `0x10369120`: no Troika body; disabled returns with no stamp; accepted: RunAI(false), the four
	// Next stamps copied to Last, all five set to curtime + 0.2.
	FThink19Fixture F(TEXT("CNPC_VCamera"));
	if (!TestNotNull(TEXT("the camera stands"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.NextThink = 42.f;
	N.NpcFlags.Set(EElysiumNpcFlag2::SCHEDULE_CHANGED);
	N.NPCThink();
	TestEqual(TEXT("0x10369142 m_bDisableAI: return, no stamp"), N.NextThink, 42.f);
	TestTrue(TEXT("the camera never clears SCHEDULE_CHANGED"), N.NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED));

	N.SetDisableAi(false);
	const double Now = F.Now();
	F.W.World.SetAiEnabled(false);
	N.NextThink = 42.f;
	N.NPCThink();
	TestEqual(TEXT("0x10369161 gate refused with the graph built: no stamp"), N.NextThink, 42.f);
	F.W.World.SetAiEnabled(true);

	N.ScheduleHost.NextUpdate = Now + 1.0;
	N.ScheduleHost.NextNormal = Now + 2.0;
	N.ScheduleHost.NextMove = Now + 3.0;
	N.ScheduleHost.NextAI = Now + 4.0;
	N.NPCThink();
	TestEqual(TEXT("0x1036919b LastUpdate <- NextUpdate"), N.ScheduleHost.LastUpdate, Now + 1.0);
	TestEqual(TEXT("0x103691a7 LastNormal <- NextNormal"), N.ScheduleHost.LastNormal, Now + 2.0);
	TestEqual(TEXT("0x103691ad LastMove <- NextMove"), N.ScheduleHost.LastMove, Now + 3.0);
	TestEqual(TEXT("0x103691b3 LastAI <- NextAI"), N.ScheduleHost.LastAI, Now + 4.0);
	const float Stamp = static_cast<float>(Now + 0.2);
	TestEqual(TEXT("0x103691c8 m_flNextThink = curtime + 0.2"), N.NextThink, Stamp);
	TestEqual(TEXT("0x103691d4 NextUpdate"), N.ScheduleHost.NextUpdate, static_cast<double>(Stamp));
	TestEqual(TEXT("0x103691de NextNormal"), N.ScheduleHost.NextNormal, static_cast<double>(Stamp));
	TestEqual(TEXT("0x103691e4 NextMove"), N.ScheduleHost.NextMove, static_cast<double>(Stamp));
	TestEqual(TEXT("0x103691ea NextAI"), N.ScheduleHost.NextAI, static_cast<double>(Stamp));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19GhoulTest,
	"Elysium.Substrate.NpcKernelThink19.Species.GhoulCroucher", GThink19TestFlags)
bool FElysiumNpcKernelThink19GhoulTest::RunTest(const FString&)
{
	// `0x1037b3f0`: the Troika body, then the burning emitter's rate from m_flPlayerDist.
	FThink19Fixture F(TEXT("CNPC_VGhoulCroucher"));
	FElysiumNpcGhoulCroucher* G = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcGhoulCroucher>() : nullptr;
	if (!TestNotNull(TEXT("the croucher stands"), G))
	{
		return false;
	}
	G->bGhoulSpawnBurning = false;
	G->BurningParticle = F.Other->Handle;
	const int32 Before = G->GhoulParticleRateScaleCalls;
	G->NPCThink();
	TestEqual(TEXT("0x1037b401 +0x6665 clear: no rate"), G->GhoulParticleRateScaleCalls, Before);

	G->bGhoulSpawnBurning = true;
	G->BurningParticle = FElysiumEntityHandle::Invalid();
	G->NPCThink();
	TestEqual(TEXT("0x1037b410 no particle: no rate"), G->GhoulParticleRateScaleCalls, Before);

	G->BurningParticle = F.Other->Handle;
	struct FRow { float Units; float Rate; };
	for (const FRow& Row : { FRow{ 100.f, 1.0f }, FRow{ 400.f, 0.5f }, FRow{ 700.f, 0.1f } })
	{
		G->Senses.Memory.ClosestPlayerDistanceCm = Row.Units * ElysiumMove::U;
		G->NPCThink();
		TestEqual(FString::Printf(TEXT("0x1037b4a3 %.0f units -> rate %.2f (0x101beed0 200..600, floor 0.1)"),
			Row.Units, Row.Rate), G->GhoulLastParticleRateScale, Row.Rate, 0.001f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19MingXiaoTest,
	"Elysium.Substrate.NpcKernelThink19.Species.MingXiao", GThink19TestFlags)
bool FElysiumNpcKernelThink19MingXiaoTest::RunTest(const FString&)
{
	// `0x10394990`: the throwable-mode flags and gaze, the slime trail, the regrow sweep, then the
	// Troika body LAST.
	FThink19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* M = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcMingXiao>() : nullptr;
	if (!TestNotNull(TEXT("Ming Xiao stands"), M))
	{
		return false;
	}
	M->MingXiaoThrowableObjectMode = 0;
	M->NPCThink();
	TestTrue(TEXT("0x10394b0b mode 0: flags2 |= 0x80000400"),
		M->NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY)
		&& M->NpcFlags.HasRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31));

	M->MingXiaoThrowableObjectMode = 1;
	const int32 FacingBefore = M->FacingTargetRequests.Num();
	M->NPCThink();
	TestFalse(TEXT("0x10394aa2 mode 1: MOVE_FACE_ENEMY cleared"), M->NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY));
	TestFalse(TEXT("0x10394aa2 and bit 31"), M->NpcFlags.HasRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31));
	if (TestEqual(TEXT("0x10394b03 slot 518 once"), M->FacingTargetRequests.Num(), FacingBefore + 1))
	{
		TestEqual(TEXT("0.5 ramp (0x10394aba)"), M->FacingTargetRequests.Last().Ramp, 0.5f);
	}
	M->MingXiaoThrowableObjectMode = 7;
	M->NPCThink();
	TestTrue(TEXT("0x10394a8b mode > 4: the default arm"), M->NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY));

	// The regrow sweep: only timer 2 has passed.
	M->MingXiaoTentacleId = INDEX_NONE;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		M->MingXiaoRegrowTimers[Index] = static_cast<double>(FLT_MAX);
	}
	M->MingXiaoRegrowTimers[2] = 5.0;
	M->PrevAnimTime = 10.f;
	M->MingXiaoSeveredTentacleMask = 0x3f;
	M->MingXiaoConnectedTentacleCount = 3;
	M->NPCThink();
	TestEqual(TEXT("0x10394c05 bit 2 cleared"), static_cast<int32>(M->MingXiaoSeveredTentacleMask), 0x3b);
	TestTrue(TEXT("0x10394c0b timer parked at FLT_MAX"), M->MingXiaoRegrowTimers[2] > 1.0e30);
	TestEqual(TEXT("0x10394c1b count + 1"), M->MingXiaoConnectedTentacleCount, 4);

	// A proxy skips the sweep.
	M->MingXiaoTentacleId = 1;
	M->MingXiaoRegrowTimers[3] = 5.0;
	M->NPCThink();
	TestEqual(TEXT("0x10394bd6 proxy: no regrow"), M->MingXiaoRegrowTimers[3], 5.0);
	M->MingXiaoTentacleId = INDEX_NONE;

	// The slime trail: 1 + 6 emitters on a RandomInt(0,99) < 10 think.
	M->MingXiaoEmitters.Reset();
	for (int32 Think = 0; Think < 200; ++Think)
	{
		M->NPCThink();
	}
	TestTrue(TEXT("0x10394b27 some thinks lay a trail"), M->MingXiaoEmitters.Num() > 0);
	TestEqual(TEXT("seven emitters per trail"), M->MingXiaoEmitters.Num() % 7, 0);
	if (M->MingXiaoEmitters.Num() >= 7)
	{
		TestEqual(TEXT("0x10394b50 the root emitter first"), M->MingXiaoEmitters[0],
			FString(TEXT("Ming_xiao_slimetrail_emitter@Bip01 TailRoot")));
		TestEqual(TEXT("0x10394bc8 Tail6 last"), M->MingXiaoEmitters[6],
			FString(TEXT("Ming_xiao_slimetrail_emitter2@Bip01 Tail6")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19NewscasterTest,
	"Elysium.Substrate.NpcKernelThink19.Species.Newscaster", GThink19TestFlags)
bool FElysiumNpcKernelThink19NewscasterTest::RunTest(const FString&)
{
	// `0x103a05b0`: move and AI clocks parked at curtime + 1.0, their Last at curtime, BEFORE the base.
	FThink19Fixture F(TEXT("CNPC_VNewscaster"));
	if (!TestNotNull(TEXT("the newscaster stands"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.Now();
	N.NPCThink();
	TestEqual(TEXT("0x103a05c1 NextMove = curtime + 1.0"), N.ScheduleHost.NextMove, Now + 1.0, 0.0001);
	TestEqual(TEXT("0x103a05d6 NextAI = curtime + 1.0"), N.ScheduleHost.NextAI, Now + 1.0, 0.0001);
	TestEqual(TEXT("0x103a05e5 LastMove = curtime"), N.ScheduleHost.LastMove, Now, 0.0001);
	TestEqual(TEXT("0x103a05f6 LastAI = curtime"), N.ScheduleHost.LastAI, Now, 0.0001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19ZombieTest,
	"Elysium.Substrate.NpcKernelThink19.Species.Zombie", GThink19TestFlags)
bool FElysiumNpcKernelThink19ZombieTest::RunTest(const FString&)
{
	FThink19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* Z = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcZombie>() : nullptr;
	if (!TestNotNull(TEXT("the zombie stands"), Z) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	// `0x103dfa90`: disabled returns before the base and before slot 614.
	Z->NpcFlags.Set(EElysiumNpcFlag2::SCHEDULE_CHANGED);
	Z->ScheduleHost.NextUpdate = 50.0;
	Z->NPCThink();
	TestTrue(TEXT("0x103dfa90 no base pass"), Z->NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED));
	TestEqual(TEXT("and no slot 614"), Z->ScheduleHost.NextUpdate, 50.0);

	// `0x103dfad0`: in the map's first second `__ftol(m_flNextThink) <= 0` resets the clocks.
	Z->SetDisableAi(false);
	const double Now = F.Now();
	Z->NPCThink();
	TestEqual(TEXT("0x103dfad6 slot 614 put the update clock on curtime"), Z->ScheduleHost.NextUpdate, Now);

	// Two seconds in, the stamp truncates to >= 1: no reset.
	FElysiumNpcWorldFixture::Quiet({ Z });
	F.W.Advance(2.0);
	Z->SetDisableAi(false);
	Z->NPCThink();
	TestTrue(TEXT("0x103dfad0 __ftol > 0: the base's own stamps stand"), Z->ScheduleHost.NextUpdate > F.Now());
	FElysiumNpcWorldFixture::Quiet({ Z });
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19ZombieReacquireTest,
	"Elysium.Substrate.NpcKernelThink19.Species.ZombieReacquire", GThink19TestFlags)
bool FElysiumNpcKernelThink19ZombieReacquireTest::RunTest(const FString&)
{
	// `0x103e0a00`, the type-1 zombie's reacquire, every arm.
	FThink19Fixture F(TEXT("CNPC_VZombie"));
	FElysiumNpcZombie* Z = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcZombie>() : nullptr;
	if (!TestNotNull(TEXT("the zombie stands"), Z) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	// No enemy, no closest player: nothing.
	Z->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	Z->ZombieReacquireEnemy();
	TestNull(TEXT("0x103e0a73 no closest player: no enemy"), static_cast<const FElysiumNpcBase*>(Z)->GetEnemy());

	// No enemy, an unobfuscated closest player: slot 596 (enemy AND a memory row).
	Z->Senses.Memory.ClosestPlayer = F.Player->Handle;
	Z->ZombieReacquireEnemy();
	TestTrue(TEXT("0x103e0b42 slot 596 adopts the player"), static_cast<const FElysiumNpcBase*>(Z)->GetEnemy() == F.Player);
	TestNotNull(TEXT("with a memory row"), Z->EnemyMemory.Find(F.Player->Handle));

	// An unobfuscated enemy: slot 544 refreshes it.
	ElysiumNpcEnemy::SetEnemy(*Z, F.Other->Handle);
	Z->ZombieReacquireEnemy();
	TestNotNull(TEXT("0x103e0a60 slot 544 remembers the enemy"), Z->EnemyMemory.Find(F.Other->Handle));

	// An obfuscated enemy it remembers: marked eluded.
	F.Player->Sheet.SetBase(EElysiumTraitContainer::ActiveDisciplines, 8, 1);
	F.Player->Sheet.RecomputeCurrent(nullptr);
	F.Player->Disciplines.bObfuscateCloaked = true;
	ElysiumNpcEnemy::SetEnemy(*Z, F.Player->Handle);
	Z->ZombieReacquireEnemy();
	TestTrue(TEXT("0x103e0a43 0x10279c20 MarkAsEluded"), Z->EnemyMemory.IsEluded(F.Player->Handle));

	// No enemy, an obfuscated closest player: SetEnemy directly, no memory row.
	Z->EnemyMemory.ClearMemory(F.Player->Handle);
	ElysiumNpcEnemy::SetEnemy(*Z, FElysiumEntityHandle::Invalid());
	Z->ZombieReacquireEnemy();
	TestTrue(TEXT("0x103e0b04 SetEnemy(player)"), static_cast<const FElysiumNpcBase*>(Z)->GetEnemy() == F.Player);
	TestNull(TEXT("and no slot-544 row"), Z->EnemyMemory.Find(F.Player->Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19FrenzyShadowTest,
	"Elysium.Substrate.NpcKernelThink19.Species.FrenzyShadow", GThink19TestFlags)
bool FElysiumNpcKernelThink19FrenzyShadowTest::RunTest(const FString&)
{
	// `0x10375e50`: the controller think, then, in COMBAT with an enemy and COND 0x46 clear, slot 544.
	FThink19Fixture F(TEXT("CNPC_VFrenzyShadow"));
	if (!TestNotNull(TEXT("the shadow stands"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.EnemyMemory.ClearMemory(F.Other->Handle);
	N.SetState(2);
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	N.NPCThink();
	TestNull(TEXT("0x10375e7f COND 0x46 set: no slot 544"), N.EnemyMemory.Find(F.Other->Handle));
	N.Cognition.Conditions.Clear(EElysiumNpcCond::SeeEnemy);
	N.NPCThink();
	TestNotNull(TEXT("0x10375e99 slot 544 refreshes the enemy"), N.EnemyMemory.Find(F.Other->Handle));
	TestEqual(TEXT("0x103a470d the controller's slot 614 ran"), N.ScheduleHost.NextUpdate, F.Now());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelThink19WerewolfTest,
	"Elysium.Substrate.NpcKernelThink19.Species.Werewolf", GThink19TestFlags)
bool FElysiumNpcKernelThink19WerewolfTest::RunTest(const FString&)
{
	// Damaged19 `0x103cb590`.
	FThink19Fixture F(TEXT("CNPC_VWerewolf"));
	FElysiumNpcWerewolf* WW = F.Guard != nullptr ? F.Guard->AsSpecies<FElysiumNpcWerewolf>() : nullptr;
	if (!TestNotNull(TEXT("the werewolf stands"), WW))
	{
		return false;
	}
	const double Now = F.Now();

	// `werewolf_draw_hints`: the overlay, slot 614, return before anything else.
	ElysiumNpcTunables::SetConVar(ElysiumNpcTunables::EConVar::WerewolfDrawHints, 1.f);
	WW->bWerewolfHintDataInitialized = false;
	WW->ScheduleHost.NextUpdate = 50.0;
	WW->NPCThink();
	TestEqual(TEXT("0x103cb63b the hint overlay"), WW->WerewolfDrawHintOverlayCalls, 1);
	TestEqual(TEXT("0x103cb644 slot 614"), WW->ScheduleHost.NextUpdate, Now);
	TestFalse(TEXT("0x103cb652 returned before the hint data"), WW->bWerewolfHintDataInitialized);
	ElysiumNpcTunables::ResetConVars();

	// AI disabled: the hint data is built, then everything else -- the Troika body included -- skipped.
	F.W.World.SetAiEnabled(false);
	WW->NpcFlags.Set(EElysiumNpcFlag2::SCHEDULE_CHANGED);
	const int32 ZonesBefore = WW->WerewolfZoneTriggerFires;
	WW->ScheduleHost.NextUpdate = 50.0;
	WW->NPCThink();
	TestTrue(TEXT("0x103cb675 +0x66a0 = 1"), WW->bWerewolfHintDataInitialized);
	TestEqual(TEXT("0x103cb670 the zone opener ran (no trigger_werewolf_zone stands: no fire)"),
		WW->WerewolfZoneTriggerFires, ZonesBefore);
	TestTrue(TEXT("0x103cb683 g_AIDisabled: no Troika body"), WW->NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED));
	TestEqual(TEXT("0x103cb71c but slot 614"), WW->ScheduleHost.NextUpdate, Now);
	F.W.World.SetAiEnabled(true);

	// AI on: TeleportOut on the 0x20 bit, the round robin with an enemy, then the Troika body.
	WW->WerewolfHintFlags = 0x20u;
	ElysiumNpcEnemy::SetEnemy(*WW, F.Other->Handle);
	WW->WerewolfLastRoundRobinArm = INDEX_NONE;
	WW->NPCThink();
	TestEqual(TEXT("0x103cb698 TeleportOut zeroed the request word"), static_cast<int32>(WW->WerewolfHintFlags), 0);
	TestTrue(TEXT("0x103cb6c1 one round-robin arm ran"),
		WW->WerewolfLastRoundRobinArm >= 0 && WW->WerewolfLastRoundRobinArm <= 4);
	TestFalse(TEXT("0x103cb6f4 the Troika body ran"), WW->NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED));

	// No enemy: no round robin.
	ElysiumNpcEnemy::SetEnemy(*WW, FElysiumEntityHandle::Invalid());
	WW->WerewolfLastRoundRobinArm = INDEX_NONE;
	WW->NPCThink();
	TestEqual(TEXT("0x103cb6a9 no enemy: none of the five"), WW->WerewolfLastRoundRobinArm, int32(INDEX_NONE));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
