// Story 29c-1, family **Dialogue** — the fourteen layer 0–9 bodies 29c's overlay filed under the
// word "dialogue". The declarations and what the family turned out to be are in
// `Substrate/ElysiumNpcKernelDialogue.inl`; the walked prose is `docs/vtmb/npc-ai/shape.md`
// (`CDialog`, the `CBasePlayer` half, the security camera, the Sabbat slot) and
// `docs/vtmb/npc-ai/conditions-and-states.md` (the payphone gate and the crosswalk rule).
//
// **Three corrections to 29c's one-line walks**, each recovered from the decompiled C and asserted
// by name in the suite:
//
//   1. `0x102a0d20` re-arms `m_flNextCrosswalkUpdateTime` at `curtime + _DAT_104454c0` = **1.0 s**,
//      not the 5 s the walk states (`corpus asm 102a0d20`, `FADD float ptr [0x104454c0]`).
//   2. `0x10161a70` sets the created controller's model from the owner's **`GetModelName()`**
//      (slot 9, `+0x24`), not from its classname; and the fixed float `0x45800000` = 4096.0 lands on
//      `+0x63b4` `m_flSeekDistBase`, which the shape map binds to `FElysiumNpc::AuthoredVision`.
//   3. `0x100e4840`'s no-match fallback returns the first response record's **field `+0x00`** — the
//      same word the loop MATCHES on, an id — while the hit path returns `+0x0c`, the goto-line.
//      The two are different fields. Retail's, reproduced; the walk called both "goto-line".
//
// A fourth, on `0x101aaee0`: the arm 29c's walk calls "the alive test 0x100b5190" is not a liveness
// test at all. `0x100b5190` is a seven-byte getter of `+0x00f4` `m_bScriptHidden`
// (`docs/vtmb/npc-kernel/fields.md`), which this runtime carries as `FElysiumEntity::IsHidden()`.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumClassRegistry.h"   // FElysiumClassDesc::ClassName — the controller's class compare
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace
{
	// ---------------------------------------------------------------------------------------------
	// The recovered constants this family reads, each beside where its value was settled. Unit-
	// prefixed because the module builds adaptive-unity and this anonymous namespace is regularly
	// merged with others.
	// ---------------------------------------------------------------------------------------------

	// `_DAT_104454c0` — the crosswalk re-arm delta `0x102a0d20` adds to `curtime`. Read out of the
	// pinned `vampire.dll`'s `.rdata` (image base `0x10000000`, VA `0x10445000`, raw `0x445000`);
	// `corpus asm 102a0d20` shows the `FADD float ptr [0x104454c0]` the decompiler folded into a
	// name. **1.0 s**, not 29c's 5 s.
	constexpr double GDlgCrosswalkRearmSeconds = 1.0;

	// `0x45800000` — the float `0x10161a70` stamps into the created controller's `+0x63b4`
	// (`m_flSeekDistBase`). Read straight off the listing's immediate.
	constexpr float GDlgControllerSeekDistBase = 4096.f;

	// `m_fEffects` bits `0x60`, OR-ed onto the created controller after `CopyAnimationDataFrom`
	// already set `src | 0x10`, so a live controller carries `0x70`.
	constexpr uint32 GDlgControllerEffects = 0x60u;

	// `m_spawnflags` bit 2. `0x10161a70` raises it on the controller it creates; the shipped
	// `camera_cinematic` teardown reads the same bit as "this entity is disposable".
	constexpr int32 GDlgControllerSpawnFlag = 0x4;

	// `CAI_Navigator`'s pedestrian/crosswalk path type. Both `0x102a0bc0` and `0x102a0d20` gate on
	// `GetPathType() == 8` and on nothing else.
	constexpr int32 GDlgCrosswalkPathType = 8;

	// The crosswalk signal's base bit. `0x10 << (((int)curtime >> 4) & 3)` walks `0x10`, `0x20`,
	// `0x40`, `0x80` — four 16-second phases of a 64-second cycle.
	constexpr int32 GDlgCrosswalkSignalBit = 0x10;

	// `m_bfNPCStateFlags` (`+0x5b64`) bit 2. `FElysiumNpcFlags::NpcStateFlagsForRetailState` gives it
	// to retail states 2 (combat), 7/9/0xa/0xd (dead and its neighbours), 8 (flee) and 0xb/0xe
	// (hunt) — every state in which the body is busy or gone. `CPayphone::CanTalk` and the Troika
	// gate `0x102c21c0` both refuse on it.
	constexpr uint8 GDlgStateFlagBusy = 0x04;

	// `m_szDialogQue` is a `char[0x60]`; `0x102c0470` `Q_strncpy`s into it, so a longer name is
	// TRUNCATED rather than refused.
	constexpr int32 GDlgQueBufferChars = 0x60;

	// `CDialog::message_send`'s three user-message opcodes, in the order it sends them.
	constexpr int32 GDlgMessageOpen = 0;
	constexpr int32 GDlgMessagePayload = 2;
	constexpr int32 GDlgMessageClose = 3;

	// The retail console strings these bodies carry are spelled at their call sites rather than
	// here: `FString::Printf` takes a checked format and will not accept a `const TCHAR*`. Each one
	// is verbatim from `.rdata` —
	//   `0x1056312c` "\n\n\t\tNPC Response: %s\n"                      (`0x100e58e0`)
	//   `0x10586138` "\nGetControllerNPC() asked for NPC class: %s, …"  (`0x10161a70`)
	//   `0x105860f8` "GetControllerNPC() created NULL Entity for :%s\n" (`0x10161a70`)
	//   `0x10562de0` "Player responded: %d %s"                          (`0x100e4840`, level 1)
	//   `0x10562e04` "Could not find response for play…"                (`0x100e4840`, level 3)

	// `DAT_10924a6c`'s slot-1 dispatch, whose result every caller discards. Families Hints,
	// Schedule, Positions and Species each recorded the same reading: a folded `ConVar`/`VPROF`
	// read sitting in front of a `SetCondition`. It writes nothing and is named rather than
	// emitted. **Unrecovered**: what the object is.
	void DlgDiscardedConVarRead()
	{
	}
}

