#pragma once

#include "CoreMinimal.h"

class FElysiumEntity;
struct IElysiumRetailSiteSink;
namespace ElysiumSoundFolder { struct FOwner; }

// The VSound group seam `FUN_101f55a0` (`0x101f55a0`, 643 B, `__thiscall(reg, ent, group, female)`,
// `RET 0xc`; `docs/specs/layers/L0-entity/walks/L0-r007.md`): the group index of an entity's
// `soundgroup` inside one category table of a SndScheme registry. Called by `CBaseEntity::
// PrecacheSoundTable` `0x1009d460` (twice, flags 0 and 1), by the eight slot-71 override bodies with
// their own registries, and by `CNPC_VWerewolf::Precache`, `CNPC_VZombie::SetModel` and
// `CNPC_VGhoulCroucher::SetModel` with the Char registry. `hooks.tsv:110-111` list it as an L0 caller
// of two L2 record fetches (`FUN_102517b0` the item record, `FUN_101d5f10` the char template);
// `audit.tsv:2598` files it under L2 character. The port keeps it here, in the entity substrate, with
// those two reads and the registry itself as the L2 hooks it stands on.

// The SndScheme table object `reg` the seam is called on: `reg+0x1c` the category count and
// `reg+0x20` the per-category table array (each a `CVSoundFileFolder_t` owner `T`,
// `Audio/ElysiumSoundFolderIndex.h`). `FUN_101f66c0` `0x101f66c0` builds five: `DAT_1073dc28`
// `SndScheme_Char` (`Female` 0, `Male` 1, `Monster` 2, `Animal` 3 -- the base `PrecacheSoundTable`'s,
// and the one `+0xbc` indexes), `DAT_1073dbd4` `SndScheme_Wpn`, `DAT_1074b308` `SndScheme_Openable`,
// `DAT_1073dc00` `SndScheme_Switch`, `DAT_1074b330` `SndScheme_Computer`. The builder chain
// (`FUN_101f5210` / `FUN_101f5390` the vocabulary file, `FUN_101f41b0` the directory, `FUN_101f3810`
// the walk) is L2's (`audit.tsv` rows 2973-2979, `rpg`); this layer declares the object it reads and
// never fills it. **HOOK (L0 -> L2, a data hook like `FOwner::FlatIndex`):** the live game answers
// through `FElysiumEntityWorld::VSoundCharRegistry`, null until L2 builds `SndScheme_Char`, which the
// seam reads as `reg+0x20 == NULL` (S0). The arena's `vsound_registry` fixture supplies one.
struct IElysiumVSoundRegistry
{
	virtual ~IElysiumVSoundRegistry() = default;
	// `reg+0x20 != NULL`.
	virtual bool HasTables() const = 0;
	// `reg+0x1c`.
	virtual int32 TableCount() const = 0;
	// `reg+0x20[idx]`: the category table, or null for an index the array does not hold (retail reads
	// past the array there; the seam states what the port does instead).
	virtual ElysiumSoundFolder::FOwner* Table(int32 Index) = 0;
};

// The seam's fourth argument (`char female`): 0 the normal group, 1 the `Female_PC_Override` form.
enum class EElysiumVSoundSex : uint8
{
	Normal = 0,
	Female = 1,
};

namespace ElysiumVSoundGroup
{
	// `FUN_101f55a0`, every arm in retail order (the VAs are the walk's):
	//   S0      `reg+0x20 == NULL`                           -> 0 (`XOR EAX,EAX` 0x101f55b6; not -1)
	//           `idx = GetVSoundTableIdx(ent)` (0x101f55cb; may re-enter slot 71), then `ent+0x9c`:
	//   S2      `m_pCombatCharacter != 0`: `RTDynamicCast(ent, CBaseTerminal 0x105606d4)` (0x101f55ef)
	//     S2a   cast non-null: female + non-empty group -> lookup `"%s\Female_PC_Override"`; female +
	//           empty -> lookup `"Female_PC_Override"`; else lookup `group`. Unreachable by a terminal
	//           (a terminal is no combat character), so every combat character takes
	//     S2b   empty group -> `FUN_101d5f10(&DAT_10738d10, cc)[+0x20]` (the template's "SoundGroup",
	//           NULL as ""); female -> `"Female_PC_Override"` for an empty group, else the `%s\` form;
	//           then S-final.
	//   S1      `m_pCombatCharacter == 0`:
	//     S1a   `m_pCombatWeapon != 0`: `w = FUN_102517b0(weapon)`; `w+0x2554 != -2` -> (empty group ->
	//           `w+0x24d4`, the record's `sound_group`), `w+0x2554 = lookup(group)`; return `w+0x2554`.
	//           Every loaded record holds -2 there (`FUN_10258b00`), so the arm returns -2 unlooked.
	//     S1b   `m_pCombatWeapon == 0` and `ent+0xa4 == 0` (`+0xa4` has no recovered writer):
	//       i   `RTDynamicCast(ent, CPropSwitch 0x105a6404)` non-null -> lookup `group` (0x101f5764)
	//       ii  classname (`+0x11c`, "" for NULL) is `func_button` or `func_rot_button` -> S-final
	//       iii else `RTDynamicCast(ent, CBaseTerminal)`: NULL -> -1 (`OR EAX,-1` 0x101f57f6); else
	//           lookup `group` (0x101f57e1)
	//           `ent+0xa4 != 0` -> S-final.
	//   final   `FUN_101f42a0(tbl[idx], group)` (0x101f5811): the folder's key, or the category root's.
	// Sites: `l0.voice.seam fn=Global::FUN_101f55a0 va=0x101f55a0 phase=return arm=<...> value=<n>`.
	int32 ResolveGroupIndex(IElysiumVSoundRegistry* Registry, FElysiumEntity& Entity, const TCHAR* Group,
		EElysiumVSoundSex Sex, IElysiumRetailSiteSink* Sites);
}
