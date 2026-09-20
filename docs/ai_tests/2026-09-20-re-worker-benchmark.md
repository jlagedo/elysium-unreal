# RE worker benchmark — 2026-09-20

Which headless model is worth firing at a VtMB reverse-engineering question. Two questions the oracle
listed as unrecovered were solved by hand first, then given — answer-free — to workers on three
harnesses. Every claim a worker made that was new to the answer key was checked against `vampire.dll`,
the corpus, the listing and the entity lumps before it counted. **One run per cell: a first signal,
not a law.**

- **Easy** — the value of `_DAT_1044ddb0`, the outer band of `MeleeAttack1Conditions 0x1026d9a0`.
  Answer: float `256.0`. Recorded in `../vtmb/npc-ai/conditions-and-states.md` § "The two melee attack
  bands".
- **Hard** — which shipped schedule issues task `0x14e` to `CNPC_VVampireBoss::StartTask 0x103c5ac0`.
  Answer: `../vtmb/npc-ai/programs.md` § "The boss transformation programs". Half of that section
  came from the workers, not from the hand solution.

Raw material — briefs, per-run event streams and answers, the answer key, `cost.py`, `rank.py`,
`audit.py`, the launcher — is under `$ELYSIUM_WORK_ROOT/bench/`.

## Ranking rule

Rank = API-equivalent dollars per correct rubric point, lowest first. **A hallucination disqualifies**:
an affirmative false statement about what the game's code or data does or contains. Omissions, honestly
scoped negatives ("not searched"), and a tool's own false negative reported faithfully do not.

Rubric, 13 points: A name, registrar, sibling ids · B the three schedules with a completeness proof ·
C the three task lists · D the three setters with guards and first writes · E the Sheriff's default
path · F the SabbatLeader's own arm, then the fall into the boss arm · G the other boss subclasses
forward · H the eight unrelated class-local `0x14e` · I the four map wires · J the lookalike wires ·
K the `NPCInit` monster models · L the `la_hub_1` script route in the export corpus (0.5) and its
dialogue caller (1.0) · M the `RunTask` proximity trigger.

## Hard task — ranked

| # | Run | Harness | Score /13 | API-equiv. cost | $ / point | Time |
|---|---|---|---|---|---|---|
| 1 | luna `max` | Codex | 10.0 | $0.17 | 0.017 | 14m26s |
| 2 | GLM flash `max` | opencode | 9.0 | $0.15 ¹ | 0.017 | 20m47s |
| 3 | Sonnet 5 `high` | Claude Code | 10.5 | $1.33 | 0.127 | 5m08s |
| 4 | GLM 5.3 `max` | opencode | 8.5 | $1.35 | 0.159 | 11m02s |
| 5 | Astra `high` | Codex | **12.0** | $2.94 | 0.245 | 5m50s |
| 6 | Sonnet 5 `xhigh` | Claude Code | 11.5 | $3.36 | 0.292 | 11m56s |
| 7 | Fable 5.1 `high` | Claude Code | 11.0 | $3.26 | 0.297 | 5m18s |
| 8 | terra `max` | Codex | 10.0 | $3.35 | 0.336 | 27m08s |
| 9 | Astra `max` | Codex | **12.0** | $4.77 | 0.397 | 11m41s |
| 10 | Fable 5.1 `xhigh` | Claude Code | 11.5 | $5.67 | 0.493 | 6m40s |

¹ flash `max` spawned one subagent whose tokens are not in the parent's event stream; understated.

### Disqualified

