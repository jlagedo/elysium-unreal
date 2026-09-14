#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"                  // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29d, family **SaveRestore10 + Lifecycle10** — slot 126 `Save`, slot 127 `Restore`, slot 180
// `UpdateOnRemove`, slot 106 `PostConstructor`, the `CAI_BaseNPC` bodies beside them, their species
// arms, and `RunAlternateAI`'s mode-4 door body.
//
// Every constant below was read off the decompiled C and, where the decompiler folded or aliased an
// argument, off the listing — `0x102993c0`'s decode pass and `0x10371a90`'s two census bytes are
// both listing reads. `ElysiumNpcKernelSaveRestore10.inl` carries the family's reading notes; the
// walked prose is `docs/vtmb/npc-ai/lifecycle.md`.

namespace
{
	// The four slots this family fills, spelled once.
	constexpr int32 GSaveSlot = 126;
	constexpr int32 GRestoreSlot = 127;
	constexpr int32 GUpdateOnRemoveSlot = 180;

	// `1028d6e6 PUSH 0x40a00000` — `ClearHintNode(this, 5.0)`, the Troika `UpdateOnRemove`'s hint
	// reuse delay. The BASE body `0x1027ca30` passes 0.0, and the difference is the point.
	constexpr float GTroikaRemoveHintReuseSeconds = 5.0f;

	// `_DAT_10451acc` — **64.0**, read out of the pinned `vampire.dll` at file offset `0x451acc`
	// (base `0x10000000`). The forward scale `RunAlternateAI` mode 4 traces over, in SOURCE units.
	constexpr float GAlternateAiDoorProbeUnits = 64.0f;
	// `10290452 PUSH 0x202400b` and the `100.0` radius, both literals in the listing.
	constexpr int32 GAlternateAiDoorTraceMask = 0x202400b;
	constexpr float GAlternateAiDoorTraceRadiusUnits = 100.0f;
	// `TaskFail(0xe)` — the failure reason mode 4's expiry arm raises.
	constexpr int32 GAlternateAiDoorTaskFailReason = 0xe;
	// `m_toggle_state` (`CBaseDoor +0x4f8`) — the value mode 4's step 2 requires.
	constexpr int32 GDoorToggleStateClosed = 0;

	// `s_Leaving_interesting_place__UpdateO_105d87d4`, read off the listing at `1028d702`. It is what
	// settles that `0x102b53d0` is the interesting-place release and not a grapple teardown.
	const TCHAR* const GLeaveInterestingPlaceReason =
		TEXT("Leaving interesting place (UpdateOnRemove)");

	// `s_npc_VVampireBoss_1065e8dc` — the literal `CNPC_VVampireBoss::Restore` resets
	// `m_pszMonsterClassname` (`+0x6694`) to.
	const TCHAR* const GVampireBossDefaultClassname = TEXT("npc_VVampireBoss");

	// `CBaseCombatCharacter::Save`'s answer. Retail's `ISave` chain answers a non-zero "wrote
	// something" for every entity that has a datamap, and `CAI_BaseNPC::Save` returns it verbatim.
	// CHOSEN as 1 rather than recovered: the chain body is not an NPC-kernel row and the corpus does
	// not pin its return for a given archive. Every caller in the closure tests it against zero, so
	// the arm taken is the same one retail takes for a live NPC.
	constexpr int32 GChainSaveResult = 1;
	// `CBaseCombatCharacter::Restore`'s answer, on the same footing.
	constexpr int32 GChainRestoreResult = 1;

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

	// `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`), the gate `UpdateOnRemove` opens its dialogue
	// arm with. Four terms: `m_bIsTalking` (`+0x64c0`), the queued dialogue string (`+0x64ec`), the
	// dialogue partner handle (`+0xfe8`) and the bound speech scene (`+0x6554`). This runtime
	// carries one session bit for the last three and a talk-end stamp for the first — the same
	// reading `ElysiumNpcThinkCadence.cpp` and family Sounds already made, repeated here as a
	// function rather than as a third reading so the three cannot drift.
	bool SaveRestore10IsInDialog(const FElysiumNpc& Npc)
	{
		const double Now = Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
		return Npc.Dialogue.bInDialog || Npc.IsTalking(Now);
	}

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

	// `DAT_1093acac` and `DAT_1093acb0`, the two process-wide cop censuses. File statics because
	// retail's are file statics — the same shape family Lifecycle gave the Werewolf's shared
	// `rdtsc` pair.
	int32 GCopAliveCensus = 0;
	int32 GCopSecondCensus = 0;
}

// -------------------------------------------------------------------------------------------------
// The sentinel codec — `0x101cf250` and `0x101cf2f0`.
// -------------------------------------------------------------------------------------------------

double FElysiumNpc::SaveStampFloatMax()
{
	// `3.4028235e+38` in the decompiled C, which is `FLT_MAX` exactly. The compare retail makes is a
	// 32-bit one; a `double` stamp only matches when it holds the widened constant, which is what
	// every port writer of an "infinite" stamp stores (family Bosses' `IgnoreCollisionUntil` is the
	// one this family meets).
	return static_cast<double>(TNumericLimits<float>::Max());
}

bool FElysiumNpc::SaveStampEncode(double& Stamp, ESaveStampMode Mode)
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

bool FElysiumNpc::SaveStampDecode(double& Stamp, ESaveStampMode Mode)
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

bool FElysiumNpc::SaveStampEncode(float& Stamp, ESaveStampMode Mode)
{
	double Widened = static_cast<double>(Stamp);
	const bool bChanged = SaveStampEncode(Widened, Mode);
	if (bChanged)
	{
		Stamp = static_cast<float>(Widened);
	}
	return bChanged;
}

