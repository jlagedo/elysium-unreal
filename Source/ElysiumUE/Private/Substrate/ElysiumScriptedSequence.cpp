#include "Substrate/ElysiumScriptedSequence.h"

#include "ElysiumAnimationIntent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcKernelTunables.h"

#include "Misc/Paths.h"

// `scripted_sequence` — `CCineNPC`, the script director (story 5 fold A3). Every body below is the
// retail body at the address its comment names, read off the decompilation and, where a branch
// mattered, the listing (`vtmb_asm`). The walked prose is `docs/vtmb/npc-ai/authored-control.md`
// § "Scripted sequences" and `docs/specs/0003-scripted-sequence/spec.md`.
//
// The outputs are the load-bearing half: across the exported maps 88 wires leave these entities,
// 48 of them `OnEndSequence`, and they unlock doors, restore cameras and start conversations.
// `OnBeginSequence` fires from `StartScript` (`0x101a81a0`), which the possessed NPC's
// `TASK_WAIT_FOR_SCRIPT` reaches once the NPC stands on its mark and `IsTimeToStart` passes — NOT at
// the input. `OnEndSequence` fires from `SequenceDone` (`0x101a8460`) unconditionally, before any
// post-idle, spawnflag `0x100` included.

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSeq, Log, All);

namespace
{
	// `CCineNPC` spawnflags, by their retail readers.
	constexpr int32 GCineSfRepeatable = 0x4;            // `Finish` 0x101a8640: no SUB_Remove, self-chain allowed
	constexpr int32 GCineSfStartOnSpawn = 0x10;         // `Spawn` 0x101a6f10: arm `CineThink` on a named cine
	constexpr int32 GCineSfNoInterrupt = 0x20;          // `Spawn`: `m_interruptable = 0`; `AllowInterrupt` gate
	constexpr int32 GCineSfOverrideState = 0x40;        // slot 585 `0x101a7210`
	constexpr int32 GCineSfLeaveCorpsePose = 0x80;      // `CineCleanup` 0x1027d170: skip the bone-0 placement
	constexpr int32 GCineSfHoldPostIdle = 0x100;        // `Finish`: replay the post-idle forever
	constexpr int32 GCineSfPriority = 0x200;            // `CanOverride` 0x101a8ac0, read on the QUEUED cine
	constexpr int32 GCineSfCyclicSearch = 0x400;        // `0x101a7760`: `m_pLastFoundEntity` latch
	constexpr int32 GCineSfQuietSearch = 0x800;         // `FindEntity` 0x101a7600: no "can't play" line
	constexpr int32 GCineSfIgnoreNpcCollision = 0x1000; // `PossessEntity`: `m_bfAINPCFlags |= 0x40`

	// `m_scriptState` values (`+0x5d70`).
	constexpr int32 GScriptPlaying = 0;
	constexpr int32 GScriptWait = 1;
	constexpr int32 GScriptPostIdle = 2;
	constexpr int32 GScriptCleanup = 3;
	constexpr int32 GScriptWalkToMark = 4;
	constexpr int32 GScriptRunToMark = 5;
	constexpr int32 GScriptCustomMoveToMark = 6;

	// Retail `NPC_STATE` ids.
	constexpr int32 GNpcStateIdle = 1;
	constexpr int32 GNpcStateScript = 4;
	constexpr int32 GNpcStateDead = 7;

	// `_DAT_10445e08` — the 0.05 s start delay `BeginSequence` and `DelayStart` add to curtime.
	constexpr double GCineStartDelaySeconds = 0.05;
	// `_DAT_10449280` — `CineThink`'s retry and `Spawn`'s first think, 1.0 s.
	constexpr double GCineThinkDelaySeconds = ElysiumNpcTunables::OneDouble;
	// `_DAT_10449e10` — the "wait for BeginSequence" start time, curtime + 1e6.
	constexpr double GCineStartTimeFar = ElysiumNpcTunables::CineStartTimeOffset;
	// `_DAT_104493d0` — `Finish`'s `SUB_Remove`, curtime + 0.1.
	constexpr double GCineRemoveDelaySeconds = ElysiumNpcTunables::TenthDouble;

	// The selector-trace source lines retail stamps beside each `m_IdealNPCState` write
	// (`+0x1b40`), the mind's transition trace carries them.
	constexpr int32 GCinePossessLine = 0x2c8;        // `CCineNPC::PossessEntity` `0x101a7880`
	constexpr int32 GCineFixScheduleLine = 0x3ca;    // `CCineNPC::FixScriptNPCSchedule` `0x101a8840`
	constexpr int32 GCineCleanupIdleLine = 0x2b1d;   // `CineCleanup` `0x1027d170`, alive
	constexpr int32 GCineCleanupDeadLine = 0x2b22;   // `CineCleanup`, health below 1

	// `m_bfAINPCFlags` bit `0x40`, `NAV_IGNORE_NPC`: what spawnflag `0x1000` ORs in for the beat.
	constexpr uint32 GNavIgnoreNpc = 0x40u;
	// `AddFlag2(0x10)` at the end of `Spawn`.
	constexpr uint32 GCineFlags2 = 0x10u;
	// `FSOLID_NOT_SOLID`, `SOLID_NONE`, `SOLID_BBOX`, `MOVETYPE_FLY`.
	constexpr uint32 GSolidFlagNotSolid = 0x4u;
	constexpr int32 GSolidNone = 0;
	constexpr int32 GSolidBbox = 2;
	constexpr int32 GMoveTypeNone = 0;
	constexpr int32 GMoveTypeFly = 4;
	// `CineCleanup`'s dead-cine arm: `SetSolidFlags(0x10)`.
	constexpr uint32 GSolidFlagsCleanupDeadCine = 0x10u;
	// `m_spawnflags` bit the NPC loses in `CineCleanup` (`& 0xffffff7f`).
	constexpr int32 GNpcSfWaitForScript = 0x80;

	// The beat stand-in's cadence: the NPC's scripted tasks run inside its think; the port polls them
	// at the patrol cadence.
	constexpr double GBeatTickSeconds = 0.05;

	FString CineDebugName(const FElysiumEntity& Entity)
	{
		if (!Entity.TargetName.IsEmpty())
		{
			return Entity.TargetName;
		}
		return Entity.Class != nullptr ? Entity.Class->ClassName.ToString() : FString();
	}

	// One segment of the beat's montage-slot run, at the SCRIPTED band and held, so the NPC's own
	// locomotion publish cannot take the channel between the beat's clips.
	FElysiumClipSegment BeatSegment(const FString& ClipName, bool bLoop)
	{
		FElysiumClipSegment Segment;
		Segment.ClipName = ClipName;
		Segment.bLoop = bLoop;
		Segment.Source = EElysiumAnimSource::Interaction;
		Segment.Priority = EElysiumAnimPriority::Scripted;
		Segment.bHoldUntilReleased = true;
		return Segment;
	}

	double CineNow(const FElysiumEntity& Entity)
	{
		return Entity.World != nullptr ? Entity.World->NowSeconds() : 0.0;
	}
}

// --- Own slots ------------------------------------------------------------------------------------

// Slot 72: `0x101a6e20`.
bool FElysiumScriptedSequence::Slot72(int32 Discipline)
{
	(void)Discipline;
	return false;
}

// Slot 82: `0x101a5db0` returns `&datamap_CCineNPC`.
void* FElysiumScriptedSequence::GetDataDescMap()
{
	return const_cast<FElysiumClassDesc*>(FElysiumClassRegistry::Get().Find(FName(RetailClassName)));
}

