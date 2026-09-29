# Reviewer brief (used after each wave's gate; the wave is named in your prompt)

Read `README.md` first. You review the working tree's diff against `main` (`git diff --stat`,
then `git diff -- <file>` per file; read hunks, not whole files). You edit nothing. You may run
`rg` and read-only python. No build, no tests.

## What to check, in this order

1. **Deleted where it should be forwarded, or kept where it should be deleted.** For every
   deleted function body in the diff, find the ledger row (`docs/vtmb/npc-kernel/delete-list.md`
   or `seam-list.md`, or `research/tooling/ghidra/driver/kernel_verdicts.tsv`): the verdict must
   be `dead` (body deleted) or `mechanism` (body forwarded to a seam or to nothing). A deleted
   body whose row is `rule` or `present` is a finding. A deleted line inside a surviving `rule`
   body must be a print (`DevMsg`, `Msg`, `Warning`, `UE_LOG` standing for one, an overlay
   draw, a stamp write); anything else deleted from a rule body is a finding, with the retail
   address the comment on that line carried.
2. **Behaviour drift in survivors.** Where a statement was removed from a branch, the branch
   structure and the else-chain must be unchanged. Where `return Helper(X)` became `return X`,
   the helper must have been a pass-through. Where a helper moved files, the body must be
   byte-identical (`git diff` of old vs new text).
3. **Live observers.** For each deleted function, `rg` its name across `Source/` (excluding
   the deletion itself and generated files). Any surviving reference is a finding. For each
   deleted word (a struct field), `rg` its name; any surviving read or write is a finding.
4. **The standing rule.** Nothing in the diff may add a body, a seam or a rule that another
   story or spec plans (jumps 0018/7, flying 0018/12, hints 0018/8, goals 0018/9, makers 0018/16,
   gesture 0015, facial 0010, player-on-head 0002/28). Additions are allowed only as one-line
   forwards into existing seams or as nothing-seams with a retail-name comment.
5. **Tests.** A deleted test must have exercised only a deleted body. A rewritten test must
   assert the same retail fact through a surviving surface.
6. **Ledger consistency.** `targets-*.tsv` addresses appear as `-` (or a service word) in the
   overlay and their bodies are gone; no address is closed whose body survives.

## Deliver

`review-<wave>.md` (≤300 words): findings first, each as `file:line — what — why it matters —
the fix`, ordered by severity; then "checked and clean" as a one-line list of the areas above.
No praise, no summary of the diff. If nothing is wrong say so in one line.
