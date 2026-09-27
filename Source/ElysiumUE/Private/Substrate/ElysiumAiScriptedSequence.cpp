#include "Substrate/ElysiumAiScriptedSequence.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"

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

// Slot 583: `0x101a9080` (`CCineAI::vfunc583`), 899 bytes. `CCineNPC`'s body (`0x101a7880`) without the
// queue arm (a standing cine is OVERWRITTEN), without the `m_hNextCine` clear and without
// `DelayStart`; case 4 writes its own state and drops `FL_ONGROUND`, and an out-of-range `m_fMoveTo`
// reaches a warning the sequence version has no equivalent for. Last, an NPC that was ALREADY in
// SCRIPT gets `SCHED_AISCRIPT` installed at once.
void FElysiumAiScriptedSequence::PossessEntity()
{
	FElysiumNpcBase* Npc = TargetNpc();   // 0x101a9087..0x101a90c0 m_hTargetEnt, +0x94
	if (Npc == nullptr)
	{
		return;                           // 0x101a9090 / 0x101a90b0 / 0x101a90ba / 0x101a90c8 -> 0x101a93fd
	}
	if (!Npc->BaseScheduleHost.bRanAi)    // 0x101a90ce m_bRanAI +0x1b4c, 0x101a90d7
	{
		// `DevMsg` (`[0x109f3630]`, held in EBX) per line: 0x101a90e4 stars, 0x101a90eb WARNING,
		// 0x101a90f2 GetDebugName(this) / 0x101a90fd "scripted sequence(%s)", 0x101a9104
		// GetDebugName(npc) / 0x101a910f "is targeting an entity(%s)", 0x101a9116 hidden, 0x101a911d
		// immediately, 0x101a9124 .1 second, 0x101a912b ScriptUnhide, 0x101a9132 scripted sequence,
		// 0x101a9139 programmer, 0x101a9140 WARNING, 0x101a9147 stars.
		NotRunAiWarning(*Npc, nullptr);   // 0x101a90d9..0x101a9147, the "has not run" line skipped
	}
	if (Npc->bHidden)                     // 0x101a914e 0x100b5190 / 0x101a9155
	{
		ScriptHiddenWarning(*Npc);        // 0x101a915a 0x101a77a0
	}
	if (!bInterruptable)                  // 0x101a915f +0x5f90 / 0x101a9167
	{
		MakeNpcOblivious(*Npc);           // 0x101a916b 0x1026d130
	}
	// A standing cine is OVERWRITTEN. The port's stand-in of the previous cine's scripted schedule
	// stops with it (retail's NPC simply reselects under the new `m_hCine`) — modernization bookkeeping.
	if (FElysiumScriptedSequence* Previous = Npc->ResolveCine(); Previous != nullptr && Previous != this)
	{
		Previous->EndBeat();
	}
	Npc->BaseScheduleHost.GoalEnt = Handle;   // 0x101a9170 npc m_pGoalEnt +0x5de8
	Npc->ScriptOwner = Handle;                // 0x101a917a..0x101a9180 npc m_hCine +0x5d74
	Npc->SetTarget(Handle);                   // 0x101a9188 SetTarget(npc, this)
	SavedMoveType = Npc->RetailMoveType;      // 0x101a9191 slot 94 -> 0x101a9197 +0x5f78
	SavedMoveCollide = Npc->RetailMoveCollide; // 0x101a91a1 slot 95 -> 0x101a91a7 +0x5f7c
	SavedSolid = Npc->RetailSolidType;        // 0x101a91b1 slot 92 -> 0x101a91b7 +0x5f80
	SavedSolidFlags = static_cast<int32>(Npc->RetailSolidFlags); // 0x101a91c1 slot 211 -> 0x101a91c7 +0x5f84
	// `+0x5f88 := npc m_fEffects (+0x19c)` (`0x101a91cd..0x101a91d3`). The NPC's word is the Troika
	// tier's `EffectsWord` (`ElysiumNpcPositions.inl`); a base-only NPC carries none and saves 0.
	const FElysiumNpc* EffectsNpc = Npc->AsNpc();
	SavedEffects = EffectsNpc != nullptr ? static_cast<int32>(EffectsNpc->EffectsWord) : 0;
	if (FElysiumNpc* Troika = Npc->AsNpc())   // 0x101a91d9 npc +0x98 / 0x101a91e1
	{
		Troika->ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);   // 0x101a91e7 slot 614
		SavedTroikaFlags = static_cast<int32>(Troika->NpcFlags.RawWord1());      // 0x101a91ed..0x101a91f3 +0x5f8c
		if ((SpawnFlags & GAiSfIgnoreNpcCollision) != 0)                           // 0x101a91f9 / 0x101a9202
		{
			Troika->NpcFlags.AssignAiFlagsWord(Troika->NpcFlags.RawWord1() | GAiNavIgnoreNpc); // 0x101a920c
		}
	}
	// 0x101a9212..0x101a9220: npc m_fEffects |= this director's `m_fEffects` (+0x19c). SEAM (spec 0003):
	// the director carries no effects word, so the OR adds nothing.
	switch (MoveTo)   // 0x101a9226 +0x5f60 / 0x101a922f JA / 0x101a9235 table 0x101a9404
	{
	case 0:
	case 5:
		NpcScriptState = GAiScriptWait;               // 0x101a923c..0x101a9245 state 1; 0x101a9240 0x1027f270(1) is empty
		break;
	case 1:
		NpcScriptState = GAiScriptWalkToMark;         // 0x101a9254..0x101a925d state 4, NO DelayStart; 0x101a9258 0x1027f270(4) empty
		break;
	case 2:
		NpcScriptState = GAiScriptRunToMark;          // 0x101a926c..0x101a9275 state 5; 0x101a9270 0x1027f270(5) empty
		break;
	case 3:
		NpcScriptState = GAiScriptCustomMoveToMark;   // 0x101a9284..0x101a928d state 6; 0x101a9288 0x1027f270(6) empty
		break;
	case 4:
		// `CCineNPC`'s teleport instruction for instruction (`0x101a7d89..0x101a7e6b`), so the shared
		// helper runs it: 0x101a92a9 slot 220 GetAbsOrigin(this) -> 0x101a92b2 slot 181 Teleport;
		// 0x101a92be 0x102e0b40(motor) yaw-speed hold; 0x101a92cd slot 221 our angles; 0x101a92df JZ
		// motor+0x28 clear skips the flip, 0x101a92f6 JNZ (C0: yaw < 180) FADD else FSUB 180; 0x101a9311
		// JNZ motor+0x1c != 180.0 -> 0x101a9323 0x102e0a80, else the direct +0x34 write; 0x101a9332
		// SetLocalAngularVelocity(vec3_angle); 0x101a933f..0x101a9344 EF_NOINTERP; 0x101a934a slot 221
		// on the NPC, 0x101a9368 slot 221 our yaw, 0x101a937e slot 64 SetAngles.
		TeleportToMark(*Npc);                         // 0x101a929c..0x101a937e
		NpcScriptState = GAiScriptWait;               // 0x101a9388 0x1027f270(1) empty / 0x101a9391 state 1
		Npc->Flags &= ~GAiFlagOnGround;               // 0x101a939b RemoveFlag(FL_ONGROUND)
		break;
	default:
		// `DevWarning(2, "aiscript:  invalid Move To Position value!")` through `0x109f3658`.
		Diagnostic(TEXT("aiscript:  invalid Move To Position value!"));   // 0x101a93a2..0x101a93a9
		break;
	}
	Diagnostic(FString::Printf(TEXT("\"%s\" found and used"), *AiDebugName(*Npc)));   // 0x101a93b4..0x101a93c1 DevMsg(2, ...)
	// `m_NPCState` is read BEFORE the ideal-state write: an NPC already in SCRIPT would not reselect,
	// so the script schedule is installed on it directly (local `0x2e` through `0x10280de0`).
	const int32 StateBefore = Npc->NpcStateRetail();                    // 0x101a93c7 npc +0x5cc0
	Npc->RequestIdealStateRetail(GAiNpcStateScript, GAiPossessLine);    // 0x101a93d3..0x101a93e7 line 0x554, ideal 4
	if (StateBefore == GAiNpcStateScript)                               // 0x101a93f2
	{
		Npc->ChangeSchedule(ScheduleAiScript);                          // 0x101a93f8 0x10280de0(0x2e)
	}
	// Named modernization: the stand-in for the scripted schedule `0x2e` selects.
	BeginBeat(*Npc);
}

