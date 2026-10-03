#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumStub.h"
#include "ElysiumUserCmd.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcPlayerController.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcWolfMorph.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Components/SkeletalMeshComponent.h"

// Story 5 fold A2 — the controller line as C++ classes: `CNPC_VPlayerController`
// (`FElysiumNpcPlayerController`), `CNPC_VFrenzyShadow` and `CNPC_VWolfMorph` below it. Every
// assertion is read off the listing of the body it names (the evidence brief:
// `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/briefs/A2-controller-line.md`).

static constexpr EAutomationTestFlags GPlayerControllerTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

/** A class-LOCAL task id as the GLOBAL id a schedule step carries -- the form every slot-442 body
 *  translates back through slot 450 (0019/8 L04: `0x10375f50` switches on the local id). */
static int32 PlayerControllerShadowTaskId(const FElysiumNpcBase& Npc, int32 LocalTask)
{
	const FElysiumLocalIdSpace* Space = Npc.IdSpace(EElysiumIdCategory::Task);
	return Space != nullptr ? Space->LocalToGlobal(LocalTask) : LocalTask;
}

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

	// A controller whose slot 614 records whether the Troika think had already run when it did, so
	// `0x103a4700`'s ORDER (the direct think, THEN the virtual reset) is observable. Story 8 lane
	// L13b ported `0x10292de0`, which no longer tallies a stub: the witness is its first write,
	// `m_bfAINPCFlags2 &= 0x7ffffffb` (`0x10292e5e`), which runs before its `m_bDisableAI` return.
	class FControllerThinkOrderProbe final : public FElysiumNpcPlayerController
	{
	public:
		virtual void ResetThinkTimers(double Now) override
		{
			++Resets;
			bScheduleChangedAtReset = NpcFlags.Has(EElysiumNpcFlag2::SCHEDULE_CHANGED);
			FElysiumNpcPlayerController::ResetThinkTimers(Now);
		}
		int32 Resets = 0;
		bool bScheduleChangedAtReset = true;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerClassesTest,
	"Elysium.Arm.NpcKernelPlayerController.Classes", GPlayerControllerTestFlags)
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
			F.Npc->RetailClass() == ElysiumNpcTestCensus::Find(Row.RetailClass));
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
	"Elysium.Arm.NpcKernelPlayerController.SpawnAndNPCInit", GPlayerControllerTestFlags)
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
	TestTrue(TEXT("AddFlag2(0x10) on m_fFlags2 (0x103a454d)"), (Controller->EntityFlags2Word & 0x10u) != 0u);
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
	"Elysium.Arm.NpcKernelPlayerController.NPCThinkThenResetThinkTimers", GPlayerControllerTestFlags)
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
	Probe->NpcFlags.Set(EElysiumNpcFlag2::SCHEDULE_CHANGED);
	Probe->Resets = 0;
	Probe->NPCThink();
	TestEqual(TEXT("slot 614 ran once"), Probe->Resets, 1);
	// `0x10292e5e`: the Troika think clears SCHEDULE_CHANGED first; slot 614 saw it clear.
	TestFalse(TEXT("AFTER the direct Troika think had run (0x10292e5e cleared the bit)"),
		Probe->bScheduleChangedAtReset);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerPreSelectTest,
	"Elysium.Arm.NpcKernelPlayerController.PreSelectSchedule", GPlayerControllerTestFlags)
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
		TestEqual(FString::Printf(TEXT("%s idle -> 0x6b (107)"), Cls), F.Npc->PreSelectSchedule(), 0x6b);
		TestEqual(FString::Printf(TEXT("%s idle leaves the state alone"), Cls), F.Npc->NpcStateRetail(), 1);
		// Non-idle tail-jumps into `0x102ae920` (`0x103a46c3 JMP 0x1000df2b`). What marks the Troika
		// body is its COMBAT arm with no enemy: `0x102aed48` slot 167 null -> `0x102aed54`/`0x102aed5c`
		// `SetState(m_bNoAlertState ? IDLE : ALERT)` before the `0x1028a260` re-entry.
		F.Npc->SetState(2);
		F.Npc->PreSelectSchedule();
		TestNotEqual(FString::Printf(TEXT("%s non-idle -> the Troika body leaves COMBAT with no enemy (0x102aed5c)"), Cls),
			F.Npc->NpcStateRetail(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerTookLifeTest,
	"Elysium.Arm.NpcKernelPlayerController.EventTookLife", GPlayerControllerTestFlags)
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
	"Elysium.Arm.NpcKernelPlayerController.ForwardingToOwner", GPlayerControllerTestFlags)
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
	// The owner's slot 142 here is `CBaseCombatCharacter::OnTakeDamage` (`0x1032ef60`, a hand body)
	// and its slot 390 `0x103302e0`, whose slot 299 `CreateDamageEffects` (`+0x4ac`, still a 29e stub)
	// is the forward's witness: it runs on the OWNER on every alive packet, before any commit.
	// NAMED GAP (not a retail route): retail's player slot 142 is `CBasePlayer::OnTakeDamage`
	// `0x10163020` (`vtmb_callers 0x1033a9e0`), whose refusals, feed teardown and `+use` drop wrap the
	// call into `0x1032ef60`; the port's player carries no slot-142 body, so this route pins the
	// forward, not the player's whole transaction.
	FElysiumNpcBase::FElysiumTakeDamageInfo Packet;
	Packet.Damage = 0.f;   // nothing to commit: the witness is the dispatch, not the health
	const int32 TakeBefore = StubFires(TEXT("CBaseCombatCharacter::CreateDamageEffects"));
	TestEqual(TEXT("OnTakeDamage always answers 0"), Shadow->OnTakeDamage(&Packet), 0);
	TestEqual(TEXT("after forwarding to the owner's slot 142 (0x1032ef60 -> slot 390 -> slot 299)"),
		StubFires(TEXT("CBaseCombatCharacter::CreateDamageEffects")), TakeBefore + 1);
	const int32 AliveBefore = StubFires(TEXT("CBaseCombatCharacter::CreateDamageEffects"));
	TestEqual(TEXT("OnTakeDamage_Alive always answers 0"), Shadow->OnTakeDamage_Alive(&Packet), 0);
	TestEqual(TEXT("after forwarding to the owner's combat character slot 390 (0x103302e0 -> slot 299)"),
		StubFires(TEXT("CBaseCombatCharacter::CreateDamageEffects")), AliveBefore + 1);
	// Slot 144 `0x10376b50` is three bytes.
	const int32 KilledBefore = StubFires(TEXT("CAI_BaseNPCTroika::Event_Killed"));
	Shadow->Event_Killed(nullptr);
	TestEqual(TEXT("Event_Killed runs no base body"), StubFires(TEXT("CAI_BaseNPCTroika::Event_Killed")),
		KilledBefore);

	// Task 330's prologue and arm `0x10375f50`: owner present, `Replenish(1)` refused (the seam), so
	// `m_bFailedGrapple = 1`.
	FElysiumScheduleStep Step;
	Step.TaskId = PlayerControllerShadowTaskId(*Shadow, 0x14a);
	Shadow->bFailedGrapple = false;
	Shadow->StartTaskSlot442(&Step);
	TestTrue(TEXT("task 330: a refused feed sets m_bFailedGrapple"), Shadow->bFailedGrapple);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerConesTest,
	"Elysium.Arm.NpcKernelPlayerController.Cones", GPlayerControllerTestFlags)
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
	"Elysium.Arm.NpcKernelPlayerController.FrenzyShadowMelee", GPlayerControllerTestFlags)
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
	"Elysium.Arm.NpcKernelPlayerController.FrenzyShadowSelect", GPlayerControllerTestFlags)
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
	// 433 `0x10375ed0`: local condition 121 on more than one hostile. Since story 8 (L07) the Troika
	// gather `0x102b27f0` runs under it, and its `ChooseEnemy` reaches slot 478 `BestEnemy`
	// (`0x103766d0`), whose rescan zeroes and rebuilds `+0x6664` (`103767a2`); a committed living
	// enemy keeps the choice sticky so the count the case writes is the one the body reads.
	ElysiumNpcEnemy::SetEnemy(*Shadow, Enemy->Handle);
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
	// Every non-feed arm tail-jumps `0x10375e13 JMP 0x10015ad2` into `CNPC_VHuman::SelectSchedule`
	// `0x10384ee0`.
	TestNotEqual(TEXT("not in combat: the VHuman selector"), Shadow->SpeciesSelectSchedule(), 0x161);
	Shadow->SetState(2);
	Shadow->BaseMemory.Enemy = Enemy->Handle;
	Shadow->Cognition.bCondTookDamage = true;
	Shadow->bFailedGrapple = false;
	TestEqual(TEXT("combat, a live enemy: 0x161 SCHED_VFRENZYSHADOW_FEED"), Shadow->SpeciesSelectSchedule(),
		0x161);
	TestFalse(TEXT("having cleared m_bCondTookDamage"), Shadow->Cognition.bCondTookDamage);
	Shadow->bFailedGrapple = true;
	TestNotEqual(TEXT("a failed grapple falls to the VHuman selector"), Shadow->SpeciesSelectSchedule(), 0x161);
	Shadow->bFailedGrapple = false;
	Shadow->HostileEnemyCount = 2;
	TestNotEqual(TEXT("so do two hostiles"), Shadow->SpeciesSelectSchedule(), 0x161);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerWolfMorphTest,
	"Elysium.Arm.NpcKernelPlayerController.WolfMorph", GPlayerControllerTestFlags)
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
	"Elysium.Arm.NpcKernelPlayerController.ScheduleSpaces", GPlayerControllerTestFlags)
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
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VWolfMorph")), 580)),
		FString(TEXT("0x103dc750")));
	TestEqual(TEXT("CNPC_VPlayerController's is CNPC_VVampire's 0x103750e0"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VPlayerController")), 580)),
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
	"Elysium.Arm.NpcKernelPlayerController.CreationPath", GPlayerControllerTestFlags)
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
	TestNull(TEXT("no controller, !playercontroller resolves nothing"),
		Guard->FindNamedEntity(TEXT("!playercontroller")));
	Player->SetRuntimeModel(TEXT("models/character/pc/male/tremere_armor_0.mdl"));
	Player->Disposition = TEXT("Cinematic");
	F.Services.bPlayerBodyEntityHidden = false;

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
	// `CopyAnimationDataFrom`'s `| 0x10` (`100973cc`) is zeroed by `CAI_BaseNPCTroika::NPCInit`
	// (`1029a0c0`) inside `DispatchSpawn`; `GetControllerNPC` then ORs `0x60`.
	TestEqual(TEXT("m_fEffects: exactly 0x60 on the controller"), Controller->EffectsWord, 0x60u);
	TestTrue(TEXT("origin copied"), Controller->Origin.Equals(Player->Origin));
	TestEqual(TEXT("model copied"), Controller->Model, Player->Model);
	TestEqual(TEXT("named playercontroller"), Controller->TargetName, FString(TEXT("playercontroller")));
	TestTrue(TEXT("m_hControllerNPC stored"), F.World.PlayerControllerHandle() == Handle);
	TestEqual(TEXT("!playercontroller resolves the stand-in"),
		F.World.FindByName(TEXT("!playercontroller")), static_cast<FElysiumEntity*>(Controller));
	TestEqual(TEXT("and through an NPC's FindNamedEntity"),
		Guard->FindNamedEntity(TEXT("!playercontroller")), static_cast<FElysiumEntity*>(Controller));

	// Who is drawn: `EF_NODRAW` on the stand-in, which `ShouldTransmit` `0x100ab020` never sends, and
	// nothing on the pawn.
	TestFalse(TEXT("the stand-in is never transmitted (m_fEffects & 0x40)"), Controller->IsTransmitted());
	if (TestNotNull(TEXT("the stand-in stands a body (its animation host)"), Controller->Visual))
	{
		TestFalse(TEXT("which is never drawn"), Controller->Visual->IsVisible());
		TestTrue(TEXT("but still ticks: the pawn draws its pose"), Controller->Visual->IsComponentTickEnabled());
		// The pose layer's clip-commit reveal (a scripted beat arming a clip on the stand-in).
		ElysiumNpcVisual::RevealPosedBody(Controller->Visual);
		TestFalse(TEXT("and a clip commit does not reveal it"), Controller->Visual->IsVisible());
	}
	TestFalse(TEXT("the pawn is not hidden (retail touches no draw state on it)"),
		F.Services.bPlayerBodyEntityHidden);
	TestNotEqual(TEXT("no disposition is copied (GetControllerNPC writes none)"), Controller->Disposition,
		FString(TEXT("Cinematic")));

	TestTrue(TEXT("removal succeeds"), F.World.RemovePlayerControllerEntity());
	TestNull(TEXT("and clears m_hControllerNPC"), F.World.FindPlayerController());
	TestNull(TEXT("so the selector resolves nothing again"), Guard->FindNamedEntity(TEXT("!playercontroller")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerBusyReadersTest,
	"Elysium.Arm.NpcKernelPlayerController.ControllerBusyReaders", GPlayerControllerTestFlags)
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

	// `0x10161a70`'s null-entity arm: a class whose factory builds no NPC takes the mismatch warning,
	// lets the shadow go, then warns "created NULL Entity" and leaves `m_hControllerNPC` at -1.
	AddExpectedError(TEXT("asked for NPC class"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("created NULL Entity"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("0x10161a70: a non-NPC class answers an invalid handle"),
		F.World.CreatePlayerControllerEntity(TEXT("info_target")).IsSet());
	TestFalse(TEXT("and m_hControllerNPC (+0x1db0) is cleared"), F.World.PlayerControllerHandle().IsSet());
	TestFalse(TEXT("so nothing is busy"), Player->ControllerNpcBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerWolfRunningMorphTest,
	"Elysium.Arm.NpcKernelPlayerController.WolfMorphRunningMorph", GPlayerControllerTestFlags)
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
	// `0x103dced5 JMP 0x10015ad2` -> `CNPC_VHuman::SelectSchedule` `0x10384ee0`.
	TestNotEqual(TEXT("running the morph -> the VHuman selector"), Wolf->SpeciesSelectSchedule(), 0x158);
	Wolf->Schedule.Current = Space->LocalToGlobal(0x6b);
	TestEqual(TEXT("running anything else -> 0x158"), Wolf->SpeciesSelectSchedule(), 0x158);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerShadowStartTaskTest,
	"Elysium.Arm.NpcKernelPlayerController.FrenzyShadowStartTask", GPlayerControllerTestFlags)
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
	AddExpectedError(TEXT("TaskFail 0x1f"), EAutomationExpectedErrorFlags::Contains, 0);
	for (const int32 Task : { 0x34, 0x35, 0x36, 0x37, 0x3e, 0x3f })
	{
		Step.TaskId = PlayerControllerShadowTaskId(*Shadow, Task);
		Shadow->BaseScheduleHost.FailureReason = 0;
		Shadow->LastAttackTime = -1.0;
		Shadow->StartTaskSlot442(&Step);
		TestEqual(FString::Printf(TEXT("task 0x%x without melee capability fails with 0x1f"), Task),
			Shadow->BaseScheduleHost.FailureReason, 0x1f);
		TestEqual(FString::Printf(TEXT("task 0x%x stamps no attack time"), Task), Shadow->LastAttackTime, -1.0);
	}

	// Task 330 `TASK_VFRENZYSHADOW_ATTEMPT_FEED`: with no owning player neither word moves.
	Step.TaskId = PlayerControllerShadowTaskId(*Shadow, 0x14a);
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

namespace
{
	// One world with the player and nothing else, and a live `npc_VPlayerController` stand-in.
	struct FControllerSceneFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumPlayer* Player = nullptr;
		FElysiumNpcPlayerController* Controller = nullptr;

		FControllerSceneFixture(const TCHAR* Map, uint32 Seed)
			: World([Map, Seed]
				{
					FElysiumNpcWorldBuilder Builder(Map, Seed);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					return Builder;
				}())
		{
			Player = World.Player();
			if (Player != nullptr)
			{
				// A model, so the stand-in `CopyAnimationDataFrom` builds stands a body to follow.
				Player->SetRuntimeModel(TEXT("models/character/pc/male/tremere_armor_0.mdl"));
			}
			FElysiumEntity* Stand = World.World.Resolve(World.World.CreatePlayerControllerEntity());
			Controller = Stand != nullptr && Stand->AsNpc() != nullptr
				? Stand->AsNpc()->AsSpecies<FElysiumNpcPlayerController>() : nullptr;
		}

		// Stage the stand-in the way a beat leaves it: a mark, a facing, a travel velocity and the
		// animation words `PostThink` reads.
		void Stage(const FVector& Mark, const FVector& Facing)
		{
			Controller->SetRuntimeTransform(Mark, Facing);
			Controller->Velocity = FVector(40.f, -40.f, 0.f);
			Controller->AnimTime = 12.5f;
			Controller->AnimOverlay[2].Sequence = 17;
			Controller->AnimOverlay[2].Cycle = 0.4f;
			Controller->AnimOverlay[2].Weight = 0.7f;
			Controller->AnimOverlay[2].Activity = 33;
			Controller->Flinch[1].Sequence = 9;
			Controller->Flinch[1].ExpireTime = 3.f;
			Controller->Viewtarget = FVector(1.f, 2.f, 3.f);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerPawnFollowsTest,
	"Elysium.Arm.NpcKernelPlayerController.PawnFollowsController", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerPawnFollowsTest::RunTest(const FString&)
{
	// `CBasePlayer::PostThink` `0x1016c510`..`0x1016c672`: one post-move tick puts the stand-in's
	// animation words, transform, velocity and view target on the pawn, and the pawn's body draws the
	// stand-in's pose.
	FControllerSceneFixture F(TEXT("npc_kernel_controller_follow"), 5240);
	if (!TestNotNull(TEXT("the player"), F.Player) || !TestNotNull(TEXT("the stand-in"), F.Controller))
	{
		return false;
	}
	const FVector Mark(320.f, -64.f, 12.f);
	const FVector Facing(0.f, 135.f, 0.f);
	F.Stage(Mark, Facing);
	F.World.Services.Calls.Reset();

	F.World.World.UpdatePlayerFromController();

	TestTrue(TEXT("slot 62: the pawn stands on the stand-in's origin"), F.Player->Origin.Equals(Mark));
	TestTrue(TEXT("slot 64: and takes its angles"), F.Player->Angles.Equals(Facing));
	TestEqual(TEXT("in one body transaction"), F.World.Services.Count(TEXT("TeleportPlayer ")), 1);
	TestTrue(TEXT("SetAbsVelocity(controller + 0x3bc)"), F.Player->Velocity.Equals(FVector(40.f, -40.f, 0.f)));
	TestTrue(TEXT("on the pawn's body too, so a pinned mover integrates no motion of its own"),
		F.World.Services.PlayerBodyVelocity.Equals(FVector(40.f, -40.f, 0.f)));
	TestEqual(TEXT("m_flAnimTime (+0x174)"), F.Player->AnimTime, 12.5f);
	TestEqual(TEXT("m_AnimOverlay[2] m_nSequence"), F.Player->AnimOverlay[2].Sequence, 17);
	TestEqual(TEXT("m_AnimOverlay[2] m_flCycle"), F.Player->AnimOverlay[2].Cycle, 0.4f);
	TestEqual(TEXT("m_AnimOverlay[2] m_flWeight"), F.Player->AnimOverlay[2].Weight, 0.7f);
	TestEqual(TEXT("m_AnimOverlay[2] m_nActivity"), F.Player->AnimOverlay[2].Activity, 33);
	TestEqual(TEXT("m_Flinch[1] nSequence"), F.Player->Flinch[1].Sequence, 9);
	TestEqual(TEXT("m_Flinch[1] flExpireTime"), F.Player->Flinch[1].ExpireTime, 3.f);
	TestTrue(TEXT("slot 277(slot 278): the view target"), F.Player->Viewtarget.Equals(FVector(1.f, 2.f, 3.f)));
	TestTrue(TEXT("the pawn's body draws the stand-in's pose"),
		F.Controller->Visual != nullptr && F.World.Services.PlayerBodyPoseSource == F.Controller->Visual);
	TestFalse(TEXT("and the pawn is not hidden"), F.World.Services.bPlayerBodyEntityHidden);

	// A write to `!player` while the stand-in lives moves the pawn only (`0x1018dc00` writes its
	// target's transform and that player's eye/grapple/use state, never `+0x1db0`); the next frame's
	// copy puts the pawn back on the stand-in.
	F.Player->SetRuntimeTransform(FVector(-500.f, 0.f, 0.f), FVector::ZeroVector);
	TestTrue(TEXT("a pawn teleport does not move the stand-in"), F.Controller->Origin.Equals(Mark));
	F.World.World.UpdatePlayerFromController();
	TestTrue(TEXT("and the next copy re-pins the pawn to it"), F.Player->Origin.Equals(Mark));

	TestTrue(TEXT("removal succeeds"), F.World.World.RemovePlayerControllerEntity());
	F.World.World.UpdatePlayerFromController();
	TestNull(TEXT("no stand-in: the pawn draws its own graph"), F.World.Services.PlayerBodyPoseSource);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerPawnEventsTest,
	"Elysium.Arm.NpcKernelPlayerController.StandInClipEventsReachPlayer", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerPawnEventsTest::RunTest(const FString&)
{
	// Retail's pawn runs `StudioFrameAdvance` (slot 250, `0x1016c2bf`) and `DispatchAnimEvents` (slot
	// 258, `0x1016c2e5`) on the sequence `PostThink` copied off the stand-in, so the stand-in's clip
	// events reach `CBasePlayer::HandleAnimEvent` `0x10178a10`. The port's pawn follows the stand-in's
	// pose, and its clip phase is read through the leader (`ElysiumNpcVisual::PoseHostOf`).
	FControllerSceneFixture F(TEXT("npc_kernel_controller_events"), 5280);
	if (!TestNotNull(TEXT("the player"), F.Player) || !TestNotNull(TEXT("the stand-in"), F.Controller)
		|| !TestNotNull(TEXT("the stand-in's body"), F.Controller->Visual)
		|| !TestNotNull(TEXT("the pawn's body"), F.Player->Visual))
	{
		return false;
	}
	FElysiumRecordingServices& Services = F.World.Services;
	TestTrue(TEXT("the double built the pawn's body"), Services.LastPlayerVisual == F.Player->Visual);

	// The stand-in stands on one clip carrying a 4100 ornament attach (the combat-character arm the
	// player's handler falls through to).
	const TCHAR* const Owner = TEXT("stand_bank");
	const TCHAR* const Label = TEXT("stand_clip");
	FElysiumAnimEvent Attach;
	Attach.Cycle = 0.25f;
	Attach.Event = ElysiumAnimEvents::AttachFollowModel;
	Attach.Options = TEXT("cigarette");
	Services.NpcEventTimelines.Add(FElysiumRecordingServices::EventTimelineKey(Owner, Label)).Add(Attach);
	const FString Path = ElysiumAnimEvents::FormatFollowModelPath(
		ElysiumAnimEvents::AttachFollowModel, TEXT("cigarette"), F.Player->Sheet.IsMale());
	Services.OrnamentModels.Add(Path);
	Services.bBodyClipPhaseSet = true;
	Services.BodyClipPhaseBody = F.Controller->Visual;
	Services.BodyClipPhase.OwnerStem = Owner;
	Services.BodyClipPhase.Label = Label;
	Services.BodyClipPhase.Cycle = 0.5f;
	Services.BodyClipPhase.Length = 1.0f;
	Services.BodyClipPhase.bLooping = true;
	Services.BodyClipPhase.PlayId = 1;

	F.Player->AdvanceAnimEvents();
	TestTrue(TEXT("a pawn on its own graph does not walk the stand-in's clip"),
		Services.WornOrnament(F.Player->Visual).IsEmpty());

	F.World.World.UpdatePlayerFromController();
	TestTrue(TEXT("the pawn's body follows the stand-in's pose"),
		F.Player->Visual->LeaderPoseComponent.Get() == F.Controller->Visual);
	F.Player->AdvanceAnimEvents();
	TestEqual(TEXT("the stand-in's clip event reaches the player's own handler"),
		Services.WornOrnament(F.Player->Visual), Path);
	TestEqual(TEXT("and lands on the player's slot"), F.Player->AnimFollowModel, Path);

	F.World.World.RemovePlayerControllerEntity();
	TestNull(TEXT("released, the pawn takes its own graph back"), F.Player->Visual->LeaderPoseComponent.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerEffectsSaveTest,
	"Elysium.Arm.NpcKernelPlayerController.EffectsSurviveSnapshot", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerEffectsSaveTest::RunTest(const FString&)
{
	// `m_fEffects` is a retail SAVE row (datamap offset 412, flags 6): a restored stand-in is still
	// `EF_NODRAW`, and still undrawn.
	const auto BuildDefs = []()
	{
		FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_controller_effects_save"), 5290);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		return MoveTemp(Builder.Defs);
	};
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(BuildDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player"), Player))
	{
		return false;
	}
	Player->SetRuntimeModel(TEXT("models/character/pc/male/tremere_armor_0.mdl"));
	World.CreatePlayerControllerEntity();
	FElysiumMapSnapshot Snapshot;
	World.Freeze(Snapshot);

	FElysiumEntityWorld Restored(nullptr, nullptr, Services.Bundle());
	Restored.Load(BuildDefs());
	Restored.SpawnPlayer();
	Restored.Activate(0.0);
	Restored.ApplySnapshot(Snapshot);
	FElysiumEntity* Stand = Restored.FindPlayerController();
	FElysiumNpc* StandNpc = Stand != nullptr ? Stand->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("the relationship is rebound"), StandNpc))
	{
		return false;
	}
	TestEqual(TEXT("m_fEffects comes back as saved"), StandNpc->EffectsWord, 0x60u);
	TestFalse(TEXT("so the restored stand-in is not transmitted"), StandNpc->IsTransmitted());
	if (StandNpc->Visual != nullptr)
	{
		TestFalse(TEXT("and its body is not drawn"), StandNpc->Visual->IsVisible());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerInputTest,
	"Elysium.Arm.NpcKernelPlayerController.InputSuppressedWhileLive", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerInputTest::RunTest(const FString&)
{
	// `CHL2_Player` slot 462 `0x10351090`: the whole usercmd is zeroed while `m_hControllerNPC`
	// resolves and `!m_bWolf`. It is read off the handle, not latched at creation.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_controller_input"), 5250);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("the player"), Player))
	{
		return false;
	}
	// One frame of a player pushing forward, looking, holding `+use` and `+attack`.
	const auto Sample = []()
	{
		FElysiumUserCmd Cmd;
		Cmd.Seq = 42;
		Cmd.DeltaSeconds = 0.016f;
		Cmd.Move = FVector2D(1.0, 0.5);
		Cmd.Up = 0.25f;
		Cmd.LookDelta = FVector2D(3.0, -1.0);
		Cmd.Buttons = EElysiumButton::Use | EElysiumButton::Attack
			| static_cast<uint64>(EElysiumButton::Forward);
		return Cmd;
	};

	FElysiumUserCmd Cmd = Sample();
	TestFalse(TEXT("no stand-in: nothing is wiped"), Player->ApplyControllerUserCmdWipe(Cmd));
	TestTrue(TEXT("and the command stands"), Cmd.SameIntent(Sample()));

	F.World.CreatePlayerControllerEntity();
	Cmd = Sample();
	TestTrue(TEXT("a live stand-in wipes the command"), Player->ApplyControllerUserCmdWipe(Cmd));
	TestTrue(TEXT("no move"), Cmd.Move.IsZero() && Cmd.Up == 0.0f);
	TestTrue(TEXT("no look"), Cmd.LookDelta.IsZero());
	TestEqual(TEXT("no button at all — IN_USE included (0x10351090 is not SetupMove's 0x807)"),
		Cmd.Buttons, static_cast<uint64>(0));
	TestEqual(TEXT("the frame's bookkeeping survives"), Cmd.Seq, static_cast<uint32>(42));
	TestTrue(TEXT("m_bIsImmobilized is NOT raised: the live PostThink arm still runs, on nothing"),
		Player->IsMobile());

	Player->bWolf = true;
	Cmd = Sample();
	TestTrue(TEXT("m_bWolf: the wipe narrows"), Player->ApplyControllerUserCmdWipe(Cmd));
	TestTrue(TEXT("to the three move words"), Cmd.Move.IsZero() && Cmd.Up == 0.0f);
	TestEqual(TEXT("the buttons survive"), Cmd.Buttons, Sample().Buttons);
	Player->bWolf = false;

	F.World.RemovePlayerControllerEntity();
	Cmd = Sample();
	TestFalse(TEXT("the stand-in gone, nothing is wiped"), Player->ApplyControllerUserCmdWipe(Cmd));
	Player->SetImmobilized(true);
	TestFalse(TEXT("m_bIsImmobilized is its own term"), Player->IsMobile());
	Player->SetImmobilized(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerReleaseTest,
	"Elysium.Arm.NpcKernelPlayerController.ReleaseCopiesPoseOnly", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerReleaseTest::RunTest(const FString&)
{
	// `0x101618e0(player, 1, 1)`: the animation words and the transform, then the removal — and
	// nothing else. The view target is `PostThink`'s alone; model, skin and disposition are never
	// written.
	FControllerSceneFixture F(TEXT("npc_kernel_controller_release"), 5260);
	if (!TestNotNull(TEXT("the player"), F.Player) || !TestNotNull(TEXT("the stand-in"), F.Controller))
	{
		return false;
	}
	F.Player->Disposition = TEXT("Cinematic");
	F.Player->Skin = 2;
	F.Player->Viewtarget = FVector::ZeroVector;
	const FString PlayerModel = F.Player->Model;
	F.Controller->SetRuntimeModel(TEXT("models/character/pc/female/toreador_armor_0.mdl"));
	F.Controller->Skin = 4;
	F.Controller->Disposition = TEXT("Neutral");
	const FVector Mark(750.f, 125.f, 20.f);
	const FVector Facing(0.f, 210.f, 0.f);
	F.Stage(Mark, Facing);

	TestTrue(TEXT("RemoveControllerNPC (0x102272b0) releases with (1, 1)"),
		F.World.World.RemovePlayerControllerEntity());
	TestNull(TEXT("m_hControllerNPC = -1"), F.World.World.FindPlayerController());
	TestTrue(TEXT("the final mark"), F.Player->Origin.Equals(Mark));
	TestTrue(TEXT("and facing"), F.Player->Angles.Equals(Facing));
	TestTrue(TEXT("and velocity"), F.Player->Velocity.Equals(FVector(40.f, -40.f, 0.f)));
	TestEqual(TEXT("the gesture table travels"), F.Player->AnimOverlay[2].Sequence, 17);
	TestEqual(TEXT("the flinch table travels"), F.Player->Flinch[1].Sequence, 9);
	TestTrue(TEXT("the release copies no view target"), F.Player->Viewtarget.Equals(FVector::ZeroVector));
	TestEqual(TEXT("no model is handed back"), F.Player->Model, PlayerModel);
	TestEqual(TEXT("no skin"), F.Player->Skin, 2);
	TestEqual(TEXT("no disposition"), F.Player->Disposition, FString(TEXT("Cinematic")));
	TestNull(TEXT("the pawn takes its own graph back"), F.World.Services.PlayerBodyPoseSource);

	// (0, 0): `GetControllerNPC`'s class-mismatch arm lets the old stand-in go with nothing copied.
	F.World.World.CreatePlayerControllerEntity();
	FElysiumEntity* Stand = F.World.World.FindPlayerController();
	if (!TestNotNull(TEXT("a second stand-in"), Stand))
	{
		return false;
	}
	Stand->SetRuntimeTransform(FVector(-900.f, 40.f, 0.f), FVector(0.f, 90.f, 0.f));
	AddExpectedError(TEXT("asked for NPC class"), EAutomationExpectedErrorFlags::Contains, 1);
	F.World.World.CreatePlayerControllerEntity(TEXT("npc_VFrenzyShadow"));
	TestTrue(TEXT("the mismatch arm copies no transform"), F.Player->Origin.Equals(Mark));

	// (1, 1): `CBasePlayer::Event_Killed` `0x10163af0` releases the live stand-in with the copy.
	FElysiumEntity* Shadow = F.World.World.FindPlayerController();
	if (!TestNotNull(TEXT("the frenzy shadow stands in"), Shadow))
	{
		return false;
	}
	const FVector DeathMark(64.f, 64.f, 8.f);
	Shadow->SetRuntimeTransform(DeathMark, FVector(0.f, 45.f, 0.f));
	F.Player->OnKilled();
	TestNull(TEXT("Event_Killed releases the stand-in"), F.World.World.FindPlayerController());
	TestTrue(TEXT("with its transform"), F.Player->Origin.Equals(DeathMark));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPlayerControllerSeamsTest,
	"Elysium.Arm.NpcKernelPlayerController.FrenzyGrappleAndWolfSeams", GPlayerControllerTestFlags)
bool FElysiumNpcKernelPlayerControllerSeamsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_controller_seams"), 5270);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("the player"), Player))
	{
		return false;
	}
	FElysiumEntity* Shadow = F.World.Resolve(F.World.CreatePlayerControllerEntity(TEXT("npc_VFrenzyShadow")));
	if (!TestNotNull(TEXT("a frenzy shadow stands in"), Shadow))
	{
		return false;
	}

	// The frenzy-grapple arm: `Classify() == 3 && m_iCurFrenzyCount > 0 && m_bFrenzyHunger &&
	// Replenish(1)`. The shadow answers 3 and the count is set, but the two seams answer false.
	Player->CurFrenzyCount = 1;
	TestEqual(TEXT("the shadow is class 3"), Shadow->Classify(), 3);
	TestFalse(TEXT("Replenish(1) answers false"), FElysiumNpcFrenzyShadow::OwnerReplenish(*Player));
	F.World.UpdatePlayerFromController();
	TestEqual(TEXT("so the grapple arm (m_bIsFrenzyGrapple, 0x1033f6d0) answers nothing"),
		Player->ControllerFrenzyGrappleRequests, 0);
	Player->CurFrenzyCount = 0;

	// The wolf arm: `m_bWolf` answers false, so the copy runs controller -> pawn. Raised by hand, the
	// transform goes the other way and the pawn draws its own graph.
	TestFalse(TEXT("m_bWolf stands false"), Player->bWolf);
	const FVector PawnMark(-128.f, 256.f, 0.f);
	Shadow->SetRuntimeTransform(FVector(10.f, 10.f, 0.f), FVector::ZeroVector);
	Player->bWolf = true;
	Player->SetRuntimeTransform(PawnMark, FVector(0.f, 30.f, 0.f));
	F.World.UpdatePlayerFromController();
	TestTrue(TEXT("wolf: the controller takes the pawn's origin"), Shadow->Origin.Equals(PawnMark));
	TestTrue(TEXT("and the pawn stays where it is"), Player->Origin.Equals(PawnMark));
	TestNull(TEXT("and draws its own graph"), F.Services.PlayerBodyPoseSource);
	Player->bWolf = false;
	F.World.UpdatePlayerFromController();
	TestTrue(TEXT("unwolfed: the pawn is pinned to the controller again"), Player->Origin.Equals(Shadow->Origin));
	return true;
}

#endif
