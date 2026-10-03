// 0018 story 5, the 0.8 s AI-network gate and the node-graph loader byte.
//
// `CAI_BaseNPC::NPCThink` `0x1026ca80` writes `m_flNextThink = curtime + 0.1` first and returns unless
// `g_pAINetworkManager && manager+0x658` (`0x1026cb2a` / `0x1026cb36`); the byte is set by the manager's
// think `0x102f6a50`, armed at `curtime + 0.8` (`_DAT_104491a8`) by `0x102f6690` inside `CWorld::Precache`,
// which a save restore re-runs. The Troika body `0x10292de0` never reads it; it gates on the loader byte
// `DAT_1093408c` alone (`Think19NodeGraphBuilt`), which is true once the map's place set is adopted.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapPlaces.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

static constexpr EAutomationTestFlags GThink19GateTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A bare Troika body whose slot 432 counts, so a think that reaches the AI pass is visible.
	class FThink19GateProbe final : public FElysiumNpc
	{
	public:
		virtual void RunAI(bool /*bReduced*/) override
		{
			++RunAiCalls;
		}
		// Vslot 584, counted with the curtime it ran at, then the Troika body.
		virtual void Slot584(int32 Arg) override
		{
			++Slot584Calls;
			LastSlot584At = World != nullptr ? World->NowSeconds() : 0.0;
			FElysiumNpc::Slot584(Arg);
		}
		int32 RunAiCalls = 0;
		int32 Slot584Calls = 0;
		double LastSlot584At = -1.0;
	};

	enum class EThink19GateMotor : uint8
	{
		None,
		Provided,
	};

	FElysiumNpcWorldBuilder MakeThink19GateBuilder(EThink19GateMotor Motor = EThink19GateMotor::None)
	{
		FElysiumNpcWorldBuilder Builder(TEXT("think19_gate"), 1932);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		FElysiumEntityDef& Def = Builder.AddTroikaNpc(TEXT("guard"), FVector::ZeroVector);
		Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FThink19GateProbe>(); };
		if (Motor == EThink19GateMotor::Provided)
		{
			// A body-carrying NPC: the model key is what makes the leaf prepare a visual and build its
			// motor (the `NpcKernelMaintain` fixture's idiom).
			Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
		}
		return Builder;
	}

	// The standard fixture: an adopted (empty) place set, the map built at -0.1 s (the fixture's spawn
	// clock) and the frame ending at 0.0, so the base line's gate opens at 0.7 s.
	struct FThink19GateFixture
	{
		FElysiumNpcWorldFixture W;
		FThink19GateProbe* Guard = nullptr;

		explicit FThink19GateFixture(EThink19GateMotor Motor = EThink19GateMotor::None)
			: W(MakeThink19GateBuilder(Motor), [Motor](FElysiumRecordingServices& Services)
				{
					Services.bProvideNpcMotor = Motor == EThink19GateMotor::Provided;
					Services.bNpcActivitiesResolve = Motor == EThink19GateMotor::Provided;
				})
		{
			Guard = static_cast<FThink19GateProbe*>(W.Npc(TEXT("guard")));
			FElysiumNpcWorldFixture::Quiet({ Guard });
		}

		double Now() const { return W.World.NowSeconds(); }
	};

	// The value `NPCThink` writes first: `curtime + 0.1` from the double at `0x104493d0`.
	float Think19GateTenthAhead(double Now)
	{
		return static_cast<float>(Now + ElysiumNpcTunables::TenthDouble);
	}
}

// --- The 0.8 s gate on the base line ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThink19GateBaseLineTest,
	"Elysium.Arm.Think19.Gate.BaseLine", GThink19GateTestFlags)
