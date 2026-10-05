#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimatingOverlay.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumNpc.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Spec 0002 V4o, lane O1 -- the NPC's four overlay layers (`m_AnimOverlay` `+0x734`, stride 0x30).
//
// `AddGesture 0x100991b0` (with `0x100990f0`), slots 268 / 269 / 270 / 272 (`0x10099020`,
// `0x10099660`, `0x10099540`, `0x10099470`), the layer half of slot 250 (`0x10098bb0` ->
// `CAnimationLayer::StudioFrameAdvance 0x10098830`) and the per-layer dispatch of slot 258
// (`0x10098c80` -> `0x10098cd0`). Every assertion names the address of the line it reads; the rows
// are fixtures of the sequence bridge, their descriptor facts written by hand.

static constexpr EAutomationTestFlags GElysiumNpcKernelOverlayFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr int32 GOverlayActRangeAttack1Layer = 0x1a;   // ACT_RANGE_ATTACK1_LAYER

	FElysiumAnimEvent OverlayEventAt(float Cycle, int32 Event)
	{
		FElysiumAnimEvent Record;
		Record.Cycle = Cycle;
		Record.Event = Event;
		return Record;
	}

	// The HANDLER: slot 259 (`+0x40c`), recording what reached it, in order.
	class FOverlayHandlerProbe final : public FElysiumEntity
	{
	public:
		TArray<int32> Handled;

		virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override
		{
			Handled.Add(Event.Event);
			return true;
		}

		int32 CountOf(int32 Id) const
		{
			int32 Count = 0;
			for (const int32 Seen : Handled)
			{
				Count += Seen == Id ? 1 : 0;
			}
			return Count;
		}

		FString Ids() const
		{
			FString Out;
			for (const int32 Id : Handled)
			{
				if (!Out.IsEmpty()) { Out += TEXT(","); }
				Out += FString::FromInt(Id);
			}
			return Out;
		}
	};

	// A Troika NPC recording slot 112 (`+0x1c0`), which `0x10098bb0` calls on a finished auto-kill
	// layer with `(i, m_nActivity)`.
	class FOverlaySlot112Npc final : public FElysiumNpc
	{
	public:
		TArray<FIntPoint> Slot112Calls;

		virtual void Slot112(int32 LayerIndex, int32 LayerActivity) override
		{
			Slot112Calls.Add(FIntPoint(LayerIndex, LayerActivity));
		}
	};

	// One fixture row of the sequence bridge with its descriptor record (the record the embodiment
	// fills from the baked clip data): the length, `flags & 1`, `flags & 2`, the event table.
	int32 OverlayFixtureRow(FElysiumNpc& Npc, const TCHAR* Label, float Seconds, bool bLoops,
		bool bSnap, const TArray<FElysiumAnimEvent>* Events)
	{
		const int32 RowIndex = Npc.SequenceRowFor(TEXT("move_and_ranged"), Label, bLoops);
		Npc.SequenceRows[RowIndex].Seconds = Seconds;
		if (Npc.SequenceDescriptorRows.Num() < Npc.SequenceRows.Num())
		{
			Npc.SequenceDescriptorRows.SetNum(Npc.SequenceRows.Num());
		}
		FElysiumNpcBase::FSequenceDescriptorRow& Descriptor = Npc.SequenceDescriptorRows[RowIndex];
		Descriptor.bAsked = true;
		Descriptor.bKnown = true;
		Descriptor.bStudioLooping = bLoops;
		Descriptor.DurationSeconds = Seconds;
		Descriptor.Events = Events;
		Npc.SequenceRows[RowIndex].bSnap = bSnap;
		return RowIndex;
	}

	void OverlayResetLayers(FElysiumNpc& Npc)
	{
		for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
		{
			Npc.AnimOverlay[Index] = FElysiumAnimatingOverlay::FAnimOverlayLayer();
		}
	}
}

// --- `0x100991b0`, `0x10099470`, `0x10099020`, `0x10099540`, `0x10099660` -------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelOverlayAddGestureTest,
	"Elysium.Arm.NpcKernelOverlay.AddGesture", GElysiumNpcKernelOverlayFlags)
