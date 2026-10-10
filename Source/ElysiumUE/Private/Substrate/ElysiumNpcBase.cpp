#include "Substrate/ElysiumNpcBase.h"

#include "Debug/ElysiumNpcDebugLogging.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumPhysicsData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumSoundLevel.h"
#include "ElysiumStub.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"
#include "HAL/IConsoleManager.h"
#include "Substrate/ElysiumScriptedScheduleOrder.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumFootsteps.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumAnimationPick.h"
#include "Visual/ElysiumNpcClips.h"

#include <cmath>

FElysiumNpcBase::FElysiumNpcBase() { EnemyMemory.BindOwner(*this); } // 0x102df320 +0

// --- `m_hCine` and the ideal-state writers a director uses (story 5 fold A3) ------------------

FElysiumScriptedSequence* FElysiumNpcBase::ResolveCine() const
{
	FElysiumEntity* Owner = World != nullptr && ScriptOwner.IsSet() ? World->Resolve(ScriptOwner) : nullptr;
	FElysiumNpcBase* AsBase = Owner != nullptr ? Owner->AsNpcBase() : nullptr;
	return AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
}

void FElysiumNpcBase::RequestIdealStateRetail(int32 RetailId, int32 SourceLine)
{
	Mind.RequestDesiredState(RetailId, SourceLine);
}

// --- Moved from `ElysiumNpc.cpp` (story 5 step 5) ---

const FElysiumEntity* FElysiumNpcBase::FindPatrolPoint(const FString& Name) const
{
	// `0x102d2840`, which `0x102d2900` wraps for `InputFollowPatrolPath` (`0x1029ed90`): walk the
	// global hint list from its head, admit a hint of type 10000 or 800 only, and compare its
	// `Group` (`+0x5f0`) to the token with a byte-for-byte, CASE-SENSITIVE compare. First match wins.
	// No targetname path, and no disabled/owner/cooldown or group-mask test (`python_bridge.md`).
	//
	// This is `0x102d2840` alone and answers the hint; its wrapper `0x102d2900`
	// (`FElysiumNpc::PatrolNodeIdFor`) turns it into the hint's network node (`+0x5e4`, -1 on a miss
	// or a standalone hint), and the patrol readers walk to that node's position
	// (`PatrolNodePosition`), not to the hint's origin.
	if (World == nullptr)
	{
		return nullptr;
	}
	for (const int32 Index : World->HintList())
	{
		const FElysiumHint* Hint = World->Entities().IsValidIndex(Index)
			? FElysiumHint::Cast(World->Entities()[Index].Get()) : nullptr;
		if (Hint == nullptr || (Hint->HintType != 10000 && Hint->HintType != 800))
		{
			continue;
		}
		if (Hint->Group.Equals(Name, ESearchCase::CaseSensitive))
		{
			return Hint;
		}
	}
	return nullptr;
}

bool FElysiumNpcBase::HasClientRagdollRig() const
{
	// 0x10090180 *0x1070b250 slot 18: source model capability, never the generated PhysicsAsset.
	const UElysiumCharacterProvenance* const RagdollSource = Visual != nullptr
		? UElysiumCharacterProvenance::Find(Visual->GetSkeletalMeshAsset()) : nullptr;
	return RagdollSource != nullptr && RagdollSource->PhysicsSourceData != nullptr
		&& RagdollSource->PhysicsSourceData->Data.bHasPhysics;
}

int32 FElysiumNpcBase::CorpseHitboxBone(const void* InInfo) const
{
	// 0x101c2a30 CTakeDamageInfo input: no hitbox/bone field in this packet. Producer is 0014.
	(void)InInfo;
	return INDEX_NONE;
}

