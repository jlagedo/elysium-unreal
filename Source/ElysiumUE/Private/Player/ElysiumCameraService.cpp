#include "ElysiumCameraService.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumUserSettings.h"

#include "Engine/GameInstance.h"

#include "Camera/CameraTypes.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	float Approach(float Value, float Target, float Seconds, float DeltaSeconds)
	{
		if (Seconds <= KINDA_SMALL_NUMBER)
		{
			return Target;
		}
		return FMath::FInterpConstantTo(Value, Target, DeltaSeconds, 1.0f / Seconds);
	}

	bool Finite(const FVector& V)
	{
		return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
	}

	const TCHAR* KindName(EElysiumCameraRequestKind Kind)
	{
		switch (Kind)
		{
		case EElysiumCameraRequestKind::Player:   return TEXT("Player");
		case EElysiumCameraRequestKind::Aim:      return TEXT("Aim");
		case EElysiumCameraRequestKind::Inspect:  return TEXT("Inspect");
		case EElysiumCameraRequestKind::Dialogue: return TEXT("Dialogue");
		case EElysiumCameraRequestKind::Feed:     return TEXT("Feed");
		case EElysiumCameraRequestKind::Death:    return TEXT("Death");
		case EElysiumCameraRequestKind::Sequence: return TEXT("Sequence");
		default:                                  return TEXT("Unknown");
		}
	}
}

FElysiumCameraHandle UElysiumCameraService::AcquireCamera(const FElysiumCameraRequest& Request)
{
	if (Epoch == 0)
	{
		return FElysiumCameraHandle();
	}
	const int32 Slot = FreeSlots.IsEmpty() ? NextSlot++ : FreeSlots.Pop(EAllowShrinking::No);
	uint32& Generation = SlotGenerations.FindOrAdd(Slot);
	++Generation;
	if (Generation == 0)
	{
		++Generation;
	}

	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Handle.Slot = Slot;
	Entry.Handle.Generation = Generation;
	Entry.Handle.Epoch = Epoch;
	Entry.Request = Request;
	Entry.Serial = NextSerial++;
	return Entry.Handle;
}

bool UElysiumCameraService::UpdateCamera(FElysiumCameraHandle Handle,
	const FElysiumCameraRequest& Request)
{
	if (FEntry* Entry = FindEntry(Handle))
	{
		Entry->Request = Request;
		if (Resolved.Handle == Handle)
		{
			Resolved.Request = Request;
		}
		return true;
	}
	return false;
}

bool UElysiumCameraService::ReleaseCamera(FElysiumCameraHandle Handle)
{
	const int32 Index = Entries.IndexOfByPredicate(
		[Handle](const FEntry& Entry) { return Entry.Handle == Handle; });
	if (Index == INDEX_NONE || Handle.Epoch != Epoch)
	{
		return false;
	}
	Entries.RemoveAt(Index);
	FreeSlots.Add(Handle.Slot);
	return true;
}

bool UElysiumCameraService::IsCameraLive(FElysiumCameraHandle Handle) const
{
	return FindEntry(Handle) != nullptr;
}

UElysiumCameraService::FEntry* UElysiumCameraService::FindEntry(FElysiumCameraHandle Handle)
{
	return Entries.FindByPredicate([Handle, this](const FEntry& Entry)
	{
		return Handle.Epoch == Epoch && Entry.Handle == Handle;
	});
}

const UElysiumCameraService::FEntry* UElysiumCameraService::FindEntry(
	FElysiumCameraHandle Handle) const
{
	return Entries.FindByPredicate([Handle, this](const FEntry& Entry)
	{
		return Handle.Epoch == Epoch && Entry.Handle == Handle;
	});
}

const UElysiumCameraService::FEntry* UElysiumCameraService::Winner() const
{
	const FEntry* Best = nullptr;
	for (const FEntry& Entry : Entries)
	{
		if (!Best || Entry.Request.Priority > Best->Request.Priority
			|| (Entry.Request.Priority == Best->Request.Priority && Entry.Serial > Best->Serial))
		{
			Best = &Entry;
		}
	}
	return Best;
}

void UElysiumCameraService::SelectWinner(const FEntry* Entry)
{
	if (!Entry)
	{
		Resolved.bActive = false;
		Resolved.Handle.Reset();
		return;
	}
	Resolved.bActive = true;
	Resolved.Handle = Entry->Handle;
	Resolved.Request = Entry->Request;
	Resolved.CandidateRejections.Reset();
	Resolved.Weight = Entry->Request.BlendInSeconds <= KINDA_SMALL_NUMBER ? 1.0f : 0.0f;
	bTrackingSeeded = false;
}

