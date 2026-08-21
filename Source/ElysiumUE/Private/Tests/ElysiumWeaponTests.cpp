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
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumComboChain.h"  // the authored combo block the chain cases seed
#include "ElysiumMoveSolve.h"   // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumUserCmd.h"   // EElysiumButton — the player weapon frame's own button field
#include "ElysiumVariant.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumDiceTables.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumSwingContact.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRulebook.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumActionTables.h"      // the recovered player selector, for the name join
#include "Visual/ElysiumAnimationDriver.h"   // the real base-channel arbitration
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
	// A melee record no other case swings. `ShouldReportOnce` is keyed by classname and never resets
	// inside a process, so the once-per-weapon reach report can only be asserted on a name whose key
	// no earlier suite has already spent.
	const TCHAR* const GReachBlade = TEXT("item_w_test_reachblade");
	// A melee record authoring BOTH attack modes, which is what makes the heavy intent reachable.
	// Its own classname because a second mode added to a record another case swings would move that
	// case's mode indices.
	const TCHAR* const GHeavyBlade = TEXT("item_w_test_heavyblade");
	const TCHAR* const GPistol  = TEXT("item_w_test_pistol");
	const TCHAR* const GShotgun = TEXT("item_w_test_shotgun");
	const TCHAR* const GTrinket = TEXT("item_g_test_trinket");
	const TCHAR* const GUnarmed = TEXT("item_w_unarmed");
	const TCHAR* const GThrown  = TEXT("item_w_test_grenade");

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

		// The reach suite's own melee record — see `GReachBlade`.
		FElysiumItemDef Blade = MakeDef(GReachBlade, EElysiumItemType::WeaponMelee);
		Blade.Bucket = 0; Blade.BucketPosition = 7;
		Blade.Modes.Add(MakeMode(TEXT("Primary"), TEXT("Attack"),
			TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 12, 1.0f));
		Table.Items.Add(MoveTemp(Blade));

		// The two-mode melee record — see `GHeavyBlade`. The secondary is a real `Secondary_Attack`,
		// so the press reaches `BeginMeleeSwing` with the heavy intent rather than being refused for
		// having no mode.
		FElysiumItemDef HeavyBlade = MakeDef(GHeavyBlade, EElysiumItemType::WeaponMelee);
		HeavyBlade.Bucket = 0; HeavyBlade.BucketPosition = 9;
		HeavyBlade.Modes.Add(MakeMode(TEXT("Primary"), TEXT("Attack"),
			TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 12, 1.0f));
		HeavyBlade.Modes.Add(MakeMode(TEXT("Secondary"), TEXT("Secondary_Attack"),
			TEXT("4 Lethal Close_Combat_Melee DMG_SLASH"), 14, 1.2f));
		Table.Items.Add(MoveTemp(HeavyBlade));

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
		// The authored ranged `Range`, in Source units — the aim query's own distance. Only this
		// record states one: the shotgun below is deliberately left without so the stated stand-in
		// stays observable.
		Pistol.Modes[0].Range = 1500.0f;
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

		// The third controllable family. It is a weapon controller like the other two, and it takes a
		// DIFFERENT `Operator_HandleAnimEvent` body from either: `0x1024f030`, which accepts nothing
		// in the 3000..3999 band. Without a record of this type "non-melee means ranged" is
		// unfalsifiable.
		FElysiumItemDef Grenade = MakeDef(GThrown, EElysiumItemType::WeaponThrown);
		Grenade.Bucket = 1; Grenade.BucketPosition = 4;
		Grenade.Modes.Add(MakeMode(TEXT("Primary"), TEXT("Attack"),
			TEXT("4 Lethal Ranged_Combat DMG_BLAST"), 6, 1.0f));
		Table.Items.Add(MoveTemp(Grenade));

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

	// The acquisition-distance world. Two candidates straight ahead, both OUTSIDE the stated
	// `MeleeReachSourceUnits` stand-in (64 units = 162.56 cm) so the constant can never be what
	// acquires either: `mid` sits inside an authored 400 cm reach and `far` outside it. No candidate
	// stands inside the constant at all, which is what makes "the authored value is the query
	// distance" falsifiable rather than merely consistent.
	FElysiumEntityDefs MakeReachTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__weapon_reach_test__");
		for (const TPair<const TCHAR*, float>& Row :
			{ TPair<const TCHAR*, float>(TEXT("mid"), 250.0f),
			  TPair<const TCHAR*, float>(TEXT("far"), 900.0f) })
		{
			FElysiumEntityDef Candidate;
			Candidate.Classname = TEXT("npc_VPedestrian");
			Candidate.TargetName = Row.Key;
			Candidate.Origin = FVector(Row.Value, 0.0f, 0.0f);
			Defs.Defs.Add(MoveTemp(Candidate));
		}
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

		// The band a margin lands in decides the DEFENDER's reaction activity, so the two are
		// asserted joined here rather than as two facts that could drift apart
		// (`docs/vtmb/combat-and-damage.md` § "Block and stagger reactions").
		TestEqual(TEXT("margin 2 is the stagger band and plays ACT_BLOCK_HEAVY"),
			FString(ElysiumReactions::BlockActivityFor(
				ElysiumWeapons::ClassifyDefender(Margins, 2))),
			FString(TEXT("ACT_BLOCK_HEAVY")));
		TestEqual(TEXT("margin 1 is the block band and plays ACT_BLOCK"),
			FString(ElysiumReactions::BlockActivityFor(
				ElysiumWeapons::ClassifyDefender(Margins, 1))),
			FString(TEXT("ACT_BLOCK")));
		TestNull(TEXT("margin 5 is past every blocked class and names no block activity"),
			ElysiumReactions::BlockActivityFor(ElysiumWeapons::ClassifyDefender(Margins, 5)));

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
// Melee: the accepted swing, the SWEPT CONTACT WALK over the clip's own authored
// windows, the automatic combo, the opposed record and the damage that lands.
// =====================================================================================

namespace
{
	// The clip a melee swing resolves to in the contact cases, the bank the include DAG named, and
	// the bone the authored record sweeps. All three matter: the records are filed under the
	// attacking body's stem and the label, and the segment is stated in the bone's own frame.
	const TCHAR* const GSwingOwner = TEXT("cast_bank");
	const TCHAR* const GSwingClip = TEXT("swing_long");
	const TCHAR* const GSwingBone = TEXT("Bip01 R Hand");

	FElysiumSwingRecord SwingRec(float Start, float End)
	{
		FElysiumSwingRecord Record;
		Record.Start = Start;
		Record.End = End;
		Record.Bone = GSwingBone;
		Record.ACm = FVector(0.f, 0.f, 0.f);
		Record.BCm = FVector(30.f, 0.f, 0.f);
		return Record;
	}

	// Everything the contact walk reads, in one call: the activity seam pointed at one clip, a phase
	// standing on it, the bone the records sweep, and the records themselves.
	void ArmSwingSeam(FElysiumRecordingServices& Services, TArray<FElysiumSwingRecord> Records)
	{
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityLabel = GSwingClip;
		Services.ResolvedNpcActivityClip = GSwingClip;
		Services.ResolvedNpcActivityOwner = GSwingOwner;
		// The mode's own `Attack_Rate`, so the recovery arithmetic reads the same with a resolved clip
		// as it did with the headless fallback.
		Services.ClipSeconds = 0.5f;

		Services.bBodyClipPhaseSet = true;
		Services.BodyClipPhase = FElysiumClipPhase();
		Services.BodyClipPhase.OwnerStem = GSwingOwner;
		Services.BodyClipPhase.Label = GSwingClip;
		Services.BodyClipPhase.Cycle = 0.0f;
		Services.BodyClipPhase.Length = 0.5f;
		Services.BodyClipPhase.PlayId = 1;

		Services.BoneFrames.Add(FString(GSwingBone).ToLower(), FTransform::Identity);
		Services.SwingsByClip.Add(FString(GSwingClip).ToLower(), MoveTemp(Records));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponMeleeTest, "Elysium.Substrate.Weapons.Melee",
	GElysiumTestFlags)
bool FElysiumWeaponMeleeTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The swing, its swept contact, and damage through the cycle-1 route ------------------
	{
		ElysiumRng::SeedAll(4242);
		FElysiumRecordingServices Services;
		// One authored window over the middle of the clip. Nothing schedules a commit any more: the
		// descriptor states when the limb is live, and the walk tests exactly that.
		ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
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
		// The walk reads the pose off a body, so the swinger needs one.
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		if (!TestNotNull(TEXT("the player carries a body the walk can read"), Player->Visual))
		{
			return false;
		}
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		Services.SwingContacts = { Victim->Handle };

		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted as a weapon"), Fists))
		{
			return false;
		}
		TestEqual(TEXT("Weapon_Equip makes the granted weapon active"),
			Player->Inventory.ActiveWeapon, Fists->Handle);

		// The Dice stream as it stands before the press, so the draws this whole swing spends can be
		// counted rather than described. Moving the opposed roll from contact time to swing start
		// moves WHEN the stream is drawn from; it must not change how often.
		FRandomStream Expected = ElysiumRng::Stream(EElysiumRngStream::Dice);

		const FElysiumWeapon::EVerdict Verdict = Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("the swing is accepted"), Verdict, FElysiumWeapon::EVerdict::Accepted);
		TestTrue(TEXT("a transaction is staged"), Fists->Swing.bActive);
		// The `2COMBO` substitution's draw, and nothing else: with no rulebook loaded the defence and
		// soak difficulties are unavailable, so the opposed record is built with no roll behind it.
		Expected.RandRange(0, 99);
		TestEqual(TEXT("the accepted swing spends exactly the combo draw"),
			ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed(),
			Expected.GetCurrentSeed());
		TestTrue(TEXT("...and stages no opposed record yet — the roll belongs to the first live frame"),
			Victim->FindMeleeRoll(Player->Handle) == nullptr);
		TestEqual(TEXT("...naming the logical activity"), Fists->Swing.Activity,
			FString(TEXT("ACT_MELEE_ATTACK")));
		TestEqual(TEXT("...and the acquired opponent"), Fists->Swing.Opponent, Victim->Handle);

		// The resolved clip runs 0.5 s and the rate is 0.70 (the attack-feat rating reads 0 with no
		// rulebook).
		TestTrue(TEXT("the playback rate is 0.70 + 0.03 x rank"),
			FMath::IsNearlyEqual(Fists->Swing.PlaybackRate, 0.70f));
		// **Both of them crossed the play seam**, and neither is inferable from the transaction: the
		// forced ideal activity is what arms the movement lock, the reselection guard and the air
		// self-latch, and the rate is what makes the drawn clip, its cycle and its authored lunge run
		// at one speed. A play that carried neither would look identical everywhere else.
		// Joined, because `Saw` matches a PREFIX of one line and both of these ride at the tail.
		const FString PlayLog = Services.Log();
		TestTrue(FString::Printf(TEXT("the swing's playback rate reaches the play (%s)"), *PlayLog),
			Services.Saw(TEXT("PlayNpcClip")) && PlayLog.Contains(TEXT("rate=0.70")));
		TestTrue(TEXT("...as does the forced ideal activity, LOGICAL rather than translated"),
			PlayLog.Contains(TEXT("act=ACT_MELEE_ATTACK")));
		TestTrue(TEXT("melee recovery is the clip duration over the playback rate"),
			NearlyEqual(Fists->Swing.RecoveryDeadline, 0.5 / 0.7));
		TestTrue(TEXT("...and both deadlines are held to it"),
			NearlyEqual(Fists->NextPrimaryAttackTime, 0.5 / 0.7)
			&& NearlyEqual(Fists->NextSecondaryAttackTime, 0.5 / 0.7));
		TestTrue(TEXT("a melee swing estimates no commit instant at all"),
			NearlyEqual(Fists->Swing.CommitTime, 0.0) && !Fists->Swing.bAwaitingAnimEvent);

		// No queue service can produce a melee contact any more, however far the clock is advanced.
		World.Tick(0.1);
		World.Tick(0.4);
		TestEqual(TEXT("no queued route commits the swing"), DamageTaken(*Victim), 0);
		TestTrue(TEXT("...and the transaction is still standing"), Fists->Swing.bActive);

