#include "ElysiumLightRig.h"

#include "ElysiumEditorLabels.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLights, Log, All);

// Live tuning: set before a map (re)loads to scale every point/spot's brightness.
// 0 keeps each rig's PointSpotScale. Applied at Build time, so re-travel to A/B a value.
static TAutoConsoleVariable<float> CVarLightScale(
	TEXT("elysium.LightScale"), 0.f,
	TEXT("LightRig point/spot brightness scale applied at map load (0 = component default)."),
	ECVF_Default);

// Per-area brightness rebalance from the baked lightmap: multiply each light's intensity by
// its `<map>.lightfit` line (reverse-engineered by tools/probe_light_attribution.py), so
// areas VtMB lit brighter/darker than the dynamic rig get nudged toward the baked balance.
// Applied at map load; re-travel to A/B. 0 = ignore the sidecar.
static TAutoConsoleVariable<int32> CVarLightFit(
	TEXT("elysium.LightFit"), 0,
	TEXT("Apply the <map>.lightfit per-area brightness rebalance to the LightRig (0/1)."),
	ECVF_Default);

namespace
{
	// Quake/Source animated-lightstyle patterns (styles 1-11). Each letter is a
	// brightness: 'a' = 0 (dark), 'm' = 1.0 (normal), 'z' ~ 2.08 (over-bright);
	// patterns advance at 10 Hz and lerp between keyframes.
	const char* const LsPatterns[] = {
		"m",                                                    // 0 normal (constant)
		"mmnmmommommnonmmonqnmmo",                              // 1 flicker A
		"abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",  // 2 slow strong pulse
		"mmmmmaaaaammmmmaaaaaabcdefgabcdefg",                   // 3 candle A
		"mamamamamama",                                         // 4 fast strobe
		"jklmnopqrstuvwxyzyxwvutsrqponmlkj",                    // 5 gentle pulse
		"nmonqnmomnmomomno",                                    // 6 flicker B
		"mmmaaaabcdefgmmmmaaaammmaamm",                         // 7 candle B
		"mmmaaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",          // 8 candle C
		"aaaaaaaazzzzzzzz",                                     // 9 slow strobe
		"mmamammmmammamamaaamammma",                            // 10 fluorescent flicker
		"abcdefghijklmnopqrrqponmlkjihgfedcba",                 // 11 slow pulse (not to black)
	};
	constexpr int32 LsCount = UE_ARRAY_COUNT(LsPatterns);
	constexpr float LsFps = 10.f;

	// Current intensity multiplier (0..~2) for a style, lerped between 10 Hz keyframes.
	// Style 0, the unanimated 12-31, and the switchable 32+ all return 1 (held ON).
	float StyleIntensity(int32 Style, float Time)
	{
		if (Style <= 0 || Style >= LsCount)
		{
			return 1.f;
		}
		const char* P = LsPatterns[Style];
		const int32 Len = FCStringAnsi::Strlen(P);
		const float T = Time * LsFps;
		const float Floor = FMath::FloorToFloat(T);
		int32 I0 = ((int32)Floor) % Len;
		if (I0 < 0)
		{
			I0 += Len;
		}
		const int32 I1 = (I0 + 1) % Len;
		const float V0 = (P[I0] - 'a') / 12.f;
		const float V1 = (P[I1] - 'a') / 12.f;
		return FMath::Lerp(V0, V1, T - Floor);
	}
}

UElysiumLightRig::UElysiumLightRig()
{
	PrimaryComponentTick.bCanEverTick = true;
}

