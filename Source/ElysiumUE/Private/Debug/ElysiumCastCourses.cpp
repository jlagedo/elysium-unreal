#include "Debug/ElysiumCastCourses.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaSpec.h"
#include "ElysiumMoveSolve.h"

namespace ElysiumCastCourses
{

namespace
{
	// The lane every course travels, stated against the room rather than in coordinates.
	//
	// It runs **off the room's axis**, half way between the centre and the south wall, because the
	// cover block sits at the middle: a route down the axis would be a route around an obstacle, and
	// what these courses measure is a body travelling, not Detour solving a detour. The length is
	// what a walking body covers in a few seconds — long enough that the gait settles, short enough
	// that a course is not mostly a straight line.
	constexpr float LaneOffsetFraction = 0.5f;    // of the room's half extent, toward the south wall
	constexpr float LaneReachFraction  = 0.35f;   // of the half extent, each side of the middle

	FVector CastLaneEnd(float Sign)
	{
		const float X = -ElysiumArena::RoomHalfExtent * LaneOffsetFraction;
		const float Y = ElysiumArena::RoomHalfExtent * LaneReachFraction * Sign;
		// Source units in the spec, centimetres in the world — the same one conversion at emission
		// that the arena and the gym make. Z is the plate's own top, which the arena puts at zero so
		// every feet-anchored point in the room is at zero (`ElysiumArena::FSpec::FloorZ`).
		return FVector(X * ElysiumMove::U, Y * ElysiumMove::U, 0.0f);
	}

	// Facing up the lane, which is +Y. A body that starts facing where it is about to walk is what
	// makes the opening frames a stride rather than a turn.
	constexpr float LaneYaw = 90.0f;
}

TArray<FCourse> All()
{
	const FVector South = CastLaneEnd(-1.0f);
	const FVector North = CastLaneEnd(+1.0f);

	TArray<FCourse> Courses;

	// Nothing asked of it. The cheap catch for a cast body whose idle resolves nothing at all, and
	// the only course whose every frame should read one activity and one generation.
	{
		FCourse& C = Courses.AddDefaulted_GetRef();
		C.Name = TEXT("stand");
		C.Order = EOrder::Stand;
		C.StartFeet = South;
		C.StartYaw = LaneYaw;
		C.RecordSeconds = 3.0f;
	}

	// The ambient route. Long enough to reach the far point at a walk and turn back onto the near
	// one, so the recording carries a leg, an arrival and the leg after it.
	{
		FCourse& C = Courses.AddDefaulted_GetRef();
		C.Name = TEXT("patrol_walk");
		C.Order = EOrder::Patrol;
		C.StartFeet = South;
		C.StartYaw = LaneYaw;
		C.Route = { North, South };
		C.RecordSeconds = 9.0f;
	}

	// The scripted beat, at the run gait — the other producer of cast travel, and the one that hands
	// the motor a speed rather than a fan.
	{
		FCourse& C = Courses.AddDefaulted_GetRef();
		C.Name = TEXT("script_run");
		C.Order = EOrder::Scripted;
		C.StartFeet = South;
		C.StartYaw = LaneYaw;
		C.Route = { North };
		C.MoveTo = 2;
		C.RecordSeconds = 7.0f;
	}

	return Courses;
}

} // namespace ElysiumCastCourses

#endif // !UE_BUILD_SHIPPING
