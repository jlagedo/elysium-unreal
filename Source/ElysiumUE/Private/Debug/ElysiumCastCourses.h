#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

// The cast harness's fixed courses (`-ElysiumCast`), the other producer's answer to
// `ElysiumMoveCourses.h`.
//
// A player course is a stream of commands, and a cast course cannot be: nothing about the cast takes
// input. What moves a cast body is an **authored travel order**, so a course here is the entities the
// room is dressed with and the input that arms them — a patrol route and `FollowPatrolPath`, a
// marker and `BeginSequence` — delivered through the same doors a map's own script uses. Nothing
// here reaches into the mind, the schedule or the motor; a course that had to would be measuring the
// harness rather than the game.
//
// The route is derived from the arena's own half extent rather than written down, for the same
// reason a gym lane is derived from the movement constants: a room that changes size must not leave
// a course walking into a wall.
namespace ElysiumCastCourses
{
	enum class EOrder : uint8
	{
		// No travel order at all — what an idle cast body resolves while it simply stands.
		Stand,
		// `info_node_patrol_point` records plus `FollowPatrolPath`, which is the ambient route every
		// map's own guards walk. The gait is the patrol executor's own Walk; `SetupPatrolType` is
		// stored by the leaf and selects nothing, so it is not fired here.
		Patrol,
		// A `scripted_sequence` marker plus `BeginSequence`: the beat that sends an NPC to a mark.
		Scripted,
	};

	struct FCourse
	{
		FName Name;
		EOrder Order = EOrder::Stand;

		// **Feet-anchored**, arena-relative cm — an entity's origin is its feet in this runtime, so
		// the route points and the body's start are the same kind of coordinate.
		FVector StartFeet = FVector::ZeroVector;
		float StartYaw = 0.0f;
		// Where the order sends the body. A patrol course cycles these; a scripted course marks the
		// first and ignores the rest.
		TArray<FVector> Route;

		// `Scripted` only: the beat's `m_fMoveTo` — 1 Walk, 2 Run.
		int32 MoveTo = 2;

		// How long the recorder runs, seconds. A fixed window rather than "until it arrives": the
		// frame count is then the same at any gait, so the day the speed authority moves this records
		// a body that got less far rather than a course of a different length.
		float RecordSeconds = 6.0f;
	};

	// Every course, in order.
	TArray<FCourse> All();
}

#endif // !UE_BUILD_SHIPPING
