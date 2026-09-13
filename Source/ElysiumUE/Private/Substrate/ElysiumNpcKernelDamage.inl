// Story 29c-1, family **Damage** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelDamage.cpp` and the tests in
// `Tests/ElysiumNpcKernelDamageTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is **damage, death, and the effects the two spawn**: `CAI_BaseNPC`'s `TraceAttack` /
// `TraceBleed` / `OnTakeDamage_Dead` / `IsLightDamage` / `IsHeavyDamage` / `GiveAmmo` /
// `DamageDecal` / `GetAttackExtents`, `CAI_BaseNPCTroika`'s `CanBeSetOnFire` and
// `PlayerAttackerBlockedReaction`, and the per-species death, throw and emitter work of
// `CNPC_VVampireBoss`, `CNPC_VChangBros`, `CNPC_VSabbatLeader`, `CNPC_VAndreiBlood`,
// `CNPC_VSheriffMan`, `CNPC_VBach`, `CNPC_VManBat`, `CNPC_VTzimisce`, `CNPC_VMingXiao`,
// `CNPC_VZombie`, `CNPC_VWerewolf`, `CNPC_Bullseye` and `CNPC_VFrenzyShadow`.
//
// FOUR STANDING FACTS OF THIS FAMILY, stated once here rather than at forty call sites.
//
//   * **The hitgroup multiplier is not a damage multiplier.** `CAI_BaseNPC::TraceAttack`
//     (`0x10266780`) computes a local float — the `CVDmg_t::Apply` result scaled by the per-hitgroup
//     cvar — and NEVER writes it back into the damage packet. The scale reaches only three
//     decisions: the `>= 1.0` gate on blood/bleed, the survived-headshot test, and the amount
//     `SpawnBlood` is given. Committed health damage is `CVDmg_t::Apply`'s own, which is why this
//     port routes that one call through `ElysiumDamage::Apply` and multiplies nothing.
//   * **`+0x5df0` is `m_fNoDamageDecal`, not a kill-shot flag.** The ledger
//     (`docs/vtmb/npc-kernel/layout.md`) names it and the port already carries it as
//     `bNoDamageDecal`. `TraceAttack` clears it on entry and raises it on exactly SDK 2013's two
//     arms: a head hit the victim survives, and a hit under `1.0` or carrying `DMG_SHOCK`.
//   * **An emitter, a decal, a tracer, a spawned prop and a ragdoll impulse are MECHANISMS.**
//     Where this runtime has a seam the body goes through it — `IElysiumEmbodiment::SpawnParticleRoot`
//     / `StopParticleRoot` / `KillParticleRoot`, `LayShotImpactDecal`, `TracePlayerSolid`,
//     `IElysiumAudio::PlayBodySound`, `ElysiumRng`, `FElysiumEntityWorld::EnqueueInput`. Where it
//     has none, a seam below answers nothing and names the retail call. The DECISION around each —
//     the threshold, the arm order, what is written — is ported verbatim either way and is what the
//     tests measure.
//   * **Distances and positions below the emitter/impulse seams are SOURCE UNITS.** Retail's
//     constants are Source units and this world is centimetres; `ElysiumMove::U` bridges them at the
//     point of use so the recovered number stays visible in the code.

// --- The two retail packets, as this family's bodies read them ---------------------------------
//
// The generated slot signatures spell `CTakeDamageInfo*` and `trace_t*` as `void*` because neither
// type exists in this runtime. Rather than invent a whole engine packet, these two carry exactly
// the words the recovered bodies touch, each at its retail offset. Every slot body below casts its
// `void*` to one of them and refuses a null.

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

/** Retail's `melee_dice_roll_result*` as `PlayerAttackerBlockedReaction` reads it: the ONE word
 *  `0x10349830` answers, which the body compares against 2. */
struct FElysiumMeleeDiceRollResult
{
	int32 RollType = 0;   // `thunk_FUN_10349830`'s answer; 2 selects the long follow-up delay
};

// --- Species words this family's bodies touch ---------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the species words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These are this family's, each by its retail name and owning class
// (`docs/vtmb/npc-kernel/layout.md`). Where another family already declared a word at one of these
// offsets for a DIFFERENT species it is left alone and a separate member stands here, exactly as
// families Bosses and Motor recorded for `+0x6684` and `+0x668c`.

// `CBaseEntity` / `CBaseCombatCharacter` / `CBaseAnimating` words 29b did not declare, because
// 29b's shape is the `CAI_BaseNPC` band (`+0x1a40` upward) and these five live below it. Every one
// is read or written by a body of this family and by nothing else in layers 0–9, so they land here
// rather than widening `FElysiumEntity` for one family's sake.
int32 LastHitGroup = 0;       // +0x1594 m_LastHitGroup (datamap, CBaseCombatCharacter)
int32 ForceBone = 0;          // +0x0660 m_nForceBone (datamap, CBaseAnimating)
int32 RenderMode = 0;         // +0x016c m_nRenderMode (CBaseEntity); 4 is kRenderTransAlpha
// +0x01fc m_takedamage (datamap, CBaseEntity). 0 = DAMAGE_NO, 1 = DAMAGE_EVENTS_ONLY,
// 2 = DAMAGE_YES. Retail's default for a live NPC is 2, which is what this seeds.
int32 TakeDamageMode = 2;
// `+0x1564 m_flNextAttack` is family **Conditions**' declaration (`ElysiumNpcKernelConditions.inl`),
// which records that nothing in this runtime writes it yet. `PlayerAttackerBlockedReaction`
// (`0x1029fdb0`) is its first writer and uses that member rather than standing a second.

