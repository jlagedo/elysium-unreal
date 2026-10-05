#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29c-1, family **Schedule** — one case per sub-family, every species row exercised BY NAME.
//
// The assertions are the decompiled C's: the arm order, the thresholds, the schedule numbers and
// what each body writes. Where a body can only answer "nothing" because its input is a seam, the
// case says so — that the seam is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelScheduleFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// -------------------------------------------------------------------------------------------------
// Slot 580 `GetClassScheduleIdSpace` — the species bodies on classes with an instance, plus the
// Troika line.
// -------------------------------------------------------------------------------------------------

namespace
{
	// A recovered-fact table's row by retail class name (test-side; the runtime asks the corpus).
	template <typename TRow>
	const TRow* ScheduleRowOf(const TRow* (*Rows)(int32&), const TCHAR* RetailClass)
	{
		int32 Count = 0;
		const TRow* All = Rows(Count);
		for (int32 Index = 0; Index < Count && RetailClass != nullptr; ++Index)
		{
			if (FCString::Strcmp(All[Index].RetailClass, RetailClass) == 0)
			{
				return &All[Index];
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleIdSpaceTest,
	"Elysium.Substrate.NpcKernelSchedule.ClassScheduleIdSpace", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleIdSpaceTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FScheduleIdSpace* Rows = FElysiumNpc::ScheduleIdSpaceRows(Count);
	// Thirteen until story 5 fold A2 dropped the controller line's two rows (each of the three
	// classes answers its own space through the corpus, keyed on its C++ class).
	TestEqual(TEXT("the slot-580 table carries eleven rows"), Count, 11);

	// Every row by name: the class resolves in the census, the census agrees the row's body fills
	// slot 580 for it, and the row carries an id-space global.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpc::FScheduleIdSpace& Row = Rows[Index];
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row.RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.RetailClass), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 580 with %s"), Row.RetailClass, Row.Body),
				FString(ElysiumNpcTestCensus::BodyOf(Cls, 580)), FString(Row.Body));
		}
		TestEqual(*FString::Printf(TEXT("%s is the row the lookup answers"), Row.RetailClass),
			reinterpret_cast<UPTRINT>(ScheduleRowOf(FElysiumNpc::ScheduleIdSpaceRows, Row.RetailClass)),
			reinterpret_cast<UPTRINT>(&Row));
	}

	TestNull(TEXT("a class with no row answers nothing"),
		ScheduleRowOf(FElysiumNpc::ScheduleIdSpaceRows, TEXT("CNotAClass")));
	// `CNPC_VCombatman` and `CNPC_VGangrel` override slot 580 in retail but have no instance
	// (0019 story 5 step 1): their census rows stand, their table rows do not.
	TestNull(TEXT("CNPC_VCombatman, a class with no instance, has no row"),
		ScheduleRowOf(FElysiumNpc::ScheduleIdSpaceRows, TEXT("CNPC_VCombatman")));
	TestNull(TEXT("nor does CNPC_VGangrel"), ScheduleRowOf(FElysiumNpc::ScheduleIdSpaceRows, TEXT("CNPC_VGangrel")));

	// The two classes that SHARE a body, which is why the table has twelve species rows and ten
	// species bodies.
	TestEqual(TEXT("CNPC_VCameraSecurity shares CNPC_VCamera's 0x103683b0"),
		FString(ScheduleRowOf(FElysiumNpc::ScheduleIdSpaceRows, TEXT("CNPC_VCameraSecurity"))->Body),
		FString(TEXT("0x103683b0")));
	// The controller line has no table row since fold A2; the census and the corpus carry it.
	TestNull(TEXT("CNPC_VPlayerController has no table row"),
		ScheduleRowOf(FElysiumNpc::ScheduleIdSpaceRows, TEXT("CNPC_VPlayerController")));
	TestEqual(TEXT("CNPC_VPlayerController shares CNPC_VVampire's 0x103750e0 in the census"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VPlayerController")), 580)),
		FString(TEXT("0x103750e0")));
	TestEqual(TEXT("CNPC_VWolfMorph fills slot 580 with its own 0x103dc750"),
		FString(ElysiumNpcTestCensus::BodyOf(ElysiumNpcTestCensus::Find(TEXT("CNPC_VWolfMorph")), 580)),
		FString(TEXT("0x103dc750")));

	// --- The LIVE spaces, which are the corpus's ------------------------------------------------
	//
	// Every assertion below used to read the other way round: the ranges were the empty state the
	// static constructor left, `ScheduleLocalToGlobal` answered -1 for every id, and the test said
	// so. 0019/3 loads the classes' own schedule texts, so the ranges are real and so is the walk.
	FElysiumScheduleCorpus& Corpus = FElysiumScheduleCorpus::Get();
	Corpus.EnsureLoaded();

	const FElysiumLocalIdSpace* Brujah =
		Corpus.SpaceFor(TEXT("CNPC_VBrujah"), EElysiumIdCategory::Schedule);
	if (!TestNotNull(TEXT("CNPC_VBrujah has a loaded schedule space"), Brujah))
	{
		return false;
	}
	TestEqual(TEXT("ScheduleLocalToGlobal(-1) is -1"),
		FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, INDEX_NONE), INDEX_NONE);

	// `CNPC_VBrujah::InitCustomSchedules` (`0x10367a40`) registers exactly two names,
	// `SCHED_VBRUJAH_WALK` 0x158 and `SCHED_VBRUJAH_WATCH` 0x159, into `DAT_1093a740` under
	// `CNPC_VVampire`'s space as the parent.
	const int32 Walk = FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, 0x158);
	const int32 Watch = FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, 0x159);
	TestTrue(TEXT("SCHED_VBRUJAH_WALK translates"), (Walk) != INDEX_NONE);
	TestEqual(TEXT("...and SCHED_VBRUJAH_WATCH is the next global id"), Watch, Walk + 1);
	TestEqual(TEXT("the round trip is the identity"), Brujah->GlobalToLocal(Walk), 0x158);
	TestEqual(TEXT("an id this class never registered is -1"),
		FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, 0x15a), INDEX_NONE);

	// The parent walk, which is the structural rule the id spaces have: a Brujah reaches its base
	// class's programs and its grandparent's, because `LocalToGlobal` falls through `m_pParent`.
	TestTrue(TEXT("a Brujah reaches CNPC_VVampire's SCHED_VVAMPIRE_IDLE_STAND (0x157)"), (FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, 0x157)) != INDEX_NONE);
	TestTrue(TEXT("...and the Troika line's SCHED_TROIKA_IDLE_DISPOSITION (0x6b)"), (FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION)) != INDEX_NONE);
	TestTrue(TEXT("...and the base's FAIL (0x43)"), (FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, ElysiumSched::FAIL)) != INDEX_NONE);
	TestEqual(TEXT("the names agree with the numbers"),
		FString(ElysiumScheduleName(
			FElysiumNpcBase::ScheduleLocalToGlobal(Brujah, ElysiumSched::FAIL))),
		FString(TEXT("FAIL")));

	// **`npc_VCop` builds `CNPC_VCop`.** `CNPC_VCop` is a census class with a slot-580 body of its
	// own (`0x10370930` -> `DAT_1093ac60`, asserted by name above), and retail's factory for the
	// entity classname `npc_VCop` builds it (`docs/vtmb/npc-ai/population.md`, "The classname →
	// class map, read from the factories"). The census used to carry an empty classname list for it,
	// so a spawned cop resolved to no class; story 5 step 2 reads the classnames off the factories.
	TestEqual(TEXT("npc_VCop is claimed by CNPC_VCop"),
		ElysiumNpcTestCensus::OfClassname(TEXT("npc_VCop")),
		ElysiumNpcTestCensus::Find(TEXT("CNPC_VCop")));
	TestNotNull(TEXT("CNPC_VCop is a census class"),
		ElysiumNpcTestCensus::Find(TEXT("CNPC_VCop")));

	// The leaf's own answer, through a spawned entity.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_idspace"), 4101);
	Builder.AddNpc(TEXT("vampire"), FVector::ZeroVector, TEXT("npc_VVampire"));
	Builder.AddNpc(TEXT("combatant"), FVector(200.0, 0.0, 0.0), TEXT("npc_VHumanCombatant"));
	Builder.AddNpc(TEXT("cop"), FVector(400.0, 0.0, 0.0), TEXT("npc_VCop"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Vampire = Fixture.Npc(TEXT("vampire"));
	FElysiumNpc* Combatant = Fixture.Npc(TEXT("combatant"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpcWorldFixture::Quiet({ Vampire, Combatant, Cop });
	if (Vampire == nullptr || Combatant == nullptr || Cop == nullptr)
	{
		return false;
	}
	const FElysiumLocalIdSpace* const Troika =
		Corpus.SpaceFor(TEXT("CAI_BaseNPCTroika"), EElysiumIdCategory::Schedule);
	TestEqual(TEXT("npc_VVampire answers CNPC_VVampire's own space"),
		Vampire->ClassScheduleIdSpace(),
		Corpus.SpaceFor(TEXT("CNPC_VVampire"), EElysiumIdCategory::Schedule));
	TestTrue(TEXT("...which is NOT the Troika line's"),
		Vampire->ClassScheduleIdSpace() != Troika);
	// `CNPC_VHumanCombatant` has no slot-580 override in the census -- the rule this test used to
	// assert gave it the Troika line. The CORPUS places it in its own unit anyway: the class -> space
	// map is the sidecar's, recovered from the image's own RTTI walk over slot 580, and it covers 77
	// classes against 58 spaces. So the space is its own and it still reaches the Troika programs
	// through the parent chain, which is the property that matters.
	TestEqual(TEXT("a class the corpus places gets ITS space, not the line's"),
		Combatant->ClassScheduleIdSpace(),
		Corpus.SpaceFor(TEXT("CNPC_VHumanCombatant"), EElysiumIdCategory::Schedule));
	TestTrue(TEXT("...and reaches the Troika line's programs through the parent chain"),
		(Combatant->ClassScheduleIdSpace()->LocalToGlobal(
			ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION)) != INDEX_NONE);
	TestEqual(TEXT("a spawned npc_VCop is CNPC_VCop"), Cop->RetailClass(),
		ElysiumNpcTestCensus::Find(TEXT("CNPC_VCop")));
	// Story 5 step 2 (retail correction): a placed cop used to take the Troika line's space, the
	// census giving `CNPC_VCop` no classname. Retail's cop runs in `CNPC_VCop`'s own space.
	TestNotNull(TEXT("the corpus places CNPC_VCop"),
		Corpus.SpaceFor(TEXT("CNPC_VCop"), EElysiumIdCategory::Schedule));
	TestEqual(TEXT("so a spawned npc_VCop answers CNPC_VCop's space, not the line's"),
		Cop->ClassScheduleIdSpace(), Corpus.SpaceFor(TEXT("CNPC_VCop"), EElysiumIdCategory::Schedule));
	TestTrue(TEXT("which is not the Troika line's space"), Cop->ClassScheduleIdSpace() != Troika);

	return true;
}

// -------------------------------------------------------------------------------------------------
// The schedule-change door, the task surface and the host's three rows.
// -------------------------------------------------------------------------------------------------

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleTaskSurfaceTest,
	"Elysium.Arm.NpcKernelSchedule.TaskSurface", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleTaskSurfaceTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_tasks"), 4103);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcWorldFixture::Quiet({ Guard });
	if (Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);

	// --- `0x10280de0`'s stamp arms --------------------------------------------------------------
	TestEqual(TEXT("an id at or above 1,000,000,000 is stamped unchanged"),
		Guard->ResolveIdealScheduleStamp(1000000000), 1000000000);
	TestEqual(TEXT("and one above it too"),
		Guard->ResolveIdealScheduleStamp(1000000001), 1000000001);
	TestEqual(TEXT("-1 goes through the id space and answers -1"),
		Guard->ResolveIdealScheduleStamp(INDEX_NONE), INDEX_NONE);
	// This used to answer -1 too, and the test called that "the empty-id-space seam". The seam is
	// closed: a local number now translates to the global id the owning class registered.
	TestEqual(TEXT("a loaded program's own number translates through the class space"),
		Guard->ResolveIdealScheduleStamp(ElysiumSched::IDLE_STAND),
		ElysiumScheduleGlobalId(ElysiumSched::IDLE_STAND));

	// `ChangeSchedule` installs through the existing kernel and stamps `m_IdealSchedule`, which is
	// what retail does and what this runtime could not do while every translation answered -1.
	Guard->ChangeSchedule(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestEqual(TEXT("ChangeSchedule installs the program"), Guard->Schedule.Current,
		ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION));
	TestEqual(TEXT("and stamps m_IdealSchedule with its GLOBAL id"),
		Guard->BaseScheduleHost.IdealScheduleRetail,
		ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION));

	// --- `0x10280db0` `IsTaskIndexCurrent` and `0x10280f40` `NextScheduledTask` -------------------
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION));
	TestNotNull(TEXT("IDLE_DISPOSITION is registered"), Program);
	if (Program == nullptr)
	{
		return false;
	}
	const int32 TaskCount = Program->Tasks.Num();
	TestTrue(TEXT("and carries at least one task"), TaskCount >= 1);

	Guard->Schedule.TaskIndex = 0;
	TestEqual(TEXT("task 0 of a multi-task program is not the end"),
		FElysiumNpcScheduleHost::IsTaskIndexCurrent(Guard->Schedule), TaskCount == 0);
	Guard->Schedule.TaskIndex = TaskCount;
	TestTrue(TEXT("the index at the task count IS the end"),
		FElysiumNpcScheduleHost::IsTaskIndexCurrent(Guard->Schedule));
	{
		FElysiumScheduleState Empty;
		TestFalse(TEXT("with no program installed there is no task count to compare"),
			FElysiumNpcScheduleHost::IsTaskIndexCurrent(Empty));
	}

	Guard->Cognition.Conditions.Reset();
	Guard->Schedule.TaskIndex = TaskCount - 1;
	Guard->Schedule.TaskStatus = EElysiumTaskStatus::Running;
	Guard->BaseScheduleHost.FailedSchedule = ElysiumSched::FAIL;
	Guard->BaseScheduleHost.InterruptSchedule = ElysiumSched::FAIL;
	Guard->NextScheduledTask();
	TestEqual(TEXT("NextScheduledTask advances the index"), Guard->Schedule.TaskIndex, TaskCount);
	// Representation update: `10280f40 MOV [this+0x5c44],0` is the full status word, not the old
	// `bTaskStarted` approximation.
	TestEqual(TEXT("and clears the task status"), Guard->Schedule.TaskStatus,
		EElysiumTaskStatus::New);
	TestTrue(TEXT("exhausting the program raises COND_SCHEDULE_DONE"),
		Guard->Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone));
	TestEqual(TEXT("and zeroes m_failedSchedule"), Guard->BaseScheduleHost.FailedSchedule,
		ElysiumScheduleId::None);
	TestEqual(TEXT("and m_interuptSchedule"), Guard->BaseScheduleHost.InterruptSchedule,
		ElysiumScheduleId::None);

	Guard->Cognition.Conditions.Reset();
	Guard->Schedule.TaskIndex = 0;
	Guard->NextScheduledTask();
	TestEqual(TEXT("an ordinary advance lands on task 1"), Guard->Schedule.TaskIndex, 1);
	TestEqual(TEXT("and raises SCHEDULE_DONE only when the program is exhausted"),
		Guard->Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone), TaskCount == 1);

	// --- `0x10273e80` `TaskComplete` and the two `CAI_Motor` forwards ----------------------------
	Guard->Cognition.Conditions.Reset();
	Guard->Schedule.TaskStatus = EElysiumTaskStatus::Running;
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
	Guard->TaskComplete(/*bIgnoreTaskFailed=*/false);
	// `10273e98 MOV [this+0x5c44],4` is reached only after the `COND_TASK_FAILED` refusal.
	TestFalse(TEXT("TaskComplete(false) refuses to overwrite an already-failed task"),
		Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete);
	Guard->TaskComplete(/*bIgnoreTaskFailed=*/true);
	TestTrue(TEXT("TaskComplete(true) writes the status regardless"),
		Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete);
	Guard->Cognition.Conditions.Reset();
	Guard->Schedule.TaskStatus = EElysiumTaskStatus::Running;
	Guard->MotorTaskComplete(/*bIgnoreTaskFailed=*/false);
	TestTrue(TEXT("the motor's slot 2 reaches the same body through the owner back-pointer"),
		Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete);

	Guard->Cognition.Conditions.Reset();
	Guard->BaseScheduleHost.FailureReason = 0;
	Guard->MotorTaskFail(0x1b);
	TestEqual(TEXT("the motor's slot 1 is the owner's slot 448, unchanged"),
		Guard->BaseScheduleHost.FailureReason, 0x1b);
	TestTrue(TEXT("and raises COND_TASK_FAILED"),
		Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// --- `0x102a18a0` `WaitFinished` --------------------------------------------------------------
	Guard->BaseScheduleHost.SetWaitFinished(2.5f, 10.0);
	TestEqual(TEXT("a positive operand stamps curtime + operand"), Guard->BaseScheduleHost.WaitFinished,
		12.5, 1e-6);
	Guard->BaseScheduleHost.SetWaitFinished(0.0f, 10.0);
	TestEqual(TEXT("a zero operand takes the retail default instead (`_DAT_10447ee0` = 1000.0)"),
		Guard->BaseScheduleHost.WaitFinished, 10.0 + ElysiumNpcTunables::Thousand, 1e-6);
	Guard->BaseScheduleHost.SetWaitFinished(-3.0f, 10.0);
	TestEqual(TEXT("and so does a negative one — not a same-frame deadline"),
		Guard->BaseScheduleHost.WaitFinished, 10.0 + ElysiumNpcTunables::Thousand, 1e-6);

	// --- `0x1027db30` -----------------------------------------------------------------------------
	TestFalse(TEXT("and a negative one is retail's own refusal"), Guard->IsUnusableNodeIndex(-1));

	return true;
}