bool FElysiumNpc::SaveStampDecode(float& Stamp, ESaveStampMode Mode)
{
	double Widened = static_cast<double>(Stamp);
	const bool bChanged = SaveStampDecode(Widened, Mode);
	if (bChanged)
	{
		Stamp = static_cast<float>(Widened);
	}
	return bChanged;
}

void FElysiumNpc::SaveSoundStampEncode(FElysiumGameSoundEvent& Sound)
{
	// `FUN_101b9840` — `thunk_FUN_101cf250((float*)(sound + 0x10), 2)`. `CSound +0x10` is
	// `m_flExpireTime`, `fieldType 15` (`FIELD_TIME`) in the class's datamap.
	SaveStampEncode(Sound.ExpireTime, ESaveStampMode::MinusOne);
}

void FElysiumNpc::SaveSoundStampDecode(FElysiumGameSoundEvent& Sound)
{
	// `FUN_101b9860` — the same field at the same mode through `0x101cf2f0`.
	SaveStampDecode(Sound.ExpireTime, ESaveStampMode::MinusOne);
}

// -------------------------------------------------------------------------------------------------
// The CRC32 `AIExtendedSaveHeader_t`'s last word carries — `0x1023f040`, `0x1023f0c0`, `0x1023f060`.
// -------------------------------------------------------------------------------------------------

uint32 FElysiumNpc::SaveCrc32Init()
{
	// `FUN_1023f040` — `*param_1 = 0xffffffff;`
	return 0xffffffffu;
}

uint32 FElysiumNpc::SaveCrc32Update(uint32 Crc, const uint8* Bytes, int32 Count)
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

uint32 FElysiumNpc::SaveCrc32Final(uint32 Crc)
{
	// `FUN_1023f060` — `*param_1 = ~*param_1;`
	return ~Crc;
}

void FElysiumNpc::ScheduleTaskBytes(TArray<uint8>& OutBytes) const
{
	// `schedule+0x20` is the `Task_t` array and `schedule+0x24` its count; the CRC runs over
	// `count << 3` bytes, so eight per task. See the `.inl` for what this seam does and does not
	// reproduce.
	OutBytes.Reset();
	if (!Schedule.IsRunning())
	{
		return;
	}
	const FElysiumSchedule* Program = ElysiumScheduleFor(Schedule.Current);
	if (Program == nullptr)
	{
		return;
	}
	OutBytes.Reserve(Program->Tasks.Num() * GScheduleTaskRecordBytes);
	for (const FElysiumTaskStep& Step : Program->Tasks)
	{
		const int32 TaskId = static_cast<int32>(Step.Task);
		const float TaskData = Step.Param;
		OutBytes.Append(reinterpret_cast<const uint8*>(&TaskId), sizeof(int32));
		OutBytes.Append(reinterpret_cast<const uint8*>(&TaskData), sizeof(float));
	}
}

// -------------------------------------------------------------------------------------------------
// The archive seam.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SaveWriteFields(void* Archive, FAiExtendedSaveHeader& Header)
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

void FElysiumNpc::SaveWriteBool(void* Archive, const TCHAR* Field, bool bValue)
{
	// `ISave` vtable `+0x30` — `WriteBool(&value, 1)`.
	SaveArchiveLog.Add({ FSaveArchiveOp::EKind::Bool, Field, bValue ? 1 : 0 });
	if (FElysiumSaveArchive* Ar = static_cast<FElysiumSaveArchive*>(Archive))
	{
		bool bLocal = bValue;
		*Ar << bLocal;
	}
}

void FElysiumNpc::SaveWriteInt(void* Archive, const TCHAR* Field, int32 Value)
{
	// `ISave` vtable `+0x28` — `WriteInt(&value, 1)`.
	SaveArchiveLog.Add({ FSaveArchiveOp::EKind::Int, Field, Value });
	if (FElysiumSaveArchive* Ar = static_cast<FElysiumSaveArchive*>(Archive))
	{
		int32 Local = Value;
		*Ar << Local;
	}
}

bool FElysiumNpc::RestoreReadBool(void* Archive, const TCHAR* Field)
{
	// `IRestore` vtable `+0x44` — `ReadBool(&value, 1, 0)`. With no archive the read answers
	// **false**, which is the arm that skips the two `ReadInt`s; a runtime that never wrote the bool
	// is a runtime whose pedestrian link was not bound, and that is the same answer.
	bool bValue = false;
	if (FElysiumSaveArchive* Ar = static_cast<FElysiumSaveArchive*>(Archive))
	{
		*Ar << bValue;
	}
	SaveArchiveLog.Add({ FSaveArchiveOp::EKind::ReadBool, Field, bValue ? 1 : 0 });
	return bValue;
}

void FElysiumNpc::RestoreReadInt(void* Archive, const TCHAR* Field, int32& Value)
{
	// `IRestore` vtable `+0x3c` — `ReadInt(&value, 1, 0)`. With no archive the destination is left
	// as it stands, which is retail's own behaviour for a field the archive holds no record of.
	if (FElysiumSaveArchive* Ar = static_cast<FElysiumSaveArchive*>(Archive))
	{
		*Ar << Value;
	}
	SaveArchiveLog.Add({ FSaveArchiveOp::EKind::ReadInt, Field, Value });
}

