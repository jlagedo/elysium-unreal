#pragma once

// Shared Substrate-tier scaffolding for the melee contact path (LIFE5).
//
// **What this exists to remove.** The melee reaction bands come out of `rules.txt`'s
// `Melee_Reactions` block, and without them `ClassifyDefender` answers `Unclassified` — which is
// deliberately not a band, so no blocked reaction and no knockback is ever produced. Suites
// therefore used to re-make the composition a contact performs by calling the same functions in the
// same order, rather than driving `FElysiumWeapon::MeleeContact` itself. That proves a copy of the
// producer, not the producer.
//
// It is not necessary. `ElysiumSheetRules::BindTables` takes a `const FElysiumRules*` as the
// fallback every combat leaf reads when no rulebook subsystem is in reach, `FElysiumRules::Blocks`
// is a plain map, and `FElysiumWeaponContext::FromCharacter` builds its `Margins` off whichever of
// the two it finds. So a fabricated block bound here makes the real contact classify, and a case
// can assert what the producer does instead of what a re-implementation of it does.
//
// The binding is process-wide (the readers are plain-C++ leaves holding no session pointer, the
// same reason `ElysiumRng`'s streams are module-static), so it is RAII — `ElysiumLawTests.cpp` and
// `ElysiumDisciplineTests.cpp` use the identical shape. A suite that forgot to unbind would leave
// its own table answering for every case that ran after it.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"

namespace ElysiumMeleeTest
{
	// The `rules.txt` block names, spelled once. `FElysiumMeleeMargins::FromRules` refuses the whole
	// table when any single key is missing, so a fixture that omits one silently produces the same
	// `Unclassified` it exists to avoid.
	inline const TCHAR* const MeleeReactionBlock = TEXT("Melee_Reactions");
	inline const TCHAR* const DamageInfoBlock = TEXT("Damage_Info");

	// The six margin cuts, as a set a case can move one at a time.
	//
	// `ClassifyDefender` walks them in this order and takes the FIRST band the margin falls at or
	// below; anything past the last is `HitKnockback`. The defaults below therefore put a margin of
	// 1 or more into the knockback band and everything at or below 0 into a blocked one, which is
	// the shape most cases want — a landed swing knocks back, a losing one is blocked.
	//
	// They are NOT retail's authored numbers. A Substrate case asserts what the classifier does with
	// a stated table, not what `rules.txt` happens to say; the shipped values are Content-tier's to
	// check, and hard-coding them here would make this fixture a second, quieter copy of the file.
	struct FMargins
	{
		int32 AttackerBlockedMajor = -2;
		int32 AttackerBlocked = -1;
		int32 DefenderDodgeAttack = -3;
		int32 DefenderDodge = -2;
		int32 DefenderBlock = -1;
		int32 DefenderBlockStagger = 0;
	};

	// A fabricated `rules.txt` carrying the melee bands and the opposed-roll difficulties, bound as
	// the process-wide fallback for as long as this object lives.
	//
	// The `FElysiumRules` is held BY VALUE inside the fixture: `BindTables` stores a bare pointer, so
	// a table built on a caller's stack and bound here would dangle the moment that scope closed.
	struct FRulesFixture
	{
		FElysiumRules Rules;

		explicit FRulesFixture(const FMargins& Margins = FMargins())
		{
			auto Set = [this](const TCHAR* Block, const TCHAR* Key, int32 Value)
			{
				Rules.Blocks.FindOrAdd(FString(Block).ToLower())
					.Add(FString(Key).ToLower(), FString::FromInt(Value));
			};
			Set(MeleeReactionBlock, TEXT("SuccessesForAttackerBlockedMajor"),
				Margins.AttackerBlockedMajor);
			Set(MeleeReactionBlock, TEXT("SuccessesForAttackerBlocked"), Margins.AttackerBlocked);
			Set(MeleeReactionBlock, TEXT("SuccessesForDefenderDodgeAttack"),
				Margins.DefenderDodgeAttack);
			Set(MeleeReactionBlock, TEXT("SuccessesForDefenderDodge"), Margins.DefenderDodge);
			Set(MeleeReactionBlock, TEXT("SuccessesForDefenderBlock"), Margins.DefenderBlock);
			Set(MeleeReactionBlock, TEXT("SuccessesForDefenderBlockStagger"),
				Margins.DefenderBlockStagger);
			// The opposed roll's own difficulties. Without them the defence is not rolled at all and
			// the transaction says so at Warning, which would bury a suite's real output in noise.
			Set(DamageInfoBlock, TEXT("Defense_Difficulty_PC"), 6);
			Set(DamageInfoBlock, TEXT("Defense_Difficulty_NPC"), 6);
			Set(DamageInfoBlock, TEXT("Soak_Difficulty_PC"), 6);
			Set(DamageInfoBlock, TEXT("Soak_Difficulty_NPC"), 6);
			Rebind();
		}

		// Unbinding is the whole reason this is a type rather than a helper call.
		~FRulesFixture() { ElysiumSheetRules::BindTables(ElysiumSheetRules::FBoundTables()); }

		FRulesFixture(const FRulesFixture&) = delete;
		FRulesFixture& operator=(const FRulesFixture&) = delete;

		// Re-point the fallback at this object's table. Exposed because a case that edits `Rules`
		// after construction has not moved the table, only its contents — but a case that wants a
		// second fixture's table back needs a way to say so.
		void Rebind()
		{
			ElysiumSheetRules::FBoundTables Bound;
			Bound.Rules = &Rules;
			ElysiumSheetRules::BindTables(Bound);
		}
	};
}

#endif   // WITH_DEV_AUTOMATION_TESTS
