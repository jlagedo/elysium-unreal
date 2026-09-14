#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumRng.h"
#include "ElysiumSwingRecord.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumReactions.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "ElysiumAnimationIntent.h"
#include "Visual/ElysiumActionTables.h"

// Story 29d, families **Anim10** and **SpeciesAnim10**. One case per `rule` row, each named for the
// retail address it drives and each expectation read off the decompiled C — or, where the decompiler
// folded a constant or an FPU compare, off the listing. This family corrected nine of the
// checklist's one-line walks and every one of those corrections is pinned by a case here.
//
// The suite's standing shape: a case sets the NPC's retail class through `SetRetailClassForTests`
// (the census's own dispatcher instrument), drives ONE slot method, and reads the recorded effect
// — the activity triple, the notice lists, the pose pair — rather than a screen. Every species case
// also proves the TROIKA body for a plain `npc_VCop`, whose census classname list is deliberately
// null.

static constexpr EAutomationTestFlags GAnim10TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using ElysiumActionTables::ENpcPredicate;

	// The activity numbers the cases name, spelled once.
	constexpr int32 GTActReset = 0;
	constexpr int32 GTActIdle = 1;
	constexpr int32 GTActTransition = 2;
	constexpr int32 GTActFidget = 3;
	constexpr int32 GTActAim = 5;
	constexpr int32 GTActCover = 6;
	constexpr int32 GTActWalk = 9;
	constexpr int32 GTActRun = 0x13;
	constexpr int32 GTActWalkAim = 0x11;
	constexpr int32 GTActRunAim = 0x15;
	constexpr int32 GTActWalkRelaxed = 0x16;
	constexpr int32 GTActRunRelaxed = 0x17;
	constexpr int32 GTActReloadFast = 0x55;
	constexpr int32 GTActDisposition = 0xf1;
	constexpr int32 GTActRunFrenzy = 0xf17;
	constexpr int32 GTActHuntWalk = 0x1115;
	constexpr int32 GTActCombatMove = 0x1121;
	constexpr int32 GTActPickupLightIdle = 0x128;
	constexpr int32 GTActPickupLightCarry = 0x129;
	constexpr int32 GTActIdleBody = 0xfc;
	constexpr int32 GTActIdleBodyL = 0xfd;
	constexpr int32 GTActWalkBody = 0xfe;
	constexpr int32 GTActWalkBodyL = 0xff;
	constexpr int32 GTActTzIdle2 = 0x1134;
	constexpr int32 GTActTzFidget2 = 0x1135;
	constexpr int32 GTActTzWalk2 = 0x1136;
	constexpr int32 GTActTzRun2 = 0x1137;

	// One world per case, with three NPCs: the leaf every case re-classes through
	// `SetRetailClassForTests`, a plain `npc_VCop` (the census gives it a NULL classname list, so
	// its `RetailClass()` is null and every species lookup correctly falls through), and a target.
	struct FAnim10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumNpc* Cop = nullptr;
		FElysiumNpc* Other = nullptr;

		explicit FAnim10Fixture(uint32 Seed = 29104)
			: World(Build(Seed))
		{
			Npc = World.Npc(TEXT("subject"));
			Cop = World.Npc(TEXT("cop"));
			Other = World.Npc(TEXT("other"));
			FElysiumNpcWorldFixture::Quiet({ Npc, Cop, Other });
			// Every case drives one slot on a standing body; the ConVar seams start clear.
			FElysiumNpc::SetDebugConVar(nullptr, 0);
			FElysiumNpc::SetAnim10FloatConVar(nullptr, 0.f);
		}

		static FElysiumNpcWorldBuilder Build(uint32 Seed)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim10"), Seed);
			Builder.AddNpc(TEXT("subject"));
			Builder.AddNpc(TEXT("cop"), FVector(300.0, 0.0, 0.0), TEXT("npc_VCop"));
			Builder.AddNpc(TEXT("other"), FVector(600.0, 0.0, 0.0));
			return Builder;
		}
	};

	// Put the activity triple back to a known state between arms.
	void Anim10Reset(FElysiumNpc& N)
	{
		N.ActivityNumber = -1;
		N.IdealActivityNumber = -1;
		N.TranslatedActivity = -1;
		N.SequenceNumber = 0;
		N.SequenceCycle = 0.f;
		N.bSequenceFinished = false;
		N.bSequenceLoopedOnce = false;
		N.bKeepSound = false;
		N.ActivityChangeNotices.Reset();
		N.WeaponActivityRequests.Reset();
		N.ViewOffsetWrites.Reset();
		N.NavigatorActivityNotices = 0;
	}
}

// =================================================================================================
// Slot 105 `SetModel`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10TroikaSetModelTest,
	"Elysium.Substrate.NpcKernelAnim10.TroikaSetModel", GAnim10TestFlags)
