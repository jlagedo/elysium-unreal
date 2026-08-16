// Content-free Substrate automation: the typed damage descriptor, the shared apply path, the one
// health commit, the `trigger_hurt` cadence and the NPC `TakeDamage` input.
//
// Every number asserted here is a fact from `docs/vtmb/combat-and-damage.md` (the descriptor
// layout, the apply order, the soak table, the health commit) or `docs/vtmb/entity_io.md` (the
// hurt cadence and the two hurt outputs). Nothing loads a rulebook: the resolver's rating and
// difficulty halves fail safe without one, and the tests that need a decided soak state it as a
// forced value rather than as a roll.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDice.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumDamageTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumSaveTestHelpers::SaveTestCounterValue;

namespace
{
	using EC = EElysiumTraitContainer;

	// A health track without a rulebook: the sheet's own clamps come from `stats.txt`, which a
	// bare world does not load, so the ceiling is written directly and the keyfields re-derived.
	void SeedHealth(FElysiumCombatCharacter& Char, int32 MaxHealth, int32 DamageTaken = 0)
	{
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, DamageTaken);
		Char.RecomputeSheet();
	}

	int32 Trait(const FElysiumCombatCharacter& Char, int32 Slot)
	{
		return Char.Sheet.GetCurrent(EC::Attributes, Slot);
	}

	// A direct-input descriptor: the damage-success count is stated, so no roll is involved and the
	// assertions are about the soak/commit halves alone.
	FElysiumDmg DirectDmg(EElysiumDmgFamily Family, int32 Successes, int32 ForcedSoak = 0,
		uint32 Mask = 0)
	{
		FElysiumDmg Dmg;
		Dmg.Family = Family;
		Dmg.Flags = ElysiumDamage::FlagDirectInput;
		Dmg.ExtraInput = Successes;
		Dmg.ForcedSoak = ForcedSoak;
		Dmg.DmgMask = Mask;
		return Dmg;
	}

	// An already-resolved descriptor, for the commit tests that drive `CommitDamage` on its own.
	FElysiumDmg ResolvedDmg(EElysiumDmgFamily Family, int32 Amount, uint32 Mask = 0)
	{
		FElysiumDmg Dmg = DirectDmg(Family, Amount, /*ForcedSoak*/ 0, Mask);
		Dmg.RolledSuccesses = Amount;
		Dmg.Remainder = Amount;
		Dmg.AppliedDamage = Amount;
		Dmg.bResolved = true;
		return Dmg;
	}

	FElysiumEntityDefs MakeDamageTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__damage_test__");

		FElysiumEntityDef Victim;
		Victim.Classname = TEXT("npc_VPedestrian");
		Victim.TargetName = TEXT("victim");
		Victim.Origin = FVector(100.0f, 0.0f, 0.0f);
		auto Wire = [&Victim](const TCHAR* Output, const TCHAR* Target, const TCHAR* Input,
			const TCHAR* Param)
		{
			FElysiumOutputDef Row;
			Row.Name = Output;
			Row.Target = Target;
			Row.Input = Input;
			Row.Param = Param;
			Victim.Outputs.Add(MoveTemp(Row));
		};
		Wire(TEXT("OnDamaged"), TEXT("damagedcount"), TEXT("Add"), TEXT("1"));
		Wire(TEXT("OnHalfHealth"), TEXT("halfcount"), TEXT("Add"), TEXT("1"));
		// The activator probe: the descriptor's Source is what `!activator` has to resolve to.
		Wire(TEXT("OnDamaged"), TEXT("!activator"), TEXT("MoneyAdd"), TEXT("7"));
		Defs.Defs.Add(MoveTemp(Victim));

		// The hurt volume. spawnflags 1 is ALLOW_CLIENTS, which is what admits the player.
		FElysiumEntityDef Hurt;
		Hurt.Classname = TEXT("trigger_hurt");
		Hurt.TargetName = TEXT("hurtbox");
		Hurt.Keys.Add(TEXT("damage"), TEXT("10"));
		Hurt.Keys.Add(TEXT("spawnflags"), TEXT("1"));
		FElysiumOutputDef HurtRow;
		HurtRow.Name = TEXT("OnHurtPlayer");
		HurtRow.Target = TEXT("hurtplayercount");
		HurtRow.Input = TEXT("Add");
		HurtRow.Param = TEXT("1");
		Hurt.Outputs.Add(MoveTemp(HurtRow));
		Defs.Defs.Add(MoveTemp(Hurt));

		for (const TCHAR* Name : { TEXT("damagedcount"), TEXT("halfcount"), TEXT("hurtplayercount") })
		{
			FElysiumEntityDef Counter;
			Counter.Classname = TEXT("math_counter");
			Counter.TargetName = Name;
			Defs.Defs.Add(MoveTemp(Counter));
		}
		return Defs;
	}

	FElysiumCombatCharacter* FindCharacter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent ? Ent->AsCombatCharacter() : nullptr;
	}

	int32 DiceSeed()
	{
		return ElysiumRng::Stream(EElysiumRngStream::Dice).GetCurrentSeed();
	}
}

