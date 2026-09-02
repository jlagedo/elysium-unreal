// env_sprite -- glow coronas, light shafts, candle flames, cop flashers, lightning (R6.1,
// `docs/architecture/seam_map_map.md` -> "Sprites (R6.1)").
//
// The billboard is the bake's (`AElysiumSpriteActor`, tagged with this entity's index); the
// entity owns nothing but its on/off state, CSprite's (vampire.dll 1042e550 Spawn, 1042ef40
// TurnOff = `m_fEffects |= EF_NODRAW`, 1042ef70 TurnOn, 1042f080..1042f130 the inputs):
// `Spawn` turns an unnamed or Start-On (spawnflag 1) sprite on and every other one off;
// HideSprite / TurnOff clear the draw, ShowSprite / TurnOn restore it, ToggleSprite flips it.
// ScriptHide / Kill hide through the base chain and ScriptUnhide restores the last `bOn`. Every
// change publishes `bOn && !IsInert()` through `IElysiumEmbodiment::SetBakedSpriteVisible`; a map
// off `MapsOnV2Models` has no sprite actor and the write lands nowhere.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"

namespace
{
	constexpr int32 SF_SPRITE_START_ON = 0x1;
}

class FElysiumEnvSprite final : public FElysiumEntity
{
public:
	bool bOn = true;

	virtual void Spawn() override
	{
		bOn = TargetName.IsEmpty() || (SpawnFlags & SF_SPRITE_START_ON) != 0;
		Publish();
	}

	void TurnOn()  { bOn = true;  Publish(); }
	void TurnOff() { bOn = false; Publish(); }
	void Toggle()  { bOn = !bOn;  Publish(); }

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish();
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << bOn;
		if (Ar.IsLoading())
		{
			Publish();
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Sprite"), bOn ? TEXT("on") : TEXT("off"));
		Out.Emplace(TEXT("Drawn"), IsVisible() ? TEXT("yes") : TEXT("no"));
	}

	bool IsVisible() const { return bOn && !IsInert(); }

private:
	void Publish() const
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->SetBakedSpriteVisible(Handle.Index, IsVisible());
		}
	}
};

static TUniquePtr<FElysiumEntity> MakeEnvSprite()
{
	return MakeUnique<FElysiumEnvSprite>();
}

static FElysiumClassRegistrar GRegEnvSprite(
	TEXT("env_sprite"), ElysiumBaseClassName(), &MakeEnvSprite,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("HideSprite"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOff(); });
		D.Input(TEXT("ShowSprite"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOn(); });
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).TurnOff(); });
		D.Input(TEXT("ToggleSprite"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSprite&>(E).Toggle(); });
	});
