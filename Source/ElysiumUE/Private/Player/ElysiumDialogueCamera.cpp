#include "ElysiumDialogueCamera.h"

namespace
{
	FVector SafeConversationAxis(const FElysiumDialogueCameraContext& Context)
	{
		FVector Axis = Context.SpeakerEye - Context.ListenerEye;
		Axis.Z = 0.0f;
		return Axis.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	}

	FVector SideAxis(const FVector& ConversationAxis, float ScreenSide)
	{
		return FVector::CrossProduct(FVector::UpVector, ConversationAxis).GetSafeNormal()
			* (ScreenSide < 0.0f ? -1.0f : 1.0f);
	}

	FElysiumDialogueCameraProfile Profile(FName Name, EElysiumDialogueShotProfile Kind,
		float Distance, float Lateral, float Height, float Fov, float Hold,
		bool bClose = false, bool bEstablishing = false)
	{
		FElysiumDialogueCameraProfile Out;
		Out.Name = Name;
		Out.Kind = Kind;
		Out.DistanceCm = Distance;
		Out.LateralCm = Lateral;
		Out.HeightCm = Height;
		Out.FieldOfView = Fov;
		Out.MinimumHoldSeconds = Hold;
		Out.bAllowCloseUp = bClose;
		Out.bEstablishing = bEstablishing;
		return Out;
	}
}

const TCHAR* ElysiumDialogueCamera::LexToString(EElysiumDialogOpenerKind Kind)
{
	switch (Kind)
	{
	case EElysiumDialogOpenerKind::Forced:   return TEXT("StartPlayerDialog");
	case EElysiumDialogOpenerKind::Remote:   return TEXT("StartPlayerDialogRemote");
	case EElysiumDialogOpenerKind::Unforced: return TEXT("StartPlayerDialogUnforced");
	case EElysiumDialogOpenerKind::Use:      return TEXT("PlayerUse");
	default:                                 return TEXT("Unknown");
	}
}

const TCHAR* ElysiumDialogueCamera::LexToString(EElysiumDialogueShotProfile Kind)
{
	switch (Kind)
	{
	case EElysiumDialogueShotProfile::TwoShot:               return TEXT("two-shot");
	case EElysiumDialogueShotProfile::SingleSpeaker:         return TEXT("single-speaker");
	case EElysiumDialogueShotProfile::SingleListener:        return TEXT("single-listener");
	case EElysiumDialogueShotProfile::OverShoulderSpeaker:   return TEXT("ots-speaker");
	case EElysiumDialogueShotProfile::OverShoulderListener:  return TEXT("ots-listener");
	case EElysiumDialogueShotProfile::CloseSpeaker:          return TEXT("close-speaker");
	case EElysiumDialogueShotProfile::Fallback:              return TEXT("player-view");
	default:                                                  return TEXT("unknown");
	}
}

TArray<FElysiumDialogueCameraProfile> ElysiumDialogueCamera::DefaultProfiles()
{
	TArray<FElysiumDialogueCameraProfile> Out;
	Out.Add(Profile(TEXT("EstablishingTwoShot"), EElysiumDialogueShotProfile::TwoShot,
		235.0f, 90.0f, 12.0f, 50.0f, 2.5f, false, true));
	Out.Add(Profile(TEXT("SpeakerSingle"), EElysiumDialogueShotProfile::SingleSpeaker,
		185.0f, 52.0f, 7.0f, 44.0f, 2.0f));
	Out.Add(Profile(TEXT("SpeakerOTS"), EElysiumDialogueShotProfile::OverShoulderSpeaker,
		82.0f, 34.0f, 5.0f, 48.0f, 2.0f));
	Out.Add(Profile(TEXT("ListenerSingle"), EElysiumDialogueShotProfile::SingleListener,
		185.0f, 52.0f, 7.0f, 44.0f, 2.0f));
	Out.Add(Profile(TEXT("ListenerOTS"), EElysiumDialogueShotProfile::OverShoulderListener,
		82.0f, 34.0f, 5.0f, 48.0f, 2.0f));
	Out.Add(Profile(TEXT("SpeakerClose"), EElysiumDialogueShotProfile::CloseSpeaker,
		105.0f, 24.0f, 4.0f, 38.0f, 2.4f, true));
	Out.Add(Profile(TEXT("PlayerViewFallback"), EElysiumDialogueShotProfile::Fallback,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f));
	return Out;
}