		// The first live frame: the roll and the notice are staged, before any contact test, and the
		// cycle is still outside the authored window.
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("the first live frame stages the roll and sweeps nothing"),
			DamageTaken(*Victim), 0);
		TestTrue(TEXT("...staging the opposed record on the defender at swing start"),
			Victim->FindMeleeRoll(Player->Handle) != nullptr);

		// A frame that walks the cycle into the authored window: the record is live and the sweep
		// lands.
		Services.BodyClipPhase.Cycle = 0.40f;
		World.AdvanceMeleeSwings(0.02f);
		// lethality 8 - defense 0 - soak 0 = 8; total = 8 x (BaseDamage 2 + DamageModifier) x 1.
		// `DamageModifier` is the attacker's `Close_Combat_Brawl` rating, which reads 0 with no
		// rulebook loaded — the formula itself is asserted over its whole domain in the Rules suite.
		TestEqual(TEXT("the swept contact commits the melee total through the typed health commit"),
			DamageTaken(*Victim), 16);

		// Hit-once: the same record cannot land twice while its window stays open.
		Services.BodyClipPhase.Cycle = 0.55f;
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("the record lands once for as long as its window is open"),
			DamageTaken(*Victim), 16);

		// The other half of the count: staging and contact together spend nothing more than the accept
		// already did. The dice moved instant, not quantity.
		TestEqual(TEXT("the whole swing spends the same draws it always did"),
			ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed(),
			Expected.GetCurrentSeed());

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
		// An AI producer's intent is refused by the deadline and nothing else. The busy path — where
		// the combo chain lives — hangs off `ItemPostFrame`'s press edge, which a schedule task never
		// produces, so this door still answers the plain refusal (`Elysium.Substrate.Weapons.Combo`).
		TestEqual(TEXT("a press before the deadline is refused"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::NotReady);
		Fists->HoldAttacksUntil(200.0);
		TestTrue(TEXT("...and a later one still advances it"),
			NearlyEqual(Fists->NextSecondaryAttackTime, 200.0));
	}

	// --- The contact can miss: a victim that dies between the swing and its window -----------
	{
		ElysiumRng::SeedAll(7);
		FElysiumRecordingServices Services;
		ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
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
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		Services.SwingContacts = { Victim->Handle };
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}

		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		Victim->SetDeathReportedForRestore(true);   // it died before the contact window
		World.AdvanceMeleeSwings(0.02f);
		Services.BodyClipPhase.Cycle = 0.40f;
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("a sweep onto a dead victim misses"), DamageTaken(*Victim), 0);
		TestTrue(TEXT("...and stages no opposed record"),
			Victim->FindMeleeRoll(Player->Handle) == nullptr);
		TestFalse(TEXT("...and stamps no combat stance on either body — a whiff is not contact"),
			Player->IsInCombatStance(World.NowSeconds())
				|| Victim->IsInCombatStance(World.NowSeconds()));
	}

	// --- A melee clip that declares NO records opens no contact window at all ----------------
	{
		// Retail's own shape, and also what a corpus exported before the `swings` column gives every
		// clip: the swing animates, recovers, and touches nothing.
		ElysiumRng::SeedAll(31);
		FElysiumRecordingServices Services;
		ArmSwingSeam(Services, {});
		Services.SwingsByClip.Reset();   // the column is absent entirely, not merely empty
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
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		Services.SwingContacts = { Victim->Handle };
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		for (float Cycle = 0.0f; Cycle <= 1.0f; Cycle += 0.1f)
		{
			Services.BodyClipPhase.Cycle = Cycle;
			World.AdvanceMeleeSwings(0.02f);
		}
		TestEqual(TEXT("a clip with no swing records never contacts"), DamageTaken(*Victim), 0);
		TestTrue(TEXT("...and stages no opposed record either — the walk never went live"),
			Victim->FindMeleeRoll(Player->Handle) == nullptr);
	}

	// --- The queued commit route is gone for melee -------------------------------------------
	{
		// A save written before the contact walk landed can still carry a queued `WeaponAttackCommit`
		// for a melee transaction. It is refused, loudly, and commits nothing.
		AddExpectedError(TEXT("dropped a queued melee commit"),
			EAutomationExpectedErrorFlags::Contains, 1);

		ElysiumRng::SeedAll(11);
		FElysiumRecordingServices Services;
		ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
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
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		Services.SwingContacts = { Victim->Handle };
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		Fists->CommitQueuedAttack(Fists->Swing.Serial);
		TestEqual(TEXT("a queued melee commit lands nothing"), DamageTaken(*Victim), 0);
		TestTrue(TEXT("...and leaves the transaction for the walk"), Fists->Swing.bActive);
	}

	// --- A later swing cannot consume an earlier swing's opposed record ----------------------
	{
		// The hole this closes: the roll's own 60-unit query selects whom the swing OPPOSES, and the
		// sweep can reach somebody else entirely. With the record unscoped, that body would be judged
		// on whatever margin the previous swing rolled against it — a landed hit nobody rolled for.
		ElysiumRng::SeedAll(4242);
		FElysiumRecordingServices Services;
		ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
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
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		SeedHealth(*Victim, 500);
		PlaceFacing(*Player, FVector::ZeroVector);
		Services.SwingContacts = { Victim->Handle };
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}

		// Swing 1: the victim is inside the roll cone, so a record is staged and the sweep lands.
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		const int32 FirstSerial = Fists->Swing.Serial;
		World.AdvanceMeleeSwings(0.02f);
		Services.BodyClipPhase.Cycle = 0.40f;
		World.AdvanceMeleeSwings(0.02f);
		const int32 AfterFirst = DamageTaken(*Victim);
		TestTrue(TEXT("the first swing lands"), AfterFirst > 0);

		// Swing 2: the victim is now far outside the roll's own 60-unit query, so nothing is staged
		// for it — but the sweep still reaches it, which is the whole point. The record swing 1 left
		// behind is still sitting on the body.
		Victim->Origin = FVector(100000.0, 0.0, 0.0);
		Fists->NextPrimaryAttackTime = 0.0;
		Fists->NextSecondaryAttackTime = 0.0;
		Services.BodyClipPhase.PlayId = 2;
		Services.BodyClipPhase.Cycle = 0.0f;
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestTrue(TEXT("the second swing takes its own serial"),
			Fists->Swing.Serial != FirstSerial);

		World.AdvanceMeleeSwings(0.02f);
		const FElysiumMeleeRoll* Stale = Victim->FindMeleeRoll(Player->Handle);
		if (!TestTrue(TEXT("the first swing's record is still on the victim"), Stale != nullptr))
		{
			return false;
		}
		TestEqual(TEXT("...still stamped with the swing that made it"), Stale->SwingSerial,
			FirstSerial);
		TestTrue(TEXT("...and the second swing staged none of its own"),
			Victim->FindMeleeRoll(Player->Handle, Fists->Swing.Serial) == nullptr);

		Services.BodyClipPhase.Cycle = 0.40f;
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("a sweep with no record of ITS OWN swing commits nothing"),
			DamageTaken(*Victim), AfterFirst);
	}

	// --- Two disjoint record groups land twice; one group's records land once ----------------
	{
		// The `2COMBO` shape. Records 0 and 1 share a window, so a hit through either marks both and
		// the pair lands once; record 2 opens later with no overlap, so it lands again.
		ElysiumRng::SeedAll(4242);
		FElysiumRecordingServices Services;
		ArmSwingSeam(Services, {
			SwingRec(0.20f, 0.35f), SwingRec(0.25f, 0.40f), SwingRec(0.70f, 0.85f) });
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
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		SeedHealth(*Victim, 500);
		PlaceFacing(*Player, FVector::ZeroVector);
		Services.SwingContacts = { Victim->Handle };
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		World.AdvanceMeleeSwings(0.02f);              // the first live frame: staging, no sweep

		Services.BodyClipPhase.Cycle = 0.30f;
		World.AdvanceMeleeSwings(0.02f);
		const int32 AfterFirstGroup = DamageTaken(*Victim);
		TestTrue(TEXT("the overlapping pair lands exactly one contact"), AfterFirstGroup == 16);

		Services.BodyClipPhase.Cycle = 0.38f;
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("...and does not land again while its window stays open"),
			DamageTaken(*Victim), AfterFirstGroup);

		// Past the first group entirely: both its records close and forget the victim.
		Services.BodyClipPhase.Cycle = 0.55f;
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("the gap between the groups contacts nothing"),
			DamageTaken(*Victim), AfterFirstGroup);

		Services.BodyClipPhase.Cycle = 0.75f;
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("the second, disjoint group lands its own contact"),
			DamageTaken(*Victim), AfterFirstGroup * 2);
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

	// --- The resolved clip's OWNER is staged with its label ----------------------------------
	{
		ElysiumRng::SeedAll(7373);
		FElysiumRecordingServices Services;
		// The shipped shape: a body's attack sequence lives on the shared bank the include DAG
		// named, not on the body's own stem.
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityLabel = TEXT("swing_long");
		Services.ResolvedNpcActivityClip = TEXT("swing_long");
		Services.ResolvedNpcActivityOwner = TEXT("cast_bank");
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
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("the staged transaction names the resolved clip"), Fists->Swing.ClipLabel,
			FString(TEXT("swing_long")));
		// Both halves. The label addresses the blocked-reaction row in the attacking body's own clip
		// slice; the owner names the bank that sequence came out of, which is the half of a
		// missing-column report the body stem cannot state. Both are read after the transaction has
		// been cleared, so both have to be staged. `Elysium.Substrate.BlockReaction.Producer` is the
		// other end of that carry.
		TestEqual(TEXT("...and the stem that owns it"), Fists->Swing.ClipOwnerStem,
			FString(TEXT("cast_bank")));
	}

	return true;
}

// =====================================================================================
// Where the melee swing spends its dice, exactly.
//
// The opposed roll is staged on the swing's first batched frame and CONSUMED at contact, so
// the Dice stream has to advance at the first and not at the second. Asserting that needs a
// rulebook: with no feat table and no `Damage_Info` difficulties, `RollFeatNet` returns before
// it reaches the resolver and both branches draw nothing, which would make the assertion pass
// against a build that rolled in either place.
//
// The tables are fabricated and bound through `ElysiumSheetRules::BindTables` — the fallback
// seam the sheet, the Discipline and the Stealth suites already stand on, and one the rulebook
// subsystem always outranks in a real run.
// =====================================================================================

namespace
{
	// The two feats a melee contact rolls, and the one it only rates. Every base names an
	// ATTRIBUTE, which `FeatValue` floors at 1 for the first ten slots — so a pool is the number
	// of bases and needs no stat table and no seeded sheet behind it.
	FElysiumFeat MakeRollFeat(const TCHAR* Name, int32 Index, int32 Bases, const TCHAR* Trait)
	{
		FElysiumFeat Feat;
		Feat.InternalName = Name;
		Feat.Name = Name;
		Feat.Index = Index;
		Feat.MaxValue = 30;   // above the pools below, so nothing here is clamped
		for (int32 i = 0; i < Bases; ++i)
		{
			FElysiumTraitRef Ref;
			Ref.Trait = Trait;
			Feat.Bases.Add(Ref);
		}
		return Feat;
	}

	// The defence pool is deliberately far above the fists' lethality, so the margin the contact
	// classifies is negative and the transaction exits before the damage commit. That keeps the
	// swing's OWN dice budget the only thing measured here: the damage resolver rolls soak of its
	// own, and pinning that belongs to the damage suite rather than to this one.
	constexpr int32 GDefencePool = 20;
	constexpr int32 GSoakPool = 2;
	constexpr int32 GDefenceDifficulty = 1;   // every face succeeds, so the margin cannot drift
	constexpr int32 GSoakDifficulty = 6;

	// The rulebook halves a melee transaction reads, bound for the duration of one case.
	struct FMeleeRollRules
	{
		FElysiumFeatTable Feats;
		FElysiumRules Rules;

		FMeleeRollRules()
		{
			Feats.Feats.Add(MakeRollFeat(TEXT("Defensive_Maneuvers"), 0, GDefencePool,
				TEXT("Dexterity")));
			Feats.Feats.Add(MakeRollFeat(TEXT("Soak_vs_Bashing"), 1, GSoakPool, TEXT("Stamina")));
			Feats.Feats.Add(MakeRollFeat(TEXT("Close_Combat_Brawl"), 2, 0, TEXT("Strength")));
			// The lookup IS the index on a hand-built table, exactly as it is for the item and quest
			// catalogues: without this every `Find` misses and both rolls silently return zero.
			Feats.Reindex();

			TMap<FString, FString>& Damage = Rules.Blocks.Add(TEXT("damage_info"));
			Damage.Add(TEXT("defense_difficulty_pc"), FString::FromInt(GDefenceDifficulty));
			Damage.Add(TEXT("defense_difficulty_npc"), FString::FromInt(GDefenceDifficulty));
			Damage.Add(TEXT("soak_difficulty_pc"), FString::FromInt(GSoakDifficulty));
			Damage.Add(TEXT("soak_difficulty_npc"), FString::FromInt(GSoakDifficulty));

			// The classifier's own block, so a contact reaching it names a band instead of warning
			// that the table never loaded.
			TMap<FString, FString>& Melee = Rules.Blocks.Add(TEXT("melee_reactions"));
			Melee.Add(TEXT("successesforattackerblockedmajor"), TEXT("-5"));
			Melee.Add(TEXT("successesforattackerblocked"), TEXT("-1"));
			Melee.Add(TEXT("successesfordefenderdodgeattack"), TEXT("-5"));
			Melee.Add(TEXT("successesfordefenderdodge"), TEXT("-3"));
			Melee.Add(TEXT("successesfordefenderblock"), TEXT("-1"));
			Melee.Add(TEXT("successesfordefenderblockstagger"), TEXT("0"));

			ElysiumSheetRules::FBoundTables Bound;
			Bound.Feats = &Feats;
			Bound.Rules = &Rules;
			ElysiumSheetRules::BindTables(Bound);
		}
		~FMeleeRollRules() { ElysiumSheetRules::BindTables(ElysiumSheetRules::FBoundTables()); }
	};

	// The Dice stream's position, so a case can compare two of them rather than describe a delta.
	int32 DiceSeed() { return ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponMeleeRollTest,
	"Elysium.Substrate.Weapons.MeleeRoll", GElysiumTestFlags)
bool FElysiumWeaponMeleeRollTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FMeleeRollRules Bound;
	constexpr int32 Seed = 0x4D524F4C;

	// The expected stream position after one accepted swing has staged its record: the `2COMBO`
	// substitution's draw, then the defender's `Defensive_Maneuvers` roll, then its soak. Replayed
	// on the live stream from the same seed rather than described, so this pins the COUNT, the
	// ARGUMENTS and the ORDER together — a build that rolled defence twice, or rolled at a
	// different difficulty, or rolled soak before defence, lands somewhere else.
	ElysiumRng::SeedAll(Seed);
	const int32 SeedAtStart = DiceSeed();
	ElysiumRng::Stream(EElysiumRngStream::Dice).RandRange(0, 99);
	const int32 SeedAfterCombo = DiceSeed();
	ElysiumDice::Roll(GDefencePool, GDefenceDifficulty, FElysiumDiceTable::Uniform());
	ElysiumDice::Roll(GSoakPool, GSoakDifficulty, FElysiumDiceTable::Uniform());
	const int32 SeedAfterStaging = DiceSeed();
	if (!TestTrue(TEXT("the replay actually spends dice, or the pin proves nothing"),
		SeedAfterCombo != SeedAtStart && SeedAfterStaging != SeedAfterCombo))
	{
		return false;
	}

