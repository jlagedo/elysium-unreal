// A1 (footsteps) — the ground-surface sense: what the two locomotion producers publish about the
// floor under the body, and what the doubles carry so a Substrate case can pin it.
//
// The retail fact being ported is the cached `surfacedata_t*` both producers keep — `CAI_BaseNPC +0x5b90`, written
// only by `CAI_Navigator::MoveEnact 0x102ef870` and cleared by `0x10273390` / `0x1027bf50`, and
// `CGameMovement`'s own, written by `CategorizePosition` and read as a `gamematerial` letter by
// `UpdateStepSound 0x1011e940`.
//
// Content-free. Both physical materials are transient `UElysiumPhysicalMaterial` objects rather
// than the baked `/ElysiumBaked/SurfaceProperties/PM_concrete`: the sense reads `SourceName` off
// whatever object the hit carries, so standing one up here proves the same rule without needing an
// export corpus. That the baked assets exist and carry the right names is the surface-table lane's
// `Elysium.Content.SurfaceSoundTable`.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumGroundSurface.h"
#include "ElysiumLocomotionSample.h"
#include "ElysiumMovementComponent.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPawn.h"
#include "ElysiumPhysicalMaterial.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumNpcBody.h"

#include "Components/BoxComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Tests/AutomationCommon.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ElysiumFootstepSenseTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static const FName ConcreteName(TEXT("concrete"));
static const FName DefaultName(TEXT("default"));

// One floor slab, blocking everything — the pawn channel a body stands on AND the render-surface
// channel the surfaceprop rides. A converted map splits those across two components (the
// material-less `.hulls` collider and the drawn `ElysiumPickOnly` geometry); one box wearing both
// is the simplest world in which the sense's answer is unambiguous.
static UBoxComponent* SpawnFloor(UWorld* World, const FVector& TopCentre,
	UPhysicalMaterial* Surface, const TCHAR* Name)
{
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (Owner == nullptr)
	{
		return nullptr;
	}
	UBoxComponent* Floor = NewObject<UBoxComponent>(Owner, Name);
	Owner->SetRootComponent(Floor);
	Floor->InitBoxExtent(FVector(400.f, 400.f, 50.f));
	Floor->SetWorldLocation(TopCentre - FVector(0.f, 0.f, 50.f));
	Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Floor->SetCollisionObjectType(ECC_WorldStatic);
	Floor->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->RegisterComponent();
	Owner->AddInstanceComponent(Floor);
	// After registration: the override reaches the live body's shapes, which is where a query with
	// `bReturnPhysicalMaterial` reads it from (the same ordering `Elysium.Substrate.ShotImpactDecal`
	// documents). A null surface leaves the engine's own default material, which is the
	// "unmaterialed floor" arm of the sense's second rule.
	if (Surface != nullptr)
	{
		Floor->SetPhysMaterialOverride(Surface);
	}
	return Floor;
}

// -------------------------------------------------------------------------- the value half

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepGroundSurfaceTest,
	"Elysium.Substrate.Footsteps.GroundSurface", GElysiumTestFlags)
