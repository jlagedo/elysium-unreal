// Story 29c-1, family **Hints** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelHints.cpp` and the tests in
// `Tests/ElysiumNpcKernelHintsTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.

// --- The hint seam --------------------------------------------------------------------------------
//
// THERE IS NO HINT NODE IN THIS SUBSTRATE. Retail's `CAI_Hint` is an ENTITY on the global hint list
// `DAT_10925450` (next link `+0x5d8`, rotating search cursor `DAT_10925454`), and every NPC word
// that names one — `m_pHintNode` (`+0x5ddc`), `m_pShootAtHint` (`+0x6444`), the Werewolf's move and
// teleport hints — is a `CAI_Hint*`. This runtime carries those as BARE INDICES
// (`FElysiumNpcScheduleHost::HintNode`), there is no hint store to index into, and no AI node graph
// under it.
//
// So this family stands the seam rather than the store: `FHintWords` is the typed view of a hint's
// own datamap words (`vtmb_fields CAI_Hint`), `HintWords()` is the one query that fills it, and the
// rules over those words are ported as PURE functions that a test can drive with a hand-built
// `FHintWords`. Every entry point that takes a node index asks the seam first and answers retail's
// null arm when it comes back empty. Nothing here invents a hint.
//
// Family **Squad** built `NthHintOfType` (`ElysiumNpcKernelSquad.inl`) over the same absent global
// list, walked by ordinal and answering an `FElysiumEntity*`. It is left exactly as it is: the two
// are the same missing store seen from two sides, and the day a hint store lands it replaces both.

/**
 * One `CAI_Hint`'s own words, by retail offset and datamap name (`vtmb_fields CAI_Hint`).
 *
 * This is a VIEW, not a store: `HintWords()` fills it and nothing keeps it. `bValid` is false when
 * the seam could not resolve the node, which today is always.
 */
struct FHintWords
{
	bool bValid = false;
	FString Name;                     // +0x026c m_iName                 key targetname
	FString Activity;                 // +0x0450 m_strActivity
	float TargetAngleRange = 0.f;     // +0x0454 m_flTargetAngleRange    key target_angle_range
	float TargetAngleRangeDot = 0.f;  // +0x0458 m_flTargetAngleRangeDot
	float TargetDistMin = 0.f;        // +0x045c m_flTargetDistMin       key target_dist_min
	float TargetDistMax = 0.f;        // +0x0460 m_flTargetDistMax       key target_dist_max
	float HintRating = 0.f;           // +0x0464 m_flHintRating          key hint_rating
	FString TargetName;               // +0x0468 m_strTargetName         key target_name
	int32 IpPercent = 0;              // +0x046c m_iIPPercent            key ip_percent
	// +0x0470 m_iGroupID, key group_id — the 32-BIT SET `FValidateHintType` (`0x10295c20`) ANDs
	// against `FElysiumNpcScheduleHost::HintGroupMask` (`m_iHintGroups +0x62e4`), NOT a plain id.
	int32 GroupMask = 0;
	int32 HintType = 0;               // +0x05dc m_nHintType             key HintType
	FElysiumEntityHandle HintOwner;   // +0x05e0 m_hHintOwner
	int32 NodeId = INDEX_NONE;        // +0x05e4 m_nNodeID — the AI-network node, not a hint index
	int32 Disabled = 0;               // +0x05e8 m_iDisabled             key StartHintDisabled
	double NextUseTime = 0.0;         // +0x05ec m_flNextUseTime
	FString Group;                    // +0x05f0 m_strGroup              key Group
	// Retail reads these through the entity vtable, `+0x364 GetAbsOrigin` and `+0x36c GetAbsAngles`.
	// The origin is in CENTIMETRES here, as every port position is; retail's is Source units.
	FVector OriginCm = FVector::ZeroVector;
	FVector Angles = FVector::ZeroVector;
};

/** SEAM. Resolve `HintNode` (a `ScheduleHost::HintNode`-shaped index) into its words. Answers false
 *  and leaves `Out` untouched: retail's `CAI_Hint` entities have no store here. */
bool HintWords(int32 HintNode, FHintWords& Out) const;

