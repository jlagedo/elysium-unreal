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
#include "ElysiumUseIcons.h"
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
	// One slab whose TOP is at `TopZ`, centred on (`X`, 0), answering the channels the way a baked
	// `ElysiumSig_PNS` world brush does: it blocks the pawn channels and IGNORES `ElysiumUse` — the
	// smoke's cause: the first cut traced `ElysiumUse` and fell through every shipped floor.
	UBoxComponent* SpawnMakerFloor(UWorld* World, float X, float TopZ, ECollisionChannel ObjectType,
		const TCHAR* Name)
	{
		AActor* Owner = World != nullptr ? World->SpawnActor<AActor>() : nullptr;
		if (Owner == nullptr)
		{
			return nullptr;
		}
		UBoxComponent* Floor = NewObject<UBoxComponent>(Owner, Name);
		Owner->SetRootComponent(Floor);
		Floor->InitBoxExtent(FVector(200.f, 200.f, 50.f));
		Floor->SetWorldLocation(FVector(X, 0.f, TopZ - 50.f));
		Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Floor->SetCollisionObjectType(ObjectType);
		Floor->SetCollisionResponseToAllChannels(ECR_Block);
		Floor->SetCollisionResponseToChannel(ELYSIUM_USE_CHANNEL, ECR_Ignore);
		Floor->RegisterComponent();
		Owner->AddInstanceComponent(Floor);
		return Floor;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerGroundCacheTest,
	"Elysium.Arm.NpcMakerGeometry.GroundCache", GElysiumNpcMakerGeometryFlags)
bool FElysiumNpcMakerGroundCacheTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	// `sp_tutorial_1`'s floor under `thug_maker`: the baked `World_PNS-` surface at -101.6 cm (Source
	// -40), with the maker authored 2 units above it at -96.52 (Source -38).
	const float FloorTop = -101.6f;
	// A second floor, a character body (object type Pawn) standing on it at x = 1000.
	const float CharacterTop = FloorTop + 180.f;
	if (!TestNotNull(TEXT("a world floor"), SpawnMakerFloor(World, 0.f, FloorTop, ECC_WorldStatic, TEXT("WorldFloor")))
		|| !TestNotNull(TEXT("a floor under the character"),
			SpawnMakerFloor(World, 1000.f, FloorTop, ECC_WorldStatic, TEXT("SecondFloor")))
		|| !TestNotNull(TEXT("a character body"),
			SpawnMakerFloor(World, 1000.f, CharacterTop, ECC_Pawn, TEXT("CharacterBody"))))
	{
		return false;
	}
	TestWorld.TickTestWorld();
	const float Depth = 2048.0f * ElysiumMove::U;   // `_DAT_1046bacc`
	const auto Ground = [World, Depth](float X, float Z)
	{
		return ElysiumNpcMakerGeometry::ResolveGroundZ(World, FVector(X, 0.f, Z), Depth, nullptr);
	};

	// A maker whose origin is EXACTLY on the floor: Source's ray starts on the surface and hits at
	// fraction 0, so `m_flGround = tr.endpos.z` is the maker's own Z.
	TestEqual(TEXT("a maker exactly on the floor caches its own Z"), Ground(0.f, FloorTop), FloorTop, 1e-3f);
	// `thug_maker`: 2 units above the floor caches the floor (the smoke read -5298.44).
	TestEqual(TEXT("thug_maker, 5.08 cm above the floor, caches the floor"),
		Ground(0.f, -96.52f), FloorTop, 1e-3f);
	// `disc3_maker`: 19 cm above the floor caches the floor.
	TestEqual(TEXT("a maker 19 cm above the floor caches the floor"), Ground(0.f, FloorTop + 19.f), FloorTop, 1e-3f);
	// A floor whose top is slightly ABOVE the origin (the maker embedded 2 cm): `startsolid`, and
	// Source's `endpos` is the start — the maker's own Z.
	TestEqual(TEXT("a maker embedded in the floor caches its own Z (startsolid)"),
		Ground(0.f, FloorTop - 2.f), FloorTop - 2.f, 1e-3f);
	// `CONTENTS_MONSTER` is not in `0x2400b`: a character body under the maker is not ground.
	TestEqual(TEXT("a character body under the maker is skipped; the floor beneath it answers"),
		Ground(1000.f, CharacterTop + 50.f), FloorTop, 1e-3f);
	// A maker with no floor within 2048 units caches the ray's end.
	TestEqual(TEXT("no floor within 2048 units: the ray's end"), Ground(5000.f, FloorTop), FloorTop - Depth, 1e-2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerSpawnBoxTest,
	"Elysium.Arm.NpcMakerGeometry.SpawnBox", GElysiumNpcMakerGeometryFlags)
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
