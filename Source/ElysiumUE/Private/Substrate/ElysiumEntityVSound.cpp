// `CBaseEntity`'s voice-table words and the bodies that write and read them: slot 70 `IsMonster`
// (`0x1009d820`), slot 71 `PrecacheSoundTable` (`0x1009d460`), the three lazy getters
// `GetVSoundTableIdx` / `GetVSoundGroup` / `GetVSoundGroupFemale` (`0x1009d5e0` / `0x1009d6a0` /
// `0x1009d760`) and the entity's call into the group seam `FUN_101f55a0`. The walk is
// `docs/specs/layers/L0-entity/walks/L0-r007.md`; the prose `docs/vtmb/audio_pipeline.md` § 7b.
//
// The words (`ElysiumEntity.h`): `VSoundGroup` (`+0xb4 m_iVSoundGroup`), `VSoundGroupFemale` (`+0xb8
// m_iVSoundGroupFemale`), `VSoundTableIdx` (`+0xbc m_iVSoundTableIdx`), all -2 from the constructor
// `0x1009d980`, and `SoundGroup` (`+0xc0 m_iszVSoundGroup`, the `soundgroup` key). Declarations of
// the non-slot members are in `ElysiumEntitySlotBodies.inl`; the two slots are declared by the
// generator (`CHAIN_HAND` 70 / 71) and defined here.

#include "ElysiumEntity.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"                     // FElysiumCombatCharacter: `+0x9c`, `Sheet.IsMale`, the template hook
#include "ElysiumRetailSite.h"
#include "Substrate/ElysiumItemClasses.h"      // FElysiumItem::AsWeapon: `+0xa0`
#include "Substrate/ElysiumRulebook.h"         // FElysiumClanTemplate: the template's "Monster"
#include "Substrate/ElysiumVSoundGroup.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace
{
	// The four `+0xbc` category values the base body writes: `SndScheme_Char`'s categories in the order
	// `FUN_101f66c0` adds them (`Female`, `Male`, `Monster`, `Animal`).
	constexpr int32 GVSoundCategoryFemale = 0;
	constexpr int32 GVSoundCategoryMale = 1;
	constexpr int32 GVSoundCategoryMonster = 2;
	constexpr int32 GVSoundCategoryAnimal = 3;

	const TCHAR* const GPrecacheSoundTableFn = TEXT("CBaseEntity::PrecacheSoundTable");
	constexpr uint32 GPrecacheSoundTableVa = 0x1009d460u;
}

// -------------------------------------------------------------------------------------------------
// Slot 70 -- `CBaseEntity::IsMonster` `0x1009d820` (31 B)
// -------------------------------------------------------------------------------------------------

bool FElysiumEntity::IsMonster()
{
	// `if (+0x9c != 0) return FUN_101d5f10(&DAT_10738d10, +0x9c)[+0x8e]; return false;`
	// `FUN_101d5f10` is the char-template fetch: `GetCharTemplate(cc)`, then `table[idx]` when the index
	// is in range, else the default record `DAT_10738e50` (built by `FUN_101d3850`, every flag 0).
	// Byte `+0x8e` is the template's `General` block `"Monster"` bool (`FUN_101d4520`; siblings `Kindred`
	// `+0x8c`, `Animal` `+0x8d`, `Supernatural` `+0x8f`).
	//
	// HOOK (hooks.tsv:57, L0 -> L2): `FElysiumCombatCharacter::CharTemplateRecord` stands for
	// `FUN_101d5f10`; a null record is the default record, so the flag reads 0.
	//
	// Nine NPC classes override this slot (`CNPC_VGargoyle` 0x10379470 and `CNPC_VWerewolf` 0x103ccb50
	// answer a constant true; the seven others are unread); those are their classes' rows, not this one.
	//
	// Sites (L0-r020, `walks/L0-r020.md`): `is_monster` at the return (`result= arm=<no_combat_character|
	// template>`) and, on the template arm, `char_template` for the hook's answer (`record=<table|default>
	// monster= template=<name|->`). Retail has no trace push here; the sites are the record's observation.
	const FElysiumCombatCharacter* CombatCharacter = AsCombatCharacter();
	if (CombatCharacter == nullptr)
	{
		if (World)
		{
			World->EmitRetailSite(*this, TEXT("is_monster"), TEXT("CBaseEntity::IsMonster"), 0x1009d820u, TEXT("return"),
				TEXT("result=0 arm=no_combat_character"));
		}
		return false;
	}
	const FElysiumClanTemplate* Record = CombatCharacter->CharTemplateRecord();
	const bool bMonster = Record != nullptr && Record->GeneralInt(TEXT("Monster")) != 0;
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("char_template"), TEXT("FUN_101d5f10"), 0x101d5f10u, TEXT("return"),
			FString::Printf(TEXT("record=%s monster=%d template=%s"), Record != nullptr ? TEXT("table") : TEXT("default"),
				bMonster ? 1 : 0, Record != nullptr ? *Record->TemplateName : TEXT("-")));
		World->EmitRetailSite(*this, TEXT("is_monster"), TEXT("CBaseEntity::IsMonster"), 0x1009d820u, TEXT("return"),
			FString::Printf(TEXT("result=%d arm=template"), bMonster ? 1 : 0));
	}
	return bMonster;
}

