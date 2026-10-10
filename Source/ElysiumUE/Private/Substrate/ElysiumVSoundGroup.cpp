#include "Substrate/ElysiumVSoundGroup.h"

#include "Audio/ElysiumSoundFolderIndex.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"                     // FElysiumCombatCharacter: `+0x9c`, the template hook
#include "ElysiumRetailSite.h"
#include "Substrate/ElysiumItemClasses.h"      // FElysiumItem::Data(), the item record (S1a)
#include "Substrate/ElysiumItemTable.h"        // FElysiumItemDef: `sound_group` and the `+0x2554` cache
#include "Substrate/ElysiumRulebook.h"         // FElysiumClanTemplate: the template's "SoundGroup"
#include "Substrate/ElysiumWeaponClasses.h"    // FElysiumWeapon: `+0xa0`

namespace
{
	// The strings the seam formats with (`%s\Female_PC_Override` at 0x105a643c, `Female_PC_Override` at
	// 0x105a6424, `func_button` 0x1055cb18, `func_rot_button` 0x1055cb68).
	const TCHAR* const GFemalePcOverride = TEXT("Female_PC_Override");
	const TCHAR* const GFuncButton = TEXT("func_button");
	const TCHAR* const GFuncRotButton = TEXT("func_rot_button");

	// The item-record sentinel `FUN_10258b00` writes to `+0x2554` before a record is parsed, and the
	// value S1a compares against (`CMP 0x101f56d3`).
	constexpr int32 GItemRecordUnlooked = -2;

	// The dummy item record `DAT_1089d810` a weapon whose record index is 0xffff resolves to: its
	// `+0x2554` sits past the `.data` raw size and loads as 0, its `+0x24d4` is "" -- so the first
	// weapon without a record caches the lookup of "" here (the walk's derivation; not run in retail).
	int32 GDummyItemRecordGroupIndex = 0;

	bool IsEmptyGroup(const TCHAR* Group)
	{
		return Group == nullptr || *Group == TEXT('\0');
	}

	void SeamSite(IElysiumRetailSiteSink* Sites, const TCHAR* Arm, int32 Value, const TCHAR* Group, int32 Idx,
		EElysiumVSoundSex Sex)
	{
		if (Sites != nullptr)
		{
			Sites->Site(TEXT("l0.voice.seam"), TEXT("Global::FUN_101f55a0"), 0x101f55a0u, TEXT("return"),
				FString::Printf(TEXT("arm=%s value=%d group=%s idx=%d female=%d"), Arm, Value,
					IsEmptyGroup(Group) ? TEXT("(empty)") : Group, Idx, Sex == EElysiumVSoundSex::Female ? 1 : 0));
		}
	}

	// `thunk_FUN_101f42a0(*(reg+0x20 + idx*4), group)`: the category table at `idx`, unchecked in
	// retail (`tbl[idx]` is a plain indexed load). An index the array does not hold is a read past it
	// there -- the arm-6 recursion is the only way to reach one, and `GetVSoundTableIdx`'s re-entry
	// refusal (`ElysiumEntityVSound.cpp`) hands -2 here. The port answers -1 and names the arm
	// (`badidx`), a stated divergence on a path retail never returns from.
	int32 Lookup(IElysiumVSoundRegistry& Registry, int32 Idx, const TCHAR* Group, IElysiumRetailSiteSink* Sites,
		bool& bOutBadIndex)
	{
		ElysiumSoundFolder::FOwner* Table = Registry.Table(Idx);
		if (Table == nullptr)
		{
			bOutBadIndex = true;
			return -1;
		}
		bOutBadIndex = false;
		// `FUN_101f39d0`'s `Q_strncpy(buf, NULL)` would fault; every retail caller substitutes "" for a
		// NULL group before the call, and so does the port.
		return Table->GroupIndex(Group != nullptr ? Group : TEXT(""), Sites);
	}
}

