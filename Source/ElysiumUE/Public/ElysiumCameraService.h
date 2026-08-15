#pragma once

#include "CoreMinimal.h"
#include "ElysiumCameraSolve.h"
#include "Subsystems/LocalPlayerSubsystem.h"

#include "ElysiumCameraService.generated.h"

struct FMinimalViewInfo;
struct FElysiumDialogueCameraProfile;

UENUM()
enum class EElysiumCameraRequestKind : uint8
{
	Player,
	Aim,
	Inspect,
	Dialogue,
	Feed,
	Death,
	Sequence,
};

UENUM()
enum class EElysiumCameraControlPolicy : uint8
{
	Preserve,
	LookOnly,
	Locked,
};

UENUM()
enum class EElysiumCameraFallback : uint8
{
	None,
	SourceShot,
	AuthoredProfile,
	PlayerView,
};

// A request identity is valid only for the generation of its slot and the map epoch in which it
// was acquired. Slots can be released out of order; stale/doubled releases are no-ops.
USTRUCT(BlueprintType)
struct FElysiumCameraHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere) int32 Slot = 0;
	UPROPERTY(VisibleAnywhere) uint32 Generation = 0;
	UPROPERTY(VisibleAnywhere) uint64 Epoch = 0;

	bool IsSet() const { return Slot > 0 && Generation > 0 && Epoch > 0; }
	void Reset() { *this = FElysiumCameraHandle(); }
	friend bool operator==(const FElysiumCameraHandle& A, const FElysiumCameraHandle& B)
	{
		return A.Slot == B.Slot && A.Generation == B.Generation && A.Epoch == B.Epoch;
	}
	friend bool operator!=(const FElysiumCameraHandle& A, const FElysiumCameraHandle& B)
	{
		return !(A == B);
	}
};

USTRUCT(BlueprintType)
struct FElysiumCameraAnchor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) FVector Position = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) FName Name;
	UPROPERTY(EditAnywhere) FName Socket;
	UPROPERTY(EditAnywhere) bool bValid = false;
};

// Public request values. Producers resolve their own entity/asset references and publish values;
// the camera service never learns how to move, rotate, navigate, or lock an actor.
USTRUCT(BlueprintType)
struct FElysiumCameraRequest
{
	GENERATED_BODY()

	// Identity / arbitration.
	UPROPERTY(EditAnywhere) EElysiumCameraRequestKind Kind = EElysiumCameraRequestKind::Player;
	UPROPERTY(EditAnywhere) FString Owner;
	UPROPERTY(EditAnywhere) FString DebugName;
	UPROPERTY(EditAnywhere) int32 Priority = 0;

	// Pose and anchors.
	UPROPERTY(EditAnywhere) bool bOverridePose = false;
	UPROPERTY(EditAnywhere) FElysiumCameraAnchor CameraAnchor;
	UPROPERTY(EditAnywhere) FElysiumCameraAnchor LookAtAnchor;
	UPROPERTY(EditAnywhere) FElysiumCameraAnchor SpeakerAnchor;
	UPROPERTY(EditAnywhere) FElysiumCameraAnchor ListenerAnchor;
	FElysiumCameraShot Shot;

	// Lens and transition.
	UPROPERTY(EditAnywhere) float BlendInSeconds = 0.35f;
	UPROPERTY(EditAnywhere) float BlendOutSeconds = 0.25f;
	UPROPERTY(EditAnywhere) bool bCameraCut = false;

	// Control and presentation policy. The input and presentation owners consume these values; this
	// service never applies input mode or widget state itself.
	UPROPERTY(EditAnywhere) EElysiumCameraControlPolicy Control = EElysiumCameraControlPolicy::Preserve;
	// Read by `FElysiumEntityWorld::DialogueCameraHidesHud` off the dialogue session's own stored
	// request — the one request whose presentation policy reaches the frame. A request that merely
	// *wins* arbitration does not gate the HUD; there is no such override.
	UPROPERTY(EditAnywhere) bool bShowHud = true;
	// **Diagnostic and MCP surface only.** Nothing gates the viewmodel on it, and nothing needs to:
	// any adopted scripted camera satisfies `CAM_IsThirdPerson`, and `ElysiumCam::SolveDrawPolicy`
	// already refuses the first-person viewmodel outright in third person
	// (`docs/vtmb/camera-view-modes.md` §5). The named shot's own `DrawViewmodel` key still reaches
	// the draw policy through `FElysiumShotPresentation`; this field is that key carried alongside the
	// request so `elysium.camera` and the MCP dump can report what the shot asked for.
	UPROPERTY(EditAnywhere) bool bDrawViewmodel = true;
	UPROPERTY(EditAnywhere) bool bDialogPOV = false;
	// There is deliberately no player-body field here. Whether the local body draws is the camera's
	// own draw policy (`ElysiumCam::SolveDrawPolicy`), resolved from the mode predicate and the fade
	// band; a request that could override it would be a second owner of the one answer, and retail has
	// no such override (`docs/vtmb/camera-view-modes.md` §6).

