#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29d, family **Debug10** — the debug ring, the trace messages, the seams, and every slot-124
// TEXT overlay body of layers 10–18. `ElysiumNpcKernelDebug10_2.cpp` carries the slot-123 GEOMETRY
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
	constexpr int32 GDebug10BitText = 0x1;             // 0x1029d4ff `TEST AL,0x1`
	constexpr int32 GDebug10BitSquad = 0x80000;        // 0x102767ef
	constexpr int32 GDebug10BitCollisionBox = 0x1000;  // 0x1029cb39 `TEST AH,0x10`
	constexpr int32 GDebug10BitTaskList = 0x100000;    // 0x10276dfc
	constexpr int32 GDebug10BitConditions = 0x10000000;// 0x1029d9cb
	constexpr int32 GDebug10BitWeaponRings = 0x20000000;// 0x1029cd68
	constexpr int32 GDebug10BitViewCones = 0x400000;   // 0x1029ca59
	constexpr int32 GDebug10BitZombieConds = 0x40000;  // 0x103e0e9a

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

	// --- The census addresses the two slot methods dispatch on -----------------------------------
	constexpr TCHAR GDebug10Body_BaseText[] = TEXT("0x102767d0");
	constexpr TCHAR GDebug10Body_TroikaText[] = TEXT("0x1029d4e0");
	constexpr TCHAR GDebug10Body_CrowText[] = TEXT("0x10358f90");
	constexpr TCHAR GDebug10Body_HengeyokaiText[] = TEXT("0x10383560");
	constexpr TCHAR GDebug10Body_NewscasterText[] = TEXT("0x103a1250");
	constexpr TCHAR GDebug10Body_TzimisceText[] = TEXT("0x103c08d0");
	constexpr TCHAR GDebug10Body_ZombieText[] = TEXT("0x103e0e80");
	constexpr TCHAR GDebug10Body_ScriptedTargetText[] = TEXT("0x1034ddf0");

	// --- Retail's `NDebugOverlay` entry points, by name, so a captured line names its call --------
	constexpr TCHAR GDebug10Box[] = TEXT("NDebugOverlay::Box");
	constexpr TCHAR GDebug10BoxAngles[] = TEXT("NDebugOverlay::BoxAngles");
	constexpr TCHAR GDebug10BoxDirection[] = TEXT("NDebugOverlay::BoxDirection");
	constexpr TCHAR GDebug10Line[] = TEXT("NDebugOverlay::Line");
	constexpr TCHAR GDebug10Text[] = TEXT("NDebugOverlay::Text");
	constexpr TCHAR GDebug10Circle[] = TEXT("NDebugOverlay::Circle");

	// --- `CAI_BaseNPC::DrawDebugTextOverlays` `0x102767d0` — the `.rdata` it prints ---------------
	constexpr TCHAR GDebug10FmtHealth[] = TEXT("Health: %i");            // 0x105cc8cc
	constexpr TCHAR GDebug10FmtSquad[] = TEXT("Squad: %c : ");           // 0x105cc8bc
	constexpr TCHAR GDebug10SquadNone[] = TEXT(" - \n");                 // 0x105cc8b4, a 4-char string
	constexpr TCHAR GDebug10EnemyPrefix[] = TEXT("Enemy: ");             // 0x105cc8a8
	constexpr TCHAR GDebug10Newline[] = TEXT("\n");                      // 0x10547e40
	constexpr TCHAR GDebug10FmtSlot[] = TEXT("Slot:  %s \n");            // 0x105cc898
	constexpr TCHAR GDebug10FmtMem[] = TEXT("MEM%02d: %s");              // 0x105cc888
	constexpr TCHAR GDebug10FmtWeapon[] = TEXT("Weapon: %s (%d/%d) (%d/%d)"); // 0x105cc868
	constexpr TCHAR GDebug10Unarmed[] = TEXT("UNARMED");                 // 0x105cc85c
	constexpr TCHAR GDebug10FmtStat[] = TEXT("Stat: %s, ");              // 0x105cc84c
	constexpr TCHAR GDebug10FmtMove[] = TEXT("Move: %s, ");              // 0x105cc83c
	constexpr TCHAR GDebug10FmtSchd[] = TEXT("Schd: %s, ");              // 0x105cc82c
	constexpr TCHAR GDebug10Unknown[] = TEXT("Unknown");                 // 0x1053c828
	constexpr TCHAR GDebug10FmtTaskRow[] = TEXT("%s%s%s%s");             // 0x105cc804
	constexpr TCHAR GDebug10TaskLead[] = TEXT("->");                     // 0x105cc800
	constexpr TCHAR GDebug10TaskNoLead[] = TEXT("   ");                  // 0x105cc824
	constexpr TCHAR GDebug10TaskTrail[] = TEXT("<-");                    // 0x105cc828
	constexpr TCHAR GDebug10TaskFirst[] = TEXT("Task:");                 // 0x105cc81c
	constexpr TCHAR GDebug10TaskRest[] = TEXT("       ");                // 0x105cc810
	constexpr TCHAR GDebug10TaskNone[] = TEXT("Task: None");             // 0x105cc7dc
	constexpr TCHAR GDebug10FmtTask[] = TEXT("Task: %s (#%d), ");        // 0x105cc7ec
	constexpr TCHAR GDebug10FmtActv[] = TEXT("Actv: %s (%s)\n");         // 0x105cc7c8
	constexpr TCHAR GDebug10ActvInvalid[] = TEXT("Actv: INVALID");       // 0x105860c0
	constexpr TCHAR GDebug10ActvReset[] = TEXT("Actv: RESET");           // 0x105860d0
	constexpr TCHAR GDebug10FmtIntr[] = TEXT("Intr: %s (%s)\n");         // 0x105cc7b4
	constexpr TCHAR GDebug10FmtFail[] = TEXT("Fail: %s (%s)\n");         // 0x105cc7a0
	constexpr TCHAR GDebug10EnemyTooFar[] = TEXT("Enemy too far to attack"); // 0x105cc784
	constexpr TCHAR GDebug10FmtVel[] =
		TEXT("Vel %.1f %.1f %.1f   Ang: %.1f %.1f %.1f\n");               // 0x105cc750

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
	constexpr TCHAR GDebug10FmtMorale[] = TEXT("morale: %d");            // 0x10628c84
	constexpr TCHAR GDebug10FmtCrowEnemy[] = TEXT("enemy (dist): %s (%g)");// 0x10628c68
	constexpr TCHAR GDebug10FmtTzimisceBody[] = TEXT("Body - %5.1f|%5.1f|%s"); // 0x1065c904
	constexpr TCHAR GDebug10FmtZombieCond[] = TEXT("Cond: %s\n");        // 0x10665864

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
	constexpr int32 GDebug10RingWrapAt = 0x3dff;

	// `_DAT_104454c4` = 0.0, the floor every clamp in this band compares against.
	constexpr float GDebug10Zero = 0.f;

	// `_DAT_1047a3ac` = 160.0 — the distance the Tzimisce body line must exceed before it latches.
	constexpr float GDebug10TzimisceLatchUnits = 160.f;

	// `_DAT_1093d01c` and `DAT_1093cd70`: the cross-NPC latch pair `CNPC_VTzimisce#124` writes. They
	// are CLASS statics in retail — every Tzimisce in the map shares one distance and one schedule
	// name — so they are file statics here and not per-instance state. That is the recovery.
	float GDebug10TzimisceLatchDistance = 0.f;
	FString GDebug10TzimisceLatchSchedule;

	// `GetDebugName()` (`0x1000b5cd`): `m_iName` when set, the classname otherwise, and the empty
	// string (`DAT_106b8540`) for a null pointer on either. `"NULL ENTITY"` (`0x105387dc`) is the
	// scope-trace spelling for a null `this`, not this one's.
	FString GDebug10DebugName(const FElysiumEntity* Entity)
	{
		if (Entity == nullptr)
		{
			return FString();
		}
		if (!Entity->TargetName.IsEmpty())
		{
			return Entity->TargetName;
		}
		return Entity->Def != nullptr ? Entity->Def->Classname : FString();
	}

	// `CBaseEntity::GetClassname()` — the classname alone, which is what `CNPC_Crow#124` prints and
	// is NOT `GetDebugName`.
	FString GDebug10Classname(const FElysiumEntity* Entity)
	{
		if (Entity == nullptr || Entity->Def == nullptr)
		{
			return FString();
		}
		return Entity->Def->Classname;
	}

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