void UElysiumCameraService::Advance(float DeltaSeconds)
{
	if (LastAdvancedFrame == GFrameCounter)
	{
		return;
	}
	LastAdvancedFrame = GFrameCounter;
	const float Dt = FMath::Max(0.0f, DeltaSeconds);
	const FEntry* Best = Winner();
	if (Best && Best->Handle != Resolved.Handle)
	{
		SelectWinner(Best);
	}
	else if (!Best && Resolved.Handle.IsSet())
	{
		// Keep the last pose while its one blend-out runs. Its handle is cleared immediately so stale
		// updates/releases cannot regain ownership during the fade.
		Resolved.Handle.Reset();
		Resolved.bActive = false;
	}
	else if (Best)
	{
		Resolved.Request = Best->Request;
	}

	if (Best)
	{
		Resolved.Weight = Approach(Resolved.Weight, 1.0f,
			Resolved.Request.BlendInSeconds, Dt);
	}
	else
	{
		Resolved.Weight = Approach(Resolved.Weight, 0.0f,
			Resolved.Request.BlendOutSeconds, Dt);
		if (Resolved.Weight <= KINDA_SMALL_NUMBER)
		{
			Resolved = FElysiumResolvedCameraState();
			bTrackingSeeded = false;
			return;
		}
	}

	if (!Resolved.Request.bOverridePose)
	{
		return;
	}

	// The shot's own values are re-resolved by whoever pushed it EVERY frame — retail's camera think
	// re-resolves all four anchors every server tick too (`vampire.dll` `FUN_1006e8e0`, loop
	// `0x1006ea90`) — so the goal jitters with the subject's animation whenever an anchor is a bone.
	// **What holds the camera still is this tracker, not the anchor mode**: `C_BaseCineCamera`'s
	// tolerance deadbands (`FUN_10001fe0` / `FUN_10001d40`) park the pose until the goal drifts past
	// the shot's own `DistanceTolerance` / `AngularTolerance`, which for a conversation shot is 10
	// degrees — wider than any head-bone motion.
	const FElysiumCameraShot& Shot = Resolved.Request.Shot;
	if (!bTrackingSeeded)
	{
		Tracker.Start(Shot);
		bTrackingSeeded = true;
	}
	else
	{
		Tracker.Advance(Shot, Dt);
	}
	TrackingLocation = Tracker.Location;
	TrackingRotation = Tracker.Rotation;
	Resolved.Location = TrackingLocation;
	Resolved.Rotation = TrackingRotation;
	Resolved.FieldOfView = Shot.FieldOfView;
}

void UElysiumCameraService::ApplyToView(FMinimalViewInfo& InOutView) const
{
	if (!Resolved.Request.bOverridePose || Resolved.Weight <= 0.0f)
	{
		return;
	}
	// A `vdata/camerashots/` `FieldOfView` is 4:3-referenced and Source is Hor+, so the authored
	// number widens to the window's aspect here rather than in the parse — the shot keeps the value
	// its file wrote and only the rendered frame carries the window's.
	float Fov = InOutView.FOV;
	ElysiumCam::ComposeScriptedShot(InOutView.Location, InOutView.Rotation, Fov,
		Resolved.Location, Resolved.Rotation,
		ElysiumCam::WidenSourceFov(Resolved.FieldOfView,
			ElysiumCameraView::RenderAspectRatio(InOutView.AspectRatio)), Resolved.Weight);
	InOutView.FOV = Fov;
	if (Resolved.Request.bCameraCut && Resolved.Weight >= 1.0f)
	{
		InOutView.PreviousViewTransform = FTransform(InOutView.Rotation, InOutView.Location);
	}
}

bool UElysiumCameraService::EvaluateDialogueCandidate(const FElysiumCameraRequest& Request,
	FString& OutReason) const
{
	OutReason.Reset();
	if (!Request.bOverridePose)
	{
		return true;
	}
	const FElysiumCameraShot& Shot = Request.Shot;
	if (!Finite(Shot.Origin) || !Finite(Shot.LookAt))
	{
		OutReason = TEXT("non-finite pose");
		return false;
	}
	const float ViewDistance = FVector::Distance(Shot.Origin, Shot.LookAt);
	if (ViewDistance < 30.0f)
	{
		OutReason = TEXT("near-plane distance");
		return false;
	}

	// Subtitle-safe framing is deterministic geometry, not viewport pixels: both declared eye
	// anchors must lie inside a conservative 70-degree horizontal / 52-degree vertical cone.
	if (Request.bRequireSubtitleSafe)
	{
		const FVector Forward = (Shot.LookAt - Shot.Origin).GetSafeNormal();
		const FRotationMatrix Basis(Forward.Rotation());
		for (const FElysiumCameraAnchor* Anchor : { &Request.SpeakerAnchor, &Request.ListenerAnchor })
		{
			if (!Anchor->bValid)
			{
				OutReason = TEXT("missing subtitle anchor");
				return false;
			}
			const FVector Local = Basis.GetTransposed().TransformVector(
				(Anchor->Position - Shot.Origin).GetSafeNormal());
			const float H = FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)));
			const float V = FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(Local.Z,
				FMath::Sqrt(Local.X * Local.X + Local.Y * Local.Y))));
			if (H > 35.0f || V > 26.0f)
			{
				OutReason = TEXT("subtitle-safe bounds");
				return false;
			}
		}
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumDialogueCamera), false);
	if (const ULocalPlayer* LP = GetLocalPlayer())
	{
		if (const APlayerController* PC = LP->GetPlayerController(World))
		{
			Params.AddIgnoredActor(PC->GetPawn());
		}
	}
	if (Request.bCheckCollision && World->OverlapBlockingTestByChannel(Shot.Origin,
		FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(12.0f), Params))
	{
		OutReason = TEXT("camera collision");
		return false;
	}
	if (Request.bCheckOcclusion)
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Shot.Origin, Shot.LookAt, ECC_Visibility, Params)
			&& Hit.Distance + 30.0f < ViewDistance)
		{
			OutReason = TEXT("target occluded");
			return false;
		}
	}
	return true;
}