	// Candidate policy and diagnostics.
	UPROPERTY(EditAnywhere) bool bCheckCollision = true;
	UPROPERTY(EditAnywhere) bool bCheckOcclusion = true;
	UPROPERTY(EditAnywhere) bool bRequireSubtitleSafe = false;
	UPROPERTY(EditAnywhere) bool bAllowCloseUp = false;
	UPROPERTY(EditAnywhere) EElysiumCameraFallback Fallback = EElysiumCameraFallback::None;
	UPROPERTY(EditAnywhere) FString SourceShot;
	UPROPERTY(EditAnywhere) FString SelectedProfile;
	UPROPERTY(EditAnywhere) FString FallbackReason;
};

USTRUCT(BlueprintType)
struct FElysiumResolvedCameraState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere) bool bActive = false;
	UPROPERTY(VisibleAnywhere) FElysiumCameraHandle Handle;
	UPROPERTY(VisibleAnywhere) FElysiumCameraRequest Request;
	UPROPERTY(VisibleAnywhere) FVector Location = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(VisibleAnywhere) float FieldOfView = 0.0f;
	UPROPERTY(VisibleAnywhere) float Weight = 0.0f;
	UPROPERTY(VisibleAnywhere) FString CandidateRejections;
};

class IElysiumCameraService
{
public:
	virtual ~IElysiumCameraService() = default;

	virtual FElysiumCameraHandle AcquireCamera(const FElysiumCameraRequest& Request) = 0;
	virtual bool UpdateCamera(FElysiumCameraHandle Handle, const FElysiumCameraRequest& Request) = 0;
	virtual bool ReleaseCamera(FElysiumCameraHandle Handle) = 0;
	virtual bool IsCameraLive(FElysiumCameraHandle Handle) const = 0;
	virtual bool EvaluateDialogueCandidate(const FElysiumCameraRequest& Request,
		FString& OutReason) const = 0;
	virtual bool DialogueCamerasEnabled() const = 0;
	virtual void GetDialogueProfiles(TArray<FElysiumDialogueCameraProfile>& Out) const = 0;
	virtual const FElysiumResolvedCameraState& ResolvedCamera() const = 0;
};

// One request registry per local player. The player camera manager is still the sole final-view
// writer: it advances this policy object and composes the resolved value into FMinimalViewInfo.
UCLASS()
class UElysiumCameraService final : public ULocalPlayerSubsystem, public IElysiumCameraService
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual FElysiumCameraHandle AcquireCamera(const FElysiumCameraRequest& Request) override;
	virtual bool UpdateCamera(FElysiumCameraHandle Handle,
		const FElysiumCameraRequest& Request) override;
	virtual bool ReleaseCamera(FElysiumCameraHandle Handle) override;
	virtual bool IsCameraLive(FElysiumCameraHandle Handle) const override;
	virtual bool EvaluateDialogueCandidate(const FElysiumCameraRequest& Request,
		FString& OutReason) const override;
	virtual bool DialogueCamerasEnabled() const override;
	virtual void GetDialogueProfiles(TArray<FElysiumDialogueCameraProfile>& Out) const override;
	virtual const FElysiumResolvedCameraState& ResolvedCamera() const override { return Resolved; }

	// A camera request is held on behalf of the map that pushed it, so the request set is map-epoch
	// state (S4). Driven by UElysiumMapSubsystem's boundary, which this service subscribes to; both
	// stay public so a headless test can step an epoch without a map.
	void BeginMapEpoch(uint64 Epoch);
	void RetireMapEpoch(uint64 Epoch);

	void Advance(float DeltaSeconds);
	void ApplyToView(FMinimalViewInfo& InOutView) const;
	void DescribeRequests(TArray<FString>& Out) const;
	uint64 CurrentEpoch() const { return Epoch; }

private:
	struct FEntry
	{
		FElysiumCameraHandle Handle;
		FElysiumCameraRequest Request;
		uint64 Serial = 0;
	};

	FEntry* FindEntry(FElysiumCameraHandle Handle);
	const FEntry* FindEntry(FElysiumCameraHandle Handle) const;
	const FEntry* Winner() const;
	void SelectWinner(const FEntry* Entry);

	TArray<FEntry> Entries;
	TMap<int32, uint32> SlotGenerations;
	TArray<int32> FreeSlots;
	int32 NextSlot = 1;
	uint64 NextSerial = 1;
	uint64 Epoch = 0;
	uint64 LastAdvancedFrame = TNumericLimits<uint64>::Max();
	FElysiumResolvedCameraState Resolved;
	FVector TrackingLocation = FVector::ZeroVector;
	FRotator TrackingRotation = FRotator::ZeroRotator;
	bool bTrackingSeeded = false;
	FDelegateHandle MapEpochBeginHandle;
	FDelegateHandle MapEpochRetiredHandle;
};
