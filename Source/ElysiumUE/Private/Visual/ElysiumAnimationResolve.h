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
	// VtMB's own deterministic weighted choice, reproduced exactly: candidates sorted by label, each
	// weight floored at 1, and the seed `hash(stem lowered) ^ variant`. Exposed because
	// `UElysiumAnimSubsystem::PickActivityClip` is expressed over it — two implementations of one
	// pick are how the player path and the cast path come to disagree about a bank silently.
	FString PickWeighted(const FElysiumNpcClipSet& Set, const FString& Activity, int32 Variant);

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
