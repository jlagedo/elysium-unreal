#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumAiScriptedSequence.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Tests/ElysiumEntityDebugStateTestHelpers.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 5 fold A3 — the script directors as C++ classes: `CCineNPC` (`FElysiumScriptedSequence`),
// `CCineAI` (`FElysiumAiScriptedSequence`) and `CCineAISchedule` (`FElysiumAiScriptedSchedule`). Every
// assertion is read off the body it names (the evidence brief:
// `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/briefs/A3-directors.md`).

static constexpr EAutomationTestFlags GDirectorTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	template <class T = FElysiumScriptedSequence>
	T* DirectorAs(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Entity = World.FindByName(Name);
		FElysiumNpcBase* Base = Entity != nullptr ? Entity->AsNpcBase() : nullptr;
		return Base != nullptr ? Base->AsSpecies<T>() : nullptr;
	}

	FElysiumEntityDef& AddDirector(FElysiumNpcWorldBuilder& Builder, const TCHAR* Classname, const TCHAR* Name,
		const TCHAR* Target, const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef& Def = Builder.AddEntity(Classname, Name, Origin);
		Def.Keys.Add(TEXT("m_iszEntity"), Target);
		return Def;
	}

	void Fire(FElysiumNpcWorldFixture& F, const FElysiumEntity* Target, const TCHAR* Input)
	{
		F.World.EnqueueInput(TEXT("!self"), FName(Input), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Target->Handle);
	}

	bool OwnedBy(const FElysiumEntity* Npc, const FElysiumEntity* Director)
	{
		return Npc != nullptr && Director != nullptr && Npc->ScriptOwner == Director->Handle;
	}
}

// -------------------------------------------------------------------------------------------------
// Slot 103 `Spawn` `0x101a6f10` and the constant own slots.
// -------------------------------------------------------------------------------------------------

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorSpawnTest,
	"Elysium.Arm.NpcKernelDirector.Spawn", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorSpawnTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_spawn"), 5301);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT(""), TEXT("nobody"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("named"), TEXT("nobody"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("on_spawn"), TEXT("nobody")).Keys.Add(
		TEXT("spawnflags"), TEXT("16"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("locked"), TEXT("nobody")).Keys.Add(
		TEXT("spawnflags"), TEXT("32"));
	AddDirector(Builder, TEXT("aiscripted_sequence"), TEXT("ai"), TEXT("nobody"));
	AddDirector(Builder, TEXT("aiscripted_schedule"), TEXT("sched"), TEXT("nobody"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));

	FElysiumScriptedSequence* Unnamed = nullptr;
	for (const TUniquePtr<FElysiumEntity>& Entity : F.World.Entities())
	{
		FElysiumNpcBase* Base = Entity.IsValid() ? Entity->AsNpcBase() : nullptr;
		FElysiumScriptedSequence* Cine = Base != nullptr ? Base->AsSpecies<FElysiumScriptedSequence>() : nullptr;
		if (Cine != nullptr && Cine->TargetName.IsEmpty())
		{
			Unnamed = Cine;
		}
	}
	FElysiumScriptedSequence* Named = DirectorAs(F.World, TEXT("named"));
	FElysiumScriptedSequence* OnSpawn = DirectorAs(F.World, TEXT("on_spawn"));
	FElysiumScriptedSequence* Locked = DirectorAs(F.World, TEXT("locked"));
	FElysiumAiScriptedSequence* Ai = DirectorAs<FElysiumAiScriptedSequence>(F.World, TEXT("ai"));
	FElysiumAiScriptedSchedule* Sched = DirectorAs<FElysiumAiScriptedSchedule>(F.World, TEXT("sched"));
	if (!TestNotNull(TEXT("the unnamed director stands"), Unnamed) || !TestNotNull(TEXT("named"), Named)
		|| !TestNotNull(TEXT("on_spawn"), OnSpawn) || !TestNotNull(TEXT("locked"), Locked)
		|| !TestNotNull(TEXT("aiscripted_sequence builds CCineAI"), Ai)
		|| !TestNotNull(TEXT("aiscripted_schedule builds CCineAISchedule"), Sched))
	{
		return false;
	}

	// A full `CAI_BaseNPC`, never a Troika NPC; its `CAI_Senses` exists and idles.
	TestNull(TEXT("a director is not a Troika NPC (+0x98 null)"), Named->AsNpc());
	TestTrue(TEXT("but answers +0x94 with itself"), Named->AsNpcBase() == Named);
	TestTrue(TEXT("and carries m_pSenses"), Named->SensesObject() == &Named->Senses);
	TestNull(TEXT("scripted_sequence is not a CCineAI"), Named->AsSpecies<FElysiumAiScriptedSequence>());

	// 0x101a6f10 in order: SOLID_NONE, FSOLID_NOT_SOLID, MOVETYPE_NONE, not targetable, not alive.
	TestEqual(TEXT("SetSolid(SOLID_NONE)"), Named->RetailSolidType, 0);
	TestTrue(TEXT("AddSolidFlags(FSOLID_NOT_SOLID)"), (Named->RetailSolidFlags & 0x4u) != 0);
	TestEqual(TEXT("SetMoveType(MOVETYPE_NONE)"), Named->RetailMoveType, 0);
	TestFalse(TEXT("m_bIsBCCTargetable = 0"), Named->bIsBccTargetable);
	TestFalse(TEXT("m_bIsAlive = 0"), Named->bNpcIsAlive);
	TestEqual(TEXT("AddFlag2(0x10)"), Named->EntityFlags2Word, 0x10u);
	TestFalse(TEXT("m_hNextCine = -1"), Named->NextCine.IsSet());

	// The think: an unnamed cine, or spawnflag 0x10, arms CineThink at +1.0; only a NAMED one that
	// armed it waits for BeginSequence (m_startTime + 1e6).
	using EThink = FElysiumScriptedSequence::EThinkFunction;
	TestTrue(TEXT("an unnamed cine arms CineThink"), Unnamed->ThinkFunction == EThink::CineThink);
	TestTrue(TEXT("a spawnflag-0x10 cine arms CineThink"), OnSpawn->ThinkFunction == EThink::CineThink);
	TestTrue(TEXT("a plain named cine arms nothing"), Named->ThinkFunction == EThink::None);
	TestTrue(TEXT("and the named 0x10 cine waits for BeginSequence"), OnSpawn->StartTime > 1.0e5);
	TestTrue(TEXT("the unnamed one does not"), Unnamed->StartTime < 1.0e5);
	TestTrue(TEXT("spawnflag 0x20 clears m_interruptable"), !Locked->bInterruptable && Named->bInterruptable);

	// The constant own slots.
	TestFalse(TEXT("slot 72 0x101a6e20 answers false"), Named->Slot72(1));
	TestEqual(TEXT("slot 117 0x101a6d20 drops FCAP_ACROSS_TRANSITION"),
		Named->ObjectCaps() & ElysiumEntityCaps::AcrossTransition, 0);
	TestFalse(TEXT("slot 362 cone is false"), Named->FInViewCone(FVector::ZeroVector));
	TestFalse(TEXT("slot 364 cone is false"), Named->FInAimCone(FVector::ZeroVector));
	TestTrue(TEXT("slot 82 answers the CCineNPC descriptor"),
		Named->GetDataDescMap() == FElysiumClassRegistry::Get().Find(FName(TEXT("CCineNPC"))));
	TestTrue(TEXT("CCineAISchedule's slot 82 answers its own descriptor"),
		Sched->GetDataDescMap() == FElysiumClassRegistry::Get().Find(FName(TEXT("CCineAISchedule"))));
	TestFalse(TEXT("slot 585 0x101a7210: spawnflag 0x40 clear"), Named->FCanOverrideState());
	TestTrue(TEXT("slot 585 0x101a9060: CCineAI always true"), Ai->FCanOverrideState());
	TestTrue(TEXT("slot 585 0x101a9770: CCineAISchedule always true"), Sched->FCanOverrideState());
	return true;
}