int32 FElysiumNpc::DebugLogRingAdvance(int32 Cursor, int32 Written, bool& bOutWrapped)
{
	// `0x1027ef20`'s cursor arm verbatim:
	//
	//     n = sprintf(this + 0x1b4e + cursor, text);
	//     cursor += n;
	//     if (0x3dff < cursor) {
	//         memset(this + 0x1b4e + cursor, 0, 0x4000 - cursor);
	//         *(byte*)(this + 0x5b54) = 1;
	//         cursor = 0;
	//     }
	//
	// The test is `>` against 0x3dff, so a cursor landing EXACTLY on 0x3dff does not wrap and a
	// cursor at 0x3e00 does; the zero-fill length is computed from the POST-advance cursor, so a
	// cursor past 0x4000 memsets a negative length, which retail's `rep stosd` treats as a very large
	// unsigned one. That overrun is retail's and is not reproduced — there is no ring to overrun.
	bOutWrapped = false;
	int32 Next = Cursor + Written;
	if (Next > GDebug10RingWrapAt)
	{
		bOutWrapped = true;
		Next = 0;
	}
	return Next;
}

void FElysiumNpc::AppendDebugLogLine(const TCHAR* Text)
{
	// `0x1027ef20`. A null line does nothing at all — the whole body is under `if (param_1 != 0)`.
	//
	// ABSENT (story 29b): the 16 KB ring at `+0x1b4e` and its cursor/latch at `+0x5b50`/`+0x5b54`.
	// The LINE is what a program can observe of this body, and it goes to the one channel this
	// runtime has. `DebugLogRingAdvance` above carries the cursor rule.
	if (Text == nullptr)
	{
		return;
	}
	EmitDevMsg(TEXT("0x1027ef20"), FString(Text));
}

