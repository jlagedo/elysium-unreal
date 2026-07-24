#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "ElysiumCogWindow.h"

class FElysiumEntity;
class FElysiumEntityWorld;
struct FElysiumPickResult;

// P2.2/P2.6 entity inspector: the detail view of the shared-selected entity and the "primary test
// harness" (debug-tooling.md). It reads one entity's identity, chain-walked live fields, raw
// keyvalues, and 7-field outputs, and fires any input on it by hand through the real event queue
// (FElysiumEntityWorld::EnqueueInput) so the delivery shows up in the Event Queue window and is
// single-steppable. Everything but the fire buttons is read-only.
//
// Selection is by click (P2.6): while the Cog menu owns the mouse, LMB anywhere over the world
// picks whatever is under the cursor — brush entity, world surface, or prop instance — and the
// window draws a translucent highlight on it. RMB clears. The game is not paused; it keeps
// running under the cursor. The pick runs from RenderTick, which Cog calls for every window
// whether or not it is visible, so picking and the highlight work with this window closed.
class FElysiumCogWindow_Inspector : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void PreBegin(ImGuiWindowFlags& WindowFlags) override;
	virtual void RenderTick(float DeltaTime) override;
	virtual void RenderContent() override;

private:
	// The picked thing's surface half (component / material / textures / section+instance+triangle).
	void RenderPickDetails(const FElysiumPickResult& InPick);
	// The selected entity's detail, inside the scrolling region. bSelectionChanged is true on the
	// frame the selection changed, which is when the collapsing sections re-apply their per-class
	// default open state (see the .cpp — an inert record leads with keyvalues, a real class with
	// fields) without fighting the user the rest of the time.
	void RenderEntityDetails(FElysiumEntity& Ent, FElysiumEntityWorld& World, bool bSelectionChanged);

	bool bClickToSelect = true;      // LMB over the world picks
	bool bDrawHighlight = true;      // draw the translucent overlay on the selection
	bool bHoverPreview = true;       // outline what the cursor is over before committing
	bool bHideDefaultFields = true;  // drop chain fields still at their zero/empty value

	// Last entity the detail pane drew, so a change can re-seat the collapsing sections.
	FElysiumEntityHandle LastDetailSelection;

	FString PendingParam;         // the param string fired inputs carry (field-2 marshalling)
	float   PendingDelay = 0.0f;
};

#endif // ENABLE_COG
