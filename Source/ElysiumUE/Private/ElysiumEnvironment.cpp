#include "ElysiumEnvironment.h"

#include "Engine/TextureCube.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEnv, Log, All);

// ---------------------------------------------------------------------------
// .env sidecar
// ---------------------------------------------------------------------------

bool FElysiumEnvDef::Parse(const FString& EnvPath, FElysiumEnvDef& Out)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *EnvPath))
	{
		return false;
	}

	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() == 0)
		{
			continue;
		}
		const FString& Key = Tok[0];
		if (Key == TEXT("skybox") && Tok.Num() >= 2)
		{
			Out.bSky = Tok[1] == TEXT("1");
		}
		else if (Key == TEXT("skyname") && Tok.Num() >= 2)
		{
			Out.SkyName = Tok[1];
		}
		else if (Key == TEXT("skyconv") && Tok.Num() >= 2)
		{
			Out.SkyConvention = FCString::Atoi(*Tok[1]);
		}
		else if (Key == TEXT("fog") && Tok.Num() >= 2)
		{
			Out.bFog = Tok[1] == TEXT("1");
		}
		else if (Key == TEXT("fogcolor") && Tok.Num() >= 4)
		{
			Out.FogColor = FLinearColor(FCString::Atof(*Tok[1]), FCString::Atof(*Tok[2]), FCString::Atof(*Tok[3]));
		}
		else if (Key == TEXT("fogstart") && Tok.Num() >= 2)
		{
			Out.FogStartCm = FCString::Atof(*Tok[1]);
		}
		else if (Key == TEXT("fogend") && Tok.Num() >= 2)
		{
			Out.FogEndCm = FCString::Atof(*Tok[1]);
		}
	}
	return true;
}

namespace
{
	// Decode a PNG on disk into tightly packed BGRA8. Returns false (and leaves OutSize 0)
	// on a missing/undecodable file.
	bool LoadPngBgra(const FString& Path, int32& OutW, int32& OutH, TArray64<uint8>& OutBgra)
	{
		OutW = OutH = 0;
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *Path))
		{
			return false;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
		{
			return false;
		}
		if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, OutBgra))
		{
			return false;
		}
		OutW = Wrapper->GetWidth();
		OutH = Wrapper->GetHeight();
		return true;
	}
}

// ---------------------------------------------------------------------------
// Sky faces -> IBL cubemap
// ---------------------------------------------------------------------------

UTextureCube* ElysiumEnvironment::BuildSkyCube(const FString& TexDir)
{
	// Unreal cube face order +X,-X,+Y,-Y,+Z,-Z.
	//
	// This binding is WRONG on every horizon face and the backdrop draws incorrectly because
	// of it, in two separate ways. VtMB's own tables (engine.dll R_DrawSkyBox/MakeSkyVec, RE'd
	// in docs/sky-ambience.md -> "K1 ... (settled)") put the Source faces on +X=rt, -X=lf,
	// +Y=bk, -Y=ft, so under our Source->Unreal transform (sx,-sy,sz) the correct order here
	// is rt, lf, ft, bk, up, dn. And a slice is the D3D face table applied to the raw Unreal
	// world vector ("K2 ... (settled)"), which assumes Y-up where Unreal is Z-up, so each face
	// also needs a rotation on the way in: rt 90 CCW, lf 90 CW, ft 180, bk none, up 90 CCW,
	// dn 90 CCW. Both land together as sky-ambience B3.
	static const TCHAR* FaceOrder[6] = { TEXT("ft"), TEXT("bk"), TEXT("rt"), TEXT("lf"), TEXT("up"), TEXT("dn") };

	TArray64<uint8> Faces[6];
	int32 Size = 0;
	for (int32 F = 0; F < 6; ++F)
	{
		const FString Path = TexDir / FString::Printf(TEXT("sky_%s.png"), FaceOrder[F]);
		int32 W = 0, H = 0;
		if (!LoadPngBgra(Path, W, H, Faces[F]) || W <= 0 || W != H)
		{
			UE_LOG(LogElysiumEnv, Warning, TEXT("sky face missing or non-square: %s"), *Path);
			return nullptr;
		}
		if (Size == 0)
		{
			Size = W;
		}
		else if (W != Size)
		{
			UE_LOG(LogElysiumEnv, Warning, TEXT("sky faces differ in size: %s"), *Path);
			return nullptr;
		}
	}

	UTextureCube* Cube = NewObject<UTextureCube>(GetTransientPackage(), NAME_None, RF_Transient);
	Cube->SRGB = true;
	Cube->NeverStream = true;

	FTexturePlatformData* PD = new FTexturePlatformData();
	PD->SizeX = Size;
	PD->SizeY = Size;
	PD->PixelFormat = PF_B8G8R8A8;
	PD->SetIsCubemap(true);
	PD->SetNumSlices(6);   // the six faces; the single mip holds them contiguously

	// Mip SizeZ is unused for cubemaps; the face count lives in the platform data above.
	const int64 FaceBytes = int64(Size) * Size * 4;
	FTexture2DMipMap* Mip = new FTexture2DMipMap(Size, Size, 1);
	PD->Mips.Add(Mip);
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* Dest = (uint8*)Mip->BulkData.Realloc(FaceBytes * 6);
	for (int32 F = 0; F < 6; ++F)
	{
		FMemory::Memcpy(Dest + FaceBytes * F, Faces[F].GetData(), FaceBytes);
	}
	Mip->BulkData.Unlock();

	Cube->SetPlatformData(PD);
	Cube->UpdateResource();
	return Cube;
}
