// `CAI_BaseNPC`'s slot 258, the animation-event dispatch of the NPC chain (spec 0002 V4a). The
// declaration is in `ElysiumNpcBase.h`; the shared dispatcher bodies (`0x10091880`, `0x10098cd0`)
// are `ElysiumAnimEvents::DispatchBase` / `DispatchLayer` (`Substrate/ElysiumAnimEvents.h`).

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimatingOverlay.h"           // FAnimOverlayLayer -- the four records of `+0x734`
#include "ElysiumOverlayStack.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumNpc.h"

namespace
{
	// One of the four `CAnimationLayer` records `0x10098c80` hands to `0x10098cd0` (`+0x734`, stride
	// `0x30`: sequence `+8`, cycle `+0xc`, playback rate `+0x10`, `m_flLastEventCheck` `+0x2c`).
	//
	// Answers true for all four records, every think (spec 0002 V4o): retail's body has no "in use"
	// test, no weight test and no skip of a freed slot. A never-set record holds sequence 0 at
	// playback rate 0, so its window is `[0, 0)`; a freed non-looping layer rests at cycle 1.0 and
	// from its second resting think its window is empty too -- by arithmetic, not by a skip.
	bool OverlayLayerWords(const FElysiumNpcBase& Npc, int32 LayerIndex, FElysiumSequenceWords& OutLayer)
	{
		const FElysiumAnimatingOverlay::FAnimOverlayLayer& Record = Npc.AnimOverlay[LayerIndex];
		const FElysiumNpc* const Troika = Npc.AsNpc();
		OutLayer.Sequence = Record.Sequence;                                      // layer+8
		OutLayer.Cycle = Record.Cycle;                                            // layer+0xc
		// `GetSequenceCycleRate(owner, layer+8) x layer+0x10` (`0x10098cd0`).
		OutLayer.CycleRate = Npc.OverlaySequenceCycleRate(Record.Sequence) * Record.PlaybackRate;
		// The row's own `seqdesc.flags & 1`: the wrap clause. (The layer body reads no loop word.)
		const bool bRowLoops = Troika != nullptr && Troika->SequenceLoops(Record.Sequence);
		OutLayer.bLoops = bRowLoops;
		OutLayer.bDescriptorLoops = bRowLoops;
		// `GetSeqDesc(layer+8) != 0`: the bridge row exists (row 0 is the model's own sequence 0).
		OutLayer.bHasDescriptor = Troika != nullptr
			&& (Record.Sequence == 0 || Troika->SequenceRows.IsValidIndex(Record.Sequence));
		OutLayer.LastEventCheck = Record.LastEventCheck;                          // layer+0x2c
		OutLayer.bSequenceFinished = Record.SequenceFinished != 0;                // layer+4
		if (Troika != nullptr && Troika->SequenceRows.IsValidIndex(Record.Sequence))
		{
			// The census's name for an unclaimed id (debug bookkeeping), as the base fills it.
			const FElysiumNpc::FSequenceRow& Row = Troika->SequenceRows[Record.Sequence];
			OutLayer.CensusOwner = Row.OwnerStem.IsEmpty() ? Npc.ModelStem() : Row.OwnerStem;
			OutLayer.CensusLabel = Row.Label;
		}
		return true;
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
	// `m_flAnimTime`. All four records, every think, in order (see `OverlayLayerWords`).
	for (int32 LayerIndex = 0; LayerIndex < ElysiumOverlay::NumSlots; ++LayerIndex)
	{
		FElysiumSequenceWords Layer;
		if (!OverlayLayerWords(*this, LayerIndex, Layer))
		{
			continue;
		}
		Layer.AnimTime = AnimTime;                                                // `param_3[0x5d]`, the owner's
		// Retail writes `layer+4 = 0` and `layer+0x2c = flEnd` on the record BEFORE its event loop,
		// so a handler that re-seeds the slot (`SetLayer 0x10099020` zeroes both) keeps what it wrote.
		Layer.WordsWritten = [this, LayerIndex](const FElysiumSequenceWords& Written)
		{
			AnimOverlay[LayerIndex].SequenceFinished = Written.bSequenceFinished ? 1 : 0;   // layer+4
			AnimOverlay[LayerIndex].LastEventCheck = Written.LastEventCheck;                // layer+0x2c
		};
		const TConstArrayView<FElysiumAnimEvent> LayerEvents = Troika != nullptr
			? Troika->SequenceEvents(Layer.Sequence) : TConstArrayView<FElysiumAnimEvent>();
		ElysiumAnimEvents::DispatchLayer(Layer, LayerEvents, *this, EventHandler);      // 0x10098cd0
	}
}
