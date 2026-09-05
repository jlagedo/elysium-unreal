#include "ElysiumSkyProvenance.h"

#include "Engine/TextureCube.h"
#if WITH_EDITOR
#include "ElysiumContentPaths.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSkyProvenance, Log, All);

UElysiumSkyProvenance* UElysiumSkyProvenance::ApplyJson(
	UTextureCube* Cube, const FString& Json, FString& OutError)
{
	OutError.Reset();
	auto Refuse = [&OutError](const FString& Reason) -> UElysiumSkyProvenance*
	{
		OutError = Reason;
		UE_LOG(LogElysiumSkyProvenance, Warning, TEXT("%s"), *Reason);
		return nullptr;
	};
#if WITH_EDITOR
	TSharedPtr<FJsonObject> Object;
	if (!Cube || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object)
		return Refuse(TEXT("sky provenance needs a cube and a JSON object"));
	FString Product, Schema, Sky, Path, Recipe, MeanMethod, Format, Compression;
	double Mean = 0, Width = 0, Height = 0, Faces = 0, Mips = 0;
	bool bSrgb = false;
	const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Hashes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Mapping = nullptr;
	if (!Object->TryGetStringField(TEXT("product"), Product) || Product != TEXT("sky-composite")
		|| !Object->TryGetStringField(TEXT("schemaVersion"), Schema) || Schema != TEXT("1.0.0")
		|| !Object->TryGetStringField(TEXT("skyName"), Sky) || Sky.IsEmpty()
		|| !Object->TryGetStringField(TEXT("assetPath"), Path)
		|| !Object->TryGetStringField(TEXT("recipeSha256"), Recipe) || Recipe.Len() != 64
		|| !Object->TryGetStringField(TEXT("meanMethod"), MeanMethod)
		|| MeanMethod != TEXT("mip0-upper-z-solid-angle-rec709-pow2.2-v1")
		|| !Object->TryGetNumberField(TEXT("upperHemisphereMean"), Mean) || !FMath::IsFinite(Mean) || Mean < 0
		|| !Object->TryGetNumberField(TEXT("width"), Width)
		|| !Object->TryGetNumberField(TEXT("height"), Height)
		|| !Object->TryGetNumberField(TEXT("faces"), Faces) || Faces != 6
		|| !Object->TryGetNumberField(TEXT("mipCount"), Mips)
		|| !Object->TryGetArrayField(TEXT("sourceUnits"), Sources) || Sources->Num() != 6
		|| !Object->TryGetArrayField(TEXT("faceMapping"), Mapping) || Mapping->Num() != 6
		|| !Object->TryGetArrayField(TEXT("sourceMipMd5"), Hashes) || Hashes->Num() != Mips
		|| !Object->TryGetStringField(TEXT("stagedFormat"), Format) || Format != TEXT("rgba32f")
		|| !Object->TryGetStringField(TEXT("compression"), Compression) || Compression != TEXT("hdr-f32")
		|| !Object->TryGetBoolField(TEXT("srgb"), bSrgb) || bSrgb)
		return Refuse(TEXT("sky provenance is incomplete or has unsupported projection settings"));

	for (TCHAR C : Sky)
	{
		if (!((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9') || C == '_' || C == '-'))
			return Refuse(TEXT("sky provenance has an invalid sky name"));
	}
	const FString Canonical = FElysiumContentPaths::BakedUnit(TEXT("vtmb:texture:skybox/") + Sky,
		TEXT("TC"), TEXT("Sky"));
	const FString ObjectPath = Path + TEXT(".") + FPackageName::GetShortName(Path);
	if (Canonical.IsEmpty() || Canonical != ObjectPath || Cube->GetPathName() != ObjectPath)
		return Refuse(TEXT("sky provenance path is not this cube's canonical _Sky address"));

	if (!Cube->Source.IsValid() || Width != Cube->Source.GetSizeX() || Height != Width
		|| Height != Cube->Source.GetSizeY() || Cube->Source.GetNumSlices() != 6
		|| Mips != Cube->Source.GetNumMips() || Mips < 1 || Cube->Source.GetFormat() != TSF_RGBA32F)
		return Refuse(TEXT("sky source extent/format/mips differ from the projection"));
	if (Cube->SRGB || Cube->CompressionSettings != TC_HDR_F32)
		return Refuse(TEXT("sky source must retain linear input and the HDR F32 build policy"));
	for (int32 Mip = 0; Mip < Cube->Source.GetNumMips(); ++Mip)
	{
		FString Expected;
		TArray64<uint8> Bytes;
		if (!(*Hashes)[Mip]->TryGetString(Expected) || !Cube->Source.GetMipData(Bytes, Mip)
			|| FMD5::HashBytes(Bytes.GetData(), Bytes.Num()) != Expected)
			return Refuse(FString::Printf(TEXT("sky source mip %d: six-face byte conservation failed"), Mip));
	}

	UElysiumSkyProvenance* Record = NewObject<UElysiumSkyProvenance>(Cube, NAME_None, RF_Public | RF_Transactional);
	Record->SkyName = Sky;
	Record->UpperHemisphereMean = Mean;
	Record->MipCount = Cube->Source.GetNumMips();
	Record->RecipeSha256 = Recipe;
	Record->ProvenanceJson = Json;
	Cube->AddAssetUserData(Record);
	Cube->MarkPackageDirty();
	return Record;
#else
	return Refuse(TEXT("sky provenance authoring requires the editor"));
#endif
}

const UElysiumSkyProvenance* UElysiumSkyProvenance::Find(const UTextureCube* Cube)
{
	return Cube ? Cast<UElysiumSkyProvenance>(const_cast<UTextureCube*>(Cube)->GetAssetUserDataOfClass(
		UElysiumSkyProvenance::StaticClass())) : nullptr;
}
