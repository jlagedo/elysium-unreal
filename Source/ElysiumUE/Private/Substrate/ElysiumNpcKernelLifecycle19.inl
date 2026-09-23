// Story 29e, family **Lifecycle19** — slot 420 `NPCInit`, slot 422 `StartNPC`, slot 130 `OnRestore`
// and their species overrides.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`. The three SLOT bodies are
// declared by the generator; this family DEFINES them. What lands here is the base-line bodies
// beneath the Troika overrides, every species arm, and the words and seams those arms need.
//
// Definitions: `Substrate/ElysiumNpcKernelLifecycle19.cpp` (spine and dispatchers),
// `…Lifecycle19_2.cpp` (species `NPCInit`). Tests: `Tests/ElysiumNpcKernelLifecycle19Tests.cpp`.
// Walked prose: `docs/vtmb/npc-ai/lifecycle.md` § "Story 29e, family Lifecycle19".
//
// --- What this family is ---------------------------------------------------------------------------
//
// 42 rows in `Lifecycle19.tsv`, three shapes:
//
//   * **Six spine bodies.** `CAI_BaseNPC::NPCInit` `0x10273390` (`BaseNPCInit`) under Troika
//     `0x1029a0b0` (`NPCInit`, slot 420); `CAI_BaseNPC::StartNPC` `0x10273ad0` (`BaseStartNPC`)
//     under Troika `0x1029a8b0` (`StartNPC`, slot 422); `CAI_BaseNPC::OnRestore` `0x1027bf50`
//     (`BaseOnRestore`) under Troika `0x102998c0` (`OnRestore`, slot 130).
//   * **Species `NPCInit` arms**, each a private helper the slot-420 dispatcher selects on the
//     retail ADDRESS. Retail's species bodies call the body they replace through a direct
//     non-virtual thunk, so every chain call here is made under `FSpeciesDispatchScope(*this, 420)`.
//   * **Species `StartNPC` / `OnRestore` arms** on slots 422 and 130, the same shape.
//
// Slot 420 also has four census overrides this family's TSV does not list (`0x10387140`
// HumanCombatant, `0x1037e240` Guard1, `0x10388b30` Hunter, `0x103dd800` Yukie). Cop and
// GhoulCroucher *call* HumanCombatant, so that body is a helper; the other three are 39–60 byte
// replacements that would otherwise silently take Troika. They are ported so the dispatcher is
// complete.
//
// --- `.rdata` cells, read out of the pinned `vampire.dll` ------------------------------------------
//
// File offset = address − `0x10000000`. The corpus holds none of these.
//
//   `_DAT_10449280` = **1.0** (DOUBLE) — `curtime <= 1.0` is the map's first second
//   `_DAT_104493d0` = **0.1** (DOUBLE)     `_DAT_1044c3a8` = **180.0f**
//   `_DAT_104454c4` = **0.0f**             `_DAT_10450aa0` = **4.0f**
//   `_DAT_10449258` = **3.0f**             `_DAT_104bc690` = **2.3** (DOUBLE)
//   `_DAT_104454d0` = **0.5f**             `_DAT_104704c0` = **512.0** (DOUBLE)
//   `_DAT_104d0080` = **2.0943951023931953** (DOUBLE) — 120°, FOV `cos` = **−0.5**
//   `_DAT_104c6148` = **2.0f**             `_DAT_1044e664` = **10.0f**
//   `_DAT_104a9300` = **2.0f**             `_DAT_104ada44` = **2.3f**
//
// --- READING.md corrections applied here, not re-derived -------------------------------------------
//
//   * `0x10360ce0`: `+0x66d0` is `m_iLastJumpPositionIdx` (Motor's `LastJumpPositionIdx`);
//     `m_pHintNode` is the inherited `+0x5ddc`.
//   * `0x1036b050`: `m_vLastTeleportPosition` `+0x66bc`, `m_fLastTeleportTime` `+0x66c8`,
//     `m_fLastJumpTime` `+0x66cc`, `m_fFacingTime` `+0x66d0`, `m_bCenterStored` `+0x66e8`.
//   * `0x103a25a0`: pre-death bounds `+0x6660`/`+0x666c` (SpeciesMisc10's
//     `PedestrianPreDeathMinsUnits` / `Maxs`); origin/angles `+0x62a8`/`+0x62b4`
//     (`InitialPosition` / `InitialAngles`). `m_eLevelResetType` `+0x667c`.
//   * `0x103cac20` opens with `SetEnemy(NULL)` and `m_hClosestPlayer = -1`. `+0x66cc`/`+0x66d0`
//     are the teleport-distance floor and BOTH accumulate (`+=`). `_DAT_104704c0` is a DOUBLE 512.
//   * `0x10273ad0` / `0x10369930` call `ThinkSet` on BOTH arms of the first-second test; only
//     the stamp differs. Capability term is mask **4** (bit 2). Camera `m_target` is gated
//     `!= 0`.
//   * `0x10375c80` ends in a tail `JMP` to `0x10376c10` (hostile-enemy recount).
//   * `0x1036f100` / `0x1036f900` run `SetChangType` BEFORE the ChangBros chain.
//
// --- Retail defects reproduced ---------------------------------------------------------------------
//
//   1. Last flyer spawned decides the node-graph hull (`DAT_109340d8`).
//   2. Werewolf teleport-distance floor grows on every `NPCInit` / `OnRestore`.
//   3. FrenzyShadow ORs `0x40` into a possibly-null weapon (crash guard: named refusal).
//   4. Camera deletes itself when the engine query refuses.
//   5. GhoulCroucher pushes `"CNPC_VWerewolf::NPCInit"` on the scope trace.
//   6. Tzimisce `StartNPC` re-arms the think the base just armed.

