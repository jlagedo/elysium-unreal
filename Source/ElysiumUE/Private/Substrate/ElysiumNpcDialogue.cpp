#include "Substrate/ElysiumNpcDialogue.h"

#include "ElysiumContentPaths.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDlgSheet.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
	// `.rdata 0x104454c4`, the float the inputs store at `+0x5bac` when their variant is not a
	// float (`FLD [0x104454c4]`, `0x1029efed`). 1,331 readers and no writer; its four bytes in the
	// pinned `vampire.dll` (file offset `0x4454c4`) are `00 00 00 00`: 0.0f.
	constexpr float GDialogSpecialDistanceFallback = 0.0f;

	// The three schedule ids, pinned by the registration table `0x102b9810` (packet R2 item 2).
	constexpr int32 GSchedTroikaRunDialog = 0x6a;                 // SCHED_TROIKA_RUN_DIALOG
	constexpr int32 GSchedTroikaStartPlayerDialog = 0x6d;         // SCHED_TROIKA_START_PLAYER_DIALOG
	constexpr int32 GSchedTroikaStartPlayerDialogRemote = 0x6e;   // ..._START_PLAYER_DIALOG_REMOTE

	// `CBaseEntity +0x204` (`m_spawnflags`) bit 3: `StartTalking` skips slot 306 when it is set.
	constexpr int32 GStartTalkingNoLookSpawnflag = 0x8;
}

void FElysiumNpcDialogue::PlayerStartDialog(FElysiumNpc& Npc, const FElysiumEntityHandle& Player)
{
	// `CBasePlayer::FUN_10178280` (slot 414). The port has one player, so the receiver is the
	// world's; `Player` is the handle the caller resolved it by.
	FElysiumPlayer* PlayerEnt = Npc.World != nullptr ? Npc.World->FindPlayer() : nullptr;
	// `npc+0x6495 == 0 && 0x10178170(player)` -> the refusal `0x101cebc0`, then the fall-through
	// `SetDialogPartner(player, NULL)`.
	if (const TCHAR* Refusal = EntryRefusalReason(Npc))
	{
		// M-REFUSE (named modernization, `+use` only): retail's refusal is silent. The port posts
		// one brief HUD notification so the player learns the conversation was offered and declined
		// rather than that use is broken; a script's opener stays silent, as retail's.
		if (DialogOpener == EElysiumDialogOpenerKind::Use && Npc.World != nullptr)
		{
			if (IElysiumPresenter* Presenter = Npc.World->Presenter())
			{
				FElysiumNotification Notice;
				Notice.Kind = EElysiumNotificationKind::Generic;
				Notice.Subject = ElysiumDialogue::RefusalNotice;
				Presenter->PostNotification(Notice);
			}
		}
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s %s refused: %s"), *Npc.DebugString(),
			ElysiumDialogueCamera::LexToString(DialogOpener), Refusal);
		if (PlayerEnt != nullptr)
		{
			PlayerEnt->SetDialogPartner(FElysiumEntityHandle::Invalid());       // 0x10107050(player, NULL)
		}
		return;
	}
	// `CDialog::Acquire(dialog, player, npc)`. Its accepted tail — `SetDialogPartner(player, npc)`,
	// `SetImmobilized`, the holster latch, the camera or the payphone grapple — runs inside the
	// world's open (`StartPlayerDialogTail`), because only the world can tell a bark (`+0x30e9`) from
	// a conversation. The input lock is the conversation screen's `Dialogue` input scope.
	if (!OpenConversation(Npc, Player, DialogOpener))
	{
		// `Acquire` answered 0 (no `dialogname`, a `.dlg` that will not load): the same fall-through.
		if (PlayerEnt != nullptr)
		{
			PlayerEnt->SetDialogPartner(FElysiumEntityHandle::Invalid());       // 0x10107050(player, NULL)
		}
	}
}

