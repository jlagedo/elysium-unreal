#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Misc** — the 37 layer 0–9 rows no other family owns. Every assertion below
// comes from the decompiled C or the listing of the row it names: the slop window is one second
// because `0x10295414` subtracts `_DAT_104454c0`, the Lasombra arm answers TRUE because the flag
// word lands in AL, Bach's gate is an OR because `0x1036450f` is a `JP` straight to the accept,
// and the melee-interrupt whitelist is the switch's own member set including the hoisted `0x51`.
//
// Where a body can only answer "nothing" because its input is a seam — the six component factories,
// the standoff schedule latch, the zone trigger, the melee move records, the arena-centre hint walk
// — the case says so: that the seam is asked and that the refusal is the recovered one.

static constexpr EAutomationTestFlags GElysiumNpcKernelMiscFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The two classnames the Troika-line/own-line cases here spawn out of `ElysiumNpcClasses.cpp`.
	// story 5 step 2: every classname builds the class retail's factory builds (population.md),
	// so `npc_VCop` (factory 0x103704f0) is `CNPC_VCop`, not the Troika line; a case that wants
	// the bare Troika line stands `AddTroikaNpc`, whose `RetailClass()` answers null.
	const TCHAR* const GMiscSpawnableCombatant = TEXT("npc_VHumanCombatant");
	const TCHAR* const GMiscSpawnableSabbatLeader = TEXT("npc_VSabbatLeader");
}

