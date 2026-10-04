#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumSceneData.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **Anim** — the gesture-layer table, the flex controllers, the scene-event
// queue and the activity commit.
//
// Every number asserted here comes out of the decompiled body the case names: the table strides, the
// search order, the growth arithmetic, the seed weight, the flinch victim rule and the fallback
// ladder's rungs. Where an input is a SEAM the case says so and asserts that the seam is asked and
// that the refusal is the recovered one, which is the only honest assertion until it has a source.

static constexpr EAutomationTestFlags GElysiumNpcKernelAnimFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// --- The gesture-layer table (slots 265, 269, 270, 273, 274, 275) -------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimGestureLayersTest,
	"Elysium.Arm.NpcKernelAnim.GestureLayers", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimGestureLayersTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_layers"), 5101);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// Four slots, because `m_Flinch[0]` begins exactly where a fifth record would.
	TestEqual(TEXT("the kernel's table is the same four slots the render stack carries"),
		static_cast<int32>(UE_ARRAY_COUNT(Guard->AnimOverlay)), ElysiumOverlay::NumSlots);

	// Two live layers, armed by hand: the pushers (`SetLayer`, `AddGesture`) are 0015's
	// gesture seam and have no port body. What stays is the lookup and the remover StartTask reads.
	Guard->AnimOverlay[0].Activity = 0x3b;
	Guard->AnimOverlay[0].Weight = ElysiumOverlay::SeedWeight;
	Guard->AnimOverlay[2].Activity = 0x3d;
	Guard->AnimOverlay[2].Weight = ElysiumOverlay::SeedWeight;
	Guard->AnimOverlay[2].Sequence = 19;

	// `FindGestureLayer` `0x100994c0`, three terms: live, owner != -1, owner == asked.
	TestEqual(TEXT("the armed layer is found by its owner"), Guard->FindGestureLayerByOwner(0x3b),
		0);
	TestEqual(TEXT("and an owner nothing holds answers -1"), Guard->FindGestureLayerByOwner(0x3c),
		INDEX_NONE);

	// `RemoveLayerByOwner` `0x100995e0`: weight then sequence behind the lookup, and a silent no-op
	// on a miss. The owner activity is NOT written.
	Guard->RemoveLayerByOwner(0x3d);
	TestEqual(TEXT("RemoveLayerByOwner frees the slot its lookup found"),
		Guard->AnimOverlay[2].Weight, 0.f);
	TestEqual(TEXT("and zeroes its sequence"), Guard->AnimOverlay[2].Sequence, 0);
	TestEqual(TEXT("and leaves the owner activity standing"), Guard->AnimOverlay[2].Activity, 0x3d);
	Guard->RemoveLayerByOwner(0x99);   // nothing holds it
	TestEqual(TEXT("a miss touches nothing"), Guard->AnimOverlay[0].Weight, ElysiumOverlay::SeedWeight);
	return true;
}

// --- The flex controllers (slots 280, 281, 282) -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimFlexTest,
	"Elysium.Arm.NpcKernelAnim.FlexControllers", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimFlexTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_flex"), 5103);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `m_flexWeight` is retail's own `float[128]`.
	TestEqual(TEXT("the flex-weight array is retail's 128"),
		static_cast<int32>(UE_ARRAY_COUNT(Guard->FlexWeight)), FElysiumFlex::NumFlexWeightSlots);

	// The studio seams, and what they refuse with.
	TestEqual(TEXT("GetNumFlexControllers answers an empty table"), Guard->NumFlexControllers(), 0);
	// `LookupFlexController` `0x100b5d10` (its callers, slots 280 / 282 / 289, closed at 0010) was
	// removed in 0019/6 with no caller left; its cases went with it.

	return true;
}

