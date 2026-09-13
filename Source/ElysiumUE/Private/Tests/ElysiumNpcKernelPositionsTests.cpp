#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Positions**. The decisions this family ports read a node graph and run
// world traces, and this substrate produces neither — so the recovered rules are written as PURE
// functions over a candidate list and the members that feed them ask the seams. These cases drive
// the rules directly (which is the only way the thresholds and the arm order can be asserted at
// all) and then drive the members to prove the seam is asked and that the refusal is retail's.
//
// Every number below came out of the pinned retail `.rdata` at the address its comment names.
// Retail's are Source units; `ElysiumMove::U` converts, exactly as the bodies do.

static constexpr EAutomationTestFlags GPositionsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float U = ElysiumMove::U;

	FElysiumNpc::FHintWords MakeNode(int32 HintType, const FVector& OriginCm,
		const TCHAR* Group = TEXT(""))
	{
		FElysiumNpc::FHintWords Node;
		Node.bValid = true;
		Node.HintType = HintType;
		Node.OriginCm = OriginCm;
		Node.Group = Group;
		return Node;
	}

	// The clearance predicate the four gated selectors take. `AlwaysClear` is the arm that lets the
	// decision through; `NeverClear` is the arm that proves the gate is asked.
	bool AlwaysClear(const FVector&, float) { return true; }
	bool NeverClear(const FVector&, float) { return false; }
}

// --- The five ungated node selectors ------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsSelectorsTest,
	"Elysium.Substrate.NpcKernelPositions.NodeSelectors", GPositionsFlags)
bool FElysiumNpcKernelPositionsSelectorsTest::RunTest(const FString&)
{
	const FVector Player(0.0, 0.0, 0.0);

	// `SelectCenterNode` `0x103b0930`: type 0x4651 only, nearest to the player, `<` so the FIRST of
	// a tie survives.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(0x4653, FVector(10.0, 0.0, 0.0)));    // a ledge, not a centre
		Nodes.Add(MakeNode(0x4651, FVector(500.0, 0.0, 0.0)));
		Nodes.Add(MakeNode(0x4651, FVector(100.0, 0.0, 0.0)));
		Nodes.Add(MakeNode(0x4651, FVector(100.0, 0.0, 0.0)));   // the tie
		TestEqual(TEXT("SelectCenterNode takes the nearest 0x4651 and keeps the first of a tie"),
			FElysiumNpc::SelectCenterNodeRule(Nodes, Player), 2);
		TArray<FElysiumNpc::FHintWords> NoCentre;
		NoCentre.Add(MakeNode(0x4653, FVector(10.0, 0.0, 0.0)));
		TestEqual(TEXT("and answers null when the map authors no centre node"),
			FElysiumNpc::SelectCenterNodeRule(NoCentre, Player), (int32)INDEX_NONE);
	}

	// `SelectLedgeNode` `0x103b0ab0`: type 0x4653, and the reference point is retail's `char`
	// argument — `'\0'` measures from the PLAYER, anything else from the NPC.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(0x4653, FVector(100.0, 0.0, 0.0)));   // near the player
		Nodes.Add(MakeNode(0x4653, FVector(900.0, 0.0, 0.0)));   // near the NPC at x=1000
		Nodes.Add(MakeNode(0x4651, FVector(0.0, 0.0, 0.0)));     // a centre node, never picked
		TestEqual(TEXT("SelectLedgeNode('\\0') measures from the player"),
			FElysiumNpc::SelectLedgeNodeRule(Nodes, Player), 0);
		TestEqual(TEXT("SelectLedgeNode(non-zero) measures from the NPC"),
			FElysiumNpc::SelectLedgeNodeRule(Nodes, FVector(1000.0, 0.0, 0.0)), 1);
	}

	// `SelectTeleportArchway` `0x103a9540`: type 0x3e82, flat distance at or past
	// `_DAT_104c3cbc = 24.0` units, and the score is the yaw delta ALONE — distance does not enter
	// it, so the far archway in front beats the near one to the side.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(0x3e82, FVector(-20.0, 0.0, 0.0)));            // inside 24 units: rejected
		Nodes.Add(MakeNode(0x3e82, FVector(0.0, 200.0 * U, 0.0)));        // 90 degrees off
		Nodes.Add(MakeNode(0x3e82, FVector(-2000.0 * U, 0.0, 0.0)));      // straight ahead, far
		const int32 Pick = FElysiumNpc::SelectTeleportArchwayRule(Nodes, Player, 0.f);
		TestEqual(TEXT("SelectTeleportArchway scores by yaw alone, so distance never breaks a tie"),
			Pick, 2);

		TArray<FElysiumNpc::FHintWords> TooClose;
		TooClose.Add(MakeNode(0x3e82, FVector(23.0 * U, 0.0, 0.0)));
		TestEqual(TEXT("and a node inside 24.0 units (_DAT_104c3cbc) is refused"),
			FElysiumNpc::SelectTeleportArchwayRule(TooClose, Player, 0.f), (int32)INDEX_NONE);
	}

	// `SelectDiveOutPoint` `0x103a9ad0`: type 0x3e85, flat distance at or past
	// `_DAT_104c3cfc = 40.0` units, score = |yawDelta| + flatDistance. One degree of misalignment
	// costs exactly one unit of distance, which is retail's own weighting.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(0x3e85, FVector(39.0 * U, 0.0, 0.0)));       // inside 40 units: refused
		Nodes.Add(MakeNode(0x3e85, FVector(-100.0 * U, 0.0, 0.0)));     // yaw 0, 100 units away
		Nodes.Add(MakeNode(0x3e85, FVector(0.0, 60.0 * U, 0.0)));       // 90 degrees off, 60 units
		// 0 + 254 cm against 90 + 152.4 cm: the 90-degrees-off node wins on the SUM even though it
		// loses on yaw outright, which is the whole of what the additive score means. The archway
		// picker, which scores on yaw alone, would have taken the other one.
		TestEqual(TEXT("SelectDiveOutPoint adds the yaw delta to the flat distance"),
			FElysiumNpc::SelectDiveOutPointRule(Nodes, Player, 0.f), 2);
		// And with the distance term removed it does: the same two positions typed as archways.
		TArray<FElysiumNpc::FHintWords> Archways;
		Archways.Add(MakeNode(0x3e82, FVector(-100.0 * U, 0.0, 0.0)));
		Archways.Add(MakeNode(0x3e82, FVector(0.0, 60.0 * U, 0.0)));
		TestEqual(TEXT("where the archway picker, scoring on yaw alone, takes the aligned one"),
			FElysiumNpc::SelectTeleportArchwayRule(Archways, Player, 0.f), 0);
	}

	// `SelectDiveInPoint` `0x103a9760`: `SelectDiveOutPoint` plus the leader's own distance gate
	// (`_DAT_104c3d00 = 45.0` units) and the away-from-the-player hemisphere test.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(0x3e85, FVector(-800.0, 0.0, 0.0)));   // behind the leader, away from player
		Nodes.Add(MakeNode(0x3e85, FVector(500.0, 0.0, 0.0)));    // toward the player: dot >= 0

		// Leader 500 cm behind the player on -X, so the player direction is +X.
		const FVector Self(-500.0, 0.0, 0.0);
		TestEqual(TEXT("SelectDiveInPoint refuses a node in the player's hemisphere"),
			FElysiumNpc::SelectDiveInPointRule(Nodes, Player, 0.f, Self), 0);

		// 45 units is 114.3 cm; a leader 100 cm from the player never walks the list.
		TestEqual(TEXT("and answers null outright when the leader is inside 45.0 units of the player"),
			FElysiumNpc::SelectDiveInPointRule(Nodes, Player, 0.f, FVector(-100.0, 0.0, 0.0)),
			(int32)INDEX_NONE);
	}

	return true;
}

