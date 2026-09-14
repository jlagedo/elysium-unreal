// Story 29c-1, family **EntityChain** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelEntityChain.cpp` and the tests in
// `Tests/ElysiumNpcKernelEntityChainTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is the 57 UNNAMED bodies of the entity chain BELOW the NPC — `CBaseEntity`,
// `CBaseAnimating`, `CBaseFlex`, `CBaseCombatCharacter`, `CBasePlayer`, `CCineNPC`, `CDialog`,
// `CGlobalEntityList` and `CPayphone` addresses under `0x101d0000` that 29a's naming pass could not
// name. Reading the decompiled C SETTLED most of them, and where it did the port method carries the
// recovered name and the report says so; where it did not, the `FUN_<address>` spelling stands,
// because inventing a name for an unrecovered concern is the guess `CLAUDE.md` forbids.
//
// THREE STANDING FACTS OF THIS FAMILY:
//
//  1. **Half of it is `CBasePlayer`.** `+0x19b8`..`+0x22b0` are player words; retail's `this` on
//     those bodies IS the player. This leaf is an NPC, so every one of them goes through
//     `ChainPlayer()` below and runs over the ONE `FElysiumPlayer` this runtime stands. Where the
//     port's player ALREADY carries the word — `Law`, `Police`, `ScareQueue`, `Observer` — the body
//     reads and writes that member and no second copy is stood beside it.
//  2. **There is no physics object, no studio header and no edict here.** Every body that reads one
//     asks a seam declared below that answers nothing and names the retail call it stands for.
//  3. **Two of its data structures are already ported by family Anim** — the gesture-layer table
//     (`AnimOverlay`, `+0x0734`), the flinch table (`Flinch`, `+0x07f4`), the flex weights
//     (`FlexWeight`, `+0x0858`) and the scene-event queue (`SceneEvents`, `+0x0a58`). The four slot
//     bodies of this family that write them (268, 271, 266, 279, 285, 287) drive family Anim's
//     members and its named methods rather than a second table.

// --- The `CBasePlayer` half of the chain ---------------------------------------------------------

/** Retail's `this` for every `+0x19b8`..`+0x22b0` body below. `CBasePlayer` sits on the same
 *  `CBaseCombatCharacter` line this NPC does, so the shared vtable carries both, but the WORDS are
 *  the player's. This runtime stands exactly one `FElysiumPlayer`, and it is the world's; null on a
 *  worldless probe or a headless world, which every body below has retail's own null arm for. */
FElysiumPlayer* ChainPlayer() const;

// --- Words 29b did not declare -------------------------------------------------------------------
//
// Every one sits OUTSIDE the hand-written shape map's band (`ElysiumNpcKernelShapeMap.cpp` binds
// `0x1a40`..`0x665a`, the words `CAI_BaseNPC` itself carries). `docs/vtmb/npc-kernel/layout.md`
// types and names the chain half; the `CBasePlayer` half is named by that class's own datamap
// (`vtmb_fields CBasePlayer`), which is where this family recovered it.

// +0x01b0 `m_NetworkChangeState` (`CEntityNetworkChangeState`, `layout.md` `+0x01b0`) — an
// 8-byte record the SDK does not declare: a bool at +0, `m_bChanged` at +1, a second bool at +2, a
// short interval at +4 and a short countdown at +6. Slot 88 (`0x10026b50`) asks whether a send is
// due; slot 89 — this family's — clears the two flag bytes. SEAM-ADJACENT: nothing in this runtime
// networks an entity, so the interval/countdown stand at 0 and only the two flags are written, by
// the one body that writes them in retail.
struct FNetworkChangeState
{
	bool bByte0 = false;       // +0x00 — set by the static prop/brush Spawns through 0x101466e0
	bool bChanged = false;     // +0x01 m_bChanged — SetAbsOrigin/SetModel/SetLocalVelocity set it
	bool bByte2 = false;       // +0x02 — the second flag 0x10146790 clears; its writer is unrecovered
	int16 IntervalTicks = 0;   // +0x04
	int16 CountdownTicks = 0;  // +0x06
};
FNetworkChangeState NetworkChangeState;   // +0x01b0

// +0x06f4 `m_flPlaybackRate` (`CBaseAnimating`, datamap `KEY playbackrate`) — the fourth of the four
// scalar animation words `0x101618e0` copies off a controller NPC, beside family Anim's
// `SequenceNumber` (+0x06f0), `AnimTime` (+0x0174) and `SequenceCycle` (+0x06f8).
float SequencePlaybackRate = 1.f;   // +0x06f4

// --- `CGlobalEntityList`'s listener registry (+0x18048 … +0x18058) --------------------------------
//
// NOT an NPC word and not a player word: `+0x18048` is the `CUtlVector<IEntityListener*>` on the
// global entity list object `g_pEntityList` (`DAT_106eb5d8`), whose registrants in the image are
// `CNavPropertyDatabase`, `CPhysSaveRestoreBlockHandler`, `CEntityListSystem` and the aim-target
// manager (`0x102cd650` / `0x102cdb70`, `param+0x114`). `0x100f8700` walks it BACKWARDS calling each
// listener's slot 1. The port's registry is `FElysiumEntityWorld`, and re-homing this onto the world
// is 0001's story — so the two bodies land here and run over this list, which nothing else reads.
struct FEntityListenerVector
{
	TArray<void*> Listeners;   // +0x18048 the block, +0x18054 the count
	int32 Allocated = 0;       // +0x1804c m_nAllocationCount
	void* DebugBlock = nullptr;  // +0x18058, the mirror of the block pointer the append rewrites
};
FEntityListenerVector EntityListeners;

// --- `CBasePlayer` words the port's player does not carry -----------------------------------------
//
// Each is declared BY ITS RETAIL DATAMAP NAME with the offset. The words the player DOES carry
// (`+0x1ccc`/`+0x1cd8`/`+0x1cdc`/`+0x1ce8`/`+0x1cec` → `FElysiumPlayer::Law`,
// `+0x1d10`..`+0x1d1c` → `Police`, `+0x1d90` → `ScareQueue`, `+0x1cc0`..`+0x1cc8` → `Observer`) are
// deliberately ABSENT from this list: those bodies drive the player's own members.

