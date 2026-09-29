// Story 29c-1, family **Bosses** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcBosses.cpp` and the tests in
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

/** One row of a `CUtlVector<BlacklistedEntity_t>` — the 8-byte `{EHANDLE, float expiry}` pair
 *  `CNPC_VBaseBoss` keeps at `+0x665c` and `CNPC_VHengeyokai` repeats at `+0x66a4`. */
struct FBlacklistedEntity
{
	FElysiumEntityHandle Entity;
	double ExpiresAt = 0.0;    // an absolute curtime stamp, carried as double
};

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

/** SEAM for `thunk_FUN_102c41b0(this, "sheriff_teleport_emitter", &position)` — the named particle
 *  emitter `0x1038b370` places at the Sheriff's origin and then at the hint's. Recorded, in SOURCE
 *  units, so the test can read the pair back. */
struct FTeleportEmitterPlacement
{
	FString Name;
	FVector PositionUnits = FVector::ZeroVector;
};

// --- The bodies ------------------------------------------------------------------------------------

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

/** `0x103850a0` (`CNPC_VHuman`, indexed under `CNPC_VAndreiBlood`) and `0x10396e90` (`CNPC_VMingXiao`)
 *  — slot 482 `CanPlaySequence(bool fDisregardState, int interruptLevel)`, written once per species,
 *  as are `CNPC_VAnimal`'s `0x1035fd40` and `CNPC_VTzimisce`'s `0x103bd270`. They are standalone
 *  copies that NEVER call the base `0x10278090`, and they diverge from it in the SCRIPT state (4),
 *  where they keep their answer (see `CanPlaySequenceStateArm`); story 5 step 3 made them their
 *  classes' overrides. Answers retail's `CanPlaySequence_t`: 0 refuse, 1 yes, 2
 *  yes-and-a-cine-is-already-running. */
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


// `thunk_FUN_101a8ac0(m_hCine)` and the `m_hCine` resolve in front of it are family **Anim**'s
// `CineAllowsDynamicInteraction()` and `ScriptOwnerIsLive()` (`ElysiumNpcAnim.inl`), which
// landed in the same wave. `CanPlaySequenceSpecies` calls both rather than standing a second seam
// over the same read — the head of these four bodies is the head of the base body, and it has one
// answer.

/** Two counters. `MeleeEventFires` records `(*DAT_10924edc)->IsCommand()` (`0x10385cf0`, slot 599's
 *  `0x10385ab0`, slot 600's `0x10385c30`): `DAT_10924edc` is `ent_trace_melee`'s parent pointer and
 *  the call is the `IsCommand()` half of a `GetFloat()` whose value nothing uses (read 2026-09-29) —
 *  no event, no observable, counted only so the call order is testable. `MeleeCoordinatorReleases`
 *  records the attack coordinator's `thunk_FUN_1025ddd0(m_pAttackCoordinator, this)` release, which
 *  has no home here — `AttackCoordinator` is a bare index of three globals. */
int32 MeleeEventFires = 0;
int32 MeleeCoordinatorReleases = 0;

/** `0x1038b370`'s stationary watchdog, which is a FILE-STATIC triple (`_DAT_1093b8b8..c0` position,
 *  `_DAT_1093b8c4` stamp) and therefore ONE watchdog shared by every ManBat in the level, not one
 *  per NPC. Ported as such — the static is retail's own and a per-NPC copy would be a divergence. */
struct FManBatStationaryWatch
{
	FVector PositionUnits = FVector::ZeroVector;
	double SinceTime = 0.0;
};
static void ResetManBatStationaryWatch();

struct FFlapActivity
{
	const TCHAR* Body = nullptr;
	int32 Activity = 0;
	float Seconds = 0.f;
};

/** `CTraceFilterManBatNoIBeamEntity::ShouldHitEntity` (`0x10006c4e`), the whole of it: an entity
 *  whose `m_iName` (+0x26c) matches the wildcard name `"lbeam*"` is NOT hit; anything else falls
 *  through to `CTraceFilterSimple::ShouldHitEntity`. The match is retail's `NameMatches` — a
 *  trailing `*` makes it a case-insensitive prefix compare of the characters before it, otherwise a
 *  whole-string case-insensitive compare — and a null name compares as the empty string. */
static bool ManBatTraceFilterShouldHit(const FString& EntityName);

/** Retail's `NameMatches` idiom as `0x10006c4e` inlines it. Exposed because the wildcard is the
 *  filter's whole content and a test has to reach it. */
static bool RetailNameMatches(const FString& EntityName, const TCHAR* NameOrWildcard);