// -------------------------------------------------------------------------------------------------
// Slot 71 -- `CBaseEntity::PrecacheSoundTable` `0x1009d460` (290 B)
// -------------------------------------------------------------------------------------------------

void FElysiumEntity::PrecacheSoundTable()
{
	// The `g_ScopeTraceStack` push/pop around the body (label 0x10555390, `m_iName` or `"NULL ENTITY"`)
	// is instrumentation and is not carried. 483 of slot 71's 501 implementers run this body; the
	// 18 others (buttons, doors, terminals, containers, prop switches, the game rules) are their
	// classes' rows.
	//
	// Re-entry: arm 6 leaves `+0xbc` at -2, and the seam's first act with a loaded table is
	// `GetVSoundTableIdx`, which re-dispatches slot 71 below 0 -- in retail an unbounded recursion no
	// shipped entry point reaches (every one has `+0x9c` or `+0xa0` set, or overrides the slot). The
	// port refuses the re-dispatch while this body is running (`bInPrecacheSoundTable`), so the seam
	// sees -2 and takes its `badidx` refusal instead of the stack fault. A named divergence on a path
	// retail never returns from.
	TGuardValue<bool> ReentryGuard(bInPrecacheSoundTable, true);
	FElysiumEntityRetailSites Sites(World, *this);
	auto CategorySite = [&Sites](const TCHAR* Arm, const TCHAR* At)
	{
		Sites.Site(TEXT("l0.voice.category"), GPrecacheSoundTableFn, GPrecacheSoundTableVa, TEXT("write"),
			FString::Printf(TEXT("arm=%s at=%s"), Arm, At));
	};

	// The category block over the three self-caches: `+0x9c m_pCombatCharacter` (`AsCombatCharacter`),
	// `+0xa0 m_pCombatWeapon` (`AsItem()->AsWeapon()`), `+0xac m_pAnimal` (`AsAnimal`) -- each zeroed
	// by the CBaseEntity constructor and set to `this` by the named class's constructor (0x10326de0,
	// 0x10250ac0, 0x1035eba0).
	FElysiumCombatCharacter* CombatCharacter = AsCombatCharacter();
	FElysiumItem* Item = AsItem();
	const bool bWeapon = Item != nullptr && Item->AsWeapon() != nullptr;
	if (CombatCharacter != nullptr)
	{
		if (AsAnimal() != nullptr)
		{
			// Arm 1. `+0x9c` and `+0xac` set: `+0xbc = 3` (store 0x1009d4de).
			VSoundTableIdx = GVSoundCategoryAnimal;
			CategorySite(TEXT("3"), TEXT("0x1009d4de"));
		}
		else if (IsMonster())
		{
			// Arm 2. The virtual `[vtbl+0x118]` -- slot 70 -- through the dispatch (call 0x1009d4ee, test
			//        0x1009d4f6): `+0xbc = 2` (store 0x1009d4f8).
			VSoundTableIdx = GVSoundCategoryMonster;
			CategorySite(TEXT("2"), TEXT("0x1009d4f8"));
		}
		else if (CombatCharacter->Sheet.IsMale())
		{
			// Arm 3. `CBaseCombatCharacter::IsMale(+0x9c)` (call 0x1009d50a -> thunk 0x100092be ->
			//        0x10336920, no stack argument; test 0x1009d511): `+0xbc = 1` (store 0x1009d513).
			//        HOOK (hooks.tsv:56, L0 -> L2 character): `IsMale` is `GetValue(stat, 0xb) == 1` on
			//        the character's first unflagged stat list, the Gender attribute -- the port's
			//        `FElysiumSheet::IsMale`.
			VSoundTableIdx = GVSoundCategoryMale;
			CategorySite(TEXT("1"), TEXT("0x1009d513"));
		}
		else
		{
			// Arm 4. Not a monster, not male: `+0xbc = 0` (store 0x1009d529).
			VSoundTableIdx = GVSoundCategoryFemale;
			CategorySite(TEXT("0"), TEXT("0x1009d529"));
		}
	}
	else if (bWeapon)
	{
		// Arm 5. `+0x9c == 0`, `+0xa0 != 0`: the same store as arm 4, `+0xbc = 0` (0x1009d529).
		VSoundTableIdx = GVSoundCategoryFemale;
		CategorySite(TEXT("0"), TEXT("0x1009d529"));
	}
	else
	{
		// Arm 6. Neither cache set: the `JZ` at 0x1009d527 jumps to the tail and `+0xbc` is NOT written
		//        (it keeps the constructor's -2).
		CategorySite(TEXT("none"), TEXT("0x1009d527"));
	}

	// The tail, on every path (0x1009d533): `name = m_iszVSoundGroup` (`+0xc0`), or "" (0x106b8540)
	// when NULL; `+0xb4 = FUN_101f55a0(&DAT_1073dc28, this, name, 0)` (call 0x1009d54b, store
	// 0x1009d550); the name is recomputed by the same rule; `+0xb8 = FUN_101f55a0(&DAT_1073dc28, this,
	// name, 1)` (call 0x1009d56e, store 0x1009d573). `DAT_1073dc28` is `SndScheme_Char`.
	const FString Name = SoundGroup;
	Sites.Site(TEXT("l0.voice.group"), GPrecacheSoundTableFn, GPrecacheSoundTableVa, TEXT("call"),
		FString::Printf(TEXT("at=0x1009d54b flag=0 group=%s"), *Name));
	VSoundGroup = VSoundGroupIndexFor(*Name, EElysiumVSoundSex::Normal);
	Sites.Site(TEXT("l0.voice.group"), GPrecacheSoundTableFn, GPrecacheSoundTableVa, TEXT("write"),
		FString::Printf(TEXT("at=0x1009d550 field=+0xb4 value=%d"), VSoundGroup));

	Sites.Site(TEXT("l0.voice.group"), GPrecacheSoundTableFn, GPrecacheSoundTableVa, TEXT("call"),
		FString::Printf(TEXT("at=0x1009d56e flag=1 group=%s"), *Name));
	VSoundGroupFemale = VSoundGroupIndexFor(*Name, EElysiumVSoundSex::Female);
	Sites.Site(TEXT("l0.voice.group"), GPrecacheSoundTableFn, GPrecacheSoundTableVa, TEXT("write"),
		FString::Printf(TEXT("at=0x1009d573 field=+0xb8 value=%d"), VSoundGroupFemale));
}