int32 UElysiumLightRig::Build(const FString& LightsPath)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *LightsPath))
	{
		return 0;
	}

	float Scale = PointSpotScale;
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.LightScale")))
	{
		if (CVar->GetFloat() > 0.f)
		{
			Scale = CVar->GetFloat();
		}
	}
	// Fold the resolved boot scale back into the tunable field, so the Lights window's slider
	// reflects what the rig actually built with (and live re-tuning stays consistent).
	PointSpotScale = Scale;

	// Optional per-area rebalance: one multiplier per `.lights` line, in the same order.
	TArray<float> Fit;
	if (CVarLightFit.GetValueOnAnyThread() != 0)
	{
		TArray<FString> FitLines;
		if (FFileHelper::LoadFileToStringArray(FitLines, *FPaths::ChangeExtension(LightsPath, TEXT("lightfit"))))
		{
			for (const FString& FL : FitLines)
			{
				const FString T = FL.TrimStartAndEnd();
				if (!T.IsEmpty() && !T.StartsWith(TEXT("#")))
				{
					Fit.Add(FCString::Atof(*T));
				}
			}
		}
	}
	const bool bApplyFit = Fit.Num() > 0;
	int32 FitApplied = 0;

	AActor* Owner = GetOwner();
	int32 LineIdx = -1;
	for (const FString& Line : Lines)
	{
		++LineIdx;   // advance for every line (incl. skipped) to stay aligned with .lightfit
		TArray<FString> P;
		Line.ParseIntoArray(P, TEXT(" "), true);
		if (P.Num() < 15)
		{
			continue;
		}
		const int32 Type = FCString::Atoi(*P[0]);
		const FVector Origin(FCString::Atod(*P[1]), FCString::Atod(*P[2]), FCString::Atod(*P[3]));
		const FVector Dir(FCString::Atod(*P[4]), FCString::Atod(*P[5]), FCString::Atod(*P[6]));
		const FVector Inten(FCString::Atod(*P[7]), FCString::Atod(*P[8]), FCString::Atod(*P[9]));
		const float RadiusCm = FCString::Atof(*P[10]);
		const float StopDot2 = FCString::Atof(*P[12]);   // cos(outer half-angle)
		const int32 Style = FCString::Atoi(*P[14]);

		// Split raw linear intensity into a normalized colour + a scalar magnitude.
		const float Mag = FMath::Max3(Inten.X, Inten.Y, Inten.Z);
		if (Mag <= 0.f)
		{
			continue;
		}
		const FLinearColor Color(Inten.X / Mag, Inten.Y / Mag, Inten.Z / Mag);

		// Skyambient (type 5) is not a light — it tints the map actor's SkyLight.
		if (Type == 5)
		{
			SkyAmbient = Color;
			bHasSkyAmbient = true;
			continue;
		}

		// Soft unitless brightness (clamped), and reach extended past the raw radius so
		// rooms don't fall to black between lights.
		const float Reach = (RadiusCm > 1.f ? RadiusCm : FallbackRadiusCm) * RadiusScale;
		const float FitMult = (bApplyFit && Fit.IsValidIndex(LineIdx)) ? Fit[LineIdx] : 1.f;
		if (bApplyFit && !FMath::IsNearlyEqual(FitMult, 1.f))
		{
			++FitApplied;
		}
		const float SoftIntensity = FMath::Min(Mag * Scale * FitMult, MaxBrightness);

		ULightComponent* Light = nullptr;
		float BaseIntensity = 0.f;
		bool bShadow = false;

		// P1.7 — readable Outliner name (Light_<idx>_<kind>); auto-named in Shipping.
		FName LightName = NAME_None;
#if WITH_EDITOR
		const TCHAR* Kind = Type == 2 ? TEXT("spot") : Type == 3 ? TEXT("sun") : Type == 0 ? TEXT("tex") : TEXT("point");
		LightName = ElysiumEditorObjectName(FString::Printf(TEXT("Light_%d_%s"), LightCount, Kind));
#endif

		if (Type == 1 || Type == 0)
		{
			UPointLightComponent* PL = NewObject<UPointLightComponent>(Owner, LightName);
			PL->SetAttenuationRadius(Reach);
			// Non-inverse-square: gentle exponent falloff (VtMB/Godot soft look).
			PL->bUseInverseSquaredFalloff = false;
			PL->SetLightFalloffExponent(FalloffExponent);
			BaseIntensity = SoftIntensity;
			PL->SetIntensity(BaseIntensity);
			bShadow = bPointShadows && Type != 0;   // texlights stay shadowless
			Light = PL;
		}
		else if (Type == 2)
		{
			USpotLightComponent* SL = NewObject<USpotLightComponent>(Owner, LightName);
			SL->SetAttenuationRadius(Reach);
			SL->bUseInverseSquaredFalloff = false;
			SL->SetLightFalloffExponent(FalloffExponent);
			BaseIntensity = SoftIntensity;
			SL->SetIntensity(BaseIntensity);
			float Outer = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(StopDot2, -1.f, 1.f)));
			Outer = FMath::Clamp(Outer, 1.f, 80.f);
			SL->SetOuterConeAngle(Outer);
			SL->SetInnerConeAngle(FMath::Max(1.f, Outer * 0.6f));
			bShadow = bSpotShadows;
			Light = SL;
		}
		else if (Type == 3)
		{
			UDirectionalLightComponent* DL = NewObject<UDirectionalLightComponent>(Owner, LightName);
			BaseIntensity = FMath::Max(Mag * SunScaleLux, 0.01f);   // lux
			DL->SetIntensity(BaseIntensity);
			bShadow = bSunShadows;
			bHasSun = true;
			Light = DL;
		}
		else
		{
			continue;
		}

		Light->SetMobility(EComponentMobility::Movable);
		Light->SetLightColor(Color);
		// VtMB world is pure Lambert — kill the specular so lights don't glare.
		Light->SpecularScale = SpecularScale;
		Light->SetCastShadows(bShadow);
		Light->SetupAttachment(this);
		Light->RegisterComponent();
		Light->SetWorldLocation(Origin);

		// Unreal spot/directional lights emit along their +X axis; aim it down the beam.
		if ((Type == 2 || Type == 3) && !Dir.IsNearlyZero())
		{
			Light->SetWorldRotation(Dir.Rotation());
		}

		Lights.Add(Light);
		++LightCount;

		const int32 SrcStyle = (Style >= 1 && Style < LsCount) ? Style : 0;
		LightSources.Add({ Light, Type, Mag, RadiusCm, FitMult, SrcStyle, BaseIntensity });
	}

	int32 AnimatedNum = 0;
	for (const FLightSource& S : LightSources)
	{
		AnimatedNum += (S.Style >= 1) ? 1 : 0;
	}

	UE_LOG(LogElysiumLights, Log, TEXT("LightRig: %d lights (%d animated)%s%s%s"),
		LightCount, AnimatedNum,
		bHasSun ? TEXT(" +sun") : TEXT(""),
		bHasSkyAmbient ? TEXT(" +skyambient") : TEXT(""),
		bApplyFit ? *FString::Printf(TEXT(" +lightfit(%d nudged)"), FitApplied) : TEXT(""));
	return LightCount;
}

