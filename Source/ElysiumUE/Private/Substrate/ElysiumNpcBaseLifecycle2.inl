// `CAI_BaseNPC`'s declarations of the `Lifecycle19` family (story 5 step 5),
// moved from `ElysiumNpcLifecycle19*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseLifecycle19.cpp`.

static constexpr double MapFirstSecond = ElysiumNpcTunables::OneDouble;

static constexpr double NpcInitThinkDelay = ElysiumNpcTunables::TenthDouble;

static constexpr float MotorYawHalfTurn = ElysiumNpcTunables::OneEighty;

// `_DAT_104ada44` ChangBros jump gravity is family SpeciesLifecycle10's `ChangBrosJumpGravity`.
static constexpr float StartNpcDelayMin = 0.1f;

static constexpr float StartNpcDelayMax = 0.4f;

static constexpr float BaseInitDistTooFar = 1024.f;

static constexpr float BaseInitDistLookUnits = 3072.f;

static constexpr float FarSightDistTooFar = 1.0e9f;

static constexpr float FarSightDistLookUnits = 6000.f;

static constexpr int32 NpcInitCollisionMask = 0x0202400b;

static constexpr int32 NpcInitAddFlags = 0x12000;

static constexpr int32 RestoreTaskIndexCeiling = 0x29;

static constexpr uint32 CapabilityNoFloorDrop = 4u;

static constexpr int32 SpawnFlagNoFloorDrop = 4;

static constexpr int32 SpawnFlagPreAimed = 0x80;

static constexpr int32 SpawnFlagFarSight = 0x100;

static constexpr int32 StartNpcGoalEntityRetailId = 3;

static constexpr int32 StartNpcAmbushRetailId = 0x2d;

/** `m_bCineScriptHidden` (`+0x5d78`). A DISTINCT byte from `m_bScriptHidden` (`+0x0f4`, the word
 *  `0x100b5190` reads and this port carries as `FElysiumEntity::bHidden`) and from `m_fEffects`
 *  (`+0x19c`). The field ledger gives it exactly TWO accesses in the whole image, both this clear
 *  in `CAI_BaseNPC::NPCInit` (`0x10273390` and its thunk): retail writes it and never reads it. */
bool bCineScriptHidden = false;

/** Recorded think-function identity. Retail function pointers; this runtime has no think-fn
 *  table, so the name is the observable. `nullptr` / empty is `ThinkSet(NULL)`. */
FString ThinkFunctionName;

double ThinkSetDelay = 0.0;

int32 ThinkSetCalls = 0;

int32 NpcInitInlineThinkCalls = 0;

int32 BaseInitAnimatingResets = 0;       // `ClearAllClientRagdolling` + `0x10095be0` + `0x1008f540`

int32 DelayedConditionListClears = 0;    // `0x102cc7e0` twice

int32 BaseInitTailCalls = 0;             // `0x10274ca0` `SetDefaultEyeOffset`

int32 BaseInitChoreoClears = 0;          // `0x10270180(NULL, false)`

int32 FloorDropPerformed = 0;

int32 FloorDropSkipped = 0;

int32 FloorDropWarnings = 0;

int32 NavigationGoalClears = 0;          // `0x102ee270`

int32 PostRestorePathRefinds = 0;        // `0x102ee1e0`

int32 SetScheduleRetailCalls = 0;

int32 LastSetScheduleRetail = 0;

bool bLastSetScheduleForce = false;

int32 LastIdealScheduleStamp = 0;

void ThinkSet(const TCHAR* Function, double Delay);

static const TCHAR* NpcInitThinkFunction();    // `0x10273aa0`

static const TCHAR* StartNpcThinkFunction();   // `LAB_1000f4e8`

/** SEAM for `CAI_MoveProbe::TraceHull` `0x102e7880` on `m_pMoveProbe`. No hull sweep stands here:
 *  answers the found-floor arm and leaves the origin where it was. */
bool MoveProbeFloorDrop(FVector& InOutOriginUnits);

/** `0x1027be60` — give-up restore. */
void RestoreGiveUp();

/** SEAM for `0x102ee1e0`. No navigator: answers false (failure). */
bool RefindPostRestorePath();

/** Install a raw registrar id through `0x10280de0` / `0x102ae750`. An id the registry lacks goes
 *  through story 25's miss arm (`IDLE_STAND`). */
void InstallScheduleRetail(int32 RawId, bool bForce);

/** `CAI_Motor::m_IdealYaw` (motor `+0x34`), the yaw `NPCInit` `0x10273390` seeds from the absolute
 *  angles (`±180` under the motor's animation-movement latch). SEAM: `IElysiumNpcMotor` keeps no
 *  ideal-yaw word, so the seed is held here and nothing reads it yet. */
float MotorIdealYaw = 0.f;

/** `m_bIsBCCTargetable` (`+0x1480`), a `CBaseCombatCharacter` byte. `CAI_BaseNPCTroika::NPCInit`
 *  sets it from the stat template and five species clear it; `CCineNPC::Spawn` (`0x101a6f10`,
 *  `101a7029`) stores 0. Carried here rather than on the Troika since story 5 fold A3, because a
 *  director writes it and `IsBccTargetable` reads it off any `+0x94` NPC. */
bool bIsBccTargetable = false;

/** `m_bIsAlive` (`+0x1481`), the combat-character liveness byte `NPCInit` writes independently of
 *  `LifeState`. Payphone clears it after the base wrote 1; `CCineNPC::Spawn` stores 0
 *  (`101a7031`). */
bool bNpcIsAlive = false;
