// Content-free Substrate automation: NPC makers, interesting places, disposition, relationships, NPC state, and runtime spawn.
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
#include "ElysiumAnimationIntent.h"          // ElysiumAnimIntent::GaitFrom — the classifier's reference
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
#include "Substrate/ElysiumNpcGait.h"       // the travel-speed fallback a body with no fan takes
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumRulebook.h"
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
namespace ElysiumNpcTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// CNPCMaker's recovered admission order, tutorial live ceiling, and saved owner lifecycle.
// =====================================================================================

namespace
{
	class FElysiumNpcMakerSceneBlockerTestEntity final : public FElysiumEntity
	{
	public:
		virtual bool BlocksNpcMakerSpawns() const override { return !IsInert(); }
	};
	TUniquePtr<FElysiumEntity> MakeNpcMakerSceneBlockerTestEntity()
	{
		return MakeUnique<FElysiumNpcMakerSceneBlockerTestEntity>();
	}
	FElysiumClassRegistrar GRegNpcMakerSceneBlockerTest(
		TEXT("elysium_test_npc_maker_scene_blocker"), ElysiumBaseClassName(),
		&MakeNpcMakerSceneBlockerTestEntity, [](FElysiumClassDesc&) {});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerLifecycleTest,
	"Elysium.Substrate.NpcMakerLifecycle", GElysiumTestFlags)
