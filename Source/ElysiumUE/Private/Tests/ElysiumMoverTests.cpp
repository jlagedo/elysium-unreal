#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBrushComponent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumWorldCollisionActor.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumLockable.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "Tests/ElysiumTestServices.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include <cmath>

static constexpr EAutomationTestFlags GElysiumMoverTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumDoorCaseTests
{
	// One engine world per case, torn down with the case. The wrapper is deliberately NOT a static
	// held across cases: `~FTestWorldWrapper` destroys the world and forces a collection, and a
	// static would run that at DLL unload, after the engine that owns GC and the world list has
	// already shut down. The world is also not the cost here -- the whole Substrate tier executes
	// in about three seconds -- so sharing one would trade a real lifetime hazard, and cross-case
	// actor accumulation in a world these cases run traces against, for nothing measurable.
	bool OpenEngineWorld(FTestWorldWrapper& Wrapper, FAutomationTestBase* Test)
	{
		if (Wrapper.CreateTestWorld(EWorldType::Game) && Wrapper.BeginPlayInTestWorld())
		{
			return true;
		}
		Wrapper.ForwardErrorMessages(Test);
		return false;
	}
	void AdvanceNativeFrames(FElysiumEntityWorld& World, FTestWorldWrapper& Wrapper, double Until, float* PreparedSeconds = nullptr,
		const TFunction<void(const TCHAR*)>& FrameObserver = {})
	{
		// These clocks are absolute. Admit real host frames and finish each actual Chaos solve
		// before the production226/229/EventQueue tail, as AElysiumMapActor does.
		while (World.NowSeconds() < Until)
		{
			const double Now = FMath::Min(World.NowSeconds() + 1. / 60., Until);
			World.BeginNativePhysicsFrame(Now, Now-World.NowSeconds());
			if (FrameObserver) FrameObserver(TEXT("after_begin"));
			const float Prepared = ElysiumModelPhysics::PreparePhysicsFrame(*Wrapper.GetTestWorld(), static_cast<float>(World.FrameSeconds()), 1.f, true);
			if (PreparedSeconds) *PreparedSeconds=Prepared;
			if (FrameObserver) FrameObserver(TEXT("after_prepare"));
			Wrapper.TickTestWorld(static_cast<float>(World.FrameSeconds()));
			if (FrameObserver) FrameObserver(TEXT("after_solve"));
			World.FinishNativePhysicsFrame();
			if (FrameObserver) FrameObserver(TEXT("after_feedback"));
		}
	}
}


// Activator-relative swing GATES — retail CRotDoor::DoorGoUp(bPropagateLinked, bResolveSwing)
// (FUN_100f3030) reaches OpenAwayFromEntity ONLY when the activator resolves AND !SF_DOOR_ONEWAY
// (0x10) AND bResolveSwing != 0. Reference: door-largeitems-spec.md T1.4 (CONFIRMED) and
// swing-centres-findings.md ITEM 1. Two gates the base swing omits:
//
//   1. bResolveSwing: CRotDoor::Blocked pre-issues its selected AngularMove before GoUp(1,0).
//      With a live activator, resolve=false preserves that move. The actual178 receiver fixture
//      below supplies the closing/endpoint history selecting the forward endpoint; a direct
//      DoorGoUp(activator,false) call alone does not prove block-reverse behavior.
//   2. SF_DOOR_ONEWAY: a ONEWAY door forces fixed-forward even with a resolved activator.
//
// The activator sits on -Y, which (per the OpensAwayFromActivator test above) is the side whose base
// activator-relative swing is BackRot (tip on +Y). The fixed-forward OpenRot is a normal +90 Source
// swing, tip on -Y. So "activator-relative" reads as TipY > +90 and "fixed-forward" as TipY < -90.


// Protected GoUp accessor for receiver contract fixtures; actual178 is exercised separately.
struct FElysiumDoorTestAccess
{
	static void DoorGoUp(FElysiumDoorBase& Door, const FElysiumEntityHandle& Activator, bool bResolveSwing)
	{
		Door.DoorGoUp(Activator, bResolveSwing);
	}
};

namespace ElysiumSwingGateTests
{
	// An asymmetric leaf from the hinge along local +X: the tip makes the swing direction observable,
	// and the door's two swing centres straddle the Y axis so an activator on -Y picks the BackRot side.
	FElysiumConvexHull DoorHull()
	{
		FElysiumConvexHull Hull;
		for (float X : { 0.0f, 100.0f })
		{
			for (float Y : { -5.0f, 5.0f })
			{
				for (float Z : { -100.0f, 100.0f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	}

	FElysiumEntityDef RotatingDoor(const TCHAR* Name, int32 Model, int32 SpawnFlags)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Origin = FVector::ZeroVector;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));   // stay open; close explicitly between cases
		Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
		return Door;
	}

	FElysiumEntityDef Activator(const TCHAR* Name, const FVector& Origin)
	{
		FElysiumEntityDef A;
		A.Classname = TEXT("math_counter");   // a bodiless point entity; its live Origin seeds from the def
		A.TargetName = Name;
		A.Origin = Origin;
		A.Keys.Add(TEXT("min"), TEXT("0"));
		A.Keys.Add(TEXT("max"), TEXT("1000"));
		return A;
	}

	AActor* SpawnMoverOwner(FAutomationTestBase* Test, UWorld* EngineWorld)
	{
		AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
		if (!Test->TestNotNull(TEXT("mover owner spawned"), Owner))
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->AddInstanceComponent(Root);
		return Owner;
	}
}


// Locked-door input matrix (retail CBaseDoor, vampire.dll).
//
// Reference: research/event-surface/doors-receivers-findings.md (D010 note, D020, D031, D032,
// D033) + door-divergence-ledger.md rows DL07/DL09/DL12. The retail truth these assert:
//
//   * CBaseDoor has NO OnLockedUse output — that output belongs to prop_switch. A door fires it
//     never, on any path (D010 note).
//   * The direct `Open` input on a locked door refuses SILENTLY: no sound, no output, no motion
//     (D031, FUN_100f0170 opens iff `!IsDoorLocked`).
//   * The direct `Toggle` input on a locked door does nothing: no sound, no output, no motion
//     (D033, FUN_100f0210 gates on `IsDoorLocked` at the top).
//   * The `+use` path on a locked door plays ONLY the `locked` sound — no output, no motion
//     (D020 step-8 locked branch). It never reaches DoorActivate, so a locked *open* door is not
//     closed by +use.
//   * `Close` (FUN_100f00a0) has NO lock check: a direct `Close` input closes a locked door.
//
// The mover sound MANIFEST is a Content-tier disk dependency, so at the content-free Substrate
// tier `SoundSubs` is empty and no `locked` WAV is submitted regardless of path — the sound
// EMISSION POINT is not observable here. These tests therefore assert the output-and-motion
// matrix (the phantom-output removal and the no-motion rule), which is the observable core of the
// slice, plus that the input paths that must be silent submit no audio.


namespace ElysiumLockedDoorTests
{
	FElysiumConvexHull DoorHull()
	{
		// An asymmetric leaf from the hinge along local +X — a real body so motion is observable.
		FElysiumConvexHull Hull;
		for (float X : { 0.0f, 100.0f })
		{
			for (float Y : { -5.0f, 5.0f })
			{
				for (float Z : { -100.0f, 100.0f })
				{
					Hull.Vertices.Emplace(X, Y, Z);
				}
			}
		}
		return Hull;
	}

	// One authored output row targeting `sink_counter` so FireOutput records a `fire` line for it.
	void Wire(FElysiumEntityDef& Def, const TCHAR* Output)
	{
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = TEXT("sink_counter");
		Row.Input = TEXT("Add");
		Row.Param = TEXT("1");
		Row.Delay = 0.f;
		Row.Times = -1;
		Def.Outputs.Add(MoveTemp(Row));
	}

	// A func_door_rotating carrying rows for every door output whose absence these tests assert, so a
	// stray fire on any of them is recorded by the sink. `SpawnFlags` sets LOCKED / START_OPEN.
	FElysiumEntityDef LockedDoor(const TCHAR* Name, int32 Model, int32 SpawnFlags)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));   // stay open once opened (no autoclose to race)
		Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
		Wire(Door, TEXT("OnLockedUse"));   // the phantom output — must NEVER fire from a door
		Wire(Door, TEXT("OnOpen"));
		Wire(Door, TEXT("OnClose"));
		Wire(Door, TEXT("OnFullyOpen"));
		Wire(Door, TEXT("OnFullyClosed"));
		return Door;
	}

	FElysiumEntityDef Counter(const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("min"), TEXT("0"));
		Def.Keys.Add(TEXT("max"), TEXT("1000"));
		return Def;
	}

	constexpr int32 SF_DOOR_START_OPEN = 0x1;
	constexpr int32 SF_DOOR_LOCKED     = 0x800;
}


// ResolveToggleStateFromTransform — the unconditional +use state-resync (retail CBaseDoor::Use
// step 5, vt +0x3e0; CRotDoor FUN_100f2520 / CBaseDoor FUN_100eff90).
//
// Reference: research/event-surface/door-largeitems-spec.md TARGET 2 step T2.5 (grade CONFIRMED,
// 0.001 tolerance PE-verified). On every +use, before the admission/locked decision, the door
// recomputes its toggle state from the LIVE body transform: within 0.001 per-component of the
// closed endpoint -> AtBottom; of the open endpoint -> AtTop; otherwise the mid-motion state is
// left as-is. This re-stamps a door whose body never physically moved back to its true endpoint,
// which is what lets the following toggle re-open it (the stale-AtTop tutorial-door bug).


namespace ElysiumDoorResyncTests
{
	using ElysiumLockedDoorTests::DoorHull;

	// A plain unlocked func_door_rotating with a real body, no wired outputs (the mapping test reads
	// only State()). distance 90 / speed 90 / wait -1 so one Tick opens it and it stays open.
	FElysiumEntityDef RotatingDoor(const TCHAR* Name, int32 Model)
	{
		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = Name;
		Door.Model = Model;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));
		Door.Keys.Add(TEXT("spawnflags"), TEXT("0"));
		return Door;
	}

	AActor* SpawnMoverOwner(FAutomationTestBase* Test, UWorld* EngineWorld)
	{
		AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
		if (!Test->TestNotNull(TEXT("mover owner spawned"), Owner))
		{
			return nullptr;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->AddInstanceComponent(Root);
		return Owner;
	}
}

// The nine door-mover cases below each built and tore down their own UWorld as separate
// IMPLEMENT_SIMPLE_AUTOMATION_TEST entries. They are grouped into one IMPLEMENT_COMPLEX_AUTOMATION_TEST
// so a failure in one case is reported and isolated by name, and each case still owns its own engine
// world, FElysiumEntityWorld, services and defs outright.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FElysiumDoorCasesTest, "Elysium.Substrate.Mover", GElysiumMoverTestFlags)

void FElysiumDoorCasesTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	static const TCHAR* Cases[] =
	{
		TEXT("RotatingDoorHandedness"),
		TEXT("RotatingDoorOpensAwayFromActivator"),
		TEXT("RotatingDoorBlockReverseFixedForward"),
		TEXT("RotatingDoorOneWayIgnoresActivator"),
		TEXT("LockedDoorRefusesSilently"),
		TEXT("LockedOpenDoorCloseMatrix"),
		TEXT("DoorResyncMapping"),
		TEXT("DoorStaleAtTopReopens"),
		TEXT("DoorUseGuardChain"),
		TEXT("NativeContactLifetimeEvidence"),
		TEXT("NativeContactMirrorEvidence"),
		TEXT("NativeContactSwizzleEvidence"),
		TEXT("NativeDoorFeedbackObserver"),
		TEXT("NativeWorldContentsFilter"),
		TEXT("NativeArrivalClockObservers"),
	};
	for (const TCHAR* Case : Cases)
	{
		OutBeautifiedNames.Add(Case);
		OutTestCommands.Add(Case);
	}
}

