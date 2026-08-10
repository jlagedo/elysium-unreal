#pragma once

#include "CoreMinimal.h"

struct FElysiumMoveTuning;

// The movement gym's specification (`docs/architecture/movement-architecture.md` → "The gym").
//
// A gym's dimensions *are* its specification, so this one is **generated from `ElysiumMove`'s own
// constants** rather than authored: each riser, roof, ramp and aperture is placed at
// `<constant> + <bracket offset>`, so the geometry cannot drift from the value it tests. What lives
// here is that derivation as plain C++ with no UObject and no engine gameplay type, so the whole
// layout is asserted with no pawn, no world and no RHI (`Elysium.Substrate.GymSpec`) — the same
// pure-rules/engine-half split `ElysiumMoveSolve.h` and `ElysiumCameraSolve.h` use.
// `ElysiumGymBuilder.h` is the engine half: it turns these values into collision and nothing else.
//
// **The bracket is derived; the baseline is measured.** An expectation recomputed from the same
// constants as the geometry moves with the geometry and can never turn red, so nothing here states
// what a course *should* record. Moving a constant moves its own lane, the run measures a different
// answer there, and the committed recording of that answer is what disagrees.

namespace ElysiumGym
{
	// Every dimension below is in **Source units**; `Build` multiplies by `ElysiumMove::U` once, at
	// emission, exactly as `ElysiumMove`'s own constants do at declaration.

	inline constexpr float LaneHalfWidth = 96.0f;   // floor half-width — room to arrive off-centre
	inline constexpr float LanePitch     = 256.0f;  // between lane centres; py/LanePitch is the index
	inline constexpr float SlabThick     = 32.0f;   // floor and roof plate
	inline constexpr float RunUp         = 192.0f;  // floor ahead of the feature face at X = 0
	inline constexpr float LaneLength    = 512.0f;  // floor runs to here, then the back wall
	inline constexpr float WallThick     = 32.0f;
	inline constexpr float WallHeight    = 224.0f;  // taller than a jump can reach from the floor
	inline constexpr float PitDepth      = 160.0f;  // how far below the floor a missed gap drops

	inline constexpr float TreadDepth    = 96.0f;   // a riser's top surface
	inline constexpr float RampFace      = 160.0f;  // along the slope, not along X
	inline constexpr float SpanLength    = 160.0f;  // a roofed passage's depth
	inline constexpr float StartInset    = 32.0f;   // how far past the floor's leading edge a body sits

	// How long a gym course holds its intent. Generous on purpose: every run channel the gym
	// compares **saturates** — a body either climbs a riser or is stopped by it, and either answer
	// is the same at any gait — but only if the course is long enough to reach the feature *and*
	// the wall past it at the slowest speed the mover can produce. A lane is about 700 units end to
	// end, which is 7 s even at the walk.
	inline constexpr float ApproachSeconds = 10.0f;

	// The ducked families need their own, and the number is not a matter of taste. A lane's 656
	// travelable units divided by this hold is the slowest gait that still saturates: at 10 s that
	// is 65.6 u/s, and the authored forward sneak cell is **65.3** — just under it, which would put
	// three permanently-committed `advance_max` values a hair off saturation and make them move with
	// the gait. At 16 s the floor is 41 u/s, which no authored crouch approaches.
	inline constexpr float DuckApproachSeconds = 16.0f;

	// What a lane exercises. The geometry is here; the command stream that drives it is the course
	// table's (`ElysiumMoveCourses.h`), and this enum is the only join between them — so adding a
	// bracket is a spec edit and never a second hand-written course.
	enum class EFamily : uint8
	{
		Riser,          // hold forward into a step-up; `StepSize` decides whether it is climbed
		Pop,            // jump under a capping roof; the roof decides how far the origin pop gets
		DuckPop,        // jump, then duck airborne; measures the crouch-jump's own lift
		Slope,          // hold forward up a ramp; `StandableZ` decides whether it is ridden
		Passage,        // hold forward under a roof, standing
		DuckPassage,    // duck, then hold forward under a lower roof
		UnduckGround,   // stop under a low roof and release the duck — it must be refused
		UnduckAir,      // jump under a low roof and release the duck airborne — likewise
		Doorway,        // hold forward at an aperture the hull barely fits
		Gap,            // run and jump a hole in the floor
		Flat,           // run the length of the lane, then release and coast to a stop

		// The two leniency families (CCC3). Identical geometry — walk off a lip into a drop — and
		// they differ only in which body event the course times its one-frame jump against. VtMB has
		// neither coyote time nor an input buffer, so both brackets are expected to record a refusal
		// on every rung but the sentinel; what they exist for is that adding either later moves a
		// committed number rather than an opinion.
		Ledge,          // walk off the lip, then jump K frames AFTER the ground is lost
		Landing,        // walk off the same lip, then tap jump K frames BEFORE the ground returns
	};

	// One solid. A ramp is a box with a pitch, and nothing in the table needs a second shape.
	struct FPlacement
	{
		FName Lane;   // which lane it belongs to
		FName Tag;    // which part of it — unique within the lane, and the spawned component's
		              // name, so a trace hit reads back as the rung it struck
		FVector  Center = FVector::ZeroVector;   // gym-relative, cm
		FVector  Extent = FVector::ZeroVector;   // half-extents, cm
		FRotator Rot = FRotator::ZeroRotator;    // zero except on a ramp
	};

	// One lane: what it tests, where a body starts, and which way it faces.
	//
	// **A lane is named for its bracket offset, never for the value it stands at.** `riser_p1` is
	// "one unit above `StepSize`" whatever `StepSize` becomes, so moving the constant changes what
	// the lane *records* rather than what it is called — a value diff against the committed
	// recording, which is the signal, instead of a lane appearing and another vanishing.
	struct FLane
	{
		FName Name;
		EFamily Family = EFamily::Riser;
		// **Feet-anchored**, gym-relative, cm. The pawn's own origin is its box centre; the one
		// conversion between the two conventions is `SeatOrigin` and there is no other.
		FVector FeetOrigin = FVector::ZeroVector;
		float Yaw = 0.0f;
		// The bracket value this lane stands at, for the log line and the manifest. The unit is the
		// family's: Source units for a riser or a doorway, degrees for a slope, and **frames** for a
		// leniency lane, whose bracket is an offset from a body event rather than a distance. Not an
		// expectation — it names which rung this is, not what the body will do on it.
		float BracketUnits = 0.0f;
		// True when what this lane measures moves with the speed authority, so its recording must
		// not be promoted before `CCC7` settles it (`docs/project/three-cs-roadmap.md`).
		bool bSpeedDependent = false;
	};

	struct FSpec
	{
		TArray<FPlacement> Placements;
		TArray<FLane> Lanes;

		const FLane* FindLane(const FName& Name) const;
		// Every solid's world-space bounds, for the green room's framing.
		FBox Bounds() const;
	};

	// The whole gym, derived from one tuning surface. Call at run time and never from a static
	// initializer: `FElysiumMoveTuning` is re-read from the console store every frame, so a spec
	// built during static init would freeze the geometry at the compiled-in defaults.
	FSpec Build(const FElysiumMoveTuning& T);

	// The single place the pawn's centre-anchored origin meets the spec's feet-anchored one.
	// `DistEpsilon` is the same gap that keeps a settling down-trace from arriving flush with the
	// floor and reporting no contact — without it a body seated exactly on a surface can spawn
	// interpenetrating it, which is the difference between standing *on* the gym and standing in it.
	FVector SeatOrigin(const FVector& FeetWorld, float HullHalfHeightCm);
}
