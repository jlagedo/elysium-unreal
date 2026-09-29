// `CAI_BaseNPC`'s declarations of the `SaveRestore10` family (story 5 step 5),
// moved from `ElysiumNpcSaveRestore10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSaveRestore10.cpp`.

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

/** The header the last `FElysiumNpcBase::Save` built, kept so a case can read the flags without an archive. */
FAiExtendedSaveHeader LastSavedExtendedHeader;

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

/** Build the header from live state — retail's own block, shared by `FElysiumNpcBase::Save` and by the record
 *  `FElysiumNpcBase::Serialize` writes. */
FAiExtendedSaveHeader BuildExtendedSaveHeader() const;

/** `ISave` vtable `+0x08` — `WriteFields(&header, &datamap_AIExtendedSaveHeader_t)`. Routes the
 *  four words through `FElysiumSaveArchive` when `Archive` is non-null, and records the call. */
void SaveWriteFields(void* Archive, FAiExtendedSaveHeader& Header);

/** `0x102ee6a0` on `m_pNavigator` — `IsGoalActive`, "a head waypoint exists", header bit `0x4`.
 *  Answers the mover's navigation sample (see `NavIsGoalActive`) until the navigator's move step pops
 *  the head at arrival; `Navigator.IsGoalActive()` is the retail word. */
bool NavigatorGoalIsActive() const;

/** Whether slot 106 has run, and the name it was handed. Retail passes the classname string; this
 *  runtime's construct path already carries it, so the argument is recorded rather than applied. */
FString PostConstructorName;

int32 PostConstructorCalls = 0;
