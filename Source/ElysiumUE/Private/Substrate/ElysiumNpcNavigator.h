#pragma once

#include "CoreMinimal.h"

// `CAI_Navigator`'s port-side record, moved verbatim from `ElysiumNpcBaseMotor.inl`
// for 0018 story 5.
//
// Retail's `CAI_Navigator` as much of it as this family's four rows reach. It is declared as a
// nested type rather than a free `FElysiumNpcNavigator` in a file of its own because four one-line
// retail bodies do not justify a new substrate class, and because the five navigator words are
// already CHAIN rows onto the motor — a second owner for them would be a second answer to the same
// question. **Named decision**, stated here and in the story report.
// Superseded 2026-09-29: 0018 story 5 grows it into the navigator object, so it now has its own file.
struct FElysiumNpcNavigator
{
	// `CAI_Navigator+0x18` — the native navigation type. `FUN_1027d990` reads it (29 direct callers,
	// the widest read in this family) and `FUN_1027d9b0` writes it through `0x102eeba0`. The port's
	// `EElysiumNpcNavType` is the same four-value vocabulary (Ground 0, Jump 1, Fly 2, Climb 3), so
	// the write is pushed on to `IElysiumNpcMotor::SetNavigationType` as well as stored.
	int32 NavType = 0;

	// `CAI_Navigator+0x1c` — set to 1 by `OnNavFailed` (`0x102eeae0`, `CAI_Navigator#10`) and by
	// `OnNavComplete` (`0x102eea90`, `CAI_Navigator#8`): the "this route has ended" latch; no
	// consumer in this substrate reads it yet.
	bool bNavFailed = false;

	// `CAI_Navigator::vfunc3` (`0x102ecb50`) copies three of the owner NPC's own pointers —
	// `m_pMotor` (+0x5d44), `m_pMoveProbe` (+0x5d40), `m_pLocalNavigator` (+0x5d38) — into
	// navigator+0x20/+0x24/+0x28 and stores its argument at +0x2c. The three pointers do not exist
	// here, so what survives the port is the FACT that the snapshot was taken and the argument it
	// was taken with. Read by the test and by nothing else.
	bool bSnapshotTaken = false;
	int32 SnapshotArgument = 0;

	// `CAI_Path +0x5c` on the navigator's path (`+0x30`) -- the path's type word, which
	// `0x1030ba50(path, 4)` sets to 4 when `0x102ed430` (`SetRandomGoal`'s body) installs a route
	// with no goal. The port keeps it for the one install that writes it (`InstallPathNoGoal`); 0
	// until then. No consumer in this substrate reads it yet.
	int32 PathTypeWord = 0;

	// `CAI_Navigator +0x14` -- the squared distance, SOURCE units², from the point the route was
	// searched from to the installed path's endpoint (`0x102ed430`'s tail over `0x1000f89e(path)`).
	// Written by `InstallPathNoGoal`; nothing in this substrate reads it yet.
	float EndpointDistanceSqrUnits = 0.0f;

	// Port-only: how many goal-less installs this navigator took (`InstallPathNoGoal`). The tests'
	// witness that the wander pick installed a PATH and never went through `SetGoal`.
	int32 PathNoGoalInstalls = 0;
};