// =================================================================================================
// `CDialog`'s two bodies
// =================================================================================================

bool FElysiumNpc::DialogQueIsFinal() const
{
	// 0x100e58b0 — seven instructions, two terms, in this order:
	//
	//     if (m_bFinalLatch (+0x30e9) != 0) return true;
	//     return m_CurrentLineId (+0x2830) == 0;
	//
	// The latch short-circuits, so a set byte makes the queue final whatever the line id is.
	if (bDialogFinalLatch)
	{
		return true;
	}
	return DialogCurrentLineId == 0;
}

void FElysiumNpc::SetDialogQue(const FString& SpeechFile, bool bFinal)
{
	// 0x102c0470 — `CAI_BaseNPCTroika::SetDialogQue`, two statements:
	//
	//     Q_strncpy(m_szDialogQue (+0x64ec), name, 0x60);
	//     m_bDialogQueIsFinal (+0x654c) = bFinal;
	//
	// Called ON THE SPEAKER, not on the dialogue object. The 0x60 truncation is retail's: a longer
	// speech-file name is cut, not refused, and `Q_strncpy` null-terminates inside the buffer, so
	// the usable length is 0x5f characters.
	Dialogue.DialogQue = SpeechFile.Left(GDlgQueBufferChars - 1);
	Dialogue.bDialogQueIsFinal = bFinal;
}

void FElysiumNpc::DialogMessageSend(const FDialogResponsePage& Page)
{
	// 0x100e58e0 — `CDialog::message_send`, 716 bytes, read off `corpus asm 100e58e0` because the
	// decompiler reports DAMAGED on it. Push one conversation turn at the recipient. Nine steps,
	// in retail's order:
	//
	//  1. Resolve the recipient EHANDLE at `CDialog+0x04` and build a
	//     `CReliableSingleUserRecipientFilter` over it. A dead handle yields a NULL recipient and
	//     retail builds the filter anyway — every message below is still sent.
	//  2. User message 0: `UserMessageBegin(filter, DAT_1072608c)` (engine slot 65), `WriteByte(0)`,
	//     `WriteByte(m_ResponseCount (+0x2834))`, `MessageEnd()`.
	//  3. `Msg(1, "\n\n\t\tNPC Response: %s\n", page)` — the console echo of the NPC's own line.
	//  4. `message_append(this, 0, page+0x0000)` — the NPC line into the history sheet.
	//  5. `speech = LookupSpeechFile(this, m_CurrentLineId (+0x2830))` and the fork:
	//       * speech RESOLVED — resolve the speaker at `CDialog+0x00` and
	//         `SetDialogQue(speaker, speech, DialogQueIsFinal())`, then `ShowPlayerChoices(0)`.
	//       * speech NULL — `ShowPlayerChoices(1)`, `ShowHistoryWindow(1)`, `PlayWhisper(NULL)`.
	//     Note which way round: a line that HAS a voice hides the choices until it finishes, and a
	//     line with none shows them at once and drains the queued whisper.
	//  6. The response texts: `message_append(this, i, page + 0x804 + (i-1)*0x800)` for
	//     i = 1 .. m_ResponseCount INCLUSIVE — N+1 appends in all, counting step 4.
	//  7. User message 2: `WriteByte(2)`, then `WriteByte(page+0x2804 + i*4)` for
	//     i = 0 .. m_ResponseCount-1, then engine slot 68 with `page+0x2814 + i*4` over the same
	//     range, then `MessageEnd()`. The two int loops run 0..N-1 while the text loop ran 1..N;
	//     that asymmetry is retail's.
	//  8. User message 3: `WriteByte(3)`, `MessageEnd()`.
	//  9. The filter's destructor.
	//
	// Every mechanism here is a seam (`ElysiumNpcKernelDialogue.inl` § "The seams"): this runtime's
	// dialogue box is the presenter's and its conversation state machine is spec 0004's
	// `FElysiumDlgConversation`. Nothing of either is re-derived; the requests are recorded so the
	// ORDER and the payload are assertable, which is the whole of what this body observably does.
	const int32 ResponseCount = DialogPcLines.Num();   // +0x2834, family EntityChain's word

	// Step 1. `CDialog+0x04`, the recipient. Recorded rather than used: the filter's only consumer
	// is the transport, which is a seam.
	const FElysiumEntity* Recipient = World != nullptr ? World->Resolve(DialogRecipient) : nullptr;
	DialogUiCalls.Add(FString::Printf(TEXT("RecipientFilter(%s)"),
		Recipient != nullptr ? *Recipient->TargetName : TEXT("<none>")));

	// Step 2.
	DialogUserMessages.Add(FString::Printf(TEXT("%d:%d"), GDlgMessageOpen, ResponseCount));

	// Step 3.
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("\n\n\t\tNPC Response: %s\n"), *Page.NpcText);

	// Step 4.
	DialogUiCalls.Add(FString::Printf(TEXT("append 0 %s"), *Page.NpcText));

	// Step 5.
	if (!DialogSpeechFile.IsEmpty())
	{
		FElysiumEntity* Speaker = World != nullptr ? World->Resolve(DialogSpeaker) : nullptr;
		FElysiumNpc* SpeakerNpc = Speaker != nullptr ? Speaker->AsNpc() : nullptr;
		const bool bFinal = DialogQueIsFinal();
		if (SpeakerNpc != nullptr)
		{
			SpeakerNpc->SetDialogQue(DialogSpeechFile, bFinal);
		}
		// A null/non-NPC speaker is retail's own arm: `thunk_FUN_102c0470` is called on a NULL
		// `this` and writes nothing. The choices are still hidden.
		DialogUiCalls.Add(TEXT("ShowPlayerChoices 0"));
	}
	else
	{
		DialogUiCalls.Add(TEXT("ShowPlayerChoices 1"));
		DialogUiCalls.Add(TEXT("ShowHistoryWindow 1"));
		// `CDialog::PlayWhisper(NULL)` — the port already carries that body
		// (`FElysiumNpcDialogue::PlayWhisper`, family Sounds). A null sound name is an empty one.
		Dialogue.PlayWhisper(*this, FString());
	}

	// Step 6 — 1..N inclusive.
	for (int32 Index = 1; Index <= ResponseCount; ++Index)
	{
		const FString& Text = Page.ResponseText.IsValidIndex(Index - 1)
			? Page.ResponseText[Index - 1] : FString();
		DialogUiCalls.Add(FString::Printf(TEXT("append %d %s"), Index, *Text));
	}

	// Step 7 — 0..N-1 twice.
	FString Payload = FString::Printf(TEXT("%d:"), GDlgMessagePayload);
	for (int32 Index = 0; Index < ResponseCount; ++Index)
	{
		Payload += FString::Printf(TEXT("%d,"),
			Page.ResponseWordA.IsValidIndex(Index) ? Page.ResponseWordA[Index] : 0);
	}
	for (int32 Index = 0; Index < ResponseCount; ++Index)
	{
		Payload += FString::Printf(TEXT("%d,"),
			Page.ResponseWordB.IsValidIndex(Index) ? Page.ResponseWordB[Index] : 0);
	}
	DialogUserMessages.Add(Payload);

	// Step 8.
	DialogUserMessages.Add(FString::Printf(TEXT("%d:"), GDlgMessageClose));
}

