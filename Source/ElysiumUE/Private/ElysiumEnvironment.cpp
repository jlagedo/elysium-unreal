#include "ElysiumEnvironment.h"

#include "Engine/TextureCube.h"
#include "HAL/PlatformFileManager.h"
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

	// The rotation a decoded Source face carries on its way into an Unreal cube slice. Named for
	// what happens to the *image content*, so `CCW90` is numpy's rot90.
	enum class ESkyRot : uint8 { None, CCW90, CW90, Half };

	struct FSkySlice
	{
		const TCHAR* Face;   // the Source face suffix, as `tex/sky_<face>.png`
		ESkyRot Rot;
		const TCHAR* Axis;   // the Unreal world axis this slice looks down (for logging)
	};

	// Unreal's cube slice order is +X, -X, +Y, -Y, +Z, -Z, and this table is the whole of the
	// K1 x K2 transform (docs/sky-ambience.md -> "K2 ... What it makes our binding"). Both halves
	// are recovered, neither is a guess:
	//
	//   K1 - VtMB's own draw tables (engine.dll R_DrawSkyBox/MakeSkyVec) bind each face to a
	//        Source axis; carried through source_to_unreal (x, -y, z) a face image pixel (u, v)
	//        looks along, with s = 2u - 1 and t = 1 - 2v:
	//            rt: ( 1,  s,  t)   lf: (-1, -s,  t)   bk: ( s, -1,  t)
	//            ft: (-s,  1,  t)   up: (-t,  s,  1)   dn: ( t,  s, -1)
	//        That gives the face->slice column: rt, lf, ft, bk, up, dn.
	//
	//   K2 - a slice is the plain D3D face table applied to the *raw* Unreal world vector
	//        (GetCubemapVector; TextureCubeSample is a bare Sample, nothing swizzles anywhere).
	//        That table was specified for a Y-up world and Unreal is Z-up, so four of the six
	//        slices store their face rotated against an upright view along their own axis.
	//
	// Solving one against the other per slice yields exactly one (face, rotation) pair each,
	// which is the table below. A renamed array alone would still draw wrong: the rotations are
	// not cosmetic, they are what Unreal's layout requires.
	const FSkySlice SkySlices[6] = {
		{ TEXT("rt"), ESkyRot::CCW90, TEXT("+X") },
		{ TEXT("lf"), ESkyRot::CW90,  TEXT("-X") },
		{ TEXT("ft"), ESkyRot::Half,  TEXT("+Y") },
		{ TEXT("bk"), ESkyRot::None,  TEXT("-Y") },
		{ TEXT("up"), ESkyRot::CCW90, TEXT("+Z") },
		{ TEXT("dn"), ESkyRot::CCW90, TEXT("-Z") },
	};

	// Where destination texel (X, Y) of an N x N image reads from under a rotation of the content.
	// Both images are row-major top-down, so a rotation is an index remap and nothing else:
	//   CCW90  dst[y][x] = src[x][N-1-y]
	//   CW90   dst[y][x] = src[N-1-x][y]
	//   Half   dst[y][x] = src[N-1-y][N-1-x]
	void RotSource(ESkyRot Rot, int32 X, int32 Y, int32 N, int32& SX, int32& SY)
	{
		switch (Rot)
		{
		case ESkyRot::CCW90: SX = N - 1 - Y; SY = X;         break;
		case ESkyRot::CW90:  SX = Y;         SY = N - 1 - X; break;
		case ESkyRot::Half:  SX = N - 1 - X; SY = N - 1 - Y; break;
		default:             SX = X;         SY = Y;         break;
		}
	}

	// Copy one N x N BGRA8 face into a slice, rotating the content.
	void BlitRotated(const uint8* Src, uint8* Dst, int32 N, ESkyRot Rot)
	{
		const uint32* SrcPx = reinterpret_cast<const uint32*>(Src);
		uint32* DstPx = reinterpret_cast<uint32*>(Dst);
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				int32 SX, SY;
				RotSource(Rot, X, Y, N, SX, SY);
				DstPx[int64(Y) * N + X] = SrcPx[int64(SY) * N + SX];
			}
		}
	}
}

const TCHAR* ElysiumEnvironment::SkySliceFace(int32 Slice)
{
	return SkySlices[FMath::Clamp(Slice, 0, 5)].Face;
}

void ElysiumEnvironment::SkySliceSource(int32 Slice, int32 X, int32 Y, int32 N,
	int32& OutSrcX, int32& OutSrcY)
{
	RotSource(SkySlices[FMath::Clamp(Slice, 0, 5)].Rot, X, Y, N, OutSrcX, OutSrcY);
}

// ---------------------------------------------------------------------------
// Sky faces -> IBL cubemap
// ---------------------------------------------------------------------------

UTextureCube* ElysiumEnvironment::BuildSkyCube(const FString& TexDir)
{
	return BuildSkyCubeFrom(TexDir, TEXT("sky_"));
}

bool ElysiumEnvironment::HasSkyFaces(const FString& Dir, const FString& Prefix)
{
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	for (int32 F = 0; F < 6; ++F)
	{
		const FString Path = Dir / FString::Printf(TEXT("%s%s.png"), *Prefix, SkySlices[F].Face);
		if (!Files.FileExists(*Path))
		{
			return false;
		}
	}
	return true;
}

UTextureCube* ElysiumEnvironment::BuildSkyCubeFrom(const FString& Dir, const FString& Prefix)
{
	// One decoded face per Unreal slice, in slice order, each already assigned its rotation.
	TArray64<uint8> Faces[6];
	int32 Size = 0;
	for (int32 F = 0; F < 6; ++F)
	{
		const FString Path = Dir / FString::Printf(TEXT("%s%s.png"), *Prefix, SkySlices[F].Face);
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
		BlitRotated(Faces[F].GetData(), Dest + FaceBytes * F, Size, SkySlices[F].Rot);
	}
	Mip->BulkData.Unlock();

	Cube->SetPlatformData(PD);
	Cube->UpdateResource();
	return Cube;
}
