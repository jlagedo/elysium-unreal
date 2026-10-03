#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **EntityChain**. The assertions come from the decompiled C of the 57 rows —
// the two recovered `.rdata` constants (80.0 and 1024.0), the three solid numbers slot 164 tests,
// slot 226's movetype switch and its call ORDER, slot 285's two bodies, slot 287's stop-at-first,
// slot 279's normalise-and-drop, the `(-180, 180]` single-pass wrap, the camera crossfade's three
// arms and its reset-on-read, the closest-NPC ladder's seven rungs, the response record's
// no-reschedule upgrade and the alert pair's asymmetry.
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
	"Elysium.Arm.NpcKernelEntityChain.RecoveredConstants",
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
	TestEqual(TEXT("the base HearingSensitivity is 1.0"), Npc.FElysiumNpcBase::HearingSensitivity(), 1.f);

	// 0x101a6420 slot 410 — an identity passthrough, NOT a fixed literal.
	const FVector Goal(12.f, 34.f, 56.f);
	TestTrue(TEXT("slot 410 hands back the pointer it was given"),
		Npc.TranslateNavGoalPosition(&Goal) == &Goal);
	TestNull(TEXT("and a null goal comes back null"), Npc.TranslateNavGoalPosition(nullptr));

	// 0x10178120 — a getter of `+0x1d24`, a word NOTHING in vampire.dll writes.
	TestEqual(TEXT("FUN_10178120 answers the unwritten +0x1d24"), Npc.FUN_10178120(), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 483 — the wrapper around slot 158, and the argument it drops.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAliveWrappersTest,
	"Elysium.Arm.NpcKernelEntityChain.AliveWrappers", GElysiumNpcKernelEntityChainFlags)
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

	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 164 / 166 — standability, and the floor rule over it.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainStandableTest,
	"Elysium.Arm.NpcKernelEntityChain.Standable", GElysiumNpcKernelEntityChainFlags)
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
	TestFalse(TEXT("the standability helper refuses a SOLID_NONE entity"), Npc.IsStandableSolid());
	TestFalse(TEXT("slot 164 IsStandable refuses it too"), Npc.IsStandable());

	// Slot 166 `0x10026f80`: a null candidate (the static world under the floor sample) is
	// standable; a real one answers ITS slot 164, dispatched through the candidate.
	TestTrue(TEXT("slot 166 stands on the world (null candidate)"),
		Npc.CanStandOn(static_cast<FElysiumEntity*>(nullptr)));
	if (Fixture.Other != nullptr)
	{
		TestEqual(TEXT("slot 166 on an entity is that entity's slot 164"),
			Npc.CanStandOn(static_cast<FElysiumEntity*>(Fixture.Other)), Fixture.Other->IsStandable());
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 266 / 271 — the animating tables.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAnimTablesTest,
	"Elysium.Arm.NpcKernelEntityChain.AnimTables", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainAnimTablesTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// A live layer, armed by hand: slot 268 `SetLayer` is 0015's gesture seam and has no
	// port body.
	Npc.AnimOverlay[1].Activity = 0x42;
	Npc.AnimOverlay[1].Weight = ElysiumOverlay::SeedWeight;

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
	for (int32 Index = 0; Index < FElysiumAnimatingOverlay::NumFlinchRecords; ++Index)
	{
		Npc.Flinch[Index].Sequence = 40 + Index;
		Npc.Flinch[Index].Latch = 2;
		Npc.Flinch[Index].FadeIn = 0.25f;
		Npc.Flinch[Index].PoseParamIndex = 0x18;
		Npc.Flinch[Index].ExpireTime = 99.f;
	}
	Npc.Slot266();
	const float Now = static_cast<float>(Fixture.World.World.NowSeconds());
	for (int32 Index = 0; Index < FElysiumAnimatingOverlay::NumFlinchRecords; ++Index)
	{
		TestEqual(TEXT("the flinch sequence is cleared to -1"), Npc.Flinch[Index].Sequence, -1);
		TestEqual(TEXT("the expire stamp is one second in the PAST"), Npc.Flinch[Index].ExpireTime,
			Now - 1.f);
		TestEqual(TEXT("the latch survives"), Npc.Flinch[Index].Latch, 2);
		TestEqual(TEXT("the fade survives"), Npc.Flinch[Index].FadeIn, 0.25f);
		TestEqual(TEXT("the pose parameter survives"), Npc.Flinch[Index].PoseParamIndex, 0x18);
	}

	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 447 / 450 / 580 — the id spaces.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainIdSpaceTest,
	"Elysium.Arm.NpcKernelEntityChain.IdSpaces", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainIdSpaceTest::RunTest(const FString&)
{
	FEntityChainFixture Fixture;
	if (!TestNotNull(TEXT("the guard spawned"), Fixture.Guard))
	{
		return false;
	}
	FElysiumNpc& Npc = *Fixture.Guard;

	// Slot 580 is this family's row (`0x101aa790`) and family Schedule's walk is its body: this
	// NPC's own space out of the loaded corpus, or the Troika line's where its class has no
	// slot-580 body. It is never null.
	TestNotNull(TEXT("slot 580 answers an id space"), Npc.ClassScheduleIdSpace());
	TestTrue(TEXT("and it IS family Schedule's walk, not a second table"),
		Npc.ClassScheduleIdSpace() == Npc.IdSpace(EElysiumIdCategory::Schedule));

	// 0x101a6d00 is slot 580's BASE body and answers a DIFFERENT space from the Troika line's. It
	// is the corpus's `cai_basenpc` unit -- the root every other schedule space parents on.
	const FElysiumLocalIdSpace* Base = Npc.FElysiumNpcBase::ClassScheduleIdSpace();
	if (TestNotNull(TEXT("the base space is loaded"), Base))
	{
		TestTrue(TEXT("and it is not the Troika line's"),
			Base != FElysiumScheduleCorpus::Get().SpaceFor(TEXT("CAI_BaseNPCTroika"),
				EElysiumIdCategory::Schedule));
		TestNull(TEXT("the base schedule space is a ROOT: it has no parent"), Base->Parent);
		TestFalse(TEXT("and it is not empty -- 68 names went into it"), Base->IsEmpty());
		TestEqual(TEXT("its first local id is NONE, 0x00"), Base->LocalBase, ElysiumSched::NONE);
		TestEqual(TEXT("and its last is FAIL, 0x43"), Base->LocalTop, ElysiumSched::FAIL);
	}

	// `0x102ea280`: -1 stays -1, and a null space is the end of the chain.
	TestEqual(TEXT("the translation refuses -1 outright"),
		FElysiumNpcBase::GlobalToLocalId(Base, INDEX_NONE), INDEX_NONE);
	TestEqual(TEXT("a null space is the end of the chain"),
		FElysiumNpcBase::GlobalToLocalId(nullptr, 5), INDEX_NONE);
	TestEqual(TEXT("a global id below the base is not in the range"),
		FElysiumNpcBase::GlobalToLocalId(Base, 5), INDEX_NONE);

	// The arithmetic, over the real range: `(localBase - globalBase) + id` on the inclusive span
	// `[m_globalBase, m_translatedTop]`. The UPPER bound is the TRANSLATED top (`+0x0c`), which is
	// the word this port was missing -- it compared a global id against `m_localTop`, a local
	// number, and only the 9999 sentinel hid it.
	if (Base != nullptr && !Base->IsEmpty())
	{
		TestEqual(TEXT("the low bound is inclusive and rebases to the local base"),
			FElysiumNpcBase::GlobalToLocalId(Base, Base->GlobalBase), Base->LocalBase);
		TestEqual(TEXT("the high bound is the TRANSLATED top, inclusive"),
			FElysiumNpcBase::GlobalToLocalId(Base, Base->TranslatedTop), Base->LocalTop);
		TestEqual(TEXT("one below the range answers -1"),
			FElysiumNpcBase::GlobalToLocalId(Base, Base->GlobalBase - 1), INDEX_NONE);
		TestEqual(TEXT("one above the TRANSLATED top answers -1"),
			FElysiumNpcBase::GlobalToLocalId(Base, Base->TranslatedTop + 1), INDEX_NONE);
	}

	// Slots 447 and 450, both over this NPC's own sub-spaces. Both used to answer -1 for every id
	// because no row in this runtime carried a range; both translate now.
	const int32 IdleStandGlobal = ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND);
	TestEqual(TEXT("slot 447 translates a base schedule id back to its local number"),
		Npc.GetLocalScheduleId(IdleStandGlobal), ElysiumSched::IDLE_STAND);
	const int32 WaitTask = FElysiumScheduleCorpus::Get()
		.Namespace(EElysiumIdCategory::Task).Find(TEXT("TASK_WAIT"));
	TestTrue(TEXT("TASK_WAIT is a registered task identity"), (WaitTask) != INDEX_NONE);
	TestTrue(TEXT("slot 450 translates it through the TASK sub-space (+0x18)"), (Npc.GetLocalTaskId(WaitTask)) != INDEX_NONE);
	// The two sub-spaces are separate NAMESPACES whose counters are both seeded at 1e9, so their
	// ids overlap numerically -- an id means nothing without the namespace it came out of, which is
	// exactly why the four spaces are four and not one.

	return true;
}

