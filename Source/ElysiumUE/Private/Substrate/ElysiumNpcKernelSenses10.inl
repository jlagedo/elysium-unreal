// Story 29d, family **Senses10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. What lands here is
// the non-slot half — the base-class bodies beneath a Troika override, the species arms the slot
// dispatchers run, the retail helpers those bodies call, and the seams that stand for retail inputs
// this substrate has no source for.
//
// The definitions are in `Substrate/ElysiumNpcKernelSenses10.cpp` (the Troika line) and
// `Substrate/ElysiumNpcKernelSenses10_2.cpp` (the species line); the tests are
// `Tests/ElysiumNpcKernelSenses10Tests.cpp`. The walked prose is `docs/vtmb/npc-ai/senses.md`
// § "Story 29d, family Senses10 — …".
//
// --- What this family is --------------------------------------------------------------------------
//
// **What the NPC perceives and who its enemy is.** Twenty-four `Senses10` rows and ten
// `SpeciesSenses10` rows: slot 201 `FVisible` and the slot 594 range/concealment test under it, slot
// 467 `QueryHearSound`, slot 468 `QuerySeeEntity`, slot 469 `OnLooked` with the `CAI_BaseNPC` base
// body beneath it, slot 472 `OnSeeEntity`, slot 478 `BestEnemy`, slot 544 `UpdateEnemyMemory`, the
// weapon-LOS pair (562/573), the aim pair (538/574), slots 223/402/445, and the species arms over
// `FVisible`, `OnSeeEntity`, `BestEnemy`, `GetShootEnemyDir`, `FInViewCone` and
// `FValidateHintType`, plus the Werewolf's hint-validity and stuck bodies and the Scurrying pair.
//
// THREE STANDING FACTS OF THIS FAMILY, stated once here rather than at thirty call sites.
//
//   * **`+0x9c` is the entity's own `CBaseCombatCharacter` self-downcast cache, so `cand->+0x9c` is
//     `cand` for every combat character and null for everything else.** Every `+0x9c` test in this
//     family therefore reads "is this a combat character", and every pointer compare against a
//     `+0x9c` (`CNPC_VZombie::FVisible`, `CNPC_VFrenzyShadow::BestEnemy`) is a compare against the
//     entity itself. This is what makes `FrenzyShadowBestEnemy` answer an entity rather than a
//     subobject, and it is why the port can use `FElysiumEntity::AsCombatCharacter()` for both.
//   * **`+0x6081` is `m_bSeenInOuterBand` and its ONE writer is slot 594.** The shape map
//     (`ElysiumNpcKernelShapeMap.cpp`) binds it to `FElysiumNpcMemory::bPlayerInOuterBand`. Slot 594
//     clears it on entry and sets it when the target is beyond `_DAT_10457f54` (**0.7**) of the
//     effective vision radius; slot 472 `OnSeeEntity` and both `OnSeeEntity` species arms read it.
//     The port's `ElysiumNpcSense::OuterBandFraction` IS that constant.
//   * **Retail's distance keys in this family are `__ftol` of the SUM OF SQUARES, not of a root.**
//     `0x10431320` is plain `__ftol` (39 bytes, no callees) and both `BestEnemy` bodies hand it the
//     unrooted sum. The checklist's walk of `CNPC_VFrenzyShadow::BestEnemy` says "`__ftol` of the
//     squared-distance root"; the listing (`1037697d`) shows no `fsqrt`. Corrected here.

// --- Slot 404 / 405: the two dispositions this family reads --------------------------------------
//
// SEAM, and the one place this family departs from a literal vtable dispatch. Slot 404
// `IRelationType` (`0x10299da0`) is family **Conditions10**'s row of this same story and is still
// the generated stub answering `0` (`D_ER`); dispatching it would tell every body below that every
// entity is an error relation, which is neither retail's answer nor this runtime's. Both helpers
// therefore read the same store the Troika body's tail reaches through
// `CBaseCombatCharacter::IRelationType` — `FElysiumRelationships` — and map it onto retail's
// `Disposition_t` ids. The day slot 404 carries its body these become one-line forwards to it.
//
// Retail's ids: `D_ER 0`, `D_HT 1`, `D_FR 2`, `D_LI 3`, `D_NU 4`. `FElysiumRelationships::Resolve`
// never answers `D_ER`, so retail's `default:` arms (the `OnLooked` `DevWarning`, `QuerySeeEntity`'s
// refusal) are unreachable through this helper and say so at each site.
int32 Disposition(const FElysiumEntity* Candidate) const;
int32 DispositionPriority(const FElysiumEntity* Candidate) const;

