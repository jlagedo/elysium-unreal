#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Hints10** — slot 566 and the Werewolf's hint endpoints. Every assertion is
// read off the decompiled C or the listing of the body it names and carries that address; none of
// them is read off the checklist's one-line walk, four of which this family corrected.

static constexpr EAutomationTestFlags GHints10TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Prefixed because the module builds adaptive-unity and this anonymous namespace is merged with
	// the other suites'.
	FElysiumNpc::FHintWords Hints10MakeHint(int32 HintType)
	{
		FElysiumNpc::FHintWords Hint;
		Hint.bValid = true;
		Hint.HintType = HintType;
		// `+0x470 m_iGroupID` — a 32-bit SET. The NPC's own mask defaults to `0xffffffff`, so any
		// non-zero group passes the gate at `10295ca0`.
		Hint.GroupMask = 1;
		return Hint;
	}

	struct FHints10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;

		FHints10Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("hints10_kernel"), 4141);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpc(TEXT("guard"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("guard"));
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}
	};
}

// =================================================================================================
// `0x10295c20` — the slot-566 Troika dispatcher.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHints10ValidateArmTest,
	"Elysium.Substrate.NpcKernelHints10.ValidateHintTypeArms", GHints10TestFlags)
bool FElysiumNpcKernelHints10ValidateArmTest::RunTest(const FString&)
{
	using EArm = FElysiumNpc::EHintTypeArm;
	auto Arm = [](int32 Type) { return FElysiumNpc::FValidateHintTypeArm(Type); };

	// `10295d8f CMP EAX,0x64 / JL` — the low boundary, CONFIRMED at the listing.
	TestTrue(TEXT("0x63 refuses"), Arm(0x63) == EArm::Refuse);
	TestTrue(TEXT("0x64 takes the loose cover validator"), Arm(0x64) == EArm::CoverValidLoose);
	TestTrue(TEXT("0x65 takes it too"), Arm(0x65) == EArm::CoverValidLoose);
	TestTrue(TEXT("0x66 refuses"), Arm(0x66) == EArm::Refuse);

	// `10295d99 CMP EAX,0x2774 / JNZ` — the one outright accept.
	TestTrue(TEXT("0x2774 is accepted outright"), Arm(0x2774) == EArm::Accept);
	TestTrue(TEXT("0x2773 refuses"), Arm(0x2773) == EArm::Refuse);
	TestTrue(TEXT("0x2775 refuses"), Arm(0x2775) == EArm::Refuse);

	// `10295d8d JZ 0x10297430`.
	TestTrue(TEXT("0x27d8 takes IsHintCoverValid"), Arm(0x27d8) == EArm::CoverValid);

	// `10295ded CMP EAX,0x283c / JL` — the upper boundary, CONFIRMED at the listing.
	TestTrue(TEXT("0x27d9 refuses"), Arm(0x27d9) == EArm::Refuse);
	TestTrue(TEXT("0x283b refuses"), Arm(0x283b) == EArm::Refuse);
	TestTrue(TEXT("0x283c takes the quiet cover rule"), Arm(0x283c) == EArm::QuietCoverRule);
	TestTrue(TEXT("0x283d takes it too"), Arm(0x283d) == EArm::QuietCoverRule);
	TestTrue(TEXT("0x283e refuses"), Arm(0x283e) == EArm::Refuse);

	// `10295dff CMP EAX,0x28a0 / JNZ`.
	TestTrue(TEXT("0x28a0 takes the verbose cover rule"), Arm(0x28a0) == EArm::VerboseCoverRule);
	TestTrue(TEXT("0x289f refuses"), Arm(0x289f) == EArm::Refuse);

	// `10295d92` puts the `0x2774` accept INSIDE the `99 < t` block, so a negative or small type can
	// never reach it.
	TestTrue(TEXT("a type below 0x64 refuses even at 0"), Arm(0) == EArm::Refuse);
	TestTrue(TEXT("and at -1"), Arm(-1) == EArm::Refuse);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHints10GroupGateTest,
	"Elysium.Substrate.NpcKernelHints10.HintGroupGate", GHints10TestFlags)