bool FElysiumDoorCasesTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!ElysiumDoorCaseTests::OpenEngineWorld(TestWorld, this))
	{
		return false;
	}
	UWorld* EngineWorld = TestWorld.GetTestWorld();
	if (Parameters == TEXT("NativeArrivalClockObservers"))
	{
		AActor* Owner = ElysiumDoorResyncTests::SpawnMoverOwner(this, EngineWorld); if (!Owner) return false;
		FElysiumEntityDefs Defs; Defs.MapName = TEXT("__native_arrival_clock__");
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FElysiumEntityDef Def; Def.Classname = TEXT("func_door"); Def.TargetName = Index == 0 ? TEXT("arrival") : Index == 1 ? TEXT("think_clock") : TEXT("band");
			Def.Model = Index + 1; Def.Origin = FVector(0, Index * 1000., 1000.);
			Def.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Def.Model)); Def.Keys.Add(TEXT("speed"), TEXT("20")); Def.Keys.Add(TEXT("lip"), TEXT("0")); Def.Keys.Add(TEXT("wait"), TEXT("-1"));
			Def.Keys.Add(TEXT("spawnflags"), Index == 1 ? TEXT("1") : TEXT("0"));
			FElysiumConvexHull Hull;
			for (float X : {-10.f,10.f}) for (float Y : {-10.f,10.f}) for (float Z : {-10.f,10.f}) Hull.Vertices.Emplace(X,Y,Z);
			Def.Hulls.Add(Hull);
			for (const TCHAR* Output : { TEXT("OnLinearMoveDone"), TEXT("OnFullyOpen"), TEXT("OnClose") }) ElysiumLockedDoorTests::Wire(Def,Output);
			Def.Outputs[0].Target = TEXT("helper_counter");
			Defs.Defs.Add(MoveTemp(Def));
		}
		Defs.Defs.Add(ElysiumLockedDoorTests::Counter(TEXT("sink_counter")));
		Defs.Defs.Add(ElysiumLockedDoorTests::Counter(TEXT("helper_counter")));
		FElysiumRecordingServices Services; FElysiumEntityWorld World(Owner,nullptr,Services.Bundle()); World.Load(MoveTemp(Defs)); World.Activate(0.);
		auto OrderedSink = MakeUnique<FElysiumOrderedIOSink>();
		FElysiumOrderedIOSink* const IO = OrderedSink.Get(); World.AddSink(MoveTemp(OrderedSink));
		auto* Arrival = World.FindByName(TEXT("arrival"))->AsDoorBase(); auto* Clock = World.FindByName(TEXT("think_clock"))->AsDoorBase(); auto* Band = World.FindByName(TEXT("band"))->AsDoorBase();
		if (!TestNotNull(TEXT("arrival is an actual native authored door"),Arrival->VPhysicsGetObject()) || !TestNotNull(TEXT("think uses an actual native START_OPEN producer"),Clock->VPhysicsGetObject())) return false;
		int32 Entries = 0, Helpers = 0, Arrivals = 0, ThinkOutputs = 0;
		World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
		{
			if (Event.Entity == Arrival->Handle)
			{
				if (Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=toggle_movedone")))
				{ ++Entries; TestEqual(TEXT("native226 clears deadline before actual133 entry"),Arrival->PhysicsMoveDoneTime,0.f); TestEqual(TEXT("authored think survives native133 entry"),Arrival->NextThink,119.f); }
				if (Event.Kind == TEXT("output") && Event.Text.StartsWith(TEXT("OnLinearMoveDone")))
				{
					++Helpers; TestEqual(TEXT("snap helper stores deadline-1 before output"),Arrival->PhysicsMoveDoneTime,-1.f); TestEqual(TEXT("snap helper preserves authored NextThink"),Arrival->NextThink,119.f);
					const FVector SnappedWords(static_cast<float>(14.92/2.54)*2.54,0,static_cast<float>(1000./2.54)*2.54);
					TestTrue(TEXT("helper output sees snapped Source-float local endpoint"),Arrival->GetOrigin().Equals(SnappedWords,.00001)); TestEqual(TEXT("helper output sees stopped velocity"),Arrival->Velocity,FVector::ZeroVector);
				}
				if (Event.Kind == TEXT("output") && Event.Text.StartsWith(TEXT("OnFullyOpen")))
				{ ++Arrivals; TestEqual(TEXT("installed arrival observes helper deadline-1"),Arrival->PhysicsMoveDoneTime,-1.f); TestTrue(TEXT("installed receiver reads cleared movement type"),Arrival->MoveKind()==FElysiumMoverBase::EMoveKind::None); }
			}
			if (Event.Entity == Clock->Handle && Event.Kind == TEXT("output") && Event.Text.StartsWith(TEXT("OnClose")))
			{ ++ThinkOutputs; TestEqual(TEXT("actual autoclose I/O uses future admitted think clock"),World.NowSeconds(),static_cast<double>(.02f)); }
		});
		Arrival->InputOpen(FElysiumEntityHandle::Invalid()); Arrival->NextThink = 119.;
		Clock->NextThink = .02f;
		ElysiumDoorCaseTests::AdvanceNativeFrames(World,TestWorld,1./60.);
		TestEqual(TEXT("scoped callback restores actual frame clock after return"),World.NowSeconds(),1./60.);
		TestEqual(TEXT("actual scheduled door think fires once"),ThinkOutputs,1);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World,TestWorld,.5);
		TestEqual(TEXT("actual native arrival enters133 once"),Entries,1); TestEqual(TEXT("actual helper output fires once"),Helpers,1); TestEqual(TEXT("actual installed arrival output fires once"),Arrivals,1);
		TestTrue(TEXT("actual helper queue has null activator and caller=door"),IO->FirstLine(TEXT("queue"),TEXT("helper_counter.Add")).Contains(FString::Printf(TEXT("act=%s cal=%s"),*FElysiumEntityHandle::Invalid().ToString(),*Arrival->Handle.ToString())));
		ElysiumDoorCaseTests::AdvanceNativeFrames(World,TestWorld,.55);
		TestEqual(TEXT("native reentry does not repeat completion"),Helpers,1);
		World.SetAiTraceSink({});
		const float Tol = .001f;
		for (float Word : {std::nextafter(Tol,0.f),Tol,std::nextafter(Tol,1.f)})
		{
			Band->InputOpen(FElysiumEntityHandle::Invalid());
			Band->SetOrigin(FVector(static_cast<double>(Word)*2.54,2000,1000)); Band->ResolveToggleStateFromTransform();
			TestEqual(TEXT("rounded Source-inch endpoint band below/equal/above"),Band->State(),Word<=Tol ? FElysiumDoorBase::EToggleState::AtBottom : FElysiumDoorBase::EToggleState::GoingUp);
		}
		return true;
	}
	if (Parameters == TEXT("NativeWorldContentsFilter"))
	{
		AActor* Owner = ElysiumDoorResyncTests::SpawnMoverOwner(this, EngineWorld);
		if (!Owner) return false;
		UElysiumMapCollisionPayload* Payload = NewObject<UElysiumMapCollisionPayload>();
		Payload->MapName = TEXT("__native_world_contents__");
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FElysiumCollisionHull Hull;
			for (float X : { -20.f, 20.f }) for (float Y : { -20.f, 20.f }) for (float Z : { -20.f, 20.f })
				Hull.Vertices.Emplace(X + Index * 200.f, Y, Z + 1000.f);
			Payload->AuthorWorldBody(3, { Hull }, Index == 0);
		}
		if (!TestTrue(TEXT("world input cooks actual common and dual-clip convexes"), Payload->CreatePhysicsMeshes())) return false;
		auto* Collider = EngineWorld->SpawnActor<AElysiumWorldCollisionActor>();
		if (!Collider || !TestEqual(TEXT("actual world producer installs both partitions"), Collider->AuthorFromPayload(Payload), 2)) return false;
		FElysiumEntityDefs Defs; Defs.MapName = Payload->MapName;
		FElysiumEntityDef WorldDef; WorldDef.Classname = TEXT("worldspawn"); WorldDef.TargetName = TEXT("world_owner"); Defs.Defs.Add(MoveTemp(WorldDef));
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FElysiumEntityDef Def; Def.Classname = TEXT("func_door"); Def.TargetName = Index == 0 ? TEXT("common_door") : TEXT("clip_door");
			Def.Origin = FVector(Index * 200.f - 35.f, 0, 1000); Def.Model = Index + 1;
			Def.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Def.Model));
			Def.Keys.Add(TEXT("speed"), TEXT("20")); Def.Keys.Add(TEXT("lip"), TEXT("0")); Def.Keys.Add(TEXT("wait"), TEXT("-1"));
			FElysiumConvexHull Hull;
			for (float X : { -10.f, 10.f }) for (float Y : { -10.f, 10.f }) for (float Z : { -10.f, 10.f }) Hull.Vertices.Emplace(X, Y, Z);
			Def.Hulls.Add(Hull); Defs.Defs.Add(MoveTemp(Def));
		}
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle()); World.Load(MoveTemp(Defs)); World.Activate(0.);
		auto* WorldOwner = World.FindByName(TEXT("world_owner"));
		auto* Common = World.FindByName(TEXT("common_door"))->AsDoorBase();
		auto* Clip = World.FindByName(TEXT("clip_door"))->AsDoorBase();
		if (!TestNotNull(TEXT("common authored door creates native shadow"), Common ? Common->VPhysicsGetObject() : nullptr)
			|| !TestNotNull(TEXT("clip authored door creates native shadow"), Clip ? Clip->VPhysicsGetObject() : nullptr)) return false;
		for (UElysiumWorldCollisionComponent* Component : Collider->Bodies)
			TestTrue(TEXT("world native creation GameData is actual worldspawn"), ElysiumModelPhysics::GameDataForObject(ElysiumModelPhysics::ActorIdentity(Component)) == WorldOwner);
		TestEqual(TEXT("profiles remain identical across native pair filtering"), Collider->Bodies[0]->GetCollisionProfileName(), Collider->Bodies[1]->GetCollisionProfileName());
		int32 CommonWorldBlocked = 0, ClipBlocked = 0;
		World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
		{
			if (Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=pusher_blocked")))
			{
				AddInfo(Event.Text);
				if (Event.Entity == Common->Handle && Event.Text.Contains(TEXT("other=world_owner"))) ++CommonWorldBlocked;
				if (Event.Entity == Clip->Handle) ++ClipBlocked;
			}
		});
		Common->InputOpen(FElysiumEntityHandle::Invalid()); Clip->InputOpen(FElysiumEntityHandle::Invalid());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .8);
		TestTrue(TEXT("common native world contact reaches226 and actual178 with worldspawn"), CommonWorldBlocked > 0);
		TestEqual(TEXT("rejected clip-only pair never becomes native44 Blocked partner"), ClipBlocked, 0);
		TestEqual(TEXT("clip-only input permits native door arrival"), Clip->State(), FElysiumDoorBase::EToggleState::AtTop);
		void* ClipPartner = nullptr;
		TestFalse(TEXT("disabled cached clip constraint cannot manufacture native44"), ElysiumModelPhysics::GetContactPoint(Clip->VPhysicsGetObject(), ClipPartner));
		// Slot237's settled live Flags producer changes admission at the next real solve.
		Clip->Flags |= 0x2000;
		ElysiumModelPhysics::Wake(Clip->Body);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .86);
		TestTrue(TEXT("live NPC mask admits the actual dual-clip native contact"), ElysiumModelPhysics::GetContactPoint(Clip->VPhysicsGetObject(), ClipPartner));
		TestTrue(TEXT("newly admitted clip contact retains world creation owner"), ElysiumModelPhysics::GameDataForObject(ClipPartner) == WorldOwner);
		Clip->Flags &= ~0x2000;
		ElysiumModelPhysics::Wake(Clip->Body);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .92);
		TestFalse(TEXT("live base mask retires formerly admitted native contact"), ElysiumModelPhysics::GetContactPoint(Clip->VPhysicsGetObject(), ClipPartner));
		void* CommonPartner = nullptr;
		TestTrue(TEXT("common contact survives the real completed frame"), ElysiumModelPhysics::GetContactPoint(Common->VPhysicsGetObject(), CommonPartner));
		ElysiumModelPhysics::PreparePhysicsFrame(*EngineWorld, 0.f, 1.f, true);
		void* PausedPartner = nullptr;
		TestTrue(TEXT("no-advance frame retains surviving completed contact"), ElysiumModelPhysics::GetContactPoint(Common->VPhysicsGetObject(), PausedPartner) && PausedPartner == CommonPartner);
		UElysiumWorldCollisionComponent* CommonComponent = Collider->Bodies[0];
		void* OldWorldActor = ElysiumModelPhysics::ActorIdentity(CommonComponent);
		CommonComponent->DestroyPhysicsState();
		TestNull(TEXT("native retirement clears old world owner mapping immediately"), ElysiumModelPhysics::GameDataForObject(OldWorldActor));
		TestNull(TEXT("temporarily missing native world actor cannot alias null GameData query"), ElysiumModelPhysics::GameDataForObject(nullptr));
		TestFalse(TEXT("retired native world contact is removed before another solve"), ElysiumModelPhysics::GetContactPoint(Common->VPhysicsGetObject(), CommonPartner));
		ElysiumModelPhysics::PreparePhysicsFrame(*EngineWorld, 0.f, 1.f, true);
		TestNull(TEXT("actual absent-body frame retains no stale proxy owner mapping"), ElysiumModelPhysics::GameDataForObject(OldWorldActor));
		CommonComponent->CreatePhysicsState();
		TestTrue(TEXT("actual native recreation retains component creation owner"), ElysiumModelPhysics::GameDataForObject(ElysiumModelPhysics::ActorIdentity(CommonComponent)) == WorldOwner);
		ElysiumModelPhysics::Wake(Common->Body);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .98);
		TestTrue(TEXT("recreated real world body supplies a fresh qualifying contact"), ElysiumModelPhysics::GetContactPoint(Common->VPhysicsGetObject(), CommonPartner)
			&& ElysiumModelPhysics::GameDataForObject(CommonPartner) == WorldOwner);
		World.SetAiTraceSink({});
		const FElysiumEntityHandle OutgoingOwner = WorldOwner->Handle;
		World.Teardown(); // production rebuild retires the outgoing world before replacing its definitions
		FElysiumEntityDefs Rebuild; Rebuild.MapName = TEXT("__new_world_epoch__"); World.Load(MoveTemp(Rebuild));
		for (UElysiumWorldCollisionComponent* Component : Collider->Bodies)
			TestNull(TEXT("old creation handle is cleared before rebuilt world callbacks"), ElysiumModelPhysics::GameDataForObject(ElysiumModelPhysics::ActorIdentity(Component)));
		World.Teardown();
		FElysiumEntityDefs NewEpoch; NewEpoch.MapName = Payload->MapName;
		FElysiumEntityDef NewWorldDef; NewWorldDef.Classname = TEXT("worldspawn"); NewWorldDef.TargetName = TEXT("new_world_owner"); NewEpoch.Defs.Add(MoveTemp(NewWorldDef));
		FElysiumEntityDef NewDoorDef; NewDoorDef.Classname = TEXT("func_door"); NewDoorDef.TargetName = TEXT("new_common_door"); NewDoorDef.Model = 1; NewDoorDef.Origin = FVector(-35,0,1000);
		NewDoorDef.Keys.Add(TEXT("model"), TEXT("*1")); NewDoorDef.Keys.Add(TEXT("speed"), TEXT("20")); NewDoorDef.Keys.Add(TEXT("lip"), TEXT("0")); NewDoorDef.Keys.Add(TEXT("wait"), TEXT("-1"));
		FElysiumConvexHull NewHull;
		for (float X : { -10.f, 10.f }) for (float Y : { -10.f, 10.f }) for (float Z : { -10.f, 10.f }) NewHull.Vertices.Emplace(X,Y,Z);
		NewDoorDef.Hulls.Add(NewHull); NewEpoch.Defs.Add(MoveTemp(NewDoorDef));
		World.Load(MoveTemp(NewEpoch)); World.Activate(0.);
		FElysiumEntity* NewWorldOwner = World.FindByName(TEXT("new_world_owner"));
		auto* NewCommon = World.FindByName(TEXT("new_common_door"))->AsDoorBase();
		TestTrue(TEXT("new world receiver has a different semantic epoch"), NewWorldOwner && NewWorldOwner->Handle != OutgoingOwner);
		TestTrue(TEXT("persistent native world geometry gains only the new creation handle"), ElysiumModelPhysics::GameDataForObject(ElysiumModelPhysics::ActorIdentity(CommonComponent)) == NewWorldOwner);
		int32 NewWorldBlocked = 0;
		World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
		{
			if (Event.Entity == NewCommon->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=pusher_blocked"))
				&& Event.Text.Contains(TEXT("other=new_world_owner"))) ++NewWorldBlocked;
		});
		NewCommon->InputOpen(FElysiumEntityHandle::Invalid());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .3);
		TestTrue(TEXT("fresh actual native contact dispatches178 with the rebuilt world receiver"), NewWorldBlocked > 0);
		World.SetAiTraceSink({});
		// CBaseButton's actual mover Spawn/native producer inherits generic178. Its shipped
		// DONTMOVE flag avoids inventing button motion; native contact supplies feedback input.
		NewCommon->Kill();
		FElysiumEntityDef ButtonDef; ButtonDef.Classname=TEXT("func_button"); ButtonDef.TargetName=TEXT("generic_blocked_button");
		ButtonDef.Model=2; ButtonDef.Origin=FVector(-35,0,1000); ButtonDef.Keys.Add(TEXT("model"),TEXT("*2"));
		ButtonDef.Keys.Add(TEXT("spawnflags"),TEXT("1")); ButtonDef.Hulls.Add(NewHull);
		FElysiumEntity* Button=World.Resolve(World.SpawnRuntimeEntity(MoveTemp(ButtonDef)));
		if (!TestNotNull(TEXT("actual button Spawn creates native brush"),Button?Button->VPhysicsGetObject():nullptr)) return false;
		TestEqual(TEXT("button Spawn supplies actual move8"),Button->GetMoveType(),8);
		for (const TCHAR* Name : {TEXT("blocked_old_parent"),TEXT("blocked_new_parent")})
		{ FElysiumEntityDef Point; Point.Classname=TEXT("info_target"); Point.TargetName=Name; World.SpawnRuntimeEntity(MoveTemp(Point)); }
		auto* OldParent=World.FindByName(TEXT("blocked_old_parent")); auto* NewParent=World.FindByName(TEXT("blocked_new_parent"));
		FVector ButtonContactTarget(-28,0,1000); Button->SetAbsOrigin(ButtonContactTarget); Button->UpdatePhysicsShadowToCurrentPosition(0.f);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World,TestWorld,.36);
		void* ButtonPartner=nullptr;
		if (!TestTrue(TEXT("button's real native44 contact is creation-owned worldspawn"),ElysiumModelPhysics::GetContactPoint(Button->VPhysicsGetObject(),ButtonPartner)
			&& ElysiumModelPhysics::GameDataForObject(ButtonPartner)==NewWorldOwner)) return false;
		Button->SetParent(OldParent,0);
		auto RecordBlocked=[](FElysiumEntity& Receiver,FElysiumEntity* Other)
		{ Receiver.World->EmitRetailSite(Receiver,TEXT("blocked_parent_contract"),TEXT("CBaseEntity::Blocked"),0x100a4d40u,TEXT("callback"),Other?Other->TargetName:TEXT("null")); };
		OldParent->BlockedCallback=RecordBlocked; NewParent->BlockedCallback=RecordBlocked;
		Button->BlockedCallback=[](FElysiumEntity& Receiver,FElysiumEntity* Other)
		{
			Receiver.World->EmitRetailSite(Receiver,TEXT("blocked_parent_contract"),TEXT("CBaseEntity::Blocked"),0x100a4d40u,TEXT("callback"),Other?Other->TargetName:TEXT("null"));
			Receiver.SetParent(Receiver.World->FindByName(TEXT("blocked_new_parent")),0); Receiver.UnlinkFromMoveParent();
			Receiver.LocalTime=99.f;
		};
		TArray<FString> BlockedOrder;
		World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
		{
			if (Event.Kind==TEXT("retail_site") && Event.Text.Contains(TEXT("tag=blocked_parent_contract")))
			{ BlockedOrder.Add(Event.Name); TestTrue(TEXT("178 retains original creation-owned world partner"),Event.Text.Contains(TEXT("new_world_owner"))); }
		});
		FVector ButtonSolved; ElysiumModelPhysics::GetShadowPosition(Button->VPhysicsGetObject(),&ButtonSolved,nullptr);
		FVector ButtonDisplaced=ButtonSolved+FVector(40,0,0); Button->SetAbsOrigin(ButtonDisplaced); Button->Velocity=FVector(20,0,0);
		Button->LocalTime=1.f; Button->StartFrameLocalTime=0.f; Button->PhysicsMoveDoneTime=0.f;
		Button->VPhysicsUpdatePusher(Button->VPhysicsGetObject()); World.SetAiTraceSink({});
		TestEqual(TEXT("actual226 reaches generic178 then live authored parent"),BlockedOrder.Num(),2);
		if (BlockedOrder.Num()==2) { TestEqual(TEXT("installed178 first"),BlockedOrder[0],FString(TEXT("generic_blocked_button"))); TestEqual(TEXT("live+254 parent follows, not old parent or cleared+25c"),BlockedOrder[1],FString(TEXT("blocked_new_parent"))); }
		TestTrue(TEXT("actual178 callback distinguishes authored and move parents"),Button->ParentHandle==NewParent->Handle && !Button->MoveParent.IsSet());
		TestEqual(TEXT("captured correction overwrites callback clock write"),Button->LocalTime,0.f);
		Button->SetParent(nullptr,0); Button->SetAbsOrigin(ButtonDisplaced); Button->LocalTime=1.f; Button->StartFrameLocalTime=0.f;
		const FElysiumEntityHandle ButtonHandle=Button->Handle;
		Button->BlockedCallback=[](FElysiumEntity& Receiver,FElysiumEntity*) { Receiver.Kill(); };
		Button->VPhysicsUpdatePusher(Button->VPhysicsGetObject());
		TestNull(TEXT("actual native178 retirement stops postcallback receiver access"),World.Resolve(ButtonHandle));
		return true;
	}
	if (Parameters == TEXT("NativeDoorFeedbackObserver"))
	{
		AActor* Owner = ElysiumDoorResyncTests::SpawnMoverOwner(this, EngineWorld);
		if (!Owner) return false;
		// Actual static world geometry pins the authored blocker; two unanchored shadow bodies
		// can move together and do not produce the required one-inch feedback disagreement.
		AActor* WallActor = EngineWorld->SpawnActor<AActor>();
		UBoxComponent* Wall = NewObject<UBoxComponent>(WallActor, TEXT("PinnedWorldWall"));
		WallActor->SetRootComponent(Wall); WallActor->AddInstanceComponent(Wall);
		Wall->SetMobility(EComponentMobility::Static); Wall->SetBoxExtent(FVector(10.,100.,100.));
		Wall->SetWorldLocation(FVector(-36.,0.,1000.)); Wall->SetCollisionProfileName(TEXT("BlockAll")); Wall->RegisterComponent();
		FElysiumEntityDefs Defs; Defs.MapName = TEXT("__native_door_feedback__");
		for (int32 Model = 1; Model <= 3; ++Model)
		{
			FElysiumEntityDef Def; Def.Classname = TEXT("func_door"); Def.TargetName = Model == 2 ? TEXT("blocker") : TEXT("door_group");
			Def.Origin = FVector(Model == 1 ? 0. : Model == 2 ? -16. : 1000., 0., 1000.); Def.Model = Model;
			Def.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
			Def.Keys.Add(TEXT("speed"), TEXT("10")); Def.Keys.Add(TEXT("wait"), TEXT("0.5"));
			Def.Keys.Add(TEXT("spawnflags"), Model == 2 ? TEXT("0") : TEXT("1"));
			FElysiumConvexHull Hull;
			for (float X : { -10.f, 10.f }) for (float Y : { -10.f, 10.f }) for (float Z : { -10.f, 10.f }) Hull.Vertices.Emplace(X,Y,Z);
			Def.Hulls.Add(Hull);
			if (Model == 1)
				for (const TCHAR* Output : { TEXT("OnOpen"), TEXT("OnClose"), TEXT("OnFullyClosed"), TEXT("OnFullyOpen") }) ElysiumLockedDoorTests::Wire(Def, Output);
			if (Model == 3) ElysiumLockedDoorTests::Wire(Def, TEXT("OnClose"));
			Defs.Defs.Add(MoveTemp(Def));
		}
		Defs.Defs.Add(ElysiumLockedDoorTests::Counter(TEXT("sink_counter")));
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle()); World.Load(MoveTemp(Defs));
		auto OwnedSink = MakeUnique<FElysiumOrderedIOSink>(); auto* Sink = OwnedSink.Get(); World.AddSink(MoveTemp(OwnedSink));
		TArray<FElysiumAiTraceEvent> Events;
		World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event) { Events.Add(Event); });
		World.Activate(0.);
		FElysiumEntity* Subject = World.FindByName(TEXT("door_group")); FElysiumEntity* Blocker = World.FindByName(TEXT("blocker"));
		FElysiumDoorBase* Peer = nullptr;
		World.ForEachNamed(TEXT("door_group"), [&](FElysiumEntity& Entity) { if (&Entity != Subject) Peer = Entity.AsDoorBase(); });
		FElysiumDoorBase* Door = Subject ? Subject->AsDoorBase() : nullptr;
		if (!TestNotNull(TEXT("authored START_OPEN sliding door resolves"), Door) || !TestNotNull(TEXT("linked authored door resolves"), Peer) || !TestNotNull(TEXT("actual blocker creates native brush"), Blocker ? Blocker->VPhysicsGetObject() : nullptr)) return false;
		TestEqual(TEXT("sliding START_OPEN produces literal state0"), Door->State(), FElysiumDoorBase::EToggleState::AtTop);
		TestEqual(TEXT("linked peer begins open before native Blocked"), Peer->State(), FElysiumDoorBase::EToggleState::AtTop);
		Sink->Reset(); Events.Reset(); Door->InputClose(FElysiumEntityHandle::Invalid());
		TestEqual(TEXT("actual Close producer emits input and movement outputs"), Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 2);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .6);
		bool bSelectedBlocker = false;
		for (const auto& Event : Events)
			if (Event.Entity == Door->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=pusher_blocked")))
			{
				AddInfo(Event.Text);
				bSelectedBlocker |= Event.Text.Contains(TEXT("other=blocker"));
			}
		TestTrue(TEXT("actual native44 selects the live blocker before actual Door178"), bSelectedBlocker);
		TestEqual(TEXT("ordinary sliding receiver has no rotating-door reopen latch"), Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
		TestEqual(TEXT("actual Door178 starts the named peer close once"), Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 3);
		TestEqual(TEXT("native blocked close does not produce a closed arrival"), Sink->CountOf(TEXT("fire"), TEXT("OnFullyClosed")), 0);
		TestEqual(TEXT("blocked sliding door retains its closing state"), Door->State(), FElysiumDoorBase::EToggleState::GoingDown);
		TestTrue(TEXT("actual native Blocked receiver advances the linked peer"), Peer->State() == FElysiumDoorBase::EToggleState::GoingDown || Peer->State() == FElysiumDoorBase::EToggleState::AtBottom);
		AddInfo(FString::Printf(TEXT("native door local=%.9g start=%.9g deadline=%.9g pose=%s outputs=%s"), Door->LocalTime, Door->StartFrameLocalTime, Door->PhysicsMoveDoneTime, *Door->GetOrigin().ToString(), *Sink->Log()));
		return true;
	}
	if (Parameters == TEXT("NativeContactLifetimeEvidence") || Parameters == TEXT("NativeContactMirrorEvidence") || Parameters == TEXT("NativeContactSwizzleEvidence"))
	{
		auto TransformTarget=[&](FVector Position)
		{
			if (Parameters==TEXT("NativeContactMirrorEvidence")) Position.X=-Position.X;
			if (Parameters==TEXT("NativeContactSwizzleEvidence")) { const double OldX=Position.X; Position.X=Position.Y; Position.Y=OldX; }
			return Position;
		};
		AActor* Owner = ElysiumDoorResyncTests::SpawnMoverOwner(this, EngineWorld);
		if (!Owner) return false;
		FElysiumEntityDefs Defs; Defs.MapName = TEXT("__native_contact_lifetime__");
		auto AddBody = [&Defs,&TransformTarget](const TCHAR* Name, int32 Model, const FVector& Origin)
		{
			FElysiumEntityDef Def; Def.Classname = TEXT("func_door"); Def.TargetName = Name;
			Def.Origin = TransformTarget(Origin); Def.Model = Model; Def.Contents = 1;
			Def.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
			Def.Keys.Add(TEXT("speed"), TEXT("10")); Def.Keys.Add(TEXT("wait"), Model == 1 ? TEXT("0.5") : TEXT("-1"));
			if (Model == 1) ElysiumLockedDoorTests::Wire(Def, TEXT("OnOpen"));
			FElysiumConvexHull Hull;
			for (float X : { -10.f, 10.f }) for (float Y : { -10.f, 10.f }) for (float Z : { -10.f, 10.f }) Hull.Vertices.Emplace(X,Y,Z);
			Def.Hulls.Add(Hull); Defs.Defs.Add(MoveTemp(Def));
		};
		AddBody(TEXT("subject"), 1, FVector(0,0,1000));
		AddBody(TEXT("first"), 2, FVector(19,0,1000));
		AddBody(TEXT("second"), 3, FVector(-100,0,1000));
		Defs.Defs.Add(ElysiumLockedDoorTests::Counter(TEXT("sink_counter")));
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs)); World.Activate(0.);
		auto OwnedContactSink = MakeUnique<FElysiumOrderedIOSink>(); auto* ContactSink = OwnedContactSink.Get(); World.AddSink(MoveTemp(OwnedContactSink));
		FElysiumEntity* Subject = World.FindByName(TEXT("subject"));
		FElysiumEntity* First = World.FindByName(TEXT("first"));
		FElysiumEntity* Second = World.FindByName(TEXT("second"));
		if (!Subject || !First || !Second || !TestNotNull(TEXT("authored subject creates native brush"), Subject->VPhysicsGetObject())) return false;
		auto MoveTarget = [&TransformTarget](FElysiumEntity& Entity, FVector Position)
		{
			Position=TransformTarget(Position);
			Entity.SetAbsOrigin(Position);
			Entity.UpdatePhysicsShadowToCurrentPosition(0.f);
		};
		double ObservedHostStart = World.NowSeconds();
		float ObservedHostDt = 0.f;
		float ObservedPreparedDt = 0.f;
		auto SourcePositionWords=[](const FVector& Cm)
		{ return FVector(static_cast<float>(Cm.X/2.54),static_cast<float>(-Cm.Y/2.54),static_cast<float>(Cm.Z/2.54)); };
		struct FContactEvidence
		{
			int32 FirstPoints = 0;
			int32 SecondPoints = 0;
			int32 InitialPoints = 0;
			double MinimumPhi = TNumericLimits<double>::Max();
			bool bPresent = false;
			FElysiumEntity* Selected = nullptr;
		};
		auto Observe = [&](const TCHAR* Phase)
		{
			FContactEvidence Evidence;
			FBodyInstance* NativeBody = ElysiumModelPhysics::Identity(Subject->Body);
			if (!NativeBody) return Evidence;
			if (auto* Solver = NativeBody->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>()) Solver->WaitOnPendingTasks_External();
			// GT, after the completed solve: report the existing target and native pose
			// separately, through the same double centimetres -> float Source stores.
			for (FElysiumEntity* Observed : {Subject,First,Second})
			{
				const auto* Shadow = ElysiumModelPhysics::ShadowFor(Observed->VPhysicsGetObject());
				FVector SolvedPosition; FRotator SolvedAngles;
				const bool bSolved = ElysiumModelPhysics::GetPosition(Observed->VPhysicsGetObject(),SolvedPosition,SolvedAngles);
				const FVector TargetWords=SourcePositionWords(Shadow ? Shadow->Target.GetLocation() : FVector::ZeroVector);
				const FVector SolvedWords=SourcePositionWords(bSolved ? SolvedPosition : FVector::ZeroVector);
				AddInfo(FString::Printf(TEXT("contact history phase=%s body=%s hostStart=%.17g hostEnd=%.17g hostFloatDt=%.9g nativeFrameDt=%.9g preparedDt=%.9g targetPresent=%d solvedPresent=%d target=(%.9g,%.9g,%.9g) solved=(%.9g,%.9g,%.9g)"),
					Phase,*Observed->TargetName,ObservedHostStart,World.NowSeconds(),ObservedHostDt,static_cast<float>(World.FrameSeconds()),ObservedPreparedDt,Shadow!=nullptr,bSolved,
					TargetWords.X,TargetWords.Y,TargetWords.Z,SolvedWords.X,SolvedWords.Y,SolvedWords.Z));
			}
			FPhysicsCommand::ExecuteRead(NativeBody->GetPhysicsActor(), [&](const FPhysicsActorHandle& NativeActor)
			{
				const auto* Particle = NativeActor->GetHandle_LowLevel(); if (!Particle) return;
				Particle->ParticleCollisions().VisitConstCollisions([&](const Chaos::FPBDCollisionConstraint& Contact)
				{
					const auto* Partner = Contact.GetParticle0() == Particle ? Contact.GetParticle1() : Contact.GetParticle0();
					void* PartnerObject = Partner ? const_cast<void*>(static_cast<const void*>(Partner->PhysicsProxy())) : nullptr;
					FElysiumEntity* PartnerEntity = ElysiumModelPhysics::GameDataForObject(PartnerObject);
					int32 PointIndex = 0;
					for (const auto& Point : Contact.GetManifoldPoints())
					{
						if (Contact.IsManifoldPointActive(PointIndex))
						{
							if (PartnerEntity == First) ++Evidence.FirstPoints;
							if (PartnerEntity == Second) ++Evidence.SecondPoints;
							if (Point.Flags.bInitialContact) ++Evidence.InitialPoints;
							Evidence.MinimumPhi = FMath::Min(Evidence.MinimumPhi, static_cast<double>(Point.ContactPoint.Phi));
						}
						AddInfo(FString::Printf(TEXT("contact phase=%s partner=%s constraint=%p shape0=%p shape1=%p point=%d phi=%.9g flags=%u normal=%s anchor0=%s anchor1=%s initial0=%s initial1=%s face=%d type=%d saved=%d"),
							Phase, PartnerEntity ? *PartnerEntity->TargetName : TEXT("nullGameData"), &Contact, Contact.GetImplicit0(), Contact.GetImplicit1(), PointIndex++,
							Point.ContactPoint.Phi, Point.Flags.Bits, *FVector(Point.ContactPoint.ShapeContactNormal).ToString(), *FVector(Point.ShapeAnchorPoints[0]).ToString(), *FVector(Point.ShapeAnchorPoints[1]).ToString(),
							*FVector(Point.InitialShapeContactPoints[0]).ToString(), *FVector(Point.InitialShapeContactPoints[1]).ToString(),
							Point.ContactPoint.FaceIndex, static_cast<int32>(Point.ContactPoint.ContactType), Contact.NumSavedManifoldPoints()));
					}
					return Chaos::ECollisionVisitorResult::Continue;
				});
			});
			FString Provenance;
			void* Selected = nullptr; Evidence.bPresent = ElysiumModelPhysics::GetContactPoint(Subject->VPhysicsGetObject(), Selected, &Provenance);
			AddInfo(FString::Printf(TEXT("contact provenance phase=%s %s"), Phase, *Provenance));
			Evidence.Selected = ElysiumModelPhysics::GameDataForObject(Selected);
			AddInfo(FString::Printf(TEXT("contact selected phase=%s present=%d partner=%s firstPoints=%d secondPoints=%d initialPoints=%d minPhi=%.9g"), Phase, Evidence.bPresent,
				Evidence.Selected ? *Evidence.Selected->TargetName : TEXT("nullGameData"), Evidence.FirstPoints, Evidence.SecondPoints, Evidence.InitialPoints, Evidence.MinimumPhi));
			return Evidence;
		};
		bool bObserveOrdinaryFirstFeedback = false;
		bool bOrdinaryFirstSolved = false;
		int32 OrdinaryFirstBlocked = 0;
		TFunction<void(const TCHAR*)> ObserveBeforeSolve = [&](const TCHAR* Stage)
		{
			if (FCString::Strcmp(Stage,TEXT("after_solve"))==0)
			{
				if (bObserveOrdinaryFirstFeedback && !bOrdinaryFirstSolved)
				{
					bOrdinaryFirstSolved = true;
					const auto Active = ElysiumModelPhysics::ActiveOutputs(World);
					TestTrue(TEXT("first ordinary opposed frame admits actual Subject creation-owned226"), Active.ContainsByPredicate([&](const auto& Entry)
					{ return ElysiumModelPhysics::IsCurrentOutput(Entry) && ElysiumModelPhysics::GameDataFor(Entry,World)==Subject; }));
					FVector NativeOrigin; FQuat NativeRotation;
					TestEqual(TEXT("first ordinary feedback reads actual47 sample"), ElysiumModelPhysics::GetShadowPosition(Subject->VPhysicsGetObject(),&NativeOrigin,&NativeRotation),1);
					const FVector Logical = SourcePositionWords(Subject->GetAbsOrigin());
					const FVector Solved = SourcePositionWords(NativeOrigin);
					const FVector Difference = Solved-Logical;
					AddInfo(FString::Printf(TEXT("ordinary first feedback host=%.17g local=%.9g start=%.9g interval=%.9g velocity=%s angular=%s logicalSource=%s solved47Source=%s distanceSquared=%.17g"),
						World.NowSeconds(),Subject->LocalTime,Subject->StartFrameLocalTime,Subject->LocalTime-Subject->StartFrameLocalTime,
						*Subject->Velocity.ToString(),*Subject->AngularVelocity.ToString(),*Logical.ToString(),*Solved.ToString(),Difference.SizeSquared()));
					TestEqual(TEXT("ordinary first frame has no advancing pusher local interval"),Subject->LocalTime-Subject->StartFrameLocalTime,0.f);
					TestTrue(TEXT("ordinary first frame has no authored local movement velocity"),Subject->Velocity.IsZero() && Subject->AngularVelocity.IsZero());
				}
				return;
			}
			if (FCString::Strcmp(Stage,TEXT("after_feedback"))==0)
			{
				if (bObserveOrdinaryFirstFeedback && bOrdinaryFirstSolved)
				{
					TestEqual(TEXT("ordinary first actual226 interval gate dispatches no178"),OrdinaryFirstBlocked,0);
					TestEqual(TEXT("ordinary first actual226 preserves equal local clocks"),Subject->LocalTime,Subject->StartFrameLocalTime);
					bObserveOrdinaryFirstFeedback=false;
				}
				return;
			}
			for (FElysiumEntity* Observed : { Subject, First, Second })
			{
				FBodyInstance* BodyState = ElysiumModelPhysics::Identity(Observed->Body);
				if (!BodyState) continue;
				if (auto* Solver = BodyState->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>()) Solver->WaitOnPendingTasks_External();
				const auto* Shadow = ElysiumModelPhysics::ShadowFor(Observed->VPhysicsGetObject());
				if (!Shadow) continue;
				const FVector Target = SourcePositionWords(Shadow->Target.GetLocation());
				const FVector Solved = SourcePositionWords(BodyState->GetUnrealWorldTransform().GetLocation());
				const FVector Linear = SourcePositionWords(BodyState->GetUnrealWorldVelocity());
				const FVector Angular = BodyState->GetUnrealWorldAngularVelocityInRadians();
				const FVector Prediction = SourcePositionWords(Shadow->PredictedPosition);
				FVector PoseOrigin; FRotator PoseAngles;
				if (ElysiumModelPhysics::GetPosition(Observed->VPhysicsGetObject(),PoseOrigin,PoseAngles))
					AddInfo(FString::Printf(TEXT("shadow angles stage=%s body=%s hostEnd=%.17g Source=(%.9g,%.9g,%.9g)"),
						Stage,*Observed->TargetName,World.NowSeconds(),PoseAngles.Pitch,PoseAngles.Yaw,PoseAngles.Roll));
				AddInfo(FString::Printf(TEXT("shadow input stage=%s body=%s hostStart=%.17g hostEnd=%.17g frameDt=%.9g remaining=%.9g active=%d allowMove=%d allowRotate=%d gravityMode=%d linearDampingCapCm=%.9g linearDesiredCapCm=%.9g angularDampingCapRad=%.9g angularDesiredCapRad=%.9g teleportCm=%.9g target=(%.9g,%.9g,%.9g) solved=(%.9g,%.9g,%.9g) prediction=(%.9g,%.9g,%.9g) linearSource=(%.9g,%.9g,%.9g) angularNativeRad=(%.9g,%.9g,%.9g)"),
					Stage,*Observed->TargetName,ObservedHostStart,World.NowSeconds(),static_cast<float>(World.FrameSeconds()),
					Shadow->RemainingSeconds,Shadow->bActive,Shadow->bAllowMovement,Shadow->bAllowRotation,Shadow->bGravityMode,
					Shadow->LinearDampingLimitCm,Shadow->LinearDesiredLimitCm,Shadow->AngularDampingLimitRadians,Shadow->AngularDesiredLimitRadians,Shadow->TeleportDistanceCm,
					Target.X,Target.Y,Target.Z,Solved.X,Solved.Y,Solved.Z,Prediction.X,Prediction.Y,Prediction.Z,
					Linear.X,Linear.Y,Linear.Z,Angular.X,Angular.Y,Angular.Z));
			}
		};
		auto AdvanceObserved = [&](const TCHAR* Phase, double Until)
		{
			FContactEvidence Evidence;
			int32 Frame = 0;
			while (World.NowSeconds() < Until)
			{
				ObservedHostStart=World.NowSeconds();
				const double ObservedHostEnd=FMath::Min(ObservedHostStart + 1./60., Until);
				ObservedHostDt=static_cast<float>(ObservedHostEnd-ObservedHostStart);
				ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, ObservedHostEnd,&ObservedPreparedDt,ObserveBeforeSolve);
				Evidence = Observe(*FString::Printf(TEXT("%s_%d"), Phase, ++Frame));
			}
			return Evidence;
		};
		FContactEvidence Evidence = AdvanceObserved(TEXT("first_birth"), .05);
		TestTrue(TEXT("actual first body creates enabled native manifold points"), Evidence.FirstPoints > 0);
		TestTrue(TEXT("single qualifying body is selected"), Evidence.bPresent && Evidence.Selected == First);
		MoveTarget(*Second, FVector(-19,0,1000));
		Evidence = AdvanceObserved(TEXT("second_birth"), .10);
		TestTrue(TEXT("two real native manifolds survive together"), Evidence.FirstPoints > 0 && Evidence.SecondPoints > 0);
		TestTrue(TEXT("later qualifying body is selected before feature change"), Evidence.bPresent && Evidence.Selected == Second);
		auto ProveBlocked = [&](FElysiumEntity* Expected, const TCHAR* Phase)
		{
			auto* Door = Subject->AsDoorBase();
			Door->InputClose(FElysiumEntityHandle::Invalid());
			ContactSink->Reset();
			FVector Solved; FQuat Rotation;
			TestTrue(TEXT("native solved pose exists before actual feedback"), ElysiumModelPhysics::GetShadowPosition(Subject->VPhysicsGetObject(), &Solved, &Rotation) != 0);
			// Discrepancy strictly exceeds speed*dt; Source operand rounding cannot
			// leave a tiny positive corrected time and bypass the reached178 arm.
			FVector Displaced = Solved + FVector(40,0,0); Subject->SetAbsOrigin(Displaced); Subject->Velocity = FVector(20,0,0);
			Subject->StartFrameLocalTime = 0.f; Subject->LocalTime = 1.f; Subject->PhysicsMoveDoneTime = 0.f;
			bool bExpectedReceiver = false;
			World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
			{
				if (Event.Entity == Subject->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=pusher_blocked")))
				{ AddInfo(FString::Printf(TEXT("%s %s"), Phase, *Event.Text)); bExpectedReceiver = Event.Text.Contains(TEXT("other=") + Expected->TargetName + TEXT(" ")); }
			});
			Subject->VPhysicsUpdatePusher(Subject->VPhysicsGetObject());
			World.SetAiTraceSink({});
			TestEqual(TEXT("feedback actually reaches the full-rewind clock boundary"), Subject->LocalTime, 0.f);
			TestTrue(TEXT("real native44 dispatches178 with the literal later qualifying partner"), bExpectedReceiver);
			TestEqual(TEXT("ordinary sliding178 retains literal false654 and emits no reopen"), ContactSink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
			Subject->SetAbsOrigin(Solved); Subject->Velocity = FVector::ZeroVector; Subject->PhysicsMoveDoneTime = 0.f;
		};
		ProveBlocked(Second, TEXT("staggered_receiver"));
		// No explicit44 query sees the intermediate nonqualifying frame. The completed
		// pre-feedback refresh must retire qualification, then admit the new entry.
		MoveTarget(*First, FVector(23.1,0,1000));
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .15);
		MoveTarget(*First, FVector(19,0,1000));
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, .20);
		Evidence = Observe(TEXT("qualification_reentry_without_query"));
		TestTrue(TEXT("qualifying reentry after an unqueried skin exit selects first over second survivor"), Evidence.bPresent && Evidence.Selected == First && Evidence.SecondPoints > 0);
		ProveBlocked(First, TEXT("qualification_reentry_receiver"));
		// A different physical face, rather than tangential sliding on the same face.
		// Point identity and the resulting retail feature equivalence remain evidence to recover.
		MoveTarget(*First, FVector(0,19,1000));
		Evidence = AdvanceObserved(TEXT("first_face_change"), .25);
		TestTrue(TEXT("face change retains actual contacts with both bodies"), Evidence.FirstPoints > 0 && Evidence.SecondPoints > 0);
		MoveTarget(*Second, FVector(-100,0,1000));
		Evidence = AdvanceObserved(TEXT("second_separation"), .30);
		TestTrue(TEXT("separation exposes actual first survivor"), Evidence.bPresent && Evidence.Selected == First && Evidence.SecondPoints == 0);
		MoveTarget(*Second, FVector(-19,0,1000));
		Evidence = AdvanceObserved(TEXT("second_rebirth"), .35);
		TestTrue(TEXT("retired separated contact returns as a later observed birth"), Evidence.bPresent && Evidence.Selected == Second);
		Second->VPhysicsDestroyObject(); Evidence = Observe(TEXT("second_retired"));
		TestTrue(TEXT("native body retirement reveals the older surviving partner"), Evidence.bPresent && Evidence.Selected == First);
		MoveTarget(*First, FVector(0,22.8,1000));
		Evidence = AdvanceObserved(TEXT("positive_skin"), .40);
		TestTrue(TEXT("native surface gap is positive within the recovered skin"), Evidence.FirstPoints > 0 && Evidence.MinimumPhi > 0. && Evidence.MinimumPhi < 2.8563570231199265);
		TestTrue(TEXT("positive skin contact remains selected"), Evidence.bPresent && Evidence.Selected == First);
		MoveTarget(*First, FVector(0,23.1,1000));
		Evidence = AdvanceObserved(TEXT("outside_skin"), .45);
		TestFalse(TEXT("outside skin does not manufacture a contact from cached allocation"), Evidence.bPresent);
		// Recreate through the actual recipe, then admit both physical partners in one native step.
		Second->CreateVPhysics();
		MoveTarget(*First, FVector(0,100,1000)); MoveTarget(*Second, FVector(-100,0,1000));
		AdvanceObserved(TEXT("same_step_clear"), .50);
		MoveTarget(*First, FVector(0,19,1000)); MoveTarget(*Second, FVector(-19,0,1000));
		Evidence = AdvanceObserved(TEXT("same_step_birth"), .55);
		TestTrue(TEXT("same-step experiment has two genuine qualifying partners"), Evidence.FirstPoints > 0 && Evidence.SecondPoints > 0 && Evidence.bPresent);
		// Public retail zero-arrival comparator091225/091340 matches actual232's
		// arrival0 input; the prior per-frame arrivalDt comparator selected Second.
		// Opposing boxes prove both links to Subject and literal selectedFirst.
		// Reset Subject after earlier actual178 pose captures.
		MoveTarget(*Subject, FVector(0,0,1000));
		MoveTarget(*First, FVector(100,0,1000)); MoveTarget(*Second, FVector(-100,0,1000));
		Evidence = AdvanceObserved(TEXT("opposed_clear"), .60);
		TestFalse(TEXT("matched opposing comparator begins with both contact links retired"), Evidence.bPresent);
		MoveTarget(*First, FVector(19,0,1000)); MoveTarget(*Second, FVector(-19,0,1000));
		bObserveOrdinaryFirstFeedback=true;
		World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
		{ if (Event.Entity==Subject->Handle && Event.Kind==TEXT("retail_site") && Event.Text.Contains(TEXT("tag=pusher_blocked"))) ++OrdinaryFirstBlocked; });
		Evidence = AdvanceObserved(TEXT("opposed_birth"), .65);
		World.SetAiTraceSink({});
		TestTrue(TEXT("ordinary first native feedback boundary was observed"),bOrdinaryFirstSolved);
		TestTrue(TEXT("matched opposing comparator reaches two actual qualifying Subject contacts"), Evidence.FirstPoints > 0 && Evidence.SecondPoints > 0 && Evidence.bPresent);
		FElysiumEntity* LiteralPartner=First;
		TestTrue(TEXT("public retail opposing comparator literal selected partner"), Evidence.Selected == LiteralPartner);
		ProveBlocked(LiteralPartner, TEXT("opposed_same_step_receiver"));
		return true;
	}

	if (Parameters == TEXT("RotatingDoorHandedness"))
	{
		AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
		if (!TestNotNull(TEXT("mover owner spawned"), Owner))
		{
			return false;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->AddInstanceComponent(Root);

		auto DoorHull = []()
		{
			// An asymmetric leaf extending from its hinge along local +X makes the swing direction
			// observable from the embodied tip, rather than merely inspecting a cached target angle.
			FElysiumConvexHull Hull;
			for (float X : { 0.0f, 100.0f })
			{
				for (float Y : { -5.0f, 5.0f })
				{
					for (float Z : { -100.0f, 100.0f })
					{
						Hull.Vertices.Emplace(X, Y, Z);
					}
				}
			}
			return Hull;
		};

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__rotating_door_handedness__");
		auto AddDoor = [&Defs, &DoorHull](const TCHAR* Name, int32 Model, const FVector& Origin,
			int32 SpawnFlags)
		{
			FElysiumEntityDef Door;
			Door.Classname = TEXT("func_door_rotating");
			Door.TargetName = Name;
			Door.Origin = Origin;
			Door.Model = Model;
			Door.Hulls.Add(DoorHull());
			Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
			Door.Keys.Add(TEXT("distance"), TEXT("90"));
			Door.Keys.Add(TEXT("speed"), TEXT("90"));
			Door.Keys.Add(TEXT("wait"), TEXT("-1"));
			Door.Keys.Add(TEXT("spawnflags"), FString::FromInt(SpawnFlags));
			Defs.Defs.Add(MoveTemp(Door));
		};
		AddDoor(TEXT("normal"), 1, FVector::ZeroVector, 0);
		AddDoor(TEXT("reversed"), 2, FVector(500.0f, 0.0f, 0.0f), 0x2);

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumDoorBase* Normal = World.FindByName(TEXT("normal"))
			? World.FindByName(TEXT("normal"))->AsDoorBase() : nullptr;
		FElysiumDoorBase* Reversed = World.FindByName(TEXT("reversed"))
			? World.FindByName(TEXT("reversed"))->AsDoorBase() : nullptr;
		if (!TestNotNull(TEXT("normal rotating door resolved"), Normal)
			|| !TestNotNull(TEXT("reversed rotating door resolved"), Reversed)
			|| !TestNotNull(TEXT("normal rotating door has a body"), Normal ? Normal->Body : nullptr)
			|| !TestNotNull(TEXT("reversed rotating door has a body"), Reversed ? Reversed->Body : nullptr))
		{
			return false;
		}

		Normal->InputOpen(FElysiumEntityHandle());
		Reversed->InputOpen(FElysiumEntityHandle());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 1.0);
		AddInfo(FString::Printf(TEXT("handedness clock local=%.9g start=%.9g deadline=%.9g angles=%s native=%s"),
			Normal->LocalTime, Normal->StartFrameLocalTime, Normal->PhysicsMoveDoneTime,
			*Normal->GetAngles().ToString(), *Normal->Body->GetRelativeRotation().ToString()));

		const FVector NormalTip = Normal->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f));
		const FVector ReversedTip = Reversed->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f));
		TestTrue(TEXT("a normal Source +90 swing reflects to Unreal -Y"),
			NormalTip.Equals(FVector(0.0f, -100.0f, 0.0f), 0.1f));
		TestTrue(TEXT("REVERSE negates the Source swing before reflection"),
			ReversedTip.Equals(FVector(0.0f, 100.0f, 0.0f), 0.1f));
		TestTrue(TEXT("float local clock remains below the one-second deadline at nominal one second"),
			Normal->LocalTime < Normal->PhysicsMoveDoneTime && Reversed->LocalTime < Reversed->PhysicsMoveDoneTime);
		TestEqual(TEXT("deadline has not completed before the float clock reaches it"), Normal->State(),
			FElysiumDoorBase::EToggleState::GoingUp);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 1.02);
		TestEqual(TEXT("normal door reaches its open state"), Normal->State(),
			FElysiumDoorBase::EToggleState::AtTop);
		TestEqual(TEXT("reversed door reaches its open state"), Reversed->State(),
			FElysiumDoorBase::EToggleState::AtTop);

		return true;
	}
	
	// Activator-relative swing — retail CRotDoor::OpenAwayFromEntity (FUN_100f3390) + ComputeSwingData
	// (FUN_100f19b0). Reference: research/event-surface/swing-centres-findings.md ITEM 1 and
	// door-largeitems-spec.md TARGET 1 (both 100%-CONFIRMED). A rotating door swings toward whichever of
	// its two swing reference centres is FARTHER from the activator's world centre, i.e. it opens away
	// from the activator; the swing MAGNITUDE stays the door's configured `distance`. The blocked-latch
	// inversion is HELD (retail field identity unrecovered) and not exercised here.
	//
	// Red/green: pre-fix IssueMoveToOpen always swung the fixed forward OpenRot (yaw -90 -> tip on -Y)
	// regardless of the activator, so the south-activator case (which must now swing to +Y) fails against
	// the pre-fix code; the mirror across the two sides is the observable contract. The handedness test
	// above stays green because it opens with NO activator, which falls back to the same forward pose.
	
	else if (Parameters == TEXT("RotatingDoorOpensAwayFromActivator"))
	{
		using EToggleState = FElysiumDoorBase::EToggleState;

		AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
		if (!TestNotNull(TEXT("mover owner spawned"), Owner))
		{
			return false;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->AddInstanceComponent(Root);

		// An asymmetric leaf from the hinge along local +X. The tip makes the swing observable, and the
		// door's two swing centres straddle the Y axis, so an activator on -Y vs +Y picks opposite sides.
		auto DoorHull = []()
		{
			FElysiumConvexHull Hull;
			for (float X : { 0.0f, 100.0f })
			{
				for (float Y : { -5.0f, 5.0f })
				{
					for (float Z : { -100.0f, 100.0f })
					{
						Hull.Vertices.Emplace(X, Y, Z);
					}
				}
			}
			return Hull;
		};

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__rotating_door_open_away__");

		FElysiumEntityDef Door;
		Door.Classname = TEXT("func_door_rotating");
		Door.TargetName = TEXT("door");
		Door.Origin = FVector::ZeroVector;
		Door.Model = 1;
		Door.Hulls.Add(DoorHull());
		Door.Keys.Add(TEXT("model"), TEXT("*1"));
		Door.Keys.Add(TEXT("distance"), TEXT("90"));
		Door.Keys.Add(TEXT("speed"), TEXT("90"));
		Door.Keys.Add(TEXT("wait"), TEXT("-1"));   // stay open; close explicitly between the two cases
		Door.Keys.Add(TEXT("spawnflags"), TEXT("0"));
		Defs.Defs.Add(MoveTemp(Door));

		// Two activators, one clearly on each side of the hinge along Y. A math_counter is a bodiless
		// point entity whose live Origin is seeded from its def — exactly the world position the swing
		// resolution reads off the activator.
		auto AddActivator = [&Defs](const TCHAR* Name, const FVector& Origin)
		{
			FElysiumEntityDef A;
			A.Classname = TEXT("math_counter");
			A.TargetName = Name;
			A.Origin = Origin;
			A.Keys.Add(TEXT("min"), TEXT("0"));
			A.Keys.Add(TEXT("max"), TEXT("1000"));
			Defs.Defs.Add(MoveTemp(A));
		};
		AddActivator(TEXT("south"), FVector(0.0f, -300.0f, 0.0f));
		AddActivator(TEXT("north"), FVector(0.0f,  300.0f, 0.0f));

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* DoorEntity = World.FindByName(TEXT("door"));
		FElysiumDoorBase* DoorPtr = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
		FElysiumEntity* South = World.FindByName(TEXT("south"));
		FElysiumEntity* North = World.FindByName(TEXT("north"));
		if (!TestNotNull(TEXT("door resolved"), DoorPtr)
			|| !TestNotNull(TEXT("door has a body"), DoorPtr ? DoorPtr->Body : nullptr)
			|| !TestNotNull(TEXT("south activator resolved"), South)
			|| !TestNotNull(TEXT("north activator resolved"), North))
		{
			return false;
		}

		const FRotator ClosedRot = DoorPtr->Body->GetRelativeRotation();
		auto TipY = [&]()
		{
			return DoorPtr->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f)).Y;
		};
		auto SwingDegrees = [&]()
		{
			return (float)FMath::RadiansToDegrees(
				ClosedRot.Quaternion().AngularDistance(DoorPtr->Body->GetRelativeRotation().Quaternion()));
		};

		// --- Activator on -Y: the leaf opens toward +Y (away from the activator). --------------------
		DoorPtr->InputOpen(South->Handle);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 2.0);   // 90deg at 90deg/s completes in 1s; 2s is margin
		TestEqual(TEXT("south-opened door reaches AtTop"), DoorPtr->State(), EToggleState::AtTop);
		TestTrue(TEXT("an activator on -Y swings the leaf toward +Y (open away)"), TipY() > 90.0f);
		TestTrue(TEXT("the swing magnitude equals the configured distance (90 deg)"),
			FMath::IsNearlyEqual(SwingDegrees(), 90.0f, 0.5f));

		// Close back to the hinge so the mirror case starts from the same closed pose.
		DoorPtr->InputClose(South->Handle);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 4.0);
		TestEqual(TEXT("the door closes back to AtBottom"), DoorPtr->State(), EToggleState::AtBottom);

		// --- Activator on +Y: the SAME door now opens toward -Y (mirrored direction). ----------------
		DoorPtr->InputOpen(North->Handle);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 6.0);
		TestEqual(TEXT("north-opened door reaches AtTop"), DoorPtr->State(), EToggleState::AtTop);
		TestTrue(TEXT("an activator on +Y swings the leaf toward -Y (mirror of the -Y case)"),
			TipY() < -90.0f);
		TestTrue(TEXT("the mirrored swing magnitude is still the configured distance (90 deg)"),
			FMath::IsNearlyEqual(SwingDegrees(), 90.0f, 0.5f));

		return true;
	}
	// GATE 1 — actual rotating178 selects the nearer forward endpoint in this closing history,
	// then pre-issues AngularMove before GoUp(1,0). The retained live activator must not replace
	// that selected move with an activator-relative swing.
	else if (Parameters == TEXT("RotatingDoorBlockReverseFixedForward"))
	{
		using namespace ElysiumSwingGateTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
		if (!Owner)
		{
			return false;
		}

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__rotating_door_block_reverse__");
		Defs.Defs.Add(RotatingDoor(TEXT("door"), 1, /*SpawnFlags*/ 0));
		Defs.Defs.Add(Activator(TEXT("south"), FVector(0.0f, -300.0f, 0.0f)));   // -Y => base swing picks BackRot
		Defs.Defs.Add(Activator(TEXT("east_blocker"), FVector(300.0f, 0.0f, 0.0f)));

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* DoorEntity = World.FindByName(TEXT("door"));
		FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
		FElysiumEntity* South = World.FindByName(TEXT("south"));
		if (!TestNotNull(TEXT("door resolved"), Door)
			|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr)
			|| !TestNotNull(TEXT("south activator resolved"), South))
		{
			return false;
		}

		auto TipY = [&]()
		{
			return Door->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f)).Y;
		};

		// --- bResolveSwing == true: the same activator resolves activator-relative to BackRot (tip +Y). ---
		FElysiumDoorTestAccess::DoorGoUp(*Door, South->Handle, /*bResolveSwing*/ true);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 2.0);   // 90deg at 90deg/s completes in 1s; 2s is margin
		TestEqual(TEXT("resolve-swing open reaches AtTop"), Door->State(), EToggleState::AtTop);
		TestTrue(TEXT("with bResolveSwing the -Y activator swings the leaf activator-relatively to +Y (BackRot)"),
			TipY() > 90.0f);

		// Close back to the hinge so the block-reverse case starts from the same closed pose.
		Door->InputClose(South->Handle);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 4.0);
		TestEqual(TEXT("the door closes back to AtBottom"), Door->State(), EToggleState::AtBottom);

		// Start on the forward side, then close with the south activator retained. Actual178
		// chooses the nearer forward endpoint and pre-issues AngularMove before GoUp(1,0).
		Door->InputOpen(FElysiumEntityHandle::Invalid());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 6.0);
		Door->InputClose(South->Handle);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 6.5);
		FElysiumEntity* Blocker = World.FindByName(TEXT("east_blocker"));
		if (!TestNotNull(TEXT("actual receiver blocker entity resolves"), Blocker)) return false;
		Door->Blocked(Blocker);
		TestEqual(TEXT("actual178 reopens before native arrival"), Door->State(), EToggleState::GoingUp);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 8.0);
		TestEqual(TEXT("block-reverse open reaches AtTop"), Door->State(), EToggleState::AtTop);
		TestTrue(TEXT("the block-reverse open is fixed-forward OpenRot (tip -Y), not activator-relative BackRot"),
			TipY() < -90.0f);

		return true;
	}
	// GATE 2 — a ONEWAY door ignores the activator. With SF_DOOR_ONEWAY (0x10) set and the activator on the
	// BackRot side (-Y), the normal player/logic open path (InputOpen) still swings the fixed-forward
	// OpenRot (tip -Y). Red pre-fix: ChooseOpenTarget had no ONEWAY gate, so it resolved activator-relative
	// to BackRot (tip +Y); green post-fix: the early return forces OpenRot.
	else if (Parameters == TEXT("RotatingDoorOneWayIgnoresActivator"))
	{
		using namespace ElysiumSwingGateTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		constexpr int32 SF_DOOR_ONEWAY = 0x10;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
		if (!Owner)
		{
			return false;
		}

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__rotating_door_oneway__");
		Defs.Defs.Add(RotatingDoor(TEXT("door"), 1, SF_DOOR_ONEWAY));
		Defs.Defs.Add(Activator(TEXT("south"), FVector(0.0f, -300.0f, 0.0f)));   // -Y => base swing would pick BackRot

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* DoorEntity = World.FindByName(TEXT("door"));
		FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
		FElysiumEntity* South = World.FindByName(TEXT("south"));
		if (!TestNotNull(TEXT("door resolved"), Door)
			|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr)
			|| !TestNotNull(TEXT("south activator resolved"), South))
		{
			return false;
		}

		auto TipY = [&]()
		{
			return Door->Body->GetRelativeRotation().RotateVector(FVector(100.0f, 0.0f, 0.0f)).Y;
		};

		// Normal open path with a resolved activator on the BackRot side; ONEWAY forces fixed-forward.
		Door->InputOpen(South->Handle);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 2.0);
		TestEqual(TEXT("the ONEWAY door reaches AtTop"), Door->State(), EToggleState::AtTop);
		TestTrue(TEXT("a ONEWAY door swings fixed-forward OpenRot (tip -Y) despite the -Y activator"),
			TipY() < -90.0f);

		return true;
	}
	// A locked door refuses the direct `Open` and `Toggle` inputs silently and inertly, and never fires
	// OnLockedUse (which CBaseDoor does not own). The `+use` path likewise fires no output and does not
	// move. (Fails against the pre-fix code, which fired OnLockedUse and beeped from InputOpen.)
	else if (Parameters == TEXT("LockedDoorRefusesSilently"))
	{
		using namespace ElysiumLockedDoorTests;

		AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
		if (!TestNotNull(TEXT("mover owner spawned"), Owner))
		{
			return false;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->AddInstanceComponent(Root);

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__locked_door_refuses__");
		Defs.Defs.Add(LockedDoor(TEXT("locked"), 1, SF_DOOR_LOCKED));
		Defs.Defs.Add(Counter(TEXT("sink_counter")));

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
		FElysiumOrderedIOSink* Sink = OwnedSink.Get();
		World.AddSink(MoveTemp(OwnedSink));
		World.Activate(0.0);

		FElysiumEntity* DoorEntity = World.FindByName(TEXT("locked"));
		FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
		if (!TestNotNull(TEXT("locked door resolved"), Door)
			|| !TestNotNull(TEXT("locked door has a body"), Door ? Door->Body : nullptr))
		{
			return false;
		}
		TestTrue(TEXT("the LOCKED spawnflag locked the door"), Door->bLocked);
		TestEqual(TEXT("the door starts closed"), Door->State(),
			FElysiumDoorBase::EToggleState::AtBottom);

		// --- direct Open input: silent, no output, no motion ---------------------------------------
		Door->InputOpen(FElysiumEntityHandle());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 1.0);
		TestEqual(TEXT("locked Open does not move the door"), Door->State(),
			FElysiumDoorBase::EToggleState::AtBottom);
		TestFalse(TEXT("locked Open schedules no motion"), Door->IsMoving());
		TestEqual(TEXT("locked Open fires no OnOpen"), Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
		TestEqual(TEXT("a door never fires OnLockedUse on Open"),
			Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);
		TestEqual(TEXT("locked Open is silent (no voice submitted)"), Services.Count(TEXT("Submit")), 0);

		// --- direct Toggle input: nothing at all ---------------------------------------------------
		Door->InputToggle(FElysiumEntityHandle());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 1.0);
		TestEqual(TEXT("locked Toggle does not move the door"), Door->State(),
			FElysiumDoorBase::EToggleState::AtBottom);
		TestFalse(TEXT("locked Toggle schedules no motion"), Door->IsMoving());
		TestEqual(TEXT("locked Toggle fires no OnOpen"), Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
		TestEqual(TEXT("locked Toggle fires no OnClose"), Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 0);
		TestEqual(TEXT("a door never fires OnLockedUse on Toggle"),
			Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);
		TestEqual(TEXT("locked Toggle is silent (no voice submitted)"),
			Services.Count(TEXT("Submit")), 0);

		// --- +use path: no output, no motion (the `locked` sound is Content-tier, not asserted) -----
		Door->DoorUse(FElysiumEntityHandle());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 1.0);
		TestEqual(TEXT("locked +use does not move the door"), Door->State(),
			FElysiumDoorBase::EToggleState::AtBottom);
		TestFalse(TEXT("locked +use schedules no motion"), Door->IsMoving());
		TestEqual(TEXT("locked +use fires no OnOpen"), Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 0);
		TestEqual(TEXT("a door never fires OnLockedUse on +use"),
			Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);

		return true;
	}
	// A locked door resting OPEN is not closed by +use (retail Use returns on the locked branch before
	// DoorActivate). A direct `Close` input, which retail leaves un-lock-checked, still closes it — so
	// the fix must not add a lock gate to Close. (The +use half fails against the pre-fix code, which
	// routed +use -> Toggle -> Close and closed the locked open door.)
	else if (Parameters == TEXT("LockedOpenDoorCloseMatrix"))
	{
		using namespace ElysiumLockedDoorTests;

		AActor* Owner = EngineWorld ? EngineWorld->SpawnActor<AActor>() : nullptr;
		if (!TestNotNull(TEXT("mover owner spawned"), Owner))
		{
			return false;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("MoverRoot"));
		Owner->SetRootComponent(Root);
		Root->RegisterComponent();
		Owner->AddInstanceComponent(Root);

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__locked_open_door_close__");
		// Produce state0 through the actual Open/arrival path, then lock it. Rotating START_OPEN
		// instead swaps endpoints and starts at state1 (100f1c60), where Close is a no-op.
		Defs.Defs.Add(LockedDoor(TEXT("locked_open"), 1, SF_DOOR_LOCKED));
		FElysiumEntityDef StartOpenDef = LockedDoor(TEXT("start_open"), 2, SF_DOOR_START_OPEN | SF_DOOR_LOCKED);
		StartOpenDef.Origin = FVector(1000., 0., 0.);
		Defs.Defs.Add(MoveTemp(StartOpenDef));
		Defs.Defs.Add(Counter(TEXT("sink_counter")));

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
		FElysiumOrderedIOSink* Sink = OwnedSink.Get();
		World.AddSink(MoveTemp(OwnedSink));
		World.Activate(0.0);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 0.1);   // run the START_OPEN seat think

		FElysiumEntity* DoorEntity = World.FindByName(TEXT("locked_open"));
		FElysiumDoorBase* Door = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
		if (!TestNotNull(TEXT("locked open door resolved"), Door)
			|| !TestNotNull(TEXT("locked open door has a body"), Door ? Door->Body : nullptr))
		{
			return false;
		}
		TestTrue(TEXT("the door is locked"), Door->bLocked);
		FElysiumDoorBase* StartOpen = World.FindByName(TEXT("start_open"))->AsDoorBase();
		if (!TestNotNull(TEXT("START_OPEN door resolves"), StartOpen)) return false;
		TestEqual(TEXT("rotating START_OPEN swaps endpoints and starts at state1"), StartOpen->State(), FElysiumDoorBase::EToggleState::AtBottom);
		Sink->Reset();
		StartOpen->InputClose(FElysiumEntityHandle::Invalid());
		TestEqual(TEXT("Close at START_OPEN state1 emits no OnClose"), Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 0);
		TestTrue(TEXT("Close at START_OPEN state1 leaves angular velocity zero"), StartOpen->AngularVelocity == FVector::ZeroVector);
		Door->InputUnlock();
		Door->InputOpen(FElysiumEntityHandle::Invalid());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 2.0);
		Door->InputLock();
		Sink->Reset();
		TestEqual(TEXT("actual Open arrival produces the locked-open state0"), Door->State(),
			FElysiumDoorBase::EToggleState::AtTop);

		// --- +use on a locked OPEN door: stays open, no OnClose, no motion --------------------------
		Door->DoorUse(FElysiumEntityHandle());
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 2.1);
		TestEqual(TEXT("+use does not close a locked open door"), Door->State(),
			FElysiumDoorBase::EToggleState::AtTop);
		TestFalse(TEXT("+use on a locked open door schedules no motion"), Door->IsMoving());
		TestEqual(TEXT("+use on a locked open door fires no OnClose"),
			Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 0);
		TestEqual(TEXT("a door never fires OnLockedUse from an open +use"),
			Sink->CountOf(TEXT("fire"), TEXT("OnLockedUse")), 0);

		// --- direct Close input: retail has no lock check, so it DOES close --------------------------
		Sink->Reset();
		Door->InputClose(FElysiumEntityHandle());
		TestEqual(TEXT("a direct Close on a locked door begins closing (no lock gate)"), Door->State(),
			FElysiumDoorBase::EToggleState::GoingDown);
		TestTrue(TEXT("the direct Close actually issued angular motion"), Door->AngularVelocity != FVector::ZeroVector);
		// Retail fires OnClose twice on an admitted Close: once in InputClose (FUN_100f00a0), once
		// again inside DoorGoDown. Only the +use and Toggle paths fire it once, because neither goes
		// through the input handler.
		TestEqual(TEXT("the direct Close fires OnClose twice — input then motion start"),
			Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 2);

		return true;
	}
	// (a) The direct mapping: closed pose -> AtBottom, open pose -> AtTop, a genuine mid-pose leaves the
	// GoingUp/GoingDown state untouched, a component comfortably inside the 0.001 tolerance snaps and one
	// 0.01 off (an order of magnitude outside) does not. (Pre-fix ResolveToggleStateFromTransform does
	// not exist, so this file does not compile
	// against the pre-fix header — the mapping is new behaviour by construction.)
	else if (Parameters == TEXT("DoorResyncMapping"))
	{
		using namespace ElysiumDoorResyncTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
		if (!Owner)
		{
			return false;
		}

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__door_resync_mapping__");
		Defs.Defs.Add(RotatingDoor(TEXT("door"), 1));

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Entity = World.FindByName(TEXT("door"));
		FElysiumDoorBase* Door = Entity ? Entity->AsDoorBase() : nullptr;
		if (!TestNotNull(TEXT("door resolved"), Door)
			|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr))
		{
			return false;
		}

		const FElysiumEntityHandle NoActivator;
		auto SnapBody = [Door](const FVector& Loc, const FRotator& Rot)
		{
			Door->SetOrigin(Loc);
			Door->SetAngles(Rot);
			Door->Body->SetRelativeLocationAndRotation(Loc,
				ElysiumModelPhysics::SourceRotation(FVector(Rot.Pitch, Rot.Yaw, Rot.Roll)), /*bSweep*/ false);
		};

		// Capture the two real endpoints off the body: closed is the spawn pose, open is where a full
		// swing lands. A rotating door keeps its origin, so location is constant across both.
		const FVector  ClosedLoc = Door->Body->GetRelativeLocation();
		const FVector ClosedWords = Door->GetAngles();
		const FRotator ClosedRot(ClosedWords.X, ClosedWords.Y, ClosedWords.Z);
		Door->InputOpen(NoActivator);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 2.0);   // 90deg at 90deg/s completes in 1s; 2s is margin
		if (!TestEqual(TEXT("the door opened fully"), Door->State(), EToggleState::AtTop))
		{
			return false;
		}
		const FVector  OpenLoc = Door->Body->GetRelativeLocation();
		const FVector OpenWords = Door->GetAngles();
		const FRotator OpenRot(OpenWords.X, OpenWords.Y, OpenWords.Z);
		AddInfo(FString::Printf(TEXT("resync endpoint source=%s native=%s"), *Door->GetAngles().ToString(), *OpenRot.ToString()));

		// 1) Body at the closed pose -> AtBottom (observed as a change away from the current AtTop).
		SnapBody(ClosedLoc, ClosedRot);
		Door->ResolveToggleStateFromTransform();
		TestEqual(TEXT("closed pose resolves to AtBottom"), Door->State(), EToggleState::AtBottom);

		// 2) Body at the open pose -> AtTop.
		SnapBody(OpenLoc, OpenRot);
		AddInfo(FString::Printf(TEXT("resync supplied source=%s"), *Door->GetAngles().ToString()));
		Door->ResolveToggleStateFromTransform();
		TestEqual(TEXT("open pose resolves to AtTop"), Door->State(), EToggleState::AtTop);

		// 3) Body genuinely between the endpoints -> the mid-motion state is left untouched. Establish a
		// clean GoingDown, snap the body to the half-swing, and confirm the resync does not move it.
		Door->InputClose(NoActivator);
		if (!TestEqual(TEXT("InputClose enters GoingDown"), Door->State(), EToggleState::GoingDown))
		{
			return false;
		}
		const FRotator MidRot(
			(ClosedRot.Pitch + OpenRot.Pitch) * 0.5f,
			(ClosedRot.Yaw   + OpenRot.Yaw)   * 0.5f,
			(ClosedRot.Roll  + OpenRot.Roll)  * 0.5f);
		SnapBody(ClosedLoc, MidRot);   // ~45deg from either endpoint
		Door->ResolveToggleStateFromTransform();
		TestEqual(TEXT("a mid-swing pose leaves GoingDown untouched"), Door->State(), EToggleState::GoingDown);

		// 4) A retained Source component inside the inclusive 0.001f band snaps. The
		// body quaternion is a separate native pose and is not the slot221 input.
		SnapBody(ClosedLoc, ClosedRot + FRotator(0.f, 0.0005f, 0.f));
		Door->ResolveToggleStateFromTransform();
		TestEqual(TEXT("a pose inside the resync tolerance snaps to AtBottom"), Door->State(),
			EToggleState::AtBottom);

		// 5) A component 0.01 off the endpoint does NOT snap — the mid-motion state is preserved.
		Door->InputOpen(NoActivator);   // AtBottom -> GoingUp, a clean mid-motion state
		if (!TestEqual(TEXT("InputOpen enters GoingUp"), Door->State(), EToggleState::GoingUp))
		{
			return false;
		}
		SnapBody(ClosedLoc, ClosedRot + FRotator(0.f, 0.01f, 0.f));
		Door->ResolveToggleStateFromTransform();
		TestEqual(TEXT("0.01 off the closed pose does not snap (state stays GoingUp)"), Door->State(),
			EToggleState::GoingUp);

		return true;
	}
	// (b) The live-bug reproduction: a door believing it is AtTop while its body sits at the CLOSED pose
	// (it never physically moved). A +use must resync the belief to AtBottom and then OPEN — firing
	// OnOpen and entering GoingUp. Pre-fix, +use on a stale-AtTop door routes toggle -> InputClose and
	// tries to close (no OnOpen, state GoingDown), which is the dropped-open bug this fixes.
	else if (Parameters == TEXT("DoorStaleAtTopReopens"))
	{
		using namespace ElysiumDoorResyncTests;
		using EToggleState = FElysiumDoorBase::EToggleState;

		AActor* Owner = SpawnMoverOwner(this, EngineWorld);
		if (!Owner)
		{
			return false;
		}

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__door_stale_attop__");
		// An unlocked rotating door with OnOpen wired to a sink counter (LockedDoor with no LOCKED bit).
		Defs.Defs.Add(ElysiumLockedDoorTests::LockedDoor(TEXT("stale"), 1, /*SpawnFlags*/ 0));
		Defs.Defs.Add(ElysiumLockedDoorTests::Counter(TEXT("sink_counter")));

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
		FElysiumOrderedIOSink* Sink = OwnedSink.Get();
		World.AddSink(MoveTemp(OwnedSink));
		World.Activate(0.0);

		FElysiumEntity* Entity = World.FindByName(TEXT("stale"));
		FElysiumDoorBase* Door = Entity ? Entity->AsDoorBase() : nullptr;
		if (!TestNotNull(TEXT("door resolved"), Door)
			|| !TestNotNull(TEXT("door has a body"), Door ? Door->Body : nullptr))
		{
			return false;
		}

		const FElysiumEntityHandle NoActivator;
		const FVector  ClosedLoc = Door->Body->GetRelativeLocation();
		const FRotator ClosedRot = Door->Body->GetRelativeRotation();

		// Drive a real open so the state legitimately reaches AtTop, then force the stale scenario: put
		// the body back at the closed pose without touching the state. Now the door BELIEVES it is open
		// while its body has (as far as anything can observe) never left closed.
		Door->InputOpen(NoActivator);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 2.0);
		if (!TestEqual(TEXT("the door reached AtTop"), Door->State(), EToggleState::AtTop))
		{
			return false;
		}
		Door->SetOrigin(ClosedLoc);
		Door->SetAngles(FRotator(-ClosedRot.Pitch, -ClosedRot.Yaw, ClosedRot.Roll));
		Door->Body->SetRelativeLocationAndRotation(ClosedLoc, ClosedRot, /*bSweep*/ false);
		Sink->Reset();   // discard the OnOpen from the legitimate open above

		// +use on the stale-AtTop door: the resync must snap the belief to AtBottom, then the toggle
		// opens it. Post-fix: OnOpen fires once and the door enters GoingUp. Pre-fix (no resync): the
		// toggle would InputClose a door it thinks is open — GoingDown, no OnOpen — so these fail.
		Door->DoorUse(NoActivator);
		TestEqual(TEXT("stale +use re-opens the door (GoingUp)"), Door->State(), EToggleState::GoingUp);
		TestTrue(TEXT("stale +use issued angular motion"), Door->AngularVelocity != FVector::ZeroVector);
		TestEqual(TEXT("stale +use fires OnOpen exactly once"),
			Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 1);
		TestEqual(TEXT("stale +use fires no OnClose"),
			Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 0);

		return true;
	}
	// CBaseDoor::Use's guard chain: the noopenwanted refusal, the mid-motion self-heal, and the
	// admission set that makes a moving door drop out. Bodiless — every assertion here is about state
	// and outputs, not geometry.
	else if (Parameters == TEXT("DoorUseGuardChain"))
	{
		using EToggleState = FElysiumDoorBase::EToggleState;

		// Embodied: a bodiless mover's BeginMove early-returns, so nothing is ever in flight and no
		// move ever completes. Every assertion below is about a move actually being under way.
		AActor* Owner = ElysiumDoorResyncTests::SpawnMoverOwner(this, EngineWorld);
		if (!Owner)
		{
			return false;
		}

		auto MakeDoor = [](const TCHAR* Name, int32 Model, const FVector& Origin, const TCHAR* SpawnFlags,
			const TCHAR* NoOpenWanted)
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("func_door_rotating");
			Def.TargetName = Name;
			Def.Model = Model;
			Def.Origin = Origin;
			FElysiumConvexHull Hull;
			for (float X : { 0.0f, 100.0f })
			{
				for (float Y : { -5.0f, 5.0f })
				{
					for (float Z : { -100.0f, 100.0f })
					{
						Hull.Vertices.Emplace(Origin.X + X, Origin.Y + Y, Origin.Z + Z);
					}
				}
			}
			Def.Hulls.Add(MoveTemp(Hull));
			Def.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Model));
			Def.Keys.Add(TEXT("spawnflags"), SpawnFlags);
			Def.Keys.Add(TEXT("distance"), TEXT("90"));
			Def.Keys.Add(TEXT("wait"), TEXT("-1"));
			Def.Keys.Add(TEXT("speed"), TEXT("9"));   // 10 s for 90 deg — stays in flight across ticks
			Def.Keys.Add(TEXT("noopenwanted"), NoOpenWanted);
			return Def;
		};

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__door_use_guard_chain__");
		Defs.Defs.Add(MakeDoor(TEXT("wanted_door"), 1, FVector::ZeroVector, TEXT("256"), TEXT("1")));
		Defs.Defs.Add(MakeDoor(TEXT("plain_door"), 2, FVector(500, 0, 0), TEXT("256"), TEXT("0")));
		// PUSE | NO_AUTO_RETURN (0x100 | 0x20)
		Defs.Defs.Add(MakeDoor(TEXT("noreturn_door"), 3, FVector(1000, 0, 0), TEXT("288"), TEXT("0")));

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(Owner, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
		World.Activate(0.0);

		auto Door = [&World](const TCHAR* Name)
		{
			FElysiumEntity* E = World.FindByName(Name);
			return E ? E->AsDoorBase() : nullptr;
		};
		FElysiumDoorBase* Wanted = Door(TEXT("wanted_door"));
		FElysiumDoorBase* Plain = Door(TEXT("plain_door"));
		FElysiumDoorBase* NoReturn = Door(TEXT("noreturn_door"));
		FElysiumPlayer* Player = World.FindPlayer();
		if (!TestNotNull(TEXT("wanted door resolves"), Wanted)
			|| !TestNotNull(TEXT("plain door resolves"), Plain)
			|| !TestNotNull(TEXT("no-return door resolves"), NoReturn)
			|| !TestNotNull(TEXT("player resolves"), Player))
		{
			return false;
		}

		// --- Step 3: noopenwanted refuses only while the player is actually hunted -----------------
		TestEqual(TEXT("the noopenwanted keyfield parsed"), Wanted->bNoOpenWanted, true);
		Player->Police.CopsInPursuit = 0;
		Wanted->DoorUse(PlayerHandle);
		TestEqual(TEXT("noopenwanted does not refuse an unhunted player"), Wanted->State(),
			EToggleState::GoingUp);

		// Tick takes an ABSOLUTE time, so every wait below only moves forward. 90 deg at 9 deg/s is a
		// ten-second swing, which is what keeps a move observably in flight.
		Wanted->InputClose(PlayerHandle);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 30.0);   // let it settle closed
		TestEqual(TEXT("the wanted door is closed again"), Wanted->State(), EToggleState::AtBottom);
		Player->Police.CopsInPursuit = 1;
		Wanted->DoorUse(PlayerHandle);
		TestEqual(TEXT("noopenwanted refuses a hunted player outright"), Wanted->State(),
			EToggleState::AtBottom);
		TestFalse(TEXT("the refused door issued no move"), Wanted->IsMoving());
		Player->Police.CopsInPursuit = 0;

		// --- Steps 4 + 7: an active movement kind skips reissue, then drops out of admission --------
		Plain->DoorUse(PlayerHandle);
		TestEqual(TEXT("the plain door starts opening"), Plain->State(), EToggleState::GoingUp);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 30.5);   // half a second into a ten-second swing
		TestTrue(TEXT("the plain door is still rotating"), Plain->AngularVelocity != FVector::ZeroVector);
		TestFalse(TEXT("slot153 reads only linear velocity during the angular move"), Plain->IsMoving());
		Plain->DoorUse(PlayerHandle);
		// Step 4 skips the active AngularMove; step 5 leaves the mid-travel state alone,
		// then step 7 refuses the toggle.
		TestEqual(TEXT("a +use on a moving door does not reverse it"), Plain->State(),
			EToggleState::GoingUp);
		TestTrue(TEXT("the original move is still in flight"), Plain->AngularVelocity != FVector::ZeroVector);

		// --- Step 7: NO_AUTO_RETURN is admitted mid-motion, so it DOES reverse ----------------------
		NoReturn->DoorUse(PlayerHandle);
		TestEqual(TEXT("the no-return door starts opening"), NoReturn->State(), EToggleState::GoingUp);
		ElysiumDoorCaseTests::AdvanceNativeFrames(World, TestWorld, 31.0);
		TestTrue(TEXT("the no-return door is still rotating"), NoReturn->AngularVelocity != FVector::ZeroVector);
		NoReturn->DoorUse(PlayerHandle);
		TestEqual(TEXT("NO_AUTO_RETURN admits a mid-motion +use and reverses"), NoReturn->State(),
			EToggleState::GoingDown);

		return true;
	}

	AddError(FString::Printf(TEXT("FElysiumDoorCasesTest: unrecognized case parameter '%s'"), *Parameters));
	return false;
}

