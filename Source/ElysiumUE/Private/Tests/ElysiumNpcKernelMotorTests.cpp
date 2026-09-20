#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Motor** — `CAI_Motor`, `CAI_Navigator` and everything the NPC asks of its
// motor.
//
// Every threshold asserted here was read out of the pinned retail `vampire.dll`'s `.rdata` at the
// address the body cites, so a case that fails is a divergence from retail and not from an opinion.
// Where an input is a SEAM the case says so: it asserts that the seam is ASKED and that the refusal
// is the recovered one, which is the only honest assertion available until the seam has a source.
// This family has more of those than any other in the story — there is no navigator, no node graph,
// no move probe, no hull table and no collision-extent surface in this substrate.

static constexpr EAutomationTestFlags GElysiumNpcKernelMotorFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Source units into this world's centimetres, Y negated — the inverse of the bodies' own
	// conversion, so a case can place a body at a retail distance.
	FVector PortUnits(double X, double Y, double Z)
	{
		return FVector(X * ElysiumMove::U, -Y * ElysiumMove::U, Z * ElysiumMove::U);
	}

	// `CNPC_VMingXiao`'s tuning record, as the SEAM answers it (every field 0) and as a case can
	// stand it to prove the two field offsets apart.
	float ZeroTuning(int32)
	{
		return 0.f;
	}
}

// --- The movement tunables, slots 521 / 522 / 523 / 524 -------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorTunablesTest,
	"Elysium.Substrate.NpcKernelMotor.Tunables", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorTunablesTest::RunTest(const FString&)
{
	// The species table, every row by name. `CAI_TestHull` is not a spawnable classname in this
	// port's roster, so its row is exercised through the table's own lookup, which is what the
	// slot-521/522/523 bodies consult.
	int32 Count = 0;
	const FElysiumNpc::FJumpTunableSpecies* Rows = FElysiumNpc::JumpTunableSpeciesRows(Count);
	TestEqual(TEXT("three tunable rows: the base line, the Troika line and the test hull"), Count, 3);
	TestNotNull(TEXT("rows"), Rows);

	const FElysiumNpc::FJumpTunableSpecies* Troika =
		FElysiumNpc::JumpTunableSpeciesOf(TEXT("CAI_BaseNPCTroika"));
	TestNotNull(TEXT("CAI_BaseNPCTroika row"), Troika);
	if (Troika != nullptr)
	{
		// `0x101a6b40` `_DAT_10453b94` = 18.0 for the step height slot 522 carries, and Troika's own
		// override of 523 (`0x101aa670`, `_DAT_1044faa8`) = 36.0 — a DIFFERENT constant.
		TestEqual(TEXT("troika step height is 18"), Troika->StepHeight, 18.0f);
		TestEqual(TEXT("troika max jump speed is 36, not the step height"), Troika->MaxJumpSpeed,
			36.0f);
		TestEqual(TEXT("troika jump rise 80"), Troika->JumpLegalRise, 80.0f);
		TestEqual(TEXT("troika jump drop 250"), Troika->JumpLegalDrop, 250.0f);
		TestEqual(TEXT("troika jump distance 160"), Troika->JumpLegalDistance, 160.0f);
	}

	const FElysiumNpc::FJumpTunableSpecies* Base =
		FElysiumNpc::JumpTunableSpeciesOf(TEXT("CAI_BaseNPC"));
	TestNotNull(TEXT("CAI_BaseNPC row"), Base);
	if (Base != nullptr)
	{
		// `0x101a6b60` returns the SAME `_DAT_10453b94` as `0x101a6b40` — the base line's jump speed
		// and its step height are one constant.
		TestEqual(TEXT("base line's jump speed equals its step height"), Base->MaxJumpSpeed,
			Base->StepHeight);
		TestEqual(TEXT("and both are 18"), Base->StepHeight, 18.0f);
	}

	const FElysiumNpc::FJumpTunableSpecies* Hull =
		FElysiumNpc::JumpTunableSpeciesOf(TEXT("CAI_TestHull"));
	TestNotNull(TEXT("CAI_TestHull row"), Hull);
	if (Hull != nullptr)
	{
		// `0x102d72b0` / `0x102d72d0` both return `_DAT_10462950` = 40.0; `0x102d7760` passes
		// 1024/1024/1024.
		TestEqual(TEXT("test hull step height 40"), Hull->StepHeight, 40.0f);
		TestEqual(TEXT("test hull jump speed 40"), Hull->MaxJumpSpeed, 40.0f);
		TestEqual(TEXT("test hull jump rise 1024"), Hull->JumpLegalRise, 1024.0f);
		TestEqual(TEXT("test hull jump drop 1024"), Hull->JumpLegalDrop, 1024.0f);
		TestEqual(TEXT("test hull jump distance 1024"), Hull->JumpLegalDistance, 1024.0f);
	}
	TestNull(TEXT("a class with no row answers null"),
		FElysiumNpc::JumpTunableSpeciesOf(TEXT("CNPC_VNotAClass")));

	// The live slots on a spawnable species, which dispatch the Troika row.
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_tunables"), 4301);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	TestEqual(TEXT("slot 522 answers 18"), Guard->StepHeight(), 18.0f);
	TestEqual(TEXT("slot 523 answers 36"), Guard->GetMaxJumpSpeed(), 36.0f);
	// `0x101a6b80` `_DAT_10477ce8` = 350.0, and no class in the family overrides slot 524.
	TestEqual(TEXT("slot 524 answers 350"), Guard->GetJumpGravity(), 350.0f);
	return true;
}

// --- `IsJumpLegal`, slot 521 ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorJumpLegalTest,
	"Elysium.Substrate.NpcKernelMotor.IsJumpLegal", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorJumpLegalTest::RunTest(const FString&)
{
	// `FUN_10280790` `0x10280790`, four arms in order. `_DAT_104493d0 = 0.1` is added to three of
	// the four thresholds; the apex uses `_DAT_10460020 = 1.25` as a MULTIPLIER on the rise instead.
	auto Legal = [](double StartZ, double ApexZ, double EndZ, double EndX)
	{
		return FElysiumNpc::IsJumpLegalGeometry(FVector(0.0, 0.0, StartZ), FVector(0.0, 0.0, ApexZ),
			FVector(EndX, 0.0, EndZ), 80.0f, 250.0f, 160.0f);
	};

	TestTrue(TEXT("a flat 10-unit hop is legal"), Legal(0.0, 0.0, 0.0, 10.0));
	// Arm 1: `maxRise + 0.1 < end.z - start.z`.
	TestTrue(TEXT("a rise of exactly 80.1 is still legal"), Legal(0.0, 0.0, 80.1, 0.0));
	TestFalse(TEXT("a rise of 80.2 is not"), Legal(0.0, 0.0, 80.2, 0.0));
	// Arm 2: `maxDrop + 0.1 < start.z - end.z`, with the apex measured from the START so a drop
	// needs an apex at the start height to clear arm 3.
	//
	// **On the base thresholds this arm is unreachable**, and that is a recovered fact rather than a
	// gap in the case: a drop big enough to trip the 250 is also a 3-D distance big enough to trip
	// the 160, and arm 4 runs on the same numbers. The boundary is therefore asserted on
	// `CAI_TestHull`'s 1024/1024/1024, where the two agree.
	TestTrue(TEXT("a 150-unit drop is legal on the base thresholds"),
		Legal(150.0, 150.0, 0.0, 0.0));
	TestFalse(TEXT("a 250-unit drop is not — the 160 distance arm forecloses the 250 drop arm"),
		Legal(250.0, 250.0, 0.0, 0.0));
	auto Hull = [](double StartZ, double ApexZ, double EndZ, double EndX)
	{
		return FElysiumNpc::IsJumpLegalGeometry(FVector(0.0, 0.0, StartZ), FVector(0.0, 0.0, ApexZ),
			FVector(EndX, 0.0, EndZ), 1024.0f, 1024.0f, 1024.0f);
	};
	TestTrue(TEXT("the test hull admits a drop of exactly 1024.1"),
		Hull(1024.1, 1024.1, 0.0, 0.0));
	TestFalse(TEXT("and refuses 1024.2"), Hull(1024.2, 1024.2, 0.0, 0.0));
	// Arm 3: `maxRise * 1.25 < apex.z - start.z` — 100.0 for the 80.0 rise, and NOT slackened by 0.1.
	TestTrue(TEXT("an apex exactly 100 above the start is legal"), Legal(0.0, 100.0, 0.0, 0.0));
	TestFalse(TEXT("an apex 100.1 above it is not"), Legal(0.0, 100.1, 0.0, 0.0));
	// Arm 4: the 3-D distance, with the SAME 0.1 slack.
	TestTrue(TEXT("a flat 160.1-unit jump is legal"), Legal(0.0, 0.0, 0.0, 160.1));
	TestFalse(TEXT("a flat 160.2-unit jump is not"), Legal(0.0, 0.0, 0.0, 160.2));

	// `CAI_TestHull::IsJumpLegal` `0x102d7760` — the same body with 1024 everywhere.
	TestTrue(TEXT("the test hull's 1024 rise admits what the base refuses"),
		FElysiumNpc::IsJumpLegalGeometry(FVector::ZeroVector, FVector::ZeroVector,
			FVector(0.0, 0.0, 300.0), 1024.0f, 1024.0f, 1024.0f));
	TestFalse(TEXT("and its apex scale is still 1.25 of the rise"),
		FElysiumNpc::IsJumpLegalGeometry(FVector::ZeroVector, FVector(0.0, 0.0, 1280.1),
			FVector::ZeroVector, 1024.0f, 1024.0f, 1024.0f));
	return true;
}