int32 FElysiumNpcBase::CorpseForceBone(const void* InInfo) const
{
	const int32 HitBone = CorpseHitboxBone(InInfo); // 0x1032c1e4
	if (HitBone != INDEX_NONE) { return HitBone; }
	if (Visual == nullptr) { return INDEX_NONE; }
	// 0x1032c226 LookupBone("Bip01 Spine2"): use the actual drawn mesh's native bone ordinal.
	FName NativeSpine(TEXT("Bip01 Spine2"));
	const UElysiumCharacterProvenance* const RagdollSource =
		UElysiumCharacterProvenance::Find(Visual->GetSkeletalMeshAsset());
	if (RagdollSource != nullptr && RagdollSource->PhysicsSourceData != nullptr)
	{
		for (const FElysiumPhysicsSourceBone& SourceBone : RagdollSource->PhysicsSourceData->Data.Bones)
		{
			if (SourceBone.SourceName.Equals(TEXT("Bip01 Spine2"), ESearchCase::IgnoreCase))
			{
				NativeSpine = SourceBone.NativeName;
				break;
			}
		}
	}
	const int32 SpineBone = Visual->GetBoneIndex(NativeSpine);
	if (SpineBone == INDEX_NONE && HasClientRagdollRig())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s source ragdoll rig lacks drawn Spine2 '%s': data/attachment failure (0x1032c226)"),
			*DebugString(), *NativeSpine.ToString());
	}
	return SpineBone;
}

void FElysiumNpcBase::BecomeClientRagdoll()
{
	(void)BecomeClientRagdoll(FVector::ZeroVector, INDEX_NONE, false); // 0x1028a8f7 explicit seed
}

bool FElysiumNpcBase::BecomeClientRagdoll(const FVector& Force, int32 Bone, bool bRetainEntity)
{
	if (!HasClientRagdollRig()) // 0x10090180 model-interface slot18
	{
		LastSetSizeMinsUnits = FVector::ZeroVector; LastSetSizeMaxsUnits = FVector::ZeroVector; // 0x10090180
		if (Motor) Motor->SetHullSize(FVector::ZeroVector, FVector::ZeroVector); // 0x101cf390
		++SetSizeCalls; // 0x10090180 UTIL_SetSize, no-rig arm
		return false; // 0x10090180: no other writes, no hold-pose substitute
	}
	FElysiumNpc* const TroikaRagdoll = AsNpc();
	int32 SeedSequence = INDEX_NONE;
	if (TroikaRagdoll != nullptr)
	{
		SeedSequence = TroikaRagdoll->SelectWeightedSequence(0x21); // 0x1009021a, V4c bridge/shared draw
	}
	else
	{
		// 0x1008dc40 bare model table on the base line too; no Troika activity translation or probe.
		TArray<FElysiumNpcClip> BaseSeedClips;
		IElysiumEmbodiment* const BaseSeedSource = World != nullptr ? World->Embodiment() : nullptr;
		if (BaseSeedSource != nullptr && Visual != nullptr)
		{
			FElysiumActivityClipRequest BaseSeedRequest;
			FillActivityClipRequest(BaseSeedRequest);
			BaseSeedRequest.Activity = TEXT("ACT_DIERAGDOLL");
			BaseSeedRequest.BodyKind = EElysiumAnimBodyKind::Cast;
			BaseSeedRequest.bAllowFallbackLadder = false;
			BaseSeedSource->NpcActivitySequences(BaseSeedRequest, BaseSeedClips);
		}
		TArray<ElysiumAnimationPick::FCandidate> BaseSeedCandidates;
		for (const FElysiumNpcClip& BaseSeedClip : BaseSeedClips)
		{
			if (BaseSeedClip.RawIndex != INDEX_NONE)
			{
				BaseSeedCandidates.Add({BaseSeedClip.RawIndex, BaseSeedClip.Weight}); // base has no bridge numbering
			}
		}
		SeedSequence = ElysiumAnimationPick::Weighted(BaseSeedCandidates); // 0x10427fc0 same shared stream
		BaseClientRagdollSeed = SeedSequence; // source row retained for ResetSequenceInfo's base play hook
	}
	if (Bone == INDEX_NONE && SeedSequence != INDEX_NONE) // 0x1009021a: only bone -1 commits
	{
		SequenceNumber = SeedSequence; SequenceCycle = 0.f; // 0x1009021a
		ResetSequenceInfo(); // 0x10090950
	}
	// 0x10090180 slot225: physics destruction input absent, not fabricated.
	if (!bRetainEntity) { SetSolidFlags(static_cast<uint16>(GetSolidFlags() | 4)); } // 0x10090180 rig branch only: `|4` through FUN_100dc580 (0x100015dc)
	CompleteDeathHandoff(); // 0x10090180 TriggerClientRagdoll visual seam, once per drawn body
	(void)Force; // 0x1008b800 force/bone latch and impulse producer remain 0014
	if (TroikaRagdoll != nullptr) { TroikaRagdoll->RenderFxWord = 0x17; } // 0x10090180
	else { BaseRagdollRenderFxWord = 0x17; } // same +0x168 word on the base-only line
	if (!bRetainEntity) // 0x10090180
	{
		SetMoveType(0, 0); // 0x10090180 slot93
		LastSetSizeMinsUnits = FVector::ZeroVector; LastSetSizeMaxsUnits = FVector::ZeroVector; // 0x10090180
		if (Motor) Motor->SetHullSize(FVector::ZeroVector, FVector::ZeroVector); // 0x101cf390
		++SetSizeCalls; // 0x10090180 UTIL_SetSize
		ThinkSet(nullptr, 0.0); // 0x10090180
	}
	return true; // 0x10090180
}

