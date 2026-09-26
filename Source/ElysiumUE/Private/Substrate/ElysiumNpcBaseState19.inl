// `CAI_BaseNPC`'s declarations of the `State19` family (story 5 step 5),
// moved from `ElysiumNpcState19*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseState19.cpp`.

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

/** Crash-guard count: `0x1026f590` ORs `0x80002000` into a non-NPC move parent. Retail faults. */
int32 SelectIdealStateNonNpcParentFlagWrites = 0;

/** SEAM for `thunk_FUN_101e3d70(&DAT_10739a4c, this)`, the discipline manager's break-on-notice
 *  sweep at the end of `CAI_BaseNPC::SelectIdealState` case 3's hear arm (`1026f91b`). It walks
 *  `this +0xf34`'s discipline bitmask and `RemoveEffect`s (`0x101e3af0`) every discipline whose
 *  record carries a non-zero byte at `+0x32`. `FElysiumDisciplines` has no "strip the effects that
 *  break on notice" accessor — the break flag itself is unrecovered — so the sweep is counted and
 *  strips nothing. Its only other retail caller is `SetEnemy` (`0x10279a50`), which the port's
 *  `ElysiumNpcEnemy::SetEnemy` does not carry either. */
int32 SelectIdealStateDisciplineStripCalls = 0;
