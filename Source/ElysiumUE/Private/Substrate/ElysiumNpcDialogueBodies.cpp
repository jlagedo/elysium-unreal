// Story 29c-1, family **Dialogue** — the fourteen layer 0–9 bodies 29c's overlay filed under the
// word "dialogue". The declarations and what the family turned out to be are in
// `Substrate/ElysiumNpcDialogueBodies.inl`; the walked prose is `docs/vtmb/npc-ai/shape.md`
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
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumPlaceSet.h"

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

	// `CAI_Navigator`'s pedestrian/crosswalk path type. Both `0x102a0bc0` and `0x102a0d20` gate on
	// `GetPathType() == 8` (`0x102ee620`).
	constexpr int32 GDlgCrosswalkPathType = 8;

	// `bits_WP_TO_NODE`, the waypoint flag `0x102a0bc0` requires (`+0x28 & 4`).
	constexpr int32 GDlgWaypointToNode = 0x4;

	// The crosswalk signal: retail's two readers test `link+0x64 & (0x10 << (((int)curtime >> 4) & 3))`,
	// four 16-second phases of a 64-second cycle walking `0x10`, `0x20`, `0x40`, `0x80`. The only
	// writer (`0x102f97c0`) sets or clears the whole nibble, so no phase ever answers differently from
	// another and the port reads one boolean per pair (`FElysiumPlaceSet::IsCrosswalkRed`, decision
	// 3). The rotation is recorded, not modelled.

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
	// Every mechanism here is a seam (`ElysiumNpcDialogueBodies.inl` § "The seams"): this runtime's
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

// =================================================================================================
// The pedestrian crosswalk rule
// =================================================================================================

void FElysiumNpc::SetAtCrosswalkLink(int32 Pair)
{
	// 0x102a0b90 — two statements and no gate:
	//
	//     m_bfAINPCFlags (+0x14b8) |= 0x4;     // AT_CROSSWALK
	//     m_pPedestrianLink (+0x630c) = link;
	//
	// The link is carried as its crosswalk pair (`PedestrianPair`, `INDEX_NONE` for NULL).
	NpcFlags.Set(EElysiumNpcFlag::AT_CROSSWALK);
	PedestrianPair = Pair;
}

bool FElysiumNpc::ResolvePedestrianPathNode(const FDialogPedWaypoint* Waypoint)
{
	// 0x102a0bc0 — 178 bytes, unnamed in retail; 29c's target name is kept. Callers: the waypoint
	// advance `0x102f0400` (answer ignored) and the obstruction sink `0x10298340` (arm ii).
	//
	// Five gates, in the listing's order, then the link:
	//
	//     waypoint != NULL
	//     waypoint->node (+0x10) >= 0
	//     waypoint->next (+0x30) != NULL
	//     waypoint->flags (+0x28) & 4                       // bits_WP_TO_NODE
	//     GetPathType(m_pNavigator +0x5d34) == 8            // 0x102ee620, pedestrian only
	//     node = network->nodes[waypoint->node]             // bounds miss: DAT_106c994c++, NULL
	//     link = 0x102f96e0(node, next->node (+0x10))       // this node to the NEXT waypoint's
	//     link != NULL && (link->+0x64 & (0x10 << (((int)curtime >> 4) & 3)))
	//
	// and only then `0x102a0b90(link)` and `return true`. The flag bit 2 at `+0x28` is the waypoint's
	// own, NOT `m_bfAINPCFlags`'s `AT_CROSSWALK`: the two share a number by coincidence.
	//
	// A bounds miss hands `0x102f96e0` a NULL node, which retail then dereferences (`+0x78`); every
	// waypoint the port lays carries a node of the network, so the arm is unreachable here and the
	// port answers false on it after the count, as a node with no links would.
	if (Waypoint == nullptr)
	{
		return false;
	}
	if (Waypoint->NodeId < 0)
	{
		return false;
	}
	if (!Waypoint->bHasNext)
	{
		return false;
	}
	if ((Waypoint->Flags & GDlgWaypointToNode) == 0)
	{
		return false;
	}
	if (NavigatorPathType() != GDlgCrosswalkPathType)
	{
		return false;
	}
	FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	if (Places == nullptr || !Places->IsValidNode(Waypoint->NodeId))
	{
		++NodeIndexErrorCount();                                             // DAT_106c994c
		return false;
	}
	const int32 Pair = Places->FindCrosswalkPair(Waypoint->NodeId, Waypoint->NextNodeId);   // 0x102f96e0
	if (Pair == INDEX_NONE || !Places->IsCrosswalkRed(Pair))
	{
		return false;
	}
	SetAtCrosswalkLink(Pair);                                                // 0x102a0b90
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
	//     if (m_pPedestrianLink (+0x630c)->+0x64 & (0x10 << (((int)curtime >> 4) & 3))) {
	//         <discarded ConVar read>;  SetCondition(0x13);  return;      // DONTWALK — and the
	//                                                                    // AT_CROSSWALK bit STAYS
	//     }
	//     <discarded ConVar read>;  SetCondition(0x12);                   // WALK
	//     m_bfAINPCFlags &= ~0x4;                                         // release the crosswalk
	//
	// Read the two arms the right way round: the signal SET is DONT-WALK (the pedestrian keeps
	// waiting and stays latched at the crossing), CLEAR is WALK (the pedestrian is released and the
	// latch drops). The three clears run on every pedestrian pass whether or not the NPC is at a
	// crossing, so a condition raised last pass never survives into this one. The stamp is re-armed
	// BEFORE the signal is read, so the 1 s throttle applies to both arms equally.
	//
	// The signal is the pair's one boolean (decision 3). Retail dereferences the link with no NULL
	// test; the port's `INDEX_NONE` (a link a restore could not re-find) reads green, which releases
	// the pedestrian -- the same answer a restored waiter gets in retail, whose link words are not
	// saved and start clear.
	//
	// Conditions `0x10` SHOULD_INTERACT, `0x12` CROSSWALK_WALK and `0x13` CROSSWALK_DONTWALK are the
	// base registrar's (`docs/vtmb/npc-ai/conditions-and-states.md` § "The base condition table").
	// SHOULD_INTERACT is cleared here; its producer `0x102a0cb0` has no recovered caller.
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

	const bool bRed = World != nullptr && World->Places().IsCrosswalkRed(PedestrianPair);
	if (bRed)
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