bool FElysiumNpcBase::PlayBaseClientRagdollSeed(int32 Sequence, float& OutSeconds, bool& bOutLoops)
{
	// 0x10090950: base-only explicit -1 seed, resolved by the actual model's raw sequence identity.
	IElysiumEmbodiment* const SeedBody = World != nullptr ? World->Embodiment() : nullptr;
	if (Sequence == INDEX_NONE || Sequence != BaseClientRagdollSeed || SeedBody == nullptr || Visual == nullptr)
	{
		return false;
	}
	FString SeedLabel;
	FElysiumNpcClip NativeSeed;
	if (!SeedBody->GetBodyClipByRawIndex(Visual, ModelStem(), Sequence, SeedLabel, NativeSeed)
		|| SeedLabel.IsEmpty()) { return false; } // absent descriptor is a named model-input seam
	bOutLoops = NativeSeed.IsLooping(); // 0x10090a14 studio loop bit
	FElysiumClipSegment SeedSegment;
	SeedSegment.ClipName = SeedLabel;
	SeedSegment.OwnerStem = NativeSeed.Owner;
	SeedSegment.AnimationName = SeedLabel;
	SeedSegment.bLoop = bOutLoops;
	return PlayAnimSegment(SeedSegment, &OutSeconds); // 0x10090950 same clip funnel, no second pick
}

void FElysiumNpcBase::CompleteDeathHandoff()
{
	if (!HasClientRagdollRig() || Visual == nullptr
		|| (bDeathHandoffDone && DeathHandoffVisual == Visual)) { return; } // 0x10090180
	IElysiumEmbodiment* const DeathEmbodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (DeathEmbodiment == nullptr) { return; }
	bDeathHandoffDone = true;
	DeathHandoffVisual = Visual;
	// 0x10090180: ordinary real-bone deaths hand the CURRENT pose over on the kill tick.
	if (DeathEmbodiment->StartBodyRagdoll(Visual))
	{
		DeathEmbodiment->ReleaseBodyAnimClaims(Visual);
		Mind.RecordExternal(TEXT("death: current body pose admitted to physics"));
	}
	else
	{
		// Source capability stays true. The solver asset/attachment failed; no HoldBodyFinalPose.
		Mind.RecordExternal(TEXT("death: source rig present, physics bake/handoff failed"));
	}
}

uint8 FElysiumNpcBase::NpcStateFlags() const
{
	// This runtime's state vocabulary onto retail's `m_NPCState` ids (`0x1026e3e0`'s cases): idle
	// 1, combat 2, alert 3, script 4, dead 7. Prone is Source's `NPC_STATE_PRONE` 6. The flee (8)
	// and hunt (0xb) states are 21a's and 10h's; until they exist no body can carry `0x85` or
	// `0x7f`.
	int32 RetailState = 0;
	switch (Mind.State())
	{
	case EElysiumNpcState::Idle:     RetailState = 1; break;
	case EElysiumNpcState::Combat:   RetailState = 2; break;
	case EElysiumNpcState::Alert:    RetailState = 3; break;
	case EElysiumNpcState::Scripted: RetailState = 4; break;
	case EElysiumNpcState::Prone:    RetailState = 6; break;
	case EElysiumNpcState::Dead:     RetailState = 7; break;
	}
	return FElysiumNpcFlags::NpcStateFlagsForRetailState(RetailState);
}


