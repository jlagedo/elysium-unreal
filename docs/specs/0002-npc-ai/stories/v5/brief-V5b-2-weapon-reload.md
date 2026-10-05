# Brief V5b-2 — weapon reload, per-set count and Presence text

Final against V4c `d0f79574`, after V4o `64895278`. Read AGENTS.md, README,
`packets-V5b-check.md`, and research section `0x102552c0` in combat-and-damage.md.
Retail: TASK_RELOAD `0x102890f3..0x102891b9`, slots 280 `0x10253ab0`,
322 `0x10255050`, 323 `0x102552c0`, FireBullets `0x10268900 / 0x10268919`.
Use the assembly for slot 322's tail jumps and deadline comparison.

## Files — only these six

- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseRunTask.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseRunTask.inl`
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelRunTaskTests.cpp`

README's A∩B=A∩C=B∩C=∅ proves disjointness. These files include V4o/V4c changes.
Already carried out: event-only NPC shot guard in CommitArrivesFromAnimEvent, shared attack
stream/fixtures and live owner deadlines. Preserve them; do not reopen the player estimate path.

## Numbered jobs

1. **FElysiumWeapon declarations**, WeaponClasses.h (lane 1 calls the first):

   ```cpp
   bool CanReloadMagazine(int32 MagazineIndex) const; // slot 280, 0x10253ab0
   void FinishReload();                              // slot 322, 0x10255050
   void FinishReloadBulk();                          // slot 323, 0x102552c0
   bool bInReload = false;                           // +0x898 m_bInReload
   ```

   The current FElysiumItem has MagazineCount/AmmoType and mode MagazineSize/bReloadSingle.
   Magazine1 is a named no-clip input for slot 277 (+0x454), ammo type absent; name retail
   m_iMagazineCurAmts[1] +0x750 at the stand-in. Add representable jammed/interrupt-reload flags
   in this owned header if absent (m_bIsJammed +0x89a / m_bInterruptReload +0x899), so slot 323 can clear its stored outputs.

2. **CanReloadMagazine**, WeaponClasses.cpp, slot 280 `0x10253ab0`, four arms in order:
   absent ammo type (`0x10253b40` false; empty AmmoType here) →true;
   uses clip and clip>0 →true;
   owner with Inventory.Reserve(AmmoType)>0 (`GetAmmoCount 0x103346c0`) →true;
   otherwise false. Name magazine1's representation rather than reading index0 for it.

3. **FinishReloadBulk**, WeaponClasses.cpp, slot 323 `0x102552c0`:
   no owner →no writes, including flags.
   Bulk: for each magazine0..1 which uses clip, Add=min(Size-clip, reserve), clip+=Add.
   Do not silently clamp negative Size-clip: preserve the listing's two-arm min.
   NPC reserve is untouched. Debit ONLY player owners under DAT_1088aef4
   (IsCommand OR int<1); if the tunable is absent, name that input and the existing debit
   stand-in, rather than inventing a cvar.
   Single: send weapon activity `0x10254cd0(this,0xc3)`; use an existing call if present,
   otherwise name the missing activity-send hook as a no-output seam.
   Both arms then clear bInReload, m_bIsJammed, m_bInterruptReload.
   Preserve the player's existing BeginReload / CommitQueuedReload route.

4. **FinishReload**, WeaponClasses.cpp, slot 322 `0x10255050`:
   bulk owner combat pointer +0x9c present AND bInReload AND the owner's
   **LIVE m_flNextAttack +0x1564 <=curtime** (`0x102551e2..0x10255219`) →
   FinishReloadBulk, then both weapon NextPrimaryAttackTime / NextSecondaryAttackTime
   (+0x730/+0x734, `0x1025521f..0x1025523a`) =curtime.
   Read FElysiumNpc::NextAttackTime for NPC owners; no constant-zero stand-in.
   The current writers include PlayerDefenderBlockReaction (Damage3.cpp,
   `0x1029fd6a / 0x1029fd6f`), Damage.cpp::PlayerAttackerBlockedReaction (`0x1029fdb0`), and V4c's
   WeaponClasses.cpp::BeginMeleeSwing (`0x103ea2eb`). A future deadline forbids finishing.
   Slot 322's single-round arm requires PLAYER +0xa8 (`0x1025506f..0x10255077`);
   NPC returns without writes, retaining bInReload=true. Player continuation is a named
   later player-weapon input, with tail targets `0x102550b4 / 0x102550d2 / 0x102551cb..0x102551dc`.
   Do not route existing player reloads through an incomplete single-round body.
   Integrator owes the stale no-writer declaration comment in ConditionsBodies.inl.
   Bulk clearing occurs only when admitted; retained NPC single-round state is owed to V6.

