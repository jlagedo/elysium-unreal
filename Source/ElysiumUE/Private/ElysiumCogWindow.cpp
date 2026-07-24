#include "ElysiumCogWindow.h"

#if ENABLE_COG

#include "ElysiumAudioSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumNpcSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

FElysiumEntityHandle FElysiumCogWindow::Selection;
FElysiumPickResult FElysiumCogWindow::Pick;

UElysiumMapSubsystem* FElysiumCogWindow::GetMapSubsystem() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
}

AElysiumMapActor* FElysiumCogWindow::GetMapActor() const
{
	const UElysiumMapSubsystem* Maps = GetMapSubsystem();
	return Maps ? Maps->GetCurrentMap() : nullptr;
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

UElysiumAudioSubsystem* FElysiumCogWindow::GetAudioSubsystem() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UElysiumAudioSubsystem>() : nullptr;
}

UElysiumNpcSubsystem* FElysiumCogWindow::GetNpcSubsystem() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UElysiumNpcSubsystem>() : nullptr;
}

#endif // ENABLE_COG
