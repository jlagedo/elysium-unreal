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

/** The write list of `CCineNPC::RemoveIgnoredConditions` (`0x101a89a0`, the director's own slot 459,
 *  `FElysiumScriptedSequence::RemoveIgnoredConditions`, which the NPC's slot 459 `0x1026d7f0`
 *  dispatches into): fourteen conditions on the director's NPC, in retail's order, with
 *  `m_bCondTookDamage` cleared between the two groups. Static and taking the NPC because `this` is
 *  the director in retail. */
static void ClearCineIgnoredConditions(FElysiumNpcBase& Partner);
