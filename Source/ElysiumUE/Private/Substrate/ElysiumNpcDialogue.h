#pragma once

#include "CoreMinimal.h"

#include "ElysiumDialogueCamera.h"
#include "ElysiumEntityHandle.h"   // the speech scene this NPC is bound to

class FElysiumNpc;
struct FElysiumEntityHandle;
struct FElysiumInputArgs;
struct FElysiumUseBeginResult;
struct FElysiumUseContext;

// --- How a dialogue holds an NPC (V3d; packets R1, R2) ----------------------------------------
//
// No body claim and no state change. The three `StartPlayerDialog*` inputs and `+use`
// (`CBasePlayer::PlayerUse 0x10167850`) INSTALL A PROGRAM on the NPC through `0x102ae750(id, 0)`
// (`0x6d` / `0x6e` / `0x6a`); the conversation opens inside the player's start-dialog
// (`FUN_10178280`, player slot 414) -> `CDialog::Acquire 0x100e05f0` -> the NPC's `StartTalking
// 0x102c0270`; `TASK_RUN_DIALOG 0xb9` holds through `0x102c1400`; the close is `CDialog::Release
// 0x100e5240` -> the NPC's `0x102c0360`, which fires `OnDialogEnd` once.

// The dialogue and use-to-talk half of `FElysiumNpc`. Owned by value on the leaf, the
// `FElysiumNpcSenses` / `FElysiumNpcWitness` posture: every entry point takes the owning NPC so
// the object holds no back pointer to rebind across a save.
struct FElysiumNpcDialogue
{
	// Port bookkeeping, no retail word: set by `StartTalking`, cleared by `0x102c0360` and the silent
	// close. It answers none of `IsInDialog`'s four terms (`0x102c1170`).
	bool bInDialog = false;
	// Which entry asked for the conversation. Port bookkeeping for the start-dialog's two port
	// behaviours (M-REFUSE's notice on `+use`, the session's debug opener); retail carries none.
	EElysiumDialogOpenerKind DialogOpener = EElysiumDialogOpenerKind::Remote;

	// `m_bForceDialogStart` (`npc+0x6495`). `FUN_10178280` refuses a conversation when this is
	// CLEAR and the player-side refusal predicate holds; a set byte opens regardless. The forced
	// openers (`StartPlayerDialog`, `StartPlayerDialogRemote`) set it, `StartPlayerDialogUnforced`
	// clears it, `+use` never touches it, and `StartTalking 0x102c0270` clears it at the open.
	bool bForceDialogStart = false;

	// --- The retail words, declared and unwritten ------------------------------------------------
	//
	// Every word of `CAI_BaseNPCTroika` this struct owns that no port system writes yet
	// (`docs/vtmb/npc-kernel/layout.md`), default-initialised, each carrying its offset, its
	// retail name and the tier that typed it. They are the shape 29b landed so a later story
	// fills a member instead of inventing one; `ElysiumNpcKernelShapeMap.cpp` binds every one of
	// them to its offset and the shape test fails if one goes missing.
	FString DialogQue;  // +0x64ec m_szDialogQue (datamap) — char[96] used as a queued line name
	bool bDialogQueIsFinal = false;  // +0x654c m_bDialogQueIsFinal (walked)
	float SpeechVolume = 0.f;  // +0x6550 m_flSpeechVol (datamap)
	// +0x6554 m_hDialogScene (doc) — the speech scene this NPC is bound to
	FElysiumEntityHandle DialogScene;

	// --- `0x102c0aa0`, story 29c-1 family Anim ---------------------------------------------------
	//
	// The oracle names it `IsTalking()` (`docs/vtmb/game_runtime.md` — the dialogue box hides the
	// player's choices while it answers true; `docs/vtmb/npc-ai/lifecycle.md` — `UpdateCharacter`
	// calls `FinishTalking` when it answers FALSE and `m_bIsTalking` is set). It is NOT
	// `IsInDialog` (`0x102c1170`), which is the four-term session gate.
	//
	// Two arms, in retail's order:
	//   1. `m_hDialogScene` (+0x6554) resolves to a live entity AND that entity's `+0x498` byte is
	//      set -> TRUE, without consulting the clock at all.
	//   2. otherwise `curtime < m_flTalkEnd` (+0x64cc) — STRICTLY less, so the stamp's own instant
	//      already answers false.
	//
	// `FElysiumNpc::IsTalking(Now)` beside it is the stamp half alone and spells its comparison
	// `Now <= TalkingUntil`; this is the whole body, and the two disagree by exactly one instant.
	bool IsTalking(const FElysiumNpc& Npc, double Now) const;