// --- The yaw-speed ladders, slot 516 --------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorYawLaddersTest,
	"Elysium.Substrate.NpcKernelMotor.MaxYawSpeedLadders", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorYawLaddersTest::RunTest(const FString&)
{
	// `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` — one constant, `_DAT_1049949c`.
	TestEqual(TEXT("the base line is 45 for everything"), FElysiumNpc::MaxYawSpeedBase(), 45.0f);

	// `CAI_BaseHumanoid::MaxYawSpeed` `0x102624b0`: 9/0x12/0x13 -> 15.0, 0x3b/0x3c -> 60.0,
	// default 45.0.
	TestEqual(TEXT("humanoid ACT_WALK is 15"), FElysiumNpc::MaxYawSpeedHumanoid(9), 15.0f);
	TestEqual(TEXT("humanoid 0x12 is 15"), FElysiumNpc::MaxYawSpeedHumanoid(0x12), 15.0f);
	TestEqual(TEXT("humanoid ACT_RUN is 15"), FElysiumNpc::MaxYawSpeedHumanoid(0x13), 15.0f);
	TestEqual(TEXT("humanoid 0x3b is 60"), FElysiumNpc::MaxYawSpeedHumanoid(0x3b), 60.0f);
	TestEqual(TEXT("humanoid 0x3c is 60"), FElysiumNpc::MaxYawSpeedHumanoid(0x3c), 60.0f);
	TestEqual(TEXT("humanoid 0x3d falls to 45"), FElysiumNpc::MaxYawSpeedHumanoid(0x3d), 45.0f);
	TestEqual(TEXT("humanoid ACT_IDLE falls to 45"), FElysiumNpc::MaxYawSpeedHumanoid(1), 45.0f);

	// `CGeneric_NPC::MaxYawSpeed` `0x1035a810`, and byte-identically `CGeneric_NPC_bathack`
	// `0x1035b080` and `CGenericSabbat_NPC` `0x1035be80`: 0x13 -> 160.0, 0x3b..0x3c -> 120.0,
	// else 45.0. Note the band is EXCLUSIVE at both ends of `0x3a` and `0x3d`.
	TestEqual(TEXT("generic ACT_RUN is 160"), FElysiumNpc::MaxYawSpeedGeneric(0x13), 160.0f);
	TestEqual(TEXT("generic 0x3b is 120"), FElysiumNpc::MaxYawSpeedGeneric(0x3b), 120.0f);
	TestEqual(TEXT("generic 0x3c is 120"), FElysiumNpc::MaxYawSpeedGeneric(0x3c), 120.0f);
	TestEqual(TEXT("generic 0x3a is not"), FElysiumNpc::MaxYawSpeedGeneric(0x3a), 45.0f);
	TestEqual(TEXT("generic 0x3d is not"), FElysiumNpc::MaxYawSpeedGeneric(0x3d), 45.0f);
	TestEqual(TEXT("generic ACT_WALK is 45 — it has no walk arm"),
		FElysiumNpc::MaxYawSpeedGeneric(9), 45.0f);

	// `CNPC_VMingXiao::MaxYawSpeed` `0x10394930` — the tuning record's +0x48 inside the
	// (0x1129, 0x112e) band and +0x44 outside it. The SEAM answers 0 for every field, so the case
	// stands the record to prove which offset each arm reaches.
	auto Field = [](int32 Offset) { return Offset == 0x48 ? 7.0f : 3.0f; };
	TestEqual(TEXT("mingxiao 0x112a reads +0x48"), FElysiumNpc::MaxYawSpeedMingXiao(0x112a, Field),
		7.0f);
	TestEqual(TEXT("mingxiao 0x112d reads +0x48"), FElysiumNpc::MaxYawSpeedMingXiao(0x112d, Field),
		7.0f);
	TestEqual(TEXT("mingxiao 0x1129 reads +0x44"), FElysiumNpc::MaxYawSpeedMingXiao(0x1129, Field),
		3.0f);
	TestEqual(TEXT("mingxiao 0x112e reads +0x44"), FElysiumNpc::MaxYawSpeedMingXiao(0x112e, Field),
		3.0f);
	TestEqual(TEXT("and through the SEAM every arm answers 0"),
		FElysiumNpc::MaxYawSpeedMingXiao(0x112a, ZeroTuning), 0.0f);

	// The species table, every row by name with its retail address.
	int32 Count = 0;
	const FElysiumNpc::FMaxYawSpeedSpecies* Rows = FElysiumNpc::MaxYawSpeedSpeciesRows(Count);
	TestEqual(TEXT("nine classes replace slot 516"), Count, 9);
	const TCHAR* Expected[][2] = {
		{ TEXT("CAI_BaseHumanoid"), TEXT("0x102624b0") },
		{ TEXT("CGeneric_NPC"), TEXT("0x1035a810") },
		{ TEXT("CGeneric_NPC_bathack"), TEXT("0x1035b080") },
		{ TEXT("CGenericSabbat_NPC"), TEXT("0x1035be80") },
		{ TEXT("CNPC_VDog"), TEXT("0x10374130") },
		{ TEXT("CNPC_VMingXiao"), TEXT("0x10394930") },
		{ TEXT("CNPC_VTzimisce"), TEXT("0x103ba020") },
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103d0a30") },
		{ TEXT("CAI_BaseNPC"), TEXT("0x10280bb0") },
	};
	for (int32 Index = 0; Index < Count && Index < UE_ARRAY_COUNT(Expected); ++Index)
	{
		TestEqual(FString::Printf(TEXT("row %d class"), Index), FString(Rows[Index].RetailClass),
			FString(Expected[Index][0]));
		TestEqual(FString::Printf(TEXT("row %d body"), Index), FString(Rows[Index].Body516),
			FString(Expected[Index][1]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorYawTroikaTest,
	"Elysium.Substrate.NpcKernelMotor.MaxYawSpeedTroika", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorYawTroikaTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_yaw"), 4302);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `CAI_BaseNPCTroika::MaxYawSpeed` `0x10297ce0`, arm by arm on the activity word (+0x0fec).
	Guard->ActivityNumber = 0x3b;
	TestEqual(TEXT("0x3b is 30"), Guard->MaxYawSpeed(), 30.0f);
	Guard->ActivityNumber = 0x3c;
	TestEqual(TEXT("0x3c is 30"), Guard->MaxYawSpeed(), 30.0f);
	Guard->ActivityNumber = 0x13;
	TestEqual(TEXT("ACT_RUN is 160"), Guard->MaxYawSpeed(), 160.0f);
	Guard->ActivityNumber = 0x1093;
	TestEqual(TEXT("0x1093 is 160"), Guard->MaxYawSpeed(), 160.0f);
	Guard->ActivityNumber = 0x1096;
	TestEqual(TEXT("0x1096 is 160"), Guard->MaxYawSpeed(), 160.0f);
	Guard->ActivityNumber = 0x1121;
	TestEqual(TEXT("0x1121 is 30"), Guard->MaxYawSpeed(), 30.0f);
	Guard->ActivityNumber = 0x1092;
	TestEqual(TEXT("0x1092 falls to 45"), Guard->MaxYawSpeed(), 45.0f);

	// The idle and walk arms take the `debug_slow_*` cvars while the per-state capability byte's
	// bit 7 is CLEAR, which it is for every state but combat (0x8f) and flee (0x85). The two cvars'
	// names and defaults were read from the `ConVar` constructor's own argument strings at
	// `0x105d902c` / `0x1057ba50` and `0x105d9008` / `0x105d9028`.
	Guard->ActivityNumber = 1;
	TestEqual(TEXT("idle out of combat is debug_slow_idle_yaw_speed's default, 20"),
		Guard->MaxYawSpeed(), 20.0f);
	Guard->ActivityNumber = 5;
	TestEqual(TEXT("0x5 takes the same arm"), Guard->MaxYawSpeed(), 20.0f);
	Guard->ActivityNumber = 9;
	TestEqual(TEXT("walk out of combat is debug_slow_walk_yaw_speed's default, 25"),
		Guard->MaxYawSpeed(), 25.0f);

	// That gate is the per-state capability byte (+0x5b64), which is `ELYSIUM_NPC_WORD_IMPLICIT` —
	// a pure function of the state here. Bit 7 is set only for the combat (0x8f) and flee (0x85)
	// states, and a fixture-spawned guard is neither, which is why the two cvar arms above are the
	// ones reached. The other side of that gate is the turning-anims branch, whose cvar is the
	// Facing family's SEAM for the unconstructed `0x109247ec` and answers false.
	TestEqual(TEXT("an idle guard's state byte has bit 7 clear"),
		static_cast<int32>(Guard->NpcStateFlags() & 0x80), 0);
	TestFalse(TEXT("the turning-anims cvar seam refuses"), Guard->TurningAnimsEnabled());

	// `PLAYING_FACE_ANIM` (word one, 0x8000000) suppresses the whole ladder.
	Guard->ActivityNumber = 0x13;
	Guard->NpcFlags.Set(EElysiumNpcFlag::PLAYING_FACE_ANIM);
	TestEqual(TEXT("a face anim suppresses even ACT_RUN's 160"), Guard->MaxYawSpeed(), 45.0f);
	Guard->NpcFlags.Clear(EElysiumNpcFlag::PLAYING_FACE_ANIM);

	// The turning arm: `m_afMemory & 0x2000` beats every activity. `GetIdealYawSpeed()` is a
	// generated slot stub and the scalar cvar is unrecovered, so the product is 0 and the arm lands
	// on retail's own floor `_DAT_104454c0` = 1.0.
	Guard->ScheduleHost.MemoryBits |= 0x2000;
	TestEqual(TEXT("the turning arm answers the recovered floor, 1.0"), Guard->MaxYawSpeed(), 1.0f);
	TestEqual(TEXT("and that is what the arm itself answers for the Troika cvar"),
		Guard->MaxYawSpeedTurningArm(TEXT("0x10924c94")), 1.0f);
	TestEqual(TEXT("as it does for the Dog's"),
		Guard->MaxYawSpeedTurningArm(TEXT("0x1093ad24")), 1.0f);
	TestEqual(TEXT("and the Tzimisce's"),
		Guard->MaxYawSpeedTurningArm(TEXT("0x1093c9fc")), 1.0f);
	Guard->ScheduleHost.MemoryBits &= ~0x2000u;

	// `CNPC_VDog` `0x10374130` and `CNPC_VTzimisce` `0x103ba020` replace the ladder wholesale. The
	// guard is neither, so the two are driven directly — a species table row is a behaviour, not a
	// type, and the behaviour is what is under test.
	Guard->ActivityNumber = 0x13;
	TestEqual(TEXT("the Dog's ACT_RUN is 40, not 160"), Guard->MaxYawSpeedDog(), 40.0f);
	Guard->ActivityNumber = 0x3b;
	TestEqual(TEXT("the Dog's 0x3b is 30"), Guard->MaxYawSpeedDog(), 30.0f);
	Guard->ActivityNumber = 9;
	TestEqual(TEXT("the Dog has no walk arm and answers 45"), Guard->MaxYawSpeedDog(), 45.0f);
	// The Dog's idle arm has NO state gate in front of it, so it is the one place this suite can
	// reach the unrecovered cvar `0x10923e84` directly: with `TurningAnimsEnabled()` false it is
	// consulted, and a cvar this substrate cannot construct answers retail's own `IsCommand()` arm.
	Guard->ActivityNumber = 1;
	TestEqual(TEXT("the Dog's idle arm reaches the unrecovered cvar and answers 0"),
		Guard->MaxYawSpeedDog(), 0.0f);
	Guard->ActivityNumber = 5;
	TestEqual(TEXT("0x5 takes the same arm"), Guard->MaxYawSpeedDog(), 0.0f);
	Guard->ActivityNumber = 1;
	TestEqual(TEXT("the Tzimisce's ACT_IDLE is 5"), Guard->MaxYawSpeedTzimisce(), 5.0f);
	Guard->ActivityNumber = 0xfc;
	TestEqual(TEXT("0xfc takes the same arm"), Guard->MaxYawSpeedTzimisce(), 5.0f);
	Guard->ActivityNumber = 0x13;
	TestEqual(TEXT("the Tzimisce's ACT_RUN is 30"), Guard->MaxYawSpeedTzimisce(), 30.0f);
	Guard->ActivityNumber = 9;
	TestEqual(TEXT("and its default is 11, not 45"), Guard->MaxYawSpeedTzimisce(), 11.0f);
	return true;
}

// --- The console variables ------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorConVarsTest,
	"Elysium.Substrate.NpcKernelMotor.ConVars", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorConVarsTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FRetailYawConVar* Rows = FElysiumNpc::RetailYawConVars(Count);
	TestEqual(TEXT("six cvar rows"), Count, 6);
	TestNotNull(TEXT("rows"), Rows);

	// The two `MaxYawSpeed` constructs in its own body: name and default read from the `ConVar`
	// constructor's argument strings.
	TestEqual(TEXT("the idle cvar's default is 20"),
		FElysiumNpc::RetailYawConVarValue(TEXT("0x10924e94")), 20.0f);
	TestEqual(TEXT("the walk cvar's default is 25"),
		FElysiumNpc::RetailYawConVarValue(TEXT("0x1092411c")), 25.0f);

	// The four that live in uninitialised `.data` and are never constructed in the corpus: name and
	// default **unrecovered**, so the seam answers retail's own `IsCommand()` arm.
	const TCHAR* Unrecovered[] = { TEXT("0x10924c94"), TEXT("0x1093ad24"), TEXT("0x1093c9fc"),
		TEXT("0x10923e84") };
	for (const TCHAR* Address : Unrecovered)
	{
		TestEqual(FString::Printf(TEXT("%s answers 0"), Address),
			FElysiumNpc::RetailYawConVarValue(Address), 0.0f);
	}

	int32 Recovered = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Rows[Index].bDefaultRecovered)
		{
			++Recovered;
			TestTrue(TEXT("a recovered row carries its name"),
				FCString::Strlen(Rows[Index].Name) > 0);
		}
		else
		{
			TestEqual(TEXT("an unrecovered row carries no name"), FString(Rows[Index].Name),
				FString());
		}
	}
	TestEqual(TEXT("exactly two of the six are recovered"), Recovered, 2);
	TestEqual(TEXT("an address in no row answers 0"),
		FElysiumNpc::RetailYawConVarValue(TEXT("0xdeadbeef")), 0.0f);
	return true;
}

