#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ElysiumEntityHandle.h"
// By value: the gym spec, the arena spec and the resolved grid are members, so their own headers
// rather than declarations. `ElysiumGymSpec.h` and `ElysiumArenaSpec.h` are the pure halves and
// ship; only their builders are debug-only.
#include "Debug/ElysiumArenaSpec.h"
#include "ElysiumGymSpec.h"
#include "ElysiumWieldTable.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumAnimSubsystem.h"

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

	// How the stage lights whatever is standing on it.
	//
	// **Studio** is the two-point rig with a cold fill and a neutral ambient. It is deliberately
	// even and deliberately bright, because it exists to make a still comparable to another still.
	//
	// **Interior** is the opposite trade: one tungsten key at under a third the intensity, a dim
	// warm bounce instead of the cold separation fill, and a warm ambient. Skin at 80000 cd and
	// 450 cm reads as flat white with its albedo clipped, so a room-lit body is the only way to
	// judge a skin tone, a garment colour or a normal that has just changed.
	enum class ELabLighting : uint8 { Studio, Interior };

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

		// The stage's lighting mood, and a multiplier over both stage lights on top of it. Studio
		// is the default because the one-shot capture path is measured against it; only the lab
		// ever moves off it.
		ELabLighting Lighting = ELabLighting::Studio;
		float LightScale = 1.0f;

		// The lattice chains and the collider spheres, drawn in the world. A collapsed chain or a
		// thigh sphere in the wrong place is obvious here and invisible in the silhouette.
		bool bDrawLattice = false;
		bool bDrawColliders = false;
		// The model's own skeleton, which is what a collider is supposed to be tracing. A sphere is
		// authored against a bone's *local* frame, so whether it sits on the limb or somewhere off
		// beside it is a question only the two drawn together can answer.
		bool bDrawSkeleton = false;

		// The arena's spawn pads and `intersting_place` anchors, drawn in the room. On by default in
		// arena mode: a pad is where a spawn lands and an anchor is a destination the ambient
		// selector can claim, and neither has any other visible existence — an NPC standing at one
		// looks identical to an NPC standing anywhere else.
		bool bDrawArenaMarkers = true;
	};

	bool IsLab() const { return bLab; }
	// The stage exists and the lab is accepting bodies.
	bool IsLabReady() const { return bLab && Phase == EPhase::Lab; }

	// --- the two lab modes ---------------------------------------------------------------------
	//
	// **Review** is the stage the animation programme verifies on: one body standing at the stage
	// origin, its clip driven by absolute time, framed by the orbit. Nothing about it is the game —
	// the body is a visual the map actor owns, posed by the cast's native host, and the orbit is a
	// camera shot pushed over whatever the player's rig was doing.
	//
	// **Drive** is the opposite in every one of those respects, which is why it is a mode rather than
	// a switch: the real pawn on real gym collision, wearing the real player visual on the real
	// graph, driven by real input and framed by the shipping camera. The lab supplies the floor and
	// then gets out of the way — no camera shot, no control-rotation pin, no clip seek. What is under
	// test is the shipping path, so anything the lab does *to* the body is something the acceptance
	// would not have proven (CCC6).
	//
	// **Arena** is drive mode standing on a different floor, and the floor is the whole difference.
	// The gym is a bracket instrument — ramps, risers, apertures, gaps at `<movement constant> +
	// <offset>` — and it deliberately contributes no navigation, so a character on it cannot path.
	// The arena is a clean square room with one cover solid, `intersting_place` anchors, and a built
	// Recast graph over all of it (`Debug/ElysiumArenaSpec.h`). That graph is the reason it is a
	// separate mode rather than a second gym spec: `AElysiumNpcBody` paths through
	// `AAIController::MoveToLocation`, so without it a spawned cast stands still and reads as broken
	// AI. Everything else — the pawn, the mover, the camera, the visual — is drive mode's, which is
	// why `IsDriving` answers true for both.
	enum class ELabMode : uint8 { Review, Drive, Arena };
	ELabMode LabMode() const { return Mode; }
	// True in both body-driven modes: the shipping path owns the pose, the view and the position, so
	// every "do not write over the body" rule holds identically.
	bool IsDriving() const { return Mode == ELabMode::Drive || Mode == ELabMode::Arena; }
	bool IsArena() const { return Mode == ELabMode::Arena; }

	// --- the arena (the combat playtest room) ----------------------------------------------------

	// The room standing under the driven body, for a panel that draws its pads and anchors. Empty
	// outside arena mode.
	const ElysiumArena::FSpec& ArenaSpec() const { return Arena; }
	// Whether the Recast graph over the arena has finished building. Until it answers true a spawned
	// character can stand but cannot path, so a spawn control gates on it rather than letting the
	// failure show up as an NPC that does nothing.
	bool IsArenaNavigationReady() const;
	// Put the driven body back at the arena's own player start.
	bool ArenaSeatPlayer(FString& OutError);
	// Where a named pad is in world space, feet-anchored. False for a pad the spec does not carry.
	bool ArenaPadOrigin(const FName& Pad, FVector& OutFeetWorld, float& OutYaw) const;
	// Whether the arena's solids are drawn. They are collision either way.
	bool ArenaVisible() const { return bGymMeshes; }
	// The one door into and out of drive mode. The `--drive` flag, the boot path and the window's
	// switch all come through here, so the teardown of whichever mode is leaving cannot be half-done
	// by one caller and whole by another.
	bool LabSetMode(ELabMode NewMode, FString& OutError);
	// Seat the driven body at the feet of a gym lane, the way the movement harness seats a course.
	bool LabSeatOnLane(const FName& Lane, FString& OutError);
	// The gym standing under the driven body: the lane list a picker draws, and which lane the body
	// was last seated on. Empty while reviewing.
	const ElysiumGym::FSpec& DriveGym() const { return GymSpec; }
	const FName& DriveLane() const { return SeatLane; }
	// Whether the gym's solids are drawn. They are collision either way — a box shape needs no mesh —
	// so this is only whether a human can see what they are walking on.
	bool DriveGymVisible() const { return bGymMeshes; }
	bool LabSetGymVisible(bool bVisible, FString& OutError);
	// Stand a body on the stage, replacing whatever is there. An empty clip asks the model's own
	// idle policy for one, so a stem alone is a complete request.
	bool LabSetBody(const FString& Stem, const FString& Clip, FString& OutError);
	// Rebuild the standing body from scratch, discarding the map's cached meshes and clips first.
	// This is the door a re-export needs: the cache is keyed by stem, not by which
	// path built it, so an ordinary restand reuses whatever was resolved the first time and the
	// toggle reads as dead.
	bool LabRestand(FString& OutError);
	// Lay a `_delta` autolayer over the standing body without disturbing it. Re-asking for the
	// layer already running only re-weights it, so the slider drives this every frame.
	bool LabSetLayer(const FString& Clip, float Weight, FString& OutError);
	// Derived form, plain-label fallback, or `nothing` — what the last LabSetLayer actually armed.
	const FString& LabLayerArmed() const { return ReviewLayerArmed; }
	// Steer an armed aim grid, in the pose parameters' own degrees. Drives every frame from a slider,
	// the same way `LabSetGridPosition` steers a base fan, and does not restart the layer under it.
	void LabSetLayerAim(float Yaw, float Pitch);
	// Where the aim was last put, so a preset case can move the sliders with it rather than leaving
	// them reading one thing while the body wears another.
	float LabLayerAimYaw() const { return ReviewLayerAim[0]; }
	float LabLayerAimPitch() const { return ReviewLayerAim[1]; }
	// Whether the armed grid's PITCH follows the view while driving — retail's own shape, since the
	// player selector takes `aim_pitch` from a separate field and pins `aim_yaw` at 0. Off leaves both
	// axes on the sliders, which is how the NPC-side yaw axis is exercised at all.
	bool LabAimFollowsLook() const { return bLayerAimFollowsLook; }
	void LabSetAimFollowsLook(bool bFollow) { bLayerAimFollowsLook = bFollow; }
	void LabClearLayers();

	// --- the wielded weapon (CCC10.2) ----------------------------------------------------------
	//
	// Put an item's wield model in the standing body's hand. The stage carries no
	// `FElysiumCombatCharacter`, so there is no inventory here to equip through and the lab installs
	// directly — but *what* it installs and *how* is the shipping path: the row is resolved out of
	// `/ElysiumBaked/Items/DA_WieldModels` and the attachment is
	// `ElysiumNpcVisual::InstallWieldModel`. Nothing about the geometry on screen is lab-only, which
	// is what makes this the place a placement is judged.
	//
	// Sex is a parameter rather than something read off the body: a mesh stem does not say which of
	// `wieldmodel_m` / `wieldmodel_f` its wearer would take, and the two are different models whose
	// binds agree with their own sex's bodies.
	//
	// The return value is the lookup's own answer, because four of its five outcomes are authored
	// rather than broken and a caller has to be able to say which it got. `OutDetail` is the
	// human-readable form of the same answer in every case.
	EElysiumWieldResult LabSetWield(const FString& Classname, bool bFemale, FString& OutDetail);
	// Empty the standing body's hands. Safe when they already are.
	void LabClearWield();
	// The instant composition readout — which mount the weapon is on, whether the wearer declares
	// it, and where the weapon currently sits relative to the hand. A description, never a
	// verification: for a name-matched mount the weapon and the wearer read the SAME leader-array
	// entry, so comparing those two transforms is an identity that answers 0 whether or not the
	// bone ever animates. Tracking is LabWieldTrackStart's to prove.
	//
	// False, with the reason in `OutReport`, when nothing is held.
	bool LabWieldCheck(FString& OutReport) const;
	// The verification. It reads what is DRAWN — the skinning matrices out of the mesh object's own
	// dynamic data — because every cheaper reading lies about a follower: the game thread's socket
	// answers come through the leader bone map, and freshly rebuilt matrices skip the staleness the
	// proxy actually renders with. Sampled every lab frame over a window of an animated base, it
	// gates three separable claims, and the failure names which one broke, at which sample, by how
	// far:
	//
	// - MAPPING — for a mount the wearer declares, the rendered mount coincides with the wearer's
	//   own animated bone (retail overwrites a matched bone with the wearer's matrix, so the gap is
	//   ~0 or the mechanism is broken; the rule holds through a swing).
	// - TRACKING — the drawn geometry's centre holds its offset in the hand's frame: a held weapon
	//   rides the hand, whatever that offset currently is.
	// - PLACEMENT — the hand actually touches the mesh: the drawn centre stays within the mesh's
	//   own bind radius of the hand, so a weapon orbiting the hand rigidly from a metre away fails
	//   rather than passing as "tracking". No offset is corrected here — a placement that needs one
	//   means the bake is wrong upstream, and this gate is what says so.
	//
	// Two guards keep a pass meaningful: the base must actually move the hand (a paused or
	// near-static base closes as unproven rather than passing vacuously), and the window aborts,
	// named, when the body or the held weapon changes under it.
	//
	// `Seconds` <= 0 samples one loop of the standing clip; `ToleranceCm` <= 0 takes the default.
	// False, with the reason, when the window cannot open. The verdict logs when the window closes
	// and stays on LabWieldTrackVerdict until the next window opens.
	bool LabWieldTrackStart(float Seconds, float ToleranceCm, FString& OutError);
	bool LabWieldTrackRunning() const { return WieldTrack.bRunning; }
	bool LabWieldTrackPassed() const { return WieldTrack.bPassed; }
	const FString& LabWieldTrackVerdict() const { return WieldTrack.Verdict; }
	const FString& LabWield() const { return ReviewWield; }
	bool LabWieldFemale() const { return bReviewWieldFemale; }
	// Every item classname whose row carries geometry for this sex, sorted. Empty when the wield bake
	// has not run — a caller reports that rather than drawing an empty picker.
	static TArray<FString> LabWieldClassnames(bool bFemale);

	// --- CCC10's acceptance, as preset cases ---------------------------------------------------
	//
	// One click stands a whole case: a base to layer over, the right layer armed in the right slot,
	// and the aim steered somewhere the claim is visible. A body resolves ~1,500 clips and the four
	// claims this rung has to answer each need a specific one, so hunting for it by hand is how the
	// acceptance goes unrun.
	//
	// Each case names ORDERED candidate labels and takes the first the standing body actually
	// carries, so it survives a body whose bank set differs rather than failing on one spelling.
	static int32 LayerCaseCount();
	static const TCHAR* LayerCaseName(int32 Index);
	// `OutSummary` names what was armed, the grip the mask came from, and what should be on screen —
	// the claim is on the panel beside the body, so a wrong pose is judged against a stated
	// expectation rather than against recollection.
	bool LabLoadLayerCase(int32 Index, FString& OutSummary, FString& OutError);

