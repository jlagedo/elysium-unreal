// Story 29c-1, family **Bosses** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelBosses.cpp` and the tests in
// `Tests/ElysiumNpcKernelBossesTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is the **unnamed bodies of the boss species** between `0x10381000` and `0x1039a000`:
// `CNPC_VHengeyokai`'s pickup chain, `CNPC_VManBat`'s (the Sheriff's bat form) flight and carry
// chain, `CNPC_VMingXiao`'s tentacle rules, and the melee-slot bodies of the `CNPC_VAndreiBlood`
// human line. This family owns NO generated slot stub: every declaration below is a plain method.
//
// THREE STANDING FACTS OF THIS FAMILY, stated once here rather than at forty call sites.
//
//   * **There is no ragdoll and no physics object in this substrate.** Every one of these bosses
//     picks a body up by creating a `phys_animlink` entity, looking a named bone up in the carried
//     model, asking the carried `CRagdollProp` for the element at that bone (vtable `+0x424`), and
//     later pushing that element with an impulse (`+0x428`) or, when the carried thing is not a
//     ragdoll, through its `IPhysicsObject` at `+0x36c`. None of `phys_animlink`,
//     `CRagdollProp::GetElement`, `IPhysicsObject::ApplyForceCenter` or the ballistic solve
//     `0x102c4cc0` exists here. Each is a seam below, each answers nothing, and each names the
//     retail call it stands for. The RULES around them — which bone, which arm, what is written,
//     what is cleared — are ported verbatim and are what the tests measure.
//   * **The SafeDisc secure integers are carried as plain integers.** `CNPC_VManBat`'s
//     `m_iMoveGoalNodeMode` (+0x6668) and `CNPC_VHengeyokai`'s `m_SecurePickupParam` (+0x6698) are
//     `MvsnSec::CSecureType<int>` — a vtable, two junk salt bytes and an XOR/add-obfuscated word —
//     and every read runs the stored word back through `0x1042fbf0`/`0x103908c0` before comparing
//     it. The obfuscation is copy protection, not gameplay: the decoded value is the whole of the
//     observable state. The two mode constants `0xfa0b069a` / `0xfa0b069b` that `0x1038b370`
//     compares against decode to **6** and **7** (`FUN_1042fbf0` applied to each), and the mode
//     `0x1038b370` writes and restores around its hint search decodes to **2**. A NAMED
//     MODERNIZATION, and the only one this family takes.
//   * **Distances are SOURCE UNITS below the seam.** Retail's constants (1025, 48, 64, 150, 200,
//     257, 300, 1024) are Source units and this world is centimetres; `ElysiumMove::U` bridges them
//     at the point of use so the recovered number stays visible in the code.

// --- Species words this family's bodies touch ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the species words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These are this family's, each by its retail name and owning class
// (`docs/vtmb/npc-kernel/layout.md`). Where another family already declared a word at one of these
// offsets for a DIFFERENT species it is left alone and a separate member stands here, exactly as
// family Motor recorded for `+0x66b8` and `+0x66d0`.

// `CNPC_VHengeyokai`'s pickup chain. `+0x6664`/`+0x6668`/`+0x6684`/`+0x6690` are its own datamap
// words; family Motor's `PickupTarget`/`PickupTargetPos` are `CNPC_VTzimisce`'s at +0x6670/+0x6674
// and are a different species' fact at a different offset.
FElysiumEntityHandle HengeyokaiPickupTarget;             // +0x6664 m_hPickupTarget
int32 HengeyokaiPickupTargetGrabBone = 0;                // +0x6668 m_iPickupTargetGrabBone
FVector HengeyokaiPickupTargetPos = FVector::ZeroVector;  // +0x6684 m_vecPickupTargetPos, SOURCE units
FElysiumEntityHandle HengeyokaiPhysicsAnimlink;          // +0x6690 m_hPhysicsAnimlink
// +0x6698 m_SecurePickupParam, the `MvsnSec::CSecureType<int>` whose encoded word is +0x66a0.
// Carried decoded (see the standing facts above). `0x10382670` hands it to the carried ragdoll's
// `+0x2fc` and `+0x424`; nothing in layers 0–9 writes it but the constructor `0x1037e680`.
int32 HengeyokaiPickupParam = 0;

// `CNPC_VManBat`'s own words.
bool bManBatReachedMoveGoal = false;   // +0x6664 m_bReachedMoveGoal
int32 ManBatMoveGoalNodeMode = 0;      // +0x6668 m_iMoveGoalNodeMode, DECODED
int32 ManBatMoveGoalNodeId = 0;        // +0x6674 m_iMoveGoalNodeID
double ManBatFlapTimer = 0.0;          // +0x6678 m_flFlapTimer, an absolute curtime deadline
FElysiumEntityHandle ManBatFlyNode;    // +0x6688 m_pFlyNode — a raw `CBaseEntity*` in retail
FElysiumEntityHandle ManBatPickupTarget;      // +0x668c m_hPickupTarget
FElysiumEntityHandle ManBatPhysicsAnimlink;   // +0x6690 m_hPhysicsAnimlink
bool bManBatPickupTargetBreakable = false;    // +0x6694 m_bPickupTargetBreakable
FElysiumEntityHandle ManBatFlyByTarget;       // +0x66ac m_hFlyByTarget

