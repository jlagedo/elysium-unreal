#include "Visual/ElysiumLightRig.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEditorLabels.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumLights, Log, All);

// Live tuning: set before a map (re)loads to scale every point/spot's brightness.
// 0 keeps each rig's PointSpotScale. Applied at Build time, so re-travel to A/B a value.
static TAutoConsoleVariable<float> CVarLightScale(
	TEXT("elysium.LightScale"), 0.f,
	TEXT("LightRig point/spot brightness scale applied at map load (0 = component default)."),
	ECVF_Default);

// Per-area brightness rebalance from the baked lightmap: multiply each light's intensity by
// its `<map>.lightfit` line (reverse-engineered by research/tooling/probes/probe_light_attribution.py), so
// areas VtMB lit brighter/darker than the dynamic rig get nudged toward the baked balance.
// Applied at map load; re-travel to A/B. 0 = ignore the sidecar.
static TAutoConsoleVariable<int32> CVarLightFit(
	TEXT("elysium.LightFit"), 0,
	TEXT("Apply the <map>.lightfit per-area brightness rebalance to the LightRig (0/1)."),
	ECVF_Default);

// A/B the point/spot brightness ceiling. 0 clips at MaxBrightness (8.0), which on a hub map pins
// roughly half the live sources to one flat value; 1 clips at ExtendedMaxBrightness instead, which
// no source reaches, so the authored range reaches the tone curve intact. Everything below the
// old ceiling is unchanged either way. Applied at map load; re-travel to A/B.
static TAutoConsoleVariable<int32> CVarLightCurve(
	TEXT("elysium.LightCurve"), 0,
	TEXT("Point/spot brightness ceiling: 0 = MaxBrightness, 1 = ExtendedMaxBrightness (0/1)."),
	ECVF_Default);

// The map's saved light edits (`_lights/<map>.json`, written by the Lights window) are applied at
// map load when present: calibration, switched-off sources and full attribute overrides. That
// makes the file the standing hand-authored state; 0 loads the full faithful source set.
static TAutoConsoleVariable<int32> CVarLightSurvey(
	TEXT("elysium.LightSurvey"), 1,
	TEXT("Auto-apply saved light calibration, disabled sources and overrides at map load (0/1)."),
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

	float ConeDegrees(float Cosine)
	{
		return FMath::Clamp(FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Cosine, -1.f, 1.f))),
			1.f, 80.f);
	}

	bool ReadNumber(const FJsonObject& Object, const TCHAR* Field, float& Out)
	{
		double Value = 0.0;
		if (!Object.TryGetNumberField(Field, Value) || !FMath::IsFinite(Value))
		{
			return false;
		}
		Out = static_cast<float>(Value);
		return true;
	}

	bool ReadVector(const FJsonObject& Object, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.TryGetArrayField(Field, Values) || Values->Num() != 3)
		{
			return false;
		}
		double X = 0.0, Y = 0.0, Z = 0.0;
		if (!(*Values)[0]->TryGetNumber(X) || !(*Values)[1]->TryGetNumber(Y)
			|| !(*Values)[2]->TryGetNumber(Z))
		{
			return false;
		}
		Out = FVector(X, Y, Z);
		return !Out.ContainsNaN();
	}

	bool ReadColor(const FJsonObject& Object, const TCHAR* Field, FLinearColor& Out)
	{
		FVector RGB;
		if (!ReadVector(Object, Field, RGB))
		{
			return false;
		}
		Out = FLinearColor(
			FMath::Max(static_cast<float>(RGB.X), 0.f),
			FMath::Max(static_cast<float>(RGB.Y), 0.f),
			FMath::Max(static_cast<float>(RGB.Z), 0.f));
		return true;
	}

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

