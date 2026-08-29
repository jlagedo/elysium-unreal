#include "ElysiumEnvironment.h"

#include "Engine/TextureCube.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEnv, Log, All);

// .env sidecar.

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
		else if (Key == TEXT("skyfog") && Tok.Num() >= 2)
		{
			Out.bSkyFog = Tok[1] == TEXT("1");
		}
		else if (Key == TEXT("skyfogcolor") && Tok.Num() >= 4)
		{
			Out.SkyFogColor = FLinearColor(FCString::Atof(*Tok[1]), FCString::Atof(*Tok[2]),
				FCString::Atof(*Tok[3]));
		}
		else if (Key == TEXT("skyfogstart") && Tok.Num() >= 2)
		{
			Out.SkyFogStartCm = FCString::Atof(*Tok[1]);
		}
		else if (Key == TEXT("skyfogend") && Tok.Num() >= 2)
		{
			Out.SkyFogEndCm = FCString::Atof(*Tok[1]);
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

bool FElysiumSkyDef::Parse(const FString& SkyPath, FElysiumSkyDef& Out)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *SkyPath))
	{
		return false;   // no miniature on this map
	}

	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() >= 4 && Tok[0] == TEXT("origin"))
		{
			Out.OriginCm = FVector(FCString::Atod(*Tok[1]), FCString::Atod(*Tok[2]),
				FCString::Atod(*Tok[3]));
		}
		else if (Tok.Num() >= 2 && Tok[0] == TEXT("scale"))
		{
			Out.Scale = FCString::Atof(*Tok[1]);
		}
	}
	// A zero or negative scale would collapse the miniature onto its own origin.
	Out.bValid = Out.Scale > 0.f;
	if (!Out.bValid)
	{
		Out.Scale = 1.f;
	}
	return Out.bValid;
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
	// K1 x K2 transform (docs/vtmb/sky-ambience.md -> "K2 ... What it makes our binding"). Both halves
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

	// The cube's solid-angle-weighted mean linear radiance over the UPPER hemisphere.
	//
	// Three things have to be right for this number to mean anything. The texels are sRGB, so
	// they are decoded to linear before averaging — averaging gamma-encoded values would
	// overstate a dark sky badly. A cube texel's solid angle is not uniform: it falls off as
	// (1 + u^2 + v^2)^-3/2 toward the face corners, so each sample is weighted by that. And
	// only the upper hemisphere counts, because the SkyLight runs with
	// bLowerHemisphereIsBlack — VtMB's ground faces are uniform near-black plates and a sky
	// that lit the world's undersides would defeat the occlusion the cubemap is there for.
	//
	// Reads the assembled slices, so it needs no knowledge of which face went where — the
	// direction comes from GetCubemapVector, the same table the GPU samples with.
	float UpperHemisphereMean(const uint8* Slices, int32 N)
	{
		const float Inv = 1.f / float(N);
		double Sum = 0.0, Weight = 0.0;
		for (int32 Slice = 0; Slice < 6; ++Slice)
		{
			const uint8* Face = Slices + int64(Slice) * N * N * 4;
			for (int32 Y = 0; Y < N; ++Y)
			{
				const float V = 2.f * ((Y + 0.5f) * Inv) - 1.f;
				for (int32 X = 0; X < N; ++X)
				{
					const float U = 2.f * ((X + 0.5f) * Inv) - 1.f;
					// GetCubemapVector's third component is world up (its own
					// "no sky lighting from below the horizon" test reads it).
					float Up;
					switch (Slice)
					{
					case 0: Up = -U;  break;   // +X: ( 1, -V, -U)
					case 1: Up =  U;  break;   // -X: (-1, -V,  U)
					case 2: Up =  V;  break;   // +Y: ( U,  1,  V)
					case 3: Up = -V;  break;   // -Y: ( U, -1, -V)
					case 4: Up =  1;  break;   // +Z
					default: Up = -1; break;   // -Z
					}
					if (Up <= 0.f)
					{
						continue;
					}
					const float W = FMath::Pow(1.f + U * U + V * V, -1.5f);
					const uint8* Px = Face + (int64(Y) * N + X) * 4;   // BGRA
					// Rec.709 luminance of the sRGB-decoded texel.
					const float Lum =
						0.2126f * FMath::Pow(Px[2] / 255.f, 2.2f) +
						0.7152f * FMath::Pow(Px[1] / 255.f, 2.2f) +
						0.0722f * FMath::Pow(Px[0] / 255.f, 2.2f);
					Sum += double(Lum) * W;
					Weight += W;
				}
			}
		}
		return Weight > 0.0 ? float(Sum / Weight) : 0.f;
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

// Sky faces -> IBL cubemap.

UTextureCube* ElysiumEnvironment::BuildSkyCube(const FString& TexDir)
{
	return BuildSkyCubeFrom(TexDir, TEXT("sky_"));
}

UTextureCube* ElysiumEnvironment::BuildConstantCube(const FLinearColor& Colour, int32 Size)
{
	Size = FMath::Clamp(Size, 1, 128);
	UTextureCube* Cube = NewObject<UTextureCube>(GetTransientPackage(), NAME_None, RF_Transient);
	Cube->SRGB = true;
	Cube->NeverStream = true;

	FTexturePlatformData* PD = new FTexturePlatformData();
	PD->SizeX = Size;
	PD->SizeY = Size;
	PD->PixelFormat = PF_B8G8R8A8;
	PD->SetIsCubemap(true);
	PD->SetNumSlices(6);

	const int64 TotalBytes = int64(Size) * Size * 4 * 6;
	FTexture2DMipMap* Mip = new FTexture2DMipMap(Size, Size, 1);
	PD->Mips.Add(Mip);
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	uint8* Dest = (uint8*)Mip->BulkData.Realloc(TotalBytes);
	// The cube is sRGB, same as the sky faces, so the colour is encoded rather than written linear.
	const FColor Encoded = Colour.ToFColor(true);
	for (int64 Texel = 0; Texel < TotalBytes; Texel += 4)
	{
		Dest[Texel + 0] = Encoded.B;
		Dest[Texel + 1] = Encoded.G;
		Dest[Texel + 2] = Encoded.R;
		Dest[Texel + 3] = 255;
	}
	Mip->BulkData.Unlock();

	Cube->SetPlatformData(PD);
	Cube->UpdateResource();
	return Cube;
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

UTextureCube* ElysiumEnvironment::BuildSkyCubeFrom(const FString& Dir, const FString& Prefix,
	float* OutUpperMean)
{
	if (OutUpperMean)
	{
		*OutUpperMean = 0.f;
	}
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
	// Measured while the bulk data is still mapped — after Unlock the pointer is not ours.
	if (OutUpperMean)
	{
		*OutUpperMean = UpperHemisphereMean(Dest, Size);
	}
	Mip->BulkData.Unlock();

	Cube->SetPlatformData(PD);
	Cube->UpdateResource();
	return Cube;
}
