#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29d, family **Debug10** — the debug ring, the trace messages, the seams, and every slot-124
// TEXT overlay body of layers 10–18. `ElysiumNpcDebug10_2.cpp` carries the slot-123 GEOMETRY
// bodies and `NPCThinkDebugPre`. The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// The standing fact of story 29c-1's Debug family holds here unchanged: none of these bodies has an
// output device in this runtime, so every arm is ported verbatim, retail's own format strings are
// reproduced as strings because the text is the evidence that the arm was taken, and each line lands
// on `FDebugLine`. What is delivered is the ORDER of the arms, the GATES on them, the LINE INDEX
// each body returns, and the constants each pushes.
//
// **Every format string below was read out of the pinned image's `.rdata`, not out of the
// decompiler.** The decompiler folded most of these argument lists into the 512-byte stack frames
// and lost them; where the checklist's one-line walk and the image disagree, the image wins and the
// correction is stated at the arm.

namespace
{
	// Unit-prefixed: the module builds adaptive-unity and anonymous namespaces are merged.

	// --- `m_debugOverlays` bits this family gates on, with the instruction that tests each --------
	constexpr int32 GDebug10BitCollisionBox = 0x1000;  // 0x1029cb39 `TEST AH,0x10`
	constexpr int32 GDebug10BitConditions = 0x10000000;// 0x1029d9cb
	constexpr int32 GDebug10BitWeaponRings = 0x20000000;// 0x1029cd68
	constexpr int32 GDebug10BitViewCones = 0x400000;   // 0x1029ca59

	// --- The debug globals these bodies read ---------------------------------------------------------
	// Retail's own trace buffer, `1028de9a PUSH 0x200` on slot 17 and the same on slot 18. It is
	// handed to the formatter as an argument rather than baked in, so `0x1028d990`'s
	// "size <= 0 writes nothing at all" arm stays reachable.
	constexpr int32 GDebug10TraceBufferBytes = 0x200;

	constexpr TCHAR GDebug10CvTraceRing[] = TEXT("DAT_10920534");   // the trace-message toggle, a byte
	// Troika text: the EALTAI line, under `ent_trace_doors` (`DAT_1092429c`).
	constexpr ElysiumNpcTunables::EConVar GDebug10CvAltAi = ElysiumNpcTunables::EConVar::EntTraceDoors;

	// The two trace bytes. Game-thread only, like the rest of the substrate.
	TMap<FString, int32> GDebug10TraceBytes;

	// --- Retail's `NDebugOverlay` entry points, by name, so a captured line names its call --------
	constexpr TCHAR GDebug10Box[] = TEXT("NDebugOverlay::Box");
	constexpr TCHAR GDebug10BoxAngles[] = TEXT("NDebugOverlay::BoxAngles");
	constexpr TCHAR GDebug10BoxDirection[] = TEXT("NDebugOverlay::BoxDirection");
	constexpr TCHAR GDebug10Line[] = TEXT("NDebugOverlay::Line");
	constexpr TCHAR GDebug10Text[] = TEXT("NDebugOverlay::Text");
	constexpr TCHAR GDebug10Circle[] = TEXT("NDebugOverlay::Circle");

	// --- `CAI_BaseNPC::DrawDebugTextOverlays` `0x102767d0` — the `.rdata` it prints ---------------

