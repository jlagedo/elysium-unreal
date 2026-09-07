// D3 — use-to-talk entry, the player-side dialogue refusal predicate and the dialogue holster.
//
// Retail chain (`docs/project/plans/dialogue.md` arm 1, `docs/vtmb/game_runtime.md` §5):
// `CBasePlayer::PlayerUse` (`0x10167850`) resolves the use target, tests the character's `WillTalk`
// latch (virtual `+0x49c`, `InputWillTalk` `0x103418f0`), clears the schedule and calls player
// vtable slot 414 (`FUN_10178280`). That function refuses when `m_bForceDialogStart` (`npc+0x6495`)
// is clear and the player-side predicate `0x10178170` holds, and otherwise acquires the dialog,
// locks input and holsters the active weapon to `item_w_unarmed`. `CDialog::Release`
// (`0x100e5240`) restores the weapon (`FUN_10178400`).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInputScope.h"
#include "ElysiumInteraction.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumStub.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDlg.h"
#include "Tests/ElysiumDialogueTestHelpers.h"
#include "Tests/ElysiumScratchContentRoot.h"
#include "Tests/ElysiumTestServices.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace ElysiumDialogueEntryTests
{
static constexpr EAutomationTestFlags GEntryTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	const TCHAR* const GTalker = TEXT("talker");
	const TCHAR* const GSilent = TEXT("silent");
	const TCHAR* const GMissingDlg = TEXT("missing_dlg");
	const TCHAR* const GBeginCounter = TEXT("dialog_begun");
	const TCHAR* const GEndCounter = TEXT("dialog_ended");

	// One NPC that carries a `dialogname`, wired so `OnDialogBegin` / `OnDialogEnd` are observable,
	// plus an `events_player` bus so `ClearDialogCombatTimers` can be fired the way a map fires it.
	FElysiumEntityDefs MakeEntryDefs(const TCHAR* DialogName = TEXT("dlg/__entry_test__"))
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__dialogue_entry_test__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VPedestrian");
		Npc.TargetName = GTalker;
		Npc.Origin = FVector(200.0f, 0.0f, 0.0f);
		// The one keyfield `IsUsable()` turns on. Unless a caller points it at a real file, the
		// file never loads in a content-free world -- the documented "manual seam" case: the
		// session still latches `bInDialog` and waits for a hand-fired `EndDialog`.
		Npc.Keys.Add(TEXT("dialogname"), DialogName);
		{
			FElysiumOutputDef Begin;
			Begin.Name = TEXT("OnDialogBegin");
			Begin.Target = GBeginCounter;
			Begin.Input = TEXT("Add");
			Begin.Param = TEXT("1");
			Npc.Outputs.Add(MoveTemp(Begin));
			FElysiumOutputDef End;
			End.Name = TEXT("OnDialogEnd");
			End.Target = GEndCounter;
			End.Input = TEXT("Add");
			End.Param = TEXT("1");
			Npc.Outputs.Add(MoveTemp(End));
		}
		Defs.Defs.Add(MoveTemp(Npc));

		// A character carrying no conversation at all. `IsUsable()` is false for it, but a script
		// can still fire `StartPlayerDialogRemote` straight at it -- 108 authored calls do -- and
		// that is the entry whose failure must not cost the player their weapon.
		FElysiumEntityDef Silent;
		Silent.Classname = TEXT("npc_VPedestrian");
		Silent.TargetName = GSilent;
		Silent.Origin = FVector(400.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Silent));

		// The other half of the same failure: a `dialogname` naming a file that is not there.
		// `CDialog::Acquire` fails on it exactly as it does on the missing name.
		FElysiumEntityDef Missing;
		Missing.Classname = TEXT("npc_VPedestrian");
		Missing.TargetName = GMissingDlg;
		Missing.Origin = FVector(600.0f, 0.0f, 0.0f);
		Missing.Keys.Add(TEXT("dialogname"), TEXT("dlg/test/absent.dlg"));
		Defs.Defs.Add(MoveTemp(Missing));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = GBeginCounter;
		Defs.Defs.Add(Counter);
		Counter.TargetName = GEndCounter;
		Defs.Defs.Add(Counter);

		FElysiumEntityDef Events;
		Events.Classname = TEXT("events_player");
		Events.TargetName = TEXT("pcevents");
		Defs.Defs.Add(MoveTemp(Events));

		return Defs;
	}

	FString DebugRow(const FElysiumEntity& Ent, const TCHAR* Key)
	{
		TArray<TPair<FString, FString>> Rows;
		Ent.GetDebugState(Rows);
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == Key)
			{
				return Row.Value;
			}
		}
		return FString();
	}

	float CounterValue(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		TArray<TPair<FString, FString>> Rows;
		if (FElysiumEntity* Counter = World.FindByName(Name))
		{
			Counter->GetDebugState(Rows);
		}
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == TEXT("Value"))
			{
				return FCString::Atof(*Row.Value);
			}
		}
		return -1.0f;
	}

	// A health track without a rulebook, so `CommitDamage` reaches its terminus rather than
	// early-returning on a zero ceiling.
	void SeedHealth(FElysiumCombatCharacter& Char, int32 Max)
	{
		Char.Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, Max);
		Char.Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
		Char.RecomputeSheet();
	}

	FElysiumUseBeginResult PressUse(FElysiumEntityWorld& World, FElysiumNpc& Npc)
	{
		return World.BeginPlayerUseSession(Npc.Handle, World.PlayerHandle());
	}
}