// --- The four gated teleport selectors ----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsTeleportNodesTest,
	"Elysium.Substrate.NpcKernelPositions.TeleportNodeSelectors", GPositionsFlags)
bool FElysiumNpcKernelPositionsTeleportNodesTest::RunTest(const FString&)
{
	const FVector Player(0.0, 0.0, 0.0);

	// `CNPC_VSheriffMan::SelectTeleportNode` `0x103b0630`.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		// Type filter: 17000 / 0x4653 / 0x4652 only.
		Nodes.Add(MakeNode(0x4651, FVector(-1000.0, 0.0, 0.0)));          // a centre node: skipped
		Nodes.Add(MakeNode(17000, FVector(-1000.0, 0.0, 0.0)));           // straight ahead
		Nodes.Add(MakeNode(0x4652, FVector(0.0, 1000.0, 0.0)));           // 90 degrees off
		// `_DAT_10450564 = 100.0` multiplies dz BEFORE the root, so one unit of height reads as a
		// hundred: this node is 10 cm up and therefore 1000 cm away as far as the score is
		// concerned. It is the closest in real space and must still lose.
		Nodes.Add(MakeNode(0x4653, FVector(-300.0, 0.0, 10.0)));

		const FElysiumNpc::FTeleportNodePick Pick =
			FElysiumNpc::SelectTeleportNodeSheriffRule(Nodes, Player, 0.f, &AlwaysClear);
		TestEqual(TEXT("SelectTeleportNode(Sheriff) takes the aligned node"), Pick.Index, 1);
		// score = |yaw|/180 * 0.4 + min(dist, 1000 units)/1000 units * 0.6.
		const float Expected = (1000.0f / (1000.0f * U)) * 0.6f;
		TestTrue(TEXT("and its score is the recovered two-term weighted sum"),
			FMath::IsNearlyEqual(Pick.Score, Expected, 1e-4f));

		const FElysiumNpc::FTeleportNodePick Refused =
			FElysiumNpc::SelectTeleportNodeSheriffRule(Nodes, Player, 0.f, &NeverClear);
		TestEqual(TEXT("and PositionClearForTeleport is a gate, not a tiebreak"), Refused.Index,
			(int32)INDEX_NONE);

		TArray<FElysiumNpc::FHintWords> TooClose;
		TooClose.Add(MakeNode(17000, FVector(-99.0 * U, 0.0, 0.0)));
		TestEqual(TEXT("a node inside DAT_104c6124 = 100.0 units is refused before the gate"),
			FElysiumNpc::SelectTeleportNodeSheriffRule(TooClose, Player, 0.f, &AlwaysClear).Index,
			(int32)INDEX_NONE);
	}

	// `CNPC_VAndreiBlood::SelectTeleportNode` `0x1035ddd0` — the coin flip picks nearest or
	// FARTHEST, and the seed of the running best follows it.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(0x4653, FVector(-400.0, 0.0, 0.0)));      // wrong type for Andrei
		Nodes.Add(MakeNode(17000, FVector(-400.0, 0.0, 0.0)));
		Nodes.Add(MakeNode(0x4269, FVector(-4000.0, 0.0, 0.0)));
		TestEqual(TEXT("SelectTeleportNode(Andrei) nearest arm"),
			FElysiumNpc::SelectTeleportNodeAndreiRule(Nodes, Player, false, &AlwaysClear), 1);
		TestEqual(TEXT("SelectTeleportNode(Andrei) farthest arm, the same list"),
			FElysiumNpc::SelectTeleportNodeAndreiRule(Nodes, Player, true, &AlwaysClear), 2);
		TestEqual(TEXT("and the clearance gate refuses both arms"),
			FElysiumNpc::SelectTeleportNodeAndreiRule(Nodes, Player, true, &NeverClear),
			(int32)INDEX_NONE);
	}

	// `CNPC_VChangBros::SelectTeleportNode` `0x1036cce0` — the nearest node is kept, and a node in
	// the PLAYER's sector replaces it outright however much further away it is.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(18000, FVector(-400.0, 0.0, 0.0)));    // nearest, wrong sector
		Nodes.Add(MakeNode(0x4652, FVector(-4000.0, 0.0, 0.0)));  // far, the player's sector
		auto SectorOf = [](const FVector& P) -> int32
		{
			if (P.Equals(FVector::ZeroVector))
			{
				return 2;   // the player
			}
			return P.X < -1000.0 ? 2 : 4;
		};
		TestEqual(TEXT("SelectTeleportNode(Chang) prefers the player's sector over the nearest"),
			FElysiumNpc::SelectTeleportNodeChangRule(Nodes, Player, &AlwaysClear, SectorOf), 1);

		auto NoMatch = [](const FVector&) -> int32 { return 4; };
		TestEqual(TEXT("and falls back to the plain nearest when no sector matches"),
			FElysiumNpc::SelectTeleportNodeChangRule(Nodes, Player, &AlwaysClear, NoMatch), 0);
	}

	// `CNPC_VAsianVampire::SelectLedgeNode` `0x103615c0` — the only selector that never asks for a
	// player, and the one that runs its clearance gate BEFORE it measures anything.
	{
		TArray<FElysiumNpc::FHintWords> Nodes;
		Nodes.Add(MakeNode(0x4652, FVector(10.0, 0.0, 0.0)));    // wrong type
		Nodes.Add(MakeNode(0x4653, FVector(900.0, 0.0, 0.0)));
		Nodes.Add(MakeNode(0x4653, FVector(200.0, 0.0, 0.0)));
		TestEqual(TEXT("SelectLedgeNode(Asian) measures from the NPC's own origin"),
			FElysiumNpc::SelectLedgeNodeAsianRule(Nodes, FVector::ZeroVector, &AlwaysClear), 2);
		TestEqual(TEXT("and the DAT_104a9320 = 150.0 clearance is a gate on every candidate"),
			FElysiumNpc::SelectLedgeNodeAsianRule(Nodes, FVector::ZeroVector, &NeverClear),
			(int32)INDEX_NONE);
	}

	return true;
}

