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
/** The species slot-440 bodies, each its class's `TranslateScheduleRetail` override's body. */
int32 AsianVampireTranslateSchedule(int32 Id);   // `0x10362910`
int32 BachTranslateSchedule(int32 Id);   // `0x10363a30`
int32 ChangBrosTranslateSchedule(int32 Id);   // `0x1036b460`
int32 CopTranslateSchedule(int32 Id);   // `0x10372150`
int32 DogTranslateSchedule(int32 Id);   // `0x10374370`
int32 GargoyleTranslateSchedule(int32 Id);   // `0x10378a30`
int32 Guard1TranslateSchedule(int32 Id);   // `0x1037d240`
int32 HengeyokaiTranslateSchedule(int32 Id);   // `0x1037ffa0`
int32 HunterTranslateSchedule(int32 Id);   // `0x10388a40`
int32 MingXiaoTranslateSchedule(int32 Id);   // `0x10394570`
int32 MingXiaoTentacleTranslateSchedule(int32 Id);   // `0x1039e2d0`
int32 SabbatLeaderTranslateSchedule(int32 Id);   // `0x103a7390`
int32 ScurryingTranslateSchedule(int32 Id);   // `0x103ac490`
int32 SheriffManTranslateSchedule(int32 Id);   // `0x103b0320`
int32 TzimisceTranslateSchedule(int32 Id);   // `0x103bd390`
int32 TzimisceHeadClawTranslateSchedule(int32 Id);   // `0x103c1720`
int32 TzimisceRunnerTranslateSchedule(int32 Id);   // `0x103c3560`
int32 WerewolfTranslateSchedule(int32 Id);   // `0x103d5e00`
int32 ZombieTranslateSchedule(int32 Id);   // `0x103df580`
int32 FrenzyShadowTranslateSchedule(int32 Id);   // `0x10375f20`, the controller line's (step 7)
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