void UElysiumLightRig::SetLightsVisible(bool bShow)
{
	bLightsVisible = bShow;
	for (ULightComponent* Light : Lights)
	{
		if (Light)
		{
			Light->SetVisibility(bShow);
		}
	}
}

void UElysiumLightRig::ApplyLiveTuning()
{
	for (FLightSource& S : LightSources)
	{
		ULightComponent* Light = S.Light.Get();
		if (Light == nullptr)
		{
			continue;
		}

		if (S.Type == 3)
		{
			// Sun/directional: lux scaled off the raw magnitude, no falloff/reach.
			S.BaseIntensity = FMath::Max(S.Mag * SunScaleLux, 0.01f);
		}
		else
		{
			const float Reach = (S.RadiusCm > 1.f ? S.RadiusCm : FallbackRadiusCm) * RadiusScale;
			S.BaseIntensity = FMath::Min(S.Mag * PointSpotScale * S.FitMult, MaxBrightness);
			if (UPointLightComponent* PL = Cast<UPointLightComponent>(Light))
			{
				PL->SetAttenuationRadius(Reach);
				PL->SetLightFalloffExponent(FalloffExponent);
			}
			else if (USpotLightComponent* SL = Cast<USpotLightComponent>(Light))
			{
				SL->SetAttenuationRadius(Reach);
				SL->SetLightFalloffExponent(FalloffExponent);
			}
		}

		Light->SpecularScale = SpecularScale;
		// Styled lights get their per-frame flicker off this new base next tick; set the base now
		// so unanimated lights update immediately (and animated ones don't stall on a paused clock).
		Light->SetIntensity(S.BaseIntensity);
	}
}

void UElysiumLightRig::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StyleTime += DeltaTime;
	for (const FLightSource& S : LightSources)
	{
		if (S.Style >= 1 && S.Light.IsValid())
		{
			S.Light->SetIntensity(S.BaseIntensity * StyleIntensity(S.Style, StyleTime));
		}
	}
}
