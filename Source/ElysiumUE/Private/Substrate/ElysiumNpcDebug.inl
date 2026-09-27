// Story 29c-1, family **Debug** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcDebug.cpp` and the tests in
// `Tests/ElysiumNpcKernelDebugTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.

// --- What a debug body IS, for this family --------------------------------------------------------
//
// Twenty-one rows, and every one of them is a `DevMsg`, a debug-message ring write, an entity text
// overlay or an `NDebugOverlay` draw. What a program can observe about such a body is NOT the
// picture: it is the ORDER in which the body reads state, the GATES under which it says anything at
// all, and the side effects it performs on the way (`DrawDebugGeometryOverlays`'s `0x10000` arm
// vacates a squad slot, drops the weapon and schedules the NPC for removal — a debug bit with real
// consequences). All three are ported verbatim below; retail's own format strings are reproduced as
// strings because the text is the evidence for the arm.
//
// This runtime has no debug renderer and no `Msg` ring, so every one of the four retail output
// channels lands on ONE seam — `FDebugLine` — which writes `LogElysiumNpcEnt` and, while a capture
// is open, records the line. The capture is what a test reads: a body's recovered arm order is a
// list of `FDebugLine`s, and `Retail` carries retail's own format string so an assertion names the
// evidence rather than the port's paraphrase.

/** One line a debug body emitted. */
struct FDebugLine
{
	// The retail output channel this line went to. One of the four spellings below.
	//   `DevMsg`     — `DevMsg(fmt, …)`, the dev console (`CAI_BaseNPC::ReportAIState`).
	//   `Msg`        — the ConVar-gated debug-message ring `0x10119750` writes 0x60 bytes into
	//                  (`DrawDebugStatOverlays`'s whole output).
	//   `EntityText` — `DAT_1070b22c`+0x8c, `IVEngineServer::AddEntityTextOverlay(edict, line, …)`.
	//   `Overlay`    — `NDebugOverlay::Box` / `BoxDirection` / `Line` / `Text` / `EntityBounds`.
	const TCHAR* Channel = nullptr;
	// Retail's own format string, or the retail overlay call's name, verbatim. This is the evidence
	// that the arm was taken; a test asserts on it rather than on the formatted text.
	const TCHAR* Retail = nullptr;
	// The formatted line, or the overlay call's arguments spelled out in SOURCE units.
	FString Text;
	// `EntityText`'s line index, `INDEX_NONE` on every other channel. Retail's text overlays are
	// numbered from whatever `CBaseEntity::DrawDebugTextOverlays` returned, and the numbering is the
	// contract between a base body and its override.
	int32 Line = INDEX_NONE;
};

/** Open a capture. Every `FDebugLine` a debug body emits until `EndDebugCapture` is recorded in
 *  emission order. Game-thread only, like the rest of the substrate; nesting is not supported and a
 *  second `Begin` simply resets the list. */
static void BeginDebugCapture();

/** Close the capture and hand back what was recorded, in emission order. */
static TArray<FDebugLine> EndDebugCapture();

// --- The two id spaces `ConditionName` and `TaskName` translate through --------------------------
//
// `CAI_ClassScheduleIdSpace` is four `CAI_LocalIdSpace`s in a row — schedules at `+0x00`, tasks at
// `+0x18`, conditions at `+0x30`, squad slots at `+0x48` (family Schedule's `FScheduleIdSpace` states
// the same layout, and family Squad's table reaches the fourth). Each is
// `+0x00 m_globalBase`, `+0x04 m_localBase`, `+0x08 m_localTop`, `+0x10` the PARENT space, and 9999
// in `LocalBase` is retail's "this space holds no ids" sentinel, which `0x102ea2d0` tests by name.

// --- The three global name tables -----------------------------------------------------------------

// --- Slot 408 `GetShortConditionName` -----------------------------------------------------------
//
// Four retail bodies fill slot 408. `CAI_BaseNPC`'s (`0x1027ede0`) forwards to the table above, and
// three species (`CNPC_VMingXiao`, `CNPC_VMingXiaoTentacle`, `CNPC_VWerewolf`) prepend a contiguous
// block of their OWN ids from 0x77, straight above the base's 0x76, and fall through to that same
// forward for everything else; each block is its class's own override (story 5 step 4).

// --- Slot 76 `DrawDebugStatOverlays`: three bodies, one slot ---------------------------------------
//
// `CAI_BaseNPC#76` (`0x102775e0`) is the sequence/activity/state/schedule dump. `CAI_BaseNPCTroika#76`
// (`0x1029c010`) replaces it with the expression/gesture dump — but only when `m_iDialog` is set,
// and otherwise TAIL-CALLS the base. `CNPC_VBaseBoss#76` (`0x10366290`) prints one distance line and
// then calls the BASE body directly, skipping the Troika dump entirely. The slot dispatches; the
// three arms are named below.

/** `CAI_BaseNPCTroika::DrawDebugStatOverlays` (`0x1029c010`) past its `m_iDialog == 0` arm — the
 *  expression/gesture dump proper. Slot 76 takes this arm only for an NPC that has a dialogue. */
void TroikaDrawDebugStatOverlays();

// --- The remaining bodies -------------------------------------------------------------------------

// --- The seams these bodies read through ----------------------------------------------------------

/** `0x100eccf0(&DAT_10924980, m_nCurrDisposition)` — the disposition table's DEFAULT eye target for
 *  a disposition, the middle `%d` of the Troika stat overlay's eye-target line. **SEAM**: the port's
 *  `FElysiumDisposition` row carries no such index; answers -1. */
int32 DispositionDefaultEyeTarget() const;

void EntityDrawBBoxOverlay() const;

/** `CAI_Enemies`'s record list (slot 541 `GetEnemies()`, then `+0xc` head and `+0x38` next), which
 *  slot 123's `0x20000` arm walks. This runtime's `FElysiumNpcEnemyMemory` IS that store, so the
 *  walk is real; what is a seam is `CBaseEntity+0x9c m_pCombatCharacter` (the arm skips a record
 *  whose entity is not a combat character) and `+0xa8 m_pPlayer`. Both are answered from the port's
 *  own `AsCombatCharacter()` and a compare against `FElysiumEntityWorld::FindPlayer()`, which are
 *  the same two questions. */
