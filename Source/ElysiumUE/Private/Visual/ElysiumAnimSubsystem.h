#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "Substrate/ElysiumDisposition.h"
#include "Visual/ElysiumBankRemap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumCompositionRig.h"
#include "Visual/ElysiumEyeRig.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumNpcClips.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumOverlayStack.h"
#include "Visual/ElysiumAnimationResolve.h"

#include "UObject/ObjectKey.h"

#include "ElysiumAnimSubsystem.generated.h"

class UAnimSequence;
class UBlendSpace;
class USkeleton;
class USkeletalMesh;

// One blend grid resolved to everything a caller needs to stand it and steer it. `Axes` is 1
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

// One overlay slot's resolved assets — the layer a producer armed, plus the trio the layer's OWN
// clip declares.
//
// **The layer's autolayer closure is resolved by the same rule the base channel's host gets**, which
// is retail's rule applied recursively: the motion stays `Sequence`, its declared aim grid composes
// OVER it (`BS_<layer>_<slot clip>`, steered by the same two aim parameters, gated by the grid's own
// mask), and its declared `_delta` composes additively after that. A layer that declares nothing —
// every reload layer — leaves the trio null and the sequence stands alone.
//
// The masks are NAMES rather than resolved `UBlendProfile*`s, for the reason `OverlayMaskName` below
// is: a profile belongs to one skeleton and the node rebuilds its per-bone weights against the
// skeleton being played, while a bank owns every masked layer.
//
// **The envelope and the rate are deliberately not here.** Both belong to `FElysiumOverlayLayer`,
// which advances the cycle they are read against; a second copy beside the asset is a number the
// graph could be driven from that nothing keeps in step with the layer it belongs to.
struct FElysiumResolvedOverlaySlot
{
	TObjectPtr<UAnimSequence> Sequence = nullptr;
	TObjectPtr<UBlendSpace> AimSpace = nullptr;
	TObjectPtr<UAnimSequence> Additive = nullptr;
	// The layer's own baked bone mask. A layer with no mask owns the whole rig, which is never what a
	// partial-body overlay means — the resolver warns rather than composing one silently.
	FName MaskName;
	FName AimMaskName;

	bool IsValid() const { return Sequence != nullptr || AimSpace != nullptr; }

	void Reset()
	{
		// **Every field the resolver writes, not just the pair the validity test reads.** `AimSpace`,
		// `AimMaskName` and `Additive` come out of `ResolveSlotDeclaredAssets` on the same pass and go
		// stale by the same argument; leaving them makes the slot answer valid off a grid under a
		// record that names no layer.
		*this = FElysiumResolvedOverlaySlot();
	}
};

// What a selection resolved to on THIS body's skeleton. Separate from the record on purpose:
// the record comes out of the sidecars and is always producible, while an asset needs a
// `USkeletalMesh` to bind against — and there is none in the gym, none on a menu backdrop, and none
// until the player visual is built. A record with no assets is `EElysiumAnimOutcome::NoAsset`, which
// is a true statement rather than a hole.
struct FElysiumResolvedAnimation
{
	// Hand every asset this record holds to a collector. **The record lives outside the object
	// graph** — `FElysiumAnimationDriver` is plain C++ owned through a `TPimplPtr` — so nothing here
	// is reachable by reflection and nothing keeps it alive. An uncooked run happens to survive on
	// `RF_Standalone`; a cooked one collects, and the next publish assigns a dangling pointer into a
	// `TObjectPtr` on the anim instance. The driver is the `FGCObject` that calls this.
	void AddReferencedObjects(class FReferenceCollector& Collector);

	TObjectPtr<UAnimSequence> Sequence = nullptr;
	TObjectPtr<UBlendSpace> Space = nullptr;

	// The upper-body layer(s) the selection named through `LayerLabels`: the bake-time
	// autolayer binding the base channel's own resolved host declared, or an activity-keyed
	// `UpperBody`/`Additive`-channel selection's own single asset (routed here by the caller
	// rather than into `Sequence`/`Space`). `OverlaySequence` and `OverlaySpace` are never both
	// set — a melee `_bobble_layer` is a plain sequence, an aim grid is a blend space.
	TObjectPtr<UAnimSequence> OverlaySequence = nullptr;
	TObjectPtr<UBlendSpace> OverlaySpace = nullptr;
	TObjectPtr<UAnimSequence> AdditiveSequence = nullptr;

