// Story 29c-1, family **Dialogue** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelDialogue.cpp` and the tests in
// `Tests/ElysiumNpcKernelDialogueTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// **What the family turned out to BE.** "Dialogue" is 29c's name for the bucket, not a subsystem:
// the fourteen rows are four unrelated retail owners that happen to share the word.
//
//   * `CDialog` — two of its own methods (`message_send` `0x100e58e0`, `goto_line_for_response`
//     `0x100e4840`). They land on `FElysiumNpc` beside `CDialog`'s other words, which family
//     EntityChain already put there (`ElysiumNpcKernelEntityChain.inl` § "`CDialog`'s own words").
//     The dialogue SESSION (`Scripting/ElysiumDlg.cpp`, `FElysiumDlgConversation`) is spec 0004's
//     and NOTHING of it is re-derived here: every mechanism these two bodies end on is a seam that
//     records the request and answers nothing.
//   * `CBasePlayer` — the cine-camera getter (`0x1017cf90`), the controller-NPC factory
//     (`0x10161a70`), the controller-busy predicate (`0x10175180`) and the two pursuit-count
//     getters (`0x1017f770`, `0x1017f8b0`). The first three run over words EntityChain already
//     declared here (`ControllerNpc`, `+0x1db0`); the last two land on `FElysiumPlayer`, whose
//     `Police` record already carries both counts.
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

/** `+0x6660 m_iszLinkedCamera` — the authored `targetname` of the `CSecCamera` this security-camera
 *  NPC watches through. A datamap keyfield in retail; `npc_VCameraSecurity` is NOT a registered
 *  spawn leaf in this runtime, so no keyfield is wired to it and it stands empty. */
FString LinkedCameraName;   // +0x6660

/** `+0x6664 m_hLinkedCamera` — the resolved handle, cached by `0x10369e70` and re-resolved whenever
 *  it goes dead. */
FElysiumEntityHandle LinkedCamera;   // +0x6664

/** `+0x6668` — "this NPC has been linked at least once". Set the first time the handle resolves and
 *  never cleared; its only consumer is `0x10369e70`'s own teardown arm, which `UTIL_Remove`s the NPC
 *  when a camera that WAS linked has gone. Retail name **unrecovered**. */
bool bLinkedCameraBound = false;   // +0x6668

// --- The pedestrian crosswalk link (`+0x630c`, and the one word retail reads off it) --------------

/** The navigation-link object at `+0x630c`, which the shape map records as ABSENT ("no
 *  navigation-link object exists in this runtime's motor seam"). This family's two bodies read
 *  exactly ONE word off it — `link+0x64`, the retail flags whose four bits `0x10`/`0x20`/`0x40`/
 *  `0x80` are the crosswalk signal's four phases — and write the pointer itself once, in
 *  `0x102a0b90`. **SEAM**: nothing in this runtime produces a crosswalk link, so the pair below is
 *  written only by `SetAtCrosswalkLink` and by a test driving the rule. */
bool bCrosswalkLinkBound = false;       // +0x630c != NULL
int32 CrosswalkLinkSignalFlags = 0;     // link +0x64

// --- `CBaseEntity + 0x8c`, the cached use activator -----------------------------------------------

/** `+0x8c` — the handle of whoever last began a `+use` on this entity, written by slot 39 and
 *  cleared to `-1` by slot 42 and by a null activator. Not a datamap field and not in
 *  `ElysiumNpcKernelShapeMap.cpp` (which covers the `CAI_BaseNPC` band only); its retail name is
 *  **unrecovered**. It has no reader in this family's closure — the two slots are its only
 *  toucher — so it is carried as the recorded write rather than wired to a consumer. */
FElysiumEntityHandle UseActivator;   // +0x8c

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

/** `CBasePlayer::GetControllerNPC`'s `CreateEntityByName(classname)` (`thunk_FUN_10136580`). **SEAM**:
 *  the kernel does not spawn entities; the world's registry does, and no kernel body may reach it
 *  without becoming a second spawner. Answers null, which takes retail's OWN
 *  `"GetControllerNPC(): ... created NULL Entity ..."` arm — the arm that clears `m_hControllerNPC`
 *  to -1 — rather than refusing the body. */
FElysiumEntity* CreateControllerNpcEntity(const TCHAR* Classname);

/** `m_pNavigator->GetPathType()` — retail's `path+0x30`, read through `0x102ee620` off `+0x5d34`.
 *  `8` is the pedestrian/crosswalk path type both crosswalk bodies gate on. **SEAM**: the shape map
 *  routes `+0x5d34` to `FElysiumScriptedCharacter::Motor`, "the one motor seam this chain stands
 *  beside the body", and that motor carries no path object at all. `-1` is neither `8` nor any
 *  other retail type; **nothing in this runtime writes it**, and it is a member rather than a
 *  literal so 0002's navigator half has one place to land it. */
int32 NavigatorPathTypeWord = -1;   // navigator's path +0x30
int32 NavigatorPathType() const;

