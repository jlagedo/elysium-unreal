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
