// `CAI_BaseNPC`'s declarations of the `Lifecycle` family (story 5 step 5),
// moved from `ElysiumNpcLifecycle*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseLifecycle.cpp`.

/** One `CAI_StandoffBehavior`'s own words, by offset. `this+4` is the NPC it is attached to; every
 *  other offset below is the behaviour's. */
struct FStandoffWords
{
	int32 Posture = 0;             // +0x001c  the posture the selector writes (0, 2, 3)
	bool bCoverDirty = false;      // +0x0024  read only by the COND_ENEMY_DEAD arm's answer
	int32 ReactionChanceMin = 0;   // +0x0030
	int32 ReactionChanceMax = 0;   // +0x0034
	int32 ChanceThreshold = 0;     // +0x0038  the RandomInt(0,99) roll is compared against this
	double NextReactionAt = 0.0;   // +0x003c  an absolute curtime stamp
	float ReactionDelayMin = 0.f;  // +0x0040
	float ReactionDelayMax = 0.f;  // +0x0044  == 0.0f means "no range: use the min alone"
	int32 ReactionsLeft = 0;       // +0x0048  the counter the selector decrements and re-rolls
	bool bSawNewEnemy = false;     // +0x004c  latched, and cleared by the first pass that reads it
	double BlockedSince = 0.0;     // +0x0064  an absolute curtime stamp
	bool bBlockedLongEnough = false;  // +0x0054 the byte the +0x64 test sets
};

/** The conditions `0x102c7600` reads, by retail condition number, handed in so the rule is pure. */
struct FStandoffConditions
{
	bool bCond0x40 = false;  // the first gate; true takes the 0x29/0x28 answer
	bool bCond0x3f = false;  // the second gate; same answer
	bool bCond0x4c = false;  // the chance-rolled hint-timer arm
	bool bCond0x48 = false;  // the posture-2 promotion and the blocked stamp
	bool bCond0x4f = false;
	bool bCond0x51 = false;
	bool bCond0x60 = false;  // the 0x21 answer, gated on a 0x31 roll when 0x48 also stands
};

/** `+0x1ddc`, read by `FUN_10160680` — a float scaled by the compiled constant `DAT_10725c9c`.
 *  **Unrecovered**: the body has one direct caller, no vtable slot, and neither the retail field
 *  name nor the class that owns the offset is settled. Declared by offset, as 29b declares an
 *  unsettled word. */
float Field_0x1ddc = 0.f;

/** What `CBaseEntity::KeyValue(const char*, const char*)` (`0x1009e430`) did with one key — the
 *  cascade slot 110 runs for `CAISound`, `CAI_Hint`, `CAI_InterestingPlace`,
 *  `CAI_InterestingPlaceConverstation` and `CAI_StandoffGoal`. */
enum class EKeyValueArm : uint8
{
	RenderColor,     // rendercolor / rendercolor32 -> m_clrRender RGB
	RenderAmt,       // renderamt -> m_clrRender alpha, atoi
	DisableShadows,  // disableshadows, nonzero -> m_fEffects |= 0x20
	DisableReceiveShadows,  // disablereceiveshadows, nonzero -> m_fEffects |= 0x80
	Mins,            // mins -> SetCollisionBounds(value, current maxs)
	Maxs,            // maxs -> SetCollisionBounds(current mins, value)
	Angle,           // angle -> rewritten as angles and re-dispatched
	Angles,          // angles -> vtable +0x368 SetAbsAngles
	Origin,          // origin -> vtable +0x360 SetAbsOrigin
	DataMap,         // no literal matched: walk the datamap chain (vtable +0x148)
};

/** `CAI_Hint::ScriptHide` (`0x102d0860`) — the base `CBaseEntity::ScriptHide` and then the hint's
 *  own `m_iDisabled` (`+0x05e8`) := 1. Pure over family **Hints**' `FHintWords`, because there is no
 *  `CAI_Hint` ENTITY in this substrate to run the base half on; the caller owns that half. */
static void HintScriptHide(FHintWords& Hint);

/** `CAI_Hint::ScriptUnhide` (`0x102d0890`) — the exact inverse, `m_iDisabled := 0`. */
static void HintScriptUnhide(FHintWords& Hint);

/** `CAI_Hint::Kill` (`0x102d08c0`), whose whole body is `JMP [[this]+0x134]` — vtable `+0x134` is
 *  slot 77, so a hint node's **Kill is its ScriptHide**, not the entity teardown every other class
 *  runs at slot 119. Verbatim: this forwards. */
static void HintKill(FHintWords& Hint);

/** `CAI_StandoffGoal::Spawn` (`0x102cd2d0`) — `ThinkSet(&LAB_10006672, 0, null)` then
 *  `m_flNextThink = curtime + _DAT_1044e658`. Answers the next-think it would have armed. */
static double StandoffGoalSpawnNextThink(double Now);

/** `CAI_Hint::Spawn` (`0x102d0b60`) — a hint node's own spawn: fill the unset per-hint-type
 *  defaults, turn the angle range into its dot-product test, and fold the group id into a bit.
 *  Pure over family **Hints**' `FHintWords`, which is the hint's datamap. */
static void HintSpawn(FHintWords& Hint);

/** The recovered classification of one key, with the `#` truncation retail performs FIRST applied to
 *  the name. `OutKey` is the truncated key — a `#` in a key name ends it, so `"origin#2"` is
 *  `"origin"`. */
static EKeyValueArm ClassifyKeyValue(const FString& Key, FString& OutKey);

/** The `angle` arm's rewrite (`0x1009e430` @ `1009e6f6`): a NEGATIVE value takes a literal, and any
 *  other value becomes `"<current pitch> <value> <current roll>"` — the yaw only. Retail then
 *  re-enters the cascade as `angles`. */
