#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapEntities.h"
#include "ElysiumMapPlaces.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNodeEntity.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumRetailHullTable.h"

// 0018 story 4: the place set -- retail's AI network as the runtime stands it.
//
//   Substrate.PlaceSet.GetPosition  `CAI_Node::GetPosition` (`0x102fb0d0`), all three arms and every
//                                   climb bit.
//   Substrate.PlaceSet.Counter      `CNodeEnt::Spawn` (`0x102d78d0`)'s node-row counter over
//                                   synthetic defs: the standalone set never advances it, a type-0
//                                   standalone row makes nothing, a node row with no hint advances
//                                   it and is retired, an id past the network counts out, and a
//                                   runtime-created node row carries on counting.
//   Substrate.PlaceSet.Storage      the node's run-time words and the map-wide words.
//   Substrate.PlaceSet.Restore      `CAI_Hint::OnRestore` (`0x102d3ec0`) through a live hint.
//   Substrate.PlaceSet.AuthorJson   the asset's editor authoring from the staged payload.
//   Content.Places.{Tutorial,Hub}   the baked assets, and the runtime's own counter walk over the
//                                   map's entity table reproducing the bake's pairing.

static constexpr EAutomationTestFlags GElysiumPlaceSetTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumPlaceSetTests
{
	constexpr float U = ElysiumMove::U;

	FElysiumPlaceRow Place(int32 Type, const FVector& OriginCm, float YawDeg = 0.0f, int32 Flags = 0)
	{
		FElysiumPlaceRow Row;
		Row.Type = Type;
		Row.Flags = Flags;
		Row.OriginCm = OriginCm;
		Row.YawDeg = YawDeg;
		for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
		{
			Row.ZOffsetCm[Hull] = 1.5f * static_cast<float>(Hull + 1);
		}
		return Row;
	}

	FElysiumEntityDef& AddNode(FElysiumEntityDefs& Defs, const TCHAR* Classname, const TCHAR* Name,
		std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Keys, const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef& Def = Defs.Defs.AddDefaulted_GetRef();
		Def.Classname = Classname;
		Def.TargetName = Name;
		Def.Origin = Origin;
		for (const TPair<const TCHAR*, const TCHAR*>& Key : Keys)
		{
			Def.Keys.Add(Key.Key, Key.Value);
		}
		return Def;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSetGetPositionTest,
	"Elysium.Substrate.PlaceSet.GetPosition", GElysiumPlaceSetTestFlags)
bool FElysiumPlaceSetGetPositionTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSetTests;
	const FVector O(100.0, 200.0, 300.0);
	FElysiumPlaceSet Places;
	Places.AdoptRows({
		Place(2, O),                         // 0 ground
		Place(3, O),                         // 1 air: the raw origin
		Place(4, O, 0.0f, 0x4),              // 2 climb, off forward
		Place(4, O, 0.0f, 0x8),              // 3 climb, off left
		Place(4, O, 0.0f, 0x10),             // 4 climb, off right
		Place(4, O, 0.0f, 0),                // 5 climb, no bit
		Place(4, O, 90.0f, 0x4),             // 6 climb, off forward, yawed
		Place(4, O, 0.0f, 0x4 | 0x8 | 0x10), // 7 climb, every bit: forward is tested first
	});
	auto At = [&Places](int32 Node, int32 Hull)
	{
		FVector Out = FVector(-1.0);
		Places.GetPositionCm(Node, Hull, Out);
		return Out;
	};
	auto Near = [this](const TCHAR* What, const FVector& Got, const FVector& Want)
	{
		TestTrue(*FString::Printf(TEXT("%s: got %s, want %s"), What, *Got.ToString(), *Want.ToString()),
			Got.Equals(Want, 1e-3));
	};

	// Type 2: `+0x14 + 4*hull` on Z, and nothing else.
	Near(TEXT("ground, hull 0: origin + zoffset[0]"), At(0, 0), O + FVector(0.0, 0.0, 1.5));
	Near(TEXT("ground, hull 12: origin + zoffset[12]"), At(0, 12), O + FVector(0.0, 0.0, 19.5));
	// Any other type: the raw origin, whatever the hull.
	Near(TEXT("air: the raw origin"), At(1, 12), O);

	// Type 4: `width(hull) * 0.5 + 8.0` Source units along the yaw. HUMAN_HULL is 26 wide -> 21.
	// At Unreal yaw 0 the node faces +X, and Source's right vector (sin, -cos) reflects to +Y.
	const double S = 21.0 * U;
	Near(TEXT("climb 0x4 (off forward): origin + fwd*s"), At(2, 0), O + FVector(S, 0.0, 0.0));
	Near(TEXT("climb 0x8 (off left): origin - right*2s - fwd*s"), At(3, 0), O + FVector(-S, -2.0 * S, 0.0));
	Near(TEXT("climb 0x10 (off right): origin + right*2s - fwd*s"), At(4, 0), O + FVector(-S, 2.0 * S, 0.0));
	Near(TEXT("climb, no bit: origin - fwd*s"), At(5, 0), O + FVector(-S, 0.0, 0.0));
	Near(TEXT("climb at Unreal yaw 90 steps along +Y"), At(6, 0), O + FVector(0.0, S, 0.0));
	Near(TEXT("climb with every bit takes the forward arm"), At(7, 0), O + FVector(S, 0.0, 0.0));
	// The width is `0x102d6180`'s maxs.x - mins.x: WIDE_HUMAN_HULL is -15..20 on X (and 30 on Y),
	// so 35 * 0.5 + 8 = 25.5 -- not the Y extent.
	Near(TEXT("climb, WIDE_HUMAN_HULL: the X width"), At(2, 3), O + FVector(25.5 * U, 0.0, 0.0));
	Near(TEXT("climb ignores the hull's Z offset"), At(2, 12), O + FVector((80.0 * 0.5 + 8.0) * U, 0.0, 0.0));

	FVector Untouched = FVector(7.0);
	TestFalse(TEXT("a node past the network answers false"), Places.GetPositionCm(8, 0, Untouched));
	TestFalse(TEXT("a hull past the table answers false"), Places.GetPositionCm(0, ElysiumRetailHulls::Count, Untouched));
	TestEqual(TEXT("...and leaves the out-parameter alone"), Untouched, FVector(7.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSetCounterTest,
	"Elysium.Substrate.PlaceSet.Counter", GElysiumPlaceSetTestFlags)
bool FElysiumPlaceSetCounterTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSetTests;
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Places().AdoptRows({ Place(2, FVector::ZeroVector), Place(2, FVector(64.0, 0.0, 0.0)),
		Place(2, FVector(128.0, 0.0, 0.0)) });

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__place_counter__");
	AddNode(Defs, TEXT("info_hint"), TEXT("typed_hint"), { { TEXT("hinttype"), TEXT("10100") } });       // 0
	AddNode(Defs, TEXT("info_hint"), TEXT("untyped_hint"), { { TEXT("Group"), TEXT("g") } });             // 1
	AddNode(Defs, TEXT("info_node_patrol_point"), TEXT("a1"), { { TEXT("Group"), TEXT("A1") } });        // 2
	AddNode(Defs, TEXT("info_node"), TEXT("plain"), {});                                                  // 3
	AddNode(Defs, TEXT("info_node_kick_over"), TEXT("kick"), {});                                         // 4
	AddNode(Defs, TEXT("info_node_cover_low"), TEXT("cover"), {});                                        // 5
	AddNode(Defs, TEXT("info_node_crosswalk"), TEXT("past"), {});                                         // 6
	AddNode(Defs, TEXT("info_target"), TEXT("bystander"), {});                                            // 7
	const int32 Misses = ElysiumAiNetwork::NodeMissCounter();                      // DAT_106c994c, never reset
	World.Load(MoveTemp(Defs));

	const FElysiumPlaceSet& Places = World.Places();
	auto HintAt = [&World](int32 Index) { return FElysiumHint::Cast(World.Entities()[Index].Get()); };

	// The standalone set: a hint with node id -1 for a type, nothing for no type; the counter unread.
	const FElysiumHint* Typed = HintAt(0);
	if (TestNotNull(TEXT("a typed info_hint is a hint"), Typed))
	{
		TestEqual(TEXT("...with no node (m_nNodeID -1)"), Typed->NodeId, static_cast<int32>(INDEX_NONE));
	}
	TestNull(TEXT("a type-0 info_hint makes nothing: its row is removed, a Group does not save it"),
		World.Entities()[1].Get());
	TestNull(TEXT("...and it is not findable by name"), World.FindByName(TEXT("untyped_hint")));
	const FElysiumHint* Kick = HintAt(4);
	if (TestNotNull(TEXT("info_node_kick_over is forced to type 0x283c: a hint"), Kick))
	{
		TestEqual(TEXT("...standalone, so no node"), Kick->NodeId, static_cast<int32>(INDEX_NONE));
	}

	// Node rows take the counter in BSP order, standalone rows skipped.
	const FElysiumHint* A1 = HintAt(2);
	if (TestNotNull(TEXT("the patrol point is a hint"), A1))
	{
		TestEqual(TEXT("...the first node row takes node 0"), A1->NodeId, 0);
		TestEqual(TEXT("...and node 0 holds it (+0xa0)"), Places.AttachedHint(0), A1->Handle);
	}
	TestNull(TEXT("a node row with no hint is removed"), World.Entities()[3].Get());
	TestFalse(TEXT("...its node holds no hint"), Places.AttachedHint(1).IsSet());
	const FElysiumHint* Cover = HintAt(5);
	if (TestNotNull(TEXT("the cover row is a hint"), Cover))
	{
		TestEqual(TEXT("...and took node 2: the hintless row still advanced the counter"), Cover->NodeId, 2);
		TestEqual(TEXT("...which holds it"), Places.AttachedHint(2), Cover->Handle);
	}
	const FElysiumHint* Past = HintAt(6);
	if (TestNotNull(TEXT("the crosswalk row is a hint"), Past))
	{
		TestEqual(TEXT("...its id is the counter even past the network (0x102d2fce)"), Past->NodeId, 3);
	}
	TestEqual(TEXT("...and it counted out (DAT_106c994c)"), ElysiumAiNetwork::NodeMissCounter() - Misses, 1);
	TestNotNull(TEXT("a non-node row is untouched"), World.Entities()[7].Get());
	TestEqual(TEXT("the counter stands after the last node row"), Places.SpawnCounter(), 4);
	TestEqual(TEXT("five live hints on the list"), World.HintList().Num(), 5);

	// A node row created at run time carries on from the map's counter.
	FElysiumEntityDef Runtime;
	Runtime.Classname = TEXT("info_node_cover_med");
	Runtime.TargetName = TEXT("runtime_cover");
	const FElysiumHint* Made = FElysiumHint::Cast(World.Resolve(World.CreateRuntimeEntityNoSpawn(MoveTemp(Runtime))));
	if (TestNotNull(TEXT("a runtime-created node row is a hint"), Made))
	{
		TestEqual(TEXT("...taking the next id"), Made->NodeId, 4);
	}
	TestEqual(TEXT("...past the network, so it counts out"), ElysiumAiNetwork::NodeMissCounter() - Misses, 2);
	FElysiumEntityDef RuntimePlain;
	RuntimePlain.Classname = TEXT("info_node");
	TestFalse(TEXT("a runtime node row with no hint builds nothing"),
		World.CreateRuntimeEntityNoSpawn(MoveTemp(RuntimePlain)).IsSet());
	TestEqual(TEXT("...but still advanced the counter"), World.Places().SpawnCounter(), 6);
	FElysiumEntityDef RuntimeHint;
	RuntimeHint.Classname = TEXT("info_hint");
	RuntimeHint.Keys.Add(TEXT("hinttype"), TEXT("100"));
	World.CreateRuntimeEntityNoSpawn(MoveTemp(RuntimeHint));
	TestEqual(TEXT("a runtime standalone row leaves the counter alone"), World.Places().SpawnCounter(), 6);

	// `0x102f6690`: a fresh map load zeroes the counter and the node words.
	World.Places().BeginMapSpawn();
	TestEqual(TEXT("BeginMapSpawn zeroes the counter"), World.Places().SpawnCounter(), 0);
	TestEqual(TEXT("...but not DAT_106c994c, which nothing in the image zeroes"), ElysiumAiNetwork::NodeMissCounter() - Misses, 2);
	TestFalse(TEXT("...and detaches every hint"), World.Places().AttachedHint(0).IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSetStorageTest,
	"Elysium.Substrate.PlaceSet.Storage", GElysiumPlaceSetTestFlags)
bool FElysiumPlaceSetStorageTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSetTests;
	FElysiumPlaceSet Places;
	TestFalse(TEXT("a fresh place set is not adopted"), Places.IsAdopted());
	TestEqual(TEXT("...and has no node"), Places.NumNodes(), 0);
	FElysiumPlaceWanderCap Human;
	Human.Hull = 0;
	Human.CapUnits = 2942.0f;
	FElysiumPlaceWanderCap Rat;
	Rat.Hull = 19;
	Rat.CapUnits = 2942.0f;
	Rat.bFromHuman = true;
	Places.AdoptRows({ Place(2, FVector::ZeroVector), Place(2, FVector::ZeroVector) }, (1 << 0) | (1 << 19),
		{ Human, Rat }, { FIntPoint(0, 1) });
	Places.BeginMapSpawn();
	const int32 Misses = ElysiumAiNetwork::NodeMissCounter();                      // DAT_106c994c, never reset
	TestTrue(TEXT("adopted"), Places.IsAdopted());
	TestTrue(TEXT("rows are indexed by position"), Places.Row(1).NetworkIndex == 1);
	TestTrue(TEXT("IsValidNode bounds"), Places.IsValidNode(1) && !Places.IsValidNode(2) && !Places.IsValidNode(-1));
	TestEqual(TEXT("UsedHullBits"), Places.UsedHullBits(), (1 << 0) | (1 << 19));
	TestEqual(TEXT("the human cap"), Places.WanderCapUnits(0), 2942.0f);
	TestFalse(TEXT("...is its own"), Places.WanderCapFromHuman(0));
	TestTrue(TEXT("the rat's cap is the human's"), Places.WanderCapFromHuman(19));
	TestEqual(TEXT("an undeclared hull has no cap"), Places.WanderCapUnits(12), 0.0f);
	TestEqual(TEXT("crosswalk pairs"), Places.CrosswalkPairs().Num(), 1);

	TestEqual(TEXT("node +0x9c starts at 0 (0x102fc5d0)"), Places.NodeCooldown(1), 0.0f);
	Places.SetNodeCooldown(1, 12.5f);
	TestEqual(TEXT("...and reads back what story 9 writes"), Places.NodeCooldown(1), 12.5f);
	Places.SetNodeCooldown(5, 1.0f);
	TestEqual(TEXT("a write past the network is dropped"), Places.NodeCooldown(5), 0.0f);

	// `0x102d3e60`.
	TestEqual(TEXT("-1 resolves to no node"), Places.ResolveHintNode(INDEX_NONE), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...uncounted"), ElysiumAiNetwork::NodeMissCounter() - Misses, 0);
	TestEqual(TEXT("an id in the network resolves to itself"), Places.ResolveHintNode(1), 1);
	TestEqual(TEXT("an id past it resolves to no node"), Places.ResolveHintNode(2), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...and counts out"), ElysiumAiNetwork::NodeMissCounter() - Misses, 1);
	TestEqual(TEXT("a negative id other than -1 counts out too"), Places.ResolveHintNode(-5), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...twice now"), ElysiumAiNetwork::NodeMissCounter() - Misses, 2);
	Places.BeginMapSpawn();
	TestEqual(TEXT("a new map spawn zeroes the cooldowns"), Places.NodeCooldown(1), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSetRestoreTest,
	"Elysium.Substrate.PlaceSet.Restore", GElysiumPlaceSetTestFlags)
bool FElysiumPlaceSetRestoreTest::RunTest(const FString&)
{
	using namespace ElysiumPlaceSetTests;
	FElysiumEntityWorld World(nullptr, nullptr);
	const FVector NodeOrigin(40.0, 50.0, 60.0);
	World.Places().AdoptRows({ Place(2, NodeOrigin) });
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__place_restore__");
	AddNode(Defs, TEXT("info_node_patrol_point"), TEXT("pp"), { { TEXT("Group"), TEXT("A1") } }, FVector(41.0, 50.0, 60.0));
	AddNode(Defs, TEXT("info_hint"), TEXT("loose"), { { TEXT("hinttype"), TEXT("10100") } });
	World.Load(MoveTemp(Defs));

	FElysiumHint* Hint = FElysiumHint::Cast(World.FindByName(TEXT("pp")));
	FElysiumHint* Loose = FElysiumHint::Cast(World.FindByName(TEXT("loose")));
	if (!TestNotNull(TEXT("the patrol hint"), Hint) || !TestNotNull(TEXT("the standalone hint"), Loose))
	{
		return false;
	}
	// Nothing about a node is saved: a restored map starts from a fresh network and the hint's own
	// `OnRestore` finds its node again by its restored `m_nNodeID`.
	World.Places().BeginMapSpawn();
	const int32 Misses = ElysiumAiNetwork::NodeMissCounter();                      // DAT_106c994c, never reset
	Hint->OnPostRestore(World);
	TestEqual(TEXT("the restored hint relinks onto its node (+0xa0)"), World.Places().AttachedHint(0), Hint->Handle);
	TestEqual(TEXT("...and stands at the node's raw origin (Teleport, vtable +0x2d4)"), Hint->Origin, NodeOrigin);
	const FVector LooseOrigin = Loose->Origin;
	Loose->OnPostRestore(World);
	TestEqual(TEXT("a standalone hint takes the no-node arm and stays put"), Loose->Origin, LooseOrigin);
	TestEqual(TEXT("...counting nothing"), ElysiumAiNetwork::NodeMissCounter() - Misses, 0);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlaceSetAuthorJsonTest,
	"Elysium.Substrate.PlaceSet.AuthorJson", GElysiumPlaceSetTestFlags)
bool FElysiumPlaceSetAuthorJsonTest::RunTest(const FString&)
{
	FString Offsets;
	for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
	{
		Offsets += FString::Printf(TEXT("%s%d.5"), Hull ? TEXT(",") : TEXT(""), Hull);
	}
	auto Payload = [&Offsets](int32 NumNodes, const TCHAR* SecondOffsets)
	{
		return FString::Printf(TEXT(R"JSON({"version":1,"map":"sp_test_1","numNodes":%d,"usedHullBits":129,
			"places":[
			 {"index":0,"type":2,"flags":0,"origin":[1,2,3],"yaw":-90,"zOffsets":[%s],"wcId":7,"hint":12},
			 {"index":1,"type":4,"flags":8,"origin":[4,5,6],"yaw":45,"zOffsets":[%s],"wcId":-1,"hint":-1}],
			"pairing":{"nodeRows":3,"outOfRange":[{"bspIndex":40,"counter":2}],"standalone":[5,6]},
			"crosswalkPairs":[[0,1,1]],
			"wanderCaps":[{"hull":0,"capUnits":2942.5,"fromHuman":false},{"hull":7,"capUnits":2942.5,"fromHuman":true}]})JSON"),
			NumNodes, *Offsets, SecondOffsets);
	};

	UElysiumMapPlaces* Asset = NewObject<UElysiumMapPlaces>(GetTransientPackage());
	if (!TestTrue(TEXT("a well-formed payload authors"), Asset->AuthorJson(Payload(2, *Offsets))))
	{
		return false;
	}
	TestTrue(TEXT("...into a valid asset"), Asset->IsValidPlaces());
	TestEqual(TEXT("map"), Asset->MapName, FString(TEXT("sp_test_1")));
	TestEqual(TEXT("rows"), Asset->Rows.Num(), 2);
	TestEqual(TEXT("used hulls"), Asset->UsedHullBits, 129);
	TestEqual(TEXT("row 0 origin (cm, as staged)"), Asset->Rows[0].OriginCm, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("row 0 yaw"), Asset->Rows[0].YawDeg, -90.0f);
	TestEqual(TEXT("row 0 last hull offset"), Asset->Rows[0].ZOffsetCm[ElysiumRetailHulls::Count - 1], 21.5f);
	TestEqual(TEXT("row 0 wc id"), Asset->Rows[0].WcId, 7);
	TestEqual(TEXT("row 0 paired hint"), Asset->Rows[0].HintBspIndex, 12);
	TestEqual(TEXT("row 1 type and flags"), Asset->Rows[1].Type * 100 + Asset->Rows[1].Flags, 408);
	TestEqual(TEXT("pairing node rows"), Asset->PairingNodeRows, 3);
	TestTrue(TEXT("pairing out of range"), Asset->PairingOutOfRange.Num() == 1
		&& Asset->PairingOutOfRange[0].BspIndex == 40 && Asset->PairingOutOfRange[0].Counter == 2);
	TestTrue(TEXT("pairing standalone"), Asset->PairingStandalone == TArray<int32>({ 5, 6 }));
	TestTrue(TEXT("crosswalk pairs"), Asset->CrosswalkPairs == TArray<FIntPoint>({ FIntPoint(0, 1) }));
	TestTrue(TEXT("wander caps"), Asset->WanderCaps.Num() == 2 && Asset->WanderCaps[1].bFromHuman
		&& Asset->WanderCaps[1].CapUnits == 2942.5f);

	AddExpectedError(TEXT("places refused"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("a count that disagrees with the rows is refused"), Asset->AuthorJson(Payload(3, *Offsets)));
	TestFalse(TEXT("a row short of 22 hull offsets is refused"), Asset->AuthorJson(Payload(2, TEXT("1,2,3"))));
	TestEqual(TEXT("...and a refusal leaves the asset as it was"), Asset->Rows.Num(), 2);
	return true;
}
#endif

// --- The baked assets -----------------------------------------------------------------------------

namespace ElysiumPlaceSetTests
{
	struct FContentPins
	{
		const TCHAR* Map = nullptr;
		int32 Places = 0;
		int32 BoundHints = 0;
		float HumanCapUnits = 0.0f;
	};

	// The runtime's own walk over the map's entity table, compared with the bake's pairing: the hint
	// each node holds, and the hint-making rows the counter ran past.
	void CheckPairing(FAutomationTestBase& Test, const TCHAR* Map, const UElysiumMapPlaces& Asset)
	{
		FElysiumEntityDefs Defs;
		if (!Test.TestTrue(*FString::Printf(TEXT("%s: the entity table loads"), Map),
			ElysiumEntityDefSource::Load(Map, Defs) != EElysiumEntityDefSource::None))
		{
			return;
		}
		FElysiumPlaceSet Places;
		Places.Adopt(Asset);
		Places.BeginMapSpawn();
		const int32 Misses = ElysiumAiNetwork::NodeMissCounter();                  // DAT_106c994c, never reset
		TArray<FIntPoint> OutOfRange;
		for (int32 Index = 0; Index < Defs.Defs.Num(); ++Index)
		{
			const ElysiumNodeEntity::FSpawnResult Spawn =
				ElysiumNodeEntity::SpawnNodeRow(Defs.Defs[Index], Places, FElysiumEntityHandle(Index, 1));
			if (Spawn.Arm == ElysiumNodeEntity::ESpawnArm::NodeHint && !Places.IsValidNode(Spawn.NodeId))
			{
				OutOfRange.Emplace(Index, Spawn.NodeId);
			}
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: the counter ends where the bake's did"), Map),
			Places.SpawnCounter(), Asset.PairingNodeRows);
		int32 Diverged = 0;
		for (int32 Node = 0; Node < Places.NumNodes(); ++Node)
		{
			const FElysiumEntityHandle Held = Places.AttachedHint(Node);
			const int32 Runtime = Held.IsSet() ? Held.Index : INDEX_NONE;
			if (Runtime != Asset.Rows[Node].HintBspIndex && Diverged++ < 5)
			{
				Test.AddError(FString::Printf(TEXT("%s: node %d holds entity %d at run time, the bake paired %d"),
					Map, Node, Runtime, Asset.Rows[Node].HintBspIndex));
			}
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: every node's hint matches the bake"), Map), Diverged, 0);
		TArray<FIntPoint> Baked;
		for (const FElysiumPlaceOutOfRange& Row : Asset.PairingOutOfRange)
		{
			Baked.Emplace(Row.BspIndex, Row.Counter);
		}
		Test.TestTrue(*FString::Printf(TEXT("%s: the out-of-range rows match the bake (%d at run time, %d baked)"), Map,
			OutOfRange.Num(), Baked.Num()), OutOfRange == Baked);
		Test.TestEqual(*FString::Printf(TEXT("%s: DAT_106c994c counts exactly those"), Map),
			ElysiumAiNetwork::NodeMissCounter() - Misses, Baked.Num());
	}

	void CheckContent(FAutomationTestBase& Test, const FContentPins& Pins)
	{
		const FString Path = FElysiumContentPaths::BakedMapPlaces(Pins.Map);
		const UElysiumMapPlaces* Asset = LoadObject<UElysiumMapPlaces>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Asset == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("%s: no places asset at %s; run: uv run elysium bake map --maps %s"),
				Pins.Map, *Path, Pins.Map));
			return;
		}
		Test.TestTrue(*FString::Printf(TEXT("%s: the asset is valid"), Pins.Map), Asset->IsValidPlaces());
		Test.TestEqual(*FString::Printf(TEXT("%s: places"), Pins.Map), Asset->NumNodes, Pins.Places);
		int32 NotGround = 0;
		for (const FElysiumPlaceRow& Row : Asset->Rows)
		{
			NotGround += Row.Type == 2 ? 0 : 1;
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: every place is a ground node"), Pins.Map), NotGround, 0);
		Test.TestEqual(*FString::Printf(TEXT("%s: bound hints"), Pins.Map),
			Asset->Rows.FilterByPredicate([](const FElysiumPlaceRow& Row) { return Row.HintBspIndex >= 0; }).Num(),
			Pins.BoundHints);
		Test.TestEqual(*FString::Printf(TEXT("%s: no hint row runs past the network"), Pins.Map),
			Asset->PairingOutOfRange.Num(), 0);
		Test.TestTrue(*FString::Printf(TEXT("%s: the human hull is used"), Pins.Map), (Asset->UsedHullBits & 1) != 0);
		const FElysiumPlaceWanderCap* Human = Asset->WanderCaps.FindByPredicate(
			[](const FElysiumPlaceWanderCap& Cap) { return Cap.Hull == 0; });
		if (Test.TestNotNull(*FString::Printf(TEXT("%s: a human wander cap"), Pins.Map), Human))
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: the human cap"), Pins.Map), Human->CapUnits, Pins.HumanCapUnits, 0.01f);
			Test.TestFalse(*FString::Printf(TEXT("%s: ...is the human hull's own"), Pins.Map), Human->bFromHuman);
		}
		for (int32 Hull = 0; Hull < ElysiumRetailHulls::Count; ++Hull)
		{
			if ((Asset->UsedHullBits & (1 << Hull)) != 0)
			{
				Test.TestTrue(*FString::Printf(TEXT("%s: used hull %d has a cap"), Pins.Map, Hull),
					Asset->WanderCaps.ContainsByPredicate([Hull](const FElysiumPlaceWanderCap& Cap) { return Cap.Hull == Hull; }));
			}
		}
		CheckPairing(Test, Pins.Map, *Asset);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlacesTutorialTest, "Elysium.Content.Places.Tutorial",
	GElysiumPlaceSetTestFlags)
bool FElysiumPlacesTutorialTest::RunTest(const FString&)
{
	ElysiumPlaceSetTests::CheckContent(*this, { TEXT("sp_tutorial_1"), 203, 49, 2942.141f });
	// The thug's patrol points A1..A3 (BSP rows 433..435) bind nodes 15..17.
	const UElysiumMapPlaces* Asset = LoadObject<UElysiumMapPlaces>(nullptr,
		*FElysiumContentPaths::BakedMapPlaces(TEXT("sp_tutorial_1")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Asset != nullptr)
	{
		for (int32 Point = 0; Point < 3; ++Point)
		{
			const FElysiumPlaceRow* Row = Asset->Rows.FindByPredicate(
				[Point](const FElysiumPlaceRow& Candidate) { return Candidate.HintBspIndex == 433 + Point; });
			if (TestNotNull(*FString::Printf(TEXT("A%d (BSP %d) is paired"), Point + 1, 433 + Point), Row))
			{
				TestEqual(*FString::Printf(TEXT("A%d binds node %d"), Point + 1, 15 + Point), Row->NetworkIndex, 15 + Point);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlacesHubTest, "Elysium.Content.Places.Hub",
	GElysiumPlaceSetTestFlags)
bool FElysiumPlacesHubTest::RunTest(const FString&)
{
	ElysiumPlaceSetTests::CheckContent(*this, { TEXT("sm_hub_1"), 578, 274, 3113.326f });
	// The six `info_node_crosswalk` rows (BSP 1613..1618) bind nodes 258..263, joined by 8 pairs.
	const UElysiumMapPlaces* Asset = LoadObject<UElysiumMapPlaces>(nullptr,
		*FElysiumContentPaths::BakedMapPlaces(TEXT("sm_hub_1")), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Asset != nullptr)
	{
		for (int32 Crosswalk = 0; Crosswalk < 6; ++Crosswalk)
		{
			const int32 Node = 258 + Crosswalk;
			TestTrue(*FString::Printf(TEXT("node %d holds crosswalk BSP %d"), Node, 1613 + Crosswalk),
				Asset->Rows.IsValidIndex(Node) && Asset->Rows[Node].HintBspIndex == 1613 + Crosswalk);
		}
		TestEqual(TEXT("crosswalk pairs"), Asset->CrosswalkPairs.Num(), 8);
	}
	return true;
}

#endif
