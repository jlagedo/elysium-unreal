// `CAI_BaseNPC`'s declarations of the `SpeciesLifecycle10` family (story 5 step 5),
// moved from `ElysiumNpcSpeciesLifecycle10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseSpeciesLifecycle10.cpp`.

/** How many times a base body forwarded the touch to `m_pParent`. This runtime's `MoveParent` IS
 *  retail's `m_pParent` (`CBaseEntity`, the move-parent handle), and the forward is a real dispatch
 *  when the parent is an NPC; when it is any other leaf the dispatch is counted and nothing is
 *  called, because no other leaf in this runtime carries slots 174/175. */
int32 ParentTouchPropagations = 0;

/** How many times `CBaseEntity::Touch` called `m_pfnTouch`. **SEAM:** this runtime has no
 *  per-entity touch think-function pointer (`+0x1ac` in retail's `CBaseEntity`), so the call is
 *  counted and nothing runs. Named because it is the FIRST thing `Touch` does, ahead of the parent
 *  forward, and dropping it silently would lose that order. */
int32 TouchFunctionCalls = 0;