/** SEAM for `0x102d1af0` — the task-side hint search `FindHintNode` (`0x10365780`) runs, with the
 *  hint type, the caller's flag byte and a radius in SOURCE UNITS. Answers `INDEX_NONE`. */
int32 FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits) const;

/** SEAM for `0x102d24b0` — the same search anchored on another entity, which is what the two-group
 *  pick `SelectTzimisceHintNode` (`0x103bfa50`) calls twice. Retail walks the global list from the
 *  rotating cursor, skipping any node `IsHintUnusable` rejects, requiring `m_nHintType == HintType`
 *  (or `HintType == 0` for any), a squared-distance test against `RadiusUnits`, and slot 566
 *  `FValidateHintType` (vtable `+0x8d8`). Answers `INDEX_NONE`. */
int32 FindHintOfTypeNear(const FElysiumEntity* Near, int32 HintType, uint8 SearchFlags,
	float RadiusUnits) const;

/** SEAM for `CGlobalEntityList::FindEntityByName` + the `CAI_Hint` RTTI cast that
 *  `FindHintEndEntity` (`0x103d6520`) performs. Answers `INDEX_NONE`. */
int32 FindHintByName(const FString& HintName) const;

/** `0x102d1420`, the hint release: `m_hHintOwner (+0x5e0) = -1` and
 *  `m_flNextUseTime (+0x5ec) = ReuseDelaySeconds + curtime`. SEAM on the write side only — the rule
 *  is exact, but there is no hint to write it into. */
void ReleaseHintNode(int32 HintNode, float ReuseDelaySeconds);

/** `0x102d14c0` — is this hint unusable right now? Three arms, in retail's order: `m_iDisabled`
 *  non-zero; `curtime < m_flNextUseTime`; a live `m_hHintOwner`. Pure over the words, so this is the
 *  whole recovered rule and not a seam. `bOwnerAlive` is the `EHANDLE` validity test retail runs on
 *  `m_hHintOwner`. */
static bool IsHintUnusable(const FHintWords& Hint, double Now, bool bOwnerAlive);

/** The node-index form of the above. Answers true — an unresolvable hint is not usable. */
bool IsHintUnusable(int32 HintNode, double Now) const;

/** SEAM for `0x10296c40`, the shared range/LOS/cover validator both `IsHintCoverValid` bodies
 *  forward into. `0x10296c40` is NOT this story's row (layer 11) and is not ported here; this
 *  answers false and names it. */
bool ValidateHintCoverRange(const FHintWords& Hint, const FElysiumEntity* CoverObject,
	float AngleRangeDot, float BadRangeLimit) const;

/** SEAM for `RestartIdealActivity` (`0x10289ee0`), which every hint body calls with a retail
 *  `Activity` enum id. This runtime's activity vocabulary is NAMES (`FElysiumClipIdentity`) and
 *  carries no retail-id table — the same reason 29b left `m_IdealTranslatedActivity` (`+0x5cd0`) an
 *  opaque `int32`. The ported bodies therefore DECIDE the id (that half is tested) and hand it here,
 *  which records nothing. */
void RestartIdealActivityId(int32 RetailActivityId);

/** What `CAI_Hint::OnRestore` (slot 130, `0x102d3ec0`) did. `bNodeFound` false is retail's
 *  `"Warning: AI hint has incorrect origin"` arm — see the definition. */
struct FHintRestoreResult
{
	bool bNodeFound = false;
	bool bClaimedNode = false;
	FVector NodeOriginCm = FVector::ZeroVector;
};

/** `CAI_Hint::OnRestore` (`0x102d3ec0`). The hint's own save-restore fixup, handed over from family
 *  Sounds. `this` is the hint, not the NPC, so it is a static helper rather than a member rule. */
static FHintRestoreResult HintOnRestore(const FHintWords& Hint);

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the species words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These are the ones this family's bodies read, declared by retail
// name with the species class that owns the offset, exactly as family Squad declared its four.

