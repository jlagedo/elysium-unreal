#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ElysiumCogSubsystem.generated.h"

// Owns Cog's window configuration for Elysium. On world init it depends-in the
// UCogSubsystem and registers the built-in CogEngine debug windows (Inspector,
// Selection, Stats, Output Log, Console, Collision, ...). Cog itself (menu, input,
// ImGui render) is driven by UCogSubsystem; this class only decides which windows
// appear. Entirely compiled out of Shipping (ENABLE_COG = !UE_BUILD_SHIPPING).
//
// Custom Elysium windows (Maps/Lights/Entities) are added here in roadmap 2.1; this
// spike (0.5) registers only the stock engine windows to prove Cog builds on 5.8.
UCLASS()
class UElysiumCogSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void PostInitialize() override;

private:
	UPROPERTY()
	TWeakObjectPtr<USubsystem> CogSubsystem;
};
