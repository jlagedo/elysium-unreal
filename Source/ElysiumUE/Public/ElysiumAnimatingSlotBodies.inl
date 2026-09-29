// `CBaseAnimating (and CBaseToggle)`'s hand-written slot bodies and the members they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Included inside `class FElysiumAnimating`
// (`ElysiumAnimating.h`), after its generated slot surface; the definitions are in
// `Private/Substrate/ElysiumAnimatingSlotBodies.cpp`.

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

/** What the seams above were ASKED, so a test can assert that a body reached its motor call and
 *  that the refusal was the recovered one. Read by the test suite and by nothing else. */
struct FMotorSeamLedger
{
	int32 PerformMovement = 0;           // the navigator's vtable slot 5 delegate
	float PerformMovementInterval = 0.f;
	int32 PostRunWeaponUpdates = 0;      // `PostRun`'s ordered pair, tallied on the second half
	float PostRunInterval = 0.f;
	int32 SetupJumpCommits = 0;          // `thunk_FUN_102c4e80`
	int32 MoveProbeChecks = 0;           // `CanStandAt`'s `thunk_FUN_102e7270`
	int32 HullTraces = 0;                // every `KernelHullTrace` caller
	int32 JumpArcSolves = 0;             // `thunk_FUN_102c4cc0`
};


mutable FMotorSeamLedger MotorSeams;

//
// `CBaseToggle`'s pair, flattened onto `CAI_BaseNPCTroika` by the shape map at `+0x04fc` and
// `+0x0504`. Slot 110 (`0x101c1480`) is their only writer in the whole kernel closure, and nothing
// in this runtime reads them yet — the motor's move distance and a door's lip are two other
// subsystems' words that happen to live on this leaf because retail's NPC derives from
// `CBaseToggle`. They are carried so the two `atof` arms have somewhere real to land.
float MoveDistance = 0.f;   // +0x04fc m_flMoveDistance, keyfield `distance`

float Lip = 0.f;            // +0x0504 m_flLip, keyfield `lip`

/** The recovered classification of one key, with the `#` truncation retail performs FIRST applied to
 *  the name. `OutKey` is the truncated key — a `#` in a key name ends it, so `"origin#2"` is
 *  `"origin"`. */
static EKeyValueArm ClassifyKeyValue(const FString& Key, FString& OutKey);

/** `CAI_BaseNPC::PerformMovement(a, b)` `0x1026c120` — VProf scaffolding around one delegating call
 *  to the navigator's vtable slot 5, both parameters forwarded. */
void PerformMovement(float Interval, int32 MoveFlags);

/** `CBaseEntity::KeyValue(const char*, const char*)` (`0x1009e430`) — the tail slot 110's third arm
 *  returns verbatim. Ten arms, of which this runtime owns one: family **Lifecycle** already
 *  recovered the classification (`ClassifyKeyValue`, which performs retail's `#` truncation FIRST)
 *  and recorded that the `DataMap` arm — retail's `GetDataDescMap` chain walk offering the key to
 *  each level's `ParseKeyvalue` — IS this runtime's own class-chain field table. That arm is run
 *  here. The other nine (`rendercolor`, `renderamt`, `disableshadows`, `disablereceiveshadows`,
 *  `mins`, `maxs`, `angle`, `angles`, `origin`) are `CBaseEntity`'s own story and are NOT this
 *  kernel's row; each answers **true** here, which is retail's answer for a key it matched, so a
 *  matched key is never reported to the caller as unhandled. */
bool BaseEntityKeyValue(const TCHAR* Key, const TCHAR* Value);
