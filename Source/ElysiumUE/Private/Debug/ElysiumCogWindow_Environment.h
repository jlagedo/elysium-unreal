#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"

// Live control surface for the one Elysium environment material path. The authored entity state
// remains authoritative; an optional presentation override substitutes only the MPC input so the
// source state can keep advancing underneath it. Niagara is intentionally outside this window.
class FElysiumCogWindow_Environment : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;
};

#endif // ENABLE_COG