	// --- `CAI_BaseNPCTroika::DrawDebugTextOverlays` `0x1029d4e0` ----------------------------------
	constexpr TCHAR GDebug10SeqPrefix[] = TEXT("Seq: ");                 // 0x105cc90c
	constexpr TCHAR GDebug10SeqInvalid[] = TEXT("(INVALID)");            // 0x105cc900
	constexpr TCHAR GDebug10SeqSeparator[] = TEXT(" / ");                // 0x105cc8fc
	constexpr TCHAR GDebug10FmtCycle[] = TEXT("Cycle: %.2f");            // 0x105cc8ec
	constexpr TCHAR GDebug10FmtMoveYaw[] = TEXT("move_yaw: %.3f");       // 0x105d9c78
	constexpr TCHAR GDebug10FmtAimPitch[] = TEXT("aim_pitch: %.3f");     // 0x105d9c64
	// **RETAIL BUG, REPRODUCED.** `0x105d9c54` is `"aim_yaw: %.3"` — the conversion character is
	// missing, so the pose value is never formatted at all. The string is transcribed verbatim as the
	// evidence (`Retail`), and the TEXT is the literal prefix, because MSVC's formatter stops at the
	// incomplete specifier. The one thing this family could not settle from the image is whether the
	// run-time library emits the prefix or nothing; the prefix is what it does on the shipped CRT.
	constexpr TCHAR GDebug10FmtAimYaw[] = TEXT("aim_yaw: %.3");          // 0x105d9c54
	constexpr TCHAR GDebug10TextAimYaw[] = TEXT("aim_yaw: ");
	constexpr TCHAR GDebug10FmtGroundSpeed[] = TEXT("ground speed: %.3f");// 0x105d9c3c
	constexpr TCHAR GDebug10FmtPlayerDist[] = TEXT("dist to player: %.3f");// 0x105d9c20
	constexpr TCHAR GDebug10FmtDisposition[] = TEXT("Disposition: %s");  // 0x105d9c0c
	constexpr TCHAR GDebug10FmtPos[] = TEXT("pos: %5.1f, %5.1f, %5.1f"); // 0x105d9bec
	constexpr TCHAR GDebug10FmtDir[] = TEXT("dir: %5.1f, %5.1f, %5.1f"); // 0x105d9bcc
	constexpr TCHAR GDebug10IntCondHeader[] = TEXT("INT COND\n");        // 0x105d9bc0
	constexpr TCHAR GDebug10CondHeader[] = TEXT("COND\n");               // 0x105d9ba8
	constexpr TCHAR GDebug10FmtCondSet[] = TEXT("%s  ");                 // 0x105d9bb8
	constexpr TCHAR GDebug10FmtCondInterrupt[] = TEXT("!%s ");           // 0x105d9bb0
	constexpr TCHAR GDebug10FmtCondWatch[] = TEXT("%s ");                // 0x105a1518
	constexpr TCHAR GDebug10FmtHitGroups[] = TEXT("HG - %d : HB - %d");  // 0x105d9b90
	constexpr TCHAR GDebug10FmtAltAi1[] =
		TEXT("EALTAI_OPEN_DOOR              - %f");                       // 0x105d9b64
	constexpr TCHAR GDebug10FmtAltAi2[] =
		TEXT("EALTAI_WAIT_DOOR              - %f");                       // 0x105d9b38
	constexpr TCHAR GDebug10FmtAltAi3[] =
		TEXT("EALTAI_BLOCKED_DOOR           - %f");                       // 0x105d9b0c
	constexpr TCHAR GDebug10FmtAltAi4[] =
		TEXT("EALTAI_SCRIPT_STOPPED_AT_DOOR - %f");                       // 0x105d9ae0
	constexpr TCHAR GDebug10FmtSceneTime[] = TEXT("Scene: %s time: %.2f");// 0x105d9ac4
	constexpr TCHAR GDebug10FmtScene[] = TEXT("Scene: %s");              // 0x105d9ab8
	constexpr TCHAR GDebug10SceneUnknown[] = TEXT("??? scene");          // 0x105d9aac

	// --- The species text bodies ------------------------------------------------------------------

	// --- The fixed 26-id watch list `0x1029d4e0` writes into its stack array at `ESP+0x80`, in
	//     retail's order. Not sorted; the order IS the line layout. -------------------------------
	constexpr int32 GDebug10CondWatchList[] = {
		0x40, 0x46, 0x01, 0x47, 0x48, 0x49, 0x4c, 0x4f, 0x51, 0x08, 0x5f, 0x60, 0x09,
		0x54, 0x56, 0x57, 0x58, 0x59, 0x63, 0x6d, 0x6f, 0x70, 0x0a, 0x0b, 0x0c, 0x0d };
	static_assert(UE_ARRAY_COUNT(GDebug10CondWatchList) == 0x1a,
		"0x1029d4e0's watch loop runs `while (i < 0x1a)`");

	// The condition-id ceiling both the bitfield walk and `0x1027e7f0` stop at.
	constexpr int32 GDebug10LastBaseCondition = 0x77;   // the loop is `while (id < 0x77)`

	// The per-entity debug ring, from `0x1027ef20`: a 0x4000-byte buffer at `+0x1b4e`, a cursor at
	// `+0x5b50` and a wrap latch at `+0x5b54`. ABSENT (story 29b); the two constants are the rule.
	constexpr int32 GDebug10RingSize = 0x4000;

