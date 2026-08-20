// Content-free Substrate automation: the weapon controller — the item install's weapon branch,
// mode parsing, next-attack scheduling, the two-half attack transaction, the melee opposed record
// and its `rules.txt` classifier, the ranged per-victim route, reload and dry fire.
//
// Every number asserted here is a fact from `docs/vtmb/combat-and-damage.md`. Nothing loads a
// rulebook: the item catalogue and the `Melee_Reactions` table are built in code for the length of
// one case, which is what makes these the runtime's statement of the contract rather than a reading
// of the export. The rating and difficulty halves fail safe without a rulebook, so every damage
// number below is decided by the authored record alone.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumWeaponTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumSaveTestHelpers::SaveTestCounterValue;

namespace
{
	using EC = EElysiumTraitContainer;

	// --- The synthetic catalogue -------------------------------------------------------------
	// Classnames are test-local (`_test_`) on purpose: `ElysiumItems::Install` registers a class
	// once per process and never unregisters, so a name shared with another suite would resolve to
	// whichever suite ran first.

	FElysiumWeaponMode MakeMode(const TCHAR* Tag, const TCHAR* TypeName, const TCHAR* Dmg,
		int32 BaseLethality, float AttackRate, int32 AmmoCost = 0, int32 AmmoFired = 1)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = Tag;
		Mode.TypeName = TypeName;
		Mode.Type = FString(TypeName).Equals(TEXT("Toggle_Primary_Mode"), ESearchCase::IgnoreCase)
			? EElysiumWeaponModeType::TogglePrimaryMode
			: (FString(TypeName).Equals(TEXT("Secondary_Attack"), ESearchCase::IgnoreCase)
				? EElysiumWeaponModeType::SecondaryAttack : EElysiumWeaponModeType::Attack);
		Mode.Dmg = Dmg;
		Mode.BaseLethality = BaseLethality;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = AttackRate;
		Mode.AmmoCost = AmmoCost;
		Mode.AmmoFired = AmmoFired;
		return Mode;
	}

	FElysiumItemDef MakeDef(const TCHAR* Classname, EElysiumItemType Type, bool bWieldable = true)
	{
		FElysiumItemDef Def;
		Def.Classname = Classname;
		Def.PrintName = Classname;
		Def.Type = Type;
		Def.bWieldable = bWieldable;
		// No `playermodel`: a loose ground body is 9.8's concern and would only add embodiment
		// traffic to cases that are about the controller.
		return Def;
	}

	const TCHAR* const GFists   = TEXT("item_w_test_fists");
	const TCHAR* const GKatana  = TEXT("item_w_test_katana");
	const TCHAR* const GPistol  = TEXT("item_w_test_pistol");
	const TCHAR* const GShotgun = TEXT("item_w_test_shotgun");
	const TCHAR* const GTrinket = TEXT("item_g_test_trinket");
	const TCHAR* const GUnarmed = TEXT("item_w_unarmed");

	FElysiumItemTable MakeWeaponTable()
	{
		FElysiumItemTable Table;

		// Fists — the patch-first record's own numbers.
		FElysiumItemDef Fists = MakeDef(GFists, EElysiumItemType::WeaponMelee);
		// The selection order the shipped records author: melee is bucket 0, ranged bucket 1, and
		// `bucket_position` orders the column. `item_w_fists` is 0/0 in the real catalogue.
		Fists.Bucket = 0; Fists.BucketPosition = 0;
		Fists.Modes.Add(MakeMode(TEXT("Primary"), TEXT("Attack"),
			TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"), /*BaseLethality*/ 8, /*Attack_Rate*/ 0.5f));
		Table.Items.Add(MoveTemp(Fists));

		// An armed melee weapon: the combo reads `Melee` rather than `Brawl`.
		FElysiumItemDef Katana = MakeDef(GKatana, EElysiumItemType::WeaponMelee);
		Katana.Bucket = 0; Katana.BucketPosition = 6;
		Katana.Modes.Add(MakeMode(TEXT("Primary"), TEXT("Attack"),
			TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 12, 1.0f));
		Table.Items.Add(MoveTemp(Katana));

		// A firearm with two primary records and a secondary that toggles between them.
		FElysiumItemDef Pistol = MakeDef(GPistol, EElysiumItemType::WeaponFirearm);
		Pistol.Bucket = 1; Pistol.BucketPosition = 1;
		Pistol.AmmoType = TEXT("TestRound");
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 6;
		Pistol.ReloadTime = 99.0f;   // authored, and deliberately NOT the reload clock
		Pistol.Modes.Add(MakeMode(TEXT("Primary"), TEXT("Attack"),
			TEXT("2 Lethal Ranged_Combat DMG_BULLET"), 9, 0.4f, /*Ammo_Cost*/ 1, /*Ammo_Fired*/ 1));
		// The two recovered dead fields, authored on the record that fires: a burst range no runtime
		// path reads, and a skill gate no runtime path checks. Both are set high enough that any
		// consumer would change the shot's observable outcome.
		Pistol.Modes[0].BurstMin = 3;
		Pistol.Modes[0].BurstMax = 5;
		Pistol.Modes[0].SkillRequirement = 9;
		Pistol.Modes.Add(MakeMode(TEXT("PrimaryMode2"), TEXT("Attack"),
			TEXT("2 Lethal Ranged_Combat DMG_BULLET"), 9, 0.2f, 1, 1));
		Pistol.Modes.Add(MakeMode(TEXT("Secondary"), TEXT("Toggle_Primary_Mode"), TEXT(""), 0, 0.3f));
		Table.Items.Add(MoveTemp(Pistol));

		// A shell-at-a-time shotgun: one round spent, eight rays emitted.
		FElysiumItemDef Shotgun = MakeDef(GShotgun, EElysiumItemType::WeaponFirearm);
		Shotgun.Bucket = 1; Shotgun.BucketPosition = 8;
		Shotgun.AmmoType = TEXT("TestShell");
		Shotgun.MagazineSize = 4;
		Shotgun.DefaultAmmo = 4;
		Shotgun.bReloadSingle = true;
		Shotgun.Modes.Add(MakeMode(TEXT("Primary"), TEXT("Attack"),
			TEXT("2 Lethal Ranged_Combat DMG_BUCKSHOT"), 10, 0.8f, /*Ammo_Cost*/ 1, /*Ammo_Fired*/ 8));
		Table.Items.Add(MoveTemp(Shotgun));

		// Not a weapon family: it must stay a plain item.
		Table.Items.Add(MakeDef(GTrinket, EElysiumItemType::Generic, /*bWieldable*/ false));

		// The classname `Holster` falls back to. The real record's `item_type` is `hidden` and it
		// authors no `Activation` block at all — `item_w_fists` is what actually punches — so this
		// is deliberately NOT a controllable weapon.
		Table.Items.Add(MakeDef(GUnarmed, EElysiumItemType::Hidden));

		Table.Reindex();
		return Table;
	}

	// --- The world -----------------------------------------------------------------------------

	FElysiumEntityDefs MakeWeaponTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__weapon_test__");

		FElysiumEntityDef Victim;
		Victim.Classname = TEXT("npc_VPedestrian");
		Victim.TargetName = TEXT("victim");
		Victim.Origin = FVector(100.0f, 0.0f, 0.0f);
		FElysiumOutputDef Row;
		Row.Name = TEXT("OnDamaged");
		Row.Target = TEXT("damagedcount");
		Row.Input = TEXT("Add");
		Row.Param = TEXT("1");
		Victim.Outputs.Add(MoveTemp(Row));
		Defs.Defs.Add(MoveTemp(Victim));

		// A loose instance of each family, so the install branch is observable without a grant.
		for (const TCHAR* Classname : { GFists, GTrinket })
		{
			FElysiumEntityDef Loose;
			Loose.Classname = Classname;
			Loose.TargetName = Classname;
			Defs.Defs.Add(MoveTemp(Loose));
		}

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("damagedcount");
		Defs.Defs.Add(MoveTemp(Counter));
		return Defs;
	}

	void SeedHealth(FElysiumCombatCharacter& Char, int32 MaxHealth)
	{
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
		Char.RecomputeSheet();
	}

	int32 DamageTaken(const FElysiumCombatCharacter& Char)
	{
		return Char.Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	}

	FElysiumCombatCharacter* FindCharacter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent ? Ent->AsCombatCharacter() : nullptr;
	}

	// Grant a weapon and return its controller. The grant goes through `Weapon_Equip`, so the
	// returned weapon is the character's active one.
	FElysiumWeapon* GiveWeapon(FElysiumCombatCharacter& Char, const TCHAR* Classname)
	{
		const FElysiumEntityHandle Handle = Char.Inventory.GiveNamedItem(Char, Classname);
		FElysiumEntity* Ent = Char.World ? Char.World->Resolve(Handle) : nullptr;
		FElysiumItem* Item = Ent ? Ent->AsItem() : nullptr;
		return Item ? Item->AsWeapon() : nullptr;
	}

	// Face the attacker at the victim entity's origin. The fields are written directly: going
	// through SetRuntimeOrigin would drive a pawn the headless case does not have.
	void PlaceFacing(FElysiumCombatCharacter& Attacker, const FVector& At)
	{
		Attacker.Origin = At;
		Attacker.Angles = FVector::ZeroVector;   // yaw 0 -> forward is +X, which is where the victim is
	}

	bool NearlyEqual(double A, double B) { return FMath::Abs(A - B) < 1.0e-3; }

	// A fabricated `rules.txt` carrying only the `Melee_Reactions` block, with the shipped values.
	// Keys are inserted already folded, which is how the loader stores them.
	FElysiumRules MakeMarginRules()
	{
		FElysiumRules Rules;
		TMap<FString, FString>& Block = Rules.Blocks.Add(TEXT("melee_reactions"));
		Block.Add(TEXT("successesforattackerblockedmajor"), TEXT("-3"));
		Block.Add(TEXT("successesforattackerblocked"),      TEXT("-1"));
		Block.Add(TEXT("successesfordefenderdodgeattack"),  TEXT("-3"));
		Block.Add(TEXT("successesfordefenderdodge"),        TEXT("-1"));
		Block.Add(TEXT("successesfordefenderblock"),        TEXT("1"));
		Block.Add(TEXT("successesfordefenderblockstagger"), TEXT("4"));
		Rules.BlockOrder.Add(TEXT("melee_reactions"));
		return Rules;
	}
}

