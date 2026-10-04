#include "Substrate/ElysiumAnimEvents.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAnimEvents, Log, All);

namespace
{
	// `_DAT_104491b4`, the look-ahead both bodies multiply the cycle rate by (`0x10091880`,
	// `0x10098cd0`): 0.1 s of clip time.
	constexpr float GEventLookAheadSeconds = 0.1f;

	// The event loop both bodies share (`0x10091880`'s and `0x10098cd0`'s are the same listing): one
	// pass over the descriptor's table in FILE order. A record fires when its id is below 5000 and
	// its cycle is in `[Start, End)`, or -- with `seqdesc.flags & 1` and `End >= 1` -- below
	// `End - 1` (the wrap, swept once). Each goes to the HANDLER's slot 259 (`+0x40c`).
	void FireWindow(const FElysiumSequenceWords& Words, TConstArrayView<FElysiumAnimEvent> Events,
		float Start, float End, FElysiumEntity& Source, FElysiumEntity& Handler)
	{
		// Read once: a handler may reset the sequence under the caller, and retail's loop keeps
		// walking the descriptor it started on with the rate it computed at entry (`fVar1`).
		const float Cycle = Words.Cycle;
		const float Rate = Words.CycleRate;
		const float AnimTime = Words.AnimTime;
		const bool bDescriptorLoops = Words.bDescriptorLoops;
		const FString CensusOwner = Words.CensusOwner;
		const FString CensusLabel = Words.CensusLabel.IsEmpty()
			? FString::Printf(TEXT("seq %d"), Words.Sequence) : Words.CensusLabel;

		for (int32 Index = 0; Index < Events.Num(); ++Index)
		{
			const FElysiumAnimEvent& Record = Events[Index];
			// `local_64 <= cycle && cycle < local_68`, or `flags & 1 && 1.0 <= flEnd && cycle <
			// flEnd - 1.0`.
			const bool bInWindow = (Start <= Record.Cycle && Record.Cycle < End)
				|| (bDescriptorLoops && End >= 1.f && Record.Cycle < End - 1.f);
			if (!bInWindow)
			{
				continue;
			}
			// `(int)pfVar6[1] < 5000`: an id at or above the ceiling is never offered to a handler.
			// It is counted in the census (the port's work list, not a retail effect).
			const bool bAboveBand = Record.Event >= ElysiumAnimEvents::ServerDispatchCeiling;
			bool bClaimed = false;
			if (!bAboveBand)
			{
				// `eventtime = (cycle - m_flCycle) / rate + m_flAnimTime` (a layer: the OWNER's
				// `m_flAnimTime`, `param_3[0x5d]`). Retail computes it into the `animevent_t` it
				// hands over; no recovered handler reads it and `FElysiumAnimEvent` has no word for
				// it, so it is computed and dropped.
				const float EventTime = (Record.Cycle - Cycle) / Rate + AnimTime;
				(void)EventTime;
				// The AI trace's `animevent` (`<id> <options>`; debug output only, behind its sink),
				// emitted for the SOURCE: the one tap for the NPC, the player and the camera. It
				// stands where retail's `DisplayAnimEvent` (`m_debugOverlays < 0`) stands.
				if (Source.World != nullptr && Source.World->HasAiTraceSink())
				{
					Source.World->EmitAiTrace(Source, TEXT("animevent"),
						FString::Printf(TEXT("%d %s"), Record.Event, *Record.Options));
				}
				bClaimed = Handler.HandleAnimEvent(Record);          // slot 259, `*param_2 + 0x40c`
			}
			if (bClaimed)
			{
				continue;   // claimed and acted on; the handler owns its own observability
			}
			// Unclaimed. The census IS the report: one Verbose line the first time each
			// (id, owner, label) is seen, never one per occurrence.
			if (ElysiumAnimEventCensus::Record(Record, CensusOwner, CensusLabel, bAboveBand))
			{
				UE_LOG(LogElysiumAnimEvents, Verbose,
					TEXT("anim event %d on '%s'@'%s' is unclaimed%s%s"),
					Record.Event, *CensusLabel, *CensusOwner,
					bAboveBand ? TEXT(" (above the server dispatch band)") : TEXT(""),
					Record.Options.IsEmpty()
						? TEXT("") : *FString::Printf(TEXT(", options '%s'"), *Record.Options));
			}
		}
	}
}

