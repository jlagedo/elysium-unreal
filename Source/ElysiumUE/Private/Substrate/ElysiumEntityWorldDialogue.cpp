#include "ElysiumEntityWorld.h"

#include "ElysiumCameraService.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumDlg.h"
#include "ElysiumLineService.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumDialogueSession.h"
#include "Substrate/ElysiumEntityWorldShared.h"
#include "Substrate/ElysiumLipTrack.h"
#include "Visual/ElysiumExpressionPreparation.h"

#include "Algo/Rotate.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

// --- Open dialogue ---

FElysiumDlgConversation* FElysiumEntityWorld::GetOpenDialog() const
{
	return DialogueSession ? DialogueSession->Conversation.Get() : nullptr;
}

FElysiumEntityHandle FElysiumEntityWorld::GetOpenDialogOwner() const
{
	return DialogueSession ? DialogueSession->Owner : FElysiumEntityHandle::Invalid();
}

bool FElysiumEntityWorld::HasActiveDialogueBodyClip(const FElysiumEntityHandle& Speaker) const
{
	return DialogueSession && DialogueSession->Owner == Speaker
		&& DialogueSession->LineScene.HasActiveBodyClip();
}

bool FElysiumEntityWorld::CanPlayerAdvanceAutomatic() const
{
	return DialogueSession && DialogueSession->bAutomaticFallback
		&& DialogueSession->Conversation.IsValid()
		&& DialogueSession->Conversation->IsAwaitingAutomatic();
}

void FElysiumEntityWorld::BeginDialogueTurn()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	FElysiumDlgConversation& Conversation = *DialogueSession->Conversation;
	const FElysiumDlgLine* Line = Conversation.CurrentNpcLine();
	if (!Line)
	{
		if (!Conversation.IsOver())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s has no current NPC line for revision %u"),
				*DescribeHandle(DialogueSession->Owner), Conversation.Revision());
		}
		return;
	}

	DialogueSession->CurrentLineId = Line->Id;
	DialogueSession->TurnRevision = Conversation.Revision();
	DialogueSession->CurrentVoice = FElysiumVoiceHandle::Invalid();
	DialogueSession->bAutomaticFallback = false;

	// The spoken line is the presented turn. Any pending Auto-Link/Auto-End remains attached to it
	// until this exact voice handle completes; it is never submitted as a subtitle or response.
	SelectDialogueCamera(/*bLineBoundary*/ true);
	DialogueSession->LineScene.Begin(*this, DialogueSession->Owner,
		Conversation.File().SourcePath, Line->Id, NowSeconds());
	if (LineService)
	{
		FElysiumEntity* Speaker = Resolve(DialogueSession->Owner);
		DialogueSession->CurrentVoice = LineService->PlayDialogueTurn(DialogueSession->Owner,
			Conversation.File().SourcePath, Line->Id,
			Speaker ? Speaker->Origin : FVector::ZeroVector,
			Speaker ? Speaker->GetSkeletalBody() : nullptr);
		if (DialogueSession->CurrentVoice.IsValid())
		{
			BeginDialogueLipsync(Conversation.File().SourcePath, Line->Id);
		}
	}

	if (Conversation.IsAwaitingAutomatic() && !DialogueSession->CurrentVoice.IsValid())
	{
		// A null audio service is a supported headless configuration. In a playable world, an invalid
		// submission is an unexpected failure and must be diagnosable. Either way the line stays on
		// screen and presentation exposes Continue, so "Alright." cannot disappear in a zero-time hop.
		DialogueSession->bAutomaticFallback = true;
		if (Audio())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s line %d could not start voice for pending automatic row %d; awaiting manual advance"),
				*DescribeHandle(DialogueSession->Owner), Line->Id,
				Conversation.PendingAutomatic() ? Conversation.PendingAutomatic()->Id : INDEX_NONE);
		}
	}
}