bool FElysiumNpcMakerLifecycleTest::RunTest(const FString&)
{
	auto MakeDefs = []()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__npc_maker_lifecycle__");
		FElysiumEntityDef Maker;
		Maker.Classname = TEXT("npc_maker");
		Maker.TargetName = TEXT("maker");
		Maker.Origin = FVector(100.0f * ElysiumMove::U, 0.0f, 0.0f);
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VPedestrian"));
		Maker.Keys.Add(TEXT("NPCTargetname"), TEXT("child"));
		Maker.Keys.Add(TEXT("model"), TEXT("models/test_child.mdl"));
		Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
		Maker.Keys.Add(TEXT("Flag_InfChild"), TEXT("1"));
		Maker.Keys.Add(TEXT("Flag_NPCClip"), TEXT("1"));
		Maker.Keys.Add(TEXT("Flag_ViewCone"), TEXT("1"));
		Maker.Keys.Add(TEXT("MaxNPCCount"), TEXT("1"));
		Maker.Keys.Add(TEXT("MaxLiveChildren"), TEXT("1"));
		Maker.Keys.Add(TEXT("SpawnFrequency"), TEXT("5"));
		Maker.Keys.Add(TEXT("MinPCDistance"), TEXT("200"));
		FElysiumOutputDef Death;
		Death.Name = TEXT("OnDeath");
		Death.Target = TEXT("nobody");
		Death.Input = TEXT("Trigger");
		Death.Times = 1;
		Maker.Outputs.Add(Death);
		FElysiumOutputDef Spawned;
		Spawned.Name = TEXT("OnSpawnNPC");
		Spawned.Target = TEXT("spawncount");
		Spawned.Input = TEXT("Add");
		Spawned.Param = TEXT("1");
		Maker.Outputs.Add(Spawned);
		Defs.Defs.Add(MoveTemp(Maker));
		FElysiumEntityDef SceneBlocker;
		SceneBlocker.Classname = TEXT("elysium_test_npc_maker_scene_blocker");
		SceneBlocker.TargetName = TEXT("scene_blocker");
		SceneBlocker.bStartHidden = true;
		Defs.Defs.Add(MoveTemp(SceneBlocker));
		FElysiumEntityDef SpawnCounter;
		SpawnCounter.Classname = TEXT("math_counter");
		SpawnCounter.TargetName = TEXT("spawncount");
		Defs.Defs.Add(MoveTemp(SpawnCounter));
		return Defs;
	};
	auto ReadInt = [this](const FElysiumEntity& Ent, const TCHAR* Name) -> int32
	{
		const FElysiumFieldAccessor* Field = Ent.Class
			? FElysiumClassRegistry::Get().FindField(*Ent.Class, FName(Name)) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("field %s resolves"), Name), Field))
		{
			return static_cast<int32>(INDEX_NONE);
		}
		return Field->Get(Ent).ToInt();
	};
	auto SetInt = [this](FElysiumEntity& Ent, const TCHAR* Name, int32 Value)
	{
		const FElysiumFieldAccessor* Field = Ent.Class
			? FElysiumClassRegistry::Get().FindField(*Ent.Class, FName(Name)) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("field %s is writable"), Name), Field))
		{
			return;
		}
		Field->Set(Ent, FElysiumVariant::Int(Value));
	};
	auto ExplicitSpawn = [](FElysiumEntityWorld& World, const FElysiumEntityHandle& Maker)
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("Spawn")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Maker);
		World.Tick(0.0);
	};
	auto CounterValue = [](const FElysiumEntity* Counter)
	{
		TArray<TPair<FString, FString>> State;
		if (Counter) { Counter->GetDebugState(State); }
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.0f;
	};

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.PlayerLocation = FVector::ZeroVector;
	Services.bUseNpcMakerGroundZ = true;
	Services.NpcMakerGroundZ = 25.0f;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntity* Maker = World.FindByName(TEXT("maker"));
	if (!TestNotNull(TEXT("maker exists"), Maker))
	{
		return false;
	}
	TestEqual(TEXT("start-disabled installs no automatic think"), Maker->NextThink, ELYSIUM_NEVER_THINK);
	const int32 Before = World.NumEntities();
	FElysiumEntity* SceneBlocker = World.FindByName(TEXT("scene_blocker"));
	if (!TestNotNull(TEXT("scene blocker exists"), SceneBlocker))
	{
		return false;
	}
	SceneBlocker->ScriptUnhide();
	Services.Calls.Reset();
	ExplicitSpawn(World, Maker->Handle);
	TestEqual(TEXT("active scene rejection allocates nothing"), World.NumEntities(), Before);
	TestEqual(TEXT("ground is resolved before admission"),
		Services.Count(TEXT("ResolveNpcMakerGroundZ")), 1);
	TestTrue(TEXT("ground depth converts 2048 Source units once"),
		Services.Log().Contains(TEXT("depth=5201.92")));
	TestEqual(TEXT("scene gate follows the live ceiling and precedes player guards"),
		Services.Count(TEXT("IsNpcMakerVisible")), 0);
	SceneBlocker->ScriptHide();

	Services.Calls.Reset();
	Services.bNpcMakerVisible = true;
	Services.bNpcMakerInViewCone = true;
	Services.bNpcMakerOccupied = true;
	ExplicitSpawn(World, Maker->Handle);
	TestEqual(TEXT("visible rejection allocates nothing"), World.NumEntities(), Before);
	TestEqual(TEXT("cached ground is not retraced"), Services.Count(TEXT("ResolveNpcMakerGroundZ")), 0);
	TestEqual(TEXT("visibility is the first player guard"), Services.Count(TEXT("IsNpcMakerVisible")), 1);
	TestEqual(TEXT("visibility short-circuits the cone"), Services.Count(TEXT("IsNpcMakerInPlayerViewCone")), 0);

	Services.Calls.Reset();
	Services.bNpcMakerVisible = false;
	ExplicitSpawn(World, Maker->Handle);
	TestEqual(TEXT("view-cone rejection allocates nothing"), World.NumEntities(), Before);
	TestEqual(TEXT("cached nonzero ground is not retraced"), Services.Count(TEXT("ResolveNpcMakerGroundZ")), 0);
	TestEqual(TEXT("cone follows visibility"), Services.Count(TEXT("IsNpcMakerInPlayerViewCone")), 1);

	Services.Calls.Reset();
	Services.bNpcMakerInViewCone = false;
	ExplicitSpawn(World, Maker->Handle);
	TestEqual(TEXT("minimum-distance rejection allocates nothing"), World.NumEntities(), Before);
	TestEqual(TEXT("distance short-circuits occupancy"), Services.Count(TEXT("IsNpcMakerSpawnAreaOccupied")), 0);

	SetInt(*Maker, TEXT("MinPCDistance"), 100); // exact equality after truncation is admitted onward
	Services.Calls.Reset();
	ExplicitSpawn(World, Maker->Handle);
	TestEqual(TEXT("occupancy rejection allocates nothing"), World.NumEntities(), Before);
	TestEqual(TEXT("occupancy is the final guard"), Services.Count(TEXT("IsNpcMakerSpawnAreaOccupied")), 1);
	TestTrue(TEXT("occupancy half-extent converts 34 Source units once"),
		Services.Log().Contains(TEXT("half=86.36")));

	Services.Calls.Reset();
	Services.bNpcMakerOccupied = false;
	ExplicitSpawn(World, Maker->Handle);
	FElysiumEntity* Child = World.FindByName(TEXT("child"));
	if (!TestNotNull(TEXT("disabled maker accepts explicit Spawn"), Child))
	{
		return false;
	}
	TestEqual(TEXT("one child was allocated"), World.NumEntities(), Before + 1);
	TestEqual(TEXT("infinite mode forces fade spawnflags"), Child->SpawnFlags, 0x204);
	TestEqual(TEXT("child owns the maker handle"), Child->GetOwnerEntity().Index, Maker->Handle.Index);
	TestFalse(TEXT("maker controls are absent from the child template"),
		Child->Def->Keys.Contains(TEXT("MaxLiveChildren")));
	TestEqual(TEXT("ordinary template keys are cloned"), Child->Def->Keys.FindRef(TEXT("model")),
		FString(TEXT("models/test_child.mdl")));
	TestEqual(TEXT("child has fresh output counters"),
		Child->OutputTimesRemaining.Num(), Maker->Def->Outputs.Num());
	TestEqual(TEXT("maker live count increments after dispatch"),
		ReadInt(*Maker, TEXT("m_cLiveChildren")), 1);
	TestEqual(TEXT("successful construction fires one maker OnSpawnNPC"),
		CounterValue(World.FindByName(TEXT("spawncount"))), 1.0f);

	Services.Calls.Reset();
	ExplicitSpawn(World, Maker->Handle);
	TestEqual(TEXT("MaxLiveChildren rejects the second tutorial-equivalent Spawn"),
		World.NumEntities(), Before + 1);
	const int32 LiveLimitGeometryCalls =
		Services.Count(TEXT("ResolveNpcMakerGroundZ"))
		+ Services.Count(TEXT("IsNpcMakerVisible"))
		+ Services.Count(TEXT("IsNpcMakerInPlayerViewCone"))
		+ Services.Count(TEXT("IsNpcMakerSpawnAreaOccupied"));
	TestEqual(TEXT("live limit short-circuits all host geometry"), LiveLimitGeometryCalls, 0);
	TestEqual(TEXT("live-limit rejection fires no second OnSpawnNPC"),
		CounterValue(World.FindByName(TEXT("spawncount"))), 1.0f);

	World.EnqueueInput(TEXT("!self"), FName(TEXT("Enable")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Maker->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("Enable schedules an immediate maker think"), Maker->NextThink, 0.0f);
	World.Tick(0.0);
	TestEqual(TEXT("a timed live-limit retry uses SpawnFrequency"), Maker->NextThink, 5.0f);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Disable")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Maker->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("Disable clears the maker think"), Maker->NextThink, ELYSIUM_NEVER_THINK);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Toggle")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Maker->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("Toggle delegates disabled to immediate Enable"), Maker->NextThink, 0.0f);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Toggle")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Maker->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("Toggle delegates enabled to Disable"), Maker->NextThink, ELYSIUM_NEVER_THINK);

	// Player-dependent guards fail open when no player entity exists; occupancy still runs.
	FElysiumRecordingServices NoPlayerServices;
	NoPlayerServices.bNpcMakerVisible = true;
	NoPlayerServices.bNpcMakerInViewCone = true;
	FElysiumEntityWorld NoPlayer(nullptr, nullptr, NoPlayerServices.Bundle());
	NoPlayer.Load(MakeDefs());
	NoPlayer.Activate(0.0);
	FElysiumEntity* NoPlayerMaker = NoPlayer.FindByName(TEXT("maker"));
	if (!TestNotNull(TEXT("missing-player maker exists"), NoPlayerMaker))
	{
		return false;
	}
	ExplicitSpawn(NoPlayer, NoPlayerMaker->Handle);
	TestNotNull(TEXT("missing player admits past visibility, cone, and distance"),
		NoPlayer.FindByName(TEXT("child")));
	TestEqual(TEXT("missing player skips visibility host query"),
		NoPlayerServices.Count(TEXT("IsNpcMakerVisible")), 0);
	TestEqual(TEXT("missing player skips cone host query"),
		NoPlayerServices.Count(TEXT("IsNpcMakerInPlayerViewCone")), 0);
	TestEqual(TEXT("missing player still checks occupancy"),
		NoPlayerServices.Count(TEXT("IsNpcMakerSpawnAreaOccupied")), 1);

	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);
	FElysiumRecordingServices RestoredServices;
	RestoredServices.bHasPlayer = true;
	FElysiumEntityWorld Restored(nullptr, nullptr, RestoredServices.Bundle());
	Restored.Load(MakeDefs());
	Restored.SpawnPlayer();
	Restored.ApplySnapshot(Snapshot);
	Restored.Activate(0.0);
	FElysiumEntity* RestoredMaker = Restored.FindByName(TEXT("maker"));
	FElysiumEntity* RestoredChild = Restored.FindByName(TEXT("child"));
	if (!TestNotNull(TEXT("maker restores"), RestoredMaker)
		|| !TestNotNull(TEXT("runtime child restores"), RestoredChild))
	{
		return false;
	}
	TestEqual(TEXT("live ceiling survives save/load"),
		ReadInt(*RestoredMaker, TEXT("m_cLiveChildren")), 1);
	TestEqual(TEXT("owner handle rebases to the restored maker"),
		RestoredChild->GetOwnerEntity().Index, RestoredMaker->Handle.Index);
	const int32 RestoredCount = Restored.NumEntities();
	ExplicitSpawn(Restored, RestoredMaker->Handle);
	TestEqual(TEXT("restored live child still blocks another spawn"),
		Restored.NumEntities(), RestoredCount);

	FElysiumCombatCharacter* RestoredCharacter = RestoredChild->AsCombatCharacter();
	if (!TestNotNull(TEXT("restored child is a combat character"), RestoredCharacter))
	{
		return false;
	}
	RestoredCharacter->OnKilled();
	TestEqual(TEXT("death decrements the restored maker once"),
		ReadInt(*RestoredMaker, TEXT("m_cLiveChildren")), 0);
	RestoredChild->Kill();
	TestEqual(TEXT("death followed by Kill does not notify twice"),
		ReadInt(*RestoredMaker, TEXT("m_cLiveChildren")), 0);

	// Removing a live finite child refunds the consumed total and never needs a rendered body.
	FElysiumEntityDefs FiniteDefs = MakeDefs();
	FiniteDefs.MapName = TEXT("__npc_maker_finite_remove__");
	FElysiumEntityDef& FiniteMakerDef = FiniteDefs.Defs[0];
	FiniteMakerDef.Keys.Add(TEXT("Flag_InfChild"), TEXT("0"));
	FiniteMakerDef.Keys.Add(TEXT("Flag_NPCClip"), TEXT("0"));
	FiniteMakerDef.Keys.Add(TEXT("Flag_ViewCone"), TEXT("0"));
	FiniteMakerDef.Keys.Add(TEXT("MinPCDistance"), TEXT("0"));
	FiniteMakerDef.Outputs[0].Target = TEXT("childdeath");
	FiniteMakerDef.Outputs[0].Input = TEXT("Add");
	FiniteMakerDef.Outputs[0].Param = TEXT("1");
	struct FMakerDeathWire { const TCHAR* Output; const TCHAR* Target; };
	for (const FMakerDeathWire& Output : {
		FMakerDeathWire{ TEXT("OnNPCDied"), TEXT("makerdeath") },
		FMakerDeathWire{ TEXT("OnLastNPCDied"), TEXT("lastdeath") } })
	{
		FElysiumOutputDef Wire;
		Wire.Name = Output.Output;
		Wire.Target = Output.Target;
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("1");
		FiniteMakerDef.Outputs.Add(MoveTemp(Wire));
	}
	for (const TCHAR* Name : { TEXT("childdeath"), TEXT("makerdeath"), TEXT("lastdeath") })
	{
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = Name;
		FiniteDefs.Defs.Add(MoveTemp(Counter));
	}
	FElysiumRecordingServices FiniteServices;
	FElysiumEntityWorld Finite(nullptr, nullptr, FiniteServices.Bundle());
	Finite.Load(MoveTemp(FiniteDefs));
	Finite.Activate(0.0);
	FElysiumEntity* FiniteMaker = Finite.FindByName(TEXT("maker"));
	if (!TestNotNull(TEXT("finite maker exists"), FiniteMaker))
	{
		return false;
	}
	ExplicitSpawn(Finite, FiniteMaker->Handle);
	FElysiumEntity* FiniteChild = Finite.FindByName(TEXT("child"));
	if (!TestNotNull(TEXT("finite child spawns"), FiniteChild))
	{
		return false;
	}
	TestEqual(TEXT("finite child receives the non-fade spawnflags"), FiniteChild->SpawnFlags, 4);
	TestEqual(TEXT("finite spawn consumes the remaining total"),
		ReadInt(*FiniteMaker, TEXT("MaxNPCCount")), 0);
	FiniteChild->Kill();
	TestEqual(TEXT("live removal refunds the finite total"),
		ReadInt(*FiniteMaker, TEXT("MaxNPCCount")), 1);
	TestEqual(TEXT("live removal decrements the live count"),
		ReadInt(*FiniteMaker, TEXT("m_cLiveChildren")), 0);
	Finite.Tick(0.0);
	TestEqual(TEXT("live removal fires no maker death output"),
		CounterValue(Finite.FindByName(TEXT("makerdeath"))), 0.0f);
	ExplicitSpawn(Finite, FiniteMaker->Handle);
	FElysiumEntity* DeadChild = nullptr;
	for (const TUniquePtr<FElysiumEntity>& Candidate : Finite.Entities())
	{
		if (Candidate.IsValid() && !Candidate->IsDead()
			&& Candidate->TargetName.Equals(TEXT("child"), ESearchCase::IgnoreCase))
		{
			DeadChild = Candidate.Get();
		}
	}
	if (!TestNotNull(TEXT("replacement finite child spawns"), DeadChild))
	{
		return false;
	}
	DeadChild->AsCombatCharacter()->OnKilled();
	Finite.Tick(0.0);
	TestEqual(TEXT("child OnDeath fires once"),
		CounterValue(Finite.FindByName(TEXT("childdeath"))), 1.0f);
	TestEqual(TEXT("genuine death fires maker OnNPCDied"),
		CounterValue(Finite.FindByName(TEXT("makerdeath"))), 1.0f);
	TestEqual(TEXT("finite depletion fires OnLastNPCDied"),
		CounterValue(Finite.FindByName(TEXT("lastdeath"))), 1.0f);
	TestEqual(TEXT("genuine death does not refund finite total"),
		ReadInt(*FiniteMaker, TEXT("MaxNPCCount")), 0);
	Finite.EnqueueInput(TEXT("!self"), FName(TEXT("Enable")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), FiniteMaker->Handle);
	Finite.Tick(0.0);
	TestEqual(TEXT("Enable refuses finite depletion"), FiniteMaker->NextThink, ELYSIUM_NEVER_THINK);
	ExplicitSpawn(Finite, FiniteMaker->Handle);
	TestEqual(TEXT("explicit Spawn ignores finite depletion and may cross below zero"),
		ReadInt(*FiniteMaker, TEXT("MaxNPCCount")), -1);

	// A non-live-limit timed rejection uses the named 1..2 second retry stream.
	FElysiumEntityDefs TimedDefs = MakeDefs();
	TimedDefs.MapName = TEXT("__npc_maker_timed_retry__");
	TimedDefs.Defs[0].Keys.Add(TEXT("Flag_StartDisabled"), TEXT("0"));
	TimedDefs.Defs[0].Keys.Add(TEXT("MinPCDistance"), TEXT("0"));
	FElysiumRecordingServices TimedServices;
	TimedServices.bHasPlayer = true;
	TimedServices.bNpcMakerVisible = true;
	FElysiumEntityWorld Timed(nullptr, nullptr, TimedServices.Bundle());
	Timed.Load(MoveTemp(TimedDefs));
	Timed.SpawnPlayer();
	Timed.Activate(0.0);
	FElysiumEntity* TimedMaker = Timed.FindByName(TEXT("maker"));
	if (!TestNotNull(TEXT("timed maker exists"), TimedMaker))
	{
		return false;
	}
	TestEqual(TEXT("enabled maker initially uses SpawnFrequency"), TimedMaker->NextThink, 5.0f);
	Timed.Tick(5.0);
	TestTrue(TEXT("transient timed rejection retries at least one second later"),
		TimedMaker->NextThink >= 6.0f);
	TestTrue(TEXT("transient timed rejection retries no more than two seconds later"),
		TimedMaker->NextThink <= 7.0f);
	TimedServices.bNpcMakerVisible = false;
	const float TimedSuccessAt = TimedMaker->NextThink;
	Timed.Tick(TimedSuccessAt);
	TestNotNull(TEXT("a later timed attempt can construct a child"), Timed.FindByName(TEXT("child")));
	TestEqual(TEXT("timed success retries at SpawnFrequency"),
		TimedMaker->NextThink, TimedSuccessAt + 5.0f);

	// Version 12 ended at the pre-maker NPC leaf. Reading that exact shape leaves ownership unset.
	FElysiumEntityDefs LegacyDefs;
	LegacyDefs.MapName = TEXT("__npc_maker_v12__");
	FElysiumEntityDef LegacyNpc;
	LegacyNpc.Classname = TEXT("npc_VPedestrian");
	LegacyNpc.TargetName = TEXT("legacy_child");
	LegacyDefs.Defs.Add(MoveTemp(LegacyNpc));
	FElysiumRecordingServices LegacyServices;
	FElysiumEntityWorld LegacyBefore(nullptr, nullptr, LegacyServices.Bundle());
	LegacyBefore.Load(MoveTemp(LegacyDefs));
	FElysiumEntity* LegacySource = LegacyBefore.FindByName(TEXT("legacy_child"));
	if (!TestNotNull(TEXT("v12 source NPC exists"), LegacySource))
	{
		return false;
	}
	TArray<uint8> LegacyLeaf;
	{
		FMemoryWriter Writer(LegacyLeaf, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::WireIdentity);
		LegacySource->Serialize(Ar);
	}
	FElysiumEntityDefs LegacyReadDefs;
	LegacyReadDefs.MapName = TEXT("__npc_maker_v12__");
	FElysiumEntityDef LegacyReadNpc;
	LegacyReadNpc.Classname = TEXT("npc_VPedestrian");
	LegacyReadNpc.TargetName = TEXT("legacy_child");
	LegacyReadDefs.Defs.Add(MoveTemp(LegacyReadNpc));
	FElysiumEntityWorld LegacyAfter(nullptr, nullptr, LegacyServices.Bundle());
	LegacyAfter.Load(MoveTemp(LegacyReadDefs));
	FElysiumEntity* LegacyDest = LegacyAfter.FindByName(TEXT("legacy_child"));
	if (!TestNotNull(TEXT("v12 destination NPC exists"), LegacyDest))
	{
		return false;
	}
	{
		FMemoryReader Reader(LegacyLeaf, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::WireIdentity);
		LegacyDest->Serialize(Ar);
	}
	TestFalse(TEXT("v12 NPC restores safely without a guessed maker"),
		LegacyDest->GetOwnerEntity().IsSet());

	return true;
}

