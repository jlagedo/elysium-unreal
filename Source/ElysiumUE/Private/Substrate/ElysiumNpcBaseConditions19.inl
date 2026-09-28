// Story 0019/8 (29e under the strict verdict), family **Conditions19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseConditions19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Conditions19's `rule` rows): 0x10270b20 CAI_BaseNPC::GatherEnemyConditions, 0x1026ec30
// CAI_BaseNPC::GatherConditions. Walked prose:
// `docs/vtmb/npc-ai/conditions-and-states.md` § "Story 8, family Conditions19".

// --- The condition identities `EElysiumNpcCond` does not spell -------------------------------------
//
// Every one is the `CAI_BaseNPC` registrar's own number (`FUN_102c8ce0`, the dump in
// `docs/vtmb/npc-ai/conditions-and-states.md` § "The base condition table"), so it is the same
// ordinal in the class-local space every species inherits. The family's bodies push them by number,
// as retail does; `ElysiumNpcConditions.h` is hot and read-only for this lane, so they stand here as
// named constants and are listed as seams in the lane report.
static constexpr EElysiumNpcCond Cond19OutsideInterruptDist = static_cast<EElysiumNpcCond>(0x14);
static constexpr EElysiumNpcCond Cond19InsideInterruptDist = static_cast<EElysiumNpcCond>(0x15);
static constexpr EElysiumNpcCond Cond19OutsideInterruptDistE = static_cast<EElysiumNpcCond>(0x16);
static constexpr EElysiumNpcCond Cond19InsideInterruptDistE = static_cast<EElysiumNpcCond>(0x17);
static constexpr EElysiumNpcCond Cond19OutsideInterruptDistF = static_cast<EElysiumNpcCond>(0x18);
static constexpr EElysiumNpcCond Cond19InsideInterruptDistF = static_cast<EElysiumNpcCond>(0x19);
static constexpr EElysiumNpcCond Cond19HaveEnemyThrowLos = static_cast<EElysiumNpcCond>(0x1b);
static constexpr EElysiumNpcCond Cond19ClawHintInvalid = static_cast<EElysiumNpcCond>(0x1c);
static constexpr EElysiumNpcCond Cond19ClawHintSpecialInvalid = static_cast<EElysiumNpcCond>(0x1d);
static constexpr EElysiumNpcCond Cond19TargetOccluded = static_cast<EElysiumNpcCond>(0x49);
static constexpr EElysiumNpcCond Cond19HaveTargetLos = static_cast<EElysiumNpcCond>(0x4b);
static constexpr EElysiumNpcCond Cond19EnemyFacingMe = static_cast<EElysiumNpcCond>(0x56);
static constexpr EElysiumNpcCond Cond19BehindEnemy = static_cast<EElysiumNpcCond>(0x57);
static constexpr EElysiumNpcCond Cond19BetterWeaponAvailable = static_cast<EElysiumNpcCond>(0x67);
static constexpr EElysiumNpcCond Cond19SquadLosEnemy = static_cast<EElysiumNpcCond>(0x32);
static constexpr EElysiumNpcCond Cond19SeeCorpse = static_cast<EElysiumNpcCond>(0x3d);

/** `0x102cc6c0(&m_DelayedConditionList +0x1a9c, cond, delay)`: with fewer than eight entries, an
 *  entry already carrying `Cond` takes `min(stamp, curtime + delay)` (`0x102cc590`, strict `<`);
 *  otherwise `{Cond, curtime + delay}` is appended (`0x102cc560`). A full list drops the push. */
void Conditions19PushDelayedCondition(int32 Cond, float Delay, double Now);
static constexpr int32 Cond19DelayedListCapacity = 8;               // `CMP EAX,0x8 / JGE`

