#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Spec 0002 V4o, lane O2 — the move-and-shoot overlay `CAI_MoveAndShootOverlay (+0x5cf4)`: slot 575
// `0x102bf4a0`, the arm `0x102e8270`, `0x102e83e0`, `0x102e84a0` and the run `0x102e8560`. Every
// assertion names the address it was read at (`docs/vtmb/animation_events.md` "The move-and-shoot
// overlay, arm by arm").

static constexpr EAutomationTestFlags GElysiumNpcKernelMoveAndShootFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Prefixed because the module builds adaptive-unity and this anonymous namespace is merged with
	// the other suites'. The classnames are retail's own.
	const TCHAR* const GMoveShootFists = TEXT("item_w_fists");
	const TCHAR* const GMoveShootKnife = TEXT("item_w_knife");
	const TCHAR* const GMoveShootGun = TEXT("item_w_mac_10");

	constexpr float GMoveShootAttackRate = 0.8f;

	EElysiumNpcCond MoveShootCond(int32 Retail)
	{
		return static_cast<EElysiumNpcCond>(Retail);
	}

	FElysiumWeaponMode MoveShootMode(const TCHAR* Dmg, float Range)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = Dmg;
		Mode.AttackRate = GMoveShootAttackRate;
		Mode.Range = Range;
		return Mode;
	}

	FElysiumItemTable MakeMoveShootItemTable()
	{
		FElysiumItemTable Table;

		FElysiumItemDef Fists;
		Fists.Classname = GMoveShootFists;
		Fists.PrintName = TEXT("Fists");
		Fists.Type = EElysiumItemType::WeaponMelee;
		Fists.bHidden = true;
		Fists.Modes.Add(MoveShootMode(TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"), 0.f));
		Table.Items.Add(MoveTemp(Fists));

		FElysiumItemDef Knife;
		Knife.Classname = GMoveShootKnife;
		Knife.PrintName = TEXT("Knife");
		Knife.Type = EElysiumItemType::WeaponMelee;
		Knife.Modes.Add(MoveShootMode(TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 0.f));
		Table.Items.Add(MoveTemp(Knife));

		// The MAC-10's own burst pair (`vdata/items/item_w_mac_10.txt`: `BurstMin 3`, `BurstMax 5`).
		FElysiumItemDef Gun;
		Gun.Classname = GMoveShootGun;
		Gun.PrintName = TEXT("MAC-10");
		Gun.Type = EElysiumItemType::WeaponFirearm;
		Gun.AmmoType = TEXT("MoveShootRound");
		Gun.MagazineSize = 30;
		Gun.DefaultAmmo = 30;
		FElysiumWeaponMode GunMode = MoveShootMode(TEXT("4 Lethal Ranged_Combat DMG_BULLET"), 2000.f);
		GunMode.AmmoCost = 1;
		GunMode.BurstMin = 3;
		GunMode.BurstMax = 5;
		Gun.Modes.Add(MoveTemp(GunMode));
		Table.Items.Add(MoveTemp(Gun));

		Table.Reindex();
		return Table;
	}

	struct FMoveShootFixture
	{
		FElysiumItemTable Items;
		bool bInstalled = false;
		FElysiumNpcWorldFixture Fixture;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__move_and_shoot_test__"), 0x4D4F5653);
			Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
			// A model key: a body with none builds no visual (`InstallPreparedCharacterVisual`), and
			// the sequence bridge answers -1 for every activity on a body with no visual.
			FElysiumEntityDef& GuardDef =
				Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, TEXT("CNPC_VHumanCombatant"));
			GuardDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
			Builder.AddNpc(TEXT("other"), FVector(400.0, 0.0, 0.0), TEXT("npc_VHumanCombatant"));
			return Builder;
		}

		FMoveShootFixture()
			: Items(MakeMoveShootItemTable())
			, Fixture(BuildWorld(), [this](FElysiumRecordingServices& Services)
				{
					ElysiumItems::Install(Items);
					bInstalled = true;
					// The body authors a sequence for every activity asked (`0x11`, `0x15`, `0x1a`,
					// `0x47`, `0x48`); a case that needs a miss turns this off and drops the cache.
					Services.bNpcActivitiesResolve = true;
					int32 FixtureActivityIndex = 10;
					for (const TCHAR* ActivityName : {TEXT("ACT_WALK"), TEXT("ACT_RUN"), TEXT("ACT_WALK_AIM"), TEXT("ACT_RUN_AIM"), TEXT("ACT_RANGE_ATTACK1_LAYER"), TEXT("ACT_LOOKBACK_LEFT"), TEXT("ACT_LOOKBACK_RIGHT")})
						Services.SeedFixtureActivity(ActivityName, FixtureActivityIndex++);
				})
		{
			Guard = Fixture.Npc(TEXT("guard"));
			Other = Fixture.Npc(TEXT("other"));
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
			ElysiumNpcTunables::ResetConVars();
			if (Guard != nullptr)
			{
				// Empty-handed: the loadout's first think hands a body with no authored equipment
				// `item_w_fists`, and each case arms what it wants.
				Guard->Inventory.Holster(*Guard);
				while (Guard->Inventory.Num() > 0)
				{
					FElysiumItem* const Carried = Guard->Inventory.At(*Guard, 0);
					if (Carried == nullptr)
					{
						break;
					}
					Guard->Inventory.Detach(*Guard, *Carried);
					Carried->Kill();
				}
			}
		}

		~FMoveShootFixture()
		{
			ElysiumNpcTunables::ResetConVars();
			if (bInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		FMoveShootFixture(const FMoveShootFixture&) = delete;
		FMoveShootFixture& operator=(const FMoveShootFixture&) = delete;

		bool Valid() const { return Guard != nullptr && Other != nullptr; }

		float Now() const { return static_cast<float>(Fixture.World.NowSeconds()); }

		FElysiumItem* Arm(const TCHAR* Classname)
		{
			const FElysiumEntityHandle Handle =
				Guard->Inventory.GiveNamedItem(*Guard, FString(Classname));
			FElysiumEntity* const Entity = Fixture.World.Resolve(Handle);
			FElysiumItem* const Item = Entity != nullptr ? Entity->AsItem() : nullptr;
			if (Item != nullptr)
			{
				Guard->Inventory.SetActiveWeapon(*Guard, *Item);
			}
			// The resolver's answers are cached on the weapon among other words.
			return Item;
		}

		// The five conditions `0x102e83e0` reads besides `0x4f`, and `0x4f`, all clear.
		void ClearAimConditions()
		{
			for (const int32 Retail : { 0x4f, 0x58, 0x60, 0x55, 0x48, 0x40 })
			{
				Guard->Cognition.Conditions.Clear(MoveShootCond(Retail));
			}
		}

		// A layer of the kernel's table held for `Activity` (`m_flWeight != 0`, `m_nActivity`).
		bool HoldsLayer(int32 Activity) const
		{
			for (const FElysiumNpc::FAnimOverlayLayer& Layer : Guard->AnimOverlay)
			{
				if (Layer.Weight != 0.f && Layer.Activity == Activity)
				{
					return true;
				}
			}
			return false;
		}

		void FreeLayers()
		{
			for (FElysiumNpc::FAnimOverlayLayer& Layer : Guard->AnimOverlay)
			{
				Layer = FElysiumNpc::FAnimOverlayLayer();
			}
		}

		// A running body with a live enemy and an active goal, its conditions gathered this think.
		void StageRun()
		{
			Guard->BaseMemory.Enemy = Other->Handle;
			Guard->Navigator.bHasHeadWaypoint = true;               // IsGoalActive 0x102ee6a0
			Guard->Navigator.MovementActivity = 0x13;               // ACT_RUN
			Guard->Cognition.GatheredAt = Fixture.World.NowSeconds();   // m_bConditionsGathered +0x5ca4 set
			ClearAimConditions();
			Guard->FacingTargetRequests.Reset();
			Guard->WeaponActivityRequests.Reset();
			FreeLayers();
		}
	};
}

// =================================================================================================
// Slot 575 `ShouldMoveAndShoot` — `0x102bf4a0` (and the base rung `0x10278c60`).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMoveAndShootSlot575Test,
	"Elysium.Arm.NpcKernelMoveAndShoot.Slot575", GElysiumNpcKernelMoveAndShootFlags)
