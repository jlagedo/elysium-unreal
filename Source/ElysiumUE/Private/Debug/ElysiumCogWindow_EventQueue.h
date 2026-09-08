#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// The pending time-sorted I/O deliveries with their fire times, the always-on I/O history ring
// buffer (the delivered/unknown/Python causality stream), and the pause / single-step controls.
// Pause holds the queue's service loop; Step releases one due event at a time — the causality
// single-stepper. The controls write the queue's
// pause/step flags; everything else is read-only.
class FElysiumCogWindow_EventQueue : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	int32 HistoryLines = 200;   // tail of the ring buffer to show
};

#endif // ENABLE_COG
