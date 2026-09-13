#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumCameraOverride.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Visual/ElysiumActionTables.h"
#include "Visual/ElysiumEyeRig.h"

// Story 29c-1, family **Closure** — the 41 layer 0–9 Troika-line slots whose verdict is `present`
// or `mechanism`, and the family that takes the band's stub count to zero.
//
// **THE POINT OF THIS SUITE IS THE DISPATCH.** Each of the 41 was a generated stub, so the slot the
// kernel dispatches through answered a TALLY rather than the port's own answer. The first case below
// walks every one of them BY NAME, with its retail address in the row, and requires that dispatching
// the slot gives back exactly what the port function it forwards to gives back — which a tally could
// not do. The cases after it are the substantive halves: the thresholds, the formulas, the arm
// order, what each refusal answers and which retail call it stands for.
//
// The brief's "two tables, and they disagree" applies here only lightly: no row in this family has a
// per-species arm, so every case stands the default combatant leaf. `npc_VCop` is spawned once, for
// the one assertion that wants a leaf whose `RetailClass()` is null, to show that these slots are
// Troika-line bodies that answer identically with or without a census class.

static constexpr EAutomationTestFlags GElysiumNpcKernelClosureFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `CAI_BaseNPCTroika::Cover_Troika` / `Reload_Troika` / `Cover_Base` activity ids, and the three
	// hint types they switch on. Repeated from the definitions on purpose: a test that read the
	// implementation's own constants would agree with it by construction.
	constexpr int32 GClosureTestActIdle = 1;
	constexpr int32 GClosureTestActCoverLow = 8;
	constexpr int32 GClosureTestActReloadFast = 85;

	struct FElysiumClosureFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Victim = nullptr;
		FElysiumNpc* Cop = nullptr;
		FElysiumPlayer* Player = nullptr;

		FElysiumClosureFixture()
			: World([]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("closure_kernel"), 29141u);
				Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
				Builder.AddNpc(TEXT("victim"), FVector(120.f, 0.f, 0.f));
				Builder.AddNpc(TEXT("cop"), FVector(900.f, 0.f, 0.f), TEXT("npc_VCop"));
				Builder.AddCounter(TEXT("fedupon"));
				Builder.WireOutput(TEXT("victim"), TEXT("OnFedUponEnd"), TEXT("fedupon"));
				return Builder;
			}())
		{
			Guard = World.Npc(TEXT("guard"));
			Victim = World.Npc(TEXT("victim"));
			Cop = World.Npc(TEXT("cop"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard, Victim, Cop });
		}
	};

	// How many times `ElysiumStub` has tallied a surface, by its recovered retail address. The
	// slot-63 case reads this before and after its dispatch rather than clearing the tally, because
	// the tally is process-global and other suites in the same run own rows in it.
	int32 ClosureTallyCountForAddress(const TCHAR* Address)
	{
		TArray<ElysiumStub::FTally> Rows;
		ElysiumStub::CollectTally(Rows);
		for (const ElysiumStub::FTally& Row : Rows)
		{
			if (Row.Address == Address)
			{
				return Row.Count;
			}
		}
		return 0;
	}

	// One row of the dispatch table: the slot, the retail body that fills it, the generated port
	// method's spelling, and the check that the dispatch reached the port's own answer.
	struct FElysiumClosureSlotRow
	{
		int32 Slot = 0;
		const TCHAR* Address = nullptr;
		const TCHAR* Method = nullptr;
		void (*Check)(FAutomationTestBase&, FElysiumClosureFixture&, const FString& Label) = nullptr;
	};
}

