// `CAI_BaseNPC`'s declarations of the `Senses10` family (story 5 step 5),
// moved from `ElysiumNpcSenses10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSenses10.cpp`.

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
	int32 UpdateCalls = 0;   // 0x102e8560, reached by RunTaskOverlay 0x10289c90
};

FMoveAndShootOverlay MoveAndShootOverlay;

FVPhysicsShadowBuild VPhysicsShadow;

bool bHasPhysicsObject = false;   // +0x36c m_pPhysicsObject, as a "is one standing" answer

/** SEAM for `CBaseCombatCharacter::CreateSecondaryDiscParticles(m_vDiscBloodType)` — the explosive
 *  gib's particle burst, keyed on `m_vDiscBloodType` (`+0xfd0`). Visual only and no particle system
 *  reaches this substrate; counted so the arm is observable. */
int32 SecondaryDiscParticleBursts = 0;

/** `m_iSquadDisconnected` (`+0x5bb0`) and the squad word (`+0x5da4`) that slot 544's first gate
 *  reads off BOTH this NPC and the candidate. **SEAM**: this substrate stands no squad object
 *  (`ConnectedSquad()` answers null), so the squad word answers `0` — retail's own "no squad" value,
 *  which makes the gate's third term false and lets every candidate through. That is the admitting
 *  arm, so nothing is silently refused. `m_iSquadDisconnected` is a real per-NPC word with no
 *  port producer and ships at `0`, which is retail's connected state. */
int32 SquadDisconnected = 0;    // +0x5bb0

int32 IRelationPriorityOf(const FElysiumEntity* Candidate) const;

/** `CAI_BaseNPC::OnLooked` (`0x1026a2c0`), 624 bytes — a DISTINCT retail function beside the Troika
 *  override `0x102b39a0` that owns slot 469, so it takes its own name and is not a slot body.
 *
 *  The body itself is `ElysiumNpcCond::GatherSight` (`ElysiumNpcConditions.cpp`), which is where the
 *  port has carried it since story 10b and where story 29d added the two gates it was missing (the
 *  `relation != D_NU` gate and the `SEE_ENEMY` raise, both cited at the line that does them). This
 *  is the NAMED ENTRY POINT retail's slot 469 calls first — it runs that body against this NPC's
 *  own condition set on the substrate clock, which is what `CAI_Senses::Look` does. */
void BaseOnLooked();

/** The visibility term both `BestEnemy` bodies compute: `CAI_Senses::DidSeeEntity`
 *  (`0x1030fb10`, this Look pass's accepted set) OR slot 201 `FVisible(cand, 0x2804091, 0, 0)`. */
bool BestEnemyCandidateVisible(FElysiumEntity* Candidate);

/** `__ftol` of the SUM OF SQUARES between two slot-217 origins, in SOURCE units squared — the
 *  distance key both `BestEnemy` bodies compare. `0x10431320` is plain `__ftol`; there is no root. */
int32 BestEnemyDistanceKey(const FElysiumEntity& Candidate) const;

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
 *       `5` (-30.0). **SEAM**: the `CVStatList_t` join by retail list TYPE does not exist on this
 *       sheet, so the stat reads not-5 and the offset is not applied. */
FVector ShootEnemyAimPoint(const FVector& ShootPositionCm);

void BuildVPhysicsShadow();       // 0x10272f40

/** SEAM for the model's authored `mass` keyvalue (`0x10272f40` reads it off the studio header).
 *  No studio header is parsed on this substrate; answers `0.0`, which is retail's at-or-below-zero
 *  arm and therefore takes the gender default rather than refusing. */
float ModelMassKeyvalue() const;

/** The gender half of that default: `90` male, `65` female. `FElysiumNpc` has no recovered gender
 *  word yet, so this answers the MALE default and names the retail read. */
bool IsFemaleBody() const;

void CreateSecondaryDiscParticles();

uint32 SquadWord() const;       // +0x5da4

/** SEAM for `CAI_Memory::UpdateMemory` (`0x102df700`), the call slot 544 forwards to with the node
 *  array at `m_pNavigator+0x2c`. This runtime's store is `FElysiumNpcEnemyMemory`; the node array is
 *  the AI network, which does not exist here, and the two node ids the record carries stay
 *  `INDEX_NONE`. Answers true when the target gained its FIRST record, which is retail's answer. */
bool UpdateCaiMemory(FElysiumEntity* Enemy, const FVector& PositionCm);

/** The `0x46004003` segment trace slot 573 runs (`0x1026fcf0`), whose HIT ENTITY is what its three
 *  arms branch on: family Motor's `KernelHullTrace` with a zero box (a ray), which answers
 *  `IElysiumEmbodiment::TraceRetail` with this NPC ignored and the characters folded by
 *  `KernelTraceKeepsCharacter`. True = clear (`fraction == 1.0`); false = blocked, with
 *  `OutBlocker` the hit entity (null for the static world). With no world or no embodiment the
 *  trace is CLEAR -- the fault path of a headless run, not a rule. Centimetres in, converted to
 *  Source units at the call. */
bool InnateWeaponLosTrace(const FVector& StartCm, const FVector& EndCm,
	FElysiumEntity*& OutBlocker) const;

/** What one ray of the weapon's line of fire (`0x1024f3d0`) reads off its `trace_t`. */
struct FWeaponLosRay
{
	float Fraction = 1.f;                  // trace_t +0x2c (`[ESP+0x5c]`)
	FElysiumEntity* Hit = nullptr;         // trace_t::m_pEnt (`[ESP+0x7c]`); null = the static world
};

