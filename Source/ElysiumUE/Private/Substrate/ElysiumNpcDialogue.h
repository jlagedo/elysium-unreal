#pragma once

#include "CoreMinimal.h"

#include "ElysiumDialogueCamera.h"

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
