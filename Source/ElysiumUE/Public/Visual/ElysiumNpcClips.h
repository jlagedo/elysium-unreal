#pragma once

#include "CoreMinimal.h"

#include "ElysiumComboChain.h"
#include "ElysiumMeleeEnvelope.h"
#include "ElysiumSwingRecord.h"
#include "ElysiumNpcClips.generated.h"

// The NPC animation vocabulary, read off the offline sidecars.
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
USTRUCT()
struct ELYSIUMUE_API FElysiumNpcClip
{
	GENERATED_BODY()

	// The stem whose glb carries the baked animation — the NPC itself, or a bank.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Owner;
	// The `ACT_*` literal the engine selects on; empty on a layer/plumbing sequence (a
	// `*_layer`/`*_delta` additive the engine composes rather than picks).
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Activity;
	// Weighted-random share among the clips sharing this activity. `idle01` carries 30 against
	// three fidgets at 1, which is how VtMB rests on the idle ~91% of the time.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Weight = 0;
	// Studio sequence bits (`docs/vtmb/mdl_v2531.md`). Bit 0 is STUDIO_LOOPING, bit 1 refuses a
	// transition, and bits 2 and 4 mark an additive layer together.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Flags = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Frames = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float Fps = 30.f;
	// This clip's GLOBAL sequence number in the body's own flat space — the index retail's
	// `LookupSequence` answers with, and the identity a label alone is not: a body's include tree
	// numbers every descriptor it reaches, and ten weapon banks declare
	// `stealth_success_attacker_shortvictim` between them.
	//
	// It is what orders an activity's candidates, because retail's collector emits them in
	// ascending order of it and both pickers resolve a tie by keeping the first candidate. A slice
	// written before the export stated it reads `INDEX_NONE`, and those rows keep their
	// (label, owner) order behind every row that carries one.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 RawIndex = INDEX_NONE;
	// The authored transition duration in seconds (`mstudioseqdesc_t`+0x264). 0.2 on almost every
	// shipped sequence; 0.3 on a handful of dialogue clips and 0.45/0.5 on the lying-down and
	// damaged stance idles. A pair of clips transitions over the LARGER of the two, which is why
	// this is carried per clip rather than tuned globally (`docs/vtmb/animation_and_movers.md`).
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float Fade = 0.2f;
	// The sequence's own melee reach in centimetres (`mstudioseqdesc_t`+0x2D0).
	// `CWeaponMelee::RequestActivity` reads it off every sequence the translated activity returns and
	// queries at the MAXIMUM (`docs/vtmb/combat-and-damage.md` § "Target acquisition, sequence commit
	// and recovery"). Zero means the sequence states none, which is every non-melee clip.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float ReachCm = 0.0f;
	// The `ACT_*` the ATTACKER plays when this sequence's swing is blocked (`mstudioseqdesc_t`+0x2E0).
	// Authored per sequence rather than derived from a direction: the blocked-reaction callback plays
	// what the attacker's current sequence descriptor stores, falling back to
	// `ACT_BLOCKED_REACTION_RIGHT` (`docs/vtmb/combat-and-damage.md` § "Block and stagger reactions").
	// Empty means the sequence names none.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString BlockedReaction;
	// The NEAR edge of the same band `ReachCm` closes (`mstudioseqdesc_t`+0x2CC). The cast-arm melee
	// selector scores a candidate's reach bit on `LowReachCm <= mag <= ReachCm`, inclusive at both
	// ends (`docs/vtmb/combat-and-damage.md` -> "The cast arm").
	//
	// **Its own statedness, and not the same population as `ReachCm`.** 516 descriptors state a low
	// edge against 581 stating a reach: 502 state both, 72 a reach with no low edge, and 14 a low
	// edge with the reach unset. Negative is the unstated answer here rather than zero, because two
	// shipped sequences author a genuine `0.0` — a band that starts at the body, which is a real
	// claim where a zero FAR edge would be a swing that can never reach.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float LowReachCm = -1.0f;
	// The authored attack envelopes the cast arm tests the enemy against
	// (`mstudioseqdesc_t`+0x2BC/+0x2C0). A DIFFERENT array from `Swings` below, with a different job
	// and no parallelism — see `Public/ElysiumMeleeEnvelope.h` for what its axes mean, which is the
	// one thing a consumer has to know before touching the numbers.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	TArray<FElysiumMeleeEnvelope> Envelopes;
	// The authored swing-contact records of this sequence's swing (`mstudioseqdesc_t`+0x2C4/+0x2C8).
	// This is where a melee attack stops being an animation and becomes one: `ReachCm` is the
	// distance the swing ACQUIRES at, and these are where and when it TOUCHES. Empty on every
	// sequence that declares none, which is all but 574 of the install's 14,012 descriptors — an
	// authored absence, and the reason a clip carrying none has no contact at all.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	TArray<FElysiumSwingRecord> Swings;
	// The chain half of the same authored block (`mstudioseqdesc_t`+0x2D4..+0x2F8): which direction
	// key selects this attack, which attack it hands off to, and the cycles bounding the hand-off.
	// Unstated on all but 208 of the install's descriptors, which is an authored absence — an attack
	// carrying none is a terminal one that no press can continue.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FElysiumComboChain Combo;