// -------------------------------------------------------------------------------------------------
// The pre-selector: the base body and its three species overrides.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSchedulePreSelectTest,
	"Elysium.Arm.NpcKernelSchedule.PreSelectSchedule", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelSchedulePreSelectTest::RunTest(const FString&)
{
	// **None of slot 437's three species classes has a registered entity classname here** —
	// `ElysiumNpcClasses.cpp` registers fourteen `npc_V*` leaves and `npc_VCamera`,
	// `npc_VMingXiaoTentacle` and `npc_VPlaceholder` are not among them. The three rows are
	// therefore exercised by retail class name through the census, and the port method's
	// fall-through is driven on a spawned NPC.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_preselect"), 4104);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcWorldFixture::Quiet({ Guard });
	if (Guard == nullptr)
	{
		return false;
	}

	// `0x1028a2a0`, arm by arm, in the corrected order: the gravity write, then NPC_FREEZE, then
	// ON_FIRE, then FLOATING_OFF_GROUND.
	Guard->Cognition.Conditions.Reset();
	TestEqual(TEXT("no condition is no opinion"), Guard->FElysiumNpcBase::PreSelectSchedule(), 0);

	Guard->Gravity = 0.0f;
	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::FloatingOffGround);
	TestEqual(TEXT("FLOATING_OFF_GROUND alone answers 0x3e FALL_TO_GROUND"),
		Guard->FElysiumNpcBase::PreSelectSchedule(), 0x3e);
	TestEqual(TEXT("and its first arm writes gravity 1.0 on the way past"), Guard->Gravity, 1.0f);

	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::FloatingOffGround);
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::NpcFreeze);
	TestEqual(TEXT("NPC_FREEZE outranks it and answers 0x3a"), Guard->FElysiumNpcBase::PreSelectSchedule(), 0x3a);

	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::FloatingOffGround);
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	TestEqual(TEXT("ON_FIRE comes next and answers 0x151"), Guard->FElysiumNpcBase::PreSelectSchedule(), 0x151);

	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::NpcFreeze);
	TestEqual(TEXT("and NPC_FREEZE outranks ON_FIRE"), Guard->FElysiumNpcBase::PreSelectSchedule(), 0x3a);

	// The three species bodies, by census row.
	TestEqual(TEXT("CNPC_VCamera fills slot 437 with 0x10368f20"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VCamera")), 437)),
		FString(TEXT("0x10368f20")));
	TestEqual(TEXT("CNPC_VCameraSecurity shares it"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VCameraSecurity")), 437)),
		FString(TEXT("0x10368f20")));
	TestEqual(TEXT("CNPC_VMingXiaoTentacle fills it with 0x1039de00"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VMingXiaoTentacle")), 437)),
		FString(TEXT("0x1039de00")));
	TestEqual(TEXT("CNPC_VPlaceholder fills it with 0x103a43f0"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VPlaceholder")), 437)),
		FString(TEXT("0x103a43f0")));

	// Through the slot: each class's `PreSelectSchedule` override is its body (story 5 step 3).
	FElysiumNpcWorldBuilder SpeciesBuilder(TEXT("npc_kernel_schedule_pre_select_species"), 4106);
	SpeciesBuilder.AddNpc(TEXT("camera"), FVector::ZeroVector, TEXT("npc_VCamera"));
	SpeciesBuilder.AddNpc(TEXT("tentacle"), FVector(200.0, 0.0, 0.0), TEXT("npc_VMingXiaoTentacle"));
	SpeciesBuilder.AddNpc(TEXT("placeholder"), FVector(400.0, 0.0, 0.0), TEXT("npc_VPlaceholder"));
	FElysiumNpcWorldFixture Species(MoveTemp(SpeciesBuilder));
	FElysiumNpcWorldFixture::Quiet({ Species.Npc(TEXT("camera")), Species.Npc(TEXT("tentacle")),
		Species.Npc(TEXT("placeholder")) });
	if (FElysiumNpc* Camera = Species.Npc(TEXT("camera")))
	{
		TestEqual(TEXT("a camera's slot 437 answers 0x156"), Camera->PreSelectSchedule(), 0x156);
	}
	if (FElysiumNpc* Tentacle = Species.Npc(TEXT("tentacle")))
	{
		TestEqual(TEXT("a tentacle's answers 0"), Tentacle->PreSelectSchedule(), 0);
	}
	if (FElysiumNpc* Placeholder = Species.Npc(TEXT("placeholder")))
	{
		TestEqual(TEXT("a placeholder's answers 0x157"), Placeholder->PreSelectSchedule(), 0x157);
	}

	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 438's species half.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleSpeciesSelectTest,
	"Elysium.Arm.NpcKernelSchedule.SpeciesSelectSchedule", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleSpeciesSelectTest::RunTest(const FString&)
{
	// The five bodies, by census row — three of the classes have no registered classname here.
	struct FExpect { const TCHAR* Class; const TCHAR* Body; };
	static const FExpect Expected[] = {
		{ TEXT("CNPC_VAndreiBlood"), TEXT("0x1035d010") },
		{ TEXT("CNPC_VCamera"), TEXT("0x10368f40") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x10368f40") },
		{ TEXT("CNPC_VManBat"), TEXT("0x1038e340") },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039de20") },
		{ TEXT("CNPC_VPlaceholder"), TEXT("0x103a4410") },
	};
	for (const FExpect& Row : Expected)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row.Class);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.Class), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 438 with %s"), Row.Class, Row.Body),
				FString(ElysiumNpcTestCensus::BodyOf(Cls, 438)), FString(Row.Body));
		}
	}

	// `npc_VAndreiBlood` builds `CNPC_VAndreiBlood`, whose `SpeciesSelectSchedule` override is its
	// replacement selector (story 5 step 3).
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_species_select"), 4105);
	Builder.AddNpc(TEXT("andrei"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	Builder.AddNpc(TEXT("guard"), FVector(400.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Andrei = Fixture.Npc(TEXT("andrei"));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcWorldFixture::Quiet({ Andrei, Guard });
	if (Andrei == nullptr || Guard == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("npc_VAndreiBlood resolves to CNPC_VAndreiBlood, not its CNPC_VVampireBoss base"),
		FString(Andrei->RetailClass() != nullptr ? Andrei->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VAndreiBlood")));
	// CNPC_VAndreiBlood's ladder ends on the `0x1035e920` split, which family **Species** landed:
	// `return m_iActiveRunnerCount (+0x66b8) < 2.0` — `_DAT_10452dc4` is 2.0f, and the rest of the
	// same budget is `CNPCMaker_Fleshpile::MakeNPC` (`0x1034c2d0`, refusing at `2 <= count` and
	// adding 1) and its `DeathNotice` (`0x1034c8e0`, subtracting 1). So the fleshpile may have at
	// most TWO runners alive, the gate ADMITS while it is under the cap, and BOTH arms of the split
	// are driven here rather than whichever one a stub happened to answer.
	Andrei->ActiveRunnerCount = 0;
	TestEqual(TEXT("with no runner alive the 0x1035e920 gate admits and the ladder answers 0x15d"),
		Andrei->SpeciesSelectSchedule(), 0x15d);
	Andrei->ActiveRunnerCount = 1;
	TestEqual(TEXT("one runner is still under the 2.0 cap, so it still answers 0x15d"),
		Andrei->SpeciesSelectSchedule(), 0x15d);
	Andrei->ActiveRunnerCount = 2;
	TestEqual(TEXT("at the cap the gate refuses and the ladder answers 0x15c"),
		Andrei->SpeciesSelectSchedule(), 0x15c);
	Andrei->ActiveRunnerCount = 3;
	TestEqual(TEXT("and past it too — retail's test is `count < 2.0`, not `count != 2`"),
		Andrei->SpeciesSelectSchedule(), 0x15c);
	Andrei->ActiveRunnerCount = 0;
	// Story 8 L06 integration (corrected to retail): there is no "no opinion" slot 438. A class
	// with no species body inherits `CAI_BaseNPCTroika::SelectSchedule` `0x102af660`, whose idle
	// case ends at `0x102af8dc` IDLE_DISPOSITION.
	TestEqual(TEXT("a class with no slot-438 species body runs the Troika selector (0x102af8dc)"),
		Guard->SpeciesSelectSchedule(), ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	FElysiumNpcWorldBuilder CameraBuilder(TEXT("npc_kernel_schedule_select_camera"), 4107);
	CameraBuilder.AddNpc(TEXT("camera"), FVector::ZeroVector, TEXT("npc_VCamera"));
	FElysiumNpcWorldFixture CameraWorld(MoveTemp(CameraBuilder));
	FElysiumNpcWorldFixture::Quiet({ CameraWorld.Npc(TEXT("camera")) });
	if (FElysiumNpc* Camera = CameraWorld.Npc(TEXT("camera")))
	{
		TestEqual(TEXT("a camera's slot-438 body answers 0x156"), Camera->SpeciesSelectSchedule(), 0x156);
	}

	// The species numbers ARE programs now, and that is 0019/3's whole effect on this selector.
	// `ScheduleFromRetailNumber` -- the old fold from a retail number to one of 29 typed identities,
	// which answered `None` for 0x156, 0x15c and 0x15d alike -- is gone with the identities it
	// folded onto: a number a recovered body answers is the answer.
	const FElysiumLocalIdSpace* const AndreiSpace = Andrei->ClassScheduleIdSpace();
	if (TestNotNull(TEXT("CNPC_VAndreiBlood has a loaded schedule space"), AndreiSpace))
	{
		TestTrue(TEXT("0x15c is a loaded program"), (AndreiSpace->LocalToGlobal(0x15c)) != INDEX_NONE);
		TestTrue(TEXT("and so is 0x15d, the other side of the runner-budget gate"), (AndreiSpace->LocalToGlobal(0x15d)) != INDEX_NONE);
		TestTrue(TEXT("and 0x6b IDLE_DISPOSITION, through the parent chain"), (AndreiSpace->LocalToGlobal(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION)) != INDEX_NONE);
	}

	// The composition still holds: the species answer is installed rather than folded away.
	Andrei->Cognition.Conditions.Reset();
	TestNotEqual(TEXT("SelectSchedule answers the species number"),
		Andrei->SelectSchedule(), ElysiumScheduleId::None);

	return true;
}

// -------------------------------------------------------------------------------------------------
// The melee failure gate and slot 604.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleMeleeTest,
	"Elysium.Arm.NpcKernelSchedule.SelectScheduleMeleeCombat", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleMeleeTest::RunTest(const FString&)
{
	// The six bodies that fill slot 604, by census row. Three of the five species classes have no
	// registered classname here, so the row is exercised by name through the census.
	struct FExpect { const TCHAR* Class; const TCHAR* Body; };
	static const FExpect Expected[] = {
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x102b6c30") },
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10361be0") },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036d800") },
		{ TEXT("CNPC_VChangBrosBlade"), TEXT("0x1036d800") },
		{ TEXT("CNPC_VChangBrosClaw"), TEXT("0x1036d800") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103aa060") },
		{ TEXT("CNPC_VSheriffMan"), TEXT("0x103af960") },
		{ TEXT("CNPC_VTzimisceRunner"), TEXT("0x103c4430") },
	};
	for (const FExpect& Row : Expected)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row.Class);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.Class), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 604 with %s"), Row.Class, Row.Body),
				FString(ElysiumNpcTestCensus::BodyOf(Cls, 604)), FString(Row.Body));
		}
	}

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_melee"), 4106);
	Builder.AddTroikaNpc(TEXT("troika"));
	Builder.AddNpc(TEXT("leader"), FVector(200.0, 0.0, 0.0), TEXT("npc_VSabbatLeader"));
	Builder.AddNpc(TEXT("runner"), FVector(400.0, 0.0, 0.0), TEXT("npc_VTzimisceRunner"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	FElysiumNpc* Runner = Fixture.Npc(TEXT("runner"));
	FElysiumNpcWorldFixture::Quiet({ Troika, Leader, Runner });
	if (Troika == nullptr || Leader == nullptr || Runner == nullptr)
	{
		return false;
	}
	// This case walks the selectors' arms with the coordinator REFUSING. Since spec 0002 V11-1 the
	// coordinator is an object and a spawned NPC binds "Normal"; index 0 (no object, retail's fault
	// path) is the state in which slot 599 / 600 refuse and slot 602's Troika line declines. The
	// admitting path is `Elysium.Arm.AttackCoordinator.*` and `NpcKernelTroikaHelpers.EnterMelee`.
	Troika->AttackCoordinator = 0;
	Leader->AttackCoordinator = 0;
	Runner->AttackCoordinator = 0;

	// --- `0x102b6fe0`, the gate, on its own ------------------------------------------------------
	Troika->Cognition.Conditions.Reset();
	TestEqual(TEXT("neither condition is no opinion"),
		Troika->MeleeScheduleFailureGate(nullptr), 0);
	Troika->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("ENEMY_OCCLUDED without a ranged weapon answers 0xcd"),
		Troika->MeleeScheduleFailureGate(nullptr), 0xcd);
	Troika->Cognition.Conditions.Reset();
	Troika->Cognition.Conditions.Set(EElysiumNpcCond::EnemyBlocked);
	TestEqual(TEXT("ENEMY_BLOCKED is the fallback term and answers 0xce"),
		Troika->MeleeScheduleFailureGate(nullptr), 0xce);
	Troika->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("and ENEMY_OCCLUDED is tested first, so it wins"),
		Troika->MeleeScheduleFailureGate(nullptr), 0xcd);

	// --- the Troika line, `0x102b6c30` ------------------------------------------------------------
	//
	// **STRENGTHENED by story 29d, family SpeciesAnim10.** `CNPC_VHumanCombatant` fills slot 604
	// with `0x10385e40` — the `CNPC_VHuman` selector 34 census classes share — so a combatant leaf
	// never reaches the Troika line. The fixture therefore stands this body as the bare Troika NPC
	// (`AddTroikaNpc`, story 5 step 2), which carries no slot-604 override row and is the one class
	// whose lookup answers "the Troika body", exactly as it did when slot 604's species arms were
	// unwritten.
	// Not in melee and slot 599 refusing is the first arm: the gate, then 0xe4.
	Troika->Cognition.Conditions.Reset();
	Troika->bInMelee = false;
	TestEqual(TEXT("out of melee with the gate silent, the Troika line answers 0xe4"),
		Troika->SelectScheduleMeleeCombat(0), 0xe4);
	Troika->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("and the gate pre-empts it with 0xcd"),
		Troika->SelectScheduleMeleeCombat(0), 0xcd);

	// In melee, slot 602 (`0x102b5900`, family TroikaHelpers) declines to leave — its third line
	// refuses on a null `m_pAttackCoordinator` (index 0, set above) — so the body falls through to
	// the shared tail rather than taking the leave-melee branch.
	Troika->Cognition.Conditions.Reset();
	Troika->bInMelee = true;
	Troika->Cognition.Conditions.Set(EElysiumNpcCond::CanMeleeAttack1);
	TestEqual(TEXT("CAN_MELEE_ATTACK1 without a ranged weapon answers 0xdd"),
		Troika->SelectScheduleMeleeCombat(0), 0xdd);
	TestTrue(TEXT("and that arm leaves the NPC in melee — it never dispatches slot 601"),
		Troika->bInMelee);

	Troika->Cognition.Conditions.Reset();
	Troika->ScheduleHost.EnemyHeightDiffUnits = 0.f;
	Troika->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestEqual(TEXT("ENEMY_UNREACHABLE without a ranged weapon answers 0x17 TAKE_COVER_FROM_ENEMY"),
		Troika->SelectScheduleMeleeCombat(0), 0x17);
	// The 0x17 arm dispatches slot 601 (`0x102b5880`) on the way out, whose second line is
	// `m_bInMelee = 0` — so retail leaves melee here and the next selection genuinely re-enters
	// through slot 599. The two tail cases below are IN-MELEE cases, so the state is put back
	// rather than inherited from the arm that just cleared it.
	TestFalse(TEXT("and slot 601 cleared m_bInMelee on the way out (0x102b5880 line 2)"),
		Troika->bInMelee);

	Troika->bInMelee = true;
	Troika->Cognition.Conditions.Reset();
	TestEqual(TEXT("neither TOO_FAR term answers 199"), Troika->SelectScheduleMeleeCombat(0), 199);
	Troika->Cognition.Conditions.Set(EElysiumNpcCond::TooFarToAttack);
	TestEqual(TEXT("TOO_FAR_TO_ATTACK without a ranged weapon answers 0xcb"),
		Troika->SelectScheduleMeleeCombat(0), 0xcb);
	TestTrue(TEXT("neither tail arm dispatches slot 601, so melee is still held"), Troika->bInMelee);

	// --- CNPC_VSabbatLeader, `0x103aa060` ---------------------------------------------------------
	Leader->Cognition.Conditions.Reset();
	Leader->bInMelee = false;
	Leader->ScheduleHost.EnemyDistUnits = 5000.f;
	TestEqual(TEXT("the leader's out-of-melee far arm answers 0xe7"),
		Leader->SelectScheduleMeleeCombat(0), 0xe7);
	Leader->ScheduleHost.EnemyDistUnits = 0.f;
	TestEqual(TEXT("and its near arm (dist <= 2 * range) answers 0x15f"),
		Leader->SelectScheduleMeleeCombat(0), 0x15f);
	Leader->bInMelee = true;
	Leader->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("in melee, ENEMY_OCCLUDED answers 0xcd"),
		Leader->SelectScheduleMeleeCombat(0), 0xcd);
	Leader->Cognition.Conditions.Reset();
	Leader->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	Leader->ScheduleHost.EnemyHeightDiffUnits = 0.f;
	TestEqual(TEXT("and ENEMY_UNREACHABLE answers 0x15b, not the Troika line's 0x17"),
		Leader->SelectScheduleMeleeCombat(0), 0x15b);
	TestFalse(TEXT("the leader's 0x15b arm dispatches slot 601 too, so it leaves melee"),
		Leader->bInMelee);
	// `_DAT_104c3cd4` = 120 units: TOO_FAR_TO_ATTACK inside it still attacks, outside it closes.
	Leader->bInMelee = true;
	Leader->Cognition.Conditions.Reset();
	Leader->Cognition.Conditions.Set(EElysiumNpcCond::TooFarToAttack);
	Leader->ScheduleHost.EnemyDistUnits = 100.f;
	TestEqual(TEXT("TOO_FAR_TO_ATTACK under the leader's 120-unit bound answers 0xdd"),
		Leader->SelectScheduleMeleeCombat(0), 0xdd);
	Leader->ScheduleHost.EnemyDistUnits = 150.f;
	TestEqual(TEXT("and past it answers 0xcb"), Leader->SelectScheduleMeleeCombat(0), 0xcb);

	// --- CNPC_VTzimisceRunner, `0x103c4430` -------------------------------------------------------
	Runner->Cognition.Conditions.Reset();
	Runner->bInMelee = false;
	Runner->ScheduleHost.EnemyDistUnits = 5000.f;
	TestEqual(TEXT("the runner's out-of-melee far arm answers 0xe7"),
		Runner->SelectScheduleMeleeCombat(0), 0xe7);
	Runner->ScheduleHost.EnemyDistUnits = 100.f;   // inside range + the 200-unit margin
	TestEqual(TEXT("inside the melee range plus its 200-unit margin it answers 0xe4"),
		Runner->SelectScheduleMeleeCombat(0), 0xe4);
	Runner->bInMelee = true;
	Runner->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestEqual(TEXT("in melee, ENEMY_OCCLUDED answers 0xcd"),
		Runner->SelectScheduleMeleeCombat(0), 0xcd);
	Runner->Cognition.Conditions.Reset();
	Runner->Cognition.Conditions.Set(EElysiumNpcCond::CanMeleeAttack1);
	TestEqual(TEXT("CAN_MELEE_ATTACK1 answers 0xdd with no ranged split at all"),
		Runner->SelectScheduleMeleeCombat(0), 0xdd);
	Runner->Cognition.Conditions.Reset();
	Runner->ScheduleHost.EnemyHeightDiffUnits = 0.f;
	Runner->Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestEqual(TEXT("and ENEMY_UNREACHABLE answers 0x17, where CNPC_VChangBros answers 0x15d"),
		Runner->SelectScheduleMeleeCombat(0), 0x17);
	// A real divergence from the Troika line, and the reason the two bodies are ported arm for arm
	// rather than collapsed: `0x103c4430`'s `COND_ENEMY_UNREACHABLE` arm answers 0x17 WITHOUT
	// dispatching slot 601, where `0x102b6c30`'s identical answer does. The runner stays in melee.
	TestTrue(TEXT("but the runner's 0x17 arm does NOT dispatch slot 601, so it stays in melee"),
		Runner->bInMelee);

	return true;
}