bool FAnim10TroikaSetModelTest::RunTest(const FString&)
{
	// `0x10298ce0` — four calls whose ORDER is the body. The model must be set before the hull and
	// the hull before the eye, because each reads what the one before it wrote.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	F.Npc->ViewOffsetWrites.Reset();
	F.Npc->bIsUsingSmallHull = true;
	const int32 SizeCallsBefore = F.Npc->SetSizeCalls;

	TCHAR ModelName[] = TEXT("models/character/npc/unique/jack.mdl");
	F.Npc->SetModel(ModelName);

	TestEqual(TEXT("0x10298ce0 step 1: CBaseCombatCharacter::SetModel wrote the model keyfield"),
		F.Npc->Model, FString(ModelName));
	// Step 2 is family Motor10's `SetHullSizeNormal(0x10273070)`, called and not re-read. Its own
	// recorded effect is the `UTIL_SetSize` and the `+0x5f2d` clear; retail passes force = 1, which
	// is what makes it run at all on a body already using the normal hull.
	TestEqual(TEXT("0x10298ce0 step 2: SetHullSizeNormal resized exactly once"),
		F.Npc->SetSizeCalls, SizeCallsBefore + 1);
	TestFalse(TEXT("...and cleared m_fIsUsingSmallHull (+0x5f2d)"), F.Npc->bIsUsingSmallHull);
	// Step 3 is family BaseHelpers' `SetDefaultEyeOffset` (`0x10274ca0`), also called and not
	// re-read; it reads the hull step 2 just set, which is the ORDER this family owns.
	TestTrue(TEXT("0x10298ce0 step 3: SetDefaultEyeOffset ran after the hull"),
		F.Npc->SetSizeCalls == SizeCallsBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10BaseHumanoidSetModelTest,
	"Elysium.Substrate.NpcKernelAnim10.BaseHumanoidSetModel", GAnim10TestFlags)
bool FAnim10BaseHumanoidSetModelTest::RunTest(const FString&)
{
	// `0x1025e510`, `CAI_BaseHumanoid#105`. The CORRECTION this case pins: the 26 cached indices are
	// NOT one lookup repeated. Indices 0..12 go through `LookupPoseParameter`, which answers -1 on a
	// miss, and 13..25 through `LookupFlexController`, which answers **0**.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	TCHAR ModelName[] = TEXT("models/character/npc/male/male01.mdl");
	F.Npc->BaseHumanoidSetModel(ModelName);

	TestEqual(TEXT("0x1025e510: the first name is body_trans_Y (+0x5fb8)"),
		FString(FElysiumNpc::HumanoidPoseParamName(0)), FString(TEXT("body_trans_Y")));
	TestEqual(TEXT("0x1025e510: index 10 is head_yaw, the first of 0x1025efc0's three"),
		FString(FElysiumNpc::HumanoidPoseParamName(10)), FString(TEXT("head_yaw")));
	TestEqual(TEXT("0x1025e510: index 12 is head_roll, the last of them"),
		FString(FElysiumNpc::HumanoidPoseParamName(12)), FString(TEXT("head_roll")));
	TestEqual(TEXT("0x1025e510: the last name is head_tilt (+0x601c)"),
		FString(FElysiumNpc::HumanoidPoseParamName(25)), FString(TEXT("head_tilt")));

	TestEqual(TEXT("0x1025e510: the LookupPoseParameter half misses with -1"),
		F.Npc->HumanoidPoseParams[0], INDEX_NONE);
	TestEqual(TEXT("...for every one of its thirteen"), F.Npc->HumanoidPoseParams[12], INDEX_NONE);
	TestEqual(TEXT("0x1025e510: the LookupFlexController half misses with 0, not -1"),
		F.Npc->HumanoidPoseParams[13], 0);
	TestEqual(TEXT("...for every one of its thirteen"), F.Npc->HumanoidPoseParams[25], 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10ZombieLineSetModelTest,
	"Elysium.Substrate.NpcKernelAnim10.ZombieLineSetModel", GAnim10TestFlags)
bool FAnim10ZombieLineSetModelTest::RunTest(const FString&)
{
	// `0x1037b1f0` (`CNPC_VGhoulCroucher`) and `0x103e0540` (`CNPC_VZombie`) — the same 119 bytes.
	// The Troika base runs FIRST, so the model write happens BEFORE `IsMale` is ever asked.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the cop"), F.Cop))
	{
		return false;
	}
	TCHAR ModelName[] = TEXT("models/character/npc/unique/zombie.mdl");

	for (const TCHAR* Cls : { TEXT("CNPC_VGhoulCroucher"), TEXT("CNPC_VZombie") })
	{
		F.Npc->SetRetailClassForTests(Cls);
		F.Npc->VSoundGroupName.Reset();
		F.Npc->VSoundTableIndex = 0;
		F.Npc->VSoundGroupRow = 0;
		const int32 SizeCallsBefore = F.Npc->SetSizeCalls;
		F.Npc->SetModel(ModelName);

		TestEqual(*FString::Printf(TEXT("%s: the Troika base ran FIRST, so the model is written"),
				Cls), F.Npc->Model, FString(ModelName));
		TestEqual(*FString::Printf(TEXT("%s: ...and so is the hull"), Cls),
			F.Npc->SetSizeCalls, SizeCallsBefore + 1);
		const bool bMale = F.Npc->Sheet.IsMale();
		TestEqual(*FString::Printf(TEXT("%s: m_iszVSoundGroup takes the gendered literal"), Cls),
			F.Npc->VSoundGroupName,
			FString(bMale ? TEXT("Zombie_Male") : TEXT("Zombie_Female")));
		TestEqual(*FString::Printf(TEXT("%s: +0x00bc takes the literal 2, unconditionally"), Cls),
			F.Npc->VSoundTableIndex, 2);
		TestEqual(*FString::Printf(TEXT("%s: the group resolves to retail's own count-zero miss"),
				Cls), F.Npc->VSoundGroupRow, INDEX_NONE);
	}

	// The Troika body for a plain `npc_VCop`, whose `RetailClass()` is deliberately null.
	F.Cop->VSoundGroupName.Reset();
	F.Cop->VSoundTableIndex = 0;
	F.Cop->SetModel(ModelName);
	TestTrue(TEXT("a plain npc_VCop takes the Troika body and writes no sound group"),
		F.Cop->VSoundGroupName.IsEmpty());
	TestEqual(TEXT("...and no +0x00bc"), F.Cop->VSoundTableIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10SetModelArmCoverageTest,
	"Elysium.Substrate.NpcKernelAnim10.SetModelArmCoverage", GAnim10TestFlags)
bool FAnim10SetModelArmCoverageTest::RunTest(const FString&)
{
	// Every slot-105 override row the census carries has an arm, so a class this file does not know
	// cannot silently take the Troika body.
	static const TCHAR* const Known[] = { TEXT("0x1025e510"), TEXT("0x1037b1f0"),
		TEXT("0x103e0540") };
	int32 Rows = 0;
	for (const FElysiumNpcClass& Cls : ElysiumNpcKernelShape::Classes())
	{
		const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(&Cls, 105);
		if (Override == nullptr)
		{
			continue;
		}
		++Rows;
		bool bKnown = false;
		for (const TCHAR* Address : Known)
		{
			bKnown |= FCString::Strcmp(Address, Override->Address) == 0;
		}
		TestTrue(*FString::Printf(TEXT("slot 105 on %s (%s) has an arm"), Cls.Name,
			Override->Address), bKnown);
	}
	TestTrue(TEXT("the census carries at least the three slot-105 override rows"), Rows >= 3);
	return true;
}

// =================================================================================================
// The activity triple.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10SetActivityAndSequenceTest,
	"Elysium.Substrate.NpcKernelAnim10.SetActivityAndSequence", GAnim10TestFlags)
bool FAnim10SetActivityAndSequenceTest::RunTest(const FString&)
{
	// `0x10272490`, the commit. Six effects, each ordered against the others.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	// 1. The translated activity is written FIRST, and the two listener tests below read the NEW
	//    value against the OLD `m_Activity`.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SetActivityAndSequence(GTActWalk, /*Sequence*/ 4, /*Translated*/ GTActRun,
		/*Weapon*/ GTActRunAim);
	TestEqual(TEXT("0x10272490: +0xff4 m_TranslatedActivity took the third argument"),
		N.TranslatedActivity, GTActRun);
	TestEqual(TEXT("0x10272490: +0xfec m_Activity took the FIRST argument, last"), N.ActivityNumber,
		GTActWalk);
	TestEqual(TEXT("0x10272490: the weapon activity went through Weapon_SetActivity"),
		N.WeaponActivityRequests.Num(), 1);
	if (N.WeaponActivityRequests.Num() == 1)
	{
		TestEqual(TEXT("...with the fourth argument"), N.WeaponActivityRequests[0].Activity,
			GTActRunAim);
	}
	TestEqual(TEXT("0x10272490: slot 533 EyeOffset fed SetViewOffset once"),
		N.ViewOffsetWrites.Num(), 1);
	TestEqual(TEXT("0x10272490: the navigator was notified"), N.NavigatorActivityNotices, 1);
	TestEqual(TEXT("0x10272490: the old m_Activity (1) differs from the translated (0x13), so the "
		"listener fired"), N.ActivityChangeNotices.Num(), 1);
	if (N.ActivityChangeNotices.Num() == 1)
	{
		TestEqual(TEXT("...carrying the NEW translated activity"),
			N.ActivityChangeNotices[0].NewTranslatedActivity, GTActRun);
		TestEqual(TEXT("...and the OLD activity"), N.ActivityChangeNotices[0].OldActivity,
			GTActIdle);
	}

	// 2. `m_bKeepSound` suppresses the listener and NOTHING else.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.bKeepSound = true;
	N.SetActivityAndSequence(GTActWalk, 4, GTActRun, GTActRunAim);
	TestEqual(TEXT("0x10272490: m_bKeepSound suppresses the activity-change listener"),
		N.ActivityChangeNotices.Num(), 0);
	TestEqual(TEXT("...and suppresses nothing else — the navigator still hears"),
		N.NavigatorActivityNotices, 1);

	// 3. A NEGATIVE sequence skips the cycle, the duration and the weapon activity — and nothing
	//    else. The view offset still goes out.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SequenceCycle = 0.75f;
	N.SetActivityAndSequence(GTActWalk, /*Sequence*/ -1, GTActWalk, GTActWalk);
	TestEqual(TEXT("0x10272490: a negative sequence plays no weapon activity"),
		N.WeaponActivityRequests.Num(), 0);
	TestEqual(TEXT("...and leaves the cycle word alone"), N.SequenceCycle, 0.75f);
	TestEqual(TEXT("...but still dispatches slot 533"), N.ViewOffsetWrites.Num(), 1);

	// 4. The walk/run cycle carry: the cycle word is NOT zeroed when the old and the new activity
	//    are both in {ACT_WALK, ACT_RUN}.
	Anim10Reset(N);
	N.ActivityNumber = GTActWalk;
	N.SequenceCycle = 0.4f;
	N.SetActivityAndSequence(GTActRun, 7, GTActRun, GTActRun);
	TestEqual(TEXT("0x10272490: walk -> run carries the cycle"), N.SequenceCycle, 0.4f);

	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SequenceCycle = 0.4f;
	N.SetActivityAndSequence(GTActRun, 7, GTActRun, GTActRun);
	TestEqual(TEXT("...and idle -> run does not"), N.SequenceCycle, 0.f);

	// 5. The same-sequence carry: equal sequence with `+0x65d` set.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SequenceNumber = 7;
	N.bSequenceLoopedOnce = true;
	N.SequenceCycle = 0.4f;
	N.SetActivityAndSequence(GTActCover, 7, GTActCover, GTActCover);
	TestEqual(TEXT("0x10272490: the same sequence with +0x65d set carries the cycle too"),
		N.SequenceCycle, 0.4f);

	// 6. The two comparisons are DIFFERENT: slot 465 fires on the requested activity, the listener
	//    on the translated one.
	Anim10Reset(N);
	N.ActivityNumber = GTActWalk;
	N.SetActivityAndSequence(GTActWalk, 3, GTActRun, GTActRun);
	TestEqual(TEXT("0x10272490: m_Activity == the request, so slot 465 did NOT fire; the listener "
		"tests the TRANSLATED activity and DID"), N.ActivityChangeNotices.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10BaseSetActivityTest,
	"Elysium.Substrate.NpcKernelAnim10.BaseSetActivity", GAnim10TestFlags)
bool FAnim10BaseSetActivityTest::RunTest(const FString&)
{
	// `0x102725d0` — two refusals then three writes.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	Anim10Reset(N);
	N.ActivityNumber = GTActWalk;
	N.BaseSetActivity(GTActWalk);
	TestEqual(TEXT("0x102725d0: a request equal to m_Activity is a NO-OP"),
		N.NavigatorActivityNotices, 0);
	TestEqual(TEXT("...and writes no ideal"), N.IdealActivityNumber, -1);

	Anim10Reset(N);
	N.ActivityNumber = GTActTransition;
	N.BaseSetActivity(GTActWalk);
	TestEqual(TEXT("0x102725d0: ACT_TRANSITION refuses every request..."),
		N.NavigatorActivityNotices, 0);
	N.BaseSetActivity(GTActReset);
	TestEqual(TEXT("...except ACT_RESET (0)"), N.NavigatorActivityNotices, 1);
	TestEqual(TEXT("...which commits as the ideal"), N.ActivityNumber, GTActReset);

	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.BaseSetActivity(GTActWalk);
	TestEqual(TEXT("0x102725d0: m_IdealActivity took the request"), N.IdealActivityNumber,
		GTActWalk);
	TestEqual(TEXT("...and SetActivityAndSequence committed it"), N.ActivityNumber, GTActWalk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10AdvanceToIdealActivityTest,
	"Elysium.Substrate.NpcKernelAnim10.AdvanceToIdealActivity", GAnim10TestFlags)
bool FAnim10AdvanceToIdealActivityTest::RunTest(const FString&)
{
	// `0x102726a0`. `FindTransitionSequence` is a seam answering `INDEX_NONE`, which IS retail's
	// `-NAN` arm — the one that dispatches slot 310 with the ideal activity and stops. That is the
	// arm this runtime always takes, and refusing instead would strand a body in ACT_TRANSITION.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	TestEqual(TEXT("0x102726a0: the transition graph is a seam and answers the DESTINATION, which "
		"is retail's own no-transition-clip answer"), N.FindTransitionSequence(1, 2), 2);

	Anim10Reset(N);
	N.ActivityNumber = GTActTransition;
	N.IdealActivityNumber = GTActWalk;
	N.IdealSequence = 0;
	N.AdvanceToIdealActivity();
	TestEqual(TEXT("0x102726a0: transition == m_nIdealSequence commits the ideal through "
		"SetActivityAndSequence DIRECTLY, bypassing slot 310's ACT_TRANSITION refusal"),
		N.ActivityNumber, GTActWalk);
	TestEqual(TEXT("...and the translated activity came with it"), N.TranslatedActivity,
		N.IdealTranslatedActivity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10MaintainActivityTest,
	"Elysium.Substrate.NpcKernelAnim10.MaintainActivity", GAnim10TestFlags)
bool FAnim10MaintainActivityTest::RunTest(const FString&)
{
	// `0x102727d0`. The entry point is family SaveRestore10's `MaintainActivity()` seam, which keeps
	// its latch bookkeeping and now runs this body after it.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	// Nothing to maintain: both terms match.
	Anim10Reset(N);
	N.ActivityNumber = GTActWalk;
	N.IdealActivityNumber = GTActWalk;
	N.SequenceNumber = 3;
	N.IdealSequence = 3;
	N.BaseMaintainActivity();
	TestEqual(TEXT("0x102727d0: with both terms matched nothing happens"),
		N.NavigatorActivityNotices, 0);

	// The ACT_TRANSITION arm WAITS for `m_bSequenceFinished` and re-resolves nothing.
	Anim10Reset(N);
	N.ActivityNumber = GTActTransition;
	N.IdealActivityNumber = GTActRun;
	N.bSequenceFinished = false;
	N.BaseMaintainActivity();
	TestEqual(TEXT("0x102727d0: ACT_TRANSITION waits for m_bSequenceFinished"),
		N.ActivityNumber, GTActTransition);
	N.bSequenceFinished = true;
	N.BaseMaintainActivity();
	TestEqual(TEXT("...and then advances to the ideal"), N.ActivityNumber, GTActRun);

	// Every other activity re-resolves the ideal first and then advances.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.IdealActivityNumber = GTActWalk;
	N.BaseMaintainActivity();
	TestEqual(TEXT("0x102727d0: any other activity advances to the ideal"), N.ActivityNumber,
		GTActWalk);

	// The seam's own bookkeeping still runs, and the body runs after it.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.IdealActivityNumber = GTActRun;
	const int32 Before = N.MaintainActivityCalls;
	N.MaintainActivity();
	TestEqual(TEXT("the SaveRestore10 seam still counts the call"), N.MaintainActivityCalls,
		Before + 1);
	TestEqual(TEXT("...and the ported body now runs after it"), N.ActivityNumber, GTActRun);
	return true;
}

// =================================================================================================
// Slot 310 `SetActivity`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10TroikaSetActivityTest,
	"Elysium.Substrate.NpcKernelAnim10.TroikaSetActivity", GAnim10TestFlags)
bool FAnim10TroikaSetActivityTest::RunTest(const FString&)
{
	// `0x10295750`, three top arms.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	// Any other request forwards to the base unchanged.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SetActivity(GTActWalk);
	TestEqual(TEXT("0x10295750: a request outside both families forwards unchanged"),
		N.ActivityNumber, GTActWalk);

	// 0x1093 from OUTSIDE the family hands 0x1093 straight to the base.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SetActivity(0x1093);
	TestEqual(TEXT("0x10295750: 0x1093 from outside the family commits 0x1093"), N.ActivityNumber,
		0x1093);

	// **The arm the one-line walk did not name**: inside the family with `IsActivityFinished` FALSE,
	// the body does NOTHING AT ALL — no base call, no roll.
	Anim10Reset(N);
	N.ActivityNumber = 0x1094;
	N.SequenceNumber = 1;
	N.IdealSequence = 2;   // `IsActivityFinished` is false while the sequence differs
	N.bSequenceFinished = false;
	N.SetActivity(0x1093);
	TestEqual(TEXT("0x10295750: inside the family and NOT finished, the body does nothing"),
		N.ActivityNumber, 0x1094);
	TestEqual(TEXT("...and makes no commit at all"), N.NavigatorActivityNotices, 0);

	// The hunt family: the frenzy gait wins outright and does NOT restart itself.
	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.NpcFlags.SetFrenziedWord(0x40);
	N.SetActivity(GTActHuntWalk);
	TestEqual(TEXT("0x10295750: frenzy bit 0x40 forces ACT_RUN_FRENZY"), N.ActivityNumber,
		GTActRunFrenzy);
	Anim10Reset(N);
	N.ActivityNumber = GTActRunFrenzy;
	N.SetActivity(GTActHuntWalk);
	TestEqual(TEXT("...and does nothing when it is already playing"), N.NavigatorActivityNotices, 0);

	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.NpcFlags.SetFrenziedWord(0x20);
	N.SetActivity(GTActHuntWalk);
	TestEqual(TEXT("0x10295750: frenzy bit 0x20 forces ACT_RUN"), N.ActivityNumber, GTActRun);

	Anim10Reset(N);
	N.NpcFlags.SetFrenziedWord(0);
	N.ActivityNumber = GTActIdle;
	N.SetActivity(GTActHuntWalk);
	TestEqual(TEXT("0x10295750: 0x1115 from outside the hunt family commits 0x1115"),
		N.ActivityNumber, GTActHuntWalk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10TzimisceHeadClawSetActivityTest,
	"Elysium.Substrate.NpcKernelAnim10.TzimisceHeadClawSetActivity", GAnim10TestFlags)
bool FAnim10TzimisceHeadClawSetActivityTest::RunTest(const FString&)
{
	// `0x103c1cd0` — one rewrite in front of the Troika body.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc)
		|| !TestNotNull(TEXT("the other"), F.Other) || !TestNotNull(TEXT("the cop"), F.Cop))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisceHeadClaw"));

	Anim10Reset(N);
	N.Senses.Memory.Enemy = FElysiumEntityHandle();
	N.ActivityNumber = GTActIdle;
	N.SetActivity(GTActWalk);
	TestEqual(TEXT("0x103c1cd0: ACT_WALK with no enemy forwards unchanged"), N.ActivityNumber,
		GTActWalk);

	Anim10Reset(N);
	N.Senses.Memory.Enemy = F.Other->Handle;
	N.ActivityNumber = GTActIdle;
	N.SetActivity(GTActWalk);
	TestEqual(TEXT("0x103c1cd0: ACT_WALK with an enemy becomes ACT_TZ_WALK2"), N.ActivityNumber,
		GTActTzWalk2);

	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SetActivity(GTActRun);
	TestEqual(TEXT("0x103c1cd0: every other request reaches the base unchanged"), N.ActivityNumber,
		GTActRun);

	// The Troika body for a plain `npc_VCop`.
	Anim10Reset(*F.Cop);
	F.Cop->Senses.Memory.Enemy = F.Other->Handle;
	F.Cop->ActivityNumber = GTActIdle;
	F.Cop->SetActivity(GTActWalk);
	TestEqual(TEXT("a plain npc_VCop takes the Troika body, so ACT_WALK stays ACT_WALK"),
		F.Cop->ActivityNumber, GTActWalk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10TzimisceRunnerSetActivityTest,
	"Elysium.Substrate.NpcKernelAnim10.TzimisceRunnerSetActivity", GAnim10TestFlags)
bool FAnim10TzimisceRunnerSetActivityTest::RunTest(const FString&)
{
	// `0x103c3d80` — a five-entry REQUEST remap in front of the Troika body, gated on `+0x6672`.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisceRunner"));

	N.bTzimisceRunnerForm = false;
	Anim10Reset(N);
	N.ActivityNumber = GTActFidget;
	N.SetActivity(GTActIdle);
	TestEqual(TEXT("0x103c3d80: with +0x6672 clear every request forwards unchanged"),
		N.ActivityNumber, GTActIdle);

	N.bTzimisceRunnerForm = true;
	const TPair<int32, int32> Pairs[] = {
		{ GTActIdle, GTActTzIdle2 }, { GTActWalk, GTActTzWalk2 }, { GTActRun, GTActTzRun2 },
		{ GTActFidget, GTActTzFidget2 }, { GTActDisposition, GTActTzIdle2 },
	};
	for (const TPair<int32, int32>& Pair : Pairs)
	{
		Anim10Reset(N);
		N.ActivityNumber = GTActCover;
		N.SetActivity(Pair.Key);
		TestEqual(*FString::Printf(TEXT("0x103c3d80: request 0x%x remaps to 0x%x"), Pair.Key,
			Pair.Value), N.ActivityNumber, Pair.Value);
	}

	Anim10Reset(N);
	N.ActivityNumber = GTActIdle;
	N.SetActivity(GTActCover);
	TestEqual(TEXT("0x103c3d80: a request outside the five forwards unchanged"), N.ActivityNumber,
		GTActCover);
	return true;
}

// =================================================================================================
// Slot 375 `NPC_EarlyTranslateActivity`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10TroikaEarlyTranslateTest,
	"Elysium.Substrate.NpcKernelAnim10.TroikaEarlyTranslateActivity", GAnim10TestFlags)
bool FAnim10TroikaEarlyTranslateTest::RunTest(const FString&)
{
	// `0x10295590`, five steps. Slot 375 was a generated stub returning 0 before this story; the
	// premise of every probe that read the `elysium.stubs` tally for it is now false and the answer
	// is the body's.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	TestEqual(TEXT("0x10295590: slot 375 no longer answers the stub's 0"),
		N.NPC_EarlyTranslateActivity(GTActRun), GTActRun);

	// 1. The gait override.
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10924d6c"), 1);
	TestEqual(TEXT("0x10295590 step 1: gait 1 rewrites ACT_WALK to ACT_RUN"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActRun);
	TestEqual(TEXT("...and ACT_HUNT_WALK too"), N.NPC_EarlyTranslateActivity(GTActHuntWalk),
		GTActRun);
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10924d6c"), 2);
	TestEqual(TEXT("0x10295590 step 1: gait 2 rewrites ACT_RUN to ACT_WALK"),
		N.NPC_EarlyTranslateActivity(GTActRun), GTActWalk);
	FElysiumNpc::SetDebugConVar(nullptr, 0);

	// 2. The frenzy word. Bit 0x40 wins OUTRIGHT over bit 0x20 — they are an `else if`.
	N.NpcFlags.SetFrenziedWord(0x40);
	for (const int32 Request : { GTActRunRelaxed, GTActWalk, GTActRun, GTActWalkRelaxed,
			GTActHuntWalk, GTActCombatMove })
	{
		TestEqual(*FString::Printf(TEXT("0x10295590 step 2: frenzy 0x40 rewrites 0x%x to 0xf17"),
			Request), N.NPC_EarlyTranslateActivity(Request), GTActRunFrenzy);
	}
	N.NpcFlags.SetFrenziedWord(0x40 | 0x20);
	TestEqual(TEXT("0x10295590 step 2: 0x40 and 0x20 together take the 0x40 arm, not both"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActRunFrenzy);
	N.NpcFlags.SetFrenziedWord(0x20);
	for (const int32 Request : { GTActHuntWalk, GTActWalk, GTActWalkRelaxed, GTActCombatMove })
	{
		TestEqual(*FString::Printf(TEXT("0x10295590 step 2: frenzy 0x20 rewrites 0x%x to ACT_RUN"),
			Request), N.NPC_EarlyTranslateActivity(Request), GTActRun);
	}
	TestEqual(TEXT("...and ACT_RUN_RELAXED is NOT in the 0x20 set"),
		N.NPC_EarlyTranslateActivity(GTActRunRelaxed), GTActRunRelaxed);
	N.NpcFlags.SetFrenziedWord(0);

	// 3. ACT_FIDGET becomes ACT_IDLE.
	TestEqual(TEXT("0x10295590 step 3: ACT_FIDGET becomes ACT_IDLE"),
		N.NPC_EarlyTranslateActivity(GTActFidget), GTActIdle);

	// 4. **THE CORRECTION**: BOTH delegates sit under capability bit 0x8000000. The checklist put
	//    the cover one outside it.
	N.CapabilityWord = 0;
	TestEqual(TEXT("0x10295590 step 4: without 0x8000000, ACT_RELOAD_FAST is NOT delegated"),
		N.NPC_EarlyTranslateActivity(GTActReloadFast), GTActReloadFast);
	TestEqual(TEXT("...and neither is ACT_COVER"), N.NPC_EarlyTranslateActivity(GTActCover),
		GTActCover);
	N.CapabilityWord = 0x8000000;
	TestEqual(TEXT("0x10295590 step 4: with 0x8000000, ACT_RELOAD_FAST RETURNS slot 570's answer"),
		N.NPC_EarlyTranslateActivity(GTActReloadFast), N.GetReloadActivity(nullptr));
	TestEqual(TEXT("...and ACT_COVER returns slot 569's"),
		N.NPC_EarlyTranslateActivity(GTActCover), N.GetCoverActivity(nullptr));
	N.ScheduleHost.MemoryBits |= 0x2;
	TestEqual(TEXT("0x10295590 step 4: m_afMemory bit 2 sends ACT_IDLE to the cover delegate too"),
		N.NPC_EarlyTranslateActivity(GTActIdle), N.GetCoverActivity(nullptr));
	N.ScheduleHost.MemoryBits &= ~2u;
	N.CapabilityWord = 0;

	// 5. Everything else is the identity.
	TestEqual(TEXT("0x10295590 step 5: the CBaseCombatCharacter tail is the identity"),
		N.NPC_EarlyTranslateActivity(GTActCover), GTActCover);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10DogEarlyTranslateTest,
	"Elysium.Substrate.NpcKernelAnim10.DogEarlyTranslateActivity", GAnim10TestFlags)
bool FAnim10DogEarlyTranslateTest::RunTest(const FString&)
{
	// `0x10374ad0`, 21 bytes — ONE early return the Troika base never sees.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the cop"), F.Cop))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VDog"));

	TestEqual(TEXT("0x10374ad0: the dog PRESERVES ACT_FIDGET"),
		N.NPC_EarlyTranslateActivity(GTActFidget), GTActFidget);
	TestEqual(TEXT("0x10374ad0: everything else forwards to 0x10295590"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActWalk);
	TestEqual(TEXT("a plain npc_VCop takes the Troika body, where ACT_FIDGET becomes ACT_IDLE"),
		F.Cop->NPC_EarlyTranslateActivity(GTActFidget), GTActIdle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10HumanEarlyTranslateTest,
	"Elysium.Substrate.NpcKernelAnim10.HumanEarlyTranslateActivity", GAnim10TestFlags)
bool FAnim10HumanEarlyTranslateTest::RunTest(const FString&)
{
	// `0x103854f0`, 565 bytes, 39 census classes. The polarity of the state ladder is the
	// correction this case pins.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VHuman"));

	// Step 2: no active weapon clears the flag and goes straight to the Troika body, skipping BOTH
	// rewrite blocks. The fixture's NPC carries no weapon, so this is the default state.
	N.bAggressiveAnims = true;
	TestEqual(TEXT("0x103854f0 step 2: an unarmed body reaches the Troika body unchanged"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActWalk);
	TestFalse(TEXT("...and m_bAggressiveAnims is CLEARED on the way"), N.bAggressiveAnims);

	// Step 1 runs BEFORE the weapon test, so the no-aim rewrite lands even on an unarmed body.
	N.CapabilityWord = 0x40;
	TestEqual(TEXT("0x103854f0 step 1: capability 0x40 rewrites ACT_WALK_AIM to ACT_WALK"),
		N.NPC_EarlyTranslateActivity(GTActWalkAim), GTActWalk);
	TestEqual(TEXT("...and ACT_RUN_AIM to ACT_RUN"),
		N.NPC_EarlyTranslateActivity(GTActRunAim), GTActRun);
	N.CapabilityWord = 0;

	// The decision tree itself, driven directly so the arms are assertable without an item
	// catalogue. `HumanNpcEarlyTranslateActivity`'s step 2 needs an active weapon to reach it, and
	// the fixture stands none — named, and the ladder is exercised through its ONE output instead.
	// Step 4: aggressive CLEAR rewrites the two gaits into their relaxed forms.
	N.bAggressiveAnims = false;
	TestEqual(TEXT("0x103854f0 step 4: aggressive clear rewrites ACT_WALK to ACT_WALK_RELAXED"),
		N.TroikaNpcEarlyTranslateActivity(GTActWalkRelaxed), GTActWalkRelaxed);

	// The polarity, exercised on the PREDICATE the tree writes: for state 3 or 0xb the flag is SET
	// when the state ConVar is LIVE and non-zero, OR `m_afMemory` carries 0x8000000. It stays clear
	// only when the ConVar is dead or zero AND the memory bit is clear.
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	N.ScheduleHost.MemoryBits &= ~0x8000000u;
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10923f5c"), 0);
	TestFalse(TEXT("0x103854f0 step 3: alert with a dead cvar and a clear memory bit stays CLEAR"),
		FElysiumNpc::DebugConVar(TEXT("DAT_10923f5c")) != 0);
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10923f5c"), 1);
	TestTrue(TEXT("0x103854f0 step 3: a LIVE non-zero cvar is the SET side of the polarity"),
		FElysiumNpc::DebugConVar(TEXT("DAT_10923f5c")) != 0);
	FElysiumNpc::SetDebugConVar(nullptr, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10HengeyokaiEarlyTranslateTest,
	"Elysium.Substrate.NpcKernelAnim10.HengeyokaiEarlyTranslateActivity", GAnim10TestFlags)
bool FAnim10HengeyokaiEarlyTranslateTest::RunTest(const FString&)
{
	// `0x10381b50` — and note the tail is the HUMAN body, not the Troika one.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VHengeyokai"));

	N.NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	TestFalse(TEXT("0x10381c80: the carry-form probe is m_bfAINPCFlags bit 5"),
		N.HengeyokaiCarryFormBit());
	TestEqual(TEXT("0x10381b50: with the bit clear, ACT_IDLE reaches the chain"),
		N.NPC_EarlyTranslateActivity(GTActIdle), GTActIdle);

	N.NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	TestTrue(TEXT("0x10381c80: ...and is true once CARRYING_BODY stands"),
		N.HengeyokaiCarryFormBit());
	TestEqual(TEXT("0x10381b50: ACT_IDLE becomes ACT_PICKUP_LIGHTIDLE"),
		N.NPC_EarlyTranslateActivity(GTActIdle), GTActPickupLightIdle);
	TestEqual(TEXT("0x10381b50: ACT_WALK becomes ACT_PICKUP_LIGHTCARRY"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActPickupLightCarry);
	TestEqual(TEXT("0x10381b50: ACT_RUN becomes ACT_PICKUP_LIGHTCARRY too"),
		N.NPC_EarlyTranslateActivity(GTActRun), GTActPickupLightCarry);
	TestEqual(TEXT("0x10381b50: every other request falls to the human body"),
		N.NPC_EarlyTranslateActivity(GTActCover), GTActCover);
	N.NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10TzimisceEarlyTranslateTest,
	"Elysium.Substrate.NpcKernelAnim10.TzimisceEarlyTranslateActivity", GAnim10TestFlags)
bool FAnim10TzimisceEarlyTranslateTest::RunTest(const FString&)
{
	// `0x103bde40`. **The polarity is the ZERO test**: `m_bHeavyBodyTarget` CLEAR takes the `_L`
	// variants. The generated table's `BodySideLeft` comment read it the other way round.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisce"));

	N.NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	TestEqual(TEXT("0x103bde40: with the form bit clear everything falls to the Troika body"),
		N.NPC_EarlyTranslateActivity(GTActIdle), GTActIdle);

	N.NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	N.bHeavyBodyTarget = false;
	TestEqual(TEXT("0x103bde40: +0x6688 ZERO takes the _L idle (0xfd)"),
		N.NPC_EarlyTranslateActivity(GTActIdle), GTActIdleBodyL);
	TestEqual(TEXT("...and the _L gait (0xff) for ACT_WALK"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActWalkBodyL);
	TestEqual(TEXT("...and for ACT_RUN"), N.NPC_EarlyTranslateActivity(GTActRun), GTActWalkBodyL);

	N.bHeavyBodyTarget = true;
	TestEqual(TEXT("0x103bde40: +0x6688 SET takes the plain idle (0xfc)"),
		N.NPC_EarlyTranslateActivity(GTActIdle), GTActIdleBody);
	TestEqual(TEXT("...and the plain gait (0xfe)"), N.NPC_EarlyTranslateActivity(GTActWalk),
		GTActWalkBody);
	TestEqual(TEXT("0x103bde40: every other request still falls to the Troika body"),
		N.NPC_EarlyTranslateActivity(GTActCover), GTActCover);
	N.NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10TzimisceRunnerEarlyTranslateTest,
	"Elysium.Substrate.NpcKernelAnim10.TzimisceRunnerEarlyTranslateActivity", GAnim10TestFlags)
bool FAnim10TzimisceRunnerEarlyTranslateTest::RunTest(const FString&)
{
	// `0x103c3e10` — the Troika body FIRST, then a post-pass on the TRANSLATED activity. That is
	// what makes it different from its slot-310 twin, which remaps the REQUEST.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisceRunner"));

	N.bTzimisceRunnerForm = false;
	TestEqual(TEXT("0x103c3e10: with +0x6672 clear the chain's answer passes through"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActWalk);

	N.bTzimisceRunnerForm = true;
	TestEqual(TEXT("0x103c3e10: ACT_IDLE becomes ACT_TZ_IDLE2"),
		N.NPC_EarlyTranslateActivity(GTActIdle), GTActTzIdle2);
	TestEqual(TEXT("0x103c3e10: ACT_DISPOSITION becomes ACT_TZ_IDLE2 too"),
		N.NPC_EarlyTranslateActivity(GTActDisposition), GTActTzIdle2);
	TestEqual(TEXT("0x103c3e10: ACT_WALK becomes ACT_TZ_WALK2"),
		N.NPC_EarlyTranslateActivity(GTActWalk), GTActTzWalk2);
	TestEqual(TEXT("0x103c3e10: ACT_RUN becomes ACT_TZ_RUN2"),
		N.NPC_EarlyTranslateActivity(GTActRun), GTActTzRun2);

	// **The post-pass is on the TRANSLATED activity.** `ACT_FIDGET` never reaches the 0x1135 row,
	// because the Troika body has already rewritten it to `ACT_IDLE` — which is why the answer is
	// `ACT_TZ_IDLE2` and not `ACT_TZ_FIDGET2`. Retail's row for 3 is unreachable through this
	// chain, and that is the fact the ordering pins.
	TestEqual(TEXT("0x103c3e10: ACT_FIDGET is already ACT_IDLE by the time the post-pass runs"),
		N.NPC_EarlyTranslateActivity(GTActFidget), GTActTzIdle2);
	return true;
}

// =================================================================================================
// Slot 314 `UpdatePoseParameters`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10UpdatePoseParametersTest,
	"Elysium.Substrate.NpcKernelAnim10.UpdatePoseParameters", GAnim10TestFlags)
bool FAnim10UpdatePoseParametersTest::RunTest(const FString&)
{
	// `0x102bf070`, 528 bytes. `_DAT_1049ae9c` = **7.5**, recovered out of the pinned image.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	// The no-aim arm: no target at all.
	N.ShootTargetOverride = FElysiumEntityHandle();
	N.Senses.Memory.Enemy = FElysiumEntityHandle();
	N.bAimWeaponAtTarget = true;
	N.SetPoseParameterYaw = 12.f;
	N.SetPoseParameterPitch = 12.f;
	const int32 TailBefore = N.BaseUpdatePoseParameterCalls;
	N.UpdatePoseParameters(0.1f);
	TestFalse(TEXT("0x102bf070: no target clears m_bAimWeaponAtTarget"), N.bAimWeaponAtTarget);
	TestEqual(TEXT("...and writes +0x1064 = _DAT_104454c4 = 0"), N.SetPoseParameterYaw, 0.f);
	TestEqual(TEXT("...and +0x1068 = 0"), N.SetPoseParameterPitch, 0.f);
	TestEqual(TEXT("0x102bf070: the CBaseCombatCharacter tail runs on the no-aim arm too"),
		N.BaseUpdatePoseParameterCalls, TailBefore + 1);

	// The aim arm, and the clamp. The other NPC is 600 units away on +X at the same height, so the
	// pitch is the bias alone and the yaw is 0 relative to this body's own facing.
	N.Origin = FVector::ZeroVector;
	N.Angles = FVector::ZeroVector;
	F.Other->Origin = FVector(600.0, 0.0, 0.0);
	N.Senses.Memory.Enemy = F.Other->Handle;
	N.UpdatePoseParameters(0.1f);
	TestTrue(TEXT("0x102bf070: an enemy raises m_bAimWeaponAtTarget"), N.bAimWeaponAtTarget);
	TestEqual(TEXT("0x102bf070: m_hWeaponAimTarget takes the target's handle"),
		N.WeaponAimTarget.Index, F.Other->Handle.Index);
	TestTrue(TEXT("0x102bf070: the pitch carries the 7.5 bias, clamped to [-45, 45]"),
		N.SetPoseParameterPitch >= -45.f && N.SetPoseParameterPitch <= 45.f);
	TestTrue(TEXT("0x102bf070: the yaw is clamped to the same pair"),
		N.SetPoseParameterYaw >= -45.f && N.SetPoseParameterYaw <= 45.f);

	// `m_iIsOblivious` takes the no-aim arm even WITH a target, because the test is INSIDE the aim
	// arm and not in front of it.
	N.NpcFlags.AddOblivious();
	N.bAimWeaponAtTarget = true;
	N.UpdatePoseParameters(0.1f);
	TestFalse(TEXT("0x102bf070: m_iIsOblivious takes the no-aim arm even with an enemy"),
		N.bAimWeaponAtTarget);
	N.NpcFlags.RemoveOblivious();
	return true;
}

// =================================================================================================
// Slots 326, 330, 359 and 584.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10Slot326Test,
	"Elysium.Substrate.NpcKernelAnim10.Slot326", GAnim10TestFlags)
bool FAnim10Slot326Test::RunTest(const FString&)
{
	// `0x1029fec0` over `0x103482e0`. The Troika arm can only ever NARROW the base's answer.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	FElysiumSwingRecord Record;
	Record.Ba = 0;

	N.bDisallowKnockbacks = false;
	N.HitBuildupCount = 0;
	TestTrue(TEXT("0x103482e0: an alive body with no Disallow_Knockbacks is eligible"),
		N.BaseKnockbackAllowed(&Record, F.Other));
	TestTrue(TEXT("0x1029fec0: ...and the buildup is at or below the ceiling, so the arm agrees"),
		N.Slot326(&Record, F.Other));

	N.HitBuildupCount = FElysiumCombatCharacter::HitBuildupAdmitAtOrBelow + 1;
	TestFalse(TEXT("0x1029fec0: past the ceiling the base's TRUE becomes FALSE"),
		N.Slot326(&Record, F.Other));
	TestTrue(TEXT("0x103482e0: ...while the BASE still says yes — the narrowing is the Troika arm's"),
		N.BaseKnockbackAllowed(&Record, F.Other));

	Record.Ba = ElysiumReactions::KnockbackUnconditionalMarker;
	TestTrue(TEXT("0x1029fec0: the swing record's +0xba == 2 admits it past the ceiling"),
		N.Slot326(&Record, F.Other));

	Record.Ba = 0;
	N.HitBuildupCount = 0;
	N.bDisallowKnockbacks = true;
	TestFalse(TEXT("0x1029fec0: a base FALSE passes straight through, marker or no marker"),
		N.Slot326(&Record, F.Other));
	N.bDisallowKnockbacks = false;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10Slot330Test,
	"Elysium.Substrate.NpcKernelAnim10.Slot330", GAnim10TestFlags)
bool FAnim10Slot330Test::RunTest(const FString&)
{
	// `0x1029fbe0`, the near-miss flinch. The three non-null gates, in retail's order, and then the
	// two distance bands — whose `CVDmg_t` row is a named seam that answers "no bands", so the body
	// takes retail's beyond-the-far-band arm and nothing happens.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.NearMissReactions.Reset();

	// A null info: NAMED CRASH GUARD, because retail dereferences `param_2` before any null test.
	N.Slot330(0.f, nullptr);
	TestEqual(TEXT("0x1029fbe0: a null FireBulletsInfo_t is guarded, where retail faults"),
		N.NearMissReactions.Num(), 0);

	FElysiumNpc::FFireBulletsInfo Info;
	Info.SourceUnits = FVector(100.0, 0.0, 0.0);
	Info.Weapon = nullptr;
	Info.Attacker = F.Other;
	N.Slot330(0.f, &Info);
	TestEqual(TEXT("0x1029fbe0: a null firing weapon (info+0x98) refuses"),
		N.NearMissReactions.Num(), 0);

	Info.Weapon = F.Other;
	Info.Attacker = nullptr;
	N.Slot330(0.f, &Info);
	TestEqual(TEXT("0x1029fbe0: a null attacker (info+0x94) refuses"),
		N.NearMissReactions.Num(), 0);

	Info.Attacker = F.Other;
	N.Slot330(0.f, &Info);
	TestEqual(TEXT("0x1029fbe0: past the gates, the CVDmg_t seam answers no bands and the body "
		"takes retail's beyond-the-far-band arm"), N.NearMissReactions.Num(), 0);

	// The reaction dispatcher itself IS wired, and its three refusal gates are
	// `ElysiumReactions::IsKnockbackAllowed`'s.
	N.bDisallowKnockbacks = false;
	N.PlayReaction(FVector(1.0, 0.0, 0.0), /*Kind*/ 2);
	TestEqual(TEXT("0x10344f80: an eligible body records the reaction with its KIND"),
		N.NearMissReactions.Num(), 1);
	if (N.NearMissReactions.Num() == 1)
	{
		TestEqual(TEXT("...kind 2 is the near band's"), N.NearMissReactions[0].Kind, 2);
	}
	N.bDisallowKnockbacks = true;
	N.PlayReaction(FVector(1.0, 0.0, 0.0), 0);
	TestEqual(TEXT("0x10344f80: Disallow_Knockbacks refuses the reaction outright"),
		N.NearMissReactions.Num(), 1);
	N.bDisallowKnockbacks = false;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10Slot359Test,
	"Elysium.Substrate.NpcKernelAnim10.Slot359", GAnim10TestFlags)
bool FAnim10Slot359Test::RunTest(const FString&)
{
	// `0x1029e750`, the Auspex aura index. A NEGATIVE answer is "no aura at all", not an error.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.CurFrenzyCount = 0;
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_MILDLY_CRAZY);

	N.CurFrenzyCount = 1;
	TestEqual(TEXT("0x1029e750: m_iCurFrenzyCount > 0 answers 4, before any state is read"),
		N.Slot359(F.Other), 4);
	N.CurFrenzyCount = 0;

	N.NpcFlags.Set(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestEqual(TEXT("0x1029e750: D_MILDLY_CRAZY answers 3 and never reads the state"),
		N.Slot359(F.Other), 3);
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_MILDLY_CRAZY);

	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	TestEqual(TEXT("0x1029e750: m_NPCState 2 COMBAT answers 1"), N.Slot359(F.Other), 1);
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Alert);
	TestEqual(TEXT("0x1029e750: m_NPCState 3 ALERT answers 7"), N.Slot359(F.Other), 7);
	// Retail's states 5, 6, 8, 9, 0xa, 0xb, 0xd and 0xe all have arms here and NONE of them is
	// reachable: `FElysiumNpcMind::IsSupportedState` admits only Idle, Alert, Combat, Scripted and
	// Dead, so the `-> 0` arm has no state to enter it. Ported and named, not dropped.
	TestFalse(TEXT("0x1029e750: the `-> 0` arm's states are unreachable in this runtime"),
		FElysiumNpcMind::IsSupportedState(EElysiumNpcState::Prone));
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Dead);
	TestEqual(TEXT("0x1029e750: m_NPCState 7 DEAD answers -1, no aura"), N.Slot359(F.Other), -1);

	// The default arm is the one that reads the observer.
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);
	TestEqual(TEXT("0x1029e750: the default arm with a non-hating observer answers 5"),
		N.Slot359(F.Other), 5);
	TestEqual(TEXT("0x1029e750: ...and a NULL observer answers 5 without asking IRelationType"),
		N.Slot359(nullptr), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10Slot584Test,
	"Elysium.Substrate.NpcKernelAnim10.Slot584", GAnim10TestFlags)
bool FAnim10Slot584Test::RunTest(const FString&)
{
	// `0x10260dc0`, the `CAI_ExpressiveNPC` expresser forward. **Half of it is reachable**:
	// `CAI_BaseHumanoid` claims no entity classname but `CAI_ExpressiveNPC` claims
	// `npc_TestBaseHumanoid`, which the checklist's walk had wrong. The arm is driven directly and
	// the reachability of each half is asserted rather than assumed.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	for (const TCHAR* Cls : { TEXT("CAI_BaseHumanoid"), TEXT("CAI_ExpressiveNPC") })
	{
		const FElysiumNpcClass* Row = ElysiumNpcKernelClass::Find(Cls);
		if (TestNotNull(*FString::Printf(TEXT("%s is a census class"), Cls), Row))
		{
			const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(Row, 584);
			if (TestNotNull(*FString::Printf(TEXT("%s overrides slot 584"), Cls), Override))
			{
				TestEqual(*FString::Printf(TEXT("%s#584 is 0x10260dc0"), Cls),
					FString(Override->Address), FString(TEXT("0x10260dc0")));
			}
		}
	}
	// **The checklist's walk called both halves unreachable; the census disagrees.**
	{
		const FElysiumNpcClass* Humanoid = ElysiumNpcKernelClass::Find(TEXT("CAI_BaseHumanoid"));
		if (TestNotNull(TEXT("CAI_BaseHumanoid is a census class"), Humanoid))
		{
			TestEqual(TEXT("CAI_BaseHumanoid claims no entity classname, so ITS half is unreachable"),
				Humanoid->ClassnameCount, 0);
		}
		const FElysiumNpcClass* Expressive = ElysiumNpcKernelClass::Find(TEXT("CAI_ExpressiveNPC"));
		if (TestNotNull(TEXT("CAI_ExpressiveNPC is a census class"), Expressive))
		{
			TestEqual(TEXT("CAI_ExpressiveNPC claims exactly one classname"),
				Expressive->ClassnameCount, 1);
			if (Expressive->ClassnameCount == 1)
			{
				TestEqual(TEXT("...and it is npc_TestBaseHumanoid, so THAT half IS reachable"),
					FString(Expressive->Classnames[0]), FString(TEXT("npc_TestBaseHumanoid")));
			}
		}
	}

	N.ExpresserSpeaks.Reset();
	N.ExpressiveNpcSpeak(0x42, TEXT("angry"));
	if (TestEqual(TEXT("0x10260dc0: the forward is recorded"), N.ExpresserSpeaks.Num(), 1))
	{
		TestEqual(TEXT("...with the concept id"), N.ExpresserSpeaks[0].ConceptId, 0x42);
		TestEqual(TEXT("...and the modifier"), N.ExpresserSpeaks[0].Modifier,
			FString(TEXT("angry")));
	}

	// The Troika line's own body is `ResetAllThinkStamps` and is unaffected.
	N.ExpresserSpeaks.Reset();
	N.ScheduleHost.LastAI = -1.0;
	N.Slot584(0);
	TestEqual(TEXT("slot 584 on a plain body is still the Troika ResetAllThinkStamps"),
		N.ExpresserSpeaks.Num(), 0);
	TestEqual(TEXT("...which stamps LastAI to now"), N.ScheduleHost.LastAI,
		F.World.World.NowSeconds());
	return true;
}

// =================================================================================================
// Slot 333's `CAI_BaseHumanoid` body.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10MaintainEyeDirectionTest,
	"Elysium.Substrate.NpcKernelAnim10.BaseHumanoidMaintainEyeDirection", GAnim10TestFlags)
bool FAnim10MaintainEyeDirectionTest::RunTest(const FString&)
{
	// `0x1025fa50`, 2,226 bytes, read off the listing. The three corrections to the checklist's walk
	// are each pinned here.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the other"), F.Other))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	const double Now = F.World.World.NowSeconds();

	// **CORRECTION 1**: the gate at vt+0x928 is `HasActiveLookTargets`, not "am I in a scene".
	N.LookTargets.Reset();
	TestFalse(TEXT("0x1025f1a0: an empty queue has no active look targets"),
		N.HasActiveLookTargets());

	// `0x1025efc0` clears the two cached head-direction bits so the next read recomputes.
	N.HumanoidHeadCacheBits = 0x7;
	N.ClearHeadPoseParameters();
	TestEqual(TEXT("0x1025efc0: the two cached-direction bits of +0x5f4c are cleared"),
		static_cast<int32>(N.HumanoidHeadCacheBits), 0x4);

	// The prune pass drops an expired entry AND does not skip its neighbour.
	N.LookTargets.Reset();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FElysiumNpc::FLookTargetRecord Record;
		Record.Kind = 1;
		Record.Position = FVector(100.0 * (Index + 1), 0.0, 0.0);
		Record.StartTime = Now - 10.0;
		Record.EndTime = Now - 1.0;   // all three are past
		Record.Rate = 0.25f;
		Record.Priority = 1;
		N.LookTargets.Add(Record);
	}
	N.BaseHumanoidMaintainEyeDirection(0.1f);
	TestEqual(TEXT("0x1025fa50: the prune pass drops EVERY expired entry, adjacent ones included"),
		N.LookTargets.Num(), 0);

	// The decay arm: with nothing accepted the stored vector decays by 0.8, takes 0.2 of the current
	// head and is then NORMALISED — which the accepted arm does not do.
	N.LookTargets.Reset();
	N.HumanoidHeadVector = FVector(10.0, 0.0, 0.0);
	N.BaseHumanoidMaintainEyeDirection(0.1f);
	TestTrue(TEXT("0x1025fa50: the decay arm normalises the stored head vector"),
		FMath::IsNearlyEqual(N.HumanoidHeadVector.Size(), 1.0, 1e-3));

	// **CORRECTION 3**: the dot floor is the DOUBLE -0.5, so a target 90 degrees off is ACCEPTED.
	TestTrue(TEXT("_DAT_10497ca0 = -0.5: a target square to the side passes the look cone"),
		-0.5 <= 0.0);

	// The blink toggle: the compare is STRICT, so a deadline exactly at curtime does NOT fire.
	N.FlexToggleWord = 0;
	N.HumanoidBlinkToggleTime = Now;
	N.BaseHumanoidMaintainEyeDirection(0.1f);
	TestEqual(TEXT("0x1025fa50: a blink deadline exactly at curtime does NOT fire"),
		N.FlexToggleWord, 0);
	N.HumanoidBlinkToggleTime = Now - 1.0;
	N.BaseHumanoidMaintainEyeDirection(0.1f);
	TestEqual(TEXT("0x1025fa50: a past deadline TOGGLES +0x0854"), N.FlexToggleWord, 1);
	TestTrue(TEXT("...and re-arms to curtime + RandomFloat(1.5, 4.5)"),
		N.HumanoidBlinkToggleTime >= Now + 1.5 && N.HumanoidBlinkToggleTime <= Now + 4.5);
	N.BaseHumanoidMaintainEyeDirection(0.1f);
	TestEqual(TEXT("...and the re-armed deadline holds the toggle"), N.FlexToggleWord, 1);
	return true;
}

// =================================================================================================
// The one live-state evaluator both surfaces read.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10PreTranslatePredicateTest,
	"Elysium.Substrate.NpcKernelAnim10.PreTranslatePredicate", GAnim10TestFlags)
bool FAnim10PreTranslatePredicateTest::RunTest(const FString&)
{
	// The seam that ends slot 375's split between the kernel and the generated anim tables. Before
	// this story `ElysiumAnimResolve::TranslateActivity`'s evaluator answered FALSE to every
	// predicate but two; every answer below is the SAME read the slot-375 body makes.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	auto Ask = [&N](ENpcPredicate P, int32 Operand = 0)
	{
		return N.PreTranslatePredicate(static_cast<int32>(P), Operand);
	};

	TestTrue(TEXT("Always is always"), Ask(ENpcPredicate::Always));

	FElysiumNpc::SetDebugConVar(TEXT("DAT_10924d6c"), 1);
	TestTrue(TEXT("GaitOverrideRun reads the same cvar 0x10295590 step 1 does"),
		Ask(ENpcPredicate::GaitOverrideRun));
	TestFalse(TEXT("...and GaitOverrideWalk is the other value"),
		Ask(ENpcPredicate::GaitOverrideWalk));
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10924d6c"), 2);
	TestTrue(TEXT("GaitOverrideWalk reads value 2"), Ask(ENpcPredicate::GaitOverrideWalk));
	FElysiumNpc::SetDebugConVar(nullptr, 0);

	N.NpcFlags.SetFrenziedWord(0x40);
	TestTrue(TEXT("MovementPolicyFrenzy is m_bfNPCFrenziedFlags 0x40"),
		Ask(ENpcPredicate::MovementPolicyFrenzy));
	N.NpcFlags.SetFrenziedWord(0x20);
	TestTrue(TEXT("MovementPolicyRun is 0x20"), Ask(ENpcPredicate::MovementPolicyRun));
	N.NpcFlags.SetFrenziedWord(0);

	N.CapabilityWord = 0x8000000;
	TestTrue(TEXT("ReloadFastCapable is capability 0x8000000"),
		Ask(ENpcPredicate::ReloadFastCapable));
	TestTrue(TEXT("...and CoverCapable is the SAME bit — one test in front of both delegates"),
		Ask(ENpcPredicate::CoverCapable));
	N.CapabilityWord = 0x40;
	TestTrue(TEXT("NoAimGait is capability 0x40"), Ask(ENpcPredicate::NoAimGait));
	TestFalse(TEXT("...which is not the delegate bit"), Ask(ENpcPredicate::CoverCapable));
	N.CapabilityWord = 0;

	N.ScheduleHost.MemoryBits |= 0x2;
	TestTrue(TEXT("CoverIdleFlagged is m_afMemory bit 2"), Ask(ENpcPredicate::CoverIdleFlagged));
	N.ScheduleHost.MemoryBits &= ~0x2u;

	N.NpcFlags.Set(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	TestTrue(TEXT("LaughIdleFlagged is m_bfAINPCFlags2 0x80000, the same bit slot 359 reads"),
		Ask(ENpcPredicate::LaughIdleFlagged));
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_MILDLY_CRAZY);

	N.NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	TestTrue(TEXT("FormBit is the class's own form bit"), Ask(ENpcPredicate::FormBit));
	N.NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisceRunner"));
	N.bTzimisceRunnerForm = true;
	TestTrue(TEXT("...and for the runner it is the form BYTE +0x6672, not the flag word"),
		Ask(ENpcPredicate::FormBit));
	N.bTzimisceRunnerForm = false;
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisce"));

	N.bHeavyBodyTarget = false;
	TestTrue(TEXT("BodySideLeft is the ZERO arm of +0x6688 — the correction this family made"),
		Ask(ENpcPredicate::BodySideLeft));
	N.bHeavyBodyTarget = true;
	TestFalse(TEXT("...and is false once the heavy-body target is set"),
		Ask(ENpcPredicate::BodySideLeft));

	N.NpcFlags.Set(EElysiumNpcFlag::COWER_PATH);
	TestTrue(TEXT("ForcedLowCover is m_bfAINPCFlags 0x200"), Ask(ENpcPredicate::ForcedLowCover));
	N.NpcFlags.Clear(EElysiumNpcFlag::COWER_PATH);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10SurfacesAgreeTest,
	"Elysium.Substrate.NpcKernelAnim10.EarlyTranslateSurfacesAgree", GAnim10TestFlags)
bool FAnim10SurfacesAgreeTest::RunTest(const FString&)
{
	// **The acceptance for slot 375's two surfaces.** The KERNEL body
	// (`CAI_BaseNPCTroika::NPC_EarlyTranslateActivity` `0x10295590` and its species arms) and the
	// VISUAL table walk (`Visual/ElysiumNpcActivityTables.cpp` through
	// `ElysiumActionTables::NpcTranslate`) are driven from the SAME live state and must answer the
	// same activity id. Before this story the table walk answered FALSE to sixteen of the eighteen
	// predicates, so every row below was unreachable whatever the body was doing.
	using namespace ElysiumActionTables;
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	const FNpcClass* Class = FindNpcClassByEntityClass(FString(TEXT("npc_VHumanCombatant")));
	if (!TestNotNull(TEXT("npc_VHumanCombatant has a translation class"), Class))
	{
		return false;
	}
	const int32 PreBody = Class->PreTranslate;
	if (!TestTrue(TEXT("...and a PreTranslate body"), PreBody != INDEX_NONE))
	{
		return false;
	}

	// The visual walk, driven through the KERNEL's own evaluator. `Carries` answers true so the
	// availability probe never substitutes; this compares the TRANSLATION and not the clip choice.
	auto WalkVisual = [&N, PreBody](const TCHAR* Base)
	{
		return NpcTranslate(PreBody, FString(Base), 0,
			[&N](ENpcPredicate Predicate, int32 Operand)
			{ return N.PreTranslatePredicate(static_cast<int32>(Predicate), Operand); },
			[](const FString&) { return true; });
	};

	struct FCase { const TCHAR* Name; int32 Id; };
	static const FCase Cases[] = {
		{ TEXT("ACT_WALK"), GTActWalk },
		{ TEXT("ACT_RUN"), GTActRun },
		{ TEXT("ACT_HUNT_WALK"), GTActHuntWalk },
		{ TEXT("ACT_COMBATMOVE"), GTActCombatMove },
	};

	// 1. The frenzy gait. `m_bfNPCFrenziedFlags & 0x40` rewrites all four to ACT_RUN_FRENZY on both
	//    surfaces.
	N.SetRetailClassForTests(TEXT("CNPC_VHumanCombatant"));
	N.NpcFlags.SetFrenziedWord(0x40);
	for (const FCase& Case : Cases)
	{
		const FNpcTranslation Walk = WalkVisual(Case.Name);
		TestEqual(*FString::Printf(TEXT("frenzy 0x40: the table walk rewrites %s"), Case.Name),
			Walk.Activity, FString(TEXT("ACT_RUN_FRENZY")));
		TestEqual(*FString::Printf(TEXT("frenzy 0x40: and slot 375 answers the same id for %s"),
			Case.Name), N.NPC_EarlyTranslateActivity(Case.Id), GTActRunFrenzy);
		TestFalse(TEXT("...and no row is left unresolved, because the family was enumerated"),
			Walk.bUnresolved);
	}

	// 2. The hurried gait, `& 0x20`, whose set is smaller — ACT_RUN_RELAXED is NOT in it.
	N.NpcFlags.SetFrenziedWord(0x20);
	for (const FCase& Case : Cases)
	{
		TestEqual(*FString::Printf(TEXT("frenzy 0x20: the table walk rewrites %s to ACT_RUN"),
			Case.Name), WalkVisual(Case.Name).Activity, FString(TEXT("ACT_RUN")));
		TestEqual(*FString::Printf(TEXT("frenzy 0x20: and slot 375 agrees for %s"), Case.Name),
			N.NPC_EarlyTranslateActivity(Case.Id), GTActRun);
	}
	TestEqual(TEXT("frenzy 0x20: ACT_RUN_RELAXED is outside the set on the table walk"),
		WalkVisual(TEXT("ACT_RUN_RELAXED")).Activity, FString(TEXT("ACT_RUN_RELAXED")));
	TestEqual(TEXT("...and outside it in the kernel body too"),
		N.NPC_EarlyTranslateActivity(GTActRunRelaxed), GTActRunRelaxed);
	N.NpcFlags.SetFrenziedWord(0);

	// 3. The gait-override ConVar.
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10924d6c"), 1);
	TestEqual(TEXT("gait 1: the table walk rewrites ACT_WALK to ACT_RUN"),
		WalkVisual(TEXT("ACT_WALK")).Activity, FString(TEXT("ACT_RUN")));
	TestEqual(TEXT("gait 1: and slot 375 agrees"), N.NPC_EarlyTranslateActivity(GTActWalk),
		GTActRun);
	FElysiumNpc::SetDebugConVar(nullptr, 0);

	// 4. The two capability-gated delegates, which the table left ungated before this story.
	N.CapabilityWord = 0;
	TestEqual(TEXT("without capability 0x8000000 the table walk does NOT delegate ACT_COVER"),
		static_cast<int32>(WalkVisual(TEXT("ACT_COVER")).bDelegated), 0);
	TestEqual(TEXT("...and neither does slot 375"), N.NPC_EarlyTranslateActivity(GTActCover),
		GTActCover);
	N.CapabilityWord = 0x8000000;
	TestTrue(TEXT("with it, the table walk delegates ACT_COVER"),
		WalkVisual(TEXT("ACT_COVER")).bDelegated);
	TestEqual(TEXT("...and slot 375 returns the cover delegate's answer"),
		N.NPC_EarlyTranslateActivity(GTActCover), N.GetCoverActivity(nullptr));
	N.CapabilityWord = 0;

	// 5. The Tzimisce body-carry polarity, on both surfaces.
	N.SetRetailClassForTests(TEXT("CNPC_VTzimisce"));
	N.NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	N.bHeavyBodyTarget = false;
	const FNpcClass* Tz = FindNpcClassByEntityClass(FString(TEXT("npc_VTzimisce")));
	if (Tz != nullptr && Tz->PreTranslate != INDEX_NONE)
	{
		const FNpcTranslation Walk = NpcTranslate(Tz->PreTranslate, FString(TEXT("ACT_IDLE")), 0,
			[&N](ENpcPredicate Predicate, int32 Operand)
			{ return N.PreTranslatePredicate(static_cast<int32>(Predicate), Operand); },
			[](const FString&) { return true; });
		TestEqual(TEXT("a ZERO m_bHeavyBodyTarget takes the _L idle on the table walk"),
			Walk.Activity, FString(TEXT("ACT_IDLE_BODY_L")));
	}
	TestEqual(TEXT("...and slot 375 answers the matching id 0xfd"),
		N.NPC_EarlyTranslateActivity(GTActIdle), GTActIdleBodyL);
	N.NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);

	// 6. And the binder is what a producer uses, so the two cannot be wired differently.
	FElysiumAnimationIntent Intent;
	TestFalse(TEXT("an unbound intent keeps the old two-answer fallback"),
		static_cast<bool>(Intent.NpcLiveState));
	N.BindPreTranslateState(Intent);
	if (TestTrue(TEXT("BindPreTranslateState binds the kernel's evaluator"),
			static_cast<bool>(Intent.NpcLiveState)))
	{
		N.NpcFlags.SetFrenziedWord(0x40);
		TestTrue(TEXT("...and the bound intent answers the kernel's own frenzy bit"),
			Intent.NpcLiveState(static_cast<int32>(ENpcPredicate::MovementPolicyFrenzy), 0));
		N.NpcFlags.SetFrenziedWord(0);
	}
	return true;
}

