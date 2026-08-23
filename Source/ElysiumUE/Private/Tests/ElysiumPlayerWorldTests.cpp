// Content-free Substrate automation: player entity, teleport, embodiment, targeting, map teardown, and story-skip contracts.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed (CCC7)
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumAnimationIntent.h"          // the jump latch and classifier the cast poses from
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules (CCC1)
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumPlayerWorldTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// The player entity (11.4, S3). The claim under test is that the player stopped being a
// special case: it is a registry class on VtMB's own chain, it answers to a targetname the
// maps already write (`!player`), its inputs arrive through the same R2 walk from either
// direction, it is a real `!activator`, and `point_teleport` moves it exactly as it moves
// anything else. No RHI, no actors, no `$ELYSIUM_EXPORT_ROOT` — the recording stub is the body.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerEntityTest, "Elysium.Substrate.PlayerEntity", GElysiumTestFlags)
bool FElysiumPlayerEntityTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("base_NotAStat"), EAutomationExpectedErrorFlags::Contains, 1);
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// --- The chain is VtMB's ------------------------------------------------------------
	const FElysiumClassDesc* PlayerDesc = Reg.Find(ElysiumPlayerClassName());
	if (!TestNotNull(TEXT("`player` is registered"), PlayerDesc))
	{
		return false;
	}
	TestEqual(TEXT("player's base is CBaseCombatCharacter"),
		PlayerDesc->BaseName.ToString(), ElysiumCombatCharacterClassName().ToString());
	const FElysiumClassDesc* CharDesc = Reg.Find(ElysiumCombatCharacterClassName());
	const FElysiumClassDesc* AnimDesc = Reg.Find(ElysiumAnimatingClassName());
	if (!TestNotNull(TEXT("CBaseCombatCharacter is registered"), CharDesc) ||
		!TestNotNull(TEXT("CBaseAnimating is registered"), AnimDesc))
	{
		return false;
	}
	TestEqual(TEXT("combat character's base is CBaseAnimating"),
		CharDesc->BaseName.ToString(), ElysiumAnimatingClassName().ToString());
	TestEqual(TEXT("animating's base is CBaseEntity"),
		AnimDesc->BaseName.ToString(), ElysiumBaseClassName().ToString());

	// One walk from the leaf reaches all four levels of the chain.
	auto Resolves = [&Reg, PlayerDesc](const TCHAR* Input)
	{
		return reinterpret_cast<const void*>(Reg.FindInput(*PlayerDesc, FName(Input)));
	};
	TestNotNull(TEXT("player input GiveItem resolves"), Resolves(TEXT("GiveItem")));
	TestNotNull(TEXT("combat-character input MoneyAdd resolves"), Resolves(TEXT("MoneyAdd")));
	TestNotNull(TEXT("animating input SetAnimation resolves"), Resolves(TEXT("SetAnimation")));
	TestNotNull(TEXT("base input Kill resolves"), Resolves(TEXT("Kill")));
	TestNotNull(TEXT("input names fold case"), Resolves(TEXT("moneyadd")));
	// The NPC inherits the same middle nodes — one MoneyAdd for every character in the game.
	if (const FElysiumClassDesc* NpcDesc = Reg.Find(FName(TEXT("npc_VVampire"))))
	{
		TestEqual(TEXT("npc_* sits under CBaseCombatCharacter"),
			NpcDesc->BaseName, ElysiumCombatCharacterClassName());
		TestNotNull(TEXT("an NPC resolves MoneyAdd through the same node"),
			reinterpret_cast<const void*>(Reg.FindInput(*NpcDesc, FName(TEXT("MoneyAdd")))));
	}

	// --- A world with a player ----------------------------------------------------------
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector(100, 200, 30);
	Services.PlayerRotation = FRotator(0.f, 90.f, 0.f);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__player_test__");

	// A trigger whose OnStartTouch fires at `!activator` — the activator must be the player.
	FElysiumEntityDef Trig;
	Trig.Classname = TEXT("trigger_multiple");
	Trig.TargetName = TEXT("trig1");
	Trig.Keys.Add(TEXT("spawnflags"), TEXT("1"));   // ALLOW_CLIENTS
	{
		FElysiumOutputDef W;
		W.Name = TEXT("OnStartTouch");
		W.Target = TEXT("!activator");
		W.Input = TEXT("MoneyAdd");
		W.Param = TEXT("7");
		Trig.Outputs.Add(W);
	}
	Defs.Defs.Add(MoveTemp(Trig));

	// A point_teleport aimed at `!player` by name, exactly as 48 of the 49 in the shipped maps are.
	FElysiumEntityDef Tele;
	Tele.Classname = TEXT("point_teleport");
	Tele.TargetName = TEXT("tp1");
	Tele.Origin = FVector(500, 600, 70);
	Tele.Keys.Add(TEXT("target"), TEXT("!player"));
	Tele.Keys.Add(TEXT("angles"), TEXT("10 45 5"));
	Defs.Defs.Add(MoveTemp(Tele));

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));

	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("SpawnPlayer created a player entity"), Player))
	{
		return false;
	}
	TestEqual(TEXT("the world reports the same handle"), World.PlayerHandle().Index, PlayerHandle.Index);
	TestEqual(TEXT("it answers to `!player` by name"),
		World.FindByName(ElysiumPlayerTargetName()), static_cast<FElysiumEntity*>(Player));
	TestFalse(TEXT("it is a registered class, not an inert record"), Player->IsRecordOnly());
	TestEqual(TEXT("a second SpawnPlayer is a no-op"), World.SpawnPlayer().Index, PlayerHandle.Index);
	TestTrue(TEXT("Spawn sampled the body's origin"), Player->Origin.Equals(FVector(100, 200, 30)));

	// This world has no game state, so no rulebook reached Spawn and the sheet stayed at zero —
	// which is the fail-closed posture, not a bug: a character with no health model does not die of
	// arithmetic. Seed the ceiling by hand for the damage assertions below. `Elysium.Content.Sheet`
	// is where the real `stats.txt` seed (a flat 100) is checked.
	TestEqual(TEXT("with no rulebook the sheet has no health track"), Player->MaxHealth, 0);
	Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
	Player->SyncHealthFromSheet();
	TestEqual(TEXT("the health keyfield is derived from the sheet's ceiling"), Player->MaxHealth, 100);
	TestEqual(TEXT("...with no damage taken yet"), Player->Health, 100);

	// --- Both directions land on the same field ------------------------------------------
	// (a) the console / Hammer-wire direction: address it by targetname.
	World.EnqueueInput(ElysiumPlayerTargetName(), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(50),
		0.0, FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestEqual(TEXT("ent_fire !player MoneyAdd 50"), Player->Money, 50);

	// (b) the script direction: a bound input fires at `!self` with the entity as caller, which is
	//     exactly what `pc.MoneyAdd(50)` manufactures in the CPython host.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(50),
		0.0, FElysiumEntityHandle::Invalid(), PlayerHandle);
	World.Tick(0.0);
	TestEqual(TEXT("pc.MoneyAdd(50) lands on the same field"), Player->Money, 100);

	// VtMB's own no-op rule: a zero-valued MoneyAdd changes nothing.
	World.EnqueueInput(ElysiumPlayerTargetName(), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(0),
		0.0, FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestEqual(TEXT("a zero-valued MoneyAdd is a silent no-op"), Player->Money, 100);

	// --- `!activator` is real -------------------------------------------------------------
	FElysiumEntity* Trigger = World.FindByName(TEXT("trig1"));
	if (!TestNotNull(TEXT("trigger resolved"), Trigger))
	{
		return false;
	}
	World.RouteBrushTouch(Trigger->Handle, PlayerHandle, /*bBegin*/ true);
	World.Tick(0.0);
	TestEqual(TEXT("a trigger the player walked into resolves !activator to it"), Player->Money, 107);

	// --- point_teleport moves it like any other entity ------------------------------------
	FElysiumEntity* Teleport = World.FindByName(TEXT("tp1"));
	if (!TestNotNull(TEXT("point_teleport resolved"), Teleport))
	{
		return false;
	}
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Teleport->Handle);
	World.Tick(0.0);
	TestTrue(TEXT("the entity's own origin moved"), Player->Origin.Equals(FVector(500, 600, 70)));
	// The body followed through the embodiment — the Source yaw is negated on the way out.
	TestTrue(TEXT("the body was placed at the destination"),
		Services.Saw(TEXT("TeleportPlayer")) && Services.PlayerLocation.Equals(FVector(500, 600, 70)));
	TestTrue(TEXT("the body took the destination's facing"),
		Services.PlayerRotation.Equals(FRotator(-10.f, -45.f, 5.f)));

	// --- Damage, the unkillable latch, and the death path ---------------------------------
	// The number lands on the sheet's `Health` slot, which counts damage TAKEN (RE24); the entity's
	// `health` keyfield is the engine-space projection of that against `Max_Health`.
	Player->TakeDamage(40.f);
	TestEqual(TEXT("damage accumulates on the sheet's damage slot"),
		Player->Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health), 40);
	TestEqual(TEXT("...and the health keyfield follows it down"), Player->Health, 60);
	Player->SetUnkillable(true);
	Player->TakeDamage(1000.f);
	// The unkillable ceiling is retail's literal cap on the damage-TAKEN counter (75), not a
	// one-hit-point floor: with the default Max_Health of 100 it leaves 25.
	TestEqual(TEXT("an unkillable player stops at the literal damage cap"), Player->Health, 25);
	Player->SetUnkillable(false);
	Player->TakeDamage(25.f);
	TestEqual(TEXT("health runs out"), Player->Health, 0);

	// --- Hydrate / dehydrate is the map boundary ------------------------------------------
	Player->Money = 250;
	Player->Sheet.SetClan(FElysiumSheet::ClanFromName(TEXT("Malkavian")));
	Player->Sheet.SetBase(EElysiumTraitContainer::Disciplines, /*Celerity*/ 3, 3);
	FElysiumPlayerRecord Record;
	Player->Dehydrate(Record);
	TestEqual(TEXT("dehydrate carries money"), Record.Money, 250);
	TestEqual(TEXT("dehydrate carries the clan"), Record.Sheet.Clan(), 4);

	FElysiumPlayer Fresh;
	Fresh.Hydrate(Record);
	TestEqual(TEXT("hydrate restores money"), Fresh.Money, 250);
	TestEqual(TEXT("hydrate restores the sheet"),
		Fresh.Sheet.GetCurrent(EElysiumTraitContainer::Disciplines, 3), 3);

	// --- The sheet reads as a registered field, and the bag holds what is left -------------
	// A script read resolves the field table BEFORE the dynamic hook (`Entity_getattro`), so a
	// compiled slot never reaches the bag — that ordering is what turns `pc.base_Celerity` from a
	// default-0 rescue into the real rating.
	{
		const FElysiumFieldAccessor* Celerity = Reg.FindField(*Player->Class, FName(TEXT("base_celerity")));
		if (TestNotNull(TEXT("base_celerity resolves as a datamap field"),
			reinterpret_cast<const void*>(Celerity)))
		{
			TestEqual(TEXT("...reading the slot the sheet holds"), Celerity->Get(*Player).ToInt(), 3);
		}
	}
	Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::HealthBuffer, 80);
	{
		const FElysiumFieldAccessor* BaseHealthBuffer =
			Reg.FindField(*Player->Class, FName(TEXT("base_health_buffer")));
		if (TestNotNull(TEXT("retail base_health_buffer resolves as a datamap field"),
			reinterpret_cast<const void*>(BaseHealthBuffer)))
		{
			TestEqual(TEXT("...reading the Blood Shield absorption pool"),
				BaseHealthBuffer->Get(*Player).ToInt(), 80);
		}
		const FElysiumFieldAccessor* HealthBuffer =
			Reg.FindField(*Player->Class, FName(TEXT("health_buffer")));
		if (TestNotNull(TEXT("retail health_buffer resolves as a datamap field"),
			reinterpret_cast<const void*>(HealthBuffer)))
		{
			TestEqual(TEXT("...reading the current Blood Shield absorption pool"),
				HealthBuffer->Get(*Player).ToInt(), 80);
		}
	}
	FElysiumVariant Dynamic;
	TestTrue(TEXT("a base_ name no slot owns resolves to 0 rather than raising"),
		Player->GetDynamicField(FName(TEXT("base_NotAStat")), Dynamic));
	TestEqual(TEXT("...as zero"), Dynamic.ToInt(), 0);
	TestFalse(TEXT("an unrelated name does not resolve dynamically"),
		Player->GetDynamicField(FName(TEXT("SetExpression")), Dynamic));

	// --- A world with no player is a legal world ------------------------------------------
	{
		FElysiumEntityDefs Bare;
		Bare.MapName = TEXT("__backdrop__");
		FElysiumEntityWorld Backdrop(nullptr, nullptr);
		Backdrop.Load(MoveTemp(Bare));
		Backdrop.Activate(0.0);
		TestNull(TEXT("a map built without a player has none"), Backdrop.FindPlayer());
		// And an input addressed at one is an unknown target, not a crash.
		Backdrop.EnqueueInput(ElysiumPlayerTargetName(), FName(TEXT("MoneyAdd")), FElysiumVariant::Int(1),
			0.0, FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		Backdrop.Tick(0.0);
		TestTrue(TEXT("it is reported as an unknown target"), Backdrop.UnknownTargets() > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPointTeleportContractTest,
	"Elysium.Substrate.PointTeleportContract", GElysiumTestFlags)
bool FElysiumPointTeleportContractTest::RunTest(const FString&)
{
	auto BuildNormalDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__point_teleport_contract__");
		FElysiumEntityDef Tele;
		Tele.Classname = TEXT("point_teleport");
		Tele.TargetName = TEXT("tp");
		Tele.Origin = FVector(100.f, 200.f, 300.f);
		Tele.Keys.Add(TEXT("angles"), TEXT("10 20 30"));
		Tele.Keys.Add(TEXT("target"), TEXT("!player"));
		Defs.Defs.Add(MoveTemp(Tele));
		return Defs;
	};
	auto CountBodyTeleports = [](const FElysiumRecordingServices& Services)
	{
		int32 Count = 0;
		for (const FString& Call : Services.Calls)
		{
			Count += Call.StartsWith(TEXT("TeleportPlayer ")) ? 1 : 0;
		}
		return Count;
	};

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector(1.f, 2.f, 3.f);
	Services.PlayerRotation = FRotator(4.f, 5.f, 6.f);
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(BuildNormalDefs());
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Teleport = World.FindByName(TEXT("tp"));
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("teleporter resolved"), Teleport)
		|| !TestNotNull(TEXT("player resolved"), Player))
	{
		return false;
	}

	// The destination is sampled once in Activate. Moving the point afterwards cannot drag it,
	// and the player's origin plus all Source angles reach the body through one atomic call.
	Teleport->SetRuntimeTransform(FVector(900.f, 901.f, 902.f), FVector(40.f, 50.f, 60.f));
	Services.Calls.Reset();
	World.EnqueueInput(TEXT("tp"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		PlayerHandle, PlayerHandle);
	World.Tick(0.0);
	TestTrue(TEXT("moved point retains its activation-time destination"),
		Player->Origin.Equals(FVector(100.f, 200.f, 300.f))
		&& Player->Angles.Equals(FVector(10.f, 20.f, 30.f)));
	TestEqual(TEXT("origin and all angles produce one body update"), CountBodyTeleports(Services), 1);
	TestTrue(TEXT("the body receives converted pitch yaw and roll"),
		Services.PlayerRotation.Equals(FRotator(-10.f, -20.f, 30.f)));

	Player->MoveParent = Teleport->Handle;
	Services.Calls.Reset();
	AddExpectedError(TEXT("can't teleport object"), EAutomationExpectedErrorFlags::Contains, 1);
	World.EnqueueInput(TEXT("tp"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		PlayerHandle, PlayerHandle);
	World.Tick(0.0);
	TestEqual(TEXT("a parented target receives no body update"), CountBodyTeleports(Services), 0);
	Player->MoveParent = FElysiumEntityHandle::Invalid();

	// !activator resolves when the input arrives, not during activation.
	FElysiumEntityDef ActivatorTeleport;
	ActivatorTeleport.Classname = TEXT("point_teleport");
	ActivatorTeleport.TargetName = TEXT("tp_activator");
	ActivatorTeleport.Origin = FVector(111.f, 222.f, 333.f);
	ActivatorTeleport.Keys.Add(TEXT("angles"), TEXT("7 8 9"));
	ActivatorTeleport.Keys.Add(TEXT("target"), TEXT("!activator"));
	const FElysiumEntityHandle ActivatorTeleportHandle =
		World.SpawnRuntimeEntity(MoveTemp(ActivatorTeleport));
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		PlayerHandle, ActivatorTeleportHandle);
	World.Tick(0.0);
	TestTrue(TEXT("!activator resolves at delivery"),
		Player->Origin.Equals(FVector(111.f, 222.f, 333.f))
		&& Player->Angles.Equals(FVector(7.f, 8.f, 9.f)));

	// An unresolved named target remains live and uses the cache when that target appears later.
	FElysiumEntityDef DeferredTeleport;
	DeferredTeleport.Classname = TEXT("point_teleport");
	DeferredTeleport.TargetName = TEXT("tp_deferred");
	DeferredTeleport.Origin = FVector(444.f, 555.f, 666.f);
	DeferredTeleport.Keys.Add(TEXT("angles"), TEXT("14 15 16"));
	DeferredTeleport.Keys.Add(TEXT("target"), TEXT("late_target"));
	const FElysiumEntityHandle DeferredTeleportHandle =
		World.SpawnRuntimeEntity(MoveTemp(DeferredTeleport));
	FElysiumEntity* Deferred = World.Resolve(DeferredTeleportHandle);
	TestTrue(TEXT("an unresolved named target does not kill the teleporter"), Deferred && !Deferred->IsDead());
	FElysiumEntityDef LateTarget;
	LateTarget.Classname = TEXT("info_landmark");
	LateTarget.TargetName = TEXT("late_target");
	const FElysiumEntityHandle LateHandle = World.SpawnRuntimeEntity(MoveTemp(LateTarget));
	World.EnqueueInput(TEXT("tp_deferred"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		PlayerHandle, PlayerHandle);
	World.Tick(0.0);
	FElysiumEntity* Late = World.Resolve(LateHandle);
	TestTrue(TEXT("the late target uses the activation-time cache"), Late
		&& Late->Origin.Equals(FVector(444.f, 555.f, 666.f))
		&& Late->Angles.Equals(FVector(14.f, 15.f, 16.f)));

	FElysiumEntityDef EmptyTeleport;
	EmptyTeleport.Classname = TEXT("point_teleport");
	EmptyTeleport.TargetName = TEXT("tp_empty");
	AddExpectedError(TEXT("given no target. Deleted"), EAutomationExpectedErrorFlags::Contains, 1);
	const FElysiumEntityHandle EmptyHandle = World.SpawnRuntimeEntity(MoveTemp(EmptyTeleport));
	TestNull(TEXT("an empty target kills the teleporter"), World.Resolve(EmptyHandle));

	// Spawnflag 1 samples the synchronized player transform during frozen activation.
	FElysiumRecordingServices HomeServices;
	HomeServices.bHasPlayer = true;
	HomeServices.PlayerLocation = FVector(21.f, 22.f, 23.f);
	HomeServices.PlayerRotation = FRotator(4.f, 5.f, 6.f);
	FElysiumEntityDefs HomeDefs;
	HomeDefs.MapName = TEXT("__point_teleport_home__");
	FElysiumEntityDef HomeTeleport;
	HomeTeleport.Classname = TEXT("point_teleport");
	HomeTeleport.TargetName = TEXT("home");
	HomeTeleport.Origin = FVector(700.f, 800.f, 900.f);
	HomeTeleport.Keys.Add(TEXT("target"), TEXT("!player"));
	HomeTeleport.Keys.Add(TEXT("spawnflags"), TEXT("1"));
	HomeDefs.Defs.Add(MoveTemp(HomeTeleport));
	FElysiumEntityWorld HomeWorld(nullptr, nullptr, HomeServices.Bundle());
	HomeWorld.Load(MoveTemp(HomeDefs));
	const FElysiumEntityHandle HomePlayerHandle = HomeWorld.SpawnPlayer();
	HomeWorld.Activate(0.0);
	FElysiumPlayer* HomePlayer = HomeWorld.FindPlayer();
	HomePlayer->SetRuntimeTransform(FVector(80.f, 90.f, 100.f), FVector(1.f, 2.f, 3.f));
	HomeWorld.EnqueueInput(TEXT("home"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		HomePlayerHandle, HomePlayerHandle);
	HomeWorld.Tick(0.0);
	TestTrue(TEXT("spawnflag 1 returns to activation-time player feet and view"),
		HomePlayer->Origin.Equals(FVector(21.f, 22.f, 23.f))
		&& HomePlayer->Angles.Equals(ElysiumPlayerView::ToSource(FRotator(4.f, 5.f, 6.f))));

	// The activation cache stays explicit in snapshots. An old record with no leaf recomputes from
	// the restored live point transform during Activate.
	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);
	const FElysiumEntityState* TeleportState = Snapshot.Entities.FindByPredicate(
		[](const FElysiumEntityState& State) { return State.TargetName == TEXT("tp"); });
	TestTrue(TEXT("the activation cache is serialized"),
		TeleportState && !TeleportState->LeafState.IsEmpty());

	FElysiumRecordingServices RestoredServices;
	RestoredServices.bHasPlayer = true;
	RestoredServices.PlayerLocation = FVector(9.f, 9.f, 9.f);
	FElysiumEntityWorld Restored(nullptr, nullptr, RestoredServices.Bundle());
	Restored.Load(BuildNormalDefs());
	const FElysiumEntityHandle RestoredPlayer = Restored.SpawnPlayer();
	Restored.ApplySnapshot(Snapshot);
	Restored.Activate(0.0);
	Restored.EnqueueInput(TEXT("tp"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		RestoredPlayer, RestoredPlayer);
	Restored.Tick(0.0);
	TestTrue(TEXT("restore retains the original activation cache"),
		Restored.FindPlayer()->Origin.Equals(FVector(100.f, 200.f, 300.f))
		&& Restored.FindPlayer()->Angles.Equals(FVector(10.f, 20.f, 30.f)));

	FElysiumMapSnapshot Legacy = Snapshot;
	if (FElysiumEntityState* LegacyTeleport = Legacy.Entities.FindByPredicate(
		[](FElysiumEntityState& State) { return State.TargetName == TEXT("tp"); }))
	{
		LegacyTeleport->LeafState.Reset();
	}
	FElysiumRecordingServices LegacyServices;
	LegacyServices.bHasPlayer = true;
	LegacyServices.PlayerLocation = FVector(8.f, 8.f, 8.f);
	FElysiumEntityWorld LegacyWorld(nullptr, nullptr, LegacyServices.Bundle());
	LegacyWorld.Load(BuildNormalDefs());
	const FElysiumEntityHandle LegacyPlayer = LegacyWorld.SpawnPlayer();
	LegacyWorld.ApplySnapshot(Legacy);
	LegacyWorld.Activate(0.0);
	LegacyWorld.EnqueueInput(TEXT("tp"), FName(TEXT("Teleport")), FElysiumVariant::Void(), 0.0,
		LegacyPlayer, LegacyPlayer);
	LegacyWorld.Tick(0.0);
	TestTrue(TEXT("an older record recomputes from restored live transform"),
		LegacyWorld.FindPlayer()->Origin.Equals(FVector(900.f, 901.f, 902.f))
		&& LegacyWorld.FindPlayer()->Angles.Equals(FVector(40.f, 50.f, 60.f)));
	return true;
}

// =====================================================================================
// Opening-embrace embodiment: the controller is a real map-epoch entity, and prop_dynamic
// selects the generated v4 skeletal representation only for indexed models.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAnimatedPropManifestTest,
	"Elysium.Substrate.AnimatedPropManifest", GElysiumTestFlags)
bool FElysiumAnimatedPropManifestTest::RunTest(const FString&)
{
	const FString MinimalNpc = TEXT("\"npcs\":{\"dummy\":{"
		"\"model\":\"models/dummy.mdl\",\"clips\":1,\"split_bones\":[\"Bip01 Spine1\"]}}");
	FString Error;
	FElysiumNpcIndex V3;
	const FString Json3 = FString::Printf(TEXT("{\"manifest_version\":3,%s,\"banks\":{},\"cinematics\":{}}"), *MinimalNpc);
	TestTrue(FString::Printf(TEXT("v3 manifest parses: %s"), *Error), V3.LoadJsonText(Json3, Error));
	TestEqual(TEXT("v3 version retained"), V3.ManifestVersion, 3);
	TestTrue(TEXT("v3 reads animated_props as empty"), V3.AnimatedProps.IsEmpty());
	TestEqual(TEXT("optional split-bone metadata is accepted in a v3 index"),
		V3.Npcs[TEXT("dummy")].SplitRotationBones.Num(), 1);
	TestEqual(TEXT("split-bone name is retained"),
		V3.Npcs[TEXT("dummy")].SplitRotationBones[0], FString(TEXT("Bip01 Spine1")));
	TestNull(TEXT("v3 resolves no animated prop"),
		V3.FindAnimatedProp(TEXT("models/cinematic/cin_wineglass.mdl")));

	FElysiumNpcIndex V4;
	const FString Json4 = FString::Printf(TEXT("{\"manifest_version\":4,%s,\"banks\":{},\"cinematics\":{},"
		"\"animated_props\":{\"cin_wineglass\":{"
		"\"model\":\"models/cinematic/cin_wineglass.mdl\",\"bones\":4,"
		"\"split_bones\":[\"glass hinge\"],\"clips\":[\"Idle\",\"Pour\"]}}}"),
		*MinimalNpc);
	Error.Reset();
	TestTrue(FString::Printf(TEXT("v4 manifest parses: %s"), *Error), V4.LoadJsonText(Json4, Error));
	TestEqual(TEXT("v4 version retained"), V4.ManifestVersion, 4);
	const FElysiumAnimatedPropEntry* Glass =
		V4.FindAnimatedProp(TEXT("cinematic\\cin_wineglass.mdl"));
	if (TestNotNull(TEXT("v4 normalizes and resolves animated prop model"), Glass))
	{
		TestTrue(TEXT("clip lookup folds case"), Glass->HasClip(TEXT("POUR")));
		TestEqual(TEXT("clip inventory retained"), Glass->Clips.Num(), 2);
		TestEqual(TEXT("animated-prop split-bone metadata is retained"),
			Glass->SplitRotationBones.Num(), 1);
	}

	// v5 adds the eyeball sidecar. It rides on its own fields rather than on the flex rig's,
	// because a player body carries eyeballs and no flex data at all — so `eyes` present with
	// `facial` absent is the shipped state for 57 of the 59 of them, not a malformed row.
	FElysiumNpcIndex V5;
	Error.Reset();
	const FString Json5 = FString::Printf(
		TEXT("{\"manifest_version\":5,\"npcs\":{\"dummy\":{"
			 "\"model\":\"models/dummy.mdl\",\"clips\":1,"
			 "\"eyes\":\"eyes/dummy.json\",\"eyeballs\":2}},\"banks\":{},\"cinematics\":{}}"));
	TestTrue(FString::Printf(TEXT("v5 manifest parses: %s"), *Error), V5.LoadJsonText(Json5, Error));
	TestEqual(TEXT("v5 version retained"), V5.ManifestVersion, 5);
	TestEqual(TEXT("v5 eyeball sidecar path is retained"),
		V5.Npcs[TEXT("dummy")].Eyes, FString(TEXT("eyes/dummy.json")));
	TestEqual(TEXT("v5 eyeball count is retained"), V5.Npcs[TEXT("dummy")].EyeballCount, 2);
	TestTrue(TEXT("eyeballs do not imply a flex rig"), V5.Npcs[TEXT("dummy")].Facial.IsEmpty());

	// An older index simply carries no eye fields, which is a model with no eyeballs rather
	// than a parse failure.
	TestTrue(TEXT("v3 index reads no eyeball sidecar"), V3.Npcs[TEXT("dummy")].Eyes.IsEmpty());

	// v6 turns `clips` from a sorted name array into the model's own sequence table, in declaration
	// order, with the selection keys beside each label. Order is what retail's index-0 rest-pose
	// fallback selects on, so the alphabetically-first clip must NOT win: this table is
	// `drknobantique`'s real shape, where sorting would stand the knob on `handle_locked`.
	FElysiumNpcIndex V6;
	Error.Reset();
	const FString Json6 = FString::Printf(TEXT("{\"manifest_version\":6,%s,\"banks\":{},\"cinematics\":{},"
		"\"animated_props\":{\"drknobantique\":{"
		"\"model\":\"models/scenery/doorknoba/drknobantique.mdl\",\"bones\":2,\"clips\":["
		"{\"name\":\"idle\",\"index\":0,\"activity\":\"\",\"weight\":0,\"flags\":1,\"frames\":16,\"fps\":15.0},"
		"{\"name\":\"handle_locked\",\"index\":1,\"activity\":\"\",\"weight\":0,\"flags\":0,\"frames\":16,\"fps\":15.0},"
		"{\"name\":\"handle_unlocked\",\"index\":2,\"activity\":\"\",\"weight\":0,\"flags\":0,\"frames\":16,\"fps\":15.0}"
		"]}}}"), *MinimalNpc);
	TestTrue(FString::Printf(TEXT("v6 manifest parses: %s"), *Error), V6.LoadJsonText(Json6, Error));
	TestEqual(TEXT("v6 version retained"), V6.ManifestVersion, 6);
	const FElysiumAnimatedPropEntry* Knob =
		V6.FindAnimatedProp(TEXT("models/scenery/doorknoba/drknobantique.mdl"));
	if (TestNotNull(TEXT("v6 resolves the animated prop"), Knob))
	{
		TestEqual(TEXT("v6 keeps declaration order"), Knob->Clips[0].Name, FString(TEXT("idle")));
		TestEqual(TEXT("v6 rest sequence is the declared first, not the alphabetical first"),
			Knob->RestSequence(), FString(TEXT("idle")));
		const FElysiumPropClip* Idle = Knob->FindClip(TEXT("IDLE"));
		if (TestNotNull(TEXT("v6 clip lookup folds case"), Idle))
		{
			TestTrue(TEXT("STUDIO_LOOPING is read off bit 0"), Idle->IsLooping());
			TestEqual(TEXT("v6 carries the declared ordinal"), Idle->Index, 0);
		}
		const FElysiumPropClip* Locked = Knob->FindClip(TEXT("handle_locked"));
		if (TestNotNull(TEXT("v6 resolves a later clip"), Locked))
		{
			TestFalse(TEXT("a non-looping clip reads as one shot"), Locked->IsLooping());
		}
	}

	// `bounds_radius_m` is additive and optional. Its presence states the reach the clip needs
	// about the model origin, in the decode's own metres; its absence is "no claim", not
	// "zero reach". These are `cin_sheriff_sword`'s real numbers — a 2 m mesh whose scene clip
	// draws it up to 22 m from the anchor it is culled on.
	FElysiumNpcIndex Reach;
	Error.Reset();
	const FString JsonReach = FString::Printf(TEXT("{\"manifest_version\":6,%s,\"banks\":{},\"cinematics\":{},"
		"\"animated_props\":{\"cin_sheriff_sword\":{"
		"\"model\":\"models/cinematic/santa_monica/courtroom/cin_sheriff_sword.mdl\",\"bones\":15,\"clips\":["
		"{\"name\":\"idle01\",\"index\":0,\"frames\":4701,\"fps\":10.0,\"bounds_radius_m\":22.388},"
		"{\"name\":\"scene\",\"index\":1,\"frames\":4701,\"fps\":30.0}"
		"]}}}"), *MinimalNpc);
	TestTrue(FString::Printf(TEXT("an index carrying a clip reach parses: %s"), *Error),
		Reach.LoadJsonText(JsonReach, Error));
	const FElysiumAnimatedPropEntry* Sword =
		Reach.FindAnimatedProp(TEXT("models/cinematic/santa_monica/courtroom/cin_sheriff_sword.mdl"));
	if (TestNotNull(TEXT("the sword resolves"), Sword))
	{
		TestEqual(TEXT("a declared clip reach is read in the decode's own metres"),
			Sword->Clips[0].BoundsRadiusMeters, 22.388f);
		TestEqual(TEXT("a clip that declares no reach makes no claim"),
			Sword->Clips[1].BoundsRadiusMeters, 0.f);
	}

	// The v4 shape above still parses, and its bare names land as ordered rows with no selection
	// keys — an index that predates the re-export keeps working, it just has no loop flags.
	if (Glass)
	{
		TestEqual(TEXT("v4 names land in array order"), Glass->Clips[0].Name, FString(TEXT("Idle")));
		TestEqual(TEXT("v4 rest sequence falls back to the first name"),
			Glass->RestSequence(), FString(TEXT("Idle")));
		TestEqual(TEXT("a v4 row declares no clip reach"), Glass->Clips[0].BoundsRadiusMeters, 0.f);
	}

	FElysiumNpcIndex V7;
	Error.Reset();
	const FString Json7 = FString::Printf(TEXT("{\"manifest_version\":7,%s,\"banks\":{},"
		"\"cinematics\":{},\"placed_models\":{\"catalogue_test\":{"
		"\"eskm\":\"placed_models/catalogue_test.eskm\",\"model\":\"models/test.mdl\","
		"\"static_stem\":\"models_test\",\"clip_mode\":\"rest\","
		"\"static_equivalent\":false,\"rest_candidates\":[\"idle_a\",\"idle_b\"],\"clips\":["
		"{\"name\":\"idle_a\",\"index\":0,\"activity\":\"ACT_IDLE\",\"weight\":5},"
		"{\"name\":\"idle_b\",\"index\":1,\"activity\":\"ACT_IDLE\",\"weight\":5}]}}}"),
		*MinimalNpc);
	TestTrue(FString::Printf(TEXT("v7 manifest parses: %s"), *Error), V7.LoadJsonText(Json7, Error));
	const FElysiumAnimatedPropEntry* Placed = V7.FindPlacedModel(TEXT("test.mdl"));
	if (TestNotNull(TEXT("v7 resolves a complete placed model"), Placed))
	{
		TestEqual(TEXT("v7 keeps the skeletal catalogue stem distinct"), Placed->Stem,
			FString(TEXT("catalogue_test")));
		TestEqual(TEXT("v7 retains its static material/collision stem"), Placed->StaticStem,
			FString(TEXT("models_test")));
		TestFalse(TEXT("the row is conservatively skeletal"), Placed->bStaticEquivalent);
		TestEqual(TEXT("placement token 0 selects the second weighted half"),
			Placed->RestSequence(0), FString(TEXT("idle_b")));
		TestEqual(TEXT("placement token 7 selects the first weighted half"),
			Placed->RestSequence(7), FString(TEXT("idle_a")));
	}

	// v8 carries the sequence-event rollup beside the grid count. Both are counts over the SAME
	// sidecar, and a model authoring events and no grid is ordinary — so the two are read
	// independently rather than one gating the other.
	FElysiumNpcIndex V8;
	Error.Reset();
	const FString Json8 = TEXT("{\"manifest_version\":8,"
		"\"npcs\":{\"dummy\":{\"model\":\"models/dummy.mdl\",\"clips\":1,"
		"\"blends\":\"blends/dummy.json\",\"blend_grids\":0,\"event_sequences\":11}},"
		"\"banks\":{\"bank\":{\"model\":\"models/bank.mdl\","
		"\"clips\":3,\"blends\":\"blends/bank.json\",\"blend_grids\":2,"
		"\"event_sequences\":7}},\"cinematics\":{}}");
	TestTrue(FString::Printf(TEXT("v8 manifest parses: %s"), *Error), V8.LoadJsonText(Json8, Error));
	TestEqual(TEXT("a gridless model still reports its event timelines"),
		V8.Npcs[TEXT("dummy")].EventSequences, 11);
	TestEqual(TEXT("...and declares no grid"), V8.Npcs[TEXT("dummy")].BlendGrids, 0);
	TestEqual(TEXT("a bank reports both counts off the one sidecar"),
		V8.Banks[TEXT("bank")].EventSequences, 7);
	TestEqual(TEXT("...beside its grids"), V8.Banks[TEXT("bank")].BlendGrids, 2);

	FElysiumNpcIndex Future;
	Error.Reset();
	const FString Json9 = FString::Printf(TEXT("{\"manifest_version\":9,%s}"), *MinimalNpc);
	TestFalse(TEXT("future manifest is rejected"), Future.LoadJsonText(Json9, Error));
	TestTrue(TEXT("future rejection explains supported versions"), Error.Contains(TEXT("expected 3 to 8")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropBoundsTest,
	"Elysium.Substrate.PropBounds", GElysiumTestFlags)
bool FElysiumPropBoundsTest::RunTest(const FString&)
{
	// `cin_sheriff_sword`'s bind pose in the centimetres the loaded mesh is in: a sword about a
	// metre off the model origin, nowhere near the 22 m its scene clip draws it at.
	const FBoxSphereBounds Bind(FVector(-65.0, 145.0, 75.0), FVector(35.0, 35.0, 115.0), 125.0);
	const double Reach = 2238.8;
	FVector Positive, Negative;

	TestTrue(TEXT("a clip reaching past the bind pose widens it"),
		ElysiumPropBounds::ExtensionFor(Bind, Reach, Positive, Negative));

	// Assert the property, not the arithmetic: compose the box the way CalculateExtendedBounds
	// does and require it to contain the whole +/-Reach cube about the MODEL origin, which is
	// what a clip's reach is measured from and is not where the bind pose is centred.
	const FVector Min = Bind.Origin - Bind.BoxExtent - Negative;
	const FVector Max = Bind.Origin + Bind.BoxExtent + Positive;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		TestTrue(FString::Printf(TEXT("the widened box reaches -%.1f on axis %d"), Reach, Axis),
			Min[Axis] <= -Reach + UE_KINDA_SMALL_NUMBER);
		TestTrue(FString::Printf(TEXT("the widened box reaches +%.1f on axis %d"), Reach, Axis),
			Max[Axis] >= Reach - UE_KINDA_SMALL_NUMBER);
		// And lands exactly there unless the bind pose had already passed it. A side that
		// overshoots is a sign error on the centre offset, which containment alone would not see.
		const double BindMin = Bind.Origin[Axis] - Bind.BoxExtent[Axis];
		const double BindMax = Bind.Origin[Axis] + Bind.BoxExtent[Axis];
		TestTrue(FString::Printf(TEXT("axis %d does not overshoot below"), Axis),
			FMath::IsNearlyEqual(Min[Axis], -Reach) || BindMin <= -Reach);
		TestTrue(FString::Printf(TEXT("axis %d does not overshoot above"), Axis),
			FMath::IsNearlyEqual(Max[Axis], Reach) || BindMax >= Reach);
	}

	// An index that states no reach must leave the mesh exactly as glTFRuntime built it — that is
	// how a pre-re-export corpus keeps today's behaviour instead of gaining a zero-sized bound.
	TestFalse(TEXT("no declared reach writes no extension"),
		ElysiumPropBounds::ExtensionFor(Bind, 0.0, Positive, Negative));
	TestEqual(TEXT("the positive extension is left at zero"), Positive, FVector::ZeroVector);
	TestEqual(TEXT("the negative extension is left at zero"), Negative, FVector::ZeroVector);

	// A prop whose bind pose already contains its clip — every ordinary skeletal prop — is not
	// widened either, so the common case pays nothing and keeps its own tight bounds.
	const FBoxSphereBounds Roomy(FVector::ZeroVector, FVector(500.0), 900.0);
	TestFalse(TEXT("a bind pose that already covers the reach is left alone"),
		ElysiumPropBounds::ExtensionFor(Roomy, 400.0, Positive, Negative));
	TestEqual(TEXT("a covered prop takes no positive extension"), Positive, FVector::ZeroVector);
	TestEqual(TEXT("a covered prop takes no negative extension"), Negative, FVector::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningEmbodimentTest,
	"Elysium.Substrate.OpeningEmbodiment", GElysiumTestFlags)
bool FElysiumOpeningEmbodimentTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector(10.f, 20.f, 30.f);
	Services.PlayerRotation = FRotator(0.f, 35.f, 0.f);
	Services.AnimatedPropModels.Add(TEXT("models/cinematic/cin_wineglass.mdl"), TEXT("cin_wineglass"));
	Services.AnimatedPropClipLoops.Add(TEXT("cin_wineglass|glass_idle"), true);

		auto BuildDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__opening_embodiment__");
		FElysiumEntityDef Glass;
		Glass.Classname = TEXT("prop_dynamic");
		Glass.TargetName = TEXT("wineglass");
		Glass.Origin = FVector(100.f, 200.f, 300.f);
		Glass.ModelMesh = TEXT("cin_wineglass");
		Glass.Keys.Add(TEXT("model"), TEXT("models/cinematic/cin_wineglass.mdl"));
		Glass.Keys.Add(TEXT("LoopSequence"), TEXT("glass_idle"));
		Defs.Defs.Add(MoveTemp(Glass));
		return Defs;
	};

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(BuildDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("player spawned"), Player))
	{
		return false;
	}
	Player->SetRuntimeOrigin(FVector(40.f, 50.f, 60.f));
	Player->SetRuntimeAngles(FVector(0.f, 70.f, 0.f));
	Player->SetRuntimeModel(TEXT("models/character/pc/male/tremere_armor_0.mdl"));
	Player->Skin = 2;
	Player->Disposition = TEXT("Cinematic");

	const FElysiumEntityHandle ControllerHandle = World.CreatePlayerControllerEntity();
	FElysiumEntity* Controller = World.FindPlayerController();
	if (!TestTrue(TEXT("controller handle is valid"), ControllerHandle.IsSet())
		|| !TestNotNull(TEXT("controller entity exists"), Controller))
	{
		return false;
	}
	TestEqual(TEXT("!playercontroller resolves the relationship entity"),
		World.FindByName(TEXT("!playercontroller")), Controller);
	TestEqual(TEXT("controller creation is idempotent"),
		World.CreatePlayerControllerEntity().Index, ControllerHandle.Index);
	TestTrue(TEXT("controller copies player origin"), Controller->Origin.Equals(Player->Origin));
	TestTrue(TEXT("controller copies player orientation"), Controller->Angles.Equals(Player->Angles));
	TestEqual(TEXT("controller copies player model"), Controller->Model, Player->Model);
	TestTrue(TEXT("controller stands through the NPC skeletal path"),
		Services.Saw(TEXT("BuildNpcVisual tremere_armor_0")));

	// A point_teleport may move !player while the cinematic stand-in exists. The explicit player
	// transform is authoritative for that move, so keep the stand-in's teardown anchor coherent;
	// otherwise the later RemoveControllerNPC restores the stale pre-teleport position.
	const FVector TeleportedOrigin(140.f, 250.f, 360.f);
	const FVector TeleportedAngles(12.f, 34.f, 5.f);
	Services.Calls.Reset();
	Player->SetRuntimeTransform(TeleportedOrigin, TeleportedAngles);
	TestTrue(TEXT("an explicit player teleport synchronizes the active controller origin"),
		Controller->Origin.Equals(TeleportedOrigin));
	TestTrue(TEXT("an explicit player teleport synchronizes the active controller angles"),
		Controller->Angles.Equals(TeleportedAngles));
	TestEqual(TEXT("controller synchronization does not duplicate the player body move"),
		Services.Count(TEXT("TeleportPlayer ")), 1);

	Services.Calls.Reset();
	TestTrue(TEXT("controller removal after a player teleport succeeds"),
		World.RemovePlayerControllerEntity());
	TestTrue(TEXT("controller teardown cannot roll the player back after teleport"),
		Player->Origin.Equals(TeleportedOrigin) && Player->Angles.Equals(TeleportedAngles));
	TestEqual(TEXT("controller teardown transfers its final transform atomically"),
		Services.Count(TEXT("TeleportPlayer ")), 1);
	TestNull(TEXT("the synchronized controller relationship clears"), World.FindPlayerController());

	// A scene remains allowed to stage the controller independently. Its final mark transfers back
	// when that controller is removed, which is the other direction of the ownership contract.
	World.CreatePlayerControllerEntity();
	Controller = World.FindPlayerController();
	if (!TestNotNull(TEXT("a second controller can be created for scene staging"), Controller))
	{
		return false;
	}

	Controller->SetRuntimeOrigin(FVector(400.f, 500.f, 600.f));
	Controller->SetRuntimeAngles(FVector(0.f, 135.f, 0.f));
	Controller->SetRuntimeModel(TEXT("models/character/pc/female/toreador_armor_0.mdl"));
	if (FElysiumCombatCharacter* ControllerCharacter = Controller->AsCombatCharacter())
	{
		ControllerCharacter->Skin = 4;
		ControllerCharacter->Disposition = TEXT("Neutral");
	}
	TestTrue(TEXT("controller removal succeeds"), World.RemovePlayerControllerEntity());
	TestNull(TEXT("controller relationship clears"), World.FindPlayerController());
	TestTrue(TEXT("final controller origin transfers to player"),
		Player->Origin.Equals(FVector(400.f, 500.f, 600.f)));
	TestTrue(TEXT("final controller orientation transfers to player"),
		Player->Angles.Equals(FVector(0.f, 135.f, 0.f)));
	TestEqual(TEXT("final controller model transfers to player"), Player->Model,
		FString(TEXT("models/character/pc/female/toreador_armor_0.mdl")));
	TestEqual(TEXT("final controller skin transfers to player"), Player->Skin, 4);
	TestEqual(TEXT("final controller disposition transfers to player"), Player->Disposition,
		FString(TEXT("Neutral")));
	TestFalse(TEXT("removing an absent controller is a no-op"), World.RemovePlayerControllerEntity());

	World.CreatePlayerControllerEntity();
	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);
	FElysiumEntityWorld Restored(nullptr, nullptr, Services.Bundle());
	Restored.Load(BuildDefs());
	Restored.SpawnPlayer();
	Restored.Activate(0.0);
	Restored.ApplySnapshot(Snapshot);
	TestNotNull(TEXT("snapshot rebinds the controller relationship"), Restored.FindPlayerController());
	TestEqual(TEXT("snapshot preserves !playercontroller resolution"),
		Restored.FindByName(TEXT("!playercontroller")), Restored.FindPlayerController());

	TestTrue(TEXT("indexed prop selects the animated visual"),
		Services.Saw(TEXT("BuildAnimatedPropVisual cin_wineglass")));
	// `LoopSequence` resolves in the Activate phase and starts behind a 0.1-0.99 s stagger, so at
	// spawn the prop is holding its rest pose and the loop has not begun. Elysium.Substrate.
	// PropAnimateThink is the dedicated coverage; this only pins that the seam still reaches here.
	TestFalse(TEXT("the authored loop waits for the Activate-phase stagger"),
		Services.Saw(TEXT("PlayAnimatedPropClip cin_wineglass glass_idle loop=1")));
	World.Tick(1.0);
	TestTrue(TEXT("authored loop starts looping once the stagger elapses"),
		Services.Saw(TEXT("PlayAnimatedPropClip cin_wineglass glass_idle loop=1")));
	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("SetAnimation")),
		FElysiumVariant::String(TEXT("glass_pour")), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("Skin")), FElysiumVariant::Int(3), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(1.0);   // the clock has already reached 1.0 above; it must not run backwards
	TestTrue(TEXT("SetAnimation selects a non-looping cinematic clip"),
		Services.Saw(TEXT("PlayAnimatedPropClip cin_wineglass glass_pour loop=0")));
	TestTrue(TEXT("Skin reaches skeletal prop material families"),
		Services.Saw(TEXT("ApplyAnimatedPropSkin cin_wineglass family=3")));

	if (FElysiumEntity* Glass = World.FindByName(TEXT("wineglass")))
	{
		Glass->SetRuntimeModel(TEXT("models/props/furniture/chair.mdl"));
	}
	TestTrue(TEXT("SetModel to an unindexed model rebuilds static representation"),
		Services.Saw(TEXT("BuildPropVisual chair")));
	if (FElysiumEntity* Glass = World.FindByName(TEXT("wineglass")))
	{
		Glass->SetRuntimeModel(TEXT("models/cinematic/cin_wineglass.mdl"));
	}
	TestTrue(TEXT("SetModel back to an indexed model rebuilds skeletal representation"),
		Services.Count(TEXT("BuildAnimatedPropVisual cin_wineglass")) >= 2);

	World.EnqueueInput(TEXT("wineglass"), FName(TEXT("Break")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(1.0);
	TArray<TPair<FString, FString>> Debug;
	if (FElysiumEntity* Glass = World.FindByName(TEXT("wineglass")))
	{
		Glass->GetDebugState(Debug);
	}
	TestTrue(TEXT("Break leaves the animated prop destroyed"),
		Debug.ContainsByPredicate([](const TPair<FString, FString>& Row)
		{
			return Row.Key == TEXT("Broken") && Row.Value == TEXT("yes");
		}));
	return true;
}

// =====================================================================================// Genesis's recovered exit: the chargen panel teleports the player into `firetrans`, whose
// OnStartTouch forces `boogieout,ChangeNow`. The recording services keep this content-free while
// exercising the real command parser, entity I/O and travel seam end to end.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGenesisExitTest,
	"Elysium.Substrate.GenesisExit", GElysiumTestFlags)