namespace ElysiumVSoundGroup
{
	int32 ResolveGroupIndex(IElysiumVSoundRegistry* Registry, FElysiumEntity& Entity, const TCHAR* Group,
		EElysiumVSoundSex Sex, IElysiumRetailSiteSink* Sites)
	{
		// S0. `*(reg+0x20) == 0` -> 0. A null registry is the port's "SndScheme_Char not built" (the L2
		//     hook unfilled) and reads as the same unloaded table.
		if (Registry == nullptr || !Registry->HasTables())
		{
			SeamSite(Sites, TEXT("S0"), 0, Group, -1, Sex);
			return 0;
		}
		// `idx = CBaseEntity::GetVSoundTableIdx(ent)` (call 0x101f55cb): re-runs slot 71 below 0.
		const int32 Idx = Entity.GetVSoundTableIdx();
		const bool bFemale = Sex == EElysiumVSoundSex::Female;
		bool bBadIndex = false;

		// `param_1[0x27]` -- `m_pCombatCharacter` (`+0x9c`), the self-cache the CBaseCombatCharacter
		// constructor 0x10326de0 sets.
		FElysiumCombatCharacter* CombatCharacter = Entity.AsCombatCharacter();
		if (CombatCharacter != nullptr)
		{
			// S2. `___RTDynamicCast(ent, 0, CBaseEntity 0x10538764, CBaseTerminal 0x105606d4, 0)`.
			if (Entity.AsTerminal() != nullptr)
			{
				// S2a. A combat character that is also a CBaseTerminal: no such class exists (a terminal is
				//      a CBaseVampireSkillEntity / CBreakableProp), so this arm never runs in retail. Kept
				//      as the body has it.
				FString Name = Group != nullptr ? Group : TEXT("");
				if (bFemale)
				{
					Name = Name.IsEmpty() ? FString(GFemalePcOverride)
						: FString::Printf(TEXT("%s\\%s"), *Name, GFemalePcOverride);
				}
				const int32 Value = Lookup(*Registry, Idx, *Name, Sites, bBadIndex);
				SeamSite(Sites, bBadIndex ? TEXT("badidx") : TEXT("S2a"), Value, *Name, Idx, Sex);
				return Value;
			}
			// S2b. An empty group reads the template record's "SoundGroup" (`FUN_101d5f10(&DAT_10738d10,
			//      cc)[+0x20]`, loaded by `FUN_101d3f10` at 0x101d4328; NULL when blank, read as "").
			//      HOOK (hooks.tsv:111, L0 -> L2): `FElysiumCombatCharacter::CharTemplateRecord`; a null
			//      record is retail's default record `DAT_10738e50` (every string NULL).
			FString Name = Group != nullptr ? Group : TEXT("");
			if (Name.IsEmpty())
			{
				const FElysiumClanTemplate* Record = CombatCharacter->CharTemplateRecord();
				Name = Record != nullptr ? Record->GeneralStr(TEXT("SoundGroup")) : FString();
			}
			if (bFemale)
			{
				// `param_3 != 0`: empty -> "Female_PC_Override" (0x105a6424); else sprintf
				// "%s\Female_PC_Override" (0x105a643c, call 0x101f56a5) into the 260-byte local.
				Name = Name.IsEmpty() ? FString(GFemalePcOverride)
					: FString::Printf(TEXT("%s\\%s"), *Name, GFemalePcOverride);
			}
			// S-final (join 0x101f580b, call 0x101f5811).
			const int32 Value = Lookup(*Registry, Idx, *Name, Sites, bBadIndex);
			SeamSite(Sites, bBadIndex ? TEXT("badidx") : TEXT("S2b"), Value, *Name, Idx, Sex);
			return Value;
		}

		// S1. `m_pCombatCharacter == 0`. `param_1[0x28]` -- `m_pCombatWeapon` (`+0xa0`), the self-cache
		//     the CBaseCombatWeapon constructor 0x10250ac0 sets.
		FElysiumItem* Item = Entity.AsItem();
		FElysiumWeapon* Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
		if (Weapon != nullptr)
		{
			// S1a. `w = FUN_102517b0(weapon)` -- the item record (`FUN_10259de0(u16 at weapon+0x8da)`;
			//      HOOK hooks.tsv:110, L0 -> L2: the port's `FElysiumItem::Data()`; a null record is the
			//      dummy `DAT_1089d810`). The female flag is NOT read on this arm.
			const FElysiumItemDef* Record = Weapon->Data();
			int32& Cache = Record != nullptr ? Record->VSoundGroupIndex : GDummyItemRecordGroupIndex;
			if (Cache != GItemRecordUnlooked)
			{
				FString Name = Group != nullptr ? Group : TEXT("");
				if (Name.IsEmpty())
				{
					// `w + 0x24d4`: the record's `sound_group` (0x80 bytes, "" by default; `FUN_10259f80`
					// at 0x1025a8e8).
					Name = Record != nullptr ? Record->SoundGroup : FString();
				}
				const int32 Value = Lookup(*Registry, Idx, *Name, Sites, bBadIndex);
				Cache = Value;                                                   // store 0x101f570d
			}
			// `return *(w + 0x2554)` (load 0x101f571a): -2 for every loaded record, unlooked.
			SeamSite(Sites, bBadIndex ? TEXT("badidx") : TEXT("S1a"), Cache, Group, Idx, Sex);
			return Cache;
		}
		// `param_1[0x29]` -- `+0xa4`: zeroed by the CBaseEntity constructor, no writer found
		// (UNRECOVERED). The port has no such word, so the test reads 0 and S1b runs.
		constexpr bool bUnknownA4Set = false;
		if (!bUnknownA4Set)
		{
			// S1b-i. `___RTDynamicCast(ent, 0, CBaseEntity, CPropSwitch 0x105a6404, 0)` (call 0x101f574a)
			//        -> lookup `group` (call 0x101f5764).
			if (Entity.AsPropSwitch() != nullptr)
			{
				const int32 Value = Lookup(*Registry, Idx, Group, Sites, bBadIndex);
				SeamSite(Sites, bBadIndex ? TEXT("badidx") : TEXT("S1b-i"), Value, Group, Idx, Sex);
				return Value;
			}
			// S1b-ii. `__strcmpi(m_iClassname or "", "func_button")` (0x101f578b), then
			//         `"func_rot_button"` (0x101f57ac): a match on either falls to S-final (0x101f5803).
			const FString Classname = Entity.Def != nullptr ? Entity.Def->Classname : FString();
			const bool bButton = Classname.Equals(GFuncButton, ESearchCase::IgnoreCase)
				|| Classname.Equals(GFuncRotButton, ESearchCase::IgnoreCase);
			if (!bButton)
			{
				// S1b-iii. `___RTDynamicCast(ent, 0, CBaseEntity, CBaseTerminal 0x105606d4, 0)` (call
				//          0x101f57c7): NULL -> -1 (`OR EAX,-1` 0x101f57f6, the seam's only explicit -1);
				//          else lookup `group` (call 0x101f57e1).
				if (Entity.AsTerminal() == nullptr)
				{
					SeamSite(Sites, TEXT("S1b-iii-miss"), -1, Group, Idx, Sex);
					return -1;
				}
				const int32 Value = Lookup(*Registry, Idx, Group, Sites, bBadIndex);
				SeamSite(Sites, bBadIndex ? TEXT("badidx") : TEXT("S1b-iii"), Value, Group, Idx, Sex);
				return Value;
			}
		}
		// S-final (join 0x101f580b, call 0x101f5811, RET 0x101f5820).
		const int32 Value = Lookup(*Registry, Idx, Group, Sites, bBadIndex);
		SeamSite(Sites, bBadIndex ? TEXT("badidx") : TEXT("final"), Value, Group, Idx, Sex);
		return Value;
	}
}