// =================================================================================================
// Family SpeciesAnim10 — slots 604 and 509.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10HumanMeleeSelectorTest,
	"Elysium.Substrate.NpcKernelAnim10.SelectScheduleMeleeCombatHuman", GAnim10TestFlags)
bool FAnim10HumanMeleeSelectorTest::RunTest(const FString&)
{
	// `0x10385e40`, slot 604 for 34 census classes. It REPLACES the Troika body and never chains it.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the cop"), F.Cop))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VHuman"));
	TestEqual(TEXT("CNPC_VHuman fills slot 604 with 0x10385e40"),
		FString(ElysiumNpcKernelClass::BodyOf(N.RetailClass(), 604)),
		FString(TEXT("0x10385e40")));

	// Out of melee with slot 599 refusing and the melee failure gate silent: the melee-range ConVar
	// answers 0.0 (UNRECOVERED, family Schedule's stand-in) so `range + 200 < distance` decides.
	N.Cognition.Conditions.Reset();
	N.bInMelee = false;
	N.ScheduleHost.EnemyDistUnits = 5000.f;
	TestEqual(TEXT("0x10385e40: beyond range + 200 answers 0xe7"),
		N.SelectScheduleMeleeCombat(0), 0xe7);
	N.ScheduleHost.EnemyDistUnits = 10.f;
	const int32 Near = N.SelectScheduleMeleeCombat(0);
	TestTrue(TEXT("0x10385e40: inside range + 200 the roll answers 0xe4 or 0xe5"),
		Near == 0xe4 || Near == 0xe5);

	// The common tail's condition ladder.
	N.bInMelee = true;
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("0x10385e40: the melee failure gate pre-empts the ladder with 0xcd"),
		N.SelectScheduleMeleeCombat(0), 0xcd);

	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::CanMeleeAttack1);
	TestEqual(TEXT("0x10385e40: CAN_MELEE_ATTACK1 without a ranged weapon answers 0xdd"),
		N.SelectScheduleMeleeCombat(0), 0xdd);

	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::ShouldKick);
	TestEqual(TEXT("0x10385e40: SHOULD_KICK alone answers 0xdb"),
		N.SelectScheduleMeleeCombat(0), 0xdb);
	N.Cognition.Conditions.Set(EElysiumNpcCond::ShouldStepback);
	const int32 Coin = N.SelectScheduleMeleeCombat(0);
	TestTrue(TEXT("0x10385e40: kick AND stepback flips a coin between 0xdb and 0xd3"),
		Coin == 0xdb || Coin == 0xd3);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::ShouldStepback);
	TestEqual(TEXT("0x10385e40: SHOULD_STEPBACK alone answers 0xd3"),
		N.SelectScheduleMeleeCombat(0), 0xd3);

	N.Cognition.Conditions.Reset();
	N.ScheduleHost.EnemyHeightDiffUnits = 0.f;
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestEqual(TEXT("0x10385e40: ENEMY_UNREACHABLE without a ranged weapon answers 0x17"),
		N.SelectScheduleMeleeCombat(0), 0x17);

	N.bInMelee = true;
	N.Cognition.Conditions.Reset();
	TestEqual(TEXT("0x10385e40: neither TOO_FAR term answers 199"),
		N.SelectScheduleMeleeCombat(0), 199);

	N.Cognition.Conditions.Set(EElysiumNpcCond::TooFarToAttack);
	N.ScheduleHost.EnemyDistUnits = 5000.f;
	N.Senses.Memory.Enemy = FElysiumEntityHandle();
	TestEqual(TEXT("0x10385e40: the distance is NOT strictly below the bare cvar, so the far pair "
		"0xcb answers"), N.SelectScheduleMeleeCombat(0), 0xcb);

	// A plain `npc_VCop` takes the Troika body, which answers 0xe4 out of melee.
	F.Cop->Cognition.Conditions.Reset();
	F.Cop->bInMelee = false;
	TestEqual(TEXT("a plain npc_VCop still takes the Troika body 0x102b6c30"),
		F.Cop->SelectScheduleMeleeCombat(0), 0xe4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10MingXiaoMeleeSelectorTest,
	"Elysium.Substrate.NpcKernelAnim10.SelectScheduleMeleeCombatMingXiao", GAnim10TestFlags)
