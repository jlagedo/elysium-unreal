#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Dialogue** — one case per ported body. Every threshold, arm order and
// written value below comes from the decompiled C (and from `corpus asm` where the decompiler
// reported DAMAGED, which it does on `0x100e58e0`), not from 29c's one-line walks — which this
// family found wrong in four places, each asserted here by name:
//
//   * `0x102a0d20` re-arms at `curtime + 1.0`, not 5 s.
//   * `0x10161a70` copies the owner's MODEL NAME, not its classname, and stamps 4096.0 into
//     `+0x63b4 m_flSeekDistBase`.
//   * `0x100e4840`'s fallback returns the first record's ID field, not its goto-line.
//   * `0x101aaee0`'s third arm is `m_bScriptHidden`, not a liveness test.
//
// The brief's "Two tables, and they disagree", obeyed: `npc_payphone` and `npc_VSabbatLeader` are
// BOTH census classnames and registered spawn leaves, so they get real bodies. `npc_VCameraSecurity`
// is a census classname and NOT a spawn leaf, so its rows are exercised by retail class name through
// the census and its behaviour by calling the ported method on an ordinary NPC.

static constexpr EAutomationTestFlags GElysiumNpcKernelDialogueFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The world every case below stands: an ordinary combatant, a second entity to act as an
	// activator/camera, a payphone and a Sabbat leader (both spawn leaves AND census classnames).
	struct FDialogueFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumNpc* Phone = nullptr;
		FElysiumNpc* Sabbat = nullptr;

		FDialogueFixture()
			: World([]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("dialogue_kernel"), 29104u);
				Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
				Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f));
				FElysiumEntityDef& PhoneDef = Builder.AddNpc(TEXT("phone"),
					FVector(0.f, 400.f, 0.f), TEXT("npc_payphone"));
				PhoneDef.Keys.Add(TEXT("dialogname"), TEXT("payphone"));
				Builder.AddNpc(TEXT("sabbat"), FVector(800.f, 0.f, 0.f), TEXT("npc_VSabbatLeader"));
				return Builder;
			}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Phone = World.Npc(TEXT("phone"));
			Sabbat = World.Npc(TEXT("sabbat"));
			FElysiumNpcWorldFixture::Quiet({ Guard, Other, Phone, Sabbat });
		}
	};
}

// =================================================================================================
// `CDialog::message_send` (`0x100e58e0`) and `SetDialogQue` (`0x102c0470`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueMessageSendTest,
	"Elysium.Substrate.NpcKernelDialogue.MessageSend", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueMessageSendTest::RunTest(const FString&)
{
	FDialogueFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }

	// Two shown responses (`+0x2834` = 2) and the page retail's caller fills.
	F.Guard->DialogPcLines.SetNum(2);
	F.Guard->DialogRecipient = F.Other->Handle;
	F.Guard->DialogSpeaker = F.Other->Handle;

	FElysiumNpc::FDialogResponsePage Page;
	Page.NpcText = TEXT("So you are the new one.");
	Page.ResponseText = { TEXT("Who are you?"), TEXT("Nothing.") };
	Page.ResponseWordA = { 11, 22 };
	Page.ResponseWordB = { 33, 44 };

	// --- The NO-REPLY arm: `LookupSpeechFile` answers nothing ------------------------------------
	// The seam is empty by default, which is retail's `speech == NULL`: choices SHOWN, history
	// window shown, whisper drained. Note the polarity — a line WITH a voice hides the choices.
	F.Guard->DialogSpeechFile.Empty();
	F.Guard->DialogMessageSend(Page);

	// Step 2 / 7 / 8 — three user messages, in order, with retail's opcodes 0, 2, 3.
	if (!TestEqual(TEXT("0x100e58e0: three user messages"), F.Guard->DialogUserMessages.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("message 0 carries the response count (+0x2834)"),
		F.Guard->DialogUserMessages[0], FString(TEXT("0:2")));
	// The two int loops run 0..N-1 — array A whole, then array B whole.
	TestEqual(TEXT("message 2 is array A then array B, both 0..N-1"),
		F.Guard->DialogUserMessages[1], FString(TEXT("2:11,22,33,44,")));
	TestEqual(TEXT("message 3 closes and carries nothing"),
		F.Guard->DialogUserMessages[2], FString(TEXT("3:")));

	// Step 4 / 6 — the text loop runs 1..N INCLUSIVE, so N+1 appends counting line 0.
	const TArray<FString>& Ui = F.Guard->DialogUiCalls;
	if (!TestEqual(TEXT("filter + 3 appends + 2 UI calls"), Ui.Num(), 6)) { return false; }
	TestEqual(TEXT("the recipient filter is built from CDialog+0x04"), Ui[0],
		FString(TEXT("RecipientFilter(other)")));
	TestEqual(TEXT("append 0 is the NPC line"), Ui[1],
		FString(TEXT("append 0 So you are the new one.")));
	TestEqual(TEXT("no-reply arm shows the choices"), Ui[2], FString(TEXT("ShowPlayerChoices 1")));
	TestEqual(TEXT("no-reply arm shows the history window"), Ui[3],
		FString(TEXT("ShowHistoryWindow 1")));
	TestEqual(TEXT("append 1 is response 1"), Ui[4], FString(TEXT("append 1 Who are you?")));
	TestEqual(TEXT("the appends run 1..N inclusive, so append N is the last"), Ui[5],
		FString(TEXT("append 2 Nothing.")));

	// --- The HAS-REPLY arm ------------------------------------------------------------------------
	F.Guard->DialogUserMessages.Reset();
	F.Guard->DialogUiCalls.Reset();
	F.Guard->DialogSpeechFile = TEXT("jack/jack_001.wav");
	// `0x100e58b0`: the line id is non-zero and the latch is clear, so the queue is NOT final.
	F.Guard->DialogCurrentLineId = 7;
	F.Guard->bDialogFinalLatch = false;
	F.Guard->DialogMessageSend(Page);

	TestEqual(TEXT("0x102c0470: the SPEAKER's m_szDialogQue (+0x64ec) takes the speech file"),
		F.Other->Dialogue.DialogQue, FString(TEXT("jack/jack_001.wav")));
	TestFalse(TEXT("0x102c0470: m_bDialogQueIsFinal (+0x654c) takes 0x100e58b0's answer"),
		F.Other->Dialogue.bDialogQueIsFinal);
	TestTrue(TEXT("has-reply arm HIDES the choices"),
		F.Guard->DialogUiCalls.Contains(TEXT("ShowPlayerChoices 0")));
	TestFalse(TEXT("has-reply arm shows no history window"),
		F.Guard->DialogUiCalls.Contains(TEXT("ShowHistoryWindow 1")));
	TestEqual(TEXT("the three user messages are sent on both arms alike"),
		F.Guard->DialogUserMessages.Num(), 3);

	// `0x100e58b0`, both terms. The latch short-circuits; otherwise the line id being 0 is final.
	TestFalse(TEXT("0x100e58b0: a live line with a clear latch is not final"),
		F.Guard->DialogQueIsFinal());
	F.Guard->DialogCurrentLineId = 0;
	TestTrue(TEXT("0x100e58b0: line id 0 is final"), F.Guard->DialogQueIsFinal());
	F.Guard->DialogCurrentLineId = 7;
	F.Guard->bDialogFinalLatch = true;
	TestTrue(TEXT("0x100e58b0: the +0x30e9 latch short-circuits the line id"),
		F.Guard->DialogQueIsFinal());

	// The 0x60 `Q_strncpy` truncation is retail's and is not a refusal.
	F.Guard->SetDialogQue(FString::ChrN(200, TEXT('x')), true);
	TestEqual(TEXT("0x102c0470: m_szDialogQue is char[0x60], so 0x5f characters survive"),
		F.Guard->Dialogue.DialogQue.Len(), 0x5f);
	TestTrue(TEXT("0x102c0470 writes the final byte"), F.Guard->Dialogue.bDialogQueIsFinal);
	return true;
}

