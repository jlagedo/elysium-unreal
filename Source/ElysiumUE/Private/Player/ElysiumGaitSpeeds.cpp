#include "ElysiumGaitSpeeds.h"

#include "ElysiumMoveSolve.h"   // the constants the authority falls back to

bool FElysiumGaitSpeedTable::IsValid() const
{
	if (Count < 2 || Count > MaxCells || !(AxisMax > AxisMin) || !FMath::IsFinite(Scale))
	{
		return false;
	}
	// A fan whose every cell baked without root motion carries no speed to answer with. One usable
	// cell is enough: `SpeedFan` interpolates across a hole, and a body with a partly-baked walk
	// still walks at the right speed in the directions that did bake.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FMath::IsFinite(Cells[Index]) && Cells[Index] > 0.0f)
		{
			return true;
		}
	}
	return false;
}

float FElysiumGaitSpeedTable::SpeedAt(float YawDegrees) const
{
	if (!IsValid() || !FMath::IsFinite(YawDegrees))
	{
		return 0.0f;
	}

	const float Span = AxisMax - AxisMin;

	// Wrap into [AxisMin, AxisMax). The fan closes on itself — the last cell is the first cell's
	// clip — so an angle landing exactly on `AxisMax` is the same direction as `AxisMin`, and the
	// double `Fmod` is what keeps a negative yaw from indexing backwards off the front.
	const float Wrapped = AxisMin + FMath::Fmod(FMath::Fmod(YawDegrees - AxisMin, Span) + Span, Span);

	const float Spacing = Span / static_cast<float>(Count - 1);
	const float Position = (Wrapped - AxisMin) / Spacing;

	const int32 Lower = FMath::Clamp(FMath::FloorToInt(Position), 0, Count - 2);
	const int32 Upper = Lower + 1;
	const float Fraction = FMath::Clamp(Position - static_cast<float>(Lower), 0.0f, 1.0f);
	return FMath::Lerp(Cells[Lower], Cells[Upper], Fraction) * Scale;
}

float FElysiumGaitSpeedTable::Forward() const
{
	return SpeedAt(0.0f);
}

float FElysiumGaitSpeedTable::Peak() const
{
	if (!IsValid())
	{
		return 0.0f;
	}
	float Largest = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (FMath::IsFinite(Cells[Index]))
		{
			Largest = FMath::Max(Largest, Cells[Index]);
		}
	}
	return Largest * Scale;
}

void FElysiumGaitSpeedTable::Symmetrize()
{
	if (Count < 2)
	{
		return;
	}
	for (int32 Low = 0, High = Count - 1; Low < High; ++Low, --High)
	{
		const float Mean = 0.5f * (Cells[Low] + Cells[High]);
		Cells[Low] = Mean;
		Cells[High] = Mean;
	}
}

bool FElysiumGaitSpeeds::IsValid() const
{
	return Walk.IsValid() || Run.IsValid() || Sneak.IsValid();
}

float FElysiumGaitSpeeds::Peak() const
{
	return FMath::Max3(Walk.Peak(), Run.Peak(), Sneak.Peak());
}

const FElysiumGaitSpeedTable& ElysiumGait::TableFor(const FElysiumGaitSpeeds& Speeds, bool bDucked,
	bool bWalkKey)
{
	if (bDucked)
	{
		return Speeds.Sneak;
	}
	return bWalkKey ? Speeds.Walk : Speeds.Run;
}

float ElysiumGait::WishSpeedFrom(const FElysiumWishSpeedInput& In, const FElysiumGaitSpeeds& Speeds)
{
	// A dev speed with no original. No gait fan has anything to say about it, and running it through
	// the ladder would let a crouch slow a noclip.
	if (In.bNoclip)
	{
		return In.NoclipSpeed * In.Scale;
	}

	// Airborne, the tables stop refreshing and the client keeps writing the last grounded cell. The
	// held value IS the behaviour — `sv_jump_maxspeed` is a clamp that never fires, because the run
	// peak (208 u/s) is below it.
	if (!In.bOnGround)
	{
		const float Held = In.LastGroundedWishSpeed > 0.0f
			? In.LastGroundedWishSpeed : In.JumpMaxSpeed;
		return Held * In.Scale;
	}

	const FElysiumGaitSpeedTable& Table = TableFor(Speeds, In.bDucked, In.bWalkKey);
	if (Table.IsValid())
	{
		return Table.SpeedAt(In.WishYawDegrees) * In.Scale;
	}

	// The fallback for a body with no fan. The ducked third is **Source's own default, not VtMB's**: retail
	// applies no duck speed multiplier at all and gets its slower crouch from the sneak table, which
	// this path by definition does not have (`docs/vtmb/source_movement.md` → "The ducking speed crop
	// is dead code").
	const float Base = In.bWalkKey ? ElysiumMove::WalkSpeed : ElysiumMove::RunSpeed;
	return (In.bDucked ? Base / 3.0f : Base) * In.Scale;
}
