// `CAI_BaseNPC`'s declarations of the `Translate19` family (story 5 step 5),
// moved from `ElysiumNpcTranslate19*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseTranslate19.cpp`.

/** SEAM for `CCineNPC::m_fMoveTo` (`cine +0x5f60`), the jump-table selector `0x102cc080`'s live
 *  arm switches on. The cine itself is NOT a seam — `m_hCine` (`+0x5d74`) is bound to
 *  `FElysiumEntity::ScriptOwner` and read through `ScriptOwnerIsLive()` — but this runtime's
 *  scripted-sequence record carries no `m_fMoveTo` column, so the selector answers 0, which is
 *  retail's own "no move" arm (shared with 4). */
int32 TranslateCineMoveTo = 0;

int32 TranslateCineCleanupCalls = 0;

/** `CAI_BaseNPC::TranslateSchedule` (`0x102cc080`). Identity except `0x2e`, which re-dispatches
 *  slot 440 virtually on the mapped id. */
int32 TranslateSchedule(int32 ScheduleNumber) override;