// --- Slot 201 `FVisible`: the blocker out-parameter ----------------------------------------------

/** SEAM. Retail's slot 201 is `bool FVisible(CBaseEntity*, int mask, CBaseEntity** ppBlocker, int)`
 *  and its refusal arms write `*ppBlocker = 0` ASYMMETRICALLY — the `npc_ignore_senses` and
 *  `npc_ignore_player` arms write it, the null-target arm does NOT, and slot 594 writes it on its
 *  range refusal and its concealment refusal but not on the far-band arm. The generated signature
 *  spells the third parameter `FElysiumEntity*` (a value, not a cell), so the write cannot be
 *  delivered to the caller; it is COUNTED here instead, with the target it was made for, so
 *  retail's asymmetry is observable rather than silently dropped. Nothing in this runtime passes a
 *  blocker yet: the Troika body itself passes `0` to slot 594, which is retail. */
int32 FVisibleBlockerWrites = 0;
FElysiumEntityHandle LastFVisibleBlockerTarget;
void WriteFVisibleBlocker(const FElysiumEntity* Target);

/** `CBaseEntity::FVisible`, the trace slot 201 ends at once every gate has passed: an eye-to-eye
 *  segment with retail's caller-supplied mask. The mask is this runtime's channel question and the
 *  embodiment answers a plain "is the segment clear", so the mask is carried for the record. */
bool BaseEntityFVisible(const FElysiumEntity& Target, int32 Mask) const;

// --- Slot 469 `OnLooked`: the `CAI_BaseNPC` base body beneath the Troika override ----------------

/** `CAI_BaseNPC::OnLooked` (`0x1026a2c0`), 624 bytes — a DISTINCT retail function beside the Troika
 *  override `0x102b39a0` that owns slot 469, so it takes its own name and is not a slot body.
 *
 *  The body itself is `ElysiumNpcCond::GatherSight` (`ElysiumNpcConditions.cpp`), which is where the
 *  port has carried it since story 10b and where story 29d added the two gates it was missing (the
 *  `relation != D_NU` gate and the `SEE_ENEMY` raise, both cited at the line that does them). This
 *  is the NAMED ENTRY POINT retail's slot 469 calls first — it runs that body against this NPC's
 *  own condition set on the substrate clock, which is what `CAI_Senses::Look` does. */
void BaseOnLooked();

// --- Slot 472 `OnSeeEntity`: the two species arms and their class statics -------------------------

/** `CNPC_VCop::OnSeeEntity` (`0x10371ae0`) and `CNPC_VHunter::OnSeeEntity` (`0x103887d0`), the two
 *  54-byte twins. Each stamps its OWN class-static suspect pair and then runs the Troika body
 *  (`0x102b3e00`) unconditionally. The cop's stamp (`0x10370560`) is guarded on the seen entity
 *  carrying a player record at `+0xa8`; the hunter's (`0x10387fd0`) is NOT — recorded because it is
 *  the only difference between them. */
void CopOnSeeEntity(FElysiumEntity* Seen);
void HunterOnSeeEntity(FElysiumEntity* Seen);

/** The four class-static cells the two arms write and three other bodies read: `DAT_1093ac3c` /
 *  `_DAT_1093aca8` (the cop's shared provoker handle and its expiry) and `DAT_1093b650` /
 *  `_DAT_1093b658` (the hunter's). They are STATIC IN RETAIL — every cop in the map shares one
 *  grudge — so they are file statics here too, reached through these accessors rather than copied
 *  per NPC. Family **Debug10**'s `CopSuspectIs` and family **Conditions10**'s two slot-404 species
 *  arms are the readers; this family is the writer.
 *
 *  The window is `_DAT_104492a8` = **30.0** seconds (`docs/vtmb/npc-ai/`). */
static constexpr float SpeciesSuspectWindowSeconds = 30.f;   // _DAT_104492a8
static FElysiumEntityHandle CopSuspectHandle();
static double CopSuspectExpiry();
static FElysiumEntityHandle HunterSuspectHandle();
static double HunterSuspectExpiry();
/** `0x10370560` and `0x10387fd0` themselves — the two stamps, so a test can drive them directly.
 *  Both are reset by `ResetSpeciesSuspectGlobals`, which exists because a process-lifetime static
 *  outlives a headless world and retail's own lifetime is the process too. */
