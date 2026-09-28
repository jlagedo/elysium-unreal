// Story 29c-1, family **Hints** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcHints.cpp` and the tests in
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
// Family **Squad** built `NthHintOfType` (`ElysiumNpcSquad.inl`) over the same absent global
// list, walked by ordinal and answering an `FElysiumEntity*`. It is left exactly as it is: the two
// are the same missing store seen from two sides, and the day a hint store lands it replaces both.

/** `CGlobalEntityList::FindEntityByName` + the `CAI_Hint` RTTI cast that `FindHintEndEntity`
 *  (`0x103d6520`) performs: the first entity the name matches, as a hint index, or `INDEX_NONE`
 *  when that first match is not a hint. */
int32 FindHintByName(const FString& HintName) const;

/** SEAM for `0x10296c40`, the shared range/LOS/cover validator both `IsHintCoverValid` bodies
 *  forward into. `0x10296c40` is NOT this story's row (layer 11) and is not ported here; this
 *  answers false and names it. */
bool ValidateHintCoverRange(const FHintWords& Hint, const FElysiumEntity* CoverObject,
	float AngleRangeDot, float BadRangeLimit) const;

// --- Species words this family's bodies read ------------------------------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout; the species words above
// `+0x665c` have no port member because one leaf carries every classname and the same offset means
// different things per species. These are the ones this family's bodies read, declared by retail
// name with the species class that owns the offset, exactly as family Squad declared its four.

uint32 WerewolfHintFlags = 0;         // +0x66e8 CNPC_VWerewolf, the hint-gate bit word (walked)
//
// `+0x66b8 m_vLastJumpPosition[2]` and `+0x66d0 m_iLastJumpPositionIdx` — the ring
// `AddHintToStoredJumpPositions` writes — are declared by family **Motor**
// (`ElysiumNpcMotor.inl`) for its reader `IsPosNearStoredJumpPositions` (`0x103618a0`), in
// SOURCE UNITS. This family writes that same pair rather than standing a second copy.

/** One row of the Werewolf's authored hint-groundpoint array — `+0x6714` the base, `+0x6720` the
 *  count, stride `0x48`. The array is filled by no ported producer, so it is empty and
 *  `GetHintGroundpoint` always takes its miss arm, which is the recovered fallback and not a
 *  refusal. SOURCE UNITS, as every retail position word here is and as family Motor's
 *  `GetGroundpoint` takes and answers.
 *
 *  **Story 29d, family Hints10 corrected the record's layout.** 29c-1 read the hint pointer at
 *  `+0x00`; both readers disagree. `GetHintGroundpoint` (`0x103d6770`) starts its cursor at
 *  `field_0x6714 + 4` and `GetHintEndEntity` (`0x103d6390`) matches on `puVar4[1]`, so the hint is
 *  at `+0x04`; `+0x00` is a cached `EHANDLE` to the hint's END ENTITY, which is the word
 *  `GetHintEndEntity` answers, and the groundpoint is at `+0x08`. The port's matching behaviour was
 *  already right — it keys on the hint — so only the record and the comment change. */
struct FWerewolfHintGroundpoint
{
	/** +0x00 — the cached end-entity handle `CNPC_VWerewolf::GetHintEndEntity` (`0x103d6390`)
	 *  answers on a hit, carried here as a hint-node index like every other handle in this seam. */
	int32 CachedEndEntity = INDEX_NONE;
	int32 HintNode = INDEX_NONE;                        // +0x04
	FVector GroundpointUnits = FVector::ZeroVector;     // +0x08
};

// --- The bodies -----------------------------------------------------------------------------------

/** `0x102a9f40` — "the wait" every `TASK_DO_INTEREST_*` shares: claim the marker, fire
 *  `OnInterestingPlaceArrived`, take the INTO arm or the idle arm, and stamp `m_flWaitFinished`. */
void ClaimInterestingPlace(FElysiumInterestingPlace* Place, bool bClaimSecondary, double Now);

/** `0x10365780` — the task-side hint install: search within 5000 units, install at
 *  `BaseScheduleHost.HintNode` and complete the task, or write the fail text and `TaskFail(4)`. */
bool FindHintNode(int32 HintType, uint8 SearchFlags);

// `CNPC_VWerewolf::GetGroundpoint` (`0x103d6a40`), the fallback `GetHintGroundpoint` ends at, is
// family **Motor**'s body and is declared in `ElysiumNpcMotor.inl`. It takes and answers
// SOURCE UNITS.

/** `0x10297430` — resolve `m_hHintCoverObject` (`+0x6448`) and forward into the cover validator
 *  with the hint's `m_flTargetAngleRangeDot` and retail's `0.731`. */
bool IsHintCoverValid(int32 HintNode) const;

/** `0x102974f0` — the same forward with `1.1` and no handle-validity pre-check. */
bool IsHintCoverValidLoose(int32 HintNode) const;

/** `0x102aaa60` — the hint-node idle activity restart. Returns whether an activity was restarted. */
bool PlayHintIdleActivity(double Now);

/** SEAM for `0x102b5de0`, the gate `PlayHintIdleActivity` puts in front of each of its three hint
 *  types: a shoot-target/enemy LOS test through the engine trace. Answers true, which is retail's
 *  "the gate passed" answer and the arm that restarts the activity. */
bool HintIdleActivityGate() const;

/** `0x1029f780` — the cached patrol-node interest-place resolve at `+0x6300`. */
int32 ResolvePatrolInterestPlace(int32 PatrolNode);

/** SEAM for `0x1029f730` — the patrol node's interest record, whose `+0x468` names the entity the
 *  resolve looks up. There is no patrol-node graph here. Answers false. */
bool PatrolNodeInterestRecordName(int32 PatrolNode, FString& OutName) const;

/** `0x102aa210` — the INTO → IDLE → OUTOF interest-place loop. Returns whether the caller's task
 *  is finished, which is the byte retail leaves in `AL`. */
bool RunInterestingPlaceLoop(FElysiumInterestingPlace* Place, double Now);

/** SEAM for the interest loop's body block: `0x102db760` (the place's occupied marker),
 *  `0x102dcc20` (the NPC standing on it), `0x10279cc0` (align to it), `0x102e2020` / `0x102e1e20`
 *  (the motor yaw set and release) and `MoveToBoneOriginAngles("Bip01", false, true)`. None of them
 *  has a source in this substrate; each answers nothing and is named at its call site. */
FElysiumNpc* InterestingPlaceMarkerOccupant(const FElysiumInterestingPlace* Place) const;
void MoveToBoneOriginAngles(const TCHAR* BoneName, bool bMoveOrigin, bool bMoveAngles);