// `CNPC_VMingXiao`'s own words. `m_rhProxies` (+0x668c) and `m_rhSeveredTentacles` (+0x66a8) are
// family **Squad**'s (`ElysiumNpcKernelSquad.inl`) and `m_bBlockedByFriend` (+0x6750) is family
// **Motor**'s; this family reads all three through their owners rather than standing copies.
int32 MingXiaoTentacleId = INDEX_NONE;       // +0x6674 m_iTentacleID, -1 on the head
FElysiumEntityHandle MingXiaoMeleeWeapon;    // +0x667c m_hMeleeWeapon
double MingXiaoProxyReadyTimer = 0.0;        // +0x66a4 m_flProxyReadyTimer, an absolute deadline
double MingXiaoAttackTimers[6] = {};         // +0x66c4 m_rflAttackTimers[6], absolute deadlines
int32 MingXiaoConnectedTentacleCount = 0;    // +0x670c m_iConnectedTentacleCount
uint32 MingXiaoSeveredTentacleMask = 0;      // +0x6710 m_iSeveredTentacleMask
FElysiumEntityHandle MingXiaoThrowObject;    // +0x6718 m_hThrowObject
int32 MingXiaoThrowingTentacle = 0;          // +0x671c m_eThrowingTentacle
int32 MingXiaoThrowableObjectMode = 0;       // +0x673c m_eThrowableObjectMode

/** One row of a `CUtlVector<BlacklistedEntity_t>` — the 8-byte `{EHANDLE, float expiry}` pair
 *  `CNPC_VBaseBoss` keeps at `+0x665c` and `CNPC_VHengeyokai` repeats at `+0x66a4`. */
struct FBlacklistedEntity
{
	FElysiumEntityHandle Entity;
	double ExpiresAt = 0.0;    // an absolute curtime stamp, carried as double
};

/** `+0x66a4 CNPC_VHengeyokai::m_BlacklistedEntities` — the store `0x10382970` appends to and
 *  `0x10382aa0`/`0x10382b30` walk. `+0x66a8` (allocation count), `+0x66ac` (grow size), `+0x66b0`
 *  (size) and `+0x66b4` (element pointer) are `CUtlMemory`'s own bookkeeping and have no counterpart
 *  on a `TArray`; the constructor's reserve of 8 rows is not observable and is not reproduced. */
TArray<FBlacklistedEntity> HengeyokaiBlacklist;

// --- The seams this family stands ------------------------------------------------------------------

/** SEAM for `CBaseEntity::CreateNoSpawn(this, "phys_animlink", vec3_origin)` — the link entity both
 *  `AttachPickupAnimlink` arms create before wiring it (`0x1014f210`) to the carried ragdoll's
 *  element. This runtime registers no `phys_animlink` class. Answers an invalid handle, which is
 *  retail's own "CreateNoSpawn failed" arm and the one that returns false without touching a word. */
FElysiumEntityHandle CreatePhysAnimlink();

/** SEAM for `CBaseAnimating::GetModelPtr(-1)` plus the linear scan over `studiohdr_t`'s bone table
 *  (`+0xf0` count, `+0xf4` offset, stride `0xa0`, `__strcmpi` on the name) that both attach bodies
 *  run to turn a bone NAME into an index. This runtime's animating tier exposes no bone table to the
 *  kernel. Answers `INDEX_NONE`.
 *
 *  Retail's scan is worth recording: it breaks on the FIRST match and leaves the index in the loop
 *  counter, and when no bone matches it leaves the counter at the bone COUNT — a positive number, so
 *  the `index < 0` guard that follows does not fire and the link is made against a bone that does
 *  not exist. That is retail's behaviour, not a transcription slip. */
int32 LookupBoneByName(const TCHAR* BoneName) const;

/** SEAM for the `__RTDynamicCast` to `CRagdollProp` (type descriptor `0x1057fff0`) plus
 *  `CRagdollProp::GetElement` (vtable `+0x424`), which answers the physics element for a bone index
 *  or for the decoded `m_SecurePickupParam`. Answers false. */
bool RagdollElementForBone(const FElysiumEntity* Carried, int32 BoneIndex) const;

/** SEAM for the RTTI cast at `0x10538764`/`0x1057c684` that `0x10381e90` performs on its grab
 *  target, plus the per-element `GetPosition` (`+0x94`) it reads through. `OutPositionUnits` is the
 *  named bone's WORLD position in SOURCE units. Answers false, which is retail's "the target is not
 *  that type" arm — and `FindPickupTargetGrabBone` then takes its recovered fallback. */
bool RagdollBonePosition(const FElysiumEntity* InTarget, const TCHAR* BoneName,
	FVector& OutPositionUnits) const;