void FElysiumEntityWorld::UpdateDialogueAutomatic()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid()
		|| !DialogueSession->Conversation->IsAwaitingAutomatic()
		|| DialogueSession->bAutomaticFallback)
	{
		return;
	}
	TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
	const FElysiumDlgLine* Line = Conversation->CurrentNpcLine();
	if (!Line || Line->Id != DialogueSession->CurrentLineId
		|| Conversation->Revision() != DialogueSession->TurnRevision)
	{
		UE_LOG(LogElysiumWorld, Warning,
			TEXT("dialogue %s automatic join lost its turn identity (line=%d revision=%u)"),
			*DescribeHandle(DialogueSession->Owner), DialogueSession->CurrentLineId,
			DialogueSession->TurnRevision);
		DialogueSession->bAutomaticFallback = true;
		return;
	}
	if (!DialogueSession->CurrentVoice.IsValid() || !Audio())
	{
		DialogueSession->bAutomaticFallback = true;
		return;
	}
	if (Audio()->IsVoicePlaying(DialogueSession->CurrentVoice))
	{
		return;
	}

	Conversation->ResolveAutomatic();
	if (!DialogueSession || DialogueSession->Conversation != Conversation)
	{
		// The automatic row's action may synchronously close or replace dialogue. Its old voice
		// completion owns no state in the resulting session.
		return;
	}
	if (Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
		return;
	}
	BeginDialogueTurn();
}

void FElysiumEntityWorld::OpenDialog(const FElysiumEntityHandle& NewOwner,
	TSharedRef<FElysiumDlgConversation> Conversation, EElysiumDialogOpenerKind Opener,
	int32 RawFlags, const FString& DefaultCamera, const FElysiumBodyOwnerToken& SuppliedBodyOwner)
{
	if (ActiveUse.IsSet())
	{
		EndActiveUse(EElysiumUseEndReason::Cancelled);
	}
	FElysiumEntity* OwnerEntity = Resolve(NewOwner);
	FElysiumBodyOwnerToken BodyOwner = SuppliedBodyOwner;
	// A different NPC can acquire before the old session is displaced, making replacement atomic:
	// refusal leaves the old conversation untouched. The same NPC must release its existing token
	// first; that path is already live and can only fail after owner loss, when retaining the old
	// conversation would be invalid too.
	const bool bSameOwnerReplacement = DialogueSession && DialogueSession->Owner == NewOwner;
	if (!BodyOwner.IsSet() && OwnerEntity && !bSameOwnerReplacement)
	{
		BodyOwner = OwnerEntity->BeginDialogueBodySession();
	}
	if (!OwnerEntity || (!BodyOwner.IsSet() && !bSameOwnerReplacement))
	{
		UE_LOG(LogElysiumWorld, Warning, TEXT("dialogue body acquisition refused for %s"),
			*DescribeHandle(NewOwner));
		return;
	}
	// Any accepted second acquisition replaces the running session silently. Release its camera
	// before selecting the new request so one conversation never owns two handles.
	if (DialogueSession)
	{
		EndDialogSession(/*bSilent*/ true);
	}
	if (bSameOwnerReplacement)
	{
		OwnerEntity = Resolve(NewOwner);
		BodyOwner = OwnerEntity ? OwnerEntity->BeginDialogueBodySession()
			: FElysiumBodyOwnerToken();
		if (!BodyOwner.IsSet())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("same-owner dialogue replacement lost body ownership for %s"),
				*DescribeHandle(NewOwner));
			return;
		}
	}
	DialogueSession = MakeUnique<FElysiumDialogueSession>();
	DialogueSession->Owner = NewOwner;
	DialogueSession->Listener = PlayerHandle();
	DialogueSession->Conversation = Conversation;
	DialogueSession->Opener = Opener;
	DialogueSession->RawFlags = RawFlags;
	DialogueSession->DefaultCamera = DefaultCamera;
	DialogueSession->BodyOwner = BodyOwner;
	DialogueSession->NormalizedCamera = ElysiumCameraShots::NormalizeKey(DefaultCamera);
	DialogueSession->ScreenSide = (NewOwner.Index & 1) == 0 ? 1.0f : -1.0f;

	// Camera acquisition precedes the presentation announcement. A headless/null-camera world still
	// runs exactly the same dialogue and event order.
	BeginDialogueTurn();

	if (IElysiumPresenter* P = Presenter())
	{
		P->OpenDialog(NewOwner, *Conversation);
	}

	// A conversation that opened already closed (no content NPC line) ends at once, so the beat still
	// advances (OnDialogEnd -> DialogPostProcess) rather than hanging on an empty panel.
	if (Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
	}
}

