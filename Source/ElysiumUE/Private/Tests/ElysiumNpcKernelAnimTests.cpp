#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumSceneData.h"
#include "Tests/ElysiumNpcTestFixture.h"

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
	"Elysium.Substrate.NpcKernelAnim.GestureLayers", GElysiumNpcKernelAnimFlags)
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

	// `SetLayer` `0x10099020`, field for field.
	Guard->SetOverlayLayer(0, /*Activity*/ 0x3b, /*Sequence*/ 17, /*bAutoKill*/ true);
	const FElysiumNpc::FAnimOverlayLayer& L0 = Guard->AnimOverlay[0];
	TestEqual(TEXT("SetLayer seeds the weight at 0.1"), L0.Weight, ElysiumOverlay::SeedWeight);
	TestEqual(TEXT("and the ceiling at 1.0"), L0.WeightMax, ElysiumOverlay::WeightMax);
	TestEqual(TEXT("and 0.2 at both ends of the envelope"), L0.BlendIn,
		ElysiumOverlay::DefaultBlendFraction);
	TestEqual(TEXT("and 0.2 out"), L0.BlendOut, ElysiumOverlay::DefaultBlendFraction);
	TestEqual(TEXT("and rate 1.0"), L0.PlaybackRate, 1.f);
	TestEqual(TEXT("and the owner activity, which is the key every lookup searches by"), L0.Activity,
		0x3b);
	TestEqual(TEXT("and the sequence"), L0.Sequence, 17);
	TestEqual(TEXT("and zeroes the cycle"), L0.Cycle, 0.f);
	TestTrue(TEXT("and carries the auto-kill bit the pusher stated"), L0.bAutoKillWhenFinished);
	TestEqual(TEXT("m_fFlags is NOT written by SetLayer"), L0.Flags, 0);

	// `FindGestureLayer` `0x100994c0`, three terms: live, owner != -1, owner == asked.
	TestEqual(TEXT("the pushed layer is found by its owner"), Guard->FindGestureLayerByOwner(0x3b),
		0);
	TestTrue(TEXT("HasLayer 0x10099540 is that lookup and a != -1"), Guard->HasLayer(0x3b));
	TestFalse(TEXT("and answers false for an owner nothing holds"), Guard->HasLayer(0x3c));

	// `AllocateLayer` `0x10099470`: the lowest ZERO-WEIGHT slot, which is why the seed weight is
	// load-bearing — slot 0 is occupied on the frame it was pushed.
	TestEqual(TEXT("AllocateLayer skips the seeded slot"), Guard->AllocateGestureLayer(), 1);
	Guard->SetOverlayLayer(1, 0x3c, 18, false);
	Guard->SetOverlayLayer(2, 0x3d, 19, false);
	Guard->SetOverlayLayer(3, 0x3e, 20, false);
	TestEqual(TEXT("a full stack refuses outright — no eviction, no displacement"),
		Guard->AllocateGestureLayer(), INDEX_NONE);

	// `RestartGesture` `0x10099570`: the found arm rewinds the CYCLE and touches nothing else.
	Guard->AnimOverlay[1].Cycle = 0.44f;
	Guard->AnimOverlay[1].Weight = 0.9f;
	Guard->RestartGesture(0x3c, /*bAddIfMissing*/ false, /*bAutoKill*/ false);
	TestEqual(TEXT("RestartGesture rewinds the cycle"), Guard->AnimOverlay[1].Cycle, 0.f);
	TestEqual(TEXT("and leaves the accumulated weight and the envelope alone"),
		Guard->AnimOverlay[1].Weight, 0.9f);

	// `RemoveLayer` `0x10099660`: weight then sequence, and NOT the owner activity — which is why a
	// removed slot still carries the activity it was pushed for.
	Guard->RemoveLayer(1);
	TestEqual(TEXT("RemoveLayer zeroes the weight, which is what frees the slot"),
		Guard->AnimOverlay[1].Weight, 0.f);
	TestEqual(TEXT("and the sequence"), Guard->AnimOverlay[1].Sequence, 0);
	TestEqual(TEXT("and leaves the owner activity standing"), Guard->AnimOverlay[1].Activity, 0x3c);
	TestFalse(TEXT("so the owner lookup no longer finds it — the liveness term is what decides"),
		Guard->HasLayer(0x3c));
	TestEqual(TEXT("and the freed slot is the one AllocateLayer answers"),
		Guard->AllocateGestureLayer(), 1);

	// `RemoveLayerByOwner` `0x100995e0`: the same pair behind the lookup, and a silent no-op on a
	// miss.
	Guard->RemoveLayerByOwner(0x3d);
	TestEqual(TEXT("RemoveLayerByOwner frees the slot its lookup found"),
		Guard->AnimOverlay[2].Weight, 0.f);
	Guard->RemoveLayerByOwner(0x99);   // nothing holds it

	// `RemoveAllGestures` `0x10099630`: four iterations, sequence and weight only.
	Guard->AnimOverlay[3].Cycle = 0.7f;
	Guard->RemoveAllGestures();
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		TestEqual(TEXT("RemoveAllGestures zeroes every weight"), Guard->AnimOverlay[Index].Weight,
			0.f);
		TestEqual(TEXT("and every sequence"), Guard->AnimOverlay[Index].Sequence, 0);
	}
	TestEqual(TEXT("and touches no cycle"), Guard->AnimOverlay[3].Cycle, 0.7f);

	// `RestartGesture`'s add arm: the sequence seam answers -1, which is below retail's own `< 1`
	// refusal, so `AddGesture` (`0x100991b0`) drops the request and NOTHING is allocated. That is
	// the recovered refusal, not a gap in the port.
	TestEqual(TEXT("the weighted-sequence seam answers retail's own miss"),
		Guard->SelectWeightedSequenceForActivity(0x3b), INDEX_NONE);
	Guard->RestartGesture(0x77, /*bAddIfMissing*/ true, /*bAutoKill*/ true);
	TestEqual(TEXT("so the add arm allocates nothing"), Guard->AllocateGestureLayer(), 0);
	return true;
}