// --- The scene-event queue (slots 283, 286, 290, 291) -------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimSceneEventsTest,
	"Elysium.Arm.NpcKernelAnim.SceneEvents", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimSceneEventsTest::RunTest(const FString&)
{
	// `CUtlMemory::Grow`, as `CBaseFlex::AddSceneEvent` inlines it. Pure arithmetic, so it is asserted with no
	// world at all.
	using FNpc = FElysiumNpc;
	TestEqual(TEXT("an empty buffer goes to 2 first"), FNpc::GrowSceneEventCapacity(0, 0, 1), 2);
	TestEqual(TEXT("then a zero grow size DOUBLES"), FNpc::GrowSceneEventCapacity(2, 0, 3), 4);
	TestEqual(TEXT("and keeps doubling until it covers the need"),
		FNpc::GrowSceneEventCapacity(2, 0, 9), 16);
	TestEqual(TEXT("a non-zero grow size ADDS instead"), FNpc::GrowSceneEventCapacity(4, 3, 5), 7);
	TestEqual(TEXT("and adds again rather than doubling"),
		FNpc::GrowSceneEventCapacity(4, 3, 11), 13);
	TestEqual(TEXT("a grow size of -1 is external memory and refuses to grow"),
		FNpc::GrowSceneEventCapacity(2, -1, 9), 2);
	TestEqual(TEXT("a capacity that already covers the need is left alone"),
		FNpc::GrowSceneEventCapacity(8, 0, 4), 8);

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_scene"), 5104);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// The port's `EElysiumChoreoEvent` carries retail's own numbering, which is what lets the Troika
	// dispatch read as names here and as `2 / 7 / 0xd / 0xe / 0xf` in the listing.
	TestEqual(TEXT("Gesture is retail's type 6"),
		static_cast<int32>(EElysiumChoreoEvent::Gesture), 6);
	TestEqual(TEXT("Sequence is 7"), static_cast<int32>(EElysiumChoreoEvent::Sequence), 7);
	TestEqual(TEXT("Silence is 0xd"), static_cast<int32>(EElysiumChoreoEvent::Silence), 0xd);
	TestEqual(TEXT("Loud is 0xe"), static_cast<int32>(EElysiumChoreoEvent::Loud), 0xe);
	TestEqual(TEXT("Python is 0xf"), static_cast<int32>(EElysiumChoreoEvent::Python), 0xf);

	FElysiumSceneData Scene;
	Scene.bValid = true;
	FElysiumSceneEvent Speak;
	Speak.Type = EElysiumChoreoEvent::Speak;
	Speak.Name = TEXT("line_01");
	FElysiumSceneEvent Gesture;
	Gesture.Type = EElysiumChoreoEvent::Gesture;
	Gesture.Name = TEXT("wave");
	Gesture.Param = TEXT("gesture_wave");
	Gesture.StartTime = 1.f;
	Gesture.EndTime = 3.f;
	Gesture.bHasEnd = true;

	// The queue: one record per add, ALWAYS at the end — retail's inlined `InsertBefore(m_Size)`
	// computes a shift count of exactly zero, so the `memmove` never moves anything on an add.
	Guard->AddSceneEvent(&Scene, &Speak);
	TestEqual(TEXT("the first add allocates 2"), Guard->SceneEventsAllocated, 2);
	TestEqual(TEXT("and queues one record"), Guard->SceneEvents.Num(), 1);
	Guard->AddSceneEvent(&Scene, &Gesture);
	TestEqual(TEXT("the second fits the same block"), Guard->SceneEventsAllocated, 2);
	Guard->AddSceneEvent(&Scene, &Speak);
	TestEqual(TEXT("the third doubles it"), Guard->SceneEventsAllocated, 4);
	TestEqual(TEXT("appending, never inserting"), Guard->SceneEvents.Num(), 3);
	TestEqual(TEXT("the record keeps the EVENT, which is what RemoveSceneEvent searches by"),
		static_cast<const void*>(Guard->SceneEvents[1].Event), static_cast<const void*>(&Gesture));
	TestEqual(TEXT("and the SCENE, which is what ClearSceneEvents searches by"),
		static_cast<const void*>(Guard->SceneEvents[1].Scene), static_cast<const void*>(&Scene));

	// The two live arms both begin with `LookupSequence`, a seam answering -1, so neither arms the
	// record: `+0x0c` stays at -1 and every `Process*` guard refuses it.
	TestEqual(TEXT("so the gesture record is queued un-armed"), Guard->SceneEvents[1].Handle, -1);

	return true;
}

// --- The Troika scene-event dispatch (slot 286) -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimTroikaSceneEventTest,
	"Elysium.Arm.NpcKernelAnim.TroikaSceneEvent", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimTroikaSceneEventTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_troika_scene"), 5105);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	FElysiumSceneData Scene;
	Scene.bValid = true;

	// Type 2 (Expression) — `0x102c1a80`, which ends in `AddScriptedExpression`. The list it writes
	// is a seam, and the request is recorded so the arm is visible.
	FElysiumSceneEvent Expression;
	Expression.Type = EElysiumChoreoEvent::Expression;
	Expression.Param = TEXT("angry");
	Expression.StartTime = 0.f;
	Expression.EndTime = 1.5f;
	Expression.bHasEnd = true;
	Guard->AddSceneEvent(&Scene, &Expression);
	TestEqual(TEXT("the expression arm raises a scripted expression"),
		Guard->ScriptedExpressions.Num(), 1);
	TestEqual(TEXT("named by the event's parameter"), Guard->ScriptedExpressions[0].Expression,
		FString(TEXT("angry")));
	TestEqual(TEXT("for the event's own duration"), Guard->ScriptedExpressions[0].Duration, 1.5f);
	TestEqual(TEXT("and it never reaches the queue"), Guard->SceneEvents.Num(), 0);

	// Type 7 (Sequence) — the lookup seam misses, so the arm writes NOTHING and does not fall
	// through to the base either. The two stance latches are the tell.
	Guard->Stance.bInFidget = true;
	Guard->Stance.bInChange = true;
	FElysiumSceneEvent SequenceEvent;
	SequenceEvent.Type = EElysiumChoreoEvent::Sequence;
	SequenceEvent.Param = TEXT("idle_stand");
	Guard->AddSceneEvent(&Scene, &SequenceEvent);
	TestTrue(TEXT("a sequence the body does not author leaves the fidget latch alone"),
		Guard->Stance.bInFidget);
	TestTrue(TEXT("and the stance-change latch"), Guard->Stance.bInChange);
	TestEqual(TEXT("and does NOT reach the base queue"), Guard->SceneEvents.Num(), 0);

	// Type 0xd (Silence) — the stance reaction, gated on a live dialogue partner FIRST. With no
	// conversation open the body stops before the disposition table is even asked.
	FElysiumSceneEvent Silence;
	Silence.Type = EElysiumChoreoEvent::Silence;
	Silence.Param = TEXT("0.9");
	TestFalse(TEXT("no conversation is open, so there is no dialogue partner"),
		Guard->HasLiveDialogPartner());
	Guard->AddSceneEvent(&Scene, &Silence);
	TestEqual(TEXT("so the Silence arm writes nothing and never reaches the queue"),
		Guard->SceneEvents.Num(), 0);
	// With a partner it reaches the disposition table, which refuses — the recovered refusal.
	// `m_hDialogPartner +0xfe8` through its one store, `SetDialogPartner 0x10107050`.
	Guard->SetDialogPartner(Fixture.Player()->Handle);
	TestTrue(TEXT("a live m_hDialogPartner is the dialogue partner"), Guard->HasLiveDialogPartner());
	Guard->AddSceneEvent(&Scene, &Silence);
	TestEqual(TEXT("so the reaction is dropped at the table"), Guard->SceneEvents.Num(), 0);
	Guard->SetDialogPartner(FElysiumEntityHandle::Invalid());

	// Type 0xe (Loud) — gated on `m_flLoudExpressionTime` (+0x6574) having passed. A cooldown in
	// the future refuses before the table is asked; a passed one reaches the seam, which refuses.
	FElysiumSceneEvent Loud;
	Loud.Type = EElysiumChoreoEvent::Loud;
	Loud.Param = TEXT("1.0");
	Guard->LoudExpressionTime = 1.0e9;
	Guard->AddSceneEvent(&Scene, &Loud);
	TestEqual(TEXT("a live cooldown refuses the loud expression"),
		Guard->ScriptedExpressions.Num(), 1);
	Guard->LoudExpressionTime = -1.0;
	Guard->AddSceneEvent(&Scene, &Loud);
	TestEqual(TEXT("so the loud arm raises nothing"), Guard->ScriptedExpressions.Num(), 1);
	TestEqual(TEXT("and leaves the cooldown where it was"), Guard->LoudExpressionTime, -1.0);

	// The clamp the loud arm applies once it HAS a row, stated against the body's own three arms.
	auto Clamp = [](float Authored, float InMin, float InMax)
	{
		return (Authored <= InMax) ? ((InMin <= Authored) ? Authored : InMin) : InMax;
	};
	TestEqual(TEXT("inside the band the authored level stands"), Clamp(0.5f, 0.2f, 0.8f), 0.5f);
	TestEqual(TEXT("below it the minimum does"), Clamp(0.1f, 0.2f, 0.8f), 0.2f);
	TestEqual(TEXT("above it the maximum does"), Clamp(0.9f, 0.2f, 0.8f), 0.8f);

	// Type 0xf (Python) — the call is recorded and named by the event's SECOND parameter.
	FElysiumSceneEvent Python;
	Python.Type = EElysiumChoreoEvent::Python;
	Python.Param = TEXT("ignored_first_parameter");
	Python.Param2 = TEXT("OnJackShrug");
	Guard->AddSceneEvent(&Scene, &Python);
	TestEqual(TEXT("the Python arm records one call"), Guard->PythonDialogCalls.Num(), 1);
	TestEqual(TEXT("named by the event's SECOND parameter"), Guard->PythonDialogCalls[0],
		FString(TEXT("OnJackShrug")));

	// Everything else forwards to the base body `CBaseFlex::AddSceneEvent`, which is what queues it.
	FElysiumSceneEvent Speak;
	Speak.Type = EElysiumChoreoEvent::Speak;
	Guard->AddSceneEvent(&Scene, &Speak);
	TestEqual(TEXT("an unclaimed type reaches the base and is queued"), Guard->SceneEvents.Num(), 1);
	return true;
}