bool FElysiumGenesisExitTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector(20.f, 30.f, 40.f);
	Services.PlayerRotation = FRotator(0.f, 75.f, 0.f);

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__genesis_exit__");

	FElysiumEntityDef Landmark;
	Landmark.Classname = TEXT("info_landmark");
	Landmark.TargetName = TEXT("newgame");
	Landmark.Origin = FVector::ZeroVector;
	Defs.Defs.Add(MoveTemp(Landmark));

	FElysiumEntityDef Change;
	Change.Classname = TEXT("trigger_changelevel");
	Change.TargetName = TEXT("boogieout_direct");
	Change.Keys.Add(TEXT("map"), TEXT("sp_theatre"));
	Change.Keys.Add(TEXT("landmark"), TEXT("newgame"));
	Defs.Defs.Add(MoveTemp(Change));

	FElysiumEntityDef BoogieoutDef;
	BoogieoutDef.Classname = TEXT("trigger_changelevel");
	BoogieoutDef.TargetName = TEXT("boogieout");
	BoogieoutDef.Keys.Add(TEXT("map"), TEXT("sp_theatre"));
	BoogieoutDef.Keys.Add(TEXT("landmark"), TEXT("newgame"));
	Defs.Defs.Add(MoveTemp(BoogieoutDef));

	FElysiumEntityDef Fire;
	Fire.Classname = TEXT("trigger_multiple");
	Fire.TargetName = TEXT("firetrans");
	Fire.Origin = FVector(400.f, 500.f, 60.f);
	Fire.Keys.Add(TEXT("angles"), TEXT("12 34 5"));
	Fire.Keys.Add(TEXT("spawnflags"), TEXT("1"));
	{
		FElysiumOutputDef Wire;
		Wire.Name = TEXT("OnStartTouch");
		Wire.Target = TEXT("boogieout");
		Wire.Input = TEXT("ChangeNow");
		Wire.Times = -1;
		Fire.Outputs.Add(MoveTemp(Wire));
	}
	Defs.Defs.Add(MoveTemp(Fire));

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* DirectChange = World.FindByName(TEXT("boogieout_direct"));
	FElysiumEntity* Boogieout = World.FindByName(TEXT("boogieout"));
	FElysiumEntity* Firetrans = World.FindByName(TEXT("firetrans"));
	if (!TestNotNull(TEXT("direct changelevel resolved"), DirectChange)
		|| !TestNotNull(TEXT("boogieout resolved"), Boogieout)
		|| !TestNotNull(TEXT("firetrans resolved"), Firetrans))
	{
		return false;
	}

	// The forced input itself is live — this was the dead wire before ChangeNow registered.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("ChangeNow")), FElysiumVariant::Void(), 0.0,
		PlayerHandle, DirectChange->Handle);
	World.Tick(0.0);
	TestTrue(TEXT("ChangeNow reaches the travel seam"),
		Services.Saw(TEXT("RequestLandmarkTravel sp_theatre@newgame")));
	TestEqual(TEXT("ChangeNow is not counted as an unknown input"), World.UnknownInputs(), 0);

	FElysiumCommands& Commands = FElysiumCommands::Get();
	FElysiumCommandBinding TeleportBinding = Commands.Bind(TEXT("teleport_player"),
		[&World](const FElysiumCommandCall& Call)
		{
			ElysiumCommands::TeleportPlayer(World, Call.Args);
		});
	if (!TestTrue(TEXT("teleport_player test implementation bound"), TeleportBinding.IsValid()))
	{
		return false;
	}

	Services.Calls.Reset();
	TestTrue(TEXT("the named form is a declared command"),
		Commands.Execute(TEXT("teleport_player firetrans")));
	TestTrue(TEXT("the named form moves through the embodiment seam"),
		Services.PlayerLocation.Equals(Firetrans->Origin));
	TestTrue(TEXT("the named form takes the target's full Source facing"),
		Services.PlayerRotation.Equals(FRotator(-12.f, -34.f, 5.f)));

	TestTrue(TEXT("the coordinate form is a declared command"),
		Commands.Execute(TEXT("teleport_player 7 8 9")));
	TestTrue(TEXT("the coordinate form lands at the requested feet origin"),
		Services.PlayerLocation.Equals(FVector(7.f, 8.f, 9.f)));
	TestTrue(TEXT("the coordinate-only form preserves the current view"),
		Services.PlayerRotation.Equals(FRotator(-12.f, -34.f, 5.f)));

	TestTrue(TEXT("the six-coordinate form is a declared command"),
		Commands.Execute(TEXT("teleport_player 10 20 30 1 2 3")));
	TestTrue(TEXT("the six-coordinate form converts the full Source view"),
		Services.PlayerLocation.Equals(FVector(10.f, 20.f, 30.f))
		&& Services.PlayerRotation.Equals(FRotator(-1.f, -2.f, 3.f)));

	const int32 CallsBeforeMissing = Services.Calls.Num();
	AddExpectedError(TEXT("Could not find entity named missing_target"),
		EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("an unresolved target is still a recognized command"),
		Commands.Execute(TEXT("teleport_player missing_target")));
	TestEqual(TEXT("an unresolved target does not move the player"),
		Services.Calls.Num(), CallsBeforeMissing);

	// Reproduce the complete close tail: teleport itself does not publish a touch. The following
	// movement reconciliation computes final containment, then the output queue reaches ChangeNow.
	Services.Calls.Reset();
	const int32 TouchBeginsBefore = World.TouchBegins();
	Commands.Execute(TEXT("teleport_player firetrans"));
	TestEqual(TEXT("teleport leaves entity touch delivery deferred"),
		World.TouchBegins(), TouchBeginsBefore);
	TArray<FElysiumEntityHandle> FinalContainment { Firetrans->Handle };
	World.ReconcilePlayerTouches(FinalContainment);
	TestEqual(TEXT("the following movement reconciliation emits one begin edge"),
		World.TouchBegins(), TouchBeginsBefore + 1);
	for (int32 i = 0; i < 3; ++i)
	{
		World.Tick(1.0 + i);
	}
	TestTrue(TEXT("teleport -> OnStartTouch -> ChangeNow reaches travel"),
		Services.Saw(TEXT("RequestLandmarkTravel sp_theatre@newgame")));
	TestEqual(TEXT("the recovered chain delivers no unknown input"), World.UnknownInputs(), 0);
	const int32 TouchEndsBefore = World.TouchEnds();
	FinalContainment.Reset();
	World.ReconcilePlayerTouches(FinalContainment);
	TestEqual(TEXT("the next movement reconciliation emits one end edge"),
		World.TouchEnds(), TouchEndsBefore + 1);

	Commands.Unbind(TeleportBinding);
	return true;
}