// -------------------------------------------------------------------------------------------------
// The seam call `thunk_FUN_101f55a0(&DAT_1073dc28, this, group, flag)` an entity makes
// -------------------------------------------------------------------------------------------------

int32 FElysiumEntity::VSoundGroupIndexFor(const TCHAR* Group, EElysiumVSoundSex Sex)
{
	// `&DAT_1073dc28` is `SndScheme_Char`: in this port `FElysiumEntityWorld::VSoundCharRegistry`, the
	// L2 data hook (`Substrate/ElysiumVSoundGroup.h`); null reads as the unloaded table (S0 -> 0). A
	// NULL group is substituted by "" at every retail call site, and here.
	FElysiumEntityRetailSites Sites(World, *this);
	return ElysiumVSoundGroup::ResolveGroupIndex(World != nullptr ? World->VSoundCharRegistry : nullptr, *this,
		Group != nullptr ? Group : TEXT(""), Sex, &Sites);
}

// -------------------------------------------------------------------------------------------------
// The three lazy getters
// -------------------------------------------------------------------------------------------------

int32 FElysiumEntity::GetVSoundTableIdx()
{
	// `CBaseEntity::GetVSoundTableIdx` 0x1009d5e0 (thunk 0x10008922): `if (+0xbc < 0) [vtbl+0x11c]();
	// return +0xbc;` -- any negative value, so the constructor's -2 AND an override's explicit -1 both
	// re-run slot 71. The scope-trace frame (label 0x105553b8) is not carried. The re-dispatch is
	// refused while slot 71 is already running on this entity (see `PrecacheSoundTable`).
	if (VSoundTableIdx < 0 && !bInPrecacheSoundTable)
	{
		PrecacheSoundTable();
	}
	return VSoundTableIdx;
}

int32 FElysiumEntity::GetVSoundGroup()
{
	// `CBaseEntity::GetVSoundGroup` 0x1009d6a0 (thunk 0x100128cd): `if (+0xb4 < -1) [vtbl+0x11c]();
	// return +0xb4;` -- below -1 only, so the seam's explicit miss (-1, S1b-iii) does not retrigger.
	if (VSoundGroup < -1 && !bInPrecacheSoundTable)
	{
		PrecacheSoundTable();
	}
	return VSoundGroup;
}

int32 FElysiumEntity::GetVSoundGroupFemale()
{
	// `CBaseEntity::GetVSoundGroupFemale` 0x1009d760 (thunk 0x1001320a): `if (+0xb8 < -1)
	// [vtbl+0x11c](); return +0xb8;`.
	if (VSoundGroupFemale < -1 && !bInPrecacheSoundTable)
	{
		PrecacheSoundTable();
	}
	return VSoundGroupFemale;
}
