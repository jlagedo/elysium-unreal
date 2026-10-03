#include "Debug/ElysiumArenaSpec.h"

#include "ElysiumMoveSolve.h"

namespace ElysiumArena
{

namespace
{
	constexpr float U = ElysiumMove::U;

	// An axis-aligned solid from its Source-unit AABB. Corners rather than a centre and a
	// half-extent, for the reason the gym states: a wall is "floor level to `WallHeight`", not
	// "centred at half of it".
	FSolid Block(const TCHAR* Tag, float X0, float X1, float Y0, float Y1, float Z0, float Z1)
	{
		FSolid S;
		S.Tag = FName(Tag);
		S.Center = FVector((X0 + X1) * 0.5f * U, (Y0 + Y1) * 0.5f * U, (Z0 + Z1) * 0.5f * U);
		S.Extent = FVector((X1 - X0) * 0.5f * U, (Y1 - Y0) * 0.5f * U, (Z1 - Z0) * 0.5f * U);
		return S;
	}

	// Yaw, in degrees, from one Source-unit point toward another. Every anchor and pad in the room
	// is oriented by where it looks rather than by a hand-written angle, so moving the block or the
	// walls re-aims them instead of leaving one facing a place that is no longer there.
	float YawToward(float FromX, float FromY, float ToX, float ToY)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(ToY - FromY, ToX - FromX));
	}

	// Yaw, in degrees, that puts the point `To` at `acos(TargetDot)` counter-clockwise off the facing
	// — i.e. `dot(facing, normalize(To - From)) == TargetDot`. How a cover node is aimed: retail's
	// `ValidateHintCoverRange` (`0x10296c40`) admits a hint by `dot(hint facing, normalize(enemy -
	// hint))` inside its type's band, so the node is authored at a chosen dot against the scenario's
	// enemy position rather than at a hand-written angle. Unreal-native yaw: the Source `angles` the
	// builder writes negate it, and the Source enemy direction negates Y, so the dot is the same in
	// both frames.
	float YawAtDotToward(float FromX, float FromY, float ToX, float ToY, float TargetDot)
	{
		return YawToward(FromX, FromY, ToX, ToY) + FMath::RadiansToDegrees(FMath::Acos(TargetDot));
	}

	// The dots the cover nodes are authored at, each inside its type's `0x10296c40` band.
	// Low cover (101, `IsHintCoverValidLoose`: good 0.87, bad 1.10): the hint faces the threat within
	// ~18 deg, the block in front of it.
	constexpr float CoverLowFacingDot = 0.95f;
	// Corner cover (10200, `IsHintCoverValid`: good 0.50, bad 0.73): the threat ~52 deg off the hint's
	// facing — mid-band, the peek round a corner rather than a stare down it.
	constexpr float CoverCornerFacingDot = 0.62f;

	FAnchor MakeAnchor(const TCHAR* Name, float X, float Y, float Yaw, int32 Rating, bool bAgainstCover)
	{
		FAnchor A;
		A.Name = FName(Name);
		A.FeetOrigin = FVector(X * U, Y * U, 0.0f);
		A.Yaw = Yaw;
		A.Rating = Rating;
		A.bAgainstCover = bAgainstCover;
		return A;
	}

	FNode MakeNode(const TCHAR* Name, float X, float Y, float Yaw, int32 HintType)
	{
		FNode N;
		N.Name = Name;
		N.FeetCm = FVector(X * U, Y * U, 0.0f);
		N.YawDeg = Yaw;
		N.HintType = HintType;
		return N;
	}

	FPad MakePad(const TCHAR* Name, float X, float Y)
	{
		FPad P;
		P.Name = FName(Name);
		P.FeetOrigin = FVector(X * U, Y * U, 0.0f);
		P.Yaw = YawToward(X, Y, 0.0f, 0.0f);   // every pad faces the room's middle
		return P;
	}
}

const FPad* FSpec::FindPad(const FName& Name) const
{
	return Pads.FindByPredicate([&Name](const FPad& P) { return P.Name == Name; });
}

const FAnchor* FSpec::FindAnchor(const FName& Name) const
{
	return Anchors.FindByPredicate([&Name](const FAnchor& A) { return A.Name == Name; });
}

const FNode* FSpec::FindNode(const FString& Name) const
{
	return Nodes.FindByPredicate([&Name](const FNode& N) { return N.Name.Equals(Name, ESearchCase::IgnoreCase); });
}

FBox FSpec::Bounds() const
{
	FBox Box(ForceInit);
	for (const FSolid& S : Solids)
	{
		Box += FBox(S.Center - S.Extent, S.Center + S.Extent);
	}
	return Box;
}

float FSpec::FloorZ() const
{
	return 0.0f;   // the plate's top surface is the arena's own Z origin (see Build)
}