// Slot 103: `0x101a6f10`, in retail's order. No base `Spawn`, so no `NPCInit`: a director never
// thinks as AI.
void FElysiumScriptedSequence::Spawn()
{
	// 1. `SetSolid(SOLID_NONE)` on `m_Collision` under its scope-trace frame.
	RetailSolidType = GSolidNone;
	++RetailSolidSets;
	// 2. `AddSolidFlags` inlined as `SetSolidFlags(word[+0x2b4] | FSOLID_NOT_SOLID)`.
	RetailSolidFlags = (RetailSolidFlags & 0xffffu) | GSolidFlagNotSolid;
	// 3. Slot 93 `SetMoveType(MOVETYPE_NONE, MOVECOLLIDE_DEFAULT)`, virtually.
	SetMoveType(GMoveTypeNone, 0);
	// 4. `m_bIsBCCTargetable = 0`, `m_bIsAlive = 0`.
	bIsBccTargetable = false;
	bNpcIsAlive = false;
	// 5. An unnamed cine, or spawnflag `0x10`: `ThinkSet(CineThink)`, `m_flNextThink = now + 1.0`,
	//    and a NAMED one waits for `BeginSequence` (`m_startTime = now + 1e6`).
	const double Now = CineNow(*this);
	if (TargetName.IsEmpty() || (SpawnFlags & GCineSfStartOnSpawn) != 0)
	{
		ArmCineThink(EThinkFunction::CineThink, Now + GCineThinkDelaySeconds);
		if (!TargetName.IsEmpty())
		{
			StartTime = Now + GCineStartTimeFar;
		}
	}
	// 6. `m_interruptable = !(spawnflags & 0x20)`.
	bInterruptable = (SpawnFlags & GCineSfNoInterrupt) == 0;
	// 7. `CBaseEntity::Relink` — no port counterpart (the port links no entity tree).
	// 8. `m_sequenceStarted = 0`, `m_hNextCine = -1`, `m_pLastFoundEntity = 0`, `AddFlag2(0x10)`.
	bSequenceStarted = false;
	NextCine = FElysiumEntityHandle::Invalid();
	LastFoundEntity = FElysiumEntityHandle::Invalid();
	Flags2Added |= GCineFlags2;
}

// Slot 113: `0x101a8de0`. Plays NOTHING: retail's pre-idle belongs to `TASK_WAIT_FOR_SCRIPT`.
void FElysiumScriptedSequence::Activate()
{
	// `CBaseEntity::Activate(this)` — direct (`0x100a0bc0`).
	FElysiumEntity::Activate();

	// `while (a = FindEntityByName(a, m_iszEntity)) if (a->+0x94) break;` — the first entity of the
	// name that is an NPC base (`m_pBaseNPC`). A named marker or trigger sharing the actor's name is
	// skipped. A director is itself an NPC base, which is retail's own answer.
	FElysiumEntity* Actor = nullptr;
	if (World != nullptr && !TargetEntity.IsEmpty())
	{
		World->ForEachNamed(TargetEntity, [&Actor](FElysiumEntity& Candidate)
			{
				if (Actor == nullptr && Candidate.AsNpcBase() != nullptr)
				{
					Actor = &Candidate;
				}
			});
	}

	const TCHAR* const Divider = TEXT("--------------------");
	if (Actor == nullptr)
	{
		// `"Could not find NPC %s in CCineNPC::Activate (%s)"` — skips the precache.
		ActivateDiagnostics.Add(Divider);
		ActivateDiagnostics.Add(FString::Printf(TEXT("Could not find NPC %s in CCineNPC::Activate (%s)"),
			*TargetEntity, *DebugString()));
		ActivateDiagnostics.Add(Divider);
	}
	else if (Actor->Model.IsEmpty())
	{
		// SEAM for `CBaseAnimating::GetModelPtr`: this port resolves a model by name, so a non-empty
		// `Model` is the same question. Retail formats THIS cine's debug name first and discards it.
		ActivateDiagnostics.Add(Divider);
		ActivateDiagnostics.Add(FString::Printf(TEXT("NPC %s has no model in CCineNPC::Activate"),
			*Actor->DebugString()));
		ActivateDiagnostics.Add(Divider);
	}
	else
	{
		// `0x10428880(model, name)` on `m_iszPreIdle`, `m_iszPostIdle`, `m_iszPlay`, in that order.
		// SEAM: recorded, nothing acquired (no studio header at kernel level).
		for (const FString* Name : { &PreIdle, &PostIdle, &Play })
		{
			ActivatePrecacheLog.Add({ Actor->DebugString(), *Name });
		}
	}

	// `m_hNextCine = FindEntityByName(0, m_iszNextScript) ?: -1`, and a handle that does not
	// resolve clears `m_iszNextScript`.
	NextCine = FElysiumEntityHandle::Invalid();
	if (World != nullptr && !NextScript.IsEmpty())
	{
		if (FElysiumEntity* Next = World->FindByName(NextScript))
		{
			NextCine = Next->Handle;
		}
	}
	if (World == nullptr || World->Resolve(NextCine) == nullptr)
	{
		NextScript.Reset();
	}
}

// Slot 117: `0x101a6d20`.
int32 FElysiumScriptedSequence::ObjectCaps() const
{
	return FElysiumEntity::ObjectCaps() & ~ElysiumEntityCaps::AcrossTransition;
}

// Slot 175: `0x101a75a0`.
void FElysiumScriptedSequence::Touch(FElysiumEntity* Other)
{
	(void)Other;
}

// Slot 178: `0x101a7580` (verdict `dead`: no caller dispatches slot 178 on a director).
void FElysiumScriptedSequence::Blocked(FElysiumEntity* Other)
{
	(void)Other;
}

// Slot 180: `0x101a7140`.
void FElysiumScriptedSequence::UpdateOnRemove()
{
	// `CAI_BaseNPC::UpdateOnRemove` (`0x1027ca30`), DIRECT.
	FElysiumNpcBase::UpdateOnRemove();
	// `ScriptEntityCancel(this)`: a director removed mid-beat releases its NPC.
	ScriptEntityCancel(*this);
}

// Slots 362–365.
bool FElysiumScriptedSequence::FInViewCone(const FVector& PointCm)
{
	(void)PointCm;
	return false;
}

bool FElysiumScriptedSequence::FInViewCone(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return false;
}

bool FElysiumScriptedSequence::FInAimCone(const FVector& TargetCm)
{
	(void)TargetCm;
	return false;
}

bool FElysiumScriptedSequence::FInAimCone(FElysiumEntity* AimTarget)
{
	(void)AimTarget;
	return false;
}

// Slot 370: `0x101a6d40`, a tail call through slot 368.
FVector FElysiumScriptedSequence::HeadDirection2D()
{
	return BodyDirection2D();
}

// Slot 371: `0x101a6d70`, a tail call through slot 369.
FVector FElysiumScriptedSequence::HeadDirection3D()
{
	return BodyDirection3D();
}

// Slot 459: `0x101a89a0`. Reached from the NPC's own slot 459 (`0x1026d7f0`) while it is in SCRIPT.
void FElysiumScriptedSequence::RemoveIgnoredConditions()
{
	if (CanInterrupt())
	{
		return;
	}
	if (FElysiumNpcBase* Npc = TargetNpc())
	{
		ClearCineIgnoredConditions(*Npc);
	}
}

// --- Slots 583–586 --------------------------------------------------------------------------------