	// --- A swing that finds an opponent: one roll, at swing start, and none at contact ---------
	{
		ElysiumRng::SeedAll(Seed);
		FElysiumRecordingServices Services;
		ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
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
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		Services.SwingContacts = { Victim->Handle };

		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted"), Fists))
		{
			return false;
		}

		TestEqual(TEXT("the swing is accepted"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("accepting spends the combo draw and nothing else"),
			DiceSeed(), SeedAfterCombo);
		TestTrue(TEXT("...and stages no opposed record: the roll is not the accept's"),
			Victim->FindMeleeRoll(Player->Handle) == nullptr);

		// The first batched frame. The roll and the notice are staged here, before any contact test.
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("the first batched frame spends the opposed roll, exactly once"),
			DiceSeed(), SeedAfterStaging);
		const FElysiumMeleeRoll* Staged = Victim->FindMeleeRoll(Player->Handle);
		if (!TestTrue(TEXT("...and the record it produced is on the defender"), Staged != nullptr))
		{
			return false;
		}
		TestEqual(TEXT("...stamped with the accepted swing's serial"), Staged->SwingSerial,
			Fists->Swing.Serial);
		const int32 StagedMargin = Staged->Margin();
		TestTrue(TEXT("...and the fixture's defence outweighs the fists, so no damage commit runs"),
			StagedMargin <= 0);

		// The contact. It CONSUMES the record; nothing here may reach the resolver again.
		Services.BodyClipPhase.Cycle = 0.40f;
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("the contact spends no dice — it consumes the staged record"),
			DiceSeed(), SeedAfterStaging);
		TestTrue(TEXT("...leaving the record it consumed untouched"),
			Victim->FindMeleeRoll(Player->Handle) != nullptr
				&& Victim->FindMeleeRoll(Player->Handle)->Margin() == StagedMargin);
		TestTrue(TEXT("...and the contact was real: both bodies are in combat stance"),
			Player->IsInCombatStance(World.NowSeconds())
				&& Victim->IsInCombatStance(World.NowSeconds()));
		TestEqual(TEXT("...with a non-positive margin committing no damage"),
			DamageTaken(*Victim), 0);
	}

	// --- A swing that finds nobody: no roll at all --------------------------------------------
	{
		ElysiumRng::SeedAll(Seed);
		FElysiumRecordingServices Services;
		ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
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
		Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		SeedHealth(*Victim, 100);
		PlaceFacing(*Player, FVector::ZeroVector);
		// Well outside the roll query's own 60 Source units, and outside the acquisition reach too.
		Victim->Origin = FVector(100000.0, 0.0, 0.0);

		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		World.AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("a swing that opens on empty air spends only the combo draw"),
			DiceSeed(), SeedAfterCombo);
		TestTrue(TEXT("...and stages no record anywhere"),
			Victim->FindMeleeRoll(Player->Handle) == nullptr);
	}

	return true;
}

// =====================================================================================
// The contact walk's batching: accumulation below one sub-step, the span cap above it,
// and the discontinuity guard between two batches.
//
// Retail walks on a server tick and its update either sees `dt == 0` and records nothing or
// sees a whole tick and walks. Under a render clock the same observable is accumulation, so a
// 144 Hz frame must not drop the span it covered — it has to survive into the batch that runs.
// =====================================================================================

namespace
{
	// One frame at a rate too fast for a batch to run on its own.
	constexpr float GFastFrame = 1.0f / 144.0f;

	// The whole walk fixture in one place: a bodied player, a live victim, a phase standing on the
	// swing clip, and one accepted swing. Returns with the walk unprimed and nothing swept.
	struct FBatchFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumWeapon* Fists = nullptr;

		bool Stand(FAutomationTestBase& Test, TArray<FElysiumSwingRecord> Records)
		{
			ElysiumRng::SeedAll(0x42415443);
			ArmSwingSeam(Services, MoveTemp(Records));
			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeWeaponTestDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);

			Player = World->FindPlayer();
			Victim = FindCharacter(*World, TEXT("victim"));
			if (!Test.TestNotNull(TEXT("the player exists"), Player)
				|| !Test.TestNotNull(TEXT("the victim exists"), Victim))
			{
				return false;
			}
			Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
			SeedHealth(*Victim, 5000);
			PlaceFacing(*Player, FVector::ZeroVector);
			Services.SwingContacts = { Victim->Handle };
			Fists = GiveWeapon(*Player, GFists);
			if (!Test.TestNotNull(TEXT("the fists are granted"), Fists))
			{
				return false;
			}
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
			return true;
		}

		// One frame of the walk at a stated cycle and delta.
		void Frame(float Cycle, float DeltaSeconds)
		{
			Services.BodyClipPhase.Cycle = Cycle;
			World->AdvanceMeleeSwings(DeltaSeconds);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponMeleeBatchTest,
	"Elysium.Substrate.Weapons.MeleeBatch", GElysiumTestFlags)
bool FElysiumWeaponMeleeBatchTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- A frame too short to batch records NOTHING, and its span is walked by the next one ----
	{
		// The window sits BEHIND the cycle the un-batched frame stands on and ahead of the one the
		// priming batch recorded, which is what makes the damage assertion below falsifying: a walk
		// that dropped the short frame's span would batch from 0.31 and step over [0.22, 0.24]
		// entirely, while one that batches from the last recorded cycle covers it.
		FBatchFixture F;
		if (!F.Stand(*this, { SwingRec(0.22f, 0.24f) }))
		{
			return false;
		}

		F.Frame(0.20f, 0.02f);   // the priming batch
		TestTrue(TEXT("the priming batch records where it stands"),
			FMath::IsNearlyEqual(F.Fists->Swing.PrevCycle, 0.20f));
		TestEqual(TEXT("...and sweeps nothing, having no previous position"),
			DamageTaken(*F.Victim), 0);

		F.Frame(0.31f, GFastFrame);
		TestEqual(TEXT("a 144 Hz frame runs no batch"), DamageTaken(*F.Victim), 0);
		TestTrue(TEXT("...and records nothing at all, so its span is not lost"),
			FMath::IsNearlyEqual(F.Fists->Swing.PrevCycle, 0.20f));
		TestTrue(TEXT("...it is held instead"), F.Fists->Swing.PendingSeconds > 0.0f);

		F.Frame(0.40f, GFastFrame);
		TestTrue(TEXT("the frame that crosses one sub-step batches the WHOLE accumulated span"),
			DamageTaken(*F.Victim) > 0);
		TestTrue(TEXT("...and records the cycle it reached"),
			FMath::IsNearlyEqual(F.Fists->Swing.PrevCycle, 0.40f));
		TestTrue(TEXT("...having consumed what it accumulated"),
			FMath::IsNearlyEqual(F.Fists->Swing.PendingSeconds, 0.0f));
	}

	// --- The span cap: a hitch walks at the cap rather than at hundreds of sub-steps -----------
	{
		FBatchFixture F;
		if (!F.Stand(*this, { SwingRec(0.0f, 1.0f) }))
		{
			return false;
		}
		F.Frame(0.10f, 0.02f);              // prime
		F.Services.SwingContactSweeps.Reset();

		// Three seconds on one frame. Uncapped that is 300 sub-steps; the cap makes it 25, and one
		// sweep is issued per live record per sub-step.
		F.Frame(0.50f, 3.0f);
		TestEqual(TEXT("a hitch is walked at the stated cap, not at its own length"),
			F.Services.SwingContactSweeps.Num(),
			ElysiumSwing::SubStepCount(ElysiumSwing::MaxBatchSeconds));
		TestTrue(TEXT("...and the batch still covers the cycle the clip reached"),
			FMath::IsNearlyEqual(F.Fists->Swing.PrevCycle, 0.50f));
	}

	// --- The discontinuity guard, with its own control ------------------------------------------
	// Record 0's window sits between the two batched cycles, so a batch that sweeps THROUGH the gap
	// reaches it and one that re-primes does not. Record 1 opens later, so either way the walk is
	// still alive afterwards.
	{
		FBatchFixture Control;
		if (!Control.Stand(*this, { SwingRec(0.30f, 0.35f), SwingRec(0.70f, 0.80f) }))
		{
			return false;
		}
		Control.Frame(0.20f, 0.02f);
		Control.Frame(0.50f, 0.02f);
		TestTrue(TEXT("without a jump, the batch sweeps through the window between two cycles"),
			DamageTaken(*Control.Victim) > 0);
	}
	{
		FBatchFixture F;
		if (!F.Stand(*this, { SwingRec(0.30f, 0.35f), SwingRec(0.70f, 0.80f) }))
		{
			return false;
		}
		F.Frame(0.20f, 0.02f);

		// A teleport between two batches. Swept as motion this would drag the limb across everything
		// on the line; re-priming throws away the crossing and keeps the swing.
		F.Player->Origin = FVector(50000.0, 0.0, 0.0);
		F.Frame(0.50f, 0.02f);
		TestEqual(TEXT("a body that jumped does not sweep through the window it crossed"),
			DamageTaken(*F.Victim), 0);
		TestTrue(TEXT("...and the walk re-primed rather than stopping"),
			FMath::IsNearlyEqual(F.Fists->Swing.PrevCycle, 0.50f));

		F.Frame(0.75f, 0.02f);
		TestTrue(TEXT("the next record still lands, from where the limb actually is"),
			DamageTaken(*F.Victim) > 0);
	}

	// --- What ends the transaction: a stopped clip, past the recovery deadline ------------------
	// Melee has no commit event and no queued half, so the walk is the only thing that can close a
	// swing. A transaction left standing would let a later play of the same clip label re-open
	// contact for a swing that ended, against a record its own serial still matches.
	{
		FBatchFixture F;
		if (!F.Stand(*this, { SwingRec(0.30f, 0.60f) }))
		{
			return false;
		}
		F.Frame(0.20f, 0.02f);
		TestTrue(TEXT("the transaction stands while its clip plays"), F.Fists->Swing.bActive);

		// The clip is on no polled channel, but the recovery has not passed: that is also the shape
		// of a swing whose pose layer has not armed the clip yet, and it must not end anything.
		F.Services.bBodyClipPhaseSet = false;
		F.World->AdvanceMeleeSwings(0.02f);
		TestTrue(TEXT("a clip the pose layer is not publishing does not end it on its own"),
			F.Fists->Swing.bActive);

		F.World->Tick(2.0);
		F.World->AdvanceMeleeSwings(0.02f);
		TestFalse(TEXT("...but a stopped clip past the recovery deadline retires it"),
			F.Fists->Swing.bActive);
	}

	return true;
}

// =====================================================================================
// The combo chain: what a press does while an attack is already running.
//
// `docs/vtmb/combat-and-damage.md` § "Melee attack, combo, block and damage". A press arriving while
// the weapon is busy takes the hand-off route instead of starting a second swing: it commits the
// PLAYING clip's authored successor when the press lands inside that clip's window, and is ignored
// outright otherwise. The hand-off keeps the playback rate, keeps the logical activity, does NOT
// push the next-attack deadline, and restages the swing exactly as an accepted one — a fresh serial,
// a fresh roll and a walk that starts over on the new clip.
//
// The route hangs off `ItemPostFrame`'s primary PRESS EDGE and nowhere else, which is the whole of
// why the chain is the player's: a schedule task calls `AttackIntent` and produces no edge.
// =====================================================================================

namespace
{
	// The successor the fixture's attack hands off to, and the bank both clips come out of.
	const TCHAR* const GComboNext = TEXT("Fists_attack_W2");

	FElysiumComboChain ComboBlock(const TCHAR* Successor, float Open, float Close, float Hold)
	{
		FElysiumComboChain Block;
		Block.bStated = true;
		Block.Mask = ElysiumCombo::InForward;
		Block.Chain = Successor;
		Block.WindowOpen = Open;
		Block.WindowClose = Close;
		Block.HoldCycle = Hold;
		return Block;
	}

	// A bodied player holding the fists, with the swing seam armed and the press edge driven through
	// the world's own weapon frame — which is the only door the busy path hangs off.
	struct FComboFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumWeapon* Fists = nullptr;

		bool Stand(FAutomationTestBase& Test)
		{
			ElysiumRng::SeedAll(0x434f4d42);
			ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeWeaponTestDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);

			Player = World->FindPlayer();
			Victim = FindCharacter(*World, TEXT("victim"));
			if (!Test.TestNotNull(TEXT("the player exists"), Player)
				|| !Test.TestNotNull(TEXT("the victim exists"), Victim))
			{
				return false;
			}
			Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
			SeedHealth(*Victim, 5000);
			PlaceFacing(*Player, FVector::ZeroVector);
			Services.SwingContacts = { Victim->Handle };
			Fists = GiveWeapon(*Player, GFists);
			return Test.TestNotNull(TEXT("the fists are granted"), Fists);
		}

		// The successor's own vocabulary entry: the bank that owns it, and the records its swing
		// sweeps. Seeded apart from the chain block so a case can state a chain whose target the body
		// does not name — the two shipped authoring bugs.
		void DeclareSuccessor(const TCHAR* Label)
		{
			Services.ClipOwnerByLabel.Add(FString(Label).ToLower(), GSwingOwner);
			Services.SwingsByClip.Add(FString(Label).ToLower(), { SwingRec(0.30f, 0.60f) });
		}

		void Chains(const TCHAR* Label, const FElysiumComboChain& Block)
		{
			Services.ComboByClip.Add(FString(Label).ToLower(), Block);
		}

		// Where the pose layer says the body is standing. `PlayId` moves with the label, because a
		// different clip on the channel is a different play.
		void StandOn(const TCHAR* Label, float Cycle, uint32 PlayId = 1)
		{
			Services.BodyClipPhase.Label = Label;
			Services.BodyClipPhase.Cycle = Cycle;
			Services.BodyClipPhase.PlayId = PlayId;
		}

		void Frame(double At, uint64 Buttons)
		{
			World->SetPlayerButtons(Buttons);
			World->Tick(At);
			World->UpdatePlayerWeaponFrame();
		}

		// One press: a frame with the button up, then a frame with it down. That pair IS the edge the
		// weapon reads, and driving it any other way would not exercise the producer.
		void Press(double At)
		{
			Frame(At, 0);
			Frame(At, static_cast<uint64>(EElysiumButton::Attack));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponComboTest, "Elysium.Substrate.Weapons.Combo",
	GElysiumTestFlags)