// --- Slots 68 and 69 — the collision-ignore chains -----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorIgnoreCollisionTest,
	"Elysium.Substrate.NpcKernelMotor.IgnoreCollision", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorIgnoreCollisionTest::RunTest(const FString&)
{
	// The species table, every row by name with its retail addresses.
	int32 Count = 0;
	const FElysiumNpc::FIgnoreCollisionSpecies* Rows =
		FElysiumNpc::IgnoreCollisionSpeciesRows(Count);
	TestEqual(TEXT("seven classes add an arm to slot 68 or 69"), Count, 7);
	TestEqual(TEXT("the Gargoyle replaces 69 only"), FString(Rows[0].RetailClass),
		FString(TEXT("CNPC_VGargoyle")));
	TestEqual(TEXT("with no 68 body"), FString(Rows[0].Body68), FString());
	TestEqual(TEXT("and 69 at 0x10379490"), FString(Rows[0].Body69),
		FString(TEXT("0x10379490")));
	TestEqual(TEXT("the Hengeyokai's 69"), FString(Rows[1].Body69), FString(TEXT("0x10380f90")));
	TestEqual(TEXT("MingXiao's 69"), FString(Rows[2].Body69), FString(TEXT("0x10396fd0")));
	TestEqual(TEXT("the tentacle replaces both"), FString(Rows[3].Body68),
		FString(TEXT("0x1039eb50")));
	TestEqual(TEXT("and its 69"), FString(Rows[3].Body69), FString(TEXT("0x1039eb90")));
	TestEqual(TEXT("the Rat replaces 68 only"), FString(Rows[4].Body68),
		FString(TEXT("0x103ad6d0")));
	TestEqual(TEXT("with no 69 body"), FString(Rows[4].Body69), FString());
	TestEqual(TEXT("the Tzimisce's 69"), FString(Rows[5].Body69), FString(TEXT("0x103bfa00")));
	TestEqual(TEXT("the Werewolf replaces both"), FString(Rows[6].Body68),
		FString(TEXT("0x103d9ab0")));
	TestEqual(TEXT("and its 69"), FString(Rows[6].Body69), FString(TEXT("0x103d9ba0")));

	// `CNPC_VGargoyle::NavIgnoreCollision` `0x10379490`'s three `FClassnameIs` compares, which use
	// `__strcmpi` and so are case-insensitive.
	TestTrue(TEXT("prop_dynamic"), FElysiumNpc::GargoyleIgnoresClassname(TEXT("prop_dynamic")));
	TestTrue(TEXT("PROP_DYNAMIC — the compare is case-insensitive"),
		FElysiumNpc::GargoyleIgnoresClassname(TEXT("PROP_DYNAMIC")));
	TestTrue(TEXT("func_brush"), FElysiumNpc::GargoyleIgnoresClassname(TEXT("func_brush")));
	TestTrue(TEXT("func_door_rotating"),
		FElysiumNpc::GargoyleIgnoresClassname(TEXT("func_door_rotating")));
	TestFalse(TEXT("func_door is NOT one of the three"),
		FElysiumNpc::GargoyleIgnoresClassname(TEXT("func_door")));
	TestFalse(TEXT("prop_physics is not either"),
		FElysiumNpc::GargoyleIgnoresClassname(TEXT("prop_physics")));

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_collide"), 4303);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("other"), FVector(200.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("rat"), FVector(400.0, 0.0, 0.0), TEXT("npc_VRat"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	FElysiumNpc* Rat = Fixture.Npc(TEXT("rat"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("other"), Other)
		|| !TestNotNull(TEXT("rat"), Rat))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other, Rat });

	// The shared head, arm 1. Without `NAV_IGNORE_NPC` the only NPC arm is the sleeping one.
	TestFalse(TEXT("an ordinary NPC is not ignored"), Guard->IgnoreCollisionSharedHead(Other));
	Other->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestTrue(TEXT("a SLEEPING NPC is"), Guard->IgnoreCollisionSharedHead(Other));
	Other->NpcFlags.Clear(EElysiumNpcFlag::SLEEPING);

	// With `NAV_IGNORE_NPC` set, any NPC and the player are ignored and the sleeping arm is not
	// reached at all.
	Guard->NpcFlags.Set(EElysiumNpcFlag::NAV_IGNORE_NPC);
	TestTrue(TEXT("NAV_IGNORE_NPC ignores every NPC"), Guard->IgnoreCollisionSharedHead(Other));
	TestTrue(TEXT("and the player"), Guard->IgnoreCollisionSharedHead(Fixture.Player()));
	// `m_bForceNPCCheck` suppresses the whole first arm, which is exactly what `CanStandAt` brackets
	// its probe with.
	Guard->bForceNpcCheck = true;
	TestFalse(TEXT("m_bForceNPCCheck suppresses arm 1 entirely"),
		Guard->IgnoreCollisionSharedHead(Other));
	Guard->bForceNpcCheck = false;
	Guard->NpcFlags.Clear(EElysiumNpcFlag::NAV_IGNORE_NPC);

	// Arm 2: the kicked physics prop.
	Guard->ScheduleHost.KickProp = Other->Handle;
	TestTrue(TEXT("the kick prop is ignored"), Guard->IgnoreCollisionSharedHead(Other));
	Guard->ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();

	// A null candidate reaches none of the arms.
	TestFalse(TEXT("a null candidate is not ignored"),
		Guard->IgnoreCollisionSharedHead(nullptr));

	// The two slots on an ordinary Troika species. `m_edtDerivedType` is a SEAM answering 0, so both
	// derived-type gates fall through and the chains reach their `IsIgnoreCollisionEntity` tail,
	// which has no handle to compare against.
	TestFalse(TEXT("slot 68 declines for an ordinary NPC"), Guard->ShouldIgnoreCollision(Other));
	TestFalse(TEXT("slot 69 declines too"), Guard->NavIgnoreCollision(Other));
	// `m_bNavIgnorePhysicsProps` swaps the NAV chain's mask from 0x12 to 0x16 — unobservable while
	// the derived-type seam answers 0, and the case says so.
	Guard->bNavIgnorePhysicsProps = true;
	TestFalse(TEXT("and still declines with m_bNavIgnorePhysicsProps set, because the "
		"m_edtDerivedType seam answers 0"), Guard->NavIgnoreCollision(Other));
	Guard->bNavIgnorePhysicsProps = false;

	// The tail: `m_hIgnoreCollisionEntity` (+0x055c), which nothing writes in this runtime yet.
	TestFalse(TEXT("the tail refuses with no handle"), Guard->IsIgnoreCollisionEntityTail(Other));
	Guard->IgnoreCollisionEntity = Other->Handle;
	TestTrue(TEXT("and answers true once the handle names the candidate"),
		Guard->IsIgnoreCollisionEntityTail(Other));
	TestFalse(TEXT("but not for a different candidate"), Guard->IsIgnoreCollisionEntityTail(Rat));
	Guard->IgnoreCollisionEntity = FElysiumEntityHandle::Invalid();

	// `CNPC_VRat::ShouldIgnoreCollision` `0x103ad6d0`: the fixed global entity, else the base. The
	// global is a SEAM answering null, so the rat declines like anybody else.
	TestTrue(TEXT("the rat resolves to CNPC_VRat"), Rat->IsRetailClass(TEXT("CNPC_VRat")));
	TestFalse(TEXT("and its slot 68 declines while the global-entity seam answers null"),
		Rat->ShouldIgnoreCollision(Other));
	TestNull(TEXT("which it does"), Rat->RatIgnoredGlobalEntity());
	return true;
}