// =====================================================================================
// The authored `Dmg` grammar and the soak table
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDamageParseTest, "Elysium.Substrate.Damage.Parse",
	GElysiumTestFlags)
bool FElysiumDamageParseTest::RunTest(const FString&)
{
	// The fists record: no source trait, and `DMG_FIST` is an alias rather than a bit of its own.
	{
		const FElysiumDmg Dmg = ElysiumDamage::ParseDmg(TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"));
		TestEqual(TEXT("fists parse the bashing family"), static_cast<int32>(Dmg.Family),
			static_cast<int32>(EElysiumDmgFamily::Bashing));
		TestEqual(TEXT("...the leading integer is the base damage"), Dmg.BaseDamage, 2);
		TestEqual(TEXT("...the trailing reference is the attack feat"), Dmg.AttackFeat,
			FString(TEXT("Close_Combat_Brawl")));
		TestTrue(TEXT("...and no source trait was authored"), Dmg.SourceTrait.IsEmpty());
		// RE40: `DMG_FIST` is not an engine damage flag — an unarmed attack aliases straight to
		// `DMG_CLUB`, the same bit a baton carries.
		TestTrue(TEXT("DMG_FIST aliases to DMG_CLUB"), Dmg.DmgMask == ElysiumDamage::DmgClub);
		TestFalse(TEXT("...which is not firearm damage"), Dmg.IsFirearm());
		TestFalse(TEXT("...and takes ordinary soak"), Dmg.TakesNoSoak());
		const FElysiumDmg Baton = ElysiumDamage::ParseDmg(TEXT("2 Bashing Close_Combat_Melee DMG_CLUB"));
		TestTrue(TEXT("...so a fist and a baton carry the same mask"), Dmg.DmgMask == Baton.DmgMask);
	}

	// A `DMG_` spelling that is neither a recovered bit nor the one recovered alias stays authored
	// data: no bit, and no parse failure.
	{
		const FElysiumDmg Dmg = ElysiumDamage::ParseDmg(TEXT("2 Lethal Ranged_Combat DMG_NOTATHING"));
		TestEqual(TEXT("an unrecovered DMG_ token carries no bit"), static_cast<int32>(Dmg.DmgMask), 0);
		TestEqual(TEXT("...and the rest of the grammar still parses"), Dmg.BaseDamage, 2);
	}

	// Holy light: the optional leading `CVStatRef`, and a real flag.
	{
		const FElysiumDmg Dmg =
			ElysiumDamage::ParseDmg(TEXT("Strength 2 Lethal Close_Combat_Melee DMG_FAITH"));
		TestEqual(TEXT("the leading word is the source trait"), Dmg.SourceTrait,
			FString(TEXT("Strength")));
		TestEqual(TEXT("...the base still parses"), Dmg.BaseDamage, 2);
		TestEqual(TEXT("...the family still parses"), static_cast<int32>(Dmg.Family),
			static_cast<int32>(EElysiumDmgFamily::Lethal));
		TestTrue(TEXT("...and DMG_FAITH carries its recovered bit"),
			Dmg.DmgMask == ElysiumDamage::DmgFaith);
		TestTrue(TEXT("faith is in the no-soak mask"), Dmg.TakesNoSoak());
	}

	// The firearm records, plus case-insensitive family words and multiple flags.
	{
		const FElysiumDmg Glock = ElysiumDamage::ParseDmg(TEXT("2 Lethal Ranged_Combat DMG_BULLET"));
		TestTrue(TEXT("a bullet reads as firearm damage"), Glock.IsFirearm());
		const FElysiumDmg Shotgun =
			ElysiumDamage::ParseDmg(TEXT("3 lethal Ranged_Combat DMG_BUCKSHOT DMG_CLUB"));
		TestEqual(TEXT("a lower-case family word still parses"), static_cast<int32>(Shotgun.Family),
			static_cast<int32>(EElysiumDmgFamily::Lethal));
		TestTrue(TEXT("two flags OR together"),
			Shotgun.DmgMask == (ElysiumDamage::DmgBuckshot | ElysiumDamage::DmgClub));
		TestTrue(TEXT("buckshot reads as firearm damage too"), Shotgun.IsFirearm());
	}

	// Nothing authored is an ordinary absence, and the family it leaves is what Apply rejects.
	{
		const FElysiumDmg Empty = ElysiumDamage::ParseDmg(FString());
		TestEqual(TEXT("an unauthored Dmg has no family"), static_cast<int32>(Empty.Family),
			static_cast<int32>(EElysiumDmgFamily::None));
	}

	// The 8-row soak table, family x creature x falling.
	{
		using ElysiumDamage::SoakFeatName;
		TestEqual(TEXT("mortal bashing"),
			FString(SoakFeatName(EElysiumDmgFamily::Bashing, false, false)),
			FString(TEXT("Soak_vs_Bashing")));
		TestEqual(TEXT("Kindred bashing"),
			FString(SoakFeatName(EElysiumDmgFamily::Bashing, true, false)),
			FString(TEXT("Soak_vs_Bashing_Kindred")));
		TestEqual(TEXT("mortal lethal"),
			FString(SoakFeatName(EElysiumDmgFamily::Lethal, false, false)),
			FString(TEXT("Soak_vs_Lethal")));
		TestEqual(TEXT("Kindred lethal"),
			FString(SoakFeatName(EElysiumDmgFamily::Lethal, true, false)),
			FString(TEXT("Soak_vs_Lethal_Kindred")));
		TestEqual(TEXT("mortal lethal falling"),
			FString(SoakFeatName(EElysiumDmgFamily::Lethal, false, true)),
			FString(TEXT("Soak_vs_Lethal_Falling")));
		TestEqual(TEXT("Kindred lethal falling"),
			FString(SoakFeatName(EElysiumDmgFamily::Lethal, true, true)),
			FString(TEXT("Soak_vs_Lethal_Falling_Kindred")));
		TestEqual(TEXT("mortal aggravated"),
			FString(SoakFeatName(EElysiumDmgFamily::Aggravated, false, false)),
			FString(TEXT("Soak_vs_Aggravated")));
		TestEqual(TEXT("Kindred aggravated"),
			FString(SoakFeatName(EElysiumDmgFamily::Aggravated, true, false)),
			FString(TEXT("Soak_vs_Aggravated_Kindred")));
		// The falling flag belongs to the lethal rows; bashing and aggravated grow no row from it.
		TestEqual(TEXT("the falling flag does not invent a bashing row"),
			FString(SoakFeatName(EElysiumDmgFamily::Bashing, false, true)),
			FString(TEXT("Soak_vs_Bashing")));
		TestTrue(TEXT("family None selects no soak feat"),
			SoakFeatName(EElysiumDmgFamily::None, false, false) == nullptr);
	}

	// The two composite masks are retail's own constants, not a grouping of ours.
	TestTrue(TEXT("the firearm mask is bullet | buckshot"),
		ElysiumDamage::FirearmMask == 0x04000002u);
	TestTrue(TEXT("the no-soak mask is faith | sunlight | superclawbite | burn"),
		ElysiumDamage::NoSoakMask == 0xC8000008u);
	return true;
}

// =====================================================================================
// ElysiumDamage::Apply — the confirmed order
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDamageApplyTest, "Elysium.Substrate.Damage.Apply",
	GElysiumTestFlags)