	// The three-character condition abbreviation `0x1029d4e0` builds for one id: copy three bytes of
	// slot 408's answer, upper-casing the first `n` of them, then NUL. `n` is `(10 * 4) / 10` = 4
	// when the probe stands and 0 when it does not, and 4 is past the end, so the whole abbreviation
	// is either all upper or all lower. The division is in the listing and is kept because it is what
	// says the count is a count of CHARACTERS.
	FString GDebug10Abbreviation(const TCHAR* Short, bool bUpperCase)
	{
		const int32 UpperCount = ((bUpperCase ? 10 : 0) * 4) / 10;
		FString Out;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const TCHAR Ch = (Short != nullptr && Short[Index] != TEXT('\0')) ? Short[Index] : TEXT('\0');
			if (Ch == TEXT('\0'))
			{
				break;
			}
			Out.AppendChar(Index < UpperCount ? FChar::ToUpper(Ch) : Ch);
		}
		return Out;
	}
}

// -------------------------------------------------------------------------------------------------
// The trace bytes.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::DebugTraceByte(const TCHAR* RetailGlobal)
{
	if (RetailGlobal == nullptr)
	{
		return 0;
	}
	const int32* Value = GDebug10TraceBytes.Find(FString(RetailGlobal));
	return Value != nullptr ? *Value : 0;
}

void FElysiumNpc::SetDebugTraceByte(const TCHAR* RetailGlobal, int32 Value)
{
	if (RetailGlobal == nullptr)
	{
		GDebug10TraceBytes.Reset();
		return;
	}
	GDebug10TraceBytes.Add(FString(RetailGlobal), Value);
}

// -------------------------------------------------------------------------------------------------
// The debug ring — `0x1027ef20`, `0x1027ee20`, `0x1027efb0`.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slots 17 / 18 / 20 — the Troika trace messages.
// -------------------------------------------------------------------------------------------------

FString FElysiumNpc::TraceMessageFormat(const TCHAR* Message, int32 IndentLevel) const
{
	// SEAM for `0x1028d990`, family **Conditions10**'s row in this same band
	// (`FElysiumNpc::BuildConditionDebugString`). Its three format arms are, from `.rdata`:
	//   `0x105d8828` `"%-20s  %6.2f : %*s %s\n%s%s %s%s %s\n\n"`  — the DevMsg arm, with GetDebugName
	//   `0x105d8868` `"%6.2f : %*s %s\n%s%s %s%s %s\n\n"`          — the ring arm with the blocks
	//   `0x105d8854` `"%6.2f : %*s %s\n"`                          — the ring arm without them
	// and the blocks are `"CONDS:"` (`0x105d8908`), the 32-glyph `"PIS__PF_T_L__TTEPLM________ICCCC"`
	// (`0x105d88e0`) over `m_afMemory`, the 30-glyph `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_"`
	// (`0x105d88b8`) over `m_bfAINPCFlags` and `"NAV %s %s"` (`0x105d888c`). The indent is clamped at
	// 0 (`if (level < 0) level = 0`) and a null message becomes the empty string.
	//
	// **No longer a seam.** Family Conditions10 landed `0x1028d990` as
	// `FElysiumNpc::BuildConditionDebugString` in this same wave, so the formatter is the real body
	// now. Both trace slots hand it retail's own buffer size — `1028de9a PUSH 0x200` on slot 17 and
	// the identical push on slot 18 — which is load-bearing rather than decorative: a size of zero
	// or less is the arm that writes NOTHING AT ALL, and it stays reachable because the size is a
	// parameter here instead of a constant inside the body.
	return BuildConditionDebugString(Message, IndentLevel, GDebug10TraceBufferBytes);
}

bool FElysiumNpc::TraceMessagesGoToRing() const
{
	// `DAT_10920534`, a byte. It ships clear, so every trace message takes the `DevMsg` arm.
	return DebugTraceByte(GDebug10CvTraceRing) != 0;
}

void FElysiumNpc::TraceMessage(const TCHAR* Message, int32 IndentLevel)
{
	// slot 18, `CAI_BaseNPCTroika::FUN_1028de10`, 94 bytes:
	//
	//     char buf[512];
	//     FUN_1028d990(this, param_1, param_2, buf, 0x200);
	//     if (DAT_10920534) { FUN_1027ef20(this, buf); return; }
	//     DevMsg(buf);
	//
	// The formatted text goes to the NPC's OWN ring, and the `DevMsg` arm prints the same text.
	const FString Formatted = TraceMessageFormat(Message, IndentLevel);
	if (TraceMessagesGoToRing())
	{
		AppendDebugLogLine(*Formatted);
		return;
	}
	EmitDevMsg(TEXT("0x1028de10"), Formatted);
}

