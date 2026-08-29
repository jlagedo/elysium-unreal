#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "ElysiumCogSubsystem.generated.h"

class FElysiumCogWindow_GreenRoom;
class IConsoleObject;

// Owns Cog's window configuration for Elysium. On world init it depends-in the
// UCogSubsystem and registers the built-in CogEngine debug windows (Inspector,
// Selection, Stats, Output Log, Console, Collision, ...). Cog itself (menu, input,
// ImGui render) is driven by UCogSubsystem; this class only decides which windows
// appear. Entirely compiled out of Shipping (ENABLE_COG = !UE_BUILD_SHIPPING).
//
// Custom Elysium windows (FElysiumCogWindow subclasses) are registered here alongside the
// stock ones.
UCLASS()
class UElysiumCogSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void PostInitialize() override;
	virtual void Deinitialize() override;

private:
	UPROPERTY()
	TWeakObjectPtr<USubsystem> CogSubsystem;

	// Startup ticker (and its elapsed clock) that keeps Cog's windows closed for the
	// first second so the game boots dormant — Cog stays available via F1. ENABLE_COG only.
	FTSTicker::FDelegateHandle StartupHideTicker;
	float StartupHideElapsed = 0.f;

	// The green-room lab window, kept because `elysium.gr` opens it by name from the console
	// rather than through the menu. Owned by Cog once added; this is a borrowed pointer.
	FElysiumCogWindow_GreenRoom* GreenRoomWindow = nullptr;

	TArray<IConsoleObject*> ConsoleObjects;
};
