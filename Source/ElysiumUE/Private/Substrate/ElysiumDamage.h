#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"   // the descriptor's source handle (word 13)

// The typed damage descriptor and the one shared apply path — VtMB's `CVDmg_t` and
// `CVDmg_t::Apply` made a value object plus a free function, beside `ElysiumDice` and
// `ElysiumSheetMath`.
//
// The behaviour reproduced here is canonical in `docs/vtmb/combat-and-damage.md`; the check
// boundary (a rating is not a roll) is `docs/vtmb/skills-and-checks.md`. Nothing in this file
// touches health: `Apply` resolves how much damage a hit is worth, and
// `FElysiumCombatCharacter::CommitDamage` is the one place that spends it.
//
// The descriptor mirrors the retail 17-word layout member for member so an audit can be compared
// word by word; the result block at the end is this runtime's addition, and exists so every
// intermediate the retail debug print exposes is readable without re-running the roll.

class FElysiumCombatCharacter;
struct FElysiumDiceTables;
struct FElysiumFeatTable;

// Word 0. The values are retail's own: a descriptor that never had a family set reads -1 and is
// rejected before anything else happens.
enum class EElysiumDmgFamily : int32
{
	None = -1,
	Bashing = 0,
	Lethal = 1,
	Aggravated = 2,
};

const TCHAR* ElysiumDmgFamilyName(EElysiumDmgFamily Family);

namespace ElysiumDamage
{
	// --- Word 4: the Source `DMG_*` bits the authored `Dmg` grammar names --------------------
	// The recovered string-to-bit table. `DMG_FIST` is authored by the fists item record and is not
	// an engine damage bit of its own: the parser aliases it to `DMG_CLUB`, so an unarmed hit carries
	// the club bit (`combat-and-damage.md` § "Reverse-engineered mechanics (RE40)" -> DMG_FIST Alias).
	inline constexpr uint32 DmgBullet        = 0x00000002u;
	inline constexpr uint32 DmgSlash         = 0x00000004u;
	inline constexpr uint32 DmgBurn          = 0x00000008u;
	inline constexpr uint32 DmgBlast         = 0x00000040u;
	inline constexpr uint32 DmgClub          = 0x00000080u;
	inline constexpr uint32 DmgBuckshot      = 0x04000000u;
	inline constexpr uint32 DmgSuperClawBite = 0x08000000u;
	inline constexpr uint32 DmgClawBite      = 0x10000000u;
	inline constexpr uint32 DmgSunlight      = 0x40000000u;
	inline constexpr uint32 DmgFaith         = 0x80000000u;

	// The two composite masks the apply path tests. Both are retail's own constants, not a
	// grouping of ours: the firearm pair selects the Kindred lethal->bashing conversion, and
	// `0xC8000008` (faith | sunlight | superclawbite | burn) is the set that takes no soak at all
	// and, on a Kindred victim, also accumulates aggravated damage.
	inline constexpr uint32 FirearmMask = DmgBullet | DmgBuckshot;
	inline constexpr uint32 NoSoakMask  = 0xC8000008u;

	// --- Word 16: the resolver flags ---------------------------------------------------------
	// Bit 0x8 takes word 3 as the damage-success count instead of rolling. It bypasses the damage
	// ROLL only — the soak test still runs.
	inline constexpr uint32 FlagDirectInput = 0x8u;
	// Bit 0x20 selects the falling row of the soak table.
	inline constexpr uint32 FlagFalling = 0x20u;

	// The damage pool's target number. Hardcoded in the pinned binary's apply callback rather than
	// authored, which is why it is a constant here and the soak difficulties below are not.
	inline constexpr int32 DamageRollDifficulty = 6;

	// The weighting table the damage pool rolls on. Every shipped feat names `Normal` too.
	inline const TCHAR* DamageRollWeighting = TEXT("Normal");

	// The unkillable ceiling on the damage-TAKEN counter. A retail literal, not a percentage and
	// not a one-hit-point floor: with the default `Max_Health` of 100 it leaves 25 health, and with
	// any other ceiling it leaves whatever that ceiling minus 75 is.
	inline constexpr int32 UnkillableDamageCap = 75;
}

// `CVDmg_t` — 17 words, `0x44` bytes. Word numbers are the retail offsets/2 documented in
// `combat-and-damage.md`; the names are semantic working names.
struct FElysiumDmg
{
	EElysiumDmgFamily Family = EElysiumDmgFamily::None;   // word 0
	int32 BaseDamage = 0;                                 // word 1 — the authored leading integer
	// Word 2 — the applied/final damage. NEGATIVE means "no damage", which is the state an
	// unresolved descriptor starts in; `Apply` writes it and the commit spends it.
	int32 AppliedDamage = -1;
	// Word 3 — extra roll dice, or the damage-success count under flag 0x8.
	int32 ExtraInput = 0;
	uint32 DmgMask = 0;                                   // word 4 — the Source `DMG_*` bits
	// Words 5..8 — the optional leading `CVStatRef` of the authored grammar. Held as the authored
	// name because that is what `SetSrc` resolves and what an audit has to be able to print.
	FString SourceTrait;
	FString AttackFeat;                                   // words 9..12 — the attack feat/reference
	FElysiumEntityHandle Source;                          // word 13
	// Word 14 — a forced soak value. NEGATIVE selects the normal soak resolver.
	int32 ForcedSoak = -1;
	// Word 15 — the accumulated template damage filter. Populated where the victim's data allows
	// and deliberately NOT multiplied into the result: the point at which retail turns it into
	// committed health damage is an open join (`combat-and-damage.md` step 9). RE40 places the
	// special-modifier/immunity half of that step in `ApplySpecialDamageModifier`, whose 224-entry
	// weapon/damage-id table raises condition flags and reactive audio rather than scaling the
	// number, so the accumulator's own commit point stays unrecovered. Authored inputs are float
	// multipliers, so the accumulator is a float here; 0 means "nothing accumulated".
	float FilterAccumulator = 0.0f;
	uint32 Flags = 0;                                     // word 16 — the resolver flags

