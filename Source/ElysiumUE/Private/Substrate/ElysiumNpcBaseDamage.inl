// `CAI_BaseNPC`'s declarations of the `Damage` family (story 5 step 5),
// moved from `ElysiumNpcDamage*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseDamage.cpp`.

// +0x01fc m_takedamage (datamap, CBaseEntity). 0 = DAMAGE_NO, 1 = DAMAGE_EVENTS_ONLY,
// 2 = DAMAGE_YES. Retail's default for a live NPC is 2, which is what this seeds.
int32 TakeDamageMode = 2;

// `CNPC_VAndreiBlood`'s `SelectIdealState` tag. The port's mind transition trace does not carry
// retail's `{selector, file, line}` triple — the shape map calls `+0x1b38` ABSENT — so the one word
// `0x1035d150` writes is kept here so the arm is measurable.
int32 SelectIdealStateSelector = 0;          // +0x1b38 m_SelectIdealStateTrace.m_iSelector (walked)

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
	FElysiumEntityHandle Attacker;              // +0x28  the attacking entity
	float Damage = 0.f;                         // +0x30  m_flDamage, the scalar fallback
	uint32 DamageBits = 0;                      // +0x38  m_bitsDamageType
	int32 AmmoType = INDEX_NONE;                // +0x40  m_iAmmoType (0x101c2a10's one field)
};

/** Retail's `trace_t` as the same bodies read it. `+0x0c` is `endpos`, `+0x44` the hitgroup,
 *  `+0x48` the physics bone (a `short`, sign-extended by `0x102667e7`) and `+0x50` the ammo type
 *  `TraceAttack`'s tail copies into the sub-packet through `0x101c2a10`. */
struct FElysiumTraceHit
{
	FVector EndPosUnits = FVector::ZeroVector;  // +0x0c  SOURCE units
	FVector PlaneNormal = FVector::ZeroVector;  // the surface normal the decal seam needs
	int32 HitGroup = 0;                         // +0x44
	int32 PhysicsBone = 0;                      // +0x48
	int32 AmmoType = INDEX_NONE;                // +0x50
};

/** What `TraceBleed` decided on each pass that got past its three refusals: the noise half-width and
 *  the trace count its damage band selected, and the segment of the last trace it ran. Retail's
 *  effect is a blood decal on a wall, which the decal seam cannot answer for headless; the TABLE is
 *  the recovered concern and this is what makes it measurable. */
struct FTraceBleedPass
{
	float Noise = 0.f;
	int32 TraceCount = 0;
	FVector LastStartUnits = FVector::ZeroVector;
	FVector LastEndUnits = FVector::ZeroVector;
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

int32 RenderMode = 0;         // +0x016c m_nRenderMode (CBaseEntity); 4 is kRenderTransAlpha

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

TArray<FTraceBleedPass> TraceBleedPasses;

/** SEAM for `thunk_FUN_101c2d20(subInfo, this)` — SDK 2013's `AddMultiDamage`, the accumulator
 *  `TraceAttack` hands its modified sub-packet to and which `ApplyMultiDamage` later spends. This
 *  runtime commits damage through `FElysiumCombatCharacter::TakeDamage` / `CommitDamage` instead of
 *  a per-frame multi-damage accumulator, so nothing is spent here: the call is RECORDED, because
 *  what `TraceAttack` puts in the packet (the ammo type from `trace_t+0x50`, `+0x28` re-copied) is
 *  the recovered half and the commit point is another story's. */
TArray<FElysiumTakeDamageInfo> MultiDamageAccumulator;

void AddMultiDamage(const FElysiumTakeDamageInfo& SubInfo);

/** SEAM for `thunk_FUN_10427620` — `GetAmmoDef()->MaxCarry(index)`, the capacity `GiveAmmo` clamps
 *  against, and for the index-to-name join the clamp needs. This runtime's `FElysiumInventory`
 *  keys its reserve by the authored ammo TYPE NAME (`AmmoReserve`, a `TMap<FString,int32>`), not by
 *  retail's 0..31 `CAmmoDef` index, and no table joins the two. Both answer nothing —
 *  `MaxCarry` `0` and the name empty — which closes `GiveAmmo`'s clamp at zero and makes it return
 *  0 without sounding. **Unrecovered:** the `CAmmoDef` index order. */
int32 AmmoMaxCarry(int32 AmmoIndex) const;

FString AmmoTypeNameForIndex(int32 AmmoIndex) const;

/** SEAM for `(*g_pGameRules)->vtable+0xd4` — the "is ammo enabled" predicate `GiveAmmo` asks before
 *  anything else. No rules object here carries it; answers TRUE, the permissive arm, so the clamp
 *  below it is what actually decides. */
bool GameRulesAllowsAmmo(int32 AmmoIndex) const;
