#include "Substrate/ElysiumGameSound.h"

#include "ElysiumMoveSolve.h"          // ElysiumMove::U — the one units conversion
#include "Substrate/ElysiumSoundVolumeTable.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGameSound, Log, All);

FElysiumGameSoundEvent FElysiumGameSoundBus::Emit(const FElysiumGameSoundRequest& Request,
	double Now)
{
	// The authored answer is resolved once, HERE, so the retained record is self-describing: a
	// consumer reads a reach in centimetres and an occlusion bit, never a category it has to look
	// back up in a table it does not own.
	const FElysiumSoundLevel& Level = ResolveLevel(Request.Category);

	FElysiumGameSoundEvent Event;
	Event.Position = Request.Position;
	Event.Category = Request.Category;
	// Raw interest is producer-owned. A category only joins authored volume data; it must never
	// infer AI semantics, because arbitrary ambient/category names are not CSound type evidence.
	Event.TypeMask = Request.TypeMask;
	Event.Source = Request.Source;
	Event.Time = Now;
	// Existing category producers predate raw duration plumbing; retain their prior bus window until
	// each producer supplies its recovered source duration.
	Event.ExpireTime = Request.DurationSeconds > 0.0 ? Now + Request.DurationSeconds
		: Now + RetentionSeconds;
	Event.bOccludable = !Request.bForceNonOccludable && Level.bOccludable;

	// The table is authored in Source game units; this is the one place the conversion happens.
	const float TableRadiusCm = Level.RadiusUnits * ElysiumMove::U;
	const float RequestedCm = Request.RadiusCm > 0.f ? Request.RadiusCm : TableRadiusCm;
	// `AdjustSoundDistForStealth`: subtract at insertion, floor at zero.
	Event.RadiusCm = FMath::Max(0.f, RequestedCm - FMath::Max(0.f, Request.StealthHearingReductionCm));
	Event.UnadjustedRadiusCm = RequestedCm;
	Event.StealthHearingReductionCm = FMath::Max(0.f, Request.StealthHearingReductionCm);

	Event.Serial = ++Serial;
	Evict(Now);
	Events.Add(Event);
	return Event;
}

FElysiumGameSoundEvent FElysiumGameSoundBus::Refresh(uint64& Slot,
	const FElysiumGameSoundRequest& Request, double Now)
{
	// The previous record for this slot is REPLACED, so the window never grows with a stimulus that
	// is rewritten every think. `Retire` first, `Emit` second: the emission is what re-serialises
	// the record so a consumer's cursor sees it again.
	Retire(Slot);
	const FElysiumGameSoundEvent Event = Emit(Request, Now);
	Slot = Event.Serial;
	return Event;
}

void FElysiumGameSoundBus::Retire(uint64& Slot)
{
	if (Slot == 0)
	{
		return;
	}
	for (int32 Index = 0; Index < Events.Num(); ++Index)
	{
		if (Events[Index].Serial == Slot)
		{
			// Not counted as an eviction: the record was replaced by its own producer, not dropped
			// by the window, and `NumEvicted` is what a test reads to prove the window bound bit.
			Events.RemoveAt(Index, 1, EAllowShrinking::No);
			break;
		}
	}
	Slot = 0;
}

const FElysiumSoundLevel& FElysiumGameSoundBus::ResolveLevel(FName Category)
{
	if (Volumes == nullptr)
	{
		// No table bound at all: a headless substrate world, or an export that carries no `vdata`.
		// The rulebook logs the load failure once and owns that diagnostic; repeating it per
		// emission would bury it.
		return FElysiumSoundVolumeTable::NormalFallback();
	}
	if (const FElysiumSoundLevel* Row = Volumes->FindCategory(Category.ToString()))
	{
		return *Row;
	}

	const FElysiumSoundLevel* Normal = Volumes->Level(FElysiumSoundVolumeTable::NormalLevel);
	const FElysiumSoundLevel& Fallback =
		Normal != nullptr ? *Normal : FElysiumSoundVolumeTable::NormalFallback();
	bool bAlreadyWarned = false;
	WarnedCategories.Add(Category, &bAlreadyWarned);
	if (!bAlreadyWarned)
	{
		UE_LOG(LogElysiumGameSound, Warning,
			TEXT("game sound category '%s' is not named in sound_volume_table.txt — emitting it at "
				"the normal level instead (%.0f units, %s). Logged once per category."),
			*Category.ToString(), Fallback.RadiusUnits,
			Fallback.bOccludable ? TEXT("occludable") : TEXT("non-occluded"));
	}
	return Fallback;
}

void FElysiumGameSoundBus::Evict(double Now)
{
	// Both bounds trim from the same (oldest) end, so the array stays ordered by serial either way.
	const double Oldest = Now - RetentionSeconds;
	int32 Drop = 0;
	while (Drop < Events.Num() && (Events[Drop].Time < Oldest || Events[Drop].ExpireTime < Now))
	{
		++Drop;
	}
	// Leave room for the event about to be added.
	const int32 Overflow = (Events.Num() - Drop) - (MaxRetained - 1);
	if (Overflow > 0)
	{
		Drop += Overflow;
	}
	if (Drop > 0)
	{
		Events.RemoveAt(0, Drop, EAllowShrinking::No);
		EvictedCount += Drop;
	}
}

TArrayView<const FElysiumGameSoundEvent> FElysiumGameSoundBus::EventsSince(uint64 LastSerial) const
{
	// A linear walk over at most `MaxRetained` records, from the oldest end: the common call is a
	// consumer that is up to date, which finds nothing after one comparison of the tail.
	int32 First = 0;
	while (First < Events.Num() && Events[First].Serial <= LastSerial)
	{
		++First;
	}
	return MakeArrayView(Events.GetData() + First, Events.Num() - First);
}

void FElysiumGameSoundBus::Reset()
{
	Events.Reset();
	Serial = 0;
	EvictedCount = 0;
	WarnedCategories.Reset();
}
