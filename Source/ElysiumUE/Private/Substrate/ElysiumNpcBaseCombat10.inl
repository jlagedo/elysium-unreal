// `CAI_BaseNPC`'s declarations of the `Combat10` family (story 5 step 5),
// moved from `ElysiumNpcCombat10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseCombat10.cpp`.

/** SEAM for the ammo reroll slot 386 runs for an `FL_NPC` body: two entries (`i = 0` and `1`, byte
 *  stride `0x1093c` up to `0x21278`) written into `weapon+0x74c+4*i`. Per entry, the weapon's slot
 *  `+0x454` decides the source — true takes `RandomInt(1, wpndata+0x2674+i*0x1093c)` through
 *  `0x102517b0`, false takes the weapon's slot `+0x450` as a cap and writes `RandomInt(1, cap)`, or
 *  a flat `0` when the cap is below `1`.
 *
 *  This runtime stands neither the two-entry ammo array nor the weapon-data table, so the rolled
 *  pair is RECORDED against the weapon with the cap it came from, and the recovered rule — the two
 *  entries, the `< 1` flat zero, the `RandomInt(1, cap)` — is what the test drives. */
struct FWeaponAmmoReroll
{
	FElysiumEntityHandle Weapon;
	int32 Entry[2] = { 0, 0 };
	int32 Cap[2] = { 0, 0 };
	bool bFromWeaponData[2] = { false, false };
};

TArray<FWeaponAmmoReroll> WeaponAmmoRerolls;
