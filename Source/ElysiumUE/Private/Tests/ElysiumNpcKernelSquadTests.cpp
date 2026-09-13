#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Squad**. The assertions come from the decompiled C of the 74 rows, not from
// the fact that a call returned: the slot-546 id spaces, `InitSquad`'s gate order, `SharesSquadWith`'s
// asymmetric null arm, the follower clamp's two thresholds, `IRelationType`'s four answers,
// `AlertNearbyAlly`'s five gates and `CoordinateTroops`'s wrap at 5.
//
// Where a body can only answer "nothing" because its input is a seam — every body that reaches
// `m_pSquad` — the case says so: that the seam is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelSquadFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// -------------------------------------------------------------------------------------------------
// Slot 546: the 61-row species table, every row by name.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadSlotNameTableTest,
	"Elysium.Substrate.NpcKernelSquad.SquadSlotNameTable", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadSlotNameTableTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FSquadSlotSpecies* Rows = FElysiumNpc::SquadSlotSpeciesRows(Count);
	TestEqual(TEXT("the table carries the Troika line plus 60 census override classes"), Count, 61);

	// Every row, by name: the class resolves in the census, the census says the same body fills
	// slot 546 for it, and the recovered id space translates NOTHING — the local range is the 9999
	// "empty" sentinel on all 56 species, so every id answers `<<null>>`.
	TSet<FString> Bodies;
	TSet<FString> IdSpaces;
	int32 SpeciesRows = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpc::FSquadSlotSpecies& Row = Rows[Index];
		const FString Name(Row.RetailClass);
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), *Name), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot 546 with %s"), *Name, Row.Body),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 546)), FString(Row.Body));
		Bodies.Add(FString(Row.Body));

		if (FCString::Strcmp(Row.RetailClass, TEXT("CAI_BaseNPCTroika")) == 0)
		{
			// The Troika line performs no translation at all: `0x101a6c00` hands `slotEN` straight
			// to `IdToSymbol`, so the two shipped ids answer their names.
			TestEqual(TEXT("CAI_BaseNPCTroika carries no id space"), FString(Row.IdSpace),
				FString());
			TestEqual(TEXT("and does not translate"),
				FElysiumNpc::SquadSlotLocalToGlobal(&Row, 1000000000), 1000000000);
			continue;
		}

		++SpeciesRows;
		TestFalse(*FString::Printf(TEXT("%s names its own CAI_ClassScheduleIdSpace"), *Name),
			FString(Row.IdSpace).IsEmpty());
		IdSpaces.Add(FString(Row.IdSpace));
		TestEqual(*FString::Printf(TEXT("%s's local range is the 9999 empty sentinel"), *Name),
			Row.LocalBase, 9999);
		// The recovered answer for every id, on every species: -1, then `<<null>>`.
		for (const int32 LocalId : { -1, 0, 1, 7, 1000000000 })
		{
			TestEqual(*FString::Printf(TEXT("%s translates %d to -1"), *Name, LocalId),
				FElysiumNpc::SquadSlotLocalToGlobal(&Row, LocalId), INDEX_NONE);
		}
		TestEqual(*FString::Printf(TEXT("%s answers <<null>> for slot 0"), *Name),
			FString(FElysiumNpc::GlobalSquadSlotName(
				FElysiumNpc::SquadSlotLocalToGlobal(&Row, 0))),
			FString(TEXT("<<null>>")));
	}
	TestEqual(TEXT("60 species rows"), SpeciesRows, 60);
	TestEqual(TEXT("across 57 distinct retail bodies (56 species + the Troika line)"), Bodies.Num(),
		57);
	// `CNPC_ProneDialog`/`CNPC_VHumanCombatant`, `CNPC_VCamera`/`CNPC_VCameraSecurity`,
	// `CNPC_VRat`/`CNPC_VScurrying` and `CNPC_VPlayerController`/`CNPC_VVampire` each share a body,
	// and a shared body shares its id space too.
	TestEqual(TEXT("and 56 distinct species id spaces"), IdSpaces.Num(), 56);

	// The join the other way: every census override of slot 546 has a row here, with the same body.
	int32 CensusOverrides = 0;
	bool bEveryOverrideHasARow = true;
	for (const FElysiumNpcClassSlot& Override : ElysiumNpcKernelShape::Overrides())
	{
		if (Override.Slot != 546)
		{
			continue;
		}
		++CensusOverrides;
		const FElysiumNpc::FSquadSlotSpecies* Row =
			FElysiumNpc::SquadSlotSpeciesOf(Override.Class);
		if (Row == nullptr || FCString::Strcmp(Row->Body, Override.Address) != 0)
		{
			bEveryOverrideHasARow = false;
			AddError(FString::Printf(TEXT("slot 546 override %s (%s) has no matching table row"),
				Override.Class, Override.Address));
		}
	}
	TestEqual(TEXT("the census records 60 slot-546 overrides"), CensusOverrides, 60);
	TestTrue(TEXT("and every one of them is a row of this family's table"), bEveryOverrideHasARow);

	TestNull(TEXT("a class outside the table has no row"),
		FElysiumNpc::SquadSlotSpeciesOf(TEXT("CNotAClass")));
	return true;
}

