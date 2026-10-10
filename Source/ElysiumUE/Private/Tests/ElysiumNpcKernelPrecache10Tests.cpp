#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Tests/ElysiumNpcDeadClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29d, family **Precache10**. Every expectation below is read off the decompiled C of the
// body it names and, where the decompiler folded an argument or a table index, off the listing —
// never off the checklist's one-line walk, which this family corrected in six places.
//
// Since 0019/6 (verdict `mechanism`, service `Bake`) a precache REQUEST is the bake's, so the
// suite covers only what still runs: the Troika body's writes (`0x10298ad0`), the species arms that
// keep a write a later rule reads (the tentacle's three mode indices, the camera's model fallback
// and group clear), the werewolf's sound-group writes, and
// the three `CNPCMaker*` arms on their own type.
//
// A case asserts the whole `PrecacheLog` elementwise — the channel, the name, the flag and the
// ORDER — because the order is the recovered half. Formatting one op as `"<channel>:<name>:<flag>"`
// makes a mismatch readable as a diff rather than as an index.

static constexpr EAutomationTestFlags GPrecache10TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Slot 104's index, and the one op every Troika-body run produces on a fixture NPC with no
	// authored `model`: the keyfield is empty, so slot 212 falls it back to the error model and the
	// engine precaches that. `AlternateEquipment` and `dialogname` are unset on a bare fixture row,
	// so neither the alt-equipment arm nor the dialogue globs fire.
	constexpr int32 GPrecacheSlotIndex = 104;
	const TCHAR* const GTroikaFallbackModel = TEXT("models/error/error.mdl");

	FString Precache10OpText(const FElysiumNpcBase::FPrecacheOp& Op)
	{
		switch (Op.Channel)
		{
		case FElysiumNpcBase::EPrecacheChannel::Model:
			return FString::Printf(TEXT("model:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpcBase::EPrecacheChannel::Sound:
			return FString::Printf(TEXT("sound:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpcBase::EPrecacheChannel::Particle:
			return FString::Printf(TEXT("particle:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpcBase::EPrecacheChannel::Other:
			return FString::Printf(TEXT("other:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpcBase::EPrecacheChannel::Directory:
			return FString::Printf(TEXT("dir:%s:%s:star=%d:flag=%d"), *Op.Name, *Op.Extension,
				Op.bStarPrefix ? 1 : 0, Op.Flag);
		}
		return TEXT("?");
	}

	TArray<FString> Precache10LogText(const TArray<FElysiumNpcBase::FPrecacheOp>& Log)
	{
		TArray<FString> Out;
		Out.Reserve(Log.Num());
		for (const FElysiumNpcBase::FPrecacheOp& Op : Log)
		{
			Out.Add(Precache10OpText(Op));
		}
		return Out;
	}

	// Run slot 104 on an NPC that was SPAWNED as the class under test (story 5 step 2: the class is
	// the C++ type its classname built, never a reclass). The log and the model keyfield start
	// clear: two arms WRITE the model keyfield (the Troika fallback and the tentacle's), so an arm
	// must start from an unset keyfield or it would see a fallback as an authored model.
	void Precache10RunArm(FElysiumNpc& Npc)
	{
		Npc.PrecacheLog.Reset();
		Npc.Model.Reset();
		Npc.Precache();
	}

	// The whole log, elementwise, with the expected list spelled in retail's order.
	bool Precache10CheckLog(FAutomationTestBase& Test, const TCHAR* What, const FElysiumNpc& Npc,
		const TArray<FString>& Expected)
	{
		const TArray<FString> Actual = Precache10LogText(Npc.PrecacheLog);
		if (Actual != Expected)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected [%s], got [%s]"), What,
				*FString::Join(Expected, TEXT(" | ")), *FString::Join(Actual, TEXT(" | "))));
			return false;
		}
		return true;
	}

	// One NPC spawned as the retail class under test (through its factory classname, story 5
	// step 2), plus the bare Troika control: `AddTroikaNpc` stands the Troika line with no species
	// class over it, so its `RetailClass()` is null and slot 104 falls through to the Troika body.
	// (The control used to be an `npc_VCop`, back when that classname resolved to no class; it now
	// builds `CNPC_VCop`, so it is no longer the Troika line.)
	struct FPrecache10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Species = nullptr;
		FElysiumNpc* Troika = nullptr;

		explicit FPrecache10Fixture(const TCHAR* RetailClass = TEXT("CNPC_VHumanCombatant"))
			: World(Build(RetailClass))
		{
			Species = World.Npc(TEXT("species"));
			Troika = World.Npc(TEXT("troika"));
			FElysiumNpcWorldFixture::Quiet({ Species, Troika });
		}

		static FElysiumNpcWorldBuilder Build(const TCHAR* RetailClass)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("precache10"), 29140u);
			Builder.AddNpcOfClass(TEXT("species"), FVector::ZeroVector, RetailClass);
			Builder.AddTroikaNpc(TEXT("troika"), FVector(200.0, 0.0, 0.0));
			return Builder;
		}
	};

	// The Troika control, asserted by every species case: a bare Troika NPC takes no species arm
	// and its slot 104 is the Troika body alone.
	bool Precache10CheckTroikaControl(FAutomationTestBase& Test, FPrecache10Fixture& Fix)
	{
		if (Fix.Troika == nullptr)
		{
			Test.AddError(TEXT("the bare Troika control did not spawn"));
			return false;
		}
		Test.TestNull(TEXT("the bare Troika NPC's RetailClass() is null"),
			Fix.Troika->RetailClass());
		Fix.Troika->PrecacheLog.Reset();
		Fix.Troika->Model.Reset();
		Fix.Troika->Precache();
		Test.TestEqual(TEXT("the bare Troika control takes the error-model fallback"),
			Fix.Troika->Model, FString(GTroikaFallbackModel));
		return Precache10CheckLog(Test, TEXT("the bare Troika control"), *Fix.Troika, {});
	}

	// The shape every species case is: spawn the class, run the arm, compare the whole log, and
	// prove the control.
	bool Precache10Case(FAutomationTestBase& Test, const TCHAR* RetailClass,
		const TArray<FString>& Expected)
	{
		FPrecache10Fixture Fix(RetailClass);
		if (Fix.Species == nullptr)
		{
			Test.AddError(FString::Printf(TEXT("the %s subject did not spawn"), RetailClass));
			return false;
		}
		Precache10RunArm(*Fix.Species);
		Precache10CheckLog(Test, RetailClass, *Fix.Species, Expected);
		return Precache10CheckTroikaControl(Test, Fix);
	}
}

// =================================================================================================
// The two base bodies.
// =================================================================================================

// `CAI_BaseNPC::Precache` (spawn-equipment request) and the Troika body's
// `sound/character/<dialog>` directory chop and globs were deleted in 0019/6 (verdict `mechanism`,
// service `Bake`), with their cases `BasePrecache`, `DialogueDirectory` and `DialogueGlob`.

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10TroikaTest,
	"Elysium.Arm.NpcKernelPrecache10.TroikaPrecache", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10TroikaTest::RunTest(const FString&)
{
	// `CAI_BaseNPCTroika::Precache` `0x10298ad0`, slot 104, on the bare Troika NPC: no species
	// class, so the Troika body is what slot 104 runs.
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the bare Troika NPC spawned"), Fix.Troika))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Troika;

	// Arm 1: an empty model keyfield falls back through slot 212 `SetModelName` to
	// `"models/error/error.mdl"` — the fallback is OBSERVABLE on the entity. The request that
	// followed it in retail is the bake's since 0019/6, so the body issues none.
	N.PrecacheLog.Reset();
	N.Model.Reset();
	N.Precache();
	TestEqual(TEXT("the empty keyfield falls back to the error model"), N.Model,
		FString(TEXT("models/error/error.mdl")));
	TestEqual(TEXT("and no precache request is issued"), N.PrecacheLog.Num(), 0);

	// Arm 2: an authored model is left as authored and the fallback does not run.
	N.Model = TEXT("models/character/npc/downtown/mercurio.mdl");
	N.Precache();
	TestEqual(TEXT("an authored model is kept"), N.Model,
		FString(TEXT("models/character/npc/downtown/mercurio.mdl")));

	// Arm 3: `m_altEquipment` and `m_spawnEquipment` no longer reach any request.
	N.PrecacheLog.Reset();
	N.AlternateEquipment = TEXT("item_w_unarmedX");
	N.AdditionalEquipment = TEXT("item_w_katana");
	N.Precache();
	TestEqual(TEXT("equipment keyfields issue no request"), N.PrecacheLog.Num(), 0);
	N.AlternateEquipment.Reset();
	N.AdditionalEquipment.Reset();

	// Arm 5: slot 608 `SetAttackCoordinator` is dispatched with `"Normal"` LAST, and `+0x64e8`
	// (this runtime's `StanceResolvedFor`) takes the disposition-table row for this body's model.
	N.PrecacheLog.Reset();
	N.StanceResolvedFor.Reset();
	N.Precache();
	TestFalse(TEXT("the disposition row was resolved at precache (+0x64e8)"),
		N.StanceResolvedFor.IsEmpty());

	// The char-template model seam for `FUN_10207e60` (template `+0x78`). Its one port reader went
	// with the dead `CGenericSabbat_NPC::Precache` in 0019 story 5 step 1; it stays as the named
	// input of the live `CAI_BaseNPCTroika::Spawn` `0x10298d30` call the port does not make yet.
	// No template column is recovered, so it answers retail's null-template name: the empty string.
	return true;
}

// =================================================================================================
// The species arms, in retail address order.
// =================================================================================================

// `CNPC_VBach::Precache` `0x103637b0` (three weapons, five sounds after the Troika body) was deleted
// in 0019/6 (target `Bake`): it wrote nothing, so Bach falls through to the Troika body.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MingXiaoTentacleTest,
	"Elysium.Arm.NpcKernelPrecache10.MingXiaoTentacle", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10MingXiaoTentacleTest::RunTest(const FString&)
{
	// `0x1039c220` — the ONE arm whose model fallback runs BEFORE the chain, and the only one that
	// stores model indices. The first two `PrecacheModel` calls push the SAME string
	// (`0x1064a2f0`), so retail's two indices are equal and this port records that equality
	// explicitly.
	FPrecache10Fixture Fix(TEXT("CNPC_VMingXiaoTentacle"));
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	Precache10RunArm(*Fix.Species);
	TestEqual(TEXT("the fallback model is written to the keyfield before the chain"),
		Fix.Species->Model,
		FString(TEXT("models/character/monster/mingxiao/mingxiao_tentacle/mingxiao_tentacle.mdl")));
	Precache10CheckLog(*this, TEXT("CNPC_VMingXiaoTentacle"), *Fix.Species,
		{
			// The three model indices the phase-change tasks hand to slot 10 (kept writes; the
			// chain's own request, the three emitters and the two sounds are the bake's, 0019/6).
			TEXT("model:models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl:0"),
			TEXT("model:models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl:0"),
			TEXT("model:models/character/monster/MingXiao/MingXiao_transformation.mdl:0"),
		});
	TestEqual(TEXT("+0x6664 and +0x6668 are ONE model's index, because retail pushes one string"),
		ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexTentacleToGrub, ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexGrub);
	TestNotEqual(TEXT("+0x666c is the transformation model's, a different index"),
		ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexGrubToProxy, ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexGrub);
	return Precache10CheckTroikaControl(*this, Fix);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10WerewolfTest,
	"Elysium.Arm.NpcKernelPrecache10.Werewolf", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10WerewolfTest::RunTest(const FString&)
{
	// `CNPC_VWerewolf::Precache`. Its two directory globs, the footstep table (the Tzimisce fat guy's, a retail
	// copy-paste), `item_w_werewolf_attacks` and the two `dev/ww_tele_*` singles are the bake's
	// since 0019/6 (target `Bake`); only the three state writes a rule reads stay, so the log is empty.
	FPrecache10Fixture Fix(TEXT("CNPC_VWerewolf"));
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	Precache10RunArm(*Fix.Species);
	Precache10CheckLog(*this, TEXT("CNPC_VWerewolf"), *Fix.Species, {});
	// The sound-group triple, in retail's write order.
	TestEqual(TEXT("+0x00bc m_iVSoundTableIdx takes the literal 2"),
		Fix.Species->VSoundTableIdx, 2);
	TestEqual(TEXT("+0x00c0 m_iszVSoundGroup takes \"Werewolf\""),
		Fix.Species->SoundGroup, FString(TEXT("Werewolf")));
	// The seam `FUN_101f55a0` (L0-r007): the fixture's world holds no `SndScheme_Char` registry (the L2
	// data hook unfilled), which the seam reads as `reg+0x20 == NULL` and answers 0 (S0, `XOR EAX,EAX`
	// at 0x101f55b6) -- not -1. Asserted as retail's own unloaded-table arm.
	TestEqual(TEXT("+0x00b4 m_iVSoundGroup takes the unloaded table's answer (S0)"),
		Fix.Species->VSoundGroup, 0);
	return Precache10CheckTroikaControl(*this, Fix);
}

// =================================================================================================
// The arm story 29c-1 ported and left unwired.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10CameraTest,
	"Elysium.Arm.NpcKernelPrecache10.Camera", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10CameraTest::RunTest(const FString&)
{
	// `CNPC_VCamera::Precache` `0x103689c0`, shared with `CNPC_VCameraSecurity`. The model fallback
	// is family Lifecycle's `CameraPrecacheModel`; the slot-452 reject arm and the
	// `m_iInterestingPlaceGroups = 0` write are the tail that family left for a later story.
	// One camera spawned per form, each in its own fixture.
	bool bOk = true;
	for (const TCHAR* Form : { TEXT("CNPC_VCamera"), TEXT("CNPC_VCameraSecurity") })
	{
		FPrecache10Fixture Fix(Form);
		if (!TestNotNull(*FString::Printf(TEXT("the %s subject spawned"), Form), Fix.Species))
		{
			return false;
		}
		Fix.Species->InterestingPlaceGroupMask = 0xffu;
		Precache10RunArm(*Fix.Species);
		Precache10CheckLog(*this, Form, *Fix.Species, {});
		TestEqual(TEXT("the keyfield took the camera's null-model fallback"), Fix.Species->Model,
			FString(TEXT("models/null.mdl")));
		TestEqual(TEXT("m_iInterestingPlaceGroups is cleared at precache"),
			Fix.Species->InterestingPlaceGroupMask, 0u);
		bOk &= Precache10CheckTroikaControl(*this, Fix);
	}
	return bOk;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// =================================================================================================
// The three `CNPCMaker*` arms, each its class's own override (story 5 fold A4).
// =================================================================================================

namespace
{
	// One maker of a given classname, with its two keyfields set. `Classname` must be one of the
	// three maker classnames — `npc_maker`, `npc_maker_fleshpile` and `npc_maker_zombie` — each of
	// which builds its own `FElysiumNpcMaker` subclass.
	struct FPrecache10MakerFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpcMaker* Maker = nullptr;

		/** What `Spawn`'s own slot-104 dispatch precached, before any case drove `Precache()` by
		 *  hand. See the constructor. */
		TArray<FElysiumNpcBase::FPrecacheOp> SpawnPrecacheLog;
		bool bDeadAfterSpawn = false;

		FPrecache10MakerFixture(const TCHAR* Classname, const TCHAR* ModelKey, const TCHAR* NpcType)
			: World(Build(Classname, ModelKey, NpcType))
		{
			check(FString(Classname) == TEXT("npc_maker")
				|| FString(Classname) == TEXT("npc_maker_fleshpile")
				|| FString(Classname) == TEXT("npc_maker_zombie"));
			// **STRENGTHENED, story 29d family SpeciesLifecycle10.** `CNPCMaker::Spawn`
			// (`0x1034afe0`) dispatches slot 104 `Precache` through `vt+0x1a0`, and the port's
			// `FElysiumNpcMaker::Spawn` now makes that dispatch. So by the time a case runs, slot 104
			// has ALREADY run once at map load — which is retail's order — and a maker with no model
			// keyfield has already `UTIL_Remove`d itself, exactly as retail's does.
			//
			// Two consequences, both recorded rather than hidden: `FindByName` answers only LIVE
			// entities, so the maker is fetched off the entity list by targetname whether it lives or
			// not; and the spawn-time log is kept before the live one is cleared, so each case below
			// still measures exactly the `Precache()` it drives by hand.
			for (const TUniquePtr<FElysiumEntity>& Entity : World.World.Entities())
			{
				if (Entity && Entity->TargetName == TEXT("maker"))
				{
					Maker = static_cast<FElysiumNpcMaker*>(Entity.Get());
					break;
				}
			}
			if (Maker != nullptr)
			{
				SpawnPrecacheLog = Maker->PrecacheLog;
				bDeadAfterSpawn = Maker->IsDead();
				Maker->PrecacheLog.Reset();
				Maker->DeveloperOverlayBoxes.Reset();
			}
		}

		static FElysiumNpcWorldBuilder Build(const TCHAR* Classname, const TCHAR* ModelKey,
			const TCHAR* NpcType)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("precache10_maker"), 29142u);
			FElysiumEntityDef& Def =
				Builder.AddEntity(Classname, TEXT("maker"), FVector(64.0, -32.0, 16.0));
			if (ModelKey != nullptr)
			{
				Def.Keys.Add(TEXT("model"), ModelKey);
			}
			if (NpcType != nullptr)
			{
				Def.Keys.Add(TEXT("NPCType"), NpcType);
			}
			return Builder;
		}
	};

	bool Precache10CheckMakerLog(FAutomationTestBase& Test, const TCHAR* What,
		const TArray<FString>& Expected, const TArray<FElysiumNpcBase::FPrecacheOp>& Log)
	{
		const TArray<FString> Actual = Precache10LogText(Log);
		if (Actual != Expected)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected [%s], got [%s]"), What,
				*FString::Join(Expected, TEXT(" | ")), *FString::Join(Actual, TEXT(" | "))));
			return false;
		}
		return true;
	}

	bool Precache10CheckMakerLog(FAutomationTestBase& Test, const TCHAR* What,
		const FElysiumNpcMaker& Maker, const TArray<FString>& Expected)
	{
		const TArray<FString> Actual = Precache10LogText(Maker.PrecacheLog);
		if (Actual != Expected)
		{
			Test.AddError(FString::Printf(TEXT("%s: expected [%s], got [%s]"), What,
				*FString::Join(Expected, TEXT(" | ")), *FString::Join(Actual, TEXT(" | "))));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MakerArmCoverageTest,
	"Elysium.Substrate.NpcKernelPrecache10.MakerArmCoverage", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10MakerArmCoverageTest::RunTest(const FString&)
{
	// `ArmCoverage`'s other half: EVERY `CNPCMaker*` slot-104 override row the census carries runs
	// its OWN arm on a real maker of the row's classname — not a sibling's, and not the Troika body.
	// The census is the input: each row's classname comes from `ElysiumNpcTestCensus::Find`.
	//
	// Each maker stands with a model and NO child classname, the one input on which the three arms
	// diverge (`0x1034b160` checks the classname and removes the maker; `0x1034c180` precaches it
	// unchecked; `0x1034cde0` does the same and adds `item_w_zombie_fists`). What is read is
	// `Spawn`'s own slot-104 dispatch (`1034b06f`), the precache retail runs at map load.
	struct FMakerArm
	{
		const TCHAR* Class;
		const TCHAR* Address;
		TArray<FString> Expected;
		bool bRemoved;
	};
	const FMakerArm Arms[] = {
		{ TEXT("CNPCMaker"), TEXT("0x1034b160"),
			{ TEXT("model:models/m.mdl:0") }, true },
		{ TEXT("CNPCMaker_Fleshpile"), TEXT("0x1034c180"),
			{ TEXT("model:models/m.mdl:0"), TEXT("other::0") }, false },
		{ TEXT("CNPCMaker_Zombie"), TEXT("0x1034cde0"),
			{ TEXT("model:models/m.mdl:0"), TEXT("other::0"), TEXT("other:item_w_zombie_fists:0") },
			false },
	};
	int32 Rows = 0;
	for (const FElysiumNpcClassSlot& Row : ElysiumNpcKernelShape::Overrides())
	{
		if (Row.Slot != GPrecacheSlotIndex || FCString::Strncmp(Row.Class, TEXT("CNPCMaker"), 9) != 0)
		{
			continue;
		}
		++Rows;
		const FMakerArm* Arm = nullptr;
		for (const FMakerArm& Candidate : Arms)
		{
			if (FCString::Strcmp(Candidate.Class, Row.Class) == 0)
			{
				Arm = &Candidate;
				break;
			}
		}
		if (!TestNotNull(*FString::Printf(TEXT("%s is a known maker arm"), Row.Class), Arm))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s's slot-104 body"), Row.Class), FString(Row.Address),
			FString(Arm->Address));
		const TCHAR* const Classname = FElysiumNpcWorldBuilder::ClassnameOf(Row.Class);
		FPrecache10MakerFixture Fix(Classname, TEXT("models/m.mdl"), nullptr);
		if (!TestNotNull(*FString::Printf(TEXT("a %s spawned"), Classname), Fix.Maker))
		{
			continue;
		}
		Precache10CheckMakerLog(*this, *FString::Printf(TEXT("%s runs %s"), Classname, Arm->Address),
			Arm->Expected, Fix.SpawnPrecacheLog);
		TestEqual(*FString::Printf(TEXT("%s's arm decides removal on an empty child classname"),
			Classname), Fix.bDeadAfterSpawn, Arm->bRemoved);
	}
	TestEqual(TEXT("the census carries 3 CNPCMaker* slot-104 override rows"), Rows, 3);
	return true;
}

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MakerBaseTest,
	"Elysium.Arm.NpcKernelPrecache10.MakerBase", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10MakerBaseTest::RunTest(const FString&)
{
	// `CNPCMaker::Precache` `0x1034b160` — the one arm that checks BOTH keyfields and draws the two
	// developer overlay boxes.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker"), TEXT("models/maker.mdl"),
			TEXT("npc_VCop"));
		if (!TestNotNull(TEXT("the maker spawned"), Fix.Maker))
		{
			return false;
		}
		// `Spawn` (`0x1034afe0`) dispatched slot 104 at map load, which is retail's order and is the
		// half story 29d family SpeciesLifecycle10 added.
		Precache10CheckMakerLog(*this, TEXT("Spawn's own slot-104 dispatch"),
			{ TEXT("model:models/maker.mdl:0"), TEXT("other:npc_VCop:0") },
			Fix.SpawnPrecacheLog);
		Fix.Maker->Precache();
		Precache10CheckMakerLog(*this, TEXT("a complete npc_maker"), *Fix.Maker,
			{ TEXT("model:models/maker.mdl:0"), TEXT("other:npc_VCop:0") });
		TestTrue(TEXT("and is not removed"), !Fix.Maker->IsDead());
		TestEqual(TEXT("no overlay is requested on the good path"),
			Fix.Maker->DeveloperOverlayBoxes.Num(), 0);
	}

	// An EMPTY model keyfield: warn, `UTIL_Remove`, and — under `developer >= 1` — the
	// `"%s: BAD MODEL NAME"` overlay box. The child classname is never reached.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker"), nullptr, TEXT("npc_VCop"));
		if (!TestNotNull(TEXT("the maker spawned"), Fix.Maker))
		{
			return false;
		}
		FElysiumNpcMaker::DeveloperCvarLevel = 0;
		Fix.Maker->Precache();
		TestEqual(TEXT("an empty model precaches nothing"), Fix.Maker->PrecacheLog.Num(), 0);
		TestTrue(TEXT("and removes the maker"), Fix.Maker->IsDead());
		TestEqual(TEXT("the overlay is refused at the shipped developer 0"),
			Fix.Maker->DeveloperOverlayBoxes.Num(), 0);

		FElysiumNpcMaker::DeveloperCvarLevel = 1;
		Fix.Maker->Precache();
		if (TestEqual(TEXT("developer 1 reaches the overlay arm"),
			Fix.Maker->DeveloperOverlayBoxes.Num(), 1))
		{
			TestTrue(TEXT("and it is the BAD MODEL NAME box"),
				Fix.Maker->DeveloperOverlayBoxes[0].Text.EndsWith(TEXT(": BAD MODEL NAME")));
		}
		FElysiumNpcMaker::DeveloperCvarLevel = 0;
	}

	// An EMPTY child classname with a model present: the model IS precached, then warn,
	// `UTIL_Remove` and the `"%s: BAD NPC Classname"` overlay.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker"), TEXT("models/maker.mdl"), nullptr);
		if (!TestNotNull(TEXT("the maker spawned"), Fix.Maker))
		{
			return false;
		}
		FElysiumNpcMaker::DeveloperCvarLevel = 1;
		Fix.Maker->Precache();
		Precache10CheckMakerLog(*this, TEXT("an empty child classname"), *Fix.Maker,
			{ TEXT("model:models/maker.mdl:0") });
		TestTrue(TEXT("and the maker is removed"), Fix.Maker->IsDead());
		if (TestEqual(TEXT("the overlay arm ran"), Fix.Maker->DeveloperOverlayBoxes.Num(), 1))
		{
			TestTrue(TEXT("and it is the BAD NPC Classname box"),
				Fix.Maker->DeveloperOverlayBoxes[0].Text.EndsWith(TEXT(": BAD NPC Classname")));
		}
		FElysiumNpcMaker::DeveloperCvarLevel = 0;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MakerFleshpileTest,
	"Elysium.Arm.NpcKernelPrecache10.MakerFleshpile", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10MakerFleshpileTest::RunTest(const FString&)
{
	// `CNPCMaker_Fleshpile::Precache` `0x1034c180` — relative to the base arm it DROPS the
	// empty-classname check and the developer overlay entirely, keeping only the missing-model
	// warning and `UTIL_Remove`.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker_fleshpile"), TEXT("models/fp.mdl"),
			TEXT("npc_VTzimisceRunner"));
		if (!TestNotNull(TEXT("the fleshpile maker spawned"), Fix.Maker))
		{
			return false;
		}
		Fix.Maker->Precache();
		Precache10CheckMakerLog(*this, TEXT("a complete npc_maker_fleshpile"), *Fix.Maker,
			{ TEXT("model:models/fp.mdl:0"), TEXT("other:npc_VTzimisceRunner:0") });
	}
	// An EMPTY classname is NOT checked here: the arm precaches it unconditionally, which reaches
	// `UTIL_PrecacheOther("")` and its own `"NULL Ent in UTIL_PrecacheOther: %s"` warning rather
	// than removing the maker.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker_fleshpile"), TEXT("models/fp.mdl"), nullptr);
		if (!TestNotNull(TEXT("the fleshpile maker spawned"), Fix.Maker))
		{
			return false;
		}
		FElysiumNpcMaker::DeveloperCvarLevel = 1;
		Fix.Maker->Precache();
		Precache10CheckMakerLog(*this, TEXT("the fleshpile's unchecked classname"), *Fix.Maker,
			{ TEXT("model:models/fp.mdl:0"), TEXT("other::0") });
		TestTrue(TEXT("and the maker stands"), !Fix.Maker->IsDead());
		TestEqual(TEXT("the fleshpile arm draws no overlay even at developer 1"),
			Fix.Maker->DeveloperOverlayBoxes.Num(), 0);
		FElysiumNpcMaker::DeveloperCvarLevel = 0;
	}
	// The missing-model arm it DOES keep.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker_fleshpile"), nullptr, TEXT("npc_VCop"));
		if (!TestNotNull(TEXT("the fleshpile maker spawned"), Fix.Maker))
		{
			return false;
		}
		FElysiumNpcMaker::DeveloperCvarLevel = 1;
		Fix.Maker->Precache();
		TestEqual(TEXT("an empty model precaches nothing"), Fix.Maker->PrecacheLog.Num(), 0);
		TestTrue(TEXT("and removes the maker"), Fix.Maker->IsDead());
		TestEqual(TEXT("with no overlay"), Fix.Maker->DeveloperOverlayBoxes.Num(), 0);
		FElysiumNpcMaker::DeveloperCvarLevel = 0;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MakerZombieTest,
	"Elysium.Arm.NpcKernelPrecache10.MakerZombie", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10MakerZombieTest::RunTest(const FString&)
{
	// `CNPCMaker_Zombie::Precache` `0x1034cde0` — the fleshpile shape plus three zombie facts: both
	// equipment words are zeroed BEFORE the base chain, so an authored loadout is discarded and the
	// base's own `UTIL_PrecacheOther(m_spawnEquipment)` arm can never fire; and
	// `item_w_zombie_fists` is precached AFTER the child classname.
	//
	// The fixture stands a real `npc_maker_zombie`, the census's classname for `CNPCMaker_Zombie`
	// (`CNPCMaker_Zombie_Classnames`), which builds `FElysiumNpcMakerZombie`; its class's override
	// is what runs.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker_zombie"), TEXT("models/z.mdl"),
			TEXT("npc_VZombie"));
		if (!TestNotNull(TEXT("the maker spawned"), Fix.Maker))
		{
			return false;
		}
		TestTrue(TEXT("the zombie maker is its own class"),
			Fix.Maker->RetailClass() != nullptr
				&& FString(Fix.Maker->RetailClass()->Name) == TEXT("CNPCMaker_Zombie"));
		// Set both words by hand (a maker inherits the Troika's `additionalequipment` /
		// `alternateequipment` keyfields since story 5 fold A4); the point of the arm is that it
		// discards them.
		Fix.Maker->AlternateEquipment = TEXT("item_w_katana");
		Fix.Maker->AdditionalEquipment = TEXT("item_w_glock_17c");
		Fix.Maker->Precache();
		TestTrue(TEXT("m_altEquipment is zeroed before the chain"),
			Fix.Maker->AlternateEquipment.IsEmpty());
		TestTrue(TEXT("m_spawnEquipment is zeroed before the chain"),
			Fix.Maker->AdditionalEquipment.IsEmpty());
		Precache10CheckMakerLog(*this,
			TEXT("the base chain precaches no equipment, and the fists come last"), *Fix.Maker,
			{ TEXT("model:models/z.mdl:0"), TEXT("other:npc_VZombie:0"),
				TEXT("other:item_w_zombie_fists:0") });
	}

	// The missing-model arm, same as the fleshpile's: warn, remove, no overlay.
	{
		FPrecache10MakerFixture Empty(TEXT("npc_maker_zombie"), nullptr, TEXT("npc_VZombie"));
		if (!TestNotNull(TEXT("the second maker spawned"), Empty.Maker))
		{
			return false;
		}
		FElysiumNpcMaker::DeveloperCvarLevel = 1;
		Empty.Maker->Precache();
		TestEqual(TEXT("an empty model precaches nothing"), Empty.Maker->PrecacheLog.Num(), 0);
		TestTrue(TEXT("and removes the maker"), Empty.Maker->IsDead());
		TestEqual(TEXT("with no overlay"), Empty.Maker->DeveloperOverlayBoxes.Num(), 0);
		FElysiumNpcMaker::DeveloperCvarLevel = 0;
	}
	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

#endif   // WITH_DEV_AUTOMATION_TESTS
