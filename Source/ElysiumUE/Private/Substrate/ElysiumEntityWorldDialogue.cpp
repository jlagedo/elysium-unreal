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

uint32 FElysiumEntityWorld::GetOpenDialogSerial() const
{
	return DialogueSession ? DialogueSession->Serial : 0u;
}

bool FElysiumEntityWorld::HasActiveDialogueBodyClip(const FElysiumEntityHandle& Speaker) const
{
	return DialogueSession && DialogueSession->Owner == Speaker
		&& DialogueSession->LineScene.HasActiveBodyClip();
}

bool FElysiumEntityWorld::CanPlayerAdvanceAutomatic() const
{
	return DialogueSession && DialogueSession->bForcedVisibleResponse
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
	DialogueSession->bForcedVisibleResponse = false;

	// The spoken line is the presented turn. Any pending Auto-Link/Auto-End remains attached to it
	// until this exact voice handle completes; it is never submitted as a subtitle or response.
	SelectDialogueCamera(/*bLineBoundary*/ true);
	// D4: the text column this player actually reads chooses the take, so the voice, the body scene
	// and the phoneme track all come off one `line<id>_col_<C>` stem (`generate_speech_filename`
	// `0x100e1680` / the column chooser `0x100e15c0`). A letterless row answers the shared ellipses
	// take instead (`FUN_100df0b0`).
	const TCHAR TakeLetter = FElysiumLineService::TakeLetterFor(*Line,
		Conversation.PlayerMale(), Conversation.PlayerClanOffset());
	DialogueSession->CurrentTakeLetter = TakeLetter;
	DialogueSession->LineScene.Begin(*this, DialogueSession->Owner,
		Conversation.File().SourcePath, Line->Id, NowSeconds(), TakeLetter);
	if (LineService)
	{
		FElysiumEntity* Speaker = Resolve(DialogueSession->Owner);
		DialogueSession->CurrentVoice = LineService->PlayDialogueTurn(DialogueSession->Owner,
			Conversation.File().SourcePath, Line->Id,
			Speaker ? Speaker->Origin : FVector::ZeroVector,
			Speaker ? Speaker->GetSkeletalBody() : nullptr, TakeLetter,
			FElysiumLineService::SpeechVolumeFor(DialogueSession->Owner));
		if (DialogueSession->CurrentVoice.IsValid())
		{
			BeginDialogueLipsync(Conversation.File().SourcePath, Line->Id, TakeLetter);
		}
	}

	if (Conversation.IsAwaitingAutomatic() && !DialogueSession->CurrentVoice.IsValid())
	{
		// A null audio service is a supported headless configuration. In a playable world, an invalid
		// submission is an unexpected failure and must be diagnosable. Either way the line stays on
		// screen and presentation exposes Continue, so "Alright." cannot disappear in a zero-time hop.
		DialogueSession->bForcedVisibleResponse = true;
		if (Audio())
		{
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("dialogue %s line %d could not start voice for pending automatic row %d; awaiting manual advance"),
				*DescribeHandle(DialogueSession->Owner), Line->Id,
				Conversation.PendingAutomatic() ? Conversation.PendingAutomatic()->Id : INDEX_NONE);
		}
	}
}

bool FElysiumEntityWorld::IsDialogueNpcSpeaking() const
{
	return DialogueSession && DialogueSession->CurrentVoice.IsValid() && Audio()
		&& Audio()->IsVoicePlaying(DialogueSession->CurrentVoice);
}

void FElysiumEntityWorld::StopDialogueVoice()
{
	if (!DialogueSession)
	{
		return;
	}
	if (DialogueSession->CurrentVoice.IsValid() && Audio())
	{
		Audio()->StopVoice(DialogueSession->CurrentVoice, 0.f);
	}
	DialogueSession->CurrentVoice = FElysiumVoiceHandle::Invalid();
	// The face completes with the voice: drop the phoneme track and let the next composition pass
	// write every key this line was driving back to zero, exactly as the end of a turn does.
	DialogueLipsync.Reset();
	DialogueLineStart = -1.0;
	DialogueSession->LineScene.Stop();
}