// --- Slots 133, 153, 166, 210, 525, 575 and `AutoMovement` ---------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorSlotsTest,
	"Elysium.Substrate.NpcKernelMotor.Slots", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorSlotsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_slots"), 4304);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("other"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("other"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other });

	// Slot 210 `0x10027370` — the three shared statics `DAT_1070d1b0/b4/b8` sit in `.data`'s
	// zero-initialised tail and nothing writes them, so the contribution is `vec3_origin`.
	FVector GroundVelocity(1.0, 2.0, 3.0);
	Guard->GetGroundVelocityToApply(GroundVelocity);
	TestEqual(TEXT("slot 210 writes vec3_origin over whatever was there"), GroundVelocity,
		FVector::ZeroVector);

	// Slot 166 `0x10026f80`: a null candidate is standable, a non-null one defers to its
	// `IsStandable()` — a SEAM answering false.
	// The cast disambiguates slot 166 `CanStandOn(CBaseEntity*)` from slot 165's
	// `CanStandOn(edict_t*)`, which the generator spells `CanStandOn(void*)`.
	TestTrue(TEXT("slot 166 admits a null candidate"),
		Guard->CanStandOn(static_cast<FElysiumEntity*>(nullptr)));
	TestFalse(TEXT("and refuses a real one while IsStandable is a seam"),
		Guard->CanStandOn(Other));

	// Slot 525 `0x1027da90` — the base declines unconditionally, and the guard is not a `CNPC_Crow`.
	TestFalse(TEXT("slot 525 declines"), Guard->OverrideMove(0.1f));
	TestEqual(TEXT("and the Crow handler was not reached"), Guard->MotorSeams.CrowOverrideMoves, 0);

	// Slot 153 `0x10280300` forwards to the navigator's `IsGoalActive`. With no motor in a headless
	// fixture there is no goal.
	TestFalse(TEXT("slot 153 answers false with no motor"), Guard->IsMoving());
	TestFalse(TEXT("and so does the seam it forwards to"), Guard->NavIsGoalActive());

	// Slot 133 `0x101c1720`. Neither the mover words nor `m_pfnMoveDone` exist here; what is
	// asserted is that the dispatch was reached.
	TestEqual(TEXT("no move-done dispatch yet"), Guard->MotorSeams.MoveDone, 0);
	Guard->MoveDone();
	Guard->MoveDone();
	TestEqual(TEXT("two dispatches"), Guard->MotorSeams.MoveDone, 2);

	// Slot 575 `0x102bf4a0`. The gate needs an enemy, `MOVE_FACE_ENEMY`, an active weapon AND the
	// weapon's `0x6000` capability bits — the last of which is a SEAM answering 0, so the Troika
	// gate closes before the base rung is ever reached.
	TestFalse(TEXT("slot 575 declines with no enemy"), Guard->ShouldMoveAndShoot());
	Guard->Senses.Memory.Enemy = Other->Handle;
	Guard->NpcFlags.Set(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	Guard->CapabilityWord |= (1 << 6);
	TestEqual(TEXT("the weapon capability seam answers 0"),
		static_cast<int32>(Guard->ActiveWeaponCapabilityWord()), 0);
	TestFalse(TEXT("so slot 575 still declines even with bits_CAP_MOVE_SHOOT set"),
		Guard->ShouldMoveAndShoot());
	Guard->NpcFlags.Clear(EElysiumNpcFlag2::MOVE_FACE_ENEMY);
	Guard->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();

	// `AutoMovement` `0x10280a50`. The recovered GATE is `GetMoveType() == 4` with `FL_FROZEN 0x400`
	// clear; both fail here, so the apply is never reached — which is the whole point of porting the
	// gate rather than the extraction.
	TestEqual(TEXT("no interval movement applied yet"),
		Guard->MotorSeams.IntervalMovementApplied, 0);
	TestFalse(TEXT("AutoMovement refuses at move type != 4"), Guard->AutoMovement());
	TestEqual(TEXT("and the motor was never asked"),
		Guard->MotorSeams.IntervalMovementApplied, 0);
	return true;
}

