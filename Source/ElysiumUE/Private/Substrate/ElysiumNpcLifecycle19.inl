// Story 29e, family **Lifecycle19** — slot 420 `NPCInit`, slot 422 `StartNPC`, slot 130 `OnRestore`
// and their species overrides.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`. The three SLOT bodies are
// declared by the generator; this family DEFINES them. What lands here is the base-line bodies
// beneath the Troika overrides, every species arm, and the words and seams those arms need.
//
// Definitions: `Substrate/ElysiumNpcLifecycle19.cpp` (spine and dispatchers),
// `…Lifecycle19_2.cpp` (species `NPCInit`). Tests: `Tests/ElysiumNpcKernelLifecycle19Tests.cpp`.
// Walked prose: `docs/vtmb/npc-ai/lifecycle.md` § "Story 29e, family Lifecycle19".
//
// --- What this family is ---------------------------------------------------------------------------
//
// 42 rows in `Lifecycle19.tsv`, three shapes:
//
//   * **Six spine bodies.** `CAI_BaseNPC::NPCInit` `0x10273390` (`FElysiumNpcBase::NPCInit`) under Troika
//     `0x1029a0b0` (`NPCInit`, slot 420); `CAI_BaseNPC::StartNPC` `0x10273ad0` (`FElysiumNpcBase::StartNPC`)
//     under Troika `0x1029a8b0` (`StartNPC`, slot 422); `CAI_BaseNPC::OnRestore` `0x1027bf50`
//     (`FElysiumNpcBase::OnRestore`) under Troika `0x102998c0` (`OnRestore`, slot 130).
//   * **Species `NPCInit` bodies**, each the body of its class's C++ override (story 5 step 3; the
//     controller line's three stay slot-420 census arms until step 7). Retail's species bodies call
//     the body they replace through a direct non-virtual thunk, spelled as a direct call here.
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

/** `DAT_106c994c` — out-of-range ped-link index counter. */
static int32& NodeIndexErrorCount();

/** `DAT_10938040` — fleshpile Andrei cache. Filled by `CNPCMaker_Fleshpile::OnRestore`. */
static FElysiumEntityHandle& FleshpileAndreiSingleton();

// -------------------------------------------------------------------------------------------------
// `.rdata` constants, named once
// -------------------------------------------------------------------------------------------------

static constexpr float TeleportMoveTimerFloor = ElysiumNpcTunables::Zero;
static constexpr float TeleportMoveTimerExtra = 4.f;     // `_DAT_10450aa0`
static constexpr float SpeciesShunWindowSeconds = 3.f;   // `_DAT_10449258`
static constexpr float ShootAtHintRearmMin = 2.f;
static constexpr float ShootAtHintRearmMax = 2.5f;
static constexpr int32 LawThresholdNever = 999999;
static constexpr float FrenzyShadowSpeedScale = 8.f;
static constexpr uint32 FrenzyShadowFrenziedFlags = 0x5ddfu;
static constexpr int32 WolfMorphRetailState = 0xc;
static constexpr int32 WolfMorphActivity = 0x1145;
static constexpr float StartNpcFloorDropUnits = -256.f;
static constexpr uint32 CapabilitySpawnEquip = 0x200000u;
static constexpr int32 TeleportForcedScheduleRetailId = 0xfe;
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

/** `CNPC_VGuard1::m_fHatesPlayer` (`+0x6660`) is family SpeciesMisc10's `bGuard1HatesPlayer` — a
 *  THIRD species' word at that offset, and the one `CNPC_VGuard1::NPCInit` (`1037e24a`) clears
 *  first. Not redeclared here. */

/** `CNPC_VPlaceholder` zeros `+0x62ec` before the base. `CurrentSpotIndex` is that word. */

/** FrenzyShadow retail state `0xb` has no `EElysiumNpcState` member; the write is the raw
 *  `WriteIdealStateRetail` / `SetState(0xb)`. */

/** `m_flSeekDistBase` (`+0x63b4`) is bound to `AuthoredVision`; Werewolf writes 4096.0 over it. */

/** `m_fEffects` (`+0x19c`) is cleared by Troika `NPCInit` at `1029a0c0`. This runtime models that
 *  word's one live bit, `EF_NODRAW`, as `FElysiumEntity::bHidden`, so the clear UNHIDES — which is
 *  what retail does, before the base body and before `CNPC_VZombie::NPCInit` re-hides itself. */

// -------------------------------------------------------------------------------------------------
// ThinkSet seam — `CBaseEntity::ThinkSet`
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Seams and counted helpers
// -------------------------------------------------------------------------------------------------

int32 TroikaInitInterestingPlaceReleases = 0; // `0x102b53d0(curtime)`
int32 TroikaInitAlertLevelResets = 0;    // `0x102b5dc0(this, 0)`
int32 StatListSeeds = 0;                 // `CVStatList_t::Set(0xf)` / `SetBase(0xc)`
int32 NpcInitHealthSeeds = 0;            // `m_iHealth = ftol(cvar)`
int32 DispositionSeeds = 0;              // `m_nCurrDisposition = DAT_10924984`
int32 SpawnEquipRequests = 0;
int32 RestorePlaceScans = 0;             // `0x102db5e0`
int32 RestorePlaceRejections = 0;        // `0x10299a80`
int32 PatrolPathRevalidations = 0;       // `0x1029f610`
int32 PatrolPathReleases = 0;            // `0x1029f5d0`
int32 PedLinkRebinds = 0;
int32 FrenzyShadowWeaponFlagOrs = 0;     // the unconditional `|= 0x40`
int32 FrenzyShadowNullWeaponFaults = 0;  // crash-guarded retail fault
int32 FollowerBossOnStartCalls = 0;      // seam for `0x102c44e0`
int32 InventoryDestroys = 0;             // `Inventory_Destroy`

/** `0x102db5e0` — the interesting place whose marker table names this NPC, or `INDEX_NONE`. */
int32 FindInterestingPlaceHoldingMe() const;

/** `0x10299a80` — the restore-time check of that place, with retail's two `DevMsg` refusals. */
void ValidateRestoredInterestingPlace();

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

void SeedStatListOnNpcInit();
void SeedCriminalLevelWitnessed();
void SpawnEquipLoadout();
int32 FrenzyShadowHostileRecount();

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

void TroikaNPCInit();   // `0x1029a0b0`
bool SpeciesNPCInit();

void FrenzyShadowNPCInit();         // `0x10375c80`
void PlayerControllerNPCInit();     // `0x103a4580`
void WolfMorphNPCInit();            // `0x103dce00`

// -------------------------------------------------------------------------------------------------
// Slot 422
// -------------------------------------------------------------------------------------------------

void TroikaStartNPC();  // `0x1029a8b0`

// -------------------------------------------------------------------------------------------------
// Slot 130
// -------------------------------------------------------------------------------------------------

void TroikaOnRestore(bool bFromLoad);  // `0x102998c0`
