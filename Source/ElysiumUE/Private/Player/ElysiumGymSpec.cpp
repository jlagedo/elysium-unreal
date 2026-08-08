#include "ElysiumGymSpec.h"

#include "ElysiumMoveSolve.h"

namespace ElysiumGym
{

namespace
{
	constexpr float U = ElysiumMove::U;

	// An axis-aligned solid from its Source-unit AABB. Authoring corners rather than a centre and a
	// half-extent is what keeps a generator readable: a riser is "floor level to `H`", not "centred
	// at `H/2` with a half-height of `H/2`".
	FPlacement Block(const FName& Lane, const TCHAR* Tag,
		float X0, float X1, float Y0, float Y1, float Z0, float Z1)
	{
		FPlacement P;
		P.Lane = Lane;
		P.Tag = FName(Tag);
		P.Center = FVector((X0 + X1) * 0.5f * U, (Y0 + Y1) * 0.5f * U, (Z0 + Z1) * 0.5f * U);
		P.Extent = FVector((X1 - X0) * 0.5f * U, (Y1 - Y0) * 0.5f * U, (Z1 - Z0) * 0.5f * U);
		return P;
	}

	// A ramp, pinned by its **leading top edge** rather than its centre, because that edge is the
	// one thing the body meets and it has to sit flush with the floor it walks off.
	//
	// Unreal's positive pitch takes local +X to `(cos P, 0, sin P)` and local +Z to
	// `(-sin P, 0, cos P)`, so the top face climbs going +X and its normal's Z is `cos P` — which is
	// exactly the quantity `StandableZ` is compared against. That identity is what makes a ramp's
	// pitch a direct statement about the constant rather than a trigonometric coincidence.
	FPlacement Ramp(const FName& Lane, const TCHAR* Tag,
		float StartX, float LaneY, float FaceLength, float HalfY, float PitchDeg, float Thickness)
	{
		FPlacement P;
		P.Lane = Lane;
		P.Tag = FName(Tag);
		P.Rot = FRotator(PitchDeg, 0.0f, 0.0f);
		P.Extent = FVector(FaceLength * 0.5f * U, HalfY * U, Thickness * 0.5f * U);

		const FVector LeadingTop(StartX * U, LaneY * U, 0.0f);
		const FVector CentreToEdge(-FaceLength * 0.5f * U, 0.0f, Thickness * 0.5f * U);
		P.Center = LeadingTop - P.Rot.RotateVector(CentreToEdge);
		return P;
	}

	// Open a lane: reserve its Y band, seat its start, and lay the floor and the back wall every
	// lane has. `bFloor` is false only for the gap lane, which lays its own two halves.
	float BeginLane(FSpec& S, const TCHAR* Name, EFamily Family, float BracketUnits,
		bool bSpeedDependent, bool bFloor = true, float Length = LaneLength)
	{
		const float LaneY = static_cast<float>(S.Lanes.Num()) * LanePitch;
		const FName LaneName(Name);

		FLane L;
		L.Name = LaneName;
		L.Family = Family;
		L.BracketUnits = BracketUnits;
		L.bSpeedDependent = bSpeedDependent;
		L.FeetOrigin = FVector((-RunUp + StartInset) * U, LaneY * U, 0.0f);
		L.Yaw = 0.0f;
		S.Lanes.Add(L);

		if (bFloor)
		{
			S.Placements.Add(Block(LaneName, TEXT("floor"), -RunUp, Length,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -SlabThick, 0.0f));
		}
		// Every lane ends in a wall so a body that gets through its feature stops at a known place
		// instead of running off the slab. That is what makes "how far did it get" saturate, and a
		// saturating answer is the same at any gait — which is the whole reason these recordings
		// survive `CCC7`. It reaches below floor level too: a body that fell into the gap lane's
		// hole would otherwise carry its speed straight underneath the wall and never stop.
		S.Placements.Add(Block(LaneName, TEXT("backwall"), Length, Length + WallThick,
			LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -PitDepth, WallHeight));
		return LaneY;
	}

	// --- The families -------------------------------------------------------------------------

