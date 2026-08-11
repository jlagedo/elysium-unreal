#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "Substrate/ElysiumDisposition.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumEyeRig.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumNpcClips.h"

#include "ElysiumAnimationIntent.h"
#include "Visual/ElysiumAnimationResolve.h"

#include "ElysiumAnimSubsystem.generated.h"

class UAnimSequence;
class UBlendSpace;
class UglTFRuntimeAsset;
class USkeletalMesh;

// One blend grid resolved to everything a caller needs to stand it and steer it (ANM3). `Axes` is 1
// for a `move_yaw` locomotion fan and 2 for an aim grid; the entries above it are unset.
struct FElysiumResolvedGrid
{
	UBlendSpace* Space = nullptr;
	// The label the grid was reached by, which is a clip name in the character's vocabulary. Kept so
	// a caller that displays what is standing does not have to remember what it asked for.
	FString Label;
	int32 Axes = 0;
	// Per axis: the pose parameter it binds to, and the range the grid spans in that parameter's own
	// units (degrees). The range is the blend space's own axis range, so a slider built from it
	// covers exactly the samples and nothing outside them.
	FString AxisName[2];
	float AxisMin[2] = { 0.f, 0.f };
	float AxisMax[2] = { 0.f, 0.f };

	bool IsValid() const { return Space != nullptr; }
};

// What a selection resolved to on THIS body's skeleton (CCC4). Separate from the record on purpose:
// the record comes out of the sidecars and is always producible, while an asset needs a
// `USkeletalMesh` to bind against — and there is none in the gym, none on a menu backdrop, and none
// until the player visual is built. A record with no assets is `EElysiumAnimOutcome::NoAsset`, which
// is a true statement rather than a hole.
struct FElysiumResolvedAnimation
{
	UAnimSequence* Sequence = nullptr;
	UBlendSpace* Space = nullptr;

	// CCC10 — the upper-body layer(s) the selection named through `LayerLabels`: the bake-time
	// autolayer binding the base channel's own resolved host declared, or an activity-keyed
	// `UpperBody`/`Additive`-channel selection's own single asset (routed here by the caller
	// rather than into `Sequence`/`Space`). `OverlaySequence` and `OverlaySpace` are never both
	// set — a melee `_bobble_layer` is a plain sequence, an aim grid is a blend space.
	UAnimSequence* OverlaySequence = nullptr;
	UBlendSpace* OverlaySpace = nullptr;
	UAnimSequence* AdditiveSequence = nullptr;

	// The overlay's baked bone mask, as the NAME the clip's own `UElysiumAnimLayerMask` metadata
	// carries (the grid's base cell for `OverlaySpace` — every cell of a grid shares one mask), never
	// guessed from a weapon's grip.
	//
	// **A name and not a resolved `UBlendProfile*`, deliberately.** A profile object belongs to one
	// skeleton, while the node rebuilds its per-bone weights against the skeleton being *played* —
	// and a bank owns every masked overlay, so those are routinely not the same asset. Resolving the
	// name against the playing skeleton is what Epic's own `ULayeredBoneBlendLibrary::SetBlendMask`
	// does, and it is the same trap `Source/ElysiumUE/CLAUDE.md` records for the retired accumulator:
	// a profile taken off the layer's skeleton gates a shifted set of bones and logs nothing.
	FName OverlayMaskName;

	bool IsValid() const { return Sequence != nullptr || Space != nullptr; }
	bool HasUpperBodyLayer() const
	{
		return OverlaySequence != nullptr || OverlaySpace != nullptr || AdditiveSequence != nullptr;
	}
};

// Which rule chose an NPC's standing idle. Reported by the console verbs and the Cog window so a
// wrong-looking pose is traceable to the rule rather than guessed at.
enum class EElysiumIdleTier : uint8
{
	None,        // nothing resolved — the caller leaves the mesh in its reference pose
	Stance,      // ACT_DISPOSITION, `Stance_<AnimName>_Idle_*` for the NPC's disposition
	ActIdle,     // ACT_IDLE, highest actweight (44 of 54 NPCs reach Stance first)
	Loose,       // no activity, but the label reads as an idle — monsters and one-off models
};

