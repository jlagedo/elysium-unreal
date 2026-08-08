#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// The camera window (CCC2). Live, read-only state for the whole camera service: the four weights
// and the three latches that drive them, both rigs' solved booms side by side, the scripted-shot
// stack, and the post layers with their alphas.
//
// It exists because of the F1-first rule (`docs/architecture/debug-tooling.md`): `elysium.camera`
// dumps one line of the faithful evaluator, which was enough while there was one rig and no layer
// stack. With two rigs evaluating every frame and `elysium.ModernCamera` deciding which supplies
// the base, the question a developer actually asks — *which rig am I looking at, and how far apart
// are they?* — needs the two answers beside each other.
class FElysiumCogWindow_Camera : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
};

#endif // ENABLE_COG