bool FElysiumNpc::NavigatorGoalIsActive() const
{
	// SEAM for `thunk_FUN_102ee6a0(m_pNavigator)`, header bit `0x4`. Nothing in this runtime stands
	// a `CAI_Navigator` goal object — family Senses records the same absence for the node graph —
	// so the bit is never set. Named rather than inlined so the day a navigator lands the bit moves
	// with it.
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 126 `Save`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::BaseSave(void* Archive)
{
	// `CAI_BaseNPC::Save` `0x1027bc60`, read off the listing because the checklist's walk read the
	// two MODE arguments as counts. `1027bc6c PUSH 0x4 / LEA EBP,[ESI+0x5b8c]` and
	// `1027bc80 PUSH 0x3 / LEA EBX,[ESI+0x5db4]`: exactly two encode calls.
	SaveStampEncode(ExtendedBlockedByFriendTimer, ESaveStampMode::FloatMax);   // +0x5b8c, mode 4
	SaveStampEncode(ScheduleHost.WaitFinished, ESaveStampMode::Zero);          // +0x5db4, mode 3

	// `if (m_pMotor +0x5d44) thunk_FUN_102e0b60(m_pMotor);` — the motor's pre-archive pointer
	// fix-up, GUARDED; then `thunk_FUN_102e8aa0(&m_MoveAndShootOverlay +0x5cf4)`, which is NOT.
	if (Motor != nullptr)
	{
		++MotorSaveFixups;
	}
	++MoveAndShootSaveFixups;

	// `AIExtendedSaveHeader_t`, built on the stack at `ESP+0x14`.
	FAiExtendedSaveHeader Header;
	Header.Version = GExtendedHeaderVersion;
	Header.Flags = 0;
	// Bit 0x1: slot 0x29c `GetEnemy()` is non-null.
	if (Senses.Memory.Enemy.IsSet())
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
	SaveWriteFields(Archive, Header);

	// `uVar3 = CBaseCombatCharacter::Save(this, param_1);` — the chain's answer, and this body's.
	const int32 ChainResult = GChainSaveResult;

	// The decode, in the same order as the encode, then the two post-fixups.
	SaveStampDecode(ExtendedBlockedByFriendTimer, ESaveStampMode::FloatMax);
	SaveStampDecode(ScheduleHost.WaitFinished, ESaveStampMode::Zero);
	if (Motor != nullptr)
	{
		++MotorRestoreFixups;
	}
	++MoveAndShootRestoreFixups;
	return ChainResult;
}

namespace
{
	// `0x102993c0`'s eleven stamps, in the listing's order with the listing's modes. The DECODE pass
	// walks the identical list — the decompiled C renders its pointers one slot out through EBX and
	// stack aliasing, but the pushed modes read `3,2,2,3,3,3,3,2,2,4,4`, which is this list.
	struct FTroikaSaveStamp
	{
		const TCHAR* Field = nullptr;
		FElysiumNpc::ESaveStampMode Mode = FElysiumNpc::ESaveStampMode::None;
	};

	const FTroikaSaveStamp GTroikaSaveStamps[] =
	{
		{ TEXT("m_flCanSeekCoverTimer"),        FElysiumNpc::ESaveStampMode::Zero },      // +0x607c
		{ TEXT("m_flSeeUnknownCheatVisionTime"), FElysiumNpc::ESaveStampMode::MinusOne }, // +0x6084
		{ TEXT("m_flMeleeHeightDiffTimer"),     FElysiumNpc::ESaveStampMode::MinusOne },  // +0x6274
		{ TEXT("m_flOccludedReportTimeE"),      FElysiumNpc::ESaveStampMode::Zero },      // +0x62cc
		{ TEXT("m_flOccludedReportTimeT"),      FElysiumNpc::ESaveStampMode::Zero },      // +0x62d0
		{ TEXT("m_flOccludedReportTimeW"),      FElysiumNpc::ESaveStampMode::Zero },      // +0x62d4
		{ TEXT("m_flInterruptTime"),            FElysiumNpc::ESaveStampMode::Zero },      // +0x632c
		{ TEXT("m_flNextInterestChangeTime"),   FElysiumNpc::ESaveStampMode::MinusOne },  // +0x63d4
		{ TEXT("m_flWeaponScareTime"),          FElysiumNpc::ESaveStampMode::MinusOne },  // +0x63dc
		{ TEXT("m_flIgnoreCollisionTimer"),     FElysiumNpc::ESaveStampMode::FloatMax },  // +0x6458
		{ TEXT("m_flEyeFidgetTime"),            FElysiumNpc::ESaveStampMode::FloatMax },  // +0x657c
	};
}

int32 FElysiumNpc::TroikaSave(void* Archive)
{
	// `CAI_BaseNPCTroika::Save` `0x102993c0`. The eleven stamps, in the listing's order.
	//
	// Ten of the eleven are `double` on this runtime's leaf and one — `m_flEyeFidgetTime`
	// (`+0x657c`) — lands on the entity chain as `FElysiumCombatCharacter::NextFidgetTime`, a
	// `float`, which is why the codec carries both widths.
	SaveStampEncode(CanSeekCoverTimer, ESaveStampMode::Zero);
	SaveStampEncode(Senses.Memory.SeeUnknownGraceUntil, ESaveStampMode::MinusOne);
	SaveStampEncode(MeleeHeightDiffTimer, ESaveStampMode::MinusOne);
	SaveStampEncode(OccludedReportTimeE, ESaveStampMode::Zero);
	SaveStampEncode(OccludedReportTimeT, ESaveStampMode::Zero);
	SaveStampEncode(OccludedReportTimeW, ESaveStampMode::Zero);
	SaveStampEncode(ScheduleHost.InterruptTime, ESaveStampMode::Zero);
	SaveStampEncode(AmbientNextActivityAt, ESaveStampMode::MinusOne);
	SaveStampEncode(WeaponScareTime, ESaveStampMode::MinusOne);
	SaveStampEncode(IgnoreCollisionUntil, ESaveStampMode::FloatMax);
	SaveStampEncode(NextFidgetTime, ESaveStampMode::FloatMax);

	// The nine `CAISound` records, in the listing's order (`+0x60b0` up to `+0x6210`, stride 0x2c).
	SaveSoundStampEncode(Senses.Memory.BestSound);
	SaveSoundStampEncode(Senses.Memory.InvestigateSound);
	SaveSoundStampEncode(Senses.Memory.LastSoundDanger);
	SaveSoundStampEncode(Senses.Memory.LastSoundPhysicsDanger);
	SaveSoundStampEncode(Senses.Memory.LastSoundCombat);
	SaveSoundStampEncode(Senses.Memory.LastSoundBulletImpact);
	SaveSoundStampEncode(Senses.Memory.LastSoundPlayer);
	SaveSoundStampEncode(Senses.Memory.LastSoundWorld);
	SaveSoundStampEncode(Senses.Memory.LastSoundFlinch);

	// `CAI_BaseNPC::thunk_FUN_1027bc60(this, param_1)` — a DIRECT call, never a vtable dispatch, so
	// no species arm can re-enter through it.
	const int32 Result = BaseSave(Archive);

	// `SETNZ AL` on `m_pPedestrianLink` (`+0x630c`), written through `ISave +0x30`; then, only when
	// it is set, the two ints at `+0x630c+4` and `+0x630c+8`. The shape map records `+0x630c` as
	// ABSENT in this runtime and family Dialogue stands `bCrosswalkLinkBound` for "is the link
	// bound", so the bool is that flag and the two ints are the link's saved node pair, which this
	// runtime already carries as `m_iRestorePedLinkNode` / `m_iRestorePedLinkDestNode` — the exact
	// two fields slot 127 reads them back into.
	SaveWriteBool(Archive, TEXT("m_pPedestrianLink"), bCrosswalkLinkBound);
	if (bCrosswalkLinkBound)
	{
		SaveWriteInt(Archive, TEXT("m_pPedestrianLink+4"), RestorePedLinkNode);
		SaveWriteInt(Archive, TEXT("m_pPedestrianLink+8"), RestorePedLinkDestNode);
	}

	// The decode pass: the same eleven in the same order with the same modes, then the same nine.
	SaveStampDecode(CanSeekCoverTimer, ESaveStampMode::Zero);
	SaveStampDecode(Senses.Memory.SeeUnknownGraceUntil, ESaveStampMode::MinusOne);
	SaveStampDecode(MeleeHeightDiffTimer, ESaveStampMode::MinusOne);
	SaveStampDecode(OccludedReportTimeE, ESaveStampMode::Zero);
	SaveStampDecode(OccludedReportTimeT, ESaveStampMode::Zero);
	SaveStampDecode(OccludedReportTimeW, ESaveStampMode::Zero);
	SaveStampDecode(ScheduleHost.InterruptTime, ESaveStampMode::Zero);
	SaveStampDecode(AmbientNextActivityAt, ESaveStampMode::MinusOne);
	SaveStampDecode(WeaponScareTime, ESaveStampMode::MinusOne);
	SaveStampDecode(IgnoreCollisionUntil, ESaveStampMode::FloatMax);
	SaveStampDecode(NextFidgetTime, ESaveStampMode::FloatMax);

	SaveSoundStampDecode(Senses.Memory.BestSound);
	SaveSoundStampDecode(Senses.Memory.InvestigateSound);
	SaveSoundStampDecode(Senses.Memory.LastSoundDanger);
	SaveSoundStampDecode(Senses.Memory.LastSoundPhysicsDanger);
	SaveSoundStampDecode(Senses.Memory.LastSoundCombat);
	SaveSoundStampDecode(Senses.Memory.LastSoundBulletImpact);
	SaveSoundStampDecode(Senses.Memory.LastSoundPlayer);
	SaveSoundStampDecode(Senses.Memory.LastSoundWorld);
	SaveSoundStampDecode(Senses.Memory.LastSoundFlinch);

	// `MOV EAX,[ESP+0x60]` — the base body's answer, stashed before the write pass and reloaded
	// last. The decompiled C's `return (int)local_4` is the same value under an aliased name.
	return Result;
}

int32 FElysiumNpc::Save(void* Archive)
{
	// Slot 126. The vtable, spelled as a table lookup: an override replaces this body outright, and
	// the arms that want the Troika body call back into this function under `FSpeciesDispatchScope`,
	// which is retail's non-virtual thunk.
	//
	// NOTHING IN THIS RUNTIME CALLS `Save()` YET — see the `.inl`'s archive-seam note.
	int32 Result = 0;
	if (SaveSpecies(Archive, Result))
	{
		return Result;
	}
	return TroikaSave(Archive);
}

namespace
{
	// One slot-126 or slot-127 override row and the port body that carries it, keyed on the retail
	// ADDRESS so one arm serves every class that shares a body.
	struct FSaveRestore10Arm
	{
		const TCHAR* Address = nullptr;
		const TCHAR* RetailClass = nullptr;
		int32 (FElysiumNpc::*Body)(void*) = nullptr;
	};

	const FSaveRestore10Arm GSaveArms[] =
	{
		{ TEXT("0x1034e320"), TEXT("CScriptedTarget"),        &FElysiumNpc::ScriptedTargetSave },
		{ TEXT("0x10395f80"), TEXT("CNPC_VMingXiao"),         &FElysiumNpc::MingXiaoSave },
		{ TEXT("0x1039ed50"), TEXT("CNPC_VMingXiaoTentacle"), &FElysiumNpc::MingXiaoTentacleSave },
		{ TEXT("0x103c2810"), TEXT("CNPC_VTzimisceHeadClaw"), &FElysiumNpc::TzimisceHeadClawSave },
	};

	const FSaveRestore10Arm GRestoreArms[] =
	{
		{ TEXT("0x1034e370"), TEXT("CScriptedTarget"),        &FElysiumNpc::ScriptedTargetRestore },
		{ TEXT("0x10396000"), TEXT("CNPC_VMingXiao"),         &FElysiumNpc::MingXiaoRestore },
		{ TEXT("0x1039eda0"), TEXT("CNPC_VMingXiaoTentacle"), &FElysiumNpc::MingXiaoTentacleRestore },
		{ TEXT("0x103c2860"), TEXT("CNPC_VTzimisceHeadClaw"), &FElysiumNpc::TzimisceHeadClawRestore },
		{ TEXT("0x103c5910"), TEXT("CNPC_VVampireBoss"),      &FElysiumNpc::VampireBossRestore },
	};

	bool SaveRestore10Dispatch(FElysiumNpc& Npc, int32 Slot, const FSaveRestore10Arm* Arms,
		int32 ArmCount, void* Archive, int32& OutResult)
	{
		// Retail's non-virtual thunk: while this slot's species body runs, its dispatcher answers
		// "no species body" so the arm's own chain call reaches the Troika body directly.
		if (Npc.SpeciesDispatchingSlot == Slot)
		{
			return false;
		}
		const FElysiumNpcClassSlot* Override =
			ElysiumNpcKernelClass::OverrideOf(Npc.RetailClass(), Slot);
		if (Override == nullptr)
		{
			// `RetailClass()` is null for a classname the census claims nothing for — `npc_VCop`'s
			// own recovered answer — and a class with no override row inherits the Troika body.
			return false;
		}
		for (int32 Index = 0; Index < ArmCount; ++Index)
		{
			if (FCString::Strcmp(Arms[Index].Address, Override->Address) != 0)
			{
				continue;
			}
			const FElysiumNpc::FSpeciesDispatchScope Scope(Npc, Slot);
			OutResult = (Npc.*(Arms[Index].Body))(Archive);
			return true;
		}
		// Unreachable: `Elysium.Substrate.NpcKernelSaveRestore10.ArmCoverage` asserts both tables
		// carry every override row the census holds for their slot.
		return false;
	}
}

bool FElysiumNpc::SaveSpecies(void* Archive, int32& OutResult)
{
	return SaveRestore10Dispatch(*this, GSaveSlot, GSaveArms, UE_ARRAY_COUNT(GSaveArms),
		Archive, OutResult);
}

int32 FElysiumNpc::MingXiaoSave(void* Archive)
{
	// `CNPC_VMingXiao::Save` `0x10395f80`. `pfVar2 = m_rflRegrowTimers; iVar1 = 6; do { encode(p, 4);
	// ++p; } while (--iVar1);` — ascending, mode 4, then the Troika body, then the identical
	// descending-count/ascending-pointer decode loop over the SAME six.
	//
	// Mode 4 is the fact: an exactly-`FLT_MAX` regrow timer is a tentacle that will never regrow,
	// and without the sentinel retail's `FIELD_TIME` rebase on load would shift it.
	for (int32 Index = 0; Index < MingXiaoRegrowTimerCount; ++Index)
	{
		SaveStampEncode(MingXiaoRegrowTimers[Index], ESaveStampMode::FloatMax);
	}
	const int32 Result = Save(Archive);   // the Troika body, through the dispatch scope
	for (int32 Index = 0; Index < MingXiaoRegrowTimerCount; ++Index)
	{
		SaveStampDecode(MingXiaoRegrowTimers[Index], ESaveStampMode::FloatMax);
	}
	return Result;
}

int32 FElysiumNpc::MingXiaoTentacleSave(void* Archive)
{
	// `CNPC_VMingXiaoTentacle::Save` `0x1039ed50` — one field, `m_flPhaseExpireTimer` (`+0x6674`),
	// at mode 3 around the Troika body.
	SaveStampEncode(MingXiaoTentaclePhaseExpireTimer, ESaveStampMode::Zero);
	const int32 Result = Save(Archive);
	SaveStampDecode(MingXiaoTentaclePhaseExpireTimer, ESaveStampMode::Zero);
	return Result;
}

int32 FElysiumNpc::TzimisceHeadClawSave(void* Archive)
{
	// `CNPC_VTzimisceHeadClaw::Save` `0x103c2810` — `m_flSlowedExpire` (`+0x6678`, family Species'
	// `HeadClawSlowedExpire`) at mode 3 around the Troika body. This is the same slow stamp
	// `0x103c2230` gates its teardown on.
	SaveStampEncode(HeadClawSlowedExpire, ESaveStampMode::Zero);
	const int32 Result = Save(Archive);
	SaveStampDecode(HeadClawSlowedExpire, ESaveStampMode::Zero);
	return Result;
}

int32 FElysiumNpc::ScriptedTargetSave(void* Archive)
{
	// `CScriptedTarget::Save` `0x1034e320` — `m_flPauseDoneTime` (`+0x5f64`) at mode 3 around
	// **`CAI_BaseNPC::Save`**, not the Troika body: the decompiled C's chain is
	// `CAI_BaseNPC::thunk_FUN_1027bc60`. `CScriptedTarget` is a `CAI_BaseNPC` in the census, so the
	// Troika half — the eleven stamps, the nine sounds and the pedestrian-link block — never runs
	// for one.
	SaveStampEncode(ScriptedTargetPauseDoneTime, ESaveStampMode::Zero);
	const int32 Result = BaseSave(Archive);
	SaveStampDecode(ScriptedTargetPauseDoneTime, ESaveStampMode::Zero);
	return Result;
}

// -------------------------------------------------------------------------------------------------
// Slot 127 `Restore`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::TroikaRestore(void* Archive)
{
	// `CAI_BaseNPCTroika::Restore` `0x10299700`. `CAI_BaseNPC::Restore` (`0x1027c160`) runs FIRST
	// and its answer is stashed in EBX; everything after it is bookkeeping and the answer comes back
	// unchanged at `10299851 MOV EAX,EBX`.
	const int32 Result = RestoreExtendedHeader(Archive);

	// `(**(code**)(*piVar1 + 0x44))(&local, 1, 0)` then, on a true, two `+0x3c` reads into `+0x6310`
	// and `+0x6314`. The bool is the pedestrian-link flag `TroikaSave` wrote.
	if (RestoreReadBool(Archive, TEXT("m_pPedestrianLink")))
	{
		RestoreReadInt(Archive, TEXT("m_iRestorePedLinkNode"), RestorePedLinkNode);
		RestoreReadInt(Archive, TEXT("m_iRestorePedLinkDestNode"), RestorePedLinkDestNode);
	}

	// The same eleven stamps and nine sounds `TroikaSave` encodes, decoded in the same order with
	// the same modes. Here the decompiled C is unaliased and reads them out field by field, which
	// is the independent confirmation that `0x102993c0`'s decode list is this one.
	SaveStampDecode(CanSeekCoverTimer, ESaveStampMode::Zero);
	SaveStampDecode(Senses.Memory.SeeUnknownGraceUntil, ESaveStampMode::MinusOne);
	SaveStampDecode(MeleeHeightDiffTimer, ESaveStampMode::MinusOne);
	SaveStampDecode(OccludedReportTimeE, ESaveStampMode::Zero);
	SaveStampDecode(OccludedReportTimeT, ESaveStampMode::Zero);
	SaveStampDecode(OccludedReportTimeW, ESaveStampMode::Zero);
	SaveStampDecode(ScheduleHost.InterruptTime, ESaveStampMode::Zero);
	SaveStampDecode(AmbientNextActivityAt, ESaveStampMode::MinusOne);
	SaveStampDecode(WeaponScareTime, ESaveStampMode::MinusOne);
	SaveStampDecode(IgnoreCollisionUntil, ESaveStampMode::FloatMax);
	SaveStampDecode(NextFidgetTime, ESaveStampMode::FloatMax);

	SaveSoundStampDecode(Senses.Memory.BestSound);
	SaveSoundStampDecode(Senses.Memory.InvestigateSound);
	SaveSoundStampDecode(Senses.Memory.LastSoundDanger);
	SaveSoundStampDecode(Senses.Memory.LastSoundPhysicsDanger);
	SaveSoundStampDecode(Senses.Memory.LastSoundCombat);
	SaveSoundStampDecode(Senses.Memory.LastSoundBulletImpact);
	SaveSoundStampDecode(Senses.Memory.LastSoundPlayer);
	SaveSoundStampDecode(Senses.Memory.LastSoundWorld);
	SaveSoundStampDecode(Senses.Memory.LastSoundFlinch);
	return Result;
}

int32 FElysiumNpc::Restore(void* Archive)
{
	// Slot 127, the same prologue shape as slot 126.
	int32 Result = 0;
	if (RestoreSpecies(Archive, Result))
	{
		return Result;
	}
	return TroikaRestore(Archive);
}

bool FElysiumNpc::RestoreSpecies(void* Archive, int32& OutResult)
{
	return SaveRestore10Dispatch(*this, GRestoreSlot, GRestoreArms, UE_ARRAY_COUNT(GRestoreArms),
		Archive, OutResult);
}

int32 FElysiumNpc::MingXiaoRestore(void* Archive)
{
	// `CNPC_VMingXiao::vfunc127` `0x10396000` — the base FIRST, then the six regrow timers decoded
	// ascending at mode 4. The decode twin of `0x10395f80`.
	const int32 Result = Restore(Archive);
	for (int32 Index = 0; Index < MingXiaoRegrowTimerCount; ++Index)
	{
		SaveStampDecode(MingXiaoRegrowTimers[Index], ESaveStampMode::FloatMax);
	}
	return Result;
}

int32 FElysiumNpc::MingXiaoTentacleRestore(void* Archive)
{
	// `CNPC_VMingXiaoTentacle::vfunc127` `0x1039eda0`.
	const int32 Result = Restore(Archive);
	SaveStampDecode(MingXiaoTentaclePhaseExpireTimer, ESaveStampMode::Zero);
	return Result;
}

int32 FElysiumNpc::TzimisceHeadClawRestore(void* Archive)
{
	// `CNPC_VTzimisceHeadClaw::vfunc127` `0x103c2860`.
	const int32 Result = Restore(Archive);
	SaveStampDecode(HeadClawSlowedExpire, ESaveStampMode::Zero);
	return Result;
}

int32 FElysiumNpc::VampireBossRestore(void* Archive)
{
	// `CNPC_VVampireBoss::Restore` `0x103c5910`, inside a scope-trace frame (`"CNPC_VVampireBoss::
	// Restore"` at `0x1065ec00`) this runtime does not stand. The three writes are in the listing's
	// order: `103c5972` the model name, `103c597c` `ClearBodyEmitterNames`, `103c5981` the
	// classname literal.
	//
	// It is a post-load reset, not a restore: a boss that was saved mid-transformation comes back
	// wearing its default model name and its default classname, whatever the archive held.
	const int32 Result = Restore(Archive);
	VampireBossMonsterModelName.Reset();   // m_pMonsterModelName +0x6680 := 0
	ClearBodyEmitterNames();               // 0x103c6eb0, family Damage's
	VampireBossMonsterClassname = GVampireBossDefaultClassname;   // +0x6694
	return Result;
}

int32 FElysiumNpc::ScriptedTargetRestore(void* Archive)
{
	// `CScriptedTarget::Restore` `0x1034e370` — `CAI_BaseNPC::Restore` and ITS answer, then one
	// mode-3 decode on `m_flPauseDoneTime`. Nothing else is read or written.
	const int32 Result = RestoreExtendedHeader(Archive);
	SaveStampDecode(ScriptedTargetPauseDoneTime, ESaveStampMode::Zero);
	return Result;
}

// -------------------------------------------------------------------------------------------------
// Slot 180 `UpdateOnRemove`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::LeaveInterestingPlaceOnRemove()
{
	// `thunk_FUN_102b53d0(this, 0, "Leaving interesting place (UpdateOnRemove)")`. See the `.inl`
	// for what the retail body does and why this is a call site rather than a second copy of it:
	// `FinishAmbientUse` already IS this runtime's interesting-place release, and `bAmbientArrived`
	// is `m_bInterestingPlaceArrived`, the flag retail hands `0x102da600`.
	//
	// `bStopMovement=false` because retail's release does not stop the motor — this is a removal,
	// and the body is about to stop existing.
	if (CurrentAmbientSpot() != nullptr)
	{
		FinishAmbientUse(bAmbientArrived, /*bStopMovement=*/false);
	}
	// `*(undefined1 *)(param_1 + 0x18ba) = 0;` is OUTSIDE the `if`: retail clears
	// `m_bInterestingPlaceArrived` whether or not a place was held.
	bAmbientArrived = false;
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s %s"), *DebugString(), GLeaveInterestingPlaceReason);
}

void FElysiumNpc::StopDialogOnRemove()
{
	// `thunk_FUN_102c0bb0(this)`. `CAI_BaseNPCTroika::FinishTalking` is this runtime's talk-end
	// stamp (`ElysiumNpc.h`: "The stamp form is the same fact without a separate `FinishTalking`
	// sweep"), so ending the window IS that call.
	TalkingUntil = -1.0;
	// SEAM, named: the sound-channel-5 stop, slot 275, `FadeoutExpressions` and the schedule `0xf1`
	// install. Schedule `0xf1` has no registered id in this runtime and nothing here stands a
	// per-channel sound stop at kernel level, so the install is COUNTED and the rest answers
	// nothing. The gate retail puts on the install — slot 613 allows it AND the running schedule is
	// not already `0xf1` — is reproduced as "a program is not already the dialogue-stop one", which
	// with no registered id is always true.
	++DialogStopScheduleRequests;
}

void FElysiumNpc::TroikaUpdateOnRemove()
{
	// `CAI_BaseNPCTroika::UpdateOnRemove` `0x1028d6e0`, read off the listing. Six steps, in order.

	// 1. `ClearHintNode(this, 5.0)` — the 5.0-second reuse delay, against the base body's 0.0.
	ClearScheduleHint(GTroikaRemoveHintReuseSeconds);

	// 2. `if (m_pAttackCoordinator +0x65e8) (**(this + 0x964))(this)` — slot 601, the melee
	//    coordinator release, dispatched with THIS entity as its argument.
	if (AttackCoordinator != 0)
	{
		Slot601(this);
	}

	// 3. The interesting-place release.
	LeaveInterestingPlaceOnRemove();

	// 4. `if (IsInDialog()) StopDialog()`.
	if (SaveRestore10IsInDialog(*this))
	{
		StopDialogOnRemove();
	}

	// 5. Both patrol arrays, `m_sppPatrolPath` (`+0x658c`) FIRST and `m_sppPatrolPathHunt`
	//    (`+0x6594`) second — `0x1029f5d0` clears the count byte and frees the block.
	PatrolPoints.Reset();
	HuntPatrolPoints.Reset();

	// 6. `JMP thunk CAI_BaseNPC::UpdateOnRemove` — story 29c-1's `BaseNpcUpdateOnRemove`.
	BaseNpcUpdateOnRemove();
}

void FElysiumNpc::UpdateOnRemove()
{
	// Slot 180. The species prologue, then the Troika body.
	if (UpdateOnRemoveSpecies())
	{
		return;
	}
	TroikaUpdateOnRemove();
}

namespace
{
	struct FUpdateOnRemoveArm
	{
		const TCHAR* Address = nullptr;
		const TCHAR* RetailClass = nullptr;
		void (FElysiumNpc::*Body)() = nullptr;
	};

	const FUpdateOnRemoveArm GUpdateOnRemoveArms[] =
	{
		{ TEXT("0x10371a90"), TEXT("CNPC_VCop"),         &FElysiumNpc::CopUpdateOnRemove },
		{ TEXT("0x10391230"), TEXT("CNPC_VMingXiao"),    &FElysiumNpc::MingXiaoUpdateOnRemove },
		{ TEXT("0x103a03a0"), TEXT("CNPC_VNewscaster"),  &FElysiumNpc::NewscasterUpdateOnRemove },
	};
}

bool FElysiumNpc::UpdateOnRemoveSpecies()
{
	if (SpeciesDispatchingSlot == GUpdateOnRemoveSlot)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GUpdateOnRemoveSlot);
	if (Override == nullptr)
	{
		return false;
	}
	for (const FUpdateOnRemoveArm& Arm : GUpdateOnRemoveArms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		const FSpeciesDispatchScope Scope(*this, GUpdateOnRemoveSlot);
		(this->*Arm.Body)();
		return true;
	}
	return false;
}