bool FElysiumNpcKernelOverlayAddGestureTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_overlay_add_gesture"), 5401);
	FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
	GuardDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder), [](FElysiumRecordingServices& Services)
		{
			Services.bNpcActivitiesResolve = true;
			Services.ResolvedNpcActivityLabel = TEXT("smith_attack_layer");
			Services.ResolvedNpcActivityClip = TEXT("smith_attack_layer");
			Services.ClipSeconds = 0.5f;
			Services.SeedFixtureActivity(TEXT("ACT_RANGE_ATTACK1_LAYER"), 10);
		});
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("the guard spawned"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	OverlayResetLayers(*Guard);

	// The bridge numbers the resolver's clip; row 0 is the model's sequence 0, so a resolved layer
	// clip is 1 or more and retail's `< 1` rule ports as written.
	const int32 Sequence = Guard->SelectWeightedSequenceForActivity(GOverlayActRangeAttack1Layer);
	if (!TestTrue(TEXT("0x1008dc40: the body authors a sequence for ACT_RANGE_ATTACK1_LAYER, numbered >= 1"),
			Sequence >= 1))
	{
		return false;
	}

	// The push: slot 272, slot 268 `(i, -1, seq, autokill)`, then the owner activity.
	Guard->AnimOverlay[0].Flags = 7;
	Guard->SequenceRows[Sequence].Seconds = 0.f;
	const int32 Pushed = Guard->AddGesture(GOverlayActRangeAttack1Layer, true);
	TestEqual(TEXT("0x10099470: the lowest slot whose weight is 0"), Pushed, 0);
	{
		const FElysiumAnimatingOverlay::FAnimOverlayLayer& Layer = Guard->AnimOverlay[0];
		TestEqual(TEXT("0x100991b0: `m_nActivity = act`, written after slot 268's -1"), Layer.Activity,
			GOverlayActRangeAttack1Layer);
		TestEqual(TEXT("0x10099020: `m_flCycle = 0`"), Layer.Cycle, 0.f);
		TestEqual(TEXT("0x10099020: `m_flPlaybackRate = 1.0`"), Layer.PlaybackRate, 1.f);
		TestEqual(TEXT("0x10099020: `m_nSequence = seq`"), Layer.Sequence, Sequence);
		TestEqual(TEXT("0x10099020: `m_flBlendIn = 0.2` (0x3e4ccccd)"), Layer.BlendIn, 0.2f);
		TestEqual(TEXT("0x10099020: `m_flBlendOut = 0.2`"), Layer.BlendOut, 0.2f);
		TestEqual(TEXT("0x10099020: `m_flWeight = 0.1` (0x3dcccccd), the occupancy seed"), Layer.Weight, 0.1f);
		TestEqual(TEXT("0x10099020: `m_flWeightMax = 1.0`"), Layer.WeightMax, 1.f);
		TestTrue(TEXT("0x10099020: `m_bAutoKillWhenFinished` from the caller"), Layer.bAutoKillWhenFinished);
		TestEqual(TEXT("0x10099020: `m_fSequenceFinished = 0`"), Layer.SequenceFinished, 0);
		TestEqual(TEXT("0x10099020: `m_flLastEventCheck = 0`"), Layer.LastEventCheck, 0.f);
		TestEqual(TEXT("0x10099020: `m_fFlags` is not written"), Layer.Flags, 7);
	}
	TestTrue(TEXT("0x10099540: slot 270 is `slot 271 != -1`"), Guard->HasLayer(GOverlayActRangeAttack1Layer));
	// The draw answered the row's length, the one thing the kernel reads back from the body.
	TestEqual(TEXT("the layer's draw stores the clip's length on the row"),
		Guard->SequenceRows[Sequence].Seconds, 0.5f);
	TestEqual(TEXT("0x10091230: the cycle rate is 1 / that length"),
		Guard->SequenceCycleRateOf(Sequence), 2.f, 1.e-4f);

	// A held activity answers its index and re-seeds nothing.
	Guard->AnimOverlay[0].Cycle = 0.5f;
	TestEqual(TEXT("0x100991b0: slot 270 true -> slot 271's index"),
		Guard->AddGesture(GOverlayActRangeAttack1Layer, true), 0);
	TestEqual(TEXT("0x100991b0: ...with no `SetLayer`"), Guard->AnimOverlay[0].Cycle, 0.5f);
	TestEqual(TEXT("0x10099470: the next free slot is 1"), Guard->AllocateLayer(), 1);

	// The snap bit: `GetSeqDesc(seq)->flags & 2` zeroes both blends, and only them.
	Guard->SequenceRows[Sequence].bSnap = true;
	Guard->AnimOverlay[1].Flags = 3;
	Guard->SetLayer(1, -1, Sequence, false);
	TestEqual(TEXT("0x100990aa: a SNAP sequence's `m_flBlendIn = 0`"), Guard->AnimOverlay[1].BlendIn, 0.f);
	TestEqual(TEXT("0x100990ac: ...and `m_flBlendOut = 0`"), Guard->AnimOverlay[1].BlendOut, 0.f);
	TestEqual(TEXT("0x10099020: ...the seed weight stands"), Guard->AnimOverlay[1].Weight, 0.1f);
	TestEqual(TEXT("0x10099020: ...and `m_fFlags` is untouched"), Guard->AnimOverlay[1].Flags, 3);
	TestFalse(TEXT("0x100994c0: an owner of -1 is never found"), Guard->HasLayer(-1));

	// Slot 269: the weight, then the sequence; nothing else.
	Guard->AnimOverlay[1].Cycle = 0.4f;
	Guard->AnimOverlay[1].Activity = 0x3d;
	Guard->RemoveLayer(1);
	TestEqual(TEXT("0x10099660: `m_flWeight = 0`"), Guard->AnimOverlay[1].Weight, 0.f);
	TestEqual(TEXT("0x10099660: `m_nSequence = 0`"), Guard->AnimOverlay[1].Sequence, 0);
	TestEqual(TEXT("0x10099660: the owner stands"), Guard->AnimOverlay[1].Activity, 0x3d);
	TestEqual(TEXT("0x10099660: the cycle stands"), Guard->AnimOverlay[1].Cycle, 0.4f);

	// A full table refuses: slot 272 answers -1 and slot 268 is not called.
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		Guard->AnimOverlay[Index].Weight = 1.f;
		Guard->AnimOverlay[Index].Activity = 99;
		Guard->AnimOverlay[Index].Cycle = 0.7f;
	}
	TestEqual(TEXT("0x10099470: no zero-weight slot -> -1"), Guard->AllocateLayer(),
		static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("0x100990f0: a full table refuses the gesture"),
		Guard->AddGesture(GOverlayActRangeAttack1Layer, true), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("0x100990f0: ...and seeds no layer"), Guard->AnimOverlay[0].Cycle, 0.7f);

	// A sequence below 1 refuses before any slot is allocated.
	OverlayResetLayers(*Guard);
	Fixture.Services.bNpcActivitiesResolve = false;
	Fixture.Services.BodyClipsByRawIndex.Reset();
	TestEqual(TEXT("0x100991b0: `SelectWeightedSequence < 1` refuses with -1"),
		Guard->AddGesture(GOverlayActRangeAttack1Layer, true), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("0x100991b0: ...and no slot is taken"), Guard->AnimOverlay[0].Weight, 0.f);
	return true;
}