// --- The species-local identities the Conditions19 species bodies push -----------------------------
//
// Class-local ordinals (each class registrar numbers its own conditions from 0x77/0x78 up, so the same
// number means different things on different classes); the names are the registrars' own strings.
static constexpr EElysiumNpcCond Cond19CanPounce = static_cast<EElysiumNpcCond>(0x23);                     // base CAN_POUNCE
static constexpr EElysiumNpcCond Cond19AndreiTimeToTeleport = static_cast<EElysiumNpcCond>(0x79);          // registrar 0x1035c490
static constexpr EElysiumNpcCond Cond19ChangTimeToJumpAttack = static_cast<EElysiumNpcCond>(0x79);         // registrar 0x1036a420
static constexpr EElysiumNpcCond Cond19ChangTimeToTeleport = static_cast<EElysiumNpcCond>(0x7a);
static constexpr EElysiumNpcCond Cond19ChangTimeToUnitedAttack = static_cast<EElysiumNpcCond>(0x7c);
// `CNPC_VDog` (registrar 0x10373700): the enum already carries these numbers under State19's
// placeholder names; the retail names are PLAYER_TOOCLOSE 0x78, PLAYER_MOVEDAWAY 0x7c,
// PLAYER_BEFRIENDED 0x7d, PLAYER_ATTACKED 0x7e.
static constexpr EElysiumNpcCond Cond19DogPlayerTooClose = EElysiumNpcCond::DogCombatLatch;
static constexpr EElysiumNpcCond Cond19DogPlayerMovedAway = EElysiumNpcCond::DogIdleFromAlert;
static constexpr EElysiumNpcCond Cond19DogPlayerBefriended = EElysiumNpcCond::DogIdleFromAlert2;
static constexpr EElysiumNpcCond Cond19DogPlayerAttacked = EElysiumNpcCond::DogCombatLatch2;
static constexpr EElysiumNpcCond Cond19CroucherUnaware = static_cast<EElysiumNpcCond>(0x79);             // registrar 0x1037a980
static constexpr int32 Cond19MingXiaoCanAttackFirst = 0x77;                                               // registrar 0x103913c0: 0x77..0x7c CAN_ATTACK_*
static constexpr EElysiumNpcCond Cond19MingXiaoCanAttackSpit = static_cast<EElysiumNpcCond>(0x7d);
static constexpr EElysiumNpcCond Cond19MingXiaoMeleeHelpless = static_cast<EElysiumNpcCond>(0x7e);
static constexpr EElysiumNpcCond Cond19TentacleFlee = static_cast<EElysiumNpcCond>(0x77);                 // registrar 0x1039b260
static constexpr EElysiumNpcCond Cond19TentaclePhaseExpired = static_cast<EElysiumNpcCond>(0x79);
static constexpr EElysiumNpcCond Cond19SabbatTimeToJump = static_cast<EElysiumNpcCond>(0x79);             // registrar 0x103a5e80
static constexpr EElysiumNpcCond Cond19ScurryingPlayerTooClose = static_cast<EElysiumNpcCond>(0x78);      // registrar 0x103abd70
static constexpr EElysiumNpcCond Cond19TzimisceShouldDropBody = static_cast<EElysiumNpcCond>(0x77);       // registrar 0x103b7120
static constexpr EElysiumNpcCond Cond19TzimisceForceThrowBody = static_cast<EElysiumNpcCond>(0x78);
static constexpr EElysiumNpcCond Cond19WerewolfCanSpecialMove = static_cast<EElysiumNpcCond>(0x78);       // registrar 0x103c8f00
static constexpr EElysiumNpcCond Cond19WerewolfEnemyReachable = static_cast<EElysiumNpcCond>(0x79);

// --- `CAI_BaseNPC::GatherConditions` (`0x1026ec30`)'s callees ---------------------------------------

/** The world's `curtime`, or 0 with no world (a fixture-free unit). */
double Conditions19Now() const;

/** `0x102cc760(&m_DelayedConditionList, this)` (`+0x1a9c`, `Cognition.DelayedConditions`): every
 *  entry whose stamp is at or before `curtime` is promoted by `SetCondition` and removed by
 *  `0x102cc730`, which moves the LAST entry into the vacated slot; the walk then re-examines that
 *  slot. An entry stamped after `curtime` (or NaN) waits. No-op on an empty list. */
void Conditions19FlushDelayedConditions(double Now);

/** `CAI_BaseNPC::PerformSensing` (`0x1026e4f0`): `m_iIsOblivious < 1` gates
 *  `CAI_Senses::PerformSensing` (`0x10310710`: Look, whose tail is slot 469 `OnLooked`, then Listen,
 *  whose tail is slot 470 `OnListened`), and slot 459 `RemoveIgnoredConditions` runs on every path.
 *  The senses runner is the Troika's (`Senses.PerformSensing`, which takes `FElysiumNpc&`); a base-only NPC
 *  (`CAI_TestHull`, the script directors) runs slot 459 alone. */
void Conditions19PerformSensing(double Now);

/** SEAM for `UTIL_FindClientInPVS(edict())` (`0x101d1800`). The port has no PVS query; the live
 *  player stands in for "a client in my PVS", which is the admitting answer and the same stand-in
 *  `ElysiumScriptedSequence.cpp`'s `!pvsplayer` uses. */
bool Conditions19ClientInPvs() const;

/** `0x1026fb40`, the better-weapon search (`Weapon_IsBetterAvailable` by shape): slot 513
 *  capability bit `0x200000`, `m_flNextWeaponSearchTime (+0x5da0) < curtime` re-armed to
 *  `curtime + 2.0` (`_DAT_10452dc4`), no active weapon, then `Weapon_FindUsable((300, 300, 100))`.
 *  True raises `BETTER_WEAPON_AVAILABLE 0x67` in the caller. */
bool Conditions19BetterWeaponAvailable(double Now);