int32& FElysiumNpc::CopAliveCensus()
{
	return GCopAliveCensus;
}

int32& FElysiumNpc::CopSecondCensus()
{
	return GCopSecondCensus;
}

void FElysiumNpc::CopUpdateOnRemove()
{
	// `CNPC_VCop::UpdateOnRemove` `0x10371a90`, instruction for instruction off the listing — the
	// decompiled C mis-renders the two census bytes as `this+1`.
	//
	//     10371a90  MOV DL, [ECX + 0x6671]        ; m_bCountedAlive
	//     10371a96  XOR AL, AL
	//     10371a9a  JZ  ...  / DEC [0x1093acac]
	//     10371aa2  MOV DL, [ECX + 0x6672]        ; READ before the first byte is cleared
	//     10371aa8  MOV [ECX + 0x6671], AL
	//     10371ab0  JZ  ...  / DEC [0x1093acb0]
	//     10371ab8  MOV [ECX + 0x6672], AL
	//     10371abe  JMP CAI_BaseNPCTroika::UpdateOnRemove
	const bool bWasCountedAlive = bCopCountedAlive;
	if (bWasCountedAlive)
	{
		--CopAliveCensus();
	}
	const bool bWasCountedSecond = bCopCountedSecond;
	bCopCountedAlive = false;
	if (bWasCountedSecond)
	{
		--CopSecondCensus();
	}
	bCopCountedSecond = false;
	// The tail jump is a CALL to the Troika body, not a re-dispatch: `Update` below runs it through
	// the dispatch scope this arm is already inside.
	TroikaUpdateOnRemove();
}

