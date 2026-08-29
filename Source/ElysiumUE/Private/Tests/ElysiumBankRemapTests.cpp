#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

	#include "ElysiumBankRemapNode.h"
	#include "Visual/ElysiumAnimSubsystem.h"
	#include "Visual/ElysiumBankRemap.h"

	#include "Animation/Skeleton.h"
	#include "BonePose.h"
	#include "Engine/GameInstance.h"
	#include "Engine/SkeletalMesh.h"
	#include "Misc/MemStack.h"
	#include "ReferenceSkeleton.h"

// Retail's per-body bank bone-remap (`vampire.dll FUN_100c67b0`), reproduced as
// `FElysiumBankRemap` (the classification and the build from two bind poses) and
// `FAnimNode_ElysiumBankRemap` (the translation-only correction applied over one closure's
// composed pose). There is no sidecar: retail itself builds this table at model load, from the
// donor bank's registered retarget source and the playing mesh's own bind, and so does
// `FElysiumBankRemap::Build`.
//
// The fixture is synthetic for the reason every fixture in this repo is: nothing game-sourced is
// committed. `Build` is exercised against two small hand-built reference skeletons; the node's
// correction is exercised against a table stated inline through `LoadJson`, because the node's
// contract (translate/similarity/copy, rotation and scale untouched, a named-but-absent bone
// skipped) does not depend on where the table came from.

static constexpr EAutomationTestFlags GElysiumBankRemapTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	USkeleton* BuildTestSkeleton(const TArray<FName>& Names, const TArray<FTransform>& Binds)
	{
		USkeleton* Skeleton = NewObject<USkeleton>();
		{
			FReferenceSkeletonModifier Modifier(Skeleton);
			for (int32 Index = 0; Index < Names.Num(); ++Index)
			{
				Modifier.Add(FMeshBoneInfo(Names[Index], Names[Index].ToString(),
								 Index == 0 ? INDEX_NONE : 0),
					Binds[Index]);
			}
		}
		return Skeleton;
	}

	FString QuoteJson(const FVector& V)
	{
		return FString::Printf(TEXT("[%.9f, %.9f, %.9f]"), V.X, V.Y, V.Z);
	}

	FString QuoteJson(const FQuat& Q)
	{
		return FString::Printf(TEXT("[%.9f, %.9f, %.9f, %.9f]"), Q.X, Q.Y, Q.Z, Q.W);
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBankRemapTest, "Elysium.Substrate.BankRemap",
	GElysiumBankRemapTestFlags)

