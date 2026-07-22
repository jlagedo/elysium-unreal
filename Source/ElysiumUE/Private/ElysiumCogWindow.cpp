#include "ElysiumCogWindow.h"

#if ENABLE_COG

#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

FElysiumEntityHandle FElysiumCogWindow::Selection;

AElysiumMapActor* FElysiumCogWindow::GetMapActor() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (const UElysiumMapSubsystem* Maps = GameInstance ? GameInstance->GetSubsystem<UElysiumMapSubsystem>() : nullptr)
	{
		return Maps->GetCurrentMap();
	}
	return nullptr;
}

FElysiumEntityWorld* FElysiumCogWindow::GetEntityWorld() const
{
	const AElysiumMapActor* Map = GetMapActor();
	return Map ? Map->GetEntityWorld() : nullptr;
}

UElysiumGameStateSubsystem* FElysiumCogWindow::GetGameState() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UElysiumGameStateSubsystem>() : nullptr;
}

#endif // ENABLE_COG