// =================================================================================================
// The whole family, by name and by address: the dispatch reaches the port's answer
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureDispatchTest,
	"Elysium.Substrate.NpcKernelClosure.EverySlotReachesThePortAnswer",
	GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureDispatchTest::RunTest(const FString&)
{
	static const FElysiumClosureSlotRow Rows[] =
	{
		{ 28, TEXT("0x101aa610"), TEXT("GetStealthVisionScalar"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->Senses.StealthVisionScalar = 0.375f;
				T.TestEqual(L, F.Guard->GetStealthVisionScalar(),
					F.Guard->Senses.StealthVisionScalar);
			} },
		{ 29, TEXT("0x101aa630"), TEXT("GetStealthVisionCone"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->Senses.StealthVisionCone = 0.625f;
				T.TestEqual(L, F.Guard->GetStealthVisionCone(), F.Guard->Senses.StealthVisionCone);
			} },
		{ 30, TEXT("0x101aa650"), TEXT("GetStealthHearingDist"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->Senses.StealthHearingDist = 512.f;
				T.TestEqual(L, F.Guard->GetStealthHearingDist(), F.Guard->Senses.StealthHearingDist);
			} },
		{ 48, TEXT("0x10026810"), TEXT("Slot48"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, F.Guard->Slot48(), ElysiumCameraOverride::DefaultRollDegrees);
			} },
		{ 49, TEXT("0x10026830"), TEXT("Slot49"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, F.Guard->Slot49(), ElysiumCameraOverride::DefaultFieldOfView);
			} },
		{ 63, TEXT("0x10026a10"), TEXT("SetOrigin(float, float, float)"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				// The forward is VIRTUAL, onto slot 62 (`0x100b2be0`), which is another story's row
				// and is still a generated stub — so what proves the dispatch arrived is that slot
				// 62's own tally advanced by exactly one.
				const int32 Before = ClosureTallyCountForAddress(TEXT("0x100b2be0"));
				F.Guard->SetOrigin(1.f, 2.f, 3.f);
				T.TestEqual(L, ClosureTallyCountForAddress(TEXT("0x100b2be0")), Before + 1);
			} },
		{ 79, TEXT("0x10321670"), TEXT("GetPredDescMap"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Before = F.Guard->ClosureRefusals.PredDescMap;
				T.TestNull(*L, F.Guard->GetPredDescMap());
				T.TestEqual(L + TEXT(" asked the seam"), F.Guard->ClosureRefusals.PredDescMap,
					Before + 1);
			} },
		{ 80, TEXT("0x102c5870"), TEXT("GetServerClass"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Before = F.Guard->ClosureRefusals.ServerClass;
				T.TestNull(*L, F.Guard->GetServerClass());
				T.TestEqual(L + TEXT(" asked the seam"), F.Guard->ClosureRefusals.ServerClass,
					Before + 1);
			} },
		{ 82, TEXT("0x1028cd10"), TEXT("GetDataDescMap"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Before = F.Guard->ClosureRefusals.DataDescMap;
				T.TestNull(*L, F.Guard->GetDataDescMap());
				T.TestEqual(L + TEXT(" asked the seam"), F.Guard->ClosureRefusals.DataDescMap,
					Before + 1);
			} },
		{ 88, TEXT("0x10026b50"), TEXT("Slot88"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Before = F.Guard->ClosureRefusals.ChangeTracker;
				// `0x10146700`'s own answer for a zero-initialised tracker.
				T.TestFalse(*L, F.Guard->Slot88());
				T.TestEqual(L + TEXT(" asked the seam"), F.Guard->ClosureRefusals.ChangeTracker,
					Before + 1);
			} },
		{ 102, TEXT("0x100ab450"), TEXT("Physics_TraceEntity"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				// A sentinel `trace_t` stand-in: the refusal must not write into a buffer whose
				// layout it does not know.
				uint8 Sentinel[32];
				FMemory::Memset(Sentinel, 0xAB, sizeof(Sentinel));
				F.Guard->Physics_TraceEntity(F.Victim, FVector(1, 2, 3), FVector(4, 5, 6), 0x4600u,
					Sentinel);
				bool bUntouched = true;
				for (uint8 Byte : Sentinel)
				{
					bUntouched &= (Byte == 0xAB);
				}
				T.TestTrue(*L, bUntouched);
				T.TestEqual(L + TEXT(" recorded the endpoints"),
					F.Guard->ClosureRefusals.TraceEndCm, FVector(4, 5, 6));
			} },
		{ 184, TEXT("0x10267260"), TEXT("MakeTracer"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Before = F.Guard->ClosureRefusals.MakeTracer;
				F.Guard->MakeTracer(FVector(7, 8, 9), nullptr, /*TRACER_LINE*/ 1);
				T.TestEqual(*L, F.Guard->ClosureRefusals.MakeTracer, Before + 1);
				T.TestEqual(L + TEXT(" recorded TRACER_LINE"), F.Guard->ClosureRefusals.TracerType,
					1);
			} },
		{ 192, TEXT("0x10027160"), TEXT("WorldSpaceCenter"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, F.Guard->WorldSpaceCenter(),
					ElysiumCameraShots::SurroundingBounds(*F.Guard).GetCenter());
			} },
		{ 215, TEXT("0x100b4c30"), TEXT("WorldSpaceCenter() const"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const FElysiumNpc& Const = *F.Guard;
				const FVector* Answer = static_cast<const FVector*>(Const.WorldSpaceCenter());
				if (T.TestNotNull(*L, Answer))
				{
					T.TestEqual(L + TEXT(" is slot 192's value"), *Answer,
						F.Guard->WorldSpaceCenter());
				}
			} },
		{ 225, TEXT("0x100b5040"), TEXT("VPhysicsDestroyObject"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Before = F.Guard->ClosureRefusals.VPhysicsDestroyObject;
				F.Guard->VPhysicsDestroyObject();
				T.TestEqual(*L, F.Guard->ClosureRefusals.VPhysicsDestroyObject, Before + 1);
			} },
		{ 333, TEXT("0x102bff20"), TEXT("MaintainEyeDirection"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Before = F.Guard->ClosureRefusals.BaseEyeMaintainer;
				F.Guard->MaintainEyeDirection(0.1f);
				T.TestEqual(*L, F.Guard->ClosureRefusals.BaseEyeMaintainer, Before + 1);
			} },
		{ 346, TEXT("0x1032fc50"), TEXT("SetPoseParameter(int32, float, bool)"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				// Every index misses the (empty) looping registry, so the body takes retail's own
				// fall-through to slot 260 `SetPoseParameter02` and answers whatever it answers.
				T.TestEqual(L, F.Guard->SetPoseParameter(3, 0.5f, /*bWrap*/ true),
					F.Guard->SetPoseParameter02(3, 0.5f));
			} },
		{ 355, TEXT("0x1026cf90"), TEXT("Slot355"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				// With no live transaction `CompleteFeedTransaction` performs nothing and fires
				// nothing — retail's stale-`m_GrapplePartner` arm. The paired case below is what
				// drives the teardown.
				T.TestFalse(TEXT("no feed is standing"), F.Victim->IsFeedPaired());
				F.Victim->Slot355();
				T.TestEqual(L, F.World.Counter(TEXT("fedupon")), 0.f);
			} },
		{ 362, TEXT("0x10326a20"), TEXT("FInViewCone(const FVector&)"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const FVector Ahead = F.Guard->EyePosition() + FVector(200.f, 0.f, 0.f);
				T.TestEqual(L, F.Guard->FInViewCone(Ahead),
					FElysiumNpcSenses::IsInViewCone(*F.Guard, Ahead));
			} },
		{ 363, TEXT("0x102b4540"), TEXT("FInViewCone(FElysiumEntity*)"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, F.Guard->FInViewCone(F.Victim),
					FElysiumNpcSenses::IsInViewCone(*F.Guard, *F.Victim));
				T.TestFalse(TEXT("slot 363's own null guard"), F.Guard->FInViewCone(
					static_cast<FElysiumEntity*>(nullptr)));
			} },
		{ 376, TEXT("0x10295710"), TEXT("NPC_TranslateActivity"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->NpcFlags.Clear(EElysiumNpcFlag2::D_MILDLY_CRAZY);
				T.TestEqual(L, F.Guard->NPC_TranslateActivity(1), 1);
			} },
		{ 406, TEXT("0x1027e740"), TEXT("GetStateName"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, FString(F.Guard->GetStateName(EElysiumNpcState::Combat)),
					FString(LexToString(EElysiumNpcState::Combat)));
			} },
		{ 412, TEXT("0x101aa6d0"), TEXT("GetLastUpdateThink"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->ScheduleHost.LastUpdate = 12.5;
				T.TestEqual(L, F.Guard->GetLastUpdateThink(),
					static_cast<float>(F.Guard->ScheduleHost.LastUpdate));
			} },
		{ 413, TEXT("0x101aa6f0"), TEXT("GetLastNormalThink"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->ScheduleHost.LastNormal = 13.25;
				T.TestEqual(L, F.Guard->GetLastNormalThink(),
					static_cast<float>(F.Guard->ScheduleHost.LastNormal));
			} },
		{ 414, TEXT("0x101aa710"), TEXT("GetLastMoveThink"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->ScheduleHost.LastMove = 14.125;
				T.TestEqual(L, F.Guard->GetLastMoveThink(),
					static_cast<float>(F.Guard->ScheduleHost.LastMove));
			} },
		{ 415, TEXT("0x101aa730"), TEXT("GetLastAIThink"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->ScheduleHost.LastAI = 15.0625;
				T.TestEqual(L, F.Guard->GetLastAIThink(),
					static_cast<float>(F.Guard->ScheduleHost.LastAI));
			} },
		{ 416, TEXT("0x101aa750"), TEXT("SetForceFrequentThink"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->bForceFrequentThink = false;
				F.Guard->SetForceFrequentThink(true);
				T.TestTrue(*L, F.Guard->bForceFrequentThink);
			} },
		{ 417, TEXT("0x101aa770"), TEXT("GetForceFrequentThink"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->bForceFrequentThink = true;
				T.TestEqual(L, F.Guard->GetForceFrequentThink(), F.Guard->bForceFrequentThink);
			} },
		{ 439, TEXT("0x1028abe0"), TEXT("SelectFailSchedule"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->Schedule.FailScheduleOverride = EElysiumScheduleId::None;
				T.TestEqual(L, F.Guard->SelectFailSchedule(0, 0, 0),
					ElysiumScheduleNumber(EElysiumScheduleId::Fail));
			} },
		{ 446, TEXT("0x102cc260"), TEXT("GetScheduleOfType"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				const int32 Number = ElysiumScheduleNumber(EElysiumScheduleId::IdleStand);
				T.TestEqual(L, F.Guard->GetScheduleOfType(Number),
					const_cast<void*>(static_cast<const void*>(
						ElysiumScheduleFor(EElysiumScheduleId::IdleStand))));
			} },
		{ 462, TEXT("0x101aa6b0"), TEXT("ShouldGoToIdleState"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->bGoToIdleState = true;
				T.TestEqual(L, F.Guard->ShouldGoToIdleState(), F.Guard->bGoToIdleState);
			} },
		{ 464, TEXT("0x101a6720"), TEXT("GetState"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, static_cast<int32>(F.Guard->GetState()),
					static_cast<int32>(F.Guard->GetMind().State()));
			} },
		{ 476, TEXT("0x101aa5f0"), TEXT("HearingSensitivity"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->Senses.Perception.HearingScalar = 1.75f;
				T.TestEqual(L, F.Guard->HearingSensitivity(),
					F.Guard->Senses.Perception.HearingScalar);
			} },
		{ 480, TEXT("0x10279d00"), TEXT("ShouldChooseNewEnemy"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, F.Guard->ShouldChooseNewEnemy(),
					ElysiumNpcEnemy::ShouldChooseNewEnemy(*F.Guard, F.Guard->Cognition.Conditions));
			} },
		{ 561, TEXT("0x1026dd10"), TEXT("GatherAttackConditions"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				FElysiumNpcConditions Direct;
				ElysiumNpcCond::GatherAttackConditions(*F.Guard, F.World.World.NowSeconds(),
					Direct);
				F.Guard->Cognition.Conditions.Reset();
				F.Guard->GatherAttackConditions(nullptr, 0.f);
				T.TestEqual(L, F.Guard->Cognition.Conditions.Num(), Direct.Num());
			} },
		{ 569, TEXT("0x10297560"), TEXT("GetCoverActivity"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->NpcFlags.Set(EElysiumNpcFlag::COWER_PATH);
				T.TestEqual(L, F.Guard->GetCoverActivity(nullptr), GClosureTestActCoverLow);
				F.Guard->NpcFlags.Clear(EElysiumNpcFlag::COWER_PATH);
			} },
		{ 570, TEXT("0x102954b0"), TEXT("GetReloadActivity"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				T.TestEqual(L, F.Guard->GetReloadActivity(nullptr), GClosureTestActReloadFast);
			} },
		{ 583, TEXT("0x1028d860"), TEXT("Slot583"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				// The same loop body `FElysiumEntityWorld::WakeNpcsNear` runs, over the same radius.
				F.Guard->ScheduleHost.NextAI = 999.0;
				F.Guard->Slot583(F.Guard->Origin);
				const double Armed = F.Guard->ScheduleHost.NextAI;
				F.Guard->ScheduleHost.NextAI = 999.0;
				F.World.World.WakeNpcsNear(F.Guard->Origin);
				T.TestEqual(L, F.Guard->ScheduleHost.NextAI, Armed);
			} },
		{ 584, TEXT("0x1028d910"), TEXT("Slot584"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->ScheduleHost.LastAI = -1.0;
				F.Guard->Slot584(0);
				T.TestEqual(L, F.Guard->ScheduleHost.LastAI, F.World.World.NowSeconds());
			} },
		{ 586, TEXT("0x101aa5d0"), TEXT("GetBestSeeUnknown"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->Senses.Memory.BestSeeUnknown = F.Victim->Handle;
				T.TestTrue(*L, F.Guard->GetBestSeeUnknown() == F.Guard->Senses.Memory.BestSeeUnknown);
			} },
		{ 593, TEXT("0x1029a070"), TEXT("Slot593"),
			[](FAutomationTestBase& T, FElysiumClosureFixture& F, const FString& L)
			{
				F.Guard->TargetLeadMin = -1.f;
				F.Guard->Slot593();
				T.TestEqual(L, F.Guard->TargetLeadMin, 0.1f);
			} },
	};

	static_assert(UE_ARRAY_COUNT(Rows) == 41, "family Closure carries 41 slots");

	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard)
		|| !TestNotNull(TEXT("the victim spawned"), F.Victim)
		|| !TestNotNull(TEXT("npc_VCop is a registered spawn leaf"), F.Cop))
	{
		return false;
	}

	// The brief's first table trap, asserted rather than worked around: no census class claims
	// `npc_VCop`, so a cop's `RetailClass()` is null and every per-species lookup falls to the
	// Troika line. None of this family's 41 rows has a species arm, so a cop answers exactly what
	// the combatant leaf answers — which is what makes standing one case per row legitimate.
	TestNull(TEXT("npc_VCop's RetailClass() is null"), F.Cop->RetailClass());
	TestEqual(TEXT("a cop's slot 49 is the Troika line's"), F.Cop->Slot49(), F.Guard->Slot49());
	TestEqual(TEXT("a cop's slot 570 is the Troika line's"), F.Cop->GetReloadActivity(nullptr),
		F.Guard->GetReloadActivity(nullptr));

	for (const FElysiumClosureSlotRow& Row : Rows)
	{
		const FString Label = FString::Printf(TEXT("slot %d %s -> FElysiumNpc::%s"), Row.Slot,
			Row.Address, Row.Method);
		Row.Check(*this, F, Label);
	}

	// And the census agrees that all 41 are Troika-line slots of the band this story owns: every one
	// of them is a row of `ElysiumNpcKernelShape.cpp` at the address above.
	TArrayView<const FElysiumNpcSlot> Census = ElysiumNpcKernelShape::Slots();
	for (const FElysiumClosureSlotRow& Row : Rows)
	{
		const FElysiumNpcSlot* Found = Census.FindByPredicate(
			[&Row](const FElysiumNpcSlot& Slot)
			{
				return Slot.Slot == Row.Slot && Slot.Address != nullptr
					&& FCString::Stricmp(Slot.Address, Row.Address) == 0;
			});
		TestNotNull(*FString::Printf(TEXT("the census carries slot %d at %s"), Row.Slot,
			Row.Address), Found);
	}
	return true;
}

