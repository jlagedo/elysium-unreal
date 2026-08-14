// Content-free Substrate automation: feeding cadence, maker outputs, and placed-model body closure.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed (CCC7)
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules (CCC1)
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumFeedingTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumSaveTestHelpers::SaveTestCounterValue;

// =====================================================================================
// B6 feeding — the cadence, the unit transaction, the acceptance policy, the paired state
// machine, idempotent teardown, the depleted-victim death path, and save/restore of an
// in-progress feed. `docs/vtmb/feeding.md` owns every number asserted here.
// =====================================================================================

namespace
{
	struct FFeedCameraRecordingService final : IElysiumCameraService
	{
		int32 AcquireCount = 0;
		int32 ReleaseCount = 0;
		FElysiumCameraRequest LastRequest;
		FElysiumCameraHandle LiveHandle;
		FElysiumResolvedCameraState Resolved;

		virtual FElysiumCameraHandle AcquireCamera(const FElysiumCameraRequest& Request) override
		{
			++AcquireCount;
			LastRequest = Request;
			LiveHandle.Reset();
			LiveHandle.Slot = 1;
			LiveHandle.Generation = static_cast<uint32>(AcquireCount);
			LiveHandle.Epoch = 1;
			Resolved.bActive = true;
			Resolved.Handle = LiveHandle;
			Resolved.Request = Request;
			return LiveHandle;
		}
		virtual bool UpdateCamera(FElysiumCameraHandle Handle,
			const FElysiumCameraRequest& Request) override
		{
			if (Handle != LiveHandle) return false;
			LastRequest = Request;
			Resolved.Request = Request;
			return true;
		}
		virtual bool ReleaseCamera(FElysiumCameraHandle Handle) override
		{
			if (Handle != LiveHandle) return false;
			++ReleaseCount;
			LiveHandle.Reset();
			Resolved.bActive = false;
			Resolved.Handle.Reset();
			return true;
		}
		virtual bool IsCameraLive(FElysiumCameraHandle Handle) const override
		{
			return Handle.IsSet() && Handle == LiveHandle;
		}
		virtual bool EvaluateDialogueCandidate(const FElysiumCameraRequest&, FString&) const override
		{
			return true;
		}
		virtual bool DialogueCamerasEnabled() const override { return false; }
		virtual void GetDialogueProfiles(TArray<FElysiumDialogueCameraProfile>&) const override {}
		virtual const FElysiumResolvedCameraState& ResolvedCamera() const override { return Resolved; }
	};

	// A victim and three counters wired off its feed/death outputs. The player is spawned by each
	// test that wants the ordinary retail attacker.
	FElysiumEntityDefs MakeFeedTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__feed_test__");

		FElysiumEntityDef Victim;
		Victim.Classname = TEXT("npc_VPedestrian");
		Victim.TargetName = TEXT("victim");
		Victim.Origin = FVector(100.0f, 0.0f, 0.0f);
		auto Wire = [&Victim](const TCHAR* Output, const TCHAR* Target)
		{
			FElysiumOutputDef Row;
			Row.Name = Output;
			Row.Target = Target;
			Row.Input = TEXT("Add");
			Row.Param = TEXT("1");
			Victim.Outputs.Add(MoveTemp(Row));
		};
		Wire(TEXT("OnFedUponBegin"), TEXT("begincount"));
		Wire(TEXT("OnFedUponEnd"), TEXT("endcount"));
		Wire(TEXT("OnDeath"), TEXT("deathcount"));
		Wire(TEXT("OnFedUponEnd"), TEXT("feed_end_order"));
		Wire(TEXT("OnDeath"), TEXT("death_order"));
		Defs.Defs.Add(MoveTemp(Victim));

		for (const TCHAR* Name : { TEXT("begincount"), TEXT("endcount"), TEXT("deathcount"),
			TEXT("feed_end_order"), TEXT("death_order") })
		{
			FElysiumEntityDef Counter;
			Counter.Classname = TEXT("math_counter");
			Counter.TargetName = Name;
			Defs.Defs.Add(MoveTemp(Counter));
		}
		return Defs;
	}

	FElysiumCombatCharacter* FeedTestCharacter(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Ent = World.FindByName(Name);
		return Ent ? Ent->AsCombatCharacter() : nullptr;
	}

	void SeedFeedSheet(FElysiumCombatCharacter& Char, int32 BloodPool, int32 MaxHealth, int32 Damage)
	{
		using EC = EElysiumTraitContainer;
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::BloodPool, BloodPool);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, Damage);
		Char.RecomputeSheet();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedingTest, "Elysium.Substrate.Feeding", GElysiumTestFlags)
