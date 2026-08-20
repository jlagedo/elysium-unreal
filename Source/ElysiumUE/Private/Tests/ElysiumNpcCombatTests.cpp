// Content-free Substrate automation: the combat schedule families — the NPC loadout, the weapon
// capability split, the committed-enemy attack conditions, the two recovered selectors, the chase
// and swing programs end to end over a recording motor, and the body-owner lifecycle around them.
//
// Every number and every ordering asserted here is a fact from
// `docs/vtmb/npc-ai-reverse-engineering.md` -> "Ordinary humanoid combat selection" / "Schedules
// and tasks" / "Interrupt conditions", or from `docs/vtmb/combat-and-damage.md` -> "Target
// acquisition, sequence commit and recovery". Nothing loads a rulebook: the item catalogue is built
// in code for the length of a case, which is what makes these the runtime's statement of the
// contract rather than a reading of the export.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumNpcCombatTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumSaveTestHelpers::SaveTestCounterValue;
using ECond = EElysiumNpcCond;
using EId = EElysiumScheduleId;

namespace
{
	double Cm(double SourceUnits) { return SourceUnits * ElysiumMove::U; }

	// --- The synthetic catalogue ---------------------------------------------------------------
	// `item_w_fists` is spelled EXACTLY, because the loadout's fallback names that record and no
	// other suite claims the name. The two authored weapons are suite-local (`_npccombat_`):
	// `ElysiumItems::Install` registers a class once per process and never unregisters, so a name
	// shared with another suite would resolve to whichever suite ran first.
	const TCHAR* const GFists   = TEXT("item_w_fists");
	const TCHAR* const GKatana  = TEXT("item_w_npccombat_katana");
	const TCHAR* const GPistol  = TEXT("item_w_npccombat_pistol");
	const TCHAR* const GTrinket = TEXT("item_g_npccombat_trinket");

	// The pistol's authored far edge, in Source units — `TOO_FAR_TO_ATTACK`'s only recovered input.
	constexpr float GPistolRangeUnits = 2000.f;

	FElysiumWeaponMode MakeMode(const TCHAR* Tag, const TCHAR* Dmg, int32 BaseLethality,
		float AttackRate, float Range = 0.f, int32 AmmoCost = 0)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = Tag;
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = Dmg;
		Mode.BaseLethality = BaseLethality;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = AttackRate;
		Mode.Range = Range;
		Mode.AmmoCost = AmmoCost;
		Mode.AmmoFired = 1;
		return Mode;
	}

	FElysiumItemTable MakeCombatItemTable(bool bWithFists)
	{
		FElysiumItemTable Table;

		if (bWithFists)
		{
			// The real record's own shape: a `weapon_melee hidden` file with one `Attack` block.
			FElysiumItemDef Fists;
			Fists.Classname = GFists;
			Fists.PrintName = TEXT("Fists");
			Fists.Type = EElysiumItemType::WeaponMelee;
			Fists.bHidden = true;
			// Deliberately NOT `is_wieldable`: none of the shipped `item_w_*` records authors it,
			// which is exactly the case the loadout's explicit active-weapon switch exists for.
			Fists.Modes.Add(MakeMode(TEXT("Primary"),
				TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"), /*BaseLethality*/ 8,
				/*Attack_Rate*/ 0.5f));
			Table.Items.Add(MoveTemp(Fists));
		}

		FElysiumItemDef Katana;
		Katana.Classname = GKatana;
		Katana.PrintName = TEXT("Katana");
		Katana.Type = EElysiumItemType::WeaponMelee;
		Katana.Modes.Add(MakeMode(TEXT("Primary"),
			TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 12, 1.0f));
		Table.Items.Add(MoveTemp(Katana));

		FElysiumItemDef Pistol;
		Pistol.Classname = GPistol;
		Pistol.PrintName = TEXT("Pistol");
		Pistol.Type = EElysiumItemType::WeaponFirearm;
		Pistol.AmmoType = TEXT("NpcCombatRound");
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 6;
		Pistol.Modes.Add(MakeMode(TEXT("Primary"), TEXT("2 Lethal Ranged_Combat DMG_BULLET"), 9,
			/*Attack_Rate*/ 0.4f, GPistolRangeUnits, /*Ammo_Cost*/ 1));
		Table.Items.Add(MoveTemp(Pistol));

		FElysiumItemDef Trinket;
		Trinket.Classname = GTrinket;
		Trinket.PrintName = TEXT("Trinket");
		Trinket.Type = EElysiumItemType::Generic;
		Table.Items.Add(MoveTemp(Trinket));

		Table.Reindex();
		return Table;
	}

	// --- The world -------------------------------------------------------------------------------
	// One armed fighter at the origin facing +X, one NPC target 100 cm in front of it, the player
	// parked far away so it never becomes a melee acquisition candidate, and a counter wired off the
	// target's `OnDamaged` so the cycle-1 commit is observable as a number.
	struct FCombatFixture
	{
		FElysiumRecordingServices Services;
		FElysiumItemTable Items;
		FElysiumEntityWorld World;
		FElysiumNpc* Fighter = nullptr;
		FElysiumNpc* Target = nullptr;
		FElysiumPlayer* Player = nullptr;
		bool bInstalled = false;

		FCombatFixture(const TCHAR* FighterEquip, bool bWithFists = true, bool bInstallCatalogue = true)
			: Items(MakeCombatItemTable(bWithFists))
			, World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x4E504343);
			if (bInstallCatalogue)
			{
				ElysiumItems::Install(Items);
				bInstalled = true;
			}
			// The recording motor is opt-in, and every movement task in this suite needs one.
			Services.bProvideNpcMotor = true;

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__npccombat_test__");

			FElysiumEntityDef FighterDef;
			FighterDef.Classname = TEXT("npc_VHumanCombatant");
			FighterDef.TargetName = TEXT("fighter");
			FighterDef.Origin = FVector::ZeroVector;
			FighterDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
			FighterDef.Keys.Add(TEXT("vision"), TEXT("4000"));
			FighterDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));
			FighterDef.Keys.Add(TEXT("additionalequipment"), FighterEquip);
			FighterDef.Keys.Add(TEXT("cantdropweapons"), TEXT("1"));
			Defs.Defs.Add(MoveTemp(FighterDef));

			FElysiumEntityDef TargetDef;
			TargetDef.Classname = TEXT("npc_VHumanCombatant");
			TargetDef.TargetName = TEXT("target");
			TargetDef.Origin = FVector(100.0, 0.0, 0.0);
			TargetDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
			FElysiumOutputDef Row;
			Row.Name = TEXT("OnDamaged");
			Row.Target = TEXT("damagedcount");
			Row.Input = TEXT("Add");
			Row.Param = TEXT("1");
			TargetDef.Outputs.Add(MoveTemp(Row));
			Defs.Defs.Add(MoveTemp(TargetDef));

			FElysiumEntityDef Counter;
			Counter.Classname = TEXT("math_counter");
			Counter.TargetName = TEXT("damagedcount");
			Defs.Defs.Add(MoveTemp(Counter));

			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(0.0);

			// `AsNpc` rather than a blind downcast: the living-NPC leaf is what carries the senses,
			// memory and cognition every case here drives.
			FElysiumEntity* FighterEnt = World.FindByName(TEXT("fighter"));
			FElysiumEntity* TargetEnt = World.FindByName(TEXT("target"));
			Fighter = FighterEnt ? FighterEnt->AsNpc() : nullptr;
			Target = TargetEnt ? TargetEnt->AsNpc() : nullptr;
			Player = World.FindPlayer();
			if (Player)
			{
				// Out of every reach and cone: this suite is about NPC-versus-NPC combat, and a
				// player standing on the fighter's own origin would be an acquisition candidate.
				Player->Origin = FVector(0.0, Cm(9000.0), 0.0);
			}
			for (FElysiumNpc* Npc : { Fighter, Target })
			{
				if (Npc)
				{
					// The Source health ceiling the damage predicates read; a headless world has no
					// rulebook behind `SeedSheet`, so it is stated here.
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
					Npc->RecomputeSheet();
					Npc->MaxHealth = 100;
				}
			}
		}

		~FCombatFixture()
		{
			if (bInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		FCombatFixture(const FCombatFixture&) = delete;
		FCombatFixture& operator=(const FCombatFixture&) = delete;

		// Nothing here wants an NPC's own think competing with the pass a case is driving.
		void Quiet()
		{
			for (FElysiumNpc* Npc : { Fighter, Target })
			{
				if (Npc)
				{
					Npc->NextThink = ELYSIUM_NEVER_THINK;
				}
			}
		}

		// Two ordinary thinks: the first crosses the admission barrier, the second resolves the
		// loadout. That is the real production path, not an injected grant.
		void RunAdmissionAndLoadout()
		{
			for (int32 i = 0; i < 2; ++i)
			{
				for (FElysiumNpc* Npc : { Fighter, Target })
				{
					if (Npc)
					{
						Npc->NextThink = 0.0f;
					}
				}
				World.Tick(0.0);
			}
			Quiet();
		}

		void Flush(double Now)
		{
			Quiet();
			World.Tick(Now);
		}

		void Hate(const FElysiumEntity* Who, int32 Priority)
		{
			if (Fighter && Who)
			{
				Fighter->Relationships.SetEntity(Who->Handle, EElysiumRelationship::Hate, Priority);
			}
		}

		// The mind's state is private to the leaf; its inspector row is the read side everything else
		// uses, so a test asserts through the same surface a developer would look at.
		FString Debug(const FElysiumEntity* Entity, const TCHAR* Key) const
		{
			if (Entity == nullptr)
			{
				return FString();
			}
			TArray<TPair<FString, FString>> Rows;
			Entity->GetDebugState(Rows);
			for (const TPair<FString, FString>& Row : Rows)
			{
				if (Row.Key == Key)
				{
					return Row.Value;
				}
			}
			return FString();
		}

		FElysiumRecordingNpcMotor* MotorFor(const FElysiumNpc* Npc) const
		{
			for (const TUniquePtr<FElysiumRecordingNpcMotor>& Motor : Services.NpcMotors)
			{
				if (Npc && Motor && Motor->Owner == Npc->Handle)
				{
					return Motor.Get();
				}
			}
			return nullptr;
		}

		FElysiumWeapon* ActiveWeapon(FElysiumNpc* Npc) const
		{
			FElysiumItem* Item = Npc ? Npc->Inventory.Active(*Npc) : nullptr;
			return Item ? Item->AsWeapon() : nullptr;
		}

		// Commit the fighter to the target and put the mind in combat state through the real
		// ideal-state pass.
		void CommitToTarget(double Now)
		{
			if (!Fighter || !Target)
			{
				return;
			}
			Hate(Target, 5);
			Fighter->Senses.Memory.Enemy = Target->Handle;
			Fighter->UpdateIdealState(Now);
		}

		int32 DamageTaken(const FElysiumNpc* Npc) const
		{
			return Npc ? Npc->Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health) : -1;
		}
	};
}

