// `CBasePlayer::PostThink 0x1016be10`'s animation step (spec 0002 V4a, lane A4; ruling J3): the
// advance, then slot 258 — the overlay body `0x10098c80`: the base `0x10091880`, then the layer
// `0x10098cd0` — then slot 312's stand-in, with `CBasePlayer::HandleAnimEvent 0x10178a10` as the
// handler.
#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

#include "Components/SkeletalMeshComponent.h"

namespace ElysiumPlayerPostThinkTests
{
static constexpr EAutomationTestFlags GFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static FElysiumAnimEvent Ev(float Cycle, int32 Id, const TCHAR* Options = TEXT(""))
{
	FElysiumAnimEvent Event;
	Event.Cycle = Cycle;
	Event.Event = Id;
	Event.Options = Options;
	return Event;
}

// Stand the scripted pose-layer record on one channel of the player's body: a 1.0 s clip at
// playback rate 1.0, so the cycle rate is 1.0 per second and the look-ahead is 0.1 of a cycle.
static void StandOn(FElysiumRecordingServices& Services, EElysiumAnimChannel Channel,
	const TCHAR* Owner, const TCHAR* Label, uint32 PlayId, float Cycle)
{
	Services.bBodyClipPhaseSet = true;
	Services.BodyClipPhase = FElysiumClipPhase();
	Services.BodyClipPhase.Channel = Channel;
	Services.BodyClipPhase.OwnerStem = Owner;
	Services.BodyClipPhase.Label = Label;
	Services.BodyClipPhase.PlayId = PlayId;
	Services.BodyClipPhase.Cycle = Cycle;
	Services.BodyClipPhase.Length = 1.0f;
	Services.BodyClipPhase.PlayRate = 1.0f;
	Services.BodyClipPhase.bLooping = true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerPostThinkOrderTest,
	"Elysium.Arm.Player.PostThinkOrder", GFlags)
bool FElysiumPlayerPostThinkOrderTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F([]
		{
			FElysiumNpcWorldBuilder Builder(TEXT("player_post_think_order"), 4104);
			Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
			return Builder;
		}());
	FElysiumPlayer* Player = F.Player();
	if (!TestNotNull(TEXT("the player"), Player))
	{
		return false;
	}
	Player->SetRuntimeModel(TEXT("models/character/pc/male/tremere_armor_0.mdl"));
	if (!TestNotNull(TEXT("the player's body"), Player->Visual))
	{
		return false;
	}
	FElysiumRecordingServices& Services = F.Services;
	ElysiumAnimEventCensus::Clear();

	const TCHAR* const Bank = TEXT("pc_bank");
	Services.NpcEventTimelines.Add(FElysiumRecordingServices::EventTimelineKey(Bank, TEXT("walk")))
		.Append({ Ev(0.25f, 2050), Ev(0.75f, 2051) });
	Services.NpcEventTimelines.Add(
		FElysiumRecordingServices::EventTimelineKey(Bank, TEXT("pistol_attack_layer")))
		.Add(Ev(0.10f, 3031));
	Services.NpcEventTimelines.Add(FElysiumRecordingServices::EventTimelineKey(Bank, TEXT("run")))
		.Add(Ev(0.05f, 2052));

	// Every `animevent` the dispatcher emits for the player, with whether the stealth action was
	// still owned when the event was handed to slot 259.
	struct FSeen
	{
		FString Text;
		bool bOwnedStealthAction = false;
	};
	TArray<FSeen> Seen;
	F.World.SetAiTraceSink([&Seen, Player](const FElysiumAiTraceEvent& Event)
	{
		if (Event.Kind == FName(TEXT("animevent")) && Event.Entity == Player->Handle)
		{
			FSeen Row;
			Row.Text = Event.Text;
			Row.bOwnedStealthAction = Player->Grapple.bOwnsStealthAction;
			Seen.Add(MoveTemp(Row));
		}
	});
	ON_SCOPE_EXIT { F.World.SetAiTraceSink(FElysiumAiTraceSink()); };

	// --- 1. The order: dispatch (slot 258), then slot 312's stand-in -----------------------------
	//
	// The player owns a stealth action whose partner no longer resolves, so `TickStealthKill` (the
	// stand-in for slot 312) leaves the pair the moment it runs. The base clip stands at 0.30, so
	// the window is [0, 0.40): the 2050 at 0.25 is inside it, the 2051 at 0.75 is not.
	StandOn(Services, EElysiumAnimChannel::Base, Bank, TEXT("walk"), 1, 0.30f);
	Player->Grapple.Type = EElysiumGrappleType::StealthKill;
	Player->Grapple.Role = EElysiumGrappleRole::Attacker;
	Player->Grapple.bOwnsStealthAction = true;

	Player->PostThinkAnimation();

	if (TestEqual(TEXT("0x10091880: the one record inside [m_flLastEventCheck, cycle + 0.1 x rate)"),
		Seen.Num(), 1))
	{
		TestTrue(TEXT("0x1016be10 slot 258: the base channel's 2050 reaches the player's handler"),
			Seen[0].Text.StartsWith(TEXT("2050")));
		TestTrue(TEXT("0x1016be10: slot 258 runs before slot 312 (the stealth action still owned)"),
			Seen[0].bOwnedStealthAction);
	}
	TestFalse(TEXT("0x1016be10: slot 312's stand-in ran after the dispatch"),
		Player->Grapple.bOwnsStealthAction);
	TestEqual(TEXT("0x10091880: m_flLastEventCheck (+0x658) = cycle + 0.1 x cycle rate"),
		Player->PostThinkChannels[0].Words.LastEventCheck, 0.40f, 1.e-4f);

