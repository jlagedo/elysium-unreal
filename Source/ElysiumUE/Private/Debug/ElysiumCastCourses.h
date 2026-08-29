#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"

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
// **Two hosts, the same courses.** An `Arena` course is the regression instrument: the room is
// derived from the arena's own half extent rather than written down, for the same reason a gym lane
// is derived from the movement constants — a room that changes size must not leave a course walking
// into a wall. A `Sited` course is the acceptance: it stands nothing, and every coordinate, name,
// classname, body and loadout in it is the priority map's own authored content.
namespace ElysiumCastCourses
{
	enum class EOrder : uint8
	{
		// No travel order at all — what an idle cast body resolves while it simply stands.
		Stand,
		// `info_node_patrol_point` records plus `FollowPatrolPath`, which is the ambient route every
		// map's own guards walk.
		Patrol,
		// A `scripted_sequence` marker plus `BeginSequence`: the beat that sends an NPC to a mark.
		Scripted,
	};

	struct FCourse
	{
		FName Name;
		EOrder Order = EOrder::Stand;

		// **Feet-anchored**, arena-relative cm — an entity's origin is its feet in this runtime, so
		// the route points and the body's start are the same kind of coordinate. Arena courses only:
		// a sited course starts on the first point of its own authored route.
		FVector StartFeet = FVector::ZeroVector;
		float StartYaw = 0.0f;
		// Where the order sends the body. A patrol course cycles these; a scripted course marks the
		// first and ignores the rest.
		TArray<FVector> Route;
		// A sited course names the map's OWN patrol nodes instead — the same tokens the level script
		// spells, resolved by targetname or by the node's `Group` key. Non-empty is what makes a
		// course sited: it dresses no room and stands no geometry.
		TArray<FString> RouteNames;
		// The `SetupPatrolType` parameter the level script fires before the route, verbatim. It is
		// stored by the leaf and selects nothing today; it is fired because the script fires it, and
		// a course that skipped it would be walking a route no map authors.
		FString PatrolType;

		// `Scripted` only: the beat's `m_fMoveTo` — 1 Walk, 2 Run.
		int32 MoveTo = 2;

		// How long the recorder runs, seconds. A fixed window rather than "until it arrives": the
		// frame count is then the same at any gait, so the day the speed authority moves this records
		// a body that got less far rather than a course of a different length.
		float RecordSeconds = 6.0f;

		// Empty asks the harness for its own default. A sited course names the map's own authored
		// values, so the body that walks the route is the body the map stands there.
		FString Classname;
		FString Model;
		FString StatTemplate;
		// `additionalequipment`. Empty is the empty-handed body, whose translation is the empty table
		// retail's own unarmed cast walks — which is the other half of the armed/unarmed pair.
		FString Weapon;

		// The gait the course is a course OF. `Unknown` declares none, and the no-slide predicate
		// then asks nothing of the activity codes. Anything else must appear in them.
		EElysiumAnimActivityCode ExpectGait = EElysiumAnimActivityCode::Unknown;
		// The body is asked for nothing and must therefore never travel. It is the cheap catch for a
		// course whose room pushed a body around while it was supposed to be standing.
		bool bExpectStill = false;
	};

	// The arena's courses, in order.
	TArray<FCourse> Arena();

	// A priority map's own courses, in order. Empty for a map that authors none, which the harness
	// reports rather than treating as a run with nothing to do.
	TArray<FCourse> Sited(const FString& MapName);
}

#endif // !UE_BUILD_SHIPPING
