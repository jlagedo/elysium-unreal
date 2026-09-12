// Content-free Substrate automation for 0005 story 15 — the NPC think cadence's four interval
// laws and their shared due test, driven as pure arithmetic over `ElysiumNpcThink::FInputs`.
//
// Every number here is a constant recovered from the DLL and recorded in
// `docs/vtmb/npc-ai-reverse-engineering.md` § "The think cadence, decoded": `CalcNextUpdateThink`
// `0x10290720`, `CalcNextNormalThink` `0x10290b60`, `CalcNextMoveThink` `0x10290fc0`,
// `CalcNextAIThink` `0x10291230`, `IsThinkDue` `0x10290660`. Nothing here stands a world.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumNpcThinkCadenceTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using FInputs = ElysiumNpcThink::FInputs;

	// The default subject: a player 512 units away, in PVS, out of LOS, no pins. Every case moves
	// one term off this so the arm under test is the only difference.
	FInputs At(float DistUnits)
	{
		FInputs In;
		In.bHasClosestPlayer = true;
		In.PlayerDistUnits = DistUnits;
		In.bInPlayerPvs = true;
		In.bInPlayerLos = false;
		return In;
	}

	// A frame short enough that a stamp one law-interval out is never accidentally due.
	constexpr double Frame = 1.0 / 60.0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkDueTest,
	"Elysium.Substrate.NpcThinkCadence.Due", GElysiumTestFlags)