void FElysiumNpc::TraceMessage(const TCHAR* Message, int32 IndentLevel) const
{
	// slot 17, `CAI_BaseNPCTroika::FUN_1028de90`, 125 bytes — the CONST twin, and NOT the same body:
	//
	//     char formatted[512]; char raw[512];
	//     FUN_1028d990(this, param_1, param_2, formatted, 0x200);
	//     if (DAT_10920534) { Q_strncpy(raw, param_1, 0x200); FUN_1027ee20(this, raw); return; }
	//     DevMsg(formatted);
	//
	// Two asymmetries against slot 18, both retail's and both reproduced: the ring receives the RAW
	// message rather than the formatted one — so only the `DevMsg` arm ever sees the format — and the
	// ring is the GLOBAL one (`0x1027ee20`) rather than the NPC's. The formatter still RUNS on the
	// ring arm; its output is simply dropped, which is why it is called before the branch.
	const FString Formatted = TraceMessageFormat(Message, IndentLevel);
	if (TraceMessagesGoToRing())
	{
		// `Q_strncpy(raw, param_1, 0x200)` of the raw message. A null source is retail's own
		// `Q_strncpy` guard, which writes an empty string.
		AppendGlobalDebugLogLine(Message != nullptr ? Message : TEXT(""));
		return;
	}
	EmitDevMsg(TEXT("0x1028de90"), Formatted);
}

void FElysiumNpc::TraceMessageBare(const TCHAR* Message)
{
	// slot 20, `CAI_BaseNPCTroika::FUN_1028df30`, 89 bytes:
	//
	//     if (!param_1) return;
	//     if (DAT_10920534) { char buf[512]; Q_strncpy(buf, param_1, 0x200); FUN_1027ef20(this, buf); return; }
	//     DevMsg(param_1);
	//
	// A null message does nothing at all — not even a `DevMsg`. Byte-identical to slot 19
	// (`0x1028dfb0`, story 29c-1) except that slot 19 rings through the GLOBAL `0x1027ee20` and this
	// one through the NPC's own `0x1027ef20`; that is the whole difference between the pair, and it
	// is the reason both exist.
	if (Message == nullptr)
	{
		return;
	}
	if (TraceMessagesGoToRing())
	{
		AppendDebugLogLine(Message);
		return;
	}
	EmitDevMsg(TEXT("0x1028df30"), FString(Message));
}

// -------------------------------------------------------------------------------------------------
// Slot 124 — the dispatcher and its arms.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::DrawDebugTextOverlays()
{
	// slot 124, the Troika body `0x1029d4e0`. `CNPC_VHengeyokai`, `CNPC_VNewscaster`, `CNPC_VTzimisce`
	// and `CNPC_VZombie` override this method on their C++ classes (story 5 step 3), each chaining the
	// Troika body directly. The two bodies on classes no map stands (`CNPC_Crow` `0x10358f90`,
	// `CScriptedTarget` `0x1034ddf0`) carry no port arm.
	return TroikaDrawDebugTextOverlays();
}

