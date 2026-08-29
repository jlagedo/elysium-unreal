#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// A live, read-only summary of the loaded VtMB map and the entity substrate — the
// map/surface/light/prop counts the Canvas HUD shows, plus the event-queue depth, the I/O
// ring-buffer fill, touch/dead-wire tallies, and the game clock. Reads the runtime's own
// structures directly (immediate-mode ImGui, no reflection).
class FElysiumCogWindow_Status : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
};

#endif // ENABLE_COG