// =================================================================================================
// Slots 192 and 215 — `WorldSpaceCenter`, `0x10027160` and `0x100b4c30`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureWorldSpaceCenterTest,
	"Elysium.Substrate.NpcKernelClosure.WorldSpaceCenter", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureWorldSpaceCenterTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}

	// `0x10027160` and `0x100b4c30` are the SAME retail body compiled twice — identical arms,
	// identical arithmetic, different return ABI. So the two slots must answer the same vector, and
	// both must be the port's one bounds accessor.
	const FVector Expected = ElysiumCameraShots::SurroundingBounds(*F.Guard).GetCenter();
	TestEqual(TEXT("slot 192 is ElysiumCameraShots::SurroundingBounds().GetCenter()"),
		F.Guard->WorldSpaceCenter(), Expected);

	const FElysiumNpc& Const = *F.Guard;
	const FVector* Held = static_cast<const FVector*>(Const.WorldSpaceCenter());
	if (!TestNotNull(TEXT("slot 215 hands out an address"), Held))
	{
		return false;
	}
	TestEqual(TEXT("slot 215 holds slot 192's value"), *Held, Expected);

	// The named modernization, asserted: the cache is PER ENTITY and one deep, so the address is
	// stable across calls on the same body and is a DIFFERENT address on another body. Retail's ring
	// gives neither guarantee — it advances twice per call and wraps after 128.
	TestEqual(TEXT("the same body answers the same address twice"),
		static_cast<const void*>(Const.WorldSpaceCenter()), static_cast<const void*>(Held));
	const FElysiumNpc& OtherConst = *F.Victim;
	TestNotEqual(TEXT("a different body answers a different address"),
		static_cast<const void*>(OtherConst.WorldSpaceCenter()), static_cast<const void*>(Held));

	// The value moves with the body, because `SurroundingBounds`' bodiless stand-in is VtMB's own
	// standing hull placed on the entity's origin.
	const FVector Before = F.Guard->WorldSpaceCenter();
	F.Guard->SetRuntimeOrigin(F.Guard->Origin + FVector(0.f, 250.f, 0.f));
	TestEqual(TEXT("the centre follows the origin"), F.Guard->WorldSpaceCenter(),
		Before + FVector(0.f, 250.f, 0.f));

	// Family Geometry's `CNPC_Crow` override falls through to this body for every other class, so
	// the two must agree on a combatant.
	TestTrue(TEXT("SpeciesWorldSpaceCenter falls through to slot 192"),
		F.Guard->SpeciesWorldSpaceCenter().Equals(F.Guard->WorldSpaceCenter(), 0.001));
	return true;
}

