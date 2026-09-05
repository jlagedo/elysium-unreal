#include "Visual/ElysiumCharacterModel.h"
#include "ElysiumCharacterProvenance.h"
#include "Engine/SkeletalMesh.h"

FString ElysiumCharacterModel::IdFromSource(const FString& Model)
{
	FString Normal = Model.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/")).ToLower();
	FString Key;
	if (Normal.StartsWith(TEXT("vtmb:model:"))) Key = Normal.Mid(11);
	else if (Normal.StartsWith(TEXT("models/")) && Normal.EndsWith(TEXT(".mdl")))
		Key = Normal.Mid(7, Normal.Len() - 11);
	else return FString();
	TArray<FString> Parts;
	Key.ParseIntoArray(Parts, TEXT("/"), false);
	if (Parts.IsEmpty()) return FString();
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || Part == TEXT(".") || Part == TEXT("..") || Part.StartsWith(TEXT("_"))) return FString();
		for (TCHAR C : Part)
			if (C < 32 || C == TEXT(':') || C == TEXT('|') || C == TEXT('"')
				|| C == TEXT('<') || C == TEXT('>') || C == TEXT('?') || C == TEXT('*')) return FString();
	}
	return TEXT("vtmb:model:") + Key;
}

bool ElysiumCharacterModel::IsCanonicalId(const FString& ModelId)
{
	return ModelId.StartsWith(TEXT("vtmb:model:"), ESearchCase::CaseSensitive) && IdFromSource(ModelId) == ModelId;
}

const UElysiumCharacterProvenance* ElysiumCharacterModel::Validate(
	const FString& ModelId, const USkeletalMesh* Mesh, FString& OutError)
{
	OutError.Reset();
	if (!IsCanonicalId(ModelId)) { OutError = TEXT("expected canonical model ID, got: ") + ModelId; return nullptr; }
	if (!Mesh) { OutError = TEXT("model mesh is not prepared: ") + ModelId; return nullptr; }
	const auto* Record = UElysiumCharacterProvenance::Find(Mesh);
	if (!Record || !Record->bHasMeshData || !Record->bHasExpressionData)
	{ OutError = TEXT("native character mesh has incomplete cooked provenance: ") + Mesh->GetPathName(); return nullptr; }
	if (Record->AssetId != ModelId || IdFromSource(Record->ModelPath) != ModelId)
	{ OutError = TEXT("native character mesh/provenance disagrees with model ID: ") + ModelId + TEXT(" / ") + Record->AssetId; return nullptr; }
	return Record;
}

FString ElysiumCharacterModel::AnimationCacheIdentity(const FString& ModelId, const USkeletalMesh* Mesh)
{
	FString Error;
	const auto* Record = Validate(ModelId, Mesh, Error);
	return Record ? ModelId + TEXT("|") + Mesh->GetPathName() + TEXT("|") + Record->RecipeFingerprint : FString();
}
