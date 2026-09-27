// `CAI_BaseNPC`'s declarations of the `Senses` family (story 5 step 5),
// moved from `ElysiumNpcSenses*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSenses.cpp`.

/** SEAM for `CAI_Navigator::MarkNodeUnreachable` (`0x102f1fa0`), which `OnDoorBlocked`
 *  (`0x1027de00`) calls with `5.0` or `20.0` seconds and the blocking door. It reaches the AI
 *  NETWORK — it looks the door's nav link up in the node graph and stamps `node+0x64 |= 1`,
 *  `node+0x68 = curtime + seconds`. This runtime has no node graph, so the call is COUNTED and
 *  marks nothing; the retail word it stands for is the node's own unreachable-until stamp. */
int32 NavigatorUnreachableMarks = 0;

int32 SquadFocusWrites = 0;

int32 DoorNextTryWrites = 0;

/** SEAM for `0x102ee6a0`, the guard retail puts in front of that call: "the navigator has a
 *  network AND that network has a node list". Answers false — there is no network — so retail's
 *  own refusal arm is the one taken and `NavigatorUnreachableMarks` never moves. */
bool NavigatorHasNodeGraph() const;

/** `GetSquadFocus` / `SetSquadFocus` (`0x103166b0` / `0x10316660`), squad `+0x70` (the focus
 *  handle) and `+0x74` (its expiry, `curtime + _DAT_10463584` = **15.0 s**). **SEAM**: this
 *  substrate stands no squad object (`FElysiumNpcBase::ConnectedSquad` answers null), so the getter
 *  answers null and the setter counts. Both are named rather than folded into one "squad seam"
 *  because `OnDoorBlocked` reads before it writes and the read's answer decides the write. */
const FElysiumEntity* SquadFocus() const;

void SetSquadFocus(const FElysiumEntity* Focus);

/** `CBaseDoor+0x644` and `+0x640`, the two words `OnDoorBlocked` reads and writes on the DOOR.
 *  `+0x644` is a flag word tested `& 0x10` (the whole retry is skipped) and `& 0x40` (the retry
 *  waits 5 s rather than 20 s); `+0x640` is a float "do not try me again before" stamp that
 *  `0x100f0e30` MAX-writes. **SEAM**: `FElysiumEntity` carries neither word and no corpus body in
 *  layers 0–9 names them, so the flags answer 0 — retail's own "no flags" door, which takes the
 *  20-second arm — and the stamp is counted. Their retail names are **unrecovered**. */
static uint32 DoorBlockFlags(const FElysiumEntity& Door);

void SetDoorNextTryTime(FElysiumEntity& Door, double At);

/** `0x10027020`, `CAISound#168` and 17 more — the BASE line's `CBaseEntity* GetEnemy()`, whose
 *  whole body is `JMP [[this]+0x29c]`: a tail jump to slot 167, the const overload, with no other
 *  work. This leaf stands the TROIKA line (`0x102b5360`, slot 168 below), so the base body is not
 *  what `FElysiumNpc` dispatches; it is carried here by name so the two lines are both in the
 *  tree and a reader can see which one this runtime runs. */
FElysiumEntity* GetEnemyBaseLine() const;

/** The arithmetic of slot 364 (`0x10326bd0`), pure over the three vectors the body fetches through
 *  a virtual. Split out because the virtual that supplies the aim — slot 370 `HeadDirection2D`,
 *  which slot 372 forwards to — is still a GENERATED STUB answering the zero vector, so the slot
 *  body below cannot exercise its own threshold yet. The rule is exact and testable here; the input
 *  is another story's seam and is named at the call.
 *
 *  `AimConeAdmits` zeroes the delta's Z BEFORE normalising and compares strictly against
 *  `0.994`. */
static bool AimConeAdmits(const FVector& OriginCm, const FVector& TargetCm, const FVector& Aim);

/** `0x102d1320`, `CAI_Hint#163` — `CAI_Hint::IsViewable`. Pure over the hint's own words, so it is
 *  the whole recovered rule and not a seam: a hint with `m_iDisabled` set is NOT viewable (retail
 *  returns `m_iDisabled & 0xffffff00`, whose low byte — the `bool` — is always zero), and
 *  otherwise viewable exactly when `m_nHintType == 13`. */
static bool IsHintViewable(const FHintWords& Hint);

/** `0x1027de00`, which 29c filed as `SetEnemy`. It is the **door-blocked** notice: `0x10298840`
 *  calls it when `0x100eec70` refuses the NPC/door pair, and `0x1027dfb0` (the hit-by-door
 *  handler) forwards to it when the door that hit this NPC is the one it was opening. Seven arms,
 *  in retail's order, at the definition. */
void OnDoorBlocked(FElysiumEntity& Door);

/** `0x10270180` — the occlusion-EDGE state machine behind `m_bEnemyWentOccluded` (`+0x5bc5`) and
 *  `m_vecEnemyWentOccluded` (`+0x5bc8`). Three arms on (enemy, bHaveLos), none of which is a
 *  fall-through; the distance gate is `_DAT_104563b0` = **4096.0**, a SQUARED distance, so the
 *  edge trips at 64 Source units of drift. */
void UpdateEnemyWentOccluded(const FElysiumEntity* Enemy, bool bHaveLos);

/** `0x1026a2a0` — `SetDistLook`. Writes `m_pSenses->m_LookDist` (`CAI_Senses+0x10`, which
 *  `vtmb_fields CAI_Senses` names outright; 29c filed the inner field as unrecovered). The
 *  argument is in SOURCE units in retail and CENTIMETRES here, as every port distance is. */
void SetDistLook(float LookDistCm);