int32 TeleportHintNode = INDEX_NONE;  // +0x66b0 CNPC_VWerewolf::m_pTeleportHint (walked)
int32 MoveHintNode = INDEX_NONE;      // +0x66bc CNPC_VWerewolf::m_pMoveHint (walked)
bool bRandomHint = false;             // +0x66c8 CNPC_VWerewolf::m_bRandomHint (walked)
int32 WerewolfDoorState = 0;          // +0x6680 CNPC_VWerewolf::m_DoorState (walked)
uint32 WerewolfHintFlags = 0;         // +0x66e8 CNPC_VWerewolf, the hint-gate bit word (walked)
//
// `+0x66b8 m_vLastJumpPosition[2]` and `+0x66d0 m_iLastJumpPositionIdx` — the ring
// `AddHintToStoredJumpPositions` writes — are declared by family **Motor**
// (`ElysiumNpcKernelMotor.inl`) for its reader `IsPosNearStoredJumpPositions` (`0x103618a0`), in
// SOURCE UNITS. This family writes that same pair rather than standing a second copy.

/** One row of the Werewolf's authored hint-groundpoint array — `+0x6714` the base, `+0x6720` the
 *  count, stride `0x48`, the hint pointer at `+0x00` and the groundpoint at `+0x08`. The array is
 *  filled by no ported producer, so it is empty and `GetHintGroundpoint` always takes its miss arm,
 *  which is the recovered fallback and not a refusal. SOURCE UNITS, as every retail position word
 *  here is and as family Motor's `GetGroundpoint` takes and answers. */
struct FWerewolfHintGroundpoint
{
	int32 HintNode = INDEX_NONE;
	FVector GroundpointUnits = FVector::ZeroVector;
};
TArray<FWerewolfHintGroundpoint> WerewolfHintGroundpoints;  // +0x6714 / +0x6720

// --- Slot 566 `FValidateHintType`: the species half -----------------------------------------------
//
// THE SLOT ITSELF IS NOT THIS STORY'S. Slot 566's Troika-line body (`0x10295c20`, layer 11) is
// story 29d's and the generator still emits its stub, so `FElysiumNpc::FValidateHintType(void*)` is
// taken. What 29c-1 owns is the SPECIES half — five bodies with a real per-species rule and six
// with a constant — and it lands under the name below until 29d's body can route to it.
//
// Every species body reads the hint's `m_nHintType` (`+0x5dc`) and nothing else. The base body is
// the one that reads `m_iGroupID` (`+0x470`) and ANDs it against `m_iHintGroups` (`+0x62e4`) before
// switching on the type, so the group gate is 29d's and `HintGroupMask` stays unread by this family.

enum class EHintTypeRule : uint8
{
	/** Retail's whole body is `return 1;` — every hint type is accepted. */
	AlwaysTrue,
	/** Retail's whole body is `return 0;`. */
	AlwaysFalse,
	/** `m_nHintType == Lo`. */
	Equals,
	/** `Lo <= m_nHintType <= Hi`. */
	InRange,
	/** `Lo <= m_nHintType <= Hi && m_nHintType != Except`. */
	InRangeExcept,
};

/** One row of retail's slot-566 species table: the census class, the body that fills the slot for
 *  it (checkable against `docs/vtmb/npc-kernel/slots.md`), and the rule that body is. */
struct FHintTypeSpecies
{
	const TCHAR* RetailClass = nullptr;
	const TCHAR* Body = nullptr;
	EHintTypeRule Rule = EHintTypeRule::AlwaysTrue;
	int32 Lo = 0;
	int32 Hi = 0;
	int32 Except = 0;
	/** Retail's body dereferences the hint without a null check. Only `CNPC_VTzimisce` tests it. */
	bool bNullChecks = false;
};

/** The table: 11 rows. `CNPC_VBach` (`0x10365800`) and `CNPC_VManBat` (`0x1038e480`) also override
 *  slot 566 but chain into the base body / build a name from a random draw, so both are 29d's and
 *  are deliberately absent rather than guessed at. */
static const FHintTypeSpecies* HintTypeSpeciesRows(int32& OutCount);

/** The row for a retail class name, walking no base chain — a species with no row of its own runs
 *  the base body, which is 29d's. Null when the table carries no row for it. */
static const FHintTypeSpecies* HintTypeSpeciesOf(const TCHAR* InRetailClass);

