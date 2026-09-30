// Story 29c-1, family **Dialogue** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcDialogueBodies.cpp` and the tests in
// `Tests/ElysiumNpcKernelDialogueTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// **What the family turned out to BE.** "Dialogue" is 29c's name for the bucket, not a subsystem:
// the fourteen rows are four unrelated retail owners that happen to share the word.
//
//   * `CDialog` — two of its own methods (`message_send` `0x100e58e0`, `goto_line_for_response`
//     `0x100e4840`). They land on `FElysiumNpc` beside `CDialog`'s other words, which family
//     EntityChain already put there (`ElysiumNpcEntityChain.inl` § "`CDialog`'s own words").
//     The dialogue SESSION (`Scripting/ElysiumDlg.cpp`, `FElysiumDlgConversation`) is spec 0004's
//     and NOTHING of it is re-derived here: every mechanism these two bodies end on is a seam that
//     records the request and answers nothing.
//   * `CBasePlayer` — the cine-camera getter (`0x1017cf90`) and the two pursuit-count getters
//     (`0x1017f770`, `0x1017f8b0`), which land on `FElysiumPlayer`, whose `Police` record already
//     carries both counts. The controller-NPC factory (`0x10161a70`) and the controller-busy
//     predicate (`0x10175180`) are the world's and the player's (`CBasePlayer::m_hControllerNPC`
//     `+0x1db0` has one home, `FElysiumEntityWorld::PlayerControllerHandle`; story 5 commit B
//     deleted the NPC-side copy).
//   * `CAI_BaseNPCTroika` — the crosswalk pair (`0x102a0bc0`, `0x102a0d20`), which is the pedestrian
//     traffic-light rule and the family's only condition producer.
//   * Two species overrides — `CPayphone::CanTalk` (`0x101aaee0`) and
//     `CNPC_VSabbatLeader::HandleInteraction` (`0x103a76d0`) — plus `CNPC_VCameraSecurity`'s
//     linked-camera resolve (`0x10369e70`).
//
// **The kernel's dialogue words already have owners and this file stands no second one.** Family
// EntityChain landed `CDialog`'s `+0x2810`/`+0x2820`/`+0x2834` line table, its speaker handle and
// `+0x31ea`; family TroikaHelpers landed `OnDialogRelease` and `DialogPartnerClears`; family Senses
// landed `IsInDialog`'s forcing arm in `ShouldTransmit`; family Anim landed `HasLiveDialogPartner`;
// family Sounds landed `SoundsIsInDialog`. Only words no file above declares appear below.

// --- `CDialog`'s remaining words (+0x2830, +0x2838, +0x30e9, +0x08) --------------------------------
//
// `CDialog` is the dialogue FILE object (`0x100e5410 CDialog::load`), not an entity; family
// EntityChain put its words on `FElysiumNpc` because this runtime's conversation state lives on the
// NPC that owns the session. These four are the ones EntityChain's two rows did not need.

/** `CDialog + 0x04` — the RECIPIENT `EHANDLE`, the player every user message `message_send` builds
 *  is filtered to. A second entity handle in word 1 of the object, beside EntityChain's speaker at
 *  word 0; `message_send` resolves them separately and for different purposes, so they are two
 *  words and not one. The class's own datamap covers neither; that both are entity handles is read
 *  off the `PTR_DAT_10566458` resolves the body performs on them. */
FElysiumEntityHandle DialogRecipient;   // +0x04

/** `CDialog + 0x2830` — the id of the NPC line currently being spoken; **0 means "no line"**.
 *  Three bodies read it and all three agree on that reading: `LookupSpeechFile(this, +0x2830)`
 *  (`0x100e1880`) resolves the line's `.wav`, `0x100e58b0` answers "this queue is final" when it is
 *  ZERO, and `goto_line_for_response` refuses outright unless it is strictly positive. */
int32 DialogCurrentLineId = 0;   // +0x2830

/** `CDialog + 0x2838` — one int per SHOWN response, parallel to EntityChain's `DialogPcLines` and
 *  counted by the same `+0x2834`: the id `goto_line_for_response` looks up in the current node's
 *  response table. A zero entry is retail's "this row targets nothing". */
TArray<int32> DialogResponseTargetIds;   // +0x2838

/** `CDialog + 0x30e9` — a byte `0x100e58b0` reads as the FIRST of its two terms, so a set byte makes
 *  the queued speech final regardless of `+0x2830`. Its writer is not in this family's closure and
 *  is **unrecovered**; nothing in this runtime sets it. */
bool bDialogFinalLatch = false;   // +0x30e9