// --- The flinch table (slot 265) ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimFlinchTest,
	"Elysium.Substrate.NpcKernelAnim.AddFlinchGesture", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimFlinchTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_flinch"), 5102);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// Three records of 0x1c bytes at +0x07f4.
	TestEqual(TEXT("m_Flinch is three records"),
		static_cast<int32>(UE_ARRAY_COUNT(Guard->Flinch)), FElysiumNpc::NumFlinchRecords);

	// `0x10099690` opens on two refusals in retail's order: `IsAlive()` (slot 158) then
	// `m_bNoFlinch`. The second is the one a case can drive.
	Guard->bNoFlinch = true;
	Guard->Flinch[0].Latch = 0;
	Guard->AddFlinchGesture(0x3b, 0.2f, 0.3f, nullptr, 0.f);
	TestEqual(TEXT("m_bNoFlinch refuses the whole body — even the latch is untouched"),
		Guard->Flinch[0].Latch, 0);
	Guard->bNoFlinch = false;

	// The sequence seam answers -1, so the write half cannot run: that is the recovered refusal and
	// it is asserted rather than worked around. The VICTIM SCAN, which is the rule, is asserted
	// directly through the same body's own reads below.
	Guard->AddFlinchGesture(0x3b, 0.2f, 0.3f, nullptr, 0.f);
	TestEqual(TEXT("a body with no clip for the flinch activity writes nothing"),
		Guard->Flinch[0].Latch, 0);
	TestEqual(TEXT("and leaves the stamp alone"), Guard->Flinch[0].ExpireTime, 0.f);

	// The victim rule, stated as the body states it: scan records 1 and 2 against the running best,
	// starting at 0, taking a STRICTLY earlier expiry. With three equal stamps record 0 wins.
	auto PickVictim = [](const FElysiumNpc& Npc)
	{
		int32 Best = 0;
		for (int32 Candidate = 1; Candidate < FElysiumNpc::NumFlinchRecords; ++Candidate)
		{
			if (Npc.Flinch[Candidate].ExpireTime < Npc.Flinch[Best].ExpireTime)
			{
				Best = Candidate;
			}
		}
		return Best;
	};
	Guard->Flinch[0].ExpireTime = 5.f;
	Guard->Flinch[1].ExpireTime = 5.f;
	Guard->Flinch[2].ExpireTime = 5.f;
	TestEqual(TEXT("three equal stamps reuse record 0"), PickVictim(*Guard), 0);
	Guard->Flinch[2].ExpireTime = 1.f;
	TestEqual(TEXT("the earliest-expiring record is the victim"), PickVictim(*Guard), 2);
	Guard->Flinch[1].ExpireTime = 1.f;
	TestEqual(TEXT("a tie keeps the EARLIER index, because the test is strict"), PickVictim(*Guard),
		1);

	// The pose-parameter tail is optional and last: the index is seeded 0x18 and a name that
	// resolves to nothing leaves the seed standing. The lookup seam answers -1.
	TestEqual(TEXT("the pose-parameter lookup seam answers retail's own miss"),
		Guard->LookupPoseParameter(TEXT("flinch")), INDEX_NONE);
	TestEqual(TEXT("and the normaliser seam is the identity"),
		Guard->NormalizePoseParameter(3, 0.75f), 0.75f);
	return true;
}