void FElysiumNpc::MingXiaoUpdateOnRemove()
{
	// `CNPC_VMingXiao::vfunc180` `0x10391230` — `if (m_eThrowableObjectMode +0x673c)
	// thunk_FUN_10398fd0(this);` then the Troika body ALWAYS. Without the drop the thrown prop
	// outlives the boss.
	if (MingXiaoThrowableObjectMode != 0)
	{
		MingXiaoThrowCleanup();   // 0x10398fd0, family Damage's
	}
	TroikaUpdateOnRemove();
}

void FElysiumNpc::NewscasterUpdateOnRemove()
{
	// `CNPC_VNewscaster::vfunc180` `0x103a03a0` — `thunk_FUN_103a0d50(this)` then the Troika body.
	// `0x103a0d50` is family Species' `FUN_103a0d50`: the two story queues torn down row by row and
	// the story-active byte cleared.
	FUN_103a0d50();
	TroikaUpdateOnRemove();
}

// -------------------------------------------------------------------------------------------------
// Slot 106 `PostConstructor`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::PostConstructor(TCHAR* Name)
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

// -------------------------------------------------------------------------------------------------
// `FUN_10290350` — `RunAlternateAI` mode 4, the door-blocked transaction.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::OpeningDoorToggleState() const
{
	// SEAM for `m_toggle_state` (`CBaseDoor +0x4f8`). This runtime's doors are `FElysiumMover` and
	// carry their own phase; `0` is the value step 2 requires and is a door at rest, which is what a
	// mover the NPC is still waiting on reads as. Named so the day the mover's phase is mapped onto
	// Source's four-state toggle the read moves with it.
	return GDoorToggleStateClosed;
}