bool FElysiumThink19GateBaseLineTest::RunTest(const FString&)
{
	FThink19GateFixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19GateProbe& N = *F.Guard;
	const double Stamp = F.W.World.BuildStampSeconds();
	TestNearlyEqual(TEXT("0x102f6690 the map is stamped when it is built"), static_cast<float>(Stamp),
		static_cast<float>(-FElysiumNpcBase::NpcInitThinkDelay));

	// Before build + 0.8: `m_flNextThink` still advances by 0.1, and nothing else runs.
	const int32 PostBefore = N.MotorSeams.PostRunWeaponUpdates;
	F.W.Advance(Stamp + FElysiumNpcBase::AiNetworkFirstThinkDelay - 0.05);
	double Now = F.Now();
	TestFalse(TEXT("0x1026cb36 +0x658 unset just before build + 0.8"), N.Think19AiNetworkReady());
	N.NextThink = 55.f;
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026cb1d m_flNextThink = curtime + 0.1 on a gated think"), N.NextThink, Think19GateTenthAhead(Now));
	TestEqual(TEXT("0x1026ccf5 gated: no slot 432"), N.RunAiCalls, 0);
	TestEqual(TEXT("gated: no PostRun"), N.MotorSeams.PostRunWeaponUpdates, PostBefore);

	// After it: the body runs on through the AI pass.
	F.W.Advance(Stamp + FElysiumNpcBase::AiNetworkFirstThinkDelay + 0.05);
	Now = F.Now();
	TestTrue(TEXT("0x1026cb36 +0x658 set once build + 0.8 has passed"), N.Think19AiNetworkReady());
	N.NextThink = 55.f;
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026cb1d the stamp is written up front either way"), N.NextThink, Think19GateTenthAhead(Now));
	TestEqual(TEXT("0x1026cc2a open: slot 432 runs"), N.RunAiCalls, 1);
	TestEqual(TEXT("0x1026cc32 open: PostRun runs"), N.MotorSeams.PostRunWeaponUpdates, PostBefore + 1);
	return true;
}

// --- The network manager's one-shot: slot 584 on every NPC at build + 0.8 (0019/6) --------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThink19GateSlot584OneShotTest,
	"Elysium.Arm.Think19.Gate.Slot584OneShot", GThink19GateTestFlags)
bool FElysiumThink19GateSlot584OneShotTest::RunTest(const FString&)
{
	FThink19GateFixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19GateProbe& N = *F.Guard;
	const double Stamp = F.W.World.BuildStampSeconds();
	F.W.Advance(Stamp + FElysiumNpcBase::AiNetworkFirstThinkDelay - 0.05);
	TestEqual(TEXT("0x102f6a50 not yet: no slot 584 before build + 0.8"), N.Slot584Calls, 0);
	F.W.Advance(Stamp + FElysiumNpcBase::AiNetworkFirstThinkDelay + 0.05);
	TestEqual(TEXT("0x1028d8d0 slot 584 runs once at build + 0.8"), N.Slot584Calls, 1);
	TestTrue(TEXT("on the first tick past the stamp"),
		N.LastSlot584At >= Stamp + FElysiumNpcBase::AiNetworkFirstThinkDelay);
	F.W.Advance(F.Now() + 2.0);
	TestEqual(TEXT("a one-shot: no second pass on the same stamp"), N.Slot584Calls, 1);
	return true;
}

// --- A restore re-runs Precache ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThink19GateRestoreTest,
	"Elysium.Arm.Think19.Gate.RestoreRestamps", GThink19GateTestFlags)
bool FElysiumThink19GateRestoreTest::RunTest(const FString&)
{
	FThink19GateFixture Saved;
	FThink19GateFixture Restored;
	if (!TestNotNull(TEXT("both probes stand"), Restored.Guard) || !TestNotNull(TEXT("saved"), Saved.Guard))
	{
		return false;
	}
	// The restored world has been up long enough that its build's gate is long open.
	Restored.W.World.Tick(5.0);
	TestTrue(TEXT("0x1026cb36 the first build's gate is open at 5.0"), Restored.Guard->Think19AiNetworkReady());

	ElysiumRoundTripSnapshot(Saved.W.World, Restored.W.World);
	// `0x101a2e40` runs Precache on the restored world entity: a fresh `curtime + 0.8` is armed.
	TestNearlyEqual(TEXT("0x102f6690 the restore re-stamps the world"),
		static_cast<float>(Restored.W.World.BuildStampSeconds()), static_cast<float>(Restored.Now()));
	TestFalse(TEXT("0x1026cb36 a restored map waits 0.8 s again"), Restored.Guard->Think19AiNetworkReady());
	Restored.W.World.Tick(Restored.Now() + FElysiumNpcBase::AiNetworkFirstThinkDelay + 0.05);
	TestTrue(TEXT("0x1026cb36 and then admits the base line"), Restored.Guard->Think19AiNetworkReady());
	return true;
}

// --- The Troika line ignores the 0.8 s gate ---------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThink19GateTroikaLineTest,
	"Elysium.Arm.Think19.Gate.TroikaIgnoresNetworkGate", GThink19GateTestFlags)
bool FElysiumThink19GateTroikaLineTest::RunTest(const FString&)
{
	FThink19GateFixture F;
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FThink19GateProbe& N = *F.Guard;
	N.SetDisableAi(false);
	TestFalse(TEXT("the base line's gate is still closed at 0.0"), N.Think19AiNetworkReady());
	TestTrue(TEXT("0x1026c3d0 the loader byte is set"), N.Think19NodeGraphBuilt());
	N.NPCThink();
	TestEqual(TEXT("0x10292de0 the Troika body never reads +0x658: slot 432 runs inside the 0.8 s"),
		N.RunAiCalls, 1);
	return true;
}

