// 0018 story 8, brief S7 -- retail's one-NPC trace, on `FElysiumNpcBase`.
//
// Declared in `Substrate/ElysiumNpcBase.h` ("Retail's one-NPC trace"). The recovery, with every
// reader of `ai_debug_npc` (`DAT_10925444`), `ent_trace_conditions` (cvar `0x10924a68`) and the
// `ent_trace` / `npc_task_text` overlay bits: `docs/vtmb/npc-ai/schedule-kernel.md` § "The debug
// NPC's prints — every reader of ai_debug_npc and ent_trace_conditions (2026-09-30)".
//
// Everything here is debug output. No rule reads a word this file writes; the world's trace ring
// (`FElysiumEntityWorld::AppendAiDebugTrace`), the log and the AI trace sink
// (`FElysiumEntityWorld::EmitAiTrace`, spec 0002 step 1 T5, `stories/wave2/seam.md`) are the only
// sinks. Where a print site already formats the text, it runs under `bits || IsAiTraced()` and the
// trace event is emitted from there.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"

namespace
{
	// `0x105d88e0`: one letter per `m_afMemory` (`+0x5d8c`) bit 0..31, `'.'` for a clear bit
	// (`0x1028dabe`..`0x1028dae6`).
	const TCHAR* const GTraceMemoryLetters = TEXT("PIS__PF_T_L__TTEPLM________ICCCC");
	constexpr int32 GTraceMemoryBits = 0x20;
	// `0x105d88b8`: one letter per `m_bfAINPCFlags` (`+0x14b8`) bit; the loop stops at `0x1e`
	// (`0x1028db18`..`0x1028db44`).
	const TCHAR* const GTraceFlagLetters = TEXT("RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_");
	constexpr int32 GTraceFlagBits = 0x1e;
	// `0x1027d990` answers the navigator type; the formatter names two of them (`0x1028db6b`..).
	constexpr int32 GTraceNavJump = 1;    // `0x105d88b0` "JUMP", else `0x105d88a8` "    "
	constexpr int32 GTraceNavClimb = 3;   // `0x105d88a0` "CLIMB", else `0x105d8898` "     "
	// `IsScheduleValid`'s break scan runs `0..0xbf` (`0x1028127c`): retail's 192 condition bits.
	constexpr int32 GTraceBreakScanBits = 0xc0;

	/** `name (0xNN)`: retail prints the registered name where it has one; the ordinal rides along so
	 *  a row reads against the binary. `COND_?` (no enumerator in this runtime) keeps its number. */
	FString TraceCondLabel(int32 Ordinal)
	{
		return FString::Printf(TEXT("%s (0x%x)"),
			ElysiumNpcCondName(static_cast<EElysiumNpcCond>(Ordinal)), Ordinal);
	}

	/** Retail's `NPC_STATE` name table, `0x1027e660` (slot 406's `GetStateName` reads it): the names
	 *  of `0`, `1` and `7` are the 4-byte strings `DAT_10547418` / `DAT_105cd430` / `DAT_1053fd98`
	 *  (None / Idle / Dead, the order every port reader of the table pins), the rest are labelled
	 *  strings; `__UNKNOWN__` past `0xe`. */
	const TCHAR* TraceStateName(int32 Retail)
	{
		switch (Retail)
		{
		case 0x0: return TEXT("None");
		case 0x1: return TEXT("Idle");
		case 0x2: return TEXT("Combat");
		case 0x3: return TEXT("Alert");
		case 0x4: return TEXT("Script");
		case 0x5: return TEXT("Playdead");
		case 0x6: return TEXT("Prone");
		case 0x7: return TEXT("Dead");
		case 0x8: return TEXT("Fleeing");
		case 0x9: return TEXT("Retreating");
		case 0xa: return TEXT("Cowering");
		case 0xb: return TEXT("Hunting");
		case 0xc: return TEXT("Dialog");
		case 0xd: return TEXT("Oblivious");
		case 0xe: return TEXT("CriminalSuspicion");
		default:  return TEXT("__UNKNOWN__");
		}
	}
}

