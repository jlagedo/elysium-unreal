// Story 29c-1, family **Positions** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelPositions.cpp` and
// `Substrate/ElysiumNpcKernelPositions2.cpp`, and the tests in
// `Tests/ElysiumNpcKernelPositionsTests.cpp`.
//
// The family is the tactical-position validators and the node selectors: eight boss-species node
// pickers over retail's global hint list, four `PositionClearForTeleport` variants, the Chang
// brothers' arena sector rule, the unreachable-entity cache, `IsValidCover` /
// `IsValidShootPosition` / `IsAreaClear` / `EnemyCouldSeeHull`, the eight fills of slot 563, and
// the Werewolf teleport pair.
//
// THE STANDING FACTS OF THIS FAMILY, which every body below runs into:
//
//   * **There is no node graph.** Retail's selectors walk the global `CAI_Hint` list `DAT_10925450`
//     (link `node+0x5d8`, type `node+0x5dc`). Family **Hints** stood the seam for one hint's words
//     (`FHintWords` / `HintWords()`) and family **Motor** the seam for the list itself
//     (`NavAllHintNodes`); both answer nothing, so every selector here answers "no node" — retail's
//     own answer on a map that authors none. This family adds no third store. Instead each selector
//     is split: a PURE rule over a candidate list, which is where the recovered decision lives and
//     what the test drives, plus a thin member entry point that feeds it from the two seams.
//   * **There is no NPC hull table.** `PTR_DAT_1060a750`'s records live past `.data`'s raw size in
//     the pinned image — they are filled at runtime and are not readable from the file — so family
//     Motor's `RetailHullExtents` answers nothing and the hull box is the zero box.
//   * **The world trace goes through family Motor's `KernelHullTrace`**, the kernel-tier seam for
//     `(*DAT_1070b254)->TraceRay`. It carries a fraction and a hit entity and NOT retail's
//     `startsolid`/`allsolid` pair, so the two start-solid arms below read its "no answer" as
//     not-solid, exactly as its own comment says every caller must.
//
// Retail distances here are **Source units** and this world is centimetres; `ElysiumMove::U` is the
// 2.54 that bridges them, applied at the point of use so the recovered constant stays visible.

// --- Words this family needed that 29b did not declare ------------------------------------------
//
// All but the first are a boss species' own words, past the hand-written shape map's band
// (`ElysiumNpcKernelShapeMap.cpp` binds `0x1a40`..`0x665a`). Several offsets are reused by
// DIFFERENT classes for different facts — `+0x66e8` is the Sheriff's centre floor height AND the
// Changs' `m_bCenterStored` byte AND (as family Hints' `WerewolfHintFlags`) the Werewolf's hint-gate
// word — so each member is named for the class that owns the offset, and one offset carrying more
// than one member is retail's own leaf-local reuse rather than a mistake.

// +0x5d48 `m_UnreachableEnts` / +0x5d54 its count — `CAI_BaseNPC::IsUnreachable`'s cache, one 0x14
// byte record per entry. The shape map's row for `+0x5d48` still reads ABSENT ("UnreachableEnt_t has
// no port counterpart"); this family gave it one, so that row wants replacing with a WORD row.
struct FUnreachableEntity
{
	FElysiumEntityHandle Entity;                 // +0x00
	double ExpiresAt = 0.0;                      // +0x04, an absolute curtime, carried as double
	FVector PositionCm = FVector::ZeroVector;    // +0x08..+0x10, where it stood when it was recorded
};
TArray<FUnreachableEntity> UnreachableEnts;

// +0x0fec `m_Activity` — the activity number `CNPC_VTzimisce`'s slot 389 override switches on. Below
// the shape map's band, so 29b did not bind it; this runtime names activities and carries retail's
// registered number beside them, exactly as family Facing's `IdealActivityNumber` (+0x0ff0) does.
int32 ActivityNumber = 0;

// --- The node selectors -------------------------------------------------------------------------
//
// Eight bodies, one shape: resolve the closest player, walk `DAT_10925450`, filter on the hint
// type, score, keep the best. Each is a PURE rule over a candidate list plus the two positions that
// score it, so every threshold is assertable without a node graph; the member entry points beside
// them feed the rule from the hint seams and answer `INDEX_NONE` today.
//
// Retail's hint-type words, exactly as the selectors spell them: 17000 and `0x4269` (17001) are
// `CNPC_VAndreiBlood`'s teleport nodes, `0x3e82` (16002) the Sabbat leader's archways, `0x3e85`
// (16005) its dive points, 18000 the Chang brothers' teleport nodes, `0x4651` (18001) the Sheriff's
// centre, `0x4652` (18002) a jumpbase and `0x4653` (18003) a ledge.
//
// `Positions` is the walked-list view every pure selector takes. Indices are into it; `INDEX_NONE`
// is retail's null node.

/** One scored candidate of the two teleport selectors that keep a score rather than a distance. */
struct FTeleportNodePick
{
	int32 Index = INDEX_NONE;
	float Score = 0.f;
};

