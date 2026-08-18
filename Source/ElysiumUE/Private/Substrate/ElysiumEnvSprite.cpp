// env_sprite — glow coronas, window lights, lightning flashes, cop flashers.
//
// Presentation is Lumen; these entities are not drawn. They keep their `.ents` identity so the
// maps' visibility wires resolve. HideSprite / ShowSprite / TurnOn / TurnOff are accepted and do
// nothing. ScriptHide / ScriptUnhide / Kill stay on the base chain. An input this class does not
// name still reports as unimplemented.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"

class FElysiumEnvSprite final : public FElysiumEntity
{
};

static void IgnoreSpriteVisibility(FElysiumEntity&, const FElysiumInputArgs&)
{
}

static TUniquePtr<FElysiumEntity> MakeEnvSprite()
{
	return MakeUnique<FElysiumEnvSprite>();
}

static FElysiumClassRegistrar GRegEnvSprite(
	TEXT("env_sprite"), ElysiumBaseClassName(), &MakeEnvSprite,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("HideSprite"), &IgnoreSpriteVisibility);
		D.Input(TEXT("ShowSprite"), &IgnoreSpriteVisibility);
		D.Input(TEXT("TurnOn"), &IgnoreSpriteVisibility);
		D.Input(TEXT("TurnOff"), &IgnoreSpriteVisibility);
	});