int32 FElysiumNpc::DialogGotoLineForResponse(int32 ResponseIndex)
{
	// 0x100e4840 — `CDialog::goto_line_for_response`, 281 bytes. Which line the conversation goes
	// to when the player picks shown response `ResponseIndex`. Arm by arm:
	//
	//     if (m_pNode (+0x08) == NULL || m_CurrentLineId (+0x2830) <= 0) return 0;
	//     if (ResponseIndex >= 0) {
	//         id = m_ResponseTargetIds (+0x2838)[ResponseIndex];
	//         if (id == 0) return 0;                              // the SAME return as no node
	//         for (i = 0; i < node->nResponses (+0x104); ++i)
	//             if (node->pResponses (+0x10c)[i].Id == id) {
	//                 Msg(1, "Player responded: %d %s");
	//                 return node->pResponses[i].GotoLine;        // record +0x0c
	//             }
	//     }
	//     Msg(3, "Could not find response for player...");
	//     return node->pResponses[0].Id;                          // record +0x00 — NOT +0x0c
	//
	// Three things the walk got wrong or left out, all read off the C:
	//
	//   * `+0x2830` is NOT a response count — it is the current NPC line's id, the same word
	//     `LookupSpeechFile` and `0x100e58b0` read. The gate is "a line is being spoken".
	//   * a ZERO target id takes the NO-NODE return (0), not the warning arm: the `goto` in the C
	//     jumps past the `Msg(3, ...)` to the outer `return 0`.
	//   * the fallback returns the first record's `+0x00`, the ID field, while the hit path returns
	//     `+0x0c`, the goto-line. Retail reads two different fields out of the same record and this
	//     port reproduces that rather than "fixing" the fallback to `+0x0c`.
	//
	// The index is NOT bounds-checked against `+0x2834` in retail — only `>= 0` is tested — so a
	// large index reads past the shown-response array. This port refuses the out-of-range read and
	// falls into the warning arm, which is a **named modernization** (M-DLG-BOUNDS): the retail read
	// is undefined behaviour whose value is whatever the object's next words hold.
	if (!bDialogNodeBound || DialogCurrentLineId <= 0)
	{
		return 0;
	}

	if (ResponseIndex >= 0)
	{
		const int32 TargetId = DialogResponseTargetIds.IsValidIndex(ResponseIndex)
			? DialogResponseTargetIds[ResponseIndex] : 0;
		if (DialogResponseTargetIds.IsValidIndex(ResponseIndex))
		{
			if (TargetId == 0)
			{
				return 0;
			}
			for (const FDialogNodeResponse& Row : DialogNodeResponses)
			{
				if (Row.Id == TargetId)
				{
					UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("Player responded: %d"), TargetId);
					return Row.GotoLine;
				}
			}
		}
	}

	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Could not find response for player"));
	// Retail dereferences `pResponses[0]` unconditionally here; an empty table is an out-of-bounds
	// read it never guards. The port answers 0 for that case and says so rather than reproducing a
	// wild read (**named modernization**, M-DLG-EMPTY).
	return DialogNodeResponses.Num() > 0 ? DialogNodeResponses[0].Id : 0;
}

// =================================================================================================
// The `CBasePlayer` half — the cine camera, the controller NPC, the two pursuit counts
// =================================================================================================

FElysiumEntity* FElysiumNpc::GetActiveCameraEntity() const
{
	// 0x1017cf90 — `CBasePlayer::GetCineCamera`. Four terms and their order matters only because
	// the first is the cheap one:
	//
	//     m_iCameraOverrideIdx (+0x1ec4) > 0        — STRICTLY positive; 0 is "nothing adopted"
	//     m_hCineCamera (+0x19b4) != -1
	//     its serial matches                        — the handle is not stale
	//     the slot's pointer is non-NULL
	//     ...then re-resolve the SAME handle and serial-check it AGAIN, and return that pointer.
	//
	// The second resolve is redundant in retail and is not reproduced as a second call: this
	// runtime's `Resolve` is the whole check and calling it twice cannot answer differently.
	//
	// The port already carries both words on the world's single adoption slot
	// (`FElysiumEntityWorld::SetCineCamera`, which cites `0x1017cef0` — this body's only writer):
	// `CineCameraShotId()` is `m_iCameraOverrideIdx` "in the only form the port has one" and
	// `CineCameraEntity()` is `+0x19b4`. An entity-less adopter (a terminal, a `SetCamera` shot)
	// holds the slot with a shot id and no handle, which is retail's `+0x1ec4 > 0` with a dead
	// `+0x19b4` — and this body answers null for it, exactly as retail does.
	if (World == nullptr || World->CineCameraShotId() <= 0)
	{
		return nullptr;
	}
	const FElysiumEntityHandle Camera = World->CineCameraEntity();
	if (!Camera.IsSet())
	{
		return nullptr;
	}
	return World->Resolve(Camera);
}