// =================================================================================================
// Slots 79, 80, 82, 88, 102, 184, 225 — the seven refusals
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureRefusalTest,
	"Elysium.Substrate.NpcKernelClosure.Refusals", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureRefusalTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// Slots 79 / 80 / 82 — the datamap and server-class descriptors. There is no `datamap_t` and no
	// `ServerClass` in this substrate: the save surface is `FElysiumSaveArchive`, a per-type
	// `Serialize`, and there is no client to replicate to. Answering a fabricated pointer would be
	// worse than answering none, because the one legitimate consumer of a datamap pointer walks it.
	TestNull(TEXT("slot 79 `0x10321670` GetPredDescMap refuses"), N.GetPredDescMap());
	TestNull(TEXT("slot 80 `0x102c5870` GetServerClass refuses"), N.GetServerClass());
	TestNull(TEXT("slot 82 `0x1028cd10` GetDataDescMap refuses"), N.GetDataDescMap());
	TestEqual(TEXT("each of the three was asked once"),
		N.ClosureRefusals.PredDescMap + N.ClosureRefusals.ServerClass
			+ N.ClosureRefusals.DataDescMap, 3);

	// Slot 88 — the change tracker at `this+0x1b0`. `false` is `0x10146700`'s OWN answer for a
	// zero-initialised tracker: with the interval word `+0x4` zero the countdown arm never runs,
	// `+0x0` is clear, and the function takes its `(interval == 0)` exit and returns 0. Repeated
	// dispatches keep answering it, because nothing here ever dirties the tracker.
	TestFalse(TEXT("slot 88 `0x10026b50` answers no change pending"), N.Slot88());
	TestFalse(TEXT("...and again"), N.Slot88());
	TestEqual(TEXT("both dispatches reached the seam"), N.ClosureRefusals.ChangeTracker, 2);

	// Slot 102 — `Physics_TraceEntity`. The refusal is forced by the OUT parameter: retail's fifth
	// argument is a `trace_t*` this substrate stands no counterpart for, so the generator typed it
	// `void*` and there is nowhere to put a fraction, an endpos or a hit entity. The buffer comes
	// back exactly as it went in.
	uint8 Trace[48];
	FMemory::Memset(Trace, 0x5C, sizeof(Trace));
	N.Physics_TraceEntity(F.Victim, FVector(10, 20, 30), FVector(40, 50, 60), 0x4600u, Trace);
	bool bUntouched = true;
	for (uint8 Byte : Trace)
	{
		bUntouched &= (Byte == 0x5C);
	}
	TestTrue(TEXT("slot 102 `0x100ab450` writes nothing into the trace buffer"), bUntouched);
	TestEqual(TEXT("...and it was asked once"), N.ClosureRefusals.PhysicsTraceEntity, 1);
	TestEqual(TEXT("...with retail's own start"), N.ClosureRefusals.TraceStartCm,
		FVector(10, 20, 30));
	TestEqual(TEXT("...its own end"), N.ClosureRefusals.TraceEndCm, FVector(40, 50, 60));
	TestEqual(TEXT("...and its own content mask"), static_cast<int32>(N.ClosureRefusals.TraceMask),
		0x4600);

	// Slot 184 — `MakeTracer`. A pure visual effect (a `CPASFilter` plus, for `TRACER_LINE` only,
	// the bullet-tracer temp entity), refused for the same two reasons: the endpos it would draw to
	// lives in the same unreproduced `trace_t`, and the PAS broadcast has no counterpart here.
	N.MakeTracer(FVector(1, 1, 1), nullptr, /*TRACER_LINE*/ 1);
	N.MakeTracer(FVector(2, 2, 2), nullptr, /*some other tracer type*/ 4);
	TestEqual(TEXT("slot 184 `0x10267260` was asked twice"), N.ClosureRefusals.MakeTracer, 2);
	TestEqual(TEXT("...the last type recorded"), N.ClosureRefusals.TracerType, 4);
	TestEqual(TEXT("...the last start recorded"), N.ClosureRefusals.TracerStartCm,
		FVector(2, 2, 2));

	// Slot 225 — `VPhysicsDestroyObject`. Retail's whole body is guarded on `m_pPhysicsObject`
	// (`+0x36c`); with the pointer null it is `return;`, which is what an NPC that never got a
	// physics object does in retail too. No port member stands `+0x36c`.
	N.VPhysicsDestroyObject();
	TestEqual(TEXT("slot 225 `0x100b5040` was asked once"), N.ClosureRefusals.VPhysicsDestroyObject,
		1);
	return true;
}

