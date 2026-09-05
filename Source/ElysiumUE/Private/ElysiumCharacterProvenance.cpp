#include "ElysiumCharacterProvenance.h"
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
#if WITH_EDITORONLY_DATA
	Record->AuthoringEvidence = Json;
#endif
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
		TEXT("Eyes"), TEXT("Composition"), TEXT("MaterialSlots"), TEXT("SkinFamilies")})
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