// --- The AI trace taps (`stories/wave2/seam.md`) --------------------------------------------------

bool FElysiumNpcBase::IsAiTraced() const
{
	return World != nullptr && World->HasAiTraceSink();
}

void FElysiumNpcBase::EmitAiTrace(FName Kind, FString Text) const
{
	if (World != nullptr)
	{
		World->EmitAiTrace(*this, Kind, MoveTemp(Text));
	}
}

void FElysiumNpcBase::TraceTaskDone() const
{
	if (!IsAiTraced())
	{
		return;
	}
	// The running step's GLOBAL task id, the one `DebugTaskStart`'s `task` event names: `TaskOps().NameOf`
	// is keyed on it (`CurrentRetailTaskNumber` answers the class-local number, which names nothing).
	const FElysiumScheduleProgram* Program = Schedule.IsRunning() ? ElysiumScheduleFor(Schedule.Current) : nullptr;
	EmitAiTrace(TEXT("taskdone"), Program != nullptr && Program->Tasks.IsValidIndex(Schedule.TaskIndex)
		? FElysiumScheduleCorpus::Get().TaskOps().NameOf(Program->Tasks[Schedule.TaskIndex].TaskId)
		: FString(TEXT("(no task)")));
}

void FElysiumNpcBase::TraceStateChange(int32 OldRetail, int32 NewRetail) const
{
	if (OldRetail == NewRetail || !IsAiTraced())
	{
		return;
	}
	EmitAiTrace(TEXT("state"), FString::Printf(TEXT("%s -> %s"),
		TraceStateName(OldRetail), TraceStateName(NewRetail)));
}

FString FElysiumNpcBase::TraceSequenceName(int32 Sequence) const
{
	if (const FElysiumNpc* const Troika = AsNpc())
	{
		if (Troika->SequenceRows.IsValidIndex(Sequence) && !Troika->SequenceRows[Sequence].Label.IsEmpty())
		{
			return Troika->SequenceRows[Sequence].Label;
		}
	}
	return FString::Printf(TEXT("seq %d"), Sequence);
}

bool FElysiumNpcBase::IsAiDebugNpc() const
{
	// `DAT_10925444` as every gated arm reads it (`10296275`..`1029629c`): `-1` answers null;
	// otherwise the slot's serial must match and its entity pointer is compared with `this`.
	// `FElysiumEntityWorld::Resolve` is that test (a stale epoch, a dead or removed entity resolve
	// to null). The word is the world's (`AiDebugNpc`), set by the console verbs.
	if (World == nullptr)
	{
		return false;
	}
	const FElysiumEntityHandle DebugNpc = World->AiDebugNpc();
	return DebugNpc.IsSet() && World->Resolve(DebugNpc) == this;
}

