#include "ElysiumUserSettings.h"

void UElysiumUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	MasterVolume = MusicVolume = DialogueVolume = AmbienceVolume = SfxVolume = UiVolume = 1.f;
	bDialogueCamerasEnabled = true;
}

float UElysiumUserSettings::AudioVolume(EElysiumAudioCategory Category) const
{
	switch (Category)
	{
	case EElysiumAudioCategory::Music:    return MusicVolume;
	case EElysiumAudioCategory::Dialogue: return DialogueVolume;
	case EElysiumAudioCategory::Ambience: return AmbienceVolume;
	case EElysiumAudioCategory::Sfx:      return SfxVolume;
	case EElysiumAudioCategory::Ui:       return UiVolume;
	default:                              return 1.f;
	}
}

void UElysiumUserSettings::SetAudioVolume(EElysiumAudioCategory Category, float Value)
{
	const float Clamped = FMath::Clamp(Value, 0.f, 1.f);
	switch (Category)
	{
	case EElysiumAudioCategory::Music:    MusicVolume = Clamped; break;
	case EElysiumAudioCategory::Dialogue: DialogueVolume = Clamped; break;
	case EElysiumAudioCategory::Ambience: AmbienceVolume = Clamped; break;
	case EElysiumAudioCategory::Sfx:      SfxVolume = Clamped; break;
	case EElysiumAudioCategory::Ui:       UiVolume = Clamped; break;
	default:                              MasterVolume = Clamped; break;
	}
	SaveSettings();
}
