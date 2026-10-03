#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Components/BoxComponent.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumCollisionChannels.h"
#include "ElysiumContentsSignature.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Engine/World.h"
#include "Map/ElysiumRetailMaskRecipe.h"
#include "Map/ElysiumWorldGeometry.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "Visual/ElysiumNpcBody.h"

// 0018 story 6: `ElysiumWorldGeometry::Trace` against hand-placed bodies in a transient world, one
// fact per case -- the centre offset, contact is not start-solid, characters listed and never
// folded, movers only under MOVEABLE, entity props only under MONSTER while static props always.

namespace ElysiumWorldGeometryTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask
	| EAutomationTestFlags::ProductFilter;

// The masks the cases ask, by what they are.
constexpr int32 NpcMask = 0x2400b;          // MONSTERCLIP + MOVEABLE, no MONSTER
constexpr int32 NpcSolidMask = 0x202400b;   // MASK_NPCSOLID: the same plus MONSTER
constexpr int32 InitLinksMask = 0x2000b;    // no MOVEABLE, no MONSTER
constexpr int32 SightMask = 0x2804091;      // FVisible
constexpr int32 LosMask = 0x4091;           // SetPlayerLOS: sight channel, no MONSTER

// Source's DIST_EPSILON, the contact tolerance `TraceWorld` names.
constexpr double DistEpsilonCm = 0.03125 * ElysiumMove::U;

// A static box, blocking every channel, optionally wearing a profile and a mask bit.
UBoxComponent* AddBox(UWorld* World, const FVector& Centre, const FVector& Extent,
	FName Profile = NAME_None, uint8 MaskBit = 0)
{
	AActor* Actor = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Box);
	Box->InitBoxExtent(Extent);
	Box->SetWorldLocation(Centre);
	if (Profile.IsNone())
	{
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
	}
	else
	{
		Box->SetCollisionProfileName(Profile);
	}
	if (MaskBit != 0)
	{
		Box->SetMaskFilterOnBodyInstance(MaskBit);
	}
	Box->RegisterComponent();
	Actor->AddInstanceComponent(Box);
	return Box;
}

// A floor whose top is z = 0.
void AddFloor(UWorld* World)
{
	AddBox(World, FVector(0, 0, -50), FVector(1000, 1000, 50));
}