FElysiumEntity* FElysiumNpc::CreateControllerNpcEntity(const TCHAR* Classname)
{
	// **SEAM** for `CreateEntityByName(classname)` + `DispatchSpawn` (`0x10161b4c` -> the factory,
	// then `thunk_FUN_101d1280` = `DispatchSpawn` `0x101d1280`). The kernel does not spawn: the
	// class registry and `FElysiumEntityWorld` do, and a kernel body that reached them would be a
	// second spawner beside `events_player.CreateControllerNPC`
	// (`ElysiumEventClasses.cpp`, still a `Note()`-only stub).
	//
	// Answers null, which is retail's OWN `"GetControllerNPC() created NULL Entity for :%s"` arm.
	// The body is not refused — that arm runs, the warning is emitted and `m_hControllerNPC` is
	// cleared, which is what retail does when the factory fails.
	(void)Classname;
	return nullptr;
}

FElysiumEntity* FElysiumNpc::GetControllerNpc(const TCHAR* Classname)
{
	// 0x10161a70 — `CBasePlayer::GetControllerNPC(const char* classname)`, 534 bytes, read off
	// `corpus asm 10161a70`. Hand back the `npc_VPlayerController` driving this body, creating one
	// of the requested class if there is none or the cached one is of a different class.
	//
	// STEP 1 — the cache. `m_hControllerNPC` (+0x1db0) must resolve to a live entity. If it does:
	//     name = cached->m_iClassname (+0x11c), or "" when null
	//     if (__strcmpi(name, classname) == 0)  -> jump straight to the tail (STEP 3)
	//     otherwise
	//         Warning("\nGetControllerNPC() asked for NPC class: %s, but already has NPC of
	//                  class %s.\n Deleting old NPC, this better be OK!!\n\n", classname,
	//                 cached->GetClassname());
	//         ReleaseControllerNpc(false, false);      // 0x101618e0, family EntityChain's body
	//     and FALL THROUGH into STEP 2. Note what the release does NOT do: it copies nothing
	//     (both flags false) and it clears the handle, so STEP 2 always creates.
	//
	// STEP 2 — create. `ent = CreateEntityByName(classname)`; `npc = ent->m_pCombatCharacter (+0x98)`.
	//     if (ent == NULL || npc == NULL)
	//         Warning("GetControllerNPC() created NULL Entity for :%s\n", classname);
	//         m_hControllerNPC = -1;
	//     else, in this order:
	//         npc->m_spawnflags |= 4;
	//         npc->SetOrigin(  me->GetAbsOrigin() );          // slot 62  (+0xf8)  <- slot 217 (+0x364)
	//         npc->SetAngles(  me->GetAngles()    );          // slot 64  (+0x100) <- slot 221 (+0x374)
	//         npc->SetOwnerEntity(me);                        // slot 202 (+0x328)
	//         CopyAnimationDataFrom(npc, me);                 // 0x10097310
	//         npc->m_flSeekDistBase (+0x63b4) = 4096.0f;      // 0x45800000
	//         npc->SetModel( me->GetModelName() );            // slot 105 (+0x1a4) <- slot 9 (+0x24)
	//         DispatchSpawn(npc);                             // 0x101d1280
	//         npc->m_fEffects |= 0x60;
	//         npc->ResetThinkTimers();                        // slot 614 (+0x998)
	//         m_hControllerNPC = npc->GetRefEHandle();
	//
	// STEP 3 — the tail, shared by both paths: resolve `m_hControllerNPC` once more with its serial
	//     check and return the pointer, or 0.
	//
	// `CopyAnimationDataFrom` (`0x10097310`) is nine assignments plus two model calls:
	// `SetModel(src->GetModelName())`, `SetModelIndex(src->GetModelIndex())`, then `m_flCycle`,
	// `m_nTopColor`, `m_nBottomColor`, `m_fEffects = src->m_fEffects | 0x10`, `m_nSequence`,
	// `m_flAnimTime`, `m_nBody`, `m_nSkin`, `m_nPhysicsChainDisableMask`. This runtime carries four
	// of the eleven (`SequenceCycle`, `EffectsWord`, `SequenceNumber`, `AnimTime`); the colour,
	// body, skin and physics-chain words have no member here and are named rather than dropped.
	const TCHAR* const Requested = Classname != nullptr ? Classname : TEXT("");

	FElysiumEntity* Cached = World != nullptr && ControllerNpc.IsSet()
		? World->Resolve(ControllerNpc) : nullptr;
	if (Cached != nullptr)
	{
		const FString CachedClass = Cached->Class != nullptr
			? Cached->Class->ClassName.ToString() : FString();
		// `__strcmpi` — a case-insensitive compare, which is what `FString::Equals` with
		// `ESearchCase::IgnoreCase` is.
		if (CachedClass.Equals(Requested, ESearchCase::IgnoreCase))
		{
			return Cached;
		}
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("\nGetControllerNPC() asked for NPC class: %s, but already has NPC of class %s.\n")
			TEXT(" Deleting old NPC, this better be OK!!\n\n"),
			Requested, *CachedClass);
		ReleaseControllerNpc(false, false);
	}

	FElysiumEntity* Created = CreateControllerNpcEntity(Requested);
	FElysiumNpc* CreatedNpc = Created != nullptr ? Created->AsNpc() : nullptr;
	if (CreatedNpc == nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("GetControllerNPC() created NULL Entity for :%s\n"), Requested);
		ControllerNpc = FElysiumEntityHandle::Invalid();
	}
	else
	{
		CreatedNpc->SpawnFlags |= GDlgControllerSpawnFlag;
		CreatedNpc->SetRuntimeTransform(Origin, Angles);
		CreatedNpc->SetOwnerEntity(Handle);

		// `CopyAnimationDataFrom(npc, me)` — the four words this runtime carries, in retail's order.
		CreatedNpc->SequenceCycle = SequenceCycle;
		CreatedNpc->EffectsWord = EffectsWord | 0x10u;
		CreatedNpc->SequenceNumber = SequenceNumber;
		CreatedNpc->AnimTime = AnimTime;
		// **Unrecovered in this substrate**: `m_nTopColor`, `m_nBottomColor`, `m_nBody`, `m_nSkin`
		// and `m_nPhysicsChainDisableMask` have no port member, and the model-index copy is the
		// model name's, below.

		CreatedNpc->AuthoredVision = GDlgControllerSeekDistBase;   // +0x63b4 m_flSeekDistBase
		CreatedNpc->SetRuntimeModel(Model);                        // slot 105 <- slot 9
		// `DispatchSpawn` — the world's spawn pass, which the create seam stands for.
		CreatedNpc->EffectsWord |= GDlgControllerEffects;
		CreatedNpc->ResetThinkTimers(World != nullptr ? World->NowSeconds() : 0.0);
		ControllerNpc = CreatedNpc->Handle;
	}

	return World != nullptr && ControllerNpc.IsSet() ? World->Resolve(ControllerNpc) : nullptr;
}