void FElysiumNpcBase::NpcTraceMessage(const FString& Message, int32 Indent) const
{
	// `0x1028d990` (reached through slot 18 `0x1028de10` / slot 17 `0x1028de90`), unbuffered arm:
	// `"%-20s  %6.2f : %*s %s\n%s%s %s%s %s\n\n"`. Retail's `%-20s` operand is `GetDebugName`; the
	// port's is `DebugString()` (index, targetname, class), which is what every other row prints.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// `0x1028d9fd`..`0x1028da88`: `ent_trace_conditions` read as `!IsCommand() && m_nValue > 0`;
	// the list is `"CONDS:"`, `" %s"` (`0x105a3060`) per held condition in index order, `"\n"`.
	FString Conds;
	if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::EntTraceConditions) > 0)
	{
		Conds = TEXT("CONDS:");
		for (int32 Ordinal = 0; Ordinal < FElysiumNpcConditions::NumOrdinals; ++Ordinal)
		{
			if (Cognition.Conditions.HasOrdinal(Ordinal))
			{
				Conds.Appendf(TEXT(" %s"), *TraceCondLabel(Ordinal));
			}
		}
		Conds.AppendChar(TEXT('\n'));
	}

	FString Memory;
	const uint32 MemoryWord = BaseScheduleHost.MemoryBits;
	for (int32 Bit = 0; Bit < GTraceMemoryBits; ++Bit)
	{
		Memory.AppendChar((MemoryWord & (1u << Bit)) != 0 ? GTraceMemoryLetters[Bit] : TEXT('.'));
	}

	FString FlagLetters;
	const uint32 FlagWord = NpcFlags.RawWord1();
	for (int32 Bit = 0; Bit < GTraceFlagBits; ++Bit)
	{
		const uint32 Mask = 1u << Bit;
		FlagLetters.AppendChar((FlagWord & Mask) == Mask ? GTraceFlagLetters[Bit] : TEXT('.'));
	}

	// `0x1028db6b`..`0x1028dbcd`: only a climbing or jumping navigator is named.
	FString Nav;
	const int32 NavType = NavGetType();
	if (NavType == GTraceNavClimb || NavType == GTraceNavJump)
	{
		Nav = FString::Printf(TEXT("NAV %s %s"),
			NavType == GTraceNavClimb ? TEXT("CLIMB") : TEXT("     "),
			NavType == GTraceNavJump ? TEXT("JUMP") : TEXT("    "));
	}

	// The `%-20s` / `%*s` widths are spelled out rather than handed to `Printf`.
	FString Entry = DebugString().RightPad(20);
	Entry.Appendf(TEXT("  %6.2f : "), Now);
	Entry += FString::ChrN(FMath::Max(Indent, 0), TEXT(' '));
	Entry.AppendChar(TEXT(' '));
	Entry += Message;
	Entry.AppendChar(TEXT('\n'));
	Entry += Conds;
	// The fourth `%s` is `auStack_828`, always empty (`0x1028db56`).
	Entry.Appendf(TEXT("%s %s %s"), *Memory, *FlagLetters, *Nav);

	// Retail's line is one `DevMsg`; the log takes it a line at a time.
	TArray<FString> Lines;
	Entry.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		UE_LOG(LogElysiumNpcTrace, Display, TEXT("%s"), *Line);
	}
	if (World != nullptr && IsAiDebugNpc())
	{
		World->AppendAiDebugTrace(MoveTemp(Entry));
	}
}

// `CAI_BaseNPCTroika` slots 17 (`0x1028de90`, const) and 18 (`0x1028de10`): verdict `mechanism`,
// `hand:FElysiumNpcBase::NpcTraceMessage` -- both stand as that one-line trace (debug output only).
// `ElysiumNpcSlots.inl` declares the two overrides; their bodies went with 0019/6's deletions and
// are restored here as the verdict states them. No port code calls either slot.
void FElysiumNpc::TraceMessage(const TCHAR* Message, int32 IndentLevel) const
{
	NpcTraceMessage(Message != nullptr ? FString(Message) : FString(), IndentLevel);
}

void FElysiumNpc::TraceMessage(const TCHAR* Message, int32 IndentLevel)
{
	NpcTraceMessage(Message != nullptr ? FString(Message) : FString(), IndentLevel);
}