// +0x19b8 `m_flCameraOverrideFadeMarkTime` and +0x19bc `m_flCameraOverrideFadeDuration` — the mark
// and the span `0x1017d900` measures the camera-override crossfade against. A NEGATIVE duration is a
// live arm of that body, not an error.
float CameraOverrideFadeMarkTime = 0.f;   // +0x19b8
float CameraOverrideFadeDuration = 0.f;   // +0x19bc
// +0x19c0 `m_hCameraViewEntity` and +0x19cc `m_hCameraTargetEntity` — the `camera_track` override
// channel's two adopted entities. `0x1017d900` needs only ONE of them live to keep the fade running,
// and resets both on the refusal arm.
FElysiumEntityHandle CameraViewEntity;     // +0x19c0
FElysiumEntityHandle CameraTargetEntity;   // +0x19cc
// +0x19e4 — the fifth word `0x1017d900`'s reset arm zeroes. Between `m_flCameraTargetCrossfadeDuration`
// (+0x19d4) and `m_bObfWasMoving` (+0x19f4) the datamap names nothing, so this word's NAME and its
// concern are **unrecovered**; it is carried by offset so the reset is the whole reset.
int32 Field_0x19e4 = 0;   // +0x19e4

// +0x1ca4 / +0x1ca8 / +0x1cac — the player-animation record `0x10182c40` writes, sitting directly
// below `m_bPlayerAnimCyclePlaying` (+0x1cb0) and `m_aLastplayerAnim` (+0x1cb4). The datamap does not
// carry them, so the NAMES are **unrecovered**; what the body does with them is not.
// `+0x1ca4` is a POINTER in retail — `&DAT_10724ef0` when the copied name is non-empty and literal
// 0 when it is not — so the port carries the set/unset half as a bool beside the buffer rather than
// as a raw pointer into an `FString`, which would dangle on the next write.
bool bPlayerAnimNameSet = false;   // +0x1ca4 != 0
float PlayerAnimEndTime = 0.f;     // +0x1ca8 — curtime - `_DAT_10449270` + a duration
int32 PlayerAnimFlags = 0;         // +0x1cac — bit 0 raised by the same body
// `DAT_10724ef0`, the 0x100-byte GLOBAL buffer `0x10182c40` `strcpy`s its argument into and then
// points `+0x1ca4` AT. One buffer for the whole process, which is a fact a program can observe: a
// second call overwrites the first caller's name in place.
FString PlayerAnimNameBuffer;

// +0x1cf8 `m_flSpawnResponseCopsTimer`, +0x1cfc `m_iSpawnResponseCopsLevel`,
// +0x1d00 `m_hSpawnResponseCopsNPC`, +0x1d04 `m_vecSpawnResponseCopsLocation[3]` — the delayed police
// response record `0x1017ed00` writes. `+0x1cf8`'s CLEAR value is the `0x7f7fffff` sentinel
// (`FIELD_TIME`, `FLT_MAX`), which is the "no response pending" marker the body tests first.
//
// The port's `FElysiumPoliceState` carries a response record of its OWN
// (`bResponsePending`/`ResponseSeverity`/`ResponseWitness`/`ResponsePosition`/`ResponseDeadline`),
// recovered by story 16 from the CONSUMER side. These four are the retail words the PRODUCER writes,
// and `SetSpawnResponseCops` mirrors into the player's record after writing them, so the two cannot
// disagree and neither is a second copy of a rule.
float SpawnResponseCopsTimer = FLT_MAX;   // +0x1cf8, the 0x7f7fffff "unused" marker
int32 SpawnResponseCopsLevel = 0;               // +0x1cfc
FElysiumEntityHandle SpawnResponseCopsNpc;      // +0x1d00
FVector SpawnResponseCopsLocation = FVector::ZeroVector;   // +0x1d04

// +0x1d24 — the word `0x10178120` is a bare getter of. The datamap names `m_flSetOnHeadTimer` at
// +0x1d20 and nothing again until `m_iVFlags` (+0x1d60), so this word's NAME and its concern are
// **unrecovered**; three kernel callers and five outside read it through the getter and NOTHING in
// the image writes it, which is itself the recovered fact.
int32 Field_0x1d24 = 0;   // +0x1d24

// +0x1db0 `m_hControllerNPC` — the `npc_VPlayerController` currently driving this player's body.
// `0x101618e0` is the DETACH: it copies the controller's animation and velocity back, arms a
// one-shot think ON THE CONTROLLER and clears this to -1.
FElysiumEntityHandle ControllerNpc;   // +0x1db0

// +0x1eb8 `m_hUseEntity` and +0x1ec0 — the held `+use` session. `+0x1eb8` is an EDICT INDEX, not an
// EHANDLE: `0x100d5000` hands it straight to the engine's `PEntityOfEntIndex` (`+0x98`). `+0x1ec0`
// is a one-shot byte `ClearUseEntity` consumes; its retail name is **unrecovered**.
int32 UseEntityIndex = 0;      // +0x1eb8 m_hUseEntity (an edict index; 0 is "nothing held")
bool bUseEntityNotify = true;  // +0x1ec0 — the one-shot the release arm clears

// +0x1efc and +0x206c — the two angle triples the autoaim pair sums. `+0x206c` is `pl.v_angle`
// (`pl` is the `CPlayerState` at +0x2068) and `+0x1efc` sits inside `m_Local` (+0x1e40), which makes
// it `m_Local.m_vecPunchAngle` by position and by what `GetAutoaimVector` does with it; the datamap
// covers neither individually, so the SECOND is a positional reading and is stated as one.
FRotator LocalPunchAngle = FRotator::ZeroRotator;   // +0x1efc (inside m_Local)
FRotator EyeAngle = FRotator::ZeroRotator;          // +0x206c pl.v_angle