void FElysiumEntityWorld::PlayerDialogChoose(int32 VisibleIndex)
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
	const uint32 BeforeRevision = Conversation->Revision();
	Conversation->Choose(VisibleIndex);
	if (Conversation->Revision() == BeforeRevision)
	{
		// Invalid/stale input, including a response submitted after an automatic wait became active,
		// must not restart the current voice and postpone its completion edge.
		return;
	}
	if (!DialogueSession || DialogueSession->Conversation != Conversation)
	{
		return; // the chosen row's action synchronously closed/replaced the session
	}
	if (!Conversation->IsOver())
	{
		BeginDialogueTurn();
	}
	if (Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
	}
}

void FElysiumEntityWorld::PlayerDialogAdvance()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	if (DialogueSession->Conversation->IsAwaitingAutomatic())
	{
		if (!DialogueSession->bAutomaticFallback)
		{
			return;
		}
		TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
		Conversation->ResolveAutomatic();
		if (!DialogueSession || DialogueSession->Conversation != Conversation)
		{
			return;
		}
		if (Conversation->IsOver())
		{
			EndDialogSession(/*bSilent*/ false);
		}
		else
		{
			BeginDialogueTurn();
		}
		return;
	}
	DialogueSession->Conversation->AdvanceTerminal();
	if (DialogueSession->Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
	}
}

void FElysiumEntityWorld::CloseDialog(bool bSilent)
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	DialogueSession->Conversation->Close();
	EndDialogSession(bSilent);
}