// =====================================================================================
// The pure rules: the authored record, the combo table, the playback rate and the
// `rules.txt` margin classifier.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponRulesTest, "Elysium.Substrate.Weapons.Rules",
	GElysiumTestFlags)
bool FElysiumWeaponRulesTest::RunTest(const FString&)
{
	// --- The `Activation` blocks parse into modes, in file order ---------------------------
	{
		const FString Text = TEXT(R"(
			WeaponData
			{
				"item_type"   "weapon_firearm"
				"reload_single" "1"
				"Disallow_FirearmsToBashing" "1"
				"wieldmodel_m" "weapons/probe/wield/w_m_probe.mdl"
				"wieldmodel_f" "weapons/probe/wield/w_f_probe.mdl"
				"anim_prefix" "probe"
				Magazine
				{
					"Type" "TestRound"
					"Size" "8"
					"ReloadTime" "3.5"
				}
				Activation
				{
					"Tag" "Primary"
					"Type" "Attack"
					"Dmg"  "2 Lethal Ranged_Combat DMG_BULLET"
					"BaseLethality" "9"
					"SkillRequirement" "5"
					"Attack_Rate" "0.4"
					"Ammo_Type" "TestRound"
					"Ammo_Cost" "1"
					"BurstMin" "5"
					"BurstMax" "3"
				}
				Activation
				{
					"Tag" "PrimaryMode2"
					"Type" "Attack"
					"Dmg"  "2 Lethal Ranged_Combat DMG_BULLET"
					"BaseLethality" "9"
					"Attack_Rate" "0.1"
					"allow_autofire" "1"
					"Ammo_Cost" "1"
					"Ammo_Fired" "8"
				}
				Activation
				{
					"Tag" "Secondary"
					"Type" "Toggle_Primary_Mode"
					"Attack_Rate" ".3"
				}
			}
		)");
		FElysiumItemDef Def;
		FString Error;
		if (!TestTrue(TEXT("a weapon record parses"),
			FElysiumItemTable::ParseText(TEXT("item_w_probe"), Text, Def, Error)))
		{
			return false;
		}
		TestTrue(TEXT("`reload_single` is a WeaponData-level key"), Def.bReloadSingle);
		TestTrue(TEXT("`Disallow_FirearmsToBashing` is read off the weapon record"),
			Def.bDisallowFirearmsToBashing);
		TestEqual(TEXT("wieldmodel_m parses onto the item def"), Def.WieldModelM,
			FString(TEXT("weapons/probe/wield/w_m_probe.mdl")));
		TestEqual(TEXT("wieldmodel_f parses onto the item def"), Def.WieldModelF,
			FString(TEXT("weapons/probe/wield/w_f_probe.mdl")));
		TestEqual(TEXT("anim_prefix parses onto the item def"), Def.AnimPrefix, FString(TEXT("probe")));
		TestEqual(TEXT("every Activation block becomes a mode"), Def.Modes.Num(), 3);
		if (Def.Modes.Num() == 3)
		{
			TestEqual(TEXT("modes keep file order"), Def.Modes[0].Tag, FString(TEXT("Primary")));
			TestEqual(TEXT("...and their authored type"), Def.Modes[0].Type,
				EElysiumWeaponModeType::Attack);
			TestEqual(TEXT("BaseLethality is loaded"), Def.Modes[0].BaseLethality, 9);
			// RE40: both `SkillRequirement` and the burst pair are parsed and never read back — dead
			// fields, stored for audit. The loader's clamp is retail's own, so it still applies.
			TestEqual(TEXT("...and SkillRequirement beside it, parsed as a dead field"),
				Def.Modes[0].SkillRequirement, 5);
			TestTrue(TEXT("Attack_Rate is loaded"),
				FMath::IsNearlyEqual(Def.Modes[0].AttackRate, 0.4f));
			TestEqual(TEXT("Ammo_Cost is the rounds spent"), Def.Modes[0].AmmoCost, 1);
			TestEqual(TEXT("an unauthored Ammo_Fired is ONE ray, not zero"),
				Def.Modes[0].AmmoFired, 1);
			TestEqual(TEXT("the loader forces BurstMin <= BurstMax on the dead burst pair"),
				Def.Modes[0].BurstMax, 5);
			TestEqual(TEXT("...keeping the authored minimum as written"), Def.Modes[0].BurstMin, 5);
			TestFalse(TEXT("allow_autofire defaults clear"), Def.Modes[0].bAllowAutofire);

			TestTrue(TEXT("a second primary record sets allow_autofire"),
				Def.Modes[1].bAllowAutofire);
			TestEqual(TEXT("Ammo_Fired is the ray count, distinct from Ammo_Cost"),
				Def.Modes[1].AmmoFired, 8);
			TestEqual(TEXT("...while Ammo_Cost stays one round"), Def.Modes[1].AmmoCost, 1);

			TestEqual(TEXT("a secondary toggle is its own mode type"), Def.Modes[2].Type,
				EElysiumWeaponModeType::TogglePrimaryMode);
		}
		TestTrue(TEXT("the parsed record is a controllable weapon"), Def.IsControllableWeapon());
		TestTrue(TEXT("a mode resolves by tag, case-insensitively"),
			Def.FindMode(TEXT("primarymode2")) != nullptr);
		TestTrue(TEXT("authored ReloadTime is loaded and is not the reload clock"),
			FMath::IsNearlyEqual(Def.ReloadTime, 3.5f));
	}

	// --- The `2COMBO` chance table -----------------------------------------------------------
	{
		const int32 Expected[] = { 0, 10, 25, 45, 70, 100 };
		for (int32 Rank = 0; Rank < 6; ++Rank)
		{
			TestEqual(*FString::Printf(TEXT("2COMBO chance at base rank %d"), Rank),
				ElysiumWeapons::ComboChancePercent(Rank), Expected[Rank]);
		}
		TestEqual(TEXT("a rank below the table clamps to its floor"),
			ElysiumWeapons::ComboChancePercent(-4), 0);
		TestEqual(TEXT("a rank above the table clamps to its ceiling"),
			ElysiumWeapons::ComboChancePercent(9), 100);
	}

	// --- The player attack sequence's playback rate ------------------------------------------
	{
		TestTrue(TEXT("rank 0 plays at 0.70"),
			FMath::IsNearlyEqual(ElysiumWeapons::MeleePlaybackRate(0), 0.70f));
		TestTrue(TEXT("each rank adds 0.03"),
			FMath::IsNearlyEqual(ElysiumWeapons::MeleePlaybackRate(5), 0.85f, KINDA_SMALL_NUMBER));
	}

	// --- The `rules.txt` margins, read rather than retyped -----------------------------------
	{
		const FElysiumRules Rules = MakeMarginRules();
		const FElysiumMeleeMargins Margins = FElysiumMeleeMargins::FromRules(Rules);
		if (!TestTrue(TEXT("the whole Melee_Reactions block reads back"), Margins.bValid))
		{
			return false;
		}
		TestEqual(TEXT("attacker blocked major"), Margins.AttackerBlockedMajor, -3);
		TestEqual(TEXT("attacker blocked"), Margins.AttackerBlocked, -1);
		TestEqual(TEXT("defender dodge attack"), Margins.DefenderDodgeAttack, -3);
		TestEqual(TEXT("defender dodge"), Margins.DefenderDodge, -1);
		TestEqual(TEXT("defender block"), Margins.DefenderBlock, 1);
		TestEqual(TEXT("defender block stagger"), Margins.DefenderBlockStagger, 4);

		using EA = EElysiumMeleeAttackerReaction;
		using ED = EElysiumMeleeDefenderReaction;
		// Every band, at its boundary and one past it. `<=` is the file's own comparison.
		TestEqual(TEXT("-4 is attacker blocked major"),
			ElysiumWeapons::ClassifyAttacker(Margins, -4), EA::BlockedMajor);
		TestEqual(TEXT("-3 is the blocked-major boundary"),
			ElysiumWeapons::ClassifyAttacker(Margins, -3), EA::BlockedMajor);
		TestEqual(TEXT("-2 falls through to attacker blocked"),
			ElysiumWeapons::ClassifyAttacker(Margins, -2), EA::Blocked);
		TestEqual(TEXT("-1 is the blocked boundary"),
			ElysiumWeapons::ClassifyAttacker(Margins, -1), EA::Blocked);
		TestEqual(TEXT("0 registers as a hit for the attacker"),
			ElysiumWeapons::ClassifyAttacker(Margins, 0), EA::Hit);

		TestEqual(TEXT("-4 is defender dodge attack"),
			ElysiumWeapons::ClassifyDefender(Margins, -4), ED::DodgeAttack);
		TestEqual(TEXT("-3 is the dodge-attack boundary"),
			ElysiumWeapons::ClassifyDefender(Margins, -3), ED::DodgeAttack);
		TestEqual(TEXT("-2 falls through to dodge"),
			ElysiumWeapons::ClassifyDefender(Margins, -2), ED::Dodge);
		TestEqual(TEXT("-1 is the dodge boundary"),
			ElysiumWeapons::ClassifyDefender(Margins, -1), ED::Dodge);
		TestEqual(TEXT("0 is a block"), ElysiumWeapons::ClassifyDefender(Margins, 0), ED::Block);
		TestEqual(TEXT("1 is the block boundary"),
			ElysiumWeapons::ClassifyDefender(Margins, 1), ED::Block);
		TestEqual(TEXT("2 falls through to block stagger"),
			ElysiumWeapons::ClassifyDefender(Margins, 2), ED::BlockStagger);
		TestEqual(TEXT("4 is the block-stagger boundary"),
			ElysiumWeapons::ClassifyDefender(Margins, 4), ED::BlockStagger);
		TestEqual(TEXT("everything above 4 is hit / knockback"),
			ElysiumWeapons::ClassifyDefender(Margins, 5), ED::HitKnockback);

		// An absent table classifies nothing rather than inventing a band.
		const FElysiumMeleeMargins Absent;
		TestEqual(TEXT("no margin table means no attacker band"),
			ElysiumWeapons::ClassifyAttacker(Absent, -9), EA::Unclassified);
		TestEqual(TEXT("no margin table means no defender band"),
			ElysiumWeapons::ClassifyDefender(Absent, 9), ED::Unclassified);
	}

	// --- The ranged clamp and the Kindred-only defence subtraction ---------------------------
	{
		TestEqual(TEXT("a mortal victim rolls no defence at all"),
			ElysiumWeapons::RangedRemainingLethality(9, /*bKindred*/ false, /*Defense*/ 4), 9);
		TestEqual(TEXT("a Kindred victim subtracts its net successes"),
			ElysiumWeapons::RangedRemainingLethality(9, true, 4), 5);
		TestEqual(TEXT("...floored at zero"),
			ElysiumWeapons::RangedRemainingLethality(2, true, 9), 0);
		TestEqual(TEXT("the ranged path clamps total lethality to at least one first"),
			ElysiumWeapons::RangedRemainingLethality(0, false, 0), 1);
	}

	// --- The two post-soak damage formulas ----------------------------------------------------
	{
		// Melee: `DamageInflicted * (BaseDamage + DamageModifier) * Multiplier`, the modifier being
		// the attacker's close-combat feat rating.
		TestEqual(TEXT("a zero modifier leaves lethality x base"),
			ElysiumWeapons::MeleeDamageTotal(/*Inflicted*/ 8, /*Base*/ 2, /*Modifier*/ 0, 1.0f), 16);
		TestEqual(TEXT("the attacker's feat rating adds to the base, inside the product"),
			ElysiumWeapons::MeleeDamageTotal(8, 2, 3, 1.0f), 40);
		TestEqual(TEXT("...and the envelope multiplier scales the whole product"),
			ElysiumWeapons::MeleeDamageTotal(8, 2, 3, 0.5f), 20);
		// `__ftol` truncates toward zero rather than rounding: 5 x (3 + 0) x 0.5 is 7.5, and the
		// commit spends 7.
		TestEqual(TEXT("a fractional melee product truncates toward zero"),
			ElysiumWeapons::MeleeDamageTotal(5, 3, 0, 0.5f), 7);

		// Ranged: `RemainingLethality * BaseDamage * (Volley_Fraction * Hitgroup_Scale)`.
		TestEqual(TEXT("a whole volley on one victim is lethality x base"),
			ElysiumWeapons::RangedDamageTotal(9, 2, 1.0f), 18);
		TestEqual(TEXT("half a volley is worth half the shot"),
			ElysiumWeapons::RangedDamageTotal(10, 2, 0.5f), 10);
		TestEqual(TEXT("...and a fractional ranged product truncates toward zero too"),
			ElysiumWeapons::RangedDamageTotal(3, 3, 0.5f), 4);

		// The volley hit share: the victim's rays over the shot's rays.
		TestTrue(TEXT("every ray on one victim is the whole share"),
			FMath::IsNearlyEqual(ElysiumWeapons::VolleyFraction(8, 8), 1.0f));
		TestTrue(TEXT("two of eight rays is a quarter share"),
			FMath::IsNearlyEqual(ElysiumWeapons::VolleyFraction(2, 8), 0.25f));
		TestTrue(TEXT("no rays on a victim is no share"),
			FMath::IsNearlyEqual(ElysiumWeapons::VolleyFraction(0, 8), 0.0f));
		TestTrue(TEXT("a share cannot exceed the volley"),
			FMath::IsNearlyEqual(ElysiumWeapons::VolleyFraction(99, 8), 1.0f));
		TestTrue(TEXT("a volley of no rays shares out nothing"),
			FMath::IsNearlyEqual(ElysiumWeapons::VolleyFraction(1, 0), 0.0f));

		// The hitgroup scale is the multiplier's other half and has no trace to report one yet.
		TestTrue(TEXT("an unreported hitgroup scores at the body scale"),
			FMath::IsNearlyEqual(ElysiumWeapons::DefaultHitgroupScale, 1.0f));
	}

	// --- The seams that are inert until their domain lands ------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Bare;
		Bare.MapName = TEXT("__weapon_seam_test__");
		World.Load(MoveTemp(Bare));
		World.SpawnPlayer();
		World.Activate(0.0);
		if (FElysiumPlayer* Player = World.FindPlayer())
		{
			TestEqual(TEXT("no shipped data authors an attack-speed value, so the scale is 1.0"),
				ElysiumWeapons::AttackSpeedScale(*Player), 1.0f);
			TestEqual(TEXT("the Potence floor is inert until the discipline layer lands"),
				ElysiumWeapons::ActivePotenceRank(*Player), 0);
		}
	}

	return true;
}