// GameInstance-scoped owner of everything about NPC animation that outlives a map (roadmap 8.5).
//
// Two things are cached here rather than on the map actor, because both are skeleton-independent
// and expensive: the parsed **bank assets** (a shared animation library is 2-35 MB of glb, and the
// same two stances banks serve essentially every map), and the **clip vocabularies** read off
// out/npc/clips/<stem>.json. What is NOT cached here is the resolved UAnimSequence: glTFRuntime
// binds each one to a specific USkeletalMesh's USkeleton, and meshes are per-map-epoch, so those
// belong to AElysiumMapActor and die with it.
//
// The clip -> owning-stem resolution is entirely offline (pipeline/src/elysium_pipeline/exporters/npc_export.py walks the studiohdr
// include DAG); this subsystem never sees an include.
UCLASS()
class UElysiumAnimSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	// out/npc/npc_index.json, loaded once. Empty when the NPC export has not been run.
	const FElysiumNpcIndex& GetIndex();
	// out/npc/clips/<Stem>.json, cached per stem. Null when the stem has no slice.
	const FElysiumNpcClipSet* GetClipSet(const FString& Stem);
	// out/npc/facial/<Stem>.json, cached per stem (12.3). Null for a model with no flex rig, which
	// is the normal case for animals, crowd bodies and every player body — the caller animates the
	// body and leaves the face still. Shared rather than raw: an anim instance holds one for as long
	// as its body lives, across map epochs this GI-scoped cache outlasts.
	TSharedPtr<const FElysiumFacialRig> GetFacialRig(const FString& Stem);
	// The eyeball pair for a stem (12.4): `npc/eyes/<stem>.json`, carried onto the skeleton through
	// the glb's import transform. Same shape and lifetime as GetFacialRig — shared, immutable once
	// built, GI-scoped. Answered independently of the flex rig, because a player body carries a
	// pair of eyeballs and no flex rig at all.
	//
	// Both sidecars state their geometry in the glb's frame and are carried into the frame the
	// baked body landed in (`ElysiumNpcVisual::ImportGlbLocal`). There is one such frame, because
	// there is one build of a character; a rig built in the wrong one aims driven bones and irises
	// sideways while every other bone looks correct.
	TSharedPtr<const FElysiumEyeSet> GetEyeSet(const FString& Stem);
	// The two composition stages' rig for a stem (CAP7.2): `npc_index.json`'s `split_bones` plus
	// `npc/procedural/<stem>.json`. Null when the model declares neither, which is a normal load —
	// the body then poses under Unreal's ordinary hierarchy composition, as it did before CAP7.2.
	// Same shape and lifetime as GetFacialRig: shared, immutable once built, and GI-scoped so it
	// outlives the map epoch the skeleton belongs to. Same frame rule as above.
	TSharedPtr<const FElysiumCompositionRig> GetCompositionRig(const FString& Stem);
	// The same for a v4 animated prop, which indexes separately and whose sidecar sits under
	// animated_props/.
	TSharedPtr<const FElysiumCompositionRig> GetAnimatedPropCompositionRig(const FString& ModelPath);
	// The blend spaces a stem declares (CAP7.3): `npc/blends/<stem>.json`. Null for every model whose
	// sequences each name a single animation, which is most of them and a normal load. The stem may
	// be a character, a bank or an animated prop — all three can declare grids.
	TSharedPtr<const FElysiumBlendTable> GetBlendTable(const FString& Stem);
	// vdata/system/dispositiontable.txt, loaded once.
	const FElysiumDispositionTable& GetDispositions();

	// A bank's parsed glb, cached for the session. Null + OutError when it cannot be loaded.
	UglTFRuntimeAsset* GetBankAsset(const FString& BankStem, FString& OutError);

	// Retarget one named clip onto Mesh, resolving which glb owns it through the stem's clip set.
	// OwnAsset is the NPC's own already-parsed glb (the owner for its dialogue clips); pass null
	// and a clip the NPC owns cannot resolve. Returns null and fills OutError on any failure.
	UAnimSequence* ResolveClip(const FString& Stem, const FString& ClipName, USkeletalMesh* Mesh,
		UglTFRuntimeAsset* OwnAsset, FString& OutError);

	// 12.1 — retarget a clip out of a NAMED bank, bypassing the clip vocabulary. A choreo scene's
	// `entire_scene` lives in a cinematic anim set that no NPC's include tree mentions, so there is
	// no vocabulary entry to look it up by; the scene knows the bank because it knows its own
	// `BaseAnim` and the actor's `bonerename` root (PL16).
	UAnimSequence* ResolveClipFromBank(const FString& BankStem, const FString& ClipName,
		USkeletalMesh* Mesh, FString& OutError);

	// CAP7.3 — the animation a label actually plays on OwnerStem, which is the label itself for every
	// label that does not name a blend grid. **This is the only place a grid is collapsed to a cell**;
	// the three resolvers above and the animated-prop path all come through here, because a hook in
	// any one of them would silently leave the others playing the -180 degree base cell.
	//
	// OwnerStem is the stem whose glb carries the animation — a bank for most locomotion — not the
	// character asking for it, because the grid and its cells are declared by the owner. The returned
	// name is NOT in the character's clip vocabulary and must not be looked up there; it addresses an
	// animation in the owner's glb directly.
	FString ResolveGridClip(const FString& OwnerStem, const FString& Label,
		const FElysiumPoseParams& Pose = FElysiumPoseParams::Neutral());

	// The same answer for a label reached through a character's vocabulary, which is what finds the
	// owner. Exposed because a caller that caches the resolved sequence has to key its cache on the
	// name that will actually be loaded — resolving is two in-memory lookups, so asking twice is
	// cheaper than a cache keyed on a label that no longer identifies what it holds.
	FString ResolveClipAnimName(const FString& Stem, const FString& ClipName,
		const FElysiumPoseParams& Pose = FElysiumPoseParams::Neutral());

	// ANM3 — a label resolved to the whole grid rather than to one of its cells: the baked
	// `UBlendSpace`, plus what a caller needs to steer and label its axes. The axis metadata travels
	// with the asset because the blend space states its ranges in the pose parameter's own units but
	// not which parameter that is — the sidecar owns that binding, and re-deriving it from the axis
	// range would be guesswork on two parameters that share one.
	bool ResolveGrid(const FString& Stem, const FString& ClipName, USkeletalMesh* Mesh,
		struct FElysiumResolvedGrid& OutGrid);

	// CCC4 — the front door. One intent in, one selection record out, plus whatever of it could be
	// bound to `Mesh`. **This is the only resolver**: the player path and the NPC motor both come
	// through it, and `ResolveActivityClip` below is expressed over it, because two implementations of
	// one pick are how the player and the cast come to disagree about a bank silently.
	//
	// `Mesh` and `OwnAsset` may both be null — the record is still complete, and `OutAssets` simply
	// comes back empty. `docs/architecture/animation-architecture.md` section 3.3.
	void ResolveAnimation(const FElysiumAnimationIntent& Intent, USkeletalMesh* Mesh,
		UglTFRuntimeAsset* OwnAsset, FElysiumAnimationSelection& OutSelection,
		FElysiumResolvedAnimation& OutAssets);

	// The catalog view the resolver reads, gathered from this subsystem's own caches. Exposed so a
	// caller that resolves repeatedly does not re-enter the cache lookups, and so the Content tier can
	// run the same pure resolver over real sidecars.
	FElysiumAnimationCatalog BuildCatalog(const FString& Stem);

	// CCC7 — the three gait fans as per-direction speed tables, which is what the mover steers by.
	//
	// Resolved from the **un-relaxed** `ACT_WALK`/`ACT_RUN`/`ACT_SNEAK`, exactly as retail's
	// `PreThink` extractor does (activities 9, 19 and 18), so no table depends on the gait currently
	// selected or on where the body is pointing. That is what makes the set a property of the body
	// rather than of the frame, and it is why the mover can be handed one on a body change instead of
	// asking for one per tick.
	//
	// A gait that resolves to no fan leaves its table invalid rather than borrowing another's; the
	// mover falls back to the constants per gait, not wholesale. Returns whether any gait resolved.
	bool ResolveGaitSpeeds(const FElysiumGaitSpeedRequest& Request, FElysiumGaitSpeeds& Out);

	// Resolve one ACT_* request all the way through its character vocabulary and the owning bank's
	// neutral blend-grid cell. OutLabel is the vocabulary key (for example `walk`) that preserves
	// bank ownership for playback; OutAnimName is the concrete glb animation (`walk_0`), and the
	// speed is zero when that cell carries no authored movement metadata.
	bool ResolveActivityClip(const FString& Stem, const FString& Activity, int32 Variant,
		FString& OutLabel, FString& OutAnimName, float& OutGroundSpeedCmPerSecond);

	// The standing idle for a stem at a disposition, by VtMB's own chain:
	//   default_disposition -> dispositiontable "Animation Name" -> Stance_<Name>_Idle_* (by weight)
	//   -> ACT_IDLE (by weight) -> a loose idle-named clip -> nothing.
	// Selection is by **activity**, never by label substring: `regular_cop` resolves 229 clips with
	// "idle" in the name, of which `Stance_Dead_Idle_1` and `Bed_Left_Idle` are not standing idles.
	// Variant picks the candidate at that index, wrapping — the seam ambient variety rides on.
	// VtMB authors three standing idles per disposition (`Stance_<D>_Idle_{1,2,3}`) and its
	// dispositiontable pacing for cycling them is written for conversation only ("while waiting
	// for the player to make a dialog choice"), so an NPC nobody is talking to just holds one.
	// Spreading the cast across the authored three is a choice among clips VtMB wrote for exactly
	// this disposition, not invented behaviour — it is what stops 42 cops standing identically.
	FString PickIdleClip(const FString& Stem, const FString& Disposition, EElysiumIdleTier& OutTier,
		int32 Variant = 0);
	// Deterministic weighted activity selection. VData interesting places name ACT_* values and
	// frequencies; the clip manifest supplies the per-sequence weights within that activity.
	FString PickActivityClip(const FString& Stem, const FString& Activity, int32 Variant = 0);

	// Every candidate the idle policy considered, best first — the debug/verification view.
	TArray<FString> IdleCandidates(const FString& Stem, const FString& Disposition,
		EElysiumIdleTier& OutTier);

	static const TCHAR* TierName(EElysiumIdleTier Tier);

	// Banks parsed so far and their total .glb bytes on disk — what the Cog window reports.
	void GetBankStats(int32& OutCount, int64& OutBytes) const;