void FElysiumEntityWorld::SelectDialogueCamera(bool bLineBoundary)
{
	if (!DialogueSession)
	{
		return;
	}
	IElysiumCameraService* Service = Camera();
	if (!Service)
	{
		DialogueSession->FallbackReason = TEXT("camera service unavailable");
		return;
	}
	FElysiumEntity* Speaker = Resolve(DialogueSession->Owner);
	FElysiumPlayer* Listener = FindPlayer();
	if (!Speaker || Speaker->IsInert() || !Listener)
	{
		DialogueSession->FallbackReason = TEXT("dialogue target unavailable");
		return;
	}

	auto Publish = [this, Service](FElysiumCameraRequest Request,
		EElysiumDialogueDirectorSource Source, EElysiumDialogueShotProfile Profile,
		float MinimumHold)
	{
		DialogueSession->CameraRequest = MoveTemp(Request);
		DialogueSession->DirectorSource = Source;
		DialogueSession->SelectedProfile = Profile;
		DialogueSession->MinimumHoldSeconds = MinimumHold;
		DialogueSession->SelectedAt = NowSeconds();
		if (Service->IsCameraLive(DialogueSession->CameraHandle))
		{
			Service->UpdateCamera(DialogueSession->CameraHandle, DialogueSession->CameraRequest);
		}
		else
		{
			DialogueSession->CameraHandle = Service->AcquireCamera(DialogueSession->CameraRequest);
		}
	};

	const FVector SpeakerEye = Speaker->EyePosition();
	const FVector ListenerEye = Listener->EyePosition();
	DialogueSession->CandidateRejections.Reset();

	if (Service->DialogueCamerasEnabled() && !DialogueSession->NormalizedCamera.IsEmpty())
	{
		const FElysiumCameraShotDef* Def = ElysiumCameraShots::Load(DialogueSession->NormalizedCamera);
		FElysiumCameraShot Shot;
		if (Def && Def->IsValid()
			&& FElysiumCameraDirector::Resolve(this, *Def, DialogueSession->Owner, Shot))
		{
			FElysiumCameraRequest Request;
			Request.Kind = EElysiumCameraRequestKind::Dialogue;
			Request.Owner = DescribeHandle(DialogueSession->Owner);
			Request.DebugName = FString::Printf(TEXT("Dialogue:%s"), *Def->Name);
			Request.Priority = 500;
			Request.bOverridePose = true;
			Request.Shot = Shot;
			Request.BlendInSeconds = Shot.BlendSeconds;
			Request.BlendOutSeconds = Shot.BlendSeconds;
			Request.Control = EElysiumCameraControlPolicy::Preserve;
			Request.bShowHud = Def->Constraints.bShowHud;
			Request.bDrawViewmodel = Def->Constraints.bDrawViewmodel;
			Request.bDialogPOV = Def->Constraints.bDialogPOV;
			Request.bRequireSubtitleSafe = false; // source shot authors its own subject framing
			Request.Fallback = EElysiumCameraFallback::SourceShot;
			Request.SourceShot = DialogueSession->NormalizedCamera;
			Request.SelectedProfile = TEXT("source-shot");
			Request.SpeakerAnchor.Position = SpeakerEye;
			Request.SpeakerAnchor.Name = TEXT("speaker-eye");
			Request.SpeakerAnchor.bValid = true;
			Request.ListenerAnchor.Position = ListenerEye;
			Request.ListenerAnchor.Name = TEXT("listener-eye");
			Request.ListenerAnchor.bValid = true;
			FString Reject;
			if (Service->EvaluateDialogueCandidate(Request, Reject))
			{
				Publish(MoveTemp(Request), EElysiumDialogueDirectorSource::SourceShot,
					EElysiumDialogueShotProfile::Fallback, 2.0f);
				return;
			}
			DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: %s"),
				*DialogueSession->NormalizedCamera, *Reject);
		}
		else
		{
			DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: missing/unresolved"),
				*DialogueSession->NormalizedCamera);
		}
	}

	if (Service->DialogueCamerasEnabled())
	{
		// Hold a selected grammar shot until its boundary minimum expires. Its anchors still track each
		// frame through UpdateSelectedDialogueCamera; only candidate selection is held.
		if (bLineBoundary
			&& DialogueSession->DirectorSource == EElysiumDialogueDirectorSource::AuthoredProfile
			&& NowSeconds() - DialogueSession->SelectedAt < DialogueSession->MinimumHoldSeconds)
		{
			UpdateSelectedDialogueCamera();
			return;
		}

		FElysiumDialogueCameraContext Context;
		Context.SpeakerEye = SpeakerEye;
		Context.ListenerEye = ListenerEye;
		Context.ScreenSide = DialogueSession->ScreenSide;
		Context.bAllowCloseUp = false; // only explicit line/profile metadata may enable it
		Context.Owner = DescribeHandle(DialogueSession->Owner);
		TArray<FElysiumDialogueCameraProfile> Profiles;
		Service->GetDialogueProfiles(Profiles);
		if (DialogueSession->DirectorSource != EElysiumDialogueDirectorSource::None && Profiles.Num() > 1)
		{
			const int32 Rotate = FMath::Abs(DialogueSession->CurrentLineId) % (Profiles.Num() - 1);
			Algo::Rotate(Profiles, Rotate);
		}
		for (const FElysiumDialogueCameraProfile& Profile : Profiles)
		{
			if (Profile.Kind == EElysiumDialogueShotProfile::Fallback
				|| (Profile.bAllowCloseUp && !Context.bAllowCloseUp))
			{
				continue;
			}
			FElysiumCameraRequest Request = ElysiumDialogueCamera::BuildRequest(Profile, Context);
			if (DialogueSession->CameraRequest.bOverridePose
				&& !ElysiumDialogueCamera::PreservesScreenSide(DialogueSession->CameraRequest, Request))
			{
				DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: screen-side; "),
					*Profile.Name.ToString());
				continue;
			}
			FString Reject;
			if (Service->EvaluateDialogueCandidate(Request, Reject))
			{
				Publish(MoveTemp(Request), EElysiumDialogueDirectorSource::AuthoredProfile,
					Profile.Kind, Profile.MinimumHoldSeconds);
				return;
			}
			DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: %s; "),
				*Profile.Name.ToString(), *Reject);
		}
	}

	FElysiumDialogueCameraProfile Fallback;
	Fallback.Name = TEXT("PlayerViewFallback");
	Fallback.Kind = EElysiumDialogueShotProfile::Fallback;
	FElysiumDialogueCameraContext Context;
	Context.SpeakerEye = SpeakerEye;
	Context.ListenerEye = ListenerEye;
	Context.Owner = DescribeHandle(DialogueSession->Owner);
	FElysiumCameraRequest Request = ElysiumDialogueCamera::BuildRequest(Fallback, Context);
	Request.FallbackReason = Service->DialogueCamerasEnabled()
		? (DialogueSession->CandidateRejections.IsEmpty() ? TEXT("no camera candidate")
			: DialogueSession->CandidateRejections)
		: TEXT("dialogue cameras disabled by user");
	DialogueSession->FallbackReason = Request.FallbackReason;
	Publish(MoveTemp(Request), EElysiumDialogueDirectorSource::PlayerView,
		EElysiumDialogueShotProfile::Fallback, 0.0f);
}

