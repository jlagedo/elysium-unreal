#include "ElysiumLightingSettings.h"

#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "Visual/ElysiumLightRig.h"
#include "Visual/ElysiumMapVisuals.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UElysiumLightingSettings::UElysiumLightingSettings()
{
	CategoryName = TEXT("Elysium");
	SectionName = TEXT("Lighting");
}

void UElysiumLightingSettings::PushToWorlds() const
{
	if (!GEngine)
	{
		return;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		const UWorld* World = Context.World();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UElysiumMapSubsystem* Maps = GameInstance ? GameInstance->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
		const AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
		const UElysiumMapVisuals* Visuals = Map ? Map->GetVisuals() : nullptr;
		UElysiumLightRig* Rig = Visuals ? Visuals->GetLightRig() : nullptr;
		if (Rig == nullptr)
		{
			continue;
		}
		Rig->ApplySettings(*this);
		Rig->ApplyLiveTuning();
	}
}

#if WITH_EDITOR
void UElysiumLightingSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// `EPropertyChangeType::Type` is a plain bitmask (UnrealType.h): an interactive drag can arrive
	// combined with another flag, so nothing pushes until the drag's terminal ValueSet lands with no
	// Interactive bit set at all -- unlike UElysiumSurfaceSettings, which follows a drag live because
	// it only has to touch a parameter collection.
	if ((PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) != 0)
	{
		return;
	}
	PushToWorlds();
}
#endif
