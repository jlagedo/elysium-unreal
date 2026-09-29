# Lane C3 · classify the surviving mechanism hand bodies: forward (`seam:`) or algorithm (`hand:`)

Read `README.md` first. You edit **no C++**. You read the ~122 `mechanism` rows in
`docs/vtmb/npc-kernel/seam-list.md` whose "Port target" is `hand:<Class>::<Method>` or a bare
`<Class>::<Method>` with a hand-written definition, look at each body, and deliver
`targets-C3.tsv` (`address<TAB>new_target<TAB>one-line reason`).

## The rule

- **`seam:<Class>::<Method>`** — the body is a one-line forward into a service (a motor call,
  `SurroundingBounds`, an actor transform read, `TraceRetail`, `QueryRoute`, `NavRaycast`,
  `SampleFloor`, `SetMoveIgnore`, `HasPath`, the save archive, a `FMath` call, a container idiom,
  a retail-constant OR onto a base call), possibly wrapped in an argument conversion, a null
  guard on the handle, or a retail "answer X when the seam has nothing" default. No loop, no
  branch on retail state beyond that guard, no threshold compared. Closed.
- **`hand:<Class>::<Method>`** — the body still runs retail's algorithm (a loop, a ladder of
  thresholds, several branches on NPC state, its own trace math). Open; one line saying which
  part is the algorithm and which service it should forward to.
- A body that is retail RULE logic wrongly verdicted mechanism (it decides behaviour: what the
  NPC does, not how the engine does it) → `RE-VERDICT rule` with the observable that makes it
  one (a schedule text, a keyfield, a player-visible timing, a caller that is a rule body).

Read only the body (the function between its signature and the closing brace) and its
comment; `rg -n "<Method>\(" Source/ElysiumUE/Private/Substrate/<file>` finds it. Do not read
whole files. Do not open the corpus. Report ≤200 words: counts per class, the `RE-VERDICT` rows
with their reasons, the algorithms left open grouped by the service they want.
