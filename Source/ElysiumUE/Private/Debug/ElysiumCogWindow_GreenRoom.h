#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "Visual/ElysiumClothRig.h"
#include "imgui.h"

class FElysiumGreenRoomRun;
class UElysiumBodyAnimInstance;
class UElysiumNpcAnimInstance;
class USkinnedAsset;

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
	// The body's shared portrait/rig surface — the rows that read a cloth rig or a composition rig.
	UElysiumBodyAnimInstance* GetBodyInstance() const;
	// The native pose machinery — the clip and layer rows. Null on a body posing from an anim graph.
	UElysiumNpcAnimInstance* GetNpcBodyInstance() const;

	// Which build of the body stands on the stage -- the baked /ElysiumBaked assets or the
	// glTFRuntime load -- and which one actually did, since only part of the cast is baked.
	void RenderSource(FElysiumGreenRoomRun& Lab);
	void RenderModel(FElysiumGreenRoomRun& Lab);
	void RenderPlayback(FElysiumGreenRoomRun& Lab);
	// Drive mode's readout (CCC6): what the mover published, what the resolver chose, what the graph
	// is playing, and how far off the bind pose the body actually is. The last one is the only
	// observable the T-pose failure has, and the middle two are what make a wrong pose traceable to
	// a step instead of guessed at.
	void RenderDrive(FElysiumGreenRoomRun& Lab);
	// The orbit, the stage, and the drawn overlays — everything about how the body is being looked
	// at, as opposed to which body it is or what its garment is doing.
	void RenderView(FElysiumGreenRoomRun& Lab);
	void RenderCloth(FElysiumGreenRoomRun& Lab);
	// The autolayer binding the standing clip declares, beside what the lab actually has riding.
	void RenderAutoLayers(FElysiumGreenRoomRun& Lab);

	void Stand(FElysiumGreenRoomRun& Lab, const FString& Stem, const FString& Clip);
	// What a clip row does when it is picked. An additive row lays a layer over the standing body
	// instead of standing the delta by itself, which is the only way to see what one is for: a
	// `_delta` alone is a difference, and a difference posed as a pose folds the skeleton up.
	void Pick(FElysiumGreenRoomRun& Lab, int32 Index);

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
	// Parallel to Clips — whether the label is a masked partial-body overlay, the other kind of
	// autolayer. Taken from the NAME, unlike ClipAdditive: the mask that actually decides this lives
	// in the container and reaches the runtime on the baked sequence, and the vocabulary sidecar the
	// list is built from carries neither. It is a row hint, not the decision — the pick still goes
	// through the same door as everything else and a wrong hint comes back as a refusal on screen.
	TArray<bool> ClipOverlay;
	// Parallel to Clips — the stem whose file carries the animation: the body itself for its own
	// dialogue clips, a shared bank otherwise. A body resolves most of its vocabulary through banks
	// it has nothing else to do with, and which file a clip came out of is the first thing worth
	// knowing when one of them looks wrong.
	TArray<FString> ClipOwner;

	// Row index of the clip the keyboard is on, into Clips. Arrow keys move it and stand what they
	// land on, so a vocabulary can be walked without the mouse.
	int32 ClipCursor = INDEX_NONE;
	// Retail's `layer_weight` for the next layer picked, and for the one already running — the
	// slider re-weights live, because re-asking for a running layer only changes its weight. The
	// scalar the accumulator actually receives in retail lives in the DLL and is not recovered, so
	// this is a control surface rather than a reproduction.
	float LayerWeight = 1.f;
	// Set when the cursor moved this frame, so the list scrolls to follow it.
	bool bClipCursorMoved = false;

	// The distinct owners in this stem's vocabulary, sorted, with the body's own file first. A
	// well-connected NPC resolves through ~30 banks, which is a list worth picking from; its 1,500
	// clips are not.
	TArray<FString> ClipOwners;
	// Filters over the clip list, all ANDed. Empty owner means every owner; the two enums are
	// 0 = everything, then one entry per value worth isolating.
	FString OwnerFilter;
	int32 KindFilter = 0;       // 1 = poses only, 2 = autolayers only (either kind)
	int32 MotionFilter = 0;     // 1 = carries the root, 2 = root held. Needs the scan below.

	// Parallel to Clips: 0 unknown, 1 the clip moves Bip01, 2 it holds it. VtMB's vocabulary does
	// not record this and only the resolved sequence can answer it, so it is filled by an explicit
	// scan rather than on load -- resolving a whole vocabulary is hundreds of package loads.
	TArray<uint8> ClipRootMotion;
	// Which stem ClipRootMotion belongs to, so a different body discards it rather than mislabels.
	FString ScannedStem;

	void ScanRootMotion(FElysiumGreenRoomRun& Lab);

	// The skeleton's own bind pose in component space, and which mesh it belongs to. Cached because
	// it is a property of the asset rather than of the frame — recomputing a whole rig every frame to
	// compare against it would be the panel doing more work than the thing it is measuring.
	TWeakObjectPtr<const USkinnedAsset> DeviationAsset;
	TArray<FTransform> DeviationRefPose;

	// The live edit and the file it came from. Both are held here rather than read back from the
	// node every frame because ImGui's sliders need a stable address to write into, and because
	// Revert has to know what the sidecar said before the dragging started.
	FElysiumClothTuning Tuning;
	FElysiumClothTuning Baseline;
	// Which body the two above belong to. A different body means re-reading both from its rig.
	FString TunedStem;
};

#endif // ENABLE_COG