int32 UElysiumLightRig::Adopt(const TArray<FAdoptedLight>& Adopted, const FString& LightsPath,
	float SkyReach)
{
	SkyReachScale = SkyReach > 0.f ? SkyReach : 1.f;

	Lights.Reset();
	LightSources.Reset();
	LightCount = 0;
	bHasSun = false;
	bHasSkyAmbient = false;

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *LightsPath))
	{
		UE_LOG(LogElysiumLights, Warning, TEXT("LightRig: no %s — baked lights left as authored"),
			*LightsPath);
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
	// reflects what the rig actually applied (and live re-tuning stays consistent).
	PointSpotScale = Scale;

	bExtendedRange = CVarLightCurve.GetValueOnAnyThread() != 0;

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

	// The sidecar row behind each line, indexed the way the bake tagged its actors. Type 5
	// (skyambient) is not a light and never has an actor; it only tints the sky fallback.
	struct FRow
	{
		int32 Type = 1;
		float Mag = 0.f;
		float RadiusCm = 0.f;
		float StopDot = 0.f;
		float StopDot2 = 0.f;
		int32 Style = 0;
		FLinearColor Color = FLinearColor::White;
		bool bSky = false;
	};
	TArray<FRow> Rows;
	Rows.SetNum(Lines.Num());
	for (int32 LineIdx = 0; LineIdx < Lines.Num(); ++LineIdx)
	{
		TArray<FString> P;
		Lines[LineIdx].ParseIntoArray(P, TEXT(" "), true);
		if (P.Num() < 15)
		{
			continue;
		}
		const int32 Type = FCString::Atoi(*P[0]);
		const FVector Inten(FCString::Atod(*P[7]), FCString::Atod(*P[8]), FCString::Atod(*P[9]));
		const float Mag = FMath::Max3(Inten.X, Inten.Y, Inten.Z);
		if (Mag <= 0.f)
		{
			continue;
		}
		const FLinearColor Color(Inten.X / Mag, Inten.Y / Mag, Inten.Z / Mag);

		if (Type == 5)
		{
			// FIRST wins, by `.lights` line order (which preserves lump-15 order): VRAD resolves
			// the sky ambient once, globally, first-entity-wins and stamps it on every type-5
			// row, and the engine's own multi-light_environment rule is first-wins as well
			// (RE-A3/RE-A5). Assigning unconditionally read the last row instead.
			if (!bHasSkyAmbient)
			{
				SkyAmbient = Color;
				SkyAmbientMag = Mag;
				bHasSkyAmbient = true;
			}
			continue;
		}

		FRow& Row = Rows[LineIdx];
		Row.Type = Type;
		Row.Mag = Mag;
		Row.RadiusCm = FCString::Atof(*P[10]);
		Row.StopDot = FCString::Atof(*P[11]);
		Row.StopDot2 = FCString::Atof(*P[12]);
		// Field 16 (optional on older exports): the source lights the 3D-skybox miniature.
		Row.bSky = P.Num() >= 16 && FCString::Atoi(*P[15]) != 0;
		const int32 Style = FCString::Atoi(*P[14]);
		Row.Style = (Style >= 1 && Style < LsCount) ? Style : 0;
		Row.Color = Color;
	}

	int32 Unmatched = 0;
	for (const FAdoptedLight& Entry : Adopted)
	{
		if (Entry.Light == nullptr)
		{
			continue;
		}
		if (!Rows.IsValidIndex(Entry.SourceIndex) || Rows[Entry.SourceIndex].Mag <= 0.f)
		{
			// A light with no readable source row keeps whatever the bake gave it, but it cannot
			// be tuned or animated — so say so rather than silently leaving a dead light.
			++Unmatched;
			continue;
		}
		const FRow& Row = Rows[Entry.SourceIndex];
		const float FitMult = (bApplyFit && Fit.IsValidIndex(Entry.SourceIndex)) ? Fit[Entry.SourceIndex] : 1.f;

		// Colour is fixed data, so it is set once here; everything derived from the calibration
		// constants is left to ApplyLiveTuning, which is the single place those live.
		Entry.Light->SetLightColor(Row.Color);
		Lights.Add(Entry.Light);
		FLightSource Source;
		Source.Light = Entry.Light;
		Source.SourceIndex = Entry.SourceIndex;
		Source.Type = Row.Type;
		Source.Mag = Row.Mag;
		Source.RadiusCm = Row.RadiusCm;
		Source.StopDot = Row.StopDot;
		Source.StopDot2 = Row.StopDot2;
		Source.FitMult = FitMult;
		Source.Style = Row.Style;
		Source.Color = Row.Color;
		Source.AuthoredTransform = Entry.Light->GetComponentTransform();
		Source.bAuthoredCastVolumetricShadow = Entry.Light->bCastVolumetricShadow;
		if (const UPointLightComponent* Point = Cast<UPointLightComponent>(Entry.Light))
		{
			Source.AuthoredSourceRadiusCm = Point->SourceRadius;
			Source.AuthoredSoftSourceRadiusCm = Point->SoftSourceRadius;
			Source.AuthoredSourceLengthCm = Point->SourceLength;
		}
		Source.bSky = Row.bSky;
		LightSources.Add(MoveTemp(Source));
		bHasSun |= (Row.Type == 3);
		++LightCount;
	}

	// One pass derives every intensity/reach/falloff/specular from the current tuning fields —
	// the same call the Lights window makes, so a fresh load and a slider drag agree exactly.
	ApplyLiveTuning();

	int32 AnimatedNum = 0;
	// How many sources the ceiling is actually clipping — the number the A/B turns on, so it is
	// reported rather than inferred from the look.
	int32 ClippedNum = 0;
	const float Ceiling = bExtendedRange ? ExtendedMaxBrightness : MaxBrightness;
	for (const FLightSource& S : LightSources)
	{
		AnimatedNum += (S.Style >= 1) ? 1 : 0;
		ClippedNum += (S.Type != 3 && S.Mag * PointSpotScale * S.FitMult > Ceiling) ? 1 : 0;
	}
	UE_LOG(LogElysiumLights, Log,
		TEXT("LightRig: adopted %d baked lights (%d animated)%s%s%s%s · ceiling %.1f%s clips %d"),
		LightCount, AnimatedNum,
		bHasSun ? TEXT(" +sun") : TEXT(""),
		bHasSkyAmbient ? TEXT(" +skyambient") : TEXT(""),
		bApplyFit ? TEXT(" +lightfit") : TEXT(""),
		Unmatched > 0 ? *FString::Printf(TEXT(" (%d unmatched)"), Unmatched) : TEXT(""),
		Ceiling, bExtendedRange ? TEXT(" (extended)") : TEXT(""), ClippedNum);

	// The standing hand-authored light state: apply the map's saved survey whenever one exists.
	// A missing file is the normal case and stays silent; a file that fails to apply is a warning.
	SurveyMapName = FPaths::GetBaseFilename(LightsPath);
	if (CVarLightSurvey.GetValueOnAnyThread() != 0
		&& FPaths::FileExists(FElysiumContentPaths::LightEdits(SurveyMapName)))
	{
		FString SurveyMessage;
		if (LoadSurvey(SurveyMessage))
		{
			UE_LOG(LogElysiumLights, Log, TEXT("LightRig: %s"), *SurveyMessage);
		}
		else
		{
			UE_LOG(LogElysiumLights, Warning, TEXT("LightRig: %s"), *SurveyMessage);
		}
	}
	return LightCount;
}

