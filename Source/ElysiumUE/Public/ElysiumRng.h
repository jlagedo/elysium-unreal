#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

// Determinism — every game-visible random draw comes from a **named,
// owned stream** whose state is in the save's `Session` block. `FMath::Rand()` is banned in gameplay
// code for the same reason `FTimerManager` is: a save is only worth as much as the run that follows
// it, and a draw the save cannot carry makes the run after a load a different run.
//
// The streams are module-static rather than members of a subsystem because their call sites are
// plain C++ leaves (a `logic_case`'s pick, `OneOfSet`'s per-frame roll) that hold no session
// pointer. Their *lifetime* is still the session's: `SeedAll` runs at New Game and at EndSession,
// and Snapshot/Restore are the save block.
// **The ORDER is a save format.** `Snapshot`/`Restore` walk the streams in enumerator order and the
// save block is that array, so an enumerator inserted or removed in the middle re-points every
// stream after it at another one's recorded position — a load that reports success and hands the run
// a different sequence than the one that was saved. Add at the END, before `Count`, and never
// reorder or delete.
enum class EElysiumRngStream : uint8
{
	OneOfSet,      // the 589 dialogue gates' 1-of-N selector
	LogicTimer,    // logic_timer's UseRandomTime refire interval
	LogicCase,     // logic_case PickRandom / PickRandomShuffle
	Dice,          // the World-of-Darkness d10 resolver
	Ambient,       // ambient idle picks and the RandomSound scheduler
	Chargen,       // which phrasing of a wizard question the quiz asks
	NpcMaker,      // npc_maker's transient-admission retry interval
	NpcSchedule,   // the NPC idle branch: schedule selection and the disposition stance rolls
	Reaction,      // the damage flinch's head/torso coin, its +-30 degree jitter and its
	               // weighted-sequence draw
	Effects,       // R7.3: the dust motes' in-solid sample points, an env_beam's endpoint pick
	               // among duplicate targetnames, its random end point and its random restrike
	Footsteps,     // the NPC step's stepleft/stepright coin flip (`vampire.dll 1026d460`'s
	               // RandomInt(0,1)), the player step's 95..105 pitch jitter (`1011e430`), the
	               // wade branch's four-phase silent counter and the Sabbat leader's 0..6 draw
	Terminal,      // the computer terminal's screensaver cell/style draws (`CPropHackingSS_Think`
	               // 0x1021a740) and the cracking stepper's filler characters (`FUN_10217200`)
	CameraFindBestShot, // `CBaseCineCam::FindBestShot` `FUN_1006e4c0`'s uniform pick over the
	               // candidate shots that passed both predicates (`RandomInt(0, count-1)`), the one
	               // draw in the scripted-camera subsystem
	NpcThinkCadence, // the jitter the three distance-driven think laws add at their ceiling:
	               // `RandomFloat(0, 0.8)` in `CalcNextUpdateThink` (`0x10290720`), `(0, 0.3)` in
	               // `CalcNextNormalThink` (`0x10290b60`) and `(0, 0.4)` in `CalcNextAIThink`
	               // (`0x10291230`). Its own stream because it draws once per NPC per think and
	               // would otherwise walk every other stream's position off the map
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