// --- The activity commit ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimActivityCommitTest,
	"Elysium.Arm.NpcKernelAnim.ActivityCommit", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimActivityCommitTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_activity"), 5106);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `IsActivityFinished` `0x10272900`, slot 251 — BOTH terms.
	Guard->bSequenceFinished = false;
	Guard->SequenceNumber = 4;
	Guard->IdealSequence = 4;
	TestFalse(TEXT("an unfinished sequence is not a finished activity"),
		Guard->IsActivityFinished());
	Guard->bSequenceFinished = true;
	TestTrue(TEXT("finished and already on the ideal sequence is"), Guard->IsActivityFinished());
	Guard->IdealSequence = 5;
	TestFalse(TEXT("a body still blending toward its ideal is not finished"),
		Guard->IsActivityFinished());

	// `ForcePreTranslatedSequenceAndActivity` `0x10272400`, slot 311. A NEGATIVE sequence refuses
	// the whole body — nothing at all is written.
	Guard->ActivityNumber = 7;
	Guard->IdealActivityNumber = 7;
	Guard->TranslatedActivity = 7;
	Guard->SequenceCycle = 0.33f;
	Guard->PrevAnimTime = 9.f;
	Guard->ForcePreTranslatedSequenceAndActivity(0x3b, 0x3c, -1);
	TestEqual(TEXT("a negative sequence writes nothing at all"), Guard->ActivityNumber, 7);
	TestEqual(TEXT("not even the cycle"), Guard->SequenceCycle, 0.33f);

	Guard->ForcePreTranslatedSequenceAndActivity(0x3b, 0x3c, 21);
	TestEqual(TEXT("m_Activity takes the FIRST argument"), Guard->ActivityNumber, 0x3b);
	TestEqual(TEXT("and so does m_IdealActivity"), Guard->IdealActivityNumber, 0x3b);
	TestEqual(TEXT("m_IdealWeaponActivity takes the second"), Guard->IdealWeaponActivity, 0x3c);
	TestEqual(TEXT("and m_IdealTranslatedActivity"), Guard->IdealTranslatedActivity, 0x3c);
	TestEqual(TEXT("and m_TranslatedActivity"), Guard->TranslatedActivity, 0x3c);
	TestEqual(TEXT("m_nIdealSequence takes the third"), Guard->IdealSequence, 21);
	TestEqual(TEXT("and the forced sequence is committed"), Guard->SequenceNumber, 21);
	TestEqual(TEXT("the cycle is zeroed"), Guard->SequenceCycle, 0.f);
	TestEqual(TEXT("and m_flPrevAnimTime with it"), Guard->PrevAnimTime, 0.f);

	// `SetIdealActivity` `0x10272650`: activity 0 TAIL JUMPS to slot 310 and stores nothing ITSELF;
	// every other activity stores the word here and re-resolves the ideal triple beside it.
	//
	// **STRENGTHENED by story 29d, family Anim10.** Slot 310 was a generated stub when this case
	// landed, so "without storing the word" could be read off `m_IdealActivity` still holding 42.
	// Slot 310 is now `CAI_BaseNPCTroika::SetActivity` (`0x10295750`) over
	// `CAI_BaseNPC::SetActivity` (`0x102725d0`), and the BASE body's own third line is
	// `m_IdealActivity = act` — so the word does land, written by the slot and not by this body.
	// The probe now reads the real body's output: 0, not 42.
	Guard->IdealActivityNumber = 42;
	Guard->SetIdealActivity(0);
	TestEqual(TEXT("ACT_INVALID resets through slot 310, whose own base body stores the word"),
		Guard->IdealActivityNumber, 0);
	Guard->SetIdealActivity(0x3b);
	TestEqual(TEXT("and any other activity stores m_IdealActivity"), Guard->IdealActivityNumber,
		0x3b);
	// Corrected to retail (story 8 wave 2): this body resolves no clip for 0x3b, so the ladder's
	// ACT_DISPOSITION retry reaches slot 611, whose no-stance fallback is `m_nSequence`
	// (`0x102c12a0`) — the 21 the forced commit above left playing.
	TestEqual(TEXT("and re-resolves the ideal sequence beside it — slot 611's fallback, the playing "
		"sequence"), Guard->IdealSequence, 21);

	// `ShouldMaintainActivity` `0x102bf510`, slot 466 — three arms in order.
	Guard->bForceMaintainActivity = false;
	Guard->ActivityNumber = 2;
	TestTrue(TEXT("an idle body maintains its activity"), Guard->ShouldMaintainActivity());
	Guard->bForceMaintainActivity = true;
	TestTrue(TEXT("and the force byte answers true outright"), Guard->ShouldMaintainActivity());

	// `CanPlaySequence` `0x10278090`, slot 482 — the return is retail's `CanPlaySequence_t`, where
	// 1 and 2 are two different yeses.
	TestEqual(TEXT("an idle body with no cine plays the sequence"),
		Guard->CanPlaySequence(false, 0), 1);
	TestFalse(TEXT("and this body holds no cine"), Guard->ScriptOwnerIsLive());
	return true;
}