void FElysiumNpc::MaintainActivity()
{
	// SEAM for `CAI_BaseNPC::MaintainActivity` `0x102727d0`. See the `.inl`: nothing in this runtime
	// publishes an ideal activity from an NPC-side maintain pass, so this answers nothing and
	// records the one fact the call site makes observable.
	bForceMaintainActivitySeenByLastMaintain = bForceMaintainActivity;
	++MaintainActivityCalls;
}

bool FElysiumNpc::AlternateAiDoorSweepHit(const FVector& StartCm, const FVector& EndCm)
{
	// SEAM for `thunk_FUN_102e6d70(filter +0x5d40, 0, start, end, 0x202400b, 0, 100.0, 0, &trace,
	// 0, 0)`. Answers FALSE — "the way ahead is clear" — which is retail's own no-hit arm and leaves
	// the expiry arm as the one that ends the transaction.
	(void)StartCm;
	(void)EndCm;
	++AlternateAiDoorSweeps;
	return false;
}

bool FElysiumNpc::RunAlternateAiDoorMode4(double Now)
{
	// `FUN_10290350` `0x10290350`. The offsets below are the listing's: `param_1[0x1749]` is
	// `+0x5d24 m_hOpeningDoor`, `param_1 + 0x174c` (a BYTE write on an `int*`) is `+0x5d30
	// m_bOpeningDoorWait`, `param_1[0x1913]` is `+0x644c m_eAlternateAI`, `param_1[0x1914]` is
	// `+0x6450 m_flAlternateAIExpireTimer`, and `param_1[0x18a4..0x18a6]` is `+0x6290 m_vecForward`.

	// 1. `m_bForceMaintainActivity := 1` across `CAI_BaseNPC::MaintainActivity`, then `:= 0`. The
	//    latch spans exactly that one call and nothing else.
	bForceMaintainActivity = true;
	MaintainActivity();
	bForceMaintainActivity = false;

	// 2. A still-resolving `m_hOpeningDoor` whose `m_toggle_state` is 0 ends the transaction: stop
	//    the motor and clear the mode. The door handle and the wait flag are deliberately NOT
	//    cleared here — only the trace arm and the expiry arm clear them, and that asymmetry is
	//    retail's.
	if (World != nullptr)
	{
		if (const FElysiumEntity* Door = World->Resolve(OpeningDoor))
		{
			(void)Door;
			if (OpeningDoorToggleState() == GDoorToggleStateClosed)
			{
				ResumeScheduledMove();   // 0x102bf7e0
				AlternateAi = 0;
			}
		}
	}

	// 3. The forward hull sweep. `fStack_3c = m_vecForward.z * _DAT_10451acc` and the same scale on
	//    x and y, so the probe end is `origin + forward * 64.0` in SOURCE units.
	const FVector Start = Origin;
	const FVector End = Start + Forward * (GAlternateAiDoorProbeUnits * ElysiumMove::U);
	if (AlternateAiDoorSweepHit(Start, End))
	{
		ResumeScheduledMove();
		OpeningDoor = FElysiumEntityHandle();
		bOpeningDoorWait = false;
		AlternateAi = 0;
	}

	// 4. `uVar1 = (uint)(curtime < m_flAlternateAIExpireTimer); if (uVar1 == 0) { TaskFail(0xe); … }`
	//    — the ELSE arm fires, so an expiry stamp EQUAL to curtime has expired.
	if (!(Now < AlternateAiExpireTime))
	{
		TaskFail(GAlternateAiDoorTaskFailReason);   // slot 448, reason 0xe
		OpeningDoor = FElysiumEntityHandle();
		bOpeningDoorWait = false;
		AlternateAi = 0;
	}

	// `return CONCAT31(..., 1);` — always true, so the transaction keeps the body.
	return true;
}