bool UElysiumLightRig::LoadSurvey(FString& OutMessage)
{
	const FString Path = FElysiumContentPaths::LightEdits(SurveyMapName);
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutMessage = FString::Printf(TEXT("no save at %s"), *Path);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		OutMessage = FString::Printf(TEXT("parse failed: %s"), *Path);
		return false;
	}

	// Restore the saved global calibration before resetting sources, so RevertAllSources derives
	// the same baseline the edits were authored against. Missing fields keep current defaults, which
	// makes older survey-only files forward-compatible.
	if (const TSharedPtr<FJsonObject>* Calibration = nullptr;
		Root->TryGetObjectField(TEXT("calibration"), Calibration))
	{
		float Value = 0.f;
		if (ReadNumber(**Calibration, TEXT("point_spot_scale"), Value))
			PointSpotScale = FMath::Clamp(Value, 0.00001f, 0.1f);
		if (ReadNumber(**Calibration, TEXT("max_brightness"), Value))
			MaxBrightness = FMath::Clamp(Value, 0.01f, 100.f);
		if (ReadNumber(**Calibration, TEXT("falloff_exponent"), Value))
			FalloffExponent = FMath::Clamp(Value, 0.1f, 16.f);
		if (ReadNumber(**Calibration, TEXT("radius_scale"), Value))
			RadiusScale = FMath::Clamp(Value, 0.01f, 10.f);
		if (ReadNumber(**Calibration, TEXT("specular_scale"), Value))
			SpecularScale = FMath::Clamp(Value, 0.f, 1.f);
		if (ReadNumber(**Calibration, TEXT("indirect_lighting_scale"), Value))
			IndirectLightingScale = FMath::Clamp(Value, 0.f, 6.f);
		if (ReadNumber(**Calibration, TEXT("volumetric_scattering_scale"), Value))
			VolumetricScatteringScale = FMath::Clamp(Value, 0.f, 4.f);
		if (ReadNumber(**Calibration, TEXT("sun_lux_scale"), Value))
			SunScaleLux = FMath::Clamp(Value, 0.01f, 100.f);
		if (ReadNumber(**Calibration, TEXT("sun_source_angle_deg"), Value))
			SunSourceAngleDegrees = FMath::Clamp(Value, 0.f, 5.f);
		if (ReadNumber(**Calibration, TEXT("sun_soft_source_angle_deg"), Value))
			SunSoftSourceAngleDegrees = FMath::Clamp(Value, 0.f, 5.f);
		(*Calibration)->TryGetBoolField(TEXT("point_shadows"), bPointShadows);
		(*Calibration)->TryGetBoolField(TEXT("spot_shadows"), bSpotShadows);
		(*Calibration)->TryGetBoolField(TEXT("sun_shadows"), bSunShadows);
	}

	EnableAllSources();
	RevertAllSources();

	// The save keys on the `.lights` line; this rig arrays by adoption order. Join through
	// SourceIndex — a saved index with no live source (a re-export changed the sidecar) is counted,
	// not guessed at.
	TMap<int32, int32> RowBySource;
	RowBySource.Reserve(LightSources.Num());
	for (int32 Row = 0; Row < LightSources.Num(); ++Row)
	{
		RowBySource.Add(LightSources[Row].SourceIndex, Row);
	}

	int32 NumDisabled = 0, NumOverridden = 0, NumUnmatched = 0;

	const TArray<TSharedPtr<FJsonValue>>* Edits = nullptr;
	if (Root->TryGetArrayField(TEXT("edits"), Edits))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Edits)
		{
			const TSharedPtr<FJsonObject>* Edit = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Edit))
			{
				continue;
			}
			int32 SourceIndex = INDEX_NONE;
			if (!(*Edit)->TryGetNumberField(TEXT("index"), SourceIndex))
			{
				continue;
			}
			const int32* Row = RowBySource.Find(SourceIndex);
			if (Row == nullptr)
			{
				++NumUnmatched;
				continue;
			}

			FLightSource& Source = LightSources[*Row];
			ULightComponent* Light = Source.Light.Get();
			if (Light == nullptr)
			{
				continue;
			}

			bool bDisabled = false;
			if ((*Edit)->TryGetBoolField(TEXT("disabled"), bDisabled) && bDisabled)
			{
				SetSourceDisabled(*Row, true);
				++NumDisabled;
			}

			bool bOverride = false;
			if (!(*Edit)->TryGetBoolField(TEXT("overridden"), bOverride) || !bOverride)
			{
				continue;
			}

			float Number = 0.f;
			FVector Vector;
			FLinearColor Color;
			if (ReadVector(**Edit, TEXT("pos_cm"), Vector)
				|| ReadVector(**Edit, TEXT("pos"), Vector))
			{
				Light->SetWorldLocation(Vector);
			}
			if (ReadVector(**Edit, TEXT("rot_deg"), Vector))
			{
				Light->SetWorldRotation(FRotator(Vector.X, Vector.Y, Vector.Z));
			}
			if (ReadNumber(**Edit, TEXT("intensity"), Number))
			{
				SetSourceIntensity(*Row, FMath::Max(Number, 0.f));
			}
			if (ReadColor(**Edit, TEXT("color"), Color))
			{
				Light->SetLightColor(Color);
			}
			if (ReadNumber(**Edit, TEXT("indirect_lighting_scale"), Number))
				Light->SetIndirectLightingIntensity(FMath::Clamp(Number, 0.f, 6.f));
			if (ReadNumber(**Edit, TEXT("volumetric_scatter"), Number))
				Light->SetVolumetricScatteringIntensity(FMath::Clamp(Number, 0.f, 4.f));
			if (ReadNumber(**Edit, TEXT("specular_scale"), Number))
			{
				Light->SpecularScale = FMath::Clamp(Number, 0.f, 1.f);
				Light->MarkRenderStateDirty();
			}
			bool bFlag = false;
			if ((*Edit)->TryGetBoolField(TEXT("cast_shadows"), bFlag))
				Light->SetCastShadows(bFlag);
			if ((*Edit)->TryGetBoolField(TEXT("cast_volumetric_shadow"), bFlag))
			{
				Light->bCastVolumetricShadow = bFlag;
				Light->MarkRenderStateDirty();
			}

			if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
			{
				if (ReadNumber(**Edit, TEXT("reach_cm"), Number))
					Local->SetAttenuationRadius(FMath::Clamp(Number, 1.f, 100000.f));
			}
			if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
			{
				if (ReadNumber(**Edit, TEXT("falloff_exponent"), Number))
					Point->SetLightFalloffExponent(FMath::Clamp(Number, 0.1f, 16.f));
				if (ReadNumber(**Edit, TEXT("source_radius_cm"), Number))
					Point->SetSourceRadius(FMath::Clamp(Number, 0.f, 10000.f));
				if (ReadNumber(**Edit, TEXT("soft_source_radius_cm"), Number))
					Point->SetSoftSourceRadius(FMath::Clamp(Number, 0.f, 10000.f));
				if (ReadNumber(**Edit, TEXT("source_length_cm"), Number))
					Point->SetSourceLength(FMath::Clamp(Number, 0.f, 10000.f));
			}
			if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
			{
				float Inner = Spot->InnerConeAngle;
				float Outer = Spot->OuterConeAngle;
				ReadNumber(**Edit, TEXT("inner_cone_deg"), Inner);
				ReadNumber(**Edit, TEXT("outer_cone_deg"), Outer);
				Outer = FMath::Clamp(Outer, 1.f, 80.f);
				Spot->SetOuterConeAngle(Outer);
				Spot->SetInnerConeAngle(FMath::Clamp(Inner, 0.f, Outer));
			}
			if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Light))
			{
				if (ReadNumber(**Edit, TEXT("source_angle_deg"), Number))
					Sun->SetLightSourceAngle(FMath::Clamp(Number, 0.f, 5.f));
				if (ReadNumber(**Edit, TEXT("soft_source_angle_deg"), Number))
					Sun->SetLightSourceSoftAngle(FMath::Clamp(Number, 0.f, 5.f));
			}

			Source.bOverridden = true;
			++NumOverridden;
		}
	}

	OutMessage = FString::Printf(TEXT("loaded %d off · %d override%s%s from %s"),
		NumDisabled, NumOverridden, NumOverridden == 1 ? TEXT("") : TEXT("s"),
		NumUnmatched > 0 ? *FString::Printf(TEXT(" · %d unmatched"), NumUnmatched) : TEXT(""),
		*Path);
	return true;
}