// =====================================================================================
// The loadout: the authored keyfield, the fists fallback, and the marked unarmed path.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatLoadoutTest,
	"Elysium.Substrate.NpcCombat.Loadout", GElysiumTestFlags)
bool FElysiumNpcCombatLoadoutTest::RunTest(const FString&)
{
	// --- The authored classname is granted and made the ACTIVE weapon --------------------------
	{
		FCombatFixture F(GPistol);
		if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
		{
			return false;
		}
		TestEqual(TEXT("the keyfield parsed"), F.Fighter->AdditionalEquipment, FString(GPistol));
		TestTrue(TEXT("`cantdropweapons` parses beside it"), F.Fighter->bCantDropWeapons);
		TestFalse(TEXT("nothing is granted before the loadout runs"), F.Fighter->bLoadoutResolved);

		F.RunAdmissionAndLoadout();
		TestTrue(TEXT("the loadout latch is set once the think has run"), F.Fighter->bLoadoutResolved);

		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("the authored weapon is the ACTIVE weapon"), Weapon))
		{
			return false;
		}
		TestEqual(TEXT("...and it is the classname the map authored"), Weapon->ClassName(),
			FString(GPistol));
		TestEqual(TEXT("its magazine spawned loaded from the record's Default_Size"),
			Weapon->MagazineCount, 6);
		// The record authors no `is_wieldable` — like every shipped `item_w_*` weapon — so this is
		// the assertion that the loadout's own active-weapon switch is what armed it.
		TestFalse(TEXT("the authored record is deliberately not `is_wieldable`"),
			Weapon->IsWieldable());

		// A second resolution must not hand a second gun to an NPC that is already armed.
		TestEqual(TEXT("re-resolving an armed NPC grants nothing"),
			ElysiumNpcLoadout::Resolve(*F.Fighter), ElysiumNpcLoadout::EResult::AlreadyArmed);
		TestEqual(TEXT("...and the carried list still holds exactly one item"),
			F.Fighter->Inventory.Num(), 1);
	}

	// --- Nothing authored falls back to the fists record ---------------------------------------
	{
		FCombatFixture F(TEXT("0"));   // the authored "none" sentinel, on 78 of the 267 rows
		if (F.Fighter == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("`0` is the authored none sentinel"),
			ElysiumNpcLoadout::IsNoneSentinel(F.Fighter->AdditionalEquipment));
		F.RunAdmissionAndLoadout();

		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("an NPC with no authored weapon still holds its fists"), Weapon))
		{
			return false;
		}
		TestEqual(TEXT("...which is `item_w_fists`, not `item_w_unarmed`"), Weapon->ClassName(),
			FString(GFists));
	}

	// --- No catalogue at all: the marked unarmed path ------------------------------------------
	{
		// A fabricated-rulebook / headless world. Nothing is armed, and that is a supported state
		// rather than a failure — combat selection routes to melee with bare-hands defaults.
		FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
		if (F.Fighter == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		TestNull(TEXT("an NPC in a world with no item catalogue stays unarmed"),
			F.ActiveWeapon(F.Fighter));
		TestEqual(TEXT("...and reports the unarmed capability"),
			ElysiumNpcCond::WeaponCapability(*F.Fighter), ElysiumNpcCond::ECapability::Unarmed);
		TestTrue(TEXT("the inspector says so in one row"),
			F.Debug(F.Fighter, TEXT("Weapon")).Contains(TEXT("unarmed")));
	}

	// --- An authored classname that is not a wielded weapon is refused by name ------------------
	{
		AddExpectedError(TEXT("is a 'Generic' item, not a wielded weapon"),
			EAutomationExpectedErrorFlags::Contains, 1);
		FCombatFixture F(GTrinket);
		if (F.Fighter == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (TestNotNull(TEXT("the refusal still falls back to the fists"), Weapon))
		{
			TestEqual(TEXT("...rather than arming a trinket"), Weapon->ClassName(), FString(GFists));
		}
	}
	return true;
}

// =====================================================================================
// The capability split: which selector a weapon routes to, and the two retail bits.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatCapabilityTest,
	"Elysium.Substrate.NpcCombat.Capability", GElysiumTestFlags)