/** `0x102f96e0` — given a waypoint's owning entity and a link id, the path node whose id matches.
 *  **SEAM**: this runtime stands no node graph; answers false and writes nothing, so
 *  `ResolvePedestrianPathNode` reaches its own no-node arm. `OutSignalFlags` is the node's `+0x64`.
 *  The request is RECORDED so a test can assert the seam is REACHED rather than only that the body
 *  refused. */
mutable TArray<FString> CrosswalkNodeQueries;
bool FindCrosswalkPathNode(int32 EntityIndex, int32 LinkId, int32& OutSignalFlags) const;

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

/** `CBasePlayer::GetControllerNPC` (`0x10161a70`, 534 bytes) — the cached `npc_VPlayerController`
 *  of the requested classname, created on demand. Writes `m_hControllerNPC` (`+0x1db0`). */
FElysiumEntity* GetControllerNpc(const TCHAR* Classname);

/** `0x10175180` (116 bytes) — "the controller NPC at `m_hControllerNPC` is in state 3". Retail name
 *  **unrecovered**; 29c's walk called `+0x1db0` a dialogue partner, which `vtmb_fields CBasePlayer`
 *  contradicts — it is `m_hControllerNPC`, the same word `GetControllerNPC` caches into. */
bool ControllerNpcBusy() const;

/** `CPayphone::CanTalk` (`0x101aaee0`, 119 bytes) — slot 295's `CPayphone` override, seven arms. */
bool PayphoneCanTalk(const FElysiumEntity* Activator) const;

/** `CNPC_VSabbatLeader::HandleInteraction` (`0x103a76d0`, 111 bytes) — slot 366's `CNPC_VSabbatLeader`
 *  override. */
bool SabbatLeaderHandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other);

/** One row of the slot-366 species table: which body fills `HandleInteraction` for a class, and what
 *  that body answers. Both recovered bodies answer FALSE; the table exists because the two are
 *  DIFFERENT addresses and `slots.md` must be checkable against the port. */
struct FHandleInteractionSpecies
{
	const TCHAR* RetailClass = nullptr;   // the census class this row came from
	const TCHAR* Body = nullptr;          // the retail address that fills slot 366 for it
	bool bAnswer = false;                 // what that body returns
};
static const FHandleInteractionSpecies* HandleInteractionSpeciesRows(int32& OutCount);
static const FHandleInteractionSpecies* HandleInteractionSpeciesOf(const TCHAR* InRetailClass);

/** `CNPC_VCameraSecurity`'s linked-camera resolve (`0x10369e70`, 252 bytes). Retail is unnamed; the
 *  name is inferred from the RTTI cast it performs (`CSecCamera`) and the census field names. */
FElysiumEntity* ResolveSecCameraLink();

/** The navigator waypoint `0x102a0bc0` is handed, as the four words it reads off it. Retail's
 *  argument is `navigator->CurWaypoint` (`FUN_102f0400` fetches it from `path+0x24`) and the offsets
 *  are the body's own: `+0x10` an entity INDEX (negative = none), `+0x28` a flag byte whose bit 2
 *  must be set, `+0x30` a hint record (NULL = none) and `hint+0x10` its link id. **SEAM**: this
 *  runtime's motor carries no waypoint, so nothing constructs one outside a test. */
struct FDialogPedWaypoint
{
	int32 EntityIndex = INDEX_NONE;   // waypoint +0x10
	uint8 Flags = 0;                  // waypoint +0x28 — bit 2 (`0x4`) is the gate
	bool bHasHint = false;            // waypoint +0x30 != NULL
	int32 HintLinkId = 0;             // hint +0x10
};

/** `0x102a0bc0` (178 bytes) — the navigator's "I have reached a crosswalk waypoint" hook. Retail is
 *  unnamed and 29c's target name is kept. */
bool ResolvePedestrianPathNode(const FDialogPedWaypoint* Waypoint);

/** `0x102a0b90` — `m_bfAINPCFlags |= AT_CROSSWALK (0x4); m_pCrosswalkLink = link;`. The two-line
 *  helper `ResolvePedestrianPathNode` ends on, spelled as a method so the write is assertable. */
void SetAtCrosswalkLink(int32 SignalFlags);

/** `CAI_BaseNPCTroika::UpdatePedestrianInfo` (`0x102a0d20`, 313 bytes) — the per-think crosswalk
 *  rule. `RunAI` (slot 432, `0x1028fcc0`) is its ONE caller and is still story 29e's generated stub,
 *  so nothing in this runtime calls it yet. */
void UpdatePedestrianInfo();

/** `0x10 << (((int)CurTime >> 4) & 3)` — the crosswalk signal's phase mask, shared verbatim by
 *  `0x102a0bc0` and `0x102a0d20`. A 64-second cycle in four 16-second phases; the float-to-int
 *  truncation is retail's `__ftol`. */
static int32 CrosswalkPhaseMask(double CurTime);