// -------------------------------------------------------------------------------------------------
// Process-wide words
// -------------------------------------------------------------------------------------------------

/** `DAT_10937cf1` — the in-`NPCInit` global. Process-wide in retail, so process-wide here. */
static bool& InNpcInit();

/** `DAT_109340d8` — node-graph hull index. Four writers, no restore. Retail defect 1. */
static int32& NodeGraphHullIndex();

static constexpr int32 HullIndexGargoyle = 0x0e;     // `0x103785f0`
static constexpr int32 HullIndexHengeyokai = 0x12;   // `0x1037fa70`
static constexpr int32 HullIndexManBat = 0x14;       // `0x1038b070`
static constexpr int32 HullIndexSheriffMan = 0x15;   // `0x103ae6c0`

/** `DAT_106c994c` — out-of-range ped-link index counter. */
static int32& NodeIndexErrorCount();

/** `DAT_10938040` — fleshpile Andrei cache. Filled by `CNPCMaker_Fleshpile::OnRestore`. */
static FElysiumEntityHandle& FleshpileAndreiSingleton();

/** `DAT_1093bd34` — Ming Xiao tentacle cached handle, invalidated on restore. */
static FElysiumEntityHandle& MingXiaoTentacleCache();

// -------------------------------------------------------------------------------------------------
// `.rdata` constants, named once
// -------------------------------------------------------------------------------------------------