// The output arity of the three inputs versus the +use path. Retail fires OnOpen/OnClose twice on
// an admitted Open/Close — once at the input, once at the motion start — while Toggle and +use
// reach the motion helpers directly and fire once. Easy to "fix" back by accident.
#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorInputOutputArityTest,
	"Elysium.Arm.DoorInputOutputArity", GElysiumMoverTestFlags)
bool FElysiumDoorInputOutputArityTest::RunTest(const FString&)
{
	auto Counter = [](const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("math_counter");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("min"), TEXT("0"));
		Def.Keys.Add(TEXT("max"), TEXT("100"));
		return Def;
	};

	// Outputs only fire down authored rows, so every door here wires OnOpen and OnClose at a
	// counter. Bodiless is fine — the arity is decided before any move is issued.
	auto ArityDoor = [](const TCHAR* Name)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("func_door");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("spawnflags"), TEXT("256"));
		Def.Keys.Add(TEXT("wait"), TEXT("-1"));
		for (const TCHAR* Output : { TEXT("OnOpen"), TEXT("OnClose") })
		{
			FElysiumOutputDef Row;
			Row.Name = Output;
			Row.Target = TEXT("sink");
			Row.Input = TEXT("Add");
			Row.Param = TEXT("1");
			Row.Times = -1;
			Def.Outputs.Add(MoveTemp(Row));
		}
		return Def;
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_input_output_arity__");
	Defs.Defs.Add(ArityDoor(TEXT("arity_door")));
	// A second leaf, left at rest, so the +use case runs the DoorActivate path rather than the
	// self-heal a mid-motion door would take.
	Defs.Defs.Add(ArityDoor(TEXT("use_door")));
	Defs.Defs.Add(Counter(TEXT("sink")));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
	FElysiumOrderedIOSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));
	World.Activate(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("arity_door"));
	FElysiumDoorBase* Door2 = Entity ? Entity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("door resolves"), Door2))
	{
		return false;
	}

	Door2->InputOpen(PlayerHandle);
	TestEqual(TEXT("an admitted Open fires OnOpen twice — input then motion start"),
		Sink->CountOf(TEXT("fire"), TEXT("OnOpen")), 2);

	// Admission is `!= AtBottom`, so this Close is admitted from GoingUp.
	Sink->Reset();
	Door2->InputClose(PlayerHandle);
	TestEqual(TEXT("an admitted Close fires OnClose twice"),
		Sink->CountOf(TEXT("fire"), TEXT("OnClose")), 2);

	// Toggle reaches DoorGoUp/DoorGoDown directly, so it fires once.
	Sink->Reset();
	Door2->InputToggle(PlayerHandle);
	TestEqual(TEXT("Toggle fires its output exactly once"),
		Sink->CountOf(TEXT("fire"), TEXT("OnOpen")) + Sink->CountOf(TEXT("fire"), TEXT("OnClose")),
		1);

	// +use on a resting leaf likewise bypasses the input handlers.
	FElysiumEntity* UseEntity = World.FindByName(TEXT("use_door"));
	FElysiumDoorBase* UseDoor = UseEntity ? UseEntity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("use door resolves"), UseDoor))
	{
		return false;
	}
	Sink->Reset();
	UseDoor->DoorUse(PlayerHandle);
	TestEqual(TEXT("+use fires its output exactly once"),
		Sink->CountOf(TEXT("fire"), TEXT("OnOpen")) + Sink->CountOf(TEXT("fire"), TEXT("OnClose")),
		1);

	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

