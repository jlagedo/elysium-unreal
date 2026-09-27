// `CNPCMaker`'s two world questions against real Unreal geometry: the ground cache of `MakeNPC`
// `0x1034b7b0` and the spawn box of `CanMakeNPC` `0x1034b580` (`Map/ElysiumNpcMakerGeometry.h`).
// Found by the story-5 commit-A map smoke: a maker standing exactly on its floor cached the ray's
// END (the Unreal trace ignored the surface it started on), and the box floated at that ground
// counted every body as occupying it.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Map/ElysiumNpcMakerGeometry.h"
#include "Substrate/ElysiumNpc.h"
#include "Tests/ElysiumNpcTestFixture.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumNpcMakerGeometryFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One floor slab whose TOP is at `TopZ`, blocking every channel.
	UBoxComponent* SpawnMakerFloor(UWorld* World, float TopZ)
	{
		AActor* Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
		if (Owner == nullptr)
		{
			return nullptr;
		}
		UBoxComponent* Floor = NewObject<UBoxComponent>(Owner, TEXT("MakerFloor"));
		Owner->SetRootComponent(Floor);
		Floor->InitBoxExtent(FVector(400.f, 400.f, 50.f));
		Floor->SetWorldLocation(FVector(0.f, 0.f, TopZ - 50.f));
		Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Floor->SetCollisionObjectType(ECC_WorldStatic);
		Floor->SetCollisionResponseToAllChannels(ECR_Block);
		Floor->RegisterComponent();
		Owner->AddInstanceComponent(Floor);
		return Floor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerGroundCacheTest,
	"Elysium.Substrate.NpcMakerGeometry.GroundCache", GElysiumNpcMakerGeometryFlags)
bool FElysiumNpcMakerGroundCacheTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	const float FloorTop = -96.52f;   // `thug_maker`'s own Z, `sp_tutorial_1`
	if (!TestNotNull(TEXT("a floor slab"), SpawnMakerFloor(World, FloorTop)))
	{
		return false;
	}
	TestWorld.TickTestWorld();
	const float Depth = 2048.0f * ElysiumMove::U;   // `_DAT_1046bacc`

	// A maker placed EXACTLY on the floor: Source's ray starts on the surface and hits at fraction 0,
	// so `m_flGround = tr.endpos.z` is the maker's own Z (the smoke's `thug_maker` cached -5298.44).
	TestEqual(TEXT("a maker exactly on the floor caches its own Z"),
		ElysiumNpcMakerGeometry::ResolveGroundZ(World, FVector(0.f, 0.f, FloorTop), Depth, nullptr),
		FloorTop, 1e-3f);
	// One 19 cm above the floor (`disc3_maker`) caches the floor.
	TestEqual(TEXT("a maker 19 cm above the floor caches the floor"),
		ElysiumNpcMakerGeometry::ResolveGroundZ(World, FVector(0.f, 0.f, FloorTop + 19.f), Depth, nullptr),
		FloorTop, 1e-3f);
	// A maker with no floor within 2048 units caches the ray's end.
	TestEqual(TEXT("no floor within 2048 units: the ray's end"),
		ElysiumNpcMakerGeometry::ResolveGroundZ(World, FVector(5000.f, 0.f, FloorTop), Depth, nullptr),
		FloorTop - Depth, 1e-2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerSpawnBoxTest,
	"Elysium.Substrate.NpcMakerGeometry.SpawnBox", GElysiumNpcMakerGeometryFlags)
bool FElysiumNpcMakerSpawnBoxTest::RunTest(const FString&)
{
	// A room: a maker (never `FL_NPC`: it runs no `NPCInit`), a standing NPC far off, and one that
	// will be moved into the box.
	FElysiumNpcWorldBuilder Builder(TEXT("maker_spawn_box"), 4410u);
	FElysiumEntityDef& Maker = Builder.AddEntity(TEXT("npc_maker"), TEXT("maker"), FVector::ZeroVector);
	Maker.Keys.Add(TEXT("model"), TEXT("models/maker.mdl"));
	Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VHuman"));
	Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
	Builder.AddNpc(TEXT("walker"), FVector(2000.f, 0.f, 0.f), TEXT("npc_VHuman"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Walker = F.Npc(TEXT("walker"));
	FElysiumEntity* MakerEntity = F.World.FindByName(TEXT("maker"));
	if (!TestNotNull(TEXT("the walker stands"), Walker) || !TestNotNull(TEXT("the maker stands"), MakerEntity))
	{
		return false;
	}
	TestTrue(TEXT("NPCInit set FL_NPC on the walker"), (Walker->Flags & ElysiumNpcMakerGeometry::FlagNpc) != 0);
	TestEqual(TEXT("and never on the maker"), MakerEntity->Flags & ElysiumNpcMakerGeometry::FlagNpc, 0);

	const float Half = 34.0f * ElysiumMove::U;   // `_DAT_1049ffac`
	const FVector Centre = FVector::ZeroVector;
	// The box flat at the maker's own Z (`CanMakeNPC`): an empty room — the maker itself inside the
	// square included — is not occupied.
	TestFalse(TEXT("an empty room is not occupied, the maker's own body included"),
		ElysiumNpcMakerGeometry::IsSpawnAreaOccupied(&F.World, nullptr, Centre, Half, Centre.Z));

	// An NPC standing in the box is.
	Walker->Origin = FVector(20.f, -10.f, 0.f);
	TestTrue(TEXT("an NPC standing in the square occupies it"),
		ElysiumNpcMakerGeometry::IsSpawnAreaOccupied(&F.World, nullptr, Centre, Half, Centre.Z));

	// The flat box only meets bodies standing at the maker's height: one 300 cm below the maker (the
	// floor a hovering maker's cached ground would have reached) does not occupy retail's box, but
	// does occupy the fleshpile's, whose floor drops to `m_flGround`.
	Walker->Origin = FVector(20.f, -10.f, -300.f);
	TestFalse(TEXT("a body below the maker's own Z is outside the flat box"),
		ElysiumNpcMakerGeometry::IsSpawnAreaOccupied(&F.World, nullptr, Centre, Half, Centre.Z));
	TestTrue(TEXT("but inside the fleshpile's box reaching down to its ground"),
		ElysiumNpcMakerGeometry::IsSpawnAreaOccupied(&F.World, nullptr, Centre, Half, -300.f));

	// Outside the 34-unit square: not occupied.
	Walker->Origin = FVector(Half + 2.f * ElysiumMove::HullHalfWidth, 0.f, 0.f);
	TestFalse(TEXT("an NPC beyond the square does not occupy it"),
		ElysiumNpcMakerGeometry::IsSpawnAreaOccupied(&F.World, nullptr, Centre, Half, Centre.Z));
	return true;
}

#endif
