#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

static constexpr EAutomationTestFlags GTranslate19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FTranslate19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;

		FTranslate19Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("translate19_kernel"), 440);
					Builder.AddNpc(TEXT("guard"));
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			FElysiumNpcWorldFixture::Quiet({ Guard });
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTranslate19TroikaTest,
	"Elysium.Substrate.NpcKernelTranslate19.Troika", GTranslate19Flags)
bool FElysiumNpcKernelTranslate19TroikaTest::RunTest(const FString&)
{
	FTranslate19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.SetRetailClassForTests(TEXT("CAI_BaseNPCTroika"));
	// `102b1335`: `(-(uint)((m_bfAINPCFlags2 & 0x80000) != 0x80000) & 0xffffff39) + 0x132` — the
	// flag CLEAR arm, exactly `0x6b`.
	TestEqual(TEXT("102b12f0 1 -> 0x6b"), N.TranslateScheduleRetail(1), 0x6b);
	TestEqual(TEXT("102b12f0 0x6b -> 0x6b"), N.TranslateScheduleRetail(0x6b), 0x6b);
	// The whole table, every row, in `102b12f0`'s own numbering.
	struct FRow { int32 From; int32 To; };
	static const FRow Rows[] = {
		{ 2, 0x46 }, { 3, 0x47 }, { 6, 0x4a }, { 0xf, 0xb1 }, { 0x10, 0xb7 }, { 0x15, 0xb8 },
		{ 0x21, 0xed }, { 0x22, 0xee }, { 0x25, 0xc1 }, { 0x28, 0xc2 }, { 0x2f, 0xf2 },
		{ 0x30, 0xf4 }, { 0x31, 0xf6 }, { 0x32, 0xf8 }, { 0x33, 0xf9 },
	};
	for (const FRow& Row : Rows)
	{
		TestEqual(*FString::Printf(TEXT("102b12f0 0x%x -> 0x%x"), Row.From, Row.To),
			N.TranslateScheduleRetail(Row.From), Row.To);
	}
	// `102b1408`: the `0x77` arm needs BOTH a hint node and hint type `0x2774`; with neither it
	// falls to the base, which is the identity.
	TestEqual(TEXT("102b12f0 0x77 with no hint node -> identity"),
		N.TranslateScheduleRetail(0x77), 0x77);
	// `102b13d1` / `102b13ef`: slot 293 `GetFollowerBoss()` answers null here, so both arms miss.
	TestEqual(TEXT("102b12f0 0x94 with no follower boss -> identity"),
		N.TranslateScheduleRetail(0x94), 0x94);
	TestEqual(TEXT("102b12f0 0x96 with no follower boss -> identity"),
		N.TranslateScheduleRetail(0x96), 0x96);
	// `102cc122 CMP EDX,0x5 / JA 0x102cc17e` with EAX still `param_1`: the base is the identity
	// for every non-`0x2e` id, not "nothing at all".
	TestEqual(TEXT("102cc080 identity for an unmapped id"), N.TranslateScheduleRetail(0x40), 0x40);
	N.NpcFlags.Set(EElysiumNpcFlag2::D_MILDLY_CRAZY);
	// `102b1335`: the flag SET arm, the other of exactly two values.
	TestEqual(TEXT("102b12f0 crazy 1 -> 0x132"), N.TranslateScheduleRetail(1), 0x132);
	N.NpcFlags.SetFrenziedWord(0x100);
	// `102b120c MOV EAX,0xc9` — the frenzied pre-table runs BEFORE the Troika table and returns.
	TestEqual(TEXT("102b11c0 frenzied 0xc7 -> 0xc9"), N.TranslateScheduleRetail(0xc7), 0xc9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTranslate19BaseCineTest,
	"Elysium.Substrate.NpcKernelTranslate19.BaseCine", GTranslate19Flags)
bool FElysiumNpcKernelTranslate19BaseCineTest::RunTest(const FString&)
{
	FTranslate19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.SetRetailClassForTests(TEXT("CAI_BaseNPCTroika"));
	// `102cc091`: no live cine (`m_hCine` fails its serial check) — `DevWarning`, `CineCleanup`
	// (`0x1027d170`), then slot 440 with the literal 1, which the Troika table maps to `0x6b`.
	const int32 CleanupsBefore = N.TranslateCineCleanupCalls;
	TestEqual(TEXT("102cc0e9 0x2e with no live cine re-dispatches slot 440 with 1"),
		N.TranslateScheduleRetail(0x2e), 0x6b);
	TestEqual(TEXT("...and runs CineCleanup exactly once"),
		N.TranslateCineCleanupCalls, CleanupsBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTranslate19FrenziedTableTest,
	"Elysium.Substrate.NpcKernelTranslate19.FrenziedTable", GTranslate19Flags)
bool FElysiumNpcKernelTranslate19FrenziedTableTest::RunTest(const FString&)
{
	FTranslate19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	TestEqual(TEXT("102b11c0 0xc7 -> 0xc9"), N.FrenziedTranslateSchedule(0xc7), 0xc9);
	for (const int32 From : { 0xca, 0xcb, 0xd1, 0xd2 })
	{
		TestEqual(*FString::Printf(TEXT("102b11c0 0x%x -> 0xcc"), From),
			N.FrenziedTranslateSchedule(From), 0xcc);
	}
	TestEqual(TEXT("102b11c0 0xef -> 0xf0"), N.FrenziedTranslateSchedule(0xef), 0xf0);
	// The default is 0, which is what lets `0x102b12f0`'s frenzied gate fall through to the table.
	TestEqual(TEXT("102b11c0 default -> 0"), N.FrenziedTranslateSchedule(0x40), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTranslate19Frenzied87Test,
	"Elysium.Substrate.NpcKernelTranslate19.Frenzied87", GTranslate19Flags)
bool FElysiumNpcKernelTranslate19Frenzied87Test::RunTest(const FString&)
{
	FTranslate19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// `102b11f4`: the shared `0x87`/`0x88` tail tests the COMPLEMENT of `MADE_HUNT_PATH`, so a
	// CLEAR bit takes the enemy arms and a SET bit answers `0x7e`.
	N.NpcFlags.Clear(EElysiumNpcFlag::MADE_HUNT_PATH);
	TestEqual(TEXT("102b11c0 0x87 bit-clear no enemy -> 0x7d"),
		N.FrenziedTranslateSchedule(0x87), 0x7d);
	TestEqual(TEXT("102b11c0 0x88 bit-clear no enemy -> 0x7d"),
		N.FrenziedTranslateSchedule(0x88), 0x7d);
	N.NpcFlags.Set(EElysiumNpcFlag::MADE_HUNT_PATH);
	TestEqual(TEXT("102b11c0 0x87 bit-set -> 0x7e"),
		N.FrenziedTranslateSchedule(0x87), 0x7e);
	TestEqual(TEXT("102b11c0 0x88 bit-set -> 0x7e"),
		N.FrenziedTranslateSchedule(0x88), 0x7e);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTranslate19WerewolfTest,
	"Elysium.Substrate.NpcKernelTranslate19.Werewolf", GTranslate19Flags)
bool FElysiumNpcKernelTranslate19WerewolfTest::RunTest(const FString&)
{
	FTranslate19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	N.SetRetailClassForTests(TEXT("CNPC_VWerewolf"));
	// `103d5e00`: all three of the werewolf's rows. `0x43 SCHED_FAIL` is one of them, so a
	// werewolf never runs the base FAIL program.
	TestEqual(TEXT("103d5e00 0x43 FAIL -> 0x15c"), N.TranslateScheduleRetail(0x43), 0x15c);
	TestEqual(TEXT("103d5e00 0xb7 -> 0x157"), N.TranslateScheduleRetail(0xb7), 0x157);
	TestEqual(TEXT("103d5e00 0xf0 -> 0xb1"), N.TranslateScheduleRetail(0xf0), 0xb1);
	// Anything else falls to Troika: `0x21` is one of ITS rows.
	TestEqual(TEXT("103d5e00 0x21 falls to Troika -> 0xed"), N.TranslateScheduleRetail(0x21), 0xed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelTranslate19SpeciesTest,
	"Elysium.Substrate.NpcKernelTranslate19.Species", GTranslate19Flags)
bool FElysiumNpcKernelTranslate19SpeciesTest::RunTest(const FString&)
{
	FTranslate19Fixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	struct FCase { const TCHAR* Class; const TCHAR* Address; int32 From; int32 To; };
	static const FCase Cases[] = {
		// `10372150`: the cop's `0x6b` runs `& 0x29`, so a non-crazy cop answers `0x15b`.
		{ TEXT("CNPC_VCop"),              TEXT("0x10372150"), 0x6b,  0x15b },
		{ TEXT("CNPC_VCop"),              TEXT("0x10372150"), 0x89,  0x15e },
		{ TEXT("CNPC_VCop"),              TEXT("0x10372150"), 0x94,  0x15c },
		{ TEXT("CNPC_VCop"),              TEXT("0x10372150"), 0x96,  0x15d },
		{ TEXT("CNPC_VCop"),              TEXT("0x10372150"), 0x103, 0x15a },
		{ TEXT("CNPC_VCop"),              TEXT("0x10372150"), 0xaa,  0x160 },
		{ TEXT("CNPC_VCop"),              TEXT("0x10372150"), 0xb7,  0x16d },
		// `1037d240`: Guard1's crazy offset is `0x28` where Cop and Hunter use `0x29` — a
		// one-character difference with two different answers.
		{ TEXT("CNPC_VGuard1"),           TEXT("0x1037d240"), 0x6b,  0x15a },
		{ TEXT("CNPC_VGuard1"),           TEXT("0x1037d240"), 0x103, 0x159 },
		{ TEXT("CNPC_VHunter"),           TEXT("0x10388a40"), 0x6b,  0x15b },
		// `103c1720` / `103c3560` / `1039e2d0`: `0xc7 -> 0xc8` SHADOWS the frenzied `0xc7 -> 0xc9`
		// because the species body is the entry point and answers before Troika is reached.
		{ TEXT("CNPC_VTzimisceHeadClaw"), TEXT("0x103c1720"), 0xc7,  0xc8 },
		{ TEXT("CNPC_VTzimisceRunner"),   TEXT("0x103c3560"), 0xc7,  0xc8 },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039e2d0"), 0xc7,  0xc8 },
		// `103bd390`: two of the Tzimisce's fifteen rows.
		{ TEXT("CNPC_VTzimisce"),         TEXT("0x103bd390"), 1,     0x157 },
		{ TEXT("CNPC_VTzimisce"),         TEXT("0x103bd390"), 0x33,  0x18c },
		// `103b0320`: CNPC_VSheriffMan has NO translation row — straight to Troika.
		{ TEXT("CNPC_VSheriffMan"),       TEXT("0x103b0320"), 0xf,   0xb1 },
		// `103df580`: the zombie's three rows.
		{ TEXT("CNPC_VZombie"),           TEXT("0x103df580"), 0x5b,  0x165 },
		{ TEXT("CNPC_VZombie"),           TEXT("0x103df580"), 0x156, 0x15e },
		// `1037ffa0`: the hengeyokai's own rows.
		{ TEXT("CNPC_VHengeyokai"),       TEXT("0x1037ffa0"), 0x5b,  0x15a },
	};
	for (const FCase& Case : Cases)
	{
		N.SetRetailClassForTests(Case.Class);
		TestEqual(*FString::Printf(TEXT("%s %s 0x%x -> 0x%x"),
				Case.Class, Case.Address, Case.From, Case.To),
			N.TranslateScheduleRetail(Case.From), Case.To);
	}

	// `1037ffa0` + `0x10383130`: merely TRANSLATING any id but `0x16e` with `m_nSkin == 1` thaws a
	// hengeyokai. The side effect is the arm, not a consequence of the answer.
	N.SetRetailClassForTests(TEXT("CNPC_VHengeyokai"));
	N.HengeyokaiSkin = 1;
	const int32 ThawsBefore = N.HengeyokaiThawCalls;
	N.TranslateScheduleRetail(0x5b);
	TestEqual(TEXT("1037ffa0 translating any id but 0x16e thaws a skin-1 hengeyokai"),
		N.HengeyokaiThawCalls, ThawsBefore + 1);
	N.TranslateScheduleRetail(0x16e);
	TestEqual(TEXT("...and 0x16e does not"), N.HengeyokaiThawCalls, ThawsBefore + 1);
	return true;
}

#endif