	// A step-up the body either climbs or is stopped by. Each rung is its own lane and is measured
	// from the floor, so a rung's height is absolute rather than relative to the one below it —
	// a flight would only ever prove that the *first* refused rung is too tall.
	void AddRisers(FSpec& S, const FElysiumMoveTuning& T)
	{
		const float StepU = T.StepSize / U;
		static const TCHAR* const Names[] =
			{ TEXT("riser_m2"), TEXT("riser_m1"), TEXT("riser_0"),
			  TEXT("riser_p1"), TEXT("riser_p2"), TEXT("riser_p6") };
		static const float Offsets[] = { -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 6.0f };

		for (int32 i = 0; i < UE_ARRAY_COUNT(Names); ++i)
		{
			const float H = StepU + Offsets[i];
			const float LaneY = BeginLane(S, Names[i], EFamily::Riser, H, /*bSpeedDependent*/ false);
			S.Placements.Add(Block(S.Lanes.Last().Name, TEXT("tread"), 0.0f, TreadDepth,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, 0.0f, H));
		}
	}

	// `sv_jump_boost` is an instant **origin** displacement, and the sweep that applies it is capped
	// by whatever is overhead — the pop is scaled by the hull trace's fraction so it can never seat
	// the body in a ceiling. So a roof at `StandHeight + G` brackets the pop directly: below the
	// pop's own height the roof truncates it, above it the roof is never touched. Nothing has to be
	// landed on, which is what keeps this a purely vertical measurement.
	//
	// The lane is driven with `BaseJumpVelocity` overridden to zero (the course table's doing), so
	// the pop is the *only* thing that lifts the body. With the shipped 185 u/s held push in play
	// the body clears every one of these roofs and the bracket says nothing.
	void AddPops(FSpec& S, const FElysiumMoveTuning& T)
	{
		const float StandU = ElysiumMove::StandHeight / U;
		static const TCHAR* const Names[] = { TEXT("pop_m1"), TEXT("pop_0"), TEXT("pop_p1") };
		static const float Offsets[] = { -1.0f, 0.0f, 1.0f };

		for (int32 i = 0; i < UE_ARRAY_COUNT(Names); ++i)
		{
			const float Clearance = T.JumpBoost + Offsets[i];
			const float LaneY = BeginLane(S, Names[i], EFamily::Pop, Clearance,
				/*bSpeedDependent*/ false);
			S.Placements.Add(Block(S.Lanes.Last().Name, TEXT("roof"), -RunUp, LaneLength,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth,
				StandU + Clearance, StandU + Clearance + SlabThick));
		}
	}

	// The crouch-jump's own contribution: ducking in flight moves the origin up by half the hull
	// difference, so the feet rise `(StandHeight - DuckHeight)/2` above wherever the pop left them.
	//
	// **This lane is a recording, not a bracket, and that is deliberate.** A roof cannot cap the
	// ducked reach without also capping the standing pop that precedes it — the standing body needs
	// the headroom the ducked measurement would have to deny it — so there is no geometry that
	// straddles the answer. What the lane measures is the lift itself, and `DuckHeight` moving is
	// what changes it.
	void AddDuckPop(FSpec& S, const FElysiumMoveTuning& T)
	{
		const float StandU = ElysiumMove::StandHeight / U;
		const float Lift = (ElysiumMove::StandHeight - ElysiumMove::DuckHeight) * 0.5f / U;
		const float LaneY = BeginLane(S, TEXT("duckpop"), EFamily::DuckPop, Lift,
			/*bSpeedDependent*/ false);
		// High enough that neither phase is capped: the pop, then the lift, then slack.
		S.Placements.Add(Block(S.Lanes.Last().Name, TEXT("roof"), -RunUp, LaneLength,
			LaneY - LaneHalfWidth, LaneY + LaneHalfWidth,
			StandU + T.JumpBoost + Lift + 16.0f, StandU + T.JumpBoost + Lift + 16.0f + SlabThick));
	}

