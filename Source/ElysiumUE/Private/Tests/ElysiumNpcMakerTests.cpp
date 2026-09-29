#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMakerFleshpile.h"
#include "Substrate/ElysiumNpcMakerZombie.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 fold A4: the makers stand as Troika NPCs of their own classes (`CNPCMaker`,
// `CNPCMaker_Fleshpile`, `CNPCMaker_Zombie`). These cases pin what the fold changed: the installed
// think is the maker's entry (not `NPCThink`), the fleshpile's think only re-arms, `Enable` installs
// the base think on every maker class, the AI-list wake paths pull a maker's think to now without
// making a disabled maker spawn, and the maker is not a body anything can shoot, stand on or target.

static constexpr EAutomationTestFlags GElysiumNpcMakerTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	template <typename T>
	T* MakerAs(FElysiumNpcWorldFixture& F, const TCHAR* Name)
	{
		FElysiumEntity* Entity = F.World.FindByName(Name);
		FElysiumNpc* Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
		return Npc != nullptr ? Npc->AsSpecies<T>() : nullptr;
	}

	FElysiumEntityDef& AddMaker(FElysiumNpcWorldBuilder& Builder, const TCHAR* Classname,
		const TCHAR* Name, const FVector& Origin, const TCHAR* NpcType, bool bStartDisabled)
	{
		FElysiumEntityDef& Def = Builder.AddEntity(Classname, Name, Origin);
		Def.Keys.Add(TEXT("model"), TEXT("models/maker.mdl"));
		Def.Keys.Add(TEXT("NPCType"), NpcType);
		Def.Keys.Add(TEXT("SpawnFrequency"), TEXT("5"));
		Def.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
		Def.Keys.Add(TEXT("Flag_StartDisabled"), bStartDisabled ? TEXT("1") : TEXT("0"));
		return Def;
	}

	int32 CountLive(FElysiumEntityWorld& World, const TCHAR* Classname)
	{
		int32 Count = 0;
		for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
		{
			if (Entity.IsValid() && !Entity->IsDead() && Entity->Def != nullptr
				&& Entity->Def->Classname.Equals(Classname, ESearchCase::IgnoreCase))
			{
				++Count;
			}
		}
		return Count;
	}
}

// D1: `CNPCMaker_Fleshpile`'s think `0x1034c8b0` re-arms `m_flNextThink = freq + curtime` and does
// nothing else; the runners come from Andrei's task `0x154` (`SummonRunnerNear`).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerFleshpileThinkTest,
	"Elysium.Substrate.NpcMaker.FleshpileThinkRearms", GElysiumNpcMakerTestFlags)
bool FElysiumNpcMakerFleshpileThinkTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("maker_fleshpile_think"), 4401u);
	AddMaker(Builder, TEXT("npc_maker_fleshpile"), TEXT("pile"), FVector(800.0, 0.0, 0.0),
		TEXT("npc_VTzimisceRunner"), /*bStartDisabled=*/false);
	Builder.AddNpc(TEXT("andrei"), FVector(900.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcMakerFleshpile* Pile = MakerAs<FElysiumNpcMakerFleshpile>(F, TEXT("pile"));
	FElysiumNpc* Andrei = F.Npc(TEXT("andrei"));
	FElysiumNpcWorldFixture::Quiet({ Andrei });
	if (!TestNotNull(TEXT("the fleshpile maker stands"), Pile) || Andrei == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("its enabled Spawn installs the fleshpile think 0x1034c8b0"),
		FString(FElysiumNpcMaker::MakerThinkName(Pile->InstalledThink)), FString(TEXT("fleshpile")));
	const int32 RunnersBefore = CountLive(F.World, TEXT("npc_VTzimisceRunner"));
	const double Due = static_cast<double>(Pile->NextThink);
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		const double At = static_cast<double>(Pile->NextThink);
		F.World.Tick(At);
		TestEqual(FString::Printf(TEXT("cycle %d re-arms at freq + curtime"), Cycle),
			static_cast<double>(Pile->NextThink), At + 5.0, 1e-3);
	}
	TestTrue(TEXT("the first due think came after the map stood"), Due > 0.0);
	TestEqual(TEXT("and the fleshpile think spawned NOTHING (retail correction D1)"),
		CountLive(F.World, TEXT("npc_VTzimisceRunner")), RunnersBefore);
	TestEqual(TEXT("so Andrei's runner budget is untouched"), Andrei->ActiveRunnerCount, 0);

	// The spawner is Andrei's summon task (`0x1035d1b0` case `0x154`): the nearest fleshpile maker
	// within 1024 units, slot 617 `MakeNPC(0)`. SEAM until that task is ported; its maker half runs.
	FElysiumNpc* Runner = FElysiumNpcMakerFleshpile::SummonRunnerNear(F.World, Andrei->Origin);
	TestNotNull(TEXT("the summon spawns a runner from the nearest fleshpile"), Runner);
	TestEqual(TEXT("and counts it against Andrei's budget"), Andrei->ActiveRunnerCount, 1);
	TestNull(TEXT("a summon 1024+ units from every fleshpile finds none"),
		FElysiumNpcMakerFleshpile::SummonRunnerNear(F.World, FVector(800.0 + 1100.0 * ElysiumMove::U, 0.0, 0.0)));
	return true;
}