static FString RewriteAngleKey(float AngleValue, const FVector& CurrentAngles);

/** `CAI_BaseNPC::Restore` (`0x1027c160`), slot 127's body on the `CAI_BaseNPC` line and the body
 *  the Troika override `0x10299700` calls through a DIRECT `thunk_`. Read the extended save header
 *  (`AIExtendedSaveHeader_t`, datamap `0x105cabd0`) into `+0x19b4`, chain
 *  `CBaseCombatCharacter::Restore` and KEEP its answer, decode two sentinel stamps, then re-link the
 *  motor and the move-and-shoot overlay. Returns the chain's answer, which is what every caller of
 *  slot 127 propagates.
 *
 *  **CORRECTED by story 29d, family SaveRestore10.** 29c-1 read `thunk_FUN_101cf2f0(p, 4)` and
 *  `(p, 3)` as "four floats from `m_flExtendedBlockedByFriendTimer`, three from `m_flWaitFinished`"
 *  and ported them as a save/restore clock re-base. The second argument is the sentinel MODE, not a
 *  count: the listing has exactly two calls (`1027c189 PUSH 0x4` on `+0x5b8c`, `1027c199 PUSH 0x3`
 *  on `+0x5db4`), and `0x101cf2f0` is the decode half of the codec family SaveRestore10 ports —
 *  `ElysiumNpcSaveRestore10.inl` § "The sentinel codec". Retail performs NO re-base here, so
 *  `RebaseRestoredStamp` — a rule retail does not have — is gone with it. */
int32 RestoreExtendedHeader(void* Archive);

/** `CAISound::OnRestore` (`0x100aa5a0`), slot 130 — the asm is
 *  `MOV [ESP+4], 0 / JMP [[this]+0x18]`: it overwrites its own argument with 0 and tail-jumps to
 *  slot 6 `SetCheckUntouch`, so **a restore always lands as `SetCheckUntouch(false)` whatever the
 *  caller passed**. Answers the value forwarded, which is always false. */
static bool OnRestoreForwardsCheckUntouch(bool bCallerValue);

/** `CAI_BaseNPC::UpdateOnRemove` (`0x1027ca30`), slot 180 for the NON-Troika branch — the squad
 *  unlink (`+0x5da4`), the hint release (`+0x5ddc`, a 0.0-delay release), the own vtable `+0x7fc`
 *  dispatch, then `CBaseCombatCharacter::UpdateOnRemove`. In that order. */
void BaseNpcUpdateOnRemove();

/** `FUN_10160680` — `*(float*)(this+0x1ddc) * DAT_10725c9c`. **Unrecovered**: one direct caller, no
 *  slot, and neither the field's retail name nor the constant's value is pinned. The constant is
 *  named at the definition and the scaling is the whole body. */
float ScaleField_0x1ddc() const;

/** `CAISound::FUN_10026e70`, slot 153 for the five AI-helper classes — is `m_vecVelocity`
 *  (`+0x03d4`) different from the static default vector `DAT_1070d1b0/b4/b8`? That vector is the
 *  always-zero one `GetGroundVelocityToApply` (slot 210) answers, so this is "am I moving at all". */
bool HasNonDefaultVelocity() const;

/** `CAI_Motor::FUN_102e1110` (`0x102e1110`) — the motor reset to default: push the navigator's move
 *  type, reset the motor state, zero the facing/move vector and set gravity back to 1.0.
 *  **The target `FElysiumNpcMotor::ResetToDefault` names a struct this runtime does not stand**, so
 *  it lands here, on the leaf that owns the motor. */
void MotorResetToDefault();

/** `CAI_StandoffBehavior::vfunc13` (`0x102c7600`) — the selector. Returns the retail schedule /
 *  task-continue code (`0x17`, `0x25`, `0x21`, `0x29`, `0x28`) or `INDEX_NONE` for "fall through to
 *  `CAI_Behavior::vfunc13`", which is the base this runtime does not carry. `bHasEnemy` is slot 156
 *  `GetEnemy() != NULL` (vtable `+0x29c`); `Hint` is the claimed hint node's words, or null.
 *
 *  The state 2 gate at the top (`m_NPCState == 2`, `+0x5cc0`) is the caller's: a standoff selector
 *  that is not in COMBAT falls straight through. */
static int32 StandoffSelect(FStandoffWords& Words, const FStandoffConditions& Conditions,
	bool bInCombatState, bool bHasEnemy, FHintWords* Hint, double Now);



/** `+0x0368 m_CollisionGroup`, a `CBaseEntity` word below the NPC table. The ONE input of slot 91
 *  `ShouldCollide` (`0x100b4de0`). Nothing in this runtime writes it yet: this substrate's collision
 *  is Unreal's channel set on the body, so the retail group number has no producer. Declared so the
 *  rule has the word it reads rather than a guess. Retail's `COLLISION_GROUP_DEBRIS` is 1. */
int32 CollisionGroup = 0;

/** `+0x0500 m_flDelay`, the `CBaseDelay` trigger delay slot 152 `GetDelay` (`0x1004fc10`) answers.
 *  A `CBaseEntity` word below the NPC table; this runtime carries an output's delay on the wire
 *  (`FElysiumOutputDef`) rather than on the entity, so nothing writes this. */
float EntityDelay = 0.f;

/** SEAM for `thunk_FUN_101618a0(player)` — the `!playercontroller` half of slot 559
 *  `FindNamedEntity` (`0x10279090`). Retail resolves the player's own scene stand-in from the
 *  player; this runtime stands that as the `npc_VPlayerController` leaf, which a map places by name
 *  and which the player holds no pointer to. Answers the player, and names what would settle it. */
FElysiumEntity* PlayerControllerOf(FElysiumEntity* Player) const;