float FElysiumNpcBase::RandomSeconds(float Max)
{
	// `RandomFloat(0.1, arg)` (`0x10283dae`). `FRandRange` is the same `low + (high - low) * frac`,
	// so an operand below the floor draws between it and 0.1, as retail's does.
	return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(0.1f, Max);
}

void FElysiumNpcBase::ClearSchedule()
{
	// `CAI_BaseNPC::ClearSchedule` (`0x10280d30`), reached by every site that drops a running
	// program. The six schedule words, `PRESERVE_PATH` and the slot-435 dispatch are
	// `ElysiumSchedule::ClearSchedule`'s; what this adds is the name, so no site clears the
	// schedule record directly and skips the release the dispatch performs.
	ElysiumSchedule::ClearSchedule(Schedule, *this);
}

void FElysiumNpcBase::DisconnectFromSquad()
{
	// 0x1026d050: the refcount is real even while no named squad exists. R17 supplies
	// the shared/global enemy-memory redirection; this host must not invent a local squad.
	// `0x1026d05b`: only a count <= 0 leaves the squad (`0x1026d068` through `+0x5da4`, no port
	// squad) and formats the `ClearMemory` reason DevMsg (`0x1026d08b`, debug text, absent); then
	// `++m_iSquadDisconnected` (`0x1026d097`). Retail writes NO flag here: `D_DISCONNECT_SQUAD`
	// (`m_bfAINPCFlags2` bit 23) is set by the callers that want it (the task at `0x102a536e`). The
	// port's `NpcFlags.Set(D_DISCONNECT_SQUAD)` is deleted (story 8 L11 integration): it made every
	// grapple (`0x1026cdc0`) and feed-begin (`0x1026cec0`) carry the bit.
	++BaseScheduleHost.SquadDisconnected;
}

void FElysiumNpcBase::ReconnectToSquad()
{
	// 0x1026d0c0 (reached through the jump thunk 0x10009601), read whole in story 29c-1:
	//
	//   if (--m_iSquadDisconnected < 1) {
	//       if (m_pSquad) AddSelfToSquadMemory(m_pSquad, this);   // 0x10316720
	//       m_iSquadDisconnected = 0;
	//   }
	//   m_bfAINPCFlags2 &= 0x7f7fffff;
	//
	// The clamp below is retail's `< 1 -> 0` arm; the mask clears D_DISCONNECT_SQUAD (0x00800000)
	// and bit 31, which `docs/vtmb/npc-ai/schedule-kernel.md` records as the flag-name resolver's
	// word-two routing marker and not a flag, so the one named bit is the whole observable clear.
	const bool bReachedZero = BaseScheduleHost.SquadDisconnected - 1 < 1;
	BaseScheduleHost.SquadDisconnected = FMath::Max(0, BaseScheduleHost.SquadDisconnected - 1);
	if (bReachedZero)
	{
		// The squad seam (`ElysiumNpcSquad.cpp`): no `CAI_Squad`, so nothing to rejoin.
		AddSelfToSquadMemory(const_cast<void*>(ConnectedSquad()));
	}
	NpcFlags.Clear(EElysiumNpcFlag2::D_DISCONNECT_SQUAD);
}

void FElysiumNpcBase::AddOblivious()
{
	NpcFlags.Set(EElysiumNpcFlag2::MADE_OBLIVIOUS);
	++ObliviousCount;
}

void FElysiumNpcBase::RemoveOblivious()
{
	// Retail clamps at zero (`0x1026d160`) rather than trusting the pairing, and so does this: the
	// binary itself has a path that drops the bookkeeping bit without decrementing, so the counter is
	// not provably balanced even in retail.
	ObliviousCount = FMath::Max(0, ObliviousCount - 1);
	NpcFlags.Clear(EElysiumNpcFlag2::MADE_OBLIVIOUS);
}

FString FElysiumNpcBase::DescribeNpcFlags() const
{
	const FString Words = NpcFlags.Describe();
	if (ObliviousCount <= 0)
	{
		return Words;
	}
	const FString Count = FString::Printf(TEXT("oblivious=%d"), ObliviousCount);
	return Words == TEXT("-") ? Count : Words + TEXT("|") + Count;
}