bool FElysiumNpc::ControllerNpcBusy() const
{
	// 0x10175180 — 116 bytes, retail name **unrecovered**:
	//
	//     h = m_hControllerNPC (+0x1db0);
	//     if (h == -1) return false;
	//     if (serial mismatch || slot pointer NULL) return false;
	//     re-resolve h (a second, laxer check) -> obj, NULL when it fails
	//     return obj->vtable[+0x228]() == 3;
	//
	// 29c's walk called `+0x1db0` "the dialogue/companion partner"; `vtmb_fields CBasePlayer` names
	// it `m_hControllerNPC` — the SAME word `GetControllerNPC` (`0x10161a70`) caches into and
	// `ReleaseControllerNpc` (`0x101618e0`) clears. The predicate is therefore "a controller NPC is
	// driving this body and it is in state 3", and its readers are the dialogue refusal predicate
	// (`0x10178170`), `CAI_BaseNPCTroika::CanTalk` (`0x102c21c0`) and the stealth-kill gate
	// (`0x101681a0`).
	//
	// **Unrecovered**: slot 138 at `+0x228` is `Classify()` by the SDK ordering, but `Class_T == 3`
	// has no recovered name in this image and no producer in this runtime — nothing here classifies
	// an entity. The port's `RetailClass()` census is the class TABLE, not `Classify`'s runtime
	// answer, so it cannot stand in. This reads the seam below and answers false, which is the
	// not-busy value every consumer's own null arm takes.
	if (World == nullptr || !ControllerNpc.IsSet())
	{
		return false;
	}
	const FElysiumEntity* Controller = World->Resolve(ControllerNpc);
	if (Controller == nullptr)
	{
		return false;
	}
	// **SEAM** for slot 138 `Classify()` on the controller. No port body fills it (the generated
	// stub answers `{}` = 0), and 0 is not 3.
	return false;
}

// =================================================================================================
// Slot 295's `CPayphone` override
// =================================================================================================

bool FElysiumNpc::PayphoneCanTalk(const FElysiumEntity* Activator) const
{
	// 0x101aaee0 — `CPayphone::CanTalk`, 119 bytes, slot 295's `CPayphone` override. Seven arms,
	// every one a refusal, in this order:
	//
	//     1. activator == NULL
	//     2. m_iDialog   (+0x0128) == 0            — no authored `dialogname`
	//     3. m_bScriptHidden (+0x00f4)             — `0x100b5190`, a 7-byte getter of that byte
	//     4. m_bWillTalk (+0x1088) == 0
	//     5. m_bfNPCStateFlags (+0x5b64) & 0x4     — the per-state busy bit
	//     6. m_bfAINPCFlags (+0x14b8) & 0x80000    — NO_DIALOG
	//     7. IsInDialog() (0x102c1170)             — the four-term session gate
	//     ...and the answer IS arm 7's negation, so a phone with no session admits.
	//
	// What the payphone does NOT test, against the Troika-line body `0x102c21c0`: the two `IsAlive`
	// dispatches, `IsUnconscious`, the player-side `0x10175180` and `0x10146b20`,
	// `IsBusyWithDiscipline`, `NO_DIALOG_PERSISTENT` (word two `0x10000000`), the menu global's
	// `+0x4ac` and slot 406 (`+0x650`). Seven arms against fourteen: a payphone is a simpler gate
	// than a person, and the difference is the recovered fact.
	//
	// 29c's walk calls arm 3 "the alive test 0x100b5190"; `docs/vtmb/npc-kernel/fields.md` names
	// `+0x00f4` `m_bScriptHidden`, whose writers are `ScriptHide`/`ScriptUnhide`. It is the
	// SCRIPT-HIDDEN test, which this runtime spells `FElysiumEntity::IsHidden()`.
	if (Activator == nullptr)
	{
		return false;
	}
	if (DialogName.IsEmpty())
	{
		return false;
	}
	if (IsHidden())
	{
		return false;
	}
	if (!bWillTalk)
	{
		return false;
	}
	if ((NpcStateFlags() & GDlgStateFlagBusy) != 0)
	{
		return false;
	}
	// Arm 6 is NO_DIALOG ALONE — word one's `0x80000`. `HasDialogSuppressFlag()` is the Troika
	// line's reading and ORs in `NO_DIALOG_PERSISTENT`, which this override does NOT test, so the
	// bit is read by name here rather than through that helper.
	if (NpcFlags.Has(EElysiumNpcFlag::NO_DIALOG))
	{
		return false;
	}
	// Arm 7 — `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`). This runtime carries one session bit
	// plus a talk-end stamp for its four terms, the reading `ElysiumNpcThinkCadence.cpp` and family
	// Sounds both already took; repeated here rather than re-derived.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return !(Dialogue.bInDialog || IsTalking(Now));
}