void FElysiumNpcDialogue::StartTalking(FElysiumNpc& Npc, const FElysiumEntityHandle& Player)
{
	// `CAI_BaseNPCTroika::StartTalking` `0x102c0270`, statement for statement (`vtmb_code`).
	bForceDialogStart = false;                                                  // +0x6495 = 0
	Npc.bCutsceneForceLOD = true;                                               // +0x1590 = 1
	++Npc.TimesTalked;                                                          // +0x64bc += 1
	Npc.FinishTalking();                                                        // 0x102c0ca0
	Npc.SetDialogPartner(Player);                                               // 0x10107050(this, [player+0xa8])
	FElysiumEntity* PlayerEntity = Npc.World != nullptr ? Npc.World->Resolve(Player) : nullptr;
	if ((Npc.SpawnFlags & GStartTalkingNoLookSpawnflag) == 0)                   // +0x204 >> 3 & 1
	{
		Npc.LookAtEntity(PlayerEntity, false);                                  // slot 306 (+0x4c8)(partner, 0)
	}
	// A live `m_hCine` (`+0x5d74`) -> `CancelScript 0x101a8c30`, whatever its interruptable bit.
	if (Npc.World != nullptr && Npc.ScriptOwner.IsSet())
	{
		if (FElysiumEntity* Cine = Npc.World->Resolve(Npc.ScriptOwner))
		{
			Cine->CancelScriptedSequenceForDialogue(Npc.Handle);
		}
	}
	static const FName OnDialogBegin(TEXT("OnDialogBegin"));
	Npc.FireOutput(OnDialogBegin, Player);                                     // +0x5f44, 0x100cd660
	bInDialog = true;                                                          // port bookkeeping
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s StartTalking (%s, times_talked=%d)"),
		*Npc.DebugString(), ElysiumDialogueCamera::LexToString(DialogOpener), Npc.TimesTalked);
}

void FElysiumNpcDialogue::ReleaseWithoutOutput(FElysiumNpc& Npc)
{
	// SEAM (unrecovered): the silent close's NPC half. `0x102c0360`'s writes without its output.
	if (Npc.GetDialogPartner().IsSet())
	{
		Npc.bCutsceneForceLOD = false;                                          // as 0x102c0360's +0x1590
		Npc.SetDialogPartner(FElysiumEntityHandle::Invalid());                  // as 0x102c0360's clear
	}
	bInDialog = false;
}

void FElysiumNpcDialogue::ShowPlayerChoices(const FElysiumNpc& Npc, bool bShow) const
{
	// SEAM: `CDialog::ShowPlayerChoices` `0x100e13d0` on `0x10178120([partner+0xa8])`, the player's
	// `CDialog`. The port's dialogue box reads its band off the world's session and has no door for
	// this byte; the call is made at retail's place and answers nothing.
	(void)Npc;
	(void)bShow;
}

void FElysiumNpcDialogue::StartFromInput(FElysiumNpc& Npc, const FElysiumInputArgs& Args,
	EElysiumDialogOpenerKind Opener)
{
	// The common guards (`0x1029ef80` / `0x1029f060` / `0x1029f120`), in order, each a silent return.
	FElysiumPlayer* PlayerEnt = Npc.World != nullptr ? Npc.World->FindPlayer() : nullptr;
	if (PlayerEnt == nullptr)                                                   // UTIL_PlayerByIndex(1), 0x101cd9e0
	{
		return;
	}
	if (PlayerEnt->GetDialogPartner().IsSet()
		&& Npc.World->Resolve(PlayerEnt->GetDialogPartner()) != nullptr)       // player +0xfe8 live
	{
		return;
	}
	if (Npc.IsBusyWithDiscipline())                                             // 0x1000caae
	{
		return;
	}
	if (Npc.NpcFlags.Has(EElysiumNpcFlag2::NO_DIALOG_PERSISTENT))               // +0x14bc & 0x10000000
	{
		return;
	}
	if (Opener == EElysiumDialogOpenerKind::Unforced && PlayerEnt->DialogRefusalReason() != nullptr)
	{
		return;                                                                 // Unforced only: !0x10178170(player)
	}
	// `+0x5bac` `m_flSpecialDistanceAccum`, Forced and Unforced only: the variant's float when its
	// type is `FIELD_FLOAT`, else `.rdata 0x104454c4`. Remote never reads its variant. Its one reader
	// is `TASK_WALK_RUN_PATH_FOR_DIALOG 0xd9`'s walk-or-run threshold.
	if (Opener != EElysiumDialogOpenerKind::Remote)
	{
		Npc.SpecialDistanceAccum = Args.Param.IsFloat() ? Args.Param.AsFloat
			: GDialogSpecialDistanceFallback;                                   // 0x1029efed FSTP [ESI+0x5bac]
	}
	Npc.FinishTalking();                                                        // 0x102c0ca0
	Npc.ResetThinkTimers(Npc.World->NowSeconds());                              // slot 614 (+0x998)
	bForceDialogStart = Opener != EElysiumDialogOpenerKind::Unforced;           // +0x6495 = 1 / 1 / 0
	// The debug stamp `+0x1b30` (`AI_BaseNPCTroika.cpp`) / `+0x1b34` (line) is not ported.
	DialogOpener = Opener;
	// `0x102ae750(id, 0)`: translate, `0x102ae780`'s refusals, `ForceScheduleChange 0x102ae490` (the
	// live cine's cancel, `PRESERVE_PATH`, slot 435 `OnScheduleChange`), install. Not a forced
	// `SetSchedule(id, true)`.
	Npc.SetSchedule(Opener == EElysiumDialogOpenerKind::Remote
		? GSchedTroikaStartPlayerDialogRemote : GSchedTroikaStartPlayerDialog, /*bForce=*/false);
}