	// Whether this sequence states a reach at all. Zero is "no claim", not a zero-length swing, so a
	// caller maximising over an activity's sequences skips it rather than clamping to it.
	bool HasReach() const { return ReachCm > 0.0f; }
	// Whether this sequence states the band's near edge. Zero is a STATED value here, so the test is
	// against the negative sentinel rather than against zero the way `HasReach` is.
	bool HasLowReach() const { return LowReachCm >= 0.0f; }
	// Whether the cast arm has any envelope to score this sequence against.
	bool HasEnvelopes() const { return !Envelopes.IsEmpty(); }
	// Whether this sequence's swing can contact anything. Retail's walk is driven by the records
	// themselves, so a melee clip declaring none simply never opens a contact window.
	bool HasSwings() const { return !Swings.IsEmpty(); }
	// Whether the sidecar stated this sequence's combo block. Same shape as `HasSwings()`: an
	// unstated block is the ordinary case and also what an omitted column gives.
	bool HasCombo() const { return Combo.bStated; }
	// Whether this clip knows its own place in the flat sequence space.
	bool HasRawIndex() const { return RawIndex != INDEX_NONE; }

	// Authored duration. The rate is per clip and is not always 30 (54 of 1,502 surveyed
	// sequences are 18 fps, including `run`), so this is read rather than assumed.
	float Seconds() const { return Fps > 0.f ? static_cast<float>(Frames) / Fps : 0.f; }
	bool IsOwnedBy(const FString& Stem) const { return Owner.Equals(Stem, ESearchCase::IgnoreCase); }
	// An additive layer rather than a pose: what it stores is the *difference* from a base clip,
	// so playing it standalone folds the skeleton up instead of animating it. The engine composes
	// these on top of something else and never selects one, which is why they carry no activity.
	bool IsAdditive() const { return (Flags & 0x14) == 0x14; }
	// Bit 0 — STUDIO_LOOPING. `ResetSequenceInfo` derives `m_bSequenceLoops` from it, so whether a
	// clip loops is a property of the CLIP and not of whoever asked to play it: an authored loop
	// keeps looping however it was requested.
	bool IsLooping() const { return (Flags & 1) != 0; }
	// Bit 1 — this clip takes no transition. It snaps in AND drops every clip still fading out,
	// so it lands on a clean pose rather than over the tail of whatever it interrupted. 2,642 of
	// the 5,836 shipped sequences set it, most of them attacks, and it is much of why VtMB's
	// combat reads sharp rather than mushy.
	bool IsSnap() const { return (Flags & 0x2) != 0; }
	// What this clip asks a transition INTO it to take. Zero for a snap, so a caller can hand the
	// result straight to the animation host and let 0 mean "cut".
	float FadeSeconds() const { return IsSnap() ? 0.f : FMath::Max(0.f, Fade); }
};

// One clip's address inside a body's vocabulary.
//
// The label alone is not one. A body's include tree numbers every descriptor it reaches, and the
// shipped corpus repeats a label across banks freely: `stealth_success_attacker_shortvictim` is
// declared by all ten weapon banks with a different `ACT_SNEAKATTACK_..._<WEAPON>` on each, and
// `idle01` by both `misc` and `pc_idles` under one `ACT_IDLE`. Retail addresses a clip by the
// global sequence number its own tree built, whose stable offline equivalent is (owner, label) —
// which is exactly what `FElysiumAnimationSelection` already carries and what the bake already
// writes (`_banks/<owner>/A_<label>`).
//
// An empty `Owner` means "whichever the include tree reaches first", which is the answer every
// caller that names only a label is asking for.
struct FElysiumClipRef
{
	FString Label;
	FString Owner;

	bool IsEmpty() const { return Label.IsEmpty(); }
	// Stable across two runs of the same map: label first, then owner, so a tie-break never rides
	// on TMap iteration order.
	bool operator<(const FElysiumClipRef& Other) const
	{
		return Label == Other.Label ? Owner < Other.Owner : Label < Other.Label;
	}
	bool operator==(const FElysiumClipRef& Other) const
	{
		return Label.Equals(Other.Label, ESearchCase::IgnoreCase)
			&& Owner.Equals(Other.Owner, ESearchCase::IgnoreCase);
	}
};