// --- The loader byte -------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThink19GateLoaderByteTest,
	"Elysium.Arm.Think19.Gate.LoaderByteFollowsPlaceSet", GThink19GateTestFlags)
bool FElysiumThink19GateLoaderByteTest::RunTest(const FString&)
{
	// A headless world that stages a map's place set: the loader byte waits for its adoption.
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.SetPlaceSetPending(true);
	FElysiumNpcWorldBuilder Builder = MakeThink19GateBuilder();
	ElysiumStandSpawnClock(World, -FElysiumNpcBase::NpcInitThinkDelay);
	World.Load(MoveTemp(Builder.Defs));
	World.SpawnPlayer();
	World.Activate(-FElysiumNpcBase::NpcInitThinkDelay);
	World.Tick(0.0);
	FElysiumNpc* Found = World.FindByName(TEXT("guard")) != nullptr ? World.FindByName(TEXT("guard"))->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("the probe stands"), Found))
	{
		return false;
	}
	FThink19GateProbe& N = *static_cast<FThink19GateProbe*>(Found);
	FElysiumNpcWorldFixture::Quiet({ &N });

	TestFalse(TEXT("DAT_1093408c is 0 while the staged place set is pending"), World.IsNodeGraphLoaded());
	TestFalse(TEXT("0x1026c3e3 the NPC reads the same byte"), N.Think19NodeGraphBuilt());
	// The base body, past its 0.8 s, still stops at the console gate on a graphless world.
	World.Tick(1.0);
	TestTrue(TEXT("the 0.8 s gate is open"), N.Think19AiNetworkReady());
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026c3d0 graphless: the console gate refuses, no slot 432"), N.RunAiCalls, 0);

	World.Places().AdoptRows(TArray<FElysiumPlaceRow>());
	TestTrue(TEXT("DAT_1093408c is 1 once the place set is adopted"), World.IsNodeGraphLoaded());
	TestTrue(TEXT("0x1026c3e3 the NPC reads the same byte"), N.Think19NodeGraphBuilt());
	N.FElysiumNpcBase::NPCThink();
	TestEqual(TEXT("0x1026cc2a with the graph loaded the pass runs"), N.RunAiCalls, 1);

	// A world never given a map place set has nothing to wait for (the rebuild arm sets the byte too).
	FElysiumRecordingServices BareServices;
	FElysiumEntityWorld Bare(nullptr, nullptr, BareServices.Bundle());
	TestTrue(TEXT("0x102f6610 a world with no staged place set is not graph-false"), Bare.IsNodeGraphLoaded());
	return true;
}

// --- The ai_step arm --------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumThink19GateAiStepTest,
	"Elysium.Arm.Think19.Gate.AiStepReadsPathHead", GThink19GateTestFlags)
bool FElysiumThink19GateAiStepTest::RunTest(const FString&)
{
	FThink19GateFixture F(EThink19GateMotor::Provided);
	if (!TestNotNull(TEXT("the probe stands"), F.Guard))
	{
		return false;
	}
	FElysiumRecordingNpcMotor* Motor = F.W.Services.NpcMotors.IsEmpty() ? nullptr : F.W.Services.NpcMotors[0].Get();
	if (!TestNotNull(TEXT("the probe has a motor"), Motor))
	{
		return false;
	}
	FThink19GateProbe& N = *F.Guard;
	F.W.World.SetAiStepMode(true);
	N.MaintainDebugTaskIndex = 0;   // >= DAT_105c9798 (-1): the goal test decides

	N.SequencePlaybackRate = 5.f;
	N.Navigator.bHasHeadWaypoint = false;                     // path+0x24: no head waypoint
	TestTrue(TEXT("0x102ee6a0 no path head"), !N.NavigatorGoalIsActive());
	TestFalse(TEXT("0x1026c4e8 ai_step: the console gate refuses"), N.Think19AiConsoleGate());
	TestEqual(TEXT("0x1026c4d5 with no path head +0x6f4 = 0"), N.SequencePlaybackRate, 0.f);

	N.SequencePlaybackRate = 5.f;
	N.Navigator.bHasHeadWaypoint = true;
	TestTrue(TEXT("0x102ee6a0 a path head exists"), N.NavigatorGoalIsActive());
	TestFalse(TEXT("0x1026c4e8 ai_step: the console gate refuses"), N.Think19AiConsoleGate());
	TestEqual(TEXT("with a path head +0x6f4 is left alone"), N.SequencePlaybackRate, 5.f);
	F.W.World.SetAiStepMode(false);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
