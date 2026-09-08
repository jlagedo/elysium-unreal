// How a wield model is placed on its wearer, and whether the corpus's own answer survives it.
//
// `EElysiumWieldBinding` states one of five mechanisms per model and the bake writes it from the
// manifest, but a binding only means something if the installer reads it. These two tests are the
// pair that makes that true: the first asserts the placement rules on the stack, and the second
// holds every shipped row to them against the real baked meshes.
//
// **The rule they exist to pin.** Leader-pose drives a model bone-by-bone from the wearer's
// same-named bones, and Unreal resolves a bone the wearer does NOT carry as that bone's own
// reference local times its parent's resolved transform -- so an unmatched bone is placed by the
// nearest ancestor the two skeletons share. A bone hanging off `Bip01` therefore rides the wearer's
// ROOT and never sees the arm, and a strafing gait's root moves 4.5 cm vertically per cycle.
//
// **No shipped model is broken by this, and the test does not claim one is.** `w_f_m37`,
// `w_f_dragonbreath` and `w_m_dragonbreath` do hang a `hands box` -> `stock` -> `pump handle`
// branch off `Bip01`, but that branch carries **zero skin influence** -- measured on the shipped
// bytes: `Box02` 832 verts and `Box01` 117 under `Bip01 R Hand`, the whole chain 0. It positions two
// attachment slots no shipped code path reads (`docs/vtmb/wielded_weapons.md`). The root-drift
// assertion below is a statement about the RESOLVER, checked on a synthetic skeleton, because the
// resolver has to be right for a model that does root its geometry that way whether or not one
// ships today.

#include "Misc/AutomationTest.h"

#include "ElysiumContentPaths.h"
#include "ElysiumModelCatalogues.h"
#include "Visual/ElysiumPreparedWieldModels.h"
#include "UObject/StrongObjectPtr.h"
#include "ElysiumWieldAttach.h"
#include "ElysiumWieldTable.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

static constexpr EAutomationTestFlags GElysiumWieldFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A reference skeleton built bone by bone, so the placement rules can be asserted without an
	// asset, a mesh or a world.
	struct FBoneSpec
	{
		const TCHAR* Name;
		const TCHAR* Parent;
		FVector Offset;
	};

	FReferenceSkeleton MakeSkeleton(TArrayView<const FBoneSpec> Bones)
	{
		FReferenceSkeleton Ref;
		{
			// **The index map is kept here, not read back off the skeleton.** A modifier has not
			// rebuilt the name lookup while it is open, so `FindBoneIndex` answers `INDEX_NONE` for
			// a bone added in the same session -- which reaches the modifier as a rootless child and
			// trips its one-root assertion rather than returning an error.
			TMap<FName, int32> Added;
			FReferenceSkeletonModifier Modifier(Ref, nullptr);
			for (const FBoneSpec& Bone : Bones)
			{
				const FName Name(Bone.Name);
				const FName Parent(Bone.Parent);
				const int32* Found = Parent.IsNone() ? nullptr : Added.Find(Parent);
				Modifier.Add(
					FMeshBoneInfo(Name, FString(Bone.Name), Found ? *Found : INDEX_NONE),
					FTransform(Bone.Offset));
				Added.Add(Name, Added.Num());
			}
		}
		return Ref;
	}

	// A wearer with an arm and one weapon bone it declares itself, which is the shape every
	// character bank ships (`Bat`, `handle`, `Sledgehammer` hang off the right hand).
	FReferenceSkeleton WearerSkeleton()
	{
		static const FBoneSpec Bones[] =
		{
			{ TEXT("Bip01"),          nullptr,                 FVector(0.0, 0.0, 95.0) },
			{ TEXT("Bip01 Spine"),    TEXT("Bip01"),           FVector(9.0, 0.0, 0.0) },
			{ TEXT("Bip01 R Hand"),   TEXT("Bip01 Spine"),     FVector(24.0, 0.0, 0.0) },
			{ TEXT("handle"),         TEXT("Bip01 R Hand"),    FVector(3.0, 0.0, 0.0) },
		};
		return MakeSkeleton(Bones);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldAttachTest,
	"Elysium.Substrate.WieldAttach", GElysiumWieldFlags)
