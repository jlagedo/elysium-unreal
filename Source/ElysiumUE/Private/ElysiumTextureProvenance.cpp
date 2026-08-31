#include "ElysiumTextureProvenance.h"

#include "DDSFile.h"
#include "ElysiumJsonField.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PixelFormat.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTextureProvenance, Log, All);

using namespace ElysiumJson;

const FName UElysiumTextureProvenance::TagAssetId(TEXT("ElysiumAssetId"));
const FName UElysiumTextureProvenance::TagSourceFormat(TEXT("ElysiumSourceFormat"));
const FName UElysiumTextureProvenance::TagRole(TEXT("ElysiumRole"));
const FName UElysiumTextureProvenance::TagRoleConflict(TEXT("ElysiumRoleConflict"));

namespace
{
	// An array element read as a number, or false: a wrong-typed element never reaches AsNumber,
	// which would log a LogJson error for a sidecar this parser is meant to tolerate. Not part of
	// ElysiumJsonField.h's shared readers: unique to this lane's fixed-length reflectivity array.
	bool ElementNumber(const TSharedPtr<FJsonValue>& Value, double& Out)
	{
		return Value.IsValid() && Value->TryGetNumber(Out);
	}
}

void UElysiumTextureProvenance::FromJson(const TSharedRef<FJsonObject>& O)
{
	AssetId = Str(O, TEXT("assetId"));
	TexturePath = Str(O, TEXT("texturePath"));
	AssetPath = Str(O, TEXT("assetPath"));
	UnitGlb = Str(O, TEXT("unitGlb"));
	UnitSchemaVersion = Str(O, TEXT("unitSchemaVersion"));
	UnitSha256 = Str(O, TEXT("unitSha256"));
	PayloadSha256 = Str(O, TEXT("payloadSha256"));
	SettingsVersion = Str(O, TEXT("settingsVersion"));

	SourceFormat = Str(O, TEXT("sourceFormat"));
	SourceFormatEnum = static_cast<int32>(Int(O, TEXT("sourceFormatEnum")));
	VkFormat = static_cast<int32>(Int(O, TEXT("vkFormat")));
	VkFormatName = Str(O, TEXT("vkFormatName"));
	VtfVersion = Str(O, TEXT("vtfVersion"));
	TthVersion = static_cast<int32>(Int(O, TEXT("tthVersion")));
	Flags = static_cast<int32>(Int(O, TEXT("flags")));
	StartFrame = static_cast<int32>(Int(O, TEXT("startFrame")));
	BumpScale = static_cast<float>(Num(O, TEXT("bumpScale"), 1.0));
	if (const TArray<TSharedPtr<FJsonValue>>* R = Arr(O, TEXT("reflectivity")); R && R->Num() == 3)
	{
		double Channels[3] = {0.0, 0.0, 0.0};
		if (ElementNumber((*R)[0], Channels[0]) && ElementNumber((*R)[1], Channels[1])
			&& ElementNumber((*R)[2], Channels[2]))
		{
			Reflectivity = FVector3f(
				static_cast<float>(Channels[0]), static_cast<float>(Channels[1]),
				static_cast<float>(Channels[2]));
		}
	}

	Width = static_cast<int32>(Int(O, TEXT("width")));
	Height = static_cast<int32>(Int(O, TEXT("height")));
	Frames = static_cast<int32>(Int(O, TEXT("frames"), 1));
	Faces = static_cast<int32>(Int(O, TEXT("faces"), 1));
	MipCount = static_cast<int32>(Int(O, TEXT("mipCount")));
	SourceMipCount = static_cast<int32>(Int(O, TEXT("sourceMipCount")));
	PartialMipChain = Bool(O, TEXT("partialMipChain"));
	ExpandedFromRgb8 = Bool(O, TEXT("expandedFromRgb8"));

	if (TSharedPtr<FJsonObject> S = Obj(O, TEXT("sampling")))
	{
		const TSharedRef<FJsonObject> SR = S.ToSharedRef();
		Sampling.bPointSample = Bool(SR, TEXT("pointSample"));
		Sampling.bTrilinear = Bool(SR, TEXT("trilinear"));
		Sampling.bClampS = Bool(SR, TEXT("clampS"));
		Sampling.bClampT = Bool(SR, TEXT("clampT"));
		Sampling.bAnisotropic = Bool(SR, TEXT("anisotropic"));
		Sampling.bNoMip = Bool(SR, TEXT("noMip"));
		Sampling.bNoLod = Bool(SR, TEXT("noLod"));
		Sampling.bAllMips = Bool(SR, TEXT("allMips"));
	}

	Members.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* M = Arr(O, TEXT("members")))
	{
		for (const TSharedPtr<FJsonValue>& V : *M)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!V.IsValid() || !V->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumTextureSourceMember& Member = Members.AddDefaulted_GetRef();
			Member.Role = Str(Row, TEXT("role"));
			Member.Path = Str(Row, TEXT("path"));
			Member.ByteLength = Int(Row, TEXT("byteLength"));
			Member.Sha256 = Str(Row, TEXT("sha256"));
			// The stage flattens the unit's origin onto the row (`originKind`, `container`,
			// `offset`, `size`); a row that still carries the unit's own nested `origin` object
			// reads the same way, so either shape of sidecar lands.
			Member.OriginKind = Str(Row, TEXT("originKind"));
			Member.Container = Str(Row, TEXT("container"));
			Member.Offset = Int(Row, TEXT("offset"));
			Member.Size = Int(Row, TEXT("size"));
			if (TSharedPtr<FJsonObject> Origin = Obj(Row, TEXT("origin")))
			{
				const TSharedRef<FJsonObject> OR = Origin.ToSharedRef();
				Member.OriginKind = Str(OR, TEXT("kind"), Member.OriginKind);
				Member.Container = Str(OR, TEXT("container"), Str(OR, TEXT("root"), Member.Container));
				Member.Offset = Int(OR, TEXT("offset"), Member.Offset);
				Member.Size = Int(OR, TEXT("size"), Member.Size);
			}
			// A loose member has no container span: the stage writes `size` 0 (or omits it), and
			// the member's size is then the file's own length.
			if (Member.Size <= 0)
			{
				Member.Size = Member.ByteLength;
			}
		}
	}

	Role = Str(O, TEXT("role"));
	RoleConflict = Bool(O, TEXT("roleConflict"));
	TwinOf = Str(O, TEXT("twinOf"));
	RoleEvidence.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* E = Arr(O, TEXT("roleEvidence")))
	{
		for (const TSharedPtr<FJsonValue>& V : *E)
		{
			FString Parameter;
			if (V.IsValid() && V->TryGetString(Parameter))
			{
				RoleEvidence.Add(Parameter);
			}
		}
	}

	FaceMapping.Reset();
	if (const TArray<TSharedPtr<FJsonValue>>* F = Arr(O, TEXT("faceMapping")))
	{
		for (const TSharedPtr<FJsonValue>& V : *F)
		{
			const TSharedPtr<FJsonObject>* RowPtr = nullptr;
			if (!V.IsValid() || !V->TryGetObject(RowPtr) || !RowPtr)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Row = (*RowPtr).ToSharedRef();
			FElysiumCubeFaceMapping& Face = FaceMapping.AddDefaulted_GetRef();
			Face.UnrealFace = Str(Row, TEXT("unrealFace"));
			Face.KtxFace = static_cast<int32>(Int(Row, TEXT("ktxFace")));
			Face.SourceFace = static_cast<int32>(Int(Row, TEXT("sourceFace")));
			Face.Transform = Str(Row, TEXT("transform"), TEXT("identity"));
		}
	}
}