// Label -> every clip that label names, in include-tree order.
//
// The first row is the tree's own first definition, so `Find(Label)` answers exactly what a
// single-owner map answered before; the rest are the copies an owner-blind map dropped. A slice
// written before the schema carried them parses as one row per label and behaves identically.
struct FElysiumClipTable
{
	TMap<FString, TArray<FElysiumNpcClip>> Rows;

	void Add(const FString& Label, FElysiumNpcClip Clip)
	{
		Rows.FindOrAdd(Label).Add(MoveTemp(Clip));
	}
	// The include tree's first answer for this label.
	const FElysiumNpcClip* Find(const FString& Label) const
	{
		const TArray<FElysiumNpcClip>* Found = Rows.Find(Label);
		return (Found != nullptr && !Found->IsEmpty()) ? &(*Found)[0] : nullptr;
	}
	// One named owner's copy, or null when that owner does not declare this label. An empty owner
	// asks for the first, which is what a label-only caller means.
	const FElysiumNpcClip* Find(const FString& Label, const FString& Owner) const
	{
		const TArray<FElysiumNpcClip>* Found = Rows.Find(Label);
		if (Found == nullptr)
		{
			return nullptr;
		}
		if (Owner.IsEmpty())
		{
			return Found->IsEmpty() ? nullptr : &(*Found)[0];
		}
		for (const FElysiumNpcClip& Clip : *Found)
		{
			if (Clip.Owner.Equals(Owner, ESearchCase::IgnoreCase))
			{
				return &Clip;
			}
		}
		return nullptr;
	}
	const FElysiumNpcClip* Find(const FElysiumClipRef& Ref) const
	{
		return Find(Ref.Label, Ref.Owner);
	}
	const TArray<FElysiumNpcClip>* FindAll(const FString& Label) const { return Rows.Find(Label); }
	// The first row, for a caller that has already established the label exists.
	const FElysiumNpcClip& operator[](const FString& Label) const { return Rows[Label][0]; }

	// Distinct labels. `RowCount` is the clips behind them, which is the larger number wherever a
	// bank repeats another bank's label.
	int32 Num() const { return Rows.Num(); }
	int32 RowCount() const
	{
		int32 Total = 0;
		for (const TPair<FString, TArray<FElysiumNpcClip>>& Pair : Rows) { Total += Pair.Value.Num(); }
		return Total;
	}
	bool IsEmpty() const { return Rows.IsEmpty(); }
	bool Contains(const FString& Label) const { return Rows.Contains(Label); }
	void Reset() { Rows.Reset(); }
	void Reserve(int32 Count) { Rows.Reserve(Count); }
	// Every declaring owner drops with the label: a caller removing a label is saying the body
	// cannot play it at all, not that one bank's copy went away.
	int32 Remove(const FString& Label) { return Rows.Remove(Label); }
	void GetKeys(TArray<FString>& OutLabels) const { Rows.GetKeys(OutLabels); }

	// Every (label, clip) pair, including the copies a label-keyed walk cannot reach.
	void ForEachClip(TFunctionRef<void(const FString&, const FElysiumNpcClip&)> Visit) const
	{
		for (const TPair<FString, TArray<FElysiumNpcClip>>& Pair : Rows)
		{
			for (const FElysiumNpcClip& Clip : Pair.Value) { Visit(Pair.Key, Clip); }
		}
	}

	auto begin() const { return Rows.begin(); }
	auto end() const { return Rows.end(); }
};

// One NPC's whole resolved vocabulary, off `out/npc/clips/<stem>.json` (~92 KB / ~1,360 clips).
struct FElysiumNpcClipSet
{
	FString Stem;
	// Label -> the clips that label names, in include-tree order. Keyed case-insensitively:
	// content spells a clip name however it likes (`m_iszPlay "Jump2"`,
	// `SetAnimation("showguns")`), and the label is the same name.
	FElysiumClipTable Clips;

	bool IsValid() const { return !Clips.IsEmpty(); }
	const FElysiumNpcClip* Find(const FString& Label) const { return Clips.Find(Label); }
	const FElysiumNpcClip* Find(const FString& Label, const FString& Owner) const
	{
		return Clips.Find(Label, Owner);
	}
	const FElysiumNpcClip* Find(const FElysiumClipRef& Ref) const { return Clips.Find(Ref); }

