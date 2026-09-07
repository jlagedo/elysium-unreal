#pragma once

#include "ElysiumEntityWorld.h"

#include "ElysiumCameraService.h"
#include "ElysiumLineService.h"
#include "Substrate/ElysiumEntityWorldShared.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"

#include "Misc/Paths.h"

// One runtime-instanced choreo scene for the current NPC line. Retail's CInstancedSceneEntity is
// not a map entity: the dialogue owner constructs it from the line's sidecar VCD, advances it on
// the conversation clock and destroys it on line replacement/close. Audio, subtitles and facial
// composition already have dialogue-owned presentation paths, so this callback consumes only the
// authored body events and leaves the other event kinds on those existing owners.
struct FElysiumDialogueLineScene final : IElysiumChoreoCallback
{
	// `TakeLetter` is the line's chosen text-column take (D4). The scene, the voice and the `.lip`
	// share one stem, so a female take performs the female body clip as well as the female mouth.
	void Begin(FElysiumEntityWorld& InWorld, const FElysiumEntityHandle& InSpeaker,
		const FString& DlgSourcePath, int32 LineId, double Now,
		TCHAR TakeLetter = FElysiumLineService::DefaultTake)
	{
		Stop();
		World = &InWorld;
		Speaker = InSpeaker;
		SourceRel = FPaths::SetExtension(
			FElysiumLineService::DialogueLineSource(DlgSourcePath, LineId, TakeLetter), TEXT("vcd"));
		Scene = ElysiumScene::Load(SourceRel);
		if (!Scene.IsValid())
		{
			// A bodiless/headless conversation has no body performance to lose. On a live body this is a
			// missing authored input, and it must not collapse silently to a plausible idle.
			const FElysiumEntity* Entity = World->Resolve(Speaker);
			if (Entity != nullptr && Entity->GetSkeletalBody() != nullptr)
			{
				UE_LOG(LogElysiumWorld, Warning,
					TEXT("dialogue %s line %d body scene '%s' did not resolve"),
					*Speaker.ToString(), LineId, *SourceRel);
			}
			Clear();
			return;
		}

		SpeechStart = 0.f;
		bHasSpeechStart = false;
		for (const FElysiumSceneEvent& Event : Scene->Events)
		{
			if (Event.bActive && Event.Type == EElysiumChoreoEvent::Speak)
			{
				SpeechStart = Event.StartTime;
				bHasSpeechStart = true;
				break;
			}
		}

		Player.Begin(Scene, /*InLatency=*/0.f, /*InMaxDuration=*/0.f);
		LastThink = Now;
		SceneTime = 0.f;
		Player.AdvanceTo(SceneTime, *this); // time-zero body events start with the voice submission
	}

	void Advance(double Now)
	{
		if (!Player.IsBound())
		{
			return;
		}
		// CInstancedSceneEntity advances by curtime - lastthink and permanently discards anything over
		// 100 ms. This is deliberately different from a map scene's absolute wall-time clock.
		const float Delta = static_cast<float>(FMath::Clamp(Now - LastThink, 0.0, 0.1));
		LastThink = Now;
		SceneTime += Delta;
		Player.AdvanceTo(SceneTime, *this);
		if (Player.IsFinished())
		{
			Stop();
		}
	}

	void Stop()
	{
		if (Player.IsBound())
		{
			Player.StopActiveEvents(*this);
		}
		Clear();
	}

	bool HasActiveBodyClip() const { return !ActiveClipEvents.IsEmpty(); }

	bool GetSpeechSeconds(float& OutSeconds) const
	{
		if (!Player.IsBound() || !bHasSpeechStart)
		{
			return false;
		}
		OutSeconds = SceneTime - SpeechStart;
		return true;
	}

	virtual void StartEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event,
		float InSceneTime) override
	{
		if (Event.Type != EElysiumChoreoEvent::Sequence
			&& Event.Type != EElysiumChoreoEvent::Gesture)
		{
			return;
		}
		if (Event.Param.IsEmpty())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' has an empty %s clip"),
				*Speaker.ToString(), *SourceRel, ElysiumScene::EventTypeName(Event.Type));
			return;
		}
		FElysiumEntity* Entity = ResolveSpeaker();
		if (Entity == nullptr)
		{
			LogMissingSpeaker();
			return;
		}
		if (!Entity->PlayAnimClip(Event.Param, /*bLoop=*/false))
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' clip '%s' did not resolve"),
				*Speaker.ToString(), *SourceRel, *Event.Param);
			return;
		}
		const int32 Index = EventIndex(Event);
		if (Index != INDEX_NONE)
		{
			ActiveClipEvents.Add(Index);
		}
		SeekClip(Event, InSceneTime);
	}

	virtual void ProcessEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event,
		float InSceneTime) override
	{
		if (Event.Type == EElysiumChoreoEvent::Sequence
			|| Event.Type == EElysiumChoreoEvent::Gesture)
		{
			SeekClip(Event, InSceneTime);
		}
	}

	virtual void EndEvent(const FElysiumSceneData&, const FElysiumSceneEvent& Event, float) override
	{
		if (Event.Type != EElysiumChoreoEvent::Sequence
			&& Event.Type != EElysiumChoreoEvent::Gesture)
		{
			return;
		}
		if (!ActiveClipEvents.Remove(EventIndex(Event)))
		{
			return;
		}
		if (ActiveClipEvents.IsEmpty())
		{
			if (FElysiumEntity* Entity = ResolveSpeaker())
			{
				Entity->StopCinematicClip();
			}
		}
	}