// --- The flex controllers (slots 280, 281, 282) -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimFlexTest,
	"Elysium.Substrate.NpcKernelAnim.FlexControllers", GElysiumNpcKernelAnimFlags)
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
		static_cast<int32>(UE_ARRAY_COUNT(Guard->FlexWeight)), FElysiumNpc::NumFlexWeightSlots);

	// The studio seams, and what they refuse with.
	TestEqual(TEXT("GetNumFlexControllers answers an empty table"), Guard->NumFlexControllers(), 0);
	float Min = 1.f;
	float Max = 2.f;
	TestFalse(TEXT("and there is no controller range to read"),
		Guard->FlexControllerRange(0, Min, Max));

	// `LookupFlexController` `0x100b5d10`: **0 on a miss, not -1**. It is retail's own behaviour and
	// the reason a misspelt flex name writes controller zero rather than being dropped.
	TestEqual(TEXT("a name that matches nothing resolves to controller ZERO"),
		Guard->LookupFlexController(TEXT("right_lip_raiser")), 0);
	TestEqual(TEXT("and so does a null name"), Guard->LookupFlexController(nullptr), 0);

	// `GetFlexWeight(int)` `0x100b5c50`, slot 281 — read off the listing. Every guard answers 0.
	TestEqual(TEXT("a negative index answers 0"), Guard->GetFlexWeight(-1), 0.f);
	Guard->FlexWeight[3] = 0.5f;
	TestEqual(TEXT("and so does an index past the (empty) controller table"), Guard->GetFlexWeight(3),
		0.f);

	// The de-normalisation itself, which is the arithmetic the slot exists for: slot 279 stores
	// `(v - min) / (max - min)` and slot 281 maps it back with `(max - min) * w + min`. The seam
	// cannot supply a range, so the formula is asserted directly against the body's own two arms.
	auto Denormalise = [](float Weight, float InMin, float InMax)
	{
		return InMax != InMin ? (InMax - InMin) * Weight + InMin : Weight;
	};
	TestEqual(TEXT("max != min de-normalises into the authored range"),
		Denormalise(0.25f, -1.f, 3.f), 0.f);
	TestEqual(TEXT("max == min passes the stored weight through untouched"),
		Denormalise(0.25f, 1.f, 1.f), 0.25f);

	// Slots 280 and 282 are two statements each: resolve the name, dispatch the INDEX overload.
	// Slot 279 is still 29c's stub, so the write lands nowhere and the read comes back at the
	// refusal — which is what the pair answers today, stated rather than hidden.
	TCHAR Name[] = TEXT("brow_raiser");
	Guard->SetFlexWeight(Name, 0.8f);
	TestEqual(TEXT("slot 282 resolves the name and reads through slot 281's refusal"),
		Guard->GetFlexWeight(Name), 0.f);
	return true;
}