bool FElysiumNpcKernelMoveAndShootSlot575Test::RunTest(const FString&)
{
	FMoveShootFixture F;
	if (!TestTrue(TEXT("the fixture stands"), F.Valid()))
	{
		return false;
	}
	F.Guard->BaseMemory.Enemy = F.Other->Handle;
	F.Guard->NpcFlags.Set(EElysiumNpcFlag2::MOVE_FACE_ENEMY);       // flags2 0x400
	F.Guard->CapabilityWord |= (1 << 6);                           // bits_CAP_MOVE_SHOOT

	TestFalse(TEXT("0x102bf4a0 no active weapon: false"), F.Guard->ShouldMoveAndShoot());

	TestNotNull(TEXT("the knife arms"), F.Arm(GMoveShootKnife));
	TestEqual(TEXT("a melee word carries no 0x6000 bit"),
		static_cast<int32>(F.Guard->SelectActiveWeaponWord() & 0x6000u), 0);
	TestFalse(TEXT("0x102bf4a0 a melee weapon (+0x5a0 & 0x6000 == 0): false"),
		F.Guard->ShouldMoveAndShoot());

	TestNotNull(TEXT("the gun arms"), F.Arm(GMoveShootGun));
	TestTrue(TEXT("the ranged word carries a 0x6000 bit"),
		(F.Guard->SelectActiveWeaponWord() & 0x6000u) != 0);
	TestTrue(TEXT("0x102bf4a0 a 0x6000 weapon read through SelectActiveWeaponWord passes"),
		F.Guard->ShouldMoveAndShoot());

	F.Guard->CapabilityWord &= ~(1 << 6);
	TestFalse(TEXT("0x10278c60 without bits_CAP_MOVE_SHOOT the base rung answers false"),
		F.Guard->ShouldMoveAndShoot());
	F.Guard->CapabilityWord |= (1 << 6);

	F.Guard->NpcFlags.Clear(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	TestFalse(TEXT("0x102bf4a0 without MOVE_FACE_ENEMY (flags2 0x400): false"),
		F.Guard->ShouldMoveAndShoot());
	F.Guard->NpcFlags.Set(EElysiumNpcFlag2::MOVE_FACE_ENEMY);

	F.Guard->BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("0x102bf4a0 without an enemy: false"), F.Guard->ShouldMoveAndShoot());
	return true;
}

