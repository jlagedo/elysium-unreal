#pragma once

#include "CoreMinimal.h"

// The NPC animation vocabulary, read off the offline sidecars (roadmap 8.5, pipeline PL4).
//
// A VtMB NPC's own `.mdl` carries only its own clips (mostly dialogue); idle, locomotion and
// combat come from shared **animation banks** pulled in through the studiohdr include DAG
// (`docs/vtmb/animation_and_movers.md` A.7). `pipeline/src/elysium_pipeline/exporters/npc_export.py` resolves that DAG offline and
// writes, per NPC, which stem owns each clip label — so the runtime never walks includes, it
// looks a label up and is told which glb to load.
//
// Plain C++ (no UObject), like the rest of the entity substrate: these types hold *names*, and
// the UObject-side caches that turn a name into a USkeletalMesh/UAnimSequence live in
// UElysiumAnimSubsystem (banks, GI-scoped) and AElysiumMapActor (per-map-epoch).

// Well-known activity literals. VtMB stores `StudioSeqDesc.activity` as -1 on disk and lets the
// game DLL resolve the *name* at model load, so the name is the durable key (A.3).
namespace ElysiumActivity
{
	// The engine's plain resting idle. `pc_idles.idle01` is the canonical one.
	extern const TCHAR* Idle;
	// The disposition stance set: `stances.mdl` tags all 67 of its clips with this, named
	// `Stance_<Disposition>_Idle_<N>` and `Stance_<Disposition>_Trans_<A>_<B>`.
	extern const TCHAR* Disposition;
}

// One clip in an NPC's resolved vocabulary.
struct FElysiumNpcClip
{
	// The stem whose glb carries the baked animation — the NPC itself, or a bank.
	FString Owner;
	// The `ACT_*` literal the engine selects on; empty on a layer/plumbing sequence (a
	// `*_layer`/`*_delta` additive the engine composes rather than picks).
	FString Activity;
	// Weighted-random share among the clips sharing this activity. `idle01` carries 30 against
	// three fidgets at 1, which is how VtMB rests on the idle ~91% of the time.
	int32 Weight = 0;
	// Studio sequence bits (`docs/vtmb/mdl_v2531.md`). Bit 0 is STUDIO_LOOPING, bit 1 refuses a
	// transition, and bits 2 and 4 mark an additive layer together.
	int32 Flags = 0;
	int32 Frames = 0;
	float Fps = 30.f;
	// The authored transition duration in seconds (`mstudioseqdesc_t`+0x264). 0.2 on almost every
	// shipped sequence; 0.3 on a handful of dialogue clips and 0.45/0.5 on the lying-down and
	// damaged stance idles. A pair of clips transitions over the LARGER of the two, which is why
	// this is carried per clip rather than tuned globally (`docs/vtmb/animation_and_movers.md`).
	float Fade = 0.2f;

	// Authored duration. The rate is per clip and is not always 30 (54 of 1,502 surveyed
	// sequences are 18 fps, including `run`), so this is read rather than assumed.
	float Seconds() const { return Fps > 0.f ? static_cast<float>(Frames) / Fps : 0.f; }
	bool IsOwnedBy(const FString& Stem) const { return Owner.Equals(Stem, ESearchCase::IgnoreCase); }
	// An additive layer rather than a pose: what it stores is the *difference* from a base clip,
	// so playing it standalone folds the skeleton up instead of animating it. The engine composes
	// these on top of something else and never selects one, which is why they carry no activity.
	bool IsAdditive() const { return (Flags & 0x14) == 0x14; }
	// Bit 1 — this clip takes no transition. It snaps in AND drops every clip still fading out,
	// so it lands on a clean pose rather than over the tail of whatever it interrupted. 2,642 of
	// the 5,836 shipped sequences set it, most of them attacks, and it is much of why VtMB's
	// combat reads sharp rather than mushy.
	bool IsSnap() const { return (Flags & 0x2) != 0; }
	// What this clip asks a transition INTO it to take. Zero for a snap, so a caller can hand the
	// result straight to the animation host and let 0 mean "cut".
	float FadeSeconds() const { return IsSnap() ? 0.f : FMath::Max(0.f, Fade); }
};

// One NPC's whole resolved vocabulary, off `out/npc/clips/<stem>.json` (~92 KB / ~1,360 clips).
struct FElysiumNpcClipSet
{
	FString Stem;
	// Label -> clip. Keyed case-insensitively: content spells a clip name however it likes
	// (`m_iszPlay "Jump2"`, `SetAnimation("showguns")`), and the label is the same name.
	TMap<FString, FElysiumNpcClip> Clips;

