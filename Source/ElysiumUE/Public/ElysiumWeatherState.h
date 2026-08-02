#pragma once

#include "CoreMinimal.h"

// Engine-neutral, game-clock weather state. No UObject, component, material or
// Niagara dependency belongs here; the map actor receives value snapshots over
// IElysiumWeather.
struct FElysiumWeatherState
{
	float InitialWetness = 0.0f;
	float CurrentWetness = 0.0f;
	float TargetWetness = 0.0f;
	double TransitionStart = 0.0;
	float TransitionDuration = 0.0f;

	float WetnessFadeIn = 0.0f;
	float WetnessFadeOut = 0.0f;
	float WetnessFadeTarget = 0.0f;

	void Configure(float InFadeIn, float InFadeOut, float InFadeTarget, double Now);
	float ValueAt(double Now) const;
	void Tick(double Now);
	void Retarget(float InTarget, double Now);
};