// =================================================================================================
// Slot 63 — the `SetOrigin` forwarding thunk, `0x10026a10`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureSetOriginThunkTest,
	"Elysium.Substrate.NpcKernelClosure.SetOriginThunk", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureSetOriginThunkTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	// `0x10026a10` builds a `Vector` on the stack and dispatches `vtable +0xf8` — slot 62,
	// `SetOrigin(const Vector&)`. The forward is VIRTUAL in retail and virtual here, which matters:
	// a species that overrides slot 62 has to be reached through slot 63 as well. Slot 62
	// (`0x100b2be0`) is another story's row and is still a generated stub, so its tally is the proof
	// that the dispatch arrived — three calls, three tallies, one per forward.
	const int32 Before = ClosureTallyCountForAddress(TEXT("0x100b2be0"));
	F.Guard->SetOrigin(1.f, 2.f, 3.f);
	F.Guard->SetOrigin(4.f, 5.f, 6.f);
	F.Guard->SetOrigin(7.f, 8.f, 9.f);
	TestEqual(TEXT("slot 63 forwards to slot 62, once per call"),
		ClosureTallyCountForAddress(TEXT("0x100b2be0")), Before + 3);
	return true;
}

// =================================================================================================
// Slots 569 and 570 — the cover and reload activity delegates, `0x10297560`, `0x102954b0`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureCoverReloadTest,
	"Elysium.Substrate.NpcKernelClosure.CoverAndReloadActivity", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureCoverReloadTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// --- Slot 569 `Cover_Troika` (`0x10297560`) --------------------------------------------------
	//
	// Retail's first line is UNCONDITIONAL: `if (m_bfAINPCFlags & 0x200) return ACT_COVER_LOW;` —
	// no `SelectWeightedSequence` probe, no cover context, no fall-through.
	//
	// **The port's committed table disagrees with retail here and the slot follows retail.**
	// `GCoverTroikaRules`' first row is `{ ForcedLowCover, 101, ForceCoverContext }`, which sets the
	// cover CONTEXT to 101 and lets the following rows run — so the table answers `ACT_CRUNCH_IDLE`
	// for a body that authors it and `ACT_IDLE` for one that does not. That is a different answer
	// for a set flag, so forwarding would have wired the slot to something retail does not do. The
	// divergence is REPORTED, not edited: `Visual/ElysiumNpcActivityTables.cpp` is another layer's
	// file.
	N.NpcFlags.Set(EElysiumNpcFlag::COWER_PATH);
	TestEqual(TEXT("COWER_PATH short-circuits slot 569 to ACT_COVER_LOW, unconditionally"),
		N.GetCoverActivity(nullptr), GClosureTestActCoverLow);
	// Not even a hint changes it: retail returns before it looks at `param_1`.
	N.ScheduleHost.HintNode = 7;
	TestEqual(TEXT("...ahead of the hint arm"), N.GetCoverActivity(&N), GClosureTestActCoverLow);
	N.ScheduleHost.HintNode = INDEX_NONE;
	N.NpcFlags.Clear(EElysiumNpcFlag::COWER_PATH);

	// Without the flag and without a hint, retail falls straight through the three crunch-idle arms
	// into `Cover_Base` (`0x10274aa0`), whose own three `RewriteIfAvailable` rows all need a
	// sequence the model authors. The port's probe — `SelectWeightedSequenceForActivity`, family
	// Facing's seam — answers -1 for everything, so every arm misses and the body lands on
	// `Cover_Base`'s unconditional `ACT_IDLE` tail. **That is retail's own answer for a body with no
	// cover clips**, not a placeholder; when the seam grows a sequence table the arms above it start
	// firing without another edit here.
	TestEqual(TEXT("the availability seam answers -1, as family Facing states"),
		N.SelectWeightedSequenceForActivity(/*ACT_COVER_MED*/ 7), INDEX_NONE);
	TestEqual(TEXT("so slot 569 lands on Cover_Base's ACT_IDLE tail"), N.GetCoverActivity(nullptr),
		GClosureTestActIdle);

	// --- Slot 570 `Reload_Troika` (`0x102954b0`) -------------------------------------------------
	//
	// There is no `Reload_Base` fall-through in the Troika body and no `ACT_RELOAD` (84) anywhere in
	// it: every miss answers `ACT_RELOAD_FAST` (0x55). The base body (`0x10274820`, 13 classes)
	// answers `ACT_RELOAD`, and this slot is NOT that body.
	TestEqual(TEXT("slot 570 answers ACT_RELOAD_FAST on every miss"), N.GetReloadActivity(nullptr),
		GClosureTestActReloadFast);
	TestEqual(TEXT("...and never ACT_RELOAD, which is the base body's tail"),
		N.GetReloadActivity(nullptr) == 84, false);
	// `COWER_PATH` is slot 569's bit alone; the reload delegate never reads the flag word.
	N.NpcFlags.Set(EElysiumNpcFlag::COWER_PATH);
	TestEqual(TEXT("slot 570 does not read COWER_PATH"), N.GetReloadActivity(nullptr),
		GClosureTestActReloadFast);
	N.NpcFlags.Clear(EElysiumNpcFlag::COWER_PATH);

	// The hint argument. THERE IS NO HINT NODE IN THIS SUBSTRATE, so family Hints' seam answers
	// false and the hint type is `INDEX_NONE` whichever pointer arrives — which takes retail's
	// `param_1 == 0` arm. Asserted rather than worked around; every retail call site passes
	// `m_pHintNode`, which is `ScheduleHost.HintNode` here.
	FElysiumNpc::FHintWords Words;
	N.ScheduleHost.HintNode = 3;
	TestFalse(TEXT("the hint store seam refuses"), N.HintWords(N.ScheduleHost.HintNode, Words));
	TestEqual(TEXT("so a non-null hint still takes the no-hint arm"), N.GetReloadActivity(&N),
		GClosureTestActReloadFast);
	N.ScheduleHost.HintNode = INDEX_NONE;
	return true;
}