UElysiumTextureProvenance* UElysiumTextureProvenance::ApplyJson(UTexture* Texture, const FString& Json, FString& OutError)
{
	OutError.Reset();
	if (!Texture)
	{
		OutError = TEXT("no texture");
		return nullptr;
	}
	// Deserialize the top-level value rather than an object, so a well-formed document that is
	// not an object (`[]`, `5`, `"x"`) is refused by type and not by whatever the object overload
	// happens to do with it.
	TSharedPtr<FJsonValue> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		OutError = FString::Printf(TEXT("provenance sidecar does not parse as JSON: %s"), *Reader->GetErrorMessage());
		return nullptr;
	}
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (!Parsed->TryGetObject(Object) || !Object || !Object->IsValid())
	{
		OutError = TEXT("provenance sidecar is not a JSON object");
		return nullptr;
	}

	// The texture is the outer, so the record serializes inside the asset's package; a flag-less
	// NewObject would land it in the transient package and the save would drop it.
	UElysiumTextureProvenance* Record = NewObject<UElysiumTextureProvenance>(Texture, NAME_None, RF_Public | RF_Transactional);
	Record->FromJson(Object->ToSharedRef());
	// UTexture::AddAssetUserData removes an existing instance of the same class first, so a
	// re-import replaces rather than accumulates.
	Texture->AddAssetUserData(Record);
	Texture->MarkPackageDirty();
	return Record;
}