/** SEAM for `thunk_FUN_1014f210(link, this, boneIndex, element, &zero, &zero)` — the wiring that
 *  binds a freshly created `phys_animlink` to a carrier bone and a carried physics element. Records
 *  nothing. */
void WirePhysAnimlink(const FElysiumEntityHandle& Link, int32 CarrierBone, int32 CarriedElement);

/** SEAM for `thunk_FUN_101cd970(link)` — the `UTIL_Remove` of the link entity both release bodies
 *  run before clearing `m_hPhysicsAnimlink`. */
void RemovePhysAnimlink(const FElysiumEntityHandle& Link);

/** SEAM for `0x102c4cc0`, the ballistic solve both release bodies run: given the launch point, the
 *  aim point and a Z speed already in `InOutImpulse.Z`, it rewrites X and Y so the arc lands on the
 *  aim point (gravity `sv_gravity` × `m_flGravity` (+0x3ec) × `_DAT_1044f030`, the negative-
 *  discriminant arm clamped to zero and a flight time at or below `_DAT_1049a1c8` answering a zero
 *  impulse). No projectile solver here. Leaves `InOutImpulse` untouched. */
void SolveThrowImpulse(const FVector& FromUnits, const FVector& ToUnits, FVector& InOutImpulse) const;

/** SEAM for the impulse application both release bodies end on: `CRagdollProp`'s vtable `+0x428`
 *  when the carried thing casts, else `IPhysicsObject::GetPosition` (`+0xa0`) followed by
 *  `ApplyForceCenter` (`+0x9c`) through the entity's `+0x36c`. Records nothing. */
void ApplyThrowImpulse(const FElysiumEntityHandle& Carried, const FVector& ImpulseUnits);

/** SEAM for `thunk_FUN_101578b0(ent)` / `thunk_FUN_101578d0(ent, b)` / `thunk_FUN_10157890(ent, b)`
 *  — the carried entity's "is it breakable" read and the two breakable/ragdoll latches the attach
 *  and release bodies set around a carry. The read answers false. */
bool IsCarriedBreakable(const FElysiumEntity* Carried) const;
void SetCarriedBreakable(const FElysiumEntityHandle& Carried, bool bBreakable);
void SetCarriedRagdollHeld(const FElysiumEntityHandle& Carried, bool bHeld);

/** The boss-side names for `0x102c4380` (`StartIgnoringCollision(other)` then
 *  `m_flIgnoreCollisionExpire` (+0x6458) = `FLT_MAX`) and `0x102c43b0` (renew that expiry at
 *  `curtime + Seconds` while an ignore is live). Both were seams here until family **TroikaHelpers**
 *  landed the bodies as `FUN_102c4380` / `FUN_102c43b0`; these two now forward to them, so the
 *  writes are real and `IgnoreCollisionEntity` (+0x055c) has its retail writer. */
void StartIgnoringCollision(const FElysiumEntityHandle& Other);
void ArmIgnoreCollisionExpiry(float Seconds);

/** The boss-side name for `0x10381c00`, which sets `m_bfAINPCFlags` `CARRYING_BODY` and, on the true
 *  arm, stamps `m_flFishTimer` (+0x666c) with `curtime + RandomFloat(5, 8)` and clears
 *  `m_bDidFakeThrow` (+0x667d). This was a seam here until family **Misc** landed the row as
 *  `FElysiumNpc::FormBit`; it now forwards to it, so the writes are real. The two counters stay
 *  because the Hengeyokai call sites below are asserted through them. `0x10381ba0` (`FINDING_BODY`)
 *  and `0x1038f600` (`CARRYING_BODY`, ManBat's) are no family's row and ARE written, because both
 *  are a single flag bit and nothing else. */
void CallFormBit(bool bSet);
int32 FormBitCalls = 0;        // how many times the seam was asked
bool bLastFormBitArm = false;  // and with which arm

/** SEAM for `0x10366400` — family **Species**' row over `CNPC_VBaseBoss::m_BlacklistedEntities`
 *  (+0x665c), the store MingXiao blacklists a thrown object in for 20 s. `0x10398b20` skips a
 *  pedestal that is still on it. There is no boss blacklist here; answers false, which admits every
 *  pedestal — the permissive arm, and stated as such. */
bool BossBlacklistHolds(const FElysiumEntity* Candidate) const;

/** SEAM for `0x102d1af0` with `(20000, 0, 15000.0, 0, 0)` — the hint search `0x1038b370` runs while
 *  it has `m_iMoveGoalNodeMode` temporarily forced to 2. Family **Hints** owns `FindHintNear` over
 *  the same absent store; this is its `FElysiumEntity*`-answering form, because `0x1038b370` reads
 *  the found hint's ORIGIN. Answers null, which is the arm that zeroes the output velocity. */
FElysiumEntity* ManBatFindMoveGoalHint(int32 HintType, float RadiusUnits);