void StampCopSuspect(FElysiumEntity* Seen);
void StampHunterSuspect(FElysiumEntity* Seen);
static void ResetSpeciesSuspectGlobals();

// --- Slot 478 `BestEnemy`: the arbitration words and the one species arm -------------------------

/** One candidate of `CAI_BaseNPC::BestEnemy` (`0x102743c0`), scored once so the comparison below is
 *  the recovered rule and nothing else. The four incumbent words are retail's four stack slots:
 *  `[ESP+0x11]` seeded `1`, `[ESP+0x14]` seeded `0x10000000`, `[ESP+0x18]` seeded `-1000` and
 *  `[ESP+0x10]` seeded `0`. */
struct FBestEnemyState
{
	FElysiumEntity* Best = nullptr;
	int32 Distance = 0x10000000;    // __ftol of the SUM OF SQUARES, Source units squared
	int32 Priority = -1000;
	bool bUnreachable = true;
	bool bVisible = false;
};

/** The visibility term both `BestEnemy` bodies compute: `CAI_Senses::DidSeeEntity`
 *  (`0x1030fb10`, this Look pass's accepted set) OR slot 201 `FVisible(cand, 0x2804091, 0, 0)`. */
bool BestEnemyCandidateVisible(FElysiumEntity* Candidate);

/** `__ftol` of the SUM OF SQUARES between two slot-217 origins, in SOURCE units squared — the
 *  distance key both `BestEnemy` bodies compare. `0x10431320` is plain `__ftol`; there is no root. */
int32 BestEnemyDistanceKey(const FElysiumEntity& Candidate) const;

/** `CNPC_VFrenzyShadow::BestEnemy` (`0x103766d0`), slot 478's one species arm: a sticky arm in front
 *  and then a SCORE-based rescan, where the base body is a lexicographic key walk. */
FElysiumEntity* FrenzyShadowBestEnemy();

/** `CNPC_VFrenzyShadow::m_iHostileEnemyCount` (`+0x6664`) and `m_bFailedGrapple` (`+0x6668`), the
 *  two words its slot-478 body owns. No port system writes either yet; they are declared here
 *  because this body IS their retail writer. */
int32 FrenzyShadowHostileEnemyCount = 0;   // +0x6664
bool bFrenzyShadowFailedGrapple = false;   // +0x6668

// --- Slot 574 `GetShootEnemyDir` and the aim point behind it -------------------------------------

/** `0x10278650` — the aim POINT slot 574 subtracts the caller's shoot position from. Not a row of
 *  this family and not a slot; carried here because slot 574 and its Ming Xiao arm are both defined
 *  by it. Three arms, in retail's order:
 *
 *    1. `m_hShootTargetOverride` (`+0x5ba8`) live → that entity's `GetAbsOrigin` (slot 217), whole.
 *    2. no enemy → the body's own forward, from slot 372's angles through `AngleVectors`
 *       (`0x10139610`) — **SEAM**: this runtime's `+0x374` accessor is the entity's angles and the
 *       vector build is family Geometry's, so this answers the NPC's own eye position, which is
 *       where retail's degenerate arm lands for a body with no enemy.
 *    3. an enemy → the enemy-memory LKP (`0x102dfed0`) plus `BodyTarget(shootPos)` minus the
 *       enemy's `GetAbsOrigin`, with `+_DAT_104994e0` added to Z when the enemy's stat `0x0b` reads
 *       `5`. **SEAM**: `_DAT_104994e0` is UNRECOVERED (an `.rdata` cell with no reader that pins
 *       it) and the `CVStatList_t` join by retail list TYPE does not exist on this sheet, so the
 *       stat reads not-5 and the bonus is not applied. Named rather than guessed. */
FVector ShootEnemyAimPoint(const FVector& ShootPositionCm);

/** `CNPC_VMingXiao::GetShootEnemyDir` (`0x10395d00`), slot 574's one species arm: the base body with
 *  `_DAT_1044eb0c` (**20.0** Source units) added to the aim point's Z before the subtraction. */
FVector MingXiaoGetShootEnemyDir(const FVector& ShootPositionCm, int32 A, int32 B);

// --- Slot 562 `WeaponLOSCondition`: the player-in-line-of-fire test ------------------------------