// =================================================================================================
// `CDialog::goto_line_for_response` (`0x100e4840`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueGotoLineTest,
	"Elysium.Substrate.NpcKernelDialogue.GotoLineForResponse", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueGotoLineTest::RunTest(const FString&)
{
	FDialogueFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }

	// `Msg(3, "Could not find response for play…")` — the fallback arm's own console line, which
	// several cases below deliberately reach.
	AddExpectedError(TEXT("Could not find response"), EAutomationExpectedErrorFlags::Contains, 0);

	// Arm 1 — no node (`CDialog+0x08 == NULL`). Nothing else is consulted.
	F.Guard->bDialogNodeBound = false;
	F.Guard->DialogCurrentLineId = 9;
	TestEqual(TEXT("0x100e4840: no node answers 0"), F.Guard->DialogGotoLineForResponse(0), 0);

	// Arm 2 — `+0x2830 <= 0`. The gate is "a line is being spoken", NOT a response count; the same
	// word `LookupSpeechFile` and `0x100e58b0` read.
	F.Guard->bDialogNodeBound = true;
	F.Guard->DialogCurrentLineId = 0;
	TestEqual(TEXT("0x100e4840: no current line id answers 0"),
		F.Guard->DialogGotoLineForResponse(0), 0);

	// The node's response table: three 0x34-byte records, of which the body touches +0x00 (the id)
	// and +0x0c (the goto-line).
	F.Guard->DialogCurrentLineId = 9;
	F.Guard->DialogNodeResponses = { { 100, 501 }, { 200, 502 }, { 300, 503 } };
	F.Guard->DialogResponseTargetIds = { 200, 0, 999 };

	// The hit path returns record `+0x0c`.
	TestEqual(TEXT("0x100e4840: a matching id returns that record's goto-line (+0x0c)"),
		F.Guard->DialogGotoLineForResponse(0), 502);

	// A ZERO target id takes the NO-NODE return, not the warning arm — the `goto` in the C jumps
	// past `Msg(3, ...)` to the outer `return 0`.
	TestEqual(TEXT("0x100e4840: a zero target id answers 0, not the fallback"),
		F.Guard->DialogGotoLineForResponse(1), 0);

	// The no-match fallback returns the FIRST record's `+0x00` — the ID field, a different word
	// from the hit path's `+0x0c`. 29c's walk called both "goto-line"; retail reads two fields.
	TestEqual(TEXT("0x100e4840: no match falls back to responses[0].Id (+0x00), NOT its goto-line"),
		F.Guard->DialogGotoLineForResponse(2), 100);
	TestNotEqual(TEXT("...and 100 is deliberately not 501, the first record's goto-line"),
		F.Guard->DialogGotoLineForResponse(2), 501);

	// A negative index skips the lookup entirely and lands on the same fallback.
	TestEqual(TEXT("0x100e4840: a negative index takes the fallback"),
		F.Guard->DialogGotoLineForResponse(-1), 100);

	// M-DLG-BOUNDS: retail does not bounds-check the index against `+0x2834` and reads past the
	// array; the port refuses and takes the fallback.
	TestEqual(TEXT("M-DLG-BOUNDS: an out-of-range index takes the fallback instead of reading past"),
		F.Guard->DialogGotoLineForResponse(50), 100);

	// M-DLG-EMPTY: retail dereferences `pResponses[0]` unconditionally; the port answers 0.
	F.Guard->DialogNodeResponses.Reset();
	TestEqual(TEXT("M-DLG-EMPTY: an empty response table answers 0 rather than reading wild"),
		F.Guard->DialogGotoLineForResponse(0), 0);
	return true;
}