/** The candidate list the entry points above build: every node the hint seams can resolve.
 *  `NavAllHintNodes` + `HintWords`, both of which answer nothing, so this is empty. */
void GatherHintNodes(TArray<FHintWords>& OutNodes, TArray<int32>& OutNodeIds) const;

// --- `PositionClearForTeleport`, four species ---------------------------------------------------
//
// One retail name, four different rules, none of them a vtable slot: each class's selector calls
// its own directly, so there is no dispatcher (story 5 step 3). Family **Motor** declared
// `PositionClearForTeleport(const FVector&, float) const` as a SEAM answering false, for
// `SelectJumpbaseNode`'s use.

/** `thunk_FUN_10315a80(m_pSquad, pos, clearance)` — the squad-wide "is this spot already claimed"
 *  query `CNPC_VChangBros::PositionClearForTeleport` asks before it walks the members itself.
 *  **SEAM**: `ConnectedSquad()` answers nothing here, so the arm is never reached; declared so it
 *  is spelled rather than dropped. */
bool SquadPositionTaken(const FVector& PositionCm, float ClearanceCm) const;

/** `thunk_FUN_103160a0` / `thunk_FUN_103160c0` — the squad's member count and its Nth member, which
 *  the same body walks asking each brother for its own `GetTeleportPosition`. **SEAM**: answers an
 *  empty squad. */
void SquadMembers(TArray<FElysiumNpc*>& OutMembers) const;

// --- Pure geometry ------------------------------------------------------------------------------

// `CNPC_VVampireBoss::DistToSegment` `0x103c6b70` is declared by family **Hints**
// (`ElysiumNpcKernelHints.inl`), which owns its other caller, `CheckJumpPathToHintNode`. Story
// 29c-1 briefly carried two bodies for it: Hints' first pass answered `|P - A|` for a degenerate
// segment where retail answers `0.0`, and this family landed a second verbatim copy rather than
// patch a sibling's file mid-wave. Hints then corrected the degenerate arm — and established that
// the other suspected divergence was not one, because Ghidra's `(t < 0) == (t == 0)` is `t > 0` and
// the three arms are arithmetically the clamped form — so the two collapsed onto the one body.
// `FElysiumNpcChangBros::DistToSegment` is that body; this family calls it with centimetres, Hints with
// source units, and the answer is in whichever the caller passed.

// --- The world-trace bodies ---------------------------------------------------------------------

/** `CAI_BaseNPCTroika::IsAreaClear` `0x102a0fb0` — the NPC's own collision hull swept from `FromCm`
 *  to its origin against `Mask`, with `m_bForceNPCCheck` (+0x63da) raised for the duration. */
bool IsAreaClear(const FVector& FromCm, int32 Mask);

/** The three sight candidates `EnemyCouldSeeHull` builds out of its box, in retail's order: the box
 *  CENTRE with a random Z drawn between the box's own min and max, then the min corner, then the max
 *  corner. Pure, so the blend is assertable without a trace. */
struct FEnemySightCandidates
{
	FVector MidCm = FVector::ZeroVector;
	FVector MinCm = FVector::ZeroVector;
	FVector MaxCm = FVector::ZeroVector;
};

// --- Slot 563's species half --------------------------------------------------------------------
//
// Eight bodies fill `TranslateEnemyChasePosition`. The generated slot carries the Troika line's
// (`0x10295300`); the other seven are the base line's (`0x10289f20`, which differs by ONE write),
// the empty camera one, the two identical species stubs, and the three that add a goal-tolerance
// arm. `NavGetType()` (family Motor's `FUN_1027d990`) is the gate all eight share. The six species
// bodies are their classes' overrides (story 5 step 3).

enum class EChaseTranslateShape : uint8
{
	/** `CAI_BaseNPC` `0x10289f20`: nav 2 offsets and writes the hull width; ELSE zeroes the
	 *  tolerance. The only body of the eight with an else arm that writes. */
	Base,
	/** `CAI_BaseNPCTroika` `0x10295300`: the same, with NO else arm. */
	Troika,
	/** `CNPC_VAnimal` `0x1035f5c0` and `CNPC_VHuman` `0x10384760`: the offset alone, no hull width. */
	OffsetOnly,
	/** `CNPC_VCamera` `0x10368ee0`: `return;`, all four arguments ignored. */
	Empty,
	/** `CNPC_VMingXiao` `0x10392c40` and `CNPC_VTzimisce` `0x103ba640`: the offset, else the goal
	 *  tolerance plus BOTH lead helpers. */
	GoalToleranceLead,
	/** `CNPC_VWerewolf` `0x103d9e00`: the offset, else the goal tolerance plus a `ConVar` term and
	 *  only the FIRST lead helper. */
	GoalToleranceWerewolfLead,
};
/** Slot 563 in the shape `Shape` names — the body the species overrides share (story 5 step 3). */
void TranslateEnemyChasePositionAs(EChaseTranslateShape Shape, FElysiumEntity* Enemy,
	FVector& InOutChasePositionCm, float& InOutTolerance, float& InOutSecondTolerance);
