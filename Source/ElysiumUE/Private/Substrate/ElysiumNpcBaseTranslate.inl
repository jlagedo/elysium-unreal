// `CAI_BaseNPC`'s declarations of the `Translate19` family (story 5 step 5),
// moved from `ElysiumNpcTranslate19*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseTranslate.cpp`.

/** `CCineNPC::m_fMoveTo` (`cine +0x5f60`), the jump-table selector `0x102cc080`'s live arm switches
 *  on: the resolved director's own word (story 5 fold A3), 0 with no director. */
int32 TranslateCineMoveTo() const;

/** How many times the "Script failed" arm ran `CineCleanup` (`0x1027d170`) — the count a test reads. */
int32 TranslateCineCleanupCalls = 0;

/** `CAI_BaseNPC::TranslateSchedule` (`0x102cc080`). Identity except `0x2e`, which re-dispatches
 *  slot 440 virtually on the mapped id. */
int32 TranslateSchedule(int32 ScheduleNumber) override;
