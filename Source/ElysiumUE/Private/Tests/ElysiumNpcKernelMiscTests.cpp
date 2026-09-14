#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumNpc.h"
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
	// The two classnames every case here can actually spawn out of `ElysiumNpcClasses.cpp` and that
	// the census also claims. `npc_VCop` is deliberately included in the census case below because
	// its census classname list is NULL — a spawned cop's `RetailClass()` answers null and every
	// per-species lookup correctly falls through to the Troika line.
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
	TestFalse(TEXT("0x102c7ef0 answers the unloaded latch"), FElysiumNpc::StandoffVfunc28());

	// Slot 26 (`0x102c7dd0`): two INDEPENDENT tests, not an if/else.
	{
		FElysiumNpc::FStandoffWords Words;
		FElysiumNpc::FStandoffAimWords Aim;
		Aim.AimMode = 0;
		Aim.AimWord0x58 = 7.f;
		Aim.AimWord0x5c = 7.f;
		Aim.AimWord0x60 = 7.f;
		FElysiumNpc::StandoffVfunc26(Words, Aim);
		TestEqual(TEXT("mode 0 resets nothing (+0x58)"), Aim.AimWord0x58, 7.f);
		TestEqual(TEXT("mode 0 resets nothing (+0x5c)"), Aim.AimWord0x5c, 7.f);
		TestEqual(TEXT("mode 0 resets nothing (+0x60)"), Aim.AimWord0x60, 7.f);
		TestFalse(TEXT("mode 0 does not latch bSawNewEnemy"), Words.bSawNewEnemy);
	}
	{
		FElysiumNpc::FStandoffWords Words;
		FElysiumNpc::FStandoffAimWords Aim;
		Aim.AimMode = 1;
		FElysiumNpc::StandoffVfunc26(Words, Aim);
		TestEqual(TEXT("mode 1 writes 5.0 into +0x5c"), Aim.AimWord0x5c, 5.f);
		TestEqual(TEXT("mode 1 writes 0.0 into +0x60"), Aim.AimWord0x60, 0.f);
		TestEqual(TEXT("mode 1 writes -1.0 into +0x58"), Aim.AimWord0x58, -1.f);
		TestFalse(TEXT("mode 1 does NOT latch bSawNewEnemy"), Words.bSawNewEnemy);
	}
	{
		FElysiumNpc::FStandoffWords Words;
		FElysiumNpc::FStandoffAimWords Aim;
		Aim.AimMode = 2;
		FElysiumNpc::StandoffVfunc26(Words, Aim);
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
		FElysiumNpc::FStandoffWords Words;
		Words.ReactionDelayMin = 3.f;
		Words.ReactionDelayMax = 0.f;
		Words.ReactionsLeft = 4;
		FElysiumNpc::FStandoffAimWords Aim;
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
		FElysiumNpc::FStandoffWords Words;
		Words.ReactionDelayMin = 2.f;
		Words.ReactionDelayMax = 6.f;
		FElysiumNpc::FStandoffAimWords Aim;
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
	TestEqual(TEXT("six Troika-line factories plus three species overrides, plus slot 424"), Count,
		10);
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

	// The recovered allocation sizes, by row — the one fact each factory exists to state. The two
	// humanoid variants are FOUR bytes larger than their bases and the rat's local navigator is the
	// same size as its base but a different constructor, which is why size alone does not identify
	// a row.
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
	const FElysiumNpc::FComponentFactory* HumanoidMotor = RowFor(TEXT("CAI_BaseHumanoid"), 427);
	const FElysiumNpc::FComponentFactory* BaseNav = RowFor(TEXT("CAI_BaseNPC"), 429);
	const FElysiumNpc::FComponentFactory* HumanoidNav = RowFor(TEXT("CAI_BaseHumanoid"), 429);
	const FElysiumNpc::FComponentFactory* BaseLocalNav = RowFor(TEXT("CAI_BaseNPC"), 428);
	const FElysiumNpc::FComponentFactory* RatLocalNav = RowFor(TEXT("CNPC_VRat"), 428);
	const FElysiumNpc::FComponentFactory* Senses = RowFor(TEXT("CAI_BaseNPC"), 425);
	const FElysiumNpc::FComponentFactory* Probe = RowFor(TEXT("CAI_BaseNPC"), 426);
	const FElysiumNpc::FComponentFactory* Pathfinder = RowFor(TEXT("CAI_BaseNPC"), 430);
	if (BaseMotor == nullptr || HumanoidMotor == nullptr || BaseNav == nullptr
		|| HumanoidNav == nullptr || BaseLocalNav == nullptr || RatLocalNav == nullptr
		|| Senses == nullptr || Probe == nullptr || Pathfinder == nullptr)
	{
		AddError(TEXT("a named component-factory row is missing"));
		return false;
	}
	TestEqual(TEXT("CAI_Senses is 0x88 bytes"), Senses->SizeBytes, 0x88);
	TestEqual(TEXT("CAI_MoveProbe is 0x14 bytes"), Probe->SizeBytes, 0x14);
	TestEqual(TEXT("CAI_Pathfinder is 0x18 bytes"), Pathfinder->SizeBytes, 0x18);
	TestEqual(TEXT("the base motor is 0x6c bytes"), BaseMotor->SizeBytes, 0x6c);
	TestEqual(TEXT("the humanoid motor is four bytes larger"), HumanoidMotor->SizeBytes,
		BaseMotor->SizeBytes + 4);
	TestEqual(TEXT("both motors share the 0x102e0900 constructor"),
		FString(HumanoidMotor->Constructor), FString(BaseMotor->Constructor));
	TestEqual(TEXT("the base navigator is 0x68 bytes"), BaseNav->SizeBytes, 0x68);
	TestEqual(TEXT("the humanoid navigator is four bytes larger"), HumanoidNav->SizeBytes,
		BaseNav->SizeBytes + 4);
	TestEqual(TEXT("both navigators share the 0x102eca50 constructor"),
		FString(HumanoidNav->Constructor), FString(BaseNav->Constructor));
	TestEqual(TEXT("the rat's local navigator is the SAME size as the base's"),
		RatLocalNav->SizeBytes, BaseLocalNav->SizeBytes);
	TestNotEqual(TEXT("but a different constructor"), FString(RatLocalNav->Constructor),
		FString(BaseLocalNav->Constructor));

	// The per-class resolve, and the `npc_VCop` fall-through the brief warns about.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_components"), 0x1027cae0);
	Builder.AddNpc(TEXT("rat"), FVector::ZeroVector, TEXT("npc_VRat"));
	Builder.AddNpc(TEXT("cop"), FVector(400.0, 0.0, 0.0), TEXT("npc_VCop"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	FElysiumNpc* Cop = Fixture.Npc(TEXT("cop"));
	TestNotNull(TEXT("the rat spawned"), Rat);
	TestNotNull(TEXT("the cop spawned"), Cop);
	if (Rat == nullptr || Cop == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Rat, Cop });

	const FElysiumNpc::FComponentFactory* RatRow = Rat->ComponentFactoryFor(428);
	TestNotNull(TEXT("the rat resolves a slot-428 row"), RatRow);
	if (RatRow != nullptr)
	{
		TestEqual(TEXT("and it is CNPC_VRat's own 0x103ad6a0"), FString(RatRow->Body),
			FString(TEXT("0x103ad6a0")));
	}
	TestNull(TEXT("npc_VCop's census classname list is null, so RetailClass() answers null"),
		Cop->RetailClass());
	const FElysiumNpc::FComponentFactory* CopRow = Cop->ComponentFactoryFor(428);
	TestNotNull(TEXT("and the cop still resolves a row"), CopRow);
	if (CopRow != nullptr)
	{
		TestEqual(TEXT("the Troika line's, which is the correct fall-through"),
			FString(CopRow->Body), FString(TEXT("0x1027cf60")));
	}

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

	// `CNPC_VLasombra` fills slot 592 with `0x103893c0`. `npc_VLasombra` is not a registered spawn
	// leaf, so the species arm is exercised through the census by retail class name — which is also
	// what proves the arm this runtime takes for a combatant is the Troika line.
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
	Npc->LasombraCoverDisableOverride = static_cast<float>(Now + 100.0);
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
	// Every row of the slot-24 table by name, against the census.
	int32 Count = 0;
	const FElysiumNpc::FVictimHitSpecies* Rows = FElysiumNpc::VictimHitSpeciesRows(Count);
	// Four species overrides since story 29d, family SpeciesMisc10 added `CNPC_VGhoulCroucher#24`
	// (`0x1037be80`); the census carries that row (`ElysiumNpcKernelShape.cpp`) and the loop below
	// checks it like every other. Count corrected by family SpeciesLifecycle10, whose own slot-174
	// row meets the same class.
	TestEqual(TEXT("the Troika line plus four species overrides"), Count, 5);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpc::FVictimHitSpecies& Row = Rows[Index];
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
	TestTrue(TEXT("\"pillar\" matches"), FElysiumNpc::GargoyleHitsPillar(TEXT("pillar")));
	TestTrue(TEXT("\"PILLAR\" matches — the compare is __strcmpi"),
		FElysiumNpc::GargoyleHitsPillar(TEXT("PILLAR")));
	TestTrue(TEXT("\"central_pillar\" matches"),
		FElysiumNpc::GargoyleHitsPillar(TEXT("central_pillar")));
	TestFalse(TEXT("\"pillar_of_salt\" does NOT — neither name carries a trailing *"),
		FElysiumNpc::GargoyleHitsPillar(TEXT("pillar_of_salt")));
	TestFalse(TEXT("an empty classname does not"), FElysiumNpc::GargoyleHitsPillar(FString()));
	TestFalse(TEXT("and neither does prop_physics"),
		FElysiumNpc::GargoyleHitsPillar(TEXT("prop_physics")));

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_victim_hit"), 0x1029f8d0);
	Builder.AddNpc(TEXT("troika"), FVector::ZeroVector, GMiscSpawnableCombatant);
	Builder.AddNpc(TEXT("leader"), FVector(500.0, 0.0, 0.0), GMiscSpawnableSabbatLeader);
	Builder.AddNpc(TEXT("victim"), FVector(200.0, 0.0, 0.0), GMiscSpawnableCombatant);
	Builder.AddCounter(TEXT("attacked"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Troika = Fixture.Npc(TEXT("troika"));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
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
	TestEqual(TEXT("npc_VHumanCombatant takes the Troika line"),
		static_cast<int32>(Troika->VictimHitLine()),
		static_cast<int32>(FElysiumNpc::EVictimHitLine::Troika));
	Troika->MeleeMoveRecordClears = 0;
	Troika->OnVictimHitByMe(Victim);
	TestEqual(TEXT("the Troika line clears the melee move records"), Troika->MeleeMoveRecordClears,
		1);
	Troika->OnVictimHitByMe(nullptr);
	TestEqual(TEXT("and does so with a null victim too — it reads no argument"),
		Troika->MeleeMoveRecordClears, 2);

	// `CNPC_VSabbatLeader::OnVictimHitByMe` (`0x103ab4a0`): the decrement is gated on the victim
	// being the tracked closest player, the clamp is at zero, and the base body does NOT run.
	TestEqual(TEXT("npc_VSabbatLeader takes its own line"),
		static_cast<int32>(Leader->VictimHitLine()),
		static_cast<int32>(FElysiumNpc::EVictimHitLine::SabbatLeader));
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

	// The Gargoyle and Zombie lines have no registered classname, so their ARM is exercised through
	// the census (above) and their BODY through the line enum: nothing in this map can take them,
	// which is itself the recovered answer.
	TestNotEqual(TEXT("no spawnable classname here takes the Gargoyle line"),
		static_cast<int32>(Troika->VictimHitLine()),
		static_cast<int32>(FElysiumNpc::EVictimHitLine::Gargoyle));
	const FElysiumNpcClass* Zombie = ElysiumNpcKernelClass::Find(TEXT("CNPC_VZombie"));
	TestNotNull(TEXT("CNPC_VZombie is a census class"), Zombie);
	TestEqual(TEXT("and its slot-24 body is 0x103e1280, the only species arm that keeps the base"),
		FString(ElysiumNpcKernelClass::BodyOf(Zombie, 24)), FString(TEXT("0x103e1280")));

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
// Slot 473's `CGeneric_NPC` pair, `CNPC_Bullseye`'s slot 576 and `CNPC_VBach`'s slots 553/554.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMiscSpeciesThresholdTest,
	"Elysium.Substrate.NpcKernelMisc.SpeciesThresholds", GElysiumNpcKernelMiscFlags)
bool FElysiumNpcKernelMiscSpeciesThresholdTest::RunTest(const FString&)
{
	// Slot 473's two rows by name — `CGeneric_NPC` and `CGenericSabbat_NPC`, byte-identical bodies.
	int32 Count = 0;
	const FElysiumNpc::FSoundInterestSpecies* Rows = FElysiumNpc::SoundInterestSpeciesRows(Count);
	TestEqual(TEXT("two classes carry the slot-473 override"), Count, 2);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FString Name(Rows[Index].RetailClass);
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Rows[Index].RetailClass);
		TestNotNull(*FString::Printf(TEXT("%s is a census class"), *Name), Cls);
		if (Cls != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s fills slot 473 with %s"), *Name, Rows[Index].Body),
				FString(ElysiumNpcKernelClass::BodyOf(Cls, 473)), FString(Rows[Index].Body));
		}
	}

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_misc_species"), 0x1035a7e0);
	Builder.AddNpc(TEXT("npc"), FVector::ZeroVector, GMiscSpawnableCombatant);
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });

	// `m_NPCState == 1` (retail `NPC_STATE_IDLE`) answers 0x19, everything else 0x17. The state is
	// forced through the scripted-schedule order the same way family Squad's own state case does —
	// the mind is private and its transitions are arbitrated, so a case states the state it wants.
	FElysiumScriptedScheduleOrder Order;
	Npc->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, EElysiumNpcState::Idle);
	TestEqual(TEXT("the NPC is IDLE"), Npc->GetMind().State(), EElysiumNpcState::Idle);
	TestEqual(TEXT("an idle generic NPC answers 0x19"), Npc->GenericNpcSoundInterests(), 0x19);
	Npc->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, EElysiumNpcState::Combat);
	TestEqual(TEXT("the NPC is COMBAT"), Npc->GetMind().State(), EElysiumNpcState::Combat);
	TestEqual(TEXT("any other state answers 0x17"), Npc->GenericNpcSoundInterests(), 0x17);
	Npc->BeginScriptedSchedule(Order, /*bHasForcedState=*/true, EElysiumNpcState::Alert);
	TestEqual(TEXT("ALERT answers 0x17 too — only retail's 1 is special"),
		Npc->GenericNpcSoundInterests(), 0x17);
	TestEqual(TEXT("and 0x19 is 0x17 plus exactly one extra sound class"), 0x19 & ~0x17, 0x8);
	TestEqual(TEXT("while the Troika line's own answer is 0x81f"), Npc->GetSoundInterests(), 0x81f);

	// `CNPC_Bullseye::vfunc576` (`0x10356f30`): `damage > 0.0f`, byte-identical to the Troika line.
	const FElysiumNpcClass* Bullseye = ElysiumNpcKernelClass::Find(TEXT("CNPC_Bullseye"));
	TestNotNull(TEXT("CNPC_Bullseye is a census class"), Bullseye);
	TestEqual(TEXT("and fills slot 576 with 0x10356f30"),
		FString(ElysiumNpcKernelClass::BodyOf(Bullseye, 576)), FString(TEXT("0x10356f30")));
	TestFalse(TEXT("zero damage is not light damage"), Npc->BullseyeIsLightDamage(0.f, 0));
	TestFalse(TEXT("negative damage is not"), Npc->BullseyeIsLightDamage(-1.f, 0));
	TestTrue(TEXT("any damage above zero is"), Npc->BullseyeIsLightDamage(0.001f, 0));
	TestEqual(TEXT("and the answer is the Troika line's, bit for bit"),
		Npc->BullseyeIsLightDamage(5.f, 0), Npc->IsLightDamage(5.f, 0));

	// `CNPC_VBach`'s slots 553/554 (`0x10364500`, `0x10364550`): an OR of two thresholds, 0.5 on the
	// dot and 120 units on the distance, with the answer the only difference between the two.
	const FElysiumNpcClass* Bach = ElysiumNpcKernelClass::Find(TEXT("CNPC_VBach"));
	TestNotNull(TEXT("CNPC_VBach is a census class"), Bach);
	TestEqual(TEXT("and fills slot 553 with 0x10364500"),
		FString(ElysiumNpcKernelClass::BodyOf(Bach, 553)), FString(TEXT("0x10364500")));
	TestEqual(TEXT("and slot 554 with 0x10364550"),
		FString(ElysiumNpcKernelClass::BodyOf(Bach, 554)), FString(TEXT("0x10364550")));

	TestEqual(TEXT("dot exactly 0.5 accepts (the compare is >=)"),
		Npc->BachRangeAttack1Conditions(0.5f, 10000.f), 0x4f);
	TestEqual(TEXT("dot above 0.5 accepts"), Npc->BachRangeAttack1Conditions(0.9f, 10000.f), 0x4f);
	TestEqual(TEXT("dot below 0.5 with the target far refuses with COND_NOT_FACING_ATTACK"),
		Npc->BachRangeAttack1Conditions(0.49f, 10000.f), 0x61);
	TestEqual(TEXT("but a near target accepts even facing away — it is an OR"),
		Npc->BachRangeAttack1Conditions(-1.f, 119.f), 0x4f);
	TestEqual(TEXT("distance exactly 120 accepts (the compare is <=)"),
		Npc->BachRangeAttack1Conditions(-1.f, 120.f), 0x4f);
	TestEqual(TEXT("distance just past 120 refuses"),
		Npc->BachRangeAttack1Conditions(-1.f, 120.1f), 0x61);
	TestEqual(TEXT("slot 554 shares the gate and answers COND_CAN_RANGE_ATTACK2"),
		Npc->BachRangeAttack2Conditions(0.5f, 10000.f), 0x50);
	TestEqual(TEXT("and shares the refusal"), Npc->BachRangeAttack2Conditions(0.49f, 10000.f),
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
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	TestNotNull(TEXT("the NPC spawned"), Npc);
	if (Npc == nullptr)
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Npc });
	const double Now = Fixture.World.NowSeconds();

	// `0x10381c00` / `0x10381ca0` — the form bit and its timer.
	Npc->bHengeyokaiDidFakeThrow = true;
	Npc->HengeyokaiFishTimer = 0.f;
	Npc->FormBit(true);
	TestTrue(TEXT("the true arm raises CARRYING_BODY (+0x14b8 bit 0x20)"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestFalse(TEXT("and clears m_bDidFakeThrow (+0x667d)"), Npc->bHengeyokaiDidFakeThrow);
	TestTrue(TEXT("m_flFishTimer takes curtime + RandomFloat(5, 8)"),
		Npc->HengeyokaiFishTimer >= static_cast<float>(Now) + 5.f
			&& Npc->HengeyokaiFishTimer <= static_cast<float>(Now) + 8.f);
	TestFalse(TEXT("so the timer has not expired"), Npc->FormBitTimerExpired());

	const float Armed = Npc->HengeyokaiFishTimer;
	Npc->bHengeyokaiDidFakeThrow = true;
	Npc->FormBit(false);
	TestFalse(TEXT("the false arm clears CARRYING_BODY"),
		Npc->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestEqual(TEXT("and leaves m_flFishTimer exactly where it stood"), Npc->HengeyokaiFishTimer,
		Armed);
	TestTrue(TEXT("and does NOT clear m_bDidFakeThrow"), Npc->bHengeyokaiDidFakeThrow);

	Npc->HengeyokaiFishTimer = static_cast<float>(Now);
	TestTrue(TEXT("a timer equal to curtime HAS expired (the compare is <=)"),
		Npc->FormBitTimerExpired());

	// `CNPC_VYukie`'s melee pair. `npc_VYukie` is not a registered spawn leaf, so the CENSUS proves
	// which bodies fill the slots and the bodies themselves are called by name.
	const FElysiumNpcClass* Yukie = ElysiumNpcKernelClass::Find(TEXT("CNPC_VYukie"));
	TestNotNull(TEXT("CNPC_VYukie is a census class"), Yukie);
	TestEqual(TEXT("and fills slot 599 with 0x103dd8b0"),
		FString(ElysiumNpcKernelClass::BodyOf(Yukie, 599)), FString(TEXT("0x103dd8b0")));
	TestEqual(TEXT("and slot 601 with 0x103dd9a0"),
		FString(ElysiumNpcKernelClass::BodyOf(Yukie, 601)), FString(TEXT("0x103dd9a0")));

	const int32 EventsBefore = Npc->MeleeEventFires;
	Npc->bInMelee = false;
	Npc->MeleeMustLeaveTimer = 0.0;
	TestTrue(TEXT("Yukie's enter-melee is UNGATED and always answers true"),
		Npc->YukieEnterMelee());
	TestTrue(TEXT("it sets m_bInMelee"), Npc->bInMelee);
	TestTrue(TEXT("and arms m_flMeleeMustLeaveTimer inside [22.5, 45]"),
		Npc->MeleeMustLeaveTimer >= Now + 22.5 && Npc->MeleeMustLeaveTimer <= Now + 45.0);
	TestEqual(TEXT("the global melee event fired once"), Npc->MeleeEventFires, EventsBefore + 1);

	Npc->MeleeCanEnterTimer = 1234.0;
	Npc->YukieLeaveMelee();
	TestFalse(TEXT("leaving clears m_bInMelee"), Npc->bInMelee);
	TestEqual(TEXT("and fires the same global event again"), Npc->MeleeEventFires,
		EventsBefore + 2);
	TestFalse(TEXT("slot 308 HasUsableRangedWeapon is still a stub answering false"),
		Npc->HasUsableRangedWeapon());
	TestEqual(TEXT("so the can-enter re-arm is NOT reached and the timer stands"),
		Npc->MeleeCanEnterTimer, 1234.0);

	// `0x1038f290` — a compare against ZERO (`_DAT_1044fab0`, the shared 0.0 double), not against
	// curtime. A flag spelled as a float.
	Npc->ManBatSlowedExpire = 0.f;
	TestFalse(TEXT("an unarmed m_flSlowedExpire answers false"), Npc->SlowedExpire());
	Npc->ManBatSlowedExpire = 0.001f;
	TestTrue(TEXT("anything above zero answers true"), Npc->SlowedExpire());
	// The clock walks well past that stamp and the answer does NOT change — which is the whole
	// point: `_DAT_1044fab0` is the shared `0.0` double, so the compare is against zero and a body
	// that read `m_flSlowedExpire` as a deadline against curtime would flip here.
	Fixture.Advance(Now + 60.0);
	TestTrue(TEXT("curtime is now far past the stamp"), Fixture.World.NowSeconds() > 1.0);
	TestTrue(TEXT("and it STILL answers true — it is NOT a deadline"), Npc->SlowedExpire());
	Npc->ManBatSlowedExpire = -1.f;
	TestFalse(TEXT("a negative value answers false"), Npc->SlowedExpire());
	Npc->ManBatSlowedExpire = 0.f;
	TestFalse(TEXT("and exactly zero does too — the compare is strict"), Npc->SlowedExpire());

	// `CNPC_VChangBros::StoreArenaCenter` `0x1036e400`: both writes are inside the found arm, and
	// the hint walk is family Squad's seam, which answers null.
	TestNull(TEXT("the global hint walk answers nothing (no hint store carries hint types)"),
		Npc->NthHintOfType(0x4651, 0));
	Npc->ChangArenaCenter = FVector(1.0, 2.0, 3.0);
	Npc->bChangCenterStored = false;
	Npc->StoreArenaCenter();
	TestFalse(TEXT("with no 0x4651 hint m_bCenterStored stays false"), Npc->bChangCenterStored);
	TestEqual(TEXT("and m_vArenaCenter is left exactly as it was"), Npc->ChangArenaCenter,
		FVector(1.0, 2.0, 3.0));

	// `0x103cade0`: the zone sweep is by TARGETNAME (`entity+0x11c`), so both zone entities are
	// reached, and a missing `rotdoor2` stores the INVALID handle rather than anything else.
	Npc->WerewolfZoneTriggerFires = 0;
	Npc->WerewolfRotDoor1 = FElysiumEntityHandle::Invalid();
	Npc->WerewolfRotDoor2 = FElysiumEntityHandle::Invalid();
	Npc->TriggerWerewolfZone();
	TestEqual(TEXT("both trigger_werewolf_zone entities were reached by targetname"),
		Npc->WerewolfZoneTriggerFires, 2);
	TestTrue(TEXT("rotdoor1 is cached"), Npc->WerewolfRotDoor1.IsSet());
	TestFalse(TEXT("and a missing rotdoor2 stores the invalid handle"),
		Npc->WerewolfRotDoor2.IsSet());
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
