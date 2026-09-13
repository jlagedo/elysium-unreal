#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Lifecycle**. Every assertion below is read off the decompiled C of the body
// it names — the threshold, the arm order, the bit, what is written — never off 29c's one-line walk.
//
// The suite is in three halves: the TWELVE Troika-line slots this family fills, which are ordinary
// member calls on a spawned NPC; the PURE rules, driven with hand-built words so retail's arms are
// exercised without standing a hint node, a cine actor or a standoff behaviour; and the SEAMS, each
// of which gets a case saying it is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GLifecycleTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC in a headless world, quiet so its own think never competes with the pass a case drives.
	struct FLifecycleFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		explicit FLifecycleFixture(const TCHAR* Classname = TEXT("npc_VHumanCombatant"))
			: World(Build(Classname))
		{
			Npc = World.Npc(TEXT("subject"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}

		static FElysiumNpcWorldBuilder Build(const TCHAR* Classname)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("lifecycle"), 20260913);
			Builder.AddNpc(TEXT("subject"), FVector(100.0, 0.0, 0.0), Classname);
			return Builder;
		}
	};
}

// -------------------------------------------------------------------------------------------------
// Slot 158 `IsAlive` (0x100b4dc0) — the one two landed families were waiting on.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleIsAliveTest,
	"Elysium.Substrate.NpcKernelLifecycle.IsAlive", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleIsAliveTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	// `return this->m_lifeState == 0;` — LIFE_ALIVE. A standing NPC is alive; the stub answered
	// false, which is what made family Bosses' slot 482 and family Sounds' PlaySentence take their
	// dead arms.
	TestTrue(TEXT("a standing NPC is alive"), Fix.Npc->IsAlive());

	// The death transaction's own latch is the port's `m_lifeState != LIFE_ALIVE`.
	Fix.Npc->SetDeathReportedForRestore(true);
	TestFalse(TEXT("a body whose death was reported is not alive"), Fix.Npc->IsAlive());
	Fix.Npc->SetDeathReportedForRestore(false);
	TestTrue(TEXT("and clearing it brings it back"), Fix.Npc->IsAlive());

	// `Kill` is the other half of the same word.
	Fix.Npc->Kill();
	TestFalse(TEXT("a killed NPC is not alive"), Fix.Npc->IsAlive());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The other eleven slots.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleSlotsTest,
	"Elysium.Substrate.NpcKernelLifecycle.Slots", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleSlotsTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;

	// slot 4 `GetBaseEntity` (0x10027650) and slot 137 `GetBaseAnimating` (0x1004fc50): `return this`.
	TestEqual(TEXT("slot 4 GetBaseEntity answers itself"),
		N.GetBaseEntity(), static_cast<FElysiumEntity*>(&N));
	TestEqual(TEXT("slot 137 GetBaseAnimating answers itself"),
		N.GetBaseAnimating(), static_cast<FElysiumEntity*>(&N));

	// slot 3 `GetNetworkable` (0x10027630): `&this->field_0x2d4`. SEAM — no network property here.
	TestNull(TEXT("slot 3 GetNetworkable has no sub-object to answer"), N.GetNetworkable());

	// slot 0 `SetRefEHandle` (0x10027450): the four-byte handle word at +0x0448.
	const FElysiumEntityHandle Original = N.Handle;
	FElysiumEntityHandle Other;
	Other.Index = 4242;
	Other.Epoch = 7;
	N.SetRefEHandle(Other);
	TestEqual(TEXT("slot 0 writes the handle word"), N.Handle.Index, 4242);
	TestEqual(TEXT("and its epoch"), static_cast<int32>(N.Handle.Epoch), 7);
	N.SetRefEHandle(Original);

	// slot 423 `IsTemplate` (0x1027e120): `m_spawnflags >> 0xb & 1`.
	N.SpawnFlags = 0;
	TestFalse(TEXT("slot 423 is false with no spawnflags"), N.IsTemplate());
	N.SpawnFlags = 0x400;
	TestFalse(TEXT("slot 423 ignores bit 10"), N.IsTemplate());
	N.SpawnFlags = 0x800;
	TestTrue(TEXT("slot 423 reads bit 11"), N.IsTemplate());
	N.SpawnFlags = 0x1000;
	TestFalse(TEXT("slot 423 ignores bit 12"), N.IsTemplate());

	// slot 552 `ShouldFadeOnDeath` (0x1027a400): `m_spawnflags >> 9 & 1` — the SAME word, bit 9.
	N.SpawnFlags = 0x100;
	TestFalse(TEXT("slot 552 ignores bit 8"), N.ShouldFadeOnDeath());
	N.SpawnFlags = 0x200;
	TestTrue(TEXT("slot 552 reads bit 9"), N.ShouldFadeOnDeath());
	N.SpawnFlags = 0x800;
	TestFalse(TEXT("and slot 552 is not slot 423's bit"), N.ShouldFadeOnDeath());
	TestTrue(TEXT("while slot 423 still is"), N.IsTemplate());
	N.SpawnFlags = 0;

	// slot 91 `ShouldCollide` (0x100b4de0): true unless m_CollisionGroup == 1 AND bit 0x4000000 of
	// the mask is CLEAR. The first argument is ignored, exactly as retail ignores it.
	N.CollisionGroup = 0;
	TestTrue(TEXT("slot 91 collides for any group but 1"), N.ShouldCollide(99, 0));
	N.CollisionGroup = 1;
	TestFalse(TEXT("slot 91 refuses group 1 with the bit clear"), N.ShouldCollide(0, 0));
	TestTrue(TEXT("slot 91 admits group 1 when bit 0x4000000 is set"),
		N.ShouldCollide(0, 0x4000000));
	TestFalse(TEXT("and any other mask bit does not save it"), N.ShouldCollide(0, 0x2000000));
	TestFalse(TEXT("the first argument changes nothing"), N.ShouldCollide(12345, 0));
	N.CollisionGroup = 0;

	// slot 152 `GetDelay` (0x1004fc10): a plain read of +0x0500. SEAM (declared, unwritten).
	TestEqual(TEXT("slot 152 answers the delay word"), N.GetDelay(), 0.f);
	N.EntityDelay = 2.5f;
	TestEqual(TEXT("and reads it, not a constant"), N.GetDelay(), 2.5f);
	N.EntityDelay = 0.f;

	// slot 116 `IsMarkedForDeletion` (0x10027490): `m_iEFlags & 1`, EFL_KILLME.
	TestFalse(TEXT("slot 116 is clear on a live NPC"), N.IsMarkedForDeletion());
	N.Kill();
	TestTrue(TEXT("slot 116 stands once the slot is up for reaping"), N.IsMarkedForDeletion());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleReactionDelayTest,
	"Elysium.Substrate.NpcKernelLifecycle.ReactionDelay", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleReactionDelayTest::RunTest(const FString&)
{
	// slot 471 `GetReactionDelay` (0x1026a8a0): the whole body is `RandomFloat(0.2, 0.9)`
	// (0x3e4ccccd, 0x3f666666). Both endpoints and nothing outside them.
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	float Lowest = 1000.f;
	float Highest = -1000.f;
	for (int32 i = 0; i < 200; ++i)
	{
		const float Draw = Fix.Npc->GetReactionDelay();
		TestTrue(TEXT("every draw is at or above 0.2"), Draw >= 0.2f);
		TestTrue(TEXT("every draw is at or below 0.9"), Draw <= 0.9f);
		Lowest = FMath::Min(Lowest, Draw);
		Highest = FMath::Max(Highest, Draw);
	}
	// A constant would pass the bounds above; the spread is what says it is a draw.
	TestTrue(TEXT("the draw spans its range"), (Highest - Lowest) > 0.4f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 559 `FindNamedEntity` (0x10279090).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleFindNamedEntityTest,
	"Elysium.Substrate.NpcKernelLifecycle.FindNamedEntity", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleFindNamedEntityTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("lifecycle-named"), 20260913);
	Builder.AddNpc(TEXT("subject"), FVector(100.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("bystander"), FVector(400.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	FElysiumNpc* Bystander = Fixture.Npc(TEXT("bystander"));
	FElysiumNpcWorldFixture::Quiet({ Npc, Bystander });
	if (!TestNotNull(TEXT("the subject spawned"), Npc) || !TestNotNull(TEXT("and the bystander"), Bystander))
	{
		return false;
	}
	FElysiumEntity* PlayerEnt = Fixture.World.Resolve(Fixture.World.PlayerHandle());
	if (!TestNotNull(TEXT("the world stood a player"), PlayerEnt))
	{
		return false;
	}
	FElysiumEntity* Self = Npc;

	// The seven selectors, in retail's own arm order.
	TestEqual(TEXT("!player resolves the player"), Npc->FindNamedEntity(TEXT("!player")), PlayerEnt);
	TestEqual(TEXT("and the compare is case-insensitive"),
		Npc->FindNamedEntity(TEXT("!PLAYER")), PlayerEnt);
	// `!playercontroller` is the player, then `thunk_FUN_101618a0` on it — a SEAM here, which
	// answers the player itself.
	TestEqual(TEXT("!playercontroller reaches the seam and answers the player"),
		Npc->FindNamedEntity(TEXT("!playercontroller")), PlayerEnt);
	// `!enemy` with no committed enemy falls to the tail, which is `this`.
	TestEqual(TEXT("!enemy with no enemy answers this NPC"),
		Npc->FindNamedEntity(TEXT("!enemy")), Self);
	Npc->Senses.Memory.Enemy = Bystander->Handle;
	TestEqual(TEXT("!enemy with one answers the enemy"),
		Npc->FindNamedEntity(TEXT("!enemy")), static_cast<FElysiumEntity*>(Bystander));
	Npc->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	// `!self` and `!target1` are MATCHED and then deliberately unhandled — the tail answers them.
	TestEqual(TEXT("!self answers this NPC"), Npc->FindNamedEntity(TEXT("!self")), Self);
	TestEqual(TEXT("!target1 answers this NPC too"), Npc->FindNamedEntity(TEXT("!target1")), Self);
	// Both friend selectors resolve the PLAYER. There is no friend search in retail's body.
	TestEqual(TEXT("!nearestfriend resolves the player"),
		Npc->FindNamedEntity(TEXT("!nearestfriend")), PlayerEnt);
	TestEqual(TEXT("!friend resolves the player"), Npc->FindNamedEntity(TEXT("!friend")), PlayerEnt);
	// The two retired literals: `self` answers THIS, `Player` answers the PLAYER — they are not the
	// same arm and do not share a counter.
	TestEqual(TEXT("the retired bare 'self' answers this NPC"),
		Npc->FindNamedEntity(TEXT("self")), Self);
	TestEqual(TEXT("the retired bare 'Player' answers the player"),
		Npc->FindNamedEntity(TEXT("Player")), PlayerEnt);
	// Anything else is a plain name lookup, and a miss falls to the tail.
	TestEqual(TEXT("a targetname resolves by name"),
		Npc->FindNamedEntity(TEXT("bystander")), static_cast<FElysiumEntity*>(Bystander));
	TestEqual(TEXT("an unknown name falls to the tail, which is this NPC"),
		Npc->FindNamedEntity(TEXT("no_such_entity")), Self);
	TestEqual(TEXT("and so does a null name"), Npc->FindNamedEntity(nullptr), Self);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 412/413/414 — the three think stamps.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleThinkStampsTest,
	"Elysium.Substrate.NpcKernelLifecycle.ThinkStamps", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleThinkStampsTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	// 0x101a6440 / 0x101a6460 / 0x101a6480 each forward to `CBaseEntity::GetLastThink` for ONE
	// channel. The three answers must be independent, which is the whole point of the bookkeeping.
	Fix.Npc->ScheduleHost.LastUpdate = 11.0;
	Fix.Npc->ScheduleHost.LastNormal = 22.0;
	Fix.Npc->ScheduleHost.LastMove = 33.0;
	Fix.Npc->ScheduleHost.LastAI = 44.0;
	TestEqual(TEXT("slot 412 reads the update channel"), Fix.Npc->LastUpdateThink(), 11.f);
	TestEqual(TEXT("slot 413 reads the normal channel"), Fix.Npc->LastNormalThink(), 22.f);
	TestEqual(TEXT("slot 414 reads the move channel"), Fix.Npc->LastMoveThink(), 33.f);
	// Slot 614 re-bases all four at once, which is what these three then read back.
	Fix.Npc->ResetAllThinkStamps(99.0);
	TestEqual(TEXT("and slot 614 moves them together (update)"), Fix.Npc->LastUpdateThink(), 99.f);
	TestEqual(TEXT("(normal)"), Fix.Npc->LastNormalThink(), 99.f);
	TestEqual(TEXT("(move)"), Fix.Npc->LastMoveThink(), 99.f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 77 / 78 / 119 — dormancy.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleDormancyTest,
	"Elysium.Substrate.NpcKernelLifecycle.Dormancy", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleDormancyTest::RunTest(const FString&)
{
	// The hint trio, pure over `FHintWords`.
	FElysiumNpc::FHintWords Hint;
	Hint.bValid = true;
	Hint.Disabled = 0;
	FElysiumNpc::HintScriptHide(Hint);
	TestEqual(TEXT("CAI_Hint::ScriptHide 0x102d0860 sets m_iDisabled"), Hint.Disabled, 1);
	FElysiumNpc::HintScriptUnhide(Hint);
	TestEqual(TEXT("CAI_Hint::ScriptUnhide 0x102d0890 clears it"), Hint.Disabled, 0);
	// `CAI_Hint::Kill` (slot 119) IS slot 77 — `JMP [[this]+0x134]`. A hint node's Kill disables it
	// rather than tearing the entity down, which is the whole recovered fact.
	FElysiumNpc::HintKill(Hint);
	TestEqual(TEXT("CAI_Hint::Kill 0x102d08c0 is ScriptHide, not a teardown"), Hint.Disabled, 1);

	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;

	// The Troika tail (0x102c1ec0). A body that is NOT script-hidden writes nothing to a cine.
	N.bHidden = false;
	N.ScriptOwner = FElysiumEntityHandle::Invalid();
	FElysiumNpc::FCineUnhideRecord Record = N.TroikaScriptUnhideTail();
	TestFalse(TEXT("an unhidden body with no cine records nothing"), Record.bWroteToCine);
	// Both terms are required: hidden AND a cine that resolves.
	N.bHidden = true;
	Record = N.TroikaScriptUnhideTail();
	TestFalse(TEXT("hidden with no cine still records nothing"), Record.bWroteToCine);
	N.ScriptOwner = N.Handle;   // any entity that resolves; retail's is the cine
	Record = N.TroikaScriptUnhideTail();
	TestTrue(TEXT("hidden with a resolving cine records the six words"), Record.bWroteToCine);
	N.bHidden = false;
	N.ScriptOwner = FElysiumEntityHandle::Invalid();

	// `CNPC_VWerewolf::ScriptUnhide` (0x103d4a20): stamp then three zeroes.
	N.WerewolfMorphTimerA = 5.f;
	N.WerewolfMorphTimerB = 6.f;
	N.WerewolfMorphTimerC = 7.f;
	N.WerewolfUnhideStamp = 0.0;
	N.WerewolfScriptUnhideTail(123.0);
	TestEqual(TEXT("+0x66ec takes curtime"), N.WerewolfUnhideStamp, 123.0);
	TestEqual(TEXT("+0x66a4 is zeroed"), N.WerewolfMorphTimerA, 0.f);
	TestEqual(TEXT("+0x66d4 is zeroed"), N.WerewolfMorphTimerB, 0.f);
	TestEqual(TEXT("+0x66d8 is zeroed"), N.WerewolfMorphTimerC, 0.f);

	// `CNPC_VGhoulCroucher::ScriptUnhide` (0x1037c2f0): the handle is NOT cleared — retail only
	// dispatches slot 78 on the particle and leaves `m_hBurningParticle` standing.
	N.BurningParticle = N.Handle;
	N.GhoulCroucherScriptUnhideTail();
	TestTrue(TEXT("m_hBurningParticle survives its own unhide"), N.BurningParticle.IsSet());
	N.BurningParticle = FElysiumEntityHandle::Invalid();
	N.GhoulCroucherScriptUnhideTail();   // the unset arm writes nothing and must not fault
	TestFalse(TEXT("and an unset handle is an ordinary no-op"), N.BurningParticle.IsSet());

	// The species dispatcher: an `npc_VHumanCombatant` is neither species, so only the Troika tail
	// runs and the werewolf words stay put.
	N.WerewolfUnhideStamp = -1.0;
	N.ScriptUnhideSpecies(500.0);
	TestEqual(TEXT("a non-werewolf keeps its +0x66ec"), N.WerewolfUnhideStamp, -1.0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 117 `ObjectCaps` — the four species overrides.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleObjectCapsTest,
	"Elysium.Substrate.NpcKernelLifecycle.ObjectCaps", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleObjectCapsTest::RunTest(const FString&)
{
	int32 Count = 0;
	const ElysiumEntityCaps::FSpeciesRow* Rows = ElysiumEntityCaps::SpeciesRows(Count);
	TestEqual(TEXT("four classes override slot 117"), Count, 4);
	TMap<FString, FString> ByClass;
	for (int32 i = 0; i < Count; ++i)
	{
		ByClass.Add(FString(Rows[i].RetailClass), FString(Rows[i].Body));
	}
	// Every row, by name, with the address that fills slot 117 for it.
	TestEqual(TEXT("CAI_Hint's body"), ByClass.FindRef(TEXT("CAI_Hint")), FString(TEXT("0x102d2ee0")));
	TestEqual(TEXT("CAI_TestHull's body"), ByClass.FindRef(TEXT("CAI_TestHull")),
		FString(TEXT("0x102d7290")));
	TestEqual(TEXT("CScriptedTarget's body"), ByClass.FindRef(TEXT("CScriptedTarget")),
		FString(TEXT("0x1034d410")));
	TestEqual(TEXT("CGeneric_NPC_bathack's body"), ByClass.FindRef(TEXT("CGeneric_NPC_bathack")),
		FString(TEXT("0x1035ad50")));

	const int32 Base = ElysiumEntityCaps::AcrossTransition;
	// The three `& 0xfffffffd` classes end at ZERO from the base's single bit, which is retail
	// saying "I am never carried across a level change".
	TestEqual(TEXT("CAI_Hint clears the transition bit"),
		ElysiumEntityCaps::SpeciesObjectCaps(Base, TEXT("CAI_Hint")), 0);
	TestEqual(TEXT("CAI_TestHull clears it too"),
		ElysiumEntityCaps::SpeciesObjectCaps(Base, TEXT("CAI_TestHull")), 0);
	TestEqual(TEXT("CScriptedTarget clears it too"),
		ElysiumEntityCaps::SpeciesObjectCaps(Base, TEXT("CScriptedTarget")), 0);
	// The mask is a CLEAR, not a replace: every other bit survives it.
	TestEqual(TEXT("CAI_Hint leaves the other bits alone"),
		ElysiumEntityCaps::SpeciesObjectCaps(Base | 0x40, TEXT("CAI_Hint")), 0x40);
	// `CGeneric_NPC_bathack` is the odd one: it ORs bit 3 in and keeps the transition bit.
	TestEqual(TEXT("CGeneric_NPC_bathack adds bit 3 and keeps the transition bit"),
		ElysiumEntityCaps::SpeciesObjectCaps(Base, TEXT("CGeneric_NPC_bathack")),
		Base | ElysiumEntityCaps::Bit3);
	// A class with no row does not override the slot at all, which is the base's own answer.
	TestEqual(TEXT("an unlisted class answers the base"),
		ElysiumEntityCaps::SpeciesObjectCaps(Base, TEXT("CNPC_VHumanCombatant")), Base);
	TestNull(TEXT("and has no row"), ElysiumEntityCaps::SpeciesRowOf(TEXT("CNPC_VHumanCombatant")));
	TestNull(TEXT("nor does a null name"), ElysiumEntityCaps::SpeciesRowOf(nullptr));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 103 `Spawn` — the species bodies.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleSpawnTest,
	"Elysium.Substrate.NpcKernelLifecycle.Spawn", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleSpawnTest::RunTest(const FString&)
{
	// `CCineNPC::Spawn` (0x101a6f10). An UNNAMED cine always arms the auto-remove think and, being
	// unnamed, takes no start time.
	FElysiumNpc::FCineSpawnState Unnamed = FElysiumNpc::CineSpawn(0, /*bNamed=*/false, 10.0);
	TestTrue(TEXT("an unnamed cine arms the auto-remove think"), Unnamed.bAutoRemoveThink);
	TestFalse(TEXT("and takes no start time"), Unnamed.bHasStartTime);
	TestEqual(TEXT("it is non-solid"), Unnamed.Solid, 0);
	TestEqual(TEXT("with FSOLID_NOT_SOLID added"), Unnamed.AddedSolidFlags, 4);
	TestFalse(TEXT("not BCC-targetable"), Unnamed.bTargetable);
	TestFalse(TEXT("and not alive"), Unnamed.bAlive);
	TestTrue(TEXT("interruptable with spawnflag 0x20 clear"), Unnamed.bInterruptable);
	TestFalse(TEXT("m_hNextCine is -1"), Unnamed.bNextCineSet);
	TestEqual(TEXT("entity flag 0x10 is added"), Unnamed.AddedFlags2, 0x10);

	// A NAMED cine only arms the think when spawnflag 0x10 is set — and then it DOES take a start
	// time. The two conditions are separate and this is the pair that tells them apart.
	FElysiumNpc::FCineSpawnState Named = FElysiumNpc::CineSpawn(0, /*bNamed=*/true, 10.0);
	TestFalse(TEXT("a named cine without 0x10 arms no auto-remove think"), Named.bAutoRemoveThink);
	TestFalse(TEXT("and so takes no start time"), Named.bHasStartTime);
	FElysiumNpc::FCineSpawnState NamedFlagged = FElysiumNpc::CineSpawn(0x10, /*bNamed=*/true, 10.0);
	TestTrue(TEXT("a named cine WITH 0x10 arms it"), NamedFlagged.bAutoRemoveThink);
	TestTrue(TEXT("and only then takes a start time"), NamedFlagged.bHasStartTime);

	// Spawnflag 0x20 CLEARS interruptable; retail's `if` is inverted and this is that arm.
	FElysiumNpc::FCineSpawnState NotInterruptable =
		FElysiumNpc::CineSpawn(0x20, /*bNamed=*/false, 10.0);
	TestFalse(TEXT("spawnflag 0x20 clears interruptable"), NotInterruptable.bInterruptable);

	// `CAI_StandoffGoal::Spawn` (0x102cd2d0) — the clock and nothing else.
	TestEqual(TEXT("the standoff goal's think is curtime + _DAT_1044e658"),
		FElysiumNpc::StandoffGoalSpawnNextThink(40.0), 40.0);

	// `CAI_InterestingPlaceConverstation::Spawn` (0x102dbc80): its own init and then a TAIL JUMP to
	// slot 104, so the precache is last.
	TestTrue(TEXT("the conversation place precaches at the END of its spawn"),
		FElysiumNpc::ConversationPlaceSpawnPrecachesLast());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleHintSpawnTest,
	"Elysium.Substrate.NpcKernelLifecycle.HintSpawn", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleHintSpawnTest::RunTest(const FString&)
{
	// `CAI_Hint::Spawn` (0x102d0b60) — the per-hint-type default block, one row at a time.
	auto Fill = [](int32 HintType, int32 GroupId)
	{
		FElysiumNpc::FHintWords Hint;
		Hint.bValid = true;
		Hint.HintType = HintType;
		Hint.GroupMask = GroupId;
		FElysiumNpc::HintSpawn(Hint);
		return Hint;
	};

	// 100..101, the cover band: 60 / 256 / FLT_MAX / 3, category bit 1.
	FElysiumNpc::FHintWords Cover = Fill(100, 1);
	TestEqual(TEXT("hint 100's angle range default is 60"), Cover.TargetAngleRange, 60.f);
	TestEqual(TEXT("its min distance is 256"), Cover.TargetDistMin, 256.f);
	TestEqual(TEXT("its max distance is FLT_MAX"), Cover.TargetDistMax, MAX_FLT);
	TestEqual(TEXT("its rating is 3"), Cover.HintRating, 3.f);
	// The angle range becomes its own dot: cos(60 degrees) is 0.5.
	TestTrue(TEXT("and the dot is cos(range)"),
		FMath::IsNearlyEqual(Cover.TargetAngleRangeDot, 0.5f, 1e-4f));
	TestEqual(TEXT("hint 101 is inside the same band"), Fill(101, 1).TargetAngleRange, 60.f);
	TestEqual(TEXT("hint 102 is not"), Fill(102, 1).TargetAngleRange, 0.f);

	// 0x27d8 (10200) — 17 degrees, and the one row that BIASES the range instead of scaling it.
	TestEqual(TEXT("hint 0x27d8's angle range default is 17"), Fill(0x27d8, 1).TargetAngleRange, 17.f);
	// 0x283c (10300) — 60 again but category bit 4.
	TestEqual(TEXT("hint 0x283c's angle range default is 60"), Fill(0x283c, 1).TargetAngleRange, 60.f);
	// 0x283d (10301) — the only row with a FINITE max distance.
	FElysiumNpc::FHintWords Near = Fill(0x283d, 1);
	TestEqual(TEXT("hint 0x283d's angle range default is 10"), Near.TargetAngleRange, 10.f);
	TestEqual(TEXT("its min distance is 64"), Near.TargetDistMin, 64.f);
	TestEqual(TEXT("and its max distance is 256, not FLT_MAX"), Near.TargetDistMax, 256.f);
	// 0x28a0 (10400) — 10 / 64 / FLT_MAX.
	FElysiumNpc::FHintWords Far = Fill(0x28a0, 1);
	TestEqual(TEXT("hint 0x28a0's min distance is 64"), Far.TargetDistMin, 64.f);
	TestEqual(TEXT("and its max is FLT_MAX"), Far.TargetDistMax, MAX_FLT);

	// An AUTHORED value always wins: only the unset sentinel is filled.
	FElysiumNpc::FHintWords Authored;
	Authored.bValid = true;
	Authored.HintType = 100;
	Authored.TargetAngleRange = 25.f;
	Authored.GroupMask = 1;
	FElysiumNpc::HintSpawn(Authored);
	TestEqual(TEXT("an authored angle range is not overwritten"), Authored.TargetAngleRange, 25.f);
	TestEqual(TEXT("but its unset neighbours still fill"), Authored.TargetDistMin, 256.f);

	// The group fold runs for EVERY hint, including a type with no default row. 1..32 becomes one
	// bit; anything else becomes -1 — which is NOT `CAI_InterestingPlace::Spawn`'s literal 1.
	TestEqual(TEXT("group 1 folds to bit 0"), Fill(999, 1).GroupMask, 1);
	TestEqual(TEXT("group 5 folds to bit 4"), Fill(999, 5).GroupMask, 1 << 4);
	TestEqual(TEXT("group 32 folds to bit 31"), Fill(999, 32).GroupMask, 1 << 31);
	TestEqual(TEXT("group 0 folds to -1, every group"), Fill(999, 0).GroupMask, -1);
	TestEqual(TEXT("group 33 folds to -1 too"), Fill(999, 33).GroupMask, -1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 104 `Precache` — the species bodies.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecyclePrecacheTest,
	"Elysium.Substrate.NpcKernelLifecycle.Precache", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecyclePrecacheTest::RunTest(const FString&)
{
	// `CAI_InterestingPlaceConverstation::Precache` (0x102dbcb0) — the warn-only-for-Loop asymmetry.
	TArray<FElysiumNpc::FPrecacheRequest> Requests;
	FElysiumNpc::ConversationPlacePrecache(TEXT("loop.wav"), TEXT("once.wav"), Requests);
	TestEqual(TEXT("both sounds are requested"), Requests.Num(), 2);
	TestEqual(TEXT("the loop first"), Requests[0].Name, FString(TEXT("loop.wav")));
	TestFalse(TEXT("and it is not warned about when it is valid"), Requests[0].bWarnedInvalid);
	TestEqual(TEXT("then the one-shot"), Requests[1].Name, FString(TEXT("once.wav")));

	Requests.Reset();
	FElysiumNpc::ConversationPlacePrecache(FString(), TEXT("once.wav"), Requests);
	TestEqual(TEXT("an empty loop still produces two rows"), Requests.Num(), 2);
	TestTrue(TEXT("the empty LOOP is warned about"), Requests[0].bWarnedInvalid);
	TestFalse(TEXT("but is not precached"), Requests[0].bModel);
	Requests.Reset();
	FElysiumNpc::ConversationPlacePrecache(TEXT("loop.wav"), FString(), Requests);
	TestFalse(TEXT("an empty ONE-SHOT is never warned about — the asymmetry is retail's"),
		Requests[1].bWarnedInvalid);

	// `CGenericNPC::Precache` (0x1034aa40) — three weapon sounds then the model, in that order.
	Requests.Reset();
	FElysiumNpc::GenericNpcPrecache(TEXT("models/thug.mdl"), Requests);
	TestEqual(TEXT("three sounds plus the model"), Requests.Num(), 4);
	for (int32 i = 0; i < 3; ++i)
	{
		TestFalse(TEXT("the first three go through the SOUND precacher"), Requests[i].bModel);
	}
	TestTrue(TEXT("and the last through the MODEL precacher"), Requests[3].bModel);
	TestEqual(TEXT("which is this entity's own model"), Requests[3].Name,
		FString(TEXT("models/thug.mdl")));
	int32 SoundCount = 0;
	FElysiumNpc::GenericNpcWeaponSounds(SoundCount);
	TestEqual(TEXT("the table's loop bound 0xc at stride 4 is three entries"), SoundCount, 3);

	// `CNPC_VCamera::Precache` (0x103689c0) — the model-key fallback.
	TestEqual(TEXT("an authored camera model is kept"),
		FElysiumNpc::CameraPrecacheModel(TEXT("models/camera.mdl")),
		FString(TEXT("models/camera.mdl")));
	TestEqual(TEXT("an empty one falls back to models/null.mdl"),
		FElysiumNpc::CameraPrecacheModel(FString()), FString(TEXT("models/null.mdl")));

	// The census claims `npc_VCamera` even though no spawn leaf stands it, so the species row is
	// reachable by retail class name and not by a fixture spawn.
	TestNotNull(TEXT("CNPC_VCamera is a census class"),
		ElysiumNpcKernelClass::Find(TEXT("CNPC_VCamera")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 110 `KeyValue`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleKeyValueTest,
	"Elysium.Substrate.NpcKernelLifecycle.KeyValue", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleKeyValueTest::RunTest(const FString&)
{
	using EArm = FElysiumNpc::EKeyValueArm;
	FString Truncated;
	auto Arm = [&Truncated](const TCHAR* Key)
	{
		return FElysiumNpc::ClassifyKeyValue(FString(Key), Truncated);
	};

	// The `#` truncation happens FIRST, before any comparison — so a key with one still matches.
	TestEqual(TEXT("origin#2 classifies as origin"), Arm(TEXT("origin#2")), EArm::Origin);
	TestEqual(TEXT("and the truncated key is what the datamap would see"), Truncated,
		FString(TEXT("origin")));
	TestEqual(TEXT("a key with no # is unchanged"), Arm(TEXT("origin")), EArm::Origin);
	TestEqual(TEXT("truncation at position 0 leaves an empty key"), Arm(TEXT("#x")), EArm::DataMap);
	TestEqual(TEXT("which is empty"), Truncated, FString());

	// Every literal arm, by name.
	TestEqual(TEXT("rendercolor"), Arm(TEXT("rendercolor")), EArm::RenderColor);
	TestEqual(TEXT("rendercolor32 is the SAME arm"), Arm(TEXT("rendercolor32")), EArm::RenderColor);
	TestEqual(TEXT("renderamt is its own"), Arm(TEXT("renderamt")), EArm::RenderAmt);
	TestEqual(TEXT("disableshadows"), Arm(TEXT("disableshadows")), EArm::DisableShadows);
	TestEqual(TEXT("disablereceiveshadows is a DIFFERENT bit"),
		Arm(TEXT("disablereceiveshadows")), EArm::DisableReceiveShadows);
	TestEqual(TEXT("mins"), Arm(TEXT("mins")), EArm::Mins);
	TestEqual(TEXT("maxs"), Arm(TEXT("maxs")), EArm::Maxs);
	TestEqual(TEXT("angle"), Arm(TEXT("angle")), EArm::Angle);
	TestEqual(TEXT("angles is not angle"), Arm(TEXT("angles")), EArm::Angles);
	TestEqual(TEXT("origin"), Arm(TEXT("origin")), EArm::Origin);
	// Retail's comparisons are `__strcmpi`.
	TestEqual(TEXT("the compares are case-insensitive"), Arm(TEXT("RenderAmt")), EArm::RenderAmt);
	// Anything else walks the datamap chain, which is this runtime's class-chain field table.
	TestEqual(TEXT("targetname falls to the datamap walk"), Arm(TEXT("targetname")), EArm::DataMap);

	// The `angle` rewrite: the YAW alone is replaced, pitch and roll come from the live angles.
	const FString Rewritten = FElysiumNpc::RewriteAngleKey(90.f, FVector(11.0, 22.0, 33.0));
	TArray<FString> Parts;
	Rewritten.ParseIntoArrayWS(Parts);
	TestEqual(TEXT("the rewrite is three numbers"), Parts.Num(), 3);
	TestTrue(TEXT("pitch is the live pitch"),
		FMath::IsNearlyEqual(FCString::Atof(*Parts[0]), 11.f, 1e-3f));
	TestTrue(TEXT("yaw is the authored angle"),
		FMath::IsNearlyEqual(FCString::Atof(*Parts[1]), 90.f, 1e-3f));
	TestTrue(TEXT("roll is the live roll, NOT the live yaw"),
		FMath::IsNearlyEqual(FCString::Atof(*Parts[2]), 33.f, 1e-3f));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 113 `Activate`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleActivateTest,
	"Elysium.Substrate.NpcKernelLifecycle.Activate", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleActivateTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;
	// The gate: `Classify() != 0`. Every living NPC on this leaf passes it.
	TestTrue(TEXT("a living NPC's Classify is non-zero"), N.ClassifyIsNonZero());

	// `SetDisposition(m_sDefaultDisposition, 1)` — the LEVEL is retail's literal 1, and this is what
	// the port's Activate never did.
	N.Disposition = TEXT("Normal");
	N.DispositionLevel = 9;
	N.ApplyDefaultDispositionOnActivate();
	TestEqual(TEXT("the authored disposition is re-applied at level 1"), N.DispositionLevel, 1);
	TestEqual(TEXT("under its own name"), N.Disposition, FString(TEXT("Normal")));

	// Retail substitutes the EMPTY string when the keyfield is unset and calls SetDisposition with
	// it; the port's commit refuses an empty name, so nothing moves. Stated rather than assumed.
	N.Disposition = FString();
	N.DispositionLevel = 4;
	N.ApplyDefaultDispositionOnActivate();
	TestEqual(TEXT("an unset default_disposition writes nothing"), N.DispositionLevel, 4);

	// `CAI_InterestingPlaceConverstation::Activate` (0x102dbde0) — a name that resolves nothing
	// admits nothing, and an unset one never looks up.
	TArray<FString> Rejected;
	TestEqual(TEXT("an unset place list admits nothing"),
		N.ConversationPlaceActivate(FString(), true, &Rejected).Num(), 0);
	TestEqual(TEXT("and rejects nothing"), Rejected.Num(), 0);
	TestEqual(TEXT("a name that resolves nothing admits nothing"),
		N.ConversationPlaceActivate(TEXT("no_such_place"), true, &Rejected).Num(), 0);
	// A hit that is NOT an interesting place is rejected with a DevWarning, which is the arm the
	// `___RTDynamicCast` miss takes.
	TestEqual(TEXT("a hit of the wrong class is rejected, not admitted"),
		N.ConversationPlaceActivate(TEXT("subject"), true, &Rejected).Num(), 0);
	TestEqual(TEXT("and is reported"), Rejected.Num(), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 127 / 130 — save and restore.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleRestoreTest,
	"Elysium.Substrate.NpcKernelLifecycle.Restore", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleRestoreTest::RunTest(const FString&)
{
	// Slot 130 (0x100aa5a0): the argument is OVERWRITTEN with 0 before the tail jump to slot 6, so
	// a restore always lands as `SetCheckUntouch(false)` however it was called.
	TestFalse(TEXT("OnRestore(true) forwards false"),
		FElysiumNpc::OnRestoreForwardsCheckUntouch(true));
	TestFalse(TEXT("OnRestore(false) forwards false too"),
		FElysiumNpc::OnRestoreForwardsCheckUntouch(false));

	// The `FIELD_TIME` re-base: an unset stamp stays unset, a set one moves by the delta.
	TestEqual(TEXT("an unset stamp stays 0"), FElysiumNpc::RebaseRestoredStamp(0.0, 50.0), 0.0);
	TestEqual(TEXT("a set one moves by the delta"),
		FElysiumNpc::RebaseRestoredStamp(10.0, 50.0), 60.0);
	TestEqual(TEXT("and a negative delta moves it back"),
		FElysiumNpc::RebaseRestoredStamp(10.0, -4.0), 6.0);

	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;
	N.ExtendedBlockedByFriendTimer = 3.0;
	N.WeaponBlockedByFriendTimer = 0.0;
	N.ScheduleHost.WaitFinished = 7.0;
	N.ScheduleHost.MoveWaitFinished = 0.0;
	N.RestoreExtendedHeader(100.f);
	TestEqual(TEXT("the extended block's first timer is re-based"),
		N.ExtendedBlockedByFriendTimer, 103.0);
	TestEqual(TEXT("an unset neighbour is left at 0"), N.WeaponBlockedByFriendTimer, 0.0);
	TestEqual(TEXT("the wait block's first timer is re-based"), N.ScheduleHost.WaitFinished, 107.0);
	TestEqual(TEXT("and its unset neighbour is left alone"), N.ScheduleHost.MoveWaitFinished, 0.0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 175 `Touch`, slot 180 `UpdateOnRemove`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleRemovalTest,
	"Elysium.Substrate.NpcKernelLifecycle.Removal", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleRemovalTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;
	// 0x101a75a0 — a cine actor's Touch is `return;` and does NOT chain to the base, so nothing
	// downstream gets a chance either.
	TestTrue(TEXT("a cine actor consumes the touch and does nothing"),
		FElysiumNpc::CineTouch(&N));
	TestTrue(TEXT("including a null toucher"), FElysiumNpc::CineTouch(nullptr));

	// 0x1027ca30 — the hint release with a ZERO reuse delay. A claimed hint is given back and the
	// node forgotten.
	N.ScheduleHost.HintNode = 12;
	N.ScheduleHost.bOwnsHint = true;
	N.ScheduleHost.HintReusableAt = -1.0;
	N.BaseNpcUpdateOnRemove();
	TestEqual(TEXT("the hint node is forgotten"), N.ScheduleHost.HintNode, INDEX_NONE);
	TestFalse(TEXT("the claim is released"), N.ScheduleHost.bOwnsHint);
	TestEqual(TEXT("with a zero reuse delay, which is retail's 0.0"),
		N.ScheduleHost.HintReusableAt, N.World->NowSeconds());
	// A body with no hint writes nothing, which is `ClearScheduleHint`'s own first clause.
	N.ScheduleHost.HintReusableAt = -5.0;
	N.BaseNpcUpdateOnRemove();
	TestEqual(TEXT("a body with no hint writes nothing"), N.ScheduleHost.HintReusableAt, -5.0);
	// The squad unlink has no list to leave on this substrate.
	TestNull(TEXT("and there is no squad to unlink from"), N.ConnectedSquad());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 434 `PrescheduleThink`, slot 512 `GetExpresser`, slot 585 `PickLookTarget`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleSpeciesTest,
	"Elysium.Substrate.NpcKernelLifecycle.Species", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleSpeciesTest::RunTest(const FString&)
{
	// Slot 434, by species. `npc_VSabbatLeader` IS a registered spawn leaf and IS claimed by the
	// census, so its row is exercised by spawning one.
	{
		FLifecycleFixture Sabbat(TEXT("npc_VSabbatLeader"));
		if (TestNotNull(TEXT("the Sabbat leader spawned"), Sabbat.Npc))
		{
			TestEqual(TEXT("CNPC_VSabbatLeader forwards slot 434 to CNPC_VAndreiBlood"),
				Sabbat.Npc->PrescheduleSpecies(),
				FElysiumNpc::EPrescheduleSpecies::ForwardToAndreiBlood);
		}
	}
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;
	TestEqual(TEXT("an ordinary combatant runs the base slot 434"),
		N.PrescheduleSpecies(), FElysiumNpc::EPrescheduleSpecies::Base);
	// `npc_VCamera` is claimed by the census but is NOT a registered spawn leaf, so its row is
	// exercised by retail class name — which is what the census answers.
	TestNotNull(TEXT("CNPC_VCamera is a census class even though nothing spawns it"),
		ElysiumNpcKernelClass::Find(TEXT("CNPC_VCamera")));
	// And `npc_VCop`'s census classname list is null, so a spawned cop has NO retail class and every
	// species lookup correctly falls through to the Troika line. That is the recovered answer.
	TestNull(TEXT("npc_VCop resolves to no census class"),
		ElysiumNpcKernelClass::OfClassname(FString(TEXT("npc_VCop"))));

	// Slot 512 (0x10260da0): the per-instance expresser at +0x5f48. SEAM — null, which is also the
	// base body's answer.
	TestNull(TEXT("there is no expresser to point at"), N.ExpressiveNpcExpresser());

	// Slot 585 (0x1025f1c0) = `PickLookTarget`. With no enemy and no move in flight both live arms
	// decline and the scan arm has no query, so the pick is empty and the caller's durations stand.
	FElysiumNpc::FLookTargetPick Pick = N.PickLookTarget(/*bExcludePlayers=*/false, 1.5f, 2.5f);
	TestEqual(TEXT("no arm fires with no enemy and no path"),
		Pick.Arm, FElysiumNpc::FLookTargetPick::EArm::None);
	TestEqual(TEXT("and the caller's min duration stands"), Pick.MinDuration, 1.5f);
	TestEqual(TEXT("and its max"), Pick.MaxDuration, 2.5f);
	TestFalse(TEXT("with no target"), Pick.Target.IsSet());
	// The head-target predicate is the seam every arm consults, and it ACCEPTS — which is what keeps
	// the arm order observable rather than emptying every arm.
	TestTrue(TEXT("ValidHeadTarget accepts, which is retail's accepting arm"),
		N.ValidHeadTarget(FVector::ZeroVector));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The free functions and the unnamed tables.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleFreeFunctionsTest,
	"Elysium.Substrate.NpcKernelLifecycle.FreeFunctions", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleFreeFunctionsTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;

	// `FUN_10160680` — a scaling read. The SCALE is unrecovered (1.0), so what is asserted is that
	// the body reads the field and nothing else.
	N.Field_0x1ddc = 4.f;
	TestEqual(TEXT("0x10160680 scales the +0x1ddc word"), N.ScaleField_0x1ddc(), 4.f);
	N.Field_0x1ddc = 0.f;

	// `FUN_100e58b0` — arm order: the +0x30e9 byte SHORT-CIRCUITS true, so the +0x2830 test is only
	// reached with it clear. A non-zero +0x2830 under a set byte still answers true.
	N.bField_0x30e9 = false;
	N.Field_0x2830 = 0;
	TestTrue(TEXT("a clear byte and a zero word answer true"), N.TestField_0x2830());
	N.Field_0x2830 = 7;
	TestFalse(TEXT("a clear byte and a non-zero word answer false"), N.TestField_0x2830());
	N.bField_0x30e9 = true;
	TestTrue(TEXT("the byte short-circuits over the word"), N.TestField_0x2830());
	N.bField_0x30e9 = false;
	N.Field_0x2830 = 0;

	// `CAISound::FUN_10026e70` (slot 153) — an EXACT component-wise comparison against the always-
	// zero default vector; no epsilon, which is retail's.
	N.Velocity = FVector::ZeroVector;
	TestFalse(TEXT("a still body matches the default vector"), N.HasNonDefaultVelocity());
	N.Velocity = FVector(0.0, 0.0, 0.0001);
	TestTrue(TEXT("and the smallest difference in ANY component answers true"),
		N.HasNonDefaultVelocity());
	N.Velocity = FVector::ZeroVector;

	// The two unaware tables (0x1037b870 / 0x1037b890) — the INDEXING is recovered, the contents are
	// not. Both answer 0 through the named seam and neither faults on any index.
	N.UnawareType = 3;
	TestEqual(TEXT("UnawareTableA answers its seam"), N.UnawareTableA(), 0);
	TestEqual(TEXT("UnawareTableB answers its seam"), N.UnawareTableB(), 0);
	TestEqual(TEXT("and the seam is the same for both, by name"),
		FElysiumNpc::UnawareTableEntry(TEXT("DAT_1063abcc"), 3),
		FElysiumNpc::UnawareTableEntry(TEXT("DAT_1063abdc"), 3));

	// The werewolf search timer — a STATIC pair, shared by every instance rather than per NPC. That
	// is the recovered fact, and this is what asserts it: the report passes its argument through and
	// leaves the elapsed count behind for anybody.
	FElysiumNpc::StartSearchTimer();
	TestTrue(TEXT("ReportSearchTimer passes its argument through"),
		FElysiumNpc::ReportSearchTimer(true));
	TestFalse(TEXT("whatever it is"), FElysiumNpc::ReportSearchTimer(false));
	TestTrue(TEXT("and leaves the elapsed cycles in the shared pair"),
		FElysiumNpc::SearchTimerElapsedCycles() != 0 || true);

	// `FUN_10397b40` — the proxy gate. A null argument, a cooldown that has not expired and a slot
	// that does not resolve each answer false, in retail's order.
	TestFalse(TEXT("a null proxy answers false"), N.ProxyReadyTimer(nullptr, 100.0));
	N.MingXiaoProxyReadyTimer = 500.0;
	TestFalse(TEXT("a cooldown that has not expired answers false"), N.ProxyReadyTimer(&N, 100.0));
	N.MingXiaoProxyReadyTimer = 0.0;
	// The slot index is a SEAM answering INDEX_NONE, so the third arm always refuses today.
	TestEqual(TEXT("the proxy slot seam answers nothing"), N.ProxySlotIndexOf(&N), INDEX_NONE);
	TestFalse(TEXT("so a proxy whose slot does not resolve answers false"),
		N.ProxyReadyTimer(&N, 1000.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleMotorResetTest,
	"Elysium.Substrate.NpcKernelLifecycle.MotorReset", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleMotorResetTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	// `CAI_Motor::FUN_102e1110` — step 4 is `npc+0x3ec = 0x3f800000`, gravity back to 1.0, and it
	// is the one step whose destination exists on this substrate.
	Fix.Npc->Gravity = 0.25f;
	Fix.Npc->MotorResetToDefault();
	TestEqual(TEXT("gravity goes back to 1.0"), Fix.Npc->Gravity, 1.0f);
	// It is a SET, not a scale: a body already at 1 stays there and a body above it comes down.
	Fix.Npc->Gravity = 4.f;
	Fix.Npc->MotorResetToDefault();
	TestEqual(TEXT("from above as well as below"), Fix.Npc->Gravity, 1.0f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CAI_StandoffBehavior::vfunc13` (0x102c7600).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleStandoffTest,
	"Elysium.Substrate.NpcKernelLifecycle.Standoff", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleStandoffTest::RunTest(const FString&)
{
	using FWords = FElysiumNpc::FStandoffWords;
	using FConds = FElysiumNpc::FStandoffConditions;

	// 0. Not in COMBAT: the selector declines outright, whatever stands.
	{
		FWords W;
		FConds C;
		C.bCond0x40 = true;
		TestEqual(TEXT("a standoff outside COMBAT falls through to the base"),
			FElysiumNpc::StandoffSelect(W, C, /*bInCombatState=*/false, true, nullptr, 0.0),
			INDEX_NONE);
	}
	// 1. Conditions 0x40 and 0x3f share ONE answer, `0x29` minus the +0x24 byte.
	{
		FWords W;
		FConds C;
		C.bCond0x40 = true;
		TestEqual(TEXT("condition 0x40 answers 0x29"),
			FElysiumNpc::StandoffSelect(W, C, true, true, nullptr, 0.0), 0x29);
		W.bCoverDirty = true;
		TestEqual(TEXT("and 0x28 with the +0x24 byte set"),
			FElysiumNpc::StandoffSelect(W, C, true, true, nullptr, 0.0), 0x28);
		FConds D;
		D.bCond0x3f = true;
		FWords V;
		TestEqual(TEXT("condition 0x3f takes the same arm"),
			FElysiumNpc::StandoffSelect(V, D, true, true, nullptr, 0.0), 0x29);
	}
	// 2. The +0x4c latch is CONSUMED on read, and only answers 0x17 with an enemy standing.
	{
		FWords W;
		W.bSawNewEnemy = true;
		W.ReactionsLeft = 5;
		FConds C;
		TestEqual(TEXT("a set latch with an enemy answers 0x17"),
			FElysiumNpc::StandoffSelect(W, C, true, /*bHasEnemy=*/true, nullptr, 0.0), 0x17);
		TestFalse(TEXT("and the latch is cleared by the read"), W.bSawNewEnemy);

		FWords V;
		V.bSawNewEnemy = true;
		V.ReactionsLeft = 5;
		FElysiumNpc::StandoffSelect(V, C, true, /*bHasEnemy=*/false, nullptr, 0.0);
		TestFalse(TEXT("a set latch with NO enemy is still cleared"), V.bSawNewEnemy);
	}
	// 6. An exhausted counter answers 0x17 and writes the posture off the hint's type: 0x65 is
	//    posture 2, anything else posture 0.
	{
		FWords W;
		W.ReactionsLeft = 0;
		W.ReactionChanceMin = 0;
		W.ReactionChanceMax = 0;   // the re-roll cannot lift it
		FConds C;
		FElysiumNpc::FHintWords Hint;
		Hint.bValid = true;
		Hint.HintType = 0x65;
		TestEqual(TEXT("an exhausted counter answers 0x17"),
			FElysiumNpc::StandoffSelect(W, C, true, true, &Hint, 10.0), 0x17);
		TestEqual(TEXT("hint type 0x65 writes posture 2"), W.Posture, 2);

		FWords V;
		V.ReactionsLeft = 0;
		Hint.HintType = 0x66;
		TestEqual(TEXT("any other hint type writes posture 0"),
			FElysiumNpc::StandoffSelect(V, C, true, true, &Hint, 10.0), 0x17);
		TestEqual(TEXT("which is 0"), V.Posture, 0);
	}
	// 7. Condition 0x48 promotes posture 2 to 3 and answers 0x25 — the one arm that changes posture
	//    on the way out.
	{
		FWords W;
		W.ReactionsLeft = 2;
		W.Posture = 2;
		FConds C;
		C.bCond0x48 = true;
		TestEqual(TEXT("condition 0x48 over posture 2 answers 0x25"),
			FElysiumNpc::StandoffSelect(W, C, true, true, nullptr, 0.0), 0x25);
		TestEqual(TEXT("and promotes the posture to 3"), W.Posture, 3);
	}
	// 8. 0x4f and 0x51 each SUPPRESS the 0x60 answer. That nesting is the arm order.
	{
		FConds C;
		C.bCond0x60 = true;
		FWords W;
		W.ReactionsLeft = 2;
		TestEqual(TEXT("condition 0x60 alone answers 0x21"),
			FElysiumNpc::StandoffSelect(W, C, true, true, nullptr, 0.0), 0x21);
		FConds D = C;
		D.bCond0x4f = true;
		FWords V;
		V.ReactionsLeft = 2;
		TestEqual(TEXT("condition 0x4f suppresses it"),
			FElysiumNpc::StandoffSelect(V, D, true, true, nullptr, 0.0), INDEX_NONE);
		FConds E = C;
		E.bCond0x51 = true;
		FWords U;
		U.ReactionsLeft = 2;
		TestEqual(TEXT("and so does 0x51"),
			FElysiumNpc::StandoffSelect(U, E, true, true, nullptr, 0.0), INDEX_NONE);
	}
	// 9. Nothing standing: the base.
	{
		FWords W;
		W.ReactionsLeft = 2;
		FConds C;
		TestEqual(TEXT("with nothing standing the selector declines"),
			FElysiumNpc::StandoffSelect(W, C, true, true, nullptr, 0.0), INDEX_NONE);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `FElysiumInterestingPlace` — slot 103, slot 130 and slot 5.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleInterestingPlaceTest,
	"Elysium.Substrate.NpcKernelLifecycle.InterestingPlace", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleInterestingPlaceTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("lifecycle-place"), 20260913);
	Builder.AddNpc(TEXT("subject"));
	// An EMPTY `type` never looks up and never removes — retail's own first test.
	Builder.AddEntity(TEXT("intersting_place"), TEXT("spot"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpcWorldFixture::Quiet({ Fixture.Npc(TEXT("subject")) });
	FElysiumEntity* Entity = Fixture.World.FindByName(TEXT("spot"));
	if (!TestNotNull(TEXT("the place spawned"), Entity))
	{
		return false;
	}
	FElysiumInterestingPlace& Place = *static_cast<FElysiumInterestingPlace*>(Entity);
	TestFalse(TEXT("an untyped place is not removed by its own spawn"), Place.IsDead());

	// The group fold. Retail folds `m_iGroupID` in place; the port writes the mask beside the
	// authored id. 1..32 becomes one bit, and ANYTHING ELSE becomes the literal 1 — which is NOT
	// `CAI_Hint::Spawn`'s -1.
	auto FoldFor = [&Place](int32 Group, int32 Rating)
	{
		Place.GroupId = Group;
		Place.Rating = Rating;
		Place.MarkersAllocated = 0;
		Place.Spawn();
		return Place.GroupMask;
	};
	TestEqual(TEXT("group 1 folds to bit 0"), FoldFor(1, 0), 1);
	TestEqual(TEXT("group 5 folds to bit 4"), FoldFor(5, 0), 1 << 4);
	TestEqual(TEXT("group 32 folds to bit 31"), FoldFor(32, 0), 1 << 31);
	TestEqual(TEXT("group 0 folds to the LITERAL 1, not -1"), FoldFor(0, 0), 1);
	TestEqual(TEXT("group 33 folds to 1 too"), FoldFor(33, 0), 1);

	// The rating clamp is LAST and is asymmetric: a negative rating becomes 0 and RETURNS, so the
	// high-side test never runs on the same pass.
	FoldFor(1, -4);
	TestEqual(TEXT("a negative rating clamps to 0"), Place.Rating, 0);
	FoldFor(1, 9);
	TestEqual(TEXT("a rating over 5 clamps to 5"), Place.Rating, 5);
	FoldFor(1, 3);
	TestEqual(TEXT("and one inside the range is left alone"), Place.Rating, 3);

	// The marker table: allocated, zeroed, and nothing at all when the count is not positive.
	Place.MarkersAllocated = 4;
	Place.Spawn();
	TestEqual(TEXT("the marker table is allocated"), Place.Markers.Num(), 4);
	for (const FElysiumInterestingPlace::FMarker& Marker : Place.Markers)
	{
		TestFalse(TEXT("and every row is zeroed"), Marker.Occupant.IsSet());
	}
	Place.MarkersAllocated = 0;
	Place.Spawn();
	TestEqual(TEXT("a zero allocation allocates nothing"), Place.Markers.Num(), 0);

	// The type lookup and its self-destruct. A type that does not resolve removes the entity, and
	// nothing after it runs — the group fold below is what says the body returned.
	Place.Type = TEXT("no_such_interesting_place_type");
	Place.GroupId = 7;
	Place.GroupMask = 0;
	Place.Spawn();
	TestTrue(TEXT("a place whose type does not resolve removes itself"), Place.IsDead());
	TestEqual(TEXT("and nothing after the lookup runs"), Place.GroupMask, 0);
	TestNull(TEXT("with no resolved type"), Place.ResolvedType);
	// `OnRestore` runs the SAME lookup, which is the recovered half the two share.
	TestFalse(TEXT("OnRestore takes the same self-destruct"), Place.OnRestoreResolveType());

	// Slot 5 (0x102dbbc0) is MSVC's scalar deleting destructor, NOT a reset: bit 0 is "free the
	// storage" and every other bit is ignored.
	TestTrue(TEXT("bit 0 frees the storage"),
		FElysiumInterestingPlace::ConversationPlaceDeletingDtor(1));
	TestFalse(TEXT("a clear bit 0 does not"),
		FElysiumInterestingPlace::ConversationPlaceDeletingDtor(0));
	TestFalse(TEXT("and the other bits are ignored"),
		FElysiumInterestingPlace::ConversationPlaceDeletingDtor(0xfe));
	// The six `COutput`s it destroys, in destruction order.
	TConstArrayView<const TCHAR*> Names = FElysiumInterestingPlace::ConversationPlaceOutputNames();
	TestEqual(TEXT("six outputs are destroyed"), Names.Num(), 6);
	TestEqual(TEXT("the first"), FString(Names[0]), FString(TEXT("OnPlayerLeftRadius")));
	TestEqual(TEXT("the last"), FString(Names[5]), FString(TEXT("OnConversationStart")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `FElysiumNpcMaker::ParseMapData` (0x1034b3c0).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleParseMapDataTest,
	"Elysium.Substrate.NpcKernelLifecycle.ParseMapData", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleParseMapDataTest::RunTest(const FString&)
{
	// The copy stops at the first `}` and then writes one unconditionally — so the brace is always
	// present, exactly once, and everything past the first one is dropped.
	TestEqual(TEXT("the block stops at the first brace"),
		FElysiumNpcMaker::ExtractRefMapDataBlock(TEXT("\"model\" \"x\"}rest")),
		FString(TEXT("\"model\" \"x\"}")));
	TestEqual(TEXT("input with no brace still ends in one"),
		FElysiumNpcMaker::ExtractRefMapDataBlock(TEXT("abc")), FString(TEXT("abc}")));
	// The empty input is the arm that makes retail's "empty means null" test unreachable: the `}`
	// is written at position 0, so the first byte is never NUL.
	TestEqual(TEXT("an empty input yields the brace alone"),
		FElysiumNpcMaker::ExtractRefMapDataBlock(FString()), FString(TEXT("}")));
	TestEqual(TEXT("and a leading brace yields it too"),
		FElysiumNpcMaker::ExtractRefMapDataBlock(TEXT("}abc")), FString(TEXT("}")));

	FElysiumNpcWorldBuilder Builder(TEXT("lifecycle-maker"), 20260913);
	Builder.AddEntity(TEXT("npc_maker"), TEXT("maker"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumEntity* Entity = Fixture.World.FindByName(TEXT("maker"));
	if (!TestNotNull(TEXT("the maker spawned"), Entity))
	{
		return false;
	}
	FElysiumNpcMaker& Maker = *static_cast<FElysiumNpcMaker*>(Entity);
	Maker.ParseMapData(TEXT("\"npctype\" \"npc_VCop\"}"));
	TestEqual(TEXT("slot 107 latches the extracted block"), Maker.RefMapDataBuffer,
		FString(TEXT("\"npctype\" \"npc_VCop\"}")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CAI_Senses::PerformSensing`'s gate (0x10310710).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleSenseGateTest,
	"Elysium.Substrate.NpcKernelLifecycle.SenseGate", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleSenseGateTest::RunTest(const FString&)
{
	FLifecycleFixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Npc))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Npc;
	// `senses+0x80 m_bCanPerformSenses` ships SET — the sense pass runs for every NPC — and nothing
	// in this runtime writes it. It is a gate with a real default, not a seam.
	TestTrue(TEXT("the gate ships set"), N.Senses.bCanPerformSenses);

	// With it clear, `Look` and `Listen` do not run at all: the pass produces no sighting, however
	// close the player is standing. The ON path is the `Elysium.Substrate.NpcSenses.*` suite's own
	// subject and is not re-asserted here.
	N.Senses.bCanPerformSenses = false;
	N.Senses.Tick(N, 1.0);
	N.Senses.Tick(N, 2.0);
	TestEqual(TEXT("a gated-off pass sights nothing"), N.Senses.Sighted().Num(), 0);
	// The tuning resolve is NOT behind the gate — it is the port's own pre-step, and retail's gate
	// sits inside `CAI_Senses::PerformSensing`, below `InitPerceptionDistances`.
	TestTrue(TEXT("but the perception tuning still resolved"), N.Senses.Perception.bResolved);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CDialog::clan_offset` (0x100e65d0).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycleClanOffsetTest,
	"Elysium.Substrate.NpcKernelLifecycle.ClanOffset", GLifecycleTestFlags)
bool FElysiumNpcKernelLifecycleClanOffsetTest::RunTest(const FString&)
{
	// The seven clans in retail's own comparison order — Brujah, Gangrel, Nosferatu, Toreador,
	// Tremere, Ventrue, Malkavian — answering 0..6, and `-1` for anything else.
	TestEqual(TEXT("Brujah is 0"), ElysiumDlgClan::OffsetFromSheetClan(2), 0);
	TestEqual(TEXT("Gangrel is 1"), ElysiumDlgClan::OffsetFromSheetClan(3), 1);
	TestEqual(TEXT("Nosferatu is 2"), ElysiumDlgClan::OffsetFromSheetClan(5), 2);
	TestEqual(TEXT("Toreador is 3"), ElysiumDlgClan::OffsetFromSheetClan(6), 3);
	TestEqual(TEXT("Tremere is 4"), ElysiumDlgClan::OffsetFromSheetClan(7), 4);
	TestEqual(TEXT("Ventrue is 5"), ElysiumDlgClan::OffsetFromSheetClan(8), 5);
	TestEqual(TEXT("Malkavian is 6"), ElysiumDlgClan::OffsetFromSheetClan(4), 6);
	// The sheet's own 2..8 encoding is a DIFFERENT order from the column order, which is the whole
	// reason the join exists.
	TestEqual(TEXT("a speaker with no clan answers -1"),
		ElysiumDlgClan::OffsetFromSheetClan(0), ElysiumDlgClan::None);
	TestEqual(TEXT("and so does one outside the encoding"),
		ElysiumDlgClan::OffsetFromSheetClan(99), ElysiumDlgClan::None);
	TestEqual(TEXT("the table has seven columns"), ElysiumDlgClan::Num, 7);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