void FElysiumNpc::AppendGlobalDebugLogLine(const TCHAR* Text) const
{
	// `0x1027ee20`, the GLOBAL trace ring slots 17 and 19 use. Seventy-nine bytes and the same shape
	// as `0x1027ef20` over a buffer that is not the NPC's. ABSENT for the same reason; story 29c-1's
	// slot-19 body (`ElysiumNpcKernelBaseHelpers.cpp`) already records it.
	if (Text == nullptr)
	{
		return;
	}
	EmitDevMsg(TEXT("0x1027ee20"), FString(Text));
}

void FElysiumNpc::DumpDebugLogRing() const
{
	// `0x1027efb0` — the dump `NPCThinkDebugPre`'s tail runs, walking the 16 KB buffer from the
	// `+0x5b50` cursor in 512-byte chunks and printing each. ABSENT: there is no ring to walk, and
	// the row's own verdict in band 0–4 is `mechanism → UE_LOG`, which is where every line the ring
	// would have held has already gone. Recorded so the arm is visible in a capture.
	EmitDevMsg(TEXT("0x1027efb0"), TEXT("0x1027efb0"));
}

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
// Slot 124 — the dispatcher and the eight arms.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::DrawDebugTextOverlays()
{
	// slot 124. Eight retail bodies fill it across the census and this leaf resolves between them by
	// the address the census says fills the slot for this NPC's retail class, exactly as story
	// 29c-1's slot-76 dispatcher does. `CNPC_VCop` has a NULL classname list in the census, so a
	// spawned `npc_VCop` answers a null `RetailClass()` and lands on the Troika-line body — which is
	// the recovered answer, not a gap (see `docs/specs/0002-npc-ai/spec.md`, story 29c-1's cleanup).
	const TCHAR* const SlotBody = ElysiumNpcKernelClass::BodyOf(RetailClass(), 124);
	if (SlotBody != nullptr)
	{
		if (FCString::Strcmp(SlotBody, GDebug10Body_CrowText) == 0)
		{
			return CrowDrawDebugTextOverlays();
		}
		if (FCString::Strcmp(SlotBody, GDebug10Body_HengeyokaiText) == 0)
		{
			return HengeyokaiDrawDebugTextOverlays();
		}
		if (FCString::Strcmp(SlotBody, GDebug10Body_NewscasterText) == 0)
		{
			return NewscasterDrawDebugTextOverlays();
		}
		if (FCString::Strcmp(SlotBody, GDebug10Body_TzimisceText) == 0)
		{
			return TzimisceDrawDebugTextOverlays();
		}
		if (FCString::Strcmp(SlotBody, GDebug10Body_ZombieText) == 0)
		{
			return ZombieDrawDebugTextOverlays();
		}
		if (FCString::Strcmp(SlotBody, GDebug10Body_ScriptedTargetText) == 0)
		{
			// `CScriptedTarget#124` (`0x1034ddf0`) is story 29c-1's body, in band 0–4. Dispatched
			// to rather than re-ported.
			return ScriptedTargetDrawDebugTextOverlays();
		}
	}
	return TroikaDrawDebugTextOverlays();
}

