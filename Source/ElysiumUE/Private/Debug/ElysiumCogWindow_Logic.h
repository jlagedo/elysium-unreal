#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// At-a-glance board of the tutorial-logic entities — math_counter values, logic_timer state,
// logic_case selection, env_fade, func_brush solidity, point_teleport, and the trigger family —
// each with its live GetDebugState, an Inspect button that hands the row to the Entity Inspector,
// and a quick-fire button for its primary input. Also shows the world's single env_fade
// screen-fade state (what AElysiumHUD is drawing this frame). Reads the plain-C++ substrate
// directly (FElysiumCogWindow); compiled out of Shipping.
class FElysiumCogWindow_Logic : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	// One collapsible section listing every live entity of `Classname`, with its live state, an
	// Inspect button, and (when non-empty) a one-click fire of `QuickInput` carrying `QuickParam`.
	void RenderClassSection(const char* Label, const TCHAR* Classname,
		const TCHAR* QuickInput, const TCHAR* QuickParam);
};

#endif // ENABLE_COG