FElysiumRetailTraceResult Trace(UWorld* World, const FElysiumRetailTrace& Request,
	TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
{
	FElysiumRetailTraceResult Out;
	ElysiumWorldGeometry::Trace(*World, Request, Out, nullptr, ToHandle);
	return Out;
}

FElysiumRetailTraceResult Trace(UWorld* World, const FElysiumRetailTrace& Request)
{
	return Trace(World, Request, [](const AActor*) { return FElysiumEntityHandle::Invalid(); });
}

FElysiumRetailTrace Line(const FVector& StartCm, const FVector& EndCm, int32 Mask)
{
	FElysiumRetailTrace Request;
	Request.StartCm = StartCm;
	Request.EndCm = EndCm;
	Request.RetailMask = Mask;
	return Request;
}

// Retail HUMAN_HULL, (-13,-13,0)..(13,13,72) units, in centimetres.
FElysiumRetailTrace HumanBox(const FVector& StartCm, const FVector& EndCm, int32 Mask)
{
	FElysiumRetailTrace Request = Line(StartCm, EndCm, Mask);
	Request.MinsCm = FVector(-13, -13, 0) * ElysiumMove::U;
	Request.MaxsCm = FVector(13, 13, 72) * ElysiumMove::U;
	return Request;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldGeometryCentreOffsetTest,
	"Elysium.Arm.Geometry.World.CentreOffset", ElysiumWorldGeometryTests::Flags)
bool FElysiumWorldGeometryCentreOffsetTest::RunTest(const FString&)
{
	using namespace ElysiumWorldGeometryTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AddFloor(Fixture.World);

	// The box is relative to the traced point: with mins.z = 0 the point itself comes down onto the
	// floor, not the box's centre. Source stops DIST_EPSILON short, and so does the seam.
	const FElysiumRetailTraceResult Feet = Trace(Fixture.World,
		HumanBox(FVector(0, 0, 100), FVector(0, 0, -100), NpcMask));
	TestTrue(TEXT("the dropped box lands"), Feet.Fraction < 1.f);
	TestFalse(TEXT("a box dropped from above does not start in solid"), Feet.bStartSolid);
	TestNearlyEqual(TEXT("mins.z = 0: the traced point stops on the floor top"),
		Feet.EndPosCm.Z, DistEpsilonCm, 0.05);
	TestNearlyEqual(TEXT("the reported point keeps its own x/y"), Feet.EndPosCm.X, 0.0, 0.01);
	TestNearlyEqual(TEXT("fraction and endpos agree"), static_cast<double>(Feet.Fraction),
		(100.0 - Feet.EndPosCm.Z) / 200.0, 0.001);

	// A box that hangs below its point stops higher by exactly that much.
	FElysiumRetailTrace Hanging = Line(FVector(0, 0, 200), FVector(0, 0, -100), NpcMask);
	Hanging.MinsCm = FVector(-10, -10, -50);
	Hanging.MaxsCm = FVector(10, 10, 20);
	const FElysiumRetailTraceResult Hung = Trace(Fixture.World, Hanging);
	TestNearlyEqual(TEXT("mins.z = -50: the traced point stops 50 cm above the floor"),
		Hung.EndPosCm.Z, 50.0 + DistEpsilonCm, 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldGeometryContactTest,
	"Elysium.Arm.Geometry.World.ContactIsNotSolid", ElysiumWorldGeometryTests::Flags)
bool FElysiumWorldGeometryContactTest::RunTest(const FString&)
{
	using namespace ElysiumWorldGeometryTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AddFloor(Fixture.World);

	for (const double Lift : { 0.0, DistEpsilonCm })
	{
		const FVector Feet(0, 0, Lift);
		// `IsValidCover`'s shape: 0.01 unit up from the feet.
		const FElysiumRetailTraceResult Cover = Trace(Fixture.World,
			HumanBox(Feet, Feet + FVector(0, 0, 0.01 * ElysiumMove::U), NpcSolidMask));
		TestFalse(*FString::Printf(TEXT("a box resting %.4f cm above the floor top is not start-solid"),
			Lift), Cover.bStartSolid);
		// `IsAreaClear`'s shape: start == end.
		const FElysiumRetailTraceResult Area = Trace(Fixture.World, HumanBox(Feet, Feet, NpcSolidMask));
		TestFalse(*FString::Printf(TEXT("an overlap %.4f cm above the floor top is clear"), Lift),
			Area.bStartSolid || Area.bAllSolid);
		TestEqual(TEXT("an overlap reads fraction 1"), Area.Fraction, 1.f);
	}

	// The control: a box sunk into the floor does start in solid, both shapes.
	const FVector Sunk(0, 0, -5);
	TestTrue(TEXT("a box 5 cm into the floor starts in solid"), Trace(Fixture.World,
		HumanBox(Sunk, Sunk + FVector(0, 0, 0.01 * ElysiumMove::U), NpcSolidMask)).bStartSolid);
	const FElysiumRetailTraceResult Buried = Trace(Fixture.World, HumanBox(Sunk, Sunk, NpcSolidMask));
	TestTrue(TEXT("an overlap 5 cm into the floor is start- and all-solid"),
		Buried.bStartSolid && Buried.bAllSolid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldGeometryCharacterTest,
	"Elysium.Arm.Geometry.World.CharacterListedNotFolded", ElysiumWorldGeometryTests::Flags)
bool FElysiumWorldGeometryCharacterTest::RunTest(const FString&)
{
	using namespace ElysiumWorldGeometryTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AddFloor(Fixture.World);
	AElysiumNpcBody* Body = Fixture.World->SpawnActor<AElysiumNpcBody>();
	if (!TestNotNull(TEXT("the NPC body spawns"), Body)) return false;
	Body->InitializeAtFeet(FVector(200, 0, 0), 0.f);
	// A body's collision is off until the runtime marks it ready (`ApplyCollisionState`).
	Body->SetRuntimeReady(true);
	const FElysiumEntityHandle Npc(7, 1);
	const auto ToHandle = [Body, Npc](const AActor* Actor)
	{
		return Actor == Body ? Npc : FElysiumEntityHandle::Invalid();
	};

	// Waist height, straight through the capsule.
	const FElysiumRetailTraceResult Seen = Trace(Fixture.World,
		Line(FVector(0, 0, 90), FVector(400, 0, 90), SightMask), ToHandle);
	TestEqual(TEXT("the character is not folded into the world answer"), Seen.Fraction, 1.f);
	TestFalse(TEXT("the world answer names no entity"), Seen.HitEntity.IsSet());
	if (TestEqual(TEXT("MONSTER lists the one character met"), Seen.Characters.Num(), 1))
	{
		TestEqual(TEXT("it is the NPC's entity"), Seen.Characters[0].Entity, Npc);
		TestTrue(TEXT("met on the near side of the capsule"),
			Seen.Characters[0].Fraction > 0.4f && Seen.Characters[0].Fraction < 0.5f);
		TestFalse(TEXT("the ray does not start inside it"), Seen.Characters[0].bStartSolid);
	}

	const FElysiumRetailTraceResult NoMonster = Trace(Fixture.World,
		Line(FVector(0, 0, 90), FVector(400, 0, 90), NpcMask), ToHandle);
	TestEqual(TEXT("without MONSTER the world answer still passes the character"),
		NoMonster.Fraction, 1.f);
	TestTrue(TEXT("without MONSTER no character is listed"), NoMonster.Characters.IsEmpty());

	FElysiumRetailTrace Ignoring = Line(FVector(0, 0, 90), FVector(400, 0, 90), SightMask);
	Ignoring.Ignore.Add(Npc);
	TestTrue(TEXT("an ignored character is not listed"),
		Trace(Fixture.World, Ignoring, ToHandle).Characters.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldGeometryMoverTest,
	"Elysium.Arm.Geometry.World.MoverNeedsMoveable", ElysiumWorldGeometryTests::Flags)
bool FElysiumWorldGeometryMoverTest::RunTest(const FString&)
{
	using namespace ElysiumWorldGeometryTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;

	// A solid brush entity's body, built as the entity world builds one.
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
	const FElysiumEntityHandle Door(40, 1);
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	UElysiumBrushComponent* Slab = NewObject<UElysiumBrushComponent>(Owner, TEXT("DoorSlab"));
	Slab->InitBrush(Door, { SlabHull }, EElysiumBrushSolidity::Solid,
		static_cast<uint8>(ElysiumContents::SignatureOf(0x1)));
	Owner->SetRootComponent(Slab);
	Slab->SetWorldLocation(FVector(200, 0, 50));
	Slab->RegisterComponent();
	Owner->AddInstanceComponent(Slab);

	const FElysiumRetailTraceResult Skipped = Trace(Fixture.World,
		Line(FVector(0, 0, 50), FVector(400, 0, 50), InitLinksMask));
	TestEqual(TEXT("without MOVEABLE the mover is not met"), Skipped.Fraction, 1.f);

	const FElysiumRetailTraceResult Met = Trace(Fixture.World,
		Line(FVector(0, 0, 50), FVector(400, 0, 50), NpcMask));
	TestTrue(TEXT("with MOVEABLE the mover is met"), Met.Fraction < 1.f);
	TestEqual(TEXT("the hit names the brush entity by its component"), Met.HitEntity, Door);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldGeometryPropTest,
	"Elysium.Arm.Geometry.World.PropNeedsMonster", ElysiumWorldGeometryTests::Flags)
bool FElysiumWorldGeometryPropTest::RunTest(const FString&)
{
	using namespace ElysiumWorldGeometryTests;
	const FVector From(0, 0, 50);
	const FVector To(400, 0, 50);
	{
		// An entity prop's body, dressed as `ElysiumPropTraceBody` dresses one.
		FPlayerWorldFixture Fixture;
		if (!Fixture.CreateWorld(*this)) return false;
		UBoxComponent* Prop = AddBox(Fixture.World, FVector(200, 0, 50), FVector(10, 50, 50),
			FName(TEXT("PhysicsActor")), ElysiumRetailMask::PropMaskBit);
		Prop->SetCollisionResponseToChannel(ElysiumCollision::SightChannel, ECR_Block);
		TestEqual(TEXT("an entity prop is not met without MONSTER"),
			Trace(Fixture.World, Line(From, To, NpcMask)).Fraction, 1.f);
		TestEqual(TEXT("nor by the MONSTER-less sight mask"),
			Trace(Fixture.World, Line(From, To, LosMask)).Fraction, 1.f);
		TestTrue(TEXT("an entity prop is met under MASK_NPCSOLID"),
			Trace(Fixture.World, Line(From, To, NpcSolidMask)).Fraction < 1.f);
		TestTrue(TEXT("an entity prop stops FVisible's 0x2804091"),
			Trace(Fixture.World, Line(From, To, SightMask)).Fraction < 1.f);
	}
	{
		// A static (GAME_LUMP) prop: the baked profile, no mask bit. Met under every mask.
		FPlayerWorldFixture Fixture;
		if (!Fixture.CreateWorld(*this)) return false;
		AddBox(Fixture.World, FVector(200, 0, 50), FVector(10, 50, 50), FName(TEXT("ElysiumPropSolid")));
		TestTrue(TEXT("a static prop is met without MONSTER"),
			Trace(Fixture.World, Line(From, To, NpcMask)).Fraction < 1.f);
		TestTrue(TEXT("a static prop stops the MONSTER-less sight mask"),
			Trace(Fixture.World, Line(From, To, LosMask)).Fraction < 1.f);
		TestTrue(TEXT("a static prop stops FVisible's 0x2804091"),
			Trace(Fixture.World, Line(From, To, SightMask)).Fraction < 1.f);
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
