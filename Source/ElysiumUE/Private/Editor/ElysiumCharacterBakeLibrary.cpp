#include "ElysiumCharacterBakeLibrary.h"

#if WITH_EDITOR
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumSkeletalSource.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "ReferenceSkeleton.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialParameters.h"
#include "UObject/Package.h"

namespace
{
	bool IsUnsaveable(const UObject* Object)
	{
		return Object != nullptr &&
			(Object->HasAnyFlags(RF_Transient) || Object->GetOutermost() == GetTransientPackage());
	}
}
#endif // WITH_EDITOR

int32 UElysiumCharacterBakeLibrary::SkeletonBoneCount(const USkeleton* Skeleton)
{
#if WITH_EDITOR
	return Skeleton != nullptr ? Skeleton->GetReferenceSkeleton().GetNum() : 0;
#else
	return 0;
#endif
}

bool UElysiumCharacterBakeLibrary::SkeletonHasMorphCurve(const USkeleton* Skeleton,
	const FName CurveName)
{
#if WITH_EDITOR
	if (Skeleton == nullptr)
	{
		return false;
	}
	const FCurveMetaData* MetaData = Skeleton->GetCurveMetaData(CurveName);
	return MetaData != nullptr && MetaData->Type.bMorphtarget;
#else
	return false;
#endif
}

FString UElysiumCharacterBakeLibrary::BakedAssetName(const FString& Raw)
{
#if WITH_EDITOR
	return FElysiumContentPaths::BakedAssetName(Raw);
#else
	return Raw;
#endif
}

bool UElysiumCharacterBakeLibrary::MaterialHasTexture(const UMaterialInterface* Material)
{
#if WITH_EDITOR
	if (Material == nullptr)
	{
		return false;
	}
	// OVERRIDDEN values only. `GetTextureParameterValue` defaults to resolving through the instance
	// chain to the parent's own default, and both character masters give every texture parameter a
	// real engine asset as its default -- WhiteSquareTexture and DefaultNormal. Reading the resolved
	// value therefore returns true on the first parameter of every material ever passed here, which
	// makes this the one check that cannot fail: an instance whose albedo never reached disk still
	// answers yes.
	const UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
	TArray<FMaterialParameterInfo> Params;
	TArray<FGuid> Ids;
	Material->GetAllParameterInfoOfType(EMaterialParameterType::Texture, Params, Ids);
	for (const FMaterialParameterInfo& Info : Params)
	{
		UTexture* Value = nullptr;
		const bool bFound = Instance != nullptr
			? Instance->GetTextureParameterValue(Info, Value, /*bOveriddenOnly=*/true)
			: Material->GetTextureParameterValue(Info, Value);
		if (bFound && Value != nullptr && !IsUnsaveable(Value))
		{
			return true;
		}
	}
	return false;
#else
	return false;
#endif
}

bool UElysiumCharacterBakeLibrary::RefPoseBoneTransform(const USkeletalMesh* Mesh, FName BoneName,
	FTransform& OutLocal)
{
	OutLocal = FTransform::Identity;
#if WITH_EDITOR
	if (Mesh == nullptr)
	{
		return false;
	}
	const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
	const int32 BoneIndex = Reference.FindBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}
	const TArray<FTransform>& Pose = Reference.GetRefBonePose();
	if (!Pose.IsValidIndex(BoneIndex))
	{
		return false;
	}
	OutLocal = Pose[BoneIndex];
	return true;
#else
	return false;
#endif
}

TArray<FName> UElysiumCharacterBakeLibrary::MeshBones(const USkeletalMesh* Mesh)
{
	TArray<FName> Names;
#if WITH_EDITOR
	if (Mesh != nullptr)
	{
		const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
		Names.Reserve(Reference.GetNum());
		for (int32 Bone = 0; Bone < Reference.GetNum(); ++Bone)
		{
			Names.Add(Reference.GetBoneName(Bone));
		}
	}
#endif
	return Names;
}

TArray<FName> UElysiumCharacterBakeLibrary::SequenceTrackBones(const UAnimSequence* Sequence)
{
	TArray<FName> Tracked;
#if WITH_EDITOR
	// The RAW track set, which is the one the bake writes. Compressed data drops and reconstructs
	// tracks on its own terms, so asking it would answer a different question than "was this channel
	// baked".
	const IAnimationDataModel* Model = Sequence != nullptr ? Sequence->GetDataModel() : nullptr;
	if (Model != nullptr)
	{
		Model->GetBoneTrackNames(Tracked);
	}
#endif
	return Tracked;
}

