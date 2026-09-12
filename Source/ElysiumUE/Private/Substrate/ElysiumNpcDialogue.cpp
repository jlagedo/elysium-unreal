#include "Substrate/ElysiumNpcDialogue.h"

#include "ElysiumContentPaths.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDlgSheet.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"

void FElysiumNpcDialogue::Begin(FElysiumNpc& Npc, EElysiumDialogOpenerKind Opener, int32 RawFlags,
	const FElysiumInputArgs& Args)
{
	if (Npc.IsInert() || bInDialog)
	{
		return;
	}
	// BeginDialogueBodySession performs the scripted-sequence cancellation and body transfer.
	// Keep that ownership transition in one place so direct World::OpenDialog uses the same path.
	if (!Npc.PrepareBodyForDialogue())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s refused dialogue body ownership"),
			*Npc.DebugString());
		return;
	}
	DialogOpener = Opener;
	DialogFlags = RawFlags;
	DecodedDialogFlags = 0;
	if (DialogFlags != 0)
	{
		ElysiumStub::Fired(TEXT("field"), TEXT("CAI_BaseNPC.DialogOpenerInteger"),
			Npc.DebugString(), FString::Printf(TEXT("raw=%d"), DialogFlags),
			TEXT("RE46 — the integer lands in m_flSpecialDistanceAccum; its reader is still open"));
	}
	// The other half of retail's open, the input lock, needs no call here: the conversation screen
	// pushes the `Dialogue` input scope (`ElysiumInput::Priority::Dialogue`, UIOnly) as it
	// activates, which removes the gameplay mapping contexts and is what locks movement. That is
	// the port's one input arbiter; a second lock from the substrate would fight it.

	static const FName OnDialogBegin(TEXT("OnDialogBegin"));
	Npc.FireOutput(OnDialogBegin, Args.Activator);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s %s(raw=%d decoded=%d)"), *Npc.DebugString(),
		ElysiumDialogueCamera::LexToString(Opener), DialogFlags, DecodedDialogFlags);

	// `FUN_10178280` runs `CDialog::Acquire` FIRST and only then remembers whether the active
	// weapon was drawable (`player+0x1e01`) and switches to `item_w_unarmed`; `CDialog::Release`
	// puts it back (`FUN_10178400`). That order is load-bearing here: `OpenConversation` fails
	// exactly where `Acquire` does — no `dialogname`, or a `.dlg` that will not load — and those
	// are the paths that leave `bInDialog` latched on the manual `EndDialog` seam with no
	// conversation to close them. Holstering before the open would strip the player's weapon on
	// them and nothing but a hand-fired `EndDialog` would ever give it back.
	// SC9 — the holster is no longer taken here. `FUN_10178280` runs the whole tail
	// (`SetImmobilized(true)`, the `+0x1e01` latch, the holster, then the camera or the payphone
	// grapple) *inside* the accepted open, and one of `CDialog::Acquire`'s three outcomes — a
	// **bark** (`+0x30e9`, RC6) — sends its line and returns false, so it must take **none** of
	// them. Only the world can tell a bark from a conversation, because only it has the started
	// conversation's response band, so the whole tail lives in
	// `FElysiumEntityWorld::StartPlayerDialogTail` and runs from `OpenDialog`.
	OpenConversation(Npc, Args.Activator, Opener);
}

void FElysiumNpcDialogue::StartForced(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	if (Npc.IsInert() || bInDialog)
	{
		return;
	}
	// The pinned ordinary handler stores this integer at NPC+0x5bac. Its bits are not yet named.
	// A forced opener sets `m_bForceDialogStart` (`npc+0x6495`), which is what makes
	// `FUN_10178280` skip the player-side refusal predicate entirely.
	bForceDialogStart = true;
	Begin(Npc, EElysiumDialogOpenerKind::Forced, Args.Param.ToInt(), Args);
}

void FElysiumNpcDialogue::StartRemote(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	if (Npc.IsInert() || bInDialog)
	{
		return;
	}
	// The pinned Remote handler never reads the input variant; authored `256` is intentionally
	// discarded before the common primitive sees it.
	bForceDialogStart = true;
	Begin(Npc, EElysiumDialogOpenerKind::Remote, 0, Args);
}