// Slot 583: `0x101a7880`.
void FElysiumScriptedSequence::PossessEntity()
{
	FElysiumNpcBase* Npc = TargetNpc();
	if (Npc == nullptr)
	{
		return;
	}
	// The "has not run its AI yet" block. SEAM for the byte retail tests (`+0x6*…`, the first-think
	// latch): the port's admission barrier is the same fact.
	if (!Npc->GetMind().IsAdmitted())
	{
		Diagnostic(FString::Printf(TEXT("scripted_sequence %s is targeting an entity (%s) that has not run "
			"it's AI yet"), *CineDebugName(*this), *CineDebugName(*Npc)));
	}

	// The QUEUE arm: the NPC already holds a live cine. Kick that cine's queued next (clear its
	// target), queue THIS one there, and take nothing.
	if (World != nullptr && Npc->ScriptOwner.IsSet() && World->Resolve(Npc->ScriptOwner) != nullptr)
	{
		FElysiumScriptedSequence* Old = Npc->ResolveCine();
		if (Old == nullptr)
		{
			// **Port ownership rule.** The port's choreographed scene also stands in `ScriptOwner` for
			// its cast; a retail scene never writes `m_hCine`, so retail would possess this NPC here.
			// The port refuses instead: a director taking an actor mid-scene would break the scene's
			// own claim, and which of the two retail lets win is spec 0003/0010's open border.
			Diagnostic(FString::Printf(TEXT("%s: %s is held by a non-director owner; nothing queued"),
				*CineDebugName(*this), *CineDebugName(*Npc)));
			return;
		}
		if (FElysiumEntity* Kicked = World->Resolve(Old->NextCine))
		{
			if (FElysiumNpcBase* KickedBase = Kicked->AsNpcBase())
			{
				KickedBase->SetTarget(FElysiumEntityHandle::Invalid());
			}
			Diagnostic(FString::Printf(TEXT("script \"%s\" kicking script \"%s\" out of the queue"),
				*CineDebugName(*this), *CineDebugName(*Kicked)));
		}
		Old->NextCine = Handle;
		return;
	}

	// `bScriptHidden` (`0x100b5190`, the chain's hidden flag here) -> `0x101a77a0`.
	if (Npc->bHidden)
	{
		ScriptHiddenWarning(*Npc);
	}
	if (!bInterruptable)
	{
		MakeNpcOblivious(*Npc);   // 0x1026d130
	}
	if (NextScript.IsEmpty())
	{
		NextCine = FElysiumEntityHandle::Invalid();
	}
	// `m_pGoalEnt = this`, `m_hCine = this`, `SetTarget(npc, this)`.
	Npc->BaseScheduleHost.GoalEnt = Handle;
	Npc->ScriptOwner = Handle;
	Npc->SetTarget(Handle);
	// The six saved words: slots 94 / 95 / 92 / 211 and `m_fEffects`, then the Troika's flag word.
	SavedMoveType = Npc->RetailMoveType;
	SavedMoveCollide = Npc->RetailMoveCollide;
	SavedSolid = Npc->RetailSolidType;
	SavedSolidFlags = static_cast<int32>(Npc->RetailSolidFlags);
	// SEAM for `m_fEffects` (`+0x19c`): the port binds no effects word (`gen_kernel_bindings`, UNBOUND
	// render word), so nothing is saved and nothing is ORed onto the NPC below.
	SavedEffects = 0;
	if (FElysiumNpc* Troika = Npc->AsNpc())
	{
		// `+0x98` -> slot 614 `ResetThinkTimers`, then `m_bfAINPCFlags` (`+0x14b8`).
		Troika->ResetThinkTimers(CineNow(*this));
		SavedTroikaFlags = static_cast<int32>(Troika->NpcFlags.RawWord1());
		if ((SpawnFlags & GCineSfIgnoreNpcCollision) != 0)
		{
			Troika->NpcFlags.AssignAiFlagsWord(Troika->NpcFlags.RawWord1() | GNavIgnoreNpc);
		}
	}
	// `m_scriptState` by `m_fMoveTo`, each write preceded by the empty `0x1027f270(state)`.
	switch (MoveTo)
	{
	case 1:
		NpcScriptState = GScriptWalkToMark;
		DelayStart(true);
		break;
	case 2:
		NpcScriptState = GScriptRunToMark;
		DelayStart(true);
		break;
	case 3:
		NpcScriptState = GScriptCustomMoveToMark;
		DelayStart(true);
		break;
	case 4:
		// Placed inline: the motor's ideal yaw from this cine's angles, zero angular velocity,
		// `EF_NOINTERP` (no port word), `Teleport(origin, angles)` — then FALLS THROUGH.
		PlaceOnMark(*Npc);
		[[fallthrough]];
	case 0:
	case 5:
		NpcScriptState = GScriptWait;
		break;
	default:
		break;
	}
	// `m_IdealNPCState = NPC_STATE_SCRIPT`, trace line 0x2c8.
	Npc->RequestIdealStateRetail(GNpcStateScript, GCinePossessLine);
	Diagnostic(FString::Printf(TEXT("Sequence %s targeting %s posessing"), *CineDebugName(*this),
		*CineDebugName(*Npc)));

	BeginBeat(*Npc);
}

// Slot 584: `0x101a82d0`.
bool FElysiumScriptedSequence::StartSequence(FElysiumNpcBase& Npc, const FString& SequenceName,
	bool bCompleteOnEmpty)
{
	bSequenceStarted = true;
	if (SequenceName.IsEmpty() && bCompleteOnEmpty)
	{
		SequenceDone(Npc);
		return false;
	}
	// `LookupSequence`; -1 -> `"%s: unknown scripted sequence \"%s\""` and sequence 0; cycle 0 and
	// `0x10090950` (reset the sequence info). The port plays the named clip on the body instead of
	// indexing a studio header; the pose clips loop, `m_iszPlay` (the one `bCompleteOnEmpty` names)
	// runs once.
	const float Seconds = PlayBeatClip(Npc, SequenceName, /*bLoop=*/!bCompleteOnEmpty);
	if (Seconds < 0.f)
	{
		UE_LOG(LogElysiumSeq, Log, TEXT("%s: unknown scripted sequence \"%s\""), *CineDebugName(Npc),
			*SequenceName);
	}
	BeatClipEndsAt = CineNow(*this) + FMath::Max(0.f, Seconds);
	Diagnostic(FString::Printf(TEXT("Sequence %s targeting %s is starting %s"), *CineDebugName(*this),
		*CineDebugName(Npc), *SequenceName));
	return true;
}

// Slot 585: `0x101a7210`.
bool FElysiumScriptedSequence::FCanOverrideState() const
{
	return (SpawnFlags & GCineSfOverrideState) != 0;
}

// Slot 586: `0x101a8840`. `m_iFinishSchedule` is NOT read here.
void FElysiumScriptedSequence::FixScriptNPCSchedule(FElysiumNpcBase& Npc)
{
	if (Npc.IdealStateRetail() != GNpcStateDead)
	{
		Npc.RequestIdealStateRetail(GNpcStateIdle, GCineFixScheduleLine);
	}
	Npc.ClearSchedule();
}

// --- The think ------------------------------------------------------------------------------------

void FElysiumScriptedSequence::ArmCineThink(EThinkFunction Function, double At)
{
	ThinkFunction = Function;
	CineThinkAt = At;
	RescheduleThink();
}

void FElysiumScriptedSequence::RescheduleThink()
{
	const double Cine = ThinkFunction != EThinkFunction::None ? CineThinkAt : ELYSIUM_NEVER_THINK;
	const double Next = FMath::Min(Cine, BeatThinkAt);
	NextThink = Next >= ELYSIUM_NEVER_THINK ? ELYSIUM_NEVER_THINK : static_cast<float>(Next);
}

void FElysiumScriptedSequence::ThinkAt(double Now)
{
	// Retail's `m_pfnThink` first: Source clears `m_flNextThink` before the call, so a think that
	// does not re-arm itself stops.
	if (ThinkFunction != EThinkFunction::None && CineThinkAt < ELYSIUM_NEVER_THINK && Now >= CineThinkAt)
	{
		const EThinkFunction Function = ThinkFunction;
		CineThinkAt = ELYSIUM_NEVER_THINK;
		if (Function == EThinkFunction::CineThink)
		{
			CineThink(Now);
		}
		else if (Function == EThinkFunction::SubRemove)
		{
			RemoveSelf();
			return;
		}
	}
	if (Phase != EBeatPhase::None && BeatThinkAt < ELYSIUM_NEVER_THINK && Now >= BeatThinkAt)
	{
		BeatThinkAt = ELYSIUM_NEVER_THINK;
		TickBeat(Now);
	}
	RescheduleThink();
}

// `CineThink` `0x101a8070`.
void FElysiumScriptedSequence::CineThink(double Now)
{
	if (FindEntityWrap())
	{
		bSequenceStarted = false;
		PossessEntity();   // slot 583, virtual
		Diagnostic(FString::Printf(TEXT("script \"%s\" using NPC \"%s\""), *CineDebugName(*this),
			*TargetEntity));
		return;
	}
	CancelScript();
	Diagnostic(FString::Printf(TEXT("script \"%s\" can't find NPC \"%s\""), *CineDebugName(*this),
		*TargetEntity));
	// `m_flNextThink = now + 1.0` — the function stays `CineThink`: retry forever.
	ArmCineThink(EThinkFunction::CineThink, Now + GCineThinkDelaySeconds);
}

void FElysiumScriptedSequence::RemoveSelf()
{
	// `SUB_Remove` `0x101c0b10` -> `UTIL_Remove`, whose first act is slot 180.
	if (IsDead())
	{
		return;
	}
	UpdateOnRemove();
	Kill();
}

// --- Target acquisition ---------------------------------------------------------------------------