// =================================================================================================
// The `CBasePlayer` half — `0x1017cf90`, `0x10161a70`, `0x10175180`, `0x1017f770`, `0x1017f8b0`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueCineCameraTest,
	"Elysium.Substrate.NpcKernelDialogue.ActiveCamera", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueCineCameraTest::RunTest(const FString&)
{
	FDialogueFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }

	// Nothing adopted: `m_iCameraOverrideIdx` (+0x1ec4) is 0, which fails the STRICTLY-positive
	// first term before the handle is ever looked at.
	TestNull(TEXT("0x1017cf90: no adoption answers null"), F.Guard->GetActiveCameraEntity());

	// `SetCineCamera` (`0x1017cef0`) is this pair's only writer.
	F.World.World.SetCineCamera(F.Other->Handle, 41, false, TEXT("shot"));
	TestTrue(TEXT("0x1017cf90: an adopted camera resolves to that entity"),
		F.Guard->GetActiveCameraEntity() == static_cast<FElysiumEntity*>(F.Other));

	// An entity-less adopter holds the slot with a shot id and a dead `+0x19b4`: retail's
	// `+0x1ec4 > 0` with an unresolvable handle, which answers null rather than refusing.
	F.World.World.SetCineCamera(FElysiumEntityHandle::Invalid(), 41, false, TEXT("shot"));
	TestNull(TEXT("0x1017cf90: an entity-less adopter answers null"),
		F.Guard->GetActiveCameraEntity());

	// `SetCineCamera(NULL)` clears both words.
	F.World.World.SetCineCamera(FElysiumEntityHandle::Invalid(), 0, false, FString());
	TestNull(TEXT("0x1017cf90: after SetCineCamera(NULL) there is nothing to answer"),
		F.Guard->GetActiveCameraEntity());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueControllerNpcTest,
	"Elysium.Substrate.NpcKernelDialogue.ControllerNpc", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueControllerNpcTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture World([]
	{
		FElysiumNpcWorldBuilder Builder(TEXT("dialogue_controller"), 29105u);
		Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
		// `npc_VPlayerController` IS a registered classname (`Substrate/ElysiumNpcClasses.cpp`),
		// but its leaf is `FElysiumPlayerControllerNpc`, not `FElysiumNpc` — so it is fetched by
		// name rather than through `Npc()`.
		Builder.AddNpc(TEXT("controller"), FVector(10.f, 0.f, 0.f), TEXT("npc_VPlayerController"));
		return Builder;
	}());
	FElysiumNpc* Guard = World.Npc(TEXT("guard"));
	FElysiumEntity* Controller = World.World.FindByName(TEXT("controller"));
	if (!TestNotNull(TEXT("guard spawned"), Guard)) { return false; }
	if (!TestNotNull(TEXT("controller spawned"), Controller)) { return false; }
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// STEP 1, the cache HIT: the cached entity's classname matches (`__strcmpi`, so case does not
	// matter) and the body returns it without creating anything.
	Guard->ControllerNpc = Controller->Handle;
	TestTrue(TEXT("0x10161a70: a matching cached class is returned unchanged"),
		Guard->GetControllerNpc(TEXT("NPC_VPLAYERCONTROLLER")) == Controller);
	TestTrue(TEXT("...and m_hControllerNPC (+0x1db0) is untouched"),
		Guard->ControllerNpc == Controller->Handle);

	// STEP 1, the MISMATCH: `Warning(...)`, `ReleaseControllerNpc(false, false)` — which clears
	// `+0x1db0` — and then STEP 2 always creates.
	AddExpectedError(TEXT("GetControllerNPC"), EAutomationExpectedErrorFlags::Contains, 0);
	Guard->ControllerNpc = Controller->Handle;
	FElysiumEntity* Answer = Guard->GetControllerNpc(TEXT("npc_VHumanCombatant"));

	// STEP 2's seam answers null, which is retail's OWN "created NULL Entity" arm: the warning is
	// emitted and `m_hControllerNPC` is cleared to -1. The arm is taken, not skipped.
	TestNull(TEXT("0x10161a70: the create seam answers nothing, so the null-entity arm runs"),
		Answer);
	TestFalse(TEXT("...and that arm clears m_hControllerNPC (+0x1db0) to -1"),
		Guard->ControllerNpc.IsSet());

	// STEP 2 with no cache at all reaches the same arm.
	Guard->ControllerNpc = FElysiumEntityHandle::Invalid();
	TestNull(TEXT("0x10161a70: no cache also reaches the create seam"),
		Guard->GetControllerNpc(TEXT("npc_VPlayerController")));

	// `0x10175180` — the predicate over the SAME word. `+0x1db0` is `m_hControllerNPC`, not a
	// dialogue partner; 29c's walk is corrected here by name.
	TestFalse(TEXT("0x10175180: no controller is not busy"), Guard->ControllerNpcBusy());
	Guard->ControllerNpc = Controller->Handle;
	// SEAM: slot 138 `Classify()` has no port body, so nothing can answer 3 and the refusal is the
	// recovered one — the seam IS asked and answers not-busy.
	TestFalse(TEXT("0x10175180: a live controller answers not-busy — Classify() is a seam"),
		Guard->ControllerNpcBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialoguePursuitCountsTest,
	"Elysium.Substrate.NpcKernelDialogue.PursuitCounts", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialoguePursuitCountsTest::RunTest(const FString&)
{
	FDialogueFixture F;
	FElysiumPlayer* Player = F.World.Player();
	if (!TestNotNull(TEXT("player spawned"), Player)) { return false; }

	// `0x1017f770` and `0x1017f8b0` are seven-byte getters of two DIFFERENT words, both already
	// carried by this runtime's `Police` record.
	Player->Police.CopsInPursuit = 0;
	Player->Police.HuntersInPursuit = 0;
	TestEqual(TEXT("0x1017f770 reads +0x1d10 m_iCopsInPursuitCount"),
		Player->DialogThreatCount(), 0);
	TestEqual(TEXT("0x1017f8b0 reads +0x1d14 m_iHuntersInPursuitCount"),
		Player->DialogHunterThreatCount(), 0);
	TestNull(TEXT("neither count blocks a conversation at zero"), Player->DialogRefusalReason());

	// `FUN_10178170` tests them as two arms in order, cops first.
	Player->Police.CopsInPursuit = 3;
	TestEqual(TEXT("0x1017f770 answers the live count"), Player->DialogThreatCount(), 3);
	TestEqual(TEXT("the cop arm is the FIRST of the two and names itself"),
		FString(Player->DialogRefusalReason()), FString(TEXT("the police are still in pursuit")));

	Player->Police.CopsInPursuit = 0;
	Player->Police.HuntersInPursuit = 1;
	TestEqual(TEXT("0x1017f8b0 answers the live count"), Player->DialogHunterThreatCount(), 1);
	TestEqual(TEXT("the hunter arm is reached only when the cop count is below 1"),
		FString(Player->DialogRefusalReason()),
		FString(TEXT("something is still hunting the player")));
	return true;
}

// =================================================================================================
// `CPayphone::CanTalk` (`0x101aaee0`) — slot 295's species override
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialoguePayphoneCanTalkTest,
	"Elysium.Substrate.NpcKernelDialogue.PayphoneCanTalk", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialoguePayphoneCanTalkTest::RunTest(const FString&)
{
	FDialogueFixture F;
	if (!TestNotNull(TEXT("phone spawned"), F.Phone)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }

	// `npc_payphone` is BOTH a census classname and a registered spawn leaf, so this is a real
	// `CPayphone` and the census agrees.
	const FElysiumNpcClass* PhoneClass = F.Phone->RetailClass();
	if (!TestNotNull(TEXT("the census claims npc_payphone"), PhoneClass)) { return false; }
	TestEqual(TEXT("slots.md: CPayphone fills slot 295 with 0x101aaee0"),
		FString(ElysiumNpcKernelClass::BodyOf(PhoneClass, 295)), FString(TEXT("0x101aaee0")));

	// The admitting state: an activator, an authored `dialogname`, visible, willing, idle, no
	// NO_DIALOG, no open session.
	F.Phone->bWillTalk = true;
	TestTrue(TEXT("0x101aaee0: all seven arms clear admits"),
		F.Phone->PayphoneCanTalk(F.Other));

	// Arm 1 — a null activator.
	TestFalse(TEXT("arm 1: a null activator refuses"), F.Phone->PayphoneCanTalk(nullptr));

	// Arm 3 — `m_bScriptHidden` (+0x00f4), read through `0x100b5190`. NOT a liveness test: 29c's
	// walk called it "the alive test" and `fields.md` names the word `m_bScriptHidden`.
	F.Phone->ScriptHide();
	TestFalse(TEXT("arm 3: m_bScriptHidden (+0x00f4) refuses — the 0x100b5190 word"),
		F.Phone->PayphoneCanTalk(F.Other));
	F.Phone->ScriptUnhide();
	TestTrue(TEXT("...and unhiding admits again"), F.Phone->PayphoneCanTalk(F.Other));

	// Arm 4 — `m_bWillTalk` (+0x1088), whose only writer is `InputWillTalk` (`0x103418f0`).
	F.Phone->bWillTalk = false;
	TestFalse(TEXT("arm 4: m_bWillTalk (+0x1088) clear refuses"),
		F.Phone->PayphoneCanTalk(F.Other));
	F.Phone->bWillTalk = true;

	// Arm 6 — NO_DIALOG (`m_bfAINPCFlags & 0x80000`) ALONE. The payphone override does NOT test
	// `NO_DIALOG_PERSISTENT`, which the Troika-line body `0x102c21c0` does.
	F.Phone->NpcFlags.Set(EElysiumNpcFlag::NO_DIALOG);
	TestFalse(TEXT("arm 6: NO_DIALOG refuses"), F.Phone->PayphoneCanTalk(F.Other));
	F.Phone->NpcFlags.Clear(EElysiumNpcFlag::NO_DIALOG);
	F.Phone->NpcFlags.Set(EElysiumNpcFlag2::NO_DIALOG_PERSISTENT);
	TestTrue(TEXT("0x101aaee0 does NOT test NO_DIALOG_PERSISTENT, unlike the Troika gate"),
		F.Phone->PayphoneCanTalk(F.Other));
	F.Phone->NpcFlags.Clear(EElysiumNpcFlag2::NO_DIALOG_PERSISTENT);

	// Arm 7 — `IsInDialog` (`0x102c1170`): the body's answer IS its negation.
	F.Phone->Dialogue.bInDialog = true;
	TestFalse(TEXT("arm 7: an open session refuses"), F.Phone->PayphoneCanTalk(F.Other));
	F.Phone->Dialogue.bInDialog = false;

	// Arm 5 — `m_bfNPCStateFlags` (+0x5b64) bit 2, the per-state busy bit. The word is not stored:
	// it is a pure function of `m_NPCState` (`0x1026e3e0`, whose whole table the port carries), so
	// the arm is asserted through that function plus the live reading. An idle phone answers 0x31,
	// which does not carry bit 2 — which is why the admitting case above admits.
	TestEqual(TEXT("0x1026e3e0: combat's state byte is 0x8f and carries bit 2"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(2)) & 0x4, 0x4);
	TestEqual(TEXT("0x1026e3e0: flee (8) is 0x85 and carries bit 2"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(8)) & 0x4, 0x4);
	TestEqual(TEXT("0x1026e3e0: dead (7) is 0x04, which IS bit 2"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(7)), 0x4);
	TestEqual(TEXT("0x1026e3e0: idle's 0x31 does not carry bit 2, so an idle phone admits"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(1)) & 0x4, 0);
	TestEqual(TEXT("arm 5 reads the live byte, which an idle phone answers 0x31 for"),
		static_cast<int32>(F.Phone->NpcStateFlags()), 0x31);

	// Arm 2 — an NPC with no authored `dialogname` (`m_iDialog` +0x0128). The ordinary combatant
	// authored none, so it stands for the empty word.
	TestFalse(TEXT("arm 2: no dialogname (+0x0128) refuses"),
		F.Other->PayphoneCanTalk(F.Phone));
	return true;
}

// =================================================================================================
// Slot 366's `CNPC_VSabbatLeader` override (`0x103a76d0`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueHandleInteractionTest,
	"Elysium.Substrate.NpcKernelDialogue.HandleInteraction", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueHandleInteractionTest::RunTest(const FString&)
{
	FDialogueFixture F;
	if (!TestNotNull(TEXT("sabbat spawned"), F.Sabbat)) { return false; }

	// Every row exercised BY NAME, and each checked against the census so `slots.md` and the port
	// cannot drift.
	int32 Count = 0;
	const FElysiumNpc::FHandleInteractionSpecies* Rows =
		FElysiumNpc::HandleInteractionSpeciesRows(Count);
	if (!TestEqual(TEXT("two slot-366 rows: the Sabbat override and the line it tail-calls"),
		Count, 2))
	{
		return false;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Rows[Index].RetailClass);
		TestNotNull(*FString::Printf(TEXT("the census carries %s"), Rows[Index].RetailClass), Cls);
		TestEqual(*FString::Printf(TEXT("slots.md: %s fills slot 366 with %s"),
			Rows[Index].RetailClass, Rows[Index].Body),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 366)), FString(Rows[Index].Body));
		TestFalse(*FString::Printf(TEXT("%s's slot 366 answers false"), Rows[Index].RetailClass),
			Rows[Index].bAnswer);
	}

	TestNotNull(TEXT("CNPC_VSabbatLeader row is reachable by name"),
		FElysiumNpc::HandleInteractionSpeciesOf(TEXT("CNPC_VSabbatLeader")));
	TestNotNull(TEXT("CNPC_VAndreiBlood row is reachable by name"),
		FElysiumNpc::HandleInteractionSpeciesOf(TEXT("CNPC_VAndreiBlood")));
	TestNull(TEXT("a class with no row answers null"),
		FElysiumNpc::HandleInteractionSpeciesOf(TEXT("CNPC_VRat")));

	// `npc_VSabbatLeader` is a spawn leaf AND a census classname, so the body runs on a real one.
	TestTrue(TEXT("the census resolves npc_VSabbatLeader to CNPC_VSabbatLeader"),
		F.Sabbat->IsRetailClass(TEXT("CNPC_VSabbatLeader")));
	// `0x103a76d0` is a scope-trace prologue and a tail call to `0x10385a70`, whose whole body is
	// `return 0`. The answer is false for every argument, which is what "the override adds only a
	// debug name" means.
	TestFalse(TEXT("0x103a76d0 answers false"),
		F.Sabbat->SabbatLeaderHandleInteraction(0, nullptr, nullptr));
	TestFalse(TEXT("...for any interaction id and any partner"),
		F.Sabbat->SabbatLeaderHandleInteraction(0x2a, nullptr, F.Guard));
	return true;
}