// =================================================================================================
// Slot 366's `CNPC_VSabbatLeader` override
// =================================================================================================

namespace
{
	// The slot-366 table. Two rows, because the two bodies are two ADDRESSES: the Troika line's is
	// `0x10385a70` (a single `return 0` — `vtmb_code` shows the whole function is `return 0;`, and
	// 59 census classes share it), and `CNPC_VSabbatLeader` replaces it with `0x103a76d0`, which is
	// a scope-trace prologue, a tail call to `0x10385a70` and the same `false`.
	//
	// So there is NO behavioural difference and the table says so. It exists because
	// `docs/vtmb/npc-kernel/slots.md` records two distinct fills and a reader must be able to check
	// the port against it; a port that silently collapsed them would lose the fact that the Sabbat
	// leader overrides the slot at all.
	constexpr FElysiumNpc::FHandleInteractionSpecies GDlgHandleInteractionRows[] =
	{
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103a76d0"), false },
		{ TEXT("CNPC_VAndreiBlood"),  TEXT("0x10385a70"), false },
	};
}

const FElysiumNpc::FHandleInteractionSpecies* FElysiumNpc::HandleInteractionSpeciesRows(
	int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GDlgHandleInteractionRows);
	return GDlgHandleInteractionRows;
}

const FElysiumNpc::FHandleInteractionSpecies* FElysiumNpc::HandleInteractionSpeciesOf(
	const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FHandleInteractionSpecies& Row : GDlgHandleInteractionRows)
	{
		if (FCString::Stricmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

bool FElysiumNpc::SabbatLeaderHandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other)
{
	// 0x103a76d0 — `CNPC_VSabbatLeader::HandleInteraction`, 111 bytes, slot 366. The whole body past
	// its scope-trace prologue and epilogue is one tail call:
	//
	//     CNPC_VAndreiBlood::HandleInteraction(this, interaction, data, other);   // 0x10385a70
	//
	// and `0x10385a70` is `return 0;` — nothing else. The override exists to put
	// `"CNPC_VSabbatLeader::HandleInteraction"` on the scope-trace stack, which is a debug artifact
	// this runtime has no counterpart for, so the observable answer is `false`.
	//
	// Slot 366's base body (`0x10326d40`, the SDK `CBaseCombatCharacter::HandleInteraction`) is
	// story 29c's generated stub and is NOT this family's, so this lands beside it as a named method
	// rather than as the slot's definition. It returns the table's answer rather than a literal so
	// the row and the body cannot drift.
	(void)Interaction;
	(void)Data;
	(void)Other;
	const FHandleInteractionSpecies* Row = HandleInteractionSpeciesOf(TEXT("CNPC_VSabbatLeader"));
	return Row != nullptr ? Row->bAnswer : false;
}

// =================================================================================================
// `CNPC_VCameraSecurity`'s linked camera
// =================================================================================================

FElysiumEntity* FElysiumNpc::ResolveSecCameraLink()
{
	// 0x10369e70 — 252 bytes, unnamed in retail; the name is inferred from the RTTI cast it performs
	// and from the census field names (`m_iszLinkedCamera` +0x6660, `m_hLinkedCamera` +0x6664). Its
	// three callers are all `CNPC_VCameraSecurity`'s: slot 201 `FVisible` (`0x10369ff0`), slot 363
	// `FInViewCone` (`0x10369fb0`) and `vfunc201`'s thunk — a security-camera NPC sees through the
	// `CSecCamera` it is linked to, not through its own eyes.
	//
	// Three blocks, each re-reading `+0x6664` from scratch:
	//
	//  1. If the cached handle does NOT resolve to a live entity:
	//         name = m_iszLinkedCamera (+0x6660), or "" when null
	//         ent  = FindEntityByName(gEntList, NULL, name, this, this)   // 0x100f7770
	//         cam  = dynamic_cast<CSecCamera*>(ent)                        // ___RTDynamicCast
	//         m_hLinkedCamera = cam ? cam->GetRefEHandle() : -1;
	//     The cast is what makes the lookup type-safe: an entity of the right NAME but the wrong
	//     class clears the handle rather than caching it.
	//  2. Re-test the handle:
	//         live  -> m_bLinkedCameraBound (+0x6668) = 1
	//         dead  -> if (m_bLinkedCameraBound) UTIL_Remove(this)          // 0x101cd940
	//     So the latch is a one-way door: a camera NPC that HAS been linked and then loses its
	//     camera removes ITSELF. A camera NPC that never linked simply keeps answering null.
	//  3. Return the resolved handle, or 0.
	//
	// **SEAM**: `npc_VCameraSecurity` is a census classname but NOT a registered spawn leaf here
	// (`Substrate/ElysiumNpcClasses.cpp`), and `CSecCamera` has no port class, so the RTTI cast has
	// nothing to test. The name lookup goes through the world's own name index — the same set
	// retail's `FindEntityByName` walks — and the class filter is stated as unrecovered rather than
	// silently dropped.
	if (World == nullptr)
	{
		return nullptr;
	}

	// Block 1.
	if (World->Resolve(LinkedCamera) == nullptr)
	{
		FElysiumEntity* Found = !LinkedCameraName.IsEmpty()
			? World->FindByName(*LinkedCameraName) : nullptr;
		// **Unrecovered**: `dynamic_cast<CSecCamera*>`. This runtime stands no `CSecCamera` leaf, so
		// the cast cannot be applied and a name hit of ANY class is accepted — the one place this
		// body is more permissive than retail. Named here so a later story that lands `CSecCamera`
		// knows exactly where the filter goes.
		LinkedCamera = Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid();
	}

	// Block 2.
	FElysiumEntity* Camera = World->Resolve(LinkedCamera);
	if (Camera != nullptr)
	{
		bLinkedCameraBound = true;
	}
	else if (bLinkedCameraBound)
	{
		Kill();   // `UTIL_Remove(this)` `0x101cd940`
	}

	// Block 3 — retail resolves `+0x6664` a THIRD time here; after block 2 nothing can have changed
	// it, so the cached pointer is returned.
	return Camera;
}

// =================================================================================================
// The pedestrian crosswalk rule
// =================================================================================================

int32 FElysiumNpc::CrosswalkPhaseMask(double CurTime)
{
	// `0x10 << (((int)curtime >> 4) & 3)` — shared verbatim by `0x102a0bc0` (`__ftol` then
	// `SAR 4 / AND 3 / SHL`) and `0x102a0d20` (the same three instructions at `102a0e00`).
	//
	// A 64-second cycle in four 16-second phases, walking `0x10`, `0x20`, `0x40`, `0x80` through the
	// link's `+0x64` flags. The truncation is retail's `__ftol` (toward zero), which for the
	// non-negative `gpGlobals->curtime` is a floor.
	const int32 Seconds = static_cast<int32>(CurTime);
	return GDlgCrosswalkSignalBit << ((Seconds >> 4) & 3);
}

int32 FElysiumNpc::NavigatorPathType() const
{
	// **SEAM** for `0x102ee620`, which is `return path->+0x30` on `m_pNavigator` (`+0x5d34`) — the
	// navigator's current path TYPE. The shape map routes `+0x5d34` to
	// `FElysiumScriptedCharacter::Motor`, "the one motor seam this chain stands beside the body",
	// and that motor carries no path object at all.
	//
	// Answers `-1`, which is neither the crosswalk type `8` nor any other retail type, so both
	// bodies below take their own "not a pedestrian path" arm rather than being refused.
	return NavigatorPathTypeWord;
}

bool FElysiumNpc::FindCrosswalkPathNode(int32 EntityIndex, int32 LinkId, int32& OutSignalFlags) const
{
	// **SEAM** for `0x102f96e0` — given the waypoint's owning entity and the hint's link id, walk
	// that entity's node array (`+0x78` count, `+0x7c` pointers), translate each node's id through
	// `0x102dda40` and answer the node whose id matches, or NULL. `OutSignalFlags` is the matched
	// node's `+0x64`.
	//
	// This runtime stands no node graph — the shape map records `+0x630c` ABSENT for the same
	// reason — so this answers false and writes nothing. The request is recorded so the refusal is
	// distinguishable from never having been asked.
	CrosswalkNodeQueries.Add(FString::Printf(TEXT("node ent=%d link=%d"), EntityIndex, LinkId));
	OutSignalFlags = 0;
	return false;
}

void FElysiumNpc::SetAtCrosswalkLink(int32 SignalFlags)
{
	// 0x102a0b90 — two statements and no gate:
	//
	//     m_bfAINPCFlags (+0x14b8) |= 0x4;     // AT_CROSSWALK
	//     m_pCrosswalkLink (+0x630c) = link;
	//
	// The port carries the flag by name and the link as the one word its readers touch.
	NpcFlags.Set(EElysiumNpcFlag::AT_CROSSWALK);
	bCrosswalkLinkBound = true;
	CrosswalkLinkSignalFlags = SignalFlags;
}

bool FElysiumNpc::ResolvePedestrianPathNode(const FDialogPedWaypoint* Waypoint)
{
	// 0x102a0bc0 — 178 bytes, unnamed in retail; 29c's target name is kept. Its one caller is
	// `FUN_102f0400`, the navigator's waypoint-advance, which hands it `path->CurWaypoint`
	// (`navigator+0x30` then `+0x24`) and ignores the answer.
	//
	// Six gates, ALL required, in this order — the first four on the waypoint, then the navigator,
	// then the node:
	//
	//     waypoint != NULL
	//     waypoint->EntityIndex (+0x10) >= 0
	//     waypoint->Hint        (+0x30) != NULL
	//     waypoint->Flags       (+0x28) & 0x4
	//     GetPathType(m_pNavigator +0x5d34) == 8
	//     node = FindPathNode(entityAt(waypoint->EntityIndex), hint->LinkId (+0x10))   // 0x102f96e0
	//     node != NULL && (node->+0x64 & (0x10 << (((int)curtime >> 4) & 3)))
	//
	// and only then `SetAtCrosswalkLink(node)` (`0x102a0b90`) and `return true`.
	//
	// The entity fetch is by INDEX into the navigator's own entity array (`navigator+0x2c`, a
	// count/pointer pair), and an out-of-range index bumps a global error counter (`DAT_106c994c`)
	// and yields NULL — which `0x102f96e0` then answers NULL for. Both are folded into the
	// `FindCrosswalkPathNode` seam.
	//
	// The flag byte's bit 2 at `+0x28` is the waypoint's own, NOT `m_bfAINPCFlags`'s `AT_CROSSWALK`:
	// the two share a bit number by coincidence and the body writes the second only on success.
	if (Waypoint == nullptr)
	{
		return false;
	}
	if (Waypoint->EntityIndex < 0)
	{
		return false;
	}
	if (!Waypoint->bHasHint)
	{
		return false;
	}
	if ((Waypoint->Flags & 0x4) == 0)
	{
		return false;
	}
	if (NavigatorPathType() != GDlgCrosswalkPathType)
	{
		return false;
	}

	int32 SignalFlags = 0;
	if (!FindCrosswalkPathNode(Waypoint->EntityIndex, Waypoint->HintLinkId, SignalFlags))
	{
		return false;
	}

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if ((SignalFlags & CrosswalkPhaseMask(Now)) == 0)
	{
		return false;
	}

	SetAtCrosswalkLink(SignalFlags);
	return true;
}

void FElysiumNpc::UpdatePedestrianInfo()
{
	// 0x102a0d20 — `CAI_BaseNPCTroika::UpdatePedestrianInfo`, 313 bytes. The per-think crosswalk
	// rule, read off `corpus asm 102a0d20`:
	//
	//     if (GetPathType(m_pNavigator +0x5d34) != 8) return;      // the ONLY outer gate
	//     ClearCondition(0x10);    // SHOULD_INTERACT
	//     ClearCondition(0x12);    // CROSSWALK_WALK
	//     ClearCondition(0x13);    // CROSSWALK_DONTWALK
	//     if ((m_bfAINPCFlags & AT_CROSSWALK 0x4) == 0) return;
	//     if (m_flNextCrosswalkUpdateTime (+0x6318) >= curtime) return;   // STRICTLY less passes
	//     m_flNextCrosswalkUpdateTime = curtime + _DAT_104454c0;          // 1.0 s
	//     mask = 0x10 << (((int)curtime >> 4) & 3);
	//     if (m_pCrosswalkLink (+0x630c)->+0x64 & mask) {
	//         <discarded ConVar read>;  SetCondition(0x13);  return;      // DONTWALK — and the
	//                                                                    // AT_CROSSWALK bit STAYS
	//     }
	//     <discarded ConVar read>;  SetCondition(0x12);                   // WALK
	//     m_bfAINPCFlags &= ~0x4;                                         // release the crosswalk
	//
	// Read the two arms the right way round: the signal bit SET is DONT-WALK (the pedestrian keeps
	// waiting and stays latched at the crossing), and the bit CLEAR is WALK (the pedestrian is
	// released and the latch drops). The three clears run on every pedestrian pass whether or not
	// the NPC is at a crossing, so a condition raised last pass never survives into this one.
	//
	// `m_flNextCrosswalkUpdateTime` is re-armed BEFORE the signal is read, so the 1 s throttle
	// applies to both arms equally.
	//
	// Conditions `0x10` SHOULD_INTERACT, `0x12` CROSSWALK_WALK and `0x13` CROSSWALK_DONTWALK are
	// this family's additions to `EElysiumNpcCond`, read off the base registrar's dump
	// (`docs/vtmb/npc-ai/conditions-and-states.md` § "The base condition table"). SHOULD_INTERACT is
	// cleared here and has no producer anywhere in this family — its producer is the pedestrian
	// interaction pass (`+0x631c m_flNextPedInteractTime`), which is not a layer 0–9 row.
	//
	// **This body has no caller in this runtime.** Retail's only caller is `CAI_BaseNPCTroika::RunAI`
	// (slot 432, `0x1028fcc0`), which is story 29e's generated stub.
	if (NavigatorPathType() != GDlgCrosswalkPathType)
	{
		return;
	}

	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldInteract);      // 0x10
	Cognition.Conditions.Clear(EElysiumNpcCond::CrosswalkWalk);       // 0x12
	Cognition.Conditions.Clear(EElysiumNpcCond::CrosswalkDontWalk);   // 0x13

	if (!NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK))
	{
		return;
	}

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (!(NextCrosswalkUpdateTime < Now))
	{
		return;
	}
	NextCrosswalkUpdateTime = Now + GDlgCrosswalkRearmSeconds;

	const int32 Mask = CrosswalkPhaseMask(Now);
	if ((CrosswalkLinkSignalFlags & Mask) != 0)
	{
		DlgDiscardedConVarRead();
		Cognition.Conditions.Set(EElysiumNpcCond::CrosswalkDontWalk);
		return;
	}

	DlgDiscardedConVarRead();
	Cognition.Conditions.Set(EElysiumNpcCond::CrosswalkWalk);
	NpcFlags.Clear(EElysiumNpcFlag::AT_CROSSWALK);
}

