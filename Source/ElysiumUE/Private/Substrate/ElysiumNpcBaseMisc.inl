// `CAI_BaseNPC`'s declarations of the `Misc` family (story 5 step 5),
// moved from `ElysiumNpcMisc*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMisc.cpp`.

/** `0x1028a190` — the shared Troika gate slot 590 opens with, and the same gate the knockback start
 *  (`0x102a01b0`, `lifecycle.md`) tests. The decompilation is DAMAGED (a jump table it could not
 *  recover); this is walked off the listing:
 *
 *      if (GetState() == 4 (NPC_STATE_SCRIPT) && m_hCine (+0x5d74) resolves)
 *          if (!cine->CanInterrupt()) return false;            // 0x101a8930, on the CINE
 *      if (m_bInChoreoScene (+0x5bc4)) return false;
 *      if (m_bfAINPCFlags2 (+0x14bc) & 0x1000) return false;   // TEST AH,0x10 at 0x1028a213
 *      return IsAlive();                                       // vtable +0x278, slot 158
 *
 *  Retail name unrecovered; named for what the body answers. */
bool OkToDisturb() const;
