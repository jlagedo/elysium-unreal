#pragma once

#include "CoreMinimal.h"

#include "ElysiumDialogueCamera.h"
#include "ElysiumEntityHandle.h"   // the speech scene this NPC is bound to

class FElysiumNpc;
struct FElysiumEntityHandle;
struct FElysiumInputArgs;
struct FElysiumUseBeginResult;
struct FElysiumUseContext;

// --- Use-to-talk (`CBasePlayer::PlayerUse`, `0x10167850`) ---------------------------------
//
// Retail resolves the use target, tests the character's `WillTalk` latch (virtual `+0x49c`,
// set by `InputWillTalk` `0x103418f0`), clears the schedule, pushes AI schedule `0x6a` and
// calls player vtable slot 414. Here the eligibility half is `CanPlayerFocus` (so the reticle
// and the prompt agree with what pressing use will do) and the transaction half is
// `BeginPlayerUse`, which routes into the same `BeginDialog` primitive the inputs use.

// The dialogue and use-to-talk half of `FElysiumNpc`. Owned by value on the leaf, the
// `FElysiumNpcSenses` / `FElysiumNpcWitness` posture: every entry point takes the owning NPC so
// the object holds no back pointer to rebind across a save.
struct FElysiumNpcDialogue
{
	bool  bInDialog = false;          // a dialog session is open (OnDialogBegin fired, OnDialogEnd pending)
	int32 DialogFlags = 0;            // raw arg on ordinary/unforced; Remote ignores its variant
	int32 DecodedDialogFlags = 0;     // no bit is named until RE46 closes it
	EElysiumDialogOpenerKind DialogOpener = EElysiumDialogOpenerKind::Remote;

	// `m_bForceDialogStart` (`npc+0x6495`). `FUN_10178280` refuses a conversation when this is
	// CLEAR and the player-side refusal predicate holds; a set byte opens regardless. The forced
	// openers (`StartPlayerDialog`, `StartPlayerDialogRemote`) set it, `StartPlayerDialogUnforced`
	// clears it, and `+use` never touches it — so use and unforced are the two gated entries.
	// Session state: retail's byte is not in the datamap's save block either.
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

	void Begin(FElysiumNpc& Npc, EElysiumDialogOpenerKind Opener, int32 RawFlags,
		const FElysiumInputArgs& Args);

	// K1: each Tier-1 name enters through its own handler. Only after that handler has applied the
	// recovered parameter posture does it join the shared dialogue-session primitive above.
	void StartForced(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	void StartRemote(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	void StartUnforced(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	// The dialog session ends: increment times_talked and fire OnDialogEnd. Reached both by the runner
	// (World::EndDialogSession routes EndDialog to `!self` when the conversation closes) and by a manual
	// ent_fire. Jack's OnDialogEnd wires DialogPostProcess(), which reads the `G` flags the dialogue's
	// field-5 actions wrote and warps the player.
	void End(FElysiumNpc& Npc, const FElysiumInputArgs& Args);

	// The authored `dialogname`. Empty when this NPC carries no conversation.
	FString Name(const FElysiumNpc& Npc) const;

	// True when there is a conversation to open at all. Deliberately independent of `bWillTalk`:
	// the class verb is "this thing talks", the eligibility is `CanPlayerFocus`'s.
	bool IsUsable(const FElysiumNpc& Npc) const;

	// `bWillTalk && !bInDialog && !IsInert() && !IsBusyWithDiscipline()` and the AINPCFlags2 bit.
	bool CanPlayerFocus(const FElysiumNpc& Npc, const FElysiumUseContext& Context) const;

	// The whole `FUN_10178280` refusal test from this NPC's side: nullptr when the conversation may
	// open, otherwise the reason. `bForceDialogStart` short-circuits it, as retail's byte does.
	const TCHAR* EntryRefusalReason(const FElysiumNpc& Npc) const;

	// Open the conversation, or refuse with the M-REFUSE notification. Always returns a terminal
	// result: dialogue owns the body through its own token, so no +use session may linger.
	FElysiumUseBeginResult BeginPlayerUse(FElysiumNpc& Npc, const FElysiumUseContext& Context);

	// The talk glyph (`hud/Context_Icons/Talk_Male` / `Talk_Female`, use_icon 15 / 14) when the
	// definition authored no `use_icon` of its own.
	int32 ResolveUseIcon(const FElysiumNpc& Npc, const FElysiumEntityHandle& Activator) const;

	// Load this NPC's `dialogname` `.dlg`, open a branch conversation bound to the installed script host,
	// and hand it to the world (the visual-novel box renders it; the runner fires EndDialog on close).
	// Returns false when there is no dialogue to run, leaving bInDialog latched for the manual seam.
	bool OpenConversation(FElysiumNpc& Npc, const FElysiumEntityHandle& Activator,
		EElysiumDialogOpenerKind Opener);
};
