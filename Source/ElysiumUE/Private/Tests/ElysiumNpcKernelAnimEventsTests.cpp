#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumNpc.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Spec 0002 V4a, lane A1 — the animation-event dispatcher under the kernel.
//
// `CBaseAnimating::DispatchAnimEvents` `0x10091880`, the per-layer body `0x10098cd0`, and
// `CAI_BaseNPC::PostRun` `0x1026c7c0`'s call order. Every assertion names the address of the line it
// reads; the event tables are fixtures on the stack (the bridge row's own table is lane A2's).

static constexpr EAutomationTestFlags GElysiumNpcKernelAnimEventsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	FElysiumAnimEvent AnimEventAt(float Cycle, int32 Event, const TCHAR* Options = TEXT(""))
	{
		FElysiumAnimEvent Record;
		Record.Cycle = Cycle;
		Record.Event = Event;
		Record.Options = Options;
		return Record;
	}

	// The HANDLER: slot 259 (`+0x40c`), recording what reached it, in order.
	class FAnimEventsHandlerProbe final : public FElysiumEntity
	{
	public:
		TArray<int32> Handled;
		// The words as they stood on the caller's object when the first event arrived.
		const float* LiveLastEventCheck = nullptr;
		float LastEventCheckAtFirstEvent = -1.f;

		virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override
		{
			if (Handled.IsEmpty() && LiveLastEventCheck != nullptr)
			{
				LastEventCheckAtFirstEvent = *LiveLastEventCheck;
			}
			Handled.Add(Event.Event);
			return true;
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

	// A non-looping sequence with a descriptor, one cycle per second.
	FElysiumSequenceWords WordsAt(float Cycle, float LastEventCheck, float CycleRate = 1.f)
	{
		FElysiumSequenceWords Words;
		Words.Sequence = 7;
		Words.Cycle = Cycle;
		Words.CycleRate = CycleRate;
		Words.LastEventCheck = LastEventCheck;
		return Words;
	}

	// `PostRun`'s probe: a Troika NPC whose slot 258 records what the frame looked like when it was
	// called, then runs the real body.
	class FPostRunOrderNpc final : public FElysiumNpc
	{
	public:
		int32 DispatchCalls = 0;
		float CycleAtDispatch = -1.f;
		int32 WeaponUpdatesAtDispatch = -1;
		float IntervalAtDispatch = -1.f;
		FElysiumEntity* HandlerAtDispatch = nullptr;

		virtual void DispatchAnimEvents(float Interval, FElysiumEntity* Handler) override
		{
			++DispatchCalls;
			CycleAtDispatch = SequenceCycle;
			WeaponUpdatesAtDispatch = MotorSeams.PostRunWeaponUpdates;
			IntervalAtDispatch = Interval;
			HandlerAtDispatch = Handler;
			FElysiumNpc::DispatchAnimEvents(Interval, Handler);
		}
	};
}

// --- `0x10091880`: the window, the look-ahead, the finish and past-half words ---------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimEventsWindowTest,
	"Elysium.Arm.NpcKernelAnimEvents.Window", GElysiumNpcKernelAnimEventsFlags)