/** `_DAT_1070ba40`/`44`/`48` — the global death-throw impulse `CAI_BaseNPC::OnTakeDamage_Dead`
 *  writes. It is a FILE-STATIC triple in retail, shared by every body in the level rather than one
 *  per NPC (its seven writers and eight readers are listed by `vtmb_globals 1070ba40`), and it is
 *  carried as such here — a per-NPC copy would be a divergence. SOURCE units, normalized. */
static FVector& DeathThrowImpulse();
static void ResetDeathThrowImpulse();

// `CNPC_VVampireBoss`'s gore words — the per-body-region emitter names and the four live emitters.
FString BodyEmitterNames[4];                 // +0x6684 m_pBodyEmitterNames[4] (datamap, string_t)
FElysiumEntityHandle ParticleEmitters[4];    // +0x66a0 m_hParticleEmitters[4] (datamap)

// `CNPC_VChangBros`'s single centre emitter.
FElysiumEntityHandle ChangCenterEmitter;     // +0x66f4 m_hCenterEmitter (datamap)

// `CNPC_VAndreiBlood`'s two emitter handles. Both are WALKED words past the datamap's last row
// (`m_iHitMax` +0x66dc): `0x1035e1a0` owns `+0x66e0` and `0x1035e3c0` owns `+0x66e4`, read off the
// listing (`MOV EAX, dword ptr [ESI + 0x66e0]`). 29b declared neither.
FElysiumEntityHandle AndreiBloodEmitter;     // +0x66e0 (walked)
FElysiumEntityHandle AndreiSummonEmitter;    // +0x66e4 (walked)
bool bAndreiActivated = false;               // +0x66cc m_bActivated (datamap)

// `CNPC_VBach`'s grenade cooldown and the byte its throw clears.
double BachLastGrenadeTime = 0.0;            // +0x6680 m_flLastGrenadeTime, an absolute stamp
bool bBachCamperFlag = false;                // +0x66a0 m_bCamperFlag (datamap)

// `CNPC_VGhoulCroucher`'s authored `on_fire` keyfield, the one word its `CanBeSetOnFire` reads.
bool bGhoulSpawnBurning = false;             // +0x6665 m_bSpawnBurning (datamap, KEY on_fire)

// `CNPC_VZombie`'s gib latch. Retail writes the byte at `&m_bShouldGib + 1`, one past the datamap
// row — `0x103e0430` raises and clears `+0x66e1` and nothing in layers 0–9 reads `+0x66e0` itself.
// Carried as the one observable bit under the datamap's name, with the off-by-one recorded.
bool bZombieShouldGib = false;               // +0x66e1 (`&m_bShouldGib + 1`, walked)

// `CNPC_VMingXiao`'s words this family touches and family Bosses did not declare.
bool bMingXiaoHasTransformed = false;        // +0x6678 m_bHasTransformed (datamap)
double MingXiaoSpitAttackTimer = 0.0;        // +0x66c0 m_flSpitAttackTimer (datamap)
double MingXiaoPickupCooldownA = 0.0;        // +0x66d4, `param_1[0x19b5]` in `0x10396bc0`
double MingXiaoPickupCooldownB = 0.0;        // +0x66d8, `param_1[0x19b6]` in `0x10396bc0`
FVector MingXiaoPickupTargetPos = FVector::ZeroVector;    // +0x6720 m_vecPickupTargetPos, SOURCE
FVector MingXiaoPickupSavedForward = FVector::ZeroVector;  // +0x672c m_vecPickupSavedForward
FElysiumEntityHandle MingXiaoPhysicsAnimlink;              // +0x6738 m_hPhysicsAnimlink (datamap)

// `CNPC_VTzimisce`'s link handle. Family Motor owns `m_hPickupTarget` (+0x6670) and `m_ePathMode`
// (+0x668c); `+0x6684` is this family's, read and cleared by `0x103bf170`.
FElysiumEntityHandle TzimiscePhysicsAnimlink;   // +0x6684 m_hPhysicsAnimlink (datamap)

// `CNPC_VAndreiBlood`'s `SelectIdealState` tag. The port's mind transition trace does not carry
// retail's `{selector, file, line}` triple — the shape map calls `+0x1b38` ABSENT — so the one word
// `0x1035d150` writes is kept here so the arm is measurable.
int32 SelectIdealStateSelector = 0;          // +0x1b38 m_SelectIdealStateTrace.m_iSelector (walked)

// --- The seams this family stands --------------------------------------------------------------

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
TArray<FSpawnBloodCall> SpawnBloodCalls;
void SpawnBlood(const FVector& PositionUnits, int32 BloodColor, float Damage);

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

/** SEAM for `CBaseEntity::Create` / `CBaseEntity::CreateNoSpawn` by CLASSNAME — the three props
 *  this family's bodies spawn: `item_w_grenade_frag` (`0x10365860`), `prop_physics`
 *  (`0x1038f2c0`) and `item_w_chang_energy_ball` (`0x1036dd20`). None is a registered leaf in
 *  `Substrate/ElysiumNpcClasses.cpp` or the item table, so this records the request and answers an
 *  invalid handle — retail's own "Create failed" arm, the one that leaves every field alone. */
