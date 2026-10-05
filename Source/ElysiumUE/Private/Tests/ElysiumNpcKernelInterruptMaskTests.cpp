#include "Misc/AutomationTest.h"
#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Tests/ElysiumNpcTestFixture.h"

static constexpr EAutomationTestFlags GInterruptMaskFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Borrow the loaded program's inverse word, just as FInterruptMaskScope borrows its positive
	// word. No alternate program or corpus: restore the authored data on every exit.
	struct FInverseMaskScope
	{
		FElysiumScheduleProgram& Program;
		FElysiumNpcConditions Saved;
		FInverseMaskScope(const FElysiumScheduleProgram& Loaded, const FElysiumNpcConditions& Mask)
			: Program(const_cast<FElysiumScheduleProgram&>(Loaded)), Saved(Loaded.InvertedInterrupts)
		{
			Program.InvertedInterrupts = Mask;
		}
		~FInverseMaskScope() { Program.InvertedInterrupts = Saved; }
		FInverseMaskScope(const FInverseMaskScope&) = delete;
		FInverseMaskScope& operator=(const FInverseMaskScope&) = delete;
	};

	struct FInterruptFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Human = nullptr;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpcBase* Cine = nullptr;
		FInterruptFixture() : World([]
		{
			FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_interrupt_masks"), 4550);
			Builder.AddNpc(TEXT("human"));
			Builder.AddNpcOfClass(TEXT("guard"), FVector(300.0, 0.0, 0.0), TEXT("CNPC_VGuard1"));
			Builder.AddEntity(TEXT("scripted_sequence"), TEXT("cine")).Keys.Add(TEXT("spawnflags"), TEXT("32")); // 0x101a6f10: no interrupt
			return Builder;
		}())
		{
			Human = World.Npc(TEXT("human"));
			Guard = World.Npc(TEXT("guard"));
			FElysiumEntity* Director = World.World.FindByName(TEXT("cine"));
			Cine = Director != nullptr ? Director->AsNpcBase() : nullptr;
			FElysiumNpcWorldFixture::Quiet({ Human, Guard });
		}
	};

	void CombatProgram(FElysiumNpc& Npc, int32 LocalId = 0xef)
	{
		Npc.SetState(2);
		Npc.WriteIdealStateRetail(2);
		Npc.NpcFlags.Clear(EElysiumNpcFlag2::CHOOSE_NEW_SCHEDULE);
		Npc.Cognition.Conditions.Reset();
		ElysiumSchedule::Start(Npc.Schedule, LocalId, Npc);
		Npc.Schedule.TaskStatus = EElysiumTaskStatus::Running;
		Npc.Schedule.bDidMaintainSchedule = true;
	}

	// Keep task timing out of the interrupt probe; all masks, species overlay, state and the
	// IsScheduleValid dispatch are the human combatant's real ones. Capture its named break cause.
	class FInterruptHuman : public FElysiumNpcHumanCombatant
	{
	public:
		FElysiumNpcConditions Broken;
		FElysiumNpcConditions InverseBroken;
		virtual void StartTaskForMaintenance(FElysiumScheduleState&, const FElysiumScheduleStep&, double) override {}
		virtual void RunTaskForMaintenance(FElysiumScheduleState&, const FElysiumScheduleStep&, double) override {}
		virtual void DebugScheduleBreak(const FElysiumNpcConditions& Positive,
			const FElysiumNpcConditions& Inverse) override
		{
			Broken = Positive;
			InverseBroken = Inverse;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelInterruptMaskCacheTailTest,
	"Elysium.Arm.NpcKernelInterruptMask.CacheTail", GInterruptMaskFlags)
bool FElysiumNpcKernelInterruptMaskCacheTailTest::RunTest(const FString&)
{
	FInterruptFixture Fixture;
	if (!TestNotNull(TEXT("HumanCombatant"), Fixture.Human)
		|| !TestNotNull(TEXT("Guard1"), Fixture.Guard)
		|| !TestNotNull(TEXT("cine director"), Fixture.Cine)) return false;
	CombatProgram(*Fixture.Human);
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(Fixture.Human->Schedule.Current);
	if (!TestNotNull(TEXT("loaded 0xef fixture program"), Program)) return false;
	FElysiumNpcConditions Inverse;
	Inverse.Set(EElysiumNpcCond::SeeUnknown);
	FInverseMaskScope Borrowed(*Program, Inverse.ToGlobalOrdinals(Fixture.Human->ConditionIdSpace()));
	for (FElysiumNpc* Subject : { Fixture.Human, Fixture.Guard })
	{
		CombatProgram(*Subject);
		FElysiumNpcConditions Direct;
		Subject->BuildScheduleTestBits(Direct);
		if (Subject == Fixture.Guard)
		{
			TestFalse(TEXT("0x1037cdf0: direct slot 453 has no folded freeze"), Direct.Has(EElysiumNpcCond::NpcFreeze));
			TestFalse(TEXT("0x1037cdf0: combat arm replaces Troika law overlay"), Direct.Has(EElysiumNpcCond::InvestigateLevel));
		}
		Subject->Cognition.Conditions.Set(EElysiumNpcCond::NpcFreeze);
		Subject->Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
		Subject->Cognition.Conditions.Set(EElysiumNpcCond::SeeUnknown);
		Subject->CacheInterruptConditionsForMaintenance(17.0);
		TestEqual(TEXT("0x1026a16b: cache timestamp"), Subject->BaseScheduleHost.CacheInterruptTime, 17.0);
		TestTrue(TEXT("0x1026a211 -> 0x1026a21b -> 0x1026a221 PUSH 0x75 / 0x1026a225: positive freeze"),
			Subject->Cognition.CustomInterruptConditions.Has(EElysiumNpcCond::NpcFreeze));
		TestTrue(TEXT("0x1026a225 / 0x10269f02: current freeze survives"), Subject->Cognition.Conditions.Has(EElysiumNpcCond::NpcFreeze));
		TestTrue(TEXT("0x1026a21b: empty slot 411 preserves ignored LightDamage"), Subject->Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
		TestTrue(TEXT("0x1026a1d8..0x1026a207: authored inverse survives separately"), Subject->Cognition.InverseInterruptConditions.Has(EElysiumNpcCond::SeeUnknown));
		TestFalse(TEXT("0x10269d30: inverse-only current bit is not a positive interrupt"),
			ElysiumSchedule::HasInterruptCondition(Subject->Schedule, *Subject, Subject->Cognition.Conditions, EElysiumNpcCond::SeeUnknown));
		const FElysiumNpcConditions Effective = ElysiumSchedule::EffectiveInterrupts(Subject->Schedule, *Subject);
		TestTrue(TEXT("0x1026a225: common mask adds freeze after every species overlay"), Effective.Has(EElysiumNpcCond::NpcFreeze));
		TestTrue(TEXT("0x1026a211: cache and common positive product agree"),
			Effective.Difference(Subject->Cognition.CustomInterruptConditions).IsEmpty()
			&& Subject->Cognition.CustomInterruptConditions.Difference(Effective).IsEmpty());

		// A real uninterruptable director makes LightDamage an ignored condition. Cache must not
		// dispatch slot 459, even in SCRIPT; the control call proves this fixture can clear it.
		Subject->SetState(4);
		Subject->ScriptOwner = Fixture.Cine->Handle;
		Fixture.Cine->SetTarget(Subject->Handle);
		Subject->CacheInterruptConditionsForMaintenance(18.0);
		TestTrue(TEXT("0x1026a21b: SCRIPT cache does not call RemoveIgnoredConditions"), Subject->Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
		Subject->RemoveIgnoredConditions();
		TestFalse(TEXT("0x1026d7f0 / 0x101a89a0: control really ignores LightDamage"), Subject->Cognition.Conditions.Has(EElysiumNpcCond::LightDamage));
		Subject->Schedule.Clear();
		Subject->CacheInterruptConditionsForMaintenance(19.0);
		TestTrue(TEXT("0x1026a173..0x1026a196: no program resets both caches"),
			Subject->Cognition.CustomInterruptConditions.IsEmpty() && Subject->Cognition.InverseInterruptConditions.IsEmpty());
		TestTrue(TEXT("no program gives no common mask"), ElysiumSchedule::EffectiveInterrupts(Subject->Schedule, *Subject).IsEmpty());
		TestTrue(TEXT("0x1026a173..0x1026a196: no-program reset preserves current freeze"),
			Subject->Cognition.Conditions.Has(EElysiumNpcCond::NpcFreeze));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelInterruptMaskRangedProgramsTest,
	"Elysium.Arm.NpcKernelInterruptMask.RangedPrograms", GInterruptMaskFlags)
bool FElysiumNpcKernelInterruptMaskRangedProgramsTest::RunTest(const FString&)
{
	FInterruptFixture Fixture;
	if (!TestNotNull(TEXT("human"), Fixture.Human) || !TestNotNull(TEXT("enemy"), Fixture.Guard)) return false;
	FElysiumNpc& Human = *Fixture.Human;
	CombatProgram(Human);
	Human.BaseMemory.Enemy = Fixture.Guard->Handle;
	// Loaded via FElysiumScheduleCorpus::EnsureLoaded, the same registry used by the world fixture:
	// Content/ElysiumCorpus/ai/schedules/cai_basenpctroika/sched_troika_step_back_range_attack1.sch
	// Content/ElysiumCorpus/ai/schedules/cai_basenpctroika/sched_troika_range_attack1.sch
	// Content/ElysiumCorpus/ai/schedules/cai_basenpctroika/sched_troika_forced_range_attack1.sch
	const TCHAR* Names[] = { TEXT("SCHED_TROIKA_STEP_BACK_RANGE_ATTACK1"),
		TEXT("SCHED_TROIKA_RANGE_ATTACK1"), TEXT("SCHED_TROIKA_FORCED_RANGE_ATTACK1") };
	for (int32 ProgramIndex = 0; ProgramIndex < 3; ++ProgramIndex)
	{
		const FElysiumScheduleProgram* Loaded = FElysiumScheduleCorpus::Get().Manager().FindByName(Names[ProgramIndex]);
		if (!TestNotNull(Names[ProgramIndex], Loaded)) return false;
		const FElysiumNpcConditions Authored = Loaded->Interrupts.ToLocalOrdinals(Human.ConditionIdSpace());
		FElysiumNpcConditions Expected;
		Expected.Set(EElysiumNpcCond::EnemyDead);
		if (ProgramIndex != 2)
		{
			for (EElysiumNpcCond Bit : { EElysiumNpcCond::NewEnemy, EElysiumNpcCond::LightDamage,
				EElysiumNpcCond::HeavyDamage, EElysiumNpcCond::EnemyOccluded, EElysiumNpcCond::NoPrimaryAmmo }) Expected.Set(Bit);
		}
		if (ProgramIndex == 1) Expected.Set(EElysiumNpcCond::TooCloseToAttack);
		TestTrue(TEXT("deployed .sch authored mask is exact"), Authored.Difference(Expected).IsEmpty() && Expected.Difference(Authored).IsEmpty());
		ElysiumSchedule::Install(Human.Schedule, Loaded->GlobalId, Human);
		const FElysiumNpcConditions Effective = ElysiumSchedule::EffectiveInterrupts(Human.Schedule, Human);
		TestTrue(TEXT("completed mask retains every authored positive interrupt"), Expected.Difference(Effective).IsEmpty());
		TestTrue(TEXT("0x102ad140 / 0x1026a225: law, comfort and post-virtual freeze"),
			Effective.Has(EElysiumNpcCond::InvestigateLevel) && Effective.Has(EElysiumNpcCond::Comfort) && Effective.Has(EElysiumNpcCond::NpcFreeze));
		if (ProgramIndex == 0)
		{
			for (EElysiumNpcCond Bit : { EElysiumNpcCond::NotFacingAttack, EElysiumNpcCond::WeaponThroughWall,
				EElysiumNpcCond::TooCloseForRanged, EElysiumNpcCond::TooCloseToAttack })
				TestFalse(TEXT("0xef: 0x61 / 0x3c / 0x08 / 0x5f are absent even after overlay"), Effective.Has(Bit));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelInterruptMaskBreakTest,
	"Elysium.Arm.NpcKernelInterruptMask.Break", GInterruptMaskFlags)
bool FElysiumNpcKernelInterruptMaskBreakTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_interrupt_break"), 4551);
	Builder.AddNpc(TEXT("human")).InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FInterruptHuman>(); };
	Builder.AddNpc(TEXT("enemy"), FVector(500.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* HumanNpc = Fixture.Npc(TEXT("human"));
	FElysiumNpc* Enemy = Fixture.Npc(TEXT("enemy"));
	if (!TestNotNull(TEXT("human"), HumanNpc) || !TestNotNull(TEXT("enemy"), Enemy)) return false;
	FInterruptHuman& Human = *static_cast<FInterruptHuman*>(HumanNpc);
	FElysiumNpcWorldFixture::Quiet({ &Human, Enemy });
	CombatProgram(Human);
	Human.BaseMemory.Enemy = Enemy->Handle;
	Human.Cognition.Conditions.Set(EElysiumNpcCond::NotFacingAttack);
	Human.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	const int32 StepBack = Human.Schedule.Current;
	ElysiumSchedule::Tick(Human.Schedule, Human, 0.0, &Human.Cognition.Conditions, true);
	TestEqual(TEXT("0x10281243..0x10281340: 0x61 + 0x3c do not break 0xef"), Human.Schedule.Current, StepBack);
	TestTrue(TEXT("0xef has no break cause for 0x61 + 0x3c"), Human.Broken.IsEmpty());
	Human.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	ElysiumSchedule::Tick(Human.Schedule, Human, 0.0, &Human.Cognition.Conditions, true);
	TestTrue(TEXT("0x10281243..0x10281340: LIGHT_DAMAGE is the break cause"),
		Human.Broken.Has(EElysiumNpcCond::LightDamage));
	TestTrue(TEXT("break names LIGHT_DAMAGE"), Human.Broken.Describe().Contains(TEXT("LIGHT_DAMAGE")));
	CombatProgram(Human);
	Human.Broken.Reset();
	Human.Cognition.Conditions.Set(EElysiumNpcCond::NpcFreeze);
	Human.CacheInterruptConditionsForMaintenance(0.0);
	TestTrue(TEXT("0x1026a225: freeze remains current after cache"), Human.Cognition.Conditions.Has(EElysiumNpcCond::NpcFreeze));
	ElysiumSchedule::Tick(Human.Schedule, Human, 0.0, &Human.Cognition.Conditions, true);
	TestTrue(TEXT("0x10281340: retained NPC_FREEZE breaks 0xef"), Human.Broken.Has(EElysiumNpcCond::NpcFreeze));
	CombatProgram(Human);
	Human.Broken.Reset();
	Human.InverseBroken.Reset();
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(Human.Schedule.Current);
	if (!TestNotNull(TEXT("loaded inverse fixture program"), Program)) return false;
	FElysiumNpcConditions Inverse;
	Inverse.Set(EElysiumNpcCond::SeeUnknown);
	FInverseMaskScope Borrowed(*Program, Inverse.ToGlobalOrdinals(Human.ConditionIdSpace()));
	Human.CacheInterruptConditionsForMaintenance(0.0);
	ElysiumSchedule::Tick(Human.Schedule, Human, 0.0, &Human.Cognition.Conditions, true);
	TestTrue(TEXT("0x10280ff0: inverse absence breaks IsScheduleValid separately"),
		Human.InverseBroken.Has(EElysiumNpcCond::SeeUnknown));
	TestTrue(TEXT("inverse absence has no positive break cause"), Human.Broken.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelInterruptMaskTestersTest,
	"Elysium.Arm.NpcKernelInterruptMask.Testers", GInterruptMaskFlags)
bool FElysiumNpcKernelInterruptMaskTestersTest::RunTest(const FString&)
{
	FInterruptFixture Fixture;
	if (!TestNotNull(TEXT("human"), Fixture.Human)) return false;
	FElysiumNpc& Human = *Fixture.Human;
	Human.Schedule.Clear();
	Human.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	auto Has = [&Human](EElysiumNpcCond Bit)
	{
		return ElysiumSchedule::HasInterruptCondition(Human.Schedule, Human, Human.Cognition.Conditions, Bit);
	};
	TestFalse(TEXT("0x10269d30: no schedule is false"), Has(EElysiumNpcCond::LightDamage));
	CombatProgram(Human);
	Human.Cognition.Conditions.Set(EElysiumNpcCond::NotFacingAttack);
	TestFalse(TEXT("0x10269d30: condition-only is false"), Has(EElysiumNpcCond::NotFacingAttack));
	TestFalse(TEXT("0x10269d30: mask-only is false"), Has(EElysiumNpcCond::LightDamage));
	Human.Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestTrue(TEXT("0x10269d30: positive mask AND condition is true"), Has(EElysiumNpcCond::LightDamage));
	const FElysiumScheduleProgram* Program = ElysiumScheduleFor(Human.Schedule.Current);
	if (!TestNotNull(TEXT("loaded program"), Program)) return false;
	FElysiumNpcConditions Inverse;
	Inverse.Set(EElysiumNpcCond::SeeUnknown);
	FInverseMaskScope Borrowed(*Program, Inverse.ToGlobalOrdinals(Human.ConditionIdSpace()));
	Human.Cognition.Conditions.Set(EElysiumNpcCond::SeeUnknown);
	TestFalse(TEXT("0x10269d30: inverse-only bit ignores a present condition"), Has(EElysiumNpcCond::SeeUnknown));
	Human.Cognition.Conditions.Clear(EElysiumNpcCond::SeeUnknown);
	TestFalse(TEXT("0x10269d30: inverse absence is not this positive-only tester"), Has(EElysiumNpcCond::SeeUnknown));
	return true;
}

#endif