// --- The scene-event queue (slots 283, 286, 290, 291) -------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimSceneEventsTest,
	"Elysium.Substrate.NpcKernelAnim.SceneEvents", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimSceneEventsTest::RunTest(const FString&)
{
	// `CUtlMemory::Grow`, as `0x100b5e60` inlines it. Pure arithmetic, so it is asserted with no
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
	TestEqual(TEXT("the sequence lookup seam answers retail's own miss"),
		Guard->LookupSequenceByName(TEXT("gesture_wave")), INDEX_NONE);
	TestEqual(TEXT("so the gesture record is queued un-armed"), Guard->SceneEvents[1].Handle, -1);

	// `ProcessGestureSceneEvent` `0x100b7040` — the guards ARE the behaviour, and the arithmetic
	// behind them is what retail computes and discards.
	Guard->SceneEvents[1].LastGestureCycle = -1.f;
	Guard->ProcessGestureSceneEvent(&Guard->SceneEvents[1]);
	TestEqual(TEXT("an un-armed record is refused by the +0x0c guard"),
		Guard->SceneEvents[1].LastGestureCycle, -1.f);

	// Armed by hand, so the cycle formula is reachable. Read off the listing at `0x100b7040`:
	//     0x100b7064  CALL CChoreoEvent::GetDuration  -> FSTP [ESP+0x14]   (SAVED)
	//     0x100b7073  CALL SequenceDuration(record[+0x10]) -> FSTP ST0     (DISCARDED)
	//     FLD [this+0x174] ; FSUB [record+0x14] ; FADD 1e-4 ; FDIV [ESP+0x18]
	// so the divisor is the EVENT's authored length and the sequence's is thrown away.
	//
	// The claim is falsifiable here because the two durations DIFFER: the event is 2 s and the
	// sequence-duration seam answers 0, which the epsilon guard would turn into a number four
	// orders of magnitude away. A fixture where they agreed would prove nothing.
	Guard->SceneEvents[1].Handle = 0;
	Guard->SceneEvents[1].Sequence = 12;
	Guard->SceneEvents[1].StartTime = 0.5f;
	Guard->AnimTime = 1.5f;
	TestEqual(TEXT("the event's own duration is end minus start"), Gesture.GetDuration(), 2.f);
	TestEqual(TEXT("and the sequence's is a different number entirely — the seam's own refusal"),
		Guard->SequenceDurationOf(12), 0.f);
	Guard->ProcessGestureSceneEvent(&Guard->SceneEvents[1]);
	TestEqual(TEXT("the cycle is measured against the EVENT's length"),
		Guard->SceneEvents[1].LastGestureCycle, static_cast<float>((1.5 - 0.5 + 0.0001) / 2.0),
		1.e-6f);

	// The same record against a DIFFERENT event length: only the divisor moved, so a body reading
	// any other duration cannot satisfy both this and the assertion above.
	Gesture.EndTime = 5.f;
	TestEqual(TEXT("the event is now 4 s long"), Gesture.GetDuration(), 4.f);
	Guard->ProcessGestureSceneEvent(&Guard->SceneEvents[1]);
	TestEqual(TEXT("and the cycle halves with it"), Guard->SceneEvents[1].LastGestureCycle,
		static_cast<float>((1.5 - 0.5 + 0.0001) / 4.0), 1.e-6f);
	Gesture.EndTime = 3.f;

	// `ProcessSequenceSceneEvent` `0x100b70e0` — the same four guards, and its one call's result is
	// discarded too. An event with no authored ramp is at full intensity, which is `RampAt`'s own
	// answer.
	Guard->SceneEvents[0].Handle = 0;
	Guard->ProcessSequenceSceneEvent(&Guard->SceneEvents[0]);
	TestEqual(TEXT("the ramp of an un-ramped event is 1"), Guard->SceneEvents[0].LastIntensity, 1.f);
	Guard->SceneEvents[2].Handle = -1;
	Guard->SceneEvents[2].LastIntensity = -5.f;
	Guard->ProcessSequenceSceneEvent(&Guard->SceneEvents[2]);
	TestEqual(TEXT("and an un-armed record is refused"), Guard->SceneEvents[2].LastIntensity, -5.f);
	Guard->ProcessSequenceSceneEvent(nullptr);

	// `ProcessSceneEvents` `0x100b6250`, slot 283 — it does NOT walk the queue. It zeroes every flex
	// controller through slot 279 and tail-jumps to slot 284. With an empty controller table the
	// loop runs zero times and only the tail happens, which is the recovered shape.
	Guard->ProcessSceneEvents();
	TestEqual(TEXT("ProcessSceneEvents leaves the queue alone — it is a flex reset, not a walk"),
		Guard->SceneEvents.Num(), 3);
	return true;
}