// D5: `InputEnable` `0x1034b490` installs the BASE think (`0x1000696a` -> `0x1034bbf0`) on every maker
// class, and `MakerThink`'s slot-617 call still reaches the class's own `MakeNPC`.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerEnableBaseThinkTest,
	"Elysium.Substrate.NpcMaker.EnableInstallsBaseThink", GElysiumNpcMakerTestFlags)
bool FElysiumNpcMakerEnableBaseThinkTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("maker_enable_base"), 4402u);
	AddMaker(Builder, TEXT("npc_maker_fleshpile"), TEXT("pile"), FVector(800.0, 0.0, 0.0),
		TEXT("npc_VTzimisceRunner"), /*bStartDisabled=*/true);
	FElysiumEntityDef& Zombie = AddMaker(Builder, TEXT("npc_maker_zombie"), TEXT("zmaker"),
		FVector(-800.0, 0.0, 0.0), TEXT("npc_VZombie"), /*bStartDisabled=*/true);
	Zombie.Keys.Add(TEXT("remove_distance"), TEXT("100000"));
	Builder.AddNpc(TEXT("andrei"), FVector(900.0, 0.0, 0.0), TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcMakerFleshpile* Pile = MakerAs<FElysiumNpcMakerFleshpile>(F, TEXT("pile"));
	FElysiumNpcMakerZombie* ZMaker = MakerAs<FElysiumNpcMakerZombie>(F, TEXT("zmaker"));
	FElysiumNpc* Andrei = F.Npc(TEXT("andrei"));
	FElysiumNpcWorldFixture::Quiet({ Andrei });
	if (!TestNotNull(TEXT("the fleshpile maker stands"), Pile) || !TestNotNull(TEXT("the zombie maker"), ZMaker)
		|| Andrei == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("disabled fleshpile: the inert think"),
		FString(FElysiumNpcMaker::MakerThinkName(Pile->InstalledThink)), FString(TEXT("inert")));
	TestEqual(TEXT("disabled zombie maker: a NULL think"),
		FString(FElysiumNpcMaker::MakerThinkName(ZMaker->InstalledThink)), FString(TEXT("none")));

	const FElysiumInputArgs NoArgs;
	Pile->InputEnable(NoArgs);
	ZMaker->SetZombieFistsItemForTests(true);
	ZMaker->InputEnable(NoArgs);
	TestEqual(TEXT("Enable installs the BASE think on the fleshpile"),
		FString(FElysiumNpcMaker::MakerThinkName(Pile->InstalledThink)), FString(TEXT("base")));
	TestEqual(TEXT("and on the zombie maker"),
		FString(FElysiumNpcMaker::MakerThinkName(ZMaker->InstalledThink)), FString(TEXT("base")));
	TestEqual(TEXT("with m_flNextThink = curtime"), static_cast<double>(Pile->NextThink),
		F.World.NowSeconds(), 1e-3);

	// The base think runs slot 617 VIRTUALLY: the fleshpile's own `0x1034c2d0` (the runner budget).
	const int32 Runners = CountLive(F.World, TEXT("npc_VTzimisceRunner"));
	const int32 Zombies = CountLive(F.World, TEXT("npc_VZombie"));
	F.World.Tick(F.World.NowSeconds());
	TestEqual(TEXT("an enabled fleshpile runs MakerThink, which spawns through its own MakeNPC"),
		CountLive(F.World, TEXT("npc_VTzimisceRunner")), Runners + 1);
	TestEqual(TEXT("which counted the runner on Andrei"), Andrei->ActiveRunnerCount, 1);
	TestEqual(TEXT("and the zombie maker spawned through 0x1034d140"),
		CountLive(F.World, TEXT("npc_VZombie")), Zombies + 1);
	TestEqual(TEXT("MakerThink re-arms at freq after a child"), static_cast<double>(Pile->NextThink),
		F.World.NowSeconds() + 5.0, 1e-3);
	ZMaker->ClearZombieFistsItemForTests();
	return true;
}