namespace ElysiumAnimEvents
{
	bool DispatchBase(FElysiumSequenceWords& Words, TConstArrayView<FElysiumAnimEvent> Events,
		FElysiumEntity& Source, FElysiumEntity& Handler)
	{
		// `CBaseAnimating::DispatchAnimEvents` `0x10091880`, in its order. The interval argument is
		// unused by retail and is not an input here. `GetModelPtr() == 0` (nothing at all) is the
		// caller's precondition.
		const bool bWasFinished = Words.bSequenceFinished;       // `cVar2 = m_bSequenceFinished`, at entry
		float Start = Words.LastEventCheck;                      // `local_64 = m_flLastEventCheck` +0x658
		Words.bSequenceFinished = false;                         // `m_bSequenceFinished = 0` +0x65c
		// `flEnd = GetSequenceCycleRate x m_flPlaybackRate x 0.1 (0x104491b4) + m_flCycle`.
		float End = Words.CycleRate * GEventLookAheadSeconds + Words.Cycle;
		if (!Words.bLoops)                                       // `m_bSequenceLoops == 0` +0x65d
		{
			// With no seqdesc on a non-looping sequence neither the finish nor past-half is written.
			if (Words.bHasDescriptor)                            // `iVar3 != 0`
			{
				if (End >= 1.f || End < 0.f)                     // `0x104454c0 <= flEnd || flEnd < 0x1044fab0`
				{
					Words.bSequenceFinished = true;              // +0x65c = 1
					End = 1.f;                                   // the clamp: `local_68 = 1.0`
				}
				else
				{
					Words.bSequencePastHalf = End > 0.5f;        // +0x568: `flEnd <= 0x104454d0` -> 0, else 1
				}
			}
		}
		else
		{
			// Looping: the same without the clamp and without the seqdesc test...
			if (End >= 1.f || End < 0.f)
			{
				Words.bSequenceFinished = true;                  // +0x65c = 1, `flEnd` kept
			}
			else
			{
				Words.bSequencePastHalf = End > 0.5f;            // +0x568
			}
			// ...plus the start wrapped into `[0,1)` (`0x10449280`, one step each way).
			if (Start >= 1.f)
			{
				Start -= 1.f;
			}
			if (Start < 0.f)
			{
				Start += 1.f;
			}
		}
		Words.LastEventCheck = End;                              // `m_flLastEventCheck = flEnd` +0x658
		// The three words are on retail's object before the first event; a caller whose handlers can
		// reset the sequence takes them now.
		if (Words.WordsWritten)
		{
			Words.WordsWritten(Words);
		}
		const bool bFinished = Words.bSequenceFinished;
		if (Words.bHasDescriptor)                                // `iVar3 != 0 && 0 < numevents`
		{
			FireWindow(Words, Events, Start, End, Source, Handler);
		}
		// `m_bSequenceFinished != 0 && cVar2 == 0` -> `OnSequenceFinished` (`0x10091b9a` ->
		// `0x10091c80`, a direct call): the caller's.
		return bFinished && !bWasFinished;
	}

	void DispatchLayer(FElysiumSequenceWords& Layer, TConstArrayView<FElysiumAnimEvent> Events,
		FElysiumEntity& Source, FElysiumEntity& Handler)
	{
		// `0x10098cd0`, one `CAnimationLayer`, in its order: no "in use" test, no weight test, no
		// loop test, no clamp, no start wrap, no past-half, and no `OnSequenceFinished`.
		const float Start = Layer.LastEventCheck;                // `fVar2 = layer+0x2c`
		Layer.bSequenceFinished = false;                         // `layer+4 = 0`, never set here
		// `GetSequenceCycleRate(owner, layer+8) x layer+0x10 x 0.1 (0x104491b4) + layer+0xc`.
		const float End = Layer.CycleRate * GEventLookAheadSeconds + Layer.Cycle;
		Layer.LastEventCheck = End;                              // `layer+0x2c = flEnd`
		if (Layer.WordsWritten)
		{
			Layer.WordsWritten(Layer);
		}
		if (Layer.bHasDescriptor)                                // `iVar4 != 0 && 0 < numevents`
		{
			FireWindow(Layer, Events, Start, End, Source, Handler);
		}
	}
}

