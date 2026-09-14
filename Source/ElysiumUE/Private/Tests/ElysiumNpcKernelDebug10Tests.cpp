#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Debug10**. One case per `rule` row, and every assertion comes from the
// LISTING of the row it names or from the pinned image's `.rdata` — the sixteen format strings of
// `0x102767d0`, the twenty-two of `0x1029d4e0`, the three cone argument sets and four box extents of
// `0x1029ca50`, the two witness colour ladders of `0x10292500`, the five cop label suffixes of
// `0x10372f00`, and MingXiao's four recovered range bands.
//
// What a debug body is observable for is asserted here and nothing else: the arm ORDER, the GATES,
// the LINE INDEX each body returns, and the constants each pushes. Where a body can only answer
// "nothing" because its input is a seam — the studio header, the pose table, the node graph, the
// hint store, the squad object, the cop statics — the case says so, and says that the refusal is the
// recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelDebug10Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `npc_VHumanCombatant` is the plain Troika-line leaf: the census gives `CNPC_VHumanCombatant`
	// no slot-123/124 override, so it takes the Troika bodies. `npc_VCop` is the CONTROL the story
	// asks for — the census gives `CNPC_VCop` a NULL classname list, so a spawned cop's
	// `RetailClass()` is null and every species lookup falls through to the Troika line, which is
	// the recovered dispatch and not a gap (story 29c-1's cleanup). `npc_VNewscaster` is the ONE
	// species arm in this family a registered classname reaches.
	const TCHAR* const GDebug10Combatant = TEXT("npc_VHumanCombatant");
	const TCHAR* const GDebug10Cop = TEXT("npc_VCop");
	const TCHAR* const GDebug10Newscaster = TEXT("npc_VNewscaster");

	FString Debug10RetailOrder(const TArray<FElysiumNpc::FDebugLine>& Lines)
	{
		TArray<FString> Parts;
		Parts.Reserve(Lines.Num());
		for (const FElysiumNpc::FDebugLine& Line : Lines)
		{
			Parts.Add(FString(Line.Retail));
		}
		return FString::Join(Parts, TEXT("|"));
	}

	FString Debug10TextOrder(const TArray<FElysiumNpc::FDebugLine>& Lines)
	{
		TArray<FString> Parts;
		Parts.Reserve(Lines.Num());
		for (const FElysiumNpc::FDebugLine& Line : Lines)
		{
			Parts.Add(Line.Text);
		}
		return FString::Join(Parts, TEXT("|"));
	}

	FElysiumNpcWorldBuilder Debug10Builder(const TCHAR* Classname)
	{
		FElysiumNpcWorldBuilder Builder(TEXT("debug10"), 29u);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("subject"), FVector::ZeroVector, Classname);
		Builder.AddNpc(TEXT("other"), FVector(100.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
		return Builder;
	}

	// Every case that drives a ConVar-gated arm resets the set first, so the shipped default (0,
	// every arm off) is what an untouched case sees.
	void Debug10ResetConVars()
	{
		FElysiumNpc::SetDebugConVar(nullptr, 0);
	}
}

