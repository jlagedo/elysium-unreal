#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ElysiumEntityHandle.h"
// By value: the resolved grid is a member, so the subsystem's own header rather than a declaration.
#include "Visual/ElysiumNpcAnimSubsystem.h"

class AActor;
class AElysiumMapActor;
class UElysiumMapSubsystem;
class UPointLightComponent;
class USkeletalMeshComponent;
class USkyLightComponent;
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
		// The model's own skeleton, which is what a collider is supposed to be tracing. A sphere is
		// authored against a bone's *local* frame, so whether it sits on the limb or somewhere off
		// beside it is a question only the two drawn together can answer.
		bool bDrawSkeleton = false;
	};

	bool IsLab() const { return bLab; }
	// The stage exists and the lab is accepting bodies.
	bool IsLabReady() const { return bLab && Phase == EPhase::Lab; }
	// Stand a body on the stage, replacing whatever is there. An empty clip asks the model's own
	// idle policy for one, so a stem alone is a complete request.
	bool LabSetBody(const FString& Stem, const FString& Clip, FString& OutError);
	// Rebuild the standing body from scratch, discarding the map's cached meshes and clips first.
	// This is the door `elysium.BakedCharacters` needs: the cache is keyed by stem, not by which
	// path built it, so an ordinary restand reuses whatever was resolved the first time and the
	// toggle reads as dead. Same for `elysium.Cloth`.
	bool LabRestand(FString& OutError);
	// Lay a `_delta` autolayer over the standing body without disturbing it. Re-asking for the
	// layer already running only re-weights it, so the slider drives this every frame.
	bool LabSetLayer(const FString& Clip, float Weight, FString& OutError);
	void LabClearLayers();
	// The layer label the lab last accepted, or empty. Cleared by LabClearLayers and by standing a
	// new body, because a layer belongs to the body it was composed onto.
	const FString& LabLayer() const { return ReviewLayer; }
	const FString& LabStem() const { return ReviewStem; }
	const FString& LabClip() const { return ReviewClip; }

	// Stand the body on a label's whole blend grid instead of the one cell the neutral pose
	// parameters pick (ANM3). False, with a reason, for a label that names no grid, for an unbaked
	// body, and for an aim grid — those are a layer's and have no base pose to be.
	bool LabSetGrid(const FString& Label, FString& OutError);
	// Move the sample point, in the pose parameters' own degrees. Drives every frame from a slider;
	// it steers the blend without restarting the animations under it.
	void LabSetGridPosition(float Axis0, float Axis1);
	void LabClearGrid();
	// What is standing, when a grid is. `Space` is null whenever the body is on an ordinary clip,
	// which is what a caller tests to know which controls to draw.
	const FElysiumResolvedGrid& LabGrid() const { return ReviewGrid; }
	float LabGridAxis(int32 Axis) const { return ReviewGridAt[FMath::Clamp(Axis, 0, 1)]; }
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
	// The lab's orbit camera around a box, published as a camera shot. Separate from UpdateStage
	// because the stage has to be *looked at* before the first body arrives, not merely built: in a
	// stage world the level is otherwise empty and the pawn sits at the origin, half a world away.
	void FrameLabCamera(const FBox& Bounds);
	// What the lab frames while no body is standing — a person-sized volume at the stage's centre, so
	// an empty stage is framed the way the first body on it will be.
	FBox EmptyStageBounds() const;
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
	// The autolayer riding over the stage body, or empty. Lab-only: the one-shot capture path
	// composes nothing.
	FString ReviewLayer;
	// The blend grid the body is standing on, invalid when it is on an ordinary clip. Cleared with
	// the body for the same reason the layer is — it belongs to the one it was stood on.
	FElysiumResolvedGrid ReviewGrid;
	// Where on that grid's axes the body is being sampled, in the pose parameters' own degrees.
	float ReviewGridAt[2] = { 0.f, 0.f };
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
	// The stage's own ambient, built only in a stage world (see CreateStage). Null inside a map,
	// where the map's environment is the ambient.
	TWeakObjectPtr<USkyLightComponent> Ambient;

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	FVector StageOrigin = FVector(50000.0f, 50000.0f, 5000.0f);
};