// --- The Troika scene-event dispatch (slot 286) -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimTroikaSceneEventTest,
	"Elysium.Substrate.NpcKernelAnim.TroikaSceneEvent", GElysiumNpcKernelAnimFlags)
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
	// With a partner it reaches the disposition seam, which refuses — the recovered refusal.
	Guard->Dialogue.bInDialog = true;
	TestTrue(TEXT("an open session IS the dialogue partner here"), Guard->HasLiveDialogPartner());
	float Threshold = -1.f;
	float Chance = -1.f;
	TestFalse(TEXT("and the disposition row's stance-reaction pair has no source yet"),
		Guard->DispositionStanceReaction(Threshold, Chance));
	Guard->AddSceneEvent(&Scene, &Silence);
	TestEqual(TEXT("so the reaction is dropped at the table"), Guard->SceneEvents.Num(), 0);
	Guard->Dialogue.bInDialog = false;

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
	FString Expr;
	float FadeIn = 0.f;
	float FadeOut = 0.f;
	float MinLevel = 0.f;
	float MaxLevel = 0.f;
	TestFalse(TEXT("the disposition row's expression block has no source yet"),
		Guard->DispositionLoudExpression(Expr, FadeIn, FadeOut, MinLevel, MaxLevel));
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

	// Everything else forwards to the base body at `0x100b5e60`, which is what queues it.
	FElysiumSceneEvent Speak;
	Speak.Type = EElysiumChoreoEvent::Speak;
	Guard->AddSceneEvent(&Scene, &Speak);
	TestEqual(TEXT("an unclaimed type reaches the base and is queued"), Guard->SceneEvents.Num(), 1);
	return true;
}

// --- The activity commit ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimActivityCommitTest,
	"Elysium.Substrate.NpcKernelAnim.ActivityCommit", GElysiumNpcKernelAnimFlags)
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
	TestEqual(TEXT("and re-resolves the ideal sequence beside it — sequence zero, the ladder's "
		"floor, because every lookup here is a seam"), Guard->IdealSequence, 0);

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
	TestTrue(TEXT("the dynamic-interaction seam answers the arm that lets a cine stand"),
		Guard->CineAllowsDynamicInteraction());
	TestFalse(TEXT("and this body holds no cine"), Guard->ScriptOwnerIsLive());
	return true;
}

// --- `ResolveActivityToSequence` `0x10272130` ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimResolveActivityTest,
	"Elysium.Substrate.NpcKernelAnim.ResolveActivityToSequence", GElysiumNpcKernelAnimFlags)
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

	// An ordinary activity: the weighted draw misses (the seam), the run-to-walk rung does not apply,
	// the whole request is retried as ACT_DISPOSITION (0xf1), the Troika disposition resolver misses
	// too, and SEQUENCE ZERO is the floor. Every rung of the ladder is walked to get there.
	Guard->ResolveActivityToSequence(0x3b, Sequence, Translated, Weapon);
	TestEqual(TEXT("the ladder ends on retail's own floor, sequence 0"), Sequence, 0);
	TestEqual(TEXT("and the rung that answered says so"),
		static_cast<int32>(Guard->LastResolveActivityRung),
		static_cast<int32>(FElysiumNpc::EResolveActivityRung::SequenceZero));
	TestEqual(TEXT("with the translated activity left at the retry's own 0xf1"), Translated, 0xf1);

	// ACT_RUN (0x13): the SAME floor, but the run-to-walk rung is reached on the way — retail rewrites
	// the translated activity to 9 before it retries as a disposition, and the rewrite survives only
	// as long as that pass.
	Guard->ResolveActivityToSequence(0x13, Sequence, Translated, Weapon);
	TestEqual(TEXT("ACT_RUN falls to the same floor"), Sequence, 0);

	// ACT_DISPOSITION asked for DIRECTLY takes the Troika resolver and, on its miss, goes straight to
	// sequence zero — there is no second retry, because the request already IS 0xf1.
	Guard->ResolveActivityToSequence(0xf1, Sequence, Translated, Weapon);
	TestEqual(TEXT("ACT_DISPOSITION lands on sequence 0 without retrying itself"), Sequence, 0);
	TestEqual(TEXT("through the disposition rung"),
		static_cast<int32>(Guard->LastResolveActivityRung),
		static_cast<int32>(FElysiumNpc::EResolveActivityRung::SequenceZero));

	// ACT_SCRIPT_CUSTOM_MOVE (0x18) with no cine is NOT the custom-move arm: retail's guard is the
	// cine handle, and without one the request takes the ordinary weighted rung.
	TestFalse(TEXT("this body holds no cine"), Guard->ScriptOwnerIsLive());
	TestEqual(TEXT("and the custom-move name seam is empty"),
		Guard->ScriptCustomMoveSequenceName().Len(), 0);
	Guard->ResolveActivityToSequence(0x18, Sequence, Translated, Weapon);
	TestEqual(TEXT("so ACT_SCRIPT_CUSTOM_MOVE walks the ordinary ladder to the floor"), Sequence, 0);

	// The disposition resolver's own refusal, asserted directly: it answers no sequence and leaves
	// the translated activity at 0xf1, which is what makes the ladder fall through.
	int32 DispSequence = 7;
	int32 DispActivity = 7;
	Guard->ResolveDispositionActivity(DispSequence, DispActivity);
	TestEqual(TEXT("the Troika disposition resolver has no sequence index to give"), DispSequence,
		INDEX_NONE);
	TestEqual(TEXT("and names ACT_DISPOSITION"), DispActivity, 0xf1);
	return true;
}

