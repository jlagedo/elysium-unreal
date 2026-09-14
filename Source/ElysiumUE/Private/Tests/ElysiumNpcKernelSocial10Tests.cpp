#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Social10** — talking, the tweak file and the dialogue packet. Every assertion
// is read off the decompiled C or the listing of the body it names and carries that address.
//
// Six of this family's ten rows are not `FElysiumNpc` bodies (`Substrate/
// ElysiumNpcKernelSocial10.inl` says where each lives and why); they are exercised from here so the
// family's suite covers the family's rows.

static constexpr EAutomationTestFlags GSocial10TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Prefixed because the module builds adaptive-unity and this anonymous namespace is merged with
	// the other suites'.
	struct FSocial10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Npc = nullptr;
		FElysiumPlayer* Player = nullptr;

		FSocial10Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("social10_kernel"), 5252);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					FElysiumEntityDef& G = Builder.AddNpc(TEXT("talker"), FVector::ZeroVector,
						TEXT("npc_VHumanCombatant"));
					// `m_iDialog` (`+0x128`) is the authored `dialogname` key and gate 2 of
					// `CanTalk` (`102c21d0`) refuses without it.
					G.Keys.Add(TEXT("dialogname"), TEXT("social10"));
					return Builder;
				}())
		{
			Npc = World.Npc(TEXT("talker"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Npc });
		}
	};

	// The CP1252 horizontal-ellipsis byte the `.dlg` parser maps 1:1 to a code point.
	const FString Social10Ellipsis = FString::Chr(static_cast<TCHAR>(0x85));
}

// =================================================================================================
// Slot 295 `CanTalk` — `0x102c21c0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSocial10CanTalkTest,
	"Elysium.Substrate.NpcKernelSocial10.CanTalk", GSocial10TestFlags)
bool FElysiumNpcKernelSocial10CanTalkTest::RunTest(const FString&)
{
	FSocial10Fixture F;
	if (F.Npc == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPC or player"));
		return false;
	}

	// Slot 404 decides gate 16; a fixture with no authored relationship rows answers `D_NU`, which
	// is neither `D_HT` nor `D_FR`. Asserted rather than assumed, so a table change does not turn
	// the rest of this case vacuous.
	const int32 Relation = F.Npc->IRelationType(F.Player);
	TestTrue(TEXT("the fixture's relation is neither D_HT nor D_FR (gate 16 admits)"),
		Relation != 1 && Relation != 2);

	TestTrue(TEXT("every gate passing answers 1"), F.Npc->CanTalk(F.Player));

	// 1. `102c21c8` — a null activator.
	TestFalse(TEXT("a null activator refuses"), F.Npc->CanTalk(nullptr));

	// 2. `102c21d0` — `m_iDialog (+0x128)`. CORRECTION: the checklist walk puts it at `+0x5b64`.
	//    Driven through the authored key, which is what fills it.
	{
		const FString Saved = F.Npc->Dialogue.Name(*F.Npc);
		TestFalse(TEXT("the fixture's dialogname is authored"), Saved.IsEmpty());
	}

	// 7. `102c221e` — `m_bWillTalk (+0x1088)`. CORRECTION: the walk puts it at `+0x128`.
	F.Npc->bWillTalk = false;
	TestFalse(TEXT("a body that will not talk refuses"), F.Npc->CanTalk(F.Player));
	F.Npc->bWillTalk = true;
	TestTrue(TEXT("and admits again"), F.Npc->CanTalk(F.Player));

	// 6. `102c220f` — `m_bScriptHidden (+0xf4)` through the seven-byte getter `0x100b5190`.
	F.Npc->bHidden = true;
	TestFalse(TEXT("a script-hidden body refuses"), F.Npc->CanTalk(F.Player));
	F.Npc->bHidden = false;

	// 9. `102c2239` — NO_DIALOG, word ONE's `0x80000`, ALONE.
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::NO_DIALOG);
	TestFalse(TEXT("NO_DIALOG refuses"), F.Npc->CanTalk(F.Player));
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::NO_DIALOG);

	// 14. `102c2272` — NO_DIALOG_PERSISTENT, word TWO's `0x10000000`. A SEPARATE gate five tests
	//     later, which is why the port's `HasDialogSuppressFlag()` (which ORs them) is not what
	//     either gate reads.
	F.Npc->NpcFlags.Set(EElysiumNpcFlag2::NO_DIALOG_PERSISTENT);
	TestFalse(TEXT("NO_DIALOG_PERSISTENT refuses"), F.Npc->CanTalk(F.Player));
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag2::NO_DIALOG_PERSISTENT);

	// 13. `102c2267` — `IsBusyWithDiscipline` (`0x1033e2b0`), the `D_IS_BUSY` bit.
	F.Npc->NpcFlags.Set(EElysiumNpcFlag::D_IS_BUSY);
	TestFalse(TEXT("a body busy with a discipline refuses"), F.Npc->CanTalk(F.Player));
	F.Npc->NpcFlags.Clear(EElysiumNpcFlag::D_IS_BUSY);

	// 10. `102c2245` — `IsInDialog()` (`0x102c1170`).
	F.Npc->Dialogue.bInDialog = true;
	TestTrue(TEXT("IsInDialog reads the open session"), F.Npc->IsInDialog());
	TestFalse(TEXT("a body already in dialog refuses"), F.Npc->CanTalk(F.Player));
	F.Npc->Dialogue.bInDialog = false;

	// The three seams, each asserted to answer the value that does NOT refuse, so nothing is
	// silently blocked.
	TestFalse(TEXT("the activator-controller seam answers not-busy"),
		F.Npc->ActivatorControllerBusy(F.Player));
	TestTrue(TEXT("the observer predicate seam answers the admitting true"),
		F.Npc->CanTalkObserverPredicate(F.Player));
	TestEqual(TEXT("the menu singleton seam answers zero"), F.Npc->DialogMenuBlockWord(), 0);

	TestTrue(TEXT("and the body admits once more"), F.Npc->CanTalk(F.Player));
	return true;
}

