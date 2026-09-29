// `CAI_BaseNPC`'s declarations of the `Motor10` family (story 5 step 5),
// moved from `ElysiumNpcMotor10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMotor10.cpp`.

/** What this family's seams were ASKED, so a test can assert that a body reached its call and that
 *  the refusal was the recovered one. Read by the test suite and by nothing else. Separate from
 *  family Motor's `MotorSeams` so that family's own cases keep their exact tallies. */
struct FMotor10SeamLedger
{
	int32 MoveTraceSweeps = 0;            // `0x102e6d70`
	int32 LastMoveTraceKind = INDEX_NONE;
	int32 LastMoveTraceMask = 0;
	float LastMoveTraceExtent = 0.f;
	int32 SetOriginCalls = 0;             // `UTIL_SetOrigin 0x101cf5c0`
	FVector LastSetOriginUnits = FVector::ZeroVector;
	int32 WeaponOwnsAsks = 0;             // Weapon_OwnsThisType
	int32 BuildLocalRouteAsks = 0;        // `0x10304130`
	int32 SplicePathAsks = 0;             // `0x10319f30`
};

//
// Retail's `AILocalMoveGoal_t` as much of it as `CAI_Motor#19` (`0x102e14a0`), its helper
// `0x102e1560` and `CAI_Motor#20` (`0x102e1760`) reach. Four words, all read from the listing.
// Those three step bodies are gone (story 6: the character movement component steps the body);
// the view stays for `OnObstructingDoor` (slot 531), which casts its move-goal block to it:
//
//   `+0x0c..+0x14`  `dir`            the unit direction the step travels
//   `+0x28`         `maxDist`        the distance the goal still has room for
//   `+0x34`         `pMoveTarget`    the entity the goal EXPECTS to be blocked by
//   `+0x38`         `flags`          bit `0x2` is the only one `0x102e1560` tests
//
// SOURCE units, as every retail position word in this port is. Carried as a declared view rather
// than as NPC state because a move goal is the CALLER's block, not the body's: retail builds one on
// the stack per interval and hands it down.
struct FLocalMoveGoal
{
	FVector DirUnits = FVector::ZeroVector;   // +0x0c / +0x10 / +0x14
	float MaxDistanceUnits = 0.f;             // +0x28
	FElysiumEntity* ExpectedBlocker = nullptr;// +0x34
	uint32 Flags = 0;                         // +0x38, bit 0x2
};

/** `AIMoveTrace_t`, the 0x38-byte (14-dword) record `0x102e6d70` zeroes and fills. The layout is
 *  read out of `0x102e6d70`'s own initialiser, which writes `[0]`, `[1..3]`, `[4..6]`, `[7]` and
 *  `[9]` by index before dispatching on the trace kind:
 *
 *    `+0x00` `fStatus`          an `AIMoveResult_t`; the trace's own answer is `fStatus >= 0`
 *    `+0x04` `vEndPosition`     seeded to the START point
 *    `+0x10` `vHitNormal`       seeded to `vec3_origin` (`DAT_1070d1b0`)
 *    `+0x1c` `pObstruction`     seeded null
 *    `+0x20` `flDistObstructed`
 *    `+0x24` `flTotalDist`      seeded 0
 *    `+0x28` an `EHANDLE`, `+0x2c`, `+0x30`, `+0x34` — VtMB's four extra words
 *
 *  **`+0x2c` and `+0x30` have no recovered meaning and `CAI_Motor#20` does not copy them out.**
 *  That is not an oversight in the port: the listing's copy block at `102e189c`–`102e18ee` writes
 *  `+0x00`, `+0x04`, `+0x08`, `+0x0c`, `+0x10`, `+0x14`, `+0x18`, `+0x1c`, `+0x20`, `+0x24`, the
 *  `EHANDLE` at `+0x28` through its assignment operator, and `+0x34` — and nothing else. */
struct FMotorMoveTrace
{
	int32 Status = 0;                                  // +0x00 fStatus
	FVector EndPositionUnits = FVector::ZeroVector;    // +0x04
	FVector HitNormal = FVector::ZeroVector;           // +0x10
	FElysiumEntity* Obstruction = nullptr;             // +0x1c
	float DistObstructedUnits = 0.f;                   // +0x20
	float TotalDistUnits = 0.f;                        // +0x24
	FElysiumEntityHandle ObstructionHandle;            // +0x28
	int32 Word2c = 0;                                  // +0x2c  NOT copied out by slot 20
	int32 Word30 = 0;                                  // +0x30  NOT copied out by slot 20
	int32 Word34 = 0;                                  // +0x34
};