	// --- The audit block ----------------------------------------------------------------------
	// Not part of the retail structure. Every intermediate the retail diagnostic prints, kept so a
	// comparison never has to re-run a roll that consumed RNG state.
	int32 RolledSuccesses = 0;   // damage successes after the roll (or the direct input)
	int32 SoakSuccesses = 0;     // total soak actually applied, after the cap
	int32 Remainder = 0;         // max(damage - soak, 0) — what the commit spends
	bool  bResolved = false;     // Apply ran to completion on this descriptor

	bool IsDirectInput() const { return (Flags & ElysiumDamage::FlagDirectInput) != 0; }
	bool IsFalling() const { return (Flags & ElysiumDamage::FlagFalling) != 0; }
	bool IsFirearm() const { return (DmgMask & ElysiumDamage::FirearmMask) != 0; }
	bool TakesNoSoak() const { return (DmgMask & ElysiumDamage::NoSoakMask) != 0; }

	// `GetDmg` — retail's own accessor: word 2 when positive, word 1 when word 2 is zero, and zero
	// when word 2 is negative. It reads a descriptor's damage BEFORE it has been applied; the
	// commit uses `CommittedDamage` instead.
	int32 GetDmg() const;

	// What the health commit spends: the applied result, floored at zero.
	int32 CommittedDamage() const { return AppliedDamage > 0 ? AppliedDamage : 0; }

	FString Describe() const;
};

// The rulebook halves `Apply` needs, gathered once so the resolver stays free of the subsystem and
// runs headless. Every member may be absent: a bare test world carries no rulebook, and the
// resolver then fails safe (rating 0, no soak roll) with one warning rather than a silent default.
struct FElysiumDamageContext
{
	const FElysiumFeatTable*  Feats = nullptr;
	const FElysiumDiceTables* DiceTables = nullptr;

	// `rules.txt` -> `RuleData/Damage_Info/Soak_Difficulty_PC` and `_NPC`. INDEX_NONE means the
	// rulebook did not answer, which is the only reason this is not simply an int pair: the numbers
	// are data and are never retyped here.
	int32 SoakDifficultyPc = INDEX_NONE;
	int32 SoakDifficultyNpc = INDEX_NONE;

	bool HasSoakDifficulties() const
	{
		return SoakDifficultyPc != INDEX_NONE && SoakDifficultyNpc != INDEX_NONE;
	}

	// Gather from the rulebook the character's world holds. Every member stays null/absent when
	// there is no game state, which is the ordinary headless case.
	static FElysiumDamageContext FromCharacter(const FElysiumCombatCharacter& Char);
};

namespace ElysiumDamage
{
	// Parse an authored weapon-mode `Dmg` string:
	//
	//     [optional source trait] <base damage integer> <damage family> [DMG_* flags] [attack feat]
	//
	// e.g. `2 Bashing Close_Combat_Brawl DMG_FIST`, `Strength 2 Lethal Close_Combat_Melee DMG_FAITH`.
	// Family words are case-insensitive. `DMG_FIST` aliases to `DMG_CLUB`; a `DMG_*` token that is
	// neither a recovered bit nor that alias carries no bit — authored data rather than a failure, so
	// it reports at Verbose.
	FElysiumDmg ParseDmg(const FString& Authored);

	// The soak feat's `feats.txt` InternalName for a family/creature/falling combination — the
	// 8-row table in `combat-and-damage.md` § "Soak selection". Returns nullptr for family None.
	const TCHAR* SoakFeatName(EElysiumDmgFamily Family, bool bKindred, bool bFalling);

	// Whether this character soaks as a vampire. See `FElysiumCombatCharacter::IsKindred`.
	bool IsKindred(const FElysiumCombatCharacter& Char);

	// `CVDmg_t::Apply` — the confirmed order in `combat-and-damage.md` § "The common
	// `CVDmg_t::Apply` path". Writes the result into `Dmg` (word 2 plus the audit block) and
	// returns whether the descriptor resolved at all; a family-less descriptor is rejected.
	//
	// `bDisallowFirearmsToBashing` is the attacker's active weapon record's
	// `Disallow_FirearmsToBashing`. The item-record join that supplies it arrives with the weapon
	// classes; until then every caller leaves it at the authored default of false.
	bool Apply(FElysiumDmg& Dmg, FElysiumCombatCharacter* Attacker, FElysiumCombatCharacter& Victim,
		const FElysiumDamageContext& Context, bool bDisallowFirearmsToBashing = false);
}
