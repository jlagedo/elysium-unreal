#include "Debug/ElysiumMoveCourses.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumMoveSolve.h"

namespace ElysiumMoveCourses
{

namespace
{
	using EB = EElysiumButton;

	constexpr uint64 Duck = static_cast<uint64>(EB::Duck);
	constexpr uint64 Jump = static_cast<uint64>(EB::Jump);
	constexpr uint64 Slow = static_cast<uint64>(EB::Speed);   // `+speed` selects the SLOW gait

	const FVector2D Fwd(1.0f, 0.0f);
	const FVector2D Still = FVector2D::ZeroVector;

	// --- The surveyed sites, feet-anchored world cm ---------------------------------------------
	//
	// Surveyed by probing the walkable surface with the **real pawn** in a running session — fly to
	// a point with collision off, turn it back on, and read where the body settles — then read back
	// with `elysium.playerpos`. Nothing else answers the question: the map's own `.hulls` sidecar
	// is not the walkable floor (it reports no surface at all under the warehouse's, which the body
	// stands on perfectly well), so a coordinate picked out of it is a coordinate picked off the
	// wrong geometry.
	//
	// The warehouse's ground floor is the map's one long clear run: level to a couple of units for
	// 630 units along +X, which is what the gait, jump, strafe and duck courses need and what
	// nothing else in `sp_tutorial_1` offers. All five open-floor courses start here, and all five
	// are confirmed against a headless run — the body reaches the full run speed, covers 617 units,
	// and lands the jump and the air-strafe.
	const FVector WarehouseFloor(-3750.0f, -1780.0f, 0.2f);

	// --- The three feature sites, and why they are still not baselined --------------------------
	//
	// **These are surveyed and they still do not work headless.** A played session puts the body on
	// solid floor at each of them; the `-ElysiumMove` run finds nothing there and the body falls
	// through, or is seated inside a solid and cannot move at all. Whatever supplies that floor in
	// a played session is not present in the harness's own load, and finding out what is a piece of
	// map-collision work rather than a survey.
	//
	// They stay in the table because the coordinates are real and the next attempt should start
	// from them, and they are marked deferred so a run records them without a baseline claiming
	// they measure anything.

	// A real curb in the alley: the floor climbs about 15 units at x ≈ -790, and the start is past
	// the two drains at x ≈ -985 and -955, which are eight units deep and swallow the body.
	const FVector AlleyStep(-930.0f, -1490.0f, -14.4f);

	// The warehouse's double door — `tutwaredoora`/`tutwaredoorb` — with level floor either side.
	// The doors are **shut** at map load, so the course holds `+use`: without it the course measures
	// a body walking into a closed door, which is a fine thing for the mover to do and not what a
	// doorway course is for.
	const FVector WarehouseDoor(-3650.0f, -1900.0f, 0.7f);

	// The stepped descent off the warehouse's raised platform toward the sewer door.
	// **`sp_tutorial_1` carries no ramp anywhere near the 0.7 standable normal** — every sloped
	// surface it ships is either a few degrees or effectively a wall — so the standable threshold
	// is bracketed by the gym alone, and what this course can ever record is a graded drop.
	const FVector SewerDescent(-2500.0f, -1628.0f, 158.6f);

	FCourse MakeSitedCourse(const TCHAR* Name, const FVector& Feet, float Yaw)
	{
		FCourse C;
		C.Name = FName(Name);
		C.Host = EHost::SitedMap;
		C.Map = TEXT("sp_tutorial_1");
		C.StartFeet = Feet;
		C.StartYaw = Yaw;
		return C;
	}

	FCourse MakeFlat()
	{
		FCourse C = MakeSitedCourse(TEXT("flat"), WarehouseFloor, 0.0f);
		// Run, then release and let friction stop the body — the stop distance is what this pins,
		// including the `sv_stopspeed` knee that makes the halt crisp rather than asymptotic.
		C.Segments.Add({ 3.0f, Fwd, 0, 0.0f });
		C.Segments.Add({ 2.0f, Still, 0, NAN });
		C.bDeferBaseline = true;
		return C;
	}