namespace ElysiumDoorKnobTests
{
	// A knob whose `difficulty` is non-zero seeds itself locked in FElysiumLockableEntity::Spawn.
	// Whether the DOOR is locked is a separate authority — the whole point of these cases.
	static FElysiumEntityDef Knob(const TCHAR* Name, const TCHAR* Parent, const TCHAR* Difficulty,
		const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("prop_doorknob");
		Def.TargetName = Name;
		Def.Origin = Origin;
		Def.ModelMesh = TEXT("test_knob");
		Def.Keys.Add(TEXT("model"), TEXT("models/test/knob.mdl"));
		Def.Keys.Add(TEXT("parentname"), Parent);
		Def.Keys.Add(TEXT("difficulty"), Difficulty);
		Def.Keys.Add(TEXT("skilltype"), TEXT("1"));
		return Def;
	}

	static FElysiumEntityDef Door(const TCHAR* Name, const TCHAR* SpawnFlags)
	{
		FElysiumEntityDef Def;
		Def.Classname = TEXT("func_door_rotating");
		Def.TargetName = Name;
		Def.Keys.Add(TEXT("spawnflags"), SpawnFlags);
		Def.Keys.Add(TEXT("wait"), TEXT("-1"));
		return Def;
	}
}

// The regression for the tutorial keypad defect: `tutchopdoord` (spawnflags 256, no LOCKED bit)
// carries a prop_doorknob_electronic with `difficulty 1`, and a bare +use opened it because the
// door wrote its own unlocked state over the knob at attach time. Retail reads the other way —
// CBaseDoor::IsUseRefused (FUN_100eec70) consults the nearest knob and only falls back to the
// door's own byte when there is no knob, or no user.
#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorKnobLockAuthorityTest,
	"Elysium.Arm.DoorKnobLockAuthority", GElysiumMoverTestFlags)