	// The overlay's baked bone mask, as the NAME the clip's own `UElysiumAnimLayerMask` metadata
	// carries (the grid's base cell for `OverlaySpace` — every cell of a grid shares one mask), never
	// guessed from a weapon's grip.
	//
	// **A name and not a resolved `UBlendProfile*`, deliberately.** A profile object belongs to one
	// skeleton, while the node rebuilds its per-bone weights against the skeleton being *played* —
	// and a bank owns every masked overlay, so those are routinely not the same asset. Resolving the
	// name against the playing skeleton is what Epic's own `ULayeredBoneBlendLibrary::SetBlendMask`
	// does, and it is the same trap `Source/ElysiumUE/CLAUDE.md` records: a profile taken off the
	// layer's skeleton gates a shifted set of bones and logs nothing.
	FName OverlayMaskName;

	// The overlay SLOTS, which are a different thing from the three layers above.
	//
	// Those are the bake-time autolayers the base channel's own resolved host DECLARES — they belong
	// to the base clip and travel with it. These are retail's `CBaseAnimatingOverlay` slots: layers a
	// PRODUCER armed on the UpperBody channel, composed over whatever owns the base pose and outliving
	// any number of base selections underneath them. Ranged fire, reload and dry-fire are the shipped
	// consumers. **Four of them, in slot index order, which is composition order.**
	FElysiumResolvedOverlaySlot Slots[ElysiumOverlay::NumSlots];

