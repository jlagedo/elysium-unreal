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
// both listing reads. `ElysiumNpcSaveRestore10.inl` carries the family's reading notes; the
// walked prose is `docs/vtmb/npc-ai/lifecycle.md`.

namespace
{
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

	// `CBaseCombatCharacter::Restore`'s answer, on the same footing.
	constexpr int32 GChainRestoreResult = 1;

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

}

// -------------------------------------------------------------------------------------------------
// The sentinel codec — `0x101cf250` and `0x101cf2f0`.
// -------------------------------------------------------------------------------------------------

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

// -------------------------------------------------------------------------------------------------
// The archive seam.
// -------------------------------------------------------------------------------------------------

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

// -------------------------------------------------------------------------------------------------
// Slot 126 `Save`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::TroikaSave(void* Archive)
{
	// `CAI_BaseNPCTroika::Save` `0x102993c0`. The eleven stamps, in the listing's order, with the
	// retail field and offset each port member stands for named on its own line. The DECODE pass
	// walks the identical list: the decompiled C renders its pointers one slot out through EBX and
	// stack aliasing, but the modes pushed at `1029956f`..`102995e9` read `3,2,2,3,3,3,3,2,2,4,4`,
	// which is this list.
	//
	// Ten of the eleven are `double` on this runtime's leaf and one — `m_flEyeFidgetTime`
	// (`+0x657c`) — lands on the entity chain as `FElysiumCombatCharacter::NextFidgetTime`, a
	// `float`, which is why the codec carries both widths.
	SaveStampEncode(CanSeekCoverTimer, ESaveStampMode::Zero);                    // +0x607c
	SaveStampEncode(Senses.Memory.SeeUnknownGraceUntil, ESaveStampMode::MinusOne);  // +0x6084
	SaveStampEncode(MeleeHeightDiffTimer, ESaveStampMode::MinusOne);            // +0x6274
	SaveStampEncode(OccludedReportTimeE, ESaveStampMode::Zero);                 // +0x62cc
	SaveStampEncode(OccludedReportTimeT, ESaveStampMode::Zero);                 // +0x62d0
	SaveStampEncode(OccludedReportTimeW, ESaveStampMode::Zero);                 // +0x62d4
	SaveStampEncode(ScheduleHost.InterruptTime, ESaveStampMode::Zero);          // +0x632c
	// `m_flNextInterestChangeTime`. `ElysiumNpcKernelShapeMap.cpp` binds `+0x63d4` to
	// `FElysiumNpc::AmbientNextActivityAt`, "the interesting-place activity clock".
	SaveStampEncode(AmbientNextActivityAt, ESaveStampMode::MinusOne);           // +0x63d4
	SaveStampEncode(WeaponScareTime, ESaveStampMode::MinusOne);                 // +0x63dc
	SaveStampEncode(IgnoreCollisionUntil, ESaveStampMode::FloatMax);            // +0x6458
	SaveStampEncode(NextFidgetTime, ESaveStampMode::FloatMax);                  // +0x657c

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
	const int32 Result = FElysiumNpcBase::Save(Archive);

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
	// Slot 126, the Troika body. A species class's override replaces it outright (story 5 step 3),
	// and the bodies that want the Troika body call `TroikaSave` directly — retail's non-virtual
	// thunk.
	//
	// NOTHING IN THIS RUNTIME CALLS `Save()` YET — see the `.inl`'s archive-seam note.
	return TroikaSave(Archive);
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
	return TroikaRestore(Archive);
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
	++InterestingPlaceReleases;
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s %s"), *DebugString(), GLeaveInterestingPlaceReason);
}

void FElysiumNpc::StopDialogOnRemove()
{
	// `thunk_FUN_102c0bb0(this)`. Story 29d, family Social10 landed
	// `CAI_BaseNPCTroika::FinishTalking` (`0x102c0ca0`) itself, so this is now the CALL retail makes
	// rather than the talk-end stamp that stood in for it. The observable difference is retail's
	// own: `FinishTalking` stamps `m_flTalkEnd` with `curtime` instead of clearing it, releases the
	// dialog partner, clears `m_szDialogQue` and picks a dialogue-singleton notify from the latched
	// `m_bIsTalking`. Named minimal fix, reported by family Social10.
	FinishTalking();
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
	// Slot 180. `CNPC_VCop`, `CNPC_VMingXiao` and `CNPC_VNewscaster` override it on their C++ classes
	// (story 5 step 3); `CCineNPC`'s `0x101a7140` is `FElysiumScriptedSequence`'s.
	TroikaUpdateOnRemove();
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
	// The bookkeeping this family stood the seam for: the one fact the call site makes observable is
	// that `m_bForceMaintainActivity` was up across exactly one call and down on either side of it.
	bForceMaintainActivitySeenByLastMaintain = bForceMaintainActivity;
	++MaintainActivityCalls;
	// Story 29d, family **Anim10** landed `CAI_BaseNPC::MaintainActivity` (`0x102727d0`) itself —
	// the slot-466 gate, the two-term mismatch test and the `ACT_TRANSITION` arm — as
	// `BaseMaintainActivity` (`ElysiumNpcAnim10.cpp`). This seam is now its entry point rather
	// than its replacement; the body runs AFTER the bookkeeping, which is the order retail's caller
	// sees (the latch is raised, the body runs, the latch is cleared).
	BaseMaintainActivity();
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