// =====================================================================================
// FElysiumNpc / npc_maker — registry coverage, npc_maker.Spawn creating a live child on a bare
// world, dialogue latches, and the engine-neutral half of named patrol resolution/persistence.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInterestingPlacePolicyTest,
	"Elysium.Substrate.InterestingPlacePolicy", GElysiumTestFlags)
bool FElysiumInterestingPlacePolicyTest::RunTest(const FString&)
{
	const TArray<int32> Ratings = { 3, 5, 4, 5, 0 };
	FRandomStream Expected(0x4E5043);
	FRandomStream Actual(0x4E5043);
	bool bSawFirst = false;
	bool bSawSecond = false;
	bool bMatchesUniformDraws = true;
	for (int32 Draw = 0; Draw < 64; ++Draw)
	{
		const int32 ExpectedIndex = Expected.RandRange(0, 1) == 0 ? 1 : 3;
		const int32 ActualIndex = ElysiumInterestingPlaces::PickHighestRatedCandidate(
			Ratings, Actual);
		bMatchesUniformDraws &= ActualIndex == ExpectedIndex;
		bSawFirst |= ActualIndex == 1;
		bSawSecond |= ActualIndex == 3;
	}
	TestTrue(TEXT("highest-rating candidates map directly from uniform RNG draws"),
		bMatchesUniformDraws);
	TestTrue(TEXT("both candidates in the highest populated rating tier are reachable"),
		bSawFirst && bSawSecond);

	const TArray<int32> SingleTopRatings = { 4, 5, 4 };
	FRandomStream SingleTop(7);
	TestEqual(TEXT("a lower rating never outranks the only rating-5 candidate"),
		ElysiumInterestingPlaces::PickHighestRatedCandidate(
			SingleTopRatings, SingleTop), 1);
	const TArray<int32> InvalidRatings = { 6, -1 };
	FRandomStream OutsideRange(7);
	TestEqual(TEXT("ratings outside the recovered 0-5 range are not candidates"),
		ElysiumInterestingPlaces::PickHighestRatedCandidate(
			InvalidRatings, OutsideRange), INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDispositionLevelTest,
	"Elysium.Substrate.DispositionLevels", GElysiumTestFlags)
bool FElysiumDispositionLevelTest::RunTest(const FString&)
{
	const FString Text =
		TEXT("DispositionTable\n{\n")
		TEXT("Neutral { \"DispositionLevel\" \"1\" \"Animation Name\" \"Neutral\" ")
		TEXT("DefaultExpression { \"Expression Name\" \"Neutral\" \"Talking Expression\" \"NeutralTalk\" \"Intensity\" \"1\" } }\n")
		TEXT("Joy { \"DispositionLevel\" \"1\" \"Animation Name\" \"Joy\" \"Standing Fidget Chance\" \"25\" ")
		TEXT("DefaultExpression { \"Expression Name\" \"Smile\" \"Talking Expression\" \"SmileTalk\" \"Intensity\" \"0.5\" } }\n")
		TEXT("Joy { \"CopyDataFrom\" \"Joy\" \"DispositionLevel\" \"3\" \"Standing Fidget Chance\" \"75\" ")
		TEXT("DefaultExpression { \"Expression Name\" \"Grin\" } }\n}\n");
	FElysiumDispositionTable Table;
	FString Error;
	if (!TestTrue(TEXT("inline disposition table parses"),
		Table.ParseText(Text, TEXT("inline"), Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("duplicate names remain separate levels"), Table.Rows.Num(), 3);
	const FElysiumDisposition* Joy3 = Table.Resolve(TEXT("Joy"), 3);
	if (TestNotNull(TEXT("Joy level 3 resolves"), Joy3))
	{
		TestEqual(TEXT("level 3 keeps its authored level"), Joy3->Level, 3);
		TestEqual(TEXT("CopyDataFrom inherits the animation"), Joy3->AnimName, FString(TEXT("Joy")));
		TestEqual(TEXT("the level overrides its face"), Joy3->DefaultExpression, FString(TEXT("Grin")));
		TestEqual(TEXT("an omitted talking face inherits"), Joy3->TalkingExpression,
			FString(TEXT("SmileTalk")));
	}
	const FElysiumDisposition* Joy2 = Table.Resolve(TEXT("Joy"), 2);
	TestTrue(TEXT("a missing requested level decrements to level 1"), Joy2 && Joy2->Level == 1);
	const FElysiumDisposition* Unknown = Table.Resolve(TEXT("DoesNotExist"), 9);
	TestTrue(TEXT("an unknown name falls back to Neutral level 1"),
		Unknown && Unknown->Name == TEXT("Neutral") && Unknown->Level == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRelationshipsTest,
	"Elysium.Substrate.Relationships", GElysiumTestFlags)
bool FElysiumRelationshipsTest::RunTest(const FString&)
{
	FElysiumRelationships Table;
	const FElysiumEntityHandle Player(7, 1);
	TestTrue(TEXT("class relationship installs"),
		Table.SetClass(TEXT("player"), EElysiumRelationship::Like, 0));
	TestEqual(TEXT("class relationship resolves when no exact rule exists"),
		static_cast<uint8>(Table.Resolve(Player, TEXT("player"))),
		static_cast<uint8>(EElysiumRelationship::Like));
	TestTrue(TEXT("exact relationship installs"),
		Table.SetEntity(Player, EElysiumRelationship::Fear, 5));
	TestEqual(TEXT("exact entity outranks its class"),
		static_cast<uint8>(Table.Resolve(Player, TEXT("player"))),
		static_cast<uint8>(EElysiumRelationship::Fear));
	TestFalse(TEXT("a lower-priority rewrite cannot erase the winner"),
		Table.SetEntity(Player, EElysiumRelationship::Neutral, 1));
	TestTrue(TEXT("an equal-priority dialogue rewrite replaces it"),
		Table.SetEntity(Player, EElysiumRelationship::Hate, 5));

	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent=*/true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		Table.Serialize(Ar);
	}
	FElysiumRelationships Restored;
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent=*/true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		Restored.Serialize(Ar);
	}
	TestEqual(TEXT("relationship table survives its save block"),
		static_cast<uint8>(Restored.Resolve(FElysiumEntityHandle(7, 0), TEXT("player"))),
		static_cast<uint8>(EElysiumRelationship::Hate));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcTest, "Elysium.Substrate.Npc", GElysiumTestFlags)
bool FElysiumNpcTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();

	// The character leaf is registered for the beat's class and resolves its dialog-gating inputs.
	const FElysiumClassDesc* Vamp = Reg.Find(FName(TEXT("npc_VVampire")));
	if (!TestNotNull(TEXT("npc_VVampire registered"), Vamp))
	{
		return false;
	}
	for (const TCHAR* In : { TEXT("WillTalk"), TEXT("UseInteresting"), TEXT("StartPlayerDialogRemote"),
		TEXT("EndDialog"), TEXT("SetupPatrolType"), TEXT("FollowPatrolPath"),
		TEXT("ClearPatrolPath"), TEXT("SetRelationship"), TEXT("TeleportToEntity"), TEXT("Kill") })
	{
		TestNotNull(FString::Printf(TEXT("npc_VVampire.%s resolves"), In),
			reinterpret_cast<const void*>(Reg.FindInput(*Vamp, FName(In))));
	}
	// 9.3 field-table audit: `npc.times_talked` is a script-read field (santamonica et al.), so it must
	// resolve as a datamap field — otherwise the read raises AttributeError instead of returning 0.
	TestNotNull(TEXT("npc_VVampire.times_talked resolves as a field"),
		reinterpret_cast<const void*>(Reg.FindField(*Vamp, FName(TEXT("times_talked")))));
	TestNotNull(TEXT("npc_VVampire.interesting_place_groups resolves as a field"),
		reinterpret_cast<const void*>(Reg.FindField(*Vamp,
			FName(TEXT("interesting_place_groups")))));

	// The typo is the retail classname, not a fixture typo. It must resolve as a live capacity/
	// timing entity or sm_hub_1's 76 authored destinations remain inert records.
	const FElysiumClassDesc* InterestingDesc = Reg.Find(FName(TEXT("intersting_place")));
	if (!TestNotNull(TEXT("intersting_place registered with retail spelling"), InterestingDesc))
	{
		return false;
	}
	for (const TCHAR* In : { TEXT("Enable"), TEXT("Disable"), TEXT("Toggle") })
	{
		TestNotNull(FString::Printf(TEXT("intersting_place.%s resolves"), In),
			reinterpret_cast<const void*>(Reg.FindInput(*InterestingDesc, FName(In))));
	}
	for (const TCHAR* Field : { TEXT("type"), TEXT("enabled"), TEXT("max_npcs"),
		TEXT("group_id"), TEXT("rating"), TEXT("match_orientation"), TEXT("min_time"),
		TEXT("max_time") })
	{
		TestNotNull(FString::Printf(TEXT("intersting_place.%s resolves"), Field),
			reinterpret_cast<const void*>(Reg.FindField(*InterestingDesc, FName(Field))));
	}

	// The maker leaf resolves Spawn/Enable.
	const FElysiumClassDesc* MakerDesc = Reg.Find(FName(TEXT("npc_maker")));
	if (!TestNotNull(TEXT("npc_maker registered"), MakerDesc))
	{
		return false;
	}
	TestNotNull(TEXT("npc_maker.Spawn resolves"),
		reinterpret_cast<const void*>(Reg.FindInput(*MakerDesc, FName(TEXT("Spawn")))));
	TestNotNull(TEXT("npc_maker.Enable resolves"),
		reinterpret_cast<const void*>(Reg.FindInput(*MakerDesc, FName(TEXT("Enable")))));
	for (const TCHAR* Field : { TEXT("NPCType"), TEXT("MaxNPCCount"), TEXT("SpawnFrequency"),
		TEXT("MaxLiveChildren"), TEXT("NPCTargetname"), TEXT("Flag_StartDisabled"),
		TEXT("Flag_NPCClip"), TEXT("Flag_Fade"), TEXT("Flag_InfChild"), TEXT("Flag_NoDrop"),
		TEXT("Flag_ViewCone"), TEXT("MinPCDistance") })
	{
		const FElysiumFieldAccessor* Accessor = Reg.FindField(*MakerDesc, FName(Field));
		if (TestNotNull(FString::Printf(TEXT("npc_maker.%s resolves"), Field), Accessor))
		{
			TestTrue(FString::Printf(TEXT("npc_maker.%s is keyable"), Field), Accessor->bKeyable);
			TestTrue(FString::Printf(TEXT("npc_maker.%s is saved"), Field), Accessor->bSave);
		}
	}
	for (const TCHAR* Field : { TEXT("m_cLiveChildren"), TEXT("m_flGround") })
	{
		const FElysiumFieldAccessor* Accessor = Reg.FindField(*MakerDesc, FName(Field));
		if (TestNotNull(FString::Printf(TEXT("npc_maker.%s resolves"), Field), Accessor))
		{
			TestFalse(FString::Printf(TEXT("npc_maker.%s is not keyable"), Field), Accessor->bKeyable);
			TestTrue(FString::Printf(TEXT("npc_maker.%s is saved"), Field), Accessor->bSave);
		}
	}

	// --- A recorded world: Jack, a counter wired off his OnDialogBegin, and the blueblood maker ---
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__npc_test__");

	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	Jack.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	Jack.Keys.Add(TEXT("player_reaction"), TEXT("D_LI 0"));
	{
		FElysiumOutputDef Wire;   // OnDialogBegin -> counter.Add(1), to observe the fire
		Wire.Name = TEXT("OnDialogBegin");
		Wire.Target = TEXT("dlgcount");
		Wire.Input = TEXT("Add");
		Wire.Param = TEXT("1");
		Jack.Outputs.Add(Wire);
	}
	Defs.Defs.Add(MoveTemp(Jack));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("dlgcount");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityDef BluebloodMaker;
	BluebloodMaker.Classname = TEXT("npc_maker");
	BluebloodMaker.TargetName = TEXT("blueblood_maker");
	BluebloodMaker.Keys.Add(TEXT("NPCType"), TEXT("npc_VPedestrian"));
	BluebloodMaker.Keys.Add(TEXT("NPCTargetname"), TEXT("blueblood"));
	BluebloodMaker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
	BluebloodMaker.Keys.Add(TEXT("Flag_InfChild"), TEXT("1"));
	BluebloodMaker.Keys.Add(TEXT("MaxNPCCount"), TEXT("1"));
	BluebloodMaker.Keys.Add(TEXT("MaxLiveChildren"), TEXT("1"));
	BluebloodMaker.Keys.Add(TEXT("SpawnFrequency"), TEXT("5"));
	BluebloodMaker.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	BluebloodMaker.Keys.Add(TEXT("use_interesting"), TEXT("1"));
	BluebloodMaker.Keys.Add(TEXT("interesting_place_groups"), TEXT("31"));
	Defs.Defs.Add(MoveTemp(BluebloodMaker));

	FElysiumEntityDef Interesting;
	Interesting.Classname = TEXT("intersting_place");
	Interesting.TargetName = TEXT("ambient_1");
	Interesting.Origin = FVector(50.0f, 25.0f, 0.0f);
	Interesting.Keys.Add(TEXT("type"), TEXT("Citizen_Idle"));
	Interesting.Keys.Add(TEXT("enabled"), TEXT("1"));
	Interesting.Keys.Add(TEXT("max_npcs"), TEXT("4"));
	Interesting.Keys.Add(TEXT("group_id"), TEXT("31"));
	Interesting.Keys.Add(TEXT("min_time"), TEXT("10"));
	Interesting.Keys.Add(TEXT("max_time"), TEXT("20"));
	Defs.Defs.Add(MoveTemp(Interesting));

	for (int32 PointIndex = 1; PointIndex <= 2; ++PointIndex)
	{
		FElysiumEntityDef Point;
		Point.Classname = TEXT("info_node_patrol_point");
		Point.TargetName = FString::Printf(TEXT("route_%d"), PointIndex);
		Point.Origin = FVector(static_cast<float>(PointIndex * 100), 25.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Point));
	}

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.bProvideNpcMotor = true;
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* JackEnt = World.FindByName(TEXT("Jack"));
	FElysiumEntity* MakerEnt = World.FindByName(TEXT("blueblood_maker"));
	if (!TestNotNull(TEXT("Jack resolved"), JackEnt) || !TestNotNull(TEXT("maker resolved"), MakerEnt))
	{
		return false;
	}
	TestFalse(TEXT("Jack is a real class, not an inert record"), JackEnt->IsRecordOnly());
	FElysiumEntity* InterestingEnt = World.FindByName(TEXT("ambient_1"));
	if (TestNotNull(TEXT("interesting place resolved"), InterestingEnt))
	{
		TestFalse(TEXT("interesting place is live rather than record-only"),
			InterestingEnt->IsRecordOnly());
	}
	const FElysiumEntityHandle JackHandle = JackEnt->Handle;

	// npc_maker.Spawn produces the child NPC (the acceptance case).
	TestNull(TEXT("blueblood absent before Spawn"), World.FindByName(TEXT("blueblood")));
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Spawn")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), MakerEnt->Handle);
	for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

	FElysiumEntity* Blueblood = World.FindByName(TEXT("blueblood"));
	if (TestNotNull(TEXT("blueblood spawned via npc_maker.Spawn"), Blueblood))
	{
		TestEqual(TEXT("blueblood is npc_VPedestrian"), Blueblood->Def->Classname, FString(TEXT("npc_VPedestrian")));
		TestFalse(TEXT("blueblood is a real NPC, not an inert record"), Blueblood->IsRecordOnly());
		TestNotNull(TEXT("blueblood handle resolves"), World.Resolve(Blueblood->Handle));
	}
	const int32 EntitiesAfterFirstSpawn = World.NumEntities();
	const FElysiumEntityHandle FirstBlueblood = Blueblood
		? Blueblood->Handle : FElysiumEntityHandle::Invalid();
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Spawn")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), MakerEnt->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("MaxLiveChildren rejects a second explicit child"),
		World.NumEntities(), EntitiesAfterFirstSpawn);
	if (FElysiumEntity* OnlyBlueblood = World.FindByName(TEXT("blueblood")))
	{
		TestEqual(TEXT("the original child remains the named live child"),
			OnlyBlueblood->Handle.Index, FirstBlueblood.Index);
	}
	else
	{
		AddError(TEXT("the first Blueblood disappeared after a rejected second Spawn"));
	}

	// WillTalk latch + StartPlayerDialogRemote fires OnDialogBegin.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("WillTalk")), FElysiumVariant::Int(1), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("StartPlayerDialogRemote")), FElysiumVariant::Int(256), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

	// Read a keyed debug row (the concrete leaf is file-local, so its state surfaces via GetDebugState).
	auto DebugRow = [](const FElysiumEntity* E, const TCHAR* Key) -> FString
	{
		TArray<TPair<FString, FString>> Rows;
		E->GetDebugState(Rows);
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == Key) { return Row.Value; }
		}
		return FString();
	};
	if (Blueblood)
	{
		TestEqual(TEXT("maker forwards interesting-place groups"),
			DebugRow(Blueblood, TEXT("Interesting groups")), FString(TEXT("31")));
	}
	TestTrue(TEXT("player_reaction seeds the independent relationship table"),
		DebugRow(World.Resolve(JackHandle), TEXT("Relationship to player")).StartsWith(TEXT("D_LI")));
	// A `D_FR` row is a real enemy-selection input: a hostile or feared row can commit this NPC to
	// an enemy on a later think, and combat selection now answers with a registered fight program
	// rather than a refusal (`Elysium.Substrate.NpcCombat.*` owns those assertions).
	World.AcceptInput(TEXT("!self"), FName(TEXT("SetRelationship")),
		FElysiumVariant::String(TEXT("player D_FR 5")), JackHandle, JackHandle);
	TestTrue(TEXT("SetRelationship rewrites the independent combat-relationship table"),
		DebugRow(World.Resolve(JackHandle), TEXT("Relationship to player")).Contains(TEXT("D_FR")));
	TestTrue(TEXT("...carrying the authored IRelationPriority with it"),
		DebugRow(World.Resolve(JackHandle), TEXT("Relationship to player")).Contains(
			TEXT("priority 5")));

	TestEqual(TEXT("WillTalk latched"), DebugRow(World.Resolve(JackHandle), TEXT("WillTalk")), FString(TEXT("yes")));
	TestEqual(TEXT("OnDialogBegin fired once (counter=1)"),
		FCString::Atof(*DebugRow(World.FindByName(TEXT("dlgcount")), TEXT("Value"))), 1.0f);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("EndDialog")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.Tick(0.0);

	// The Python surface calls these exact names on sm_hub_1's two cops. The recording motor keeps
	// the route engine-neutral while making its move/stop requests observable in this tier.
	World.EnqueueInput(TEXT("!self"), FName(TEXT("SetupPatrolType")),
		FElysiumVariant::String(TEXT("255 0 FOLLOW_PATROL_PATH_WALK")), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.EnqueueInput(TEXT("!self"), FName(TEXT("FollowPatrolPath")),
		FElysiumVariant::String(TEXT("route_1 route_2")), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.Tick(0.0);
	TestTrue(TEXT("named patrol resolves and arms both authored points"),
		DebugRow(World.Resolve(JackHandle), TEXT("Patrol")).Contains(TEXT("point 1/2")));
	World.Tick(0.05);
	FElysiumRecordingNpcMotor* JackMotor = Services.NpcMotors.IsEmpty()
		? nullptr : Services.NpcMotors[0].Get();
	if (TestNotNull(TEXT("Jack owns the recording motor"), JackMotor))
	{
		TestTrue(TEXT("the NPC motor retains Jack rather than player identity"),
			JackMotor->Owner == JackHandle);
		TestTrue(TEXT("an active patrol issues a native movement request"), JackMotor->bMoving);
		TestTrue(TEXT("the native request carries the first authored point"),
			JackMotor->RequestedFeet.Equals(FVector(100.0f, 25.0f, 0.0f)));
	}

	FElysiumMapSnapshot PatrolSnapshot;
	World.Freeze(PatrolSnapshot);
	const FElysiumEntityState* SavedJack = PatrolSnapshot.Entities.FindByPredicate(
		[JackHandle](const FElysiumEntityState& State) { return State.Index == JackHandle.Index; });
	if (TestNotNull(TEXT("active patrol contributes a save record"), SavedJack))
	{
		TestTrue(TEXT("patrol cursor/path are carried by leaf state"), !SavedJack->LeafState.IsEmpty());
	}
	World.EnqueueInput(TEXT("!self"), FName(TEXT("ClearPatrolPath")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), JackHandle);
	World.Tick(0.05);
	if (JackMotor)
	{
		TestFalse(TEXT("clearing a patrol stops its native request"), JackMotor->bMoving);
		TestTrue(TEXT("clearing a patrol crosses the explicit motor Stop seam"),
			Services.Saw(TEXT("NpcMotor Stop")));
	}

	return true;
}

// LIFE3 — the cast travels at its own body's authored cell speed, not at a constant.
//
// The two halves of the same rule: a body whose export resolves a walk fan commands that fan's
// forward cell, and a body that resolves none commands `ElysiumNpcGait::WalkSpeed`. A patrol leg is
// the smallest producer that exercises both, and the recording motor is what makes the commanded
// number readable without a world.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcTravelSpeedTest,
	"Elysium.Substrate.Npc.TravelSpeed", GElysiumTestFlags)
