#include "ElysiumRng.h"

namespace
{
	constexpr int32 GStreamCount = static_cast<int32>(EElysiumRngStream::Count);

	FRandomStream GStreams[GStreamCount];
	int32 GSessionSeed = 0;
	bool bGSeeded = false;

	// Unsized deliberately: a sized declaration would pad a missing entry with a null and hand the
	// readable dump a nullptr for the new stream. The static_assert below is what makes adding an
	// enumerator without a name a compile error instead.
	const TCHAR* const GStreamNames[] =
	{
		TEXT("OneOfSet"), TEXT("LogicTimer"), TEXT("LogicCase"), TEXT("Dice"), TEXT("Ambient"),
		TEXT("Chargen"), TEXT("NpcMaker"), TEXT("NpcSchedule"), TEXT("Reaction"), TEXT("Effects"),
		TEXT("Footsteps"), TEXT("Terminal")
	};
	static_assert(UE_ARRAY_COUNT(GStreamNames) == GStreamCount,
		"every EElysiumRngStream enumerator needs its own name in GStreamNames");

	// Each stream's seed is the session seed through one shared odd multiplier plus a per-stream
	// ADDITIVE offset — the multiplier is the same for every stream and the offset is what separates
	// them. A save's one recorded session seed therefore reproduces every stream, and no two walk the
	// same sequence.
	int32 DerivedSeed(int32 SessionSeed, int32 StreamIndex)
	{
		return static_cast<int32>(static_cast<uint32>(SessionSeed) * 2654435761u
			+ static_cast<uint32>(StreamIndex) * 40503u + 1u);
	}

	void EnsureSeeded()
	{
		if (!bGSeeded)
		{
			ElysiumRng::SeedAll(0);
		}
	}
}

namespace ElysiumRng
{
	FRandomStream& Stream(EElysiumRngStream Which)
	{
		EnsureSeeded();
		const int32 Index = FMath::Clamp(static_cast<int32>(Which), 0, GStreamCount - 1);
		return GStreams[Index];
	}

	void SeedAll(int32 SessionSeed)
	{
		GSessionSeed = SessionSeed;
		bGSeeded = true;
		for (int32 i = 0; i < GStreamCount; ++i)
		{
			GStreams[i].Initialize(DerivedSeed(SessionSeed, i));
		}
	}

	int32 SessionSeed() { return GSessionSeed; }

	void Snapshot(TArray<FState>& Out)
	{
		EnsureSeeded();
		Out.Reset(GStreamCount);
		for (int32 i = 0; i < GStreamCount; ++i)
		{
			Out.Add({ GStreams[i].GetInitialSeed(), GStreams[i].GetCurrentSeed() });
		}
	}

	void Restore(TArrayView<const FState> In)
	{
		bGSeeded = true;
		// A payload written by a build with fewer streams restores what it carries and leaves the
		// rest at their session seed — the same forward-compatibility rule the field walk uses.
		for (int32 i = 0; i < FMath::Min(In.Num(), GStreamCount); ++i)
		{
			// `Initialize` is the only writer FRandomStream exposes, and it sets both the initial and
			// the current seed. Seeding with the saved **position** is what makes the restored run
			// continue the saved sequence; the recorded initial seed is carried for the readable dump
			// only, because nothing in this codebase calls FRandomStream::Reset().
			GStreams[i].Initialize(In[i].Current);
		}
	}

	const TCHAR* Name(EElysiumRngStream Which)
	{
		const int32 Index = FMath::Clamp(static_cast<int32>(Which), 0, GStreamCount - 1);
		return GStreamNames[Index];
	}
}