	bool IsValid() const { return Sequence != nullptr || Space != nullptr; }
	bool HasUpperBodyLayer() const
	{
		return OverlaySequence != nullptr || OverlaySpace != nullptr || AdditiveSequence != nullptr;
	}
	bool HasSlotLayer(int32 SlotIndex) const
	{
		return SlotIndex >= 0 && SlotIndex < ElysiumOverlay::NumSlots && Slots[SlotIndex].IsValid();
	}
	bool AnySlotLayer() const
	{
		for (const FElysiumResolvedOverlaySlot& Slot : Slots)
		{
			if (Slot.IsValid())
			{
				return true;
			}
		}
		return false;
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

// GameInstance-scoped owner of everything about NPC animation that outlives a map.
//
// Two things are cached here rather than on the map actor, because both are skeleton-independent
// and expensive: the parsed **bank assets** (a shared animation library is 2-35 MB of glb, and the
// same two stances banks serve essentially every map), and the **clip vocabularies** read off
// out/npc/clips/<stem>.json. What is NOT cached here is the resolved UAnimSequence: each one is
// baked against exactly one USkeleton, and the meshes that carry it are per-map-epoch, so those
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
	/** Native model preparation is a loading-phase operation; resolution below performs no asset I/O. */
	TSharedPtr<struct FStreamableHandle> PrepareNativeModel(const FString& ModelId, FString& OutError);
	void ReleaseNativeModels();
	const class UElysiumClipData* NativeClipData(const FString& Owner, const FString& Label,
		const FString& OwnerRoot = FString()) const;
	const FElysiumNpcClip* ClipDescription(const FString& Model, const FString& Label);

	// out/npc/npc_index.json, loaded once. Empty when the NPC export has not been run.
	const FElysiumNpcIndex& GetIndex();
	// out/npc/clips/<Stem>.json, cached per stem. Null when the stem has no slice.
	const FElysiumNpcClipSet* GetClipSet(const FString& Stem);
	// A supplied V2 mesh provides cooked data, cached by asset path; incomplete V2 data warns
	// and never falls through to loose files. The legacy stem route remains until cast cutover.
	// Null for a model with no flex rig, which
	// is the normal case for animals, crowd bodies and every player body — the caller animates the
	// body and leaves the face still. Shared rather than raw: an anim instance holds one for as long
	// as its body lives, across map epochs this GI-scoped cache outlasts.
	TSharedPtr<const FElysiumFacialRig> GetFacialRig(const FString& Stem, const USkeletalMesh* Mesh = nullptr);
	// The eyeball pair for a stem: `npc/eyes/<stem>.json`. Same shape and lifetime as
	// GetFacialRig — shared, immutable once built, GI-scoped. Answered independently of the flex
	// rig, because a player body carries a pair of eyeballs and no flex rig at all.
	//
	// Both sidecars are Unreal-native, in the frame the baked body is in, so a rig is read verbatim
	// and nothing here converts.
	TSharedPtr<const FElysiumEyeSet> GetEyeSet(const FString& Stem, const USkeletalMesh* Mesh = nullptr);
	// The two composition stages' rig for a stem: `npc_index.json`'s `split_bones` plus
	// `npc/procedural/<stem>.json`. Null when the model declares neither, which is a normal load —
	// the body then poses under Unreal's ordinary hierarchy composition.
	// Same shape and lifetime as GetFacialRig: shared, immutable once built, and GI-scoped so it
	// outlives the map epoch the skeleton belongs to. Same frame rule as above.
	TSharedPtr<const FElysiumCompositionRig> GetCompositionRig(const FString& Stem, const USkeletalMesh* Mesh = nullptr);
	// The same for a v4 animated prop, which indexes separately and whose sidecar sits under
	// animated_props/.
	TSharedPtr<const FElysiumCompositionRig> GetAnimatedPropCompositionRig(const FString& ModelPath, const USkeletalMesh* Mesh = nullptr);
	// Retail's per-body bank bone-remap table (`vampire.dll FUN_100c67b0`) for one CLOSURE's own
	// (mesh, source skeleton, retarget source) tuple — never for a body "stem": a cinematic body's
	// per-actor bank is not in any manifest, so a stem-keyed cache would miss it by construction.
	// There is no sidecar; this BUILDS the table from
	// `SourceSkeleton->AnimRetargetSources[RetargetSource]` (the donor bind registered on the playing
	// sequence's bank-family skeleton) against `Mesh->GetRefSkeleton()` (the playing body's own bind)
	// the first time this tuple is asked for, and caches the result for the life of the game instance.
	// Null is the ordinary "nothing to correct" case when every bone copies. A named source missing
	// from its sequence skeleton is a broken baked prerequisite and warns.
	TSharedPtr<const FElysiumBankRemap> GetBankRemap(USkeletalMesh* Mesh,
		USkeleton* SourceSkeleton, FName RetargetSource);
	// The blend spaces a stem declares: `npc/blends/<stem>.json`. Null for every model whose
	// sequences each name a single animation, which is most of them and a normal load. The stem may
	// be a character, a bank or an animated prop — all three can declare grids.
	TSharedPtr<const FElysiumBlendTable> GetBlendTable(const FString& Stem);
	// vdata/system/dispositiontable.txt, loaded once.

	// One named clip off the baked mount, resolving which stem owns it through the stem's clip set.
	// Returns null and fills OutError when the mount does not carry it.
	//
	// **`Channel` is what the caller intends to POSE the clip as, and it is load-bearing.** This is
	// the montage/claim door — `PlayNpcClip`, a `scripted_sequence`'s `m_iszPlay`, `SetAnimation`,
	// the green room's clip reports, an NPC idle — and a clip that carries a baked bone mask
	// (`UElysiumAnimLayerMask`) is a partial-body layer whose unowned bones decode to a zero
	// quaternion and a zero position. Posed as the BASE pose it collapses the body; composed on a
	// layer channel it is exactly what it is for. So a Base-channel caller is refused here rather
	// than handed a clip that poses wrong, through the same `RefuseMaskedBase` funnel
	// `ResolveAnimation` uses.
	//
	// The criterion is the metadata and never the name: the bake marks a clip masked when any bone
	// on the emitted skeleton has `weight`@0 == 0, so an ordinary-looking label can carry one and a
	// `_layer` suffix is neither necessary nor sufficient.
	UAnimSequence* ResolveClip(const FString& Stem, const FString& ClipName, USkeletalMesh* Mesh,
		FString& OutError, EElysiumAnimChannel Channel = EElysiumAnimChannel::Base);

	// Retarget a clip out of a NAMED bank, bypassing the clip vocabulary. A choreo scene's
	// `entire_scene` lives in a cinematic anim set that no NPC's include tree mentions, so there is
	// no vocabulary entry to look it up by; the scene knows the bank because it knows its own
	// `BaseAnim` and the actor's `bonerename` root.
	UAnimSequence* ResolveClipFromBank(const FString& BankStem, const FString& ClipName,
		USkeletalMesh* Mesh, FString& OutError, const FString& OwnerRoot = FString());

	// The animation a label actually plays on OwnerStem, which is the label itself for every
	// label that does not name a blend grid. **This is the only place a grid is collapsed to a cell**;
	// the three resolvers above and the animated-prop path all come through here, because a hook in
	// any one of them would silently leave the others playing the -180 degree base cell.
	//
	// OwnerStem is the stem whose glb carries the animation — a bank for most locomotion — not the
	// character asking for it, because the grid and its cells are declared by the owner. The returned
	// name is NOT in the character's clip vocabulary and must not be looked up there; it addresses an
	// animation in the owner's glb directly.
	FString ResolveGridClip(const FString& OwnerStem, const FString& Label,
		const FElysiumPoseParams& Pose = FElysiumPoseParams::Neutral(), const FString& OwnerRoot = FString());

	// The same answer for a label reached through a character's vocabulary, which is what finds the
	// owner. Exposed because a caller that caches the resolved sequence has to key its cache on the
	// name that will actually be loaded — resolving is two in-memory lookups, so asking twice is
	// cheaper than a cache keyed on a label that no longer identifies what it holds.
	FString ResolveClipAnimName(const FString& Stem, const FString& ClipName,
		const FElysiumPoseParams& Pose = FElysiumPoseParams::Neutral());

	// A label resolved to the whole grid rather than to one of its cells: the baked
	// `UBlendSpace`, plus what a caller needs to steer and label its axes. The axis metadata travels
	// with the asset because the blend space states its ranges in the pose parameter's own units but
	// not which parameter that is — the sidecar owns that binding, and re-deriving it from the axis
	// range would be guesswork on two parameters that share one.
	bool ResolveGrid(const FString& Stem, const FString& ClipName, USkeletalMesh* Mesh,
		struct FElysiumResolvedGrid& OutGrid, const FString& Host = FString(),
		FString* OutError = nullptr, FString* OutArmed = nullptr);

	// The front door. One intent in, one selection record out, plus whatever of it could be
	// bound to `Mesh`. **This is the only resolver**: the player path and the NPC motor both come
	// through it, and `ResolveActivityClip` below is expressed over it, because two implementations of
	// one pick are how the player and the cast come to disagree about a bank silently.
	//
	// `Mesh` may be null — the record is still complete, and `OutAssets` simply comes back empty.
	// `docs/architecture/animation-architecture.md` section 3.3.
	//
	// `Overlay` is the body's standing overlay stack (retail's `CBaseAnimatingOverlay`), or null when
	// nothing is layered on this body. It is a parameter rather than a field of the intent because it
	// is a SECOND set of requests on the same body: the intent describes what the base channel is
	// asking for, and a layer composes over whatever answer that gets rather than participating in
	// it. Its clips are resolved here, and not in the pure resolver, because the thing that decides a
	// layer is playable is its baked bone mask — metadata that only exists once the asset is loaded.
	//
	// It has no default: a caller that does not state it is a caller whose layers nobody looked for,
	// and a silently dropped layer is a body that stops shooting with nothing saying so.
	void ResolveAnimation(const FElysiumAnimationIntent& Intent, USkeletalMesh* Mesh,
		FElysiumAnimationSelection& OutSelection, FElysiumResolvedAnimation& OutAssets,
		const FElysiumOverlayStack* Overlay);

	// The overlay slot's own resolution: the layer clip the claim named and its baked mask, written
	// onto the record and the assets beside whatever the base channel resolved to.
	//
	// `SlotIndex` names which of the four rows the answer lands in — composition order, never a
	// packing order, so a free slot between two live ones stays free.
	//
	// **A separate door because a slot is a separate request, not a second base resolver.** It
	// reads and writes nothing the base selection owns — a miss here leaves the base pose exactly as
	// it resolved — and it depends on no rung of the base ladder, which is why `ResolveAnimation`
	// above runs it ahead of every one of them. `FElysiumAnimationDriver::ResolveSlotClaim` calls it
	// directly on the two `Tick` exits that publish a record without re-entering the base pass at
	// all; routing those through `ResolveAnimation` instead would re-run a selection those exits
	// exist to skip.
	void ResolveSlotLayer(int32 SlotIndex, const FElysiumAnimationRequest& Claim, const FString& Stem,
		USkeletalMesh* Mesh, FElysiumAnimationSelection& OutSelection,
		FElysiumResolvedAnimation& OutAssets);

	// The slot clip's own declared layers — its aim grid (with the grid's mask) and its `_delta`
	// additive — resolved by the same autolayer rule the base channel's host gets. Public because the
	// arm seam resolves the same trio at trigger-pull time: an arm that staged only the sequence
	// would pose one frame of bare shot before the driver's first publish filled the rest in.
	void ResolveSlotDeclaredAssets(const FString& OwnerStem, const FString& Label,
		USkeletalMesh* Mesh, TObjectPtr<UBlendSpace>& OutAimSpace, FName& OutAimMaskName,
		TObjectPtr<UAnimSequence>& OutAdditive);

	// The catalog view the resolver reads, gathered from this subsystem's own caches. Exposed so a
	// caller that resolves repeatedly does not re-enter the cache lookups, and so the Content tier can
	// run the same pure resolver over real sidecars.
	FElysiumAnimationCatalog BuildCatalog(const FString& Stem);

	// The three gait fans as per-direction speed tables, which is what the mover steers by.
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
	// neutral blend-grid cell. `Out.Label` is the vocabulary key (for example `walk`) that preserves
	// bank ownership for playback; `Out.AnimationName` is the concrete glb animation (`walk_0`), the
	// speed is zero when that cell carries no authored movement metadata, and `Out.bLooping` is the
	// selected row's own flag.
	//
	// The request states the whole translation context — body kind, actor classname, weapon
	// classname, actor state — because the translation forks on every one of them: a resolve that
	// assumed any would answer for a different body than the one being posed.
	bool ResolveActivityClip(const FElysiumActivityClipRequest& Request, FElysiumActivityClip& Out);

	// The one cell a body that cannot evaluate a fan collapses one onto: the grid resolved at
	// `AxisValue` on the axis it binds, taken to the NEARER of the two cells the parameter sits
	// between (`ElysiumBlendGrids::NearerCell`, which owns the arithmetic). Empty when the label names
	// no grid, which tells the caller there was nothing to collapse.
	//
	// It is not a second grid resolver: every body with a reaction branch plays the fan itself, and
	// this exists only so a graphless one poses a neighbour of the direction it was hit from instead
	// of the fan's own base cell.
	FString ResolveNearestGridClip(const FString& OwnerStem, const FString& Label, float AxisValue);

	// The label-route sibling: ClipName is already exact (a scripted m_iszCustomMove and the like),
	// so no weighted choice and no translation run over it. OutAnimName is the concrete cell ClipName
	// resolves to -- itself, unless ClipName names a blend grid -- and the speed is zero when that
	// cell carries no authored movement metadata. BodyKind still travels, because the record it
	// produces names the chain the body belongs to.
	bool ResolveSequenceClip(const FString& Stem, const FString& ClipName,
		EElysiumAnimBodyKind BodyKind, FString& OutAnimName, float& OutGroundSpeedCmPerSecond);

	// The standing idle for a stem at a disposition, by VtMB's own chain:
	//   default_disposition -> dispositiontable "Animation Name" -> Stance_<Name>_Idle_* (by weight)
	//   -> ACT_IDLE (by weight) -> a loose idle-named clip -> nothing.
	// Selection is by **activity**, never by label substring: `regular_cop` resolves 229 clips with
	// "idle" in the name, of which `Stance_Dead_Idle_1` and `Bed_Left_Idle` are not standing idles.
	// `Variant` means different things per tier, because the tiers are different kinds of set. On the
	// stance tier it is `m_CurrStance` and **addresses** a cell of the disposition's own table; on the
	// weighted tiers there is no index to address and it is ignored.
	FString PickIdleClip(const FString& Stem, const FString& Disposition, EElysiumIdleTier& OutTier,
		int32 Variant = 0, int32 DispositionLevel = 1);

	// One model's disposition stance set for `AnimName` — three idles, three fidgets and the 3x3
	// transition matrix, with retail's precache fallback ladder applied. The single owner of the
	// `Stance_<Anim>_*` naming: both the standing-idle resolution above and the substrate's stance
	// machine read this rather than deriving the labels twice.
	bool ResolveStanceClips(const FString& Stem, const FString& AnimName,
		struct FElysiumStanceClips& OutClips);

	// Every candidate the idle policy considered, best first — the debug/verification view.
	TArray<FString> IdleCandidates(const FString& Stem, const FString& Disposition,
		EElysiumIdleTier& OutTier, int32 DispositionLevel = 1);

	static const TCHAR* TierName(EElysiumIdleTier Tier);

private:
	// The disposition table belongs to the rulebook, like every other `vdata/system` catalog. This
	// is the local reach for it — the two idle resolvers below need the `Animation Name` column.
	const FElysiumDispositionTable& Dispositions();

	FElysiumNpcIndex Index;
	bool bIndexLoaded = false;

	// Value is null for a stem whose slice is missing, so a failed read is remembered rather than
	// retried on every NPC that shares the stem.
	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> ClipSets;
	// Same shape, same reason: a null entry is the remembered "this model has no flex rig".
	TMap<FString, TSharedPtr<const FElysiumFacialRig>> FacialRigs;
	// Eye and composition sidecars are authored in the same Unreal-native frame as the baked body,
	// so the owning stem is the complete cache identity.
	TMap<FString, TSharedPtr<const FElysiumEyeSet>> EyeSets;
	// Character rigs are keyed by stem; animated-prop rigs use the normalized model's indexed stem.
	TMap<FString, TSharedPtr<const FElysiumCompositionRig>> CompositionRigs;
	// The bank bone-remap table, keyed by (playing mesh, source skeleton, retarget source) rather than
	// by stem — a cinematic body's per-actor bank has no stem any manifest names, and the same source
	// name on two bank-family skeletons does not identify the same donor pose. `TObjectKey` is a
	// stable, non-owning identity: it never keeps either asset alive and never dangles, so a stale
	// entry from a dead map epoch is a harmless dead key rather than a crash. A null entry is the
	// common "every bone copies" case, not the exception.
	using FBankRemapKey = TPair<TObjectKey<USkeletalMesh>,
		TPair<TObjectKey<USkeleton>, FName>>;
	TMap<FBankRemapKey, TSharedPtr<const FElysiumBankRemap>> BankRemaps;
	// Blend spaces keyed by the OWNING stem — a bank serves every character that
	// resolves a clip out of it, so this is parsed once for the whole cast rather than per NPC.
	TMap<FString, TSharedPtr<const FElysiumBlendTable>> BlendTables;

	// A request that binds no asset says so, once per (stem, request, outcome). Once, because a
	// resolve runs on every selection change across the whole cast and the same body asking the same
	// thing again is the same failure — a per-frame line would bury the first one. But at least
	// once: a miss that only reaches the selection record is visible to whoever opens Cog, and a
	// play session where the cast poses nothing has to be readable in an ordinary log.
	void ReportMiss(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationSelection& Selection);
	TSet<uint32> ReportedMisses;

	// Slot layers already reported, once per (stem, label, reason). A layer is re-resolved on every
	// shot, so a body firing a clip the mount does not carry would otherwise warn per trigger pull.
	TSet<uint32> ReportedSlotMisses;

	// **A bone-masked partial-body layer may never own the base pose**, the sibling of the pure
	// resolver's additive refusal (`ElysiumAnimResolve`, `EElysiumAnimOutcome::MaskedRejected`). A
	// masked clip's unowned bones decode to a zero quaternion and a zero position, so posing one as
	// the base collapses the character — which is degenerate in retail too.
	//
	// It lives here rather than in the pure resolver because the evidence does: `FElysiumNpcClip`
	// carries the raw studio sequence bits and nothing else, while the mask is `UElysiumAnimLayerMask`
	// metadata the bake writes onto the sequence and only a loaded asset can answer. Returns whether
	// the base was refused, in which case the record names no asset and nothing is posed.
	//
	// A blend-space base is asked through its base cell, mirroring the layer path: every cell of a
	// grid shares one mask, and the `UBlendSpace` itself carries none.
	bool RefuseMaskedBase(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog, USkeletalMesh* Mesh,
		FElysiumAnimationSelection& OutSelection, FElysiumResolvedAnimation& OutAssets);

	// And the same shape for a gait fan that would not resolve. A body whose fans do not resolve
	// rides the stated `speed_walk`/`speed_runbase` constants for the rest of its life while its
	// record keeps naming a cell, which is the speed authority quietly becoming two numbers. Once
	// per (stem, gait, reason): the resolve re-runs on every equip and every state change across the
	// whole cast, and the same body failing the same way again is the same fact.
	void ReportGaitFanMiss(const FElysiumGaitSpeedRequest& Request, EElysiumAnimActivityCode Code,
		const TCHAR* Reason, const FElysiumAnimationSelection& Selection);
	TSet<uint32> ReportedGaitMisses;
};