// +0x20a8 `m_fOnTarget` — the byte both autoaim bodies write, and the only thing either of them
// publishes besides the vector.
bool bOnTarget = false;   // +0x20a8

// +0x22a8 `m_vecAutoAim[3]` — the retained autoaim deflection, blended frame to frame.
FRotator AutoAim = FRotator::ZeroRotator;   // +0x22a8

// --- `CDialog`'s own words (+0x2810 … +0x31ea) ----------------------------------------------------
//
// NOT entity words at all: `CDialog` is the dialogue file object (`0x100e5410 CDialog::load`,
// `0x100e8520 CDialog::process_pc_line`). Two of this family's rows are its methods, and they land
// here for the same reason the entity-list pair does.

/** One PC line of the loaded dialogue, as `0x100e49b0` indexes it. Retail's table is three parallel
 *  arrays at `+0x2810` (a flag byte per line), `+0x2820` (a type int per line) and a count at
 *  `+0x2834`; the port keeps one record per line because the three are always indexed together. */
struct FDialogPcLine
{
	uint8 Flags = 0;   // +0x2810 + i — bit 0 is the "this line is live" gate
	int32 Type = 0;    // +0x2820 + i*4 — 5 and 6 are the two that resolve a head bone
};
TArray<FDialogPcLine> DialogPcLines;   // count at +0x2834
// `CDialog + 0x00` — the EHANDLE `0x100e49b0` reads as word 0 of the object and resolves TWICE on
// its live arm. The class's own datamap does not cover its first word; that it is an entity handle
// is read off the `PTR_DAT_10566458` resolve the body performs on it.
FElysiumEntityHandle DialogSpeaker;

// +0x31ea — the 0x100-byte buffer `CDialog::process_pc_line` `Q_strncpy`s the chosen line's event
// script into (from `+0x2964 + line*0x228`) and that `0x100e4ef0` runs and then zero-fills. It is a
// DIFFERENT field from the one the port's `FElysiumDlgConversation::FlushPendingNpcAction` drains.
FString PendingDialogEventScript;   // +0x31ea

// --- The seams ------------------------------------------------------------------------------------
//
// Each answers NOTHING and names the retail call it stands for. None of them invents a value.

/** `IPhysicsObject::GetPosition(&origin, &angles)` — the physics object's own transform, which slot
 *  226's movetype-7 arm reads and writes back through slots 216/218. **SEAM**: this runtime stands
 *  no `IPhysicsObject`; the pointer arrives through the generated `void*` and nothing can be read
 *  off it, so this answers false and the arm writes nothing. */
bool PhysicsObjectPosition(const void* PhysicsObject, FVector& OutOrigin, FRotator& OutAngles) const;

/** `CBaseEntity::PhysicsTouchTriggers(0)` (`0x100b0f30`) and `CBaseEntity::PhysicsRelinkChildren`,
 *  the two calls slot 226's movetype-7 arm ends on, IN THAT ORDER. **SEAM**: this runtime's overlap
 *  routing is the world's, not the entity's, and it carries no child relink. Recorded so the ORDER
 *  is assertable; read by the test and by nothing else. */
TArray<FString> PhysicsUpdateCalls;

/** `CBaseEntity::VPhysicsUpdatePusher(physicsObject)` — the arm movetypes 1 and 8 take. **SEAM**:
 *  the same missing physics object. Recorded into `PhysicsUpdateCalls`. */
void VPhysicsUpdatePusher(const void* PhysicsObject);

/** `edict_t + 0x40`'s `IServerNetworkable::GetBaseEntity()` (`+0x10`) — the hop slot 165 makes
 *  before dispatching slot 166. **SEAM**: there are no edicts here. The generated signature hands
 *  the edict in as `void*`; this answers null, which takes retail's own "no networkable" arm and
 *  dispatches slot 166 with 0 — the SAME call retail makes, not a refusal of it. */
FElysiumEntity* EntityOfEdict(const void* Edict) const;

/** `IPhysics`'s "is this vphysics object static/asleep" query — `(*DAT_1070b250 + 0x18)(index)`,
 *  the second arm of `0x100b5110`. **SEAM**: answers false, so a `SOLID_VPHYSICS` entity is not
 *  standable, which is retail's answer for a moving one. */
bool PhysicsObjectIsStandable(const FElysiumEntity& Entity) const;

/** `DAT_1072b360`, the single global word slot 240 returns. It is the CPython interop side of the
 *  entity — whatever the embedded interpreter last stored there — and this runtime embeds no
 *  interpreter. **SEAM**: answers null, which is the global's own pre-interpreter value. */
void* PythonInteropObject() const;

/** `CBaseAnimating::LookupBone("bip01_head")` and slot 192's bone-position fetch (`+0x300`), the
 *  two calls `0x100e49b0` makes on the resolved speaker. **SEAM**: the kernel stands no bone table;
 *  answers false and the walk records the miss. */
bool HeadBonePosition(const FElysiumEntity& Speaker, FVector& OutWorld) const;

/** `CDialog::CallEventScript(this, script)` (`0x100e4cf0`) — the authored line's event script.
 *  **SEAM**: the port runs dialogue scripts through `FElysiumDlgConversation`, which the kernel does
 *  not reach. The request is RECORDED and nothing is called. */
TArray<FString> DialogEventScriptCalls;

/** `CScriptedTarget`'s / the engine's sound-duration query `(*DAT_1070b248 + 0x30)(name)`, the float
 *  `0x10182c40` adds to its animation deadline when the dialogue partner resolves. **SEAM**: the
 *  kernel reaches no sound catalogue; answers 0, which makes the deadline the bare
 *  `curtime - _DAT_10449270`. */
float SoundDurationOf(const TCHAR* SoundName) const;