void FElysiumEntityWorld::UpdateSelectedDialogueCamera()
{
	if (!DialogueSession || !Camera()
		|| !Camera()->IsCameraLive(DialogueSession->CameraHandle))
	{
		return;
	}
	FElysiumEntity* Speaker = Resolve(DialogueSession->Owner);
	FElysiumPlayer* Listener = FindPlayer();
	if (!Speaker || Speaker->IsInert() || !Listener)
	{
		EndDialogSession(/*bSilent*/ true);
		return;
	}

	if (DialogueSession->DirectorSource == EElysiumDialogueDirectorSource::SourceShot)
	{
		const FElysiumCameraShotDef* Def = ElysiumCameraShots::Load(DialogueSession->NormalizedCamera);
		FElysiumCameraShot Shot;
		if (!Def || !FElysiumCameraDirector::Resolve(this, *Def, DialogueSession->Owner, Shot))
		{
			EndDialogSession(/*bSilent*/ true);
			return;
		}
		DialogueSession->CameraRequest.Shot = Shot;
	}
	else if (DialogueSession->DirectorSource == EElysiumDialogueDirectorSource::AuthoredProfile)
	{
		FElysiumDialogueCameraContext Context;
		Context.SpeakerEye = Speaker->EyePosition();
		Context.ListenerEye = Listener->EyePosition();
		Context.ScreenSide = DialogueSession->ScreenSide;
		Context.Owner = DescribeHandle(DialogueSession->Owner);
		TArray<FElysiumDialogueCameraProfile> Profiles;
		Camera()->GetDialogueProfiles(Profiles);
		for (const FElysiumDialogueCameraProfile& Profile : Profiles)
		{
			if (Profile.Kind == DialogueSession->SelectedProfile)
			{
				FElysiumCameraRequest Updated = ElysiumDialogueCamera::BuildRequest(Profile, Context);
				Updated.FallbackReason = DialogueSession->CameraRequest.FallbackReason;
				DialogueSession->CameraRequest = MoveTemp(Updated);
				break;
			}
		}
	}
	Camera()->UpdateCamera(DialogueSession->CameraHandle, DialogueSession->CameraRequest);
}

