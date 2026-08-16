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

	// --- The plate ------------------------------------------------------------------------------
	// Its TOP is at Z = 0, so every feet-anchored point in the room is at zero and a recorded
	// coordinate reads as a position in the room rather than as a position plus a slab thickness.
	const float H = RoomHalfExtent;
	const float Outer = H + WallThickness;
	S.Solids.Add(Block(TEXT("floor"), -Outer, Outer, -Outer, Outer, -FloorThickness, 0.0f));

	// --- The four walls -------------------------------------------------------------------------
	// Placed OUTSIDE the interior half-extent, so `RoomHalfExtent` is the usable floor rather than
	// the distance to a wall's centreline. Named for the axis they face down: +X is north.
	S.Solids.Add(Block(TEXT("wall_north"),  H, Outer, -Outer, Outer, 0.0f, WallHeight));
	S.Solids.Add(Block(TEXT("wall_south"), -Outer, -H, -Outer, Outer, 0.0f, WallHeight));
	S.Solids.Add(Block(TEXT("wall_east"),  -H, H,  H, Outer, 0.0f, WallHeight));
	S.Solids.Add(Block(TEXT("wall_west"),  -H, H, -Outer, -H,  0.0f, WallHeight));

	// --- The cover block ------------------------------------------------------------------------
	const float B = BlockHalfWidth;
	S.Solids.Add(Block(TEXT("cover"), -B, B, -B, B, 0.0f, BlockHeight));

	// --- The anchors ----------------------------------------------------------------------------
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

	// --- The spawn pads -------------------------------------------------------------------------
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

	// --- The player's start ---------------------------------------------------------------------
	// Against the south wall, looking down the long axis at the block. Feet-anchored; `SeatOrigin`
	// is the one conversion to the pawn's centre and there is no other.
	S.PlayerFeet = FVector(-(H - PadInset) * U, 0.0f, 0.0f);
	S.PlayerYaw = 0.0f;

	return S;
}

} // namespace ElysiumArena