// =================================================================================================
// `CAI_BaseNPCTroika::FinishTalking` — `0x102c0ca0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSocial10FinishTalkingTest,
	"Elysium.Substrate.NpcKernelSocial10.FinishTalking", GSocial10TestFlags)
bool FElysiumNpcKernelSocial10FinishTalkingTest::RunTest(const FString&)
{
	FSocial10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	const double Now = F.Npc->World != nullptr ? F.Npc->World->NowSeconds() : 0.0;

	// `102c0923` / `102c092a` — the spoken-line player writes the flag AND the end time.
	F.Npc->OnDialogFilePlayed(5.0);
	TestTrue(TEXT("the spoken-line player raises m_bIsTalking"), F.Npc->bIsTalking);
	TestTrue(TEXT("and stamps the end time"), F.Npc->TalkingUntil > Now);

	F.Npc->Dialogue.DialogQue = TEXT("queued_line");
	const int32 ResetsBefore = F.Npc->ScriptedSoundOverrideResets;
	F.Npc->FinishTalking();

	// `102c0e62`..`102c0e8b`, unconditional.
	TestFalse(TEXT("the dialog partner handle is cleared"), F.Npc->Dialogue.DialogScene.IsSet());
	TestTrue(TEXT("m_szDialogQue[0] is zeroed"), F.Npc->Dialogue.DialogQue.IsEmpty());
	TestFalse(TEXT("m_bIsTalking is cleared"), F.Npc->bIsTalking);
	// `102c0e85 MOV [ESI + 0x64cc],EAX` — a STAMP with curtime, not a clear. This is the recovered
	// fact that made the port declare the flag separately from the stamp.
	TestEqual(TEXT("m_flTalkEnd is stamped with curtime, not cleared"), F.Npc->TalkingUntil,
		F.Npc->World != nullptr ? F.Npc->World->NowSeconds() : 0.0);
	TestEqual(TEXT("ResetScriptedSoundOverrideEnt is asked"),
		F.Npc->ScriptedSoundOverrideResets, ResetsBefore + 1);

	// `102c0d12` / `102c0e92` — the LATCH picks the notify. The flag was SET on entry, so the
	// done-talking notification is the one asked for.
	TestTrue(TEXT("a talking body asks for NPCNotifyDoneTalking"),
		F.Npc->LastFinishTalkingNotify
			== FElysiumNpc::EFinishTalkingNotify::NpcNotifyDoneTalking);

	// And a body that was NOT talking asks for the pending-script flush instead.
	F.Npc->bIsTalking = false;
	F.Npc->FinishTalking();
	TestTrue(TEXT("a quiet body asks for CallPendingNPCEventScript"),
		F.Npc->LastFinishTalkingNotify
			== FElysiumNpc::EFinishTalkingNotify::CallPendingNpcEventScript);

	// `_DAT_104493d0` — 0.1, a DOUBLE, read out of the pinned image (`102c0e29 FADD qword`).
	TestEqual(TEXT("the partner think delay is 0.1"),
		FElysiumNpc::DialogPartnerThinkDelaySeconds, 0.1);
	return true;
}