struct FCreateEntityCall
{
	FString Classname;
	FVector PositionUnits = FVector::ZeroVector;
};
TArray<FCreateEntityCall> CreateEntityCalls;
FElysiumEntityHandle CreateNamedEntity(const TCHAR* Classname, const FVector& PositionUnits);

/** SEAM for the named particle emitter chain every emitter body in this family runs:
 *  `thunk_FUN_100fbc90(name, origin, angles)` creates it, vtable `+0x3cc` attaches it (mode 1 =
 *  one-shot on the caller, mode 2 = attached at an entity or a named bone), `+0x3c4` starts it and
 *  `+0x3c8` stops it. This runtime HAS the seam — `IElysiumEmbodiment::SpawnParticleRoot` /
 *  `StopParticleRoot` / `KillParticleRoot` — and these go through it; the record beside it is what
 *  makes the DECISION (which name, which mode, which bone, in what order) measurable in a headless
 *  world, where the embodiment answers an invalid effect handle. */
struct FEmitterCall
{
	FString Name;
	FVector PositionUnits = FVector::ZeroVector;
	int32 AttachMode = 0;        // retail's `+0x3cc` first argument: 0 none, 1 one-shot, 2 attached
	FString AttachBone;          // retail's `+0x3cc` third argument when mode 2 names a bone
	FElysiumEntityHandle AttachEntity;
	bool bStarted = false;       // `+0x3c4` ran
	bool bStopped = false;       // `+0x3c8` ran
	// `IElysiumEffectHandle::Id` from `SpawnParticleRoot`, or `INDEX_NONE` headless. Held as a plain
	// int so this `.inl` — included inside the class — does not drag `ElysiumWorldServices.h` into
	// `ElysiumNpc.h`.
	int32 EffectId = INDEX_NONE;
};
TArray<FEmitterCall> EmitterCalls;
/** How many times a kill/stop body was ASKED. Retail's emitter words are `EHANDLE`s to entities and
 *  nothing in this substrate creates an emitter ENTITY, so every one of them resolves dead and every
 *  kill body takes its guarded arm — which is retail's own behaviour for a dead handle. The counter
 *  is what lets a test say the body ran and took that arm rather than silently doing nothing. */
int32 EmitterKillCalls = 0;
/** Create + (optionally) attach + start, in retail's order. Answers the index into `EmitterCalls`,
 *  or `INDEX_NONE` when the name is empty — retail's own "no name configured" refusal. */
int32 CreateNamedEmitter(const FString& Name, const FVector& PositionUnits, int32 AttachMode,
	const FElysiumEntityHandle& AttachEntity, const TCHAR* AttachBone);
/** `+0x3c4`. */
void StartNamedEmitter(int32 EmitterIndex);
/** `+0x3c8` then `thunk_FUN_100fbbb0(entity, 0.1)` — retail's stop-and-fade-out pair. */
void KillNamedEmitter(const FElysiumEntityHandle& Emitter);

/** SEAM for `thunk_FUN_101cd970` / `thunk_FUN_101cd940` (`UTIL_Remove`) on an emitter or a spawned
 *  prop. `FElysiumEntity::Kill()` exists and is used where the handle resolves; the record is kept
 *  so a headless case can see the removal happened. */
TArray<FElysiumEntityHandle> RemovedEntities;
void RemoveNamedEntity(const FElysiumEntityHandle& Entity);

/** SEAM for `CBaseAnimating::GetModelPtr()->numhitboxsets`/`hitboxsetindex` and
 *  `thunk_FUN_10399ef0` — the per-hitbox ray test `CNPC_VMingXiao::TestHitboxes` runs. This runtime
 *  exposes no hitbox table to the kernel; `HitboxSetCount` answers 0, which takes retail's own
 *  "fewer than 7 hitbox sets" refusal, and `TestOneHitbox` answers false. */
int32 HitboxSetCount() const;
bool TestOneHitbox(int32 HitboxSetIndex, const FVector& RayStartUnits, const FVector& RayEndUnits,
	uint32 Mask) const;

/** SEAM for `CBaseEntity::IsStandable` (vtable `+0x290`, slot 164) on ANOTHER entity — the fallback
 *  `CNPC_VMingXiao`'s slot-166 override takes once the candidate is neither a proxy nor a severed
 *  tentacle. `0x100b50a0` reads `GetSolidFlags() & FSOLID_NOT_STANDABLE (0x10)`, then `GetSolid()`
 *  against `SOLID_BSP` / `SOLID_VPHYSICS` / `SOLID_BBOX` (1, 6, 2), then `IsBSPModel()`; this
 *  substrate carries neither the solid TYPE nor the solid FLAGS, so it answers TRUE — the arm an
 *  ordinary solid body takes. It deliberately does not dispatch slot 164, whose Troika body is
 *  another story's generated stub. */
bool CandidateIsStandable(const FElysiumEntity* Candidate) const;

/** SEAM for `CBaseCombatCharacter::GetBlockReactionActivity` (the activity
 *  `PlayerAttackerBlockedReaction` sets before it re-arms the attack timer) and for
 *  `thunk_FUN_10272650` (`SetIdealActivity`). `ElysiumReactions::DefaultBlockedReaction` is the one
 *  recovered name (`combat-and-damage.md` § "Block and stagger reactions"); the activity is
 *  RECORDED rather than played, because the choosing half is story 29e's slot 318 and playing a
 *  pose from the attacker's re-arm would be a second answer to what this body is doing. */
