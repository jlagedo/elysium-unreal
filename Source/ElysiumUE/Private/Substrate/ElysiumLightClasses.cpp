// light / light_spot / light_dynamic -- the light entities.
//
// `light` and `light_spot` are CLight (vampire.dll 10130460 Spawn, 10130610 on, 10130690 off,
// 101306f0 toggle, 10130780 SetPattern, 10130800 FadeToPattern, 101308d0 FadeThink). The entity
// owns no light of its own: VRAD baked every named light's sources into lump 15 under the style
// it assigned (>= 32), and the bake tagged those sources `elysium.style=<s>`, so the whole of the
// leaf's job is `engine->LightStyle(style, pattern)` -- one write per input, by style, through
// `IElysiumEmbodiment::SetLightStylePattern` to the rig's clock. A style below 32 is an unnamed
// (or unswitchable) light and takes no input, exactly as retail's `if (m_iStyle >= 32)` gates.
//
// `light_dynamic` is CDynamicLight (100568a0 KeyValue, 10056a90 Spawn, 10056a00 TurnOn): the one
// light with no lump-15 row. It stands a runtime point/spot through the legacy `ApplyToSource`
// derivation (`BuildDynamicLight`) and toggles it by visibility. Its magnitude convention is the
// doc's; nothing here is a knob.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

#include "Components/LightComponent.h"

namespace
{
	constexpr int32 SF_LIGHT_START_OFF = 0x1;
	// Retail floors `fade_time` against a cvar the corpus does not name; every shipped light
	// authors 0.05, so this floor never bites on shipped data.
	constexpr float MinFadeSeconds = 0.05f;
	// VRAD's first entity-switched style; below it a light is unnamed or a static style.
	constexpr int32 FirstSwitchedStyle = 32;
}

class FElysiumLight final : public FElysiumEntity
{
public:
	int32 Style = 0;          // `style`, VRAD's assignment
	FString Pattern;          // `pattern` -- the current pattern, retail's m_iszPattern
	float FadeTime = 0.f;     // `fade_time`, seconds per FadeThink step

	virtual void Spawn() override
	{
		FadeTime = FMath::Max(FadeTime, MinFadeSeconds);
		if (Style < FirstSwitchedStyle)
		{
			return;   // retail removes an unnamed light; here it stays an inert record
		}
		if (SpawnFlags & SF_LIGHT_START_OFF)
		{
			Pattern = TEXT("a");
			Write(Pattern);
		}
		else if (!Pattern.IsEmpty())
		{
			Write(Pattern);
		}
		else
		{
			Pattern = TEXT("m");
			Write(Pattern);
		}
	}

	// CLight's on: the pattern only when it is at least two letters and does not start dark;
	// otherwise full. Clears START_OFF (the on/off memory Toggle reads).
	void TurnOn()
	{
		if (Style < FirstSwitchedStyle)
		{
			return;
		}
		const bool bUsePattern = Pattern.Len() > 1 && Pattern[0] != TEXT('a');
		Write(bUsePattern ? Pattern : FString(TEXT("m")));
		SpawnFlags &= ~SF_LIGHT_START_OFF;
	}

	void TurnOff()
	{
		if (Style < FirstSwitchedStyle)
		{
			return;
		}
		Write(TEXT("a"));
		SpawnFlags |= SF_LIGHT_START_OFF;
	}

	void Toggle()
	{
		if (SpawnFlags & SF_LIGHT_START_OFF) { TurnOn(); } else { TurnOff(); }
	}

	void SetPattern(const FString& NewPattern)
	{
		if (Style < FirstSwitchedStyle)
		{
			return;
		}
		Pattern = NewPattern;
		Write(Pattern);
		SpawnFlags &= ~SF_LIGHT_START_OFF;
	}

	void FadeToPattern(const FString& TargetPattern)
	{
		if (Style < FirstSwitchedStyle)
		{
			return;
		}
		CurrentFade = Pattern.IsEmpty() ? TEXT('\0') : Pattern[0];
		TargetFade = TargetPattern.IsEmpty() ? TEXT('\0') : TargetPattern[0];
		Pattern = TargetPattern;
		bFading = true;
		NextThink = World ? static_cast<float>(World->NowSeconds()) : 0.f;
		SpawnFlags &= ~SF_LIGHT_START_OFF;
	}