bool FElysiumNpcCombatCapabilityTest::RunTest(const FString&)
{
	TestEqual(TEXT("the melee capability is retail's own 0x18000"),
		ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::ECapability::Melee), 0x18000);
	TestEqual(TEXT("a firearm reports 0x2000"),
		ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::ECapability::Ranged), 0x2000);

	// --- A melee weapon routes to the melee selector -------------------------------------------
	{
		FCombatFixture F(GKatana);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		TestEqual(TEXT("a `weapon_melee` record is melee capability"),
			ElysiumNpcCond::WeaponCapability(*F.Fighter), ElysiumNpcCond::ECapability::Melee);

		F.CommitToTarget(10.0);
		// Injected rather than gathered: this case is about the ROUTE, and the melee selector's
		// dodge branch is one no ranged program carries.
		F.Fighter->Cognition.Conditions.Reset();
		F.Fighter->Cognition.Conditions.Set(ECond::ShouldDodge);
		TestEqual(TEXT("melee capability enters the melee selector"), F.Fighter->SelectSchedule(),
			EId::MeleeDodge);
	}

	// --- A firearm routes to the ranged selector -----------------------------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		TestEqual(TEXT("a `weapon_firearm` record is ranged capability"),
			ElysiumNpcCond::WeaponCapability(*F.Fighter), ElysiumNpcCond::ECapability::Ranged);

		F.CommitToTarget(10.0);
		F.Fighter->Cognition.Conditions.Reset();
		// The same injected condition the melee selector answers with a dodge: the ranged selector
		// has no such branch, so it must NOT be what decides here.
		F.Fighter->Cognition.Conditions.Set(ECond::ShouldDodge);
		F.Fighter->Cognition.Conditions.Set(ECond::CanRangeAttack1);
		TestEqual(TEXT("ranged capability enters the ranged selector"), F.Fighter->SelectSchedule(),
			EId::RangeAttack1);
	}

	// --- Unarmed takes the melee branch with bare-hands defaults -------------------------------
	{
		FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.CommitToTarget(10.0);
		F.Fighter->Cognition.Conditions.Reset();
		F.Fighter->Cognition.Conditions.Set(ECond::ShouldBlock);
		TestEqual(TEXT("an unarmed NPC still fights, on the melee branch"),
			F.Fighter->SelectSchedule(), EId::MeleePreblock);
	}
	return true;
}

// =====================================================================================
// The committed-enemy attack conditions: reach, facing, readiness, range bands, ammo.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatAttackConditionsTest,
	"Elysium.Substrate.NpcCombat.AttackConditions", GElysiumTestFlags)
