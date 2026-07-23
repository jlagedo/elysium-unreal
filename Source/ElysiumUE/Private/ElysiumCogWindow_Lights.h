#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "ElysiumCogWindow.h"

// P2.5 Lights window: the F1-first surface for the real-time light rig (UElysiumLightRig). A
// visibility toggle, live calibration sliders (point/spot scale, max brightness, falloff, reach,
// specular, sun lux) that re-tune the running rig with no map reload, a source-type breakdown, and
// a scrollable per-source list (type / colour / raw magnitude / reach / lightstyle). Subsumes the
// Canvas HUD's lights readout.
class FElysiumCogWindow_Lights : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
};

#endif // ENABLE_COG