bool FElysiumFeedingTest::RunTest(const FString&)
{
	using EC = EElysiumTraitContainer;
	FElysiumLogTap FeedLogs(512);
	FeedLogs.Install();
	ON_SCOPE_EXIT { FeedLogs.Remove(); };
	const uint64 FeedLogCursor = FeedLogs.Cursor();

	// --- The cadence, pure ---------------------------------------------------------------------
	// `initial interval = 0.30 + (B + 1) * 0.15`, so a fuller victim starts slower.
	TestEqual(TEXT("an empty victim's first interval"), ElysiumFeed::InitialInterval(0), 0.45f);
	TestEqual(TEXT("three blood points' first interval"), ElysiumFeed::InitialInterval(3), 0.90f);
	TestEqual(TEXT("a full (15) victim's first interval"), ElysiumFeed::InitialInterval(15), 2.70f);
	// `if (current > 0.30) current -= 0.15` — it accelerates to the floor and stays there.
	float Interval = ElysiumFeed::InitialInterval(3);
	const float Expected[] = { 0.75f, 0.60f, 0.45f, 0.30f, 0.30f, 0.30f };
	for (int32 Step = 0; Step < UE_ARRAY_COUNT(Expected); ++Step)
	{
		Interval = ElysiumFeed::NextInterval(Interval);
		TestEqual(*FString::Printf(TEXT("interval after %d steps"), Step + 1), Interval,
			Expected[Step], 1e-4f);
	}

	// --- The complementary ordinary pair, pure -----------------------------------------------
	{
		using Height = ElysiumFeed::EPartnerHeight;
		using Side = ElysiumFeed::ESide;
		const ElysiumFeed::FClipPair Tutorial = ElysiumFeed::ResolveClipPair(
			EElysiumFeedPhase::Release, Height::Taller, Side::Front);
		TestEqual(TEXT("a shorter attacker selects the captured tall-victim release"),
			Tutorial.Attacker, FString(TEXT("feeding_attacker_tallvictim_front_feed_release")));
		TestEqual(TEXT("the victim receives the complementary short-attacker release"),
			Tutorial.Victim, FString(TEXT("feeding_victim_shortattacker_front_feed_release")));
		TestEqual(TEXT("the captured release lasts 68 frames at 30 fps"),
			ElysiumFeed::PhaseSeconds(EElysiumFeedPhase::Release, Height::Taller),
			68.0f / 30.0f, 1e-5f);
		TestEqual(TEXT("the captured release event is at its authored cycle"),
			ElysiumFeed::EventCycle(EElysiumFeedPhase::Release, Height::Taller),
			0.328358f, 1e-6f);

		const ElysiumFeed::FClipPair Reverse = ElysiumFeed::ResolveClipPair(
			EElysiumFeedPhase::Loop, Height::Shorter, Side::Back);
		TestEqual(TEXT("the reverse height/side cell resolves on the attacker"), Reverse.Attacker,
			FString(TEXT("feeding_attacker_shortvictim_back_feed_loop")));
		TestEqual(TEXT("...and stays complementary on the victim"), Reverse.Victim,
			FString(TEXT("feeding_victim_tallattacker_back_feed_loop")));
		TestEqual(TEXT("an equal-height tie has one deterministic attacker cell"),
			static_cast<int32>(ElysiumFeed::VictimHeightFor(180.0f, 180.0f)),
			static_cast<int32>(Height::Shorter));
	}

	// --- Paired input keeps look and the toggle, not body/combat intent -----------------------
	{
		FElysiumUserCmd Cmd;
		Cmd.Seq = 7;
		Cmd.DeltaSeconds = 0.016f;
		Cmd.Move = FVector2D(1.0, -0.5);
		Cmd.Up = 1.0f;
		Cmd.LookDelta = FVector2D(4.0, -2.0);
		Cmd.Buttons = EElysiumButton::Forward | EElysiumButton::Attack;
		Cmd.Buttons |= static_cast<uint64>(EElysiumButton::Use);
		Cmd.Buttons |= static_cast<uint64>(EElysiumButton::Feed);
		const FElysiumUserCmd Gated = ElysiumFeed::GatePairedUserCmd(Cmd);
		TestTrue(TEXT("paired input removes analog movement"), Gated.Move.IsNearlyZero());
		TestEqual(TEXT("paired input removes vertical movement"), Gated.Up, 0.0f);
		TestTrue(TEXT("paired input locks look"), Gated.LookDelta.IsNearlyZero());
		TestTrue(TEXT("paired input preserves the second Feed press"),
			Gated.IsDown(EElysiumButton::Feed));
		TestFalse(TEXT("paired input suppresses attack"), Gated.IsDown(EElysiumButton::Attack));
		TestFalse(TEXT("paired input suppresses use"), Gated.IsDown(EElysiumButton::Use));
	}
	TestEqual(TEXT("female attacker feed start resolves through the character sound mirror"),
		ElysiumFeed::AudioPath(false, false, TEXT("start")),
		FString(TEXT("Character/Female/feed_on_start.wav")));
	TestEqual(TEXT("male victim feed loop resolves its complementary activity"),
		ElysiumFeed::AudioPath(true, true, TEXT("loop")),
		FString(TEXT("Character/Male/fed_upon_loop.wav")));

	// --- The unit transaction's two independent decisions, pure --------------------------------
	// Healing is still evaluated when the feeder's pool is full; the successful-blood counter is
	// not; the victim is drained either way (`DecBloodPool(false)` is called once regardless).
	{
		const ElysiumFeed::FPulseEffects Gained = ElysiumFeed::PulseEffects(/*bIncremented*/ true);
		TestTrue(TEXT("a landed increment counts as stolen"), Gained.bCountStolen);
		TestTrue(TEXT("...and heals"), Gained.bHeal);
		TestTrue(TEXT("...and drains the victim"), Gained.bDrainVictim);
		const ElysiumFeed::FPulseEffects Full = ElysiumFeed::PulseEffects(/*bIncremented*/ false);
		TestFalse(TEXT("a full feeder's pool does NOT count as stolen"), Full.bCountStolen);
		TestTrue(TEXT("...but the feed-heal is still evaluated"), Full.bHeal);
		TestTrue(TEXT("...and the victim is still drained"), Full.bDrainVictim);
		TestEqual(TEXT("the full-pool pulse remains explicit in the console contract"),
			ElysiumFeed::PulseLogLine(15, 15, 1, 0, 0),
			FString(TEXT("INFO - Feed pulse: player=15->15, victim=1->0, stolen=0")));
	}

	// --- The opposed comparison, pure ----------------------------------------------------------
	{
		FElysiumRollResult Roll;
		Roll.Net = 2;
		TestFalse(TEXT("an equal rating does not beat the roll (strictly greater)"),
			ElysiumFeed::OpposedAccepts(2, Roll));
		TestTrue(TEXT("one above does"), ElysiumFeed::OpposedAccepts(3, Roll));
		Roll.Net = -3;   // a botched defence
		TestFalse(TEXT("a negative net is floored at zero, so a zero rating still fails"),
			ElysiumFeed::OpposedAccepts(0, Roll));
		TestTrue(TEXT("...and one is enough against it"), ElysiumFeed::OpposedAccepts(1, Roll));
		TestEqual(TEXT("the victim rolls at the recovered difficulty"),
			ElysiumFeed::OpposedDifficulty, 6);
	}

	// --- Acceptance policy order ---------------------------------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeFeedTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FeedTestCharacter(World, TEXT("victim"));
		if (!TestNotNull(TEXT("the player entity exists"), Player)
			|| !TestNotNull(TEXT("the victim exists"), Victim))
		{
			return false;
		}
		SeedFeedSheet(*Player, /*Blood*/ 0, /*MaxHealth*/ 100, /*Damage*/ 0);
		SeedFeedSheet(*Victim, /*Blood*/ 3, /*MaxHealth*/ 100, /*Damage*/ 0);

		// 1. The automatic-acceptance states accept with no roll at all. The stand-in for retail's
		//    ACT_* state is the disposition name (see FElysiumCombatCharacter::IsFeedAutoAcceptState).
		Victim->Disposition = TEXT("cower");
		TestEqual(TEXT("a cowering victim accepts on the automatic-state route"),
			static_cast<int32>(Player->EvaluateFeedAcceptance(*Victim)),
			static_cast<int32>(EElysiumFeedVerdict::AcceptedAutomaticState));

		// 2. A non-resisting victim accepts without a roll either. With no rulebook the effect layer
		//    is empty, so the recovered `Fx_No_Resist_Feeding` flag is absent and the victim resists.
		Victim->Disposition = TEXT("normal");
		TestTrue(TEXT("an ordinary victim resists"), Victim->ResistsFeeding());

		// 3. Which sends it to the opposed check. Both feats read 0 with no rulebook loaded, so a
		//    zero rating cannot beat a floored-at-zero net and the request is refused — the same
		//    fail-closed posture CalcFeat takes for an unresolved gate. The Dice stream is pinned so
		//    the roll is the same one on every run.
		ElysiumRng::SeedAll(4242);
		TestEqual(TEXT("a resisting victim goes to the opposed check and wins it here"),
			static_cast<int32>(Player->EvaluateFeedAcceptance(*Victim)),
			static_cast<int32>(EElysiumFeedVerdict::RefusedOpposedCheck));

		// 4. Body ownership outranks all of it: an open conversation refuses the grapple even for a
		//    victim that would otherwise accept automatically.
		Victim->Disposition = TEXT("cower");
		World.EnqueueInput(TEXT("victim"), FName(TEXT("StartPlayerDialogRemote")),
			FElysiumVariant::Int(256), 0.0, FElysiumEntityHandle::Invalid(), Victim->Handle);
		World.Tick(0.0);
		TestEqual(TEXT("a victim in dialogue refuses"),
			static_cast<int32>(Player->EvaluateFeedAcceptance(*Victim)),
			static_cast<int32>(EElysiumFeedVerdict::RefusedBusy));
		World.EnqueueInput(TEXT("victim"), FName(TEXT("EndDialog")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Victim->Handle);
		World.Tick(0.0);

		// A refused attempt leaves no partial transaction behind.
		Victim->Disposition = TEXT("normal");
		Player->AttemptFeed(*Victim);
		TestFalse(TEXT("a refused attempt pairs nobody"), Player->IsFeedPaired());
		TestFalse(TEXT("...on either side"), Victim->IsFeedPaired());
		TestEqual(TEXT("...and fires no OnFedUponBegin"),
			SaveTestCounterValue(World.FindByName(TEXT("begincount"))), 0.0f);
	}

	// --- Toggle-style command routing ----------------------------------------------------------
	{
		FElysiumRecordingServices Services;
		FFeedCameraRecordingService Camera;
		FElysiumWorldServices Bundle = Services.Bundle();
		Bundle.Camera = &Camera;
		FElysiumEntityWorld World(nullptr, nullptr, Bundle);
		World.Load(MakeFeedTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FeedTestCharacter(World, TEXT("victim"));
		if (!TestNotNull(TEXT("toggle player exists"), Player)
			|| !TestNotNull(TEXT("toggle victim exists"), Victim))
		{
			return false;
		}
		SeedFeedSheet(*Player, /*Blood*/ 0, /*MaxHealth*/ 100, /*Damage*/ 0);
		SeedFeedSheet(*Victim, /*Blood*/ 3, /*MaxHealth*/ 100, /*Damage*/ 0);
		Victim->Disposition = TEXT("cower");

		World.QueuePlayerFeedEdge(EElysiumUseEdge::Pressed);
		World.UpdatePlayerFeed();
		TestFalse(TEXT("a target miss leaves the player unpaired"), Player->IsFeedPaired());

		Services.FeedTarget = Victim->Handle;

		World.QueuePlayerFeedEdge(EElysiumUseEdge::Pressed);
		World.UpdatePlayerFeed();
		TestTrue(TEXT("the first feed press starts the pair"), Player->IsFeedPaired());
		TestTrue(TEXT("the new pair owns its continuation latch"), Player->FeedState.bContinuation);
		TestEqual(TEXT("accepted feeding acquires one semantic camera request"), Camera.AcquireCount, 1);
		TestEqual(TEXT("the request is the existing Feed camera kind"),
			static_cast<int32>(Camera.LastRequest.Kind),
			static_cast<int32>(EElysiumCameraRequestKind::Feed));
		TestTrue(TEXT("the feed request keeps the HUD visible"), Camera.LastRequest.bShowHud);
		TestFalse(TEXT("the feed request hides the first-person viewmodel"),
			Camera.LastRequest.bDrawViewmodel);
		TestTrue(TEXT("the feed request shows the paired player body"),
			Camera.LastRequest.bShowPlayerBody);

		// The patch's vm_feed producer emits this edge 0.1 seconds after the press. It releases the
		// command button only; the paired action must keep running.
		World.QueuePlayerFeedEdge(EElysiumUseEdge::Released);
		World.UpdatePlayerFeed();
		TestTrue(TEXT("the first button release leaves feeding active"), Player->IsFeedPaired());
		TestTrue(TEXT("release does not clear paired continuation"), Player->FeedState.bContinuation);

		auto Advance = [&World](double To)
		{
			World.RunPlayerThink(To);
			World.Tick(To);
		};
		Advance(0.6);
		Advance(1.2);
		Advance(1.5);
		TestEqual(TEXT("the tap survives long enough to transfer blood"),
			Player->FeedState.BloodStolen, 1);
		TestEqual(TEXT("the toggle feed drained its victim"), Victim->BloodPoolValue(), 2);

		World.QueuePlayerFeedEdge(EElysiumUseEdge::Pressed);
		World.UpdatePlayerFeed();
		TestTrue(TEXT("the second press requests release through the latch"),
			!Player->FeedState.bContinuation);
		// The request is latched until the state machine's next scheduled boundary; input does not
		// perform immediate teardown or bypass the release animation family.
		Advance(static_cast<double>(Player->NextThink) + 0.01);
		TestEqual(TEXT("the paired state enters its normal release family"),
			static_cast<int32>(Player->FeedState.Phase),
			static_cast<int32>(EElysiumFeedPhase::Release));

		World.QueuePlayerFeedEdge(EElysiumUseEdge::Released);
		World.UpdatePlayerFeed();
		TestTrue(TEXT("the second button release is inert"), Player->IsFeedPaired());
		const float ReleaseEventDeadline = Player->FeedState.PhaseDeadline;
		Advance(static_cast<double>(Player->FeedState.PhaseDeadline) + 0.01);
		TestTrue(TEXT("event 4006 leaves the release pose paired"), Player->IsFeedPaired());
		TestFalse(TEXT("event 4006 closes the blood transaction"), Player->FeedState.IsTransacting());
		TestEqual(TEXT("event 4006 enters the presentation-only release tail"),
			static_cast<int32>(Player->FeedState.Phase),
			static_cast<int32>(EElysiumFeedPhase::ReleaseTail));
		TestEqual(TEXT("toggle teardown fires OnFedUponEnd once"),
			SaveTestCounterValue(World.FindByName(TEXT("endcount"))), 1.0f);
		TestEqual(TEXT("event 4006 releases the camera before the pose tail"),
			Camera.ReleaseCount, 1);
		TestEqual(TEXT("the release tail stays anchored to the authored event time"),
			Player->FeedState.PhaseDeadline,
			ReleaseEventDeadline + ElysiumFeed::ShortVictimReleaseSeconds
				* (1.0f - ElysiumFeed::ShortVictimReleaseEventCycle), 1e-4f);
		Advance(static_cast<double>(Player->FeedState.PhaseDeadline) + 0.01);
		TestFalse(TEXT("the authored release tail finally hands both bodies back"),
			Player->IsFeedPaired());
		TestEqual(TEXT("body-tail completion does not release the camera twice"),
			Camera.ReleaseCount, 1);
		TestTrue(TEXT("a surviving victim is re-armed for its ordinary NPC think"),
			Victim->NextThink != ELYSIUM_NEVER_THINK);
	}

	// --- FeedBegin's field seeding, and one pulse per update ------------------------------------
	{
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		Services.bProvideNpcMotor = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		FElysiumEntityDefs Defs = MakeFeedTestDefs();
		Defs.Defs[0].Keys.Add(TEXT("model"), TEXT("models/test/feed_victim.mdl"));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FeedTestCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedFeedSheet(*Player, /*Blood*/ 0, /*MaxHealth*/ 100, /*Damage*/ 45);
		SeedFeedSheet(*Victim, /*Blood*/ 3, /*MaxHealth*/ 100, /*Damage*/ 0);
		Victim->Disposition = TEXT("cower");
		Victim->OnFeedAnimEvent(ElysiumFeed::EventFeedEmitter);
		Victim->OnFeedAnimEvent(ElysiumFeed::EventFeedEmitter);
		TestEqual(TEXT("each event 5116 occurrence asks for a fresh mouth burst"),
			Services.Count(TEXT("PlayAttachedEffect force_feeding_emitter mouth")), 2);
		const FVector PlayerOriginBefore = Player->Origin;
		const FVector VictimOriginBefore = Victim->Origin;

		TestTrue(TEXT("the feed is accepted"),
			ElysiumFeedAccepted(Player->AttemptFeed(*Victim)));
		TestTrue(TEXT("both halves are paired"),
			Player->IsFeedPaired() && Victim->IsFeedPaired());
		TestFalse(TEXT("but the transaction is not open until the bite"),
			Player->FeedState.IsTransacting());
		TestTrue(TEXT("the victim's body is held for the duration"),
			Victim->FeedState.bFrozenByFeed);
		TestTrue(TEXT("pair alignment does not move the player"),
			Player->Origin.Equals(PlayerOriginBefore));
		TestTrue(TEXT("pair alignment does not move the victim"),
			Victim->Origin.Equals(VictimOriginBefore));
		TestEqual(TEXT("the player faces the victim"), Services.PlayerRotation.Yaw, 0.0, 1e-3);
		FElysiumRecordingNpcMotor* VictimMotor = Services.LastNpcMotor();
		if (TestNotNull(TEXT("the paired victim has a recording motor"), VictimMotor))
		{
			TestEqual(TEXT("the victim faces back toward the player"),
				FMath::Abs(VictimMotor->Yaw), 180.0f, 1e-3f);
		}

		auto Advance = [&World](double To)
		{
			World.RunPlayerThink(To);
			World.Tick(To);
		};

		// The engage clip runs first; event 4007 sits at cycle 0 of the bite that follows it.
		Advance(0.4);
		TestFalse(TEXT("no transaction part-way through the engage"),
			Player->FeedState.IsTransacting());
		const double BiteStart = static_cast<double>(Player->FeedState.PhaseDeadline) + 0.01;
		Advance(BiteStart);
		if (!TestTrue(TEXT("the bite opens the transaction (event 4007 -> FeedBegin)"),
			Player->FeedState.IsTransacting()))
		{
			return false;
		}
		TestEqual(TEXT("the cadence is seeded from the victim's blood pool at FeedBegin"),
			Player->FeedState.Interval, ElysiumFeed::InitialInterval(3), 1e-4f);
		TestEqual(TEXT("the first deadline is now + that interval"),
			Player->FeedState.NextPulse,
			static_cast<float>(BiteStart) + ElysiumFeed::InitialInterval(3),
			1e-3f);
		TestEqual(TEXT("OnFedUponBegin fired exactly once"),
			SaveTestCounterValue(World.FindByName(TEXT("begincount"))), 1.0f);
		TestEqual(TEXT("nothing is stolen before the first pulse"),
			Player->FeedState.BloodStolen, 0);

		// A single update that jumps far past several deadlines performs AT MOST ONE pulse — the
		// cadence is a scheduled deadline, never a catch-up loop.
		Advance(20.0);
		TestEqual(TEXT("one update, one pulse"), Player->FeedState.BloodStolen, 1);
		TestEqual(TEXT("...one blood point on the feeder"), Player->BloodPoolValue(), 1);
		TestEqual(TEXT("...one off the victim"), Victim->BloodPoolValue(), 2);
		// The feed-heal lands on the same pulse, from the recovered feeding ratio (10 per point).
		TestEqual(TEXT("...and the feed-heal reduced the feeder's damage"),
			Player->Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health), 35);
	}

	// --- Teardown: idempotency, and the depleted victim's death path ----------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeFeedTestDefs());
		World.SpawnPlayer();
		TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
		FElysiumOrderedIOSink* Sink = OwnedSink.Get();
		World.AddSink(MoveTemp(OwnedSink));
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FeedTestCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedFeedSheet(*Player, /*Blood*/ 0, /*MaxHealth*/ 100, /*Damage*/ 45);
		SeedFeedSheet(*Victim, /*Blood*/ 3, /*MaxHealth*/ 100, /*Damage*/ 0);
		Victim->Disposition = TEXT("cower");
		Player->AttemptFeed(*Victim);

		auto Advance = [&World](double To)
		{
			World.RunPlayerThink(To);
			World.Tick(To);
		};
		for (int32 Step = 1; Step <= 120; ++Step)
		{
			Advance(Step * 0.05);
		}

		TestFalse(TEXT("the feed ran to completion and left nobody paired"), Player->IsFeedPaired());
		TestEqual(TEXT("the victim was drained dry"), Victim->BloodPoolValue(), 0);
		TestEqual(TEXT("every point it had was stolen"), Player->FeedState.BloodStolen, 3);
		TestEqual(TEXT("OnFedUponEnd fired exactly once"),
			SaveTestCounterValue(World.FindByName(TEXT("endcount"))), 1.0f);
		// Remaining blood below one selects the death path, deferred from the pulse to teardown.
		TestEqual(TEXT("the depleted victim died, firing OnDeath"),
			SaveTestCounterValue(World.FindByName(TEXT("deathcount"))), 1.0f);
		TestTrue(TEXT("feed-end queues before the collapsed death outcome"),
			Sink->AppearsInOrder(TEXT("queue"),
				{ TEXT("feed_end_order.Add"), TEXT("death_order.Add") }));
		TestFalse(TEXT("and its body was handed back"), Victim->FeedState.bFrozenByFeed);

		// The single idempotent teardown: a second FeedInterrupt performs nothing and fires nothing.
		Player->FeedInterrupt();
		Player->FeedInterrupt();
		World.Tick(6.5);
		TestEqual(TEXT("a repeated interrupt does not fire OnFedUponEnd again"),
			SaveTestCounterValue(World.FindByName(TEXT("endcount"))), 1.0f);
	}

	// --- Damage interrupts a running feed before the damage commits -----------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeFeedTestDefs());
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);

		FElysiumPlayer* Player = World.FindPlayer();
		FElysiumCombatCharacter* Victim = FeedTestCharacter(World, TEXT("victim"));
		if (!Player || !Victim)
		{
			return false;
		}
		SeedFeedSheet(*Player, /*Blood*/ 0, /*MaxHealth*/ 100, /*Damage*/ 0);
		SeedFeedSheet(*Victim, /*Blood*/ 8, /*MaxHealth*/ 100, /*Damage*/ 0);
		Victim->Disposition = TEXT("cower");
		Player->AttemptFeed(*Victim);
		World.RunPlayerThink(1.0);
		World.Tick(1.0);
		if (!TestTrue(TEXT("the feed is running"), Player->FeedState.IsTransacting()))
		{
			return false;
		}
		// Hitting the VICTIM tears the pair down through its attacker.
		Victim->TakeDamage(5.0f);
		World.Tick(1.05);
		TestFalse(TEXT("incoming damage broke the pair"), Player->IsFeedPaired());
		TestEqual(TEXT("...and fired OnFedUponEnd once"),
			SaveTestCounterValue(World.FindByName(TEXT("endcount"))), 1.0f);
		TestEqual(TEXT("...leaving a surviving victim alive"),
			SaveTestCounterValue(World.FindByName(TEXT("deathcount"))), 0.0f);
	}

	// --- Save/restore of an in-progress feed ----------------------------------------------------
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld A(nullptr, nullptr, Services.Bundle());
		A.Load(MakeFeedTestDefs());
		A.SpawnPlayer();
		A.Activate(0.0);
		A.Tick(0.0);

		FElysiumPlayer* Feeder = A.FindPlayer();
		FElysiumCombatCharacter* Victim = FeedTestCharacter(A, TEXT("victim"));
		if (!TestNotNull(TEXT("the player feeder exists"), Feeder)
			|| !TestNotNull(TEXT("the victim exists"), Victim))
		{
			return false;
		}
		SeedFeedSheet(*Feeder, /*Blood*/ 0, /*MaxHealth*/ 100, /*Damage*/ 0);
		SeedFeedSheet(*Victim, /*Blood*/ 8, /*MaxHealth*/ 100, /*Damage*/ 0);
		Victim->Disposition = TEXT("cower");
		Feeder->AttemptFeed(*Victim);
		// Into the loop, with a pulse already banked and the next one scheduled. A victim carrying 8
		// blood starts on a 1.65 s interval, so 3 s is one pulse in and well short of the second.
		constexpr double FreezeTime = 3.0;
		for (int32 Step = 1; Step <= 60; ++Step)
		{
			A.RunPlayerThink(Step * 0.05);
			A.Tick(Step * 0.05);
		}
		if (!TestTrue(TEXT("the feed is mid-loop at the freeze"),
			Feeder->FeedState.IsTransacting() && Feeder->FeedState.BloodStolen > 0))
		{
			return false;
		}
		const int32 StolenBefore = Feeder->FeedState.BloodStolen;
		const float PulseBefore = Feeder->FeedState.NextPulse;
		const float IntervalBefore = Feeder->FeedState.Interval;
		const int32 VictimBloodBefore = Victim->BloodPoolValue();
		const int32 VictimIndex = Victim->Handle.Index;
		TestTrue(TEXT("an in-progress feed no longer blocks saving"),
			A.ScriptedSessionSaveBlockReason().IsEmpty());

		FElysiumPlayerRecord PlayerRecord;
		Feeder->Dehydrate(PlayerRecord);
		FElysiumMapSnapshot Snapshot;
		A.Freeze(Snapshot);

		FElysiumRecordingServices ServicesB;
		FElysiumEntityWorld B(nullptr, nullptr, ServicesB.Bundle());
		B.Load(MakeFeedTestDefs());
		B.SpawnPlayer();
		B.ApplySnapshot(Snapshot);
		FElysiumPlayer* FeederB = B.FindPlayer();
		if (FeederB)
		{
			FeederB->Hydrate(PlayerRecord);
		}
		B.Activate(FreezeTime);

		FElysiumCombatCharacter* VictimB = FeedTestCharacter(B, TEXT("victim"));
		if (!TestNotNull(TEXT("the player feeder restored"), FeederB)
			|| !TestNotNull(TEXT("the victim restored"), VictimB))
		{
			return false;
		}
		TestTrue(TEXT("the restored feed is still open"), FeederB->FeedState.IsTransacting());
		TestEqual(TEXT("the victim link survived (re-stamped against the live epoch)"),
			FeederB->FeedState.Target.Index, VictimIndex);
		TestTrue(TEXT("...and resolves in the new world"),
			B.Resolve(FeederB->FeedState.Target) != nullptr);
		TestEqual(TEXT("m_flNextFeedPulse survived"), FeederB->FeedState.NextPulse, PulseBefore, 1e-3f);
		TestEqual(TEXT("the accelerating interval survived"),
			FeederB->FeedState.Interval, IntervalBefore, 1e-3f);
		TestEqual(TEXT("the stolen counter survived"),
			FeederB->FeedState.BloodStolen, StolenBefore);
		TestTrue(TEXT("the victim came back knowing it is the victim half"),
			VictimB->FeedState.bVictim);
		TestEqual(TEXT("and the victim's blood is where the freeze left it"),
			VictimB->BloodPoolValue(), VictimBloodBefore);

		// Resuming at the frozen instant must not replay the pulse that was already banked: the
		// deadline is absolute simulation time, so a restore at that time is simply "not due yet".
		B.RunPlayerThink(FreezeTime);
		B.Tick(FreezeTime);
		TestEqual(TEXT("resuming does not duplicate a pulse"),
			FeederB->FeedState.BloodStolen, StolenBefore);
		TestEqual(TEXT("...nor re-drain the victim"), VictimB->BloodPoolValue(), VictimBloodBefore);

		// And the restored schedule still fires: running past the stored deadline banks exactly the
		// one pulse that was outstanding, on the interval the freeze had already accelerated to.
		for (double At = FreezeTime; At <= static_cast<double>(PulseBefore) + 0.11; At += 0.05)
		{
			B.RunPlayerThink(At);
			B.Tick(At);
		}
		TestEqual(TEXT("the outstanding pulse fires once, on the restored deadline"),
			FeederB->FeedState.BloodStolen, StolenBefore + 1);
	}

	TArray<FElysiumLogTap::FLine> Lines;
	FeedLogs.CollectSince(FeedLogCursor, Lines);
	auto SawDisplay = [&Lines](const TCHAR* Prefix)
	{
		return Lines.ContainsByPredicate([Prefix](const FElysiumLogTap::FLine& Line)
		{
			return Line.Verbosity.Equals(TEXT("Display"), ESearchCase::IgnoreCase)
				&& Line.Text.StartsWith(Prefix);
		});
	};
	TestTrue(TEXT("a miss is visible in the console"),
		SawDisplay(TEXT("INFO - Feed missed: no live target")));
	TestTrue(TEXT("a refusal is visible in the console"),
		SawDisplay(TEXT("INFO - Feed refused:")));
	TestTrue(TEXT("an accepted grapple is visible in the console"),
		SawDisplay(TEXT("INFO - Feed engaged:")));
	TestTrue(TEXT("transaction start is visible in the console"),
		SawDisplay(TEXT("INFO - Feed started:")));
	TestTrue(TEXT("a normal pulse reports both blood transitions"),
		SawDisplay(TEXT("INFO - Feed pulse: player=0->1, victim=3->2, stolen=1")));
	TestTrue(TEXT("the second press reports its stop request"),
		SawDisplay(TEXT("INFO - Feed stop requested")));
	TestTrue(TEXT("manual completion reports the surviving victim"),
		SawDisplay(TEXT("INFO - Feed stopped:")));
	TestTrue(TEXT("depletion reports the terminal outcome"),
		SawDisplay(TEXT("INFO - Feed ended:")));
	TestTrue(TEXT("damage teardown reports interruption"),
		SawDisplay(TEXT("INFO - Feed interrupted:")));

	return true;
}