int32 FElysiumNpc::TroikaDrawDebugTextOverlays()
{
	// `0x1029d4e0`, 3,754 bytes, recovered from the LISTING. Every one of its twenty-odd lines uses
	// the same eight-argument `NDebugOverlay::EntityText(IndexOfEdict(edict), line, text, 0.0, 255,
	// 255, 255, 255)` shape; `EmitEntityText` is that seam and the colour is not carried because it
	// is white and opaque on every call site.
	int32 Line = FElysiumNpcBase::DrawDebugTextOverlays();
	if ((DebugOverlays & NpcKernelDebug10Shared::GDebug10BitText) == 0)
	{
		return Line;
	}

	// --- 1. The sequence line ----------------------------------------------------------------------
	//
	//     Q_strncpy(buf, "Seq: ", 512);
	//     model   = GetModelPtr();                       // kept for the pose block below
	//     seqdesc = GetSeqDesc(m_nSequence);
	//     if (!seqdesc) Q_strncat(buf, "(INVALID)");
	//     else { Q_strncat(buf, label); Q_strncat(buf, " / "); Q_strncat(buf, activityName); }
	//
	// `GetModelPtr()` is asked BEFORE `GetSeqDesc` and its answer is what gates the three pose lines,
	// not the descriptor's.
	const bool bHasModel = !Model.IsEmpty();
	{
		FString SeqLine(GDebug10SeqPrefix);
		FString SeqLabel;
		FString SeqActivity;
		if (!SequenceDescriptor(SequenceNumber, SeqLabel, SeqActivity))
		{
			SeqLine += GDebug10SeqInvalid;
			EmitEntityText(Line, GDebug10SeqInvalid, MoveTemp(SeqLine));
		}
		else
		{
			SeqLine += SeqLabel;
			SeqLine += GDebug10SeqSeparator;
			SeqLine += SeqActivity;
			EmitEntityText(Line, GDebug10SeqPrefix, MoveTemp(SeqLine));
		}
		++Line;
	}

	// --- 2. `"Cycle: %.2f"` with `m_flCycle` (+0x6f8) ---------------------------------------------
	EmitEntityText(Line, GDebug10FmtCycle, FString::Printf(GDebug10FmtCycle, SequenceCycle));
	++Line;

	// --- 3. The three pose parameters, only with a model ------------------------------------------
	if (bHasModel)
	{
		EmitEntityText(Line, GDebug10FmtMoveYaw,
			FString::Printf(GDebug10FmtMoveYaw, PoseParameter01(TEXT("move_yaw"))));
		++Line;
		EmitEntityText(Line, GDebug10FmtAimPitch,
			FString::Printf(GDebug10FmtAimPitch, PoseParameter01(TEXT("aim_pitch"))));
		++Line;
		// The third format string is `"aim_yaw: %.3"` — see the constant above. Retail's own typo:
		// the pose value it just computed is never printed. The line still appears and still consumes
		// an index, which is the part a later override can observe.
		(void)PoseParameter01(TEXT("aim_yaw"));
		EmitEntityText(Line, GDebug10FmtAimYaw, GDebug10TextAimYaw);
		++Line;
	}

	// --- 4. `"ground speed: %.3f"` (+0x654) and `"dist to player: %.3f"` (+0x6264) ----------------
	EmitEntityText(Line, GDebug10FmtGroundSpeed,
		FString::Printf(GDebug10FmtGroundSpeed, RetailGroundSpeed()));
	++Line;
	EmitEntityText(Line, GDebug10FmtPlayerDist, FString::Printf(GDebug10FmtPlayerDist,
		Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U));
	++Line;

	// --- 5. `"Disposition: %s"`, the name `0x100ec410(&DAT_10924980, m_nCurrDisposition)` answers.
	//        The port stores the disposition BY NAME, which is the same answer.
	EmitEntityText(Line, GDebug10FmtDisposition,
		FString::Printf(GDebug10FmtDisposition, *Disposition));
	++Line;

	// --- 6. `pos:` from slot 217 and `dir:` from slot 219, both `%5.1f, %5.1f, %5.1f` --------------
	//
	// Retail re-dispatches each accessor once per component (three calls for `pos`, three for `dir`),
	// which is an artefact and not a behaviour. Both are SOURCE units.
	{
		const FVector PosUnits = Origin / ElysiumMove::U;
		EmitEntityText(Line, GDebug10FmtPos,
			FString::Printf(GDebug10FmtPos, PosUnits.X, PosUnits.Y, PosUnits.Z));
		++Line;
		EmitEntityText(Line, GDebug10FmtDir,
			FString::Printf(GDebug10FmtDir, Angles.X, Angles.Y, Angles.Z));
		++Line;
	}

	// --- 7. The condition dump, or the one-line short circuit -------------------------------------
	if ((DebugOverlays & GDebug10BitConditions) != 0 && Schedule.IsRunning())
	{
		Line = EmitConditionDump(Line);
	}
	else if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyTooFar))
	{
		// The `else` arm is a SECOND `Enemy too far to attack` line — the base body already printed
		// one under `m_debugOverlays & 1`, and this one lands again on the Troika body's own budget.
		// Retail's, and reproduced.
		EmitEntityText(Line, NpcKernelDebug10Shared::GDebug10EnemyTooFar, NpcKernelDebug10Shared::GDebug10EnemyTooFar);
		++Line;
	}

	// --- 8. `"HG - %d : HB - %d"` — the last hit group and the last damage packet's `+0x40` -------
	EmitEntityText(Line, GDebug10FmtHitGroups,
		FString::Printf(GDebug10FmtHitGroups, LastHitGroup, RetailLastDamageInfoWord40()));
	++Line;

	// --- 9. The alternate-AI line, under `DAT_1092429c` --------------------------------------------
	//
	// The float is `m_flAlternateAIExpireTimer (+0x6450) - curtime` — the REMAINING time, which the
	// checklist's walk left as "a %f". Mode 0 and anything above 4 print nothing: the switch's own
	// `JA 4` and `default:` both fall past the line.
	if (ElysiumNpcTunables::ConVarInt(GDebug10CvAltAi) != 0)
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		const float Remaining = static_cast<float>(AlternateAiExpireTime - Now);
		switch (AlternateAi)
		{
		case 1:
			EmitEntityText(Line, GDebug10FmtAltAi1,
				FString::Printf(GDebug10FmtAltAi1, Remaining));
			++Line;
			break;
		case 2:
			EmitEntityText(Line, GDebug10FmtAltAi2,
				FString::Printf(GDebug10FmtAltAi2, Remaining));
			++Line;
			break;
		case 3:
			EmitEntityText(Line, GDebug10FmtAltAi3,
				FString::Printf(GDebug10FmtAltAi3, Remaining));
			++Line;
			break;
		case 4:
			EmitEntityText(Line, GDebug10FmtAltAi4,
				FString::Printf(GDebug10FmtAltAi4, Remaining));
			++Line;
			break;
		default:
			break;
		}
	}

	// --- 10. The dialogue-scene line, three arms ---------------------------------------------------
	//
	//     if (!m_hDialogScene (+0x6554) resolves) -> nothing at all, and the index does not advance
	//     scene (+0x4bc) live  -> "Scene: %s time: %.2f" and RETURN IMMEDIATELY
	//     scene null           -> "Scene: %s"          and RETURN IMMEDIATELY
	//     neither              -> "??? scene"
	//
	// The two named arms return out of the body rather than falling through, which is why the third
	// is reachable only for a scene entity whose `+0x450` name pointer is itself null.
	{
		FString SceneName;
		bool bScenePlaying = false;
		double SceneTime = 0.0;
		if (DialogSceneWords(SceneName, bScenePlaying, SceneTime))
		{
			if (bScenePlaying)
			{
				EmitEntityText(Line, GDebug10FmtSceneTime,
					FString::Printf(GDebug10FmtSceneTime, *SceneName, SceneTime));
				return Line + 1;
			}
			if (!SceneName.IsEmpty())
			{
				EmitEntityText(Line, GDebug10FmtScene,
					FString::Printf(GDebug10FmtScene, *SceneName));
				return Line + 1;
			}
			EmitEntityText(Line, GDebug10SceneUnknown, GDebug10SceneUnknown);
			++Line;
		}
	}

	return Line;
}