int32 FElysiumNpc::BaseDrawDebugTextOverlays()
{
	// `0x102767d0`, 2,872 bytes, arm by arm and in retail's order. Recovered from the LISTING: the
	// decompiler lost eleven of the sixteen format strings to the 0x408-byte stack frame and mislaid
	// the whole of the weapon line's argument order.
	//
	// **Four corrections to the checklist's walk, all from the listing.** (1) The `0x80000` arm emits
	// FOUR lines, not three: the first is `"Health: %i"` and the walk omits it, which is why the
	// index advances by four. (2) The squad format is `"Squad: %c : "` and carries NO `%s` — the
	// squad name is `strncat`ed after it, and the no-squad literal is `" - \n"` (`0x105cc8b4`), not
	// `"none"`. (3) The `0x1` arm opens with a SECOND `"Health: %i"` line, immediately after the
	// memory walk. (4) The activity line is `"Actv: %s (%s)\n"`, the same string the stat overlay
	// uses, and the velocity line is `"Vel %.1f …"` with no colon.
	//
	// The starting index is `CBaseCombatCharacter::DrawDebugTextOverlays()`'s answer; story 29c-1's
	// `EntityDrawDebugTextOverlays` is that seam (the chain from `CBaseCombatCharacter` down to
	// `CBaseEntity` adds nothing this runtime stands) and answers 0.
	int32 Line = EntityDrawDebugTextOverlays();

	// --- Arm 1, `0x80000` — health, squad, enemy, squad slot -------------------------------------
	if ((DebugOverlays & GDebug10BitSquad) != 0)
	{
		// 1. `Q_snprintf(buf, 512, "Health: %i", m_iHealth)`.
		EmitEntityText(Line, GDebug10FmtHealth, FString::Printf(GDebug10FmtHealth, Health));
		++Line;

		// 2. `Q_snprintf(buf, 512, "Squad: %c : ", ch)` where the character is chosen by
		//    `SETLE AL; DEC AL; AND AL,9; ADD AL,0x4f` over `m_iSquadDisconnected` — 'O' (0x4f) when
		//    it is at or below zero and 'X' (0x58) when it is above. Then `m_pSquad`'s name, or the
		//    literal `" - \n"` when there is no squad; the name arm appends `"\n"` after it.
		const TCHAR SquadMark = ScheduleHost.SquadDisconnected < 1 ? TEXT('O') : TEXT('X');
		FString SquadLine = FString::Printf(GDebug10FmtSquad, SquadMark);
		FString SquadNameText;
		if (!SquadObjectName(SquadNameText))
		{
			SquadLine += GDebug10SquadNone;
		}
		else
		{
			SquadLine += SquadNameText;
			SquadLine += GDebug10Newline;
		}
		EmitEntityText(Line, GDebug10FmtSquad, MoveTemp(SquadLine));
		++Line;

		// 3. `Q_strncpy(buf, "Enemy: ", 512)`, then the enemy's `m_iName` (`+0x26c`) when set else its
		//    `m_iClassname` (`+0x11c`), both with the empty string for a null pointer, then `"\n"`.
		//    With no enemy the SAME `" - \n"` block is written straight over the terminator.
		FString EnemyLine(GDebug10EnemyPrefix);
		const FElysiumEntity* Enemy = GetEnemy();
		if (Enemy == nullptr)
		{
			EnemyLine += GDebug10SquadNone;
		}
		else
		{
			EnemyLine += GDebug10DebugName(Enemy);
			EnemyLine += GDebug10Newline;
		}
		EmitEntityText(Line, GDebug10EnemyPrefix, MoveTemp(EnemyLine));
		++Line;

		// 4. `Q_snprintf(buf, 512, "Slot:  %s \n", SquadSlotName(m_iMySquadSlot))` — slot 546, family
		//    Squad's body, over `+0x5dac`.
		const TCHAR* const SlotName = SquadSlotName(MySquadSlot);
		EmitEntityText(Line, GDebug10FmtSlot,
			FString::Printf(GDebug10FmtSlot, SlotName != nullptr ? SlotName : TEXT("")));
		++Line;
	}

	// --- Everything below is under `m_debugOverlays & 1` ------------------------------------------
	if ((DebugOverlays & GDebug10BitText) == 0)
	{
		return Line;
	}

	// --- Arm 2, the enemy-memory walk -------------------------------------------------------------
	//
	//     for (mem = GetEnemies()->+0xc, i = 0; mem; mem = mem->+0x38, ++i)
	//         if (mem->+0x24 resolves)
	//             Q_snprintf(buf, 512, "MEM%02d: %s", i, GetDebugName(ent));
	//
	// The record ORDINAL is what `%02d` prints, and it advances for every record including the ones
	// whose handle no longer resolves — `INC EDI` sits outside the `if`. The line index advances only
	// for the printed ones. This runtime's `FElysiumNpcEnemyMemory` IS `CAI_Memory`'s list.
	{
		int32 RecordIndex = 0;
		for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
		{
			const FElysiumEntity* Remembered =
				World != nullptr ? World->Resolve(Record.Handle) : nullptr;
			if (Remembered != nullptr)
			{
				EmitEntityText(Line, GDebug10FmtMem,
					FString::Printf(GDebug10FmtMem, RecordIndex, *GDebug10DebugName(Remembered)));
				++Line;
			}
			++RecordIndex;
		}
	}

	// --- Arm 3, the second health line ------------------------------------------------------------
	EmitEntityText(Line, GDebug10FmtHealth, FString::Printf(GDebug10FmtHealth, Health));
	++Line;

	// --- Arm 4, the weapon line -------------------------------------------------------------------
	//
	//     if (!GetActiveWeapon()) Q_snprintf(buf, 512, "UNARMED");
	//     else {
	//         secondary = wpn->m_iSecondaryAmmoType (+0x748) < 0 ? -1 : GetAmmoCount(that);
	//         primary   = wpn->m_iPrimaryAmmoType   (+0x744) < 0 ? -1 : GetAmmoCount(that);
	//         Q_snprintf(buf, 512, "Weapon: %s (%d/%d) (%d/%d)",
	//                    wpn->vtable[0x570](), wpn->m_iClip1 (+0x74c), primary,
	//                    wpn->m_iClip2 (+0x750), secondary);
	//     }
	//
	// The listing's push order is what settles the pairing: clip1 goes with the PRIMARY ammo count
	// and clip2 with the SECONDARY, and the two negative-type guards answer -1 without asking for a
	// count at all. The checklist's walk had the two indices the other way round.
	{
		FString WeaponName;
		int32 Clip1 = 0;
		int32 Ammo1 = 0;
		int32 Clip2 = 0;
		int32 Ammo2 = 0;
		if (!ActiveWeaponTextWords(WeaponName, Clip1, Ammo1, Clip2, Ammo2))
		{
			EmitEntityText(Line, GDebug10Unarmed, GDebug10Unarmed);
		}
		else
		{
			EmitEntityText(Line, GDebug10FmtWeapon,
				FString::Printf(GDebug10FmtWeapon, *WeaponName, Clip1, Ammo1, Clip2, Ammo2));
		}
		++Line;
	}

	// --- Arm 5, the state line, `"Stat: %s, "` with slot 406 over `m_NPCState` -------------------
	{
		const TCHAR* const StateName = GetStateName(GetMind().State());
		EmitEntityText(Line, GDebug10FmtStat,
			FString::Printf(GDebug10FmtStat, StateName != nullptr ? StateName : TEXT("")));
		++Line;
	}

	// --- Arm 6, the movement line, `"Move: %s, "` with slot 407 over `GetNavType()` ---------------
	EmitEntityText(Line, GDebug10FmtMove,
		FString::Printf(GDebug10FmtMove, GetNavTypeName(RetailNavType())));
	++Line;

	// --- Arm 7, the schedule block ----------------------------------------------------------------
	if (Schedule.IsRunning())
	{
		// `"Schd: %s, "` with the program's name at `m_pSchedule + 0x40`, or `"Unknown"` for a null
		// name pointer.
		const TCHAR* const ScheduleName = ElysiumScheduleName(Schedule.Current);
		EmitEntityText(Line, GDebug10FmtSchd, FString::Printf(GDebug10FmtSchd,
			ScheduleName != nullptr ? ScheduleName : GDebug10Unknown));
		++Line;

		const FElysiumScheduleProgram* const Program = ElysiumScheduleFor(Schedule.Current);
		if ((DebugOverlays & GDebug10BitTaskList) != 0)
		{
			// One line per task, re-reading `m_pSchedule->numTasks` at the top of every iteration:
			//
			//     prefix = (i == 0) ? "Task:" : "       "
			//     if (i == m_ScheduleState) { lead = "->"; trail = "<-"; } else { lead = "   "; trail = ""; }
			//     Q_snprintf(buf, 512, "%s%s%s%s", prefix, lead, TaskName(tasks[i].iTask), trail)
			//
			// The step is named through `ElysiumTaskName` rather than slot 449, for the reason story
			// 29c-1 states at the identical block in `0x102775e0`: the task namespace is a seam.
			if (Program != nullptr)
			{
				for (int32 Index = 0; Index < Program->Tasks.Num(); ++Index)
				{
					const TCHAR* const Prefix = Index == 0 ? GDebug10TaskFirst : GDebug10TaskRest;
					const bool bCurrent = Index == Schedule.TaskIndex;
					const TCHAR* const Lead = bCurrent ? GDebug10TaskLead : GDebug10TaskNoLead;
					const TCHAR* const Trail = bCurrent ? GDebug10TaskTrail : TEXT("");
					EmitEntityText(Line, GDebug10FmtTaskRow, FString::Printf(GDebug10FmtTaskRow,
						Prefix, Lead, *FElysiumScheduleCorpus::Get().TaskOps().NameOf(Program->Tasks[Index].TaskId), Trail));
					++Line;
				}
			}
		}
		else
		{
			// `GetCurTask()` (`0x1028a150`): `"Task: None"` for no task, else
			// `"Task: %s (#%d), "` with the task's name and `m_ScheduleState`.
			if (Program == nullptr || !Program->Tasks.IsValidIndex(Schedule.TaskIndex))
			{
				EmitEntityText(Line, GDebug10TaskNone, GDebug10TaskNone);
			}
			else
			{
				EmitEntityText(Line, GDebug10FmtTask, FString::Printf(GDebug10FmtTask,
					*FElysiumScheduleCorpus::Get().TaskOps().NameOf(Program->Tasks[Schedule.TaskIndex].TaskId), Schedule.TaskIndex));
			}
			++Line;
		}
	}

	// --- Arm 8, the activity line -----------------------------------------------------------------
	//
	//     if (m_Activity == -1 || m_IdealActivity == -1)
	//         -> m_Activity == 0 ? "Actv: RESET" : "Actv: INVALID"
	//     else if (m_Activity == 0) -> "Actv: RESET"
	//     else "Actv: %s (%s)\n" through slots 0x5dc / 0x5f4 / 0x5e0 then SelectWeightedSequence and
	//          GetSequenceActivityName for the current and the ideal.
	//
	// This line prints UNCONDITIONALLY — it is outside the schedule block, so an NPC with no program
	// still advances the index by one here.
	if (ActivityNumber == INDEX_NONE || IdealActivityNumber == INDEX_NONE)
	{
		const TCHAR* const Text = ActivityNumber == 0 ? GDebug10ActvReset : GDebug10ActvInvalid;
		EmitEntityText(Line, Text, Text);
	}
	else if (ActivityNumber == 0)
	{
		EmitEntityText(Line, GDebug10ActvReset, GDebug10ActvReset);
	}
	else
	{
		EmitEntityText(Line, GDebug10FmtActv, FString::Printf(GDebug10FmtActv,
			*ActivityNameForNumber(ActivityNumber), *ActivityNameForNumber(IdealActivityNumber)));
	}
	++Line;

	// --- Arm 9, the two scheduling-diagnostic lines -----------------------------------------------
	//
	//     if (m_interuptSchedule (+0x5f3c)) "Intr: %s (%s)\n" with its name and m_interruptText (+0x5f34)
	//     if (m_failedSchedule  (+0x5f38)) "Fail: %s (%s)\n" with its name and m_failText      (+0x5f30)
	//
	// The checklist called these "two conditional lines gated on two non-zero ints just past the
	// decompiler's view"; the listing names all four words and both strings. A null schedule NAME
	// prints `"Unknown"`; the text pointer is printed raw, so a null one reaches `printf` as a null
	// `%s` — which is the empty string here.
	if (ScheduleHost.InterruptSchedule != ElysiumScheduleId::None)
	{
		const TCHAR* const Name = ElysiumScheduleName(ScheduleHost.InterruptSchedule);
		EmitEntityText(Line, GDebug10FmtIntr, FString::Printf(GDebug10FmtIntr,
			Name != nullptr ? Name : GDebug10Unknown, *ScheduleHost.InterruptText));
		++Line;
	}
	if (ScheduleHost.FailedSchedule != ElysiumScheduleId::None)
	{
		const TCHAR* const Name = ElysiumScheduleName(ScheduleHost.FailedSchedule);
		EmitEntityText(Line, GDebug10FmtFail, FString::Printf(GDebug10FmtFail,
			Name != nullptr ? Name : GDebug10Unknown, *ScheduleHost.FailText));
		++Line;
	}

	// --- Arm 10, `COND_ENEMY_TOO_FAR` -------------------------------------------------------------
	//
	// Printed as a bare literal, with no `Q_snprintf` at all.
	if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyTooFar))
	{
		EmitEntityText(Line, GDebug10EnemyTooFar, GDebug10EnemyTooFar);
		++Line;
	}

	// --- Arm 11, the velocity line ----------------------------------------------------------------
	//
	//     if (m_iEFlags & EFL_DIRTY_ABSVELOCITY) CalcAbsoluteVelocity();
	//     if (m_vecAbsVelocity != vec3_origin || m_vecAngVelocity != vec3_angle) {
	//         CalcAbsoluteVelocity();  CalcAbsoluteVelocity();  CalcAbsoluteVelocity();   // x3, again
	//         Q_snprintf(buf, 512, "Vel %.1f %.1f %.1f   Ang: %.1f %.1f %.1f\n", …);
	//     }
	//
	// `DAT_1070d1b0`/`DAT_1070d9d0` are `vec3_origin` and `vec3_angle`, both BSS zero vectors, so the
	// gate is "either velocity is non-zero". The four recompute calls are an artefact of the SDK body
	// this was cut down from — each of the six arguments re-asks under its own flag test — and the
	// port has no dirty-velocity flag, so they are recorded here and not made.
	{
		const FVector VelUnits = Velocity / ElysiumMove::U;
		const FVector& AngVel = AngularVelocity;
		if (!VelUnits.IsZero() || !AngVel.IsZero())
		{
			EmitEntityText(Line, GDebug10FmtVel, FString::Printf(GDebug10FmtVel,
				VelUnits.X, VelUnits.Y, VelUnits.Z, AngVel.X, AngVel.Y, AngVel.Z));
			++Line;
		}
	}

	return Line;
}