bool FElysiumNpcCombatAttackConditionsTest::RunTest(const FString&)
{
	// --- Melee: reach, facing and the recovery deadline -----------------------------------------
	{
		FCombatFixture F(GKatana);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.Fighter->Senses.Memory.Enemy = F.Target->Handle;

		// 100 cm is inside the recovered 64-Source-unit reach, and the fighter faces +X.
		FElysiumNpcConditions Cond;
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("an enemy in reach, faced, with the deadline passed can attack"),
			Cond.Has(ECond::CanMeleeAttack1));
		TestFalse(TEXT("...and is not too far"), Cond.Has(ECond::TooFarToAttack));
		TestFalse(TEXT("...nor waiting on its attack timer"), Cond.Has(ECond::WaitingAttackTime));

		// Turned away: still in reach, no longer inside the swing's 30-degree half-angle.
		F.Fighter->Angles.Y = 180.0;
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestFalse(TEXT("an enemy behind the NPC is not attackable"), Cond.Has(ECond::CanMeleeAttack1));
		TestFalse(TEXT("...and turning away does not make it far away"),
			Cond.Has(ECond::TooFarToAttack));
		F.Fighter->Angles.Y = 0.0;

		// Beyond the reach: the melee band's own `TOO_FAR_TO_ATTACK`.
		F.Target->Origin = FVector(Cm(500.0), 0.0, 0.0);
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("an enemy beyond the swing's reach is too far"), Cond.Has(ECond::TooFarToAttack));
		TestFalse(TEXT("...and cannot be attacked"), Cond.Has(ECond::CanMeleeAttack1));
		F.Target->Origin = FVector(100.0, 0.0, 0.0);

		// The recovery deadline the weapon controller owns is what raises `WAITING_ATTACK_TIME`.
		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("the fighter holds its katana"), Weapon))
		{
			return false;
		}
		Weapon->HoldAttacksUntil(50.0);
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("an unexpired next-attack deadline raises WAITING_ATTACK_TIME (0x2f)"),
			Cond.Has(ECond::WaitingAttackTime));
		TestFalse(TEXT("...and withholds CAN_MELEE_ATTACK1"), Cond.Has(ECond::CanMeleeAttack1));
	}

	// --- Ranged: the two bands, the ammunition test and the ready answer ------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.Fighter->Senses.Memory.Enemy = F.Target->Handle;
		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("the fighter holds its pistol"), Weapon))
		{
			return false;
		}

		// Inside the fighter's own swing reach: too close to shoot.
		FElysiumNpcConditions Cond;
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("an enemy inside the NPC's own melee reach is TOO_CLOSE_TO_ATTACK"),
			Cond.Has(ECond::TooCloseToAttack));
		TestFalse(TEXT("...so the shot is not offered"), Cond.Has(ECond::CanRangeAttack1));

		// In the band: past the near edge, inside the mode's authored `Range`.
		F.Target->Origin = FVector(Cm(400.0), 0.0, 0.0);
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("an enemy inside the authored Range is shootable"),
			Cond.Has(ECond::CanRangeAttack1));
		TestFalse(TEXT("...and neither band edge fires"),
			Cond.Has(ECond::TooCloseToAttack) || Cond.Has(ECond::TooFarToAttack));

		// Beyond the mode's authored `Range`.
		F.Target->Origin = FVector(Cm(GPistolRangeUnits + 500.0), 0.0, 0.0);
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("an enemy past the mode's authored Range is TOO_FAR_TO_ATTACK"),
			Cond.Has(ECond::TooFarToAttack));
		TestFalse(TEXT("...and is not shootable"), Cond.Has(ECond::CanRangeAttack1));

		// `NO_PRIMARY_AMMO` (0x40) is an empty magazine AND an empty reserve.
		F.Target->Origin = FVector(Cm(400.0), 0.0, 0.0);
		Weapon->MagazineCount = 0;
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("an empty magazine with an empty reserve raises NO_PRIMARY_AMMO"),
			Cond.Has(ECond::NoPrimaryAmmo));
		TestFalse(TEXT("...and withholds the shot"), Cond.Has(ECond::CanRangeAttack1));

		F.Fighter->Inventory.AddReserve(TEXT("NpcCombatRound"), 12);
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestFalse(TEXT("a refillable magazine is a reload, not an ammunition failure"),
			Cond.Has(ECond::NoPrimaryAmmo));

		// The occlusion latch drives the line-of-fire arm.
		Weapon->MagazineCount = 6;
		F.Fighter->Senses.Memory.bEnemyOccluded = true;
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0, Cond);
		TestTrue(TEXT("the occlusion latch raises WEAPON_SIGHT_OCCLUDED"),
			Cond.Has(ECond::WeaponSightOccluded));
		TestFalse(TEXT("...and an occluded enemy is not shootable"), Cond.Has(ECond::CanRangeAttack1));
		TestFalse(TEXT("WEAPON_THROUGH_WALL has no producer and is never set"),
			Cond.Has(ECond::WeaponThroughWall));
		TestFalse(TEXT("...nor does WEAPON_BLOCKED_BY_FRIEND"),
			Cond.Has(ECond::WeaponBlockedByFriend));
	}

	// --- The four SHOULD_* conditions stay plumbed and unset -------------------------------------
	{
		FCombatFixture F(GKatana);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.Fighter->Senses.Memory.Enemy = F.Target->Handle;
		// A real notice, delivered: the record is written, and the response policy still raises
		// nothing, because the policy is what is unrecovered.
		TestTrue(TEXT("a swing from 100 cm is inside the recovered 150-unit notice radius"),
			ElysiumNpcCond::NoticeMeleeAttack(*F.Target, F.Fighter->Handle, F.Fighter->Origin, 10.0));
		TestTrue(TEXT("...and the five-second retention holds it"),
			ElysiumNpcCond::HasDetectedAttack(*F.Target, 14.9));
		TestFalse(TEXT("...and expires it"), ElysiumNpcCond::HasDetectedAttack(*F.Target, 15.1));

		FElysiumNpcConditions Cond;
		ElysiumNpcCond::GatherAttackConditions(*F.Target, 10.5, Cond);
		for (const ECond Response : { ECond::ShouldDodge, ECond::ShouldBlock, ECond::ShouldStepback,
			ECond::ShouldKick })
		{
			TestFalse(*FString::Printf(TEXT("%s has no producer and is never set"),
				ElysiumNpcCondName(Response)), Cond.Has(Response));
		}

		// Out of the notice radius and out of sight: refused rather than remembered.
		F.Target->Senses.Memory.DetectedAttackAttacker = FElysiumEntityHandle::Invalid();
		F.Target->Senses.Memory.DetectedAttackTime = -1.0;
		TestFalse(TEXT("a swing from beyond 150 units with no sight route is not noticed"),
			ElysiumNpcCond::NoticeMeleeAttack(*F.Target, F.Fighter->Handle,
				FVector(Cm(900.0), 0.0, 0.0), 11.0));
		TestFalse(TEXT("...and writes no record"),
			ElysiumNpcCond::HasDetectedAttack(*F.Target, 11.0));
	}
	return true;
}

// =====================================================================================
// The melee selector's recovered order, and the retained binary draw.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatMeleeSelectorTest,
	"Elysium.Substrate.NpcCombat.MeleeSelectorOrder", GElysiumTestFlags)
bool FElysiumNpcCombatMeleeSelectorTest::RunTest(const FString&)
{
	// Find one seed for each side of the binary draw, from the same stream the selector reads.
	int32 SeedKick = INDEX_NONE;
	int32 SeedStepback = INDEX_NONE;
	for (int32 Seed = 1; Seed <= 64 && (SeedKick == INDEX_NONE || SeedStepback == INDEX_NONE); ++Seed)
	{
		ElysiumRng::SeedAll(Seed);
		const bool bKick = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 1) != 0;
		if (bKick && SeedKick == INDEX_NONE)
		{
			SeedKick = Seed;
		}
		else if (!bKick && SeedStepback == INDEX_NONE)
		{
			SeedStepback = Seed;
		}
	}
	if (!TestTrue(TEXT("both sides of the binary draw are reachable"),
		SeedKick != INDEX_NONE && SeedStepback != INDEX_NONE))
	{
		return false;
	}

	FCombatFixture F(GKatana);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	F.CommitToTarget(10.0);

	auto Select = [&F](std::initializer_list<ECond> Conditions)
	{
		F.Fighter->Cognition.Conditions = FElysiumNpcConditions::Of(Conditions);
		return F.Fighter->SelectSchedule();
	};

	// --- The precedence ladder, in the recovered order ------------------------------------------
	// "`SHOULD_DODGE` returns `SCHED_TROIKA_MELEE_DODGE` (0xd5); `SHOULD_BLOCK` returns
	// `SCHED_TROIKA_MELEE_PREBLOCK` (0xd6). ... either condition outranks an ordinary attack."
	TestEqual(TEXT("dodge outranks block, kick, stepback and the attack"),
		Select({ ECond::ShouldDodge, ECond::ShouldBlock, ECond::ShouldKick, ECond::ShouldStepback,
			ECond::CanMeleeAttack1 }), EId::MeleeDodge);
	TestEqual(TEXT("block outranks kick, stepback and the attack"),
		Select({ ECond::ShouldBlock, ECond::ShouldKick, ECond::ShouldStepback,
			ECond::CanMeleeAttack1 }), EId::MeleePreblock);
	TestEqual(TEXT("a lone kick request outranks the attack"),
		Select({ ECond::ShouldKick, ECond::CanMeleeAttack1 }), EId::MeleeKick);
	TestEqual(TEXT("a lone stepback request outranks the attack"),
		Select({ ECond::ShouldStepback, ECond::CanMeleeAttack1 }), EId::MeleeStepback);
	TestEqual(TEXT("an ordinary usable attack selects SCHED_TROIKA_MELEE_ATTACK1 (0xdc)"),
		Select({ ECond::CanMeleeAttack1 }), EId::MeleeAttack1);
	TestEqual(TEXT("too far selects the advance"),
		Select({ ECond::TooFarToAttack }), EId::MeleeAdvance);
	TestEqual(TEXT("in reach but on the attack timer circles"),
		Select({ ECond::WaitingAttackTime }), EId::MeleeCircle);
	TestEqual(TEXT("in reach, off the timer and unaligned holds in melee"),
		Select({}), EId::MeleeIdle);

	// --- The binary draw, seeded both ways ------------------------------------------------------
	F.Fighter->CombatSelector.Reset();
	ElysiumRng::SeedAll(SeedKick);
	TestEqual(TEXT("kick and stepback together take the binary draw — kick"),
		Select({ ECond::ShouldKick, ECond::ShouldStepback }), EId::MeleeKick);

	F.Fighter->CombatSelector.Reset();
	ElysiumRng::SeedAll(SeedStepback);
	TestEqual(TEXT("...and stepback"),
		Select({ ECond::ShouldKick, ECond::ShouldStepback }), EId::MeleeStepback);
	TestTrue(TEXT("the stepback stamps the enemy as the position it retreats from"),
		F.Fighter->SavePosition.Equals(F.Target->Origin));

	// --- The retention: repeated ticks do not independently reroll -------------------------------
	// The stream is re-seeded to the OTHER outcome between the two selections. A selector that
	// rerolled would change its answer; the recovered one does not.
	ElysiumRng::SeedAll(SeedKick);
	TestEqual(TEXT("the retained decision survives a stream that would now draw the other way"),
		Select({ ECond::ShouldKick, ECond::ShouldStepback }), EId::MeleeStepback);

	// Past the stated retention, the draw is taken again.
	F.Fighter->CombatSelector.DrawnReactionExpiresAt = -1.0;
	ElysiumRng::SeedAll(SeedKick);
	TestEqual(TEXT("an expired decision is redrawn"),
		Select({ ECond::ShouldKick, ECond::ShouldStepback }), EId::MeleeKick);
	return true;
}