// =================================================================================================
// Slot 376 — `NPC_TranslateActivity`, `0x10295710`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureTranslateActivityTest,
	"Elysium.Substrate.NpcKernelClosure.TranslateActivity", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureTranslateActivityTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// The rule, from the decompiled C: `ACT_IDLE` (1) becomes `ACT_LAUGH_IDLE` (0x105d = 4189) when
	// `m_bfAINPCFlags2 & 0x80000`, and everything else is identity.
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	TestEqual(TEXT("without the flag ACT_IDLE is identity"), N.NPC_TranslateActivity(1), 1);
	N.NpcFlags.Set(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	TestEqual(TEXT("0x80000 rewrites ACT_IDLE to ACT_LAUGH_IDLE"), N.NPC_TranslateActivity(1), 4189);
	TestEqual(TEXT("...and nothing else"), N.NPC_TranslateActivity(2), 2);
	TestEqual(TEXT("...including ACT_LAUGH_IDLE itself"), N.NPC_TranslateActivity(4189), 4189);
	TestEqual(TEXT("...and ACT_COVER"), N.NPC_TranslateActivity(6), 6);

	// The two ids are the port's own `ClassTranslate_Troika` row, read off the committed table
	// rather than spelled twice, so a regeneration that moved either one moves the slot with it.
	// This is the join, asserted by name.
	const ElysiumActionTables::FNpcTranslationBody* Troika = nullptr;
	for (const ElysiumActionTables::FNpcTranslationBody& Body :
		ElysiumActionTables::NpcTranslationBodies())
	{
		if (Body.Name != nullptr
			&& FCString::Stricmp(Body.Name, TEXT("ClassTranslate_Troika")) == 0)
		{
			Troika = &Body;
		}
	}
	if (!TestNotNull(TEXT("the committed table carries ClassTranslate_Troika"), Troika))
	{
		return false;
	}
	TestEqual(TEXT("...at this retail address"), FString(Troika->Address), FString(TEXT("0x10295710")));
	if (!TestEqual(TEXT("...with exactly one row"), Troika->RuleCount, 1))
	{
		return false;
	}
	TestEqual(TEXT("...whose FromId is the slot's input"), Troika->Rules[0].FromId, 1);
	TestEqual(TEXT("...and whose ToId is the slot's answer"), Troika->Rules[0].ToId,
		N.NPC_TranslateActivity(1));
	N.NpcFlags.Clear(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	return true;
}

// =================================================================================================
// Slots 583 and 584 — the think-stamp resets, `0x1028d860` and `0x1028d910`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureThinkResetTest,
	"Elysium.Substrate.NpcKernelClosure.ThinkResets", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureThinkResetTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.World.World.NowSeconds();
	const double RadiusCm = 2048.0 * ElysiumMove::U;

	// --- Slot 583 `0x1028d860` -------------------------------------------------------------------
	//
	// `dist²(point, GetAbsOrigin()) <= _DAT_1049adfc` (4194304.0 = 2048²) -> slot 614
	// `ResetThinkTimers`. The decompiler spells the comparison `(d2 < c) != (d2 == c)`, which is
	// `<=`: a body EXACTLY on the radius is woken. Both boundary sides are asserted.
	auto Sleep = [&N]() { N.ScheduleHost.NextAI = 999.0; };
	auto Woken = [&N]() { return N.ScheduleHost.NextAI != 999.0; };

	Sleep();
	N.Slot583(N.Origin + FVector(RadiusCm - 1.0, 0.0, 0.0));
	TestTrue(TEXT("a point inside 2048 units wakes the body"), Woken());

	Sleep();
	N.Slot583(N.Origin + FVector(RadiusCm, 0.0, 0.0));
	TestTrue(TEXT("a point exactly on 2048 units wakes it too -- the compare is <="), Woken());

	Sleep();
	N.Slot583(N.Origin + FVector(RadiusCm + 1.0, 0.0, 0.0));
	TestFalse(TEXT("a point outside it does not"), Woken());

	// The radius is a SPHERE, not a cylinder: the body's Z delta counts.
	Sleep();
	N.Slot583(N.Origin + FVector(RadiusCm * 0.8, 0.0, RadiusCm * 0.8));
	TestFalse(TEXT("the test is a squared distance in three axes"), Woken());

	// And the slot IS the loop body of `FElysiumEntityWorld::WakeNpcsNear`, 29c's named target,
	// which carries this test inline over the same radius.
	Sleep();
	N.Slot583(N.Origin);
	const double ArmedBySlot = N.ScheduleHost.NextAI;
	Sleep();
	F.World.World.WakeNpcsNear(N.Origin);
	TestEqual(TEXT("slot 583 arms what WakeNpcsNear arms"), N.ScheduleHost.NextAI, ArmedBySlot);
	Sleep();
	F.World.World.WakeNpcsNear(N.Origin + FVector(RadiusCm + 1.0, 0.0, 0.0));
	TestFalse(TEXT("...and refuses what it refuses"), Woken());

	// --- Slot 584 `0x1028d910` -------------------------------------------------------------------
	//
	// Slot 614 first, then every `Last` mirror to curtime. -> `FElysiumNpc::ResetAllThinkStamps`,
	// which `ElysiumNpc.h` already names this slot and this address for.
	//
	// `m_flLastThink` (`+0x0178`) is the fifth word retail writes and is the ONE with no port
	// member: a `CBaseEntity` word below 29b's band, with no row in `ElysiumNpcKernelShapeMap.cpp`
	// and no reader in layers 0–9. Stated rather than invented.
	N.ScheduleHost.LastUpdate = N.ScheduleHost.LastNormal = -1.0;
	N.ScheduleHost.LastMove = N.ScheduleHost.LastAI = -1.0;
	N.ScheduleHost.NextUpdate = N.ScheduleHost.NextAI = 999.0;
	N.Slot584(/*the argument reaches no instruction*/ 12345);
	TestEqual(TEXT("slot 584 sets LastUpdate to now"), N.ScheduleHost.LastUpdate, Now);
	TestEqual(TEXT("...LastNormal"), N.ScheduleHost.LastNormal, Now);
	TestEqual(TEXT("...LastMove"), N.ScheduleHost.LastMove, Now);
	TestEqual(TEXT("...LastAI"), N.ScheduleHost.LastAI, Now);
	TestEqual(TEXT("...and slot 614 ran first, so NextAI is now too"), N.ScheduleHost.NextAI, Now);

	// The difference between the two slots, which is the whole reason both exist: 583 runs slot 614
	// ONLY and leaves the `Last` mirrors alone (retail's comment in `ElysiumNpc.h`: the interval the
	// next `Calc*` reports is measured from the stamp the reset overwrote).
	N.ScheduleHost.LastAI = -7.0;
	N.ScheduleHost.NextAI = 999.0;
	N.Slot583(N.Origin);
	TestEqual(TEXT("slot 583 arms the Next stamp"), N.ScheduleHost.NextAI, Now);
	TestEqual(TEXT("...and deliberately leaves the Last mirror alone"), N.ScheduleHost.LastAI, -7.0);
	return true;
}