| Run | Would-be score | Cost | The false statement |
|---|---|---|---|
| GLM flash `high` | 10.0 | $0.09 | The SabbatLeader's `StartTask` has "no `case 0x14e`" — it has one, with three writes before the boss arm |
| luna `high` | 5.7 | $0.12 | "Only one schedule contains `0x14e`" (three do); the SabbatLeader "does not reach the boss arm" (it does) |
| terra `high` | 10.0 | $0.98 | Hengeyokai's `0x14e` arm "calls `TeleportIn`" — it sets `m_bfAINPCFlags2 \|= 0x80000800` |
| Opus 5 `high` | 9.5 | $2.51 | The Sheriff and Andrei "always take the fail branch" — both `NPCInit`s set `m_pMonsterModelName`. It queried the field ledger for `CNPC_VVampireBoss` only; the subclass writes are filed under the subclass names. **The most damaging error in the set.** |
| Opus 5 `xhigh` | 10.0 | $4.15 | `StartTransformation`'s writes cited at `+0x1b44`/`+0x1b48`; the listing has `+0x1b30`/`+0x1b34` |
| Haiku 4.5 `max` ² | 4.5 | $0.96 | Confused task id `0x14e` with schedule id `0x14e`: says the task is "registered at `0x102b9810`" (the Troika schedule registrar), lists `SCHED_TROIKA_FLYING_*` as its sibling tasks and as "the schedules containing `0x14e`" with invented task lists, and names `CAI_BaseNPCTroika::RunTask` as the setter. Its map list is right and names the three correct schedules — contradicting its own section 2. |

² Run for curiosity on the open harness (all tools; it spawned two subagents), 4m40s. The only run
on that harness, so not comparable cell-for-cell with the rest.

**A correction made while recording the findings.** GLM 5.3 `max` was first disqualified for saying
`la_hub_1`'s `MingXiao2` is "never on `0x159`", because `mingxiao2.dlg:171` →
`downtown.py` `ChangeMingXiaoToNines()` calls `TransformModel()` on it. That call exists only in the
Unofficial Patch copy of the script; in the base `Vampire/python/downtown/downtown.py:446` the two lines
are commented out. GLM's sentence is true of the base game, so it is reinstated — though it got there
without looking at a script. No worker opened the base copy; four (Sonnet `high`/`xhigh`, Fable
`xhigh`, Astra `max`) flagged the "changed by Wesp" provenance as unverified, which was the right call.

## Easy task

| Run | Result | Cost | Time |
|---|---|---|---|
| luna `high` | correct, clean | $0.012 | 1m51s |
| luna `max` | correct, clean — nothing gained over `high` | $0.028 | 4m26s |
| GLM 5.3 `max` | **DQ** — value right, two fabricated instruction addresses (`0x1026daa8`, `0x1026dabb`) | $0.096 | 2m14s |
| GLM flash `max` | **DQ** — value right, ladder inverted from a misread `JNZ`, plus an invented "the decompiler swaps x87 operands" theory offered as a correction to the oracle | $0.017 | 6m02s |

## What the table says

- **Cheapest clean answer: luna `max`.** 10/13 for $0.17, nothing false found. Slow (14 min), and it
  signs off "Unrecovered: none" with things left unsearched.
- **Best answers: Astra**, 12/13 at both efforts. `high` alone found the SabbatLeader's `RunTask`
  proximity trigger, which the hand solution also missed; `max` alone among the Codex runs closed the
  dialogue route. `high` is the better buy.
- **Best speed for a clean answer:** Sonnet 5 `high` (5 min, 10.5) and Fable 5.1 `high` (5 min, 11.0).
  Sonnet 5 `xhigh` was the only run to find both the `NPCInit` models and the dialogue line.
- **More effort is not monotonic.** luna `high` → `max` turned a wrong answer into a right one. flash
  `high` → `max` fixed one error and lost two findings. terra `high` → `max` tripled the cost for
  nothing. Opus `high` → `xhigh` traded a serious hallucination for a small one.
- **Price did not buy reliability.** Both Opus runs fell; the two cheapest clean runs out-scored
  terra `max`.
- **The bill is cached input.** Over 90% of every run's tokens are the conversation re-read on each
  of 50–110 tool calls, so the cached-read price and the call count decide the cost; the output price
  barely shows. luna's cached read is $0.02 / M against terra's $0.20, GLM 5.3's $0.26, Astra's $1.00.
- **Typical failure shapes.** A false completeness claim from a narrow search (luna `high`, Opus
  `high`); a fabricated citation beside a correct conclusion (GLM 5.3 easy, Opus `xhigh`, terra
  `high`); a confident theory built on one misread instruction (flash `max` easy). All three would put
  a false retail fact into the oracle if copied unverified — the citation rule in `CLAUDE.md` stays.

## Picking a worker

