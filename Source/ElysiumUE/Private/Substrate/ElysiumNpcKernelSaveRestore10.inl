// Story 29d, family **SaveRestore10 + Lifecycle10** — the declarations of slots 126 `Save`,
// 127 `Restore`, 180 `UpdateOnRemove` and 106 `PostConstructor`, plus the non-slot bodies beside
// them.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual and this family only defines it. The four virtuals
// this family fills are `Save`, `Restore`, `UpdateOnRemove` and `PostConstructor`; everything below
// is the half the generator does not declare — the `CAI_BaseNPC` base bodies beside them, the
// species arms, the sentinel codec all three share, and the seams they go through.
//
// The definitions are in `Substrate/ElysiumNpcKernelSaveRestore10.cpp` and the tests in
// `Tests/ElysiumNpcKernelSaveRestore10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` § "Story 29d, family SaveRestore10", and the non-slot half's is
// § "Story 29d, family Lifecycle10".
//
// --- What this family is --------------------------------------------------------------------------
//
// **Save is a sentinel codec, and that is the whole shape of the family.** Twelve of the sixteen
// `SaveRestore10` rows are one pair of retail functions applied to a list of `FIELD_TIME` stamps:
// `0x101cf250` rewrites a stamp to `1e+11` when it matches its mode's "unset" value, and
// `0x101cf2f0` reverses it. Everything around them — which field, which mode, and that the encode
// runs BEFORE the archive call and the decode AFTER it — is retail's rule, which is why the three
// species bodies a batch verdicted `mechanism` were re-verdicted `rule` by review: a mechanism
// routed through a service seam is not what they are, they are a retail-chosen sentinel applied to
// named offsets in a recovered order.
//
// **Why a disposable save file does not make this moot.** `CLAUDE.md` says save games are
// disposable and no migration is wanted. What is reproduced here is not a file format: it is the
// ORDER in which a program observes its own fields change and the VALUES they take while the
// archive runs. A retail body that read `m_flEyeFidgetTime` between the encode and the decode would
// see `1e+11`, and any body that runs while `Save` is on the stack is that body.
//
// --- The three corrections this family's reading made to the checklist's walks --------------------
//
//   * **`0x1027bc60`'s "4 timers and 3 timers" is a misread MODE.** `thunk_FUN_101cf250` takes
//     `(float*, mode)`, never `(first, count)`. The listing (`1027bc6c` `PUSH 0x4` / `1027bc80`
//     `PUSH 0x3`) shows exactly TWO encode calls: `m_flExtendedBlockedByFriendTimer` (`+0x5b8c`)
//     with mode **4** and `m_flWaitFinished` (`+0x5db4`) with mode **3**. `0x1027c160`
//     (`CAI_BaseNPC::Restore`, band 5–9, ported by story 29c-1 as `RestoreExtendedHeader`) carries
//     the same two calls and was read the same wrong way; both are corrected in this story, and the
//     `RebaseRestoredStamp` helper 29c-1 coined for a re-base that retail does not perform is gone.
//   * **`0x1023f040`/`0x1023f0c0`/`0x1023f060` are not a bit-vector copy: they are CRC32.**
//     `0x1023f040` writes `0xffffffff`, `0x1023f0c0` is an unrolled table-driven CRC32 over
//     `DAT_10496f58`, and `0x1023f060` complements. So `AIExtendedSaveHeader_t`'s last word is a
//     CHECKSUM of the running schedule's task array (`schedule+0x20`, `schedule+0x24 << 3` bytes —
//     an 8-byte `Task_t` per task), not a copy of its interrupt bits.
//   * **`0x102993c0`'s decode pass is NOT shifted where it matters.** The decompiled C renders the
//     eleven decode pointers one slot out (EBX/stack aliasing) and the nine sound pointers two
//     slots out, but the MODE arguments are in the listing in their own right and read
//     `3,2,2,3,3,3,3,2,2,4,4` — identical to the encode list. The decode therefore walks the same
//     eleven fields in the same order with the same modes; nothing is shifted in the ported body.
//
// --- The `.rdata` cells, read out of the pinned image ---------------------------------------------
//
// `_DAT_104454c4` = **0.0** (already settled by story 29c-1, family Geometry) and
// `_DAT_10482fac` = **1e+10**, read at file offset `0x482fac` of the pinned `vampire.dll`
// (base `0x10000000`, `.rdata` VA `0x10445000` at file offset `0x445000`). The encode writes
// `1e+11` and the decode catches anything at or above `1e+10`, so the sentinel has a decade of
// headroom over any stamp a running game could hold.

// --- The sentinel codec ---------------------------------------------------------------------------