// --- Elysium.Substrate.NpcUseStartsDialog ----------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcUseStartsDialogTest,
	"Elysium.Substrate.NpcUseStartsDialog", GEntryTestFlags)
bool FElysiumNpcUseStartsDialogTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeEntryDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* Ent = World.FindByName(GTalker);
	FElysiumNpc* Npc = Ent ? Ent->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("the talker spawned"), Npc))
	{
		return false;
	}

	// --- Eligibility: the class verb is `dialogname`, the latch is `WillTalk` ---------------
	TestTrue(TEXT("an NPC with a dialogname is usable"), Npc->IsUsable());
	FElysiumUseContext Context;
	Context.Activator = World.PlayerHandle();
	Context.Owner = Npc->Handle;
	// The latch SHIPS SET. `vampire.dll` has no constructor and no keyfield writer for `+0x49c`
	// -- `InputWillTalk` (`0x103418f0`) is its only writer -- and `sp_tutorial_1` fires
	// `Jack,WillTalk 0` at map load, which only means anything against a true default. With a
	// false one, `+use` would be dead on every character no script had cued.
	TestTrue(TEXT("a spawned character ships willing to talk"), Npc->CanPlayerFocus(Context));

	// `WillTalk 0` is therefore a DISABLER, and it is the authored spelling the corpus uses at load.
	World.AcceptInput(Npc->Handle, FName(TEXT("WillTalk")), FElysiumVariant::Int(0),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestFalse(TEXT("WillTalk 0 refuses the focus"), Npc->CanPlayerFocus(Context));
	TestEqual(TEXT("...and pressing use on it does nothing"),
		PressUse(World, *Npc).Outcome, EElysiumUseOutcome::Unavailable);
	TestFalse(TEXT("...leaving no session open"), Npc->bInDialog);

	World.AcceptInput(Npc->Handle, FName(TEXT("WillTalk")), FElysiumVariant::Int(1),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("WillTalk 1 re-admits the focus"), Npc->CanPlayerFocus(Context));
	// The reticle glyph is the talk icon from the recovered use_icon table (14 female / 15 male).
	const int32 Icon = Npc->ResolveUseIcon(World.PlayerHandle());
	TestTrue(TEXT("a focusable talker draws the talk glyph"), Icon == 14 || Icon == 15);

	// --- The transaction --------------------------------------------------------------------
	const FElysiumUseBeginResult Opened = PressUse(World, *Npc);
	TestEqual(TEXT("use completes rather than opening a lingering use session"),
		Opened.Outcome, EElysiumUseOutcome::Completed);
	TestEqual(TEXT("...and starts no captured session kind"),
		Opened.SessionKind, EElysiumUseSessionKind::None);
	TestTrue(TEXT("the conversation is open"), Npc->bInDialog);
	TestEqual(TEXT("the opener is recorded as PlayerUse"), Npc->DialogOpener,
		EElysiumDialogOpenerKind::Use);
	TestTrue(TEXT("dialogue holds the body through its own owner token"),
		DebugRow(*Npc, TEXT("Body owner")).Contains(TEXT("Dialogue")));
	TestFalse(TEXT("+use does not set m_bForceDialogStart"), Npc->bForceDialogStart);
	World.Tick(0.0);
	TestEqual(TEXT("OnDialogBegin fired exactly once"), CounterValue(World, GBeginCounter), 1.0f);
	TestFalse(TEXT("an NPC already in dialogue is no longer focusable"),
		Npc->CanPlayerFocus(Context));

	// --- The close ---------------------------------------------------------------------------
	World.AcceptInput(Npc->Handle, FName(TEXT("EndDialog")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	World.Tick(0.0);
	TestFalse(TEXT("EndDialog closes the session"), Npc->bInDialog);
	TestEqual(TEXT("OnDialogEnd fired exactly once"), CounterValue(World, GEndCounter), 1.0f);
	TestEqual(TEXT("times_talked counted the conversation"), Npc->TimesTalked, 1);
	TestFalse(TEXT("the body claim is released"),
		DebugRow(*Npc, TEXT("Body owner")).Contains(TEXT("Dialogue")));
	TestTrue(TEXT("the character is focusable again"), Npc->CanPlayerFocus(Context));

	// An inert character is never a talk target, whatever its latches say.
	Npc->Kill();
	TestFalse(TEXT("a dead talker refuses focus"), Npc->CanPlayerFocus(Context));
	return true;
}

// --- Elysium.Substrate.DialogRefusalPredicate ------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogRefusalPredicateTest,
	"Elysium.Substrate.DialogRefusalPredicate", GEntryTestFlags)
bool FElysiumDialogRefusalPredicateTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeEntryDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* Ent = World.FindByName(GTalker);
	FElysiumNpc* Npc = Ent ? Ent->AsNpc() : nullptr;
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the talker spawned"), Npc)
		|| !TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}
	SeedHealth(*Player, 100);
	World.AcceptInput(Npc->Handle, FName(TEXT("WillTalk")), FElysiumVariant::Int(1),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());

	TestTrue(TEXT("a quiet player has no refusal reason"),
		Player->DialogRefusalReason() == nullptr);

	// --- The damage path stamps `player+0x1d1c` ---------------------------------------------
	Player->TakeDamage(10.0f);
	TestTrue(TEXT("a hit stamps the no-dialogue-until deadline"),
		Player->NoDialogueUntil > World.NowSeconds());
	TestTrue(TEXT("...so the predicate now refuses"),
		Player->DialogRefusalReason() != nullptr);

	// --- A refused +use posts the M-REFUSE notification and opens nothing -------------------
	Services.Notifications.Reset();
	const FElysiumUseBeginResult Refused =
		World.BeginPlayerUseSession(Npc->Handle, World.PlayerHandle());
	TestEqual(TEXT("a refused use reports Unavailable"), Refused.Outcome,
		EElysiumUseOutcome::Unavailable);
	TestFalse(TEXT("...and no conversation opened"), Npc->bInDialog);
	TestEqual(TEXT("M-REFUSE posts exactly one HUD notification"),
		Services.Notifications.Num(), 1);
	if (Services.Notifications.IsValidIndex(0))
	{
		TestEqual(TEXT("...carrying the refusal line"), Services.Notifications[0].Subject,
			FString(ElysiumDialogue::RefusalNotice));
	}

	// --- The scripted unforced opener refuses SILENTLY, as retail does ----------------------
	Services.Notifications.Reset();
	World.AcceptInput(Npc->Handle, FName(TEXT("StartPlayerDialogUnforced")),
		FElysiumVariant::Int(0), FElysiumEntityHandle::Invalid(),
		FElysiumEntityHandle::Invalid());
	TestFalse(TEXT("StartPlayerDialogUnforced honours the predicate"), Npc->bInDialog);
	TestEqual(TEXT("...and says nothing on the HUD"), Services.Notifications.Num(), 0);

	// --- A forced opener sets `m_bForceDialogStart` and bypasses the predicate --------------
	World.AcceptInput(Npc->Handle, FName(TEXT("StartPlayerDialog")), FElysiumVariant::Int(0),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestTrue(TEXT("a forced start opens through a standing refusal"), Npc->bInDialog);
	TestTrue(TEXT("...because it set the force byte"), Npc->bForceDialogStart);
	World.AcceptInput(Npc->Handle, FName(TEXT("EndDialog")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestFalse(TEXT("the close clears the force byte"), Npc->bForceDialogStart);

	// --- `ClearDialogCombatTimers` reaches the player ---------------------------------------
	// Fired at the map's `events_player` bus, which is how `pcevents` fires it off Jack's
	// `OnDamaged` in `sp_tutorial_1`.
	World.AcceptInput(TEXT("pcevents"), FName(TEXT("ClearDialogCombatTimers")),
		FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(),
		FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("the input clears the no-dialogue-until stamp"), Player->NoDialogueUntil, 0.0);
	TestTrue(TEXT("...so the predicate stands down"),
		Player->DialogRefusalReason() == nullptr);

	Services.Notifications.Reset();
	const FElysiumUseBeginResult Admitted =
		World.BeginPlayerUseSession(Npc->Handle, World.PlayerHandle());
	TestEqual(TEXT("use is admitted once the timers are cleared"), Admitted.Outcome,
		EElysiumUseOutcome::Completed);
	TestTrue(TEXT("...and the conversation opened"), Npc->bInDialog);
	TestEqual(TEXT("...with nothing posted to the HUD"), Services.Notifications.Num(), 0);

	// --- The seam arms are read, not dropped -------------------------------------------------
	World.AcceptInput(Npc->Handle, FName(TEXT("EndDialog")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	// `player+0x1cf8` clears at FLT_MAX; anything else blocks. No producer writes it yet, so the
	// arm is exercised by hand here to prove it is wired rather than commented out.
	Player->DialogRefusalFloat = 0.0f;
	TestTrue(TEXT("the FLT_MAX-sentinel arm blocks when it is not at the sentinel"),
		Player->DialogRefusalReason() != nullptr);
	Player->ClearDialogCombatTimers();
	TestTrue(TEXT("...and the clear restores the sentinel"),
		Player->DialogRefusalReason() == nullptr);
	Player->DialogCombatStampB = World.NowSeconds() + 30.0;
	TestTrue(TEXT("the second auxiliary stamp blocks too"),
		Player->DialogRefusalReason() != nullptr);
	Player->ClearDialogCombatTimers();
	TestTrue(TEXT("...and clears with the rest"),
		Player->DialogRefusalReason() == nullptr);
	return true;
}

// --- Elysium.Substrate.DialogHolster ----------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogHolsterTest,
	"Elysium.Substrate.DialogHolster", GEntryTestFlags)
bool FElysiumDialogHolsterTest::RunTest(const FString&)
{
	// The movement half of retail's input lock is the port's input-scope stack, not a substrate
	// call: the conversation screen pushes the `Dialogue` scope as it activates. Assert the
	// resolved policy, which is what actually takes the gameplay mapping contexts away.
	{
		FElysiumInputScopeStack Stack;
		TestEqual(TEXT("an empty stack leaves gameplay in charge"),
			Stack.Resolve().Mode, EElysiumInputMode::GameOnly);
		FElysiumInputScope Dialogue;
		Dialogue.Name = TEXT("Dialogue");
		Dialogue.Priority = ElysiumInput::Priority::Dialogue;
		Dialogue.Mode = EElysiumInputMode::UIOnly;
		const FElysiumInputScopeHandle Pushed = Stack.Push(Dialogue);
		TestEqual(TEXT("the Dialogue scope locks movement by taking input UI-only"),
			Stack.Resolve().Mode, EElysiumInputMode::UIOnly);
		TestEqual(TEXT("...at the conversation priority"), Dialogue.Priority, 40);
		Stack.Pop(Pushed);
		TestEqual(TEXT("closing gives movement back"),
			Stack.Resolve().Mode, EElysiumInputMode::GameOnly);
	}

	// --- The weapon half ---------------------------------------------------------------------
	FElysiumItemTable Table;
	{
		FElysiumItemDef Unarmed;
		Unarmed.Classname = TEXT("item_w_unarmed");
		Unarmed.PrintName = Unarmed.Classname;
		Unarmed.Type = EElysiumItemType::WeaponMelee;
		Unarmed.PlayerModel = TEXT("models/weapons/w_null.mdl");
		Table.Items.Add(MoveTemp(Unarmed));

		FElysiumItemDef Iron;
		Iron.Classname = TEXT("item_w_test_iron");
		Iron.PrintName = Iron.Classname;
		Iron.Type = EElysiumItemType::WeaponMelee;
		Iron.PlayerModel = TEXT("models/weapons/w_null.mdl");
		Iron.bWieldable = true;
		Table.Items.Add(MoveTemp(Iron));
		Table.Reindex();
	}
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// `FUN_10178280` holsters only AFTER `CDialog::Acquire` has taken the conversation, so the
	// fixture needs a `.dlg` that really loads: a conversation that fails to open must leave the
	// hands alone, and that is asserted below on its own NPC.
	const FElysiumScratchContentRoot Scratch(TEXT("DialogHolster"));
	const FString DialogName = TEXT("dlg/test/holster.dlg");
	const FString DlgPath = FElysiumContentPaths::DlgFromDialogname(DialogName);
	{
		using ElysiumDialogueTestHelpers::ElysiumDlgRow;
		const FString Joined = ElysiumDlgRow(1, TEXT("A word with you."), TEXT("#"),
			FString(), FString()) + TEXT("\r\n");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(DlgPath), /*Tree*/ true);
		if (!TestTrue(TEXT("the scratch content root is installed"), Scratch.IsInstalled())
			|| !TestTrue(TEXT("the holster fixture conversation writes"),
				FFileHelper::SaveStringToFile(Joined, *DlgPath)))
		{
			return false;
		}
	}

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.ItemGroundModelStates.Add(TEXT("models/weapons/w_null.mdl"),
		EElysiumItemGroundModelState::Geometryless);
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeEntryDefs(*DialogName));
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* Ent = World.FindByName(GTalker);
	FElysiumNpc* Npc = Ent ? Ent->AsNpc() : nullptr;
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("the talker spawned"), Npc)
		|| !TestNotNull(TEXT("the player exists"), Player))
	{
		return false;
	}

	// With nothing to fall back to, the open reports the named seam and leaves the hands alone —
	// the same refusal the `Holster` input makes rather than an invented empty-handed state.
	ElysiumStub::ClearTally();
	TestFalse(TEXT("a player with no carried unarmed cannot be holstered"),
		Player->HolsterForDialog());
	{
		TArray<ElysiumStub::FTally> Tally;
		ElysiumStub::CollectTally(Tally);
		const bool bFired = Tally.ContainsByPredicate([](const ElysiumStub::FTally& T)
			{ return T.Surface == TEXT("CBasePlayer.DialogHolster"); });
		TestTrue(TEXT("...and the seam is reported rather than silently skipped"), bFired);
	}

	// Now arm the player properly: `item_w_unarmed` carried, a real weapon in hand.
	const FElysiumEntityHandle Unarmed =
		Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_unarmed"));
	const FElysiumEntityHandle Iron =
		Player->Inventory.GiveNamedItem(*Player, TEXT("item_w_test_iron"));
	if (!TestTrue(TEXT("the fallback is carried"), Unarmed.IsSet())
		|| !TestTrue(TEXT("the weapon is carried"), Iron.IsSet()))
	{
		return false;
	}
	// The grant order leaves whatever it equipped active; state the hand explicitly.
	if (FElysiumEntity* IronEnt = World.Resolve(Iron))
	{
		if (FElysiumItem* IronItem = IronEnt->AsItem())
		{
			Player->Inventory.SetActiveWeapon(*Player, *IronItem);
		}
	}
	TestEqual(TEXT("the weapon is in hand before the conversation"),
		Player->Inventory.ActiveWeapon, Iron);

	const FElysiumUseBeginResult Opened =
		World.BeginPlayerUseSession(Npc->Handle, World.PlayerHandle());
	TestEqual(TEXT("the conversation opened"), Opened.Outcome, EElysiumUseOutcome::Completed);
	TestEqual(TEXT("opening a conversation holsters to item_w_unarmed"),
		Player->Inventory.ActiveWeapon, Unarmed);
	TestTrue(TEXT("...remembering the drawable weapon (player+0x1e01)"),
		Player->bDialogWeaponHolstered && Player->bDialogWeaponWasDrawable);
	TestEqual(TEXT("...by handle"), Player->DialogHolsteredWeapon, Iron);

	World.AcceptInput(Npc->Handle, FName(TEXT("EndDialog")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	// A script-fired `EndDialog` is retail's `CDialog::Release`: the world session goes with it.
	TestNull(TEXT("a script-fired EndDialog tears the world session down"), World.GetOpenDialog());
	TestEqual(TEXT("closing restores the holstered weapon"),
		Player->Inventory.ActiveWeapon, Iron);
	TestFalse(TEXT("...and forgets it"), Player->bDialogWeaponHolstered);

	// A second close restores nothing: the pair is one-shot, so a stale handle cannot re-arm the
	// player after the weapon has moved on.
	Player->Inventory.ActiveWeapon = Unarmed;
	World.AcceptInput(Npc->Handle, FName(TEXT("EndDialog")), FElysiumVariant::Void(),
		FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("a close with nothing holstered leaves the hands alone"),
		Player->Inventory.ActiveWeapon, Unarmed);

	// --- An open that FAILS must not disarm the player --------------------------------------
	// `CDialog::Acquire` is what `FUN_10178280` runs before it touches the weapon, and
	// `OpenConversation` fails exactly where `Acquire` does: no `dialogname`, or a `.dlg` that
	// will not load. Both leave `bInDialog` latched on the manual `EndDialog` seam with no
	// conversation to close them, so a holster taken on the way in would only come back if a
	// script happened to fire `EndDialog` by hand.
	if (FElysiumEntity* IronEnt = World.Resolve(Iron))
	{
		if (FElysiumItem* IronItem = IronEnt->AsItem())
		{
			Player->Inventory.SetActiveWeapon(*Player, *IronItem);
		}
	}
	TestEqual(TEXT("the weapon is back in hand"), Player->Inventory.ActiveWeapon, Iron);

	FElysiumEntity* SilentEnt = World.FindByName(GSilent);
	FElysiumNpc* Silent = SilentEnt ? SilentEnt->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("the dialogue-less character spawned"), Silent))
	{
		return false;
	}
	TestFalse(TEXT("a character with no dialogname carries no conversation"), Silent->IsUsable());
	World.AcceptInput(Silent->Handle, FName(TEXT("StartPlayerDialogRemote")),
		FElysiumVariant::Int(256), FElysiumEntityHandle::Invalid(),
		FElysiumEntityHandle::Invalid());
	TestEqual(TEXT("a remote start with no dialogname leaves the weapon drawn"),
		Player->Inventory.ActiveWeapon, Iron);
	TestFalse(TEXT("...and nothing is remembered as holstered"),
		Player->bDialogWeaponHolstered);
	TestNull(TEXT("...and no conversation opened"), World.GetOpenDialog());

	// The same for a `dialogname` naming a file that is not there.
	if (FElysiumEntity* MissingEnt = World.FindByName(GMissingDlg))
	{
		AddExpectedError(TEXT("dialog load failed"), EAutomationExpectedErrorFlags::Contains, 0);
		World.AcceptInput(MissingEnt->Handle, FName(TEXT("StartPlayerDialogRemote")),
			FElysiumVariant::Int(0), FElysiumEntityHandle::Invalid(),
			FElysiumEntityHandle::Invalid());
		TestEqual(TEXT("an unloadable .dlg leaves the weapon drawn too"),
			Player->Inventory.ActiveWeapon, Iron);
		TestFalse(TEXT("...with nothing remembered"), Player->bDialogWeaponHolstered);
	}
	return true;
}

}   // namespace ElysiumDialogueEntryTests

#endif   // WITH_DEV_AUTOMATION_TESTS
