#pragma once

#include "CoreMinimal.h"

struct FElysiumIOEvent;
struct FElysiumOutputDef;
struct FElysiumVariant;
class FElysiumEntity;
class FElysiumEntityWorld;

// The debug tap on the two chokepoints. Every observability facility (the LogElysiumIO
// stream, the always-on ring buffer, VLOG, and the entity/queue windows) is a sink;
// three log lines instrument the whole game. The world notifies its sinks at each I/O event.
// Params are passed by reference to forward-declared types, so a sink implementation includes
// only what it actually reads.
class IElysiumIOSink
{
public:
	virtual ~IElysiumIOSink() = default;

	// An entity fired a named output; one row matched and was queued (Times not yet exhausted).
	virtual void OnOutputFired(double Now, const FElysiumEntity& Source, const FElysiumOutputDef& Output) {}
	// The queued delivery entered the event queue (chokepoint 2).
	virtual void OnQueued(double Now, const FElysiumIOEvent& Event) {}
	// A due event resolved to a live target and its input thunk ran (chokepoint 1).
	virtual void OnDelivered(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event) {}
	// A due event named a target that resolved to nothing (dead wire — retail data has these).
	virtual void OnUnknownTarget(double Now, const FElysiumIOEvent& Event) {}
	// A due event's target has no such input in its class chain.
	virtual void OnUnknownInput(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event) {}
	// A field-6 Python payload was handed to the script host.
	virtual void OnPython(double Now, const FElysiumIOEvent& Event, const FElysiumVariant& Result) {}
	// The zero-delay drain hit its iteration cap and bailed (runaway I/O loop).
	virtual void OnLoopGuard(double Now, int32 Delivered) {}
};

// The always-on I/O history (Source's env_debughistory, 1,000 lines). Holds formatted, ordered
// lines for postmortem forensics — dumpable on demand, cheap to serialize into saves. It
// records the *delivered* events (I/O, unknowns, Python) — the causality stream — not the
// intermediate queue churn. Formatting is delegated to the world so handles resolve to the
// canonical `#idx name(class)` string.
class FElysiumRingBufferSink final : public IElysiumIOSink
{
public:
	explicit FElysiumRingBufferSink(const FElysiumEntityWorld& InWorld, int32 InCapacity = 1000);

	virtual void OnDelivered(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event) override;
	virtual void OnUnknownTarget(double Now, const FElysiumIOEvent& Event) override;
	virtual void OnUnknownInput(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event) override;
	virtual void OnPython(double Now, const FElysiumIOEvent& Event, const FElysiumVariant& Result) override;
	virtual void OnLoopGuard(double Now, int32 Delivered) override;

	int32 Num() const { return Count; }
	int32 Capacity() const { return Buffer.Num(); }
	// Append the last N recorded lines (or all, if N <= 0) in chronological order.
	void CollectOrdered(int32 LastN, TArray<FString>& Out) const;

private:
	void Push(FString&& Line);

	const FElysiumEntityWorld& World;
	TArray<FString> Buffer;   // fixed-size ring
	int32 Head = 0;           // next write slot
	int32 Count = 0;          // entries written (saturates at Capacity)
};

// The LogElysiumIO stream (the `developer 2` equivalent) plus a VLOG line per delivery for
// offline scrubbing. Verbose logs the output-fired/queued churn; Display logs the delivered
// dispatches, unknowns, and the loop-guard bail.
class FElysiumLogSink final : public IElysiumIOSink
{
public:
	explicit FElysiumLogSink(const FElysiumEntityWorld& InWorld);

	virtual void OnOutputFired(double Now, const FElysiumEntity& Source, const FElysiumOutputDef& Output) override;
	virtual void OnQueued(double Now, const FElysiumIOEvent& Event) override;
	virtual void OnDelivered(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event) override;
	virtual void OnUnknownTarget(double Now, const FElysiumIOEvent& Event) override;
	virtual void OnUnknownInput(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event) override;
	virtual void OnPython(double Now, const FElysiumIOEvent& Event, const FElysiumVariant& Result) override;
	virtual void OnLoopGuard(double Now, int32 Delivered) override;

private:
	const FElysiumEntityWorld& World;
};
