#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ElysiumHUD.generated.h"

class AElysiumMapActor;
class IConsoleObject;

// Debug overlay, toggled by the `elysium.debug` console command (' or ` opens the console)
// or F1. Hidden on a clean load. Top-left panel: map + surface counts, the view point in
// metres and Source units, yaw, movement/skybox state, and a crosshair pick reporting the
// mesh under the aim. A big FPS meter sits top-right; a centre crosshair draws while on.
UCLASS()
class AElysiumHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DrawHUD() override;

	void ToggleDebug() { bShowDebug = !bShowDebug; }
	bool IsDebugVisible() const { return bShowDebug; }

private:
	bool bShowDebug = false;
	float SmoothedFPS = 0.f;
	IConsoleObject* DebugCmd = nullptr;

	AElysiumMapActor* ResolveMapActor() const;
};