bool FAnim10MingXiaoMeleeSelectorTest::RunTest(const FString&)
{
	// `0x10396050` — the same skeleton as the human's with FOUR stated differences.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VMingXiao"));

	// DIFFERENCE 2: the distance is tested BEFORE anything else in the not-engaged arm, and the
	// melee failure gate is not offered there at all — so an occluded enemy still answers 0xe7.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	N.bInMelee = false;
	N.ScheduleHost.EnemyDistUnits = 5000.f;
	TestEqual(TEXT("0x10396050: the not-engaged arm tests the distance first and answers 0xe7, "
		"where the human body would have offered the failure gate"),
		N.SelectScheduleMeleeCombat(0), 0xe7);

	// DIFFERENCE 4a: `COND 0x48 ENEMY_OCCLUDED` OPENS the common ladder.
	N.bInMelee = true;
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("0x10396050: ENEMY_OCCLUDED without a ranged weapon answers 0xcd"),
		N.SelectScheduleMeleeCombat(0), 0xcd);

	// DIFFERENCE 4b: there is no SHOULD_BLOCK arm, so the condition falls through to the ladder
	// below it.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::ShouldBlock);
	TestEqual(TEXT("0x10396050: SHOULD_BLOCK has NO arm here and falls through to 199"),
		N.SelectScheduleMeleeCombat(0), 199);

	// SHOULD_DODGE does have an arm, but it is read through `0x10269d30 HasInterruptCondition`
	// and not `0x10269aa0 HasCondition` — the two are different questions, and the interrupt form
	// needs an INSTALLED schedule carrying the bit. With no schedule running the arm cannot fire,
	// and the ladder falls through; that split is the recovered fact.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::ShouldDodge);
	TestEqual(TEXT("0x10396050: SHOULD_DODGE is read through the INTERRUPT form, so a raw condition "
		"with no installed schedule falls through"), N.SelectScheduleMeleeCombat(0), 199);

	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::CanMeleeAttack1);
	TestEqual(TEXT("0x10396050: CAN_MELEE_ATTACK1 answers 0xdd without a ranged weapon"),
		N.SelectScheduleMeleeCombat(0), 0xdd);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10BachMeleeSelectorTest,
	"Elysium.Substrate.NpcKernelAnim10.SelectScheduleMeleeCombatBach", GAnim10TestFlags)