/** `0x10266b10` — the cone test slot 562 applies AFTER the weapon has answered, and which overrides
 *  a weapon that said yes. For each client: `dot(normalize(target - owner), normalize(center -
 *  owner))` STRICTLY above `0.92` AND the distance to the target STRICTLY greater than the distance
 *  to that client. True means a player stands between this body and what it is aiming at.
 *
 *  `0x10137220` (`1057966c`) is `VectorNormalize`, which answers the LENGTH — that is where both
 *  distance terms come from, and it is why they are unsquared. A zero-length delta normalises to
 *  the zero vector here rather than faulting, which is a CRASH GUARD and not a rule: retail divides
 *  by the length unguarded. */
bool PlayerInLineOfFire(const FVector& OwnerPosCm, const FVector& TargetPosCm) const;

// --- Slot 445 `StartTaskOverlay`: the move-and-shoot overlay's two words -------------------------

/** `CAI_MoveAndShootOverlay+0x18`, the re-arm stamp slot 445 writes. `0x102e8250` disables the
 *  overlay by storing `FLT_MAX` (`0x7f7fffff`) into it; `0x102e8270` re-arms it to
 *  `curtime + overlay+0x2c` after re-deriving the shot counts from the active weapon.
 *
 *  **SEAM**: this runtime stands no `CAI_MoveAndShootOverlay`. The stamp, the two pause bounds and
 *  the disable/arm decision ARE what slot 445 decides, so they are recorded; the overlay's own
 *  weapon-data re-derivation (`+0x3a4`/`+0x3a8`) is the overlay's story. `0x102e8270`'s own fallback
 *  — state 4, no weapon, or neither the `0x11` nor the `0x15` activity sequence — lands on the same
 *  disable, and this runtime has no activity-sequence table, so the fallback is named and the arm
 *  is taken. */
struct FMoveAndShootOverlay
{
	// +0x18. `FLT_MAX` is retail's own "disabled" value, and it is the shipping default.
	float NextShotTime = MAX_flt;
	float PauseMin = 0.f;    // +0x24, from m_flBurstShootPauseMin (+0x5bbc)
	float PauseMax = 0.f;    // +0x28, from m_flBurstShootPauseMax (+0x5bc0)
	int32 Disables = 0;      // how many times 0x102e8250 ran
	int32 Arms = 0;          // how many times 0x102e8270 ran
};
FMoveAndShootOverlay MoveAndShootOverlay;
void DisableMoveAndShootOverlay();                       // 0x102e8250
void ArmMoveAndShootOverlay(float PauseMin, float PauseMax);  // 0x102e8270

/** `m_flBurstShootPauseMin` (`+0x5bbc`) and `m_flBurstShootPauseMax` (`+0x5bc0`), the pair slot 445
 *  hands `0x102e8270`. Declared here because this body is their only reader in the kernel closure
 *  and no port producer writes them yet. */
float BurstShootPauseMin = 0.f;
float BurstShootPauseMax = 0.f;

// --- Slot 223 `CreateVPhysics`: the shadow the callee builds -------------------------------------

/** SEAM for `0x10272f40`, the shadow-physics builder slot 223 guards. Retail: refuse when slot 94
 *  answers `7`, destroy any existing object, `VPhysicsInitShadow(true, false, NULL)`, set the mass
 *  from the model's own `mass` keyvalue or — at or below `_DAT_104454c4` (**0.0**) — `90` for a male
 *  body and `65` for a female one, and set the damping from the summed hull extents times
 *  `_DAT_10449270` (**0.5**) squared. This runtime stands no physics object (`m_pPhysicsObject`
 *  `+0x36c` has no port word), so the builder RECORDS the mass it chose and creates nothing; the
 *  slot's own answer is `true` either way, which is retail's. */
struct FVPhysicsShadowBuild
{
	bool bBuilt = false;
	float MassKg = 0.f;
	float Damping = 0.f;
};
FVPhysicsShadowBuild VPhysicsShadow;
bool bHasPhysicsObject = false;   // +0x36c m_pPhysicsObject, as a "is one standing" answer
void BuildVPhysicsShadow();       // 0x10272f40

/** SEAM for the model's authored `mass` keyvalue (`0x10272f40` reads it off the studio header).
 *  No studio header is parsed on this substrate; answers `0.0`, which is retail's at-or-below-zero
 *  arm and therefore takes the gender default rather than refusing. */
float ModelMassKeyvalue() const;
/** The gender half of that default: `90` male, `65` female. `FElysiumNpc` has no recovered gender
 *  word yet, so this answers the MALE default and names the retail read. */
bool IsFemaleBody() const;

// --- Slot 402 `Event_Gibbed` ---------------------------------------------------------------------

