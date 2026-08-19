#pragma once

#include "CoreMinimal.h"

class APawn;
class APlayerController;
class IElysiumPlayerBody;
class UElysiumMovementComponent;
class UWorld;

// Internals shared by the sibling translation units that together implement
// FElysiumGreenRoomRun (Debug/ElysiumGreenRoomRun.h). Private to the green room.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumGreenRoom, Log, All);

namespace ElysiumGreenRoom
{
	// Everything drive mode needs off the possessed body, resolved together so a half-built session
	// is one refusal rather than five null checks scattered through the mode change.
	struct FDriveRefs
	{
		APlayerController* PC = nullptr;
		APawn* Pawn = nullptr;
		IElysiumPlayerBody* Body = nullptr;
		UElysiumMovementComponent* Move = nullptr;

		explicit operator bool() const { return PC && Pawn && Body && Move; }
	};

	FDriveRefs ResolveDriveBody(UWorld* World);
}