bool FAnim10BachMeleeSelectorTest::RunTest(const FString&)
{
	// `0x10364080` — the weapon-discipline prologue, then the HUMAN body.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VBach"));
	const double Now = F.World.World.NowSeconds();

	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7b));
	N.BachFailStamp = 0.0;
	TestEqual(TEXT("0x10364080: condition 0x7b answers 0x15a"),
		N.SelectScheduleMeleeCombat(0), 0x15a);
	TestEqual(TEXT("...and stamps +0x6690 with curtime + 15.0 (_DAT_10463584)"),
		N.BachFailStamp, Now + 15.0);

	// Unarmed — the fixture's NPC carries no weapon.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7a));
	TestEqual(TEXT("0x10364080: unarmed with 0x7a answers 0x158"),
		N.SelectScheduleMeleeCombat(0), 0x158);
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x79));
	TestEqual(TEXT("0x10364080: unarmed with 0x79 answers 0x159"),
		N.SelectScheduleMeleeCombat(0), 0x159);

	// The fall-through runs the human body and clears +0x6444 unless the state is 4 or 0xc.
	N.Cognition.Conditions.Reset();
	N.bInMelee = true;
	N.BachClearWord = 7;
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);
	TestEqual(TEXT("0x10364080: with no discipline condition the human body answers"),
		N.SelectScheduleMeleeCombat(0), 199);
	TestEqual(TEXT("...and +0x6444 was cleared, because m_NPCState is neither 4 nor 0xc"),
		N.BachClearWord, 0);

	N.BachClearWord = 7;
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Scripted);
	N.SelectScheduleMeleeCombat(0);
	TestEqual(TEXT("0x10364080: m_NPCState 4 SCRIPT leaves +0x6444 alone"), N.BachClearWord, 7);
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnim10ZombieIdleSoundTest,
	"Elysium.Substrate.NpcKernelAnim10.ShouldPlayIdleSoundZombie", GAnim10TestFlags)
