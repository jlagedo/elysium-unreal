#include "ElysiumCharacterProvenance.h"
#include "ElysiumDynamicsData.h"
#include "ElysiumPhysicsData.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ElysiumJsonField.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

namespace ElysiumCharacterData
{
	bool ReadExpressions(UElysiumCharacterProvenance& Record, const TSharedPtr<FJsonObject>& Mesh, FString& Error)
	{
		// Old pilot assets remain readable while the expression bake is introduced. Their
		// explicit false presence is refused by native expression resolution, never replaced
		// by a guessed gender or by loose text. New stages always publish this declaration.
		if (!Mesh->HasField(TEXT("expressionData"))) return true;
		const TSharedPtr<FJsonObject>* Data = nullptr;
		if (!Mesh->TryGetObjectField(TEXT("expressionData"), Data) || !Data)
		{ Error = TEXT("expressionData: expected an object"); return false; }
		FString Version;
		double Flags = 0.;
		bool bMale = false;
		const auto FlagsValue = (*Data)->TryGetField(TEXT("modelFlags"));
		if (!(*Data)->TryGetStringField(TEXT("schemaVersion"), Version) || Version != TEXT("1.0.0"))
		{ Error = TEXT("expressionData.schemaVersion: expected 1.0.0"); return false; }
		if (!FlagsValue.IsValid() || FlagsValue->Type != EJson::Number || !FlagsValue->TryGetNumber(Flags)
			|| !FMath::IsFinite(Flags) || Flags < 0 || Flags > MAX_uint32 || double(uint32(Flags)) != Flags)
		{ Error = TEXT("expressionData.modelFlags: expected exact uint32 source flags"); return false; }
		if (!(*Data)->TryGetBoolField(TEXT("modelIsMale"), bMale) || bMale != ((uint32(Flags) & 0x100u) == 0))
		{ Error = TEXT("expressionData.modelIsMale: disagrees with source model flag bit 8"); return false; }
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!(*Data)->TryGetArrayField(TEXT("selections"), Rows))
		{ Error = TEXT("expressionData.selections: missing ordered declaration"); return false; }
		TSet<FString> Classes;
		for (int32 I = 0; I < Rows->Num(); ++I)
		{
			const FString Path = FString::Printf(TEXT("expressionData.selections[%d]"), I);
			const TSharedPtr<FJsonObject>* Row = nullptr;
			FElysiumExpressionSelection Selection;
			if (!(*Rows)[I]->TryGetObject(Row)
				|| !(*Row)->TryGetStringField(TEXT("tableClass"), Selection.TableClass)
				|| !(*Row)->TryGetStringField(TEXT("primaryAssetId"), Selection.PrimaryAssetId)
				|| !(*Row)->TryGetStringArrayField(TEXT("fallbackAssetIds"), Selection.FallbackAssetIds)
				|| !(*Row)->TryGetStringField(TEXT("sourceSelectionJson"), Selection.SourceSelectionJson))
			{ Error = Path + TEXT(": missing typed selection field"); return false; }
			if ((Selection.TableClass != TEXT("phonemes") && Selection.TableClass != TEXT("expressions"))
				|| Classes.Contains(Selection.TableClass))
			{ Error = Path + TEXT(".tableClass: invalid or duplicate class"); return false; }
			TSharedPtr<FJsonObject> Source;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Selection.SourceSelectionJson), Source) || !Source.IsValid())
			{ Error = Path + TEXT(".sourceSelectionJson: invalid preserved source row"); return false; }
			FString SourceClass;
			if (!Source->TryGetStringField(TEXT("class"), SourceClass) || SourceClass != Selection.TableClass)
			{ Error = Path + TEXT(".tableClass: differs from source row"); return false; }
			auto Matches = [](const TSharedPtr<FJsonObject>& Candidate, const FString& Projected)
			{
				bool bResolved = false; FString Id, Stem;
				if (!Candidate->TryGetBoolField(TEXT("resolved"), bResolved)
					|| !Candidate->TryGetStringField(TEXT("asset"), Id) || !Candidate->TryGetStringField(TEXT("stem"), Stem)) return false;
				const FString Prefix = bResolved ? TEXT("vtmb:expression-table:") : TEXT("vtmb:missing-expression-table:");
				return Id == Prefix + Stem && Projected == (bResolved ? Id : FString());
			};
			if (!Matches(Source, Selection.PrimaryAssetId))
			{ Error = Path + TEXT(".primaryAssetId: differs from source resolution"); return false; }
			const TArray<TSharedPtr<FJsonValue>>* Fallbacks = nullptr;
			if (!Source->TryGetArrayField(TEXT("fallbacks"), Fallbacks) || Fallbacks->Num() != Selection.FallbackAssetIds.Num())
			{ Error = Path + TEXT(".fallbackAssetIds: source fallback count differs"); return false; }
			for (int32 J = 0; J < Fallbacks->Num(); ++J)
			{
				const TSharedPtr<FJsonObject>* Fallback = nullptr;
				if (!(*Fallbacks)[J]->TryGetObject(Fallback) || !Matches(*Fallback, Selection.FallbackAssetIds[J]))
				{ Error = FString::Printf(TEXT("%s.fallbackAssetIds[%d]: differs from ordered source fallback"), *Path, J); return false; }
			}
			Classes.Add(Selection.TableClass);
			Record.ExpressionSelections.Add(MoveTemp(Selection));
		}
		Record.ExpressionModelFlags = uint32(Flags);
		Record.bExpressionModelIsMale = bMale;
		Record.bHasExpressionData = true;
		return true;
	}

	int32 ArrayCount(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		return Object->TryGetArrayField(Key, Rows) && Rows ? Rows->Num() : INDEX_NONE;
	}

	FString JsonText(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		return Text;
	}

	bool ReadMesh(UElysiumCharacterProvenance& Record, const TSharedPtr<FJsonObject>& Data, FString& Error)
	{
		if (ElysiumJson::Str(Data.ToSharedRef(), TEXT("schemaVersion")) != TEXT("1.0.0")
			|| !Data->HasField(TEXT("facial")) || !Data->HasField(TEXT("eyes")))
		{
			Error = TEXT("invalid mesh-data schema or absent rig declarations");
			return false;
		}
		Record.ModelPath = ElysiumJson::Str(Data.ToSharedRef(), TEXT("modelPath"));
		Record.Stem = ElysiumJson::Str(Data.ToSharedRef(), TEXT("stem"));
		if (!ReadExpressions(Record, Data, Error)) return false;
		for (const TCHAR* Key : {TEXT("facial"), TEXT("eyes")})
		{
			const TSharedPtr<FJsonValue> Value = Data->TryGetField(Key);
			if (!Value || (Value->Type != EJson::Null && Value->Type != EJson::Object))
			{
				Error = FString::Printf(TEXT("%s must be a record or an explicit absence"), Key);
				return false;
			}
		}
		const TSharedPtr<FJsonObject>* Domain = nullptr;
		if (Data->TryGetObjectField(TEXT("facial"), Domain) && Domain)
		{
			if (!Record.Facial.LoadJsonText(JsonText(*Domain), Error)) return false;
			Record.Facial.Stem = Record.Stem;
			if (Record.Facial.FlexDescs.Num() != ArrayCount(*Domain, TEXT("flexdescs"))
				|| Record.Facial.Controllers.Num() != ArrayCount(*Domain, TEXT("controllers"))
				|| Record.Facial.Rules.Num() != ArrayCount(*Domain, TEXT("rules"))
				|| Record.Facial.Morphs.Num() != ArrayCount(*Domain, TEXT("morphs"))
				|| Record.Facial.Mouths.Num() != ArrayCount(*Domain, TEXT("mouths")))
			{
				Error = TEXT("facial parser dropped a declared record");
				return false;
			}
			const auto& Rules = (*Domain)->GetArrayField(TEXT("rules"));
			for (int32 Index = 0; Index < Rules.Num(); ++Index)
			{
				const TSharedPtr<FJsonObject>* Rule = nullptr;
				if (!Rules[Index]->TryGetObject(Rule) || !Rule
					|| Record.Facial.Rules[Index].Ops.Num() != ArrayCount(*Rule, TEXT("ops")))
				{
					Error = FString::Printf(TEXT("facial rule %d dropped an operation"), Index);
					return false;
				}
			}
		}
		if (Data->TryGetObjectField(TEXT("eyes"), Domain) && Domain)
		{
			if (!Record.Eyes.LoadJsonText(JsonText(*Domain), Error)) return false;
			Record.Eyes.Stem = Record.Stem;
			if (Record.Eyes.Eyeballs.Num() != ArrayCount(*Domain, TEXT("eyeballs")))
			{
				Error = TEXT("eye parser dropped a declared record");
				return false;
			}
		}
		if (!Data->TryGetObjectField(TEXT("composition"), Domain) || !Domain
			|| !Record.Composition.LoadAxisRulesJson(JsonText(*Domain), Error))
		{
			if (Error.IsEmpty()) Error = TEXT("missing composition declaration");
			return false;
		}
		Record.Composition.Stem = Record.Stem;
		if (Record.Composition.AxisRules.Num() != ArrayCount(*Domain, TEXT("rules")))
		{
			Error = TEXT("procedural parser dropped a declared rule");
			return false;
		}
		for (const FString& Bone : ElysiumJson::Strings(Data.ToSharedRef(), TEXT("splitBones")))
			Record.Composition.SplitBones.Add(FName(*Bone));
		if (Record.Composition.SplitBones.Num() != ArrayCount(Data, TEXT("splitBones")))
		{
			Error = TEXT("missing or invalid split-bone declaration");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Data->TryGetArrayField(TEXT("materialSlots"), Rows) || !Rows)
		{
			Error = TEXT("missing material-slot declaration");
			return false;
		}
		for (const auto& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* Slot = nullptr;
			if (!Value->TryGetObject(Slot) || !Slot)
			{
				Error = TEXT("invalid material-slot declaration");
				return false;
			}
			FElysiumCharacterMaterialSlot& Out = Record.MaterialSlots.AddDefaulted_GetRef();
			(*Slot)->TryGetNumberField(TEXT("slot"), Out.SourceIndex);
			Out.Name = FName(*ElysiumJson::Str((*Slot).ToSharedRef(), TEXT("sourceName")));
			Out.MaterialId = ElysiumJson::Str((*Slot).ToSharedRef(), TEXT("material"));
			if (Out.SourceIndex != Record.MaterialSlots.Num() - 1 || Out.MaterialId.IsEmpty())
			{
				Error = TEXT("material columns are not ordered or have no source identity");
				return false;
			}
		}
		if (!Data->TryGetArrayField(TEXT("skinFamilies"), Rows) || !Rows)
		{
			Error = TEXT("missing skin-family declaration");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* SkinTable = nullptr;
		if (!Data->TryGetArrayField(TEXT("skinTable"), SkinTable) || !SkinTable || SkinTable->Num() != Rows->Num())
		{
			Error = TEXT("skin-family mapping is absent or has a different family count");
			return false;
		}
		int32 FamilyIndex = 0, Columns = INDEX_NONE;
		for (const auto& Value : *Rows)
		{
			const TArray<TSharedPtr<FJsonValue>>* Materials = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
			if (!Value->TryGetArray(Materials) || !Materials
				|| !(*SkinTable)[FamilyIndex++]->TryGetArray(Indices) || !Indices || Indices->Num() != Materials->Num()
				|| (Columns != INDEX_NONE && Columns != Materials->Num()))
			{
				Error = TEXT("skin family does not preserve its declared reference columns");
				return false;
			}
			Columns = Materials->Num();
			FElysiumCharacterSkinFamily& Family = Record.SkinFamilies.AddDefaulted_GetRef();
			for (const auto& IndexValue : *Indices)
			{
				int32 Index = INDEX_NONE;
				if (!IndexValue->TryGetNumber(Index) || !Record.MaterialSlots.IsValidIndex(Index))
				{
					Error = TEXT("skin reference names an absent material declaration");
					return false;
				}
				Family.TextureIndices.Add(Index);
			}
			for (const auto& Material : *Materials)
			{
				FString Path;
				if (!Material->TryGetString(Path) || !FSoftObjectPath(Path).IsValid())
				{
					Error = TEXT("skin family contains an invalid material reference");
					return false;
				}
				Family.Materials.Add(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Path)));
			}
		}
		Record.bHasMeshData = true;
		return true;
	}
}