void FElysiumEntityWorld::FlushDialogueVoiceCompletion()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid()
		|| !DialogueSession->Conversation->HasPendingNpcAction())
	{
		return;
	}
	if (IsDialogueNpcSpeaking())
	{
		return;   // still talking — `CallPendingNPCEventScript` has not been reached
	}
	// A turn whose voice never started (no audio service, no shipped take) is finished the moment it
	// is presented, which is the same boundary retail's `NPCNotifyDoneTalking` reports for a silent
	// line. Hold the conversation alive across the call: the parked col-5 may close or replace it.
	TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
	Conversation->FlushPendingNpcAction();
}

void FElysiumEntityWorld::PlayerDialogSkip()
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	// M-SKIP — retail's pick `-2` hurry verb as a skip key. The voice and the face end, the parked
	// col-5 runs, and the response band is left exactly as it was.
	StopDialogueVoice();
	TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
	Conversation->FlushPendingNpcAction();
	if (!DialogueSession || DialogueSession->Conversation != Conversation)
	{
		return;   // the flushed col-5 closed or replaced the session
	}
	if (!Conversation->IsAwaitingAutomatic())
	{
		return;
	}
	// The skip IS the done-talking edge. Retail's `NPCNotifyDoneTalking` (`0x100e4780`) flushes the
	// parked script and then takes the turn's automatic continuation (`Pick(0)` / `Release`), so an
	// Auto-Link that would have followed the voice must follow the skip too.
	//
	// Without this the skip's own `StopDialogueVoice` invalidates `CurrentVoice`, which sends
	// `UpdateDialogueAutomatic` down its `!CurrentVoice.IsValid()` arm and raises
	// `bForcedVisibleResponse` — a Continue the player must press a second time for a transition the
	// engine owns. That forced response is retail's no-audio rule (`process_pc_line` `0x100e8520`),
	// not a skip rule.
	Conversation->ResolveAutomatic();
	if (!DialogueSession || DialogueSession->Conversation != Conversation)
	{
		return;
	}
	if (Conversation->IsOver())
	{
		EndDialogSession(/*bSilent*/ false);
		return;
	}
	BeginDialogueTurn();
}

void FElysiumEntityWorld::UpdateDialogueAutomatic()
{
	// Every turn's voice completion is a flush edge, not just an automatic one.
	FlushDialogueVoiceCompletion();
	if (!DialogueSession || !DialogueSession->Conversation.IsValid()
		|| !DialogueSession->Conversation->IsAwaitingAutomatic()
		|| DialogueSession->bForcedVisibleResponse)
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
		DialogueSession->bForcedVisibleResponse = true;
		return;
	}
	if (!DialogueSession->CurrentVoice.IsValid() || !Audio())
	{
		DialogueSession->bForcedVisibleResponse = true;
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
	DialogueSession->Serial = ++NextDialogSerial;
	DialogueSession->Owner = NewOwner;
	DialogueSession->Listener = PlayerHandle();
	DialogueSession->Conversation = Conversation;
	DialogueSession->Opener = Opener;
	DialogueSession->RawFlags = RawFlags;
	DialogueSession->DefaultCamera = DefaultCamera;
	DialogueSession->BodyOwner = BodyOwner;
	DialogueSession->NormalizedCamera = ElysiumCameraShots::NormalizeKey(DefaultCamera);
	DialogueSession->ScreenSide = (NewOwner.Index & 1) == 0 ? 1.0f : -1.0f;
	const uint32 OpeningSerial = DialogueSession->Serial;

	// The opening NPC line's col-4 is an ACTION (`process_npc_line` `0x100e8100`), and retail runs
	// it from inside `CDialog::Acquire` — with the dialog object already installed as the player's
	// partner. So the session is built FIRST and the conversation is started here: a col-4 that
	// fires `EndDialog` or opens another conversation then acts on THIS session rather than on
	// whatever was open a moment ago. `Start()` is idempotent, so a caller that started the
	// conversation before handing it over is unaffected.
	Conversation->Start();
	if (!DialogueSession || DialogueSession->Serial != OpeningSerial)
	{
		// The opening col-4 ended this session or opened another one. Whatever is open now owns
		// itself; this call has nothing left to announce.
		return;
	}

	// Camera acquisition precedes the presentation announcement. A headless/null-camera world still
	// runs exactly the same dialogue and event order.
	BeginDialogueTurn();
	if (!DialogueSession || DialogueSession->Serial != OpeningSerial)
	{
		return;
	}

	if (IElysiumPresenter* P = Presenter())
	{
		P->OpenDialog(NewOwner, *Conversation);
	}

	// A conversation that opened already closed (no content NPC line) ends at once, so the beat still
	// advances (OnDialogEnd -> DialogPostProcess) rather than hanging on an empty panel.
	if (Conversation->IsOver() && DialogueSession && DialogueSession->Serial == OpeningSerial)
	{
		EndDialogSession(/*bSilent*/ false);
	}
}

