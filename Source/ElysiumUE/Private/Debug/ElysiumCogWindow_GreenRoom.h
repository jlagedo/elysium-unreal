#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "Visual/ElysiumClothRig.h"
#include "imgui.h"

class FElysiumGreenRoomRun;
class UElysiumNpcAnimInstance;

// The green room's control surface: pick a body, pick a clip, watch it move, and tune the garment
// simulation while it does.
//
// The green room has always been a one-shot — it resolved a fixed case, seeked fixed times, wrote
// PNGs and exited — and everything it answered was answered by a still. A garment is not a still.
// Whether a skirt settles or keeps ringing, whether a hem clears the knee at walking pace, whether
// a coat flares like a coat or like a crinoline: all of that is timing, and none of it survives
// being sampled at five fractions. This window drives the same stage in the live lab mode
// (`Debug/ElysiumGreenRoomRun.h`), where nothing is captured and nothing exits.
//
// The cloth sliders write to the running simulation, not to a file. `npc/cloth/<stem>.json` is what
// the offline spike derived from the model's own measurements — the shape of the cone ramp down a
// panel, the radius of the thigh a hem must clear — and it stays the record until Save bakes a
// tuning into it. Revert goes back to it. So an afternoon of dragging sliders costs nothing that
// closing the window does not undo.
class FElysiumCogWindow_GreenRoom : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

public:
	// Open, or arm the lab first if this session has no green room. Called by `elysium.gr` as well
	// as by the menu, so it must be safe from a cold session with a map already loaded.
	void OpenLab();

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void RenderContent() override;

private:
	// The lab this window drives, or null when nothing is armed (which is every ordinary session).
	FElysiumGreenRoomRun* GetLab() const;
	// The anim instance of the body currently standing on the stage, or null.
	UElysiumNpcAnimInstance* GetBodyInstance() const;

	void RenderModel(FElysiumGreenRoomRun& Lab);
	void RenderPlayback(FElysiumGreenRoomRun& Lab);
	// The orbit, the stage, and the drawn overlays — everything about how the body is being looked
	// at, as opposed to which body it is or what its garment is doing.
	void RenderView(FElysiumGreenRoomRun& Lab);
	void RenderCloth(FElysiumGreenRoomRun& Lab);

	void Stand(FElysiumGreenRoomRun& Lab, const FString& Stem, const FString& Clip);

	FString PendingStem;
	FString PendingClip;
	FString StemFilter;
	FString ClipFilter;
	FString LastError;
	FString LastNotice;

	// Whether the two fields above are still mirroring the stage or have been steered by hand. Set
	// by the first typed character, list click or Stand, and never cleared: once someone has picked
	// a model, the window must stop overwriting what they picked.
	bool bUserPicked = false;

	// Rescanned on first open and on demand: an export can land while the game is up.
	bool bStemsDirty = true;
	TArray<FString> Stems;
	// Parallel to Stems — whether the spike built a simulated garment for that model. Probing the
	// disk once per rescan rather than once per frame per row, which is what drawing the list
	// straight from `UseClothMesh` would cost.
	TArray<bool> StemHasCloth;

	// The selected model's clip vocabulary, sorted, cached per stem. A well-connected NPC resolves
	// well over a thousand clips, so this is neither rebuilt nor re-sorted per frame.
	FString ClipsStem;
	TArray<FString> Clips;
	// Parallel to Clips — the cell a blend-grid label actually plays, or empty when the label names
	// one animation. A grid resolves to a cell that is NOT in the vocabulary, so without this the
	// list would show `walk` and stand something whose name appears nowhere on screen. Built once per
	// stem beside the vocabulary rather than per frame per row.
	TArray<FString> ClipCells;
	// Parallel to Clips — whether the label is an additive layer rather than a pose. Read off the
	// vocabulary once per stem, because the answer changes what a broken-looking body means.
	TArray<bool> ClipAdditive;

	// The live edit and the file it came from. Both are held here rather than read back from the
	// node every frame because ImGui's sliders need a stable address to write into, and because
	// Revert has to know what the sidecar said before the dragging started.
	FElysiumClothTuning Tuning;
	FElysiumClothTuning Baseline;
	// Which body the two above belong to. A different body means re-reading both from its rig.
	FString TunedStem;
};

#endif // ENABLE_COG