FElysiumCameraRequest ElysiumDialogueCamera::BuildRequest(
	const FElysiumDialogueCameraProfile& Profile,
	const FElysiumDialogueCameraContext& Context)
{
	FElysiumCameraRequest Request;
	Request.Kind = EElysiumCameraRequestKind::Dialogue;
	Request.Owner = Context.Owner;
	Request.DebugName = FString::Printf(TEXT("Dialogue:%s"), *Profile.Name.ToString());
	Request.Priority = 500;
	Request.SelectedProfile = Profile.Name.ToString();
	Request.Fallback = EElysiumCameraFallback::AuthoredProfile;
	Request.BlendInSeconds = 0.35f;
	// **The release is a cut** (M1, ruled). `EndPlayerDialog` (`vampire.dll` `0x10178400`) is an
	// unconditional `UTIL_Remove` of the camera and there is no blend field anywhere on
	// `C_BaseCineCamera`: retail hands control back on the *same tick* the camera dies, together with
	// `SetImmobilized(false)`, the weapon restore and the HUD restore. The 0.25 s here was the port's
	// invention and composed a dead camera over a player who already had input; it is deleted rather
	// than defaulted, and no cvar exists that could bring it back.
	Request.BlendOutSeconds = 0.0f;
	Request.Control = EElysiumCameraControlPolicy::Preserve;
	Request.bShowHud = true;
	Request.bDrawViewmodel = false;
	Request.bRequireSubtitleSafe = true;
	Request.bAllowCloseUp = Profile.bAllowCloseUp;
	Request.SpeakerAnchor = { Context.SpeakerEye, TEXT("speaker-eye"), NAME_None, true };
	Request.ListenerAnchor = { Context.ListenerEye, TEXT("listener-eye"), NAME_None, true };

	if (Profile.Kind == EElysiumDialogueShotProfile::Fallback)
	{
		Request.bOverridePose = false;
		Request.Fallback = EElysiumCameraFallback::PlayerView;
		Request.FallbackReason = TEXT("no safe dialogue candidate");
		return Request;
	}
	if (Profile.bAllowCloseUp && !Context.bAllowCloseUp)
	{
		Request.bOverridePose = false;
		Request.Fallback = EElysiumCameraFallback::PlayerView;
		Request.FallbackReason = TEXT("close-up policy disabled");
		return Request;
	}

	const FVector Axis = SafeConversationAxis(Context);
	const FVector Side = SideAxis(Axis, Context.ScreenSide);
	const FVector Mid = (Context.SpeakerEye + Context.ListenerEye) * 0.5f;
	FVector Origin = Mid;
	FVector LookAt = Mid;

	switch (Profile.Kind)
	{
	case EElysiumDialogueShotProfile::TwoShot:
		Origin = Mid - Axis * Profile.DistanceCm + Side * Profile.LateralCm;
		LookAt = Mid;
		break;
	case EElysiumDialogueShotProfile::SingleSpeaker:
		Origin = Context.SpeakerEye - Axis * Profile.DistanceCm + Side * Profile.LateralCm;
		LookAt = Context.SpeakerEye;
		break;
	case EElysiumDialogueShotProfile::SingleListener:
		Origin = Context.ListenerEye + Axis * Profile.DistanceCm + Side * Profile.LateralCm;
		LookAt = Context.ListenerEye;
		break;
	case EElysiumDialogueShotProfile::OverShoulderSpeaker:
		Origin = Context.ListenerEye - Axis * Profile.DistanceCm + Side * Profile.LateralCm;
		LookAt = Context.SpeakerEye;
		break;
	case EElysiumDialogueShotProfile::OverShoulderListener:
		Origin = Context.SpeakerEye + Axis * Profile.DistanceCm + Side * Profile.LateralCm;
		LookAt = Context.ListenerEye;
		break;
	case EElysiumDialogueShotProfile::CloseSpeaker:
		Origin = Context.SpeakerEye - Axis * Profile.DistanceCm + Side * Profile.LateralCm;
		LookAt = Context.SpeakerEye;
		break;
	default:
		break;
	}
	Origin.Z += Profile.HeightCm;
	Request.bOverridePose = true;
	Request.Shot.Origin = Origin;
	Request.Shot.LookAt = LookAt;
	Request.Shot.bUseLookAt = true;
	// A profile stands in for a `vdata/camerashots/` dialogue shot, which retail runs in `CamMode` 1:
	// it is tracked, at the record's own rates, not copied through like a `camera_track` value.
	Request.Shot.bTracked = true;
	// **`dialogdefault.txt`'s `CameraConstraints`, verbatim** — the record every conversation retail
	// opens runs on, and the numbers the tracker needs to move at all. A tracked shot with the
	// struct defaults carries `MoveSpeed 0` and `TurnAccel 0`, and retail's `TurnAccel == 0` arm
	// leaves the turn rate exactly where it is (`FUN_10001c80` → `FUN_10001070` with a zero step),
	// so such a shot would open aimed and then never turn again. These are the file's own values:
	// MoveSpeed 500 u/s, MoveAccel 250 u/s², TurnAccel 30 deg/s², MaxTurnRate [60,60,60],
	// DistanceTolerance 5 u, AngularTolerance [10,10,10]°, `SyncRotateOnMove` set — and the 10°
	// deadband is what holds a conversation camera still while its subject's head animates.
	Request.Shot.MoveSpeed = 500.0f * ElysiumCam::U;
	Request.Shot.MoveAccel = 250.0f * ElysiumCam::U;
	Request.Shot.TurnAccel = 30.0f;
	Request.Shot.MaxTurnRate = FVector(60.0f, 60.0f, 60.0f);
	Request.Shot.DistanceTolerance = 5.0f * ElysiumCam::U;
	Request.Shot.AngularTolerance = FVector(10.0f, 10.0f, 10.0f);
	Request.Shot.bSyncRotateOnMove = true;
	Request.Shot.FieldOfView = Profile.FieldOfView;
	Request.Shot.BlendSeconds = Request.BlendInSeconds;
	Request.Shot.DebugName = Request.DebugName;
	Request.CameraAnchor = { Origin, TEXT("camera"), NAME_None, true };
	Request.LookAtAnchor = { LookAt, TEXT("look-at"), NAME_None, true };
	return Request;
}

bool ElysiumDialogueCamera::PreservesScreenSide(const FElysiumCameraRequest& A,
	const FElysiumCameraRequest& B)
{
	if (!A.SpeakerAnchor.bValid || !A.ListenerAnchor.bValid
		|| !B.SpeakerAnchor.bValid || !B.ListenerAnchor.bValid)
	{
		return true;
	}
	const FVector AxisA = (A.SpeakerAnchor.Position - A.ListenerAnchor.Position).GetSafeNormal();
	const FVector AxisB = (B.SpeakerAnchor.Position - B.ListenerAnchor.Position).GetSafeNormal();
	const float SideA = FVector::DotProduct(
		FVector::CrossProduct(AxisA, A.Shot.Origin - A.ListenerAnchor.Position), FVector::UpVector);
	const float SideB = FVector::DotProduct(
		FVector::CrossProduct(AxisB, B.Shot.Origin - B.ListenerAnchor.Position), FVector::UpVector);
	return SideA == 0.0f || SideB == 0.0f || FMath::Sign(SideA) == FMath::Sign(SideB);
}