// =====================================================================================
// The install branch: the record's item type decides the class, and the modes' `Dmg`
// parses once when the entity spawns.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponInstallTest, "Elysium.Substrate.Weapons.Install",
	GElysiumTestFlags)
bool FElysiumWeaponInstallTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	if (const FElysiumClassDesc* Fists = Reg.Find(FName(GFists)))
	{
		TestEqual(TEXT("a weapon-family definition registers under CWeapon"), Fists->BaseName,
			ElysiumWeaponClassName());
	}
	else
	{
		AddError(TEXT("the weapon classname did not register"));
		return false;
	}
	if (const FElysiumClassDesc* Trinket = Reg.Find(FName(GTrinket)))
	{
		TestEqual(TEXT("a non-weapon definition stays on CBaseCombatWeapon"), Trinket->BaseName,
			ElysiumItemClassName());
	}
	if (const FElysiumClassDesc* Chain = Reg.Find(ElysiumWeaponClassName()))
	{
		TestEqual(TEXT("CWeapon sits under CBaseCombatWeapon"), Chain->BaseName,
			ElysiumItemClassName());
		TestTrue(TEXT("the attack commit is an input on CWeapon"),
			Reg.FindInput(*Chain, ElysiumWeaponCommitInput()) != nullptr);
		TestTrue(TEXT("...and so is the reload commit"),
			Reg.FindInput(*Chain, ElysiumWeaponReloadInput()) != nullptr);
	}

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeWeaponTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* LooseFists = World.FindByName(GFists);
	FElysiumEntity* LooseTrinket = World.FindByName(GTrinket);
	if (!TestNotNull(TEXT("the loose weapon spawned"), LooseFists)
		|| !TestNotNull(TEXT("the loose trinket spawned"), LooseTrinket))
	{
		return false;
	}
	FElysiumItem* FistsItem = LooseFists->AsItem();
	FElysiumItem* TrinketItem = LooseTrinket->AsItem();
	if (!TestNotNull(TEXT("both are items"), FistsItem) || !TestNotNull(TEXT("both are items"), TrinketItem))
	{
		return false;
	}
	FElysiumWeapon* Weapon = FistsItem->AsWeapon();
	TestNotNull(TEXT("a weapon-family classname builds the controller"), Weapon);
	TestNull(TEXT("a generic classname stays a plain item"), TrinketItem->AsWeapon());
	if (!Weapon)
	{
		return false;
	}

	// The mode's `Dmg` was parsed once at spawn, not at swing time.
	const FElysiumDmg& Dmg = Weapon->DamageForMode(0);
	TestEqual(TEXT("the mode descriptor carries its family"), Dmg.Family, EElysiumDmgFamily::Bashing);
	TestEqual(TEXT("...its base damage"), Dmg.BaseDamage, 2);
	TestEqual(TEXT("...and its attack feat"), Dmg.AttackFeat, FString(TEXT("Close_Combat_Brawl")));
	TestTrue(TEXT("the authored DMG_FIST aliases to the club bit"),
		Dmg.DmgMask == ElysiumDamage::DmgClub);
	TestEqual(TEXT("the primary mode is the first tagged Primary record"),
		Weapon->PrimaryModeIndex, 0);

	return true;
}

