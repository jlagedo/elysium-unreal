// `camera_cinematic` — the director entity, the adoption slot, the five `CamMode` arms, the 24 Hz
// goal publish and the immobilize pair (`docs/project/camera_scripted.md` §SC4, as corrected by
// `$ELYSIUM_WORK_ROOT/_camera_recovery/rc_group_a.md` RC2/RC3/RC4/RC12).
//
// Everything here runs on a bare `FElysiumEntityWorld` + `FElysiumRecordingServices`: no UWorld, no
// pawn, no renderer. The shot files are seeded into the parse cache with `ElysiumCameraShots::Install`
// so the cases drive the real solver with no export mounted. The one content-tier case at the bottom
// loads `sp_tutorial_1`'s real `.ents` through `FElysiumMapSlice` and abstains without the corpus.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumUserCmd.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraAnimated.h"
#include "Substrate/ElysiumCameraCinematic.h"
#include "ElysiumContentPaths.h"
#include "Tests/ElysiumMapSlice.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumCameraCinematicTests
{
static constexpr EAutomationTestFlags GFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// `sp_tutorial_1`'s `feedcamera` names `vdata/CameraShots/LookAtTarget_Snap.txt`, whose shape is one
// `End` anchor on a `Named` entity and one `Target Point1` on another, snapping on the shot change.
// Built by hand so the case runs with no export root; the content-tier case at the bottom is what
// proves the authored file against it.
static FElysiumCameraShotDef LookAtTargetSnap()
{
	FElysiumCameraShotDef Def;
	Def.Name = TEXT("LookAtTarget_Snap");
	Def.End.bPresent = true;
	Def.End.Position = EElysiumShotPosition::Named;
	Def.End.AttachPos = TEXT("Origin");
	Def.End.AttachPoint = EElysiumShotAttachPos::Origin;
	Def.End.Attach = EElysiumShotAttach::Follow;
	Def.Target1.bPresent = true;
	Def.Target1.Position = EElysiumShotPosition::Named;
	Def.Target1.AttachPos = TEXT("Origin");
	Def.Target1.AttachPoint = EElysiumShotAttachPos::Origin;
	Def.Target1.Attach = EElysiumShotAttach::Follow;
	Def.TargetPointCount = 1;
	Def.bTargetPoint1Flagged = true;
	Def.Constraints.bSnapOnShotChange = true;
	Def.Constraints.FieldOfView = 60.0f;
	return Def;
}

// A `Start`-only shot: the placement arm that reads anchor 0 and derives its angles from the look-at.
static FElysiumCameraShotDef StartOnly(const TCHAR* Name)
{
	FElysiumCameraShotDef Def;
	Def.Name = Name;
	Def.Start.bPresent = true;
	Def.Start.Position = EElysiumShotPosition::Named;
	Def.Start.Attach = EElysiumShotAttach::Follow;
	Def.Target1.bPresent = true;
	Def.Target1.Position = EElysiumShotPosition::Named;
	Def.Target1.Attach = EElysiumShotAttach::Follow;
	Def.TargetPointCount = 1;
	Def.bTargetPoint1Flagged = true;
	return Def;
}

// An `End`-only shot with no `Start`: RC3's arm B, the one that consumes the saved placement.
static FElysiumCameraShotDef EndOnly(const TCHAR* Name)
{
	FElysiumCameraShotDef Def;
	Def.Name = Name;
	Def.End.bPresent = true;
	Def.End.Position = EElysiumShotPosition::Named;
	Def.End.Attach = EElysiumShotAttach::Follow;
	return Def;
}

// `special-case.txt`'s two engine-facing blocks, as the two unshipped `CamMode` factories name them.
static void InstallSpecialCase()
{
	TArray<FElysiumCameraShotDef> Blocks;
	FElysiumCameraShotDef Follow;
	Follow.Name = TEXT("Follow");
	Follow.Start.bPresent = true;
	Follow.Start.Position = EElysiumShotPosition::Named;
	Follow.Start.AttachPos = TEXT("Center");
	Follow.Start.AttachPoint = EElysiumShotAttachPos::Center;
	Follow.Start.Attach = EElysiumShotAttach::None;   // the one latching anchor in the whole corpus
	Blocks.Add(Follow);

	FElysiumCameraShotDef Animated;
	Animated.Name = TEXT("Animated");
	Animated.Start.bPresent = true;
	Animated.Start.Position = EElysiumShotPosition::Named;
	Animated.Start.AttachPos = TEXT("Bone: cam_bone");
	Animated.Start.AttachPoint = EElysiumShotAttachPos::Bone;
	Animated.Start.AttachPointName = TEXT("cam_bone");
	Animated.Start.Attach = EElysiumShotAttach::Follow;
	Animated.Constraints.FieldOfView = 42.0f;
	Blocks.Add(Animated);

	ElysiumCameraShots::InstallNamed(TEXT("special-case"), Blocks);
}

// One point entity with a targetname the anchors can be bound to. `info_target` has no leaf class,
// so it lands as an inert record — which still carries a handle, a name and an origin, and is all a
// `Named` anchor reads.
static FElysiumEntityDef Point(const TCHAR* Name, const FVector& Origin)
{
	FElysiumEntityDef Def;
	Def.Classname = TEXT("info_target");
	Def.TargetName = Name;
	Def.Origin = Origin;
	return Def;
}

static FElysiumEntityDef Director(const TCHAR* Name, const TCHAR* ShotName, const TCHAR* SpawnFlags)
{
	FElysiumEntityDef Def;
	Def.Classname = TEXT("camera_cinematic");
	Def.TargetName = Name;
	Def.Origin = FVector::ZeroVector;
	Def.Keys.Add(TEXT("shotname"), ShotName);
	Def.Keys.Add(TEXT("spawnflags"), SpawnFlags);
	return Def;
}

static FElysiumCameraCinematic* CineOf(FElysiumEntityWorld& World, const FElysiumEntityHandle& Handle)
{
	FElysiumEntity* Ent = World.Resolve(Handle);
	return Ent ? Ent->AsCameraCinematic() : nullptr;
}

static FElysiumCameraCinematic* AdoptedCamera(FElysiumEntityWorld& World)
{
	return CineOf(World, World.CineCameraEntity());
}
}

using namespace ElysiumCameraCinematicTests;

// ------------------------------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraCinematicTest,
	"Elysium.Substrate.CameraCinematic", GFlags)
