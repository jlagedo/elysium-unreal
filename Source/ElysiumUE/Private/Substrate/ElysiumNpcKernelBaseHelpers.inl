// Story 29c-1, family **BaseHelpers** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelBaseHelpers.cpp` and the tests in
// `Tests/ElysiumNpcKernelBaseHelpersTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// THIS FAMILY IS `CAI_BaseNPC`'S OWN UNNAMED LAYER — the `0x1025…`–`0x1029…` bodies 29a's naming
// pass could not name. Two things follow from that and are worth stating once:
//
//   * **The `CAI_BaseHumanoid` / `CAI_BaseActor` rows** (`0x1025e780`, `0x1025f1a0`,
//     `0x1025ea00`, `0x10260540`, `0x10260670`/`0x10260750`) and the `CAI_BaseActor` words they
//     read were deleted by 0019 story 5 step 1: the class has no instance (`population.md`). The
//     census keeps them.
//   * **The SDK twin settles four names on the base line.** Those carry the name; everything whose
//     concern did not settle keeps its `FUN_<address>` spelling, because inventing a name for an
//     unrecovered concern is the guess `CLAUDE.md` forbids.

// --- `CAI_BaseNPC::m_UnreachableEnts` (`+0x5d48`, count `+0x5d54`) --------------------------------
//
// NOT declared here. Family **Positions** already stands the list as `FUnreachableEntity` /
// `UnreachableEnts` (`ElysiumNpcPositions.inl`) for `IsUnreachable`'s sweep, which is the
// READ side of this family's `RememberUnreachable` (`0x10274080`) — the write side. One list, two
// bodies, and re-declaring it would have been the drift this story exists to end.

// --- Seams ----------------------------------------------------------------------------------------
//
// Each answers NOTHING and names the retail call it stands for. Nothing below invents a value.

/** `GetActiveWeapon()->+0x8c0` (`m_fMaxRange1`), the ACTIVE WEAPON's own maximum range in SOURCE
 *  units (`ElysiumWeapons::ItemRangeWords`, 0018 story 8). Read by `0x10296c40`'s distance band,
 *  slot 609's shoot-at radius, `GatherEnemyConditions`' too-far limit and the StartTask radius
 *  arms. False only when there is no active weapon. */
bool ActiveWeaponMaxRangeUnits(float& OutRangeUnits) const;

/** The patrol node's record: the hint the network node `PatrolNode` holds
 *  (`node+0xa0`, `FElysiumPlaceSet::AttachedHint`), as its entity index. Its `+0x46c` is the
 *  `ip_percent` chance `0x1029f650` rolls against, its pointer what `0x1029f730` caches at `+0x659c`
 *  and its `+0x468` the place name `0x1029f780` resolves. `INDEX_NONE` (retail's 0) for a -1 id, a
 *  node with no hint, or an id outside the network (which bumps `DAT_106c994c`). */
int32 PatrolNodeInterestRecord(int32 PatrolNode) const;

/** That record's `+0x46c m_iIPPercent` (`FHintWords::IpPercent`). 0 — the chance that never fires —
 *  for an index that names no live hint. */
int32 PatrolNodeInterestPercent(int32 Record) const;

/** `0x102968f0`, the hint LOS check `0x10295ed0` tails into (target: the cover object) and
 *  `0x10296c40` (`ValidateHintCoverRange`) runs under `m_bForceCoverLOSCheck`: a line FROM the
 *  hint's position for this NPC (`0x102d1180`, raised by this NPC's collision maxs z) TO `Target`'s eye, mask `0x46804099`, no character blocking.
 *  Either argument null → false; clear (`fraction >= 1`, neither solid flag) → true. Writes
 *  nothing. With no embodiment or no collision world it answers the PASS arm. */
bool HintLosCheck(int32 HintNode, const FElysiumEntity* LosTarget) const;

/** `DAT_10925444`, the `ai_debug_npc` handle every reason-string arm of `0x102961a0` and
 *  `0x10296c40` (and slot 566's hint-group arm) compares against `this` before formatting anything:
 *  the world's `AiDebugNpc` resolves (serial match, non-null slot) to THIS NPC. Set by the
 *  `elysium.ai_debug_npc` verb; unset (retail's `-1`) for every NPC otherwise, so the strings are
 *  never built. `0x102961a0`'s REASONS are decided and returned either way (`EHintRejectReason`
 *  below), because the reason is what the walk recovered and a test has to be able to read it. */
