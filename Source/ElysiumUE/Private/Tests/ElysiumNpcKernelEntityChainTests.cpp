#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **EntityChain**. The assertions come from the decompiled C of the 57 rows —
// the two recovered `.rdata` constants (80.0 and 1024.0), the three solid numbers slot 164 tests,
// slot 226's movetype switch and its call ORDER, slot 285's two bodies, slot 287's stop-at-first,
// slot 279's normalise-and-drop, the `(-180, 180]` single-pass wrap, the camera crossfade's three
// arms and its reset-on-read, the closest-NPC ladder's seven rungs, the response record's
// no-reschedule upgrade, the alert pair's asymmetry, and `CineIsTimeToStart`'s AND.
//
// Where a body can only answer "nothing" because its input is a seam — the physics object, the
// studio header, the trace, the game rules, the id spaces, the six unrecovered `.rdata` words — the
// case says so: that the seam is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelEntityChainFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC and the player, quiet, so nothing's own think competes with the pass a case drives.
	struct FEntityChainFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;

		FEntityChainFixture()
			: World([]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("sp_entitychain"), 29103);
				Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
				Builder.AddNpc(TEXT("other"), FVector(300.f, 0.f, 0.f));
				return Builder;
			}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
		}
	};
}

// -------------------------------------------------------------------------------------------------
// The two recovered constants, and the small fixed-answer slots.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainConstantsTest,
	"Elysium.Substrate.NpcKernelEntityChain.RecoveredConstants",
	GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainConstantsTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// 0x10026710 slot 37 — `_DAT_104454c8`. The ledger records the value as unrecovered; it is
	// 80.0, from the same word's other readers.
	TestEqual(TEXT("slot 37 answers _DAT_104454c8 = 80.0"), Npc.Slot37(), 80.f);

	// 0x101a6c20 slot 550 `CoverRadius` — `_DAT_1045d650` = 1024.0.
	TestEqual(TEXT("slot 550 CoverRadius answers _DAT_1045d650 = 1024.0"), Npc.CoverRadius(),
		1024.f);

	// 0x101a67c0 slot 476's BASE — `_DAT_104454c0`, the shared 1.0. Unity, so `CanHearSound`'s
	// `volume * sensitivity` is the bare volume.
	TestEqual(TEXT("the base HearingSensitivity is 1.0"), Npc.BaseHearingSensitivity(), 1.f);

	// 0x10026730 slot 38 — `return this;`, and the argument never reaches anything.
	TestTrue(TEXT("slot 38 answers itself"),
		Npc.Slot38(nullptr) == static_cast<FElysiumEntity*>(&Npc));
	TestTrue(TEXT("slot 38 ignores the entity it is handed"),
		Npc.Slot38(Fixture.Other) == static_cast<FElysiumEntity*>(&Npc));

	// 0x101a6420 slot 410 — an identity passthrough, NOT a fixed literal.
	const FVector Goal(12.f, 34.f, 56.f);
	TestTrue(TEXT("slot 410 hands back the pointer it was given"),
		Npc.TranslateNavGoalPosition(&Goal) == &Goal);
	TestNull(TEXT("and a null goal comes back null"), Npc.TranslateNavGoalPosition(nullptr));

	// 0x1014f8b0 slot 240 — one global word, the CPython interop side, which this runtime has none
	// of. The seam is asked and refuses.
	TestNull(TEXT("slot 240 answers the Python interop seam's nothing"), Npc.Slot240());

	// 0x10178120 — a getter of `+0x1d24`, a word NOTHING in vampire.dll writes.
	TestEqual(TEXT("FUN_10178120 answers the unwritten +0x1d24"), Npc.FUN_10178120(), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 483 and 579 — the two wrappers around slot 158, and the arguments they drop.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAliveWrappersTest,
	"Elysium.Substrate.NpcKernelEntityChain.AliveWrappers", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainAliveWrappersTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	const bool bAlive = Npc.IsAlive();
	TestTrue(TEXT("a freshly spawned guard is alive"), bAlive);

	// 0x101a6840 slot 483 `CanPlaySentence` — forwards to slot 158 and DROPS its own bool. What the
	// caller passed never reaches the callee, so both arguments answer identically.
	TestEqual(TEXT("slot 483 answers IsAlive"), Npc.CanPlaySentence(false), bAlive);
	TestEqual(TEXT("slot 483 drops its argument: true answers the same"), Npc.CanPlaySentence(true),
		Npc.CanPlaySentence(false));

	// 0x101a6ce0 slot 579 — the NEGATION of slot 158, with its int argument dropped the same way.
	TestEqual(TEXT("slot 579 is the negation of IsAlive"), Npc.Slot579(0), !bAlive);
	TestEqual(TEXT("slot 579 drops its argument too"), Npc.Slot579(7), Npc.Slot579(0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 89 — the network change-state flags.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainNetworkChangeTest,
	"Elysium.Substrate.NpcKernelEntityChain.NetworkChangeState",
	GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainNetworkChangeTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// 0x10026b70 / 0x10146790 clear bytes +1 and +2 of the `m_NetworkChangeState` record at
	// `+0x01b0` and NOTHING ELSE — byte +0 and the interval/countdown shorts at +4/+6 survive.
	Npc.NetworkChangeState.bByte0 = true;
	Npc.NetworkChangeState.bChanged = true;
	Npc.NetworkChangeState.bByte2 = true;
	Npc.NetworkChangeState.IntervalTicks = 5;
	Npc.NetworkChangeState.CountdownTicks = 3;

	Npc.Slot89();

	TestFalse(TEXT("+0x1b1 m_bChanged is cleared"), Npc.NetworkChangeState.bChanged);
	TestFalse(TEXT("+0x1b2 is cleared"), Npc.NetworkChangeState.bByte2);
	TestTrue(TEXT("+0x1b0 byte 0 is NOT touched"), Npc.NetworkChangeState.bByte0);
	TestEqual(TEXT("the interval survives"), static_cast<int32>(Npc.NetworkChangeState.IntervalTicks),
		5);
	TestEqual(TEXT("the countdown survives"),
		static_cast<int32>(Npc.NetworkChangeState.CountdownTicks), 3);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 159 / 164 / 165 — standability, and the asymmetry between the two.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainStandableTest,
	"Elysium.Substrate.NpcKernelEntityChain.Standable", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainStandableTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// SEAM: slots 92 (`GetSolid`), 94 (`GetMoveType`) and 211 (`GetSolidFlags`) are 29c's generated
	// stubs and answer 0 — `SOLID_NONE`, no solid flags. So the recovered answer for a port NPC is:
	//   slot 164 `IsStandable` : no NOT_SOLID bit, and solid 0 is none of 1/6/2, so the helper runs,
	//                            and the helper's own two arms both miss -> false.
	//   0x100b5110             : false, for the same reason.
	//   slot 159 `ReflectGauss`: false, because the FIRST term already refuses.
	TestFalse(TEXT("the standability helper refuses a SOLID_NONE entity"), Npc.IsStandableSolid());
	TestFalse(TEXT("slot 164 IsStandable refuses it too"), Npc.IsStandable());

	// Slot 159's SECOND term is `m_takedamage == 0` — family Damage's `TakeDamageMode`, seeded 2
	// (`DAMAGE_YES`) for a live NPC. Both terms refuse here, and setting the second alone does not
	// rescue it, which is what makes the conjunction assertable against a one-term reading.
	TestFalse(TEXT("slot 159 refuses while the solid term refuses"), Npc.ReflectGauss());
	Npc.TakeDamageMode = 0;
	TestFalse(TEXT("slot 159 still refuses with m_takedamage 0: the solid term is independent"),
		Npc.ReflectGauss());
	Npc.TakeDamageMode = 2;

	// Slot 165 dispatches slot 166 on BOTH arms — a null edict is `CanStandOn(nullptr)`, not a
	// refusal. The edict seam answers null, so both calls reach slot 166 with the same argument and
	// must therefore answer the same thing.
	TestEqual(TEXT("slot 165 with a null edict is CanStandOn(nullptr)"),
		Npc.CanStandOn(static_cast<void*>(nullptr)),
		Npc.CanStandOn(static_cast<FElysiumEntity*>(nullptr)));
	int32 Dummy = 0;
	TestEqual(TEXT("and so is slot 165 with an edict the seam cannot resolve"),
		Npc.CanStandOn(static_cast<void*>(&Dummy)),
		Npc.CanStandOn(static_cast<FElysiumEntity*>(nullptr)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 226 — the movetype switch and its call order.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainVPhysicsUpdateTest,
	"Elysium.Substrate.NpcKernelEntityChain.VPhysicsUpdate", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainVPhysicsUpdateTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// SEAM: slot 94 `GetMoveType` is 29c's stub and answers 0, which is NONE of the three movetypes
	// slot 226 acts on — so the recovered answer is that the body does nothing at all. That is the
	// arm retail takes for every movetype outside {1, 7, 8}, and it is asserted rather than worked
	// around.
	int32 PhysicsObject = 0;
	Npc.PhysicsUpdateCalls.Reset();
	Npc.VPhysicsUpdate(&PhysicsObject);
	TestEqual(TEXT("movetype 0 makes slot 226 call nothing"), Npc.PhysicsUpdateCalls.Num(), 0);

	// The two arms' ORDER is the deliverable, so it is asserted through the seam directly: the
	// pusher arm is one call, and the physics-read arm ends on PhysicsTouchTriggers then
	// PhysicsRelinkChildren, in that order.
	Npc.PhysicsUpdateCalls.Reset();
	Npc.VPhysicsUpdatePusher(&PhysicsObject);
	TestEqual(TEXT("the pusher arm is one call"), Npc.PhysicsUpdateCalls.Num(), 1);
	TestEqual(TEXT("and it is VPhysicsUpdatePusher"), Npc.PhysicsUpdateCalls[0],
		FString(TEXT("VPhysicsUpdatePusher")));

	// The physics-object transform seam refuses, which is why the write half of the movetype-7 arm
	// cannot run; the two trailing calls still must.
	FVector SeamOrigin(1.f, 2.f, 3.f);
	FRotator SeamAngles(4.f, 5.f, 6.f);
	TestFalse(TEXT("the IPhysicsObject::GetPosition seam refuses"),
		Npc.PhysicsObjectPosition(&PhysicsObject, SeamOrigin, SeamAngles));
	TestEqual(TEXT("and it zeroes what it was going to write"), SeamOrigin, FVector::ZeroVector);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 135 — the move-rebound easing.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainMoveReboundTest,
	"Elysium.Substrate.NpcKernelEntityChain.MoveRebound", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainMoveReboundTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// The formula, on its own: `(t*t + 1)*t - (t/D)*(D*D + 1)*t`. Zero at both ends of the span,
	// non-zero between them — a rebound that leaves and returns.
	const float D = 2.f;
	TestEqual(TEXT("the blend is zero at t = 0"), FElysiumNpc::MoveReboundBlend(0.f, D), 0.f);
	TestEqual(TEXT("the blend is zero at t = D"), FElysiumNpc::MoveReboundBlend(D, D), 0.f);
	// t = 1, D = 2: (1 + 1)*1 - (0.5)*(4 + 1)*1 = 2 - 2.5 = -0.5.
	TestEqual(TEXT("the blend at the midpoint is the cubic minus the linear"),
		FElysiumNpc::MoveReboundBlend(1.f, D), -0.5f);

	// The body ALWAYS answers its own interval, on every path including the refusal.
	TestEqual(TEXT("slot 135 answers the interval it was given"), Npc.Slot135(0.25f), 0.25f);
	TestEqual(TEXT("even for a zero interval, which the first gate refuses"), Npc.Slot135(0.f), 0.f);

	// SEAM: none of the seven `CBaseEntity` mover words has a port member, so the state answers the
	// resting one and the first gate refuses — the velocities are not touched.
	FElysiumNpc::FMoveRebound Rebound;
	TestFalse(TEXT("the mover-word seam refuses"), Npc.MoveReboundState(Rebound));
	Npc.Velocity = FVector(9.f, 9.f, 9.f);
	Npc.Slot135(0.25f);
	TestEqual(TEXT("and a refused rebound writes no velocity"), Npc.Velocity, FVector(9.f, 9.f, 9.f));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 266 / 268 / 271 / 279 — the animating tables.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAnimTablesTest,
	"Elysium.Substrate.NpcKernelEntityChain.AnimTables", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainAnimTablesTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// --- slot 268 `SetLayer` (0x10099020) ---
	// Eleven writes and one conditional pair, over family Anim's `AnimOverlay`. `m_fFlags` (+0x00)
	// is deliberately NOT written.
	Npc.AnimOverlay[1].Flags = 0x77;
	Npc.SetLayer(1, /*Activity*/ 0x42, /*Sequence*/ 9, /*bAutoKill*/ true);
	const FElysiumNpc::FAnimOverlayLayer& Layer = Npc.AnimOverlay[1];
	TestEqual(TEXT("SetLayer stores the owner activity"), Layer.Activity, 0x42);
	TestEqual(TEXT("SetLayer stores the sequence"), Layer.Sequence, 9);
	TestEqual(TEXT("SetLayer seeds the weight to 0.1"), Layer.Weight, ElysiumOverlay::SeedWeight);
	TestEqual(TEXT("SetLayer sets the weight ceiling to 1.0"), Layer.WeightMax,
		ElysiumOverlay::WeightMax);
	TestEqual(TEXT("SetLayer sets the playback rate to 1.0"), Layer.PlaybackRate, 1.f);
	TestEqual(TEXT("SetLayer zeroes the cycle"), Layer.Cycle, 0.f);
	TestEqual(TEXT("SetLayer blends in over 0.2"), Layer.BlendIn,
		ElysiumOverlay::DefaultBlendFraction);
	TestEqual(TEXT("SetLayer blends out over 0.2"), Layer.BlendOut,
		ElysiumOverlay::DefaultBlendFraction);
	TestTrue(TEXT("SetLayer stores the auto-kill flag"), Layer.bAutoKillWhenFinished);
	TestEqual(TEXT("SetLayer zeroes the finished marker"), Layer.SequenceFinished, 0);
	TestEqual(TEXT("SetLayer zeroes the last event check"), Layer.LastEventCheck, 0.f);
	TestEqual(TEXT("SetLayer does NOT write m_fFlags"), Layer.Flags, 0x77);

	// --- slot 271 `FindLayerByOwner` (0x100994c0) ---
	// Three terms: live weight, owner not ACT_INVALID, owner matches. -1 on a miss, which is the
	// convention every caller tests against — and the opposite of the stub this replaced.
	TestEqual(TEXT("slot 271 finds the layer it just armed"), Npc.FindLayerByOwner(0x42), 1);
	TestEqual(TEXT("slot 271 answers -1 for an activity no layer owns"),
		Npc.FindLayerByOwner(0x43), INDEX_NONE);
	Npc.AnimOverlay[1].Weight = 0.f;
	TestEqual(TEXT("a zero weight is what frees a slot, so the lookup misses"),
		Npc.FindLayerByOwner(0x42), INDEX_NONE);
	Npc.AnimOverlay[1].Weight = ElysiumOverlay::SeedWeight;
	Npc.AnimOverlay[1].Activity = -1;
	TestEqual(TEXT("ACT_INVALID never matches, even asked for by name"),
		Npc.FindLayerByOwner(-1), INDEX_NONE);
	// The scan STARTS at `GetFirstGestureLayer()`, which is 0 for every class in the hierarchy.
	TestEqual(TEXT("the scan starts at slot 0"), Npc.FirstGestureLayerOrRefusal(), 0);

	// --- slot 266 (0x100997f0) ---
	// Three records, two writes each: the sequence to -1 and the expire time to curtime - 1.0. The
	// latch, the fades and the pose parameter SURVIVE.
	for (int32 Index = 0; Index < FElysiumNpc::NumFlinchRecords; ++Index)
	{
		Npc.Flinch[Index].Sequence = 40 + Index;
		Npc.Flinch[Index].Latch = 2;
		Npc.Flinch[Index].FadeIn = 0.25f;
		Npc.Flinch[Index].PoseParamIndex = 0x18;
		Npc.Flinch[Index].ExpireTime = 99.f;
	}
	Npc.Slot266();
	const float Now = static_cast<float>(Fixture.World.World.NowSeconds());
	for (int32 Index = 0; Index < FElysiumNpc::NumFlinchRecords; ++Index)
	{
		TestEqual(TEXT("the flinch sequence is cleared to -1"), Npc.Flinch[Index].Sequence, -1);
		TestEqual(TEXT("the expire stamp is one second in the PAST"), Npc.Flinch[Index].ExpireTime,
			Now - 1.f);
		TestEqual(TEXT("the latch survives"), Npc.Flinch[Index].Latch, 2);
		TestEqual(TEXT("the fade survives"), Npc.Flinch[Index].FadeIn, 0.25f);
		TestEqual(TEXT("the pose parameter survives"), Npc.Flinch[Index].PoseParamIndex, 0x18);
	}

	// --- slot 279 `SetFlexWeight(int, float)` (0x100b5ba0) ---
	// SEAM: `GetNumFlexControllers` answers 0 with no studio header, so EVERY index is out of range
	// and the write is dropped. That is the recovered refusal.
	TestEqual(TEXT("the flex-controller seam answers an empty table"), Npc.NumFlexControllers(), 0);
	Npc.FlexWeight[3] = 0.5f;
	Npc.SetFlexWeight(3, 0.9f);
	TestEqual(TEXT("slot 279 drops the write when the table is empty"), Npc.FlexWeight[3], 0.5f);
	Npc.SetFlexWeight(-1, 0.9f);
	TestEqual(TEXT("a negative index is refused first"), Npc.FlexWeight[3], 0.5f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 285 / 287 — the scene-event queue, and the difference between them.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainSceneEventsTest,
	"Elysium.Substrate.NpcKernelEntityChain.SceneEvents", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainSceneEventsTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// Four records over two scenes and three events, one of which appears TWICE — which is what
	// makes slot 287's stop-at-first observable.
	int32 SceneA = 0;
	int32 SceneB = 0;
	int32 EventA = 0;
	int32 EventB = 0;
	auto Seed = [&Npc, &SceneA, &SceneB, &EventA, &EventB]()
	{
		Npc.SceneEvents.Reset();
		Npc.SceneEventReleases = 0;
		auto Add = [&Npc](void* Scene, void* Event)
		{
			FElysiumNpc::FSceneEventRecord Record;
			Record.Scene = static_cast<const FElysiumSceneData*>(Scene);
			Record.Event = static_cast<const FElysiumSceneEvent*>(Event);
			Npc.SceneEvents.Add(Record);
		};
		Add(&SceneA, &EventA);
		Add(&SceneB, &EventB);
		Add(&SceneA, &EventB);   // the duplicate event, on the other scene
		Add(&SceneB, &EventA);
	};

	// Slot 285 with a scene: EVERY record of that scene goes, in one pass — the cursor does not
	// advance over a compacted-in record, which is what makes two non-adjacent matches both die.
	Seed();
	Npc.ClearSceneEvents(&SceneA);
	TestEqual(TEXT("slot 285 removes every record of the named scene"), Npc.SceneEvents.Num(), 2);
	TestEqual(TEXT("and releases each one it removed"), Npc.SceneEventReleases, 2);
	for (const FElysiumNpc::FSceneEventRecord& Record : Npc.SceneEvents)
	{
		TestTrue(TEXT("no record of scene A survives"),
			static_cast<const void*>(Record.Scene) != static_cast<const void*>(&SceneA));
	}

	// Slot 285 with NULL is a different body entirely: retail sets the COUNT to zero and does not
	// release, zero or compact anything.
	Seed();
	Npc.ClearSceneEvents(nullptr);
	TestEqual(TEXT("slot 285 with null clears the whole queue"), Npc.SceneEvents.Num(), 0);
	TestEqual(TEXT("and releases NOTHING — it only stops counting"), Npc.SceneEventReleases, 0);

	// Slot 287 keys on the EVENT and stops at the FIRST match, so the duplicate survives.
	Seed();
	Npc.RemoveSceneEvent(&EventB);
	TestEqual(TEXT("slot 287 removes exactly one record"), Npc.SceneEvents.Num(), 3);
	TestEqual(TEXT("and releases exactly one"), Npc.SceneEventReleases, 1);
	int32 RemainingB = 0;
	for (const FElysiumNpc::FSceneEventRecord& Record : Npc.SceneEvents)
	{
		if (static_cast<const void*>(Record.Event) == static_cast<const void*>(&EventB))
		{
			++RemainingB;
		}
	}
	TestEqual(TEXT("the second record carrying the same event SURVIVES"), RemainingB, 1);

	// An event the queue does not hold is a silent no-op.
	Seed();
	int32 Stranger = 0;
	Npc.RemoveSceneEvent(&Stranger);
	TestEqual(TEXT("slot 287 with an unknown event changes nothing"), Npc.SceneEvents.Num(), 4);
	TestEqual(TEXT("and releases nothing"), Npc.SceneEventReleases, 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 447 / 450 / 580 — the id spaces.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainIdSpaceTest,
	"Elysium.Substrate.NpcKernelEntityChain.IdSpaces", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainIdSpaceTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// Slot 580 is this family's row (`0x101aa790`) and family Schedule's walk is its body: the
	// nearest species override of slot 580, else the Troika line's own. It is never null.
	TestNotNull(TEXT("slot 580 answers an id space"), Npc.GetClassScheduleIdSpace());
	TestTrue(TEXT("and it IS family Schedule's walk, not a second table"),
		Npc.GetClassScheduleIdSpace()
			== static_cast<void*>(const_cast<FElysiumNpc::FScheduleIdSpace*>(
				Npc.ClassScheduleIdSpace())));

	// 0x101a6d00 is slot 580's BASE body and answers a DIFFERENT global from the Troika line's.
	const FElysiumNpc::FScheduleIdSpace* Base = Npc.BaseClassScheduleIdSpace();
	if (TestNotNull(TEXT("the base row exists"), Base))
	{
		TestEqual(TEXT("the base row names CAI_BaseNPC"), FString(Base->RetailClass),
			FString(TEXT("CAI_BaseNPC")));
		TestEqual(TEXT("and 0x101a6d00 as its body"), FString(Base->Body),
			FString(TEXT("0x101a6d00")));
		TestEqual(TEXT("and DAT_1090ff08, not the Troika line's DAT_10924248"),
			FString(Base->IdSpace), FString(TEXT("0x1090ff08")));
		TestEqual(TEXT("its local range is the 9999 empty sentinel"), Base->LocalBase, 9999);
	}

	// `0x102ea280`: -1 stays -1, a null space is the end of the chain, and the empty sentinel never
	// matches. SEAM: every row in this runtime carries that sentinel, so both slots answer -1.
	TestEqual(TEXT("the translation refuses -1 outright"),
		FElysiumNpc::GlobalToLocalId(Base, INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("a null space is the end of the chain"),
		FElysiumNpc::GlobalToLocalId(nullptr, 5), INDEX_NONE);
	TestEqual(TEXT("the 9999 sentinel never matches"), FElysiumNpc::GlobalToLocalId(Base, 5),
		INDEX_NONE);

	// A hand-built row proves the arithmetic the sentinel hides: `(localBase - globalBase) + id`
	// over the inclusive range `[globalBase, localTop]`.
	FElysiumNpc::FScheduleIdSpace Row;
	Row.GlobalBase = 100;
	Row.LocalBase = 10;
	Row.LocalTop = 120;
	TestEqual(TEXT("in range, the id is rebased"), FElysiumNpc::GlobalToLocalId(&Row, 105), 15);
	TestEqual(TEXT("the low bound is inclusive"), FElysiumNpc::GlobalToLocalId(&Row, 100), 10);
	TestEqual(TEXT("the high bound is inclusive"), FElysiumNpc::GlobalToLocalId(&Row, 120), 30);
	TestEqual(TEXT("below the range answers -1"), FElysiumNpc::GlobalToLocalId(&Row, 99),
		INDEX_NONE);
	TestEqual(TEXT("above the range answers -1"), FElysiumNpc::GlobalToLocalId(&Row, 121),
		INDEX_NONE);

	// Slots 447 and 450 both forward into it; 450 over the TASK sub-space (+0x18), which no row in
	// this runtime carries, so it answers -1 for every id including one the schedule space holds.
	TestEqual(TEXT("slot 447 answers -1 through the empty schedule space"),
		Npc.GetLocalScheduleId(5), INDEX_NONE);
	TestEqual(TEXT("slot 450 answers -1: no row carries the task sub-space"),
		Npc.GetLocalTaskId(5), INDEX_NONE);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The `CPayphone` trio.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainPayphoneTest,
	"Elysium.Substrate.NpcKernelEntityChain.Payphone", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainPayphoneTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// `npc_payphone` IS a registered spawn leaf (`Substrate/ElysiumNpcClasses.cpp`), and `CPayphone`
	// IS a census class — so both tables are checked here rather than assumed.
	const FElysiumNpcClass* Payphone = ElysiumNpcKernelClass::Find(TEXT("CPayphone"));
	if (TestNotNull(TEXT("CPayphone is a census class"), Payphone))
	{
		TestEqual(TEXT("CPayphone fills slot 286 with 0x101aad90"),
			FString(ElysiumNpcKernelClass::BodyOf(Payphone, 286)), FString(TEXT("0x101aad90")));
		TestEqual(TEXT("CPayphone fills slot 612 with 0x101aadb0"),
			FString(ElysiumNpcKernelClass::BodyOf(Payphone, 612)), FString(TEXT("0x101aadb0")));
		TestEqual(TEXT("CPayphone fills slot 35 with 0x101aa950"),
			FString(ElysiumNpcKernelClass::BodyOf(Payphone, 35)), FString(TEXT("0x101aa950")));
	}

	// 0x101aadb0 — the two speech sound flags, selected by `bDialogQueIsFinal` (+0x654c).
	Npc.Dialogue.bDialogQueIsFinal = true;
	TestEqual(TEXT("a payphone's FINAL line carries 0xa80"), Npc.PayphoneSpeechSoundFlags(), 0xa80);
	Npc.Dialogue.bDialogQueIsFinal = false;
	TestEqual(TEXT("and every other line 0xe80"), Npc.PayphoneSpeechSoundFlags(), 0xe80);

	// 0x101aad90 — an EMPTY body. It touches the scene-event queue that
	// `CBaseFlex::AddSceneEvent` would have appended to, and that is the whole observable.
	Npc.SceneEvents.Reset();
	int32 Scene = 0;
	int32 Event = 0;
	Npc.PayphoneAddSceneEvent(&Scene, &Event);
	TestEqual(TEXT("a payphone swallows the scene event instead of queueing it"),
		Npc.SceneEvents.Num(), 0);

	// 0x101aa950 — `CanTalk(other) ? 0x2f : 0`. SEAM: slot 295 `CanTalk` is story 29d's stub and
	// answers false, so the recovered answer today is 0 — and the test says WHICH term refused
	// rather than only that the number is 0.
	TestFalse(TEXT("slot 295 CanTalk is 29d's stub and refuses"), Npc.CanTalk(Fixture.Other));
	TestEqual(TEXT("so the payphone publishes no caps"), Npc.PayphoneUseCaps(Fixture.Other), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CDialog` and `CGlobalEntityList`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainDialogAndListTest,
	"Elysium.Substrate.NpcKernelEntityChain.DialogAndList", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainDialogAndListTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard) || Fixture.Other == nullptr)
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// --- 0x100e4ef0 ---
	// Run the buffer if it is non-empty, then clear it UNCONDITIONALLY — the clear is outside the
	// `if`, which is what makes an empty flush still a wipe.
	Npc.DialogEventScriptCalls.Reset();
	Npc.PendingDialogEventScript = TEXT("g_scripts.OnBeat()");
	Npc.CallPendingDialogEventScript();
	TestEqual(TEXT("a non-empty buffer is run"), Npc.DialogEventScriptCalls.Num(), 1);
	TestEqual(TEXT("with its own text"), Npc.DialogEventScriptCalls[0],
		FString(TEXT("g_scripts.OnBeat()")));
	TestTrue(TEXT("and the buffer is zero-filled"), Npc.PendingDialogEventScript.IsEmpty());

	Npc.DialogEventScriptCalls.Reset();
	Npc.PendingDialogEventScript.Empty();
	Npc.CallPendingDialogEventScript();
	TestEqual(TEXT("an empty buffer runs nothing"), Npc.DialogEventScriptCalls.Num(), 0);
	TestTrue(TEXT("but is still wiped"), Npc.PendingDialogEventScript.IsEmpty());

	// --- 0x100e49b0 ---
	// Four gates then a type switch; only types 5 and 6 reach the bone lookup.
	Npc.DialogPcLines.Reset();
	FVector Head = FVector(7.f, 7.f, 7.f);
	TestFalse(TEXT("an out-of-range line index refuses"), Npc.DialogLineHeadPosition(0, Head));
	TestFalse(TEXT("a negative line index refuses"), Npc.DialogLineHeadPosition(-1, Head));

	Npc.DialogPcLines.SetNum(3);
	Npc.DialogPcLines[0] = { /*Flags*/ 1, /*Type*/ 5 };
	Npc.DialogPcLines[1] = { /*Flags*/ 0, /*Type*/ 5 };   // flag bit 0 clear
	Npc.DialogPcLines[2] = { /*Flags*/ 1, /*Type*/ 4 };   // a type neither arm claims
	TestFalse(TEXT("an unbound speaker handle refuses before the flag is even read"),
		Npc.DialogLineHeadPosition(0, Head));

	Npc.DialogSpeaker = Fixture.Other->Handle;
	TestFalse(TEXT("the flag-clear line refuses"), Npc.DialogLineHeadPosition(1, Head));
	TestFalse(TEXT("type 4 refuses: only 5 and 6 resolve a head"),
		Npc.DialogLineHeadPosition(2, Head));
	// SEAM: the bone table. Type 5 reaches it and the seam refuses, which is the recovered answer.
	TestFalse(TEXT("type 5 reaches the bone seam, which refuses"),
		Npc.DialogLineHeadPosition(0, Head));
	Npc.DialogPcLines[0].Type = 6;
	TestFalse(TEXT("type 6 takes the byte-identical arm and refuses the same way"),
		Npc.DialogLineHeadPosition(0, Head));
	TestEqual(TEXT("and the out-parameter is zeroed on every refusal"), Head, FVector::ZeroVector);

	// --- 0x100f6d80 / 0x100f6e40 ---
	// Dedupe-append and search-compact over `CGlobalEntityList`'s listener vector.
	int32 ListenerA = 0;
	int32 ListenerB = 0;
	Npc.EntityListeners = FElysiumNpc::FEntityListenerVector();
	Npc.AddListenerEntity(&ListenerA);
	Npc.AddListenerEntity(&ListenerB);
	TestEqual(TEXT("two listeners register"), Npc.EntityListeners.Listeners.Num(), 2);
	TestTrue(TEXT("and the insert is always at the END"),
		Npc.EntityListeners.Listeners[1] == static_cast<void*>(&ListenerB));
	Npc.AddListenerEntity(&ListenerA);
	TestEqual(TEXT("a duplicate is refused by the dedupe scan"),
		Npc.EntityListeners.Listeners.Num(), 2);
	TestTrue(TEXT("the capacity word tracks the growth"), Npc.EntityListeners.Allocated >= 2);

	Npc.RemoveListenerEntity(&ListenerA);
	TestEqual(TEXT("removal compacts"), Npc.EntityListeners.Listeners.Num(), 1);
	TestTrue(TEXT("leaving the survivor at index 0"),
		Npc.EntityListeners.Listeners[0] == static_cast<void*>(&ListenerB));
	int32 Stranger = 0;
	Npc.RemoveListenerEntity(&Stranger);
	TestEqual(TEXT("removing a value the list never held is a silent no-op"),
		Npc.EntityListeners.Listeners.Num(), 1);
	TestTrue(TEXT("and the capacity is NOT reduced"), Npc.EntityListeners.Allocated >= 2);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The `CCineNPC` trio.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainCineTest,
	"Elysium.Substrate.NpcKernelEntityChain.Cine", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainCineTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard) || Fixture.Other == nullptr)
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// 0x101a7540 — `m_iDelay < 1 && m_startTime <= curtime`, an **AND** where the SDK's own
	// `IsTimeToStart` is an OR. SEAM: both words answer the resting (0, 0) pair, where both terms
	// hold — a beat with no authored delay is ready, which is retail's answer for that pair too.
	int32 Delay = 7;
	float StartTime = 7.f;
	Npc.CineDelayState(Delay, StartTime);
	TestEqual(TEXT("the CCineNPC delay seam answers 0"), Delay, 0);
	TestEqual(TEXT("and the start time 0"), StartTime, 0.f);
	TestTrue(TEXT("so IsTimeToStart holds — both terms, not either"), Npc.CineIsTimeToStart());

	// 0x101a8930 — `m_interruptable && m_hTargetEnt->IsAlive()`. SEAM: `m_interruptable` has no
	// port member and answers false, so the FIRST term refuses; the test says which.
	TestFalse(TEXT("the m_interruptable seam answers false"), Npc.CineIsInterruptable());
	Npc.SetTarget(Fixture.Other->Handle);
	TestTrue(TEXT("the target IS live and alive, so the second term would hold"),
		Fixture.Other->IsAlive());
	TestFalse(TEXT("but CanInterrupt still refuses on the first term"), Npc.CineCanInterrupt());
	// And a missing target is FALSE, not true — an interruptable beat with no actor is already over.
	Npc.SetTarget(FElysiumEntityHandle::Invalid());
	TestFalse(TEXT("an unset target answers false"), Npc.CineCanInterrupt());

	// 0x101a8840 `FixScriptNPCSchedule` — ideal state 7 (DEAD) is left alone; anything else becomes
	// 1 (IDLE) with the selector-trace line 970 stamped. `ClearSchedule` runs on EVERY path.
	FElysiumNpc& Actor = *Fixture.Other;
	TestEqual(TEXT("a fresh NPC has never had a raw ideal state written"),
		Actor.GetMind().DesiredRetailState(), 0);
	Npc.FixScriptNpcSchedule(Actor);
	TestEqual(TEXT("the teardown stores NPC_STATE_IDLE"), Actor.GetMind().DesiredRetailState(), 1);

	// Now the dead arm: the state is NOT overwritten. `FElysiumNpc` publishes the mind read-only
	// (`GetMind()`), and nothing else in this runtime writes retail state 7 yet, so the case stands
	// the precondition through the same writer the body uses.
	const_cast<FElysiumNpcMind&>(Actor.GetMind()).RequestDesiredState(7, 0);
	TestEqual(TEXT("the actor is now NPC_STATE_DEAD"), Actor.GetMind().DesiredRetailState(), 7);
	Npc.FixScriptNpcSchedule(Actor);
	TestEqual(TEXT("a dead actor keeps NPC_STATE_DEAD"), Actor.GetMind().DesiredRetailState(), 7);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The `CBasePlayer` law half.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainLawTest,
	"Elysium.Substrate.NpcKernelEntityChain.Law", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainLawTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;
	FElysiumPlayer* Player = Fixture.World.Player();
	if (!TestNotNull(TEXT("the chain player stands"), Player))
	{
		return false;
	}
	TestTrue(TEXT("ChainPlayer resolves the one player this runtime stands"),
		Npc.ChainPlayer() == Player);

	// SEAM: all three act-level ConVars answer `!IsCommand()` with a NEGATIVE value, which is the
	// arm that reads the stored field. That is the shipped default and it is asserted as such.
	int32 Override = 99;
	TestFalse(TEXT("the act-level ConVar seam answers not-a-command"),
		Npc.ActLevelOverrideCvar(FElysiumNpc::ActCvarCriminal, Override));
	TestEqual(TEXT("with a negative value, meaning no override"), Override, -1);

	Player->Law.Supernatural = 3;
	Player->Law.Criminal = 4;
	Player->Law.Investigate = 2;
	Player->Law.CriminalCount = 11;
	Player->Law.SupernaturalCount = 5;

	// 0x1017dd80 / 0x1017ddd0 / 0x1017de60 — the three levels, each behind its own ConVar.
	TestEqual(TEXT("0x1017dd80 reads m_LevelSupernaturalAct"), Npc.LevelSupernaturalAct(), 3);
	TestEqual(TEXT("0x1017ddd0 reads m_LevelCriminalAct"), Npc.LevelCriminalAct(), 4);
	TestEqual(TEXT("0x1017de60 reads m_LevelInvestigateAct"), Npc.LevelInvestigateAct(), 2);

	// The obfuscation's five literals, exercised as arithmetic. It is NOT an identity, which is the
	// whole point of the word being obfuscated in retail's storage.
	TestTrue(TEXT("the criminal-level fold changes its input"),
		FElysiumNpc::ObfuscateActLevel(4u) != 4u);
	// (((4 & 0x8a66e35) ^ 0x793f90) + 0x8b10412) & 0x175991ca ^ 4 ^ 0x783682a9.
	const uint32 Expected = ((((4u & 0x08a66e35u) ^ 0x00793f90u) + 0x08b10412u) & 0x175991cau)
		^ 4u ^ 0x783682a9u;
	TestTrue(TEXT("and it is exactly retail's five literals"),
		FElysiumNpc::ObfuscateActLevel(4u) == Expected);

	// 0x1017e720 / 0x1017e740 — the two monotonic counts, already carried by the port's player.
	TestEqual(TEXT("0x1017e720 is FElysiumPlayer::CriminalActCount"), Npc.CriminalActCount(), 11);
	TestEqual(TEXT("0x1017e740 is FElysiumPlayer::SupernaturalActCount"),
		Npc.SupernaturalActCount(), 5);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The police response, the pursuit count and the heightened alert.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainPoliceTest,
	"Elysium.Substrate.NpcKernelEntityChain.Police", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainPoliceTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard) || Fixture.Other == nullptr)
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;
	FElysiumPlayer* Player = Fixture.World.Player();
	if (!TestNotNull(TEXT("the chain player stands"), Player))
	{
		return false;
	}

	// --- 0x1017ed00 ---
	// The hunter gate first: `m_iHuntersInPursuitCount >= 1` refuses outright.
	Player->Police.HuntersInPursuit = 1;
	Npc.SpawnResponseCopsTimer = FLT_MAX;
	Npc.SetSpawnResponseCops(3, Fixture.Other, FVector(10.f, 20.f, 30.f));
	TestEqual(TEXT("a hunter on the street refuses the response outright"),
		Npc.SpawnResponseCopsTimer, FLT_MAX);
	Player->Police.HuntersInPursuit = 0;

	// The FLT_MAX sentinel arm: level, handle, location and a fresh deadline.
	Npc.SetSpawnResponseCops(3, Fixture.Other, FVector(10.f, 20.f, 30.f));
	TestEqual(TEXT("the level is stored"), Npc.SpawnResponseCopsLevel, 3);
	TestEqual(TEXT("the source NPC's handle is stored"), Npc.SpawnResponseCopsNpc.Index,
		Fixture.Other->Handle.Index);
	TestEqual(TEXT("the location is stored"), Npc.SpawnResponseCopsLocation,
		FVector(10.f, 20.f, 30.f));
	TestTrue(TEXT("and the FLT_MAX sentinel is gone"), Npc.SpawnResponseCopsTimer != FLT_MAX);
	const float ArmedAt = Npc.SpawnResponseCopsTimer;
	// SEAM: both delay ConVars answer `IsCommand`, so the draw is over an empty range and the
	// deadline is curtime exactly.
	float Low = 9.f;
	float High = 9.f;
	TestFalse(TEXT("the response-delay ConVar seam refuses"),
		Npc.SpawnResponseCopsDelayCvars(Low, High));
	TestEqual(TEXT("with a zero low bound"), Low, 0.f);
	TestEqual(TEXT("and a zero high bound"), High, 0.f);

	// A LOWER level is ignored entirely.
	Npc.SetSpawnResponseCops(2, nullptr, FVector(1.f, 1.f, 1.f));
	TestEqual(TEXT("a lower level does not displace the armed one"), Npc.SpawnResponseCopsLevel, 3);
	TestEqual(TEXT("nor its location"), Npc.SpawnResponseCopsLocation, FVector(10.f, 20.f, 30.f));

	// An EQUAL level is also ignored — retail's test is strictly greater.
	Npc.SetSpawnResponseCops(3, nullptr, FVector(2.f, 2.f, 2.f));
	TestEqual(TEXT("an equal level is refused too: the test is strict"),
		Npc.SpawnResponseCopsLocation, FVector(10.f, 20.f, 30.f));

	// A HIGHER level upgrades the record but does NOT reschedule the timer. This is the fact that
	// makes a burst of incidents one response landing at the first one's deadline.
	Npc.SetSpawnResponseCops(5, nullptr, FVector(4.f, 5.f, 6.f));
	TestEqual(TEXT("a higher level upgrades the record"), Npc.SpawnResponseCopsLevel, 5);
	TestEqual(TEXT("and its location"), Npc.SpawnResponseCopsLocation, FVector(4.f, 5.f, 6.f));
	TestFalse(TEXT("a null source clears the handle to invalid"), Npc.SpawnResponseCopsNpc.IsSet());
	TestEqual(TEXT("but the TIMER is NOT rescheduled"), Npc.SpawnResponseCopsTimer, ArmedAt);
	TestTrue(TEXT("and the port's own police record mirrors it"), Player->Police.bResponsePending);
	TestEqual(TEXT("at the same severity"), Player->Police.ResponseSeverity, 5);

	// --- 0x1017f9c0 / 0x1017f980 / 0x1017f8d0: the alert trio and its asymmetry ---
	Npc.UnrecoveredChainCalls.Reset();
	Player->Police.bHeightenedAlert = false;
	Player->Police.HeightenedAlertExpiry = 0.0;
	Npc.BeginHeightenedAlert();
	TestTrue(TEXT("arming raises m_bInHeightenedAlert"), Player->Police.bHeightenedAlert);
	// SEAM: the duration ConVar answers `IsCommand`, so retail's own 0.0 arm makes the alert expire
	// the instant it is armed. That is the recovered refusal, not a chosen duration.
	float Duration = 9.f;
	TestFalse(TEXT("the alert-duration ConVar seam refuses"),
		Npc.HeightenedAlertDurationCvar(Duration));
	TestEqual(TEXT("with a zero duration"), Duration, 0.f);
	TestFalse(TEXT("so the alert is already inactive on the frame it is armed"),
		Npc.IsHeightenedAlertActive());
	// The two unrecovered calls the arm makes, in order.
	TestTrue(TEXT("the begin arm fires the +0x498 output through the unrecovered singleton"),
		Npc.UnrecoveredChainCalls.Contains(TEXT("0x1023dcd0+0x498 FireOutput")));
	TestTrue(TEXT("and calls the unrecovered 0x1017f900"),
		Npc.UnrecoveredChainCalls.Contains(TEXT("0x1017f900")));

	// Make it genuinely active, then end it — and observe that ending does NOT clear the flag.
	Player->Police.HeightenedAlertExpiry = Fixture.World.World.NowSeconds() + 60.0;
	TestTrue(TEXT("a future expiry makes the alert active"), Npc.IsHeightenedAlertActive());
	Npc.UnrecoveredChainCalls.Reset();
	Npc.EndHeightenedAlert();
	TestFalse(TEXT("ending zeroes the timer, so the predicate answers false"),
		Npc.IsHeightenedAlertActive());
	TestTrue(TEXT("but m_bInHeightenedAlert is deliberately LEFT SET — retail's own asymmetry"),
		Player->Police.bHeightenedAlert);
	TestTrue(TEXT("and the end arm fires the +0x480 output"),
		Npc.UnrecoveredChainCalls.Contains(TEXT("0x1023dcd0+0x480 FireOutput")));

	// --- 0x1017f6e0 ---
	// The decrement is unconditional and the zero test is `== 0`, not `<= 0`.
	Player->Police.CopsInPursuit = 2;
	Player->Police.HeightenedAlertExpiry = 0.0;
	Npc.UnrecoveredChainCalls.Reset();
	Npc.RemoveCopInPursuit();
	TestEqual(TEXT("one cop leaves"), Player->Police.CopsInPursuit, 1);
	TestEqual(TEXT("and nothing else fires yet"), Npc.UnrecoveredChainCalls.Num(), 0);
	Npc.RemoveCopInPursuit();
	TestEqual(TEXT("the last cop leaves"), Player->Police.CopsInPursuit, 0);
	TestTrue(TEXT("which runs the unrecovered 0x10370630"),
		Npc.UnrecoveredChainCalls.Contains(TEXT("0x10370630")));
	TestTrue(TEXT("and arms the heightened alert"), Player->Police.bHeightenedAlert);
	// Below zero: the count keeps falling and the `== 0` test never fires again.
	Npc.UnrecoveredChainCalls.Reset();
	Npc.RemoveCopInPursuit();
	TestEqual(TEXT("the count goes NEGATIVE — retail does not clamp"),
		Player->Police.CopsInPursuit, -1);
	TestFalse(TEXT("and == 0 never fires again"),
		Npc.UnrecoveredChainCalls.Contains(TEXT("0x10370630")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The scare queue — 0x1017fd60.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainScareTest,
	"Elysium.Substrate.NpcKernelEntityChain.ScaredNpc", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainScareTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard) || Fixture.Other == nullptr)
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;
	FElysiumPlayer* Player = Fixture.World.Player();
	if (!TestNotNull(TEXT("the chain player stands"), Player))
	{
		return false;
	}
	Player->ScareQueue.Reset();

	// A miss appends.
	Npc.RememberScaredNpc(2, Fixture.Other);
	if (!TestEqual(TEXT("a new NPC appends a record"), Player->ScareQueue.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("keyed by the NPC's entity index"), Player->ScareQueue[0].Npc.Index,
		Fixture.Other->Handle.Index);
	TestEqual(TEXT("at the offered severity"), Player->ScareQueue[0].Severity, 2);

	// A hit keeps the GREATER severity...
	Npc.RememberScaredNpc(1, Fixture.Other);
	TestEqual(TEXT("a repeat does not append"), Player->ScareQueue.Num(), 1);
	TestEqual(TEXT("a LOWER severity is discarded"), Player->ScareQueue[0].Severity, 2);
	Npc.RememberScaredNpc(4, Fixture.Other);
	TestEqual(TEXT("a HIGHER severity is kept"), Player->ScareQueue[0].Severity, 4);

	// ...and ALWAYS refreshes the timestamp, hit or not.
	Player->ScareQueue[0].Time = -99.0;
	Npc.RememberScaredNpc(1, Fixture.Other);
	TestTrue(TEXT("the timestamp is refreshed even on a discarded severity"),
		Player->ScareQueue[0].Time != -99.0);

	// A second NPC is a second record.
	Npc.RememberScaredNpc(3, Fixture.Guard);
	TestEqual(TEXT("a different NPC appends its own record"), Player->ScareQueue.Num(), 2);

	// A null NPC does nothing at all.
	Npc.RememberScaredNpc(9, nullptr);
	TestEqual(TEXT("a null NPC appends nothing"), Player->ScareQueue.Num(), 2);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The camera-override crossfade — 0x1017d900 and 0x1017d680.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainCameraFadeTest,
	"Elysium.Substrate.NpcKernelEntityChain.CameraFade", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainCameraFadeTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard) || Fixture.Other == nullptr)
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;
	// The gate wants a POSITIVE mark, so the clock is moved well off zero before anything is armed —
	// far enough that a mark a hundred seconds in the past is still positive, which is what the
	// past-the-end and underflow arms below need.
	Fixture.World.World.Tick(1000.0);
	const float Now = static_cast<float>(Fixture.World.World.NowSeconds());
	TestTrue(TEXT("the clock is well off zero"), Now > 500.f);

	// The gate: a positive mark AND at least one camera entity resolving. Neither alone is enough.
	Npc.CameraOverrideFadeMarkTime = 0.f;
	Npc.CameraTargetEntity = Fixture.Other->Handle;
	TestEqual(TEXT("a zero mark refuses"), Npc.CameraOverrideFadeFraction(), 0.f);
	TestFalse(TEXT("and the refusal RESETS the target handle"), Npc.CameraTargetEntity.IsSet());

	Npc.CameraOverrideFadeMarkTime = Now - 1.f;
	Npc.CameraOverrideFadeDuration = 4.f;
	TestEqual(TEXT("no camera entity refuses too"), Npc.CameraOverrideFadeFraction(), 0.f);
	TestEqual(TEXT("and the refusal zeroes the mark"), Npc.CameraOverrideFadeMarkTime, 0.f);
	TestEqual(TEXT("and the duration"), Npc.CameraOverrideFadeDuration, 0.f);

	// Duration zero answers 1.0 — a zero-length fade is already finished.
	Npc.CameraOverrideFadeMarkTime = Now - 1.f;
	Npc.CameraOverrideFadeDuration = 0.f;
	Npc.CameraViewEntity = Fixture.Other->Handle;
	TestEqual(TEXT("a zero duration answers 1.0"), Npc.CameraOverrideFadeFraction(), 1.f);
	TestTrue(TEXT("and does NOT reset — the fade is still armed"),
		Npc.CameraViewEntity.IsSet());

	// The count-UP arm: clamp((now - mark) / duration, 0, 1). The VIEW entity alone is enough.
	Npc.CameraOverrideFadeMarkTime = Now - 1.f;
	Npc.CameraOverrideFadeDuration = 4.f;
	TestEqual(TEXT("a quarter of the way through answers 0.25"),
		Npc.CameraOverrideFadeFraction(), 0.25f);
	Npc.CameraOverrideFadeMarkTime = Now - 100.f;
	TestEqual(TEXT("past the end it clamps to 1.0, and does not reset"),
		Npc.CameraOverrideFadeFraction(), 1.f);
	TestTrue(TEXT("the count-up arm never tears the fade down"), Npc.CameraViewEntity.IsSet());

	// The count-DOWN arm: a NEGATIVE duration starts at 1 and falls, and its underflow falls
	// through to the reset — unlike the count-up arm's clamp.
	Npc.CameraOverrideFadeMarkTime = Now - 1.f;
	Npc.CameraOverrideFadeDuration = -4.f;
	TestEqual(TEXT("a negative duration counts DOWN from 1.0"),
		Npc.CameraOverrideFadeFraction(), 0.75f);
	Npc.CameraOverrideFadeMarkTime = Now - 100.f;
	Npc.CameraOverrideFadeDuration = -4.f;
	Npc.CameraViewEntity = Fixture.Other->Handle;
	TestEqual(TEXT("underflowing the count-down arm answers 0"),
		Npc.CameraOverrideFadeFraction(), 0.f);
	TestFalse(TEXT("and TEARS THE FADE DOWN, unlike the count-up clamp"),
		Npc.CameraViewEntity.IsSet());

	// 0x1017d680 runs the query FIRST, which is why a finished count-down fade answers null on the
	// very call that observes it ending.
	Npc.CameraOverrideFadeMarkTime = Now - 100.f;
	Npc.CameraOverrideFadeDuration = -4.f;
	Npc.CameraTargetEntity = Fixture.Other->Handle;
	TestNull(TEXT("resolving the camera target after the fade ran out answers null"),
		Npc.ResolveCameraTargetEntity());

	// And a live fade resolves the entity for real.
	Npc.CameraOverrideFadeMarkTime = Now - 1.f;
	Npc.CameraOverrideFadeDuration = 4.f;
	Npc.CameraTargetEntity = Fixture.Other->Handle;
	TestTrue(TEXT("a live fade resolves the target entity"),
		Npc.ResolveCameraTargetEntity() == static_cast<FElysiumEntity*>(Fixture.Other));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The closest-NPC cache — 0x101828b0 and 0x10182a90.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainClosestNpcTest,
	"Elysium.Substrate.NpcKernelEntityChain.ClosestNpc", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainClosestNpcTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard) || Fixture.Other == nullptr)
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;
	FElysiumPlayer* Player = Fixture.World.Player();
	if (!TestNotNull(TEXT("the chain player stands"), Player))
	{
		return false;
	}

	// An unresolvable cache resets the triple to (invalid, 100000.0, 0) — the `0x47c34ff3`
	// immediate.
	Player->Observer.Observer = FElysiumEntityHandle::Invalid();
	Player->Observer.DistanceCm = 5.f;
	Npc.UpdateClosestNpc(Fixture.Other, 4000.f);
	TestEqual(TEXT("the reset distance is the 0x47c34ff3 immediate, 100000.0"),
		Player->Observer.DistanceCm, 100000.f);

	// The acceptance ladder. SEAM: `GetModelPtr()` answers false with no studio header, and that
	// rung REFUSES — so the recovered answer today is that no candidate is ever cached. The test
	// asserts the rung that refused rather than only the outcome.
	TestFalse(TEXT("the studio-header seam refuses the candidate"),
		Npc.HasStudioModel(*Fixture.Other));
	TestFalse(TEXT("the cache is left empty"), Player->Observer.Observer.IsSet());

	// The two unrecovered rungs both answer the ADMITTING value, so neither is what refused.
	TestFalse(TEXT("the +0x19c & 0x40 bit seam admits"),
		Npc.ClosestNpcCandidateBitSet(*Fixture.Other));
	TestFalse(TEXT("and the 0x100b5190 predicate seam admits"),
		Npc.ClosestNpcCandidateRefused(*Fixture.Other));

	// The "same entity" arm accepts ANY distance, including a LARGER one, because the nearest NPC
	// staying nearest is not a comparison. Stand the cache by hand to reach it.
	Player->Observer.Observer = Fixture.Other->Handle;
	Player->Observer.DistanceCm = 10.f;
	Npc.UpdateClosestNpc(Fixture.Other, 900.f);
	TestEqual(TEXT("the cached entity refreshes to a LARGER distance"),
		Player->Observer.DistanceCm, 900.f);

	// ...and a cached entity that has died resets the triple.
	Fixture.Other->Kill();
	Npc.UpdateClosestNpc(Fixture.Other, 5.f);
	TestFalse(TEXT("a dead cached entity is dropped"), Player->Observer.Observer.IsSet());

	// 0x10182a90 with an empty cache is 0 — the first gate.
	TestEqual(TEXT("the sense value over an empty cache is 0"), Npc.ClosestNpcSense(), 0);
	// The scalar seam is unrecovered and answers 0, which zeroes the middle arm's product.
	TestEqual(TEXT("the sense scalar seam answers 0"), Npc.ClosestNpcSenseScalar(), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Autoaim, the held use entity, the player animation and the controller detach.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAutoaimTest,
	"Elysium.Substrate.NpcKernelEntityChain.Autoaim", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainAutoaimTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// The single-pass wrap. Retail runs each test ONCE, so an angle outside (-540, 540) comes out
	// still outside — which a loop-based normalise would silently "fix".
	TestEqual(TEXT("190 wraps to -170"), FElysiumNpc::ChainWrapDegrees(190.f), -170.f);
	TestEqual(TEXT("-190 wraps to 170"), FElysiumNpc::ChainWrapDegrees(-190.f), 170.f);
	TestEqual(TEXT("170 is left alone"), FElysiumNpc::ChainWrapDegrees(170.f), 170.f);
	TestEqual(TEXT("the wrap is SINGLE-PASS: 730 comes out at 370, not 10"),
		FElysiumNpc::ChainWrapDegrees(730.f), 370.f);

	// SEAM: the autoaim mode is `AUTOAIM_NONE`, which is arm one of `GetAutoaimVector` — and arm
	// one does NOT include `m_vecAutoAim`, so a retained deflection survives a toggle and is
	// ignored while off.
	TestFalse(TEXT("the game-rules autoaim seam answers AUTOAIM_NONE"),
		Npc.GameRulesAutoAimEnabled());
	Npc.LocalPunchAngle = FRotator(0.f, 0.f, 0.f);
	Npc.EyeAngle = FRotator(0.f, 90.f, 0.f);
	Npc.AutoAim = FRotator(0.f, 45.f, 0.f);
	const FVector Off = Npc.GetAutoaimVector(FVector::ZeroVector);
	TestTrue(TEXT("autoaim off answers punch + v_angle, ignoring m_vecAutoAim"),
		Off.Equals(FRotator(0.f, 90.f, 0.f).Vector(), 1e-4f));
	TestEqual(TEXT("and does not clear the retained deflection"),
		static_cast<float>(Npc.AutoAim.Yaw), 45.f);

	// `AllowAutoTargetCrosshair` answers TRUE deliberately: false is the arm that CLEARS
	// `m_fOnTarget`, and a missing rules object must not clear a flag retail only clears on demand.
	TestTrue(TEXT("the crosshair-rules seam answers true, the leave-it-alone arm"),
		Npc.GameRulesAllowAutoTargetCrosshair());

	// `AutoaimDeflection` opens with the same gate and clears `m_fOnTarget` before anything is
	// found, so an autoaim-off deflection is zero with the flag down.
	Npc.bOnTarget = true;
	const FRotator Deflection = Npc.AutoaimDeflection(FVector::ZeroVector, 16384.f, 0.f);
	TestEqual(TEXT("an autoaim-off deflection is zero"), Deflection, FRotator::ZeroRotator);
	TestFalse(TEXT("and m_fOnTarget is cleared first, not last"), Npc.bOnTarget);

	// The water-level pair, both directions, spelled from the decompilation.
	Npc.WaterLevel = 0;
	Fixture.Other->WaterLevel = 3;
	TestTrue(TEXT("dry land cannot autoaim at something fully submerged"),
		Npc.AutoaimWaterLevelBlocks(*Fixture.Other));
	Npc.WaterLevel = 3;
	Fixture.Other->WaterLevel = 0;
	TestTrue(TEXT("and under water cannot autoaim at something fully dry"),
		Npc.AutoaimWaterLevelBlocks(*Fixture.Other));
	Npc.WaterLevel = 1;
	Fixture.Other->WaterLevel = 1;
	TestFalse(TEXT("two wading entities are not blocked"),
		Npc.AutoaimWaterLevelBlocks(*Fixture.Other));
	Npc.WaterLevel = 0;
	Fixture.Other->WaterLevel = 0;

	// The three unrecovered blend words, asserted as seams so the gap is explicit.
	float Scale = 9.f;
	float OldWeight = 9.f;
	TestTrue(TEXT("the blend takes the DAT_1070ba3c == 1 scale arm"),
		Npc.AutoaimBlendWeights(Scale, OldWeight));
	TestEqual(TEXT("with an unrecovered scale, answering 0"), Scale, 0.f);
	TestEqual(TEXT("the deflection delta is unrecovered too"), Npc.AutoaimDelta(), 0.f);

	// `m_takedamage` reaches the autoaim trace through the NPC leaf only.
	TestTrue(TEXT("a live NPC takes damage"), FElysiumNpc::ChainTakesDamage(*Fixture.Other));
	Fixture.Other->TakeDamageMode = 0;
	TestFalse(TEXT("DAMAGE_NO does not"), FElysiumNpc::ChainTakesDamage(*Fixture.Other));
	Fixture.Other->TakeDamageMode = 2;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainUseAndControllerTest,
	"Elysium.Substrate.NpcKernelEntityChain.UseAndController",
	GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainUseAndControllerTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard) || Fixture.Other == nullptr)
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// --- 0x1017c6d0 `ClearUseEntity` ---
	// The clear is OUTSIDE every guard: a release with the one-shot already down still drops the
	// held entity, silently.
	Npc.UseEntityIndex = 17;
	Npc.bUseEntityNotify = false;
	Npc.UnrecoveredChainCalls.Reset();
	Npc.ClearUseEntity();
	TestEqual(TEXT("the held entity is dropped even with the one-shot down"), Npc.UseEntityIndex, 0);
	TestEqual(TEXT("and nothing is dispatched"), Npc.UnrecoveredChainCalls.Num(), 0);

	// With the one-shot up, the edict seam is asked and refuses — so the input is not fired and the
	// one-shot is NOT consumed, which is retail's own inner guard.
	Npc.UseEntityIndex = 17;
	Npc.bUseEntityNotify = true;
	Npc.ClearUseEntity();
	TestEqual(TEXT("the held entity is dropped"), Npc.UseEntityIndex, 0);
	TestTrue(TEXT("the one-shot survives a failed resolve"), Npc.bUseEntityNotify);
	TestEqual(TEXT("and the edict seam's refusal fires no input"),
		Npc.UnrecoveredChainCalls.Num(), 0);
	TestNull(TEXT("the edict seam answers null"), Npc.EntityOfEdict(&Npc));

	// --- 0x10182c40 `SetPlayerAnim` ---
	// The pointer at +0x1ca4 tracks whether the COPIED name is non-empty, and the buffer is written
	// either way.
	Npc.PlayerAnimFlags = 0;
	Npc.SetPlayerAnim(TEXT("player_reload"), nullptr);
	TestTrue(TEXT("a non-empty name arms the pointer"), Npc.bPlayerAnimNameSet);
	TestEqual(TEXT("and lands in the one global buffer"), Npc.PlayerAnimNameBuffer,
		FString(TEXT("player_reload")));
	TestEqual(TEXT("bit 0 of +0x1cac is raised"), Npc.PlayerAnimFlags & 1, 1);
	// SEAM: both the lead-in and the no-sound fallback are unrecovered and answer 0, so the deadline
	// is bare curtime and the animation is over on the frame it is armed.
	TestEqual(TEXT("the _DAT_10449270 lead-in is unrecovered, answering 0"), Npc.PlayerAnimLeadIn(),
		0.f);
	TestEqual(TEXT("and the _DAT_10471720 fallback duration too"),
		Npc.PlayerAnimFallbackDuration(), 0.f);
	TestEqual(TEXT("so the deadline is bare curtime"), Npc.PlayerAnimEndTime,
		static_cast<float>(Fixture.World.World.NowSeconds()));

	Npc.SetPlayerAnim(TEXT(""), nullptr);
	TestFalse(TEXT("an empty name disarms the pointer"), Npc.bPlayerAnimNameSet);
	TestTrue(TEXT("but the buffer is still overwritten"), Npc.PlayerAnimNameBuffer.IsEmpty());

	// --- 0x101618e0 `ReleaseControllerNpc` ---
	// The gate is the handle resolving. Nothing happens otherwise — not even the clear.
	Npc.ControllerNpc = FElysiumEntityHandle::Invalid();
	Npc.SequenceNumber = 5;
	Npc.ReleaseControllerNpc(true, true);
	TestEqual(TEXT("an unset controller handle changes nothing"), Npc.SequenceNumber, 5);

	FElysiumNpc& Controller = *Fixture.Other;
	Controller.SequenceNumber = 77;
	Controller.AnimTime = 1.5f;
	Controller.SequenceCycle = 0.25f;
	Controller.SequencePlaybackRate = 2.f;
	Controller.AnimOverlay[2].Activity = 0x99;
	Controller.Flinch[1].Sequence = 31;
	Controller.Velocity = FVector(1.f, 2.f, 3.f);
	Controller.AngularVelocity = FVector(4.f, 5.f, 6.f);

	Npc.ControllerNpc = Controller.Handle;
	Npc.SequenceNumber = 0;
	Npc.Velocity = FVector::ZeroVector;
	Npc.ReleaseControllerNpc(/*bCopyAnimation*/ false, /*bCopyVelocity*/ false);
	TestEqual(TEXT("with both flags clear, no animation travels"), Npc.SequenceNumber, 0);
	TestEqual(TEXT("and no velocity"), Npc.Velocity, FVector::ZeroVector);
	TestFalse(TEXT("but the handle is cleared unconditionally"), Npc.ControllerNpc.IsSet());

	Npc.ControllerNpc = Controller.Handle;
	Npc.ReleaseControllerNpc(/*bCopyAnimation*/ true, /*bCopyVelocity*/ true);
	TestEqual(TEXT("+0x6f0 m_nSequence travels"), Npc.SequenceNumber, 77);
	TestEqual(TEXT("+0x174 m_flAnimTime travels"), Npc.AnimTime, 1.5f);
	TestEqual(TEXT("+0x6f8 m_flCycle travels"), Npc.SequenceCycle, 0.25f);
	TestEqual(TEXT("+0x6f4 m_flPlaybackRate travels"), Npc.SequencePlaybackRate, 2.f);
	TestEqual(TEXT("the whole gesture-layer table travels (0xc0 bytes from +0x734)"),
		Npc.AnimOverlay[2].Activity, 0x99);
	TestEqual(TEXT("and the whole flinch table (0x54 bytes from +0x7f4)"), Npc.Flinch[1].Sequence,
		31);
	TestEqual(TEXT("the absolute velocity transfers"), Npc.Velocity, FVector(1.f, 2.f, 3.f));
	TestEqual(TEXT("and the angular velocity"), Npc.AngularVelocity, FVector(4.f, 5.f, 6.f));
	TestFalse(TEXT("and the link is detached"), Npc.ControllerNpc.IsSet());
	// The one-shot think is armed on the CONTROLLER, not on the releaser; the delay is unrecovered
	// and answers 0, which is the next pass.
	TestEqual(TEXT("the released controller's think is armed at curtime + the seam's 0"),
		static_cast<double>(Controller.NextThink), Fixture.World.World.NowSeconds());
	TestEqual(TEXT("the _DAT_1044e658 think delay is unrecovered, answering 0"),
		Npc.ControllerReleaseThinkDelay(), 0.f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 65 and the think channel.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAnglesAndThinkTest,
	"Elysium.Substrate.NpcKernelEntityChain.AnglesAndThink", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainAnglesAndThinkTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// 0x10026a50 slot 65 — the PACKING is the body, and it dispatches slot 64. SEAM: slot 64 is
	// 29c's stub, so nothing is stored; what is assertable is that the three-scalar overload and the
	// FRotator overload are the SAME call, which is what the packing means.
	Npc.SetAngles(10.f, 20.f, 30.f);
	Npc.SetAngles(FRotator(10.f, 20.f, 30.f));
	TestTrue(TEXT("slot 65 is slot 64 with its arguments packed"), true);

	// 0x101a64a0 — the FOURTH think channel, beside family Lifecycle's other three.
	Npc.ScheduleHost.LastAI = 12.5;
	Npc.ScheduleHost.LastUpdate = 1.0;
	Npc.ScheduleHost.LastNormal = 2.0;
	Npc.ScheduleHost.LastMove = 3.0;
	TestEqual(TEXT("0x101a64a0 reads the AI channel"), Npc.LastAiThink(), 12.5f);
	TestEqual(TEXT("and not the update channel"), Npc.LastUpdateThink(), 1.f);
	TestEqual(TEXT("nor the normal channel"), Npc.LastNormalThink(), 2.f);
	TestEqual(TEXT("nor the move channel"), Npc.LastMoveThink(), 3.f);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
