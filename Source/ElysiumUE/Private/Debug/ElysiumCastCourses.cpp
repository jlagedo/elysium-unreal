#include "Debug/ElysiumCastCourses.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaSpec.h"
#include "ElysiumMoveSolve.h"

namespace ElysiumCastCourses
{

namespace
{
	// The lane every arena course travels, stated against the room rather than in coordinates.
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

	// Santa Monica's own south beat cop: the classname, the body, the stat block and the route the
	// level script's `copPatrol()` spells, copied from `sm_hub_1`'s authored entities and from
	// `scripts/santamonica/santamonica.py` rather than invented. The armed course carries the
	// authored `additionalequipment`; the unarmed one carries none, which is the whole armed/unarmed
	// pair the rung asks for on one body over one route.
	const TCHAR* const HubCopClass = TEXT("npc_VCop");
	const TCHAR* const HubCopModel = TEXT("regular_cop");
	const TCHAR* const HubCopTemplate = TEXT("OfficerGeneric");
	// Both of the cop's authored loadout keys: `additionalequipment` is the baton it carries and
	// `alternateequipment` is the glock it draws. The pair is what makes the armed half of this
	// course a test of the ladder rather than of one weapon — a baton reaches no weapon ladder row
	// and a glock reaches the pistol set, so the two answer differently on the same body.
	const TCHAR* const HubCopBaton = TEXT("item_w_baton");
	const TCHAR* const HubCopGlock = TEXT("item_w_glock_17c");
	const TCHAR* const HubCopPatrolType = TEXT("255 0 FOLLOW_PATROL_PATH_WALK");

	TArray<FString> HubSouthRoute()
	{
		return { TEXT("s1"), TEXT("s2"), TEXT("s3"), TEXT("s4"), TEXT("s7"), TEXT("s8"),
			TEXT("s9"), TEXT("s10"), TEXT("s11"), TEXT("s12"), TEXT("s13"), TEXT("s14") };
	}
}

TArray<FCourse> Arena()
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
		C.bExpectStill = true;
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
		C.ExpectGait = EElysiumAnimActivityCode::Walk;
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
		C.ExpectGait = EElysiumAnimActivityCode::Run;
	}

	return Courses;
}

TArray<FCourse> Sited(const FString& MapName)
{
	TArray<FCourse> Courses;
	if (!MapName.Equals(TEXT("sm_hub_1"), ESearchCase::IgnoreCase))
	{
		return Courses;
	}

	// The same cop, the same route, three times: hands empty, then each of the two loadouts the map
	// authors on it. The weapon classname is what the committed ladders are joined to, so a body that
	// walks the same route with and without one is the ladder being exercised rather than merely
	// present — and the two weapons differ in kind, which is what keeps "armed" from meaning one row.
	struct FHands { const TCHAR* Suffix; const TCHAR* Weapon; };
	const FHands Loadouts[] =
	{
		{ TEXT(""), TEXT("") },
		{ TEXT("_baton"), HubCopBaton },
		{ TEXT("_glock"), HubCopGlock },
	};
	for (const FHands& Hands : Loadouts)
	{
		FCourse& C = Courses.AddDefaulted_GetRef();
		C.Name = *FString::Printf(TEXT("hub_patrol%s"), Hands.Suffix);
		C.Order = EOrder::Patrol;
		C.RouteNames = HubSouthRoute();
		C.PatrolType = HubCopPatrolType;
		C.Classname = HubCopClass;
		C.Model = HubCopModel;
		C.StatTemplate = HubCopTemplate;
		C.Weapon = Hands.Weapon;
		// Long enough for the opening leg, the arrival at s2 and the leg after it, which is where a
		// stride that does not track direction shows.
		C.RecordSeconds = 14.0f;
		C.ExpectGait = EElysiumAnimActivityCode::Walk;
	}
	return Courses;
}

} // namespace ElysiumCastCourses

#endif // !UE_BUILD_SHIPPING
