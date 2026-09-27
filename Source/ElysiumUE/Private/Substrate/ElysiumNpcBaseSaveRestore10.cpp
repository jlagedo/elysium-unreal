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
#include "Substrate/ElysiumNpcKernelClassLookup.h"
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

double FElysiumNpcBase::SaveStampFloatMax()
{
	// `3.4028235e+38` in the decompiled C, which is `FLT_MAX` exactly. The compare retail makes is a
	// 32-bit one; a `double` stamp only matches when it holds the widened constant, which is what
	// every port writer of an "infinite" stamp stores (family Bosses' `IgnoreCollisionUntil` is the
	// one this family meets).
	return static_cast<double>(TNumericLimits<float>::Max());
}

bool FElysiumNpcBase::SaveStampEncode(double& Stamp, ESaveStampMode Mode)
{
	// `FUN_101cf250`, switch arm for switch arm. A mode outside 1..4 falls through retail's
	// `default:` and writes nothing.
	switch (Mode)
	{
	case ESaveStampMode::BelowZero:
		// `case 1: if (*param_1 < _DAT_104454c4)` — STRICTLY below 0.0. A stamp of exactly 0.0 is
		// not encoded by this mode, which is why mode 3 exists beside it.
		if (Stamp < SaveStampZero)
		{
			Stamp = SaveStampSentinel;
			return true;
		}
		return false;
	case ESaveStampMode::MinusOne:
		// `case 2: bVar1 = *param_1 == -1.0;` then the shared `LAB_101cf2ab`.
		if (Stamp == -1.0)
		{
			Stamp = SaveStampSentinel;
			return true;
		}
		return false;
	case ESaveStampMode::Zero:
		// `case 3: if (*param_1 == _DAT_104454c4)` — exactly 0.0.
		if (Stamp == SaveStampZero)
		{
			Stamp = SaveStampSentinel;
			return true;
		}
		return false;
	case ESaveStampMode::FloatMax:
		// `case 4: bVar1 = *param_1 == 3.4028235e+38;` then `LAB_101cf2ab`.
		if (Stamp == SaveStampFloatMax())
		{
			Stamp = SaveStampSentinel;
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool FElysiumNpcBase::SaveStampDecode(double& Stamp, ESaveStampMode Mode)
{
	// `FUN_101cf2f0`. Every arm shares one guard — `_DAT_10482fac <= *param_1`, which is `1e+10` —
	// and differs only in what it writes. Modes 1 and 2 share a `case` label, so mode 1 is NOT its
	// own inverse: a `-0.5` that mode 1 encoded comes back as `-1.0`. That is retail's, and the
	// suite states it by name.
	if (Stamp < SaveStampSentinelFloor)
	{
		return false;
	}
	switch (Mode)
	{
	case ESaveStampMode::BelowZero:
	case ESaveStampMode::MinusOne:
		Stamp = -1.0;
		return true;
	case ESaveStampMode::Zero:
		Stamp = SaveStampZero;
		return true;
	case ESaveStampMode::FloatMax:
		Stamp = SaveStampFloatMax();
		return true;
	default:
		return false;
	}
}

bool FElysiumNpcBase::SaveStampEncode(float& Stamp, ESaveStampMode Mode)
{
	double Widened = static_cast<double>(Stamp);
	const bool bChanged = SaveStampEncode(Widened, Mode);
	if (bChanged)
	{
		Stamp = static_cast<float>(Widened);
	}
	return bChanged;
}

bool FElysiumNpcBase::SaveStampDecode(float& Stamp, ESaveStampMode Mode)
{
	double Widened = static_cast<double>(Stamp);
	const bool bChanged = SaveStampDecode(Widened, Mode);
	if (bChanged)
	{
		Stamp = static_cast<float>(Widened);
	}
	return bChanged;
}

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

void FElysiumNpcBase::SaveWriteFields(void* Archive, FAiExtendedSaveHeader& Header)
{
	// `(**(code **)(*param_1 + 8))(&header, &datamap_AIExtendedSaveHeader_t)` — `ISave::WriteFields`.
	// The datamap is `0x105cabd0`, which `CAI_BaseNPC::Restore` reads the same block back through.
	SaveArchiveLog.Add({ FSaveArchiveOp::EKind::Fields, TEXT("AIExtendedSaveHeader_t"),
		static_cast<int32>(Header.Flags) });
	LastSavedExtendedHeader = Header;
	if (FElysiumSaveArchive* Ar = static_cast<FElysiumSaveArchive*>(Archive))
	{
		// The four words in the block's own layout order (`+0x00` version, `+0x04` flags, `+0x08`
		// the 128-byte name, `+0x88` the CRC).
		*Ar << Header.Version;
		*Ar << Header.Flags;
		*Ar << Header.ScheduleName;
		*Ar << Header.ScheduleCrc;
	}
}

bool FElysiumNpcBase::NavigatorGoalIsActive() const
{
	// SEAM for `thunk_FUN_102ee6a0(m_pNavigator)`, header bit `0x4`. Nothing in this runtime stands
	// a `CAI_Navigator` goal object — family Senses records the same absence for the node graph —
	// so the bit is never set. Named rather than inlined so the day a navigator lands the bit moves
	// with it.
	return false;
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
	// `CAI_BaseNPC::Save` `0x1027bc60`, read off the listing because the checklist's walk read the
	// two MODE arguments as counts. `1027bc6c PUSH 0x4 / LEA EBP,[ESI+0x5b8c]` and
	// `1027bc80 PUSH 0x3 / LEA EBX,[ESI+0x5db4]`: exactly two encode calls.
	SaveStampEncode(ExtendedBlockedByFriendTimer, ESaveStampMode::FloatMax);   // +0x5b8c, mode 4
	SaveStampEncode(BaseScheduleHost.WaitFinished, ESaveStampMode::Zero);          // +0x5db4, mode 3

	// `if (m_pMotor +0x5d44) thunk_FUN_102e0b60(m_pMotor);` — the motor's pre-archive pointer
	// fix-up, GUARDED; then `thunk_FUN_102e8aa0(&m_MoveAndShootOverlay +0x5cf4)`, which is NOT.
	if (Motor != nullptr)
	{
		++MotorSaveFixups;
	}
	++MoveAndShootSaveFixups;

	// `AIExtendedSaveHeader_t`, built on the stack at `ESP+0x14`.
	FAiExtendedSaveHeader Header = BuildExtendedSaveHeader();
	SaveWriteFields(Archive, Header);

	// `uVar3 = CBaseCombatCharacter::Save(this, param_1);` — the chain's answer, and this body's.
	const int32 ChainResult = GChainSaveResult;

	// The decode, in the same order as the encode, then the two post-fixups.
	SaveStampDecode(ExtendedBlockedByFriendTimer, ESaveStampMode::FloatMax);
	SaveStampDecode(BaseScheduleHost.WaitFinished, ESaveStampMode::Zero);
	if (Motor != nullptr)
	{
		++MotorRestoreFixups;
	}
	++MoveAndShootRestoreFixups;
	return ChainResult;
}

// -------------------------------------------------------------------------------------------------
// Slot 106 `PostConstructor`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcBase::PostConstructor(TCHAR* Name)
{
	// `CAI_BaseNPC::PostConstructor` `0x1027bb20`, 27 bytes whose entire content is an ORDER:
	//
	//     1027bb28  CALL CBaseCombatCharacter::PostConstructor(name)
	//     1027bb31  CALL [this->vtable + 0x6a0]        ; 0x6a0 / 4 = slot 424 CreateComponents
	//
	// The base pass runs to completion first, so the NPC-side pass observes everything it built and
	// nothing it has not. No state is written here directly.
	//
	// The base half is `FElysiumEntity::Construct`'s classname bind in this runtime and has already
	// run by the time any NPC stands, so the name is RECORDED rather than applied — applying it a
	// second time would be a second event retail's order does not have.
	PostConstructorName = Name != nullptr ? FString(Name) : FString();
	++PostConstructorCalls;
	CreateComponents();   // slot 424, family Lifecycle's
}
