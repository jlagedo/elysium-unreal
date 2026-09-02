#include "UI/ElysiumUITexture.h"

#include "Engine/Texture2D.h"

UTexture2D* ElysiumUI::MakeAlphaRamp(const TArray<uint8>& Alpha, int32 Width, int32 Height)
{
	if (Width <= 0 || Height <= 0 || Alpha.Num() != Width * Height)
	{
		return nullptr;
	}
	UTexture2D* Tex = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	// SRGB off: the payload is an alpha ramp, and the flat white RGB under it is there only so the
	// brush tint survives the multiply. Clamped, because a veil sampled at its edge must not wrap
	// its opaque end round to its transparent one.
	Tex->SRGB = false;
	Tex->NeverStream = true;
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;

	FTexturePlatformData* PlatformData = Tex->GetPlatformData();
	uint8* Dest = static_cast<uint8*>(PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 i = 0; i < Alpha.Num(); ++i)
	{
		Dest[i * 4 + 0] = 255;   // B
		Dest[i * 4 + 1] = 255;   // G
		Dest[i * 4 + 2] = 255;   // R
		Dest[i * 4 + 3] = Alpha[i];
	}
	PlatformData->Mips[0].BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}