// =================================================================================================
// Slot 585 `ProcessTweakParam` — `0x1029aa10`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSocial10TweakParamTest,
	"Elysium.Substrate.NpcKernelSocial10.ProcessTweakParam", GSocial10TestFlags)
bool FElysiumNpcKernelSocial10TweakParamTest::RunTest(const FString&)
{
	FSocial10Fixture F;
	if (F.Npc == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpc& N = *F.Npc;
	using EKey = FElysiumNpc::ETweakParamKey;

	// Retail's `Error()` does not return; the port logs at Error level and clamps (the named crash
	// guard). The three reports below are the BODY'S OWN output and are expected here rather than
	// quietened at the source, so the arms that reach them stay observable.
	AddExpectedError(TEXT("You specified an invalid NPCPERCEPTION parameter"),
		EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("You specified a negative VISION parameter"),
		EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("You specified a negative HEARING parameter"),
		EAutomationExpectedErrorFlags::Contains, 0);

	// `__strcmpi`, so the ladder is case-insensitive.
	TestTrue(TEXT("VISION is recognised"), FElysiumNpc::TweakParamKeyOf(TEXT("VISION"))
		== EKey::Vision);
	TestTrue(TEXT("and case-insensitively"), FElysiumNpc::TweakParamKeyOf(TEXT("vision"))
		== EKey::Vision);
	TestTrue(TEXT("an unknown key is Unknown"), FElysiumNpc::TweakParamKeyOf(TEXT("NOPE"))
		== EKey::Unknown);

	// `1029aa31` / `1029aa67` — CAPABILITIES and GOALS are RECOGNISED and still fall into the ignore
	// message, so both print. That is the arm the walk records and the counter makes observable.
	int32 Ignored = N.TweakParamsIgnored;
	N.ProcessTweakParam(TEXT("CAPABILITIES"), TEXT("x"));
	TestEqual(TEXT("CAPABILITIES reaches the ignore message"), N.TweakParamsIgnored, Ignored + 1);
	N.ProcessTweakParam(TEXT("GOALS"), TEXT("x"));
	TestEqual(TEXT("GOALS reaches it too"), N.TweakParamsIgnored, Ignored + 2);
	N.ProcessTweakParam(TEXT("NOPE"), TEXT("x"));
	TestEqual(TEXT("and so does an unrecognised key"), N.TweakParamsIgnored, Ignored + 3);
	N.ProcessTweakParam(TEXT("VISION"), TEXT("500"));
	TestEqual(TEXT("but a recognised acting key does NOT"), N.TweakParamsIgnored, Ignored + 3);

	// `1029aa85`..`1029aadd` — NPCPERCEPTION, with both clamps.
	int32 Recomputes = N.PerceptionRecomputes;
	int32 Errors = N.TweakParamErrors;
	N.ProcessTweakParam(TEXT("NPCPERCEPTION"), TEXT("5"));
	TestEqual(TEXT("a value in band is stored"), N.AuthoredPerception, 5);
	TestEqual(TEXT("no error"), N.TweakParamErrors, Errors);
	TestEqual(TEXT("and both recomputes run"), N.PerceptionRecomputes, Recomputes + 1);
	N.ProcessTweakParam(TEXT("NPCPERCEPTION"), TEXT("0"));
	TestEqual(TEXT("below 1 clamps to 1"), N.AuthoredPerception, 1);
	TestEqual(TEXT("and reports"), N.TweakParamErrors, Errors + 1);
	N.ProcessTweakParam(TEXT("NPCPERCEPTION"), TEXT("11"));
	TestEqual(TEXT("above 10 clamps to 10"), N.AuthoredPerception, 10);
	TestEqual(TEXT("and reports once"), N.TweakParamErrors, Errors + 2);
	// `1029aabd` re-reads the field, so the low clamp's result (1) cannot also trip the high one.
	N.ProcessTweakParam(TEXT("NPCPERCEPTION"), TEXT("-4"));
	TestEqual(TEXT("a clamped-low value is not then clamped high"), N.AuthoredPerception, 1);

	// `1029ab12`..`1029ab66` — VISION. The store is BEFORE the branch, so a refused value still
	// lands, and the two recomputes run on BOTH paths.
	Errors = N.TweakParamErrors;
	Recomputes = N.PerceptionRecomputes;
	N.ProcessTweakParam(TEXT("VISION"), TEXT("-25"));
	TestEqual(TEXT("a negative VISION is still stored"), N.AuthoredVision, -25.f);
	TestEqual(TEXT("and reports"), N.TweakParamErrors, Errors + 1);
	TestEqual(TEXT("and still recomputes"), N.PerceptionRecomputes, Recomputes + 1);
	N.ProcessTweakParam(TEXT("VISION"), TEXT("-1"));
	TestEqual(TEXT("-1 is the derive sentinel, not an error"), N.AuthoredVision, -1.f);
	TestEqual(TEXT("so no second report"), N.TweakParamErrors, Errors + 1);
	TestEqual(TEXT("and it recomputes too"), N.PerceptionRecomputes, Recomputes + 2);
	N.ProcessTweakParam(TEXT("VISION"), TEXT("0"));
	TestEqual(TEXT("zero is not below the floor"), N.AuthoredVision, 0.f);
	TestEqual(TEXT("and is not an error"), N.TweakParamErrors, Errors + 1);

	// `1029ab83`..`1029abd2` — HEARING, the identical shape at `+0x63bc`.
	Errors = N.TweakParamErrors;
	N.ProcessTweakParam(TEXT("HEARING"), TEXT("2.5"));
	TestEqual(TEXT("HEARING stores the scalar"), N.AuthoredHearing, 2.5f);
	TestEqual(TEXT("no error"), N.TweakParamErrors, Errors);
	N.ProcessTweakParam(TEXT("HEARING"), TEXT("-2"));
	TestEqual(TEXT("a negative HEARING is still stored"), N.AuthoredHearing, -2.f);
	TestEqual(TEXT("and reports"), N.TweakParamErrors, Errors + 1);

	// The two recovered `.rdata` cells, read out of the pinned image.
	TestEqual(TEXT("_DAT_104454c4 is 0.0f"), FElysiumNpc::TweakParamNegativeFloor, 0.f);
	TestEqual(TEXT("_DAT_104492dc is -1.0f"), FElysiumNpc::TweakParamDeriveSentinel, -1.f);

	// `1029ac4c` / `1029ac28` — the two group-list parsers, which are the bodies the port already
	// carries. Bit 3 is group id 4.
	N.ProcessTweakParam(TEXT("HINTGROUPS"), TEXT("4"));
	TestEqual(TEXT("HINTGROUPS parses through 0x102989e0"),
		static_cast<int64>(N.ScheduleHost.HintGroupMask), static_cast<int64>(1u << 3));
	N.ProcessTweakParam(TEXT("IPGROUPS"), TEXT("2 5"));
	TestEqual(TEXT("IPGROUPS parses through 0x10298910"),
		static_cast<int64>(N.InterestingPlaceGroupMask),
		static_cast<int64>((1u << 1) | (1u << 4)));

	// `1029ac6e` — TPMOVETIMER is an ABSOLUTE curtime.
	const double NowSeconds = N.World != nullptr ? N.World->NowSeconds() : 0.0;
	N.ProcessTweakParam(TEXT("TPMOVETIMER"), TEXT("3"));
	TestEqual(TEXT("TPMOVETIMER is curtime plus the value"), N.TeleportMoveTimer,
		static_cast<float>(NowSeconds + 3.0), 0.001f);

	// `1029aca2` / `1029acd6` — the two `atoi != 0` bytes.
	N.ProcessTweakParam(TEXT("IGNOREATTACK"), TEXT("1"));
	TestTrue(TEXT("IGNOREATTACK sets the byte"), N.bIgnoreDetectedAttack);
	N.ProcessTweakParam(TEXT("IGNOREATTACK"), TEXT("0"));
	TestFalse(TEXT("and clears it"), N.bIgnoreDetectedAttack);
	N.ProcessTweakParam(TEXT("NOALERTSTATE"), TEXT("7"));
	TestTrue(TEXT("NOALERTSTATE is any non-zero"), N.bNoAlertState);
	N.ProcessTweakParam(TEXT("NOALERTSTATE"), TEXT("nope"));
	TestFalse(TEXT("and atoi of a non-number is 0"), N.bNoAlertState);
	return true;
}

// =================================================================================================
// `CDialog::process_npc_line` (`0x100e8100`) — the two missing arms.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSocial10EllipsisTest,
	"Elysium.Substrate.NpcKernelSocial10.EllipsisNormaliser", GSocial10TestFlags)
