#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "ElysiumCogWindow.h"

// P2.2 entity inspector: the detail view of the shared-selected entity (set by the browser or a
// picker) and the "primary test harness" (debug-tooling.md). It reads one entity's identity,
// chain-walked live fields, raw keyvalues, and 7-field outputs, and fires any input on it by hand
// through the real event queue (FElysiumEntityWorld::EnqueueInput) so the delivery shows up in the
// Event Queue window and is single-steppable. Everything but the fire buttons is read-only.
class FElysiumCogWindow_Inspector : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	FString PendingParam;      // the param string fired inputs carry (field-2 marshalling)
	float   PendingDelay = 0.0f;
};

#endif // ENABLE_COG
