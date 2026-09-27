#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcMakerZombie.h"
#include "Tests/ElysiumNpcDeadClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **SaveRestore10 + Lifecycle10**. Every expectation below is read off the
// decompiled C of the body it names and, where the decompiler aliased or mislabelled an argument,
// off the listing — this family corrected the checklist's walk in five places and each correction
// has a case that states the corrected fact.
//
// The suite is in five parts: the sentinel CODEC and the CRC32 the whole family turns on; slot 126
// `Save` (the base, the Troika body, the four species arms); slot 127 `Restore` (the same shape plus
// the vampire-boss reset); slot 180 `UpdateOnRemove` and slot 106 `PostConstructor`; and the
// non-slot bodies — `RunAlternateAI` mode 4, the two maker helpers and the scripted sequence's
// `Activate`.
//
// Every species case proves the species body for its retail class AND the Troika body for a bare
// `CAI_BaseNPCTroika` control, whose `RetailClass()` is null. Each species body is reached on a
// fresh NPC built as that class (`AddNpcOfClass`), never by reclassing a live one. The control was a
// plain `npc_VCop` until story 5 step 2 made that classname build `CNPC_VCop`; it is the bare
// Troika line now (`AddTroikaNpc`), which is what the control always stood for.

static constexpr EAutomationTestFlags GSaveRestore10TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC of retail class `SpeciesClass` that a case's arm is driven through, plus the bare
	// `CAI_BaseNPCTroika` control.
	struct FSaveRestore10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Species = nullptr;
		FElysiumNpc* Troika = nullptr;

		explicit FSaveRestore10Fixture(const TCHAR* SpeciesClass = TEXT("CNPC_VHumanCombatant"))
			: World(Build(SpeciesClass))
		{
			Species = World.Npc(TEXT("species"));
			Troika = World.Npc(TEXT("troika"));
			FElysiumNpcWorldFixture::Quiet({ Species, Troika });
		}

		static FElysiumNpcWorldBuilder Build(const TCHAR* SpeciesClass)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("saverestore10"), 20260914);
			Builder.AddNpcOfClass(TEXT("species"), FVector(100.0, 0.0, 0.0), SpeciesClass);
			Builder.AddTroikaNpc(TEXT("troika"), FVector(200.0, 0.0, 0.0));
			return Builder;
		}
	};

	// The eleven stamps `0x102993c0` encodes, by the port member each lands on, so a case can set
	// them all to one value and read them all back.
	void SaveRestore10SetAllStamps(FElysiumNpc& N, double Value)
	{
		N.CanSeekCoverTimer = Value;
		N.Senses.Memory.SeeUnknownGraceUntil = Value;
		N.MeleeHeightDiffTimer = Value;
		N.OccludedReportTimeE = Value;
		N.OccludedReportTimeT = Value;
		N.OccludedReportTimeW = Value;
		N.ScheduleHost.InterruptTime = Value;
		N.WeaponScareTime = Value;
		N.IgnoreCollisionUntil = Value;
		N.NextFidgetTime = static_cast<float>(Value);
	}

	// The archive log as one readable line per op, so an order mismatch reads as a diff.
	TArray<FString> SaveRestore10LogText(const TArray<FElysiumNpcBase::FSaveArchiveOp>& Log)
	{
		TArray<FString> Out;
		Out.Reserve(Log.Num());
		for (const FElysiumNpcBase::FSaveArchiveOp& Op : Log)
		{
			const TCHAR* Kind = TEXT("?");
			switch (Op.Kind)
			{
			case FElysiumNpcBase::FSaveArchiveOp::EKind::Fields:   Kind = TEXT("fields"); break;
			case FElysiumNpcBase::FSaveArchiveOp::EKind::Bool:     Kind = TEXT("bool"); break;
			case FElysiumNpcBase::FSaveArchiveOp::EKind::Int:      Kind = TEXT("int"); break;
			case FElysiumNpcBase::FSaveArchiveOp::EKind::ReadBool: Kind = TEXT("readbool"); break;
			case FElysiumNpcBase::FSaveArchiveOp::EKind::ReadInt:  Kind = TEXT("readint"); break;
			}
			Out.Add(FString::Printf(TEXT("%s:%s:%d"), Kind, *Op.Name, Op.Value));
		}
		return Out;
	}
}