// =====================================================================================
// The ranged selector's recovered order.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatRangedSelectorTest,
	"Elysium.Substrate.NpcCombat.RangedSelectorOrder", GElysiumTestFlags)
bool FElysiumNpcCombatRangedSelectorTest::RunTest(const FString&)
{
	FCombatFixture F(GPistol);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	F.CommitToTarget(10.0);

	auto Select = [&F](std::initializer_list<ECond> Conditions)
	{
		F.Fighter->Cognition.Conditions = FElysiumNpcConditions::Of(Conditions);
		return F.Fighter->SelectSchedule();
	};

	TestEqual(TEXT("an attack-ready shot selects SCHED_TROIKA_RANGE_ATTACK1 (0xec)"),
		Select({ ECond::CanRangeAttack1 }), EId::RangeAttack1);
	TestEqual(TEXT("an occluded enemy routes through the chase"),
		Select({ ECond::EnemyOccluded }), EId::ChaseEnemy);
	TestEqual(TEXT("...as does the weapon-sight occlusion arm"),
		Select({ ECond::WeaponSightOccluded }), EId::ChaseEnemy);
	TestEqual(TEXT("excessive distance selects SCHED_TROIKA_CHASE_ENEMY (0xb1)"),
		Select({ ECond::TooFarToAttack }), EId::ChaseEnemy);
	TestEqual(TEXT("an enemy too close to shoot backs off"),
		Select({ ECond::TooCloseToAttack }), EId::RunAway);
	TestEqual(TEXT("no ammunition falls through to the spacing arms rather than dry-firing"),
		Select({ ECond::NoPrimaryAmmo, ECond::TooFarToAttack }), EId::ChaseEnemy);

	// A zero return falls through to the Troika base selector: the composition rule.
	TestEqual(TEXT("merely waiting on the attack timer composes down to the base idle"),
		Select({ ECond::WaitingAttackTime }), EId::IdleDisposition);
	TestEqual(TEXT("...and the base branch's damage reaction is SMALL_FLINCH (0x14)"),
		Select({ ECond::WaitingAttackTime, ECond::HeavyDamage }), EId::SmallFlinch);
	return true;
}

// =====================================================================================
// The chase end to end: the schedule owner, the motor requests, arrival, and release.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatChaseTest,
	"Elysium.Substrate.NpcCombat.Chase", GElysiumTestFlags)
bool FElysiumNpcCombatChaseTest::RunTest(const FString&)
{
	FCombatFixture F(GPistol);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Fighter);
	if (!TestNotNull(TEXT("the fighter owns a recording motor"), Motor))
	{
		return false;
	}

	// Well beyond the pistol's authored range.
	F.Target->Origin = FVector(Cm(GPistolRangeUnits + 2000.0), 0.0, 0.0);
	F.CommitToTarget(10.0);
	ElysiumNpcEnemy::GatherConditions(*F.Fighter, 10.0);
	TestTrue(TEXT("a distant enemy raises TOO_FAR_TO_ATTACK"),
		F.Fighter->Cognition.Conditions.Has(ECond::TooFarToAttack));
	TestEqual(TEXT("...and the ranged selector chases"), F.Fighter->SelectSchedule(), EId::ChaseEnemy);

	TestTrue(TEXT("the chase starts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, EId::ChaseEnemy, *F.Fighter));

	double Delay = 0.0;
	TestTrue(TEXT("the first think reaches the movement watch"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.0, Delay,
			&F.Fighter->Cognition.Conditions));

	// The body-owner arbiter: the `Schedule` owner is live for the first time.
	TestTrue(TEXT("combat movement claims the Schedule body owner"),
		F.Debug(F.Fighter, TEXT("Body owner")).StartsWith(TEXT("Schedule")));
	TestTrue(TEXT("the path was issued at the enemy's feet"),
		Motor->RequestedFeet.Equals(F.Target->Origin));
	TestTrue(TEXT("...at the schedule's recovered tolerance of 24 Source units"),
		F.Services.Log().Contains(FString::Printf(TEXT("radius=%.1f"), 24.0f * ElysiumMove::U)));
	TestTrue(TEXT("...and at running speed"),
		FMath::IsNearlyEqual(Motor->RequestedSpeedCmPerSecond, ElysiumNpcGait::RunSpeed));
	// The chase asks the activity seam for ACT_RUN and plays whatever label it answers with; this
	// fixture resolves none, so what reaches the body is the stated retail-label fallback.
	TestTrue(TEXT("run locomotion went onto the body"), F.Services.Saw(TEXT("PlayNpcClip")));

	// Still travelling: the watch holds the task rather than advancing it.
	TestTrue(TEXT("an in-flight path keeps the schedule open"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.1, Delay,
			&F.Fighter->Cognition.Conditions));
	TestTrue(TEXT("...on a fast sample cadence"), Delay <= 0.05 + KINDA_SMALL_NUMBER);

	// Arrival ends the program.
	Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
	TestFalse(TEXT("arrival completes the last task and ends the chase"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.2, Delay,
			&F.Fighter->Cognition.Conditions));
	TestFalse(TEXT("nothing is left running"), F.Fighter->Schedule.IsRunning());

	// The claim is given back where the program ended, before the next selection runs.
	F.Fighter->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	F.Fighter->Cognition.Conditions.Reset();
	F.Fighter->ThinkStanceOrIdle(10.3);
	TestTrue(TEXT("the ended program hands the body back"),
		F.Debug(F.Fighter, TEXT("Body owner")).StartsWith(TEXT("None")));
	TestTrue(TEXT("...and stops the request it was holding"), F.Services.Saw(TEXT("NpcMotor Stop")));

	// Back in the band, the attack window opens.
	F.Target->Origin = FVector(Cm(400.0), 0.0, 0.0);
	F.Fighter->Senses.Memory.Enemy = F.Target->Handle;
	ElysiumNpcEnemy::GatherConditions(*F.Fighter, 11.0);
	TestTrue(TEXT("an enemy back inside the band is shootable"),
		F.Fighter->Cognition.Conditions.Has(ECond::CanRangeAttack1));
	TestEqual(TEXT("...and reselection takes the shot"), F.Fighter->SelectSchedule(),
		EId::RangeAttack1);
	return true;
}

// =====================================================================================
// The swing program: the notice, the weapon press, the damage that lands through the
// cycle-1 commit, and the recovered empty mask that owns the NPC while it runs.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatSwingTest,
	"Elysium.Substrate.NpcCombat.Swing", GElysiumTestFlags)