// =================================================================================================
// The two `CBaseEntity` use slots, 39 and 42
// =================================================================================================

void FElysiumNpc::OnUseBegin(FElysiumEntity* Activator)
{
	// slot 39, 0x100a4fe0 — `CBaseEntity`'s use-begin hook, 59 bytes, shared by 82 classes in the
	// kernel family (`CAISound` is simply the class the corpus attributes the address to). Two
	// statements:
	//
	//     FireOutput(m_OnUseBegin (+0x5c), activator = param_1, caller = this, delay = 0);
	//     m_hUseActivator (+0x8c) = param_1 ? param_1->GetRefEHandle() : -1;
	//
	// The output fires FIRST and UNCONDITIONALLY — a null activator still fires it, with a null
	// activator — and the handle is written after. `OnUseBegin` is a `CBaseEntity` datamap keyfield
	// (`vtmb_fields CAISound` names `+0x5c` `m_OnUseBegin`, key `OnUseBegin`), so every `npc_*` row
	// in a shipped map may wire it.
	static const FName GOnUseBegin(TEXT("OnUseBegin"));
	FireOutput(GOnUseBegin, Activator != nullptr ? Activator->Handle : FElysiumEntityHandle());
	UseActivator = Activator != nullptr ? Activator->Handle : FElysiumEntityHandle::Invalid();
}

void FElysiumNpc::OnUseEnd(FElysiumEntity* Activator)
{
	// slot 42, 0x100a5030 — the other half, 33 bytes:
	//
	//     FireOutput(m_OnUseEnd (+0x74), activator = param_1, caller = this, delay = 0);
	//     m_hUseActivator (+0x8c) = -1;
	//
	// The clear is UNCONDITIONAL — it does not consult the activator at all, so ending a use someone
	// else began still clears the cache. That asymmetry with slot 39 is retail's.
	static const FName GOnUseEnd(TEXT("OnUseEnd"));
	FireOutput(GOnUseEnd, Activator != nullptr ? Activator->Handle : FElysiumEntityHandle());
	UseActivator = FElysiumEntityHandle::Invalid();
}