/** SEAM for `thunk_FUN_101cf390(this, mins, maxs)` = `UTIL_SetSize` — the bounds write both hull
 *  bodies make. `FElysiumEntity` carries no collision box (family Motor's `RetailCollisionExtents`
 *  records the same gap), and the mins/maxs come off family Motor's `RetailHullExtents`, which
 *  answers the ZERO box, so what is recovered is that the write happened and with what. SOURCE
 *  units. */
FVector LastSetSizeMinsUnits = FVector::ZeroVector;

FVector LastSetSizeMaxsUnits = FVector::ZeroVector;

int32 SetSizeCalls = 0;

/** SEAM for `thunk_FUN_10272f40(this)` — `CAI_BaseNPC::SetupVPhysicsHull`, the VPhysics shadow
 *  rebuild both hull bodies run when `m_pPhysicsObject` (`+0x36c`) is live. **SEAM**: there is no
 *  VPhysics shadow here; counted. */
int32 VPhysicsHullRebuilds = 0;

/** `+0x036c`, the physics object pointer both hull bodies gate the rebuild on. **SEAM**: this
 *  substrate stands no physics object on the kernel surface, so it answers false and the rebuild is
 *  skipped — which is retail's own arm for an NPC with no `VPhysicsGetObject()`. Carried as a bool
 *  a case can raise, because the gate is the recovered half. */
bool bHasVPhysicsObject = false;

mutable FMotor10SeamLedger Motor10Seams;

/** `thunk_FUN_102e6d70(motor->+0x68, kind, start, end, mask, 0, extent, 0, &trace, filter, 0)` —
 *  `CAI_MoveProbe::MoveLimit` `0x102e6d70`, dispatched on the nav type (0 ground, 1 jump, 2 fly,
 *  3 climb) and answering `trace.fStatus >= 0`. It performs retail's own initialisation of the
 *  record; the ground arm is the body's NavMesh raycast (`IElysiumNpcMotor::NavRaycast`, the named
 *  divergence the body states), and every other arm keeps the initialised (admitting) record —
 *  the definition names the service or the story each arm is owed to. Counted. */
bool MotorMoveTraceSweep(int32 Kind, const FVector& StartUnits, const FVector& EndUnits, int32 Mask,
	float ExtentUnits, const void* Filter, FMotorMoveTrace& OutTrace) const;

/** SEAM for `CBaseCombatCharacter::Weapon_OwnsThisType(name, 0)` (`0x102c7aa4` /
 *  `0x102c7ad7`) — does this body carry a weapon of the named class? The two names are
 *  `"weapon_smg1"` (`0x10601ef8`) and `"weapon_pistol"` (`0x10601ee8`), read out of the listing.
 *  **SEAM**: answers false. Its live caller is the zombie maker's fists test. */
bool WeaponOwnsThisType(const TCHAR* WeaponClassname) const;

/** `NAI_Hull::Bits(hull)` = `PTR_DAT_1060a750[hull][0]` (`0x102d6210`) — the used-hull bit of one
 *  hull id. **SEAM**: family Motor already records that there is no hull table here
 *  (`RetailHullExtents`); this answers **0** for every id, which makes every candidate miss. */
static int32 RetailHullBits(int32 Hull);

// The global used-hull mask `DAT_10610be8` (`0x102f9950` / `0x102f9900` / `0x102f9920`) is free
// functions, not `CAI_BaseNPC` bodies: `Substrate/ElysiumNpcUsedHullBits.h`.

/** `0x10273070` `CAI_BaseNPC::SetHullSizeNormal(bool force)` — nineteen direct callers plus two
 *  outside, the widest-called body in this band. Answers nothing; retail's `RET 0x4` leaves no
 *  value. */
void SetHullSizeNormal(bool bForce);

/** `0x10273180` `CAI_BaseNPC::SetHullSizeSmall(bool force)` — the twin with the gate INVERTED, and
 *  it answers **1 unconditionally**, including on the path where the gate refused and nothing
 *  changed. That is retail's and is reproduced. */