// --- The navigator seam ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorNavigatorTest,
	"Elysium.Substrate.NpcKernelMotor.Navigator", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorNavigatorTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_nav"), 4305);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `FUN_1027d990` / `FUN_1027d9b0` — the one navigator word this family reads and writes.
	TestEqual(TEXT("the nav type starts at Ground 0"), Guard->NavGetType(), 0);
	Guard->NavSetType(2);
	TestEqual(TEXT("and takes the value it is given"), Guard->NavGetType(), 2);
	Guard->NavSetType(0);

	// `CAI_Navigator::vfunc3` `0x102ecb50` — the owner-pointer snapshot, of which only the fact and
	// the argument survive the port.
	TestFalse(TEXT("no snapshot taken yet"), Guard->Navigator.bSnapshotTaken);
	Guard->NavSnapshotOwnerPointers(7);
	TestTrue(TEXT("the snapshot is recorded"), Guard->Navigator.bSnapshotTaken);
	TestEqual(TEXT("with its argument"), Guard->Navigator.SnapshotArgument, 7);

	// `FUN_1027a6c0` `0x1027a6c0` — the link's cached activity, or `ACT_IDLE` 1. The link is a SEAM,
	// so the answer is always the fallback.
	int32 Activity = 0;
	TestFalse(TEXT("the link seam refuses"), Guard->NavLinkActivity(Activity));
	TestEqual(TEXT("so the resolved activity is ACT_IDLE"), Guard->ResolveLinkActivity(), 1);

	// `FUN_10382d20` `0x10382d20` — the cancel half of the same unrecovered link object.
	TestEqual(TEXT("no cancels yet"), Guard->MotorSeams.LinkFacingCancels, 0);
	Guard->ClearLinkActivity();
	TestEqual(TEXT("one cancel"), Guard->MotorSeams.LinkFacingCancels, 1);

	// `CAI_Navigator::OnNavFailed` `0x102eeae0` — `TaskFail`, then the ideal activity from the link,
	// then the failed latch. `CAI_Navigator#9` `0x102eeb50` is a tail-jump into the same body.
	TestFalse(TEXT("the navigator has not failed"), Guard->Navigator.bNavFailed);
	Guard->NavOnNavFailed(0x1b);
	TestTrue(TEXT("OnNavFailed sets the latch"), Guard->Navigator.bNavFailed);
	TestEqual(TEXT("and re-plays the resolved link activity, which is ACT_IDLE"),
		Guard->IdealActivityNumber, 1);
	TestEqual(TEXT("and routes the caller's reason through TaskFail"),
		Guard->ScheduleHost.FailureReason, 0x1b);

	// `FUN_102bf7e0` `0x102bf7e0` — `m_bShouldMove` is set UNCONDITIONALLY, outside the stop.
	Guard->ScheduleHost.bShouldMove = false;
	Guard->ResumeScheduledMove();
	TestTrue(TEXT("ResumeScheduledMove sets bShouldMove even with no active goal"),
		Guard->ScheduleHost.bShouldMove);

	// `FUN_1029f6c0` `0x1029f6c0` — the node-graph read. Every index is out of range on an empty
	// array, which is retail's own counted-refusal arm.
	TestEqual(TEXT("the node graph answers 0 for any route step"), Guard->NavNodeWordAt(0), 0);
	TestEqual(TEXT("including a negative one"), Guard->NavNodeWordAt(-1), 0);

	// Slot 528 `0x10280360`. The gate is the navigator's goal state, a SEAM answering -1, so the
	// body answers true — retail's own answer for every goal type but 6 — and the cover trace is
	// never reached.
	TestEqual(TEXT("the goal-state seam answers -1"), Guard->NavGoalState(), INDEX_NONE);
	const int32 TracesBefore = Guard->MotorSeams.HullTraces;
	TestTrue(TEXT("slot 528 validates"), Guard->ValidateNavGoal());
	TestEqual(TEXT("and ran no trace"), Guard->MotorSeams.HullTraces, TracesBefore);
	FVector Goal(1.0, 2.0, 3.0);
	TestFalse(TEXT("the goal-position seam refuses"), Guard->NavGoalPosition(Goal));
	TestEqual(TEXT("and leaves the caller's vector alone"), Goal, FVector(1.0, 2.0, 3.0));
	return true;
}

