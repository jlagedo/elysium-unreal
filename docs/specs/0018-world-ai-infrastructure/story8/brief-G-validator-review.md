# Brief G — review fixes on the two cover validators (0018/8, wave 2, review loop)

Read `CLAUDE.md` first. You edit ONLY:
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelBaseHelpers.cpp`, `ElysiumNpcKernelBaseHelpers.inl`
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelBaseHelpersTests.cpp`
Do NOT build or run. Other agents are editing other files concurrently; touch nothing else.
Report ≤300 words.

Context: wave 1's brief B ported `0x10296c40` as `FElysiumNpc::ValidateHintCoverRange`
(`Substrate/ElysiumNpcHints.cpp`) arm for arm from the listing, and `0x102968f0` as
`HintLosCheck` (in your cpp, ~:729, real now). B's review found, in your files:

1. **`FUN_10296c40` / `AttackHintRejectReason`** (`ElysiumNpcKernelBaseHelpers.cpp:~431-540`,
   `.inl:~149-160`) is an older duplicate of `0x10296c40` with three defects: it uses the hint's own
   angle instead of `0x102d12e0` (a node-bound hint's yaw is the NETWORK NODE's yaw), does not
   convert to Source axes (Y negated), zeroes Z before normalising, and drops the weapon-range term.
   Grep shows no caller outside this file and its tests. DELETE both (declaration, definition,
   `EHintRejectReason` if nothing else uses it, and their tests), or, if a test pins a reason
   string that `ValidateHintCoverRange` cannot report, make `FUN_10296c40` a one-line forward to
   `ValidateHintCoverRange` and say so. Prefer deletion.
2. **`CoverHintStillValid` / the `0x10295ed0` wrapper** (`~:250-335`) and **the verbose twin
   `0x102961a0`** (`~:336-430`) have the same facing-frame problem: the yaw comes from
   `Hint.Angles.Y` where retail's `0x102d12e0` answers the node's yaw for a node-bound hint
   (`m_nNodeID != -1`) and the hint's own yaw otherwise, and the 2-D basis `0x101d2f40` is in
   Source axes. Read how B did it in `ElysiumNpcHints.cpp` `ValidateHintCoverRange` (grep
   `0x102d12e0` there and in `ElysiumNpcBaseStartTask.inl:~241-250`, which declares the port of
   `0x102d12e0` / `0x102d11f0`) and use the SAME helper and the same axis conversion in both
   validators. Confirm every other arm of `0x10295ed0` against the listing (`vtmb_code
   0x10295ed0`) and the oracle (`docs/vtmb/npc-ai/schedule-kernel.md` § "The three hint
   validators"): null/disabled fails; cover object resolves; the 2-D band `[min, max]` widened by 64
   on both ends when the hint is `m_pHintNode`; normalise by `1/(dist+eps)`; projection STRICTLY
   greater than `TargetAngleRangeDot`; the current hint accepts there; another hint must be within
   512 of me and, for type `0x283d` only, forward projection over 0.5; then `0x102968f0`
   (`HintLosCheck`) — which must now be CALLED at the tail (it was a seam answering true; check the
   wrapper wires it with the cover object as the target).
   B also noted: the normalise (`0x10137220`) is 3-D with an epsilon and then dotted in X and Y
   only — port that literally; and an unordered (NaN) compare counts as pass where the listing's
   flag test says so (`TEST AH,5 / JP`-style) — keep the compare direction the listing has.
3. Update the tests to the corrected frame (a node-bound hint whose node yaw differs from the
   hint's authored angle must be judged by the node yaw) and delete the tests of the deleted
   bodies. Message strings cite addresses as the file does.