int32 FElysiumNpc::TroikaDrawDebugTextOverlays()
{
	// `0x1029d4e0`, 3,754 bytes, recovered from the LISTING. Every one of its twenty-odd lines uses
	// the same eight-argument `NDebugOverlay::EntityText(IndexOfEdict(edict), line, text, 0.0, 255,
	// 255, 255, 255)` shape; `EmitEntityText` is that seam and the colour is not carried because it
	// is white and opaque on every call site.
	int32 Line = BaseDrawDebugTextOverlays();
	if ((DebugOverlays & GDebug10BitText) == 0)
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
		EmitEntityText(Line, GDebug10EnemyTooFar, GDebug10EnemyTooFar);
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
				Buffer += GDebug10Newline;
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
				Buffer += GDebug10Newline;
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
		Buffer += GDebug10Newline;
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
			Buffer += GDebug10Newline;
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
		Buffer += GDebug10Newline;
		EmitEntityText(Line, GDebug10FmtCondWatch, MoveTemp(Buffer));
		++Line;
	}
	return Line;
}

// -------------------------------------------------------------------------------------------------
// The five species arms of slot 124.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::CrowDrawDebugTextOverlays()
{
	// `0x10358f90`, 259 bytes. The one body in the census that chains the BASE (`0x102767d0`) and NOT
	// the Troika one — a crow never gets the sequence, pose, disposition or condition lines.
	const int32 Base = BaseDrawDebugTextOverlays();
	if ((DebugOverlays & GDebug10BitText) == 0)
	{
		return Base;
	}

	// `Q_snprintf(buf, 512, "morale: %d", m_nMorale)`.
	EmitEntityText(Base, GDebug10FmtMorale, FString::Printf(GDebug10FmtMorale, CrowMorale()));
	int32 Line = Base + 1;

	// Only with an enemy: `"enemy (dist): %s (%g)"` with the enemy's CLASSNAME (not its debug name)
	// and the cached distance at `+0x5f4c`. Retail pushes the float FIRST and re-dispatches
	// `GetEnemy()` afterwards for the classname, so a body that changed enemies between the two
	// would print the new one's name beside the old one's distance; reproduced as one resolve,
	// which is the same answer for every NPC that does not.
	if (GetEnemy() != nullptr)
	{
		const float DistUnits = CrowEnemyDistUnits();
		EmitEntityText(Line, GDebug10FmtCrowEnemy,
			FString::Printf(GDebug10FmtCrowEnemy, *GDebug10Classname(GetEnemy()), DistUnits));
		return Base + 2;
	}
	return Line;
}