bool FElysiumNpcCombatSwingTest::RunTest(const FString&)
{
	FCombatFixture F(GKatana);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	F.CommitToTarget(0.0);
	ElysiumNpcEnemy::GatherConditions(*F.Fighter, 0.0);
	TestTrue(TEXT("an enemy in reach and faced is attackable"),
		F.Fighter->Cognition.Conditions.Has(ECond::CanMeleeAttack1));
	TestEqual(TEXT("the melee selector takes the approach"), F.Fighter->SelectSchedule(),
		EId::MeleeAttack1);

	// The whole approach plus its transfer to the terminal swing runs inside one think: face, stop,
	// transfer, announce, attack.
	TestTrue(TEXT("the approach starts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, EId::MeleeAttack1, *F.Fighter));
	double Delay = 0.0;
	ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 0.0, Delay,
		&F.Fighter->Cognition.Conditions);

	TestTrue(TEXT("TASK_ANNOUNCE_ATTACK wrote the victim's detected-attack record"),
		ElysiumNpcCond::HasDetectedAttack(*F.Target, 0.0));
	TestTrue(TEXT("...naming the attacker"),
		F.Target->Senses.Memory.DetectedAttackAttacker == F.Fighter->Handle);

	FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
	if (!TestNotNull(TEXT("the fighter holds its katana"), Weapon))
	{
		return false;
	}
	TestTrue(TEXT("TASK_MELEE_ATTACK1 staged a real weapon transaction"), Weapon->Swing.bActive);
	TestEqual(TEXT("...aimed at the committed enemy"), Weapon->Swing.Opponent, F.Target->Handle);
	TestTrue(TEXT("...and held the next-attack deadline"), Weapon->NextPrimaryAttackTime > 0.0);

	// Producers enqueue; only queue service delivers.
	TestEqual(TEXT("no damage lands inside the accepted swing"), F.DamageTaken(F.Target), 0);
	F.Flush(2.0);
	TestTrue(TEXT("the contact commits damage through the typed health commit"),
		F.DamageTaken(F.Target) > 0);
	F.Flush(2.5);
	TestEqual(TEXT("...and OnDamaged fires from its real producer"),
		SaveTestCounterValue(F.World.FindByName(TEXT("damagedcount"))), 1.0f);

	// The recovered EMPTY mask: once the terminal task owns the NPC it is not reevaluated.
	const FElysiumSchedule* Swing = ElysiumScheduleFor(EId::MeleeAttack1Swing);
	if (TestNotNull(TEXT("the swing program is registered"), Swing))
	{
		TestTrue(TEXT("its recovered interrupt mask is empty"), Swing->Interrupts.IsEmpty());
	}
	const FElysiumNpcConditions Storm = FElysiumNpcConditions::Of({
		ECond::NewEnemy, ECond::HeavyDamage, ECond::LightDamage, ECond::EnemyDead });
	const int32 SerialBefore = Weapon->Swing.Serial;

	// The control, so "the mask holds" is a statement about the mask and not about the kernel: with
	// a mask installed the same storm ends the program before its terminal task can run.
	{
		ElysiumSchedule::FInterruptMaskScope Scope(EId::MeleeAttack1Swing,
			FElysiumNpcConditions::Of({ ECond::NewEnemy }));
		TestTrue(TEXT("the swing program restarts"),
			ElysiumSchedule::Start(F.Fighter->Schedule, EId::MeleeAttack1Swing, *F.Fighter));
		TestFalse(TEXT("with a mask installed, NEW_ENEMY aborts it"),
			ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 2.5, Delay, &Storm));
		TestEqual(TEXT("...and no attack was pressed"), Weapon->Swing.Serial, SerialBefore);
	}

	// The registered posture: no interrupts, so the same storm cannot stop the swing.
	TestTrue(TEXT("the swing program restarts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, EId::MeleeAttack1Swing, *F.Fighter));
	ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 2.5, Delay, &Storm);
	TestTrue(TEXT("a NEW_ENEMY mid-swing does not abort the terminal attack"),
		Weapon->Swing.Serial > SerialBefore);
	TestFalse(TEXT("...and nothing reported an interrupt"),
		F.Debug(F.Fighter, TEXT("Mind transition")).Contains(TEXT("interrupted by")));
	return true;
}

// =====================================================================================
// Interrupts: the chase admits ENEMY_DEAD, and the starvation gate no longer blocks it.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatInterruptTest,
	"Elysium.Substrate.NpcCombat.ChaseInterrupt", GElysiumTestFlags)
