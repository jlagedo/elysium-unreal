#include "Visual/ElysiumBankRemap.h"

#include "ReferenceSkeleton.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Named apart from the sibling readers in `ElysiumCompositionRig.cpp` and `ElysiumEyeRig.cpp`:
	// an anonymous namespace still gives internal linkage per translation unit, but a unity build
	// folds several `.cpp`s into one TU, and two same-named free functions in two folded anonymous
	// namespaces is a redefinition there even though neither file sees the other on its own build
	// (`ElysiumRigComposeTests.cpp`'s `FindByRawIndex` hit exactly this once).
	bool ReadBankRemapVector(const TSharedPtr<FJsonValue>& Value, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Numbers = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Numbers) || Numbers == nullptr
			|| Numbers->Num() < 3)
		{
			return false;
		}
		Out = FVector((*Numbers)[0]->AsNumber(), (*Numbers)[1]->AsNumber(), (*Numbers)[2]->AsNumber());
		return true;
	}

	// glTF component order, x/y/z/w -- the same convention `ElysiumCompositionRig.cpp` reads.
	bool ReadBankRemapQuat(const TSharedPtr<FJsonValue>& Value, FQuat& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Numbers = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Numbers) || Numbers == nullptr
			|| Numbers->Num() < 4)
		{
			return false;
		}
		Out = FQuat((*Numbers)[0]->AsNumber(), (*Numbers)[1]->AsNumber(),
			(*Numbers)[2]->AsNumber(), (*Numbers)[3]->AsNumber());
		return true;
	}
} // namespace

// The branch rule.

EElysiumBankRemapBranch FElysiumBankRemap::ClassifyBranch(const FVector& BankBind, const FVector& BodyBind)
{
	// Retail's own threshold, `vampire.dll 0x10450aa4`: `0.01f` compared against a SQUARED length in
	// Source's inch-derived units, which is 0.254 cm here -- read verbatim, no coordinate scaling.
	constexpr double ThresholdCm = 0.254;
	if ((BankBind - BodyBind).Size() <= ThresholdCm)
	{
		return EElysiumBankRemapBranch::Copy;
	}
	// **Both binds near the origin is a COPY, not a translation.** Retail tests each bind's own
	// squared length after the difference test, and when BOTH fall inside the threshold it takes
	// neither the translate nor the similarity path -- the position falls through unmodified.
	// Only when EXACTLY ONE bind is small does it add the raw `b - a` offset. Read off
	// `vampire.dll 0x100c67b0`; the reference compositor states the same rule
	// (`retail_compositor._bind_branch`), and the two are held to each other.
	const bool bBankSmall = BankBind.Size() <= ThresholdCm;
	const bool bBodySmall = BodyBind.Size() <= ThresholdCm;
	if (bBankSmall && bBodySmall)
	{
		return EElysiumBankRemapBranch::Copy;
	}
	if (bBankSmall || bBodySmall)
	{
		return EElysiumBankRemapBranch::Translate;
	}
	return EElysiumBankRemapBranch::Similarity;
}

// Build from the two bind poses.