void UElysiumLightRig::SetLightsVisible(bool bShow)
{
	bLightsVisible = bShow;
	// Driven off LightSources, not the plain Lights array, because a hand-disabled source stays
	// off through a master flip — the disabled set is survey data, not a display state.
	for (const FLightSource& S : LightSources)
	{
		if (ULightComponent* Light = S.Light.Get())
		{
			Light->SetVisibility(bShow && !S.bDisabled);
		}
	}
}

void UElysiumLightRig::ApplyLiveTuning()
{
	for (FLightSource& S : LightSources)
	{
		// A hand-edited source is deliberately left alone: the sliders drive the rig, the
		// inspector drives the one light the user is working on.
		if (!S.bOverridden)
		{
			ApplyToSource(S);
		}
	}
}

ULightComponent* UElysiumLightRig::SourceLight(int32 Index) const
{
	return LightSources.IsValidIndex(Index) ? LightSources[Index].Light.Get() : nullptr;
}

bool UElysiumLightRig::IsSourceOverridden(int32 Index) const
{
	return LightSources.IsValidIndex(Index) && LightSources[Index].bOverridden;
}

void UElysiumLightRig::SetSourceOverridden(int32 Index, bool bOverride)
{
	if (LightSources.IsValidIndex(Index))
	{
		LightSources[Index].bOverridden = bOverride;
	}
}

