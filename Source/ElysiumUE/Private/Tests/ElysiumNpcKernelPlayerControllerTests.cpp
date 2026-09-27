#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcPlayerController.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcWolfMorph.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 fold A2 — the controller line as C++ classes: `CNPC_VPlayerController`
// (`FElysiumNpcPlayerController`), `CNPC_VFrenzyShadow` and `CNPC_VWolfMorph` below it. Every
// assertion is read off the listing of the body it names (the evidence brief:
// `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/briefs/A2-controller-line.md`).

static constexpr EAutomationTestFlags GPlayerControllerTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One world holding one body of `RetailClass`, built by its factory classname, and the player.
	// The controller's `Spawn` renames the body `playercontroller`, so it is found by its class.
	struct FControllerLineFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FControllerLineFixture(const TCHAR* RetailClass, uint32 Seed = 5200)
			: World([RetailClass, Seed]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_player_controller"), Seed);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, RetailClass);
					Builder.AddNpc(TEXT("other"), FVector(300.f, 0.f, 0.f));
					return Builder;
				}())
		{
			Npc = World.NpcOfClass(RetailClass);
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Npc, World.Npc(TEXT("other")) });
		}

		// Owned by the player, as `GetControllerNPC` (`0x10161be1`) makes it, and `NPCInit` re-run so
		// `m_hFriendPlayer` reads the owner.
		void OwnByPlayer()
		{
			if (Npc != nullptr && Player != nullptr)
			{
				Npc->SetOwnerEntity(Player->Handle);
				Npc->NPCInit();
			}
		}
	};

	// How many times the stub tally has seen `Surface` fire.
	int32 StubFires(const TCHAR* Surface)
	{
		TArray<ElysiumStub::FTally> Tally;
		ElysiumStub::CollectTally(Tally);
		for (const ElysiumStub::FTally& Row : Tally)
		{
			if (Row.Surface == Surface)
			{
				return Row.Count;
			}
		}
		return 0;
	}

	// A controller whose slot 614 records how many times the Troika think stub had fired at the
	// moment it ran, so `0x103a4700`'s ORDER (the direct think, THEN the virtual reset) is observable.
	class FControllerThinkOrderProbe final : public FElysiumNpcPlayerController
	{
	public:
		virtual void ResetThinkTimers(double Now) override
		{
			++Resets;
			TroikaThinksAtReset = StubFires(TEXT("CAI_BaseNPCTroika::NPCThink"));
			FElysiumNpcPlayerController::ResetThinkTimers(Now);
		}
		int32 Resets = 0;
		int32 TroikaThinksAtReset = -1;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerClassesTest,
	"Elysium.Substrate.NpcKernelPlayerController.Classes", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerClassesTest::RunTest(const FString&)
{
	// The three factories build the three classes, and the tree is retail's.
	struct FRow
	{
		const TCHAR* RetailClass;
		int32 Classify;
	};
	const FRow Rows[] = {
		{ TEXT("CNPC_VPlayerController"), 2 },   // 0x103a4890
		{ TEXT("CNPC_VFrenzyShadow"), 3 },       // 0x10375d70
		{ TEXT("CNPC_VWolfMorph"), 2 },          // 0x103dce50
	};
	for (const FRow& Row : Rows)
	{
		FControllerLineFixture F(Row.RetailClass);
		if (!TestNotNull(FString::Printf(TEXT("%s stands"), Row.RetailClass), F.Npc))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s answers its own census row"), Row.RetailClass),
			F.Npc->RetailClass() == ElysiumNpcKernelClass::Find(Row.RetailClass));
		TestNotNull(FString::Printf(TEXT("%s is a player controller"), Row.RetailClass),
			F.Npc->AsSpecies<FElysiumNpcPlayerController>());
		TestNotNull(FString::Printf(TEXT("%s is a CNPC_VVampire"), Row.RetailClass),
			F.Npc->AsSpecies<FElysiumNpcVampire>());
		TestEqual(FString::Printf(TEXT("%s Classify"), Row.RetailClass), F.Npc->Classify(), Row.Classify);
		TestFalse(FString::Printf(TEXT("%s: slot 72 0x103751a0 refuses discipline targeting"),
			Row.RetailClass), F.Npc->Slot72(0));
	}
	FControllerLineFixture Shadow(TEXT("CNPC_VFrenzyShadow"));
	FControllerLineFixture Wolf(TEXT("CNPC_VWolfMorph"));
	TestNull(TEXT("a frenzy shadow is not a wolf morph"),
		Shadow.Npc != nullptr ? Shadow.Npc->AsSpecies<FElysiumNpcWolfMorph>() : nullptr);
	TestNull(TEXT("and the reverse"),
		Wolf.Npc != nullptr ? Wolf.Npc->AsSpecies<FElysiumNpcFrenzyShadow>() : nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerSpawnInitTest,
	"Elysium.Substrate.NpcKernelPlayerController.SpawnAndNPCInit", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerSpawnInitTest::RunTest(const FString&)
{
	FControllerLineFixture F(TEXT("CNPC_VPlayerController"));
	FElysiumNpcPlayerController* Controller =
		F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcPlayerController>() : nullptr;
	if (!TestNotNull(TEXT("the controller"), Controller) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	// `Spawn` `0x103a4510`, what it left.
	TestEqual(TEXT("SetName(\"playercontroller\")"), Controller->TargetName, FString(TEXT("playercontroller")));
	TestEqual(TEXT("AddFlag2(0x10), on the seam word"), Controller->Flags2Added, 0x10u);
	EElysiumRelationship PlayerClassValue = EElysiumRelationship::Neutral;
	int32 PlayerClassPriority = -1;
	if (TestTrue(TEXT("AddClassRelationship(1, 3, 0) wrote the player's class row"),
		Controller->Relationships.ResolvePersistentRow(FElysiumEntityHandle::Invalid(), TEXT("player"),
			PlayerClassValue, PlayerClassPriority)))
	{
		TestEqual(TEXT("liked (D_LI 3)"), PlayerClassValue, EElysiumRelationship::Like);
		TestEqual(TEXT("at priority 0"), PlayerClassPriority, 0);
	}

	// `NPCInit` `0x103a4580` on an owned body.
	Controller->bForceFrequentThink = false;
	Controller->SetFrenziedWord(0x5ddfu);
	F.OwnByPlayer();
	TestTrue(TEXT("m_hFriendPlayer (+0x60ac) is the owner"), Controller->FriendPlayer == F.Player->Handle);
	TestTrue(TEXT("slot 416 SetForceFrequentThink(1)"), Controller->bForceFrequentThink);
	TestEqual(TEXT("+0x6348 999999"), Controller->PlInvestigate, FElysiumNpc::LawThresholdNever);
	TestEqual(TEXT("+0x6358 999999"), Controller->PlSupernaturalAttack, FElysiumNpc::LawThresholdNever);
	TestEqual(TEXT("+0x6338 = 6"), Controller->InvestigateMode, 6);
	TestEqual(TEXT("+0x633c = 6"), Controller->InvestigateModeCombat, 6);
	TestFalse(TEXT("+0x5b84 = 0"), Controller->HasFrenzied(0x5ddfu));
	TestFalse(TEXT("m_pSenses+0x80 = 0"), Controller->Senses.bCanPerformSenses);
	TestFalse(TEXT("+0x1480 = 0"), Controller->bIsBccTargetable);

	// With no owner the friend player is -1.
	Controller->SetOwnerEntity(FElysiumEntityHandle::Invalid());
	Controller->NPCInit();
	TestFalse(TEXT("no owner: m_hFriendPlayer = -1"), Controller->FriendPlayer.IsSet());

	// `CNPC_VFrenzyShadow::Spawn` `0x10375c50` adds the spawn-equip capability first.
	FControllerLineFixture S(TEXT("CNPC_VFrenzyShadow"));
	if (TestNotNull(TEXT("the shadow"), S.Npc))
	{
		TestTrue(TEXT("CapabilitiesAdd(0x200000)"), (S.Npc->CapabilityWord & 0x200000) != 0);
		TestEqual(TEXT("and the controller's Spawn ran under it"), S.Npc->TargetName,
			FString(TEXT("playercontroller")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerThinkOrderTest,
	"Elysium.Substrate.NpcKernelPlayerController.NPCThinkThenResetThinkTimers", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerThinkOrderTest::RunTest(const FString&)
{
	// `0x103a4700`: `CALL` the Troika `NPCThink` `0x10292de0`, then tail-`JMP [vt+0x998]` — slot 614.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_controller_think"), 5210);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	FElysiumEntityDef& Def = Builder.AddNpc(TEXT("probe"), FVector::ZeroVector, TEXT("npc_VPlayerController"));
	Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FControllerThinkOrderProbe>(); };
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FControllerThinkOrderProbe* Probe = static_cast<FControllerThinkOrderProbe*>(
		F.NpcOfClass(TEXT("CNPC_VPlayerController")));
	if (!TestNotNull(TEXT("the probe"), Probe))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Probe });
	const int32 Before = StubFires(TEXT("CAI_BaseNPCTroika::NPCThink"));
	Probe->Resets = 0;
	Probe->NPCThink();
	TestEqual(TEXT("slot 614 ran once"), Probe->Resets, 1);
	TestEqual(TEXT("AFTER the direct Troika think had run"), Probe->TroikaThinksAtReset, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerPreSelectTest,
	"Elysium.Substrate.NpcKernelPlayerController.PreSelectSchedule", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerPreSelectTest::RunTest(const FString&)
{
	// `0x103a46b0`: IDLE answers 0x6b; any other state tail-jumps to the Troika body `0x102ae920`.
	for (const TCHAR* Cls : { TEXT("CNPC_VPlayerController"), TEXT("CNPC_VFrenzyShadow"),
			TEXT("CNPC_VWolfMorph") })
	{
		FControllerLineFixture F(Cls);
		if (!TestNotNull(FString::Printf(TEXT("%s"), Cls), F.Npc))
		{
			continue;
		}
		F.Npc->SetState(1);
		const int32 TroikaBefore = StubFires(TEXT("CAI_BaseNPCTroika::PreSelectSchedule"));
		TestEqual(FString::Printf(TEXT("%s idle -> 0x6b (107)"), Cls), F.Npc->PreSelectSchedule(), 0x6b);
		TestEqual(FString::Printf(TEXT("%s idle does not reach the Troika body"), Cls),
			StubFires(TEXT("CAI_BaseNPCTroika::PreSelectSchedule")), TroikaBefore);
		F.Npc->SetState(2);
		F.Npc->PreSelectSchedule();
		TestEqual(FString::Printf(TEXT("%s non-idle -> the Troika body, directly"), Cls),
			StubFires(TEXT("CAI_BaseNPCTroika::PreSelectSchedule")), TroikaBefore + 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerTookLifeTest,
	"Elysium.Substrate.NpcKernelPlayerController.EventTookLife", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerTookLifeTest::RunTest(const FString&)
{
	// `0x103a4950` — RETAIL CORRECTION (fold A2): the stand-in's kill raises the OWNER player's
	// criminal level to 4 (`0x1017e150`, `ElysiumLaw::SetCriminalLevel`), not an AI event.
	FControllerLineFixture F(TEXT("CNPC_VPlayerController"));
	FElysiumNpcPlayerController* Controller =
		F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcPlayerController>() : nullptr;
	FElysiumEntity* Victim = F.World.World.FindByName(TEXT("other"));
	if (!TestNotNull(TEXT("the controller"), Controller) || !TestNotNull(TEXT("the player"), F.Player)
		|| !TestNotNull(TEXT("the victim"), Victim))
	{
		return false;
	}
	F.Player->Law.Criminal = 0;
	Controller->Event_TookLife(Victim, false, false);
	TestEqual(TEXT("without an owner the body writes nothing"), F.Player->Law.Criminal, 0);

	F.OwnByPlayer();
	Controller->Event_TookLife(Victim, false, false);
	TestEqual(TEXT("owned by the player: criminal level 4"), F.Player->Law.Criminal, 4);
	TestEqual(TEXT("tagged with the retail reason"), Controller->LastTookLifeReason,
		FString(TEXT("CNPC_VPlayerController::Event_TookLife other")));
	Controller->Event_TookLife(nullptr, false, false);
	TestEqual(TEXT("a null victim is \"UNKNOWN\""), Controller->LastTookLifeReason,
		FString(TEXT("CNPC_VPlayerController::Event_TookLife UNKNOWN")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerForwardingTest,
	"Elysium.Substrate.NpcKernelPlayerController.ForwardingToOwner", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerForwardingTest::RunTest(const FString&)
{
	// `+0x184` is slot 97 `GetOwnerEntity`: every forward reaches the player.
	FControllerLineFixture F(TEXT("CNPC_VFrenzyShadow"));
	FElysiumNpcFrenzyShadow* Shadow = F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcFrenzyShadow>() : nullptr;
	if (!TestNotNull(TEXT("the shadow"), Shadow) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}

	// Slot 245 `0x103a49c0`: the base half records, then the owner's slot 245 is asked.
	Shadow->ExtraAnimationModels.Reset();
	Shadow->AddExtraAnimationModels(nullptr, TEXT("attach_a"), TEXT("attach_b"), 1, 2);
	TestEqual(TEXT("unowned: the base half still records"), Shadow->ExtraAnimationModels.Num(), 1);
	TestFalse(TEXT("and reaches no master"), Shadow->ExtraAnimationModels[0].bForwardedToMaster);
	F.OwnByPlayer();
	const int32 AddBefore = StubFires(TEXT("CBaseAnimating::AddExtraAnimationModels"));
	Shadow->AddExtraAnimationModels(nullptr, TEXT("attach_a"), TEXT("attach_b"), 1, 2);
	TestTrue(TEXT("owned: forwarded to the master"), Shadow->ExtraAnimationModels.Last().bForwardedToMaster);
	TestEqual(TEXT("the player's slot 245 was asked"),
		StubFires(TEXT("CBaseAnimating::AddExtraAnimationModels")), AddBefore + 1);
	const int32 RemoveBefore = StubFires(TEXT("CBaseAnimating::RemoveExtraAnimationModels"));
	Shadow->RemoveExtraAnimationModels();
	TestEqual(TEXT("slot 246 0x103a4a60 clears the base list first"), Shadow->ExtraAnimationModels.Num(), 0);
	TestEqual(TEXT("then tail-jumps into the player's slot 246"),
		StubFires(TEXT("CBaseAnimating::RemoveExtraAnimationModels")), RemoveBefore + 1);

	// Slots 142 / 390 `0x10376ae0` / `0x10376b10`: the owner takes the damage; the shadow answers 0.
	const int32 TakeBefore = StubFires(TEXT("CBaseCombatCharacter::OnTakeDamage"));
	TestEqual(TEXT("OnTakeDamage always answers 0"), Shadow->OnTakeDamage(nullptr), 0);
	TestEqual(TEXT("after forwarding to the owner's slot 142"),
		StubFires(TEXT("CBaseCombatCharacter::OnTakeDamage")), TakeBefore + 1);
	const int32 AliveBefore = StubFires(TEXT("CBaseCombatCharacter::OnTakeDamage_Alive"));
	TestEqual(TEXT("OnTakeDamage_Alive always answers 0"), Shadow->OnTakeDamage_Alive(nullptr), 0);
	TestEqual(TEXT("after forwarding to the owner's combat character slot 390"),
		StubFires(TEXT("CBaseCombatCharacter::OnTakeDamage_Alive")), AliveBefore + 1);
	// Slot 144 `0x10376b50` is three bytes.
	const int32 KilledBefore = StubFires(TEXT("CAI_BaseNPCTroika::Event_Killed"));
	Shadow->Event_Killed(nullptr);
	TestEqual(TEXT("Event_Killed runs no base body"), StubFires(TEXT("CAI_BaseNPCTroika::Event_Killed")),
		KilledBefore);

	// Task 330's prologue and arm `0x10375f50`: owner present, `Replenish(1)` refused (the seam), so
	// `m_bFailedGrapple = 1`.
	FElysiumScheduleStep Step;
	Step.TaskId = 0x14a;
	Shadow->bFailedGrapple = false;
	Shadow->StartTaskSlot442(&Step);
	TestTrue(TEXT("task 330: a refused feed sets m_bFailedGrapple"), Shadow->bFailedGrapple);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerConesTest,
	"Elysium.Substrate.NpcKernelPlayerController.Cones", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerConesTest::RunTest(const FString&)
{
	// 362-365: the controller and the wolf answer false; the shadow puts the base bodies back.
	FControllerLineFixture C(TEXT("CNPC_VPlayerController"));
	FControllerLineFixture S(TEXT("CNPC_VFrenzyShadow"));
	FElysiumEntity* COther = C.World.World.FindByName(TEXT("other"));
	FElysiumEntity* SOther = S.World.World.FindByName(TEXT("other"));
	if (!TestNotNull(TEXT("controller"), C.Npc) || !TestNotNull(TEXT("shadow"), S.Npc)
		|| !TestNotNull(TEXT("targets"), COther) || !TestNotNull(TEXT("targets"), SOther))
	{
		return false;
	}
	C.Npc->Angles = FVector::ZeroVector;
	S.Npc->Angles = FVector::ZeroVector;
	TestFalse(TEXT("0x10375140 FInViewCone(point) false"), C.Npc->FInViewCone(COther->Origin));
	TestFalse(TEXT("0x10375120 FInViewCone(entity) false"), C.Npc->FInViewCone(COther));
	TestFalse(TEXT("0x10375180 FInAimCone(point) false"), C.Npc->FInAimCone(COther->Origin));
	TestFalse(TEXT("0x10375160 FInAimCone(entity) false"), C.Npc->FInAimCone(COther));
	FElysiumNpcFrenzyShadow* Shadow = S.Npc->AsSpecies<FElysiumNpcFrenzyShadow>();
	if (TestNotNull(TEXT("the shadow class"), Shadow))
	{
		TestEqual(TEXT("0x10326a20 restored"), Shadow->FInViewCone(SOther->Origin),
			Shadow->FElysiumCombatCharacter::FInViewCone(SOther->Origin));
		TestEqual(TEXT("0x10326bd0 restored"), Shadow->FInAimCone(SOther->Origin),
			Shadow->FElysiumCombatCharacter::FInAimCone(SOther->Origin));
		TestEqual(TEXT("0x10326ae0 restored"), Shadow->FInAimCone(SOther),
			Shadow->FElysiumCombatCharacter::FInAimCone(SOther));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerShadowMeleeTest,
	"Elysium.Substrate.NpcKernelPlayerController.FrenzyShadowMelee", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerShadowMeleeTest::RunTest(const FString&)
{
	FControllerLineFixture F(TEXT("CNPC_VFrenzyShadow"));
	FElysiumNpcFrenzyShadow* Shadow = F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcFrenzyShadow>() : nullptr;
	FElysiumEntity* Enemy = F.World.World.FindByName(TEXT("other"));
	if (!TestNotNull(TEXT("the shadow"), Shadow) || !TestNotNull(TEXT("the enemy"), Enemy))
	{
		return false;
	}
	FElysiumNpc* AsNpc = Shadow;
	// 599 `0x10376b70`: event, then latch; every gate dropped; no leave timer.
	Shadow->bInMelee = false;
	Shadow->MeleeEventFires = 0;
	Shadow->MeleeMustLeaveTimer = 0.0;
	TestTrue(TEXT("599 always enters melee, through the vtable"), AsNpc->Slot599(0));
	TestTrue(TEXT("m_bInMelee"), Shadow->bInMelee);
	TestEqual(TEXT("one event"), Shadow->MeleeEventFires, 1);
	TestEqual(TEXT("no m_flMeleeMustLeaveTimer"), Shadow->MeleeMustLeaveTimer, 0.0);
	// 600 `0x10376ba0`: latch, then event; a body already in melee still accepts.
	TestTrue(TEXT("600 always accepts, already in melee included"), AsNpc->Slot600(Enemy));
	TestEqual(TEXT("a second event"), Shadow->MeleeEventFires, 2);
	// 601 `0x10376bd0`: the event ALONE.
	AsNpc->Slot601(Enemy);
	TestEqual(TEXT("601 fires the event"), Shadow->MeleeEventFires, 3);
	TestTrue(TEXT("and does NOT clear m_bInMelee"), Shadow->bInMelee);
	// 602 `0x10376bf0`: false.
	TestFalse(TEXT("602 never asks to leave"), AsNpc->Slot602());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerShadowSelectTest,
	"Elysium.Substrate.NpcKernelPlayerController.FrenzyShadowSelect", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerShadowSelectTest::RunTest(const FString&)
{
	FControllerLineFixture F(TEXT("CNPC_VFrenzyShadow"));
	FElysiumNpcFrenzyShadow* Shadow = F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcFrenzyShadow>() : nullptr;
	FElysiumNpc* Enemy = F.World.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the shadow"), Shadow) || !TestNotNull(TEXT("the enemy"), Enemy))
	{
		return false;
	}
	const EElysiumNpcCond TwoHostiles = static_cast<EElysiumNpcCond>(0x79);
	// 433 `0x10375ed0`: local condition 121 on more than one hostile.
	Shadow->HostileEnemyCount = 2;
	Shadow->TwoHostilesEventFires = 0;
	Shadow->GatherConditions();
	TestTrue(TEXT("two hostiles: local condition 121 set"), Shadow->Cognition.Conditions.Has(TwoHostiles));
	TestEqual(TEXT("after the global event"), Shadow->TwoHostilesEventFires, 1);
	Shadow->HostileEnemyCount = 1;
	Shadow->GatherConditions();
	TestFalse(TEXT("one hostile: cleared"), Shadow->Cognition.Conditions.Has(TwoHostiles));

	// 438 `0x10375d90`: 353 FEED only in COMBAT with a live enemy, few hostiles, reachable, no failed
	// grapple; otherwise the VHuman selector (0 here).
	Shadow->SetState(1);
	TestEqual(TEXT("not in combat: the VHuman selector"), Shadow->SpeciesSelectSchedule(), 0);
	Shadow->SetState(2);
	Shadow->BaseMemory.Enemy = Enemy->Handle;
	Shadow->Cognition.bCondTookDamage = true;
	Shadow->bFailedGrapple = false;
	TestEqual(TEXT("combat, a live enemy: 0x161 SCHED_VFRENZYSHADOW_FEED"), Shadow->SpeciesSelectSchedule(),
		0x161);
	TestFalse(TEXT("having cleared m_bCondTookDamage"), Shadow->Cognition.bCondTookDamage);
	Shadow->bFailedGrapple = true;
	TestEqual(TEXT("a failed grapple falls to the VHuman selector"), Shadow->SpeciesSelectSchedule(), 0);
	Shadow->bFailedGrapple = false;
	Shadow->HostileEnemyCount = 2;
	TestEqual(TEXT("so do two hostiles"), Shadow->SpeciesSelectSchedule(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerWolfMorphTest,
	"Elysium.Substrate.NpcKernelPlayerController.WolfMorph", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerWolfMorphTest::RunTest(const FString&)
{
	FControllerLineFixture F(TEXT("CNPC_VWolfMorph"));
	FElysiumNpcWolfMorph* Wolf = F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcWolfMorph>() : nullptr;
	if (!TestNotNull(TEXT("the wolf"), Wolf))
	{
		return false;
	}
	// 375 `0x103dcdc0`.
	TestEqual(TEXT("every activity is ACT_WOLF_MORPH"), Wolf->NPC_EarlyTranslateActivity(1), 0x1145);
	TestEqual(TEXT("whatever it is"), Wolf->NPC_EarlyTranslateActivity(0x13), 0x1145);
	// 420 `0x103dce00`.
	Wolf->NPCInit();
	TestEqual(TEXT("+0x5cc4 = 0xc"), Wolf->IdealStateRetail(), 0xc);
	TestEqual(TEXT("and the controller body under it: investigate mode 6"), Wolf->InvestigateMode, 6);
	// 438 `0x103dceb0`: not yet running the morph -> 0x158.
	Wolf->Schedule.Current = ElysiumScheduleId::None;
	TestEqual(TEXT("no schedule: 0x158 (local 344 SCHED_VWOLFMORPH_MORPH)"), Wolf->SpeciesSelectSchedule(),
		0x158);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerSpacesTest,
	"Elysium.Substrate.NpcKernelPlayerController.ScheduleSpaces", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerSpacesTest::RunTest(const FString&)
{
	// Each child loads its own space; the controller shares CNPC_VVampire's. Local 344 names a
	// different schedule in the two siblings.
	FElysiumScheduleCorpus& Corpus = FElysiumScheduleCorpus::Get();
	if (!TestTrue(TEXT("the corpus loads"), Corpus.EnsureLoaded()))
	{
		return false;
	}
	const FElysiumLocalIdSpace* Vampire = Corpus.SpaceFor(TEXT("CNPC_VVampire"), EElysiumIdCategory::Schedule);
	const FElysiumLocalIdSpace* Controller =
		Corpus.SpaceFor(TEXT("CNPC_VPlayerController"), EElysiumIdCategory::Schedule);
	const FElysiumLocalIdSpace* Shadow = Corpus.SpaceFor(TEXT("CNPC_VFrenzyShadow"), EElysiumIdCategory::Schedule);
	const FElysiumLocalIdSpace* Wolf = Corpus.SpaceFor(TEXT("CNPC_VWolfMorph"), EElysiumIdCategory::Schedule);
	if (!TestNotNull(TEXT("vampire"), Vampire) || !TestNotNull(TEXT("controller"), Controller)
		|| !TestNotNull(TEXT("shadow"), Shadow) || !TestNotNull(TEXT("wolf"), Wolf))
	{
		return false;
	}
	TestTrue(TEXT("the controller runs CNPC_VVampire's space (0x103750e0)"), Controller == Vampire);
	TestEqual(TEXT("CNPC_VWolfMorph's slot 580 is its own 0x103dc750"),
		FString(ElysiumNpcKernelClass::BodyOf(ElysiumNpcKernelClass::Find(TEXT("CNPC_VWolfMorph")), 580)),
		FString(TEXT("0x103dc750")));
	TestEqual(TEXT("CNPC_VPlayerController's is CNPC_VVampire's 0x103750e0"),
		FString(ElysiumNpcKernelClass::BodyOf(ElysiumNpcKernelClass::Find(TEXT("CNPC_VPlayerController")), 580)),
		FString(TEXT("0x103750e0")));
	const int32 ShadowGlobal = Shadow->LocalToGlobal(344);
	const int32 WolfGlobal = Wolf->LocalToGlobal(344);
	TestTrue(TEXT("both siblings resolve local 344"), ShadowGlobal != INDEX_NONE && WolfGlobal != INDEX_NONE);
	TestNotEqual(TEXT("to DIFFERENT schedules"), ShadowGlobal, WolfGlobal);
	TestEqual(TEXT("the wolf's is SCHED_VWOLFMORPH_MORPH"), FString(ElysiumScheduleName(WolfGlobal)),
		FString(TEXT("SCHED_VWOLFMORPH_MORPH")));
	TestEqual(TEXT("the shadow's local 353 is SCHED_VFRENZYSHADOW_FEED"),
		FString(ElysiumScheduleName(Shadow->LocalToGlobal(353))), FString(TEXT("SCHED_VFRENZYSHADOW_FEED")));
	TestEqual(TEXT("the controller's 0x6b resolves up the chain to SCHED_TROIKA_IDLE_DISPOSITION"),
		FString(ElysiumScheduleName(Controller->LocalToGlobal(0x6b))),
		FString(TEXT("SCHED_TROIKA_IDLE_DISPOSITION")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerCreationTest,
	"Elysium.Substrate.NpcKernelPlayerController.CreationPath", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerCreationTest::RunTest(const FString&)
{
	// `events_player.CreateControllerNPC` -> `GetControllerNPC` `0x10161a70`'s sequence.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_controller_create"), 5220);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"), FVector(500.f, 0.f, 0.f));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumPlayer* Player = F.Player();
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("the player"), Player) || !TestNotNull(TEXT("the guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	TestNull(TEXT("0x101618a0: no controller, !playercontroller resolves nothing"),
		Guard->FindNamedEntity(TEXT("!playercontroller")));

	const FElysiumEntityHandle Handle = F.World.CreatePlayerControllerEntity();
	FElysiumEntity* Entity = F.World.Resolve(Handle);
	FElysiumNpcPlayerController* Controller = Entity != nullptr && Entity->AsNpc() != nullptr
		? Entity->AsNpc()->AsSpecies<FElysiumNpcPlayerController>() : nullptr;
	if (!TestNotNull(TEXT("the classname factory built a CNPC_VPlayerController"), Controller))
	{
		return false;
	}
	TestTrue(TEXT("SetOwnerEntity(player) (0x10161be1)"), Controller->GetOwnerEntity() == Player->Handle);
	TestTrue(TEXT("so NPCInit's m_hFriendPlayer is the player"), Controller->FriendPlayer == Player->Handle);
	TestTrue(TEXT("spawnflags |= 4"), (Controller->SpawnFlags & 4) != 0);
	TestEqual(TEXT("m_flSeekDistBase = 4096"), Controller->AuthoredVision, 4096.f);
	TestEqual(TEXT("m_fEffects: 0x10 from the copy, 0x60 on the controller"), Controller->EffectsWord & 0x70u,
		0x70u);
	TestTrue(TEXT("origin copied"), Controller->Origin.Equals(Player->Origin));
	TestEqual(TEXT("model copied"), Controller->Model, Player->Model);
	TestEqual(TEXT("named playercontroller"), Controller->TargetName, FString(TEXT("playercontroller")));
	TestTrue(TEXT("m_hControllerNPC stored"), F.World.PlayerControllerHandle() == Handle);
	TestEqual(TEXT("!playercontroller resolves the stand-in"),
		F.World.FindByName(TEXT("!playercontroller")), static_cast<FElysiumEntity*>(Controller));
	TestEqual(TEXT("and through an NPC's FindNamedEntity (0x101618a0)"),
		Guard->FindNamedEntity(TEXT("!playercontroller")), static_cast<FElysiumEntity*>(Controller));

	TestTrue(TEXT("removal succeeds"), F.World.RemovePlayerControllerEntity());
	TestNull(TEXT("and clears m_hControllerNPC"), F.World.FindPlayerController());
	TestNull(TEXT("so the selector resolves nothing again"), Guard->FindNamedEntity(TEXT("!playercontroller")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerBusyReadersTest,
	"Elysium.Substrate.NpcKernelPlayerController.ControllerBusyReaders", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerBusyReadersTest::RunTest(const FString&)
{
	// `0x10175180` over the PLAYER's `m_hControllerNPC` (the world's controller handle): busy when the
	// stand-in answers `Classify() == 3`. Its live readers are the dialogue refusal `0x10178170` and
	// `CanTalk` gate 10 (`0x102c21c0`).
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_controller_busy"), 5230);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"), FVector(500.f, 0.f, 0.f));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumPlayer* Player = F.Player();
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("the player"), Player) || !TestNotNull(TEXT("the guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	const FString Occupied(TEXT("the player is already occupied"));
	auto Refusal = [Player]() { const TCHAR* R = Player->DialogRefusalReason(); return FString(R != nullptr ? R : TEXT("")); };

	TestFalse(TEXT("no stand-in: not busy"), Player->ControllerNpcBusy());
	TestNotEqual(TEXT("and the dialogue refusal does not name it"), Refusal(), Occupied);

	F.World.CreatePlayerControllerEntity();
	TestFalse(TEXT("a player controller (Classify 2, 0x103a4890) is not busy"), Player->ControllerNpcBusy());
	TestFalse(TEXT("CanTalk gate 10 admits the player"), Guard->ActivatorControllerBusy(Player));
	TestNotEqual(TEXT("and the dialogue refusal does not name it"), Refusal(), Occupied);

	// `GetControllerNPC("npc_VFrenzyShadow")`, as `CheckForPlayerFrenzy` asks: the mismatched stand-in
	// is let go and a frenzy shadow takes the handle.
	AddExpectedError(TEXT("asked for NPC class"), EAutomationExpectedErrorFlags::Contains, 1);
	const FElysiumEntityHandle ShadowHandle = F.World.CreatePlayerControllerEntity(TEXT("npc_VFrenzyShadow"));
	FElysiumEntity* Shadow = F.World.Resolve(ShadowHandle);
	if (!TestNotNull(TEXT("the frenzy shadow stands in"), Shadow))
	{
		return false;
	}
	TestTrue(TEXT("as the player's m_hControllerNPC"), F.World.FindPlayerController() == Shadow);
	TestTrue(TEXT("a frenzy shadow (Classify 3, 0x10375d70) is busy"), Player->ControllerNpcBusy());
	TestTrue(TEXT("CanTalk gate 10 refuses the player"), Guard->ActivatorControllerBusy(Player));
	TestFalse(TEXT("but not an activator that is not the player"), Guard->ActivatorControllerBusy(Guard));
	TestEqual(TEXT("the dialogue refusal names it"), Refusal(), Occupied);
	TestEqual(TEXT("asking again for the same class hands the same one back"),
		F.World.CreatePlayerControllerEntity(TEXT("npc_VFrenzyShadow")).Index, ShadowHandle.Index);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerWolfRunningMorphTest,
	"Elysium.Substrate.NpcKernelPlayerController.WolfMorphRunningMorph", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerWolfRunningMorphTest::RunTest(const FString&)
{
	// 438 `0x103dceb0`: a body already running `SCHED_VWOLFMORPH_MORPH` takes the VHuman selector (0
	// here); any other running schedule still answers `0x158`.
	FElysiumScheduleCorpus& Corpus = FElysiumScheduleCorpus::Get();
	if (!TestTrue(TEXT("the corpus loads"), Corpus.EnsureLoaded()))
	{
		return false;
	}
	FControllerLineFixture F(TEXT("CNPC_VWolfMorph"));
	FElysiumNpcWolfMorph* Wolf = F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcWolfMorph>() : nullptr;
	const FElysiumLocalIdSpace* Space = Corpus.SpaceFor(TEXT("CNPC_VWolfMorph"), EElysiumIdCategory::Schedule);
	if (!TestNotNull(TEXT("the wolf"), Wolf) || !TestNotNull(TEXT("its space"), Space))
	{
		return false;
	}
	Wolf->Schedule.Current = Space->LocalToGlobal(344);
	TestEqual(TEXT("running the morph -> the VHuman selector"), Wolf->SpeciesSelectSchedule(), 0);
	Wolf->Schedule.Current = Space->LocalToGlobal(0x6b);
	TestEqual(TEXT("running anything else -> 0x158"), Wolf->SpeciesSelectSchedule(), 0x158);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerShadowStartTaskTest,
	"Elysium.Substrate.NpcKernelPlayerController.FrenzyShadowStartTask", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerShadowStartTaskTest::RunTest(const FString&)
{
	FControllerLineFixture F(TEXT("CNPC_VFrenzyShadow"));
	FElysiumNpcFrenzyShadow* Shadow = F.Npc != nullptr ? F.Npc->AsSpecies<FElysiumNpcFrenzyShadow>() : nullptr;
	if (!TestNotNull(TEXT("the shadow"), Shadow) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	FElysiumScheduleStep Step;

	// The attack tasks (0x34-0x37, 0x3e, 0x3f), not frenzy-hungry: the active weapon's capability word
	// must carry 0x18000 or the task fails with 0x1f. The port's word is a seam answering 0, so the
	// pass arm (stamp `m_flLastAttackTime`, the weapon's `+0x5d0`) is unreachable here.
	TestEqual(TEXT("the capability seam answers 0"), Shadow->ActiveWeaponCapabilityWord(), 0u);
	AddExpectedError(TEXT("TaskFail 0x1f"), EAutomationExpectedErrorFlags::Contains, 0);
	for (const int32 Task : { 0x34, 0x35, 0x36, 0x37, 0x3e, 0x3f })
	{
		Step.TaskId = Task;
		Shadow->BaseScheduleHost.FailureReason = 0;
		Shadow->LastAttackTime = -1.0;
		Shadow->StartTaskSlot442(&Step);
		TestEqual(FString::Printf(TEXT("task 0x%x without melee capability fails with 0x1f"), Task),
			Shadow->BaseScheduleHost.FailureReason, 0x1f);
		TestEqual(FString::Printf(TEXT("task 0x%x stamps no attack time"), Task), Shadow->LastAttackTime, -1.0);
	}

	// Task 330 `TASK_VFRENZYSHADOW_ATTEMPT_FEED`: with no owning player neither word moves.
	Step.TaskId = 0x14a;
	Shadow->SetOwnerEntity(FElysiumEntityHandle::Invalid());
	Shadow->bFailedGrapple = false;
	Shadow->StartTaskSlot442(&Step);
	TestFalse(TEXT("no owner: +0x6668 stays 0"), Shadow->bFailedGrapple);
	Shadow->bFailedGrapple = true;
	Shadow->StartTaskSlot442(&Step);
	TestTrue(TEXT("no owner: +0x6668 stays 1"), Shadow->bFailedGrapple);
	// Owned by the player: `Replenish(1)` refused (the seam) writes `m_bFailedGrapple = 1`; the
	// accepted arm would clear it and count the player-side writes, unreachable while the seam refuses.
	Shadow->SetOwnerEntity(F.Player->Handle);
	Shadow->bFailedGrapple = false;
	Shadow->OwnerFeedAcceptances = 0;
	Shadow->StartTaskSlot442(&Step);
	TestTrue(TEXT("owned, refused: +0x6668 = 1"), Shadow->bFailedGrapple);
	TestEqual(TEXT("and the accepted arm did not run"), Shadow->OwnerFeedAcceptances, 0);
	return true;
}

#endif
