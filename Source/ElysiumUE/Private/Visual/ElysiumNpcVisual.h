#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UChaosClothComponent;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkeleton;
struct FElysiumWieldModelRef;

// The runtime's character-body surface. A body, its clips and its blend grids all come off the
// `/ElysiumBaked` mount, built offline from the `.eskm` containers `UE_mdl_skeletal.py` writes in
// the repo's canonical Source->Unreal frame. There is no second build of a character and no file
// parsed at load: a stem the export has not covered fails here by name.
namespace ElysiumNpcVisual
{
	// The mount's body for this stem, or null with OutError set when the export has not covered it.
	// `bPlayerMaterial` does NOT select a second asset — a stem has exactly one body package and one
	// skeleton, and the player permutation is the same sections driven through different material
	// parameters (`FElysiumContentPaths::BakedCharacterMesh`). The flag survives here only as a
	// discriminator in the runtime's own visual cache key.
	USkeletalMesh* LoadMesh(const FString& Stem, FString& OutError, bool bPlayerMaterial = false);

	// Declare every morph target a mesh carries as a morph-target *curve* on its skeleton. An anim
	// curve only reaches USkeletalMeshComponent::ActiveMorphTargets when the bone container flags
	// it, and the bone container takes those flags from this metadata — so without this the facial
	// track evaluates correctly and moves nothing. The mesh loader calls it; the bake calls it again
	// on the body's own skeleton so the metadata is serialised rather than rebuilt per load.
	void RegisterMorphTargetCurves(USkeletalMesh* Mesh);

	// Show or hide the generated garments led by this body, and suspend or resume their simulation.
	//
	// A garment is a separate full-surface renderer — for Sheriff it redraws both `sheriffbody2` and
	// `sheriffhead` while substituting only the simulated vertices — and component visibility does not
	// inherit from an attach parent, so hiding only the body leaves a complete character on screen.
	// Only cloth **led by this body** is gated: propagating to every child would turn independently
	// controlled particles back on. Both the entity's dormancy gate and the camera's draw policy go
	// through here, so the two cannot disagree about a garment.
	void GateLeaderCloth(USkeletalMeshComponent* Body, bool bShown);

	// The master an eye section is drawn with, or null when the policy content has not been
	// generated. Callers compare a built slot's base material against this to find the eye slots.
	UMaterialInterface* EyeMaster();

	// Whether the mount carries a body for this stem. There is no second build of a character, so
	// false means the export has not covered it and nothing will stand — asked ahead of a load by
	// callers that would rather report than fail.
	bool IsStemBaked(const FString& Stem);
	// One baked body / one baked clip off the /ElysiumBaked mount, or null when the bake has not
	// covered it. `Owner` is the stem that owns the clip — the body for its own dialogue clips, the
	// bank stem otherwise — and `ClipName` is the resolved animation name, after any blend-grid
	// cell selection, never the label. `Owner` alone is the whole address: a bank's clips sit under
	// the bank folder, a body's own clips under the body's stem, and a body never owns another
	// body's clip. `Mesh` is not part of that address — it is the on-mount body the sequence is
	// about to play on, so a null one resolves nothing rather than handing back a clip with no
	// rig behind it.
	USkeletalMesh* LoadBakedMesh(const FString& Stem, bool bPlayerMaterial = false);
	UAnimSequence* LoadBakedClip(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& ClipName);
	// One baked blend grid (ANM3), addressed the same way and for the same reason. Unlike a clip
	// this takes the LABEL — a grid is the thing a label names when it does not name one animation,
	// so there is no cell selection to resolve first. Null for every label that is one clip, which
	// is most of them, and for every body the bake has not covered.
	// `Host` names the clip a LAYER grid was composed with, empty for a standalone grid.
	class UBlendSpace* LoadBakedBlendSpace(const USkeletalMesh* Mesh, const FString& Owner,
		const FString& Label, const FString& Host = FString());

	// Attach this stem's generated garment, if its model authored one.
	//
	// VtMB simulates a garment in its renderer and substitutes the result over ordinary skinning;
	// the port bakes that authored payload into a `UChaosClothAsset` offline, so nothing of VtMB's
	// solve reaches the frame path (`docs/vtmb/secondary_motion.md`). What runs here is a stock
	// `UChaosClothComponent` following the body as its leader pose.
	//
	// Only the 60 installed models whose header carries the cloth flag have an asset at all, so a
	// miss is the ordinary case and leaves the body exactly as it was. Returns the component when
	// one was attached, null otherwise.
	UChaosClothComponent* InstallGarment(USkeletalMeshComponent* Body, const FString& Stem);

	// Install this stem's authored stock-AnimDynamics hair chains.
	//
	// The recipes are owner-authored presentation, tuned in the editor and tracked in
	// `/Game/ElysiumAuthored/Hair/DA_HairDynamics`; having an entry there is the whole scope gate,
	// so most of the cast simulates nothing and false is the ordinary answer. An authored chain the
	// body cannot run warns naming the stem and the chain, and only that chain is dropped.
	bool InstallHairDynamics(USkeletalMeshComponent* Body, const FString& Stem);

	// The component tag every installed wield model carries. A body's owner holds other skeletal
	// components — the body itself, and on the map actor every other character standing — so the tag
	// is what tells a sweep which ones are weapons.
	FName WieldComponentTag();

	// The wield model this body is holding, or null. Found by walking the owner's components rather
	// than cached: the body can be rebuilt under a caller that still holds a stale pointer.
	USkeletalMeshComponent* FindWieldModel(const USkeletalMeshComponent* Body);

	// Put a drawn weapon's geometry in this body's hand, replacing whatever it was already holding.
	//
	// VtMB evaluates the weapon's own pose and then overwrites every bone whose NAME matches the
	// wearer with the wearer's world matrix (`docs/vtmb/wielded_weapons.md`). `SetLeaderPoseComponent`
	// is that rule: it matches follower bones to the wearer by name with no shared-`USkeleton`
	// requirement, and gives a bone the wearer lacks a rigid offset from its nearest matched
	// ancestor — which, against the frame-0 reference pose the wield bake stores, is retail's own
	// composition. So a melee weapon rides the wearer's prop bone and a firearm rides the hand
	// through one mechanism, with no socket, no offset and no per-binding branch.
	//
	// Returns the component when one was attached, null when the mesh could not be loaded — which it
	// reports. `Context` names the caller's subject (an item classname, or a stem in the lab) and
	// appears in that report.
	USkeletalMeshComponent* InstallWieldModel(USkeletalMeshComponent* Body,
		const FElysiumWieldModelRef& Ref, const FString& Context);

	// Take away whatever this body is holding. Safe on a body holding nothing, which is why every
	// path that changes what a character wields calls it rather than testing first.
	void ClearWieldModel(USkeletalMeshComponent* Body);

}
