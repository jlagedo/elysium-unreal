// `CAI_BaseNPC`'s declarations of the `Senses` family (story 5 step 5),
// moved from `ElysiumNpcSenses*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSenses.cpp`.

/** Counts `OnDoorBlocked` (`0x1027de00`)'s calls of the stale-link mark `0x102f1fa0(nav, 5.0 or 20.0,
 *  door)`: `link+0x64 |= 1`, `link+0x68 = curtime + seconds`, `link+0 = door` on the link the
 *  current path stands on. Since 0018/7 the mark is real (`NavMarkLinkStale`: the door smart link the
 *  body is held at, the words on its door, `FElysiumDoorLinkWords`); the count stays for the suites. */
int32 NavigatorUnreachableMarks = 0;

int32 SquadFocusWrites = 0;

int32 DoorNextTryWrites = 0;

/** `0x102ee6a0`, the guard retail puts in front of that call: `CAI_Navigator::IsGoalActive`
 *  (`nav+0x30` is the PATH, `path+0x24` its head), not "a network with nodes" as first read
 *  (0018/7 correction). Answers `NavIsGoalActive()`. The name is kept for its callers. */
bool NavigatorHasNodeGraph() const;

/** `GetSquadFocus` / `SetSquadFocus` (`0x103166b0` / `0x10316660`), squad `+0x70` (the focus
 *  handle) and `+0x74` (its expiry, `curtime + _DAT_10463584` = **15.0 s**). **SEAM**: this
 *  substrate stands no squad object (`FElysiumNpcBase::ConnectedSquad` answers null), so the getter
 *  answers null and the setter counts. Both are named rather than folded into one "squad seam"
 *  because `OnDoorBlocked` reads before it writes and the read's answer decides the write. */
const FElysiumEntity* SquadFocus() const;

void SetSquadFocus(const FElysiumEntity* Focus);

/** `CBaseDoor+0x644` `m_bfNpcFailedFlags` and `+0x640` `m_flNpcFailedTimer` (the datamap names;
 *  `FElysiumDoorBase::NpcFailedFlags` / `NpcFailedTimer` since 0018/7), the two words `OnDoorBlocked`
 *  reads and writes on the DOOR. `+0x644` is tested `& 0x10` (the whole retry is skipped) and
 *  `& 0x40` (the retry waits 5 s rather than 20 s); `+0x640` is the "do not try me again before"
 *  stamp `0x100f0e30` MAX-writes. A non-door answers 0 and takes no write. The stamp is counted too. */
static uint32 DoorBlockFlags(const FElysiumEntity& Door);

void SetDoorNextTryTime(FElysiumEntity& Door, double At);

/** `0x10027020`, `CAISound#168` and 17 more — the BASE line's `CBaseEntity* GetEnemy()`, whose
 *  whole body is `JMP [[this]+0x29c]`: a tail jump to slot 167, the const overload, with no other
 *  work. This leaf stands the TROIKA line (`0x102b5360`, slot 168 below), so the base body is not
 *  what `FElysiumNpc` dispatches; it is carried here by name so the two lines are both in the
 *  tree and a reader can see which one this runtime runs. */
FElysiumEntity* GetEnemyBaseLine() const;

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
