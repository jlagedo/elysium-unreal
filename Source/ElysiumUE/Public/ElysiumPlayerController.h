#pragma once

#include "CoreMinimal.h"
#include "ElysiumCommands.h"
#include "ElysiumUserCmd.h"
#include "GameFramework/PlayerController.h"

#include "ElysiumPlayerController.generated.h"

class AElysiumMapActor;
class FElysiumEntityWorld;
class UElysiumInputRouter;

// Player controller for the boot game mode. It hosts `UElysiumCheatManager` (CheatClass) and
// `UElysiumInputRouter` (11.6), and it **binds no key of its own**: every key in VtMB's default set
// is installed by the router and fires a named verb through the command bus.
//
// What it does own is the implementation of world verbs that need a live map — `+attack` (which
// dismisses a sign panel), `noclip` and `god` — plus the FElysiumUserCmd edge handoff for `+use`.
// Those verbs are registered into
// `FElysiumCommands` while the controller is alive and released when it goes away, so a verb has an
// implementation exactly when there is a world to run it against.
UCLASS()
class AElysiumPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AElysiumPlayerController();

	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	// Step 1 of the frame: the bindings for this frame have run by the time Super returns, so the
	// user command is built here and lands before the substrate ticks (11.1).
	virtual void PlayerTick(float DeltaTime) override;

	UElysiumInputRouter* GetInputRouter() const { return Router; }

private:
	void RegisterCommands();
	void UnregisterCommands();

	// The current map's actor, cached per world so a verb is one weak-pointer check rather than a
	// GameInstance -> map subsystem -> map actor walk.
	AElysiumMapActor* CurrentMap() const;
	FElysiumEntityWorld* CurrentEntityWorld() const;
	mutable TWeakObjectPtr<AElysiumMapActor> CachedMap;

	UPROPERTY(Transient)
	TObjectPtr<UElysiumInputRouter> Router;

	TArray<FElysiumCommandBinding> Bindings;
	FElysiumUserCmd PreviousCmd;
};
