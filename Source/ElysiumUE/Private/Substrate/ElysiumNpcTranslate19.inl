// Story 29e, family **Translate19** — slot 440 `TranslateSchedule` and the frenzied pre-table.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`. The Troika-line virtual is
// already declared as `TranslateSchedule(int32)` (story 25); this family adds the
// raw-number body the overlay names and the species bodies.

/** Slot 440's raw registrar-number body — the method species classes override (story 5 step 3).
 *  `LastTranslateScheduleRetail` is the number the typed adapter cannot spell when the target is
 *  unregistered. */
virtual int32 TranslateScheduleRetail(int32 ScheduleNumber);
/** `CAI_BaseNPCTroika::TranslateSchedule` (`0x102b12f0`), the body every species body calls
 *  directly on a miss. */
int32 TroikaTranslateScheduleRetail(int32 ScheduleNumber);
int32 FrenzyShadowTranslateSchedule(int32 Id);   // `0x10375f20`, the controller line's (step 7)
int32 LastTranslateScheduleRetail = 0;

/** `FUN_102b11c0`, the frenzied pre-table. Answers 0 for an id it does not name. */
int32 FrenziedTranslateSchedule(int32 ScheduleNumber);
/** `CAI_BaseNPC::TranslateSchedule` (`0x102cc080`). Identity except `0x2e`, which re-dispatches
 *  slot 440 virtually on the mapped id. */
int32 BaseTranslateSchedule(int32 ScheduleNumber);

// `CNPC_VTzimisce` (`0x103bd390`) and `CNPC_VWerewolf` (`0x103d5e00`) stamp retail's own
// `__FILE__`/`__LINE__` into `+0x1b30`/`+0x1b34` before answering. The shape map calls that pair
// ABSENT; the mind's transition trace carries the same account, so no member stands for it.