// Retail touch-reconciliation ordering (RE campaign `touchpartition-findings.md` F5-F8; ledger
// C171/C172; recovered in retail engine.dll/vampire.dll). When a player's trigger containment
// changes in one reconciliation, the 2004 engine fires the NEW contact's StartTouch synchronously
// during the move and defers the OLD contact's EndTouch to that frame's post-think untouch pass
// (`GameFrame` step 4); both outputs land in the same event-queue drain (step 6). So begins are
// queued BEFORE ends, and with the queue's FIFO tie-break the END edge is the LAST writer of any
// state both edges touch. `ReconcilePlayerTouches` must reproduce that order — the faithful
// behaviour, not the current inverted ends-then-begins pass.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTouchReconcileOrderTest,
	"Elysium.Substrate.TouchReconcileOrder", GElysiumTestFlags)
bool FElysiumTouchReconcileOrderTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__touch_reconcile_order__");

	auto AddWire = [](FElysiumEntityDef& Source, const TCHAR* Output,
		const TCHAR* Target, const TCHAR* Input, const TCHAR* Param)
	{
		FElysiumOutputDef Wire;
		Wire.Name = Output;
		Wire.Target = Target;
		Wire.Input = Input;
		Wire.Param = Param;
		Wire.Times = -1;
		Source.Outputs.Add(MoveTemp(Wire));
	};

	// Two abutting player triggers writing the same counter: the OLD volume's OnEndTouch sets 100,
	// the NEW volume's OnStartTouch sets 200. Retail order (begin before end) leaves 100 as the final
	// value because the end edge is queued last; the inverted order leaves 200.
	FElysiumEntityDef VolumeOld;
	VolumeOld.Classname = TEXT("trigger_multiple");
	VolumeOld.TargetName = TEXT("vol_old");
	VolumeOld.Keys.Add(TEXT("spawnflags"), TEXT("1")); // ALLOW_CLIENTS
	AddWire(VolumeOld, TEXT("OnEndTouch"), TEXT("counter1"), TEXT("SetValue"), TEXT("100"));
	Defs.Defs.Add(MoveTemp(VolumeOld));

	FElysiumEntityDef VolumeNew;
	VolumeNew.Classname = TEXT("trigger_multiple");
	VolumeNew.TargetName = TEXT("vol_new");
	VolumeNew.Keys.Add(TEXT("spawnflags"), TEXT("1")); // ALLOW_CLIENTS
	AddWire(VolumeNew, TEXT("OnStartTouch"), TEXT("counter1"), TEXT("SetValue"), TEXT("200"));
	Defs.Defs.Add(MoveTemp(VolumeNew));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	FElysiumEntity* VolOld = World.FindByName(TEXT("vol_old"));
	FElysiumEntity* VolNew = World.FindByName(TEXT("vol_new"));
	FElysiumEntity* CounterEnt = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("old volume resolved"), VolOld)
		|| !TestNotNull(TEXT("new volume resolved"), VolNew)
		|| !TestNotNull(TEXT("counter resolved"), CounterEnt))
	{
		return false;
	}
	World.Activate(0.0);

	// Seed containment: the player is inside the old volume (its OnStartTouch is unwired, so the seed
	// produces no output), then drain so the queue is clean before the reconciliation under test.
	World.RouteBrushTouch(VolOld->Handle, Player, /*bBegin*/ true);
	World.Tick(0.0);

	auto CounterValue = [&CounterEnt]()
	{
		TArray<TPair<FString, FString>> State;
		CounterEnt->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.0f;
	};
	TestEqual(TEXT("counter unwritten before the reconciliation"), CounterValue(), 0.0f);

	// One reconciliation moves the player from the old volume into the new one: End(old) + Begin(new).
	TArray<FElysiumEntityHandle> FinalContainment { VolNew->Handle };
	World.ReconcilePlayerTouches(FinalContainment);

	// The queue now holds exactly the two SetValue events in delivery order (Pending() is sorted by
	// FireTime then FIFO serial). Retail queues the begin first, so the earliest pending event must be
	// the NEW volume's StartTouch, and the OLD volume's EndTouch must sit behind it.
	const TArray<FElysiumIOEvent>& Pending = World.Queue().Pending();
	if (!TestEqual(TEXT("reconciliation queued exactly the two edge outputs"), Pending.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the new contact's StartTouch is delivered first (begin before end)"),
		Pending[0].Caller.Index, VolNew->Handle.Index);
	TestEqual(TEXT("the old contact's EndTouch is delivered last"),
		Pending[1].Caller.Index, VolOld->Handle.Index);

	// Draining both: the end edge, queued last, is the final writer — retail leaves 100, not 200.
	World.Tick(0.0);
	TestEqual(TEXT("the old contact's OnEndTouch wins as the last writer"), CounterValue(), 100.0f);

	return true;
}