	bool IsValid() const { return !Clips.IsEmpty(); }
	const FElysiumNpcClip* Find(const FString& Label) const { return Clips.Find(Label); }

	// Every clip carrying Activity, unordered.
	TArray<FString> ByActivity(const FString& Activity) const;
	// Every ACT_DISPOSITION clip named `Stance_<AnimName>_Idle*` (the standing idles) or, with
	// bWantTransitions, `Stance_<AnimName>_Trans*` (the authored blends between two of them).
	TArray<FString> StanceClips(const FString& AnimName, bool bWantTransitions = false) const;

	// Highest Weight first, then label — the engine's resting pick among equals. Stable, so the
	// same NPC resolves the same clip every load.
	void SortByWeight(TArray<FString>& Labels) const;

	// Parse out/npc/clips/<Stem>.json. Returns false and fills OutError on any failure.
	bool Load(const FString& InStem, FString& OutError);
};

// out/npc/npc_index.json — every NPC and bank with its glb and counts, no clip maps (~34 KB).
struct FElysiumNpcIndexEntry
{
	FString Glb;        // relative to out/npc ("gangmember_male_2.glb", "banks/x.glb")
	FString Model;      // the source .mdl, for diagnostics
	int32   Bones = 0;  // NPCs only
	int32   ClipCount = 0;
	// The facial flex rig sidecar, relative to out/npc ("facial/<stem>.json"), and how many glTF
	// morph targets the glb carries. Both empty/zero on a model with no flex rig — most of the
	// cast's animals, dancers and crowd bodies, and every player body.
	FString Facial;
	int32   MorphCount = 0;
	// The eyeball sidecar, relative to out/npc ("eyes/<stem>.json"), and how many records it
	// carries — two on every character model. Deliberately independent of `Facial`: 57 of the 59
	// player bodies carry eyeballs and no flex rig at all, so their irises aim while their lids
	// have no flexdesc to land on. Both empty/zero on a model with none (gibs, props, scenery)
	// and on any export predating manifest v5.
	FString Eyes;
	int32   EyeballCount = 0;
	// StudioBone names whose Flags & 0x2 select retail split rotation/translation inheritance.
	// Optional in v3/v4 manifests; an older sidecar therefore retains conventional composition.
	TArray<FString> SplitRotationBones;
	// The procedural bone rule table sidecar, relative to out/npc ("procedural/<stem>.json", or
	// "animated_props/procedural/<stem>.json"), and how many driven bones it declares. Both
	// empty/zero on a model with no `ProcType == 1` bone, and on any export predating CAP7.1.
	FString Procedural;
	int32   ProceduralBones = 0;
	// The blend-space sidecar, relative to out/npc ("blends/<stem>.json"), and how many multi-cell
	// sequences it declares. Both empty/zero on a model whose every sequence names a single
	// animation — most of them — and on any export predating CAP7.3.
	FString Blends;
	int32   BlendGrids = 0;
};

// One cinematic anim set (12.1 / PL16): the whole-cast performance a choreo scene's
// `BaseAnim`/`MaleAnim`/`FemaleAnim` names, split offline into one bank per bone root because a
// single clip carries several co-located skeletons. A scene actor's `bonerename "BipNN" "Bip01"`
// picks which root — and therefore which bank — is that actor's.
struct FElysiumCinematicSet
{
	FString Stem;
	// Root token (`Bip01`, `Bip02`, …) -> the bank stem holding that actor's copy of the clips.
	TMap<FString, FString> Roots;

	// The bank for an exact bonerename source. An empty source resolves only when this is an
	// unambiguous single-root cinematic; a wrong non-empty root never selects another actor.
	FString BankForRoot(const FString& Root) const;
};