// Slot 584: `0x101a9510` (`CCineAI::vfunc584`), 139 bytes. `0x101a82d0`'s shape with two differences:
// the empty-name arm answers TRUE, and there is no debug tail.
bool FElysiumAiScriptedSequence::StartSequence(FElysiumNpcBase& Npc, const FString& SequenceName,
	bool bCompleteOnEmpty)
{
	bSequenceStarted = true;                            // 0x101a9517 m_sequenceStarted := 1
	if (SequenceName.IsEmpty() && bCompleteOnEmpty)     // 0x101a951e / 0x101a9526
	{
		SequenceDone(Npc);                              // 0x101a952d SequenceDone 0x101a8460
		return true;                                    // 0x101a9532 AL = 1 (`CCineNPC`'s answers 0)
	}
	// `LookupSequence` (`0x101a9549`) into `m_nSequence` (`0x101a9551`); -1 -> the warning
	// (`0x101a9570`) and sequence 0 (`0x101a9579`); `m_flCycle = 0` (`0x101a9585`);
	// `ResetSequenceInfo` (`0x101a958f`). The clip plays through the body, as `CCineNPC`'s does.
	const float Seconds = PlayBeatClip(Npc, SequenceName, /*bLoop=*/!bCompleteOnEmpty);
	if (Seconds < 0.f)                                  // 0x101a9557
	{
		// `Warning("%s: unknown aiscripted sequence \"%s\"\n")` through `0x109f362c` (`0x101a9570`).
		// 0x101a955b JNZ: a null name prints as "" (`0x106b8540`); 0x101a9565 GetDebugName(npc).
		UE_LOG(LogElysiumAiSeq, Log, TEXT("%s: unknown aiscripted sequence \"%s\""), *AiDebugName(Npc),
			*SequenceName);
	}
	BeatClipEndsAt = (World != nullptr ? World->NowSeconds() : 0.0) + FMath::Max(0.f, Seconds);
	return true;                                        // 0x101a9595 AL = 1
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