bool FElysiumCameraCinematicTest::RunTest(const FString&)
{
	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

	// --- 1. `sp_tutorial_1`'s `feedcamera`, verbatim ---------------------------------------------
	//
	//   shotname     "vdata/CameraShots/LookAtTarget_Snap.txt"   (the `.txt` form, 43 of 51 do)
	//   startent     "feedcamera"      endent  "feedcamera"      (itself, both)
	//   target1      "tutwareportal03"
	//   point_player "0"                                          (the key -- and it is DEAD)
	//   spawnflags   "3"                                          (freeze | drawplayer)
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("LookAtTarget_Snap"), LookAtTargetSnap());
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.PlayerLocation = FVector(0.0f, 0.0f, 100.0f);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_feedcamera__");
		FElysiumEntityDef Cam = Director(TEXT("feedcamera"),
			TEXT("vdata/CameraShots/LookAtTarget_Snap.txt"), TEXT("3"));
		Cam.Origin = FVector(500.0f, 200.0f, 150.0f);
		Cam.Keys.Add(TEXT("startent"), TEXT("feedcamera"));
		Cam.Keys.Add(TEXT("endent"), TEXT("feedcamera"));
		Cam.Keys.Add(TEXT("target1"), TEXT("tutwareportal03"));
		Cam.Keys.Add(TEXT("point_player"), TEXT("0"));
		Defs.Defs.Add(MoveTemp(Cam));
		Defs.Defs.Add(Point(TEXT("tutwareportal03"), FVector(900.0f, 0.0f, 120.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* DirectorEnt = World.FindByName(TEXT("feedcamera"));
		FElysiumCameraCinematic* DirectorCam = DirectorEnt ? DirectorEnt->AsCameraCinematic() : nullptr;
		if (!TestNotNull(TEXT("camera_cinematic resolves as its own leaf, not a stub"), DirectorCam))
		{
			return false;
		}
		const FElysiumEntityHandle DirectorHandle = DirectorCam->Handle;

		// `Spawn` `0x1006d9a0` is `if (spawnflags & 2) m_bDrawPlayer = 1`, and nothing else.
		// **27 of 51 shipped directors set bit 2**, `feedcamera` among them (RC2.2) — so the plan's
		// "m_bDrawPlayer defaults to 0 on every shipped director" is inverted here.
		TestTrue(TEXT("spawnflags 3 carries the draw-player bit"), DirectorCam->bDrawPlayerBody);
		// Bit 1 is `CCameraAnimated`'s freeze bit, authored on 50 of 51 directors and dead on this
		// class: `StartShot` immobilizes unconditionally.
		TestEqual(TEXT("spawnflags 3 raises the freeze bit"),
			DirectorCam->SpawnFlags & ElysiumCineCam::SF_FreezePlayer,
			ElysiumCineCam::SF_FreezePlayer);
		TestFalse(TEXT("spawnflags 3 does not carry the disposable bit"), DirectorCam->bDisposable);
		// The `point_player` keyvalue parses (so the census can read it) and is never copied.
		TestFalse(TEXT("point_player 0 parses as authored"), DirectorCam->bPointPlayerKey);
		TestFalse(TEXT("a director is idle until StartShot"), DirectorCam->IsActive());

		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		if (!TestNotNull(TEXT("the fixture has a player"), PlayerEnt))
		{
			return false;
		}
		TestTrue(TEXT("the player is mobile before the shot"), PlayerEnt->IsMobile());

		World.AcceptInput(TEXT("feedcamera"), FName(TEXT("StartShot")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());

		// The director/runtime split: the map entity holds the keys, a SECOND `camera_cinematic`
		// becomes the view.
		FElysiumCameraCinematic* Runtime = AdoptedCamera(World);
		if (!TestNotNull(TEXT("StartShot adopted a runtime camera"), Runtime))
		{
			return false;
		}
		TestTrue(TEXT("the adopted camera is not the director"), Runtime->Handle != DirectorHandle);
		TestTrue(TEXT("the runtime camera is disposable"), Runtime->bDisposable);
		TestTrue(TEXT("the world's cine slot is live"), World.HasScriptedCamera());
		TestEqual(TEXT("the runtime camera runs CamMode 1"), Runtime->CamMode,
			static_cast<int32>(EElysiumCineCamMode::NamedShot));
		TestTrue(TEXT("the .txt shotname normalized to the shot"), Runtime->bShotLoaded);
		TestEqual(TEXT("and it is LookAtTarget_Snap"), Runtime->ShotDef.Name,
			FString(TEXT("LookAtTarget_Snap")));

		// The four `FindEntityByName` lookups: `startent`/`endent` are the director itself and
		// `target1` is the portal, and they OVERRIDE the shot file's `Named` anchors.
		const FElysiumEntity* Portal = World.FindByName(TEXT("tutwareportal03"));
		if (TestNotNull(TEXT("tutwareportal03 is in the fixture"), Portal))
		{
			TestTrue(TEXT("anchor 0 (startent) is the director"),
				Runtime->Bindings.Anchors[0].Entity == DirectorHandle);
			TestTrue(TEXT("anchor 1 (endent) is the director"),
				Runtime->Bindings.Anchors[1].Entity == DirectorHandle);
			TestTrue(TEXT("anchor 2 (target1) is tutwareportal03"),
				Runtime->Bindings.Anchors[2].Entity == Portal->Handle);
			TestFalse(TEXT("anchor 3 (target2, unauthored) is unbound"),
				Runtime->Bindings.Anchors[3].Entity.IsSet());
		}

		// `FUN_10070780` step 6 copies `m_bDrawPlayer` — the ONLY field the director hands over.
		TestTrue(TEXT("the runtime camera inherited m_bDrawPlayer"), Runtime->bDrawPlayerBody);
		TestTrue(TEXT("and the published shot carries it as bDrawPlayerBody"),
			Services.LastCameraShot.Presentation.bDrawPlayerBody);
		TestTrue(TEXT("the published shot is a cine shot"), Services.LastCameraShot.bCine);
		TestTrue(TEXT("and CamMode 1 makes it tracked"), Services.LastCameraShot.bTracked);
		TestEqual(TEXT("a cine shot has no blend at all (M1)"),
			Services.LastCameraShot.BlendSeconds, 0.0f);

		// **The `point_player` keyvalue is never copied**: the runtime camera's own byte is the
		// constructor's 1, so the shot turns its subject even though the map authored 0.
		// `docs/vtmb/retail-defects.md` §7.
		TestTrue(TEXT("point_player 0 notwithstanding, the runtime camera forces the look"),
			Runtime->bForcePlayerLook);

		// The origin selector: the `End` handle is live, so the shot start set it to 0 (drive from
		// `End`).
		TestTrue(TEXT("a live End anchor selects origin source 0 (EndAnchor)"),
			Runtime->OriginSelector == EElysiumShotOriginSelector::EndAnchor);
		// `LookAtTarget_Snap` is `End` with no `Start`, so RC3's arm B places the camera at the
		// **player's eye** (nothing has been saved on a camera that was created this frame) — and
		// the shot start publishes that placement, not the anchor. The dolly toward the `End` point
		// is the 24 Hz think's, below.
		TestTrue(TEXT("an End-only shot start places at the player's eye"),
			Runtime->PlacementOrigin.Equals(PlayerEnt->EyePosition(), 0.1f));
		TestTrue(TEXT("and the first published goal is that placement"),
			Services.LastCameraShot.Origin.Equals(PlayerEnt->EyePosition(), 0.1f));
		TestTrue(TEXT("with the look-at already at Point1"),
			Services.LastCameraShot.bUseLookAt
			&& Services.LastCameraShot.LookAt.Equals(FVector(900.0f, 0.0f, 120.0f), 0.1f));

		// One 24 Hz think later the origin is driven from the `End` anchor — the director itself,
		// where the map put it.
		World.Tick(0.001);
		World.Tick(0.06);
		TestTrue(TEXT("the think drives the origin from the End anchor"),
			Services.LastCameraShot.Origin.Equals(FVector(500.0f, 200.0f, 150.0f), 0.1f));

		// --- 2. Immobilize: the four retail consumers -------------------------------------------
		// `FUN_1015ef40(player)` -> `m_bIsImmobilized`, read by `SetupMove` `0x10186120`,
		// `CheckJumpButton` `0x101226b0`, `Duck` `0x10126fd0` and the two `ItemPostFrame` bodies
		// (`0x10253ea0` / `0x103eaec0`). All five read ONE latch, which is `IsMobile()` here.
		TestFalse(TEXT("StartShot immobilized the player"), PlayerEnt->IsMobile());
		{
			// Move, jump and duck: `AElysiumPlayerController::ProcessPlayerInput` answers
			// `!IsMobile()` with `ClearMovement()`, which is the port's `SetupMove` button mask.
			FElysiumUserCmd Cmd;
			Cmd.Move = FVector2D(1.0f, 1.0f);
			Cmd.Buttons = static_cast<uint64>(EElysiumButton::Forward)
				| static_cast<uint64>(EElysiumButton::Jump)
				| static_cast<uint64>(EElysiumButton::Duck)
				| static_cast<uint64>(EElysiumButton::Attack);
			Cmd.ClearMovement();
			TestTrue(TEXT("the immobilize gate zeroes the wish move"), Cmd.Move.IsNearlyZero());
			TestEqual(TEXT("... and drops forward"),
				Cmd.Buttons & static_cast<uint64>(EElysiumButton::Forward), 0ull);
			TestEqual(TEXT("... and jump"),
				Cmd.Buttons & static_cast<uint64>(EElysiumButton::Jump), 0ull);
			TestEqual(TEXT("... and duck"),
				Cmd.Buttons & static_cast<uint64>(EElysiumButton::Duck), 0ull);
			// The weapon consumer is `FElysiumEntityWorld::UpdatePlayerWeaponFrame`, which takes the
			// same `!IsMobile()` early-out (`ElysiumEntityWorldInteraction.cpp`) — the attack bit is
			// deliberately still set on the command, because retail does not mask it either: it is
			// `ItemPostFrame` that refuses to act on it.
			TestTrue(TEXT("attack survives the mask; ItemPostFrame is what refuses it"),
				(Cmd.Buttons & static_cast<uint64>(EElysiumButton::Attack)) != 0);
		}

		// --- 3. A director survives its own StartShot -------------------------------------------
		// `FUN_1017cef0` removes the outgoing camera only when it carries `+0x204 & 0x4`; a
		// map-placed director never does.
		FElysiumEntity* StillThere = World.Resolve(DirectorHandle);
		TestTrue(TEXT("the map-placed director survives its own StartShot"),
			StillThere != nullptr && !StillThere->IsDead());

		// --- 4. The 24 Hz goal publish (M2) -----------------------------------------------------
		// `_DAT_1044eb04 = 0.04165999963879585`. Driving the think at 120 Hz publishes a new goal
		// every FIFTH frame (1/24 / 1/120 = 5) and holds it in between. (§SC4's "every third frame"
		// is an arithmetic slip in the plan; 5 is what 1/24 over 1/120 is, and the constant is the
		// contract.)
		{
			// 15 frames of 1/120 s is 0.125 s, which is exactly three 1/24 s intervals however the
			// accumulator's phase started.
			double Now = 0.06;
			int32 Published = 0;
			for (int32 Frame = 0; Frame < 15; ++Frame)
			{
				const int32 Was = Services.Count(TEXT("UpdateCameraShotValue"));
				Now += 1.0 / 120.0;
				World.Tick(Now);
				Published += Services.Count(TEXT("UpdateCameraShotValue")) > Was ? 1 : 0;
			}
			TestEqual(TEXT("15 frames at 120 Hz publish 3 goals (one per 1/24 s)"), Published, 3);

			// A 0.5 s delta publishes **exactly once**, with the remainder carried — retail runs at
			// most one think per frame however long the frame was.
			const int32 Was = Services.Count(TEXT("UpdateCameraShotValue"));
			Now += 0.5;
			World.Tick(Now);
			TestEqual(TEXT("a 0.5 s frame publishes exactly one goal"),
				Services.Count(TEXT("UpdateCameraShotValue")) - Was, 1);
			TestTrue(TEXT("and carries the remainder"),
				Runtime->ThinkAccumulator > 0.4f);
			// The carried remainder then fires the very next tick, without waiting 1/24 s again.
			const int32 Was2 = Services.Count(TEXT("UpdateCameraShotValue"));
			Now += 1.0 / 120.0;
			World.Tick(Now);
			TestEqual(TEXT("the carried remainder publishes on the next tick"),
				Services.Count(TEXT("UpdateCameraShotValue")) - Was2, 1);
		}

		// --- 5. `point_player` turns the SUBJECT, never the camera ------------------------------
		// `FUN_10178590(subject, m_vecCamTarget)` -> `FUN_10178550`: the pending eye-angle snap.
		TestTrue(TEXT("the shot raised the subject's pending eye-angle snap"),
			PlayerEnt->HasPendingEyeAngleSnap());
		TestTrue(TEXT("and it aims at the shot's look-at, not at the camera"),
			PlayerEnt->PendingEyeLookPoint.Equals(FVector(900.0f, 0.0f, 120.0f), 0.1f));
		TestTrue(TEXT("the player's view was snapped through the embodiment seam"),
			Services.Saw(TEXT("SnapPlayerViewTo")));
		// The camera itself is untouched by the arm: the placement the shot start computed still
		// stands, because the mode-1 think never writes the entity's transform.
		TestTrue(TEXT("point_player does not move the camera"),
			Runtime->PlacementOrigin.Equals(PlayerEnt->EyePosition(), 0.1f));

		// --- 6. `EndShot` is a same-frame cut ---------------------------------------------------
		const FElysiumEntityHandle RuntimeHandle = Runtime->Handle;
		PlayerEnt->AddViewFlags(EElysiumViewFlags::MoveAnglesFromEntity
			| EElysiumViewFlags::ViewAngleLock);
		World.AcceptInput(TEXT("feedcamera"), FName(TEXT("EndShot")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());

		TestFalse(TEXT("EndShot cleared the cine slot the same frame"), World.HasScriptedCamera());
		TestFalse(TEXT("... and the slot's entity"), World.CineCameraEntity().IsSet());
		{
			const FElysiumEntity* Dead = World.Resolve(RuntimeHandle);
			TestTrue(TEXT("... and destroyed the disposable runtime camera"),
				Dead == nullptr || Dead->IsDead());
		}
		TestTrue(TEXT("the release is a cut: PopCameraShot with a zero blend"),
			Services.Saw(TEXT("PopCameraShot")));
		TestTrue(TEXT("the same frame mobilizes the player"), PlayerEnt->IsMobile());
		// `FUN_101815b0(player, 1)` and `(player, 8)`: **EndShot clears locks StartShot never took**.
		TestFalse(TEXT("the same frame clears the pose lock"),
			PlayerEnt->HasViewFlags(EElysiumViewFlags::MoveAnglesFromEntity));
		TestFalse(TEXT("... and the view-angle lock"),
			PlayerEnt->HasViewFlags(EElysiumViewFlags::ViewAngleLock));
		// The HUD restore rides the same frame because the shot that hid it is simply gone: no
		// residual weight, nothing left on the channel to read `bShowHud` from.
		TestFalse(TEXT("the director is idle again"), DirectorCam->IsActive());
		TestTrue(TEXT("a non-disposable director is not removed by its own EndShot"),
			World.Resolve(DirectorHandle) != nullptr && !World.Resolve(DirectorHandle)->IsDead());
	}

	// --- 7. The re-shot branch with an unknown name leaves the camera IDLE ------------------------
	// `FUN_10070780`'s "a camera is already adopted" branch **ignores `SetShot`'s return value**, so
	// a bad name leaves `CamMode 0` / no shot rather than falling back — the asymmetry with
	// `SetCamera`, which falls back to `DialogDefault`.
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("LookAtTarget_Snap"), LookAtTargetSnap());
		InstallSpecialCase();
		AddExpectedError(TEXT("camera shot 'no_such_shot' not found"),
			EAutomationExpectedErrorFlags::Contains, 0);

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_reshot__");
		FElysiumEntityDef First = Director(TEXT("cam_good"), TEXT("LookAtTarget_Snap"), TEXT("0"));
		First.Origin = FVector(100.0f, 0.0f, 0.0f);
		First.Keys.Add(TEXT("endent"), TEXT("cam_good"));
		First.Keys.Add(TEXT("target1"), TEXT("focus"));
		Defs.Defs.Add(MoveTemp(First));
		Defs.Defs.Add(Director(TEXT("cam_bad"), TEXT("no_such_shot"), TEXT("0")));
		Defs.Defs.Add(Point(TEXT("focus"), FVector(400.0f, 0.0f, 0.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		World.AcceptInput(TEXT("cam_good"), FName(TEXT("StartShot")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		FElysiumCameraCinematic* Runtime = AdoptedCamera(World);
		if (!TestNotNull(TEXT("the good shot adopted a camera"), Runtime))
		{
			return false;
		}
		const FElysiumEntityHandle RuntimeHandle = Runtime->Handle;

		World.AcceptInput(TEXT("cam_bad"), FName(TEXT("StartShot")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		FElysiumCameraCinematic* Still = CineOf(World, RuntimeHandle);
		if (TestNotNull(TEXT("the re-shot reused the SAME camera entity"), Still))
		{
			TestEqual(TEXT("a bad shot name leaves the camera idle, not fallen back"),
				Still->CamMode, static_cast<int32>(EElysiumCineCamMode::Idle));
			TestFalse(TEXT("... with no shot loaded"), Still->bShotLoaded);
		}
		TestTrue(TEXT("the camera entity itself survives an idle re-shot"),
			World.CineCameraEntity() == RuntimeHandle);
		// `ShouldTransmit` refuses an idle camera to every client, so the view falls back to the
		// player's eye even though the entity is still what `m_iCameraOverrideIdx` names.
		TestFalse(TEXT("an idle camera puts nothing on the channel"), World.HasScriptedCamera());
		// `1006ebaf` re-stamps `m_nClientResetFrame` on **every** shot start, including one that
		// re-shots the camera already adopted — the id does not change, so the re-stamp is what arms
		// the consumer's shot start again.
		TestTrue(TEXT("a re-shot re-stamps the reset frame on the same handle"),
			Services.Saw(TEXT("RestartCameraShot")));
	}

	// --- 8. The adoption slot's destroy rule, and the terminal on it (M8) ------------------------
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("LookAtTarget_Snap"), LookAtTargetSnap());
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_slot__");
		FElysiumEntityDef Cam = Director(TEXT("cam_a"), TEXT("LookAtTarget_Snap"), TEXT("0"));
		Cam.Origin = FVector(100.0f, 0.0f, 0.0f);
		Cam.Keys.Add(TEXT("endent"), TEXT("cam_a"));
		Cam.Keys.Add(TEXT("target1"), TEXT("focus"));
		Defs.Defs.Add(MoveTemp(Cam));
		Defs.Defs.Add(Point(TEXT("focus"), FVector(400.0f, 0.0f, 0.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		World.AcceptInput(TEXT("cam_a"), FName(TEXT("StartShot")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		FElysiumCameraCinematic* Runtime = AdoptedCamera(World);
		if (!TestNotNull(TEXT("cam_a adopted a runtime camera"), Runtime))
		{
			return false;
		}
		const FElysiumEntityHandle RuntimeHandle = Runtime->Handle;

		// A terminal opening over a live cine shot is `FUN_10070470` then `FUN_1017cef0` — the SAME
		// slot (M8), so the disposable camera the director created is destroyed. The terminal's own
		// adopter owns no entity, which is why the slot takes a bare shot handle here.
		const int32 TerminalShot = Services.PushCameraShotNamed(TEXT("special-case"),
			TEXT("Hacking"), FElysiumEntityHandle(), EElysiumShotExposure::Clamped);
		World.SetCineCamera(FElysiumEntityHandle::Invalid(), TerminalShot, /*bDisposable*/ true,
			TEXT("special-case"));
		{
			const FElysiumEntity* Dead = World.Resolve(RuntimeHandle);
			TestTrue(TEXT("a terminal opened over a live cine shot destroys the disposable camera"),
				Dead == nullptr || Dead->IsDead());
		}
		TestTrue(TEXT("and the slot is the terminal's"), World.HasScriptedCamera());
		TestFalse(TEXT("which owns no entity"), World.CineCameraEntity().IsSet());

		// The closer drops to the **player view**, never to a shot that was live before it opened.
		World.ClearScriptedCamera();
		TestFalse(TEXT("closing the terminal drops to the player view"), World.HasScriptedCamera());
	}

	// --- 9. `SetShot`'s `.txt` -> basename normalization ------------------------------------------
	// `if (_strstr(name, ".txt")) Q_FileBase(name, buf, 0x20)`. Load-bearing: `shotname` is authored
	// as `vdata/CameraShots/X.txt` 43 times and bare 8 times across the 51 shipped directors.
	{
		TestEqual(TEXT("the .txt path form normalizes"),
			ElysiumCineCam::NormalizeShotName(TEXT("vdata/CameraShots/Jack.txt")),
			FString(TEXT("Jack")));
		TestEqual(TEXT("the backslash + upper-case form normalizes to the same"),
			ElysiumCineCam::NormalizeShotName(TEXT("CameraShots\\Jack.TXT")),
			FString(TEXT("Jack")));
		TestEqual(TEXT("a bare name is passed verbatim"),
			ElysiumCineCam::NormalizeShotName(TEXT("Jack")), FString(TEXT("Jack")));

		ElysiumCameraShots::FlushCache();
		FElysiumCameraShotDef Jack = LookAtTargetSnap();
		Jack.Name = TEXT("Jack");
		ElysiumCameraShots::Install(TEXT("Jack"), Jack);
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_normalize__");
		Defs.Defs.Add(Point(TEXT("focus"), FVector(400.0f, 0.0f, 0.0f)));
		World.Load(MoveTemp(Defs));
		const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntityHandle Anchors[FElysiumShotBindings::Num];
		Anchors[0] = PlayerHandle;
		Anchors[1] = PlayerHandle;
		for (const TCHAR* Spelling : { TEXT("vdata/CameraShots/Jack.txt"),
			TEXT("CameraShots\\Jack.TXT"), TEXT("Jack") })
		{
			const FElysiumEntityHandle Created = FElysiumCameraCinematic::CreateRuntimeCamera(
				World, Spelling, static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, Created);
			if (TestNotNull(FString::Printf(TEXT("'%s' resolves a camera"), Spelling), Cine))
			{
				TestEqual(FString::Printf(TEXT("'%s' resolves to the one shot"), Spelling),
					Cine->ShotDef.Name, FString(TEXT("Jack")));
			}
		}
	}

	// --- 10. The five `CamMode` arms, and the expiry --------------------------------------------
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("TrackedShot"), StartOnly(TEXT("TrackedShot")));
		InstallSpecialCase();
		// `Follow` and `Animated` are blocks INSIDE `special-case.txt`, so the "the file of that
		// name first" probe misses before the sibling lookup finds them — exactly as retail's
		// single shot table would not have to.
		AddExpectedError(TEXT("camera shot 'follow' not found"),
			EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("camera shot 'animated' not found"),
			EAutomationExpectedErrorFlags::Contains, 0);

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_modes__");
		Defs.Defs.Add(Point(TEXT("anchor"), FVector(300.0f, 0.0f, 50.0f)));
		Defs.Defs.Add(Point(TEXT("focus"), FVector(700.0f, 0.0f, 50.0f)));
		Defs.Defs.Add(Point(TEXT("animanchor"), FVector(-200.0f, 0.0f, 50.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		const FElysiumEntity* Anchor = World.FindByName(TEXT("anchor"));
		const FElysiumEntity* Focus = World.FindByName(TEXT("focus"));
		if (!TestNotNull(TEXT("the mode fixture has an anchor"), Anchor)
			|| !TestNotNull(TEXT("... and a focus"), Focus))
		{
			return false;
		}
		FElysiumEntityHandle Anchors[FElysiumShotBindings::Num];
		Anchors[0] = Anchor->Handle;
		Anchors[2] = Focus->Handle;

		double Now = 0.0;
		auto Step = [&World, &Now](double Seconds)
		{
			// One 1/24 s think per call, driven by the accumulator the entity keeps.
			Now += Seconds;
			World.Tick(Now);
		};

		// Mode 1 — tracks, and **never expires on its own**: the only writers of `+0x55c` are the
		// mode-4 think and the mode-3 arm, and the constructor seeds `-1.0`.
		{
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("TrackedShot"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("mode 1 creates"), Cine))
			{
				TestEqual(TEXT("a fresh camera never expires"), Cine->ExpiryTime, -1.0f);
				const int32 Was = Services.Count(TEXT("UpdateCameraShotValue"));
				Step(0.001);   // seed the stamp
				Step(0.1);     // one due tick
				TestTrue(TEXT("mode 1 publishes a goal"),
					Services.Count(TEXT("UpdateCameraShotValue")) > Was);
				const FElysiumEntity* Live = World.Resolve(H);
				TestTrue(TEXT("a mode-1 shot never expires"),
					Live != nullptr && !Live->IsDead());
				Cine->Kill();
			}
		}

		// Mode 2 (`OnRails`) — `0x1006fde0` is an **empty function** on server and client. It
		// publishes nothing and never expires; the client copies the last replicated pose through.
		{
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("TrackedShot"), static_cast<int32>(EElysiumCineCamMode::OnRails), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("mode 2 creates"), Cine))
			{
				const int32 Was = Services.Count(TEXT("UpdateCameraShotValue"));
				Step(0.001);
				Step(0.1);
				Step(0.1);
				TestEqual(TEXT("mode 2 publishes nothing"),
					Services.Count(TEXT("UpdateCameraShotValue")), Was);
				const FElysiumEntity* Live = World.Resolve(H);
				TestTrue(TEXT("mode 2 never expires"), Live != nullptr && !Live->IsDead());
				Cine->Kill();
			}
		}

		// Mode 3 (`FollowEntity`) — origin = anchor 0's world centre, angles = its abs angles, and
		// **no target, no FOV**. Its factory `FUN_100705d0` stores the handle raw.
		{
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("Follow"), static_cast<int32>(EElysiumCineCamMode::FollowEntity), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("mode 3 creates from special-case's Follow"), Cine))
			{
				Step(0.001);
				Step(0.1);
				TestTrue(TEXT("mode 3 follows anchor 0"),
					Services.LastCameraShot.Origin.Equals(FVector(300.0f, 0.0f, 50.0f), 0.1f));
				TestFalse(TEXT("mode 3 publishes no target"), Services.LastCameraShot.bUseLookAt);
				TestEqual(TEXT("mode 3 publishes no FOV"),
					Services.LastCameraShot.FieldOfView, 0.0f);

				// The dead-anchor arm: retail writes an INTEGER 0 into the expiry, which the
				// `> 0.0f` gate rejects forever, and then null-derefs. **Named modernization:
				// crash -> no-op** (`retail-defects.md` §7). The shot does not expire and freezes on
				// its last pose.
				if (FElysiumEntity* Victim = World.Resolve(Anchor->Handle))
				{
					Victim->Kill();
				}
				const int32 Was = Services.Count(TEXT("UpdateCameraShotValue"));
				Step(0.1);
				Step(0.1);
				TestEqual(TEXT("a dead anchor makes mode 3 a no-op, not a crash"),
					Services.Count(TEXT("UpdateCameraShotValue")), Was);
				TestEqual(TEXT("and it stores the integer zero the gate rejects"),
					Cine->ExpiryTime, 0.0f);
				const FElysiumEntity* Live = World.Resolve(H);
				TestTrue(TEXT("so the shot does not expire"), Live != nullptr && !Live->IsDead());
				Cine->Kill();
			}
		}

		// Mode 4 (`Animated`) — publishes ONLY the FOV (which the client discards), and arms the
		// expiry at **exactly `curtime`** when its anchor dies, so it is removed one think LATER.
		{
			const FElysiumEntity* AnimAnchor = World.FindByName(TEXT("animanchor"));
			if (!TestNotNull(TEXT("the mode-4 fixture has its own anchor"), AnimAnchor))
			{
				return false;
			}
			const FElysiumEntityHandle AnimAnchorHandle = AnimAnchor->Handle;
			FElysiumEntityHandle AnimAnchors[FElysiumShotBindings::Num];
			AnimAnchors[0] = AnimAnchorHandle;
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("Animated"), static_cast<int32>(EElysiumCineCamMode::Animated), AnimAnchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("mode 4 creates from special-case's Animated"), Cine))
			{
				Step(0.001);
				Step(0.1);
				TestEqual(TEXT("mode 4 publishes the shot's FOV"),
					Services.LastCameraShot.FieldOfView, 42.0f);
				TestEqual(TEXT("a live anchor leaves the expiry unarmed"), Cine->ExpiryTime, -1.0f);

				// The anchor goes. `FUN_1006e8b0(this, 0.0)` stores `+0x55c = curtime`, and the
				// gate in the SAME call is `+0x55c < curtime` — false at equality, so the camera
				// survives the tick that armed it.
				//
				// **RG-A's "and is removed one think later" does not survive the listing.** The
				// arm is unguarded and re-runs on every think while the anchor stays dead, so the
				// expiry is re-stamped to the new `curtime` *before* the gate reads it and the
				// equality holds again, forever. Mode 4 therefore reaches the same end state mode 3
				// does — a dead anchor freezes the shot rather than ending it — by a different
				// mechanism. Reproduced as written; recorded in SC4's closure record.
				if (FElysiumEntity* Victim = World.Resolve(AnimAnchorHandle))
				{
					Victim->Kill();
				}
				const float ArmedAt = static_cast<float>(Now + 0.1);
				Step(0.1);
				TestTrue(TEXT("mode 4 armed the expiry at exactly curtime"),
					FMath::IsNearlyEqual(Cine->ExpiryTime, ArmedAt, 0.001f));
				TestTrue(TEXT("and survives the tick that armed it"), World.Resolve(H) != nullptr);
				Step(0.1);
				TestTrue(TEXT("the unguarded re-arm keeps it alive, exactly as the listing does"),
					World.Resolve(H) != nullptr);
				TestTrue(TEXT("with the expiry re-stamped to the newer curtime"),
					Cine->ExpiryTime > ArmedAt);
				Cine->Kill();
			}
		}

		// Mode > 4 — the dispatcher's `default:` arm, reachable because `CamMode` replicates in
		// 4 bits. It installs `CamEndThink` **only** when the expiry is armed and past.
		{
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("TrackedShot"), /*CamMode*/ 5, Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("mode 5 creates"), Cine))
			{
				Step(0.001);
				Step(0.1);
				const FElysiumEntity* Alive = World.Resolve(H);
				TestTrue(TEXT("mode 5 with no expiry armed is not removed"),
					Alive != nullptr && !Alive->IsDead());
				Cine->SetExpiry(0.0f);
				Step(0.1);
				const FElysiumEntity* Gone = World.Resolve(H);
				TestTrue(TEXT("mode 5 past its expiry is removed"),
					Gone == nullptr || Gone->IsDead());
			}
		}
	}

	// --- 11. RC3's placement pose pair ------------------------------------------------------------
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("StartHere"), StartOnly(TEXT("StartHere")));
		ElysiumCameraShots::Install(TEXT("EndOnly"), EndOnly(TEXT("EndOnly")));
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.PlayerLocation = FVector(0.0f, 0.0f, 0.0f);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_placement__");
		Defs.Defs.Add(Point(TEXT("startpoint"), FVector(250.0f, 0.0f, 60.0f)));
		Defs.Defs.Add(Point(TEXT("focus"), FVector(800.0f, 0.0f, 60.0f)));
		Defs.Defs.Add(Point(TEXT("endpoint"), FVector(-100.0f, 0.0f, 60.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		const FElysiumEntity* StartPoint = World.FindByName(TEXT("startpoint"));
		const FElysiumEntity* Focus = World.FindByName(TEXT("focus"));
		const FElysiumEntity* EndPoint = World.FindByName(TEXT("endpoint"));
		if (!PlayerEnt || !StartPoint || !Focus || !EndPoint)
		{
			AddError(TEXT("the placement fixture did not build"));
			return false;
		}

		// A camera whose FIRST ever shot is `End`-only places at the **player's eye position** with
		// the player's abs angles: the saved slot is still the constructor's zero.
		{
			FElysiumEntityHandle Anchors[FElysiumShotBindings::Num];
			Anchors[1] = EndPoint->Handle;
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("EndOnly"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("the End-only camera creates"), Cine))
			{
				TestFalse(TEXT("the first shot saves no previous placement"), Cine->bSavedPlacement);
				TestTrue(TEXT("a first End-only shot places at the player's eye"),
					Cine->PlacementOrigin.Equals(PlayerEnt->EyePosition(), 0.1f));
				Cine->Kill();
			}
		}

		// The pair proper: a `Start` shot places the camera, a following `End`-only re-shot saves
		// that placement and restores it rather than snapping back to the eye.
		{
			FElysiumEntityHandle Anchors[FElysiumShotBindings::Num];
			Anchors[0] = StartPoint->Handle;
			Anchors[2] = Focus->Handle;
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("StartHere"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (!TestNotNull(TEXT("the Start camera creates"), Cine))
			{
				return false;
			}
			TestTrue(TEXT("a Start shot places at anchor 0"),
				Cine->PlacementOrigin.Equals(FVector(250.0f, 0.0f, 60.0f), 0.1f));
			// `VectorAngles(lookAt - anchor0)` — the shot looks down +X at the focus.
			TestTrue(TEXT("and looks from there at the look-at"),
				FMath::IsNearlyZero(FRotator::NormalizeAxis(Cine->PlacementAngles.Yaw), 0.5f));

			// The re-shot: `SetShot` then the shot start. The placement memory is written on every
			// shot start after the first, and this one has a live placement to save.
			Cine->SetShot(TEXT("EndOnly"), static_cast<int32>(EElysiumCineCamMode::NamedShot),
				FElysiumEntityHandle());
			Cine->SetShotAnchorEntity(1, EndPoint->Handle);
			Cine->StartShotPlacement();
			TestTrue(TEXT("the re-shot saved the previous placement"), Cine->bSavedPlacement);
			TestTrue(TEXT("and an End-only shot re-places at it, not at the player's eye"),
				Cine->PlacementOrigin.Equals(FVector(250.0f, 0.0f, 60.0f), 0.1f));
			TestFalse(TEXT("the saved pose is not the player's eye"),
				Cine->PlacementOrigin.Equals(PlayerEnt->EyePosition(), 0.1f));

			// **`SetShot` alone never re-places** — which is why a mid-conversation `SetCamera`
			// leaves the entity where the first shot put it (`FUN_1017d020`'s re-shot branch never
			// calls `FUN_1006e8e0`).
			const FVector Before = Cine->PlacementOrigin;
			Cine->SetShot(TEXT("StartHere"), static_cast<int32>(EElysiumCineCamMode::NamedShot),
				FElysiumEntityHandle());
			TestTrue(TEXT("SetShot without a shot start leaves the placement alone"),
				Cine->PlacementOrigin.Equals(Before, 0.001f));
		}
	}

	// --- 12. `LeaveGrappleState` ends the shot (RC13) ---------------------------------------------
	// `CBasePlayer::LeaveGrappleState` `0x10169660` calls `SetCineCamera(NULL)`: ending a grapple
	// ends the scripted shot and destroys the disposable camera with it.
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("LookAtTarget_Snap"), LookAtTargetSnap());
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_grapple__");
		FElysiumEntityDef Cam = Director(TEXT("cam_grapple"), TEXT("LookAtTarget_Snap"), TEXT("0"));
		Cam.Origin = FVector(120.0f, 0.0f, 0.0f);
		Cam.Keys.Add(TEXT("endent"), TEXT("cam_grapple"));
		Cam.Keys.Add(TEXT("target1"), TEXT("focus"));
		Defs.Defs.Add(MoveTemp(Cam));
		Defs.Defs.Add(Point(TEXT("focus"), FVector(400.0f, 0.0f, 0.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		if (!TestNotNull(TEXT("the grapple fixture has a player"), PlayerEnt))
		{
			return false;
		}
		World.AcceptInput(TEXT("cam_grapple"), FName(TEXT("StartShot")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		FElysiumCameraCinematic* Runtime = AdoptedCamera(World);
		if (!TestNotNull(TEXT("the grapple fixture adopted a camera"), Runtime))
		{
			return false;
		}
		const FElysiumEntityHandle RuntimeHandle = Runtime->Handle;

		// The enter raises the pose lock; the leave drops it AND the shot.
		PlayerEnt->EnterGrappleState(FElysiumEntityHandle(), EElysiumGrappleRole::Attacker,
			EElysiumGrappleType::Feed, INDEX_NONE, /*bHolster*/ false);
		TestTrue(TEXT("the grapple enter raises the pose lock on the player"),
			PlayerEnt->HasViewFlags(EElysiumViewFlags::MoveAnglesFromEntity));

		PlayerEnt->LeaveGrappleState();
		TestFalse(TEXT("the grapple leave drops the pose lock"),
			PlayerEnt->HasViewFlags(EElysiumViewFlags::MoveAnglesFromEntity));
		TestFalse(TEXT("... and ends the scripted shot (RC13)"), World.HasScriptedCamera());
		const FElysiumEntity* Dead = World.Resolve(RuntimeHandle);
		TestTrue(TEXT("... destroying the disposable camera with it"),
			Dead == nullptr || Dead->IsDead());
	}

	// --- 13. `camera_showdebug`, `ShouldTransmit` and `ObjectCaps` --------------------------------
	{
		TestEqual(TEXT("camera_showdebug is declared with retail's default"),
			ElysiumCineCam::CvarDefs().Num(), 1);
		TestEqual(TEXT("... under retail's own name"),
			FString(ElysiumCineCam::CvarDefs()[0].Name), FString(TEXT("camera_showdebug")));
		TestEqual(TEXT("... defaulting to 0"),
			FString(ElysiumCineCam::CvarDefs()[0].Default), FString(TEXT("0")));
		// Both readers test `GetInt() == 1`, not truthiness.
		TestTrue(TEXT("camera_showdebug 1 shows the overlay"), ElysiumCineCam::ShowsDebug(1));
		TestFalse(TEXT("camera_showdebug 2 does NOT"), ElysiumCineCam::ShowsDebug(2));
		TestFalse(TEXT("camera_showdebug 0 does not"), ElysiumCineCam::ShowsDebug(0));
		// `CBaseCineCam::ObjectCaps()` clears the base's single `FCAP_ACROSS_TRANSITION`.
		TestEqual(TEXT("a cine camera carries no object caps at all"),
			FElysiumCameraCinematic::ObjectCaps(), 0);
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// The content-tier witness: `sp_tutorial_1`'s real `feedcamera`, cut out of the exported `.ents` and
// driven headless. Abstains, never fails, without the export root.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialFeedCameraTest,
	"Elysium.Content.TutorialFeedCamera", GFlags)
bool FElysiumTutorialFeedCameraTest::RunTest(const FString&)
{
	static const TCHAR* const SliceMap = TEXT("sp_tutorial_1");
	if (!FElysiumMapSlice::Available(SliceMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_tutorial_1.ents is not under $ELYSIUM_EXPORT_ROOT"));
		return true;
	}
	const TArray<FString> Roots = {
		TEXT("feedcamera"), TEXT("sTalkguy_move"), TEXT("sTalkguy_die"), TEXT("tutwareportal03") };
	FElysiumMapSlice Slice;
	FString Error;
	if (!FElysiumMapSlice::Build(SliceMap, Roots, Slice, Error))
	{
		AddError(Error);
		return false;
	}
	AddInfo(Slice.Report());
	if (!TestTrue(TEXT("the slice keeps feedcamera"), Slice.Kept.Contains(TEXT("feedcamera"))))
	{
		return false;
	}

	// The authored origins, read out of the slice before the world is built, so the resolved anchor
	// positions below are compared against the map rather than against the port's own answer.
	FVector AuthoredCameraOrigin = FVector::ZeroVector;
	FVector AuthoredPortalOrigin = FVector::ZeroVector;
	FString AuthoredShotName;
	FString AuthoredPointPlayer;
	int32 AuthoredSpawnFlags = 0;
	for (const FElysiumEntityDef& Def : Slice.Defs.Defs)
	{
		if (Def.TargetName.Equals(TEXT("feedcamera"), ESearchCase::IgnoreCase))
		{
			AuthoredCameraOrigin = Def.Origin;
			if (const FString* Value = Def.Keys.Find(TEXT("shotname"))) { AuthoredShotName = *Value; }
			if (const FString* Value = Def.Keys.Find(TEXT("point_player")))
			{
				AuthoredPointPlayer = *Value;
			}
			if (const FString* Value = Def.Keys.Find(TEXT("spawnflags")))
			{
				AuthoredSpawnFlags = FCString::Atoi(**Value);
			}
		}
		else if (Def.TargetName.Equals(TEXT("tutwareportal03"), ESearchCase::IgnoreCase))
		{
			AuthoredPortalOrigin = Def.Origin;
		}
	}
	AddInfo(FString::Printf(TEXT("feedcamera: shotname '%s', point_player '%s', spawnflags %d"),
		*AuthoredShotName, *AuthoredPointPlayer, AuthoredSpawnFlags));

	// The tutorial's own authored values, as RC2's census reads them.
	TestEqual(TEXT("feedcamera authors spawnflags 3 (freeze + drawplayer)"), AuthoredSpawnFlags, 3);
	TestTrue(TEXT("and names LookAtTarget_Snap"),
		AuthoredShotName.Contains(TEXT("LookAtTarget_Snap"), ESearchCase::IgnoreCase));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = AuthoredCameraOrigin;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Slice.Defs));
	World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* DirectorEnt = World.FindByName(TEXT("feedcamera"));
	FElysiumCameraCinematic* DirectorCam = DirectorEnt ? DirectorEnt->AsCameraCinematic() : nullptr;
	if (!TestNotNull(TEXT("the map's feedcamera spawns as a camera_cinematic"), DirectorCam))
	{
		return false;
	}
	const FElysiumEntityHandle DirectorHandle = DirectorCam->Handle;
	TestTrue(TEXT("spawnflags 3 gives the map's own director the draw-player bit"),
		DirectorCam->bDrawPlayerBody);

	FElysiumPlayer* PlayerEnt = World.FindPlayer();
	if (!TestNotNull(TEXT("the slice world has a player"), PlayerEnt))
	{
		return false;
	}

	// The map export and the `vdata/` corpus are two separate roots: a run can have the `.ents` and
	// not the shot files. That is the abstain below, not a failure — but the loader says so out
	// loud on the way there, and `FUN_10070780`'s `DevWarning("%s could not start shot properly")`
	// follows it, so those lines are declared only on the run that will actually emit them.
	const bool bShotCorpus = FPaths::FileExists(FElysiumContentPaths::VdataFile(
		FString(TEXT("camerashots")) / TEXT("lookattarget_snap.txt")));
	if (!bShotCorpus)
	{
		AddExpectedError(TEXT("camera shot 'lookattarget_snap' not found"),
			EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("camera shot 'special-case' not found"),
			EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("could not start shot properly"),
			EAutomationExpectedErrorFlags::Contains, 0);
	}

	World.AcceptInput(TEXT("feedcamera"), FName(TEXT("StartShot")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());

	FElysiumCameraCinematic* Runtime = AdoptedCamera(World);
	if (Runtime == nullptr)
	{
		// The one honest abstain inside the case: the shot file itself lives under the read-only
		// vdata corpus, and a run with the map export but no `vdata/camerashots/` cannot load it.
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: vdata/camerashots/LookAtTarget_Snap.txt did not load; ")
			TEXT("the shot corpus is not mounted"));
		return true;
	}
	TestTrue(TEXT("StartShot adopted a runtime camera"), World.HasScriptedCamera());
	TestTrue(TEXT("which is a second entity, not the director"),
		Runtime->Handle != DirectorHandle);
	TestTrue(TEXT("the director survives its own StartShot"),
		World.Resolve(DirectorHandle) != nullptr && !World.Resolve(DirectorHandle)->IsDead());

	// The anchors come from the director's `startent`/`endent`/`target1` targetnames, and they
	// resolve to the map's OWN authored origins.
	const FElysiumEntity* Portal = World.FindByName(TEXT("tutwareportal03"));
	if (TestNotNull(TEXT("tutwareportal03 is in the slice"), Portal))
	{
		TestTrue(TEXT("anchor 2 is tutwareportal03"),
			Runtime->Bindings.Anchors[2].Entity == Portal->Handle);
		TestTrue(TEXT("and the shot looks at its authored origin"),
			Services.LastCameraShot.bUseLookAt
			&& Services.LastCameraShot.LookAt.Equals(AuthoredPortalOrigin, 1.0f));
	}
	TestTrue(TEXT("anchor 1 (endent) is feedcamera itself"),
		Runtime->Bindings.Anchors[1].Entity == DirectorHandle);
	// `LookAtTarget_Snap` has an `End` and no `Start`, so the shot start places at the player's eye
	// and only the 24 Hz think drives the origin from the anchor — which here is `feedcamera`
	// itself, at the origin the map authored for it.
	World.Tick(0.001);
	World.Tick(0.06);
	TestTrue(TEXT("the goal settles on the map's authored camera origin"),
		Services.LastCameraShot.Origin.Equals(AuthoredCameraOrigin, 1.0f));
	TestTrue(TEXT("the published shot draws the player's body (spawnflags & 2)"),
		Services.LastCameraShot.Presentation.bDrawPlayerBody);
	TestFalse(TEXT("StartShot immobilized the player"), PlayerEnt->IsMobile());

	World.AcceptInput(TEXT("feedcamera"), FName(TEXT("EndShot")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestFalse(TEXT("EndShot cuts the shot on the same frame"), World.HasScriptedCamera());
	TestTrue(TEXT("and mobilizes the player on it"), PlayerEnt->IsMobile());
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