	// `StandableZ` is compared against a surface normal's Z, and a ramp pitched at `P` has normal Z
	// `cos P`, so the threshold is a pitch of `acos(StandableZ)` and the bracket straddles it. A
	// standable ramp is ridden to its landing; a steeper one is not climbed at all.
	void AddSlopes(FSpec& S, const FElysiumMoveTuning& T)
	{
		const float LimitDeg = FMath::RadiansToDegrees(FMath::Acos(ElysiumMove::StandableZ));
		static const TCHAR* const Names[] =
			{ TEXT("slope_m15"), TEXT("slope_m05"), TEXT("slope_p05"), TEXT("slope_p45") };
		static const float Offsets[] = { -1.5f, -0.5f, 0.5f, 4.5f };

		for (int32 i = 0; i < UE_ARRAY_COUNT(Names); ++i)
		{
			const float Pitch = LimitDeg + Offsets[i];
			const float LaneY = BeginLane(S, Names[i], EFamily::Slope, Pitch,
				/*bSpeedDependent*/ false);
			const FName Lane = S.Lanes.Last().Name;

			S.Placements.Add(Ramp(Lane, TEXT("ramp"), 0.0f, LaneY, RampFace, LaneHalfWidth,
				Pitch, SlabThick));

			// The landing the ramp arrives at, so a ridden slope ends somewhere flat and the run
			// channel saturates instead of drifting up a bottomless incline.
			const float Rise = RampFace * FMath::Sin(FMath::DegreesToRadians(Pitch));
			const float Run  = RampFace * FMath::Cos(FMath::DegreesToRadians(Pitch));
			S.Placements.Add(Block(Lane, TEXT("landing"), Run, LaneLength,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, Rise - SlabThick, Rise));
		}
	}

	// A roofed span the standing hull either fits under or does not — `StandHeight` straddled — and
	// the same shape one unit at a time around `DuckHeight` for a body that has already ducked.
	void AddPassages(FSpec& S, const FElysiumMoveTuning& T)
	{
		auto AddSpan = [&S](const TCHAR* Name, EFamily Family, float Clearance)
		{
			const float LaneY = BeginLane(S, Name, Family, Clearance, /*bSpeedDependent*/ false);
			S.Placements.Add(Block(S.Lanes.Last().Name, TEXT("roof"), 0.0f, SpanLength,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, Clearance, Clearance + SlabThick));
		};

		const float StandU = ElysiumMove::StandHeight / U;
		AddSpan(TEXT("stand_m1"), EFamily::Passage, StandU - 1.0f);
		AddSpan(TEXT("stand_0"),  EFamily::Passage, StandU);
		AddSpan(TEXT("stand_p1"), EFamily::Passage, StandU + 1.0f);

		const float DuckU = ElysiumMove::DuckHeight / U;
		AddSpan(TEXT("duck_m1"), EFamily::DuckPassage, DuckU - 1.0f);
		AddSpan(TEXT("duck_0"),  EFamily::DuckPassage, DuckU);
		AddSpan(TEXT("duck_p1"), EFamily::DuckPassage, DuckU + 1.0f);
	}

	// `CanUnduck` traces the standing hull before it lets a body up, and applies the airborne -18
	// first when it is off the ground. A chamber a ducked body fits in and a standing one does not
	// exercises both refusals; the roof runs to the back wall so the body ends up under it whatever
	// its gait, rather than at a position a course had to time.
	void AddUnduckChambers(FSpec& S, const FElysiumMoveTuning& T)
	{
		const float DuckU = ElysiumMove::DuckHeight / U;
		const float StandU = ElysiumMove::StandHeight / U;
		// Strictly between the two hulls: a ducked body walks in, a standing one cannot come up.
		const float Clearance = DuckU + (StandU - DuckU) * 0.25f;

		for (const TCHAR* Name : { TEXT("unduck_ground"), TEXT("unduck_air") })
		{
			const EFamily Family = FCString::Strcmp(Name, TEXT("unduck_air")) == 0
				? EFamily::UnduckAir : EFamily::UnduckGround;
			const float LaneY = BeginLane(S, Name, Family, Clearance, /*bSpeedDependent*/ false);
			S.Placements.Add(Block(S.Lanes.Last().Name, TEXT("roof"), 0.0f, LaneLength + WallThick,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, Clearance, Clearance + SlabThick));
		}
	}