bool FElysiumWieldAttachTest::RunTest(const FString&)
{
	const FReferenceSkeleton Wearer = WearerSkeleton();

	// A carry whose geometry roots at the model's ROOT through a bone no wearer has -- `w_f_m37`'s
	// own shape, reduced to what matters.
	static const FBoneSpec RootRootedBones[] =
	{
		{ TEXT("Bip01"),        nullptr,               FVector(0.0, 0.0, 97.0) },
		{ TEXT("Bip01 Spine"),  TEXT("Bip01"),         FVector(9.0, 0.0, 0.0) },
		{ TEXT("Bip01 R Hand"), TEXT("Bip01 Spine"),   FVector(24.0, 0.0, 0.0) },
		{ TEXT("Box02"),        TEXT("Bip01 R Hand"),  FVector(11.0, -1.7, 2.0) },
		{ TEXT("hands box"),    TEXT("Bip01"),         FVector(33.0, 3.4, 51.0) },
		{ TEXT("stock"),        TEXT("hands box"),     FVector(-9.4, 3.4, 5.1) },
	};
	const FReferenceSkeleton RootRooted = MakeSkeleton(RootRootedBones);

	// **The invariant that names the defect.** Leader-pose places an unmatched bone by the nearest
	// ancestor the two skeletons share, so this is where a model either can or cannot reach the
	// hand -- and it is a property of the two skeletons, not of the code that installs them.
	TestEqual(TEXT("a model rooting its geometry at Bip01 is placed by the wearer's ROOT under "
			"leader-pose, never by the hand"),
		ElysiumWieldAttach::NearestSharedAncestor(RootRooted, Wearer, TEXT("stock")),
		FName(TEXT("Bip01")));
	TestEqual(TEXT("...while its own grip box is placed by the hand"),
		ElysiumWieldAttach::NearestSharedAncestor(RootRooted, Wearer, TEXT("Box02")),
		FName(TEXT("Bip01 R Hand")));

	// A socket binding does not care where the geometry hangs: it anchors the model's own copy of
	// the shared bone onto the wearer's and everything rides rigidly.
	{
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::SocketHand, RootRooted, Wearer,
			TEXT("Box02"), TEXT("Bip01 R Hand"));
		TestEqual(TEXT("a socket_hand carry attaches rigidly"), Plan.Placement,
			ElysiumWieldAttach::EPlacement::RigidToBone);
		TestEqual(TEXT("...on the hand the two skeletons share"), Plan.AnchorBone,
			FName(TEXT("Bip01 R Hand")));

		// The whole point of the relative transform: the model's own hand lands exactly on the
		// wearer's, so every bone of the model arrives at the pose it was authored in relative to
		// that hand -- which is what retail's per-frame name-matched copy produces for a sub-rig
		// that holds still, at none of the per-frame cost.
		const int32 ModelHand = RootRooted.FindBoneIndex(TEXT("Bip01 R Hand"));
		const FTransform Landed =
			ElysiumWieldAttach::RefPoseComponentSpace(RootRooted, ModelHand) * Plan.Relative;
		TestTrue(FString::Printf(TEXT("the model's own hand lands on the wearer's (off by %.6f cm)"),
			Landed.GetLocation().Size()),
			Landed.Equals(FTransform::Identity, 1e-4f));
	}

	// A prop carry anchors on the bone the WEARER declares for it.
	{
		static const FBoneSpec PropBones[] =
		{
			{ TEXT("Bip01"),        nullptr,              FVector(0.0, 0.0, 97.0) },
			{ TEXT("Bip01 R Hand"), TEXT("Bip01"),        FVector(30.0, 0.0, 0.0) },
			{ TEXT("handle"),       TEXT("Bip01 R Hand"), FVector(3.0, 0.0, 0.0) },
			{ TEXT("blade"),        TEXT("handle"),       FVector(40.0, 0.0, 0.0) },
		};
		const FReferenceSkeleton Prop = MakeSkeleton(PropBones);
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::SocketProp, Prop, Wearer, TEXT("handle"), TEXT("Bip01 R Hand"));
		TestEqual(TEXT("a socket_prop carry anchors on the prop bone the wearer declares"),
			Plan.AnchorBone, FName(TEXT("handle")));
		const int32 ModelHandle = Prop.FindBoneIndex(TEXT("handle"));
		TestTrue(TEXT("...bringing the model's own prop bone onto the wearer's"),
			(ElysiumWieldAttach::RefPoseComponentSpace(Prop, ModelHandle) * Plan.Relative)
				.Equals(FTransform::Identity, 1e-4f));
	}

	// **A prop bone the wearer does not declare falls back to the hand, and says so.** The female
	// player skeleton carries no `bush hook`, so its row would otherwise have nothing to anchor on.
	{
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::SocketProp, RootRooted, Wearer,
			TEXT("hands box"), TEXT("Bip01 R Hand"));
		TestEqual(TEXT("a prop carry whose bone the wearer lacks still anchors, on the hand"),
			Plan.AnchorBone, FName(TEXT("Bip01 R Hand")));
		TestTrue(TEXT("...and records that it is not the bone the manifest asked for"),
			Plan.bAnchoredOnHandInstead);
		TestTrue(TEXT("...naming the bone that was missing"),
			Plan.Reason.Contains(TEXT("hands box")));
	}

	// **A refusal is named, never silent.** Each of these would otherwise draw a weapon at the
	// wearer's feet or at the world origin, which reads as a missing asset rather than a bad bind.
	{
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::SocketProp, RootRooted, Wearer,
			TEXT("hands box"), TEXT("Bip01 L Hand"));
		TestEqual(TEXT("a prop carry with neither its bone nor a usable hand is refused"),
			Plan.Placement, ElysiumWieldAttach::EPlacement::Refused);
		TestTrue(TEXT("...and the refusal names the bone the manifest asked for"),
			Plan.Reason.Contains(TEXT("hands box")));
	}
	{
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::SocketHand, RootRooted, Wearer, TEXT("Box02"), NAME_None);
		TestEqual(TEXT("a hand carry naming no hand bone is refused"), Plan.Placement,
			ElysiumWieldAttach::EPlacement::Refused);
	}
	{
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::Projectile, RootRooted, Wearer, TEXT("Box02"),
			TEXT("Bip01 R Hand"));
		TestEqual(TEXT("a projectile is never worn"), Plan.Placement,
			ElysiumWieldAttach::EPlacement::Refused);
	}
	{
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::None, RootRooted, Wearer, NAME_None, NAME_None);
		TestEqual(TEXT("a row carrying no geometry is not worn"), Plan.Placement,
			ElysiumWieldAttach::EPlacement::Refused);
	}

	// **The gun must not move when only the wearer's ROOT moves.** This is the symptom stated as a
	// number: the gait's root travels 4.5 cm vertically per strafe cycle, and a model placed by that
	// root rides the whole of it while the hands do not -- a weapon floating against the hand holding
	// its grip. Under a socket the model rides one bone and the displacement is exactly zero; under
	// leader-pose it is exactly the root's, because Unreal resolves an unmatched bone as its own
	// reference local times its parent's resolved transform.
	{
		// Where a model's geometry lands relative to the wearer's HAND, for a wearer posed with its
		// root somewhere and its hand somewhere else.
		//
		// **The two are supplied independently, and that is the whole point.** During a gait the
		// root bobs while the arm animation holds the weapon steady, so the hand does NOT inherit
		// the bob. Lifting both together would cancel the very displacement being measured.
		const auto GeometryInHand = [&Wearer](const FReferenceSkeleton& Model,
			const ElysiumWieldAttach::FPlan& Plan, FName Geometry, double RootLift)
		{
			const auto WearerBone = [&Wearer, RootLift](FName Bone)
			{
				const FTransform Rest =
					ElysiumWieldAttach::RefPoseComponentSpace(Wearer, Wearer.FindBoneIndex(Bone));
				// Only the root moves; the arm holds its pose, which is what the aim layer does.
				return Bone == FName(TEXT("Bip01"))
					? Rest * FTransform(FVector(0.0, 0.0, RootLift)) : Rest;
			};
			const FTransform HandWorld = WearerBone(TEXT("Bip01 R Hand"));

			if (Plan.Placement == ElysiumWieldAttach::EPlacement::RigidToBone)
			{
				const FTransform Component = Plan.Relative * WearerBone(Plan.AnchorBone);
				const FTransform Landed = ElysiumWieldAttach::RefPoseComponentSpace(
					Model, Model.FindBoneIndex(Geometry)) * Component;
				return Landed.GetRelativeTransform(HandWorld).GetLocation();
			}
			// Leader-pose: walk from the geometry up to the first bone the wearer shares, carrying
			// the model's own reference locals, then seat that on the wearer's copy of it.
			FTransform Local = FTransform::Identity;
			int32 Index = Model.FindBoneIndex(Geometry);
			while (Index != INDEX_NONE
				&& Wearer.FindBoneIndex(Model.GetBoneName(Index)) == INDEX_NONE)
			{
				Local = Local * Model.GetRefBonePose()[Index];
				Index = Model.GetParentIndex(Index);
			}
			const FTransform Shared = Index == INDEX_NONE
				? FTransform::Identity : WearerBone(Model.GetBoneName(Index));
			return (Local * Shared).GetRelativeTransform(HandWorld).GetLocation();
		};

		//: The strafe cells move `Bip01` this far vertically per cycle, measured on both banks.
		constexpr double GaitRootBobCm = 4.5;

		const ElysiumWieldAttach::FPlan Followed = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::LeaderPose, RootRooted, Wearer, TEXT("Box02"),
			TEXT("Bip01 R Hand"));
		const double FollowedDrift = FVector::Dist(
			GeometryInHand(RootRooted, Followed, TEXT("stock"), 0.0),
			GeometryInHand(RootRooted, Followed, TEXT("stock"), GaitRootBobCm));
		TestTrue(FString::Printf(
			TEXT("a root-rooted model FOLLOWED rides the wearer's root: the gun drifts %.3f cm ")
			TEXT("against the hand for %.1f cm of gait bob"), FollowedDrift, GaitRootBobCm),
			FollowedDrift > GaitRootBobCm * 0.9);

		const ElysiumWieldAttach::FPlan Socketed = ElysiumWieldAttach::Resolve(
			EElysiumWieldBinding::SocketHand, RootRooted, Wearer, TEXT("Box02"),
			TEXT("Bip01 R Hand"));
		const double SocketedDrift = FVector::Dist(
			GeometryInHand(RootRooted, Socketed, TEXT("stock"), 0.0),
			GeometryInHand(RootRooted, Socketed, TEXT("stock"), GaitRootBobCm));
		TestTrue(FString::Printf(
			TEXT("...and the same model SOCKETED does not move against the hand at all ")
			TEXT("(%.6f cm)"), SocketedDrift), SocketedDrift < 1e-3);
	}

	// Leader-pose and copy-pose are the one mechanism the manifest spells two ways.
	for (const EElysiumWieldBinding Binding :
		{ EElysiumWieldBinding::LeaderPose, EElysiumWieldBinding::CopyPose })
	{
		const ElysiumWieldAttach::FPlan Plan = ElysiumWieldAttach::Resolve(
			Binding, RootRooted, Wearer, NAME_None, NAME_None);
		TestEqual(TEXT("a leader-pose or copy-pose carry is driven by the wearer's bones"),
			Plan.Placement, ElysiumWieldAttach::EPlacement::LeaderPose);
	}
	return true;
}

bool 