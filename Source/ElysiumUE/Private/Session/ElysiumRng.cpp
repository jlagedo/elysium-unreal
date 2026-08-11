#include "ElysiumRng.h"

namespace
{
	constexpr int32 GStreamCount = static_cast<int32>(EElysiumRngStream::Count);

	FRandomStream GStreams[GStreamCount];
	int32 GSessionSeed = 0;
	bool bGSeeded = false;

	const TCHAR* const GStreamNames[GStreamCount] =
	{
		TEXT("OneOfSet"), TEXT("LogicTimer"), TEXT("LogicCase"), TEXT("Dice"), TEXT("Ambient"),
		TEXT("Chargen"), TEXT("NpcMaker")
	};

	// Each stream's seed is derived from the session seed by a distinct odd multiplier, so a save's
	// one recorded session seed reproduces all five and no two streams walk the same sequence.
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
