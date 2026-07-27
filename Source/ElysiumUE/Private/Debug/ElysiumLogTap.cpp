#include "Debug/ElysiumLogTap.h"

#include "Misc/OutputDeviceRedirector.h"

FElysiumLogTap& ElysiumLogTapGet()
{
	static FElysiumLogTap Tap;
	return Tap;
}

FElysiumLogTap::FElysiumLogTap(int32 InCapacity)
{
	Buffer.SetNum(FMath::Max(1, InCapacity));
}

FElysiumLogTap::~FElysiumLogTap()
{
	Remove();
}

void FElysiumLogTap::Install()
{
	if (!bInstalled && GLog)
	{
		GLog->AddOutputDevice(this);
		bInstalled = true;
	}
}

void FElysiumLogTap::Remove()
{
	if (bInstalled && GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	bInstalled = false;
}

void FElysiumLogTap::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FScopeLock ScopeLock(&Lock);

	FLine& Slot = Buffer[Head];
	Slot.Category = Category.ToString();
	Slot.Verbosity = ToString(Verbosity);
	Slot.Text = V;

	Head = (Head + 1) % Buffer.Num();
	Count = FMath::Min(Count + 1, Buffer.Num());
	++Total;
}

uint64 FElysiumLogTap::Cursor() const
{
	FScopeLock ScopeLock(&Lock);
	return Total;
}

void FElysiumLogTap::CollectOrdered(int32 LastN, const FString& CategoryFilter, TArray<FLine>& Out) const
{
	FScopeLock ScopeLock(&Lock);

	// Oldest held entry: Head once the ring has wrapped, 0 while it is still filling.
	const int32 Cap = Buffer.Num();
	const int32 Start = (Count == Cap) ? Head : 0;

	TArray<FLine> Matched;
	Matched.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const FLine& Line = Buffer[(Start + i) % Cap];
		if (!CategoryFilter.IsEmpty() && !Line.Category.Contains(CategoryFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		Matched.Add(Line);
	}

	const int32 Take = (LastN <= 0) ? Matched.Num() : FMath::Min(LastN, Matched.Num());
	Out.Append(Matched.GetData() + (Matched.Num() - Take), Take);
}

void FElysiumLogTap::CollectSince(uint64 SinceCursor, TArray<FLine>& Out) const
{
	FScopeLock ScopeLock(&Lock);

	if (Total <= SinceCursor)
	{
		return;
	}
	// Lines produced since the cursor, clamped to what the ring still holds (a burst larger than
	// the capacity silently drops its oldest — same contract as the I/O ring buffer).
	const int32 Wanted = static_cast<int32>(FMath::Min<uint64>(Total - SinceCursor, static_cast<uint64>(Count)));
	const int32 Cap = Buffer.Num();
	for (int32 i = Count - Wanted; i < Count; ++i)
	{
		const int32 Start = (Count == Cap) ? Head : 0;
		Out.Add(Buffer[(Start + i) % Cap]);
	}
}