void FElysiumNpcBase::ArmThinkAt(double Stamp)
{
	// `m_flNextThink := Stamp`. `NextThink` is a float and the world clock a double, so
	// `float(Stamp)` rounds ABOVE `Stamp` for most values -- and `RunThinks`' `NextThink > Now` test
	// would then skip the very frame the stamp names, which is visible for any armed delay
	// (`NPCInit`'s `curtime + 0.1` most of all). One ULP down where it does. (Retail has no such
	// problem: `m_flNextThink` and `gpGlobals->curtime` are both floats there.)
	float Armed = static_cast<float>(Stamp);
	if (static_cast<double>(Armed) > Stamp)
	{
		Armed = std::nextafterf(Armed, -FLT_MAX);
	}
	NextThink = Armed;
}

// `AIExtendedSaveHeader_t` -- the one block retail's own `Save` writes by hand (`0x1027bc60`), and
// the only thing `FElysiumNpcBase::OnRestore` `0x1027bf50` needs in order to re-find the program the save was
// taken during: its NAME, the checksum of its task array, and the three-bit liveness word.
void FElysiumNpcBase::SerializeExtendedHeader(FElysiumSaveArchive& Ar)
{
	FAiExtendedSaveHeader Header = Ar.IsLoading()
		? FAiExtendedSaveHeader() : BuildExtendedSaveHeader();
	Ar << Header.Version;
	Ar << Header.Flags;
	Ar << Header.ScheduleName;
	Ar << Header.ScheduleCrc;
	if (Ar.IsLoading())
	{
		LastSavedExtendedHeader = Header;
	}
}

void FElysiumNpcBase::TaskFail(int32 Reason)
{
	// `CAI_BaseNPC::TaskFail` `0x10273fc0`, slot 448's base body; the Troika override `0x1029adb0`
	// ends in a direct call to it. Its head is a debug arm under `developer != 0` (`DAT_1070af4c`,
	// `0x10273fc3`..`0x10273fe3`): the overlay record `+0x5f30` (the failure text, `0x10316fa0`),
	// `+0x5f38` (the schedule), `+0x5f3c = 0`, and — under `m_debugOverlays & 0x8000000`
	// (`npc_task_text`, `0x10274000`) — `DevMsg("   TaskFail -> %s\n")` (`0x105cc5e0`). The port
	// prints that line through the NPC trace; NAMED DIVERGENCE (debug output only): the `developer`
	// half of the gate (shipped 0) is dropped for the print and the overlay record is not kept.
	// The AI trace's `taskfail` event is the same name and number (debug output only).
	const bool bPrint = (DebugOverlays & OverlayTaskTextBit) != 0;
	if (bPrint || IsAiTraced())
	{
		const FString Failure = FString::Printf(TEXT("%s (0x%x)"), ElysiumTaskFailureName(Reason), Reason);
		if (bPrint)
		{
			NpcTraceMessage(FString::Printf(TEXT("   TaskFail -> %s"), *Failure));
		}
		EmitAiTrace(TEXT("taskfail"), Failure);
	}
	// The three writes are the body.
	BaseScheduleHost.bShouldMove = false;                                    // +0x1a40
	BaseScheduleHost.FailureReason = Reason;                                 // +0x5c50 taskFailureCode
	Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);                   // SetCondition(0x5c)
}

