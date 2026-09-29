// `CAI_BaseNPC`'s bodies of the `SaveRestore10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSaveRestore10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `CBaseCombatCharacter::Save`'s answer. Retail's `ISave` chain answers a non-zero "wrote
	// something" for every entity that has a datamap, and `CAI_BaseNPC::Save` returns it verbatim.
	// CHOSEN as 1 rather than recovered: the chain body is not an NPC-kernel row and the corpus does
	// not pin its return for a given archive. Every caller in the closure tests it against zero, so
	// the arm taken is the same one retail takes for a live NPC.
	constexpr int32 GChainSaveResult = 1;
	// `AIExtendedSaveHeader_t`'s three flag bits, in the order `0x1027bc60` ORs them.
	constexpr uint32 GExtendedHeaderFlagEnemy = 0x1;
	constexpr uint32 GExtendedHeaderFlagTargetEnt = 0x2;
	constexpr uint32 GExtendedHeaderFlagNavigatorGoal = 0x4;
	// `MOV EDI,1 / MOV word ptr [ESP+0x14],DI` — the header's version word.
	constexpr int16 GExtendedHeaderVersion = 1;
	// `PUSH 0x80` — `Q_strncpy(name, schedule+0x40, 0x80)`. The name is truncated to 127 characters
	// plus the NUL, which is a limit a schedule name can reach and so is reproduced.
	constexpr int32 GExtendedHeaderScheduleNameBytes = 0x80;
	// `schedule+0x24 << 3` — retail's `Task_t` is `{ int iTask; float flTaskData; }`, eight bytes.
	constexpr int32 GScheduleTaskRecordBytes = 8;
	// `DAT_10496f58`, the CRC32 table `0x1023f0c0` indexes. **The bytes are not in the corpus**; the
	// loop's shape is the standard reflected byte-at-a-time CRC-32, so the table is generated from
	// `0xedb88320` here and the routine is pinned against the published `"123456789"` check value.
	// See the `.inl` for what that inference does and does not claim.
	constexpr uint32 GCrc32ReflectedPolynomial = 0xedb88320u;
	const uint32* SaveRestore10Crc32Table()
	{
		static uint32 Table[256];
		static bool bBuilt = false;
		if (!bBuilt)
		{
			for (uint32 Index = 0; Index < 256; ++Index)
			{
				uint32 Cell = Index;
				for (int32 Bit = 0; Bit < 8; ++Bit)
				{
					Cell = (Cell & 1u) != 0u ? (Cell >> 1) ^ GCrc32ReflectedPolynomial : (Cell >> 1);
				}
				Table[Index] = Cell;
			}
			bBuilt = true;
		}
		return Table;
	}
}

// --- Moved from `ElysiumNpcSaveRestore10.cpp` (story 5 step 5) ---

uint32 FElysiumNpcBase::SaveCrc32Init()
{
	// `FUN_1023f040` — `*param_1 = 0xffffffff;`
	return 0xffffffffu;
}

uint32 FElysiumNpcBase::SaveCrc32Update(uint32 Crc, const uint8* Bytes, int32 Count)
{
	// `FUN_1023f0c0`. The decompiled C is the classic Duff-style unrolled CRC — a `switch` on the
	// remaining count with fall-through cases 0..7, an alignment prologue keyed on
	// `(uintptr)param_2 & 3`, then a four-bytes-at-a-time body. Every arm computes the same
	// recurrence, `crc = (crc >> 8) ^ table[(byte ^ crc) & 0xff]`, so the unrolling is a speed
	// detail and the byte loop below is the same function.
	if (Bytes == nullptr || Count <= 0)
	{
		return Crc;
	}
	const uint32* const Table = SaveRestore10Crc32Table();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Crc = (Crc >> 8) ^ Table[(Bytes[Index] ^ Crc) & 0xffu];
	}
	return Crc;
}

uint32 FElysiumNpcBase::SaveCrc32Final(uint32 Crc)
{
	// `FUN_1023f060` — `*param_1 = ~*param_1;`
	return ~Crc;
}

void FElysiumNpcBase::ScheduleTaskBytes(TArray<uint8>& OutBytes) const
{
	// `schedule+0x20` is the `Task_t` array and `schedule+0x24` its count; the CRC runs over
	// `count << 3` bytes, so eight per task. See the `.inl` for what this seam does and does not
	// reproduce.
	OutBytes.Reset();
	if (!Schedule.IsRunning())
	{
		return;
	}
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(Schedule.Current);
	if (Program == nullptr)
	{
		return;
	}
	OutBytes.Reserve(Program->Tasks.Num() * GScheduleTaskRecordBytes);
	for (const FElysiumScheduleStep& Step : Program->Tasks)
	{
		// The two words retail hashes, as retail's `memcpy` sees them: the task ID and the raw 32
		// bits of the data word. The port used to hash a declaration ordinal of its own closed task
		// enum -- a number that silently renumbered whenever anyone edited the enum -- against a
		// float it had already converted. Both halves are the corpus's own now.
		const int32 TaskId = Step.TaskId;
		const uint32 TaskData = Step.RawWord();
		OutBytes.Append(reinterpret_cast<const uint8*>(&TaskId), sizeof(int32));
		OutBytes.Append(reinterpret_cast<const uint8*>(&TaskData), sizeof(uint32));
	}
}

