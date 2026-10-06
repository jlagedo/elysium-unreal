# Decisions for the owner

Collected while the plan was built (2026-10-06, overnight). Nothing here is applied; the plan
works around each one with the default stated.

## D1. Start a story when what it calls below is done (amend R1)

R1 as committed: a layer is tested before the next layer starts. The plan marks every story and
brief **ready** when every lower-layer function it calls is already done (`audit.tsv`), and
`schedule.md` compares both orders. Proposed wording: "a story starts when every lower-layer
function it calls is done; the layer order holds per function". **Default until decided:** strict R1;
the readiness marks are informational.

## D2. More than one lane needs a second checkout

One checkout builds one binary; two workers editing it at once break each other's builds. Lanes in
`schedule.md` beyond one need a second checkout (a full clone, never a `git worktree` — `AGENTS.md`'s
gotcha) with its own binaries, or worktrees that only write and are compiled in turn in the main
checkout (0002's method step 3). **Default:** one lane.

## D3. The "engine-replaced" verdicts

556 core functions (137 KB) were judged *engine-replaced*: Unreal does the job and the port does not
reproduce the Source mechanism. Most are safe by `AGENTS.md`'s modernization rule — debug output,
rendering, networking and edicts, client prediction, memory and string pools, precache, file I/O.
About 150 touch state or event order — physics and collision, traces, sound, bone and pose queries —
and the rule admits them only "with the retail contract and event sequencing kept". **Proposed:** accept
the safe categories as named modernizations; each layer gets a contract-check story over its
state-bearing engine-replaced rows (inputs, outputs and event order compared with retail).
**Default:** those rows count as done in the numbers, and the contract checks are listed in each layer.

## D4. The ledger tooling story (R3)

R3 takes `research kernel --check` off the per-change path. The tooling half — the ledger stops
writing port `file:line` citations into `functions.md` / `coverage.md` — is a small pipeline story.
**Default:** first story of L0's tooling lane, before any L0 code lands.

## D5. Retail-default stubs

The re-check found generated slot "stubs" whose retail body is empty or returns zero, so the port's
default already matches (28 in L0/L1 alone); only the stub's diagnostic tally remains. **Proposed:**
the slot generator marks them `default:` like the other retail-default bodies. **Default:** listed as done.