bool SetHullSizeSmall(bool bForce);

/** `0x10278650` — **`CAI_BaseNPC::GetShootTarget(posSrc, bNoisy, bFlag2)`**, and NOT the standoff
 *  anchor the checklist's row named. The reading that settles it: the call at `102786f1`–`10278709`
 *  is `this->slot 541 GetEnemies()` (no arguments, so the two pushes around it belong to the NEXT
 *  call) then `CAI_Enemies::GetLastKnownPosition(&lkp, enemy)` (`0x102dfed0`, whose own DevWarning
 *  is `"Asking LastKnownPosition for ene…"`); the enemy comes from `this->slot 167 GetEnemy()`; the
 *  offset comes from `enemy->slot 197 BodyTarget(posSrc, bNoisy, bFlag2)`; and the cached handle at
 *  `+0x5ba8` the body short-circuits on is the shape map's own `ShootTargetOverride`.
 *
 *  In retail's order:
 *
 *    1. `m_hShootTargetOverride` (`+0x5ba8`) resolving → **that entity's `GetAbsOrigin()`**, and
 *       the three arguments are ignored entirely.
 *    2. No enemy (slot 167) → `AngleVectors(GetAngles(), &forward)` (slot 221, `0x10139610`) and
 *       the answer is `forward + posSrc`.
 *    3. An enemy → `lkp + (BodyTarget(posSrc, bNoisy, bFlag2) - enemy->GetAbsOrigin())`, with the
 *       body target's **Z raised by `_DAT_104994e0` = -30.0** first when the enemy's per-class
 *       type-3 `CVStatList_t` answers **5** for stat `0xb`.
 *
 *  SOURCE units in, SOURCE units out. */
FVector GetShootTarget(const FVector& PosSrcUnits, bool bNoisy, bool bFlag2) const;

/** SEAM for `0x10278650`'s stat lookup — `enemy->+0x9c` (its player record) then its `+0x13bc`
 *  count / `+0x13c0` table walked for the first list whose `+0x10` is **3**, then
 *  `CVStatList_t::GetValue(0xb)` (`0x102012d0`); an entity with no type-3 list falls back to the
 *  lazily-built EMPTY global `DAT_109f0b40`, whose `GetValue` answers 0. Family **Sounds10**
 *  records the same join for `FireBullets`' ranged skill and takes the same refusal: this
 *  runtime's sheet carries no `CVStatList_t` keyed by retail's list type, so the join is
 *  **unrecovered** and the answer is **0** — which is not 5, so the Z offset is not applied. */
static int32 EnemyTypedStatValue(const FElysiumEntity& Enemy, int32 StatId);

/** SEAM for slot 221 `CBaseEntity::GetAngles()` (`0x100b3110`, vtable `+0x374`) in SOURCE degrees —
 *  the angles arm 2 turns into a forward vector. Slot 221 is a generated stub on this line, so this
 *  answers this body's own `FElysiumEntity::Angles`, which is the port's equivalent word, as a
 *  SOURCE `(pitch, yaw, roll)` triple in degrees. */
FVector RetailGetAnglesDegrees() const;

/** `0x10139610` `AngleVectors(angles, &forward, NULL, NULL)`, forward only, read off the decompiled
 *  body: with `_DAT_1044eb08` the degrees-to-radians scale,
 *  `forward = (cos(pitch)cos(yaw), cos(pitch)sin(yaw), -sin(pitch))`. SOURCE axes. */
static FVector AngleVectorsForward(const FVector& AnglesDegrees);

/** `UTIL_SetOrigin(owner, trace.vEndPosition, true)` — `0x101cf5c0`, whose body is `entity->slot 62
 *  SetLocalOrigin(vec)` then `PhysicsTouchTriggers()` when the third argument is non-zero.
 *
 *  **A CORRECTION.** The checklist's walk of `0x102e1760` calls this `NDebugOverlay::Line`. It is
 *  not: `docs/vtmb/npc-kernel/layout.md`'s `m_vecGrappleSavedOrigin` row already names `0x101cf5c0`
 *  "the SetAbsOrigin helper", and the body itself is the two statements above. A one-line forward
 *  to the chain's slot 62 `SetOrigin`. */
void MotorSetOriginToTraceEnd(const FVector& EndPositionUnits);