void UElysiumLightRig::SetSourceIntensity(int32 Index, float Intensity)
{
	if (!LightSources.IsValidIndex(Index))
	{
		return;
	}
	FLightSource& S = LightSources[Index];
	S.bOverridden = true;
	// Styled sources scale BaseIntensity per frame, so writing it is what makes a hand-set value
	// the light's new base rather than something the next tick overwrites.
	S.BaseIntensity = Intensity;
	if (ULightComponent* Light = S.Light.Get())
	{
		Light->SetIntensity(Intensity);
	}
}

void UElysiumLightRig::RevertSource(int32 Index)
{
	if (!LightSources.IsValidIndex(Index))
	{
		return;
	}
	FLightSource& S = LightSources[Index];
	S.bOverridden = false;
	// Colour is fixed sidecar data that ApplyToSource does not own, so restore it here — an
	// inspector colour edit has to come back too, not just the calibrated numbers.
	if (ULightComponent* Light = S.Light.Get())
	{
		Light->SetLightColor(S.Color);
	}
	ApplyToSource(S);
}

void UElysiumLightRig::RevertAllSources()
{
	for (int32 Index = 0; Index < LightSources.Num(); ++Index)
	{
		RevertSource(Index);
	}
}

bool UElysiumLightRig::IsSourceDisabled(int32 Index) const
{
	return LightSources.IsValidIndex(Index) && LightSources[Index].bDisabled;
}