/** The engine's edict array as `AutoaimDeflection` (`0x10176930`) walks it: index 1 up to
 *  `gpGlobals->maxEntities` (`gpGlobals+0x38`), one `0x78`-byte slot at a time, skipping the
 *  free-slot byte at `edict+0x4c`.
 *
 *  The ORDER is reproduced, not approximated: `FElysiumEntityWorld::EntityList` is this port's edict
 *  array — its index IS the handle index, stable and never recycled, the map's entity lump in lump
 *  order followed by the runtime spawns in creation order — and this walks it by ascending index.
 *  That is observable, because `AutoaimDeflection`'s score test is `<=`: among equally aligned
 *  candidates the highest index wins. `ElysiumNpcKernelEntityChain.cpp` carries the reading. */
void ChainEntityList(TArray<FElysiumEntity*>& Out) const;

/** `UTIL_TraceLine(src, src + dir*dist, MASK_SHOT, this, COLLISION_GROUP_NONE, &tr)` as both autoaim
 *  bodies use it: the FIRST thing hit along the aim ray. **SEAM**: this substrate's trace surface is
 *  the world's move solver, which answers world geometry and not entities, so this answers null —
 *  retail's own "the ray hit nothing that takes damage" arm, which is the arm that then walks the
 *  entity list. */
FElysiumEntity* TraceAimRay(const FVector& Src, const FVector& Dir, float Distance) const;

/** `g_pGameRules->AllowAutoTargetCrosshair()` (`(*DAT_1070ba0c + 0xa0)`) and `GetAutoAimMode()`
 *  (`+0x58`), and the per-entity autoaim admission `(+0x80)`. **SEAM**: there is no `CGameRules`
 *  object here. `AllowAutoTargetCrosshair` answers TRUE (the arm that LEAVES `m_fOnTarget` alone,
 *  because answering false would clear a flag retail only clears when the rules say so) and the
 *  other two answer FALSE, which is retail's `AUTOAIM_NONE` — autoaim off. */
bool GameRulesAllowAutoTargetCrosshair() const;
bool GameRulesAutoAimEnabled() const;
bool GameRulesAllowsAutoAimAt(const FElysiumEntity& Candidate) const;

/** `CBaseAnimating::GetModelPtr()` — the studio header `UpdateClosestNpc` requires a candidate to
 *  have before it will cache it. **SEAM**: family Anim already records that this runtime stands no
 *  studio header; this is the same refusal under this family's name, and it answers FALSE, which is
 *  retail's arm for a candidate with no model. */
bool HasStudioModel(const FElysiumEntity& Candidate) const;

/** `thunk_FUN_100b5190(candidate)` — the second gate `UpdateClosestNpc` puts in front of an accepted
 *  candidate, whose TRUE answer REFUSES it. **SEAM**: unrecovered predicate on the candidate
 *  entity; answers false, the accepting arm. */
bool ClosestNpcCandidateRefused(const FElysiumEntity& Candidate) const;

/** `thunk_FUN_1029c970(npc)` / `thunk_FUN_1029c9f0(v, player)` / `thunk_FUN_1029ca30(v)` — the three
 *  perception terms `0x10182a90` composes into the sense value it compares against
 *  `m_flClosestNPCDist`. **SEAM**: the port's stealth surface publishes its OWN committed meter
 *  (`FElysiumPlayer::Observer.Meter`) and the kernel may not recompute it, so this answers the
 *  published number and says so. */
float ClosestNpcPerception(const FElysiumEntity& Npc) const;

/** `thunk_FUN_101e9000(0x10739d08)` — the cvar-like integer `0x10182a90` scales its second term by.
 *  Its NAME and DEFAULT are **unrecovered** (the pointer lives in uninitialised `.data`).
 *  **SEAM**: answers 0, which zeroes the scaled term and leaves the threshold comparison to the
 *  unscaled one. */
int32 ClosestNpcSenseScalar() const;

/** `thunk_FUN_10370630()` — the no-argument call `RemoveCopInPursuit` makes when the pursuit count
 *  reaches zero, before it arms the heightened alert. Its concern is **unrecovered**. **SEAM**:
 *  records that retail called it and does nothing. */
TArray<FString> UnrecoveredChainCalls;

/** `thunk_FUN_1023dcd0()` — the singleton both heightened-alert bodies resolve an output record off
 *  (`+0x480` for the end, `+0x498` for the begin), then fire through `thunk_FUN_100cd660`
 *  (`COutputEvent::FireOutput`). RECOVERED against `docs/vtmb/player-entity.md` § "Law, Masquerade
 *  and world response": the two are `OnEndCopAlertMode` and `OnStartCopAlertMode`. **SEAM**: this
 *  runtime already fires both edges from `Substrate/ElysiumLaw.cpp`, so the kernel body RECORDS the
 *  call rather than double-firing an authored output. */
void FireGlobalActsOutput(int32 RecordOffset);

/** `DAT_10725f74` — the ConVar whose value `BeginHeightenedAlert` adds to curtime for the alert's
 *  expiry. RECOVERED as **`debug_heightened_alert_expire_time`** against
 *  `docs/vtmb/player-entity.md`. **SEAM**: the kernel stands no ConVar table, so `bIsCommand`
 *  answers true and the body takes its `_DAT_104454c4` = 0.0 arm — the alert expires the instant it
 *  is armed. That is the recovered refusal, not a chosen duration. */
bool HeightenedAlertDurationCvar(float& OutSeconds) const;

/** `DAT_10725894` / `DAT_107257bc` — the two ConVars `SetSpawnResponseCops` reads for the delay
 *  `(*DAT_1070b244 + 4)(lo, hi)` draws between, and `DAT_107258dc` — the developer ConVar
 *  `RemoveCopInPursuit` gates its `DevMsg` on. NAMES **unrecovered**. **SEAM**: both duration
 *  ConVars answer `IsCommand`, so the body takes retail's own `0`/`_DAT_104454c4` arms and the draw
 *  is `RandomFloat(0, 0)` = 0 — the response timer becomes curtime exactly. */