// --- `ResolveActivityToSequence` `0x10272130` ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimResolveActivityTest,
	"Elysium.Arm.NpcKernelAnim.ResolveActivityToSequence", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimResolveActivityTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_resolve"), 5107);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// The translation seam answers the activity unchanged, which is retail's own EMPTY-table answer
	// and the case of an unarmed body.
	int32 Weapon = -99;
	TestEqual(TEXT("an empty translation table answers the activity unchanged"),
		Guard->TranslateActivityNumber(0x13, Weapon), 0x13);
	TestEqual(TEXT("and writes the weapon activity beside it"), Weapon, 0x13);

	int32 Sequence = -99;
	int32 Translated = -99;
	Weapon = -99;

	// An ordinary activity: the weighted draw misses (this body resolves no clip for it), the
	// run-to-walk rung does not apply, and the whole request is retried as ACT_DISPOSITION (0xf1).
	// Corrected to retail (story 8 wave 2, the sequence bridge): the Troika disposition resolver
	// `0x10295a80` answers slot 611, whose own fallback for "no stance sequence" is `m_nSequence`
	// (`0x102c12a0`: `return seq == -1 ? m_nSequence : seq`) — never -1 — so the disposition rung
	// answers the PLAYING sequence (0 here) and the floor rung is not reached.
	Guard->ResolveActivityToSequence(0x3b, Sequence, Translated, Weapon);
	TestEqual(TEXT("the ladder ends on the playing sequence, 0"), Sequence, 0);
	TestEqual(TEXT("and the rung that answered says so: the disposition resolver (0x10295a80)"),
		static_cast<int32>(Guard->LastResolveActivityRung),
		static_cast<int32>(FElysiumNpcBase::EResolveActivityRung::DispositionTable));
	TestEqual(TEXT("with the translated activity left at the retry's own 0xf1"), Translated, 0xf1);

	// ACT_RUN (0x13): the SAME floor, but the run-to-walk rung is reached on the way — retail rewrites
	// the translated activity to 9 before it retries as a disposition, and the rewrite survives only
	// as long as that pass.
	Guard->ResolveActivityToSequence(0x13, Sequence, Translated, Weapon);
	TestEqual(TEXT("ACT_RUN falls to the same floor"), Sequence, 0);

	// ACT_DISPOSITION asked for DIRECTLY takes the Troika resolver, which answers the playing
	// sequence (slot 611's fallback).
	Guard->ResolveActivityToSequence(0xf1, Sequence, Translated, Weapon);
	TestEqual(TEXT("ACT_DISPOSITION lands on the playing sequence 0"), Sequence, 0);
	TestEqual(TEXT("through the disposition rung"),
		static_cast<int32>(Guard->LastResolveActivityRung),
		static_cast<int32>(FElysiumNpcBase::EResolveActivityRung::DispositionTable));

	// ACT_SCRIPT_CUSTOM_MOVE (0x18) with no cine is NOT the custom-move arm: retail's guard is the
	// cine handle, and without one the request takes the ordinary weighted rung.
	TestFalse(TEXT("this body holds no cine"), Guard->ScriptOwnerIsLive());
	Guard->ResolveActivityToSequence(0x18, Sequence, Translated, Weapon);
	TestEqual(TEXT("so ACT_SCRIPT_CUSTOM_MOVE walks the ordinary ladder to the floor"), Sequence, 0);

	// The disposition resolver, asserted directly: a body with no stance set answers slot 611's
	// fallback, `m_nSequence`. Corrected (L13 wave-2 fixes, review note 43): `0x10295a80` writes
	// `*param_2` alone (`0x10295a88..0x10295a8e`, `RET 0x10`), so the translated activity is left as
	// the caller held it.
	int32 DispSequence = 7;
	int32 DispActivity = 7;
	Guard->SequenceNumber = 5;
	Guard->ResolveDispositionActivity(DispSequence, DispActivity);
	TestEqual(TEXT("0x102c12a0 no stance sequence: slot 611 answers m_nSequence"), DispSequence, 5);
	TestEqual(TEXT("0x10295a8e: the translated activity is not written"), DispActivity, 7);
	return true;
}