// =====================================================================================
// The maker's child output provenance (§5.5.1): a synthesized child carries the maker's authored
// lifecycle wires and fires them itself.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFeedMakerOutputsTest,
	"Elysium.Substrate.FeedMakerOutputs", GElysiumTestFlags)
bool FElysiumFeedMakerOutputsTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__feed_maker_test__");

	FElysiumEntityDef Maker;
	Maker.Classname = TEXT("npc_maker");
	Maker.TargetName = TEXT("blueblood_maker");
	Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VPedestrian"));
	Maker.Keys.Add(TEXT("NPCTargetname"), TEXT("blueblood"));
	Maker.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
	Maker.Keys.Add(TEXT("Flag_InfChild"), TEXT("1"));
	Maker.Keys.Add(TEXT("MaxLiveChildren"), TEXT("1"));
	auto Wire = [&Maker](const TCHAR* Output, const TCHAR* Target)
	{
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = Target;
		Row.Input = TEXT("Add");
		Row.Param = TEXT("1");
		Maker.Outputs.Add(MoveTemp(Row));
	};
	Wire(TEXT("OnFedUponBegin"), TEXT("begincount"));
	Wire(TEXT("OnFedUponEnd"), TEXT("endcount"));
	Wire(TEXT("OnDeath"), TEXT("deathcount"));
	Defs.Defs.Add(MoveTemp(Maker));

	for (const TCHAR* Name : { TEXT("begincount"), TEXT("endcount"), TEXT("deathcount") })
	{
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = Name;
		Defs.Defs.Add(MoveTemp(Counter));
	}

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* MakerEnt = World.FindByName(TEXT("blueblood_maker"));
	if (!TestNotNull(TEXT("the maker exists"), MakerEnt))
	{
		return false;
	}
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Spawn")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), MakerEnt->Handle);
	World.Tick(0.0);

	FElysiumEntity* ChildEnt = World.FindByName(TEXT("blueblood"));
	if (!TestNotNull(TEXT("the child spawned"), ChildEnt))
	{
		return false;
	}
	TestEqual(TEXT("the child def carries the maker's authored output rows"),
		ChildEnt->Def->Outputs.Num(), MakerEnt->Def->Outputs.Num());
	TestEqual(TEXT("...with its own times countdown"),
		ChildEnt->OutputTimesRemaining.Num(), MakerEnt->Def->Outputs.Num());

	FElysiumPlayer* Player = World.FindPlayer();
	FElysiumCombatCharacter* Child = ChildEnt->AsCombatCharacter();
	if (!TestNotNull(TEXT("the player exists"), Player)
		|| !TestNotNull(TEXT("the child is a combat character"), Child))
	{
		return false;
	}
	SeedFeedSheet(*Player, /*Blood*/ 0, /*MaxHealth*/ 100, /*Damage*/ 0);
	SeedFeedSheet(*Child, /*Blood*/ 1, /*MaxHealth*/ 100, /*Damage*/ 0);
	Child->Disposition = TEXT("cower");
	TestTrue(TEXT("feeding on the child is accepted"),
		ElysiumFeedAccepted(Player->AttemptFeed(*Child)));

	for (int32 Step = 1; Step <= 160; ++Step)
	{
		World.RunPlayerThink(Step * 0.05);
		World.Tick(Step * 0.05);
	}

	// The child is the firing entity: the maker's wires resolve because the rows are on the child's
	// own def, which is the recovered child-output contract in `docs/vtmb/entity_io.md`.
	TestEqual(TEXT("the maker-authored OnFedUponBegin reached its target"),
		SaveTestCounterValue(World.FindByName(TEXT("begincount"))), 1.0f);
	TestEqual(TEXT("...and OnFedUponEnd did too"),
		SaveTestCounterValue(World.FindByName(TEXT("endcount"))), 1.0f);
	TestEqual(TEXT("...and the drained child's OnDeath"),
		SaveTestCounterValue(World.FindByName(TEXT("deathcount"))), 1.0f);
	TestFalse(TEXT("the maker itself is not left paired"), Player->IsFeedPaired());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlacedModelBodyClosureTest,
	"Elysium.Substrate.PlacedModelBodyClosure", GElysiumTestFlags)