5. **FElysiumNpcBase::WeaponFinishReload**, BaseRunTask.cpp / declaration BaseRunTask.inl:
   keep WeaponFinishReloadCalls as a witness, resolve the active entity as FElysiumWeapon,
   set bInReload=true (`0x1028918d`), then call FinishReload (`0x1028919d`).
   A nonweapon entity retains only the diagnostic tally.
   Keep surrounding RunTask's AutoMovement, turn/slot251 gate, condition clears 0x40/0x41,
   and TaskComplete in their existing order (`0x102890f3..0x102891b9`).

6. **FakeReloadCount per SET**, WeaponClasses.cpp::CommitQueuedAttack:
   `0x10268919 DEC [NPC+0x65f0]` is FireBullets' first NPC statement; Shot `0x102387b0`
   calls FireBullets once per set.
   Put --FakeReloadCount at the top of **the existing trace loop**, before TraceShotImpact.
   That loop precedes the missing/dead-victim refusal and the separate damage loop.
   Decrement for NPC attackers even on miss, dead target or team damage refusal.
   No decrement for a player, zero-set shot, refused/stale commit, or repeated queue delivery.
   The event route is ShotFromAnimEvent → StageRangedShot → queued CommitQueuedAttack;
   overlay HandleAnimEvent reaches the same route. V4c's CommitArrivesFromAnimEvent refuses
   NPC timer exceptions, including missing model/timeline. Preserve it.
   Do not call Entity::FireBullets (EntitySlotBodies.cpp): that would trace twice.
   Do not decrement inside RangedImpact/damage loop, which can refuse or stop early.
   The existing EntitySlotBodies decrement remains for its separate direct callers.

7. **PresenceDoublesAttackRate**, WeaponClasses.cpp and declaration in .h,
   `0x1033d940 / 0x101e3f50`: name Presence id10, m_iDisciplineFlags2 +0xeb4, writer
   AddDiscFlag `0x1033cfb0` from status apply `0x101e3560` (activation callers
   `0x101e2f50 / 0x101e3380 / 0x101e33c0 / 0x101f8620`).
   Keep false until spec0006 supplies the cast/status seam; replace “nothing writes” text.

8. **Arm tests**, owned test files, actual prefix **Elysium.Arm.Weapons.**:
   - `.CanReloadMagazine` pins all four arms (`0x10253ab0`).
   - `.ReloadFinish` pins bulk writes, untouched NPC reserve, both stamps, flags,
     owner absent, bInReload false, owner future deadline, exact equality admitted;
     single-round NPC retains bInReload (`0x10255050 / 0x102552c0`).
   - `.FakeReloadCountPerSet` pins one/multiple/zero sets, player, miss, dead and teammate
     targets, serial replay, and both normal/overlay event entry (`0x10268919`).
   - `Elysium.Arm.NpcKernelRunTask19.ReloadFinish` pins set-before-call and the surrounding
     clears/completion/no-weapon behavior (`0x1028918d / 0x1028919d`).
   Rewrite the seam-only assertions in the existing Base.ReloadAndSetActivity test;
   report exact test names. Keep V4c fixture and shared-stream assertions.

## Dependencies and owed lines

Lane1 owns gate/template, lane3 Motor/cache. Do not edit their files, ItemClasses/ItemTable,
EntitySlotBodies, or Arena. A needed field in an unowned file is an exact owed declaration/body
line for the integrator. The live NextAttackTime declaration-comment repair belongs to integrator.
Record changes and retained-single-round persistence assessment are integrator/V6 work.

## Rules

Write only this lane's six files, in the worktree the coordinator names, **by absolute path**.
Read files and run read-only tools from **E:\dev\elysium-unreal**.
Never build, run the editor/game/tests/arena, or commit. Retail first, with the retail address
at every changed runtime line. Report a new divergence; do not adopt it.
Never hand-edit generated `*Slots.cpp`: a hand body goes in the matching `*SlotBodies.cpp`;
the integrator owns the kernel_verdicts.tsv row and regeneration.
Shadowed locals/members/globals are compile errors (C4458 / C4459).
End with a report **under 350 words**: addresses ported, tests changed, seams retained and the
exact lines owed by each file outside the lane, or explicitly none.
