#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ElysiumHUD.generated.h"

class AElysiumMapActor;
class IConsoleObject;

// World-hosted developer commands. Every player-facing HUD and modal surface, including game_sign,
// is owned by UElysiumPlayerUISubsystem's CommonUI root; this actor no longer renders UI on Canvas.
UCLASS()
class AElysiumHUD : public AHUD
{
	GENERATED_BODY()

public:
	AElysiumHUD();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	IConsoleObject* LightsCmd = nullptr;
	IConsoleObject* PropsCmd = nullptr;
	IConsoleObject* LightProbeCmd = nullptr;

	// For the dev console verbs above only — never for anything drawn.
	AElysiumMapActor* ResolveMapActor() const;

};
