#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcPlaceholder.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcChangBrosClaw.h"
#include "Substrate/ElysiumNpcChangBrosBlade.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcFrenzyShadow.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29e, family **Lifecycle19**. Every assertion is read off the decompiled C or the listing
// (READING.md corrections cited at the arm). Suite `Elysium.Substrate.NpcKernelLifecycle19`.

static constexpr EAutomationTestFlags GLifecycle19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Clear the counters and latches a case reads, so what it asserts is its own call's effect and
	// not the spawn's. The spawn already ran the class's own `NPCInit` (`Activate` -> slot 420), so
	// the species WORDS it wrote are standing too; this does not reset them. A case asserting such a
	// word first writes a value only the call under test replaces.
	void Lifecycle19Stand(FElysiumNpc& Npc)
	{
		Npc.ThinkSetCalls = 0;
		Npc.SpawnEquipRequests = 0;
		Npc.FloorDropPerformed = 0;
		Npc.FloorDropSkipped = 0;
		Npc.SetScheduleRetailCalls = 0;
		Npc.NodeGraphHullIndex() = 0;
		Npc.InNpcInit() = false;
	}

	// One NPC spawned as `RetailClass` through its factory classname (story 5 step 2; the controller
	// line since fold A2), stood by `Lifecycle19Stand`. Null or `CAI_BaseNPCTroika` stands the bare
	// Troika line. A controller-line body renames itself `playercontroller` in its `Spawn`
	// (`0x103a4510`), so it is found by its class.
	struct FLifecycle19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		explicit FLifecycle19Fixture(const TCHAR* RetailClass = TEXT("CNPC_VHumanCombatant"), bool bDormant = false)
			: World([RetailClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("lifecycle19_kernel"), 919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, RetailClass);
					return Builder;
				}(), bDormant)
		{
			Npc = World.Npc(TEXT("subject"));
			if (Npc == nullptr && RetailClass != nullptr)
			{
				Npc = World.NpcOfClass(RetailClass);
			}
			FElysiumNpcWorldFixture::Quiet({ Npc });
			if (Npc != nullptr)
			{
				Lifecycle19Stand(*Npc);
			}
		}

		// The subject as its species class (`FElysiumNpc::AsSpecies`); the fixture stood that class.
		template <class T>
		T* As() const
		{
			T* Typed = Npc != nullptr ? Npc->AsSpecies<T>() : nullptr;
			check(Typed != nullptr);
			return Typed;
		}
	};
}

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TroikaNpcInitTest,
	"Elysium.Arm.NpcKernelLifecycle19.TroikaNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TroikaNpcInitTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	F.Npc->StatTemplate = TEXT("Human");
	// The spawn already ran this very body, so every word asserted below takes a value only the
	// call under test replaces.
	F.Npc->DistTooFar = 0.f;
	ElysiumSchedule::Start(F.Npc->Schedule, ElysiumSched::IDLE_STAND, *F.Npc);
	F.Npc->Senses.Memory.bPlayerInPvs = false;
	F.Npc->Senses.Memory.bPlayerLos = false;
	F.Npc->WriteIdealStateRetail(3);
	F.Npc->bIsBccTargetable = false;
	F.Npc->bNpcIsAlive = false;
	F.Npc->MaxHealth = 0;
	F.Npc->CollisionMask = 0;
	F.Npc->InNpcInit() = true;
	F.Npc->WriteNpcStateRetail(3);
	F.Npc->bHidden = true;
	F.Npc->bCineScriptHidden = true;
	F.Npc->DisciplineFlags = 1;
	F.Npc->DisciplineFlags2 = 1;
	F.Npc->DisciplinePreFlags = 1;
	F.Npc->DisciplinePreFlags2 = 1;
	F.Npc->HitBuildupCount = 1;
	F.Npc->ScheduleHost.ForcedSchedule = 1;
	F.Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 5);
	F.Npc->NPCInit();                                                    // 0x1029a0b0
	TestEqual(TEXT("102733xx DistTooFar = 1024"), F.Npc->DistTooFar, 1024.f);
	TestFalse(TEXT("10280d30 schedule cleared"), F.Npc->Schedule.IsRunning());
	TestTrue(TEXT("1029a0xx PVS seeded"), F.Npc->Senses.Memory.bPlayerInPvs);
	TestTrue(TEXT("1029a0xx LOS seeded"), F.Npc->Senses.Memory.bPlayerLos);
	TestEqual(TEXT("1029a0xx Ideal IDLE"), F.Npc->IdealStateRetail(), 1);
	TestTrue(TEXT("1029a49e template → BCC targetable"), F.Npc->bIsBccTargetable);
	TestTrue(TEXT("1029a4xx m_bIsAlive = 1"), F.Npc->bNpcIsAlive);
	TestEqual(TEXT("1029a0xx MaxHealth 100"), F.Npc->MaxHealth, 100);
	TestEqual(TEXT("1029a0xx collision mask"), F.Npc->CollisionMask, 0x0202400b);
	TestFalse(TEXT("DAT_10937cf1 cleared"), F.Npc->InNpcInit());
	// `1029a0f5`: `m_NPCState = 0` — NPC_STATE_NONE, a state the raw overlay must be able to spell.
	TestEqual(TEXT("1029a0f5 m_NPCState = NONE"), F.Npc->NpcStateRetail(), 0);
	// `1029a0c0`: `m_fEffects = 0`, whose one modelled bit here is EF_NODRAW.
	TestTrue(TEXT("0x1029a0c0 clears effects but retains script-hidden latch"), F.Npc->bHidden);
	// `102735xx`: `m_bCineScriptHidden = 0` — its own byte, not the entity's hidden flag.
	TestFalse(TEXT("102735xx m_bCineScriptHidden cleared"), F.Npc->bCineScriptHidden);
	// `1029a589`: the four discipline bit words, cleared whole.
	TestEqual(TEXT("1029a589 m_iDisciplineFlags"), F.Npc->DisciplineFlags, 0);
	TestEqual(TEXT("1029a58f m_iDisciplineFlags2"), F.Npc->DisciplineFlags2, 0);
	TestEqual(TEXT("1029a595 m_iDisciplinePreFlags"), F.Npc->DisciplinePreFlags, 0);
	TestEqual(TEXT("1029a59b m_iDisciplinePreFlags2"), F.Npc->DisciplinePreFlags2, 0);
	// `1029a28b`: `m_iHitBuildupCount` is cleared BEFORE the spawn-equip block, not after it.
	TestEqual(TEXT("1029a28b m_iHitBuildupCount"), F.Npc->HitBuildupCount, 0);
	// `1029a52b`: `m_iForcedSchedule = 0` on the ordinary path.
	TestEqual(TEXT("1029a531 m_iForcedSchedule cleared"),
		static_cast<int32>(F.Npc->ScheduleHost.ForcedSchedule), 0);
	// `1029a5f4` / `1029a67a`: the two stat-list seeds land on the attribute container.
	TestEqual(TEXT("1029a5f4 CVStatList_t::Set(0xf, 0)"),
		F.Npc->Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health), 0);

	return true;
}