/** The four modes `0x101cf250` / `0x101cf2f0` switch on, spelled as retail's own numbers because
 *  that is what every call site pushes. A mode outside 1..4 is retail's `default:` — it does
 *  nothing, which is reproduced.
 *
 *  Encode (`0x101cf250`), per mode: **1** a stamp strictly BELOW `_DAT_104454c4` (0.0); **2** a
 *  stamp exactly `-1.0`; **3** a stamp exactly `_DAT_104454c4` (0.0); **4** a stamp exactly
 *  `FLT_MAX`. A match becomes `1e+11`.
 *
 *  Decode (`0x101cf2f0`): any stamp at or above `_DAT_10482fac` (1e+10) becomes `-1.0` for modes
 *  **1** and **2** (they share a `case` label), `0.0` for mode **3** and `FLT_MAX` for mode **4**.
 *  Mode 1 is therefore NOT its own inverse — a `-0.5` encoded by mode 1 comes back as `-1.0` — and
 *  that asymmetry is retail's, exercised by name in the suite. */
enum class ESaveStampMode : int32
{
	None = 0,
	BelowZero = 1,   // encode: `*p < 0.0`;  decode: `-1.0`
	MinusOne = 2,    // encode: `*p == -1.0`; decode: `-1.0`
	Zero = 3,        // encode: `*p == 0.0`;  decode: `0.0`
	FloatMax = 4,    // encode: `*p == FLT_MAX`; decode: `FLT_MAX`
};

/** `1e+11`, the value the encode writes. */
static constexpr double SaveStampSentinel = 1e+11;
/** `_DAT_10482fac` — **1e+10**, the floor at or above which the decode fires. */
static constexpr double SaveStampSentinelFloor = 1e+10;
/** `_DAT_104454c4` — **0.0**. */
static constexpr double SaveStampZero = 0.0;

/** `FLT_MAX` as this runtime's stamps carry it. Retail's fields are 32-bit floats and the compare
 *  is exact, so a `double` stamp only matches when it holds the widened float constant — which is
 *  what every port writer of an "infinite" stamp stores. */
static double SaveStampFloatMax();

/** `FUN_101cf250` (`0x101cf250`), the encode. Returns whether the stamp was rewritten, so a test
 *  can state which arm fired without inspecting the value twice. */
static bool SaveStampEncode(double& Stamp, ESaveStampMode Mode);

/** `FUN_101cf2f0` (`0x101cf2f0`), the decode. Same return contract. */
static bool SaveStampDecode(double& Stamp, ESaveStampMode Mode);

/** The same pair over a stamp this runtime carries as a 32-bit `float` rather than a `double` —
 *  `m_flEyeFidgetTime` (`+0x657c`) is the only one in this family, and it lands on the entity chain
 *  as `FElysiumCombatCharacter::NextFidgetTime`. Retail's fields are ALL `float`, so this overload
 *  is the exact width and the `double` one is the widened convenience. */
static bool SaveStampEncode(float& Stamp, ESaveStampMode Mode);
static bool SaveStampDecode(float& Stamp, ESaveStampMode Mode);

/** `FUN_101b9840` (`0x101b9840`) and `FUN_101b9860` (`0x101b9860`) — the nine-times-repeated
 *  wrappers whose whole body is the codec at mode **2** over a `CSound`'s `+0x10 m_flExpireTime`
 *  (`FIELD_TIME`, `vtmb_fields CSound`). This runtime's `CSound` is `FElysiumGameSoundEvent` and
 *  its `+0x10` is `ExpireTime`. */
static void SaveSoundStampEncode(struct FElysiumGameSoundEvent& Sound);
static void SaveSoundStampDecode(struct FElysiumGameSoundEvent& Sound);

// --- The CRC32 `AIExtendedSaveHeader_t`'s last word carries ---------------------------------------

/** `FUN_1023f040` — `*crc = 0xffffffff`. */
static uint32 SaveCrc32Init();
/** `FUN_1023f0c0` — the unrolled table-driven CRC32 over `DAT_10496f58`. The table is the standard
 *  reflected CRC-32 (`0xedb88320`) one, which is what makes the routine reproducible without the
 *  1 KB of `.rdata`: the port generates the table from the polynomial and the suite pins the
 *  routine against the published `"123456789"` check value `0xcbf43926`.
 *
 *  **UNRECOVERED, and it does not matter here:** the corpus does not hold `DAT_10496f58`'s bytes,
 *  so "this is the standard table" is an inference from the shape of the unrolled loop (byte-at-a-
 *  time, `crc >> 8 ^ table[(byte ^ crc) & 0xff]`, four-at-a-time on an aligned run) and not a read.
 *  A different polynomial would change the checksum's value and nothing else about the body. */