// --- The ground and stand probes ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorProbesTest,
	"Elysium.Substrate.NpcKernelMotor.Probes", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorProbesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_probes"), 4306);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `CAI_BaseNPCTroika::CanStandAt` `0x102a0ed0` — the whole recovered behaviour is the
	// `m_bForceNPCCheck` bracket around the probe. The probe is a SEAM, so what is asserted is that
	// it was asked and that the bracket closed.
	TestFalse(TEXT("bForceNpcCheck starts clear"), Guard->bForceNpcCheck);
	const int32 ProbesBefore = Guard->MotorSeams.MoveProbeChecks;
	TestFalse(TEXT("CanStandAt refuses while the move probe is a seam"),
		Guard->CanStandAt(FVector(10.0, 0.0, 0.0), 0));
	TestEqual(TEXT("and the probe was asked exactly once"), Guard->MotorSeams.MoveProbeChecks,
		ProbesBefore + 1);
	TestFalse(TEXT("and the bracket closed"), Guard->bForceNpcCheck);

	// `CAI_BaseNPC::CheckOnGround` `0x1026e5e0`. Without condition 0x73 and at nav type 0 the body
	// stamps its deadline (`curtime + 0.5`, `_DAT_104454d0`) and then asks for the trace.
	const EElysiumNpcCond OnGround = static_cast<EElysiumNpcCond>(0x73);
	TestEqual(TEXT("the deadline starts at 0"), Guard->CheckOnGroundTime, 0.0);
	const int32 TracesBefore = Guard->MotorSeams.HullTraces;
	Guard->CheckOnGround();
	TestTrue(TEXT("CheckOnGround stamped its 0.5-second deadline"),
		FMath::IsNearlyEqual(Guard->CheckOnGroundTime, Fixture.World.NowSeconds() + 0.5, 1e-6));
	TestEqual(TEXT("and asked for one hull trace"), Guard->MotorSeams.HullTraces,
		TracesBefore + 1);
	TestFalse(TEXT("which the seam refused, so the ground condition is untouched"),
		Guard->Cognition.Conditions.Has(OnGround));

	// The deadline gate: a second call inside the window does nothing at all.
	const int32 TracesAfterFirst = Guard->MotorSeams.HullTraces;
	Guard->CheckOnGround();
	TestEqual(TEXT("a second call inside the 0.5-second window traces nothing"),
		Guard->MotorSeams.HullTraces, TracesAfterFirst);

	// The `HasCondition(0x73)` arm: with the condition set and FL_ONGROUND clear and nav type 0 the
	// body returns without clearing it; with either of those two false it clears it.
	Guard->Cognition.Conditions.Set(OnGround);
	Guard->CheckOnGround();
	TestTrue(TEXT("airborne on nav type 0 keeps the condition"),
		Guard->Cognition.Conditions.Has(OnGround));
	Guard->Flags |= 1;   // FL_ONGROUND
	Guard->CheckOnGround();
	TestFalse(TEXT("FL_ONGROUND clears it"), Guard->Cognition.Conditions.Has(OnGround));
	Guard->Flags &= ~1;
	Guard->Cognition.Conditions.Set(OnGround);
	Guard->NavSetType(1);
	Guard->CheckOnGround();
	TestFalse(TEXT("so does a non-ground nav type"), Guard->Cognition.Conditions.Has(OnGround));
	Guard->NavSetType(0);

	// `CNPC_VWerewolf::GetGroundpoint` `0x103d6a40`. With no hull table and no trace the body lands
	// on retail's own no-hit arm, which answers `DAT_10713de0/de4/de8` — and `staticinit_101371a0`
	// fills all three with `0x7f7fffff`, so that fallback is **`vec3_invalid`, not `vec3_origin`**.
	const FVector Ground = Guard->GetGroundpoint(FVector(100.0, 100.0, 100.0));
	TestEqual(TEXT("GetGroundpoint answers retail's no-hit fallback, vec3_invalid"),
		static_cast<float>(Ground.X), 3.4028234663852886e+38f);
	TestEqual(TEXT("in all three terms"), static_cast<float>(Ground.Z),
		3.4028234663852886e+38f);
	// The hull table, replayed from the image. Hull 0 is HUMAN_HULL, and its two extent pairs
	// differ: the full box is 13 wide and the small one 8, at the same 72 height.
	FVector Mins(1.0, 1.0, 1.0);
	FVector Maxs(2.0, 2.0, 2.0);
	TestTrue(TEXT("the hull table answers for hull 0"),
		Guard->RetailHullExtents(0, FElysiumNpc::EElysiumHullExtents::Full, Mins, Maxs));
	TestEqual(TEXT("HUMAN_HULL's full mins"), Mins, FVector(-13.0, -13.0, 0.0));
	TestEqual(TEXT("...and its full maxs"), Maxs, FVector(13.0, 13.0, 72.0));
	TestTrue(TEXT("the small pair is the same row's other box"),
		Guard->RetailHullExtents(0, FElysiumNpc::EElysiumHullExtents::Small, Mins, Maxs));
	TestEqual(TEXT("HUMAN_HULL's small mins"), Mins, FVector(-8.0, -8.0, 0.0));
	TestEqual(TEXT("...and its small maxs"), Maxs, FVector(8.0, 8.0, 72.0));
	// Retail's table has 22 rows; a hull id outside it keeps the refusal every caller's failure
	// arm is written against.
	TestFalse(TEXT("a hull id the table does not carry still refuses"),
		Guard->RetailHullExtents(22, FElysiumNpc::EElysiumHullExtents::Full, Mins, Maxs));
	TestEqual(TEXT("and zeroes both extents"), Mins, FVector::ZeroVector);
	TestEqual(TEXT("both"), Maxs, FVector::ZeroVector);
	// Not every small box is smaller: TZIMISCE1 (bit 10) widens from 35 to 45.
	TestTrue(TEXT("TZIMISCE1's full box"),
		Guard->RetailHullExtents(10, FElysiumNpc::EElysiumHullExtents::Full, Mins, Maxs));
	TestEqual(TEXT("reaches 35"), Maxs.X, 35.0);
	TestTrue(TEXT("and its SMALL box is wider"),
		Guard->RetailHullExtents(10, FElysiumNpc::EElysiumHullExtents::Small, Mins, Maxs));
	TestEqual(TEXT("at 45"), Maxs.X, 45.0);

	// `PerformMovement` `0x1026c120` and `PostRun` `0x1026c7c0` — the delegate and the ordered pair.
	Guard->PerformMovement(0.25f, 3);
	TestEqual(TEXT("PerformMovement forwarded its interval"),
		Guard->MotorSeams.PerformMovementInterval, 0.25f);
	TestEqual(TEXT("once"), Guard->MotorSeams.PerformMovement, 1);
	Guard->PostRun();
	TestEqual(TEXT("PostRun ran its pair once"), Guard->MotorSeams.PostRunWeaponUpdates, 1);
	TestEqual(TEXT("with the interval the animating seam gave it"),
		Guard->MotorSeams.PostRunInterval, 0.f);
	return true;
}

// --- `OnObstructingDoor`'s base branch and the cine no-op -----------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorDoorTest,
	"Elysium.Substrate.NpcKernelMotor.ObstructingDoor", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorDoorTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_door"), 4307);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	using EResult = FElysiumNpc::EObstructingDoorResult;
	EResult Result = EResult::Ok;

	// `0x1027dc80` arm 1: the move goal's own max distance is already shorter than the clearance.
	float MaxDistance = 5.0f;
	TestFalse(TEXT("a nearer goal is not obstructed"),
		Guard->OnObstructingDoorBase(MaxDistance, 1, 10.0f, Result));
	TestEqual(TEXT("and the goal is untouched"), MaxDistance, 5.0f);

	// Arm 2: only door states 1 and 3 obstruct.
	MaxDistance = 100.0f;
	TestFalse(TEXT("state 0 does not obstruct"),
		Guard->OnObstructingDoorBase(MaxDistance, 0, 10.0f, Result));
	TestFalse(TEXT("state 2 does not either"),
		Guard->OnObstructingDoorBase(MaxDistance, 2, 10.0f, Result));
	TestEqual(TEXT("and neither wrote the goal"), MaxDistance, 100.0f);

	// Arm 3: a clearance under `_DAT_104493d0` = 0.1 answers -1 and leaves the goal alone.
	Result = EResult::Ok;
	TestTrue(TEXT("state 1 with no room obstructs"),
		Guard->OnObstructingDoorBase(MaxDistance, 1, 0.05f, Result));
	TestEqual(TEXT("with the illegal result"), static_cast<int32>(Result), -1);
	TestEqual(TEXT("and the goal untouched"), MaxDistance, 100.0f);

	// Arm 4: the goal is shortened to the clearance and the result is 0.
	Result = EResult::Illegal;
	TestTrue(TEXT("state 3 with room obstructs"),
		Guard->OnObstructingDoorBase(MaxDistance, 3, 12.5f, Result));
	TestEqual(TEXT("with the ok result"), static_cast<int32>(Result), 0);
	TestEqual(TEXT("and the goal shortened to the clearance"), MaxDistance, 12.5f);
	// Exactly 0.1 is NOT under the threshold.
	MaxDistance = 100.0f;
	Result = EResult::Illegal;
	TestTrue(TEXT("exactly 0.1 of clearance takes the shorten arm"),
		Guard->OnObstructingDoorBase(MaxDistance, 1, 0.1f, Result));
	TestEqual(TEXT("with the ok result"), static_cast<int32>(Result), 0);

	// `CCineNPC::Blocked` `0x101a7580` — the three cine classes answer slot 178 with nothing. A
	// spawnable `npc_*` is none of them.
	TestFalse(TEXT("an ordinary NPC is not a cine actor"), Guard->BlockedIsNoOp());

	// `0x1039aaf0` / `0x1039ab10` — `m_bBlockedByFriend`'s setter and getter.
	TestFalse(TEXT("blocked-by-friend starts clear"), Guard->BlockedByFriend());
	Guard->SetBlockedByFriend(true);
	TestTrue(TEXT("and takes what it is given"), Guard->BlockedByFriend());
	Guard->SetBlockedByFriend(false);
	TestFalse(TEXT("and back"), Guard->BlockedByFriend());
	return true;
}