/** One record of the current node's response table — retail's `node+0x10c` array of 0x34-byte rows,
 *  counted by `node+0x104`. `goto_line_for_response` touches exactly two of the thirteen words:
 *  `+0x00`, which it MATCHES on, and `+0x0c`, which it returns on a hit. */
struct FDialogNodeResponse
{
	int32 Id = 0;         // record +0x00 — matched against the shown response's target id
	int32 GotoLine = 0;   // record +0x0c — the line the conversation goes to
};

/** `CDialog + 0x08` — the current node record, as the only two words this family reads off it.
 *  `bDialogNodeBound` is retail's `this+8 != 0`: a CLEAR flag is "no node loaded", which is a
 *  different refusal from a node with an empty table. */
bool bDialogNodeBound = false;
TArray<FDialogNodeResponse> DialogNodeResponses;   // node +0x10c, count node +0x104

/** `CDialog::message_send`'s caller-owned page buffer, as retail lays it out. Retail's argument is a
 *  raw block and every offset below was read off the listing (`corpus asm 100e58e0`):
 *
 *      +0x0000  the NPC's own response text
 *      +0x0804 + (i-1)*0x800   response text i, for i = 1 .. +0x2834
 *      +0x2804 + i*4           an int per response, written as a BYTE, for i = 0 .. +0x2834-1
 *      +0x2814 + i*4           an int per response, written through engine slot 68, same range
 *
 *  Note the two ranges are different: the text loop runs 1..N inclusive (N+1 lines in all), the two
 *  int loops run 0..N-1. That asymmetry is retail's and is reproduced rather than squared up. What
 *  the two int arrays MEAN is **unrecovered** — they are the HUD's per-row payload and the client
 *  side is not in this corpus. */
struct FDialogResponsePage
{
	FString NpcText;                 // page +0x0000
	TArray<FString> ResponseText;    // page +0x0804, stride 0x800
	TArray<int32> ResponseWordA;     // page +0x2804, stride 4
	TArray<int32> ResponseWordB;     // page +0x2814, stride 4
};

// --- `CNPC_VCameraSecurity`'s linked camera (+0x6660 … +0x6668) -----------------------------------
//
// Three words the `CAI_BaseNPCTroika` shape map does not cover: they sit in the per-subclass band
// past the Troika line, where the census (`ElysiumNpcKernelShape.cpp`) names them per class. The
// census rows are `{0x6660, CNPC_VCameraSecurity, m_iszLinkedCamera, string_t, Datamap}` and
// `{0x6664, CNPC_VCameraSecurity, m_hLinkedCamera, EHANDLE, Walked}`; `+0x6668` has no census row
// and is named from what `0x10369e70` does with it.

// --- The pedestrian crosswalk link (`+0x630c`) ----------------------------------------------------
//
// `+0x630c m_pPedestrianLink` (a `CAI_Link*`) is `FElysiumNpc::PedestrianPair` (`ElysiumNpc.h`): the
// link as a crosswalk pair index into the entity world's place set, `INDEX_NONE` for NULL. The one
// word the two bodies below read off the link, `link+0x64`'s signal nibble, is the place set's
// `IsCrosswalkRed(pair)` (0018 story 7, decision 3).

// --- `CBaseEntity + 0x8c`, the cached use activator -----------------------------------------------

// --- The seams ------------------------------------------------------------------------------------
//
// Each answers NOTHING and names the retail call it stands for. None of them invents a value.

/** `CDialog::LookupSpeechFile(this, m_CurrentLineId)` (`0x100e1880`) — the `.wav` the current NPC
 *  line is spoken with, and the whole of `message_send`'s branch condition. **SEAM**: resolving a
 *  speech file is the dialogue session's job (`Scripting/ElysiumDlg.cpp`, spec 0004) and the kernel
 *  reaches no `.dlg` catalogue. Empty is "no speech file", which takes retail's OWN no-reply arm —
 *  the arm that shows the player's choices and the history window — rather than refusing the body. */
FString DialogSpeechFile;

/** `CDialog::message_append(this, index, text)` (`0x100e5620`-line thunk `0x1001269d`) and
 *  `CDialog::ShowPlayerChoices` / `CDialog::ShowHistoryWindow`. **SEAM**: all three are the dialogue
 *  box's, which this runtime renders through the presenter rather than through `CDialog`. The
 *  requests are RECORDED, in order, and nothing is called. Read by the test and by nothing else. */
TArray<FString> DialogUiCalls;

/** The three `CReliableSingleUserRecipientFilter` user messages `message_send` sends to the
 *  recipient at `CDialog+0x04` — engine slots 65 (`UserMessageBegin`), 67 (`WriteByte`), 68 and 66
 *  (`MessageEnd`). **SEAM**: this runtime has no user-message transport; each message is recorded as
 *  `"<opcode>:<byte>,<byte>,…"` so the ORDER and the payload are assertable. */
