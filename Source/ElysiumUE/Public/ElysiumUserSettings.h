#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "ElysiumAudioSubsystem.h"
#include "ElysiumUserSettings.generated.h"

UCLASS(Config=GameUserSettings)
class UElysiumUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	virtual void SetToDefaults() override;

	float AudioVolume(EElysiumAudioCategory Category) const;
	void SetAudioVolume(EElysiumAudioCategory Category, float Value);

	UPROPERTY(Config) float MasterVolume = 1.f;
	UPROPERTY(Config) float MusicVolume = 1.f;
	UPROPERTY(Config) float DialogueVolume = 1.f;
	UPROPERTY(Config) float AmbienceVolume = 1.f;
	UPROPERTY(Config) float SfxVolume = 1.f;
	UPROPERTY(Config) float UiVolume = 1.f;
	// Player-view fallback is always available; this preference changes camera presentation only.
	UPROPERTY(Config) bool bDialogueCamerasEnabled = true;

	// **`camera_prefs` and `camera_weaponswitch`, persisted here rather than to `config.cfg`.**
	// Retail archives both to the user's own config; this build must not write into the VtMB install
	// (repo-root `CLAUDE.md`, Bring-your-own-game), so the live value stays the console store's and
	// the durable copy lives here. *Elysium divergence, owner-called* — the storage moves, the
	// per-weapon-class stickiness does not.
	//
	// `bCameraPrefsStored` is what makes a first run inherit the user's retail history: until the
	// player toggles once, the value the install's `config.cfg` carried wins.
	UPROPERTY(Config) int32 CameraPrefs = 6;
	UPROPERTY(Config) bool bCameraWeaponSwitch = true;
	UPROPERTY(Config) bool bCameraPrefsStored = false;
};