int32 FElysiumNpc::HengeyokaiDrawDebugTextOverlays()
{
	// `0x10383560`, 106 bytes: the Troika body, then slot 9's string on one further line under bit 0.
	// The string is taken from slot 9's returned object's first word and the empty string
	// (`DAT_106b8540`) substitutes for a null one; retail prints it with NO format string at all.
	// The `+1` IS the contract: it is the budget every later overlay consumes.
	const int32 Base = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & GDebug10BitText) == 0)
	{
		return Base;
	}
	const FString Slot9 = HengeyokaiSlot9String();
	EmitEntityText(Base, TEXT("0x10383560"), Slot9);
	return Base + 1;
}

int32 FElysiumNpc::NewscasterDrawDebugTextOverlays()
{
	// `0x103a1250`, 151 bytes. A scope-trace push carrying `GetDebugName()` — `"NULL ENTITY"`
	// (`0x105387dc`) for a null `this`, the empty string for a null `m_iName` — then the Troika body,
	// then `0x103a0ff0`'s lines. `0x103a0ff0` returns a COUNT and the newscaster adds it to the
	// Troika body's answer, so the two share one budget.
	UE_LOG(LogElysiumNpcEnt, VeryVerbose, TEXT("CNPC_VNewscaster::DrawDebugTextOverlays %s"),
		TargetName.IsEmpty() ? TEXT("") : *TargetName);
	const int32 Base = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & GDebug10BitText) == 0)
	{
		return Base;
	}
	return Base + NewscasterStoryOverlayLines(Base);
}