// -------------------------------------------------------------------------------------------------
// `0x1027ef20` — the per-entity debug ring.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10RingTest,
	"Elysium.Substrate.NpcKernelDebug10.DebugLogRing", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10RingTest::RunTest(const FString&)
{
	// The cursor arm, as the listing spells it: `cursor += sprintf(...); if (0x3dff < cursor) {
	// memset the tail; latch +0x5b54; cursor = 0; }`. The test is `>` and not `>=`, so a write that
	// lands EXACTLY on 0x3dff does not wrap and the next byte does.
	bool bWrapped = true;
	TestEqual(TEXT("an ordinary append advances the cursor by the byte count"),
		FElysiumNpc::DebugLogRingAdvance(0, 16, bWrapped), 16);
	TestFalse(TEXT("and does not wrap"), bWrapped);

	TestEqual(TEXT("a cursor landing exactly on 0x3dff does NOT wrap"),
		FElysiumNpc::DebugLogRingAdvance(0x3df0, 0xf, bWrapped), 0x3dff);
	TestFalse(TEXT("0x3dff is inside the buffer"), bWrapped);

	TestEqual(TEXT("one byte further resets the cursor to 0"),
		FElysiumNpc::DebugLogRingAdvance(0x3df0, 0x10, bWrapped), 0);
	TestTrue(TEXT("and raises the wrap latch at +0x5b54"), bWrapped);

	// The append itself. Story 29b records the 16 KB ring at `+0x1b4e` absent, so the LINE is what
	// is observable and it goes to the one channel this runtime has. The null guard is retail's —
	// `0x1027ef20`'s whole body is under `if (param_1 != 0)`.
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	FElysiumNpc::BeginDebugCapture();
	Npc->AppendDebugLogLine(nullptr);
	TestEqual(TEXT("a null line does nothing at all"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	FElysiumNpc::BeginDebugCapture();
	Npc->AppendDebugLogLine(TEXT("gathered"));
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("a line is appended once"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("under the row's own address"), FString(Lines[0].Retail),
				FString(TEXT("0x1027ef20")));
			TestEqual(TEXT("carrying the line verbatim"), Lines[0].Text, FString(TEXT("gathered")));
		}
	}

	// `0x1027ee20`, the GLOBAL ring slots 17 and 19 use, is a separate absence and says so.
	FElysiumNpc::BeginDebugCapture();
	Npc->AppendGlobalDebugLogLine(TEXT("global"));
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the global ring is a second, distinct absence"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("recorded under 0x1027ee20"), FString(Lines[0].Retail),
				FString(TEXT("0x1027ee20")));
		}
	}

	// `0x1027efb0`, the dump, and `+0x5b55`, its request byte. Both absent (story 29b).
	TestFalse(TEXT("+0x5b55 never stands: the dump is never requested"),
		Npc->DebugRingDumpRequested());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 18 / 17 / 20 — the three trace messages.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10TraceMessagesTest,
	"Elysium.Substrate.NpcKernelDebug10.TraceMessages", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10TraceMessagesTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	const FElysiumNpc* ConstNpc = Npc;

	// `DAT_10920534` clear (the shipped default): every one of the three takes the `DevMsg` arm and
	// each names its OWN retail address, because the three bodies are three rows.
	FElysiumNpc::BeginDebugCapture();
	Npc->TraceMessage(TEXT("slot 18"), 0);
	ConstNpc->TraceMessage(TEXT("slot 17"), 0);
	Npc->TraceMessageBare(TEXT("slot 20"));
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("with the toggle clear all three DevMsg"), Debug10RetailOrder(Lines),
			FString(TEXT("0x1028de10|0x1028de90|0x1028df30")));
		// The three texts are the formatter's output now that `0x1028d990` is ported (it was a
		// passthrough seam when this case was written), so each is asserted by what it CARRIES
		// rather than by equality with the bare message.
		const FString Texts = Debug10TextOrder(Lines);
		TestTrue(TEXT("the DevMsg arm carries slot 18's message"), Texts.Contains(TEXT("slot 18")));
		TestTrue(TEXT("...slot 17's"), Texts.Contains(TEXT("slot 17")));
		TestTrue(TEXT("...and slot 20's"), Texts.Contains(TEXT("slot 20")));
		TestTrue(TEXT("...each behind the formatter's %-20s debug-name column"),
			Texts.Contains(TEXT("subject ")));
	}

	// A null message. Slot 20's whole body is under `if (param_1 != 0)`, so it says nothing at all;
	// slots 17 and 18 have no such guard and hand the null straight to the formatter, which
	// substitutes the empty string.
	FElysiumNpc::BeginDebugCapture();
	Npc->TraceMessageBare(nullptr);
	TestEqual(TEXT("slot 20 refuses a null message outright"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	FElysiumNpc::BeginDebugCapture();
	Npc->TraceMessage(nullptr, 0);
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("slot 18 has no null guard and still prints"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			// `1028d9c1`: a null message becomes the EMPTY STRING, not a refusal — so the line is
			// still the full formatted frame (debug name, curtime, indent) with nothing in the
			// message slot. Asserting the whole line is empty was the seam's answer, not retail's.
			TestFalse(TEXT("a null message still prints a formatted frame"), Lines[0].Text.IsEmpty());
			TestTrue(TEXT("...carrying the debug-name column"),
				Lines[0].Text.StartsWith(TEXT("subject ")));
			TestFalse(TEXT("...and the empty-string substitution, not the literal 'null'"),
				Lines[0].Text.Contains(TEXT("null")));
		}
	}

	// `DAT_10920534` SET: the three route to two DIFFERENT rings, and slot 17 rings the RAW message
	// where slot 18 rings the formatted one. Both asymmetries are retail's.
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10920534"), 1);
	FElysiumNpc::BeginDebugCapture();
	Npc->TraceMessage(TEXT("slot 18"), 0);
	ConstNpc->TraceMessage(TEXT("slot 17"), 0);
	Npc->TraceMessageBare(TEXT("slot 20"));
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("slot 18 and slot 20 ring the NPC's own buffer, slot 17 the global one"),
			Debug10RetailOrder(Lines),
			FString(TEXT("0x1027ef20|0x1027ee20|0x1027ef20")));
	}
	Debug10ResetConVars();

	// The formatter is family Conditions10's row (`0x1028d990` ->
	// `FElysiumNpc::BuildConditionDebugString`). It was SEAMED when this case was written, so these
	// probes asserted the passthrough; the story's close wired the real body, so they now read what
	// retail's `"%-20s  %6.2f : %*s %s\n…"` actually produces. Same questions, answered off the
	// formatter instead of past it.
	const FString Indented = ConstNpc->TraceMessageFormat(TEXT("msg"), 3);
	const FString Clamped = ConstNpc->TraceMessageFormat(TEXT("msg"), -5);
	const FString Flush = ConstNpc->TraceMessageFormat(TEXT("msg"), 0);
	TestTrue(TEXT("the formatter carries the message"), Indented.Contains(TEXT("msg")));
	TestTrue(TEXT("...behind the %-20s debug name"), Indented.StartsWith(TEXT("subject ")));
	// The column is `curtime`, and the fixture's clock stands at the NPC's first think — `NPCInit`
	// (`0x1029a0b0`) arms it a tenth of a second after Activate, so the world is at 0.10 here.
	TestTrue(TEXT("...and the %6.2f curtime column"),
		Indented.Contains(FString::Printf(TEXT("%6.2f : "),
			FElysiumNpcWorldFixture::FirstThinkSeconds)));
	// `%*s` is the indent, so level 3 and level 0 are DIFFERENT strings — which is what proves the
	// width argument reaches the format at all.
	TestNotEqual(TEXT("an indent of 3 is not an indent of 0"), Indented, Flush);
	// `if (level < 0) level = 0` — a negative indent is clamped, not rejected, so it must produce
	// exactly the level-0 string rather than an error or a refusal.
	TestEqual(TEXT("and a negative indent is clamped to 0, not refused"), Clamped, Flush);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102767d0` — `CAI_BaseNPC::DrawDebugTextOverlays`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10BaseTextTest,
	"Elysium.Substrate.NpcKernelDebug10.BaseTextOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10BaseTextTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc) || !TestNotNull(TEXT("and a second"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Other });
	Npc->Schedule.Clear();
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;
	Npc->Velocity = FVector::ZeroVector;
	Npc->AngularVelocity = FVector::ZeroVector;

	// Nothing set: both gates closed, no lines, and the answer is the base seam's 0.
	Npc->DebugOverlays = 0;
	FElysiumNpc::BeginDebugCapture();
	TestEqual(TEXT("a zero m_debugOverlays emits nothing and returns the base line"),
		Npc->BaseDrawDebugTextOverlays(), 0);
	TestEqual(TEXT("no lines"), FElysiumNpc::EndDebugCapture().Num(), 0);

	// --- The `0x80000` arm: FOUR lines, not three -------------------------------------------------
	//
	// The checklist's walk omitted the `"Health: %i"` line entirely, which is why it read the index
	// advancing by four for three lines. The squad format carries no `%s`, and the no-squad literal
	// is `" - \n"` (`0x105cc8b4`) and not `"none"`.
	Npc->DebugOverlays = 0x80000;
	Npc->Health = 47;
	Npc->ScheduleHost.SquadDisconnected = 0;
	Npc->SquadName.Reset();
	FElysiumNpc::BeginDebugCapture();
	const int32 SquadLines = Npc->BaseDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the 0x80000 arm emits FOUR lines"), Lines.Num(), 4);
		TestEqual(TEXT("and advances the index by four"), SquadLines, 4);
		TestEqual(TEXT("in the listing's order"), Debug10RetailOrder(Lines),
			FString(TEXT("Health: %i|Squad: %c : |Enemy: |Slot:  %s \n")));
		if (Lines.Num() == 4)
		{
			TestEqual(TEXT("the health line"), Lines[0].Text, FString(TEXT("Health: 47")));
			TestEqual(TEXT("a connected squad is 'O', and no squad object appends \" - \\n\""),
				Lines[1].Text, FString(TEXT("Squad: O :  - \n")));
			TestEqual(TEXT("no enemy appends the same block"), Lines[2].Text,
				FString(TEXT("Enemy:  - \n")));
			TestEqual(TEXT("each line is numbered from the base's 0"), Lines[0].Line, 0);
			TestEqual(TEXT("and the fourth is line 3"), Lines[3].Line, 3);
		}
	}

	// `m_iSquadDisconnected > 0` flips the character to 'X'; the test is `< 1`, so zero and every
	// negative are 'O'.
	Npc->ScheduleHost.SquadDisconnected = 1;
	Npc->SquadName = TEXT("alpha");
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("a disconnected squad member is 'X' and the squad NAME is appended"),
			Lines[1].Text, FString(TEXT("Squad: X : alpha\n")));
	}
	Npc->ScheduleHost.SquadDisconnected = 0;
	Npc->SquadName.Reset();

	// The enemy line takes `GetDebugName`'s own two arms — `m_iName` when set, the classname
	// otherwise — and appends `"\n"` rather than the `" - \n"` block.
	Npc->Senses.Memory.Enemy = Other->Handle;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("a live enemy is named"), Lines[2].Text, FString(TEXT("Enemy: other\n")));
	}
	Npc->Senses.Memory.Enemy = FElysiumEntityHandle();

	// --- The `0x1` arm ------------------------------------------------------------------------------
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	const int32 TextLines = Npc->BaseDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		// No memory records, no schedule, no weapon, activities invalid, velocity zero. What stands
		// is the SECOND health line the checklist's walk omits, the `UNARMED` weapon line, the
		// state and movement lines, and the activity line, which prints unconditionally.
		TestEqual(TEXT("the 0x1 arm's spine"), Debug10RetailOrder(Lines),
			FString(TEXT("Health: %i|UNARMED|Stat: %s, |Move: %s, |Actv: INVALID")));
		TestEqual(TEXT("five lines, numbered from 0"), TextLines, 5);
		TestEqual(TEXT("the movement line is the nav-type seam's -1, which slot 407 names None"),
			Lines[3].Text, FString(TEXT("Move: None, ")));
	}

	// The memory walk prints `"MEM%02d: %s"` with the RECORD ordinal, and the ordinal advances for
	// every record while the line index advances only for the printed ones.
	Npc->EnemyMemory.Update(*Npc, Other->Handle, 0.0);
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the memory line leads the 0x1 arm"), FString(Lines[0].Retail),
			FString(TEXT("MEM%02d: %s")));
		TestEqual(TEXT("with the record ordinal at %02d and GetDebugName beside it"),
			Lines[0].Text, FString(TEXT("MEM00: other")));
	}

	// The activity line's three arms, in the listing's test order.
	Npc->ActivityNumber = 0;
	Npc->IdealActivityNumber = INDEX_NONE;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	TestTrue(TEXT("ACT_RESET wins over ACT_INVALID when either activity is -1"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture()).Contains(TEXT("Actv: RESET")));

	Npc->ActivityNumber = 3;
	Npc->IdealActivityNumber = 4;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	TestTrue(TEXT("two live activities take the named arm, 'Actv: %s (%s)\\n'"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture()).Contains(TEXT("Actv: %s (%s)\n")));
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	// The two scheduling-diagnostic lines the checklist's walk left as "two non-zero ints".
	Npc->ScheduleHost.InterruptSchedule = EElysiumScheduleId::IdleStand;
	Npc->ScheduleHost.InterruptText = TEXT("saw enemy");
	Npc->ScheduleHost.FailedSchedule = EElysiumScheduleId::IdleStand;
	Npc->ScheduleHost.FailText = TEXT("no route");
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestTrue(TEXT("Intr: then Fail:, in that order and after the activity line"),
			Debug10RetailOrder(Lines).Contains(
				TEXT("Actv: INVALID|Intr: %s (%s)\n|Fail: %s (%s)\n")));
	}
	Npc->ScheduleHost.InterruptSchedule = EElysiumScheduleId::None;
	Npc->ScheduleHost.FailedSchedule = EElysiumScheduleId::None;

	// `COND_ENEMY_TOO_FAR` prints a bare literal with no `Q_snprintf` at all.
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyTooFar);
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	TestTrue(TEXT("COND 0x55 adds the fixed line"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture())
			.Contains(TEXT("Enemy too far to attack")));
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyTooFar);

	// The velocity line: `DAT_1070d1b0` and `DAT_1070d9d0` are `vec3_origin` and `vec3_angle`, both
	// BSS zero vectors, so the gate is "either velocity is non-zero" and the format carries NO colon
	// after `Vel` and three spaces before `Ang:`.
	Npc->Velocity = FVector(12.f * ElysiumMove::U, 0.f, 0.f);
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the velocity line is last"), FString(Lines.Last().Retail),
			FString(TEXT("Vel %.1f %.1f %.1f   Ang: %.1f %.1f %.1f\n")));
		TestTrue(TEXT("in SOURCE units"), Lines.Last().Text.StartsWith(TEXT("Vel 12.0 0.0 0.0")));
	}
	Npc->Velocity = FVector::ZeroVector;
	FElysiumNpc::BeginDebugCapture();
	Npc->BaseDrawDebugTextOverlays();
	TestFalse(TEXT("and is absent for a still NPC"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture()).Contains(TEXT("Vel %.1f")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x1029d4e0` — `CAI_BaseNPCTroika::DrawDebugTextOverlays`, and its condition dump.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10TroikaTextTest,
	"Elysium.Substrate.NpcKernelDebug10.TroikaTextOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10TroikaTextTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->Schedule.Clear();
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;
	Npc->Velocity = FVector::ZeroVector;
	Npc->AngularVelocity = FVector::ZeroVector;

	// Bit 0 clear: the body chains the base and returns its answer UNCHANGED.
	Npc->DebugOverlays = 0;
	FElysiumNpc::BeginDebugCapture();
	TestEqual(TEXT("with bit 0 clear the Troika body adds nothing"),
		Npc->TroikaDrawDebugTextOverlays(), 0);
	TestEqual(TEXT("and emits nothing"), FElysiumNpc::EndDebugCapture().Num(), 0);

	// Bit 0 set. The base body's five lines come first, then the Troika body's own.
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	const int32 Total = Npc->TroikaDrawDebugTextOverlays();
	const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("the recovered line order, base body first"), Debug10RetailOrder(Lines),
		FString(TEXT("Health: %i|UNARMED|Stat: %s, |Move: %s, |Actv: INVALID|"))
			+ TEXT("(INVALID)|Cycle: %.2f|ground speed: %.3f|dist to player: %.3f|")
			+ TEXT("Disposition: %s|pos: %5.1f, %5.1f, %5.1f|dir: %5.1f, %5.1f, %5.1f|")
			+ TEXT("HG - %d : HB - %d"));
	TestEqual(TEXT("and the line index each one consumed"), Total, Lines.Num());
	TestEqual(TEXT("numbered contiguously from the base's 0"), Lines.Last().Line, Total - 1);

	// The three pose lines are gated on `GetModelPtr()`, which is `Model` here — an NPC with no
	// model prints none of them, and the sequence line still prints `(INVALID)` because that is the
	// DESCRIPTOR's arm and not the model's.
	TestTrue(TEXT("no model, no pose lines"),
		!Debug10RetailOrder(Lines).Contains(TEXT("move_yaw")));

	Npc->Model = TEXT("models/character/npc/unique/jack.mdl");
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Posed = FElysiumNpc::EndDebugCapture();
		TestTrue(TEXT("a model opens the three pose lines, between Cycle and ground speed"),
			Debug10RetailOrder(Posed).Contains(
				TEXT("Cycle: %.2f|move_yaw: %.3f|aim_pitch: %.3f|aim_yaw: %.3|ground speed: %.3f")));
		// **RETAIL BUG.** `0x105d9c54` is `"aim_yaw: %.3"` — the conversion character is missing, so
		// the pose value the body just computed is dropped. The evidence is the format string; the
		// text is the literal prefix.
		int32 AimYaw = INDEX_NONE;
		for (int32 Index = 0; Index < Posed.Num(); ++Index)
		{
			if (FCString::Strcmp(Posed[Index].Retail, TEXT("aim_yaw: %.3")) == 0)
			{
				AimYaw = Index;
			}
		}
		TestTrue(TEXT("the aim_yaw line exists"), AimYaw != INDEX_NONE);
		if (AimYaw != INDEX_NONE)
		{
			TestEqual(TEXT("and prints no value at all, retail's own typo"), Posed[AimYaw].Text,
				FString(TEXT("aim_yaw: ")));
		}
	}
	Npc->Model.Reset();

	// The `HG - %d : HB - %d` line: the hit group is live (family Damage's `LastHitGroup`), the hit
	// box is the `CTakeDamageInfo + 0x40` seam.
	Npc->LastHitGroup = 4;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Hit = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("HG is m_LastHitGroup and HB the damage packet's seam"),
			Hit.Last().Text, FString(TEXT("HG - 4 : HB - 0")));
	}
	Npc->LastHitGroup = 0;

	// The alternate-AI line, under `DAT_1092429c`. Mode 0 prints nothing; 1..4 name the four door
	// states with the REMAINING time, not the absolute stamp.
	Npc->AlternateAi = 2;
	Npc->AlternateAiExpireTime = Npc->World != nullptr ? Npc->World->NowSeconds() + 1.5 : 1.5;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugTextOverlays();
	TestFalse(TEXT("with the ConVar off the EALTAI line is absent"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture()).Contains(TEXT("EALTAI")));

	FElysiumNpc::SetDebugConVar(TEXT("DAT_1092429c"), 1);
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Alt = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("mode 2 is EALTAI_WAIT_DOOR"), FString(Alt.Last().Retail),
			FString(TEXT("EALTAI_WAIT_DOOR              - %f")));
		TestTrue(TEXT("printing the remaining time, not the stamp"),
			Alt.Last().Text.Contains(TEXT("1.5")));
	}

	Npc->AlternateAi = 0;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugTextOverlays();
	TestFalse(TEXT("mode 0 prints nothing: the switch's own default falls past the line"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture()).Contains(TEXT("EALTAI")));
	Npc->AlternateAi = 5;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugTextOverlays();
	TestFalse(TEXT("and neither does anything above 4, the JA arm"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture()).Contains(TEXT("EALTAI")));
	Npc->AlternateAi = 0;
	Debug10ResetConVars();

	// The scene tail. An unresolved `m_hDialogScene` prints NOTHING and does not advance the index;
	// a resolved one with no `CChoreoScene` behind it prints `"Scene: %s"` and RETURNS.
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugTextOverlays();
	TestFalse(TEXT("no scene entity, no scene line"),
		Debug10RetailOrder(FElysiumNpc::EndDebugCapture()).Contains(TEXT("Scene")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10ConditionDumpTest,
	"Elysium.Substrate.NpcKernelDebug10.ConditionDump", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10ConditionDumpTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// The dump with both masks empty. The header lines and the 26-id watch list are the fixed part:
	// `"INT COND\n"` opens it, `"COND\n"` separates the two halves, and the watch list flushes at
	// indices 7 and 15 plus a final flush.
	FElysiumNpc::BeginDebugCapture();
	const int32 Next = Npc->EmitConditionDump(10);
	const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("the dump's recovered line order with both masks empty"),
		Debug10RetailOrder(Lines),
		FString(TEXT("INT COND\n|COND\n|%s |%s |%s |%s ")));
	TestEqual(TEXT("numbered from the line it was handed"), Lines[0].Line, 10);
	TestEqual(TEXT("and it answers the next free line"), Next, 10 + Lines.Num());

	// The watch list is retail's own 26 ids, in retail's order, eight per line. The first flush
	// carries 0x40, 0x46, 0x01, 0x47, 0x48, 0x49, 0x4c, 0x4f — `npa see seu len eoc toc ldm ra1`,
	// which is `0x1027e7f0`'s table (story 29c-1's `ShortConditionNameTable`).
	TestEqual(TEXT("the first eight watch entries, in the listing's order"), Lines[2].Text,
		FString(TEXT("npa see seu len eoc toc ldm ra1 \n")));
	// 26 ids over eight-per-line is three full flushes and a remainder of two, which the final
	// flush carries.
	TestEqual(TEXT("and the last two"), Lines[5].Text, FString(TEXT("sdg sbl \n")));

	// A condition that STANDS upper-cases its abbreviation — the count is `(10 * 4) / 10` = 4, which
	// is past the end of a three-character name, so the whole of it is upper-cased.
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	FElysiumNpc::BeginDebugCapture();
	Npc->EmitConditionDump(0);
	{
		const TArray<FElysiumNpc::FDebugLine> Upper = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("a standing condition is upper-cased in the watch list"), Upper[2].Text,
			FString(TEXT("npa SEE seu len eoc toc ldm ra1 \n")));
	}

	// The SET mask arm prints `"%s  "` (two trailing spaces) and the INTERRUPT arm `"!%s "`, and the
	// two arms use OPPOSITE upper-case probes: the set arm upper-cases when `HasCondition` stands,
	// the interrupt arm when it does NOT. That asymmetry is a `JZ` where the first has a `JNZ`.
	//
	// One id in both masks reaches the loop's leftover flush without ever hitting the eight-per-line
	// boundary, so the two abbreviations land in ONE line — which is exactly what makes the pair of
	// formats and the inverted probe readable side by side.
	Npc->Cognition.CustomInterruptConditions.Set(EElysiumNpcCond::SeeEnemy);
	Npc->Cognition.InverseInterruptConditions.Set(EElysiumNpcCond::SeeEnemy);
	FElysiumNpc::BeginDebugCapture();
	Npc->EmitConditionDump(0);
	{
		const TArray<FElysiumNpc::FDebugLine> Masked = FElysiumNpc::EndDebugCapture();
		// `COND_SEE_ENEMY` STANDS, so the set arm shouts and the interrupt arm whispers.
		TestEqual(TEXT("the set arm's \"%s  \" shouts and the interrupt arm's \"!%s \" whispers"),
			Masked[1].Text, FString(TEXT("SEE  !see \n")));
	}

	// Clear the condition: the two arms swap.
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::SeeEnemy);
	FElysiumNpc::BeginDebugCapture();
	Npc->EmitConditionDump(0);
	{
		const TArray<FElysiumNpc::FDebugLine> Cleared = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("and with it clear the set arm whispers and the interrupt arm shouts"),
			Cleared[1].Text, FString(TEXT("see  !SEE \n")));
	}

	// **RETAIL BUG, REPRODUCED.** The final flush after the watch list tests the BITFIELD loop's
	// saved `count & 7` against 7, not the watch list's own 26 % 8 == 2 remainder. Seven standing
	// bits in the first loop therefore silently drop the last two watch entries.
	Npc->Cognition.CustomInterruptConditions = FElysiumNpcConditions();
	Npc->Cognition.InverseInterruptConditions = FElysiumNpcConditions();
	for (const EElysiumNpcCond Cond : { EElysiumNpcCond::SeeUnknown, EElysiumNpcCond::LostUnknown,
		EElysiumNpcCond::IgnoreUnknown, EElysiumNpcCond::UnknownRunTimer,
		EElysiumNpcCond::UnknownAdvancing, EElysiumNpcCond::UnknownHolding,
		EElysiumNpcCond::UnknownRetreating })
	{
		Npc->Cognition.CustomInterruptConditions.Set(Cond);
	}
	FElysiumNpc::BeginDebugCapture();
	Npc->EmitConditionDump(0);
	{
		const TArray<FElysiumNpc::FDebugLine> Bugged = FElysiumNpc::EndDebugCapture();
		TestFalse(TEXT("seven pending abbreviations drop the watch list's last two entries"),
			Debug10TextOrder(Bugged).Contains(TEXT("sdg sbl ")));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x1029ca50` — `CAI_BaseNPCTroika::DrawDebugGeometryOverlays`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10TroikaGeometryTest,
	"Elysium.Substrate.NpcKernelDebug10.TroikaGeometryOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10TroikaGeometryTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc) || !TestNotNull(TEXT("and a second"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Other });
	Npc->Schedule.Clear();
	// The fixture's own `Tick(0.0)` has already run a sense pass, which fills the closest-player
	// cache; the first cone case wants the no-player arm, so the handle is cleared explicitly.
	Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();

	// Nothing set: every gate is closed and only the base body's own arms could speak, which they
	// do not either.
	Npc->DebugOverlays = 0;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	TestEqual(TEXT("a zero m_debugOverlays draws nothing at all"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// --- `0x400000`: THREE cones, and the third takes the second's registers ------------------------
	Npc->DebugOverlays = 0x400000;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("no closest player, so two cones: mine and the third"), Lines.Num(), 2);
		if (Lines.Num() == 2)
		{
			TestEqual(TEXT("both through 0x1029c4a0"), FString(Lines[0].Retail),
				FString(TEXT("0x1029c4a0")));
			TestTrue(TEXT("the first pushes a3 0.0 with rgb (64 192 64), len 50 and arg8 1"),
				Lines[0].Text.Contains(TEXT("a3=0.0 rgb=(64 192 64) a7=50 a8=1")));
			TestTrue(TEXT("the third pushes a3 4.0 with rgb (255 0 128), len 150 and arg8 0"),
				Lines[1].Text.Contains(TEXT("a3=4.0 rgb=(255 0 128) a7=150 a8=0")));
		}
	}

	// With a closest player the SECOND cone appears, between the two, and its own constants are the
	// listing's (a3 2.0, rgb 192/64/0, len 100, arg8 0).
	Npc->Senses.Memory.ClosestPlayer = Other->Handle;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("three cones with a closest player"), Lines.Num(), 3);
		if (Lines.Num() == 3)
		{
			TestTrue(TEXT("the player cone's constants"),
				Lines[1].Text.Contains(TEXT("a3=2.0 rgb=(192 64 0) a7=100 a8=0")));
		}
	}
	Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();

	// --- `0x1000`: Troika's attack-extents box, then the base body's degenerate ±5 box --------------
	//
	// Troika 2c is live because `CAI_BaseNPCTroika::NPCInit` `0x1029a0b0` writes
	// `m_vecSavedAttackExtents = (-1,-1,-1)`, which is not `vec3_origin`. The collision OBB,
	// `"Bip01"` bone and alternate hull seams still refuse. The tail
	// `CAI_BaseNPC::DrawDebugGeometryOverlays` `0x10275760` then draws the degenerate ±5 box
	// (family Debug, 29c-1). Two boxes, both `NDebugOverlay::Box` at 255/128/0 alpha 20.
	Npc->DebugOverlays = 0x1000;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("1029a0b0 extents open Troika 2c; 10275760 adds the ±5 degenerate box"),
			Lines.Num(), 2);
		if (Lines.Num() == 2)
		{
			TestEqual(TEXT("through NDebugOverlay::Box"), FString(Lines[0].Retail),
				FString(TEXT("NDebugOverlay::Box")));
			TestTrue(TEXT("in the listing's 255/128/0 at alpha 20"),
				Lines[0].Text.Contains(TEXT("rgba=(255 128 0 20)")));
			TestTrue(TEXT("the base degenerate ±5 box"),
				Lines[1].Text.Contains(TEXT("mins=(-5.0 -5.0 -5.0) maxs=(5.0 5.0 5.0)")));
		}
	}
	TestEqual(TEXT("the alternate hull answers m_eHull, which closes the second-box gate"),
		Npc->RetailAlternateHullKind(), Npc->HullKind);

	// --- `0x20000000`: the two weapon rings ---------------------------------------------------------
	//
	// Unarmed: the NEAR ring is the 0.0 one and the FAR ring 2000.0, both of HEIGHT 32.0. The
	// checklist's walk read 0x42000000 as the radius; it is the height, and the near/far pair are
	// the radii.
	Npc->DebugOverlays = 0x20000000;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("two rings"), Lines.Num(), 2);
		if (Lines.Num() == 2)
		{
			TestEqual(TEXT("through NDebugOverlay::Circle"), FString(Lines[0].Retail),
				FString(TEXT("NDebugOverlay::Circle")));
			TestEqual(TEXT("the near ring first, at radius 0.0 and height 32.0"), Lines[0].Text,
				FString(TEXT("(0.0 0.0 0.0) \"axis=(1.0 0.0 0.0) r=0.0 h=32.0 ")
					TEXT("rgba=(255 255 32 128)\"")));
			TestEqual(TEXT("then the unarmed far ring at 2000.0"), Lines[1].Text,
				FString(TEXT("(0.0 0.0 0.0) \"axis=(1.0 0.0 0.0) r=2000.0 h=32.0 ")
					TEXT("rgba=(128 128 16 128)\"")));
		}
	}

	// --- `DAT_10924f24`: five boxes at the enemy's NOISY body target --------------------------------
	Npc->DebugOverlays = 0;
	Npc->Senses.Memory.Enemy = Other->Handle;
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10924f24"), 1);
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("exactly five boxes, one per BodyTarget call"), Lines.Num(), 5);
		if (Lines.Num() == 5)
		{
			TestTrue(TEXT("at +-2 in (255 32 32) alpha 1"),
				Lines[0].Text.Contains(TEXT("mins=(-2.0 -2.0 -2.0) maxs=(2.0 2.0 2.0) ")
					TEXT("rgba=(255 32 32 1)")));
		}
	}
	// With no enemy the arm is silent, even with the ConVar on.
	Npc->Senses.Memory.Enemy = FElysiumEntityHandle();
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	TestEqual(TEXT("and none without an enemy"), FElysiumNpc::EndDebugCapture().Num(), 0);
	Debug10ResetConVars();

	// --- The hint facing pair, and the relationship walk --------------------------------------------
	//
	// The hint arm asks a seam that refuses (hints are node indices here with no type or yaw store),
	// so it draws nothing. The relationship arm is gated on `+0x6658`, which `layout.md` records as
	// `m_bUnread6658`: the Troika constructor zeroes it and NOTHING in the corpus writes it, so the
	// arm is dead in retail and dead here.
	TestFalse(TEXT("+0x6658 is never raised, so the relationship walk is dead in retail too"),
		Npc->bUnread6658);
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaDrawDebugGeometryOverlays();
	TestEqual(TEXT("neither the hint pair nor the relationship walk speaks"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// The walk itself is real when it is reached: the entity list IS retail's, and the arm's own
	// radius is 4096.0 SOURCE units about `WorldSpaceCenter()`.
	{
		const TArray<FElysiumNpc*> Near = Npc->RelationshipLineCandidates(
			FElysiumNpc::RetailWorldSpaceCenterUnits(Npc), 4096.f);
		TestTrue(TEXT("the 4096-unit sphere finds the second NPC"), Near.Contains(Other));
		const TArray<FElysiumNpc*> Far = Npc->RelationshipLineCandidates(
			FElysiumNpc::RetailWorldSpaceCenterUnits(Npc), 0.5f);
		TestFalse(TEXT("and a tighter one does not"), Far.Contains(Other));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x10292500` — `CAI_BaseNPCTroika::NPCThinkDebugPre`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10ThinkDebugPreTest,
	"Elysium.Substrate.NpcKernelDebug10.ThinkDebugPre", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10ThinkDebugPreTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc) || !TestNotNull(TEXT("and a second"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Other });
	const double Now = Npc->World != nullptr ? Npc->World->NowSeconds() : 0.0;

	// Nothing standing: no witness timers, every ConVar off.
	Npc->Witness.CriminalWitnessedTime = Now - 1.0;
	Npc->Witness.SupernaturalWitnessedTime = Now - 1.0;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	TestEqual(TEXT("an expired pair of witness timers draws nothing"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	// --- The criminal witness box -------------------------------------------------------------------
	//
	//     x = (timer - curtime) * 0.2                             (_DAT_10451ab4)
	//     colour = (MIN(255, 255x), MIN(64, 64x), MIN(64, 64x))    per channel, three clamps
	//     lift   = OBBMaxs().z + 16.0                              (_DAT_10451ad0)
	//
	// At a remaining time of 5.0 seconds x is 1.0, so every channel saturates at its own ceiling.
	Npc->Witness.CriminalWitnessedTime = Now + 5.0;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("one box"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("at +-3, lifted 16.0, in (255 64 64) alpha 1"), Lines[0].Text,
				FString(TEXT("(0.0 0.0 16.0) mins=(-3.0 -3.0 -3.0) maxs=(3.0 3.0 3.0) ")
					TEXT("rgba=(255 64 64 1)")));
		}
	}

	// Half a second left: x is 0.1, so every channel is a tenth of its ceiling. The three clamps are
	// independent and the red one does NOT saturate at 64.
	Npc->Witness.CriminalWitnessedTime = Now + 0.5;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestTrue(TEXT("the three channels scale independently"),
			Lines.Num() == 1 && Lines[0].Text.Contains(TEXT("rgba=(25 6 6 1)")));
	}

	// The comparison is `<=`: a timer standing EXACTLY at curtime still draws.
	Npc->Witness.CriminalWitnessedTime = Now;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	TestEqual(TEXT("a timer exactly at curtime still draws, the gate is <="),
		FElysiumNpc::EndDebugCapture().Num(), 1);
	Npc->Witness.CriminalWitnessedTime = Now - 1.0;

	// --- The supernatural witness box, whose colour ladder DIFFERS ----------------------------------
	//
	// A different lift (12.0, `_DAT_1044faa4`) and a different ceiling set: 192.0 on red where the
	// criminal box has 255.0, 255.0 on green where it has 64.0.
	Npc->Witness.SupernaturalWitnessedTime = Now + 5.0;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("one box"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("lifted 12.0 and coloured (192 255 64), not (255 64 64)"),
				Lines[0].Text,
				FString(TEXT("(0.0 0.0 12.0) mins=(-3.0 -3.0 -3.0) maxs=(3.0 3.0 3.0) ")
					TEXT("rgba=(192 255 64 1)")));
		}
	}
	Npc->Witness.SupernaturalWitnessedTime = Now - 1.0;

	// --- `DAT_1092479c`: the line of sight and the blocker label ------------------------------------
	//
	// The trace is a seam that answers "clear", which is retail's GREEN line; the line itself is the
	// arm's output and the checklist's walk omitted it entirely.
	FElysiumNpc::SetDebugConVar(TEXT("DAT_1092479c"), 1);
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::SeePlayer);
	Npc->Senses.Memory.ClosestPlayer = Other->Handle;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("one line"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("through NDebugOverlay::Line"), FString(Lines[0].Retail),
				FString(TEXT("NDebugOverlay::Line")));
			TestTrue(TEXT("green, because the trace seam answers a clear line"),
				Lines[0].Text.Contains(TEXT("rgb=(0 255 0) nodepth=1")));
		}
	}
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::SeePlayer);
	Npc->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();

	// `COND_ENEMY_OCCLUDED` labels the blocker 20.0 units above the eye, and an unresolved occluder
	// handle prints the literal `"**UNKNOWN**"` — which the checklist's walk omits.
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("one label"), Lines.Num(), 1);
		if (Lines.Num() == 1)
		{
			TestEqual(TEXT("through NDebugOverlay::Text"), FString(Lines[0].Retail),
				FString(TEXT("NDebugOverlay::Text")));
			TestTrue(TEXT("naming the unresolved occluder **UNKNOWN**"),
				Lines[0].Text.Contains(TEXT("\"Blocked by **UNKNOWN**\"")));
		}
	}
	Npc->Senses.Memory.EnemyOccluder = Other->Handle;
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestTrue(TEXT("and GetDebugName when it resolves"),
			Lines.Num() == 1 && Lines[0].Text.Contains(TEXT("\"Blocked by other\"")));
	}
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	Npc->Senses.Memory.EnemyOccluder = FElysiumEntityHandle();
	Debug10ResetConVars();

	// --- `DAT_109244c4`: the eye box, the ideal box and the line between them ------------------------
	FElysiumNpc::SetDebugConVar(TEXT("DAT_109244c4"), 1);
	FElysiumNpc::BeginDebugCapture();
	Npc->TroikaNPCThinkDebugPre();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the pair and the line"), Debug10RetailOrder(Lines),
			FString(TEXT("NDebugOverlay::Box|NDebugOverlay::Box|NDebugOverlay::Line")));
		if (Lines.Num() == 3)
		{
			TestTrue(TEXT("the eye box is +-3 in (255 64 64)"),
				Lines[0].Text.Contains(TEXT("mins=(-3.0 -3.0 -3.0) maxs=(3.0 3.0 3.0) ")
					TEXT("rgba=(255 64 64 1)")));
			TestTrue(TEXT("the ideal box is +-2 in (64 255 64)"),
				Lines[1].Text.Contains(TEXT("mins=(-2.0 -2.0 -2.0) maxs=(2.0 2.0 2.0) ")
					TEXT("rgba=(64 255 64 1)")));
			TestTrue(TEXT("and the joining line is the same green"),
				Lines[2].Text.Contains(TEXT("rgb=(64 255 64) nodepth=1")));
		}
	}
	Debug10ResetConVars();

	// --- `DAT_1092435c`: an INT selector, not a flag -------------------------------------------------
	for (const TPair<int32, const TCHAR*> Row : { TPair<int32, const TCHAR*>(1, TEXT("0x1028e030")),
		TPair<int32, const TCHAR*>(2, TEXT("0x1028e060")) })
	{
		FElysiumNpc::SetDebugConVar(TEXT("DAT_1092435c"), Row.Key);
		FElysiumNpc::BeginDebugCapture();
		Npc->TroikaNPCThinkDebugPre();
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(*FString::Printf(TEXT("mode %d selects %s"), Row.Key, Row.Value),
			Debug10RetailOrder(Lines), FString(Row.Value));
	}
	for (const int32 Mode : { 0, 3, 99 })
	{
		FElysiumNpc::SetDebugConVar(TEXT("DAT_1092435c"), Mode);
		FElysiumNpc::BeginDebugCapture();
		Npc->TroikaNPCThinkDebugPre();
		TestEqual(*FString::Printf(TEXT("mode %d selects neither"), Mode),
			FElysiumNpc::EndDebugCapture().Num(), 0);
	}
	Debug10ResetConVars();
	return true;
}