// -------------------------------------------------------------------------------------------------
// `CAI_StandoffBehavior` — slots 4, 26 and 28.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscStandoffTest,
	"Elysium.Substrate.NpcKernelMisc.Standoff", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscStandoffTest::RunTest(const FString&)
{
	// Slot 28 (`0x102c7ef0`): the whole body is `return DAT_10601874`, and that latch is only ever
	// raised by `CAI_StandoffBehavior`'s class initialiser, which this runtime has no loader for.
	TestFalse(TEXT("0x102c7ef0 answers the unloaded latch"), FElysiumNpcBase::StandoffVfunc28());

	// Slot 26 (`0x102c7dd0`): two INDEPENDENT tests, not an if/else.
	{
		FElysiumNpcBase::FStandoffWords Words;
		FElysiumNpcBase::FStandoffAimWords Aim;
		Aim.AimMode = 0;
		Aim.AimWord0x58 = 7.f;
		Aim.AimWord0x5c = 7.f;
		Aim.AimWord0x60 = 7.f;
		FElysiumNpcBase::StandoffVfunc26(Words, Aim);
		TestEqual(TEXT("mode 0 resets nothing (+0x58)"), Aim.AimWord0x58, 7.f);
		TestEqual(TEXT("mode 0 resets nothing (+0x5c)"), Aim.AimWord0x5c, 7.f);
		TestEqual(TEXT("mode 0 resets nothing (+0x60)"), Aim.AimWord0x60, 7.f);
		TestFalse(TEXT("mode 0 does not latch bSawNewEnemy"), Words.bSawNewEnemy);
	}
	{
		FElysiumNpcBase::FStandoffWords Words;
		FElysiumNpcBase::FStandoffAimWords Aim;
		Aim.AimMode = 1;
		FElysiumNpcBase::StandoffVfunc26(Words, Aim);
		TestEqual(TEXT("mode 1 writes 5.0 into +0x5c"), Aim.AimWord0x5c, 5.f);
		TestEqual(TEXT("mode 1 writes 0.0 into +0x60"), Aim.AimWord0x60, 0.f);
		TestEqual(TEXT("mode 1 writes -1.0 into +0x58"), Aim.AimWord0x58, -1.f);
		TestFalse(TEXT("mode 1 does NOT latch bSawNewEnemy"), Words.bSawNewEnemy);
	}
	{
		FElysiumNpcBase::FStandoffWords Words;
		FElysiumNpcBase::FStandoffAimWords Aim;
		Aim.AimMode = 2;
		FElysiumNpcBase::StandoffVfunc26(Words, Aim);
		TestEqual(TEXT("mode 2 takes the reset arm too"), Aim.AimWord0x5c, 5.f);
		TestTrue(TEXT("and mode 2 ALSO latches bSawNewEnemy"), Words.bSawNewEnemy);
	}

	// Slot 4 (`0x102c7490`) needs an owner for `m_flDistTooFar` and `+0x1fc`.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_standoff"), 0x102c7490);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	const double Now = Fixture.World.NowSeconds();

	// The `ReactionDelayMax == 0.0f` arm: the min alone, no draw.
	{
		Npc->DistTooFar = 900.f;
		Npc->Field_0x01fc = 0;
		FElysiumNpcBase::FStandoffWords Words;
		Words.ReactionDelayMin = 3.f;
		Words.ReactionDelayMax = 0.f;
		Words.ReactionsLeft = 4;
		FElysiumNpcBase::FStandoffAimWords Aim;
		Aim.bForcesOwnerWord0x1fc = false;
		Npc->StandoffVfunc4(Words, Aim);
		TestTrue(TEXT("slot 4 latches bSawNewEnemy"), Words.bSawNewEnemy);
		TestEqual(TEXT("slot 4 zeroes ReactionsLeft"), Words.ReactionsLeft, 0);
		TestEqual(TEXT("a zero max uses the min alone"), Words.NextReactionAt, Now + 3.0, 1e-6);
		TestEqual(TEXT("the owner's m_flDistTooFar is PARKED in +0x50"), Npc->StandoffDistTooFar,
			900.f);
		TestTrue(TEXT("and FLT_MAX stands in its place"), Npc->DistTooFar > 3.0e38f);
		TestEqual(TEXT("+0x1a clear leaves the owner's +0x1fc alone"), Npc->Field_0x01fc, 0);
	}

	// The ranged arm draws, and stays inside the bounds.
	{
		FElysiumNpcBase::FStandoffWords Words;
		Words.ReactionDelayMin = 2.f;
		Words.ReactionDelayMax = 6.f;
		FElysiumNpcBase::FStandoffAimWords Aim;
		Aim.bForcesOwnerWord0x1fc = true;
		Npc->StandoffVfunc4(Words, Aim);
		TestTrue(TEXT("a non-zero max draws inside [min, max]"),
			Words.NextReactionAt >= Now + 2.0 && Words.NextReactionAt <= Now + 6.0);
		TestEqual(TEXT("+0x1a set forces the owner's +0x1fc to 1"), Npc->Field_0x01fc, 1);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 272 `AllocateLayer` and slot 296 — `0x10099470`, `0x10348ba0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscLayerAndGrappleTest,
	"Elysium.Substrate.NpcKernelMisc.LayerAndGrapple", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscLayerAndGrappleTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_layer"), 0x10099470);
	Builder.AddNpc(TEXT("npc"));
	Builder.AddNpc(TEXT("victim"), FVector(100.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpc* Victim = Fixture.Npc(TEXT("victim"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	TestNotNull(TEXT("the partner spawned"), Victim);
	if (Npc == nullptr || Victim == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Victim });

	// `0x10099470`: the lowest slot whose weight is exactly 0.0f, starting at 0 (slot 267 answers 0
	// for every class), -1 when all four are held. Four slots and no eviction.
	TestEqual(TEXT("an empty table allocates slot 0"), Npc->AllocateLayer(), 0);
	Npc->SetOverlayLayer(0, 0x30, 11, false);
	TestEqual(TEXT("with slot 0 live the next is 1"), Npc->AllocateLayer(), 1);
	Npc->SetOverlayLayer(1, 0x31, 12, false);
	Npc->SetOverlayLayer(2, 0x32, 13, false);
	Npc->SetOverlayLayer(3, 0x33, 14, false);
	TestEqual(TEXT("all four held answers -1, not an eviction"), Npc->AllocateLayer(), INDEX_NONE);

	// `0x10348ba0`: three grapple terms then `param_1 == 0xb`.
	TestFalse(TEXT("an unpaired NPC refuses every argument"), Npc->Slot296(0xb));
	Npc->Grapple.Role = EElysiumGrappleRole::Attacker;
	Npc->Grapple.Partner = Victim->Handle;
	TestTrue(TEXT("paired and resolving, 0xb answers true"), Npc->Slot296(0xb));
	TestFalse(TEXT("and every other argument answers false"), Npc->Slot296(0xa));
	TestFalse(TEXT("including 0"), Npc->Slot296(0));
	Npc->Grapple.Role = EElysiumGrappleRole::None;
	TestFalse(TEXT("role -1 refuses even with a live partner"), Npc->Slot296(0xb));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 347 `GetExpressionEventParams` — `0x102c1c10` over the base `0x10014ba0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscExpressionEventTest,
	"Elysium.Substrate.NpcKernelMisc.ExpressionEventParams", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscExpressionEventTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_expr"), 0x102c1c10);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	TCHAR Name[FElysiumNpc::ExpressionEventNameChars];
	float A = -1.f;
	float B = -1.f;
	float C = -1.f;
	float D = -1.f;

	// Event 1 is the Troika override's own arm: it keeps the base's name and its first, second and
	// fourth floats and replaces only the third (`2.0` -> `0.8`).
	Name[0] = TEXT('\0');
	TestTrue(TEXT("event 1 is answered"), Npc->GetExpressionEventParams(1, Name, &A, &B, &C, &D));
	TestEqual(TEXT("event 1 names Knockback"), FString(Name), FString(TEXT("Knockback")));
	TestEqual(TEXT("event 1 A is 1.0"), A, 1.f);
	TestEqual(TEXT("event 1 B is 0.25"), B, 0.25f);
	TestEqual(TEXT("event 1 C is 0.8, the Troika replacement for the base's 2.0"), C, 0.8f);
	TestEqual(TEXT("event 1 D is 0.5"), D, 0.5f);

	// The extension term: `SelectHeaviestSequence(m_knockbackType, -1)` is a seam answering
	// INDEX_NONE, so the `seq >= 0` arm is NOT taken and C stays flat. That is the recovered
	// refusal, and it must not change when the knockback type does.
	Npc->KnockbackType = 0x21;
	C = -1.f;
	Npc->GetExpressionEventParams(1, Name, &A, &B, &C, &D);
	TestEqual(TEXT("with the sequence seam refusing, C keeps the flat 0.8"), C, 0.8f);
	TestEqual(TEXT("the seam answers INDEX_NONE"), Npc->SelectHeaviestSequence(0x21, INDEX_NONE),
		INDEX_NONE);

	// Event 0 is NOT special-cased by the override and falls to the base's own arm.
	Name[0] = TEXT('\0');
	TestTrue(TEXT("event 0 is answered by the base"),
		Npc->GetExpressionEventParams(0, Name, &A, &B, &C, &D));
	TestEqual(TEXT("event 0 names Knockback too"), FString(Name), FString(TEXT("Knockback")));
	TestEqual(TEXT("event 0 A is 1.0"), A, 1.f);
	TestEqual(TEXT("event 0 B is 0.1"), B, 0.1f);
	TestEqual(TEXT("event 0 C is 1.0"), C, 1.f);
	TestEqual(TEXT("event 0 D is 0.2"), D, 0.2f);

	// Everything outside [0, 2) is refused by the base.
	TestFalse(TEXT("event 2 is refused"), Npc->GetExpressionEventParams(2, Name, &A, &B, &C, &D));
	TestFalse(TEXT("event -1 is refused"), Npc->GetExpressionEventParams(-1, Name, &A, &B, &C, &D));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 513 `CapabilitiesGet` — `0x1026db30`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscCapabilitiesGetTest,
	"Elysium.Substrate.NpcKernelMisc.CapabilitiesGet", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscCapabilitiesGetTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_caps"), 0x1026db30);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `m_afCapability` verbatim, bit for bit, with no weapon.
	Npc->CapabilityWord = 0;
	TestEqual(TEXT("no capability word answers 0"), Npc->CapabilitiesGet(), 0);
	Npc->CapabilityWord = 0x4000040;   // bits_CAP_SQUAD | bits_CAP_MOVE_SHOOT
	TestEqual(TEXT("the whole word is answered verbatim"), Npc->CapabilitiesGet(), 0x4000040);

	// The OR term is the active weapon's slot 360 (`+0x5a0`), which family Motor's seam answers 0
	// for — so the answer cannot grow past `m_afCapability` today. Asserting the seam's refusal is
	// the point: the day slot 360 lands, this case is what says the OR is live.
	TestEqual(TEXT("the weapon capability seam answers 0"),
		static_cast<int32>(Npc->ActiveWeaponCapabilityWord()), 0);
	TestEqual(TEXT("so the OR adds nothing"), Npc->CapabilitiesGet(), Npc->CapabilityWord);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slots 424–430 — the component factories.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscComponentFactoryTest,
	"Elysium.Substrate.NpcKernelMisc.ComponentFactories", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscComponentFactoryTest::RunTest(const FString&)
{
	// Every row by name, against the census: the class exists, and the census agrees that this body
	// fills that slot for it.
	int32 Count = 0;
	const FElysiumNpc::FComponentFactory* Rows = FElysiumNpc::ComponentFactoryRows(Count);
	TestEqual(TEXT("six Troika-line factories plus the rat's live override, plus slot 424"), Count,
		8);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpc::FComponentFactory& Row = Rows[Index];
		const FString Name(Row.RetailClass);
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), *Name), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot %d with %s"), *Name, Row.Slot, Row.Body),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, Row.Slot)), FString(Row.Body));
	}

	// The recovered allocation sizes, by row — the one fact each factory exists to state. The rat's
	// local navigator is the same size as its base but a different constructor, which is why size
	// alone does not identify a row. (`CAI_BaseHumanoid`'s motor/navigator rows have no instance
	// and were deleted by 0019 story 5 step 1.)
	auto RowFor = [Rows, Count](const TCHAR* Class, int32 Slot)
		-> const FElysiumNpc::FComponentFactory*
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Rows[Index].Slot == Slot && FCString::Stricmp(Rows[Index].RetailClass, Class) == 0)
			{
				return &Rows[Index];
			}
		}
		return nullptr;
	};
	const FElysiumNpc::FComponentFactory* BaseMotor = RowFor(TEXT("CAI_BaseNPC"), 427);
	const FElysiumNpc::FComponentFactory* BaseNav = RowFor(TEXT("CAI_BaseNPC"), 429);
	const FElysiumNpc::FComponentFactory* BaseLocalNav = RowFor(TEXT("CAI_BaseNPC"), 428);
	const FElysiumNpc::FComponentFactory* RatLocalNav = RowFor(TEXT("CNPC_VRat"), 428);
	const FElysiumNpc::FComponentFactory* Senses = RowFor(TEXT("CAI_BaseNPC"), 425);
	const FElysiumNpc::FComponentFactory* Probe = RowFor(TEXT("CAI_BaseNPC"), 426);
	const FElysiumNpc::FComponentFactory* Pathfinder = RowFor(TEXT("CAI_BaseNPC"), 430);
	if (BaseMotor == nullptr || BaseNav == nullptr || BaseLocalNav == nullptr
		|| RatLocalNav == nullptr
		|| Senses == nullptr || Probe == nullptr || Pathfinder == nullptr)
	{
		AddError(TEXT("a named component-factory row is missing"));
		return false;
	}
	TestEqual(TEXT("CAI_Senses is 0x88 bytes"), Senses->SizeBytes, 0x88);
	TestEqual(TEXT("CAI_MoveProbe is 0x14 bytes"), Probe->SizeBytes, 0x14);
	TestEqual(TEXT("CAI_Pathfinder is 0x18 bytes"), Pathfinder->SizeBytes, 0x18);
	TestEqual(TEXT("the base motor is 0x6c bytes"), BaseMotor->SizeBytes, 0x6c);
	TestEqual(TEXT("the base motor is built by 0x102e0900"), FString(BaseMotor->Constructor),
		FString(TEXT("0x102e0900")));
	TestEqual(TEXT("the base navigator is 0x68 bytes"), BaseNav->SizeBytes, 0x68);
	TestEqual(TEXT("the base navigator is built by 0x102eca50"), FString(BaseNav->Constructor),
		FString(TEXT("0x102eca50")));
	TestEqual(TEXT("the rat's local navigator is the SAME size as the base's"),
		RatLocalNav->SizeBytes, BaseLocalNav->SizeBytes);
	TestNotEqual(TEXT("but a different constructor"), FString(RatLocalNav->Constructor),
		FString(BaseLocalNav->Constructor));

	// The per-class resolve, and the bare Troika line's fall-through.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_components"), 0x1027cae0);
	Builder.AddNpc(TEXT("rat"), FVector::ZeroVector, TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("cop"), FVector(400.0, 0.0, 0.0), TEXT("npc_VCop"));
	Builder.AddTroikaNpc(TEXT("troika"), FVector(800.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	TestNotNull(TEXT("the rat spawned"), Rat);
	TestNotNull(TEXT("the cop spawned"), Cop);
	TestNotNull(TEXT("the bare Troika NPC stood"), Troika);
	if (Rat == nullptr || Cop == nullptr || Troika == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Rat, Cop, Troika });

	// Slot 428 through the vtable (story 5 step 3): the rat's own override answers `CNPC_VRat`'s
	// `0x103ad6a0`, and every class whose chain replaces nothing at 428 answers the base body.
	auto BodyAt428 = [](FElysiumNpc* Npc) -> FString
	{
		Npc->LastComponentFactoryBody = nullptr;
		Npc->CreateLocalNavigator();
		return Npc->LastComponentFactoryBody != nullptr ? FString(Npc->LastComponentFactoryBody)
			: FString();
	};
	TestEqual(TEXT("the rat's slot 428 is CNPC_VRat's own 0x103ad6a0"), BodyAt428(Rat),
		FString(TEXT("0x103ad6a0")));
	int32 RowCount = 0;
	const FElysiumNpc::FComponentFactory* FactoryRows = FElysiumNpc::ComponentFactoryRows(RowCount);
	bool bRatRow = false;
	for (int32 Index = 0; Index < RowCount; ++Index)
	{
		bRatRow |= FactoryRows[Index].Slot == 428
			&& FCString::Strcmp(FactoryRows[Index].Body, TEXT("0x103ad6a0")) == 0
			&& FactoryRows[Index].SizeBytes == 0x20;
	}
	TestTrue(TEXT("and the table carries its 0x20-byte row"), bRatRow);
	TestNull(TEXT("the bare Troika line has no species class, so RetailClass() answers null"),
		Troika->RetailClass());
	TestEqual(TEXT("the bare Troika NPC answers the Troika line's 0x1027cf60"), BodyAt428(Troika),
		FString(TEXT("0x1027cf60")));
	// story 5 step 2: npc_VCop's factory 0x103704f0 builds CNPC_VCop (population.md). Slot 428's
	// only species override is `CNPC_VRat`'s (`docs/vtmb/npc-kernel/slots.md`), so the cop's chain
	// (CNPC_VCop -> CNPC_VHumanCombatant -> ...) inherits the base body.
	const FElysiumNpcClass* CopClass = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCop"));
	TestTrue(TEXT("npc_VCop's RetailClass() is CNPC_VCop — its factory builds it"),
		CopClass != nullptr && Cop->RetailClass() == CopClass);
	TestEqual(TEXT("the inherited base body, since CNPC_VCop's chain replaces nothing at 428"),
		BodyAt428(Cop), FString(TEXT("0x1027cf60")));

	// The six seams, each asked, each refusing, and `CreateComponents` running retail's chain over
	// them. The ORDER is the fact the refusal records: slot 425 first, NOT the lowest slot number.
	Rat->ComponentFactoryRefusals = 0;
	TestNull(TEXT("CreateSenses answers null"), Rat->CreateSenses());
	TestNull(TEXT("CreateMoveProbe answers null"), Rat->CreateMoveProbe());
	TestNull(TEXT("CreateMotor answers null"), Rat->CreateMotor());
	TestNull(TEXT("CreateLocalNavigator answers null"), Rat->CreateLocalNavigator());
	TestNull(TEXT("CreateNavigator answers null"), Rat->CreateNavigator());
	TestNull(TEXT("CreatePathfinder answers null"), Rat->CreatePathfinder());
	TestEqual(TEXT("all six seams were asked"), Rat->ComponentFactoryRefusals, 6);

	Rat->ComponentFactoryRefusals = 0;
	TestFalse(TEXT("CreateComponents bails false on the first null, as retail does"),
		Rat->CreateComponents());
	TestEqual(TEXT("and it asked exactly one factory before bailing"),
		Rat->ComponentFactoryRefusals, 1);
	TestEqual(TEXT("the first link of retail's chain is slot 425, not slot 424 or 426"),
		Rat->FirstRefusedComponentSlot, 425);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 590 `OkToInterruptForMelee` and the `0x1028a190` gate.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscMeleeInterruptTest,
	"Elysium.Substrate.NpcKernelMisc.OkToInterruptForMelee", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscMeleeInterruptTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_melee_interrupt"), 0x1029f940);
	Builder.AddNpc(TEXT("troika"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddNpc(TEXT("leader"), FVector(500.0, 0.0, 0.0), GMiscSpawnableSabbatLeader);
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	TestNotNull(TEXT("the combatant spawned"), Troika);
	TestNotNull(TEXT("the Sabbat leader spawned"), Leader);
	if (Troika == nullptr || Leader == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Troika, Leader });

	// The gate first (`0x1028a190`): a live, unchoreographed, non-oblivious NPC passes.
	TestTrue(TEXT("0x1028a190 passes a live idle NPC"), Troika->OkToDisturb());
	Troika->bInChoreoScene = true;
	TestFalse(TEXT("m_bInChoreoScene (+0x5bc4) refuses"), Troika->OkToDisturb());
	Troika->bInChoreoScene = false;
	// `+0x14bc & 0x1000` is `MADE_OBLIVIOUS`, and this address IS a reader of it — the bit
	// `ElysiumNpcFlags.h` records as having none.
	Troika->NpcFlags.Set(EElysiumNpcFlag2::MADE_OBLIVIOUS);
	TestFalse(TEXT("m_bfAINPCFlags2 & 0x1000 refuses"), Troika->OkToDisturb());
	Troika->NpcFlags.Clear(EElysiumNpcFlag2::MADE_OBLIVIOUS);
	TestTrue(TEXT("and clearing it lets the gate open again"), Troika->OkToDisturb());

	// The whitelist. These are the switch's own members, including `0x51` — the one the compiler
	// hoisted out of the jump table with a `!= 0x51` test that falls THROUGH to the accept.
	const int32 Accepted[] = { 0x1, 0x9, 0x13, 0x30, 0x4b, 0x4d, 0x51, 0x73, 0x80, 0x8a, 0xcb5,
		0xd25, 0x1121, 0x1157, 0x1158 };
	for (int32 Activity : Accepted)
	{
		Troika->ActivityNumber = Activity;
		TestTrue(*FString::Printf(TEXT("activity 0x%x is melee-interruptible"), Activity),
			Troika->OkToInterruptForMelee());
	}
	// Just outside every boundary, and two ordinary activities inside none of the bands.
	const int32 Refused[] = { 0x0, 0x2, 0x8, 0xa, 0x14, 0x50, 0x52, 0x72, 0x8b, 0xcb4, 0xcb6,
		0xd24, 0xd26, 0x1120, 0x1122, 0x1156, 0x1159 };
	for (int32 Activity : Refused)
	{
		Troika->ActivityNumber = Activity;
		TestFalse(*FString::Printf(TEXT("activity 0x%x is not"), Activity),
			Troika->OkToInterruptForMelee());
	}

	// The gate is FIRST: a refused gate beats every whitelisted activity.
	Troika->ActivityNumber = 0x1;
	Troika->bInChoreoScene = true;
	TestFalse(TEXT("the gate is tested before the whitelist"), Troika->OkToInterruptForMelee());
	Troika->bInChoreoScene = false;

	// `CNPC_VSabbatLeader::OkToInterruptForMelee` (`0x103ab400`): activity 0x1141 is an EXCEPTION
	// that skips the gate entirely, and everything else falls to the Troika line.
	TestEqual(TEXT("npc_VSabbatLeader resolves to CNPC_VSabbatLeader"),
		FString(Leader->RetailClass() != nullptr ? Leader->RetailClass()->Name : TEXT("")),
		FString(TEXT("CNPC_VSabbatLeader")));
	TestEqual(TEXT("which fills slot 590 with 0x103ab400"),
		FString(ElysiumNpcKernelClass::BodyOf(Leader->RetailClass(), 590)),
		FString(TEXT("0x103ab400")));
	Leader->ActivityNumber = 0x1141;
	Leader->bInChoreoScene = true;
	TestTrue(TEXT("activity 0x1141 answers true even with the gate shut"),
		Leader->OkToInterruptForMelee());
	Leader->bInChoreoScene = false;
	Leader->ActivityNumber = 0x2;
	TestFalse(TEXT("any other activity falls to the Troika line and is refused"),
		Leader->OkToInterruptForMelee());
	Leader->ActivityNumber = 0x9;
	TestTrue(TEXT("and a whitelisted one is accepted there"), Leader->OkToInterruptForMelee());

	// The Troika line does NOT carry the exception.
	Troika->ActivityNumber = 0x1141;
	TestFalse(TEXT("0x1141 is not on the Troika whitelist"), Troika->OkToInterruptForMelee());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 592 `CanSeekCover` — `0x102953e0` and `CNPC_VLasombra`'s `0x103893c0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscCanSeekCoverTest,
	"Elysium.Substrate.NpcKernelMisc.CanSeekCover", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscCanSeekCoverTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_cover"), 0x102953e0);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, GMiscSpawnableCombatant);
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	// The window this case measures is exactly one second wide and `m_flCanSeekCoverTimer` is a
	// FLOAT against a double clock, so the boundary assertion below is only exact on a stamp both
	// types represent exactly. The fixture's clock stands at the NPC's first think (a tenth of a
	// second, which float rounds up), so the case puts it on a whole second first.
	Fixture.World.Tick(1.0);
	const double Now = Fixture.World.NowSeconds();

	// Arm 1: `COND_ENEMY_OCCLUDED` (0x48) answers true on its own, before the clock is read at all.
	Npc->CanSeekCoverTimer = Now + 1000.0;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::EnemyOccluded);
	TestTrue(TEXT("COND_ENEMY_OCCLUDED answers true regardless of the timer"), Npc->CanSeekCover());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::EnemyOccluded);
	TestFalse(TEXT("and without it a far-future timer refuses"), Npc->CanSeekCover());

	// Arm 2: the timer at or before curtime.
	Npc->CanSeekCoverTimer = Now;
	TestTrue(TEXT("a timer EQUAL to curtime answers true"), Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now - 0.5;
	TestTrue(TEXT("an elapsed timer answers true"), Npc->CanSeekCover());

	// Arm 3: the slop window is ONE second (`FSUB [0x104454c0]`, the shared 1.0f), not five, and it
	// additionally requires `COND_CAN_RANGE_ATTACK1` (0x4f) to be CLEAR.
	Npc->CanSeekCoverTimer = Now + 0.5;
	TestTrue(TEXT("half a second out is inside the one-second window"), Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 1.0;
	TestTrue(TEXT("exactly one second out is still inside it (the compare is <=)"),
		Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 1.25;
	TestFalse(TEXT("1.25 s out is OUTSIDE it — the window is one second, not five"),
		Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 4.0;
	TestFalse(TEXT("and four seconds out is too, which is what 29c's 'five second' walk missed"),
		Npc->CanSeekCover());
	Npc->CanSeekCoverTimer = Now + 0.5;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::CanRangeAttack1);
	TestFalse(TEXT("COND_CAN_RANGE_ATTACK1 closes the slop window"), Npc->CanSeekCover());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::CanRangeAttack1);
	// ... but it does NOT close arm 2, which is tested before it.
	Npc->CanSeekCoverTimer = Now - 1.0;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::CanRangeAttack1);
	TestTrue(TEXT("an elapsed timer beats COND_CAN_RANGE_ATTACK1: arm 2 comes first"),
		Npc->CanSeekCover());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::CanRangeAttack1);

	// `CNPC_VLasombra` fills slot 592 with `0x103893c0`. The species arm is exercised through the
	// census by retail class name (`npc_VLasombra` builds `CNPC_VLasombra` since story 5 step 2,
	// population.md) — which is also what proves the arm a combatant takes is the Troika line.
	const FElysiumNpcClass* Lasombra = ElysiumNpcKernelClass::Find(TEXT("CNPC_VLasombra"));
	TestNotNull(TEXT("CNPC_VLasombra is a census class"), Lasombra);
	TestEqual(TEXT("and fills slot 592 with 0x103893c0"),
		FString(ElysiumNpcKernelClass::BodyOf(Lasombra, 592)), FString(TEXT("0x103893c0")));
	TestEqual(TEXT("while npc_VHumanCombatant takes the Troika line 0x102953e0"),
		FString(ElysiumNpcKernelClass::BodyOf(Npc->RetailClass(), 592)),
		FString(TEXT("0x102953e0")));

	// The combatant therefore ignores `m_flCoverDisableOverride` entirely: setting it changes
	// nothing, which is what says the species arm is gated on the census and not on the field.
	Npc->CanSeekCoverTimer = Now + 100.0;
	TestFalse(TEXT("a non-Lasombra ignores m_flCoverDisableOverride"), Npc->CanSeekCover());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 24 `OnVictimHitByMe` — the Troika line and its three species overrides.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscVictimHitTest,
	"Elysium.Substrate.NpcKernelMisc.OnVictimHitByMe", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscVictimHitTest::RunTest(const FString&)
{
	// The four species bodies of slot 24 by name, against the census. Each is its class's own
	// `OnVictimHitByMe` override (story 5 step 3); `CNPC_VGhoulCroucher#24` (`0x1037be80`) was added by
	// story 29d, family SpeciesMisc10.
	struct FRow
	{
		const TCHAR* RetailClass;
		const TCHAR* Body;
	};
	const FRow Rows[] =
	{
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x1029f8d0") },
		{ TEXT("CNPC_VGargoyle"), TEXT("0x1037a450") },
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103ab4a0") },
		{ TEXT("CNPC_VZombie"), TEXT("0x103e1280") },
		{ TEXT("CNPC_VGhoulCroucher"), TEXT("0x1037be80") },
	};
	for (const FRow& Row : Rows)
	{
		const FString Name(Row.RetailClass);
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), *Name), Cls);
		if (Cls == nullptr)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s fills slot 24 with %s"), *Name, Row.Body),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 24)), FString(Row.Body));
	}

	// The Gargoyle classname filter, as a pure function: two names, case-insensitive, whole-name.
	TestTrue(TEXT("\"pillar\" matches"), FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("pillar")));
	TestTrue(TEXT("\"PILLAR\" matches — the compare is __strcmpi"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("PILLAR")));
	TestTrue(TEXT("\"central_pillar\" matches"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("central_pillar")));
	TestFalse(TEXT("\"pillar_of_salt\" does NOT — neither name carries a trailing *"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("pillar_of_salt")));
	TestFalse(TEXT("an empty classname does not"), FElysiumNpcGargoyle::GargoyleHitsPillar(FString()));
	TestFalse(TEXT("and neither does prop_physics"),
		FElysiumNpcGargoyle::GargoyleHitsPillar(TEXT("prop_physics")));

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_victim_hit"), 0x1029f8d0);
	Builder.AddNpc(TEXT("troika"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddNpc(TEXT("leader"), FVector(500.0, 0.0, 0.0), GMiscSpawnableSabbatLeader);
	Builder.AddNpc(TEXT("victim"), FVector(200.0, 0.0, 0.0), GMiscSpawnableCombatant);
	Builder.AddNpc(TEXT("zombie"), FVector(-500.0, 0.0, 0.0), TEXT("npc_VZombie"));
	Builder.AddCounter(TEXT("attacked"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpcSabbatLeader* Leader = Fixture.NpcAs<FElysiumNpcSabbatLeader>(TEXT("leader"));
	FElysiumNpc* Victim = Fixture.Npc(TEXT("victim"));
	TestNotNull(TEXT("the combatant spawned"), Troika);
	TestNotNull(TEXT("the Sabbat leader spawned"), Leader);
	TestNotNull(TEXT("the victim spawned"), Victim);
	if (Troika == nullptr || Leader == nullptr || Victim == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Troika, Leader, Victim });

	// The Troika line (`0x1029f8d0`): the whole body is the melee-record clear, and it does NOT read
	// the victim — a null victim clears just the same.
	Troika->MeleeMoveRecordClears = 0;
	Troika->OnVictimHitByMe(Victim);
	TestEqual(TEXT("the Troika line clears the melee move records"), Troika->MeleeMoveRecordClears,
		1);
	Troika->OnVictimHitByMe(nullptr);
	TestEqual(TEXT("and does so with a null victim too — it reads no argument"),
		Troika->MeleeMoveRecordClears, 2);

	// `CNPC_VSabbatLeader::OnVictimHitByMe` (`0x103ab4a0`): the decrement is gated on the victim
	// being the tracked closest player, the clamp is at zero, and the base body does NOT run.
	Leader->MeleeMoveRecordClears = 0;
	Leader->SabbatLeaderRoarAttackCount = 2;
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	Leader->OnVictimHitByMe(Victim);
	TestEqual(TEXT("with no tracked player the count is untouched"),
		Leader->SabbatLeaderRoarAttackCount, 2);
	Leader->Senses.Memory.ClosestPlayer = Victim->Handle;
	Leader->OnVictimHitByMe(Victim);
	TestEqual(TEXT("hitting the tracked player decrements m_RoarAttackCount"),
		Leader->SabbatLeaderRoarAttackCount, 1);
	Leader->OnVictimHitByMe(Victim);
	Leader->OnVictimHitByMe(Victim);
	Leader->OnVictimHitByMe(Victim);
	TestEqual(TEXT("and it clamps at zero, never below"), Leader->SabbatLeaderRoarAttackCount, 0);
	Leader->OnVictimHitByMe(Troika);
	TestEqual(TEXT("a different victim is ignored"), Leader->SabbatLeaderRoarAttackCount, 0);
	TestEqual(TEXT("the Sabbat leader never runs the base record clear"),
		Leader->MeleeMoveRecordClears, 0);

	// `CNPC_VZombie::OnVictimHitByMe` (`0x103e1280`), through a spawned `npc_VZombie`'s own override:
	// the Troika body FIRST (the record clear), then `OnAttackedVictim`.
	FElysiumNpc* Zombie = Fixture.Npc(TEXT("zombie"));
	TestNotNull(TEXT("the zombie spawned"), Zombie);
	if (Zombie != nullptr)
	{
		FElysiumNpcWorldFixture::Quiet({ Zombie });
		Zombie->MeleeMoveRecordClears = 0;
		Zombie->OnVictimHitByMe(Victim);
		TestEqual(TEXT("the zombie keeps the Troika record clear"), Zombie->MeleeMoveRecordClears, 1);
	}

	// The slot-266 dispatch seam: an NPC victim takes the real slot, a non-NPC is counted only.
	Troika->VictimHitReactionDispatches = 0;
	Troika->DispatchVictimHitReaction(Victim);
	TestEqual(TEXT("the victim-reaction dispatch is counted"),
		Troika->VictimHitReactionDispatches, 1);
	Troika->DispatchVictimHitReaction(nullptr);
	TestEqual(TEXT("and a missing victim is counted without dispatching"),
		Troika->VictimHitReactionDispatches, 2);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 473's Troika answer and `CNPC_VBach`'s slots 553/554.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscSpeciesThresholdTest,
	"Elysium.Substrate.NpcKernelMisc.SpeciesThresholds", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscSpeciesThresholdTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_species"), 0x10364500);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddNpcOfClass(TEXT("bach"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VBach"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcBach* BachNpc = Fixture.NpcAs<FElysiumNpcBach>(TEXT("bach"));
	if (!TestNotNull(TEXT("the Bach spawned"), BachNpc))
	{
		return false;
	}
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// Slot 473's two species overrides (`CGeneric_NPC`, `CGenericSabbat_NPC`) are on classes with
	// no instance; every class with one answers the Troika line's own mask.
	TestEqual(TEXT("the Troika line's own answer is 0x81f"), Npc->GetSoundInterests(), 0x81f);

	// `CNPC_VBach`'s slots 553/554 (`0x10364500`, `0x10364550`): an OR of two thresholds, 0.5 on the
	// dot and 120 units on the distance, with the answer the only difference between the two.
	const FElysiumNpcClass* Bach = ElysiumNpcKernelClass::Find(TEXT("CNPC_VBach"));
	TestNotNull(TEXT("CNPC_VBach is a census class"), Bach);
	TestEqual(TEXT("and fills slot 553 with 0x10364500"),
		FString(ElysiumNpcKernelClass::BodyOf(Bach, 553)), FString(TEXT("0x10364500")));
	TestEqual(TEXT("and slot 554 with 0x10364550"),
		FString(ElysiumNpcKernelClass::BodyOf(Bach, 554)), FString(TEXT("0x10364550")));

	TestEqual(TEXT("dot exactly 0.5 accepts (the compare is >=)"),
		BachNpc->BachRangeAttack1Conditions(0.5f, 10000.f), 0x4f);
	TestEqual(TEXT("dot above 0.5 accepts"), BachNpc->BachRangeAttack1Conditions(0.9f, 10000.f), 0x4f);
	TestEqual(TEXT("dot below 0.5 with the target far refuses with COND_NOT_FACING_ATTACK"),
		BachNpc->BachRangeAttack1Conditions(0.49f, 10000.f), 0x61);
	TestEqual(TEXT("but a near target accepts even facing away — it is an OR"),
		BachNpc->BachRangeAttack1Conditions(-1.f, 119.f), 0x4f);
	TestEqual(TEXT("distance exactly 120 accepts (the compare is <=)"),
		BachNpc->BachRangeAttack1Conditions(-1.f, 120.f), 0x4f);
	TestEqual(TEXT("distance just past 120 refuses"),
		BachNpc->BachRangeAttack1Conditions(-1.f, 120.1f), 0x61);
	TestEqual(TEXT("slot 554 shares the gate and answers COND_CAN_RANGE_ATTACK2"),
		BachNpc->BachRangeAttack2Conditions(0.5f, 10000.f), 0x50);
	TestEqual(TEXT("and shares the refusal"), BachNpc->BachRangeAttack2Conditions(0.49f, 10000.f),
		0x61);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The Hengeyokai form bit, the Yukie melee pair, the ManBat slow, the Chang arena centre and the
// Werewolf zone.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscSpeciesBodiesTest,
	"Elysium.Substrate.NpcKernelMisc.SpeciesBodies", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscSpeciesBodiesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_species_bodies"), 0x10381c00);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddEntity(TEXT("point_target"), TEXT("trigger_werewolf_zone"), FVector(10.0, 0.0, 0.0));
	Builder.AddEntity(TEXT("point_target"), TEXT("trigger_werewolf_zone"), FVector(20.0, 0.0, 0.0));
	Builder.AddEntity(TEXT("point_target"), TEXT("rotdoor1"), FVector(30.0, 0.0, 0.0));
	Builder.AddNpcOfClass(TEXT("wolf"), FVector(400.0, 0.0, 0.0), TEXT("CNPC_VWerewolf"));
	Builder.AddNpcOfClass(TEXT("heng"), FVector(0.0, 400.0, 0.0), TEXT("CNPC_VHengeyokai"));
	Builder.AddNpcOfClass(TEXT("yukie"), FVector(0.0, 800.0, 0.0), TEXT("CNPC_VYukie"));
	Builder.AddNpcOfClass(TEXT("bat"), FVector(0.0, 1200.0, 0.0), TEXT("CNPC_VManBat"));
	Builder.AddNpcOfClass(TEXT("chang"), FVector(0.0, 1600.0, 0.0), TEXT("CNPC_VChangBros"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWerewolf* Wolf = Fixture.NpcAs<FElysiumNpcWerewolf>(TEXT("wolf"));
	FElysiumNpcHengeyokai* Heng = Fixture.NpcAs<FElysiumNpcHengeyokai>(TEXT("heng"));
	FElysiumNpcYukie* YukieNpc = Fixture.NpcAs<FElysiumNpcYukie>(TEXT("yukie"));
	FElysiumNpcManBat* Bat = Fixture.NpcAs<FElysiumNpcManBat>(TEXT("bat"));
	FElysiumNpcChangBros* Chang = Fixture.NpcAs<FElysiumNpcChangBros>(TEXT("chang"));
	if (Heng == nullptr || YukieNpc == nullptr || Bat == nullptr || Chang == nullptr)
	{
		AddError(TEXT("the species bodies' classes did not stand"));
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Heng, YukieNpc, Bat, Chang });
	TestNotNull(TEXT("the NPC spawned"), Npc);
	TestNotNull(TEXT("the werewolf spawned"), Wolf);
	if (Npc == nullptr || Wolf == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc, Wolf });
	// `m_flFishTimer` is a FLOAT against a double clock, so the `<= curtime` boundary below is
	// only exact on a stamp both types represent. The fixture's clock stands at the NPC's first
	// think (a tenth of a second, which float rounds up), so this case puts it on a whole second.
	Fixture.World.Tick(1.0);
	const double Now = Fixture.World.NowSeconds();

	// `0x10381c00` / `0x10381ca0` — the form bit and its timer.
	Heng->bHengeyokaiDidFakeThrow = true;
	Heng->HengeyokaiFishTimer = 0.f;
	Heng->FormBit(true);
	TestTrue(TEXT("the true arm raises CARRYING_BODY (+0x14b8 bit 0x20)"),
		Heng->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestFalse(TEXT("and clears m_bDidFakeThrow (+0x667d)"), Heng->bHengeyokaiDidFakeThrow);
	TestTrue(TEXT("m_flFishTimer takes curtime + RandomFloat(5, 8)"),
		Heng->HengeyokaiFishTimer >= static_cast<float>(Now) + 5.f
			&& Heng->HengeyokaiFishTimer <= static_cast<float>(Now) + 8.f);
	TestFalse(TEXT("so the timer has not expired"), Heng->FormBitTimerExpired());

	const float Armed = Heng->HengeyokaiFishTimer;
	Heng->bHengeyokaiDidFakeThrow = true;
	Heng->FormBit(false);
	TestFalse(TEXT("the false arm clears CARRYING_BODY"),
		Heng->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestEqual(TEXT("and leaves m_flFishTimer exactly where it stood"), Heng->HengeyokaiFishTimer,
		Armed);
	TestTrue(TEXT("and does NOT clear m_bDidFakeThrow"), Heng->bHengeyokaiDidFakeThrow);

	Heng->HengeyokaiFishTimer = static_cast<float>(Now);
	TestTrue(TEXT("a timer equal to curtime HAS expired (the compare is <=)"),
		Heng->FormBitTimerExpired());

	// `CNPC_VYukie`'s melee pair. The CENSUS proves which bodies fill the slots and the bodies
	// themselves are called by name (`npc_VYukie` builds `CNPC_VYukie` since story 5 step 2,
	// population.md; this case does not stand one).
	const FElysiumNpcClass* Yukie = ElysiumNpcKernelClass::Find(TEXT("CNPC_VYukie"));
	TestNotNull(TEXT("CNPC_VYukie is a census class"), Yukie);
	TestEqual(TEXT("and fills slot 599 with 0x103dd8b0"),
		FString(ElysiumNpcKernelClass::BodyOf(Yukie, 599)), FString(TEXT("0x103dd8b0")));
	TestEqual(TEXT("and slot 601 with 0x103dd9a0"),
		FString(ElysiumNpcKernelClass::BodyOf(Yukie, 601)), FString(TEXT("0x103dd9a0")));

	const int32 EventsBefore = YukieNpc->MeleeEventFires;
	YukieNpc->bInMelee = false;
	YukieNpc->MeleeMustLeaveTimer = 0.0;
	TestTrue(TEXT("Yukie's enter-melee is UNGATED and always answers true"),
		YukieNpc->YukieEnterMelee());
	TestTrue(TEXT("it sets m_bInMelee"), YukieNpc->bInMelee);
	TestTrue(TEXT("and arms m_flMeleeMustLeaveTimer inside [22.5, 45]"),
		YukieNpc->MeleeMustLeaveTimer >= Now + 22.5 && YukieNpc->MeleeMustLeaveTimer <= Now + 45.0);
	TestEqual(TEXT("the global melee event fired once"), YukieNpc->MeleeEventFires, EventsBefore + 1);

	YukieNpc->MeleeCanEnterTimer = 1234.0;
	YukieNpc->YukieLeaveMelee();
	TestFalse(TEXT("leaving clears m_bInMelee"), YukieNpc->bInMelee);
	TestEqual(TEXT("and fires the same global event again"), YukieNpc->MeleeEventFires,
		EventsBefore + 2);
	TestFalse(TEXT("slot 308 HasUsableRangedWeapon is still a stub answering false"),
		YukieNpc->HasUsableRangedWeapon());
	TestEqual(TEXT("so the can-enter re-arm is NOT reached and the timer stands"),
		YukieNpc->MeleeCanEnterTimer, 1234.0);

	// `0x1038f290` — a compare against ZERO (`_DAT_1044fab0`, the shared 0.0 double), not against
	// curtime. A flag spelled as a float.
	Bat->ManBatSlowedExpire = 0.f;
	TestFalse(TEXT("an unarmed m_flSlowedExpire answers false"), Bat->SlowedExpire());
	Bat->ManBatSlowedExpire = 0.001f;
	TestTrue(TEXT("anything above zero answers true"), Bat->SlowedExpire());
	// The clock walks well past that stamp and the answer does NOT change — which is the whole
	// point: `_DAT_1044fab0` is the shared `0.0` double, so the compare is against zero and a body
	// that read `m_flSlowedExpire` as a deadline against curtime would flip here.
	Fixture.Advance(Now + 60.0);
	TestTrue(TEXT("curtime is now far past the stamp"), Fixture.World.NowSeconds() > 1.0);
	TestTrue(TEXT("and it STILL answers true — it is NOT a deadline"), Bat->SlowedExpire());
	Bat->ManBatSlowedExpire = -1.f;
	TestFalse(TEXT("a negative value answers false"), Bat->SlowedExpire());
	Bat->ManBatSlowedExpire = 0.f;
	TestFalse(TEXT("and exactly zero does too — the compare is strict"), Bat->SlowedExpire());

	// `CNPC_VChangBros::StoreArenaCenter` `0x1036e400`: both writes are inside the found arm, and
	// the hint walk is family Squad's seam, which answers null.
	TestNull(TEXT("the global hint walk answers nothing (no hint store carries hint types)"),
		Chang->NthHintOfType(0x4651, 0));
	Chang->ChangArenaCenter = FVector(1.0, 2.0, 3.0);
	Chang->bChangCenterStored = false;
	Chang->StoreArenaCenter();
	TestFalse(TEXT("with no 0x4651 hint m_bCenterStored stays false"), Chang->bChangCenterStored);
	TestEqual(TEXT("and m_vArenaCenter is left exactly as it was"), Chang->ChangArenaCenter,
		FVector(1.0, 2.0, 3.0));

	// `0x103cade0`: the zone sweep is by TARGETNAME (`entity+0x11c`), so both zone entities are
	// reached, and a missing `rotdoor2` stores the INVALID handle rather than anything else.
	Wolf->WerewolfZoneTriggerFires = 0;
	Wolf->WerewolfRotDoor1 = FElysiumEntityHandle::Invalid();
	Wolf->WerewolfRotDoor2 = FElysiumEntityHandle::Invalid();
	Wolf->TriggerWerewolfZone();
	TestEqual(TEXT("both trigger_werewolf_zone entities were reached by targetname"),
		Wolf->WerewolfZoneTriggerFires, 2);
	TestTrue(TEXT("rotdoor1 is cached"), Wolf->WerewolfRotDoor1.IsSet());
	TestFalse(TEXT("and a missing rotdoor2 stores the invalid handle"),
		Wolf->WerewolfRotDoor2.IsSet());
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