static constexpr double MapFirstSecond = ElysiumNpcTunables::OneDouble;
static constexpr double NpcInitThinkDelay = ElysiumNpcTunables::TenthDouble;
static constexpr float MotorYawHalfTurn = ElysiumNpcTunables::OneEighty;
static constexpr float TeleportMoveTimerFloor = ElysiumNpcTunables::Zero;
static constexpr float TeleportMoveTimerExtra = 4.f;     // `_DAT_10450aa0`
static constexpr float SpeciesShunWindowSeconds = 3.f;   // `_DAT_10449258`
static constexpr double ManBatFlapDelaySeconds = 2.3;    // `_DAT_104bc690`
static constexpr double WerewolfTeleportFloorSquare = 512.0; // `_DAT_104704c0`
static constexpr float HullCentreHalf = ElysiumNpcTunables::Half;
static constexpr double WerewolfFieldOfViewRadians = 2.0943951023931953; // `_DAT_104d0080`
static constexpr float SheriffManJumpGravity = 2.f;      // `_DAT_104c6148`
static constexpr float AsianVampireJumpGravity = 2.f;    // `_DAT_104a9300`
// `_DAT_104ada44` ChangBros jump gravity is family SpeciesLifecycle10's `ChangBrosJumpGravity`.
static constexpr float StartNpcDelayMin = 0.1f;
static constexpr float StartNpcDelayMax = 0.4f;
static constexpr float ShootAtHintRearmMin = 2.f;
static constexpr float ShootAtHintRearmMax = 2.5f;
static constexpr float SwarmDistTooFar = 65535.f;        // `0x477fff00`
static constexpr float BaseInitDistTooFar = 1024.f;
static constexpr float BaseInitDistLookUnits = 3072.f;
static constexpr float FarSightDistTooFar = 1.0e9f;
static constexpr float FarSightDistLookUnits = 6000.f;
static constexpr float WerewolfSeekDistBaseUnits = 4096.f;
static constexpr float WerewolfHearingScalarBase = 3.f;
static constexpr int32 LawThresholdNever = 999999;
static constexpr float FrenzyShadowSpeedScale = 8.f;
static constexpr uint32 FrenzyShadowFrenziedFlags = 0x5ddfu;
static constexpr int32 WolfMorphRetailState = 0xc;
static constexpr int32 WolfMorphActivity = 0x1145;
static constexpr int32 NpcInitCollisionMask = 0x0202400b;
static constexpr int32 NpcInitAddFlags = 0x12000;
static constexpr float StartNpcFloorDropUnits = -256.f;
static constexpr int32 RestoreTaskIndexCeiling = 0x29;
static constexpr uint32 CapabilitySpawnEquip = 0x200000u;
static constexpr uint32 CapabilityNoFloorDrop = 4u;
static constexpr int32 SpawnFlagNoFloorDrop = 4;
static constexpr int32 SpawnFlagPreAimed = 0x80;
static constexpr int32 SpawnFlagFarSight = 0x100;
static constexpr int32 TeleportForcedScheduleRetailId = 0xfe;
static constexpr int32 StartNpcGoalEntityRetailId = 3;
static constexpr int32 StartNpcAmbushRetailId = 0x2d;
static constexpr int32 ZombieCrawlScheduleRetailId = 0x161;
static constexpr float CameraOccludedDelayNormal = 3.4f;
static constexpr float CameraOccludedDelayCover = 10.f;
static constexpr float CameraEnemyStoreInterval = 0.5f;
static constexpr float RunnerAttackExtentX = 50.f;
static constexpr float RunnerAttackExtentY = 50.f;
static constexpr float RunnerAttackExtentZ = 82.f;
static constexpr float NeverThinkSentinel = 3.402823466e+38f; // `0x7f7fffff`

// -------------------------------------------------------------------------------------------------
// Words this family needed that no earlier family declared
// -------------------------------------------------------------------------------------------------

/** `m_bIsBCCTargetable` (`+0x1480`). Family Senses10's `IsBccTargetable` is a CANDIDATE seam
 *  answering true; this is THIS NPC's own byte, written by `NPCInit`. */
bool bIsBccTargetable = false;

/** `m_bIsAlive` (`+0x1481`), the combat-character liveness byte `NPCInit` writes independently of
 *  `LifeState`. Payphone clears it after the base wrote 1. */
bool bNpcIsAlive = false;

/** `m_bCineScriptHidden` (`+0x5d78`). A DISTINCT byte from `m_bScriptHidden` (`+0x0f4`, the word
 *  `0x100b5190` reads and this port carries as `FElysiumEntity::bHidden`) and from `m_fEffects`
 *  (`+0x19c`). The field ledger gives it exactly TWO accesses in the whole image, both this clear
 *  in `CAI_BaseNPC::NPCInit` (`0x10273390` and its thunk): retail writes it and never reads it. */
bool bCineScriptHidden = false;

/** `CAI_BaseNPCTroika`'s four discipline bit words, cleared whole by `NPCInit` at `1029a589`. No
 *  earlier family declared them: this runtime's discipline activity lives in
 *  `FElysiumDisciplineState::EndTime`, and these are the retail masks beside it. */