bool IsHintDebugNpc() const;

/** `0x102d0ab0(hint, reason)` under the `ai_debug_npc` gate, the port's reading: retail copies the
 *  reason into the hint's debug text (`CAI_Hint +0x478`); the port logs it (`LogElysiumNpcEnt`,
 *  Display, prefixed with this NPC's `DebugString()` and the hint's index and name) and records it in
 *  the world's `AiDebugHintProbe` beside the numbers already written there. An EMPTY reason is the
 *  passing tail's `0x102d0b20` (the clear): recorded, not logged. Callers gate on `IsHintDebugNpc()`
 *  themselves, so no string is formatted otherwise; this body re-checks and does nothing when unset. */
void HintDebugNote(const FHintWords& Hint, const TCHAR* Validator, const FString& Reason) const;

// --- The rejection reasons the verbose cover validator decides ------------------------------------
//
// `0x102961a0` ends every failing arm that has a string by formatting it onto the hint
// (`0x102d0ab0`); its pass arms return without a clear (`10296651`, re-read 0018 story 8 — it is
// `0x10296c40` whose passing tail calls `0x102d0b20`). The STRING is a debug artefact
// gated on `ai_debug_npc` and is formatted, verbatim with retail's operands, through
// `HintDebugNote` only under `IsHintDebugNpc()`; the REASON is the recovered decision, so the body
// answers it either way. (`0x10296c40`'s own reasons went with the duplicate this family carried;
// family Hints' `ValidateHintCoverRange` answers a bool and formats its strings the same way.)
enum class EHintRejectReason : uint8
{
	None = 0,            // the hint passed
	NoHint,              // a null node, or the seam could not resolve it (no string)
	Disabled,            // m_iDisabled != 0 (no string)
	TargetNameMismatch,  // "Target name mismatch (%s)"
	NoCoverObject,       // "No cover object"
	DistanceOutOfBand,   // "Distance (%d) < %d or > %d"
	OutsideGoodRange,    // "Enemy outside of good range (%.2f) <= %.2f"
	FailedLos,           // "Failed LOS check (%s)"
};

// --- The bodies -----------------------------------------------------------------------------------
//
// `CAI_BaseNPC`'s own.

/** `0x1028ebc0` — can I see this point? `FInViewCone`, then a squared-distance test against
 *  `m_flVisionDistance` (`+0x63b8`), then a clear trace. Retail name UNRECOVERED and the corpus
 *  records no caller for it, so the spelling stays the address. */
bool FUN_1028ebc0(const FVector& PointCm) const;

/** `IsThinkDue` (`0x10290660`) over the three named clocks `0x102906a0` (`m_flNextUpdateTime`
 *  `+0x6244`), `0x102906c0` (`m_flNextNormalTime` `+0x6248`) and `0x10290700` (`m_flNextAITime`
 *  `+0x6250`). Each retail body is a 13-byte forward and nothing else; the NAME of each forward is
 *  unrecovered but its CONCERN is settled by the clock it names, so they are spelled by clock. */
bool IsUpdateThinkDue() const;
bool IsNormalThinkDue() const;
bool IsAiThinkDue() const;

struct FPatrolPathCell;   // `m_sppPatrolPath`'s cell, defined by family Script19 (`ElysiumNpcScript.inl`)

/** `0x1029f610` — the patrol path's network check `CAI_BaseNPCTroika::OnRestore` (`0x102998c0`) runs
 *  on `m_sppPatrolPath` and `m_sppPatrolPathHunt`: a cell holding a path answers `0x10307ac0(path,
 *  m_pNavigator->+0x2c)` -- every node id it holds names a node of the network (an id past the
 *  count bumps `DAT_106c994c`); an empty cell answers false. Retail name UNRECOVERED. (The 29c walk
 *  read the argument as an `AILocalMoveGoal_t*`; `0x102998c0`'s two calls hand it the cells.) */
bool FUN_1029f610(const FPatrolPathCell* Cell) const;