bool FElysiumNpcKernelAnimEventsWindowTest::RunTest(const FString&)
{
	ElysiumAnimEventCensus::Clear();
	FElysiumEntity Source;

	// Closed at the bottom, open at the top, in TABLE order (one pass, never sorted).
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{
			AnimEventAt(0.45f, 2053), AnimEventAt(0.30f, 2051), AnimEventAt(0.10f, 2050),
			AnimEventAt(0.20f, 2052) };
		FElysiumSequenceWords Words = WordsAt(0.3f, 0.2f);
		ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: `m_flLastEventCheck <= cycle && cycle < flEnd`, in table order"),
			Handler.Ids(), FString(TEXT("2051,2052")));
	}
	// The top is open, on an exact end: 0.25 + 0.1 (0x104491b4) x 2.5 = 0.5.
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{ AnimEventAt(0.5f, 2050), AnimEventAt(0.25f, 2051) };
		FElysiumSequenceWords Words = WordsAt(0.25f, 0.25f, 2.5f);
		Words.bSequencePastHalf = true;
		ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: the look-ahead end is `m_flCycle + 0.1 (0x104491b4) x rate`, stored at +0x658"),
			Words.LastEventCheck, 0.5f);
		TestEqual(TEXT("0x10091880: a record AT the look-ahead end does not fire (`cycle < flEnd`)"),
			Handler.Ids(), FString(TEXT("2051")));
		TestFalse(TEXT("0x10091880: +0x568 is `flEnd > 0.5` (`flEnd <= 0x104454d0` writes 0)"),
			Words.bSequencePastHalf);
		TestFalse(TEXT("0x10091880: +0x65c stays clear short of 1.0"), Words.bSequenceFinished);
	}
	// Past-half set from the look-ahead end, not from the cycle.
	{
		FAnimEventsHandlerProbe Handler;
		FElysiumSequenceWords Words = WordsAt(0.45f, 0.45f);
		ElysiumAnimEvents::DispatchBase(Words, TConstArrayView<FElysiumAnimEvent>(), Source, Handler);
		TestTrue(TEXT("0x10091880: +0x568 = 1 once the look-ahead end passes 0.5 (cycle 0.45)"),
			Words.bSequencePastHalf);
	}

	// Ids at or above 5000 are skipped (`(int)event < 5000`).
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{
			AnimEventAt(0.31f, 4999), AnimEventAt(0.32f, 5000), AnimEventAt(0.33f, 5100) };
		FElysiumSequenceWords Words = WordsAt(0.3f, 0.3f);
		ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: only an id below 5000 reaches slot 259"),
			Handler.Ids(), FString(TEXT("4999")));
		TArray<ElysiumAnimEventCensus::FRow> Rows;
		ElysiumAnimEventCensus::Collect(Rows);
		TestEqual(TEXT("the two skipped ids are census rows"), Rows.Num(), 2);
		TestTrue(TEXT("marked as never offered to a handler"),
			Rows.Num() == 2 && Rows[0].bAboveServerBand && Rows[1].bAboveServerBand);
		ElysiumAnimEventCensus::Clear();
	}

	// The events go to the HANDLER (`*param_2 + 0x40c`), not to the source.
	{
		FAnimEventsHandlerProbe SourceProbe;
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{ AnimEventAt(0.31f, 3031) };
		FElysiumSequenceWords Words = WordsAt(0.3f, 0.3f);
		ElysiumAnimEvents::DispatchBase(Words, Events, SourceProbe, Handler);
		TestEqual(TEXT("0x10091880: slot 259 is the handler's"), Handler.Ids(), FString(TEXT("3031")));
		TestEqual(TEXT("...and the source hears nothing"), SourceProbe.Handled.Num(), 0);
	}

	// A zeroed `m_flLastEventCheck` (`ResetSequenceInfo 0x10090a3d`) restarts the window at 0.
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{ AnimEventAt(0.f, 2050), AnimEventAt(0.05f, 2051) };
		FElysiumSequenceWords Words = WordsAt(0.f, 0.f);
		ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: from +0x658 == 0 a record at cycle 0 fires"),
			Handler.Ids(), FString(TEXT("2050,2051")));
	}

	// Non-looping: the finish, the clamp, and a record at 1.0 that never fires.
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{ AnimEventAt(1.f, 2050), AnimEventAt(0.97f, 2051) };
		FElysiumSequenceWords Words = WordsAt(0.95f, 0.9f);
		Words.bDescriptorLoops = true;   // the wrap clause needs `flEnd >= 1` AND `cycle < flEnd - 1`
		Words.bSequencePastHalf = true;
		const bool bEdge = ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestTrue(TEXT("0x10091880: non-looping, `flEnd >= 1` sets +0x65c"), Words.bSequenceFinished);
		TestEqual(TEXT("0x10091880: ...and clamps the end to 1.0 (+0x658)"), Words.LastEventCheck, 1.f);
		TestEqual(TEXT("0x10091880: a record at 1.0 never fires on a non-looping clip"),
			Handler.Ids(), FString(TEXT("2051")));
		TestTrue(TEXT("0x10091880: +0x568 is not written on the finishing branch"), Words.bSequencePastHalf);
		TestTrue(TEXT("0x10091b9a: the rising edge (set now, clear at entry) is reported"), bEdge);
		// The next think: the flag is still set at entry (`cVar2`), cleared, set again -- no edge.
		Handler.Handled.Reset();
		const bool bEdgeAgain = ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestTrue(TEXT("0x10091880: the held end stays finished"), Words.bSequenceFinished);
		TestFalse(TEXT("0x10091b9a: `OnSequenceFinished` once -- no edge while the flag was set at entry"),
			bEdgeAgain);
		TestEqual(TEXT("0x10091880: `[1.0, 1.0)` fires nothing"), Handler.Handled.Num(), 0);
	}
	// A negative end finishes too (`flEnd < 0`).
	{
		FAnimEventsHandlerProbe Handler;
		FElysiumSequenceWords Words = WordsAt(0.05f, 0.f, -1.f);
		ElysiumAnimEvents::DispatchBase(Words, TConstArrayView<FElysiumAnimEvent>(), Source, Handler);
		TestTrue(TEXT("0x10091880: `flEnd < 0` sets +0x65c"), Words.bSequenceFinished);
		TestEqual(TEXT("0x10091880: ...with the end at 1.0"), Words.LastEventCheck, 1.f);
	}
	// The flag is cleared at entry when the end is short of 1.0.
	{
		FAnimEventsHandlerProbe Handler;
		FElysiumSequenceWords Words = WordsAt(0.2f, 0.2f);
		Words.bSequenceFinished = true;
		const bool bEdge = ElysiumAnimEvents::DispatchBase(Words, TConstArrayView<FElysiumAnimEvent>(),
			Source, Handler);
		TestFalse(TEXT("0x10091880: `m_bSequenceFinished = 0` at entry"), Words.bSequenceFinished);
		TestFalse(TEXT("0x10091b9a: no edge"), bEdge);
	}

	// No descriptor on a non-looping sequence: neither the finish nor past-half, no clamp, no events.
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{ AnimEventAt(0.97f, 2050) };
		FElysiumSequenceWords Words = WordsAt(0.95f, 0.9f, 2.f);
		Words.bHasDescriptor = false;
		Words.bSequencePastHalf = false;
		const bool bEdge = ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestFalse(TEXT("0x10091880: no seqdesc, non-looping: +0x65c cleared and not set"),
			Words.bSequenceFinished);
		TestFalse(TEXT("0x10091880: ...+0x568 not written"), Words.bSequencePastHalf);
		TestTrue(TEXT("0x10091880: ...+0x658 takes the unclamped end"), Words.LastEventCheck > 1.f);
		TestEqual(TEXT("0x10091880: ...and no event table is walked"), Handler.Handled.Num(), 0);
		TestFalse(TEXT("0x10091b9a: ...and no edge"), bEdge);
	}

	// Looping: no clamp, the wrap swept once with `seqdesc.flags & 1`, the start wrapped into [0,1).
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{
			AnimEventAt(0.02f, 2050), AnimEventAt(0.97f, 2051), AnimEventAt(0.12f, 2052) };
		FElysiumSequenceWords Words = WordsAt(0.95f, 0.9f);
		Words.bLoops = true;
		Words.bDescriptorLoops = true;
		const bool bEdge = ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: the tail and, with `flags & 1` and `flEnd >= 1`, `cycle < flEnd - 1`, in table order"),
			Handler.Ids(), FString(TEXT("2050,2051")));
		TestTrue(TEXT("0x10091880: looping, `flEnd >= 1` sets +0x65c"), Words.bSequenceFinished);
		TestTrue(TEXT("0x10091880: ...with no clamp on +0x658"), Words.LastEventCheck > 1.f);
		TestTrue(TEXT("0x10091b9a: ...and the edge"), bEdge);
		// The next think, after the clock wrapped the cycle: the start 1.05 wraps to 0.05.
		Handler.Handled.Reset();
		Words.Cycle = 0.1f;
		ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: the start wraps into [0,1) (0x10449280); the wrapped record fired once"),
			Handler.Ids(), FString(TEXT("2052")));
		TestFalse(TEXT("0x10091880: +0x65c cleared on the new lap"), Words.bSequenceFinished);
	}
	// The wrap is lost without the descriptor's flag.
	{
		FAnimEventsHandlerProbe Handler;
		const TArray<FElysiumAnimEvent> Events{ AnimEventAt(0.02f, 2050), AnimEventAt(0.97f, 2051) };
		FElysiumSequenceWords Words = WordsAt(0.95f, 0.9f);
		Words.bLoops = true;
		Words.bDescriptorLoops = false;
		ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: without `seqdesc.flags & 1` the wrapped record is lost"),
			Handler.Ids(), FString(TEXT("2051")));
	}

	// The three words are on the object before the first event reaches slot 259.
	{
		FAnimEventsHandlerProbe Handler;
		float LiveLastEventCheck = 0.25f;
		Handler.LiveLastEventCheck = &LiveLastEventCheck;
		const TArray<FElysiumAnimEvent> Events{ AnimEventAt(0.3f, 2050) };
		FElysiumSequenceWords Words = WordsAt(0.25f, 0.25f, 2.5f);
		Words.WordsWritten = [&LiveLastEventCheck](const FElysiumSequenceWords& Written)
		{
			LiveLastEventCheck = Written.LastEventCheck;
		};
		ElysiumAnimEvents::DispatchBase(Words, Events, Source, Handler);
		TestEqual(TEXT("0x10091880: `m_flLastEventCheck = flEnd` is written before the event loop"),
			Handler.LastEventCheckAtFirstEvent, 0.5f);
	}

	ElysiumAnimEventCensus::Clear();
	return true;
}