/** SEAM for `CBaseCombatCharacter::CreateSecondaryDiscParticles(m_vDiscBloodType)` — the explosive
 *  gib's particle burst, keyed on `m_vDiscBloodType` (`+0xfd0`). Visual only and no particle system
 *  reaches this substrate; counted so the arm is observable. */
int32 SecondaryDiscParticleBursts = 0;
void CreateSecondaryDiscParticles();

/** SEAM for `UTIL_Remove(this)` (`0x101cd940`), slot 402's no-explosive-gibs arm. `FElysiumEntity`
 *  has `Remove`-shaped lifetime elsewhere in this substrate; this counts the call and names it so
 *  the arm that removes the body is distinguishable from the arm that fades it. */
int32 UtilRemoveCalls = 0;
void UtilRemoveSelf();

// --- Slot 544 `UpdateEnemyMemory`: the squad gate and the eluded gate ----------------------------

/** `m_iSquadDisconnected` (`+0x5bb0`) and the squad word (`+0x5da4`) that slot 544's first gate
 *  reads off BOTH this NPC and the candidate. **SEAM**: this substrate stands no squad object
 *  (`ConnectedSquad()` answers null), so the squad word answers `0` — retail's own "no squad" value,
 *  which makes the gate's third term false and lets every candidate through. That is the admitting
 *  arm, so nothing is silently refused. `m_iSquadDisconnected` is a real per-NPC word with no
 *  port producer and ships at `0`, which is retail's connected state. */
int32 SquadDisconnected = 0;    // +0x5bb0
uint32 SquadWord() const;       // +0x5da4

/** SEAM for `CAI_Memory::UpdateMemory` (`0x102df700`), the call slot 544 forwards to with the node
 *  array at `m_pNavigator+0x2c`. This runtime's store is `FElysiumNpcEnemyMemory`; the node array is
 *  the AI network, which does not exist here, and the two node ids the record carries stay
 *  `INDEX_NONE`. Answers true when the target gained its FIRST record, which is retail's answer. */
bool UpdateCaiMemory(FElysiumEntity* Enemy, const FVector& PositionCm);

// --- Slot 594 `Slot594`: the range and concealment test under `FVisible` -------------------------
//
// The slot itself is generated; what it needs and this runtime has no word for is below.

/** `m_flSeekDistInspection` (`+0x63b8`) — the effective vision distance slot 594 multiplies the
 *  target's own slot-28 scalar by. It IS `FElysiumNpcSenses::Perception.VisionDistanceCm`; named
 *  here so the body reads as retail's and the unit conversion happens in one place. */
float SeekDistInspectionCm() const;

/** The target's slot 28 (`vtable +0x70`) — its own stealth VISION scalar, the twin of slot 30's
 *  hearing reduction that `AdjustSoundDistForStealth` reads. `1.0` for a target carrying no stealth
 *  surface, which is every character except the player. */
float TargetStealthVisionScalar(const FElysiumEntity& Target) const;

/** SEAM for `m_bIsBCCTargetable` (`+0x1480`), the byte `BestEnemy` reads off a candidate's `+0x9c`
 *  combat character and both `BestEnemy` bodies gate on. `FElysiumCombatCharacter` carries no such
 *  word and no body in the kernel closure clears it, so this answers **true** — the ADMITTING arm,
 *  which is retail's own answer for an untouched character. Named so the gate is in the tree. */
static bool IsBccTargetable(const FElysiumEntity& Candidate);

/** SEAM for `FL_NOTARGET` — `CBaseEntity::GetFlags()` bit `0x8000`, which `BestEnemy` reads as the
 *  SIGN of `(flags >> 8)` (`10274449` `TEST AH,AH / JS`). `FElysiumEntity::Flags` carries no
 *  `FL_NOTARGET` bit and no producer sets one, so this answers false — the admitting arm. */
static bool HasNoTargetFlag(const FElysiumEntity& Candidate);

/** SEAM for the `0x46004003` segment trace slot 573 runs, whose HIT ENTITY is what its three arms
 *  branch on. `IElysiumEmbodiment::QueryLineOfSight` answers a bool and names no blocker, and the
 *  kernel hull trace (family Motor's `KernelHullTrace`) answers no hit either, so this reports a
 *  CLEAR trace with no blocker — which is retail's `fraction == 1.0` arm and slot 573's `true`. */
bool InnateWeaponLosTrace(const FVector& StartCm, const FVector& EndCm,
	FElysiumEntity*& OutBlocker) const;

// --- Slot 467 `QueryHearSound` -------------------------------------------------------------------

