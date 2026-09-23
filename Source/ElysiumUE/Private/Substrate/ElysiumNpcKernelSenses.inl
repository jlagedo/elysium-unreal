// Story 29c-1, family **Senses** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelSenses.cpp` and the tests in
// `Tests/ElysiumNpcKernelSensesTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// --- What this family is --------------------------------------------------------------------------
//
// The bodies that answer "what do I perceive, who is my enemy, and what did I witness": the two
// aim cones, the two enemy accessors, the enemy-memory pair, `OnListened`, the two witness-record
// setters, the occlusion-edge state machine and the species overrides of `FVisible` /
// `FInViewCone` / `QuerySeeEntity` / `PassesFindEntityFOVTrace`. 26 rows; NINE of them fill
// Troika-line vtable slots and are DEFINED (not declared) here — 86, 167, 168, 196, 364, 365,
// 470, 541 and 543.
//
// THE STANDING FACT OF THIS FAMILY: **four of the six species overrides are not senses at all,
// they are the two debug ConVars.** `CNPC_VWerewolf#201`, `CNPC_VYukie#201` and `CNPC_VYukie#363`
// replace the whole LOS/cone test with `npc_ignore_senses` / `npc_ignore_player`
// (`DAT_10924fba` / `DAT_10924fb9`, `ElysiumNpcSense::IgnoreSenses` / `IgnorePlayer`) and answer
// TRUE for everything else — a werewolf has no view cone and no line-of-sight check. The shared
// gate is `SpeciesStealthSenseGate` and every one of the three arms it feeds is recovered, not
// guessed.
//
// THE SECOND STANDING FACT: **29c named three of these rows wrong, and the decompiled C says so.**
// `0x1027de00` is not `SetEnemy` — it writes `m_hBlockedDoor` (`+0x5d28`) and dispatches slot 532
// with retail's door-blocked reason `2` (`npc-kernel/signatures.md`, slot 532), so it is
// `OnDoorBlocked`. `0x101aaf80` runs no FOV cone — `0x10240250` is a six-term AABB overlap.
// `0x1036a030` does not add a gate on top of the base `QuerySeeEntity`, it REPLACES it: the whole
// body is `candidate->m_pPlayer != NULL`. Each is stated again at its definition.

// --- Words and seams this family needs ------------------------------------------------------------

/** SEAM for `CAI_Navigator::MarkNodeUnreachable` (`0x102f1fa0`), which `OnDoorBlocked`
 *  (`0x1027de00`) calls with `5.0` or `20.0` seconds and the blocking door. It reaches the AI
 *  NETWORK — it looks the door's nav link up in the node graph and stamps `node+0x64 |= 1`,
 *  `node+0x68 = curtime + seconds`. This runtime has no node graph, so the call is COUNTED and
 *  marks nothing; the retail word it stands for is the node's own unreachable-until stamp. */
int32 NavigatorUnreachableMarks = 0;

/** SEAM for `0x102ee6a0`, the guard retail puts in front of that call: "the navigator has a
 *  network AND that network has a node list". Answers false — there is no network — so retail's
 *  own refusal arm is the one taken and `NavigatorUnreachableMarks` never moves. */
bool NavigatorHasNodeGraph() const;

/** `GetSquadFocus` / `SetSquadFocus` (`0x103166b0` / `0x10316660`), squad `+0x70` (the focus
 *  handle) and `+0x74` (its expiry, `curtime + _DAT_10463584` = **15.0 s**). **SEAM**: this
 *  substrate stands no squad object (`FElysiumNpc::ConnectedSquad` answers null), so the getter
 *  answers null and the setter counts. Both are named rather than folded into one "squad seam"
 *  because `OnDoorBlocked` reads before it writes and the read's answer decides the write. */
const FElysiumEntity* SquadFocus() const;
void SetSquadFocus(const FElysiumEntity* Focus);
int32 SquadFocusWrites = 0;