// The one shared squad-slot namespace, `DAT_10936c74`.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadSlotNamespaceTest,
	"Elysium.Substrate.NpcKernelSquad.SquadSlotNamespace", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadSlotNamespaceTest::RunTest(const FString&)
{
	// `0x10316e80` seeds exactly two symbols, at 1000000000 and 1000000001. `0x102ea020` answers
	// the literal `<<null>>` for -1 and NULL for an id the table does not carry.
	TestEqual(TEXT("-1 is <<null>>"), FString(FElysiumNpc::GlobalSquadSlotName(INDEX_NONE)),
		FString(TEXT("<<null>>")));
	TestEqual(TEXT("1000000000 is SQUAD_SLOT_ATTACK1"),
		FString(FElysiumNpc::GlobalSquadSlotName(1000000000)),
		FString(TEXT("SQUAD_SLOT_ATTACK1")));
	TestEqual(TEXT("1000000001 is SQUAD_SLOT_ATTACK2"),
		FString(FElysiumNpc::GlobalSquadSlotName(1000000001)),
		FString(TEXT("SQUAD_SLOT_ATTACK2")));
	TestNull(TEXT("0 is in no namespace"), FElysiumNpc::GlobalSquadSlotName(0));
	TestNull(TEXT("and neither is 1000000002"), FElysiumNpc::GlobalSquadSlotName(1000000002));
	return true;
}