// --- The remaining single-arm bodies ------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimMiscBodiesTest,
	"Elysium.Arm.NpcKernelAnim.MiscBodies", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimMiscBodiesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_misc"), 5108);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpcOfClass(TEXT("ming"), FVector(300.0, 0.0, 0.0), TEXT("CNPC_VMingXiao"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcMingXiao* Ming = Fixture.NpcAs<FElysiumNpcMingXiao>(TEXT("ming"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	TestNotNull(TEXT("the Ming Xiao spawned"), Ming);
	if (Guard == nullptr || Ming == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Ming });

	// `BodyGroup` `0x10398800` (`CNPC_VMingXiao`), three arms. The cvar is a seam whose NAME and
	// DEFAULT are unrecovered; it answers "not a command, value -1", which is the arm that takes the
	// mask.
	TestEqual(TEXT("standing at retail's negative default"), Ming->BodyGroupCvarValue(), -1);
	Ming->MingXiaoSeveredTentacleMask = 6;
	Ming->NpcBody = 99;
	Ming->BodyGroup();
	TestEqual(TEXT("so m_nBody takes the severed-tentacle mask"), Ming->NpcBody, 6);

	// Slots 59/60/61 — the choreo latches. `m_bCutsceneForceLOD` is gated on the scene entity's own
	// +0x57d byte, which is a seam answering false, so the LOD byte is never touched.
	Guard->bInChoreoScene = false;
	Guard->bCutsceneForceLOD = true;
	Guard->Slot59(nullptr);
	TestTrue(TEXT("slot 59 raises bInChoreoScene unconditionally"), Guard->bInChoreoScene);
	TestTrue(TEXT("and leaves the LOD byte alone when the scene does not force it"),
		Guard->bCutsceneForceLOD);
	Guard->Slot60(nullptr);
	TestFalse(TEXT("slot 60 clears bInChoreoScene"), Guard->bInChoreoScene);
	Guard->Slot59(nullptr);
	Guard->Slot61(nullptr);
	TestFalse(TEXT("and slot 61 is byte for byte the same body"), Guard->bInChoreoScene);

	// `IdleSequenceGate` `0x102b8a10`: COND_ENEMY_DEAD (0x58) first, then the activity probe.
	TestEqual(TEXT("EnemyDead is retail's condition 0x58"),
		static_cast<int32>(EElysiumNpcCond::EnemyDead), 0x58);
	Guard->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyDead);
	TestEqual(TEXT("without the condition the gate answers SCHED_NONE"), Guard->IdleSequenceGate(),
		0);
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::EnemyDead);
	TestEqual(TEXT("with it, but with no clip for activity 0x61, it still answers none — the "
		"sequence probe is the second gate and it is a seam"), Guard->IdleSequenceGate(), 0);
	Guard->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyDead);

	// Slot 345 `SetPoseParameter(name, value, bool)` — the write is recorded on family Facing's one
	// pose-parameter surface, and the index the retail dispatch resolves is the seam's -1.
	const int32 Before = Guard->PoseParameterWrites.Num();
	Guard->SetPoseParameter(TEXT("aim_yaw"), 0.25f, true);
	TestEqual(TEXT("slot 345 records the write"), Guard->PoseParameterWrites.Num(), Before + 1);
	TestEqual(TEXT("by name"), Guard->PoseParameterWrites.Last().Name, FString(TEXT("aim_yaw")));
	TestEqual(TEXT("and value"), Guard->PoseParameterWrites.Last().Value, 0.25f);

	// `0x102c0aa0` on `FElysiumNpcDialogue` — the two arms and the STRICT comparison.
	Guard->TalkingUntil = 10.0;
	TestTrue(TEXT("still before the talk-end stamp is still talking"),
		Guard->Dialogue.IsTalking(*Guard, 9.9));
	TestFalse(TEXT("the stamp's own instant is NOT — retail compares strictly"),
		Guard->Dialogue.IsTalking(*Guard, 10.0));
	TestFalse(TEXT("and the scene arm has no byte to read yet"),
		Guard->Dialogue.DialogSceneReportsDone(*Guard));
	return true;
}