bool UElysiumLightRig::ShouldSourceBeLit(int32 Index) const
{
	return bLightsVisible && LightSources.IsValidIndex(Index) && !LightSources[Index].bDisabled;
}

void UElysiumLightRig::SetSourceDisabled(int32 Index, bool bDisable)
{
	if (!LightSources.IsValidIndex(Index))
	{
		return;
	}
	LightSources[Index].bDisabled = bDisable;
	if (ULightComponent* Light = LightSources[Index].Light.Get())
	{
		Light->SetVisibility(ShouldSourceBeLit(Index));
	}
}

void UElysiumLightRig::EnableAllSources()
{
	for (int32 Index = 0; Index < LightSources.Num(); ++Index)
	{
		SetSourceDisabled(Index, false);
	}
}

void UElysiumLightRig::SetNonSpotSourcesDisabled(bool bDisable)
{
	for (int32 Index = 0; Index < LightSources.Num(); ++Index)
	{
		if (LightSources[Index].Type != 2)
		{
			SetSourceDisabled(Index, bDisable);
		}
	}
}

void UElysiumLightRig::ApplyToSource(FLightSource& S)
{
	ULightComponent* Light = S.Light.Get();
	if (Light == nullptr)
	{
		return;
	}

	// A non-overridden source is the faithful row plus the current map-wide calibration. Keeping
	// every owned attribute here makes Revert complete and prevents an editor/bake value from
	// silently surviving after the runtime takes ownership.
	Light->SetWorldTransform(S.AuthoredTransform);
	Light->SetLightColor(S.Color);

	if (S.Type == 3)
	{
		// Sun/directional: lux scaled off the raw magnitude, no falloff/reach.
		S.BaseIntensity = FMath::Max(S.Mag * SunScaleLux, 0.01f);
	}
	else
	{
		float Reach = (S.RadiusCm > 1.f ? S.RadiusCm : FallbackRadiusCm) * RadiusScale;
		// A miniature light's radius is authored in miniature units, so it scales with the
		// geometry it lights or it reaches a 16th of what it did.
		if (S.bSky)
		{
			Reach = FMath::Max(Reach * SkyReachScale, MinSkyReachCm);
		}
		const float Ceiling = bExtendedRange ? ExtendedMaxBrightness : MaxBrightness;
		S.BaseIntensity = FMath::Min(S.Mag * PointSpotScale * S.FitMult, Ceiling);
		if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
		{
			Local->SetAttenuationRadius(Reach);
		}
	}

	if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
	{
		Point->SetUseInverseSquaredFalloff(false);
		Point->SetLightFalloffExponent(FalloffExponent);
		Point->SetSourceRadius(S.AuthoredSourceRadiusCm);
		Point->SetSoftSourceRadius(S.AuthoredSoftSourceRadiusCm);
		Point->SetSourceLength(S.AuthoredSourceLengthCm);
	}
	if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
	{
		const float Outer = ConeDegrees(S.StopDot2);
		Spot->SetOuterConeAngle(Outer);
		Spot->SetInnerConeAngle(FMath::Min(ConeDegrees(S.StopDot), Outer));
	}
	if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Light))
	{
		Sun->SetLightSourceAngle(FMath::Clamp(SunSourceAngleDegrees, 0.f, 5.f));
		Sun->SetLightSourceSoftAngle(FMath::Clamp(SunSoftSourceAngleDegrees, 0.f, 5.f));
	}

	const bool bCastShadows = S.Type == 0 ? false
		: S.Type == 2 ? bSpotShadows
		: S.Type == 3 ? bSunShadows
		: bPointShadows;
	Light->SetCastShadows(bCastShadows);
	Light->bCastVolumetricShadow = S.bAuthoredCastVolumetricShadow;
	Light->SetIndirectLightingIntensity(FMath::Clamp(IndirectLightingScale, 0.f, 6.f));
	Light->SetVolumetricScatteringIntensity(FMath::Clamp(VolumetricScatteringScale, 0.f, 4.f));
	Light->SpecularScale = SpecularScale;
	// Elysium's hundreds of movable local lights depend on fixed-cost RT MegaLights. This is a
	// renderer contract, not an art override; never let baked defaults or a prior editor setting
	// send one source through per-light VSM shadowing.
	if (S.Type != 3)
	{
		Light->bAllowMegaLights = true;
		Light->MegaLightsShadowMethod = EMegaLightsShadowMethod::RayTracing;
	}
	Light->MarkRenderStateDirty();
	// Styled lights get their per-frame flicker off this new base next tick; set the base now
	// so unanimated lights update immediately (and animated ones don't stall on a paused clock).
	Light->SetIntensity(S.BaseIntensity);
}

void UElysiumLightRig::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StyleTime += DeltaTime;
	for (const FLightSource& S : LightSources)
	{
		// An overridden source holds the intensity the inspector set, flicker included: the whole
		// point of the override is that nothing writes over a hand-set value.
		if (S.Style >= 1 && !S.bOverridden && S.Light.IsValid())
		{
			S.Light->SetIntensity(S.BaseIntensity * StyleIntensity(S.Style, StyleTime));
		}
	}
}