bool FElysiumNpcKernelHints10GroupGateTest::RunTest(const FString&)
{
	FHints10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `10295ca0` — `hint->+0x470 & this->+0x62e4`. Both sides are 32-bit SETS.
	FElysiumNpc::FHintWords Hint = Hints10MakeHint(0x2774);
	Hint.GroupMask = 1u << 3;
	F.Npc->ScheduleHost.HintGroupMask = 1u << 3;
	TestTrue(TEXT("a shared group bit admits, and 0x2774 is then accepted outright"),
		F.Npc->FValidateHintType(&Hint));

	const int32 RefusalsBefore = F.Npc->HintGroupRefusals;
	F.Npc->ScheduleHost.HintGroupMask = 1u << 4;
	TestFalse(TEXT("a disjoint mask refuses before the type is ever looked at"),
		F.Npc->FValidateHintType(&Hint));
	TestEqual(TEXT("and the refusal is counted"), F.Npc->HintGroupRefusals, RefusalsBefore + 1);

	// `10295c96` — the null hint takes the `XOR AL,AL` tail without touching the mask.
	TestFalse(TEXT("a null hint refuses"), F.Npc->FValidateHintType(nullptr));

	// `10295cf0 LEA EDX,[ESI + 0x1]` and the format at `0x105a1814`, which the pinned image reads as
	// `" %d"`. The indices are ONE-BASED.
	TestEqual(TEXT("bit 0 prints as 1"), FElysiumNpc::HintGroupBitList(1u), FString(TEXT(" 1")));
	TestEqual(TEXT("bits 0 and 31 print in ascending order"),
		FElysiumNpc::HintGroupBitList(1u | (1u << 31)), FString(TEXT(" 1 32")));
	TestEqual(TEXT("an empty mask prints nothing"), FElysiumNpc::HintGroupBitList(0u), FString());
	return true;
}

// =================================================================================================
// `0x1038e480` — `CNPC_VManBat::FValidateHintType`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHints10ManBatTest,
	"Elysium.Substrate.NpcKernelHints10.ManBatValidateHintType", GHints10TestFlags)