int32 FElysiumNpc::TzimisceDrawDebugTextOverlays()
{
	// `0x103c08d0`, 387 bytes.
	//
	//     dist = 0.0;                                              // _DAT_104454c4
	//     if (m_hPickupTarget (+0x6670) resolves)
	//         dist = |target->GetAbsOrigin() - GetAbsOrigin()|;     // the full 3-D length
	//     if (0x103be130() && _DAT_1047a3ac < dist) {               // 160.0
	//         _DAT_1093d01c = dist;
	//         if (m_pSchedule) strcpy(DAT_1093cd70, m_pSchedule->name);
	//     }
	//     Q_snprintf(buf, 512, "Body - %5.1f|%5.1f|%s", dist, _DAT_1093d01c, DAT_1093cd70);
	//
	// The two globals are a CROSS-NPC latch — every Tzimisce in the map writes and reads the same
	// pair — so they are file statics here and not per-instance state. The checklist's walk spelled
	// the format `"Body: %5.1f %5.1f %s"`; the image says `"Body - %5.1f|%5.1f|%s"`.
	const int32 Base = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & GDebug10BitText) == 0)
	{
		return Base;
	}

	float Distance = GDebug10Zero;
	const FElysiumEntity* const Carried =
		World != nullptr ? World->Resolve(PickupTarget) : nullptr;
	if (Carried != nullptr)
	{
		Distance = static_cast<float>((Carried->Origin - Origin).Size() / ElysiumMove::U);
	}
	if (TzimisceIsCarryingBody() && Distance > GDebug10TzimisceLatchUnits)
	{
		GDebug10TzimisceLatchDistance = Distance;
		if (Schedule.IsRunning())
		{
			const TCHAR* const Name = ElysiumScheduleName(Schedule.Current);
			GDebug10TzimisceLatchSchedule = Name != nullptr ? Name : TEXT("");
		}
	}
	EmitEntityText(Base, GDebug10FmtTzimisceBody, FString::Printf(GDebug10FmtTzimisceBody,
		Distance, GDebug10TzimisceLatchDistance, *GDebug10TzimisceLatchSchedule));
	return Base + 1;
}

