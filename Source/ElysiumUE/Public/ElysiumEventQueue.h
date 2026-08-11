#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumVariant.h"
#include "ElysiumWireReport.h"

// One queued I/O delivery — the unit the event queue sorts and services. `Target` is the raw
// output target string kept verbatim so `!self`/`!activator` resolve at *dispatch* time (R3),
// against `Caller`/`Activator`. `PythonSrc` is field 6 (may be set with or without an I/O
// target). `Serial` breaks FireTime ties so equal-time events keep insertion (FIFO) order.
struct FElysiumIOEvent
{
	double FireTime = 0.0;                 // absolute game seconds (curtime) to deliver at
	FString Target;                        // targetname, or "!self"/"!activator" (resolved at delivery)
	FName Input;                           // input name on the target (FName folds case)
	FElysiumVariant Param;                 // marshalled parameter (field 2)
	FString PythonSrc;                     // field 6 Python call string (may be empty)
	FElysiumEntityHandle Activator;        // propagated activator
	FElysiumEntityHandle Caller;           // the entity that fired the output (`!self`)
	uint64 Serial = 0;                     // FIFO tiebreaker for equal FireTime (set by Add)
	// The authored output row this record came from, so a delivery can be attributed back to the
	// wire that produced it. Unset for records nobody authored — EnqueueInput's console injection,
	// EnqueuePython's ScheduleTask, and the transient event AcceptInput builds to carry dispatch
	// context. Serialized with the record (FElysiumSaveVersion::WireIdentity) so a delivery that
	// lands after a restore still attributes; the tally it feeds is per-session and is not saved.
	FElysiumWireRef Wire;
};

// R4 — the one time-sorted event queue. Every delayed I/O, field-6 Python payload, and (later)
// think/discipline event lives here, keyed on absolute game seconds; never FTimerManager, so
// the queue serializes into saves (R8). Passive: it sorts and hands back due events; the entity
// world drives the drain loop (with the zero-delay loop guard) and delivers through AcceptInput.
// Pause/step flags are here for the Phase-2 single-stepper; the world's service loop honours them.
class FElysiumEventQueue
{
public:
	// Chokepoint 2 (R5): the only way an event enters the queue. Inserts keeping
	// (FireTime, Serial) ascending, so equal-time events stay FIFO.
	void Add(FElysiumIOEvent&& Event)
	{
		Event.Serial = NextSerial++;
		int32 Insert = Events.Num();
		for (int32 i = 0; i < Events.Num(); ++i)
		{
			if (Events[i].FireTime > Event.FireTime)
			{
				Insert = i;
				break;
			}
		}
		Events.Insert(MoveTemp(Event), Insert);
	}

	// 11.9 — the restore path (`docs/architecture/save-architecture.md` §6). A saved event already carries the serial
	// it was queued under, so re-adding it must keep that serial rather than mint a new one: the
	// serial is the FIFO tiebreaker, and re-numbering would reorder equal-time events. Insertion is
	// the same (FireTime, Serial) ordering Add uses, so a payload written out of order still lands
	// sorted. Not a second chokepoint: it takes only events this queue itself wrote out.
	void AddRestored(FElysiumIOEvent&& Event)
	{
		int32 Insert = Events.Num();
		for (int32 i = 0; i < Events.Num(); ++i)
		{
			if (Events[i].FireTime > Event.FireTime
				|| (Events[i].FireTime == Event.FireTime && Events[i].Serial > Event.Serial))
			{
				Insert = i;
				break;
			}
		}
		Events.Insert(MoveTemp(Event), Insert);
	}

	// The serial the next Add will take. Saved and restored so a load cannot hand out a serial that
	// is already sitting in the restored queue.
	uint64 NextSerialValue() const { return NextSerial; }
	void SetNextSerial(uint64 In) { NextSerial = FMath::Max(In, (uint64)1); }

	// The game time the last enqueue was observed at — retail's backward-clock guard state
	// (`docs/vtmb/game_runtime.md` → "Queue service order, recursion and starvation"). The world's
	// AddEvent chokepoint reads it, shifts a rewound deadline forward, and writes it back; the
	// restore path sets it from the snapshot instead, so a load that rewinds the clock to the saved
	// one cannot shift every restored deadline.
	double LastEnqueueValue() const { return LastEnqueue; }
	void SetLastEnqueue(double In) { LastEnqueue = In; }

	// The head is the earliest event; due when its FireTime has been reached.
	bool HasDue(double Now) const { return Events.Num() > 0 && Events[0].FireTime <= Now; }
	const FElysiumIOEvent* PeekEarliest() const { return Events.Num() > 0 ? &Events[0] : nullptr; }

	bool PopEarliest(FElysiumIOEvent& Out)
	{
		if (Events.Num() == 0)
		{
			return false;
		}
		Out = MoveTemp(Events[0]);
		Events.RemoveAt(0);
		return true;
	}

	// Drop every pending event a given caller queued (e.g. when it is killed). Returns the count.
	int32 Cancel(const FElysiumEntityHandle& Caller)
	{
		return Events.RemoveAll([&Caller](const FElysiumIOEvent& E) { return E.Caller == Caller; });
	}

	void Reset() { Events.Reset(); NextSerial = 1; LastEnqueue = 0.0; PendingSteps = 0; bPaused = false; }
	int32 Num() const { return Events.Num(); }
	const TArray<FElysiumIOEvent>& Pending() const { return Events; }

	// --- Pause / single-step (Phase 2 `ent_pause`/`ent_step`) ------------------------------
	void Pause() { bPaused = true; }
	void Resume() { bPaused = false; PendingSteps = 0; }
	bool IsPaused() const { return bPaused; }
	void RequestSteps(int32 N) { PendingSteps += FMath::Max(0, N); }
	int32 StepsPending() const { return PendingSteps; }
	void ConsumeStep() { if (PendingSteps > 0) { --PendingSteps; } }

private:
	TArray<FElysiumIOEvent> Events;   // ascending by (FireTime, Serial)
	uint64 NextSerial = 1;
	double LastEnqueue = 0.0;         // game seconds at the last enqueue (backward-clock guard)
	bool bPaused = false;
	int32 PendingSteps = 0;
};