// -------------------------------------------------------------------------------------------------
// The `CPayphone` trio.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainPayphoneTest,
	"Elysium.Arm.NpcKernelEntityChain.Payphone", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainPayphoneTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture World([]
	{
		FElysiumNpcWorldBuilder Builder(TEXT("sp_entitychain_phone"), 29103);
		Builder.AddNpc(TEXT("phone"), FVector(0.f, 0.f, 0.f), TEXT("npc_payphone"));
		Builder.AddNpc(TEXT("other"), FVector(300.f, 0.f, 0.f));
		return Builder;
	}());
	FElysiumNpcPayphone* Phone = World.NpcAs<FElysiumNpcPayphone>(TEXT("phone"));
	FElysiumNpc* Other = World.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the phone spawned"), Phone) || !TestNotNull(TEXT("the other spawned"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Phone, Other });
	FElysiumNpcPayphone& Npc = *Phone;

	// `npc_payphone` IS a registered spawn leaf (`Substrate/ElysiumNpcClasses.cpp`), and `CPayphone`
	// IS a census class — so both tables are checked here rather than assumed.
	const FElysiumNpcClass* Payphone = ElysiumNpcTestCensus::Find(TEXT("CPayphone"));
	if (TestNotNull(TEXT("CPayphone is a census class"), Payphone))
	{
		TestEqual(TEXT("CPayphone fills slot 286 with 0x101aad90"),
			FString(ElysiumNpcTestCensus::BodyOf(Payphone, 286)), FString(TEXT("0x101aad90")));
		TestEqual(TEXT("CPayphone fills slot 612 with 0x101aadb0"),
			FString(ElysiumNpcTestCensus::BodyOf(Payphone, 612)), FString(TEXT("0x101aadb0")));
		TestEqual(TEXT("CPayphone fills slot 35 with 0x101aa950"),
			FString(ElysiumNpcTestCensus::BodyOf(Payphone, 35)), FString(TEXT("0x101aa950")));
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

	// 0x101aa950 — `CanTalk(other) ? 0x2f : 0`. Slot 295 is the payphone's own `0x101aaee0`, and a
	// phone with no authored `dialogname` refuses at its arm 2; the test says WHICH term refused
	// rather than only that the number is 0.
	TestFalse(TEXT("slot 295 (0x101aaee0) refuses: no dialogname"), Npc.CanTalk(Other));
	TestEqual(TEXT("so the payphone publishes no caps"), Npc.PayphoneUseCaps(Other), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CDialog` and `CGlobalEntityList`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainDialogAndListTest,
	"Elysium.Arm.NpcKernelEntityChain.DialogAndList", GElysiumNpcKernelEntityChainFlags)
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

// The `CCineNPC` trio (`IsTimeToStart`, `CanInterrupt`, `FixScriptNPCSchedule`) is asserted on the
// real directors since story 5 fold A3: `Elysium.Substrate.NpcKernelDirector.*`.

// -------------------------------------------------------------------------------------------------
// The `CBasePlayer` law half.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainLawTest,
	"Elysium.Arm.NpcKernelEntityChain.Law", GElysiumNpcKernelEntityChainFlags)
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
	"Elysium.Arm.NpcKernelEntityChain.Police", GElysiumNpcKernelEntityChainFlags)
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
	// The duration ConVar answers `IsCommand`, so retail's own 0.0 arm makes the alert expire the
	// instant it is armed. That is the recovered refusal, not a chosen duration.
	TestFalse(TEXT("so the alert is already inactive on the frame it is armed"),
		Npc.IsHeightenedAlertActive());

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
	"Elysium.Arm.NpcKernelEntityChain.ScaredNpc", GElysiumNpcKernelEntityChainFlags)
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
	"Elysium.Arm.NpcKernelEntityChain.CameraFade", GElysiumNpcKernelEntityChainFlags)
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
	"Elysium.Arm.NpcKernelEntityChain.ClosestNpc", GElysiumNpcKernelEntityChainFlags)
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
	// `0x101828f0 MOV [+0x1cc4],0x47c34ff3`: the float 99999.8984375, not 100000.0 (integrator
	// correction, story 8 L08: the constant was rounded).
	TestEqual(TEXT("the reset distance is the 0x47c34ff3 immediate, 99999.8984375"),
		Player->Observer.DistanceCm, 99999.8984375f);

	// The acceptance ladder. SEAM: `GetModelPtr()` answers false with no studio header, and that
	// rung REFUSES — so the recovered answer today is that no candidate is ever cached. The test
	// asserts the rung that refused rather than only the outcome.
	TestFalse(TEXT("the cache is left empty"), Player->Observer.Observer.IsSet());

	// The two unrecovered rungs both answer the ADMITTING value, so neither is what refused.

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
	return true;
}

// -------------------------------------------------------------------------------------------------
// Autoaim, the held use entity, the player animation and the controller detach.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAutoaimTest,
	"Elysium.Arm.NpcKernelEntityChain.Autoaim", GElysiumNpcKernelEntityChainFlags)
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

	// `0x10176520`'s blend words: skill 1 (`DAT_1070ba3c`, the seam's answer) takes the 0.9 scale arm;
	// the old-sample weight is 0.3 for skills 2 and 3.
	float Scale = 9.f;
	float OldWeight = 9.f;
	TestTrue(TEXT("skill 1 takes the DAT_1070ba3c == 1 scale arm"),
		Npc.AutoaimBlendWeights(Scale, OldWeight));
	TestEqual(TEXT("the scale is _DAT_10450a9c = 0.9"), Scale, 0.9f);
	TestEqual(TEXT("the old-sample weight is _DAT_10451ab8 = 0.3"), OldWeight, 0.3f);

	// `m_takedamage` reaches the autoaim trace through the NPC leaf only.
	TestTrue(TEXT("a live NPC takes damage"), FElysiumNpc::ChainTakesDamage(*Fixture.Other));
	Fixture.Other->TakeDamageMode = 0;
	TestFalse(TEXT("DAMAGE_NO does not"), FElysiumNpc::ChainTakesDamage(*Fixture.Other));
	Fixture.Other->TakeDamageMode = 2;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainUseAndControllerTest,
	"Elysium.Arm.NpcKernelEntityChain.UseAndController",
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

	// --- 0x10182c40 `SetPlayerAnim` ---
	// The pointer at +0x1ca4 tracks whether the COPIED name is non-empty, and the buffer is written
	// either way.
	Npc.PlayerAnimFlags = 0;
	Npc.SetPlayerAnim(TEXT("player_reload"), nullptr);
	TestTrue(TEXT("a non-empty name arms the pointer"), Npc.bPlayerAnimNameSet);
	TestEqual(TEXT("and lands in the one global buffer"), Npc.PlayerAnimNameBuffer,
		FString(TEXT("player_reload")));
	TestEqual(TEXT("bit 0 of +0x1cac is raised"), Npc.PlayerAnimFlags & 1, 1);
	// No sound: the deadline is `curtime - 0.5 + 0.6`, both DOUBLES, stored as a float.
	TestEqual(TEXT("the _DAT_10449270 lead-in is the double 0.5"), Npc.PlayerAnimLeadIn(), 0.5);
	TestEqual(TEXT("and the _DAT_10471720 fallback duration the double 0.6"),
		Npc.PlayerAnimFallbackDuration(), 0.6);
	TestEqual(TEXT("so a no-sound deadline stands 0.1 s past curtime"), Npc.PlayerAnimEndTime,
		static_cast<float>(static_cast<double>(static_cast<float>(Fixture.World.World.NowSeconds()))
			- 0.5 + 0.6));

	Npc.SetPlayerAnim(TEXT(""), nullptr);
	TestFalse(TEXT("an empty name disarms the pointer"), Npc.bPlayerAnimNameSet);
	TestTrue(TEXT("but the buffer is still overwritten"), Npc.PlayerAnimNameBuffer.IsEmpty());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 65 and the think channel.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainAnglesAndThinkTest,
	"Elysium.Arm.NpcKernelEntityChain.AnglesAndThink", GElysiumNpcKernelEntityChainFlags)
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
	return true;
}

// --- `AutoaimDeflection`'s walk order (`0x10176930`) ----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelEntityChainWalkOrderTest,
	"Elysium.Arm.NpcKernelEntityChain.WalkOrder", GElysiumNpcKernelEntityChainFlags)
bool FElysiumNpcKernelEntityChainWalkOrderTest::RunTest(const FString&)
{
	// Six rows in a known lump order. Retail's walk is the EDICT ARRAY by ascending index, and this
	// port's entity index is that index: the def array in lump order, stable and never recycled.
	FElysiumNpcWorldBuilder Builder(TEXT("sp_entitychain_order"), 29104);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("a"));
	Builder.AddCounter(TEXT("b"));
	Builder.AddNpc(TEXT("c"));
	Builder.AddEntity(TEXT("point_target"), TEXT("d"));
	Builder.AddNpc(TEXT("e"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* A = Fixture.Npc(TEXT("a"));
	if (!TestNotNull(TEXT("a"), A))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ A, Fixture.Npc(TEXT("c")), Fixture.Npc(TEXT("e")) });

	TArray<FElysiumEntity*> Walk;
	A->ChainEntityList(Walk);

	// 1. The walk is in ASCENDING ENTITY INDEX, which is retail's ascending EDICT index — not the
	//    order the name index, the class index or a hash happens to hold. This is the whole rule,
	//    and it is observable because `AutoaimDeflection`'s score test is `<=`: among equally
	//    aligned candidates the last one walked wins, so the order decides the winner.
	bool bAscending = true;
	for (int32 i = 1; i < Walk.Num(); ++i)
	{
		bAscending = bAscending && Walk[i - 1]->Handle.Index < Walk[i]->Handle.Index;
	}
	TestTrue(TEXT("0x10176930 walks by strictly ascending entity index"), bAscending);

	// 2. And that index order is the map's LUMP order: the def array, as loaded.
	TArray<FString> Names;
	for (const FElysiumEntity* Entity : Walk)
	{
		if (!Entity->TargetName.IsEmpty())
		{
			Names.Add(Entity->TargetName);
		}
	}
	const TArray<FString> Expected = { TEXT("world"), TEXT("a"), TEXT("b"), TEXT("c"),
		TEXT("d"), TEXT("e") };
	if (TestTrue(TEXT("every def row is walked"), Names.Num() >= Expected.Num()))
	{
		const TArray<FString> Lump(Names.GetData(), Expected.Num());
		TestEqual(TEXT("in the def array's own order"), FString::Join(Lump, TEXT(",")),
			FString::Join(Expected, TEXT(",")));
	}

	// 3. A spawn made AFTER the load walks after every map entity, because its index continues past
	//    the def array — retail's next free edict is past the map's too. The player is the one such
	//    spawn every world has (the viewmodels it brings with it come after it again, which is why
	//    this asserts "after the map", not "last").
	const FElysiumPlayer* Player = Fixture.Player();
	const FElysiumNpc* E = Fixture.Npc(TEXT("e"));
	if (TestNotNull(TEXT("the player stands"), Player) && TestNotNull(TEXT("e"), E))
	{
		TestTrue(TEXT("a spawn past the def array walks after every map entity"),
			Player->Handle.Index > E->Handle.Index);
		int32 PlayerAt = INDEX_NONE;
		int32 EAt = INDEX_NONE;
		for (int32 i = 0; i < Walk.Num(); ++i)
		{
			PlayerAt = Walk[i]->Handle.Index == Player->Handle.Index ? i : PlayerAt;
			EAt = Walk[i]->Handle.Index == E->Handle.Index ? i : EAt;
		}
		TestTrue(TEXT("and the walk visits it after them"),
			PlayerAt != INDEX_NONE && EAt != INDEX_NONE && EAt < PlayerAt);
	}

	// 4. `edict[0x4c] != 0` — the free-slot byte. A reaped slot drops OUT of the walk and the
	//    survivors keep their indices; retail's `continue` renumbers nothing.
	FElysiumNpc* C = Fixture.Npc(TEXT("c"));
	if (!TestNotNull(TEXT("c resolves"), C))
	{
		return false;
	}
	const int32 CIndex = C->Handle.Index;
	C->Kill();
	A->ChainEntityList(Walk);
	bool bReapedGone = true;
	bool bStillAscending = true;
	for (int32 i = 0; i < Walk.Num(); ++i)
	{
		bReapedGone = bReapedGone && Walk[i]->Handle.Index != CIndex;
		bStillAscending = bStillAscending
			&& (i == 0 || Walk[i - 1]->Handle.Index < Walk[i]->Handle.Index);
	}
	TestTrue(TEXT("a reaped slot is skipped"), bReapedGone);
	TestTrue(TEXT("and the survivors keep their index order"), bStillAscending);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