	// The scene entity's `+0x498` byte — "this speech scene has finished its line". **SEAM**: the
	// port's `logic_choreographed_scene` carries completion on the scene player rather than as a byte
	// the kernel reads by offset, so this answers false and arm 1 never fires.
	bool DialogSceneReportsDone(const FElysiumNpc& Npc) const;

	// --- `CDialog::PlayWhisper` (`0x100e0a40`), story 29c-1 family Sounds -------------------------
	//
	// `CDialog +0x33ad`, the QUEUED WHISPER TEXT — a char buffer, not a flag. It is written by
	// `CDialog::Acquire` (`0x100e05f0`), `FUN_100e09a0` and `CDialog::load` (`0x100e5410`) as the
	// running conversation reaches a line that carries one, and drained exactly once by
	// `PlayWhisper`. `CDialog` is the conversation object rather than the NPC, and this runtime's
	// conversation state lives on the NPC that owns the session, which is where the word lands.
	// A member 29b did not declare: the `CAI_BaseNPCTroika` shape map covers no `CDialog` word.
	FString PendingWhisper;

	// `CDialog::PlayWhisper(const char* pszSoundName)` (`0x100e0a40`). Three statements inside a
	// VPROF scope: if the queued whisper text is non-empty, resolve the conversation's bound player
	// (`CDialog +0x4`, an `EHANDLE`) and hand it `(text, pszSoundName)` through
	// `CBasePlayer`'s whisper display (`0x10182c40`), then CLEAR the text. A one-shot: a whisper
	// never replays, and a null/dead player still clears it.
	//
	// `0x10182c40` itself copies the text into the HUD's global whisper buffer, points the player's
	// `+0x1ca4` at it, and sets the display deadline `+0x1ca8` to `curtime - k` plus either the
	// named sound's duration (when the player has a live dialogue partner AND the sound name is
	// non-empty) or a fixed fallback, then raises bit 0 of `+0x1cac`.
	void PlayWhisper(FElysiumNpc& Npc, const FString& SoundName);

	// `CBasePlayer::FUN_10178280` (player slot 414), the player's start-dialog, reached from
	// `TASK_START_PLAYER_DIALOG 0xda` (`0x102a4eab`) and from `+use` (`0x10167ac4`). With
	// `m_bForceDialogStart` clear and the player-side predicate `0x10178170` holding it refuses
	// (`0x101cebc0`) and clears the player's partner; otherwise `CDialog::Acquire` (`OpenConversation`
	// -> `FElysiumEntityWorld::OpenDialog`, which runs `StartTalking` and the player's tail), and a
	// failed acquire clears the player's partner too. The opener is `DialogOpener`.
	void PlayerStartDialog(FElysiumNpc& Npc, const FElysiumEntityHandle& Player);

	// `CAI_BaseNPCTroika::StartTalking` (`0x102c0270`), called once from inside the open
	// (`CDialog::Acquire 0x100e05f0`, after the `.dlg` loads and before the starting line). In the
	// listing's order: `m_bForceDialogStart = 0`, `m_bCutsceneForceLOD = 1`, `m_nTimesTalked += 1`,
	// `FinishTalking`, `SetDialogPartner(player)` (the only writer of the NPC's `+0xfe8`), slot 306
	// `LookAtEntity(player, false)` unless spawnflag 8, `CancelScript` on a live `m_hCine`, then
	// `m_OnDialogBegin` (activator the player, caller the NPC).
	void StartTalking(FElysiumNpc& Npc, const FElysiumEntityHandle& Player);