	// FadeThink: one letter towards the target per step; arriving writes the whole pattern.
	virtual void Think() override
	{
		if (!bFading)
		{
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		if (CurrentFade < TargetFade) { ++CurrentFade; }
		else if (CurrentFade > TargetFade) { --CurrentFade; }
		if (CurrentFade == TargetFade)
		{
			Write(Pattern);
			bFading = false;
			NextThink = ELYSIUM_NEVER_THINK;
			return;
		}
		Write(FString::Chr(CurrentFade));
		NextThink = static_cast<float>((World ? World->NowSeconds() : 0.0) + FadeTime);
	}

	// Retail's ScriptHide/Kill turn the light off first and ScriptUnhide turns it on; the base
	// inputs then run as usual.
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		if (IsHidden()) { TurnOff(); } else { TurnOn(); }
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << Pattern << bFading << CurrentFade << TargetFade;
		if (Ar.IsLoading() && Style >= FirstSwitchedStyle)
		{
			// The rig's table is per map load; the saved state re-publishes what it last wrote.
			Write((SpawnFlags & SF_LIGHT_START_OFF) ? FString(TEXT("a")) : Pattern);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Style"), FString::Printf(TEXT("%d%s"), Style,
			Style >= FirstSwitchedStyle ? TEXT(" (switched)") : TEXT("")));
		Out.Emplace(TEXT("Pattern"), Pattern);
		Out.Emplace(TEXT("State"), (SpawnFlags & SF_LIGHT_START_OFF) ? TEXT("off") : TEXT("on"));
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Out.Emplace(TEXT("Rig pattern"), Embodiment->LightStylePattern(Style));
		}
	}

private:
	void Write(const FString& Text) const
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->SetLightStylePattern(Style, Text);
		}
	}

	bool bFading = false;
	TCHAR CurrentFade = 0;
	TCHAR TargetFade = 0;
};

class FElysiumLightDynamic final : public FElysiumEntity
{
public:
	FString LightColor;        // `_light` "r g b [a]" -- the render colour; the fourth term is not read
	int32 Brightness = 0;      // `brightness`, the dlight's ColorRGBExp32 exponent
	float Distance = 0.f;      // `distance`, Source inches
	float InnerCone = 0.f;     // `_inner_cone`, degrees
	float Cone = 0.f;          // `_cone`, degrees; 0 = a point light
	float SpotRadius = 0.f;    // `spotlight_radius`, carried
	float Pitch = 0.f;         // `pitch`, degrees; -90 = straight down
	int32 Style = 0;
	bool bOn = true;           // m_On: on at spawn

	virtual void Spawn() override
	{
		bOn = true;
		Stand();
	}

	void TurnOn()  { bOn = true;  Publish(); }
	void TurnOff() { bOn = false; Publish(); }
	void Toggle()  { bOn = !bOn;  Publish(); }

	virtual USceneComponent* GetAttachChild() const override
	{
		return Light.IsValid() ? static_cast<USceneComponent*>(Light.Get()) : FElysiumEntity::GetAttachChild();
	}

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish();
	}

	virtual void OnRuntimeTransformChanged() override
	{
		FElysiumEntity::OnRuntimeTransformChanged();
		if (Light.IsValid())
		{
			Light->SetWorldLocation(Origin);
		}
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << bOn;
		if (Ar.IsLoading())
		{
			Publish();
		}
	}

	virtual ~FElysiumLightDynamic() override
	{
		if (Light.IsValid())
		{
			if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
			{
				Embodiment->DestroyDynamicLight(Light.Get());
			}
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Light"), FString::Printf(TEXT("%s, %s"), Cone > 0.f ? TEXT("spot") : TEXT("point"),
			bOn ? TEXT("on") : TEXT("off")));
		Out.Emplace(TEXT("Colour / brightness"), FString::Printf(TEXT("%s / %d"), *LightColor, Brightness));
		Out.Emplace(TEXT("Reach"), FString::Printf(TEXT("%.0f in, cone %.0f/%.0f"), Distance, InnerCone, Cone));
	}

	// The spec the doc states: colour normalised, reach in cm, cosines of the cones, a forward
	// from (pitch, yaw) in the reflected frame, and the magnitude convention
	// `pow(c/255, 2.2) x 100 x 2^brightness x 100 / 2.55`.
	FElysiumDynamicLightSpec MakeSpec() const
	{
		FElysiumDynamicLightSpec Spec;
		Spec.LocationCm = Origin;
		float Rgb[3] = { 255.f, 255.f, 255.f };
		TArray<FString> Parts;
		LightColor.ParseIntoArray(Parts, TEXT(" "), true);
		for (int32 I = 0; I < 3 && I < Parts.Num(); ++I)
		{
			Rgb[I] = FMath::Clamp(FCString::Atof(*Parts[I]), 0.f, 255.f);
		}
		const float Max = FMath::Max3(Rgb[0], Rgb[1], Rgb[2]);
		Spec.Color = Max > 0.f ? FLinearColor(Rgb[0] / Max, Rgb[1] / Max, Rgb[2] / Max) : FLinearColor::White;
		const float Scale = 100.f * FMath::Pow(2.f, static_cast<float>(FMath::Clamp(Brightness, 0, 16)));
		Spec.Mag = Max > 0.f ? FMath::Pow(Max / 255.f, 2.2f) * Scale * (100.f / 2.55f) : 0.f;
		Spec.RadiusCm = FMath::Max(Distance, 0.f) * 2.54f;
		Spec.Style = Style;
		Spec.bSpot = Cone > 0.f;
		if (Spec.bSpot)
		{
			Spec.StopDot2 = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Cone, 0.f, 89.f)));
			Spec.StopDot = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(InnerCone, 0.f, Cone)));
		}
		// CDynamicLight negates `pitch` into its angles, and AngleVectors' forward.z is -sin(pitch):
		// the FGD's "-90 = straight down". Yaw is the `angles` triple's; Y flips for the frame.
		const float P = FMath::DegreesToRadians(Pitch);
		const float Y = FMath::DegreesToRadians(static_cast<float>(Angles.Y));
		Spec.Forward = FVector(FMath::Cos(P) * FMath::Cos(Y), -FMath::Cos(P) * FMath::Sin(Y), FMath::Sin(P))
			.GetSafeNormal(UE_SMALL_NUMBER, FVector(1.f, 0.f, 0.f));
		return Spec;
	}