// -------------------------------------------------------------------------------------------------
// `FindEntity` `0x101a7600`: the radius gate, `CineThink` `0x101a8070`'s retry, `CancelScript`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorRadiusTest,
	"Elysium.Arm.NpcKernelDirector.RadiusGate", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorRadiusTest::RunTest(const FString&)
{
	const float U = ElysiumMove::U;
	FElysiumNpcWorldBuilder Builder(TEXT("director_radius"), 5302);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"), FVector(2000.f * U, 0.f, 0.f));
	Builder.AddNpc(TEXT("bob"), FVector(0.f, 300.f * U, 0.f));
	// 512 units, the commonest authored radius; jack stands 2000 units away.
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("far"), TEXT("jack")).Keys.Add(
		TEXT("m_flRadius"), TEXT("512"));
	// Two directors sharing one targetname: `CancelScript` sweeps them both.
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("pair"), TEXT("bob"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("pair"), TEXT("jack"), FVector(0.f, 0.f, 10.f)).Keys.Add(
		TEXT("m_flRadius"), TEXT("64"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumNpc* Bob = F.Npc(TEXT("bob"));
	FElysiumScriptedSequence* Far = DirectorAs(F.World, TEXT("far"));
	TArray<FElysiumScriptedSequence*> Pair;
	for (const TUniquePtr<FElysiumEntity>& Entity : F.World.Entities())
	{
		FElysiumNpcBase* Base = Entity.IsValid() ? Entity->AsNpcBase() : nullptr;
		FElysiumScriptedSequence* Cine = Base != nullptr ? Base->AsSpecies<FElysiumScriptedSequence>() : nullptr;
		if (Cine != nullptr && Cine->TargetName == TEXT("pair"))
		{
			Pair.Add(Cine);
		}
	}
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("bob"), Bob) || !TestNotNull(TEXT("far"), Far)
		|| !TestEqual(TEXT("two directors named pair"), Pair.Num(), 2))
	{
		return false;
	}
	// Bob thinks: possession writes only `m_IdealNPCState = 4` (`0x101a7e84`); he enters
	// NPC_STATE_SCRIPT through his own `MaintainSchedule` `0x102817c0`, as retail's NPC does.
	FElysiumNpcWorldFixture::Quiet({ Jack });
	using EThink = FElysiumScriptedSequence::EThinkFunction;

	// BeginSequence with the NPC beyond the radius: not found, and NOTHING happens — no think, no
	// output (`0x101a7412 JZ 0x101a74a9`).
	Fire(F, Far, TEXT("BeginSequence"));
	F.Advance(0.1);
	TestFalse(TEXT("an NPC beyond m_flRadius is not found"), Jack->ScriptOwner.IsSet());
	TestNull(TEXT("and the director holds no target"), Far->TargetNpc());
	TestTrue(TEXT("and arms no think"), Far->ThinkFunction == EThink::None);

	// MoveToPosition with no target arms CineThink now; its failure retries in 1.0 s, forever.
	Fire(F, Far, TEXT("MoveToPosition"));
	F.Advance(0.2);
	TestTrue(TEXT("CineThink stays installed after a failed search"), Far->ThinkFunction == EThink::CineThink);
	TestTrue(TEXT("and retries 1.0 s later"), Far->CineThinkAt > F.World.NowSeconds() + 0.5);

	// The NPC walks into the radius: the next retry finds and possesses it.
	Jack->SetRuntimeOrigin(FVector(100.f * U, 0.f, 0.f));
	F.Advance(F.World.NowSeconds() + 1.2);
	TestTrue(TEXT("inside m_flRadius the retry possesses the NPC"), OwnedBy(Jack, Far));
	TestTrue(TEXT("MoveToPosition holds it waiting (m_startTime + 1e6)"), Far->StartTime > 1.0e5);

	// `CancelScript` 0x101a8c30: `pair`#1 takes bob; `pair`#2's failed search sweeps every entity
	// named `pair` through `ScriptEntityCancel`, which releases bob.
	FElysiumScriptedSequence* Holder = Pair[0]->TargetEntity == TEXT("bob") ? Pair[0] : Pair[1];
	FElysiumScriptedSequence* Seeker = Holder == Pair[0] ? Pair[1] : Pair[0];
	Fire(F, Holder, TEXT("MoveToPosition"));
	F.Advance(F.World.NowSeconds() + 0.2);
	if (TestTrue(TEXT("the first pair director possesses bob"), OwnedBy(Bob, Holder)))
	{
		TestEqual(TEXT("bob is in NPC_STATE_SCRIPT"), Bob->NpcStateRetail(), 4);
		Fire(F, Seeker, TEXT("MoveToPosition"));
		F.Advance(F.World.NowSeconds() + 0.2);
		TestFalse(TEXT("the second one's failed search cancels every same-named cine"),
			Bob->ScriptOwner.IsSet());
		TestTrue(TEXT("and keeps retrying"), Seeker->ThinkFunction == EThink::CineThink);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `PossessEntity` `0x101a7880`: the queue arm, the kick, and the chain through `Finish`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorQueueTest,
	"Elysium.Arm.NpcKernelDirector.Queue", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorQueueTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_queue"), 5303);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"));
	Builder.AddCounter(TEXT("c2"));
	Builder.AddCounter(TEXT("c3"));
	{
		// Holds jack in its post-idle forever (spawnflag 0x100): m_scriptState 2.
		FElysiumEntityDef& Hold = AddDirector(Builder, TEXT("scripted_sequence"), TEXT("s1"), TEXT("jack"));
		Hold.Keys.Add(TEXT("m_iszPostIdle"), TEXT("idle"));
		Hold.Keys.Add(TEXT("spawnflags"), TEXT("256"));
	}
	// Two challengers that may disregard the NPC's state (spawnflag 0x40).
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("s2"), TEXT("jack")).Keys.Add(TEXT("spawnflags"), TEXT("64"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("s3"), TEXT("jack")).Keys.Add(TEXT("spawnflags"), TEXT("64"));
	Builder.WireOutput(TEXT("s2"), TEXT("OnBeginSequence"), TEXT("c2"));
	Builder.WireOutput(TEXT("s3"), TEXT("OnBeginSequence"), TEXT("c3"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = DirectorAs(F.World, TEXT("s1"));
	FElysiumScriptedSequence* S2 = DirectorAs(F.World, TEXT("s2"));
	FElysiumScriptedSequence* S3 = DirectorAs(F.World, TEXT("s3"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1) || !TestNotNull(TEXT("s2"), S2)
		|| !TestNotNull(TEXT("s3"), S3))
	{
		return false;
	}
	// Jack thinks: the scene runs in his own program (`SelectSchedule` `0x1028a380` case 4 ->
	// `SCHED_AISCRIPT 0x2e`), and the post-idle task's run arm is what sees the queued cine.

	Fire(F, S1, TEXT("BeginSequence"));
	F.Advance(0.5);
	if (!TestTrue(TEXT("s1 holds jack in its post-idle"), OwnedBy(Jack, S1)))
	{
		return false;
	}
	TestEqual(TEXT("the NPC's m_scriptState 2 (post-idle)"), Jack->GetScriptState(), 2);

	// Both challengers arrive in one pass: s2 is queued behind s1, then s3 kicks s2 out of the queue.
	Fire(F, S2, TEXT("BeginSequence"));
	Fire(F, S3, TEXT("BeginSequence"));
	F.World.Tick(F.World.NowSeconds());
	TestTrue(TEXT("the NPC stays with s1"), OwnedBy(Jack, S1));
	TestTrue(TEXT("s3 is queued as s1's m_hNextCine"), S1->NextCine == S3->Handle);
	TestNull(TEXT("s2 was kicked: its target is cleared"), S2->TargetNpc());

	// s1's post-idle task sees a live next cine and runs `Finish`: SUB_Remove (not repeatable),
	// cleanup, and s3 possesses jack directly.
	F.Advance(F.World.NowSeconds() + 0.5);
	TestEqual(TEXT("s3's beat began"), ElysiumEntityDebugTest::CounterValue(F.World.FindByName(TEXT("c3"))), 1.f);
	TestEqual(TEXT("the kicked s2 never did"), ElysiumEntityDebugTest::CounterValue(F.World.FindByName(TEXT("c2"))), 0.f);
	TestTrue(TEXT("s1 removed itself (SUB_Remove, spawnflag 4 clear)"), S1->IsDead());
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CCineAI` `0x101a9080` / `0x101a95d0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorAiPossessTest,
	"Elysium.Arm.NpcKernelDirector.AiPossess", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorAiPossessTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_ai"), 5304);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"));
	Builder.AddNpc(TEXT("bob"), FVector(300.f, 0.f, 0.f));
	{
		FElysiumEntityDef& Hold = AddDirector(Builder, TEXT("scripted_sequence"), TEXT("s1"), TEXT("jack"));
		Hold.Keys.Add(TEXT("m_iszPostIdle"), TEXT("idle"));
		Hold.Keys.Add(TEXT("spawnflags"), TEXT("256"));
	}
	AddDirector(Builder, TEXT("aiscripted_sequence"), TEXT("a1"), TEXT("jack"));
	AddDirector(Builder, TEXT("aiscripted_sequence"), TEXT("a2"), TEXT("bob"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumNpc* Bob = F.Npc(TEXT("bob"));
	FElysiumScriptedSequence* S1 = DirectorAs(F.World, TEXT("s1"));
	FElysiumAiScriptedSequence* A1 = DirectorAs<FElysiumAiScriptedSequence>(F.World, TEXT("a1"));
	FElysiumAiScriptedSequence* A2 = DirectorAs<FElysiumAiScriptedSequence>(F.World, TEXT("a2"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("bob"), Bob) || !TestNotNull(TEXT("s1"), S1)
		|| !TestNotNull(TEXT("a1"), A1) || !TestNotNull(TEXT("a2"), A2))
	{
		return false;
	}
	// Jack thinks, so his own `MaintainSchedule` `0x102817c0` takes him into NPC_STATE_SCRIPT; bob stays
	// quiet, so he is not in it when a2 possesses him.
	FElysiumNpcWorldFixture::Quiet({ Bob });

	Fire(F, S1, TEXT("BeginSequence"));
	F.Advance(0.5);
	if (!TestTrue(TEXT("s1 holds jack"), OwnedBy(Jack, S1)))
	{
		return false;
	}
	TestEqual(TEXT("jack is in NPC_STATE_SCRIPT"), Jack->NpcStateRetail(), 4);

	// No queue: CCineAI OVERWRITES the standing cine, and an NPC already in SCRIPT gets `0x2e`
	// (SCHED_AISCRIPT) installed through `0x10280de0` directly.
	const int32 AiScriptStamp = Jack->ResolveIdealScheduleStamp(FElysiumAiScriptedSequence::ScheduleAiScript);
	TestNotEqual(TEXT("0x2e resolves through the NPC's id space"), AiScriptStamp, static_cast<int32>(INDEX_NONE));
	A1->SetTarget(Jack->Handle);
	A1->PossessEntity();
	TestTrue(TEXT("CCineAI takes the NPC from the standing cine"), OwnedBy(Jack, A1));
	TestFalse(TEXT("and queues nothing on it"), S1->NextCine.IsSet());
	TestEqual(TEXT("0x2e is stamped as the NPC's ideal schedule"), Jack->BaseScheduleHost.IdealScheduleRetail,
		AiScriptStamp);

	// An NPC NOT yet in SCRIPT reselects on its own: no install.
	A2->SetTarget(Bob->Handle);
	A2->PossessEntity();
	TestTrue(TEXT("a2 possesses bob"), OwnedBy(Bob, A2));
	TestNotEqual(TEXT("and installs no 0x2e on an NPC outside SCRIPT"),
		Bob->BaseScheduleHost.IdealScheduleRetail,
		Bob->ResolveIdealScheduleStamp(FElysiumAiScriptedSequence::ScheduleAiScript));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorAiFinishScheduleTest,
	"Elysium.Arm.NpcKernelDirector.AiFinishSchedule", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorAiFinishScheduleTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_ai_finish"), 5305);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"));
	AddDirector(Builder, TEXT("aiscripted_sequence"), TEXT("a1"), TEXT("guard"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("s1"), TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumAiScriptedSequence* A1 = DirectorAs<FElysiumAiScriptedSequence>(F.World, TEXT("a1"));
	FElysiumScriptedSequence* S1 = DirectorAs(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("a1"), A1) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);

	// m_iFinishSchedule 0: ClearSchedule.
	A1->FinishSchedule = 0;
	Guard->ChangeSchedule(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestTrue(TEXT("a program is installed"), Guard->Schedule.IsRunning());
	A1->FixScriptNPCSchedule(*Guard);
	TestFalse(TEXT("m_iFinishSchedule 0 clears the schedule"), Guard->Schedule.IsRunning());

	// m_iFinishSchedule 1: local 0x2a — SCHED_AMBUSH in the cai_basenpc space, NOT SCHED_AISCRIPT (0x2e)
	// — through `0x10280de0`, with no clear.
	A1->FinishSchedule = 1;
	Guard->ChangeSchedule(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	A1->FixScriptNPCSchedule(*Guard);
	TestNotEqual(TEXT("0x2a resolves through the NPC's id space"),
		Guard->ResolveIdealScheduleStamp(FElysiumAiScriptedSequence::ScheduleAmbush), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("m_iFinishSchedule 1 stamps local 0x2a (AMBUSH)"), Guard->BaseScheduleHost.IdealScheduleRetail,
		Guard->ResolveIdealScheduleStamp(FElysiumAiScriptedSequence::ScheduleAmbush));

	// Anything else: "no case" and the clear.
	A1->FinishSchedule = 7;
	Guard->ChangeSchedule(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	A1->FixScriptNPCSchedule(*Guard);
	TestFalse(TEXT("any other value clears"), Guard->Schedule.IsRunning());

	// `CCineNPC::FixScriptNPCSchedule` 0x101a8840 ignores m_iFinishSchedule: IDLE unless DEAD, then clear.
	S1->FinishSchedule = 1;
	Guard->ChangeSchedule(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	S1->FixScriptNPCSchedule(*Guard);
	TestFalse(TEXT("CCineNPC clears on every path"), Guard->Schedule.IsRunning());
	TestEqual(TEXT("and stores NPC_STATE_IDLE"), Guard->IdealStateRetail(), 1);
	Guard->RequestIdealStateRetail(7, 0);
	S1->FixScriptNPCSchedule(*Guard);
	TestEqual(TEXT("a dead NPC keeps NPC_STATE_DEAD"), Guard->IdealStateRetail(), 7);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The scene's program: `SelectSchedule` `0x1028a380` case 4 answers `SCHED_AISCRIPT 0x2e` while
// `m_hCine` is live, and `TranslateSchedule` `0x102cc080` splits it by the cine's `m_fMoveTo`
// (`+0x5f60`) into `0x2f..0x33`, which the Troika table (`0x102b12f0`) maps to `0xf2..0xf9`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorAiScriptTranslateTest,
	"Elysium.Arm.NpcKernelDirector.AiScriptTranslate", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorAiScriptTranslateTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_aiscript"), 5316);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpcOfClass(TEXT("jack"), FVector::ZeroVector, TEXT("CAI_BaseNPCTroika"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("s1"), TEXT("jack"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = DirectorAs(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });

	// `m_hCine` on the NPC and `m_hTargetEnt` on the director, stood by hand; jack in SCRIPT.
	Jack->ScriptOwner = S1->Handle;
	S1->SetTarget(Jack->Handle);
	Jack->WriteNpcStateRetail(4);
	TestEqual(TEXT("0x1028a991 case 4 with a live m_hCine -> 0x2e SCHED_AISCRIPT"), Jack->BaseSelectSchedule(),
		0x2e);

	struct FRow { int32 MoveTo; int32 Program; const TCHAR* Name; };
	static const FRow Rows[] = {
		{ 1, 0xf2, TEXT("m_fMoveTo 1 -> 0x2f -> 0xf2 SCHED_TROIKA_SCRIPTED_WALK") },
		{ 2, 0xf4, TEXT("m_fMoveTo 2 -> 0x30 -> 0xf4 SCHED_TROIKA_SCRIPTED_RUN") },
		{ 3, 0xf6, TEXT("m_fMoveTo 3 -> 0x31 -> 0xf6 SCHED_TROIKA_SCRIPTED_CUSTOM_MOVE") },
		{ 0, 0xf8, TEXT("m_fMoveTo 0 -> 0x32 -> 0xf8 SCHED_TROIKA_SCRIPTED_WAIT") },
		{ 4, 0xf8, TEXT("m_fMoveTo 4 -> 0x32 -> 0xf8 SCHED_TROIKA_SCRIPTED_WAIT") },
		{ 5, 0xf9, TEXT("m_fMoveTo 5 -> 0x33 -> 0xf9 SCHED_TROIKA_SCRIPTED_FACE") },
	};
	for (const FRow& Row : Rows)
	{
		S1->MoveTo = Row.MoveTo;
		TestEqual(*FString::Printf(TEXT("0x102cc080 %s"), Row.Name), Jack->TranslateScheduleRetail(0x2e),
			Row.Program);
	}
	// `0x102cc122 CMP EDX,0x5 / JA 0x102cc17e`: past the table, `0x2e` comes back untranslated.
	S1->MoveTo = 6;
	TestEqual(TEXT("0x102cc17e m_fMoveTo 6 leaves 0x2e as it is"), Jack->TranslateScheduleRetail(0x2e), 0x2e);
	TestTrue(TEXT("and the live-cine arms never ran CineCleanup"), Jack->ScriptOwner == S1->Handle);
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// -------------------------------------------------------------------------------------------------
// `DelayStart` `0x101a8cf0`, `IsTimeToStart` `0x101a7540`, `LinkedSequence` `0x101a8130`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorStartGateTest,
	"Elysium.Substrate.NpcKernelDirector.StartGate", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorStartGateTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_gate"), 5306);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("grp"), TEXT("nobody"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("grp"), TEXT("nobody"), FVector(10.f, 0.f, 0.f));
	AddDirector(Builder, TEXT("aiscripted_sequence"), TEXT("grp"), TEXT("nobody"), FVector(20.f, 0.f, 0.f));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("lead"), TEXT("nobody")).Keys.Add(
		TEXT("m_iszLinkedSequence"), TEXT("partner"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("partner"), TEXT("nobody"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	TArray<FElysiumScriptedSequence*> Grp;
	FElysiumAiScriptedSequence* AiGrp = nullptr;
	for (const TUniquePtr<FElysiumEntity>& Entity : F.World.Entities())
	{
		FElysiumNpcBase* Base = Entity.IsValid() ? Entity->AsNpcBase() : nullptr;
		FElysiumScriptedSequence* Cine = Base != nullptr ? Base->AsSpecies<FElysiumScriptedSequence>() : nullptr;
		if (Cine != nullptr && Cine->TargetName == TEXT("grp"))
		{
			if (FElysiumAiScriptedSequence* Ai = Base->AsSpecies<FElysiumAiScriptedSequence>())
			{
				AiGrp = Ai;
			}
			else
			{
				Grp.Add(Cine);
			}
		}
	}
	if (!TestEqual(TEXT("two scripted_sequence rows named grp"), Grp.Num(), 2)
		|| !TestNotNull(TEXT("and one aiscripted_sequence"), AiGrp))
	{
		return false;
	}
	// `DelayStart(1)` counts every same-named LITERAL `scripted_sequence`.
	Grp[0]->DelayStart(true);
	TestEqual(TEXT("the caller counts"), Grp[0]->Delay, 1);
	TestEqual(TEXT("its same-named twin counts"), Grp[1]->Delay, 1);
	TestEqual(TEXT("the aiscripted_sequence sharing the name does not"), AiGrp->Delay, 0);
	TestFalse(TEXT("m_iDelay 1 is not time to start"), Grp[1]->IsTimeToStart());

	// `DelayStart(0)` releases it and stamps m_startTime = now + 0.05: an AND, so not yet.
	const double Now = F.World.NowSeconds();
	Grp[1]->DelayStart(false);
	TestEqual(TEXT("m_iDelay back to 0"), Grp[0]->Delay, 0);
	TestTrue(TEXT("m_startTime = now + 0.05"), FMath::IsNearlyEqual(Grp[0]->StartTime, Now + 0.05, 1.0e-6));
	TestFalse(TEXT("both terms must hold: the start time has not come"), Grp[0]->IsTimeToStart());
	F.Advance(Now + 0.1);
	TestTrue(TEXT("and then it has"), Grp[0]->IsTimeToStart());

	FElysiumScriptedSequence* Lead = DirectorAs(F.World, TEXT("lead"));
	FElysiumScriptedSequence* Partner = DirectorAs(F.World, TEXT("partner"));
	if (TestNotNull(TEXT("lead"), Lead) && TestNotNull(TEXT("partner"), Partner))
	{
		TestTrue(TEXT("0x101a8130 resolves m_iszLinkedSequence to the CCineNPC it names"),
			Lead->LinkedSequence() == Partner);
		TestNull(TEXT("and nothing without one"), Partner->LinkedSequence());
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CanInterrupt` `0x101a8930`, `AllowInterrupt` `0x101a8890`, slot 459 `0x101a89a0`.
// -------------------------------------------------------------------------------------------------

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorInterruptTest,
	"Elysium.Arm.NpcKernelDirector.Interrupt", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorInterruptTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_interrupt"), 5307);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("open"), TEXT("jack"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("locked"), TEXT("jack")).Keys.Add(
		TEXT("spawnflags"), TEXT("32"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* Open = DirectorAs(F.World, TEXT("open"));
	FElysiumScriptedSequence* Locked = DirectorAs(F.World, TEXT("locked"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("open"), Open) || !TestNotNull(TEXT("locked"), Locked))
	{
		return false;
	}
	// Jack thinks: slot 459's `0x1026d7f0` guard reads NPC_STATE_SCRIPT, which he enters through his
	// own `MaintainSchedule` `0x102817c0` after possession writes the ideal.

	TestFalse(TEXT("no target: CanInterrupt is false"), Open->CanInterrupt());
	Open->SetTarget(Jack->Handle);
	TestTrue(TEXT("interruptable and a live target: true"), Open->CanInterrupt());

	// `AllowInterrupt` flips the word and the NPC's oblivious count with it.
	const int32 Oblivious = Jack->ObliviousCount;
	Open->AllowInterrupt(false);
	TestFalse(TEXT("AllowInterrupt(false) clears m_interruptable"), Open->bInterruptable);
	TestEqual(TEXT("and makes the NPC oblivious (0x1026d130)"), Jack->ObliviousCount, Oblivious + 1);
	Open->AllowInterrupt(true);
	TestTrue(TEXT("AllowInterrupt(true) sets it back"), Open->bInterruptable);
	TestEqual(TEXT("and releases the count (0x10007ea0)"), Jack->ObliviousCount, Oblivious);
	Locked->SetTarget(Jack->Handle);
	Locked->AllowInterrupt(true);
	TestFalse(TEXT("spawnflag 0x20 makes AllowInterrupt a no-op"), Locked->bInterruptable);
	Locked->SetTarget(FElysiumEntityHandle::Invalid());
	Open->SetTarget(FElysiumEntityHandle::Invalid());

	// The locked director possesses jack (NOINTERRUPT -> `0x1026d130`), and the NPC's slot 459 in
	// SCRIPT dispatches the director's `0x101a89a0`, which strips its conditions.
	Fire(F, Locked, TEXT("MoveToPosition"));
	F.Advance(0.3);
	if (!TestTrue(TEXT("the locked director holds jack"), OwnedBy(Jack, Locked)))
	{
		return false;
	}
	TestEqual(TEXT("possession made jack oblivious"), Jack->ObliviousCount, Oblivious + 1);
	Jack->Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	Jack->Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	Jack->RemoveIgnoredConditions();
	TestFalse(TEXT("slot 459 reaches the director and clears LIGHT_DAMAGE"),
		Jack->Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
	TestTrue(TEXT("but not SEE_ENEMY"), Jack->Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy));

	// `InputCancelSequence` 0x101a7500 cancels THIS cine: cleanup releases jack and the count.
	Fire(F, Locked, TEXT("CancelSequence"));
	F.Advance(F.World.NowSeconds() + 0.1);
	TestFalse(TEXT("CancelSequence releases the NPC"), Jack->ScriptOwner.IsSet());
	TestEqual(TEXT("and CineCleanup gives the oblivious count back"), Jack->ObliviousCount, Oblivious);
	TestEqual(TEXT("and leaves the ideal state IDLE"), Jack->IdealStateRetail(), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 180 `UpdateOnRemove` `0x101a7140`, and the save refusal while the cine possesses its NPC.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorRemovalTest,
	"Elysium.Arm.NpcKernelDirector.Removal", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorRemovalTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_removal"), 5308);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("beat"), TEXT("jack"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* Beat = DirectorAs(F.World, TEXT("beat"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("beat"), Beat))
	{
		return false;
	}
	// Jack thinks: possession writes only `m_IdealNPCState = 4` (`0x101a7e84`), and his own
	// `MaintainSchedule` `0x102817c0` takes him into NPC_STATE_SCRIPT -- the state
	// `ScriptEntityCancel 0x101a7170` tests (`0x101a71b8`) before it cleans the NPC up.
	TestNull(TEXT("an idle director does not refuse a save"), Beat->SaveBlockReason());

	Fire(F, Beat, TEXT("MoveToPosition"));
	F.Advance(0.5);
	if (!TestTrue(TEXT("the beat holds jack"), OwnedBy(Jack, Beat))
		|| !TestEqual(TEXT("jack is in NPC_STATE_SCRIPT"), Jack->NpcStateRetail(), 4))
	{
		return false;
	}
	// **Kept divergence K1** (`stories/v3/README.md` § 7): retail saves an in-progress scene (the
	// datamap words and the NPC's scripted schedule) and resumes it; the port refuses the save while
	// the cine possesses its NPC (`m_hCine == this`), until V6 lands resume.
	TestNotNull(TEXT("a save is refused while the cine possesses its NPC"), Beat->SaveBlockReason());

	// Kill mid-scene: `UTIL_Remove` runs slot 180, whose `ScriptEntityCancel` releases the NPC.
	Fire(F, Beat, TEXT("Kill"));
	F.Advance(F.World.NowSeconds() + 0.1);
	TestTrue(TEXT("the director is gone"), Beat->IsDead());
	TestFalse(TEXT("and released its NPC through UpdateOnRemove"), Jack->ScriptOwner.IsSet());
	TestNotEqual(TEXT("which is no longer in NPC_STATE_SCRIPT"), Jack->NpcStateRetail(), 4);
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// -------------------------------------------------------------------------------------------------
// Persistence of an idle director: the `CCineNPC` / `CCineAISchedule` datamap SAVE rows and the
// installed think ride a real snapshot (retail's datamap chain has no Save/Restore override).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorSaveRestoreTest,
	"Elysium.Substrate.NpcKernelDirector.SaveRestore", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorSaveRestoreTest::RunTest(const FString&)
{
	auto Build = []()
	{
		FElysiumNpcWorldBuilder Builder(TEXT("director_save"), 5309);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		FElysiumEntityDef& Seq = AddDirector(Builder, TEXT("scripted_sequence"), TEXT("seq"), TEXT("nobody"));
		Seq.Keys.Add(TEXT("m_flRadius"), TEXT("512"));
		Seq.Keys.Add(TEXT("m_iszIdle"), TEXT("wait_idle"));
		AddDirector(Builder, TEXT("scripted_sequence"), TEXT("next"), TEXT("nobody"));
		FElysiumEntityDef& Sched = AddDirector(Builder, TEXT("aiscripted_schedule"), TEXT("sched"), TEXT("nobody"));
		Sched.Keys.Add(TEXT("goalent"), TEXT("!player"));
		Sched.Keys.Add(TEXT("schedule"), TEXT("2"));
		Sched.Keys.Add(TEXT("forcestate"), TEXT("3"));
		return Builder;
	};
	FElysiumNpcWorldFixture From(Build());
	FElysiumScriptedSequence* Seq = DirectorAs(From.World, TEXT("seq"));
	FElysiumScriptedSequence* Next = DirectorAs(From.World, TEXT("next"));
	FElysiumAiScriptedSchedule* Sched = DirectorAs<FElysiumAiScriptedSchedule>(From.World, TEXT("sched"));
	if (!TestNotNull(TEXT("seq"), Seq) || !TestNotNull(TEXT("next"), Next) || !TestNotNull(TEXT("sched"), Sched))
	{
		return false;
	}
	TestTrue(TEXT("m_iszIdle keys m_iszPreIdle"), Seq->PreIdle == TEXT("wait_idle"));
	TestEqual(TEXT("m_flRadius is bound"), Seq->Radius, 512.f);
	// The words no keyvalue reaches: stamp them as a beat would leave them.
	Seq->Delay = 2;
	Seq->StartTime = 42.5;
	Seq->NextCine = Next->Handle;
	Seq->bSequenceStarted = true;
	Seq->bInterruptable = false;
	Seq->SavedMoveType = 3;
	// The schedule director is mid-search: `CineThink` armed, the retry pending.
	Fire(From, Sched, TEXT("StartSchedule"));
	From.Advance(0.2);
	TestTrue(TEXT("the search is retrying"),
		Sched->ThinkFunction == FElysiumScriptedSequence::EThinkFunction::CineThink);
	TestNull(TEXT("an idle director does not refuse the save"), Seq->SaveBlockReason());

	FElysiumNpcWorldFixture To(Build(), true);
	ElysiumRoundTripSnapshot(From.World, To.World);
	FElysiumScriptedSequence* RSeq = DirectorAs(To.World, TEXT("seq"));
	FElysiumAiScriptedSchedule* RSched = DirectorAs<FElysiumAiScriptedSchedule>(To.World, TEXT("sched"));
	if (!TestNotNull(TEXT("seq restores"), RSeq) || !TestNotNull(TEXT("sched restores"), RSched))
	{
		return false;
	}
	TestEqual(TEXT("m_iDelay survives"), RSeq->Delay, 2);
	TestTrue(TEXT("m_startTime survives"), FMath::IsNearlyEqual(RSeq->StartTime, 42.5, 1.0e-3));
	TestTrue(TEXT("m_hNextCine survives"), RSeq->NextCine.IsSet() && To.World.Resolve(RSeq->NextCine) != nullptr);
	TestTrue(TEXT("m_sequenceStarted survives"), RSeq->bSequenceStarted);
	TestFalse(TEXT("m_interruptable survives"), RSeq->bInterruptable);
	TestEqual(TEXT("m_saved_movetype survives"), RSeq->SavedMoveType, 3);
	TestEqual(TEXT("CCineAISchedule's m_nSchedule survives"), RSched->Mode, 2);
	TestEqual(TEXT("and m_nForceState"), RSched->ForceState, 3);
	TestTrue(TEXT("the installed CineThink survives"),
		RSched->ThinkFunction == FElysiumScriptedSequence::EThinkFunction::CineThink);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `Event_Killed` `0x10265ad0`: a killed NPC in SCRIPT runs `CancelScript` at once, mid-play.
// -------------------------------------------------------------------------------------------------

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorKilledTest,
	"Elysium.Arm.NpcKernelDirector.KilledMidPlay", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorKilledTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_killed"), 5311);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack")).Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	{
		FElysiumEntityDef& Beat = AddDirector(Builder, TEXT("scripted_sequence"), TEXT("beat"), TEXT("jack"));
		Beat.Keys.Add(TEXT("m_iszPlay"), TEXT("long_action"));
		Beat.Keys.Add(TEXT("spawnflags"), TEXT("32"));
	}
	FElysiumNpcWorldFixture F(MoveTemp(Builder), [](FElysiumRecordingServices& Services)
		{
			Services.ClipSeconds = 30.f;
			// Jack's model authors `long_action`, so `StartSequence`'s `LookupSequence`
			// (`0x101a830d`) finds it and the play runs its 30 s rather than sequence 0's none.
			Services.KnownNpcClips.FindOrAdd(TEXT("jack")).Add(TEXT("long_action"));
		});
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* Beat = DirectorAs(F.World, TEXT("beat"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("beat"), Beat))
	{
		return false;
	}
	// Jack thinks: the scene's play starts in his own program (`TASK_WAIT_FOR_SCRIPT`'s run arm ->
	// `StartScript` -> `StartSequence(m_iszPlay)`), which is what sets the cine's `m_sequenceStarted`.
	const int32 Oblivious = Jack->ObliviousCount;
	Fire(F, Beat, TEXT("BeginSequence"));
	F.Advance(0.5);
	if (!TestTrue(TEXT("the cine holds jack and has started its 30 s play"), OwnedBy(Jack, Beat)
		&& Beat->bSequenceStarted))
	{
		return false;
	}
	Beat->Delay = 3;
	// Corrected to retail (story 8 wave 2): the kill is slot 144, and `CAI_BaseNPC::Event_Killed`
	// (`0x10265ad0`) DEFERS it for an NPC in NPC_STATE_SCRIPT whose live cine has started a sequence
	// and whose spawnflags do not read exactly 0x80 of 0x2080 (`0x10265b2f` / `0x10265b51`): the
	// packet is copied into `m_DeferredDeathInfo` and the body returns with the NPC alive and still
	// owned. (No function in the image reads `m_DeferredDeathInfo` back — `vtmb_grep` finds only its
	// two writers — so the deferred death is never replayed; reproduced.) The port's retired
	// transaction cancelled the beat outright.
	(void)Oblivious;
	TestEqual(TEXT("the beat's NPC is in NPC_STATE_SCRIPT"), Jack->NpcStateRetail(), 4);
	Jack->OnKilled();
	TestTrue(TEXT("0x10265c04 the death is deferred: the NPC stays owned by the cine"),
		OwnedBy(Jack, Beat));
	TestTrue(TEXT("...alive (0x1032b9b0 never ran)"), Jack->IsAlive());
	TestEqual(TEXT("...and m_iDelay untouched"), Beat->Delay, 3);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The remaining constant slots.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorSlotsTest,
	"Elysium.Arm.NpcKernelDirector.ConstantSlots", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorSlotsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_slots"), 5313);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"));
	Builder.AddCounter(TEXT("c"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("seq"), TEXT("jack")).Keys.Add(
		TEXT("angles"), TEXT("0 90 0"));
	AddDirector(Builder, TEXT("aiscripted_sequence"), TEXT("ai"), TEXT("jack"));
	Builder.WireOutput(TEXT("seq"), TEXT("OnEndSequence"), TEXT("c"));
	Builder.WireOutput(TEXT("ai"), TEXT("OnEndSequence"), TEXT("c"), TEXT("Add"), TEXT("10"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* Seq = DirectorAs(F.World, TEXT("seq"));
	FElysiumAiScriptedSequence* Ai = DirectorAs<FElysiumAiScriptedSequence>(F.World, TEXT("ai"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("seq"), Seq) || !TestNotNull(TEXT("ai"), Ai))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });

	// Slots 363 / 365: the entity cones are false.
	TestFalse(TEXT("slot 363 0x101a6da0"), Seq->FInViewCone(static_cast<FElysiumEntity*>(Jack)));
	TestFalse(TEXT("slot 365 0x101a6de0"), Seq->FInAimCone(static_cast<FElysiumEntity*>(Jack)));
	// Slots 370 / 371: the body direction.
	TestTrue(TEXT("slot 370 0x101a6d40 is BodyDirection2D"),
		Seq->HeadDirection2D().Equals(Seq->BodyDirection2D(), 1.0e-4));
	TestTrue(TEXT("slot 371 0x101a6d70 is BodyDirection3D"),
		Seq->HeadDirection3D().Equals(Seq->BodyDirection3D(), 1.0e-4));
	// Slot 584 on an empty name with bCompleteOnEmpty: `SequenceDone` runs (OnEndSequence fires);
	// `CCineNPC` answers false, `CCineAI` true.
	TestFalse(TEXT("0x101a82d0 answers false on the empty-name path"), Seq->StartSequence(*Jack, FString(), true));
	TestTrue(TEXT("and set m_sequenceStarted"), Seq->bSequenceStarted);
	TestTrue(TEXT("0x101a9510 answers true on it"), Ai->StartSequence(*Jack, FString(), true));
	F.Advance(0.1);
	TestEqual(TEXT("both ran SequenceDone's OnEndSequence"),
		ElysiumEntityDebugTest::CounterValue(F.World.FindByName(TEXT("c"))), 11.f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Procedural names and the radius (`0x100f7c30` tests whatever `0x100f7770` returns).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorProceduralRadiusTest,
	"Elysium.Arm.NpcKernelDirector.ProceduralRadius", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorProceduralRadiusTest::RunTest(const FString&)
{
	const float U = ElysiumMove::U;
	FElysiumNpcWorldBuilder Builder(TEXT("director_procedural"), 5314);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"), FVector(500.f * U, 0.f, 0.f));
	Builder.AddCounter(TEXT("begun"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("seq"), TEXT("!activator")).Keys.Add(
		TEXT("m_flRadius"), TEXT("64"));
	Builder.WireOutput(TEXT("seq"), TEXT("OnBeginSequence"), TEXT("begun"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* Seq = DirectorAs(F.World, TEXT("seq"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("seq"), Seq))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	auto BeginFrom = [&F, Seq](const FElysiumEntityHandle& Activator)
	{
		F.World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			Activator, Seq->Handle);
	};
	// `!activator` resolves to jack (the searching director's own `m_hLastInputActivator`), but jack
	// stands 500 units away: outside `m_flRadius` 64, so not found.
	BeginFrom(Jack->Handle);
	F.Advance(0.2);
	TestFalse(TEXT("a procedural hit outside m_flRadius is not found"), Jack->ScriptOwner.IsSet());
	TestEqual(TEXT("so no beat begins"), ElysiumEntityDebugTest::CounterValue(F.World.FindByName(TEXT("begun"))), 0.f);
	Jack->SetRuntimeOrigin(FVector(10.f * U, 0.f, 0.f));
	BeginFrom(Jack->Handle);
	F.Advance(F.World.NowSeconds() + 0.2);
	TestTrue(TEXT("inside it, the same name possesses the NPC"), OwnedBy(Jack, Seq));
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// -------------------------------------------------------------------------------------------------
// Open question 8.1: directors stand in the Troika Look walk (retail's AI list), and `BestEnemy`'s
// `m_bIsBCCTargetable` gate keeps them out of enemy choice.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDirectorLookWalkTest,
	"Elysium.Substrate.NpcKernelDirector.LookWalk", GDirectorTestFlags)
bool FElysiumNpcKernelDirectorLookWalkTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("director_look"), 5315);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"));
	AddDirector(Builder, TEXT("scripted_sequence"), TEXT("seq"), TEXT("nobody"), FVector(100.f, 0.f, 0.f));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* Seq = DirectorAs(F.World, TEXT("seq"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("seq"), Seq))
	{
		return false;
	}
	// The Look walk's NPC channel admits any `+0x94` entity (`ElysiumNpcSenses.cpp`, the
	// `g_AI_Manager` list the director's constructor joins, `0x1027c300`)...
	TestNotNull(TEXT("a director is a +0x94 CAI_BaseNPC the Look walk admits"), Seq->AsNpcBase());
	TestNull(TEXT("but not a Troika NPC"), Seq->AsNpc());
	// ...and `BestEnemy` (`0x10274451`) refuses a clear `m_bIsBCCTargetable`, which `Spawn` stores.
	TestFalse(TEXT("a director is never BCC-targetable"), FElysiumNpcBase::IsBccTargetable(*Seq));
	TestTrue(TEXT("an ordinary NPC is"), FElysiumNpcBase::IsBccTargetable(*Jack));
	FElysiumNpcWorldFixture::Wake({ Jack }, F.World.NowSeconds());
	F.Advance(F.World.NowSeconds() + 2.0);
	TestFalse(TEXT("so a sensing NPC never commits a director as its enemy"),
		Jack->BaseMemory.Enemy == Seq->Handle);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