static uint32 SaveCrc32Update(uint32 Crc, const uint8* Bytes, int32 Count);
/** `FUN_1023f060` — `*crc = ~*crc`. */
static uint32 SaveCrc32Final(uint32 Crc);

/** The running program's task array as retail's `Task_t[]` — an `int32` task id and a `float`
 *  operand per task, 8 bytes each, which is the stride `schedule+0x24 << 3` counts in.
 *
 *  **SEAM, and it answers this runtime's own ids:** retail checksums its compiled schedule table's
 *  memory image, whose task numbers are `vdata`'s. This runtime's `EElysiumTask` is its own
 *  enumeration, so the checksum is not comparable with a retail save — the recovered half is the
 *  ROUTINE, the 8-byte stride and the fact that the header carries a checksum of the task array at
 *  all. An NPC with no running program produces no bytes, and CRC32 over zero bytes is
 *  `~0xffffffff == 0`, which is the literal `0` retail's own no-schedule arm writes. */
void ScheduleTaskBytes(TArray<uint8>& OutBytes) const;

// --- The archive seam ------------------------------------------------------------------------------
//
// Retail's slot 126 takes an `ISave&` and slot 127 an `IRestore&`; the generated signature spells
// both `void*` because `signatures.tsv` has no port type to name. This runtime's archive is
// `FElysiumSaveArchive` (`ElysiumSaveArchive.h`) and that is what the `void*` IS — every write
// below goes through it when one is handed in, and is recorded either way.
//
// **NOTHING IN THIS RUNTIME CALLS `Save()` OR `Restore()` YET, and that is deliberate.** This
// port's persistence is `FElysiumNpc::Serialize` (`ElysiumNpc.cpp`), a block-per-subsystem record
// written by the snapshot applier; wiring slot 126 into it would ADD the sentinel encode/decode
// events to a path retail does not have here and would change a payload format for no observable
// gain. The bodies are ported whole and driven by `Elysium.Substrate.NpcKernelSaveRestore10.*`, the
// same posture family Precache10 took for slot 104 and for the same reason.

/** One archive operation, in retail's order. `Name` is the retail field the call was made for, so
 *  a case can assert the ORDER as text rather than as indices. */
struct FSaveArchiveOp
{
	enum class EKind : uint8
	{
		Fields,   // `ISave` vtable `+0x08`  — `WriteFields(block, datamap)`
		Bool,     // `ISave` vtable `+0x30`  — `WriteBool(&value, 1)`
		Int,      // `ISave` vtable `+0x28`  — `WriteInt(&value, 1)`
		ReadBool, // `IRestore` vtable `+0x44` — `ReadBool(&value, 1, 0)`
		ReadInt,  // `IRestore` vtable `+0x3c` — `ReadInt(&value, 1, 0)`
	};
	EKind Kind = EKind::Fields;
	FString Name;
	int32 Value = 0;
};

/** Every archive call this NPC's `Save`/`Restore` made, in retail's order. Never cleared by a body:
 *  a second `Save` appends, as retail's second call to `ISave` would. */
TArray<FSaveArchiveOp> SaveArchiveLog;

/** `AIExtendedSaveHeader_t`, the 0x8c-byte stack block `CAI_BaseNPC::Save` builds and
 *  `WriteFields(&datamap_AIExtendedSaveHeader_t)` writes. Offsets are the frame's
 *  (`ESP+0x14`..`ESP+0x9c` at `1027bcb2`). */
struct FAiExtendedSaveHeader
{
	/** `+0x00`, a 16-bit word. The literal **1** (`MOV EDI,1 / MOV word ptr [ESP+0x14],DI`). */
	int16 Version = 1;
	/** `+0x04`. Three bits, OR'd in this order:
	 *    - `0x1` slot `0x29c` `GetEnemy()` is non-null;
	 *    - `0x2` `m_hTargetEnt` (`+0x5ce4`) passes its `& 0x1fff` index and `>> 13` serial check
	 *      onto a LIVE entity — a handle that is merely set is not enough;
	 *    - `0x4` the navigator has an active goal (`0x102ee6a0` on `m_pNavigator +0x5d34`). */
	uint32 Flags = 0;
	/** `+0x08`, `char[0x80]` — `Q_strncpy(name, schedule+0x40, 0x80)`. Empty when no schedule runs
	 *  (retail writes a single NUL at `name[0]` and leaves the rest of the buffer alone). */
	FString ScheduleName;
	/** `+0x88` — the CRC32 above, `0` when no schedule runs. */
	uint32 ScheduleCrc = 0;
};

/** Build the header from live state — retail's own block, shared by `BaseSave` and by the record
 *  `FElysiumNpc::Serialize` writes. */
FAiExtendedSaveHeader BuildExtendedSaveHeader() const;