// =================================================================================================
// Slot 593 — the target-lead defaults, `0x1029a070`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureTargetLeadTest,
	"Elysium.Substrate.NpcKernelClosure.TargetLeadDefaults", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureTargetLeadTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.TargetLeadMin = N.TargetLeadMax = N.TargetLeadCurrentWeight = -1.f;
	N.TargetLeadPredictedWeight = N.TargetLeadWeightScale = -1.f;

	// Five immediate stores and nothing else. The values are transcribed from the instruction
	// stream, not from `.rdata`: 0x3dcccccd, 0x3f800000, 0x42480000, 0x42480000, 0x3c23d70a.
	N.Slot593();
	TestEqual(TEXT("+0x655c m_flTargetLeadMin = 0.1"), N.TargetLeadMin, 0.1f);
	TestEqual(TEXT("+0x6560 m_flTargetLeadMax = 1.0"), N.TargetLeadMax, 1.0f);
	TestEqual(TEXT("+0x6564 m_flTargetLeadCurrentWeight = 50"), N.TargetLeadCurrentWeight, 50.0f);
	TestEqual(TEXT("+0x6568 m_flTargetLeadPredictedWeight = 50"), N.TargetLeadPredictedWeight,
		50.0f);
	TestEqual(TEXT("+0x656c m_flTargetLeadWeightScale = 0.01"), N.TargetLeadWeightScale, 0.01f);

	// The two weights are a 50/50 split and the scale is what turns them into a fraction, which is
	// why they are 50 and 0.01 rather than 0.5: retail divides by 100 at the point of use.
	TestEqual(TEXT("the two weights are equal, a 50/50 split"), N.TargetLeadCurrentWeight,
		N.TargetLeadPredictedWeight);
	TestEqual(TEXT("and the scale takes the pair to 1.0"),
		(N.TargetLeadCurrentWeight + N.TargetLeadPredictedWeight) * N.TargetLeadWeightScale, 1.0f);

	// It is an initialiser, so it is idempotent.
	N.Slot593();
	TestEqual(TEXT("a second dispatch answers the same"), N.TargetLeadMin, 0.1f);
	return true;
}

// =================================================================================================
// Slots 439 and 446 — the two schedule lookups, `0x1028abe0` and `0x102cc260`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureScheduleTest,
	"Elysium.Substrate.NpcKernelClosure.ScheduleLookups", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureScheduleTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// --- Slot 439 `0x1028abe0` -------------------------------------------------------------------
	//
	// `return m_failSchedule ? m_failSchedule : 0x43`. `+0x5c54` is
	// `FElysiumScheduleState::FailScheduleOverride`, the same word `ElysiumSchedule.cpp`'s
	// `FailScheduleFor` reads FIRST — that function is file-local so it cannot be forwarded to, and
	// it also does more than this slot (the program's declared route and `TranslateSchedule`, which
	// in retail are the CALLER's work at `0x10281730`).
	//
	// The recovered literal is 0x43, and the port's `SCHED_FAIL` number is the same; asserted rather
	// than assumed.
	TestEqual(TEXT("SCHED_FAIL is retail's 0x43"),
		ElysiumScheduleNumber(EElysiumScheduleId::Fail), 0x43);
	N.Schedule.FailScheduleOverride = EElysiumScheduleId::None;
	TestEqual(TEXT("no fail schedule set answers SCHED_FAIL"), N.SelectFailSchedule(0, 0, 0), 0x43);

	N.Schedule.FailScheduleOverride = EElysiumScheduleId::ChaseEnemyFailed;
	TestEqual(TEXT("a set fail schedule wins"), N.SelectFailSchedule(0, 0, 0),
		ElysiumScheduleNumber(EElysiumScheduleId::ChaseEnemyFailed));
	N.Schedule.FailScheduleOverride = EElysiumScheduleId::IdleDisposition;
	TestEqual(TEXT("...whichever it is"), N.SelectFailSchedule(0, 0, 0), 0x6b);

	// All three arguments reach no instruction in the retail body, on the base line or in any
	// override in the closure.
	TestEqual(TEXT("the three arguments are ignored"), N.SelectFailSchedule(7, 9, 11),
		N.SelectFailSchedule(0, 0, 0));
	N.Schedule.FailScheduleOverride = EElysiumScheduleId::None;

	// --- Slot 446 `0x102cc260` -------------------------------------------------------------------
	//
	// -> `ElysiumScheduleFor`, keyed by the retail NUMBER through `ElysiumScheduleNumber`. There is
	// no schedule id space to translate through: this runtime registers every program in one global
	// namespace, so the `< 1e9` local-id arm has no operand.
	TestEqual(TEXT("slot 446 answers the registry's SCHED_IDLE_STAND"),
		N.GetScheduleOfType(ElysiumScheduleNumber(EElysiumScheduleId::IdleStand)),
		const_cast<void*>(static_cast<const void*>(
			ElysiumScheduleFor(EElysiumScheduleId::IdleStand))));
	TestEqual(TEXT("...and its SCHED_FAIL"), N.GetScheduleOfType(0x43),
		const_cast<void*>(static_cast<const void*>(
			ElysiumScheduleFor(EElysiumScheduleId::Fail))));

	// **The miss answers null, not `IDLE_STAND`.** Retail's miss Warnings and returns schedule 1;
	// the port's equivalent of that whole arm is `ElysiumSchedule::Start`, which records the miss
	// and installs `IDLE_STAND` — because the substitution is `SetSchedule`'s decision, not the
	// lookup's. A lookup that substituted would make an unported program indistinguishable from an
	// idle one.
	TestNull(TEXT("a number this runtime carries no program for answers null"),
		N.GetScheduleOfType(0x7fff));
	// Number 0 is the ledger's "registration site not decoded" marker on five ids and must never
	// resolve, or every undecoded number would land on whichever of them came first.
	TestNull(TEXT("number 0 never resolves"), N.GetScheduleOfType(0));
	TestNull(TEXT("nor -1, retail's own sentinel"), N.GetScheduleOfType(-1));
	return true;
}