	// Every clip carrying Activity, unordered, addressed by (label, owner) — so two banks that
	// declare one label under different activities are two separate candidates, and two that
	// declare it under the same activity are both in the draw, which is what retail's number
	// space gives its own selector.
	TArray<FElysiumClipRef> ByActivity(const FString& Activity) const;
	// Whether any clip carries it. The weapon ladder's availability probe asks this once per rung
	// and never wants the labels, and `ByActivity` would allocate a list per rung to answer it.
	bool HasActivity(const FString& Activity) const;
	// The largest reach any clip answering Activity states, in centimetres.
	// `CWeaponMelee::RequestActivity` reads the reach off EVERY sequence the translated activity
	// returns and queries `FindEntityFOV` at the maximum, so the answer is a property of the activity
	// rather than of whichever variant the weighted pick lands on
	// (`docs/vtmb/combat-and-damage.md` § "Target acquisition, sequence commit and recovery").
	// Zero when no answering clip states one, which is the same "no claim" a single clip's zero means.
	float MaxReachCmForActivity(const FString& Activity) const;
	// Every ACT_DISPOSITION clip named `Stance_<AnimName>_Idle*` (the standing idles) or, with
	// bWantTransitions, `Stance_<AnimName>_Trans*` (the authored blends between two of them).
	TArray<FElysiumClipRef> StanceClips(const FString& AnimName, bool bWantTransitions = false) const;

	// Highest Weight first, then label and owner — the engine's resting pick among equals. Stable,
	// so the same NPC resolves the same clip every load.
	void SortByWeight(TArray<FElysiumClipRef>& Refs) const;

	// Parse out/npc/clips/<Stem>.json. Returns false and fills OutError on any failure.
	bool Load(const FString& InStem, FString& OutError);
	// Parse an already-loaded slice. The same schema gate as Load(), exposed so the column contract
	// — a row truncated at its last stated column, a legal null reach — can be asserted without
	// writing into $ELYSIUM_EXPORT_ROOT.
	bool LoadJsonText(const FString& InStem, const FString& JsonText, FString& OutError);

	// Every ACT_* literal this stem's vocabulary can answer, added to `Out` — the slice's own
	// intern table, without building the clip map. A corpus-wide question ("which activities does
	// any shipped body carry?") needs 166 of these and none of the per-clip columns, and the intern
	// table is exactly that set: the exporter interns an activity as it writes the clip that
	// carries it. The empty literal every plumbing sequence shares is skipped.
	static bool LoadActivities(const FString& InStem, TSet<FString>& Out, FString& OutError);
};

// out/npc/npc_index.json — every NPC and bank with its glb and counts, no clip maps (~34 KB).
struct FElysiumNpcIndexEntry
{
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
	// and when the sidecar omits the column.
	FString Eyes;
	int32   EyeballCount = 0;
	// StudioBone names whose Flags & 0x2 select retail split rotation/translation inheritance.
	// Optional in v3/v4 manifests; a sidecar that omits it retains conventional composition.
	TArray<FString> SplitRotationBones;
	// The procedural bone rule table sidecar, relative to out/npc ("procedural/<stem>.json", or
	// "animated_props/procedural/<stem>.json"), and how many driven bones it declares. Both
	// empty/zero on a model with no `ProcType == 1` bone, and when the sidecar omits the column.
	FString Procedural;
	int32   ProceduralBones = 0;
	// The blend-space sidecar, relative to out/npc ("blends/<stem>.json"), and how many multi-cell
	// sequences it declares. Both empty/zero on a model whose every sequence names a single
	// animation — most of them — and when the sidecar omits the column.
	FString Blends;
	int32   BlendGrids = 0;
	// How many of that same sidecar's sequences carry an event timeline. The sidecar is written for
	// any of the three payloads, so a non-zero count here with `BlendGrids` at zero is an ordinary
	// model that authors events and no grid. Zero when the sidecar omits the column.
	int32   EventSequences = 0;
};

// One cinematic anim set: the whole-cast performance a choreo scene's
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
	// Same as the character entry's: "animated_props/blends/<stem>.json", its grid count — empty on
	// every prop but `wolf_form`, which is the one skeletal prop declaring a multi-cell sequence —
	// and how many of its sequences carry an event timeline.
	FString Blends;
	int32 BlendGrids = 0;
	int32 EventSequences = 0;
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

	// The normalized cinematic-set record for a scene model path, or null.
	const FElysiumCinematicSet* FindCinematic(const FString& ModelPath) const;

	// The bank stem a scene's anim-set model + actor bonerename resolves to, or empty.
	FString CinematicBank(const FString& ModelPath, const FString& BoneRoot) const;

	// The v4 animated-prop record selected by a normalized source model path, or null. Version 3
	// indexes answer null for every model.
	const FElysiumAnimatedPropEntry* FindAnimatedProp(const FString& ModelPath) const;
	const FElysiumAnimatedPropEntry* FindPlacedModel(const FString& ModelPath) const;
};
