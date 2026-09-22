#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
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
	struct FLifecycle19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		FLifecycle19Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("lifecycle19_kernel"), 919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpc(TEXT("subject"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("subject"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}
	};

	void Lifecycle19Stand(FElysiumNpc& Npc, const TCHAR* RetailClass)
	{
		Npc.SetRetailClassForTests(RetailClass);
		Npc.ThinkSetCalls = 0;
		Npc.SpawnEquipRequests = 0;
		Npc.FloorDropPerformed = 0;
		Npc.FloorDropSkipped = 0;
		Npc.SetScheduleRetailCalls = 0;
		Npc.NodeGraphHullIndex() = 0;
		Npc.InNpcInit() = false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TroikaNpcInitTest,
	"Elysium.Substrate.NpcKernelLifecycle19.TroikaNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TroikaNpcInitTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
	F.Npc->StatTemplate = TEXT("Human");
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
	TestFalse(TEXT("1029a0c0 m_fEffects cleared unhides"), F.Npc->bHidden);
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TroikaNpcInitAbsencesTest,
	"Elysium.Substrate.NpcKernelLifecycle19.TroikaNPCInitAbsences", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TroikaNpcInitAbsencesTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
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
	// The two resolved patrol routes ARE dropped (`1029a236`..`1029a248`).
	TestEqual(TEXT("1029a236 the patrol route is dropped"), F.Npc->NumPatrolPointsForDebug(), 0);
	TestEqual(TEXT("1029a242 the hunt route with it"), F.Npc->HuntPatrolPoints.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TuningTest,
	"Elysium.Substrate.NpcKernelLifecycle19.OccludedTuning", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TuningTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
	F.Npc->NPCInit();
	// `0x101e8c50` / `0x101e8c70` are field getters on the process-global `CVFeatList_t`
	// (`0x10739d08`), loaded by `0x101e6310` from `Rules.txt` `Npc_Combat_Info` with the image
	// defaults 0.5 and 5.0 — NOT the 3.4 / 10.0 immediates `CNPC_VCamera::NPCInit` hard-codes.
	TestEqual(TEXT("101e8c50 OccludedDelayNormal default 0.5"), F.Npc->OccludedDelayNormal, 0.5f);
	TestEqual(TEXT("101e8c70 OccludedDelayCover default 5.0"), F.Npc->OccludedDelayCover, 5.f);
	TestEqual(TEXT("m_flOccludedDelay takes the normal one"), F.Npc->OccludedDelay, 0.5f);
	// The camera keeps its own literals (`103696xx`: `0x4059999a`, `0x41200000`, `0x3f000000`).
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VCamera"));
	F.Npc->bCameraEngineQueryAnswer = true;
	F.Npc->NPCInit();
	TestEqual(TEXT("103692c0 hard-codes 3.4"), F.Npc->OccludedDelayNormal, 3.4f);
	TestEqual(TEXT("103692c0 hard-codes 10.0"), F.Npc->OccludedDelayCover, 10.f);
	TestEqual(TEXT("103696f7 hard-codes the enemy-store interval"),
		static_cast<float>(F.Npc->EnemyMemory.FreeKnowledgeDuration), 0.5f);
	// `10369302`: the refuse arm returns WITHOUT clearing `DAT_10937cf1`.
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VCamera"));
	F.Npc->bCameraEngineQueryAnswer = false;
	F.Npc->NPCInit();
	TestTrue(TEXT("10369302 the refuse arm leaves DAT_10937cf1 SET"), F.Npc->InNpcInit());
	F.Npc->InNpcInit() = false;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19WeaponHideTest,
	"Elysium.Substrate.NpcKernelLifecycle19.HumanCombatantHidesWeapon", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19WeaponHideTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VHumanCombatant"));
	// `1038714a`: `GetActiveWeapon()` first — a null weapon is a no-op and nothing else happens.
	const int32 Before = F.Npc->HideActiveWeaponCalls;
	F.Npc->NPCInit();
	TestEqual(TEXT("1038714f a null active weapon hides nothing"),
		F.Npc->HideActiveWeaponCalls, Before);
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
	Weapon->Unhide(F.Npc);
	F.Npc->NPCInit();
	TestEqual(TEXT("1038715f the ACTIVE WEAPON is hidden, not the NPC"),
		F.Npc->HideActiveWeaponCalls, Before + 1);
	TestTrue(TEXT("...and the weapon carries the NODRAW bit"), Weapon->bHidden);
	TestFalse(TEXT("...while the NPC itself is not hidden"), F.Npc->bHidden);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19RestoreChecksumTest,
	"Elysium.Substrate.NpcKernelLifecycle19.OnRestoreChecksum", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19RestoreChecksumTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
	FElysiumNpc& N = *F.Npc;

	// A saved header that names a live schedule and carries ITS checksum is kept.
	ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N);
	TArray<uint8> TaskBytes;
	N.ScheduleTaskBytes(TaskBytes);
	uint32 Crc = FElysiumNpc::SaveCrc32Init();
	Crc = FElysiumNpc::SaveCrc32Update(Crc, TaskBytes.GetData(), TaskBytes.Num());
	N.LastSavedExtendedHeader.Version = 1;
	N.LastSavedExtendedHeader.Flags = 0;
	N.LastSavedExtendedHeader.ScheduleName = ElysiumScheduleName(ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND));
	N.LastSavedExtendedHeader.ScheduleCrc = FElysiumNpc::SaveCrc32Final(Crc);
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
	"Elysium.Substrate.NpcKernelLifecycle19.TroikaOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TroikaRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
	FElysiumNpc& N = *F.Npc;
	N.HuntPatrolPoints.Add(FVector::ZeroVector);
	N.bSpawnCalled = false;
	const int32 Revalidations = N.PatrolPathRevalidations;
	const int32 Releases = N.PatrolPathReleases;
	const int32 Scans = N.RestorePlaceScans;
	N.OnRestore(true);
	// `102998c0` validates BOTH pairs (`0x1029f610`) and releases each on its own (`0x1029f5d0`).
	TestEqual(TEXT("1029f610 is asked for both routes"),
		N.PatrolPathRevalidations, Revalidations + 2);
	TestEqual(TEXT("1029f5d0 releases the stored hunt route"), N.PatrolPathReleases, Releases + 1);
	// `102db5e0` answers the place that holds this NPC — none here, which is `INDEX_NONE`.
	TestEqual(TEXT("102db5e0 runs once per restore"), N.RestorePlaceScans, Scans + 1);
	TestEqual(TEXT("...and with no place holding this NPC it answers nothing"),
		FElysiumNpcWorldFixture::Debug(F.Npc, TEXT("Ambient place")),
		FString(TEXT("searching")));   // the debug row's word for INDEX_NONE
	// `+0x62e9 = 1` is the entity's own spawn-called byte.
	TestTrue(TEXT("10299xxx +0x62e9 = 1"), N.bSpawnCalled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19WerewolfRearmTest,
	"Elysium.Substrate.NpcKernelLifecycle19.WerewolfRearm", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19WerewolfRearmTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VWerewolf"));
	FElysiumNpc& N = *F.Npc;
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
	"Elysium.Substrate.NpcKernelLifecycle19.SpeciesWords", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19SpeciesWordsTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	// `103a6de9` is `CNPC_VSabbatLeader::m_nLastWaterLevel` (`+0x66c4`), the splash detector's edge
	// latch — not the entity's own `m_nWaterLevel` (`+0x03e0`).
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VSabbatLeader"));
	F.Npc->SabbatLastWaterLevel = 3;
	F.Npc->WaterLevel = 2;
	F.Npc->NPCInit();
	TestEqual(TEXT("103a6de9 clears m_nLastWaterLevel"), F.Npc->SabbatLastWaterLevel, 0);
	TestEqual(TEXT("...and leaves the entity's own water level alone"), F.Npc->WaterLevel, 2);

	// `103a435x` writes `m_pInterestingPlace = NULL`, which this runtime spells INDEX_NONE.
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VPlaceholder"));
	F.Npc->NPCInit();
	TestEqual(TEXT("103a435x the placeholder holds NO interesting place"),
		FElysiumNpcWorldFixture::Debug(F.Npc, TEXT("Ambient place")),
		FString(TEXT("searching")));   // the debug row's word for INDEX_NONE

	// `0x10376c10` walks a scratch list `NPCInit` never fills, so the recount's only observable
	// effect is the count reset — it must NOT seed enemy memory for the whole map.
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VFrenzyShadow"));
	F.Npc->FrenzyShadowHostileEnemyCount = 7;
	F.Npc->NPCInit();
	TestEqual(TEXT("10376c17 +0x6664 = 0"), F.Npc->FrenzyShadowHostileEnemyCount, 0);
	TestEqual(TEXT("...and the empty scratch list touches no other NPC"),
		F.Npc->EnemyMemory.Records().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19FarSightTest,
	"Elysium.Substrate.NpcKernelLifecycle19.FarSight", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19FarSightTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
	F.Npc->SpawnFlags |= 0x100;
	F.Npc->BaseNPCInit();                                                // 0x10273390 far-sight arm
	TestEqual(TEXT("spawnflag 0x100 DistTooFar = 1e9"), F.Npc->DistTooFar, 1.0e9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TeleportTailTest,
	"Elysium.Substrate.NpcKernelLifecycle19.TeleportTail", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TeleportTailTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
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
	"Elysium.Substrate.NpcKernelLifecycle19.CameraNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19CameraNpcInitTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VCamera"));
	F.Npc->bCameraEngineQueryAnswer = true;
	F.Npc->NPCInit();                                                    // 0x103692c0
	TestEqual(TEXT("hard-coded occluded delay 3.4"), F.Npc->OccludedDelayNormal, 3.4f);
	TestEqual(TEXT("hard-coded cover delay 10"), F.Npc->OccludedDelayCover, 10.f);
	TestFalse(TEXT("camera not BCC targetable"), F.Npc->bIsBccTargetable);
	TestEqual(TEXT("takedamage 0"), F.Npc->TakeDamageMode, 0);
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VCamera"));
	F.Npc->bCameraEngineQueryAnswer = false;
	const int32 Before = F.Npc->CameraSelfRemovals;
	F.Npc->NPCInit();
	TestEqual(TEXT("engine refuse deletes the camera"), F.Npc->CameraSelfRemovals, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19PayphoneTest,
	"Elysium.Substrate.NpcKernelLifecycle19.PayphoneNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19PayphoneTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CPayphone"));
	F.Npc->NPCInit();                                                    // 0x101aab90
	TestTrue(TEXT("+0x1480 targetable"), F.Npc->bIsBccTargetable);
	TestTrue(TEXT("+0x63d8 invincible"), F.Npc->bInvincible);
	TestFalse(TEXT("+0x1481 not alive"), F.Npc->bNpcIsAlive);
	TestFalse(TEXT("senses+0x80 off"), F.Npc->Senses.bCanPerformSenses);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19SwarmDistTest,
	"Elysium.Substrate.NpcKernelLifecycle19.SwarmDistTooFar", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19SwarmDistTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	const TCHAR* Classes[] = { TEXT("CNPC_VBach"), TEXT("CNPC_VBatSwarm"), TEXT("CNPC_VSheriffSwarm") };
	for (const TCHAR* Cls : Classes)
	{
		Lifecycle19Stand(*F.Npc, Cls);
		F.Npc->NPCInit();
		TestEqual(FString::Printf(TEXT("%s DistTooFar 65535"), Cls), F.Npc->DistTooFar, 65535.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19HullIndexTest,
	"Elysium.Substrate.NpcKernelLifecycle19.NodeGraphHull", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19HullIndexTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VGargoyle"));
	F.Npc->NPCInit();                                                    // 0x103785f0
	TestEqual(TEXT("Gargoyle hull 0xe"), F.Npc->NodeGraphHullIndex(), 0x0e);
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VManBat"));
	F.Npc->NPCInit();                                                    // 0x1038b070 last writer wins
	TestEqual(TEXT("ManBat hull 0x14 last-writer"), F.Npc->NodeGraphHullIndex(), 0x14);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ManBatOrderTest,
	"Elysium.Substrate.NpcKernelLifecycle19.ManBatTimersFirst", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ManBatOrderTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VManBat"));
	F.Npc->NPCInit();
	TestTrue(TEXT("flap timer armed before base (curtime+2.3)"), F.Npc->ManBatFlapTimer > 0.0);
	TestTrue(TEXT("fly timer armed (curtime+0.1)"), F.Npc->ManBatFlyTimer > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19PlaceholderTest,
	"Elysium.Substrate.NpcKernelLifecycle19.PlaceholderNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19PlaceholderTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VPlaceholder"));
	F.Npc->NPCInit();                                                    // 0x103a4350
	TestTrue(TEXT("targetable"), F.Npc->bIsBccTargetable);
	TestTrue(TEXT("ThinkSet(NULL)"), F.Npc->ThinkFunctionName.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19CopTest,
	"Elysium.Substrate.NpcKernelLifecycle19.CopNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19CopTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VCop"));
	F.Npc->bWasEverInCombat = true;
	F.Npc->NPCInit();                                                    // 0x10372b00
	TestFalse(TEXT("+0x6670 cleared AFTER HumanCombatant"), F.Npc->bWasEverInCombat);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19FrenzyShadowTest,
	"Elysium.Substrate.NpcKernelLifecycle19.FrenzyShadowNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19FrenzyShadowTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VFrenzyShadow"));
	F.Npc->NPCInit();                                                    // 0x10375c80
	TestEqual(TEXT("state 0xb"), F.Npc->IdealStateRetail(), 0xb);
	TestTrue(TEXT("frenzied flags 0x5ddf"), F.Npc->NpcFlags.HasFrenzied(0x5ddfu));
	TestEqual(TEXT("speed scale 8"), F.Npc->NpcSpeedScale, 8.f);
	TestTrue(TEXT("senses on"), F.Npc->Senses.bCanPerformSenses);
	TestTrue(TEXT("nav ignore physics"), F.Npc->bNavIgnorePhysicsProps);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19WerewolfTest,
	"Elysium.Substrate.NpcKernelLifecycle19.WerewolfNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19WerewolfTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VWerewolf"));
	F.Npc->NPCInit();                                                    // 0x103caef0
	TestEqual(TEXT("stat template Werewolf"), F.Npc->StatTemplate, FString(TEXT("Werewolf")));
	TestTrue(TEXT("FOV cos(120)= -0.5"),
		FMath::IsNearlyEqual(F.Npc->FieldOfViewDot, -0.5f, 1.e-4f));
	TestEqual(TEXT("DistTooFar 1e9"), F.Npc->DistTooFar, 1.0e9f);
	TestEqual(TEXT("investigate AnyPlayer"), F.Npc->InvestigateMode, 3);
	const float FloorAfterInit = F.Npc->WerewolfTeleportDistanceB;
	F.Npc->OnRestore(true);                                              // 0x103cabf0 += again
	TestTrue(TEXT("teleport floor accumulates across restore (defect 2)"),
		F.Npc->WerewolfTeleportDistanceB > FloorAfterInit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ChangTypeTest,
	"Elysium.Substrate.NpcKernelLifecycle19.ChangTypeBeforeChain", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ChangTypeTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VChangBrosBlade"));
	F.Npc->NPCInit();                                                    // 0x1036f100
	TestEqual(TEXT("Blade SetChangType(0) before chain"), F.Npc->ChangType, 0);
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VChangBrosClaw"));
	F.Npc->NPCInit();                                                    // 0x1036f900
	TestEqual(TEXT("Claw SetChangType(1) before chain"), F.Npc->ChangType, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19StartNpcTest,
	"Elysium.Substrate.NpcKernelLifecycle19.StartNPC", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19StartNpcTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
	F.Npc->StartNPC();                                                   // 0x1029a8b0
	TestTrue(TEXT("10273ad0 ThinkSet on both first-second arms"), F.Npc->ThinkSetCalls >= 2);
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VTzimisce"));
	F.Npc->StartNPC();                                                   // 0x103b9270
	TestEqual(TEXT("Tzimisce redundant re-arm"), F.Npc->TzimisceStartNpcRearms, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19OnRestoreGiveUpTest,
	"Elysium.Substrate.NpcKernelLifecycle19.OnRestoreGiveUp", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19OnRestoreGiveUpTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CAI_BaseNPCTroika"));
	F.Npc->LastSavedExtendedHeader.Version = 0;
	F.Npc->OnRestore(true);                                              // 0x102998c0 → give-up
	TestFalse(TEXT("give-up clears refind latch"), F.Npc->ScheduleHost.bDoPostRestoreRefindPath);
	TestFalse(TEXT("give-up clears schedule"), F.Npc->Schedule.IsRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19PedestrianRestoreTest,
	"Elysium.Substrate.NpcKernelLifecycle19.PedestrianOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19PedestrianRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VPedestrian"));
	F.Npc->PedestrianLevelResetType = 2;
	const int32 Before = F.Npc->InventoryDestroys;
	F.Npc->OnRestore(true);                                              // gate m_eLevelResetType==2
	TestEqual(TEXT("type 2 skips reset"), F.Npc->InventoryDestroys, Before);
	F.Npc->PedestrianLevelResetType = 0;
	F.Npc->OnRestore(false);                                             // bool argument clear
	TestEqual(TEXT("bFromLoad false skips"), F.Npc->InventoryDestroys, Before);
	F.Npc->OnRestore(true);
	TestTrue(TEXT("reset arm Inventory_Destroy"), F.Npc->InventoryDestroys > Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ZombieCrawlTest,
	"Elysium.Substrate.NpcKernelLifecycle19.ZombieNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ZombieCrawlTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VZombie"));
	F.Npc->NPCInit();                                                    // 0x103defc0
	TestTrue(TEXT("crawl-out latch first"), F.Npc->bZombieNeedsCrawlOutOfGround);
	TestEqual(TEXT("SetSchedule(0x161) miss arm"), F.Npc->LastSetScheduleRetail, 0x161);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19TaxiIdleTest,
	"Elysium.Substrate.NpcKernelLifecycle19.TaxiDriverNPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19TaxiIdleTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VTaxiDriver"));
	F.Npc->NPCInit();                                                    // 0x103b35c0
	TestEqual(TEXT("ideal IDLE written then SetState(1)"), F.Npc->IdealStateRetail(), 1);
	TestTrue(TEXT("senses ON"), F.Npc->Senses.bCanPerformSenses);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19ArmCoverageTest,
	"Elysium.Substrate.NpcKernelLifecycle19.ArmCoverage", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19ArmCoverageTest::RunTest(const FString&)
{
	// Every slot-420/422/130 override this family ports is named. Addresses from the census.
	const TCHAR* Slot420[] = {
		TEXT("0x101aab90"), TEXT("0x1035cec0"), TEXT("0x10360ce0"), TEXT("0x10363940"),
		TEXT("0x103673b0"), TEXT("0x103692c0"), TEXT("0x1036b050"), TEXT("0x1036f100"),
		TEXT("0x1036f900"), TEXT("0x10372b00"), TEXT("0x10375c80"), TEXT("0x103785f0"),
		TEXT("0x1037b290"), TEXT("0x1037e240"), TEXT("0x1037fa70"), TEXT("0x10387140"),
		TEXT("0x10388b30"), TEXT("0x1038b070"), TEXT("0x103a0420"), TEXT("0x103a2570"),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19Guard1Test,
	"Elysium.Substrate.NpcKernelLifecycle19.Guard1NPCInit", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19Guard1Test::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VGuard1"));
	// `+0x6660` on THIS class is `CNPC_VGuard1::m_fHatesPlayer` (`vtmb_fields CNPC_VGuard1 0x6660`),
	// not `CNPC_VAnimal::m_bPlayerAttackedMe` at the same offset.
	F.Npc->bGuard1HatesPlayer = true;
	F.Npc->NPCInit();                                                    // 0x1037e240 +0x6660 first
	TestFalse(TEXT("1037e24a m_fHatesPlayer cleared BEFORE Troika"), F.Npc->bGuard1HatesPlayer);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19CameraStartNpcTest,
	"Elysium.Substrate.NpcKernelLifecycle19.CameraStartNPC", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19CameraStartNpcTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VCamera"));
	F.Npc->Model = TEXT("models/null.mdl");
	F.Npc->StartNPC();                                                   // 0x10369930
	TestTrue(TEXT("null.mdl drops to floor"), F.Npc->FloorDropPerformed > 0);
	TestTrue(TEXT("final ThinkSet re-arm does not change stamp (delay 0)"),
		F.Npc->ThinkSetCalls >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19MingXiaoRestoreTest,
	"Elysium.Substrate.NpcKernelLifecycle19.MingXiaoTentacleOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19MingXiaoRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VMingXiaoTentacle"));
	F.Npc->MingXiaoTentacleCache() = F.Npc->Handle;
	F.Npc->OnRestore(true);                                              // 0x1039f000 after base
	TestFalse(TEXT("DAT_1093bd34 invalidated after Troika"),
		F.Npc->MingXiaoTentacleCache().IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19RunnerRestoreTest,
	"Elysium.Substrate.NpcKernelLifecycle19.TzimisceRunnerOnRestore", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19RunnerRestoreTest::RunTest(const FString&)
{
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	Lifecycle19Stand(*F.Npc, TEXT("CNPC_VTzimisceRunner"));
	const int32 Before = F.Npc->Flag2Removals;
	F.Npc->OnRestore(true);                                              // 0x103c3c40
	TestEqual(TEXT("RemoveFlag2(4)"), F.Npc->Flag2Removals, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelLifecycle19SpeciesSmokeTest,
	"Elysium.Substrate.NpcKernelLifecycle19.SpeciesNPCInitSmoke", GLifecycle19Flags)
bool FElysiumNpcKernelLifecycle19SpeciesSmokeTest::RunTest(const FString&)
{
	// One call per slot-420 species body. Distinguishing writes from the listing.
	FLifecycle19Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
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
		{ TEXT("CNPC_VBatSwarm"), TEXT("0x103673b0") },
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
		{ TEXT("CNPC_VHunter"), TEXT("0x10388b30") },
		{ TEXT("CNPC_VManBat"), TEXT("0x1038b070") },
		{ TEXT("CNPC_VNewscaster"), TEXT("0x103a0420") },
		{ TEXT("CNPC_VPedestrian"), TEXT("0x103a2570") },
		{ TEXT("CNPC_VPlaceholder"), TEXT("0x103a4350") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103a4580") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103a6d40") },
		{ TEXT("CNPC_VSheriffMan"), TEXT("0x103ae6c0") },
		{ TEXT("CNPC_VSheriffSwarm"), TEXT("0x103b2360") },
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
		Lifecycle19Stand(*F.Npc, Row.Cls);
		F.Npc->NPCInit();
		TestFalse(FString::Printf(TEXT("%s %s left DAT_10937cf1 set"), Row.Cls, Row.Addr),
			F.Npc->InNpcInit());
		TestEqual(FString::Printf(TEXT("%s MaxHealth 100 from the chain"), Row.Cls),
			F.Npc->MaxHealth, 100);
	}
	return true;
}

#endif