FString LastBlockedReactionActivity;
int32 BlockedReactionActivityCalls = 0;

/** SEAM for `KillSheriff`'s weapon half: `m_fEffects |= 0x20` (`EF_NODRAW`),
 *  `CCollisionProperty::AddSolidFlags(4)` (`FSOLID_NOT_SOLID`) and `CBaseEntity::Relink`, all three
 *  on the ACTIVE WEAPON rather than on this NPC. No `FElysiumEntity` word stands for the effects
 *  mask or the solid flags, and widening the entity base for one body is not this family's edit;
 *  the call is recorded so the test can see the sword went invisible and non-solid rather than
 *  being removed. */
struct FHideAndUnsolidifyCall
{
	FElysiumEntityHandle Entity;
	uint32 EffectBits = 0;    // 0x20, EF_NODRAW
	uint32 SolidBits = 0;     // 0x4, FSOLID_NOT_SOLID
};
TArray<FHideAndUnsolidifyCall> HideAndUnsolidifyCalls;
void HideAndUnsolidifyWeapon(const FElysiumEntityHandle& Weapon);

/** SEAM for `CVDmg_t::Apply`'s AOE sound pick: the victim's own vtable `+0x50c`, whose answer
 *  `CausePlayerAOEDamage` switches its impact sound id on, and `+0x500`, which plays it. Neither
 *  slot has a body in this substrate. `+0x50c` answers 0, which selects the default id `0x79`, and
 *  the chosen id is recorded rather than played — the CHOICE is what is recovered. */
int32 AoeTraceAttackResultCode(const FElysiumEntity* Victim) const;
TArray<int32> AoeImpactSounds;

// --- The bodies ---------------------------------------------------------------------------------

/** `0x102b8c40` — the took-damage arm of `CAI_BaseNPCTroika::SelectSchedule`. 29c named the target
 *  `CacheDamagePosition` and the name is kept so the overlay row matches; the retail body is an
 *  unnamed `AI_BaseNPCTroika.cpp` line-0x5f8b selector arm and it does four things in this order:
 *  refuse (answer 0) unless `COND_LIGHT_DAMAGE` (0x4c) or `COND_HEAVY_DAMAGE` (0x4d) is set; clear
 *  `m_bCondTookDamage` (+0x5b80); copy `m_vecLastDamageAttackPos` (+0x5b9c) into `m_vSavePosition`
 *  (+0x5dd0); stamp the selector trace and answer schedule `0x8a`.
 *
 *  Retail's own `__FILE__`/`__LINE__` pair (`"E:\\Vampire\\main\\dlls\\AI_BaseNPCTroika.cpp"`, line
 *  `0x5f8b`) goes to `+0x1b30`/`+0x1b34`, which the shape map calls ABSENT; the mind transition
 *  trace carries the same account. */
int32 CacheDamagePosition();

/** `0x1035d150` — `CNPC_VAndreiBlood`'s slot-461 `SelectIdealState`. Writes the trace selector tag
 *  4 (the base writes 1, `CNPC_VAnimal` 5, `CNPC_VHengeyokai` 0x13, `CNPC_VHunter` 0x17) and
 *  answers `NPC_STATE_ALERT` (2) when `m_bActivated` is set, else `NPC_STATE_IDLE` (1). Slot 461's
 *  Troika body is story 29e's, so this species arm lands under its retail name. */
EElysiumNpcState CNPC_VAndreiBlood_vfunc461();

/** `0x103c7230` — `CNPC_VVampireBoss::CausePlayerAOEDamage(const Vector& centreUnits, float radius)`.
 *  Only with a live `m_hClosestPlayer` and only when its origin is strictly inside `radius` of the
 *  centre: trace from the centre to the player with `CTraceFilterWorldOnly` and mask 1, build a
 *  `CVDmg_t` through `CVDmg_t::Set(1, 0x40, (int)distanceSquared)` — family LETHAL, `DMG_BLAST`,
 *  and the damage input is the SQUARED distance, which is retail's own arithmetic and not a slip —
 *  dispatch it at the player with `DispatchTraceAttack`, then pick an impact sound off the result
 *  of the player's own vtable `+0x50c`: 0x79 by default, 0x7a on 1, 0x7b on 3, played through its
 *  `+0x500`. */
void CausePlayerAOEDamage(const FVector& CentreUnits, float RadiusUnits);

/** `0x103c6eb0` — `CNPC_VVampireBoss::ClearBodyEmitterNames`: all four `m_pBodyEmitterNames`
 *  entries to the null string, unconditionally. */
void ClearBodyEmitterNames();

/** `0x103c6df0` — `CNPC_VVampireBoss::SetBodyEmitterName(int region, string_t name)`. One store,
 *  no bound check — retail indexes the four-entry array with the caller's word as given. */
void SetBodyEmitterName(int32 Region, const FString& Name);

/** `0x103c7010` — `CNPC_VVampireBoss::SpawnBodyEmitter(int region, CBaseEntity* attachTo)`.
 *  Answers nothing when `attachTo` is null or the region's name is unset; otherwise creates the
 *  named emitter and either one-shots it on ITSELF when the region is 3 (`+0x3cc(this, 1)`) or
 *  attaches it at `attachTo` (`+0x3cc(this, 2, attachTo)`). Note retail never STARTS it here —
 *  `+0x3c4` is not called — unlike every other emitter body in this family. */
int32 SpawnBodyEmitter(int32 Region, const FElysiumEntityHandle& AttachTo);