bool FElysiumNpcKernelSocial10EllipsisTest::RunTest(const FString&)
{
	auto Norm = [](const FString& In) { return ElysiumDlgRetail::NormaliseEllipses(In); };

	// Pass 1 — `" . . . "` -> `" ... "` (`0x10561b3c` -> `0x10561b14`).
	TestEqual(TEXT("pass 1"), Norm(TEXT("well . . . then")), FString(TEXT("well ... then")));

	// Pass 2 — `". . . "` -> `"... "` (`0x10561b34` -> `0x10561b0c`). Reached only when pass 1
	// could not match, which is why the needle carries no leading space.
	TestEqual(TEXT("pass 2"), Norm(TEXT("well. . . then")), FString(TEXT("well... then")));

	// Pass 3 — `" . . ."` -> `" ... "`. **The replacement carries a TRAILING SPACE**: `0x10561aa8`
	// holds `0x10561b14`, the same pointer pass 1 uses, so a six-character needle is replaced by a
	// five-character string that is not its prefix. The checklist walk reads this as `" ..."`.
	TestEqual(TEXT("pass 3 adds a trailing space"), Norm(TEXT("well . . .")),
		FString(TEXT("well ... ")));

	// Pass 4 — `". . ."` -> `"..."` (`0x10561b24` -> `0x10561b08`).
	TestEqual(TEXT("pass 4"), Norm(TEXT("well. . .then")), FString(TEXT("well...then")));

	// Pass 5 — CP1252 `0x85` followed by a space -> `"... "` (`0x10561b20` -> `0x10561b0c`).
	TestEqual(TEXT("pass 5, the CP1252 ellipsis byte with a space"),
		Norm(TEXT("well") + Social10Ellipsis + TEXT(" then")), FString(TEXT("well... then")));

	// Pass 6 — a BARE `0x85` -> `"... "` (`0x10561b1c` -> `0x10561b0c`), which ADDS a space where
	// there was none. A one-character needle replaced by a four-character string that ends in a
	// space is retail's own table and is reproduced: the replacement pointer at `0x10561ab4` is
	// `0x10561b0c`, the same `"... "` pass 5 uses.
	TestEqual(TEXT("pass 6, a bare CP1252 ellipsis byte gains a space"),
		Norm(TEXT("well") + Social10Ellipsis + TEXT("then")), FString(TEXT("well... then")));
	// With nothing after it, the added space survives.
	TestEqual(TEXT("and at the end of the line"),
		Norm(TEXT("well") + Social10Ellipsis), FString(TEXT("well... ")));

	// The ORDER matters: pass 5 consumes every `0x85 ` before pass 6 sees a bare one, so the two
	// never both fire on the same byte.
	TestEqual(TEXT("pass 5 runs before pass 6"),
		Norm(Social10Ellipsis + TEXT(" ") + Social10Ellipsis), FString(TEXT("... ... ")));

	// Each pass is replace-UNTIL-NO-MATCH, not replace-once.
	TestEqual(TEXT("a pass repeats until no match"), Norm(TEXT("a . . . b . . . c")),
		FString(TEXT("a ... b ... c")));

	// A string with nothing to normalise comes back untouched.
	TestEqual(TEXT("an ordinary line is unchanged"), Norm(TEXT("Just a line.")),
		FString(TEXT("Just a line.")));
	TestEqual(TEXT("an already-normalised ellipsis is unchanged"), Norm(TEXT("well ... then")),
		FString(TEXT("well ... then")));

	// `0x100e7f70` on its own — replace the FIRST occurrence and report whether one happened.
	FString Buffer = TEXT("aXbXc");
	TestTrue(TEXT("the first occurrence is replaced"),
		ElysiumDlgRetail::ReplaceFirst(Buffer, TEXT("X"), TEXT("-"), 0x800));
	TestEqual(TEXT("and only the first"), Buffer, FString(TEXT("a-bXc")));
	TestFalse(TEXT("a miss reports false and changes nothing"),
		ElysiumDlgRetail::ReplaceFirst(Buffer, TEXT("Z"), TEXT("-"), 0x800));
	TestEqual(TEXT("untouched"), Buffer, FString(TEXT("a-bXc")));
	// `Q_strncpy(param_1, scratch, param_2)` is a TRUNCATION at the buffer size.
	FString Small = TEXT("abcX");
	TestTrue(TEXT("a replacement that overruns the buffer still happens"),
		ElysiumDlgRetail::ReplaceFirst(Small, TEXT("X"), TEXT("YYYY"), 5));
	TestEqual(TEXT("and is truncated to size-1"), Small, FString(TEXT("abcY")));

	// `0x1054ca50` — the row-fetch miss default, read out of the pinned image: a single space.
	TestEqual(TEXT("the missing-line default is one space"),
		FString(ElysiumDlgRetail::MissingNpcLineText()), FString(TEXT(" ")));
	return true;
}

