// Story 29d, family **Social10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl` and story 29c-1's family `.inl`s. A body that fills a Troika-line
// vtable slot is NOT declared here: the generator already declares that virtual and this family only
// defines it (slots 295 and 585). What lands here is the non-slot half — `FinishTalking`, the
// helpers those bodies call, and the seams they read through.
//
// The definitions are in `Substrate/ElysiumNpcKernelSocial10.cpp`; the tests are
// `Tests/ElysiumNpcKernelSocial10Tests.cpp` and the walked prose is `docs/vtmb/npc-ai/social.md`
// § "Story 29d, family Social10 — talking, the tweak file and the dialogue packet".
//
// --- What this family is --------------------------------------------------------------------------
//
// **Whether an NPC will talk, what ends a line, and what the dialogue packet carries.** Ten rows.
// Four are bodies of `CAI_BaseNPCTroika` and land here: slot 295 `CanTalk` (`0x102c21c0`),
// `FinishTalking` (`0x102c0ca0`) and slot 585 `ProcessTweakParam` (`0x1029aa10`).
//
// **Six of the ten are not this class's, and they are ported where they belong.** Stated once here
// rather than left as an absence for a reader to trip over:
//
//   * `CDialog::fill_packet` (`0x100e7da0`), `CDialog::process_npc_line` (`0x100e8100`) and
//     `CDialog::process_pc_line` (`0x100e8520`) are the CONVERSATION OBJECT's, and the overlay names
//     `FElysiumDlgConversation::EnterNpcLine` as their target. Their missing arms land in
//     `Public/ElysiumDlg.h` / `Private/Scripting/ElysiumDlg.cpp` under `namespace ElysiumDlgRetail`,
//     which is the file that carries `EnterNpcLine`. `FElysiumNpc` is not a `CDialog` and a static on
//     it would have been a worse home than the one the overlay already names.
//   * `0x1017c600` and `0x10183120` are `CBasePlayer`'s (`+0x1eb8`, `+0x1ec0`, `+0x1adc`
//     `m_flClientVActiveDisciplineDurations`) and land on `FElysiumPlayer` in
//     `Substrate/ElysiumPlayerEntity.cpp`.
//   * `CDialog::NPCNotifyDoneTalking` (`0x100e4780`) and `CDialog::Pick` (`0x100e4bd0`) are
//     `present` and stay where the port already carries them.
//
// All six are exercised from `Tests/ElysiumNpcKernelSocial10Tests.cpp`, so the family's suite covers
// the family's rows wherever the bodies live.

// --- Slot 295 `CanTalk` (`0x102c21c0`) ------------------------------------------------------------
//
// Fourteen gates, one nested chain in which every failure falls out to 0 and only the innermost line
// returns 1. **Three field names in the checklist walk are rotated and are corrected here from the
// listing and from `vtmb_fields CAI_BaseNPCTroika`**: `m_iDialog` is `+0x128` (`102c21d0`),
// `m_bWillTalk` is `+0x1088` (`102c221e`) and `m_bfNPCStateFlags` is `+0x5b64` (`102c222c`) — the
// walk gives `+0x5b64`, `+0x128` and `+0x1088` respectively. **And the last gate's receiver is
// `this`, not the activator**: `102c2291 MOV EDX,[ESI] / PUSH EDI / MOV ECX,ESI / CALL [EDX+0x650]`
// dispatches slot 404 on the NPC with the activator as the argument, so the question is "what do I
// think of you", not "what do you think of me".

/** SEAM for `thunk_FUN_10175180(activator)` (gate 10) — "the ACTIVATOR's controller NPC is in state
 *  3". `FElysiumNpc::ControllerNpcBusy` (story 29c-1, family Dialogue) is that body, but the word it
 *  reads (`m_hControllerNPC +0x1db0`) sits on `FElysiumNpc` in this port and the activator here is a
 *  player, which `FElysiumPlayer::CanAttemptStealthKill` already records as a shape gap. This
 *  answers FALSE, the ADMITTING value: retail refuses when the test is true. */
bool ActivatorControllerBusy(const FElysiumEntity* Activator) const;