// --- The census — the work list of event ids nothing claims yet ---

namespace
{
	// Keyed on id + owner + label, which is what makes the tally a work list: one row per thing to
	// implement, however many bodies fire it or how often.
	TMap<FString, ElysiumAnimEventCensus::FRow>& CensusRows()
	{
		static TMap<FString, ElysiumAnimEventCensus::FRow> Instance;
		return Instance;
	}
}

namespace ElysiumAnimEventCensus
{
	bool Record(const FElysiumAnimEvent& Event, const FString& OwnerStem, const FString& Label,
		bool bAboveServerBand)
	{
		const FString Key = FString::Printf(TEXT("%d|%s|%s"), Event.Event, *OwnerStem, *Label);
		FRow& Row = CensusRows().FindOrAdd(Key);
		const bool bNew = Row.Count == 0;
		if (bNew)
		{
			Row.Event = Event.Event;
			Row.OwnerStem = OwnerStem;
			Row.Label = Label;
			Row.Options = Event.Options;
			Row.bAboveServerBand = bAboveServerBand;
		}
		++Row.Count;
		return bNew;
	}

	void Collect(TArray<FRow>& Out)
	{
		Out.Reset();
		CensusRows().GenerateValueArray(Out);
		Out.Sort([](const FRow& A, const FRow& B)
		{
			if (A.Count != B.Count) { return A.Count > B.Count; }
			if (A.Event != B.Event) { return A.Event < B.Event; }
			// Case-INSENSITIVELY, which is what both the ordering and the row key already are:
			// `FString::operator<` compares that way and the map hashing the rows compares that way,
			// so a case-sensitive discriminator here would claim two rows differ and then order them
			// by a comparison that says they do not.
			if (A.OwnerStem.Compare(B.OwnerStem, ESearchCase::IgnoreCase) != 0)
			{
				return A.OwnerStem < B.OwnerStem;
			}
			return A.Label < B.Label;
		});
	}

	void Clear()
	{
		CensusRows().Reset();
	}

	int32 Num()
	{
		return CensusRows().Num();
	}
}

// --- Verification command -----------------------------------------------------------------
// `elysium.animevents` reads the census back: every sequence-event id that fired and reached no
// handler since load, what model and label it fired from, and how hard the shipped content leans on
// it. `elysium.animevents clear` resets the counts so one map load or one fight can be measured on
// its own. The same shape as `elysium.stubs`, and for the same reason — an unclaimed id is work
// that has not landed, not a fault to warn about.

static FAutoConsoleCommandWithWorldAndArgs GElysiumAnimEventsCmd(
	TEXT("elysium.animevents"),
	TEXT("elysium.animevents [clear] — list every unclaimed sequence-event id fired since load, ")
	TEXT("most-fired first"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() >= 1 && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
		{
			ElysiumAnimEventCensus::Clear();
			UE_LOG(LogElysiumAnimEvents, Display, TEXT("anim-event census cleared"));
			return;
		}

		TArray<ElysiumAnimEventCensus::FRow> Rows;
		ElysiumAnimEventCensus::Collect(Rows);
		if (Rows.Num() == 0)
		{
			UE_LOG(LogElysiumAnimEvents, Display,
				TEXT("no unclaimed sequence event fired since load"));
			return;
		}

		int32 Total = 0;
		for (const ElysiumAnimEventCensus::FRow& R : Rows) { Total += R.Count; }
		UE_LOG(LogElysiumAnimEvents, Display,
			TEXT("%d unclaimed sequence-event ids, %d fires total"), Rows.Num(), Total);
		for (const ElysiumAnimEventCensus::FRow& R : Rows)
		{
			// The band is spelled per row because the two absences have different repairs: an id
			// below the ceiling reached `HandleAnimEvent` and nothing claimed it, while one at or
			// above it was never offered to a handler at all.
			UE_LOG(LogElysiumAnimEvents, Display, TEXT("  %6d  %5d  %s@%s%s%s"),
				R.Count, R.Event, *R.Label, *R.OwnerStem,
				R.bAboveServerBand ? TEXT("  [above server band]") : TEXT(""),
				R.Options.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  options: %s"), *R.Options));
		}
	}));