bool FElysiumWeaponComboTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// --- The hand-off, and everything it carries across --------------------------------------------
	{
		FComboFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Chains(GSwingClip, ComboBlock(GComboNext, 0.5f, 0.9f, 0.91f));
		F.DeclareSuccessor(GComboNext);

		F.Press(0.0);
		if (!TestEqual(TEXT("the first press swings"), F.Fists->AcceptedSwingCount(), 1))
		{
			return false;
		}
		const int32 FirstSerial = F.Fists->Swing.Serial;
		const double Deadline = F.Fists->Swing.RecoveryDeadline;
		const float Rate = F.Fists->Swing.PlaybackRate;

		// Walk the swing far enough that its roll is staged and its own window has opened, so the
		// hand-off is asserted against a transaction with real state to throw away.
		F.StandOn(GSwingClip, 0.35f);
		F.World->AdvanceMeleeSwings(0.02f);
		F.StandOn(GSwingClip, 0.45f);
		F.World->AdvanceMeleeSwings(0.02f);
		TestTrue(TEXT("the first swing staged its opposed roll"), F.Fists->Swing.bContactStaged);

		// The press, inside the authored window.
		F.StandOn(GSwingClip, 0.70f);
		const int32 SeedBeforeChain = DiceSeed();
		F.Press(0.10);

		TestEqual(TEXT("the press committed the clip's authored successor"),
			F.Fists->Swing.ClipLabel, FString(GComboNext));
		TestEqual(TEXT("...off the bank the vocabulary named"),
			F.Fists->Swing.ClipOwnerStem, FString(GSwingOwner));
		TestEqual(TEXT("...as an accepted swing, with a fresh serial"),
			F.Fists->Swing.Serial, FirstSerial + 1);
		TestEqual(TEXT("...counted as one"), F.Fists->AcceptedSwingCount(), 2);
		TestTrue(TEXT("...at the SAME playback rate"),
			FMath::IsNearlyEqual(F.Fists->Swing.PlaybackRate, Rate));
		TestTrue(TEXT("...without pushing the next-attack deadline"),
			NearlyEqual(F.Fists->Swing.RecoveryDeadline, Deadline)
			&& NearlyEqual(F.Fists->NextPrimaryAttackTime, Deadline));
		TestEqual(TEXT("...leaving the logical activity the ordinary attack"),
			F.Fists->Swing.Activity, FString(TEXT("ACT_MELEE_ATTACK")));
		TestFalse(TEXT("...with a fresh roll to stage"), F.Fists->Swing.bContactStaged);
		TestEqual(TEXT("...and a walk that has met no play yet"),
			static_cast<int32>(F.Fists->Swing.WalkPlayId), 0);
		TestTrue(TEXT("the successor was played from the start of its own clip"),
			F.Services.Saw(FString::Printf(TEXT("PlayNpcClip male_pc %s loop=0"), GComboNext)));

		// **The RNG pin.** A hand-off is not a new `RequestActivity`: it draws no 2COMBO chance and
		// spends nothing off any stream.
		TestEqual(TEXT("a chain hand-off spends no randomness"), DiceSeed(), SeedBeforeChain);

		// And the sweep follows the NEW clip: a fresh play, its own records, its own roll.
		const int32 Before = DamageTaken(*F.Victim);
		F.StandOn(GComboNext, 0.20f, /*PlayId*/ 2);
		F.World->AdvanceMeleeSwings(0.02f);
		TestEqual(TEXT("the walk primes on the successor's own play"),
			static_cast<int32>(F.Fists->Swing.WalkPlayId), 2);
		F.StandOn(GComboNext, 0.40f, /*PlayId*/ 2);
		F.World->AdvanceMeleeSwings(0.02f);
		TestTrue(TEXT("...and the successor's own window lands its contact"),
			DamageTaken(*F.Victim) > Before);
	}

	// --- A press outside the window is IGNORED: no queue, no restart -------------------------------
	{
		FComboFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Chains(GSwingClip, ComboBlock(GComboNext, 0.5f, 0.9f, 0.91f));
		F.DeclareSuccessor(GComboNext);

		F.Press(0.0);
		F.StandOn(GSwingClip, 0.30f);   // before the window opens
		F.Press(0.10);
		TestEqual(TEXT("a press before the window commits nothing"), F.Fists->AcceptedSwingCount(), 1);
		TestEqual(TEXT("...and leaves the attack on its own clip"),
			F.Fists->Swing.ClipLabel, FString(GSwingClip));

		F.StandOn(GSwingClip, 0.95f);   // after it closes
		F.Press(0.20);
		TestEqual(TEXT("a press after the window commits nothing either"),
			F.Fists->AcceptedSwingCount(), 1);

		// Nothing was banked: walking back INTO the window does not fire the press that missed it.
		F.StandOn(GSwingClip, 0.70f);
		F.Frame(0.30, static_cast<uint64>(EElysiumButton::Attack));
		TestEqual(TEXT("a missed press is spent, not queued for the window"),
			F.Fists->AcceptedSwingCount(), 1);
	}

	// --- A terminal attack — and every `2COMBO` clip — chains nothing ------------------------------
	{
		FComboFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		// No block seeded at all: the shape all but 208 of the install's descriptors have.
		F.Press(0.0);
		F.StandOn(GSwingClip, 0.70f);
		F.Press(0.10);
		TestEqual(TEXT("an attack that names no successor spends the press"),
			F.Fists->AcceptedSwingCount(), 1);

		// A stated block whose successor is empty is the same answer, and it is a different row: the
		// 12 shipped descriptors that name a dodge activity and no chain at all.
		F.Chains(GSwingClip, ComboBlock(TEXT(""), 0.0f, 1.0f, 1.0f));
		F.Press(0.20);
		TestEqual(TEXT("a block naming no successor spends it too"), F.Fists->AcceptedSwingCount(), 1);
	}

	// --- The dangling link: the target the body's own bank never defines ---------------------------
	// Four shipped links, across both sexes' `fists` and `katana` banks, chain to a sequence their own
	// bank does not carry. `LookupSequence` answers -1, the chain is silently dead, and the press is
	// ignored — the authored bug is reported once and never repaired.
	{
		FComboFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Chains(GSwingClip, ComboBlock(TEXT("Fists_attack_W3"), 0.5f, 0.9f, 0.91f));
		// Deliberately NOT declared: the vocabulary does not name `Fists_attack_W3`.

		F.Press(0.0);
		F.StandOn(GSwingClip, 0.70f);
		F.Press(0.10);
		TestEqual(TEXT("a chain whose target does not resolve commits nothing"),
			F.Fists->AcceptedSwingCount(), 1);
		TestEqual(TEXT("...and leaves the attack where it was"),
			F.Fists->Swing.ClipLabel, FString(GSwingClip));
		TestTrue(TEXT("...having really asked the vocabulary for it"),
			F.Services.Saw(TEXT("NpcClipOwner male_pc Fists_attack_W3 -> -")));
		TestTrue(TEXT("...and nothing was played"),
			!F.Services.Saw(TEXT("PlayNpcClip male_pc Fists_attack_W3")));
	}

	// --- `w_hold` BELOW `w_close`: the busy predicate ends before the window does ------------------
	// `katana_running_attack` authors 0.25/1.0/0.9. Past the next-attack deadline the CLOCK arm is
	// gone, so the predicate alone decides — and it releases at 0.9 while the hand-off window stays
	// open to 1.0. The pair below is the whole difference between reading `w_hold` and deriving it.
	{
		FComboFixture Inside;
		if (!Inside.Stand(*this))
		{
			return false;
		}
		Inside.Chains(GSwingClip, ComboBlock(GComboNext, 0.25f, 1.0f, 0.9f));
		Inside.DeclareSuccessor(GComboNext);
		Inside.Press(0.0);
		const double Deadline = Inside.Fists->Swing.RecoveryDeadline;
		TestTrue(TEXT("the press below is past the next-attack deadline"), Deadline < 1.0);

		Inside.StandOn(GSwingClip, 0.50f);   // below the hold: still busy
		Inside.Press(1.0);
		TestEqual(TEXT("below the hold, a press past the deadline still chains"),
			Inside.Fists->Swing.ClipLabel, FString(GComboNext));
		TestTrue(TEXT("...and the deadline is still not pushed"),
			NearlyEqual(Inside.Fists->Swing.RecoveryDeadline, Deadline));
	}
	{
		FComboFixture Past;
		if (!Past.Stand(*this))
		{
			return false;
		}
		Past.Chains(GSwingClip, ComboBlock(GComboNext, 0.25f, 1.0f, 0.9f));
		Past.DeclareSuccessor(GComboNext);
		Past.Press(0.0);
		const double Deadline = Past.Fists->Swing.RecoveryDeadline;

		Past.StandOn(GSwingClip, 0.95f);   // above the hold: the weapon is free
		Past.Press(1.0);
		TestEqual(TEXT("above the hold the press is an ordinary swing, not a hand-off"),
			Past.Fists->Swing.ClipLabel, FString(GSwingClip));
		TestEqual(TEXT("...a second accepted swing"), Past.Fists->AcceptedSwingCount(), 2);
		TestTrue(TEXT("...which DOES push the deadline, unlike a hand-off"),
			Past.Fists->Swing.RecoveryDeadline > Deadline);
	}

	return true;
}

// =====================================================================================
// The band a melee swing claims the base channel at, and the air fork (LIFE5).
//
// A melee swing replaces the BASE pose, while the ranged and reload arms write only a layer and
// leave the base to the gait ladder. Two consequences are asserted here, because both are invisible
// in the swing's own state and visible only in what the play seam was handed:
//
//  * a swing claims the base channel ABOVE a travelling body's own locomotion publish, or the
//    arbitration consumes it and the attacker walks through an attack that animates nothing;
//  * a player whose ideal activity is one of the recovered airborne phases swings the air form, and
//    nobody else ever does.
// =====================================================================================

namespace
{
	// The band token a recorded `PlayNpcClip` line states. The band rides at the tail of the line
	// (`Tests/ElysiumTestServices.h`), so this reads what the PRODUCER claimed rather than which door
	// it went through.
	FString BandOfPlay(const FElysiumRecordingServices& Services, const FString& Prefix)
	{
		for (const FString& Call : Services.Calls)
		{
			FString Tail;
			if (!Call.StartsWith(Prefix) || !Call.Split(TEXT("band="), nullptr, &Tail))
			{
				continue;
			}
			FString Band;
			return Tail.Split(TEXT(" "), &Band, nullptr) ? Band : Tail;
		}
		return FString();
	}

	// The band token back as the enum. Read back rather than assumed: the arbitration below is only a
	// statement about the swing if it ranks the band the swing actually claimed.
	bool ParsePriority(const FString& Name, EElysiumAnimPriority& Out)
	{
		for (uint8 I = 0; I <= static_cast<uint8>(EElysiumAnimPriority::Debug); ++I)
		{
			const EElysiumAnimPriority Band = static_cast<EElysiumAnimPriority>(I);
			if (!Name.IsEmpty() && Name.Equals(ElysiumAnimIntent::PriorityName(Band)))
			{
				Out = Band;
				return true;
			}
		}
		return false;
	}

	// A base-channel claim as a producer submits one.
	FElysiumAnimationRequest BaseClaim(EElysiumAnimPriority Band, EElysiumAnimSource Source,
		const TCHAR* Label)
	{
		FElysiumAnimationRequest Claim;
		Claim.Source = Source;
		Claim.Channel = EElysiumAnimChannel::Base;
		Claim.Priority = Band;
		Claim.Label = Label;
		Claim.HoldSeconds = 0.5f;
		return Claim;
	}

	// A driver standing a body that is TRAVELLING, which is the publish the swing has to survive.
	FElysiumAnimationDriver TravellingDriver()
	{
		FElysiumAnimationDriver Driver;
		Driver.Stem = TEXT("male_pc");
		Driver.Source = EElysiumAnimSource::Player;
		Driver.BodyKind = EElysiumAnimBodyKind::Player;
		Driver.Selection.GraphState = EElysiumGraphState::Run;
		return Driver;
	}

	// A bodied swinger holding the fists, with the swing seam armed. The body is what makes the play
	// reach `PlayNpcClip` at all — a bodiless character resolves a clip and plays nothing.
	struct FBandFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;

		bool Stand(FAutomationTestBase& Test)
		{
			ElysiumRng::SeedAll(0x42414e44);
			ArmSwingSeam(Services, { SwingRec(0.30f, 0.60f) });
			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeWeaponTestDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);

			Player = World->FindPlayer();
			Victim = FindCharacter(*World, TEXT("victim"));
			if (!Test.TestNotNull(TEXT("the player exists"), Player)
				|| !Test.TestNotNull(TEXT("the victim exists"), Victim))
			{
				return false;
			}
			Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
			Victim->SetRuntimeModel(TEXT("models/character/npc/male/gangbanger_a.mdl"));
			SeedHealth(*Victim, 5000);
			SeedHealth(*Player, 5000);
			PlaceFacing(*Player, FVector::ZeroVector);
			return Test.TestNotNull(TEXT("the swinger carries a body the play can reach"),
				Player->Visual);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponMeleeBandTest, "Elysium.Substrate.Weapons.MeleeBand",
	GElysiumTestFlags)
bool FElysiumWeaponMeleeBandTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	const FString PlayerSwingPlay =
		FString::Printf(TEXT("PlayNpcClip male_pc %s loop=0 "), GSwingClip);
	EElysiumAnimPriority SwingBand = EElysiumAnimPriority::Ambient;

	// --- The player's swing states its band, and it is not the band-less door's -----------------
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FElysiumWeapon* Fists = GiveWeapon(*F.Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted"), Fists))
		{
			return false;
		}
		F.Services.Calls.Reset();
		TestEqual(TEXT("the swing is accepted"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::Accepted);

		const FString Band = BandOfPlay(F.Services, PlayerSwingPlay);
		TestTrue(TEXT("the swing's play states a band"), ParsePriority(Band, SwingBand));
		TestEqual(TEXT("...and it is Scripted, not the band-less door's Ambient"),
			Band, FString(ElysiumAnimIntent::PriorityName(EElysiumAnimPriority::Scripted)));
		TestFalse(TEXT("...with the claim ended by its own clip rather than held"),
			F.Services.bNpcSegmentHeld);
	}

	// --- The same weapon in a cast hand states the same band ------------------------------------
	// `DefaultPriority(Npc)` answers `Ambient`, so a producer that read the band off its source
	// would leave the identical defect on every travelling combatant. The band is the ATTACK's, not
	// the owner's.
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FElysiumWeapon* Fists = GiveWeapon(*F.Victim, GFists);
		if (!TestNotNull(TEXT("the NPC is armed with the same record"), Fists))
		{
			return false;
		}
		F.Services.Calls.Reset();
		TestEqual(TEXT("the NPC's swing is accepted"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary, F.Player->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("a cast swing claims the base at the same band the player's does"),
			BandOfPlay(F.Services,
				FString::Printf(TEXT("PlayNpcClip gangbanger_a %s loop=0 "), GSwingClip)),
			FString(ElysiumAnimIntent::PriorityName(EElysiumAnimPriority::Scripted)));
	}

	// --- The layer families keep yielding: a shot leaves the base to the gait ladder -------------
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FElysiumWeapon* Pistol = GiveWeapon(*F.Player, GPistol);
		if (!TestNotNull(TEXT("the player is armed with a firearm"), Pistol))
		{
			return false;
		}
		F.Services.Calls.Reset();
		TestEqual(TEXT("the shot is accepted"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, F.Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("a ranged attack's base-channel stand-in still yields to a travelling body"),
			BandOfPlay(F.Services, PlayerSwingPlay),
			FString(ElysiumAnimIntent::PriorityName(EElysiumAnimPriority::Ambient)));
	}

	// --- The arbitration itself, over the band the swing just claimed ----------------------------
	{
		FElysiumAnimationDriver Driver = TravellingDriver();
		const uint32 Handle =
			Driver.SubmitRequest(BaseClaim(SwingBand, EElysiumAnimSource::Player, GSwingClip));
		TestTrue(TEXT("the swing's claim is granted"), Handle != 0);

		Driver.ArbitrateBase();
		TestFalse(TEXT("a travelling body's locomotion publish does NOT take the swing's base pose"),
			Driver.Selection.bBasePoseOwned);
		TestTrue(TEXT("...and the verdict names the swing as the holder"),
			Driver.Selection.BaseHold.Contains(GSwingClip));
		TestNotNull(TEXT("...leaving the claim standing"),
			Driver.ActiveRequest(EElysiumAnimChannel::Base));

		// The control, and the defect this test exists for: the band-less door's own band IS consumed
		// by that same publish, so a swing armed through it animates nothing while the body moves.
		FElysiumAnimationDriver Ambient = TravellingDriver();
		TestTrue(TEXT("an ambient claim is granted too"),
			Ambient.SubmitRequest(BaseClaim(EElysiumAnimPriority::Ambient,
				EElysiumAnimSource::Player, GSwingClip)) != 0);
		Ambient.ArbitrateBase();
		TestTrue(TEXT("...and the travelling publish takes the base pose from it"),
			Ambient.Selection.bBasePoseOwned);
		TestNull(TEXT("...consuming the claim outright"),
			Ambient.ActiveRequest(EElysiumAnimChannel::Base));

		// Being struck mid-swing still flinches the body out of the swing: `Reaction` outranks the
		// band. The reverse does not hold — a swing cannot displace a standing reaction, which is
		// what keeps a body attacking out of its own flinch.
		TestTrue(TEXT("an incoming reaction takes the base channel from a standing swing"),
			Driver.SubmitRequest(BaseClaim(EElysiumAnimPriority::Reaction,
				EElysiumAnimSource::Damage, TEXT("knockback_small_high_back"))) != 0);
		TestEqual(TEXT("a swing submitted under a standing reaction is refused"),
			Driver.SubmitRequest(BaseClaim(SwingBand, EElysiumAnimSource::Player, GSwingClip)),
			static_cast<uint32>(0));
	}

	// --- The combo hand-off is one more frame of the same attack, at the same band ---------------
	{
		FComboFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Chains(GSwingClip, ComboBlock(GComboNext, 0.5f, 0.9f, 0.91f));
		F.DeclareSuccessor(GComboNext);

		F.Press(0.0);
		F.StandOn(GSwingClip, 0.70f);
		F.Services.Calls.Reset();
		F.Press(0.10);
		TestEqual(TEXT("the press committed the successor"), F.Fists->Swing.ClipLabel,
			FString(GComboNext));
		TestEqual(TEXT("...claiming the base channel at the band the swing it continues did"),
			BandOfPlay(F.Services,
				FString::Printf(TEXT("PlayNpcClip male_pc %s loop=0 "), GComboNext)),
			FString(ElysiumAnimIntent::PriorityName(EElysiumAnimPriority::Scripted)));
	}

	return true;
}

// =====================================================================================
// The airborne melee fork: the recovered switch on the player's IDEAL ACTIVITY, not on
// ground contact — and who the fork belongs to.
//
// `CWeaponMelee::PrimaryAttack` asks one helper whether to request the air form, and that
// helper switches on the player's ideal activity over six cases: five airborne phases and
// the air attack itself. A ground-flag test is a different predicate and the difference is
// observable, which is what the "off the ground but not airborne" case below pins.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponMeleeAirTest, "Elysium.Substrate.Weapons.MeleeAir",
	GElysiumTestFlags)
bool FElysiumWeaponMeleeAirTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	const FString Ordinary = TEXT("ACT_MELEE_ATTACK");
	const FString AirForm  = TEXT("ACT_MELEE_AIR_ATTACK");
	const FString Combo    = TEXT("ACT_MELEE_ATTACK_2COMBO");
	const FString Heavy    = TEXT("ACT_MELEE_ATTACK_HEAVY");

	// The five airborne phases the recovered switch enters on, plus the air attack's own self-latch
	// case. Spellings, never numbers: the registered IDs this runtime carries are the binary's
	// registration numbers rather than the compiled enum slots the switch's cases are.
	const TCHAR* const Entries[] =
	{
		TEXT("ACT_HOP"), TEXT("ACT_HOP_UP"), TEXT("ACT_HOP_DOWN"), TEXT("ACT_LEAP"),
		TEXT("ACT_FALLING"),
	};
	// Off the ground and NOT in the switch. The three landings are the point of the whole case:
	// a body mid-land has no ground contact and swings the grounded form anyway.
	const TCHAR* const NotEntries[] =
	{
		TEXT("ACT_LAND"), TEXT("ACT_LAND_CROUCH"), TEXT("ACT_LAND_HARD"),
		TEXT("ACT_LEAP_ASCEND"), TEXT("ACT_LEAP_DESCEND"),
	};

	// Rank 5 Brawl draws the combo at 100%, which is what makes the two substitutions separable: on
	// the grounded arm every primary promotes, so a swing that did NOT promote can only be the fork.
	auto ArmCombo = [](FElysiumCombatCharacter& Char)
	{
		Char.Sheet.SetBase(EC::Abilities, /*Brawl*/ 1, 5);
	};

	// --- The names are joined to the recovered tables, not assumed ---------------------------------
	{
		TArray<FString> Vocabulary;
		ElysiumActionTables::CollectPlayerActivities(Vocabulary);
		auto Registered = [&Vocabulary](const TCHAR* Name)
		{
			return Vocabulary.ContainsByPredicate([Name](const FString& Seen)
				{ return Seen.Equals(Name, ESearchCase::IgnoreCase); });
		};
		for (const TCHAR* const Name : Entries)
		{
			TestTrue(FString::Printf(TEXT("%s is an activity the player selector names"), Name),
				Registered(Name));
		}
		TestTrue(TEXT("ACT_MELEE_AIR_ATTACK is one too — the self-latch case"),
			Registered(*AirForm));
		// The exclusions have to be spellings the tables carry as well, or "not in the set" would be
		// indistinguishable from a typo that can never match anything.
		for (const TCHAR* const Name : NotEntries)
		{
			TestTrue(FString::Printf(TEXT("%s is a named activity too, so its exclusion is a rule"),
				Name), Registered(Name));
		}

		// The five entries and the landings alike come off ONE arm — the jump classifier's — which is
		// what makes the split inside it the recovered switch's own and not a distinction between
		// two unrelated vocabularies.
		const ElysiumActionTables::FPlayerAction* Jump =
			ElysiumActionTables::FindPlayerAction(TEXT("PLAYER_JUMP"));
		if (TestNotNull(TEXT("the player selector carries a jump arm"), Jump))
		{
			auto NamedByJumpArm = [Jump](const TCHAR* Name)
			{
				for (int32 Index = 0; Index < Jump->RuleCount; ++Index)
				{
					const TCHAR* const Activity = Jump->Rules[Index].Activity;
					if (Activity != nullptr && FCString::Stricmp(Activity, Name) == 0)
					{
						return true;
					}
				}
				return false;
			};
			for (const TCHAR* const Name : Entries)
			{
				TestTrue(FString::Printf(TEXT("the jump arm names %s"), Name),
					NamedByJumpArm(Name));
			}
			for (const TCHAR* const Name : NotEntries)
			{
				TestTrue(FString::Printf(TEXT("...and names %s, which the fork excludes"), Name),
					NamedByJumpArm(Name));
			}
		}
	}

	// --- The predicate itself, as pure rules --------------------------------------------------------
	{
		for (const TCHAR* const Name : Entries)
		{
			TestTrue(FString::Printf(TEXT("%s enters the air fork"), Name),
				ElysiumWeapons::IsAirborneMeleeActivity(Name));
		}
		for (const TCHAR* const Name : NotEntries)
		{
			TestFalse(FString::Printf(TEXT("%s does not"), Name),
				ElysiumWeapons::IsAirborneMeleeActivity(Name));
		}
		for (const TCHAR* const Name : { TEXT("ACT_IDLE"), TEXT("ACT_WALK"), TEXT("ACT_RUN"),
			TEXT("ACT_CROUCH"), TEXT("ACT_MELEE_ATTACK") })
		{
			TestFalse(FString::Printf(TEXT("nor does the ordinary %s"), Name),
				ElysiumWeapons::IsAirborneMeleeActivity(Name));
		}
		// The switch's own `case ACT_MELEE_AIR_ATTACK`: once the air attack is the ideal, a follow-up
		// press stays on the air form until the activity moves off it.
		//
		// **Asserted at the predicate, and its publisher is asserted where the publisher lives.** A
		// swing's forced ideal activity travels with the base-channel claim
		// (`FElysiumClipSegment::Activity`) and the animation driver publishes it ahead of its own
		// locomotion classification, which `Elysium.Substrate.MeleeMovementLock` asserts for the air
		// form by name; `AElysiumMapActor::GetPlayerBaseActivity` is the seam that hands the same
		// value back here. The window it is published in is the whole air clip, and whether a
		// follow-up press can arrive inside that window is the weapon's busy predicate's answer, not
		// this switch's.
		TestTrue(TEXT("the air attack latches itself"),
			ElysiumWeapons::IsAirborneMeleeActivity(*AirForm));
		TestFalse(TEXT("an unpublished activity forks nowhere"),
			ElysiumWeapons::IsAirborneMeleeActivity(FString()));
	}

	// --- Grounded: the ordinary activity, promoted by the combo draw --------------------------------
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		ArmCombo(*F.Player);
		FElysiumWeapon* Fists = GiveWeapon(*F.Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted"), Fists))
		{
			return false;
		}
		F.Services.bPlayerOnGround = true;
		F.Services.PlayerBaseActivity = TEXT("ACT_IDLE");
		FRandomStream Expected = ElysiumRng::Stream(EElysiumRngStream::Dice);

		TestEqual(TEXT("the grounded swing is accepted"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("a grounded primary requests the ordinary activity, promoted by the draw"),
			Fists->Swing.Activity, Combo);

		// The grounded path's draw count is what the melee suite's determinism rests on, so it is
		// pinned here rather than described.
		Expected.RandRange(0, 99);
		TestEqual(TEXT("...spending exactly the one combo draw"),
			ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed(),
			Expected.GetCurrentSeed());
	}

	// --- Each of the five recovered phases takes the air form ---------------------------------------
	for (const TCHAR* const Phase : Entries)
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		ArmCombo(*F.Player);
		FElysiumWeapon* Fists = GiveWeapon(*F.Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted"), Fists))
		{
			return false;
		}
		F.Services.bPlayerOnGround = false;
		F.Services.PlayerBaseActivity = Phase;
		F.Services.Calls.Reset();
		const FRandomStream Expected = ElysiumRng::Stream(EElysiumRngStream::Dice);

		TestEqual(FString::Printf(TEXT("the swing from %s is accepted"), Phase),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(FString::Printf(TEXT("...and %s requests the air form"), Phase),
			Fists->Swing.Activity, AirForm);
		TestTrue(TEXT("...asking the driver's own published ideal activity for it"),
			F.Services.Saw(TEXT("GetPlayerBaseActivity")));
		TestFalse(TEXT("...and never consulting ground contact, which is a different predicate"),
			F.Services.Saw(TEXT("IsPlayerOnGround")));
		TestTrue(TEXT("...and the vocabulary is searched for that activity"),
			F.Services.Saw(FString::Printf(TEXT("ResolveNpcActivityClip male_pc %s"), *AirForm)));
		// The air form is not the ordinary activity, so `RequestActivity`'s equality test never
		// reaches its draw — the combo cannot promote a swing that is already the air one.
		TestEqual(TEXT("...spending no combo draw at all"),
			ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed(),
			Expected.GetCurrentSeed());
	}

	// --- THE CORRECTION: off the ground, but not in one of the five phases ---------------------------
	//
	// A body mid-landing, or riding a lift with an ordinary gait, has no ground contact and is not
	// airborne by the recovered switch. It swings the GROUNDED form — which the combo promotion
	// makes unmistakable, since only the grounded arm reaches the draw.
	for (const TCHAR* const Phase : { TEXT("ACT_LAND"), TEXT("ACT_LAND_CROUCH"),
		TEXT("ACT_LAND_HARD"), TEXT("ACT_RUN"), TEXT("ACT_IDLE") })
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		ArmCombo(*F.Player);
		FElysiumWeapon* Fists = GiveWeapon(*F.Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted"), Fists))
		{
			return false;
		}
		// Off the ground by the mover's flag, and it must not matter.
		F.Services.bPlayerOnGround = false;
		F.Services.PlayerBaseActivity = Phase;
		F.Services.Calls.Reset();

		TestEqual(FString::Printf(TEXT("the swing from %s is accepted"), Phase),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(FString::Printf(
			TEXT("...and %s keeps the grounded form though the body is off the ground"), Phase),
			Fists->Swing.Activity, Combo);
		TestFalse(TEXT("...the air form's vocabulary is never searched"),
			F.Services.Saw(FString::Printf(TEXT("ResolveNpcActivityClip male_pc %s"), *AirForm)));
	}

	// --- Airborne secondary: the heavy has no air form, so the grounded heavy is the answer ----------
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FElysiumWeapon* Blade = GiveWeapon(*F.Player, GHeavyBlade);
		if (!TestNotNull(TEXT("the two-mode blade is granted"), Blade))
		{
			return false;
		}
		F.Services.bPlayerOnGround = false;
		F.Services.PlayerBaseActivity = TEXT("ACT_FALLING");
		// No recovered weapon ladder declares an airborne heavy base and no authored clip answers
		// one, so the fork is keyed on the ORDINARY activity alone: nothing airborne rewrites a
		// secondary. An authored absence, not a gap, so nothing reports.
		TestEqual(TEXT("the airborne secondary press is accepted"),
			Blade->AttackIntent(FElysiumWeapon::EIntent::Secondary),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("an airborne secondary keeps the heavy activity"),
			Blade->Swing.Activity, Heavy);
	}

	// --- The fork is the player's: a cast body never swings the air form -----------------------------
	{
		FBandFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FElysiumWeapon* Fists = GiveWeapon(*F.Victim, GFists);
		if (!TestNotNull(TEXT("the NPC is armed with the same record"), Fists))
		{
			return false;
		}
		F.Services.bPlayerOnGround = false;
		F.Services.PlayerBaseActivity = TEXT("ACT_FALLING");
		F.Services.Calls.Reset();

		TestEqual(TEXT("the NPC's swing is accepted"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary, F.Player->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("a cast swing keeps the ordinary activity however the player's body is"),
			Fists->Swing.Activity, Ordinary);
		TestFalse(TEXT("...and never asks the player's ideal activity at all"),
			F.Services.Saw(TEXT("GetPlayerBaseActivity")));
	}

	return true;
}

// =====================================================================================
// Melee acquisition distance: the authored sequence reach, and the stand-in behind it.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponMeleeReachTest, "Elysium.Substrate.Weapons.MeleeReach",
	GElysiumTestFlags)
bool FElysiumWeaponMeleeReachTest::RunTest(const FString&)
{
	// The one degraded-path report, and it fires exactly once for the whole suite even though two
	// swings below take that path — which is the "once per weapon entity" half of the contract.
	AddExpectedError(TEXT("states no authored reach"), EAutomationExpectedErrorFlags::Contains, 1);

	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// The stand-in the authored value has to beat, stated as a number so the distances below are
	// readable: 64 Source units.
	const float StandInCm = ElysiumWeapons::MeleeReachSourceUnits * ElysiumMove::U;
	TestTrue(TEXT("the mid candidate stands beyond the stated stand-in reach"), StandInCm < 250.0f);

	// --- The authored reach is the query distance ---------------------------------------------
	{
		ElysiumRng::SeedAll(0x52454143);
		FElysiumRecordingServices Services;
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityLabel = TEXT("swing_long");
		Services.ResolvedNpcActivityClip = TEXT("swing_long");
		Services.ResolvedNpcActivityOwner = TEXT("cast_bank");
		// The maximum over every sequence the TRANSLATED activity answers — `andrei_rAttack`'s own
		// order of magnitude, and far past the stand-in.
		Services.ResolvedNpcActivityMaxReachCm = 400.0f;

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeReachTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Mid = FindCharacter(World, TEXT("mid"));
		FElysiumCombatCharacter* Far = FindCharacter(World, TEXT("far"));
		if (!TestNotNull(TEXT("the player exists"), Player)
			|| !TestNotNull(TEXT("the mid candidate exists"), Mid)
			|| !TestNotNull(TEXT("the far candidate exists"), Far))
		{
			return false;
		}
		PlaceFacing(*Player, FVector::ZeroVector);
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted"), Fists))
		{
			return false;
		}

		TestEqual(TEXT("the swing is accepted"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::Accepted);
		// The whole point of the slice: a body the 64-unit stand-in could never have reached is
		// reserved, because the activity's own sequences author a reach that reaches it.
		TestEqual(TEXT("a candidate beyond the stand-in but inside the authored reach is acquired"),
			Fists->Swing.Opponent, Mid->Handle);
		TestTrue(TEXT("...and it is not the nearer-of-two accident: the far one is outside 400 cm"),
			Fists->Swing.Opponent != Far->Handle);
	}

	// --- A candidate outside the authored reach is not acquired -------------------------------
	// Same reach, and the only candidate now stands past it. An ordinary swing still animates with
	// nothing reserved, so the transaction is accepted and its opponent is simply unset.
	{
		ElysiumRng::SeedAll(0x52454144);
		FElysiumRecordingServices Services;
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityMaxReachCm = 400.0f;

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeReachTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Mid = FindCharacter(World, TEXT("mid"));
		FElysiumCombatCharacter* Far = FindCharacter(World, TEXT("far"));
		if (!Player || !Mid || !Far)
		{
			return false;
		}
		// Stand the player 700 cm back: `far` is then 200 cm away and `mid` 450, so the only body
		// inside the 400 cm reach is the one the previous case could not reach.
		PlaceFacing(*Player, FVector(700.0f, 0.0f, 0.0f));
		Mid->Origin = FVector(1500.0f, 0.0f, 0.0f);   // pushed past the reach in both directions
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Fists->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestEqual(TEXT("a swing reserves the one body inside the authored reach"),
			Fists->Swing.Opponent, Far->Handle);
	}

	// --- No authored reach: the stated stand-in, reported once --------------------------------
	{
		ElysiumRng::SeedAll(0x52454145);
		FElysiumRecordingServices Services;
		// The clip resolves, so this is not a body/bank miss — it is a row whose descriptor states
		// no reach at all, which is the case the constant exists for.
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityMaxReachCm = 0.0f;

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeReachTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			return false;
		}
		PlaceFacing(*Player, FVector::ZeroVector);
		FElysiumWeapon* Blade = GiveWeapon(*Player, GReachBlade);
		if (!TestNotNull(TEXT("the reach suite's own blade is granted"), Blade))
		{
			return false;
		}

		Blade->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestFalse(TEXT("the stand-in reaches neither candidate, so nothing is reserved"),
			Blade->Swing.Opponent.IsSet());

		// A second swing on the same weapon takes the same degraded path and must NOT report again;
		// the expected-error count of 1 above is what asserts it. The clock is advanced rather than
		// the deadline rewound — `HoldAttacksUntil` is a maximum operation and cannot shorten one.
		World.Tick(5.0);
		Blade->AttackIntent(FElysiumWeapon::EIntent::Primary);
		TestFalse(TEXT("...and the second swing is silent about it"),
			Blade->Swing.Opponent.IsSet());
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

	// --- The aim seam: the player weapon frame supplies the shot's victim --------------------
	// The transaction has always taken an explicit handle; what is asserted here is the producer
	// that finally supplies one on the player side. Geometry is the embodiment's, so the double
	// answers it — and the range it was asked at is the mode's own authored `Range`, because a
	// query at the stated stand-in would acquire over a different distance with nothing failing.
	{
		ElysiumRng::SeedAll(0x41494D31);
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
		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!TestNotNull(TEXT("the pistol is the active weapon"), Pistol))
		{
			return false;
		}
		Services.AimTarget = Victim->Handle;
		Services.Calls.Reset();

		World.SetPlayerButtons(static_cast<uint64>(EElysiumButton::Attack));
		World.UpdatePlayerWeaponFrame();

		TestTrue(TEXT("the frame queried the aim seam at the mode's authored Range"),
			Services.Saw(TEXT("QueryAimTarget 3810.0")));
		TestTrue(TEXT("...and a shot was staged"), Pistol->Swing.bActive);
		TestEqual(TEXT("...carrying the queried handle as the transaction's victim"),
			Pistol->Swing.Opponent, Victim->Handle);

		// A press whose mode does not FIRE takes no aim either. The pistol's secondary is a
		// `Toggle_Primary_Mode`, which authors no `Range` for the same reason it authors no damage
		// — nothing leaves the barrel — so a query here would both trace for a mode swap and
		// report a missing range the record was never supposed to carry.
		const int32 ModeBefore = Pistol->PrimaryModeIndex;
		Services.Calls.Reset();
		World.SetPlayerButtons(0);
		World.UpdatePlayerWeaponFrame();
		World.Tick(2.0);
		World.SetPlayerButtons(static_cast<uint64>(EElysiumButton::Attack2));
		World.UpdatePlayerWeaponFrame();
		// The press reached the frame — asserted through its effect, so the silence below is the
		// absence of a query rather than the absence of a press.
		TestNotEqual(TEXT("the secondary press toggled the primary mode"),
			Pistol->PrimaryModeIndex, ModeBefore);
		TestFalse(TEXT("...and a mode-toggle press runs no aim query"),
			Services.Saw(TEXT("QueryAimTarget")));

		// The melee half of the same rule: a swing reserves its own opponent on the authored reach
		// inside the transaction, so the player frame must not run a second acquisition for it.
		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!Fists)
		{
			return false;
		}
		Services.Calls.Reset();
		World.SetPlayerButtons(0);
		World.UpdatePlayerWeaponFrame();
		World.Tick(2.0);
		World.SetPlayerButtons(static_cast<uint64>(EElysiumButton::Attack));
		World.UpdatePlayerWeaponFrame();
		TestFalse(TEXT("a melee record's frame runs no aim query"),
			Services.Saw(TEXT("QueryAimTarget")));
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
// The sequence-event weapon route (LIFE5 slice 2).
//
// `CBaseCombatCharacter::HandleAnimEvent` (`0x1032e330`) forwards the whole 3000..3999
// band to its active weapon's `Operator_HandleAnimEvent` `+0x5c8`; the 17 ranged classes'
// body (`0x10238160`) commits the shot on 3030..3044 and the 29 common-melee classes'
// (`0x103ea5b0`) commits contact on 3047 and swallows 3001/3003/3030..3037
// (`docs/vtmb/animation_and_movers.md` → "Sequence events and native dispatch").
//
// Every case below drives the REAL producer path: the world's own event pass reads a clip
// phase off the recording body, walks the timeline the fixture declared for that clip, and
// dispatches into the character. Nothing calls a handler directly.
// =====================================================================================

namespace
{
	// The clip the attack activity resolves to in these cases, and the bank the include DAG named.
	// Both halves matter: a timeline is keyed by (owner, label), so a route that carried only the
	// label would read the wrong bank's sequence.
	const TCHAR* const GAttackOwner = TEXT("move_and_ranged");
	const TCHAR* const GAttackLabel = TEXT("attack_layer");

	FElysiumAnimEvent WeaponEv(float Cycle, int32 Event, const TCHAR* Options = TEXT(""))
	{
		FElysiumAnimEvent Record;
		Record.Cycle = Cycle;
		Record.Event = Event;
		Record.Options = Options;
		return Record;
	}

	// Point the activity seam at one named clip and stand a phase on the body for it, which is what
	// makes the event pass walk that clip's timeline.
	void ArmClipSeam(FElysiumRecordingServices& Services, float Cycle)
	{
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityLabel = GAttackLabel;
		Services.ResolvedNpcActivityClip = TEXT("attack_layer_0");
		Services.ResolvedNpcActivityOwner = GAttackOwner;

		Services.bBodyClipPhaseSet = true;
		Services.BodyClipPhase = FElysiumClipPhase();
		Services.BodyClipPhase.OwnerStem = GAttackOwner;
		Services.BodyClipPhase.Label = GAttackLabel;
		Services.BodyClipPhase.Cycle = Cycle;
		Services.BodyClipPhase.Length = 1.0f;
		Services.BodyClipPhase.bLooping = false;
		Services.BodyClipPhase.PlayId = 1;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponAnimEventTest, "Elysium.Substrate.Weapons.AnimEvent",
	GElysiumTestFlags)
bool FElysiumWeaponAnimEventTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// Stand a bodied player, a live victim and (optionally) the clip seam. The player is the shooter
	// because it carries no AI that would arm clips of its own between the frames a case drives.
	auto Stand = [this](FElysiumRecordingServices& Services, FElysiumEntityWorld& World,
		FElysiumPlayer*& OutPlayer, FElysiumCombatCharacter*& OutVictim) -> bool
	{
		World.Load(MakeWeaponTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		OutPlayer = World.FindPlayer();
		OutVictim = FindCharacter(World, TEXT("victim"));
		if (!TestNotNull(TEXT("the player exists"), OutPlayer)
			|| !TestNotNull(TEXT("the victim exists"), OutVictim))
		{
			return false;
		}
		// The world's pass skips anything with no skeletal body, so the shooter needs one. The model
		// goes on through the ordinary runtime writer, which is the door `SetModel` uses.
		OutPlayer->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
		if (!TestNotNull(TEXT("the player carries a body the pass can walk"), OutPlayer->Visual))
		{
			return false;
		}
		SeedHealth(*OutVictim, 100);
		PlaceFacing(*OutPlayer, FVector::ZeroVector);
		return true;
	};

	// --- 3030..3044 on a ranged weapon commits the shot, through the queue -----------------------
	{
		ElysiumRng::SeedAll(4242);
		// The census is process-wide by design (a work list across a session, not per-map state), so
		// every case that reads a row count starts from a known state — including the first.
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		if (!Stand(Services, World, Player, Victim))
		{
			return false;
		}
		ArmClipSeam(Services, 0.0f);
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GAttackOwner, GAttackLabel));
		// 3038 rather than the first id of the band, so nothing can pass by matching 3030 alone.
		Timeline.Add(WeaponEv(0.30f, 3038, TEXT("0")));

		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!TestNotNull(TEXT("the pistol is granted"), Pistol))
		{
			return false;
		}
		const int32 MagazineBefore = Pistol->MagazineCount;

		TestEqual(TEXT("the shot is accepted"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestTrue(TEXT("...and waits on the clip's own event rather than the estimate"),
			Pistol->Swing.bAwaitingAnimEvent);

		// Past the `ContactEventCycle` instant the estimate WOULD have used (0.5 of a 1.0s clip) and
		// nothing has happened: the estimate stood down, and no event has fired yet.
		Services.BodyClipPhase.Cycle = 0.2f;
		World.Tick(0.6);
		TestEqual(TEXT("the suppressed estimate commits nothing at its own instant"),
			DamageTaken(*Victim), 0);
		TestEqual(TEXT("...and spends no ammunition"), Pistol->MagazineCount, MagazineBefore);
		TestTrue(TEXT("...leaving the transaction staged"), Pistol->Swing.bActive);

		// The frame whose interval contains 0.30. The record reaches the character, the character
		// forwards the band to the active weapon, the weapon queues the commit at delay 0.0 — and
		// queue service runs later in this same tick, so the shot lands on this frame.
		Services.BodyClipPhase.Cycle = 0.4f;
		World.Tick(0.7);
		TestTrue(TEXT("the clip's own shot event commits the transaction"), DamageTaken(*Victim) > 0);
		TestEqual(TEXT("...spending Ammo_Cost at the authoritative boundary"),
			Pistol->MagazineCount, MagazineBefore - 1);
		TestFalse(TEXT("...and consuming it"), Pistol->Swing.bActive);
		TestEqual(TEXT("a claimed id is not census work"), ElysiumAnimEventCensus::Num(), 0);
	}

	// --- 3047 is CLAIMED and commits nothing; the swallow set is claimed and does nothing ---------
	{
		ElysiumRng::SeedAll(4242);
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		if (!Stand(Services, World, Player, Victim))
		{
			return false;
		}
		ArmClipSeam(Services, 0.0f);
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GAttackOwner, GAttackLabel));
		// A swish and a ranged id ahead of the swing trigger. Both are inside the common-melee body's
		// swallow set, so they are claimed, do nothing, and never reach the census. 3047 behind them
		// is retail's NPC swing TRIGGER rather than a commit, and is claimed on the same terms: the
		// contact is the swept walk over the clip's own authored records, and no id commits it.
		Timeline.Add(WeaponEv(0.10f, 3003));
		Timeline.Add(WeaponEv(0.20f, 3035));
		Timeline.Add(WeaponEv(0.40f, 3047));

		FElysiumWeapon* Fists = GiveWeapon(*Player, GFists);
		if (!TestNotNull(TEXT("the fists are granted"), Fists))
		{
			return false;
		}
		TestEqual(TEXT("the swing is accepted"),
			Fists->AttackIntent(FElysiumWeapon::EIntent::Primary),
			FElysiumWeapon::EVerdict::Accepted);
		TestEqual(TEXT("...naming the acquired opponent"), Fists->Swing.Opponent, Victim->Handle);
		TestFalse(TEXT("...waiting on no event at all — melee estimates and commits on neither"),
			Fists->Swing.bAwaitingAnimEvent);

		Services.BodyClipPhase.Cycle = 0.30f;
		World.Tick(0.1);
		TestEqual(TEXT("the swallowed swish and shot ids commit nothing"), DamageTaken(*Victim), 0);
		TestEqual(TEXT("...and are claimed, so neither reaches the census"),
			ElysiumAnimEventCensus::Num(), 0);

		Services.BodyClipPhase.Cycle = 0.50f;
		World.Tick(0.2);
		TestEqual(TEXT("3047 commits nothing — it is the swing trigger, not the contact"),
			DamageTaken(*Victim), 0);
		TestTrue(TEXT("...leaving the transaction to the contact walk"), Fists->Swing.bActive);
		TestTrue(TEXT("...and staging no opposed record, which the walk owns"),
			Victim->FindMeleeRoll(Player->Handle) == nullptr);
		TestEqual(TEXT("...but it IS claimed, so it is not census work"),
			ElysiumAnimEventCensus::Num(), 0);
	}

	// --- An id in the band with no weapon held is an ordinary unclaimed record --------------------
	{
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		if (!Stand(Services, World, Player, Victim))
		{
			return false;
		}
		ArmClipSeam(Services, 0.0f);
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GAttackOwner, GAttackLabel));
		Timeline.Add(WeaponEv(0.20f, 3038, TEXT("0")));
		Timeline.Add(WeaponEv(0.25f, 3047));

		TestNull(TEXT("the player holds nothing"), Player->Inventory.Active(*Player));

		Services.BodyClipPhase.Cycle = 0.50f;
		World.Tick(0.1);

		// Empty-handed is a negative query, not a failure: the band resolves to no receiver, the
		// character says so by answering false, and the census is the whole report.
		TArray<ElysiumAnimEventCensus::FRow> Rows;
		ElysiumAnimEventCensus::Collect(Rows);
		TestEqual(TEXT("both band ids land in the unclaimed census"), Rows.Num(), 2);
		for (const ElysiumAnimEventCensus::FRow& Row : Rows)
		{
			TestFalse(TEXT("...reported as refused rather than as out of band"), Row.bAboveServerBand);
			TestEqual(TEXT("...naming the clip they fired from"), Row.Label, FString(GAttackLabel));
			TestEqual(TEXT("...and its owning bank"), Row.OwnerStem, FString(GAttackOwner));
		}
		TestEqual(TEXT("the victim takes nothing from a record nothing claimed"),
			DamageTaken(*Victim), 0);
	}

	// --- A clip with no commit id keeps the estimate, and says so exactly once --------------------
	{
		// The degraded path. The body IS being walked, so the event route was available and the clip
		// simply does not name its instant — which retail never does. One warning per clip, however
		// many swings take it.
		AddExpectedError(TEXT("falls back to the ContactEventCycle estimate"),
			EAutomationExpectedErrorFlags::Contains, 1);

		ElysiumRng::SeedAll(4242);
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		if (!Stand(Services, World, Player, Victim))
		{
			return false;
		}
		ArmClipSeam(Services, 0.0f);
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GAttackOwner, GAttackLabel));
		// A footstep and nothing else: a real timeline that names no commit for either family.
		Timeline.Add(WeaponEv(0.20f, 2050));

		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!TestNotNull(TEXT("the pistol is granted"), Pistol))
		{
			return false;
		}
		TestEqual(TEXT("the shot is accepted"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestFalse(TEXT("...on the estimate, because the clip names no commit id"),
			Pistol->Swing.bAwaitingAnimEvent);
		TestTrue(TEXT("...scheduled at ContactEventCycle of the resolved clip"),
			NearlyEqual(Pistol->Swing.CommitTime, 1.0 * ElysiumWeapons::ContactEventCycle));

		Services.BodyClipPhase.Cycle = 0.30f;
		World.Tick(0.6);
		TestTrue(TEXT("the estimate still commits the shot"), DamageTaken(*Victim) > 0);

		// A second swing past the recovery deadline takes the same degraded route and restates
		// nothing: the warning is keyed by clip, and the expected-error count above is what asserts it.
		World.Tick(2.0);
		TestEqual(TEXT("a second shot is accepted too"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestFalse(TEXT("...on the estimate again"), Pistol->Swing.bAwaitingAnimEvent);
	}

	// --- A phase for a DIFFERENT clip is not this clip's dispatcher -------------------------------
	{
		// The body is being walked, the resolved clip's timeline carries a shot id, and the estimate
		// must STILL stand: the polled channel is standing on some other clip, so the pass walks that
		// clip's timeline and never reaches this one's. "A phase is published" is the wrong question.
		ElysiumRng::SeedAll(4242);
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		if (!Stand(Services, World, Player, Victim))
		{
			return false;
		}
		ArmClipSeam(Services, 0.0f);
		// The attack resolves to `attack_layer`, whose timeline names the commit — but the channel is
		// left standing on the idle the body was already playing.
		Services.BodyClipPhase.Label = TEXT("idle01");
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GAttackOwner, GAttackLabel));
		Timeline.Add(WeaponEv(0.30f, 3038, TEXT("0")));

		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!TestNotNull(TEXT("the pistol is granted"), Pistol))
		{
			return false;
		}
		TestEqual(TEXT("the shot is accepted"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestFalse(TEXT("a phase for another clip does not stand the estimate down"),
			Pistol->Swing.bAwaitingAnimEvent);

		Services.BodyClipPhase.Cycle = 0.50f;
		World.Tick(0.6);
		TestTrue(TEXT("...so the estimate is what commits the shot"), DamageTaken(*Victim) > 0);
		// And the other clip's own walk fired nothing, because it declares no timeline at all.
		TestEqual(TEXT("the clip the channel actually stands on contributes no records"),
			ElysiumAnimEventCensus::Num(), 0);
	}

	// --- A flinch takes the channel mid-swing, and the commit still arrives ------------------------
	{
		// The combat consequence of arm precedence, driven through the substrate as the pose layer
		// publishes it. A body swings — the attack clip's timeline names the commit, so the estimate
		// stands DOWN and nothing is queued — and then something hits it. The reaction owns the base
		// channel while it stands, so the pass walks the flinch's timeline instead; if the attack
		// clip's own arm were discarded rather than displaced, its commit id would never fire and the
		// swing would expire on its recovery deadline with the damage silently gone.
		//
		// What the instance publishes when the flinch ends is scripted here exactly: the same clip,
		// the SAME `PlayId` (it never stopped), a cycle that advanced while it was off screen, and an
		// anchor at the phase the dispatcher last saw. That is what makes the skipped interval fire
		// once instead of being lost or replayed.
		ElysiumRng::SeedAll(4242);
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		if (!Stand(Services, World, Player, Victim))
		{
			return false;
		}
		ArmClipSeam(Services, 0.0f);
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GAttackOwner, GAttackLabel));
		// An early record the swing passes BEFORE the hit, and the commit it has not reached yet.
		// The early one is what makes the resumption assertion discriminate: an arm that resumed from
		// zero instead of from its anchor would walk `[0, 0.45)` — which still contains the commit,
		// so the commit alone proves nothing — and fire this footstep a second time for a step the
		// body took once.
		Timeline.Add(WeaponEv(0.05f, 2050));
		Timeline.Add(WeaponEv(0.30f, 3038, TEXT("0")));

		FElysiumWeapon* Pistol = GiveWeapon(*Player, GPistol);
		if (!TestNotNull(TEXT("the pistol is granted"), Pistol))
		{
			return false;
		}
		TestEqual(TEXT("the shot is accepted"),
			Pistol->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestTrue(TEXT("...on the clip's own commit id, so no estimate is queued"),
			Pistol->Swing.bAwaitingAnimEvent);

		// Still short of the commit record, and past the early one.
		Services.BodyClipPhase.Cycle = 0.10f;
		World.Tick(0.2);
		TestEqual(TEXT("nothing has committed yet"), DamageTaken(*Victim), 0);
		TArray<ElysiumAnimEventCensus::FRow> Walked;
		ElysiumAnimEventCensus::Collect(Walked);
		TestEqual(TEXT("the early record fired once on the way in"), Walked.Num(), 1);

		// The hit lands. The flinch is a different clip and a different play, and it owns the base
		// channel for as long as it stands.
		Services.BodyClipPhase.Label = TEXT("hit_torso");
		Services.BodyClipPhase.Cycle = 0.40f;
		Services.BodyClipPhase.PlayId = 2;
		World.Tick(0.3);
		TestEqual(TEXT("the flinch's own timeline commits nothing for the swing"),
			DamageTaken(*Victim), 0);
		TestTrue(TEXT("...and the swing is still staged rather than dropped"), Pistol->Swing.bActive);

		// The flinch ends. The attack clip never stopped, so it takes the channel back as the same
		// play, advanced, resuming from where the dispatcher left it.
		Services.BodyClipPhase.Label = GAttackLabel;
		Services.BodyClipPhase.PlayId = 1;
		Services.BodyClipPhase.AnchorCycle = 0.10f;
		Services.BodyClipPhase.Cycle = 0.45f;
		World.Tick(0.4);
		TestTrue(TEXT("the resumed clip fires the commit it passed, and the shot lands"),
			DamageTaken(*Victim) > 0);
		ElysiumAnimEventCensus::Collect(Walked);
		TestEqual(TEXT("...and it resumes from its anchor, so the record behind it does not re-fire"),
			Walked.Num() == 1 ? Walked[0].Count : -1, 1);
	}

	// --- A thrown weapon takes the body that accepts nothing --------------------------------------
	{
		// `0x1024f030` — the 17 base/discipline/armor/thrown/unarmed classes. A thrown weapon is not
		// a slow firearm: the shot band commits nothing on it, so the ids land on the census and its
		// transaction stays on the estimate. The pure assignment is asserted first, because the
		// routing below is only meaningful if the record decides it.
		TestTrue(TEXT("a firearm record takes the ranged body"),
			ElysiumWeapons::OperatorBodyFor(EElysiumItemType::WeaponFirearm)
				== ElysiumWeapons::EOperatorBody::Ranged);
		TestTrue(TEXT("a melee record takes the common melee body"),
			ElysiumWeapons::OperatorBodyFor(EElysiumItemType::WeaponMelee)
				== ElysiumWeapons::EOperatorBody::Melee);
		TestTrue(TEXT("a thrown record takes the body with no accepted route"),
			ElysiumWeapons::OperatorBodyFor(EElysiumItemType::WeaponThrown)
				== ElysiumWeapons::EOperatorBody::None);
		TestFalse(TEXT("...which commits on no id in the band"),
			ElysiumWeapons::IsCommitEvent(3038, ElysiumWeapons::EOperatorBody::None)
			|| ElysiumWeapons::IsCommitEvent(3047, ElysiumWeapons::EOperatorBody::None));

		ElysiumRng::SeedAll(4242);
		ElysiumAnimEventCensus::Clear();
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumPlayer* Player = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		if (!Stand(Services, World, Player, Victim))
		{
			return false;
		}
		ArmClipSeam(Services, 0.0f);
		TArray<FElysiumAnimEvent>& Timeline = Services.NpcEventTimelines.Add(
			FElysiumRecordingServices::EventTimelineKey(GAttackOwner, GAttackLabel));
		// Both families' commit ids, on a clip a thrown weapon is playing. Neither may commit it.
		Timeline.Add(WeaponEv(0.20f, 3038, TEXT("0")));
		Timeline.Add(WeaponEv(0.25f, 3047));

		FElysiumWeapon* Grenade = GiveWeapon(*Player, GThrown);
		if (!TestNotNull(TEXT("the thrown weapon is granted as a weapon"), Grenade))
		{
			return false;
		}
		TestEqual(TEXT("the throw is accepted"),
			Grenade->AttackIntent(FElysiumWeapon::EIntent::Primary, Victim->Handle),
			FElysiumWeapon::EVerdict::Accepted);
		TestFalse(TEXT("...on the estimate, because its body accepts no commit id"),
			Grenade->Swing.bAwaitingAnimEvent);

		Services.BodyClipPhase.Cycle = 0.30f;
		World.Tick(0.1);
		TArray<ElysiumAnimEventCensus::FRow> Rows;
		ElysiumAnimEventCensus::Collect(Rows);
		TestEqual(TEXT("both band ids go unclaimed on a thrown weapon"), Rows.Num(), 2);
		TestFalse(TEXT("...and neither committed the transaction early"),
			DamageTaken(*Victim) > 0);
		TestTrue(TEXT("...leaving it staged for its own estimate"), Grenade->Swing.bActive);

		World.Tick(0.6);
		TestTrue(TEXT("the estimate commits it, exactly as it did before this slice"),
			DamageTaken(*Victim) > 0);
	}

	ElysiumAnimEventCensus::Clear();
	return true;
}