bool FElysiumNpcCombatInterruptTest::RunTest(const FString&)
{
	FCombatFixture F(GPistol);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	F.Target->Origin = FVector(Cm(GPistolRangeUnits + 2000.0), 0.0, 0.0);
	F.CommitToTarget(10.0);
	ElysiumNpcEnemy::GatherConditions(*F.Fighter, 10.0);

	TestTrue(TEXT("the chase starts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, EId::ChaseEnemy, *F.Fighter));
	double Delay = 0.0;
	TestTrue(TEXT("the chase reaches its movement watch"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.0, Delay,
			&F.Fighter->Cognition.Conditions));

	// The recovered mask: a chase interrupts on a new, dead, unreachable, occluded or lost enemy and
	// on any newly available attack. "Pathing cannot monopolize an attack-ready NPC."
	if (const FElysiumSchedule* Chase = ElysiumScheduleFor(EId::ChaseEnemy))
	{
		for (const ECond Admitted : { ECond::NewEnemy, ECond::EnemyDead, ECond::EnemyUnreachable,
			ECond::EnemyOccluded, ECond::LostEnemy, ECond::CanMeleeAttack1, ECond::CanRangeAttack1,
			ECond::TooCloseToAttack })
		{
			TestTrue(*FString::Printf(TEXT("the chase admits %s"), ElysiumNpcCondName(Admitted)),
				Chase->Interrupts.Has(Admitted));
		}
	}
	// ...and the enemy transaction's own gate reads that mask, so a dead enemy is not starved.
	TestTrue(TEXT("the starvation gate no longer blocks a dead enemy mid-chase"),
		ElysiumNpcEnemy::IsScheduleInterested(*F.Fighter, ECond::EnemyDead));

	const FElysiumNpcConditions Dead = FElysiumNpcConditions::Of({ ECond::EnemyDead });
	TestFalse(TEXT("ENEMY_DEAD mid-chase ends the program"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.1, Delay, &Dead));
	TestFalse(TEXT("...returning the NPC to selection rather than to a fail schedule"),
		F.Fighter->Schedule.IsRunning());
	TestTrue(TEXT("...and the trace names the condition that fired"),
		F.Debug(F.Fighter, TEXT("Mind transition")).Contains(TEXT("interrupted by ENEMY_DEAD")));
	return true;
}

// =====================================================================================
// Live acquisition: the disposition idle now admits NEW_ENEMY, so a standing NPC can be
// taken into combat between one think and the next.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatIdleAcquisitionTest,
	"Elysium.Substrate.NpcCombat.IdleAcquisition", GElysiumTestFlags)
bool FElysiumNpcCombatIdleAcquisitionTest::RunTest(const FString&)
{
	// The registered masks themselves. This is the cycle-6 change stated as data.
	for (const EId Idle : { EId::IdleDisposition, EId::AlertLookAroundNi })
	{
		const FElysiumSchedule* Program = ElysiumScheduleFor(Idle);
		if (!TestNotNull(TEXT("the idle program is registered"), Program))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s admits NEW_ENEMY"), ElysiumScheduleName(Idle)),
			Program->Interrupts.Has(ECond::NewEnemy));
		TestTrue(TEXT("...and the damage pair"),
			Program->Interrupts.Has(ECond::LightDamage) && Program->Interrupts.Has(ECond::HeavyDamage));
		TestTrue(TEXT("...and ENEMY_DEAD"), Program->Interrupts.Has(ECond::EnemyDead));
		TestTrue(TEXT("...and the hear family"),
			Program->Interrupts.Has(ECond::HearCombat) && Program->Interrupts.Has(ECond::HearDanger));
	}

	FCombatFixture F(GKatana);
	if (F.Fighter == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();

	// A hostile player standing in front of the idling NPC, seen.
	F.Player->Origin = FVector(Cm(200.0), 0.0, 0.0);
	F.Fighter->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);
	F.Fighter->Senses.Memory.ClosestPlayer = F.Player->Handle;
	F.Fighter->Senses.Memory.bPlayerLos = true;
	F.Fighter->Senses.Memory.bPlayerInCone = true;
	F.Fighter->Senses.Memory.bPlayerInRange = true;

	TestTrue(TEXT("the NPC is running its disposition idle"),
		ElysiumSchedule::Start(F.Fighter->Schedule, EId::IdleDisposition, *F.Fighter));

	ElysiumNpcEnemy::GatherConditions(*F.Fighter, 20.0);
	TestTrue(TEXT("the idle program does not starve the acquisition"),
		F.Fighter->Senses.Memory.Enemy == F.Player->Handle);
	TestTrue(TEXT("...and the pass raises NEW_ENEMY"),
		F.Fighter->Cognition.Conditions.Has(ECond::NewEnemy));

	// The interrupt then ends the idle program, and the ideal-state pass takes the NPC to combat.
	double Delay = 0.0;
	TestFalse(TEXT("NEW_ENEMY interrupts the disposition idle"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 20.0, Delay,
			&F.Fighter->Cognition.Conditions));
	F.Fighter->UpdateIdealState(20.0);
	TestTrue(TEXT("a committed enemy takes the NPC to combat"),
		F.Debug(F.Fighter, TEXT("Mind")).Contains(TEXT("current=Combat")));
	TestNotEqual(TEXT("...and combat selects a fight program, not the idle it just left"),
		F.Fighter->SelectSchedule(), EId::IdleDisposition);
	return true;
}

// =====================================================================================
// Running away: the projected retreat, and its fail path when the world will not have it.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatRunAwayTest,
	"Elysium.Substrate.NpcCombat.RunAway", GElysiumTestFlags)
bool FElysiumNpcCombatRunAwayTest::RunTest(const FString&)
{
	// --- The projected retreat ------------------------------------------------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Fighter);
		if (!TestNotNull(TEXT("the fighter owns a recording motor"), Motor))
		{
			return false;
		}
		F.CommitToTarget(10.0);
		ElysiumNpcEnemy::GatherConditions(*F.Fighter, 10.0);
		TestTrue(TEXT("an enemy inside the NPC's own reach is too close to shoot"),
			F.Fighter->Cognition.Conditions.Has(ECond::TooCloseToAttack));
		TestEqual(TEXT("...and the ranged selector backs off"), F.Fighter->SelectSchedule(),
			EId::RunAway);
		TestTrue(TEXT("the selector stamps the enemy as the position to leave"),
			F.Fighter->SavePosition.Equals(F.Target->Origin));

		TestTrue(TEXT("the retreat starts"),
			ElysiumSchedule::Start(F.Fighter->Schedule, EId::RunAway, *F.Fighter));
		double Delay = 0.0;
		TestTrue(TEXT("the retreat reaches its movement watch"),
			ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.0, Delay,
				&F.Fighter->Cognition.Conditions));
		TestTrue(TEXT("the world was asked where the extrapolated point lands"),
			F.Services.Saw(TEXT("NpcMotor ProjectToNavigable")));
		TestTrue(TEXT("the retreat moves directly away from the enemy"),
			Motor->RequestedFeet.X < F.Fighter->Origin.X);
		TestTrue(TEXT("...and claims the Schedule body owner to do it"),
			F.Debug(F.Fighter, TEXT("Body owner")).StartsWith(TEXT("Schedule")));
	}

	// --- The fail path: nowhere navigable to retreat to ------------------------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Fighter);
		if (Motor == nullptr)
		{
			return false;
		}
		Motor->bProjectsToNavigable = false;
		F.CommitToTarget(10.0);
		F.Fighter->SavePosition = F.Target->Origin;

		TestTrue(TEXT("the retreat starts"),
			ElysiumSchedule::Start(F.Fighter->Schedule, EId::RunAway, *F.Fighter));
		double Delay = 0.0;
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.0, Delay, nullptr);
		TestTrue(TEXT("an unprojectable retreat was asked and refused"),
			F.Services.Saw(TEXT("NpcMotor ProjectToNavigable")));
		TestFalse(TEXT("...and the retreat never became a move request"), Motor->bMoving);
		TestNotEqual(TEXT("...so the program left the retreat through its fail path"),
			F.Fighter->Schedule.Current, EId::RunAway);
	}
	return true;
}