/** `CStealthKillRules::InDeafZone(&DAT_1072c540, player, this)` — slot 467's sound-type-4 arm.
 *  Forwards to the rulebook's own deaf-zone rule; false with no rulebook, which is retail's answer
 *  for a sound whose owner carries no player record. */
bool SoundOwnerInDeafZone(const FElysiumEntity* Owner) const;

// --- `CAI_BaseNPC`'s head probe ------------------------------------------------------------------

/** `0x1026ab50` — the shrunk-hull head probe `RunAI` (`0x1026f110`) runs between `GatherConditions`
 *  and `PrescheduleThink`. It is UNPORTED as a behaviour and this is why: its whole body is gated on
 *  `+0x5f2d` (the shrunk-hull latch) AND `+0x5f2c`, and neither word has a port producer — the hull
 *  swap they record is `CAI_Navigator`'s, which this substrate does not stand. The recovered rule is
 *  written out in full at the definition and the two latches are declared below so the day the
 *  navigator lands the probe is one function that changes rather than one that is invented.
 *
 *  With `bHullShrunk` false — which is every NPC today — the body does nothing, which is retail's
 *  own answer for a body whose hull was never shrunk. */
bool bHullShrunk = false;        // +0x5f2d
bool bHullShrinkArmed = false;   // +0x5f2c
void HeadProbe();

/** SEAM for `0x10273070`, the probe's clean-trace tail: restore the normal hull from the navigator,
 *  clear `+0x5f2d`, and re-run `0x10272f40` when `+0x36c` (the physics object) stands. The hull
 *  restore is the navigator's; the two observable halves — the latch clear and the shadow rebuild —
 *  are performed. */
void RestoreNormalHull();

/** SEAM for `CAI_Navigator`'s hull bounds (`0x102d6100` mins / `0x102d6120` maxs, and the SMALL
 *  hull's `0x102d6140` / `0x102d6160`). No navigator stands here, so both answer the entity's own
 *  collision bounds and say so. SOURCE units, as every retail hull word is. */
FVector HullMinsUnits(bool bSmall) const;
FVector HullMaxsUnits(bool bSmall) const;

// =================================================================================================
// SpeciesSenses10 — the ten species-line rows
// =================================================================================================

// --- `CNPC_VCameraSecurity` (slots 201 and 363) --------------------------------------------------

/** `CNPC_VCameraSecurity::FVisible` (`0x10369ff0`) and `::FInViewCone` (`0x10369fb0`). Both REPLACE
 *  the body they override outright: a security-camera NPC never consults its own eyes. `FVisible`
 *  answers true only for a candidate carrying a player record when the link resolves and the
 *  camera's full sight test (`0x1020cd00`) passes; `FInViewCone` answers true only for the same
 *  candidate shape when the camera's CONE test alone (`0x1020cc60`) passes. A missing link is false
 *  for both. */
bool CameraSecurityFVisible(FElysiumEntity* Candidate);
bool CameraSecurityFInViewCone(FElysiumEntity* Candidate);

/** SEAM for `CSecCamera::CanSee` (`0x1020cd00`) and `CSecCamera::InViewCone` (`0x1020cc60`), the two
 *  tests the camera itself performs — the enabled byte `+0x7d8`, the 2-D distance against the far
 *  radius `+0x794` and the near radius `+0x790` (`0x1020cd30`), the cone dot against `+0x798`, and a
 *  `0x4091` trace whose fraction must equal `_DAT_10449280` (**1.0**). `FElysiumNpc::ResolveSecCameraLink`
 *  (family Dialogue, `0x10369e70`) is the link; the camera entity carries none of those five words
 *  on this substrate, so both answer **false** — which is retail's answer for a camera that is
 *  switched off, and the arm that leaves a security NPC blind rather than omniscient. */
bool SecCameraCanSee(const FElysiumEntity* Camera, const FElysiumEntity* Target) const;
bool SecCameraInViewCone(const FElysiumEntity* Camera, const FElysiumEntity* Target) const;

// --- `CNPC_VTzimisce` and `CNPC_VZombie` (slot 201) ----------------------------------------------

/** `CNPC_VTzimisce::FVisible` (`0x103ba290`), 30 bytes: the Troika base with the FOURTH argument
 *  FORCED to `0`, whatever the caller supplied. That is the whole override, and it is observable. */
bool TzimisceFVisible(FElysiumEntity* Candidate, int32 Mask, FElysiumEntity* Blocker, int32 Arg4);