bool FElysiumNpcKernelHints10ManBatTest::RunTest(const FString&)
{
	FHints10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `0x1042fbf0` on its own, and the caller-side ladder at `1038e49b`. Both values were computed
	// from the LISTING's constants independently of this port, so they pin the ladder rather than
	// restate it.
	TestEqual(TEXT("0x1042fbf0 folds 0 to 0xfa0b0e5c"),
		static_cast<int64>(FElysiumNpc::HintObfuscationFold(0u)),
		static_cast<int64>(0xfa0b0e5cu));
	TestEqual(TEXT("the whole descramble takes the zero word to 0xbb258278"),
		static_cast<int64>(FElysiumNpc::ManBatHintMode(0u)),
		static_cast<int64>(0xbb258278u));

	// The descramble is a BIJECTION over the ladder's masked bits, so every mode 1..8 has exactly
	// one `+0x6670` word that produces it. These nine were recovered by inverting the ladder from
	// the listing, and they let the whole body run end to end.
	struct FModeWord { uint32 Word; uint32 Mode; };
	static const FModeWord ModeWords[] =
	{
		{ 0xbb2782fbu, 1 }, { 0xbb2782fau, 2 }, { 0xbb2782f9u, 3 }, { 0xbb2782fcu, 4 },
		{ 0xbb2782f7u, 5 }, { 0xbb2782feu, 6 }, { 0xbb2782f5u, 7 }, { 0xbb2782f0u, 8 },
		{ 0xbb2782f8u, 0 },
	};
	for (const FModeWord& Row : ModeWords)
	{
		TestEqual(FString::Printf(TEXT("word 0x%08x descrambles to mode %u"), Row.Word, Row.Mode),
			static_cast<int64>(FElysiumNpc::ManBatHintMode(Row.Word)), static_cast<int64>(Row.Mode));
	}

	// `1038e48b CMP [EBX + 0x5dc],0x4e20 / JNZ 0x1038e5b3` — anything but 20000 refuses before the
	// scrambled word is even read.
	FElysiumNpc::FHintWords Wrong = Hints10MakeHint(19999);
	TestFalse(TEXT("a hint type other than 20000 refuses"), F.Npc->ManBatValidateHintType(Wrong));

	// The five templates. Driven through `ManBatHintName` directly, because the mode is the output of
	// a 12-instruction descramble and a test that fixed the scrambled word would be asserting the
	// descramble twice.
	TestEqual(TEXT("mode 1 is the raw literal, with no index"),
		FElysiumNpc::ManBatHintName(1, 7), FString(TEXT("ManBat Landpoint")));
	TestEqual(TEXT("mode 2 is the divepoint format"),
		FElysiumNpc::ManBatHintName(2, 7), FString(TEXT("ManBat Divepoint 7")));
	TestEqual(TEXT("mode 4 shares it"),
		FElysiumNpc::ManBatHintName(4, 7), FString(TEXT("ManBat Divepoint 7")));
	TestEqual(TEXT("mode 3 is the bottom variant"),
		FElysiumNpc::ManBatHintName(3, 2), FString(TEXT("ManBat Divepoint 2 Bottom")));
	TestEqual(TEXT("mode 8 is the script node"),
		FElysiumNpc::ManBatHintName(8, 3), FString(TEXT("ManBat Script Node 3")));
	// `1038e4cb JA 0x1038e51b` and the jump-table entries for 5, 6 and 7, which all point at the
	// default label.
	TestEqual(TEXT("mode 5 takes the default"),
		FElysiumNpc::ManBatHintName(5, 1), FString(TEXT("ManBat 1")));
	TestEqual(TEXT("mode 6 takes the default"),
		FElysiumNpc::ManBatHintName(6, 1), FString(TEXT("ManBat 1")));
	TestEqual(TEXT("mode 7 takes the default"),
		FElysiumNpc::ManBatHintName(7, 1), FString(TEXT("ManBat 1")));
	TestEqual(TEXT("mode 0 wraps above 7 through the DEC and takes the default"),
		FElysiumNpc::ManBatHintName(0, 9), FString(TEXT("ManBat 9")));
	TestEqual(TEXT("mode 9 takes it too"),
		FElysiumNpc::ManBatHintName(9, 9), FString(TEXT("ManBat 9")));

	// The whole body, end to end. Mode 3, index 4 -> `"ManBat Divepoint 4 Bottom"`.
	F.Npc->ManBatHintModeWord = 0xbb2782f9u;
	F.Npc->ManBatHintIndex = 4;
	FElysiumNpc::FHintWords Hint = Hints10MakeHint(20000);
	Hint.Name = TEXT("ManBat Divepoint 4 Bottom");
	TestTrue(TEXT("the built name matches the hint's own name"),
		F.Npc->ManBatValidateHintType(Hint));
	// `1038e598 __strcmpi` — case-insensitive over the whole string.
	Hint.Name = TEXT("manbat divepoint 4 BOTTOM");
	TestTrue(TEXT("the compare is case-insensitive"), F.Npc->ManBatValidateHintType(Hint));
	Hint.Name = TEXT("ManBat Divepoint 4 Bottomx");
	TestFalse(TEXT("a longer name does not match"), F.Npc->ManBatValidateHintType(Hint));
	Hint.Name = TEXT("ManBat Divepoint 5 Bottom");
	TestFalse(TEXT("a different index does not match"), F.Npc->ManBatValidateHintType(Hint));
	// The group gate is the BASE body's, not this one's: the ManBat arm never reads `+0x470`.
	Hint.Name = TEXT("ManBat Divepoint 4 Bottom");
	Hint.GroupMask = 0;
	TestTrue(TEXT("the ManBat arm never consults the hint group mask"),
		F.Npc->ManBatValidateHintType(Hint));

	// `1038e556 JNZ` — the EMPTY-template arm never compares strings: it loads the hint's name
	// POINTER into `EAX` and tests it for zero at `1038e5a0`, so an unnamed hint matches and a named
	// one does not. None of the five templates can produce an empty name, so the arm is reachable in
	// retail only through a corrupt table; it is reproduced and asserted here.
	for (uint32 Mode = 0; Mode <= 9; ++Mode)
	{
		TestFalse(FString::Printf(TEXT("mode %u never builds an empty name"), Mode),
			FElysiumNpc::ManBatHintName(Mode, 0).IsEmpty());
	}
	return true;
}

// =================================================================================================
// `0x103d6390` / `0x103d6650` — the end-entity cache and the endpoint.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHints10EndEntityTest,
	"Elysium.Substrate.NpcKernelHints10.HintEndEntity", GHints10TestFlags)
