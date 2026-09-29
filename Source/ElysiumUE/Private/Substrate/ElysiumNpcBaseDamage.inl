// `CAI_BaseNPC`'s declarations of the `Damage` family (story 5 step 5),
// moved from `ElysiumNpcDamage*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseDamage.cpp`.

/** SEAM for `CBaseEntity::IsStandable` (vtable `+0x290`, slot 164) on ANOTHER entity — the fallback
 *  `CNPC_VMingXiao`'s slot-166 override takes once the candidate is neither a proxy nor a severed
 *  tentacle. `0x100b50a0` reads `GetSolidFlags() & FSOLID_NOT_STANDABLE (0x10)`, then `GetSolid()`
 *  against `SOLID_BSP` / `SOLID_VPHYSICS` / `SOLID_BBOX` (1, 6, 2), then `IsBSPModel()`; this
 *  substrate carries neither the solid TYPE nor the solid FLAGS, so it answers TRUE — the arm an
 *  ordinary solid body takes. It deliberately does not dispatch slot 164, whose Troika body is
 *  another story's generated stub. */
bool CandidateIsStandable(const FElysiumEntity* Candidate) const;

/** Retail's `CTakeDamageInfo`, 0x4c bytes, as `TraceAttack` / `OnTakeDamage_Dead` / `DamageFlinch`
 *  read it. `Dmg` is word 0 — the `CVDmg_t*` this fork bolted onto Valve's packet — and EVERY
 *  reader of this structure tests it for null first, because retail does. */
struct FElysiumTakeDamageInfo
{
	FElysiumDmg* Dmg = nullptr;                 // +0x00  the CVDmg_t*, may be null
	FElysiumEntityHandle Attacker;              // +0x2c  m_hAttacker (`param_1[0xb]` in 0x1032ef60)
	float Damage = 0.f;                         // +0x30  m_flDamage, the scalar fallback
	uint32 DamageBits = 0;                      // +0x38  m_bitsDamageType
	int32 AmmoType = INDEX_NONE;                // +0x40  m_iAmmoType (0x101c2a10's one field)
	// Port-only, not a packet word: the attacker's active weapon record's
	// `Disallow_FirearmsToBashing`, which `CVDmg_t::Apply` inside `0x103302e0` reads off the attacker
	// the packet names (`info+0x2c`). The dispatcher (`DispatchTakeDamagePacket`) fills it.
	bool bDisallowFirearmsToBashing = false;
};

/** SEAM for `thunk_FUN_102699e0` — `SpawnBlood(ptr->endpos, BloodColor(), damage)`, the surface
 *  spray `TraceAttack` emits before it bleeds. `PositionUnits` is SOURCE units. Recorded so the
 *  test can read back that the gate opened and with what amount; the visual is the particle seam's
 *  when a blood root is authored for it. */
struct FSpawnBloodCall
{
	FVector PositionUnits = FVector::ZeroVector;
	int32 BloodColor = 0;
	float Damage = 0.f;
};

// `CBaseEntity` / `CBaseCombatCharacter` / `CBaseAnimating` words 29b did not declare, because
// 29b's shape is the `CAI_BaseNPC` band (`+0x1a40` upward) and these five live below it. Every one
// is read or written by a body of this family and by nothing else in layers 0–9, so they land here
// rather than widening `FElysiumEntity` for one family's sake.
int32 LastHitGroup = 0;       // +0x1594 m_LastHitGroup (datamap, CBaseCombatCharacter)

int32 ForceBone = 0;          // +0x0660 m_nForceBone (datamap, CBaseAnimating)

/** `_DAT_1070ba40`/`44`/`48` — the global death-throw impulse `CAI_BaseNPC::OnTakeDamage_Dead`
 *  writes. It is a FILE-STATIC triple in retail, shared by every body in the level rather than one
 *  per NPC (its seven writers and eight readers are listed by `vtmb_globals 1070ba40`), and it is
 *  carried as such here — a per-NPC copy would be a divergence. SOURCE units, normalized. */
static FVector& DeathThrowImpulse();

/** SEAM for the five per-hitgroup damage-scale cvars `CAI_BaseNPC::TraceAttack` multiplies by:
 *  `DAT_109201ac` (head, hitgroup 1), `DAT_1090fe74` (chest, 2), `DAT_109204f4` (stomach, 3),
 *  `DAT_1090fdc4` (arms, 4 and 5) and `DAT_1092023c` (legs, 6 and 7). SDK 2013 names them
 *  `sk_npc_head` / `_chest` / `_stomach` / `_arm` / `_leg`.
 *
 *  All five POINTER cells live past `.data`'s raw size in the pinned image and **no function in the
 *  corpus constructs them**, so their NAMES and DEFAULTS are **unrecovered** — the same finding
 *  family Bosses recorded for `MingXiaoPedestalCvar` and family Facing for the facing cvars.
 *  Retail's own read is `if (cvar->IsCommand()) 0.0f else cvar->m_flValue`, and an unconstructed
 *  cvar answers `0.0f`; that is what this answers, and the test asserts the refusal rather than a
 *  guessed multiplier. Hitgroups 0 and 8..9 take no scale at all and hitgroup 10 takes the GEAR
 *  constant, so both of those arms are live today. */
float HitGroupDamageScaleCvar(int32 HitGroup) const;

/** SEAM for `CVDmg_t::EvadeCheck` (`0x10012783`), the first gate of `TraceAttack`.
 *  `combat-and-damage.md` § "`CVDmg_t`: the 17-word damage descriptor" records the recovered fact:
 *  **the installed generic `EvadeCheck` callback returns zero** and actual defense lives in the
 *  ranged/melee attack code. So this is not an absence — false IS the answer, and it is cited. */
bool TraceAttackEvadeCheck(const FElysiumDmg* Dmg) const;

TArray<FSpawnBloodCall> SpawnBloodCalls;

void SpawnBlood(const FVector& PositionUnits, int32 BloodColor, float Damage);

/** SEAM for `thunk_FUN_101c2d20(subInfo, this)` — SDK 2013's `AddMultiDamage`, the accumulator
 *  `TraceAttack` hands its modified sub-packet to and which `ApplyMultiDamage` later spends. This
 *  runtime commits damage through `FElysiumCombatCharacter::TakeDamage` / `CommitDamage` instead of
 *  a per-frame multi-damage accumulator, so nothing is spent here: the call is RECORDED, because
 *  what `TraceAttack` puts in the packet (the ammo type from `trace_t+0x50`, `+0x28` re-copied) is
 *  the recovered half and the commit point is another story's. */
TArray<FElysiumTakeDamageInfo> MultiDamageAccumulator;

void AddMultiDamage(const FElysiumTakeDamageInfo& SubInfo);

