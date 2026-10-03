// 0018 story 8, brief S7 -- retail's one-NPC trace, on `FElysiumNpcBase`.
//
// Declared in `Substrate/ElysiumNpcBase.h` ("Retail's one-NPC trace"). The recovery, with every
// reader of `ai_debug_npc` (`DAT_10925444`), `ent_trace_conditions` (cvar `0x10924a68`) and the
// `ent_trace` / `npc_task_text` overlay bits: `docs/vtmb/npc-ai/schedule-kernel.md` § "The debug
// NPC's prints — every reader of ai_debug_npc and ent_trace_conditions (2026-09-30)".
//
// Everything here is debug output. No rule reads a word this file writes; the world's trace ring
// (`FElysiumEntityWorld::AppendAiDebugTrace`) and the log are the only sinks.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

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

void FElysiumNpcBase::TraceConditionDelta(const FElysiumNpcConditions& Before) const
{
	// "When ent_trace is on, this will dump info about conditions also." (`0x105d7af0`): the
	// `ent_trace` bit on this entity and `ent_trace_conditions > 0`. Retail's own lines at those
	// sites were compiled out; the port prints the change, one line per condition, as
	// `SetCondition name (id)` / `ClearCondition name (id)`.
	if ((DebugOverlays & OverlayEntTraceBit) == 0
		|| ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::EntTraceConditions) <= 0)
	{
		return;
	}
	for (int32 Ordinal = 0; Ordinal < FElysiumNpcConditions::NumOrdinals; ++Ordinal)
	{
		const bool bWas = Before.HasOrdinal(Ordinal);
		const bool bIs = Cognition.Conditions.HasOrdinal(Ordinal);
		if (bWas != bIs)
		{
			NpcTraceMessage(FString::Printf(TEXT("%s %s"),
				bIs ? TEXT("SetCondition") : TEXT("ClearCondition"), *TraceCondLabel(Ordinal)), 1);
		}
	}
}

void FElysiumNpcBase::DebugScheduleInstalled(int32 GlobalScheduleId)
{
	// `SetSchedule` `0x10280e50`, last statement: `if (m_debugOverlays & 0x8000000)
	// DevMsg("Schedule: %s\n", pSchedule->GetName())` (`0x105cde18`, the name at `CAI_Schedule
	// +0x40`). Printed as `name (global/local)`.
	if ((DebugOverlays & OverlayTaskTextBit) != 0)
	{
		NpcTraceMessage(FString::Printf(TEXT("Schedule: %s"), *ElysiumScheduleLabel(GlobalScheduleId, this)));
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
	// not reproduce the `+0x5f34`/`+0x5f38`/`+0x5f3c` overlay record the same arm writes.
	if ((DebugOverlays & OverlayTaskTextBit) == 0)
	{
		return;
	}
	for (int32 Ordinal = 0; Ordinal < GTraceBreakScanBits; ++Ordinal)
	{
		const bool bInverted = InvertedFiring.HasOrdinal(Ordinal);
		if (bInverted || Firing.HasOrdinal(Ordinal))
		{
			NpcTraceMessage(FString::Printf(TEXT("   Break condition -> %s%s"),
				bInverted ? TEXT("!") : TEXT(""), *TraceCondLabel(Ordinal)));
			return;
		}
	}
}