// =================================================================================================
// `CDialog::process_pc_line` (`0x100e8520`) and `CDialog::fill_packet` (`0x100e7da0`).
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSocial10PacketTest,
	"Elysium.Substrate.NpcKernelSocial10.DialoguePacket", GSocial10TestFlags)
bool FElysiumNpcKernelSocial10PacketTest::RunTest(const FString&)
{
	using ElysiumDlgRetail::ClassifyPcRow;
	using ElysiumDlgRetail::EPcRowFlag;
	const uint32 AutoEndSpoken = static_cast<uint32>(EPcRowFlag::AutoEndSpoken);
	const uint32 AutoLink = static_cast<uint32>(EPcRowFlag::AutoLink);

	// An ordinary response row: `local_38` is its initial 1 and no flag bit is set.
	{
		const auto R = ClassifyPcRow(false, false, false, false, true, true);
		TestEqual(TEXT("an ordinary row answers 1"), R.Return, 1);
		TestEqual(TEXT("with no flag bits"), static_cast<int64>(R.Flags), static_cast<int64>(0));
	}
	// `100e8600` — Auto-End with a speech file: bit 0x10, answer -1.
	{
		const auto R = ClassifyPcRow(true, false, false, false, true, true);
		TestEqual(TEXT("an Auto-End with a speech file answers -1"), R.Return, -1);
		TestEqual(TEXT("and sets bit 0x10"), static_cast<int64>(R.Flags),
			static_cast<int64>(AutoEndSpoken));
		TestFalse(TEXT("and does not collapse the band"), R.bCollapseBand);
	}
	// `100e85e0` — Auto-End with NO speech file: the band collapses and the row answers 1.
	{
		const auto R = ClassifyPcRow(true, false, false, false, true, false);
		TestEqual(TEXT("an Auto-End with no speech file answers 1"), R.Return, 1);
		TestTrue(TEXT("and collapses the band"), R.bCollapseBand);
		TestEqual(TEXT("with no flag bits"), static_cast<int64>(R.Flags), static_cast<int64>(0));
	}
	// Both automatic arms are gated on the packet's 0x30 test being CLEAR and on the dependency.
	{
		const auto Blocked = ClassifyPcRow(true, false, false, true, true, true);
		TestEqual(TEXT("the 0x30 packet flag blocks the Auto-End arm"), Blocked.Return, 1);
		TestEqual(TEXT("and no bit is set"), static_cast<int64>(Blocked.Flags),
			static_cast<int64>(0));
		const auto Failed = ClassifyPcRow(true, false, false, false, false, true);
		TestEqual(TEXT("a failing dependency blocks it too"), Failed.Return, 1);
	}
	// `100e866a` — Auto-Link: bit 0x20, answer 1, the row STAYS.
	{
		const auto R = ClassifyPcRow(false, true, false, false, true, true);
		TestEqual(TEXT("an Auto-Link answers 1"), R.Return, 1);
		TestEqual(TEXT("and sets bit 0x20"), static_cast<int64>(R.Flags),
			static_cast<int64>(AutoLink));
		const auto Blocked = ClassifyPcRow(false, true, false, true, true, true);
		TestEqual(TEXT("a blocked Auto-Link still answers 1"), Blocked.Return, 1);
		TestEqual(TEXT("with no bit"), static_cast<int64>(Blocked.Flags), static_cast<int64>(0));
	}
	// `100e8687` — a starting-condition row answers 0, which DROPS it without raising `+0x30e9`.
	{
		const auto R = ClassifyPcRow(false, false, true, false, true, true);
		TestEqual(TEXT("a starting-condition row answers 0"), R.Return, 0);
	}

	// `0x100e7da0`'s band arithmetic.
	using ElysiumDlgRetail::FillPacketBand;
	{
		const auto B = FillPacketBand({ 1, 1, 1 });
		TestEqual(TEXT("three surviving rows"), B.Count, 3);
		TestFalse(TEXT("no auto-terminate"), B.bAutoTerminate);
		TestFalse(TEXT("no fallback"), B.bNoValidReply);
	}
	{
		const auto B = FillPacketBand({ 1, 0, 1 });
		TestEqual(TEXT("a 0 answer drops the row"), B.Count, 2);
		TestFalse(TEXT("without raising the flag"), B.bAutoTerminate);
	}
	{
		const auto B = FillPacketBand({ 1, -1 });
		TestEqual(TEXT("a -1 answer drops the row too"), B.Count, 1);
		TestTrue(TEXT("and raises the auto-terminate flag"), B.bAutoTerminate);
		TestFalse(TEXT("so the fallback does not fire"), B.bNoValidReply);
	}
	{
		const auto B = FillPacketBand({ -1 });
		TestEqual(TEXT("the only row dropped by -1 leaves a zero count"), B.Count, 0);
		TestTrue(TEXT("but the flag is up"), B.bAutoTerminate);
		TestFalse(TEXT("so retail does NOT substitute the line"), B.bNoValidReply);
	}
	{
		const auto B = FillPacketBand({ 0, 0 });
		TestTrue(TEXT("every row gated out with the flag clear fires the fallback"),
			B.bNoValidReply);
		TestEqual(TEXT("and forces the count to 1"), B.Count, 1);
	}
	{
		// The divergence this family closed: retail's fallback has NO "there was at least one
		// authored row" gate. `get_pc_responses` answers 0, the drop loop never runs, the count is
		// zero and the substitution fires.
		const auto B = FillPacketBand({});
		TestTrue(TEXT("an NPC line with NO authored PC rows fires the fallback too"),
			B.bNoValidReply);
		TestEqual(TEXT("and forces the count to 1"), B.Count, 1);
	}
	TestEqual(TEXT("the substituted literal"), FString(ElysiumDlgRetail::NoValidReplyLiteral()),
		FString(TEXT("I do not have a valid reply.")));
	return true;
}

