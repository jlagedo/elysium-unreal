#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// Live control surface for the one Elysium weather path: authored wetness, the follow-rain
// Niagara volume, and the shared material graph. Entity I/O stays authoritative; an optional
// presentation override substitutes only the MPC wetness input so the timer graph can keep
// advancing underneath it.
class FElysiumCogWindow_Environment : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
};

#endif // ENABLE_COG
