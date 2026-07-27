#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// P2.5 Maps window: the F1-first surface for map lifecycle. Lists every exported VtMB map with a
// per-row Travel button (current map highlighted), a Reload button (the export->reload hot loop,
// same as elysium.reload), and the current map's per-phase load timings + surface/light/prop
// counts. Subsumes the Canvas HUD's map panel and the removed `elysium.debug` overlay's player
// readout (position, mode, FPS — see RenderPlayer).
class FElysiumCogWindow_Maps : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	// P4.6 transitions: this map's trigger_changelevel destinations (each with a fire button) and
	// info_landmark anchors, plus the pending/entry landmark travel state.
	void RenderTransitions();
	// Player pose (metres + Source units + yaw), movement/skybox/light state, and FPS — the info
	// the old always-on `elysium.debug` HUD overlay used to show.
	void RenderPlayer();

	float SmoothedFPS = 0.f;
};

#endif // ENABLE_COG
