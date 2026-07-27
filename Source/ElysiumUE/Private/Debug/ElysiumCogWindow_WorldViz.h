#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// P2.4 world-visualization control panel: the F1-first surface for the map-wide debug layers that
// UElysiumEntityDebugSubsystem renders every frame — entity origin gizmos (off / visible / all),
// wireframe trigger hulls, and fading I/O beam arrows. Every control here flips the same VizSettings
// the elysium.showtriggers / ent_gizmos / ent_beams verbs flip, and the always-running subsystem
// tick draws them whether or not this window is open (so a layer left on stays on while you play).
class FElysiumCogWindow_WorldViz : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
};

#endif // ENABLE_COG