// =====================================================================================
// The equip funnel: OnEquipped/OnHolstered are the ONE door a real equip/holster
// transaction reaches the wield attach through, for an NPC and the player alike
// (`docs/project/plans/animation.md` -> LIFE4 "The equip funnels"). This suite is
// content-free and builds neither wearer a `model`, so neither stands a skeletal body
// (`FElysiumAnimating::BuildBody`) and `ApplyWieldVisual`'s
// `Wearer.GetSkeletalBody() == nullptr` early-out is what actually runs on both sides
// (docs/architecture/wielded-weapon-integration.md's live acceptance instrument is the
// green room's `elysium.gr_wield_check`, which does stand a body). What this proves is
// that the real transaction — `GiveNamedItem` -> `Equip` -> `SetActiveWeapon` ->
// `OnEquipped`/`OnHolstered` — runs the SAME call for both chain leaves with no crash and
// no player/NPC branch in its bookkeeping.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponWieldFunnelTest, "Elysium.Substrate.Weapons.WieldFunnel",
	GElysiumTestFlags)
bool FElysiumWeaponWieldFunnelTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeWeaponTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
	if (!TestNotNull(TEXT("the player entity exists"), Player)
		|| !TestNotNull(TEXT("the victim NPC exists"), Victim))
	{
		return false;
	}

	// Draw the katana on both — the real transaction (`GiveNamedItem` -> `Equip` ->
	// `SetActiveWeapon` -> `OnEquipped`), not a direct call into the visual layer. Neither call
	// site is guarded by `World->PlayerHandle() == ...` or any other player/NPC branch.
	FElysiumWeapon* PlayerKatana = GiveWeapon(*Player, GKatana);
	FElysiumWeapon* VictimKatana = GiveWeapon(*Victim, GKatana);
	if (!TestNotNull(TEXT("the player equips the katana"), PlayerKatana)
		|| !TestNotNull(TEXT("the NPC equips the katana"), VictimKatana))
	{
		return false;
	}
	TestTrue(TEXT("the katana is the player's active weapon"),
		Player->Inventory.Active(*Player) == PlayerKatana);
	TestTrue(TEXT("the katana is the NPC's active weapon"),
		Victim->Inventory.Active(*Victim) == VictimKatana);

	// Draw the fists on both, which holsters the katana through the same `SetActiveWeapon` ->
	// `OnHolstered` door before the fists' own `OnEquipped` runs. Nothing here should assert,
	// warn about a null owner, or otherwise misbehave on either wearer.
	FElysiumWeapon* PlayerFists = GiveWeapon(*Player, GFists);
	FElysiumWeapon* VictimFists = GiveWeapon(*Victim, GFists);
	TestTrue(TEXT("the fists become the player's active weapon"),
		Player->Inventory.Active(*Player) == PlayerFists);
	TestTrue(TEXT("the fists become the NPC's active weapon"),
		Victim->Inventory.Active(*Victim) == VictimFists);

	return true;
}

// =====================================================================================
// Melee: the accepted swing, the scheduled commit, the automatic combo, the opposed
// record and the damage that lands through the cycle-1 path.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponMeleeTest, "Elysium.Substrate.Weapons.Melee",
	GElysiumTestFlags)
bool FElysiumWeaponMeleeTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The swing, its commit, and damage through the cycle-1 route ------------------------
	{
		ElysiumRng::SeedAll(4242);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!TestNotNull(TEXT("the player exists"), Player)
			|| !TestNotNull(TEXT("the victim exists"), Victim))
		{
			return false;
		}
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);

		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted as a weapon"), Fists))
		{
			return false;
		}
		TestEqual(TEXT("Weapon_Equip makes the granted weapon active"),
			Player->Inventory.ActiveWeapon, Fists->Handle);

		const FElysiumWeapon::EVerdict Verdict = Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("the swing is accepted"), Verdict, FElysiumWeapon::EVerdict::Accepted);
		TestTrue(TEXT("a transaction is staged"), Fists->Swing.bActive);
		TestEqual(TEXT("...naming the logical activity"), Fists->Swing.Activity,
			FString(TEXT("ACT_MELEE_ATTACK")));
		TestEqual(TEXT("...and the acquired opponent"), Fists->Swing.Opponent, Victim->Handle);

		// No embodiment can resolve a clip headlessly, so the duration is the mode's authored
		// `Attack_Rate` and the rate is 0.70 (the attack-feat rating reads 0 with no rulebook).
		TestTrue(TEXT("the playback rate is 0.70 + 0.03 x rank"),
			FMath::IsNearlyEqual(Fists->Swing.PlaybackRate, 0.70f));
		TestTrue(TEXT("melee recovery is the clip duration over the playback rate"),
			NearlyEqual(Fists->Swing.RecoveryDeadline, 0.5 / 0.7));
		TestTrue(TEXT("...and both deadlines are held to it"),
			NearlyEqual(Fists->NextPrimaryAttackTime, 0.5 / 0.7)
			&& NearlyEqual(Fists->NextSecondaryAttackTime, 0.5 / 0.7));
		TestTrue(TEXT("the contact commit is scheduled inside the clip"),
			NearlyEqual(Fists->Swing.CommitTime, 0.5 * 0.5 / 0.7));

		// Producers enqueue; only queue service delivers.
		TestEqual(TEXT("no damage lands inside the accepted swing"), DamageTaken(*Victim), 0);
		World.Tick(0.1);
		TestEqual(TEXT("...nor before the commit instant"), DamageTaken(*Victim), 0);

		World.Tick(0.4);
		// lethality 8 - defense 0 - soak 0 = 8; total = 8 x (BaseDamage 2 + DamageModifier) x 1.
		// `DamageModifier` is the attacker's `Close_Combat_Brawl` rating, which reads 0 with no
		// rulebook loaded — the formula itself is asserted over its whole domain in the Rules suite.
		TestEqual(TEXT("the contact commits the melee total through the typed health commit"),
			DamageTaken(*Victim), 16);
		TestFalse(TEXT("the transaction is consumed"), Fists->Swing.bActive);

		const FElysiumMeleeRoll* Roll = Victim->FindMeleeRoll(Player->Handle);
		if (TestTrue(TEXT("the opposed record is staged on the defender"), Roll != nullptr))
		{
			TestEqual(TEXT("...keyed by the attacker"), Roll->Attacker, Player->Handle);
			TestEqual(TEXT("...carrying the weapon's total lethality"), Roll->Lethality, 8);
			TestEqual(TEXT("...and the defender's defence"), Roll->Defense, 0);
			TestEqual(TEXT("...and its soak"), Roll->Soak, 0);
			TestEqual(TEXT("the signed margin is lethality - defense - soak"), Roll->Margin(), 8);
		}
		TestEqual(TEXT("GetNumAttackSuccesses reads word 1 back"),
			Victim->GetNumAttackSuccesses(Player->Handle), 8);

		// --- The combat-stance clock: contact holds BOTH bodies for five seconds -------------
		// `m_flLastCombatAnimTime`, stamped by the melee transaction and read by the player gait
		// ladder's `CombatReady`/`Relaxed` predicates through `IsInCombatStance`.
		TestTrue(TEXT("the contact puts the attacker in combat stance"),
			Player->IsInCombatStance(World.NowSeconds()));
		TestTrue(TEXT("...and the victim"),
			Victim->IsInCombatStance(World.NowSeconds()));
		TestFalse(TEXT("...and the window closes five seconds after the contact"),
			Player->IsInCombatStance(Player->LastMeleeContactSeconds
				+ FElysiumCombatCharacter::CombatStanceHoldSeconds));

		World.Tick(0.5);
		TestEqual(TEXT("OnDamaged fires from the commit's real producer"),
			SaveTestCounterValue(World.FindByName(TEXT("damagedcount"))), 1.0f);

		// --- The next-attack deadline is a maximum operation ---------------------------------
		Fists->HoldAttacksUntil(100.0);
		Fists->HoldAttacksUntil(1.0);
		TestTrue(TEXT("a later deadline is never shortened"),
			NearlyEqual(Fists->NextPrimaryAttackTime, 100.0));
		TestEqual(TEXT("a press before the deadline is refused"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::NotReady);
		Fists->HoldAttacksUntil(200.0);
		TestTrue(TEXT("...and a later one still advances it"),
			NearlyEqual(Fists->NextSecondaryAttackTime, 200.0));
	}

	// --- The commit can miss: a victim that dies between swing and contact -------------------
	{
		ElysiumRng::SeedAll(7);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}

		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		Victim->SetDeathReportedForRestore(true);   // it died before the contact window
		World.Tick(0.4);
		TestEqual(TEXT("a commit against a dead victim misses"), DamageTaken(*Victim), 0);
		TestTrue(TEXT("...and stages no opposed record"),
			Victim->FindMeleeRoll(Player->Handle) == nullptr);
		TestFalse(TEXT("...and stamps no combat stance on either body — a whiff is not contact"),
			Player->IsInCombatStance(World.NowSeconds())
				|| Victim->IsInCombatStance(World.NowSeconds()));
	}

	// --- A stale commit is dropped rather than fired against a replaced transaction ----------
	{
		ElysiumRng::SeedAll(11);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		const int32 Serial = Fists->Swing.Serial;
		Fists->CommitQueuedAttack(Serial);            // the real commit
		TestEqual(TEXT("the first commit lands"), DamageTaken(*Victim), 16);
		Fists->CommitQueuedAttack(Serial);            // the queued duplicate
		TestEqual(TEXT("a repeat of the same serial is dropped"), DamageTaken(*Victim), 16);
		World.Tick(0.4);
		TestEqual(TEXT("...and so is the queued delivery"), DamageTaken(*Victim), 16);
	}

	// --- The automatic `2COMBO` substitution, at both ends of the table ----------------------
	{
		ElysiumRng::SeedAll(99);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			return false;
		}
		PlaceFacing(*Player, FVector::ZeroVector);

		// Fists read `Brawl` (ability slot 1). Rank 0 -> 0%, so the substitution never fires.
		Player->Sheet.SetBase(EC::Abilities, 1, 0);
		Player->Sheet.SetBase(EC::Abilities, 6, 0);
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("base rank 0 never substitutes the combo"), Fists->Swing.Activity,
			FString(TEXT("ACT_MELEE_ATTACK")));

		// Rank 5 -> 100%, so it always does. The controlling value is the BASE, so writing the base
		// is what moves it.
		Player->Sheet.SetBase(EC::Abilities, 1, 5);
		Fists->HoldAttacksUntil(0.0);
		Fists->NextPrimaryAttackTime = 0.0;
		Fists->NextSecondaryAttackTime = 0.0;
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("base rank 5 always substitutes the combo"), Fists->Swing.Activity,
			FString(TEXT("ACT_MELEE_ATTACK_2COMBO")));

		// An armed melee weapon reads `Melee` (slot 6) instead, so the Brawl rank above does not
		// carry over to it.
		FElysiumWeapon* Katana = GiveWeapon(*Player, GKatana);
		if (!TestNotNull(TEXT("the katana is granted"), Katana))
		{
			return false;
		}
		Katana->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("an armed weapon reads Melee, not Brawl"), Katana->Swing.Activity,
			FString(TEXT("ACT_MELEE_ATTACK")));
		Player->Sheet.SetBase(EC::Abilities, 6, 5);
		Katana->NextPrimaryAttackTime = 0.0;
		Katana->NextSecondaryAttackTime = 0.0;
		Katana->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("...and substitutes once Melee reaches rank 5"), Katana->Swing.Activity,
			FString(TEXT("ACT_MELEE_ATTACK_2COMBO")));
	}

	return true;
}

// =====================================================================================
// Ranged: the per-victim route, the ammo/ray distinction, dry fire, reload and the
// single-round interruption latch.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponRangedTest, "Elysium.Substrate.Weapons.Ranged",
	GElysiumTestFlags)
bool FElysiumWeaponRangedTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The per-victim body, and `Ammo_Cost` against `Ammo_Fired` --------------------------
	{
		ElysiumRng::SeedAll(5150);
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedHealth(*Victim, 200);

		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!TestNotNull(TEXT("the pistol is granted as a weapon"), Pistol))
		{
			return false;
		}
		TestEqual(TEXT("a fresh firearm spawns loaded with its Default_Size"),
			Pistol->MagazineCount, 6);

		// The transaction takes an explicit victim handle: the shot's trace is a producer that has
		// not landed, so nothing here invents one.
		const FElysiumWeapon::EVerdict Verdict =
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle);
		TestEqual(TEXT("the shot is accepted"), Verdict, FElysiumWeapon::EVerdict::Accepted);
		TestTrue(TEXT("the ranged schedule advances by the authored Attack_Rate"),
			NearlyEqual(Pistol->NextPrimaryAttackTime, 0.4));
		TestEqual(TEXT("the magazine is not spent at accept time"), Pistol->MagazineCount, 6);

		World.Tick(0.3);
		// lethality max(9 + 0, 1) = 9; multiplier = volley share 1/1 x hitgroup 1.0; value = 9 x
		// BaseDamage 2 x 1.0 = 18.
		TestEqual(TEXT("the shot commit spends Ammo_Cost rounds"), Pistol->MagazineCount, 5);
		TestEqual(TEXT("...and commits remaining lethality x BaseDamage x multiplier"),
			DamageTaken(*Victim), 18);
		// The two dead fields, proven inert at the only place a consumer could show: this mode
		// authors BurstMin 3 / BurstMax 5 and SkillRequirement 9 against a character with no skill
		// at all, and the press still spends exactly one round and commits exactly one shot's worth.
		if (const FElysiumWeaponMode* Mode = Pistol->ModeAt(0))
		{
			TestEqual(TEXT("the mode really authors a burst range"), Mode->BurstMax, 5);
			TestEqual(TEXT("...and a skill requirement"), Mode->SkillRequirement, 9);
		}
		TestFalse(TEXT("a burst range queues no further shot"), Pistol->Swing.bActive);
		World.Tick(0.35);
		TestEqual(TEXT("...so no second round is spent"), Pistol->MagazineCount, 5);
		TestEqual(TEXT("...and no second commit lands"), DamageTaken(*Victim), 18);

		// The shotgun: one shell, eight rays, and the ray count does NOT multiply the damage. The
		// rays enter only as the volley share's denominator, and one explicit victim takes the whole
		// volley — 8/8, which is one.
		FElysiumWeapon* Shotgun = GiveWeapon(*Player, GShotgun);
		if (!TestNotNull(TEXT("the shotgun is granted"), Shotgun))
		{
			return false;
		}
		Shotgun->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle);
		World.Tick(0.9);
		TestEqual(TEXT("Ammo_Cost spends one shell"), Shotgun->MagazineCount, 3);
		// lethality max(10, 1) = 10; multiplier = 8/8 x 1.0; value = 10 x 2 x 1.0 = 20, on top of
		// the pistol's 18.
		TestEqual(TEXT("Ammo_Fired is a ray count, not a damage multiplier"),
			DamageTaken(*Victim), 18 + 20);
	}

	// --- Dry fire: the empty-fire action advances BOTH attack timers -------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedHealth(*Victim, 100);
		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!Pistol)
		{
			return false;
		}
		Pistol->MagazineCount = 0;

		const FElysiumWeapon::EVerdict Verdict =
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle);
		TestEqual(TEXT("an empty magazine dry-fires rather than refusing"), Verdict,
			FElysiumWeapon::EVerdict::DryFire);
		TestFalse(TEXT("dry fire stages no transaction"), Pistol->Swing.bActive);
		TestTrue(TEXT("dry fire advances the primary timer"),
			NearlyEqual(Pistol->NextPrimaryAttackTime, 0.4));
		TestTrue(TEXT("...and the secondary timer with it"),
			NearlyEqual(Pistol->NextSecondaryAttackTime, 0.4));
		World.Tick(1.0);
		TestEqual(TEXT("dry fire commits no damage"), DamageTaken(*Victim), 0);
	}

	// --- Bulk reload: min(missing, reserve), and the end time is not `ReloadTime` -------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			return false;
		}
		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!Pistol)
		{
			return false;
		}
		Pistol->MagazineCount = 0;

		TestFalse(TEXT("a reload with no reserve does not start"), Pistol->BeginReload());
		Player->Inventory.AddReserve(TEXT("TestRound"), 10);
		TestTrue(TEXT("a reload starts with reserve, permission and missing capacity"),
			Pistol->BeginReload());
		TestTrue(TEXT("the end time comes from the resolved sequence, never authored ReloadTime"),
			NearlyEqual(Pistol->ReloadEndTime, 0.4));
		TestFalse(TEXT("a second request while one is live is refused"), Pistol->BeginReload());

		World.Tick(0.5);
		TestFalse(TEXT("the reload completed"), Pistol->bReloading);
		TestEqual(TEXT("a bulk reload fills min(missing capacity, reserve)"),
			Pistol->MagazineCount, 6);
		TestEqual(TEXT("...and removes the same amount from reserve"),
			Player->Inventory.Reserve(TEXT("TestRound")), 4);

		TestFalse(TEXT("a full magazine does not reload"), Pistol->BeginReload());
	}

	// --- `reload_single`: one round per cycle, re-entering until out of reserve --------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			return false;
		}
		FElysiumWeapon* Shotgun = GiveWeapon(*Player, GShotgun);
		if (!Shotgun)
		{
			return false;
		}
		Shotgun->MagazineCount = 0;
		Player->Inventory.AddReserve(TEXT("TestShell"), 3);

		TestTrue(TEXT("the single reload starts"), Shotgun->BeginReload());
		// Ticks land a hair past each cycle's deadline, same margin idiom as the rest of this file
		// (e.g. Tick(0.9) for an 0.8 deadline above) — the deadline is authored-float-derived and
		// re-derived from whatever Now the previous cycle actually fired at, so an exact-boundary
		// tick is not guaranteed to compare due against a double NowSeconds().
		World.Tick(0.9);
		TestEqual(TEXT("one round per cycle"), Shotgun->MagazineCount, 1);
		TestTrue(TEXT("...and it re-enters"), Shotgun->bReloading);
		World.Tick(1.8);
		TestEqual(TEXT("a second cycle adds a second round"), Shotgun->MagazineCount, 2);
		World.Tick(2.7);
		TestEqual(TEXT("the third round empties the reserve"), Shotgun->MagazineCount, 3);
		TestEqual(TEXT("...leaving nothing in reserve"),
			Player->Inventory.Reserve(TEXT("TestShell")), 0);
		TestFalse(TEXT("running out ends the transaction"), Shotgun->bReloading);
	}

	// --- The fire-intent interruption latch --------------------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		FElysiumWeapon* Shotgun = GiveWeapon(*Player, GShotgun);
		if (!Shotgun)
		{
			return false;
		}
		Shotgun->MagazineCount = 0;
		Player->Inventory.AddReserve(TEXT("TestShell"), 3);
		TestTrue(TEXT("the single reload starts"), Shotgun->BeginReload());

		TestEqual(TEXT("fire intent during a reload is reported, not accepted"),
			Shotgun->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Reloading);
		TestTrue(TEXT("...and sets the interruption latch"), Shotgun->bFireIntentDuringReload);

		World.Tick(0.9);
		TestEqual(TEXT("the cycle in flight still finishes its round"), Shotgun->MagazineCount, 1);
		TestFalse(TEXT("...and then the latch stops the re-entry"), Shotgun->bReloading);
		TestFalse(TEXT("the latch is consumed"), Shotgun->bFireIntentDuringReload);
		TestEqual(TEXT("the reserve kept what the interrupted cycles did not take"),
			Player->Inventory.Reserve(TEXT("TestShell")), 2);
	}

	// --- Mode dispatch: a `Toggle_Primary_Mode` secondary swaps the primary records ----------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			return false;
		}
		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!Pistol)
		{
			return false;
		}
		TestEqual(TEXT("the first primary record is in force"), Pistol->PrimaryModeIndex, 0);
		TestEqual(TEXT("the secondary press toggles the primary modes"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Secondary),
			FElysiumWeapon::EVerdict::ModeToggled);
		TestEqual(TEXT("...to the second primary record"), Pistol->PrimaryModeIndex, 1);
	}

	// --- Holster falls back to a carried `item_w_unarmed` ------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			return false;
		}
		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!Pistol)
		{
			return false;
		}
		// With nothing to fall back to the input is a reported no-op, not an invented empty hand.
		World.EnqueueInput(TEXT("!player"), FName(TEXT("Holster")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.Tick(0.1);
		TestEqual(TEXT("Holster with no carried unarmed leaves the active weapon alone"),
			Player->Inventory.ActiveWeapon, Pistol->Handle);

		const FElysiumEntityHandle Unarmed = Player->Inventory.GiveNamedItem(*Player, GUnarmed);
		// The grant equipped it; put the pistol back so the Holster input has work to do. Every
		// writer of the active-weapon handle republishes the camera class, including this one.
		Player->Inventory.ActiveWeapon = Pistol->Handle;
		Player->PublishEquippedCameraClass();
		World.EnqueueInput(TEXT("!player"), FName(TEXT("Holster")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.Tick(0.2);
		TestEqual(TEXT("Holster clears the active weapon back to item_w_unarmed"),
			Player->Inventory.ActiveWeapon, Unarmed);
	}

	return true;
}

// =====================================================================================
// The attack clip's chain: one weapon entity, two bodies (LIFE5).
//
// `FElysiumWeapon` is the same class in the player's hand and in a combatant's, so the
// activity it asks for cannot pick its translator off the weapon or off the stem. It picks
// it off the OWNER: `CBasePlayer` walks its one pass, `CAI_BaseNPC` walks the alternation
// and the availability probe. With the kind stamped rather than threaded, a player attack
// would resolve through the cast chain and `TranslatePlayerActivity` would never run.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponAnimBodyKindTest,
	"Elysium.Substrate.Weapons.AnimBodyKind", GElysiumTestFlags)
bool FElysiumWeaponAnimBodyKindTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// The resolve line for one attack, whichever stem the headless body reports: the chain is the
	// last token, so the assertion reads it without depending on a model being exported.
	auto ChainOf = [](const FElysiumRecordingServices& Services) -> FString
	{
		for (const FString& Call : Services.Calls)
		{
			if (Call.StartsWith(TEXT("ResolveNpcActivityClip")))
			{
				FString Chain;
				Call.Split(TEXT("body="), nullptr, &Chain);
				return Chain;
			}
		}
		return FString();
	};

	// --- The player's own weapon walks the player chain ---------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedHealth(*Victim, 100);

		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!TestNotNull(TEXT("the player is armed"), Pistol))
		{
			return false;
		}
		// The arming itself resolves clips; `ChainOf` reads the FIRST resolve line, so the record has
		// to start at the attack or the assertion below would be reading the equip's.
		Services.Calls.Reset();
		TestEqual(TEXT("the shot is accepted"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestTrue(TEXT("the attack asked for its clip"),
			Services.Saw(TEXT("ResolveNpcActivityClip")));
		TestEqual(TEXT("a player-owned weapon resolves through the player chain"),
			ChainOf(Services), FString(TEXT("player")));
	}

	// --- The same weapon in a cast hand walks the cast chain -----------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Shooter = FindCharacter(World, TEXT("victim"));
		if (!Player || !Shooter)
		{
			return false;
		}
		SeedHealth(*Player, 100);

		FElysiumWeapon* Pistol = GiveWeapon(*Shooter, GPistol);
		if (!TestNotNull(TEXT("the NPC is armed with the same record"), Pistol))
		{
			return false;
		}
		Services.Calls.Reset();
		TestEqual(TEXT("its shot is accepted too"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Player->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("an NPC-owned weapon resolves through the cast chain"),
			ChainOf(Services), FString(TEXT("cast")));
	}

	// --- The scene stand-in is a cast body, not the player it stands in for ---------------------
	// `!playercontroller` wears the player's model and is a `CAI_BaseNPC` duplicate, so a weapon in
	// its hand walks the cast chain. It is the one body where reading the kind off the stem or off
	// the model would answer `player` and be wrong.
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Stand = World.Resolve(World.CreatePlayerControllerEntity());
		FElysiumCombatCharacter* Controller = Stand ? Stand->AsCombatCharacter() : nullptr;
		if (!Player || !TestNotNull(TEXT("the scene stand-in spawned"), Controller))
		{
			return false;
		}
		SeedHealth(*Player, 100);

		FElysiumWeapon* Pistol = GiveWeapon(*Controller, GPistol);
		if (!TestNotNull(TEXT("the stand-in is armed with the same record"), Pistol))
		{
			return false;
		}
		Services.Calls.Reset();
		TestEqual(TEXT("the stand-in's shot is accepted"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Player->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("a stand-in wearing the player's model still resolves through the cast chain"),
			ChainOf(Services), FString(TEXT("cast")));
	}

	return true;
}

