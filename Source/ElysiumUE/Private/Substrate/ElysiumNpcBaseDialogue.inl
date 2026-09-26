// `CAI_BaseNPC`'s declarations of the `Dialogue` family (story 5 step 5),
// moved from `ElysiumNpcDialogue*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseDialogue.cpp`.

/** `m_pNavigator->GetPathType()` — retail's `path+0x30`, read through `0x102ee620` off `+0x5d34`.
 *  `8` is the pedestrian/crosswalk path type both crosswalk bodies gate on. **SEAM**: the shape map
 *  routes `+0x5d34` to `FElysiumScriptedCharacter::Motor`, "the one motor seam this chain stands
 *  beside the body", and that motor carries no path object at all. `-1` is neither `8` nor any
 *  other retail type; **nothing in this runtime writes it**, and it is a member rather than a
 *  literal so 0002's navigator half has one place to land it. */
int32 NavigatorPathTypeWord = -1;   // navigator's path +0x30