// =================================================================================================
// `0x102e8270` — the arm.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMoveAndShootArmTest,
	"Elysium.Arm.NpcKernelMoveAndShoot.Arm", GElysiumNpcKernelMoveAndShootFlags)
bool FElysiumNpcKernelMoveAndShootArmTest::RunTest(const FString&)
{
	FMoveShootFixture F;
	if (!TestTrue(TEXT("the fixture stands"), F.Valid()))
	{
		return false;
	}
	ElysiumRng::SeedAll(0x4D4F5653);
	FElysiumNpcBase::FMoveAndShootOverlay& Overlay = F.Guard->MoveAndShootOverlay;

	// No active weapon: the disable `0x102e8250`.
	Overlay.NextShotTime = 12.f;
	F.Guard->ArmMoveAndShootOverlay(1.f, 2.f);
	TestEqual(TEXT("0x102e8270 no active weapon -> 0x102e8250 (+0x18 = FLT_MAX)"),
		Overlay.NextShotTime, MAX_flt);

	// A weapon, but no sequence for the translated 0x11 / 0x15: the disable, and no draw.
	TestNotNull(TEXT("the gun arms"), F.Arm(GMoveShootGun));
	F.Fixture.Services.bNpcActivitiesResolve = false;
	F.Fixture.Services.BodyClipsByRawIndex.Reset();
	Overlay.NextShotTime = 12.f;
	const int32 SeedBeforeRefusal = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed();
	F.Guard->ArmMoveAndShootOverlay(1.f, 2.f);
	TestEqual(TEXT("0x102e8270 SelectHeaviestSequence(TranslateActivity(0x11)) < 0 -> the disable"),
		Overlay.NextShotTime, MAX_flt);
	TestEqual(TEXT("...and the refusal takes no RandomInt draw"),
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed(), SeedBeforeRefusal);

	// Both sequences authored: the arm.
	F.Fixture.Services.bNpcActivitiesResolve = true;
	F.Fixture.Services.SeedFixtureActivity(TEXT("ACT_WALK"), 8); // 0x103854f0 de-aims 0x11
	F.Fixture.Services.SeedFixtureActivity(TEXT("ACT_RUN"), 9); // 0x103854f0 de-aims 0x15
	int32 FixtureActivityIndex = 10;
	for (const TCHAR* ActivityName : {TEXT("ACT_WALK_AIM"), TEXT("ACT_RUN_AIM"), TEXT("ACT_RANGE_ATTACK1_LAYER"), TEXT("ACT_LOOKBACK_LEFT"), TEXT("ACT_LOOKBACK_RIGHT")})
		F.Fixture.Services.SeedFixtureActivity(ActivityName, FixtureActivityIndex++);
	FRandomStream Probe = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const int32 ExpectedShots = Probe.RandRange(3, 5);
	Overlay.InitialDelay = 0.f;                                    // 0x1027c300 leaves 0
	F.Guard->ArmMoveAndShootOverlay(1.f, 2.f);
	TestEqual(TEXT("0x102e8270 m_minBurst (+0x1c) = weapon data +0x3a4"), Overlay.MinBurst, 3);
	TestEqual(TEXT("0x102e8270 m_maxBurst (+0x20) = weapon data +0x3a8"), Overlay.MaxBurst, 5);
	TestEqual(TEXT("0x102e8270 m_minPause (+0x24) = the first argument"), Overlay.PauseMin, 1.f);
	TestEqual(TEXT("0x102e8270 m_maxPause (+0x28) = the second argument"), Overlay.PauseMax, 2.f);
	TestEqual(TEXT("0x102e8270 m_nMoveShots (+0x14) = RandomInt(min, max) on the NpcSchedule stream"),
		Overlay.MoveShots, ExpectedShots);
	TestEqual(TEXT("...one draw"),
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed(), Probe.GetCurrentSeed());
	TestEqual(TEXT("0x102e8270 +0x18 = curtime + m_initialDelay (0)"),
		Overlay.NextShotTime, F.Now(), 1.e-4f);

	Overlay.InitialDelay = 0.5f;
	F.Guard->ArmMoveAndShootOverlay(1.f, 2.f);
	TestEqual(TEXT("0x102e8270 +0x18 = curtime + m_initialDelay (+0x2c)"),
		Overlay.NextShotTime, F.Now() + 0.5f, 1.e-4f);
	return true;
}