int32 DisciplineFlags = 0;        // +0x0eb0 `m_iDisciplineFlags`
int32 DisciplineFlags2 = 0;       // +0x0eb4 `m_iDisciplineFlags2`
int32 DisciplinePreFlags = 0;     // +0x0eb8 `m_iDisciplinePreFlags`
int32 DisciplinePreFlags2 = 0;    // +0x0ebc `m_iDisciplinePreFlags2`

/** `m_flSpeedScale` (`+0x1488`). FrenzyShadow writes `8.0`. The animation driver's copy is a
 *  different object; this is the kernel word. */
float NpcSpeedScale = 1.f;

/** `m_flFieldOfView` (`+0x1574`), the cone half-angle cosine. Default `0.2` is Troika's; Werewolf
 *  `NPCInit` writes `cos(120°) = −0.5`. */
float FieldOfViewDot = 0.2f;

/** `CNPC_VPedestrian::m_bFirstThink` (`+0x6678`) and `CNPC_VTaxiDriver::m_bFirstThink` (`+0x6660`).
 *  Different offsets, two members. */
bool bPedestrianFirstThink = false;
bool bTaxiFirstThink = false;

/** `CNPC_VPedestrian::m_eLevelResetType` (`+0x667c`). */
int32 PedestrianLevelResetType = 0;

/** `CNPC_VGargoyle` species words at `NPCInit`. */
FElysiumEntityHandle GargoylePillarTarget;   // +0x667c
int32 GargoyleDoingGibDeath = 0;             // +0x6684
int32 GargoyleCanKnockback = 0;              // +0x6688

/** `CNPC_VHengeyokai` species words. `SpeciesShunnedFindCount` already carries `+0x6678`. */
bool bHengeyokaiJustFoundFish = false;       // +0x667c
bool bHengeyokaiInSharkForm = false;         // +0x6694
double HengeyokaiShunnedFishTimer = 0.0;     // +0x6670

/** `CNPC_VManBat` species words. `ManBatFlapTimer` and `bHasPlayedFlyBySound` already exist. */
bool bManBatHasScaredMinions = false;        // +0x66b0
double ManBatFlyTimer = 0.0;                 // +0x667c

/** `CNPC_VTzimisce` species words. `PickupTarget` / `PathMode` / `SpeciesShunnedFindCount` exist. */
bool bTzimisceFirstEnemy = false;            // +0x6689
bool bTzimisceJustFoundBody = false;         // +0x66bc
double TzimiscePounceCheckTimer = 0.0;       // +0x66ac
double TzimisceShunnedBodyTimer = 0.0;       // +0x66b0

/** `CNPC_VZombie`. */
bool bZombieNeedsCrawlOutOfGround = false;   // +0x667c
double ZombieGrappleReadyTimer = 0.0;        // +0x66d8

/** `CNPC_VGhoulCroucher::m_bSpawnDisturbed` (`+0x6664`). `bGhoulSpawnBurning` is Damage's. */
bool bGhoulSpawnDisturbed = false;

/** `CNPC_VAsianVampire::m_bPathBlocked` (`+0x66d4`) and `m_bSuppressRanged` (`+0x66e8`). */
bool bAsianVampirePathBlocked = false;
bool bAsianVampireSuppressRanged = false;

/** SheriffMan flags written before the VampireBoss chain. `SheriffLastTeleportPosition` exists. */
bool bSheriffTeleporting = false;            // +0x66e4
bool bSheriffDead = false;                   // +0x66e5
bool bSheriffActivated = false;              // +0x66e6

/** Ledger `CNPC_VAnimal::m_bPlayerAttackedMe` at `+0x6660`. TaxiDriver's `bTaxiFirstThink` and
 *  Pedestrian's pre-death bounds are different classes at the same offset. */
bool bPlayerAttackedMe = false;

/** `CNPC_VGuard1::m_fHatesPlayer` (`+0x6660`) is family SpeciesMisc10's `bGuard1HatesPlayer` — a
 *  THIRD species' word at that offset, and the one `CNPC_VGuard1::NPCInit` (`1037e24a`) clears
 *  first. Not redeclared here. */