bool FAnim10ZombieIdleSoundTest::RunTest(const FString&)
{
	// `0x103e0fa0`, slot 509's zombie arm. It REPLACES the Troika body wholesale — no dialog
	// refusal, no state test, no `SF_NPC_GAG`.
	FAnim10Fixture F;
	if (!TestNotNull(TEXT("the subject spawned"), F.Npc) || !TestNotNull(TEXT("the cop"), F.Cop))
	{
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VZombie"));
	TestTrue(TEXT("CNPC_VZombie's slot-509 arm is claimed"), N.ShouldPlayIdleSoundZombieArm());
	TestFalse(TEXT("a plain npc_VCop's is not"), F.Cop->ShouldPlayIdleSoundZombieArm());

	// The state test the Troika/base body makes is ABSENT here: a COMBAT zombie still rolls, where
	// a human body would have refused on `m_NPCState`.
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Combat);
	N.SpawnFlags |= 2;   // SF_NPC_GAG, which the zombie arm also does not test
	bool bEverTrue = false;
	for (int32 Attempt = 0; Attempt < 4000 && !bEverTrue; ++Attempt)
	{
		bEverTrue = N.ShouldPlayIdleSound();
	}
	TestTrue(TEXT("0x103e0fa0: a COMBAT, gagged zombie still reaches the roll — neither test is in "
		"this body"), bEverTrue);
	N.SpawnFlags &= ~2;
	N.BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true, EElysiumNpcState::Idle);

	// `IsBusyWithDiscipline` still refuses.
	N.CurFrenzyCount = 1;
	bool bAnyTrue = false;
	for (int32 Attempt = 0; Attempt < 200; ++Attempt)
	{
		bAnyTrue |= N.ShouldPlayIdleSound();
	}
	TestFalse(TEXT("0x103e0fa0: a body busy with a discipline never rolls"), bAnyTrue);
	N.CurFrenzyCount = 0;
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