void FElysiumNpcBase::Serialize(FElysiumSaveArchive& Ar)
{
	// The `CAI_BaseNPC` half of the NPC record, written ahead of the Troika's (the Troika `Save`
	// calls `0x1027bc60` first): retail's one hand block (`AIExtendedSaveHeader_t`), then the base words no
	// retail datamap row reaches through the generated walk.
	SerializeExtendedHeader(Ar); // 0x1027bc60
	int32 SavedNpcState = Mind.NpcStateRetail(), SavedIdealState = Mind.IdealStateRetail(); // +0x5cc0/+0x5cc4, 0x1027bc60
	bool bSavedForceState = Mind.IsStateChangeForced(); // +0x1b28 BOOL SAVE
	double SavedStateTime = Mind.GetLastStateChangeTime(); // +0x5cc8 TIME SAVE
	Ar << SavedNpcState << SavedIdealState << bSavedForceState;
	Ar.Time(SavedStateTime);
	if (Ar.IsLoading())
	{
		Mind.Restore(EElysiumNpcState::Idle); // host admission only; raw words below supersede the old SCRIPT clamp
		Mind.WriteNpcStateRetail(SavedNpcState);
		Mind.WriteIdealStateRetail(SavedIdealState);
		Mind.StampLastStateChangeTime(SavedStateTime);
		if (bSavedForceState) Mind.ForceStateChange(); else Mind.ClearForceStateChange();
	}
	Schedule.Serialize(Ar); // embedded AIScheduleState_t +0x5c40; FailureReason has its own writer below
	bool bGathered = Cognition.GatheredAt >= 0.0; // +0x5ca4 BOOL SAVE, not TIME
	Ar << bGathered; // 0x1027bc60/0x1027c160
	if (Ar.IsLoading()) Cognition.GatheredAt = bGathered ? Ar.RestoreBase() : -1.0;
	Ar << ScheduleIdealActivity.OwnerStem << ScheduleIdealActivity.OwnerRoot << ScheduleIdealActivity.Label; // 0x1008df10 task clip continuation identity
	Ar << SequenceZero.Label << SequenceZero.OwnerStem << SequenceZero.bLoops << SequenceZero.Seconds; // raw native seq0 binding, no weighted pick
	Ar << ActivityNumber << IdealActivityNumber << TranslatedActivity; // combat +0xfec/+0xff0/+0xff4 INT SAVE, 0x1008df10
	Ar << SequenceNumber << SequenceCycle << SequenceCycleRate; // 0x1008f120 +0x6f0 INT, +0x6f8 FLOAT / derived rate
	Ar.Time(AnimTime); // +0x174 TIME SAVE, 0x101a0a80
	Ar.Time(PrevAnimTime); // +0x170 TIME SAVE
	Ar.Time(LastEventCheck); // +0x658 TIME SAVE; layer cursor below is FLOAT
	Ar << bSequenceFinished << bSequenceLoopedOnce << SequencePastHalf; // 0x1008f120 +0x65c/+0x65d/+0x568
	Ar << GroundSpeed << YawSpeed << bGroundSpeedFromIntervalMovement; // 0x1008f120 live speed words
	SerializeNativeOverlay(Ar); // 0x10098c80 four layer rows and three flinch rows
	Ar << MoveAndShootOverlay.bMovingAndShooting << MoveAndShootOverlay.MoveShots; // 0x102e8aa0 +0x10/+0x14
	Ar.Time(MoveAndShootOverlay.NextShotTime, EElysiumTimePolicy::MaxFloat); // +0x18 TIME SAVE mode4
	Ar << MoveAndShootOverlay.MinBurst << MoveAndShootOverlay.MaxBurst; // +0x1c/+0x20 INT SAVE
	Ar << MoveAndShootOverlay.PauseMin << MoveAndShootOverlay.PauseMax << MoveAndShootOverlay.InitialDelay; // +0x24..2c FLOAT SAVE
	Navigator.Serialize(Ar); // 0x102ee1e0 semantic route, not UE cache pointers
	Ar << bMoveIssued << bDeathCommitted; // 0x1032c0e0 continuation and committed corpse identity
	Ar.Time(DeathPerformanceEndsAt, EElysiumTimePolicy::Zero); // 0x10265b00 port clip deadline, map-clock domain
	if (Ar.IsLoading())
	{
		RestoreThinkCallback(); // +0x118 is archived once by lane1's base snapshot
		bNativeAnimationRestorePending = true; // 0x1008df10 seek when its actual body is available
	}
	// The combat character's flag words travel with the record and not with the walk: retail's
	// `m_bfAINPCFlags` pair is a concern the shape map reaches no compiled path into. The NPC's own
	// `m_iIsOblivious` and `m_bfNPCFrenziedFlags` are generated bindings and ride the walk.
	NpcFlags.Serialize(Ar);
	Relationships.Serialize(Ar);
	BaseMemory.Serialize(Ar);
	FElysiumNpcPendingSound::SerializeQueue(Ar, PendingSounds);
	EnemyMemory.Serialize(Ar);
	BaseScheduleHost.Serialize(Ar);
	// `CBaseEntity::m_vecAttackExtents` (+0x50), on the entity since story 5 step 6; saved where the
	// base host saved it, so the record's order is unchanged.
	Ar << AttackExtentsCm;
	// `m_hTargetEnt`, a retail `SAVE` row and a recorded gap in the generated walk.
	Ar << TargetEnt; // +0x5ce4 EHANDLE SAVE, 0x1027bc60
	Ar << ScriptOwner << BaseScheduleHost.GoalEnt << IgnoreCollisionEntity; // +0x5d74/+0x5de8/+0x55c
	Ar << NavPathScalar20; // path+0x20 route operand, 0x102ee1e0; bShouldMove is accessor-owned
	Ar << RetailMoveType << RetailMoveCollide << RetailSolidType << RetailSolidFlags; // 0x100a9f70 native physical SAVE words
	Ar << RetailSolidSets; // port availability spelling for the saved solid word
	bool bSavedDeathReported = HasReportedDeath(); // 0x10265a90 once-only death output latch
	Ar << bSavedDeathReported;
	if (Ar.IsLoading()) SetDeathReportedForRestore(bSavedDeathReported);
	Ar << FadeRenderAlpha; // +0x1a3 BYTE SAVE on base-only NPC, 0x10269960
}