/** The same, over the generated slot's `void*` tolerance pair. */
void TranslateEnemyChasePositionShaped(EChaseTranslateShape Shape, FElysiumEntity* Enemy,
	FVector& InOutChasePositionCm, void* InTolerance, void* InSecondTolerance);

/** `pEnemy->vtable[0x304]()` — slot 193 `EyePosition` on the ENEMY. The chase position is nudged by
 *  the delta between that and the enemy's own origin, so an NPC chases the enemy's EYE. */
static FVector EnemyChaseAnchor(const FElysiumEntity& Enemy);

/** `thunk_FUN_102c3b50` and `thunk_FUN_102c36d0`, the two target-lead helpers `CNPC_VMingXiao`,
 *  `CNPC_VTzimisce` and `CNPC_VWerewolf` call on the non-navigating arm. Neither body is in this
 *  family's rows and neither has a port counterpart. **SEAM**: both leave their outputs alone. */
void ChaseLeadTolerance(FElysiumEntity* Enemy, const FVector& ChasePositionCm,
	float& InOutTolerance) const;
void ChaseLeadPosition(FElysiumEntity* Enemy, const FVector& VelocityCm, float GroundSpeed,
	const FVector& ChasePositionCm, FVector& OutPositionCm) const;

/** `m_flGroundSpeed` (+0x0654) and `GetLocalVelocity()` (slot 220, vtable `+0x370`), the two inputs
 *  of the lead. **SEAM**: no motor publishes either; both answer nothing. */
float GroundSpeedCm() const;
FVector LocalVelocityCm() const;

/** The `ConVar` `CNPC_VWerewolf`'s slot 563 adds to the goal tolerance — `DAT_1093d52c`
 *  `werewolf_translated_enemy_position_tolerance`, read as `IsCommand() ? 0.0f : m_fValue` (+0x28);
 *  shipped "0". */
static float WerewolfChaseToleranceConVar();

// --- The Werewolf teleport pair -----------------------------------------------------------------

/** `m_fEffects` (+0x019c) bit `0x20` — `EF_NODRAW`. `TeleportOut` raises it and `TeleportIn` clears
 *  it. This runtime has no effects word at the kernel tier; it is carried here because both bodies
 *  write it and the pairing is the recovered behaviour. */
uint32 EffectsWord = 0;

// --- What is left of `FUN_102c5570` -------------------------------------------------------------
//
// 29c's overlay names this row `FElysiumNpc::IsNearShootTarget`. That name is 29c's own INFERENCE
// and the listing does not support it: the body returns a FLOAT in `ST0`, has no caller anywhere in
// the image, and its tail multiplies by two fields of a pointer argument (`+0x260`, `+0x26c`) whose
// owning class is **unrecovered** — they are not `CBaseEntity`'s `m_pMoveChild` / `m_iName`, which
// is what those offsets are on an entity. What IS recovered is the falloff it computes, and it is
// ported as that under a name that says so.
float ShootTargetFalloff(float RangeBase, float RangeDivisor, float Value) const;

/** The delta half of the same body: the shoot target's position minus this body's origin, from the
 *  `m_hShootTargetOverride` (+0x5ba8) arm or from `GetEnemies()->GetLastKnownPosition(GetEnemy())`.
 *  False when neither resolves, which is the arm that skips the divide entirely. */
bool ShootTargetDelta(FVector& OutDeltaCm) const;
/** `GetEnemies()` (slot 541) then `thunk_FUN_102dfed0` — the enemy's last known position. */
bool EnemyLastKnownPosition(FVector& OutPositionCm) const;

// --- Slot 389's `CNPC_VTzimisce` override -------------------------------------------------------

/** `CBaseAnimating::ComputeHitboxSurroundingBox` for `m_nSequence` (+0x6f0) — the box
 *  `EnemyCouldSeeHull`'s hitbox arm uses instead of the hull one. **SEAM**: the animating tier
 *  exposes no hitbox set to the kernel, so this answers false and the body takes retail's own
 *  "no sequence description" arm, which is the hull box. */
bool ComputeHitboxSurroundingBox(FVector& OutMinsCm, FVector& OutMaxsCm) const;
/** `m_Collision`'s solid-flag word (+0x02b4) — `TeleportOut` ORs in `0x4` (`FSOLID_NOT_SOLID`) and
 *  `TeleportIn` masks it out. Carried for the same reason. */
uint32 SolidFlagsWord = 0;
/** `thunk_FUN_102f41b0(m_pNavigator->GetNetwork(), &pos)` — the navigator's nearest-node query.
 *  **SEAM**: no node graph, so this answers retail's own miss value `-1` and the caller takes its
 *  `DevWarning` arm. */
int32 NavNearestNodeTo(const FVector& PositionCm) const;
