#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "CogDebugGizmo.h"
#include "Debug/ElysiumCogWindow.h"

class UElysiumLightRig;

// P2.5 Lights window: the F1-first surface for the real-time light rig (UElysiumLightRig). A
// visibility toggle, human-scale live calibration controls (brightness, reach, Lumen bounce,
// fog scattering, source shape, shadows and sun) that re-tune the running rig with no map reload, and
// a scrollable per-source list (type / colour / raw magnitude / reach / lightstyle). It also owns
// the map's ambience — the baked sky light's intensity/colour/cubemap and the height fog — because
// how much the sky contributes and how much the per-source rig must carry is one calibration, not
// two. Subsumes the Canvas HUD's lights readout.
//
// Per-light inspector: one source is selected at a time, from the list or by clicking its marker
// in the world, and its output/transport/shape/cone/shadow is edited directly, with a 3D gizmo on
// its transform. Editing a light marks it overridden in the rig, which keeps the global
// sliders and the lightstyle animation from writing back over the edit. A separate Enabled switch
// takes one light out of the map without touching its values.
//
// Save writes the complete calibration, disabled set and overrides as JSON (one file per map under
// `_lights/`, overwritten each time); map load restores it by stable `.lights` source index.
//
// Lights carry no collision, so the world pick here is its own thing rather than ElysiumPick — it
// is a screen-space nearest-marker test, which is also what makes a light inside solid geometry
// (most of them) reachable at all.
class FElysiumCogWindow_Lights : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderTick(float DeltaTime) override;
	virtual void RenderContent() override;

private:
	// The selected source's own attributes, plus its transform gizmo. Returns nothing; every edit
	// goes straight to the live component.
	void RenderSelectedSource(UElysiumLightRig& Rig, int32 Index);
	// The map-wide edit actions (revert/enable/non-spot batch/save), rendered whether or not a source
	// is selected — they act on the whole rig, not on the selection.
	void RenderEditActions(UElysiumLightRig& Rig, const FString& MapName);
	// Write the map's edits to FElysiumContentPaths::LightEdits(MapName), overwriting any previous
	// save. Returns false and fills OutMessage on failure. (The Load button is the rig's own
	// LoadSurvey — the same pass Adopt auto-applies at map load.)
	static bool SaveEdits(UElysiumLightRig& Rig, const FString& MapName, FString& OutMessage);
	// Draw a marker per light over the world and, when armed, note which one a click would take.
	void TickMarkersAndPick(UElysiumLightRig& Rig);
	// Apply a click noted by TickMarkersAndPick, unless the gizmo took it. Runs at the end of
	// RenderContent because that is where the gizmo submits: the gizmo grabs on LMB without
	// setting WantCaptureMouse, so a pick resolved in RenderTick would steal the click that
	// starts a drag and re-select whatever light happened to be behind the handle.
	void CommitPendingPick();
	// Enforce/lift the isolate ("solo") state, which hides every light but the selected one.
	void TickSolo(UElysiumLightRig& Rig);

	// Parked here while the sky cubemap is toggled off, so switching back does not need the map
	// to reload and rebuild it from the six exported face images. Weak: the cube is outer'd to the
	// map actor, so a map unload takes it and the toggle simply disappears with the sky light.
	TWeakObjectPtr<class UTextureCube> SkyCubemap;
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

	// The last save's outcome, shown under the button so a write is visibly confirmed (the file
	// lands outside the game window, where nothing else would report it).
	FString SaveStatus;
	bool bSaveFailed = false;

	FCogDebug_Gizmo Gizmo;
};

#endif // ENABLE_COG
