#pragma once

#include "CoreMinimal.h"
#include "ElysiumCameraService.h"
#include "Engine/DataAsset.h"

#include "ElysiumDialogueCamera.generated.h"

UENUM(BlueprintType)
enum class EElysiumDialogOpenerKind : uint8
{
	Forced,
	Remote,
	Unforced,
};

UENUM(BlueprintType)
enum class EElysiumDialogueShotProfile : uint8
{
	TwoShot,
	SingleSpeaker,
	SingleListener,
	OverShoulderSpeaker,
	OverShoulderListener,
	CloseSpeaker,
	Fallback,
};

USTRUCT(BlueprintType)
struct FElysiumDialogueCameraProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FName Name;
	UPROPERTY(EditAnywhere) EElysiumDialogueShotProfile Kind = EElysiumDialogueShotProfile::TwoShot;
	UPROPERTY(EditAnywhere) float DistanceCm = 220.0f;
	UPROPERTY(EditAnywhere) float LateralCm = 75.0f;
	UPROPERTY(EditAnywhere) float HeightCm = 12.0f;
	UPROPERTY(EditAnywhere) float FieldOfView = 50.0f;
	UPROPERTY(EditAnywhere) float MinimumHoldSeconds = 2.0f;
	UPROPERTY(EditAnywhere) bool bAllowCloseUp = false;
	UPROPERTY(EditAnywhere) bool bEstablishing = false;
};

UCLASS(BlueprintType)
class UElysiumDialogueCameraSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FElysiumDialogueCameraProfile> Profiles;
};

struct FElysiumDialogueCameraContext
{
	FVector SpeakerEye = FVector::ZeroVector;
	FVector ListenerEye = FVector::ZeroVector;
	// +1 or -1. A conversation chooses once and keeps it so cuts do not cross the line.
	float ScreenSide = 1.0f;
	bool bAllowCloseUp = false;
	FString Owner;
};

namespace ElysiumDialogueCamera
{
	const TCHAR* LexToString(EElysiumDialogOpenerKind Kind);
	const TCHAR* LexToString(EElysiumDialogueShotProfile Profile);
	TArray<FElysiumDialogueCameraProfile> DefaultProfiles();
	FElysiumCameraRequest BuildRequest(const FElysiumDialogueCameraProfile& Profile,
		const FElysiumDialogueCameraContext& Context);
	bool PreservesScreenSide(const FElysiumCameraRequest& A,
		const FElysiumCameraRequest& B);
}