bool FElysiumNpcKernelHints10EndEntityTest::RunTest(const FString&)
{
	FHints10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `103d63xx` — the empty cache falls straight through to `FindHintEndEntity` (`0x103d6520`),
	// which with no hint store and no target name answers the hint's own node id.
	FElysiumNpc::FHintWords Hint = Hints10MakeHint(15000);
	Hint.HintIndex = 11;
	TestEqual(TEXT("an empty cache falls through to FindHintEndEntity, which answers the hint"),
		F.Npc->GetHintEndEntity(Hint), 11);

	// A cache row whose HINT word matches but whose cached handle does not resolve is SKIPPED by the
	// loop guard, not answered — `103d63d5` increments the cursor and keeps walking. With no hint
	// store, `HintWords` never resolves, so every row is skipped and the fallback still runs.
	// The record's layout is the correction this family made: `+0x00` is the cached END ENTITY and
	// `+0x04` is the hint (`103d6779 piVar6 = field_0x6714 + 4`).
	FElysiumNpc::FWerewolfHintGroundpoint Row;
	Row.CachedEndEntity = 5;
	Row.HintNode = 11;
	F.Npc->WerewolfHintGroundpoints.Add(Row);
	TestEqual(TEXT("a cache row whose handle does not resolve is skipped, not answered"),
		F.Npc->GetHintEndEntity(Hint), 11);

	// `103d6650` — a null hint answers `DAT_1070d1b0/b4/b8`, which `staticinit_101370b0` zeroes.
	TestEqual(TEXT("a null hint answers vec3_origin"), F.Npc->GetHintEndpoint(nullptr),
		FVector::ZeroVector);
	// The crash guard: retail dereferences the end entity unchecked. With no hint store the end
	// entity never resolves, so the same value is answered.
	TestEqual(TEXT("an unresolvable end entity answers vec3_origin rather than faulting"),
		F.Npc->GetHintEndpoint(&Hint), FVector::ZeroVector);
	return true;
}

// =================================================================================================
// `0x103d7090` — the forward hint.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHints10ForwardHintTest,
	"Elysium.Substrate.NpcKernelHints10.ForwardHintForHint", GHints10TestFlags)
bool FElysiumNpcKernelHints10ForwardHintTest::RunTest(const FString&)
{
	FHints10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// The switch at `103d70dd`, case for case — FOURTEEN literals.
	const int32 Exempt[] = { 15000, 0x3a99, 0x3a9c, 0x3a9f, 0x3aa0, 0x3aa1,
		0x3aa3, 0x3aa4, 0x3aa5, 0x3aa6, 0x3aa7, 0x3aa8, 0x3aa9, 0x3aaa };
	for (const int32 Type : Exempt)
	{
		TestTrue(FString::Printf(TEXT("0x%x is exempt"), Type),
			FElysiumNpc::IsForwardHintExemptType(Type));
	}
	// The corrected reading: `0x3aa2` is NOT in the jump table, and neither are the three gaps below
	// it. The checklist walk's "0x3a9f-0x3aaa" run would have made all four exempt.
	TestFalse(TEXT("0x3aa2 is NOT exempt — the corrected reading"),
		FElysiumNpc::IsForwardHintExemptType(0x3aa2));
	TestFalse(TEXT("0x3a9a is not exempt"), FElysiumNpc::IsForwardHintExemptType(0x3a9a));
	TestFalse(TEXT("0x3a9b is not exempt"), FElysiumNpc::IsForwardHintExemptType(0x3a9b));
	TestFalse(TEXT("0x3a9d is not exempt"), FElysiumNpc::IsForwardHintExemptType(0x3a9d));
	TestFalse(TEXT("0x3a9e is not exempt"), FElysiumNpc::IsForwardHintExemptType(0x3a9e));

	// `103d70dd` — an exempt hint is handed straight back.
	FElysiumNpc::FHintWords Exempted = Hints10MakeHint(0x3a9c);
	Exempted.HintIndex = 21;
	TestEqual(TEXT("an exempt hint is returned unchanged"),
		F.Npc->GetForwardHintForHint(Exempted), 21);

	// `103d7176` — the global hint list is empty here, so the loop runs to its end and the body
	// answers the null cursor after warning. That is retail's own no-match arm, not a refusal.
	FElysiumNpc::FHintWords Other = Hints10MakeHint(0x3aab);
	Other.HintIndex = 22;
	TestTrue(TEXT("the global hint list seam answers nothing"), F.Npc->GlobalHintList().IsEmpty());
	TestEqual(TEXT("a non-exempt hint with no partner answers null"),
		F.Npc->GetForwardHintForHint(Other), static_cast<int32>(INDEX_NONE));
	return true;
}