bool FElysiumNpcTravelSpeedTest::RunTest(const FString&)
{
	// The male body's own forward walk cell, in cm/s — the number `walk_0` authors, which is 53.8 u/s
	// against `speed_walk`'s stated 100 and is the whole reason a constant slides.
	constexpr float AuthoredWalk = 136.7f;

	auto PatrolSpeed = [this](float BodyWalkSpeed) -> float
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__npc_travel_speed__");

		FElysiumEntityDef Walker;
		Walker.Classname = TEXT("npc_VVampire");
		Walker.TargetName = TEXT("walker");
		Walker.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Defs.Defs.Add(MoveTemp(Walker));

		for (int32 PointIndex = 1; PointIndex <= 2; ++PointIndex)
		{
			FElysiumEntityDef Point;
			Point.Classname = TEXT("info_node_patrol_point");
			Point.TargetName = FString::Printf(TEXT("route_%d"), PointIndex);
			Point.Origin = FVector(static_cast<float>(PointIndex * 100), 25.0f, 0.0f);
			Defs.Defs.Add(MoveTemp(Point));
		}

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.bProvideNpcMotor = true;
		Services.NpcWalkSpeedCmPerSecond = BodyWalkSpeed;
		FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* WalkerEnt = World.FindByName(TEXT("walker"));
		if (!TestNotNull(TEXT("the walker resolved"), WalkerEnt))
		{
			return 0.f;
		}
		World.EnqueueInput(TEXT("!self"), FName(TEXT("SetupPatrolType")),
			FElysiumVariant::String(TEXT("255 0 FOLLOW_PATROL_PATH_WALK")), 0.0,
			FElysiumEntityHandle::Invalid(), WalkerEnt->Handle);
		World.EnqueueInput(TEXT("!self"), FName(TEXT("FollowPatrolPath")),
			FElysiumVariant::String(TEXT("route_1 route_2")), 0.0,
			FElysiumEntityHandle::Invalid(), WalkerEnt->Handle);
		World.Tick(0.0);
		World.Tick(0.05);

		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("the walker owns a motor"), Motor))
		{
			return 0.f;
		}
		TestTrue(TEXT("the patrol issued its travel request"), Motor->bMoving);
		// The trap's first half: a patrol leg is gait-derived, so the motor must be told which fan
		// its speed came from — that is what lets a mid-leg equip/holster re-derive it below.
		TestTrue(TEXT("a patrol leg tags its motor with the walk fan it rode"),
			Motor->RequestedGaitKind.IsSet() && *Motor->RequestedGaitKind == EElysiumNpcGaitKind::Walk);
		return Motor->RequestedSpeedCmPerSecond;
	};

	TestTrue(TEXT("a body with a walk fan patrols at its own forward cell"),
		FMath::IsNearlyEqual(PatrolSpeed(AuthoredWalk), AuthoredWalk, 0.01f));
	TestTrue(TEXT("a body whose export resolves no fan keeps the stated constant"),
		FMath::IsNearlyEqual(PatrolSpeed(0.f), ElysiumNpcGait::WalkSpeed, 0.01f));

	// The trap's second half: a scripted `m_fMoveTo 1` (Walk) hands the motor the resolved clip's
	// OWN authored ground speed — not the gait fan's forward cell — via
	// `IElysiumEmbodiment::ResolveNpcActivityClip`. That speed must reach the motor untagged, so a
	// later fan change (a mid-beat equip) cannot silently overwrite a number the beat never asked to
	// have replaced.
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__npc_travel_speed_scripted__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("scripted_walker");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("scripted_walk");
		Seq.Origin = FVector(500.0f, 0.0f, 0.0f);
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("scripted_walker"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.bProvideNpcMotor = true;
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityClip = TEXT("walk_0");
		// Deliberately NOT the gait fan's own forward cell (AuthoredWalk above), so a test that
		// mistakenly re-derived this speed from the fan would read the wrong number.
		Services.ResolvedNpcGroundSpeedCmPerSecond = 210.0f;
		FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* SeqEnt = World.FindByName(TEXT("scripted_walk"));
		if (TestNotNull(TEXT("the scripted beat resolved"), SeqEnt))
		{
			World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
				FElysiumEntityHandle::Invalid(), SeqEnt->Handle);
			double Now = 0.0;
			for (int32 Index = 0; Index < 10; ++Index) { World.Tick(Now); Now += 0.1; }

			FElysiumRecordingNpcMotor* ScriptedMotor = Services.LastNpcMotor();
			if (TestNotNull(TEXT("the scripted walker owns a motor"), ScriptedMotor))
			{
				TestTrue(TEXT("the scripted walk issued its travel request"), ScriptedMotor->bMoving);
				TestTrue(TEXT("the scripted walk uses the clip's own authored ground speed"),
					FMath::IsNearlyEqual(ScriptedMotor->RequestedSpeedCmPerSecond, 210.0f, 0.01f));
				TestFalse(TEXT("a caller-authored speed is NOT tagged with a gait fan to re-derive from"),
					ScriptedMotor->RequestedGaitKind.IsSet());
			}
		}
	}

	// LIFE3 — a scripted `m_fMoveTo 2` (Run) rides the run FAN rather than a named clip, so it names
	// the fan on its way out: that tag is what lets the body's own animation pass re-derive the cell
	// as the leg turns. It is the exact opposite of the Walk case above, and both have to hold.
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__npc_travel_speed_scripted_run__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("scripted_runner");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("scripted_run");
		Seq.Origin = FVector(500.0f, 0.0f, 0.0f);
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("scripted_runner"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("2"));
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.bProvideNpcMotor = true;
		Services.NpcRunSpeedCmPerSecond = 500.0f;
		FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);

		FElysiumEntity* SeqEnt = World.FindByName(TEXT("scripted_run"));
		if (TestNotNull(TEXT("the scripted run beat resolved"), SeqEnt))
		{
			World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
				FElysiumEntityHandle::Invalid(), SeqEnt->Handle);
			double Now = 0.0;
			for (int32 Index = 0; Index < 10; ++Index) { World.Tick(Now); Now += 0.1; }

			FElysiumRecordingNpcMotor* RunMotor = Services.LastNpcMotor();
			if (TestNotNull(TEXT("the scripted runner owns a motor"), RunMotor))
			{
				TestTrue(TEXT("the scripted run issued its travel request"), RunMotor->bMoving);
				TestTrue(TEXT("a scripted run tags its motor with the run fan it rode"),
					RunMotor->RequestedGaitKind.IsSet()
						&& *RunMotor->RequestedGaitKind == EElysiumNpcGaitKind::Run);
				TestTrue(TEXT("...at that fan's own forward cell"),
					FMath::IsNearlyEqual(RunMotor->RequestedSpeedCmPerSecond, 500.0f, 0.01f));
			}
		}
	}

	// LIFE3 — and the number itself is per-direction. A travel request being ISSUED asks at forward,
	// because the leg has not started; the body's own animation pass asks again at the realized
	// `move_yaw` every frame after that, and a turnaround is where the two answers separate.
	{
		FElysiumRecordingNpcMotor Motor;
		Motor.AuthoredWalkSpeedCmPerSecond = AuthoredWalk;
		Motor.StrafeSpeedFraction = 0.5f;
		TestTrue(TEXT("a settled leg commands the forward cell"),
			FMath::IsNearlyEqual(ElysiumNpcGait::TravelSpeed(&Motor, EElysiumNpcGaitKind::Walk),
				AuthoredWalk, 0.01f));
		TestTrue(TEXT("a reversing one commands the cell it is about to play"),
			FMath::IsNearlyEqual(ElysiumNpcGait::TravelSpeed(&Motor, EElysiumNpcGaitKind::Walk, 180.0f),
				AuthoredWalk * 0.5f, 0.01f));
		// A body with no fan answers zero however it is pointed, so the stated constant is what a
		// direction can never quietly scale.
		FElysiumRecordingNpcMotor NoFan;
		TestTrue(TEXT("a body with no fan keeps the stated constant at every direction"),
			FMath::IsNearlyEqual(ElysiumNpcGait::TravelSpeed(&NoFan, EElysiumNpcGaitKind::Walk, 180.0f),
				ElysiumNpcGait::WalkSpeed, 0.01f));
	}

	// The classifier's threshold moves with the same tables, so a body walking at its authored cell
	// is not judged against a constant it can never reach.
	FElysiumGaitSpeeds Speeds;
	Speeds.Walk.Count = 3;
	Speeds.Walk.AxisMin = -180.0f;
	Speeds.Walk.AxisMax = 180.0f;
	Speeds.Walk.Cells[0] = 60.0f;
	Speeds.Walk.Cells[1] = AuthoredWalk;
	Speeds.Walk.Cells[2] = 60.0f;
	const FElysiumGaitReference Gait = ElysiumAnimIntent::GaitFrom(Speeds);
	TestTrue(TEXT("the walk/run split is the forward walk cell plus one unit"),
		FMath::IsNearlyEqual(Gait.RunSplitSpeed(), AuthoredWalk + ElysiumMove::U, 0.01f));
	TestTrue(TEXT("...which the authored walk itself cannot reach"),
		AuthoredWalk < Gait.RunSplitSpeed());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcTeleportToEntityTest,
	"Elysium.Substrate.Npc.TeleportToEntity", GElysiumTestFlags)
