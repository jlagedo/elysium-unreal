#pragma once

#include "CoreMinimal.h"

#include "ElysiumAnimationIntent.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"

// Steps 4, 5 and 6 of `docs/architecture/animation-architecture.md` section 3.3 — resolve the model
// vocabulary, resolve the asset shape, publish the graph parameters (CCC4).
//
// Pure C++: a catalog view in, a selection record out. No UObject, no filesystem, no mesh — which is
// what lets `Elysium.Substrate.AnimationResolve` build a two-bank fixture on the stack and assert the
// thing that actually matters, that the same label reaches different banks on a player body and on a
// cast body. The engine half that turns the resolved label into a `UAnimSequence*`/`UBlendSpace*`
// lives on `UElysiumAnimSubsystem`.
//
// It sits in `Visual/` rather than beside the intent because the catalog types it reads are private,
// and because `CCC9` moves this whole cluster out of its NPC-named host together.

// Everything the resolver may read about a character, as a view rather than an owner.
//
// The blend table is a **callback and not a map** because the owning bank is not known until the
// weighted pick has run — `Clip->Owner` is the include DAG's offline answer — and one player body's
// DAG names dozens of banks. A caller cannot preload the right one, because which one is the answer.
struct FElysiumAnimationCatalog
{
	// The character's own resolved vocabulary. `Owner` on each clip is the whole of the bank-ownership
	// mechanism and is never re-derived here.
	const FElysiumNpcClipSet* Clips = nullptr;

	// The grid table an OWNING stem declares. Returning null is an ordinary answer, not a failure:
	// most labels name one animation and declare no grid at all.
	TFunction<const FElysiumBlendTable*(const FString& OwnerStem)> BlendTableFor;

	// A skeletal prop's own clips, for the `SetAnimation` route. Null on a character request; a prop
	// owns every clip it can play, so there is no bank indirection on this side.
	const FElysiumAnimatedPropEntry* PropClips = nullptr;

	bool IsValid() const { return Clips != nullptr || PropClips != nullptr; }
};

namespace ElysiumAnimResolve
{
	// What step 3 answered, in the shape the record keeps it.
	//
	// Every hop is retained because they are what a wrong pose is diagnosed from: the same request
	// reaches a different sequence set through a weapon, through the actor's class body, and through
	// neither, and a record that carries only the final answer cannot say which.
	struct FElysiumTranslationResult
	{
		// The logical request, un-translated. Retail's `m_Activity` stays this.
		FString Requested;
		// Virtual `+0x5dc`'s answer. Empty on the player, whose pinned order has nothing before the
		// weapon hook.
		FString PreTranslation;
		// The first and last weapon answers, kept apart because retail retains them separately —
		// they are rungs 3 and 1 of the availability ladder below.
		FString FirstWeaponActivity;
		FString WeaponActivity;
		// The latest CHANGED class/NPC answer, which is availability rung 2. Empty when no class row
		// rewrote anything, and empty on the player.
		FString ClassActivity;
		// What the model vocabulary is searched for.
		FString Resolved;
		// How many (class → weapon) passes ran. One on the player, whose chain is a single
		// weapon-then-actor pass; one to five on the cast.
		int32 Iterations = 0;
		// Which rung of the weapon ladder answered, 1-based over the rungs that declare the base.
		// Zero when no rung could be played, which is the untranslated answer retail's own empty
		// table gives.
		int32 WeaponRung = 0;
		// Which rung of `CAI_BaseNPC`'s four-way availability probe answered — final weapon answer,
		// class answer, first weapon answer, original request. Zero on the player, who has no such
		// probe, and zero on a cast request nothing could play.
		int32 AvailabilityRung = 0;
		// The answering weapon row's authored `required` bit. Reported, never acted on: the pinned
		// server translator never reads the third dword.
		bool bRequired = false;
		// Set when the probe found nothing and the ORIGINAL request was `ACT_RUN`, whose recovered
		// translated fallback is `ACT_WALK`.
		bool bRunToWalk = false;
		// Set when a class body carried a rule whose request family the RE never enumerated and that
		// rule could have applied. The walk cannot decide membership, so it leaves the request alone
		// and says so; a caller reports it rather than treating the request as an ordinary miss.
		bool bUnresolvedFamily = false;
		// Set when the walk reached the paired-action tail carrying a registered grapple base. The
		// variant needs role and counterpart state no locomotion request supplies.
		bool bGrappleUnresolved = false;
	};

