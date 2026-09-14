#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Precache10**. Every expectation below is read off the decompiled C of the
// body it names and, where the decompiler folded an argument or a table index, off the listing —
// never off the checklist's one-line walk, which this family corrected in six places.
//
// The suite is in four parts: the two BASE bodies (`0x1027bb50`, `0x10298ad0`) and the dialogue
// chop they share; the SPECIES arms, one named case each, every one of them proving the species
// body for its retail class AND the Troika body for a plain `npc_VCop`; the two arms story 29c-1
// left unwired; and the three `CNPCMaker*` arms on their own type.
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

	FString Precache10OpText(const FElysiumNpc::FPrecacheOp& Op)
	{
		switch (Op.Channel)
		{
		case FElysiumNpc::EPrecacheChannel::Model:
			return FString::Printf(TEXT("model:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpc::EPrecacheChannel::Sound:
			return FString::Printf(TEXT("sound:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpc::EPrecacheChannel::Particle:
			return FString::Printf(TEXT("particle:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpc::EPrecacheChannel::Other:
			return FString::Printf(TEXT("other:%s:%d"), *Op.Name, Op.Flag);
		case FElysiumNpc::EPrecacheChannel::Directory:
			return FString::Printf(TEXT("dir:%s:%s:star=%d:flag=%d"), *Op.Name, *Op.Extension,
				Op.bStarPrefix ? 1 : 0, Op.Flag);
		}
		return TEXT("?");
	}

	TArray<FString> Precache10LogText(const TArray<FElysiumNpc::FPrecacheOp>& Log)
	{
		TArray<FString> Out;
		Out.Reserve(Log.Num());
		for (const FElysiumNpc::FPrecacheOp& Op : Log)
		{
			Out.Add(Precache10OpText(Op));
		}
		return Out;
	}

	// Stand this NPC as `RetailClass`, clear everything the previous arm wrote, and run slot 104.
	void Precache10RunArm(FElysiumNpc& Npc, const TCHAR* RetailClass)
	{
		Npc.SetRetailClassForTests(RetailClass);
		Npc.PrecacheLog.Reset();
		// Three arms WRITE the model keyfield (the Troika fallback, `CGeneric_NPC`'s and the
		// tentacle's), so the next arm must start from an unset keyfield or it would see the
		// previous one's fallback as an authored model.
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

	// One NPC that every species arm is driven through, plus the `npc_VCop` control whose
	// `RetailClass()` is null — story 29c-1's recovered fall-through, and so "a plain Troika NPC
	// with no species class".
	struct FPrecache10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Species = nullptr;
		FElysiumNpc* Troika = nullptr;

		FPrecache10Fixture()
			: World(Build())
		{
			Species = World.Npc(TEXT("species"));
			Troika = World.Npc(TEXT("troika"));
			FElysiumNpcWorldFixture::Quiet({ Species, Troika });
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("precache10"), 29140u);
			Builder.AddNpc(TEXT("species"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
			Builder.AddNpc(TEXT("troika"), FVector(200.0, 0.0, 0.0), TEXT("npc_VCop"));
			return Builder;
		}
	};

	// The Troika control, asserted by every species case: an `npc_VCop` takes no species arm and its
	// slot 104 is the Troika body alone.
	bool Precache10CheckTroikaControl(FAutomationTestBase& Test, FPrecache10Fixture& Fix)
	{
		if (Fix.Troika == nullptr)
		{
			Test.AddError(TEXT("the npc_VCop control did not spawn"));
			return false;
		}
		Test.TestNull(TEXT("npc_VCop's RetailClass() is null, which is its recovered answer"),
			Fix.Troika->RetailClass());
		Fix.Troika->PrecacheLog.Reset();
		Fix.Troika->Model.Reset();
		Fix.Troika->Precache();
		return Precache10CheckLog(Test, TEXT("the npc_VCop control"), *Fix.Troika,
			{ GTroikaOnly });
	}

	// The shape every species case is: run the arm, compare the whole log, and prove the control.
	bool Precache10Case(FAutomationTestBase& Test, const TCHAR* RetailClass,
		const TArray<FString>& Expected)
	{
		FPrecache10Fixture Fix;
		if (Fix.Species == nullptr)
		{
			Test.AddError(TEXT("the subject did not spawn"));
			return false;
		}
		Precache10RunArm(*Fix.Species, RetailClass);
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
	N.BasePrecache();
	TestEqual(TEXT("an unset m_spawnEquipment precaches nothing"), N.PrecacheLog.Num(), 0);

	// Arm 1b: the authored none sentinel is the one-character string `"0"` (`DAT_105399a0`), and
	// retail's two-byte `REPE CMPSB` against it is what skips it.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment = TEXT("0");
	N.BasePrecache();
	TestEqual(TEXT("the \"0\" sentinel precaches nothing"), N.PrecacheLog.Num(), 0);

	// Arm 1c: anything else goes through `UTIL_PrecacheOther` `0x101d0ec0`.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment = TEXT("item_w_glock_17c");
	N.BasePrecache();
	Precache10CheckLog(*this, TEXT("an authored m_spawnEquipment"), N,
		{ TEXT("other:item_w_glock_17c:0") });

	// Arm 1d: `"00"` is NOT the sentinel — retail compares the two bytes `'0'` and the NUL, so a
	// second character makes the compare fail on byte two and the string IS precached.
	N.PrecacheLog.Reset();
	N.AdditionalEquipment = TEXT("00");
	N.BasePrecache();
	Precache10CheckLog(*this, TEXT("\"00\" is not the sentinel"), N, { TEXT("other:00:0") });

	// Arm 2: slot 452 `LoadedSchedules` (vtable `+0x710`) decides the rest. A false answer prints
	// `"ERROR: Rejecting spawn of %s as error in NPC's schedules."`, `UTIL_Remove`s the entity and
	// RETURNS WITHOUT chaining the base.
	//
	// That arm is UNREACHABLE in this runtime and this case says so rather than weakening: nothing
	// here parses schedule text, so no class flag can be cleared and
	// `ElysiumNpcKernelSchedule.cpp`'s body answers true for every class. What is asserted is the
	// gate itself and the arm it therefore takes — the body falls through and the NPC is still
	// alive, which a reject would not leave behind.
	TestTrue(TEXT("slot 452 answers true for every class in this runtime"), N.LoadedSchedules());
	N.AdditionalEquipment.Reset();
	N.PrecacheLog.Reset();
	N.BasePrecache();
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
	// `CAI_BaseNPCTroika::Precache` `0x10298ad0`, slot 104.
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	N.SetRetailClassForTests(nullptr);   // no species class: the Troika body is what slot 104 runs

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
	FElysiumEntityDef& Def = Builder.AddNpc(TEXT("talker"));
	Def.Keys.Add(TEXT("dialogname"), TEXT("dlg/Downtown/Trip.dlg"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("talker"));
	if (!TestNotNull(TEXT("the talker spawned"), Npc))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	Npc->SetRetailClassForTests(nullptr);
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
	// EVERY slot-104 override row the census carries must be claimed by an arm. A class the arm
	// table does not know would silently take the Troika body, which is the one failure this
	// dispatch shape can have — so the census is the test's input, not a list typed here.
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	int32 Rows = 0;
	for (const FElysiumNpcClassSlot& Row : ElysiumNpcKernelShape::Overrides())
	{
		if (Row.Slot != GPrecacheSlotIndex)
		{
			continue;
		}
		++Rows;
		Fix.Species->SetRetailClassForTests(Row.Class);
		Fix.Species->PrecacheLog.Reset();
		Fix.Species->Model.Reset();
		TestTrue(*FString::Printf(TEXT("%s's slot-104 override is claimed by an arm"), Row.Class),
			Fix.Species->PrecacheSpecies());
	}
	// 31 rows over 28 distinct bodies: the three Chang forms share `0x1036ae60` and the two camera
	// forms share `0x103689c0`, which is why the table keys on the address.
	TestEqual(TEXT("the census carries 31 slot-104 override rows"), Rows, 31);

	// And a class with NO slot-104 row runs the Troika body: `CNPC_VHumanCombatant` is on the
	// Troika line and carries no override.
	Fix.Species->SetRetailClassForTests(TEXT("CNPC_VHumanCombatant"));
	TestNull(TEXT("CNPC_VHumanCombatant carries no slot-104 override"),
		ElysiumNpcKernelClass::OverrideOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VHumanCombatant")), GPrecacheSlotIndex));
	TestFalse(TEXT("so no species arm claims it"), Fix.Species->PrecacheSpecies());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10ThunkTest,
	"Elysium.Substrate.NpcKernelPrecache10.SpeciesThunk", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10ThunkTest::RunTest(const FString&)
{
	// Retail's chain call is a DIRECT `thunk_`, never a vtable dispatch, so a species body's own
	// `CAI_BaseNPCTroika::Precache` can never re-enter the species body. `FSpeciesDispatchScope` is
	// what makes that true here: while slot 104's species body runs, slot 104's dispatcher answers
	// "no species body". Without it `CNPC_VZombie`'s arm would recurse forever — so the fact that
	// this case TERMINATES with exactly one Troika model op is the assertion.
	return Precache10Case(*this, TEXT("CNPC_VZombie"),
		{
			GTroikaOnly,
			TEXT("particle:zombie_headshot_death_emitter:0"),
			TEXT("particle:zombie_headshot_dmg_emitter:0"),
			TEXT("other:item_w_zombie_fists:0"),
		});
}

// =================================================================================================
// The twenty-three species arms, in retail address order.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10CrowTest,
	"Elysium.Substrate.NpcKernelPrecache10.Crow", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10CrowTest::RunTest(const FString&)
{
	// `0x10358ec0` — the only arm that chains FIRST and then precaches, and the chain is
	// `CAI_BaseNPC::Precache` (a `CAI_BaseNPC` class), so NO Troika op appears at all: no model
	// fallback, no coordinator bind. A crow's model is hard-coded and a map cannot override it.
	return Precache10Case(*this, TEXT("CNPC_Crow"), { TEXT("model:models/crow.mdl:0") });
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10GenericNpcTest,
	"Elysium.Substrate.NpcKernelPrecache10.GenericNpc", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10GenericNpcTest::RunTest(const FString&)
{
	// `0x10359f70`, the Troika-line `CGeneric_NPC` (`npc_generic`). The empty model keyfield falls
	// back to the SABBAT FEMALE model — this class's recovered oddity — and the Troika body then
	// precaches that same fallback when the chain runs LAST.
	return Precache10Case(*this, TEXT("CGeneric_NPC"),
		{
			TEXT("model:models/character/npc/sabbat/sabbat_female.mdl:0"),
			TEXT("sound:npc/metropolice/alert1.wav:0"),
			TEXT("sound:npc/metropolice/surprise1.wav:0"),
			TEXT("sound:npc/metropolice/die1.wav:0"),
			TEXT("sound:npc/citizen/pain1.wav:0"),
			TEXT("sound:npc/citizen/pain2.wav:0"),
			TEXT("sound:npc/citizen/pain3.wav:0"),
			TEXT("sound:npc/citizen/pain4.wav:0"),
			// The chain, LAST — and it sees the fallback this arm already wrote.
			TEXT("model:models/character/npc/sabbat/sabbat_female.mdl:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10BathackTest,
	"Elysium.Substrate.NpcKernelPrecache10.GenericNpcBathack", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10BathackTest::RunTest(const FString&)
{
	// `0x1035ade0` — `models/bats.mdl` hard-coded, its own copy of the four names, and
	// `CAI_BaseNPC::Precache` LAST (so no Troika op). The decompiled C shows the surprise1 call
	// with ONE argument; the listing at `1035ae13` shows `PUSH 0x0` before all four, so there is no
	// stack quirk to reproduce.
	return Precache10Case(*this, TEXT("CGeneric_NPC_bathack"),
		{
			TEXT("model:models/bats.mdl:0"),
			TEXT("sound:npc/metropolice/alert1.wav:0"),
			TEXT("sound:npc/metropolice/surprise1.wav:0"),
			TEXT("sound:npc/metropolice/die1.wav:0"),
			TEXT("sound:npc/citizen/pain1.wav:0"),
			TEXT("sound:npc/citizen/pain2.wav:0"),
			TEXT("sound:npc/citizen/pain3.wav:0"),
			TEXT("sound:npc/citizen/pain4.wav:0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10SabbatNpcTest,
	"Elysium.Substrate.NpcKernelPrecache10.GenericSabbatNpc", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10SabbatNpcTest::RunTest(const FString&)
{
	// `0x1035b5d0` — `thunk_FUN_10207e60` FIRST (the char template's `+0x78` model, a seam that
	// answers the empty string, which is also retail's answer for a null template), then the same
	// fallback and the same four names, and `CAI_BaseNPC::Precache` LAST.
	return Precache10Case(*this, TEXT("CGenericSabbat_NPC"),
		{
			TEXT("model::0"),   // the char-template seam: an empty name, precached all the same
			TEXT("model:models/character/npc/sabbat/sabbat_female.mdl:0"),
			TEXT("sound:npc/metropolice/alert1.wav:0"),
			TEXT("sound:npc/metropolice/surprise1.wav:0"),
			TEXT("sound:npc/metropolice/die1.wav:0"),
			TEXT("sound:npc/citizen/pain1.wav:0"),
			TEXT("sound:npc/citizen/pain2.wav:0"),
			TEXT("sound:npc/citizen/pain3.wav:0"),
			TEXT("sound:npc/citizen/pain4.wav:0"),
		});
}

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
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	for (const TCHAR* Form : { TEXT("CNPC_VChangBros"), TEXT("CNPC_VChangBrosBlade"),
		TEXT("CNPC_VChangBrosClaw") })
	{
		Precache10RunArm(*Fix.Species, Form);
		Precache10CheckLog(*this, Form, *Fix.Species, Expected);
	}
	return Precache10CheckTroikaControl(*this, Fix);
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
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	Precache10RunArm(*Fix.Species, TEXT("CNPC_VMingXiaoTentacle"));
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
		Fix.Species->ModeIndexTentacleToGrub, Fix.Species->ModeIndexGrub);
	TestNotEqual(TEXT("+0x666c is the transformation model's, a different index"),
		Fix.Species->ModeIndexGrubToProxy, Fix.Species->ModeIndexGrub);
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
	// `ElysiumNpcKernelSounds.cpp` already carries as vocalization data; the models, the seven
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10TestNpcTest,
	"Elysium.Substrate.NpcKernelPrecache10.TestNpc", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10TestNpcTest::RunTest(const FString&)
{
	// `0x103b41e0` — twelve sounds and only THEN the Troika body. Base-last, like `CNPC_VTzimisce`.
	// The decompiled C shows surprise1 with ONE argument; the listing at `103b427f` shows `PUSH
	// 0x0` before all twelve, so the checklist's "retail stack quirk" is a decompiler artifact and
	// is NOT reproduced.
	return Precache10Case(*this, TEXT("CNPC_VTest"),
		{
			TEXT("sound:character/npc/test/death1.wav:0"),
			TEXT("sound:character/npc/test/alert1.wav:0"),
			TEXT("sound:character/npc/test/idle1.wav:0"),
			TEXT("sound:character/npc/test/pain1.wav:0"),
			TEXT("sound:character/npc/test/pain2.wav:0"),
			TEXT("sound:character/npc/test/pain3.wav:0"),
			TEXT("sound:character/npc/test/pain4.wav:0"),
			TEXT("sound:character/npc/test/fear1.wav:0"),
			TEXT("sound:character/npc/test/lostenemy1.wav:0"),
			TEXT("sound:character/npc/test/foundenemy1.wav:0"),
			TEXT("sound:character/npc/test/surprise1.wav:0"),
			TEXT("sound:character/npc/test/knockout1.wav:0"),
			GTroikaOnly,
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
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	Precache10RunArm(*Fix.Species, TEXT("CNPC_VWerewolf"));
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
// The two arms story 29c-1 ported and left unwired.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10GenericNpcLineTest,
	"Elysium.Substrate.NpcKernelPrecache10.GenericNpcLine", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10GenericNpcLineTest::RunTest(const FString&)
{
	// `CGenericNPC::Precache` `0x1034aa40` — family Lifecycle's `GenericNpcPrecache`, now reached
	// through slot 104. The body chains NOTHING: no Troika op and no base op, which is why this is
	// four entries and not five. Two of the three table names are unrecovered and carried by index.
	return Precache10Case(*this, TEXT("CGenericNPC"),
		{
			TEXT("sound:weapons/ar2/ar2_fire1.wav:0"),
			TEXT("sound:PTR_s_weapons_ar2_ar2_fire1_wav_106244c0[1]:0"),
			TEXT("sound:PTR_s_weapons_ar2_ar2_fire1_wav_106244c0[2]:0"),
			// The entity's own model keyfield, unset on the fixture row and precached as the empty
			// string — this class runs no fallback at all.
			TEXT("model::0"),
		});
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelPrecache10CameraTest,
	"Elysium.Substrate.NpcKernelPrecache10.Camera", GPrecache10TestFlags)
bool FElysiumNpcKernelPrecache10CameraTest::RunTest(const FString&)
{
	// `CNPC_VCamera::Precache` `0x103689c0`, shared with `CNPC_VCameraSecurity`. The model fallback
	// is family Lifecycle's `CameraPrecacheModel`; the slot-452 reject arm and the
	// `m_iInterestingPlaceGroups = 0` write are the tail that family left for a later story.
	FPrecache10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	for (const TCHAR* Form : { TEXT("CNPC_VCamera"), TEXT("CNPC_VCameraSecurity") })
	{
		Fix.Species->InterestingPlaceGroupMask = 0xffu;
		Precache10RunArm(*Fix.Species, Form);
		Precache10CheckLog(*this, Form, *Fix.Species, { TEXT("model:models/null.mdl:0") });
		TestEqual(TEXT("the keyfield took the camera's null-model fallback"), Fix.Species->Model,
			FString(TEXT("models/null.mdl")));
		TestEqual(TEXT("m_iInterestingPlaceGroups is cleared at precache"),
			Fix.Species->InterestingPlaceGroupMask, 0u);
	}
	return Precache10CheckTroikaControl(*this, Fix);
}

// =================================================================================================
// The three `CNPCMaker*` arms, on `FElysiumNpcMaker`.
// =================================================================================================

namespace
{
	// One maker of a given classname, with its two keyfields set. `Classname` must be one of the
	// two `ElysiumNpcClasses.cpp` registers against `FElysiumNpcMaker` — `npc_maker` and
	// `npc_maker_fleshpile` — because anything else stands a different leaf and the downcast below
	// would be a lie. `npc_maker_zombie` is NOT one of them; see `MakerZombie`.
	struct FPrecache10MakerFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpcMaker* Maker = nullptr;

		/** What `Spawn`'s own slot-104 dispatch precached, before any case drove `Precache()` by
		 *  hand. See the constructor. */
		TArray<FElysiumNpc::FPrecacheOp> SpawnPrecacheLog;
		bool bDeadAfterSpawn = false;

		FPrecache10MakerFixture(const TCHAR* Classname, const TCHAR* ModelKey, const TCHAR* NpcType)
			: World(Build(Classname, ModelKey, NpcType))
		{
			check(FString(Classname) == TEXT("npc_maker")
				|| FString(Classname) == TEXT("npc_maker_fleshpile"));
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
		const TArray<FString>& Expected, const TArray<FElysiumNpc::FPrecacheOp>& Log)
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
	// The fixture stands an `npc_maker` and TELLS it it is the zombie variant. That is not a
	// shortcut around the spawn path, it is the only way in: `ElysiumNpcClasses.cpp` registers no
	// `npc_maker_zombie`, so no map in this runtime can stand a `CNPCMaker_Zombie` — a
	// spawn-registration gap the census contradicts (`CNPCMaker_Zombie_Classnames`) and the shipped
	// maps use. Same instrument family Species used for the twelve unspawnable species slots.
	{
		FPrecache10MakerFixture Fix(TEXT("npc_maker"), TEXT("models/z.mdl"), TEXT("npc_VZombie"));
		if (!TestNotNull(TEXT("the maker spawned"), Fix.Maker))
		{
			return false;
		}
		Fix.Maker->SetZombieMakerForTests();
		TestTrue(TEXT("the zombie arm is selected"), Fix.Maker->IsZombieMaker());
		// Set both words by hand: nothing in this port writes them (the maker class table registers
		// no such keyfield), and the point of the arm is that it discards them.
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
		FPrecache10MakerFixture Empty(TEXT("npc_maker"), nullptr, TEXT("npc_VZombie"));
		if (!TestNotNull(TEXT("the second maker spawned"), Empty.Maker))
		{
			return false;
		}
		Empty.Maker->SetZombieMakerForTests();
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
