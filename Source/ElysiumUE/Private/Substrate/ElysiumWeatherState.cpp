#include "ElysiumWeatherState.h"

void FElysiumWeatherState::Configure(float InFadeIn, float InFadeOut, float InFadeTarget, double Now)
{
	WetnessFadeIn = FMath::Max(0.0f, InFadeIn);
	WetnessFadeOut = FMath::Max(0.0f, InFadeOut);
	WetnessFadeTarget = FMath::Clamp(InFadeTarget, 0.0f, 1.0f);
	InitialWetness = WetnessFadeTarget;
	CurrentWetness = WetnessFadeTarget;
	TargetWetness = WetnessFadeTarget;
	TransitionStart = Now;
	TransitionDuration = 0.0f;
}

float FElysiumWeatherState::ValueAt(double Now) const
{
	if (TransitionDuration <= 0.0f || Now <= TransitionStart)
	{
		return CurrentWetness;
	}
	const float Alpha = FMath::Clamp(
		static_cast<float>((Now - TransitionStart) / TransitionDuration), 0.0f, 1.0f);
	return FMath::Lerp(InitialWetness, TargetWetness, Alpha);
}

void FElysiumWeatherState::Tick(double Now)
{
	CurrentWetness = ValueAt(Now);
	if (TransitionDuration > 0.0f && Now >= TransitionStart + TransitionDuration)
	{
		InitialWetness = TargetWetness;
		CurrentWetness = TargetWetness;
		TransitionStart = Now;
		TransitionDuration = 0.0f;
	}
}

void FElysiumWeatherState::Retarget(float InTarget, double Now)
{
	CurrentWetness = ValueAt(Now);
	InitialWetness = CurrentWetness;
	TargetWetness = FMath::Clamp(InTarget, 0.0f, 1.0f);
	TransitionStart = Now;
	TransitionDuration = TargetWetness > CurrentWetness ? WetnessFadeIn
		: TargetWetness < CurrentWetness ? WetnessFadeOut : 0.0f;
	if (TransitionDuration <= 0.0f)
	{
		InitialWetness = TargetWetness;
		CurrentWetness = TargetWetness;
	}
}