// One baked clip of a skeletal prop. A prop owns every clip it can play — there is no bank
// indirection — so this carries the selection keys directly rather than an owner column.
struct FElysiumPropClip
{
	FString Name;
	// The `ACT_*` literal, empty on a plumbing sequence. 16 of the 19 exported prop models tag
	// nothing (`palmtree`, `drknobantique`) or tag `ACT_VM_IDLE` (every theatre cinematic prop),
	// so the activity pick usually misses and the index-0 fallback is what selects the rest pose.
	FString Activity;
	int32 Weight = 0;
	int32 Flags = 0;
	// Ordinal in the model's own sequence-declaration order. Index 0 is retail's rest-pose
	// fallback (`CBaseProp::Spawn`), which is why the export must not sort these by name.
	int32 Index = 0;
	int32 Frames = 0;
	float Fps = 30.f;
	// How far this clip carries the posed model from its origin, in the glb's own metres — the
	// sequence's authored bounding box reduced to a radius, reconciled at export against what
	// baked. Zero on a v4/v5 index and on any row the export could not vouch for, which reads
	// as "no claim" and leaves the mesh's bind-pose bounds alone.
	float BoundsRadiusMeters = 0.f;

	float Seconds() const { return Fps > 0.f ? static_cast<float>(Frames) / Fps : 0.f; }
	// RE35: `ResetSequenceInfo` derives `m_bSequenceLoops` from `GetSequenceFlags() & 1`.
	bool IsLooping() const { return (Flags & 1) != 0; }
};

struct FElysiumAnimatedPropEntry
{
	FString Stem;
	FString Glb;       // relative to out/npc — an inspection product; nothing the game loads
	FString Eskm;      // relative to out/npc, normally animated_props/<stem>.eskm
	FString Model;     // normalized source .mdl path
	FString StaticStem; // the map-baked SM_ stem whose materials/collision this body reuses
	FString ClipMode;   // "rest" (candidate clips only) or "full"
	bool bStaticEquivalent = false; // every possible rest pose equals storage geometry
	TArray<FString> RestCandidates;
	int32 Bones = 0;
	TArray<FString> SplitRotationBones;
	FString Procedural;
	int32 ProceduralBones = 0;
	// Same as the character entry's: "animated_props/blends/<stem>.json" and its grid count, empty
	// on every prop but `wolf_form`, which is the one skeletal prop declaring a multi-cell sequence.
	FString Blends;
	int32 BlendGrids = 0;
	// **Declaration order is semantic** — see FElysiumPropClip::Index. A v4/v5 index carries only
	// names, so those rows land here with Index set from the array position and no selection keys.
	TArray<FElysiumPropClip> Clips;

	const FElysiumPropClip* FindClip(const FString& Label) const;
	bool HasClip(const FString& Label) const { return FindClip(Label) != nullptr; }

	// The clip retail stands this model on at rest: `SelectWeightedSequence(ACT_IDLE)` falling
	// back to sequence index 0 (`CBaseProp::Spawn`, FUN_1018df70). Empty only when the model
	// bakes no clip at all, which is also the test a prop uses to keep its static mesh.
	FString RestSequence(int32 PlacementToken = 0) const;
};

struct FElysiumNpcIndex
{
	int32 ManifestVersion = 0;
	TMap<FString, FElysiumNpcIndexEntry> Npcs;
	TMap<FString, FElysiumNpcIndexEntry> Banks;
	// Keyed by the model path exactly as a scene's keyvalue spells it, lowercased/forward-slashed.
	TMap<FString, FElysiumCinematicSet> Cinematics;
	// v4 only. Version 3 is accepted and leaves this empty.
	TMap<FString, FElysiumAnimatedPropEntry> AnimatedProps;
	// v7: every non-character model placed by .ents or GAME_LUMP.
	TMap<FString, FElysiumAnimatedPropEntry> PlacedModels;

	bool IsValid() const { return !Npcs.IsEmpty(); }
	bool Load(FString& OutError);
	// Parse an already-loaded manifest. This is the same compatibility gate as Load(), exposed so
	// generated-content validation can cover old/new schema migration without rewriting $ELYSIUM_EXPORT_ROOT.
	bool LoadJsonText(const FString& JsonText, FString& OutError);

	// Absolute path to a bank's glb, or empty when the stem is not a known bank.
	FString BankGlbPath(const FString& BankStem) const;

	// The normalized cinematic-set record for a scene model path, or null.
	const FElysiumCinematicSet* FindCinematic(const FString& ModelPath) const;

	// The bank stem a scene's anim-set model + actor bonerename resolves to, or empty.
	FString CinematicBank(const FString& ModelPath, const FString& BoneRoot) const;

	// The v4 animated-prop record selected by a normalized source model path, or null. Version 3
	// indexes answer null for every model.
	const FElysiumAnimatedPropEntry* FindAnimatedProp(const FString& ModelPath) const;
	const FElysiumAnimatedPropEntry* FindPlacedModel(const FString& ModelPath) const;
};
