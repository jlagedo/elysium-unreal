#pragma once

#include "CogCommon.h"

#if ENABLE_COG

#include "CoreMinimal.h"
#include "Debug/ElysiumCogWindow.h"
#include "imgui.h"

class FElysiumGreenRoomRun;
class UElysiumBodyAnimInstance;
class UElysiumEntityBodies;
class UElysiumBipedAnimInstance;
class USkinnedAsset;

// The green room's control surface: pick a body, pick a clip, and watch it move.
//
// The green room has always been a one-shot — it resolved a fixed case, seeked fixed times, wrote
// PNGs and exited — and everything it answered was answered by a still. Most of what a body does is
// not a still. Whether a gait reads at walking pace, whether a held weapon tracks the hand through
// one, whether two irises converge on the same point: all of that is timing, and none of it
// survives being sampled at five fractions. This window drives the same stage in the live lab mode
// (`Debug/ElysiumGreenRoomRun.h`), where nothing is captured and nothing exits.
class FElysiumCogWindow_GreenRoom : public FElysiumCogWindow
{
	typedef FElysiumCogWindow Super;

public:
	// Open, or arm the lab first if this session has no green room. Called by `elysium.gr` as well
	// as by the menu, so it must be safe from a cold session with a map already loaded.
	void OpenLab();

	// Park the window against one edge of Cog's viewport dockspace, so the stage keeps the rest of
	// the screen instead of being covered by a floating panel. The dock is applied on the next
	// rendered frame: the dockspace node only exists once Cog has submitted it, which has not
	// happened yet when a launch switch is read.
	void DockToSide(bool bLeft);

protected:
	virtual void Initialize() override;
	virtual void RenderHelp() override;
	virtual void PreBegin(ImGuiWindowFlags& WindowFlags) override;
	virtual void RenderContent() override;

private:
	// The lab this window drives, or null when nothing is armed (which is every ordinary session).
	FElysiumGreenRoomRun* GetLab() const;
	// The anim instance of the body currently standing on the stage, or null.
	// The body's shared portrait/rig surface — the rows that read a composition rig.
	UElysiumBodyAnimInstance* GetBodyInstance() const;
	// The pose machinery itself — the clip and layer rows, which read the proxy rather than a rig.
	UElysiumBipedAnimInstance* GetBipedInstance() const;

	// Which build of the body stands on the stage -- the baked /ElysiumBaked assets or the
	// glTFRuntime load -- and which one actually did, since only part of the cast is baked.
	void RenderSource(FElysiumGreenRoomRun& Lab);
	void RenderModel(FElysiumGreenRoomRun& Lab);
	void RenderClips(FElysiumGreenRoomRun& Lab);
	void RenderLayers(FElysiumGreenRoomRun& Lab);
	void RenderPlayback(FElysiumGreenRoomRun& Lab);
	// Drive mode's readout (CCC6): what the mover published, what the resolver chose, what the graph
	// is playing, and how far off the bind pose the body actually is. The last one is the only
	// observable the T-pose failure has, and the middle two are what make a wrong pose traceable to
	// a step instead of guessed at.
	void RenderDrive(FElysiumGreenRoomRun& Lab);
	// The orbit, the stage, and the drawn overlays — everything about how the body is being looked
	// at, as opposed to which body it is or what its garment is doing.
	void RenderView(FElysiumGreenRoomRun& Lab);
	// The evaluated local pose of the standing body's torso-to-head chain against its mesh bind —
	// the Cog equivalent of `elysium.gr_bones`. Written by the "Dump bones" button in RenderView; a
	// hand-triggered snapshot rather than a per-frame readout, matching that verb's log-on-request
	// shape.
	void DumpBones(FElysiumGreenRoomRun& Lab);
	FString BoneDumpText;

	// The eye rig (12.4). The stage carries no `FElysiumNpc`, so nothing supplies a gaze and every eye
	// rests on its authored aim — a correct state that shows none of what the rig does. This tab is
	// the aim, the blink and the two renderer knobs, plus the readout that separates "this model has
	// no eyes" from "its sections drew as eyes and joined no record", which look identical on screen.
	void RenderEyes(FElysiumGreenRoomRun& Lab);
	// The body factory holding the eye bindings, or null. Off the map subsystem rather than the body's
	// owner: in drive mode the visual hangs on the pawn, and the bindings never move off the map actor.
	UElysiumEntityBodies* GetBodies() const;
	// The autolayer binding the standing clip declares, beside what the lab actually has riding.
	void RenderAutoLayers(FElysiumGreenRoomRun& Lab);
	// The wielded weapon (CCC10.2): pick an item, pick the wielder's sex, and read back the row the
	// table answered with — the baked mesh, the mount bone and the hand it descends from. The
	// picker lists only the rows that carry geometry, because the corpus's ordinary answer is that
	// an item holds none, and 244 mostly-empty rows would hide the 40 that do.
	//
	// The placement is judged here and nowhere else: there is no correction factor to reach for, so
	// a weapon in the wrong place is a bake defect and this tab is where it is caught.
	void RenderWield(FElysiumGreenRoomRun& Lab);
	// The classnames the picker draws, rebuilt when the sex toggle moves or a bake lands. Resolving
	// the whole table is a map walk, so it is not done per frame.
	TArray<FString> WieldRows;
	FString WieldFilter;
	bool bWieldRowsDirty = true;
	bool bWieldRowsFemale = false;

	void Stand(FElysiumGreenRoomRun& Lab, const FString& Stem, const FString& Clip);
	// What a clip row does when it is picked. An additive row lays a layer over the standing body
	// instead of standing the delta by itself, which is the only way to see what one is for: a
	// `_delta` alone is a difference, and a difference posed as a pose folds the skeleton up.
	void Pick(FElysiumGreenRoomRun& Lab, int32 Index);