// --- Moved from `ElysiumNpc.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::IsIdealActivityCurrent() const
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr || !ScheduleIdealActivity.IsValid())
	{
		return false;
	}

	FElysiumClipPhase Current;
	if (!Embodiment->GetBodyClipPhase(Visual, EElysiumAnimChannel::Base, Current)
		|| !Current.IsValid())
	{
		return false;
	}
	// Both sides are canonicalized before they cross their seams: the activity resolver supplies the
	// owning include-DAG bank, and the body publishes that same committed identity. Case-insensitive
	// comparison is the convention at every other identity join; no source-model alias participates.
	return Current.OwnerStem.Equals(ScheduleIdealActivity.OwnerStem, ESearchCase::IgnoreCase)
		&& Current.OwnerRoot.Equals(ScheduleIdealActivity.OwnerRoot, ESearchCase::IgnoreCase)
		&& Current.Label.Equals(ScheduleIdealActivity.Label, ESearchCase::IgnoreCase);
}

void FElysiumNpcBase::RecordScheduleEvent(const FString& Row)
{
	Mind.RecordExternal(Row);
	// NAMED MODERNIZATION (debug output only): the port's schedule rows — among them every
	// selector's `+0x1b2c`/`+0x1b30`/`+0x1b34` stamp (retail writes the selector id, `__FILE__` and
	// `__LINE__` and prints nothing) — join the NPC trace under `npc_task_text`, indented 2.
	if ((DebugOverlays & OverlayTaskTextBit) != 0)
	{
		NpcTraceMessage(Row, 2);
	}
}

void FElysiumNpcBase::StopMoving()
{
	if (Motor != nullptr)
	{
		Motor->Stop();
		ClearMoveIgnores();
	}
	bMoveIssued = false;
}

FName FElysiumNpcBase::SelectedThinkCallback() const
{
	// 0x100a9f70: public FUNCTION identity is authoritative, including NULL.
	return ThinkCallback;
}

void FElysiumNpcBase::RestoreThinkCallback()
{
	// 0x100aa140: restore dispatch spelling without ThinkSet/deadline side effects.
	ThinkFunctionName = ThinkCallback.IsNone() ? FString() : ThinkCallback.ToString();
}