	// The silent close (a replacement, a map teardown, the owner's death or dormancy). Whether retail
	// reaches `CDialog::Release` -> `0x102c0360` on those paths is UNRECOVERED (packets R1/R2); the
	// port clears what `0x102c0360` clears (`m_bCutsceneForceLOD`, the partner) WITHOUT firing
	// `OnDialogEnd`, which is the old silent close's posture, named here rather than guessed.
	void ReleaseWithoutOutput(FElysiumNpc& Npc);

	// `CDialog::ShowPlayerChoices` (`0x100e13d0`), sent by `0x102c1400` step 6 every tick: a reliable
	// user message 7 carrying the byte. SEAM, answering nothing: the port's dialogue box decides its
	// own choice visibility off the world's session, and no door from the kernel into it exists.
	void ShowPlayerChoices(const FElysiumNpc& Npc, bool bShow) const;

	// The three `StartPlayerDialog*` inputs (`0x1029ef80`, `0x1029f060`, `0x1029f120`; packet R2
	// items 1-3). Each a distinct body: the common guards, then the `+0x5bac` float (Forced and
	// Unforced), `FinishTalking`, slot 614, `m_bForceDialogStart` (1 / 1 / 0), and the program
	// installed through `0x102ae750(id, 0)` — `0x6d` / `0x6e` / `0x6d`. They open nothing.
	void StartForced(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	void StartRemote(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	void StartUnforced(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	// The `EndDialog` input: a bare script close. When this NPC owns the open conversation it is
	// `CDialog::Release` (`FElysiumEntityWorld::CloseDialog`), which runs `0x102c0360` once; nothing
	// else. Whether `EndDialog` is a retail input at all is V7's question.
	void End(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	// The authored `dialogname`. Empty when this NPC carries no conversation.
	FString Name(const FElysiumNpc& Npc) const;

	// True when there is a conversation to open at all. Deliberately independent of `bWillTalk`:
	// the class verb is "this thing talks", the eligibility is `CanPlayerFocus`'s.
	bool IsUsable(const FElysiumNpc& Npc) const;

	// `bWillTalk && !bInDialog && !IsInert() && !IsBusyWithDiscipline()` and the AINPCFlags2 bit.
	bool CanPlayerFocus(const FElysiumNpc& Npc, const FElysiumUseContext& Context) const;

	// The `FUN_10178280` refusal test: nullptr when the conversation may open, otherwise the reason.
	// `bForceDialogStart` short-circuits it, as retail's byte does; otherwise it is the player-side
	// predicate `0x10178170` alone (the NPC-side guards are the inputs' and `CanTalk`'s).
	const TCHAR* EntryRefusalReason(const FElysiumNpc& Npc) const;

	// `CBasePlayer::PlayerUse 0x10167850`'s NPC arm (packet R2 item 5): slot 295 `CanTalk` (false
	// returns with no ordinary use and no notice), slot 614, `0x102ae750(npc, 0x6a, 0)` — no
	// `ClearSchedule` — then the player's start-dialog. Always a terminal result: a conversation is
	// held by the program and the partner word, so no +use session may linger.
	FElysiumUseBeginResult BeginPlayerUse(FElysiumNpc& Npc, const FElysiumUseContext& Context);

	// The talk glyph (`hud/Context_Icons/Talk_Male` / `Talk_Female`, use_icon 15 / 14) when the
	// definition authored no `use_icon` of its own.
	int32 ResolveUseIcon(const FElysiumNpc& Npc, const FElysiumEntityHandle& Activator) const;

	// `CDialog::Acquire`'s load half: this NPC's `dialogname` `.dlg`, a branch conversation bound to
	// the installed script host, handed to the world (`OpenDialog` runs `StartTalking` and the rest of
	// `Acquire`). Returns false when there is no dialogue to run — `Acquire` answering 0, before
	// `StartTalking`, so the NPC's words are untouched.
	bool OpenConversation(FElysiumNpc& Npc, const FElysiumEntityHandle& Activator,
		EElysiumDialogOpenerKind Opener);

	// The inputs' shared body, keyed by which of the three it is (R2 items 1-3).
	void StartFromInput(FElysiumNpc& Npc, const FElysiumInputArgs& Args, EElysiumDialogOpenerKind Opener);
};