/** SEAM for `CBaseCombatCharacter::Weapon_FindUsable(const Vector& range)`: the nearest pick-up-able
 *  weapon inside the box. No port weapon search stands on the kernel; answers null ("none usable"),
 *  so `0x1026fb40` answers false after its re-arm. Counted. */
FElysiumEntity* Conditions19WeaponFindUsable(const FVector& RangeUnits);
int32 Conditions19WeaponFindUsableCalls = 0;

/** The tunables `0x1026fb40` reads. */
static constexpr int32 Cond19CapWeaponSearch = 0x200000;              // slot 513 bit, `TEST EAX,0x200000`
static constexpr double Cond19WeaponSearchInterval = 2.0;             // `_DAT_10452dc4`
static constexpr uint32 Cond19SpawnflagSenseAlways = 0x400;           // `m_spawnflags >> 10 & 1`

// --- `CAI_BaseNPC::GatherEnemyConditions` (`0x10270b20`)'s callees --------------------------------

/** `0x10270890` — the enemy distance slot 481 hands to slot 561: 3-D between the two slot-217 origins
 *  with the vertical term replaced by the GAP between the two world-space surrounding boxes
 *  (`m_Collision +0x270` vfunc `0x3c`): `E.mins.z - M.maxs.z` when the enemy is wholly above,
 *  `E.maxs.z - M.mins.z` when wholly below, else 0. SOURCE units. */
float Conditions19EnemyDistanceUnits(const FElysiumEntity& Enemy) const;

/** `CAI_Enemies::LastTimeSeen` (`0x102e0150`) on `GetEnemies()`: the matching record's time, else the
 *  last position-only record's (`+0x34`), else 0.0 with the DevWarning; a null enemy answers 0.0. */
double Conditions19LastTimeSeen(const FElysiumEntity* Enemy) const;

/** `CAI_Enemies::GetLastKnownPosition` (`0x102dfed0`): the matching record's position, else the last
 *  position-only record's with the "(using danger pos)" DevWarning, else `vec3_origin` with the plain
 *  one. Centimetres. The no-match notifier calls (`vfunc 0xe8` / `0x10316bc0`) are unrecovered. */
FVector Conditions19LastKnownPosition(const FElysiumEntity* Enemy) const;

/** SEAM for `CAI_BaseNPC::UpdateEnemyPos` (`0x10271900`, checklist-19-29 `mechanism`: the
 *  GOALTYPE_ENEMY moving-goal upkeep behind the 0018 nav seam). Records the call, moves nothing. */
void Conditions19UpdateEnemyPos();
int32 Conditions19UpdateEnemyPosCalls = 0;

/** SEAM for `m_pNavigator +0x34` (`CAI_Navigator::m_bNotOnNetwork`). The kernel's navigator keeps no
 *  such byte; answers false, the cleared state, so slot 530 `IsUnreachable` is asked. */
bool Conditions19NavNotOnNetwork() const;

/** The family's zero-extent `TraceRay`s with `CTraceFilterSimple(this, 0)`: the eluded tail's
 *  (`0x102713c9..0x102714fe`, mask `0x2804091`), the Troika wall ray (`102b2e2e..102b2fba`, mask
 *  `0x2000b`), Hengeyokai's (`0x10382020`, `0x600400b`) and Tzimisce's (`0x103be630`, `0x400b`)
 *  throw lines. True when the ray reaches (`tr.fraction == 1.0`, no start/all-solid). The port's one
 *  world ray is the embodiment's `QueryLineOfSight` (world geometry, characters not occluders): the
 *  masks' `CONTENTS_MONSTER`/debris bits are the named divergence the landed slot-201 body already
 *  takes. No embodiment answers "reaches", retail's clear result. Centimetres. */
bool Conditions19RayReaches(const FVector& FromCm, const FVector& ToCm) const;

/** `m_afMemory (+0x5d8c)` bit `0x20000`: "the enemy has been in sight" -- set on every pass below the
 *  occlusion limit and cleared on every pass at it; its edges fire the four found/lost outputs. The
 *  bit's SDK name is unrecovered. */
static constexpr uint32 Cond19MemoryEnemyInSight = 0x20000u;
static constexpr int32 Cond19EnemyOccludedLimit = 10;               // `CMP EAX,0xa` at `0x10270ba3` / `0x10270bbd`
static constexpr int32 Cond19EnemyVisibleMask = 0x2804091;          // `PUSH 0x2804091` at `0x10270b86`
static constexpr float Cond19EnemyLeadMin = -0.05f;                 // `PUSH 0xbd4ccccd` at `0x10270fb4`
static constexpr double Cond19ElusionSeconds = 8.0;                 // `_DAT_1045597c`
static constexpr float Cond19ElusionRadiusUnits = 48.0f;            // `_DAT_10447ee8`