/** SEAM for `thunk_FUN_102c41b0(this, "sheriff_teleport_emitter", &position)` — the named particle
 *  emitter `0x1038b370` places at the Sheriff's origin and then at the hint's. Recorded, in SOURCE
 *  units, so the test can read the pair back. */
struct FTeleportEmitterPlacement
{
	FString Name;
	FVector PositionUnits = FVector::ZeroVector;
};
TArray<FTeleportEmitterPlacement> TeleportEmitterPlacements;
void PlaceNamedEmitter(const TCHAR* Name, const FVector& PositionUnits);

/** SEAM for `thunk_FUN_101cf5c0(this, &position)` — the teleport `0x1038b370` performs once both
 *  emitters are placed. Recorded in SOURCE units; the entity is not moved, because the flight body
 *  is a velocity producer and moving the body from inside it would be a second answer to where this
 *  NPC is. */
bool bManBatTeleportRequested = false;
FVector ManBatTeleportPositionUnits = FVector::ZeroVector;

/** SEAM for `CBaseEntity::CalcAbsoluteVelocity` (the `+0x268 & 0x1000` dirty-velocity arm) and for
 *  `m_vecAbsVelocity` (+0x3bc), which `0x1038b370` reads as the animation-driven velocity. This
 *  runtime carries `FElysiumEntity::Velocity` in CENTIMETRES per second; this answers it in SOURCE
 *  units, which is what every constant in that body is in. */
FVector AbsVelocityUnits() const;

/** SEAM for `thunk_FUN_102f1a20(m_pNavigator, &position, 0x2400b)` — the navigator's "is this
 *  destination reachable" probe `0x1038b370` runs before it will chase a fly-by target, and for the
 *  two `m_Collision` reads (`+0x270` slots 4 and 8, the OBB mins and maxs) it offsets that position
 *  by. Family Motor states the same navigator gap. Answers false, which is retail's REFUSAL arm —
 *  `TaskFail(0x1a)` and the plain velocity fallback. */
bool NavigatorCanReach(const FVector& PositionUnits) const;

/** SEAM for `0x1038fb20`'s tail — `CBaseEntity::GetCollideable()` (`+0x8`), its collision group
 *  (`+0x38`), the `CTraceFilterSimple` built from the pair, and `enginetrace->SweepCollideable`
 *  (`+0x14`). The FILTER is the recovered concern and is ported as `ManBatTraceFilterShouldHit`
 *  below; the sweep itself records the call and nothing else. */
struct FPhysicsTraceEntityCall
{
	FElysiumEntityHandle Entity;
	FVector StartUnits = FVector::ZeroVector;
	FVector EndUnits = FVector::ZeroVector;
	uint32 Mask = 0;
};
TArray<FPhysicsTraceEntityCall> PhysicsTraceEntityCalls;

// --- The bodies ------------------------------------------------------------------------------------

/** `0x10381e90` — `CNPC_VHengeyokai`'s grab-bone search. Walks the fixed two-name bone table
 *  (`PTR_s_Bone01_1063bd88`: `"Bone01"`, `"Bone04"`, terminated by an empty string) on the grab
 *  target, keeps the bone whose world position is nearest this NPC's own `GetOrigin()` (slot 220)
 *  inside 1025 units, and writes it to `m_vecPickupTargetPos` / `m_iPickupTargetGrabBone`. When the
 *  target does not cast, it writes the target's own origin with bone 0 and answers true. */
bool FindPickupTargetGrabBone(const FElysiumEntity* InTarget);

/** The two bone names `FindPickupTargetGrabBone` walks, in retail's order, `nullptr`-terminated. */
static const TCHAR* const* PickupGrabBoneNames();

/** `0x103822a0` — `CNPC_VHengeyokai`'s facing gate on its grab target: the 2-D yaw of the vector
 *  from me to `Target` versus `GetAngles().y`, wrapped by `UTIL_AngleDiff`, must land inside
 *  `[-20, +20]` degrees. Answers TRUE when `Target` is null or `m_hPickupTarget` does not resolve —
 *  retail's own early-out, and the permissive one. */
bool FUN_103822a0(const FElysiumEntity* InTarget) const;

/** The pure rule behind it, so the cone can be measured without a world: retail's own
 *  `UTIL_AngleDiff(UTIL_VecToYaw(delta), yaw)` inside `[_DAT_1049ae98, _DAT_1044eb0c]`. `Delta` is
 *  in THIS world's axes and `YawDegrees` is Source's, exactly as family Facing's readers take them. */
static bool WithinPickupFacingCone(const FVector& Delta, float YawDegrees);

/** One row of the pickup species table: the class, the two retail bodies that fill the attach and
 *  release halves for it, the carrier bone the link is made on, the word the carried handle lives
 *  in, and the collision-ignore expiry the release arms. Checkable against
 *  `docs/vtmb/npc-kernel/layout.md`. */