bool FElysiumDamageApplyTest::RunTest(const FString&)
{
	const FElysiumDamageContext Context;   // no rulebook: ratings read 0, no soak roll is taken

	// 1. A descriptor that never had a family set is rejected before anything else.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		FElysiumDmg Dmg;
		Dmg.ExtraInput = 5;
		Dmg.Flags = ElysiumDamage::FlagDirectInput;
		TestFalse(TEXT("a family-less descriptor is rejected"),
			ElysiumDamage::Apply(Dmg, nullptr, Victim, Context));
		TestEqual(TEXT("...and its applied word stays negative (no damage)"), Dmg.AppliedDamage, -1);
		TestFalse(TEXT("...and it never resolved"), Dmg.bResolved);
	}

	// 2. Flag 0x8 bypasses the damage ROLL and nothing else: the soak test still runs.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		FElysiumDmg Dmg = DirectDmg(EElysiumDmgFamily::Bashing, /*Successes*/ 5, /*ForcedSoak*/ 2);
		Dmg.BaseDamage = 99;   // ignored under direct input — that is the roll's pool, not a result
		ElysiumRng::SeedAll(1234);
		const int32 SeedBefore = DiceSeed();
		TestTrue(TEXT("a direct-input descriptor resolves"),
			ElysiumDamage::Apply(Dmg, nullptr, Victim, Context));
		TestEqual(TEXT("the direct input IS the damage-success count"), Dmg.RolledSuccesses, 5);
		TestEqual(TEXT("...the forced soak still applies"), Dmg.SoakSuccesses, 2);
		TestEqual(TEXT("...and the remainder is the difference"), Dmg.Remainder, 3);
		TestEqual(TEXT("...which is the applied word"), Dmg.AppliedDamage, 3);
		TestTrue(TEXT("no die was drawn"), DiceSeed() == SeedBefore);
	}

	// 3. Soak is capped to the damage it resists — it never turns a hit into healing.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		FElysiumDmg Dmg = DirectDmg(EElysiumDmgFamily::Lethal, /*Successes*/ 3, /*ForcedSoak*/ 10);
		ElysiumDamage::Apply(Dmg, nullptr, Victim, Context);
		TestEqual(TEXT("soak caps at the damage-success count"), Dmg.SoakSuccesses, 3);
		TestEqual(TEXT("...leaving nothing behind, and never a negative"), Dmg.Remainder, 0);
	}

	// 4. Automatic soak is added by the apply path, outside the soak resolver — and the four
	//    no-soak masks skip the whole step.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		EElysiumTraitContainer Container = EC::Attributes;
		int32 AutoSlot = INDEX_NONE;
		if (TestTrue(TEXT("the automatic-soak trait has a compiled slot"),
			ElysiumFindSheetSlot(TEXT("Automatic_Soak_Successes"), Container, AutoSlot)))
		{
			Victim.Sheet.SetBase(Container, AutoSlot, 2);
			Victim.RecomputeSheet();
		}
		FElysiumDmg Dmg = DirectDmg(EElysiumDmgFamily::Lethal, /*Successes*/ 5, /*ForcedSoak*/ 0);
		ElysiumDamage::Apply(Dmg, nullptr, Victim, Context);
		TestEqual(TEXT("automatic soak lands on top of the resolved soak"), Dmg.SoakSuccesses, 2);
		TestEqual(TEXT("...and reduces the remainder by exactly that"), Dmg.Remainder, 3);

		FElysiumDmg Faith = DirectDmg(EElysiumDmgFamily::Aggravated, /*Successes*/ 5,
			/*ForcedSoak*/ 10, ElysiumDamage::DmgFaith);
		ElysiumDamage::Apply(Faith, nullptr, Victim, Context);
		TestEqual(TEXT("faith damage takes no soak, forced or automatic"), Faith.SoakSuccesses, 0);
		TestEqual(TEXT("...so all of it remains"), Faith.Remainder, 5);
	}

	// 5. The Kindred firearm conversion, both ways, and the mortal case that does not convert.
	{
		FElysiumPlayer Kindred;
		SeedHealth(Kindred, 100);
		Kindred.Sheet.SetClan(FElysiumSheet::ClanFromName(TEXT("Brujah")));
		TestTrue(TEXT("a clanned character is Kindred"), ElysiumDamage::IsKindred(Kindred));

		FElysiumDmg Bullet = DirectDmg(EElysiumDmgFamily::Lethal, 4, 0, ElysiumDamage::DmgBullet);
		ElysiumDamage::Apply(Bullet, nullptr, Kindred, Context);
		TestEqual(TEXT("lethal firearm damage on a Kindred victim becomes bashing"),
			static_cast<int32>(Bullet.Family), static_cast<int32>(EElysiumDmgFamily::Bashing));

		FElysiumDmg Disallowed = DirectDmg(EElysiumDmgFamily::Lethal, 4, 0, ElysiumDamage::DmgBullet);
		ElysiumDamage::Apply(Disallowed, nullptr, Kindred, Context,
			/*bDisallowFirearmsToBashing=*/true);
		TestEqual(TEXT("...unless the weapon record disallows the conversion"),
			static_cast<int32>(Disallowed.Family), static_cast<int32>(EElysiumDmgFamily::Lethal));

		FElysiumDmg Slash = DirectDmg(EElysiumDmgFamily::Lethal, 4, 0, ElysiumDamage::DmgSlash);
		ElysiumDamage::Apply(Slash, nullptr, Kindred, Context);
		TestEqual(TEXT("a non-firearm lethal hit is not converted"),
			static_cast<int32>(Slash.Family), static_cast<int32>(EElysiumDmgFamily::Lethal));

		FElysiumPlayer Mortal;
		SeedHealth(Mortal, 100);
		Mortal.Sheet.SetClan(0);
		TestFalse(TEXT("a clanless character is mortal"), ElysiumDamage::IsKindred(Mortal));
		FElysiumDmg OnMortal = DirectDmg(EElysiumDmgFamily::Lethal, 4, 0, ElysiumDamage::DmgBullet);
		ElysiumDamage::Apply(OnMortal, nullptr, Mortal, Context);
		TestEqual(TEXT("a mortal takes firearm lethal as lethal"),
			static_cast<int32>(OnMortal.Family), static_cast<int32>(EElysiumDmgFamily::Lethal));
	}

	// 6. The rolled path and the non-botch floor. With no dice table loaded the resolver rolls the
	//    uniform d10 the rulebook itself falls back to, so what is asserted is the pool: an empty
	//    pool never enters the roller's loop and its tier stays Failure, which the floor promotes.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		FElysiumDmg Dmg;
		Dmg.Family = EElysiumDmgFamily::Bashing;
		Dmg.BaseDamage = 0;
		Dmg.ExtraInput = 0;
		Dmg.ForcedSoak = 0;
		ElysiumRng::SeedAll(99);
		ElysiumDamage::Apply(Dmg, nullptr, Victim, Context);
		TestEqual(TEXT("an empty pool still lands the non-botch floor of one"), Dmg.RolledSuccesses, 1);
		TestEqual(TEXT("...and it survives to the remainder"), Dmg.Remainder, 1);

		// A real pool draws dice. The assertion is that the roll happened, not what it rolled.
		FElysiumDmg Rolled;
		Rolled.Family = EElysiumDmgFamily::Bashing;
		Rolled.BaseDamage = 6;
		Rolled.ExtraInput = 3;   // pool = base + max(extra - 1, 0) = 8
		Rolled.ForcedSoak = 0;
		ElysiumRng::SeedAll(99);
		const int32 SeedBefore = DiceSeed();
		ElysiumDamage::Apply(Rolled, nullptr, Victim, Context);
		TestTrue(TEXT("a non-empty pool draws from the Dice stream"), DiceSeed() != SeedBefore);
		TestTrue(TEXT("...and never resolves below the non-botch floor"), Rolled.RolledSuccesses >= 1);
	}
	return true;
}