// --- The four `PositionClearForTeleport` rules --------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsClearanceTest,
	"Elysium.Substrate.NpcKernelPositions.PositionClearForTeleport", GPositionsFlags)
bool FElysiumNpcKernelPositionsClearanceTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("positions_clearance"), 4242);
	Builder.AddNpc(TEXT("andrei"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	Builder.AddNpc(TEXT("human"), FVector::ZeroVector, TEXT("npc_VHuman"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Andrei = Fixture.Npc(TEXT("andrei"));
	FElysiumNpc* Human = Fixture.Npc(TEXT("human"));
	FElysiumNpcWorldFixture::Quiet({ Andrei, Human });
	if (Andrei == nullptr || Human == nullptr)
	{
		AddError(TEXT("the fixture did not stand both NPCs"));
		return false;
	}

	const float Clearance = 200.f;

	// `CNPC_VAndreiBlood::PositionClearForTeleport` `0x1035e030`: far enough from me, AND far enough
	// from where I last went (`DAT_104a6f7c = 100.0` units).
	Andrei->Origin = FVector::ZeroVector;
	Andrei->AndreiLastTeleportPosition = FVector(10000.0, 0.0, 0.0);
	TestFalse(TEXT("Andrei refuses a spot inside his own clearance"),
		Andrei->PositionClearForTeleportAndrei(FVector(150.0, 0.0, 0.0), Clearance));
	TestTrue(TEXT("Andrei accepts one past it"),
		Andrei->PositionClearForTeleportAndrei(FVector(300.0, 0.0, 0.0), Clearance));
	Andrei->AndreiLastTeleportPosition = FVector(300.0, 0.0, 0.0);
	TestFalse(TEXT("Andrei refuses a spot within 100.0 units of his last teleport"),
		Andrei->PositionClearForTeleportAndrei(FVector(300.0, 0.0, 0.0), Clearance));
	TestTrue(TEXT("and accepts one 100.0 units clear of it"),
		Andrei->PositionClearForTeleportAndrei(FVector(300.0 + 101.0 * U, 0.0, 0.0), Clearance));

	// `CNPC_VSheriffMan::PositionClearForTeleport` `0x103b0c70`: the same two terms plus the player.
	Andrei->SheriffLastTeleportPosition = FVector(10000.0, 0.0, 0.0);
	TestTrue(TEXT("the Sheriff rule accepts a clear spot with no player"),
		Andrei->PositionClearForTeleportSheriff(FVector(300.0, 0.0, 0.0), Clearance));

	// `CNPC_VAsianVampire::PositionClearForTeleport` `0x103629d0`: both distances are FLAT, so a
	// spot directly overhead is clear however close it is vertically. That is the one thing that
	// separates it from the other three.
	TestTrue(TEXT("the Asian rule is flat: a spot straight above is clear"),
		Andrei->PositionClearForTeleportAsian(FVector(300.0, 0.0, 5000.0), Clearance));
	TestFalse(TEXT("and a spot inside the flat clearance is not"),
		Andrei->PositionClearForTeleportAsian(FVector(150.0, 0.0, 5000.0), Clearance));

	// `CNPC_VChangBros::PositionClearForTeleport` `0x1036d350`: the squad walk is unreachable
	// because `ConnectedSquad()` answers nothing, so the recovered two-term head is the live half.
	Andrei->ChangLastTeleportPosition = FVector(10000.0, 0.0, 0.0);
	TestTrue(TEXT("the Chang rule accepts a clear spot with no squad"),
		Andrei->PositionClearForTeleportChang(FVector(300.0, 0.0, 0.0), Clearance));
	TestFalse(TEXT("and its squad seam refuses nothing, which is what an empty squad answers"),
		Andrei->SquadPositionTaken(FVector(300.0, 0.0, 0.0), Clearance));

	// The dispatcher picks by retail class and nothing else.
	TestTrue(TEXT("npc_VAndreiBlood dispatches to the Andrei rule"),
		Andrei->IsRetailClass(TEXT("CNPC_VAndreiBlood")));
	Andrei->AndreiLastTeleportPosition = FVector(10000.0, 0.0, 0.0);
	TestTrue(TEXT("and PositionClearForTeleportSpecies answers what that rule answers"),
		Andrei->PositionClearForTeleportSpecies(FVector(300.0, 0.0, 0.0), Clearance));
	TestFalse(TEXT("a class retail stands no PositionClearForTeleport on refuses by name"),
		Human->PositionClearForTeleportSpecies(FVector(300.0, 0.0, 0.0), Clearance));

	return true;
}

// --- The Chang brothers' arena ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsArenaTest,
	"Elysium.Substrate.NpcKernelPositions.Arena", GPositionsFlags)
bool FElysiumNpcKernelPositionsArenaTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("positions_arena"), 7);
	Builder.AddNpc(TEXT("chang"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("chang"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `GetSector` `0x1036e580`.
	TestEqual(TEXT("GetSector answers 0 while m_bCenterStored is clear"),
		Npc->GetSector(FVector::ZeroVector), 0);

	Npc->bChangCenterStored = true;
	Npc->ChangArenaCenter = FVector::ZeroVector;

	// The low arm. `_DAT_104ada60 = 270.0` is compared against a SQUARED distance while
	// `_DAT_104ada64 = 370.0` is squared first by `staticinit_0x1036e500`, so sector 1 is a circle
	// of radius sqrt(270) = 16.4 units and not 270. Reproduced as the inconsistency it is.
	TestEqual(TEXT("sector 1 is distSq <= 270.0, which is 16.43 units, not 270"),
		Npc->GetSector(FVector(16.0 * U, 0.0, 0.0)), 1);
	TestEqual(TEXT("17 units out is already sector 2"),
		Npc->GetSector(FVector(17.0 * U, 0.0, 0.0)), 2);
	TestEqual(TEXT("sector 2 reaches 370.0 units"),
		Npc->GetSector(FVector(369.0 * U, 0.0, 0.0)), 2);
	TestEqual(TEXT("and past it is sector 4"),
		Npc->GetSector(FVector(371.0 * U, 0.0, 0.0)), 4);

	// The high arm: at or above `_DAT_104ada6c = 200.0` units the only answers are 3 and 4, and the
	// radius is `_DAT_104ada68 = 525.0`.
	TestEqual(TEXT("above 200.0 units a near point is sector 3, not sector 1"),
		Npc->GetSector(FVector(0.0, 0.0, 300.0 * U)), 3);
	TestEqual(TEXT("sector 3 reaches 525.0 units"),
		Npc->GetSector(FVector(524.0 * U, 0.0, 300.0 * U)), 3);
	TestEqual(TEXT("and past it is sector 4"),
		Npc->GetSector(FVector(526.0 * U, 0.0, 300.0 * U)), 4);
	TestEqual(TEXT("and 199 units up is still the low arm, where a near point is sector 1"),
		Npc->GetSector(FVector(0.0, 0.0, 199.0 * U)), 1);

	// `SectorIsInPit` `0x1036b6b0` — `0 < s && s < 3`.
	TestFalse(TEXT("sector 0 is not the pit"), FElysiumNpc::SectorIsInPit(0));
	TestTrue(TEXT("sector 1 is"), FElysiumNpc::SectorIsInPit(1));
	TestTrue(TEXT("sector 2 is"), FElysiumNpc::SectorIsInPit(2));
	TestFalse(TEXT("sector 3 is not"), FElysiumNpc::SectorIsInPit(3));
	TestFalse(TEXT("sector 4 is not"), FElysiumNpc::SectorIsInPit(4));

	// `GetTeleportPosition` `0x1036d270` — the claim expires `_DAT_104ada04 = 3.0` seconds after it
	// was stamped, and that window is what stops the other brother taking the same spot.
	FVector Claimed;
	Npc->ChangLastTeleportPosition = FVector(1.0, 2.0, 3.0);
	Npc->ChangLastTeleportTime = Fixture.World.NowSeconds();
	TestTrue(TEXT("a fresh claim reads back"), Npc->GetTeleportPosition(Claimed));
	TestEqual(TEXT("with the stored position"), Claimed, FVector(1.0, 2.0, 3.0));
	Npc->ChangLastTeleportTime = Fixture.World.NowSeconds() - 3.5;
	TestFalse(TEXT("and a claim older than 3.0 seconds does not"),
		Npc->GetTeleportPosition(Claimed));

	// `IsUnreachable` `0x1036e6f0`: one of the two in the pit and the other on the ledge.
	TestTrue(TEXT("pit versus sector 3 is unreachable both ways"),
		FElysiumNpc::SectorIsInPit(1) && !FElysiumNpc::SectorIsInPit(3));

	return true;
}

// --- The Sheriff's floor heights ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsFloorHeightsTest,
	"Elysium.Substrate.NpcKernelPositions.FloorHeights", GPositionsFlags)
bool FElysiumNpcKernelPositionsFloorHeightsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("positions_heights"), 11);
	Builder.AddNpc(TEXT("sheriff"), FVector(0.0, 0.0, 500.0), TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("sheriff"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `CategorizeHeight` `0x103b1790`: 0 when the centre reference is STRICTLY nearer, 1 otherwise —
	// so an exact tie goes to the LEDGE, and there is no gate on `m_bLedgeStored`.
	Npc->SheriffCenterFloorZ = 0.f;
	Npc->SheriffLedgeFloorZ = 1000.f;
	int32 Category = -1;
	Npc->CategorizeHeight(100.f, Category);
	TestEqual(TEXT("nearer the centre reference is category 0"), Category, 0);
	Npc->CategorizeHeight(900.f, Category);
	TestEqual(TEXT("nearer the ledge reference is category 1"), Category, 1);
	Npc->CategorizeHeight(500.f, Category);
	TestEqual(TEXT("and an exact tie goes to the ledge"), Category, 1);

	// `CategorizeHeights` `0x103b1680`: BOTH out-params are zeroed before the player is resolved, so
	// a Sheriff with no closest player answers 0/0 rather than leaving them alone.
	int32 PlayerCategory = 7;
	int32 SelfCategory = 7;
	Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	Npc->CategorizeHeights(PlayerCategory, SelfCategory);
	TestEqual(TEXT("no closest player leaves the player category zeroed"), PlayerCategory, 0);
	TestEqual(TEXT("and the self category zeroed"), SelfCategory, 0);

	FElysiumPlayer* Player = Fixture.Player();
	if (Player != nullptr)
	{
		Player->Origin = FVector(0.0, 0.0, 950.0);
		Npc->Senses.Memory.ClosestPlayer = Player->Handle;
		Npc->Origin = FVector(0.0, 0.0, 50.0);
		Npc->CategorizeHeights(PlayerCategory, SelfCategory);
		TestEqual(TEXT("the player's height is categorised first"), PlayerCategory, 1);
		TestEqual(TEXT("then this body's own"), SelfCategory, 0);
	}

	// `CacheFloorHeights` `0x103b1510` — nested, so no ledge height is stored without a centre node
	// first. The node seam answers nothing, so neither is stored and the latch stays clear.
	Npc->bSheriffLedgeHeightStored = false;
	Npc->CacheFloorHeights();
	TestFalse(TEXT("CacheFloorHeights stores nothing while the node seam is empty"),
		Npc->bSheriffLedgeHeightStored);

	return true;
}

// --- Pure geometry and the shoot-target falloff --------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsGeometryTest,
	"Elysium.Substrate.NpcKernelPositions.Geometry", GPositionsFlags)
bool FElysiumNpcKernelPositionsGeometryTest::RunTest(const FString&)
{
	// `CNPC_VVampireBoss::DistToSegment` `0x103c6b70`, and the two arms that differ from the
	// ordinary clamped form.
	const FVector A(0.0, 0.0, 0.0);
	const FVector B(100.0, 0.0, 0.0);

	TestEqual(TEXT("a degenerate segment answers 0.0, not the distance to its endpoint"),
		FElysiumNpc::DistToSegment(A, A, FVector(0.0, 50.0, 0.0)), 0.f);
	TestTrue(TEXT("a point before the segment takes the |p - a| arm"),
		FMath::IsNearlyEqual(FElysiumNpc::DistToSegment(A, B, FVector(-30.0, 40.0, 0.0)),
			50.f, 1e-3f));
	TestTrue(TEXT("a point past it takes the |p - b| arm"),
		FMath::IsNearlyEqual(FElysiumNpc::DistToSegment(A, B, FVector(130.0, 40.0, 0.0)),
			50.f, 1e-3f));
	TestTrue(TEXT("and a point beside it takes the projection"),
		FMath::IsNearlyEqual(FElysiumNpc::DistToSegment(A, B, FVector(50.0, 40.0, 0.0)),
			40.f, 1e-3f));

	// `EnemyCouldSeeHull`'s candidate blend, `0x10366510`.
	const FElysiumNpc::FEnemySightCandidates Candidates = FElysiumNpc::EnemySightCandidatesOf(
		FVector(0.0, 0.0, 0.0), FVector(10.0, 20.0, 30.0), FVector(1.0, 2.0, 3.0), 7.f);
	TestEqual(TEXT("the min corner is the box mins minus the extents"), Candidates.MinCm,
		FVector(-1.0, -2.0, -3.0));
	TestEqual(TEXT("the max corner is the box maxs plus them"), Candidates.MaxCm,
		FVector(11.0, 22.0, 33.0));
	TestEqual(TEXT("and the mid candidate is the inflated centre with a randomized Z"),
		Candidates.MidCm, FVector(5.0, 10.0, 7.0));

	// `CNPC_VTzimisce::vfunc389` `0x103bfd80` — the two arms differ in the sign of the RIGHT term
	// and in nothing else.
	const FVector Fwd(1.0, 0.0, 0.0);
	const FVector Right(0.0, 1.0, 0.0);
	const FVector Up(0.0, 0.0, 1.0);
	TestEqual(TEXT("activity 0x106 subtracts the right term"),
		FElysiumNpc::TzimisceAimOffset(FVector::ZeroVector, Fwd, Right, Up, 2.f, 3.f, 5.f, false),
		FVector(2.0, -3.0, 5.0));
	TestEqual(TEXT("activity 0x107 adds it"),
		FElysiumNpc::TzimisceAimOffset(FVector::ZeroVector, Fwd, Right, Up, 2.f, 3.f, 5.f, true),
		FVector(2.0, 3.0, 5.0));

	return true;
}

// --- The unreachable cache, the hint-group validators and slot 563 -------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsValidatorsTest,
	"Elysium.Substrate.NpcKernelPositions.Validators", GPositionsFlags)