/** The rule applied. A null row is "no species override", which this family cannot answer for. */
static bool FValidateHintTypeSpecies(const FHintTypeSpecies* Row, int32 HintType);

/** This NPC's row applied to a hint node — the entry point the slot will call. False when the seam
 *  cannot resolve the node, and false when no row carries this species. */
bool FValidateHintTypeForSpecies(int32 HintNode) const;

// --- Slot 567 `GetHintActivity`: the species half -------------------------------------------------
//
// Same shape as 566: slot 567's Troika-line body (`0x1026a8f0`) is a `rule` row 29c already landed
// as `return 1`, so `FElysiumNpc::GetHintActivity(int16)` is taken by the generator. `CNPC_Crow`
// (`0x10358c90`) is the one species override and lands here.

/** `CNPC_Crow::GetHintActivity` (`0x10358c90`), the rule: hint type 700 answers activity `0x22`,
 *  anything else falls through to `BaseAnswer`, which is the Troika line's `1`. */
static int32 CrowHintActivity(int16 HintType, int32 BaseAnswer);

/** The dispatch: `CrowHintActivity` for a `CNPC_Crow`, slot 567's own body for anything else. */
int32 GetHintActivitySpecies(int16 HintType) const;

// --- The bodies -----------------------------------------------------------------------------------

/** `CNPC_VAsianVampire::AddHintToStoredJumpPositions` (`0x10361990`) — push the hint's origin into
 *  the two-slot ring at `m_vLastJumpPosition`, then advance and wrap `m_iLastJumpPositionIdx`. */
void AddHintToStoredJumpPositions(const FHintWords& Hint);

/** `CNPC_VChangBros::CheckJumpPathToHintNode` (`0x1036df50`) — may this brother jump to the hint
 *  without crossing the closest player, or the other brother, or a sector-4 endpoint? */
bool CheckJumpPathToHintNode(const FHintWords& Hint) const;

/** `CNPC_VVampireBoss::DistToSegment` (`0x103c6b70`) — the point-to-segment distance
 *  `CheckJumpPathToHintNode` tests against `_DAT_104ada34`. Verbatim, including the degenerate arm,
 *  which answers `0.0` and NOT the distance to the endpoint. */
static float DistToSegment(const FVector& A, const FVector& B, const FVector& P);

/** SEAM for `CNPC_VChangBros::GetSector(pos)` — the map-authored sector index the jump-path check
 *  compares against 4. No sector partition exists on this substrate. Answers 0, which is "not
 *  sector 4" and so does not block a jump retail would have allowed. */
int32 JumpPathSector(const FVector& PositionCm) const;

/** SEAM for the entity vtable `+0x370` position accessor `SelectTzimisceHintNode` (`0x103bfa50`)
 *  compares through — NOT `+0x364 GetAbsOrigin`, which the rest of this family uses. Slot 220 is
 *  unidentified in the census, so this answers the entity's origin and says that is a stand-in. */
FVector HintComparePosition(const FElysiumEntity* Entity) const;

/** SEAM for `m_Activity` (`+0xfec`), the currently playing retail `Activity` id that
 *  `RunInterestingPlaceLoop` compares its refreshed id against. Answers -1: this runtime's activity
 *  is an `FElysiumClipIdentity` name pair and carries no retail id. */
int32 CurrentRetailActivityId() const;

/** `0x102a9f40` — "the wait" every `TASK_DO_INTEREST_*` shares: claim the marker, fire
 *  `OnInterestingPlaceArrived`, take the INTO arm or the idle arm, and stamp `m_flWaitFinished`. */
void ClaimInterestingPlace(FElysiumInterestingPlace* Place, bool bClaimSecondary, double Now);

/** `CNPC_VWerewolf::ClearMoveHint` (`0x103d4690`). */
void ClearMoveHint();

/** `CNPC_VWerewolf::ClearTeleportHint` (`0x103d4760`). */
void ClearTeleportHint();

/** `CNPC_VVampireBoss::DistToHintCenterLine2D_3` (`0x103c6680`) — the squared-then-rooted distance
 *  from `Point` to the line through `LineStart` along `LineDir`. */
static float DistToHintCenterLine2D_3(const FVector& LineStart, const FVector& LineDir,
	const FVector& Point);