// =====================================================================================
// FElysiumCombatCharacter::CommitDamage — the one health commit
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDamageCommitTest, "Elysium.Substrate.Damage.Commit",
	GElysiumTestFlags)
bool FElysiumDamageCommitTest::RunTest(const FString&)
{
	// 1. HealthBuffer absorbs first; a partial absorption only reduces it.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		Victim.Sheet.SetBase(EC::Attributes, ElysiumSlot::HealthBuffer, 10);
		Victim.Effects.Add(TEXT("Discipline (Thaumaturgy-Bloodshield)"));
		Victim.RecomputeSheet();

		Victim.CommitDamage(ResolvedDmg(EElysiumDmgFamily::Bashing, 4));
		TestEqual(TEXT("a partial absorption only reduces the buffer"),
			Trait(Victim, ElysiumSlot::HealthBuffer), 6);
		TestEqual(TEXT("...and no damage reaches the health counter"),
			Trait(Victim, ElysiumSlot::Health), 0);
		TestEqual(TEXT("...so Bloodshield is still up"), Victim.Effects.Num(), 1);

		// Exhausting it clears the counter, ends Bloodshield and spends the overflow on health.
		Victim.CommitDamage(ResolvedDmg(EElysiumDmgFamily::Bashing, 10));
		TestEqual(TEXT("an exhausted buffer clears"), Trait(Victim, ElysiumSlot::HealthBuffer), 0);
		TestEqual(TEXT("...the overflow lands on the damage counter"),
			Trait(Victim, ElysiumSlot::Health), 4);
		TestEqual(TEXT("...and Bloodshield ends"), Victim.Effects.Num(), 0);
	}

	// 2. Unkillable caps the damage-TAKEN counter at the retail literal 75 — not at one hit point.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		Victim.SetUnkillable(true);
		Victim.CommitDamage(ResolvedDmg(EElysiumDmgFamily::Lethal, 1000));
		TestEqual(TEXT("the unkillable cap is the literal 75"),
			ElysiumDamage::UnkillableDamageCap, 75);
		TestEqual(TEXT("...so the damage counter stops there"),
			Trait(Victim, ElysiumSlot::Health), 75);
		TestEqual(TEXT("...leaving 25 health with the default ceiling"), Victim.Health, 25);
		TestFalse(TEXT("...and nobody died"), Victim.HasReportedDeath());
	}

	// 3. Kindred aggravated tracking under the no-soak mask, and the mortal case that does not.
	{
		FElysiumPlayer Kindred;
		SeedHealth(Kindred, 100);
		Kindred.Sheet.SetClan(FElysiumSheet::ClanFromName(TEXT("Gangrel")));
		Kindred.CommitDamage(
			ResolvedDmg(EElysiumDmgFamily::Aggravated, 4, ElysiumDamage::DmgSunlight));
		TestEqual(TEXT("sunlight lands on the damage counter"),
			Trait(Kindred, ElysiumSlot::Health), 4);
		TestEqual(TEXT("...and the same amount on the aggravated counter"),
			Trait(Kindred, ElysiumSlot::HealthAggDmg), 4);

		Kindred.CommitDamage(ResolvedDmg(EElysiumDmgFamily::Bashing, 3));
		TestEqual(TEXT("an unmasked hit still adds to the damage counter"),
			Trait(Kindred, ElysiumSlot::Health), 7);
		TestEqual(TEXT("...but not to the aggravated one"),
			Trait(Kindred, ElysiumSlot::HealthAggDmg), 4);

		FElysiumPlayer Mortal;
		SeedHealth(Mortal, 100);
		Mortal.Sheet.SetClan(0);
		Mortal.CommitDamage(ResolvedDmg(EElysiumDmgFamily::Aggravated, 4, ElysiumDamage::DmgFaith));
		TestEqual(TEXT("a mortal tracks no aggravated damage"),
			Trait(Mortal, ElysiumSlot::HealthAggDmg), 0);
	}

	// 4. The scalar fallback keeps its observable outcome, and death is the RPG comparison.
	{
		FElysiumPlayer Victim;
		SeedHealth(Victim, 100);
		Victim.TakeDamage(40.f);
		TestEqual(TEXT("the scalar fallback lands on the damage counter"),
			Trait(Victim, ElysiumSlot::Health), 40);
		TestEqual(TEXT("...and the health keyfield follows it down"), Victim.Health, 60);
		TestFalse(TEXT("...without dying"), Victim.HasReportedDeath());

		Victim.TakeDamage(0.4f);   // rounds to zero, and the fallback floors a positive hit at one
		TestEqual(TEXT("a sub-point hit still lands one point"),
			Trait(Victim, ElysiumSlot::Health), 41);

		Victim.TakeDamage(1000.f);
		TestEqual(TEXT("the counter stops at the ceiling"), Trait(Victim, ElysiumSlot::Health), 100);
		TestEqual(TEXT("...health runs out"), Victim.Health, 0);
		TestTrue(TEXT("...and the death latch reports once"), Victim.HasReportedDeath());
	}
	return true;
}