/** `CNPC_VWerewolf` words `0x103cac20` clears that no earlier family declared. */
bool bWerewolfPlayFrustration = false;     // +0x66a9 `m_bPlayFrustration`
int32 WerewolfMoveHintSearchStart = 0;     // +0x66b8 `m_pMoveHintSearchStart`, retail's NULL is 0
int32 WerewolfWord66f8 = 0;                // +0x66f8 (retail name unrecovered)
int32 WerewolfWord66fc = 0;                // +0x66fc (retail name unrecovered)

/** SabbatLeader words Damage / Misc / Schedule already carry some of; these are the rest.
 *  `bSabbatLeaderActivated` (`+0x66b8`) is family State19's. */
int32 SabbatLeaderRouteFailCount = 0;        // +0x66bc — Schedule already has FailureType at +0x66c0
double SabbatLeaderLastSplashTime = 0.0;     // +0x66c8
bool bSabbatLeaderDiving = false;
bool bSabbatLeaderLargeSplash = false;
bool bSabbatLeaderParticleSpawned = false;
int32 SabbatLeaderJumpBloodBalance = 0;
bool bSabbatLeaderLastAttackWasNova = false;
bool bSabbatLeaderTrackPlayer = false;

/** `CNPC_VPlaceholder` zeros `+0x62ec` before the base. `CurrentSpotIndex` is that word. */

/** FrenzyShadow retail state `0xb` has no `EElysiumNpcState` member; the write is the raw
 *  `WriteIdealStateRetail` / `SetState(0xb)`. */

/** Camera engine-query seam: slot 74 of `DAT_1070ba0c`. Default admits. Tests may refuse. */
bool bCameraEngineQueryAnswer = true;
int32 CameraEngineQueries = 0;
int32 CameraSelfRemovals = 0;

/** `m_flSeekDistBase` (`+0x63b4`) is bound to `AuthoredVision`; Werewolf writes 4096.0 over it. */

/** `m_fEffects` (`+0x19c`) is cleared by Troika `NPCInit` at `1029a0c0`. This runtime models that
 *  word's one live bit, `EF_NODRAW`, as `FElysiumEntity::bHidden`, so the clear UNHIDES — which is
 *  what retail does, before the base body and before `CNPC_VZombie::NPCInit` re-hides itself. */

// -------------------------------------------------------------------------------------------------
// ThinkSet seam — `CBaseEntity::ThinkSet`
// -------------------------------------------------------------------------------------------------

/** Recorded think-function identity. Retail function pointers; this runtime has no think-fn
 *  table, so the name is the observable. `nullptr` / empty is `ThinkSet(NULL)`. */
FString ThinkFunctionName;
double ThinkSetDelay = 0.0;
int32 ThinkSetCalls = 0;
int32 NpcInitInlineThinkCalls = 0;

void ThinkSet(const TCHAR* Function, double Delay);

static const TCHAR* NpcInitThinkFunction();    // `0x10273aa0`
static const TCHAR* StartNpcThinkFunction();   // `LAB_1000f4e8`

// -------------------------------------------------------------------------------------------------
// Seams and counted helpers
// -------------------------------------------------------------------------------------------------