	// Which edge the window is still waiting to dock against, or None once it has (or was never
	// asked to). Held rather than acted on immediately because ImGui's dock builder can only reach a
	// node that has already been submitted this frame.
	enum class EPendingDock : uint8 { None, Left, Right };
	EPendingDock PendingDock = EPendingDock::None;

	FString PendingStem;
	FString PendingClip;
	FString StemFilter;
	FString ClipFilter;
	FString LastError;
	FString LastNotice;

	// Arena navigation pins (drive/arena's own, `RenderDrive`'s arena branch): the name field a
	// dropped pin takes, and the loop checkbox a panel-started `gr_walk` reads.
	FString PendingPinName;
	bool bArenaWalkLoop = false;

	// Whether the two fields above are still mirroring the stage or have been steered by hand. Set
	// by the first typed character, list click or Stand, and never cleared: once someone has picked
	// a model, the window must stop overwriting what they picked.
	bool bUserPicked = false;

	// Rescanned on first open and on demand: an export can land while the game is up.
	bool bStemsDirty = true;
	TArray<FString> Stems;
	// Parallel to Stems — whether that model exported an authored garment payload. Probed once per
	// rescan rather than once per frame per row. This is the EXPORT, not the generated asset: it
	// answers "does this character have cloth at all", which is what the list column is for.
	TArray<bool> StemHasCloth;

	// One row of the selected model's clip vocabulary — one struct rather than parallel arrays, so
	// a label can never drift apart from the facts the list draws beside it.
	struct FClipRow
	{
		// The vocabulary label the pick resolves.
		FString Label;
		// The cell a blend-grid label actually plays, or empty when the label names one animation.
		// A grid resolves to a cell that is NOT in the vocabulary, so without this the list would
		// show `walk` and stand something whose name appears nowhere on screen.
		FString Cell;
		// The stem whose file carries the animation: empty for the body's own clips, a shared bank
		// otherwise. A body resolves most of its vocabulary through banks it has nothing else to do
		// with, and which file a clip came out of is the first thing worth knowing when one of them
		// looks wrong.
		FString Owner;
		// 0 unknown, 1 the clip moves Bip01, 2 it holds it. VtMB's vocabulary does not record this
		// and only the resolved sequence can answer it, so it is filled by ScanRootMotion rather
		// than on load -- resolving a whole vocabulary is hundreds of package loads.
		uint8 RootMotion = 0;
		// Whether the label is an additive layer rather than a pose. Read off the vocabulary,
		// because the answer changes what a broken-looking body means.
		bool bAdditive = false;
		// Whether the label is a masked partial-body overlay, the other kind of autolayer. Taken
		// from the NAME, unlike bAdditive: the mask that actually decides this lives in the
		// container and reaches the runtime on the baked sequence, and the vocabulary sidecar the
		// list is built from carries neither. It is a row hint, not the decision — the pick still
		// goes through the same door as everything else and a wrong hint comes back as a refusal
		// on screen.
		bool bOverlay = false;
	};

	// The selected model's clip vocabulary, sorted by label, cached per stem. A well-connected NPC
	// resolves well over a thousand clips, so this is neither rebuilt nor re-sorted per frame.
	FString ClipsStem;
	TArray<FClipRow> ClipRows;
	// Rebuilds the vocabulary cache for PendingStem, along with the distinct-owner list, and resets
	// everything keyed to the previous stem — the filters, the cursor and the root-motion scan.
	void RebuildClipCache();

	// Row index of the clip the keyboard is on, into ClipRows. Arrow keys move it and stand what they
	// land on, so a vocabulary can be walked without the mouse.
	int32 ClipCursor = INDEX_NONE;
	// Retail's `layer_weight` for the next layer picked, and for the one already running — the
	// slider re-weights live, because re-asking for a running layer only changes its weight. The
	// scalar the accumulator actually receives in retail lives in the DLL and is not recovered, so
	// this is a control surface rather than a reproduction.
	float LayerWeight = 1.f;
	// Where an armed aim grid is sampled, in the pose parameters' own degrees (CCC10). Distinct from
	// the eye/gaze sliders: those aim a look, these pick the cell of a 3x3 weapon-aim grid. The
	// ordinary player producer pins both at zero, so a grid that could not be steered here would read
	// as a still pose and its whole point — that the torso tracks — would be invisible.
	float LayerAimYaw = 0.f;
	float LayerAimPitch = 0.f;
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

	// Which stem the rows' RootMotion answers belong to, so a different body discards them rather
	// than mislabels.
	FString ScannedStem;

	void ScanRootMotion(FElysiumGreenRoomRun& Lab);

	// Where the Eyes tab's manual aim sits, in the standing body's own frame rather than in world
	// coordinates: the stage stands at (50000, 50000, 5000), so a world XYZ is a number nobody can
	// steer. Yaw and pitch are degrees off the body's facing, measured about the head; the distance is
	// centimetres out along that direction. The resolved world point is what reaches the eye pass.
	float EyeAimYaw = 0.f;
	float EyeAimPitch = 0.f;
	float EyeAimDistance = 150.f;
	// Draw the resolved point on the stage. On by default — convergence is the whole observable, and
	// two irises aimed at a marker is the check, not two irises aimed somewhere plausible.
	bool bEyeAimDraw = true;

	// The skeleton's own bind pose in component space, and which mesh it belongs to. Cached because
	// it is a property of the asset rather than of the frame — recomputing a whole rig every frame to
	// compare against it would be the panel doing more work than the thing it is measuring.
	TWeakObjectPtr<const USkinnedAsset> DeviationAsset;
	TArray<FTransform> DeviationRefPose;

};

#endif // ENABLE_COG