// =================================================================================================
// `0x1017c600` and `0x10183120` — the two `CBasePlayer` bodies.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSocial10PlayerBodiesTest,
	"Elysium.Substrate.NpcKernelSocial10.PlayerBodies", GSocial10TestFlags)
bool FElysiumNpcKernelSocial10PlayerBodiesTest::RunTest(const FString&)
{
	FSocial10Fixture F;
	if (F.Player == nullptr || F.Npc == nullptr)
	{
		AddError(TEXT("no player or NPC"));
		return false;
	}
	FElysiumPlayer& P = *F.Player;

	// `0x1017c600` — the barter arm.
	P.OpenBarterOrLoot(F.Npc, /*bLoot=*/false, 1, 2, 3);
	TestEqual(TEXT("the target's index is recorded (+0x1eb8)"), P.BarterTarget, F.Npc->Handle);
	TestFalse(TEXT("the loot byte is clear (+0x1ec0)"), P.bBarterTargetIsLoot);
	TestEqual(TEXT("the vendor sync is asked once"), P.VendorInventorySyncs, 1);
	TestEqual(TEXT("the corpse-loot build is not"), P.CorpseLootBuilds, 0);
	// The command carries retail's trailing newline: `0x10587ef4` is `"showbarter\n"`.
	TestEqual(TEXT("and the console command is showbarter"), P.LastTradeWindowCommand,
		FString(TEXT("showbarter\n")));

	// The loot arm, which does NOT pass the three extra arguments.
	P.OpenBarterOrLoot(F.Npc, /*bLoot=*/true, 1, 2, 3);
	TestTrue(TEXT("the loot byte is set"), P.bBarterTargetIsLoot);
	TestEqual(TEXT("the corpse-loot build is asked"), P.CorpseLootBuilds, 1);
	TestEqual(TEXT("and the vendor sync is not asked again"), P.VendorInventorySyncs, 1);
	TestEqual(TEXT("and the console command is showloot"), P.LastTradeWindowCommand,
		FString(TEXT("showloot\n")));

	// `0x10183120` — `1018312a CMP EAX,[ESP + 0x28] / JZ` — pointer identity is a no-op guard.
	for (int32 i = 0; i < FElysiumPlayer::ClientDisciplineDurationCount; ++i)
	{
		P.ClientDisciplineDurations[i] = 10.f;
	}
	const int32 ProbesBefore = P.ClientDisciplineDurationProbes;
	int32 Anchor = 0;
	P.RescaleActiveDisciplineDurations(&Anchor, &Anchor, 2.f, 1.f);
	TestEqual(TEXT("pointer-equal arguments skip the whole body"),
		P.ClientDisciplineDurationProbes, ProbesBefore);
	TestEqual(TEXT("and no slot is touched"), P.ClientDisciplineDurations[0], 10.f);

	// Distinct arguments run all seventeen slots. The record seam answers nothing, so nothing is
	// rewritten — which is the recovered walk with no producer behind it, not a refusal.
	int32 Other = 0;
	P.RescaleActiveDisciplineDurations(&Anchor, &Other, 2.f, 1.f);
	TestEqual(TEXT("seventeen slots are probed"), P.ClientDisciplineDurationProbes,
		ProbesBefore + FElysiumPlayer::ClientDisciplineDurationCount);
	TestEqual(TEXT("the array is retail's seventeen wide"),
		FElysiumPlayer::ClientDisciplineDurationCount, 17);
	TestEqual(TEXT("and with no record nothing is rewritten"), P.ClientDisciplineDurations[0],
		10.f);
	// `1018321a` — the bounds seam admits, which is the value that lets the write happen.
	TestTrue(TEXT("the bounds seam answers the admitting true"),
		P.ClientDisciplineDurationInBounds(0.f));
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