int32 FElysiumNpc::EmitConditionDump(int32 FirstLine)
{
	// `0x1029d4e0`'s `0x10000000` block, split out because it is a third of the body. Two loops over
	// the same 512-byte assembly buffer, eight abbreviations per line.
	//
	// **Three corrections to the checklist's walk, all from the listing.**
	//   (1) The block opens with an `"INT COND\n"` header line (`0x105d9bc0`) before any abbreviation.
	//   (2) The two arms of the first loop do NOT share an upper-casing rule. The SET arm
	//       upper-cases when `HasCondition(id)` stands; the INTERRUPT arm upper-cases when it does
	//       NOT (`JZ` where the first has `JNZ`). That asymmetry is retail's.
	//   (3) The flush after the 26-id watch list is gated on the FIRST loop's leftover count — the
	//       listing reloads the saved `count & 7` and tests it against 7 — not on the watch list's
	//       own index. A retail bug, and the whole watch list is silently dropped whenever the
	//       bitfield loop happened to end with exactly seven pending abbreviations.
	int32 Line = FirstLine;
	EmitEntityText(Line, GDebug10IntCondHeader, GDebug10IntCondHeader);
	++Line;

	// The two six-word masks the body copies onto the stack before walking: the SET mask at
	// `+0x5c74` (`m_CustomInterruptConditions`) and the INTERRUPT mask at `+0x5c8c`
	// (`m_InverseInterruptConditions`). Both are `FElysiumNpcCognition` members here.
	FString Buffer;
	int32 Count = 0;
	for (int32 Id = 0; Id < GDebug10LastBaseCondition; ++Id)
	{
		const EElysiumNpcCond Cond = static_cast<EElysiumNpcCond>(Id);
		const bool bInterrupt = Cognition.InverseInterruptConditions.Has(Cond);
		if (Cognition.CustomInterruptConditions.Has(Cond))
		{
			++Count;
			Buffer += FString::Printf(GDebug10FmtCondSet,
				*GDebug10Abbreviation(GetShortConditionName(Id), Cognition.Conditions.Has(Cond)));
			if ((Count & 7) == 0)
			{
				Buffer += NpcKernelDebug10Shared::GDebug10Newline;
				EmitEntityText(Line, GDebug10FmtCondSet, MoveTemp(Buffer));
				Buffer.Reset();
				++Line;
			}
		}
		if (bInterrupt)
		{
			++Count;
			// The INVERTED probe: upper case when the condition is NOT standing.
			Buffer += FString::Printf(GDebug10FmtCondInterrupt,
				*GDebug10Abbreviation(GetShortConditionName(Id), !Cognition.Conditions.Has(Cond)));
			if ((Count & 7) == 0)
			{
				Buffer += NpcKernelDebug10Shared::GDebug10Newline;
				EmitEntityText(Line, GDebug10FmtCondInterrupt, MoveTemp(Buffer));
				Buffer.Reset();
				++Line;
			}
		}
	}

	// The first loop's leftover, flushed when the count is not a multiple of eight. The value of
	// `count & 7` is SAVED here and read again at the very end of the block.
	const int32 Leftover = Count & 7;
	if (Leftover != 0)
	{
		Buffer += NpcKernelDebug10Shared::GDebug10Newline;
		EmitEntityText(Line, GDebug10FmtCondSet, MoveTemp(Buffer));
		Buffer.Reset();
		++Line;
	}

	// The `COND` header and the fixed 26-id watch list, eight per line. This loop's probe is the
	// ordinary one (upper case when the condition stands) and its flush is on the WATCH INDEX.
	EmitEntityText(Line, GDebug10CondHeader, GDebug10CondHeader);
	++Line;
	Buffer.Reset();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GDebug10CondWatchList); ++Index)
	{
		const int32 Id = GDebug10CondWatchList[Index];
		const EElysiumNpcCond Cond = static_cast<EElysiumNpcCond>(Id);
		Buffer += FString::Printf(GDebug10FmtCondWatch,
			*GDebug10Abbreviation(GetShortConditionName(Id), Cognition.Conditions.Has(Cond)));
		if ((Index & 7) == 7)
		{
			Buffer += NpcKernelDebug10Shared::GDebug10Newline;
			EmitEntityText(Line, GDebug10FmtCondWatch, MoveTemp(Buffer));
			Buffer.Reset();
			++Line;
		}
	}

	// **Retail's bug, reproduced.** The final flush tests the BITFIELD loop's saved leftover against
	// 7, not the watch list's 26 % 8 == 2 remainder. So the last two watch entries are flushed unless
	// the bitfield loop ended with exactly seven pending, in which case they are dropped.
	if (Leftover != 7)
	{
		Buffer += NpcKernelDebug10Shared::GDebug10Newline;
		EmitEntityText(Line, GDebug10FmtCondWatch, MoveTemp(Buffer));
		++Line;
	}
	return Line;
}