// --- The jump chain -------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorJumpChainTest,
	"Elysium.Substrate.NpcKernelMotor.JumpChain", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorJumpChainTest::RunTest(const FString&)
{
	// The `SetupJump` species table, both rows by name with the constant each was read from.
	int32 Count = 0;
	const FElysiumNpc::FSetupJumpSpecies* Rows = FElysiumNpc::SetupJumpSpeciesRows(Count);
	TestEqual(TEXT("two SetupJump species"), Count, 2);
	const FElysiumNpc::FSetupJumpSpecies* Asian =
		FElysiumNpc::SetupJumpSpeciesOf(TEXT("CNPC_VAsianVampire"));
	const FElysiumNpc::FSetupJumpSpecies* Sheriff =
		FElysiumNpc::SetupJumpSpeciesOf(TEXT("CNPC_VSheriffMan"));
	TestNotNull(TEXT("the AsianVampire row"), Asian);
	TestNotNull(TEXT("the SheriffMan row"), Sheriff);
	if (Asian != nullptr)
	{
		TestEqual(TEXT("0x10361a70 rises 100"), Asian->Rise, 100.0f);
		TestEqual(TEXT("from _DAT_104a9310"), FString(Asian->RiseConstant),
			FString(TEXT("_DAT_104a9310")));
		TestEqual(TEXT("at 0x10361a70"), FString(Asian->Body), FString(TEXT("0x10361a70")));
	}
	if (Sheriff != nullptr)
	{
		TestEqual(TEXT("0x103b1300 rises 400"), Sheriff->Rise, 400.0f);
		TestEqual(TEXT("from _DAT_104c614c"), FString(Sheriff->RiseConstant),
			FString(TEXT("_DAT_104c614c")));
	}
	TestNull(TEXT("nobody else has a row"),
		FElysiumNpc::SetupJumpSpeciesOf(TEXT("CNPC_VChangBros")));
	TestNotNull(TEXT("rows"), Rows);

	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_jump"), 4308);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumPlayer* Player = Fixture.Player();
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `SetupJump` and `SetupSuperJump` both take a float gate and do NOTHING at 0.0.
	const int32 CommitsBefore = Guard->MotorSeams.SetupJumpCommits;
	Guard->SetupJump(0.0f);
	Guard->SetupSuperJump(0.0f);
	TestEqual(TEXT("neither setup ran at a zero gate"), Guard->MotorSeams.SetupJumpCommits,
		CommitsBefore);
	// With the gate open they still refuse: the hint store carries no origins, and retail would
	// dereference a null `m_pHintNode` here rather than check it.
	Guard->SetupJump(1.0f);
	Guard->SetupSuperJump(1.0f);
	TestEqual(TEXT("and neither commits while the hint-origin seam refuses"),
		Guard->MotorSeams.SetupJumpCommits, CommitsBefore);
	TestEqual(TEXT("so the three jump words are untouched"), Guard->JumpHeight, 0.f);
	FVector HintOrigin(1.0, 2.0, 3.0);
	TestFalse(TEXT("the hint-origin seam refuses"),
		Guard->NavHintNodeOrigin(0, HintOrigin));

	// `CNPC_VAsianVampire::GetJumpSchedule` `0x10362430`: schedule 0x15b when the closest player is
	// more than `_DAT_104a9314` = 40 SOURCE units BELOW this NPC, else 0x15a.
	Guard->Origin = PortUnits(0.0, 0.0, 100.0);
	Player->Origin = PortUnits(0.0, 0.0, 100.0);
	Guard->Senses.Memory.ClosestPlayer = Player->Handle;
	TestEqual(TEXT("level with the player is the across schedule"), Guard->GetJumpSchedule(),
		0x15a);
	Player->Origin = PortUnits(0.0, 0.0, 59.0);   // 41 units below
	TestEqual(TEXT("41 units below is the down schedule"), Guard->GetJumpSchedule(), 0x15b);
	Player->Origin = PortUnits(0.0, 0.0, 60.0);   // exactly 40 below — NOT strictly more
	TestEqual(TEXT("exactly 40 below is not"), Guard->GetJumpSchedule(), 0x15a);
	Guard->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	TestEqual(TEXT("and with no closest player it is the across schedule"),
		Guard->GetJumpSchedule(), 0x15a);

	// `IsPosNearStoredJumpPositions` `0x103618a0` — a 2-D distance under `_DAT_104a9308` = 30, over
	// the two-entry ring, and the Z term is ignored.
	Guard->LastJumpPosition[0] = FVector(0.0, 0.0, 0.0);
	Guard->LastJumpPosition[1] = FVector(500.0, 0.0, 0.0);
	TestTrue(TEXT("29.9 units away in 2-D is near"),
		Guard->IsPosNearStoredJumpPositions(FVector(29.9, 0.0, 0.0)));
	TestFalse(TEXT("30 exactly is not — the compare is strict"),
		Guard->IsPosNearStoredJumpPositions(FVector(30.0, 0.0, 0.0)));
	TestTrue(TEXT("the second ring entry counts too"),
		Guard->IsPosNearStoredJumpPositions(FVector(500.0, 10.0, 0.0)));
	TestTrue(TEXT("and Z is ignored entirely"),
		Guard->IsPosNearStoredJumpPositions(FVector(0.0, 0.0, 9000.0)));

	// The ring write pairs with it, and refuses while the hint store carries no origins.
	Guard->LastJumpPositionIdx = 0;
	Guard->AddHintToStoredJumpPositions(3);
	TestEqual(TEXT("the ring index did not move — the hint-origin seam refused"),
		Guard->LastJumpPositionIdx, 0);

	// `SelectJumpbaseNode` `0x10361730` — the global hint list is a SEAM answering empty, so the
	// nearest type-18000 node is nobody.
	TestEqual(TEXT("no jumpbase node is found"), Guard->SelectJumpbaseNode(), INDEX_NONE);
	TArray<int32> Hints;
	TestFalse(TEXT("because the hint list refuses"), Guard->NavAllHintNodes(Hints));
	TestEqual(TEXT("and is empty"), Hints.Num(), 0);

	// `StationaryForTooLong` `0x10362670` and `UpdateMovedTimeStamp` `0x10362540`.
	const double Now = Fixture.World.NowSeconds();
	Guard->MovedTimeStamp = Now;
	TestFalse(TEXT("just moved is not stationary"), Guard->StationaryForTooLong());
	Guard->MovedTimeStamp = Now - 3.0;
	TestTrue(TEXT("exactly 3 seconds IS — the compare is inclusive"),
		Guard->StationaryForTooLong());
	Guard->MovedTimeStamp = Now - 2.99;
	TestFalse(TEXT("2.99 is not"), Guard->StationaryForTooLong());

	Guard->Origin = PortUnits(0.0, 0.0, 0.0);
	Guard->MovedPosition = FVector(0.0, 0.0, 0.0);
	Guard->MovedTimeStamp = -5.0;
	Guard->UpdateMovedTimeStamp();
	TestEqual(TEXT("a body that has not moved does not re-stamp"), Guard->MovedTimeStamp, -5.0);
	Guard->Origin = PortUnits(40.0, 0.0, 0.0);   // exactly _DAT_104a931c = 40, which is NOT past it
	Guard->UpdateMovedTimeStamp();
	TestEqual(TEXT("exactly 40 units of travel does not either — the compare is strict"),
		Guard->MovedTimeStamp, -5.0);
	Guard->Origin = PortUnits(41.0, 0.0, 0.0);   // past 40
	Guard->UpdateMovedTimeStamp();
	TestTrue(TEXT("41 units of travel re-stamps"),
		FMath::IsNearlyEqual(Guard->MovedTimeStamp, Fixture.World.NowSeconds(), 1e-6));
	TestTrue(TEXT("and refreshes the remembered position"),
		Guard->MovedPosition.Equals(FVector(41.0, 0.0, 0.0), 1e-3));
	return true;
}