// =====================================================================================
// The producers: the two NPC outputs, the NPC input and the hurt cadence
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDamageProducersTest, "Elysium.Substrate.Damage.Producers",
	GElysiumTestFlags)
bool FElysiumDamageProducersTest::RunTest(const FString&)
{
	// --- OnDamaged / OnHalfHealth, through the queue, with the descriptor's Source as activator --
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDamageTestDefs());
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
		Player->Money = 0;

		FElysiumDmg Dmg = DirectDmg(EElysiumDmgFamily::Bashing, 10);
		Dmg.Source = Player->Handle;
		Victim->TakeDamage(Dmg, Player);
		TestEqual(TEXT("the commit lands the damage synchronously"),
			Trait(*Victim, ElysiumSlot::Health), 10);
		// Producers enqueue; only queue service delivers (K11), so no wire has landed yet.
		TestEqual(TEXT("the output has not been delivered inside the commit"),
			SaveTestCounterValue(World.FindByName(TEXT("damagedcount"))), 0.0f);
		World.Tick(0.0);
		TestEqual(TEXT("OnDamaged fires once per damaging hit"),
			SaveTestCounterValue(World.FindByName(TEXT("damagedcount"))), 1.0f);
		TestEqual(TEXT("...with the descriptor's Source as the activator"), Player->Money, 7);
		TestEqual(TEXT("OnHalfHealth stays quiet above half health"),
			SaveTestCounterValue(World.FindByName(TEXT("halfcount"))), 0.0f);

		FElysiumDmg Big = DirectDmg(EElysiumDmgFamily::Bashing, 45);
		Big.Source = Player->Handle;
		Victim->TakeDamage(Big, Player);
		World.Tick(0.0);
		TestEqual(TEXT("OnDamaged fires again"),
			SaveTestCounterValue(World.FindByName(TEXT("damagedcount"))), 2.0f);
		TestEqual(TEXT("OnHalfHealth is offered once health reaches half"),
			SaveTestCounterValue(World.FindByName(TEXT("halfcount"))), 1.0f);
	}

	// --- The NPC `TakeDamage` input, backed rather than pending --------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDamageTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumCombatCharacter* Victim = FindCharacter(World, TEXT("victim"));
		if (!TestNotNull(TEXT("the victim exists"), Victim))
		{
			return false;
		}
		SeedHealth(*Victim, 100);

		World.EnqueueInput(TEXT("victim"), FName(TEXT("TakeDamage")), FElysiumVariant::Int(12), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.Tick(0.0);
		TestEqual(TEXT("a numeric TakeDamage wire routes to the scalar fallback"),
			Trait(*Victim, ElysiumSlot::Health), 12);

		World.EnqueueInput(TEXT("victim"), FName(TEXT("TakeDamage")),
			FElysiumVariant::String(TEXT("lots")), 0.0, FElysiumEntityHandle::Invalid(),
			FElysiumEntityHandle::Invalid());
		World.Tick(0.0);
		TestEqual(TEXT("a non-numeric parameter is a reported no-op, not a guessed default"),
			Trait(*Victim, ElysiumSlot::Health), 12);
	}

	// --- trigger_hurt: entry half tick, then x3 every 3 s ---------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDamageTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Hurt = World.FindByName(TEXT("hurtbox"));
		if (!TestNotNull(TEXT("the player exists"), Player)
			|| !TestNotNull(TEXT("the hurt volume exists"), Hurt))
		{
			return false;
		}
		SeedHealth(*Player, 1000);

		// Entry deals `damage * 0.5`.
		World.RouteBrushTouch(Hurt->Handle, Player->Handle, /*bBegin=*/true);
		TestEqual(TEXT("entry deals a half tick"), Trait(*Player, ElysiumSlot::Health), 5);
		World.Tick(0.0);
		TestEqual(TEXT("...and fires OnHurtPlayer"),
			SaveTestCounterValue(World.FindByName(TEXT("hurtplayercount"))), 1.0f);
		TestTrue(TEXT("...and the body took the same number"), Services.DamageTaken == 5.0f);

		// The think is 3 s out, not 0.5 s.
		World.Tick(2.9);
		TestEqual(TEXT("nothing happens before the 3 s think"),
			Trait(*Player, ElysiumSlot::Health), 5);
		World.Tick(3.0);
		TestEqual(TEXT("the think deals a triple tick"), Trait(*Player, ElysiumSlot::Health), 35);
		World.Tick(6.0);
		TestEqual(TEXT("...and re-arms every 3 s while it keeps hurting"),
			Trait(*Player, ElysiumSlot::Health), 65);

		// Leaving stops it. The sustained rate was `damage` per second, which is the point.
		World.RouteBrushTouch(Hurt->Handle, Player->Handle, /*bBegin=*/false);
		World.Tick(9.0);
		TestEqual(TEXT("leaving the volume stops the cadence"),
			Trait(*Player, ElysiumSlot::Health), 65);

		// A re-entry re-arms it with another half tick.
		World.RouteBrushTouch(Hurt->Handle, Player->Handle, /*bBegin=*/true);
		TestEqual(TEXT("re-entry deals another half tick"),
			Trait(*Player, ElysiumSlot::Health), 70);
	}

	// --- trigger_hurt: HurtNow, enabled and disabled --------------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeDamageTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumEntity* Hurt = World.FindByName(TEXT("hurtbox"));
		if (!TestNotNull(TEXT("the player exists"), Player)
			|| !TestNotNull(TEXT("the hurt volume exists"), Hurt))
		{
			return false;
		}
		SeedHealth(*Player, 1000);

		World.RouteBrushTouch(Hurt->Handle, Player->Handle, /*bBegin=*/true);
		TestEqual(TEXT("entry half tick"), Trait(*Player, ElysiumSlot::Health), 5);

		// On an ENABLED volume HurtNow hurts every toucher immediately, at the think's own scale.
		World.EnqueueInput(TEXT("hurtbox"), FName(TEXT("HurtNow")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.Tick(1.0);
		TestEqual(TEXT("HurtNow on an enabled volume hurts at once"),
			Trait(*Player, ElysiumSlot::Health), 35);

		// Disabling ends the retained touch and stops the ordinary think.
		World.EnqueueInput(TEXT("hurtbox"), FName(TEXT("Disable")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.Tick(1.5);
		World.Tick(3.0);
		TestEqual(TEXT("a disabled volume stops hurting"),
			Trait(*Player, ElysiumSlot::Health), 35);

		// Against a DISABLED volume HurtNow borrows the enable and arms one pass 0.1 s out. The
		// engine's own re-overlap on enable is what re-establishes the touch; the headless harness
		// supplies that edge, and its entry half tick is part of the same enable.
		World.EnqueueInput(TEXT("hurtbox"), FName(TEXT("HurtNow")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.Tick(4.0);
		World.RouteBrushTouch(Hurt->Handle, Player->Handle, /*bBegin=*/true);
		TestEqual(TEXT("the borrowed enable produces the ordinary entry tick"),
			Trait(*Player, ElysiumSlot::Health), 40);
		World.Tick(4.05);
		TestEqual(TEXT("the one-shot has not run yet"), Trait(*Player, ElysiumSlot::Health), 40);
		World.Tick(4.1);
		TestEqual(TEXT("the one-shot deals a triple tick 0.1 s after HurtNow"),
			Trait(*Player, ElysiumSlot::Health), 70);
		World.Tick(20.0);
		TestEqual(TEXT("...and does not re-arm — the volume disarmed back to disabled"),
			Trait(*Player, ElysiumSlot::Health), 70);

		// SetDamage is a direct write onto the same key the `damage` keyvalue fills.
		World.EnqueueInput(TEXT("hurtbox"), FName(TEXT("SetDamage")), FElysiumVariant::Int(2), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.EnqueueInput(TEXT("hurtbox"), FName(TEXT("Enable")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		World.Tick(21.0);
		World.RouteBrushTouch(Hurt->Handle, Player->Handle, /*bBegin=*/true);
		TestEqual(TEXT("SetDamage changes what the entry tick is worth"),
			Trait(*Player, ElysiumSlot::Health), 71);
	}
	return true;
}
}   // namespace ElysiumDamageTests

#endif   // WITH_DEV_AUTOMATION_TESTS