void FElysiumNpcDialogue::StartUnforced(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	if (Npc.IsInert() || bInDialog)
	{
		return;
	}
	// `StartPlayerDialogUnforced` is the one input that does NOT set `m_bForceDialogStart`
	// (`npc+0x6495`), so `FUN_10178280` runs the player-side refusal predicate (`0x10178170`) and
	// refuses when it holds. Retail's refusal here is SILENT — no HUD, no sound — because the
	// caller is a script, not the player pressing a key; only the `+use` entry announces itself
	// (M-REFUSE). This closes the `CAI_BaseNPC.StartPlayerDialogUnforcedGate` stub.
	bForceDialogStart = false;
	if (const TCHAR* Refusal = EntryRefusalReason(Npc))
	{
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s StartPlayerDialogUnforced refused: %s"), *Npc.DebugString(), Refusal);
		return;
	}
	Begin(Npc, EElysiumDialogOpenerKind::Unforced, Args.Param.ToInt(), Args);
}

void FElysiumNpcDialogue::End(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	if (!bInDialog)
	{
		return;
	}
	// Two callers reach this input. The world's own teardown queues it after `EndDialogSession`
	// with the closed session's serial as the param; a level script fires it bare
	// (`Jack,EndDialog`) to throw the player out of a running conversation, which in retail lands
	// in `CDialog::Release` (`0x100e5240`): flush the parked script, clear the live state, THEN
	// fire `OnDialogEnd`. So a bare (or void) close while this NPC owns the open session tears the
	// world session down first; a serial that names a session this NPC has already replaced is a
	// stale bookkeeping close — the old conversation did end (count it, fire its output) but the
	// live one stays.
	if (Npc.World)
	{
		const bool bOwnsLive = Npc.World->GetOpenDialogOwner() == Npc.Handle;
		const int32 QueuedSerial = Args.Param.IsInt() ? Args.Param.ToInt() : 0;
		if (bOwnsLive && QueuedSerial != 0
			&& QueuedSerial != static_cast<int32>(Npc.World->GetOpenDialogSerial()))
		{
			++Npc.TimesTalked;
			static const FName OnDialogEndStale(TEXT("OnDialogEnd"));
			Npc.FireOutput(OnDialogEndStale, Args.Activator);
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("%s EndDialog for replaced session %d (live %u stays; times_talked=%d)"),
				*Npc.DebugString(), QueuedSerial, Npc.World->GetOpenDialogSerial(), Npc.TimesTalked);
			return;
		}
		if (bOwnsLive)
		{
			// Flush + teardown. This queues a second EndDialog carrying the serial; it finds
			// `bInDialog` false below and returns.
			Npc.World->CloseDialog(/*bSilent*/ false);
		}
	}
	if (Npc.GetDialogueBodyOwner().IsSet())
	{
		Npc.EndDialogueBodySession(Npc.GetDialogueBodyOwner(), /*bSilent=*/false);
	}
	bInDialog = false;
	bForceDialogStart = false;
	// `CDialog::Release` (`0x100e5240`) unlocks input and restores the holstered weapon
	// (`FUN_10178400`). The input half is the screen's — deactivating the conversation screen pops
	// the `Dialogue` input scope — so the weapon is what this door owes.
	if (FElysiumPlayer* DialoguePlayer = Npc.World ? Npc.World->FindPlayer() : nullptr)
	{
		DialoguePlayer->RestoreDialogHolster();
	}
	++Npc.TimesTalked;
	static const FName OnDialogEnd(TEXT("OnDialogEnd"));
	Npc.FireOutput(OnDialogEnd, Args.Activator);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s EndDialog (times_talked=%d)"), *Npc.DebugString(), Npc.TimesTalked);
	// Unconditional: leaving a conversation releases the stance machine's talking branch, so a
	// standing character has a decision to make on the very next think.
	Npc.NextThink = static_cast<float>(Npc.World ? Npc.World->NowSeconds() : 0.0);
}

// --- Use-to-talk: `CBasePlayer::PlayerUse` (`0x10167850`) ------------------------------------
//
// Retail resolves the use target, tests the character's `WillTalk` latch (virtual `+0x49c`, set by
// `InputWillTalk` `0x103418f0`), clears the NPC schedule, pushes AI schedule `0x6a` and calls
// player vtable slot 414 (`FUN_10178280`, the real StartDialog). The schedule clear and the `0x6a`
// push are `BeginDialogueBodySession`'s job here — it already cancels the scripted sequence, ends
// the pushed schedule and takes the Dialogue body claim — so this half is the eligibility test and
// the refusal.

FString FElysiumNpcDialogue::Name(const FElysiumNpc& Npc) const
{
	return Npc.Def ? Npc.Def->Keys.FindRef(TEXT("dialogname")) : FString();
}

bool FElysiumNpcDialogue::IsUsable(const FElysiumNpc& Npc) const
{
	// The class verb: this NPC has a conversation to open. `CBasePlayer::PlayerUse` resolves the
	// target first and only then tests the latches, so "usable" and "will talk right now" are two
	// different questions and the second one is `CanPlayerFocus`'s.
	return !Name(Npc).IsEmpty();
}