/** `CNPC_VVampireBoss::DistToHintCenterLine2D_2` (`0x103c6570`) — the same, with the line taken from
 *  the hint's own origin and facing, flattened to 2D. */
static float DistToHintCenterLine2D(const FHintWords& Hint, const FVector& PointCm);

/** `CNPC_VWerewolf::FindHintEndEntity` (`0x103d6520`) — follow the hint's `m_strTargetName` to
 *  another hint, then one further unchecked hop from that hint's own target name. */
int32 FindHintEndEntity(const FHintWords& Hint) const;

/** `0x10365780` — the task-side hint install: search within 5000 units, install at
 *  `ScheduleHost.HintNode` and complete the task, or write the fail text and `TaskFail(4)`. */
bool FindHintNode(int32 HintType, uint8 SearchFlags);

/** `0x103bfa50` — `CNPC_VTzimisce`'s two-group hint pick (14000 vs 14001 within 200 units, nearer
 *  wins, loser released with a 0.5 s reuse delay). NAMED for what it does: the generic
 *  `FindHintNode` above is a different behaviour that happens to share 29c's target name. */
int32 SelectTzimisceHintNode(const FElysiumEntity* Anchor);

/** SEAM for `0x103bfc20`, the usability check `SelectTzimisceHintNode` applies to its winner before
 *  choosing between the two schedule ids. Answers false. */
bool IsTzimisceHintUsable(int32 HintNode, const FElysiumEntity* Anchor) const;

/** `CNPC_VWerewolf::GetHintGroundpoint` (`0x103d6770`) — the authored groundpoint for a hint, or
 *  retail's `DevWarning` plus the plain `GetGroundpoint` fallback. SOURCE UNITS, because family
 *  Motor's `GetGroundpoint` is and because the fallback can answer `vec3_invalid`, a SENTINEL that
 *  no unit conversion may be applied to. */
FVector GetHintGroundpoint(const FHintWords& Hint) const;

/** Is `Value` retail's `vec3_invalid` — `DAT_10713de0/de4/de8`, which `staticinit_101371a0` fills
 *  with `0x7f7fffff` (`FLT_MAX`)? `GetGroundpoint`'s no-hit answer, which `PositionAtHint` has to
 *  recognise. */
static bool IsVec3Invalid(const FVector& Value);

// `CNPC_VWerewolf::GetGroundpoint` (`0x103d6a40`), the fallback `GetHintGroundpoint` ends at, is
// family **Motor**'s body and is declared in `ElysiumNpcKernelMotor.inl`. It takes and answers
// SOURCE UNITS.

/** `CNPC_VWerewolf::GetHintTeleportPriority` (`0x103d3220`) — the hint-type to priority map. */
static int32 GetHintTeleportPriority(int32 HintType);

/** `0x10297430` — resolve `m_hHintCoverObject` (`+0x6448`) and forward into the cover validator
 *  with the hint's `m_flTargetAngleRangeDot` and retail's `0.731`. */
bool IsHintCoverValid(int32 HintNode) const;

/** `0x102974f0` — the same forward with `1.1` and no handle-validity pre-check. */
bool IsHintCoverValidLoose(int32 HintNode) const;

/** `CNPC_VWerewolf::IsImperativeTeleportHint` (`0x103d3360`) — the authored-name ladder that says a
 *  teleport hint must be taken. */
bool IsImperativeTeleportHint(const FHintWords& Hint) const;

/** SEAM for the Werewolf's vtable `+0x9a4` trace (slot 617), the last gate of the
 *  `jump_to_platform` arm. Answers true — retail's "trace was blocked" answer, which is the arm that
 *  does NOT call the hint imperative. */
bool WerewolfHintTrace(const FVector& PositionCm) const;

/** `CNPC_VWerewolf::IsValidBreakHint` (`0x103d8550`). */
bool IsValidBreakHint(const FHintWords& Hint, double Now) const;

/** `0x102781e0` — `SetHintGroup(string_t)`: write `m_strHintGroup` (`+0x5db0`) and, only on a real
 *  change, dispatch slot 551 `OnChangeHintGroup(old, new)`. NAMED `SetHintGroup`, not 29c's
 *  `OnChangeHintGroup`: that name is slot 551 itself, which this body CALLS. */