FElysiumEntity* FElysiumScriptedSequence::FindGenericWithin(FElysiumEntity* Start, const FString& Name,
	const FVector& CenterCm, float RadiusUnits)
{
	if (World == nullptr || Name.IsEmpty())
	{
		return nullptr;
	}
	// `0x100f7c30` / `0x100f7e30`: a zero radius is unbounded; otherwise the candidate's origin must lie
	// STRICTLY inside it (`d² < r²`). The port's origins are centimetres, `m_flRadius` is authored in
	// Source units. Retail also requires the candidate's `m_Network.m_pPev` (`+0x2e0`, the edict) to be
	// non-null; SEAM: the port has no edicts, and `!IsDead()` (a removed entity has none) stands in.
	const double RadiusCm = static_cast<double>(RadiusUnits) * ElysiumMove::U;
	const bool bBounded = RadiusUnits * RadiusUnits != 0.f;
	auto Within = [&CenterCm, RadiusCm, bBounded](const FElysiumEntity& Candidate)
	{
		return !Candidate.IsDead()
			&& (!bBounded || FVector::DistSquared(Candidate.Origin, CenterCm) < RadiusCm * RadiusCm);
	};
	// `FindEntityByName`'s procedural names answer one entity, and only to a search from the start;
	// the radius gate applies to that entity like any other (`0x100f7c30` tests whatever
	// `0x100f7770` returns). No classname fallback follows a procedural name: `0x100f7e30` would
	// compare the literal `!name` against classnames and match nothing.
	if (Name.StartsWith(TEXT("!")))
	{
		if (Start != nullptr)
		{
			return nullptr;
		}
		FElysiumEntity* Hit = ResolveProceduralName(Name);
		return Hit != nullptr && Within(*Hit) ? Hit : nullptr;
	}
	const TArray<TUniquePtr<FElysiumEntity>>& List = World->Entities();
	const int32 First = Start != nullptr ? Start->Handle.Index + 1 : 0;
	// `0x100f7f70`: the next name match after `Start`, else the next CLASSNAME match after it.
	for (int32 Index = First; Index < List.Num(); ++Index)
	{
		FElysiumEntity* Candidate = List[Index].Get();
		if (Candidate != nullptr && !Candidate->IsDead() && !Candidate->TargetName.IsEmpty()
			&& FElysiumEntityWorld::NameMatches(Candidate->TargetName, Name) && Within(*Candidate))
		{
			return Candidate;
		}
	}
	for (int32 Index = First; Index < List.Num(); ++Index)
	{
		FElysiumEntity* Candidate = List[Index].Get();
		const FString Classname = Candidate != nullptr && Candidate->Def != nullptr
			? Candidate->Def->Classname : FString();
		if (Candidate != nullptr && !Candidate->IsDead() && !Classname.IsEmpty()
			&& FElysiumEntityWorld::NameMatches(Classname, Name) && Within(*Candidate))
		{
			return Candidate;
		}
	}
	return nullptr;
}

FElysiumEntity* FElysiumScriptedSequence::ResolveProceduralName(const FString& Name)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	if (Name.Equals(TEXT("!player"), ESearchCase::IgnoreCase))
	{
		return World->FindPlayer();
	}
	if (Name.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
	{
		return World->FindPlayerController();   // the player's `m_hControllerNPC` (`0x101618a0`)
	}
	if (Name.Equals(TEXT("!pvsplayer"), ESearchCase::IgnoreCase))
	{
		// SEAM: retail answers the player in the searching entity's PVS (`0x101d1800`); the port
		// has no PVS query here, so the player stands in.
		return World->FindPlayer();
	}
	if (Name.Equals(TEXT("!activator"), ESearchCase::IgnoreCase))
	{
		return World->Resolve(LastInputActivator);   // activator arg 0 -> searching `+0x10c`
	}
	if (Name.Equals(TEXT("!caller"), ESearchCase::IgnoreCase))
	{
		return World->Resolve(LastInputCaller);      // searching `+0x110`
	}
	if (Name.Equals(TEXT("!picker"), ESearchCase::IgnoreCase))
	{
		// SEAM: the entity under the host player's crosshair (`0x10172710`), a debug verb.
		return nullptr;
	}
	UE_LOG(LogElysiumSeq, Log, TEXT("Invalid entity search name %s"), *Name);
	return nullptr;
}

void FElysiumScriptedSequence::RecordInput(const FElysiumInputArgs& Args)
{
	LastInputActivator = Args.Activator;
	LastInputCaller = Args.Caller;
}

// `FindEntity` `0x101a7600`.
FElysiumNpcBase* FElysiumScriptedSequence::FindEntity()
{
	FElysiumNpcBase* Fallback = nullptr;
	// The walk starts at `m_pLastFoundEntity` and stops at the list's end; the centre is this cine's
	// `WorldSpaceCenter` (slot 220), its origin for a point entity.
	FElysiumEntity* Cursor = World != nullptr ? World->Resolve(LastFoundEntity) : nullptr;
	for (FElysiumEntity* Hit = FindGenericWithin(Cursor, TargetEntity, Origin, Radius); Hit != nullptr;
		Hit = FindGenericWithin(Hit, TargetEntity, Origin, Radius))
	{
		FElysiumNpcBase* Npc = Hit->AsNpcBase();
		if (Npc == nullptr)
		{
			continue;
		}
		// Slot 482 `CanPlaySequence(FCanOverrideState(), 0)`: 1 wins at once, the LAST 2 is kept.
		const int32 Answer = Npc->CanPlaySequence(FCanOverrideState(), 0);
		if (Answer == 1)
		{
			return Npc;
		}
		if (Answer == 2)
		{
			Fallback = Npc;
		}
		else if ((SpawnFlags & GCineSfQuietSearch) == 0)
		{
			UE_LOG(LogElysiumSeq, Log, TEXT("Found %s, but can't play!"), *TargetEntity);
			Diagnostic(FString::Printf(TEXT("Found %s, but can't play!"), *TargetEntity));
		}
	}
	return Fallback;
}

// `0x101a7760`.
bool FElysiumScriptedSequence::FindEntityWrap()
{
	FElysiumNpcBase* Found = FindEntity();
	if ((SpawnFlags & GCineSfCyclicSearch) != 0)
	{
		LastFoundEntity = Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid();
	}
	SetTarget(Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid());
	return Found != nullptr;
}

FElysiumNpcBase* FElysiumScriptedSequence::TargetNpc() const
{
	FElysiumEntity* Resolved = World != nullptr ? World->Resolve(GetTarget()) : nullptr;
	return Resolved != nullptr ? Resolved->AsNpcBase() : nullptr;
}

int32 FElysiumScriptedSequence::ScriptStateOf(const FElysiumNpcBase& Npc)
{
	const FElysiumScriptedSequence* Cine = Npc.ResolveCine();
	return Cine != nullptr ? Cine->NpcScriptState : GScriptPlaying;
}

// --- Interruption and cleanup ---------------------------------------------------------------------

// `ScriptEntityCancel` `0x101a7170`.
void FElysiumScriptedSequence::ScriptEntityCancel(FElysiumEntity& Entity)
{
	// The `+0x4c & 0x1000` gate is "this entity is a cine" (the constructor sets it).
	FElysiumNpcBase* AsBase = Entity.AsNpcBase();
	FElysiumScriptedSequence* Cine = AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
	if (Cine == nullptr)
	{
		return;
	}
	// The TARGET's state, not whether this cine owns it: retail does not compare `m_hCine`.
	if (FElysiumNpcBase* Npc = Cine->TargetNpc())
	{
		if (Npc->NpcStateRetail() == GNpcStateScript)
		{
			if (FElysiumScriptedSequence* Owner = Npc->ResolveCine())
			{
				Owner->NpcScriptState = GScriptCleanup;
			}
			CineCleanup(*Npc);
		}
	}
	Cine->Delay = 0;
}

// `CancelScript` `0x101a8c30`.
void FElysiumScriptedSequence::CancelScript()
{
	UE_LOG(LogElysiumSeq, Verbose, TEXT("Cancelling script: %s"), *Play);
	if (World == nullptr || TargetName.IsEmpty())
	{
		ScriptEntityCancel(*this);
		return;
	}
	TArray<FElysiumEntityHandle> Named;
	World->ForEachNamed(TargetName, [&Named](FElysiumEntity& Candidate) { Named.Add(Candidate.Handle); });
	for (const FElysiumEntityHandle& Each : Named)
	{
		if (FElysiumEntity* Entity = World->Resolve(Each))
		{
			ScriptEntityCancel(*Entity);
		}
	}
}