struct FPickupSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* AttachBody = nullptr;
	const TCHAR* ReleaseBody = nullptr;
	const TCHAR* CarrierBone = nullptr;
	// `m_hPickupTarget`'s offset on this species — `+0x6664` Hengeyokai, `+0x668c` ManBat.
	int32 PickupTargetOffset = 0;
	// `0x102c43b0`'s argument on the release arm: 0.75 s Hengeyokai, 2.0 s ManBat.
	float IgnoreCollisionSeconds = 0.f;
	// The ManBat arm restores the carried thing's breakable latch after the throw; the Hengeyokai
	// arm does not, and instead clears `CARRYING_BODY` through family Misc's `FormBit`.
	bool bRestoresBreakable = false;
};
static const FPickupSpecies* PickupSpeciesRows(int32& OutCount);
static const FPickupSpecies* PickupSpeciesOf(const TCHAR* InRetailClass);
/** This NPC's row, walking the census chain, or null when neither boss claims it. */
const FPickupSpecies* PickupSpecies() const;

/** `0x10382670` (`CNPC_VHengeyokai`, `"Bip01 R Hand"`) and `0x1038f430` (`CNPC_VManBat`,
 *  `"Bip01_R_Foot"`) — ONE body written twice. Create a `phys_animlink`, find the carrier bone by
 *  name, ask the carried thing for the element at the species' element key, wire the two together
 *  and store the link at `m_hPhysicsAnimlink`. `Carried` is retail's `param_1`; `ElementKey` is its
 *  `param_2`, which the ManBat arm passes straight through and the Hengeyokai arm ignores in favour
 *  of the decoded `m_SecurePickupParam`. */
bool AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey);

/** The same body with the species row handed in rather than resolved, so a row whose classname this
 *  runtime registers no leaf for (`npc_VHengeyokai` and `npc_VManBat` are both unregistered) is
 *  still exercised by name. A null row is "no body fills this for my class" and answers false. */
bool AttachPickupAnimlinkFor(const FPickupSpecies* Row, FElysiumEntity* Carried, int32 ElementKey);

/** `0x10382400` (`CNPC_VHengeyokai`) and `0x1038f790` (`CNPC_VManBat`) — the release half, also one
 *  body written twice: drop the link, solve an impulse from the carried thing onto a target point
 *  48 units above the aim entity's origin, apply it, clear the carried handle and re-arm the
 *  collision-ignore expiry. `AimTarget` is the Hengeyokai arm's `param_1`; the ManBat arm ignores it
 *  and aims at `m_hClosestPlayer` (+0x628c) instead. */
void ReleasePickupAnimlink(const FElysiumEntity* AimTarget);

/** The row-explicit form, for the same reason as `AttachPickupAnimlinkFor`. */
void ReleasePickupAnimlinkFor(const FPickupSpecies* Row, const FElysiumEntity* AimTarget);

/** `0x10382970` — append `Entity` to `m_BlacklistedEntities` with an expiry of
 *  `curtime + _DAT_1044eb0c` (20 s). Retail's `CUtlVector` grow (4, then double, then by the grow
 *  size) and the zero-length `memmove` it always performs are bookkeeping with no observable effect
 *  and are not reproduced; the APPEND and the STAMP are. */
void AddBlacklistedEntity(const FElysiumEntity* Entity);

/** `0x10382b30` — the index of `Entity` in `m_BlacklistedEntities`, or `INDEX_NONE`. Retail resolves
 *  each stored `EHANDLE` and compares the POINTER, so a dead handle matches a null candidate. */
int32 FindBlacklistedEntity(const FElysiumEntity* Entity) const;

/** `0x10382aa0` — is `Entity` still blacklisted? Found and not yet expired answers true; found and
 *  expired swap-removes the row with the LAST one and answers false; not found answers false.
 *  Byte-for-byte the same body as `CNPC_VBaseBoss`'s `0x10366400` at `+0x665c`, which is family
 *  **Species**' row — the rule below is written once and both stores can use it. */
bool IsEntityBlacklisted(const FElysiumEntity* Entity);

/** The pure rule over any such store, so both species' arrays are measurable without a world.
 *  `Index` is what `FindBlacklistedEntity` answered. */
static bool BlacklistTestAndExpire(TArray<FBlacklistedEntity>& Store, int32 Index, double Now);

/** `0x103850a0` (`CNPC_VAndreiBlood`) and `0x10396e90` (`CNPC_VMingXiao`) — slot 482
 *  `CanPlaySequence(bool fDisregardState, int interruptLevel)`, written once per species and
 *  byte-identical to the base `0x10278090` and to `CNPC_VAnimal`'s `0x1035fd40` (family Species')
 *  and `CNPC_VTzimisce`'s `0x103bd270`. Answers retail's `CanPlaySequence_t`: 0 refuse, 1 yes,
 *  2 yes-and-a-cine-is-already-running. */
int32 CanPlaySequenceSpecies(bool bDisregardState, int32 InterruptLevel) const;