// --- The species tables -------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimSpeciesTest,
	"Elysium.Arm.NpcKernelAnim.Species", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimSpeciesTest::RunTest(const FString&)
{
	// Slot 259's EMPTY override, exercised by RETAIL CLASS NAME through the census reader. (Since
	// story 5 step 2 `npc_VCamera` and `npc_VCameraSecurity` are registered classnames building
	// these classes, population.md; the rows are read by class name so no camera need be stood.)
	const FElysiumNpcClass* const Camera = ElysiumNpcTestCensus::Find(TEXT("CNPC_VCamera"));
	TestNotNull(TEXT("CNPC_VCamera is a census class"), Camera);
	if (Camera != nullptr)
	{
		TestTrue(TEXT("and it is its own class"),
			ElysiumNpcTestCensus::DerivesFrom(Camera, TEXT("CNPC_VCamera")));
		TestEqual(TEXT("whose slot 259 body is the empty one"),
			FString(ElysiumNpcTestCensus::BodyOf(Camera, 259)), FString(TEXT("0x10368ec0")));
	}
	const FElysiumNpcClass* const CameraSecurity =
		ElysiumNpcTestCensus::Find(TEXT("CNPC_VCameraSecurity"));
	TestNotNull(TEXT("CNPC_VCameraSecurity is one too"), CameraSecurity);
	if (CameraSecurity != nullptr)
	{
		TestEqual(TEXT("sharing the same empty slot-259 body"),
			FString(ElysiumNpcTestCensus::BodyOf(CameraSecurity, 259)),
			FString(TEXT("0x10368ec0")));
	}

	// Slots 245/246's forwarding override, by retail class name.
	const TCHAR* const Forwarders[] =
	{
		TEXT("CNPC_VFrenzyShadow"), TEXT("CNPC_VPlayerController"), TEXT("CNPC_VWolfMorph"),
	};
	for (const TCHAR* const Name : Forwarders)
	{
		const FElysiumNpcClass* const Cls = ElysiumNpcTestCensus::Find(Name);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Name), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 245 with the shared body"), Name),
				FString(ElysiumNpcTestCensus::BodyOf(Cls, 245)), FString(TEXT("0x103a49c0")));
			TestEqual(*FString::Printf(TEXT("%s fills slot 246 with the shared body"), Name),
				FString(ElysiumNpcTestCensus::BodyOf(Cls, 246)), FString(TEXT("0x103a4a60")));
		}
	}

	// A body that is NOT one of them takes neither species arm. The bare Troika line has no species
	// class, so `RetailClass()` answers null and every per-species lookup falls through to the
	// Troika line. A real cop is a species class that is not a camera: story 5 step 2,
	// `npc_VCop`'s factory 0x103704f0 builds `CNPC_VCop` (population.md), and `CNPC_VCop` does not
	// derive from either camera class, so it swallows nothing either.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_species"), 5109);
	Builder.AddTroikaNpc(TEXT("troika"), FVector::ZeroVector);
	Builder.AddNpc(TEXT("cop"), FVector(500.f, 0.f, 0.f), TEXT("npc_VCop"));
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpcOfClass(TEXT("camera"), FVector(-500.f, 0.f, 0.f), TEXT("CNPC_VCamera"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* CameraNpc = Fixture.Npc(TEXT("camera"));
	TestNotNull(TEXT("the bare Troika NPC spawned"), Troika);
	TestNotNull(TEXT("the cop spawned"), Cop);
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Troika == nullptr || Cop == nullptr || Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Troika, Cop, Guard });
	if (CameraNpc != nullptr)
	{
		FElysiumNpcWorldFixture::Quiet({ CameraNpc });
	}

	// Slot 259 through the vtable (story 5 step 3): an id no footstep or ornament arm claims (2070,
	// `0x816`, past both switches of `0x1029b290` / `0x10274e30`; the old 2040 is `0x7f8` NPC_PICKUP,
	// claimed by both at `0x1029b2ee` / `0x10275242` — corrected to retail) falls to the chain and is
	// unclaimed everywhere but on a camera, whose own empty override `0x10368ec0` swallows it.
	FElysiumAnimEvent Unclaimed;
	Unclaimed.Event = 2070;
	TestNull(TEXT("the bare Troika line has no species class"), Troika->RetailClass());
	TestFalse(TEXT("so it swallows no anim events"), Troika->HandleAnimEvent(Unclaimed));
	TestTrue(TEXT("npc_VCop builds CNPC_VCop"),
		Cop->RetailClass() == ElysiumNpcTestCensus::Find(TEXT("CNPC_VCop")));
	TestFalse(TEXT("a cop is not a camera, so it swallows none either"),
		Cop->HandleAnimEvent(Unclaimed));
	TestFalse(TEXT("and neither does the ordinary combatant"), Guard->HandleAnimEvent(Unclaimed));
	if (TestNotNull(TEXT("the camera spawned"), CameraNpc))
	{
		TestTrue(TEXT("a CNPC_VCamera swallows it"), CameraNpc->HandleAnimEvent(Unclaimed));
	}

	// The extra-model pair's forwarding gate, "the owner IS the player". A freshly spawned NPC has no
	// owner. The two bodies themselves are `FElysiumNpcPlayerController`'s overrides since story 5
	// fold A2 (`Elysium.Arm.NpcKernelPlayerController.ForwardingToOwner`).
	TestFalse(TEXT("a spawned NPC is not owned by the player"), Guard->OwnerIsThePlayer());
	Guard->SetOwnerEntity(Fixture.Player()->Handle);
	TestTrue(TEXT("an NPC owned by the player is"), Guard->OwnerIsThePlayer());
	return true;
}

