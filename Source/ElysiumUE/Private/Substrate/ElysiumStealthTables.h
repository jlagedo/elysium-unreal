#pragma once

#include "CoreMinimal.h"

// ================================================================================================
// 18. system/stealth.txt — the four StealthData tables
// ================================================================================================
//
// `docs/vtmb/stealth.md` -> "The rulebook" owns the behaviour. Four named sections under one
// `StealthData` root:
//
//   * `StealthVisionScalarTable`     — Light0..10 x Stealth0..10, multiplier on an observer's
//                                      effective visual range;
//   * `StealthVisionConeScalarTable` — the same shape, multiplier applied in the cone test;
//   * `StealthHearingDistTable`      — Stealth0..10, distance REMOVED from an eligible player
//                                      sound radius, in Source game units;
//   * `StealthLightRangeTable`       — Light0..10, the DESCENDING normalized-light thresholds the
//                                      light row is chosen against.
//
// Both matrices index row-major `light * 11 + Sneaking`; hearing indexes on `Sneaking` alone.
// A missing section or row is a developer diagnostic and the individual read retains the table's
// existing default, which is what the neutral construction below is for. Radii stay in the
// authored Source game units: the one conversion to centimetres happens at the consumer edge,
// like every other recovered distance in the runtime.

struct FElysiumStealthTables
{
	static constexpr int32 NumLight = 11;     // Light0..Light10
	static constexpr int32 NumStealth = 11;   // Stealth0..Stealth10

	// The neutral defaults an unauthored cell keeps: a scalar of 1.0 changes nothing, a hearing
	// reduction of 0 removes nothing, and a threshold of 0 puts every lit value in `Light0`.
	float VisionScalar[NumLight * NumStealth];
	float ConeScalar[NumLight * NumStealth];
	float HearingDistUnits[NumStealth];
	float LightThreshold[NumLight];

	FElysiumStealthTables();

	bool Load(FString& OutError);
	// Every section present, with all 11 rows and all 11 columns authored. A partial parse still
	// answers — each unauthored cell keeps its neutral default — but it is not a valid table.
	bool IsValid() const
	{
		return bVisionLoaded && bConeLoaded && bHearingLoaded && bThresholdsLoaded;
	}

	// Counted in authored VALUES rather than sections: a section that silently lost its tail is the
	// regression that matters, and four is not a number a status row can regress on.
	int32 NumAuthoredValues() const { return AuthoredValues; }

	// The four reads. Every index is clamped rather than checked: the row selector and the feat cap
	// already bound their inputs, and a clamp keeps a patched table that stops short from reading
	// off the end.
	float Vision(int32 Light, int32 Stealth) const;
	float Cone(int32 Light, int32 Stealth) const;
	float HearingUnits(int32 Stealth) const;
	float Threshold(int32 Light) const;

	// What a resolver uses when `stealth.txt` is absent entirely: the neutral table above. Failing
	// open to "stealth changes nothing" is the same posture `FElysiumSoundVolumeTable::
	// NormalFallback()` takes, and it is the correct answer for a character the tables cannot
	// describe.
	static const FElysiumStealthTables& Neutral();

private:
	bool bVisionLoaded = false;
	bool bConeLoaded = false;
	bool bHearingLoaded = false;
	bool bThresholdsLoaded = false;
	int32 AuthoredValues = 0;
};