void FElysiumNpcDialogue::StartForced(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	// `InputStartPlayerDialog` `0x1029ef80`: the float, `+0x6495 = 1`, schedule `0x6d`.
	StartFromInput(Npc, Args, EElysiumDialogOpenerKind::Forced);
}

void FElysiumNpcDialogue::StartRemote(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	// `InputStartPlayerDialogRemote` `0x1029f060`: the variant is never read (the tutorial's authored
	// `256` is ignored), `+0x6495 = 1`, schedule `0x6e` (`PUSH 0x6e`, `0x1029f0c7`).
	StartFromInput(Npc, Args, EElysiumDialogOpenerKind::Remote);
}

void FElysiumNpcDialogue::StartUnforced(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	// `InputStartPlayerDialogUnforced` `0x1029f120`: the player-side predicate `0x10178170` as one
	// more guard (silent: the caller is a script), the float, `+0x6495 = 0`, schedule `0x6d`.
	StartFromInput(Npc, Args, EElysiumDialogOpenerKind::Unforced);
}

void FElysiumNpcDialogue::End(FElysiumNpc& Npc, const FElysiumInputArgs& Args)
{
	(void)Args;
	// A level script fires `EndDialog` bare (`Jack,EndDialog`) to throw the player out of a running
	// conversation: `CDialog::Release` (`0x100e5240`) when this NPC owns the open one — the flush, the
	// window, `0x102c0360` (the one `OnDialogEnd`), `EndPlayerDialog`. Nothing else: no count (the
	// count is `StartTalking`'s), no holster (`EndPlayerDialog`'s), no think re-arm (retail re-bases
	// only at the start). The world no longer queues this input on its own close.
	if (Npc.World != nullptr && Npc.World->GetOpenDialogOwner() == Npc.Handle)
	{
		Npc.World->CloseDialog(/*bSilent*/ false);
	}
}

// --- Use-to-talk: `CBasePlayer::PlayerUse` (`0x10167850`) ------------------------------------
//
// Retail resolves the use target and, for an NPC, asks slot 295 `CanTalk(player)`; true resets
// the think timers (slot 614), installs `0x6a SCHED_TROIKA_RUN_DIALOG` through `0x102ae750(npc,
// 0x6a, 0)` (no `ClearSchedule`) and calls player slot 414 (`FUN_10178280`, the real StartDialog).
// `CanPlayerFocus` stays the reticle's focus question (its retail word, the Troika class's slot 35,
// is not recovered).

FString FElysiumNpcDialogue::Name(const FElysiumNpc& Npc) const
{
	// `m_iDialog` is a `CBaseEntity` word, so the live member is the answer: a read of the def's
	// keys would miss a runtime write, which retail's mutable `string_t` accepts.
	return Npc.DialogName;
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
	// The player-side predicate `0x10178170` and nothing else: the NPC-side guards are the inputs'
	// common guards and slot 295 `CanTalk`'s, which every path runs before reaching here. A world
	// with no player refuses: retail's very first common guard is "a player exists".
	const FElysiumPlayer* RefusingPlayer = Npc.World ? Npc.World->FindPlayer() : nullptr;
	if (!RefusingPlayer)
	{
		return TEXT("there is no player");
	}
	return RefusingPlayer->DialogRefusalReason();
}

FElysiumUseBeginResult FElysiumNpcDialogue::BeginPlayerUse(FElysiumNpc& Npc, const FElysiumUseContext& Context)
{
	// `0x10167a6d`: the target carries an NPC; slot 295 `CanTalk(player)` (`+0x49c`, `0x102c21c0`).
	// False returns from `PlayerUse`: no ordinary use, no notice.
	FElysiumEntity* Activator = Npc.World != nullptr ? Npc.World->Resolve(Context.Activator) : nullptr;
	if (!Npc.CanTalk(Activator))
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s +use: CanTalk refused"), *Npc.DebugString());
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	Npc.ResetThinkTimers(Npc.World->NowSeconds());                              // 0x10167a9a slot 614
	// The debug stamp (`"player.cpp"`, line `0x1508`) is not ported.
	Npc.SetSchedule(GSchedTroikaRunDialog, /*bForce=*/false);                   // 0x10167aba 0x102ae750(npc, 0x6a, 0)
	// `+use` never touches `m_bForceDialogStart`: the predicate stands unless a scripted opener left
	// the byte set on this character.
	DialogOpener = EElysiumDialogOpenerKind::Use;
	PlayerStartDialog(Npc, Context.Activator);                                  // 0x10167ac4 player slot 414
	// Completed, never SessionStarted: the conversation is held by `0x6a`'s `TASK_RUN_DIALOG` and the
	// partner word, and the world's `DialogueSession` already refuses a second use edge.
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
	const FString& DialogName = Npc.DialogName;
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
	// UN-STARTED. `OpenDialog` calls `StartTalking` and then `Start()` itself once the world's session
	// exists, because the opening NPC line's col-4 is an action that may `EndDialog`/`OpenDialog` and
	// must act on this conversation, not on the one it replaced (retail runs both inside
	// `CDialog::Acquire`). The opener integer is gone (the inputs' value is the `+0x5bac` float), so
	// the session's debug `RawFlags` is 0.
	Npc.World->OpenDialog(Self, Conv, Opener, /*RawFlags=*/0, Npc.DefaultCamera);
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s opened dialogue '%s' (%d rows)"),
		*Npc.DebugString(), *DialogName, DlgFile->Lines.Num());
	return true;
}