// =================================================================================================
// `0x103d8300` — the teleport-hint gate.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelHints10TeleportHintTest,
	"Elysium.Substrate.NpcKernelHints10.IsValidTeleportHint", GHints10TestFlags)
bool FElysiumNpcKernelHints10TeleportHintTest::RunTest(const FString&)
{
	FHints10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	const double Now = F.Npc->World != nullptr ? F.Npc->World->NowSeconds() : 0.0;

	// The exclusion set, `103d83c8`..`103d8430` — six `CMP`s.
	for (int32 Type = 0x3aa3; Type <= 0x3aa8; ++Type)
	{
		TestTrue(FString::Printf(TEXT("0x%x is excluded"), Type),
			FElysiumNpc::IsTeleportHintExcludedType(Type));
	}
	TestFalse(TEXT("0x3aa2 is not excluded"), FElysiumNpc::IsTeleportHintExcludedType(0x3aa2));
	TestFalse(TEXT("0x3aa9 is not excluded"), FElysiumNpc::IsTeleportHintExcludedType(0x3aa9));

	// Gate 1 — a null hint.
	TestFalse(TEXT("a null hint refuses"), F.Npc->IsValidTeleportHint(nullptr, Now));

	// A hint that passes every gate. `0x2774` is the one type the base dispatcher accepts outright,
	// so the slot-566 gate (gate 4) is satisfied without a cover object; the NPC's group mask
	// defaults to every group.
	FElysiumNpc::FHintWords Good = Hints10MakeHint(0x2774);
	Good.HintIndex = 31;
	F.Npc->WerewolfHintFlags = 0;
	TestTrue(TEXT("a hint that passes every gate is valid, because the endpoint seam answers "
				  "'not script-hidden', which is the admitting value"),
		F.Npc->IsValidTeleportHint(&Good, Now));

	// Gate 2 — `field_0x66e8 & 4`.
	F.Npc->WerewolfHintFlags = 0x4;
	TestFalse(TEXT("bit 0x4 of the Werewolf flag word refuses"),
		F.Npc->IsValidTeleportHint(&Good, Now));
	F.Npc->WerewolfHintFlags = 0;

	// Gate 3 — `IsHintUnusable` (`0x102d14c0`), family Hints' three-arm rule. `m_iDisabled` is its
	// first arm.
	FElysiumNpc::FHintWords Disabled = Good;
	Disabled.Disabled = 1;
	TestFalse(TEXT("a disabled hint refuses through IsHintUnusable"),
		F.Npc->IsValidTeleportHint(&Disabled, Now));

	// Gate 4 — slot 566. `0x2773` is not an accepted type.
	FElysiumNpc::FHintWords BadType = Good;
	BadType.HintType = 0x2773;
	TestFalse(TEXT("a type slot 566 refuses refuses here too"),
		F.Npc->IsValidTeleportHint(&BadType, Now));

	// Gate 6 — `hint->+0x470 == 1` AND `field_0x66e8 & 0x40`. Note the EQUALITY compare against 1 on
	// the same word gate 4 treats as a bit set: retail's own asymmetry.
	FElysiumNpc::FHintWords GroupOne = Good;
	GroupOne.GroupMask = 1;
	F.Npc->WerewolfHintFlags = 0x40;
	TestFalse(TEXT("group word 1 with flag 0x40 refuses"),
		F.Npc->IsValidTeleportHint(&GroupOne, Now));
	FElysiumNpc::FHintWords GroupTwo = Good;
	GroupTwo.GroupMask = 2;
	TestTrue(TEXT("group word 2 with the same flag admits — the compare is for equality with 1"),
		F.Npc->IsValidTeleportHint(&GroupTwo, Now));
	F.Npc->WerewolfHintFlags = 0;

	// Gate 7 — the endpoint's `m_bScriptHidden`, NEGATED. `0x100b5190` is a seven-byte getter of
	// `+0xf4`, which the checklist walk calls an "entity-busy/occupied test"; the corrected reading
	// is asserted by the seam answering the ADMITTING value.
	TestFalse(TEXT("the endpoint seam answers 'not script-hidden'"),
		F.Npc->HintEndEntityScriptHidden(31));
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
