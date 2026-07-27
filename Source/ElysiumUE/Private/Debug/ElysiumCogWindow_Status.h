#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// The first custom Elysium Cog window: a live, read-only summary of the loaded VtMB map and the
// Track-B entity substrate — the map/surface/light/prop counts the Canvas HUD shows, plus the
// event-queue depth, the I/O ring-buffer fill, touch/dead-wire tallies, and the game clock. It
// proves the reflection-free custom-window path end to end; 2.2's entity browser/inspector and
// 2.5's Maps/Lights windows derive from FElysiumCogWindow the same way.
class FElysiumCogWindow_Status : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
};

#endif // ENABLE_COG