// -------------------------------------------------------------------------------------------------
// slot 418 `ResolveTaskDistance`, `FlipFailureType`, `FixScriptNPCSchedule`, `0x102ae840`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleMiscTest,
	"Elysium.Arm.NpcKernelSchedule.TaskDistanceAndOrders", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleMiscTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_misc"), 4107);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("leader"), FVector(200.0, 0.0, 0.0), TEXT("npc_VSabbatLeader"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcSabbatLeader* Leader = Fixture.NpcAs<FElysiumNpcSabbatLeader>(TEXT("leader"));
	FElysiumNpcWorldFixture::Quiet({ Guard, Leader });
	if (Guard == nullptr || Leader == nullptr)
	{
		return false;
	}

	// --- slot 418's four-entry table --------------------------------------------------------------
	Guard->FollowerDistanceBackAway = 11.f;
	Guard->FollowerDistanceWalkTo = 22.f;
	Guard->FollowerDistanceRunTo = 33.f;
	// The jump table `0x102bf738`, dwords read from the image (`schedule-kernel.md` § "`0x102bf6e0`",
	// corrected 2026-09-21): 0 -> `0x102bf71b`, 1 -> `0x102bf711`, 2 -> `0x102bf707`, 3 -> `0x102bf6fd`.
	TestEqual(TEXT("-1000008 answers the fixed _DAT_1044e664 = 10.0 (0x102bf71b)"),
		Guard->ResolveTaskDistance(-1000008.f), 10.f);
	TestEqual(TEXT("-1000007 answers m_flFollowerDistanceRunTo +0x648c (0x102bf711)"),
		Guard->ResolveTaskDistance(-1000007.f), 33.f);
	TestEqual(TEXT("-1000006 answers m_flFollowerDistanceWalkTo +0x6488 (0x102bf707)"),
		Guard->ResolveTaskDistance(-1000006.f), 22.f);
	TestEqual(TEXT("-1000005 answers m_flFollowerDistanceBackAway +0x6484 (0x102bf6fd)"),
		Guard->ResolveTaskDistance(-1000005.f), 11.f);
	TestEqual(TEXT("an ordinary distance passes through AS PASSED (0x102702f1 FLD [ESP+8])"),
		Guard->ResolveTaskDistance(64.75f), 64.75f);
	// The two species sentinels, by census row — neither class has a registered classname here.
	TestEqual(TEXT("CNPC_VMingXiao fills slot 418 with 0x10392a10"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VMingXiao")), 418)),
		FString(TEXT("0x10392a10")));
	TestEqual(TEXT("CNPC_VTzimisce fills it with 0x103b9120"),
		FString(ElysiumNpcTestCensus::BodyOf(
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VTzimisce")), 418)),
		FString(TEXT("0x103b9120")));
	TestEqual(TEXT("a class with neither override still reads the Troika line's table at -1000004"),
		Guard->ResolveTaskDistance(-1000004.f), -1000004.f);

	// --- `0x103a9d00` -----------------------------------------------------------------------------
	TestEqual(TEXT("m_FailureType starts at 0"), Leader->FailureType, 0);
	Leader->FlipFailureType();
	TestEqual(TEXT("one flip makes it 1"), Leader->FailureType, 1);
	Leader->FlipFailureType();
	TestEqual(TEXT("and a second puts it back"), Leader->FailureType, 0);

	// `0x101a95d0` (`CCineAI::FixScriptNPCSchedule`) is the director's since story 5 fold A3:
	// `Elysium.Arm.NpcKernelDirector.AiFinishSchedule`.
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);

	// --- `0x102ae840` -----------------------------------------------------------------------------
	Guard->ScriptedScheduleOrder.Reset();
	Guard->AcceptScriptedScheduleOrder(9, /*bForce=*/false);
	TestEqual(TEXT("a live NPC records the order id at +0x65cc"),
		Guard->ScriptedScheduleOrder.RetailOrderId, 9);
	// `m_bForceStateChange` is private to `FElysiumNpcMind` (the shape map records it that way), so
	// the two observable writes of the same body stand for the triple here.
	TestTrue(TEXT("and sets CHOOSE_NEW_SCHEDULE"),
		Guard->NpcFlags.Has(EElysiumNpcFlag2::CHOOSE_NEW_SCHEDULE));
	TestTrue(TEXT("and the unnamed flags2 bit 31 retail ORs in beside it"),
		Guard->NpcFlags.HasRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31));

	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 453's species half, and `0x102b7690`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleTestBitsTest,
	"Elysium.Arm.NpcKernelSchedule.SpeciesBuildScheduleTestBits",
	GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleTestBitsTest::RunTest(const FString&)
{
	// The four bodies, by census row.
	struct FExpect { const TCHAR* Class; const TCHAR* Body; };
	static const FExpect Expected[] = {
		{ TEXT("CNPC_VGuard1"), TEXT("0x1037cdf0") },
		{ TEXT("CNPC_VHumanCombatant"), TEXT("0x10387520") },
		{ TEXT("CNPC_ProneDialog"), TEXT("0x10387520") },
		{ TEXT("CNPC_VCop"), TEXT("0x10387520") },
		{ TEXT("CNPC_VGhoulCroucher"), TEXT("0x10387520") },
		{ TEXT("CNPC_VHunter"), TEXT("0x10387520") },
		{ TEXT("CNPC_VStalker"), TEXT("0x10387520") },
		{ TEXT("CNPC_VYukie"), TEXT("0x10387520") },
		{ TEXT("CNPC_VPedestrian"), TEXT("0x103a2980") },
		{ TEXT("CNPC_VTzimisceHeadClaw"), TEXT("0x103c16f0") },
	};
	for (const FExpect& Row : Expected)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcTestCensus::Find(Row.Class);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), Row.Class), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 453 with %s"), Row.Class, Row.Body),
				FString(ElysiumNpcTestCensus::BodyOf(Cls, 453)), FString(Row.Body));
		}
	}

	// `CNPC_VCop` is in the 0x10387520 group above, and `npc_VCop` now builds it (story 5 step 2,
	// `docs/vtmb/npc-ai/population.md` "The classname → class map, read from the factories"); the
	// census used to claim no class for that classname. `npc_VHunter` is the same slot-453 body.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_testbits"), 4108);
	Builder.AddNpc(TEXT("hunter"), FVector::ZeroVector, TEXT("npc_VHunter"));
	Builder.AddNpc(TEXT("ped"), FVector(200.0, 0.0, 0.0), TEXT("npc_VPedestrian"));
	Builder.AddNpc(TEXT("guard"), FVector(400.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("cop"), FVector(600.0, 0.0, 0.0), TEXT("npc_VCop"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Hunter = Fixture.Npc(TEXT("hunter"));
	FElysiumNpc* Ped = Fixture.Npc(TEXT("ped"));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpcWorldFixture::Quiet({ Hunter, Ped, Guard, Cop });
	if (Hunter == nullptr || Ped == nullptr || Guard == nullptr || Cop == nullptr)
	{
		return false;
	}

	// `0x10387520`: alive, idle, and not (derived-type bit 7 AND spawner-made) adds
	// SEE_CORPSE_FRIEND. The derived-type word is a seam that reads bit 7 clear, so the third term
	// is open.
	{
		FElysiumNpcConditions Mask;
		Hunter->BuildScheduleTestBits(Mask);
		TestTrue(TEXT("an alive idle CNPC_VHunter adds SEE_CORPSE_FRIEND (0x3e)"),
			Mask.Has(EElysiumNpcCond::SeeCorpseFriend));
	}
	{
		FElysiumNpcConditions Mask;
		TestEqual(TEXT("a spawned npc_VCop is CNPC_VCop"), Cop->RetailClass(),
			ElysiumNpcTestCensus::Find(TEXT("CNPC_VCop")));
		Cop->BuildScheduleTestBits(Mask);
		// Story 5 step 2 (retail correction): `CNPC_VCop` fills slot 453 with the combatant body
		// `0x10387520`, which a placed cop now takes; alive and idle like the hunter above, it adds
		// SEE_CORPSE_FRIEND.
		TestTrue(TEXT("so an alive idle cop takes CNPC_VCop's slot-453 body, 0x10387520"),
			Mask.Has(EElysiumNpcCond::SeeCorpseFriend));
	}
	{
		FElysiumNpcConditions Mask;
		Ped->BuildScheduleTestBits(Mask);
		TestTrue(TEXT("a pedestrian not busy with a discipline adds PASS_OUT (0x24)"),
			Mask.Has(EElysiumNpcCond::PassOut));
		TestFalse(TEXT("and adds nothing else"),
			Mask.Has(EElysiumNpcCond::SeeCorpseFriend));
	}
	{
		FElysiumNpcConditions Mask;
		Guard->BuildScheduleTestBits(Mask);
		// CNPC_VHumanCombatant IS one of the nine 0x10387520 classes, so the combatant leaf takes
		// the same arm the cop does.
		TestTrue(TEXT("CNPC_VHumanCombatant takes the same 0x10387520 arm"),
			Mask.Has(EElysiumNpcCond::SeeCorpseFriend));
	}

	// The whole virtual, through the Troika-line body: the species overlay composes with it.
	{
		FElysiumNpcConditions Mask;
		Hunter->BuildScheduleTestBits(Mask);
		TestTrue(TEXT("the Troika overlay still runs — COMFORT is in the mask"),
			Mask.Has(EElysiumNpcCond::Comfort));
		TestTrue(TEXT("and the species addition composes with it"),
			Mask.Has(EElysiumNpcCond::SeeCorpseFriend));
		TestFalse(TEXT("0x1026a225: freeze belongs after direct slot 453"),
			Mask.Has(EElysiumNpcCond::NpcFreeze));
	}

	// `0x102b7690` — every input is a seam, and the case says so.
	{
		FElysiumNpc::FScheduleHintSearchRequest Request;
		Request.bRequest4 = true;
		Guard->ScheduleHost.bAllowKickHintUse = true;
		Guard->ScheduleHost.KickPropSearchTimer = 0.0;
		Guard->BaseScheduleHost.HintNode = INDEX_NONE;
		const double Now = Fixture.World.NowSeconds();
		TestEqual(TEXT("with no hint node and no kick prop the selector has no opinion"),
			Guard->SelectCoverOrKickSchedule(Request), 0);
		TestEqual(TEXT("but the kick-prop search timer was rearmed two seconds out"),
			Guard->ScheduleHost.KickPropSearchTimer, Now + 2.0, 1e-6);
		TestFalse(TEXT("the hint-cover object is cleared with no enemy"),
			Guard->ScheduleHost.HintCoverObject.IsSet());
	}

	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102b7690`'s cover tail — the enemy's weapon read, `0x102b78a2..0x102b78ee`.
// -------------------------------------------------------------------------------------------------

namespace
{
	// Suite-local: `ElysiumItems::Install` registers a class once per process and never unregisters.
	const TCHAR* const GScheduleTestPistol = TEXT("item_w_kernelschedule_pistol");
	const TCHAR* const GScheduleTestKatana = TEXT("item_w_kernelschedule_katana");

	FElysiumItemTable MakeScheduleTestItemTable()
	{
		auto MakeMode = [](const TCHAR* Dmg)
		{
			FElysiumWeaponMode Mode;
			Mode.Tag = TEXT("Primary");
			Mode.TypeName = TEXT("Attack");
			Mode.Type = EElysiumWeaponModeType::Attack;
			Mode.Dmg = Dmg;
			Mode.BaseLethality = 8;
			Mode.AttackRate = 0.5f;
			return Mode;
		};
		FElysiumItemTable Table;

		FElysiumItemDef Pistol;
		Pistol.Classname = GScheduleTestPistol;
		Pistol.PrintName = TEXT("Pistol");
		Pistol.Type = EElysiumItemType::WeaponFirearm;
		Pistol.AmmoType = TEXT("KernelScheduleRound");
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 6;
		Pistol.Modes.Add(MakeMode(TEXT("2 Lethal Ranged_Combat DMG_BULLET")));
		Table.Items.Add(MoveTemp(Pistol));

		FElysiumItemDef Katana;
		Katana.Classname = GScheduleTestKatana;
		Katana.PrintName = TEXT("Katana");
		Katana.Type = EElysiumItemType::WeaponMelee;
		Katana.Modes.Add(MakeMode(TEXT("3 Lethal Close_Combat_Melee DMG_SLASH")));
		Table.Items.Add(MoveTemp(Katana));

		Table.Reindex();
		return Table;
	}

	struct FScheduleCoverTailFixture
	{
		FElysiumItemTable Items;
		bool bInstalled = false;
		FElysiumNpcWorldFixture Fixture;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Foe = nullptr;
		FElysiumEntity* Hint = nullptr;
		FElysiumEntity* Worldspawn = nullptr;

		FScheduleCoverTailFixture()
			: Items(MakeScheduleTestItemTable())
			, Fixture([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_covertail"), 4109);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpc(TEXT("guard"));
					Builder.AddNpc(TEXT("foe"), FVector(400.0, 0.0, 0.0));
					// A cover-low hint by its TYPE (100): the selector's table reads `m_nHintType`.
					Builder.AddEntity(TEXT("info_node_hint"), TEXT("cover"), FVector(0.0, 100.0, 0.0))
						.Keys.Add(TEXT("hinttype"), TEXT("100"));
					return Builder;
				}(),
				[this](FElysiumRecordingServices&)
				{
					ElysiumItems::Install(Items);
					bInstalled = true;
				})
		{
			Guard = Fixture.Npc(TEXT("guard"));
			Foe = Fixture.Npc(TEXT("foe"));
			Hint = Fixture.World.FindByName(TEXT("cover"));
			Worldspawn = Fixture.World.FindByName(TEXT("world"));
			FElysiumNpcWorldFixture::Quiet({ Guard, Foe });
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
			if (Foe != nullptr)
			{
				// Empty-handed to begin with, whatever the loadout handed out.
				Foe->Inventory.Holster(*Foe);
			}
		}

		~FScheduleCoverTailFixture()
		{
			if (bInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		FScheduleCoverTailFixture(const FScheduleCoverTailFixture&) = delete;
		FScheduleCoverTailFixture& operator=(const FScheduleCoverTailFixture&) = delete;

		bool ArmFoe(const TCHAR* Classname)
		{
			const FElysiumEntityHandle Handle = Foe->Inventory.GiveNamedItem(*Foe, FString(Classname));
			FElysiumEntity* Entity = Fixture.World.Resolve(Handle);
			FElysiumItem* Item = Entity != nullptr ? Entity->AsItem() : nullptr;
			if (Item == nullptr)
			{
				return false;
			}
			Foe->Inventory.SetActiveWeapon(*Foe, *Item);
			return true;
		}

		// The selector with the guard standing AT a cover hint, not already in `0x9e` and not in
		// cover-vs-melee mode: the inner arm answers `0xa3` for a ranged threat and `0xa4` for none.
		int32 Select(const FElysiumEntity* Enemy)
		{
			Guard->BaseMemory.Enemy = Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid();
			Guard->BaseScheduleHost.HintNode = Hint->Handle.Index;
			Guard->NpcFlags.Set(EElysiumNpcFlag::AT_COVER_HINT);
			Guard->NpcFlags.ClearRawWord2Bits(static_cast<uint32>(EElysiumNpcFlag2::COVER_VS_MELEE_MODE));
			Guard->ScheduleHost.ShootAtHintNode = 0;
			Guard->PeekOutCount = 0;
			Guard->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
			return Guard->SelectCoverOrKickSchedule(FElysiumNpc::FScheduleHintSearchRequest());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleCoverTailRangedThreatTest,
	"Elysium.Arm.NpcKernelSchedule.CoverTailRangedThreat", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleCoverTailRangedThreatTest::RunTest(const FString&)
{
	FScheduleCoverTailFixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("foe"), F.Foe)
		|| !TestNotNull(TEXT("the cover hint"), F.Hint) || !TestNotNull(TEXT("worldspawn"), F.Worldspawn))
	{
		return false;
	}

	// `0x102b78a2..0x102b78ee`: GetEnemy() -> `+0x9c` -> GetActiveWeapon -> slot 360 `& 0x6000`.
	TestEqual(TEXT("0x102b78a2: an unarmed enemy is no ranged threat -> 0xa4"), F.Select(F.Foe), 0xa4);
	TestEqual(TEXT("0x102b78a2: an enemy that is no combat character (+0x9c null) -> 0xa4"),
		F.Select(F.Worldspawn), 0xa4);
	if (TestTrue(TEXT("the foe takes the katana"), F.ArmFoe(GScheduleTestKatana)))
	{
		TestEqual(TEXT("0x102b78ee: a melee weapon's word carries no 0x6000 bit -> 0xa4"),
			F.Select(F.Foe), 0xa4);
	}
	if (TestTrue(TEXT("the foe takes the pistol"), F.ArmFoe(GScheduleTestPistol)))
	{
		TestEqual(TEXT("0x102b78ee: an enemy holding a 0x6000 weapon -> 0xa3"), F.Select(F.Foe), 0xa3);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CAI_BaseNPC::ResolveTaskDistance 0x102702d0` -- the base body's three sentinels (spec 0002 V11-1).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleResolveTaskDistanceBaseTest,
	"Elysium.Arm.NpcKernelSchedule.ResolveTaskDistanceBase", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleResolveTaskDistanceBaseTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_distance_base"), 4111);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpcWorldFixture::Quiet({ Guard });
	if (Guard == nullptr)
	{
		return false;
	}
	Guard->SpecialDistanceAccum = 77.5f;
	TestEqual(TEXT("-1000000 ACCUM answers m_flSpecialDistanceAccum +0x5bac (0x102702f9)"),
		Guard->ResolveTaskDistance(-1000000.f), 77.5f);
	TestEqual(TEXT("-1000002 DIALOG answers 160.0, 0x1047a3ac (0x10270303)"),
		Guard->ResolveTaskDistance(-1000002.f), 160.f);
	TestEqual(TEXT("-1000003 COMBATMOVE answers the melee-range ConVar DAT_10924a1c (0x1027030d)"),
		Guard->ResolveTaskDistance(-1000003.f), FElysiumNpc::MeleeRangeUnits());
	TestEqual(TEXT("shipped 100"), FElysiumNpc::MeleeRangeUnits(), 100.f);
	TestEqual(TEXT("-1000001 is no base sentinel: it passes through"),
		Guard->ResolveTaskDistance(-1000001.f), -1000001.f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102a11d0` -- "an NPC I do not hate stands in the way" (spec 0002 V11-1, J10).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScheduleAllyInTheWayTest,
	"Elysium.Arm.NpcKernelSchedule.AllyInTheWay", GElysiumNpcKernelScheduleFlags)
bool FElysiumNpcKernelScheduleAllyInTheWayTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_schedule_ally_in_the_way"), 4112);
	Builder.AddNpc(TEXT("rear"));
	Builder.AddNpc(TEXT("front"), FVector(150.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("foe"), FVector(400.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Rear = Fixture.Npc(TEXT("rear"));
	FElysiumNpc* Front = Fixture.Npc(TEXT("front"));
	FElysiumNpc* Foe = Fixture.Npc(TEXT("foe"));
	FElysiumNpcWorldFixture::Quiet({ Rear, Front, Foe });
	if (Rear == nullptr || Front == nullptr || Foe == nullptr)
	{
		return false;
	}

	// No enemy: no point to sweep to.
	TestFalse(TEXT("0x102a11d0: no enemy answers false"), Rear->ScheduleNonHatedNpcInTheWay());

	ElysiumNpcEnemy::SetEnemy(*Rear, Foe->Handle);
	if (!TestTrue(TEXT("precondition: the enemy is set"), Rear->GetEnemy() == Foe))
	{
		return false;
	}
	// No collision world behind the NPC (the double has not opted in): the fault path, false.
	Fixture.Services.TraceRetailQuery = nullptr;
	TestFalse(TEXT("0x102a11d0: no embodiment answer is false"), Rear->ScheduleNonHatedNpcInTheWay());

	// The trace double: records the request, answers a clear world and whatever characters the case
	// lists.
	FElysiumRetailTrace Seen;
	TArray<FElysiumRetailTraceCharacter> Met;
	Fixture.Services.TraceRetailQuery = [&Seen, &Met](const FElysiumRetailTrace& Asking,
		FElysiumRetailTraceResult& Out)
	{
		Seen = Asking;
		for (const FElysiumRetailTraceCharacter& Character : Met)
		{
			Out.Characters.Add(Character);
		}
		Out.EndPosCm = Asking.EndCm;
		return true;
	};
	ON_SCOPE_EXIT { Fixture.Services.TraceRetailQuery = nullptr; };

	// Nothing on the line.
	TestFalse(TEXT("0x102a11d0: a clear sweep answers false"), Rear->ScheduleNonHatedNpcInTheWay());
	TestEqual(TEXT("mask 0x2000000 (CONTENTS_MONSTER)"), Seen.RetailMask, 0x2000000);
	TestTrue(TEXT("CTraceFilterSimple(this, 0): this NPC is the pass entity"),
		Seen.Ignore.Contains(Rear->Handle));
	TestTrue(TEXT("from WorldSpaceCenter (slot 192)"),
		Seen.StartCm.Equals(Rear->WorldSpaceCenter(), 0.01));
	TestTrue(TEXT("to the enemy's WorldSpaceCenter (the callers' point)"),
		Seen.EndCm.Equals(Foe->WorldSpaceCenter(), 0.01));
	// The hull: the collision box doubled in X / Y, Z +-6, Source units -> cm by `ElysiumMove::U`.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	FElysiumNpcBase::RetailCollisionExtents(*Rear, MinsUnits, MaxsUnits);
	const double CmPerUnit = ElysiumMove::U;
	TestEqual(TEXT("hull mins.x = 2 x OBBMins.x"), Seen.MinsCm.X, 2.0 * MinsUnits.X * CmPerUnit, 0.01);
	TestEqual(TEXT("hull maxs.x = 2 x OBBMaxs.x"), Seen.MaxsCm.X, 2.0 * MaxsUnits.X * CmPerUnit, 0.01);
	TestEqual(TEXT("hull maxs.y - mins.y = 2 x the box's Y span"), Seen.MaxsCm.Y - Seen.MinsCm.Y,
		2.0 * (MaxsUnits.Y - MinsUnits.Y) * CmPerUnit, 0.01);
	TestEqual(TEXT("hull mins.z = -6 units"), Seen.MinsCm.Z, -6.0 * CmPerUnit, 0.01);
	TestEqual(TEXT("hull maxs.z = +6 units"), Seen.MaxsCm.Z, 6.0 * CmPerUnit, 0.01);

	// An NPC nearest on the line.
	FElysiumRetailTraceCharacter Body;
	Body.Entity = Front->Handle;
	Body.Fraction = 0.4f;
	Met.Add(Body);
	Rear->Relationships.SetEntity(Front->Handle, EElysiumRelationship::Neutral, 5);
	TestTrue(TEXT("0x102a11d0: an NPC I am neutral to (D_NU) in the way answers true"),
		Rear->ScheduleNonHatedNpcInTheWay());
	Rear->Relationships.SetEntity(Front->Handle, EElysiumRelationship::Like, 5);
	TestTrue(TEXT("an NPC I like (D_LI) too"), Rear->ScheduleNonHatedNpcInTheWay());
	Rear->Relationships.SetEntity(Front->Handle, EElysiumRelationship::Hate, 5);
	TestFalse(TEXT("an NPC I hate (D_HT 1) answers false"), Rear->ScheduleNonHatedNpcInTheWay());

	// The player's body nearest: no NPC pointer (`+0x94`), false whatever the relation.
	if (FElysiumPlayer* Player = Fixture.Player())
	{
		Met.Reset();
		Body.Entity = Player->Handle;
		Met.Add(Body);
		Rear->Relationships.SetEntity(Player->Handle, EElysiumRelationship::Neutral, 5);
		TestFalse(TEXT("0x102a11d0: the player's body ends the trace and answers false"),
			Rear->ScheduleNonHatedNpcInTheWay());
	}
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

#endif