// --- `0x10098bb0` / `0x10098830`: the layer advance ------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelOverlayLayerAdvanceTest,
	"Elysium.Arm.NpcKernelOverlay.LayerAdvance", GElysiumNpcKernelOverlayFlags)
bool FElysiumNpcKernelOverlayLayerAdvanceTest::RunTest(const FString&)
{
	ElysiumAnimEventCensus::Clear();
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_overlay_layer_advance"), 5402);
	FElysiumEntityDef& Def = Builder.AddNpc(TEXT("guard"), FVector::ZeroVector, TEXT("CAI_BaseNPCTroika"));
	Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FOverlaySlot112Npc>(); };
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FOverlaySlot112Npc* Guard = static_cast<FOverlaySlot112Npc*>(Fixture.Npc(TEXT("guard")));
	if (!TestNotNull(TEXT("the guard spawned"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	OverlayResetLayers(*Guard);

	// Fixture rows: a blended one-second clip authoring 3031 at cycle 0.0 (the non-snap row the
	// zero-interval rule bites), a snap half-second clip, and a looping one-second clip.
	const TArray<FElysiumAnimEvent> BlendEvents{ OverlayEventAt(0.f, 3031) };
	const int32 Blend = OverlayFixtureRow(*Guard, TEXT("blend_layer"), 1.f, false, false, &BlendEvents);
	const int32 Snap = OverlayFixtureRow(*Guard, TEXT("snap_layer"), 0.5f, false, true, nullptr);
	const int32 Loop = OverlayFixtureRow(*Guard, TEXT("loop_layer"), 1.f, true, true, nullptr);

	// The base's interval: a fresh animation clock (`m_flPrevAnimTime == 0` re-seeds), so
	// `StudioFrameAdvance(0)` answers 0.1 and the live layer advances by it; a zero-weight record
	// is not advanced.
	Guard->SequenceCycleRate = 1.f;
	Guard->SequencePlaybackRate = 1.f;
	Guard->SequenceCycle = 0.2f;
	Guard->bSequenceLoopedOnce = false;
	Guard->bSequenceFinished = false;
	Guard->PrevAnimTime = 0.f;
	Guard->SetLayer(0, 5, Blend, false);
	Guard->AnimOverlay[1].Sequence = Snap;
	Guard->AnimOverlay[1].PlaybackRate = 1.f;
	Guard->AnimOverlay[1].Cycle = 0.3f;
	const float Interval = Guard->StudioFrameAdvance(0.f);
	TestEqual(TEXT("0x1008f120: the base answers its interval"), Interval, 0.1f, 1.e-5f);
	TestEqual(TEXT("0x10098bb0: the layer advances on the base's returned interval (rate x 1.0 x 0.1)"),
		Guard->AnimOverlay[0].Cycle, 0.1f, 1.e-5f);
	TestEqual(TEXT("0x10098917: `cycle / blendIn` = 0.5, then `3w^2 - 2w^3` (0x10450010) = 0.5"),
		Guard->AnimOverlay[0].Weight, 0.5f, 1.e-4f);
	TestEqual(TEXT("0x10098bb0: a zero-weight record is not advanced"), Guard->AnimOverlay[1].Cycle, 0.3f);

	// The zero-interval rule: a second advance at the same clock answers 0.0 (`dt <= 0.001`,
	// 0x1044f020) and the owner still runs the layer body with it.
	FOverlayHandlerProbe Handler;
	Guard->SetLayer(2, 6, Blend, true);
	// The fixture's clock stands at 0, so `m_flPrevAnimTime` (written from the re-seeded
	// `m_flAnimTime`, 0) would re-seed the clock again (`0x1008f192`); a world past time 0 never
	// meets that, and the word is given the value it would hold there.
	Guard->PrevAnimTime = Guard->AnimTime;
	const float SecondInterval = Guard->StudioFrameAdvance(0.f);
	TestEqual(TEXT("0x1008f1fc: the base's early-out answers 0.0"), SecondInterval, 0.f);
	TestEqual(TEXT("0x10098830: a zero interval moves no cycle"), Guard->AnimOverlay[0].Cycle, 0.1f, 1.e-5f);
	TestEqual(TEXT("0x10098830: a layer past cycle 0 recomputes to its old weight"),
		Guard->AnimOverlay[0].Weight, 0.5f, 1.e-4f);
	TestEqual(TEXT("0x10098830: the fresh layer's cycle stays 0"), Guard->AnimOverlay[2].Cycle, 0.f);
	TestEqual(TEXT("0x10098917: a cycle-0 layer with `blendIn 0.2` gets `0 / 0.2 = 0`: it frees itself"),
		Guard->AnimOverlay[2].Weight, 0.f);
	// Its dispatch still runs (no weight test in `0x10098cd0`): the 3031 at cycle 0.0 fires once.
	Guard->AnimOverlay[0].Weight = 0.f;
	Guard->AnimOverlay[0].LastEventCheck = 0.5f;   // layer 0's own window is past its 3031
	Guard->DispatchAnimEvents(SecondInterval, &Handler);
	TestEqual(TEXT("0x10098cd0: the freed cycle-0 layer's 3031 still dispatches"), Handler.CountOf(3031), 1);
	Guard->DispatchAnimEvents(SecondInterval, &Handler);
	TestEqual(TEXT("0x10098cd0: ...once (`[0.1, 0.1)` holds nothing)"), Handler.CountOf(3031), 1);

	// The envelope at three cycles, and the clamp to `m_flWeightMax`.
	OverlayResetLayers(*Guard);
	Guard->SetLayer(0, 5, Blend, false);
	Guard->AnimOverlay[0].Cycle = 0.05f;
	Guard->AdvanceOverlayLayers(0.f);
	TestEqual(TEXT("0x10098917: cycle 0.05 -> `0.05 / 0.2` = 0.25 -> 0.15625"),
		Guard->AnimOverlay[0].Weight, 0.15625f, 1.e-4f);
	Guard->AnimOverlay[0].Cycle = 0.5f;
	Guard->AdvanceOverlayLayers(0.f);
	TestEqual(TEXT("0x100988dc: between the blends the weight is 1.0"), Guard->AnimOverlay[0].Weight, 1.f, 1.e-5f);
	Guard->AnimOverlay[0].Cycle = 0.9f;
	Guard->AdvanceOverlayLayers(0.f);
	TestEqual(TEXT("0x10098945: cycle 0.9 -> `(1 - 0.9) / 0.2` = 0.5 -> 0.5"),
		Guard->AnimOverlay[0].Weight, 0.5f, 1.e-4f);
	Guard->AnimOverlay[0].Cycle = 0.5f;
	Guard->AnimOverlay[0].WeightMax = 0.4f;
	Guard->AdvanceOverlayLayers(0.f);
	TestEqual(TEXT("0x10098982: the weight is clamped to `m_flWeightMax`"), Guard->AnimOverlay[0].Weight, 0.4f);

	// The 0.95 gate (the float at 0x1045001c): both blends at or above it -> no envelope.
	OverlayResetLayers(*Guard);
	Guard->SetLayer(0, 5, Blend, false);
	Guard->AnimOverlay[0].Cycle = 0.1f;
	Guard->AnimOverlay[0].BlendIn = 0.95f;
	Guard->AnimOverlay[0].BlendOut = 0.95f;
	Guard->AdvanceOverlayLayers(0.f);
	TestEqual(TEXT("0x100988d6 / 0x100988ed: both blends >= 0.95 -> weight 1.0, no envelope"),
		Guard->AnimOverlay[0].Weight, 1.f);
	Guard->AnimOverlay[0].BlendOut = 0.5f;
	Guard->AdvanceOverlayLayers(0.f);
	TestTrue(TEXT("0x100988ed: one blend under 0.95 opens the envelope (`0.1 / 0.95`, smoothed)"),
		Guard->AnimOverlay[0].Weight > 0.f && Guard->AnimOverlay[0].Weight < 0.1f);
	// A snapped layer (both blends 0) keeps weight 1, at cycle 0 and on a zero interval too.
	Guard->SetLayer(3, 7, Snap, false);
	Guard->AdvanceOverlayLayers(0.f);
	TestEqual(TEXT("0x100988fa / 0x10098920: a blend of 0 skips its arm -- a snapped layer keeps 1.0"),
		Guard->AnimOverlay[3].Weight, 1.f);

	// The ends. At or above 1: finished; non-looping rests at 1.0; not auto-kill keeps its weight.
	OverlayResetLayers(*Guard);
	Guard->SetLayer(3, 7, Snap, false);
	Guard->AdvanceOverlayLayers(0.6f);   // rate 2.0 x 0.6 = 1.2
	TestEqual(TEXT("0x100988a4: at or above 1.0 `layer+4 = 1`"), Guard->AnimOverlay[3].SequenceFinished, 1);
	TestEqual(TEXT("0x100988cc: a non-looping layer's cycle is 1.0"), Guard->AnimOverlay[3].Cycle, 1.f);
	TestEqual(TEXT("0x10098bb0: finished without auto-kill keeps the weight"), Guard->AnimOverlay[3].Weight, 1.f);
	TestEqual(TEXT("0x10098bb0: ...and calls no slot 112"), Guard->Slot112Calls.Num(), 0);
	// Finished and auto-kill: the weight and nothing else, then slot 112 `(i, m_nActivity)`.
	Guard->SetLayer(3, 7, Snap, true);
	Guard->AdvanceOverlayLayers(0.6f);
	TestEqual(TEXT("0x10098bb0: finished && autokill -> `m_flWeight = 0`"), Guard->AnimOverlay[3].Weight, 0.f);
	TestEqual(TEXT("0x10098bb0: ...the sequence stays"), Guard->AnimOverlay[3].Sequence, Snap);
	TestEqual(TEXT("0x10098bb0: ...the activity stays"), Guard->AnimOverlay[3].Activity, 7);
	TestEqual(TEXT("0x10098bb0: ...the cycle rests at 1.0"), Guard->AnimOverlay[3].Cycle, 1.f);
	TestTrue(TEXT("0x10098bb0: slot 112 (+0x1c0) takes `(i, m_nActivity)`"),
		Guard->Slot112Calls.Num() == 1 && Guard->Slot112Calls[0] == FIntPoint(3, 7));
	// Looping: the integer part dropped, and still finished.
	Guard->SetLayer(1, 8, Loop, false);
	Guard->AdvanceOverlayLayers(1.25f);
	TestEqual(TEXT("0x100988b4: a looping layer drops the integer part"), Guard->AnimOverlay[1].Cycle, 0.25f, 1.e-5f);
	TestEqual(TEXT("0x100988a4: ...with `layer+4 = 1`"), Guard->AnimOverlay[1].SequenceFinished, 1);
	// Below 0: non-looping -> 0, no finish flag; looping -> the integer part dropped.
	OverlayResetLayers(*Guard);
	Guard->SetLayer(3, 7, Snap, false);
	Guard->AnimOverlay[3].Cycle = 0.3f;
	Guard->AnimOverlay[3].PlaybackRate = -1.f;
	Guard->AdvanceOverlayLayers(0.5f);   // 0.3 - 2.0 x 0.5 = -0.7
	TestEqual(TEXT("0x10098886: below 0 a non-looping layer's cycle is 0"), Guard->AnimOverlay[3].Cycle, 0.f);
	TestEqual(TEXT("0x1009885b: ...with no finish flag"), Guard->AnimOverlay[3].SequenceFinished, 0);
	Guard->SetLayer(1, 8, Loop, false);
	Guard->AnimOverlay[1].Cycle = 0.3f;
	Guard->AnimOverlay[1].PlaybackRate = -1.f;
	Guard->AnimOverlay[3].Weight = 0.f;
	Guard->AdvanceOverlayLayers(1.5f);   // 0.3 - 1.5 = -1.2
	TestEqual(TEXT("0x1009886e: below 0 a looping layer drops the integer part (-1.2 -> -0.2)"),
		Guard->AnimOverlay[1].Cycle, -0.2f, 1.e-5f);
	TestEqual(TEXT("0x1009885b: ...with no finish flag"), Guard->AnimOverlay[1].SequenceFinished, 0);

	ElysiumAnimEventCensus::Clear();
	return true;
}

// --- `0x10098c80` -> `0x10098cd0`: the four layers' dispatch ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelOverlayLayerDispatchTest,
	"Elysium.Arm.NpcKernelOverlay.LayerDispatch", GElysiumNpcKernelOverlayFlags)
bool FElysiumNpcKernelOverlayLayerDispatchTest::RunTest(const FString&)
{
	ElysiumAnimEventCensus::Clear();
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_overlay_layer_dispatch"), 5403);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("the guard spawned"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	OverlayResetLayers(*Guard);

	// `smith_attack_layer` as the data has it (S12 a.1): 0.4667 s, flags 2 (snap, not looping), 3031
	// at cycle 0.0 then a client id; a second layer row; and the base's run, authoring no 3031.
	const TArray<FElysiumAnimEvent> SmithEvents{ OverlayEventAt(0.f, 3031), OverlayEventAt(0.f, 5003) };
	const TArray<FElysiumAnimEvent> OtherEvents{ OverlayEventAt(0.f, 3032) };
	const TArray<FElysiumAnimEvent> RunEvents{ OverlayEventAt(0.25f, 2050) };
	const int32 Smith = OverlayFixtureRow(*Guard, TEXT("smith_attack_layer"), 0.4667f, false, true, &SmithEvents);
	const int32 Other = OverlayFixtureRow(*Guard, TEXT("other_layer"), 1.f, false, true, &OtherEvents);
	const int32 RunRow = OverlayFixtureRow(*Guard, TEXT("run"), 1.f, true, false, &RunEvents);

	// The base sequence a fifth of the way in, its window `[0.2, 0.3)`.
	Guard->SequenceNumber = RunRow;
	Guard->SequenceCycleRate = 1.f;
	Guard->SequencePlaybackRate = 1.f;
	Guard->SequenceCycle = 0.2f;
	Guard->LastEventCheck = 0.2f;
	Guard->bSequenceLoopedOnce = true;
	Guard->bSequenceFinished = false;

	// Order: the base, then layers 0..3; no weight or "in use" test; ids >= 5000 skipped.
	FOverlayHandlerProbe Handler;
	Guard->SetLayer(0, GOverlayActRangeAttack1Layer, Smith, true);
	Guard->SetLayer(2, 9, Other, true);
	Guard->AnimOverlay[0].SequenceFinished = 1;
	Guard->AnimOverlay[2].Weight = 0.f;   // freed: still handed to the layer body
	Guard->DispatchAnimEvents(0.1f, &Handler);
	TestEqual(TEXT("0x10098c80: the base (0x10091880), then 0x10098cd0 on layers 0..3; a zero-weight layer still scanned; 5003 never offered"),
		Handler.Ids(), FString(TEXT("2050,3031,3032")));
	TestEqual(TEXT("0x10098cd0: `layer+4 = 0`"), Guard->AnimOverlay[0].SequenceFinished, 0);
	TestEqual(TEXT("0x10098cd0: `layer+0x2c = cycle + 0.1 x cycleRate x rate` (0 + 0.1 / 0.4667)"),
		Guard->AnimOverlay[0].LastEventCheck, 0.1f / 0.4667f, 1.e-4f);
	TestEqual(TEXT("0x10098cd0: a never-set record's window is `[0, 0)`"),
		Guard->AnimOverlay[1].LastEventCheck, 0.f);
	// The window opens at `layer+0x2c`: the same think again fires nothing.
	Handler.Handled.Reset();
	Guard->DispatchAnimEvents(0.1f, &Handler);
	TestEqual(TEXT("0x10098cd0: the window starts at `layer+0x2c` -- 3031 at 0.0 fired once"),
		Handler.Handled.Num(), 0);
	// No clamp past 1.0, and the finish word is never set by the layer body.
	Guard->AnimOverlay[0].Cycle = 0.95f;
	Guard->DispatchAnimEvents(0.1f, &Handler);
	TestTrue(TEXT("0x10098cd0: no clamp -- `layer+0x2c` passes 1.0"), Guard->AnimOverlay[0].LastEventCheck > 1.f);
	TestEqual(TEXT("0x10098cd0: `layer+4` is never set here"), Guard->AnimOverlay[0].SequenceFinished, 0);

	// A layer's life (S12 a.2), 0.1 s thinks: pushed, its 3031 fires in that think's dispatch; the
	// fifth advance carries the cycle past 1.0 (5 x 0.1 / 0.4667), finishes and frees it; a dead
	// non-looping layer then dispatches nothing -- by arithmetic, all four records still handed.
	OverlayResetLayers(*Guard);
	Handler.Handled.Reset();
	Guard->SetLayer(0, GOverlayActRangeAttack1Layer, Smith, true);
	for (int32 Think = 1; Think <= 5; ++Think)
	{
		Guard->AdvanceOverlayLayers(0.1f);                 // slot 250's layer half, `PostRun`'s advance
		if (Think < 5)
		{
			TestEqual(TEXT("0x100988dc: a snapped attack layer is at weight 1.0 until its end"),
				Guard->AnimOverlay[0].Weight, 1.f);
		}
		Guard->DispatchAnimEvents(0.1f, &Handler);         // slot 258
		if (Think == 1)
		{
			TestEqual(TEXT("0x10098cd0: the 3031 at cycle 0.0 fires in the think the layer is pushed"),
				Handler.CountOf(3031), 1);
		}
	}
	TestEqual(TEXT("0x10098bb0: finished && autokill frees the slot on the fifth think"),
		Guard->AnimOverlay[0].Weight, 0.f);
	TestEqual(TEXT("0x100988cc: ...resting at cycle 1.0"), Guard->AnimOverlay[0].Cycle, 1.f);
	TestEqual(TEXT("0x10098bb0: ...its sequence kept"), Guard->AnimOverlay[0].Sequence, Smith);
	TestEqual(TEXT("0x10098cd0: ...and `layer+4` zeroed by the same think's dispatch"),
		Guard->AnimOverlay[0].SequenceFinished, 0);
	TestEqual(TEXT("0x10099470: the slot is free again"), Guard->AllocateLayer(), 0);
	for (int32 Think = 0; Think < 10; ++Think)
	{
		Guard->AdvanceOverlayLayers(0.1f);
		Guard->DispatchAnimEvents(0.1f, &Handler);
	}
	TestEqual(TEXT("0x10098cd0: a dead non-looping layer dispatches nothing (window `[1 + 0.1r, 1 + 0.1r)`)"),
		Handler.CountOf(3031), 1);

	ElysiumAnimEventCensus::Clear();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