bool SpawnResponseCopsDelayCvars(float& OutLow, float& OutHigh) const;

/** `DAT_10724ffc` / `DAT_1072594c` / `DAT_107250d4` — the three ConVars the three act-level getters
 *  override their stored field with. Each getter's shape is the same: `IsCommand()` true → 0;
 *  `IsCommand()` false and `m_nValue` (`+0x2c`, word 0xb) NEGATIVE → the stored field; otherwise
 *  `m_nValue`. NAMES **unrecovered**. **SEAM**: `IsCommand` answers FALSE and the value answers -1,
 *  which is the arm that reads the stored field — the shipped default behaviour. */
bool ActLevelOverrideCvar(int32 CvarId, int32& OutValue) const;

/** The three act-level ConVar ids `ActLevelOverrideCvar` is asked for, spelled as the retail
 *  globals so a reader can check them against the bodies. */
static constexpr int32 ActCvarSupernatural = 0x10724ffc;
static constexpr int32 ActCvarCriminal = 0x1072594c;
static constexpr int32 ActCvarInvestigate = 0x107250d4;

// --- The slot bodies' named halves ----------------------------------------------------------------

/** `FUN_100b5110` (`0x100b5110`) — the shared standability helper slots 159 and 164 both end in.
 *  `GetSolid() == 1` (`SOLID_BSP`) is standable outright; `SOLID_VPHYSICS` (6) asks the physics
 *  object; everything else is not. NAMED from what it does, not from a recovered symbol. */
bool IsStandableSolid() const;

// Family Schedule's slot-580 row type, declared here because this family's `.inl` is included
// BEFORE `ElysiumNpcKernelSchedule.inl` and slot 580's Troika-line body is this family's.
struct FScheduleIdSpace;

/** `0x101a6d00` — slot 580's BASE body (`CAI_BaseNPC`), `return &DAT_1090ff08`. Not the slot: slot
 *  580 is `CAI_BaseNPCTroika`'s own (`0x101aa790`), which this family defines. The base answers a
 *  DIFFERENT `CAI_ClassScheduleIdSpace` from the Troika line's `&DAT_10924248`, and family
 *  Schedule's table has no row for it, so this is the row and the reading of it. */
const FScheduleIdSpace* BaseClassScheduleIdSpace() const;

/** `0x101a67c0` — slot 476 `HearingSensitivity`'s BASE body. The slot itself is Troika's
 *  (`0x101aa5f0`, which reads `+0x63c0`), so this is the base under its own name. RECOVERED VALUE:
 *  `_DAT_104454c0` is the image's shared `1.0f` (`docs/vtmb/animation_and_movers.md` line 659 reads
 *  the same word as `1.0f`), so the base sensitivity is unity and `CanHearSound`'s
 *  `volume * sensitivity` is the bare volume. */
float BaseHearingSensitivity() const;

/** `CPayphone::vfunc286` (`0x101aad90`) — the payphone's override of slot 286 `AddSceneEvent`, whose
 *  whole body is `return;`. Species-only (only `CPayphone#286` fills it), so `default:void` does not
 *  formally apply and it lands as a body: a payphone swallows every choreo scene event. */
void PayphoneAddSceneEvent(const void* Scene, const void* Event);

/** `0x101aadb0` — `CPayphone#612`, the speech sound FLAGS the emitter (`0x102c0520`) passes to
 *  `EmitSound` (`signatures.md` slot 612). `0xa80` when `bDialogQueIsFinal` (+0x654c) is SET,
 *  `0xe80` when it is clear — note the inversion against the Troika line's own body, which adds
 *  `0x400` when the flag is CLEAR. */
int32 PayphoneSpeechSoundFlags() const;

/** `CPayphone::vfunc35` (`0x101aa950`) — `return CanTalk(other) ? 0x2f : 0;`, the capability bitmask
 *  a payphone publishes. Slot 35's own base is generic ObjectCaps-style across the shared vtable;
 *  this is the payphone's, and it is the whole mask gated on one virtual. */
int32 PayphoneUseCaps(FElysiumEntity* Other);

// --- The bodies that fill no slot -----------------------------------------------------------------

/** `0x100e49b0` — `CDialog`'s per-line look-target walk. For PC line `LineIndex` (bounds-checked
 *  against `+0x2834`) with the dialog's speaker handle live and the line's flag bit 0 set, line TYPE
 *  5 and TYPE 6 take IDENTICAL arms: `LookupBone("bip01_head")` on the speaker, then slot 192 for
 *  the bone's world position. Every other type returns having done nothing. */
bool DialogLineHeadPosition(int32 LineIndex, FVector& OutWorld) const;

/** `0x100e4ef0` — run the buffer at `CDialog+0x31ea` through `CallEventScript` if it is non-empty,
 *  then zero-fill all 0x100 bytes of it unconditionally. The clear is NOT inside the `if`. */
void CallPendingDialogEventScript();

/** `0x100f6d80` — `CGlobalEntityList::AddListenerEntity`. A dedupe scan, then a grow, then an
 *  always-at-the-end insert. RECOVERED from what it is called with (`&DAT_106eb5d8`, the entity
 *  list) and what `0x100f8700` does with the result. */
void AddListenerEntity(void* Listener);

/** `0x100f6e40` — `CGlobalEntityList::RemoveListenerEntity`: linear search by value, `memmove`
 *  compaction, decrement. A value not present is a silent no-op. */
void RemoveListenerEntity(void* Listener);

/** `0x101618e0` — the `npc_VPlayerController` DETACH. `bCopyAnimation` copies the controller's four
 *  scalar animation words plus its whole gesture-layer table (0xc0 bytes) and flinch table
 *  (0x54 bytes) onto this body; `bCopyVelocity` transfers its absolute linear and angular velocity.
 *  Then, UNCONDITIONALLY, it arms a one-shot think on the CONTROLLER (`0x101c0b10`, at
 *  `curtime + _DAT_1044e658`) and clears `m_hControllerNPC` to -1. */