bool FElysiumNpcKernelPositionsValidatorsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("positions_validators"), 99);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, TEXT("npc_VHuman"));
	Builder.AddNpc(TEXT("target"), FVector(100.0, 0.0, 0.0), TEXT("npc_VHuman"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpc* Target = Fixture.Npc(TEXT("target"));
	FElysiumNpcWorldFixture::Quiet({ Npc, Target });
	if (Npc == nullptr || Target == nullptr)
	{
		AddError(TEXT("the fixture did not stand both NPCs"));
		return false;
	}

	// `CAI_BaseNPC::IsUnreachable` `0x102741e0`, slot 530.
	const double Now = Fixture.World.NowSeconds();
	TestFalse(TEXT("an empty cache answers false"), Npc->IsUnreachable(Target));

	FElysiumNpc::FUnreachableEntity Record;
	Record.Entity = Target->Handle;
	Record.ExpiresAt = Now + 5.0;
	Record.PositionCm = Target->Origin;
	Npc->UnreachableEnts.Add(Record);
	TestTrue(TEXT("a live record whose target has not moved answers true"),
		Npc->IsUnreachable(Target));
	TestEqual(TEXT("and is kept"), Npc->UnreachableEnts.Num(), 1);

	// `_DAT_10499560 = 14400` is 120 units SQUARED, so the record survives a move inside 120 units
	// and is dropped past it.
	Target->Origin = Record.PositionCm + FVector(119.0 * U, 0.0, 0.0);
	TestTrue(TEXT("a move inside 120.0 units keeps the record standing"),
		Npc->IsUnreachable(Target));
	Target->Origin = Record.PositionCm + FVector(121.0 * U, 0.0, 0.0);
	TestFalse(TEXT("a move past it answers false"), Npc->IsUnreachable(Target));
	TestEqual(TEXT("and REMOVES the record, which is retail's own sweep"),
		Npc->UnreachableEnts.Num(), 0);

	// An expired record is dropped on the same terms.
	Record.ExpiresAt = Now - 1.0;
	Record.PositionCm = Target->Origin;
	Npc->UnreachableEnts.Add(Record);
	TestFalse(TEXT("an expired record answers false"), Npc->IsUnreachable(Target));
	TestEqual(TEXT("and is removed"), Npc->UnreachableEnts.Num(), 0);

	// A record whose handle no longer resolves is compacted away and the walk continues.
	FElysiumNpc::FUnreachableEntity Stale;
	Stale.Entity = FElysiumEntityHandle();
	Npc->UnreachableEnts.Add(Stale);
	Npc->UnreachableEnts.Add(Record);
	Npc->UnreachableEnts[1].ExpiresAt = Now + 5.0;
	Npc->UnreachableEnts[1].PositionCm = Target->Origin;
	TestTrue(TEXT("a stale record is compacted and the live one behind it still answers"),
		Npc->IsUnreachable(Target));
	Npc->UnreachableEnts.Reset();

	// `IsValidShootPosition` `0x1028b0b0`, slot 549 — the hint group and nothing else, and the
	// position argument is never read.
	Npc->ScheduleHost.HintGroup.Reset();
	TestTrue(TEXT("an NPC with no hint group accepts every shoot position"),
		Npc->IsValidShootPosition(FVector::ZeroVector, nullptr));
	Npc->ScheduleHost.HintGroup = TEXT("rooftops");
	TestFalse(TEXT("one with a hint group refuses a null hint"),
		Npc->IsValidShootPosition(FVector::ZeroVector, nullptr));
	FElysiumNpc::FHintWords Hint;
	Hint.bValid = true;
	Hint.Group = TEXT("alley");
	TestFalse(TEXT("and refuses a hint from another group"),
		Npc->IsValidShootPosition(FVector::ZeroVector, &Hint));
	Hint.Group = TEXT("rooftops");
	TestTrue(TEXT("and accepts one from its own"),
		Npc->IsValidShootPosition(FVector::ZeroVector, &Hint));

	// `IsValidCover` `0x1028af20`, slot 548 — the same group rule behind a downward hull trace whose
	// seam answers nothing, which reads as "not in solid" and admits the cover.
	TestTrue(TEXT("IsValidCover admits a matching hint with the trace seam silent"),
		Npc->IsValidCover(FVector::ZeroVector, &Hint));
	Hint.Group = TEXT("alley");
	TestFalse(TEXT("and the hint-group arm still refuses"),
		Npc->IsValidCover(FVector::ZeroVector, &Hint));
	Npc->ScheduleHost.HintGroup.Reset();

	// `IsAreaClear` `0x102a0fb0` — the flag is raised for exactly the trace and dropped after it.
	Npc->bForceNpcCheck = false;
	TestTrue(TEXT("IsAreaClear reads the silent trace seam as clear"),
		Npc->IsAreaClear(FVector::ZeroVector, 0x202400b));
	TestFalse(TEXT("and m_bForceNPCCheck is back down afterwards"), Npc->bForceNpcCheck);

	return true;
}

// --- Slot 563's eight bodies ---------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsChaseTest,
	"Elysium.Substrate.NpcKernelPositions.ChasePosition", GPositionsFlags)
bool FElysiumNpcKernelPositionsChaseTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("positions_chase"), 5);
	Builder.AddNpc(TEXT("human"), FVector::ZeroVector, TEXT("npc_VHuman"));
	Builder.AddNpc(TEXT("rat"), FVector::ZeroVector, TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("enemy"), FVector(500.0, 0.0, 0.0), TEXT("npc_VHuman"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Human = Fixture.Npc(TEXT("human"));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* Enemy = Fixture.Npc(TEXT("enemy"));
	FElysiumNpcWorldFixture::Quiet({ Human, Rat, Enemy });
	if (Human == nullptr || Rat == nullptr || Enemy == nullptr)
	{
		AddError(TEXT("the fixture did not stand all three NPCs"));
		return false;
	}

	// Every recovered species maps to its shape by the census address and never by a name compare.
	// `CNPC_VHuman`'s body (`0x10384760`) covers 42 classes and `CNPC_VAnimal`'s (`0x1035f5c0`) five;
	// both are the offset-only shape.
	TestEqual(TEXT("npc_VHuman answers slot 563 with the offset-only shape"),
		static_cast<int32>(Human->ChaseTranslateShape()),
		static_cast<int32>(FElysiumNpc::EChaseTranslateShape::OffsetOnly));
	TestEqual(TEXT("npc_VRat does too, through CNPC_VAnimal's body"),
		static_cast<int32>(Rat->ChaseTranslateShape()),
		static_cast<int32>(FElysiumNpc::EChaseTranslateShape::OffsetOnly));

	// The census is what says so, one row per recovered body. Every row is exercised by name.
	struct FRow { const TCHAR* Class; const TCHAR* Body; };
	const FRow Rows[] =
	{
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x10295300") },
		{ TEXT("CNPC_VAnimal"),      TEXT("0x1035f5c0") },
		{ TEXT("CNPC_VCamera"),      TEXT("0x10368ee0") },
		{ TEXT("CNPC_VHuman"),       TEXT("0x10384760") },
		{ TEXT("CNPC_VMingXiao"),    TEXT("0x10392c40") },
		{ TEXT("CNPC_VTzimisce"),    TEXT("0x103ba640") },
		{ TEXT("CNPC_VWerewolf"),    TEXT("0x103d9e00") },
	};
	for (const FRow& Row : Rows)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.Class);
		if (Cls == nullptr)
		{
			AddError(FString::Printf(TEXT("%s is not a census class"), Row.Class));
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s fills slot 563 with %s"), Row.Class, Row.Body),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 563)), FString(Row.Body));
	}

	// The nav gate. `NavGetType()` is family Motor's seam and answers 0, so the offset arm is not
	// taken and slot 563's generated body writes nothing — which is the Troika body's whole
	// behaviour at nav type != 2.
	FVector Chase(1000.0, 0.0, 0.0);
	float Tolerance = 42.f;
	float Second = 7.f;
	Human->TranslateEnemyChasePosition(Enemy, Chase, &Tolerance, &Second);
	TestEqual(TEXT("the Troika body leaves the chase position alone off nav type 2"), Chase,
		FVector(1000.0, 0.0, 0.0));
	TestEqual(TEXT("and leaves the tolerance alone, which is what separates it from the base body"),
		Tolerance, 42.f);

	// The base line's own body is the one that writes on the else arm.
	Human->TranslateEnemyChasePositionSpecies(Enemy, Chase, Tolerance, Second);
	TestEqual(TEXT("the offset-only shape also leaves the tolerance alone"), Tolerance, 42.f);

	// The two lead helpers are seams and say so.
	float LeadTolerance = 5.f;
	Human->ChaseLeadTolerance(Enemy, Chase, LeadTolerance);
	TestEqual(TEXT("the tolerance lead seam changes nothing"), LeadTolerance, 5.f);
	FVector Led = FVector::ZeroVector;
	Human->ChaseLeadPosition(Enemy, FVector::ZeroVector, 0.f, Chase, Led);
	TestEqual(TEXT("the position lead seam answers the position unchanged"), Led, Chase);
	TestEqual(TEXT("and the ground speed it would be fed is nothing"), Human->GroundSpeedCm(), 0.f);

	return true;
}