// =====================================================================================
// Engine integration for the one part GenesisExit cannot model on a bare entity world:
// SetActorLocation(..., TeleportPhysics) may synchronously recompute the real hull overlap. The
// map wrapper deliberately suppresses only entity-bus ingress; raw UE delegates remain observable.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMotorSleepTest,
	"Elysium.Substrate.NpcMotorSleep", GElysiumTestFlags)
bool FElysiumNpcMotorSleepTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumNpcBody* Body = World ? World->SpawnActor<AElysiumNpcBody>() : nullptr;
	if (!TestNotNull(TEXT("native NPC motor spawned"), Body))
	{
		return false;
	}

	const FElysiumEntityHandle NpcOwner(37, 4);
	// No map actor: this body stands in a bare test world, and the identity half is what is asserted.
	Body->SetOwningEntity(nullptr, NpcOwner);
	TestTrue(TEXT("native NPC motor retains its logical toucher identity"),
		Body->GetOwningEntity() == NpcOwner);
	Body->InitializeAtFeet(FVector::ZeroVector, 0.0f);
	UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
	if (!TestNotNull(TEXT("native NPC motor owns CharacterMovement"), Movement))
	{
		return false;
	}
	TestFalse(TEXT("NPC capsule never contributes navigation geometry"),
		Body->GetCapsuleComponent()->CanEverAffectNavigation());
	TestFalse(TEXT("NPC movement never contributes navigation geometry"),
		Movement->CanEverAffectNavigation());
	TestNull(TEXT("an idle NPC does not create a crowd controller before Recast is ready"),
		Body->GetController());
	TestFalse(TEXT("movement stays inactive before the runtime barrier"), Movement->IsActive());

	Body->SetRuntimeReady(true);
	TestTrue(TEXT("the enabled idle body keeps physical collision"), Body->GetActorEnableCollision());
	TestFalse(TEXT("crossing the runtime barrier does not tick an idle motor"), Movement->IsActive());
	TestNull(TEXT("crossing the runtime barrier still does not create an idle controller"),
		Body->GetController());

	Movement->Activate();
	TestTrue(TEXT("the test can model an outstanding movement request"), Movement->IsActive());
	Body->Stop();
	TestFalse(TEXT("Stop deactivates CharacterMovement"), Movement->IsActive());
	TestFalse(TEXT("Stop disables the movement component tick"), Movement->IsComponentTickEnabled());

	Body->SetEnabled(false);
	TestFalse(TEXT("a disabled NPC drops physical collision"), Body->GetActorEnableCollision());
	Body->SetEnabled(true);
	TestTrue(TEXT("re-enabling restores collision"), Body->GetActorEnableCollision());
	TestFalse(TEXT("re-enabling an idle NPC does not wake CharacterMovement"), Movement->IsActive());
	return true;
}