// The two words retail does NOT write in this body, each asserted as an absence.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19LateNpcInitTest,
	"Elysium.Arm.NpcKernelLifecycle19.LateNPCInitThinksInline", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19LateNpcInitTest::RunTest(const FString&)
{
	// `0x10273390` at `0x10273a5x`: `if (curtime <= 1.0) { ThinkSet(NPCInitThink, 0); m_flNextThink =
	// curtime + 0.1; } else { NPCInitThink(this); }` -- past the map's first second the init think
	// runs INLINE (`0x10273760`, slot 422 `StartNPC`, slot 421), so a maker child or an NPC on a map
	// reached by travel is started by its own `NPCInit`. `CNPC_VCamera::NPCInit` `0x103692c0` has the
	// same two arms. The pass-C smoke found the port's else arm empty: every late NPC stood on FLT_MAX.
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	F.World.Advance(2.0);
	const int32 InlineBefore = F.Npc->NpcInitInlineThinkCalls;
	const int32 ThinkSetsBefore = F.Npc->ThinkSetCalls;
	F.Npc->ThinkFunctionName.Reset();
	F.Npc->NPCInit();                                                    // 0x1029a0b0 -> 0x10273390
	TestEqual(TEXT("10273a5x else arm ran NPCInitThink inline"), F.Npc->NpcInitInlineThinkCalls, InlineBefore + 1);
	TestTrue(TEXT("10273aac slot 422 StartNPC re-armed the ordinary think"), F.Npc->ThinkSetCalls > ThinkSetsBefore);
	TestEqual(TEXT("1029a8b0 StartNPC installed LAB_1000f4e8"), F.Npc->ThinkFunctionName,
		FString(FElysiumNpcBase::StartNpcThinkFunction()));
	TestTrue(TEXT("late NPC is armed to think"), F.Npc->NextThink < ELYSIUM_NEVER_THINK);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TroikaNpcInitAbsencesTest,
	"Elysium.Arm.NpcKernelLifecycle19.TroikaNPCInitAbsences", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TroikaNpcInitAbsencesTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	// `1029a43e`/`1029a444`/`1029a44a`/`1029a450` write `+0x6418`, `+0x623c`, `+0x60a4` and
	// `+0x60a8` and then STOP: `+0x641c m_flNextFleeSoundTime` is not in this body at all. Only the
	// four species bodies that clear it (Newscaster, PlayerController, Pedestrian, Werewolf) do.
	F.Npc->Senses.Memory.NextFleeSoundTime = 7.0;
	F.Npc->Senses.Memory.NextSeeSoundSourceTime = 7.0;
	const int32 DelayedClearsBefore = F.Npc->DelayedConditionListClears;
	F.Npc->NPCInit();
	// `10273390` clears the delayed CONDITION list and then the delayed SOUND list — two
	// `0x102cc7e0` calls on two different lists.
	TestEqual(TEXT("10273390 clears both delayed lists"),
		F.Npc->DelayedConditionListClears, DelayedClearsBefore + 2);
	TestEqual(TEXT("1029a43e +0x6418 IS cleared"), F.Npc->Senses.Memory.NextSeeSoundSourceTime, 0.0);
	TestEqual(TEXT("+0x641c is NOT written by 0x1029a0b0"),
		F.Npc->Senses.Memory.NextFleeSoundTime, 7.0);
	// The two patrol path pairs ARE zeroed (`1029a236`..`1029a248`).
	TestEqual(TEXT("1029a236 the patrol route is dropped"), F.Npc->NumPatrolPointsForDebug(), 0);
	TestNull(TEXT("1029a242 the hunt route with it"), F.Npc->PatrolPathHuntCell.Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TuningTest,
	"Elysium.Arm.NpcKernelLifecycle19.OccludedTuning", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TuningTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	// The spawn already ran this very body, so the three words are dirtied first.
	F.Npc->OccludedDelayNormal = 0.f;
	F.Npc->OccludedDelayCover = 0.f;
	F.Npc->OccludedDelay = 0.f;
	F.Npc->NPCInit();
	// `0x101e8c50` / `0x101e8c70` are field getters on the process-global `CVFeatList_t`
	// (`0x10739d08`), loaded by `0x101e6310` from `Rules.txt` `Npc_Combat_Info` with the image
	// defaults 0.5 and 5.0 — NOT the 3.4 / 10.0 immediates `CNPC_VCamera::NPCInit` hard-codes.
	TestEqual(TEXT("101e8c50 OccludedDelayNormal default 0.5"), F.Npc->OccludedDelayNormal, 0.5f);
	TestEqual(TEXT("101e8c70 OccludedDelayCover default 5.0"), F.Npc->OccludedDelayCover, 5.f);
	TestEqual(TEXT("m_flOccludedDelay takes the normal one"), F.Npc->OccludedDelay, 0.5f);
	// The camera keeps its own literals (`103696xx`: `0x4059999a`, `0x41200000`, `0x3f000000`).
	{
		FLifecycle19Fixture Camera(TEXT("CNPC_VCamera"));
		if (Camera.Npc == nullptr)
		{
			AddError(TEXT("no camera"));
			return false;
		}
		Camera.As<FElysiumNpcCamera>()->bSpawn19GameRulesAllowNpcs = true;
		// The spawn's own `NPCInit` already wrote all three, so each is dirtied first.
		Camera.Npc->OccludedDelayNormal = 0.f;
		Camera.Npc->OccludedDelayCover = 0.f;
		Camera.Npc->EnemyMemory.FreeKnowledgeDuration = 0.0;
		Camera.Npc->NPCInit();
		TestEqual(TEXT("103692c0 hard-codes 3.4"), Camera.Npc->OccludedDelayNormal, 3.4f);
		TestEqual(TEXT("103692c0 hard-codes 10.0"), Camera.Npc->OccludedDelayCover, 10.f);
		TestEqual(TEXT("103696f7 hard-codes the enemy-store interval"),
			static_cast<float>(Camera.Npc->EnemyMemory.FreeKnowledgeDuration), 0.5f);
	}
	// `10369302`: the refuse arm returns WITHOUT clearing `DAT_10937cf1`.
	{
		FLifecycle19Fixture Camera(TEXT("CNPC_VCamera"));
		if (Camera.Npc == nullptr)
		{
			AddError(TEXT("no camera"));
			return false;
		}
		Camera.As<FElysiumNpcCamera>()->bSpawn19GameRulesAllowNpcs = false;
		Camera.Npc->NPCInit();
		TestTrue(TEXT("10369302 the refuse arm leaves DAT_10937cf1 SET"), Camera.Npc->InNpcInit());
		Camera.Npc->InNpcInit() = false;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19WeaponHideTest,
	"Elysium.Arm.NpcKernelLifecycle19.HumanCombatantHidesWeapon", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19WeaponHideTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	// `1038714a`: `GetActiveWeapon()` first — a null weapon is a no-op and nothing else happens.
	const int32 Before = F.As<FElysiumNpcHuman>()->HideActiveWeaponCalls;
	F.Npc->NPCInit();
	TestEqual(TEXT("1038714f a null active weapon hides nothing"),
		F.As<FElysiumNpcHuman>()->HideActiveWeaponCalls, Before);
	// With a weapon carried, `1038715f JMP [weapon vtbl + 0x108]` hides THE WEAPON.
	F.Npc->GiveNamedFightingItem(TEXT("item_w_fists"));
	FElysiumItem* const Active = F.Npc->Inventory.Active(*F.Npc);
	FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		// No item catalogue in this world: the arm cannot be driven, and saying so is the honest
		// answer rather than asserting the counter alone.
		return true;
	}
	Weapon->Unhide();
	F.Npc->NPCInit();
	TestEqual(TEXT("1038715f the ACTIVE WEAPON is hidden, not the NPC"),
		F.As<FElysiumNpcHuman>()->HideActiveWeaponCalls, Before + 1);
	TestTrue(TEXT("...and the weapon carries the NODRAW bit"), Weapon->bHidden);
	TestFalse(TEXT("...while the NPC itself is not hidden"), F.Npc->bHidden);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19RestoreChecksumTest,
	"Elysium.Arm.NpcKernelLifecycle19.OnRestoreChecksum", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19RestoreChecksumTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpc& N = *F.Npc;

	// A saved header that names a live schedule and carries ITS checksum is kept.
	ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N);
	TArray<uint8> TaskBytes;
	N.ScheduleTaskBytes(TaskBytes);
	uint32 Crc = FElysiumNpcBase::SaveCrc32Init();
	Crc = FElysiumNpcBase::SaveCrc32Update(Crc, TaskBytes.GetData(), TaskBytes.Num());
	N.LastSavedExtendedHeader.Version = 1;
	N.LastSavedExtendedHeader.Flags = 0;
	N.LastSavedExtendedHeader.ScheduleName = ElysiumScheduleName(ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND));
	N.LastSavedExtendedHeader.ScheduleCrc = FElysiumNpcBase::SaveCrc32Final(Crc);
	N.OnRestore(true);
	TestTrue(TEXT("1027c064 a matching task checksum keeps the schedule"), N.Schedule.IsRunning());

	// `1027c068`: a checksum that does NOT match the resolved schedule's task list drops it, and
	// the drop takes the give-up arm.
	ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N);
	N.LastSavedExtendedHeader.ScheduleCrc ^= 0xffffffffu;
	N.OnRestore(true);
	TestFalse(TEXT("1027c068 a stale task checksum drops the restored schedule"),
		N.Schedule.IsRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TroikaRestoreTest,
	"Elysium.Arm.NpcKernelLifecycle19.TroikaOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TroikaRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	// The world's network: one node. A patrol route over it validates (`0x1029f610` -> `0x10307ac0`);
	// a hunt route naming node 1 -- past the count -- does not.
	FElysiumPlaceRow Node;
	Node.Type = 2;
	F.World.World.Places().AdoptRows({ Node });
	{ const int32 PatrolIds[] = { 0, -1 }; N.BuildPatrolPath(&N.PatrolPathCell, 0, 0, 0, PatrolIds, FElysiumNpc::EPatrolPathBuild::Replace); }
	{ const int32 HuntIds[] = { 1, -1 }; N.BuildPatrolPath(&N.PatrolPathHuntCell, 0, 0, 0, HuntIds, FElysiumNpc::EPatrolPathBuild::Replace); }
	N.bSpawnCalled = false;
	const int32 Revalidations = N.PatrolPathRevalidations;
	const int32 Releases = N.PatrolPathReleases;
	const int32 Scans = N.RestorePlaceScans;
	N.OnRestore(true);
	// `102998c0` validates BOTH pairs (`0x1029f610`) and releases each on its own (`0x1029f5d0`).
	TestEqual(TEXT("1029f610 is asked for both routes"),
		N.PatrolPathRevalidations, Revalidations + 2);
	TestEqual(TEXT("1029f5d0 releases the stored hunt route alone"), N.PatrolPathReleases, Releases + 1);
	TestNotNull(TEXT("...the route over a network node is kept"), N.PatrolPathCell.Path);
	// `102db5e0` answers the place that holds this NPC — none here, which is `INDEX_NONE`.
	TestEqual(TEXT("102db5e0 runs once per restore"), N.RestorePlaceScans, Scans + 1);
	TestEqual(TEXT("...and with no place holding this NPC it answers nothing"),
		FElysiumNpcWorldFixture::Debug(F.Npc, TEXT("Ambient place")),
		FString(TEXT("searching")));   // the debug row's word for INDEX_NONE
	// `+0x62e9 = 1` is the entity's own spawn-called byte.
	TestTrue(TEXT("10299xxx +0x62e9 = 1"), N.bSpawnCalled);
	// The node arm's count: a restore with a valid hunt route bumps `DAT_106c994c` only through the
	// ped-link arm (the baseline); one whose hunt route names an id at or past the count bumps it
	// once more (`*network <= id`), then answers false.
	{ const int32 Near[] = { 0, -1 }; N.BuildPatrolPath(&N.PatrolPathHuntCell, 0, 0, 0, Near, FElysiumNpc::EPatrolPathBuild::Replace); }
	const int32 MissesBefore = FElysiumNpc::PatrolNodeMissCounter();
	N.OnRestore(true);
	const int32 BaselineDelta = FElysiumNpc::PatrolNodeMissCounter() - MissesBefore;
	TestNotNull(TEXT("a hunt route over the network is kept"), N.PatrolPathHuntCell.Path);
	{ const int32 FarIds[] = { 1000000, -1 }; N.BuildPatrolPath(&N.PatrolPathHuntCell, 0, 0, 0, FarIds, FElysiumNpc::EPatrolPathBuild::Replace); }
	const int32 MissesBeforeFar = FElysiumNpc::PatrolNodeMissCounter();
	N.OnRestore(true);
	TestEqual(TEXT("10307ac0: `*network <= id` bumps DAT_106c994c once more than a valid route"),
		FElysiumNpc::PatrolNodeMissCounter() - MissesBeforeFar, BaselineDelta + 1);
	TestNull(TEXT("...and the route is released"), N.PatrolPathHuntCell.Path);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19WerewolfRearmTest,
	"Elysium.Arm.NpcKernelLifecycle19.WerewolfRearm", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19WerewolfRearmTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VWerewolf"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcWerewolf& N = *F.As<FElysiumNpcWerewolf>();
	// Every word `0x103cac20` clears, dirtied first so the clear is visible.
	N.WerewolfMorphTimerA = 3.f;
	N.WerewolfMorphTimerB = 3.f;
	N.WerewolfMorphTimerC = 3.f;
	N.bWerewolfTaskFailed = true;
	N.WerewolfSnapWordA = 9;
	N.bWerewolfPlayFrustration = true;
	N.WerewolfFakeHullPosUnits = FVector(5.0, 5.0, 5.0);
	N.WerewolfMoveHintSearchStart = 9;
	N.WerewolfWord66ac = 9;
	N.WerewolfHintNodeCacheA = 9;
	N.RandomMoveHintNodeZone = 9;
	N.WerewolfHintFlags = 0xfu;
	N.WerewolfWord66f8 = 9;
	N.WerewolfWord66fc = 9;
	N.NearestNodeToPlayer = 9;
	N.NearestNodeToPlayerRefreshedAt = 9.0;
	N.NPCInit();                                                         // 0x103caef0 tail
	TestEqual(TEXT("103cac38 +0x66a4"), N.WerewolfMorphTimerA, 0.f);
	TestEqual(TEXT("103cac50 +0x66d4"), N.WerewolfMorphTimerB, 0.f);
	TestEqual(TEXT("103cac56 +0x66d8"), N.WerewolfMorphTimerC, 0.f);
	TestFalse(TEXT("103cac3e +0x66a1"), N.bWerewolfTaskFailed);
	TestEqual(TEXT("103cac44 +0x66a8"), N.WerewolfSnapWordA, 0);
	TestFalse(TEXT("103cac4a +0x66a9 m_bPlayFrustration"), N.bWerewolfPlayFrustration);
	TestEqual(TEXT("103cac62/6e/7a the fake-hull point takes vec3_origin"),
		N.WerewolfFakeHullPosUnits, FVector::ZeroVector);
	TestEqual(TEXT("103caca5 +0x66b8 m_pMoveHintSearchStart"), N.WerewolfMoveHintSearchStart, 0);
	TestEqual(TEXT("103cacab +0x66ac"), N.WerewolfWord66ac, 0);
	TestEqual(TEXT("103cacb1 +0x6708"), N.WerewolfHintNodeCacheA, INDEX_NONE);
	TestEqual(TEXT("103cacb7 +0x670c m_iRandomMoveHintNodeZone"), N.RandomMoveHintNodeZone,
		INDEX_NONE);
	TestEqual(TEXT("103cacbd +0x66e8 the hint-gate word"), static_cast<int32>(N.WerewolfHintFlags), 0);
	TestEqual(TEXT("103cacc3 +0x66f8"), N.WerewolfWord66f8, 0);
	TestEqual(TEXT("103cacc9 +0x66fc"), N.WerewolfWord66fc, 0);
	TestEqual(TEXT("103cacd5 +0x6704"), N.NearestNodeToPlayer, 0);
	TestEqual(TEXT("103caccf +0x6700"), N.NearestNodeToPlayerRefreshedAt, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19SpeciesWordsTest,
	"Elysium.Arm.NpcKernelLifecycle19.SpeciesWords", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19SpeciesWordsTest::RunTest(const FString&)
{
	// One NPC per class, each in its own fixture.
	// `103a6de9` is `CNPC_VSabbatLeader::m_nLastWaterLevel` (`+0x66c4`), the splash detector's edge
	// latch — not the entity's own `m_nWaterLevel` (`+0x03e0`).
	{
		FLifecycle19Fixture F(TEXT("CNPC_VSabbatLeader"));
		if (F.Npc == nullptr)
		{
			AddError(TEXT("no NPC"));
			return false;
		}
		F.As<FElysiumNpcSabbatLeader>()->SabbatLastWaterLevel = 3;
		F.Npc->WaterLevel = 2;
		F.Npc->NPCInit();
		TestEqual(TEXT("103a6de9 clears m_nLastWaterLevel"), F.As<FElysiumNpcSabbatLeader>()->SabbatLastWaterLevel, 0);
		TestEqual(TEXT("...and leaves the entity's own water level alone"), F.Npc->WaterLevel, 2);
	}

	// `103a435x` writes `m_pInterestingPlace = NULL`, which this runtime spells INDEX_NONE. The
	// spawn's own `NPCInit` already left it NULL, so the placeholder is first made to HOLD a place:
	// a live `intersting_place` whose marker table names it, which `OnRestore`'s `0x102db5e0` scan
	// writes into `+0x62ec` (the word has no other public writer).
	{
		FElysiumNpcWorldBuilder Builder(TEXT("lifecycle19_kernel"), 919);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, TEXT("CNPC_VPlaceholder"));
		Builder.AddEntity(TEXT("intersting_place"), TEXT("spot"), FVector(200.0, 0.0, 0.0));
		FElysiumNpcWorldFixture World(MoveTemp(Builder));
		FElysiumNpc* Npc = World.Npc(TEXT("subject"));
		FElysiumEntity* SpotEntity = World.World.FindByName(TEXT("spot"));
		if (Npc == nullptr || SpotEntity == nullptr)
		{
			AddError(TEXT("no NPC or no place"));
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Npc });
		FElysiumInterestingPlace* Spot = static_cast<FElysiumInterestingPlace*>(SpotEntity);
		Spot->Markers.Add(FElysiumInterestingPlace::FMarker{ Npc->Handle });
		Npc->OnRestore(true);                                                // 102db5e0 -> +0x62ec
		if (!TestNotEqual(TEXT("the placeholder holds the place before the call"),
			FElysiumNpcWorldFixture::Debug(Npc, TEXT("Ambient place")), FString(TEXT("searching"))))
		{
			return false;
		}
		Lifecycle19Stand(*Npc);
		Npc->NPCInit();
		TestEqual(TEXT("103a435x the placeholder holds NO interesting place"),
			FElysiumNpcWorldFixture::Debug(Npc, TEXT("Ambient place")),
			FString(TEXT("searching")));   // the debug row's word for INDEX_NONE
	}

	// `0x10376c10` walks the scratch list `DAT_1093ada8[0 .. DAT_1093ae9c)`. Retail's shipped path
	// FILLS it (`CheckForPlayerFrenzy` `0x10161fc0` runs the producer `0x10376d00` before creating
	// the shadow); the port has no frenzy check, so the list's seam answers empty and the recount on
	// `NPCInit` only resets the count — it seeds no enemy memory for the map.
	{
		FLifecycle19Fixture F(TEXT("CNPC_VFrenzyShadow"));
		if (F.Npc == nullptr)
		{
			AddError(TEXT("no NPC"));
			return false;
		}
		FElysiumNpcFrenzyShadow* Shadow = F.As<FElysiumNpcFrenzyShadow>();
		Shadow->HostileEnemyCount = 7;
		Shadow->NPCInit();
		TestEqual(TEXT("10376c17 +0x6664 = 0"), Shadow->HostileEnemyCount, 0);
		TestEqual(TEXT("...and the empty scratch list touches no other NPC"),
			Shadow->EnemyMemory.Records().Num(), 0);
	}
	// The loop itself, over a list the producer WOULD have filled: an entry that hates the friend
	// player is counted, and EVERY entry is handed to slot 544 at its origin.
	{
		FElysiumNpcWorldBuilder Builder(TEXT("lifecycle19_recount"), 921);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpcOfClass(TEXT("shadow"), FVector::ZeroVector, TEXT("CNPC_VFrenzyShadow"));
		Builder.AddNpc(TEXT("hater"), FVector(300.f, 0.f, 0.f));
		Builder.AddNpc(TEXT("bystander"), FVector(-300.f, 0.f, 0.f));
		FElysiumNpcWorldFixture W(MoveTemp(Builder));
		FElysiumNpcFrenzyShadow* Shadow = ElysiumTestAsSpecies<FElysiumNpcFrenzyShadow>(
			W.NpcOfClass(TEXT("CNPC_VFrenzyShadow")));
		FElysiumNpc* Hater = W.Npc(TEXT("hater"));
		FElysiumNpc* Bystander = W.Npc(TEXT("bystander"));
		FElysiumPlayer* Player = W.Player();
		if (!TestNotNull(TEXT("shadow"), Shadow) || !TestNotNull(TEXT("hater"), Hater)
			|| !TestNotNull(TEXT("bystander"), Bystander) || !TestNotNull(TEXT("player"), Player))
		{
			return false;
		}
		FElysiumNpcWorldFixture::Quiet({ Shadow, Hater, Bystander });
		Shadow->FriendPlayer = Player->Handle;
		Hater->Relationships.SetEntity(Player->Handle, EElysiumRelationship::Hate, 10);
		Bystander->Relationships.SetEntity(Player->Handle, EElysiumRelationship::Like, 10);
		FElysiumEntity* const Scratch[] = { Hater, Bystander };
		TestEqual(TEXT("0x10376c10 counts the one entry that hates the friend player"),
			Shadow->HostileRecountOver(MakeArrayView(Scratch)), 1);
		TestEqual(TEXT("...into m_iHostileEnemyCount"), Shadow->HostileEnemyCount, 1);
		TestEqual(TEXT("...and hands every entry to slot 544 UpdateEnemyMemory"),
			Shadow->EnemyMemory.Records().Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19FarSightTest,
	"Elysium.Arm.NpcKernelLifecycle19.FarSight", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19FarSightTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	F.Npc->SpawnFlags |= 0x100;
	F.Npc->FElysiumNpcBase::NPCInit();                                                // 0x10273390 far-sight arm
	TestEqual(TEXT("spawnflag 0x100 DistTooFar = 1e9"), F.Npc->DistTooFar, 1.0e9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TeleportTailTest,
	"Elysium.Arm.NpcKernelLifecycle19.TeleportTail", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TeleportTailTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	F.Npc->TeleportMoveTimer = 2.f;
	F.Npc->NPCInit();
	TestTrue(TEXT("1029a6xx timer above 0 becomes curtime+self+4"),
		F.Npc->TeleportMoveTimer > 2.f);
	// `1029a6f9 CALL 0x10001041` -> `0x102ae7f0`, whose whole body is `*(this + 0x65c8) = param_1`:
	// it writes `m_iForcedSchedule` and installs NOTHING.
	TestEqual(TEXT("102ae7f0 writes m_iForcedSchedule 0xfe"),
		static_cast<int32>(F.Npc->ScheduleHost.ForcedSchedule), 0xfe);
	TestEqual(TEXT("...and installs no schedule"), F.Npc->SetScheduleRetailCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19CameraNpcInitTest,
	"Elysium.Arm.NpcKernelLifecycle19.CameraNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19CameraNpcInitTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VCamera"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcCamera* CameraNpc = ElysiumTestAsSpecies<FElysiumNpcCamera>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VCamera"), CameraNpc))
	{
		return false;
	}
	F.As<FElysiumNpcCamera>()->bSpawn19GameRulesAllowNpcs = true;
	// The spawn's own `NPCInit` already wrote every word read below, so each is dirtied first.
	CameraNpc->OccludedDelayNormal = 0.f;
	CameraNpc->OccludedDelayCover = 0.f;
	CameraNpc->bIsBccTargetable = true;
	CameraNpc->TakeDamageMode = 2;
	CameraNpc->NPCInit();                                                    // 0x103692c0
	TestEqual(TEXT("hard-coded occluded delay 3.4"), CameraNpc->OccludedDelayNormal, 3.4f);
	TestEqual(TEXT("hard-coded cover delay 10"), CameraNpc->OccludedDelayCover, 10.f);
	TestFalse(TEXT("camera not BCC targetable"), CameraNpc->bIsBccTargetable);
	TestEqual(TEXT("takedamage 0"), CameraNpc->TakeDamageMode, 0);
	// The refuse arm on a second camera of its own.
	FLifecycle19Fixture Refused(TEXT("CNPC_VCamera"));
	if (Refused.Npc == nullptr)
	{
		AddError(TEXT("no second camera"));
		return false;
	}
	Refused.As<FElysiumNpcCamera>()->bSpawn19GameRulesAllowNpcs = false;
	const int32 Before = Refused.As<FElysiumNpcCamera>()->CameraSelfRemovals;
	Refused.Npc->NPCInit();
	TestEqual(TEXT("engine refuse deletes the camera"), Refused.As<FElysiumNpcCamera>()->CameraSelfRemovals, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19PayphoneTest,
	"Elysium.Arm.NpcKernelLifecycle19.PayphoneNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19PayphoneTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CPayphone"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcPayphone* Payphone = ElysiumTestAsSpecies<FElysiumNpcPayphone>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CPayphone"), Payphone))
	{
		return false;
	}
	// The spawn's own `NPCInit` already wrote all four, so each takes its opposite first.
	Payphone->bIsBccTargetable = false;
	Payphone->bInvincible = false;
	Payphone->bNpcIsAlive = true;
	Payphone->Senses.bCanPerformSenses = true;
	Payphone->NPCInit();                                                    // 0x101aab90
	TestTrue(TEXT("+0x1480 targetable"), Payphone->bIsBccTargetable);
	TestTrue(TEXT("+0x63d8 invincible"), Payphone->bInvincible);
	TestFalse(TEXT("+0x1481 not alive"), Payphone->bNpcIsAlive);
	TestFalse(TEXT("senses+0x80 off"), Payphone->Senses.bCanPerformSenses);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19SwarmDistTest,
	"Elysium.Arm.NpcKernelLifecycle19.SwarmDistTooFar", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19SwarmDistTest::RunTest(const FString&)
{
	// `CNPC_VBatSwarm` (`0x103673b0`) and `CNPC_VSheriffSwarm` (`0x103b2360`) write the same word,
	// but neither class has an instance and neither carries a port arm (0019 story 5 step 1).
	FLifecycle19Fixture F(TEXT("CNPC_VBach"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcBach* Bach = ElysiumTestAsSpecies<FElysiumNpcBach>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VBach"), Bach))
	{
		return false;
	}
	// The spawn's own `NPCInit` already wrote the word; the base body's 1024 stands in first, so
	// only the Bach tail's write lands 65535.
	Bach->DistTooFar = 1024.f;
	Bach->NPCInit();
	TestEqual(TEXT("CNPC_VBach DistTooFar 65535"), Bach->DistTooFar, 65535.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19HullIndexTest,
	"Elysium.Arm.NpcKernelLifecycle19.NodeGraphHull", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19HullIndexTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VGargoyle"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcGargoyle* Gargoyle = ElysiumTestAsSpecies<FElysiumNpcGargoyle>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VGargoyle"), Gargoyle))
	{
		return false;
	}
	Gargoyle->NPCInit();                                                    // 0x103785f0
	TestEqual(TEXT("Gargoyle hull 0xe"), Gargoyle->NodeGraphHullIndex(), 0x0e);
	FLifecycle19Fixture ManBat(TEXT("CNPC_VManBat"));
	if (ManBat.Npc == nullptr)
	{
		AddError(TEXT("no ManBat"));
		return false;
	}
	FElysiumNpcManBat* Bat = ElysiumTestAsSpecies<FElysiumNpcManBat>(ManBat.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VManBat"), Bat))
	{
		return false;
	}
	Bat->NPCInit();                                              // 0x1038b070 last writer wins
	TestEqual(TEXT("ManBat hull 0x14 last-writer"), Bat->NodeGraphHullIndex(), 0x14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ManBatOrderTest,
	"Elysium.Arm.NpcKernelLifecycle19.ManBatTimersFirst", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ManBatOrderTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VManBat"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcManBat* ManBat = ElysiumTestAsSpecies<FElysiumNpcManBat>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VManBat"), ManBat))
	{
		return false;
	}
	// The spawn's own `NPCInit` already armed both, so each is disarmed first.
	ManBat->ManBatFlapTimer = 0.0;
	ManBat->ManBatFlyTimer = 0.0;
	ManBat->NPCInit();
	TestTrue(TEXT("flap timer armed before base (curtime+2.3)"), ManBat->ManBatFlapTimer > 0.0);
	TestTrue(TEXT("fly timer armed (curtime+0.1)"), ManBat->ManBatFlyTimer > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19PlaceholderTest,
	"Elysium.Arm.NpcKernelLifecycle19.PlaceholderNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19PlaceholderTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VPlaceholder"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcPlaceholder* Placeholder = ElysiumTestAsSpecies<FElysiumNpcPlaceholder>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VPlaceholder"), Placeholder))
	{
		return false;
	}
	// The spawn's own `NPCInit` already wrote both; each takes a value only the call replaces. The
	// think name is the base body's first-second one, which `ThinkSet(NULL)` must clear.
	Placeholder->bIsBccTargetable = false;
	Placeholder->ThinkFunctionName = FElysiumNpcBase::NpcInitThinkFunction();
	Placeholder->NPCInit();                                                    // 0x103a4350
	TestTrue(TEXT("targetable"), Placeholder->bIsBccTargetable);
	TestTrue(TEXT("ThinkSet(NULL)"), Placeholder->ThinkFunctionName.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19CopTest,
	"Elysium.Arm.NpcKernelLifecycle19.CopNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19CopTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VCop"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcCop* CopNpc = ElysiumTestAsSpecies<FElysiumNpcCop>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VCop"), CopNpc))
	{
		return false;
	}
	CopNpc->bWasEverInCombat = true;
	CopNpc->NPCInit();                                                    // 0x10372b00
	TestFalse(TEXT("+0x6670 cleared AFTER HumanCombatant"), CopNpc->bWasEverInCombat);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19FrenzyShadowTest,
	"Elysium.Arm.NpcKernelLifecycle19.FrenzyShadowNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19FrenzyShadowTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VFrenzyShadow"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcFrenzyShadow* Shadow = F.As<FElysiumNpcFrenzyShadow>();
	Shadow->FistsNullWeaponFaults = 0;
	Shadow->bFailedGrapple = true;
	Shadow->NPCInit();                                                   // 0x10375c80
	TestEqual(TEXT("state 0xb"), Shadow->IdealStateRetail(), 0xb);
	TestTrue(TEXT("frenzied flags 0x5ddf"), Shadow->HasFrenzied(0x5ddfu));
	TestEqual(TEXT("speed scale 8"), Shadow->NpcSpeedScale, 8.f);
	TestTrue(TEXT("senses on"), Shadow->Senses.bCanPerformSenses);
	TestTrue(TEXT("nav ignore physics"), Shadow->bNavIgnorePhysicsProps);
	TestFalse(TEXT("+0x6668 m_bFailedGrapple = 0"), Shadow->bFailedGrapple);
	TestEqual(TEXT("the controller body under it ran: investigate mode 6"), Shadow->InvestigateMode, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19WerewolfTest,
	"Elysium.Arm.NpcKernelLifecycle19.WerewolfNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19WerewolfTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VWerewolf"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcWerewolf* Werewolf = ElysiumTestAsSpecies<FElysiumNpcWerewolf>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VWerewolf"), Werewolf))
	{
		return false;
	}
	// The spawn's own `NPCInit` already wrote all four; each takes a non-werewolf value first (the
	// fixture's authored template, the member default, the base body's 1024, mode 0), so only the
	// werewolf body's writes pass.
	Werewolf->StatTemplate = TEXT("Thug");
	Werewolf->FieldOfView = 0.2f;
	Werewolf->DistTooFar = 1024.f;
	Werewolf->InvestigateMode = 0;
	Werewolf->NPCInit();                                                    // 0x103caef0
	TestEqual(TEXT("stat template Werewolf"), Werewolf->StatTemplate, FString(TEXT("Werewolf")));
	TestTrue(TEXT("FOV cos(120)= -0.5"),
		FMath::IsNearlyEqual(Werewolf->FieldOfView, -0.5f, 1.e-4f));
	TestEqual(TEXT("DistTooFar 1e9"), Werewolf->DistTooFar, 1.0e9f);
	TestEqual(TEXT("investigate AnyPlayer"), Werewolf->InvestigateMode, 3);
	const float FloorAfterInit = F.As<FElysiumNpcWerewolf>()->WerewolfTeleportDistanceB;
	Werewolf->OnRestore(true);                                              // 0x103cabf0 += again
	TestTrue(TEXT("teleport floor accumulates across restore (defect 2)"),
		F.As<FElysiumNpcWerewolf>()->WerewolfTeleportDistanceB > FloorAfterInit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ChangTypeTest,
	"Elysium.Arm.NpcKernelLifecycle19.ChangTypeBeforeChain", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ChangTypeTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VChangBrosBlade"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcChangBrosBlade* BladeNpc = ElysiumTestAsSpecies<FElysiumNpcChangBrosBlade>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VChangBrosBlade"), BladeNpc))
	{
		return false;
	}
	// The spawn's own `NPCInit` already wrote 0; the Claw's answer stands in first, so the Blade's
	// write is visible as a change.
	BladeNpc->ChangType = 1;
	BladeNpc->NPCInit();                                                    // 0x1036f100
	TestEqual(TEXT("Blade SetChangType(0) before chain"), BladeNpc->ChangType, 0);
	FLifecycle19Fixture Claw(TEXT("CNPC_VChangBrosClaw"));
	if (Claw.Npc == nullptr)
	{
		AddError(TEXT("no Claw"));
		return false;
	}
	FElysiumNpcChangBrosClaw* ClawNpc = ElysiumTestAsSpecies<FElysiumNpcChangBrosClaw>(Claw.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VChangBrosClaw"), ClawNpc))
	{
		return false;
	}
	// The Blade's answer, so the Claw's write is visible as a change.
	ClawNpc->ChangType = 0;
	ClawNpc->NPCInit();                                                 // 0x1036f900
	TestEqual(TEXT("Claw SetChangType(1) before chain"), ClawNpc->ChangType, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19StartNpcTest,
	"Elysium.Arm.NpcKernelLifecycle19.StartNPC", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19StartNpcTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	F.Npc->StartNPC();                                                   // 0x1029a8b0
	TestTrue(TEXT("10273ad0 ThinkSet on both first-second arms"), F.Npc->ThinkSetCalls >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19OnRestoreGiveUpTest,
	"Elysium.Arm.NpcKernelLifecycle19.OnRestoreGiveUp", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19OnRestoreGiveUpTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CAI_BaseNPCTroika"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	F.Npc->LastSavedExtendedHeader.Version = 0;
	F.Npc->OnRestore(true);                                              // 0x102998c0 → give-up
	TestFalse(TEXT("give-up clears refind latch"), F.Npc->BaseScheduleHost.bDoPostRestoreRefindPath);
	TestFalse(TEXT("give-up clears schedule"), F.Npc->Schedule.IsRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19PedestrianRestoreTest,
	"Elysium.Arm.NpcKernelLifecycle19.PedestrianOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19PedestrianRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VPedestrian"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcPedestrian* Pedestrian = ElysiumTestAsSpecies<FElysiumNpcPedestrian>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VPedestrian"), Pedestrian))
	{
		return false;
	}
	Pedestrian->PedestrianLevelResetType = 2;
	const int32 Before = Pedestrian->InventoryDestroys;
	Pedestrian->OnRestore(true);                                              // gate m_eLevelResetType==2
	TestEqual(TEXT("type 2 skips reset"), Pedestrian->InventoryDestroys, Before);
	Pedestrian->PedestrianLevelResetType = 0;
	Pedestrian->OnRestore(false);                                             // bool argument clear
	TestEqual(TEXT("bFromLoad false skips"), Pedestrian->InventoryDestroys, Before);
	Pedestrian->OnRestore(true);
	TestTrue(TEXT("reset arm Inventory_Destroy"), Pedestrian->InventoryDestroys > Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ZombieCrawlTest,
	"Elysium.Arm.NpcKernelLifecycle19.ZombieNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ZombieCrawlTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VZombie"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcZombie* Zombie = ElysiumTestAsSpecies<FElysiumNpcZombie>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VZombie"), Zombie))
	{
		return false;
	}
	// The spawn's own `NPCInit` already wrote both, so each is cleared first.
	F.As<FElysiumNpcZombie>()->bZombieNeedsCrawlOutOfGround = false;
	Zombie->LastSetScheduleRetail = 0;
	Zombie->NPCInit();                                                    // 0x103defc0
	TestTrue(TEXT("crawl-out latch first"), F.As<FElysiumNpcZombie>()->bZombieNeedsCrawlOutOfGround);
	TestEqual(TEXT("SetSchedule(0x161) miss arm"), Zombie->LastSetScheduleRetail, 0x161);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TaxiIdleTest,
	"Elysium.Arm.NpcKernelLifecycle19.TaxiDriverNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TaxiIdleTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VTaxiDriver"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcTaxiDriver* TaxiDriver = ElysiumTestAsSpecies<FElysiumNpcTaxiDriver>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VTaxiDriver"), TaxiDriver))
	{
		return false;
	}
	// The spawn's own `NPCInit` (and the first think's NONE -> IDLE) already left both, so the
	// ideal state takes ALERT and the senses go off first.
	TaxiDriver->WriteIdealStateRetail(3);
	TaxiDriver->Senses.bCanPerformSenses = false;
	TaxiDriver->NPCInit();                                                    // 0x103b35c0
	TestEqual(TEXT("ideal IDLE written then SetState(1)"), TaxiDriver->IdealStateRetail(), 1);
	TestTrue(TEXT("senses ON"), TaxiDriver->Senses.bCanPerformSenses);
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ArmCoverageTest,
	"Elysium.Substrate.NpcKernelLifecycle19.ArmCoverage", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ArmCoverageTest::RunTest(const FString&)
{
	// Every slot-420 override the census holds is named. Addresses from the census. The swarms'
	// `0x103673b0` / `0x103b2360` stay census rows although neither class has an instance and 0019
	// story 5 step 1 removed their port arms: this case checks the census, not the arms.
	const TCHAR* Slot420[] = {
		TEXT("0x101aab90"), TEXT("0x1035cec0"), TEXT("0x10360ce0"), TEXT("0x10363940"),
		TEXT("0x103673b0"), TEXT("0x103692c0"), TEXT("0x1036b050"), TEXT("0x1036f100"),
		TEXT("0x1036f900"), TEXT("0x10372b00"), TEXT("0x10375c80"), TEXT("0x103785f0"),
		TEXT("0x1037b290"), TEXT("0x1037e240"), TEXT("0x1037fa70"), TEXT("0x10387140"),
		TEXT("0x1038b070"), TEXT("0x103a0420"), TEXT("0x103a2570"),
		TEXT("0x103a4350"), TEXT("0x103a4580"), TEXT("0x103a6d40"), TEXT("0x103ae6c0"),
		TEXT("0x103b2360"), TEXT("0x103b35c0"), TEXT("0x103b91d0"), TEXT("0x103c1c80"),
		TEXT("0x103c5840"), TEXT("0x103caef0"), TEXT("0x103dce00"), TEXT("0x103dd800"),
		TEXT("0x103defc0"),
	};
	for (const TCHAR* Addr : Slot420)
	{
		bool bFound = false;
		for (const FElysiumNpcClassSlot& Row : ElysiumNpcKernelShape::Overrides())
		{
			if (Row.Slot == 420 && FCString::Strcmp(Row.Address, Addr) == 0)
			{
				bFound = true;
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("slot 420 %s is a census override"), Addr), bFound);
	}
	return true;
}

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19Guard1Test,
	"Elysium.Arm.NpcKernelLifecycle19.Guard1NPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19Guard1Test::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VGuard1"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcGuard1* Guard1 = ElysiumTestAsSpecies<FElysiumNpcGuard1>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VGuard1"), Guard1))
	{
		return false;
	}
	// `+0x6660` on THIS class is `CNPC_VGuard1::m_fHatesPlayer` (`vtmb_fields CNPC_VGuard1 0x6660`),
	// not `CNPC_VAnimal::m_bPlayerAttackedMe` at the same offset.
	Guard1->bGuard1HatesPlayer = true;
	Guard1->NPCInit();                                                    // 0x1037e240 +0x6660 first
	TestFalse(TEXT("1037e24a m_fHatesPlayer cleared BEFORE Troika"), Guard1->bGuard1HatesPlayer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19CameraStartNpcTest,
	"Elysium.Arm.NpcKernelLifecycle19.CameraStartNPC", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19CameraStartNpcTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VCamera"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcCamera* CameraNpc = ElysiumTestAsSpecies<FElysiumNpcCamera>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VCamera"), CameraNpc))
	{
		return false;
	}
	CameraNpc->Model = TEXT("models/null.mdl");
	CameraNpc->StartNPC();                                                   // 0x10369930
	TestTrue(TEXT("null.mdl drops to floor"), CameraNpc->FloorDropPerformed > 0);
	TestTrue(TEXT("final ThinkSet re-arm does not change stamp (delay 0)"),
		CameraNpc->ThinkSetCalls >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19MingXiaoRestoreTest,
	"Elysium.Arm.NpcKernelLifecycle19.MingXiaoTentacleOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19MingXiaoRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VMingXiaoTentacle"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcMingXiaoTentacle* MingXiaoTentacle = ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VMingXiaoTentacle"), MingXiaoTentacle))
	{
		return false;
	}
	F.As<FElysiumNpcMingXiaoTentacle>()->MingXiaoTentacleCache() = MingXiaoTentacle->Handle;
	MingXiaoTentacle->OnRestore(true);                                              // 0x1039f000 after base
	TestFalse(TEXT("DAT_1093bd34 invalidated after Troika"),
		F.As<FElysiumNpcMingXiaoTentacle>()->MingXiaoTentacleCache().IsSet());
	return true;
}

// 0019/6: `CNPC_VMingXiao::Save` / `vfunc127` and the tentacle's `Save` / `Restore` pair only bracketed two FIELD_TIME stamps (modes 4 and 3) around the archive. The
// generated SAVE walk stores the stamps itself; this pins that an unset stamp survives the real
// persistence path (`Freeze` / `ApplySnapshot`) unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19MingXiaoStampRoundTripTest,
	"Elysium.Arm.NpcKernelLifecycle19.MingXiaoStampRoundTrip", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19MingXiaoStampRoundTripTest::RunTest(const FString&)
{
	{
		FLifecycle19Fixture From(TEXT("CNPC_VMingXiao"));
		FLifecycle19Fixture To(TEXT("CNPC_VMingXiao"), true);
		if (From.Npc == nullptr || To.Npc == nullptr)
		{
			AddError(TEXT("no NPC"));
			return false;
		}
		FElysiumNpcMingXiao* Src = From.As<FElysiumNpcMingXiao>();
		Src->MingXiaoRegrowTimers[0] = static_cast<double>(FLT_MAX);
		Src->MingXiaoRegrowTimers[1] = 12.5;
		ElysiumRoundTripSnapshot(From.World.World, To.World.World);
		FElysiumNpc* Back = To.World.NpcOfClass(TEXT("CNPC_VMingXiao"));
		FElysiumNpcMingXiao* Dst = Back != nullptr ? ElysiumTestAsSpecies<FElysiumNpcMingXiao>(Back) : nullptr;
		if (!TestNotNull(TEXT("ming xiao restores"), Dst))
		{
			return false;
		}
		TestEqual(TEXT("m_rflRegrowTimers[0] stays parked at FLT_MAX (mode 4)"),
			Dst->MingXiaoRegrowTimers[0], static_cast<double>(FLT_MAX));
		TestEqual(TEXT("m_rflRegrowTimers[1] keeps its stamp"), Dst->MingXiaoRegrowTimers[1], 12.5);
	}
	{
		FLifecycle19Fixture From(TEXT("CNPC_VMingXiaoTentacle"));
		FLifecycle19Fixture To(TEXT("CNPC_VMingXiaoTentacle"), true);
		if (From.Npc == nullptr || To.Npc == nullptr)
		{
			AddError(TEXT("no NPC"));
			return false;
		}
		From.As<FElysiumNpcMingXiaoTentacle>()->MingXiaoTentaclePhaseExpireTimer = 0.0;
		ElysiumRoundTripSnapshot(From.World.World, To.World.World);
		FElysiumNpc* Back = To.World.NpcOfClass(TEXT("CNPC_VMingXiaoTentacle"));
		FElysiumNpcMingXiaoTentacle* Dst =
			Back != nullptr ? ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Back) : nullptr;
		if (!TestNotNull(TEXT("tentacle restores"), Dst))
		{
			return false;
		}
		TestEqual(TEXT("m_flPhaseExpireTimer 0 stays unarmed (mode 3)"),
			Dst->MingXiaoTentaclePhaseExpireTimer, 0.0);
	}
	return true;
}

// Slot 127's load-side halves on the boss line (0019/6): `CNPC_VVampireBoss::Restore`
// and the species rows that run it first, now each class's `OnPostRestore` after the SAVE walk. Both
// worlds hold the same off-retail values first, so each expectation holds whatever the walk
// carries. Andrei and the Chang brothers are
// `NpcKernelSpeciesLifecycle10.AndreiBloodRestore` / `.ChangBrosRestore`.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19BossPostRestoreTest,
	"Elysium.Arm.NpcKernelLifecycle19.BossPostRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19BossPostRestoreTest::RunTest(const FString&)
{
	struct FRow
	{
		const TCHAR* Class;
		const TCHAR* Model;
		const TCHAR* Classname;
		const TCHAR* E0;
		const TCHAR* E1;
		float Gravity;
		int32 Hull;
	};
	const FRow Rows[] = {
		{ TEXT("CNPC_VVampireBoss"), TEXT(""), TEXT("npc_VVampireBoss"), TEXT(""), TEXT(""), 9.f, 0 },
		{ TEXT("CNPC_VAsianVampire"), TEXT(""), TEXT("npc_VVampireBoss"), TEXT(""), TEXT(""), 2.f, 0 },  // CNPC_VAsianVampire::restore
		{ TEXT("CNPC_VSabbatLeader"), TEXT("models/character/monster/Andrei/andrei.mdl"),
			TEXT("npc_VSabbatLeader"), TEXT("Andrei_powerup_emitter"), TEXT("Andrei_powerup_emitter"),
			9.f, 0 },                                                                                    // vfunc127
		{ TEXT("CNPC_VSheriffMan"), TEXT("models/character/monster/manbat/manbat.mdl"),
			TEXT("npc_VSheriffMan"), TEXT(""), TEXT(""), 2.f, 0x15 },                                    // vfunc127
	};
	// `DAT_109340d8` is a process global; the case puts it back the way it found it.
	const int32 HullBefore = FElysiumNpc::NodeGraphHullIndex();
	for (const FRow& Row : Rows)
	{
		FLifecycle19Fixture From(Row.Class);
		FLifecycle19Fixture To(Row.Class, true);
		if (!TestNotNull(*FString::Printf(TEXT("%s stood"), Row.Class), From.Npc)
			|| !TestNotNull(*FString::Printf(TEXT("%s stood twice"), Row.Class), To.Npc))
		{
			continue;
		}
		for (FElysiumNpcVampireBoss* Boss : { From.As<FElysiumNpcVampireBoss>(), To.As<FElysiumNpcVampireBoss>() })
		{
			Boss->VampireBossMonsterModelName = TEXT("models/saved_mid_transform.mdl");
			Boss->VampireBossMonsterClassname = TEXT("npc_saved");
			for (FString& Name : Boss->BodyEmitterNames)
			{
				Name = TEXT("saved_emitter");
			}
			Boss->JumpGravity = 9.f;
		}
		FElysiumNpc::NodeGraphHullIndex() = 0;
		ElysiumRoundTripSnapshot(From.World.World, To.World.World);
		const FElysiumNpcVampireBoss* B = To.As<FElysiumNpcVampireBoss>();
		TestEqual(*FString::Printf(TEXT("%s +0x6680"), Row.Class), B->VampireBossMonsterModelName,
			FString(Row.Model));
		TestEqual(*FString::Printf(TEXT("%s +0x6694"), Row.Class), B->VampireBossMonsterClassname,
			FString(Row.Classname));
		TestEqual(*FString::Printf(TEXT("%s emitter 0"), Row.Class), B->BodyEmitterNames[0], FString(Row.E0));
		TestEqual(*FString::Printf(TEXT("%s emitter 1"), Row.Class), B->BodyEmitterNames[1], FString(Row.E1));
		TestTrue(*FString::Printf(TEXT("%s emitter 3 cleared (0x103c6eb0)"), Row.Class),
			B->BodyEmitterNames[3].IsEmpty());
		TestEqual(*FString::Printf(TEXT("%s +0x64b8"), Row.Class), B->JumpGravity, Row.Gravity);
		TestEqual(*FString::Printf(TEXT("%s DAT_109340d8"), Row.Class), FElysiumNpc::NodeGraphHullIndex(),
			Row.Hull);
	}
	FElysiumNpc::NodeGraphHullIndex() = HullBefore;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19RunnerRestoreTest,
	"Elysium.Arm.NpcKernelLifecycle19.TzimisceRunnerOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19RunnerRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F(TEXT("CNPC_VTzimisceRunner"));
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcTzimisceRunner* TzimisceRunner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(F.Npc);
	if (!TestNotNull(TEXT("the subject is a CNPC_VTzimisceRunner"), TzimisceRunner))
	{
		return false;
	}
	TzimisceRunner->EntityFlags2Word = 0x14u;
	TzimisceRunner->OnRestore(true);                                              // 0x103c3c40
	TestEqual(TEXT("RemoveFlag2(4) (0x103c3c78)"), TzimisceRunner->EntityFlags2Word, 0x10u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19SpeciesSmokeTest,
	"Elysium.Arm.NpcKernelLifecycle19.SpeciesNPCInitSmoke", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19SpeciesSmokeTest::RunTest(const FString&)
{
	// One call per slot-420 species body, each on a fresh NPC spawned as the row's class (the
	// controller line through its own factories since fold A2). Distinguishing writes from the
	// listing.
	struct FRow
	{
		const TCHAR* Cls;
		const TCHAR* Addr;
	};
	const FRow Rows[] = {
		{ TEXT("CPayphone"), TEXT("0x101aab90") },
		{ TEXT("CNPC_VAndreiBlood"), TEXT("0x1035cec0") },
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10360ce0") },
		{ TEXT("CNPC_VBach"), TEXT("0x10363940") },
		{ TEXT("CNPC_VCamera"), TEXT("0x103692c0") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036b050") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036f100") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036f900") },
		{ TEXT("CNPC_VCop"), TEXT("0x10372b00") },
		{ TEXT("CNPC_VFrenzyShadow"), TEXT("0x10375c80") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x103785f0") },
		{ TEXT("CNPC_VGhoulCroucher"), TEXT("0x1037b290") },
		{ TEXT("CNPC_VGuard1"), TEXT("0x1037e240") },
		{ TEXT("CNPC_VHengeyokai"), TEXT("0x1037fa70") },
		{ TEXT("CNPC_VHumanCombatant"), TEXT("0x10387140") },
		{ TEXT("CNPC_VHunter"), TEXT("0x10387140") },
		{ TEXT("CNPC_VManBat"), TEXT("0x1038b070") },
		{ TEXT("CNPC_VNewscaster"), TEXT("0x103a0420") },
		{ TEXT("CNPC_VPedestrian"), TEXT("0x103a2570") },
		{ TEXT("CNPC_VPlaceholder"), TEXT("0x103a4350") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103a4580") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103a6d40") },
		{ TEXT("CNPC_VSheriffMan"), TEXT("0x103ae6c0") },
		{ TEXT("CNPC_VTaxiDriver"), TEXT("0x103b35c0") },
		{ TEXT("CNPC_VTzimisce"), TEXT("0x103b91d0") },
		{ TEXT("CNPC_VTzimisceHeadClaw"), TEXT("0x103c1c80") },
		{ TEXT("CNPC_VVampireBoss"), TEXT("0x103c5840") },
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103caef0") },
		{ TEXT("CNPC_VWolfMorph"), TEXT("0x103dce00") },
		{ TEXT("CNPC_VYukie"), TEXT("0x103dd800") },
		{ TEXT("CNPC_VZombie"), TEXT("0x103defc0") },
	};
	for (const FRow& Row : Rows)
	{
		FLifecycle19Fixture F(Row.Cls);
		if (F.Npc == nullptr)
		{
			AddError(FString::Printf(TEXT("%s: no NPC"), Row.Cls));
			continue;
		}
		// The spawn's own `NPCInit` already left both words as asserted, so each takes its opposite
		// first: every body raises `DAT_10937cf1` on entry and must drop it, and writes MaxHealth.
		F.Npc->InNpcInit() = true;
		F.Npc->MaxHealth = 0;
		F.Npc->NPCInit();
		TestFalse(FString::Printf(TEXT("%s %s left DAT_10937cf1 set"), Row.Cls, Row.Addr),
			F.Npc->InNpcInit());
		TestEqual(FString::Printf(TEXT("%s MaxHealth 100 from the chain"), Row.Cls),
			F.Npc->MaxHealth, 100);
	}
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

#endif