bool FElysiumNpcDialogue::CanPlayerFocus(const FElysiumNpc& Npc, const FElysiumUseContext& Context) const
{
	if (!IsUsable(Npc) || Npc.IsInert())
	{
		return false;
	}
	// The `WillTalk` latch (virtual `+0x49c`). 79 authored calls: a character is opened for
	// conversation by a script, not by a player standing near it.
	if (!Npc.bWillTalk)
	{
		return false;
	}
	// A conversation already open on this character is not a second target.
	if (bInDialog)
	{
		return false;
	}
	// The two remaining common guards `CBasePlayer::PlayerUse` and `CAI_BaseNPCTroika::StartTask`
	// share. Both are seams today (see the declarations) and neither blocks yet.
	if (Npc.IsBusyWithDiscipline() || Npc.HasDialogSuppressFlag())
	{
		return false;
	}
	return true;
}

const TCHAR* FElysiumNpcDialogue::EntryRefusalReason(const FElysiumNpc& Npc) const
{
	// `FUN_10178280`: the predicate only runs when `m_bForceDialogStart` is clear.
	if (bForceDialogStart)
	{
		return nullptr;
	}
	// The NPC-side guards, restated here because the scripted openers never went through
	// `CanPlayerFocus` — a script fires the input straight at the character.
	if (Npc.IsBusyWithDiscipline())
	{
		return TEXT("the character is busy with a discipline");
	}
	if (Npc.HasDialogSuppressFlag())
	{
		return TEXT("the character refuses conversation");
	}
	// The player half (`0x10178170`). A world with no player refuses: retail's very first common
	// guard is "a player exists".
	const FElysiumPlayer* RefusingPlayer = Npc.World ? Npc.World->FindPlayer() : nullptr;
	if (!RefusingPlayer)
	{
		return TEXT("there is no player");
	}
	return RefusingPlayer->DialogRefusalReason();
}

FElysiumUseBeginResult FElysiumNpcDialogue::BeginPlayerUse(FElysiumNpc& Npc, const FElysiumUseContext& Context)
{
	// `+use` never sets `m_bForceDialogStart`, so the predicate stands unless a scripted opener
	// left the byte set on this character (retail's byte is per-NPC and persists until an unforced
	// start or the close clears it).
	if (const TCHAR* Refusal = EntryRefusalReason(Npc))
	{
		// M-REFUSE (named modernization): retail's refusal is silent. The port posts one brief HUD
		// notification through the existing FIFO so the player learns the conversation was offered
		// and declined rather than that use is broken.
		if (Npc.World)
		{
			if (IElysiumPresenter* Presenter = Npc.World->Presenter())
			{
				FElysiumNotification Notice;
				Notice.Kind = EElysiumNotificationKind::Generic;
				Notice.Subject = ElysiumDialogue::RefusalNotice;
				Presenter->PostNotification(Notice);
			}
		}
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s +use refused: %s"), *Npc.DebugString(), Refusal);
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}

	FElysiumInputArgs Args;
	Args.Activator = Context.Activator;
	Args.Caller = Context.Activator;
	Begin(Npc, EElysiumDialogOpenerKind::Use, 0, Args);
	// Completed, never SessionStarted: dialogue holds the body through its own owner token and the
	// world's `DialogueSession` already refuses a second use edge. A lingering +use session would
	// be a second claim on the same interaction with its own teardown.
	return FElysiumUseBeginResult::Completed();
}

int32 FElysiumNpcDialogue::ResolveUseIcon(const FElysiumNpc& Npc, const FElysiumEntityHandle& Activator) const
{
	// `hud/Context_Icons/Talk_Female` (14) and `Talk_Male` (15) in the recovered use_icon table
	// (`ElysiumUseIconName`). An authored `use_icon` on the definition wins — a character carrying
	// a scripted panel glyph keeps it.
	if (const int32 Authored = Npc.GetUseIcon(); Authored != 0)
	{
		return Authored;
	}
	if (!IsUsable(Npc))
	{
		return 0;
	}
	static constexpr int32 TalkFemaleIcon = 14;
	static constexpr int32 TalkMaleIcon = 15;
	return Npc.Sheet.IsMale() ? TalkMaleIcon : TalkFemaleIcon;
}

