// `CAI_BaseNPC`'s slot 258, the animation-event dispatch of the NPC chain (spec 0002 V4a). The
// declaration is in `ElysiumNpcBase.h`; the shared dispatcher bodies (`0x10091880`, `0x10098cd0`)
// are `ElysiumAnimEvents::DispatchBase` / `DispatchLayer` (`Substrate/ElysiumAnimEvents.h`).

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumOverlayStack.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumNpc.h"

namespace
{
	// One of the four `CAnimationLayer` records `0x10098c80` hands to `0x10098cd0` (`+0x734`, stride
	// `0x30`: sequence `+8`, cycle `+0xc`, playback rate `+0x10`, `m_flLastEventCheck` `+0x2c`).
	//
	// **SEAM answering "no layer"** (spec 0002 V4, J5 / K6): the NPC's overlay stack is spec 0015's.
	// Retail's body has no "in use" test -- it runs on all four records every think -- and an empty
	// record dispatches nothing (rate 0, window `[0, 0)`), which is what this answer stands for. On
	// step-2 paths the only pusher is the move-and-shoot overlay `0x102e8560` (`RunTaskOverlay
	// 0x10289c90`: `AddGesture(TranslateActivity(0x1a))`, whose LAYER's 3031 fires the shot on the
	// move); the port keeps it a counter (`ElysiumNpcBaseMaintain.cpp`,
	// `++MoveAndShootOverlay.UpdateCalls`), and **this seam is correct only while that overlay stays
	// a counter** -- the red record is `cover_move_shoot`, the wire is 0002 R3's (story V4o). The
	// other pushers (`AddGesture 0x100991b0` from TASK 0xe4 and the discipline applier, slot 273,
	// the scene entity's `AddGestureSequence`) are off step 2's paths.
	bool OverlayLayerWords(const FElysiumNpcBase& Npc, int32 LayerIndex, FElysiumSequenceWords& OutLayer)
	{
		(void)Npc;
		(void)LayerIndex;
		(void)OutLayer;
		return false;
	}
}

void FElysiumNpcBase::DispatchAnimEvents(float Interval, FElysiumEntity* Handler)
{
	// `CBaseAnimatingOverlay::DispatchAnimEvents` `0x10098c80`, slot 258 on every NPC: the base
	// `0x10091880`, then `0x10098cd0` on layers 0..3. `PostRun 0x1026c7c0` calls it with
	// `(interval, this)` (`0x1026c8d8`). The interval is unused by both retail bodies.
	(void)Interval;
	FElysiumEntity& EventHandler = Handler != nullptr ? *Handler : *this;
	const FElysiumNpc* const Troika = AsNpc();

	// The words `0x10091880` reads, from the kernel. `GetModelPtr() != 0` is taken as true for every
	// NPC: the kernel's sequence words are this port's stand-in for the model's sequence table (the
	// bridge, K2), and a valid sequence of a model always has a descriptor.
	FElysiumSequenceWords Words;
	Words.Sequence = SequenceNumber;                                              // +0x6f0 m_nSequence
	Words.Cycle = SequenceCycle;                                                  // +0x6f8 m_flCycle
	// `GetSequenceCycleRate(m_nSequence) x m_flPlaybackRate` (+0x6f4).
	Words.CycleRate = SequenceCycleRate * (Troika != nullptr ? Troika->SequencePlaybackRate : 1.f);
	Words.AnimTime = AnimTime;                                                    // +0x174 m_flAnimTime
	Words.bLoops = bSequenceLoopedOnce;                                           // +0x65d m_bSequenceLoops
	Words.bHasDescriptor = true;
	Words.bDescriptorLoops = Troika != nullptr && Troika->SequenceLoops(SequenceNumber);   // seqdesc.flags & 1
	Words.LastEventCheck = LastEventCheck;                                        // +0x658
	Words.bSequenceFinished = bSequenceFinished;                                  // +0x65c at entry (`cVar2`)
	Words.bSequencePastHalf = SequencePastHalf;                                   // +0x568
	// The descriptor's event table, from the bridge row. A base-only NPC carries no bridge.
	const TConstArrayView<FElysiumAnimEvent> Events = Troika != nullptr
		? Troika->SequenceEvents(SequenceNumber) : TConstArrayView<FElysiumAnimEvent>();
	if (Troika != nullptr && Events.Num() > 0 && Troika->SequenceRows.IsValidIndex(SequenceNumber))
	{
		// The census's name for an unclaimed id (debug bookkeeping): the row's bank, or the body's
		// own model for a clip of its own.
		const FElysiumNpc::FSequenceRow& Row = Troika->SequenceRows[SequenceNumber];
		Words.CensusOwner = Row.OwnerStem.IsEmpty() ? ModelStem() : Row.OwnerStem;
		Words.CensusLabel = Row.Label;
	}
	// Retail writes the three words on the object before its event loop, so a handler that resets the
	// sequence (`ResetSequenceInfo 0x10090950`) keeps what it wrote.
	const bool bWasFinished = bSequenceFinished;
	Words.WordsWritten = [this](const FElysiumSequenceWords& Written)
	{
		bSequenceFinished = Written.bSequenceFinished;                            // +0x65c
		SequencePastHalf = Written.bSequencePastHalf;                             // +0x568
		LastEventCheck = Written.LastEventCheck;                                  // +0x658
	};
	const int32 DispatchedSequence = SequenceNumber;
	ElysiumAnimEvents::DispatchBase(Words, Events, *this, EventHandler);                // 0x10098c80 -> 0x10091880
	// `m_bSequenceFinished != 0 && cVar2 == 0` -> `OnSequenceFinished` (`0x10091b9a` ->
	// `0x10091c80`), read from the live word after the loop. `0x10091c80` is an empty body and the
	// port has none; the AI trace's `seqfinished` (debug output only, behind its sink) rides the
	// edge, as it does on `StudioFrameAdvance`'s (`0x1008f316`).
	if (bSequenceFinished && !bWasFinished && IsAiTraced())
	{
		EmitAiTrace(TEXT("seqfinished"), TraceSequenceName(DispatchedSequence));
	}

	// Layers 0..3 through `0x10098cd0`, each on its own cursor, `eventtime` from the owner's
	// `m_flAnimTime`. The seam answers "no layer" for all four (see `OverlayLayerWords`).
	for (int32 LayerIndex = 0; LayerIndex < ElysiumOverlay::NumSlots; ++LayerIndex)
	{
		FElysiumSequenceWords Layer;
		if (!OverlayLayerWords(*this, LayerIndex, Layer))
		{
			continue;
		}
		Layer.AnimTime = AnimTime;                                                // `param_3[0x5d]`, the owner's
		const TConstArrayView<FElysiumAnimEvent> LayerEvents = Troika != nullptr
			? Troika->SequenceEvents(Layer.Sequence) : TConstArrayView<FElysiumAnimEvent>();
		ElysiumAnimEvents::DispatchLayer(Layer, LayerEvents, *this, EventHandler);      // 0x10098cd0
	}
}