void FElysiumEntityWorld::PlayerDialogChoose(int32 VisibleIndex, int32 ExpectedLineId)
{
	if (!DialogueSession || !DialogueSession->Conversation.IsValid())
	{
		return;
	}
	TSharedPtr<FElysiumDlgConversation> Conversation = DialogueSession->Conversation;
	if (ExpectedLineId != INDEX_NONE
		&& Conversation->VisibleChoiceLineId(VisibleIndex) != ExpectedLineId)
	{
		// The band moved between the frame the player read and the frame the click arrived (a
		// voice-completion flush or an NPC col-5 can re-enter the turn). Refuse rather than pick by
		// position: nothing is cut, nothing is flushed, nothing is charged.
		UE_LOG(LogElysiumWorld, Verbose,
			TEXT("dialogue %s refused a stale pick at index %d (expected line %d, band holds %d)"),
			*DescribeHandle(DialogueSession->Owner), VisibleIndex, ExpectedLineId,
			Conversation->VisibleChoiceLineId(VisibleIndex));
		return;
	}
	// M-REVEAL — retail refuses a pick while `IsTalking()` holds; the port accepts it and cuts the
	// line instead. The voice and the face end first so the parked col-5 the pick is about to flush
	// runs at the same boundary a completed voice would have given it.
	if (Conversation->IsChoiceEnabled(VisibleIndex) && !Conversation->IsAwaitingAutomatic())
	{
		StopDialogueVoice();
	}
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
		if (!DialogueSession->bForcedVisibleResponse)
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
	if (!DialogueSession)
	{
		return;
	}
	// `EndDialogSession` closes the conversation itself — retail `CDialog::Release` (`0x100e5240`)
	// flushes the parked NPC script on EVERY teardown path, not only on the explicit close — so
	// there is nothing left to do here but tear the session down.
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

	// `DialogPOV` is a property of the SHOT IN EFFECT, not of the camera path the director happened
	// to take. Retail asks the active camera entity's current shot for the flag every think
	// (`FUN_1006EDB0` -> shot-table stride 0x104, flags dword at +0x20, bit 0x10; parser
	// 0x100721E0) and redirects the dialogue arm on a set bit however that shot was selected
	// (`CAI_BaseNPC::MaintainAutonomousEyeDirection` 0x1026B810).
	//
	// The port has three camera paths where retail has one, so the flag is resolved ONCE here and
	// stamped on whichever request is published:
	//
	//   * the retail source shot — the flag is read straight off it, exactly as retail does;
	//   * an authored profile (`DA_ElysiumDialogueCameraSet`) — the profile REPLACES the shot, so it
	//     carries the flag from the NPC's own `default_camera` source shot, which is the authored
	//     intent for this conversation;
	//   * no source shot at all — MODERNIZATION (`docs/architecture/camera-architecture.md`,
	//     2026-09-07): default the flag SET, because 51 of the 66 shipped shot files set it and a
	//     conversation with no authored shot is the case retail never had. Clearing it instead would
	//     leave the majority of ported conversations aiming at the player's eye while the camera
	//     looks from somewhere else, which is the divergence that reads as a wandering gaze.
	bool bShotDialogPOV = true;
	if (!DialogueSession->NormalizedCamera.IsEmpty())
	{
		if (const FElysiumCameraShotDef* FlagDef =
			ElysiumCameraShots::Load(DialogueSession->NormalizedCamera))
		{
			if (FlagDef->IsValid())
			{
				bShotDialogPOV = FlagDef->Constraints.bDialogPOV;
			}
		}
	}

	// Why the retail shot did not win, when it did not. The owner's live read needs this: a source
	// shot silently displaced by a closer authored profile is indistinguishable, in the frame, from
	// a source shot that was simply framed wrong.
	FString SourceShotRejection;

	auto Publish = [this, Service, bShotDialogPOV, &SourceShotRejection](FElysiumCameraRequest Request,
		EElysiumDialogueDirectorSource Source, EElysiumDialogueShotProfile Profile,
		float MinimumHold)
	{
		if (!SourceShotRejection.IsEmpty() && Source != EElysiumDialogueDirectorSource::SourceShot)
		{
			UE_LOG(LogElysiumWorld, Log,
				TEXT("dialogue camera: source shot '%s' rejected (%s); replaced by %s '%s'"),
				*DialogueSession->NormalizedCamera, *SourceShotRejection,
				Source == EElysiumDialogueDirectorSource::AuthoredProfile
					? TEXT("authored profile") : TEXT("fallback"),
				*Request.SelectedProfile);
		}
		Request.bDialogPOV = bShotDialogPOV;
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
			SourceShotRejection = Reject;
			DialogueSession->CandidateRejections += FString::Printf(TEXT("%s: %s"),
				*DialogueSession->NormalizedCamera, *Reject);
		}
		else
		{
			SourceShotRejection = TEXT("missing/unresolved");
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
				// The per-frame anchor refresh rebuilds the request from the profile, which knows
				// nothing about the shot flag. Carry the selection's resolved `DialogPOV` across, or
				// the gaze redirect would survive exactly one frame after each line boundary.
				Updated.bDialogPOV = DialogueSession->CameraRequest.bDialogPOV;
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

EElysiumDialogueGazeLens FElysiumEntityWorld::GetDialogueCameraGaze(FVector& OutPoint) const
{
	// Retail's condition is the flag alone: `MaintainAutonomousEyeDirection` (0x1026B810) asks the
	// ACTIVE camera entity for its shot's flags and, on bit 0x10, aims at that entity's position.
	// There is no second test for "the camera moved" — a shot that leaves the view where it was is
	// still the shot in effect, and the camera entity is still where the eye is.
	if (!DialogueSession || !DialogueSession->CameraRequest.bDialogPOV)
	{
		return EElysiumDialogueGazeLens::None;
	}
	if (DialogueSession->CameraRequest.bOverridePose)
	{
		OutPoint = DialogueSession->CameraRequest.Shot.Origin;
		return EElysiumDialogueGazeLens::ShotOrigin;
	}
	// `bOverridePose == false` is the port's player-view fallback: the dialogue request is live and
	// scoped, but it publishes no pose, so the player's own camera is still what is rendered. That
	// camera IS the lens retail would aim at. The substrate cannot read a camera component, so it
	// names the case and answers with the player entity's eye point — which is where the view sits
	// to within the pawn's own camera offset, and is the whole answer in a headless world.
	const FElysiumPlayer* Listener = FindPlayer();
	if (!Listener)
	{
		return EElysiumDialogueGazeLens::None;
	}
	OutPoint = Listener->EyePosition();
	return EElysiumDialogueGazeLens::PlayerView;
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
	// The entity walk runs **ahead of** the cine slot (M8, SC4). Every interaction that adopts a
	// scripted camera — a terminal, a monitor, a keypad, the lockpick — now reaches the one slot
	// `SetCamera` reaches, so testing the slot first would answer "an authored legacy camera is
	// active" for every one of them and bury the session that actually owns the block. The slot's
	// own row survives below it for the case it is really for: a `SetCamera` shot with no entity
	// session behind it.
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
	if (HasScriptedCamera())
	{
		return TEXT("an authored legacy camera is active");
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

void FElysiumEntityWorld::BeginDialogueLipsync(const FString& DlgSourcePath, int32 LineId,
	TCHAR TakeLetter)
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
	Binding.Track = ElysiumLip::Load(
		FElysiumLineService::DialogueLineSource(DlgSourcePath, LineId, TakeLetter));
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
	// DETACH FIRST. `CDialog::Release` (`0x100e5240`) flushes the pending NPC event script before it
	// clears the live dialogue state, and that script is authored: it can fire `EndDialog` (landing
	// back here) or `StartPlayerDialogRemote` (landing in `OpenDialog`, which ends the open session
	// on the way in). Taking the record out of the world before running any of it is the
	// re-entrancy guard — a nested end sees no open session and returns, and a nested OPEN installs
	// a new session this teardown will not touch, because everything below reads the detached copy.
	TUniquePtr<FElysiumDialogueSession> Closed = MoveTemp(DialogueSession);
	DialogueSession.Reset();
	if (!Closed)
	{
		return;
	}
	const FElysiumEntityHandle Closing = Closed->Owner;
	const FElysiumBodyOwnerToken ClosingBodyOwner = Closed->BodyOwner;
	const uint32 ClosingSerial = Closed->Serial;
	Closed->LineScene.Stop();
	// The flush retail owes every teardown. `Close()` runs the turn's parked col-5 exactly once (it
	// clears the buffer before calling, so a re-entrant close cannot run it twice) and is a no-op on
	// a conversation that is already over — the ordinary path, where the pick or the terminal
	// advance closed it. Without this, a session displaced mid-line (a second `OpenDialog`, a
	// `Kill`, a map teardown) silently dropped the NPC line's parked col-5.
	if (Closed->Conversation.IsValid())
	{
		Closed->Conversation->Close();
	}
	// A col-5 flushed above may have opened the next conversation. It owns the panel from here;
	// this teardown must not close what it just opened.
	const bool bReplaced = DialogueSession.IsValid();
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

	if (Camera())
	{
		Camera()->ReleaseCamera(Closed->CameraHandle);
	}
	if (FElysiumEntity* OwnerEntity = Resolve(Closing))
	{
		// The token is generation-stamped, so a replacement session that already re-acquired the
		// body carries a newer one and this release cannot take the new claim away.
		OwnerEntity->EndDialogueBodySession(ClosingBodyOwner, bSilent);
	}
	Closed.Reset();

	if (!bReplaced)
	{
		if (IElysiumPresenter* P = Presenter())
		{
			P->CloseDialog();
		}
	}

	if (!bSilent && Closing.IsSet())
	{
		// Route EndDialog to exactly the owning NPC (its InputEndDialog clears bInDialog and fires
		// OnDialogEnd -> DialogPostProcess). Queued through chokepoint 2 like every other input, with
		// the owner as `!self` so no name lookup can hit a same-named entity. The param carries the
		// closed session's serial so the NPC can tell this bookkeeping close from a script-fired
		// `EndDialog` (which must reach the world's teardown itself) and from a stale close arriving
		// after the flushed col-5 re-opened the same NPC.
		static const FName EndDialogInput(TEXT("EndDialog"));
		EnqueueInput(ElysiumEntityWorldShared::GSelfTarget, EndDialogInput,
			FElysiumVariant::Int(static_cast<int32>(ClosingSerial)), 0.0,
			FElysiumEntityHandle(), Closing);
	}
}