bool FElysiumThinkDueTest::RunTest(const FString&)
{
	// `(stamp - curtime) <= frametime`, and equality IS due (`FCOMP` + `TEST AH,0x41`).
	TestTrue(TEXT("a stamp in the past is due"), ElysiumNpcThink::IsDue(9.0, 10.0, Frame));
	TestTrue(TEXT("a stamp exactly at the clock is due"), ElysiumNpcThink::IsDue(10.0, 10.0, Frame));
	// Binary-exact values, so this asserts the `<=` and not the rounding of 1/60.
	TestTrue(TEXT("a stamp exactly one frame ahead is due"),
		ElysiumNpcThink::IsDue(10.5, 10.0, 0.5));
	TestFalse(TEXT("a stamp beyond the frame is not"),
		ElysiumNpcThink::IsDue(11.0, 10.0, 0.5));
	// The epsilon is the frame, so a coarse clock makes more stamps due -- which is why the world
	// clamps what it reports rather than handing over the raw gap.
	TestTrue(TEXT("a ten-second frame makes a distant stamp due"),
		ElysiumNpcThink::IsDue(15.0, 10.0, 10.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkUpdateLawTest,
	"Elysium.Substrate.NpcThinkCadence.UpdateLaw", GElysiumTestFlags)
bool FElysiumThinkUpdateLawTest::RunTest(const FString&)
{
	ElysiumRng::SeedAll(0x15CADE);
	const double Tol = 1e-6;

	// No closest player: the floor, and it is NOT an early return -- the visibility scaling still
	// applies to it.
	FInputs None;
	None.bInPlayerLos = true;
	TestEqual(TEXT("no closest player is 0.03"), ElysiumNpcThink::UpdateInterval(None), 0.03, Tol);
	None.bInPlayerPvs = false;
	TestEqual(TEXT("...and out of PVS that floor is scaled too"),
		ElysiumNpcThink::UpdateInterval(None), 0.3, Tol);

	// `(dist - 512) / 704`, in LOS so no scaling applies.
	FInputs In = At(512.f);
	In.bInPlayerLos = true;
	TestEqual(TEXT("at 512 units the law is under its floor and takes 0.03"),
		ElysiumNpcThink::UpdateInterval(In), 0.03, Tol);
	In.PlayerDistUnits = 512.f + 704.f;
	TestEqual(TEXT("one whole divisor out is one second"),
		ElysiumNpcThink::UpdateInterval(In), 1.0, Tol);
	In.PlayerDistUnits = 512.f + 704.f * 0.02f;   // 0.02 -- below the 0.03 floor
	TestEqual(TEXT("under the floor takes the floor"),
		ElysiumNpcThink::UpdateInterval(In), 0.03, Tol);

	// At and beyond the ceiling the jitter joins: 8 + RandomFloat(0, 0.8).
	In.PlayerDistUnits = 512.f + 704.f * 20.f;
	for (int32 Draw = 0; Draw < 16; ++Draw)
	{
		const double Interval = ElysiumNpcThink::UpdateInterval(In);
		TestTrue(TEXT("the ceiling is 8 plus a jitter inside 0.8"),
			Interval >= 8.0 && Interval <= 8.8);
	}

	// The two visibility arms are EXCLUSIVE: out of PVS never also takes the LOS multiplier.
	FInputs Far = At(512.f + 704.f * 2.f);   // a clean 2.0 s before scaling
	Far.bInPlayerLos = false;
	TestEqual(TEXT("out of LOS but in PVS is x5"), ElysiumNpcThink::UpdateInterval(Far), 10.0, Tol);
	Far.bInPlayerPvs = false;
	TestEqual(TEXT("out of PVS is x10 and the LOS arm does not also apply"),
		ElysiumNpcThink::UpdateInterval(Far), 16.0, Tol);   // 2 x 10 = 20, capped at 16

	FInputs Capped = At(512.f + 704.f * 3.f);   // 3.0 s before scaling
	Capped.bInPlayerLos = false;
	TestEqual(TEXT("the out-of-LOS cap is 12"), ElysiumNpcThink::UpdateInterval(Capped), 12.0, Tol);

	// `ShouldThinkFrequently` is applied LAST and overrides everything, including the caps.
	Capped.bThinkFrequently = true;
	TestEqual(TEXT("a frequent thinker updates at 0.03 however far or hidden"),
		ElysiumNpcThink::UpdateInterval(Capped), 0.03, Tol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkNormalLawTest,
	"Elysium.Substrate.NpcThinkCadence.NormalLaw", GElysiumTestFlags)
bool FElysiumThinkNormalLawTest::RunTest(const FString&)
{
	ElysiumRng::SeedAll(0x15CADE);
	const double Tol = 1e-6;

	// The three pins, each skipping the distance branch AND the visibility scaling.
	FInputs Pinned = At(20000.f);
	Pinned.bInPlayerLos = true;
	TestEqual(TEXT("in LOS pins the normal think to 0.1"),
		ElysiumNpcThink::NormalInterval(Pinned), 0.1, Tol);
	Pinned = At(20000.f);
	Pinned.bScheduleChanged = true;
	TestEqual(TEXT("SCHEDULE_CHANGED pins it too"),
		ElysiumNpcThink::NormalInterval(Pinned), 0.1, Tol);
	Pinned = At(20000.f);
	Pinned.bAlwaysInPlayerView = true;
	TestEqual(TEXT("...and so does the frenzy bit"),
		ElysiumNpcThink::NormalInterval(Pinned), 0.1, Tol);

	// `ShouldThinkFrequently` is FIRST here and gives 0.01, not the update law's 0.03.
	Pinned.bThinkFrequently = true;
	TestEqual(TEXT("a frequent thinker is 0.01 and outranks the pins"),
		ElysiumNpcThink::NormalInterval(Pinned), 0.01, Tol);

	// The distance branch: `(dist - 2048) x 3/4096`, and because every LOS case was taken by the
	// pin above, the `x3 cap 6` ALWAYS applies here. The law's real ceiling in PVS is 6 s.
	FInputs In = At(2048.f);
	TestEqual(TEXT("at 2048 units the branch is under its floor, then scaled"),
		ElysiumNpcThink::NormalInterval(In), 0.3, Tol);   // 0.1 floored, then x3
	In.PlayerDistUnits = 2048.f + 4096.f / 3.f;           // exactly 1.0 s before scaling
	TestEqual(TEXT("one second of raw interval reads as three"),
		ElysiumNpcThink::NormalInterval(In), 3.0, Tol);
	In.PlayerDistUnits = 20000.f;
	for (int32 Draw = 0; Draw < 16; ++Draw)
	{
		TestEqual(TEXT("past the ceiling the x3 cap of 6 swallows the jitter"),
			ElysiumNpcThink::NormalInterval(In), 6.0, Tol);
	}
	// No player at all is the same 0.1 the pins give, but it DOES take the scaling.
	FInputs NoPlayer;
	NoPlayer.bInPlayerLos = false;
	TestEqual(TEXT("no closest player is 0.1 scaled by the LOS arm"),
		ElysiumNpcThink::NormalInterval(NoPlayer), 0.3, Tol);
	NoPlayer.bInPlayerPvs = false;
	TestEqual(TEXT("...and x10 out of PVS instead"),
		ElysiumNpcThink::NormalInterval(NoPlayer), 1.0, Tol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkAiLawTest,
	"Elysium.Substrate.NpcThinkCadence.AiLaw", GElysiumTestFlags)
bool FElysiumThinkAiLawTest::RunTest(const FString&)
{
	ElysiumRng::SeedAll(0x15CADE);
	const double Tol = 1e-6;

	FInputs Pinned = At(20000.f);
	Pinned.bInPlayerLos = true;
	TestEqual(TEXT("in LOS pins the AI think to 0.1"), ElysiumNpcThink::AiInterval(Pinned), 0.1, Tol);
	Pinned = At(20000.f);
	Pinned.bScheduleChanged = true;
	TestEqual(TEXT("SCHEDULE_CHANGED pins it"), ElysiumNpcThink::AiInterval(Pinned), 0.1, Tol);
	FInputs NoPlayer;
	NoPlayer.bInPlayerLos = false;
	TestEqual(TEXT("no closest player is 0.1"), ElysiumNpcThink::AiInterval(NoPlayer), 0.1, Tol);

	// `(dist - 512) / 896`, with NO visibility scaling of any kind -- the one law of four that
	// reads neither PVS nor `ShouldThinkFrequently`.
	FInputs In = At(512.f + 896.f);
	TestEqual(TEXT("one whole divisor out is one second"),
		ElysiumNpcThink::AiInterval(In), 1.0, Tol);
	In.bInPlayerPvs = false;
	TestEqual(TEXT("out of PVS changes nothing"), ElysiumNpcThink::AiInterval(In), 1.0, Tol);
	In.bThinkFrequently = true;
	TestEqual(TEXT("a frequent thinker changes nothing either"),
		ElysiumNpcThink::AiInterval(In), 1.0, Tol);

	In = At(512.f);
	TestEqual(TEXT("at 512 units it takes the 0.1 floor"),
		ElysiumNpcThink::AiInterval(In), 0.1, Tol);
	In.PlayerDistUnits = 512.f + 896.f * 20.f;
	for (int32 Draw = 0; Draw < 16; ++Draw)
	{
		const double Interval = ElysiumNpcThink::AiInterval(In);
		TestTrue(TEXT("the ceiling is 4 plus a jitter inside 0.4"),
			Interval >= 4.0 && Interval <= 4.4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkStampsTest,
	"Elysium.Substrate.NpcThinkCadence.Stamps", GElysiumTestFlags)
bool FElysiumThinkStampsTest::RunTest(const FString&)
{
	ElysiumRng::SeedAll(0x15CADE);
	const double Tol = 1e-6;
	FInputs In = At(512.f + 896.f);   // AI = 1.0 s, Normal = 0.3 s, Update = 5.0 s (x5, out of LOS)

	FElysiumNpcScheduleHost Host;
	Host.ResetThinkTimers(10.0);
	Host.LastUpdate = Host.LastNormal = Host.LastMove = Host.LastAI = 10.0;

	// Every gated law is due at its own stamp, so one pass advances all three.
	ElysiumNpcThink::CalcNextUpdateThink(Host, In, 10.0, Frame);
	ElysiumNpcThink::CalcNextNormalThink(Host, In, 10.0, Frame);
	ElysiumNpcThink::CalcNextAiThink(Host, In, 10.0, Frame);
	TestEqual(TEXT("the AI stamp advances by its law"), Host.NextAI, 11.0, Tol);
	TestEqual(TEXT("the normal stamp advances by its own"), Host.NextNormal, 10.3, Tol);
	TestEqual(TEXT("the last mirror keeps the stamp that was replaced"), Host.LastAI, 10.0, Tol);

	// THE SELF-GATE. A pass on a clock that is not due must leave that clock alone -- otherwise a
	// think woken by the update stamp would keep pushing the AI stamp out and the NPC would never
	// gather again.
	const double NormalBefore = Host.NextNormal;
	const double AiBefore = Host.NextAI;
	ElysiumNpcThink::CalcNextNormalThink(Host, In, 10.1, Frame);
	ElysiumNpcThink::CalcNextAiThink(Host, In, 10.1, Frame);
	TestEqual(TEXT("a normal stamp that is not due is untouched"), Host.NextNormal, NormalBefore, Tol);
	TestEqual(TEXT("an AI stamp that is not due is untouched"), Host.NextAI, AiBefore, Tol);

	// `Next = max(Next, curtime) + i`: a stamp that fell far behind the clock restarts from now
	// rather than accumulating the intervals it missed.
	Host.NextAI = 5.0;
	ElysiumNpcThink::CalcNextAiThink(Host, In, 100.0, Frame);
	TestEqual(TEXT("a stamp behind the clock restarts from now"), Host.NextAI, 101.0, Tol);

	// The move clock has no gate at all and does not preserve its stamp.
	Host.NextMove = 500.0;
	ElysiumNpcThink::CalcNextMoveThink(Host, 20.0);
	TestEqual(TEXT("the move clock is always curtime + 0.001"), Host.NextMove, 20.001, Tol);
	TestEqual(TEXT("...and mirrors what it replaced"), Host.LastMove, 500.0, Tol);

	// Slot 614's reset puts all four back on the clock and leaves the mirrors alone.
	Host.ResetThinkTimers(50.0);
	TestEqual(TEXT("the reset lands all four stamps on now"),
		Host.NextUpdate + Host.NextNormal + Host.NextMove + Host.NextAI, 200.0, Tol);
	TestEqual(TEXT("...and does not touch the mirrors"), Host.LastMove, 500.0, Tol);
	return true;
}

// --- The cadence on a live world -------------------------------------------------------------
// Everything above is arithmetic. These drive the real `Think` and assert what a reader can see
// from outside it: how often a body is asked, and what a pass does when the AI clock declines.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkCadenceLiveTest,
	"Elysium.Substrate.NpcThinkCadence.Live", GElysiumTestFlags)
bool FElysiumThinkCadenceLiveTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("__npccadence_test__"), 0x43414445);
	FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
	GuardDef.Keys.Add(TEXT("vision"), TEXT("4000"));
	GuardDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("the guard leaf constructs"), Guard)
		|| !TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}

	// --- In the player's view: the normal law's 0.1 s LOS pin ---------------------------------
	Player->Origin = FVector(100.0 * ElysiumMove::U, 0.0, 0.0);
	F.Advance(2.0);
	TestTrue(TEXT("a body in LOS is on the normal law's pin"),
		Guard->ScheduleHost.NextNormal - F.World.NowSeconds() <= 0.1 + 1e-3);
	TestTrue(TEXT("...and its AI clock is pinned with it"),
		Guard->ScheduleHost.NextAI - F.World.NowSeconds() <= 0.1 + 1e-3);

	// --- Out of PVS: the distance branch, scaled by the exclusive `x10 cap 16` arm -------------
	// Inside the `20000.0` search bound, so there IS a closest player and the law takes its
	// distance branch rather than the no-player sentinel.
	F.Services.PvsQuery = [](const FVector&, const FVector&) { return false; };
	Player->Origin = FVector(15000.0 * ElysiumMove::U, 0.0, 0.0);
	// Past the 2 s `SetPlayerLOS` gate AND past the 8 s hysteresis, so the cached LOS has really
	// dropped rather than merely gone stale.
	F.Advance(F.World.NowSeconds() + 14.0, 0.05);
	const double Now = F.World.NowSeconds();
	TestFalse(TEXT("the body is out of the player's PVS"), Guard->Senses.Memory.bPlayerInPvs);
	TestFalse(TEXT("...and out of its LOS"), Guard->Senses.Memory.bPlayerLos);
	// The UPDATE clock, because it is the one law with no `SCHEDULE_CHANGED` term. This headless
	// guard has no activity resolver behind it, so its idle program fails and reselects on every
	// pass and the bit is set on every pass -- which pins the normal and AI clocks to 0.1 s. That
	// pin is retail's own and correct; it just makes those two clocks the wrong thing to read here.
	TestTrue(TEXT("an unseen distant body's update clock is throttled well past the pin"),
		Guard->ScheduleHost.NextUpdate - Now > 1.0);
	TestTrue(TEXT("...and never past the law's `x10 cap 16`"),
		Guard->ScheduleHost.NextUpdate - Now <= 16.0 + 1e-3);
	TestTrue(TEXT("the entity think is the earlier of the two written clocks"),
		FMath::IsNearlyEqual(static_cast<double>(Guard->NextThink),
			FMath::Min(Guard->ScheduleHost.NextUpdate, Guard->ScheduleHost.NextNormal), 1e-3));
	// The in-think install pin, on the same hidden body: this guard's idle program reselects
	// inside every think (no activity resolver), so `OnScheduleChange` sets `SCHEDULE_CHANGED`
	// inside the think and the normal law reads it before it is cleared -- 0.1 s out of PVS,
	// where the distance branch alone would have chosen seconds.
	TestTrue(TEXT("a program installed inside the think pins the hidden body's normal clock"),
		Guard->ScheduleHost.NextNormal - Now <= 0.1 + 1e-3);
	return true;
}

// The slot-614 sites, each against the port's own door, and the non-sites that used to reset.
namespace
{
	// Push every stamp off `Now` so a re-base is a write and never a coincidence.
	void Prime(FElysiumNpc& Npc, double Now)
	{
		Npc.ScheduleHost.NextUpdate = Npc.ScheduleHost.NextNormal =
			Npc.ScheduleHost.NextMove = Npc.ScheduleHost.NextAI = Now + 50.0;
		Npc.ScheduleHost.LastUpdate = Npc.ScheduleHost.LastNormal =
			Npc.ScheduleHost.LastMove = Npc.ScheduleHost.LastAI = 1.0;
		Npc.NextThink = static_cast<float>(Now + 50.0);
	}
	bool NextStampsAt(const FElysiumNpc& Npc, double At)
	{
		const FElysiumNpcScheduleHost& H = Npc.ScheduleHost;
		return FMath::IsNearlyEqual(H.NextUpdate, At, 1e-6) && FMath::IsNearlyEqual(H.NextNormal, At, 1e-6)
			&& FMath::IsNearlyEqual(H.NextMove, At, 1e-6) && FMath::IsNearlyEqual(H.NextAI, At, 1e-6);
	}
	bool LastStampsAt(const FElysiumNpc& Npc, double At)
	{
		const FElysiumNpcScheduleHost& H = Npc.ScheduleHost;
		return FMath::IsNearlyEqual(H.LastUpdate, At, 1e-6) && FMath::IsNearlyEqual(H.LastNormal, At, 1e-6)
			&& FMath::IsNearlyEqual(H.LastMove, At, 1e-6) && FMath::IsNearlyEqual(H.LastAI, At, 1e-6);
	}
	bool Untouched(const FElysiumNpc& Npc, double Now)
	{
		return NextStampsAt(Npc, Now + 50.0) && FMath::IsNearlyEqual(Npc.ScheduleHost.LastNormal, 1.0, 1e-6);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkResetSitesTest,
	"Elysium.Substrate.NpcThinkCadence.ResetSites", GElysiumTestFlags)
bool FElysiumThinkResetSitesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("__npcreset_test__"), 0x52455345);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("far"), FVector(3000.0 * ElysiumMove::U, 0.0, 0.0));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumNpc* Far = F.Npc(TEXT("far"));
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("far"), Far)
		|| !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	Player->Origin = FVector(100.0 * ElysiumMove::U, 0.0, 0.0);
	F.Advance(1.0);
	FElysiumNpcWorldFixture::Quiet({ Guard, Far });   // so no think of their own rewrites a stamp
	const double Now = F.World.NowSeconds();

	// --- Sites ------------------------------------------------------------------------------
	// `SetDisableAI` `0x1029f300`: the 1 -> 0 edge only.
	Prime(*Guard, Now);
	Guard->SetDisableAi(true);
	TestTrue(TEXT("disabling the AI touches no stamp"), Untouched(*Guard, Now));
	Guard->SetDisableAi(false);
	TestTrue(TEXT("re-enabling it is slot 614"), NextStampsAt(*Guard, Now));
	TestTrue(TEXT("...on the Next stamps only"), FMath::IsNearlyEqual(Guard->ScheduleHost.LastNormal, 1.0, 1e-6));
	Guard->SetDisableAi(true);

	// `LeaveGrappleState` `0x102b5d90`: the base body then slot 614.
	Prime(*Guard, Now);
	Guard->LeaveGrappleState();
	TestTrue(TEXT("leaving a grapple is slot 614"), NextStampsAt(*Guard, Now));

	// Dialogue START: the three `StartPlayerDialog*` inputs and `PlayerUse` all re-base before
	// installing; the port's one door is the dialogue body session.
	Prime(*Guard, Now);
	const FElysiumBodyOwnerToken Token = Guard->BeginDialogueBodySession();
	TestTrue(TEXT("a dialogue session begins with slot 614"), NextStampsAt(*Guard, Now));
	Prime(*Guard, Now);
	Guard->EndDialogueBodySession(Token, /*bSilent=*/true);
	TestTrue(TEXT("...and ends with no reset at all"), Untouched(*Guard, Now));

	// `InputDisableThink` `0x1029f2a0`: a bool variant through, anything else means false.
	Guard->SetDisableAi(false);
	Prime(*Guard, Now);
	FElysiumInputArgs DisableArgs;
	DisableArgs.Param = FElysiumVariant::Bool(true);
	Guard->InputDisableThink(DisableArgs);
	TestTrue(TEXT("DisableThink 1 disables the AI"), Guard->IsAiDisabled());
	FElysiumInputArgs StringArgs;
	StringArgs.Param = FElysiumVariant::String(TEXT("1"));
	Guard->InputDisableThink(StringArgs);
	TestFalse(TEXT("a non-bool variant re-enables it (retail passes false)"), Guard->IsAiDisabled());
	TestTrue(TEXT("...which is the 1 -> 0 edge, so slot 614"), NextStampsAt(*Guard, Now));
	Guard->SetDisableAi(true);

	// The spoken-line player `0x102c0520`: talking until the line ends, and slot 614.
	Prime(*Guard, Now);
	Guard->OnDialogFilePlayed(2.5);
	TestTrue(TEXT("a played line is slot 614"), NextStampsAt(*Guard, Now));
	TestTrue(TEXT("...and the body is talking until it ends"),
		Guard->IsTalking(Now + 2.4) && !Guard->IsTalking(Now + 2.6));
	TestTrue(TEXT("...which pins ShouldThinkFrequently"), ElysiumNpcThink::ShouldThinkFrequently(*Guard));
	Guard->TalkingUntil = -1.0;

	// The teleport broadcast `0x1028d820`: slot 583 on every NPC, 2048 units around the point.
	Prime(*Guard, Now);
	Prime(*Far, Now);
	F.World.WakeNpcsNear(FVector::ZeroVector);
	TestTrue(TEXT("an NPC within 2048 units of the teleport point takes slot 614"), NextStampsAt(*Guard, Now));
	TestTrue(TEXT("...and one 3000 units away does not"), Untouched(*Far, Now));

	// `SetAIEnabled` `0x10265680`: off touches nothing; on re-bases EVERY NPC's eight stamps.
	Prime(*Guard, Now);
	Prime(*Far, Now);
	F.World.SetAiEnabled(false);
	TestTrue(TEXT("disabling the map's AI touches no stamp"), Untouched(*Guard, Now) && Untouched(*Far, Now));
	F.World.SetAiEnabled(true);
	TestTrue(TEXT("enabling it re-bases every NPC's Next stamps"), NextStampsAt(*Guard, Now) && NextStampsAt(*Far, Now));
	TestTrue(TEXT("...and their Last stamps (slot 584's shape)"), LastStampsAt(*Guard, Now) && LastStampsAt(*Far, Now));

	// --- Non-sites --------------------------------------------------------------------------
	// `aiscripted_schedule`: `0x101a98c0` -> `SetSchedule` `0x10280e50`, no stamp anywhere.
	Prime(*Guard, Now);
	FElysiumScriptedScheduleOrder Order;
	Order.Mode = 0;
	Guard->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, EElysiumNpcState::Alert);
	TestTrue(TEXT("an aiscripted_schedule push touches no stamp"), Untouched(*Guard, Now));
	// The patrol and interesting-place inputs, and a named-schedule input.
	Prime(*Guard, Now);
	FElysiumInputArgs One;
	One.Param = FElysiumVariant::Int(1);
	Guard->InputUseInteresting(One);
	TestTrue(TEXT("UseInteresting touches no stamp"), Untouched(*Guard, Now));
	FElysiumInputArgs Named;
	Named.Param = FElysiumVariant::String(TEXT("SCHED_IDLE_STAND"));
	Guard->InputNamedSchedule(Named);
	TestTrue(TEXT("a ChangeSchedule input touches no stamp"), Untouched(*Guard, Now));
	return true;
}