/** `Ray_t::Init(start, end)` (`0x10015929`, a line), `CTraceFilterSimple(ignore, 0)` (`0x1000bd7f`),
 *  `enginetrace->TraceRay(ray, 0x46004003, &filter, &tr)` (`0x1024f424..0x1024f42a`). One
 *  `IElysiumEmbodiment::TraceRetail` with `Ignore` as the pass entity and the character list folded
 *  by `KernelTraceKeepsCharacter` (`CTraceFilterSimple::ShouldHitEntity 0x101d31c0`), nearest kept
 *  first; a character recorded `FSOLID_NOT_SOLID` (`FElysiumEntity::IsRetailNotSolid`, a corpse
 *  after `BecomeClientRagdoll 0x10090180`) is never met, as the engine never hands one to the
 *  filter. Centimetres. With no world or no embodiment the ray is clear (the headless fault path). */
FWeaponLosRay WeaponLosRay(const FVector& StartCm, const FVector& EndCm,
	const FElysiumEntityHandle& Ignore) const;

/** The weapon's line of fire: weapon slot 364 `0x1024f330` (`+0x5b0`) hands owner slot 389's point
 *  to its vtable `+0x470` = `0x1024f3d0(owner, ignore, &start, &end, bSet)`. The port stands no
 *  weapon vtable (one body fills both slots for every weapon class), so the body is the owner's.
 *
 *  `0x1024f3d0`, in the listing's order: clear (`fraction == 1.0`) -> true; the hit is the owner's
 *  `GetEnemy()` -> true; a hit combat character (`+0x9c`): `IRelationType == D_HT` -> true (shot
 *  through), else `0x63` under `bSetConditions` and false; no combat character: `m_CollisionGroup
 *  (+0x368) == 4` and `fraction > 0` -> the same body again from the hit point with the HIT ENTITY
 *  as the filter's pass entity (not the owner), else `0x66` under `bSetConditions` and false.
 *  Unlike slot 573 it never writes `m_hEnemyOccluder`.
 *
 *  `Depth` is the port's recursion guard (retail's is unbounded): past `GWeaponLineOfFireMaxDepth`
 *  re-traces the body takes the `0x66` arm. Centimetres. */
bool WeaponLineOfFire(const FVector& ShootPosCm, const FVector& TargetCm,
	const FElysiumEntityHandle& Ignore, bool bSetConditions, int32 Depth = 0);

/** `0x1026ab50` — the shrunk-hull head probe `RunAI` (`0x1026f110`) runs between `GatherConditions`
 *  and `PrescheduleThink`. It is UNPORTED as a behaviour and this is why: its whole body is gated on
 *  `+0x5f2d` (the shrunk-hull latch) AND `+0x5f2c`, and neither word has a port producer — the hull
 *  swap they record is `CAI_Navigator`'s, which this substrate does not stand. The recovered rule is
 *  written out in full at the definition and the two latches are declared below so the day the
 *  navigator lands the probe is one function that changes rather than one that is invented.
 *
 *  With `m_fIsUsingSmallHull` false — which is every NPC today — the body does nothing, which is
 *  retail's own answer for a body whose hull was never shrunk. The two latches are `FElysiumNpc`'s
 *  own `bIsUsingSmallHull` (`+0x5f2d`) and `bWantsLargeHull` (`+0x5f2c`), declared by story 29b. */
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

/** SEAM for `0x102edae0(navigator, threat, fleeDistance, 30000.0, &out)` -- the navigator's node
 *  search `0x103008f0` / `0x10300b50` around the threat, which takes the caller's flee distance and
 *  the constant 30000.0 (a recursive node walk that ends on a node `CanStandAt` accepts), then the
 *  node's position (`0x102ee9c0`). Not "the nearest node within 30000" (story 5 step 4r). No AI
 *  network stands here; answers false, which is the march arm's failure branch. */
bool NearestNavigatorNode(const FVector& ThreatCm, float FleeDistanceUnits, float SearchLimitUnits,
	FVector& OutNodeCm) const;

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

void DisableMoveAndShootOverlay();                       // 0x102e8250

void ArmMoveAndShootOverlay(float PauseMin, float PauseMax);  // 0x102e8270

/** SEAM for `UTIL_Remove(this)` (`0x101cd940`), slot 402's no-explosive-gibs arm. `FElysiumEntity`
 *  has `Remove`-shaped lifetime elsewhere in this substrate; this counts the call and names it so
 *  the arm that removes the body is distinguishable from the arm that fades it. */
int32 UtilRemoveCalls = 0;

void UtilRemoveSelf();

/** SEAM for `FL_NOTARGET` — `CBaseEntity::GetFlags()` bit `0x8000`, which `BestEnemy` reads as the
 *  SIGN of `(flags >> 8)` (`10274449` `TEST AH,AH / JS`). `FElysiumEntity::Flags` carries no
 *  `FL_NOTARGET` bit and no producer sets one, so this answers false — the admitting arm. */
static bool HasNoTargetFlag(const FElysiumEntity& Candidate);

/** SEAM for `m_bIsBCCTargetable` (`+0x1480`), the byte `BestEnemy` reads off a candidate's `+0x9c`
 *  combat character and both `BestEnemy` bodies gate on. `FElysiumCombatCharacter` carries no such
 *  word and no body in the kernel closure clears it, so this answers **true** — the ADMITTING arm,
 *  which is retail's own answer for an untouched character. Named so the gate is in the tree. */
static bool IsBccTargetable(const FElysiumEntity& Candidate);
