// Story 29e, family **Translate19** — slot 440 `TranslateSchedule` and the frenzied pre-table.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`. The Troika-line virtual is
// already declared as `TranslateSchedule(EElysiumScheduleId)` (story 25); this family adds the
// raw-number body the overlay names and the species tables keyed on retail address.

/** Slot 440's raw registrar-number body. `LastTranslateScheduleRetail` is the number the
 *  typed adapter cannot spell when the target is unregistered. */
int32 TranslateScheduleRetail(int32 ScheduleNumber);
int32 LastTranslateScheduleRetail = 0;

/** `FUN_102b11c0`, the frenzied pre-table. Answers 0 for an id it does not name. */
int32 FrenziedTranslateSchedule(int32 ScheduleNumber);
/** `CAI_BaseNPC::TranslateSchedule` (`0x102cc080`). Identity except `0x2e`, which re-dispatches
 *  slot 440 virtually on the mapped id. */
int32 BaseTranslateSchedule(int32 ScheduleNumber);

/** SEAM for `CCineNPC::m_fMoveTo` (`cine +0x5f60`), the jump-table selector `0x102cc080`'s live
 *  arm switches on. The cine itself is NOT a seam — `m_hCine` (`+0x5d74`) is bound to
 *  `FElysiumEntity::ScriptOwner` and read through `ScriptOwnerIsLive()` — but this runtime's
 *  scripted-sequence record carries no `m_fMoveTo` column, so the selector answers 0, which is
 *  retail's own "no move" arm (shared with 4). */
int32 TranslateCineMoveTo = 0;
int32 TranslateCineCleanupCalls = 0;

/** `CNPC_VHengeyokai` thaw side-effect count (`0x10383130`). */
int32 HengeyokaiThawCalls = 0;
/** `m_nSkin` (`+0x670`) as Hengeyokai's translate body reads it. */
int32 HengeyokaiSkin = 0;
/** `CNPC_VBach::m_bMovementSpot` (`+0x66a7`). */
bool bBachMovementSpot = false;
// `CNPC_VTzimisce` (`0x103bd390`) and `CNPC_VWerewolf` (`0x103d5e00`) stamp retail's own
// `__FILE__`/`__LINE__` into `+0x1b30`/`+0x1b34` before answering. The shape map calls that pair
// ABSENT; the mind's transition trace carries the same account, so no member stands for it.