// =================================================================================================
// `0x102e83e0` — can it aim.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMoveAndShootCanAimTest,
	"Elysium.Arm.NpcKernelMoveAndShoot.CanAim", GElysiumNpcKernelMoveAndShootFlags)
bool FElysiumNpcKernelMoveAndShootCanAimTest::RunTest(const FString&)
{
	FMoveShootFixture F;
	if (!TestTrue(TEXT("the fixture stands"), F.Valid()))
	{
		return false;
	}
	TestNotNull(TEXT("the gun arms"), F.Arm(GMoveShootGun));
	FElysiumNpcConditions& Conditions = F.Guard->Cognition.Conditions;
	F.Guard->BaseMemory.Enemy = F.Other->Handle;

	// Gathered (`m_bConditionsGathered +0x5ca4` set): the condition table alone, slot 481 not run.
	F.Guard->Cognition.GatheredAt = F.Fixture.World.NowSeconds();
	F.ClearAimConditions();
	Conditions.Clear(EElysiumNpcCond::HaveEnemyLos);
	TestTrue(TEXT("0x102e83e0 none of 0x58 / 0x60 / 0x55 / 0x48 / 0x40: true without 0x4f"),
		F.Guard->CanAimAtEnemy());
	TestFalse(TEXT("0x102e83e0 gathered: slot 481 is not run (neither LOS word written)"),
		Conditions.Has(EElysiumNpcCond::HaveEnemyLos) || Conditions.Has(EElysiumNpcCond::EnemyOccluded));
	for (const int32 Blocking : { 0x58, 0x60, 0x55, 0x48, 0x40 })
	{
		F.ClearAimConditions();
		Conditions.Set(MoveShootCond(Blocking));
		TestFalse(*FString::Printf(TEXT("0x102e83e0 COND 0x%x without 0x4f: false"), Blocking),
			F.Guard->CanAimAtEnemy());
		Conditions.Set(MoveShootCond(0x4f));
		TestTrue(*FString::Printf(TEXT("0x102e83e0 COND 0x4f answers true over 0x%x"), Blocking),
			F.Guard->CanAimAtEnemy());
	}

	// Ungathered (`RunAI 0x1026f110` cleared the byte): slot 481 `GatherEnemyConditions 0x10270b20`
	// runs first -- it always writes one of `HAVE_ENEMY_LOS 0x4a` / `ENEMY_OCCLUDED 0x48` -- and a
	// stale `0x4f` on an enemy no weapon reaches is gone before the body reads it.
	F.ClearAimConditions();
	Conditions.Clear(EElysiumNpcCond::HaveEnemyLos);
	F.Other->Origin = F.Guard->Origin + FVector(4000000.0, 0.0, 0.0);
	Conditions.Set(MoveShootCond(0x4f));
	F.Guard->Cognition.GatheredAt = -1.0;
	const bool bAnswer = F.Guard->CanAimAtEnemy();
	TestTrue(TEXT("0x102e83e0 ungathered: slot 481 ran (one LOS word written)"),
		Conditions.Has(EElysiumNpcCond::HaveEnemyLos) || Conditions.Has(EElysiumNpcCond::EnemyOccluded));
	TestFalse(TEXT("0x102e83e0 the gather runs BEFORE 0x4f is read: the stale bit is gone"),
		Conditions.Has(MoveShootCond(0x4f)));
	const bool bBlocked = Conditions.Has(MoveShootCond(0x58)) || Conditions.Has(MoveShootCond(0x60))
		|| Conditions.Has(MoveShootCond(0x55)) || Conditions.Has(MoveShootCond(0x48))
		|| Conditions.Has(MoveShootCond(0x40));
	TestEqual(TEXT("0x102e83e0 the answer is read off the gathered set"), bAnswer, !bBlocked);
	return true;
}