	// --- 2. 2050..2053 are swallowed by `0x10178a10` ----------------------------------------------
	//
	// The next tick's window is [0.40, 0.80): the 2051. The handler claims it and does nothing — the
	// step sound stays the step clock's — so no service call is made and the census gains no row.
	Services.BodyClipPhase.Cycle = 0.70f;
	const int32 CallsBefore = Services.Calls.Num();
	Player->PostThinkAnimation();
	if (TestEqual(TEXT("0x10091880: the window opens where the last one ended"), Seen.Num(), 2))
	{
		TestTrue(TEXT("0x1016be10 slot 258: the base channel's 2051"),
			Seen[1].Text.StartsWith(TEXT("2051")));
	}
	TestEqual(TEXT("0x10178a10: 2050..2053 play nothing"), Services.Calls.Num(), CallsBefore);
	TestEqual(TEXT("0x10178a10: and are claimed, never census work"), ElysiumAnimEventCensus::Num(), 0);
	TestTrue(TEXT("0x10178a10: the swallow arm claims 2050"), Player->HandleAnimEvent(Ev(0.f, 2050)));
	TestTrue(TEXT("0x10178a10: 0x80c is the player's own arm and never reaches 0x1032e330"),
		Player->HandleAnimEvent(Ev(0.f, 2060, TEXT("smile 1.0 0.2 0.2"))));

	// --- 3. The overlay layer dispatches too: `0x10098cd0` ----------------------------------------
	//
	// The firearm's 3031 is authored on the `*_attack_layer` clip, which composes on overlay layer 0.
	// The base stands on nothing here, as the double answers one channel at a time.
	StandOn(Services, EElysiumAnimChannel::UpperBody, Bank, TEXT("pistol_attack_layer"), 2, 0.05f);
	Player->PostThinkAnimation();
	if (TestEqual(TEXT("0x10098c80: the layer's record is dispatched"), Seen.Num(), 3))
	{
		TestTrue(TEXT("0x10098cd0: the layer's 3031 reaches the player's handler"),
			Seen[2].Text.StartsWith(TEXT("3031")));
	}
	const FElysiumAnimEvent Shot = Ev(0.10f, 3031);
	TestEqual(TEXT("0x10178a10: the weapon band is 0x1032e330's, the player adds nothing of its own"),
		Player->HandleAnimEvent(Shot), Player->FElysiumCombatCharacter::HandleAnimEvent(Shot));
	TestFalse(TEXT("0x1016be10: a channel standing on nothing keeps no sequence"),
		Player->PostThinkChannels[0].bArmed);
	TestEqual(TEXT("0x10098cd0: layer+0x2c = the layer's look-ahead end"),
		Player->PostThinkChannels[1].Words.LastEventCheck, 0.15f, 1.e-4f);

	// A layer's look-ahead end is not clamped and its finish word is never set.
	Services.BodyClipPhase.Cycle = 0.95f;
	Player->PostThinkAnimation();
	TestEqual(TEXT("0x10098cd0: no clamp at 1.0"),
		Player->PostThinkChannels[1].Words.LastEventCheck, 1.05f, 1.e-4f);
	TestFalse(TEXT("0x10098cd0: layer+4 is zeroed and never set"),
		Player->PostThinkChannels[1].Words.bSequenceFinished);

	// --- 4. A clip change restarts the window at 0: `ResetSequenceInfo 0x10090950` -----------------
	const int32 SeenBeforeRun = Seen.Num();
	StandOn(Services, EElysiumAnimChannel::Base, Bank, TEXT("run"), 3, 0.50f);
	Player->PostThinkAnimation();
	if (TestEqual(TEXT("0x10090950: a new sequence's window opens at 0, so its 0.05 record fires"),
		Seen.Num(), SeenBeforeRun + 1))
	{
		TestTrue(TEXT("0x10090950: the new clip's 2052"), Seen.Last().Text.StartsWith(TEXT("2052")));
	}
	TestEqual(TEXT("0x10091880: m_flLastEventCheck (+0x658) on the new sequence"),
		Player->PostThinkChannels[0].Words.LastEventCheck, 0.60f, 1.e-4f);

	// The same clip, played again: another `ResetSequenceInfo`, another window from 0.
	StandOn(Services, EElysiumAnimChannel::Base, Bank, TEXT("run"), 4, 0.02f);
	Player->PostThinkAnimation();
	TestEqual(TEXT("0x10090950: a replay of the same clip opens at 0 again"),
		Seen.Num(), SeenBeforeRun + 2);
	TestEqual(TEXT("0x10091880: m_flLastEventCheck (+0x658) after the replay's first window"),
		Player->PostThinkChannels[0].Words.LastEventCheck, 0.12f, 1.e-4f);
	return true;
}

}   // namespace ElysiumPlayerPostThinkTests

#endif   // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