bool FElysiumNpcDialogue::OpenConversation(FElysiumNpc& Npc, const FElysiumEntityHandle& Activator,
	EElysiumDialogOpenerKind Opener)
{
	if (!Npc.World || !Npc.Def)
	{
		return false;
	}
	const FString DialogName = Npc.Def->Keys.FindRef(TEXT("dialogname"));
	if (DialogName.IsEmpty())
	{
		return false;   // an NPC with no dialogue file — nothing to open
	}

	const FString Path = FElysiumContentPaths::DlgFromDialogname(DialogName);
	TSharedRef<FElysiumDlgFile> DlgFile = MakeShared<FElysiumDlgFile>();
	FString Err;
	if (!FElysiumDlgFile::LoadFile(Path, DlgFile.Get(), &Err))
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s dialog load failed: %s"), *Npc.DebugString(), *Err);
		return false;
	}

	// Player gender + clan drive text selection: VtMB shows col-2 for a female PC and the player's
	// own clan column when that column is filled. `clan_offset` is the .dlg column order, which is
	// NOT the 2..8 sheet encoding — the join lives in `ElysiumDlgClan::OffsetFromSheetClan`.
	const UElysiumSessionSubsystem* GameState = Npc.World->GetGameState();
	const bool bMale = GameState ? GameState->PlayerSheet().IsMale() : true;
	const int32 ClanOffset = GameState
		? ElysiumDlgClan::OffsetFromSheetClan(GameState->PlayerSheet().Clan())
		: ElysiumDlgClan::None;
	const FElysiumEntityHandle Self = Npc.Handle;
	FElysiumEntityWorld* W = Npc.World;

	// Field-4 conditions eval, field-4(NPC)/field-5 actions exec — both through the installed host
	// (EvalCondition also execs statements), so they land in the same `G` the level script reads and
	// obey the same live/off switch and eval log as field-6. dlgexpr -> Python via the normalizer.
	auto Cond = [W, Self, Activator](const FString& Raw) -> bool
	{
		// `CDialogDependency::TestPython` (`0x100e9ff0`) — CallPyDialogFunction in Py_eval_input
		// mode, TRUE only for a non-zero Python integer: the exact logic_pythoncheck/terminal/sign
		// rule, not generic truthiness. A non-integer result (a string, the float 1.0) and an eval
		// error both read FALSE, so the line is unavailable rather than wrongly offered.
		// Error-to-false still logs inside EvalCondition/the host.
		//
		// Only the PYTHON half of a dependency reaches here — the skill check is answered off the
		// sheet by `FElysiumDlgDependency`, which is why `Humanity -8` finally works. The
		// normalizer still runs because a Python half may carry parens, `and`/`or` and the odd
		// stray join it must translate.
		return W->EvalCondition(ElysiumDlgExpr::ConditionToPython(Raw), Self, Activator).IsPythonCheckTrue();
	};
	auto Act = [W, Self, Activator](const FString& Raw)
	{
		W->EvalCondition(ElysiumDlgExpr::ActionToPython(Raw), Self, Activator);
	};
	const FString DialogUseScript = Npc.UseScript;
	auto StartFallback = [W, Self, Activator, DialogUseScript]() -> TOptional<int32>
	{
		if (DialogUseScript.IsEmpty())
		{
			return TOptional<int32>();   // no usescript: retail's default is line 1
		}
		const FElysiumVariant Result = W->EvalCondition(DialogUseScript, Self, Activator);
		// CallPyDialogFunc accepts only a Python int; every other result (including an error/None)
		// returns 0 and lets CDialog::Acquire apply its first-stored-line fallback.
		return Result.IsInt() ? Result.ToInt() : 0;
	};

	TSharedRef<FElysiumDlgConversation> Conv =
		MakeShared<FElysiumDlgConversation>(DlgFile, bMale, /*bMalkavian*/ false, MoveTemp(Cond),
			MoveTemp(Act), MoveTemp(StartFallback));
	// The dependency's two injected halves: the rulebook names the trait's class and id, the
	// player's own sheet answers the ratings, the blood pool and the sex/clan gates, and the charge
	// on pick lands on this NPC. Bound before Start(), because the opener's own starting-condition
	// sentinels are dependencies too.
	Conv->SetGateContext(ElysiumDlgSheet::MakePlayerSheet(*W, W->PlayerHandle(), Self),
		ElysiumDlgSheet::MakeTraitResolver(GameState ? GameState->Rulebook() : nullptr),
		ClanOffset);
	// UN-STARTED. `OpenDialog` calls `Start()` itself once the world's session exists, because the
	// opening NPC line's col-4 is an action that may `EndDialog`/`OpenDialog` and must act on this
	// conversation, not on the one it replaced (retail runs it inside `CDialog::Acquire`).
	Npc.World->OpenDialog(Self, Conv, Opener, DialogFlags, Npc.DefaultCamera, Npc.GetDialogueBodyOwner());
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s opened dialogue '%s' (%d rows)"),
		*Npc.DebugString(), *DialogName, DlgFile->Lines.Num());
	return true;
}