/** SEAM for `thunk_FUN_10146b20(activator, this)` (gate 11) — the cross predicate whose three
 *  passing routes are the activator's `0x10146a80` visibility test failing, the NPC's scripted
 *  stat-list entries 0xe or 1 being positive, and the observer record at `DAT_10738d10 +0x97` with a
 *  squared-distance test against its `+0xac`. `FElysiumDisciplineState` carries the two producer
 *  words (`bObfuscateCloaked`, `bObfuscateDetectionReady`) and spec 0006 owns them; until then this
 *  answers TRUE, which is the ADMITTING value — retail refuses when the predicate is false. */
bool CanTalkObserverPredicate(const FElysiumEntity* Activator) const;

/** SEAM for `thunk_FUN_1023bd00()` and its `+0x4ac` (gate 13) — the world/menu singleton whose word
 *  refuses conversation while a full-screen menu is up.
 *  `FElysiumPlayer::CanAttemptStealthKill` names the same global as a seam. Answers 0, which is
 *  retail's own "the singleton is null or the word is zero" arm: the admitting one. */
int32 DialogMenuBlockWord() const;

/** `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`), gate 9 — the four-term session gate. This runtime
 *  carries the partner as the open dialogue SESSION plus the talk stamp, the reading families
 *  Sounds, SaveRestore10 and Conditions10 all took; this is the one public spelling of it, so a
 *  future reader of `0x102c1170` has a named entry point rather than four file-local twins. */
bool IsInDialog() const;

// --- `CAI_BaseNPCTroika::FinishTalking` (`0x102c0ca0`) ---------------------------------------------
//
// It LATCHES `m_bIsTalking` (`102c0d12 MOV BL,[ESI + 0x64c0]`) before touching anything and uses the
// latched byte at the very end to pick which of the two dialogue-singleton notifications runs.

/** `m_bIsTalking` (`+0x64c0`). Retail's flag and its end time (`+0x64cc m_flTalkEnd`) are written
 *  TOGETHER by the spoken-line player `0x102c0520` (`102c0923 MOV byte [ESI+0x64c0],1` and
 *  `102c092a FSTP float [ESI+0x64cc]`) and are cleared apart by `FinishTalking`, which zeroes the
 *  flag but STAMPS the time with `curtime` rather than clearing it. The port carried both as the one
 *  `TalkingUntil` stamp until story 29d; `FinishTalking` is the body that can tell them apart, so the
 *  byte is declared. `ElysiumNpcKernelShapeMap.cpp` binds `+0x64c0` here and `+0x64cc` to the
 *  stamp. */
bool bIsTalking = false;

/** `CAI_BaseNPCTroika::FinishTalking` (`0x102c0ca0`), 573 bytes. */
void FinishTalking();

/** SEAM for the dialog partner's slot `0x3d4` (**245**) and `CBaseEntity::ThinkSet(partner,
 *  0x101c0b10, 0.0)` followed by `partner->m_flNextThink (+0x17c) = curtime + 0.1`. The partner is
 *  `m_hDialogScene` (`+0x6554`), a `logic_choreographed_scene`; this runtime's scene player carries
 *  its own completion and exposes no per-entity think word to the kernel, so the request is COUNTED
 *  and named. `_DAT_104493d0` is **0.1** — a DOUBLE, read out of the pinned image at
 *  `0x104493d0` (`102c0e29 FADD qword ptr [0x104493d0]`). */
static constexpr double DialogPartnerThinkDelaySeconds = 0.1;   // _DAT_104493d0
int32 DialogPartnerStopRequests = 0;

/** SEAM for `thunk_FUN_101cd940(partner)` — `UTIL_Remove` on a dialog partner that reports NOT done
 *  (`partner->+0x498 == 0`). Counted for the same reason: the scene is not the kernel's to destroy
 *  here, and `FElysiumNpcDialogue::DialogSceneReportsDone` is the `+0x498` seam already built. */
int32 DialogPartnerRemovals = 0;

/** SEAM for `CBaseEntity::ResetScriptedSoundOverrideEnt(this)` (`102c0e8b`). Retail's
 *  `m_iszScriptedSoundOverrideEnt` is `+0x0108` and `ElysiumNpcKernelShapeMap.cpp` binds no port
 *  word to it, so the reset is counted and named. */
int32 ScriptedSoundOverrideResets = 0;