/** The header the last `BaseSave` built, kept so a case can read the flags without an archive. */
FAiExtendedSaveHeader LastSavedExtendedHeader;

/** `ISave` vtable `+0x08` — `WriteFields(&header, &datamap_AIExtendedSaveHeader_t)`. Routes the
 *  four words through `FElysiumSaveArchive` when `Archive` is non-null, and records the call. */
void SaveWriteFields(void* Archive, FAiExtendedSaveHeader& Header);

/** `ISave` vtable `+0x30` / `+0x28` — the single `bool` and the two `int`s the Troika body writes
 *  between the encode and the decode. */
void SaveWriteBool(void* Archive, const TCHAR* Field, bool bValue);
void SaveWriteInt(void* Archive, const TCHAR* Field, int32 Value);

/** `IRestore` vtable `+0x44` / `+0x3c` — the mirror pair slot 127 reads. `RestoreReadBool` answers
 *  the value read; with no archive it answers **false**, which is the arm that skips the two
 *  `ReadInt`s, and is what a fresh runtime's record holds. */
bool RestoreReadBool(void* Archive, const TCHAR* Field);
void RestoreReadInt(void* Archive, const TCHAR* Field, int32& Value);

/** SEAM for `thunk_FUN_102e0b60` / `thunk_FUN_102e0b80` (`CAI_Motor`'s pre- and post-archive
 *  pointer fix-up, run only when `m_pMotor +0x5d44` is non-null) and `thunk_FUN_102e8aa0` /
 *  `thunk_FUN_102e8ac0` (the same pair for `m_MoveAndShootOverlay +0x5cf4`, run unconditionally).
 *
 *  Both halves save and re-link a RAW POINTER into the save block. This runtime rebuilds the motor
 *  from the entity def at load (`FElysiumNpc::BuildOwnMotor`) and binds no move-and-shoot overlay
 *  at all (`ElysiumNpcKernelShapeMap.cpp` records `+0x5cf4` ABSENT), so there is no pointer to fix
 *  up and all four answer nothing. They are COUNTED, because whether the motor arm ran at all is
 *  the observable half — retail skips it for a motorless NPC and this runtime must too. */
int32 MotorSaveFixups = 0;
int32 MotorRestoreFixups = 0;
int32 MoveAndShootSaveFixups = 0;
int32 MoveAndShootRestoreFixups = 0;

/** SEAM for `0x102ee6a0` on `m_pNavigator` — "the navigator has an active goal", header bit `0x4`.
 *  Family Senses already stands `NavigatorHasNodeGraph()` for this substrate's missing node graph;
 *  this is the goal query beside it and answers **false**, because nothing in this runtime stands a
 *  `CAI_Navigator` goal object. Named rather than inlined so the day a navigator lands the bit
 *  moves with it. */
bool NavigatorGoalIsActive() const;

// --- Slot 126 `Save` -------------------------------------------------------------------------------

/** `CAI_BaseNPC::Save` (`0x1027bc60`), slot 126's body on the `CAI_BaseNPC`-line classes and the
 *  body the Troika override `0x102993c0` calls through a DIRECT `thunk_`. A distinct retail
 *  function beside the slot's own body, so it takes its own name — the precedent family Precache10
 *  set with `BasePrecache`/`TroikaPrecache`/`Precache`.
 *
 *  In retail's order:
 *    1. encode `m_flExtendedBlockedByFriendTimer` (`+0x5b8c`) mode **4**;
 *    2. encode `m_flWaitFinished` (`+0x5db4`) mode **3**;
 *    3. the motor pre-fixup, only when `m_pMotor` stands;
 *    4. the move-and-shoot overlay pre-fixup, always;
 *    5. build `AIExtendedSaveHeader_t` — version, the three flag bits in the order above, then the
 *       schedule name and its task CRC, or a cleared name and a zero CRC when `+0x5c38` is null;
 *    6. `WriteFields`;
 *    7. `CBaseCombatCharacter::Save`, whose answer is THIS body's return value;
 *    8. decode 1 and 2 in the same order, then the two post-fixups.
 *
 *  `CBaseCombatCharacter::Save` (`0x1000e534` -> the chain's) is not an NPC-kernel row and has no
 *  port body; it answers **1** here, the "wrote something" answer every retail `Save` chain
 *  produces, so a caller that tests the result takes the same arm. */
int32 BaseSave(void* Archive);

/** `CAI_BaseNPCTroika::Save` (`0x102993c0`) — slot 126's own body, without the species prologue.
 *  `Save()` is the slot; this is what it runs when no species arm claims it, and what a species
 *  arm's own chain call reaches through `FSpeciesDispatchScope`.
 *
 *  Eleven stamps encoded in retail's order and modes, then nine `CSound` expiry stamps at mode 2,
 *  then `BaseSave`, then one `bool` and (when it is true) two `int`s through the archive, then the
 *  same eleven and the same nine decoded in the same order. The return is `BaseSave`'s. */
