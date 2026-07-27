#include "Visual/ElysiumTextureCache.h"

#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	// Minimal DDS reader for the pipeline's retex_dds.py output: DXT1/3/5 fourCC with an
	// optional mip chain. Returns nullptr for anything else (PNG fallback covers it).
	// Loading the original game's DXT blocks directly means no decode cost, ~4-8x less
	// GPU memory than BGRA8, real mips, and bit-identical texels.
	UTexture2D* LoadDDS(const TArray<uint8>& File, bool bSRGB)
	{
		if (File.Num() < 128 + 8 || FMemory::Memcmp(File.GetData(), "DDS ", 4) != 0)
		{
			return nullptr;
		}
		const uint8* D = File.GetData();
		auto U32 = [D](int32 Off) { uint32 V; FMemory::Memcpy(&V, D + Off, 4); return V; };

		const int32 Height = int32(U32(12));
		const int32 Width = int32(U32(16));
		const int32 MipCount = FMath::Max(1, int32(U32(28)));

		EPixelFormat Format;
		int32 BlockBytes;
		const uint32 FourCC = U32(84);
		switch (FourCC)
		{
		case 0x31545844: Format = PF_DXT1; BlockBytes = 8; break;    // 'DXT1'
		case 0x33545844: Format = PF_DXT3; BlockBytes = 16; break;   // 'DXT3'
		case 0x35545844: Format = PF_DXT5; BlockBytes = 16; break;   // 'DXT5'
		default: return nullptr;
		}

		UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, Format);
		if (!Tex)
		{
			return nullptr;
		}
		Tex->SRGB = bSRGB;
		Tex->NeverStream = true;

		FTexturePlatformData* PD = Tex->GetPlatformData();
		int64 Off = 128;
		for (int32 Mip = 0; Mip < MipCount; ++Mip)
		{
			const int32 MW = FMath::Max(1, Width >> Mip);
			const int32 MH = FMath::Max(1, Height >> Mip);
			const int64 Size = int64(FMath::DivideAndRoundUp(MW, 4)) * FMath::DivideAndRoundUp(MH, 4) * BlockBytes;
			if (Off + Size > File.Num())
			{
				break;   // truncated chain: keep the mips read so far
			}

			if (Mip >= PD->Mips.Num())
			{
				FTexture2DMipMap* NewMip = new FTexture2DMipMap(MW, MH, 1);
				PD->Mips.Add(NewMip);
				NewMip->BulkData.Lock(LOCK_READ_WRITE);
				NewMip->BulkData.Realloc(Size);
				NewMip->BulkData.Unlock();
			}
			FTexture2DMipMap& MipData = PD->Mips[Mip];
			void* Dest = MipData.BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Dest, D + Off, Size);
			MipData.BulkData.Unlock();
			Off += Size;
		}

		Tex->UpdateResource();
		return Tex;
	}

	UTexture2D* MakeTexture(int32 Width, int32 Height, const TArray64<uint8>& Bgra, bool bSRGB = true)
	{
		if (Width <= 0 || Height <= 0 || Bgra.Num() < int64(Width) * Height * 4)
		{
			return nullptr;
		}

		UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (!Tex)
		{
			return nullptr;
		}
		Tex->SRGB = bSRGB;
		Tex->NeverStream = true;

		FTexturePlatformData* PlatformData = Tex->GetPlatformData();
		void* Dest = PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
		FMemory::Memcpy(Dest, Bgra.GetData(), int64(Width) * Height * 4);
		PlatformData->Mips[0].BulkData.Unlock();
		Tex->UpdateResource();
		return Tex;
	}
}

UTexture2D* FElysiumTextureCache::LoadTex(const FString& Dir, const FString& Rel, bool bSRGB)
{
	const FString Path = FPaths::Combine(Dir, Rel);
	// The sRGB flag is part of the key: a normal map and an albedo could in principle share a
	// path but must not share a cached texture (linear vs gamma-decoded).
	const FString Key = bSRGB ? Path : Path + TEXT("#lin");
	if (const TStrongObjectPtr<UTexture2D>* Found = TexCache.Find(Key))
	{
		return Found->Get();
	}

	auto CacheAndReturn = [this, &Key](UTexture2D* Tex) -> UTexture2D*
	{
		TexCache.Add(Key, TStrongObjectPtr<UTexture2D>(Tex));
		return Tex;
	};

	// Prefer a .dds sibling (original DXT blocks + mips, no decode); fall back to PNG.
	// The probe is speculative — many textures (e.g. the _ke self-illum maps) ship as PNG
	// only — so read it FILEREAD_Silent to keep an expected miss out of the log.
	const FString DdsPath = FPaths::ChangeExtension(Path, TEXT("dds"));
	TArray<uint8> FileData;
	if (FFileHelper::LoadFileToArray(FileData, *DdsPath, FILEREAD_Silent))
	{
		if (UTexture2D* Dds = LoadDDS(FileData, bSRGB))
		{
			return CacheAndReturn(Dds);
		}
		FileData.Reset();
	}

	if (!FFileHelper::LoadFileToArray(FileData, *Path))
	{
		return CacheAndReturn(nullptr);
	}

	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return CacheAndReturn(nullptr);
	}

	TArray64<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return CacheAndReturn(nullptr);
	}

	return CacheAndReturn(MakeTexture(Wrapper->GetWidth(), Wrapper->GetHeight(), Raw, bSRGB));
}

UTexture2D* FElysiumTextureCache::SolidTex(const FLinearColor& Color)
{
	const FString Key = Color.ToString();
	if (const TStrongObjectPtr<UTexture2D>* Found = SolidCache.Find(Key))
	{
		return Found->Get();
	}

	const FColor C = Color.ToFColor(true);
	TArray64<uint8> Bgra;
	Bgra.Append({ C.B, C.G, C.R, C.A });   // PF_B8G8R8A8 byte order
	UTexture2D* Tex = MakeTexture(1, 1, Bgra);
	SolidCache.Add(Key, TStrongObjectPtr<UTexture2D>(Tex));
	return Tex;
}
