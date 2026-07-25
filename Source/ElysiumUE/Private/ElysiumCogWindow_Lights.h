#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "ElysiumCogWindow.h"

// P2.5 Lights window: the F1-first surface for the real-time light rig (UElysiumLightRig). A
// visibility toggle, live calibration sliders (point/spot scale, max brightness, falloff, reach,
// specular, sun lux) that re-tune the running rig with no map reload, a source-type breakdown, and
// a scrollable per-source list (type / colour / raw magnitude / reach / lightstyle). It also owns
// the map's ambience — the baked sky light's intensity/colour/cubemap and the height fog — because
// how much the sky contributes and how much the per-source rig must carry is one calibration, not
// two. Subsumes the Canvas HUD's lights readout.
class FElysiumCogWindow_Lights : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	// Parked here while the sky cubemap is toggled off, so switching back does not need the map
	// to reload and rebuild it from the six exported face images. Weak: the cube is outer'd to the
	// map actor, so a map unload takes it and the toggle simply disappears with the sky light.
	TWeakObjectPtr<class UTextureCube> SkyCubemap;
};

#endif // ENABLE_COG