int32 TroikaSave(void* Archive);

/** Slot 126's species prologue. True means a species body ran and the Troika body must NOT — which
 *  is what a vtable dispatch to an override does. Keyed on the override row's retail ADDRESS
 *  through `Substrate/ElysiumNpcKernelClassLookup.h`, as family Precache10's slot-104 prologue is. */
bool SaveSpecies(void* Archive, int32& OutResult);

/** `CNPC_VMingXiao::Save` (`0x10395f80`) — the six `m_rflRegrowTimers` (`+0x66f4`) encoded at mode
 *  **4** ascending, the Troika body, then the same six decoded ascending. */
int32 MingXiaoSave(void* Archive);

/** `CNPC_VMingXiaoTentacle::Save` (`0x1039ed50`) — `m_flPhaseExpireTimer` (`+0x6674`) at mode
 *  **3** around the Troika body. */
int32 MingXiaoTentacleSave(void* Archive);

/** `CNPC_VTzimisceHeadClaw::Save` (`0x103c2810`) — `m_flSlowedExpire` (`+0x6678`) at mode **3**
 *  around the Troika body. */
int32 TzimisceHeadClawSave(void* Archive);

/** `CScriptedTarget::Save` (`0x1034e320`) — `m_flPauseDoneTime` (`+0x5f64`) at mode **3** around
 *  **`BaseSave`**, not the Troika body: `CScriptedTarget` is a `CAI_BaseNPC` in the census and its
 *  slot 126 chains `0x1027bc60` directly. */
int32 ScriptedTargetSave(void* Archive);