// =================================================================================================
// `0x102e84a0` — the navigator's movement activity.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMoveAndShootMoveActivityTest,
	"Elysium.Arm.NpcKernelMoveAndShoot.MoveActivity", GElysiumNpcKernelMoveAndShootFlags)
bool FElysiumNpcKernelMoveAndShootMoveActivityTest::RunTest(const FString&)
{
	FMoveShootFixture F;
	if (!TestTrue(TEXT("the fixture stands"), F.Valid()))
	{
		return false;
	}
	FElysiumNpcBase::FMoveAndShootOverlay& Overlay = F.Guard->MoveAndShootOverlay;
	const float Now = F.Now();
	const float Floor = Now + 0.3f;                                // double 0x1047b868

	struct FSwap
	{
		bool bCanAim;
		int32 From;
		int32 To;
	};
	const FSwap Swaps[] = {
		{ true, 9, 0x11 }, { true, 0x13, 0x15 }, { false, 0x11, 9 }, { false, 0x15, 0x13 },
	};
	for (const FSwap& Swap : Swaps)
	{
		F.Guard->Navigator.MovementActivity = Swap.From;
		Overlay.NextShotTime = Now;
		F.Guard->UpdateMoveShootActivity(Swap.bCanAim);
		TestEqual(*FString::Printf(TEXT("0x102e84a0 can=%d: 0x%x -> 0x%x through 0x102ee250"),
			Swap.bCanAim ? 1 : 0, Swap.From, Swap.To),
			F.Guard->Navigator.GetMovementActivity(), Swap.To);
		TestEqual(TEXT("0x102e84a0 a swap floors +0x18 at curtime + 0.3"),
			Overlay.NextShotTime, Floor, 1.e-4f);
	}

	// A clock already past the floor is kept (`fVar1 <= +0x18`).
	F.Guard->Navigator.MovementActivity = 0x13;
	Overlay.NextShotTime = Now + 5.f;
	F.Guard->UpdateMoveShootActivity(true);
	TestEqual(TEXT("0x102e84a0 the swap still lands"), F.Guard->Navigator.GetMovementActivity(), 0x15);
	TestEqual(TEXT("0x102e84a0 +0x18 = max(+0x18, curtime + 0.3)"),
		Overlay.NextShotTime, Now + 5.f, 1.e-4f);

	// Any other activity, and the twin it already holds: nothing written.
	const FSwap Idles[] = {
		{ true, 1, 1 }, { false, 1, 1 }, { true, 0x11, 0x11 }, { true, 0x15, 0x15 },
		{ false, 9, 9 }, { false, 0x13, 0x13 },
	};
	for (const FSwap& Idle : Idles)
	{
		F.Guard->Navigator.MovementActivity = Idle.From;
		Overlay.NextShotTime = Now;
		F.Guard->UpdateMoveShootActivity(Idle.bCanAim);
		TestEqual(*FString::Printf(TEXT("0x102e84a0 can=%d: 0x%x is left alone"),
			Idle.bCanAim ? 1 : 0, Idle.From), F.Guard->Navigator.GetMovementActivity(), Idle.To);
		TestEqual(TEXT("0x102e84a0 ...and +0x18 is not written"), Overlay.NextShotTime, Now, 1.e-4f);
	}
	return true;
}