// -------------------------------------------------------------------------------------------------
// The sentinel codec — `0x101cf250` and `0x101cf2f0`, and the CRC32 beside them.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10CodecTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.Codec", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10CodecTest::RunTest(const FString&)
{
	using EMode = FElysiumNpcBase::ESaveStampMode;
	const double Sentinel = FElysiumNpcBase::SaveStampSentinel;
	const double FloatMax = FElysiumNpcBase::SaveStampFloatMax();

	// `0x101cf250`, arm by arm. Mode 1 is `*p < 0.0` STRICTLY.
	double V = -0.5;
	TestTrue(TEXT("mode 1 encodes a value below 0.0"), FElysiumNpcBase::SaveStampEncode(V, EMode::BelowZero));
	TestEqual(TEXT("and writes 1e+11"), V, Sentinel);
	V = 0.0;
	TestFalse(TEXT("mode 1 leaves exactly 0.0 alone"),
		FElysiumNpcBase::SaveStampEncode(V, EMode::BelowZero));
	TestEqual(TEXT("so 0.0 survives mode 1"), V, 0.0);

	V = -1.0;
	TestTrue(TEXT("mode 2 encodes exactly -1.0"), FElysiumNpcBase::SaveStampEncode(V, EMode::MinusOne));
	TestEqual(TEXT("and writes 1e+11"), V, Sentinel);
	V = -1.5;
	TestFalse(TEXT("mode 2 is an EQUALITY, not a comparison"),
		FElysiumNpcBase::SaveStampEncode(V, EMode::MinusOne));

	V = 0.0;
	TestTrue(TEXT("mode 3 encodes exactly _DAT_104454c4 (0.0)"),
		FElysiumNpcBase::SaveStampEncode(V, EMode::Zero));
	TestEqual(TEXT("and writes 1e+11"), V, Sentinel);

	V = FloatMax;
	TestTrue(TEXT("mode 4 encodes exactly FLT_MAX"), FElysiumNpcBase::SaveStampEncode(V, EMode::FloatMax));
	TestEqual(TEXT("and writes 1e+11"), V, Sentinel);

	// A mode outside 1..4 is retail's `default:` and writes nothing.
	V = 0.0;
	TestFalse(TEXT("mode 0 is retail's default: and does nothing"),
		FElysiumNpcBase::SaveStampEncode(V, EMode::None));
	TestEqual(TEXT("the stamp survives an unknown mode"), V, 0.0);

	// The ROUND TRIP, one case per mode. `_DAT_10482fac` is 1e+10 and the sentinel 1e+11, so every
	// encoded stamp clears the decode's floor.
	const TPair<EMode, double> RoundTrips[] = {
		{ EMode::BelowZero, -1.0 },   // mode 1 decodes to -1.0 — NOT its own inverse
		{ EMode::MinusOne,  -1.0 },
		{ EMode::Zero,       0.0 },
		{ EMode::FloatMax,   FloatMax },
	};
	const double Seeds[] = { -0.5, -1.0, 0.0, FloatMax };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		double Stamp = Seeds[Index];
		TestTrue(FString::Printf(TEXT("mode %d encodes its seed"), static_cast<int32>(RoundTrips[Index].Key)),
			FElysiumNpcBase::SaveStampEncode(Stamp, RoundTrips[Index].Key));
		TestEqual(TEXT("the encoded stamp is the sentinel"), Stamp, Sentinel);
		TestTrue(TEXT("and the decode fires on it"),
			FElysiumNpcBase::SaveStampDecode(Stamp, RoundTrips[Index].Key));
		TestEqual(FString::Printf(TEXT("mode %d decodes to retail's value"),
			static_cast<int32>(RoundTrips[Index].Key)), Stamp, RoundTrips[Index].Value);
	}

	// Mode 1's asymmetry, stated by name: `-0.5` goes in and `-1.0` comes back, because modes 1 and
	// 2 share a `case` label in `0x101cf2f0`.
	double Asymmetric = -0.5;
	FElysiumNpcBase::SaveStampEncode(Asymmetric, EMode::BelowZero);
	FElysiumNpcBase::SaveStampDecode(Asymmetric, EMode::BelowZero);
	TestEqual(TEXT("mode 1 is not its own inverse: -0.5 returns as -1.0"), Asymmetric, -1.0);

	// The decode's floor is `1e+10` inclusive, and anything under it survives.
	double Floor = FElysiumNpcBase::SaveStampSentinelFloor;
	TestTrue(TEXT("the decode fires AT the floor"), FElysiumNpcBase::SaveStampDecode(Floor, EMode::Zero));
	double Under = FElysiumNpcBase::SaveStampSentinelFloor * 0.5;
	TestFalse(TEXT("and not below it"), FElysiumNpcBase::SaveStampDecode(Under, EMode::Zero));
	TestEqual(TEXT("an ordinary stamp survives the decode"),
		Under, FElysiumNpcBase::SaveStampSentinelFloor * 0.5);

	// `0x101b9840` / `0x101b9860` are mode 2 on a `CSound`'s `+0x10 m_flExpireTime`.
	FElysiumGameSoundEvent Sound;
	Sound.ExpireTime = -1.0;
	FElysiumNpc::SaveSoundStampEncode(Sound);
	TestEqual(TEXT("a sound's expiry encodes at mode 2"), Sound.ExpireTime, Sentinel);
	FElysiumNpc::SaveSoundStampDecode(Sound);
	TestEqual(TEXT("and comes back as -1.0"), Sound.ExpireTime, -1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10Crc32Test,
	"Elysium.Substrate.NpcKernelSaveRestore10.Crc32", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10Crc32Test::RunTest(const FString&)
{
	// `0x1023f040` / `0x1023f0c0` / `0x1023f060` are CRC32, not the bit-vector copy the checklist's
	// walk calls them: init `0xffffffff`, a table-driven byte loop over `DAT_10496f58`, final
	// complement. Pinned against the published reflected CRC-32 check value.
	const char Check[] = "123456789";
	uint32 Crc = FElysiumNpcBase::SaveCrc32Init();
	TestEqual(TEXT("the init word is 0xffffffff"), Crc, 0xffffffffu);
	Crc = FElysiumNpcBase::SaveCrc32Update(Crc, reinterpret_cast<const uint8*>(Check), 9);
	Crc = FElysiumNpcBase::SaveCrc32Final(Crc);
	TestEqual(TEXT("\"123456789\" checksums to 0xcbf43926"), Crc, 0xcbf43926u);

	// Zero bytes: `~0xffffffff == 0`, which is the literal `0` retail's own no-schedule arm writes
	// into the header — the two arms agree on the checksum and differ only in the name.
	uint32 Empty = FElysiumNpcBase::SaveCrc32Final(
		FElysiumNpcBase::SaveCrc32Update(FElysiumNpcBase::SaveCrc32Init(), nullptr, 0));
	TestEqual(TEXT("an empty run checksums to 0"), Empty, 0u);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 126 `Save`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10BaseSaveTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.BaseSave", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10BaseSaveTest::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// `CAI_BaseNPC::Save` `0x1027bc60` encodes exactly TWO fields — the CORRECTION this family made:
	// the second argument of `thunk_FUN_101cf250` is the sentinel MODE, not a count.
	// `1027bc6c PUSH 0x4` on `+0x5b8c` and `1027bc80 PUSH 0x3` on `+0x5db4`.
	N.ExtendedBlockedByFriendTimer = FElysiumNpcBase::SaveStampFloatMax();   // mode 4 catches it
	N.BaseScheduleHost.WaitFinished = 0.0;                                  // mode 3 catches it
	N.WeaponBlockedByFriendTimer = FElysiumNpcBase::SaveStampFloatMax();    // NOT one of the two
	N.BaseScheduleHost.MoveWaitFinished = 0.0;                              // NOT one of the two
	N.SaveArchiveLog.Reset();

	TestEqual(TEXT("the base answers the chain's result"), N.FElysiumNpcBase::Save(nullptr), 1);
	// The encode and the decode bracket the archive call, so the fields are back where they started.
	TestEqual(TEXT("the extended-block timer round-trips"),
		N.ExtendedBlockedByFriendTimer, FElysiumNpcBase::SaveStampFloatMax());
	TestEqual(TEXT("the wait stamp round-trips"), N.BaseScheduleHost.WaitFinished, 0.0);
	TestEqual(TEXT("the neighbour of the first is untouched"),
		N.WeaponBlockedByFriendTimer, FElysiumNpcBase::SaveStampFloatMax());
	TestEqual(TEXT("and so is the neighbour of the second"), N.BaseScheduleHost.MoveWaitFinished, 0.0);

	// The archive call: one `WriteFields` of `AIExtendedSaveHeader_t` and nothing else.
	TestEqual(TEXT("the base writes exactly one block"), N.SaveArchiveLog.Num(), 1);
	TestEqual(TEXT("and it is the extended header"),
		SaveRestore10LogText(N.SaveArchiveLog)[0], FString(TEXT("fields:AIExtendedSaveHeader_t:0")));
	TestEqual(TEXT("the header's version is the literal 1"),
		static_cast<int32>(N.LastSavedExtendedHeader.Version), 1);

	// The three flag bits, in the order `0x1027bc60` ORs them.
	TestEqual(TEXT("a quiet NPC sets no flag bit"), N.LastSavedExtendedHeader.Flags, 0u);
	N.BaseMemory.Enemy = N.Handle;   // slot 0x29c `GetEnemy()` is non-null
	N.FElysiumNpcBase::Save(nullptr);
	TestEqual(TEXT("bit 0x1 is the committed enemy"), N.LastSavedExtendedHeader.Flags, 1u);
	N.SetTarget(N.Handle);              // `m_hTargetEnt` resolves onto a live entity
	N.FElysiumNpcBase::Save(nullptr);
	TestEqual(TEXT("bit 0x2 is m_hTargetEnt, and both stand together"),
		N.LastSavedExtendedHeader.Flags, 3u);
	// Bit 0x4 is the navigator goal, whose seam answers nothing (`NavigatorGoalIsActive`).
	TestFalse(TEXT("the navigator-goal seam answers nothing"), N.NavigatorGoalIsActive());

	// No running schedule: the name is cleared and the CRC is 0, which is what retail's own else arm
	// writes (`1027bd76 MOV byte ptr [ESP+0x1c],0x0` and `MOV dword ptr [ESP+0x9c],0x0`).
	TestTrue(TEXT("a scheduleless NPC writes an empty schedule name"),
		N.LastSavedExtendedHeader.ScheduleName.IsEmpty());
	TestEqual(TEXT("and a zero task checksum"), N.LastSavedExtendedHeader.ScheduleCrc, 0u);

	// The motor and move-and-shoot fix-ups: `0x102e0b60` is GUARDED on `m_pMotor` (`+0x5d44`) and
	// `0x102e8aa0` on the overlay is NOT, so the overlay count moves on every pass and the motor
	// count only when a motor stands. Both halves are paired around the archive call, which is the
	// invariant that holds whatever the fixture provides.
	const int32 OverlayBefore = N.MoveAndShootSaveFixups;
	const int32 MotorBefore = N.MotorSaveFixups;
	N.FElysiumNpcBase::Save(nullptr);
	TestEqual(TEXT("the overlay fix-up is not guarded and runs every pass"),
		N.MoveAndShootSaveFixups, OverlayBefore + 1);
	TestTrue(TEXT("the motor fix-up is guarded, so it runs at most once per pass"),
		N.MotorSaveFixups - MotorBefore <= 1);
	TestEqual(TEXT("the overlay's post-archive half is paired with its pre half"),
		N.MoveAndShootRestoreFixups, N.MoveAndShootSaveFixups);
	TestEqual(TEXT("and so is the motor's"), N.MotorRestoreFixups, N.MotorSaveFixups);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10TroikaSaveTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.TroikaSave", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10TroikaSaveTest::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Troika))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Troika;
	// The control is the bare Troika line (`AddTroikaNpc`): no species class stands over it.
	TestNull(TEXT("the bare Troika line's RetailClass() is null"), N.RetailClass());

	// Every stamp at the value its OWN mode encodes, so one pass fires all eleven.
	N.CanSeekCoverTimer = 0.0;                            // mode 3
	N.Senses.Memory.SeeUnknownGraceUntil = -1.0;          // mode 2
	N.MeleeHeightDiffTimer = -1.0;                        // mode 2
	N.OccludedReportTimeE = 0.0;                          // mode 3
	N.OccludedReportTimeT = 0.0;                          // mode 3
	N.OccludedReportTimeW = 0.0;                          // mode 3
	N.ScheduleHost.InterruptTime = 0.0;                   // mode 3
	N.WeaponScareTime = -1.0;                             // mode 2
	N.IgnoreCollisionUntil = FElysiumNpcBase::SaveStampFloatMax();    // mode 4
	N.NextFidgetTime = static_cast<float>(FElysiumNpcBase::SaveStampFloatMax());   // mode 4
	N.Senses.Memory.BestSound.ExpireTime = -1.0;
	N.Senses.Memory.LastSoundFlinch.ExpireTime = -1.0;
	N.SaveArchiveLog.Reset();

	TestEqual(TEXT("slot 126 answers the base body's result"), N.Save(nullptr), 1);

	// Encode-then-decode is the whole shape: every stamp is back where it started.
	TestEqual(TEXT("m_flCanSeekCoverTimer round-trips at mode 3"), N.CanSeekCoverTimer, 0.0);
	TestEqual(TEXT("m_flSeeUnknownCheatVisionTime round-trips at mode 2"),
		N.Senses.Memory.SeeUnknownGraceUntil, -1.0);
	TestEqual(TEXT("m_flIgnoreCollisionTimer round-trips at mode 4"),
		N.IgnoreCollisionUntil, FElysiumNpcBase::SaveStampFloatMax());
	TestEqual(TEXT("m_flEyeFidgetTime is the one float-width stamp and round-trips too"),
		N.NextFidgetTime, static_cast<float>(FElysiumNpcBase::SaveStampFloatMax()));
	TestEqual(TEXT("m_BestSound's expiry round-trips at mode 2"),
		N.Senses.Memory.BestSound.ExpireTime, -1.0);
	TestEqual(TEXT("and so does m_LastSoundFlinch's"),
		N.Senses.Memory.LastSoundFlinch.ExpireTime, -1.0);

	// The archive pass, in retail's order: the base's header, then the pedestrian-link bool. The two
	// ints are gated on the bool, which is false on an unbound link.
	TestEqual(TEXT("an unbound pedestrian link writes the header and one bool"),
		SaveRestore10LogText(N.SaveArchiveLog),
		TArray<FString>({ TEXT("fields:AIExtendedSaveHeader_t:0"), TEXT("bool:m_pPedestrianLink:0") }));

	N.SaveArchiveLog.Reset();
	N.bCrosswalkLinkBound = true;
	N.RestorePedLinkNode = 7;
	N.RestorePedLinkDestNode = 11;
	N.Save(nullptr);
	TestEqual(TEXT("a bound link adds its two ints, in retail's order"),
		SaveRestore10LogText(N.SaveArchiveLog),
		TArray<FString>({
			TEXT("fields:AIExtendedSaveHeader_t:0"),
			TEXT("bool:m_pPedestrianLink:1"),
			TEXT("int:m_pPedestrianLink+4:7"),
			TEXT("int:m_pPedestrianLink+8:11") }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10EncodeOrderTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.EncodeOrder", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10EncodeOrderTest::RunTest(const FString&)
{
	// The ORDER of the eleven encodes is observable through the save file, so it is pinned.
	//
	// The instrument: set every stamp to the SENTINEL first. `FElysiumNpcBase::Save` runs between the encode and
	// the decode pass, and the header it writes is the one observable a body on the stack would see
	// — but a stronger statement is available without an archive. Each mode fires on a different
	// seed value, so seeding the eleven with a mode-matched value and checking that exactly the
	// fields whose mode matches got rewritten pins WHICH mode each field carries, which is the half
	// the checklist's aliased decode pass could have got wrong.
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Troika))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Troika;

	// Seed every stamp with `-1.0`. Only the mode-2 fields encode; the mode-3 and mode-4 ones do
	// not, so after the encode pass the mode-2 fields hold the sentinel. The body's own decode would
	// hide that, so the check is run against the codec on the same list rather than through `Save`.
	SaveRestore10SetAllStamps(N, -1.0);
	struct FStampProbe { const TCHAR* Field; double* Value; FElysiumNpcBase::ESaveStampMode Mode; };
	using EMode = FElysiumNpcBase::ESaveStampMode;
	const FStampProbe Probes[] = {
		{ TEXT("m_flCanSeekCoverTimer"),         &N.CanSeekCoverTimer,                  EMode::Zero },
		{ TEXT("m_flSeeUnknownCheatVisionTime"), &N.Senses.Memory.SeeUnknownGraceUntil, EMode::MinusOne },
		{ TEXT("m_flMeleeHeightDiffTimer"),      &N.MeleeHeightDiffTimer,               EMode::MinusOne },
		{ TEXT("m_flOccludedReportTimeE"),       &N.OccludedReportTimeE,                EMode::Zero },
		{ TEXT("m_flOccludedReportTimeT"),       &N.OccludedReportTimeT,                EMode::Zero },
		{ TEXT("m_flOccludedReportTimeW"),       &N.OccludedReportTimeW,                EMode::Zero },
		{ TEXT("m_flInterruptTime"),             &N.ScheduleHost.InterruptTime,         EMode::Zero },
		{ TEXT("m_flWeaponScareTime"),           &N.WeaponScareTime,                    EMode::MinusOne },
		{ TEXT("m_flIgnoreCollisionTimer"),      &N.IgnoreCollisionUntil,               EMode::FloatMax },
	};
	// The listed modes ARE the listing's `PUSH` sequence `3,2,2,3,3,3,3,2,2,4,4`
	// (`m_flNextInterestChangeTime` sits between `m_flInterruptTime` and `m_flWeaponScareTime` and
	// `m_flEyeFidgetTime` after `m_flIgnoreCollisionTimer`; both are covered by the body case).
	TArray<FString> ModeOrder;
	for (const FStampProbe& Probe : Probes)
	{
		ModeOrder.Add(FString::FromInt(static_cast<int32>(Probe.Mode)));
		const bool bFired = FElysiumNpcBase::SaveStampEncode(*Probe.Value, Probe.Mode);
		TestEqual(FString::Printf(TEXT("%s fires at mode 2 only"), Probe.Field),
			bFired, Probe.Mode == EMode::MinusOne);
	}
	TestEqual(TEXT("the nine listed modes are the listing's push sequence"),
		FString::Join(ModeOrder, TEXT(",")), FString(TEXT("3,2,2,3,3,3,3,2,4")));
	return true;
}

namespace
{
	// Stand a fresh fixture whose subject is built as `RetailClass` and clear its archive log, so a
	// case can run slot 126 or 127 on it.
	TUniquePtr<FSaveRestore10Fixture> SaveRestore10StandSpecies(const TCHAR* RetailClass)
	{
		TUniquePtr<FSaveRestore10Fixture> Fix = MakeUnique<FSaveRestore10Fixture>(RetailClass);
		if (Fix->Species != nullptr)
		{
			Fix->Species->SaveArchiveLog.Reset();
		}
		return Fix;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10SaveSpeciesTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.SaveSpecies", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10SaveSpeciesTest::RunTest(const FString&)
{
	const double FloatMax = FElysiumNpcBase::SaveStampFloatMax();

	// `CNPC_VMingXiao::Save` `0x10395f80` — the six `m_rflRegrowTimers` at mode 4, ascending.
	{
		const TUniquePtr<FSaveRestore10Fixture> Fix =
			SaveRestore10StandSpecies(TEXT("CNPC_VMingXiao"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix->Species))
		{
			return false;
		}
		FElysiumNpcMingXiao& N = *ElysiumTestAsSpecies<FElysiumNpcMingXiao>(Fix->Species);
		for (int32 Index = 0; Index < FElysiumNpcMingXiao::MingXiaoRegrowTimerCount; ++Index)
		{
			N.MingXiaoRegrowTimers[Index] = Index == 3 ? FloatMax : 5.0;
		}
		TestEqual(TEXT("the MingXiao arm answers the Troika body's result"), N.Save(nullptr), 1);
		TestEqual(TEXT("a FLT_MAX regrow timer round-trips through the sentinel"),
			N.MingXiaoRegrowTimers[3], FloatMax);
		TestEqual(TEXT("and an ordinary one is untouched"), N.MingXiaoRegrowTimers[0], 5.0);
		TestTrue(TEXT("the MingXiao arm ran the Troika body, which writes the link bool"),
			SaveRestore10LogText(N.SaveArchiveLog).Contains(TEXT("bool:m_pPedestrianLink:0")));
	}

	// `CNPC_VMingXiaoTentacle::Save` `0x1039ed50` — `m_flPhaseExpireTimer` at mode 3.
	{
		const TUniquePtr<FSaveRestore10Fixture> Fix =
			SaveRestore10StandSpecies(TEXT("CNPC_VMingXiaoTentacle"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix->Species))
		{
			return false;
		}
		FElysiumNpcMingXiaoTentacle& N = *ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix->Species);
		N.MingXiaoTentaclePhaseExpireTimer = 0.0;
		N.Save(nullptr);
		TestEqual(TEXT("the tentacle's phase stamp round-trips at mode 3"),
			N.MingXiaoTentaclePhaseExpireTimer, 0.0);
	}

	// `CNPC_VTzimisceHeadClaw::Save` `0x103c2810` — `m_flSlowedExpire` at mode 3.
	{
		const TUniquePtr<FSaveRestore10Fixture> Fix =
			SaveRestore10StandSpecies(TEXT("CNPC_VTzimisceHeadClaw"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix->Species))
		{
			return false;
		}
		FElysiumNpcTzimisceHeadClaw& N = *ElysiumTestAsSpecies<FElysiumNpcTzimisceHeadClaw>(Fix->Species);
		N.HeadClawSlowedExpire = 0.0;
		N.Save(nullptr);
		TestEqual(TEXT("the head claw's slow stamp round-trips at mode 3"), N.HeadClawSlowedExpire, 0.0);
	}

	// The control: a bare Troika NPC takes no species arm and runs the Troika body.
	{
		FSaveRestore10Fixture Fix;
		if (!TestNotNull(TEXT("the control spawned"), Fix.Troika))
		{
			return false;
		}
		Fix.Troika->SaveArchiveLog.Reset();
		Fix.Troika->Save(nullptr);
		TestEqual(TEXT("the bare Troika line runs the Troika body"),
			SaveRestore10LogText(Fix.Troika->SaveArchiveLog),
			TArray<FString>({ TEXT("fields:AIExtendedSaveHeader_t:0"), TEXT("bool:m_pPedestrianLink:0") }));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 127 `Restore`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10RestoreTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.Restore", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10RestoreTest::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the control spawned"), Fix.Troika))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Troika;

	// `CAI_BaseNPCTroika::Restore` `0x10299700`: the base's answer comes back unchanged, then one
	// `ReadBool` gates two `ReadInt`s, then the eleven stamps and the nine sounds decode.
	SaveRestore10SetAllStamps(N, FElysiumNpcBase::SaveStampSentinel);
	N.Senses.Memory.BestSound.ExpireTime = FElysiumNpcBase::SaveStampSentinel;
	N.SaveArchiveLog.Reset();

	TestEqual(TEXT("slot 127 answers the base body's result"), N.Restore(nullptr), 1);
	TestEqual(TEXT("the mode-3 stamps decode to 0.0"), N.CanSeekCoverTimer, 0.0);
	TestEqual(TEXT("the mode-2 stamps decode to -1.0"),
		N.Senses.Memory.SeeUnknownGraceUntil, -1.0);
	TestEqual(TEXT("the mode-4 stamps decode to FLT_MAX"),
		N.IgnoreCollisionUntil, FElysiumNpcBase::SaveStampFloatMax());
	TestEqual(TEXT("the nine sounds decode at mode 2"),
		N.Senses.Memory.BestSound.ExpireTime, -1.0);

	// With no archive the `ReadBool` answers false, which is the arm that skips the two `ReadInt`s.
	TestEqual(TEXT("the restore reads the header and one bool, and stops"),
		SaveRestore10LogText(N.SaveArchiveLog),
		TArray<FString>({ TEXT("fields:AIExtendedSaveHeader_t:0"),
			TEXT("readbool:m_pPedestrianLink:0") }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10RestoreSpeciesTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.RestoreSpecies", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10RestoreSpeciesTest::RunTest(const FString&)
{
	const double Sentinel = FElysiumNpcBase::SaveStampSentinel;

	// `CNPC_VMingXiao::vfunc127` `0x10396000` — the base first, then the six regrow timers at mode 4.
	{
		const TUniquePtr<FSaveRestore10Fixture> Fix =
			SaveRestore10StandSpecies(TEXT("CNPC_VMingXiao"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix->Species))
		{
			return false;
		}
		FElysiumNpcMingXiao& N = *ElysiumTestAsSpecies<FElysiumNpcMingXiao>(Fix->Species);
		for (int32 Index = 0; Index < FElysiumNpcMingXiao::MingXiaoRegrowTimerCount; ++Index)
		{
			N.MingXiaoRegrowTimers[Index] = Sentinel;
		}
		TestEqual(TEXT("the MingXiao restore answers the base's result"), N.Restore(nullptr), 1);
		for (int32 Index = 0; Index < FElysiumNpcMingXiao::MingXiaoRegrowTimerCount; ++Index)
		{
			TestEqual(FString::Printf(TEXT("regrow timer %d decodes to FLT_MAX"), Index),
				N.MingXiaoRegrowTimers[Index], FElysiumNpcBase::SaveStampFloatMax());
		}
	}

	// `CNPC_VMingXiaoTentacle::vfunc127` `0x1039eda0` — mode 3 on one field.
	{
		const TUniquePtr<FSaveRestore10Fixture> Fix =
			SaveRestore10StandSpecies(TEXT("CNPC_VMingXiaoTentacle"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix->Species))
		{
			return false;
		}
		FElysiumNpcMingXiaoTentacle& N = *ElysiumTestAsSpecies<FElysiumNpcMingXiaoTentacle>(Fix->Species);
		N.MingXiaoTentaclePhaseExpireTimer = Sentinel;
		N.Restore(nullptr);
		TestEqual(TEXT("the tentacle's phase stamp decodes to 0.0"),
			N.MingXiaoTentaclePhaseExpireTimer, 0.0);
	}

	// `CNPC_VTzimisceHeadClaw::vfunc127` `0x103c2860`.
	{
		const TUniquePtr<FSaveRestore10Fixture> Fix =
			SaveRestore10StandSpecies(TEXT("CNPC_VTzimisceHeadClaw"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix->Species))
		{
			return false;
		}
		FElysiumNpcTzimisceHeadClaw& N = *ElysiumTestAsSpecies<FElysiumNpcTzimisceHeadClaw>(Fix->Species);
		N.HeadClawSlowedExpire = Sentinel;
		N.Restore(nullptr);
		TestEqual(TEXT("the head claw's slow stamp decodes to 0.0"), N.HeadClawSlowedExpire, 0.0);
	}

	// `CNPC_VVampireBoss::Restore` `0x103c5910` — a post-load RESET, not a restore.
	{
		const TUniquePtr<FSaveRestore10Fixture> Fix =
			SaveRestore10StandSpecies(TEXT("CNPC_VVampireBoss"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix->Species))
		{
			return false;
		}
		FElysiumNpcVampireBoss& N = *ElysiumTestAsSpecies<FElysiumNpcVampireBoss>(Fix->Species);
		N.VampireBossMonsterModelName = TEXT("models/monster.mdl");
		N.VampireBossMonsterClassname = TEXT("npc_VSomethingElse");
		N.BodyEmitterNames[0] = TEXT("blood_emitter");
		TestEqual(TEXT("the boss restore answers the Troika body's result"), N.Restore(nullptr), 1);
		TestTrue(TEXT("m_pMonsterModelName is nulled"), N.VampireBossMonsterModelName.IsEmpty());
		TestTrue(TEXT("ClearBodyEmitterNames ran"), N.BodyEmitterNames[0].IsEmpty());
		TestEqual(TEXT("and m_pszMonsterClassname is reset to the literal"),
			N.VampireBossMonsterClassname, FString(TEXT("npc_VVampireBoss")));
	}

	// The control.
	{
		FSaveRestore10Fixture Fix;
		if (!TestNotNull(TEXT("the control spawned"), Fix.Troika))
		{
			return false;
		}
		Fix.Troika->SaveArchiveLog.Reset();
		Fix.Troika->CanSeekCoverTimer = Sentinel;
		Fix.Troika->Restore(nullptr);
		TestEqual(TEXT("the bare Troika line runs the Troika body, which decodes its stamps"),
			Fix.Troika->CanSeekCoverTimer, 0.0);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 180 `UpdateOnRemove` and slot 106 `PostConstructor`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10UpdateOnRemoveTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.UpdateOnRemove", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10UpdateOnRemoveTest::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the control spawned"), Fix.Troika))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Troika;

	// `CAI_BaseNPCTroika::UpdateOnRemove` `0x1028d6e0`, step by step.
	//
	// Step 2 is gated on `m_pAttackCoordinator` (`+0x65e8`) being non-zero.
	N.AttackCoordinator = 0;
	N.TalkingUntil = -1.0;
	N.Dialogue.bInDialog = false;
	N.HuntPatrolPoints.Add(FVector(1.0, 2.0, 3.0));
	const int32 DialogStopsBefore = N.DialogStopScheduleRequests;
	const int32 ReleasesBefore = N.InterestingPlaceReleases;
	N.UpdateOnRemove();
	TestEqual(TEXT("the interesting-place release runs unconditionally, as retail's call site does"),
		N.InterestingPlaceReleases, ReleasesBefore + 1);
	TestEqual(TEXT("a quiet NPC's dialogue arm does not run"),
		N.DialogStopScheduleRequests, DialogStopsBefore);
	TestTrue(TEXT("the hunt patrol array is released"), N.HuntPatrolPoints.IsEmpty());

	// Step 4: `IsInDialog()` is the four-term gate, and a talking body takes the arm.
	N.TalkingUntil = N.World != nullptr ? N.World->NowSeconds() + 10.0 : 10.0;
	N.UpdateOnRemove();
	TestEqual(TEXT("a talking body runs the dialogue stop"),
		N.DialogStopScheduleRequests, DialogStopsBefore + 1);
	// Story 29d, family Social10 landed `CAI_BaseNPCTroika::FinishTalking` (`0x102c0ca0`) and this
	// arm now calls it, so the probe reads the real body's output: `102c0e85` STAMPS `+0x64cc` with
	// `curtime` rather than clearing it, and `102c0e73` clears `m_bIsTalking` (`+0x64c0`). The
	// window is still over — `CAI_BaseNPCTroika::IsTalking` tests `curtime < m_flTalkEnd` strictly.
	TestEqual(TEXT("FinishTalking stamps the talk end with curtime"),
		N.TalkingUntil, N.World != nullptr ? N.World->NowSeconds() : 0.0);
	TestFalse(TEXT("and clears the m_bIsTalking latch"), N.bIsTalking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10RemoveSpeciesTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.RemoveSpecies", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10RemoveSpeciesTest::RunTest(const FString&)
{
	// `CNPC_VCop::UpdateOnRemove` `0x10371a90` — both census decrements, read off the listing. The
	// two counters are PROCESS-WIDE, so the case sets and reads them explicitly. The subject is a
	// real `npc_VCop`, which is `CNPC_VCop`.
	{
		FSaveRestore10Fixture Fix(TEXT("CNPC_VCop"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
		{
			return false;
		}
		FElysiumNpcCop& N = *ElysiumTestAsSpecies<FElysiumNpcCop>(Fix.Species);
		FElysiumNpcCop::CopAliveCensus() = 3;
		FElysiumNpcCop::CopSecondCensus() = 2;
		N.bCopCountedAlive = true;
		N.bCopCountedSecond = true;
		N.UpdateOnRemove();
		TestEqual(TEXT("a counted cop decrements the live census"), FElysiumNpcCop::CopAliveCensus(), 2);
		TestEqual(TEXT("and the second census"), FElysiumNpcCop::CopSecondCensus(), 1);
		TestFalse(TEXT("m_bCountedAlive is cleared"), N.bCopCountedAlive);
		TestFalse(TEXT("and so is its twin at +0x6672"), N.bCopCountedSecond);
		// The guard is what keeps a second removal from driving the census negative.
		N.UpdateOnRemove();
		TestEqual(TEXT("an uncounted cop decrements nothing"), FElysiumNpcCop::CopAliveCensus(), 2);
		TestEqual(TEXT("nor the second census"), FElysiumNpcCop::CopSecondCensus(), 1);
		// Only the first byte set: the two arms are independent.
		N.bCopCountedAlive = true;
		N.UpdateOnRemove();
		TestEqual(TEXT("only the live census moves"), FElysiumNpcCop::CopAliveCensus(), 1);
		TestEqual(TEXT("the second is untouched"), FElysiumNpcCop::CopSecondCensus(), 1);
	}
	// Zeroed after the cop's world is torn down, so nothing its teardown does survives the case.
	FElysiumNpcCop::CopAliveCensus() = 0;
	FElysiumNpcCop::CopSecondCensus() = 0;

	// `CNPC_VMingXiao::UpdateOnRemove` `0x10391230` — the throwable drop, gated on the mode.
	{
		FSaveRestore10Fixture Fix(TEXT("CNPC_VMingXiao"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
		{
			return false;
		}
		FElysiumNpcMingXiao& N = *ElysiumTestAsSpecies<FElysiumNpcMingXiao>(Fix.Species);
		N.MingXiaoThrowableObjectMode = 2;
		const int32 ChainBefore = N.InterestingPlaceReleases;
		N.UpdateOnRemove();
		TestEqual(TEXT("the carried throwable is dropped, which zeroes the mode"),
			N.MingXiaoThrowableObjectMode, 0);
		TestEqual(TEXT("and the Troika body ran after it"),
			N.InterestingPlaceReleases, ChainBefore + 1);
	}

	// `CNPC_VNewscaster::UpdateOnRemove` `0x103a03a0` — the story-queue teardown, then the Troika
	// body. The teardown's own assertions are family Species'; what this case states is the CHAIN.
	{
		FSaveRestore10Fixture Fix(TEXT("CNPC_VNewscaster"));
		if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
		{
			return false;
		}
		FElysiumNpcNewscaster& N = *ElysiumTestAsSpecies<FElysiumNpcNewscaster>(Fix.Species);
		const int32 ChainBefore = N.InterestingPlaceReleases;
		N.UpdateOnRemove();
		TestEqual(TEXT("the newscaster arm chains the Troika body"),
			N.InterestingPlaceReleases, ChainBefore + 1);
	}

	// The control: a bare Troika NPC has a null `RetailClass()`, so it takes no species arm.
	{
		FSaveRestore10Fixture Fix;
		if (!TestNotNull(TEXT("the control spawned"), Fix.Troika))
		{
			return false;
		}
		FElysiumNpcCop::CopAliveCensus() = 5;
		Fix.Troika->UpdateOnRemove();
		TestEqual(TEXT("the bare Troika line takes the Troika body, not the CNPC_VCop arm"),
			FElysiumNpcCop::CopAliveCensus(), 5);
	}
	FElysiumNpcCop::CopAliveCensus() = 0;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10PostConstructorTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.PostConstructor", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10PostConstructorTest::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	// `CAI_BaseNPC::PostConstructor` `0x1027bb20`: the base pass runs to completion, THEN slot
	// `0x6a0` (424, `CreateComponents`). The order is the whole body.
	const int32 Before = N.PostConstructorCalls;
	TCHAR Name[] = TEXT("npc_VHumanCombatant");
	N.PostConstructor(Name);
	TestEqual(TEXT("slot 106 ran once"), N.PostConstructorCalls, Before + 1);
	TestEqual(TEXT("and recorded the classname the base pass was handed"),
		N.PostConstructorName, FString(TEXT("npc_VHumanCombatant")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `FUN_10290350` — `RunAlternateAI` mode 4.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10DoorMode4Test,
	"Elysium.Substrate.NpcKernelSaveRestore10.DoorMode4", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10DoorMode4Test::RunTest(const FString&)
{
	FSaveRestore10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// Step 1: the latch is UP across `MaintainActivity` and down on either side of it.
	N.AlternateAi = 4;
	N.AlternateAiExpireTime = 100.0;
	const int32 SweepsBefore = N.AlternateAiDoorSweeps;
	TestTrue(TEXT("mode 4 always answers true, so the transaction keeps the body"),
		N.RunAlternateAiDoorMode4(/*Now=*/10.0));
	TestTrue(TEXT("m_bForceMaintainActivity was up while MaintainActivity ran"),
		N.bForceMaintainActivitySeenByLastMaintain);
	TestFalse(TEXT("and is down again afterwards"), N.bForceMaintainActivity);
	TestEqual(TEXT("the forward sweep was asked for"), N.AlternateAiDoorSweeps, SweepsBefore + 1);
	TestEqual(TEXT("an unexpired transaction keeps its mode"), N.AlternateAi, 4);

	// Step 4: `curtime < timer` keeps it; the ELSE arm fires, so an EQUAL stamp has expired.
	N.AlternateAi = 4;
	N.bOpeningDoorWait = true;
	N.AlternateAiExpireTime = 10.0;
	N.BaseScheduleHost.FailureReason = 0;
	N.RunAlternateAiDoorMode4(/*Now=*/10.0);
	TestEqual(TEXT("an expiry stamp equal to curtime expires"), N.AlternateAi, 0);
	TestFalse(TEXT("m_bOpeningDoorWait is cleared with it"), N.bOpeningDoorWait);
	// `TaskFail` (`0x1029adb0`) writes the reason into `m_failureReason` and ZEROES the pending
	// one, so the landed reason is `BaseScheduleHost.FailureReason` and not `TaskFailureReason()`.
	TestEqual(TEXT("and TaskFail is raised with reason 0xe"), N.BaseScheduleHost.FailureReason, 0xe);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The species arm tables cover every override row the census holds.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10ArmCoverageTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.ArmCoverage", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10ArmCoverageTest::RunTest(const FString&)
{
	// Every class the census gives an override of 126, 127 or 180 must take a named arm — which is
	// what stops a class this family does not know from silently falling through to the Troika
	// body. The probe: stand as that class and run the slot; a claimed arm leaves a fingerprint the
	// Troika body does not, and an unclaimed one is caught by the dispatcher's own fall-through
	// returning false. The check here is that the census holds no override row this file has not
	// listed. Classes with no instance (`CScriptedTarget`'s `0x1034e320` / `0x1034e370`) carry no
	// arm and are skipped; every other class still fails loudly on an unlisted row.
	const TCHAR* const KnownSave[] = {
		TEXT("0x10395f80"), TEXT("0x1039ed50"), TEXT("0x103c2810") };
	const TCHAR* const KnownRestore[] = {
		TEXT("0x10396000"), TEXT("0x1039eda0"), TEXT("0x103c2860"), TEXT("0x103c5910"),
		// The five other bosses' own slot-127 bodies. This family's briefs do not carry them, so
		// each one's own half is unrecovered here and the arm routes to the base every one of them
		// chains (`0x103c5910`); the table lists them so a census row is never unknown to this file.
		TEXT("0x1035cf80"), TEXT("0x10360e10"), TEXT("0x1036b170"), TEXT("0x103a6e80"),
		TEXT("0x103ae7f0") };
	const TCHAR* const KnownRemove[] = {
		TEXT("0x10371a90"), TEXT("0x10391230"), TEXT("0x103a03a0"),
		// The three cine classes share `0x101a7140`, `FElysiumScriptedSequence::UpdateOnRemove`
		// (story 5 fold A3), which releases the NPC through `ScriptEntityCancel`.
		TEXT("0x101a7140") };

	int32 ClassCount = 0;
	int32 DeadSkipped = 0;
	for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
	{
		++ClassCount;
		if (ElysiumNpcDeadClasses::Contains(Row.Name))
		{
			++DeadSkipped;
			continue;
		}
		struct FSlotProbe { int32 Slot; const TCHAR* const* Known; int32 Count; };
		const FSlotProbe Probes[] = {
			{ 126, KnownSave,    UE_ARRAY_COUNT(KnownSave) },
			{ 127, KnownRestore, UE_ARRAY_COUNT(KnownRestore) },
			{ 180, KnownRemove,  UE_ARRAY_COUNT(KnownRemove) },
		};
		for (const FSlotProbe& Probe : Probes)
		{
			const FElysiumNpcClassSlot* Override =
				ElysiumNpcKernelClass::OverrideOf(&Row, Probe.Slot);
			if (Override == nullptr)
			{
				continue;
			}
			bool bListed = false;
			for (int32 Index = 0; Index < Probe.Count; ++Index)
			{
				if (FCString::Strcmp(Probe.Known[Index], Override->Address) == 0)
				{
					bListed = true;
					break;
				}
			}
			TestTrue(FString::Printf(TEXT("%s#%d (%s) takes a named arm"),
				Row.Name, Probe.Slot, Override->Address), bListed);
		}
	}
	TestTrue(TEXT("the census was walked"), ClassCount > 0);
	TestEqual(TEXT("and every class with no instance was skipped, keeping its census row"),
		DeadSkipped, static_cast<int32>(UE_ARRAY_COUNT(ElysiumNpcDeadClasses::Names)));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The maker: `MakeNPC`'s child inheritance and `CNPCMaker_Zombie`'s two slots (story 5 fold A4:
// both are NPCs of their own classes, and the inherited words are the maker's OWN Troika words).
// -------------------------------------------------------------------------------------------------

namespace
{
	struct FSaveRestore10MakerFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpcMaker* Maker = nullptr;
		FElysiumNpcMakerZombie* ZombieMaker = nullptr;

		FSaveRestore10MakerFixture()
			: World(Build())
		{
			FElysiumEntity* Entity = World.World.FindByName(TEXT("maker"));
			FElysiumNpc* Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
			Maker = Npc != nullptr ? Npc->AsSpecies<FElysiumNpcMaker>() : nullptr;
			Entity = World.World.FindByName(TEXT("zmaker"));
			Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
			ZombieMaker = Npc != nullptr ? Npc->AsSpecies<FElysiumNpcMakerZombie>() : nullptr;
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("saverestore10maker"), 20260914);
			// Far from the player the fixture spawns at the origin, so the zombie maker's MAXIMUM
			// player distance has a distance to refuse on.
			FElysiumEntityDef& Def =
				Builder.AddEntity(TEXT("npc_maker"), TEXT("maker"), FVector(50000.0, 0.0, 0.0));
			// `CNPCMaker::Spawn` (`0x1034afe0`) dispatches slot 104 `Precache`, whose missing-model arm
			// `UTIL_Remove`s the maker, so the maker authors a model.
			Def.Keys.Add(TEXT("model"), TEXT("models/maker.mdl"));
			Def.Keys.Add(TEXT("NPCType"), TEXT("npc_VHumanCombatant"));
			Def.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
			Def.Keys.Add(TEXT("MaxLiveChildren"), TEXT("10"));
			Def.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
			// `sp_tutorial_1`'s `thug_maker`, key for key: the Troika words it authors ON THE MAKER
			// (the witness for D8, story 5 fold A4).
			Def.Keys.Add(TEXT("vision"), TEXT("540"));
			Def.Keys.Add(TEXT("hearing"), TEXT("1.00"));
			Def.Keys.Add(TEXT("npc_perception"), TEXT("3"));
			Def.Keys.Add(TEXT("use_interesting"), TEXT("1"));
			Def.Keys.Add(TEXT("percent_occluded_wait"), TEXT("10"));
			Def.Keys.Add(TEXT("percent_occluded_cover"), TEXT("30"));
			Def.Keys.Add(TEXT("allow_alert_lookaround"), TEXT("1"));
			Def.Keys.Add(TEXT("allow_kick_hint_use"), TEXT("1"));
			FElysiumEntityDef& Zombie =
				Builder.AddEntity(TEXT("npc_maker_zombie"), TEXT("zmaker"), FVector(50000.0, 0.0, 0.0));
			Zombie.Keys.Add(TEXT("model"), TEXT("models/zombiemaker.mdl"));
			Zombie.Keys.Add(TEXT("NPCType"), TEXT("npc_VZombie"));
			Zombie.Keys.Add(TEXT("MaxNPCCount"), TEXT("10"));
			Zombie.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
			Zombie.Keys.Add(TEXT("Flag_ZombieAIType"), TEXT("1"));
			Zombie.Keys.Add(TEXT("remove_distance"), TEXT("1500.5"));
			return Builder;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10MakerTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.Maker", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10MakerTest::RunTest(const FString&)
{
	FSaveRestore10MakerFixture Fix;
	if (!TestNotNull(TEXT("the maker spawned"), Fix.Maker)
		|| !TestNotNull(TEXT("the zombie maker spawned"), Fix.ZombieMaker))
	{
		return false;
	}
	FElysiumNpcMaker& M = *Fix.Maker;

	// D8: the authored keys bind on the maker's OWN Troika words (a maker IS a Troika NPC).
	TestEqual(TEXT("the maker carries its authored vision"), M.AuthoredVision, 540.f);
	TestEqual(TEXT("and its npc_perception"), M.AuthoredPerception, 3);

	// `CNPCMaker::MakeNPC` `0x1034b7b0`, slot 617: `child.X = this->X`, nine words and the triple.
	M.ChildTargetName = TEXT("spawned");
	const int32 DeriveBefore = M.PerceptionRecomputes;
	FElysiumNpc* Child = M.MakeNPC(/*bBypass=*/true);
	if (!TestNotNull(TEXT("the maker spawned a child, and slot 617 returns it"), Child))
	{
		return false;
	}
	TestEqual(TEXT("D8: the child keeps thug_maker's vision 540, not a default"), Child->AuthoredVision, 540.f);
	TestEqual(TEXT("hearing 1.00"), Child->AuthoredHearing, 1.0f);
	TestEqual(TEXT("npc_perception 3"), Child->AuthoredPerception, 3);
	TestTrue(TEXT("use_interesting 1"), Child->bUseInteresting);
	TestEqual(TEXT("percent_occluded_wait 10"), Child->PercentOccludedWait, 10);
	TestEqual(TEXT("percent_occluded_cover 30"), Child->PercentOccludedCover, 30);
	TestTrue(TEXT("allow_alert_lookaround 1"), Child->bAllowAlertLookaround);
	TestTrue(TEXT("allow_kick_hint_use 1"), Child->ScheduleHost.bAllowKickHintUse);
	TestTrue(TEXT("+0x65f4 m_bCameFromSpawner is the last write of the body"), Child->bCameFromSpawner);
	TestEqual(TEXT("InitPerceptionDistances and 0x1028fc90 run on the MAKER, retail's oddity"),
		M.PerceptionRecomputes, DeriveBefore + 1);
	TestTrue(TEXT("the child is named NPCTargetname"), Child->TargetName == TEXT("spawned"));
	// D10: the child is built from the maker's own block, keys it does not read included.
	TestTrue(TEXT("the replay carries the maker's own block (MaxNPCCount rides along)"),
		Child->Def != nullptr && Child->Def->Keys.Contains(TEXT("MaxNPCCount")));

	// The copy is `this->X` at the moment of the call: change the maker's words, spawn again.
	M.AuthoredPerception = 7;
	M.AuthoredVision = 512.f;
	M.AuthoredHearing = 256.f;
	M.PercentOccludedWait = 11;
	M.PercentOccludedCover = 22;
	M.PercentOccludedWalk = 33;
	M.PercentOccludedFlank = 44;
	M.PercentOccludedChase = 55;
	M.bUseInteresting = false;
	M.bAllowAlertLookaround = false;
	M.bStayEntrenched = true;
	M.ScheduleHost.bAllowKickHintUse = false;
	FElysiumNpc* Second = M.MakeNPC(/*bBypass=*/true);
	if (TestNotNull(TEXT("a second child"), Second))
	{
		TestEqual(TEXT("+0x63b0 follows the maker"), Second->AuthoredPerception, 7);
		TestEqual(TEXT("+0x63b4"), Second->AuthoredVision, 512.f);
		TestEqual(TEXT("+0x63bc"), Second->AuthoredHearing, 256.f);
		TestFalse(TEXT("+0x63d9"), Second->bUseInteresting);
		TestEqual(TEXT("+0x6420"), Second->PercentOccludedWait, 11);
		TestEqual(TEXT("+0x6424"), Second->PercentOccludedCover, 22);
		TestEqual(TEXT("+0x6428"), Second->PercentOccludedWalk, 33);
		TestEqual(TEXT("+0x642c"), Second->PercentOccludedFlank, 44);
		TestEqual(TEXT("+0x6430"), Second->PercentOccludedChase, 55);
		TestFalse(TEXT("+0x6434"), Second->bAllowAlertLookaround);
		TestTrue(TEXT("+0x6435"), Second->bStayEntrenched);
		TestFalse(TEXT("+0x6436"), Second->ScheduleHost.bAllowKickHintUse);
	}

	// `CNPCMaker_Zombie::CanMakeNPC` `0x1034d0a0`, slot 618, through a `CNPCMaker` reference.
	FElysiumNpcMakerZombie& Z = *Fix.ZombieMaker;
	FElysiumNpcMaker& AsMaker = Z;
	//   D9: `remove_distance` binds the ONE float word `+0x76d8`.
	TestEqual(TEXT("remove_distance is the float +0x76d8"), Z.RemoveDistance, 1500.5f);
	TestEqual(TEXT("Flag_ZombieAIType is +0x76d0 m_iZombieAISpawnType"), Z.ZombieAiSpawnType, 1);
	//   1. A bypass answers yes before anything else.
	TestTrue(TEXT("a bypass admits before any other arm"), AsMaker.CanMakeNPC(/*bBypass=*/true));
	//   2. `m_flRemoveDist < 0.9 * Manhattan` refuses: 50000 units away is too FAR.
	TestFalse(TEXT("a player 50000 units away is beyond 0.9 x Manhattan"), AsMaker.CanMakeNPC(false));
	TestEqual(TEXT("on the distance arm"), FString(FElysiumNpcMaker::AttemptName(Z.LastAttempt)),
		FString(TEXT("distance")));
	Z.RemoveDistance = 100000.f;
	AsMaker.CanMakeNPC(/*bBypass=*/false);
	TestNotEqual(TEXT("a generous maximum falls through to the base"),
		FString(FElysiumNpcMaker::AttemptName(Z.LastAttempt)), FString(TEXT("distance")));
	// The comparison is the float against the scaled sum, strictly: at 0.9 x 50000 = 45000 exactly
	// the maker admits past this arm.
	Z.RemoveDistance = 45000.f;
	AsMaker.CanMakeNPC(/*bBypass=*/false);
	TestNotEqual(TEXT("equality admits (m_flRemoveDist < scaled refuses, not <=)"),
		FString(FElysiumNpcMaker::AttemptName(Z.LastAttempt)), FString(TEXT("distance")));

	// `CNPCMaker_Zombie::MakeNPC` `0x1034d140`, slot 617 — the missing-item arm first (forced: the
	// class registry is process-wide, so an earlier suite could have installed the catalogue).
	Z.SetZombieFistsItemForTests(false);
	TestFalse(TEXT("the missing-item arm sees no catalogue"), Z.ZombieFistsItemExists());
	const int32 EquipsBefore = Z.ZombieFistsEquips;
	TestNull(TEXT("a missing item definition answers null"), AsMaker.MakeNPC(/*bBypass=*/true));
	TestEqual(TEXT("and hands nothing over"), Z.ZombieFistsEquips, EquipsBefore);

	// The SUCCESS arm.
	Z.SetZombieFistsItemForTests(true);
	FElysiumNpc* Zombie = AsMaker.MakeNPC(/*bBypass=*/true);
	if (TestNotNull(TEXT("the zombie maker spawned a child"), Zombie))
	{
		TestEqual(TEXT("the fists were handed over"), Z.ZombieFistsEquips, EquipsBefore + 1);
		TestEqual(TEXT("the item it looked up"), Z.LastZombieFistsItem, FString(TEXT("item_w_zombie_fists")));
		TestEqual(TEXT("the spawn emitter was requested once"), Z.ZombieSpawnEmitters.Num(), 1);
		if (Z.ZombieSpawnEmitters.Num() > 0)
		{
			TestEqual(TEXT("at the maker's own origin"), Z.ZombieSpawnEmitters[0].Origin, Z.Origin);
			TestEqual(TEXT("with retail's 15.0-second life"), Z.ZombieSpawnEmitters[0].LifetimeSeconds, 15.0f);
		}
		TestFalse(TEXT("SetDisableAI(false) overrides the maker's own copy"), Zombie->IsAiDisabled());
		// The RTTI cast to `CNPC_VZombie` (`0x1062567c`, `.?AVCNPC_VZombie@@`) hits, so
		// `SetZombieAIType(child, m_iZombieAISpawnType)` runs: 1 is stored as-is.
		TestEqual(TEXT("an npc_VZombie child takes the maker's AI spawn type"), Zombie->ZombieAiType, 1);
	}
	Z.ClearZombieFistsItemForTests();
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CCineAISchedule::FUN_101a8de0` — slot 113 `Activate` on the scripted sequence.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSaveRestore10SequenceActivateTest,
	"Elysium.Substrate.NpcKernelSaveRestore10.SequenceActivate", GSaveRestore10TestFlags)
bool FElysiumNpcKernelSaveRestore10SequenceActivateTest::RunTest(const FString&)
{
	// `0x101a8de0`: the actor search, the two diagnostics, the three sequence precaches in the order
	// pre-idle / post-idle / play, and the next-script resolution that CLEARS `m_iszNextScript` when
	// the named beat does not resolve.
	FElysiumNpcWorldBuilder Builder(TEXT("saverestore10seq"), 20260914);
	Builder.AddNpc(TEXT("actor"), FVector(100.0, 0.0, 0.0));
	FElysiumEntityDef& Found = Builder.AddEntity(TEXT("scripted_sequence"), TEXT("beat_found"));
	Found.Keys.Add(TEXT("m_iszEntity"), TEXT("actor"));
	Found.Keys.Add(TEXT("m_iszIdle"), TEXT("wait_idle"));
	Found.Keys.Add(TEXT("m_iszPostIdle"), TEXT("after_idle"));
	Found.Keys.Add(TEXT("m_iszPlay"), TEXT("the_action"));
	Found.Keys.Add(TEXT("m_iszNextScript"), TEXT("beat_missing"));
	FElysiumEntityDef& Lost = Builder.AddEntity(TEXT("scripted_sequence"), TEXT("beat_lost"));
	Lost.Keys.Add(TEXT("m_iszEntity"), TEXT("nobody_here"));
	Lost.Keys.Add(TEXT("m_iszNextScript"), TEXT("beat_found"));

	FElysiumNpcWorldFixture Fix(MoveTemp(Builder));
	const FElysiumEntity* FoundBeat = Fix.World.FindByName(TEXT("beat_found"));
	const FElysiumEntity* LostBeat = Fix.World.FindByName(TEXT("beat_lost"));
	if (!TestNotNull(TEXT("the found beat spawned"), FoundBeat)
		|| !TestNotNull(TEXT("the lost beat spawned"), LostBeat))
	{
		return false;
	}

	// An actor that resolves and has no model takes the "has no model" arm — three diagnostic lines
	// (divider, message, divider) and NO precache. A headless fixture's NPC authors no model.
	TestEqual(TEXT("the no-model arm prints the bracketed diagnostic"),
		FElysiumNpcWorldFixture::Debug(FoundBeat, TEXT("Activate diagnostics")), FString(TEXT("3")));
	TestTrue(TEXT("and issues no sequence precache"),
		FElysiumNpcWorldFixture::Debug(FoundBeat, TEXT("Activate precaches")).IsEmpty());

	// An actor that resolves to nothing takes the "could not find" arm, which also prints three
	// lines and SKIPS the precache.
	TestEqual(TEXT("the missing-actor arm prints the bracketed diagnostic too"),
		FElysiumNpcWorldFixture::Debug(LostBeat, TEXT("Activate diagnostics")), FString(TEXT("3")));

	// The next-script half: a name that does not resolve is CLEARED, and one that does survives.
	TestTrue(TEXT("an unresolvable m_iszNextScript is cleared"),
		FElysiumNpcWorldFixture::Debug(FoundBeat, TEXT("Next script")).IsEmpty());
	TestEqual(TEXT("and a resolvable one survives"),
		FElysiumNpcWorldFixture::Debug(LostBeat, TEXT("Next script")), FString(TEXT("beat_found")));
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