/** `CBaseDoor+0x644` and `+0x640`, the two words `OnDoorBlocked` reads and writes on the DOOR.
 *  `+0x644` is a flag word tested `& 0x10` (the whole retry is skipped) and `& 0x40` (the retry
 *  waits 5 s rather than 20 s); `+0x640` is a float "do not try me again before" stamp that
 *  `0x100f0e30` MAX-writes. **SEAM**: `FElysiumEntity` carries neither word and no corpus body in
 *  layers 0–9 names them, so the flags answer 0 — retail's own "no flags" door, which takes the
 *  20-second arm — and the stamp is counted. Their retail names are **unrecovered**. */
static uint32 DoorBlockFlags(const FElysiumEntity& Door);
void SetDoorNextTryTime(FElysiumEntity& Door, double At);
int32 DoorNextTryWrites = 0;

/** `DAT_1093f8ec` and `DAT_1093d574` — the two ConVar objects `CNPC_VWerewolf::ShouldPursueEnemy`
 *  (`0x103cf5f0`) thresholds on, each read `IsCommand() ? _DAT_104454c4 (0.0) : +0x28`:
 *  `werewolf_pursuit_unseen_time` "3.0" and `werewolf_pursuit_distance` "800". */
static float WerewolfPursueElapsedLimitSeconds();
static float WerewolfPursuePlayerDistLimitUnits();

// --- Non-slot bodies ------------------------------------------------------------------------------

/** `0x10027020`, `CAISound#168` and 17 more — the BASE line's `CBaseEntity* GetEnemy()`, whose
 *  whole body is `JMP [[this]+0x29c]`: a tail jump to slot 167, the const overload, with no other
 *  work. This leaf stands the TROIKA line (`0x102b5360`, slot 168 below), so the base body is not
 *  what `FElysiumNpc` dispatches; it is carried here by name so the two lines are both in the
 *  tree and a reader can see which one this runtime runs. */
FElysiumEntity* GetEnemyBaseLine() const;

/** The arithmetic of slot 364 (`0x10326bd0`) and of `ValidEyeTarget` (`0x1025e920`), pure over the
 *  three vectors each body fetches through a virtual. Split out because the two virtuals that
 *  supply the aim — slot 370 `HeadDirection2D` and slot 371 `HeadDirection3D`, which slots 372/373
 *  forward to — are still GENERATED STUBS answering the zero vector, so the slot bodies below
 *  cannot exercise their own thresholds yet. The rule is exact and testable here; the input is
 *  another story's seam and is named at each call.
 *
 *  `AimConeAdmits` zeroes the delta's Z BEFORE normalising and compares against `0.994`;
 *  `EyeTargetConeAdmits` normalises in 3-D and compares against `0.5`. Both are strict. */
static bool AimConeAdmits(const FVector& OriginCm, const FVector& TargetCm, const FVector& Aim);
static bool EyeTargetConeAdmits(const FVector& EyeCm, const FVector& PointCm,
	const FVector& HeadDirection);

/** `0x1025e920`, `CAI_BaseHumanoid#587` — `CAI_BaseActor::ValidEyeTarget(const Vector&)`, the
 *  sibling of `ValidHeadTarget` (`0x1025ea00`, `#588`) already in `senses.md`. NOT the Troika
 *  line's slot 587 (`0x1028ef20`, `CanWitnessSupernatural`, story 29d): past `CAI_BaseActor` the
 *  two branches declare unrelated virtuals at the same index. Dots `HeadDirection3D()` (slot 371,
 *  vtable `+0x5cc`) with the NORMALISED direction from `EyePosition()` (slot 193) to the argument
 *  and requires it strictly above `0.5` (`_DAT_10449270`, a double). */
bool HumanoidValidEyeTarget(const FVector& PointCm);

/** `0x102d1320`, `CAI_Hint#163` — `CAI_Hint::IsViewable`. Pure over the hint's own words, so it is
 *  the whole recovered rule and not a seam: a hint with `m_iDisabled` set is NOT viewable (retail
 *  returns `m_iDisabled & 0xffffff00`, whose low byte — the `bool` — is always zero), and
 *  otherwise viewable exactly when `m_nHintType == 13`. */
static bool IsHintViewable(const FHintWords& Hint);