bool FElysiumNpcTeleportToEntityTest::RunTest(const FString&)
{
	ElysiumStub::ClearTally();
	ON_SCOPE_EXIT { ElysiumStub::ClearTally(); };

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__npc_teleport_to_entity__");
	FElysiumEntityDef Jack;
	Jack.Classname = TEXT("npc_VVampire");
	Jack.TargetName = TEXT("Jack");
	Jack.Origin = FVector(-10.0f, -20.0f, -30.0f);
	Jack.Keys.Add(TEXT("angles"), TEXT("1 2 3"));
	Jack.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	Defs.Defs.Add(MoveTemp(Jack));

	FElysiumEntityDef FirstDestination;
	FirstDestination.Classname = TEXT("point_target");
	FirstDestination.TargetName = TEXT("teleport_1");
	FirstDestination.Origin = FVector(100.0f, 200.0f, 300.0f);
	FirstDestination.Keys.Add(TEXT("angles"), TEXT("10 20 30"));
	Defs.Defs.Add(MoveTemp(FirstDestination));

	// FindEntityByName starts from null, so a duplicate name selects the earlier live entity.
	FElysiumEntityDef LaterDuplicate;
	LaterDuplicate.Classname = TEXT("point_target");
	LaterDuplicate.TargetName = TEXT("teleport_1");
	LaterDuplicate.Origin = FVector(900.0f, 900.0f, 900.0f);
	LaterDuplicate.Keys.Add(TEXT("angles"), TEXT("90 90 90"));
	Defs.Defs.Add(MoveTemp(LaterDuplicate));

	FElysiumRecordingServices Services;
	Services.bProvideNpcMotor = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);
	FElysiumEntity* JackEnt = World.FindByName(TEXT("Jack"));
	if (!TestNotNull(TEXT("Jack resolves"), JackEnt))
	{
		return false;
	}

	// Hold the ordinary admission think in the future. The queued input is delivered after this
	// frame's think pass and must leave the NPC due, not recursively think it in the same frame.
	JackEnt->NextThink = 10.0f;
	World.EnqueueInput(TEXT("!self"), FName(TEXT("TeleportToEntity")),
		FElysiumVariant::String(TEXT("teleport_1")), 0.0,
		FElysiumEntityHandle::Invalid(), JackEnt->Handle);
	World.Tick(1.0);

	TestTrue(TEXT("the first duplicate destination supplies the absolute origin"),
		JackEnt->Origin.Equals(FVector(100.0f, 200.0f, 300.0f)));
	TestTrue(TEXT("all three absolute angles are copied"),
		JackEnt->Angles.Equals(FVector(10.0f, 20.0f, 30.0f)));
	TestEqual(TEXT("queued delivery makes the NPC due after the completed think pass"),
		JackEnt->NextThink, 1.0f);

	FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
	if (TestNotNull(TEXT("Jack owns the embodiment motor"), Motor))
	{
		TestTrue(TEXT("the discontinuity reaches the NPC body"),
			Motor->Feet.Equals(FVector(100.0f, 200.0f, 300.0f)));
		TestEqual(TEXT("Source yaw crosses the existing handedness seam"), Motor->Yaw, -20.0f);
	}

	World.Tick(1.01);
	TestTrue(TEXT("the next server frame consumes the due AI think"), JackEnt->NextThink > 1.01f);

	// Retail consumes an invalid EHANDLE as a no-op. The project warning is expected, and neither
	// the transform nor the pre-existing think deadline is replaced.
	JackEnt->NextThink = 7.0f;
	const FVector BeforeOrigin = JackEnt->Origin;
	const FVector BeforeAngles = JackEnt->Angles;
	AddExpectedError(TEXT("TeleportToEntity destination 'missing_spot' resolved to no live entity"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.AcceptInput(JackEnt->Handle, FName(TEXT("TeleportToEntity")),
		FElysiumVariant::String(TEXT("missing_spot")),
		FElysiumEntityHandle::Invalid(), JackEnt->Handle);
	TestTrue(TEXT("a missing destination preserves origin"), JackEnt->Origin.Equals(BeforeOrigin));
	TestTrue(TEXT("a missing destination preserves angles"), JackEnt->Angles.Equals(BeforeAngles));
	TestEqual(TEXT("a missing destination preserves the existing think deadline"),
		JackEnt->NextThink, 7.0f);

	TArray<ElysiumStub::FTally> Rows;
	ElysiumStub::CollectTally(Rows);
	TestFalse(TEXT("the backed input never enters the stub work list"),
		Rows.ContainsByPredicate([](const ElysiumStub::FTally& Row)
		{
			return Row.Surface.Contains(TEXT("TeleportToEntity"));
		}));

	return true;
}

// =====================================================================================
// 9.3 scripted entity manipulation — the two-phase runtime create (CreateEntityNoSpawn ->
// CallEntitySpawn), Entity.SetName re-keying the name index, and the runtime origin backing
// SetOrigin. A bare world (Owner null) exercises the substrate half; bodies no-op.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRuntimeSpawnTest, "Elysium.Substrate.RuntimeSpawn", GElysiumTestFlags)
bool FElysiumRuntimeSpawnTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__spawn_test__");
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));   // empty map — everything here is runtime-created
	World.Activate(0.0);

	// --- Phase 1: CreateEntityNoSpawn appends a live, findable, NOT-yet-spawned entity ---
	FElysiumEntityDef D;
	D.Classname = TEXT("math_counter");
	D.TargetName = TEXT("c1");
	D.Origin = FVector(1, 2, 3);
	const FElysiumEntityHandle H = World.CreateRuntimeEntityNoSpawn(MoveTemp(D));
	FElysiumEntity* E = World.Resolve(H);
	if (!TestNotNull(TEXT("create returned a live entity"), E))
	{
		return false;
	}
	TestFalse(TEXT("not spawned yet"), E->bSpawnCalled);
	TestEqual(TEXT("findable by its targetname immediately"), World.FindByName(TEXT("c1")), E);
	TestTrue(TEXT("runtime origin seeded from the def"), E->Origin.Equals(FVector(1, 2, 3)));

	// --- SetName re-keys the name index: old name drops, new name resolves ---
	World.RenameEntity(*E, TEXT("c2"));
	TestNull(TEXT("old name no longer resolves"), World.FindByName(TEXT("c1")));
	TestEqual(TEXT("new name resolves to the same entity"), World.FindByName(TEXT("c2")), E);

	// --- SetOrigin mutates the live origin (what GetOrigin reads back) ---
	E->SetRuntimeOrigin(FVector(9, 9, 9));
	TestTrue(TEXT("SetOrigin moved the live origin"), E->Origin.Equals(FVector(9, 9, 9)));

	// --- Phase 2: CallEntitySpawn runs Spawn() once; a second call is an idempotent no-op ---
	World.CallEntitySpawn(*E);
	TestTrue(TEXT("spawned after CallEntitySpawn"), E->bSpawnCalled);
	World.CallEntitySpawn(*E);   // must not crash or re-spawn
	TestTrue(TEXT("still spawned (idempotent)"), E->bSpawnCalled);

	// --- The fused SpawnRuntimeEntity (npc_maker's path) spawns immediately ---
	FElysiumEntityDef D2;
	D2.Classname = TEXT("math_counter");
	D2.TargetName = TEXT("c3");
	const FElysiumEntityHandle H2 = World.SpawnRuntimeEntity(MoveTemp(D2));
	FElysiumEntity* E2 = World.Resolve(H2);
	if (TestNotNull(TEXT("fused spawn returned a live entity"), E2))
	{
		TestTrue(TEXT("fused path is spawned on return"), E2->bSpawnCalled);
	}

	return true;
}

} // namespace ElysiumNpcTests

#endif // WITH_DEV_AUTOMATION_TESTS