	FCourse MakeWalkRun()
	{
		FCourse C = MakeSitedCourse(TEXT("walkrun"), WarehouseFloor, 0.0f);
		// `+speed` selects the SLOW gait: the run is the default and holding the key walks. A gait
		// regression shows up here as the two halves swapping speeds.
		C.Segments.Add({ 2.0f, Fwd, 0, 0.0f });
		C.Segments.Add({ 2.0f, Fwd, Slow, NAN });
		C.bDeferBaseline = true;
		return C;
	}

	FCourse MakeJump()
	{
		FCourse C = MakeSitedCourse(TEXT("jump"), WarehouseFloor, 0.0f);
		// A standing jump, then a running one. The apex is the gravity-split assertion in a built
		// world, and the held-jump segment is what proves the OldButtons latch does not pogo.
		C.Segments.Add({ 0.5f, Still, 0, 0.0f });
		C.Segments.Add({ 1.5f, Still, Jump, NAN });
		C.Segments.Add({ 0.5f, Still, 0, NAN });
		C.Segments.Add({ 2.0f, Fwd, Jump, NAN });
		return C;
	}

	FCourse MakeStrafe()
	{
		FCourse C = MakeSitedCourse(TEXT("strafe"), WarehouseFloor, 0.0f);
		// The air-strafe pattern: jump, then hold forward+right. This is the headline frame-rate
		// measurement — `AirAccelerate`'s cap stops binding above ~117 fps, so the same intent at
		// 60 / 120 / 240 diverges under the faithful variable step and agrees under a fixed one.
		C.Segments.Add({ 0.3f, Fwd, 0, 0.0f });
		C.Segments.Add({ 2.5f, FVector2D(1.0f, 1.0f), Jump, NAN });
		C.bDeferBaseline = true;
		return C;
	}

	FCourse MakeDuck()
	{
		FCourse C = MakeSitedCourse(TEXT("duck"), WarehouseFloor, 0.0f);
		// Duck in place, walk ducked, release. The hull swap, the eye drop to 30u, the 0.4 s duck
		// and 0.2 s unduck ramps, and the headroom gate all read off this one.
		C.Segments.Add({ 0.5f, Still, 0, 0.0f });
		C.Segments.Add({ 1.0f, Still, Duck, NAN });
		C.Segments.Add({ 1.5f, Fwd, Duck, NAN });
		C.Segments.Add({ 1.0f, Still, 0, NAN });
		return C;
	}

	// One long forward hold at a surveyed feature. The hold is generous for the same reason the
	// gym's is: what these record has to be reached at any gait.
	FCourse MakeSited(const TCHAR* Name, const FVector& Feet, float Yaw, uint64 Buttons = 0)
	{
		FCourse C = MakeSitedCourse(Name, Feet, Yaw);
		C.Segments.Add({ 8.0f, Fwd, Buttons, Yaw });
		// Deferred until the harness can find the floor these sites stand on (see above), not
		// because the speed authority can move them.
		C.bDeferBaseline = true;
		return C;
	}

	// --- The gym's segment recipes, one per lane family ----------------------------------------
	//
	// A recipe is written so the run channel it feeds **saturates**: a body either clears the
	// feature and runs on to the lane's back wall, or is stopped by it, and both answers are the
	// same at any gait. That is the whole reason these recordings can be promoted before `CCC7`.