// `CDialog::PlayWhisper` (`0x100e0a40`), story 29c-1 family Sounds.
//
// Retail, statement for statement past its VPROF scope:
//
//     if (m_szWhisper[0] == '\0') return;                       // 100e0a7c
//     CBasePlayer* p = EHANDLE_Get(this->m_hPlayer /* +0x4 */); // 100e0a8b, null when dead
//     ShowWhisper(p, m_szWhisper, pszSoundName);                // thunk_FUN_10182c40
//     m_szWhisper[0] = '\0';                                    // one-shot
//
// The clear is UNCONDITIONAL once the text was non-empty: a whisper queued while the player handle
// is dead is consumed and lost, not retried.
//
// SEAM: `ShowWhisper` (`0x10182c40`) writes the HUD's whisper caption — the text into a global
// buffer, the player's `+0x1ca4` pointer at it, the display deadline `+0x1ca8` from the named
// sound's duration (`IEngineSound` slot 12) or a fixed fallback, and bit 0 of `+0x1cac`. This
// runtime has no whisper caption surface at all: `CBasePlayer::InputWhisper` (`0x10171da0`) is a
// different producer and lands on the audio bus, not on a caption. The forward is made and answers
// nothing; the day a caption surface exists it is the one thing this body has to call.
//
// The deadline `ShowWhisper` would stamp is `curtime - 0.5 + (sound length or 0.6)` — the two
// doubles `_DAT_10449270` / `_DAT_10471720`, which `FElysiumNpc::SetPlayerAnim` (the same body,
// `0x10182c40`) carries.
void FElysiumNpcDialogue::PlayWhisper(FElysiumNpc& Npc, const FString& SoundName)
{
	if (PendingWhisper.IsEmpty())
	{
		return;
	}
	const FElysiumPlayer* Player = Npc.World != nullptr ? Npc.World->FindPlayer() : nullptr;
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("%s whisper '%s' (sound '%s') %s"), *Npc.DebugString(), *PendingWhisper, *SoundName,
		Player != nullptr
			? TEXT("has no caption surface to show it on (retail 0x10182c40)")
			: TEXT("dropped: no player (retail's EHANDLE resolves null and the text is cleared "
				"anyway)"));
	PendingWhisper.Reset();
}

// --- `0x102c0aa0`, story 29c-1 family Anim ------------------------------------------------------

bool FElysiumNpcDialogue::DialogSceneReportsDone(const FElysiumNpc& Npc) const
{
	// The bound speech scene's `+0x498` byte. **SEAM**: the port's `logic_choreographed_scene`
	// carries completion on `FElysiumScenePlayer` rather than as a byte the kernel reads by offset,
	// so the answer is "no scene is reporting done" and the clock arm decides on its own.
	// The handle half is real — retail's first test is that `m_hDialogScene` resolves at all — and
	// only the byte behind it is the seam, which is why the resolve is still performed.
	if (!DialogScene.IsSet() || Npc.World == nullptr
		|| Npc.World->Resolve(DialogScene) == nullptr)
	{
		return false;
	}
	// SEAM: `+0x498` has no port member; false is "this scene is not reporting done".
	return false;
}

bool FElysiumNpcDialogue::IsTalking(const FElysiumNpc& Npc, double Now) const
{
	// `0x102c0aa0`, both arms in retail's order. The scene arm short-circuits: a scene that reports
	// done answers true whatever the clock says, and only when there is no such scene does the
	// talk-end stamp decide. The comparison is STRICTLY less, so the stamp's own instant is already
	// "finished" — which is what lets `UpdateCharacter`'s `FinishTalking` fire on the frame the
	// deadline lands rather than one frame later.
	if (DialogSceneReportsDone(Npc))
	{
		return true;
	}
	return Now < Npc.TalkingUntil;
}