FSpec Build()
{
	FSpec S;

	// The plate.
	// Its TOP is at Z = 0, so every feet-anchored point in the room is at zero and a recorded
	// coordinate reads as a position in the room rather than as a position plus a slab thickness.
	const float H = RoomHalfExtent;
	const float Outer = H + WallThickness;
	S.Solids.Add(Block(TEXT("floor"), -Outer, Outer, -Outer, Outer, -FloorThickness, 0.0f));

	// The four walls.
	// Placed OUTSIDE the interior half-extent, so `RoomHalfExtent` is the usable floor rather than
	// the distance to a wall's centreline. Named for the axis they face down: +X is north.
	S.Solids.Add(Block(TEXT("wall_north"),  H, Outer, -Outer, Outer, 0.0f, WallHeight));
	S.Solids.Add(Block(TEXT("wall_south"), -Outer, -H, -Outer, Outer, 0.0f, WallHeight));
	S.Solids.Add(Block(TEXT("wall_east"),  -H, H,  H, Outer, 0.0f, WallHeight));
	S.Solids.Add(Block(TEXT("wall_west"),  -H, H, -Outer, -H,  0.0f, WallHeight));

	// The cover block.
	const float B = BlockHalfWidth;
	S.Solids.Add(Block(TEXT("cover"), -B, B, -B, B, 0.0f, BlockHeight));

	// The anchors.
	// Four against the block's faces and four in the corners. The face anchors face OUT of the room
	// with their backs to the solid, which is the posture a body in cover stands in; the corner
	// anchors face the middle, because a corner has nothing to put a back against that is not
	// already the wall behind them.
	//
	// The face set carries the higher rating so `PickHighestRatedCandidate` prefers it. That is an
	// ARENA authoring choice expressed through the entity's own authored field, not a new rule: the
	// selection arithmetic is `ElysiumInterestingPlaces`' and is untouched.
	const float FaceOut = B + FaceAnchorSetback;
	S.Anchors.Add(MakeAnchor(TEXT("cover_north"),  FaceOut, 0.0f,   0.0f, 10, /*bAgainstCover=*/true));
	S.Anchors.Add(MakeAnchor(TEXT("cover_south"), -FaceOut, 0.0f, 180.0f, 10, /*bAgainstCover=*/true));
	S.Anchors.Add(MakeAnchor(TEXT("cover_east"),  0.0f,  FaceOut,  90.0f, 10, /*bAgainstCover=*/true));
	S.Anchors.Add(MakeAnchor(TEXT("cover_west"),  0.0f, -FaceOut, -90.0f, 10, /*bAgainstCover=*/true));

	const float C = H - CornerAnchorInset;
	S.Anchors.Add(MakeAnchor(TEXT("corner_ne"),  C,  C, YawToward( C,  C, 0.0f, 0.0f), 4, false));
	S.Anchors.Add(MakeAnchor(TEXT("corner_nw"),  C, -C, YawToward( C, -C, 0.0f, 0.0f), 4, false));
	S.Anchors.Add(MakeAnchor(TEXT("corner_se"), -C,  C, YawToward(-C,  C, 0.0f, 0.0f), 4, false));
	S.Anchors.Add(MakeAnchor(TEXT("corner_sw"), -C, -C, YawToward(-C, -C, 0.0f, 0.0f), 4, false));

	// The spawn pads.
	// Three at the far half's edges plus the middle, all facing in. `north` is directly across the
	// room from where the player starts, which is the pad a first spawn wants.
	const float P = H - PadInset;
	S.Pads.Add(MakePad(TEXT("north"),  P, 0.0f));
	S.Pads.Add(MakePad(TEXT("east"),  0.0f,  P));
	S.Pads.Add(MakePad(TEXT("west"),  0.0f, -P));
	S.Pads.Add(MakePad(TEXT("far_ne"), P * 0.7f,  P * 0.7f));
	S.Pads.Add(MakePad(TEXT("far_nw"), P * 0.7f, -P * 0.7f));
	// Deliberately behind the block from the player's start: the pad that spawns a character the
	// player cannot see, which is the only way to watch an acquisition happen rather than start
	// already acquired.
	S.Pads.Add(MakePad(TEXT("behind_cover"), B + FaceAnchorSetback * 2.5f, 0.0f));

	// The player's start.
	// Against the south wall, looking down the long axis at the block. Feet-anchored; `SeatOrigin`
	// is the one conversion to the pawn's centre and there is no other.
	S.PlayerFeet = FVector(-(H - PadInset) * U, 0.0f, 0.0f);
	S.PlayerYaw = 0.0f;

	// The player behind the block: the `behind_cover` pad mirrored onto the player's side, ~2 m off
	// the south face, still looking north at the solid. From the north pad the block is between.
	S.PlayerCoverFeet = FVector(-(B + PlayerCoverSetback) * U, 0.0f, 0.0f);
	S.PlayerCoverYaw = 0.0f;

	// The cover scenario's seats (brief S4). The gunman is on `far_ne` (P*0.7, P*0.7 units, ~683 cm);
	// the open seat is (CoverSeatXCm, far_ne.y) facing +X toward it, ~983 cm off and inside vision.
	// That line is y = far_ne.y, so it clears the block only while the block's half width is well
	// under far_ne's Y: asserted here, in Source units, so moving either fails the build rather than
	// silently putting the block on the line.
	const float FarNe = P * 0.7f;
	static_assert(BlockHalfWidth < (RoomHalfExtent - PadInset) * 0.7f,
		"the cover block must not reach the open seat's sight line y = far_ne.y");
	S.CoverSeatFeet = FVector(CoverSeatXCm, FarNe * U, 0.0f);
	S.CoverSeatYaw = YawToward(S.CoverSeatFeet.X, S.CoverSeatFeet.Y, FarNe * U, FarNe * U);

	// The occluded seat: on the far_ne -> block-centre line, (BlockHalfWidth + 150 cm) past the
	// centre, so the block sits between it and the gunman. Facing far_ne.
	const float BehindCm = B * U + CoverBehindSetbackCm;
	const float Diagonal = BehindCm * 0.70710678f;
	S.CoverBehindFeet = FVector(-Diagonal, -Diagonal, 0.0f);
	S.CoverBehindYaw = YawToward(S.CoverBehindFeet.X, S.CoverBehindFeet.Y, FarNe * U, FarNe * U);

	// The cover nodes (brief S8).
	// Aimed against the cover scenario: the ENEMY `0x10296c40` validates against is the player on the
	// open seat (-300, 683) cm and the searching NPC is the gunman on `far_ne` (683, 683) cm. Two
	// gates decide a node there:
	//   facing     dot(hint facing, normalize(enemy - hint)) inside the type's band (see the dots);
	//   projection dot(normalize(hint - enemy), normalize(npc - enemy)) >= 0.2 — the hint on the NPC's
	//              side of the enemy.
	// Only the type, the group and the yaw are authored: the distances, the angle range and the
	// rating are `CAI_Hint::Spawn`'s per-type defaults. Network order is this order, so a node's index
	// here is the node id its hint takes. Numbers below are computed from this geometry (Source
	// units: seat (-118.1, 268.8), far_ne (268.8, 268.8)).
	const float SeatX = CoverSeatXCm / U;
	const float SeatY = FarNe;

	// Low cover, on the block's +X (north) and -X (south) faces at the face anchors' setback.
	// The projection gate picks the face, whatever the yaw: +X gives 0.663 (passes), -X gives -0.007
	// (the old "Projection < 0.2" refusal) because it sits level with the seat, across from the NPC.
	// The +X node faces the seat through the block: toward-seat yaw 131.54 deg + acos(0.95) 18.19 deg
	// = 149.73 deg, facing (-0.864, 0.505); facing dot 0.950 (was -0.663 at yaw 0), projection
	// 0.663, 359 units to the seat, and the seat line crosses the block's +X face.
	const float LowNorthYaw = YawAtDotToward(FaceOut, 0.0f, SeatX, SeatY, CoverLowFacingDot);
	S.Nodes.Add(MakeNode(TEXT("cover_low_north"),  FaceOut, 0.0f, LowNorthYaw, HintTypeCoverLow));
	// The -X node is the +X node mirrored across the block's centre line (yaw 180 - 149.73 = 30.27
	// deg), for the mirrored scenario (seat (+300, 683) cm, NPC at (-683, 683) cm): facing 0.950,
	// projection 0.663 there. Against THIS seat it stays refused (facing 0.510, projection -0.007).
	S.Nodes.Add(MakeNode(TEXT("cover_low_south"), -FaceOut, 0.0f, 180.0f - LowNorthYaw,
		HintTypeCoverLow));

	// Corner cover, in the NE and NW room corners. Facing the middle they stared at the seat (dots
	// 0.847 and 0.992, both refused "Enemy inside of bad range" >= 0.73). Each is rotated off the
	// seat by acos(0.62) = 51.68 deg, the way that turns it least from its old toward-the-middle yaw:
	//   ne: toward-seat -167.08 deg -> -115.39 deg (middle -135 rotated +19.61 deg), facing
	//       (-0.429, -0.903); facing dot 0.620, projection 0.975, 515 units.
	//   nw: toward-seat  127.57 deg ->  179.25 deg (middle  135 rotated +44.25 deg), facing
	//       (-1.000, 0.013); facing dot 0.620, projection 0.610, 824 units.
	S.Nodes.Add(MakeNode(TEXT("cover_corner_ne"),  C,  C,
		YawAtDotToward(C,  C, SeatX, SeatY, CoverCornerFacingDot), HintTypeCoverCorner));
	S.Nodes.Add(MakeNode(TEXT("cover_corner_nw"),  C, -C,
		YawAtDotToward(C, -C, SeatX, SeatY, CoverCornerFacingDot), HintTypeCoverCorner));

	return S;
}

} // namespace ElysiumArena