const UElysiumTextureProvenance* UElysiumTextureProvenance::Find(const UTexture* Texture)
{
	if (!Texture)
	{
		return nullptr;
	}
	// GetAssetUserDataOfClass is non-const on the interface; the lookup itself mutates nothing.
	return Cast<UElysiumTextureProvenance>(
		const_cast<UTexture*>(Texture)->GetAssetUserDataOfClass(UElysiumTextureProvenance::StaticClass()));
}

void UElysiumTextureProvenance::StampRegistryTags(UTexture* Texture, bool& bOutStamped, FString& OutError)
{
	bOutStamped = false;
	OutError.Reset();
	const UElysiumTextureProvenance* Record = Find(Texture);
	if (!Record)
	{
		OutError = TEXT("texture carries no ElysiumTextureProvenance");
		bOutStamped = false;
		return;
	}
#if WITH_EDITORONLY_DATA
	UPackage* Package = Texture->GetPackage();
	if (!Package)
	{
		OutError = TEXT("texture has no package");
		bOutStamped = false;
		return;
	}
	FMetaData& Meta = Package->GetMetaData();
	Meta.SetValue(Texture, TagAssetId, *Record->AssetId);
	Meta.SetValue(Texture, TagSourceFormat, *Record->SourceFormat);
	Meta.SetValue(Texture, TagRole, *Record->Role);
	Meta.SetValue(Texture, TagRoleConflict, Record->RoleConflict ? TEXT("1") : TEXT("0"));
	Texture->MarkPackageDirty();
	bOutStamped = true;
	return;
#else
	OutError = TEXT("package metadata is editor-only data");
	bOutStamped = false;
		return;
#endif
}

// ---------------------------------------------------------------------------------------------
// UElysiumTextureImportLibrary
// ---------------------------------------------------------------------------------------------

namespace
{
	FTexturePlatformData* PlatformDataOf(UTexture* Texture)
	{
		if (!Texture)
		{
			return nullptr;
		}
#if WITH_EDITOR
		// An import leaves the build queued; the platform data is only meaningful once it lands.
		Texture->BlockOnAnyAsyncBuild();
#endif
		if (UTexture2D* T2 = Cast<UTexture2D>(Texture))
		{
			return T2->GetPlatformData();
		}
		if (UTextureCube* TC = Cast<UTextureCube>(Texture))
		{
			return TC->GetPlatformData();
		}
		if (UTexture2DArray* TA = Cast<UTexture2DArray>(Texture))
		{
			return TA->GetPlatformData();
		}
		return nullptr;
	}

#if WITH_EDITOR
	bool DxgiFor(EPixelFormat Format, UE::DDS::EDXGIFormat& Out)
	{
		switch (Format)
		{
		case PF_DXT1: Out = UE::DDS::EDXGIFormat::BC1_UNORM; return true;
		case PF_DXT3: Out = UE::DDS::EDXGIFormat::BC2_UNORM; return true;
		case PF_DXT5: Out = UE::DDS::EDXGIFormat::BC3_UNORM; return true;
		case PF_B8G8R8A8: Out = UE::DDS::EDXGIFormat::B8G8R8A8_UNORM; return true;
		case PF_R8G8B8A8: Out = UE::DDS::EDXGIFormat::R8G8B8A8_UNORM; return true;
		case PF_G8: Out = UE::DDS::EDXGIFormat::R8_UNORM; return true;
		default: return false;
		}
	}

	int64 ImageBytes(EPixelFormat Format, int32 Width, int32 Height)
	{
		const FPixelFormatInfo& Info = GPixelFormats[Format];
		const int64 BlocksX = FMath::Max(1, FMath::DivideAndRoundUp(Width, int32(Info.BlockSizeX)));
		const int64 BlocksY = FMath::Max(1, FMath::DivideAndRoundUp(Height, int32(Info.BlockSizeY)));
		return BlocksX * BlocksY * int64(Info.BlockBytes);
	}
#endif // WITH_EDITOR
}

