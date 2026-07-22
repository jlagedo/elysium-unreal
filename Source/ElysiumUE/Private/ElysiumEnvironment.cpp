#include "ElysiumEnvironment.h"

#include "Engine/Texture2D.h"
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
// .cube colour-grading LUT
// ---------------------------------------------------------------------------

UTexture2D* ElysiumEnvironment::BuildColorGradeLUT(const FString& CubePath)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *CubePath))
	{
		return nullptr;
	}

	int32 SrcSize = 0;
	TArray<FVector3f> Vals;
	Vals.Reserve(33 * 33 * 33);
	for (const FString& Raw : Lines)
	{
		const FString Line = Raw.TrimStartAndEnd();
		if (Line.IsEmpty() || Line[0] == TEXT('#'))
		{
			continue;
		}
		if (Line.StartsWith(TEXT("LUT_3D_SIZE")))
		{
			TArray<FString> Tok;
			Line.ParseIntoArray(Tok, TEXT(" "), true);
			SrcSize = FCString::Atoi(*Tok.Last());
			continue;
		}
		TArray<FString> Tok;
		Line.ParseIntoArray(Tok, TEXT(" "), true);
		if (Tok.Num() != 3 || !Tok[0].IsNumeric())
		{
			continue;   // TITLE / DOMAIN_* / other headers
		}
		Vals.Emplace(FCString::Atof(*Tok[0]), FCString::Atof(*Tok[1]), FCString::Atof(*Tok[2]));
	}

	if (SrcSize < 2 || Vals.Num() != SrcSize * SrcSize * SrcSize)
	{
		UE_LOG(LogElysiumEnv, Warning, TEXT("bad .cube LUT (size=%d, %d values): %s"), SrcSize, Vals.Num(), *CubePath);
		return nullptr;
	}

	// .cube data is red-fastest: index(r,g,b) = (b*size + g)*size + r.
	auto SrcAt = [&Vals, SrcSize](int32 R, int32 G, int32 B) -> FVector3f
	{
		return Vals[(B * SrcSize + G) * SrcSize + R];
	};
	// Trilinear sample at normalised (u,v,w) in [0,1].
	auto Sample = [&SrcAt, SrcSize](float U, float V, float W) -> FVector3f
	{
		const float Fr = U * (SrcSize - 1);
		const float Fg = V * (SrcSize - 1);
		const float Fb = W * (SrcSize - 1);
		const int32 R0 = FMath::Clamp((int32)Fr, 0, SrcSize - 1), R1 = FMath::Min(R0 + 1, SrcSize - 1);
		const int32 G0 = FMath::Clamp((int32)Fg, 0, SrcSize - 1), G1 = FMath::Min(G0 + 1, SrcSize - 1);
		const int32 B0 = FMath::Clamp((int32)Fb, 0, SrcSize - 1), B1 = FMath::Min(B0 + 1, SrcSize - 1);
		const float Dr = Fr - R0, Dg = Fg - G0, Db = Fb - B0;
		auto Lerp = [](const FVector3f& A, const FVector3f& B, float T) { return A + (B - A) * T; };
		const FVector3f C00 = Lerp(SrcAt(R0, G0, B0), SrcAt(R1, G0, B0), Dr);
		const FVector3f C10 = Lerp(SrcAt(R0, G1, B0), SrcAt(R1, G1, B0), Dr);
		const FVector3f C01 = Lerp(SrcAt(R0, G0, B1), SrcAt(R1, G0, B1), Dr);
		const FVector3f C11 = Lerp(SrcAt(R0, G1, B1), SrcAt(R1, G1, B1), Dr);
		return Lerp(Lerp(C00, C10, Dg), Lerp(C01, C11, Dg), Db);
	};

	// Unreal's neutral colour-grading LUT is 16^3 unwrapped to 256x16: 16 tiles across,
	// tile index = blue, and within a tile x = red, y = green.
	constexpr int32 N = 16;
	constexpr int32 W = N * N;   // 256
	constexpr int32 H = N;       // 16
	TArray64<uint8> Bgra;
	Bgra.SetNumZeroed(int64(W) * H * 4);
	for (int32 B = 0; B < N; ++B)
	{
		for (int32 G = 0; G < N; ++G)
		{
			for (int32 R = 0; R < N; ++R)
			{
				const FVector3f C = Sample(R / float(N - 1), G / float(N - 1), B / float(N - 1));
				const int32 X = B * N + R;
				const int32 Y = G;
				const int64 Idx = (int64(Y) * W + X) * 4;
				Bgra[Idx + 0] = (uint8)FMath::RoundToInt(FMath::Clamp(C.Z, 0.f, 1.f) * 255.f);   // B
				Bgra[Idx + 1] = (uint8)FMath::RoundToInt(FMath::Clamp(C.Y, 0.f, 1.f) * 255.f);   // G
				Bgra[Idx + 2] = (uint8)FMath::RoundToInt(FMath::Clamp(C.X, 0.f, 1.f) * 255.f);   // R
				Bgra[Idx + 3] = 255;
			}
		}
	}

	UTexture2D* Lut = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Lut)
	{
		return nullptr;
	}
	// A LUT is data, not colour: sample the stored values verbatim (no sRGB decode), and
	// never stream/mip it.
	Lut->SRGB = false;
	Lut->NeverStream = true;
	Lut->Filter = TF_Bilinear;
	FTexturePlatformData* PD = Lut->GetPlatformData();
	void* Dest = PD->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Bgra.GetData(), Bgra.Num());
	PD->Mips[0].BulkData.Unlock();
	Lut->UpdateResource();
	return Lut;
}

// ---------------------------------------------------------------------------
// Sky faces -> IBL cubemap
// ---------------------------------------------------------------------------

UTextureCube* ElysiumEnvironment::BuildSkyCube(const FString& TexDir)
{
	// Unreal cube face order +X,-X,+Y,-Y,+Z,-Z. Under our Source->Unreal transform
	// (sx,-sy,sz), the Source sky faces map: +X=ft, -X=bk, +Y=rt, -Y=lf, +Z=up, -Z=dn.
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
