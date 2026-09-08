// `CBaseCineCam::FindBestShot` `FUN_1006e4c0`, its two predicates (`FUN_1006d9d0` /
// `FUN_1006db10`), the factory `FUN_10070550` and the anim-event channel that is their only route in
// — `CBasePlayer::HandleAnimEvent` `0x10178a10` events **4050** (`0xfd2`) and **4051** (`0xfd3`).
// `docs/project/camera_scripted.md` §SC8; the recovery is
// `$ELYSIUM_WORK_ROOT/_camera_recovery/server_cine_camera.md` §4 with `rc_group_a.md` (the
// `point_player` default) and `rc_group_de.md` RC13 (the grapple role pair).
//
// Everything runs on a bare `FElysiumEntityWorld` + `FElysiumRecordingServices`: no UWorld, no pawn,
// no renderer. The shot table is seeded with `ElysiumCameraShots::Install` / `InstallNamed`, and the
// geometry the visibility predicate asks about is the recording double's scriptable blocker list, so
// the "the subject never blocks its own shot" half is assertable without a collision world.
//
// The last case is the plan's **`StealthKillCamera` witness**: `stealth_kill.txt` verbatim, both
// grapple roles live, 4050 then 4051, and RC13's "leaving the grapple ends the shot" edge. No
// shipped map is needed for any of it.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSkeletalBasis.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraCinematic.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumFindBestShotTests
{
static constexpr EAutomationTestFlags GFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// `vdata/camerashots/stealth_kill.txt`, byte for byte — the one shipped family anim event 4050
// names, and the `"%s_%d"` shape `FindBestShot` enumerates.
static const TCHAR* StealthKillText()
{
	return TEXT(R"KV(
CameraShotTable
{

	// slightly above, in front of, and to the left of the sequence.
	Stealth_Kill_1
	{
		Start
		{
			"Position"		"GrappleAttacker"
			"AttachPos"		"Origin"
			"AttachType"		"Follow"
			"OffsetOrigin"		"[64, 64, 96]"
		}

		Target
		{
			Point1
			{
				"Position"		"GrappleAttacker"
				"AttachPos"		"Bone: Bip01 Head"
				"AttachType"		"Follow"
			}
		}

		CameraConstraints
		{
			"TurnAccel"		"60"
			"MaxTurnRate"		"[180,180,180]"
			"DistanceTolerance"	"10"
			"AngularTolerance"	"[2, 2, 2]"
		}

	}

	// in front of target, looking at target.
	Stealth_Kill_2
	{
		Start
		{
			"Position"		"GrappleAttacker"
			"AttachPos"		"Bone: Bip01 Head"
			"AttachType"		"FollowEntAngles"
			"OffsetOrigin"		"[128,0,0]"
		}

		Target
		{
			Point1
			{
				"Position"		"GrappleAttacker"
				"AttachPos"		"Bone: Bip01 Head"
				"AttachType"		"Follow"
			}
		}

		CameraConstraints
		{
			"TurnAccel"		"60"
			"MaxTurnRate"		"[180,180,180]"
			"DistanceTolerance"	"10"
			"AngularTolerance"	"[2, 2, 2]"
		}

	}

	// slightly above, in front of, and to the left of the sequence.
	Stealth_Kill_3
	{
		Start
		{
			"Position"		"GrappleAttacker"
			"AttachPos"		"Origin"
			"AttachType"		"Follow"
			"OffsetOrigin"		"[64, -64, 24]"
		}

		Target
		{
			Point1
			{
				"Position"		"GrappleAttacker"
				"AttachPos"		"Bone: Bip01 Head"
				"AttachType"		"Follow"
			}
		}

		CameraConstraints
		{
			"TurnAccel"		"60"
			"MaxTurnRate"		"[180,180,180]"
			"DistanceTolerance"	"10"
			"AngularTolerance"	"[2, 2, 2]"
		}

	}

	// high above, slightly behindlooking straight down
	Stealth_Kill_4
	{
		Start
		{
			"Position"		"GrappleVictim"
			"AttachPos"		"Origin"
			"AttachType"		"FollowEntAngles"
			"OffsetOrigin"		"[-32, 0, 128]"
		}

		Target
		{
			Point1
			{
				"Position"		"GrappleVictim"
				"AttachPos"		"Bone: Bip01 Head"
				"AttachType"		"Follow"
			}
		}

		CameraConstraints
		{
			"TurnAccel"		"60"
			"MaxTurnRate"		"[180,180,180]"
			"DistanceTolerance"	"10"
			"AngularTolerance"	"[2, 2, 2]"
		}

	}

}
)KV");
}

// A shot with one anchor and one target point, spelled through the keywords the port's `Position`
// parser recognises. `FindBestShot`'s scan calls `SetShot(name, 1, NULL)` and never
// `SetShotAnchorEntity`, so a candidate's anchors have to come from the keyword alone.
static FElysiumCameraShotDef Framed(const TCHAR* Name, EElysiumShotPosition StartOn,
	EElysiumShotPosition TargetOn)
{
	FElysiumCameraShotDef Def;
	Def.Name = Name;
	Def.Start.bPresent = true;
	Def.Start.Position = StartOn;
	Def.Start.Attach = EElysiumShotAttach::Follow;   // not `None`, so nothing latches
	Def.Target1.bPresent = true;
	Def.Target1.Position = TargetOn;
	Def.Target1.Attach = EElysiumShotAttach::Follow;
	Def.TargetPointCount = 1;
	Def.bTargetPoint1Flagged = true;
	return Def;
}

// A shot that declares nothing at all: no `Start`, no `End`, no `Target`. Retail's anchor predicate
// tests only the flags the record raises, so this one passes with no entity anywhere.
static FElysiumCameraShotDef Bare(const TCHAR* Name)
{
	FElysiumCameraShotDef Def;
	Def.Name = Name;
	return Def;
}

static FElysiumEntityDefs Defs(const TCHAR* MapName, bool bWithWorldSpawn, bool bWithVictim)
{
	FElysiumEntityDefs Out;
	Out.MapName = MapName;
	if (bWithWorldSpawn)
	{
		FElysiumEntityDef World;
		World.Classname = TEXT("worldspawn");
		Out.Defs.Add(MoveTemp(World));
	}
	if (bWithVictim)
	{
		FElysiumEntityDef Victim;
		Victim.Classname = TEXT("npc_VPedestrian");
		Victim.TargetName = TEXT("victim");
		Out.Defs.Add(MoveTemp(Victim));
	}
	return Out;
}

// A bare disposable runtime camera, the entity `FUN_10070550` creates.
static FElysiumCameraCinematic* MakeCamera(FElysiumEntityWorld& World)
{
	FElysiumEntityDef Def;
	Def.Classname = TEXT("camera_cinematic");
	Def.Origin = FVector::ZeroVector;
	FElysiumEntity* Ent = World.Resolve(World.SpawnRuntimeEntity(MoveTemp(Def)));
	FElysiumCameraCinematic* Camera = Ent ? Ent->AsCameraCinematic() : nullptr;
	if (Camera)
	{
		Camera->bDisposable = true;
	}
	return Camera;
}

static FElysiumAnimEvent Event(int32 Id, const TCHAR* Options)
{
	FElysiumAnimEvent Out;
	Out.Event = Id;
	Out.Options = Options;
	return Out;
}

static FElysiumCameraCinematic* AdoptedCamera(FElysiumEntityWorld& World)
{
	FElysiumEntity* Ent = World.Resolve(World.CineCameraEntity());
	return Ent ? Ent->AsCameraCinematic() : nullptr;
}

// The trailing `_<n>` of a chosen shot name, or `INDEX_NONE`.
static int32 ChosenIndex(const FString& ShotName, const TCHAR* BaseName)
{
	const FString Prefix = FString(BaseName) + TEXT("_");
	if (!ShotName.StartsWith(Prefix, ESearchCase::IgnoreCase))
	{
		return INDEX_NONE;
	}
	return FCString::Atoi(*ShotName.Mid(Prefix.Len()));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraFindBestShotTest,
	"Elysium.Substrate.CameraFindBestShot", GFlags)

bool FElysiumCameraFindBestShotTest::RunTest(const FString&)
{
	// The enumeration walks names until one is missing, and a miss is a Warning from the shot
	// loader — the terminator IS the miss, so every case here produces at least one.
	AddExpectedError(TEXT("not found"), EAutomationExpectedErrorFlags::Contains, 0);

	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

	// --- 1. The enumeration stops at the first gap ------------------------------------------------
	//
	// `for (i = 1; ; ++i) { if (SetShot("<base>_<i>")) {...} if (m_ShotIndex == -1) break; }` — a
	// `_1`, `_2`, `_4` family is a two-candidate family, and `_4` is never seen at all.
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("__sc8_gap_1"), Bare(TEXT("__sc8_gap_1")));
		ElysiumCameraShots::Install(TEXT("__sc8_gap_2"), Bare(TEXT("__sc8_gap_2")));
		ElysiumCameraShots::Install(TEXT("__sc8_gap_4"), Bare(TEXT("__sc8_gap_4")));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(Defs(TEXT("__sc8_gap__"), /*bWithWorldSpawn*/ true, /*bWithVictim*/ false));
		World.SpawnPlayer();
		World.Activate(0.0);

		TSet<int32> Picked;
		for (int32 Step = 1; Step <= 24; ++Step)
		{
			FElysiumCameraCinematic* Camera = MakeCamera(World);
			if (!TestNotNull(TEXT("the runtime camera spawns"), Camera))
			{
				return false;
			}
			// Scattered seeds rather than 1, 2, 3 — the assertion is about which shots the SCAN
			// offers, so the draw must be given a fair chance to reach both of them.
			FRandomStream Rng(Step * 104729);
			TestTrue(TEXT("FindBestShot succeeds over a two-candidate family"),
				Camera->FindBestShot(TEXT("__sc8_gap"), Rng));
			Picked.Add(ChosenIndex(Camera->ShotDef.Name, TEXT("__sc8_gap")));
			Camera->Kill();
		}
		TestTrue(TEXT("the scan reaches _1"), Picked.Contains(1));
		TestTrue(TEXT("the scan reaches _2"), Picked.Contains(2));
		// The whole point: the gap at `_3` ends the scan, so `_4` is never enumerated even though
		// the table holds it.
		TestFalse(TEXT("the gap at _3 ends the scan, so _4 is never a candidate"),
			Picked.Contains(4));
		TestEqual(TEXT("and nothing else was chosen"), Picked.Num(), 2);
	}

	// --- 2. The anchor predicate `FUN_1006d9d0` ---------------------------------------------------
	{
		ElysiumCameraShots::FlushCache();
		// A shot whose `Start` declares `World` — with no `worldspawn` in the map there is no entity
		// for it, so the declared anchor has a dead handle.
		ElysiumCameraShots::Install(TEXT("__sc8_orphan"),
			Framed(TEXT("__sc8_orphan"), EElysiumShotPosition::World,
				EElysiumShotPosition::Player));
		ElysiumCameraShots::Install(TEXT("__sc8_bare"), Bare(TEXT("__sc8_bare")));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(Defs(TEXT("__sc8_anchors__"), /*bWithWorldSpawn*/ false, /*bWithVictim*/ false));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumCameraCinematic* Camera = MakeCamera(World);
		if (!TestNotNull(TEXT("the runtime camera spawns"), Camera))
		{
			return false;
		}
		TestTrue(TEXT("the orphan shot loads"),
			Camera->SetShot(TEXT("__sc8_orphan"), 1, FElysiumEntityHandle::Invalid()));
		Camera->StartShotPlacement();
		// `if ((flags & 1) && !handleLive(+0x610)) return false;`
		TestFalse(TEXT("a declared Start with no entity fails the anchor predicate"),
			Camera->AnchorsExist());
		// The visibility predicate is separate and does not save it — but with no live Start/End
		// anchor it has nothing to trace, so it answers true. `FindBestShot` ANDs the two.
		TestTrue(TEXT("and the visibility predicate has nothing to trace"), Camera->CanSeeTarget());

		TestTrue(TEXT("the bare shot loads"),
			Camera->SetShot(TEXT("__sc8_bare"), 1, FElysiumEntityHandle::Invalid()));
		Camera->StartShotPlacement();
		// `flags` with no anchor bits raised: every `if` is skipped and the answer is
		// `m_ShotIndex != -1`.
		TestTrue(TEXT("a shot that declares no anchor passes the predicate trivially"),
			Camera->AnchorsExist());

		// The trailing `m_ShotIndex != -1` term, on its own.
		Camera->ClearMode();
		TestFalse(TEXT("an idle camera has no record to read flags out of"), Camera->AnchorsExist());
	}

	// --- 3. The visibility predicate `FUN_1006db10`, and its subject filter -----------------------
	//
	// `UTIL_TraceHull(anchor -> lookAt, mins (-1,-1,-1), maxs (1,1,1), mask 0x1400b,
	//  CTraceFilterSimple(m_hSubject, 0))`, failing on `fraction < 1 || startsolid || allsolid`.
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("__sc8_vis"),
			Framed(TEXT("__sc8_vis"), EElysiumShotPosition::World, EElysiumShotPosition::Player));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(Defs(TEXT("__sc8_vis__"), /*bWithWorldSpawn*/ true, /*bWithVictim*/ false));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		if (!TestNotNull(TEXT("the fixture has a player"), PlayerEnt))
		{
			return false;
		}
		// The `Start` anchor is `worldspawn` at the origin; the look-at is the player, 10 m away.
		PlayerEnt->Origin = FVector(1000.0f, 0.0f, 0.0f);

		FElysiumCameraCinematic* Camera = MakeCamera(World);
		if (!TestNotNull(TEXT("the runtime camera spawns"), Camera))
		{
			return false;
		}
		// `SetShot(name, 1, NULL)` falls the subject back to player 1, which is what makes the
		// filter below the *player's* exemption.
		TestTrue(TEXT("the visibility shot loads"),
			Camera->SetShot(TEXT("__sc8_vis"), 1, FElysiumEntityHandle::Invalid()));
		Camera->StartShotPlacement();
		TestTrue(TEXT("the subject is the player"), Camera->Subject == PlayerEnt->Handle);
		TestTrue(TEXT("an unobstructed candidate is visible"), Camera->CanSeeTarget());
		TestTrue(TEXT("and the predicate really traced"), Services.Saw(TEXT("TraceCameraHull")));

		// A wall halfway along the segment: `fraction < 1` refuses the candidate.
		FElysiumRecordingServices::FCameraHullBlocker Wall;
		Wall.PointCm = FVector(500.0f, 0.0f, 0.0f);
		Wall.RadiusCm = 32.0f;
		Services.CameraHullBlockers.Add(Wall);
		TestFalse(TEXT("a blocked candidate is refused"), Camera->CanSeeTarget());

		// The same obstruction owned by the shot's SUBJECT: retail's `CTraceFilterSimple` ignores
		// it, so the player's own body never blocks a shot framed on him.
		Services.CameraHullBlockers.Reset();
		Wall.Owner = PlayerEnt->Handle;
		Services.CameraHullBlockers.Add(Wall);
		TestTrue(TEXT("a candidate whose only obstruction is the subject is admitted"),
			Camera->CanSeeTarget());
	}

	// --- 4. The uniform pick is a named stream's, and it is seed-deterministic ---------------------
	{
		ElysiumCameraShots::FlushCache();
		for (int32 Index = 1; Index <= 4; ++Index)
		{
			const FString Name = FString::Printf(TEXT("__sc8_pick_%d"), Index);
			ElysiumCameraShots::Install(Name, Bare(*Name));
		}

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(Defs(TEXT("__sc8_pick__"), /*bWithWorldSpawn*/ true, /*bWithVictim*/ false));
		World.SpawnPlayer();
		World.Activate(0.0);

		auto PickWithSeed = [&World](int32 Seed) -> int32
		{
			FElysiumCameraCinematic* Camera = MakeCamera(World);
			if (!Camera)
			{
				return INDEX_NONE;
			}
			FRandomStream Rng(Seed);
			const bool bFound = Camera->FindBestShot(TEXT("__sc8_pick"), Rng);
			const int32 Chosen = bFound
				? ChosenIndex(Camera->ShotDef.Name, TEXT("__sc8_pick")) : INDEX_NONE;
			Camera->Kill();
			return Chosen;
		};

		constexpr int32 Reference = 7 * 104729;
		const int32 First = PickWithSeed(Reference);
		TestTrue(TEXT("a seeded pick lands inside the candidate set"), First >= 1 && First <= 4);
		TestEqual(TEXT("the same seed picks the same shot"), PickWithSeed(Reference), First);
		// Uniform over four candidates: some seed in a short sweep must land elsewhere.
		bool bDiffered = false;
		for (int32 Step = 1; Step <= 32 && !bDiffered; ++Step)
		{
			bDiffered = PickWithSeed(Step * 104729) != First;
		}
		TestTrue(TEXT("a different seed picks a different shot"), bDiffered);

		// No candidate at all: `FUN_10070550` removes the camera it just made.
		FRandomStream Rng(1);
		const FElysiumEntityHandle Failed = FElysiumCameraCinematic::CreateFindBestShotCamera(
			World, TEXT("__sc8_no_such_family"), Rng);
		TestFalse(TEXT("a base name with no _1 creates nothing"), Failed.IsSet());
		TestNull(TEXT("and leaves no entity behind"),
			static_cast<void*>(World.Resolve(Failed)));
	}

	// --- 5. Anim events 4050 and 4051 --------------------------------------------------------------
	{
		ElysiumCameraShots::FlushCache();
		ElysiumCameraShots::Install(TEXT("__sc8_evt_1"), Bare(TEXT("__sc8_evt_1")));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(Defs(TEXT("__sc8_events__"), /*bWithWorldSpawn*/ true, /*bWithVictim*/ false));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		if (!TestNotNull(TEXT("the fixture has a player"), PlayerEnt))
		{
			return false;
		}
		TestTrue(TEXT("the player is mobile before the event"), PlayerEnt->IsMobile());

		// The event path draws from the session stream rather than a local one, so it is seeded here
		// — the point of the named stream is that a run is reproducible, and a case that inherited
		// whatever position a previous suite left would not be.
		ElysiumRng::Stream(EElysiumRngStream::CameraFindBestShot).Initialize(20260907);

		// **The `options` string is the shot BASE NAME**, not an entity classname — the row
		// `docs/vtmb/animation_events.md` carried before SC8.
		TestTrue(TEXT("4050 is claimed"), PlayerEnt->HandleAnimEvent(Event(4050, TEXT("__sc8_evt"))));

		FElysiumCameraCinematic* Camera = AdoptedCamera(World);
		if (!TestNotNull(TEXT("4050 adopted a camera"), Camera))
		{
			return false;
		}
		TestEqual(TEXT("and it is running the family's only shot"), Camera->ShotDef.Name,
			FString(TEXT("__sc8_evt_1")));
		TestTrue(TEXT("the camera 4050 made is disposable"), Camera->bDisposable);
		// `cam->m_bDrawPlayer (+0x640) = 1` — with a director's `spawnflags & 2`, one of the two
		// non-zero writers in the game, and SC5's body gate is what reads it.
		TestTrue(TEXT("4050 raises m_bDrawPlayer"), Camera->bDrawPlayerBody);
		// `cam->m_bForcePlayerLook (+0x5e8) = 0` — the runtime default is **1** (RG-A), and 4050 is
		// one of only three paths in the game that clear it.
		TestFalse(TEXT("4050 clears point_player"), Camera->bForcePlayerLook);
		TestTrue(TEXT("the goal carries the body gate"),
			Services.LastCameraShot.Presentation.bDrawPlayerBody);
		// **Neither event immobilizes** — only `StartShot` and `StartPlayerDialog` do.
		TestTrue(TEXT("4050 does not immobilize"), PlayerEnt->IsMobile());

		const FElysiumEntityHandle CameraHandle = Camera->Handle;

		// 4051: `SetCineCamera(NULL)` — drop AND destroy — then the flattened-forward eye snap.
		// Source angles are [pitch yaw roll] and the runtime yaw is the reflection of the Source one.
		const FVector SourceAngles(0.0f, -40.0f, 0.0f);
		const float ExpectedYaw = ElysiumSkeletalBasis::FromSourceAngles(SourceAngles).Yaw;
		PlayerEnt->Angles = SourceAngles;
		FRotator Consumed;
		FVector ConsumedPoint;
		PlayerEnt->ConsumePendingEyeAngleSnap(Consumed, ConsumedPoint);   // clear whatever the shot left

		TestTrue(TEXT("4051 is claimed"), PlayerEnt->HandleAnimEvent(Event(4051, TEXT(""))));
		TestFalse(TEXT("4051 drops the cine slot"), World.HasScriptedCamera());
		TestNull(TEXT("and destroys the disposable camera it dropped"),
			static_cast<void*>(World.Resolve(CameraHandle)));
		TestTrue(TEXT("4051 raised the pending eye snap"), PlayerEnt->HasPendingEyeAngleSnap());
		if (PlayerEnt->ConsumePendingEyeAngleSnap(Consumed, ConsumedPoint))
		{
			// `fwd.z = 0` before the normalize is what makes the snap level.
			TestTrue(TEXT("4051 levels the pitch"), FMath::IsNearlyZero(Consumed.Pitch, 0.01f));
			TestTrue(TEXT("4051 faces the player's own yaw"),
				FMath::IsNearlyEqual(FRotator::NormalizeAxis(Consumed.Yaw), ExpectedYaw, 0.01f));
		}
		TestTrue(TEXT("4051 does not immobilize"), PlayerEnt->IsMobile());

		// An empty `options` is retail's `name && *name` guard: claimed, and nothing created.
		TestTrue(TEXT("4050 with no options is still claimed"),
			PlayerEnt->HandleAnimEvent(Event(4050, TEXT(""))));
		TestFalse(TEXT("and adopts nothing"), World.HasScriptedCamera());
	}

	// --- 6. The whole chain over `stealth_kill.txt`, both grapple roles live ------------------------
	//
	// The plan's `StealthKillCamera` witness. `PlayerTryStealthKill` `0x10167370` starts a mode-3
	// grapple with the player as attacker; the stealth-kill clip then fires 4050 with
	// `Stealth_Kill` and, at its end, 4051.
	{
		ElysiumCameraShots::FlushCache();
		TArray<FElysiumCameraShotDef> Shots;
		if (!TestTrue(TEXT("stealth_kill.txt parses"),
			ElysiumCameraShots::ParseAllText(StealthKillText(), Shots)))
		{
			return false;
		}
		TestEqual(TEXT("it carries four shots"), Shots.Num(), 4);
		// One file per name, which is what `SetShot`'s table lookup sees: retail's table is keyed by
		// shot name across every parsed file, so `Stealth_Kill_2` is addressable on its own.
		for (const FElysiumCameraShotDef& Def : Shots)
		{
			ElysiumCameraShots::Install(Def.Name, Def);
		}

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(Defs(TEXT("__sc8_stealth__"), /*bWithWorldSpawn*/ true, /*bWithVictim*/ true));
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);   // deterministic NPC admission, no executor action

		FElysiumPlayer* PlayerEnt = World.FindPlayer();
		FElysiumEntity* VictimEnt = World.FindByName(TEXT("victim"));
		FElysiumCombatCharacter* Victim = VictimEnt ? VictimEnt->AsCombatCharacter() : nullptr;
		if (!TestNotNull(TEXT("the fixture has a player"), PlayerEnt)
			|| !TestNotNull(TEXT("the fixture has a victim"), Victim))
		{
			return false;
		}
		PlayerEnt->Origin = FVector(0.0f, 0.0f, 0.0f);
		VictimEnt->Origin = FVector(120.0f, 0.0f, 0.0f);
		ElysiumRng::Stream(EElysiumRngStream::CameraFindBestShot).Initialize(20260907);

		// `StartGrappleAttack(this, victim, 3)` — RC13's role literals: attacker 0, victim 1.
		TestTrue(TEXT("the stealth-kill pair enters"),
			PlayerEnt->EnterGrapplePair(*Victim, EElysiumGrappleType::StealthKill));
		TestTrue(TEXT("the player holds role 0"),
			PlayerEnt->Grapple.Role == EElysiumGrappleRole::Attacker);
		TestTrue(TEXT("the victim holds role 1"),
			Victim->Grapple.Role == EElysiumGrappleRole::Victim);

		TestTrue(TEXT("4050 is claimed"),
			PlayerEnt->HandleAnimEvent(Event(4050, TEXT("Stealth_Kill"))));
		FElysiumCameraCinematic* Camera = AdoptedCamera(World);
		if (!TestNotNull(TEXT("Stealth_Kill adopted a camera"), Camera))
		{
			return false;
		}
		const int32 Chosen = ChosenIndex(Camera->ShotDef.Name, TEXT("Stealth_Kill"));
		TestTrue(TEXT("the chosen shot is one of Stealth_Kill_1..4"), Chosen >= 1 && Chosen <= 4);
		TestTrue(TEXT("4050 raises m_bDrawPlayer on the stealth-kill camera"),
			Camera->bDrawPlayerBody);
		TestFalse(TEXT("and clears point_player"), Camera->bForcePlayerLook);
		TestTrue(TEXT("the shot runs CamMode 1"), Camera->CamMode ==
			static_cast<int32>(EElysiumCineCamMode::NamedShot));

		// The role resolve, RC13: `Stealth_Kill_1..3` anchor on `GrappleAttacker` — which, with the
		// attacker as subject, is **the subject itself** — and `_4` on `GrappleVictim`, which is
		// the partner.
		const FElysiumEntityHandle Expected = (Chosen == 4) ? VictimEnt->Handle : PlayerEnt->Handle;
		TestTrue(TEXT("the Start anchor resolved to the right half of the pair"),
			Camera->Bindings.Anchors[0].Entity == Expected);
		TestTrue(TEXT("and so did Point1"), Camera->Bindings.Anchors[2].Entity == Expected);
		TestTrue(TEXT("the player is still mobile under the stealth-kill camera"),
			PlayerEnt->IsMobile());

		const FElysiumEntityHandle KillCamera = Camera->Handle;
		TestTrue(TEXT("4051 is claimed"), PlayerEnt->HandleAnimEvent(Event(4051, TEXT(""))));
		TestFalse(TEXT("4051 ends the stealth-kill shot"), World.HasScriptedCamera());
		TestNull(TEXT("and destroys its camera"),
			static_cast<void*>(World.Resolve(KillCamera)));

		// **RC13's other edge**, the one SC4 wired: `CBasePlayer::LeaveGrappleState` `0x10169660`
		// calls `SetCineCamera(NULL)` too, so a shot still up when the grapple ends dies with it —
		// which is how a stealth kill interrupted before its 4051 frame gives the view back.
		TestTrue(TEXT("4050 opens a second shot"),
			PlayerEnt->HandleAnimEvent(Event(4050, TEXT("Stealth_Kill"))));
		FElysiumCameraCinematic* Second = AdoptedCamera(World);
		if (!TestNotNull(TEXT("the second shot adopted"), Second))
		{
			return false;
		}
		const FElysiumEntityHandle SecondHandle = Second->Handle;
		PlayerEnt->LeaveGrappleState();
		TestFalse(TEXT("leaving the grapple ends a live scripted shot"), World.HasScriptedCamera());
		TestNull(TEXT("and destroys the disposable camera under it"),
			static_cast<void*>(World.Resolve(SecondHandle)));
		TestTrue(TEXT("the grapple block is cleared"),
			PlayerEnt->Grapple.Role == EElysiumGrappleRole::None);
	}

	return true;
}

}   // namespace ElysiumFindBestShotTests

#endif // WITH_DEV_AUTOMATION_TESTS
