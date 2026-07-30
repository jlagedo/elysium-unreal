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
};