// --- The sequence speed words (spec 0002 V4a) ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimSpeedWordsTest,
	"Elysium.Arm.NpcKernelAnim.SpeedWords", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimSpeedWordsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_speed_words"), 5131);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// Two fixture rows of the sequence bridge, their descriptor facts written by hand (the record
	// the embodiment fills from the baked clip data): a plain clip turning 90 degrees over 2 s at
	// 50 cm/s, and a two-cell `move_yaw` fan (-180 and +180) of 100 and 200 cm/s.
	const int32 Plain = Guard->SequenceRowFor(TEXT("bank"), TEXT("turn_left"), false);
	const int32 Fan = Guard->SequenceRowFor(TEXT("bank"), TEXT("walk"), true);
	const int32 Still = Guard->SequenceRowFor(TEXT("bank"), TEXT("still"), false);
	Guard->SequenceRows[Plain].Seconds = 2.f;
	Guard->SequenceRows[Fan].Seconds = 1.f;
	Guard->SequenceDescriptorRows.SetNum(Guard->SequenceRows.Num());
	{
		FElysiumNpcBase::FSequenceDescriptorRow& Row = Guard->SequenceDescriptorRows[Plain];
		Row.bAsked = true;
		Row.bKnown = true;
		Row.DurationSeconds = 2.f;
		Row.TurnYawDegrees = 90.f;
		Row.GroundSpeedCm = 50.f;
	}
	{
		FElysiumNpcBase::FSequenceDescriptorRow& Row = Guard->SequenceDescriptorRows[Fan];
		Row.bAsked = true;
		Row.bKnown = true;
		Row.bStudioLooping = true;
		Row.DurationSeconds = 1.f;
		Row.FanCells = 2;
		Row.FanAxisMin = -180.f;
		Row.FanAxisMax = 180.f;
		Row.FanSpeedCm[0] = 100.f;
		Row.FanSpeedCm[1] = 200.f;
		Row.FanTurnYawDegrees[0] = 10.f;
		Row.FanTurnYawDegrees[1] = 30.f;
		Row.FanParameter = TEXT("move_yaw");
	}
	{
		FElysiumNpcBase::FSequenceDescriptorRow& Row = Guard->SequenceDescriptorRows[Still];
		Row.bAsked = true;
		Row.bKnown = true;
		Row.TurnYawDegrees = 90.f;   // a turn yaw over a zero duration
	}

	// `ResetSequenceInfo 0x10090950`: both words, playback rate 1.0, `m_flLastEventCheck = 0`.
	Guard->LastEventCheck = 0.7f;
	Guard->SequencePlaybackRate = 3.f;
	Guard->CommitForcedSequence(Plain);
	TestEqual(TEXT("ResetSequenceInfo writes +0x560 = turn yaw / duration (0x10091310)"),
		Guard->YawSpeed, 45.f, 0.001f);
	TestEqual(TEXT("and +0x654 = the sequence's ground speed (0x10091490), cm/s"),
		Guard->GroundSpeed, 50.f, 0.001f);
	TestEqual(TEXT("and the playback rate 1.0 (0x10090a23)"), Guard->SequencePlaybackRate, 1.f);
	TestEqual(TEXT("and zeroes m_flLastEventCheck (0x10090a3d)"), Guard->LastEventCheck, 0.f);
	// Slots 242 `0x100916a0` and 248 `0x10091740`: plain reads, no playback-rate term.
	Guard->SequencePlaybackRate = 2.f;
	TestEqual(TEXT("GetIdealYawSpeed is the word"), Guard->GetIdealYawSpeed(), Guard->YawSpeed);
	TestEqual(TEXT("GetIdealSpeed is the word, with no playback term"), Guard->GetIdealSpeed(),
		Guard->GroundSpeed);
	TestEqual(TEXT("and GroundSpeedCm reads the same word"), Guard->GroundSpeedCm(),
		Guard->GroundSpeed);
	Guard->SequencePlaybackRate = 1.f;

	// `GetSequenceYawSpeed 0x10091310`: a zero duration answers 0 (the row never played and its
	// descriptor states no length).
	Guard->SequenceNumber = Still;
	Guard->WriteSequenceSpeedWords();
	TestEqual(TEXT("a zero duration answers a zero yaw speed"), Guard->YawSpeed, 0.f);

	// The fan: pose-weighted over its corners at the kernel's own `move_yaw`.
	Guard->PoseParameterWrites.Add(FElysiumNpc::FPoseParameterWrite{ FString(TEXT("move_yaw")), 0.f });
	Guard->CommitForcedSequence(Fan);
	TestEqual(TEXT("the fan at move_yaw 0 is the corners' mean"), Guard->GroundSpeed, 150.f, 0.001f);
	TestEqual(TEXT("and so is its turn yaw over its 1 s"), Guard->YawSpeed, 20.f, 0.001f);
	TestTrue(TEXT("the row's loop bit is the descriptor's STUDIO_LOOPING"), Guard->SequenceLoops(Fan));
	TestFalse(TEXT("and a non-looping descriptor answers false"), Guard->SequenceLoops(Plain));

	// `StudioFrameAdvance 0x1008f120`: every real advance rewrites both words at the live pose
	// (`0x1008f2e5`, `0x1008f2fa`) and writes past-half from the real cycle (`0x1008f268`).
	Guard->PoseParameterWrites.Add(FElysiumNpc::FPoseParameterWrite{ FString(TEXT("move_yaw")), -90.f });
	Guard->PrevAnimTime = 0.f;         // re-seeds the clock: the advance is 0.1 s
	Guard->SequenceCycle = 0.55f;
	Guard->SequencePastHalf = false;
	const float Advance = Guard->StudioFrameAdvance(0.f);
	TestEqual(TEXT("a real advance answers dt (0x1008f321)"), Advance, 0.1f, 0.0001f);
	TestEqual(TEXT("and re-reads the fan at the new move_yaw"), Guard->GroundSpeed, 125.f, 0.001f);
	TestEqual(TEXT("and the yaw speed with it"), Guard->YawSpeed, 15.f, 0.001f);
	TestTrue(TEXT("and writes past-half from the real cycle"), Guard->SequencePastHalf);

	// The early-out (`0x1008f1e9..0x1008f209`): a second advance in the same tick has `dt <= 0.001`,
	// answers 0.0 and writes nothing -- neither speed word, not the cycle, not past-half.
	Guard->PoseParameterWrites.Add(FElysiumNpc::FPoseParameterWrite{ FString(TEXT("move_yaw")), 0.f });
	const float CycleBefore = Guard->SequenceCycle;
	Guard->SequencePastHalf = false;
	// The fixture's clock stands at 0.0, so the first advance left `m_flPrevAnimTime` (+0x170) at
	// 0 and the first-call re-seed (`0x1008f192`: `prev == 0` -> both words = curtime) would take
	// this call for a first one. At any curtime above 0 the word is non-zero after an advance;
	// state it so.
	Guard->PrevAnimTime = Guard->AnimTime;
	const float Inert = Guard->StudioFrameAdvance(0.f);
	TestEqual(TEXT("the inert advance answers 0.0"), Inert, 0.f);
	TestEqual(TEXT("and leaves the ground speed word"), Guard->GroundSpeed, 125.f, 0.001f);
	TestEqual(TEXT("and the yaw speed word"), Guard->YawSpeed, 15.f, 0.001f);
	TestEqual(TEXT("and the cycle"), Guard->SequenceCycle, CycleBefore);
	TestFalse(TEXT("and the past-half byte"), Guard->SequencePastHalf);
	return true;
}