/** `CNPC_VZombie::FVisible` (`0x103e0bc0`): ONE arm in front of the Troika base — a candidate that
 *  IS this zombie's current enemy is answered by the obfuscate test `0x10146a80` (discipline stat 8
 *  at or above 1 AND the entity's `+0x14dc` cloak byte) rather than by sight — and the same
 *  fourth-argument clamp for everything else. */
bool ZombieFVisible(FElysiumEntity* Candidate, int32 Mask, FElysiumEntity* Blocker, int32 Arg4);

// --- `CNPC_VBach` (slot 566) ---------------------------------------------------------------------
//
// `CNPC_VBach::FValidateHintType` (`0x10365800`) lands as a ROW of family Hints' slot-566 species
// table (`EHintTypeRule::InRangeOrBase`), not as a method here — see `ElysiumNpcKernelHints.inl`.

// --- `CNPC_VScurrying` ---------------------------------------------------------------------------

/** `CNPC_VScurrying::m_flDetectionDistance` (`+0x6690`), `m_fIgnoreNosferatu` (`+0x6694`) and
 *  `m_fMustDetect` (`+0x6695`) — shape rows with no port producer until now. They are this family's
 *  words because `ScurryingShouldDetect` is their only reader. SOURCE units on the distance, as the
 *  authored keyvalue is. */
float ScurryingDetectionDistanceUnits = 0.f;   // +0x6690
bool bScurryingIgnoreNosferatu = false;        // +0x6694
bool bScurryingMustDetect = false;             // +0x6695

/** `0x103acac0` — the Scurrying detection test. Four arms in retail's order; see the definition. */
bool ScurryingShouldDetect(const FElysiumEntity* Target) const;

/** `0x103ad0f0` — "is this target's character template `Player_Nosferatu`". The port's sheet carries
 *  the clan on the player, so this is the whole recovered rule for a player target and answers false
 *  for everything else, which is retail's. */
static bool IsNosferatuTemplate(const FElysiumEntity& Target);

/** `0x103ad0a0` — the must-detect gate: true for any target that is NOT a player, and for a player
 *  only while `COND_SEE_PLAYER` (`0x5a`) or COND `0x6f` stands. */
bool ScurryingMustDetectAdmits(const FElysiumEntity& Target) const;

/** `0x103acba0` — the Scurrying flee-destination search. Two halves: a node jitter when the
 *  navigator finds a node within 30000 units, and a hull-traced march away from the threat when it
 *  does not. **SEAM**: no node graph stands here, so the node search always fails and the MARCH is
 *  the arm taken — which is retail's own answer for a map with no AI network, and it is the arm
 *  that still produces a destination. `OutCm` is written only on success, exactly as retail writes
 *  its out-vector only when one was passed. */
bool ScurryingFindFleeDestination(const FVector& ThreatPosCm, float DistanceUnits,
	FVector* OutDestinationCm);

/** SEAM for `0x102edae0` — "the nearest navigator node to `pos` within `radius`". No AI network
 *  stands here; answers false, which is the failure arm above. */
bool NearestNavigatorNode(const FVector& PositionCm, float RadiusUnits, FVector& OutNodeCm) const;

/** SEAM for `CAI_BaseNPCTroika::IsAreaClear(pos, mask, 0, 0)` — the jitter arm's acceptance test.
 *  This runtime has no hull sweep; answers true, which admits the jittered point, and the march arm
 *  below is the one a test can drive end to end. */
bool IsAreaClear(const FVector& PositionCm, int32 Mask) const;

// --- `CNPC_VWerewolf` ----------------------------------------------------------------------------

/** `CNPC_VWerewolf::CheckStuck` (`0x103cb920`), 1,558 bytes. Gated on slot 0x28c; probe 1 is the
 *  navigator hull trace, and the two halves below it are the CLEAR re-probe and the BLOCKED
 *  escalation. See the definition for every arm. **SEAM**: the hull sweeps are the navigator's and
 *  do not exist here, so the probes answer CLEAR — retail's own not-stuck answer — and the body's
 *  observable tail (`SetHullSizeSmall(1)`) still runs, which is what every exit but the teleport
 *  does. */
void WerewolfCheckStuck();

/** `SetHullSizeSmall(bSmall)` (`0x10273180`) — the tail every `CheckStuck` exit but the teleport
 *  ends in. **SEAM**: the hull swap is the navigator's; the LATCH (`+0x5f2d`) is this object's and
 *  is written. */
void SetHullSizeSmall(bool bSmall);