bool UElysiumCameraService::DialogueCamerasEnabled() const
{
	if (GEngine)
	{
		if (const UElysiumUserSettings* Settings =
			Cast<UElysiumUserSettings>(GEngine->GetGameUserSettings()))
		{
			return Settings->bDialogueCamerasEnabled;
		}
	}
	return true;
}

void UElysiumCameraService::GetDialogueProfiles(
	TArray<FElysiumDialogueCameraProfile>& Out) const
{
	static const FString AssetPath = FElysiumContentPaths::DialogueCameraSet();
	if (const UElysiumDialogueCameraSet* Set = LoadObject<UElysiumDialogueCameraSet>(nullptr, *AssetPath))
	{
		if (!Set->Profiles.IsEmpty())
		{
			Out = Set->Profiles;
			return;
		}
	}
	// Headless tests and a clean checkout have no generated package. These values mirror the tracked
	// JSON and preserve deterministic logic without manufacturing a UObject substitute.
	Out = ElysiumDialogueCamera::DefaultProfiles();
}

namespace
{
	// The map subsystem is GameInstance-scoped and this service is LocalPlayer-scoped, so the
	// collection cannot express the dependency. Local players are created after the game instance's
	// own subsystems, so the lookup resolves for the whole of this service's life.
	UElysiumMapSubsystem* MapsFor(const ULocalPlayer* Player)
	{
		UGameInstance* GI = Player ? Player->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UElysiumMapSubsystem>() : nullptr;
	}
}

void UElysiumCameraService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UElysiumMapSubsystem* Maps = MapsFor(GetLocalPlayer()))
	{
		MapEpochBeginHandle = Maps->OnMapEpochBegin().AddUObject(
			this, &UElysiumCameraService::BeginMapEpoch);
		MapEpochRetiredHandle = Maps->OnMapEpochRetired().AddUObject(
			this, &UElysiumCameraService::RetireMapEpoch);
	}
}

void UElysiumCameraService::Deinitialize()
{
	if (UElysiumMapSubsystem* Maps = MapsFor(GetLocalPlayer()))
	{
		Maps->OnMapEpochBegin().Remove(MapEpochBeginHandle);
		Maps->OnMapEpochRetired().Remove(MapEpochRetiredHandle);
	}
	MapEpochBeginHandle.Reset();
	MapEpochRetiredHandle.Reset();
	Super::Deinitialize();
}

void UElysiumCameraService::BeginMapEpoch(uint64 NewEpoch)
{
	Entries.Reset();
	FreeSlots.Reset();
	NextSlot = 1;
	Resolved = FElysiumResolvedCameraState();
	bTrackingSeeded = false;
	Epoch = NewEpoch;
}

void UElysiumCameraService::RetireMapEpoch(uint64 RetiringEpoch)
{
	if (RetiringEpoch != Epoch)
	{
		return;
	}
	Entries.Reset();
	FreeSlots.Reset();
	NextSlot = 1;
	Resolved = FElysiumResolvedCameraState();
	bTrackingSeeded = false;
	Epoch = 0;
}

void UElysiumCameraService::DescribeRequests(TArray<FString>& Out) const
{
	Out.Reset();
	TArray<const FEntry*> Sorted;
	for (const FEntry& Entry : Entries)
	{
		Sorted.Add(&Entry);
	}
	Sorted.Sort([](const FEntry& A, const FEntry& B)
	{
		return A.Request.Priority != B.Request.Priority
			? A.Request.Priority > B.Request.Priority : A.Serial > B.Serial;
	});
	for (const FEntry* Entry : Sorted)
	{
		Out.Add(FString::Printf(TEXT("%s p%d slot=%d gen=%u epoch=%llu owner=%s source=%s profile=%s"),
			KindName(Entry->Request.Kind), Entry->Request.Priority, Entry->Handle.Slot,
			Entry->Handle.Generation, Entry->Handle.Epoch, *Entry->Request.Owner,
			*Entry->Request.SourceShot, *Entry->Request.SelectedProfile));
	}
}
