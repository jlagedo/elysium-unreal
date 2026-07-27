#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "imgui.h"

// P2.2 entity browser: a filterable, paged list of every FElysiumEntityWorld record — all live
// entities plus the inert unhandled classnames — with a classname histogram and per-row dormancy
// state. Clicking a row sets the shared FElysiumCogWindow selection, which the Entity Inspector
// reads. Read-only over the substrate; the only side effect is the selection.
class FElysiumCogWindow_Entities : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	ImGuiTextFilter Filter;   // matches "<targetname> <classname>" per row

	// Include toggles (a record can be any combination of these; default: show everything).
	bool bShowLive = true;
	bool bShowHidden = true;
	bool bShowDead = true;
	bool bShowRecordOnly = true;
};

#endif // ENABLE_COG