bool FElysiumDoorKnobLockAuthorityTest::RunTest(const FString&)
{
	using namespace ElysiumDoorKnobTests;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_knob_authority__");
	// Unlocked door, locked knob — the tutchopdoord shape.
	Defs.Defs.Add(Door(TEXT("keypad_door"), TEXT("256")));
	Defs.Defs.Add(Knob(TEXT("keypad"), TEXT("keypad_door"), TEXT("1")));
	// Locked door, unlocked knob — the mirror. Retail lets the knob win here too.
	Defs.Defs.Add(Door(TEXT("open_knob_door"), TEXT("2304")));
	Defs.Defs.Add(Knob(TEXT("free_knob"), TEXT("open_knob_door"), TEXT("0")));
	// No knob at all: the door's own byte is still the authority.
	Defs.Defs.Add(Door(TEXT("plain_locked_door"), TEXT("2304")));
	// A non-character activator, to stand in for the relay/button/trigger that propagates itself.
	{
		FElysiumEntityDef Relay;
		Relay.Classname = TEXT("math_counter");
		Relay.TargetName = TEXT("not_a_character");
		Relay.Keys.Add(TEXT("min"), TEXT("0"));
		Relay.Keys.Add(TEXT("max"), TEXT("100"));
		Defs.Defs.Add(MoveTemp(Relay));
	}

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 2);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	auto FindDoor = [&World](const TCHAR* Name)
	{
		FElysiumEntity* E = World.FindByName(Name);
		return E ? E->AsDoorBase() : nullptr;
	};
	auto FindKnob = [&World](const TCHAR* Name)
	{
		FElysiumEntity* E = World.FindByName(Name);
		return E ? E->AsLockableEntity() : nullptr;
	};

	FElysiumDoorBase* KeypadDoor = FindDoor(TEXT("keypad_door"));
	FElysiumLockableEntity* Keypad = FindKnob(TEXT("keypad"));
	FElysiumDoorBase* OpenKnobDoor = FindDoor(TEXT("open_knob_door"));
	FElysiumLockableEntity* FreeKnob = FindKnob(TEXT("free_knob"));
	FElysiumDoorBase* PlainDoor = FindDoor(TEXT("plain_locked_door"));
	if (!TestNotNull(TEXT("keypad door resolves"), KeypadDoor)
		|| !TestNotNull(TEXT("keypad knob resolves"), Keypad)
		|| !TestNotNull(TEXT("open-knob door resolves"), OpenKnobDoor)
		|| !TestNotNull(TEXT("free knob resolves"), FreeKnob)
		|| !TestNotNull(TEXT("plain door resolves"), PlainDoor))
	{
		return false;
	}

	// The defect, stated directly: attaching to an unlocked door must not open the knob. Both of
	// these fail before the fix — RegisterDoorknob wrote the door's state over the knob's.
	TestTrue(TEXT("a difficulty knob stays locked on an unlocked door"), Keypad->IsUseLocked());
	TestTrue(TEXT("+use on the slab is refused by the knob"),
		KeypadDoor->IsUseRefused(PlayerHandle));
	// The reticle and the refusal are one predicate, so a knob-gated door cannot draw its unlocked
	// icon over a +use that will be refused.
	TestTrue(TEXT("the reticle reports the same refusal it will apply"), KeypadDoor->IsUseLocked());

	// The mirror: a locked door does not lock an unlocked knob, and the knob still decides. The
	// first fails before the fix; the second is the invariant that makes it meaningful.
	TestFalse(TEXT("a difficulty-0 knob stays unlocked on a locked door"), FreeKnob->IsUseLocked());
	TestTrue(TEXT("the door's own LOCKED byte survives, unread"), OpenKnobDoor->bLocked);
	TestFalse(TEXT("+use defers to the unlocked knob over the door's own locked byte"),
		OpenKnobDoor->IsUseRefused(PlayerHandle));

	// No knob: the door's byte is the authority, exactly as before.
	TestTrue(TEXT("a knobless locked door still refuses"), PlainDoor->IsUseRefused(PlayerHandle));

	// The null-activator fallthrough — GetNearestDoorknob returns null for a null user, so an
	// I/O-driven Open never consults the knob. This is what keeps every authored `Unlock -> door`
	// wire and every script-fired open working after the authority flip.
	const FElysiumEntityHandle NoActivator = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("a script-fired open bypasses the knob"), KeypadDoor->IsUseRefused(NoActivator));

	// Retail's user is the activator's CHARACTER sub-object, null for anything that is not one — so
	// a relay or trigger propagating itself as the activator is also "no user" and bypasses the
	// knob, exactly like a null handle. Without that rule those wires would start being gated.
	FElysiumEntity* Relay = World.FindByName(TEXT("not_a_character"));
	if (!TestNotNull(TEXT("non-character activator resolves"), Relay))
	{
		return false;
	}
	TestNull(TEXT("a math_counter is not a character"),
		reinterpret_cast<const void*>(Relay->AsCombatCharacter()));
	TestFalse(TEXT("a non-character activator bypasses the knob"),
		KeypadDoor->IsUseRefused(Relay->Handle));
	KeypadDoor->InputOpen(NoActivator);
	TestEqual(TEXT("script-fired Open still opens a knob-gated door"), KeypadDoor->State(),
		FElysiumDoorBase::EToggleState::GoingUp);

	// ...while a player-activated Open on the same door is refused.
	OpenKnobDoor->InputOpen(PlayerHandle);   // knob unlocked -> admitted
	TestEqual(TEXT("player Open admitted through an unlocked knob"), OpenKnobDoor->State(),
		FElysiumDoorBase::EToggleState::GoingUp);
	FElysiumDoorBase* SecondKeypad = KeypadDoor;
	SecondKeypad->InputToggle(PlayerHandle);
	TestEqual(TEXT("player Toggle refused by the locked knob leaves the state alone"),
		SecondKeypad->State(), FElysiumDoorBase::EToggleState::GoingUp);

	return true;
}

