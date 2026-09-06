#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;

// Retail's per-body bank bone-remap table (`vampire.dll FUN_100c67b0`), built at LOAD TIME from two
// bind poses already on the baked assets -- never from a sidecar. There is no offline classifier to
// read: retail itself builds this table when a body's model loads, and so does this struct, from
// the same two inputs retail used.
//
// VtMB shares its ~250 animation banks across bodies whose bind poses differ. Retail resolves a
// per-bone correction ONCE, per (body, owning bank) pair -- never per clip -- from the bank's own
// bind-pose translation (`a`) and the body's own (`b`), against a threshold of 0.254 cm (retail's
// `0.01f`, a squared-length compare in Source units at `vampire.dll 0x10450aa4`):
//
//     |a - b| <= 0.254 cm            -> COPY        (unchanged -- absent from this table)
//     |a| <= 0.254 or |b| <= 0.254   -> TRANSLATE:   p += (b - a)
//     else                           -> SIMILARITY:  p = shortestArc(a -> b).RotateVector(p) * (|b|/|a|)
//
// **Rotation and scale are never touched, in any branch.** Only a bone's translation moves, which
// is what lets the whole correction apply as a flat, order-independent list of per-bone entries
// rather than a hierarchy walk.
//
// `a` is `Skeleton->AnimRetargetSources[Sequence->RetargetSource].ReferencePose` -- the donor bind
// pose the bake already registers per bank and stamps on every sequence it bakes
// (`Source/ElysiumUE/Private/Editor/ElysiumSkeletalBuild.cpp`, `RegisterRetargetSource` and
// `Sequence->RetargetSource = RetargetSource`), indexed by the SKELETON's own bone order. `b` is the
// PLAYING body's own `USkeletalMesh::GetRefSkeleton()` bind pose, indexed by the MESH's own bone
// order -- routinely a different order and count than the skeleton's, since a compatible group of
// bodies shares one `USkeleton` while each carries its own mesh. The two are correlated BY NAME.
//
// It is applied ONCE per include closure -- a decoded sequence together with its own autolayers,
// composed in the owning bank's space, corrected on the way out -- never per sequence. Unreal's
// `EBoneTranslationRetargetingMode::OrientAndScale` is exactly the third branch above applied PER
// SEQUENCE instead, which is wrong for the second: TRANSLATE is affine, so a base clip and an
// additive corrected separately double the offset ((a+t) + (b+t) = a+b+2t against retail's single
// a+b+t). The fix is to disable Unreal's own retargeting entirely and reproduce this table as a
// post-process over the composed closure -- `FAnimNode_ElysiumBankRemap`
// (`docs/architecture/animation-architecture.md`).

// Which transform category one of retail's four observable outcomes reduces to. Both binds near
// the origin and binds within the copy threshold are distinct retail paths but both reduce to Copy.
enum class EElysiumBankRemapBranch : uint8
{
	Copy,
	Translate,
	Similarity,
};

// One bone's TRANSLATE-branch correction: `p += Offset`, where `Offset` is retail's `b - a` in
// Unreal-native centimetres, read verbatim with no coordinate conversion.
struct FElysiumBankRemapTranslateEntry
{
	FName	Bone;
	FVector Offset = FVector::ZeroVector;
};

// One bone's SIMILARITY-branch correction: `p = Rotation.RotateVector(p) * Scale`, where `Rotation`
// is retail's shortest-arc `a -> b` and `Scale` is `|b| / |a|`.
struct FElysiumBankRemapSimilarityEntry
{
	FName Bone;
	FQuat Rotation = FQuat::Identity;
	float Scale = 1.f;
};

// One (body mesh, owning bank/retarget-source) closure's table. Only non-COPY bones appear; a bone
// the body's skeleton carries but neither list names takes the copy branch -- unchanged, which is
// the ordinary case for most of a shared bank's bones.
struct FElysiumBankRemap
{
	TArray<FElysiumBankRemapTranslateEntry>	 Translate;
	TArray<FElysiumBankRemapSimilarityEntry> Similarity;

	// Whether this table carries any correction at all. A body whose bind pose tracks the bank
	// closely enough that every bone copies answers false, which is the ordinary case for most
	// bodies against most banks.
	bool HasWork() const { return !Translate.IsEmpty() || !Similarity.IsEmpty(); }

	// Retail's own three-way decision (`vampire.dll 0x10450aa4`), as a pure function of the two
	// bind-pose translations -- no skeleton, no bone name, no pose. Exposed so the threshold itself
	// is directly testable against measured numbers, independently of `Build` below.
	static EElysiumBankRemapBranch ClassifyBranch(const FVector& BankBind, const FVector& BodyBind);

	// Build the table for one (mesh, source skeleton, retarget source) tuple from the two bind poses:
	// `BankBindPose`
	// is the retarget source's own reference pose (`FReferencePose::ReferencePose`), indexed by
	// `BankSkeleton`'s bone order; `MeshSkeleton` is the playing body's own reference skeleton. Every
	// bone `MeshSkeleton` carries is looked up BY NAME against `BankSkeleton` to read `a` -- a bone
	// the bank's own skeleton has never had (an appendix, a hair chain no other body shares) has
	// nothing to correct against and is skipped, the same silent absence a missing bone in the
	// evaluating pose is at runtime.
	static FElysiumBankRemap Build(const TArray<FTransform>& BankBindPose,
		const FReferenceSkeleton& BankSkeleton, const FReferenceSkeleton& MeshSkeleton);

	// A compatible group of bodies shares one `USkeleton`, so the skeleton's bone tree is the UNION
	// of every body's appendix -- Smiling Jack's beard chain, a hat's tassel, a coat's hem -- under
	// generic names (`Bone01`, `Bone07`) that recur from body to body with unrelated binds. A bank's
	// donor pose is indexed by that whole tree, so the entries for bones the bank itself never had
	// must say so, or `Build` reads another body's bind as the bank's `a` and corrects a bone the
	// bank never posed (the beard vanished into Jack's head on every shared clip). The bake writes
	// this sentinel -- zero scale, which no genuine bind carries and the remap never reads --
	// wherever the bank had no bone, and `Build` takes the copy branch for it.
	static FTransform AbsentBankBind()
	{
		return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	}
	static bool IsAbsentBankBind(const FTransform& BankBind)
	{
		return BankBind.GetScale3D().IsNearlyZero();
	}

	// Parse `{"translate": [{"bone", "offset"}...], "similarity": [{"bone", "rotation", "scale"}...]}`
	// directly -- no sidecar, no per-body/per-owner wrapper, because a caller already knows which
	// (mesh, source skeleton, retarget source) tuple this table answers for. Exists purely so a test
	// can state a table
	// inline with no baked asset at all; production never parses JSON for this struct.
	bool LoadJson(const FString& JsonText, FString& OutError);
};
