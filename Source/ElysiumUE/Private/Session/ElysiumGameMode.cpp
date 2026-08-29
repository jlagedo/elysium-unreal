#include "ElysiumGameMode.h"

#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumHUD.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerController.h"

#include "Engine/GameInstance.h"

AElysiumGameMode::AElysiumGameMode()
{
	DefaultPawnClass = AElysiumPawn::StaticClass();
	HUDClass = AElysiumHUD::StaticClass();
	PlayerControllerClass = AElysiumPlayerController::StaticClass();
}

UClass* AElysiumGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const UElysiumMapSubsystem* Maps =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	if (Maps && Maps->IsMenuBackdrop())
	{
		return nullptr;
	}
	// The box hull is a recovered requirement, not a preference (`docs/vtmb/source_movement.md` § StepMove):
	// `StepMove` depends on a flat bottom, which a capsule does not have.
	return AElysiumPawn::StaticClass();
}

void AElysiumGameMode::BeginPlay()
{
	Super::BeginPlay();

	// The boot decision, pending-map spawn and backdrop camera belong to the application, not
	// to a per-world object. This is the one call left.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>())
		{
			Flow->NotifyWorldReady(this);
		}
	}
}