	// Step 3 — the committed weapon and actor tables, applied in their witnessed order.
	//
	// **It needs the vocabulary.** `CBaseCombatWeapon::ActivityOverride` walks a weapon's ladder
	// front to back and accepts the first rung the body can actually play, so a translation that
	// cannot ask the body what it carries is not this translation: it would hand a glock-armed body
	// `ACT_WALK_RELAXED_GLOCK`, which no shipped model carries, instead of the pistol rung that
	// answers it.
	//
	// The two orders are `docs/vtmb/animation_and_movers.md` A.3. The player's is one pass —
	// `+0x5f4` then `+0x5e0`. The cast's is `+0x5dc`, the weapon translator whose first answer is
	// preserved, then up to five (`+0x5e0`, `+0x5f4`) alternations, then the four-way availability
	// probe. Which one a request takes is its BODY KIND, the same discriminator the fallback ladder
	// below already uses — retail forks on the receiver's own class, so the producer that asked
	// changes nothing about the chain.
	FElysiumTranslationResult TranslateActivity(const FElysiumAnimationIntent& Intent,
		const FElysiumAnimationCatalog& Catalog);

	// VtMB's own deterministic weighted choice, reproduced exactly: candidates sorted by label, each
	// weight floored at 1, and the seed `hash(stem lowered) ^ variant`. The activity route's own pick,
	// exposed so the rule can be asserted directly — every producer reaches it through `Resolve`,
	// because two entries into one pick are how the player path and the cast path come to disagree
	// about a bank silently.
	FString PickWeighted(const FElysiumNpcClipSet& Set, const FString& Activity, int32 Variant);

	// The activity seam's request, as the resolver's own intent (LIFE5).
	//
	// This is the whole of what `UElysiumAnimSubsystem::ResolveActivityClip` does before it calls
	// `Resolve`, split out so the forwarding is a pure function that can be asserted with no
	// subsystem, no game instance and no export corpus. Every field it carries is one a producer
	// cannot restate later: a reaction's hit angle steers the `hit_yaw` fan, and its cleared fallback
	// ladder is what stops a miss being answered with a disposition — an adapter that dropped either
	// would resolve a directional reaction at the fan's forward cell with a stance substituted under
	// it, and both failures look like content bugs.
	FElysiumAnimationIntent ActivityIntentFor(const FElysiumActivityClipRequest& Request);

	// The whole of steps 4, 5 and 6. Always fills `Out` — a record that resolved nothing still names
	// what it was asked for and why it missed, because "no pose" with no line explaining it is the
	// failure the record exists to prevent.
	void Resolve(const FElysiumAnimationIntent& Intent, const FElysiumAnimationCatalog& Catalog,
		FElysiumAnimationSelection& Out);

	// The three pose parameters VtMB declares, gathered from an intent. Named in one place so a grid
	// whose axis binds to `move_yaw` and a caller that writes `move_yaw` cannot drift apart.
	FElysiumPoseParams PoseFrom(const FElysiumAnimationIntent& Intent);

	// Which asset form a layer or grid arm actually loaded. Named on success so a console verb
	// cannot report a ride over a miss, and named on failure so each attempted form is in the line.
	enum class ELayerAssetForm : uint8
	{
		None,
		DerivedGrid,
		PlainGrid,
		DerivedSequence,
		PlainSequence,
	};

	// Hosts in `Table` that declare `LayerLabel`, sorted. Empty when the table is missing or the
	// label is unbound — the table fallback, and the miss report's host list.
	void CollectDeclaringHosts(const FElysiumBlendTable* Table, const FString& LayerLabel,
		TArray<FString>& OutHosts);

	// One host-resolution rule for every layer path: the sequence the body is standing on first,
	// the first sorted declaring host when that is empty. A standing label is the host even when
	// the table does not name it — the derived form is `<layer>@<standing>`, and picking a
	// different table host is how the lab used to stand the wrong derived asset.
	FString ResolveLayerHost(const FString& StandingSequence, const FElysiumBlendTable* Table,
		const FString& LayerLabel);

	// The miss line: label, owner, derived form, plain label, and the host that was tried (or that
	// the table did not supply). `Table` is what lets the line separate the two causes that produce
	// the same absent asset — a host that does not declare this layer at all, and a declaring host
	// whose derived form was never baked — which are different repairs.
	FString DescribeLayerAssetMiss(const FString& LayerLabel, const FString& LayerOwner,
		const FString& Host, const FElysiumBlendTable* Table);

	// What actually armed, for `gr_layer` / `gr_grid`.
	FString DescribeLayerArmedForm(ELayerAssetForm Form, const FString& Label, const FString& Host);
}
