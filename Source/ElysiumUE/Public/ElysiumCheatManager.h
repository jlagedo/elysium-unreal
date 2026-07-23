#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "ElysiumCheatManager.generated.h"

// Player-centric dev cheats (debug-tooling.md Layer 0). A UCheatManager subclass is the natural
// home: UFUNCTION(exec) members auto-register as console commands with autocomplete, and the whole
// object only exists in non-Shipping / cheats-enabled builds. Subclassing also carries the stock
// UCheatManager execs (God, Teleport, Fly, Ghost, Walk, Slomo, ...) for free.
//
// Elysium-specific cheats live here; map lifecycle stays on the elysium.* console verbs and the
// Maps/Lights Cog windows. Hosted by AElysiumPlayerController (its CheatClass).
UCLASS()
class UElysiumCheatManager : public UCheatManager
{
	GENERATED_BODY()

public:
	// Toggle noclip on the Elysium pawn (fly through geometry) - the console/autocomplete twin of
	// the pawn's V key. The stock UCheatManager Fly/Ghost don't drive this pawn's custom flight.
	UFUNCTION(exec)
	void Noclip();

	// Teleport the pawn to a VtMB Source-unit position (inches, right-handed) - the coordinate the
	// debug overlays and sidecars speak. Pairs with the HUD's `src` readout for jump-to-here.
	UFUNCTION(exec)
	void ElysiumTeleport(float SrcX, float SrcY, float SrcZ);
};