// --- Slot 127 `Restore` ----------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::Restore` (`0x10299700`) — slot 127's own body. `CAI_BaseNPC::Restore`
 *  (`0x1027c160`, story 29c-1's `RestoreExtendedHeader`) runs FIRST and ITS answer is what this
 *  returns, unchanged; then one `ReadBool` whose truth gates two `ReadInt`s into
 *  `m_iRestorePedLinkNode` (`+0x6310`) and `m_iRestorePedLinkDestNode` (`+0x6314`); then the same
 *  eleven stamps and nine sounds `TroikaSave` encodes, decoded in the same order with the same
 *  modes. */
int32 TroikaRestore(void* Archive);

/** Slot 127's species prologue, the mirror of `SaveSpecies`. */
bool RestoreSpecies(void* Archive, int32& OutResult);

/** `CNPC_VMingXiao::vfunc127` (`0x10396000`) — the Troika body, then the six `m_rflRegrowTimers`
 *  decoded at mode **4** ascending. */
int32 MingXiaoRestore(void* Archive);

/** `CNPC_VMingXiaoTentacle::vfunc127` (`0x1039eda0`) — the Troika body, then `m_flPhaseExpireTimer`
 *  at mode **3**. */
int32 MingXiaoTentacleRestore(void* Archive);

/** `CNPC_VTzimisceHeadClaw::vfunc127` (`0x103c2860`) — the Troika body, then `m_flSlowedExpire` at
 *  mode **3**. */
int32 TzimisceHeadClawRestore(void* Archive);

/** `CNPC_VVampireBoss::Restore` (`0x103c5910`) — the Troika body, then a post-load reset of the
 *  monster-model override: `m_pMonsterModelName` (`+0x6680`) := null, `ClearBodyEmitterNames()`
 *  (family Damage's, `0x103c6eb0`), and `m_pszMonsterClassname` (`+0x6694`) := the literal
 *  `"npc_VVampireBoss"`. The write order is the listing's (`103c5972` the model name, `103c597c`
 *  the emitter names, `103c5981` the classname).
 *
 *  This is also the body the other bosses' own slot-127 overrides call as their base
 *  (`CNPC_VAndreiBlood` `0x1035cf80`, `CNPC_VAsianVampire` `0x10360e10`, the Chang brothers
 *  `0x1036b170`, `CNPC_VSabbatLeader` `0x103a6e80`, `CNPC_VSheriffMan` `0x103ae7f0`), so it sits
 *  between the Troika body and those rows — which is why the arm keys on `CNPC_VVampireBoss` by
 *  the CHAIN walk (`IsRetailClass`) and not by a name compare. */
int32 VampireBossRestore(void* Archive);

/** `CScriptedTarget::Restore` (`0x1034e370`) — `CAI_BaseNPC::Restore` and ITS answer, then
 *  `m_flPauseDoneTime` decoded at mode **3**. Nothing else. */
int32 ScriptedTargetRestore(void* Archive);

// --- Slot 180 `UpdateOnRemove` ---------------------------------------------------------------------

/** `CAI_BaseNPCTroika::UpdateOnRemove` (`0x1028d6e0`) — slot 180's own body, read off the listing.
 *  Six steps in order:
 *    1. `ClearHintNode(this, 5.0)` — a **5.0-second** reuse delay, where the base `0x1027ca30`
 *       (story 29c-1's `BaseNpcUpdateOnRemove`) passes 0.0. That difference is the whole reason the
 *       two bodies are distinct.
 *    2. slot `0x964` (**601**) dispatched with `this`, only when `m_pAttackCoordinator` (`+0x65e8`)
 *       is non-zero — the melee-coordinator release.
 *    3. `LeaveInterestingPlace(0, "Leaving interesting place (UpdateOnRemove)")` — the string is
 *       `0x105d87d4`, read off the listing at `1028d702`, and it settles what `0x102b53d0` is.
 *    4. `if (IsInDialog()) StopDialog()` — `0x102c1170` then `0x102c0bb0`.
 *    5. free `m_sppPatrolPath` (`+0x658c`) and then `m_sppPatrolPathHunt` (`+0x6594`), in that
 *       order, through `0x1029f5d0`.
 *    6. tail-jump to `CAI_BaseNPC::UpdateOnRemove`. */
void TroikaUpdateOnRemove();

/** Slot 180's species prologue. */
bool UpdateOnRemoveSpecies();

/** `CNPC_VCop::UpdateOnRemove` (`0x10371a90`), read off the listing — the decompiled C mis-renders
 *  the two census bytes as `this+1`. Retail, instruction for instruction:
 *
 *      DL = m_bCountedAlive (+0x6671);  AL = 0
 *      if (DL != 0) --DAT_1093acac
 *      DL = +0x6672;  m_bCountedAlive = 0
 *      if (DL != 0) --DAT_1093acb0
 *      +0x6672 = 0
 *      JMP CAI_BaseNPCTroika::UpdateOnRemove
 *
 *  Note the interleave: `+0x6672` is READ before `+0x6671` is cleared, so the two arms cannot
 *  interfere. Both counters are program-visible — `DAT_1093acac` is written by `CNPC_VCop::Spawn`
 *  and read by `CNPC_VCop::SelectSchedule`, `0x103707e0` and `0x10370850`; `DAT_1093acb0` by
 *  `CNPC_VCop::OnStateChange` and `SelectSchedule`. */
void CopUpdateOnRemove();

/** `CNPC_VMingXiao::UpdateOnRemove` (`0x10391230`) — when `m_eThrowableObjectMode` (`+0x673c`) is
 *  non-zero, drop the carried throwable through family Damage's `MingXiaoThrowCleanup`
 *  (`0x10398fd0`), then the Troika body either way. Without the drop the thrown prop outlives the
 *  boss. */
void MingXiaoUpdateOnRemove();

/** `CNPC_VNewscaster::UpdateOnRemove` (`0x103a03a0`) — family Species' `FUN_103a0d50` (the two
 *  story-queue teardowns and the active-story byte), then the Troika body. */
void NewscasterUpdateOnRemove();

/** `FUN_102b53d0` (`0x102b53d0`) — the interesting-place release, which the checklist's walk called
 *  "the grapple release". It is not: `+0x62ec` is `m_pInterestingPlace`, `+0x62e8`
 *  `m_bInterestingPlaceArrived` and `+0x6304` `m_eInterestingPlaceMode`
 *  (`ElysiumNpcKernelShape.cpp`), and the listing's own string argument is
 *  `"Leaving interesting place (UpdateOnRemove)"`.
 *
 *  Retail: with a place held, re-check it (`0x10299a80`), emit two sounds through a
 *  `CPASAttenuationFilter` (channel 4, pitch `0x24`, volume 100), detach through `0x102da600` with
 *  a flag computed from `m_bInterestingPlaceArrived` and `0x100cd660`, clear the place and the
 *  mode, strip `m_bfAINPCFlags` bit `0x20000000` and `m_bfAINPCFlags2` bits `0x08000008`, call
 *  `0x102ae310`; and ALWAYS, place or no place, clear `m_bInterestingPlaceArrived` last.
 *
 *  This runtime already owns that transaction as `FinishAmbientUse(bFireLeft, bStopMovement)`, so
 *  this is the call site and not a second copy of the body — `bAmbientArrived` is
 *  `m_bInterestingPlaceArrived` and it is the `bFireLeft` argument, which is retail's own
 *  `0x100cd660` flag. */
void LeaveInterestingPlaceOnRemove();

/** How many times the release above ran. The call is unconditional in retail — the place check is
 *  INSIDE `0x102b53d0`, not at its call site — so this counts the Troika body's passes as well as
 *  the releases, which is what a species arm's "did it chain?" case reads. */
int32 InterestingPlaceReleases = 0;

/** `FUN_102c0bb0` (`0x102c0bb0`), the dialogue stop `UpdateOnRemove` runs when `IsInDialog()` says
 *  yes: stop sound channel 5 on this entity, dispatch slot `0x44c` (**275**), then
 *  `CBaseCombatCharacter::FadeoutExpressions`, `CAI_BaseNPCTroika::FinishTalking`, and — when slot
 *  `0x994` (**613**) allows and the running schedule is not already `0xf1` — install schedule
 *  `0xf1` with a fresh think.
 *
 *  **SEAM**: this runtime's dialogue teardown is `FElysiumNpcDialogue`'s session end and its
 *  `TalkingUntil` stamp, not a schedule install; schedule `0xf1` has no registered id here. What
 *  the body does that this substrate CAN state is end the talking window, which is what it does —
 *  the schedule install is counted and named. */
void StopDialogOnRemove();
int32 DialogStopScheduleRequests = 0;

// --- Slot 106 `PostConstructor` --------------------------------------------------------------------
//
// `CAI_BaseNPC::PostConstructor` (`0x1027bb20`) is 27 bytes whose entire content is an ORDER:
// `CBaseCombatCharacter::PostConstructor(name)` runs to completion FIRST, and only then does this
// object dispatch its own slot `0x6a0` — `0x6a0 / 4` = slot **424**, `CreateComponents`. So the
// NPC-side post-construct pass observes everything the base pass built and nothing it has not, and
// no state is written here directly. Slot 424 is family Lifecycle's, already ported, and is
// DISPATCHED here rather than re-recovered.
//
// `PostConstructor` is a Troika-line slot, so its body is the generated virtual and is defined —
// not declared — in this family's `.cpp`. The base half is named here because it is a distinct
// retail function: `CBaseCombatCharacter::PostConstructor` (`0x100035e4` -> the chain's) sets the
// entity's classname and registers it, which this runtime's `FElysiumEntity::Construct` has already
// done by the time any NPC stands.

/** Whether slot 106 has run, and the name it was handed. Retail passes the classname string; this
 *  runtime's construct path already carries it, so the argument is recorded rather than applied. */
FString PostConstructorName;
int32 PostConstructorCalls = 0;

// --- `FUN_10290350` — `RunAlternateAI` mode 4, the door-blocked transaction ------------------------

/** `FUN_10290350` (`0x10290350`), the fourth arm of the door transaction family Conditions carries
 *  `EnterAlternateAi` and `RunAlternateAiOpeningDoor` (mode 1) for. Retail, in order:
 *
 *    1. `m_bForceMaintainActivity` (`+0x65fa`) := 1 across `CAI_BaseNPC::MaintainActivity`
 *       (`0x102727d0`), then := 0. The latch spans exactly that one call.
 *    2. When `m_hOpeningDoor` (`+0x5d24`) still resolves AND that door's `m_toggle_state`
 *       (`+0x4f8`) is 0 (fully closed): stop the motor (`0x102bf7e0`, family Motor's
 *       `ResumeScheduledMove`) and `m_eAlternateAI` (`+0x644c`) := 0.
 *    3. A hull trace from slot 217 `GetAbsOrigin()` to that origin plus `m_vecForward`
 *       (`+0x6290`) scaled by `_DAT_10451acc` — **64.0**, read out of the pinned image — with mask
 *       `0x202400b` and radius `100.0`, through the filter at `+0x5d40`. On a HIT: stop the motor,
 *       `m_hOpeningDoor` := -1, `m_bOpeningDoorWait` (`+0x5d30`) := false, `m_eAlternateAI` := 0.
 *    4. Once `curtime` has reached `m_flAlternateAIExpireTimer` (`+0x6450`) — the test is
 *       `curtime < timer` and the ELSE arm fires, so an equal stamp expires — dispatch slot `0x700`
 *       (**448**) `TaskFail` with `0xe` and clear the same three fields.
 *
 *  Always answers **true** (`CONCAT31(..., 1)`), so the transaction keeps the body. */
bool RunAlternateAiDoorMode4(double Now);

/** SEAM for `CAI_BaseNPC::MaintainActivity` (`0x102727d0`), the one call `m_bForceMaintainActivity`
 *  brackets. Retail's body re-publishes the ideal activity onto the body when the current sequence
 *  has finished (or when the force latch is up, which is what step 1 raises it for).
 *
 *  **Not a vtable slot** (`vtmb_func 0x102727d0`: `__thiscall`, no dispatch site), so it takes its
 *  own name here. This runtime publishes locomotion and reaction bands from the animation layer
 *  rather than from an NPC-side maintain pass, so there is no ideal activity to push and this
 *  answers nothing. It is COUNTED, because the observable half at this call site is that the latch
 *  was up across exactly one call and down on either side of it. */
void MaintainActivity();
int32 MaintainActivityCalls = 0;
/** The value `m_bForceMaintainActivity` held while `MaintainActivity` last ran — the whole reason
 *  step 1 exists, and what a case asserts instead of a body this substrate does not have. */
bool bForceMaintainActivitySeenByLastMaintain = false;

/** SEAM for the hull trace at step 3 — `thunk_FUN_102e6d70(filter, 0, start, end, 0x202400b, 0,
 *  100.0, 0, &trace, 0, 0)`. This runtime's move solver has no radius-hull sweep against an
 *  arbitrary content mask at kernel level, so this answers **false** (no hit), which is retail's
 *  own "the way ahead is clear" arm and leaves the expiry arm as the one that ends the
 *  transaction. Counted, so a case can state that the trace was asked for. */
bool AlternateAiDoorSweepHit(const FVector& StartCm, const FVector& EndCm);
int32 AlternateAiDoorSweeps = 0;

/** `m_toggle_state` (`CBaseDoor +0x4f8`), the one word step 2 reads off the door it is holding.
 *  **SEAM**: this runtime's doors are `FElysiumMover` and carry their own phase rather than
 *  Source's four-state toggle; `0` is `TS_AT_TOP`… retail's own numbering makes `0` the CLOSED
 *  rest state for a door that opens upward, which is the value this answers for a mover at rest.
 *  Named so the day the mover's phase is mapped the read moves with it. */
int32 OpeningDoorToggleState() const;

// --- The species words this family's bodies read ---------------------------------------------------
//
// Each carries its offset and the retail class that owns it, the convention family Lifecycle set:
// one offset means a different thing per class, and `+0x6674` alone is five different fields.

/** `+0x66f4 CNPC_VMingXiao::m_rflRegrowTimers[6]` — the six tentacle regrow stamps
 *  `CNPC_VMingXiao::Save` brackets the archive with. SIX is the loop bound in both bodies
 *  (`iVar1 = 6`), and `ElysiumNpcKernelShape.cpp` gives the array a 4-byte stride. */
static constexpr int32 MingXiaoRegrowTimerCount = 6;
double MingXiaoRegrowTimers[MingXiaoRegrowTimerCount] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

/** `+0x6674 CNPC_VMingXiaoTentacle::m_flPhaseExpireTimer` — the tentacle's phase deadline. */
double MingXiaoTentaclePhaseExpireTimer = 0.0;

/** `+0x5f64 CScriptedTarget::m_flPauseDoneTime`. The same offset is `CCineNPC::m_iFinishSchedule`
 *  on the cine line, which is exactly why it is declared by retail class. */
double ScriptedTargetPauseDoneTime = 0.0;

/** `+0x6680 CNPC_VVampireBoss::m_pMonsterModelName` and `+0x6694 m_pszMonsterClassname` — the two
 *  words `CNPC_VVampireBoss::Restore` resets. Neither had a carrier before this story. */
FString VampireBossMonsterModelName;
FString VampireBossMonsterClassname;

/** `+0x6671 CNPC_VCop::m_bCountedAlive` and `+0x6672`, its unnamed twin. The census row names only
 *  the first; the second is read and written by the same body at the same width and has no
 *  recovered name, so it is spelled by offset — the convention 29b uses for an unsettled word.
 *  `+0x6671` is `CNPC_VTzimisceRunner::m_bDeathNoticeProcessed` on another class, which family
 *  Species already declares, so these are declared by class here. */
bool bCopCountedAlive = false;      // +0x6671 CNPC_VCop
bool bCopCountedSecond = false;     // +0x6672 CNPC_VCop

/** `DAT_1093acac` and `DAT_1093acb0`, the two PROCESS-WIDE live-cop censuses `CNPC_VCop` keeps.
 *  Statics and not per-NPC words, because retail's are: `CNPC_VCop::Spawn` increments the first for
 *  every cop in the level and `SelectSchedule` reads the total.
 *
 *  Ported as file statics, the way family Lifecycle ported the Werewolf's shared `rdtsc` pair, with
 *  named accessors so a fixture can read and reset them. **They answer nothing today in the sense
 *  that nothing INCREMENTS them** — `CNPC_VCop::Spawn` and `OnStateChange` are other stories' rows
 *  — so the decrement this family ports is the only writer so far. That is a missing producer, not
 *  a missing rule, and the guard (`only when the byte is set`) is what keeps the count from going
 *  negative meanwhile. */
static int32& CopAliveCensus();    // DAT_1093acac
static int32& CopSecondCensus();   // DAT_1093acb0