private:
	// The body a preset case stands when the session has none: the PC body the rung's acceptance
	// names, falling back to the first baked body that carries one of `Candidates`.
	bool PickLayerCaseBody(const TArray<FString>& Candidates, FString& OutStem);

public:
	// The layer label the lab last accepted, or empty. Cleared by LabClearLayers and by standing a
	// new body, because a layer belongs to the body it was composed onto.
	const FString& LabLayer() const { return ReviewLayer; }
	// Every layer the lab currently has riding, in the order it accepted them. The proxy holds two
	// slots and evicts the weakest for a third, so this is what was ASKED for; it is compared
	// against the binding rather than trusted as what is running.
	const TArray<FString>& LabLayers() const { return ReviewLayers; }
	const FString& LabStem() const { return ReviewStem; }
	const FString& LabClip() const { return ReviewClip; }

	// Stand the body on a label's whole blend grid instead of the one cell the neutral pose
	// parameters pick (ANM3). False, with a reason, for a label that names no grid, for an unbaked
	// body, and for an aim grid — those are a layer's and have no base pose to be.
	bool LabSetGrid(const FString& Label, FString& OutError,
		EElysiumGraphState State = EElysiumGraphState::Walk);
	// Derived grid, plain-label grid, or `nothing` — what the last LabSetGrid actually armed.
	const FString& LabGridArmed() const { return ReviewGridArmed; }
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
	// One frame of drive mode, which is deliberately almost nothing: carry the stage lights with the
	// body and draw the overlays. The pose, the view and the position all belong to the shipping
	// path, and every one of them would be a claim the acceptance no longer proves if the lab wrote
	// it here.
	void TickDrive(float DeltaSeconds);
	// Stand the generated gym up at its own origin, so the pawn has a floor. The stage world freezes
	// its body on arrival precisely because it has none; releasing that freeze belongs to whoever
	// supplied one.
	bool BuildDriveGym(FString& OutError);
	void DestroyDriveGym();
	// Stand the arena room up and request its Recast build. The counterpart of BuildDriveGym, and
	// separate from it for the reason stated at `ELabMode::Arena`: the two floors differ in what
	// they contribute to navigation, which is not a parameter of one builder.
	bool BuildArena(FString& OutError);
	void DestroyArena();
	// Build the player visual and leave it attached where the builder put it — on the pawn. This is
	// the whole of "the stage body is the player body", and the one-shot capture path's
	// detach-and-re-parent is exactly what it must not do.
	bool LabSetDriveBody(const FString& Stem, FString& OutError);
	// Put the held weapon back on whatever is standing now. Every clip change destroys and rebuilds
	// the body, so without this a weapon would survive exactly one pose.
	void ReapplyWield();
	void DrawLabOverlays() const;
	// The arena's pads and anchors, drawn in the room. Separate from DrawLabOverlays because that
	// one is about a BODY — its lattice, its colliders, its skeleton — and these are about the
	// place, which is there whether anything is standing in it or not.
	void DrawArenaOverlays() const;
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
	// Push the view's lighting mood at the stage's own lights. Lab-only, so the capture path keeps
	// the rig every existing contact sheet was shot under.
	void ApplyStageLighting();
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
	// The basis differs per representation (ElysiumSkeletalBasis), so a caller says which body it
	// is placing rather than the theatre case answering for both.
	FRotator BodyRotation(bool bAnimatedProp) const;
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
	FString ReviewLayerArmed;
	// The same, accumulated in acceptance order, because a host declares an ordered pair and the
	// order is the thing being verified.
	TArray<FString> ReviewLayers;
	// The blend grid the body is standing on, invalid when it is on an ordinary clip. Cleared with
	// the body for the same reason the layer is — it belongs to the one it was stood on.
	FElysiumResolvedGrid ReviewGrid;
	FString ReviewGridArmed;
	// Where on that grid's axes the body is being sampled, in the pose parameters' own degrees.
	float ReviewGridAt[2] = { 0.f, 0.f };
	// The same for an armed aim grid's own two axes, which are the LAYER's rather than the base's.
	float ReviewLayerAim[2] = { 0.f, 0.f };
	// The item classname whose wield model the standing body is holding, and the sex whose row was
	// taken. Unlike a layer or a grid, this survives a restand — a weapon belongs to the character
	// rather than to the pose it is in, and watching one track the hand across clips is the point.
	FString ReviewWield;
	bool bReviewWieldFemale = false;
	// One sampling window of the wield tracking check. The weak pointers pin what the window opened
	// over, so a restand or a re-equip mid-window is a named abort rather than a measurement that
	// quietly spans two different weapons.
	struct FWieldTrackProbe
	{
		bool bRunning = false;
		bool bPassed = false;
		float SecondsWanted = 0.0f;
		float SecondsSeen = 0.0f;
		float ToleranceCm = 0.0f;
		int32 Samples = 0;
		FString Classname;
		FName MountBone;
		FName HandBone;
		// Whether the wearer declares the mount, read when the window opens; the mapping gate only
		// exists for a declared mount (an undeclared one has no wearer bone to coincide with).
		bool bWearerDeclares = false;
		// The mesh's own bind-space bounds radius — the placement gate's yardstick for "the hand
		// touches the mesh".
		float BindRadiusCm = 0.0f;
		TWeakObjectPtr<const USkeletalMeshComponent> Body;
		TWeakObjectPtr<const USkeletalMeshComponent> Wield;
		// The drawn centre in the wearer's hand frame at the first sample — the tracking gate's
		// baseline.
		FVector Baseline = FVector::ZeroVector;
		// The component-space hand at the first sample and how far it got from there — the proof
		// the base animated at all, without which a pass claims nothing.
		FVector HandStart = FVector::ZeroVector;
		float HandPeakCm = 0.0f;
		// One worst reading per gate, each with the sample and clip time it landed on.
		float WorstMapCm = 0.0f;
		float WorstMapDeg = 0.0f;
		int32 WorstMapSample = 0;
		float WorstMapTime = 0.0f;
		float WorstDriftCm = 0.0f;
		int32 WorstDriftSample = 0;
		float WorstDriftTime = 0.0f;
		// Placement keeps the worst distance-beyond-radius and the raw distance it came from.
		float WorstPlaceCm = 0.0f;
		float WorstPlaceDistCm = 0.0f;
		int32 WorstPlaceSample = 0;
		float WorstPlaceTime = 0.0f;
		FString Verdict;
	};
	FWieldTrackProbe WieldTrack;
	// One frame of an open window, from TickLab. Closes the window itself when the time is up or
	// the stage changed under it.
	void TickWieldTrack(float DeltaSeconds);
	void CloseWieldTrack(bool bPass, const FString& Verdict);
	bool bLayerAimFollowsLook = true;
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

	// `-GreenRoomDrive` / `-GreenRoomArena`: enter that mode as soon as the stage is ready, rather
	// than standing a review body. Held as a request rather than acted on in the constructor, where
	// there is no map, no pawn and no floor yet. Arena wins if both are given — it is the strictly
	// larger request, and refusing the pair outright would fail a launch over a redundant switch.
	bool bDriveRequested = false;
	bool bArenaRequested = false;
	ELabMode Mode = ELabMode::Review;
	TWeakObjectPtr<AActor> GymActor;
	ElysiumGym::FSpec GymSpec;
	// The arena room and what standing it up produced. Both empty outside arena mode.
	ElysiumArena::FSpec Arena;
	ElysiumArena::FStanding ArenaStanding;
	// The lane the body is seated on. `flat` is the only lane with room to build up to a run and
	// nothing to trip over, which is what the acceptance walks first.
	FName SeatLane = TEXT("flat");
	bool bGymMeshes = true;

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
	FVector TheatreSceneAngles = FVector::ZeroVector;

	TWeakObjectPtr<AActor> StageActor;
	TWeakObjectPtr<UStaticMeshComponent> Floor;
	TWeakObjectPtr<UStaticMeshComponent> Wall;
	TWeakObjectPtr<UPointLightComponent> KeyLight;
	TWeakObjectPtr<UPointLightComponent> FillLight;
	// The stage's own ambient, built only in a stage world (see CreateStage). Null inside a map,
	// where the map's environment is the ambient.
	TWeakObjectPtr<USkyLightComponent> Ambient;
	// The mood that ambient's cubemap currently holds. Tracked because rebuilding one is a texture
	// allocation and a sky recapture, which is an edge-triggered cost rather than a per-frame one
	// like the two point lights beside it.
	ELabLighting AmbientMood = ELabLighting::Studio;

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	FVector StageOrigin = FVector(50000.0f, 50000.0f, 5000.0f);
};
