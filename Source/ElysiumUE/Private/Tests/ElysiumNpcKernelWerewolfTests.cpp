// Story 0019/8 (29e under the strict verdict), family **Werewolf19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelWerewolf19.` and the retail address.
//
// Owns (Werewolf19's `rule` rows): 0x103cc450 CNPC_VWerewolf::UpdateConditionShouldBreakHint,
// 0x103d0ec0 CNPC_VWerewolf::FindBreakHint, 0x103d1200 CNPC_VWerewolf::FindEgressHint, 0x103d2070
// CNPC_VWerewolf::IsImperativeMoveHint, 0x103d3c20 CNPC_VWerewolf::FindTeleportHint, 0x103da0a0
// CNPC_VWerewolf::IsEnemyUnreachable, 0x102c44e0 SetFollowerBoss, 0x103cac20 FUN_103cac20,
// 0x103d2810 CNPC_VWerewolf::IsImperativeRandomMoveHint, 0x103d2a10 CNPC_VWerewolf::FindMoveHint,
// 0x102c4430 FUN_102c4430, 0x10397380 FUN_10397380, 0x103cc320
// CNPC_VWerewolf::UpdateConditionEnemyUnreachable, 0x103cf770
// CNPC_VWerewolf::CheckAllRandomMoveHints, 0x103d14f0 CNPC_VWerewolf::FindRandomMoveHint,
// 0x10271d10 CAI_BaseNPC::CheckTarget, 0x103cc5c0 CNPC_VWerewolf::UpdateConditionCanSpecialMove.
//
// Story 8, lane L12. Every assertion is read off the decompiled C or the listing (the address in the
// assertion text). The world stands a `CNPC_VWerewolf` (`npc_VWerewolf`) and Werewolf hints
// (`info_node_werewolf_hint`, which lives as `ai_hint` on the world's hint list, head = last
// authored). Two substrate seams are driven explicitly: the navigator's `HasPath` (`0x103d0db0` ->
// `0x102fdcc0`) through `WerewolfHasPathAnswers`, and the hint groundpoints (`+0x6714`) through the
// `WerewolfHintGroundpoints` rows, because the ground trace answers `vec3_invalid` headless.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GWerewolf19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace NpcKernelWerewolf19Tests
{
	struct FWerewolf19HintSpec
	{
		const TCHAR* Name;
		int32 HintType;
		FVector Origin;
	};

	EElysiumNpcCond Cond(int32 Retail)
	{
		return static_cast<EElysiumNpcCond>(Retail);
	}

	struct FWerewolf19Fixture
	{
		FElysiumNpcWorldFixture W;
		FElysiumNpcWerewolf* Wolf = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumNpc* Third = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FWerewolf19Fixture(std::initializer_list<FWerewolf19HintSpec> Hints = {})
			: W([&Hints]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("werewolf19_kernel"), 1919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("wolf"), FVector::ZeroVector, TEXT("CNPC_VWerewolf"));
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					Builder.AddNpc(TEXT("third"), FVector(0.f, 400.f, 0.f), TEXT("npc_VHumanCombatant"));
					for (const FWerewolf19HintSpec& Spec : Hints)
					{
						FElysiumEntityDef& Def =
							Builder.AddEntity(TEXT("info_node_werewolf_hint"), Spec.Name, Spec.Origin);
						Def.Keys.Add(TEXT("hinttype"), FString::FromInt(Spec.HintType));
					}
					return Builder;
				}())
		{
			Wolf = W.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
			Other = W.Npc(TEXT("other"));
			Third = W.Npc(TEXT("third"));
			Player = W.Player();
			FElysiumNpcWorldFixture::Quiet({ Wolf, Other, Third });
		}

		int32 HintIndex(const TCHAR* Name)
		{
			const FElysiumEntity* Entity = W.World.FindByName(Name);
			return Entity != nullptr ? Entity->Handle.Index : INDEX_NONE;
		}

		// A `+0x6714` record: the hint and its groundpoint (SOURCE units).
		void Groundpoint(const TCHAR* Name, const FVector& Units)
		{
			FElysiumNpc::FWerewolfHintGroundpoint Row;
			Row.HintNode = HintIndex(Name);
			Row.GroundpointUnits = Units;
			Wolf->WerewolfHintGroundpoints.Add(Row);
		}

		void ClosestPlayerLive()
		{
			if (Player != nullptr)
			{
				Wolf->Senses.Memory.ClosestPlayer = Player->Handle;
			}
		}
	};
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19ShouldBreakHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.UpdateConditionShouldBreakHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19ShouldBreakHintTest::RunTest(const FString&)
{
	// 0x103cc450
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F({ { TEXT("break_a"), 0x3aa3, FVector(100.f, 0.f, 0.f) } });
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	const int32 Break = F.HintIndex(TEXT("break_a"));
	F.Groundpoint(TEXT("break_a"), FVector(3.f, 0.f, 0.f));

	N.Cognition.Conditions.Set(Cond(0x7b));
	N.WerewolfBreakHintNode = Break;
	N.WerewolfHintFlags = 0u;
	N.UpdateConditionShouldBreakHint();
	TestFalse(TEXT("0x103cc4c2 clears 0x7b first"), N.Cognition.Conditions.Has(Cond(0x7b)));
	TestEqual(TEXT("0x103cc4d4 +0x66e8 bit 2 clear nulls m_pBreakHint"), N.WerewolfBreakHintNode,
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("0x103cc4e6 no hint, no path asked"), N.HasPathQueries.Num(), 0);

	N.WerewolfBreakHintNode = Break;
	N.WerewolfHintFlags = 0x2u;
	N.WerewolfHasPathAnswers = { false };
	N.UpdateConditionShouldBreakHint();
	TestEqual(TEXT("0x103cc4d2 bit 2 set keeps the hint"), N.WerewolfBreakHintNode, Break);
	TestFalse(TEXT("0x103cc546 no path, no 0x7b"), N.Cognition.Conditions.Has(Cond(0x7b)));
	if (TestEqual(TEXT("0x103cc53f one path ask"), N.HasPathQueries.Num(), 1))
	{
		TestTrue(TEXT("0x103cc50d from the origin"),
			N.HasPathQueries[0].StartUnits.Equals(N.Origin / ElysiumMove::U, 0.01));
		TestTrue(TEXT("0x103cc502 to the hint's groundpoint"),
			N.HasPathQueries[0].EndUnits.Equals(FVector(3.f, 0.f, 0.f), 0.01));
	}

	N.WerewolfHasPathAnswers = { true };
	N.UpdateConditionShouldBreakHint();
	TestTrue(TEXT("0x103cc557 a valid reachable 0x3aa3 hint sets 0x7b"),
		N.Cognition.Conditions.Has(Cond(0x7b)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19FindBreakHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.FindBreakHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19FindBreakHintTest::RunTest(const FString&)
{
	// 0x103d0ec0
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F({ { TEXT("break_far"), 0x3aa3, FVector(200.f, 0.f, 0.f) },
		{ TEXT("break_near"), 0x3aa3, FVector(100.f, 0.f, 0.f) } });
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	F.Groundpoint(TEXT("break_far"), FVector(100.f, 0.f, 0.f));
	F.Groundpoint(TEXT("break_near"), FVector(50.f, 0.f, 0.f));

	N.WerewolfMorphTimerC = -5.f;
	N.Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("0x103d0ff4 no closest player answers false"), N.FindBreakHint());
	TestEqual(TEXT("0x103d0fc8 but the search stamp +0x66d8 is written first"), N.WerewolfMorphTimerC,
		static_cast<float>(F.W.World.NowSeconds()));

	F.ClosestPlayerLive();
	N.HasPathQueries.Reset();
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103d1143 a reachable break hint is found"), N.FindBreakHint());
	// The list head is the LAST authored hint (`break_near`): nearest first, path true, best 50; the
	// far one (distance 100) fails `0x103d10d0` and is never asked a path.
	TestEqual(TEXT("0x103d1127 the nearest reachable hint is m_pBreakHint"), N.WerewolfBreakHintNode,
		F.HintIndex(TEXT("break_near")));
	TestEqual(TEXT("0x103d10d0 a farther candidate is not asked a path"), N.HasPathQueries.Num(), 1);

	N.HasPathQueries.Reset();
	N.WerewolfMorphTimerC = -7.f;
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103d0fab a held valid reachable hint answers true"), N.FindBreakHint());
	TestEqual(TEXT("0x103d0f97 before any stamp"), N.WerewolfMorphTimerC, -7.f);

	N.WerewolfHasPathAnswers = { false, false, false };
	TestFalse(TEXT("0x103d110d no path to any candidate"), N.FindBreakHint());
	TestEqual(TEXT("0x103d1127 writes null"), N.WerewolfBreakHintNode, static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19FindEgressHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.FindEgressHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19FindEgressHintTest::RunTest(const FString&)
{
	// 0x103d1200
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F({ { TEXT("egress"), 0x3aa8, FVector(100.f, 0.f, 0.f) },
		{ TEXT("move"), 0x3a9c, FVector(0.f, 100.f, 0.f) } });
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	N.SetMoveHint(F.HintIndex(TEXT("move")), true);
	N.WerewolfHasPathAnswers = { true };
	TestFalse(TEXT("0x103d1444 the scan answers false"), N.FindEgressHint());
	// No enemy: the held hint is cleared without a path ask (`0x103d1289` -> `0x103d1356`), then the
	// one `0x3aa8` hint is asked a path (true) and slot 617 — which answers false with no enemy —
	// breaks the loop before `SetMoveHint`.
	TestEqual(TEXT("0x103d1356 ClearMoveHint"), N.MoveHintNode, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("0x103d13be only the 0x3aa8 hint is asked a path"), N.HasPathQueries.Num(), 1);
	TestFalse(TEXT("0x103d1418 a false slot 617 breaks before SetMoveHint"),
		N.Cognition.Conditions.Has(Cond(0x78)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19IsImperativeMoveHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.IsImperativeMoveHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19IsImperativeMoveHintTest::RunTest(const FString&)
{
	// 0x103d2070
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	auto Hint = [](int32 Type, const TCHAR* Name, int32 Disabled = 0)
	{
		FElysiumNpcBase::FHintWords H;
		H.bValid = true;
		H.HintType = Type;
		H.Name = Name;
		H.Disabled = Disabled;
		return H;
	};

	// Gate A.
	N.WerewolfHintFlags = 0x100u;
	TestTrue(TEXT("0x103d2142 bit 0x100, bit 4 clear: 0x3aa6 is imperative, no path"),
		N.IsImperativeMoveHint(Hint(0x3aa6, TEXT("any"))));
	TestEqual(TEXT("0x103d2651 without a path ask"), N.HasPathQueries.Num(), 0);
	N.WerewolfHintFlags = 0x180u;
	N.WerewolfDoorState = 2;   // `0x103d1e50` answers true on door state 2
	TestTrue(TEXT("0x103d2121 bit 0x80 with the door open: 0x3aaa is imperative"),
		N.IsImperativeMoveHint(Hint(0x3aaa, TEXT("any"))));
	TestFalse(TEXT("0x103d2115 the same arm skips the 0x3aa6 test"),
		N.IsImperativeMoveHint(Hint(0x3aa6, TEXT("any"))));
	N.WerewolfHintFlags = 0x104u;
	TestFalse(TEXT("0x103d2101 bit 4 closes gate A (and D needs 0x100 clear)"),
		N.IsImperativeMoveHint(Hint(0x3aa6, TEXT("any"))));

	// Gate B.
	N.WerewolfHintFlags = 0x200u;
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103d217f wall_b_1_breakthrough_r at 0x3a9b with a path"),
		N.IsImperativeMoveHint(Hint(0x3a9b, TEXT("wall_b_1_breakthrough_r"))));
	TestFalse(TEXT("0x103d2166 a disabled hint answers false under bit 0x200"),
		N.IsImperativeMoveHint(Hint(0x3a9b, TEXT("wall_b_1_breakthrough_r"), 1)));
	N.HasPathQueries.Reset();
	TestFalse(TEXT("0x102d1220 the name must match at its own type"),
		N.IsImperativeMoveHint(Hint(0x3a9c, TEXT("wall_b_1_breakthrough_r"))));
	TestEqual(TEXT("0x103d2216 no match, no path ask"), N.HasPathQueries.Num(), 0);

	// Gate C.
	N.WerewolfHintFlags = 0x400u;
	N.WerewolfHasPathAnswers = { false };
	TestFalse(TEXT("0x103d2347 tramdoor match without a path falls through to D and fails"),
		N.IsImperativeMoveHint(Hint(0x3a9d, TEXT("tramdoor_a_1_squeeze_left"))));

	// Gate D.
	N.WerewolfHintFlags = 0x4u;
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103d237e 0x3aa7 goes straight to the path test"),
		N.IsImperativeMoveHint(Hint(0x3aa7, TEXT("any"))));
	N.WerewolfDoorState = 2;
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103d2491 door open: jump_to_platform_hint_2 at 0x3aa4"),
		N.IsImperativeMoveHint(Hint(0x3aa4, TEXT("jump_to_platform_hint_2"))));
	TestFalse(TEXT("0x103d2414 door open: another type fails"),
		N.IsImperativeMoveHint(Hint(0x3a9c, TEXT("archway_a_5_squeeze_front"))));
	N.WerewolfDoorState = 0;   // `0x103d1e50` answers false on door state 0
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103d258b door shut: archway_a_5_squeeze_front at 0x3a9c"),
		N.IsImperativeMoveHint(Hint(0x3a9c, TEXT("archway_a_5_squeeze_front"))));
	TestFalse(TEXT("0x103d2600 door shut: an unlisted name fails"),
		N.IsImperativeMoveHint(Hint(0x3a9c, TEXT("archway_c_squeeze_front"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19FindTeleportHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.FindTeleportHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19FindTeleportHintTest::RunTest(const FString&)
{
	// 0x103d3c20
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F({ { TEXT("tele"), 0x3a99, FVector(0.f, 200.f, 0.f) } });
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	const int32 Tele = F.HintIndex(TEXT("tele"));
	F.Groundpoint(TEXT("tele"), FVector(10.f, 0.f, 0.f));
	F.ClosestPlayerLive();

	N.WerewolfMorphTimerC = static_cast<float>(F.W.World.NowSeconds());
	TestFalse(TEXT("0x103d3ce2 inside the 0.15 s retry interval answers false"), N.FindTeleportHint());

	N.WerewolfMorphTimerC = -1.f;
	N.WerewolfMorphTimerB = -1.f;   // a later frame: `+0x66d4 != framecount` (the search's once-per-frame gate)
	N.TeleportHintNode = Tele;
	TestTrue(TEXT("0x103d3d01 a held teleport hint answers true"), N.FindTeleportHint());
	TestEqual(TEXT("0x103d3d19 before the stamp"), N.WerewolfMorphTimerC, -1.f);

	N.TeleportHintNode = INDEX_NONE;
	N.WerewolfTimeTeleportedOut = -100.0;   // 25.0 + -100 < now: the time arm accepts (0x103d401a)
	TestTrue(TEXT("0x103d4224 a valid hint the enemy cannot see is installed"), N.FindTeleportHint());
	TestEqual(TEXT("0x103d4213 SetTeleportHint(best)"), N.TeleportHintNode, Tele);
	TestEqual(TEXT("0x103d4203 +0x66ac = best"), N.WerewolfWord66ac, Tele);
	TestEqual(TEXT("0x103d401a the time arm needs no path ask"), N.HasPathQueries.Num(), 0);

	N.TeleportHintNode = INDEX_NONE;
	N.WerewolfWord66ac = 0;
	N.WerewolfMorphTimerC = -1.f;
	N.WerewolfMorphTimerB = -1.f;   // a later frame: `+0x66d4 != framecount` (the search's once-per-frame gate)
	N.WerewolfTimeTeleportedOut = F.W.World.NowSeconds();
	N.WerewolfHasPathAnswers = { false };
	TestFalse(TEXT("0x103d409e a failed full-path check rejects the only hint"), N.FindTeleportHint());
	TestEqual(TEXT("0x103d4052 werewolf_teleport_full_path_check asks HasPath(target, reference)"),
		N.HasPathQueries.Num(), 1);
	TestEqual(TEXT("0x103d4203 +0x66ac = null"), N.WerewolfWord66ac, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19IsEnemyUnreachableTest,
	"Elysium.Arm.NpcKernelWerewolf19.IsEnemyUnreachable", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19IsEnemyUnreachableTest::RunTest(const FString&)
{
	// 0x103da0a0
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	N.bWerewolfTaskFailed = false;
	TestTrue(TEXT("0x103da159 no enemy sets the +0x66a1 latch"), N.IsEnemyUnreachable());
	TestEqual(TEXT("0x103da1ae and asks a path to the chase point"), N.HasPathQueries.Num(), 1);
	// The same frame answers the latch without asking again (`0x103da122` `+0x66a4 == framecount`).
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103da122 the same frame answers the memoised latch"), N.IsEnemyUnreachable());
	TestEqual(TEXT("0x103da122 and asks no second path"), N.HasPathQueries.Num(), 1);
	N.WerewolfMorphTimerA = -1.f;   // a later frame
	TestFalse(TEXT("0x103da1b7 a path clears the latch"), N.IsEnemyUnreachable());
	TestFalse(TEXT("0x103da1be the latch is the answer"), N.bWerewolfTaskFailed);
	TestEqual(TEXT("0x103da136 +0x66a4 takes the engine frame"), N.WerewolfMorphTimerA,
		static_cast<float>(N.EngineFrameNumber()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19ResetHuntStateTest,
	"Elysium.Arm.NpcKernelWerewolf19.WerewolfResetHuntState", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19ResetHuntStateTest::RunTest(const FString&)
{
	// 0x103cac20
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.WerewolfHintFlags = 0xffu;
	N.WerewolfWord66ac = 9;
	N.WerewolfMoveHintSearchStart = 9;
	N.bWerewolfPlayFrustration = true;
	const float DistanceB = N.WerewolfTeleportDistanceB;
	N.WerewolfResetHuntState();
	TestFalse(TEXT("0x103cac2a SetEnemy(NULL)"), N.BaseMemory.Enemy.IsSet());
	TestFalse(TEXT("0x103cac32 m_hClosestPlayer = -1"), N.Senses.Memory.ClosestPlayer.IsSet());
	TestEqual(TEXT("0x103cacbd +0x66e8 = 0"), static_cast<int32>(N.WerewolfHintFlags), 0);
	TestEqual(TEXT("0x103cacab +0x66ac = 0"), N.WerewolfWord66ac, 0);
	TestEqual(TEXT("0x103caca5 +0x66b8 = 0"), N.WerewolfMoveHintSearchStart, 0);
	TestFalse(TEXT("0x103cac4a +0x66a9 = 0"), N.bWerewolfPlayFrustration);
	// Retail defect, reproduced: `0x103cad6d` is `FADD [ESI+0x66d0]` — the floor ACCUMULATES.
	TestEqual(TEXT("0x103cad6d +0x66d0 += sqrt(512)"), N.WerewolfTeleportDistanceB,
		DistanceB + FMath::Sqrt(512.f), 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19IsImperativeRandomMoveHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.IsImperativeRandomMoveHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19IsImperativeRandomMoveHintTest::RunTest(const FString&)
{
	// 0x103d2810
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	FElysiumNpcBase::FHintWords Egress;
	Egress.bValid = true;
	Egress.HintType = 0x3aa8;
	N.WerewolfHintFlags = 0u;

	N.Senses.Memory.ClosestPlayerDistanceCm = 10.f * ElysiumMove::U;   // inside werewolf_pursuit_distance 800
	TestFalse(TEXT("0x103d28bf a near player falls through to IsImperativeMoveHint"),
		N.IsImperativeRandomMoveHint(Egress));
	TestEqual(TEXT("0x103d297e and asks no path"), N.HasPathQueries.Num(), 0);

	N.Senses.Memory.ClosestPlayerDistanceCm = 5000.f * ElysiumMove::U;
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103d2978 far player, slot 617 false, a path: imperative"),
		N.IsImperativeRandomMoveHint(Egress));

	FElysiumNpcBase::FHintWords Other = Egress;
	Other.HintType = 0x3a9c;
	TestFalse(TEXT("0x103d288e any other type is IsImperativeMoveHint's answer"),
		N.IsImperativeRandomMoveHint(Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19FindMoveHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.FindMoveHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19FindMoveHintTest::RunTest(const FString&)
{
	// 0x103d2a10
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F({ { TEXT("move"), 0x3a9c, FVector(0.f, 100.f, 0.f) } });
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	const int32 Move = F.HintIndex(TEXT("move"));
	F.Groundpoint(TEXT("move"), FVector(100.f, 0.f, 0.f));
	F.ClosestPlayerLive();
	N.WerewolfHintFlags = 0x4u;   // `ShouldPursueEnemy` answers true outright on bit 4

	N.WerewolfMorphTimerC = static_cast<float>(F.W.World.NowSeconds());
	TestFalse(TEXT("0x103d2ace inside the 0.5 s retry interval"), N.FindMoveHint());

	N.WerewolfMorphTimerC = -1.f;
	N.WerewolfMorphTimerB = -1.f;   // a later frame: `+0x66d4 != framecount` (the search's once-per-frame gate)
	N.WerewolfHasPathAnswers = { true, true };
	TestTrue(TEXT("0x103d2f83 a valid hint with both paths is chosen"), N.FindMoveHint());
	TestEqual(TEXT("0x103d2f72 SetMoveHint(hint, false)"), N.MoveHintNode, Move);
	TestFalse(TEXT("0x103d2f72 not random"), N.bRandomHint);
	TestEqual(TEXT("0x103d2f61 +0x66b8 = the winner"), N.WerewolfMoveHintSearchStart, Move);
	TestEqual(TEXT("0x103d2e2f / 0x103d2e79 two path asks"), N.HasPathQueries.Num(), 2);

	N.WerewolfMorphTimerC = -1.f;
	N.WerewolfMorphTimerB = -1.f;   // a later frame: `+0x66d4 != framecount` (the search's once-per-frame gate)
	TestTrue(TEXT("0x103d2b05 a held move hint answers true"), N.FindMoveHint());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19ShareEnemyWithAllyTest,
	"Elysium.Arm.NpcKernelWerewolf19.ShareEnemyWithAlly", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19ShareEnemyWithAllyTest::RunTest(const FString&)
{
	// 0x10397380
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf) || !TestNotNull(TEXT("the ally"), F.Third))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	F.Third->Cognition.Conditions.Reset();
	N.ShareEnemyWithAlly(F.Third);
	TestFalse(TEXT("0x10397390 no enemy: nothing is shared"),
		F.Third->Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));

	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.ShareEnemyWithAlly(F.Third);
	TestTrue(TEXT("0x103973bd COND_NEW_ENEMY on the ally"),
		F.Third->Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));
	TestEqual(TEXT("0x1039739e the ally's D_HT row for the enemy, priority 5"),
		F.Third->Relationships.ResolvePriority(F.Other->Handle, FString()), 5);
	// `0x1039739e` is `AddEntityRelationship` (`0x10005849` -> `0x10332ca0`), which overwrites a
	// higher-priority row (`SetEntity` would refuse it).
	F.Third->Relationships.AddEntityRelationship(F.Other->Handle, EElysiumRelationship::Like, 10);
	N.ShareEnemyWithAlly(F.Third);
	TestEqual(TEXT("0x1039739e a priority-10 row is overwritten at 5"),
		F.Third->Relationships.ResolvePriority(F.Other->Handle, FString()), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19SetFollowerBossTest,
	"Elysium.Arm.NpcKernelWerewolf19.SetFollowerBoss", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19SetFollowerBossTest::RunTest(const FString&)
{
	// 0x102c44e0
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpc& N = *F.Other;
	TestFalse(TEXT("0x102c4507 an unknown name refuses"), N.SetFollowerBoss(TEXT("nobody")));
	TestFalse(TEXT("0x102c45d7 and leaves +0x647c = -1"), N.FollowerBoss.IsSet());
	TestFalse(TEXT("0x102c4579 this NPC itself refuses"), N.SetFollowerBoss(TEXT("other")));
	TestFalse(TEXT("0x102c45d7 again -1"), N.FollowerBoss.IsSet());

	ElysiumNpcEnemy::SetEnemy(N, F.Wolf->Handle);
	N.SetFrenziedWord(0x1u);
	TestTrue(TEXT("0x102c45cb a live foreign boss is accepted"), N.SetFollowerBoss(TEXT("wolf")));
	TestTrue(TEXT("0x102c44ff +0x647c is its handle"), N.FollowerBoss == F.Wolf->Handle);
	TestEqual(TEXT("0x102c45c6 m_bfNPCFrenziedFlags |= 0x3008"), static_cast<int32>(N.FrenziedWord), 0x3009);
	TestNull(TEXT("0x102c45bb ResetAiState(false, false) dropped the enemy"), N.GetEnemy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19SetFollowerBossNameTest,
	"Elysium.Arm.NpcKernelWerewolf19.SetFollowerBossName", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19SetFollowerBossNameTest::RunTest(const FString&)
{
	// 0x102c4430
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpc& N = *F.Other;
	N.SetFollowerBossName(FString(TEXT("wolf")));
	TestEqual(TEXT("0x102c4447 +0x6478 keeps a non-empty name"), N.FollowerBossName, FString(TEXT("wolf")));
	TestTrue(TEXT("0x102c4439 SetFollowerBoss ran"), N.FollowerBoss == F.Wolf->Handle);
	N.SetFollowerBossName(FString());
	TestTrue(TEXT("0x102c4447 the empty string is normalised to NULL"), N.FollowerBossName.IsEmpty());
	TestFalse(TEXT("0x102c4439 and SetFollowerBoss(\"\") refused"), N.FollowerBoss.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19UpdateConditionEnemyUnreachableTest,
	"Elysium.Arm.NpcKernelWerewolf19.UpdateConditionEnemyUnreachable", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19UpdateConditionEnemyUnreachableTest::RunTest(const FString&)
{
	// 0x103cc320. The previous-pass latch `+0x66a8` is held SET so the `CheckAllMoveHints` edge
	// (lane L11's row) is not entered; the edge's gate is asserted through the latch.
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	N.WerewolfSnapWordA = 1;
	N.bWerewolfPlayFrustration = false;
	N.UpdateConditionEnemyUnreachable();
	TestTrue(TEXT("0x103cc3a1 unreachable sets 0x59"), N.Cognition.Conditions.Has(Cond(0x59)));
	TestFalse(TEXT("0x103cc3aa and clears 0x79"), N.Cognition.Conditions.Has(Cond(0x79)));
	TestFalse(TEXT("0x103cc3d6 a set latch skips the edge"), N.bWerewolfPlayFrustration);
	TestEqual(TEXT("0x103cc400 +0x66a8 = HasCondition(0x59)"), N.WerewolfSnapWordA, 1);

	N.WerewolfHasPathAnswers = { true };
	N.WerewolfMorphTimerA = -1.f;   // a later frame: `IsEnemyUnreachable`'s memo (`+0x66a4`) is stale
	N.UpdateConditionEnemyUnreachable();
	TestFalse(TEXT("0x103cc3b5 reachable clears 0x59"), N.Cognition.Conditions.Has(Cond(0x59)));
	TestTrue(TEXT("0x103cc3c9 and sets 0x79"), N.Cognition.Conditions.Has(Cond(0x79)));
	TestEqual(TEXT("0x103cc400 +0x66a8 = 0"), N.WerewolfSnapWordA, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19CheckAllRandomMoveHintsTest,
	"Elysium.Arm.NpcKernelWerewolf19.CheckAllRandomMoveHints", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19CheckAllRandomMoveHintsTest::RunTest(const FString&)
{
	// 0x103cf770
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F({ { TEXT("move"), 0x3a9c, FVector(0.f, 100.f, 0.f) } });
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	const int32 Move = F.HintIndex(TEXT("move"));
	N.SetMoveHint(Move, false);
	N.WerewolfHasPathAnswers = { true };
	TestTrue(TEXT("0x103cf855 a held hint with a path to its target answers true"),
		N.CheckAllRandomMoveHints());
	TestEqual(TEXT("0x103cf855 and keeps it"), N.MoveHintNode, Move);

	N.WerewolfMoveHintSearchStart = 9;
	N.WerewolfMorphTimerC = -3.f;
	N.WerewolfHasPathAnswers = { false };
	// The held hint has no path (`0x103cf840`) and is cleared; the walk then reaches the same hint,
	// valid, near, but with no path to its groundpoint (script exhausted), and runs off the list.
	TestFalse(TEXT("0x103cfa48 nothing found answers false"), N.CheckAllRandomMoveHints());
	TestEqual(TEXT("0x103cf858 the held hint is cleared"), N.MoveHintNode, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("0x103cf885 +0x66b8 = NULL"), N.WerewolfMoveHintSearchStart, 0);
	TestEqual(TEXT("0x103cf839 twice, then 0x103cf9e8 once: three path asks"), N.HasPathQueries.Num(), 3);
	TestEqual(TEXT("0x103cf88b +0x66d8 = curtime"), N.WerewolfMorphTimerC,
		static_cast<float>(F.W.World.NowSeconds()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19FindRandomMoveHintTest,
	"Elysium.Arm.NpcKernelWerewolf19.FindRandomMoveHint", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19FindRandomMoveHintTest::RunTest(const FString&)
{
	// 0x103d14f0
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	N.WerewolfMorphTimerC = static_cast<float>(F.W.World.NowSeconds());
	TestFalse(TEXT("0x103d15ae inside the 0.25 s retry interval"), N.FindRandomMoveHint());
	N.WerewolfMorphTimerC = -1.f;
	N.MoveHintNode = 5;
	TestTrue(TEXT("0x103d15cd a held move hint answers true"), N.FindRandomMoveHint());
	N.MoveHintNode = INDEX_NONE;
	TestFalse(TEXT("0x103d1616 an empty hint list answers false"), N.FindRandomMoveHint());
	TestEqual(TEXT("0x103d1609 after the stamp"), N.WerewolfMorphTimerC,
		static_cast<float>(F.W.World.NowSeconds()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19CheckTargetTest,
	"Elysium.Arm.NpcKernelWerewolf19.CheckTarget", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19CheckTargetTest::RunTest(const FString&)
{
	// 0x10271d10
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F;
	if (!TestNotNull(TEXT("the other NPC stands"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Other;
	N.Cognition.Conditions.Set(Cond(0x4b));
	const int32 Before = N.UpdateTargetPosCalls;
	N.CheckTarget(nullptr);
	TestFalse(TEXT("0x10271d6e clears 0x4b"), N.Cognition.Conditions.Has(Cond(0x4b)));
	TestTrue(TEXT("0x10271da3 an invisible target sets 0x49"), N.Cognition.Conditions.Has(Cond(0x49)));
	TestEqual(TEXT("0x10271db7 UpdateTargetPos runs unconditionally"), N.UpdateTargetPosCalls, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelWerewolf19CanSpecialMoveTest,
	"Elysium.Arm.NpcKernelWerewolf19.UpdateConditionCanSpecialMove", GWerewolf19Flags)
bool FElysiumNpcKernelWerewolf19CanSpecialMoveTest::RunTest(const FString&)
{
	// 0x103cc5c0
	using namespace NpcKernelWerewolf19Tests;
	FWerewolf19Fixture F({ { TEXT("imperative"), 0x3aa6, FVector(0.f, 100.f, 0.f) } });
	if (!TestNotNull(TEXT("the werewolf stands"), F.Wolf))
	{
		return false;
	}
	FElysiumNpcWerewolf& N = *F.Wolf;
	const int32 Hint = F.HintIndex(TEXT("imperative"));
	N.Schedule.Current = ElysiumScheduleId::None;

	N.EffectsWord = 0x40u;
	N.MoveHintNode = Hint;
	N.UpdateConditionCanSpecialMove();
	TestEqual(TEXT("0x103cc6af m_fEffects & 0x40 does nothing"), N.MoveHintNode, Hint);

	N.EffectsWord = 0u;
	N.WerewolfHintFlags = 0x100u;   // gate A: 0x3aa6 is imperative
	N.Cognition.Conditions.Clear(Cond(0x78));
	N.UpdateConditionCanSpecialMove();
	TestTrue(TEXT("0x103cc706 an imperative move hint sets 0x78"), N.Cognition.Conditions.Has(Cond(0x78)));

	N.WerewolfHintFlags = 0u;
	N.WerewolfHasPathAnswers = { false };
	N.Cognition.Conditions.Clear(Cond(0x59));
	N.UpdateConditionCanSpecialMove();
	TestEqual(TEXT("0x103cc797 no path from its target: the hint is cleared"), N.MoveHintNode,
		static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("0x103cc7e6 and without 0x59 nothing is searched (0x78 cleared by ClearMoveHint)"),
		N.Cognition.Conditions.Has(Cond(0x78)));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
