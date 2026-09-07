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
	// `CBasePlayer::PlayerUse` (`0x10167850`) — the player walked up and pressed +use. It reaches
	// the same player vtable slot 414 (`FUN_10178280`) the three scripted inputs do, so it is an
	// opener kind rather than a separate path. Appended last: the enum is UENUM-reflected and the
	// three above are the retail input names.
	Use,
};

// Which lens the `DialogPOV` gaze redirect aims at.
//
// Retail (`CAI_BaseNPC::MaintainAutonomousEyeDirection`, `vampire.dll` 0x1026B810) reads the
// player's ACTIVE camera entity (`GetActiveCameraEntity` 0x1017CF90, off `player+0x19B4` /
// `+0x1EC4`), asks its current shot for the flags dword at `+0x20` (`FUN_1006EDB0`, shot-table
// stride 0x104; the `DialogPOV` key sets bit 0x10, parser 0x100721E0), and on a set bit aims at
// that camera entity's own position — the lens — regardless of HOW the shot was selected. So the
// flag is a property of the shot in effect, and the aim is wherever the eye actually is.
//
// The port's director can answer with a pose of its own (a resolved source shot or an authored
// profile) or leave the player's view standing. The second case still has a lens — the player
// camera — but only the embodiment can locate it, so the substrate names the case and hands back
// the player entity's eye point as the headless stand-in.
enum class EElysiumDialogueGazeLens : uint8
{
	None,        // no conversation, or the shot in effect does not set DialogPOV
	ShotOrigin,  // the dialogue camera holds its own pose; the lens is that point
	PlayerView,  // the request left the player's own view up; the lens IS the player camera
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
