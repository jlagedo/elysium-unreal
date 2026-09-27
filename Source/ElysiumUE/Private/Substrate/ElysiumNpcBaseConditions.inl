// `CAI_BaseNPC`'s declarations of the `Conditions` family (story 5 step 5),
// moved from `ElysiumNpcConditions*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseConditions.cpp`.

/** `CAI_BaseNPC::FCanCheckAttacks` (`0x10270840`) — the BASE half, which the Troika body
 *  (`0x102953a0`, slot 564) tail-calls. Kept separate because the Troika body's own arm is a
 *  suppression in front of it and collapsing the two would lose the delegation. */
bool FCanCheckAttacksBase() const;

/** SEAM for `CAI_Navigator::GetNavType()` (`0x1027d990` = `m_pNavigator(+0x5d34)[0x18]`), whose
 *  `NAV_JUMP`(1) and `NAV_CLIMB`(3) values are the two `FCanCheckAttacks` refuses on. This
 *  substrate's motor has no nav-type word, so this answers `NAV_GROUND`(0) — the value that does NOT
 *  suppress, which is the arm every ground body takes in retail. */
int32 NavType() const;

/** SEAM for `0x102e0b40` + `0x102e1c10(motor, yaw, -1.0)`, the reset-and-set-ideal-yaw pair the door
 *  transaction performs once the door has named a point. Kept separate from family Hints'
 *  `SetMotorHintYaw` because it is a different retail call with a different turn-rate argument. */
void SetAlternateAiIdealYaw(float YawDegrees);

/** SEAM for `FUN_10298840`, the "start the open" follow-up mode 1 runs when `m_bOpeningDoorWait` is
 *  clear: it asks the door for its point again, `RestartIdealActivity`s onto it, and either pushes
 *  the door open or fires its use handler. Answers false. */
bool StartOpeningDoor(FElysiumEntity& Door);

/** `CCineAISchedule::RemoveIgnoredConditions` (`0x101a89a0`) — the SCRIPT entity's own slot-459
 *  body, which the NPC's slot 459 (`0x1026d7f0`) dispatches into. It clears fourteen conditions on
 *  the scene partner, in retail's order, and clears its `m_bCondTookDamage` byte between the two
 *  groups. Static and taking the partner because `this` is the cine entity in retail, not the NPC. */
static void ClearCineIgnoredConditions(FElysiumNpc& Partner);

/** SEAM for `CCineAISchedule::0x101a8930`, the "am I already in this state" predicate slot 459 tests
 *  first, and for the cine entity's `m_hTargetEnt` -> `+0x94` partner walk. This runtime's scripted
 *  scenes do not carry either, so the resolve answers null and slot 459 clears nothing. */
FElysiumNpc* CineIgnoredConditionsPartner() const;
