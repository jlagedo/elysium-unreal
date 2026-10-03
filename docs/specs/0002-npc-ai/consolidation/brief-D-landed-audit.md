# Brief D — every landed story, audited for what it really proved

Scope: `docs/specs/TRACKER.md` rows 01–13 (ticked) and 14 (open), every `- [x]` story in
`docs/specs/0002-npc-ai/spec.md`, `docs/specs/0018-world-ai-infrastructure/spec.md`,
`docs/specs/0019-npc-kernel-rework/spec.md`; `docs/specs/RE-BACKLOG.md`; the hand-off files
(`docs/specs/0019-npc-kernel-rework/handoff-story-8.md`, `docs/specs/0018-world-ai-infrastructure/story8/*.md`).
Read the specs' story blocks (the `Landed`, `Gap`, `Handed on`, `Named modernizations`,
`Named divergences`, `What stays partial` paragraphs), not the whole files at once: grep for
`- [x]`, `Landed`, `Handed on`, `Gap:`, `partial`, `unobservable`, `modernization`, `divergence`.

Deliver `findings-D-landed.tsv` (rows: story | tracker row | landed date | what it claims | tests
named in the landing (count) | live check recorded (yes / unobservable / none) | named
modernizations that change state or event order | hand-offs to rows not yet landed | defects found
since (RE-BACKLOG row / findings file) | risk H/M/L) and `findings-D-landed.md` (≤2 pages):

1. The stories whose live check was recorded as unobservable, deferred, or not done, and why.
2. Every "named modernization" across the three specs, with your judgement: visual-only (allowed)
   or state/event-order changing (not a modernization by `CLAUDE.md`'s rule; must be re-classed).
   The known case: the body arbiter (`EElysiumBodyOwner`) kept under the sequence bridge in 0019/8
   wave 2 ("a named modernization of playback only") gates the kernel's own activity commits.
3. Hand-off chains: items handed from one story to another that no open row now owns (walk each
   `Handed on` paragraph to its target row; report the orphans).
4. The three specs' overlaps: which stories in 0002 and 0018 describe the same retail body
   (cite both story ids and the address), and which 0019 "landed" items 0002 rows still list as gaps.
5. What one consolidated 0002 sequence should look like: dependency order (clock → kernel → senses
   → navigator → programs → selectors → species), which open rows merge, which are done and only
   need their tick, which are dead.

Report ≤300 words.
