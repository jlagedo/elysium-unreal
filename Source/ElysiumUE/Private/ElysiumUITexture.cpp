#include "ElysiumUITexture.h"

#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

UTexture2D* ElysiumUI::LoadPngTexture(const FString& PngPath)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *PngPath))
	{
		return nullptr;
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()) ||
		!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return nullptr;
	}
	const int32 W = Wrapper->GetWidth();
	const int32 H = Wrapper->GetHeight();
	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->NeverStream = true;
	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	void* Dest = PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Raw.GetData(), int64(W) * H * 4);
	PlatformData->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}
