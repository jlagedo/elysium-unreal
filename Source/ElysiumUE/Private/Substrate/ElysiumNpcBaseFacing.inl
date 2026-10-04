// `CAI_BaseNPC`'s declarations of the `Facing` family (story 5 step 5),
// moved from `ElysiumNpcFacing*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseFacing.cpp`.

// `CAI_Motor` slots 12/13/14 — the three queued-facing-target overloads (`0x102e2150`,
// `0x102e2120`, `0x102e20f0`), each of which forwards its arguments into the motor's own
// `m_facingQueue` (motor+0x54). The request is recorded (`FacingTargetRequests`, what the tests read)
// and queued (`FacingQueue`). The three floats are the SDK's `(flImportance, flDuration, flRamp)`:
// the NPC's slot 517/518 wrappers are tail jumps with every argument untouched (shape.md
// "The facing-target queue"), and `NPCThink`'s `(1.0, 0.8, 0)` at `0x10293051` is the SDK's own
// `AddFacingTarget(enemy, lastKnown, 1.0, 0.8)`. The motor add bodies (`0x102d8cf0`, `0x102d8e50`,
// `0x102d8f20`) are **unrecovered** (corpus down at 0019/6 fix 3).
struct FFacingTargetRequest
{
	int32 MotorSlot = 0;               // 12, 13 or 14 — which retail overload was reached
	FElysiumEntityHandle Target;       // the entity form, unset for the vector-only overload
	FVector Position = FVector::ZeroVector;   // centimetres
	float Importance = 0.f;            // record `+0x20`, the entry's interest
	float Duration = 0.f;              // seconds: end stamp `+0x18` = start + duration
	float Ramp = 0.f;                  // stored as `Ramp / Duration` at `+0x1c`
};

// One `CAI_InterestTarget_t` of `m_facingQueue` (stride `0x24`, the record the look queue
// `+0x5f88` also holds: `kernel_fields.tsv` `m_lookQueue`, shape.md "`CAI_BaseHumanoid`'s look-target
// list"): kind `+0x00` (0 entity, 1 position, 2 both), `EHANDLE` `+0x04`, position `+0x08`, start
// `+0x14`, end `+0x18`, ramp `+0x1c`, interest `+0x20`.
struct FFacingQueueEntry
{
	int32 Kind = 1;
	FElysiumEntityHandle Target;
	FVector PositionCm = FVector::ZeroVector;
	double StartSeconds = 0.0;
	double EndSeconds = 0.0;
	float Ramp = 0.f;
	float Interest = 0.f;
};
static constexpr int32 FacingQueueKindEntity = 0;     // motor slot 14 `0x102e20f0`
static constexpr int32 FacingQueueKindPosition = 1;   // motor slot 13 `0x102e2120`
static constexpr int32 FacingQueueKindBoth = 2;       // motor slot 12 `0x102e2150`

// +0x0ff0 m_IdealActivity — the logical activity `SetIdealActivity` (`0x10272650`) stores before it
// re-translates the ideal triple beside it. This runtime names activities rather than numbering
// them (`FElysiumIdealActivityState::Activity`), so the kernel's own copy is carried as retail's
// registered number, which is what its bodies compare and write.
int32 IdealActivityNumber = 0;

TArray<FFacingTargetRequest> FacingTargetRequests;
// `CAI_Motor::m_facingQueue` (motor `+0x54`), in add order. Not saved (save files are disposable):
// every live writer re-adds on its own think.
TArray<FFacingQueueEntry> FacingQueue;
// The point last handed to `IElysiumNpcMotor::SetFacingTarget`, so the seam is called on a change only.
TOptional<FVector> FacingTargetHanded;

/** `0x102d8b50` (72 bytes), reached through `0x102e2b10` from slot 15's compaction: an entry whose
 *  end stamp is before now, or an ENTITY entry whose handle no longer resolves, is expired. The body
 *  is **unrecovered** past its footprint; the test is the look queue's own prune (`0x1025fa50`
 *  pass 3), the same record in the same DLL. */
bool MotorFacingEntryExpired(const FFacingQueueEntry& Entry, double Now) const;
/** `0x102d8bc0` (231 bytes, reads `+0x14`..`+0x20`): the entry's weight now. Read as the look queue's
 *  inline copy (`0x1025fa50` pass 5): `t = (now - start) / (end - start)`, 0 outside `[0, 1]`; `f =
 *  t / ramp` below the ramp, `(1 - t) / ramp` past `1 - ramp`, else 1; `w = (3f^2 - 2f^3) * interest`. */
float MotorFacingEntryInterest(const FFacingQueueEntry& Entry, double Now) const;
/** The entry's point, centimetres: an ENTITY entry follows its entity (refreshed from the entity's
 *  `EyePosition` each call, as `0x1025fa50` pass 5 refreshes a look entry); a POSITION or BOTH entry
 *  answers the stated point. Which point `0x102e2180` itself asks for is **unrecovered**. */
FVector MotorFacingEntryPosition(FFacingQueueEntry& Entry);
/** `CAI_Motor` slot 15 `0x102e2180` (the SDK's `GetFacingDirection`): compact the queue, then blend
 *  the survivors into a unit direction. Answers zero for an empty queue or a zero total weight.
 *  `OutInfluence` is the slot's second answer (ST0): the total interest `1 - prod(1 - w)`
 *  (`102e22de..102e22f5`), 0.0 for an empty queue; motor slot 18 `0x102e19e0` blends by it.
 *  `OutRangeCm` is the port's presentation: the farthest contributing entry's distance. */
FVector MotorFacingQueueBlend(float& OutInfluence, double& OutRangeCm);
/** Hand slot 15's blend to the body as a point (`IElysiumNpcMotor::SetFacingTarget`), every think
 *  from `MotorThinkUpkeep`; unset when the queue blends to nothing. The body only faces it. */
void MotorHandFacingTarget();

struct FTurnActivityPick
{
	int32 Activity = 1;            // ACT_IDLE is the tail of both ladders
	bool bTagsTurnMemory = false;  // `m_afMemory |= 0x2000` on the picks that take it
};

// The seam's read side, so a body cites `0x102e1f90` at the point of use.
float MotorDeltaIdealYaw() const;

void MotorAddFacingTarget(const FFacingTargetRequest& Request);

// `CAI_BaseNPC::SetIdealActivity` (`0x10272650`): store `m_IdealActivity` (+0x0ff0), then
// re-translate it into `m_nIdealSequence`/`m_IdealTranslatedActivity`/`m_IdealWeaponActivity`
// through `0x10272130`. The translation chain is story 29d's; this stores the word and stops,
// which is the half the turn ladder is measured by.
void SetIdealActivityNumber(int32 Activity);

// `CAI_BaseNPC::SetTurnActivity` `0x10289d10` — the base line's ladder.
static FTurnActivityPick TurnActivityBaseLadder(float YawDelta,
	TFunctionRef<bool(int32)> HasSequence);

// `CAI_Motor` slot 7 `0x102e11f0`. The ledger's seam name; retail's body is the airborne jump
// step and never touches the facing queue (see the definition).
void ClearFacingTarget();

// `CAI_BaseNPC::FacingIdeal` `0x10278c80`.
bool FacingIdeal() const;

// The retail cvar the whole facing-target family is gated on (`0x10924f74`, read as
// `!IsCommand() && m_nValue != 0` — `ConVar::GetBool()` inlined): `debug_allow_move_facing`,
// shipped "1".
bool FacingTargetsEnabled() const;