FString UElysiumCharacterBakeLibrary::VerifyAnimationSamples(const FString& StagePath,
	const TMap<FString,UAnimSequence*>& Sequences, const TArray<FName>& OmittedDonorBones,
	int32& OutSourceTracks, int64& OutSourceKeys, int32& OutOmittedSourceTracks)
{
	OutSourceTracks=0; OutSourceKeys=0; OutOmittedSourceTracks=0;
#if WITH_EDITOR
	FElysiumSkeletalSource Source; FString Error;
	if (!FElysiumSkeletalSource::Load(StagePath,Source,Error)) return Error;
	TSet<FName> Omitted(OmittedDonorBones);
	if (!Omitted.IsEmpty() && !Source.Vertices.IsEmpty()) return TEXT("body clips cannot declare dormant donor omissions");
	for (FName Name : Omitted)
		if (!Source.Bones.ContainsByPredicate([Name](const auto& Bone){return Bone.Name==Name;}))
			return TEXT("omitted donor bone is absent from source: ")+Name.ToString();
	int32 ClipCount=0;
	for (const auto& Clip : Source.Clips)
	{
		if ((Clip.Flags&4) && !Clip.BaseName.IsEmpty()) continue;
		++ClipCount;
		const auto* Found=Sequences.Find(Clip.Name);
		const UAnimSequence* Sequence=Found?*Found:nullptr;
		const IAnimationDataModel* Model=Sequence?Sequence->GetDataModel():nullptr;
		if (!Model) return TEXT("no native data model for source clip: ")+Clip.Name;
		const int32 Keys=FMath::Max(Clip.FrameCount,2);
		if (Model->GetNumberOfKeys()!=Keys) return TEXT("native key count differs: ")+Clip.Name;
		const double Rate=FMath::RoundToInt(FMath::Max(double(Clip.FrameRate),double(UE_KINDA_SMALL_NUMBER))*1000.)/1000.;
		if (!FMath::IsNearlyEqual(Model->GetFrameRate().AsDecimal(),Rate,1.e-9))
			return TEXT("native sample rate differs: ")+Clip.Name;
		TArray<FName> Names; Model->GetBoneTrackNames(Names);
		TSet<FName> NativeTracks(Names);
		for (const auto& Track : Clip.Tracks)
		{
			if (!Source.Bones.IsValidIndex(Track.Bone)) return TEXT("source track has invalid bone: ")+Clip.Name;
			const auto& Bone=Source.Bones[Track.Bone];
			const bool bOmitted=Omitted.Contains(Bone.Name);
			const bool bAdditive=(Clip.Flags&4)!=0;
			if (bOmitted)
			{
				++OutOmittedSourceTracks;
				if (!bAdditive)
				{
					if (NativeTracks.Contains(Bone.Name)) return TEXT("omitted donor bone still has a native track: ")+Clip.Name+TEXT(" / ")+Bone.Name.ToString();
					continue;
				}
			}
			if (!NativeTracks.Contains(Bone.Name)) return TEXT("native source track is missing: ")+Clip.Name+TEXT(" / ")+Bone.Name.ToString();
			TArray<FTransform> Actual; Model->GetBoneTrackTransforms(Bone.Name,Actual);
			if (Actual.Num()!=Keys) return TEXT("native source track has incomplete keys: ")+Clip.Name+TEXT(" / ")+Bone.Name.ToString();
			for (int32 Key=0; Key<Keys; ++Key)
			{
				const int32 Frame=FMath::Min(Key,Clip.FrameCount-1);
				const FVector Position=bOmitted?FVector::ZeroVector:Track.Translations.IsValidIndex(Frame)
					?FVector(Track.Translations[Frame]):Bone.Local.GetTranslation();
				const FQuat Rotation=bOmitted?FQuat::Identity:Track.Rotations.IsValidIndex(Frame)
					?FQuat(Track.Rotations[Frame]):Bone.Local.GetRotation();
				// Quaternion sign is not a rotation difference. Absolute components also catch
				// non-unit/corrupt keys, which normalizing both sides would hide.
				const FQuat Q=Actual[Key].GetRotation();
				const double Sign=(Q|Rotation)<0.?-1.:1.;
				const double RotationError=FMath::Max(FMath::Max(FMath::Abs(Q.X-Sign*Rotation.X),FMath::Abs(Q.Y-Sign*Rotation.Y)),
					FMath::Max(FMath::Abs(Q.Z-Sign*Rotation.Z),FMath::Abs(Q.W-Sign*Rotation.W)));
				const double PositionError=(Actual[Key].GetTranslation()-Position).GetAbsMax();
				if (Actual[Key].ContainsNaN() || PositionError>1.e-4 || RotationError>1.e-6
					|| !Actual[Key].GetScale3D().Equals(FVector::OneVector,1.e-6))
					return FString::Printf(TEXT("native source sample differs: %s / %s key %d (position %.9g cm, rotation %.9g)"),
						*Clip.Name,*Bone.Name.ToString(),Key,PositionError,RotationError);
			}
			if (!bOmitted) { ++OutSourceTracks; OutSourceKeys+=Keys; }
		}
	}
	if (ClipCount!=Sequences.Num()) return TEXT("native clip inventory differs from source sample inventory");
	return FString();
#else
	return TEXT("editor only");
#endif
}
