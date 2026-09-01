#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "Math/Color.h"

class UElysiumLightRig;

// F1-first **read-only viewer** for the real-time light rig (UElysiumLightRig): a visibility
// toggle, a per-source list (type / colour / raw magnitude / reach / lightstyle), and per-light
// identification by clicking a marker in the world. Subsumes the Canvas HUD's lights readout.
//
// R4.3 (`docs/architecture/seam_map_map_lighting.md` -> "Import") retired every tuning affordance
// this window used to carry -- global calibration sliders, the sky-light/height-fog panel and
// skylight-leaking A/B, the per-light editor and gizmo, batch enable/disable, and the JSON survey's
// Save/Load -- in favour of `UElysiumLightingSettings` (Project Settings -> Elysium -> Lighting) and
// the per-map `UElysiumLightCalibration` data asset, both edited the ordinary Unreal way ("the
// Unreal editor is the tuning surface", `docs/project/seam_migration.md`). What is left is exactly
// what a human still needs Cog for: seeing which light in the world a `.lights` row is.
//
// Lights carry no collision, so the world pick here is its own thing rather than ElysiumPick — it
// is a screen-space nearest-marker test among the sources the camera can see. A light slightly
// inside its fixture still counts (the visual hit is allowed to land short of the origin); a
// light behind a wall does not, so the overlay is the room in view rather than every source in
// the frustum.
class FElysiumCogWindow_Lights : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderTick(float DeltaTime) override;
	virtual void RenderContent() override;

private:
	// The selected source's own attributes, read-only. Returns nothing; there is nothing here to
	// write back.
	void RenderSelectedSource(UElysiumLightRig& Rig, int32 Index);
	// Draw a marker per light over the world and, when armed, note which one a click would take.
	void TickMarkersAndPick(UElysiumLightRig& Rig);
	// Apply a click noted by TickMarkersAndPick. Runs at the end of RenderContent, after every other
	// widget has had a chance to consume the frame's input.
	void CommitPendingPick();
	// Enforce/lift the isolate ("solo") state, which hides every light but the selected one. A
	// viewing aid, not a value edit: nothing it touches survives a re-select or a map reload.
	void TickSolo(UElysiumLightRig& Rig);

	// Row indices and all editor-only state below belong to one adopted rig. A rig change clears
	// them so travel cannot apply a previous map's selection or isolate state to the next map.
	TWeakObjectPtr<UElysiumLightRig> ActiveRig;

	int32 SelectedSource = INDEX_NONE;   // index into UElysiumLightRig::Sources()
	bool bClickToSelect = true;          // LMB over the world selects the nearest light marker
	bool bDrawMarkers = true;            // draw the per-light markers over the world
	bool bScrollToSelected = false;      // a world pick asks the list to scroll its row into view

	// Isolate: while on, every light but the selected one is hidden, which is the fastest way to
	// tell which fixture a row actually is. Tracked separately from the selection so changing the
	// selection while isolated just moves the lit light.
	bool bIsolate = false;
	bool bIsolateApplied = false;        // whether the hide pass is currently in effect

	// This frame's deferred pick: what the cursor is over, and which button was pressed.
	int32 HoveredSource = INDEX_NONE;
	bool bSelectPending = false;
	bool bClearPending = false;
};

#endif // ENABLE_COG