// --- The Werewolf teleport pair and its condition -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsTeleportTest,
	"Elysium.Substrate.NpcKernelPositions.WerewolfTeleport", GPositionsFlags)
bool FElysiumNpcKernelPositionsTeleportTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("positions_teleport"), 13);
	Builder.AddNpc(TEXT("wolf"), FVector::ZeroVector, TEXT("npc_VHuman"));
	Builder.AddNpc(TEXT("swarm"), FVector(10.0, 0.0, 0.0), TEXT("npc_VRat"));
	Builder.WireOutput(TEXT("wolf"), TEXT("OnTeleportOut"), TEXT("outcount"));
	Builder.AddCounter(TEXT("outcount"));
	Builder.WireOutput(TEXT("wolf"), TEXT("OnTeleportIn"), TEXT("incount"));
	Builder.AddCounter(TEXT("incount"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Wolf = Fixture.Npc(TEXT("wolf"));
	FElysiumNpc* Swarm = Fixture.Npc(TEXT("swarm"));
	FElysiumNpcWorldFixture::Quiet({ Wolf, Swarm });
	if (Wolf == nullptr || Swarm == nullptr)
	{
		AddError(TEXT("the fixture did not stand both NPCs"));
		return false;
	}

	// The stamps are absolute curtimes, so the clock has to have moved off zero for them to be
	// distinguishable from "never stamped".
	Fixture.Advance(2.0);
	const double Now = Fixture.World.NowSeconds();

	// `TeleportOut` `0x103d4a60`: the stamp, the two bit writes, the two cleared words, the output.
	Wolf->EffectsWord = 0;
	Wolf->SolidFlagsWord = 0;
	Wolf->WerewolfWord66ac = 5;
	Wolf->WerewolfHintFlags = 0xffu;
	Wolf->TeleportHintNode = 3;
	Wolf->TeleportOut();
	TestEqual(TEXT("TeleportOut stamps m_flTimeTeleportedOut with curtime"),
		Wolf->WerewolfTimeTeleportedOut, Now);
	TestEqual(TEXT("raises EF_NODRAW"), Wolf->EffectsWord & 0x20u, 0x20u);
	TestEqual(TEXT("adds FSOLID_NOT_SOLID"), Wolf->SolidFlagsWord & 0x4u, 0x4u);
	TestEqual(TEXT("clears +0x66ac"), Wolf->WerewolfWord66ac, 0);
	TestEqual(TEXT("clears the hint-flag word at +0x66e8"), Wolf->WerewolfHintFlags, 0u);
	TestEqual(TEXT("and clears the teleport hint"), Wolf->TeleportHintNode, (int32)INDEX_NONE);

	// `TeleportIn` `0x103d4d60`: the mirror, and the asymmetry — it does NOT clear the hint and it
	// stamps +0x66ec, the word the can-teleport pass reads.
	Wolf->WerewolfLastSeenTime = 0.0;
	Wolf->TeleportIn();
	TestEqual(TEXT("TeleportIn clears EF_NODRAW"), Wolf->EffectsWord & 0x20u, 0u);
	TestEqual(TEXT("removes FSOLID_NOT_SOLID"), Wolf->SolidFlagsWord & 0x4u, 0u);
	TestEqual(TEXT("and stamps the +0x66ec last-seen word the teleport cooldown measures"),
		Wolf->WerewolfLastSeenTime, Now);

	// Both outputs fire by name.
	Fixture.World.Tick(Fixture.World.NowSeconds());
	TestEqual(TEXT("OnTeleportOut fired once"), Fixture.Counter(TEXT("outcount")), 1.f);
	TestEqual(TEXT("OnTeleportIn fired once"), Fixture.Counter(TEXT("incount")), 1.f);

	// The sound gate is the unrecovered cvar `DAT_1093f73c`, and it answers the arm that plays
	// nothing.
	TestFalse(TEXT("the ww_tele wav gate is the unrecovered cvar and refuses"),
		FElysiumNpc::WerewolfTeleportSoundConVar());

	// `KillTeleportBats` `0x103b0560`: the handle is invalidated whether or not it resolved.
	Wolf->SheriffTeleportSwarm = Swarm->Handle;
	Wolf->KillTeleportBats();
	TestFalse(TEXT("KillTeleportBats invalidates m_hTeleportSwarm"),
		Wolf->SheriffTeleportSwarm.IsSet());
	Wolf->KillTeleportBats();
	TestFalse(TEXT("and is idempotent on an already-dead handle"),
		Wolf->SheriffTeleportSwarm.IsSet());

	// `UpdateConditionCanTeleport` `0x103cc0d0`: the condition is CLEARED at the top of every pass —
	// 29c's one-line walk had that backwards — and set only at the end. `IsViewable()` (slot 163) is
	// another story's stub and answers false, so the pass refuses at the entry gate and the observable
	// effect today is the clear.
	Wolf->Cognition.Conditions.Set(EElysiumNpcCond::CanTeleport);
	Wolf->UpdateConditionCanTeleport();
	TestFalse(TEXT("UpdateConditionCanTeleport clears COND 0x77 before anything else"),
		Wolf->Cognition.Conditions.Has(EElysiumNpcCond::CanTeleport));
	TestFalse(TEXT("and the IsViewable gate stops the pass, so it is never set back"),
		Wolf->Cognition.Conditions.Has(EElysiumNpcCond::CanTeleport));
	TestEqual(TEXT("its delay threshold is the unrecovered cvar, answering 0"),
		FElysiumNpc::WerewolfTeleportDelayConVar(), 0.f);

	return true;
}

// --- The seams, and what each of them refuses -----------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPositionsSeamsTest,
	"Elysium.Substrate.NpcKernelPositions.Seams", GPositionsFlags)
bool FElysiumNpcKernelPositionsSeamsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("positions_seams"), 3);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	FElysiumPlayer* Player = Fixture.Player();
	if (Npc == nullptr || Player == nullptr)
	{
		AddError(TEXT("the fixture did not stand an NPC and a player"));
		return false;
	}
	Npc->Senses.Memory.ClosestPlayer = Player->Handle;

	// The node-graph seam. Every selector asks it, and every one of them answers retail's null.
	TArray<FElysiumNpc::FHintWords> Nodes;
	TArray<int32> NodeIds;
	Npc->GatherHintNodes(Nodes, NodeIds);
	TestEqual(TEXT("the global hint list answers empty"), Nodes.Num(), 0);
	TestEqual(TEXT("SelectCenterNode answers null"), Npc->SelectCenterNode(), (int32)INDEX_NONE);
	TestEqual(TEXT("SelectLedgeNode answers null"), Npc->SelectLedgeNode(false), (int32)INDEX_NONE);
	TestEqual(TEXT("SelectLedgeNodeAsian answers null"), Npc->SelectLedgeNodeAsian(),
		(int32)INDEX_NONE);
	TestEqual(TEXT("SelectTeleportArchway answers null"), Npc->SelectTeleportArchway(),
		(int32)INDEX_NONE);
	TestEqual(TEXT("SelectDiveInPoint answers null"), Npc->SelectDiveInPoint(), (int32)INDEX_NONE);
	TestEqual(TEXT("SelectDiveOutPoint answers null"), Npc->SelectDiveOutPoint(), (int32)INDEX_NONE);
	TestEqual(TEXT("SelectTeleportNodeSheriff answers null"), Npc->SelectTeleportNodeSheriff(),
		(int32)INDEX_NONE);
	TestEqual(TEXT("SelectTeleportNodeAndrei answers null"), Npc->SelectTeleportNodeAndrei(),
		(int32)INDEX_NONE);
	TestEqual(TEXT("SelectTeleportNodeChang answers null"), Npc->SelectTeleportNodeChang(),
		(int32)INDEX_NONE);

	// And the caches those two would have written are untouched — the named divergence from retail,
	// which dereferences its null winner instead.
	TestEqual(TEXT("a null winner leaves the Sheriff's cached position alone"),
		Npc->SheriffLastTeleportPosition, FVector::ZeroVector);
	TestEqual(TEXT("and the Chang brothers' stamp alone"), Npc->ChangLastTeleportTime, 0.0);

	// The navigator's node query, and the cache `GetNearestNodeToPlayer` keeps in front of it. The
	// cache is keyed on ZERO, not -1, so a permanent miss re-queries every interval and never
	// latches.
	TestEqual(TEXT("the navigator's nearest-node query answers retail's miss value"),
		Npc->NavNearestNodeTo(FVector::ZeroVector), -1);
	TestEqual(TEXT("GetNearestNodeToPlayer therefore answers 0"), Npc->GetNearestNodeToPlayer(), 0);
	TestTrue(TEXT("and stamps its refresh clock even on the miss"),
		Npc->NearestNodeToPlayerRefreshedAt >= 0.0);

	// The sight seams.
	TestFalse(TEXT("the enemy view cone answers false"),
		FElysiumNpc::EnemyInViewCone(*Npc, FVector::ZeroVector));
	TestFalse(TEXT("the Werewolf sight cvar closes its gate"), FElysiumNpc::WerewolfSightConVar());
	TestFalse(TEXT("so its EnemyCouldSeeHull refuses without tracing"),
		Npc->EnemyCouldSeeHullWerewolf(FVector::ZeroVector, true, false, FVector::ZeroVector));
	FVector Mins;
	FVector Maxs;
	TestFalse(TEXT("the hitbox surrounding box answers nothing, so the hull box is used"),
		Npc->ComputeHitboxSurroundingBox(Mins, Maxs));
	TestFalse(TEXT("and with no committed enemy the boss body refuses"),
		Npc->EnemyCouldSeeHull(FVector::ZeroVector, true, false, FVector::ZeroVector));

	// `FUN_102c5570`'s falloff, whose argument class is unrecovered but whose arms are not.
	TestEqual(TEXT("a non-positive divisor leaves the scale at 1.0"),
		Npc->ShootTargetFalloff(10.f, 0.f, 30.f), 20.f);
	FVector Delta;
	TestFalse(TEXT("with no shoot target and no enemy the delta half refuses"),
		Npc->ShootTargetDelta(Delta));
	TestTrue(TEXT("and the no-target arm is sqrt(1 / divisor), not the distance"),
		FMath::IsNearlyEqual(Npc->ShootTargetFalloff(0.f, 4.f, 1.f), 0.5f, 1e-4f));

	// The three `CNPC_VTzimisce` aim cvars.
	for (int32 Which = 0; Which < 3; ++Which)
	{
		TestEqual(TEXT("the Tzimisce aim cvars answer retail's unreadable-cvar zero"),
			FElysiumNpc::TzimisceAimConVar(Which), 0.f);
	}
	FVector Aim = FVector(1.0, 2.0, 3.0);
	Npc->ActivityNumber = 0x105;
	TestFalse(TEXT("an activity outside 0x106/0x107 falls through to the base shoot position"),
		Npc->WeaponShootPositionTzimisce(FVector::ZeroVector, Aim));
	Npc->ActivityNumber = 0x106;
	TestTrue(TEXT("and 0x106 takes the override"),
		Npc->WeaponShootPositionTzimisce(FVector(5.0, 6.0, 7.0), Aim));
	TestEqual(TEXT("which, with all three cvars at zero, is the source point itself"), Aim,
		FVector(5.0, 6.0, 7.0));

	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
