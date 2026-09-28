// `CAI_BaseNPC`'s declarations of the `State19` family (story 5 step 5),
// moved from `ElysiumNpcState19*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseState.cpp`.

/** Slot 461's raw answer, kept beside the typed virtual because this runtime's
 *  `EElysiumNpcState` has no member for retail 8 / 0xb / 0xe. */
int32 LastSelectIdealStateRetail = 0;

// Retail stamps `m_SelectIdealStateTrace`'s `__FILE__` (`+0x1b3c`) and `__LINE__` (`+0x1b40`) at
// every arm of every slot-461 body. The shape map calls that pair ABSENT; the mind's transition
// trace carries the same account, so no member stands for it. Only `+0x1b38`, the selector tag,
// is real here — 29d stood it as `SelectIdealStateSelector`.
/** Slot 463's arguments as retail ids. `SetState` writes these before the typed virtual so Bach
 *  and Cop can switch on 8 / 0xb / 0xe which `EElysiumNpcState` cannot spell. */
int32 LastOnStateChangeOldRetail = 0;

int32 LastOnStateChangeNewRetail = 0;

/** How many times the base idle/alert hear arms reset the motor (`0x102e0b40`). */
int32 SelectIdealStateMotorResets = 0;

/** How many times `SquadNewEnemy` (`0x103161a0`) was reached. */
int32 SelectIdealStateSquadNewEnemyCalls = 0;

/** How many times case 4 ran `0x1027d0a0` (script-fail / cine release). */
int32 SelectIdealStateScriptExitCalls = 0;

/** SEAM for `thunk_FUN_101e3d70(&DAT_10739a4c, this)`, the discipline manager's break-on-notice
 *  sweep at the end of `CAI_BaseNPC::SelectIdealState` case 3's hear arm (`1026f91b`). It walks
 *  `this +0xeb4`'s discipline bitmask (`FElysiumNpc::DisciplineFlags2`, read at `0x101e3d7f`) and `RemoveEffect`s (`0x101e3af0`) every discipline whose
 *  record carries a non-zero byte at `+0x32`. `FElysiumDisciplines` has no "strip the effects that
 *  break on notice" accessor — the break flag itself is unrecovered — so the sweep is counted and
 *  strips nothing. Its only other retail caller is `SetEnemy` (`0x10279a50`), counted there as
 *  `SetEnemyDisciplineStripCalls` (`ElysiumNpcBaseMisc2.inl`). */
int32 SelectIdealStateDisciplineStripCalls = 0;

/** `m_NPCState` (`+0x5cc0`) as retail's raw id. Maps the typed mind when `SetState` has not
 *  written an unmapped value (8 FLEE, 0xb HUNT, 0xe). */
int32 NpcStateRetail() const;

void WriteNpcStateRetail(int32 RetailId);

/** `m_IdealNPCState` (`+0x5cc4`) as retail's raw id. */
int32 IdealStateRetail() const;

void WriteIdealStateRetail(int32 RetailId);

/** `CAI_BaseNPC::SetState` (`0x1026e340`). Not a vtable slot. Writes both state words, strips
 *  the enemy on a transition to IDLE, and dispatches slot 463 with the entry-time old state. */
void SetState(int32 NewRetail);

/** `CAI_BaseNPC::PreSelectIdealState` (`0x1026f590`), slot 460's base body, in retail ids: the
 *  `+0x98` Troika arm or `SquadNewEnemy`, always answering 0. The typed slot body wraps it. */
int32 BasePreSelectIdealState();

/** `CAI_BaseNPC::SelectIdealState` (`0x1026f660`), slot 461's base body, in retail ids. The typed
 *  slot body wraps it; the Troika's `TroikaSelectIdealState` falls through to it. */
int32 BaseSelectIdealState();

/** Slots 460 / 461 in retail ids: the vtable entries `0x1026f4d0` dispatches (`0x1026f4ec`,
 *  `0x1026f4f8`). The base answers its own bodies; the Troika line overrides both. */
virtual int32 PreSelectIdealStateRetail() { return BasePreSelectIdealState(); }
virtual int32 SelectIdealStateRetail() { return BaseSelectIdealState(); }

/** Slots 460 / 461 as this runtime's typed state: the census names, wrapping the two `Retail`
 *  virtuals above. 0019/8's shape commit mapped the slots to the `Retail` virtuals (the ones the
 *  species override and `0x1026f4d0` dispatches), so the generated surface stopped declaring these;
 *  the bodies are unchanged (`ElysiumNpcBaseState.cpp`). */
virtual EElysiumNpcState PreSelectIdealState();
virtual EElysiumNpcState SelectIdealState();