// Lock/Unlock are per-authority. A knob's Unlock writes the knob; a door's Unlock writes the door.
// Eleven authored wires in the corpus aim `Unlock` straight at a knob, and they must land there.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorKnobUnlockInputsTest,
	"Elysium.Arm.DoorKnobUnlockInputs", GElysiumMoverTestFlags)
bool FElysiumDoorKnobUnlockInputsTest::RunTest(const FString&)
{
	using namespace ElysiumDoorKnobTests;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_knob_unlock__");
	Defs.Defs.Add(Door(TEXT("gate"), TEXT("2304")));         // LOCKED door
	Defs.Defs.Add(Knob(TEXT("gate_knob"), TEXT("gate"), TEXT("5")));   // locked knob

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("gate"));
	FElysiumEntity* KnobEntity = World.FindByName(TEXT("gate_knob"));
	FElysiumDoorBase* LiveDoor = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	FElysiumLockableEntity* LiveKnob = KnobEntity ? KnobEntity->AsLockableEntity() : nullptr;
	if (!TestNotNull(TEXT("door resolves"), LiveDoor) || !TestNotNull(TEXT("knob resolves"), LiveKnob))
	{
		return false;
	}
	TestTrue(TEXT("both authorities start locked"), LiveDoor->bLocked && LiveKnob->IsUseLocked());

	// Unlock the DOOR: its byte clears, the knob does not, and a player +use is still refused.
	LiveDoor->InputUnlock();
	TestFalse(TEXT("door Unlock clears the door byte"), LiveDoor->bLocked);
	TestTrue(TEXT("door Unlock does not reach the knob"), LiveKnob->IsUseLocked());
	TestTrue(TEXT("a locked knob still gates the door after the door unlocks"),
		LiveDoor->IsUseRefused(PlayerHandle));

	// Unlock the KNOB: the authored `Unlock -> knob` wire is what actually opens the way.
	LiveKnob->InputUnlock(PlayerHandle);
	TestFalse(TEXT("knob Unlock clears the knob"), LiveKnob->IsUseLocked());
	TestFalse(TEXT("with both clear the door admits a player +use"),
		LiveDoor->IsUseRefused(PlayerHandle));

	// Re-lock the knob alone and confirm the door byte was not dragged along.
	LiveKnob->InputLock();
	TestTrue(TEXT("knob Lock re-locks only the knob"), LiveKnob->IsUseLocked());
	TestFalse(TEXT("knob Lock leaves the door byte clear"), LiveDoor->bLocked);
	TestTrue(TEXT("the re-locked knob gates again"), LiveDoor->IsUseRefused(PlayerHandle));

	return true;
}