// --- N19: a missed lookup plays the model's sequence 0 (J1) -------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimSequenceZeroTest,
	"Elysium.Arm.NpcKernelAnim.SequenceZero", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimSequenceZeroTest::RunTest(const FString&)
{
	const TCHAR* const JackModel = TEXT("models/character/npc/unique/jack/Jack.mdl");

	// A body whose embodiment answers a `RawIndex 0` clip: a looping 2 s clip.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_sequence_zero"), 5132);
		Builder.AddNpc(TEXT("jack")).Keys.Add(TEXT("model"), JackModel);
		FElysiumNpcWorldFixture Fixture(MoveTemp(Builder), [](FElysiumRecordingServices& Services)
		{
			FElysiumRecordingServices::FRawIndexClip Zero;
			Zero.Label = TEXT("first_sequence");
			Zero.Clip.Owner = TEXT("bank");
			Zero.Clip.RawIndex = 0;
			Zero.Clip.Flags = 1;   // STUDIO_LOOPING
			Services.BodyClipsByRawIndex.Add(0, Zero);
			Services.ClipSeconds = 2.f;
		});
		FElysiumNpc* Jack = Fixture.Npc(TEXT("jack"));
		TestNotNull(TEXT("jack spawned"), Jack);
		if (Jack == nullptr || !TestNotNull(TEXT("with a body"), Jack->GetSkeletalBody()))
		{
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Jack });

		// `StartSequence 0x101a82d0`'s miss: `m_nSequence := 0` (`0x101a833d`), then
		// `ResetSequenceInfo 0x10090950`.
		Jack->SequencePlaybackRate = 3.f;
		Jack->CommitForcedSequence(0);
		TestTrue(TEXT("row 0 plays the body's RawIndex 0 clip with the clip's own loop bit"),
			Fixture.Services.Log().Contains(TEXT("first_sequence loop=1")));
		TestEqual(TEXT("at playback rate 1.0 (0x10090a23)"), Jack->SequencePlaybackRate, 1.f);
		TestTrue(TEXT("m_bSequenceLoops is the clip's STUDIO_LOOPING"), Jack->bSequenceLoopedOnce);
		TestTrue(TEXT("and the row accessor answers the same bit"), Jack->SequenceLoops(0));
		TestEqual(TEXT("the cycle rate is 1 / the clip's length"), Jack->SequenceCycleRate, 0.5f,
			0.0001f);
		TestEqual(TEXT("the trace still names it seq 0"), Jack->TraceSequenceName(0),
			FString(TEXT("seq 0")));

		// It finishes at its length: 0.1 s short of the end is not finished, the next advance is.
		Jack->PrevAnimTime = 0.f;
		Jack->SequenceCycle = 0.5f;
		Jack->StudioFrameAdvance(0.f);
		TestFalse(TEXT("mid-clip the sequence is not finished"), Jack->bSequenceFinished);
		Jack->PrevAnimTime = 0.f;
		Jack->SequenceCycle = 0.96f;
		Jack->StudioFrameAdvance(0.f);
		TestTrue(TEXT("StudioFrameAdvance raises the finish at the clip's length"),
			Jack->bSequenceFinished);
	}

	// A body answering none keeps today's row 0: nothing plays, and the zero-length sequence
	// finishes on the first advance (`GetSequenceCycleRate`'s 10.0, `0x100912c8`).
	{
		FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_sequence_zero_none"), 5133);
		Builder.AddNpc(TEXT("jack")).Keys.Add(TEXT("model"), JackModel);
		FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
		FElysiumNpc* Jack = Fixture.Npc(TEXT("jack"));
		TestNotNull(TEXT("the second jack spawned"), Jack);
		if (Jack == nullptr)
		{
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Jack });
		Jack->CommitForcedSequence(0);
		TestFalse(TEXT("a body answering no RawIndex 0 clip has no sequence-zero clip"),
			Jack->SequenceZero.bKnown);
		TestFalse(TEXT("so row 0 does not loop"), Jack->bSequenceLoopedOnce);
		TestEqual(TEXT("and stays the zero-length sequence"), Jack->SequenceCycleRate, 10.f);
		Jack->PrevAnimTime = 0.f;
		Jack->SequenceCycle = 0.f;
		Jack->StudioFrameAdvance(0.f);
		TestTrue(TEXT("which finishes on the first advance"), Jack->bSequenceFinished);
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
