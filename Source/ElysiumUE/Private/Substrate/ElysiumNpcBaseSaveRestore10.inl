// `CAI_BaseNPC`'s declarations of the `SaveRestore10` family (story 5 step 5),
// moved from `ElysiumNpcSaveRestore10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSaveRestore10.cpp`.

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

/** `+0x19b4`, the header the NPC record last read back (`SerializeExtendedHeader` on load); what
 *  `OnRestore` `0x1027bf50` re-finds the saved program from. */
FAiExtendedSaveHeader LastSavedExtendedHeader;

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

/** `0x102ee6a0` on `m_pNavigator` — `IsGoalActive`, "a head waypoint exists", header bit `0x4`.
 *  Answers the mover's navigation sample (see `NavIsGoalActive`) until the navigator's move step pops
 *  the head at arrival; `Navigator.IsGoalActive()` is the retail word. */
bool NavigatorGoalIsActive() const;