// =================================================================================================
// `CNPC_VCameraSecurity`'s linked camera (`0x10369e70`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueSecCameraLinkTest,
	"Elysium.Substrate.NpcKernelDialogue.SecCameraLink", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueSecCameraLinkTest::RunTest(const FString&)
{
	FDialogueFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }

	// The brief's second table fact: `npc_VCameraSecurity` IS claimed by the census but is NOT a
	// registered spawn leaf, so the class row is exercised by NAME and the body on an ordinary NPC.
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity"));
	if (!TestNotNull(TEXT("the census carries CNPC_VCameraSecurity"), Cls)) { return false; }
	TestNotNull(TEXT("the census claims the classname npc_VCameraSecurity"),
		ElysiumNpcKernelClass::OfClassname(FString(TEXT("npc_VCameraSecurity"))));
	TestNull(TEXT("...but the spawn registry does NOT register it, so no map may stand one"),
		FElysiumClassRegistry::Get().Find(FName(TEXT("npc_VCameraSecurity"))));
	// The payphone is the counter-example both tables agree on.
	TestNotNull(TEXT("npc_payphone is in both tables"),
		FElysiumClassRegistry::Get().Find(FName(TEXT("npc_payphone"))));

	// Block 1 with an EMPTY name: nothing to look up, so the handle stays clear.
	F.Guard->LinkedCameraName.Empty();
	TestNull(TEXT("0x10369e70: no m_iszLinkedCamera (+0x6660) resolves to nothing"),
		F.Guard->ResolveSecCameraLink());
	TestFalse(TEXT("...and the +0x6668 latch stays clear"), F.Guard->bLinkedCameraBound);
	TestFalse(TEXT("...and an NPC that never linked is NOT removed"), F.Guard->IsDead());

	// Block 1 with a name that resolves: the handle is cached at `+0x6664` and block 2 raises the
	// `+0x6668` latch.
	F.Guard->LinkedCameraName = TEXT("other");
	TestTrue(TEXT("0x10369e70: the name resolves and is cached at +0x6664"),
		F.Guard->ResolveSecCameraLink() == static_cast<FElysiumEntity*>(F.Other));
	TestTrue(TEXT("...at m_hLinkedCamera (+0x6664)"),
		F.Guard->LinkedCamera == F.Other->Handle);
	TestTrue(TEXT("...and the +0x6668 latch is raised"), F.Guard->bLinkedCameraBound);

	// A second call with the handle still live skips block 1 entirely — the cached handle is what
	// is returned, which is the whole point of the cache.
	F.Guard->LinkedCameraName = TEXT("no-such-entity");
	TestTrue(TEXT("0x10369e70: a live cache is not re-resolved"),
		F.Guard->ResolveSecCameraLink() == static_cast<FElysiumEntity*>(F.Other));

	// Block 2's teardown: the cached camera is gone AND the latch was set, so the NPC removes
	// ITSELF (`UTIL_Remove` `0x101cd940`). This is the one-way door.
	F.Other->Kill();
	F.World.World.Tick(F.World.World.NowSeconds());
	TestNull(TEXT("0x10369e70: a dead camera with no replacement answers null"),
		F.Guard->ResolveSecCameraLink());
	TestTrue(TEXT("...and a PREVIOUSLY LINKED camera NPC removes itself"), F.Guard->IsDead());
	return true;
}

