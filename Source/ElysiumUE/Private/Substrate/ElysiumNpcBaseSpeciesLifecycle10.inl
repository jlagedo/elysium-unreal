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

/** `CBaseEntity::StartTouch` (`0x100a49d0`), slot 174's Troika-line body. The whole body is the
 *  parent forward: when `m_pParent` resolves live, dispatch ITS slot 174 (`vtable +0x2b8`) with the
 *  same toucher. Nothing else — no output, no condition, no state. */
void BaseEntityStartTouch(FElysiumEntity* Other);

/** `CBaseEntity::Touch` (`0x100a4af0`), slot 175's Troika-line body. Two steps in order:
 *  `if (m_pfnTouch) m_pfnTouch(other)`, then the same parent forward through the parent's slot 175
 *  (`vtable + 700`). */
void BaseEntityTouch(FElysiumEntity* Other);

/** `CAI_BaseNPC::FUN_101a67e0` (`0x101a67e0`), vtable `+0x29c` = slot **167** — `GetEnemy()`. The
 *  whole body resolves `m_hEnemy` through the global entity table and answers null when the handle
 *  is stale. Declared here because this family is the first to need it by name and because the
 *  checklist's two walks call it a door reference; it is `BaseMemory.Enemy` resolved. */
FElysiumEntity* GetEnemyEntity() const;