TArray<FString> DialogUserMessages;

// --- The bodies -----------------------------------------------------------------------------------

/** `CDialog::message_send` (`0x100e58e0`, 716 bytes) — push one conversation turn at the recipient.
 *  Three user messages (`0`: open with the response count; `2`: the two per-response int arrays;
 *  `3`: close), the console `Msg`, the page's N+1 `message_append`s, and the has-reply /
 *  no-reply fork on `LookupSpeechFile`. Every mechanism is a seam above. */
void DialogMessageSend(const FDialogResponsePage& Page);

/** `0x100e58b0` — `return m_bFinalLatch (+0x30e9) || m_CurrentLineId (+0x2830) == 0;`. The
 *  `bIsFinal` argument `message_send` hands to `SetDialogQue`. */
bool DialogQueIsFinal() const;

/** `CAI_BaseNPCTroika::SetDialogQue` (`0x102c0470`) — `Q_strncpy(m_szDialogQue +0x64ec, name, 0x60)`
 *  then `m_bDialogQueIsFinal (+0x654c) = bFinal`. Called ON THE SPEAKER, not on the dialogue object;
 *  the 0x60 truncation is retail's and is reproduced. */
void SetDialogQue(const FString& SpeechFile, bool bFinal);

/** `CDialog::goto_line_for_response` (`0x100e4840`, 281 bytes) — the line the conversation goes to
 *  when the player picks shown response `ResponseIndex`. */
int32 DialogGotoLineForResponse(int32 ResponseIndex);

/** `CBasePlayer::GetCineCamera` (`0x1017cf90`, 111 bytes) — the adopted cine camera, or null.
 *  `m_iCameraOverrideIdx` (`+0x1ec4`) must be strictly positive AND `+0x19b4` must resolve. */
FElysiumEntity* GetActiveCameraEntity() const;

/** The navigator waypoint `0x102a0bc0` is handed, as the words it reads off it. Retail's argument is
 *  `navigator->CurWaypoint` (`FUN_102f0400` / `0x10298340` fetch it from `path+0x24`), a
 *  `AI_Waypoint_t`: `+0x00..+0x08` the position, `+0x10` its graph NODE id (-1 for a waypoint that
 *  is no node: the goal, a detour), `+0x28` the flag word (`4` bits_WP_TO_NODE, `8` the goal,
 *  `0x20` bits_WP_DONT_SIMPLIFY, which the pedestrian builder `0x102fcd00` puts on both curbs of a
 *  crosswalk pair), `+0x30` the NEXT waypoint (NULL on the last) and `next+0x10` its node id
 *  (corrected 2026-09-19: the 09-13 walk read `+0x10` as an entity index and `+0x30` as a hint).
 *  The port builds one from the head of `PedestrianLegs` (`PedestrianHeadWaypoint`). */
struct FDialogPedWaypoint
{
	FVector DestCm = FVector::ZeroVector;   // waypoint +0x00..+0x08, centimetres
	int32 NodeId = INDEX_NONE;              // waypoint +0x10
	int32 Flags = 0;                        // waypoint +0x28 — bit 2 (`0x4`) is the gate
	bool bHasNext = false;                  // waypoint +0x30 != NULL
	int32 NextNodeId = INDEX_NONE;          // next waypoint +0x10
};

/** `0x102a0bc0` (178 bytes) — "must I wait at this curb": the waypoint's node and the next one's
 *  name a crosswalk link whose signal is red. Called by the waypoint advance `0x102f0400` (answer
 *  ignored) and the obstruction sink `0x10298340`. Retail is unnamed and 29c's target name is kept. */
bool ResolvePedestrianPathNode(const FDialogPedWaypoint* Waypoint);

/** `0x102a0b90` — `m_bfAINPCFlags |= AT_CROSSWALK (0x4); m_pPedestrianLink (+0x630c) = link;`, the
 *  link carried as its crosswalk pair (`PedestrianPair`). The one port of the helper: the Troika
 *  family's second copy (`SetAtCrosswalk`, which read `+0x630c` as a node id) is deleted. */
void SetAtCrosswalkLink(int32 Pair);

/** `CAI_BaseNPCTroika::UpdatePedestrianInfo` (`0x102a0d20`, 313 bytes) — the per-think crosswalk
 *  rule, from `RunAI` (slot 432, `0x1028fcc0`) before conditions are gathered. */
void UpdatePedestrianInfo();

// The pedestrian route's crosswalk legs, the obstruction sink and the link's save words (0018
// story 7): the crosswalk lane's own declarations, with their bodies in `ElysiumNpcCrosswalk.cpp`.
#include "Substrate/ElysiumNpcCrosswalk.inl"
