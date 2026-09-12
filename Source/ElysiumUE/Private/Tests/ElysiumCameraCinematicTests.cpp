// `camera_cinematic` — the director entity, the adoption slot, the five `CamMode` arms, the 24 Hz
// goal publish and the immobilize pair (as corrected by
// `$ELYSIUM_WORK_ROOT/_camera_recovery/rc_group_a.md` RC2/RC3/RC4/RC12).
//
// Everything here runs on a bare `FElysiumEntityWorld` + `FElysiumRecordingServices`: no UWorld, no
// pawn, no renderer. The shot files are seeded into the parse cache with `ElysiumCameraShots::Install`
// so the cases drive the real solver with no export mounted. The one content-tier case at the bottom
// loads `sp_tutorial_1`'s real `.ents` through `FElysiumMapSlice` and abstains without the corpus.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"
#include "ElysiumClassRegistry.h"   // FElysiumClassDesc — the camera_animated leaf is found by class
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumUserCmd.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraAnimated.h"
#include "Substrate/ElysiumCameraCinematic.h"
#include "ElysiumContentPaths.h"
#include "ElysiumSaveTypes.h"   // the map snapshot: the port's whole ObjectCaps transition carry
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
			// `CPlayerMove::SetupMove` `0x10186120`, the whole of what the immobilize does to a
			// command: `mv->m_nButtons &= ~DAT_10589050` with `DAT_10589050 == 0x807`
			// (`IN_ATTACK | IN_JUMP | IN_DUCK | IN_ATTACK2`), and `forward = side = up = 0` in the
			// same gate. That is `FElysiumUserCmd::ApplyImmobilize` and **not** `ClearMovement`:
			// the two clear disjoint halves of the command and mean opposite things. Retail leaves
			// the direction bits standing (the analog pair is already zeroed) and drops the combat
			// ones.
			FElysiumUserCmd Cmd;
			Cmd.Move = FVector2D(1.0f, 1.0f);
			Cmd.Up = 1.0f;
			Cmd.Buttons = static_cast<uint64>(EElysiumButton::Forward)
				| static_cast<uint64>(EElysiumButton::Jump)
				| static_cast<uint64>(EElysiumButton::Duck)
				| static_cast<uint64>(EElysiumButton::Attack)
				| static_cast<uint64>(EElysiumButton::Attack2)
				| static_cast<uint64>(EElysiumButton::Use);
			Cmd.ApplyImmobilize();
			TestTrue(TEXT("the immobilize gate zeroes the wish move"), Cmd.Move.IsNearlyZero());
			TestEqual(TEXT("... and the up axis with it"), Cmd.Up, 0.0f);
			TestEqual(TEXT("0x807 drops attack"),
				Cmd.Buttons & static_cast<uint64>(EElysiumButton::Attack), 0ull);
			TestEqual(TEXT("... and jump"),
				Cmd.Buttons & static_cast<uint64>(EElysiumButton::Jump), 0ull);
			TestEqual(TEXT("... and duck"),
				Cmd.Buttons & static_cast<uint64>(EElysiumButton::Duck), 0ull);
			TestEqual(TEXT("... and attack2"),
				Cmd.Buttons & static_cast<uint64>(EElysiumButton::Attack2), 0ull);
			// **`IN_USE` is not in the mask.** Every shipped opener that immobilizes does it *for*
			// an interaction the player is holding `+use` on, so masking it would make the
			// interaction unendable — an immobilized player can still press use.
			TestTrue(TEXT("IN_USE survives the mask: an immobilized player can still end the shot"),
				(Cmd.Buttons & static_cast<uint64>(EElysiumButton::Use)) != 0);
			// The direction bits are not masked either; retail zeroes the analog pair instead.
			TestTrue(TEXT("and the direction bits stand, inert, exactly as retail leaves them"),
				(Cmd.Buttons & static_cast<uint64>(EElysiumButton::Forward)) != 0);
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
				// `WorldSpaceCenter()` (vfunc `0x300`), measured off the **same** box the anchor
				// resolver reads (`ElysiumCameraShots::SurroundingBounds`). This fixture's anchor is
				// a bodiless point entity with no use-anchor box, so the box is VtMB's standing hull
				// on its origin and the centre is half a hull above it — where a `Center` anchor on
				// the same entity lands. The think used to fall back to the raw origin instead.
				TestTrue(TEXT("mode 3 follows anchor 0"),
					Services.LastCameraShot.Origin.Equals(
						ElysiumCameraShots::SurroundingBounds(*Anchor).GetCenter(), 0.1f));
				TestTrue(TEXT("...at its world-space centre, not at its feet"),
					Services.LastCameraShot.Origin.Equals(
						FVector(300.0f, 0.0f, 50.0f + 36.0f * ElysiumCam::U), 0.1f));
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
				// `FUN_1006e8b0(this, 0.0)` — `+0x55c = curtime`. `curtime` is handed in, because
				// nothing under the think chain reads a clock.
				Cine->SetExpiry(0.0f, Now);
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

		// **`ShouldTransmit` (slot 86), called.** Its two recovered rules are "only to the client
		// whose player is `m_hSubject`" and "only while `CamMode != 0`"; the base's force-transmit
		// window (`+0x90 > curtime`) has no port counterpart and its producer is unrecovered.
		// Asserting the slot means calling the predicate on a real camera in every state, not
		// naming it in a section header.
		//
		// And `ObjectCaps`: `CBaseCineCam::ObjectCaps()` clears the base's single
		// `FCAP_ACROSS_TRANSITION` (RC2.4), so a live scripted shot is never carried across a
		// `trigger_changelevel`. The port's whole carry is `FElysiumEntityWorld::Freeze`, which
		// reads the cap — so the assertion is the freeze, not a literal compared with itself.
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("LookAtTarget_Snap"), LookAtTargetSnap());

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.PlayerLocation = FVector(0.0f, 0.0f, 100.0f);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_transmit__");
		Defs.Defs.Add(Point(TEXT("tutwareportal03"), FVector(900.0f, 0.0f, 120.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		const FElysiumEntity* Ordinary = World.FindByName(TEXT("tutwareportal03"));
		const FElysiumEntityHandle NoAnchors[FElysiumShotBindings::Num] = {};
		const FElysiumEntityHandle TransmitHandle = FElysiumCameraCinematic::CreateRuntimeCamera(
			World, TEXT("LookAtTarget_Snap"),
			static_cast<int32>(EElysiumCineCamMode::NamedShot), NoAnchors);
		FElysiumEntity* TransmitEntity = World.Resolve(TransmitHandle);
		FElysiumCameraCinematic* Transmit =
			TransmitEntity ? TransmitEntity->AsCameraCinematic() : nullptr;
		if (TestNotNull(TEXT("a runtime camera to ask ShouldTransmit about"), Transmit)
			&& TestNotNull(TEXT("and an ordinary entity to compare caps against"), Ordinary))
		{
			const FElysiumPlayer* Subject = World.FindPlayer();
			const FElysiumEntityHandle SubjectHandle =
				Subject ? Subject->Handle : FElysiumEntityHandle::Invalid();
			TestTrue(TEXT("an active camera transmits to its own subject"),
				Transmit->ShouldTransmit(SubjectHandle));
			// A recipient that is not `m_hSubject`, standing in for a second client: retail's shot
			// is invisible to it.
			TestFalse(TEXT("... and to nobody else"),
				Transmit->ShouldTransmit(Ordinary->Handle));
			TestFalse(TEXT("... nor to an unset recipient"),
				Transmit->ShouldTransmit(FElysiumEntityHandle::Invalid()));

			// The caps, through the BASE's virtual — the wire, not the leaf's literal.
			const FElysiumEntity* AsBase = TransmitEntity;
			TestEqual(TEXT("a cine camera carries no object caps at all"),
				AsBase->ObjectCaps(), 0);
			TestEqual(TEXT("while an ordinary entity carries the transition bit"),
				Ordinary->ObjectCaps() & ElysiumEntityCaps::AcrossTransition,
				ElysiumEntityCaps::AcrossTransition);

			// A live scripted shot is not written into the map snapshot, so walking back into the
			// map cannot restore one — and it is not recorded *absent* either, which is what would
			// delete a map-placed director on the next load.
			FElysiumMapSnapshot Snapshot;
			World.Freeze(Snapshot);
			bool bCarried = false;
			for (const FElysiumEntityState& State : Snapshot.Entities)
			{
				bCarried = bCarried || State.Index == TransmitHandle.Index;
			}
			TestFalse(TEXT("the freeze does not carry a live cine camera across the transition"),
				bCarried);
			TestFalse(TEXT("nor record it absent"),
				Snapshot.AbsentEntities.Contains(TransmitHandle.Index));

			// `CamMode == 0` refuses every client, which is the port's "the goal is released".
			Transmit->ClearMode();
			TestFalse(TEXT("an idle camera transmits to nobody, subject included"),
				Transmit->ShouldTransmit(SubjectHandle));
		}
	}

	// --- 14. A shot with **no `Target` block**: the published angles, and `point_player` ----------
	//
	// `0x1006f8f0` seeds `fStack_24..1c` from `GetAbsAngles()` (vfunc `0x36c`) **unconditionally**
	// and publishes them at `param_1[0x181..0x183]`; the `+0xd4` gate only decides whether they are
	// *replaced* by `VectorAngles(lookAt - GetOrigin())`. So a `TargetPointCount == 0` shot carries
	// the entity's own angles at every 24 Hz publish, not a zero rotation.
	//
	// And the tail is `thunk_FUN_10178590(this, param_1[0x17e..0x180])` — `m_vecCamTarget`,
	// **always**, which is `vec3_origin` when neither `Target` flag is raised. With
	// `m_bForcePlayerLook` 1 by construction, retail spins the subject toward `(0,0,0)`.
	//
	// `special-case.txt`'s `Follow` is one of the two shipped shots that can reach this.
	{
		ElysiumCameraShots::FlushCache();
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.PlayerLocation = FVector(0.0f, 0.0f, 100.0f);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_no_target__");
		Defs.Defs.Add(Point(TEXT("anchor"), FVector(600.0f, 0.0f, 200.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* AnchorEnt = World.FindByName(TEXT("anchor"));
		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		if (TestNotNull(TEXT("the anchor exists"), AnchorEnt)
			&& TestNotNull(TEXT("and the player"), PlayerEnt))
		{
			FElysiumEntityHandle Anchors[FElysiumShotBindings::Num] = {};
			Anchors[0] = AnchorEnt->Handle;
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("Follow"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("the Target-less shot loads and places"), Cine))
			{
				// `FUN_1006e8e0`'s arm A is unconditional (M3): `FUN_1006f670` answers
				// `vec3_origin` and `VectorAngles` of the direction to it is what `+0x57c` keeps.
				// So the placement faces the world origin from the anchor.
				const FRotator FacingOrigin =
					(FVector::ZeroVector - Cine->PlacementOrigin).Rotation();
				TestTrue(TEXT("a Target-less Start shot places facing the world origin"),
					Cine->PlacementAngles.Equals(FacingOrigin, 0.05f));

				TestFalse(TEXT("the published goal has no look-at"),
					Services.LastCameraShot.bUseLookAt);
				TestTrue(TEXT("and the shot start publishes the placement angles"),
					Services.LastCameraShot.Rotation.Equals(Cine->PlacementAngles, 0.05f));

				// The 24 Hz think: the angles are published again, and are NOT a zero rotation.
				World.Tick(0.001);
				World.Tick(0.06);
				TestTrue(TEXT("every think republishes the entity's abs angles, not zero"),
					Services.LastCameraShot.Rotation.Equals(Cine->PlacementAngles, 0.05f));
				TestFalse(TEXT("which is not the zero rotation the port used to publish"),
					Services.LastCameraShot.Rotation.IsNearlyZero());
				// The goal carries a real `m_angCamAngles`, so the client copies it through rather
				// than re-deriving it.
				TestTrue(TEXT("the goal is marked as a real m_angCamAngles publish"),
					Services.LastCameraShot.bAnglesPublished);

				// `point_player`: `m_vecCamTarget` is `vec3_origin`, and it is passed anyway.
				TestTrue(TEXT("the Target-less shot still raises the subject's eye snap"),
					PlayerEnt->HasPendingEyeAngleSnap());
				TestTrue(TEXT("aimed at the world origin, not at the lens"),
					PlayerEnt->PendingEyeLookPoint.IsNearlyZero());
			}
		}
	}

	// --- 15. The `+0xd4` gate measures from the PLACEMENT, not from the published origin ----------
	//
	// `if (0 < rec->+0xd4) { pfVar5 = GetOrigin(); VectorAngles(lookAt - *pfVar5, &fStack_24); }`
	// with `GetOrigin()` (vfunc `0x370`) the entity's LOCAL transform — the pose `FUN_1006e8e0`
	// wrote with `SetOrigin(+0x564)` — while `param_1[0x17b..0x17d]` publishes the **selector's**
	// origin. A `Start` + `End` shot has the two at different points, and retail's published angle
	// is the one measured from `Start`.
	{
		ElysiumCameraShots::FlushCache();
		FElysiumCameraShotDef Both;
		Both.Name = TEXT("StartAndEnd");
		Both.Start.bPresent = true;
		Both.Start.Position = EElysiumShotPosition::Named;
		Both.Start.Attach = EElysiumShotAttach::Follow;
		Both.End.bPresent = true;
		Both.End.Position = EElysiumShotPosition::Named;
		Both.End.Attach = EElysiumShotAttach::Follow;
		Both.Target1.bPresent = true;
		Both.Target1.Position = EElysiumShotPosition::Named;
		Both.Target1.Attach = EElysiumShotAttach::Follow;
		Both.TargetPointCount = 1;
		Both.bTargetPoint1Flagged = true;
		ElysiumCameraShots::Install(TEXT("StartAndEnd"), Both);

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.PlayerLocation = FVector(0.0f, 0.0f, 100.0f);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_gate_origin__");
		Defs.Defs.Add(Point(TEXT("startpt"), FVector(0.0f, 500.0f, 100.0f)));
		Defs.Defs.Add(Point(TEXT("endpt"), FVector(0.0f, -500.0f, 100.0f)));
		Defs.Defs.Add(Point(TEXT("looktarget"), FVector(800.0f, 0.0f, 100.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* StartPt = World.FindByName(TEXT("startpt"));
		FElysiumEntity* EndPt = World.FindByName(TEXT("endpt"));
		FElysiumEntity* LookPt = World.FindByName(TEXT("looktarget"));
		if (TestNotNull(TEXT("start point"), StartPt) && TestNotNull(TEXT("end point"), EndPt)
			&& TestNotNull(TEXT("look target"), LookPt))
		{
			FElysiumEntityHandle Anchors[FElysiumShotBindings::Num] = {};
			Anchors[0] = StartPt->Handle;
			Anchors[1] = EndPt->Handle;
			Anchors[2] = LookPt->Handle;
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("StartAndEnd"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("the two-anchor shot loads"), Cine))
			{
				// The live `End` handle set the selector to 0, so the published origin is `End`'s.
				TestTrue(TEXT("a live End anchor drives the origin"),
					Cine->OriginSelector == EElysiumShotOriginSelector::EndAnchor);
				TestTrue(TEXT("and arm A placed the entity on the Start anchor"),
					Cine->PlacementOrigin.Equals(StartPt->Origin, 0.1f));

				World.Tick(0.001);
				World.Tick(0.06);
				const FElysiumCameraShot& Goal = Services.LastCameraShot;
				TestTrue(TEXT("the think publishes the End anchor as the origin"),
					Goal.Origin.Equals(EndPt->Origin, 0.1f));
				// The whole finding: the angle is measured from `Start`, and the two differ.
				const FRotator FromPlacement =
					(LookPt->Origin - StartPt->Origin).Rotation();
				const FRotator FromPublished =
					(LookPt->Origin - EndPt->Origin).Rotation();
				TestFalse(TEXT("the two measuring points really do disagree"),
					FromPlacement.Equals(FromPublished, 1.0f));
				TestTrue(TEXT("m_angCamAngles is measured from the placement, not the origin"),
					Goal.Rotation.Equals(FromPlacement, 0.05f));

				// And the shot-start seed takes the published triple straight through
				// (`FUN_10002210`, `0x474..0x47c = 0x428..0x430`) — no look-at re-derive. This is a
				// `Start`-bearing shot, so `StartsOnGoal()` puts it on the goal arm.
				TestTrue(TEXT("a Start-bearing shot seeds on the goal"), Goal.StartsOnGoal());
				FElysiumScriptedShotTracker Tracker;
				FElysiumViewSetup LiveView;
				LiveView.Location = FVector(-1000.0f, 0.0f, 0.0f);
				LiveView.Rotation = FRotator(30.0f, 180.0f, 0.0f);
				Tracker.Start(Goal, LiveView);
				TestTrue(TEXT("the seed is the published origin"),
					Tracker.Location.Equals(Goal.Origin, 0.1f));
				TestTrue(TEXT("and the published angle, not one re-derived from the look-at"),
					Tracker.Rotation.Equals(FromPlacement, 0.05f));
			}
		}
	}

	// --- 16. The shot-start anchor cache is refilled on EVERY shot start --------------------------
	//
	// `FUN_1006e8e0`'s fill loop (`1006eb2f`-`1006eba5`) writes `+0x598 + i*12` unconditionally,
	// and `FUN_1006e0e0` deliberately does not clear it — so a second shot start on the same entity
	// re-latches from the new anchors. Only `Start` + `AttachType None` shots latch at all, which is
	// `special-case.txt`'s `Follow`; without the refill a re-shot would frame the first anchor
	// forever, and `FindBestShot`'s per-candidate loop would let a latching candidate poison the
	// candidates after it.
	{
		ElysiumCameraShots::FlushCache();
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.PlayerLocation = FVector(0.0f, 0.0f, 100.0f);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_relatch__");
		Defs.Defs.Add(Point(TEXT("first"), FVector(300.0f, 0.0f, 100.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* First = World.FindByName(TEXT("first"));
		if (TestNotNull(TEXT("first anchor"), First))
		{
			FElysiumEntityHandle Anchors[FElysiumShotBindings::Num] = {};
			Anchors[0] = First->Handle;
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("Follow"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("the latching shot loads"), Cine))
			{
				TestTrue(TEXT("the shot start latched anchor 0"),
					Cine->Bindings.Anchors[0].bCached);
				const FVector FirstCached = Cine->Bindings.Anchors[0].Cached;
				TestTrue(TEXT("and the placement is that same point"),
					Cine->PlacementOrigin.Equals(FirstCached, 0.1f));
				// Every anchor the record does not declare is cached too — retail's loop writes
				// `vec3_origin` for a dead handle rather than leaving the slot alone.
				TestTrue(TEXT("and the undeclared anchors cached the world origin"),
					Cine->Bindings.Anchors[1].bCached
					&& Cine->Bindings.Anchors[1].Cached.IsNearlyZero());

				// The re-shot, in `FindBestShot`'s own shape: `SetShot` (which clears the mode and
				// re-binds the anchors, but NOT the cache) then the shot start. No
				// `SetShotAnchorEntity`, so nothing re-opens the cache by hand — the refill has to
				// come from the shot start itself. `Follow`'s anchor is `Position Named`, which
				// `SetShot` resolves to the subject, so the new anchor is the player.
				Cine->SetShot(TEXT("Follow"),
					static_cast<int32>(EElysiumCineCamMode::NamedShot),
					FElysiumEntityHandle());
				Cine->StartShotPlacement();

				// **RC3's ordering trap, preserved:** arm A reads the cache-aware anchor reader
				// *before* the fill at the bottom of the same function, so the placement is the
				// PREVIOUS shot's cached point.
				TestTrue(TEXT("the re-shot's placement still comes from the stale cache (RC3)"),
					Cine->PlacementOrigin.Equals(FirstCached, 0.1f));
				// **And the fill still runs**, so the next think latches the new anchor rather than
				// the first shot's forever.
				TestTrue(TEXT("but the shot start refilled the cache from the NEW anchor"),
					Cine->Bindings.Anchors[0].bCached
					&& !Cine->Bindings.Anchors[0].Cached.Equals(FirstCached, 0.1f));
			}
		}
	}

	// --- 17. `SetShot` installs no think, and does not reset the 24 Hz phase (M4) -----------------
	//
	// `FUN_1006e130`'s tail is `+0x63c = GetFrameCount(); +0x638 = camMode; return 1;` — there is no
	// `ThinkSet` on it. `FUN_1006e770` is called by `FUN_1006e8e0` alone. So a `SetCamera` re-shot
	// of a camera left **idle** by a failed `InputStartShot` sets `CamMode = 1` with no think
	// installed and publishes nothing, ever.
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("StartHere"), StartOnly(TEXT("StartHere")));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__cine_setshot_think__");
		Defs.Defs.Add(Point(TEXT("anchor"), FVector(400.0f, 0.0f, 100.0f)));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* AnchorEnt = World.FindByName(TEXT("anchor"));
		if (TestNotNull(TEXT("the anchor exists"), AnchorEnt))
		{
			FElysiumEntityHandle Anchors[FElysiumShotBindings::Num] = {};
			Anchors[0] = AnchorEnt->Handle;
			Anchors[2] = AnchorEnt->Handle;
			const FElysiumEntityHandle H = FElysiumCameraCinematic::CreateRuntimeCamera(World,
				TEXT("StartHere"), static_cast<int32>(EElysiumCineCamMode::NamedShot), Anchors);
			FElysiumCameraCinematic* Cine = CineOf(World, H);
			if (TestNotNull(TEXT("the camera creates"), Cine))
			{
				// Idle it the way a failed `InputStartShot` re-shot does, and let the think queue
				// see mode 0 once so the deadline goes to "never".
				Cine->ClearMode();
				World.Tick(0.001);
				TestEqual(TEXT("an idle camera schedules no think"),
					Cine->NextThink, ELYSIUM_NEVER_THINK);

				// `SetShot` alone: the mode is written, and nothing re-arms the deadline.
				const bool bSet = Cine->SetShot(TEXT("StartHere"),
					static_cast<int32>(EElysiumCineCamMode::NamedShot),
					FElysiumEntityHandle());
				TestTrue(TEXT("the re-shot succeeds"), bSet);
				TestTrue(TEXT("and sets CamMode 1"), Cine->IsActive());
				TestEqual(TEXT("but installs NO think: the camera publishes nothing, ever"),
					Cine->NextThink, ELYSIUM_NEVER_THINK);

				// The 24 Hz phase is likewise untouched by `SetShot` — only the shot start re-arms
				// it, which is what makes a mid-conversation `SetCamera` keep the cadence.
				Cine->ThinkAccumulator = 0.02f;
				Cine->SetShot(TEXT("StartHere"),
					static_cast<int32>(EElysiumCineCamMode::NamedShot),
					FElysiumEntityHandle());
				TestEqual(TEXT("SetShot does not reset the accumulator"),
					Cine->ThinkAccumulator, 0.02f);
				Cine->StartShotPlacement();
				TestEqual(TEXT("the shot start does"), Cine->ThinkAccumulator, 0.0f);
			}
		}
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// `camera_animated` — `CCameraAnimated`'s sequence path (`StartCamera` `0x10071550`,
// `FUN_10071770`, the think `FUN_10071840`, `EndCamera` `0x10071660`), recovered in
// `$ELYSIUM_WORK_ROOT/_camera_recovery/rc_group_f.md` §RC15.3.
//
// No shipped map places one (0 instances across 108 `.ents`), so every case here is hand-built. The
// body comes up through the **animated-prop** route, which is the route a camera rig's model can
// actually take: `FElysiumAnimating::BuildBody` stands a character body only, and the clip resolve
// under it refuses any stem outside the manifest's `npcs`/`banks` groups.
namespace ElysiumCameraAnimatedTests
{
// The rig's model, and the `animated_props` stem the fixture maps it to.
static const TCHAR* const RigModel = TEXT("models/props/camrig.mdl");
static const TCHAR* const RigStem = TEXT("camrig");

static FElysiumEntityDef AnimatedCamera(const TCHAR* Name, const TCHAR* AnimName,
	const TCHAR* SpawnFlags)
{
	FElysiumEntityDef Def;
	Def.Classname = TEXT("camera_animated");
	Def.TargetName = Name;
	Def.Origin = FVector(120.0f, -40.0f, 60.0f);
	Def.Keys.Add(TEXT("model"), RigModel);
	Def.Keys.Add(TEXT("animname"), AnimName);
	Def.Keys.Add(TEXT("spawnflags"), SpawnFlags);
	FElysiumOutputDef Begin;
	Begin.Name = TEXT("OnCameraBegin");
	Begin.Target = TEXT("counter1");
	Begin.Input = TEXT("Add");
	Begin.Param = TEXT("1");
	Def.Outputs.Add(Begin);
	FElysiumOutputDef Complete;
	Complete.Name = TEXT("OnCameraComplete");
	Complete.Target = TEXT("counter1");
	Complete.Input = TEXT("Add");
	Complete.Param = TEXT("100");
	Def.Outputs.Add(Complete);
	return Def;
}

static float CounterValue(const FElysiumEntity* Entity)
{
	if (Entity == nullptr)
	{
		return -1.0f;
	}
	TArray<TPair<FString, FString>> State;
	Entity->GetDebugState(State);
	for (const TPair<FString, FString>& Row : State)
	{
		if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
	}
	return -1.0f;
}

// One world holding one `camera_animated`, one `math_counter` its two outputs add into, and a
// player. `Services` stays with the caller because every case reads its call log.
static void BuildWorld(FElysiumEntityWorld& World, FElysiumEntityDef&& Camera)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__camera_animated__");
	Defs.Defs.Add(MoveTemp(Camera));
	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
}

// There is no `AsCameraAnimated()` on the entity base, so the leaf is reached by its own registered
// classname — null if the class ever fell back to a record-only stub, which is the thing worth
// failing on rather than casting through.
static FElysiumCameraAnimated* CameraOf(FElysiumEntityWorld& World, const TCHAR* Name)
{
	FElysiumEntity* Ent = World.FindByName(Name);
	if (Ent == nullptr || Ent->Class == nullptr || Ent->Class->bStub
		|| Ent->Class->ClassName != FName(TEXT("camera_animated")))
	{
		return nullptr;
	}
	return static_cast<FElysiumCameraAnimated*>(Ent);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraAnimatedTest,
	"Elysium.Substrate.CameraAnimated", GFlags)
bool FElysiumCameraAnimatedTest::RunTest(const FString&)
{
	using namespace ElysiumCameraAnimatedTests;

	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

	// --- 1. A resolvable `animname`: begin, a 0.1 s think, and an end on the finished flag --------
	{
		ElysiumCameraShots::FlushCache();
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.ClipSeconds = 2.0f;   // the rig's authored sequence length
		Services.AnimatedPropModels.Add(RigModel, RigStem);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		BuildWorld(World, AnimatedCamera(TEXT("animcam"), TEXT("swoop"), TEXT("1")));

		FElysiumCameraAnimated* Cam = CameraOf(World, TEXT("animcam"));
		if (!TestNotNull(TEXT("camera_animated resolves as its own leaf"), Cam))
		{
			return false;
		}
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		if (!TestNotNull(TEXT("the fixture has a player"), PlayerEnt))
		{
			return false;
		}

		// `Spawn` `0x10071330` requires a model and calls `SetModel`. The port's build order picks
		// the `animated_props` vocabulary, so `Bone: cam_bone` has a body to resolve against.
		TestTrue(TEXT("Spawn stood the rig through the animated-prop route"),
			Services.Saw(TEXT("BuildAnimatedPropVisual camrig")));
		TestFalse(TEXT("and not through the character route"),
			Services.Saw(TEXT("BuildNpcVisual")));
		TestEqual(TEXT("a spawned camera_animated is idle"), Cam->NextThink, ELYSIUM_NEVER_THINK);
		TestEqual(TEXT("nothing has fired yet"), CounterValue(Count), 0.0f);

		// Retail's `curtime` is never zero; move the clock before the input so the deadline the
		// think arms is readable.
		World.Tick(1.0);
		World.AcceptInput(TEXT("animcam"), FName(TEXT("StartCamera")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());

		// `FUN_10070690` created the `CamMode 4` camera and `SetCineCamera` adopted it.
		TestTrue(TEXT("StartCamera adopted a cine camera"), World.HasScriptedCamera());
		FElysiumCameraCinematic* Runtime = AdoptedCamera(World);
		if (TestNotNull(TEXT("the adopted camera is a camera_cinematic"), Runtime))
		{
			TestEqual(TEXT("and it runs CamMode 4"), Runtime->CamMode,
				static_cast<int32>(EElysiumCineCamMode::Animated));
			TestTrue(TEXT("it is disposable, as the factory's spawnflags |= 4 says"),
				Runtime->bDisposable);
		}
		// `FUN_10071770`: the sequence resolved, so `OnCameraBegin` fired and the think is armed at
		// `curtime + 0.1` — `_DAT_104493d0`, a double 0.1, ten times slower than the cine camera's
		// own 1/24 s.
		TestTrue(TEXT("the named sequence played on the rig"),
			Services.Saw(TEXT("PlayAnimatedPropClip camrig swoop")));
		TestEqual(TEXT("the think is armed at curtime + 0.1"),
			static_cast<double>(Cam->NextThink), 1.1, 1.e-4);
		// `spawnflags & 1` — the one class that reads the freeze bit.
		TestFalse(TEXT("spawnflags 1 immobilized the player"), PlayerEnt->IsMobile());

		// Outputs ride the I/O queue, so one tick short of the think delivers `OnCameraBegin`
		// without advancing the sequence.
		World.Tick(1.05);
		TestEqual(TEXT("OnCameraBegin fired"), CounterValue(Count), 1.0f);
		TestEqual(TEXT("and the think is still armed at 1.1"),
			static_cast<double>(Cam->NextThink), 1.1, 1.e-4);

		// The think re-arms at 0.1 while the sequence runs, and NOT on a cycle test.
		World.Tick(1.15);
		TestTrue(TEXT("the camera is still adopted mid-sequence"), World.HasScriptedCamera());
		TestEqual(TEXT("and the think re-armed at another 0.1"),
			static_cast<double>(Cam->NextThink), 1.25, 1.e-4);
		TestEqual(TEXT("OnCameraComplete has not fired"), CounterValue(Count), 1.0f);

		// `m_bSequenceFinished` — the clip's own 2 s at `ResetSequenceInfo`'s rate 1.0.
		World.Tick(3.05);
		TestFalse(TEXT("EndCamera removed the camera it created"), World.HasScriptedCamera());
		TestTrue(TEXT("and released the freeze"), PlayerEnt->IsMobile());
		TestEqual(TEXT("a finished camera_animated stops thinking"), Cam->NextThink,
			ELYSIUM_NEVER_THINK);
		World.Tick(3.1);
		TestEqual(TEXT("the finished flag fires OnCameraComplete"), CounterValue(Count), 101.0f);
	}

	// --- 2. A looping clip ends the camera on its FIRST wrap --------------------------------------
	//
	// `StudioFrameAdvance` raises `m_bSequenceFinished` on the wrap regardless of
	// `m_bSequenceLoops`, and the think reads only that byte — so `STUDIO_LOOPING` buys the clip
	// nothing here. The clip is still PLAYED looping, exactly as `ResetSequenceInfo` copies the flag.
	{
		ElysiumCameraShots::FlushCache();
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.ClipSeconds = 1.5f;
		Services.AnimatedPropModels.Add(RigModel, RigStem);
		Services.AnimatedPropClipLoops.Add(FString(RigStem) + TEXT("|orbit"), true);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		BuildWorld(World, AnimatedCamera(TEXT("animcam"), TEXT("orbit"), TEXT("0")));

		FElysiumCameraAnimated* Cam = CameraOf(World, TEXT("animcam"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		World.Tick(1.0);
		World.AcceptInput(TEXT("animcam"), FName(TEXT("StartCamera")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());

		TestTrue(TEXT("the looping clip is played with its own STUDIO_LOOPING bit"),
			Services.Saw(TEXT("PlayAnimatedPropClip camrig orbit loop=1")));
		if (Cam != nullptr)
		{
			TestTrue(TEXT("m_bSequenceLoops is recorded"), Cam->bSequenceLoops);
		}
		World.Tick(1.4);
		TestTrue(TEXT("the shot is live before the wrap"), World.HasScriptedCamera());
		World.Tick(2.55);
		TestFalse(TEXT("the first wrap ends the camera anyway"), World.HasScriptedCamera());
		World.Tick(2.6);
		TestEqual(TEXT("and OnCameraComplete fired on it"), CounterValue(Count), 101.0f);
	}

	// --- 3. A missing sequence STRANDS the adopted camera ------------------------------------------
	//
	// `FUN_10071770`'s `seq < 0` arm writes `m_nSequence = 0` and returns: no `OnCameraBegin`, no
	// `ThinkSet`, no `m_flNextThink`. The `CamMode 4` camera adopted two lines earlier stays
	// adopted, `OnCameraComplete` never fires, and the `spawnflags & 1` freeze `StartCamera` applies
	// immediately AFTER the failed call is never released. RC15.3 §3.2; recorded in
	// `docs/vtmb/retail-defects.md` §7.
	{
		ElysiumCameraShots::FlushCache();
		InstallSpecialCase();
		AddExpectedError(TEXT("no sequence named:missing_clip"),
			EAutomationExpectedErrorFlags::Contains, 1);

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.AnimatedPropModels.Add(RigModel, RigStem);
		// An explicit empty rest clip is the fixture's "this model bakes no clip", which is what
		// makes `FindAnimatedPropClip` answer false — the port's `LookupSequence` returning < 0.
		Services.AnimatedPropRestClips.Add(RigStem, FString());
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		BuildWorld(World, AnimatedCamera(TEXT("animcam"), TEXT("missing_clip"), TEXT("1")));

		FElysiumCameraAnimated* Cam = CameraOf(World, TEXT("animcam"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		World.Tick(1.0);
		World.AcceptInput(TEXT("animcam"), FName(TEXT("StartCamera")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());

		TestFalse(TEXT("nothing was played"), Services.Saw(TEXT("PlayAnimatedPropClip")));
		TestEqual(TEXT("no output fired — not OnCameraBegin, not OnCameraComplete"),
			CounterValue(Count), 0.0f);
		TestTrue(TEXT("the camera the factory created is adopted"), World.HasScriptedCamera());
		if (Cam != nullptr)
		{
			TestEqual(TEXT("no think was armed"), Cam->NextThink, ELYSIUM_NEVER_THINK);
			TestTrue(TEXT("and the entity still holds its camera handle"), Cam->CineCamera.IsSet());
		}
		if (PlayerEnt != nullptr)
		{
			TestFalse(TEXT("the freeze bit still bit"), PlayerEnt->IsMobile());
		}

		// Ticking forever changes nothing: the only exit is EndCamera, and nothing calls it.
		for (int32 i = 0; i < 20; ++i)
		{
			World.Tick(1.0 + 0.5 * static_cast<double>(i + 1));
		}
		TestTrue(TEXT("the view is still stuck on the still camera"), World.HasScriptedCamera());
		TestEqual(TEXT("and still nothing has fired"), CounterValue(Count), 0.0f);
		if (PlayerEnt != nullptr)
		{
			TestFalse(TEXT("the player is still frozen"), PlayerEnt->IsMobile());
		}

		// A map that wires `EndCamera` by hand is the only way out, and it works — the strand is
		// "nothing fires it", not "the exit is broken".
		World.AcceptInput(TEXT("animcam"), FName(TEXT("EndCamera")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		TestFalse(TEXT("a hand-fired EndCamera drops the camera"), World.HasScriptedCamera());
		World.Tick(11.5);
		TestEqual(TEXT("and completes it — the only output the strand ever reaches"),
			CounterValue(Count), 100.0f);
		if (PlayerEnt != nullptr)
		{
			TestTrue(TEXT("and releases the freeze"), PlayerEnt->IsMobile());
		}
	}

	// --- 4. Without the freeze bit the player keeps moving ----------------------------------------
	{
		ElysiumCameraShots::FlushCache();
		InstallSpecialCase();

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.ClipSeconds = 0.5f;
		Services.AnimatedPropModels.Add(RigModel, RigStem);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		BuildWorld(World, AnimatedCamera(TEXT("animcam"), TEXT("swoop"), TEXT("0")));

		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		World.Tick(1.0);
		World.AcceptInput(TEXT("animcam"), FName(TEXT("StartCamera")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		TestTrue(TEXT("the shot runs"), World.HasScriptedCamera());
		if (PlayerEnt != nullptr)
		{
			TestTrue(TEXT("spawnflags 0 leaves the player mobile"), PlayerEnt->IsMobile());
		}
	}
	return true;
}

}   // namespace ElysiumCameraCinematicTests

#endif   // WITH_DEV_AUTOMATION_TESTS