// --- `CNPC_VSabbatLeader` and `CNPC_VChangBros` ---------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorSpeciesProbesTest,
	"Elysium.Substrate.NpcKernelMotor.SpeciesProbes", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorSpeciesProbesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_species"), 4309);
	Builder.AddNpc(TEXT("leader"), FVector::ZeroVector, TEXT("npc_VSabbatLeader"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Leader = Fixture.Npc(TEXT("leader"));
	FElysiumPlayer* Player = Fixture.Player();
	if (!TestNotNull(TEXT("leader"), Leader) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Leader });
	TestTrue(TEXT("the leader resolves to CNPC_VSabbatLeader"),
		Leader->IsRetailClass(TEXT("CNPC_VSabbatLeader")));

	Leader->Origin = PortUnits(0.0, 0.0, 0.0);
	Player->Origin = PortUnits(20.0, 0.0, 0.0);
	Leader->Senses.Memory.ClosestPlayer = Player->Handle;

	// `CheckStuck` `0x103ab580` — both boxes come from `m_Collision`'s OBB slots, which is a SEAM,
	// so the body refuses before it tests anything and the leader is not moved.
	const FVector Before = Leader->Origin;
	Leader->CheckStuck();
	TestEqual(TEXT("CheckStuck moves nobody while the collision-extent seam refuses"),
		Leader->Origin, Before);
	FVector Mins(1.0, 1.0, 1.0);
	FVector Maxs(2.0, 2.0, 2.0);
	TestFalse(TEXT("the extent seam refuses"),
		FElysiumNpc::RetailCollisionExtents(*Leader, Mins, Maxs));
	TestEqual(TEXT("and zeroes the mins"), Mins, FVector::ZeroVector);

	// `PlayerInNoJumpZone` `0x103a9e70` — the hint list is empty, so nobody is ever inside one.
	TestFalse(TEXT("no player is in a no-jump zone"), Leader->PlayerInNoJumpZone());

	// `SetJumpVelocityTowardPlayer` `0x103aad40` — the arc solver is a SEAM, and retail's own
	// out-parameter is the body's current velocity, which it then assigns back unchanged.
	Leader->Velocity = FVector(7.0, 8.0, 9.0);
	const int32 SolvesBefore = Leader->MotorSeams.JumpArcSolves;
	Leader->SetJumpVelocityTowardPlayer();
	TestEqual(TEXT("the arc solver was asked"), Leader->MotorSeams.JumpArcSolves,
		SolvesBefore + 1);
	TestEqual(TEXT("and the velocity is unchanged, which is retail's own answer for an untouched "
		"out-parameter"), Leader->Velocity, FVector(7.0, 8.0, 9.0));
	// With no closest player the body does not even reach the solver.
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	const int32 SolvesAfter = Leader->MotorSeams.JumpArcSolves;
	Leader->SetJumpVelocityTowardPlayer();
	TestEqual(TEXT("no closest player, no solve"), Leader->MotorSeams.JumpArcSolves, SolvesAfter);

	// `CNPC_VChangBros::CheckForJumpAttack` `0x1036c8d0`. The leader is not a ChangBros, so the
	// behaviour is driven directly: `m_ChangType != 0` refuses outright, and the sector seam answers
	// 4 — the value that CLOSES the gate.
	Leader->ChangType = 1;
	TestFalse(TEXT("a non-zero ChangType refuses"), Leader->CheckForJumpAttack());
	Leader->ChangType = 0;
	Leader->Senses.Memory.ClosestPlayer = Player->Handle;
	TestEqual(TEXT("the sector seam answers 4"), Leader->ChangBrosSector(FVector::ZeroVector), 4);
	TestFalse(TEXT("so the jump attack is refused rather than allowed on a guess"),
		Leader->CheckForJumpAttack());
	Leader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("and with no closest player it refuses too"), Leader->CheckForJumpAttack());
	return true;
}

// --- `CNPC_VTzimisce`'s slot 410 branch -----------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorTranslateGoalTest,
	"Elysium.Substrate.NpcKernelMotor.TranslateNavGoal", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorTranslateGoalTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_goal"), 4310);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("enemy"), FVector(300.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Enemy = Fixture.Npc(TEXT("enemy"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("enemy"), Enemy))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Enemy });

	const FVector Goal(11.0, 22.0, 33.0);
	FVector Out = FVector::ZeroVector;

	// `CNPC_VTzimisce::vfunc410` `0x103bf580`, path mode 0 — neither 1 nor 2, so the caller's goal
	// comes back untouched.
	Guard->PathMode = 0;
	TestFalse(TEXT("path mode 0 does not translate"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("and returns the caller's goal"), Out, Goal);

	// Path mode 1 with an enemy: the enemy's position replaces the goal.
	Guard->PathMode = 1;
	Enemy->Origin = PortUnits(50.0, 60.0, 70.0);
	Guard->Senses.Memory.Enemy = Enemy->Handle;
	TestTrue(TEXT("path mode 1 with an enemy translates"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestTrue(TEXT("to the enemy's position"), Out.Equals(FVector(50.0, 60.0, 70.0), 1e-3));

	// Path mode 1 with NO enemy falls THROUGH to the pickup arm rather than returning the goal —
	// retail's own control flow, and the reason the pickup test is not nested under mode 2 alone.
	Guard->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	Guard->PickupTarget = Enemy->Handle;
	Guard->PickupTargetPos = FVector(1.0, 2.0, 3.0);
	TestTrue(TEXT("path mode 1 with no enemy falls through to the pickup arm"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("and answers the pickup position"), Out, FVector(1.0, 2.0, 3.0));

	// Path mode 2 goes straight to the pickup arm, and refuses when the handle is dead.
	Guard->PathMode = 2;
	TestTrue(TEXT("path mode 2 answers the pickup position"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("the pickup position"), Out, FVector(1.0, 2.0, 3.0));
	Guard->PickupTarget = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("with no pickup target it does not translate"),
		Guard->TranslateNavGoalPositionTzimisce(Goal, Out));
	TestEqual(TEXT("and the caller's goal comes back"), Out, Goal);
	return true;
}

// --- `IElysiumNpcMotor::MinStoppingDistanceUnits` -------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorStoppingDistanceTest,
	"Elysium.Substrate.NpcKernelMotor.MinStoppingDistance", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorStoppingDistanceTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_stop"), 4311);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `CAI_Motor#16` `0x102e1300`: `max(0.5 * v^2 / decel, 10.0)` when the deceleration source
	// answers a positive number, and `_DAT_1044e664` = 10.0 when it does not. With no motor at all
	// the floor is the answer, and that floor is retail's own, not a port constant.
	TestEqual(TEXT("no motor answers retail's floor, 10.0"),
		Guard->MotorMinStoppingDistanceUnits(), 10.0f);
	return true;
}

// --- Slot 153's other body: `CAISound::FUN_10026e70` ----------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMotorBaseIsMovingTest,
	"Elysium.Substrate.NpcKernelMotor.BaseEntityIsMoving", GElysiumNpcKernelMotorFlags)
bool FElysiumNpcKernelMotorBaseIsMovingTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("npc_kernel_motor_ismoving"), 4312);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// `0x10026e70`, all three components against `vec3_origin` (`DAT_1070d1b0..b8`).
	Guard->Velocity = FVector::ZeroVector;
	TestFalse(TEXT("0x10026e70: a zero velocity is not moving"),
		FElysiumNpc::BaseEntityIsMoving(*Guard));

	// One component differing is enough: retail's test is an AND of three equalities, so any
	// inequality falls through to `return 1`.
	Guard->Velocity = FVector(0.0, 0.0, 1.0);
	TestTrue(TEXT("0x10026e70: Z alone is moving"), FElysiumNpc::BaseEntityIsMoving(*Guard));
	Guard->Velocity = FVector(0.0, 1.0, 0.0);
	TestTrue(TEXT("0x10026e70: Y alone is moving"), FElysiumNpc::BaseEntityIsMoving(*Guard));
	Guard->Velocity = FVector(1.0, 0.0, 0.0);
	TestTrue(TEXT("0x10026e70: X alone is moving"), FElysiumNpc::BaseEntityIsMoving(*Guard));

	// EXACT equality, not a tolerance — a velocity retail calls moving must not be rounded away.
	Guard->Velocity = FVector(0.0, 0.0, 1e-8);
	TestTrue(TEXT("0x10026e70: the comparison is exact, so a tiny velocity is still moving"),
		FElysiumNpc::BaseEntityIsMoving(*Guard));

	// This body is NOT the NPC's slot-153 answer: every class on the NPC line carries `0x10280300`,
	// which is the navigator forward, and it answers independently of the velocity word above.
	Guard->Velocity = FVector(1.0, 0.0, 0.0);
	TestFalse(TEXT("slot 153 on an NPC is 0x10280300, the navigator forward, not this body"),
		Guard->IsMoving());
	Guard->Velocity = FVector::ZeroVector;
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