// =================================================================================================
// The pedestrian crosswalk rule — `0x102a0bc0`, `0x102a0b90`, `0x102a0d20`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueCrosswalkTest,
	"Elysium.Substrate.NpcKernelDialogue.Crosswalk", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueCrosswalkTest::RunTest(const FString&)
{
	FDialogueFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }

	// The phase mask: `0x10 << (((int)curtime >> 4) & 3)`, a 64-second cycle in four 16-second
	// phases. The truncation is retail's `__ftol`, toward zero.
	TestEqual(TEXT("phase 0 (t in [0,16)) is 0x10"), FElysiumNpc::CrosswalkPhaseMask(0.0), 0x10);
	TestEqual(TEXT("t = 15.9 is still phase 0"), FElysiumNpc::CrosswalkPhaseMask(15.9), 0x10);
	TestEqual(TEXT("t = 16 is phase 1, 0x20"), FElysiumNpc::CrosswalkPhaseMask(16.0), 0x20);
	TestEqual(TEXT("t = 32 is phase 2, 0x40"), FElysiumNpc::CrosswalkPhaseMask(32.0), 0x40);
	TestEqual(TEXT("t = 48 is phase 3, 0x80"), FElysiumNpc::CrosswalkPhaseMask(48.0), 0x80);
	TestEqual(TEXT("t = 64 wraps to phase 0"), FElysiumNpc::CrosswalkPhaseMask(64.0), 0x10);

	// `0x102a0b90` — the two-statement helper.
	TestFalse(TEXT("AT_CROSSWALK starts clear"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	F.Guard->SetAtCrosswalkLink(0x30);
	TestTrue(TEXT("0x102a0b90 raises AT_CROSSWALK (m_bfAINPCFlags 0x4)"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	TestTrue(TEXT("0x102a0b90 stores the link at +0x630c"), F.Guard->bCrosswalkLinkBound);
	TestEqual(TEXT("...and the one word its readers touch, link+0x64"),
		F.Guard->CrosswalkLinkSignalFlags, 0x30);

	// --- `0x102a0bc0` — six gates, all required, in retail's order ---------------------------------
	FElysiumNpc::FDialogPedWaypoint Waypoint;
	Waypoint.EntityIndex = 4;
	Waypoint.Flags = 0x4;
	Waypoint.bHasHint = true;
	Waypoint.HintLinkId = 77;

	TestFalse(TEXT("0x102a0bc0 gate 1: a null waypoint refuses"),
		F.Guard->ResolvePedestrianPathNode(nullptr));

	FElysiumNpc::FDialogPedWaypoint NoEntity = Waypoint;
	NoEntity.EntityIndex = -1;
	TestFalse(TEXT("gate 2: a negative entity index (+0x10) refuses"),
		F.Guard->ResolvePedestrianPathNode(&NoEntity));

	FElysiumNpc::FDialogPedWaypoint NoHint = Waypoint;
	NoHint.bHasHint = false;
	TestFalse(TEXT("gate 3: a null hint (+0x30) refuses"),
		F.Guard->ResolvePedestrianPathNode(&NoHint));

	FElysiumNpc::FDialogPedWaypoint NoFlag = Waypoint;
	NoFlag.Flags = 0;
	TestFalse(TEXT("gate 4: the waypoint's own bit 2 (+0x28) clear refuses"),
		F.Guard->ResolvePedestrianPathNode(&NoFlag));

	// Gate 5 — the navigator's path type. The motor seam answers -1, which is the refusal this
	// runtime honestly has, and no seam below it is reached.
	F.Guard->CrosswalkNodeQueries.Reset();
	TestEqual(TEXT("the navigator path-type seam answers -1, not 8"),
		F.Guard->NavigatorPathType(), -1);
	TestFalse(TEXT("gate 5: a non-pedestrian path type refuses"),
		F.Guard->ResolvePedestrianPathNode(&Waypoint));
	TestEqual(TEXT("...and the node seam is never even asked"),
		F.Guard->CrosswalkNodeQueries.Num(), 0);

	// With the path type driven to retail's 8, gate 6 is reached and the node seam IS asked — the
	// refusal is the recovered one (no node graph in this runtime), not a dropped arm.
	F.Guard->NavigatorPathTypeWord = 8;
	TestFalse(TEXT("gate 6: the node seam answers nothing, so the body refuses"),
		F.Guard->ResolvePedestrianPathNode(&Waypoint));
	if (!TestEqual(TEXT("...but the seam WAS asked, with the waypoint's entity and the hint's link"),
		F.Guard->CrosswalkNodeQueries.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("...carrying +0x10 and hint+0x10"), F.Guard->CrosswalkNodeQueries[0],
		FString(TEXT("node ent=4 link=77")));

	// --- `0x102a0d20` — the per-think rule --------------------------------------------------------
	F.Guard->NavigatorPathTypeWord = -1;
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::CrosswalkWalk);
	F.Guard->UpdatePedestrianInfo();
	TestTrue(TEXT("0x102a0d20: a non-pedestrian path type returns BEFORE the three clears"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkWalk));

	// The three clears run on every pedestrian pass, whether or not the NPC is at a crossing.
	F.Guard->NavigatorPathTypeWord = 8;
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::ShouldInteract);
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::CrosswalkDontWalk);
	F.Guard->NpcFlags.Clear(EElysiumNpcFlag::AT_CROSSWALK);
	F.Guard->UpdatePedestrianInfo();
	TestFalse(TEXT("0x102a0d20 clears SHOULD_INTERACT (0x10)"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::ShouldInteract));
	TestFalse(TEXT("0x102a0d20 clears CROSSWALK_WALK (0x12)"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkWalk));
	TestFalse(TEXT("0x102a0d20 clears CROSSWALK_DONTWALK (0x13)"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkDontWalk));

	// The throttle: `m_flNextCrosswalkUpdateTime (+0x6318) < curtime` STRICTLY.
	const double Now = F.World.World.NowSeconds();
	F.Guard->SetAtCrosswalkLink(0x10);            // phase 0's bit — the DONT-WALK signal at t = 0
	F.Guard->NextCrosswalkUpdateTime = Now + 100.0;
	F.Guard->UpdatePedestrianInfo();
	TestFalse(TEXT("0x102a0d20: a future stamp throttles the pass"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkDontWalk));
	TestEqual(TEXT("...and does not re-arm the stamp"), F.Guard->NextCrosswalkUpdateTime,
		Now + 100.0);

	// The DONT-WALK arm: the signal bit for this phase is SET. The NPC keeps waiting and the
	// AT_CROSSWALK latch STAYS.
	F.Guard->NextCrosswalkUpdateTime = Now - 1.0;
	F.Guard->CrosswalkLinkSignalFlags = FElysiumNpc::CrosswalkPhaseMask(Now);
	F.Guard->UpdatePedestrianInfo();
	TestTrue(TEXT("0x102a0d20: the signal bit SET raises CROSSWALK_DONTWALK (0x13)"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkDontWalk));
	TestFalse(TEXT("...and NOT CROSSWALK_WALK"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkWalk));
	TestTrue(TEXT("...and the AT_CROSSWALK latch STAYS on the dont-walk arm"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));
	// The 29c walk says curtime+5s; `corpus asm 102a0d20` shows `FADD [0x104454c0]` = 1.0.
	TestEqual(TEXT("0x102a0d20 re-arms at curtime + _DAT_104454c0 = 1.0 s, NOT 5 s"),
		F.Guard->NextCrosswalkUpdateTime, Now + 1.0);

	// The WALK arm: the signal bit for this phase is CLEAR. The NPC is released and the latch drops.
	F.Guard->NextCrosswalkUpdateTime = Now - 1.0;
	F.Guard->CrosswalkLinkSignalFlags = ~FElysiumNpc::CrosswalkPhaseMask(Now);
	F.Guard->UpdatePedestrianInfo();
	TestTrue(TEXT("0x102a0d20: the signal bit CLEAR raises CROSSWALK_WALK (0x12)"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkWalk));
	TestFalse(TEXT("...and NOT CROSSWALK_DONTWALK"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::CrosswalkDontWalk));
	TestFalse(TEXT("...and the AT_CROSSWALK latch DROPS on the walk arm"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK));

	// The condition identities are the registrar's own (`FUN_102c8ce0`).
	TestEqual(TEXT("0x10 is SHOULD_INTERACT"),
		FString(ElysiumNpcCondName(EElysiumNpcCond::ShouldInteract)),
		FString(TEXT("SHOULD_INTERACT")));
	TestEqual(TEXT("0x12 is CROSSWALK_WALK"),
		FString(ElysiumNpcCondName(EElysiumNpcCond::CrosswalkWalk)),
		FString(TEXT("CROSSWALK_WALK")));
	TestEqual(TEXT("0x13 is CROSSWALK_DONTWALK"),
		FString(ElysiumNpcCondName(EElysiumNpcCond::CrosswalkDontWalk)),
		FString(TEXT("CROSSWALK_DONTWALK")));
	return true;
}

// =================================================================================================
// Slots 39 and 42 — `OnUseBegin` (`0x100a4fe0`) and `OnUseEnd` (`0x100a5030`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDialogueUseSlotsTest,
	"Elysium.Substrate.NpcKernelDialogue.UseSlots", GElysiumNpcKernelDialogueFlags)