int32 BaseInitAnimatingResets = 0;       // `ClearAllClientRagdolling` + `0x10095be0` + `0x1008f540`
int32 DelayedConditionListClears = 0;    // `0x102cc7e0` twice
int32 BaseInitTailCalls = 0;             // `0x10274ca0` `SetDefaultEyeOffset`
int32 BaseInitChoreoClears = 0;          // `0x10270180(NULL, false)`
int32 TroikaInitInterestingPlaceReleases = 0; // `0x102b53d0(curtime)`
int32 TroikaInitAlertLevelResets = 0;    // `0x102b5dc0(this, 0)`
int32 StatListSeeds = 0;                 // `CVStatList_t::Set(0xf)` / `SetBase(0xc)`
int32 NpcInitHealthSeeds = 0;            // `m_iHealth = ftol(cvar)`
int32 DispositionSeeds = 0;              // `m_nCurrDisposition = DAT_10924984`
int32 SpawnEquipRequests = 0;
int32 FloorDropPerformed = 0;
int32 FloorDropSkipped = 0;
int32 FloorDropWarnings = 0;
int32 NavigationGoalClears = 0;          // `0x102ee270`
int32 PostRestorePathRefinds = 0;        // `0x102ee1e0`
int32 RestorePlaceScans = 0;             // `0x102db5e0`
int32 RestorePlaceRejections = 0;        // `0x10299a80`
int32 PatrolPathRevalidations = 0;       // `0x1029f610`
int32 PatrolPathReleases = 0;            // `0x1029f5d0`
int32 PedLinkRebinds = 0;
int32 GhoulBurningParticleCreates = 0;
int32 Flag2Removals = 0;                 // `RemoveFlag2(4)`
int32 FrenzyShadowWeaponFlagOrs = 0;     // the unconditional `|= 0x40`
int32 FrenzyShadowNullWeaponFaults = 0;  // crash-guarded retail fault
int32 HideActiveWeaponCalls = 0;         // slot 66 Hide on the ACTIVE WEAPON
int32 SetScheduleRetailCalls = 0;
int32 LastSetScheduleRetail = 0;
bool bLastSetScheduleForce = false;
int32 LastIdealScheduleStamp = 0;
int32 TzimisceStartNpcRearms = 0;
int32 ExpressionMapResets = 0;           // `0x103b9f50`
int32 FollowerBossOnStartCalls = 0;      // seam for `0x102c44e0`
int32 InventoryDestroys = 0;             // `Inventory_Destroy`
/** SEAM for `CBaseEntity::Relink` (`0x1001514a` through `0x101cf600`), which `CNPC_VPedestrian`'s
 *  level reset runs after it resizes the hull. No spatial partition stands at this tier; counted. */
int32 RestoreRelinkCalls = 0;

/** SEAM for `CAI_MoveProbe::TraceHull` `0x102e7880` on `m_pMoveProbe`. No hull sweep stands here:
 *  answers the found-floor arm and leaves the origin where it was. */
bool MoveProbeFloorDrop(FVector& InOutOriginUnits);

/** `0x102db5e0` — the interesting place whose marker table names this NPC, or `INDEX_NONE`. */
int32 FindInterestingPlaceHoldingMe() const;

/** `0x10299a80` — the restore-time check of that place, with retail's two `DevMsg` refusals. */
void ValidateRestoredInterestingPlace();

/** `0x1027be60` — give-up restore. */
void RestoreGiveUp();

/** SEAM for `0x102ee1e0`. No navigator: answers false (failure). */
bool RefindPostRestorePath();

/** `0x1029f340` — the null guard plus `ActivityNameToId` (`0x10412520`), which family Hints already
 *  stands as `ActivityIdForName`. */
int32 ResolveCombatStartActivity(const FString& Name) const;

/** `0x102c4430("")` — SetFollowerBoss then `m_sFollowerBoss = NULL`. */
void ClearFollowerBossName();

/** SEAM for `0x102c44e0`. Squad's `SetFollowerBossName` is `0x102c4470` and already states this
 *  row is unported. Counted; handle stays unwritten. Three searches: address `0x102c44e0` is an
 *  overlay row of another family, `SetFollowerBoss` identifier is absent, `+0x647c` is
 *  `FollowerBoss` with no writer. */
void SetFollowerBossByAuthoredName(const FString& Name);

/** Install a raw registrar id through `0x10280de0` / `0x102ae750`. An id the registry lacks goes
 *  through story 25's miss arm (`IDLE_STAND`). */
void InstallScheduleRetail(int32 RawId, bool bForce);

void SeedStatListOnNpcInit();
void SeedCriminalLevelWitnessed();
void SpawnEquipLoadout();
void HideActiveWeaponIfAny();
int32 FrenzyShadowHostileRecount();
void WerewolfRearm();