private:
	void Stand()
	{
		IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		if (!Embodiment || Light.IsValid())
		{
			return;
		}
		Light = Embodiment->BuildDynamicLight(MakeSpec(), nullptr);
		Publish();
	}

	void Publish() const
	{
		if (Light.IsValid())
		{
			Light->SetVisibility(bOn && !IsInert());
		}
	}

	// Weak: the map actor owns the component and may tear it down before the substrate does.
	TWeakObjectPtr<ULightComponent> Light;
};

static TUniquePtr<FElysiumEntity> MakeLight() { return MakeUnique<FElysiumLight>(); }
static TUniquePtr<FElysiumEntity> MakeLightDynamic() { return MakeUnique<FElysiumLightDynamic>(); }

static void BuildLightDesc(FElysiumClassDesc& D)
{
	D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLight&>(E).TurnOn(); });
	D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLight&>(E).TurnOff(); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLight&>(E).Toggle(); });
	D.Input(TEXT("SetPattern"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLight&>(E).SetPattern(A.Param.ToString()); });
	D.Input(TEXT("FadeToPattern"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumLight&>(E).FadeToPattern(A.Param.ToString()); });
	ElysiumAddClassField(D, TEXT("style"), &FElysiumLight::Style, EElysiumField::None);
	ElysiumAddClassField(D, TEXT("pattern"), &FElysiumLight::Pattern, EElysiumField::None);
	ElysiumAddClassField(D, TEXT("fade_time"), &FElysiumLight::FadeTime, EElysiumField::None);
}

static FElysiumClassRegistrar GRegLight(TEXT("light"), ElysiumBaseClassName(), &MakeLight, &BuildLightDesc);
static FElysiumClassRegistrar GRegLightSpot(TEXT("light_spot"), ElysiumBaseClassName(), &MakeLight, &BuildLightDesc);

static FElysiumClassRegistrar GRegLightDynamic(
	TEXT("light_dynamic"), ElysiumBaseClassName(), &MakeLightDynamic,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLightDynamic&>(E).TurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLightDynamic&>(E).TurnOff(); });
		D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumLightDynamic&>(E).Toggle(); });
		ElysiumAddClassField(D, TEXT("_light"), &FElysiumLightDynamic::LightColor, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("brightness"), &FElysiumLightDynamic::Brightness, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("distance"), &FElysiumLightDynamic::Distance, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("_inner_cone"), &FElysiumLightDynamic::InnerCone, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("_cone"), &FElysiumLightDynamic::Cone, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("spotlight_radius"), &FElysiumLightDynamic::SpotRadius, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("pitch"), &FElysiumLightDynamic::Pitch, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("style"), &FElysiumLightDynamic::Style, EElysiumField::None);
	});