private:
	// Session-lifetime: a bank is map-independent, and re-parsing 11 MB per travel is the cost
	// this cache exists to avoid.
	UPROPERTY() TMap<FString, TObjectPtr<UglTFRuntimeAsset>> BankAssets;

	FElysiumNpcIndex Index;
	bool bIndexLoaded = false;

	FElysiumDispositionTable Dispositions;
	bool bDispositionsLoaded = false;

	// Value is null for a stem whose slice is missing, so a failed read is remembered rather than
	// retried on every NPC that shares the stem.
	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> ClipSets;
	// Same shape, same reason: a null entry is the remembered "this model has no flex rig".
	TMap<FString, TSharedPtr<const FElysiumFacialRig>> FacialRigs;
	// These two are keyed `<stem>|baked` / `<stem>|loader`, not by stem: their contents depend on
	// which frame the body landed in, so a single entry per stem would pin whichever path was built
	// first and hand the other one a rig aimed 90 degrees off.
	TMap<FString, TSharedPtr<const FElysiumEyeSet>> EyeSets;
	// And again for the composition stages. Keyed by stem+frame for characters and by the
	// normalized model path for animated props, which is how each is addressed upstream.
	TMap<FString, TSharedPtr<const FElysiumCompositionRig>> CompositionRigs;
	// The key those two share.
	static FString FrameKey(const FString& Stem, bool bBaked)
	{
		return Stem + (bBaked ? TEXT("|baked") : TEXT("|loader"));
	}
	// And again for the garment spike. A null entry here is the common case, not the exception.
	// And again for the blend spaces. Keyed by the OWNING stem — a bank serves every character that
	// resolves a clip out of it, so this is parsed once for the whole cast rather than per NPC.
	TMap<FString, TSharedPtr<const FElysiumBlendTable>> BlendTables;
};