void FElysiumNpcBase::RebaseSavedReferences(FElysiumEntityWorld& InWorld)
{
	FElysiumScriptedCharacter::RebaseSavedReferences(InWorld); // 0x101a2e40 all words before consumers
	TargetEnt = InWorld.RestoreHandle(TargetEnt); // +0x5ce4 EHANDLE SAVE, 0x1027bf50
	ScriptOwner = InWorld.RestoreHandle(ScriptOwner); // +0x5d74 m_hCine
	BaseScheduleHost.GoalEnt = InWorld.RestoreHandle(BaseScheduleHost.GoalEnt); // +0x5de8 CLASS PTR
	BlockedDoor = InWorld.RestoreHandle(BlockedDoor); // +0x5d28
	CondHitByDoor = InWorld.RestoreHandle(CondHitByDoor); // +0x5d2c
	ShootTargetOverride = InWorld.RestoreHandle(ShootTargetOverride); // +0x5ba8
	IgnoreCollisionEntity = InWorld.RestoreHandle(IgnoreCollisionEntity); // +0x55c, 0x1008df10
	BaseMemory.Rebase(InWorld); // 0x102df090 enemy before OnRestore liveness checks
	EnemyMemory.BindOwner(*this); // 0x102df090 owner/world pointers are rebound, never serialized
	EnemyMemory.Rebase(InWorld); // 0x102df090
	Relationships.Rebase(InWorld); // 0x10273790
	Navigator.RebaseSavedReferences(InWorld); // 0x102ee1e0
	// 0x101a7880/0x1027bf50: director links are rebound in the shared fixup fence, not OnPostRestore.
	if (FElysiumScriptedSequence* Director = AsSpecies<FElysiumScriptedSequence>())
	{
		Director->LastInputActivator = InWorld.RestoreHandle(Director->LastInputActivator);
		Director->LastInputCaller = InWorld.RestoreHandle(Director->LastInputCaller);
		FElysiumEntity* OwnedEntity = Director->RestoredNpcIndex == INDEX_NONE ? nullptr
			: InWorld.Resolve(FElysiumEntityHandle(Director->RestoredNpcIndex, InWorld.GetEpoch()));
		FElysiumNpcBase* OwnedNpc = OwnedEntity != nullptr ? OwnedEntity->AsNpcBase() : nullptr;
		if (OwnedNpc != nullptr && !OwnedNpc->IsDead())
		{
			Director->SetTarget(OwnedNpc->Handle);
			OwnedNpc->ScriptOwner = Director->Handle;
			OwnedNpc->BaseScheduleHost.GoalEnt = Director->Handle;
		}
		Director->RestoredNpcIndex = INDEX_NONE;
	}

}

TFunction<bool(FElysiumNpcBase&)> FElysiumNpcBase::RestoreNativeAnimationAdapter;

bool FElysiumNpcBase::RestoreNativeAnimation()
{
	// 0x1008df10: invalidate studio descriptor cache; no ResetSequenceInfo, pick, event or cursor reset.
	SequenceDescriptorRows.Reset();
	bNativeAnimationRestoreApplied = RestoreNativeAnimationAdapter ? RestoreNativeAnimationAdapter(*this) : false;
	bNativeAnimationRestorePending = !bNativeAnimationRestoreApplied; // unavailable admission stays unavailable
	return bNativeAnimationRestoreApplied;
}

void FElysiumNpcBase::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	PrepareRestoredBody(); // 0x100aa140 motor position before route reconstruction
	OnRestore(InWorld.IsLevelTransitionRestore()); // 0x1011a710/0x1011a620: false on ordinary save load
	RestoreThinkCallback(); // 0x100aa140 identity only, no timer write
	RestoreNativeAnimation(); // 0x1008df10 unavailable visual consumer never resets logical words
}

void FElysiumNpcBase::PrepareRestoredBody()
{
	// 0x100aa140/0x102ee1e0: native fields restore position before navigator refind. The existing
	// UE Teleport cancels its route, so perform it once here and consume the applier's later duplicate.
	FElysiumScriptedCharacter::OnRuntimeTransformChanged();
	if (Motor) Motor->SetEnabled(!IsInert()); // restored physical words before0x102ee1e0 route acceptance, not an activation/think
	SnapshotTransformOrigin = Origin; SnapshotTransformAngles = Angles;
	bSkipSnapshotTransformOnce = true;
}

void FElysiumNpcBase::OnRuntimeTransformChanged()
{
	if (World != nullptr && World->IsApplyingSnapshot() && bSkipSnapshotTransformOnce)
	{
		if (Origin == SnapshotTransformOrigin && Angles == SnapshotTransformAngles)
		{
			bSkipSnapshotTransformOnce = false; // 0x102ee1e0 skip only the unchanged applier duplicate
			return;
		}
		FElysiumScriptedCharacter::OnRuntimeTransformChanged(); // 0x100aa140 later native transform writes still win
		SnapshotTransformOrigin = Origin; SnapshotTransformAngles = Angles;
		return;
	}
	FElysiumScriptedCharacter::OnRuntimeTransformChanged(); // ordinary live transform writer
}

void FElysiumNpcBase::OnPreparedVisualAttached()
{
	FElysiumScriptedCharacter::OnPreparedVisualAttached(); // stand the existing motor, no AI initialization
	if (bNativeAnimationRestorePending) RestoreNativeAnimation(); // 0x1008df10 event-free phase rebind
}