bool FElysiumNpcKernelDialogueUseSlotsTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture World([]
	{
		FElysiumNpcWorldBuilder Builder(TEXT("dialogue_use"), 29106u);
		Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
		Builder.AddNpc(TEXT("user"), FVector(100.f, 0.f, 0.f));
		Builder.AddCounter(TEXT("begins"));
		Builder.AddCounter(TEXT("ends"));
		// `OnUseBegin` (+0x5c) and `OnUseEnd` (+0x74) are `CBaseEntity` datamap keyfields, so a
		// shipped map may wire either on any `npc_*` row.
		Builder.WireOutput(TEXT("guard"), TEXT("OnUseBegin"), TEXT("begins"));
		Builder.WireOutput(TEXT("guard"), TEXT("OnUseEnd"), TEXT("ends"));
		return Builder;
	}());
	FElysiumNpc* Guard = World.Npc(TEXT("guard"));
	FElysiumNpc* User = World.Npc(TEXT("user"));
	if (!TestNotNull(TEXT("guard spawned"), Guard)) { return false; }
	if (!TestNotNull(TEXT("user spawned"), User)) { return false; }
	FElysiumNpcWorldFixture::Quiet({ Guard, User });

	TestFalse(TEXT("+0x8c starts unset"), Guard->UseActivator.IsSet());

	// slot 39 — the output fires FIRST, then the handle is cached.
	Guard->OnUseBegin(User);
	World.World.Tick(World.World.NowSeconds());
	TestEqual(TEXT("0x100a4fe0 fires m_OnUseBegin (+0x5c)"), World.Counter(TEXT("begins")), 1.f);
	TestTrue(TEXT("0x100a4fe0 caches the activator at +0x8c"),
		Guard->UseActivator == User->Handle);

	// slot 42 — the output fires, then `+0x8c` is cleared UNCONDITIONALLY.
	Guard->OnUseEnd(User);
	World.World.Tick(World.World.NowSeconds());
	TestEqual(TEXT("0x100a5030 fires m_OnUseEnd (+0x74)"), World.Counter(TEXT("ends")), 1.f);
	TestFalse(TEXT("0x100a5030 clears +0x8c to -1"), Guard->UseActivator.IsSet());

	// A NULL activator still fires slot 39's output — the write is after the fire, not inside a
	// guard — and writes -1 rather than a handle.
	Guard->OnUseBegin(User);
	Guard->OnUseBegin(nullptr);
	World.World.Tick(World.World.NowSeconds());
	TestEqual(TEXT("0x100a4fe0 fires even for a null activator"),
		World.Counter(TEXT("begins")), 3.f);
	TestFalse(TEXT("0x100a4fe0 writes -1 to +0x8c for a null activator"),
		Guard->UseActivator.IsSet());

	// slot 42's clear does not consult the activator: ending someone else's use still clears.
	Guard->OnUseBegin(User);
	Guard->OnUseEnd(nullptr);
	TestFalse(TEXT("0x100a5030 clears +0x8c whoever ends the use"), Guard->UseActivator.IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
