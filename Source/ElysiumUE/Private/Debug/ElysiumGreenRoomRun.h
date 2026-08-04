#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ElysiumEntityHandle.h"

class AActor;
class AElysiumMapActor;
class UElysiumMapSubsystem;
class UPointLightComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;

namespace ElysiumCameraTrack
{
	struct FPath;
}

// Deterministic rendered validation for VtMB bodies, cinematic banks, and skeletal props. Activated
// by -ElysiumGreenRoom with a real RHI. Individual cases use a neutral stage; the embrace case places
// the validated cast at the authored theatre scene origin and samples both real camera streams
// through the production world seam. Every case seeks fixed absolute times, captures PNGs plus
// numeric bounds/root/facing/camera data, and exits. The courtroom case pairs one seated audience
// actor with LaCroix so a whole-body reversal cannot hide behind a plausible isolated pose. This is
// the gate before an aggregate theatre run: one model, clip, placement, or edit can fail in isolation.
class FElysiumGreenRoomRun
{
public:
	// bForceLab arms the interactive lab regardless of the command line — the door `elysium.gr`
	// uses to stand a green room up inside a session that never asked for one.
	explicit FElysiumGreenRoomRun(UElysiumMapSubsystem* InSubsystem, bool bForceLab = false);
	~FElysiumGreenRoomRun();

	static bool IsRequested();

	// --- the interactive lab (`elysium.gr`) -------------------------------------------------
	//
	// Everything above this line is a one-shot: resolve fixed cases, seek fixed times, capture,
	// exit. The lab is the same stage and the same body factory with the state machine's tail
	// removed — it builds the room, stands one body on it, and then does nothing until asked. It
	// captures nothing and never exits, because the questions it answers are the ones a still
	// cannot hold: whether a garment settles, whether it flares too eagerly, whether a hem clears
	// the knee at walking speed. The Cog window is the only thing that drives it.

	// What the window owns and the run reads back every frame. Kept as one struct passed by
	// reference rather than a wall of setters: it is all display state, all written from the game
	// thread by exactly one window, and a slider that has to round-trip through an accessor is a
	// slider that fights the immediate-mode UI it lives in.
	struct FLabView
	{
		// Degrees around the body and above it, and the pull-back distance as a multiple of the
		// automatic fit. 1 is the framing the capture path chooses.
		float OrbitYaw = 35.0f;
		float OrbitPitch = -6.0f;
		float DistanceScale = 1.0f;
		// 0 = the feet, 1 = the top of the bounds. Half is the hip, which is where a garment is.
		float LookHeight = 0.5f;

		bool bPaused = false;
		float Speed = 1.0f;

		// The game's own HUD over the stage. Off, because the lab is not showing the player
		// anything: a masquerade meter and a reticle across a model being inspected are noise, and
		// the reticle in particular sits exactly where a hem is being watched.
		bool bShowHud = false;

		// The lattice chains and the collider spheres, drawn in the world. A collapsed chain or a
		// thigh sphere in the wrong place is obvious here and invisible in the silhouette.
		bool bDrawLattice = false;
		bool bDrawColliders = false;
	};

	bool IsLab() const { return bLab; }
	// The stage exists and the lab is accepting bodies.
	bool IsLabReady() const { return bLab && Phase == EPhase::Lab; }
	// Stand a body on the stage, replacing whatever is there. An empty clip asks the model's own
	// idle policy for one, so a stem alone is a complete request.
	bool LabSetBody(const FString& Stem, const FString& Clip, FString& OutError);
	const FString& LabStem() const { return ReviewStem; }
	const FString& LabClip() const { return ReviewClip; }
	USkeletalMeshComponent* LabBody() const;
	float LabDuration() const;
	float LabTime() const { return LabClipTime; }
	void LabSetTime(float Seconds);
	FLabView& LabView() { return LabViewState; }

private:
	struct FCase
	{
		FString Label;
		FString MeshStem;
		FString AnimSetModel;
		FString BoneRoot;
		bool bPlayerSurface = false;
		bool bAnimatedProp = false;
		bool bLoop = false;
		FString ClipName;
		bool bResolvedClip = false;
	};

	struct FBodyEntry
	{
		FCase Case;
		TWeakObjectPtr<USkeletalMeshComponent> Body;
		float Duration = 0.0f;
	};

	struct FBoneMetric
	{
		FString Name;
		FVector AuthoredLocation = FVector::ZeroVector;
		FVector2D NormalizedView = FVector2D::ZeroVector;
		float ViewDepth = 0.0f;
		bool bInFrame = false;
	};

	struct FBodyMetric
	{
		FString Label;
		FString MeshStem;
		FVector AuthoredRoot = FVector::ZeroVector;
		FRotator AuthoredRootRotation = FRotator::ZeroRotator;
		FVector AuthoredHead = FVector::ZeroVector;
		FVector AuthoredHeadForward = FVector::ZeroVector;
		float FacingDotToFocus = 0.0f;
		bool bHasFacingFocus = false;
		FVector AuthoredBoundsCenter = FVector::ZeroVector;
		FVector BoundsExtent = FVector::ZeroVector;
		FVector CenteringOffset = FVector::ZeroVector;
		TArray<FBoneMetric> KeyBones;
		int32 VisibleKeyBones = 0;
		float TimeSeconds = 0.0f;
	};