UElysiumCharacterProvenance* UElysiumCharacterProvenance::ApplyJson(UObject* Asset, const FString& Json, FString& OutError)
{
	IInterface_AssetUserData* Interface = Cast<IInterface_AssetUserData>(Asset);
	if (!Interface)
	{
		OutError = TEXT("skeletal asset does not support asset user data");
		return nullptr;
	}
	TSharedPtr<FJsonObject> Parsed;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Parsed) || !Parsed.IsValid())
	{
		OutError = TEXT("invalid character provenance JSON");
		return nullptr;
	}
	const TSharedRef<FJsonObject> Object = Parsed.ToSharedRef();
	UElysiumCharacterProvenance* Record = NewObject<UElysiumCharacterProvenance>(Asset);
	Record->AssetId = ElysiumJson::Str(Object, TEXT("assetId"));
	Record->OwnerRoot = ElysiumJson::Str(Object, TEXT("ownerRoot"));
	Record->UnitSha256 = ElysiumJson::Str(Object, TEXT("unitSha256"));
	Record->PayloadSha256 = ElysiumJson::Str(Object, TEXT("payloadSha256"));
	Record->SourceUnits = ElysiumJson::Strings(Object, TEXT("sourceUnits"));
	Record->BankFamilyTreeSha256 = ElysiumJson::Str(Object, TEXT("bankFamilyTreeSha256"));
	Record->RecipeFingerprint = ElysiumJson::Str(Object, TEXT("recipeFingerprint"));
	Object->TryGetNumberField(TEXT("sourceGarmentCount"),Record->SourceGarmentCount);
	if (Record->AssetId.IsEmpty() && Record->SourceUnits.IsEmpty())
	{
		OutError = TEXT("character provenance names no source unit");
		return nullptr;
	}
	const TSharedPtr<FJsonObject>* MeshData = nullptr;
	if (Parsed->TryGetObjectField(TEXT("meshData"), MeshData) && MeshData)
	{
		if (!Cast<USkeletalMesh>(Asset))
		{
			OutError = TEXT("mesh-aligned character data must be attached to a skeletal mesh");
			return nullptr;
		}
		if (!ElysiumCharacterData::ReadMesh(*Record, *MeshData, OutError)) return nullptr;
	}
	const FString DynamicsPath=ElysiumJson::Str(Object,TEXT("dynamicsAsset"));
	const FString PhysicsPath = ElysiumJson::Str(Object, TEXT("physicsSourceAsset"));
	if (!PhysicsPath.IsEmpty())
	{
		Record->PhysicsSourceData = LoadObject<UElysiumPhysicsData>(nullptr, *PhysicsPath);
		if (!Record->bHasMeshData || !Record->PhysicsSourceData || Record->PhysicsSourceData->Data.AssetId != Record->AssetId)
		{
			OutError = TEXT("mesh physics source reference is absent or names a different model: ") + PhysicsPath;
			return nullptr;
		}
	}
	if (!DynamicsPath.IsEmpty())
	{
		Record->Dynamics=LoadObject<UElysiumDynamicsData>(nullptr,*DynamicsPath);
		if (!Record->bHasMeshData || !Record->Dynamics || Record->Dynamics->AssetId!=Record->AssetId)
		{
			OutError=TEXT("mesh dynamics reference is absent or names a different model: ")+DynamicsPath;
			return nullptr;
		}
	}