// The AI console gate `0x1026c3d0` inside `NPCThink`, and what re-enabling does to a body it
// silenced.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkAiGateTest,
	"Elysium.Substrate.NpcThinkCadence.AiGate", GElysiumTestFlags)
bool FElysiumThinkAiGateTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("__npcaigate_test__"), 0x41494741);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	Player->Origin = FVector(100.0 * ElysiumMove::U, 0.0, 0.0);
	F.Advance(1.0);
	TestTrue(TEXT("a body in view thinks on the pin"),
		Guard->NextThink != ELYSIUM_NEVER_THINK && Guard->NextThink - F.World.NowSeconds() <= 0.1 + 1e-3);

	F.World.SetAiEnabled(false);
	F.Advance(2.0);
	TestEqual(TEXT("with the AI disabled, a normal-due think refuses and does not re-arm"),
		Guard->NextThink, ELYSIUM_NEVER_THINK);
	const double Before = Guard->ScheduleHost.NextNormal;
	F.Advance(3.0);
	TestTrue(TEXT("...and no law runs while it is silent"),
		FMath::IsNearlyEqual(Guard->ScheduleHost.NextNormal, Before, 1e-6));

	F.World.SetAiEnabled(true);
	const double At = F.World.NowSeconds();
	TestTrue(TEXT("re-enabling puts the body on every clock at once"),
		NextStampsAt(*Guard, At) && LastStampsAt(*Guard, At));
	F.Advance(At + 0.5);
	TestTrue(TEXT("...and it is thinking again"),
		Guard->NextThink != ELYSIUM_NEVER_THINK && Guard->ScheduleHost.NextNormal > At);
	return true;
}