bool FElysiumBankRemapTest::RunTest(const FString&)
{

	// Part 1 — `FElysiumBankRemap::ClassifyBranch` and `::Build`: the classification rule and the
	// correlate-by-name/skip-if-absent build, against the measured ash / Bip01 Pelvis numbers and a
	// small synthetic bank/mesh skeleton pair.


	// --- the branch rule against the measured ash/Bip01 Pelvis numbers ---------------------------
	//
	// Bank bind |a| = 2.968 cm (`(-2.945, -0.144, -0.344)`, the defect report's own vector); body
	// bind |b| = 0.145 cm, 177.2 degrees apart, ratio 0.04884 -- constructed by rotating the bank
	// bind by exactly 177.2 degrees about an axis perpendicular to it (which leaves the angle
	// between the two exact) and rescaling to the reported magnitude.
	const FVector BankPelvisBind(-2.945, -0.144, -0.344);
	TestTrue(TEXT("the bank bind reproduces the reported 2.968 cm magnitude"),
		FMath::IsNearlyEqual(BankPelvisBind.Size(), 2.968, 1e-3));

	const FVector PerpAxis =
		FVector::CrossProduct(BankPelvisBind, FVector::ZAxisVector).GetSafeNormal();
	const FVector BodyPelvisBind = FQuat(PerpAxis, FMath::DegreesToRadians(177.2))
									   .RotateVector(BankPelvisBind)
									   .GetSafeNormal()
		* 0.145;
	TestTrue(TEXT("the constructed body bind reproduces the reported 0.145 cm magnitude"),
		FMath::IsNearlyEqual(BodyPelvisBind.Size(), 0.145, 1e-4));
	const double AngleDeg = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(BankPelvisBind.GetSafeNormal() | BodyPelvisBind.GetSafeNormal(), -1.0, 1.0)));
	TestTrue(TEXT("the constructed pair sits 177.2 degrees apart"),
		FMath::IsNearlyEqual(AngleDeg, 177.2, 0.1));

	TestTrue(TEXT("ash's Bip01 Pelvis pair takes the TRANSLATE branch, not similarity or copy"),
		FElysiumBankRemap::ClassifyBranch(BankPelvisBind, BodyPelvisBind)
			== EElysiumBankRemapBranch::Translate);

	// --- Build: correlate by name, classify per bone, skip what the bank skeleton never had -------
	//
	// `root`/`spine` bind identically on both skeletons (COPY); `pelvis` takes the ash pair above
	// (TRANSLATE); `forearm` is rotated 35 degrees and scaled 1.3x from the bank's own bind
	// (SIMILARITY); `extra` exists only on the mesh, which the bank skeleton has never had.
	const FVector	 BankForearmBind(5.0, 0.0, 0.0);
	const FQuat		 ForearmRotation(FVector::ZAxisVector, FMath::DegreesToRadians(35.0));
	constexpr double ForearmScale = 1.3;
	const FVector	 MeshForearmBind = ForearmRotation.RotateVector(BankForearmBind) * ForearmScale;

	const FVector SharedSpineBind(0.0, 0.0, 30.0);

	USkeleton* BankSkeleton = BuildTestSkeleton(
		{ TEXT("root"), TEXT("pelvis"), TEXT("spine"), TEXT("forearm") },
		{ FTransform::Identity, FTransform(BankPelvisBind), FTransform(SharedSpineBind),
			FTransform(BankForearmBind) });
	USkeleton* MeshSkeleton = BuildTestSkeleton(
		{ TEXT("root"), TEXT("pelvis"), TEXT("spine"), TEXT("forearm"), TEXT("extra") },
		{ FTransform::Identity, FTransform(BodyPelvisBind), FTransform(SharedSpineBind),
			FTransform(MeshForearmBind), FTransform(FVector(9.0, 9.0, 9.0)) });

	const FElysiumBankRemap Table = FElysiumBankRemap::Build(
		BankSkeleton->GetReferenceSkeleton().GetRefBonePose(), BankSkeleton->GetReferenceSkeleton(),
		MeshSkeleton->GetReferenceSkeleton());

	TestEqual(TEXT("exactly one bone takes the translate branch (pelvis)"), Table.Translate.Num(), 1);
	TestEqual(TEXT("exactly one bone takes the similarity branch (forearm)"),
		Table.Similarity.Num(), 1);
	if (Table.Translate.Num() == 1)
	{
		TestEqual(TEXT("the translate entry names the pelvis"), Table.Translate[0].Bone,
			FName(TEXT("pelvis")));
		TestTrue(TEXT("the translate entry states b - a exactly"),
			Table.Translate[0].Offset.Equals(BodyPelvisBind - BankPelvisBind, 1e-6));
	}
	if (Table.Similarity.Num() == 1)
	{
		TestEqual(TEXT("the similarity entry names the forearm"), Table.Similarity[0].Bone,
			FName(TEXT("forearm")));
		TestTrue(TEXT("the similarity entry recovers the constructed rotation"),
			Table.Similarity[0].Rotation.Equals(ForearmRotation, 1e-4));
		TestTrue(TEXT("the similarity entry recovers the constructed scale"),
			FMath::IsNearlyEqual(static_cast<double>(Table.Similarity[0].Scale), ForearmScale, 1e-4));
	}
	// `root` and `spine` copy (absent from both lists); `extra` has nothing on the bank skeleton to
	// correct against and is skipped the same way. Two entries total, from five mesh bones.
	TestTrue(TEXT("root and spine copy, and the mesh-only `extra` bone is skipped"),
		Table.Translate.Num() + Table.Similarity.Num() == 2);

	// The runtime lookup must read the donor pose off the PLAYING SEQUENCE'S skeleton, not the
	// body's skeleton. Give the body a same-named decoy pose that would produce an empty table; the
	// bank skeleton alone carries the ash pair above. This is the exact topology that exposed the
	// runtime no-op: bank assets keep their bank-family skeleton while posing a compatible body mesh.
	const FName	   SharedSourceName(TEXT("shared_bank"));
	FReferencePose BankSourcePose;
	BankSourcePose.PoseName = SharedSourceName;
	BankSourcePose.ReferencePose = BankSkeleton->GetReferenceSkeleton().GetRefBonePose();
	BankSkeleton->AnimRetargetSources.Add(SharedSourceName, BankSourcePose);

	FReferencePose BodyDecoyPose;
	BodyDecoyPose.PoseName = SharedSourceName;
	BodyDecoyPose.ReferencePose = MeshSkeleton->GetReferenceSkeleton().GetRefBonePose();
	MeshSkeleton->AnimRetargetSources.Add(SharedSourceName, BodyDecoyPose);

	USkeletalMesh* BodyMesh = NewObject<USkeletalMesh>();
	BodyMesh->SetRefSkeleton(MeshSkeleton->GetReferenceSkeleton());
	BodyMesh->SetSkeleton(MeshSkeleton);
	UGameInstance*							  GameInstance = NewObject<UGameInstance>();
	UElysiumAnimSubsystem*					  AnimSubsystem = NewObject<UElysiumAnimSubsystem>(GameInstance);
	const TSharedPtr<const FElysiumBankRemap> RoutedTable =
		AnimSubsystem->GetBankRemap(BodyMesh, BankSkeleton, SharedSourceName);
	TestTrue(TEXT("the runtime lookup resolves a table from the sequence skeleton"),
		RoutedTable.IsValid());
	if (RoutedTable.IsValid())
	{
		TestEqual(TEXT("the sequence-skeleton table retains the pelvis translation branch"),
			RoutedTable->Translate.Num(), 1);
		TestEqual(TEXT("the sequence-skeleton table retains the forearm similarity branch"),
			RoutedTable->Similarity.Num(), 1);
	}

	// Cache identity includes the source skeleton as well as the source name. A second bank
	// skeleton with the same name but the body's own bind has no work; returning RoutedTable here
	// would prove the cache had aliased the two banks.
	USkeleton* CopyBankSkeleton = BuildTestSkeleton(
		{ TEXT("root"), TEXT("pelvis"), TEXT("spine"), TEXT("forearm"), TEXT("extra") },
		{ FTransform::Identity, FTransform(BodyPelvisBind), FTransform(SharedSpineBind),
			FTransform(MeshForearmBind), FTransform(FVector(9.0, 9.0, 9.0)) });
	FReferencePose CopySourcePose;
	CopySourcePose.PoseName = SharedSourceName;
	CopySourcePose.ReferencePose = CopyBankSkeleton->GetReferenceSkeleton().GetRefBonePose();
	CopyBankSkeleton->AnimRetargetSources.Add(SharedSourceName, CopySourcePose);
	TestFalse(TEXT("a same-named source on another skeleton gets its own empty cache entry"),
		AnimSubsystem->GetBankRemap(BodyMesh, CopyBankSkeleton, SharedSourceName).IsValid());


	// Part 2 — `FAnimNode_ElysiumBankRemap`: the translation-only correction over a resolved table,
	// stated inline through `LoadJson` so this does not depend on Part 1's `Build` or on any baked
	// asset.


	USkeleton* NodeSkeleton = BuildTestSkeleton(
		{ TEXT("root"), TEXT("pelvis"), TEXT("spine"), TEXT("forearm"), TEXT("hand") },
		{ FTransform::Identity, FTransform::Identity, FTransform::Identity, FTransform::Identity,
			FTransform::Identity });
	TArray<FBoneIndexType> RequiredBoneIndices;
	RequiredBoneIndices.SetNumUninitialized(NodeSkeleton->GetReferenceSkeleton().GetNum());
	for (int32 Index = 0; Index < RequiredBoneIndices.Num(); ++Index)
	{
		RequiredBoneIndices[Index] = static_cast<FBoneIndexType>(Index);
	}
	FBoneContainer Container;
	Container.InitializeTo(RequiredBoneIndices,
		UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::None), *NodeSkeleton);

	// One TRANSLATE entry (`pelvis`), one SIMILARITY entry (`forearm`), and one entry naming a bone
	// this skeleton does not carry at all (`ghost`) -- the ordinary absence the node has to skip
	// rather than fail on. `spine` and `hand` are named by neither list, so they take retail's COPY
	// branch by construction (absence from the table, not a third list).
	const FVector	 TranslateOffset(1.5, -0.75, 2.0);
	const FQuat		 SimilarityRotation(FVector::YAxisVector, FMath::DegreesToRadians(40.0));
	constexpr double SimilarityScale = 1.384;

	const FString JsonText = FString::Printf(TEXT(
												 "{"
												 "\"translate\": ["
												 "{\"bone\": \"pelvis\", \"offset\": %s},"
												 "{\"bone\": \"ghost\", \"offset\": [9.0, 9.0, 9.0]}"
												 "],"
												 "\"similarity\": [{\"bone\": \"forearm\", \"rotation\": %s, \"scale\": %.9f}]"
												 "}"),
		*QuoteJson(TranslateOffset), *QuoteJson(SimilarityRotation), SimilarityScale);

	FElysiumBankRemap NodeTable;
	FString			  LoadError;
	TestTrue(FString::Printf(TEXT("the inline table parses (%s)"), *LoadError),
		NodeTable.LoadJson(JsonText, LoadError));
	TestTrue(TEXT("the loaded table has work"), NodeTable.HasWork());

	FAnimNode_ElysiumBankRemap Node;
	TestFalse(TEXT("a node with no table has no work"), Node.HasWork());
	// Reproduce the runtime order: Unreal caches bones while this node has no table, then the first
	// playing bank installs one. The setter must resolve immediately against the already-valid
	// container; waiting for another CacheBones callback leaves the node a permanent no-op.
	Node.ResolveBones(Container);
	Node.SetTable(MakeShared<const FElysiumBankRemap>(NodeTable), &Container);

	TestTrue(TEXT("the node has work once a table resolves"), Node.HasWork());
	TestEqual(TEXT("exactly the one valid translate bone resolved (ghost is skipped, not substituted)"),
		Node.NumResolvedTranslate(), 1);
	TestEqual(TEXT("exactly the one similarity bone resolved"), Node.NumResolvedSimilarity(), 1);

	// --- the pose: a distinct, non-identity transform on every bone -------------------------------
	//
	// Rotation and non-uniform scale on EVERY bone, including the two the table corrects, so a node
	// that touched either would be caught bone by bone rather than only on the bones it leaves
	// alone.
	FMemMark		 Mark(FMemStack::Get());
	FPoseContext	 Output(Container);
	const FTransform SourceLocals[] = {
		FTransform(FQuat::Identity, FVector::ZeroVector, FVector::OneVector), // root
		FTransform(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(25.0)),
			FVector(3.0, 1.0, -2.0), FVector(1.0)), // pelvis (TRANSLATE)
		FTransform(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-10.0)),
			FVector(0.0, 0.0, 30.0), FVector(1.0)), // spine (COPY)
		FTransform(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(60.0)),
			FVector(5.0, -1.0, 0.5), FVector(1.1, 1.2, 1.3)), // forearm (SIMILARITY)
		FTransform(FQuat(FVector(1.0, 1.0, 0.0).GetSafeNormal(), FMath::DegreesToRadians(15.0)),
			FVector(-2.0, 3.0, 4.0), FVector(1.0)), // hand (COPY)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SourceLocals); ++Index)
	{
		Output.Pose[FCompactPoseBoneIndex(Index)] = SourceLocals[Index];
	}

	Node.Apply(Output);

	auto BoneAt = [&Output](int32 Index) -> const FTransform& {
		return Output.Pose[FCompactPoseBoneIndex(Index)];
	};

	// COPY (root, spine, hand): unchanged, bit-for-bit.
	for (const int32 Index : { 0, 2, 4 })
	{
		TestTrue(FString::Printf(TEXT("copy bone %d keeps its translation"), Index),
			BoneAt(Index).GetTranslation().Equals(SourceLocals[Index].GetTranslation(), 0.0));
		TestTrue(FString::Printf(TEXT("copy bone %d keeps its rotation bit-for-bit"), Index),
			BoneAt(Index).GetRotation() == SourceLocals[Index].GetRotation());
		TestTrue(FString::Printf(TEXT("copy bone %d keeps its scale bit-for-bit"), Index),
			BoneAt(Index).GetScale3D() == SourceLocals[Index].GetScale3D());
	}

	// TRANSLATE (pelvis, index 1): `p += Offset`. Rotation and scale untouched.
	{
		const FTransform& Pelvis = BoneAt(1);
		const FVector	  Expected = SourceLocals[1].GetTranslation() + TranslateOffset;
		TestTrue(TEXT("translate bone applies p += (b - a) exactly"),
			Pelvis.GetTranslation().Equals(Expected, 1e-6));
		TestTrue(TEXT("translate bone keeps its rotation bit-for-bit"),
			Pelvis.GetRotation() == SourceLocals[1].GetRotation());
		TestTrue(TEXT("translate bone keeps its scale bit-for-bit"),
			Pelvis.GetScale3D() == SourceLocals[1].GetScale3D());
	}

	// SIMILARITY (forearm, index 3): `p = Rotation.RotateVector(p) * Scale`. Rotation and scale
	// untouched -- only the translation this node ever writes is `SetTranslation` on this branch.
	{
		const FTransform& Forearm = BoneAt(3);
		const FVector	  Expected = SimilarityRotation.RotateVector(SourceLocals[3].GetTranslation())
			* SimilarityScale;
		TestTrue(TEXT("similarity bone applies rotation and scale to the translation"),
			Forearm.GetTranslation().Equals(Expected, 1e-6));
		TestTrue(TEXT("similarity bone keeps its OWN rotation bit-for-bit"),
			Forearm.GetRotation() == SourceLocals[3].GetRotation());
		TestTrue(TEXT("similarity bone keeps its scale bit-for-bit"),
			Forearm.GetScale3D() == SourceLocals[3].GetScale3D());
	}

	return true;
}

#endif