// `CineCleanup` `0x1027d170`, on the NPC.
void FElysiumScriptedSequence::CineCleanup(FElysiumNpcBase& Npc)
{
	FElysiumScriptedSequence* Cine = Npc.ResolveCine();
	const bool bBeatRan = Cine != nullptr && Cine->Phase != EBeatPhase::None;
	if (Cine != nullptr)
	{
		Cine->NpcScriptState = GScriptPlaying;   // `m_scriptState = 0`
	}
	if (Cine == nullptr)
	{
		// A dead `m_hCine`: `SetMoveType(MOVETYPE_FLY, 0)`, `SetSolid(SOLID_BBOX)`, `SetSolidFlags(0x10)`.
		Npc.SetMoveType(GMoveTypeFly, 0);
		Npc.RetailSolidType = GSolidBbox;
		++Npc.RetailSolidSets;
		Npc.RetailSolidFlags = GSolidFlagsCleanupDeadCine;
	}
	else
	{
		if (!Cine->bInterruptable)
		{
			ReleaseNpcOblivious(Npc);   // 0x10007ea0
		}
		Cine->SetTarget(FElysiumEntityHandle::Invalid());
		Npc.SetMoveType(Cine->SavedMoveType, Cine->SavedMoveCollide);
		Npc.RetailSolidFlags = static_cast<uint32>(Cine->SavedSolidFlags) & 0xffffu;
		// `m_fEffects = saved` — SEAM (no port effects word).
		if (FElysiumNpc* Troika = Npc.AsNpc())
		{
			Troika->NpcFlags.AssignAiFlagsWord(static_cast<uint32>(Cine->SavedTroikaFlags));
		}
		// The port's beat stand-in ends with the cine that ran it.
		Cine->EndBeat();
	}
	// The port's presentation claim leaves with `m_hCine`: the body arbiter, the montage run, a
	// scripted move, and — for a `0x1000` cine — the collision view of the restored `NAV_IGNORE_NPC`.
	Npc.ReleaseAnimSegment();
	Npc.EndScriptMove();
	Npc.ReleaseScriptBody(TEXT("CineCleanup"));
	if (Cine != nullptr && (Cine->SpawnFlags & GCineSfIgnoreNpcCollision) != 0)
	{
		if (const FElysiumNpc* Troika = Npc.AsNpc())
		{
			Npc.SetIgnoreCharacterCollision(Troika->NpcFlags.Has(EElysiumNpcFlag::NAV_IGNORE_NPC));
		}
	}
	if (bBeatRan)
	{
		Npc.ResetAnimToIdle();
	}

	// `m_hCine = -1`, `SetTarget(npc, NULL)`, `m_pGoalEnt = 0`.
	Npc.ScriptOwner = FElysiumEntityHandle::Invalid();
	Npc.SetTarget(FElysiumEntityHandle::Invalid());
	Npc.BaseScheduleHost.GoalEnt = FElysiumEntityHandle::Invalid();

	// `m_lifeState != LIFE_DYING`. SEAM: the port carries no NPC `m_lifeState` word (its death
	// transaction is `FElysiumNpcBase::CommitDeath`), so the dying arm — health 0, not-solid,
	// `SetState(DEAD)`, the corpse bounds — is not reached from here.
	// With `m_iszPlay` and `m_sequenceStarted`, retail puts the body at the played sequence's bone 0
	// (spawnflag `0x2000`: `MoveToBoneOriginAngles("Bip01")`; spawnflag `0x80` skips the placement).
	// SEAM: the animation driver exposes no played-clip root, so the body stays where it stands.
	(void)GCineSfLeaveCorpsePose;
	if (Npc.Health < 1)
	{
		Npc.RequestIdealStateRetail(GNpcStateDead, GCineCleanupDeadLine);
		Npc.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);   // SetCondition(0x4c)
	}
	else
	{
		Npc.RequestIdealStateRetail(GNpcStateIdle, GCineCleanupIdleLine);
	}
	Npc.SpawnFlags &= ~GNpcSfWaitForScript;
}

void FElysiumScriptedSequence::MakeNpcOblivious(FElysiumNpcBase& Npc)
{
	// `0x1026d130`, whole.
	ElysiumNpcEnemy::SetEnemy(Npc, FElysiumEntityHandle::Invalid());
	Npc.DisconnectFromSquad();
	++Npc.ObliviousCount;
}

void FElysiumScriptedSequence::ReleaseNpcOblivious(FElysiumNpcBase& Npc)
{
	// `0x10007ea0`, whole.
	Npc.ObliviousCount = FMath::Max(0, Npc.ObliviousCount - 1);
	Npc.ReconnectToSquad();
}

void FElysiumScriptedSequence::ScriptHiddenWarning(FElysiumNpcBase& Npc) const
{
	// `0x101a77a0`: `m_bCineScriptHidden = 1`, then the block of `DevWarning`s.
	Npc.bCineScriptHidden = true;
	UE_LOG(LogElysiumSeq, Warning, TEXT("Attempting to play a scripted sequence (%s) on a hidden NPC (%s). "
		"Performing voodoo flag magic."), *CineDebugName(*this), *CineDebugName(Npc));
}

// `CanInterrupt` `0x101a8930`.
bool FElysiumScriptedSequence::CanInterrupt() const
{
	if (!bInterruptable)
	{
		return false;
	}
	FElysiumNpcBase* Npc = TargetNpc();
	return Npc != nullptr && Npc->IsAlive();   // slot 158
}

// `CanOverride` `0x101a8ac0`, asked of the cine an NPC already holds.
bool FElysiumScriptedSequence::CanOverride() const
{
	FElysiumEntity* Next = World != nullptr ? World->Resolve(NextCine) : nullptr;
	if (Next == nullptr)
	{
		return true;
	}
	if (!NextScript.IsEmpty())
	{
		UE_LOG(LogElysiumSeq, Log, TEXT("%s is specified as the 'Next Script' and cannot be kicked out "
			"of the queue"), *CineDebugName(*Next));
		return false;
	}
	// The QUEUED cine's spawnflag `0x200`, not this one's.
	if ((Next->SpawnFlags & GCineSfPriority) == 0)
	{
		return true;
	}
	UE_LOG(LogElysiumSeq, Log, TEXT("%s is a priority script and cannot be kicked out of the queue"),
		*CineDebugName(*Next));
	return false;
}

// `AllowInterrupt` `0x101a8890`.
void FElysiumScriptedSequence::AllowInterrupt(bool bAllow)
{
	if ((SpawnFlags & GCineSfNoInterrupt) != 0)
	{
		return;
	}
	if (FElysiumNpcBase* Npc = TargetNpc())
	{
		if (!bInterruptable)
		{
			if (bAllow)
			{
				ReleaseNpcOblivious(*Npc);
			}
		}
		else if (!bAllow)
		{
			MakeNpcOblivious(*Npc);
			bInterruptable = false;
			return;
		}
	}
	bInterruptable = bAllow;
}

void FElysiumScriptedSequence::FireScriptEvent(int32 Index)
{
	static const FName Names[] = {
		TEXT("OnScriptEvent01"), TEXT("OnScriptEvent02"), TEXT("OnScriptEvent03"), TEXT("OnScriptEvent04"),
		TEXT("OnScriptEvent05"), TEXT("OnScriptEvent06"), TEXT("OnScriptEvent07"), TEXT("OnScriptEvent08"),
	};
	if (Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(Names)))
	{
		FireOutput(Names[Index], LastInputActivator);
	}
}

// --- The start gate -------------------------------------------------------------------------------