// =====================================================================================
// A weaponless NPC fails its terminal attack task BY NAME rather than dealing damage
// out of nothing.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatUnarmedTaskFailureTest,
	"Elysium.Substrate.NpcCombat.UnarmedTaskFailure", GElysiumTestFlags)
bool FElysiumNpcCombatUnarmedTaskFailureTest::RunTest(const FString&)
{
	FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	TestNull(TEXT("the NPC really is unarmed"), F.ActiveWeapon(F.Fighter));
	F.CommitToTarget(0.0);

	TestTrue(TEXT("the swing program starts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, EId::MeleeAttack1Swing, *F.Fighter));
	double Delay = 0.0;
	ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 0.0, Delay, nullptr);

	TestEqual(TEXT("no damage is dealt out of nothing"), F.DamageTaken(F.Target), 0);
	TestNotEqual(TEXT("the failed swing left its own program"), F.Fighter->Schedule.Current,
		EId::MeleeAttack1Swing);

	// The announce still landed: opponent reservation changes no health and does not need a weapon.
	TestTrue(TEXT("TASK_ANNOUNCE_ATTACK still delivered its notice"),
		ElysiumNpcCond::HasDetectedAttack(*F.Target, 0.0));
	return true;
}

// =====================================================================================
// The Reaction-band producer every combat reaction goes through (LIFE5). What is pinned here is the
// blend rule, because it is the one thing the producer decides rather than forwards: a reaction is
// an ideal-activity write and takes the ordinary sequence-blend rules, so a request stating no blend
// takes the resolved clip's OWN authored fade (`docs/vtmb/combat-and-damage.md` § "Block and stagger
// reactions"). Only retail's flinch gesture hard-codes a pair, and it states one.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatReactionProducerTest,
	"Elysium.Substrate.NpcCombat.ReactionProducer", GElysiumTestFlags)
bool FElysiumNpcCombatReactionProducerTest::RunTest(const FString&)
{
	FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
	if (F.Fighter == nullptr)
	{
		return false;
	}
	F.Quiet();
	if (!TestNotNull(TEXT("the fighter carries a body to react with"), F.Fighter->Visual))
	{
		return false;
	}

	auto FirstCall = [&F](const TCHAR* Prefix) -> FString
	{
		for (const FString& Call : F.Services.Calls)
		{
			if (Call.StartsWith(Prefix))
			{
				return Call;
			}
		}
		return FString();
	};

	F.Services.bNpcActivitiesResolve = true;
	F.Services.bNpcOneShotsPlay = true;
	F.Services.ResolvedNpcActivityLabel = TEXT("block_heavy");
	F.Services.ResolvedNpcActivityClip = TEXT("block_heavy");
	F.Services.ResolvedNpcActivityOwner = TEXT("melee_bank");
	// The lying-down and damaged stance idles author 0.45; a reaction clip authoring anything but the
	// common 0.2 is what makes the difference between "read" and "defaulted" visible at all.
	F.Services.ResolvedNpcActivityFadeSeconds = 0.45f;

	// --- No stated blend: the clip's own authored fade -------------------------------------------
	F.Services.Calls.Reset();
	{
		FElysiumReactionPlayRequest Request;
		Request.Activity = TEXT("ACT_BLOCK_HEAVY");
		float Seconds = 0.0f;
		TestTrue(TEXT("the producer plays the resolved reaction"),
			F.Fighter->PlayReactionActivity(Request, &Seconds));
		TestTrue(TEXT("...and reports what the claim holds for"), Seconds > 0.0f);

		const FString Resolve = FirstCall(TEXT("ResolveNpcActivityClip"));
		TestTrue(TEXT("...having asked for the stated activity"),
			Resolve.Contains(TEXT("ACT_BLOCK_HEAVY")));
		// Non-directional: the blocked reaction is the activity the attacker's own sequence descriptor
		// stores, so nothing here derives an angle.
		TestTrue(TEXT("...with no hit yaw, because only the flinch is directional"),
			Resolve.Contains(TEXT("hit=0.0")));

		const FString Played = FirstCall(TEXT("PlayNpcOneShot"));
		TestTrue(TEXT("...and blends it in over the fade its own sequence authored"),
			Played.Contains(TEXT("in=0.45")) && Played.Contains(TEXT("out=0.45")));
		TestTrue(TEXT("...on the reaction route"), Played.Contains(TEXT("route=reaction")));
		TestTrue(TEXT("...in the reaction band"), Played.Contains(TEXT("prio=reaction")));
	}

	// --- A stated blend wins, which is what the flinch's hard-coded gesture pair relies on ---------
	F.Services.Calls.Reset();
	{
		FElysiumReactionPlayRequest Request;
		Request.Activity = TEXT("ACT_BLOCK_HEAVY");
		Request.BlendInSeconds = 0.1f;
		Request.BlendOutSeconds = 0.3f;
		TestTrue(TEXT("the producer plays it again"), F.Fighter->PlayReactionActivity(Request));

		const FString Played = FirstCall(TEXT("PlayNpcOneShot"));
		TestTrue(TEXT("...over the stated pair rather than the authored fade"),
			Played.Contains(TEXT("in=0.10")) && Played.Contains(TEXT("out=0.30")));
	}

	// --- A vocabulary with no such reaction is an ordinary negative, not a failure -----------------
	F.Services.Calls.Reset();
	F.Services.bNpcActivitiesResolve = false;
	{
		FElysiumReactionPlayRequest Request;
		Request.Activity = TEXT("ACT_BLOCK_HEAVY");
		float Seconds = 7.0f;
		TestFalse(TEXT("a body with no such reaction reacts with nothing"),
			F.Fighter->PlayReactionActivity(Request, &Seconds));
		TestEqual(TEXT("...and the duration is left untouched on the miss"), Seconds, 7.0f);
		TestFalse(TEXT("...with nothing handed to the pose layer"),
			F.Services.Saw(TEXT("PlayNpcOneShot")));
	}
	return true;
}

}   // namespace ElysiumNpcCombatTests

#endif   // WITH_DEV_AUTOMATION_TESTS
