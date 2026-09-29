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

/** SEAM for `CAI_Hint` `+0x8c0`… no: `GetActiveWeapon()->+0x8c0`, the ACTIVE WEAPON's own maximum
 *  range in SOURCE units, which `0x10296c40`'s distance band uses as its upper bound alongside the
 *  hint's `m_flTargetDistMax`. No port weapon record carries a range, so this answers false and the
 *  weapon half of that `||` cannot be evaluated; the hint half still is, and the body says so. */
bool ActiveWeaponMaxRangeUnits(float& OutRangeUnits) const;

/** `0x1029f6c0` — the patrol node's record: the hint the network node `PatrolNode` holds
 *  (`node+0xa0`, `FElysiumPlaceSet::AttachedHint`), as its entity index. Its `+0x46c` is the
 *  `ip_percent` chance `0x1029f650` rolls against, its pointer what `0x1029f730` caches at `+0x659c`
 *  and its `+0x468` the place name `0x1029f780` resolves. `INDEX_NONE` (retail's 0) for a -1 id, a
 *  node with no hint, or an id outside the network (which bumps `DAT_106c994c`). */
int32 PatrolNodeInterestRecord(int32 PatrolNode) const;

/** That record's `+0x46c m_iIPPercent` (`FHintWords::IpPercent`). 0 — the chance that never fires —
 *  for an index that names no live hint. */
int32 PatrolNodeInterestPercent(int32 Record) const;

/** SEAM for `0x102968f0`, the hint LOS check `0x10295ed0` tails into and `0x10296c40` runs under
 *  `m_bForceCoverLOSCheck`. Takes the hint being validated and the entity being covered from.
 *  Answers true, which is retail's PASS arm — the trace seam behind it reports a clear line. */
bool HintLosCheck(int32 HintNode, const FElysiumEntity* Target) const;

/** SEAM for `DAT_10925444`, the `ai_debug_npc` handle every reason-string arm of `0x102961a0` and
 *  `0x10296c40` compares against `this` before formatting anything. Answers false, so the reason
 *  strings are never built — which is retail's own behaviour for every NPC but the one being
 *  debugged. The REASONS are still decided and returned (`FHintRejectReason` below), because the
 *  reason is what the walk recovered and a test has to be able to read it. */
bool IsHintDebugNpc() const;

// --- The rejection reasons the two verbose validators decide --------------------------------------
//
// `0x102961a0` and `0x10296c40` each end every failing arm by formatting a named string onto the
// hint (`0x102d0ab0`) and a passing one by clearing it (`0x102d0b20`). The STRING is a debug
// artefact gated on `ai_debug_npc`; the REASON is the recovered decision, so the bodies answer it
// and the strings are reproduced verbatim beside each arm.
enum class EHintRejectReason : uint8
{
	None = 0,            // the hint passed
	NoHint,              // a null node, or the seam could not resolve it
	Disabled,            // "Disabled"                          — m_iDisabled != 0
	TargetNameMismatch,  // "Target name mismatch (%s)"         — 0x102961a0 only
	NoCoverObject,       // "No cover object"                   — 0x102961a0 only
	NoActiveWeapon,      // "No active weapon"                  — 0x10296c40 only
	HeightDiff,          // "Height diff (%d) > %d"             — 0x10296c40 only
	DistanceBelowMin,    // "Distance (%d) < %d"                — 0x10296c40
	DistanceOutOfBand,   // "Distance (%d) < %d or > %d"        — 0x102961a0
	DistanceAboveMax,    // "Distance (%d) > %d or %d"          — 0x10296c40
	Projection,          // "Projection (%.2f) < 0.2"           — 0x10296c40 only
	OutsideGoodRange,    // "Enemy outside of good range (%.2f) <= %.2f"
	InsideBadRange,      // "Enemy inside of bad range (%.2f) >= %.2f"  — 0x10296c40 only
	FailedLos,           // "Failed LOS check (%s)" / "Failed hint LOS"
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
 *  The quiet twin of `0x102961a0`. Retail name UNRECOVERED. */
bool FUN_10295ed0(int32 HintNode) const;

/** The pure rule of `0x10295ed0` over a hint's words, so every threshold is assertable without a
 *  hint store. `CoverObjectCm` is where `m_hHintCoverObject` is standing, `bIsCurrentHint` is
 *  `hint == m_pHintNode`, `MyOriginCm` is `GetAbsOrigin()`. */
bool CoverHintStillValid(const FHintWords& Hint, const FVector& CoverObjectCm,
	const FVector& MyOriginCm, bool bIsCurrentHint) const;

/** `0x102961a0` — the verbose twin of `0x10295ed0`: the same band and projection over the cover
 *  object, with a `target_name` gate in front and its own inline LOS ray instead of `0x102968f0`,
 *  and a named reason on every rejection. Retail name UNRECOVERED. */
EHintRejectReason FUN_102961a0(int32 HintNode) const;

/** The pure rule of `0x102961a0`, minus the LOS ray (which the trace seam answers). */
EHintRejectReason CoverHintRejectReason(const FHintWords& Hint, const FVector& CoverObjectCm,
	bool bIsCurrentHint) const;

/** `0x10296c40` — is this hint a valid place to attack `Enemy` from, given my active weapon?
 *  Retail name UNRECOVERED. Family Hints stands `ValidateHintCoverRange` as a SEAM naming this
 *  address and deliberately left it refusing; this is the recovered body, and the two should be
 *  joined by the coordinator rather than by an edit to another family's file. */
EHintRejectReason FUN_10296c40(int32 HintNode, const FElysiumEntity* Enemy, float GoodRangeDot,
	float BadRangeDot) const;

/** The pure rule of `0x10296c40`, minus the hint-LOS seam. `EnemyCm` is the enemy's origin,
 *  `MyOriginCm` mine, `bHasActiveWeapon` is `GetActiveWeapon() != NULL`. */
EHintRejectReason AttackHintRejectReason(const FHintWords& Hint, const FVector& EnemyCm,
	const FVector& MyOriginCm, bool bIsCurrentHint, bool bHasActiveWeapon, float GoodRangeDot,
	float BadRangeDot) const;

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