/** `0x103c7150` — `CNPC_VVampireBoss::KillBodyEmitters`: walk all four `m_hParticleEmitters`, and
 *  for each that still resolves call its stop (`+0x3c8`) then `thunk_FUN_100fbbb0(entity, 0.1)`,
 *  the 0.1 s fade-and-remove. The handles are NOT cleared. */
void KillBodyEmitters();

/** `0x1036e8c0` — `CNPC_VChangBros::KillCenterEmitter`: the same stop-then-0.1 s-fade pair on the
 *  single `m_hCenterEmitter` (+0x66f4), and it too leaves the handle standing. */
void KillCenterEmitter();

/** `0x103ab110` — `CNPC_VSabbatLeader::SpawnBloodPoolEmitter(string_t name, CBaseEntity* orient)`.
 *  Takes this NPC's own origin (slot 217, `+0x364`), replaces its Z either with `orient`'s origin Z
 *  when one is given or with `thunk_FUN_101d08e0(origin, z)`'s answer when it is not — retail's
 *  floor-drop lookup — creates the named emitter there and starts it (`+0x3c4`). The create's
 *  failure arm dereferences a null pointer in retail; this port refuses instead and says so. */
void SpawnBloodPoolEmitter(const FString& Name, const FElysiumEntity* OrientTo);

/** `0x1035e1a0` — `CNPC_VAndreiBlood::StartBloodEmitter(string_t name)` and `0x1035e3c0` —
 *  `StartSummonEmitter`. ONE body written twice: refuse a null name; release the previously cached
 *  handle (`thunk_FUN_101cd940`) and set it to -1 if it still resolves; create the named emitter at
 *  this NPC's origin; store the new handle (or -1 on failure); then attach and start it. The whole
 *  of the difference is the word the handle lives in and the attach: the BLOOD arm parents the
 *  emitter to this entity (`thunk_FUN_100faf60`) and the SUMMON arm attaches it at the bone
 *  `Bip01_R_Hand` (`+0x3cc(this, 2, name)`). Both then call `+0x3c4`. */
void StartBloodEmitter(const FString& Name);
void StartSummonEmitter(const FString& Name);

/** The bone the summon emitter attaches at, `s_Bip01_R_Hand_1053ec20`. */
static const TCHAR* SummonEmitterBoneName();

/** `0x1036dd20` — `CNPC_VChangBros::SpawnEnergyBall`. Builds a spawn point by taking the muzzle
 *  attachment's basis (slot 219, `+0x36c` -> `AngleVectors` `0x10139610`) and pushing this NPC's
 *  origin along it by the retail offset triple `(_DAT_104ada24, _DAT_104ada28, _DAT_104ada2c)` =
 *  `(50, 40, -10)` — forward 50, right 40, up -10 — then creates `item_w_chang_energy_ball` there
 *  and, only when `m_hClosestPlayer` resolves, fires it at that player through the projectile's own
 *  `+0x5d0` with speed `DAT_104ada30` = 800. */
FElysiumEntityHandle SpawnEnergyBall();
/** The pure spawn-point rule, so the offsets are measurable without a world. `Forward`, `Right` and
 *  `Up` are the muzzle attachment's basis and `OriginUnits` this NPC's origin, both SOURCE units. */
static FVector EnergyBallSpawnPoint(const FVector& OriginUnits, const FVector& FwdAxis,
	const FVector& RightAxis, const FVector& UpAxis);

/** `0x103b10f0` — `CNPC_VSheriffMan::KillSheriff`. Two halves, in order. First: find the entities
 *  named `logic_zap_player` and `sheriff`, RTTI-cast the first to `CLogicRelay`, and fire its
 *  `Trigger` input **only when BOTH resolve** — the named `sheriff` is a presence test and nothing
 *  more, it is never used. Second: take this NPC's active weapon and, if it has one, raise
 *  `m_fEffects |= 0x20` (`EF_NODRAW`) and add solid flag `4` (`FSOLID_NOT_SOLID`) to its collision
 *  before relinking it — the sword goes invisible and non-solid rather than being removed. */
void KillSheriff();

/** `0x103c67f0` — `CNPC_VVampireBoss`'s "has it been longer than this since I last attacked".
 *  `curtime - m_flLastAttackTime (+0x5d9c) > Threshold`, strictly. 29c named the target
 *  `FElysiumNpc::LastAttackTime`, which is already the NAME OF THE MEMBER 29b declared for
 *  `+0x5d9c`; the body lands under the elapsed-form name instead and the report says so. */
bool LastAttackTimeElapsed(float ThresholdSeconds) const;

