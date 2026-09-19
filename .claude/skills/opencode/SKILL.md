---
name: opencode
description: Fire the opencode CLI headless as a second worker on GLM 5.3 (Z.AI Coding Plan — flat-rate, spends no Claude quota) with the vtmb-corpus MCP attached. Use when the user says "opencode", "GLM", "fire a worker", or asks to offload a bulk reverse-engineering sweep or get a second-model read of a decompile.
---

# opencode headless worker

`opencode run` sends one prompt, streams events to stdout, and exits when the session goes idle.
The worker starts with the project already loaded: the repo's `opencode.json` attaches the
`vtmb-corpus` MCP (its tools appear as `vtmb-corpus_vtmb_*`) and opencode reads `AGENTS.md`.

## Fire

The worker is the project agent `.opencode/agent/re.md`: it pins the model
(`zai-coding-plan/glm-5.3`) and `variant: max`, removes the edit tools, allowlists bash to
`python` / `grep` / `rg` / `ls` / `cat` / `head`, and carries the standing RE contract (cite
addresses, offsets from the datamap replay, exact bounds and branch order, an Unrecovered list).
A brief therefore holds only the targets and their specific questions. Model and effort are fixed —
do not substitute either; `--variant max` stays on the command line because the logs do not show
whether the agent's own `variant` is applied.

```bash
W="$(grep -E '^ELYSIUM_WORK_ROOT' .elysium.local.env | cut -d= -f2- | tr -d '"\r')"
O="$W/opencode/<slug>"; mkdir -p "$O"          # write the brief to $O/brief.md first
opencode run --agent re --variant max \
  --format json --dir E:/dev/elysium-unreal --title "<slug>" \
  "$(cat "$O/brief.md")" < /dev/null > "$O/events.jsonl" 2> "$O/stderr.log"
```

- **`< /dev/null` is mandatory.** `opencode run` reads piped stdin to EOF before starting; under the
  Bash tool stdin is an open pipe, so without it the run hangs forever with zero output.
- **The machine roots are pre-allowed** in the user config `~/.config/opencode/opencode.jsonc`:
  `permission.external_directory` for `ELYSIUM_WORK_ROOT` and `ELYSIUM_VTMB_ROOT`, `read` for
  `.elysium.local.env`, `edit` denied under the game install. The worker reads the exports, the
  datamap replay and `vampire.dll` with no `--auto`. If `.elysium.local.env` changes, mirror it
  there (machine paths stay out of the tracked `opencode.json`; write the file with the Write tool —
  a shell heredoc eats the JSONC `\\`).
- **An unallowed path kills the run.** A permission that resolves to `ask` is auto-rejected headless
  and the rejection ends the whole session: exit 0, no `text` event, `permission requested: …
  auto-rejecting` in `stderr.log`. Resume with `-s <sessionID> --auto`; the context is kept.
- Run it with `run_in_background: true`. A `max` RE walk of two classes took 10–16 minutes and
  60–75 corpus calls; a background run is not cut at the Bash tool's 600 s. The harness re-invokes
  you on exit; do not poll.
- Use a real Windows path for `$O`. Git Bash `/tmp` is not the `/tmp` Python sees.
- The brief must be self-contained: headless mode denies the `question` and plan tools, so the
  worker cannot ask anything back.

## Read the result

`events.jsonl` is NDJSON, one `{type, timestamp, sessionID, part}` per line. Types seen:
`step_start`, `tool_use` (`part.tool`, `part.state.status`), `text` (`part.text` — the answer),
`step_finish` (`part.tokens`; `part.cost` is 0 on the flat plan).

```bash
python -c "import json,sys; [print(e['part']['text']) for e in (json.loads(l) for l in open(sys.argv[1],encoding='utf-8') if l.strip()) if e['type']=='text']" "$O/events.jsonl"
```

Exit 0 with no `text` event means it failed quietly: read `stderr.log`, and re-fire with
`--print-logs --log-level INFO` to see provider, MCP and permission lines.

Follow up in the same session with `-s <sessionID>` (every event carries it); add `--fork` to branch
instead of extending.

## Permissions

| Flag | Effect |
|---|---|
| `--agent re` | The default here. No edit tools; bash outside the allowlist is refused with an error the worker sees (a `deny` does not end the run — only an `ask` does). The answer comes back in the `text` events: several progress notes, then the deliverable as the last and longest one. |
| `--agent plan` | Not used: it carries opencode's planning-mode prompt and leaves bash fully open. |
| `--agent build` (opencode's default) | Edits and bash inside the repo are **allowed with no prompt**. One run per tree — give it a worktree via `--dir` if it writes. |
| `--auto` | Approves everything that would ask (paths outside the repo, `*.env` reads, doom-loop). Explicit denies still hold. |

The worker's output is a lead, not evidence: CLAUDE.md's citation rule applies before any of it
reaches `docs/vtmb/`.

## If a flag drifts

Verified on opencode 1.18.31. Check `opencode run --help`,
`opencode models zai-coding-plan --verbose` (the `variants` key lists valid `--variant` values) and
`opencode mcp list` (must show `vtmb-corpus connected`).