// `TASK_WAIT_PVS` (`0x102aacf0`, task 5) and the state byte's PVS/LOS force.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkWaitPvsAndStateByteTest,
	"Elysium.Substrate.NpcThinkCadence.WaitPvsAndStateByte", GElysiumTestFlags)
bool FElysiumThinkWaitPvsAndStateByteTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("__npcwaitpvs_test__"), 0x57505653);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	Player->Origin = FVector(1000.0 * ElysiumMove::U, 0.0, 0.0);
	F.Advance(1.0);   // `SetClosestPlayer` has run: the task's PVS test has a player to ask about
	FElysiumNpcWorldFixture::Quiet({ Guard });
	const double Now = F.World.NowSeconds();

	// --- WAIT_PVS ---------------------------------------------------------------------------
	F.Services.PvsQuery = [](const FVector&, const FVector&) { return false; };
	Prime(*Guard, Now);
	TestFalse(TEXT("out of the player's PVS the task keeps waiting"), Guard->WaitPvs());
	TestTrue(TEXT("...and touches no stamp"), Untouched(*Guard, Now));
	F.Services.PvsQuery = nullptr;
	TestTrue(TEXT("in PVS the task completes"), Guard->WaitPvs());
	TestTrue(TEXT("...re-basing all eight stamps"), NextStampsAt(*Guard, Now) && LastStampsAt(*Guard, Now));
	F.Services.PvsQuery = [](const FVector&, const FVector&) { return false; };
	Guard->SpawnFlags |= 0x400;   // SF_NPC_ALWAYSTHINK
	Prime(*Guard, Now);
	TestTrue(TEXT("SF_NPC_ALWAYSTHINK completes at once, out of PVS"), Guard->WaitPvs());
	TestTrue(TEXT("...with no clock work"), Untouched(*Guard, Now));
	Guard->SpawnFlags &= ~0x400;

	// --- The state byte `0x1026e3e0` -------------------------------------------------------
	// The state is pushed through the director's forced-state door, the one public writer a
	// case has; the AI is quiet, so nothing re-selects it.
	auto ForceState = [Guard](EElysiumNpcState State)
	{
		FElysiumScriptedScheduleOrder Order;
		Order.Mode = 0;
		Guard->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, State);
	};
	TestEqual(TEXT("idle is 0x31"), static_cast<int32>(Guard->NpcStateFlags()), 0x31);
	Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	Guard->Senses.SetPlayerLos(*Guard, Now);
	TestFalse(TEXT("an idle body out of the player's PVS reads it"), Guard->Senses.Memory.bPlayerInPvs);
	ForceState(EElysiumNpcState::Alert);
	TestEqual(TEXT("alert is 0x39"), static_cast<int32>(Guard->NpcStateFlags()), 0x39);
	Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	Guard->Senses.SetPlayerLos(*Guard, Now + 3.0);
	TestTrue(TEXT("an alert body is forced into the player's PVS and LOS by bit 3"),
		Guard->Senses.Memory.bPlayerInPvs && Guard->Senses.Memory.bPlayerLos);
	ForceState(EElysiumNpcState::Combat);
	TestEqual(TEXT("combat is 0x8f"), static_cast<int32>(Guard->NpcStateFlags()), 0x8f);
	ForceState(EElysiumNpcState::Idle);
	return true;
}