// --- `0x10098cd0`: one overlay layer ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimEventsLayerTest,
	"Elysium.Arm.NpcKernelAnimEvents.Layer", GElysiumNpcKernelAnimEventsFlags)
bool FElysiumNpcKernelAnimEventsLayerTest::RunTest(const FString&)
{
	ElysiumAnimEventCensus::Clear();
	FElysiumEntity Source;
	FAnimEventsHandlerProbe Handler;
	const TArray<FElysiumAnimEvent> Events{
		AnimEventAt(0.02f, 3031), AnimEventAt(0.97f, 2051), AnimEventAt(0.12f, 2052),
		AnimEventAt(0.98f, 5000) };

	FElysiumSequenceWords Layer = WordsAt(0.95f, 0.9f);
	Layer.bLoops = false;             // the layer body reads no loop word
	Layer.bDescriptorLoops = true;
	Layer.bSequenceFinished = true;   // layer+4
	Layer.bSequencePastHalf = false;
	ElysiumAnimEvents::DispatchLayer(Layer, Events, Source, Handler);
	TestFalse(TEXT("0x10098cd0: `layer+4 = 0`, and it is never set, even past 1.0"),
		Layer.bSequenceFinished);
	TestTrue(TEXT("0x10098cd0: no clamp -- `layer+0x2c` takes `cycle + 0.1 x rate` past 1.0"),
		Layer.LastEventCheck > 1.f);
	TestFalse(TEXT("0x10098cd0: no past-half write"), Layer.bSequencePastHalf);
	TestEqual(TEXT("0x10098cd0: the same window and wrap clause, ids >= 5000 skipped, slot 259"),
		Handler.Ids(), FString(TEXT("3031,2051")));

	// No start wrap in the layer body: the cursor stays past 1.0 and the next window is empty.
	Handler.Handled.Reset();
	Layer.Cycle = 0.1f;
	Layer.bSequencePastHalf = true;
	ElysiumAnimEvents::DispatchLayer(Layer, Events, Source, Handler);
	TestEqual(TEXT("0x10098cd0: `fVar2 = layer+0x2c` is used unwrapped -- `[1.05, 0.2)` holds nothing"),
		Handler.Handled.Num(), 0);
	TestTrue(TEXT("0x10098cd0: `layer+0x2c` = the new look-ahead end"),
		Layer.LastEventCheck > 0.19f && Layer.LastEventCheck < 0.21f);
	TestTrue(TEXT("0x10098cd0: past-half still untouched"), Layer.bSequencePastHalf);

	// No "in use" or weight test: a layer with a rate and a table dispatches whatever it is.
	Handler.Handled.Reset();
	Layer.Cycle = 0.25f;
	ElysiumAnimEvents::DispatchLayer(Layer, Events, Source, Handler);
	TestEqual(TEXT("0x10098cd0: the next window `[0.2, 0.35)` is empty of these records"),
		Handler.Handled.Num(), 0);

	ElysiumAnimEventCensus::Clear();
	return true;
}