void FElysiumEntityWorld::RefreshDialogueCamera()
{
	if (!DialogueSession)
	{
		return;
	}
	if (!Resolve(DialogueSession->Owner) || !FindPlayer())
	{
		EndDialogSession(/*bSilent*/ true);
		return;
	}
	UpdateSelectedDialogueCamera();
}

bool FElysiumEntityWorld::GetDialogueCameraGaze(FVector& OutPoint) const
{
	if (!DialogueSession || !DialogueSession->CameraRequest.bDialogPOV
		|| !DialogueSession->CameraRequest.bOverridePose)
	{
		return false;
	}
	OutPoint = DialogueSession->CameraRequest.Shot.Origin;
	return true;
}

bool FElysiumEntityWorld::DialogueCameraHidesHud() const
{
	return DialogueSession && !DialogueSession->CameraRequest.bShowHud;
}

void FElysiumEntityWorld::GetDialogueDebugState(
	TArray<TPair<FString, FString>>& Out) const
{
	if (!DialogueSession)
	{
		Out.Emplace(TEXT("Dialogue"), TEXT("closed"));
		return;
	}
	Out.Emplace(TEXT("Opener"), ElysiumDialogueCamera::LexToString(DialogueSession->Opener));
	Out.Emplace(TEXT("Raw flags"), FString::FromInt(DialogueSession->RawFlags));
	Out.Emplace(TEXT("Decoded flags"), FString::FromInt(DialogueSession->DecodedFlags));
	Out.Emplace(TEXT("default_camera"), DialogueSession->DefaultCamera);
	Out.Emplace(TEXT("Camera handle"), FString::Printf(TEXT("slot=%d gen=%u epoch=%llu"),
		DialogueSession->CameraHandle.Slot, DialogueSession->CameraHandle.Generation,
		DialogueSession->CameraHandle.Epoch));
	Out.Emplace(TEXT("Source shot"), DialogueSession->CameraRequest.SourceShot);
	Out.Emplace(TEXT("Profile"), DialogueSession->CameraRequest.SelectedProfile);
	Out.Emplace(TEXT("Fallback"), DialogueSession->FallbackReason);
	Out.Emplace(TEXT("Candidate rejects"), DialogueSession->CandidateRejections);
	Out.Emplace(TEXT("Body owner"), FString::Printf(TEXT("%s gen=%u"),
		LexToString(DialogueSession->BodyOwner.Owner), DialogueSession->BodyOwner.Generation));
	if (const FElysiumEntity* Speaker = Resolve(DialogueSession->Owner))
	{
		const float RenderedYaw = ElysiumSkeletalBasis::FromSourceAngles(Speaker->Angles).Yaw;
		const FVector ToSpeaker = Speaker->EyePosition() - DialogueSession->CameraRequest.Shot.Origin;
		const FVector SpeakerForward = FRotator(0.0f, RenderedYaw, 0.0f).Vector();
		const float Error = ToSpeaker.IsNearlyZero() ? 0.0f
			: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct((-ToSpeaker).GetSafeNormal2D(), SpeakerForward), -1.0f, 1.0f)));
		Out.Emplace(TEXT("Speaker yaw"), FString::Printf(TEXT("source %.2f / rendered %.2f"),
			Speaker->Angles.Y, RenderedYaw));
		Out.Emplace(TEXT("Camera-to-speaker"), ToSpeaker.ToCompactString());
		Out.Emplace(TEXT("Forward angular error"), FString::Printf(TEXT("%.2f deg"), Error));
		Out.Emplace(TEXT("Gaze target"), DialogueSession->CameraRequest.bDialogPOV
			? DialogueSession->CameraRequest.Shot.Origin.ToCompactString() : TEXT("listener"));
	}
}

FString FElysiumEntityWorld::ScriptedSessionSaveBlockReason() const
{
	if (DialogueSession)
	{
		return TEXT("a conversation is open");
	}
	if (HasTrackCamera())
	{
		return TEXT("an authored camera track is active");
	}
	if (HasScriptedCamera())
	{
		return TEXT("an authored legacy camera is active");
	}
	for (const TUniquePtr<FElysiumEntity>& Entity : EntityList)
	{
		if (Entity && !Entity->IsInert())
		{
			if (const TCHAR* Reason = Entity->SaveBlockReason())
			{
				return Reason;
			}
		}
	}
	return FString();
}

