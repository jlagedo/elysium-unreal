#include "ElysiumGaitSpeeds.h"

bool FElysiumGaitSpeedTable::IsValid() const
{
	// `Scale` must be POSITIVE, not merely finite: a zero or negative multiplier makes `Peak()` zero
	// on cells that carry real speed, which is a fan reporting itself as absent. A fan that cannot
	// answer a speed is not a fan, and it has to say so rather than answer a wrong number.
	if (Count < 2 || Count > MaxCells || !(AxisMax > AxisMin) || !FMath::IsFinite(Scale)
		|| !(Scale > 0.0f))
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

	// Airborne, the tables stop refreshing and the client keeps reading the last grounded cell, so
	// the held value IS the behaviour. **`sv_jump_maxspeed` is a ceiling over it, never a stand-in
	// for it** — retail pins `m_flMaxspeed` to it while the jump phase is live and clamps the wish
	// velocity against the pin, which at stock settings never cuts because the run peak (208 u/s)
	// is below it (`docs/vtmb/source_movement.md` → "Airborne").
	//
	// A held zero is a held value, not a missing one: it is what a body standing still leaves behind,
	// and it is written on every grounded frame. Reading it as absent hands a standing jump the full
	// 350 u/s, which the gait classifier then maxes against the body's real speed and lands in the
	// run fan.
	if (!In.bOnGround)
	{
		return FMath::Min(In.LastGroundedWishSpeed, In.JumpMaxSpeed) * In.Scale;
	}

	const FElysiumGaitSpeedTable& Table = TableFor(Speeds, In.bDucked, In.bWalkKey);
	if (Table.IsValid())
	{
		return Table.SpeedAt(In.WishYawDegrees) * In.Scale;
	}

	// **A gait with no fan commands zero, because that is what retail's table holds.** `PreThink`'s
	// fill is conditional on the resolved sequence being a 9-blend grid; when it is not, the slot is
	// never written and the client keeps reading whatever was already in it. `Spawn` is the only site
	// that clears the six tables, so on a body that never had that fan the held value IS zero — and
	// there is no constant, no per-gait substitute and no `speed_runbase` anywhere on that path
	// (`docs/vtmb/source_movement.md` → "Fallbacks").
	//
	// Retail's other reachable state is a mid-game model swap: the tables are never cleared between
	// bodies, so a stale real fan outlives the body that authored it. This set is re-resolved whole
	// per body, so it cannot carry that one — what it reproduces is the fresh-body state, which is
	// the honest half of the pair.
	return 0.0f;
}

float ElysiumGait::MaxSpeedFrom(const FElysiumWishSpeedInput& In, const FElysiumGaitSpeeds& Speeds)
{
	if (In.bNoclip)
	{
		return In.NoclipSpeed;
	}
	// The jump-phase ceiling. `PreThink` writes `sv_jump_maxspeed` into `m_flMaxspeed` for the whole
	// of a jump instead of refilling it from the tables, so an air attack is bounded by 350 u/s
	// rather than by the run peak.
	if (!In.bOnGround)
	{
		return In.JumpMaxSpeed;
	}
	if (Speeds.IsValid())
	{
		return Speeds.Peak();
	}
	// A body with no fan at all has no ceiling either, and zero is the value rather than the absence
	// of one. `m_flMaxspeed` is written the same conditional way the cells are — `if (peak > 0.0f)`,
	// else it holds — and `Spawn` zeroes it alongside the six tables, so a body whose every gait
	// resolved nothing carries the zero it spawned with. It is consistent with the cells rather than
	// a separate rule: nothing can command a speed here, so nothing needs bounding, and the one
	// caller that reads it (`SetupMove`'s `CheckParameters` clamp) erases a substituted lunge exactly
	// as retail's zero-seeded `mv->m_flMaxSpeed` does.
	return 0.0f;
}