// A body that only ever stands must still report itself standing.
//
// `MovementMode` is zero-initialised to `MOVE_None` and only becomes `MOVE_Walking` when a
// controller possesses the character — which happens on the body's first accepted travel request
// and never at all for a background NPC. `IsMovingOnGround()` reads the mode alone, so such a body
// used to report itself airborne for its whole life, and the animation latch pinned it at
// `ACT_FALLING` while it stood on the pavement. Retail has no ground/air poll for the cast at all
// (`docs/vtmb/animation_and_movers.md` — the compact-code classifier is the player chain's), so a
// standing NPC selecting a fall is a defect with no faithful counterpart.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcStandingGroundTest,
	"Elysium.Substrate.NpcStandingGround", GElysiumTestFlags)
bool FElysiumNpcStandingGroundTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	if (!TestNotNull(TEXT("transient game world exists"), World))
	{
		return false;
	}

	// A solid slab to stand on. Without real floor under the capsule the body genuinely is airborne,
	// and the assertion below would pass for the wrong reason.
	AActor* FloorOwner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("floor owner spawned"), FloorOwner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(FloorOwner, TEXT("Root"));
	FloorOwner->SetRootComponent(Root);
	Root->RegisterComponent();
	FloorOwner->AddInstanceComponent(Root);

	FElysiumConvexHull Slab;
	for (const float X : { -600.f, 600.f })
	{
		for (const float Y : { -600.f, 600.f })
		{
			for (const float Z : { -100.f, 0.f })
			{
				Slab.Vertices.Emplace(X, Y, Z);
			}
		}
	}
	UElysiumBrushComponent* Floor = NewObject<UElysiumBrushComponent>(FloorOwner, TEXT("Slab"));
	Floor->InitBrush(FElysiumEntityHandle::Invalid(), { Slab }, EElysiumBrushSolidity::Solid);
	Floor->SetupAttachment(Root);
	Floor->RegisterComponent();
	FloorOwner->AddInstanceComponent(Floor);

	AElysiumNpcBody* Body = World->SpawnActor<AElysiumNpcBody>();
	if (!TestNotNull(TEXT("native NPC body spawned"), Body))
	{
		return false;
	}
	Body->InitializeAtFeet(FVector::ZeroVector, 0.0f);
	Body->SetRuntimeReady(true);

	UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
	if (!TestNotNull(TEXT("native NPC body owns CharacterMovement"), Movement))
	{
		return false;
	}

	// The body has never been issued a travel request — the exact case that used to read airborne.
	TestNull(TEXT("the standing body still has no controller"), Body->GetController());
	TestTrue(TEXT("a settled standing body reports a grounded movement mode"),
		Movement->IsMovingOnGround());

	const FElysiumLocomotionSample Sample = Body->SampleLocomotion();
	TestTrue(TEXT("...so its locomotion sample reports it on the ground"), Sample.bOnGround);
	TestTrue(TEXT("...standing still"), Sample.Speed2D() <= UE_KINDA_SMALL_NUMBER);

	// The sleep policy is the thing this fix must not trade away: an idle body still does not tick
	// movement or own a crowd agent (`docs/architecture/map-architecture.md`).
	TestFalse(TEXT("a standing body still does not wake CharacterMovement"), Movement->IsActive());

	// The payoff, through the pure rules the cast actually poses from: the latch stays grounded and
	// the classifier answers Idle rather than ACT_FALLING.
	const FElysiumGaitReference Gait;
	FElysiumJumpLatch Latch;
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		Latch = ElysiumAnimIntent::AdvanceJumpLatch(Latch, Sample, 1.0f / 60.0f, Gait);
	}
	TestEqual(TEXT("the jump latch stays grounded under a standing body"),
		static_cast<int32>(Latch.Phase), static_cast<int32>(EElysiumAirPhase::Grounded));
	TestEqual(TEXT("...and the classifier answers idle, not falling"),
		static_cast<int32>(ElysiumAnimIntent::Classify(Sample, Latch, Gait)),
		static_cast<int32>(EElysiumAnimActivityCode::Idle));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumEngineTeleportOverlapTest,
	"Elysium.Substrate.EngineTeleportOverlap", GElysiumTestFlags)
bool FElysiumEngineTeleportOverlapTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	if (!TestNotNull(TEXT("transient game world exists"), World))
	{
		return false;
	}

	APlayerController* PC = World->SpawnActor<APlayerController>();
	AElysiumPawn* Pawn = World->SpawnActor<AElysiumPawn>(
		FVector(0.f, 0.f, ElysiumMove::StandHeight * 0.5f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("player controller spawned"), PC)
		|| !TestNotNull(TEXT("faithful player hull spawned"), Pawn))
	{
		return false;
	}
	TestFalse(TEXT("the moving faithful player hull never reshapes Recast"),
		Pawn->GetRootComponent()->CanEverAffectNavigation());
	PC->Possess(Pawn);
	TestTrue(TEXT("transient world exposes the possessed pawn"),
		World->GetFirstPlayerController() == PC && PC->GetPawn() == Pawn);

	AActor* TriggerOwner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("trigger owner spawned"), TriggerOwner))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(TriggerOwner, TEXT("Root"));
	TriggerOwner->SetRootComponent(Root);
	Root->RegisterComponent();
	TriggerOwner->AddInstanceComponent(Root);

	// The destination is the entity's feet-origin, matching `teleport_player firetrans`. The convex
	// encloses the lifted standing hull but is far enough from the start that registration itself
	// cannot produce the notification under test.
	const FVector FeetDestination(500.f, 0.f, 0.f);
	FElysiumConvexHull Hull;
	for (const float X : { -200.f, 200.f })
	{
		for (const float Y : { -200.f, 200.f })
		{
			for (const float Z : { -100.f, 220.f })
			{
				Hull.Vertices.Emplace(X, Y, Z);
			}
		}
	}
	UElysiumBrushComponent* Trigger =
		NewObject<UElysiumBrushComponent>(TriggerOwner, TEXT("Firetrans"));
	Trigger->InitBrush(FElysiumEntityHandle::Invalid(), { Hull }, EElysiumBrushSolidity::Trigger);
	Trigger->SetupAttachment(Root);
	Trigger->SetRelativeLocation(FeetDestination);
	Trigger->RegisterComponent();
	TriggerOwner->AddInstanceComponent(Trigger);

	UElysiumOverlapTestProbe* Probe = NewObject<UElysiumOverlapTestProbe>(World);
	Trigger->OnComponentBeginOverlap.AddDynamic(
		Probe, &UElysiumOverlapTestProbe::HandleBeginOverlap);
	TestEqual(TEXT("fixture starts outside firetrans"), Probe->BeginCount, 0);
	TestFalse(TEXT("fixture starts with no player overlap"), Trigger->IsOverlappingActor(Pawn));

	// Deferred construction avoids loading a map; TeleportPlayer itself needs only this actor's
	// world and its first player controller, so this calls the exact production wrapper.
	AElysiumMapActor* MapActor = World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("production map actor wrapper spawned deferred"), MapActor))
	{
		return false;
	}
	const FRotator ViewRotation(11.f, 37.f, 5.f);
	MapActor->TeleportPlayer(FeetDestination, ViewRotation);

	TestEqual(TEXT("engine teleport synchronously emits one begin overlap"), Probe->BeginCount, 1);
	TestTrue(TEXT("begin overlap identifies the player pawn"), Probe->LastOther == Pawn);
	TestTrue(TEXT("player hull is registered inside firetrans after teleport"),
		Trigger->IsOverlappingActor(Pawn));
	TestTrue(TEXT("production wrapper lifts the feet-origin by the hull half-height"),
		Pawn->GetActorLocation().Equals(
			FeetDestination + FVector(0.f, 0.f, ElysiumMove::StandHeight * 0.5f)));
	TestTrue(TEXT("production wrapper applies the full requested view"),
		PC->GetControlRotation().Equals(ViewRotation));
	TestTrue(TEXT("the body itself receives view yaw only"),
		Pawn->GetActorRotation().Equals(FRotator(0.f, ViewRotation.Yaw, 0.f)));

	MapActor->Destroy();
	return true;
}

// LIFE4 "Visibility": the world weapon submits or not by the camera's own gate
// (`FElysiumCameraDrawPolicy::bWorldWeaponEligible`, published as `FElysiumCameraView::bDrawWorldWeapon`),
// suppression-only — the attachment, its leader pose and its model survive the toggle in both
// directions, and an NPC body never consults the local player's camera policy at all.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPawnWieldVisibilityTest,
	"Elysium.Substrate.PawnWieldVisibility", GElysiumTestFlags)
bool FElysiumPawnWieldVisibilityTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumPawn* Pawn = World ? World->SpawnActor<AElysiumPawn>(
		FVector(0.f, 0.f, ElysiumMove::StandHeight * 0.5f), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("wield-visibility fixture pawn spawned"), Pawn))
	{
		return false;
	}

	// A minimal stand-in for the player body InstallWieldModel's real callers build: no baked mesh
	// is needed to prove a hidden-flag decision, only the leader-pose relationship and the tag
	// `ElysiumNpcVisual::FindWieldModel` walks.
	USkeletalMeshComponent* Visual = NewObject<USkeletalMeshComponent>(Pawn, TEXT("Visual"));
	Visual->SetupAttachment(Pawn->GetRootComponent());
	Visual->RegisterComponent();
	Pawn->SetPlayerVisual(Visual);

	USkeletalMeshComponent* Wield = NewObject<USkeletalMeshComponent>(Pawn, TEXT("Wield"));
	Wield->ComponentTags.Add(ElysiumNpcVisual::WieldComponentTag());
	Wield->SetupAttachment(Visual);
	Wield->RegisterComponent();
	Wield->AttachToComponent(Visual, FAttachmentTransformRules::SnapToTargetIncludingScale);
	Wield->SetLeaderPoseComponent(Visual);

	FElysiumCameraDrawPolicy Policy;
	Policy.bThirdPerson = true;
	Policy.bBodyEligible = true;
	Policy.BodyAlpha = 1.0f;
	Policy.bWorldWeaponEligible = true;
	Pawn->ApplyDrawPolicy(Policy);

	TestFalse(TEXT("eligible in third person: the weapon submits"), Wield->bHiddenInGame);
	TestTrue(TEXT("the attachment is the same component found before any toggle"),
		ElysiumNpcVisual::FindWieldModel(Visual) == Wield);

	Policy.bThirdPerson = false;
	Policy.bBodyEligible = false;
	Policy.BodyAlpha = 0.0f;
	Policy.bWorldWeaponEligible = false;
	Pawn->ApplyDrawPolicy(Policy);

	TestTrue(TEXT("ineligible in first person: submission is suppressed"), Wield->bHiddenInGame);
	TestTrue(TEXT("suppression never destroys or detaches the attachment"),
		ElysiumNpcVisual::FindWieldModel(Visual) == Wield);
	TestTrue(TEXT("the leader-pose relationship survives suppression"),
		Wield->LeaderPoseComponent.Get() == Visual);

	Policy.bThirdPerson = true;
	Policy.bBodyEligible = true;
	Policy.BodyAlpha = 1.0f;
	Policy.bWorldWeaponEligible = true;
	Pawn->ApplyDrawPolicy(Policy);

	TestFalse(TEXT("eligible again: the same component resumes drawing, not a rebuilt one"),
		Wield->bHiddenInGame);
	TestTrue(TEXT("still the identical attachment"),
		ElysiumNpcVisual::FindWieldModel(Visual) == Wield);

	// An NPC body sits on its own actor with its own wield model and is never reached by the
	// player pawn's draw policy: `RefreshBodyVisibility` only ever looks at its own `PlayerVisual`.
	AActor* NpcOwner = World->SpawnActor<AActor>();
	USceneComponent* NpcRoot = NewObject<USceneComponent>(NpcOwner, TEXT("NpcRoot"));
	NpcOwner->SetRootComponent(NpcRoot);
	NpcRoot->RegisterComponent();
	USkeletalMeshComponent* NpcBody = NewObject<USkeletalMeshComponent>(NpcOwner, TEXT("NpcBody"));
	NpcBody->SetupAttachment(NpcRoot);
	NpcBody->RegisterComponent();
	USkeletalMeshComponent* NpcWield = NewObject<USkeletalMeshComponent>(NpcOwner, TEXT("NpcWield"));
	NpcWield->ComponentTags.Add(ElysiumNpcVisual::WieldComponentTag());
	NpcWield->SetupAttachment(NpcBody);
	NpcWield->RegisterComponent();
	NpcWield->AttachToComponent(NpcBody, FAttachmentTransformRules::SnapToTargetIncludingScale);
	NpcWield->SetLeaderPoseComponent(NpcBody);
	NpcWield->SetHiddenInGame(false);

	Policy.bThirdPerson = false;
	Policy.bBodyEligible = false;
	Policy.BodyAlpha = 0.0f;
	Policy.bWorldWeaponEligible = false;
	Pawn->ApplyDrawPolicy(Policy);

	TestFalse(TEXT("an NPC's wield model never consults the local player's camera policy"),
		NpcWield->bHiddenInGame);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUseTargetingEmbodimentTest,
	"Elysium.Substrate.UseTargetingEmbodiment", GElysiumTestFlags)
bool FElysiumUseTargetingEmbodimentTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	APlayerController* PC = World ? World->SpawnActor<APlayerController>() : nullptr;
	AElysiumPawn* Pawn = World ? World->SpawnActor<AElysiumPawn>(
		FVector(0, 0, ElysiumMove::StandHeight * 0.5f), FRotator::ZeroRotator) : nullptr;
	if (!TestNotNull(TEXT("targeting controller"), PC)
		|| !TestNotNull(TEXT("targeting player body"), Pawn))
	{
		return false;
	}
	PC->Possess(Pawn);
	PC->SetControlRotation(FRotator::ZeroRotator);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}

	AElysiumMapActor* Map = World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("targeting map embodiment"), Map))
	{
		return false;
	}
	FVector CameraLocation;
	FRotator CameraRotation;
	FVector BodyOrigin;
	if (!TestTrue(TEXT("final player camera POV resolves"),
		Map->GetPlayerViewPoint(CameraLocation, CameraRotation))
		|| !TestTrue(TEXT("player body use origin resolves"), Map->GetPlayerUseOrigin(BodyOrigin)))
	{
		return false;
	}

	auto AddTarget = [Map](const TCHAR* Name, const FVector& Location,
		const FVector& Extent, const FElysiumEntityHandle& Handle)
	{
		UBoxComponent* Source = NewObject<UBoxComponent>(Map, FName(Name));
		Source->InitBoxExtent(Extent);
		Source->SetupAttachment(Map->GetRootComponent());
		Source->SetWorldLocation(Location);
		Source->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Source->RegisterComponent();
		Map->AddInstanceComponent(Source);
		Map->RegisterUseAnchor(Source, Handle);
		return Source;
	};

	const FVector Aim = Pawn->GetViewRotation().Vector().GetSafeNormal();
	const FVector Side = FRotationMatrix(Pawn->GetViewRotation()).GetScaledAxis(EAxis::Y).GetSafeNormal();
	const FElysiumEntityHandle ExactHandle(10, 1);
	UBoxComponent* ExactSource = AddTarget(TEXT("ExactSource"), BodyOrigin + Aim * 150.0f,
		FVector(3.0f), ExactHandle);
	TestEqual(TEXT("presentation resolves the physical use visual rather than its query proxy"),
		Map->FindUseVisual(ExactHandle), static_cast<UPrimitiveComponent*>(ExactSource));
	FElysiumUseQueryResult Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("exact ray produces one target"), Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("exact ray selects its logical entity"), Query.Candidates[0].Owner, ExactHandle);
		TestEqual(TEXT("exact ray is classified exact"),
			Query.Candidates[0].Selection, EElysiumUseSelection::Exact);
		TestTrue(TEXT("query reports body distance"), Query.Candidates[0].BodyDistance > 0.0f);
		TestTrue(TEXT("query reports camera distance"), Query.Candidates[0].CameraDistance > 0.0f);
	}

	Map->SetUseAnchorEnabled(ExactHandle, false);
	TestNull(TEXT("a disabled use visual is not a terminal projection target"),
		Map->FindUseVisual(ExactHandle));
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("disabled anchor is removed from exact targeting"), Query.Candidates.IsEmpty());

	// 10 cm off-axis at 150 cm is ~3.8°, inside retail player_use_arc 30°.
	const FElysiumEntityHandle ConeHandle(11, 1);
	AddTarget(TEXT("ConeSource"), BodyOrigin + Aim * 150.0f + Side * 10.0f,
		FVector(2.0f), ConeHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("an off-axis target inside the 30 degree FOV is selected"),
		Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("FOV walk names the off-axis target"), Query.Candidates[0].Owner, ConeHandle);
		TestEqual(TEXT("FOV walk is not a look-ray hit"),
			Query.Candidates[0].Selection, EElysiumUseSelection::Assisted);
	}

	Map->SetUseAnchorEnabled(ConeHandle, false);
	const FElysiumEntityHandle WideHandle(12, 1);
	AddTarget(TEXT("WideSource"), BodyOrigin + Aim * 150.0f + Side * 150.0f,
		FVector(2.0f), WideHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("a 45 degree miss is outside the 30 degree FOV"), Query.Candidates.IsEmpty());

	Map->SetUseAnchorEnabled(WideHandle, false);
	const FElysiumEntityHandle LeftHandle(20, 1);
	const FElysiumEntityHandle RightHandle(21, 1);
	AddTarget(TEXT("LeftButtonSource"), BodyOrigin + Aim * 160.0f - Side * 7.0f,
		FVector(3.0f), LeftHandle);
	AddTarget(TEXT("RightButtonSource"), BodyOrigin + Aim * 160.0f, FVector(3.0f), RightHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("look-ray among adjacent buttons returns one control"), Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("look-ray picks the on-axis button"),
			Query.Candidates[0].Owner, RightHandle);
	}

	Map->SetUseAnchorEnabled(LeftHandle, false);
	Map->SetUseAnchorEnabled(RightHandle, false);
	const FElysiumEntityHandle MidFarHandle(29, 1);
	AddTarget(TEXT("MidFarSource"), BodyOrigin + Aim * 240.0f, FVector(2.0f), MidFarHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("the 160 unit fallback ray still reaches past 80 units"),
		Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("fallback names the mid-far target"), Query.Candidates[0].Owner, MidFarHandle);
	}

	Map->SetUseAnchorEnabled(MidFarHandle, false);
	const FElysiumEntityHandle FarHandle(30, 1);
	AddTarget(TEXT("FarSource"), BodyOrigin + Aim * 450.0f, FVector(2.0f), FarHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("beyond the 160 unit fallback is rejected"), Query.Candidates.IsEmpty());

	Map->SetUseAnchorEnabled(FarHandle, false);
	const FElysiumEntityHandle OccludedHandle(31, 1);
	const FVector OccludedPoint = BodyOrigin + Aim * 150.0f;
	AddTarget(TEXT("OccludedSource"), OccludedPoint, FVector(3.0f), OccludedHandle);
	AActor* WallOwner = World->SpawnActor<AActor>();
	UBoxComponent* Wall = NewObject<UBoxComponent>(WallOwner, TEXT("UseWall"));
	WallOwner->SetRootComponent(Wall);
	Wall->InitBoxExtent(FVector(4.0f, 50.0f, 50.0f));
	Wall->SetWorldLocation(BodyOrigin + Aim * 75.0f);
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Wall->SetCollisionObjectType(ECC_WorldStatic);
	Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
	Wall->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
	Wall->RegisterComponent();
	WallOwner->AddInstanceComponent(Wall);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("wall occlusion rejects the target"), Query.Candidates.IsEmpty());
	TestEqual(TEXT("wall occlusion is reported"), Query.MissOutcome, EElysiumUseOutcome::Occluded);
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	const FElysiumEntityHandle SlabHandle(40, 1);
	const FElysiumEntityHandle KnobHandle(41, 1);
	FElysiumConvexHull SlabHull;
	for (const float X : { -4.f, 4.f })
	{
		for (const float Y : { -50.f, 50.f })
		{
			for (const float Z : { -50.f, 50.f })
			{
				SlabHull.Vertices.Emplace(X, Y, Z);
			}
		}
	}
	UElysiumBrushComponent* Slab = NewObject<UElysiumBrushComponent>(Map, TEXT("DoorSlab"));
	Slab->InitBrush(SlabHandle, { SlabHull }, EElysiumBrushSolidity::Solid);
	Slab->SetupAttachment(Map->GetRootComponent());
	Slab->SetWorldLocation(BodyOrigin + Aim * 75.0f);
	Slab->RegisterComponent();
	Map->AddInstanceComponent(Slab);
	Map->RegisterUseAnchor(Slab, SlabHandle);
	AddTarget(TEXT("KnobBehindSlab"), BodyOrigin + Aim * 150.0f, FVector(3.0f), KnobHandle);
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("the look-ray selects the door slab in front of its knob"),
		Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("the slab is the use target"), Query.Candidates[0].Owner, SlabHandle);
	}
	Map->SetUseAnchorEnabled(KnobHandle, false);
	Slab->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Map->SetUseAnchorEnabled(SlabHandle, false);
	Map->SetUseAnchorEnabled(OccludedHandle, true);

	// The rendered boom is not the search origin. Offset the camera; look and eye stay on the pawn.
	ACameraActor* OffsetCamera = World->SpawnActor<ACameraActor>();
	const FVector OffsetLocation = BodyOrigin + FVector(-180.0f, 80.0f, 40.0f);
	OffsetCamera->SetActorLocationAndRotation(OffsetLocation,
		(OccludedPoint - OffsetLocation).Rotation());
	PC->SetViewTarget(OffsetCamera);
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->UpdateCamera(0.0f);
	}
	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("a third-person boom does not move the use search off the eye"),
		Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("eye-forward still names the on-axis target"),
			Query.Candidates[0].Owner, OccludedHandle);
	}
	Map->SetUseAnchorEnabled(OccludedHandle, false);

	// A source component rotated in world space must keep its use proxy aligned to its local bounds.
	const FElysiumEntityHandle RotatedHandle(50, 1);
	UBoxComponent* RotatedSource = NewObject<UBoxComponent>(Map, TEXT("RotatedTargetSource"));
	RotatedSource->InitBoxExtent(FVector(10.0f, 2.0f, 2.0f));
	RotatedSource->SetupAttachment(Map->GetRootComponent());
	RotatedSource->SetWorldLocationAndRotation(
		BodyOrigin + Aim * 150.0f, FRotator(0.0f, 90.0f, 0.0f));
	RotatedSource->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RotatedSource->RegisterComponent();
	Map->AddInstanceComponent(RotatedSource);
	Map->RegisterUseAnchor(RotatedSource, RotatedHandle);

	Query = Map->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("rotated use anchor is selectable along player aim"),
		Query.Candidates.Num(), 1);
	if (!Query.Candidates.IsEmpty())
	{
		TestEqual(TEXT("rotated use anchor names the expected entity"),
			Query.Candidates[0].Owner, RotatedHandle);
	}
	Map->SetUseAnchorEnabled(RotatedHandle, false);

	Map->ClearUseAnchors();
	Map->Destroy();
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedTargetingOcclusionTest,
	"Elysium.Substrate.FeedTargetingOcclusion", GElysiumTestFlags)
bool FElysiumFeedTargetingOcclusionTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	if (!TestNotNull(TEXT("feed targeting world"), World))
	{
		return false;
	}

	// The gameplay probe starts at the pawn's eye/use pivot even when the final third-person POV is
	// one boom-length behind it. Starting at that rendered camera is the exact reversal regression:
	// the short recovered probe cannot reach the victim in front and instead reaches one behind.
	const FVector UseOrigin(0.0f, 0.0f, ElysiumMove::StandViewZ);
	const FRotator ControlRotation = FRotator::ZeroRotator;
	const ElysiumFeedTargeting::FProbe BodyProbe =
		ElysiumFeedTargeting::MakeProbe(UseOrigin, ControlRotation);
	TestTrue(TEXT("feed probe starts at the body-relative use origin"),
		BodyProbe.Start.Equals(UseOrigin));
	TestTrue(TEXT("feed probe keeps the recovered local endpoint"),
		BodyProbe.End.Equals(UseOrigin + FVector(32.0f, 0.0f, -32.0f) * ElysiumMove::U));
	TestTrue(TEXT("feed probe keeps the recovered eight-unit hull"),
		BodyProbe.HullExtent.Equals(FVector(8.0f * ElysiumMove::U)));

	const FVector BodyMidpoint = FMath::Lerp(BodyProbe.Start, BodyProbe.End, 0.5f);
	const FVector RenderedCamera = UseOrigin - FVector(220.0f, 0.0f, 0.0f);
	const ElysiumFeedTargeting::FProbe RenderedProbe =
		ElysiumFeedTargeting::MakeProbe(RenderedCamera, ControlRotation);
	const FVector RenderedMidpoint = FMath::Lerp(RenderedProbe.Start, RenderedProbe.End, 0.5f);
	const FVector TargetExtent(10.0f);
	const FBox FrontTarget(BodyMidpoint - TargetExtent, BodyMidpoint + TargetExtent);
	const FBox RearTarget(RenderedMidpoint - TargetExtent, RenderedMidpoint + TargetExtent);
	TestTrue(TEXT("body-origin probe reaches the victim in front"),
		FMath::LineBoxIntersection(FrontTarget, BodyProbe.Start, BodyProbe.End,
			BodyProbe.End - BodyProbe.Start));
	TestFalse(TEXT("body-origin probe rejects the victim behind"),
		FMath::LineBoxIntersection(RearTarget, BodyProbe.Start, BodyProbe.End,
			BodyProbe.End - BodyProbe.Start));
	TestFalse(TEXT("the old rendered-camera origin misses the forward victim"),
		FMath::LineBoxIntersection(FrontTarget, RenderedProbe.Start, RenderedProbe.End,
			RenderedProbe.End - RenderedProbe.Start));
	TestTrue(TEXT("the old rendered-camera origin produces the reversed rear hit"),
		FMath::LineBoxIntersection(RearTarget, RenderedProbe.Start, RenderedProbe.End,
			RenderedProbe.End - RenderedProbe.Start));

	// Reproduce the production ownership shape: a separately owned skeletal visual is attached
	// below the native NPC body's blocking capsule.
	AActor* MotorOwner = World->SpawnActor<AActor>();
	UCapsuleComponent* Capsule = MotorOwner
		? NewObject<UCapsuleComponent>(MotorOwner, TEXT("FeedCandidateCapsule")) : nullptr;
	AActor* VisualOwner = World->SpawnActor<AActor>();
	USceneComponent* CandidateVisual = VisualOwner
		? NewObject<USceneComponent>(VisualOwner, TEXT("FeedCandidateVisual")) : nullptr;
	if (!TestNotNull(TEXT("candidate motor"), MotorOwner)
		|| !TestNotNull(TEXT("candidate capsule"), Capsule)
		|| !TestNotNull(TEXT("candidate visual"), CandidateVisual))
	{
		return false;
	}
	MotorOwner->SetRootComponent(Capsule);
	Capsule->InitCapsuleSize(20.0f, 40.0f);
	Capsule->SetWorldLocation(FVector(100.0f, 0.0f, 0.0f));
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
	Capsule->RegisterComponent();
	MotorOwner->AddInstanceComponent(Capsule);

	VisualOwner->SetRootComponent(CandidateVisual);
	CandidateVisual->RegisterComponent();
	VisualOwner->AddInstanceComponent(CandidateVisual);
	CandidateVisual->AttachToComponent(Capsule, FAttachmentTransformRules::KeepWorldTransform);

	FHitResult Hit;
	TestTrue(TEXT("the feed occlusion ray reaches the candidate capsule"),
		World->LineTraceSingleByChannel(Hit, FVector::ZeroVector, FVector(150.0f, 0.0f, 0.0f),
			ELYSIUM_USE_CHANNEL));
	TestTrue(TEXT("the candidate's ancestor capsule is an acceptable terminal hit"),
		ElysiumFeedTargeting::HitBelongsToCandidate(Hit.GetComponent(), CandidateVisual));

	AActor* WallOwner = World->SpawnActor<AActor>();
	UBoxComponent* Wall = WallOwner
		? NewObject<UBoxComponent>(WallOwner, TEXT("FeedOccludingWall")) : nullptr;
	if (!TestNotNull(TEXT("feed wall"), Wall))
	{
		return false;
	}
	WallOwner->SetRootComponent(Wall);
	Wall->InitBoxExtent(FVector(4.0f, 40.0f, 40.0f));
	Wall->SetWorldLocation(FVector(50.0f, 0.0f, 0.0f));
	Wall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
	Wall->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Block);
	Wall->RegisterComponent();
	WallOwner->AddInstanceComponent(Wall);

	Hit = FHitResult();
	TestTrue(TEXT("the nearer wall blocks the feed ray"),
		World->LineTraceSingleByChannel(Hit, FVector::ZeroVector, FVector(150.0f, 0.0f, 0.0f),
			ELYSIUM_USE_CHANNEL));
	TestFalse(TEXT("an intervening wall is not the candidate body"),
		ElysiumFeedTargeting::HitBelongsToCandidate(Hit.GetComponent(), CandidateVisual));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapActorTeardownTest,
	"Elysium.Substrate.MapActorTeardown", GElysiumTestFlags)
bool FElysiumMapActorTeardownTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)
		|| !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	AElysiumMapActor* Map = World ? World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity) : nullptr;
	if (!TestNotNull(TEXT("stage map actor"), Map))
	{
		return false;
	}
	Map->bStageOnly = true;
	Map->MapName.Reset();
	Map->FinishSpawning(FTransform::Identity);

	FElysiumEntityWorld* EntityWorld = Map->GetEntityWorld();
	if (!TestNotNull(TEXT("stage map owns an entity world before teardown"), EntityWorld))
	{
		return false;
	}

	// A non-brush source creates the owned query proxy that exposed the late-destruction crash.
	UBoxComponent* Source = NewObject<UBoxComponent>(Map, TEXT("TeardownUseSource"));
	Source->SetupAttachment(Map->GetRootComponent());
	Source->RegisterComponent();
	Map->AddInstanceComponent(Source);
	Map->RegisterUseAnchor(Source, EntityWorld->PlayerHandle());

	if (!TestWorld.EndPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	TestNull(TEXT("EndPlay destroys the substrate before UObject reclamation"), Map->GetEntityWorld());
	return !HasAnyErrors();
}

// =====================================================================================
// The theatre detour is a pure decision at the travel funnel: only the authored genesis→theatre
// destination is rewritten, and a rewrite is a direct tutorial-landmark entry.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStorySkipTest,
	"Elysium.Substrate.StorySkip", GElysiumTestFlags)
bool FElysiumStorySkipTest::RunTest(const FString&)
{
	{
		FString Map = ElysiumStory::TheatreMap;
		FString Landmark = TEXT("newgame");
		FVector Offset(10.f, 20.f, 30.f);
		bool bHasYaw = true;
		TestTrue(TEXT("the enabled skip rewrites the theatre leg"),
			ElysiumStory::ResolveIntroSkip(true, Map, Landmark, Offset, bHasYaw));
		TestEqual(TEXT("the rewrite selects the tutorial map"), Map,
			FString(ElysiumStory::TutorialMap));
		TestEqual(TEXT("the rewrite selects the tutorial landmark"), Landmark,
			FString(ElysiumStory::TutorialLandmark));
		TestTrue(TEXT("the source-landmark offset is dropped"), Offset.IsNearlyZero());
		TestFalse(TEXT("the source yaw is dropped"), bHasYaw);
	}

	{
		FString Map = ElysiumStory::TheatreMap;
		FString Landmark = TEXT("newgame");
		FVector Offset(1.f, 2.f, 3.f);
		bool bHasYaw = true;
		TestFalse(TEXT("a disabled skip leaves theatre authored"),
			ElysiumStory::ResolveIntroSkip(false, Map, Landmark, Offset, bHasYaw));
		TestEqual(TEXT("theatre remains the destination"), Map,
			FString(ElysiumStory::TheatreMap));
		TestEqual(TEXT("the authored landmark remains"), Landmark, FString(TEXT("newgame")));
		TestTrue(TEXT("the authored offset remains"), Offset.Equals(FVector(1.f, 2.f, 3.f)));
		TestTrue(TEXT("the authored yaw remains"), bHasYaw);
	}

	{
		FString Map = TEXT("sm_pawnshop_1");
		FString Landmark = TEXT("newgame");
		FVector Offset(4.f, 5.f, 6.f);
		bool bHasYaw = true;
		TestFalse(TEXT("the skip does not rewrite another destination"),
			ElysiumStory::ResolveIntroSkip(true, Map, Landmark, Offset, bHasYaw));
		TestEqual(TEXT("another map remains untouched"), Map, FString(TEXT("sm_pawnshop_1")));
		TestTrue(TEXT("another destination keeps its placement"),
			Offset.Equals(FVector(4.f, 5.f, 6.f)) && bHasYaw);
	}

	{
		FVector Offset(1120.756f, 325.981f, -89.643f);
		bool bHasYaw = true;
		TestTrue(TEXT("the authored theatre exit selects direct tutorial placement"),
			ElysiumStory::ResolveTheatreExitPlacement(ElysiumStory::TheatreMap,
				ElysiumStory::TutorialMap, ElysiumStory::TutorialLandmark, Offset, bHasYaw));
		TestTrue(TEXT("the theatre cinematic displacement is dropped"), Offset.IsNearlyZero());
		TestFalse(TEXT("the tutorial landmark supplies the arrival facing"), bHasYaw);
	}

	{
		FVector Offset(7.f, 8.f, 9.f);
		bool bHasYaw = true;
		TestFalse(TEXT("ordinary landmark travel keeps its relative placement"),
			ElysiumStory::ResolveTheatreExitPlacement(TEXT("sp_tutorial_1"),
				TEXT("sm_pawnshop_1"), TEXT("newgame"), Offset, bHasYaw));
		TestTrue(TEXT("ordinary placement is untouched"),
			Offset.Equals(FVector(7.f, 8.f, 9.f)) && bHasYaw);
	}

	return true;
}

} // namespace ElysiumPlayerWorldTests

#endif // WITH_DEV_AUTOMATION_TESTS