/** `0x101aaf80`, `CPayphone#45` — the payphone's `PassesFindEntityFOVTrace`. **No cone and no
 *  trace**, whatever 29c's walk says: the MANHATTAN distance between the two `EyePosition()`s
 *  against `_DAT_1047a3b0` = **85.0** Source units, and under it a six-term AABB overlap of the two
 *  entities' OBBs (`0x10240250`). Both arguments of the slot's `Vector, Vector, int` tail are
 *  ignored by the body. */
bool PayphonePassesFindEntityFovTrace(const FElysiumEntity& Other) const;

/** `0x103a4bb0`, `CNPC_ProneDialog#45` — the prone-dialog body. A ray from `FromCm` toward `ToCm`
 *  with the caller's mask; the answer is TRUE only when the trace hit THIS NPC, or hit nothing at
 *  all with `fraction == _DAT_10449280` (**1.0**). The `!= 0.0` squared-length byte retail packs
 *  into the ray request is the engine's "this ray has a direction" flag and is carried as
 *  `bOutRayIsValid` so the degenerate case is visible rather than silently equal. */
bool ProneDialogPassesFindEntityFovTrace(const FVector& FromCm, const FVector& ToCm, int32 Mask,
	bool& bOutRayIsValid) const;

/** `0x1036a030`, `CNPC_VCameraSecurity#468` — `QuerySeeEntity`. The WHOLE body is
 *  `return candidate->m_pPlayer != NULL` (`+0x00a8`, `CBaseEntity`'s self-downcast cache, which is
 *  non-null on exactly the player). It chains nothing: a security camera sees the player and
 *  nothing else. Not static because `FElysiumEntity` carries no self-downcast cache — "is the
 *  player" is `Handle == World->PlayerHandle()` here, and the world comes off this NPC. */
bool CameraSecurityQuerySeeEntity(const FElysiumEntity& Candidate) const;

/** The gate `CNPC_VWerewolf::FVisible` (`0x103cb810`), `CNPC_VYukie::FVisible` (`0x103ddaf0`) and
 *  `CNPC_VYukie::FInViewCone` (`0x103ddaa0`) share, in retail's order: a null candidate fails;
 *  `DAT_10924fba` (`npc_ignore_senses`) set fails; `DAT_10924fb9` (`npc_ignore_player`) set AND the
 *  candidate being the player fails. Nothing else. */
bool SpeciesStealthSenseGate(const FElysiumEntity* Candidate) const;

/** `0x103cb810`, `CNPC_VWerewolf#201` — `FVisible`. The gate above, then **`true`
 *  unconditionally**: a werewolf has no range check, no cone and no line of sight. On refusal it
 *  zeroes the blocker out-parameter, which is retail's own write. */
bool WerewolfFVisible(const FElysiumEntity* Candidate, FElysiumEntityHandle* OutBlocker);

/** `0x103ddaa0`, `CNPC_VYukie#363` — `FInViewCone(CBaseEntity*)`. The gate and nothing else: Yukie
 *  has no view cone. */
bool YukieFInViewCone(const FElysiumEntity* Candidate) const;

/** `0x103ddaf0`, `CNPC_VYukie#201` — `FVisible`. The gate, and on success the base check through
 *  vtable `+0x948` (slot 594, `0x102b4760`, story 29d) rather than the werewolf's unconditional
 *  true. The refusal arm zeroes the blocker only on the `npc_ignore_player` branch and on the
 *  `npc_ignore_senses` branch, not on a null candidate — retail's own asymmetry. */
bool YukieFVisible(const FElysiumEntity* Candidate, FElysiumEntityHandle* OutBlocker);

/** `0x103dda10`, `CNPC_VYukie#602` — Yukie's replacement for the melee-leave decision the Troika
 *  line answers at `FElysiumNpc::Slot602`. Two arms on slot 308 `HasUsableRangedWeapon()` (vtable
 *  `+0x4d0`): with no ranged weapon, leave when `2 * meleeRange * 1.5` (`_DAT_1044f02c`) is
 *  **`<=`** `m_flEnemyDist`; with one, leave when `m_flMeleeMustLeaveTimer` has expired. No
 *  frenzy gate, no follower-boss gate and no attack coordinator — the four terms the Troika body
 *  spends its first half on are simply gone. */