/** The four `CVFeatList_t` field getters `NPCInit` and `CNPC_VZombie::NPCInit` read off the
 *  process-global tuning record (`0x10739d08`), which `0x101e6310` fills from `Rules.txt`. They are
 *  NOT cvars: `0x101e8c50` is `+0x288 Npc_Combat_Info/OccludedDelayNormal` (image default 0.5),
 *  `0x101e8c70` is `+0x28c .../OccludedDelayCover` (5.0), `0x101e8c30` is
 *  `+0x284 .../FreeKnowledgeDuration` (0.25, the word `FElysiumNpcEnemyMemory` already carries) and
 *  `0x101e8bf0` is `+0x27c Zombie_Grapple_Info/DelayInitial` (20.0). */
float TuningOccludedDelayNormal() const;
float TuningOccludedDelayCover() const;
float TuningEnemyStoreInterval() const;
float TuningZombieGrappleReadyInterval() const;

// -------------------------------------------------------------------------------------------------
// Slot 420
// -------------------------------------------------------------------------------------------------

void BaseNPCInit();     // `0x10273390`
void TroikaNPCInit();   // `0x1029a0b0`
bool SpeciesNPCInit();

void PayphoneNPCInit();             // `0x101aab90`
void AndreiBloodNPCInit();          // `0x1035cec0`
void AsianVampireNPCInit();         // `0x10360ce0`
void BachNPCInit();                 // `0x10363940`
void BatSwarmNPCInit();             // `0x103673b0`
void CameraNPCInit();               // `0x103692c0`
void ChangBrosNPCInit();            // `0x1036b050`
void ChangBrosBladeNPCInit();       // `0x1036f100`
void ChangBrosClawNPCInit();        // `0x1036f900`
void CopNPCInit();                  // `0x10372b00`
void FrenzyShadowNPCInit();         // `0x10375c80`
void GargoyleNPCInit();             // `0x103785f0`
void GhoulCroucherNPCInit();        // `0x1037b290`
void Guard1NPCInit();               // `0x1037e240` — not a TSV row; dispatcher completeness
void HengeyokaiNPCInit();           // `0x1037fa70`
void HumanCombatantNPCInit();       // `0x10387140` — helper for Cop / Hunter / Yukie
void HunterNPCInit();               // `0x10388b30`
void ManBatNPCInit();               // `0x1038b070`
void NewscasterNPCInit();           // `0x103a0420`
void PedestrianNPCInit();           // `0x103a2570`
void PlaceholderNPCInit();          // `0x103a4350`
void PlayerControllerNPCInit();     // `0x103a4580`
void SabbatLeaderNPCInit();         // `0x103a6d40`
void SheriffManNPCInit();           // `0x103ae6c0`
void SheriffSwarmNPCInit();         // `0x103b2360`
void TaxiDriverNPCInit();           // `0x103b35c0`
void TzimisceNPCInit();             // `0x103b91d0`
void TzimisceHeadClawNPCInit();     // `0x103c1c80`
void VampireBossNPCInit();          // `0x103c5840`
void WerewolfNPCInit();             // `0x103caef0`
void WolfMorphNPCInit();            // `0x103dce00`
void YukieNPCInit();                // `0x103dd800`
void ZombieNPCInit();               // `0x103defc0`

// -------------------------------------------------------------------------------------------------
// Slot 422
// -------------------------------------------------------------------------------------------------

void BaseStartNPC();    // `0x10273ad0`
void TroikaStartNPC();  // `0x1029a8b0`
bool SpeciesStartNPC();
void CameraStartNPC();     // `0x10369930`
void TzimisceStartNPC();   // `0x103b9270`

// -------------------------------------------------------------------------------------------------
// Slot 130
// -------------------------------------------------------------------------------------------------

void BaseOnRestore(bool bFromLoad);    // `0x1027bf50`
void TroikaOnRestore(bool bFromLoad);  // `0x102998c0`
bool SpeciesOnRestore(bool bFromLoad);
void MingXiaoTentacleOnRestore(bool bFromLoad);  // `0x1039f000`
void PedestrianOnRestore(bool bFromLoad);        // `0x103a25a0`
void TzimisceRunnerOnRestore(bool bFromLoad);    // `0x103c3c40`
void WerewolfOnRestore(bool bFromLoad);          // `0x103cabf0`