void ReleaseControllerNpc(bool bCopyAnimation, bool bCopyVelocity);

/** `0x10176520` — `CBasePlayer::GetAutoaimVector`. Autoaim off is the whole first arm:
 *  `AngleVectors(punch + v_angle)`. Autoaim on clears `m_vecAutoAim`, runs `AutoaimDeflection` from
 *  the shoot position over 16384 units, normalises the two angles into (-180, 180], clamps them to
 *  ±25 and ±12 degrees, then either SCALES by `_DAT_10450a9c` or BLENDS with the retained value, and
 *  finally returns `AngleVectors(punch + v_angle + m_vecAutoAim)`. */
FVector GetAutoaimVector(const FVector& ShootPosition);

/** `0x10176930` — `CBasePlayer::AutoaimDeflection`. Trace the aim ray first: something that takes
 *  damage in the way ends the search at zero deflection (raising `m_fOnTarget` when it carries
 *  `FL_AIMTARGET` `0x10000`). Otherwise walk the live entity list, screening each on the free-edict
 *  byte, "not me", the rules' own admission, `FL_AIMTARGET`, alive, the water-level pair, and
 *  `IRelationType == D_HT` OR "is a player" OR the rules' override; the survivor closest to the
 *  look direction within the delta wins, and its offset is the answer. */
FRotator AutoaimDeflection(const FVector& Src, float Distance, float Delta);

/** `0x10178120` — a bare getter of `+0x1d24`. The field's NAME and CONCERN are **unrecovered**, so
 *  the port method keeps 29c's `FUN_` spelling. */
int32 FUN_10178120() const;

/** `0x1017c6d0` — `CBasePlayer::ClearUseEntity`. Guarded on a held `m_hUseEntity` (+0x1eb8) AND the
 *  one-shot at +0x1ec0: resolve the held edict, clear the one-shot, `AcceptInput(<name>, this, this)`
 *  on it, and then — outside every guard, on EVERY path — zero `m_hUseEntity`.
 *
 *  **Unrecovered:** the input's NAME. `DAT_10555f7c` has 13 referrers, among them
 *  `datamap_CBaseEntity_builder`, `CBaseDoor::Use` and `CBasePlayer::PlayerUse`, which makes it an
 *  input `CBaseEntity` itself registers; the string's bytes are not in the corpus. */
void ClearUseEntity();

/** `0x1017d680` — resolve `m_hCameraTargetEntity` (+0x19cc) to a live entity, AFTER running
 *  `CameraOverrideFadeFraction` for its side effects. The sibling `0x1017d630` does the same over
 *  `m_hCameraViewEntity` (+0x19c0) and is not this family's row. */
FElysiumEntity* ResolveCameraTargetEntity();

/** `0x1017d900` — the camera-override crossfade fraction. Runs only while `+0x19b8` is positive AND
 *  at least one of the two camera entities resolves; a zero duration answers 1.0, a positive one
 *  answers `clamp((now - mark) / duration, 0, 1)`, and a NEGATIVE one answers
 *  `clamp(1 + (now - mark) / duration, 0, 1)` — the count-DOWN arm. Every refusal, and the negative
 *  arm's own underflow, resets `+0x19b8`, `+0x19bc`, both handles and `+0x19e4` and answers 0. */
float CameraOverrideFadeFraction();

/** `0x1017dd80` / `0x1017ddd0` / `0x1017de60` — `m_LevelSupernaturalAct` (+0x1ccc),
 *  `m_LevelCriminalAct` (+0x1cd8, the OBFUSCATED one) and `m_LevelInvestigateAct` (+0x1cdc), each
 *  behind its own ConVar override. The criminal one is the odd body: its stored word is XOR/AND
 *  obfuscated with the literals `0x8a66e35`, `0x793f90`, `0x8b10412`, `0x175991ca` and `0x783682a9`
 *  and put through `thunk_FUN_1042fd40`, which is why the datamap types `+0x1cd0` as a record and
 *  not an int. The port's `FElysiumLawState::Criminal` is the plaintext and is documented as
 *  `+0x1cd8`, so the port stores plaintext and the obfuscation is reproduced as a named round trip
 *  rather than as storage. */
int32 LevelSupernaturalAct() const;
int32 LevelCriminalAct() const;
int32 LevelInvestigateAct() const;

/** The obfuscation `0x1017ddd0` applies, as its own function so the five literals are assertable.
 *  `thunk_FUN_1042fd40` is a single-argument transform whose body the corpus does not carry; this
 *  reproduces the arithmetic AROUND it and passes the result through unchanged, which is stated as
 *  the gap it is. */
static uint32 ObfuscateActLevel(uint32 Stored);

/** `0x1017e720` / `0x1017e740` — bare getters of `m_iCriminalActCount` (+0x1ce8) and
 *  `m_iSupernaturalActCount` (+0x1cec). The port's player already carries both
 *  (`FElysiumPlayer::CriminalActCount()` / `SupernaturalActCount()`), so these ARE those calls. */
int32 CriminalActCount() const;
int32 SupernaturalActCount() const;

/** `0x1017ed00` — record a delayed police response. Refuses outright while
 *  `m_iHuntersInPursuitCount` (+0x1d14, read through `0x1017f8b0`) is 1 or more. On the FLT_MAX
 *  sentinel it stores the level, the source NPC's handle, the location, and an expiry of
 *  `curtime + RandomFloat(lo, hi)`; on an armed record it upgrades ONLY the level, the handle and
 *  the location, and only when `Level` strictly exceeds the stored one — the TIMER IS NOT
 *  RESCHEDULED, which is what makes a burst of incidents one response. */
void SetSpawnResponseCops(int32 Level, FElysiumEntity* Source, const FVector& At);