bool FElysiumPlacedModelBodyClosureTest::RunTest(const FString&)
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__placed_model_body_closure__");

	FElysiumEntityDef Generic;
	Generic.Classname = TEXT("info_target");
	Generic.TargetName = TEXT("ornament");
	Generic.ModelMesh = TEXT("ornament_static");
	Generic.Keys.Add(TEXT("model"), TEXT("models/test/ornament.mdl"));
	Defs.Defs.Add(MoveTemp(Generic));

	FElysiumEntityDef Physics;
	Physics.Classname = TEXT("prop_physics");
	Physics.TargetName = TEXT("bottle");
	Physics.ModelMesh = TEXT("bottle_static");
	Physics.Keys.Add(TEXT("model"), TEXT("models/test/bottle.mdl"));
	Defs.Defs.Add(MoveTemp(Physics));

	FElysiumRecordingServices Services;
	Services.bPlacedModelsResolve = true;
	FElysiumEntityWorld World(/*Owner=*/nullptr, /*GameState=*/nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));

	TestEqual(TEXT("the generic leaf inherits one body before activation"),
		Services.Count(TEXT("BuildPlacedModelBody models/test/ornament.mdl")), 1);
	TestTrue(TEXT("the inherited body carries its stable handle token"),
		Services.Saw(TEXT("BuildPlacedModelBody models/test/ornament.mdl token=0")));
	TestEqual(TEXT("a generic fallback adds no interaction anchor"),
		Services.Count(TEXT("RegisterUseAnchor ")), 0);
	TestTrue(TEXT("physics props explicitly request a simulated proxy"),
		Services.Saw(TEXT("BuildPlacedModelBody models/test/bottle.mdl token=1 skin=0 physics=2")));
	if (FElysiumEntity* Bottle = World.FindByName(TEXT("bottle")))
	{
		TestTrue(TEXT("physics attachment authority is the static proxy"),
			Bottle->GetAttachBody() && Bottle->GetAttachBody()->IsA<UStaticMeshComponent>());
	}
	else
	{
		AddError(TEXT("physics prop did not spawn"));
	}

	Services.Calls.Reset();
	if (FElysiumEntity* Ornament = World.FindByName(TEXT("ornament")))
	{
		Ornament->SetRuntimeModel(TEXT("models/test/replacement.mdl"));
	}
	TestEqual(TEXT("SetModel rebuilds through the same composite factory"),
		Services.Count(TEXT("BuildPlacedModelBody models/test/replacement.mdl")), 1);
	TestEqual(TEXT("a generic model swap still creates no interaction semantics"),
		Services.Count(TEXT("RegisterUseAnchor ")), 0);
	return true;
}

} // namespace ElysiumFeedingTests

#endif // WITH_DEV_AUTOMATION_TESTS