void UElysiumTextureImportLibrary::WriteBuiltMipZeroAsDds(UTexture* Texture, const FString& Path, bool& bOutWritten, FString& OutError)
{
	bOutWritten = false;
	OutError.Reset();
#if !WITH_EDITOR
	// FTexturePlatformData::TryInlineMipData and the derived-data build behind it exist only in the
	// editor; a packaged game has no built-mip export to offer the measure lane.
	OutError = TEXT("built mip export needs the editor's derived-data build");
	bOutWritten = false;
		return;
#else
	FTexturePlatformData* PD = PlatformDataOf(Texture);
	if (!PD || PD->Mips.Num() == 0)
	{
		OutError = TEXT("no built platform data");
		bOutWritten = false;
		return;
	}
	UE::DDS::EDXGIFormat Dxgi;
	if (!DxgiFor(PD->PixelFormat, Dxgi))
	{
		OutError = FString::Printf(TEXT("built format %s is not one the measure lane decodes"),
			GetPixelFormatString(PD->PixelFormat));
		bOutWritten = false;
		return;
	}
	// Mip 0 of an editor build normally lives in derived data, not in the mip's bulk data; inlining
	// pulls every level into BulkData so it can be locked like a cooked mip.
	if (!PD->TryInlineMipData(0, Texture->GetPathName()))
	{
		OutError = TEXT("mip data could not be inlined from derived data");
		bOutWritten = false;
		return;
	}
	FTexture2DMipMap& Mip = PD->Mips[0];
	const int64 Available = Mip.BulkData.GetBulkDataSize();
	const int64 Wanted = ImageBytes(PD->PixelFormat, Mip.SizeX, Mip.SizeY);
	if (Available < Wanted || Wanted <= 0)
	{
		OutError = FString::Printf(TEXT("mip 0 holds %lld bytes, one %dx%d image needs %lld"),
			Available, int32(Mip.SizeX), int32(Mip.SizeY), Wanted);
		bOutWritten = false;
		return;
	}

	UE::DDS::EDDSError Error = UE::DDS::EDDSError::OK;
	UE::DDS::FDDSFile* Dds = UE::DDS::FDDSFile::CreateEmpty2D(Mip.SizeX, Mip.SizeY, 1, Dxgi, UE::DDS::FDDSFile::CREATE_FLAG_NONE, &Error);
	if (!Dds)
	{
		OutError = FString::Printf(TEXT("DDS header could not be built (error %d)"), int32(Error));
		bOutWritten = false;
		return;
	}
	ON_SCOPE_EXIT { delete Dds; };
	if (Dds->Mips.Num() != 1 || Dds->Mips[0].DataSize != Wanted)
	{
		OutError = TEXT("DDS mip storage does not match the built image size");
		bOutWritten = false;
		return;
	}
	// The first image of the level: the whole level for a 2D texture, face 0 of a cubemap, slice
	// 0 of an array. Slices are laid out contiguously in the level's bulk data.
	const uint8* Source = static_cast<const uint8*>(Mip.BulkData.LockReadOnly());
	FMemory::Memcpy(Dds->Mips[0].Data, Source, Wanted);
	Mip.BulkData.Unlock();

	TArray64<uint8> Bytes;
	Error = Dds->WriteDDS(Bytes, UE::DDS::EDDSFormatVersion::D3D10);
	if (Error != UE::DDS::EDDSError::OK)
	{
		OutError = FString::Printf(TEXT("DDS could not be serialized (error %d)"), int32(Error));
		bOutWritten = false;
		return;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree*/ true);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *Path);
		bOutWritten = false;
		return;
	}
	bOutWritten = true;
	return;
#endif // WITH_EDITOR
}

int32 UElysiumTextureImportLibrary::BuiltMipCount(UTexture* Texture)
{
	const FTexturePlatformData* PD = PlatformDataOf(Texture);
	return PD ? PD->Mips.Num() : -1;
}

FString UElysiumTextureImportLibrary::BuiltPixelFormat(UTexture* Texture)
{
	const FTexturePlatformData* PD = PlatformDataOf(Texture);
	return PD ? FString(GetPixelFormatString(PD->PixelFormat)) : FString();
}

void UElysiumTextureImportLibrary::BuiltExtent(UTexture* Texture, int32& OutWidth, int32& OutHeight, int32& OutSlices, int32& OutMips)
{
	OutWidth = OutHeight = OutSlices = OutMips = 0;
	const FTexturePlatformData* PD = PlatformDataOf(Texture);
	if (!PD)
	{
		return;
	}
	OutWidth = PD->SizeX;
	OutHeight = PD->SizeY;
	OutSlices = PD->GetNumSlices();
	OutMips = PD->Mips.Num();
}