/** The pure state half, which is every arm after the cine test: `Result` is 1 or 2 on the way in.
 *
 *  THIS IS WHERE THE FOUR SPECIES BODIES DIVERGE FROM THE BASE. `0x10278090` (family **Anim**'s
 *  slot 482) answers a flat 0 once the gate holds; these four answer `((m_NPCState != 4) - 1)
 *  & result`, so a body in retail state 4 (`NPC_STATE_SCRIPT` — this image numbers 1 IDLE,
 *  2 COMBAT, 3 ALERT, 7 DEAD) keeps its 1-or-2 where the base refuses. Read from the listing at
 *  `0x10385159`; 29c's "byte-identical" walk is true of the four and not of the base. */
static int32 CanPlaySequenceStateArm(int32 Result, bool bDisregardState, int32 InterruptLevel,
	EElysiumNpcState State, EElysiumNpcState IdealState);

/** One row of slot 482's species table: the census class and the body that fills the slot for it. */
struct FCanPlaySequenceSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
};
static const FCanPlaySequenceSpecies* CanPlaySequenceSpeciesRows(int32& OutCount);
static const FCanPlaySequenceSpecies* CanPlaySequenceSpeciesOf(const TCHAR* InRetailClass);

// `thunk_FUN_101a8ac0(m_hCine)` and the `m_hCine` resolve in front of it are family **Anim**'s
// `CineAllowsDynamicInteraction()` and `ScriptOwnerIsLive()` (`ElysiumNpcKernelAnim.inl`), which
// landed in the same wave. `CanPlaySequenceSpecies` calls both rather than standing a second seam
// over the same read — the head of these four bodies is the head of the base body, and it has one
// answer.

/** `0x10385cf0` — slot 601's `CNPC_VAndreiBlood`-line body (38 species). It differs from the
 *  Troika line's `0x102b5880` in two recovered ways, and both are ported: it fires the global
 *  melee-left event `DAT_10924edc+4` FIRST, and it forwards to the coordinator WITHOUT the
 *  `m_pAttackCoordinator != 0` guard the Troika body puts in front of it. */
void FUN_10385cf0();

/** SEAM for the two global melee events: `DAT_10924edc`'s `+0x4` (fired by `0x10385cf0`, by slot
 *  599's `0x10385ab0` and by slot 600's `0x10385c30`) and the attack coordinator's own
 *  `thunk_FUN_1025ddd0(m_pAttackCoordinator, this)` release. Neither has a home in this substrate —
 *  `AttackCoordinator` is a bare index of three globals — so both record the call. */
int32 MeleeEventFires = 0;
int32 MeleeCoordinatorReleases = 0;

/** `0x1038b370` — `CNPC_VManBat`'s velocity producer, the biggest body in this family (2,274 bytes).
 *  Three shapes, chosen by `m_pFlyNode` and the decoded `m_iMoveGoalNodeMode`: the animation-driven
 *  velocity steered by the obstacle probe; the fly-node / fly-by-target homing with its 700 and 500
 *  unit speeds, its acceleration clamp and its overspeed latch on `m_bReachedMoveGoal`; and the
 *  `sheriff_teleport_emitter` teleport the stationary watchdog fires. `Interval` is retail's
 *  `param_2` and `OutVelocityUnits` its `param_1`, both SOURCE units. */
void FUN_1038b370(float Interval, FVector& OutVelocityUnits);

/** `0x1038b370`'s stationary watchdog, which is a FILE-STATIC triple (`_DAT_1093b8b8..c0` position,
 *  `_DAT_1093b8c4` stamp) and therefore ONE watchdog shared by every ManBat in the level, not one
 *  per NPC. Ported as such — the static is retail's own and a per-NPC copy would be a divergence. */
struct FManBatStationaryWatch
{
	FVector PositionUnits = FVector::ZeroVector;
	double SinceTime = 0.0;
};
static FManBatStationaryWatch& ManBatStationaryWatch();
static void ResetManBatStationaryWatch();

/** `0x1038bec0` — the hull probe `0x1038b370` steers by. Sweeps this NPC's own collision box from
 *  `GetAbsOrigin()` along `DirUnits * Speed * 0.1` with mask `0x202400b`; a fraction under 1.0
 *  answers the straight-up steer `(0, 0, 1)` and, unless `m_Activity` (+0x0fec) is already `0x22`,
 *  stamps `m_flFlapTimer` with `curtime`; a clear sweep answers `vec3_origin` and false. */
bool FUN_1038bec0(const FVector& DirUnits, float Speed, FVector& OutSteerUnits);

/** `0x1038e640`, `0x1038e670`, `0x1038e6a0` and `0x1038e6e0` — four bodies that are one behaviour:
 *  `SetIdealActivity(act)` then `m_flFlapTimer = curtime + T`. The table below is the whole of the
 *  difference between them. */
void SetFlapActivity(int32 InActivityNumber, float Seconds);

struct FFlapActivity
{
	const TCHAR* Body = nullptr;
	int32 Activity = 0;
	float Seconds = 0.f;
};
static const FFlapActivity* FlapActivityRows(int32& OutCount);
static const FFlapActivity* FlapActivityOf(const TCHAR* Body);

