#include "Substrate/ElysiumAiScriptedSequence.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAiSeq, Log, All);

// `aiscripted_sequence` — `CCineAI`, story 5 fold A3. Each body is the retail body at the address its
// comment names (`vtmb_code`); the class's shared behaviour is `CCineNPC`'s
// (`ElysiumScriptedSequence.cpp`).

namespace
{
	constexpr int32 GAiScriptWait = 1;
	constexpr int32 GAiScriptWalkToMark = 4;
	constexpr int32 GAiScriptRunToMark = 5;
	constexpr int32 GAiScriptCustomMoveToMark = 6;
	constexpr int32 GAiNpcStateScript = 4;
	constexpr int32 GAiPossessLine = 0x554;   // the selector trace `0x101a9080` stamps
	constexpr int32 GAiFlagOnGround = 0x1;    // `FL_ONGROUND`
	constexpr uint32 GAiNavIgnoreNpc = 0x40u;
	constexpr int32 GAiSfIgnoreNpcCollision = 0x1000;

	FString AiDebugName(const FElysiumEntity& Entity)
	{
		if (!Entity.TargetName.IsEmpty())
		{
			return Entity.TargetName;
		}
		return Entity.Class != nullptr ? Entity.Class->ClassName.ToString() : FString();
	}
}

const FElysiumNpcClass* FElysiumAiScriptedSequence::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 583: `0x101a9080`. `CCineNPC`'s body without the queue, without the `m_hNextCine` clear and
// without `DelayStart`.
void FElysiumAiScriptedSequence::PossessEntity()
{
	FElysiumNpcBase* Npc = TargetNpc();
	if (Npc == nullptr)
	{
		return;
	}
	if (!Npc->GetMind().IsAdmitted())
	{
		// The same warning block as `CCineNPC`'s, less its "has not run it's AI yet" line.
		Diagnostic(FString::Printf(TEXT("scripted_sequence %s is targeting an entity (%s)"),
			*AiDebugName(*this), *AiDebugName(*Npc)));
	}
	if (Npc->bHidden)
	{
		ScriptHiddenWarning(*Npc);   // 0x101a77a0
	}
	if (!bInterruptable)
	{
		MakeNpcOblivious(*Npc);   // 0x1026d130
	}
	// A standing cine is OVERWRITTEN: `m_pGoalEnt`, `m_hCine`, `SetTarget(npc, this)`.
	if (FElysiumScriptedSequence* Previous = Npc->ResolveCine(); Previous != nullptr && Previous != this)
	{
		// The port's stand-in of the previous cine's scripted schedule stops with it (retail's NPC
		// simply reselects under the new `m_hCine`).
		Previous->EndBeat();
	}
	Npc->BaseScheduleHost.GoalEnt = Handle;
	Npc->ScriptOwner = Handle;
	Npc->SetTarget(Handle);
	SavedMoveType = Npc->RetailMoveType;
	SavedMoveCollide = Npc->RetailMoveCollide;
	SavedSolid = Npc->RetailSolidType;
	SavedSolidFlags = static_cast<int32>(Npc->RetailSolidFlags);
	SavedEffects = 0;   // SEAM: no port `m_fEffects` word
	if (FElysiumNpc* Troika = Npc->AsNpc())
	{
		Troika->ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);   // slot 614
		SavedTroikaFlags = static_cast<int32>(Troika->NpcFlags.RawWord1());
		if ((SpawnFlags & GAiSfIgnoreNpcCollision) != 0)
		{
			Troika->NpcFlags.AssignAiFlagsWord(Troika->NpcFlags.RawWord1() | GAiNavIgnoreNpc);
		}
	}
	switch (MoveTo)
	{
	case 0:
	case 5:
		NpcScriptState = GAiScriptWait;
		break;
	case 1:
		NpcScriptState = GAiScriptWalkToMark;
		break;
	case 2:
		NpcScriptState = GAiScriptRunToMark;
		break;
	case 3:
		NpcScriptState = GAiScriptCustomMoveToMark;
		break;
	case 4:
		// Placed inline, then WAIT and `RemoveFlag(FL_ONGROUND)` — no fall-through here.
		PlaceOnMark(*Npc);
		NpcScriptState = GAiScriptWait;
		Npc->Flags &= ~GAiFlagOnGround;
		break;
	default:
		UE_LOG(LogElysiumAiSeq, Log, TEXT("aiscript:  invalid Move To Position value!"));
		break;
	}
	Diagnostic(FString::Printf(TEXT("\"%s\" found and used"), *AiDebugName(*Npc)));
	// `m_NPCState` is read BEFORE the ideal-state write: an NPC already in SCRIPT would not reselect,
	// so the script schedule is installed on it directly (local `0x2e` through `0x10280de0`).
	const int32 StateBefore = Npc->NpcStateRetail();
	Npc->RequestIdealStateRetail(GAiNpcStateScript, GAiPossessLine);
	if (StateBefore == GAiNpcStateScript)
	{
		Npc->ChangeSchedule(ScheduleAiScript);
	}
	// The port's stand-in for the scripted schedule `0x2e` selects (see `ElysiumScriptedSequence.h`).
	BeginBeat(*Npc);
}

// Slot 584: `0x101a9510`.
bool FElysiumAiScriptedSequence::StartSequence(FElysiumNpcBase& Npc, const FString& SequenceName,
	bool bCompleteOnEmpty)
{
	bSequenceStarted = true;
	if (SequenceName.IsEmpty() && bCompleteOnEmpty)
	{
		SequenceDone(Npc);
		return true;   // `CCineNPC`'s answers false here
	}
	const float Seconds = PlayBeatClip(Npc, SequenceName, /*bLoop=*/!bCompleteOnEmpty);
	if (Seconds < 0.f)
	{
		UE_LOG(LogElysiumAiSeq, Log, TEXT("%s: unknown aiscripted sequence \"%s\""), *AiDebugName(Npc),
			*SequenceName);
	}
	BeatClipEndsAt = (World != nullptr ? World->NowSeconds() : 0.0) + FMath::Max(0.f, Seconds);
	return true;
}

// Slot 585: `0x101a9060`.
bool FElysiumAiScriptedSequence::FCanOverrideState() const
{
	return true;
}

// Slot 586: `0x101a95d0`.
void FElysiumAiScriptedSequence::FixScriptNPCSchedule(FElysiumNpcBase& Npc)
{
	if (FinishSchedule != 0)
	{
		if (FinishSchedule == 1)
		{
			// Local `0x2a` in the `cai_basenpc` space: `SCHED_AMBUSH`, installed with NO clear.
			Npc.ChangeSchedule(ScheduleAmbush);
			return;
		}
		Diagnostic(TEXT("FixScriptNPCSchedule - no case!"));
	}
	Npc.ClearSchedule();
}
