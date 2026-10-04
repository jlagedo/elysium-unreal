# Brief A0-review — Fable's review of V3a's seam commit (read only)

Read `README.md` here, `brief-A0-seam.md`, `packets.md`, and the seam commit's diff (`git show
<hash>`, given to you). You write nothing in `Source/` and do not build or run anything.

## Check

1. **No behaviour change.** Nothing writes `ScriptState` or `DialogPartner`; `SetDialogPartner` is
   empty; no arbiter, executor, cine or dialogue body changed. Any line that changes state or event
   order is a finding.
2. **Retail words.** Each new member carries its offset, its retail name and its writers/readers by
   address, and those addresses agree with `research where` and `docs/vtmb/`. The shape-map move of
   `0x5d70` is to the NPC word, not to a new port name.
3. **The records state retail.** Every expectation in `script_dialog_hold`, `dialog_use_hold`,
   `script_aischedule_walk` traces to an address or a schedule text in the `about` (packet R2's
   settled schedule for each input; `0x6a` as a schedule and `0xb9` as the task; the executor's
   `0x46` install). Flag any expectation that encodes the port's behaviour, any `within` that a
   retail timer does not justify, and any missing `never` that would let a double `OnDialogEnd`
   pass.
4. **H12 observes.** `dialog_choose` goes through the player's own door; it cannot close a session
   by a path the player cannot reach; an unmet target is `error`, proven by its self-test.
5. **The design.** Read README §4–§7 against the code you now see: a lane whose files are not
   disjoint from another lane in the same wave, a deletion that a later sub-story's lane depends on
   before it lands, a record that cannot turn green in the sub-story that claims it. Say which.

## Report (≤300 words)

Findings as a numbered list, each with file:line and the retail address it violates, marked
**blocking** (the wave must not start until fixed) or **advisory**. If nothing blocks, say so in one
line.
