// Deterministic pose envelopes for the opening embrace. The structural GLB tests prove that files
// are finite and bindable; this test evaluates the actual UAnimSequence on each target skeleton so
// a generic retargeter cannot silently turn a valid 1-3 m biped into an exploded 4-8 m pose.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumSkeletalBasis.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "glTFRuntimeAsset.h"

static constexpr EAutomationTestFlags GElysiumSkeletalPoseFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FOpeningPoseCase
	{
		const TCHAR* Label;
		const TCHAR* MeshStem;
		const TCHAR* BankStem;
	};

	const FOpeningPoseCase GOpeningPoseCases[] =
	{
		{ TEXT("player/Bip01"), TEXT("brujah_male_armor_0"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip01") },
		{ TEXT("Sire/Bip02"), TEXT("brujah_female_armor_3"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip02") },
		{ TEXT("Vampire1/Bip03"), TEXT("malkavian_male_armor_0"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip03") },
		{ TEXT("Vampire2/Bip04"), TEXT("toreador_male_armor_0"),
			TEXT("cinematic_santa_monica_haven_embrace_bips1__bip04") },
		{ TEXT("Sheriff/Bip05"), TEXT("sheriff"),
			TEXT("cinematic_santa_monica_haven_embrace_bips2__bip05") },
	};

	float RootRelativeRadius(const TArray<FTransform>& ComponentPose)
	{
		if (ComponentPose.IsEmpty())
		{
			return 0.f;
		}
		const FTransform& Root = ComponentPose[0];
		float Radius = 0.f;
		for (const FTransform& Bone : ComponentPose)
		{
			Radius = FMath::Max(Radius,
				Root.InverseTransformPosition(Bone.GetTranslation()).Size());
		}
		return Radius;
	}

	void BuildComponentPose(const FReferenceSkeleton& Ref, IAnimationDataModel& Model,
		const TSet<FName>& AnimatedTracks, int32 Frame, TArray<FTransform>& Out)
	{
		Out = Ref.GetRefBonePose();
		for (int32 BoneIndex = 0; BoneIndex < Ref.GetNum(); ++BoneIndex)
		{
			const FName BoneName = Ref.GetBoneName(BoneIndex);
			if (AnimatedTracks.Contains(BoneName))
			{
				Out[BoneIndex] = Model.GetBoneTrackTransform(BoneName, FFrameNumber(Frame));
			}
			const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				Out[BoneIndex] *= Out[ParentIndex];
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningPoseEnvelopeTest,
	"Elysium.Content.OpeningPoseEnvelope", GElysiumSkeletalPoseFlags)

bool FElysiumOpeningPoseEnvelopeTest::RunTest(const FString&)
{
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: NPC export is absent"));
		return true;
	}

	bool bValid = true;
	int32 Evaluated = 0;
	for (const FOpeningPoseCase& PoseCase : GOpeningPoseCases)
	{
		FString Error;
		UglTFRuntimeAsset* MeshAsset = nullptr;
		USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(PoseCase.MeshStem, MeshAsset, Error);
		if (Mesh == nullptr || MeshAsset == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: mesh load failed: %s"), PoseCase.Label, *Error));
			bValid = false;
			continue;
		}
		Mesh->AddToRoot();
		MeshAsset->AddToRoot();

		const FString BankPath = FElysiumContentPaths::NpcBankGlb(
			FString::Printf(TEXT("banks/%s.glb"), PoseCase.BankStem));
		UglTFRuntimeAsset* BankAsset = ElysiumNpcVisual::LoadAssetFromPath(BankPath, Error);
		UAnimSequence* Anim = BankAsset != nullptr
			? ElysiumNpcVisual::RetargetClip(BankAsset, Mesh, TEXT("entire_scene"), Error)
			: nullptr;
		if (BankAsset == nullptr || Anim == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: bank/clip load failed: %s"), PoseCase.Label, *Error));
			if (BankAsset != nullptr) BankAsset->AddToRoot();
			MeshAsset->RemoveFromRoot();
			Mesh->RemoveFromRoot();
			bValid = false;
			continue;
		}
		BankAsset->AddToRoot();
		Anim->AddToRoot();

		IAnimationDataModel* Model = Anim->GetDataModel();
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		if (Model == nullptr || Ref.GetNum() == 0)
		{
			AddError(FString::Printf(TEXT("%s: no animation model or target skeleton"), PoseCase.Label));
			bValid = false;
		}
		else
		{
			TArray<FName> TrackNames;
			Model->GetBoneTrackNames(TrackNames);
			TSet<FName> AnimatedTracks(TrackNames);
			const int32 LastFrame = Model->GetNumberOfFrames();

			TArray<FTransform> ReferenceComponent = Ref.GetRefBonePose();
			for (int32 BoneIndex = 0; BoneIndex < Ref.GetNum(); ++BoneIndex)
			{
				const int32 ParentIndex = Ref.GetParentIndex(BoneIndex);
				if (ParentIndex != INDEX_NONE)
				{
					ReferenceComponent[BoneIndex] *= ReferenceComponent[ParentIndex];
				}
			}
			const float ReferenceRadius = RootRelativeRadius(ReferenceComponent);
			const float RadiusLimit = FMath::Max(350.f, ReferenceRadius * 2.25f);

			float MaxPoseRadius = 0.f;
			float MaxRootDistance = 0.f;
			float MaxLocalSegment = 0.f;
			for (const int32 Frame : { 0, LastFrame / 4, LastFrame / 2,
				(LastFrame * 3) / 4, LastFrame })
			{
				TArray<FTransform> ComponentPose;
				BuildComponentPose(Ref, *Model, AnimatedTracks, Frame, ComponentPose);
				for (int32 BoneIndex = 0; BoneIndex < ComponentPose.Num(); ++BoneIndex)
				{
					if (ComponentPose[BoneIndex].ContainsNaN()
						|| !ComponentPose[BoneIndex].IsRotationNormalized())
					{
						AddError(FString::Printf(TEXT("%s: invalid bone %s at frame %d"),
							PoseCase.Label, *Ref.GetBoneName(BoneIndex).ToString(), Frame));
						bValid = false;
					}
					if (BoneIndex > 0)
					{
						const FTransform Local = AnimatedTracks.Contains(Ref.GetBoneName(BoneIndex))
							? Model->GetBoneTrackTransform(Ref.GetBoneName(BoneIndex), FFrameNumber(Frame))
							: Ref.GetRefBonePose()[BoneIndex];
						MaxLocalSegment = FMath::Max(MaxLocalSegment, Local.GetTranslation().Size());
					}
				}
				MaxPoseRadius = FMath::Max(MaxPoseRadius, RootRelativeRadius(ComponentPose));
				MaxRootDistance = FMath::Max(MaxRootDistance,
					ComponentPose[0].GetTranslation().Size());
			}

			if (MaxPoseRadius > RadiusLimit)
			{
				AddError(FString::Printf(TEXT("%s: exploded pose radius %.1f cm exceeds %.1f cm "
					"(reference %.1f cm)"), PoseCase.Label, MaxPoseRadius, RadiusLimit,
					ReferenceRadius));
				bValid = false;
			}
			if (MaxRootDistance > 900.f)
			{
				AddError(FString::Printf(TEXT("%s: authored root placement %.1f cm exceeds scene envelope"),
					PoseCase.Label, MaxRootDistance));
				bValid = false;
			}
			if (MaxLocalSegment > 200.f)
			{
				AddError(FString::Printf(TEXT("%s: non-root segment %.1f cm exceeds biped envelope"),
					PoseCase.Label, MaxLocalSegment));
				bValid = false;
			}
			AddInfo(FString::Printf(TEXT("%s: reference radius %.1f cm, pose %.1f cm, "
				"root %.1f cm, longest local %.1f cm"), PoseCase.Label, ReferenceRadius,
				MaxPoseRadius, MaxRootDistance, MaxLocalSegment));
			++Evaluated;
		}

		Anim->RemoveFromRoot();
		BankAsset->RemoveFromRoot();
		MeshAsset->RemoveFromRoot();
		Mesh->RemoveFromRoot();
	}

	TestEqual(TEXT("all five opening bodies were evaluated"), Evaluated,
		static_cast<int32>(UE_ARRAY_COUNT(GOpeningPoseCases)));
	TestTrue(TEXT("opening poses stay inside their skeletal envelopes"), bValid);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningScenePlacementTest,
	"Elysium.Content.OpeningScenePlacement", GElysiumSkeletalPoseFlags)

bool FElysiumOpeningScenePlacementTest::RunTest(const FString&)
{
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*EntsPath)
		|| !IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: theatre or NPC export is absent"));
		return true;
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre entities parse"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return true;
	}
	const FElysiumEntityDef* Scene = nullptr;
	const FElysiumEntityDef* Target = nullptr;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (Def.TargetName.Equals(TEXT("embrace_o_matic"), ESearchCase::IgnoreCase))
		{
			Scene = &Def;
		}
		else if (Def.TargetName.Equals(TEXT("embrace_target_5"), ESearchCase::IgnoreCase))
		{
			Target = &Def;
		}
	}
	if (!TestNotNull(TEXT("embrace scene entity exists"), Scene)
		|| !TestNotNull(TEXT("the post-7.5s target key exists"), Target))
	{
		return true;
	}

	FVector SourceAngles = FVector::ZeroVector;
	TArray<FString> AngleParts;
	Scene->Keys.FindRef(TEXT("angles")).ParseIntoArrayWS(AngleParts);
	if (AngleParts.Num() >= 3)
	{
		SourceAngles = FVector(FCString::Atof(*AngleParts[0]), FCString::Atof(*AngleParts[1]),
			FCString::Atof(*AngleParts[2]));
	}
	TestTrue(TEXT("the theatre scene keeps its authored 270 degree yaw"),
		FMath::IsNearlyEqual(SourceAngles.Y, 270.0f));
	TestTrue(TEXT("a 270 Source yaw plus the skeletal basis resolves to zero Unreal yaw"),
		FMath::IsNearlyZero(FMath::UnwindDegrees(
			ElysiumSkeletalBasis::FromSourceAngles(SourceAngles).Yaw)));

	FString Error;
	UglTFRuntimeAsset* MeshAsset = nullptr;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(TEXT("brujah_male_armor_0"), MeshAsset, Error);
	const FString BankPath = FElysiumContentPaths::NpcBankGlb(
		TEXT("banks/cinematic_santa_monica_haven_embrace_bips1__bip01.glb"));
	UglTFRuntimeAsset* BankAsset = ElysiumNpcVisual::LoadAssetFromPath(BankPath, Error);
	UAnimSequence* Anim = Mesh && BankAsset
		? ElysiumNpcVisual::RetargetClip(BankAsset, Mesh, TEXT("entire_scene"), Error)
		: nullptr;
	if (!TestNotNull(TEXT("player mesh loads for placement probe"), Mesh)
		|| !TestNotNull(TEXT("player mesh asset is retained for placement probe"), MeshAsset)
		|| !TestNotNull(TEXT("Bip01 bank loads for placement probe"), BankAsset)
		|| !TestNotNull(TEXT("entire_scene binds for placement probe"), Anim))
	{
		AddError(Error);
		return true;
	}
	Mesh->AddToRoot();
	MeshAsset->AddToRoot();
	BankAsset->AddToRoot();
	Anim->AddToRoot();

	IAnimationDataModel* Model = Anim->GetDataModel();
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	if (TestNotNull(TEXT("placement clip has an animation model"), Model)
		&& TestTrue(TEXT("placement target skeleton has a root"), Ref.GetNum() > 0)
		&& TestTrue(TEXT("placement clip reaches 7.5 seconds"), Model->GetNumberOfFrames() >= 225))
	{
		const FVector RootLocal = Model->GetBoneTrackTransform(
			Ref.GetBoneName(0), FFrameNumber(225)).GetTranslation();
		const FVector CorrectWorld = Scene->Origin
			+ ElysiumSkeletalBasis::FromSourceAngles(SourceAngles).RotateVector(RootLocal);
		const FVector LegacyWorld = Scene->Origin
			+ FRotator(0.0f, -SourceAngles.Y, 0.0f).RotateVector(RootLocal);
		const float CorrectDistance = FVector::Distance(CorrectWorld, Target->Origin);
		const float LegacyDistance = FVector::Distance(LegacyWorld, Target->Origin);
		TestTrue(TEXT("the corrected player root lands inside the authored 7.5s shot envelope"),
			CorrectDistance < 150.0f);
		TestTrue(TEXT("the previous extra quarter-turn misses that shot by over three metres"),
			LegacyDistance > 300.0f);
		AddInfo(FString::Printf(TEXT("7.5s placement: corrected %.1f cm, legacy %.1f cm"),
			CorrectDistance, LegacyDistance));
	}

	Anim->RemoveFromRoot();
	BankAsset->RemoveFromRoot();
	MeshAsset->RemoveFromRoot();
	Mesh->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS