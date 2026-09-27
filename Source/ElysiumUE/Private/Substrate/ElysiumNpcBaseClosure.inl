// `CAI_BaseNPC`'s declarations of the `Closure` family (story 5 step 5),
// moved from `ElysiumNpcClosure*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseClosure.cpp`.

/** The clock of the gather pass that is dispatching slot 561 (`ElysiumNpcEnemy::GatherConditions`
 *  sets it around the virtual call and clears it after), so the base body reads the pass's `Now`
 *  rather than the world's. -1 outside a pass. A port mechanism, not a retail word: retail's body
 *  reads `gpGlobals->curtime`, which the pass's `Now` is. */
double GatherPassNow = -1.0;