	void Recipe(FCourse& C, ElysiumGym::EFamily Family)
	{
		const float Hold = ElysiumGym::ApproachSeconds;
		switch (Family)
		{
		case ElysiumGym::EFamily::Riser:
		case ElysiumGym::EFamily::Slope:
		case ElysiumGym::EFamily::Passage:
		case ElysiumGym::EFamily::Doorway:
			C.Segments.Add({ Hold, Fwd, 0, 0.0f });
			break;

		case ElysiumGym::EFamily::DuckPassage:
			// Duck standing still first: on the ground the transition takes 0.4 s, and a body that
			// walks into the span mid-duck is measuring the ramp rather than the hull.
			C.Segments.Add({ 1.0f, Still, Duck, 0.0f });
			C.Segments.Add({ Hold, Fwd, Duck, NAN });
			break;

		case ElysiumGym::EFamily::Pop:
			// Nothing to walk to — the roof spans the lane, so the pop is the only thing measured.
			C.Segments.Add({ 0.5f, Still, 0, 0.0f });
			C.Segments.Add({ 2.0f, Still, Jump, NAN });
			C.Segments.Add({ 1.0f, Still, 0, NAN });
			C.JumpOverrides.Add({ TEXT("BaseJumpVelocity"), 0.0f });
			break;

		case ElysiumGym::EFamily::DuckPop:
			// The duck has to land the frame *after* the jump. Ducking first would be a ground
			// duck, which plants the feet and lifts nothing; the airborne transition is the one
			// that moves the origin, and it is what the crouch-jump is.
			C.Segments.Add({ 0.5f, Still, 0, 0.0f });
			C.Segments.Add({ 0.0f, Still, Jump, NAN });            // exactly one frame
			C.Segments.Add({ 2.0f, Still, Jump | Duck, NAN });
			C.Segments.Add({ 1.0f, Still, 0, NAN });
			C.JumpOverrides.Add({ TEXT("BaseJumpVelocity"), 0.0f });
			break;

		case ElysiumGym::EFamily::UnduckGround:
			// Walk in ducked, come to rest against the back wall, then release. The roof runs the
			// length of the lane so where the body ends up is the wall's business and not a
			// course's timing.
			C.Segments.Add({ 1.0f, Still, Duck, 0.0f });
			C.Segments.Add({ Hold, Fwd, Duck, NAN });
			C.Segments.Add({ 2.0f, Still, 0, NAN });
			break;

		case ElysiumGym::EFamily::UnduckAir:
			// The same refusal off the ground, where `CanUnduck` applies the airborne -18 before it
			// traces — which is what stops a stand-up through the floor.
			C.Segments.Add({ 1.0f, Still, Duck, 0.0f });
			C.Segments.Add({ Hold, Fwd, Duck, NAN });
			C.Segments.Add({ 0.0f, Still, Duck | Jump, NAN });      // exactly one frame
			C.Segments.Add({ 2.0f, Still, 0, NAN });
			break;

		case ElysiumGym::EFamily::Gap:
			C.Segments.Add({ 2.0f, Fwd, 0, 0.0f });
			C.Segments.Add({ 2.0f, Fwd, Jump, NAN });
			break;

		case ElysiumGym::EFamily::Flat:
			C.Segments.Add({ 3.0f, Fwd, 0, 0.0f });
			C.Segments.Add({ 3.0f, Still, 0, NAN });
			break;
		}
	}
}

TArrayView<const FCourse> Sited()
{
	static const TArray<FCourse> Courses =
	{
		MakeFlat(),
		MakeWalkRun(),
		MakeJump(),
		MakeStrafe(),
		MakeDuck(),
		MakeSited(TEXT("stairs"), AlleyStep, 0.0f),
		MakeSited(TEXT("slope"), SewerDescent, 0.0f),
		MakeSited(TEXT("doorway"), WarehouseDoor, 90.0f, static_cast<uint64>(EB::Use)),
	};
	return MakeArrayView(Courses);
}

TArray<FCourse> Gym(const ElysiumGym::FSpec& Spec)
{
	TArray<FCourse> Courses;
	Courses.Reserve(Spec.Lanes.Num());
	for (const ElysiumGym::FLane& Lane : Spec.Lanes)
	{
		FCourse C;
		C.Name = Lane.Name;
		C.Host = EHost::GymStage;
		C.GymLane = Lane.Name;
		C.StartYaw = Lane.Yaw;
		C.bDeferBaseline = Lane.bSpeedDependent;
		Recipe(C, Lane.Family);
		Courses.Add(MoveTemp(C));
	}
	return Courses;
}

FElysiumUserCmdStream Expand(const FCourse& Course, float StepSeconds)
{
	FElysiumUserCmdStream Stream;
	if (StepSeconds <= 0.0f)
	{
		return Stream;
	}

	int32 Seq = 0;
	for (const FSegment& Seg : Course.Segments)
	{
		const int32 Frames = FMath::Max(1, FMath::RoundToInt32(Seg.Seconds / StepSeconds));
		for (int32 i = 0; i < Frames; ++i)
		{
			FElysiumUserCmd Cmd;
			Cmd.Seq = Seq++;
			Cmd.DeltaSeconds = StepSeconds;
			Cmd.Move = Seg.Move;
			Cmd.Buttons = Seg.Buttons;
			Stream.Record(Cmd);
		}
	}
	return Stream;
}

} // namespace ElysiumMoveCourses

#endif // !UE_BUILD_SHIPPING