/** `0x103990c0` — `CNPC_VMingXiao`'s throw release, the largest body in this family (1,074 bytes).
 *  In retail's order: remove and clear `m_hPhysicsAnimlink` (+0x6738) if it resolves; then, ONLY
 *  with a live enemy (slot 167), take the held object's centre (`+0x370`), solve a lead point at
 *  the enemy through `thunk_FUN_102c36d0` with the gravity cvar `DAT_1093bbcc`, add the enemy's own
 *  per-frame position delta (`piVar7[0xa0..0xa2] - piVar7[0x9d..0x9f]`) scaled by `_DAT_104454d0`
 *  = 0.5 to the lead's Z, take the yaw of the lead direction and — when `UTIL_AngleDiff` against
 *  this NPC's own yaw leaves the `[-20, +20]` cone — re-aim the XY at exactly `yaw -/+ 20` degrees,
 *  normalize, then scale by a speed that is `1000.0` when
 *  `DAT_1093bc14 + DAT_1093bbcc * distanceSquared` is at or below `_DAT_10447ee0` = 1000 and that
 *  same sum otherwise, with `DAT_1093b9fc * distanceSquared` added to the Z afterwards. It applies
 *  the result through the ragdoll element (`+0x428`) or the physics object (`+0xa0` then `+0x9c`),
 *  and then — unconditionally, on EVERY path including the no-enemy one — clears `m_hThrowObject`
 *  (+0x6718), re-arms the collision ignore at 0.75 s and sets the throwable mode to 0.
 *
 *  The three `DAT_1093…` cvar cells live past `.data`'s raw size and no corpus function constructs
 *  them, so their names and defaults are **unrecovered**; the seam below answers 0.0f, which is an
 *  unconstructed cvar's own answer and which makes the speed take the `<= 1000` arm. */
void LaunchRagdollTowardTarget();
/** SEAM for `DAT_1093bbcc` (the gravity/quadratic term), `DAT_1093bc14` (the constant term) and
 *  `DAT_1093b9fc` (the Z term) of that speed. **Unrecovered**; all answer 0.0f. */
float MingXiaoThrowCvar(int32 Which) const;
/** The pure speed rule, so the two arms are measurable: `Quadratic * DistSq + Constant`, answering
 *  1000.0 when that sum is at or below 1000.0 and the sum itself otherwise. */
static float MingXiaoThrowSpeed(float DistanceSquared, float Quadratic, float Constant);

/** `0x103937d0` — `CNPC_VMingXiao`'s melee/throw swing task. 29c mapped three distinct retail
 *  bodies onto the one target name `MingXiaoThrowAttack`; they are three behaviours and land as
 *  three methods. This one: reset the navigator's path (`thunk_FUN_102e0b40(m_pNavigator)`), resolve
 *  `m_hMeleeWeapon`'s owner (+0xa0), run the pre-attack hook (`+0x610`), and TaskFail `0x1f` when
 *  there is no active weapon. Otherwise ask the weapon for the activity that matches the requested
 *  one (`+0x5a4`), run `+0x5e0`, and choose a melee sequence through slot 331; a refusal or a
 *  negative activity fails the task (`thunk_FUN_10289ee0`) and a success sets the activity
 *  (`+0x4dc`). Either way it then stamps `m_rflAttackTimers[tentacle]` (+0x66c4) with
 *  `curtime + FUN_103983d0(...)` — family Bosses owns both that array and that curve. */
void MingXiaoThrowAttack(int32 TaskId, int32 Tentacle, TFunctionRef<float(int32)> TuningField);

/** `0x10396bc0` — `CNPC_VMingXiao`'s pickup search, `SelectSchedule`'s grab arm. Answers 0 unless
 *  `m_hThrowObject` (+0x6718) is DEAD and both `curtime >= +0x66d4` and `curtime >= +0x66d8`; then
 *  it clears condition 9, draws `RandomInt` against the tuning record's `+8` cell and, only on a
 *  draw below it, runs family Bosses' pedestal search (`0x10398b20`) and stores its answer. With a
 *  live object it starts ignoring that object's collision, sets the throwable mode to 1, stamps the
 *  selector trace with line `0xbc3` and answers schedule `0x167`; otherwise 0. */
int32 MingXiaoFindThrowObject(int32 PedestalCvarDraw, int32 PedestalCvarCeiling);

/** `0x10398fd0` — `CNPC_VMingXiao`'s throw cleanup, which is `0x103990c0`'s tail on its own: remove
 *  and clear `m_hPhysicsAnimlink`, clear `m_hThrowObject`, re-arm the 0.75 s collision ignore and
 *  set the throwable mode to 0. */
void MingXiaoThrowCleanup();

/** `0x103bf170` — `CNPC_VTzimisce`'s pickup release. 29c named the target `VGargoyleGibCleanup` on
 *  the strength of the offset shapes and flagged it unconfirmed; the offsets settle it the other
 *  way — `+0x6670` is `CNPC_VTzimisce::m_hPickupTarget` (family Motor's `PickupTarget`) and
 *  `+0x6684` its `m_hPhysicsAnimlink`, and `thunk_FUN_103be0b0` is the Tzimisce `CARRYING_BODY`
 *  flag write. The name is kept so the overlay row matches. Body, in order: clear the pickup target
 *  to -1; re-arm the collision ignore at 0.75 s; resolve, remove and clear the link handle —
 *  **`UTIL_Remove` is called even when the handle does NOT resolve**, on a null pointer, which is
 *  retail's own unguarded call; then clear the carrying-body flag. */
void VGargoyleGibCleanup();

/** `0x10365860` — `CNPC_VBach::ThrowGrenade(const char* targetName, float force)`. Gated on
 *  `curtime - m_flLastGrenadeTime (+0x6680) >= 5.0` (`_DAT_10454110`). Then: find the named entity
 *  and, only if it exists, stamp `m_flLastGrenadeTime` with curtime, create an
 *  `item_w_grenade_frag` at that entity's origin, set `m_takedamage = 2`, `m_iHealth = 1` and clear
 *  its touch function, initialise its physics if it has none (a failure `Msg`es
 *  `"No physics data for grenade"` and removes it), take the target's FORWARD vector, apply
 *  `forward * force` as a velocity with zero angular velocity, arm both its own `+0x7c` word and
 *  `m_flNextThink` at `curtime + 3.0 (+0.01)` and clear `m_bCamperFlag` (+0x66a0). */
