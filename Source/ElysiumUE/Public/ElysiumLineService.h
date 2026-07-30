#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"
#include "ElysiumEntityHandle.h"

class IElysiumAudio;
class USceneComponent;

// Map-owned story/audio join. It knows dialogue and scene ownership, while the GI subsystem alone
// resolves media, decodes it, schedules it and owns the rendering component.
class FElysiumLineService
{
public:
	explicit FElysiumLineService(IElysiumAudio* InAudio) : Audio(InAudio) {}

	FElysiumVoiceHandle PlayDirect(const FString& SessionId, const FString& AuthoredPath,
		const FVector& Origin, USceneComponent* AttachTo = nullptr,
		EElysiumAudioCategory Category = EElysiumAudioCategory::Auto,
		float StartOffsetSeconds = 0.f);

	FElysiumVoiceHandle PlayDialogueTurn(const FElysiumEntityHandle& Owner,
		const FString& DlgSourcePath, int32 LineId, const FVector& Origin,
		USceneComponent* AttachTo = nullptr);

	void CancelSession(const FString& SessionId, float FadeSeconds = 0.f);
	void CancelDialogue(const FElysiumEntityHandle& Owner, float FadeSeconds = 0.f);
	void Shutdown();

	static FString DialogueLineSource(const FString& DlgSourcePath, int32 LineId);

private:
	IElysiumAudio* Audio = nullptr;
	TMap<FString, FElysiumVoiceHandle> Active;
};