bool FElysiumNpcBase::NavigatorGoalIsActive() const
{
	// `thunk_FUN_102ee6a0(m_pNavigator)` — SDK `CAI_Navigator::IsGoalActive()`:
	// `102ee6a0 MOV EAX,[ECX+0x30]` (`m_pPath`), `102ee6a5 JZ`, `102ee6a7 MOV ECX,[EAX+0x24]` (the
	// path's current waypoint), `102ee6ac JZ` -> 0, else 1. Read by the RunTask spines (Troika
	// `0x102aaf72`, base `0x10288f8a`), the interrupt-distance gates of `0x102b27f0` and save header
	// bit `0x4`.
	//
	// The navigator's head waypoint (`path+0x24`), popped by the move step at arrival (0018 story 5).
	// Retail's `0x102ee680` is a different question (`IsGoalSet`, `NavigatorIsGoalSet`).
	return Navigator.IsGoalActive();
}

// The `AIExtendedSaveHeader_t` half of `CAI_BaseNPC::Save 0x1027bc60`, on its own so the two save
// paths build one header. Retail writes exactly this block by hand and defers every other word to
// the datamap walk; this port's generated SAVE walk is that walk, so this is the one hand block the
// NPC record carries, and `FElysiumNpcBase::OnRestore` `0x1027bf50` is what reads it back.
FElysiumNpcBase::FAiExtendedSaveHeader FElysiumNpcBase::BuildExtendedSaveHeader() const
{
	FAiExtendedSaveHeader Header;
	Header.Version = GExtendedHeaderVersion;
	Header.Flags = 0;
	// Bit 0x1: slot 0x29c `GetEnemy()` is non-null.
	if (BaseMemory.Enemy.IsSet())
	{
		Header.Flags |= GExtendedHeaderFlagEnemy;
	}
	// Bit 0x2: `m_hTargetEnt` (`+0x5ce4`) passes its `& 0x1fff` index and `>> 13` serial check onto
	// a LIVE entity. This runtime's handle resolution IS that check, and a handle that is merely set
	// but stale resolves to null — which is the arm retail's serial mismatch takes.
	if (World != nullptr && World->Resolve(GetTarget()) != nullptr)
	{
		Header.Flags |= GExtendedHeaderFlagTargetEnt;
	}
	// Bit 0x4: the navigator goal.
	if (NavigatorGoalIsActive())
	{
		Header.Flags |= GExtendedHeaderFlagNavigatorGoal;
	}
	// `if (m_pSchedule +0x5c38)`: the name and the task checksum, else a cleared name and a zero
	// CRC. CRC32 over zero bytes is `~0xffffffff == 0`, so the two arms agree on the checksum and
	// only the name differs — which is why the else arm writes just `name[0] = 0`.
	if (Schedule.IsRunning())
	{
		Header.ScheduleName = FString(ElysiumScheduleName(Schedule.Current)).Left(
			GExtendedHeaderScheduleNameBytes - 1);
		TArray<uint8> TaskBytes;
		ScheduleTaskBytes(TaskBytes);
		uint32 Crc = SaveCrc32Init();
		Crc = SaveCrc32Update(Crc, TaskBytes.GetData(), TaskBytes.Num());
		Header.ScheduleCrc = SaveCrc32Final(Crc);
	}
	else
	{
		Header.ScheduleName.Reset();
		Header.ScheduleCrc = 0;
	}
	return Header;
}

int32 FElysiumNpcBase::Save(void* Archive)
{
	// `CAI_BaseNPC::Save` `0x1027bc60`, forwarded to the NPC record (0019/6, service
	// `FElysiumSaveArchive`). Its one hand block, `AIExtendedSaveHeader_t`, is what
	// `SerializeExtendedHeader` writes (built by `BuildExtendedSaveHeader` above). The rest has no
	// work here: the two sentinel encodes (`0x101cf250`, modes 4 and 3 on `+0x5b8c` / `+0x5db4`)
	// guard retail's time re-base, which this walk does not do, and the motor / move-and-shoot
	// fix-ups (`0x102e0b60`, `0x102e8aa0`) re-link raw pointers this record never saves.
	if (FElysiumSaveArchive* Ar = static_cast<FElysiumSaveArchive*>(Archive)) { SerializeExtendedHeader(*Ar); }
	return GChainSaveResult;
}