// `DelayStart` `0x101a8cf0`.
void FElysiumScriptedSequence::DelayStart(bool bIncrement)
{
	if (World == nullptr)
	{
		return;
	}
	TArray<FElysiumEntityHandle> Named;
	World->ForEachNamed(TargetName, [&Named](FElysiumEntity& Candidate) { Named.Add(Candidate.Handle); });
	const double Now = CineNow(*this);
	for (const FElysiumEntityHandle& Each : Named)
	{
		FElysiumEntity* Entity = World->Resolve(Each);
		// The literal classname `scripted_sequence` — an `aiscripted_sequence` sharing the name is
		// skipped, and so is any other entity.
		const FString Classname = Entity != nullptr && Entity->Def != nullptr ? Entity->Def->Classname
			: FString();
		FElysiumNpcBase* AsBase = Entity != nullptr ? Entity->AsNpcBase() : nullptr;
		FElysiumScriptedSequence* Cine = AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
		if (Cine == nullptr || !Classname.Equals(TEXT("scripted_sequence"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (!bIncrement)
		{
			--Cine->Delay;
			if (Cine->Delay < 1)
			{
				Cine->Delay = 0;
				Cine->StartTime = Now + GCineStartDelaySeconds;
			}
		}
		else
		{
			++Cine->Delay;
		}
	}
}

// `IsTimeToStart` `0x101a7540`.
bool FElysiumScriptedSequence::IsTimeToStart() const
{
	return Delay < 1 && StartTime <= CineNow(*this);
}

// `0x101a8130`.
FElysiumScriptedSequence* FElysiumScriptedSequence::LinkedSequence() const
{
	if (World == nullptr || LinkedSequenceName.IsEmpty())
	{
		return nullptr;
	}
	FElysiumEntity* Hit = World->FindByName(LinkedSequenceName);
	FElysiumNpcBase* AsBase = Hit != nullptr ? Hit->AsNpcBase() : nullptr;   // `+0x94` non-null
	return AsBase != nullptr ? AsBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;   // RTDynamicCast
}

// `StartScript` `0x101a81a0`.
void FElysiumScriptedSequence::StartScript()
{
	if (FElysiumScriptedSequence* Linked = LinkedSequence())
	{
		if (FElysiumNpcBase* LinkedNpc = Linked->TargetNpc())
		{
			LinkedNpc->TaskComplete(false);
			if (Linked->LinkedSequence() != this)
			{
				Linked->StartScript();
			}
			if (Linked->StartSequence(*LinkedNpc, Linked->Play, true)
				&& Linked->Phase == EBeatPhase::WaitForScript)
			{
				// The port's stand-in: the linked NPC's own `TASK_WAIT_FOR_SCRIPT` was completed above.
				Linked->Phase = EBeatPhase::Play;
				Linked->NpcScriptState = GScriptPlaying;
			}
			if (LinkedNpc->bSequenceFinished)
			{
				LinkedNpc->ClearSchedule();
			}
			// `m_flPlaybackRate = 1.0` — the body's playback rate is the clip segment's own here.
		}
	}
	FireOutput(FName(TEXT("OnBeginSequence")), LastInputActivator);
}

// `SequenceDone` `0x101a8460`.
void FElysiumScriptedSequence::SequenceDone(FElysiumNpcBase& Npc)
{
	Diagnostic(FString::Printf(TEXT("Sequence %s targeting %s is done"), *CineDebugName(*this),
		*CineDebugName(Npc)));
	FElysiumEntity* Next = World != nullptr ? World->Resolve(NextCine) : nullptr;
	if (PostIdle.IsEmpty() || Next != nullptr)
	{
		Finish(Npc);
	}
	else
	{
		NpcScriptState = GScriptPostIdle;
		StartSequence(Npc, PostIdle, false);   // slot 584
		if (Phase != EBeatPhase::None)
		{
			Phase = EBeatPhase::PostIdle;
		}
	}
	// `OnEndSequence`, UNCONDITIONALLY, with `m_hLastInputActivator`.
	FireOutput(FName(TEXT("OnEndSequence")), LastInputActivator);
}

// `Finish` `0x101a8640`.
void FElysiumScriptedSequence::Finish(FElysiumNpcBase& Npc)
{
	FElysiumEntity* Next = World != nullptr ? World->Resolve(NextCine) : nullptr;
	if (!PostIdle.IsEmpty() && (SpawnFlags & GCineSfHoldPostIdle) != 0 && Next == nullptr)
	{
		Diagnostic(FString::Printf(TEXT("Post Idle %s finished"), *PostIdle));
		NpcScriptState = GScriptPostIdle;
		StartSequence(Npc, PostIdle, false);
		if (Phase != EBeatPhase::None)
		{
			Phase = EBeatPhase::PostIdleHeld;
		}
		return;
	}
	if ((SpawnFlags & GCineSfRepeatable) == 0)
	{
		ArmCineThink(EThinkFunction::SubRemove, CineNow(*this) + GCineRemoveDelaySeconds);
	}
	CineCleanup(Npc);
	FixScriptNPCSchedule(Npc);   // slot 586, virtual
	Next = World != nullptr ? World->Resolve(NextCine) : nullptr;
	FElysiumNpcBase* NextBase = Next != nullptr ? Next->AsNpcBase() : nullptr;
	FElysiumScriptedSequence* NextCineEntity = NextBase != nullptr
		? NextBase->AsSpecies<FElysiumScriptedSequence>() : nullptr;
	if (NextCineEntity != nullptr && (NextCineEntity != this || (SpawnFlags & GCineSfRepeatable) != 0))
	{
		NextCineEntity->SetTarget(Npc.Handle);
		NextCineEntity->PossessEntity();   // slot 583, virtual
	}
}

// --- Inputs ---------------------------------------------------------------------------------------

// `InputBeginSequence` `0x101a7390`.
void FElysiumScriptedSequence::InputBeginSequence(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	const double Now = CineNow(*this);
	const double PendingThink = ThinkFunction != EThinkFunction::None ? CineThinkAt : ELYSIUM_NEVER_THINK;
	if (PendingThink < ELYSIUM_NEVER_THINK && PendingThink > Now)
	{
		UE_LOG(LogElysiumSeq, Log, TEXT("***WARNING*** Called BeginSequence for '%s' before it had a chance "
			"to think. Still another %f seconds before we think. Try delaying your BeginSequence call."),
			*CineDebugName(*this), PendingThink - Now);
		StartTime = PendingThink + GCineStartDelaySeconds;
		return;
	}
	StartTime = Now + GCineStartDelaySeconds;
	if (!FindEntityWrap())
	{
		// Not found: nothing — no think, no output (`0x101a7412 JZ 0x101a74a9`).
		return;
	}
	FElysiumNpcBase* Npc = TargetNpc();
	if (Npc == nullptr)
	{
		ArmCineThink(EThinkFunction::CineThink, Now);
		return;
	}
	const int32 State = ScriptStateOf(*Npc);
	if (State != GScriptPlaying && State != GScriptPostIdle)
	{
		return;
	}
	if (Npc->CanPlaySequence(FCanOverrideState(), 1) != 0)
	{
		PossessEntity();
	}
}

// `InputMoveToPosition` `0x101a72b0`: the standing `m_hTargetEnt`, no search.
void FElysiumScriptedSequence::InputMoveToPosition(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	const double Now = CineNow(*this);
	FElysiumNpcBase* Npc = TargetNpc();
	if (Npc == nullptr)
	{
		ArmCineThink(EThinkFunction::CineThink, Now);
	}
	else
	{
		const int32 State = ScriptStateOf(*Npc);
		if (State != GScriptPlaying && State != GScriptPostIdle)
		{
			return;
		}
		if (Npc->CanPlaySequence(FCanOverrideState(), 1) == 0)
		{
			return;
		}
		PossessEntity();
	}
	StartTime = Now + GCineStartTimeFar;
}

// `InputCancelSequence` `0x101a7500`: THIS cine only, no output.
void FElysiumScriptedSequence::InputCancelSequence(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	UE_LOG(LogElysiumSeq, Verbose, TEXT("InputCancelScript: Cancelling script '%s'"), *Play);
	ScriptEntityCancel(*this);
}

void FElysiumScriptedSequence::InputKill(const FElysiumInputArgs& Args)
{
	RecordInput(Args);
	RemoveSelf();
}

bool FElysiumScriptedSequence::CancelScriptedSequenceForDialogue(const FElysiumEntityHandle& NpcHandle)
{
	// The NPC-side callers (`EnterGrappleState` `0x102b5c00`, `ForceScheduleChange` `0x102ae490`, the
	// dialogue opener) run `CancelScript` (`0x101a8c30`) on their `m_hCine`.
	FElysiumEntity* NpcEntity = World != nullptr ? World->Resolve(NpcHandle) : nullptr;
	const bool bOwner = NpcEntity != nullptr && NpcEntity->ScriptOwner == Handle;
	CancelScript();
	return bOwner;
}

void FElysiumScriptedSequence::AddInputs(FElysiumClassDesc& D, const TCHAR* RetailClass)
{
	if (FCString::Strcmp(RetailClass, TEXT("CCineNPC")) != 0)
	{
		return;
	}
	using FS = FElysiumScriptedSequence;
	D.Input(TEXT("BeginSequence"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputBeginSequence(Args); });
	D.Input(TEXT("MoveToPosition"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputMoveToPosition(Args); });
	D.Input(TEXT("CancelSequence"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputCancelSequence(Args); });
	// The port's `Kill` runs no slot 180; a director's states the `UTIL_Remove` pair itself.
	D.Input(TEXT("Kill"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FS&>(*E.AsNpcBase()).InputKill(Args); });
}

// --- The beat stand-in (named modernization, see the header) --------------------------------------

FElysiumNpcBase* FElysiumScriptedSequence::BeatNpc() const
{
	FElysiumNpcBase* Npc = TargetNpc();
	return Npc != nullptr && Npc->ScriptOwner == Handle ? Npc : nullptr;
}

void FElysiumScriptedSequence::ClaimBody(FElysiumNpcBase& Npc)
{
	Npc.ClaimScriptBody(TEXT("scripted sequence beat"));
	if ((SpawnFlags & GCineSfIgnoreNpcCollision) != 0 && Npc.AsNpc() != nullptr)
	{
		// The collision view of the `NAV_IGNORE_NPC` bit `PossessEntity` just ORed in
		// (`CBaseAnimating::IsIgnoreCollisionEntity` reads it): the Unreal motor ignores characters.
		Npc.SetIgnoreCharacterCollision(true);
	}
}

void FElysiumScriptedSequence::BeginBeat(FElysiumNpcBase& Npc)
{
	EndBeat();
	ClaimBody(Npc);
	const bool bTravels = NpcScriptState == GScriptWalkToMark || NpcScriptState == GScriptRunToMark
		|| NpcScriptState == GScriptCustomMoveToMark || MoveTo == 5;
	Phase = bTravels ? EBeatPhase::Travel : EBeatPhase::WaitForScript;
	bResumeTravel = bTravels;   // the move is issued on the first beat think, as the NPC's think would
	BeatThinkAt = CineNow(*this);
	RescheduleThink();
}

void FElysiumScriptedSequence::EndBeat()
{
	Phase = EBeatPhase::None;
	bRestartBeat = false;
	BeatThinkAt = ELYSIUM_NEVER_THINK;
	BeatClipEndsAt = 0.0;
	bTravelled = false;
	bResumeTravel = false;
	bPreIdleStarted = false;
	RescheduleThink();
}

bool FElysiumScriptedSequence::StartTravel(FElysiumNpcBase& Npc)
{
	EElysiumScriptGait Gait = EElysiumScriptGait::Walk;
	switch (MoveTo)
	{
	case 1: Gait = EElysiumScriptGait::Walk; break;
	case 2: Gait = EElysiumScriptGait::Run; break;
	case 3: Gait = EElysiumScriptGait::Custom; break;
	case 5: Gait = EElysiumScriptGait::Face; break;
	default: return false;
	}
	return Npc.BeginScriptMove(Origin, Angles, Gait, CustomMove);
}

void FElysiumScriptedSequence::PlaceOnMark(FElysiumNpcBase& Npc) const
{
	if (MoveTo != 5)
	{
		Npc.SetRuntimeOrigin(Origin);
	}
	Npc.SetRuntimeAngles(Angles);
}

float FElysiumScriptedSequence::PlayBeatClip(FElysiumNpcBase& Npc, const FString& Clip, bool bLoop)
{
	float Seconds = 0.f;
	if (Clip.IsEmpty() || !Npc.PlayAnimSegment(BeatSegment(Clip, bLoop), &Seconds))
	{
		return -1.f;
	}
	return Seconds;
}

void FElysiumScriptedSequence::Arrive(FElysiumNpcBase& Npc, double Now)
{
	// `TASK_PLANT_ON_SCRIPT` / `TASK_FACE_SCRIPT` are the motor's arrival and facing; then the move
	// schedules' `TASK_ENABLE_SCRIPT` (100) -> `DelayStart(0)`. The WAIT/FACE schedules carry none.
	if (MoveTo == 1 || MoveTo == 2 || MoveTo == 3)
	{
		DelayStart(false);
	}
	Phase = EBeatPhase::WaitForScript;
	BeatThinkAt = Now;
}

void FElysiumScriptedSequence::TickBeat(double Now)
{
	FElysiumNpcBase* Npc = BeatNpc();
	if (Npc == nullptr)
	{
		// The NPC left this cine (another cine's cleanup, a removal): its schedule is no longer ours.
		EndBeat();
		return;
	}
	if (bRestartBeat)
	{
		// The first think after a restore: the body claim and the current phase start again (the
		// travel from wherever the body now stands, the pose clips from their first frame). No output
		// fires twice: `OnBeginSequence` belongs to the wait phase and `OnEndSequence` to `SequenceDone`.
		bRestartBeat = false;
		ClaimBody(*Npc);
		switch (Phase)
		{
		case EBeatPhase::Travel:
			bResumeTravel = true;
			break;
		case EBeatPhase::WaitForScript:
			bPreIdleStarted = false;
			break;
		case EBeatPhase::Play:
		case EBeatPhase::PostIdle:
		case EBeatPhase::PostIdleHeld:
		{
			const bool bPlay = Phase == EBeatPhase::Play;
			const float Seconds = PlayBeatClip(*Npc, bPlay ? Play : PostIdle, /*bLoop=*/!bPlay);
			BeatClipEndsAt = Now + FMath::Max(0.f, Seconds);
			BeatThinkAt = FMath::Max(Now + GBeatTickSeconds, BeatClipEndsAt);
			return;
		}
		default:
			break;
		}
	}
	if (Npc->IsDead())
	{
		// Fallback for a death that reached no `Event_Killed` (`FElysiumNpc::OnKilled` runs
		// `CancelScript` at once): the same `ScriptEntityCancel` effects, without the SCRIPT-state gate
		// a dead mind no longer passes.
		CineCleanup(*Npc);
		Delay = 0;
		return;
	}
	switch (Phase)
	{
	case EBeatPhase::Travel:
	{
		if (bResumeTravel)
		{
			bResumeTravel = false;
			bTravelled = StartTravel(*Npc);
			if (!bTravelled)
			{
				// No motor, no route, or a bodiless record: placed on the mark (the port's modernization;
				// retail's `_FAILED` schedule idles and retries the route forever).
				PlaceOnMark(*Npc);
				Arrive(*Npc, Now);
				break;
			}
			BeatThinkAt = Now + GBeatTickSeconds;
			break;
		}
		const EElysiumScriptMove Status = Npc->AdvanceScriptMove();
		if (Status == EElysiumScriptMove::Moving)
		{
			BeatThinkAt = Now + GBeatTickSeconds;
			break;
		}
		Npc->EndScriptMove();
		if (Status != EElysiumScriptMove::Arrived)
		{
			UE_LOG(LogElysiumSeq, Log, TEXT("%s travel did not reach the mark; placing %s on it"),
				*DebugString(), *Npc->DebugString());
			PlaceOnMark(*Npc);
		}
		Arrive(*Npc, Now);
		break;
	}
	case EBeatPhase::WaitForScript:
	{
		if (!bPreIdleStarted)
		{
			// `TASK_WAIT_FOR_SCRIPT` start: the pre-idle through slot 584, else `ACT_IDLE` unless the
			// NPC custom-moved here.
			bPreIdleStarted = true;
			if (!PreIdle.IsEmpty())
			{
				StartSequence(*Npc, PreIdle, false);
			}
			else if (NpcScriptState != GScriptCustomMoveToMark && !IsTimeToStart())
			{
				// `SetActivity(ACT_IDLE)`. Skipped when the start gate is already open: retail's play
				// replaces the idle in the same think, before a frame is drawn, so the port does not
				// flash a stance idle it would overwrite at once.
				Npc->ReleaseAnimSegment();
				Npc->ResetAnimToIdle();
			}
		}
		if (!IsTimeToStart())
		{
			BeatThinkAt = Now + GBeatTickSeconds;
			break;
		}
		StartScript();
		if (Phase != EBeatPhase::WaitForScript)
		{
			break;
		}
		StartSequence(*Npc, Play, true);   // slot 584
		if (Phase != EBeatPhase::WaitForScript)
		{
			break;   // an empty play ran `SequenceDone` already
		}
		// `TASK_PLAY_SCRIPT` start: `MOVETYPE_NONE`, `m_scriptState = 0`.
		Phase = EBeatPhase::Play;
		NpcScriptState = GScriptPlaying;
		Npc->SetMoveType(GMoveTypeNone, 0);
		BeatThinkAt = FMath::Max(Now + GBeatTickSeconds, BeatClipEndsAt);
		break;
	}
	case EBeatPhase::Play:
		if (Now < BeatClipEndsAt)
		{
			BeatThinkAt = BeatClipEndsAt;
			break;
		}
		SequenceDone(*Npc);
		if (Phase == EBeatPhase::PostIdle || Phase == EBeatPhase::PostIdleHeld)
		{
			BeatThinkAt = FMath::Max(Now + GBeatTickSeconds, BeatClipEndsAt);
		}
		break;
	case EBeatPhase::PostIdle:
	case EBeatPhase::PostIdleHeld:
	{
		// `TASK_PLAY_SCRIPT_POST_IDLE` (99): hold while unfinished and no `m_hNextCine`, else `Finish`.
		const bool bNext = World != nullptr && World->Resolve(NextCine) != nullptr;
		if (Now < BeatClipEndsAt && !bNext)
		{
			BeatThinkAt = BeatClipEndsAt;
			break;
		}
		Finish(*Npc);
		if (Phase == EBeatPhase::PostIdle || Phase == EBeatPhase::PostIdleHeld)
		{
			BeatThinkAt = FMath::Max(Now + GBeatTickSeconds, BeatClipEndsAt);
		}
		break;
	}
	default:
		break;
	}
	// Any transition above that left the beat running without a next tick (an empty play that ran
	// `SequenceDone` into the post-idle, a `Finish` that holds it) polls again at the beat cadence or
	// when the clip it started runs out, whichever is later.
	if (Phase != EBeatPhase::None && BeatThinkAt >= ELYSIUM_NEVER_THINK)
	{
		BeatThinkAt = FMath::Max(Now + GBeatTickSeconds, BeatClipEndsAt);
	}
}

// --- Persistence and presentation -----------------------------------------------------------------

const TCHAR* FElysiumScriptedSequence::SaveBlockReason() const
{
	// **Named modernization — no save mid-beat.** Retail saves the datamap words and the NPC's
	// scripted schedule and resumes; the port's beat stand-in (travel request, montage run) cannot be
	// rebuilt from a payload, so a save is refused while a beat runs, a held post-idle included.
	return Phase != EBeatPhase::None ? TEXT("a scripted sequence is active") : nullptr;
}

void FElysiumScriptedSequence::Serialize(FElysiumSaveArchive& Ar)
{
	// The `CAI_BaseNPC` record first (retail's datamap chain), then the installed think, the input
	// activator, `m_scriptState` and the possession: the beat's phase and the NPC this director owns
	// (`m_hCine` on the NPC, which the base snapshot does not carry). The six saved words, `m_iDelay`,
	// `m_startTime` and `m_hNextCine` ride the registry's SAVE walk. A map snapshot is taken at every
	// level teardown, beat or no beat — only an explicit save refuses mid-beat.
	FElysiumNpcBase::Serialize(Ar);
	uint8 Function = static_cast<uint8>(ThinkFunction);
	Ar << Function;
	Ar << CineThinkAt;
	int32 ActivatorIndex = LastInputActivator.IsSet() ? LastInputActivator.Index : INDEX_NONE;
	Ar << ActivatorIndex;
	Ar << NpcScriptState;
	uint8 SavedPhase = static_cast<uint8>(Phase);
	Ar << SavedPhase;
	const FElysiumNpcBase* Owned = Ar.IsLoading() ? nullptr : BeatNpc();
	int32 OwnedIndex = Owned != nullptr ? Owned->Handle.Index : INDEX_NONE;
	Ar << OwnedIndex;
	if (Ar.IsLoading())
	{
		ThinkFunction = static_cast<EThinkFunction>(Function);
		LastInputActivator = (ActivatorIndex == INDEX_NONE || World == nullptr)
			? FElysiumEntityHandle::Invalid()
			: FElysiumEntityHandle(ActivatorIndex, World->GetEpoch());
		EndBeat();
		if (SavedPhase != static_cast<uint8>(EBeatPhase::None) && OwnedIndex != INDEX_NONE)
		{
			Phase = static_cast<EBeatPhase>(SavedPhase);
			RestoredNpcIndex = OwnedIndex;
			bRestartBeat = true;
		}
	}
}

void FElysiumScriptedSequence::OnPostRestore(FElysiumEntityWorld& InWorld)
{
	FElysiumNpcBase::OnPostRestore(InWorld);
	if (!bRestartBeat)
	{
		return;
	}
	// Re-stamp the possession: `m_hTargetEnt` on this director, `m_hCine` and `m_pGoalEnt` on the NPC.
	FElysiumEntity* Entity = RestoredNpcIndex != INDEX_NONE
		? InWorld.Resolve(FElysiumEntityHandle(RestoredNpcIndex, InWorld.GetEpoch())) : nullptr;
	FElysiumNpcBase* Npc = Entity != nullptr ? Entity->AsNpcBase() : nullptr;
	RestoredNpcIndex = INDEX_NONE;
	if (Npc == nullptr || Npc->IsDead())
	{
		bRestartBeat = false;
		EndBeat();
		return;
	}
	SetTarget(Npc->Handle);
	Npc->ScriptOwner = Handle;
	Npc->BaseScheduleHost.GoalEnt = Handle;
	BeatThinkAt = InWorld.NowSeconds();
	RescheduleThink();
}

void FElysiumScriptedSequence::PreloadForActivation()
{
	FElysiumEntity* Npc = World != nullptr && !TargetEntity.StartsWith(TEXT("!")) && !TargetEntity.IsEmpty()
		? World->FindByName(TargetEntity) : nullptr;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FString ProxyStem;
	if (Npc == nullptr && Embodiment != nullptr
		&& TargetEntity.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase))
	{
		if (const FElysiumPlayer* Player = World->FindPlayer())
		{
			ProxyStem = FPaths::GetBaseFilename(Player->Model).ToLower();
		}
	}
	auto Preload = [Npc, Embodiment, &ProxyStem](const FString& Clip)
	{
		if (Clip.IsEmpty())
		{
			return;
		}
		if (Npc != nullptr)
		{
			Npc->PreloadAnimClip(Clip);
		}
		else if (Embodiment != nullptr && !ProxyStem.IsEmpty())
		{
			Embodiment->PreloadNpcClipForModel(ProxyStem, /*bPlayerMaterial=*/false, Clip);
		}
	};
	Preload(PreIdle);
	Preload(Play);
	Preload(PostIdle);
	Preload(CustomMove);
	if (MoveTo == 1)
	{
		Preload(TEXT("walk"));
	}
	else if (MoveTo == 2)
	{
		Preload(TEXT("run"));
	}
}

void FElysiumScriptedSequence::Diagnostic(const FString& Line)
{
	UE_LOG(LogElysiumSeq, Verbose, TEXT("%s"), *Line);
	Diagnostics.Add(Line);
	if (Diagnostics.Num() > 32)
	{
		Diagnostics.RemoveAt(0);
	}
}

void FElysiumScriptedSequence::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	static const TCHAR* const PhaseNames[] = {
		TEXT("none"), TEXT("travel"), TEXT("wait for script"), TEXT("play"), TEXT("post-idle"),
		TEXT("post-idle (held)") };
	Out.Emplace(TEXT("Target NPC"), TargetEntity.IsEmpty() ? TEXT("(none)") : TargetEntity);
	const FElysiumNpcBase* Npc = TargetNpc();
	Out.Emplace(TEXT("Resolved"), Npc != nullptr ? Npc->DebugString() : TEXT("no"));
	Out.Emplace(TEXT("Running"), PhaseNames[static_cast<int32>(Phase)]);
	Out.Emplace(TEXT("Script state"), FString::FromInt(NpcScriptState));
	Out.Emplace(TEXT("Move to"), FString::FromInt(MoveTo));
	Out.Emplace(TEXT("Radius"), FString::Printf(TEXT("%.0f"), Radius));
	Out.Emplace(TEXT("Delay"), FString::FromInt(Delay));
	Out.Emplace(TEXT("Start time"), FString::Printf(TEXT("%.3f"), StartTime));
	Out.Emplace(TEXT("Think"), ThinkFunction == EThinkFunction::CineThink ? TEXT("CineThink")
		: ThinkFunction == EThinkFunction::SubRemove ? TEXT("SUB_Remove") : TEXT("none"));
	Out.Emplace(TEXT("Interruptable"), bInterruptable ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Next cine"), World != nullptr && World->Resolve(NextCine) != nullptr
		? World->DescribeHandle(NextCine) : TEXT("(none)"));
	for (const TPair<const TCHAR*, const FString*> F : {
			TPair<const TCHAR*, const FString*>(TEXT("Pre-idle"), &PreIdle),
			TPair<const TCHAR*, const FString*>(TEXT("Play"), &Play),
			TPair<const TCHAR*, const FString*>(TEXT("Post-idle"), &PostIdle),
			TPair<const TCHAR*, const FString*>(TEXT("Custom move"), &CustomMove),
			TPair<const TCHAR*, const FString*>(TEXT("Next script"), &NextScript) })
	{
		if (!F.Value->IsEmpty())
		{
			Out.Emplace(F.Key, *F.Value);
		}
	}
	Out.Emplace(TEXT("Activate precaches"), FString::JoinBy(ActivatePrecacheLog, TEXT(","),
		[](const FSequenceSoundPrecache& Row) { return Row.SequenceName; }));
	Out.Emplace(TEXT("Activate diagnostics"), FString::FromInt(ActivateDiagnostics.Num()));
	Out.Emplace(TEXT("Diagnostics"), FString::Join(Diagnostics, TEXT(" | ")));
}

float FElysiumScriptedSequence::RunSpecialIdleActivity(double Now)
{
	(void)Now;
	return -1.0f;
}

bool FElysiumScriptedSequence::IsBodyVisible() const
{
	return false;
}

float FElysiumScriptedSequence::PlayActivity(const FString& Activity)
{
	(void)Activity;
	return -1.0f;
}
