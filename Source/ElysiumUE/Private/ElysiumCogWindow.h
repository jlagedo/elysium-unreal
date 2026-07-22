#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "CogWindow.h"
#include "ElysiumEntityHandle.h"

class AElysiumMapActor;
class FElysiumEntityWorld;
class UElysiumGameStateSubsystem;

// Shared base for Elysium's custom Cog windows. Cog's stock inspector is reflection-driven, so
// it only sees UObjects — but Track B's entities, event queue, and clock are plain C++, invisible
// to it. Every Elysium window therefore reads the runtime's own structures directly (immediate-mode
// ImGui, no reflection); this base hands them the few live handles they need, resolved off the
// window's world. Derived windows override RenderContent() and read through these. The whole class
// is compiled out of Shipping (ENABLE_COG = !UE_BUILD_SHIPPING).
class FElysiumCogWindow : public FCogWindow
{
	typedef FCogWindow Super;

protected:
	// The live map actor for the current world, or null between/without a loaded map.
	AElysiumMapActor* GetMapActor() const;
	// The Track-B entity substrate for the current map, or null if the map has none loaded.
	FElysiumEntityWorld* GetEntityWorld() const;
	// The persistent game-state subsystem (the `G` store, quests, and the game clock), or null.
	UElysiumGameStateSubsystem* GetGameState() const;

	// Shared debug selection across the Elysium windows: the browser sets it, the inspector reads
	// it. Debug-only state (one browser + one inspector instance), so a plain static is enough. A
	// stale handle self-clears because Resolve() rejects a mismatched epoch after a map reload.
	static const FElysiumEntityHandle& GetSelection() { return Selection; }
	static void SetSelection(const FElysiumEntityHandle& InHandle) { Selection = InHandle; }

private:
	static FElysiumEntityHandle Selection;
};

#endif // ENABLE_COG