// --- The dialogue half of lipsync ---
//
// The same join as a map scene's — the line's `.lip`, the speaker's
// `expressions/<stem>_phonemes` table, and the model's phoneme filter. A dialogue line now owns its
// instanced VCD timeline too, so the face reads that scene's authored speak offset and remains in
// lockstep with its body events. The submission timestamp remains the diagnostic fallback for a
// line whose VCD is absent.
static TAutoConsoleVariable<int32> CVarDialogueLipsync(
	TEXT("elysium.DialogueLipsync"),
	1,
	TEXT("A .dlg conversation turn drives the speaking NPC's mouth from the line's .lip phoneme track (1, default) or leaves it at rest (0)."),
	ECVF_Default);

void FElysiumEntityWorld::BeginDialogueLipsync(const FString& DlgSourcePath, int32 LineId)
{
	DialogueLipsync.Reset();
	DialogueLineStart = -1.0;
	DialogueFaceOwner = GetOpenDialogOwner();
	FElysiumEntity* Speaker = Resolve(GetOpenDialogOwner());
	if (FElysiumCombatCharacter* Character = Speaker ? Speaker->AsCombatCharacter() : nullptr)
	{
		Character->SetDispositionTalking(true);
	}
	if (CVarDialogueLipsync.GetValueOnGameThread() == 0)
	{
		return;
	}
	if (Speaker == nullptr)
	{
		return;
	}

	FElysiumLipSyncBinding Binding;
	// The audio path this turn resolves to, with the extension swapped — the one place the two
	// halves of the join have to agree, so it goes through the line service's own rule.
	Binding.Track = ElysiumLip::Load(FElysiumLineService::DialogueLineSource(DlgSourcePath, LineId));
	FString ExpressionDiagnostic;
	Binding.Table = ElysiumExpressions::LoadPreparedPhonemes(*Speaker, ExpressionDiagnostic);
	if (!ExpressionDiagnostic.IsEmpty())
	{
		// BeginDialogueLipsync runs once per submitted line, never in the per-frame evaluator.
		UE_LOG(LogElysiumWorld, Warning, TEXT("dialogue phoneme selection for %s: %s"),
			*Speaker->DebugString(), *ExpressionDiagnostic);
	}
	// This speaker's own blend width, same read the cutscene driver makes. A body with no rig keeps
	// the binding's modal default.
	Speaker->GetPhonemeFilter(Binding.BlendMin, Binding.BlendMax);
	if (!Binding.IsValid())
	{
		return;
	}
	DialogueLineStart = NowSeconds();
	DialogueLipsync = MakeShared<FElysiumLipSyncBinding>(MoveTemp(Binding));
}