bool FElysiumFootstepGroundSurfaceTest::RunTest(const FString&)
{
	// The sample's own default IS the contract: a record nobody filled names no surface, which is
	// retail's null `surfacedata_t` and a silent step — never `default`, which is a real surface.
	const FElysiumLocomotionSample Fresh;
	TestTrue(TEXT("a default-constructed sample names no ground surface"),
		Fresh.GroundSurface.IsNone());

	// `FName` comparison is case-insensitive, which is what lets a table entry spelled `Kitchen_Pan`
	// answer a consumer asking for `kitchen_pan` without the sense lower-casing anything.
	TestTrue(TEXT("a surface name compares without regard to the table's spelling"),
		FName(TEXT("Concrete")) == ConcreteName);

	FElysiumRecordingServices Services;
	IElysiumEmbodiment& Embodiment = Services;

	// The stated headless answer, and the reason it is a bool rather than a returned sample: `Out`
	// is left exactly as the caller had it, so a world with no body cannot be mistaken for a body
	// standing still on no surface.
	FElysiumLocomotionSample Out;
	Out.GroundSurface = FName(TEXT("sentinel"));
	TestFalse(TEXT("a world with no player body publishes no locomotion record"),
		Embodiment.SamplePlayerLocomotion(Out));
	TestEqual(TEXT("...and leaves the caller's record untouched"), Out.GroundSurface,
		FName(TEXT("sentinel")));

	FElysiumLocomotionSample Published;
	Published.LocalVelocity = FVector(180.f, 0.f, 0.f);
	Published.bOnGround = true;
	Published.GroundSurface = ConcreteName;
	Services.PlayerLocomotion = Published;

	Out = FElysiumLocomotionSample();
	TestTrue(TEXT("a stood player body publishes its record"),
		Embodiment.SamplePlayerLocomotion(Out));
	TestEqual(TEXT("...carrying the surface under its foot"), Out.GroundSurface, ConcreteName);
	TestTrue(TEXT("...and the rest of the record with it"),
		Out.bOnGround && FMath::IsNearlyEqual(Out.Speed2D(), 180.f, 0.1f));
	TestTrue(TEXT("the query is recorded like every other service call"),
		Services.Saw(TEXT("SamplePlayerLocomotion")));

	// The cast's half of the same contract. The motor double answers a body standing still at the
	// yaw it was placed at; the surface is the one field a footstep case has to be able to set.
	FElysiumRecordingNpcMotor Motor;
	TestTrue(TEXT("a motor that has never travelled names no surface"),
		Motor.SampleLocomotion().GroundSurface.IsNone());
	Motor.GroundSurface = ConcreteName;
	TestEqual(TEXT("a motor's published sample carries the surface its last move step left"),
		Motor.SampleLocomotion().GroundSurface, ConcreteName);

	// The rule half, over hits the world never had to produce.
	FHitResult Miss;
	TestTrue(TEXT("a query that hit nothing names no surface"),
		ElysiumGroundSurface::SurfaceNameFor(Miss).IsNone());

	TStrongObjectPtr<UElysiumPhysicalMaterial> Concrete(
		NewObject<UElysiumPhysicalMaterial>(GetTransientPackage()));
	Concrete->SourceName = TEXT("concrete");
	TStrongObjectPtr<UElysiumPhysicalMaterial> Unnamed(
		NewObject<UElysiumPhysicalMaterial>(GetTransientPackage()));
	TStrongObjectPtr<UPhysicalMaterial> Stock(
		NewObject<UPhysicalMaterial>(GetTransientPackage()));

	FHitResult Floor;
	Floor.bBlockingHit = true;
	Floor.PhysMaterial = Concrete.Get();
	TestEqual(TEXT("a floor carrying a surface property names it"),
		ElysiumGroundSurface::SurfaceNameFor(Floor), ConcreteName);
	Floor.PhysMaterial = Stock.Get();
	TestEqual(TEXT("a floor wearing the engine's own material is Source's index 0"),
		ElysiumGroundSurface::SurfaceNameFor(Floor), DefaultName);
	Floor.PhysMaterial = Unnamed.Get();
	TestEqual(TEXT("...as is one whose surface property names no entry"),
		ElysiumGroundSurface::SurfaceNameFor(Floor), DefaultName);
	Floor.PhysMaterial.Reset();
	TestEqual(TEXT("...and one carrying no material at all"),
		ElysiumGroundSurface::SurfaceNameFor(Floor), DefaultName);

	TestEqual(TEXT("the default surface is spelled as the table spells index 0"),
		ElysiumGroundSurface::DefaultSurface(), DefaultName);

	return !HasAnyErrors();
}

// -------------------------------------------------------------------------- the world half

// The two producers against real geometry. Named under `Elysium.Substrate.` like every other
// world-backed case in this module (`Elysium.Substrate.NpcStandingGround` spawns the same slab and
// the same body): the tiers `uv run elysium test` accepts are `substrate`, `content` and `policy`,
// so a case under any other root would never be selected by the wave gate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepGroundSurfaceWorldTest,
	"Elysium.Substrate.Footsteps.GroundSurfaceWorld", GElysiumTestFlags)
