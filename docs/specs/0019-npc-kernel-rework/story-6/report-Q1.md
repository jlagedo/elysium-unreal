# Lane Q1 report · inline cells, files A

(Saved by the orchestrator from the lane's final answer. Nothing built or run.)

**Counts.** 223 `DAT_10…` mentions over 113 addresses in 53 files. Moved: 43 cell rows (literals now read `ElysiumNpcTunables::<Name>`; comment-only value cells get a row too). ConVar: 1 row, `10924af8` `debug_allow_non_idle_auto_sk`, default "1". Ref: 40 rows. Already held: 29 addresses reused. Every cell and convar row passes `gen_kernel_tunables.verify` against the pinned image. Left: the 198 remaining spellings are held addresses, new rows, or refs; the four ConVar object+4 pointers (`109245e4`, `10924a1c`, `10924a6c`, `10924d6c`) are covered by held object rows. Most mentions: `AsianVampire.cpp` 16, `BaseFacing.cpp` 13, `AndreiBlood.cpp` 12, `Anim10.cpp` 12, `BaseSenses.cpp` 11.

**Retail values recovered where the port had a stand-in:** `1044ddb0` was +inf "unrecovered"; the image holds 256.0f (`conditions-and-states.md`, "The four cells") — now `Melee1OuterBand`, so `COND_TOO_FAR_FOR_MELEE` is reachable; `BaseHelpersTests` pins it. `1049954c` is 0x3e4ccccc (0.19999999), not 0.2f; the head-filter weight reads it. The unused `GAnim10EyeOffsetFallbackScale = 0.5` stand-in removed (the live reader in `BaseHelpers.cpp` reads the 0.75 cell). Float width kept for the head-filter `Remaining -= 0.1` step and the `FacingIdeal` tolerance (retail reads double cells) to keep the equal-bit cases the tests pin.

**Open:** `debug_allow_non_idle_auto_sk` defaults to 1, but `docs/vtmb/stealth.md:405` says the shipping arm is IDLE/ALERT — the polarity of the test at `0x102c2300` needs rechecking; still not ported. 18 of the 40 refs sit past `.data` raw size (`.bss` runtime globals, plus the five `sk_npc_*` hitgroup ConVar pointer cells, whose objects and defaults are unrecovered).

Includes of `ElysiumNpcKernelTunables.h` added to `ElysiumNpcBach.h` and `ElysiumNpcAsianVampire.h`. Names like `Ten`, `Two`, `TwoHundred` may collide with other lanes' rows at the same address (orchestrator dedupes).
