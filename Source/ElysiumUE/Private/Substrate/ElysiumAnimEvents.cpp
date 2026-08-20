#include "Substrate/ElysiumAnimEvents.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAnimEvents, Log, All);

namespace
{
	// One half-open interval of the timeline, walked in FILE order. The order is data: records that
	// share a cycle fire in the order the model declares them, so this never sorts and never
	// dedupes.
	//
	// `Lo <= cycle < Hi` is the recovered comparison verbatim. The exclusive upper bound is what
	// makes a record authored at exactly 1.0 unreachable: a phase is normalized into `[0,1]`, so the
	// widest interval any frame can present is `[x, 1)`. The parser permits such a record — the
	// exporter writes what the model declares — and retail never fires it either.
	void FireIn(const TArray<FElysiumAnimEvent>& Timeline, float Lo, float Hi,
		TArray<const FElysiumAnimEvent*>& OutFired)
	{
		for (const FElysiumAnimEvent& Record : Timeline)
		{
			if (Record.Cycle >= Lo && Record.Cycle < Hi)
			{
				OutFired.Add(&Record);
			}
		}
	}
}

namespace ElysiumAnimEvents
{
	void Advance(const TArray<FElysiumAnimEvent>* Timeline, const FElysiumClipPhase& Phase,
		FElysiumAnimEventCursor& InOut, TArray<const FElysiumAnimEvent*>& OutFired)
	{
		OutFired.Reset();

		if (!Phase.IsValid())
		{
			// A channel standing on nothing. Not a failure — most bodies play on the base channel
			// alone — but the cursor must forget where it was, or the next clip to arm on this
			// channel would inherit a phase from a different timeline.
			InOut.Reset();
			return;
		}

		// The identity, all three parts. A label alone is not a clip (two banks can declare the same
		// key) and a clip alone is not a play: the same sequence re-armed fires its timeline again
		// from zero, and only `PlayId` can tell that from a loop.
		const bool bSamePlay = InOut.bArmed
			&& InOut.PlayId == Phase.PlayId
			&& InOut.OwnerStem.Equals(Phase.OwnerStem, ESearchCase::IgnoreCase)
			&& InOut.Label.Equals(Phase.Label, ESearchCase::IgnoreCase);

		// A phase is normalized. Clamping here keeps the interval arithmetic total; a producer that
		// hands over a phase outside the range is a defect its own seam reports, because this rule
		// has no way to name which body it came from. A non-finite cycle is anchored at zero rather
		// than clamped, because `FMath::Clamp` passes a NaN straight through: stored on the cursor it
		// would fail every subsequent comparison and silently retire that clip's timeline for the
		// rest of the play.
		const float Cycle = FMath::IsFinite(Phase.Cycle)
			? FMath::Clamp(Phase.Cycle, 0.0f, 1.0f)
			: 0.0f;
		// A new play is anchored at zero, so its first frame is the interval `[0, Cycle)` — which is
		// what makes a record authored at cycle 0 fire on the first advance rather than being
		// stepped over.
		const float Last = bSamePlay ? InOut.LastCycle : 0.0f;

		InOut.OwnerStem = Phase.OwnerStem;
		InOut.Label = Phase.Label;
		InOut.PlayId = Phase.PlayId;
		InOut.LastCycle = Cycle;
		InOut.bArmed = true;

		if (Timeline == nullptr || Timeline->IsEmpty())
		{
			// Most sequences declare no timeline, which is an ordinary absence. The cursor still
			// advanced above, so a clip whose timeline arrives later cannot fire a backlog.
			return;
		}

		if (Cycle > Last)
		{
			FireIn(*Timeline, Last, Cycle, OutFired);
			return;
		}
		if (Cycle < Last && Phase.bLooping)
		{
			// The wrap, visited exactly once: the tail of the lap that ended, then the head of the
			// one that began. Two calls rather than one modular test, because the file order has to
			// hold inside each half and a record in the tail precedes every record in the head.
			FireIn(*Timeline, Last, 1.0f, OutFired);
			FireIn(*Timeline, 0.0f, Cycle, OutFired);
			return;
		}
		// What is left is a zero-delta frame (a paused or fully faded clip), or a backwards phase on
		// a NON-looping clip — a seek. Neither fires anything: a seek re-anchors the cursor, which
		// the write above already did, and replaying the skipped interval would fire a footstep for
		// a step the body never took.
	}
}

// ============================================================================================
// The census — the work list of event ids nothing claims yet
// ============================================================================================

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
			if (!A.OwnerStem.Equals(B.OwnerStem)) { return A.OwnerStem < B.OwnerStem; }
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