void ThrowGrenade(const FString& GrenadeTargetName, float Force);

/** `0x1038f2c0` — `CNPC_VManBat::ThrowModel(const char* model, const char* parentName)`, named by
 *  its own `DevMsg` literal `"ManBat is throwing model %s"`. Creates a `prop_physics` at this NPC's
 *  origin without spawning it, sets its model, spawns it, looks a bone up by the SAME string, makes
 *  a corpse-shaped ragdoll from it (`thunk_FUN_10157da0`), removes the template prop, then arms the
 *  ragdoll's think at `curtime + 20.0` (`_DAT_1044eb0c`), optionally parents/owns it to `parentName`
 *  and finally attaches it through family Bosses' ManBat animlink arm before storing its handle in
 *  `m_hPickupTarget` (+0x668c, Bosses' `ManBatPickupTarget`). */
bool ThrowModel(const FString& ModelName, const FString& ThrowParentName);

/** `0x10397dd0` — `CNPC_VMingXiao`: reset `m_flSpitAttackTimer` (+0x66c0) to 0, **only when both
 *  parameters are non-null**. Retail's two parameters are never read for anything else, so the
 *  presence test is the whole of the condition and is reproduced as a pair of bools. */
void SpitAttackTimer(bool bFirstParamSet, bool bSecondParamSet);

/** `0x10398d90` — `m_eThrowableObjectMode = value` (+0x673c, family Bosses' member). Thirteen
 *  bytes; three of this family's bodies and three of Bosses' call it. */
void ThrowableObjectMode(int32 Mode);

/** `0x10397000` — `CNPC_VMingXiao`'s slot-166 `CanStandOn(CBaseEntity*)` override. 29c named the
 *  target `FElysiumNpc::SeveredTentacles`, which is ALREADY family Squad's member array for
 *  `m_rhSeveredTentacles` (+0x66a8); the body lands under the fuller name and the report says so.
 *  Walks indices 0..5 of BOTH `m_rhProxies`
 *  (+0x668c) and `m_rhSeveredTentacles` (+0x66a8) — family Squad's two arrays, read through their
 *  owner — testing each RESOLVED entity pointer against the candidate, and answers FALSE on the
 *  first match. On a full miss a non-null candidate is asked its own `IsStandable` (slot 164) and a
 *  false there answers false; a NULL candidate skips that test and answers TRUE. Slot 166's
 *  Troika-line body is family Motor's `CanStandOn`, which this does not call. */
bool SeveredTentaclesCanStandOn(const FElysiumEntity* Candidate) const;

/** `0x10399fe0` — `CNPC_VMingXiao`'s slot-100 `TestHitboxes` override. Refuses without a model,
 *  without `m_bHasTransformed` (+0x6678) and with fewer than 7 hitbox sets; then tests hitbox set 0
 *  and, on a miss, sets 1..6 — each gated by family Bosses' `IsTentacleConnected(index)`
 *  (`0x10398000`), whose index runs 0..6 across seven iterations while the set index advances by
 *  `0xc` from `0xc` to `0x48`. Slot 100's Troika body is generated, so this species arm lands under
 *  its own name and the report says so. */
bool TestHitboxesMingXiao(const FVector& RayStartUnits, const FVector& RayEndUnits, uint32 Mask);

/** `0x10376ae0`, `0x10376b10`, `0x10376b50` and `0x103a4950` — the `CNPC_VFrenzyShadow` /
 *  `CNPC_VPlayerController` line's four damage-and-death slot arms. Each fills a slot whose
 *  Troika-line body is a LATER story's and is already generated, so they land as named species arms
 *  and the report says so.
 *
 *  All four route through `+0x184`, the controller's stored sub-object: `OnTakeDamage` (slot 142)
 *  forwards the packet to its slot 0x238 and always answers 0; `OnTakeDamage_Alive` (slot 390)
 *  forwards through that object's own `+0x9c` to slot 0x618 and always answers 0; `Event_Killed`
 *  (slot 144) is an EMPTY body — death handling fully suppressed; and `Event_TookLife` (slot 300,
 *  shared with `CNPC_VWolfMorph`) resolves the object's `+0xa8` AI component, builds the killed
 *  entity's debug name and dispatches AI event type 4 at priority -1.0 tagged with the retail
 *  literal `"CNPC_VPlayerController::Event_TookLife"`.
 *
 *  `+0x184` is a `CBasePlayer*` slot no substrate word stands for; the seam below answers null,
 *  which takes the guarded arm of all four. */
int32 OnTakeDamageSpecies(const FElysiumTakeDamageInfo* Info);
int32 OnTakeDamage_AliveSpecies(const FElysiumTakeDamageInfo* Info);
void Event_KilledSpecies(const FElysiumTakeDamageInfo* Info);
void Event_TookLifeSpecies(const FElysiumEntity* Victim);

/** SEAM for `this->vtable+0x184` — `CNPC_VPlayerController`'s stored controller object, and for the
 *  AI-event dispatch `thunk_FUN_1017e150(component, type, priority, source)` `Event_TookLife` ends
 *  on. No word of this substrate stands for either; the first answers null and the second records
 *  the call. */
