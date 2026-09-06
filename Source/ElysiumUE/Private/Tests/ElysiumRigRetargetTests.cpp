// The bake's retarget inputs against the retail rig-remap table a capture read out of the game.
//
// `uv run elysium research rig_parity` writes a `retarget` section into `rig_oracle.json`: one row
// per (body, bank, bone) the capture witnessed, carrying the two parent-relative binds this
// repository exported beside what retail's own builder wrote for the same pair. Retail's builder
// decides per bone whether to COPY the bank's position or to transform it, and the transform is a
// similarity derived from those two binds (`docs/vtmb/animation_and_movers.md` A.4b).
//
// This test replays those rows against the BAKED assets rather than against the containers the
// fixture was derived from, so a bake that wrote a different bind fails here instead of agreeing
// with a second derivation of its own numbers. Three things are asserted per row: the baked body
// mesh's reference pose is the bind the container states, the bank's registered retarget source
// carries the donor's own bind, and the pair falls on the same side of "these binds differ" that
// retail put it on.
//
// The oracle is game-derived and lives under ELYSIUM_WORK_ROOT; the test abstains without one.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumRigRetargetFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	//: Unreal refuses a retarget cache entry when the two binds agree within this, so a pair
	//: inside it is a no-op whatever mode the skeleton names (`BoneContainer.cpp`). Centimetres.
	constexpr double GUnrealSkipCm = 0.001;
	//: How far a baked bind may sit from the container's before it is a bake defect rather than
	//: float32 packing. The containers state float32 and the bake reads them verbatim.
	constexpr double GBakeTolerance = 1e-3;

	struct FRetargetRow
	{
		FString Body;
		FString Bank;
		FString BodyBone;
		FString BankBone;
		bool bRetailCopies = false;
		double RetailScale = 1.0;
		double RetailTranslationCm = 0.0;
		FVector BankBind = FVector::ZeroVector;
		FVector BodyBind = FVector::ZeroVector;

		// Retail wrote a rotation only on the axis-angle branch; a unit-scale matrix carrying a
		// translation IS the origin branch, which is the one no stock translation mode carries.
		bool RetailTookTranslationBranch() const
		{
			return FMath::IsNearlyEqual(RetailScale, 1.0, 1e-4)
				&& RetailTranslationCm > GUnrealSkipCm;
		}
	};

	// The divergences this corpus already carries, each an owner call rather than a defect to
	// hide. A row named here is reported and allowed; a row that diverges without being named
	// fails, which is the whole reason the list is keyed rather than a tolerance.
	//
	// 1. Six bones where retail COPIES and stock Unreal retargets. Retail compares squared Source
	//    lengths against 0.01 (0.1 in / 0.254 cm), while Unreal's threshold is 0.001 cm, so a pair
	//    inside that gap is copied by one engine and retargeted by the other. The two engines
	//    disagree about equality; neither bind is wrong.
	// 2. Five `Bip01 Pelvis` pairs where retail takes its ORIGIN branch -- identity rotation and
	//    a pure `b - a` offset -- while `OrientAndScale` applies a 0.049 length ratio, because
	//    Unreal's own zero test is against zero and retail's is not.
	const TSet<FString>& ExpectedDivergences()
	{
		static const TSet<FString> Rows = {
			TEXT("nosferatu_female_armor_0|character_shared_female_frenzy|Bip01 L Finger2"),
			TEXT("nosferatu_female_armor_0|character_shared_female_frenzy|Bip01 R Finger2"),
			TEXT("nosferatu_female_armor_0|character_shared_female_frenzy|Bip01 L Clavicle"),
			TEXT("nosferatu_female_armor_0|character_shared_female_frenzy|Bip01 R Clavicle"),
			TEXT("nosferatu_female_armor_0|character_shared_female_frenzy|Bip01 L Finger31"),
			TEXT("nosferatu_female_armor_0|character_shared_female_frenzy|Bip01 R Finger31"),
			TEXT("tremere_male_armor_2|character_shared_male_frenzy|Bip01 Pelvis"),
			TEXT("tremere_male_armor_2|character_shared_male_runotherspc_pcidles_allsequences|Bip01 Pelvis"),
			TEXT("tremere_male_armor_3|character_shared_male_frenzy|Bip01 Pelvis"),
			TEXT("tremere_male_armor_3|character_shared_male_runotherspc_pcidles_allsequences|Bip01 Pelvis"),
			TEXT("security_guard|character_shared_male_fat_male|Bip01 Pelvis"),
		};
		return Rows;
	}

	FString RowKey(const FRetargetRow& Row)
	{
		return FString::Printf(TEXT("%s|%s|%s"), *Row.Body, *Row.Bank, *Row.BodyBone);
	}

	FString FindRetargetOracle()
	{
		const FString Named = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_RIG_ORACLE"));
		if (!Named.IsEmpty())
		{
			return Named;
		}
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return FString();
		}
		const FString Frida = WorkRoot / TEXT("research") / TEXT("frida");
		TArray<FString> Sessions;
		IFileManager::Get().FindFiles(Sessions, *(Frida / TEXT("*-life_rig_resolution")), false, true);
		Sessions.Sort();
		for (int32 Index = Sessions.Num() - 1; Index >= 0; --Index)
		{
			const FString Candidate = Frida / Sessions[Index] / TEXT("rig_oracle.json");
			if (IFileManager::Get().FileExists(*Candidate))
			{
				return Candidate;
			}
		}
		return FString();
	}

	FVector ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object->TryGetArrayField(Field, Values) || Values->Num() != 3)
		{
			return FVector::ZeroVector;
		}
		return FVector((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
	}

	bool ReadRetargetRows(const FString& Path, TArray<FRetargetRow>& OutRows, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not a JSON object"), *Path);
			return false;
		}
		const TSharedPtr<FJsonObject>* Retarget = nullptr;
		if (!Root->TryGetObjectField(TEXT("retarget"), Retarget) || Retarget == nullptr)
		{
			OutError = FString::Printf(TEXT("%s carries no `retarget` section; re-run rig_parity"), *Path);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!(*Retarget)->TryGetArrayField(TEXT("rows"), Rows))
		{
			OutError = TEXT("the `retarget` section carries no `rows`");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object))
			{
				continue;
			}
			FRetargetRow Row;
			(*Object)->TryGetStringField(TEXT("body"), Row.Body);
			(*Object)->TryGetStringField(TEXT("bank"), Row.Bank);
			(*Object)->TryGetStringField(TEXT("body_bone"), Row.BodyBone);
			(*Object)->TryGetStringField(TEXT("bank_bone"), Row.BankBone);
			(*Object)->TryGetBoolField(TEXT("retail_copies"), Row.bRetailCopies);
			(*Object)->TryGetNumberField(TEXT("retail_scale"), Row.RetailScale);
			(*Object)->TryGetNumberField(TEXT("retail_translation_cm"), Row.RetailTranslationCm);
			Row.BankBind = ReadVector(*Object, TEXT("bank_bind"));
			Row.BodyBind = ReadVector(*Object, TEXT("body_bind"));
			if (!Row.Body.IsEmpty() && !Row.Bank.IsEmpty() && !Row.BodyBone.IsEmpty())
			{
				OutRows.Add(MoveTemp(Row));
			}
		}
		return true;
	}

	// One clip the named bank owns on the named body, which is how a baked bank sequence -- and
	// through it the skeleton carrying the bank's retarget source -- is reached.
	FString FindBankClip(const FElysiumNpcClipSet& Set, const FString& Bank)
	{
		FString Found;
		Set.Clips.ForEachClip([&Found, &Bank](const FString& Label, const FElysiumNpcClip& Clip)
		{
			if (Found.IsEmpty() && Clip.Owner.Equals(Bank, ESearchCase::IgnoreCase))
			{
				Found = Label;
			}
		});
		return Found;
	}

	struct FFailures
	{
		FString Kind;
		TArray<FString> Rows;

		void Add(const FString& Row) { Rows.Add(Row); }

		void Report(FAutomationTestBase& Test, int32 Examples = 10) const
		{
			if (Rows.IsEmpty())
			{
				return;
			}
			FString Message = FString::Printf(TEXT("%s: %d row(s)"), *Kind, Rows.Num());
			for (int32 Index = 0; Index < FMath::Min(Examples, Rows.Num()); ++Index)
			{
				Message += TEXT("\n    ") + Rows[Index];
			}
			if (Rows.Num() > Examples)
			{
				Message += FString::Printf(TEXT("\n    ... and %d more"), Rows.Num() - Examples);
			}
			Test.AddError(Message);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRigRetargetTest,
	"Elysium.Content.RigRetarget", GElysiumRigRetargetFlags)
bool FElysiumRigRetargetTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}
	const FString OraclePath = FindRetargetOracle();
	if (OraclePath.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no rig_oracle.json under $ELYSIUM_WORK_ROOT/research/frida "
			"(capture with frida_probe --recipe life_rig_resolution, run capture_rig_remap while the "
			"game is open, then rig_parity)"));
		return true;
	}
	TArray<FRetargetRow> Rows;
	FString Error;
	if (!ReadRetargetRows(OraclePath, Rows, Error))
	{
		AddError(Error);   // present but unreadable is a failure, not an abstention
		return false;
	}
	if (Rows.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the oracle's retarget section names no applicable row"));
		return true;
	}

	FFailures BodyBind{ TEXT("the baked body mesh's reference pose is not the container's bind") };
	FFailures BankBind{ TEXT("a bank's retarget source does not carry the donor's own bind") };
	FFailures MissingSource{ TEXT("a bank sequence names no retarget source for its own container") };
	FFailures Classification{ TEXT("our binds fall on the other side of 'these binds differ' than retail's") };
	FFailures Branch{ TEXT("retail took its origin branch where we take the length ratio") };
	TArray<FString> Allowed;
	TArray<FString> Skipped;

	int32 Checked = 0;
	TMap<FString, TArray<FTransform>> BodyPoses;
	TMap<FString, TMap<FName, int32>> BodyBoneIndex;
	TMap<FString, TArray<FTransform>> BankPoses;
	TMap<FString, TMap<FName, int32>> BankBoneIndex;
	TSet<FString> BankResolved;

	for (const FRetargetRow& Row : Rows)
	{
		// --- the body half: the baked mesh's own reference pose --------------------------------
		if (!BodyPoses.Contains(Row.Body))
		{
			TArray<FTransform> Pose;
			TMap<FName, int32> Index;
			if (const USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Row.Body))
			{
				const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
				Pose = Reference.GetRefBonePose();
				for (int32 Bone = 0; Bone < Reference.GetNum(); ++Bone)
				{
					Index.Add(Reference.GetBoneName(Bone), Bone);
				}
			}
			BodyPoses.Add(Row.Body, MoveTemp(Pose));
			BodyBoneIndex.Add(Row.Body, MoveTemp(Index));
		}
		const TArray<FTransform>& Pose = BodyPoses[Row.Body];
		const int32* BodyBone = BodyBoneIndex[Row.Body].Find(FName(*Row.BodyBone));
		if (Pose.IsEmpty() || BodyBone == nullptr)
		{
			Skipped.AddUnique(FString::Printf(TEXT("%s (no baked mesh or bone)"), *Row.Body));
			continue;
		}

		// --- the bank half: the retarget source the bake registered for that container ---------
		const FString BankKey = Row.Body + TEXT("|") + Row.Bank;
		if (!BankResolved.Contains(BankKey))
		{
			BankResolved.Add(BankKey);
			FElysiumNpcClipSet Set;
			FString LoadError;
			if (ElysiumNativeTest::Load(Set, Row.Body, LoadError))
			{
				const FString Clip = FindBankClip(Set, Row.Bank);
				const USkeletalMesh* Mesh = ElysiumNpcVisual::LoadBakedMesh(Row.Body);
				if (!Clip.IsEmpty() && Mesh != nullptr)
				{
					if (const UAnimSequence* Sequence =
						ElysiumNpcVisual::LoadBakedClip(Mesh, Row.Bank, Clip))
					{
						if (USkeleton* Skeleton = Sequence->GetSkeleton())
						{
							// Read the map directly rather than through `GetRefLocalPoses`, which
							// answers an unknown source name with the skeleton's own reference
							// pose -- a missing source would then read as a bind that agrees.
							if (const FReferencePose* Source =
								Skeleton->AnimRetargetSources.Find(FName(*Row.Bank)))
							{
								TMap<FName, int32> Index;
								const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
								for (int32 Bone = 0; Bone < Reference.GetNum(); ++Bone)
								{
									Index.Add(Reference.GetBoneName(Bone), Bone);
								}
								BankPoses.Add(BankKey, Source->ReferencePose);
								BankBoneIndex.Add(BankKey, MoveTemp(Index));
							}
							else
							{
								MissingSource.Add(FString::Printf(
									TEXT("%s: '%s' is absent from %s"),
									*Row.Body, *Row.Bank, *Skeleton->GetName()));
							}
						}
					}
				}
			}
		}
		const TArray<FTransform>* BankPose = BankPoses.Find(BankKey);
		const TMap<FName, int32>* BankIndex = BankBoneIndex.Find(BankKey);
		const int32* BankBone = BankIndex ? BankIndex->Find(FName(*Row.BankBone)) : nullptr;
		if (BankPose == nullptr || BankBone == nullptr || !BankPose->IsValidIndex(*BankBone))
		{
			Skipped.AddUnique(FString::Printf(TEXT("%s <- %s (no retarget source)"),
				*Row.Body, *Row.Bank));
			continue;
		}

		++Checked;
		const FVector BakedBody = Pose[*BodyBone].GetTranslation();
		const FVector BakedBank = (*BankPose)[*BankBone].GetTranslation();
		const FString Key = RowKey(Row);
		const bool bAllowed = ExpectedDivergences().Contains(Key);

		// 1. The bake wrote the container's binds, on both sides.
		if (!BakedBody.Equals(Row.BodyBind, GBakeTolerance))
		{
			BodyBind.Add(FString::Printf(TEXT("%s %s: baked %s, container %s"),
				*Row.Body, *Row.BodyBone, *BakedBody.ToString(), *Row.BodyBind.ToString()));
			continue;
		}
		if (!BakedBank.Equals(Row.BankBind, GBakeTolerance))
		{
			BankBind.Add(FString::Printf(TEXT("%s <- %s %s: baked %s, container %s"),
				*Row.Body, *Row.Bank, *Row.BankBone, *BakedBank.ToString(),
				*Row.BankBind.ToString()));
			continue;
		}

		// 2. The pair falls on the same side of "these binds differ" retail put it on.
		const double Separation = FVector::Dist(BakedBank, BakedBody);
		const bool bWeCopy = Separation <= GUnrealSkipCm;
		if (bWeCopy != Row.bRetailCopies)
		{
			const FString Detail = FString::Printf(
				TEXT("%s <- %s %s: retail %s, we %s (separation %.5f cm)"),
				*Row.Body, *Row.Bank, *Row.BodyBone,
				Row.bRetailCopies ? TEXT("copies") : TEXT("transforms"),
				bWeCopy ? TEXT("copy") : TEXT("transform"), Separation);
			if (bAllowed)
			{
				Allowed.Add(Detail);
			}
			else
			{
				Classification.Add(Detail);
			}
			continue;
		}

		// 3. Where retail transformed, it must not have taken the branch no stock translation
		//    mode carries: an identity rotation with a pure `b - a` offset.
		if (!Row.bRetailCopies && Row.RetailTookTranslationBranch())
		{
			const bool bOursAtOrigin = BakedBank.Size() <= GUnrealSkipCm
				|| BakedBody.Size() <= GUnrealSkipCm;
			if (!bOursAtOrigin)
			{
				const double Ratio = BakedBank.Size() > 0.0
					? BakedBody.Size() / BakedBank.Size() : 0.0;
				const FString Detail = FString::Printf(
					TEXT("%s <- %s %s: retail offset %.5f cm, OrientAndScale ratio %.5f "
						 "(|bank| %.5f, |body| %.5f)"),
					*Row.Body, *Row.Bank, *Row.BodyBone, Row.RetailTranslationCm, Ratio,
					BakedBank.Size(), BakedBody.Size());
				if (bAllowed)
				{
					Allowed.Add(Detail);
				}
				else
				{
					Branch.Add(Detail);
				}
			}
		}
	}

	if (Checked == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no oracle row reached a baked body and bank; "
			"run: uv run elysium import characters"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("oracle %s: %d of %d rows checked against the baked mount"),
		*OraclePath, Checked, Rows.Num()));
	for (const FString& Row : Allowed)
	{
		AddInfo(FString::Printf(TEXT("known divergence: %s"), *Row));
	}
	for (const FString& Row : Skipped)
	{
		AddInfo(FString::Printf(TEXT("not on the mount: %s"), *Row));
	}
	BodyBind.Report(*this);
	BankBind.Report(*this);
	MissingSource.Report(*this);
	Classification.Report(*this);
	Branch.Report(*this);
	return true;
}