// --- `0x1026c7c0`: RunAnimation, slot 258, Weapon_FrameUpdate --------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelAnimEventsPostRunOrderTest,
	"Elysium.Arm.NpcKernelAnimEvents.PostRunOrder", GElysiumNpcKernelAnimEventsFlags)
bool FElysiumNpcKernelAnimEventsPostRunOrderTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_animevents_postrun"), 5258);
	FElysiumEntityDef& Def = Builder.AddNpc(TEXT("guard"), FVector::ZeroVector, TEXT("CAI_BaseNPCTroika"));
	Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FPostRunOrderNpc>(); };
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FPostRunOrderNpc* Guard = static_cast<FPostRunOrderNpc*>(Fixture.Npc(TEXT("guard")));
	TestNotNull(TEXT("the guard spawned"), Guard);
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// A non-looping sequence a fifth of the way in, one cycle per second, on a fresh animation
	// clock (`m_flPrevAnimTime == 0` re-seeds, so `StudioFrameAdvance(0)` answers 0.1).
	Guard->SequenceCycleRate = 1.f;
	Guard->SequencePlaybackRate = 1.f;
	Guard->SequenceCycle = 0.2f;
	Guard->bSequenceLoopedOnce = false;
	Guard->bSequenceFinished = false;
	Guard->PrevAnimTime = 0.f;
	Guard->LastEventCheck = 0.f;
	Guard->DispatchCalls = 0;
	const int32 WeaponUpdatesBefore = Guard->MotorSeams.PostRunWeaponUpdates;

	const float Interval = Guard->PostRun();

	TestEqual(TEXT("0x1026c8d8: slot 258 is called once"), Guard->DispatchCalls, 1);
	TestTrue(TEXT("0x1026c8c4 before 0x1026c8d8: `RunAnimation` advanced the cycle before slot 258 ran"),
		FMath::IsNearlyEqual(Guard->CycleAtDispatch, 0.3f, 1.e-4f));
	TestEqual(TEXT("0x1026c8d8: slot 258 takes `RunAnimation`'s interval"),
		Guard->IntervalAtDispatch, Interval);
	TestTrue(TEXT("0x1026c8d8: ...and `this` as the handler"),
		Guard->HandlerAtDispatch == static_cast<FElysiumEntity*>(Guard));
	TestEqual(TEXT("0x1026c8e0 after 0x1026c8d8: `Weapon_FrameUpdate` had not run when slot 258 did"),
		Guard->WeaponUpdatesAtDispatch, WeaponUpdatesBefore);
	TestEqual(TEXT("0x1026c8e0: `Weapon_FrameUpdate` ran once, after it"),
		Guard->MotorSeams.PostRunWeaponUpdates, WeaponUpdatesBefore + 1);
	// Slot 258 is the dispatcher: it stored its look-ahead end over the ADVANCED cycle.
	TestTrue(TEXT("0x10091880 from 0x10098c80: +0x658 = the advanced cycle + 0.1 x rate"),
		FMath::IsNearlyEqual(Guard->LastEventCheck, 0.4f, 1.e-4f));
	TestFalse(TEXT("0x10091880: +0x65c clear short of the end"), Guard->bSequenceFinished);

	// Two tenths short of the end by the clock, one tenth by the dispatcher: the finish a task reads
	// is the look-ahead's, one think early.
	Guard->SequenceCycle = 0.85f;
	Guard->PrevAnimTime = 0.f;
	Guard->PostRun();
	TestTrue(TEXT("0x10091880: the look-ahead end reaching 1.0 sets +0x65c while the cycle is 0.95"),
		Guard->bSequenceFinished && Guard->SequenceCycle < 1.f);
	TestEqual(TEXT("0x10091880: +0x658 clamped to 1.0"), Guard->LastEventCheck, 1.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