// `COND_WAS_BUMPED` (0x38). The bit itself, its one-pass life, and the recovered guard that keeps
// it off an NPC whose program does not list it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThinkWasBumpedTest,
	"Elysium.Substrate.NpcThinkCadence.WasBumped", GElysiumTestFlags)
bool FElysiumThinkWasBumpedTest::RunTest(const FString&)
{
	TestEqual(TEXT("the condition carries its recovered id"),
		static_cast<int32>(EElysiumNpcCond::WasBumped), 0x38);
	TestEqual(TEXT("...and its recovered name"),
		FString(ElysiumNpcCondName(EElysiumNpcCond::WasBumped)), FString(TEXT("WAS_BUMPED")));

	FElysiumNpcWorldBuilder Builder(TEXT("__npcbump_test__"), 0x42554d50);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("the guard leaf constructs"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// The guard's own program does not list `WAS_BUMPED`, and retail's producer consults
	// `ConditionInterruptsCurrentSchedule` (`0x10269c70`) before it sets the bit -- so nothing is
	// recorded at all. That refusal is the recovered behaviour, not an omission.
	Guard->OnBumped(1.0);
	TestTrue(TEXT("a program that does not list the bit records no bump"),
		Guard->Senses.Memory.LastBumpTime < 0.0);

	// Written by hand, the reconstruction behaves like the damage pair: live for the first pass
	// that reads it, gone for the second.
	Guard->Senses.Memory.LastBumpTime = 5.0;
	FElysiumNpcConditions Out;
	ElysiumNpcCond::GatherBump(*Guard, 4.0, Out);
	TestTrue(TEXT("a bump newer than the last pass is decision input"),
		Out.Has(EElysiumNpcCond::WasBumped));
	FElysiumNpcConditions Second;
	ElysiumNpcCond::GatherBump(*Guard, 6.0, Second);
	TestFalse(TEXT("...and is gone by the pass after it"),
		Second.Has(EElysiumNpcCond::WasBumped));
	return true;
}

} // namespace ElysiumNpcThinkCadenceTests

#endif // WITH_DEV_AUTOMATION_TESTS