// =====================================================================================
// Inventory selection — the selector's authority (8.9's selector clause).
//
// `system/items.txt` declares the categories and the section each item type files
// under, so the cursor's vocabulary is authored rather than chosen. What this suite pins
// is that the section decides what a selection MEANS: a wielded section reaches the equip
// funnel by the same `SetActiveWeapon` door every other switch uses, and every other
// section moves a cursor without touching the hand.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInventorySelectionTest,
	"Elysium.Substrate.Inventory.Selection", GElysiumTestFlags)
bool FElysiumInventorySelectionTest::RunTest(const FString&)
{
	// The authored type -> section join, asserted first because everything below rides it. Four
	// types file under a section that is not their own name.
	TestTrue(TEXT("a melee weapon files under Weapon (Melee)"),
		ElysiumSectionForItemType(EElysiumItemType::WeaponMelee) == EElysiumInvSection::WeaponMelee);
	TestTrue(TEXT("a firearm files under Weapon (Ranged)"),
		ElysiumSectionForItemType(EElysiumItemType::WeaponFirearm) == EElysiumInvSection::WeaponRanged);
	TestTrue(TEXT("a blood pack files under General"),
		ElysiumSectionForItemType(EElysiumItemType::Bloodpack) == EElysiumInvSection::Generic);
	TestTrue(TEXT("ammunition files under the undisplayed None"),
		ElysiumSectionForItemType(EElysiumItemType::Ammo) == EElysiumInvSection::None);
	TestFalse(TEXT("None is not browsable"), ElysiumSectionIsBrowsable(EElysiumInvSection::None));
	TestFalse(TEXT("Hidden is not browsable"), ElysiumSectionIsBrowsable(EElysiumInvSection::Hidden));
	TestTrue(TEXT("armour is worn"), ElysiumItemTypeIsWorn(EElysiumItemType::Armor));
	TestTrue(TEXT("a firearm is wielded"), ElysiumItemTypeIsWielded(EElysiumItemType::WeaponFirearm));
	// The `slotN` keys index the same block, offset by the commented-out Disciplines section.
	TestTrue(TEXT("slot2 selects melee"),
		ElysiumSectionForSlot(2) == EElysiumInvSection::WeaponMelee);
	TestTrue(TEXT("slot6 selects General"),
		ElysiumSectionForSlot(6) == EElysiumInvSection::Generic);
	TestTrue(TEXT("slot1 addresses no surviving section"),
		ElysiumSectionForSlot(1) == EElysiumInvSection::None);

	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeWeaponTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the player entity exists"), Player))
	{
		return false;
	}

	// Granted deliberately out of selection order, so an order that came from the inventory rather
	// than from the records would be visible.
	FElysiumWeapon* Shotgun = GiveWeapon(*Player, GShotgun);   // ranged, bucket position 8
	FElysiumWeapon* Fists   = GiveWeapon(*Player, GFists);     // melee,  bucket position 0
	FElysiumWeapon* Pistol  = GiveWeapon(*Player, GPistol);    // ranged, bucket position 1
	FElysiumWeapon* Katana  = GiveWeapon(*Player, GKatana);    // melee,  bucket position 6
	Player->Inventory.GiveNamedItem(*Player, GTrinket);        // General, and not wieldable
	if (!TestNotNull(TEXT("the shotgun exists"), Shotgun) || !TestNotNull(TEXT("the fists exist"), Fists)
		|| !TestNotNull(TEXT("the pistol exists"), Pistol) || !TestNotNull(TEXT("the katana exists"), Katana))
	{
		return false;
	}

	{
		// Each section carries only its own types, in authored order.
		TArray<FElysiumItem*> Melee, Ranged, General, Undisplayed;
		ElysiumItems::CollectSection(*Player, EElysiumInvSection::WeaponMelee, Melee);
		ElysiumItems::CollectSection(*Player, EElysiumInvSection::WeaponRanged, Ranged);
		ElysiumItems::CollectSection(*Player, EElysiumInvSection::Generic, General);
		ElysiumItems::CollectSection(*Player, EElysiumInvSection::Hidden, Undisplayed);

		if (!TestEqual(TEXT("two melee weapons are carried"), Melee.Num(), 2)
			|| !TestEqual(TEXT("two ranged weapons are carried"), Ranged.Num(), 2))
		{
			return false;
		}
		TestEqual(TEXT("melee sorts by bucket position"), Melee[0]->ClassName(), FString(GFists));
		TestEqual(TEXT("melee sorts by bucket position"), Melee[1]->ClassName(), FString(GKatana));
		TestEqual(TEXT("ranged sorts by bucket position"), Ranged[0]->ClassName(), FString(GPistol));
		TestEqual(TEXT("ranged sorts by bucket position"), Ranged[1]->ClassName(), FString(GShotgun));
		TestEqual(TEXT("the trinket files under General"), General.Num(), 1);
		// An undisplayed section is empty by rule, whatever is carried.
		TestEqual(TEXT("an undisplayed section collects nothing"), Undisplayed.Num(), 0);
	}

	{
		// The katana was granted last, so it is in hand and its section is where a first cycle
		// starts — the cursor does not have to be primed.
		TestEqual(TEXT("the last grant is in hand"), Player->Inventory.ActiveWeapon, Katana->Handle);
		TestTrue(TEXT("the first cycle succeeds"), ElysiumItems::CycleSelection(World, 1));
		TestTrue(TEXT("the cursor started in the hand's own section"),
			Player->Inventory.CurrentSection == EElysiumInvSection::WeaponMelee);
		// Melee is [Fists, Katana]; forward from the katana wraps to the fists.
		TestEqual(TEXT("forward wraps inside the section"),
			Player->Inventory.ActiveWeapon, Fists->Handle);
		ElysiumItems::CycleSelection(World, 1);
		TestEqual(TEXT("forward reaches the katana"), Player->Inventory.ActiveWeapon, Katana->Handle);
		ElysiumItems::CycleSelection(World, -1);
		TestEqual(TEXT("backward returns to the fists"),
			Player->Inventory.ActiveWeapon, Fists->Handle);
		// Cycling never leaves the section: the ranged weapons are not in this rotation.
		TestTrue(TEXT("the cursor stayed in melee"),
			Player->Inventory.CurrentSection == EElysiumInvSection::WeaponMelee);
	}

	{
		// `slotN` moves the category; repeating it advances inside the one already selected.
		TestTrue(TEXT("the ranged category selects"),
			ElysiumItems::SelectSection(World, EElysiumInvSection::WeaponRanged));
		TestEqual(TEXT("the ranged category lands on its first entry"),
			Player->Inventory.ActiveWeapon, Pistol->Handle);
		ElysiumItems::SelectSection(World, EElysiumInvSection::WeaponRanged);
		TestEqual(TEXT("repeating the category advances inside it"),
			Player->Inventory.ActiveWeapon, Shotgun->Handle);

		// An undisplayed section is refused rather than silently selected.
		TestFalse(TEXT("an undisplayed section is refused"),
			ElysiumItems::SelectSection(World, EElysiumInvSection::Hidden));
		// A browsable section the player carries nothing in is a reported no-op.
		TestFalse(TEXT("an empty category does not select"),
			ElysiumItems::SelectSection(World, EElysiumInvSection::Powerups));
		TestEqual(TEXT("a refused category leaves the hand alone"),
			Player->Inventory.ActiveWeapon, Shotgun->Handle);
	}

	{
		// **The section decides what a selection means.** Browsing General moves a cursor and leaves
		// the hand exactly where it was — no non-weapon category can disarm the player.
		TestTrue(TEXT("the General category selects"),
			ElysiumItems::SelectSection(World, EElysiumInvSection::Generic));
		TestEqual(TEXT("a non-wielded selection does not touch the hand"),
			Player->Inventory.ActiveWeapon, Shotgun->Handle);
		TestTrue(TEXT("a non-wielded selection moves the item cursor"),
			Player->Inventory.SelectedItem.IsSet());
		ElysiumItems::CycleSelection(World, 1);
		TestEqual(TEXT("cycling General still does not touch the hand"),
			Player->Inventory.ActiveWeapon, Shotgun->Handle);
	}

	{
		// `lastinv` returns to the weapon held before the current one, and brings the cursor back to
		// its section so the next cycle continues where the hand actually is.
		TestTrue(TEXT("lastinv succeeds"), ElysiumItems::SelectLastWeapon(World));
		TestEqual(TEXT("lastinv returns to the previously held weapon"),
			Player->Inventory.ActiveWeapon, Pistol->Handle);
		TestTrue(TEXT("lastinv restores the weapon's own section"),
			Player->Inventory.CurrentSection == EElysiumInvSection::WeaponRanged);
	}

	{
		// The projection the HUD reads. It describes the same hand and the same browsed section, and
		// its ammunition comes off the magazine and the owner's reserve rather than off a fixture.
		Player->Inventory.AddReserve(TEXT("TestRound"), 12);
		FElysiumEquipmentView View;
		ElysiumItems::BuildInventoryView(World, View);

		TestTrue(TEXT("the view is valid with a player and a catalogue"), View.bValid);
		TestTrue(TEXT("the hand projects"), View.bEquippedValid);
		TestEqual(TEXT("the hand is the pistol"), View.Equipped.Classname, FString(GPistol));
		TestTrue(TEXT("a firearm carries a magazine"), View.Equipped.bHasMagazine);
		TestEqual(TEXT("the loaded magazine is the item's own"), View.Equipped.AmmoCurrent, 6);
		TestEqual(TEXT("the reserve is the owner's"), View.Equipped.AmmoReserve, 12);

		TestTrue(TEXT("the view carries the browsed section"),
			View.Section == EElysiumInvSection::WeaponRanged);
		if (!TestEqual(TEXT("the section's two rows project"), View.Entries.Num(), 2))
		{
			return false;
		}
		TestEqual(TEXT("the rows are in authored order"), View.Entries[0].Classname, FString(GPistol));
		TestEqual(TEXT("the cursor names the hand"), View.SelectedIndex, 0);
		// Nothing worn in this fixture, and an absent worn item is a cleared slot rather than a hole.
		TestFalse(TEXT("no armour is carried, so nothing is worn"), View.bWornValid);
	}

	{
		// A world with a player but no catalogue cannot name an item: the view stays invalid rather
		// than claiming the player carries nothing.
		ElysiumItems::Uninstall(Table);
		FElysiumEquipmentView View;
		ElysiumItems::BuildInventoryView(World, View);
		TestFalse(TEXT("no catalogue leaves the view invalid"), View.bValid);
		ElysiumItems::Install(Table);
	}

	return true;
}

}   // namespace ElysiumWeaponTests

#endif   // WITH_DEV_AUTOMATION_TESTS