// The dispatch, through spawned leaves rather than through a name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadSlotNameDispatchTest,
	"Elysium.Substrate.NpcKernelSquad.SquadSlotNameDispatch", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadSlotNameDispatchTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_546"), 5461);
	const TCHAR* const Classnames[] = { TEXT("npc_VHumanCombatant"), TEXT("npc_VRat"),
		TEXT("npc_VSabbatLeader"), TEXT("npc_VCop"), TEXT("npc_VTaxiDriver"), TEXT("npc_VHuman"),
		TEXT("npc_VHunter"), TEXT("npc_VTzimisceRunner"), TEXT("npc_VAnimal"),
		TEXT("npc_VAndreiBlood"), TEXT("npc_VVampire"), TEXT("npc_VPedestrian") };
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Classnames)); ++Index)
	{
		Builder.AddNpc(*FString::Printf(TEXT("n%d"), Index),
			FVector(600.0 * Index, 0.0, 0.0), Classnames[Index]);
	}
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));

	int32 Claimed = 0;
	int32 Unclaimed = 0;
	for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Classnames)); ++Index)
	{
		FElysiumNpc* Npc = Fixture.Npc(*FString::Printf(TEXT("n%d"), Index));
		TestNotNull(*FString::Printf(TEXT("%s spawned"), Classnames[Index]), Npc);
		if (Npc == nullptr)
		{
			continue;
		}
		if (Npc->RetailClass() != nullptr)
		{
			// A classname the census claims IS one of the 56 species: it translates through an
			// EMPTY id space, so every id is `<<null>>` — including the two the global namespace
			// does carry, because the translation refuses before the lookup ever happens.
			++Claimed;
			for (const int32 SlotEn : { 0, 1, -1, 1000000000 })
			{
				TestEqual(*FString::Printf(TEXT("%s answers <<null>> for squad slot %d"),
					Classnames[Index], SlotEn),
					FString(Npc->SquadSlotName(SlotEn)), FString(TEXT("<<null>>")));
			}
		}
		else
		{
			// A classname retail stands no NPC class for — `CNPC_VCop` carries no entity classname
			// in the census, so `npc_VCop` resolves to nothing. There is then no species override
			// of slot 546 to find and the Troika line's own body answers: no translation at all,
			// straight into the global namespace.
			++Unclaimed;
			TestEqual(*FString::Printf(TEXT("%s takes the Troika line and reads the namespace"),
					Classnames[Index]),
				FString(Npc->SquadSlotName(1000000000)), FString(TEXT("SQUAD_SLOT_ATTACK1")));
			TestEqual(*FString::Printf(TEXT("%s still answers <<null>> for -1"), Classnames[Index]),
				FString(Npc->SquadSlotName(INDEX_NONE)), FString(TEXT("<<null>>")));
			TestNull(*FString::Printf(TEXT("%s answers nothing for an unregistered id"),
					Classnames[Index]),
				Npc->SquadSlotName(0));
		}
	}
	TestTrue(TEXT("most of the spawned leaves are census species"), Claimed >= 10);
	TestEqual(TEXT("and npc_VCop is the one the census claims for no class"), Unclaimed, 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The squad bodies, and the seam they all stop at.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadInitSquadTest,
	"Elysium.Substrate.NpcKernelSquad.InitSquad", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadInitSquadTest::RunTest(const FString&)
{
	// The two arms, off the census: `CNPC_VCamera` and `CNPC_VCameraSecurity` replace slot 545 with
	// `0x10369bd0` (find-by-name, else create-and-immediately-remove); everything else inherits the
	// Troika line's `0x10273d30`.
	TestEqual(TEXT("the Troika line's InitSquad is 0x10273d30"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CAI_BaseNPCTroika")), 545)),
		FString(TEXT("0x10273d30")));
	TestEqual(TEXT("CNPC_VCamera replaces it with 0x10369bd0"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VCamera")), 545)),
		FString(TEXT("0x10369bd0")));
	TestEqual(TEXT("and CNPC_VCameraSecurity inherits that same body"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity")), 545)),
		FString(TEXT("0x10369bd0")));
	TestEqual(TEXT("a combatant takes the Troika line's"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VHumanCombatant")), 545)),
		FString(TEXT("0x10273d30")));

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_init"), 5450);
	Builder.AddNpc(TEXT("solo"));
	Builder.AddNpc(TEXT("squaddie"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Solo = Fixture.Npc(TEXT("solo"));
	FElysiumNpc* Squad = Fixture.Npc(TEXT("squaddie"));
	TestNotNull(TEXT("solo spawned"), Solo);
	TestNotNull(TEXT("squaddie spawned"), Squad);
	if (Solo == nullptr || Squad == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Solo, Squad });
	// `m_SquadName` (`+0x5da8`, KEY `squadname`) is a declared word with no keyfield parse in this
	// runtime yet, so the fixture writes it directly rather than through the def.
	Squad->SquadName = TEXT("alpha");

	// Retail's first gate is `m_pSquad == NULL`, its second `CapabilitiesGet() & bits_CAP_SQUAD`
	// (0x4000000). Slot 513 is still a generated stub answering 0, so the capability never reads
	// set — the seam is asked and the whole body stops there. Both NPCs therefore answer false and
	// neither joins anything, named-squad or not.
	TestFalse(TEXT("a solo NPC is not squadded"), Solo->InitSquad());
	TestFalse(TEXT("and neither is one with a squadname, because bits_CAP_SQUAD answers nothing"),
		Squad->InitSquad());
	TestNull(TEXT("no squad object exists to join"), Squad->ConnectedSquad());
	TestEqual(TEXT("and the keyfield is untouched by the attempt"), Squad->SquadName,
		FString(TEXT("alpha")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadSeamTest,
	"Elysium.Substrate.NpcKernelSquad.SquadSeam", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadSeamTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_seam"), 5440);
	Builder.AddNpc(TEXT("a"));
	Builder.AddNpc(TEXT("b"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("a"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("b"));
	TestNotNull(TEXT("a spawned"), Npc);
	TestNotNull(TEXT("b spawned"), Other);
	if (Npc == nullptr || Other == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Other });
	// `squadname` (`m_SquadName +0x5da8`) has no keyfield parse in this runtime yet.
	Npc->SquadName = TEXT("alpha");
	Other->SquadName = TEXT("alpha");

	// `0x102781a0`: the null-other arm and the null-MY-squad arm are separate, and BOTH answer
	// false — two squadless NPCs do NOT "share" a squad, which is the asymmetry the retail body
	// spells out (`if (m_pSquad == 0) return 0;` comes before any comparison).
	TestFalse(TEXT("nobody shares a squad with nothing"), Npc->SharesSquadWith(nullptr));
	TestFalse(TEXT("and two squadless NPCs do not share one either"),
		Npc->SharesSquadWith(Other));
	TestFalse(TEXT("even with the same authored squadname"),
		Other->SharesSquadWith(Npc));
	TestEqual(TEXT("because the name is the keyfield, not the object"), Npc->SquadName,
		Other->SquadName);

	// `0x1029a930 SetSquad`: leaves the prior squad, finds-or-creates the named one, re-points the
	// enemy memory. The seam never creates, so the NPC stays squadless — and `SetSquad` does not
	// write `m_SquadName` (`+0x5da8`), which is the one word this test can see.
	Npc->SetSquad(TEXT("bravo"));
	TestNull(TEXT("SetSquad finds and creates nothing"), Npc->ConnectedSquad());
	TestEqual(TEXT("and does not write the squadname keyfield"), Npc->SquadName,
		FString(TEXT("alpha")));

	// `0x1028ae60 VacateSquadSlot`: gates are `m_iMySquadSlot != -1`, `m_iSquadDisconnected < 1`
	// and `m_pSquad != 0`. The third refuses, so the slot is never cleared.
	Npc->MySquadSlot = 3;
	Npc->VacateSquadSlot();
	TestEqual(TEXT("VacateSquadSlot stops at the squad seam and keeps the slot"), Npc->MySquadSlot,
		3);
	Npc->ScheduleHost.SquadDisconnected = 1;
	Npc->VacateSquadSlot();
	TestEqual(TEXT("a disconnected NPC does not vacate either"), Npc->MySquadSlot, 3);
	Npc->ScheduleHost.SquadDisconnected = 0;
	Npc->MySquadSlot = INDEX_NONE;
	Npc->VacateSquadSlot();
	TestEqual(TEXT("and -1 means there is no slot to vacate"), Npc->MySquadSlot, INDEX_NONE);

	// `0x1036e2f0 GetOtherBrother`: the disconnect gate comes FIRST, then the squad walk. No
	// registered classname reaches `CNPC_VChangBros` (the census records no entity classname for
	// it), so the identity half is asserted against the census by name and the body half on a
	// spawned leaf — neither of which can find a brother without a squad object.
	TestNotNull(TEXT("CNPC_VChangBros is a census class"),
		ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBros")));
	TestTrue(TEXT("and derives from CNPC_VVampireBoss"),
		ElysiumNpcKernelClass::DerivesFrom(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBros")), TEXT("CNPC_VVampireBoss")));
	TestNull(TEXT("a squadless NPC finds no brother"), Npc->GetOtherBrother());
	Npc->ScheduleHost.SquadDisconnected = 1;
	TestNull(TEXT("and a disconnected one refuses before the walk"), Npc->GetOtherBrother());
	Npc->ScheduleHost.SquadDisconnected = 0;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadReconnectTest,
	"Elysium.Substrate.NpcKernelSquad.ReconnectToSquad", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadReconnectTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_reconnect"), 5430);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `0x1026d0c0` (thunk `0x10009601`): `--count`, the squad-memory rejoin only at `< 1`, and the
	// flag clear UNCONDITIONALLY — a nested disconnect that has not unwound still loses the bit.
	Npc->DisconnectFromSquad();
	Npc->DisconnectFromSquad();
	TestEqual(TEXT("two disconnects nest"), Npc->ScheduleHost.SquadDisconnected, 2);
	Npc->ReconnectToSquad();
	TestEqual(TEXT("one reconnect decrements rather than clearing"),
		Npc->ScheduleHost.SquadDisconnected, 1);
	TestFalse(TEXT("but the D_DISCONNECT_SQUAD bit is cleared on every reconnect"),
		Npc->NpcFlags.Has(EElysiumNpcFlag2::D_DISCONNECT_SQUAD));
	Npc->ReconnectToSquad();
	TestEqual(TEXT("the second reconnect reaches zero"), Npc->ScheduleHost.SquadDisconnected, 0);
	Npc->ReconnectToSquad();
	TestEqual(TEXT("and the counter never goes negative (retail's `< 1 -> 0` arm)"),
		Npc->ScheduleHost.SquadDisconnected, 0);
	// The rejoin arm at zero asks `AddSelfToSquadMemory` (`0x10316720`); there is no `CAI_Squad`,
	// so it answers nothing and the NPC keeps its own enemy memory.
	TestNull(TEXT("and there is still no squad memory to rejoin"), Npc->ConnectedSquad());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The follower pair.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadFollowerTest,
	"Elysium.Substrate.NpcKernelSquad.Follower", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadFollowerTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_follower"), 6478);
	Builder.AddNpc(TEXT("follower"));
	Builder.AddNpc(TEXT("boss"), FVector(300.0, 0.0, 0.0));
	Builder.AddCounter(TEXT("not_a_character"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("follower"));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumEntity* Counter = Fixture.World.FindByName(TEXT("not_a_character"));
	FElysiumPlayer* Player = Fixture.Player();
	TestNotNull(TEXT("the follower spawned"), Npc);
	TestNotNull(TEXT("the boss spawned"), Boss);
	TestNotNull(TEXT("the counter spawned"), Counter);
	TestNotNull(TEXT("the player spawned"), Player);
	if (Npc == nullptr || Boss == nullptr || Counter == nullptr || Player == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Boss });

	// slot 293 `0x102c5470`: resolve `m_hFollowerBoss` and answer `boss + 0x9c`, the cached
	// `CBaseCombatCharacter*` — so a live boss that is not a combat character answers NULL exactly
	// as a dead handle does.
	TestNull(TEXT("an unset follower-boss handle answers nothing"), Npc->GetFollowerBoss());
	Npc->FollowerBoss = Boss->Handle;
	TestTrue(TEXT("a live combat-character boss answers that entity"),
		Npc->GetFollowerBoss() == static_cast<FElysiumEntity*>(Boss));
	Npc->FollowerBoss = Counter->Handle;
	TestNull(TEXT("but a live NON-combat-character boss answers nothing (+0x9c is NULL)"),
		Npc->GetFollowerBoss());
	Npc->FollowerBoss = FElysiumEntityHandle::Invalid();

	// `0x102c4470`: `!player` for the player, else the boss's own targetname, and an empty name
	// stores NULL rather than "".
	Npc->SetFollowerBossName(Player);
	TestEqual(TEXT("the player is the authored key !player"), Npc->FollowerBossName,
		FString(TEXT("!player")));
	Npc->SetFollowerBossName(Boss);
	TestEqual(TEXT("and any other entity is its targetname"), Npc->FollowerBossName,
		FString(TEXT("boss")));
	Npc->SetFollowerBossName(Counter);
	TestEqual(TEXT("including a non-character one"), Npc->FollowerBossName,
		FString(TEXT("not_a_character")));
	TestNull(TEXT("and naming a boss does NOT resolve the handle — 0x102c44e0 is unported"),
		Npc->GetFollowerBoss());

	// `0x102c4680`, the clamp `0x102c4640` calls before it stores the type: the `Npc_Follower_Info`
	// row read is a seam here, so the three distances keep what they carry and the two overlap
	// rules — `walkTo >= backAway + 10`, `runTo >= walkTo + 10` — are what runs.
	Npc->FollowerDistanceBackAway = 100.0f;
	Npc->FollowerDistanceWalkTo = 100.0f;
	Npc->FollowerDistanceRunTo = 100.0f;
	Npc->SetFollowerType(TEXT("bodyguard"));
	TestEqual(TEXT("backAway is never moved"), Npc->FollowerDistanceBackAway, 100.0f);
	TestEqual(TEXT("walkTo is raised to backAway + 10"), Npc->FollowerDistanceWalkTo, 110.0f);
	TestEqual(TEXT("and runTo to the CLAMPED walkTo + 10, not the original"),
		Npc->FollowerDistanceRunTo, 120.0f);
	TestEqual(TEXT("and the type is stored"), Npc->FollowerType, FString(TEXT("bodyguard")));

	// A row that already separates the bands is left alone.
	Npc->FollowerDistanceBackAway = 100.0f;
	Npc->FollowerDistanceWalkTo = 200.0f;
	Npc->FollowerDistanceRunTo = 500.0f;
	Npc->SetFollowerType(TEXT("escort"));
	TestEqual(TEXT("a well-separated walkTo is untouched"), Npc->FollowerDistanceWalkTo, 200.0f);
	TestEqual(TEXT("and so is runTo"), Npc->FollowerDistanceRunTo, 500.0f);

	// An empty type stores NULL, not "".
	Npc->SetFollowerType(FString());
	TestTrue(TEXT("an empty follower type clears the keyfield"), Npc->FollowerType.IsEmpty());

	// `0x101a8130` — the named-master lookup. Its key word and its RTTI target are UNRECOVERED, so
	// the body asks for a key it has no member for and refuses.
	TestNull(TEXT("ResolveNamedMaster refuses: the +0x5f5c key and the RTTI class are unrecovered"),
		Npc->ResolveNamedMaster());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Relations and the ally notice.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadRelationsTest,
	"Elysium.Substrate.NpcKernelSquad.Relations", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadRelationsTest::RunTest(const FString&)
{
	// The three classes `0x103a48b0` fills slot 404 for.
	for (const TCHAR* Name : { TEXT("CNPC_VFrenzyShadow"), TEXT("CNPC_VPlayerController"),
			TEXT("CNPC_VWolfMorph") })
	{
		const FElysiumNpcClassSlot* Row =
			ElysiumNpcKernelClass::OverrideOf(ElysiumNpcKernelClass::Find(Name), 404);
		TestNotNull(*FString::Printf(TEXT("%s overrides slot 404"), Name), Row);
		if (Row != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s's IRelationType is 0x103a48b0"), Name),
				FString(Row->Address), FString(TEXT("0x103a48b0")));
		}
	}

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_relations"), 6040);
	Builder.AddNpc(TEXT("shadow"));
	Builder.AddNpc(TEXT("stranger"), FVector(300.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("ghost"), FVector(600.0, 0.0, 0.0));
	Builder.AddCounter(TEXT("crate"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("shadow"));
	FElysiumNpc* Stranger = Fixture.Npc(TEXT("stranger"));
	FElysiumNpc* Ghost = Fixture.Npc(TEXT("ghost"));
	FElysiumEntity* Crate = Fixture.World.FindByName(TEXT("crate"));
	FElysiumPlayer* Player = Fixture.Player();
	TestNotNull(TEXT("shadow spawned"), Npc);
	TestNotNull(TEXT("stranger spawned"), Stranger);
	TestNotNull(TEXT("ghost spawned"), Ghost);
	TestNotNull(TEXT("crate spawned"), Crate);
	TestNotNull(TEXT("the player spawned"), Player);
	if (Npc == nullptr || Stranger == nullptr || Ghost == nullptr || Crate == nullptr
		|| Player == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Stranger, Ghost });

	// `0x103a48b0`, its four answers in retail's order.
	TestEqual(TEXT("a null target is D_ER (0)"), Npc->SpeciesIRelationType(nullptr), 0);
	Npc->FriendPlayer = Player->Handle;
	TestEqual(TEXT("m_hFriendPlayer is D_LI (3)"), Npc->SpeciesIRelationType(Player), 3);
	TestEqual(TEXT("any other combat character is D_HT (1)"),
		Npc->SpeciesIRelationType(Stranger), 1);
	Ghost->ScriptHide();
	TestEqual(TEXT("a script-hidden one falls through to D_NU (4)"),
		Npc->SpeciesIRelationType(Ghost), 4);
	TestEqual(TEXT("and so does an entity that is no combat character at all"),
		Npc->SpeciesIRelationType(Crate), 4);
	Npc->FriendPlayer = FElysiumEntityHandle::Invalid();
	TestEqual(TEXT("with no friend player the player is D_HT like anyone else"),
		Npc->SpeciesIRelationType(Player), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadAlertAllyTest,
	"Elysium.Substrate.NpcKernelSquad.AlertNearbyAlly", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadAlertAllyTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_alert"), 65192);
	Builder.AddNpc(TEXT("ally"));
	// Inside `_DAT_1049aea0`: 150 Source units is 381 cm, so 200 cm is the near arm and the
	// visibility route never has to be consulted.
	Builder.AddNpc(TEXT("attacker"), FVector(200.0, 0.0, 0.0));
	Builder.AddCounter(TEXT("crate"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ally = Fixture.Npc(TEXT("ally"));
	FElysiumNpc* Attacker = Fixture.Npc(TEXT("attacker"));
	FElysiumEntity* Crate = Fixture.World.FindByName(TEXT("crate"));
	TestNotNull(TEXT("the ally spawned"), Ally);
	TestNotNull(TEXT("the attacker spawned"), Attacker);
	TestNotNull(TEXT("the crate spawned"), Crate);
	if (Ally == nullptr || Attacker == nullptr || Crate == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Ally, Attacker });

	const double RadiusCm =
		static_cast<double>(ElysiumNpcCond::MeleeNoticeAcceptanceUnits) * ElysiumMove::U;
	TestTrue(TEXT("the attacker is inside the recovered radius"),
		FVector::Dist(Ally->Origin, Attacker->Origin) < RadiusCm);

	// Gate 1: a null attacker is refused before anything else.
	Ally->AlertNearbyAlly(nullptr);
	TestFalse(TEXT("a null attacker records nothing"),
		Ally->Senses.Memory.DetectedAttackAttacker.IsSet());

	// Gate 3: `attacker->m_pCombatCharacter` (`+0x9c`) must be non-null.
	Ally->AlertNearbyAlly(Crate);
	TestFalse(TEXT("an attacker that is no combat character records nothing"),
		Ally->Senses.Memory.DetectedAttackAttacker.IsSet());

	// Gate 4: `IRelationType(attacker) != D_LI`. The friend player is the one D_LI answer this
	// species body gives, so pointing `m_hFriendPlayer` at the attacker refuses the notice.
	Ally->FriendPlayer = Attacker->Handle;
	Ally->AlertNearbyAlly(Attacker);
	TestFalse(TEXT("a D_LI attacker is not reported"),
		Ally->Senses.Memory.DetectedAttackAttacker.IsSet());
	Ally->FriendPlayer = FElysiumEntityHandle::Invalid();

	// `m_bIgnoreDetectedAttack` (`+0x65f5`), the keyfield `0x102bf560` tests before it writes.
	Ally->bIgnoreDetectedAttack = true;
	Ally->AlertNearbyAlly(Attacker);
	TestFalse(TEXT("ignore_detected_attack suppresses the record"),
		Ally->Senses.Memory.DetectedAttackAttacker.IsSet());
	Ally->bIgnoreDetectedAttack = false;

	// The whole chain, admitted: state IDLE (retail's 1), a combat-character attacker, D_HT, near.
	TestEqual(TEXT("the ally is in the IDLE state retail admits"), Ally->GetMind().State(),
		EElysiumNpcState::Idle);
	Ally->AlertNearbyAlly(Attacker);
	TestTrue(TEXT("a near, hated combat character IS recorded"),
		Ally->Senses.Memory.DetectedAttackAttacker.IsSet());
	TestTrue(TEXT("as the attacker itself"),
		Ally->Senses.Memory.DetectedAttackAttacker == Attacker->Handle);
	TestTrue(TEXT("with a stamp"), Ally->Senses.Memory.DetectedAttackTime >= 0.0);

	// Gate 2: the state set. Retail admits 1 (IDLE), 3 (COMBAT) and the custom 0xb ONLY — ALERT
	// (2) is not on the list, which is the arm this checks.
	Ally->Senses.Memory.DetectedAttackAttacker = FElysiumEntityHandle::Invalid();
	Ally->Senses.Memory.DetectedAttackTime = -1.0;
	FElysiumScriptedScheduleOrder Order;
	Ally->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, EElysiumNpcState::Alert);
	TestEqual(TEXT("the ally is now ALERT"), Ally->GetMind().State(), EElysiumNpcState::Alert);
	Ally->AlertNearbyAlly(Attacker);
	TestFalse(TEXT("and ALERT is NOT one of retail's three admitted states"),
		Ally->Senses.Memory.DetectedAttackAttacker.IsSet());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The two coordination species.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadChangBrosTest,
	"Elysium.Substrate.NpcKernelSquad.ChangBros", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadChangBrosTest::RunTest(const FString&)
{
	// No registered `npc_*` classname reaches `CNPC_VChangBros` — the census records none for it —
	// so the two bodies run on an ordinary leaf carrying the species words. Both are keyed on
	// `m_ChangType` and the current schedule, not on the class.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_chang"), 66184);
	Builder.AddNpc(TEXT("chang"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Chang = Fixture.Npc(TEXT("chang"));
	TestNotNull(TEXT("the stand-in leaf spawned"), Chang);
	if (Chang == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Chang });
	TestEqual(TEXT("CNPC_VChangBros fills slot 546 with 0x1036a3f0"),
		FString(ElysiumNpcKernelClass::BodyOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBros")), 546)),
		FString(TEXT("0x1036a3f0")));

	// `0x1036e820 ReadyForUnited`: `GetCurSchedule()`'s id against 0x15a or 0x15b, false with no
	// schedule at all. The port's schedule set does not carry the two `UNITED` programs yet, so
	// nothing it CAN run satisfies the test — the two numbers are the rule and the gap is named.
	TestFalse(TEXT("an NPC running no schedule is not ready for UNITED"), Chang->ReadyForUnited());
	Chang->Schedule.Current = EElysiumScheduleId::IdleStand;
	TestTrue(TEXT("IDLE_STAND is running"), Chang->Schedule.IsRunning());
	TestNotEqual(TEXT("but IDLE_STAND is not 0x15a"),
		ElysiumScheduleNumber(EElysiumScheduleId::IdleStand), 0x15a);
	TestFalse(TEXT("so it is still not ready for UNITED"), Chang->ReadyForUnited());
	Chang->Schedule.Clear();

	// `0x1036d100 SelectUnitedNode`: hint type 18000, the FIRST match for `m_ChangType == 0` and
	// the SECOND for `== 1`; any other type walks the whole list and answers 0. The hint-list seam
	// has no typed store, so all three answer nothing — and the third arm would answer nothing in
	// retail too.
	for (const int32 Type : { 0, 1, 2 })
	{
		Chang->ChangType = Type;
		TestNull(*FString::Printf(TEXT("ChangType %d finds no hint of type 18000"), Type),
			Chang->SelectUnitedNode());
	}
	Chang->ChangType = 0;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSquadCoordinateTroopsTest,
	"Elysium.Substrate.NpcKernelSquad.CoordinateTroops", GElysiumNpcKernelSquadFlags)
bool FElysiumNpcKernelSquadCoordinateTroopsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_squad_mingxiao"), 67400);
	Builder.AddNpc(TEXT("ming"));
	Builder.AddNpc(TEXT("tentacle"), FVector(300.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ming = Fixture.Npc(TEXT("ming"));
	FElysiumNpc* Tentacle = Fixture.Npc(TEXT("tentacle"));
	TestNotNull(TEXT("MingXiao's stand-in spawned"), Ming);
	TestNotNull(TEXT("the tentacle spawned"), Tentacle);
	if (Ming == nullptr || Tentacle == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Ming, Tentacle });

	// `0x10399610`: one id per call, `m_iCoordinateTentacleID` advancing 0..5 and wrapping past 5.
	// A live handle at the current index is resolved and the two per-troop arms (`0x103998d0`,
	// `0x103999f0`) are asked; both are rows of their own and answer nothing, so the observable
	// half of this body is the round robin — which is the half that decides WHICH troop each think
	// reaches.
	Ming->SeveredTentacles[2] = Tentacle->Handle;
	Ming->Proxies[4] = Tentacle->Handle;
	TestEqual(TEXT("the round robin starts at 0"), Ming->CoordinateTentacleId, 0);
	const int32 Expected[] = { 1, 2, 3, 4, 5, 0, 1, 2 };
	for (int32 Step = 0; Step < static_cast<int32>(UE_ARRAY_COUNT(Expected)); ++Step)
	{
		Ming->CoordinateTroops();
		TestEqual(*FString::Printf(TEXT("after %d calls the id is %d"), Step + 1, Expected[Step]),
			Ming->CoordinateTentacleId, Expected[Step]);
	}
	// An out-of-range id still advances — retail reads the array first and advances afterwards, so
	// a hand-written 6 wraps to 0 on the next call rather than sticking.
	Ming->CoordinateTentacleId = 6;
	Ming->CoordinateTroops();
	TestEqual(TEXT("an out-of-range id wraps rather than sticking"), Ming->CoordinateTentacleId, 0);
	return true;
}

#endif