void SetHintGroup(const FString& NewHintGroup);

/** `0x102aaa60` — the hint-node idle activity restart. Returns whether an activity was restarted. */
bool PlayHintIdleActivity(double Now);

/** SEAM for `0x102b5de0`, the gate `PlayHintIdleActivity` puts in front of each of its three hint
 *  types: a shoot-target/enemy LOS test through the engine trace. Answers true, which is retail's
 *  "the gate passed" answer and the arm that restarts the activity. */
bool HintIdleActivityGate() const;

/** `CNPC_VWerewolf::PositionAtHint` (`0x103d6280`) — snap to the hint's groundpoint and facing. */
void PositionAtHint(const FHintWords& Hint);

/** `0x1029f780` — the cached patrol-node interest-place resolve at `+0x6300`. */
int32 ResolvePatrolInterestPlace(int32 PatrolNode);

/** SEAM for `0x1029f730` — the patrol node's interest record, whose `+0x468` names the entity the
 *  resolve looks up. There is no patrol-node graph here. Answers false. */
bool PatrolNodeInterestRecordName(int32 PatrolNode, FString& OutName) const;

/** `0x102aa210` — the INTO → IDLE → OUTOF interest-place loop. Returns whether the caller's task
 *  is finished, which is the byte retail leaves in `AL`. */
bool RunInterestingPlaceLoop(FElysiumInterestingPlace* Place, double Now);

/** SEAM for `ActivityNameToId` (`0x10412520`), the interest-place activity resolve. This runtime's
 *  activities are NAMES, so there is no retail id to answer: it answers -1, which is retail's own
 *  "not in the activity table" value and takes the `DevWarning` + `ACT_IDLE` fallback arm that both
 *  interest bodies carry. */
int32 ActivityIdForName(const FString& ActivityName) const;

/** SEAM for `m_bSequenceFinished` (`+0x65c`) and `m_bSequenceLoops` (`+0x65d`), the two animation
 *  bytes `RunInterestingPlaceLoop` reads. The port's clip phase is not wired to this seam yet, so
 *  both answer false. */
bool IsHintSequenceFinished() const;
bool DoesHintSequenceLoop() const;

/** SEAM for the interest loop's body block: `0x102db760` (the place's occupied marker),
 *  `0x102dcc20` (the NPC standing on it), `0x10279cc0` (align to it), `0x102e2020` / `0x102e1e20`
 *  (the motor yaw set and release) and `MoveToBoneOriginAngles("Bip01", false, true)`. None of them
 *  has a source in this substrate; each answers nothing and is named at its call site. */
FElysiumNpc* InterestingPlaceMarkerOccupant(const FElysiumInterestingPlace* Place) const;
void MoveToBoneOriginAngles(const TCHAR* BoneName, bool bMoveOrigin, bool bMoveAngles);
void SetMotorHintYaw(float Yaw);
void ReleaseMotorHintYaw();

/** `CNPC_VWerewolf::SelectScheduleForHint` (`0x103ce9b0`) — the pure half: the three hint-type
 *  answers plus the save-position distance test. Distances in SOURCE UNITS, as retail's are. */
static int32 SelectScheduleForHint(const FHintWords* Hint, float DistToSavePositionUnits,
	float GoalToleranceUnits);

/** The node-index form. */
int32 SelectScheduleForHint(int32 HintNode) const;

/** `CNPC_VWerewolf::SetHintActivity` (`0x103d6000`) — the pure half: the hint type to activity
 *  switch, with the two random draws already made. */
static int32 HintActivityForType(int32 HintType, bool bPercentRollPassed, bool bCoinFlip);

/** The whole body: draw, switch, `PositionAtHint`, `RestartIdealActivity`. */
bool SetHintActivity(const FHintWords& Hint);

/** `CNPC_VWerewolf::SetMoveHint` (`0x103d44e0`). */
void SetMoveHint(int32 HintNode, bool bRandom);

/** `CNPC_VWerewolf::SetTeleportHint` (`0x103d45c0`). */
void SetTeleportHint(int32 HintNode);