private:
	void Clear()
	{
		Player = FElysiumScenePlayer();
		Scene.Reset();
		ActiveClipEvents.Reset();
		World = nullptr;
		Speaker = FElysiumEntityHandle::Invalid();
		SourceRel.Reset();
		LastThink = 0.0;
		SceneTime = 0.f;
		SpeechStart = 0.f;
		bHasSpeechStart = false;
		bLoggedMissingSpeaker = false;
		bLoggedSeekFailure = false;
	}

	FElysiumEntity* ResolveSpeaker() const
	{
		return World != nullptr ? World->Resolve(Speaker) : nullptr;
	}

	int32 EventIndex(const FElysiumSceneEvent& Event) const
	{
		if (!Scene.IsValid() || Scene->Events.IsEmpty())
		{
			return INDEX_NONE;
		}
		const FElysiumSceneEvent* First = Scene->Events.GetData();
		const ptrdiff_t Delta = &Event - First;
		return Delta >= 0 && Delta < Scene->Events.Num() ? static_cast<int32>(Delta) : INDEX_NONE;
	}

	void SeekClip(const FElysiumSceneEvent& Event, float InSceneTime)
	{
		if (!ActiveClipEvents.Contains(EventIndex(Event)))
		{
			return;
		}
		FElysiumEntity* Entity = ResolveSpeaker();
		if (Entity == nullptr)
		{
			LogMissingSpeaker();
			return;
		}
		if (!Entity->SeekCinematicClip(FMath::Max(0.f, InSceneTime - Event.StartTime))
			&& !bLoggedSeekFailure)
		{
			bLoggedSeekFailure = true;
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' could not seek clip '%s'"),
				*Speaker.ToString(), *SourceRel, *Event.Param);
		}
	}

	void LogMissingSpeaker()
	{
		if (!bLoggedMissingSpeaker)
		{
			bLoggedMissingSpeaker = true;
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s body scene '%s' lost its speaker"),
				*Speaker.ToString(), *SourceRel);
		}
	}

	FElysiumEntityWorld* World = nullptr;
	FElysiumEntityHandle Speaker;
	TSharedPtr<const FElysiumSceneData> Scene;
	FElysiumScenePlayer Player;
	TSet<int32> ActiveClipEvents;
	FString SourceRel;
	double LastThink = 0.0;
	float SceneTime = 0.f;
	float SpeechStart = 0.f;
	bool bHasSpeechStart = false;
	bool bLoggedMissingSpeaker = false;
	bool bLoggedSeekFailure = false;
};

enum class EElysiumDialogueDirectorSource : uint8
{
	None,
	SourceShot,
	AuthoredProfile,
	PlayerView,
};

// The one open conversation record. Everything with conversation lifetime lives here: branch
// cursor, opener provenance, one camera handle and its selected-director state. No member is
// serialized because CanSave refuses the whole session category.
struct FElysiumDialogueSession
{
	FElysiumEntityHandle Owner;
	FElysiumEntityHandle Listener;
	TSharedPtr<FElysiumDlgConversation> Conversation;
	// `FElysiumEntityWorld::GetOpenDialogSerial()` — this session's monotonic id, stamped by
	// `OpenDialog`. Identity for anything held across a call that can replace the session: the
	// opening line's col-4, a flushed NPC col-5, a UI frame.
	uint32 Serial = 0;
	EElysiumDialogOpenerKind Opener = EElysiumDialogOpenerKind::Remote;
	int32 RawFlags = 0;
	int32 DecodedFlags = 0;
	FString DefaultCamera;
	FString NormalizedCamera;
	FElysiumCameraHandle CameraHandle;
	FElysiumCameraRequest CameraRequest;
	EElysiumDialogueDirectorSource DirectorSource = EElysiumDialogueDirectorSource::None;
	EElysiumDialogueShotProfile SelectedProfile = EElysiumDialogueShotProfile::Fallback;
	FString CandidateRejections;
	FString FallbackReason;
	int32 CurrentLineId = INDEX_NONE;
	uint32 TurnRevision = 0;
	// The `_col_<C>` text-column take the current line resolved to (D4). Published so a diagnostic
	// can name which take is playing; `FElysiumLineService::EllipsesTake` means the shared
	// wordless take.
	TCHAR CurrentTakeLetter = FElysiumLineService::DefaultTake;
	FElysiumVoiceHandle CurrentVoice;
	// Retail's forced-visible-response rule, not a port fallback: `process_pc_line` (`0x100e8520`)
	// only lets an Auto-End become automatic when `LookupSpeechFile` finds audio for the NPC line;
	// with no audio it clears the slot's flags and forces one visible response with value -1. This
	// is that response — the manual Continue the presentation exposes for an automatic turn whose
	// voice could not be started.
	bool bForcedVisibleResponse = false;
	double SelectedAt = 0.0;
	float MinimumHoldSeconds = 0.0f;
	float ScreenSide = 1.0f;
	FElysiumBodyOwnerToken BodyOwner;
	FElysiumDialogueLineScene LineScene;
};