	struct FCameraMetric
	{
		bool bValid = false;
		float SceneTime = 0.0f;
		int32 PositionSegment = INDEX_NONE;
		int32 TargetSegment = INDEX_NONE;
		FVector Position = FVector::ZeroVector;
		FVector Target = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Roll = 0.0f;
		float FieldOfView = 0.0f;
		float FadeAlpha = 0.0f;
	};

	struct FFadeWindow
	{
		float StartTime = 0.0f;
		float Duration = 0.0f;
		float HoldTime = 0.0f;
		float Opacity = 1.0f;
		bool bAutoReverse = false;
	};

	struct FShot
	{
		FString Label;
		FString File;
		float Fraction = 0.0f;
		float ViewYaw = 0.0f;
		int32 Width = 0;
		int32 Height = 0;
		bool bOk = false;
		TArray<FBodyMetric> Bodies;
		FCameraMetric Camera;
	};

	enum class EPhase : uint8 { WaitReady, PoseWarmup, Settle, Await, Lab, Done };

	bool Tick(float DeltaSeconds);
	// One frame of the lab: advance the clip under the window's playback state, re-frame the stage
	// and the orbit camera around whatever the body is doing now, and draw the requested overlays.
	void TickLab(float DeltaSeconds);
	void DrawLabOverlays() const;
	// Push FLabView::bShowHud at the local-player HUD. Called every lab frame rather than on the
	// edge: the HUD subsystem rebuilds its root on travel and on a controller change, and a
	// one-shot hide would lose to either.
	void ApplyLabHud(bool bShow) const;
	UWorld* GetWorld() const;
	AElysiumMapActor* GetMap() const;
	void ResolveCases();
	bool CreateStage();
	bool PrepareTheatreCase();
	bool BuildBodies();
	void DestroyBodies();
	void SeekPose();
	bool PrepareFrame();
	void UpdateStage(const FBox& Bounds);
	void PublishCamera(const FBox& Bounds);
	void PublishTheatreCamera();
	float CurrentSceneTime() const;
	float TheatreFadeAlpha(float SceneTime) const;
	FVector BodyOrigin() const;
	FRotator BodyRotation() const;
	void PinCameraAndPlayerSurface();
	void BeginCapture();
	void Advance();
	void Finish();
	FString CurrentLabel() const;
	FString OutputDirectory() const;

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;
	EPhase Phase = EPhase::WaitReady;
	int32 FrameInPhase = 0;
	int32 SettleFrames = 15;
	int32 CaseIndex = 0;
	int32 FractionIndex = 0;
	int32 ViewIndex = 0;
	FString Selector = TEXT("player");
	FString ReviewStem;
	FString ReviewClip;
	FString ReviewAnimSet;
	FString ReviewBoneRoot;
	bool bEnsemble = false;
	bool bTheatreCamera = false;
	bool bCourtroom = false;
	bool bReview = false;
	bool bPlayerSurfaceActive = false;
	bool bAwaitingCapture = false;
	bool bAnyFailure = false;
	// `-GreenRoomLive`: keep the window up after the captures instead of exiting. The stills answer
	// whether a body is there and holds together; anything about *timing* — a garment settling, a
	// blend easing — only reads in motion, and there is otherwise no way to watch one.
	bool bLive = false;
	// `-GreenRoomLab` (or `elysium.gr`): the interactive mode. Implies live — a lab that exits when
	// it has finished building is not a lab.
	bool bLab = false;
	FLabView LabViewState;
	float LabClipTime = 0.0f;
	int32 CameraShotId = 0;

	TArray<FCase> ActiveCases;
	TArray<FBodyEntry> Bodies;
	TArray<float> Fractions;
	TArray<float> ReviewViewYaws;
	TArray<FBodyMetric> CurrentMetrics;
	FCameraMetric CurrentCamera;
	TArray<FShot> Shots;

	TUniquePtr<ElysiumCameraTrack::FPath> TheatrePositionPath;
	TUniquePtr<ElysiumCameraTrack::FPath> TheatreTargetPath;
	TArray<FFadeWindow> TheatreFades;
	FElysiumEntityHandle TheatrePositionOwner;
	FElysiumEntityHandle TheatreTargetOwner;
	float TheatreDuration = 0.0f;
	FVector TheatreSceneOrigin = FVector::ZeroVector;
	FRotator TheatreSceneRotation = FRotator::ZeroRotator;

	TWeakObjectPtr<AActor> StageActor;
	TWeakObjectPtr<UStaticMeshComponent> Floor;
	TWeakObjectPtr<UStaticMeshComponent> Wall;
	TWeakObjectPtr<UPointLightComponent> KeyLight;
	TWeakObjectPtr<UPointLightComponent> FillLight;

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	FVector StageOrigin = FVector(50000.0f, 50000.0f, 5000.0f);
};