int32 FElysiumNpc::ZombieDrawDebugTextOverlays()
{
	// `0x103e0e80`, 224 bytes. The Troika body, then — under `m_debugOverlays & 0x40000`, which is
	// NOT bit 0 — one line per set bit of the 0..0xbf bitfield at `+0x5c5c`:
	//
	//     global = (id == -1) ? -1 : id + 1000000000;
	//     local  = ConditionGlobalToLocal(GetClassScheduleIdSpace() + 0x30, global);   // 0x102ea280
	//     Q_snprintf(buf, 512, "Cond: %s\n", ConditionName(local));                    // slot 458
	//
	// The `id == -1` arm is unreachable (the loop starts at 0) and is recorded rather than written.
	// The 1e9 offset is the same script-range constant slot 458 tests against, so the pair
	// global-to-local then local-to-global is the identity for every base condition, which is what
	// `ConditionName` is handed here.
	int32 Line = TroikaDrawDebugTextOverlays();
	if ((DebugOverlays & GDebug10BitZombieConds) == 0)
	{
		return Line;
	}
	for (int32 Id = 0; Id < 0xc0; ++Id)
	{
		if (!ZombieConditionBit(Id))
		{
			continue;
		}
		const TCHAR* const Name = ConditionName(Id);
		EmitEntityText(Line, GDebug10FmtZombieCond,
			FString::Printf(GDebug10FmtZombieCond, Name != nullptr ? Name : TEXT("")));
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

bool FElysiumNpc::ActiveWeaponTextWords(FString& OutName, int32& OutClip1, int32& OutAmmo1,
	int32& OutClip2, int32& OutAmmo2) const
{
	// SEAM for the weapon line's five words. `ActiveWeaponEntity()` is null, so this answers false
	// and the line is retail's `UNARMED`.
	OutName.Reset();
	OutClip1 = -1;
	OutAmmo1 = -1;
	OutClip2 = -1;
	OutAmmo2 = -1;
	return ActiveWeaponEntity() != nullptr;
}

bool FElysiumNpc::SquadObjectName(FString& OutName) const
{
	// `m_pSquad` (`+0x5da4`, ABSENT in the shape map) and its name at `+0x4`. Retail reads the squad
	// OBJECT here without the `m_iSquadDisconnected` gate `ConnectedSquad()` applies — the gate only
	// decides the `%c`. `CAI_BaseNPC::InitSquad` stands a squad object for any NPC whose
	// `m_SquadName` (`+0x5da8`) is set and the object's `+0x4` IS that name, so the port's own
	// `SquadName` is the recovered answer.
	if (SquadName.IsEmpty())
	{
		return false;
	}
	OutName = SquadName;
	return true;
}

int32 FElysiumNpc::CrowMorale() const
{
	// SEAM for `CNPC_Crow::m_nMorale` (`+0x5f50`).
	return 0;
}

float FElysiumNpc::CrowEnemyDistUnits() const
{
	// SEAM for `CNPC_Crow::m_flEnemyDist` (`+0x5f4c`), the CACHED distance the crow's own
	// `GatherConditions` writes — not `EnemyDistUnits`, which is the Troika line's word.
	return 0.f;
}

FElysiumEntity* FElysiumNpc::CopPursuitPlayer() const
{
	// `CNPC_VCop::m_hPursuitPlayer` (`+0x6664`). **No longer a seam**: story 29d's family
	// SpeciesMisc10 landed the WRITER (`CNPC_VCop#597`, `0x10372cc0`) and the word with it, so this
	// resolves it. A cop that has not latched a pursuit still answers null, which is retail's own
	// answer for the `0xffffffff` the latch writes when the seen entity carries no player record.
	return World != nullptr ? World->Resolve(CopPursuitHandle) : nullptr;
}

bool FElysiumNpc::CopSuspectIs(const FElysiumEntity* Candidate) const
{
	// SEAM for the cop class's two statics, `DAT_1093ac3c` (the shared provoker handle) and
	// `_DAT_1093aca8` (its expiry). Family Senses10's `CNPC_VCop::OnSeeEntity` (`0x10370560`) is the
	// writer and family Conditions10's `CNPC_VCop::IRelationType` the other reader.
	(void)Candidate;
	return false;
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

bool FElysiumNpc::TzimisceIsCarryingBody() const
{
	// `0x103be130`, the carry probe the `Body - …` latch stands behind.
	//
	// **No longer a seam.** It was landed as one on the grounds that the body has "no port
	// counterpart", but the whole of it is
	//     `return (m_bfAINPCFlags [+0x14b8] >> 5) & 0xffffff01;`
	// — bit 5 of the NPC flag word, which story 29d (family Conditions10) recovered as
	// `CARRYING_BODY` alongside `FINDING_BODY` (0x10) while porting `0x103be090`/`0x103be050`. The
	// port carries that word, so the seam was refusing something it could answer. Ghidra's
	// `0xffffff01` mask is the `AL`-return artifact: the shift puts bit 5 in the low bit and only
	// the low bit is read.
	return NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY);
}

int32 FElysiumNpc::NewscasterStoryOverlayLines(int32 FirstLine)
{
	// SEAM for `0x103a0ff0`, family Species' row in band 5–9. Its answer is a COUNT of lines, which
	// the newscaster adds to the Troika body's line index.
	(void)FirstLine;
	return 0;
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
	OutSceneName = GDebug10DebugName(Scene);
	return true;
}

FString FElysiumNpc::HengeyokaiSlot9String() const
{
	// SEAM for slot 9's string, which `CNPC_VHengeyokai#124` prints verbatim. Slot 9 is a generated
	// stub owned by another story; the empty string is retail's null arm (`DAT_106b8540`).
	return FString();
}

bool FElysiumNpc::ZombieConditionBit(int32 ConditionId) const
{
	// SEAM for `CNPC_VZombie`'s bitfield at `+0x5c5c`, walked 0..0xbf. It is one of the schedule
	// block's six words and no port member carries it.
	(void)ConditionId;
	return false;
}