/** `0x1017f6e0` — one cop leaves the pursuit. Decrements `m_iCopsInPursuitCount` (+0x1d10)
 *  UNCONDITIONALLY (it can go negative), and on exactly zero runs the unrecovered `0x10370630` and
 *  then `BeginHeightenedAlert`. Its `DevMsg` reads `"CSActs: %6.1f - OnCopPursuitStart - %d in
 *  pursuit"` — retail prints "Start" from the REMOVE path, a shipped copy-paste it shares verbatim
 *  with `0x1017f650`. */
void RemoveCopInPursuit();

/** `0x1017f8d0` — `curtime < m_flHeightenedAlertExpireTimer` (+0x1d1c). It reads the TIMER and not
 *  `m_bInHeightenedAlert` (+0x1d18), so a cleared timer answers false while the flag still stands. */
bool IsHeightenedAlertActive() const;

/** `0x1017f9c0` — arm the heightened alert: fire the `+0x498` output, set `m_bInHeightenedAlert` and
 *  `m_flHeightenedAlertExpireTimer = curtime + <cvar>`, then call the unrecovered `0x1017f900`. */
void BeginHeightenedAlert();

/** `0x1017f980` — end it: fire the `+0x480` output, then zero the expire timer. It does NOT clear
 *  `m_bInHeightenedAlert`, which is the asymmetry `IsHeightenedAlertActive` is written against. */
void EndHeightenedAlert();

/** `0x1017fd60` — remember that `Npc` scared this player at `Priority`. Searches the 16-byte record
 *  array at `+0x1d90` by the NPC's ENTITY INDEX; a hit keeps the GREATER priority and always
 *  refreshes the timestamp; a miss grows the array and appends. Logs
 *  `"Scared NPC: %d (dist:%.2f)"` with the index and the NPC's own `+0x6264`. */
void RememberScaredNpc(int32 Priority, FElysiumEntity* Npc);

/** `0x101828b0` — the private cache behind `AddLookTarget`: offer `Candidate` at `Distance` as the
 *  nearest NPC. Resolve the cached handle first; a dead or missing cache resets the triple to
 *  (-1, `0x47c34ff3` = 100000.0, 0). Then three arms — the SAME entity (refresh distance and sense,
 *  or drop it if it died), a DIFFERENT live-but-dead cache (drop it), and finally the acceptance
 *  ladder, which requires `Distance` strictly LESS than the cached one and then six gates. */
void UpdateClosestNpc(FElysiumEntity* Candidate, float Distance);

/** `0x10182a90` — the sense value the cache stores beside the NPC. Resolves `m_hClosestNPC`, folds
 *  three perception terms, and answers the fixed code `0x264` when the folded value EXCEEDS the
 *  stored distance, a packed `(int)scaled | (dist < scaled) << 8` when the distance is at or below
 *  the first term, and 0 otherwise. `0x264` is a magic answer, not a number the caller does
 *  arithmetic with. */
int32 ClosestNpcSense() const;

/** `0x10182c40` — arm a player animation by name for the duration of a spoken line. Copies `Name`
 *  into the ONE global buffer, points `+0x1ca4` at it (or at 0 when the copy is empty), computes
 *  `curtime - _DAT_10449270` and adds either the resolved sound duration (when the dialogue partner
 *  at `+0x0fe8` resolves, carries an NPC, and `Sound` is a non-empty string) or the fallback
 *  `_DAT_10471720`, then ORs bit 0 into `+0x1cac`. */
void SetPlayerAnim(const TCHAR* Name, const TCHAR* Sound);

/** `0x101a7540` — `CCineNPC::IsTimeToStart`. `m_iDelay` (+0x5f70) below 1 **AND** `m_startTime`
 *  (+0x5f74) at or before curtime. The SDK's own body is an **OR** of the same two terms; retail's
 *  is an AND, which is a shipped divergence and is what this ports. */
bool CineIsTimeToStart() const;

/** `0x101a8930` — `CCineNPC::CanInterrupt`. `m_interruptable` (+0x5f90) set AND the resolved
 *  `m_hTargetEnt` (+0x5ce4) answering slot 158 `IsAlive`. A missing target is false, not true. */
bool CineCanInterrupt() const;

/** `0x101a8840` — `CCineNPC::FixScriptNPCSchedule`, the body every scripted-sequence teardown ends
 *  on. If the NPC's `m_IdealNPCState` (+0x5cc4) is not 7 (`NPC_STATE_DEAD`), stamp the
 *  `m_SelectIdealStateTrace` file/line pair (+0x1b3c / +0x1b40 = 970) and store 1
 *  (`NPC_STATE_IDLE`); then, on every path, `ClearSchedule` (`0x10280d30`).
 *
 *  29c's walk read the file/line write as "a one-shot assertion tripwire". It is not: `layout.md`
 *  names `+0x1b3c`/`+0x1b40` the ideal-state selector trace, which every `m_IdealNPCState` writer in
 *  the image stamps. Corrected here and in the walked paragraph. */
void FixScriptNpcSchedule(FElysiumNpc& Npc);

/** `0x101a64a0` — `GetLastThink` for the FOURTH (AI) think channel, beside family Lifecycle's
 *  `LastUpdateThink` / `LastNormalThink` / `LastMoveThink`. This runtime carries the word as
 *  `FElysiumNpcScheduleHost::LastAI`, so the body is that read and nothing else. */
float LastAiThink() const;

/** `0x100994c0`'s companion read, exposed so a case can assert slot 271 against the table's own
 *  starting index. `GetFirstGestureLayer()` (slot 267) answers 0 for every class in the hierarchy;
 *  retail's scan starts THERE and refuses outright when it is 4 or more. */
int32 FirstGestureLayerOrRefusal() const;

// --- Slot 135's own inputs ------------------------------------------------------------------------

/** The `CBaseEntity` mover words slot 135 reads. Not one of them has a port member
 *  (`docs/vtmb/npc-kernel/layout.md` types them on `CBaseEntity`; the shape map's band starts at
 *  `+0x1a40`), so they arrive through one seam rather than seven. */
