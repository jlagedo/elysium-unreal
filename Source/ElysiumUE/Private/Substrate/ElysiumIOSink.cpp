#include "ElysiumIOSink.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEventQueue.h"
#include "ElysiumVariant.h"

#if ENABLE_VISUAL_LOG
#include "VisualLogger/VisualLogger.h"
#endif

// The dedicated I/O category (Source's `developer 2` stream). Verbose carries the output-fired /
// queued churn; Display carries the delivered dispatches, dead wires, and the loop-guard bail.
DEFINE_LOG_CATEGORY_STATIC(LogElysiumIO, Log, All);

// --- Ring buffer sink -------------------------------------------------------------------

FElysiumRingBufferSink::FElysiumRingBufferSink(const FElysiumEntityWorld& InWorld, int32 InCapacity)
	: World(InWorld)
{
	Buffer.SetNum(FMath::Max(1, InCapacity));
}

void FElysiumRingBufferSink::Push(FString&& Line)
{
	Buffer[Head] = MoveTemp(Line);
	Head = (Head + 1) % Buffer.Num();
	Count = FMath::Min(Count + 1, Buffer.Num());
}

void FElysiumRingBufferSink::CollectOrdered(int32 LastN, TArray<FString>& Out) const
{
	const int32 Take = (LastN <= 0) ? Count : FMath::Min(LastN, Count);
	// The oldest of the Take entries sits `Count` back from Head, then advanced by the skipped ones.
	const int32 Start = (Head - Count + (Count - Take) + Buffer.Num() * 2) % Buffer.Num();
	Out.Reserve(Out.Num() + Take);
	for (int32 i = 0; i < Take; ++i)
	{
		Out.Add(Buffer[(Start + i) % Buffer.Num()]);
	}
}

void FElysiumRingBufferSink::OnDelivered(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event)
{
	const TCHAR* Note = Event.PythonSrc.IsEmpty() ? nullptr : TEXT("[py]");
	Push(World.FormatEventLine(Now, Event, Target.DebugString(), Note));
}

void FElysiumRingBufferSink::OnUnknownTarget(double Now, const FElysiumIOEvent& Event)
{
	const FString Label = FString::Printf(TEXT("\"%s\""), *Event.Target);
	Push(World.FormatEventLine(Now, Event, Label, TEXT("[no target]")));
}

void FElysiumRingBufferSink::OnUnknownInput(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event)
{
	Push(World.FormatEventLine(Now, Event, Target.DebugString(), TEXT("[no input]")));
}

void FElysiumRingBufferSink::OnPython(double Now, const FElysiumIOEvent& Event, const FElysiumVariant& Result)
{
	Push(FString::Printf(TEXT("(%8.3f) %s  py: __main__.%s => %s"),
		Now, *World.DescribeHandle(Event.Caller), *Event.PythonSrc, *Result.Describe()));
}

void FElysiumRingBufferSink::OnLoopGuard(double Now, int32 Delivered)
{
	// The cap defers a still-due tail across a think boundary retail never observes, so the history
	// has to mark where the pass stopped — otherwise the deferred deliveries read as spontaneous
	// next frame. The queue is time-sorted, so the due tail is its head run.
	int32 StillDue = 0;
	for (const FElysiumIOEvent& Pending : World.Queue().Pending())
	{
		if (Pending.FireTime > Now)
		{
			break;
		}
		++StillDue;
	}
	Push(FString::Printf(TEXT("(%8.3f) -- loop guard: %d delivered, %d still due --"),
		Now, Delivered, StillDue));
}

// --- Log + VLOG sink --------------------------------------------------------------------

FElysiumLogSink::FElysiumLogSink(const FElysiumEntityWorld& InWorld)
	: World(InWorld)
{
}

namespace
{
	// Route one formatted line to the log category and the visual logger together.
	void EmitIO(const FElysiumEntityWorld& World, const FString& Line, bool bWarn)
	{
		if (bWarn)
		{
			UE_LOG(LogElysiumIO, Display, TEXT("%s"), *Line);
		}
		else
		{
			UE_LOG(LogElysiumIO, Verbose, TEXT("%s"), *Line);
		}
#if ENABLE_VISUAL_LOG
		if (AActor* Owner = World.GetOwnerActor())
		{
			UE_VLOG(Owner, LogElysiumIO, Log, TEXT("%s"), *Line);
		}
#endif
	}
}

void FElysiumLogSink::OnOutputFired(double Now, const FElysiumEntity& Source, const FElysiumOutputDef& Output)
{
	EmitIO(World, FString::Printf(TEXT("(%8.3f) fire %s.%s -> %s.%s(%s) [+%.2fs]"),
		Now, *Source.DebugString(), *Output.Name,
		Output.Target.IsEmpty() ? TEXT("<py-only>") : *Output.Target, *Output.Input, *Output.Param, Output.Delay),
		/*bWarn*/ false);
}

void FElysiumLogSink::OnQueued(double Now, const FElysiumIOEvent& Event)
{
	EmitIO(World, FString::Printf(TEXT("(%8.3f) queue @%.3f -> %s.%s(%s)"),
		Now, Event.FireTime, *Event.Target, *Event.Input.ToString(), *Event.Param.ToString()),
		/*bWarn*/ false);
}

void FElysiumLogSink::OnDelivered(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event)
{
	const TCHAR* Note = Event.PythonSrc.IsEmpty() ? nullptr : TEXT("[py]");
	EmitIO(World, World.FormatEventLine(Now, Event, Target.DebugString(), Note), /*bWarn*/ false);
}

void FElysiumLogSink::OnUnknownTarget(double Now, const FElysiumIOEvent& Event)
{
	const FString Label = FString::Printf(TEXT("\"%s\""), *Event.Target);
	EmitIO(World, World.FormatEventLine(Now, Event, Label, TEXT("[no target]")), /*bWarn*/ true);
}

void FElysiumLogSink::OnUnknownInput(double Now, const FElysiumEntity& Target, const FElysiumIOEvent& Event)
{
	EmitIO(World, World.FormatEventLine(Now, Event, Target.DebugString(), TEXT("[no input]")), /*bWarn*/ true);
}

void FElysiumLogSink::OnPython(double Now, const FElysiumIOEvent& Event, const FElysiumVariant& Result)
{
	EmitIO(World, FString::Printf(TEXT("(%8.3f) %s  py: __main__.%s => %s"),
		Now, *World.DescribeHandle(Event.Caller), *Event.PythonSrc, *Result.Describe()),
		/*bWarn*/ false);
}

void FElysiumLogSink::OnLoopGuard(double Now, int32 Delivered)
{
	UE_LOG(LogElysiumIO, Warning, TEXT("(%8.3f) zero-delay loop guard tripped after %d deliveries this frame"),
		Now, Delivered);
}