| Need | Fire |
|---|---|
| Bulk or routine walk, cost matters | luna `max` (never luna `high` on a multi-function walk) |
| A single constant or one function | luna `high` |
| A finding headed for `docs/vtmb/` | Astra `high` or Sonnet 5 `high`; Sonnet 5 `xhigh` when the chain must close |
| A cheap second opinion | GLM flash `high` — and check what it says |
| Not worth it on this evidence | terra (either effort), Opus 5, GLM 5.3 `max` at its price |

## Prices used (USD per 1M tokens, list, 2026-09-20)

| Model | Input | Cached read | Cache write | Output |
|---|---|---|---|---|
| `gpt-5.6-luna` | 0.20 | 0.02 | — | 1.20 |
| `gpt-5.6-terra` | 2.00 | 0.20 | — | 12.00 |
| `gpt-6-astra` | 10.00 | 1.00 | — | 50.00 |
| `glm-5.3` | 1.40 | 0.26 | — | 4.40 |
| `glm-5.3-flash` | 0.15 | 0.03 | — | 0.50 |
| `claude-sonnet-5` | 2.00 | 0.20 | 2.50 | 10.00 |
| `claude-opus-5` | 5.00 | 0.50 | 6.25 | 25.00 |
| `claude-fable-5-1` | 10.00 | 0.25 | 12.50 | 50.00 |

OpenAI and Z.AI from their pricing pages, cross-checked with models.dev; Claude from models.dev, with
each run's cost taken from the CLI's own `total_cost_usd`. Reasoning tokens bill as output. Every run
was on a flat plan (ChatGPT login, Z.AI Coding Plan, claude.ai login — the shell's `ANTHROPIC_API_KEY`
was unset for the Claude workers so nothing billed per token); the dollars are a proxy for quota.

## Harness

The 19 runs above (Haiku aside) used unequal harnesses. opencode ran the `re` agent's allowlisted shell (five denied
compound commands across three runs) but kept subagents, and flash `max` used one. Claude Code ran with
four tools — no web, no subagents, no edit tool — and inherited the owner's "Concise" output style.
Codex ran wide open with six MCP servers, of which only the corpus was ever called. Effect on accuracy:
probably small. Effect on cost: Codex is overstated by its extra tool definitions (~13k tokens per
request).

The standard from here is **open, not restricted** — each harness with its native surface, launched by
`$ELYSIUM_WORK_ROOT/bench/parity/fire.sh` with `brief-open.md`:

| | Codex | opencode (`.opencode/agent/re-bench.md`) | Claude Code |
|---|---|---|---|
| Shell | PowerShell, unrestricted | bash, unrestricted | Bash + PowerShell, unrestricted |
| Write / edit files | `apply_patch` + shell | `edit`, `write` + shell | `Edit`, `Write` + shell |
| Web | search + fetch | fetch only | `WebSearch`, `WebFetch`, Kagi |
| Subagents | `multi_agent` flag on (a probe reported none) | `task` | `Agent`, `Workflow` |
| MCP servers | vtmb-corpus, elysium (live game), node_repl, computer-use, OpenAI docs | vtmb-corpus | vtmb-corpus, elysium, context7, Claude Docs, Kagi |
| Guard rails | the brief only — do not modify the repo, the docs or the game install — and `git status` after each run |

Two things an open harness can reach that a careless worker could misuse: the `elysium` MCP drives the
live editor and game, and Codex's computer-use server drives the desktop. No run touched either, and no
run read the answer key or the benchmark folder (checked per run).

How to call each: the `codex-cli` skill (Codex), the `opencode` skill (GLM), and
`env -u ANTHROPIC_API_KEY claude -p --model <id> --effort <level> --output-format stream-json --verbose
--permission-mode bypassPermissions --no-session-persistence` (Claude Code).

## Caveats

One run per cell, so no variance estimate. Rubric points carry about ±0.5 of judgment. The answer key
grew during the benchmark — items K, L and M were worker findings — so an early run was scored against
things its peers, not its brief, surfaced. The install carries the Unofficial Patch and the export
corpus takes the patched scripts; the base `Vampire/python` tree was not checked against a pristine 1.2.