/** SEAM for `CNPC_VWerewolf::TeleportOut` — the third-probe escape. Counted; the teleport itself is
 *  family Positions' `PositionAtHint` story. */
int32 WerewolfTeleportOutCalls = 0;

/** `CNPC_VWerewolf::GetHintTargetGroundpoint` (`0x103d68d0`) — the TARGET variant of family Hints'
 *  `GetHintGroundpoint` (`0x103d6770`): a linear scan of `m_HintData` (`+0x6714`, count `+0x6720`,
 *  stride `0x48`) comparing the ENTITY POINTER at element `+0x04`, answering the Vector at element
 *  `+0x14` — the TARGET groundpoint, not `+0x08`'s own groundpoint — and on a miss `DevWarning`ing
 *  and falling back to `GetGroundpoint(GetHintEndpoint(hint))`. So a miss still answers a point.
 *  SOURCE units, as family Hints' twin is. */
FVector GetHintTargetGroundpoint(const FHintWords& Hint) const;

/** SEAM for `CNPC_VWerewolf::GetHintEndpoint` (the hint's END entity's origin) and
 *  `GetForwardHintForHint` (the hint the forward is measured from). Both resolve through family
 *  Hints' `FindHintEndEntity` (`0x103d6520`), which is a real recovered walk over a hint store that
 *  does not exist yet, so both answer the hint's own origin and name what they stand for. */
FVector GetHintEndpointUnits(const FHintWords& Hint) const;
int32 GetForwardHintForHint(const FHintWords& Hint) const;

/** `CNPC_VWerewolf::GetForwardYawForHint` (`0x103d7210`). The working direction is seeded with
 *  `vec3_invalid` (`DAT_10713de0`…), then overwritten by `endOrigin - forwardOrigin` normalised and
 *  converted to a yaw through `0x101d2c70`; the hint TYPE then adjusts it. **Recovered from the
 *  listing, because the decompiler lost the `float10` return storage and both tails read alike**:
 *  the final compare is against `_DAT_10450568` = **360.0** and it is a WRAP, not a selection — see
 *  the definition. */
float GetForwardYawForHint(const FHintWords& Hint) const;

/** `CNPC_VWerewolf::InitializeHintData` (`0x103d7710`), 1,178 bytes — the one-shot build of the
 *  `+0x6714` array, which runs only while `+0x6720` is zero. It walks the global hint chain
 *  (`DAT_10925450`, `+0x18` next) and per hint stores the hint, its end-entity handle, its own
 *  groundpoint and its TARGET groundpoint, writing the forward yaw back through the hint's own
 *  angles and falling back to the raw origin / raw endpoint when a groundpoint fails the
 *  `0x7f800000` exponent test. **SEAM**: the global hint chain is family Hints' `HintWords` seam and
 *  resolves nothing, so the array stays empty — retail's own answer for a map with no hints — and
 *  the per-hint rule is exercised through `InitializeHintDataRow`. */
void InitializeHintData();

/** One row of the build above, applied to one hint. Separated so the recovered per-hint rule is
 *  testable while the chain that feeds it is a seam. */
FWerewolfHintGroundpoint InitializeHintDataRow(const FHintWords& Hint) const;

/** Retail's `(bits & 0x7f800000) == 0x7f800000` validity test on each component of a groundpoint —
 *  an infinity or a NaN exponent, which is what `vec3_invalid` (`FLT_MAX`) is NOT, so a `FLT_MAX`
 *  groundpoint passes this test and is stored. Recorded because it is the surprising half. */
static bool IsGroundpointExponentValid(const FVector& PointUnits);

/** `CNPC_VWerewolf::IsValidRandomMoveHint` (`0x103d7dc0`) and `::IsValidMoveHint` (`0x103d8060`) —
 *  the two 520-byte twins whose type sets differ in BOTH membership and sense. `Now` is the
 *  substrate clock; the cooldown list is family Species' `FUN_10366400`. */
bool IsValidRandomMoveHint(const FHintWords& Hint, double Now);
bool IsValidMoveHint(const FHintWords& Hint, double Now);

/** `m_iRandomMoveHintNodeZone` (`+0x670c`) — the node zone the random-move arm requires the cached
 *  nearest node's `+0x94` to equal. **SEAM**: no node graph, so `0x103d0ad0` answers "no node" and
 *  the arm refuses, which is retail's own answer when the cache is empty. */
int32 RandomMoveHintNodeZone = 0;
bool CachedNearestNodeZone(int32& OutZone) const;
