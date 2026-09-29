// `CAI_BaseNPC`'s declarations of the `Dialogue` family (story 5 step 5),
// moved from `ElysiumNpcDialogue*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseDialogue.cpp`.

/** `m_pNavigator->GetGoalType()` -- retail's `path+0x5c`, read through `0x102ee620` off `+0x5d34`
 *  (`Navigator.GetGoalType()`). `8` is the pedestrian/crosswalk goal type both crosswalk bodies gate
 *  on. No goal: 0. */
int32 NavigatorPathType() const;

