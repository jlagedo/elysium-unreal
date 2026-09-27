#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Tests/ElysiumNpcDeadClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Precache10**. Every expectation below is read off the decompiled C of the
// body it names and, where the decompiler folded an argument or a table index, off the listing —
// never off the checklist's one-line walk, which this family corrected in six places.
//
// The suite is in four parts: the two BASE bodies (`0x1027bb50`, `0x10298ad0`) and the dialogue
// chop they share; the SPECIES arms, one named case each, every one of them proving the species
// body for an NPC spawned as its retail class AND the Troika body for a bare Troika NPC; the two
// arms story 29c-1 left unwired; and the three `CNPCMaker*` arms on their own type.
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
	const TCHAR* const GTroikaOnly = TEXT("model:models/error/error.mdl:0");

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
		return Precache10CheckLog(Test, TEXT("the bare Troika control"), *Fix.Troika,
			{ GTroikaOnly });
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10BaseTest,
	"Elysium.Substrate.NpcKernelPrecache10.BasePrecache", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10BaseTest::RunTest(const FString&)
{
	// `CAI_BaseNPC::Precache` `0x1027bb50`.
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// Arm 1a: an UNSET `m_spawnEquipment` precaches nothing — retail's outer `TEST EAX,EAX / JZ`.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment.Reset();
	N.FElysiumNpcBase::Precache();
	TestEqual(TEXT("an unset m_spawnEquipment precaches nothing"), N.PrecacheLog.Num(), 0);

	// Arm 1b: the authored none sentinel is the one-character string `"0"` (`DAT_105399a0`), and
	// retail's two-byte `REPE CMPSB` against it is what skips it.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment = TEXT("0");
	N.FElysiumNpcBase::Precache();
	TestEqual(TEXT("the \"0\" sentinel precaches nothing"), N.PrecacheLog.Num(), 0);

	// Arm 1c: anything else goes through `UTIL_PrecacheOther` `0x101d0ec0`.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment = TEXT("item_w_glock_17c");
	N.FElysiumNpcBase::Precache();
	Precache10CheckLog(*this, TEXT("an authored m_spawnEquipment"), N,
		{ TEXT("other:item_w_glock_17c:0") });

	// Arm 1d: `"00"` is NOT the sentinel — retail compares the two bytes `'0'` and the NUL, so a
	// second character makes the compare fail on byte two and the string IS precached.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment = TEXT("00");
	N.FElysiumNpcBase::Precache();
	Precache10CheckLog(*this, TEXT("\"00\" is not the sentinel"), N, { TEXT("other:00:0") });

	// Arm 2: slot 452 `LoadedSchedules` (vtable `+0x710`) decides the rest. A false answer prints
	// `"ERROR: Rejecting spawn of %s as error in NPC's schedules."`, `UTIL_Remove`s the entity and
	// RETURNS WITHOUT chaining the base.
	//
	// That arm is UNREACHABLE in this runtime and this case says so rather than weakening: nothing
	// here parses schedule text, so no class flag can be cleared and
	// `ElysiumNpcSchedule.cpp`'s body answers true for every class. What is asserted is the
	// gate itself and the arm it therefore takes — the body falls through and the NPC is still
	// alive, which a reject would not leave behind.
	TestTrue(TEXT("slot 452 answers true for every class in this runtime"), N.LoadedSchedules());
	N.AdditionalEquipment.Reset();
	N.PrecacheLog.Reset();
	N.FElysiumNpcBase::Precache();
	TestTrue(TEXT("so the base falls through and does not remove the entity"), N.IsAlive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10DialogueDirTest,
	"Elysium.Substrate.NpcKernelPrecache10.DialogueDirectory", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10DialogueDirTest::RunTest(const FString&)
{
	// `0x10298ad0`'s chop, read off the listing at `10298bf8`..`10298c04`: `buf[strlen(buf) - 4]`
	// takes the NUL, so FOUR characters come off — not the five the checklist's walk claims. The
	// lowercase pass then runs over the chopped length.
	TestEqual(TEXT("four characters come off, not five"),
		FElysiumNpc::DialogueSoundDirectory(TEXT("Jack.dlg")),
		FString(TEXT("sound/character/jack")));
	TestEqual(TEXT("and the whole buffer is lowercased, prefix included"),
		FElysiumNpc::DialogueSoundDirectory(TEXT("Downtown/Bertram.DLG")),
		FString(TEXT("sound/character/downtown/bertram")));
	// A name with no extension loses its last four characters all the same — retail chops by count,
	// never by a dot.
	TestEqual(TEXT("the chop is by count, not by a dot"),
		FElysiumNpc::DialogueSoundDirectory(TEXT("abcdefgh")),
		FString(TEXT("sound/character/abcd")));
	// The prefix alone is sixteen characters, so the clamp named at the definition cannot be
	// reached by any dialog name at all, including the empty one.
	TestEqual(TEXT("an empty dialog name still leaves the chopped prefix"),
		FElysiumNpc::DialogueSoundDirectory(FString()),
		FString(TEXT("sound/charac")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10TroikaTest,
	"Elysium.Substrate.NpcKernelPrecache10.TroikaPrecache", GPrecache10TestFlags)
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
	// `"models/error/error.mdl"` and is then precached — so the fallback is OBSERVABLE on the
	// entity, not just in the request.
	N.PrecacheLog.Reset();
	N.Model.Reset();
	N.Precache();
	TestEqual(TEXT("the empty keyfield falls back to the error model"), N.Model,
		FString(TEXT("models/error/error.mdl")));
	Precache10CheckLog(*this, TEXT("a bare Troika NPC"), N, { GTroikaOnly });

	// Arm 2: an authored model is precached as authored and the fallback does not run.
	N.PrecacheLog.Reset();
	N.Model = TEXT("models/character/npc/downtown/mercurio.mdl");
	N.Precache();
	Precache10CheckLog(*this, TEXT("an authored model"), N,
		{ TEXT("model:models/character/npc/downtown/mercurio.mdl:0") });

	// Arm 3: `m_altEquipment` (`+0x1a98`) has TWO exclusions where the base body has one — the
	// sentinel `"0"` and the exact literal `"item_w_unarmed"` (a 15-byte compare, the string plus
	// its NUL).
	N.Model = TEXT("m.mdl");
	for (const TCHAR* Skipped : { TEXT("0"), TEXT("item_w_unarmed") })
	{
		N.PrecacheLog.Reset();
		N.AlternateEquipment = Skipped;
		N.Precache();
		Precache10CheckLog(*this, Skipped, N, { TEXT("model:m.mdl:0") });
	}
	// `"item_w_unarmedX"` is not the literal: retail compares fifteen bytes and byte fifteen is the
	// NUL, so a longer string fails the compare and IS precached.
	N.PrecacheLog.Reset();
	N.AlternateEquipment = TEXT("item_w_unarmedX");
	N.Precache();
	Precache10CheckLog(*this, TEXT("a longer alt equipment"), N,
		{ TEXT("model:m.mdl:0"), TEXT("other:item_w_unarmedX:0") });
	N.AlternateEquipment.Reset();

	// Arm 4: the chain is `CAI_BaseNPC::Precache`, so the base body's own `m_spawnEquipment` arm
	// runs INSIDE the Troika body and lands between the model and the dialogue globs.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment = TEXT("item_w_katana");
	N.Precache();
	Precache10CheckLog(*this, TEXT("the base chain runs inside the Troika body"), N,
		{ TEXT("model:m.mdl:0"), TEXT("other:item_w_katana:0") });
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
	TestEqual(TEXT("the char-template model seam answers the empty name"),
		N.CharTemplateModelName(), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10DialogueGlobTest,
	"Elysium.Substrate.NpcKernelPrecache10.DialogueGlob", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10DialogueGlobTest::RunTest(const FString&)
{
	// The `m_iDialog` half of `0x10298ad0`: `.wav` FIRST then `.mp3`, both with `0x101d0f10`'s
	// third argument SET (the `"*%s/%s"` name format) and its fourth CLEAR. The other two call
	// sites of that function in this family pass different pairs, which is why the op carries both.
	FElysiumNpcWorldBuilder Builder(TEXT("precache10_dialog"), 29141u);
	// A bare Troika NPC: no species class, so slot 104 runs the Troika body the globs live in.
	FElysiumEntityDef& Def = Builder.AddTroikaNpc(TEXT("talker"));
	Def.Keys.Add(TEXT("dialogname"), TEXT("dlg/Downtown/Trip.dlg"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("talker"));
	if (!TestNotNull(TEXT("the talker spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->PrecacheLog.Reset();
	Npc->Model = TEXT("m.mdl");
	Npc->Precache();
	Precache10CheckLog(*this, TEXT("the dialogue globs"), *Npc,
		{
			TEXT("model:m.mdl:0"),
			TEXT("dir:sound/character/dlg/downtown/trip:.wav:star=1:flag=0"),
			TEXT("dir:sound/character/dlg/downtown/trip:.mp3:star=1:flag=0"),
		});
	return true;
}

// =================================================================================================
// The species dispatch.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10ArmCoverageTest,
	"Elysium.Substrate.NpcKernelPrecache10.ArmCoverage", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10ArmCoverageTest::RunTest(const FString&)
{
	// EVERY slot-104 override row the census carries must be dispatched to its species body: since
	// story 5 step 3 that body is the override on the class's C++ type, so a class whose override
	// was missing would silently take the Troika body. The census is the test's input, not a list
	// typed here; each row stands a fresh NPC spawned as its class, from the same seed, and its slot
	// must precache something the Troika body alone (`FElysiumNpc::Precache`) does not.
	int32 Rows = 0;
	for (const FElysiumNpcClassSlot& Row : ElysiumNpcKernelShape::Overrides())
	{
		// The classes with no instance keep their census rows but carry no port arm.
		if (Row.Slot != GPrecacheSlotIndex || ElysiumNpcDeadClasses::Contains(Row.Class))
		{
			continue;
		}
		++Rows;
		// Counted but not driven here: the three `CNPCMaker*` rows. Their bodies chain
		// `CAI_BaseNPC::Precache` directly, not the Troika's, so the Troika control this loop checks
		// does not apply; `MakerArmCoverage` below stands a real maker of each row's classname (its
		// own class since story 5 fold A4) and proves the row's own body runs.
		if (FCString::Strncmp(Row.Class, TEXT("CNPCMaker"), 9) == 0)
		{
			continue;
		}
		FPrecache10Fixture Fix(Row.Class);
		if (!TestNotNull(*FString::Printf(TEXT("%s spawned"), Row.Class), Fix.Species))
		{
			continue;
		}
		Fix.Species->PrecacheLog.Reset();
		Fix.Species->Model.Reset();
		Fix.Species->Precache();
		const TArray<FString> Species = Precache10LogText(Fix.Species->PrecacheLog);
		Fix.Species->PrecacheLog.Reset();
		Fix.Species->Model.Reset();
		Fix.Species->FElysiumNpc::Precache();
		TestTrue(*FString::Printf(TEXT("%s's slot 104 runs its own body, not the Troika one"), Row.Class),
			Species != Precache10LogText(Fix.Species->PrecacheLog));
	}
	// 25 rows over 22 distinct bodies: the three Chang forms share `0x1036ae60` and the two camera
	// forms share `0x103689c0`, which is why the table keys on the address. The census's other six
	// slot-104 rows are the classes with no instance (`CNPC_Crow`, `CGeneric_NPC`, its bathack,
	// `CGenericSabbat_NPC`, `CNPC_VTest`, `CGenericNPC`).
	TestEqual(TEXT("the census carries 25 slot-104 override rows on classes with an instance"),
		Rows, 25);

	// And a class with NO slot-104 row runs the Troika body: `CNPC_VHumanCombatant` is on the
	// Troika line and carries no override.
	FPrecache10Fixture Plain(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the npc_VHumanCombatant subject spawned"), Plain.Species))
	{
		return false;
	}
	TestNull(TEXT("CNPC_VHumanCombatant carries no slot-104 override"),
		ElysiumNpcKernelClass::OverrideOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VHumanCombatant")), GPrecacheSlotIndex));
	Plain.Species->PrecacheLog.Reset();
	Plain.Species->Precache();
	const TArray<FString> Slot = Precache10LogText(Plain.Species->PrecacheLog);
	Plain.Species->PrecacheLog.Reset();
	Plain.Species->FElysiumNpc::Precache();
	TestTrue(TEXT("so its slot 104 is the Troika body"), Slot == Precache10LogText(Plain.Species->PrecacheLog));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10ThunkTest,
	"Elysium.Substrate.NpcKernelPrecache10.SpeciesThunk", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10ThunkTest::RunTest(const FString&)
{
	// Retail's chain call is a DIRECT `thunk_`, never a vtable dispatch, so a species body's own
	// `CAI_BaseNPCTroika::Precache` can never re-enter the species body. Since story 5 step 3 the
	// species body calls `TroikaPrecache()` by name — so the fact that `CNPC_VZombie`'s override
	// TERMINATES with exactly one Troika model op is the assertion.
	return Precache10Case(*this, TEXT("CNPC_VZombie"),
		{
			GTroikaOnly,
			TEXT("particle:zombie_headshot_death_emitter:0"),
			TEXT("particle:zombie_headshot_dmg_emitter:0"),
			TEXT("other:item_w_zombie_fists:0"),
		});
}

// =================================================================================================
// The species arms, in retail address order.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10AndreiBloodTest,
	"Elysium.Substrate.NpcKernelPrecache10.AndreiBlood", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10AndreiBloodTest::RunTest(const FString&)
{
	// `0x1035cb90` — the Troika body FIRST, three sounds, three preload-1 emitters. The emitter
	// names carry a HYPHEN before `Emitter`, not the underscore the checklist's walk spells.
	return Precache10Case(*this, TEXT("CNPC_VAndreiBlood"),
		{
			GTroikaOnly,
			TEXT("sound:Character/Boss/Andrei/TeleportOut.wav:0"),
			TEXT("sound:Character/Boss/Andrei/TeleportIn.wav:0"),
			TEXT("sound:Character/Boss/Andrei/Summon.wav:0"),
			TEXT("particle:Andrei_Teleport_Out-Emitter:1"),
			TEXT("particle:Andrei_Teleport_In-Emitter:1"),
			TEXT("particle:Andrei_Summon-Emitter:1"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10AsianVampireTest,
	"Elysium.Substrate.NpcKernelPrecache10.AsianVampire", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10AsianVampireTest::RunTest(const FString&)
{
	// `0x10360bc0` — a scope-trace frame, the Troika body, and exactly one weapon. That single
	// weapon is the whole species payload.
	return Precache10Case(*this, TEXT("CNPC_VAsianVampire"),
		{ GTroikaOnly, TEXT("other:item_w_avamp_blade:0") });
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10BachTest,
	"Elysium.Substrate.NpcKernelPrecache10.Bach", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10BachTest::RunTest(const FString&)
{
	// `0x103637b0` — the weapons-before-sounds order is this arm's fact.
	return Precache10Case(*this, TEXT("CNPC_VBach"),
		{
			GTroikaOnly,
			TEXT("other:item_w_grenade_frag:0"),
			TEXT("other:item_w_katana:0"),
			TEXT("other:item_w_rem_m_700_bach:0"),
			TEXT("sound:Character/Boss/Bach/bach_grenade.wav:0"),
			TEXT("sound:Character/Boss/Bach/bach_shield.wav:0"),
			TEXT("sound:Character/Boss/Bach/bach_camp_warn.wav:0"),
			TEXT("sound:Character/Boss/Bach/bach_holy_light.wav:0"),
			TEXT("sound:Character/Boss/Bach/snipe_warn6.wav:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10ChangBrosTest,
	"Elysium.Substrate.NpcKernelPrecache10.ChangBros", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10ChangBrosTest::RunTest(const FString&)
{
	// `0x1036ae60` fills THREE species slots — `CNPC_VChangBros#104`, `…Blade#104` and
	// `…Claw#104` — so all three forms take one arm, which is what keying on the address buys.
	const TArray<FString> Expected =
	{
		GTroikaOnly,
		TEXT("particle:chang_teleport_in_emitter:1"),
		TEXT("particle:chang_teleport_out_emitter:1"),
		TEXT("particle:chang_powerup_emitter:1"),
		TEXT("particle:chang_spine_emitter:1"),
		TEXT("particle:chang_center_emitter:1"),
		TEXT("particle:chang_blast_emitter:1"),
		TEXT("particle:chang_ball_charge_emitter:1"),
		TEXT("other:item_w_chang_claw:0"),
		TEXT("other:item_w_chang_blade:0"),
		TEXT("other:item_w_chang_energy_ball:0"),
		TEXT("other:item_w_chang_ghost:0"),
	};
	// One NPC spawned per form, each in its own fixture.
	bool bOk = true;
	for (const TCHAR* Form : { TEXT("CNPC_VChangBros"), TEXT("CNPC_VChangBrosBlade"),
		TEXT("CNPC_VChangBrosClaw") })
	{
		bOk &= Precache10Case(*this, Form, Expected);
	}
	return bOk;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10GargoyleTest,
	"Elysium.Substrate.NpcKernelPrecache10.Gargoyle", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10GargoyleTest::RunTest(const FString&)
{
	// `0x10378470` — nine gib models with preload 1 in the body's own push order (descending
	// `.rdata`, `0x1063a808` down to `0x1063a550`), the 0x10-byte stomp table, the 0xc-byte exert
	// table, one roar and the fist.
	return Precache10Case(*this, TEXT("CNPC_VGargoyle"),
		{
			GTroikaOnly,
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/garg_gibbs.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_head.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_foot.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_hand.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_L_torso.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_pelvis.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_foot.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_hand.mdl:1"),
			TEXT("model:models/character/monster/gargoyle/gargoyle_gibbs/gargoyle_R_torso.mdl:1"),
			TEXT("sound:character/monster/gargoyle/stomp_1.wav:0"),
			TEXT("sound:character/monster/gargoyle/stomp_2.wav:0"),
			TEXT("sound:character/monster/gargoyle/stomp_3.wav:0"),
			TEXT("sound:character/monster/gargoyle/stomp_4.wav:0"),
			TEXT("sound:character/monster/gargoyle/exert_heavy_1.wav:0"),
			TEXT("sound:character/monster/gargoyle/exert_heavy_2.wav:0"),
			TEXT("sound:character/monster/gargoyle/exert_heavy_3.wav:0"),
			TEXT("sound:character/monster/gargoyle/roar2.wav:0"),
			TEXT("other:item_w_gargoyle_fist:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10GhoulCroucherTest,
	"Elysium.Substrate.NpcKernelPrecache10.GhoulCroucher", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10GhoulCroucherTest::RunTest(const FString&)
{
	// `0x1037b1a0` — 55 bytes, the shortest arm in the band. The two preload-0 stalker models are
	// what makes the male/female split in its `SetModel` sibling (`0x1037b1f0`) reachable.
	return Precache10Case(*this, TEXT("CNPC_VGhoulCroucher"),
		{
			GTroikaOnly,
			TEXT("other:item_w_claws_ghoul:0"),
			TEXT("model:models/character/npc/unique/Malkavian_mansion/Stalker/stalker.mdl:0"),
			TEXT("model:models/character/npc/unique/Malkavian_mansion/Stalker_Female/"
				"stalker_female.mdl:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10HengeyokaiTest,
	"Elysium.Substrate.NpcKernelPrecache10.Hengeyokai", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10HengeyokaiTest::RunTest(const FString&)
{
	// `0x1037f960` — and the freeze emitter takes preload **0**, not the 1 Andrei, Chang and the
	// ManBat use. That split is retail data and this case is where it is pinned.
	return Precache10Case(*this, TEXT("CNPC_VHengeyokai"),
		{
			GTroikaOnly,
			TEXT("sound:character/monster/hengeyokai/stomp_1.wav:0"),
			TEXT("sound:character/monster/hengeyokai/stomp_2.wav:0"),
			TEXT("sound:character/monster/hengeyokai/stomp_3.wav:0"),
			TEXT("sound:character/monster/hengeyokai/stomp_4.wav:0"),
			TEXT("sound:character/monster/hengeyokai/exert_heavy_1.wav:0"),
			TEXT("sound:character/monster/hengeyokai/exert_heavy_2.wav:0"),
			TEXT("sound:character/monster/hengeyokai/exert_heavy_3.wav:0"),
			TEXT("model:models/character/monster/Hengeyokai/hengeyokai.mdl:0"),
			TEXT("particle:Hengeyokai_freeze_emitter:0"),
			TEXT("other:item_w_hengeyokai_fist:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10ManBatTest,
	"Elysium.Substrate.NpcKernelPrecache10.ManBat", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10ManBatTest::RunTest(const FString&)
{
	// `0x1038aec0` — 280 bytes, the longest arm here. `sheriff_teleport_emitter` is precached TWICE
	// in a row from the identical `.rdata` cell `0x10642adc`; that is a retail duplicate and it is
	// KEPT. Entries 2 and 3 of the four-entry model table are unrecovered and carried by index.
	return Precache10Case(*this, TEXT("CNPC_VManBat"),
		{
			GTroikaOnly,
			TEXT("model:models/character/monster/manbat/Throw_Objects/ThrowTaxi.mdl:0"),
			TEXT("model:models/character/monster/manbat/Throw_Objects/supportb.mdl:0"),
			TEXT("model:PTR_0x10640ce0[2]:0"),
			TEXT("model:PTR_0x10640ce0[3]:0"),
			TEXT("particle:Manbat_screechcone_emitter:1"),
			TEXT("particle:Manbat_player_emitter:1"),
			TEXT("particle:HUD_Manbat_emitter:1"),
			TEXT("particle:Manbat_blast_player:1"),
			TEXT("sound:character/male/sheriff_manbat/wingflap_1.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/wingflap_2.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/wingflap_3.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/exert_heavy_1.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/exert_heavy_2.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/exert_heavy_3.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/fly_by_1.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/fly_by_2.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/fly_by_3.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/screech.wav:0"),
			TEXT("sound:character/male/sheriff_manbat/fall.wav:0"),
			TEXT("particle:sheriff_teleport_emitter:1"),
			TEXT("particle:sheriff_teleport_emitter:1"),
			TEXT("other:item_w_manbat_claw:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MingXiaoTest,
	"Elysium.Substrate.NpcKernelPrecache10.MingXiao", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10MingXiaoTest::RunTest(const FString&)
{
	// `0x10392660` — eleven emitters, ALL with preload 0, then one move sound and three weapons.
	// The move sound's directory carries a SPACE (`ming xiao`), not an underscore.
	return Precache10Case(*this, TEXT("CNPC_VMingXiao"),
		{
			GTroikaOnly,
			TEXT("particle:Ming_xiao_slimetrail_emitter:0"),
			TEXT("particle:Ming_xiao_slimetrail_emitter2:0"),
			TEXT("particle:Ming_xiao_tentacle_damage_emitter:0"),
			TEXT("particle:Ming_xiao_tentacle_burst_emitter:0"),
			TEXT("particle:Ming_xiao_death_emitter:0"),
			TEXT("particle:Ming_xiao_death_emitter2:0"),
			TEXT("particle:Ming_xiao_death_proxy_emitter:0"),
			TEXT("particle:Ming_xiao_death_proxy_emitter2:0"),
			TEXT("particle:Ming_xiao_vomit_emitter:0"),
			TEXT("particle:Ming_xiao_transform_emitter:0"),
			TEXT("particle:Ming_xiao_transform_emitter2:0"),
			TEXT("sound:character/monster/ming xiao/movement.wav:0"),
			TEXT("other:item_w_mingxiao_melee:0"),
			TEXT("other:item_w_mingxiao_tentacle:0"),
			TEXT("other:item_w_mingxiao_spit:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MingXiaoTentacleTest,
	"Elysium.Substrate.NpcKernelPrecache10.MingXiaoTentacle", GPrecache10TestFlags)
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
			// The chain, which now sees the fallback the arm just wrote.
			TEXT("model:models/character/monster/mingxiao/mingxiao_tentacle/"
				"mingxiao_tentacle.mdl:0"),
			TEXT("model:models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl:0"),
			TEXT("model:models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl:0"),
			TEXT("model:models/character/monster/MingXiao/MingXiao_transformation.mdl:0"),
			TEXT("particle:Ming_xiao_tentacle_transform_emitter:0"),
			TEXT("particle:Ming_xiao_baby_transform_emitter:0"),
			TEXT("particle:Ming_xiao_baby_death_emitter:0"),
			TEXT("sound:character/monster/ming xiao/tentacle_hit_ground.wav:0"),
			TEXT("sound:character/monster/ming xiao/tentacle_flopping_loop.wav:0"),
		});
	TestEqual(TEXT("+0x6664 and +0x6668 are ONE model's index, because retail pushes one string"),
		ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexTentacleToGrub, ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexGrub);
	TestNotEqual(TEXT("+0x666c is the transformation model's, a different index"),
		ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexGrubToProxy, ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix.Species)->ModeIndexGrub);
	return Precache10CheckTroikaControl(*this, Fix);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10NewscasterTest,
	"Elysium.Substrate.NpcKernelPrecache10.Newscaster", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10NewscasterTest::RunTest(const FString&)
{
	// `0x103a03e0` — `.mp3` FIRST and `.wav` second, the reverse of the Troika body's own pair, and
	// BOTH calls pass `0x101d0f10`'s third and fourth arguments as 0 where the Troika body passes
	// 1 and 0. Three call sites of one function, three argument pairs.
	return Precache10Case(*this, TEXT("CNPC_VNewscaster"),
		{
			GTroikaOnly,
			TEXT("dir:sound/character/conversations/news/tv:.mp3:star=0:flag=0"),
			TEXT("dir:sound/character/conversations/news/tv:.wav:star=0:flag=0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10SabbatLeaderTest,
	"Elysium.Substrate.NpcKernelPrecache10.SabbatLeader", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10SabbatLeaderTest::RunTest(const FString&)
{
	// `0x103a6ab0`. The seven-entry step table and the three-entry exert table are the two
	// `ElysiumNpcSounds.cpp` already carries as vocalization data; the models, the seven
	// singles, the two emitters and the weapon are what had no body.
	return Precache10Case(*this, TEXT("CNPC_VSabbatLeader"),
		{
			GTroikaOnly,
			TEXT("model:models/character/monster/Andrei/andrei.mdl:1"),
			TEXT("model:models/character/npc/unique/hollywood/andrei/andrei_no_mouth.mdl:1"),
			TEXT("sound:character/monster/andrei_transformed/step1.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/step2.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/step3.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/step4.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/step5.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/step6.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/step7.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/exert_heavy_1.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/exert_heavy_2.wav:0"),
			TEXT("sound:character/monster/andrei_transformed/exert_heavy_3.wav:0"),
			TEXT("sound:Character/Monster/Andrei_Transformed/ambient_run.wav:0"),
			TEXT("sound:Character/Monster/Andrei_Transformed/Leap_Down_Attack_1.wav:0"),
			TEXT("sound:Character/Monster/Andrei_Transformed/dive_in_splash.wav:0"),
			TEXT("sound:Character/Monster/Andrei_Transformed/dive_out_splash.wav:0"),
			TEXT("sound:Character/Monster/Andrei_Transformed/splash_warning.wav:0"),
			TEXT("sound:Character/Monster/Andrei_Transformed/jump_retreat.wav:0"),
			TEXT("sound:Character/Monster/Andrei_Transformed/roar_1.wav:0"),
			TEXT("particle:Andrei_powerup_emitter:1"),
			TEXT("particle:Andrei_blast_emitter:1"),
			TEXT("other:item_w_sabbatleader_attack:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10SheriffManTest,
	"Elysium.Substrate.NpcKernelPrecache10.SheriffMan", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10SheriffManTest::RunTest(const FString&)
{
	// `0x103ae540` — the SAME `sheriff_teleport_emitter` duplicate the ManBat arm keeps, from the
	// same `.rdata` cell.
	return Precache10Case(*this, TEXT("CNPC_VSheriffMan"),
		{
			GTroikaOnly,
			TEXT("model:models/character/monster/manbat/manbat.mdl:1"),
			TEXT("particle:sheriff_landblast_emitter:1"),
			TEXT("particle:sheriff_teleport_emitter:1"),
			TEXT("particle:sheriff_teleport_emitter:1"),
			TEXT("other:item_w_sheriff_sword:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10TzimisceTest,
	"Elysium.Substrate.NpcKernelPrecache10.Tzimisce", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10TzimisceTest::RunTest(const FString&)
{
	// `0x103b8fa0` — base-LAST. Oracle: `docs/vtmb/footsteps.md`.
	return Precache10Case(*this, TEXT("CNPC_VTzimisce"),
		{
			TEXT("sound:character/monster/spiderchick/spi_footstep_indiv_1.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_footstep_indiv_2.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_footstep_indiv_3.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_footstep_indiv_4.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_footstep_indiv_5.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_footstep_indiv_6.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_attack_swish_1.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_attack_swish_2.wav:0"),
			TEXT("sound:character/monster/spiderchick/spi_attack_swish_3.wav:0"),
			TEXT("other:item_w_tzimisce_melee:0"),
			GTroikaOnly,
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10TzimisceHeadClawTest,
	"Elysium.Substrate.NpcKernelPrecache10.TzimisceHeadClaw", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10TzimisceHeadClawTest::RunTest(const FString&)
{
	// `0x103c1400` — the four preload-1 emitters are exactly the four the slot-332 grab body
	// spawns by name, and the fat guy's four footsteps arrive as TWO tables of two.
	return Precache10Case(*this, TEXT("CNPC_VTzimisceHeadClaw"),
		{
			GTroikaOnly,
			TEXT("particle:Tzim2_powerup_emitter:1"),
			TEXT("particle:Tzim2_blast_emitter:1"),
			TEXT("particle:Tzim2_player_emitter:1"),
			TEXT("particle:HUD_Tzim2_emitter:1"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step1.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step2.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step3.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step4.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Exert_Heavy_1.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Exert_Heavy_2.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Exert_Heavy_3.wav:0"),
			TEXT("sound:Character/Monster/TC_FatGuy/Sluge_Hit.wav:0"),
			TEXT("sound:Character/Monster/TC_FatGuy/Sluge_Affected.wav:0"),
			TEXT("other:item_w_tzimisce2_claw:0"),
			TEXT("other:item_w_tzimisce2_head:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10TzimisceRunnerTest,
	"Elysium.Substrate.NpcKernelPrecache10.TzimisceRunner", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10TzimisceRunnerTest::RunTest(const FString&)
{
	// `0x103c31e0` — four contiguous tables (2, 2, 4, 3). The 2+2 left/right split is the same one
	// `ElysiumFootsteps.cpp` already carries for this class.
	return Precache10Case(*this, TEXT("CNPC_VTzimisceRunner"),
		{
			GTroikaOnly,
			TEXT("sound:character/monster/TC_Runner/foot_steps_1.wav:0"),
			TEXT("sound:character/monster/TC_Runner/foot_steps_2.wav:0"),
			TEXT("sound:character/monster/TC_Runner/foot_steps_3.wav:0"),
			TEXT("sound:character/monster/TC_Runner/foot_steps_4.wav:0"),
			TEXT("sound:character/monster/TC_Runner/Breath1.wav:0"),
			TEXT("sound:character/monster/TC_Runner/Breath2.wav:0"),
			TEXT("sound:character/monster/TC_Runner/Breath3.wav:0"),
			TEXT("sound:character/monster/TC_Runner/Breath4.wav:0"),
			TEXT("sound:character/monster/TC_Runner/Exert_Heavy_1.wav:0"),
			TEXT("sound:character/monster/TC_Runner/Exert_Heavy_2.wav:0"),
			TEXT("sound:character/monster/TC_Runner/Exert_Heavy_3.wav:0"),
			TEXT("other:item_w_tzimisce3_claw:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10WerewolfTest,
	"Elysium.Substrate.NpcKernelPrecache10.Werewolf", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10WerewolfTest::RunTest(const FString&)
{
	// `0x103cb2a0`. Two facts pinned here and nowhere else: the two directory globs pass
	// `0x101d0f10`'s third argument CLEAR and its fourth SET — the reverse of the Troika body's own
	// pair — and the four-entry footstep table is the TZIMISCE FAT GUY's, which the listing
	// annotates at `103cb385`. The werewolf's own steps come through the sound GROUP it binds three
	// lines earlier; this table is a retail copy-paste and is reproduced, not corrected.
	FPrecache10Fixture Fix(TEXT("CNPC_VWerewolf"));
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	Precache10RunArm(*Fix.Species);
	Precache10CheckLog(*this, TEXT("CNPC_VWerewolf"), *Fix.Species,
		{
			GTroikaOnly,
			TEXT("dir:sound/Character/Monster/Werewolf:.wav:star=0:flag=1"),
			TEXT("dir:sound/Area/Special/Observatory:.wav:star=0:flag=1"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step1.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step2.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step3.wav:0"),
			TEXT("sound:character/monster/TC_FatGuy/Foot_Step4.wav:0"),
			TEXT("other:item_w_werewolf_attacks:0"),
			TEXT("sound:dev/ww_tele_out.wav:0"),
			TEXT("sound:dev/ww_tele_in.wav:0"),
		});
	// The sound-group triple, in retail's write order.
	TestEqual(TEXT("+0x00bc m_iVSoundTableIdx takes the literal 2"),
		Fix.Species->VSoundTableIndex, 2);
	TestEqual(TEXT("+0x00c0 m_iszVSoundGroup takes \"Werewolf\""),
		Fix.Species->VSoundGroupName, FString(TEXT("Werewolf")));
	// The SEAM: no VSound concept list is parsed in this runtime, so the group lookup takes
	// retail's own count-zero miss. Asserted as the recovered refusal, not worked around.
	TestEqual(TEXT("+0x00b4 m_iVSoundGroup takes the empty table's miss"),
		Fix.Species->VSoundGroupRow, static_cast<int32>(INDEX_NONE));
	return Precache10CheckTroikaControl(*this, Fix);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10ZombieTest,
	"Elysium.Substrate.NpcKernelPrecache10.Zombie", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10ZombieTest::RunTest(const FString&)
{
	// `0x103df120` — the two headshot emitters take preload **0**, and they are the assets
	// `CNPC_VZombie::OnTakeDamage` (`0x103e06d0`) names.
	return Precache10Case(*this, TEXT("CNPC_VZombie"),
		{
			GTroikaOnly,
			TEXT("particle:zombie_headshot_death_emitter:0"),
			TEXT("particle:zombie_headshot_dmg_emitter:0"),
			TEXT("other:item_w_zombie_fists:0"),
		});
}

// =================================================================================================
// The arm story 29c-1 ported and left unwired.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10CameraTest,
	"Elysium.Substrate.NpcKernelPrecache10.Camera", GPrecache10TestFlags)
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
		Precache10CheckLog(*this, Form, *Fix.Species, { TEXT("model:models/null.mdl:0") });
		TestEqual(TEXT("the keyfield took the camera's null-model fallback"), Fix.Species->Model,
			FString(TEXT("models/null.mdl")));
		TestEqual(TEXT("m_iInterestingPlaceGroups is cleared at precache"),
			Fix.Species->InterestingPlaceGroupMask, 0u);
		bOk &= Precache10CheckTroikaControl(*this, Fix);
	}
	return bOk;
}

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
	// The census is the input: each row's classname comes from `ElysiumNpcKernelClass::Find`.
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10MakerBaseTest,
	"Elysium.Substrate.NpcKernelPrecache10.MakerBase", GPrecache10TestFlags)
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
	"Elysium.Substrate.NpcKernelPrecache10.MakerFleshpile", GPrecache10TestFlags)
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
	"Elysium.Substrate.NpcKernelPrecache10.MakerZombie", GPrecache10TestFlags)
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

#endif   // WITH_DEV_AUTOMATION_TESTS