// GetNearestDoorknob (FUN_100ee950): Manhattan distance between world-space centres, ties to the
// second-registered knob. A double-knobbed door asks whichever handle the user is standing at.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDoorNearestKnobSelectionTest,
	"Elysium.Arm.DoorNearestKnobSelection", GElysiumMoverTestFlags)
bool FElysiumDoorNearestKnobSelectionTest::RunTest(const FString&)
{
	using namespace ElysiumDoorKnobTests;

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__door_nearest_knob__");
	// Placed so Manhattan and Euclidean DISAGREE: from the origin the first knob is 100 away by
	// both metrics, while the second is 120 by Manhattan but only ~84.9 by Euclidean. Manhattan
	// must pick `inside`; a Euclidean implementation would pick `outside` and flip every assertion
	// below. Inside handle unlocked, outside locked — the "locked from the street" door.
	Defs.Defs.Add(Door(TEXT("double_door"), TEXT("256")));
	Defs.Defs.Add(Knob(TEXT("inside"), TEXT("double_door"), TEXT("0"), FVector(100.0, 0.0, 0.0)));
	Defs.Defs.Add(Knob(TEXT("outside"), TEXT("double_door"), TEXT("7"), FVector(0.0, 60.0, 60.0)));
	// A third door whose two knobs are exactly equidistant, to pin the tie-break direction.
	Defs.Defs.Add(Door(TEXT("tie_door"), TEXT("256")));
	Defs.Defs.Add(Knob(TEXT("tie_first"), TEXT("tie_door"), TEXT("0"), FVector(0.0, 50.0, 0.0)));
	Defs.Defs.Add(Knob(TEXT("tie_second"), TEXT("tie_door"), TEXT("9"), FVector(0.0, -50.0, 0.0)));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("its attachment body is unavailable"),
		EAutomationExpectedErrorFlags::Contains, 4);
	World.Load(MoveTemp(Defs));
	const FElysiumEntityHandle PlayerHandle = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntity* DoorEntity = World.FindByName(TEXT("double_door"));
	FElysiumDoorBase* LiveDoor = DoorEntity ? DoorEntity->AsDoorBase() : nullptr;
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("door resolves"), LiveDoor) || !TestNotNull(TEXT("player resolves"), Player))
	{
		return false;
	}

	// Manhattan: inside = 100, outside = 0 + 60 + 60 = 120 -> inside wins.
	// Euclidean:  inside = 100, outside = sqrt(60^2 + 60^2) ~= 84.9 -> outside would win.
	Player->Origin = FVector::ZeroVector;
	const FElysiumLockableEntity* Near = LiveDoor->FindNearestDoorknob(PlayerHandle);
	if (!TestNotNull(TEXT("a knob is selected"), Near))
	{
		return false;
	}
	TestEqual(TEXT("the Manhattan-nearest knob is chosen, not the Euclidean-nearest"),
		Near->TargetName, FString(TEXT("inside")));
	TestFalse(TEXT("that unlocked handle admits the +use"), LiveDoor->IsUseRefused(PlayerHandle));

	// Standing on the far side, the locked handle is now nearest and the same door refuses.
	Player->Origin = FVector(0.0, 60.0, 60.0);
	const FElysiumLockableEntity* FarSide = LiveDoor->FindNearestDoorknob(PlayerHandle);
	if (!TestNotNull(TEXT("a knob is selected from the far side"), FarSide))
	{
		return false;
	}
	TestEqual(TEXT("the other handle is selected from the other side"), FarSide->TargetName,
		FString(TEXT("outside")));
	TestTrue(TEXT("the locked handle refuses the same door"), LiveDoor->IsUseRefused(PlayerHandle));

	// An exact draw goes to the second-registered knob (retail returns the 0x62c handle when its
	// distance is <= the 0x628 handle's). Here that knob is the locked one, so the tie is visible.
	FElysiumEntity* TieEntity = World.FindByName(TEXT("tie_door"));
	FElysiumDoorBase* TieDoor = TieEntity ? TieEntity->AsDoorBase() : nullptr;
	if (!TestNotNull(TEXT("tie door resolves"), TieDoor))
	{
		return false;
	}
	Player->Origin = FVector::ZeroVector;   // exactly 50 from each handle
	const FElysiumLockableEntity* Tie = TieDoor->FindNearestDoorknob(PlayerHandle);
	if (!TestNotNull(TEXT("a knob is selected on an exact tie"), Tie))
	{
		return false;
	}
	TestEqual(TEXT("an exact tie goes to the second-registered knob"), Tie->TargetName,
		FString(TEXT("tie_second")));

	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

#endif // WITH_DEV_AUTOMATION_TESTS
