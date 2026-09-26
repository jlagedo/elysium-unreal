// Story 29c-1, family **Damage** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcDamage.cpp` and the tests in
// `Tests/ElysiumNpcKernelDamageTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is **damage, death, and the effects the two spawn**: `CAI_BaseNPC`'s `TraceAttack` /
// `TraceBleed` / `OnTakeDamage_Dead` / `IsLightDamage` / `IsHeavyDamage` / `GiveAmmo` /
// `DamageDecal` / `GetAttackExtents`, `CAI_BaseNPCTroika`'s `CanBeSetOnFire` and
// `PlayerAttackerBlockedReaction`, and the per-species death, throw and emitter work of
// `CNPC_VVampireBoss`, `CNPC_VChangBros`, `CNPC_VSabbatLeader`, `CNPC_VAndreiBlood`,
// `CNPC_VSheriffMan`, `CNPC_VBach`, `CNPC_VManBat`, `CNPC_VTzimisce`, `CNPC_VMingXiao`,
// `CNPC_VZombie`, `CNPC_VWerewolf` and `CNPC_VFrenzyShadow`.
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
// `+0x1564 m_flNextAttack` is family **Conditions**' declaration (`ElysiumNpcConditionsBodies.inl`),
// which records that nothing in this runtime writes it yet. `PlayerAttackerBlockedReaction`
// (`0x1029fdb0`) is its first writer and uses that member rather than standing a second.

/** `_DAT_1070ba40`/`44`/`48` — the global death-throw impulse `CAI_BaseNPC::OnTakeDamage_Dead`
 *  writes. It is a FILE-STATIC triple in retail, shared by every body in the level rather than one
 *  per NPC (its seven writers and eight readers are listed by `vtmb_globals 1070ba40`), and it is
 *  carried as such here — a per-NPC copy would be a divergence. SOURCE units, normalized. */
static FVector& DeathThrowImpulse();
static void ResetDeathThrowImpulse();

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

/** The pure spawn-point rule, so the offsets are measurable without a world. `Forward`, `Right` and
 *  `Up` are the muzzle attachment's basis and `OriginUnits` this NPC's origin, both SOURCE units. */
static FVector EnergyBallSpawnPoint(const FVector& OriginUnits, const FVector& FwdAxis,
	const FVector& RightAxis, const FVector& UpAxis);

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

// --- The flinch hook, and the deferred controller line's slot-300 table ----------------------------

/** `FElysiumCombatCharacter::StartDamageFlinch`'s NPC hook: whether this class suppresses the generic
 *  flinch for this descriptor. The Troika line answers false; the two species that do override it. */
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

/** How many times a kill/stop body was ASKED. Retail's emitter words are `EHANDLE`s to entities and
 *  nothing in this substrate creates an emitter ENTITY, so every one of them resolves dead and every
 *  kill body takes its guarded arm — which is retail's own behaviour for a dead handle. The counter
 *  is what lets a test say the body ran and took that arm rather than silently doing nothing. */
int32 EmitterKillCalls = 0;