	// An aperture the hull barely fits, straddling its own width. `WalkMove` special-cases neither a
	// doorway nor a corner — it runs the same two attempts everywhere — so what this brackets is the
	// hull, and what it exercises is the slide.
	void AddDoorways(FSpec& S, const FElysiumMoveTuning& T)
	{
		const float HullU = 2.0f * ElysiumMove::HullHalfWidth / U;
		static const TCHAR* const Names[] =
			{ TEXT("door_m1"), TEXT("door_0"), TEXT("door_p1"),
			  TEXT("door_p4"), TEXT("door_p16") };
		static const float Offsets[] = { -1.0f, 0.0f, 1.0f, 4.0f, 16.0f };

		for (int32 i = 0; i < UE_ARRAY_COUNT(Names); ++i)
		{
			const float Aperture = HullU + Offsets[i];
			const float LaneY = BeginLane(S, Names[i], EFamily::Doorway, Aperture,
				/*bSpeedDependent*/ false);
			const FName Lane = S.Lanes.Last().Name;
			const float Half = Aperture * 0.5f;

			S.Placements.Add(Block(Lane, TEXT("jamb_left"), 0.0f, WallThick,
				LaneY - LaneHalfWidth, LaneY - Half, 0.0f, WallHeight));
			S.Placements.Add(Block(Lane, TEXT("jamb_right"), 0.0f, WallThick,
				LaneY + Half, LaneY + LaneHalfWidth, 0.0f, WallHeight));
		}
	}

	// A hole in the floor, graded. What clears it is the held-jump arc *and* the speed carried into
	// it, so every one of these is speed-dependent and none may be promoted before `CCC7`.
	void AddGaps(FSpec& S, const FElysiumMoveTuning& T)
	{
		static const TCHAR* const Names[] =
			{ TEXT("gap_32"), TEXT("gap_64"), TEXT("gap_96"), TEXT("gap_128"), TEXT("gap_160") };
		static const float Widths[] = { 32.0f, 64.0f, 96.0f, 128.0f, 160.0f };

		for (int32 i = 0; i < UE_ARRAY_COUNT(Names); ++i)
		{
			const float LaneY = BeginLane(S, Names[i], EFamily::Gap, Widths[i],
				/*bSpeedDependent*/ true, /*bFloor*/ false);
			const FName Lane = S.Lanes.Last().Name;
			S.Placements.Add(Block(Lane, TEXT("near"), -RunUp, 0.0f,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -SlabThick, 0.0f));
			S.Placements.Add(Block(Lane, TEXT("far"), Widths[i], LaneLength,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -SlabThick, 0.0f));
			// A body that misses the far side lands in the hole and stays in it. Without a floor it
			// falls out of the world; with a floor but no far face it simply walks on underneath
			// the lane, and every gap width then reports the same distance — which is a measurement
			// that has stopped measuring.
			S.Placements.Add(Block(Lane, TEXT("pit"), 0.0f, Widths[i],
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -PitDepth, -PitDepth + SlabThick));
			S.Placements.Add(Block(Lane, TEXT("pit_face"), Widths[i], Widths[i] + WallThick,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -PitDepth, -SlabThick));
		}
	}

	// A long clear run: what it measures is how far a released body coasts, which is `Friction`,
	// `StopSpeed` and `Accelerate` against the speed it had — speed-dependent by construction. It
	// is four lanes long so the body comes to rest on its own; a wall reached before the coast ends
	// would record the wall's position instead of the friction's.
	void AddFlat(FSpec& S, const FElysiumMoveTuning& T)
	{
		BeginLane(S, TEXT("flat"), EFamily::Flat, 0.0f, /*bSpeedDependent*/ true,
			/*bFloor*/ true, /*Length*/ LaneLength * 4.0f);
	}

	// --- The leniency lanes (CCC3) ---------------------------------------------------------------
	// One lip and one drop, shared by both families. The body walks off the edge at X = 0 and falls
	// `PitDepth` to a lower floor it then keeps walking along until the back wall stops it.
	//
	// **Walking off, never jumping off.** A held jump would put `JumpHoldRemaining` and the reduced
	// gravity scale into the fall, and releasing it mid-air calls `EndJumpHold`, which restores full
	// gravity and perturbs the trajectory. Falling off a lip leaves both already neutral, so the
	// one-frame tap the course places is the only thing the mover sees.
	//
	// Ground loss is unambiguous: `CategorizePosition` sweeps the whole box hull down, so the body
	// stays grounded until its rear face clears the lip, and that resolves inside one frame at any
	// gait. The drop is deep enough that the fall spans far more frames than the widest bracket.
	void AddLedgeLanes(FSpec& S, EFamily Family, const TCHAR* const* Names, const float* Offsets,
		int32 Count)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			// The bracket is in FRAMES, not units: what is under test is `CheckJumpButton`'s per-step
			// decision, so "one decision late" is the rate-invariant statement and a wall-clock offset
			// would not be. Both families are speed-invariant — exactly one press exists in the whole
			// stream and it is placed against a body event, so the count it produces is 0 or 1 at any
			// gait, which is what lets these be committed before `CCC7`.
			const float LaneY = BeginLane(S, Names[i], Family, Offsets[i],
				/*bSpeedDependent*/ false, /*bFloor*/ false);
			const FName Lane = S.Lanes.Last().Name;