/** `0x1038fb20` — `CNPC_VManBat`'s slot 102 `Physics_TraceEntity`. The recovered concern is the
 *  FILTER: the body builds the ordinary `CTraceFilterSimple` and then overwrites its vtable pointer
 *  with `vftable_CTraceFilterManBatNoIBeamEntity` before sweeping. Slot 102's Troika-line body
 *  (`0x100ab450`) is another story's, so this lands as a named method. */
void PhysicsTraceEntityManBat(FElysiumEntity* Entity, const FVector& StartUnits,
	const FVector& EndUnits, uint32 Mask);

/** `CTraceFilterManBatNoIBeamEntity::ShouldHitEntity` (`0x10006c4e`), the whole of it: an entity
 *  whose `m_iName` (+0x26c) matches the wildcard name `"lbeam*"` is NOT hit; anything else falls
 *  through to `CTraceFilterSimple::ShouldHitEntity`. The match is retail's `NameMatches` — a
 *  trailing `*` makes it a case-insensitive prefix compare of the characters before it, otherwise a
 *  whole-string case-insensitive compare — and a null name compares as the empty string. */
static bool ManBatTraceFilterShouldHit(const FString& EntityName);

/** Retail's `NameMatches` idiom as `0x10006c4e` inlines it. Exposed because the wildcard is the
 *  filter's whole content and a test has to reach it. */
static bool RetailNameMatches(const FString& EntityName, const TCHAR* NameOrWildcard);

/** `0x10396dc0` — `CNPC_VMingXiao::SelectSchedule`'s grabbed-object arm. With a live
 *  `m_hThrowObject` and `m_eThrowableObjectMode` strictly inside `(2, 5)`, condition `0x7b` answers
 *  schedule `0x15c` and condition `0x7c` answers `0x15d`; anything else answers 0. Retail also
 *  writes its own `__FILE__`/`__LINE__` (`"E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_…"`, lines 0xbe1
 *  and 0xbe5) into the selector trace at `+0x1b30`/`+0x1b34`; that pair is `_ABSENT` in this
 *  runtime's shape map, whose mind transition trace carries the same account. */
int32 FUN_10396dc0() const;

/** `0x10397a50` — `CNPC_VMingXiao::Event_Killed`'s tentacle arm. Stamps `m_flProxyReadyTimer` with
 *  `curtime + max(Tuning[100] + Tuning[0x68] * (6 - m_iConnectedTentacleCount), 0)` and, when the
 *  dead tentacle is still the registered proxy for its own `m_iTentacleID`, severs it through
 *  `0x10397930`. */
void FUN_10397a50(const FElysiumEntity* Tentacle, TFunctionRef<float(int32)> TuningField);

/** SEAM for `0x10397930`, the sever: `m_rhSeveredTentacles[id] = -1`, `m_rbProxyRegistered[id] = 0`,
 *  `m_rhProxies[id] = -1`, `m_rflHitPoints[id] = Tuning[4]`, `m_rflAttackTimers[id] = curtime +
 *  _DAT_1044e664`, `m_rflRegrowTimers[id] = curtime + 1.0` and a bodygroup set. `m_rhProxies` and
 *  `m_rhSeveredTentacles` are family Squad's members; `0x10397930` is no family's row, so the two
 *  writes this substrate CAN make are made and the rest is recorded. */
void SeverTentacle(int32 TentacleId);
int32 LastSeveredTentacle = INDEX_NONE;

/** `0x10398000` — `(m_iSeveredTentacleMask & (1 << n)) == 0`, which every MingXiao body spells as
 *  "tentacle n is still there". Not a row of this family; two lines, and three of this family's
 *  bodies gate on it, so it is written rather than seamed. */
bool IsTentacleConnected(int32 TentacleId) const;

/** `0x10398870` — `m_iTentacleID != -1`, retail's "this MingXiao is a proxy, not the head". */
bool IsMingXiaoProxy() const;

/** `0x10397f70` — `CNPC_VMingXiao::Spawn`/`StartTask`'s rate pick: a proxy answers `Tuning[0x20]`
 *  flat; the head answers `max(Tuning[0x6c] + Tuning[0x70] * (6 - m_iConnectedTentacleCount), 0)`. */
float FUN_10397f70(TFunctionRef<float(int32)> TuningField) const;

/** `0x10398030` — the six-way tentacle attack gate `SelectSchedule` and `GatherConditions` share.
 *  `Slot` is 0..5. Answers whether the slot may attack; `OutSchedule` carries retail's schedule id
 *  (`0x112a`..`0x112d` for slots 0–3, none for 4 and 5). `bTestMelee` is retail's `param_2`, which
 *  adds the `m_hMeleeWeapon` / slot 331 `ChooseMeleeAttackSequence` test on top. */
bool FUN_10398030(int32 Slot, bool bTestMelee, int32& OutSchedule);