// =====================================================================================
// The leaf blob's schema (11.9 rider).
//
// A `FElysiumEntityState::LeafState` is opaque bytes: the freeze writes the entity
// through its own `Serialize` into a private memory archive and stores the result. That
// archive carries no version, so a leaf that gates a field on `Ar.Version()` is only
// correct if the version its blob was WRITTEN at travels with it —
// `FElysiumMapSnapshot::SchemaVersion`, threaded into the replay archive by
// `ApplyEntityRecord`.
//
// The weapon leaf is the concrete case: `Swing.bAwaitingAnimEvent` sits mid-record, so a
// pre-`WeaponAnimEvent` blob read at `Latest` does not lose one field — it shifts every
// field after it and runs off the end.
// =====================================================================================

namespace
{
	// A world holding one loose pistol and nothing else, so the record under test has a stable def
	// index, no owner to rebase and no AI to move it. Its own defs rather than the shared ones,
	// because adding a row to those would shift every other suite's indices.
	FElysiumEntityDefs MakeLeafSchemaDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__weapon_leaf_test__");

		FElysiumEntityDef Loose;
		Loose.Classname = GPistol;
		Loose.TargetName = TEXT("loose_pistol");
		Defs.Defs.Add(MoveTemp(Loose));
		return Defs;
	}

	FElysiumWeapon* FindLooseWeapon(FElysiumEntityWorld& World)
	{
		FElysiumEntity* Ent = World.FindByName(TEXT("loose_pistol"));
		FElysiumItem* Item = Ent ? Ent->AsItem() : nullptr;
		return Item ? Item->AsWeapon() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWeaponLeafSchemaTest, "Elysium.Substrate.Weapons.LeafSchema",
	GElysiumTestFlags)
bool FElysiumWeaponLeafSchemaTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeWeaponTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// Freeze a weapon carrying state on both sides of the new field, then re-write its blob at the
	// schema a pre-26 build would have written — which is exactly what an old save file holds.
	FElysiumMapSnapshot Snapshot;
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeLeafSchemaDefs());
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumWeapon* Pistol = FindLooseWeapon(World);
		if (!TestNotNull(TEXT("the loose pistol installs as a weapon"), Pistol))
		{
			return false;
		}
		// Fields on BOTH sides of `bAwaitingAnimEvent`: the two ahead of it prove the read reached
		// the right place, and the four behind it are what a shifted read destroys.
		Pistol->NextPrimaryAttackTime = 2.25;
		Pistol->NextSecondaryAttackTime = 3.75;
		Pistol->bReloading = true;
		Pistol->ReloadSerial = 7;
		Pistol->ReloadEndTime = 11.5;
		Pistol->bFireIntentDuringReload = true;

		World.Freeze(Snapshot);
		TestEqual(TEXT("a snapshot frozen in memory stamps this build's schema"),
			Snapshot.SchemaVersion, (int32)FElysiumSaveVersion::Latest);

		FElysiumEntityState* Record = Snapshot.Entities.FindByPredicate(
			[](const FElysiumEntityState& S) { return S.TargetName == TEXT("loose_pistol"); });
		if (!TestTrue(TEXT("the weapon was frozen with a leaf blob"),
			Record != nullptr && Record->LeafState.Num() > 0))
		{
			return false;
		}

		// The pre-26 blob. Written through the leaf's own `Serialize` at the older schema, so it is
		// byte-for-byte what that build produced rather than a hand-built approximation.
		Record->LeafState.Reset();
		{
			FMemoryWriter Writer(Record->LeafState, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::NpcDisciplines);
			Pistol->Serialize(Ar);
		}
		Snapshot.SchemaVersion = FElysiumSaveVersion::NpcDisciplines;
	}

	// --- Replayed at the schema it was written at ------------------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeLeafSchemaDefs());
		World.Activate(0.0);
		World.ApplySnapshot(Snapshot);

		FElysiumWeapon* Pistol = FindLooseWeapon(World);
		if (!TestNotNull(TEXT("the weapon restored"), Pistol))
		{
			return false;
		}
		TestTrue(TEXT("the field ahead of the new one is intact"),
			NearlyEqual(Pistol->NextPrimaryAttackTime, 2.25));
		TestTrue(TEXT("...and its neighbour"),
			NearlyEqual(Pistol->NextSecondaryAttackTime, 3.75));
		// The four behind it. A blob read one field out of step turns every one of these into
		// garbage, which is the whole failure the recorded schema exists to prevent.
		TestTrue(TEXT("the reload latch behind the new field is intact"), Pistol->bReloading);
		TestEqual(TEXT("...its serial"), Pistol->ReloadSerial, 7);
		TestTrue(TEXT("...its deadline"), NearlyEqual(Pistol->ReloadEndTime, 11.5));
		TestTrue(TEXT("...and the fire-intent latch after it"), Pistol->bFireIntentDuringReload);
		// And the field the old build never wrote defaults to the only route it could have taken.
		TestFalse(TEXT("a pre-26 transaction restores on the estimate route"),
			Pistol->Swing.bAwaitingAnimEvent);
	}

	// --- The control: the same blob replayed at `Latest` -----------------------------------------
	{
		// What every leaf gate did before the schema was recorded. This is not a supported path —
		// it is the failure being fixed, reproduced deliberately so the fix is shown to be
		// load-bearing rather than decorative. The read runs off the end of the blob, and that must
		// be reported rather than restoring a half-read weapon in silence.
		AddExpectedError(TEXT("failed to read"), EAutomationExpectedErrorFlags::Contains, 1);
		// The engine notices the shift before the overrun does: a `bool` field landing on bytes that
		// spell neither 0 nor 1 is refused by `FArchive::SerializeBool`. How many do that depends on
		// the byte layout, so this is expected without a count.
		AddExpectedError(TEXT("Invalid boolean encountered"), EAutomationExpectedErrorFlags::Contains, 0);

		FElysiumMapSnapshot Mislabelled = Snapshot;
		Mislabelled.SchemaVersion = FElysiumSaveVersion::Latest;

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeLeafSchemaDefs());
		World.Activate(0.0);
		World.ApplySnapshot(Mislabelled);

		FElysiumWeapon* Pistol = FindLooseWeapon(World);
		if (!TestNotNull(TEXT("the weapon still exists after a failed leaf read"), Pistol))
		{
			return false;
		}
		// The shift, stated as the concrete thing it destroys: the reload serial reads out of the
		// deadline's bytes instead of its own.
		TestNotEqual(TEXT("a blob read at the wrong schema does NOT restore the reload serial"),
			Pistol->ReloadSerial, 7);
	}

	// --- The swing's clip owner, on both sides of its own version --------------------------------
	//
	// `Swing.ClipOwnerStem` is the LAST field of the swing block, so an older payload loses it
	// rather than shifting anything — but the fields behind the block still have to read, which is
	// what makes this a byte-layout assertion and not a field-presence one.
	{
		auto FreezeAt = [&](int32 Schema, FElysiumMapSnapshot& Out)
		{
			FElysiumRecordingServices Services;
			FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
			World.Load(MakeLeafSchemaDefs());
			World.Activate(0.0);
			World.Tick(0.0);

			FElysiumWeapon* Pistol = FindLooseWeapon(World);
			if (Pistol == nullptr)
			{
				return false;
			}
			Pistol->Swing.bActive = true;
			Pistol->Swing.Serial = 3;
			Pistol->Swing.ClipLabel = TEXT("swing_long");
			Pistol->Swing.ClipOwnerStem = TEXT("cast_bank");
			Pistol->Swing.bAwaitingAnimEvent = true;
			Pistol->ReloadSerial = 7;
			Pistol->bFireIntentDuringReload = true;
			World.Freeze(Out);

			if (Schema != (int32)FElysiumSaveVersion::Latest)
			{
				FElysiumEntityState* Record = Out.Entities.FindByPredicate(
					[](const FElysiumEntityState& S) { return S.TargetName == TEXT("loose_pistol"); });
				if (Record == nullptr)
				{
					return false;
				}
				Record->LeafState.Reset();
				FMemoryWriter Writer(Record->LeafState, /*bIsPersistent*/ true);
				FElysiumSaveArchive Ar(Writer, Schema);
				Pistol->Serialize(Ar);
				Out.SchemaVersion = Schema;
			}
			return true;
		};

		auto RestoreFrom = [&](const FElysiumMapSnapshot& Snap, FString& OutStem, int32& OutSerial,
			bool& bOutFireIntent)
		{
			FElysiumRecordingServices Services;
			FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
			World.Load(MakeLeafSchemaDefs());
			World.Activate(0.0);
			World.ApplySnapshot(Snap);

			FElysiumWeapon* Pistol = FindLooseWeapon(World);
			if (Pistol == nullptr)
			{
				return false;
			}
			OutStem = Pistol->Swing.ClipOwnerStem;
			OutSerial = Pistol->ReloadSerial;
			bOutFireIntent = Pistol->bFireIntentDuringReload;
			return true;
		};

		FString Stem;
		int32 Serial = 0;
		bool bFireIntent = false;

		FElysiumMapSnapshot Current;
		if (!TestTrue(TEXT("a v27 snapshot freezes"),
			FreezeAt((int32)FElysiumSaveVersion::Latest, Current))
			|| !TestTrue(TEXT("...and restores"), RestoreFrom(Current, Stem, Serial, bFireIntent)))
		{
			return false;
		}
		TestEqual(TEXT("a v27 payload round-trips the swing's clip owner"), Stem,
			FString(TEXT("cast_bank")));
		TestEqual(TEXT("...with the fields behind the swing block intact"), Serial, 7);

		FElysiumMapSnapshot Legacy;
		if (!TestTrue(TEXT("a v26 snapshot freezes"),
			FreezeAt((int32)FElysiumSaveVersion::WeaponAnimEvent, Legacy))
			|| !TestTrue(TEXT("...and restores"), RestoreFrom(Legacy, Stem, Serial, bFireIntent)))
		{
			return false;
		}
		// The degradation is the diagnostic alone: `ClipLabel` above still restores, and it plus the
		// attacking body's stem are what address the blocked-reaction row, so a pre-27 swing plays
		// the same reaction and only its log line cannot name the bank.
		TestTrue(TEXT("a v26 payload restores an empty clip owner"), Stem.IsEmpty());
		// And nothing shifted: the reload block sits behind the swing block in the record.
		TestEqual(TEXT("...with every field behind the swing block still readable"), Serial, 7);
		TestTrue(TEXT("...including the last one"), bFireIntent);
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