bool FElysiumFootstepGroundSurfaceWorldTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	if (!TestNotNull(TEXT("transient game world exists"), World))
	{
		return false;
	}

	TStrongObjectPtr<UElysiumPhysicalMaterial> Concrete(
		NewObject<UElysiumPhysicalMaterial>(GetTransientPackage()));
	Concrete->SourceName = TEXT("concrete");

	// Three places to stand: a floor that names its surface, a floor that names none, and nothing.
	UBoxComponent* Paved = SpawnFloor(World, FVector::ZeroVector, Concrete.Get(), TEXT("PavedFloor"));
	UBoxComponent* Plain = SpawnFloor(World, FVector(2000.f, 0.f, 0.f), nullptr, TEXT("PlainFloor"));
	if (!TestNotNull(TEXT("a floor with a surface property"), Paved)
		|| !TestNotNull(TEXT("a floor without one"), Plain))
	{
		return false;
	}
	// One tick pushes the material overrides into the solver's external query list, which is what a
	// scene query resolves a shape handle against; without it a hit reports no material at all.
	TestWorld.TickTestWorld();

	// --- the probe itself
	TestEqual(TEXT("a probe over a paved floor reads its surface property"),
		ElysiumGroundSurface::TraceBelow(World, FVector(0.f, 0.f, 40.f), 100.f, nullptr),
		ConcreteName);
	TestEqual(TEXT("a probe over an unmaterialed floor reads Source's index 0"),
		ElysiumGroundSurface::TraceBelow(World, FVector(2000.f, 0.f, 40.f), 100.f, nullptr),
		DefaultName);
	TestTrue(TEXT("a probe over nothing reads no surface"),
		ElysiumGroundSurface::TraceBelow(World, FVector(6000.f, 0.f, 40.f), 100.f, nullptr).IsNone());

	// --- the composition a producer with a floor hit uses
	FHitResult Nowhere;
	Nowhere.bBlockingHit = true;
	Nowhere.ImpactPoint = FVector(6000.f, 0.f, 0.f);
	Nowhere.PhysMaterial = Concrete.Get();
	TestEqual(TEXT("a floor hit that already carries the surface needs no probe"),
		ElysiumGroundSurface::AtFloorHit(World, Nowhere, nullptr), ConcreteName);
	Nowhere.PhysMaterial.Reset();
	TestEqual(TEXT("a floor hit with nothing drawn under it is index 0, never silence"),
		ElysiumGroundSurface::AtFloorHit(World, Nowhere, nullptr), DefaultName);

	FHitResult OverPaved;
	OverPaved.bBlockingHit = true;
	OverPaved.ImpactPoint = FVector::ZeroVector;
	TestEqual(TEXT("a material-less floor hit takes its surface off the drawn half"),
		ElysiumGroundSurface::AtFloorHit(World, OverPaved, nullptr), ConcreteName);

	FHitResult NoFloor;
	TestTrue(TEXT("a query that found no floor at all is silence, not index 0"),
		ElysiumGroundSurface::AtFloorHit(World, NoFloor, nullptr).IsNone());

	// --- producer A: the player's mover, publishing off the ground trace it already runs
	// Two centimetres clear of the slab, so the 2u sweep travels rather than starting in contact.
	AElysiumPawn* Pawn = World->SpawnActor<AElysiumPawn>(
		FVector(0.f, 0.f, 2.f + ElysiumMove::StandHeight * 0.5f), FRotator::ZeroRotator);
	UElysiumMovementComponent* Move = Pawn
		? Pawn->FindComponentByClass<UElysiumMovementComponent>() : nullptr;
	if (!TestNotNull(TEXT("player body spawned"), Pawn)
		|| !TestNotNull(TEXT("player body owns the ported mover"), Move))
	{
		return false;
	}
	Move->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("the player body settles on the paved floor"),
		Move->GetLocomotionSample().bOnGround);
	TestEqual(TEXT("...and publishes the surface under its foot"),
		Move->GetLocomotionSample().GroundSurface, ConcreteName);

	// Off the world entirely: no floor, no surface. This is retail's null `surfacedata_t`, which
	// `0x1026d460` returns silently on and the player's clock has no `gamematerial` letter for.
	Pawn->SetActorLocation(FVector(6000.f, 0.f, 600.f), /*bSweep*/ false);
	Move->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
	TestFalse(TEXT("a player body over nothing is not grounded"),
		Move->GetLocomotionSample().bOnGround);
	TestTrue(TEXT("...and names no surface"),
		Move->GetLocomotionSample().GroundSurface.IsNone());

	// --- producer B: the cast body's own trace
	AElysiumNpcBody* Body = World->SpawnActor<AElysiumNpcBody>();
	if (!TestNotNull(TEXT("cast body spawned"), Body))
	{
		return false;
	}
	Body->InitializeAtFeet(FVector::ZeroVector, 0.f);
	Body->SetRuntimeReady(true);
	TestTrue(TEXT("a cast body that has never travelled names no surface"),
		Body->SampleLocomotion().GroundSurface.IsNone());

	Body->RefreshGroundSurface();
	TestEqual(TEXT("one move step's refresh publishes the paved floor"),
		Body->SampleLocomotion().GroundSurface, ConcreteName);

	// Retail's cache survives the end of a leg and a teleport: `+0x5b90` is cleared only by
	// `NPCInit 0x10273390` / `OnRestore 0x1027bf50` and rewritten by the next `MoveEnact`. So an
	// arrival keeps the floor it stopped on, and a teleport answers the floor it left until the
	// body's first move step on the new one.
	Body->Stop();
	TestEqual(TEXT("stopping a cast body keeps its cached surface"),
		Body->SampleLocomotion().GroundSurface, ConcreteName);

	Body->Teleport(FVector(2000.f, 0.f, 0.f), 0.f);
	TestEqual(TEXT("a teleport keeps it too, until the body has traced somewhere new"),
		Body->SampleLocomotion().GroundSurface, ConcreteName);
	Body->RefreshGroundSurface();
	TestEqual(TEXT("...and the first move step on the unmaterialed floor is index 0"),
		Body->SampleLocomotion().GroundSurface, DefaultName);

	// The spawn is the one clear, standing for retail's two.
	Body->InitializeAtFeet(FVector(2000.f, 0.f, 0.f), 0.f);
	TestTrue(TEXT("re-initialising a cast body clears its cached surface"),
		Body->SampleLocomotion().GroundSurface.IsNone());

	// Well clear of the player body parked above, so "nothing under the feet" is the world's answer
	// and not a capsule resting inside another hull.
	Body->Teleport(FVector(9000.f, 0.f, 600.f), 0.f);
	Body->RefreshGroundSurface();
	TestTrue(TEXT("a cast body standing on nothing names no surface"),
		Body->SampleLocomotion().GroundSurface.IsNone());

	Body->SetEnabled(false);
	TestTrue(TEXT("disabling a cast body leaves it naming no surface"),
		Body->SampleLocomotion().GroundSurface.IsNone());

	return !HasAnyErrors();
}

} // namespace ElysiumFootstepSenseTests

#endif // WITH_DEV_AUTOMATION_TESTS
