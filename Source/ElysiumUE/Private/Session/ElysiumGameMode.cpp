#include "ElysiumGameMode.h"

#include "ElysiumCapsulePawn.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumHUD.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerController.h"

#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

// The A/B over the player's mover (11.6). 1 = the faithful body — an `APawn` with Source's box hull
// and `UElysiumMovementComponent`; 0 = the `ACharacter` capsule over `UCharacterMovementComponent`
// that preceded it. Read at pawn spawn, so it takes effect on the next map load or `elysium.reload`.
static TAutoConsoleVariable<int32> CVarSourceMovement(
	TEXT("elysium.SourceMovement"), 1,
	TEXT("1 = box hull + UElysiumMovementComponent (default); 0 = the capsule ACharacter baseline."),
	ECVF_Default);

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
	// The box hull is a recovered requirement, not a preference (`docs/vtmb/source_movement.md` § StepMove);
	// the capsule stays reachable so a feel change has something known-good to be compared against.
	return CVarSourceMovement.GetValueOnGameThread() != 0
		? AElysiumPawn::StaticClass()
		: AElysiumCapsulePawn::StaticClass();
}

void AElysiumGameMode::BeginPlay()
{
	Super::BeginPlay();

	// The whole of what used to live here — the boot decision, the pending-map spawn, the backdrop
	// camera — belongs to the application, not to a per-world object, and moved to
	// UElysiumGameFlowSubsystem with roadmap 11.3. This is the one call left.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElysiumGameFlowSubsystem* Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>())
		{
			Flow->NotifyWorldReady(this);
		}
	}
}