void FElysiumNpcBase::TraceConditionDelta(const FElysiumNpcConditions& Before) const
{
	// "When ent_trace is on, this will dump info about conditions also." (`0x105d7af0`): the
	// `ent_trace` bit on this entity and `ent_trace_conditions > 0`. Retail's own lines at those
	// sites were compiled out; the port prints the change, one line per condition, as
	// `SetCondition name (id)` / `ClearCondition name (id)`. The AI trace takes the same delta as
	// `cond+` / `cond-` events.
	const bool bPrint = (DebugOverlays & OverlayEntTraceBit) != 0
		&& ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::EntTraceConditions) > 0;
	const bool bTrace = IsAiTraced();
	if (!bPrint && !bTrace)
	{
		return;
	}
	for (int32 Ordinal = 0; Ordinal < FElysiumNpcConditions::NumOrdinals; ++Ordinal)
	{
		const bool bWas = Before.HasOrdinal(Ordinal);
		const bool bIs = Cognition.Conditions.HasOrdinal(Ordinal);
		if (bWas != bIs)
		{
			if (bPrint)
			{
				NpcTraceMessage(FString::Printf(TEXT("%s %s"),
					bIs ? TEXT("SetCondition") : TEXT("ClearCondition"), *TraceCondLabel(Ordinal)), 1);
			}
			if (bTrace)
			{
				EmitAiTrace(bIs ? FName(TEXT("cond+")) : FName(TEXT("cond-")), TraceCondLabel(Ordinal));
			}
		}
	}
}

void FElysiumNpcBase::DebugScheduleInstalled(int32 GlobalScheduleId)
{
	// `SetSchedule` `0x10280e50`, last statement: `if (m_debugOverlays & 0x8000000)
	// DevMsg("Schedule: %s\n", pSchedule->GetName())` (`0x105cde18`, the name at `CAI_Schedule
	// +0x40`). Printed as `name (global/local)`; the AI trace's `schedule` event carries the same label.
	const bool bPrint = (DebugOverlays & OverlayTaskTextBit) != 0;
	const bool bTrace = IsAiTraced();
	if (!bPrint && !bTrace)
	{
		return;
	}
	const FString Label = ElysiumScheduleLabel(GlobalScheduleId, this);
	if (bPrint)
	{
		NpcTraceMessage(FString::Printf(TEXT("Schedule: %s"), *Label));
	}
	if (bTrace)
	{
		EmitAiTrace(TEXT("schedule"), Label);
	}
}

void FElysiumNpcBase::DebugScheduleBreak(const FElysiumNpcConditions& Firing,
	const FElysiumNpcConditions& InvertedFiring)
{
	// `IsScheduleValid` `0x10280ff0`, `0x10281259`..`0x10281334`: the lowest bit set in either
	// fired set; its name through slot `0x728` (the miss arm prints and stores "ERROR: Unknown
	// condition!", which `TraceCondLabel`'s `COND_? (0xNN)` stands for); `"   Break condition ->
	// !%s\n"` (`0x105cde48`) when the bit is in the inverted set, else `"   Break condition ->
	// %s\n"` (`0x105cde28`) — under `m_debugOverlays & 0x8000000`. Retail's arm is also gated on
	// `developer != 0` (`DAT_1070af4c`, `0x1028121f`..`0x1028123d`), which ships 0: NAMED
	// DIVERGENCE (debug output only) — the port drops that half of the gate for the print, and does
	// not reproduce the `+0x5f34`/`+0x5f38`/`+0x5f3c` overlay record the same arm writes. The AI
	// trace's `break` event is the same lowest ordinal, `!`-prefixed when inverted.
	const bool bPrint = (DebugOverlays & OverlayTaskTextBit) != 0;
	const bool bTrace = IsAiTraced();
	if (!bPrint && !bTrace)
	{
		return;
	}
	for (int32 Ordinal = 0; Ordinal < GTraceBreakScanBits; ++Ordinal)
	{
		const bool bInverted = InvertedFiring.HasOrdinal(Ordinal);
		if (bInverted || Firing.HasOrdinal(Ordinal))
		{
			const FString Label = FString::Printf(TEXT("%s%s"),
				bInverted ? TEXT("!") : TEXT(""), *TraceCondLabel(Ordinal));
			if (bPrint)
			{
				NpcTraceMessage(FString::Printf(TEXT("   Break condition -> %s"), *Label));
			}
			if (bTrace)
			{
				EmitAiTrace(TEXT("break"), Label);
			}
			return;
		}
	}
}