void FElysiumEntityWorld::RefreshDialogueLipsync(double Now)
{
	if (!DialogueSession && !DialogueLipsync.IsValid() && DialogueFacialPose.IsEmpty())
	{
		return;
	}

	TMap<FString, float> Next;
	FElysiumEntity* Speaker = Resolve(DialogueFaceOwner.IsSet()
		? DialogueFaceOwner : GetOpenDialogOwner());
	FElysiumCombatCharacter* Character = Speaker ? Speaker->AsCombatCharacter() : nullptr;
	if (Character)
	{
		Character->AccumulateDispositionFacialPose(Next);
	}
	if (DialogueLipsync.IsValid() && DialogueLineStart >= 0.0
		&& CVarDialogueLipsync.GetValueOnGameThread() != 0)
	{
		float LineSeconds = static_cast<float>(Now - DialogueLineStart);
		if (DialogueSession)
		{
			DialogueSession->LineScene.GetSpeechSeconds(LineSeconds);
		}
		if (LineSeconds >= 0.f && LineSeconds <= DialogueLipsync->Track->LatestTime)
		{
			DialogueLipsync->Accumulate(LineSeconds, Next, nullptr);
		}
		else if (LineSeconds > DialogueLipsync->Track->LatestTime && Character)
		{
			Character->SetDispositionTalking(false);
			Next.Reset();
			Character->AccumulateDispositionFacialPose(Next);
		}
	}

	if (Speaker == nullptr)
	{
		// The face went away mid-line. Drop the bookkeeping rather than holding a pose for an entity
		// that no longer exists.
		DialogueFacialPose.Reset();
		DialogueFaceOwner = FElysiumEntityHandle();
		return;
	}

	// Same compose-diff-push as FElysiumChoreoScene::RefreshFacialPose, for one face.
	TArray<FElysiumFlexWrite> Writes;
	bool bChanged = DialogueFacialPose.Num() != Next.Num();
	for (const TPair<FString, float>& Key : Next)
	{
		const float* Was = DialogueFacialPose.Find(Key.Key);
		bChanged |= Was == nullptr || *Was != Key.Value;
		Writes.Add({ Key.Key, Key.Value });
	}
	for (const TPair<FString, float>& Key : DialogueFacialPose)
	{
		if (!Next.Contains(Key.Key))
		{
			bChanged = true;
			Writes.Add({ Key.Key, 0.f });
		}
	}
	if (bChanged && !Writes.IsEmpty())
	{
		Speaker->SetFlexControllers(Writes, nullptr);
	}
	DialogueFacialPose = MoveTemp(Next);
	if (!DialogueSession && !DialogueLipsync.IsValid())
	{
		// The default disposition pose now lives on the character. Forget only this compositor's
		// previous-pose bookkeeping; clearing its keys would erase the baseline we just restored.
		DialogueFacialPose.Reset();
		DialogueFaceOwner = FElysiumEntityHandle();
	}
}

void FElysiumEntityWorld::EndDialogSession(bool bSilent)
{
	const FElysiumEntityHandle Closing = GetOpenDialogOwner();
	FElysiumBodyOwnerToken ClosingBodyOwner;
	if (DialogueSession)
	{
		ClosingBodyOwner = DialogueSession->BodyOwner;
		DialogueSession->LineScene.Stop();
	}
	if (LineService)
	{
		LineService->CancelDialogue(Closing);
	}
	// Drop the phoneme track but NOT the pose: the next RefreshDialogueLipsync writes every key this
	// turn was driving back to zero, exactly as a choreo scene's release pass does. Clearing the pose
	// here instead would leave the last phoneme latched on the face for the rest of the map.
	DialogueLipsync.Reset();
	DialogueLineStart = -1.0;
	if (FElysiumEntity* Speaker = Resolve(Closing))
	{
		if (FElysiumCombatCharacter* Character = Speaker->AsCombatCharacter())
		{
			Character->SetDispositionTalking(false);
		}
	}

	if (DialogueSession && Camera())
	{
		Camera()->ReleaseCamera(DialogueSession->CameraHandle);
	}
	if (FElysiumEntity* OwnerEntity = Resolve(Closing))
	{
		OwnerEntity->EndDialogueBodySession(ClosingBodyOwner, bSilent);
	}
	DialogueSession.Reset();

	if (IElysiumPresenter* P = Presenter())
	{
		P->CloseDialog();
	}

	if (!bSilent && Closing.IsSet())
	{
		// Route EndDialog to exactly the owning NPC (its InputEndDialog clears bInDialog and fires
		// OnDialogEnd -> DialogPostProcess). Queued through chokepoint 2 like every other input, with
		// the owner as `!self` so no name lookup can hit a same-named entity.
		static const FName EndDialogInput(TEXT("EndDialog"));
		EnqueueInput(ElysiumEntityWorldShared::GSelfTarget, EndDialogInput, FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle(), Closing);
	}
}