			// The run-up, ending at the lip.
			S.Placements.Add(Block(Lane, TEXT("upper"), -RunUp, 0.0f,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -SlabThick, 0.0f));
			// What it lands on. `BeginLane`'s back wall already reaches from `-PitDepth` up, so the
			// body is stopped down here too and "how far did it get" still saturates.
			S.Placements.Add(Block(Lane, TEXT("lower"), 0.0f, LaneLength,
				LaneY - LaneHalfWidth, LaneY + LaneHalfWidth, -PitDepth - SlabThick, -PitDepth));
		}
	}

	void AddLedges(FSpec& S, const FElysiumMoveTuning& T)
	{
		// Bracketing "there is no coyote time" the way the risers bracket `StepSize`. `ledge_m1` is
		// the last grounded frame and must jump; `ledge_0` is the frame the press is taken on, where
		// `CheckJumpButton` still sees the ground it is leaving. From `ledge_p1` on the body is
		// airborne at the decision point and retail refuses — so p1 is the rung coyote time flips.
		static const TCHAR* const Names[] =
			{ TEXT("ledge_m1"), TEXT("ledge_0"), TEXT("ledge_p1"),
			  TEXT("ledge_p2"), TEXT("ledge_p4") };
		static const float Offsets[] = { -1.0f, 0.0f, 1.0f, 2.0f, 4.0f };
		AddLedgeLanes(S, EFamily::Ledge, Names, Offsets, UE_ARRAY_COUNT(Names));
	}

	void AddLandings(FSpec& S, const FElysiumMoveTuning& T)
	{
		// The same bracket for an input buffer. The mover decides on the ground state as seen at the
		// head of its step, so the frame at which ground is first *recorded* is still airborne at its
		// own decision point — `land_p1`, one frame later, is the sentinel that must jump, and
		// everything at or before the landing is what a buffer would flip.
		static const TCHAR* const Names[] =
			{ TEXT("land_p1"), TEXT("land_0"), TEXT("land_m1"),
			  TEXT("land_m2"), TEXT("land_m4") };
		static const float Offsets[] = { 1.0f, 0.0f, -1.0f, -2.0f, -4.0f };
		AddLedgeLanes(S, EFamily::Landing, Names, Offsets, UE_ARRAY_COUNT(Names));
	}
}

const FLane* FSpec::FindLane(const FName& Name) const
{
	return Lanes.FindByPredicate([&Name](const FLane& L) { return L.Name == Name; });
}

FBox FSpec::Bounds() const
{
	FBox Box(ForceInit);
	for (const FPlacement& P : Placements)
	{
		// A rotated box's own AABB, from its eight corners — `FBox(Center ± Extent)` would be wrong
		// on every ramp.
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local(
				(Corner & 1) ? P.Extent.X : -P.Extent.X,
				(Corner & 2) ? P.Extent.Y : -P.Extent.Y,
				(Corner & 4) ? P.Extent.Z : -P.Extent.Z);
			Box += P.Center + P.Rot.RotateVector(Local);
		}
	}
	return Box;
}

FSpec Build(const FElysiumMoveTuning& T)
{
	FSpec S;
	AddRisers(S, T);
	AddPops(S, T);
	AddDuckPop(S, T);
	AddSlopes(S, T);
	AddPassages(S, T);
	AddUnduckChambers(S, T);
	AddDoorways(S, T);
	AddGaps(S, T);
	AddFlat(S, T);
	// **Appended, never inserted.** `BeginLane` derives a lane's Y from the count so far, so adding a
	// family anywhere but the end slides every later lane sideways. The recordings would still
	// reproduce — every run channel is start-relative — so the damage would be invisible while
	// `Elysium.Substrate.GymSpecOwnership`'s index-paired comparison quietly ran against different
	// solids.
	AddLedges(S, T);
	AddLandings(S, T);
	return S;
}

FVector SeatOrigin(const FVector& FeetWorld, float HullHalfHeightCm)
{
	return FeetWorld + FVector(0.0f, 0.0f, HullHalfHeightCm + ElysiumMove::DistEpsilon);
}

} // namespace ElysiumGym