FElysiumBankRemap FElysiumBankRemap::Build(const TArray<FTransform>& BankBindPose,
	const FReferenceSkeleton& BankSkeleton, const FReferenceSkeleton& MeshSkeleton)
{
	FElysiumBankRemap Table;

	const TArray<FMeshBoneInfo>& MeshBones = MeshSkeleton.GetRefBoneInfo();
	const TArray<FTransform>&	 MeshBindPose = MeshSkeleton.GetRefBonePose();
	Table.Translate.Reserve(MeshBones.Num());

	for (int32 MeshIndex = 0; MeshIndex < MeshBones.Num(); ++MeshIndex)
	{
		const FName BoneName = MeshBones[MeshIndex].Name;
		const int32 BankIndex = BankSkeleton.FindBoneIndex(BoneName);
		if (BankIndex == INDEX_NONE || !BankBindPose.IsValidIndex(BankIndex)
			|| IsAbsentBankBind(BankBindPose[BankIndex]))
		{
			// A bone this body's mesh carries that the bank itself has never had (an appendix, a
			// hair chain no other body shares) has nothing to correct against -- retail's own remap
			// builder has no entry for it either, and this is the same silent absence a
			// table-named bone missing from the evaluating pose is at runtime. The shared
			// skeleton's tree still names it (another body's same-named chain put it there), so
			// the bank's donor pose says so per bone (`AbsentBankBind`), not the tree.
			continue;
		}

		const FVector BankBind = BankBindPose[BankIndex].GetTranslation();
		const FVector BodyBind = MeshBindPose[MeshIndex].GetTranslation();
		switch (ClassifyBranch(BankBind, BodyBind))
		{
			case EElysiumBankRemapBranch::Copy:
				break;
			case EElysiumBankRemapBranch::Translate:
			{
				FElysiumBankRemapTranslateEntry Entry;
				Entry.Bone = BoneName;
				Entry.Offset = BodyBind - BankBind;
				Table.Translate.Add(Entry);
				break;
			}
			case EElysiumBankRemapBranch::Similarity:
			{
				FElysiumBankRemapSimilarityEntry Entry;
				Entry.Bone = BoneName;
				Entry.Rotation = FQuat::FindBetweenVectors(BankBind, BodyBind);
				Entry.Scale = static_cast<float>(BodyBind.Size() / BankBind.Size());
				Table.Similarity.Add(Entry);
				break;
			}
		}
	}
	return Table;
}

// Test-only inline load.

bool FElysiumBankRemap::LoadJson(const FString& JsonText, FString& OutError)
{
	Translate.Reset();
	Similarity.Reset();

	TSharedPtr<FJsonObject>			Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("malformed bank remap JSON");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (Root->TryGetArrayField(TEXT("translate"), Rows) && Rows != nullptr)
	{
		Translate.Reserve(Rows->Num());
		for (const TSharedPtr<FJsonValue>& RowVal : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowObj = nullptr;
			if (!RowVal.IsValid() || !RowVal->TryGetObject(RowObj) || RowObj == nullptr)
			{
				continue;
			}
			FElysiumBankRemapTranslateEntry Entry;
			FString							BoneName;
			const TSharedPtr<FJsonValue>*	OffsetVal = (*RowObj)->Values.Find(TEXT("offset"));
			if (!(*RowObj)->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty()
				|| OffsetVal == nullptr || !ReadBankRemapVector(*OffsetVal, Entry.Offset))
			{
				OutError = FString::Printf(TEXT("`translate` row '%s' is missing `bone` or `offset`"),
					BoneName.IsEmpty() ? TEXT("<unnamed>") : *BoneName);
				return false;
			}
			Entry.Bone = FName(*BoneName);
			Translate.Add(Entry);
		}
	}

	if (Root->TryGetArrayField(TEXT("similarity"), Rows) && Rows != nullptr)
	{
		Similarity.Reserve(Rows->Num());
		for (const TSharedPtr<FJsonValue>& RowVal : *Rows)
		{
			const TSharedPtr<FJsonObject>* RowObj = nullptr;
			if (!RowVal.IsValid() || !RowVal->TryGetObject(RowObj) || RowObj == nullptr)
			{
				continue;
			}
			FElysiumBankRemapSimilarityEntry Entry;
			FString							 BoneName;
			const TSharedPtr<FJsonValue>*	 RotationVal = (*RowObj)->Values.Find(TEXT("rotation"));
			if (!(*RowObj)->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty()
				|| RotationVal == nullptr || !ReadBankRemapQuat(*RotationVal, Entry.Rotation)
				|| !(*RowObj)->TryGetNumberField(TEXT("scale"), Entry.Scale))
			{
				OutError = FString::Printf(
					TEXT("`similarity` row '%s' is missing `bone`, `rotation` or `scale`"),
					BoneName.IsEmpty() ? TEXT("<unnamed>") : *BoneName);
				return false;
			}
			Entry.Bone = FName(*BoneName);
			Similarity.Add(Entry);
		}
	}
	return true;
}