// =================================================================================================
// `0x102e8560` — the run, step by step.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMoveAndShootRunTest,
	"Elysium.Arm.NpcKernelMoveAndShoot.Run", GElysiumNpcKernelMoveAndShootFlags)
bool FElysiumNpcKernelMoveAndShootRunTest::RunTest(const FString&)
{
	FMoveShootFixture F;
	if (!TestTrue(TEXT("the fixture stands"), F.Valid()))
	{
		return false;
	}
	ElysiumRng::SeedAll(0x52554E31);
	TestNotNull(TEXT("the gun arms"), F.Arm(GMoveShootGun));
	FElysiumNpcBase::FMoveAndShootOverlay& Overlay = F.Guard->MoveAndShootOverlay;
	FElysiumNpcConditions& Conditions = F.Guard->Cognition.Conditions;
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const float Now = F.Now();

	// 1. Disabled (`+0x18 == FLT_MAX`): nothing, not even the tail.
	F.StageRun();
	F.Guard->DisableMoveAndShootOverlay();
	F.Guard->RunMoveAndShootOverlay();
	TestEqual(TEXT("0x102e8560 step 1: FLT_MAX returns before the tail"),
		F.Guard->FacingTargetRequests.Num(), 0);
	TestEqual(TEXT("0x102e8560 step 1: the movement activity is not touched"),
		F.Guard->Navigator.GetMovementActivity(), 0x13);

	// 3. Armed, an enemy, no active goal (`IsGoalActive 0x102ee6a0` false): return, no tail.
	F.Guard->ArmMoveAndShootOverlay(1.f, 2.f);
	TestTrue(TEXT("the overlay arms"), Overlay.NextShotTime != MAX_flt);
	F.StageRun();
	F.Guard->Navigator.bHasHeadWaypoint = false;
	F.Guard->RunMoveAndShootOverlay();
	TestEqual(TEXT("0x102e8560 step 3: no active goal returns before the tail"),
		F.Guard->FacingTargetRequests.Num(), 0);
	TestEqual(TEXT("0x102e8560 step 3: ...and before 0x102e84a0"),
		F.Guard->Navigator.GetMovementActivity(), 0x13);

	// 6. Cannot aim (`ENEMY_TOO_FAR 0x55`): the state byte is cleared, no swap from 0x13, no tail.
	F.StageRun();
	Conditions.Set(MoveShootCond(0x55));
	Overlay.bMovingAndShooting = true;
	F.Guard->RunMoveAndShootOverlay();
	TestFalse(TEXT("0x102e8560 step 6: m_bMovingAndShooting (+0x10) cleared"), Overlay.bMovingAndShooting);
	TestEqual(TEXT("0x102e8560 step 6: the cannot-aim exit has no tail"),
		F.Guard->FacingTargetRequests.Num(), 0);
	TestEqual(TEXT("0x102e84a0 cannot aim leaves ACT_RUN"), F.Guard->Navigator.GetMovementActivity(), 0x13);

	// 5, 7, 9. Can aim, no `0x4f`: the aim twin, the 0.3 s floor, the tail; the gesture gate is
	// closed by `debug_allow_mf_turn` "0" though `|move_yaw| > 90` -- no gesture and NO draw.
	F.StageRun();
	FElysiumNpc::FPoseParameterWrite MoveYaw;
	MoveYaw.Name = TEXT("move_yaw");
	MoveYaw.Value = -175.f;
	F.Guard->PoseParameterWrites.Add(MoveYaw);
	Overlay.NextShotTime = Now;
	Overlay.bMovingAndShooting = false;
	const double LastAttackBefore = -5.0;
	F.Guard->LastAttackTime = LastAttackBefore;
	const int32 SeedBefore = Stream.GetCurrentSeed();
	F.Guard->RunMoveAndShootOverlay();
	TestEqual(TEXT("0x102e84a0 can aim: ACT_RUN 0x13 -> 0x15"), F.Guard->Navigator.GetMovementActivity(), 0x15);
	TestEqual(TEXT("0x102e84a0 +0x18 floored at curtime + 0.3"), Overlay.NextShotTime, Now + 0.3f, 1.e-4f);
	TestEqual(TEXT("0x102e8626 debug_allow_mf_turn \"0\": no RandomInt draw is taken"),
		Stream.GetCurrentSeed(), SeedBefore);
	TestFalse(TEXT("0x102e8626 ...and no gesture is pushed"), F.HoldsLayer(0x47) || F.HoldsLayer(0x48));
	TestFalse(TEXT("0x102e8560 step 8: no shot without COND 0x4f"), F.HoldsLayer(0x1a));
	if (TestEqual(TEXT("0x102e8560 step 9: the tail queues one facing target"),
		F.Guard->FacingTargetRequests.Num(), 1))
	{
		const FElysiumNpcBase::FFacingTargetRequest& Request = F.Guard->FacingTargetRequests[0];
		TestEqual(TEXT("slot 517 reaches motor slot 12"), Request.MotorSlot, 12);
		TestTrue(TEXT("slot 517 carries GetEnemy()"), Request.Target == F.Other->Handle);
		TestEqual(TEXT("slot 517 importance 1.0 (0x3f800000)"), Request.Importance, 1.0f);
		TestEqual(TEXT("slot 517 duration 0.8 (0x3f4ccccd)"), Request.Duration, 0.8f);
		TestEqual(TEXT("slot 517 ramp 0"), Request.Ramp, 0.f);
	}

	// 8. `0x4f` but the clock not due (`+0x18 > curtime`): no shot, the tail still.
	Conditions.Set(MoveShootCond(0x4f));
	Overlay.MoveShots = 1;
	F.Guard->RunMoveAndShootOverlay();
	TestEqual(TEXT("0x102e8560 step 8: the clock not due keeps m_nMoveShots"), Overlay.MoveShots, 1);
	TestEqual(TEXT("0x102e8560 step 8: ...and m_flLastAttackTime"), F.Guard->LastAttackTime, LastAttackBefore);
	TestEqual(TEXT("0x102e8560 step 8: ...and takes no draw"), Stream.GetCurrentSeed(), SeedBefore);
	TestEqual(TEXT("0x102e8560 step 9: the tail runs again"), F.Guard->FacingTargetRequests.Num(), 2);

	// 8. Due, a shot left in the burst: the shot.
	Overlay.NextShotTime = Now - 0.01f;
	F.Guard->RunMoveAndShootOverlay();
	TestTrue(TEXT("0x102e8560 step 8: m_bMovingAndShooting (+0x10) set (slot 557 answers 1)"),
		Overlay.bMovingAndShooting);
	TestEqual(TEXT("0x102e8560 step 8: --m_nMoveShots"), Overlay.MoveShots, 0);
	TestEqual(TEXT("0x102e8560 step 8: m_flLastAttackTime (+0x5d9c) = curtime"),
		F.Guard->LastAttackTime, static_cast<double>(Now), 1.e-4);
	TestTrue(TEXT("0x100991b0 AddGesture(TranslateActivity(0x1a), 1) holds a layer"), F.HoldsLayer(0x1a));
	if (TestEqual(TEXT("0x1032a910 Weapon_SetActivity called once"),
		F.Guard->WeaponActivityRequests.Num(), 1))
	{
		TestEqual(TEXT("0x1032a910 ...with duration 0"), F.Guard->WeaponActivityRequests[0].Duration, 0.f);
	}
	TestEqual(TEXT("0x102e8560 step 8: +0x18 = curtime + slot 332 (Attack_Rate) - 0.1"),
		Overlay.NextShotTime, Now + GMoveShootAttackRate - 0.1f, 1.e-4f);
	TestEqual(TEXT("0x102e8560 step 9: the tail after the shot"), F.Guard->FacingTargetRequests.Num(), 3);

	// 8. Due, the burst spent (`--m_nMoveShots < 0`): the re-arm draws, the pause, no shot.
	Overlay.NextShotTime = Now - 0.01f;
	Overlay.MinBurst = 2;
	Overlay.MaxBurst = 4;
	Overlay.PauseMin = 1.f;
	Overlay.PauseMax = 2.f;
	F.FreeLayers();
	FRandomStream Probe = Stream;
	const int32 ExpectedShots = Probe.RandRange(2, 4);
	const float ExpectedPause = Probe.FRandRange(1.f, 2.f);
	F.Guard->LastAttackTime = LastAttackBefore;
	F.Guard->RunMoveAndShootOverlay();
	TestEqual(TEXT("0x102e8560 step 8: m_nMoveShots = RandomInt(m_minBurst, m_maxBurst)"),
		Overlay.MoveShots, ExpectedShots);
	TestEqual(TEXT("0x102e8560 step 8: +0x18 = curtime + RandomFloat(m_minPause, m_maxPause)"),
		Overlay.NextShotTime, Now + ExpectedPause, 1.e-4f);
	TestEqual(TEXT("0x102e8560 step 8: the two draws, in that order, on the NpcSchedule stream"),
		Stream.GetCurrentSeed(), Probe.GetCurrentSeed());
	TestFalse(TEXT("0x102e8560 step 8: m_bMovingAndShooting cleared (slot 558)"), Overlay.bMovingAndShooting);
	TestFalse(TEXT("0x102e8560 step 8: the spent burst pushes no layer"), F.HoldsLayer(0x1a));
	TestEqual(TEXT("0x102e8560 step 8: ...and does not stamp m_flLastAttackTime"),
		F.Guard->LastAttackTime, LastAttackBefore);
	TestEqual(TEXT("0x102e8560 step 8: ...and calls no Weapon_SetActivity"),
		F.Guard->WeaponActivityRequests.Num(), 1);
	TestEqual(TEXT("0x102e8560 step 9: the tail after the re-arm"), F.Guard->FacingTargetRequests.Num(), 4);

	// 7. The gate open (`debug_allow_mf_turn` 1): `|move_yaw| 175 > 90`, `RandomInt(0, ftol((190 -
	// 175) * 0.25) = 3) < 5` always -- the draw is taken and the side is the dot's sign against
	// `m_vecRight (+0x629c)`: `<= 0` -> 0x48, else 0x47.
	ElysiumNpcTunables::SetConVar(ElysiumNpcTunables::EConVar::DebugAllowMfTurn, 1.f);
	Conditions.Clear(MoveShootCond(0x4f));
	F.Guard->Right = FVector(0.0, 1.0, 0.0);                       // retail frame
	F.FreeLayers();
	F.Guard->Navigator.GoalPosCm = F.Guard->Origin + FVector(0.0, -100.0, 0.0);   // retail +Y: the right side
	F.Guard->Navigator.TargetOffsetCm = FVector::ZeroVector;
	const int32 SeedBeforeOpen = Stream.GetCurrentSeed();
	F.Guard->RunMoveAndShootOverlay();
	TestNotEqual(TEXT("0x102e869b the open gate takes its RandomInt draw"),
		Stream.GetCurrentSeed(), SeedBeforeOpen);
	TestTrue(TEXT("0x102e8767 dot > 0: AddGesture(0x47, 1)"), F.HoldsLayer(0x47));
	TestFalse(TEXT("0x102e8767 ...and not 0x48"), F.HoldsLayer(0x48));

	F.FreeLayers();
	F.Guard->Navigator.GoalPosCm = F.Guard->Origin + FVector(0.0, 100.0, 0.0);
	F.Guard->RunMoveAndShootOverlay();
	TestTrue(TEXT("0x102e8775 dot <= 0 (0x104454c4): AddGesture(0x48, 1)"), F.HoldsLayer(0x48));
	TestFalse(TEXT("0x102e8775 ...and not 0x47"), F.HoldsLayer(0x47));

	// A gesture already playing closes the arm (slot 270 on either activity), after the draw.
	F.Guard->Navigator.GoalPosCm = F.Guard->Origin + FVector(0.0, -100.0, 0.0);
	F.Guard->RunMoveAndShootOverlay();
	TestFalse(TEXT("0x102e86f5 a held 0x48 layer refuses the 0x47 push"), F.HoldsLayer(0x47));
	ElysiumNpcTunables::ResetConVars();

	// 2. No enemy and none to be had: slot 560 clears the attack conditions, then step 3 returns.
	F.StageRun();
	F.Guard->BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	Conditions.Set(MoveShootCond(0x4f));
	Overlay.NextShotTime = Now;
	F.Guard->RunMoveAndShootOverlay();
	const FElysiumNpcBase* const ConstGuard = F.Guard;
	if (ConstGuard->GetEnemy() == nullptr)
	{
		TestFalse(TEXT("0x102e8560 step 2: BestEnemy none -> slot 560 ClearAttackConditions"),
			Conditions.Has(MoveShootCond(0x4f)));
		TestEqual(TEXT("0x102e8560 step 3: no enemy returns before the tail"),
			F.Guard->FacingTargetRequests.Num(), 0);
	}
	else
	{
		TestTrue(TEXT("0x102e8560 step 2: BestEnemy's pick raises NEW_ENEMY 0x54"),
			Conditions.Has(MoveShootCond(0x54)));
	}
	return true;
}

#endif