struct FMoveRebound
{
	float LocalTime = 0.f;        // m_flLocalTime
	float MoveDoneTime = 0.f;     // m_flMoveDoneTime
	float StartTime = 0.f;        // m_flMoveReboundStartTime
	float Duration = 0.f;         // m_flMoveReboundDuration
	FVector Velocity = FVector::ZeroVector;      // m_flMoveReboundVelocity[3]
	FVector AngVelocity = FVector::ZeroVector;   // m_flMoveReboundAngVelocity[3]
	FVector FinalDest = FVector::ZeroVector;     // m_vecFinalDest[3]
	FVector FinalAngle = FVector::ZeroVector;    // m_vecFinalAngle[3]
	FVector LocalOrigin = FVector::ZeroVector;   // slot 220 GetLocalOrigin
	FVector LocalAngle = FVector::ZeroVector;    // slot 221 GetLocalAngles
};

/** **SEAM**: answers the resting state and FALSE, which makes slot 135's first gate refuse — which
 *  is what retail answers for an NPC that is not rebounding. */
bool MoveReboundState(FMoveRebound& Out) const;

/** The easing inside `0x101c10d0`, on its own so the cubic is assertable without a mover:
 *  `(t*t + 1)*t - (t/D)*(D*D + 1)*t`. Zero at `t == 0` and at `t == D`. */
static float MoveReboundBlend(float T, float Duration);

/** `CBaseEntity::SetLocalVelocity` / `SetLocalAngularVelocity`, slot 135's two writes. The port's
 *  `FElysiumEntity::Velocity` and `AngularVelocity` ARE those words. */
void SetLocalVelocity(const FVector& NewVelocity);
void SetLocalAngularVelocity(const FVector& NewAngularVelocity);

/** `thunk_FUN_10075b70(record.event)` — the release both scene-event removers call before they
 *  compact. **SEAM**: the port's scene events are parsed data owned by the scene asset and are not
 *  reference-counted; the call is recorded so the SEQUENCE is assertable. */
void ReleaseSceneEvent(const FSceneEventRecord& Record);
/** How many times that release ran, which is the only observable it has here. */
int32 SceneEventReleases = 0;

/** `0x102ea280` — the GLOBAL-to-LOCAL range translation slots 447 and 450 both forward into.
 *  A null space is retail's end-of-chain and answers -1. */
static int32 GlobalToLocalId(const FScheduleIdSpace* Space, int32 GlobalId);

/** **SEAM** for `CCineNPC::m_iDelay` (+0x5f70) and `m_startTime` (+0x5f74): the port's beat lives on
 *  the `scripted_sequence` entity and the kernel holds no pointer to it. Answers (0, 0), the state
 *  retail's constructor leaves. */
void CineDelayState(int32& OutDelay, float& OutStartTime) const;

/** **SEAM** for `CCineNPC::m_interruptable` (+0x5f90). No port member; answers false. */
bool CineIsInterruptable() const;

// --- The `CBasePlayer` bodies' own inputs ---------------------------------------------------------

/** `CBaseEntity::GetFlags()` on a candidate — `FL_AIMTARGET` (`0x10000`) is the only bit either
 *  autoaim body reads. The port's `FElysiumEntity::Flags` IS that word; no seam. */
static int32 ChainEntityFlags(const FElysiumEntity& Entity);

/** `AngleVectors(angles, &forward, nullptr, nullptr)` (`thunk_FUN_10139610`) — the one conversion
 *  both autoaim bodies end on. Retail's angles are `(pitch, yaw, roll)` in SOURCE degrees and the
 *  port's `FRotator` is the same triple. */
static FVector ChainAngleForward(const FRotator& Angles);

/** The `(-180, 180]` wrap the deflection's two angles go through before the ±25/±12 clamps. Retail
 *  runs the test ONCE per direction, not in a loop, so an angle outside `(-540, 540)` stays
 *  outside — a fact a program can observe and one this reproduces. */
static float ChainWrapDegrees(float Degrees);

/** `entity->m_takedamage != DAMAGE_NO` (+0x01fc). Family Damage carries the word on the NPC leaf
 *  only, so a non-NPC entity answers `DAMAGE_NO` — retail's own default. */
static bool ChainTakesDamage(const FElysiumEntity& Entity);

/** The water-level pair both autoaim bodies screen on: you cannot autoaim from dry land at
 *  something fully submerged, nor from under water at something fully dry. */
bool AutoaimWaterLevelBlocks(const FElysiumEntity& Candidate) const;

/** **SEAM** for `AutoaimDeflection`'s `flDelta`, which arrives in the caller's own frame and the
 *  decompilation cannot attribute. The SDK's callers pass `AUTOAIM_2DEGREES`; **unrecovered** here,
 *  and 0 is the LOOSEST screen rather than a number invented from the SDK. */
float AutoaimDelta() const;

/** **SEAM** for `DAT_1070ba3c` (the branch selector), `_DAT_10450a9c` (the scale) and
 *  `_DAT_10451ab8` (the old-sample weight). Returns true for the SCALE arm — `DAT_1070ba3c == 1`,
 *  the shipped "always non-sticky autoaim" latch — with both numbers **unrecovered** and 0. */
bool AutoaimBlendWeights(float& OutScale, float& OutOldWeight) const;

/** **SEAM** for `candidate + 0x19c & 0x40`, the per-entity bit whose SET state refuses a
 *  closest-NPC candidate. Named by neither datamap; **unrecovered**. False admits. */
bool ClosestNpcCandidateBitSet(const FElysiumEntity& Candidate) const;

/** **SEAM** for `_DAT_10449270` (subtracted from curtime) and `_DAT_10471720` (the no-sound
 *  fallback duration) in `SetPlayerAnim`. Both **unrecovered**; both answer 0. */
float PlayerAnimLeadIn() const;
float PlayerAnimFallbackDuration() const;

/** **SEAM** for `_DAT_1044e658`, the delay the released controller NPC's one-shot think is armed
 *  at. **Unrecovered**; 0 arms it for the next pass. */
float ControllerReleaseThinkDelay() const;
