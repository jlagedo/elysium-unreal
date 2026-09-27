// `CAI_BaseNPC`'s declarations of the `Facing` family (story 5 step 5),
// moved from `ElysiumNpcFacing*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseFacing.cpp`.

// `CAI_Motor` slots 12/13/14 — the three queued-facing-target overloads (`0x102e2150`,
// `0x102e2120`, `0x102e20f0`), each of which forwards its arguments into the motor's own
// `m_facingQueue` (motor+0x54). **SEAM**: this substrate models no facing queue, so each records
// the request and adds nothing. `FacingTargetRequests` is what the test reads; nothing else does.
struct FFacingTargetRequest
{
	int32 MotorSlot = 0;               // 12, 13 or 14 — which retail overload was reached
	FElysiumEntityHandle Target;       // the entity form, unset for the vector-only overload
	FVector Position = FVector::ZeroVector;
	float Duration = 0.f;
	float Ramp = 0.f;
	float Tolerance = 0.f;
};


// +0x0ff0 m_IdealActivity — the logical activity `SetIdealActivity` (`0x10272650`) stores before it
// re-translates the ideal triple beside it. This runtime names activities rather than numbering
// them (`FElysiumIdealActivityState::Activity`), so the kernel's own copy is carried as retail's
// registered number, which is what its bodies compare and write.
int32 IdealActivityNumber = 0;

// `CAI_Motor::DeltaIdealYaw` (`0x102e1f90`): `AngleDiff(m_IdealYaw, AngleMod(GetLocalAngles().y))`,
// and exactly 0 when the two are equal. **SEAM** — nothing in this substrate writes it, so it
// stands at retail's "already facing the ideal" answer, 0. It is a field rather than a query so the
// ladders below can be driven against a live number the day the mover carries one.
float MotorIdealYawDelta = 0.f;

TArray<FFacingTargetRequest> FacingTargetRequests;

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

// `CAI_Motor` slot 7 `0x102e11f0` — cancel the current facing-queue entry.
void ClearFacingTarget();

// `CAI_BaseNPC::FacingIdeal` `0x10278c80`.
bool FacingIdeal() const;

// The retail cvar the whole facing-target family is gated on (`0x10924f74`, read as
// `!IsCommand() && m_nValue != 0` — `ConVar::GetBool()` inlined): `debug_allow_move_facing`,
// shipped "1".
bool FacingTargetsEnabled() const;