// =================================================================================================
// Slot 333 — `MaintainEyeDirection`, `0x102bff20`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureEyeDirectionTest,
	"Elysium.Substrate.NpcKernelClosure.MaintainEyeDirection", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureEyeDirectionTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const float Now = static_cast<float>(F.World.World.NowSeconds());

	// Arm 2 — the ONE write of the Troika wrapper, and the one this tier can perform:
	// `if (m_hDialogPartner resolves live) m_flNextEyeLookTime = curtime + 2.0` (`+0x5d6c`,
	// `_DAT_10452dc4` = 2.0f). It is what makes the gaze cascade's fall-through terminal for the
	// length of a conversation.
	N.NextEyeLookTime = -1.f;
	TestFalse(TEXT("no dialogue partner is live"), N.HasLiveDialogPartner());
	N.MaintainEyeDirection(0.1f);
	TestEqual(TEXT("with no partner the re-scan stamp is untouched"), N.NextEyeLookTime, -1.f);

	N.Dialogue.bInDialog = true;
	TestTrue(TEXT("the dialogue seam now resolves a partner"), N.HasLiveDialogPartner());
	N.MaintainEyeDirection(0.1f);
	TestEqual(TEXT("a live partner pushes the re-scan stamp to curtime + 2.0"), N.NextEyeLookTime,
		Now + 2.0f);
	// Every think, not once: retail rewrites it unconditionally while the partner stands.
	N.NextEyeLookTime = 0.f;
	N.MaintainEyeDirection(0.1f);
	TestEqual(TEXT("...on every think, not once"), N.NextEyeLookTime, Now + 2.0f);
	N.Dialogue.bInDialog = false;

	// Arm 1's GATE is reachable here even though its countdown is not: retail wraps the whole blink
	// cadence in `m_flPlayerDist < _DAT_10483aac` (`+0x6264`, the port's
	// `FElysiumNpcMemory::ClosestPlayerDistanceCm`), and a body far from the player neither blinks
	// nor runs its timer down — which is why walking up to a distant NPC fires no backlog. The
	// countdown itself is `FElysiumBlinkSchedule`, an `ELYSIUM_NPC_WORD_CHAIN` onto the eye pass,
	// so the refusal is recorded exactly when retail would have looked at the timer.
	const int32 BlinkBefore = N.ClosureRefusals.BlinkCadence;
	N.Senses.Memory.ClosestPlayerDistanceCm = ElysiumEyes::BlinkPlayerDistance + 100.f;
	N.MaintainEyeDirection(0.1f);
	TestEqual(TEXT("a distant body never reaches the blink cadence"),
		N.ClosureRefusals.BlinkCadence, BlinkBefore);
	N.Senses.Memory.ClosestPlayerDistanceCm = ElysiumEyes::BlinkPlayerDistance - 100.f;
	N.MaintainEyeDirection(0.1f);
	TestEqual(TEXT("a near one does"), N.ClosureRefusals.BlinkCadence, BlinkBefore + 1);

	// Arms 3 and 4 are named seams, asked on every dispatch: the disposition fidget driver
	// (`0x102c0010` -> `FElysiumCombatCharacter::FidgetStep` / `NextFidgetTime`) and the base
	// maintainer (`0x1026b810` -> `FElysiumCombatCharacter::TickGaze`). Both need the head frame and
	// the disposition's eye tuning, which only `AElysiumMapActor::TickGaze` can supply, and it
	// already drives them once per frame per drawn body.
	const int32 TailBefore = N.ClosureRefusals.BaseEyeMaintainer;
	N.MaintainEyeDirection(0.1f);
	TestEqual(TEXT("the fidget driver is asked"), N.ClosureRefusals.EyeFidgetDriver,
		N.ClosureRefusals.BaseEyeMaintainer);
	TestEqual(TEXT("and so is the base maintainer"), N.ClosureRefusals.BaseEyeMaintainer,
		TailBefore + 1);
	return true;
}

// =================================================================================================
// Slot 355 — the feed-end output, `0x1026cf90`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureFeedEndTest,
	"Elysium.Substrate.NpcKernelClosure.FeedEnd", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureFeedEndTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the victim spawned"), F.Victim)
		|| !TestNotNull(TEXT("the player spawned"), F.Player))
	{
		return false;
	}

	// With no transaction standing the body performs nothing and fires nothing — the port's
	// `IsPaired()` guard, and retail's stale-`m_GrapplePartner` arm, which resolves a null activator
	// and reaches no wire.
	TestFalse(TEXT("no feed is standing"), F.Victim->IsFeedPaired());
	F.Victim->Slot355();
	TestEqual(TEXT("slot 355 on an unpaired body fires no OnFedUponEnd"),
		F.World.Counter(TEXT("fedupon")), 0.f);

	// With one standing it tears it down, which is `CompleteFeedTransaction(false)` — 29c's named
	// target, and the port's ONE producer of `OnFedUponEnd`. `cower` is the recovered auto-accept
	// disposition, so the grapple opens with no opposed roll.
	F.Victim->Disposition = TEXT("cower");
	F.Player->AttemptFeed(*F.Victim);
	if (!TestTrue(TEXT("the feed pair opened"), F.Player->IsFeedPaired())
		|| !TestTrue(TEXT("the transaction began"), F.Player->FeedBegin(*F.Victim))
		|| !TestTrue(TEXT("the victim is paired too"), F.Victim->IsFeedPaired()))
	{
		return false;
	}
	F.Victim->Slot355();
	TestFalse(TEXT("slot 355 tears the victim's half of the transaction down"),
		F.Victim->IsFeedPaired());

	// And it is re-entry guarded, which is what lets the same slot be dispatched on both parties of
	// a grapple the way retail's block is entered on both.
	F.Victim->Slot355();
	TestFalse(TEXT("a second dispatch performs nothing"), F.Victim->IsFeedPaired());
	return true;
}

// =================================================================================================
// Slots 362 and 363 — `FInViewCone`, `0x10326a20` and `0x102b4540`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelClosureViewConeTest,
	"Elysium.Substrate.NpcKernelClosure.ViewCone", GElysiumNpcKernelClosureFlags)
bool FElysiumNpcKernelClosureViewConeTest::RunTest(const FString&)
{
	FElysiumClosureFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// **Two different bodies, not one body twice.** Slot 362 is `CAI_BaseNPC`'s
	// (a scope-trace push, `thunk_FUN_103268e0(this, point, m_flFieldOfView)`, a pop) and slot 363
	// is `CAI_BaseNPCTroika`'s override, which adds a null guard, the two sense-off ConVars and the
	// follower bypass before it reaches the base body at the target's EYE.
	N.Angles = FVector::ZeroVector;   // facing +X in Source angles
	const FVector Ahead = N.EyePosition() + FVector(300.f, 0.f, 0.f);
	const FVector Behind = N.EyePosition() + FVector(-300.f, 0.f, 0.f);

	TestEqual(TEXT("slot 362 is FElysiumNpcSenses::IsInViewCone on the point"),
		N.FInViewCone(Ahead), FElysiumNpcSenses::IsInViewCone(N, Ahead));
	TestTrue(TEXT("...and a point straight ahead is in it"), N.FInViewCone(Ahead));
	TestFalse(TEXT("...a point behind is not"), N.FInViewCone(Behind));

	TestEqual(TEXT("slot 363 is FElysiumNpcSenses::IsInViewCone on the entity"),
		N.FInViewCone(F.Victim), FElysiumNpcSenses::IsInViewCone(N, *F.Victim));
	// Retail's own first line: `if (param_1 == NULL) return false`.
	TestFalse(TEXT("slot 363's null guard"),
		N.FInViewCone(static_cast<FElysiumEntity*>(nullptr)));

	// The two are not interchangeable: 363 measures to the target's EYE, 362 to the point it was
	// given. Asking 362 for the victim's origin and 363 for the victim can disagree, and where they
	// agree it is because the eye and the origin fall in the same cone.
	TestEqual(TEXT("slot 362 at the victim's eye agrees with slot 363"),
		N.FInViewCone(F.Victim->EyePosition()), N.FInViewCone(F.Victim));
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