// D5 knock-on: a slot-614 reset (`SetAIEnabled` `0x10265680`, `WakeNpcsNear` `0x1028d820`) pulls a
// maker's think to now — retail walks the AI list and makers do not override 614 — but the pulled
// think is the INSTALLED one, so a disabled maker spawns nothing. `g_AIDisabled` is `NPCThink`'s gate
// and never read by a maker think: an enabled maker keeps its cadence through a feed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerWakeTest,
	"Elysium.Substrate.NpcMaker.WakeKeepsInstalledThink", GElysiumNpcMakerTestFlags)
bool FElysiumNpcMakerWakeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("maker_wake"), 4403u);
	AddMaker(Builder, TEXT("npc_maker"), TEXT("off"), FVector(300.0, 0.0, 0.0), TEXT("npc_VHuman"),
		/*bStartDisabled=*/true);
	AddMaker(Builder, TEXT("npc_maker"), TEXT("on"), FVector(-300.0, 0.0, 0.0), TEXT("npc_VHuman"),
		/*bStartDisabled=*/false);
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcMaker* Off = MakerAs<FElysiumNpcMaker>(F, TEXT("off"));
	FElysiumNpcMaker* On = MakerAs<FElysiumNpcMaker>(F, TEXT("on"));
	if (!TestNotNull(TEXT("the disabled maker"), Off) || !TestNotNull(TEXT("the enabled maker"), On))
	{
		return false;
	}
	const int32 Humans = CountLive(F.World, TEXT("npc_VHuman"));

	// `WakeNpcsNear`: both makers are in the walk (flag 0x40), both thinks are pulled to now.
	F.World.WakeNpcsNear(FVector::ZeroVector);
	TestEqual(TEXT("the wake pulls the disabled maker's think to now"),
		static_cast<double>(Off->NextThink), F.World.NowSeconds(), 1e-3);
	TestEqual(TEXT("and the enabled maker's"), static_cast<double>(On->NextThink), F.World.NowSeconds(), 1e-3);
	F.World.Tick(F.World.NowSeconds());
	TestEqual(TEXT("the pulled think is the installed one: the inert maker made nothing, the enabled one one"),
		CountLive(F.World, TEXT("npc_VHuman")), Humans + 1);
	TestEqual(TEXT("the disabled maker has no live child"), Off->LiveChildren, 0);

	// `SetAIEnabled(false)` then `(true)`: the re-enable re-bases every AI-list think; the disabled
	// maker still spawns nothing.
	F.World.SetAiEnabled(false);
	const double Next = static_cast<double>(On->NextThink);
	F.World.Tick(Next);
	TestEqual(TEXT("with AI disabled an enabled maker still keeps its cadence and spawns"),
		CountLive(F.World, TEXT("npc_VHuman")), Humans + 2);
	F.World.SetAiEnabled(true);
	TestEqual(TEXT("SetAIEnabled(true) pulls the disabled maker to now"),
		static_cast<double>(Off->NextThink), F.World.NowSeconds(), 1e-3);
	F.World.Tick(F.World.NowSeconds());
	TestEqual(TEXT("and it still spawns nothing"), Off->LiveChildren, 0);

	// A depleted maker (`MakeNPC` installed `ThinkSet(NULL)`) is the same: a reset spawns nothing.
	On->RemainingTotal = 1;
	On->bInfinite = false;
	On->MaxLiveChildren = 0;
	FElysiumNpc* Last = On->MakeNPC(/*bBypass=*/true);
	TestNotNull(TEXT("the last child"), Last);
	TestEqual(TEXT("depletion clears the installed think"),
		FString(FElysiumNpcMaker::MakerThinkName(On->InstalledThink)), FString(TEXT("none")));
	const int32 Before = CountLive(F.World, TEXT("npc_VHuman"));
	F.World.WakeNpcsNear(FVector::ZeroVector);
	F.World.Tick(F.World.NowSeconds());
	TestEqual(TEXT("a woken depleted maker spawns nothing"), CountLive(F.World, TEXT("npc_VHuman")), Before);
	return true;
}

// Participation: a maker is an NPC for every AI-list walk, and answers retail's own "no" at every
// body question — no solid, no transmit, no cone, no discipline target, no witness.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerParticipationTest,
	"Elysium.Substrate.NpcMaker.Participation", GElysiumNpcMakerTestFlags)
bool FElysiumNpcMakerParticipationTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("maker_participation"), 4404u);
	AddMaker(Builder, TEXT("npc_maker"), TEXT("maker"), FVector(300.0, 0.0, 0.0), TEXT("npc_VHuman"),
		/*bStartDisabled=*/true);
	Builder.AddNpc(TEXT("human"), FVector(-300.0, 0.0, 0.0), TEXT("npc_VHuman"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcMaker* Maker = MakerAs<FElysiumNpcMaker>(F, TEXT("maker"));
	FElysiumNpc* Human = F.Npc(TEXT("human"));
	if (!TestNotNull(TEXT("the maker stands"), Maker) || Human == nullptr)
	{
		return false;
	}
	FElysiumEntity& AsEntity = *Maker;
	TestNotNull(TEXT("AsNpc answers (m_pTroika +0x98 = this)"), AsEntity.AsNpc());
	TestNotNull(TEXT("AsNpcBase answers (m_pBaseNPC +0x94 = this)"), AsEntity.AsNpcBase());
	TestNotNull(TEXT("AsCombatCharacter answers"), AsEntity.AsCombatCharacter());
	// `Spawn` `0x1034afe0`: SOLID_NONE, no body, no model.
	TestTrue(TEXT("the maker is not solid (SetSolid(SOLID_NONE))"), AsEntity.IsRetailNotSolid());
	TestFalse(TEXT("an ordinary NPC is"), Human->IsRetailNotSolid());
	TestNull(TEXT("the maker stands no skeletal body"), AsEntity.GetSkeletalBody());
	TestFalse(TEXT("slot 72 0x1034aef0: no discipline targets it"), AsEntity.Slot72(0));
	TestTrue(TEXT("while an ordinary NPC is a target"), Human->Slot72(0));
	TestFalse(TEXT("slot 587 CanWitnessSupernatural 0x1034aed0 answers false"), Maker->CanWitnessSupernatural(1));
	TestFalse(TEXT("slot 362 answers false"), Maker->FInViewCone(Human->Origin));
	TestFalse(TEXT("slot 363 answers false"), Maker->FInViewCone(static_cast<FElysiumEntity*>(Human)));
	TestFalse(TEXT("slot 364 answers false"), Maker->FInAimCone(Human->Origin));
	TestFalse(TEXT("slot 365 answers false"), Maker->FInAimCone(static_cast<FElysiumEntity*>(Human)));
	// `Activate` `0x1034b140` is empty: no `NPCInit`, so the Troika's think stamps were never armed and
	// the maker's own entry is the installed think.
	TestEqual(TEXT("the maker's think is its installed one"),
		FString(FElysiumNpcMaker::MakerThinkName(Maker->InstalledThink)), FString(TEXT("inert")));
	TestFalse(TEXT("NPCInit never ran: the targetable byte it sets stays clear"),
		FElysiumNpcBase::IsBccTargetable(AsEntity));
	return true;
}

// D11: the installed think rides the maker's record.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMakerInstalledThinkSaveTest,
	"Elysium.Substrate.NpcMaker.InstalledThinkSaved", GElysiumNpcMakerTestFlags)
bool FElysiumNpcMakerInstalledThinkSaveTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("maker_think_save"), 4405u);
	AddMaker(Builder, TEXT("npc_maker_zombie"), TEXT("a"), FVector(300.0, 0.0, 0.0), TEXT("npc_VZombie"),
		/*bStartDisabled=*/false);
	AddMaker(Builder, TEXT("npc_maker_zombie"), TEXT("b"), FVector(-300.0, 0.0, 0.0), TEXT("npc_VZombie"),
		/*bStartDisabled=*/true);
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcMakerZombie* A = MakerAs<FElysiumNpcMakerZombie>(F, TEXT("a"));
	FElysiumNpcMakerZombie* B = MakerAs<FElysiumNpcMakerZombie>(F, TEXT("b"));
	if (!TestNotNull(TEXT("maker a"), A) || !TestNotNull(TEXT("maker b"), B))
	{
		return false;
	}
	TestEqual(TEXT("a's enabled Spawn installed the zombie think 0x1034d2d0"),
		FString(FElysiumNpcMaker::MakerThinkName(A->InstalledThink)), FString(TEXT("zombie")));
	TestEqual(TEXT("b's disabled Spawn installed NULL"),
		FString(FElysiumNpcMaker::MakerThinkName(B->InstalledThink)), FString(TEXT("none")));
	TArray<uint8> Bytes;
	{
		FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::WireIdentity);
		A->Serialize(Ar);
	}
	{
		FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::WireIdentity);
		B->Serialize(Ar);
	}
	TestEqual(TEXT("the record carries the installed think"),
		FString(FElysiumNpcMaker::MakerThinkName(B->InstalledThink)), FString(TEXT("zombie")));
	return true;
}

#endif