// -------------------------------------------------------------------------------------------------
// The seams the text bodies read through.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::PoseParameter01(const TCHAR* PoseName) const
{
	// SEAM for `CBaseAnimating::GetPoseParameter01(name)` (`0x1000108c`). No studio header stands
	// here, so there is no pose table to index; 0.0 is what a model with no such parameter answers.
	(void)PoseName;
	return 0.f;
}

float FElysiumNpc::RetailGroundSpeed() const
{
	// SEAM for `m_flGroundSpeed` (`+0x0654`), which `CBaseAnimating::StudioFrameAdvance` is the sole
	// writer of. Family Positions records the same word as an input with no store.
	return 0.f;
}

int32 FElysiumNpc::RetailLastDamageInfoWord40() const
{
	// SEAM for `m_LastTakeDamageInfo + 0x40` (`+0x664c`). `0x101c2a30` is four bytes,
	// `return *(int*)(this + 0x40)`, over the 0x4c-byte `CTakeDamageInfo` the Troika constructor
	// builds at `+0x660c`; layout.md records the packet as walked and no reader for it. The port
	// carries the attacker (`FElysiumNpcMemory::LastDamageAttacker`) and not the packet.
	return 0;
}

float FElysiumNpc::RetailFieldOfViewDot() const
{
	// `m_flFieldOfView` (`+0x1574`). Story 29c-1's view-cone arm already answers this word from the
	// sense layer's default cone, which is the same question.
	return FieldOfViewDot;
}