bool YukieShouldLeaveMelee();

/** `0x1028ea60` — the CRIMINAL witness record, written whole: the witnessed level (obfuscated into
 *  `+0x6364`), the three-float location (`+0x6380`) and the offender handle (`+0x638c`, or `-1`
 *  for no entity). `0x1028eb30` below is its supernatural twin. */
void RecordCriminalWitness(int32 Level, const FVector& AtCm, const FElysiumEntity* Offender);

/** `0x1028eb30` — the SUPERNATURAL witness record: the level PLAIN at `+0x6368`, the location at
 *  `+0x6374`, the offender at `+0x6390` and `m_bPLSupernaturalActFleeOnly` at `+0x6394`, which is
 *  written on BOTH arms. */
void RecordSupernaturalWitness(int32 Level, const FVector& AtCm, const FElysiumEntity* Offender,
	bool bFleeOnly);

/** `m_iPLCriminalLevelWitnessed` (`+0x635c`) is a `custom` datamap type — a `CSecureType` whose
 *  payload lives at `+0x6364` scrambled. `0x1028ea60` writes it through `0x1042fde0` and
 *  `CNPC_VPedestrian::vfunc461` (`0x103a2e30`) reads it back through `0x1042fe90`; the two
 *  round-trip. Ported as a pair of pure functions so the recovered constants are in the tree and
 *  checkable, while the port's own `FElysiumNpcWitnessChannel::Level` carries the plain number —
 *  the obfuscation is anti-tamper, not behaviour, and nothing the bytecode runs can observe it. */
static uint32 EncodeWitnessedLevel(uint32 Level);
static uint32 DecodeWitnessedLevel(uint32 Stored);

/** `+0x6360` and `+0x6361`, the two bytes `0x1028ea60` writes beside the scrambled level.
 *  **RECOVERED RETAIL DEFECT**: the listing (`1028ea72` `MOV DL,[ESP+0x8]`, `1028eaa9`
 *  `MOV CL,[ESP+0x5]`) reads them out of the eight bytes `SUB ESP,0x8` just allocated and nothing
 *  ever writes — they are uninitialised stack. Carried so the write is not silently dropped; this
 *  runtime writes 0, which is the one value an indeterminate read cannot be reproduced as. */
uint8 CriminalWitnessByte6360 = 0;
uint8 CriminalWitnessByte6361 = 0;

/** `0x1027de00`, which 29c filed as `SetEnemy`. It is the **door-blocked** notice: `0x10298840`
 *  calls it when `0x100eec70` refuses the NPC/door pair, and `0x1027dfb0` (the hit-by-door
 *  handler) forwards to it when the door that hit this NPC is the one it was opening. Seven arms,
 *  in retail's order, at the definition. */
void OnDoorBlocked(FElysiumEntity& Door);

/** `0x103cf5f0`, `CNPC_VWerewolf::ShouldPursueEnemy`. `m_DoorState`-adjacent flag word `+0x66e8`
 *  bit 2 (`& 4`) skips the whole test; otherwise BOTH of two independent gates must fail before a
 *  werewolf gives up the chase. */
bool WerewolfShouldPursueEnemy() const;

/** `0x10270180` — the occlusion-EDGE state machine behind `m_bEnemyWentOccluded` (`+0x5bc5`) and
 *  `m_vecEnemyWentOccluded` (`+0x5bc8`). Three arms on (enemy, bHaveLos), none of which is a
 *  fall-through; the distance gate is `_DAT_104563b0` = **4096.0**, a SQUARED distance, so the
 *  edge trips at 64 Source units of drift. */
void UpdateEnemyWentOccluded(const FElysiumEntity* Enemy, bool bHaveLos);

/** `0x1026a2a0` — `SetDistLook`. Writes `m_pSenses->m_LookDist` (`CAI_Senses+0x10`, which
 *  `vtmb_fields CAI_Senses` names outright; 29c filed the inner field as unrecovered). The
 *  argument is in SOURCE units in retail and CENTIMETRES here, as every port distance is. */
void SetDistLook(float LookDistCm);
