#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

// S8 determinism (`save-architecture.md` §8) — every game-visible random draw comes from a **named,
// owned stream** whose state is in the save's `Session` block. `FMath::Rand()` is banned in gameplay
// code for the same reason `FTimerManager` is: a save is only worth as much as the run that follows
// it, and a draw the save cannot carry makes the run after a load a different run.
//
// The streams are module-static rather than members of a subsystem because their call sites are
// plain C++ leaves (a `logic_case`'s pick, `OneOfSet`'s per-frame roll) that hold no session
// pointer. Their *lifetime* is still the session's: `SeedAll` runs at New Game and at EndSession,
// and Snapshot/Restore are the save block.
enum class EElysiumRngStream : uint8
{
	OneOfSet,      // the 589 dialogue gates' 1-of-N selector (9.7d)
	LogicTimer,    // logic_timer's UseRandomTime refire interval
	LogicCase,     // logic_case PickRandom / PickRandomShuffle
	Dice,          // the World-of-Darkness d10 resolver (9.6, `recovered/dice-system.md`)
	Ambient,       // ambient idle picks and the RandomSound scheduler
	Chargen,       // which phrasing of a wizard question the quiz asks (9.4f)
	Count
};

namespace ElysiumRng
{
	// The live stream. Draw through this, never through FMath::Rand.
	FRandomStream& Stream(EElysiumRngStream Which);

	// Re-seed every stream from one session seed (New Game, EndSession). Each stream takes a
	// distinct derived seed so two systems drawing the same number of times do not correlate.
	void SeedAll(int32 SessionSeed);

	// The session seed the streams were last seeded from — saved so a reader can report it.
	int32 SessionSeed();

	// The save block: each stream's initial seed and its current position, in stream order.
	struct FState
	{
		int32 Seed = 0;
		int32 Current = 0;
	};
	void Snapshot(TArray<FState>& Out);
	void Restore(TArrayView<const FState> In);

	// A stable name per stream, for the readable payload dump (`elysium.save.diff`).
	const TCHAR* Name(EElysiumRngStream Which);
}