/** SEAM for `0x10398030`'s `param_2` tail: `m_hMeleeWeapon`'s owner (`+0xa0`), its activity through
 *  the weapon's `+0x5a4`, this NPC's slot 376 `NPC_TranslateActivity`, `GetEnemy()`'s `+0x9c`, and
 *  slot 331 `ChooseMeleeAttackSequence` — whose Troika body is story 29d's. Answers false, which is
 *  retail's REFUSAL arm, so the melee-tested form of the gate never opens. */
bool ChooseMeleeAttackSequenceSeam() const;

/** `0x103983d0` — `CNPC_VMingXiao::RunTask`'s throw-force curve, selected by `m_eThrowingTentacle`
 *  (0..5). Recovered from the LISTING (`vtmb_asm 0x103983d0`): the decompiled C lost the jump table
 *  and the ST0 return. Slots 0–3 answer `max(Tuning[0x74] + Tuning[0x78] * (6 - count), 0) * Scale`
 *  with `Scale` picked by the closest-player distance; slots 4–5 answer
 *  `max(First + Second * (6 - count), 0)` with no second factor; anything else answers 20.0. */
float FUN_103983d0(int32 Selector, TFunctionRef<float(int32)> TuningField) const;

/** `0x103989b0` — which tentacle should take a pedestal that is abeam of me. The pure rule, so it
 *  can be measured without a world: the target must be inside 64 units of my own height, the
 *  NORMALIZED 2-D direction to it must land in `[-0.17, 0.5]` of `Forward`, and its side against
 *  `Right` picks tentacle 5 (at or left of centre) or tentacle 4. `Delta` is target minus me in
 *  SOURCE units and in THIS world's axes, as `Forward`/`Right` are. */
static bool PedestalTaskForSide(const FVector& DeltaUnits, const FVector& Forward,
	const FVector& Right, bool bTentacle4Connected, bool bTentacle5Connected, int32& OutTask);

/** The body itself, reading `m_vecForward` (+0x6290) and `m_vecRight` (+0x629c). Both are retail's
 *  cached basis and NOTHING in this runtime writes them, so the member form takes its miss arm
 *  until a sense pass fills them; the pure form above is the recovered rule. */
bool FUN_103989b0(const FVector& TargetOriginUnits, int32& OutTask) const;

/** `0x10398b20` — MingXiao's pedestal pick. Only with `m_flClosestPlayerDistance` at or past 150
 *  units, at least one of tentacles 4 and 5 connected, AND the species cvar `DAT_1093ba8c` reading
 *  non-zero: walk every entity within 257 units, skipping any the boss blacklist still holds, keep
 *  those whose `m_iName` starts with `"Pedestal"` (eight characters, case-insensitive) and whose
 *  velocity is within 0.1 of zero on all three axes, ask `FUN_103989b0` which tentacle takes it, and
 *  keep the nearest winner — the search radius shrinking to each winner's distance as retail's does.
 *  Answers the chosen entity, having written the aim point and forward through `0x10398890`. */
FElysiumEntity* FUN_10398b20(int32& InOutTask, FVector& OutAimPointUnits, FVector& OutForward);

/** The `DAT_1093ba8c` cvar gate `0x10398b20` puts in front of its whole search
 *  (`!cvar->vfunc1() && cvar->m_nValue != 0`, retail's inlined `ConVar::GetInt`):
 *  `ming_xiao_pickup`, shipped "1" — the search runs. */
int32 MingXiaoPedestalCvar() const;

/** The search itself, behind the cvar gate, so the recovered rule is measurable. `RadiusUnits` is
 *  retail's 257.0 starting radius, which shrinks to each accepted winner's distance. */
FElysiumEntity* FindNearestPedestal(float RadiusUnits, int32& OutTask);

/** The `DAT_1093b7cc` cvar `0x1038b370` multiplies by the think interval to get its per-axis
 *  acceleration clamp (retail's inlined `ConVar::GetFloat`): `manbat_delta`, shipped "600.0". */
float ManBatAccelerationCvar() const;

/** SEAM for `0x10398890` — the aim point `0x10398b20` hands its caller: the pedestal's origin pushed
 *  `_DAT_10463584` along `m_vecForward` and `_DAT_1049ae40` along `m_vecRight`, added for task 4 and
 *  subtracted otherwise, with `m_vecForward` copied out beside it. Both cells live past `.data`'s
 *  raw size and are filled at runtime, so their values are **unrecovered**; the two SIGNS and the
 *  forward copy are the recovered half and are ported. */
void MingXiaoPedestalAimPoint(const FVector& PedestalOriginUnits, int32 Task,
	FVector& OutAimPointUnits, FVector& OutForward) const;

/** SEAM for `CBaseEntity::GetVelocity(&vel, &angvel)` (slot 199), which `0x10398b20` uses to require
 *  a pedestal be stationary. `FElysiumEntity::Velocity` is CENTIMETRES per second; this answers
 *  SOURCE units, which is what the 0.1 tolerance is in. */
FVector EntityVelocityUnits(const FElysiumEntity& Entity) const;