/** The two dialogue-singleton notifications `FinishTalking` picks between on the LATCHED
 *  `m_bIsTalking`: it was CLEAR -> `CDialog::CallPendingNPCEventScript`, it was SET ->
 *  `CDialog::NPCNotifyDoneTalking`. `thunk_FUN_101cd9e0(1)` is `UTIL_PlayerByIndex(1)` and
 *  `thunk_FUN_10178120` reaches the conversation object hanging off that player, so a world with no
 *  player notifies nobody — retail's own `if (iVar3 != 0)` guard. The world's dialogue update
 *  (`ElysiumEntityWorldDialogue.cpp`) is where both land; this records WHICH one was asked for so
 *  the latch is observable in a headless fixture. */
enum class EFinishTalkingNotify : uint8
{
	None,
	CallPendingNpcEventScript,   // the latch was CLEAR
	NpcNotifyDoneTalking,        // the latch was SET
};
EFinishTalkingNotify LastFinishTalkingNotify = EFinishTalkingNotify::None;

// --- Slot 585 `ProcessTweakParam` (`0x1029aa10`) ---------------------------------------------------
//
// The tweak-file key dispatch, in retail's own `__strcmpi` order. Eleven recognised keys plus the
// two that are recognised, print a placeholder and then FALL INTO the ignore message — which is what
// makes `CAPABILITIES` and `GOALS` print twice.

/** One key of `0x1029aa10`'s ladder, in the order the listing compares them. */
enum class ETweakParamKey : uint8
{
	Unknown,
	Capabilities,   // 0x105d976c — DevMsg placeholder, then FALLS THROUGH to the ignore message
	Goals,          // 0x105d96e8 — the same
	NpcPerception,  // 0x105d96a0
	Vision,         // 0x105d9618
	Hearing,        // 0x105d957c
	Squad,          // 0x105d94ec
	IpGroups,       // 0x105d94e0
	HintGroups,     // 0x105d94d0
	TpMoveTimer,    // 0x105d94c0
	IgnoreAttack,   // 0x105d94b0
	NoAlertState,   // 0x105d94a0
};

/** The `__strcmpi` ladder as a pure function. Retail compares against eleven literals in this exact
 *  order and reaches the ignore message on the twelfth miss. */
static ETweakParamKey TweakParamKeyOf(const TCHAR* Key);

/** `_DAT_104454c4` — the shared `0.0f` of `vampire.dll`, the floor `VISION` and `HEARING` compare
 *  against (`1029ab17 FCOM dword ptr [0x104454c4]`). A FLOAT, not a double: family Hints recovered
 *  the same cell as `0.0f` and the `FCOM dword` confirms the width. */
static constexpr float TweakParamNegativeFloor = 0.0f;      // _DAT_104454c4

/** `_DAT_104492dc` — **-1.0f**, read out of the pinned image at `0x104492dc`
 *  (`1029ab31 FCOMP dword ptr [0x104492dc]`). The sentinel that exempts a negative `VISION` or
 *  `HEARING` from the error: `-1` means "derive it", which is what
 *  `ElysiumNpcSense::DerivedSentinel` already is. */
static constexpr float TweakParamDeriveSentinel = -1.0f;    // _DAT_104492dc

/** The `NPCPERCEPTION` clamp band, `1029aa93 CMP EAX,1` and `1029aac3 CMP EAX,0xa`. */
static constexpr int32 TweakParamPerceptionMin = 1;
static constexpr int32 TweakParamPerceptionMax = 10;

/** SEAM for `thunk_FUN_1028fb70` (`InitPerceptionDistances`) and `thunk_FUN_1028fc90`, the two
 *  recomputes `NPCPERCEPTION`, `VISION` and `HEARING` all run — and which run on BOTH the error path
 *  and the accepting path. `FElysiumNpcSense` resolves its distances lazily off the authored words,
 *  so the recompute is expressed as INVALIDATING that resolve; the count is what a test reads. */
void RecomputePerceptionDistances();
int32 PerceptionRecomputes = 0;

/** How many times the body reached `DevMsg(2, "ProcessTweakParam(%s, %s) ignored by base class.")`
 *  (`0x105d96f0`). `CAPABILITIES` and `GOALS` reach it too, which is the recovered fact this counter
 *  makes observable. */
int32 TweakParamsIgnored = 0;

/** How many times `Error()` was reached. Retail's `Error` does not return, so the `NPCPERCEPTION`
 *  clamp that follows it is dead code in retail; this runtime logs and CONTINUES, which is a named
 *  crash guard and the one divergence in this body. */
int32 TweakParamErrors = 0;