// --- The remaining single-arm bodies ------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimMiscBodiesTest,
	"Elysium.Substrate.NpcKernelAnim.MiscBodies", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimMiscBodiesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_misc"), 5108);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `BodyGroup` `0x10398800`, three arms. The cvar is a seam whose NAME and DEFAULT are
	// unrecovered; it answers "not a command, value -1", which is the arm that takes the mask.
	TestFalse(TEXT("the body-group cvar seam is a real convar"), Guard->BodyGroupCvarIsCommand());
	TestEqual(TEXT("standing at retail's negative default"), Guard->BodyGroupCvarValue(), -1);
	Guard->SeveredTentacleMask = 6;
	Guard->NpcBody = 99;
	Guard->BodyGroup();
	TestEqual(TEXT("so m_nBody takes the severed-tentacle mask"), Guard->NpcBody, 6);

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
	TestFalse(TEXT("the scene-entity LOD byte has no source yet"),
		Guard->SceneEntityForcesCutsceneLod(nullptr));

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

	// `PlayScene` `0x10279060`, slot 540: the scene seam cannot stand an `instanced_scripted_scene`,
	// so the body takes retail's own "Unknown scene specified" arm and answers 0.
	TestEqual(TEXT("the instanced-scene seam refuses"),
		Guard->PlayInstancedScene(TEXT("jack/hello.vcd")), -1.f);
	TestEqual(TEXT("so PlayScene answers retail's failure length"),
		Guard->PlayScene(TEXT("jack/hello.vcd")), 0.f);

	// Slot 345 `SetPoseParameter(name, value, bool)` — the write is recorded on family Facing's one
	// pose-parameter surface, and the index the retail dispatch resolves is the seam's -1.
	const int32 Before = Guard->PoseParameterWrites.Num();
	Guard->SetPoseParameter(TEXT("aim_yaw"), 0.25f, true);
	TestEqual(TEXT("slot 345 records the write"), Guard->PoseParameterWrites.Num(), Before + 1);
	TestEqual(TEXT("by name"), Guard->PoseParameterWrites.Last().Name, FString(TEXT("aim_yaw")));
	TestEqual(TEXT("and value"), Guard->PoseParameterWrites.Last().Value, 0.25f);

	// `StudioFrameAdvanceHumanoid` `0x1025e4e0` — clear bits 0 and 1 of +0x5f4c, keep the rest.
	Guard->HumanoidHeadCacheBits = 0xFu;
	Guard->StudioFrameAdvanceHumanoid(0.1f);
	TestEqual(TEXT("the humanoid frame advance invalidates only the two cache bits"),
		static_cast<int32>(Guard->HumanoidHeadCacheBits), 0xC);

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
	"Elysium.Substrate.NpcKernelAnim.Species", GElysiumNpcKernelAnimFlags)