#if WITH_EDITORONLY_DATA
	Record->AuthoringEvidence = Json;
#endif
	for (const FString& Path : ElysiumJson::Strings(Object,TEXT("clothAssets")))
	{
		auto* Cloth=LoadObject<UChaosClothAsset>(nullptr,*Path);
		if (!Record->bHasMeshData || !Cloth)
		{
			OutError=TEXT("mesh cloth reference is absent: ")+Path; return nullptr;
		}
		Record->ClothAssets.Add(Cloth);
	}
	Interface->RemoveUserDataOfClass(StaticClass());
	Interface->AddAssetUserData(Record);
#if WITH_EDITORONLY_DATA
	FMetaData& Meta = Asset->GetOutermost()->GetMetaData();
	Meta.SetValue(Asset, TEXT("ElysiumAssetId"), *Record->AssetId);
	Meta.SetValue(Asset, TEXT("ElysiumProducer"), TEXT("characters"));
#endif
	Asset->MarkPackageDirty();
	OutError.Reset();
	return Record;
}

FString UElysiumCharacterProvenance::VerifyMeshData(UObject* Asset, const FString& Json)
{
	IInterface_AssetUserData* Interface = Cast<IInterface_AssetUserData>(Asset);
	const auto* Record = Interface ? Cast<UElysiumCharacterProvenance>(Interface->GetAssetUserDataOfClass(StaticClass())) : nullptr;
	if (!Record || !Record->bHasMeshData) return TEXT("mesh has no cooked character data");
	TSharedPtr<FJsonObject> Parsed;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Parsed) || !Parsed.IsValid())
		return TEXT("invalid staged mesh-data JSON");
	UElysiumCharacterProvenance* Expected = NewObject<UElysiumCharacterProvenance>();
	FString Error;
	if (!ElysiumCharacterData::ReadMesh(*Expected, Parsed, Error)) return Error;
	for (const TCHAR* Field : {TEXT("bHasMeshData"), TEXT("ModelPath"), TEXT("Stem"), TEXT("Facial"),
		TEXT("Eyes"), TEXT("Composition"), TEXT("MaterialSlots"), TEXT("SkinFamilies"),
		TEXT("bHasExpressionData"), TEXT("ExpressionModelFlags"), TEXT("bExpressionModelIsMale"), TEXT("ExpressionSelections")})
	{
		const FProperty* Property = FindFProperty<FProperty>(StaticClass(), Field);
		if (!Property || !Property->Identical_InContainer(Record, Expected))
			return FString::Printf(TEXT("saved mesh data differs in %s"), Field);
	}
	return FString();
}

const UElysiumCharacterProvenance* UElysiumCharacterProvenance::Find(const USkeletalMesh* Mesh)
{
	const TArray<UAssetUserData*>* Data = Mesh ? Mesh->GetAssetUserDataArray() : nullptr;
	if (Data)
	{
		for (const UAssetUserData* Value : *Data)
			if (const auto* Record = Cast<UElysiumCharacterProvenance>(Value)) return Record;
	}
	return nullptr;
}