int32 FElysiumNpc::RetailAlternateHullKind() const
{
	// `+0x156c`, the PATHING hull, which this arm draws a second box for when it differs from
	// `m_eHull`. No longer a seam: the word is carried (`PathingHullKind`) and differs from the
	// standing hull on the Sheriff, Hengeyokai and Ming Xiao, so retail's two-box arm is now
	// reachable for exactly the species retail reaches it for.
	return PathingHullKind;
}

bool FElysiumNpc::RetailBonePosition(const TCHAR* BoneName, FVector& OutPositionUnits,
	FVector& OutAnglesDegrees) const
{
	// SEAM for `CBaseAnimating::GetBonePosition01(name, &pos, &ang)` (`0x1000f263`).
	(void)BoneName;
	OutPositionUnits = FVector::ZeroVector;
	OutAnglesDegrees = FVector::ZeroVector;
	return false;
}

bool FElysiumNpc::ActiveWeaponRangeRingsUnits(float& OutFarUnits, float& OutNearUnits) const
{
	// SEAM for the four weapon range words. Story 29c-1's `ActiveWeaponEntity` answers null, so this
	// answers false and LEAVES both outputs alone — retail's unarmed values are written by the arm's
	// own `else`, not by the read, and clobbering them here would erase 2000.0 with a zero.
	(void)OutFarUnits;
	(void)OutNearUnits;
	return ActiveWeaponEntity() != nullptr;
}

bool FElysiumNpc::PlayerHeightenedAlert(const FElysiumEntity* Candidate) const
{
	// `0x1017f8d0`: `curtime < player->m_flHeightenedAlertExpireTimer (+0x1d1c)`, off the candidate's
	// `+0xa8 m_pPlayer`. A non-player candidate is retail's null-`+0xa8` arm, which answers false.
	// `m_pPlayer` is one of `CBaseEntity`'s cached downcasts; the port's question for the same thing
	// is "is this entity the one `FindPlayer()` answers", exactly as story 29c-1's enemy-memory arm
	// asks it.
	FElysiumPlayer* const Player = World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Candidate != static_cast<const FElysiumEntity*>(Player))
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return Now < Player->Police.HeightenedAlertExpiry;
}

int32 FElysiumNpc::PlayerCopsInPursuitCount(const FElysiumEntity* Candidate) const
{
	// `0x1017f770`: `player->m_iCopsInPursuitCount (+0x1d10)` — the same word family Dialogue reads
	// through `DialogThreatCount()`.
	FElysiumPlayer* const Player = World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Candidate != static_cast<const FElysiumEntity*>(Player))
	{
		return 0;
	}
	return Player->Police.CopsInPursuit;
}

bool FElysiumNpc::DialogSceneWords(FString& OutSceneName, bool& bOutScenePlaying,
	double& OutSceneTime) const
{
	// `m_hDialogScene` (`+0x6554`) resolves through the port's handle table; the scene entity's
	// `+0x450` name and the `CChoreoScene*` at `+0x4bc` with `GetTime()` (`0x1007dfa0`) are the
	// SEAM story 29c-1's Troika STAT body already records. So this answers whether the ENTITY
	// resolved and leaves the scene not playing, which is retail's `"Scene: %s"` arm.
	//
	// Retail resolves the handle THREE times here and checks the serial at `+0x8` on one of them and
	// at `+0x4` on the other two — a transcription slip in the shipped code that makes the two
	// resolutions disagree for a recycled handle. Reproduced as one resolve, which is the same
	// answer for every live handle.
	OutSceneName.Reset();
	bOutScenePlaying = false;
	OutSceneTime = 0.0;
	const FElysiumEntity* const Scene =
		World != nullptr ? World->Resolve(Dialogue.DialogScene) : nullptr;
	if (Scene == nullptr)
	{
		return false;
	}
	OutSceneName = NpcKernelDebug10Shared::GDebug10DebugName(Scene);
	return true;
}