bool HasPlayerControllerObject() const;
struct FControllerAiEvent
{
	int32 EventType = 0;
	float Priority = 0.f;
	FString Source;
	FString VictimName;
};
TArray<FControllerAiEvent> ControllerAiEvents;

/** The retail literal `Event_TookLife` tags its dispatch with, `s_CNPC_VPlayerController__Event_To_1064bcc0`. */
static const TCHAR* TookLifeEventSource();

// --- The species tables -------------------------------------------------------------------------

/** One row of slot 141's (`TraceAttack`) species table: the census class, the retail body that
 *  fills the slot for it, and what that body does BEFORE it falls into the Troika line. Every arm
 *  in the table delegates to `0x10266780` in the end — the three species bodies are prologues. */
enum class ETraceAttackPrologue : uint8
{
	None,          // the Troika line's own body, `0x10266780`
	BullseyeGate,  // `0x10356f60`: spawnflag 0x40000 restricts the hit to the closest owner, and
	               // spawnflag 0x80000 with `m_takedamage == 0` runs slot 146 first
	ZeroAmmoType,  // `0x103ccbf0`: `SetAmmoType(info, 0)`
	ZombieGib      // `0x103e0430`: pick the gib latch and force a cvar-driven ammo type
};
struct FTraceAttackSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	ETraceAttackPrologue Prologue = ETraceAttackPrologue::None;
};
static const FTraceAttackSpecies* TraceAttackSpeciesRows(int32& OutCount);
static const FTraceAttackSpecies* TraceAttackSpeciesOf(const TCHAR* InRetailClass);
/** This NPC's row, walking the census chain, or null for the Troika line. */
const FTraceAttackSpecies* TraceAttackSpecies() const;

/** `0x103e0430` — `CNPC_VZombie`'s prologue as a pure rule, so both arms are measurable. The gib
 *  latch is raised when the hitgroup is 1 (a head hit) and cleared otherwise; a non-head hit only
 *  forces an ammo type when the attacker's active weapon's capability mask intersects `0x18000`
 *  (the melee-block capability), and the type forced is the SECOND cvar for a head hit and the
 *  FIRST for a qualifying melee one. Answers whether an ammo type is forced. */
static bool ZombieTraceAttackPrologue(int32 HitGroup, bool bAttackerWeaponIsMelee,
	int32 FirstCvarAmmoType, int32 SecondCvarAmmoType, bool& OutShouldGib, int32& OutAmmoType);

/** SEAM for `DAT_10940404` and `DAT_1094044c` — the two cvar-backed ammo-type cells
 *  `CNPC_VZombie::TraceAttack` reads. Both pointer cells live past `.data`'s raw size and no corpus
 *  function constructs them: **unrecovered**, and both answer 0, which is an unconstructed cvar's
 *  own answer. */
int32 ZombieGibAmmoTypeCvar(int32 Which) const;

/** One row of slot 615's (`CanBeSetOnFire`) species table. `CNPC_VGhoulCroucher` is the only class
 *  in the family tree that replaces the Troika body. */
struct FCanBeSetOnFireSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	// True where the species refuses outright while its `m_bSpawnBurning` keyfield is set, ahead of
	// the Troika body's condition and timer test.
	bool bRefusesWhileSpawnBurning = false;
};
static const FCanBeSetOnFireSpecies* CanBeSetOnFireSpeciesRows(int32& OutCount);
static const FCanBeSetOnFireSpecies* CanBeSetOnFireSpeciesOf(const TCHAR* InRetailClass);

/** One row of slot 292's (`DamageFlinch`) species table — the two classes that gate the generic
 *  flinch. `CNPC_VGargoyle`'s `0x10378cb0` and `CNPC_VHengeyokai`'s `0x103802a0` are BYTE-IDENTICAL:
 *  suppress the flinch entirely when `(dmg->m_bdmgTypes | info.m_bitsDamageType)` intersects
 *  `0x4000002` — `DMG_BULLET | DMG_BUCKSHOT`, which is exactly `ElysiumDamage::FirearmMask` — and
 *  skip a hit whose magnitude is exactly zero; otherwise call the base body. */
struct FDamageFlinchSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	uint32 SuppressMask = 0;
};
static const FDamageFlinchSpecies* DamageFlinchSpeciesRows(int32& OutCount);
static const FDamageFlinchSpecies* DamageFlinchSpeciesOf(const TCHAR* InRetailClass);

/** The gate itself, as the two species bodies spell it, so it can be measured without a world.
 *  `Magnitude` is `CVDmg_t::GetDmg()` when the packet carries a descriptor and `m_flDamage` when it
 *  does not — retail's own two-armed read. */
static bool DamageFlinchSuppressed(uint32 CombinedDamageBits, float Magnitude, uint32 SuppressMask);

/** `FElysiumCombatCharacter::StartDamageFlinch`'s NPC hook: whether the species that stands this
 *  classname suppresses the generic flinch for this descriptor. The base character has no species,
 *  so its override answers false and every other body flinches exactly as before. */
virtual bool SuppressesDamageFlinch(const FElysiumDmg& Dmg) const override;

/** One row of slot 300's (`Event_TookLife`) species table — the three classes of the
 *  `CNPC_VPlayerController` line that share `0x103a4950`. */
struct FTookLifeSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
};
static const FTookLifeSpecies* TookLifeSpeciesRows(int32& OutCount);
static const FTookLifeSpecies* TookLifeSpeciesOf(const TCHAR* InRetailClass);
