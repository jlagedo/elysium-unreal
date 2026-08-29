#pragma once

#include "CommonInputBaseTypes.h"

#include "ElysiumInputGlyphControllerData.generated.h"

class UTexture2D;

// CommonInput controller data backed by soft references to generated Kenney textures.
UCLASS(Abstract)
class UElysiumInputGlyphControllerData : public UCommonInputBaseControllerData
{
	GENERATED_BODY()

public:
	virtual bool TryGetInputBrush(FSlateBrush& OutBrush, const FKey& Key) const override;
	virtual bool TryGetInputBrush(FSlateBrush& OutBrush, const TArray<FKey>& Keys) const override;

	// Construction helpers used by the concrete native data CDOs below.
	void AddGlyph(const FKey& Key, const TCHAR* TexturePath);
	void AddHardwareId(const TCHAR* InputDeviceName, const TCHAR* HardwareDeviceIdentifier);

private:
	UPROPERTY()
	TMap<FName, TSoftObjectPtr<UTexture2D>> GlyphTextures;

	mutable TSet<FName> FailedGlyphLoads;
};

UCLASS()
class UElysiumKeyboardControllerData final : public UElysiumInputGlyphControllerData
{
	GENERATED_BODY()

public:
	UElysiumKeyboardControllerData();
};

UCLASS()
class UElysiumXboxControllerData final : public UElysiumInputGlyphControllerData
{
	GENERATED_BODY()

public:
	UElysiumXboxControllerData();
};

UCLASS()
class UElysiumDualSenseControllerData final : public UElysiumInputGlyphControllerData
{
	GENERATED_BODY()

public:
	UElysiumDualSenseControllerData();
};