/** `0x1029f650` — reset `m_bPatrolPathUseHint` (`+0x65a0`), then roll `Random(0, 99)` against the
 *  patrol node's interesting-place `ip_percent` and set the flag when the roll comes in under it.
 *  Retail name UNRECOVERED; `docs/vtmb/npc-ai/programs.md` reads the flag as the patrol route's
 *  "visit the interesting place at this node" draw. Returns the flag, as retail does. */
bool FUN_1029f650(int32 PatrolNode);

/** `0x1029f730` — the cached read side of the draw above: nothing unless `+0x65a0` stands, then the
 *  record cached at `+0x659c`, resolved on first ask. Retail name UNRECOVERED. */
int32 FUN_1029f730(int32 PatrolNode);

/** `0x10295ed0` — is this cover hint still a valid place to stand relative to `m_hHintCoverObject`?
 *  The quiet twin of `0x102961a0`: the rule below, then `0x102968f0` (`HintLosCheck`) with the
 *  cover object as its target for every hint but the current one. Retail name UNRECOVERED.
 *  (`0x10296c40`, the attack-position validator, is family Hints' `ValidateHintCoverRange`.) */
bool FUN_10295ed0(int32 HintNode) const;

/** The pure rule of `0x10295ed0` over a hint's words, so every threshold is assertable without a
 *  hint store. `CoverObjectCm` is where `m_hHintCoverObject` is standing, `bIsCurrentHint` is
 *  `hint == m_pHintNode`, `MyOriginCm` is `GetAbsOrigin()`. The facing is `0x102d12e0`'s — a
 *  node-bound hint (`NodeId != -1`) is judged by its NETWORK NODE's yaw, read off this NPC's
 *  world — through `0x101d2f40` in Source axes (the port's Y negated). */
bool CoverHintStillValid(const FHintWords& Hint, const FVector& CoverObjectCm,
	const FVector& MyOriginCm, bool bIsCurrentHint) const;

/** `0x102961a0` — the verbose twin of `0x10295ed0`: the same band and projection over the cover
 *  object, with a `target_name` gate in front and its own inline LOS ray (me, raised by my collision
 *  maxs z, to `0x102d1180`; mask `0x46804099`) instead of `0x102968f0`, and a named reason on every
 *  rejection. Retail name UNRECOVERED. */
EHintRejectReason FUN_102961a0(int32 HintNode) const;

/** The pure rule of `0x102961a0`, minus the LOS ray; the same facing frame as `CoverHintStillValid`. */
EHintRejectReason CoverHintRejectReason(const FHintWords& Hint, const FVector& CoverObjectCm,
	bool bIsCurrentHint) const;

/** `0x10297a20` — pick a turn-in-place program from the motor's yaw delta and record the pick in
 *  `m_eFaceAnim` (`+0x63e4`) and `m_flFaceYawDiff` (`+0x63e8`). Retail name UNRECOVERED. A THIRD
 *  turn ladder beside the two family Facing already carries (`TurnActivityBaseLadder` `0x10289d10`,
 *  `TurnActivityTroikaLadder` `0x10297640`): different activities, different thresholds, and it
 *  writes two words neither of those touches. */
void FUN_10297a20();

/** The pure rule of `0x10297a20`, as a pick over the yaw delta. */
struct FFaceAnimPick
{
	int32 Activity = 1;     // the retail activity the body makes ideal; 1 is the ACT_IDLE tail
	int32 FaceAnim = 0;     // +0x63e4 m_eFaceAnim — 8 / 6 / 3 / 1 / 0
	bool bRandomDuration = false;  // the three upper rungs draw +0x63e8; the lower two copy the delta
};
static FFaceAnimPick FaceAnimLadder(float YawDelta, TFunctionRef<bool(int32)> HasSequence);

/** `CBaseAnimating::GetBonePosition01(name, &pos, &ang)` (`0x1000f263`), read for a value by the
 *  RunTask, StartTask and Werewolf anim-event rules. **SEAM**: no skeleton is readable from the
 *  kernel surface; answers false. Re-homed from the deleted Debug10 family (0019 story 6); the body
 *  is in `ElysiumNpcGeometry.cpp`. */
bool RetailBonePosition(const TCHAR* BoneName, FVector& OutPositionUnits,
	FVector& OutAnglesDegrees) const;
