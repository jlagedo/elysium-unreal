#pragma once

#include "CoreMinimal.h"

#include "ElysiumWieldTable.h"

struct FReferenceSkeleton;

// How a wield model is placed on its wearer, decided from the binding the bake wrote.
//
// **The binding is data, and it selects a mechanism.** `EElysiumWieldBinding` states one of five,
// and they are not interchangeable: leader-pose drives the model bone-by-bone from the wearer's
// same-named bones, while a socket binding attaches it rigidly.
//
// **This is a representation change, not a reproduction.** Retail has exactly ONE binding --
// `MOVETYPE_FOLLOW` plus `m_hAimEnt`, with a per-bone name-matched copy every frame -- and derives
// everything else from which names happen to match. The five-way enum is a bake-time classification
// of the model's own topology, chosen because a rigid attach costs nothing where retail's per-frame
// name scan would produce the same answer. It is faithful where the model's sub-rig holds still,
// which the corpus says it does: 57 of 61 single-mount models carry one near-constant `idle01` and
// nothing ever sends the wield entity another sequence. Detail: `docs/vtmb/wielded_weapons.md`.
//
// The socket arithmetic is exact against retail's client merge, and that is the merge on screen.
// Retail's SERVER carries a second, different one -- it filters leader bones by `flags & 0x0C`,
// which rejects every prop bone -- so a server-side attachment query mid-swing disagrees with the
// drawn frame. Nothing here reproduces that, deliberately: the drawn frame is the answer.
//
// Pure: reference skeletons in, a placement out. No component, no world, no asset -- which is what
// lets `Elysium.Substrate.WieldAttach` assert every rule on the stack.
namespace ElysiumWieldAttach
{
	enum class EPlacement : uint8
	{
		/** Driven bone-by-bone from the wearer's matched bones. */
		LeaderPose,
		/** Attached rigidly to `AnchorBone` at `Relative`, evaluating no pose of its own. */
		RigidToBone,
		/** Nothing is worn -- `Reason` says why. Never a silent no-op. */
		Refused,
	};

	struct FPlan
	{
		EPlacement Placement = EPlacement::Refused;

		/**
		 * The WEARER bone this model attaches to. The same name exists on both skeletons: it is the
		 * bone whose two copies the placement brings into coincidence.
		 */
		FName AnchorBone;

		/**
		 * The wield component's transform relative to `AnchorBone`.
		 *
		 * One rule for both socket bindings: **align the model's own copy of the anchor with the
		 * wearer's.** Attaching to a bone gives `ComponentToWorld = Relative * BoneWorld`, so the
		 * model's anchor lands at `AnchorRefCS * Relative * BoneWorld`; setting `Relative` to the
		 * inverse of the model's own component-space reference transform of that bone makes the two
		 * coincide exactly, and every other bone of the model rides along rigidly.
		 */
		FTransform Relative = FTransform::Identity;

		/** Why, when refused. Empty otherwise. */
		FString Reason;

		/**
		 * Set when a prop carry anchored on the hand because the wearer declares no bone for the
		 * prop -- `item_w_bush_hook` on a female body, whose skeleton carries no `bush hook`. It is
		 * a worn answer, not a failure, but it is a different one from what the manifest asked for
		 * and the installer says so once rather than placing it quietly.
		 */
		bool bAnchoredOnHandInstead = false;

		bool IsWorn() const { return Placement != EPlacement::Refused; }
	};

	/**
	 * The placement `Binding` asks for, or a refusal naming what was missing.
	 *
	 * `MountBone` and `HandBone` are the bake's own answer for the model (`wield_models.json` ->
	 * `mount_bone` / `hand_bone`). A socket binding anchors on the bone the wearer shares: the hand
	 * for `SocketHand`, the prop bone itself for `SocketProp`.
	 */
	FPlan Resolve(EElysiumWieldBinding Binding, const FReferenceSkeleton& WieldRef,
		const FReferenceSkeleton& WearerRef, FName MountBone, FName HandBone);

	/**
	 * A bone's transform in a reference skeleton's own component space. Identity for a bone the
	 * skeleton does not carry, which every caller here has already excluded.
	 */
	FTransform RefPoseComponentSpace(const FReferenceSkeleton& Ref, int32 BoneIndex);

	/**
	 * The nearest ancestor of `BoneName` -- itself included -- that `WearerRef` also carries, or
	 * `NAME_None`.
	 *
	 * This is what a leader-pose binding actually runs the model's unmatched bones off: Unreal
	 * resolves an unmatched follower bone as its own reference local times its parent's resolved
	 * transform, so a bone's placement is decided by the first matched ancestor above it. A model
	 * whose geometry answers anything but the hand here is a model leader-pose cannot place.
	 */
	FName NearestSharedAncestor(const FReferenceSkeleton& WieldRef, const FReferenceSkeleton& WearerRef,
		FName BoneName);
}