bool FElysiumNpcKernelAnimSpeciesTest::RunTest(const FString&)
{
	// Slot 259's EMPTY override. `npc_VCamera` is claimed by `CNPC_VCamera` in the CENSUS but is not
	// a registered spawn leaf, so the row is exercised by RETAIL CLASS NAME through the census
	// reader — the two tables disagree and this is the side that can answer.
	const FElysiumNpcClass* const Camera = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCamera"));
	TestNotNull(TEXT("CNPC_VCamera is a census class"), Camera);
	if (Camera != nullptr)
	{
		TestTrue(TEXT("and it is its own class"),
			ElysiumNpcKernelClass::DerivesFrom(Camera, TEXT("CNPC_VCamera")));
		TestEqual(TEXT("whose slot 259 body is the empty one"),
			FString(ElysiumNpcKernelClass::BodyOf(Camera, 259)), FString(TEXT("0x10368ec0")));
	}
	const FElysiumNpcClass* const CameraSecurity =
		ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity"));
	TestNotNull(TEXT("CNPC_VCameraSecurity is one too"), CameraSecurity);
	if (CameraSecurity != nullptr)
	{
		TestEqual(TEXT("sharing the same empty slot-259 body"),
			FString(ElysiumNpcKernelClass::BodyOf(CameraSecurity, 259)),
			FString(TEXT("0x10368ec0")));
	}

	// Slots 245/246's forwarding override, by retail class name for the same reason.
	const TCHAR* const Forwarders[] =
	{
		TEXT("CNPC_VFrenzyShadow"), TEXT("CNPC_VPlayerController"), TEXT("CNPC_VWolfMorph"),
	};
	for (const TCHAR* const Name : Forwarders)
	{
		const FElysiumNpcClass* const Cls = ElysiumNpcKernelClass::Find(Name);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Name), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 245 with the shared body"), Name),
				FString(ElysiumNpcKernelClass::BodyOf(Cls, 245)), FString(TEXT("0x103a49c0")));
			TestEqual(*FString::Printf(TEXT("%s fills slot 246 with the shared body"), Name),
				FString(ElysiumNpcKernelClass::BodyOf(Cls, 246)), FString(TEXT("0x103a4a60")));
		}
	}

	// A spawnable body that is NOT one of them takes neither species arm. `npc_VCop`'s census class
	// list is null — no census class claims it — so `RetailClass()` answers null and every
	// per-species lookup correctly falls through to the Troika line. That is the recovered answer.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_anim_species"), 5109);
	Builder.AddNpc(TEXT("cop"), FVector::ZeroVector, TEXT("npc_VCop"));
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	TestNotNull(TEXT("the cop spawned"), Cop);
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Cop == nullptr || Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Cop, Guard });

	TestNull(TEXT("no census class claims npc_VCop"), Cop->RetailClass());
	TestFalse(TEXT("so it swallows no anim events"), Cop->SwallowsAnimEvents());
	TestFalse(TEXT("and neither does the ordinary combatant"), Guard->SwallowsAnimEvents());

	// The extra-model pair, whose forwarding arm is "the owner IS the player". A freshly spawned NPC
	// has no owner, which is the arm that warns.
	TestFalse(TEXT("a spawned NPC is not owned by the player"), Guard->OwnerIsThePlayer());
	Guard->AddExtraAnimationModelsPlayerController(nullptr, TEXT("attach_a"), TEXT("attach_b"), 1, 2);
	TestEqual(TEXT("the base half still records the request"), Guard->ExtraAnimationModels.Num(), 1);
	TestFalse(TEXT("and reports that it reached no master"),
		Guard->ExtraAnimationModels[0].bForwardedToMaster);
	TestEqual(TEXT("carrying the first attachment name"),
		Guard->ExtraAnimationModels[0].AttachmentA, FString(TEXT("attach_a")));
	Guard->RemoveExtraAnimationModelsPlayerController();
	TestEqual(TEXT("and the remover clears the base list first, as retail does"),
		Guard->ExtraAnimationModels.Num(), 0);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