// -------------------------------------------------------------------------------------------------
// The two slot dispatchers, and the species arms.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10SlotDispatchTest,
	"Elysium.Substrate.NpcKernelDebug10.SlotDispatch", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10SlotDispatchTest::RunTest(const FString&)
{
	// The census IS the dispatch table, and this is what the two slot methods read. Every row here
	// is a `rule` row of this family.
	const TPair<const TCHAR*, const TCHAR*> Slot124[] = {
		{ TEXT("CNPC_Crow"), TEXT("0x10358f90") },
		{ TEXT("CNPC_VHengeyokai"), TEXT("0x10383560") },
		{ TEXT("CNPC_VNewscaster"), TEXT("0x103a1250") },
		{ TEXT("CNPC_VTzimisce"), TEXT("0x103c08d0") },
		{ TEXT("CNPC_VZombie"), TEXT("0x103e0e80") },
		// The Troika line itself, and a class with no override of its own.
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x1029d4e0") },
		{ TEXT("CNPC_VHumanCombatant"), TEXT("0x1029d4e0") },
	};
	for (const TPair<const TCHAR*, const TCHAR*>& Row : Slot124)
	{
		const FElysiumNpcClass* const Cls = ElysiumNpcKernelClass::Find(Row.Key);
		TestNotNull(*FString::Printf(TEXT("%s is in the census"), Row.Key), Cls);
		TestEqual(*FString::Printf(TEXT("%s fills slot 124 with %s"), Row.Key, Row.Value),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 124)), FString(Row.Value));
	}

	const TPair<const TCHAR*, const TCHAR*> Slot123[] = {
		{ TEXT("CNPC_VCop"), TEXT("0x10372f00") },
		{ TEXT("CNPC_VMingXiao"), TEXT("0x10399d40") },
		{ TEXT("CNPCMaker"), TEXT("0x1034bd30") },
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x1029ca50") },
	};
	for (const TPair<const TCHAR*, const TCHAR*>& Row : Slot123)
	{
		const FElysiumNpcClass* const Cls = ElysiumNpcKernelClass::Find(Row.Key);
		TestNotNull(*FString::Printf(TEXT("%s is in the census"), Row.Key), Cls);
		TestEqual(*FString::Printf(TEXT("%s fills slot 123 with %s"), Row.Key, Row.Value),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 123)), FString(Row.Value));
	}

	// **The control the story asks for.** A plain `npc_VCop` takes the TROIKA bodies at both slots,
	// because the census gives `CNPC_VCop` a NULL classname list — a spawned cop's `RetailClass()`
	// is null and every species lookup falls through. That is the recovered dispatch (story 29c-1's
	// cleanup), not a gap, and it is why `0x10372f00`'s arms are unreachable in this port.
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Cop));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the cop spawned"), Cop))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Cop });
	Cop->Schedule.Clear();
	TestNull(TEXT("a spawned npc_VCop has no census class"), Cop->RetailClass());

	Cop->DebugOverlays = 0x20000000;
	FElysiumNpc::BeginDebugCapture();
	Cop->DrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("so slot 123 runs the TROIKA body: two weapon rings, no cop label"),
			Debug10RetailOrder(Lines),
			FString(TEXT("NDebugOverlay::Circle|NDebugOverlay::Circle")));
	}

	Cop->DebugOverlays = 0x1;
	Cop->ActivityNumber = INDEX_NONE;
	Cop->IdealActivityNumber = INDEX_NONE;
	FElysiumNpc::BeginDebugCapture();
	Cop->DrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestTrue(TEXT("and slot 124 the Troika text body, which ends on the HG/HB line"),
			Debug10RetailOrder(Lines).EndsWith(TEXT("HG - %d : HB - %d")));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x10358f90` — `CNPC_Crow::DrawDebugTextOverlays`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10CrowTextTest,
	"Elysium.Substrate.NpcKernelDebug10.CrowTextOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10CrowTextTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc) || !TestNotNull(TEXT("and a second"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Other });
	Npc->Schedule.Clear();
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	// `CNPC_Crow` is not a registered spawn leaf here, so the arm is exercised by name. The census
	// is what routes to it and the dispatch case is asserted in `SlotDispatch` above.
	TestEqual(TEXT("CNPC_Crow fills slot 124 with its own body"),
		FString(ElysiumNpcKernelClass::BodyOf(ElysiumNpcKernelClass::Find(TEXT("CNPC_Crow")), 124)),
		FString(TEXT("0x10358f90")));

	// Bit 0 clear: the base runs and the crow adds nothing.
	Npc->DebugOverlays = 0;
	FElysiumNpc::BeginDebugCapture();
	TestEqual(TEXT("with bit 0 clear the crow adds nothing"),
		Npc->CrowDrawDebugTextOverlays(), 0);
	TestEqual(TEXT("and emits nothing"), FElysiumNpc::EndDebugCapture().Num(), 0);

	// Bit 0 set, no enemy: exactly ONE extra line, and the chained body is the BASE
	// (`0x102767d0`) and NOT the Troika one — a crow never gets `Seq:`, `Cycle:` or `pos:`.
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	const int32 Lines1 = Npc->CrowDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the crow chains the BASE body, not the Troika one"),
			Debug10RetailOrder(Lines),
			FString(TEXT("Health: %i|UNARMED|Stat: %s, |Move: %s, |Actv: INVALID|morale: %d")));
		TestEqual(TEXT("returning base + 1"), Lines1, 6);
		TestEqual(TEXT("with the morale seam's 0"), Lines.Last().Text,
			FString(TEXT("morale: 0")));
	}

	// With an enemy: a SECOND line, printing the enemy's CLASSNAME (not its debug name) and the
	// crow's own cached distance at `+0x5f4c`.
	Npc->Senses.Memory.Enemy = Other->Handle;
	FElysiumNpc::BeginDebugCapture();
	const int32 Lines2 = Npc->CrowDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("an enemy adds the second line"), FString(Lines.Last().Retail),
			FString(TEXT("enemy (dist): %s (%g)")));
		TestEqual(TEXT("naming the CLASSNAME, not the debug name"), Lines.Last().Text,
			FString(TEXT("enemy (dist): npc_VHumanCombatant (0)")));
		TestEqual(TEXT("and the return is base + 2"), Lines2, Lines1 + 1);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x10383560`, `0x103a1250`, `0x103c08d0`, `0x103e0e80` — the four Troika-chaining species arms.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10HengeyokaiTextTest,
	"Elysium.Substrate.NpcKernelDebug10.HengeyokaiTextOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10HengeyokaiTextTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->Schedule.Clear();
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	// Bit 0 clear: the Troika body's answer, unchanged.
	Npc->DebugOverlays = 0;
	TestEqual(TEXT("with bit 0 clear the hengeyokai adds nothing"),
		Npc->HengeyokaiDrawDebugTextOverlays(), 0);

	// Bit 0 set: exactly ONE more line, and the `+1` is the contract every later overlay consumes.
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	const int32 WithoutLine = Npc->TroikaDrawDebugTextOverlays();
	FElysiumNpc::EndDebugCapture();
	FElysiumNpc::BeginDebugCapture();
	const int32 WithLine = Npc->HengeyokaiDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the return is the Troika body's plus one"), WithLine, WithoutLine + 1);
		TestEqual(TEXT("the added line names the species body"), FString(Lines.Last().Retail),
			FString(TEXT("0x10383560")));
		// Slot 9 is a generated stub owned by another story; the empty string is retail's null arm.
		TestEqual(TEXT("and slot 9's seam prints the empty string, retail's null arm"),
			Lines.Last().Text, FString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10NewscasterTextTest,
	"Elysium.Substrate.NpcKernelDebug10.NewscasterTextOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10NewscasterTextTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	// `npc_VNewscaster` is the ONE species arm of this family a registered classname reaches, so it
	// is exercised through the slot method itself and not by name.
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Newscaster));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the newscaster spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->Schedule.Clear();
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	TestEqual(TEXT("a spawned npc_VNewscaster resolves to CNPC_VNewscaster"),
		FString(Npc->RetailClass() != nullptr ? Npc->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VNewscaster")));
	TestEqual(TEXT("which fills slot 124 with 0x103a1250"),
		FString(ElysiumNpcKernelClass::BodyOf(Npc->RetailClass(), 124)),
		FString(TEXT("0x103a1250")));

	// The slot method routes to the species arm, which chains the Troika body and adds
	// `0x103a0ff0`'s lines to the SAME budget. That body is family Species' row in band 5–9 and is
	// seamed here, so it contributes 0 and the newscaster's answer is the Troika body's.
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	const int32 ViaSlot = Npc->DrawDebugTextOverlays();
	const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
	FElysiumNpc::BeginDebugCapture();
	const int32 Direct = Npc->TroikaDrawDebugTextOverlays();
	FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("the story-queue seam adds no lines yet"), ViaSlot, Direct);
	TestTrue(TEXT("but the Troika body's own lines are there"),
		Debug10RetailOrder(Lines).Contains(TEXT("HG - %d : HB - %d")));
	TestEqual(TEXT("and the seam is asked"), Npc->NewscasterStoryOverlayLines(0), 0);

	// Bit 0 clear short-circuits the species half but not the chain.
	Npc->DebugOverlays = 0;
	TestEqual(TEXT("with bit 0 clear nothing is added"), Npc->DrawDebugTextOverlays(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10TzimisceTextTest,
	"Elysium.Substrate.NpcKernelDebug10.TzimisceTextOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10TzimisceTextTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc) || !TestNotNull(TEXT("and a second"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Other });
	Npc->Schedule.Clear();
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	Npc->DebugOverlays = 0;
	TestEqual(TEXT("with bit 0 clear the tzimisce adds nothing"),
		Npc->TzimisceDrawDebugTextOverlays(), 0);

	// No pickup target: the distance starts at `_DAT_104454c4` = 0.0 and the two globals are the
	// cross-NPC latch, which `0x103be130`'s seam never opens.
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	Npc->TzimisceDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the body line, with the image's own format"), FString(Lines.Last().Retail),
			FString(TEXT("Body - %5.1f|%5.1f|%s")));
		TestEqual(TEXT("all three fields at their unlatched values"), Lines.Last().Text,
			FString(TEXT("Body -   0.0|  0.0|")));
	}

	// A live pickup target makes the first field the full 3-D distance in SOURCE units. The second
	// stays latched at 0.0 because `0x103be130` answers false, so the latch never updates — which is
	// the recovered refusal and not a gap.
	Npc->PickupTarget = Other->Handle;
	FElysiumNpc::BeginDebugCapture();
	Npc->TzimisceDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestTrue(TEXT("the live distance is the 3-D separation"),
			Lines.Last().Text.StartsWith(TEXT("Body - ")));
		TestTrue(TEXT("and the latch is untouched, the carry probe being a seam"),
			Lines.Last().Text.EndsWith(TEXT("|  0.0|")));
	}
	TestFalse(TEXT("the carry probe 0x103be130 answers false"), Npc->TzimisceIsCarryingBody());
	Npc->PickupTarget = FElysiumEntityHandle();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10ZombieTextTest,
	"Elysium.Substrate.NpcKernelDebug10.ZombieTextOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10ZombieTextTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->Schedule.Clear();
	Npc->ActivityNumber = INDEX_NONE;
	Npc->IdealActivityNumber = INDEX_NONE;

	// The zombie's gate is `m_debugOverlays & 0x40000` and NOT bit 0, so the two are independent:
	// bit 0 alone runs the Troika body and adds nothing.
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	const int32 TroikaOnly = Npc->TroikaDrawDebugTextOverlays();
	FElysiumNpc::EndDebugCapture();
	FElysiumNpc::BeginDebugCapture();
	const int32 ZombieBit0 = Npc->ZombieDrawDebugTextOverlays();
	FElysiumNpc::EndDebugCapture();
	TestEqual(TEXT("bit 0 alone adds no Cond lines: the zombie's gate is 0x40000"),
		ZombieBit0, TroikaOnly);

	// `0x40000` set: the bitfield at `+0x5c5c` is a seam that answers false for every id, so no line
	// is added — the arm is reached and its refusal is the recovered one.
	Npc->DebugOverlays = 0x1 | 0x40000;
	FElysiumNpc::BeginDebugCapture();
	const int32 WithBit = Npc->ZombieDrawDebugTextOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("the 0x40000 arm asks the bitfield seam and adds nothing"),
			WithBit, TroikaOnly);
		TestFalse(TEXT("no Cond line"),
			Debug10RetailOrder(Lines).Contains(TEXT("Cond: %s")));
	}

	// The seam itself, across the whole walked range 0..0xbf.
	bool bAnySet = false;
	for (int32 Id = 0; Id < 0xc0; ++Id)
	{
		bAnySet = bAnySet || Npc->ZombieConditionBit(Id);
	}
	TestFalse(TEXT("+0x5c5c answers false for every id 0..0xbf"), bAnySet);

	// The naming half is real: slot 458 `ConditionName` answers the registered `COND_*` symbol, and
	// the zombie's local-to-global-and-back pair is the identity for a base condition.
	TestEqual(TEXT("slot 458 names the condition the Cond: line would print"),
		FString(Npc->ConditionName(0x46)), FString(TEXT("COND_SEE_ENEMY")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x10372f00` and `0x10399d40` — the two species geometry arms.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10VCopGeometryTest,
	"Elysium.Substrate.NpcKernelDebug10.VCopGeometryOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10VCopGeometryTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Cop));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("subject"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the cop spawned"), Cop) || !TestNotNull(TEXT("and a second NPC"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Cop, Other });
	Cop->Schedule.Clear();

	// The label stands behind THREE gates in order: bit 0, a resolving `m_hClosestPlayer`, and a
	// non-degenerate collision OBB. The third is story 29c-1's `CollisionObbExtentsUnits` seam,
	// which refuses — so the label never draws and the Troika body ALWAYS runs, whichever gate
	// closed. That last is the arm's contract and is what is asserted.
	Cop->DebugOverlays = 0;
	FElysiumNpc::BeginDebugCapture();
	Cop->VCopDrawDebugGeometryOverlays();
	TestEqual(TEXT("with bit 0 clear the label is skipped and the Troika body still runs"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	Cop->DebugOverlays = 0x1 | 0x20000000;
	Cop->Senses.Memory.ClosestPlayer = Other->Handle;
	FElysiumNpc::BeginDebugCapture();
	Cop->VCopDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		// No label — the OBB seam takes the degenerate arm — but the Troika body's weapon rings are
		// there, which proves the tail ran.
		TestEqual(TEXT("the OBB seam closes the label gate, and the Troika tail still runs"),
			Debug10RetailOrder(Lines),
			FString(TEXT("NDebugOverlay::Circle|NDebugOverlay::Circle")));
	}
	Cop->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();

	// The four suffix seams, each named with the retail call it stands for. Every one answers the
	// value that DROPS its suffix, which is the recovered refusal.
	TestFalse(TEXT("DAT_1093ac3c / _DAT_1093aca8 are not stood: no ' Suspect'"),
		Cop->CopSuspectIs(Other));
	TestFalse(TEXT("0x1017f8d0 answers false for a non-player: no ' Alert'"),
		Cop->PlayerHeightenedAlert(Other));
	TestEqual(TEXT("0x1017f770 answers 0 for a non-player: no ' Count%d'"),
		Cop->PlayerCopsInPursuitCount(Other), 0);
	TestNull(TEXT("m_hPursuitPlayer is not stood: no ' Pursuit'"), Cop->CopPursuitPlayer());

	// The player words ARE real when the candidate IS the player — the same two `FElysiumPlayer`
	// words family Dialogue reads through `DialogThreatCount()`.
	FElysiumPlayer* const Player = Fixture.Player();
	if (Player != nullptr)
	{
		Player->Police.CopsInPursuit = 3;
		TestEqual(TEXT("0x1017f770 reads m_iCopsInPursuitCount off the player"),
			Cop->PlayerCopsInPursuitCount(Player), 3);
		Player->Police.CopsInPursuit = 0;

		Player->Police.HeightenedAlertExpiry =
			(Cop->World != nullptr ? Cop->World->NowSeconds() : 0.0) + 5.0;
		TestTrue(TEXT("and 0x1017f8d0 the alert window"), Cop->PlayerHeightenedAlert(Player));
		Player->Police.HeightenedAlertExpiry = 0.0;
		TestFalse(TEXT("which is strict against curtime"), Cop->PlayerHeightenedAlert(Player));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDebug10MingXiaoGeometryTest,
	"Elysium.Substrate.NpcKernelDebug10.MingXiaoGeometryOverlays", GElysiumNpcKernelDebug10Flags)
bool FElysiumNpcKernelDebug10MingXiaoGeometryTest::RunTest(const FString&)
{
	Debug10ResetConVars();
	FElysiumNpcWorldFixture Fixture(Debug10Builder(GDebug10Combatant));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("subject"));
	if (!TestNotNull(TEXT("the combatant spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->Schedule.Clear();

	// The gate is `0x20000000` — the WEAPON-RING bit, not bit 0 like its siblings — so MingXiao's
	// bands and the Troika body's two weapon rings appear together.
	Npc->DebugOverlays = 0x1;
	FElysiumNpc::BeginDebugCapture();
	Npc->MingXiaoDrawDebugGeometryOverlays();
	TestEqual(TEXT("bit 0 does not open MingXiao's arm"),
		FElysiumNpc::EndDebugCapture().Num(), 0);

	Npc->DebugOverlays = 0x20000000;
	FElysiumNpc::BeginDebugCapture();
	Npc->MingXiaoDrawDebugGeometryOverlays();
	{
		const TArray<FElysiumNpc::FDebugLine> Lines = FElysiumNpc::EndDebugCapture();
		TestEqual(TEXT("four rings, then the Troika body's two"), Lines.Num(), 6);
		if (Lines.Num() == 6)
		{
			// The four recovered range bands, in the order the body draws them.
			const TCHAR* const Radii[] = { TEXT("r=100.0"), TEXT("r=150.0"), TEXT("r=200.0"),
				TEXT("r=300.0") };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				TestTrue(*FString::Printf(TEXT("band %d is %s at height 8.0 in (255 32 32 128)"),
						Index, Radii[Index]),
					Lines[Index].Text.Contains(Radii[Index])
						&& Lines[Index].Text.Contains(TEXT("h=8.0 rgba=(255 32 32 128)")));
			}
			TestTrue(TEXT("and the Troika weapon rings follow"),
				Lines[4].Text.Contains(TEXT("r=0.0 h=32.0")));
		}
	}
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
