# Brief — wave 1 integrator

Runs after the three coders report. Read `README.md`, the three briefs and the coders' reports
(handed to you in the prompt).

## Job

1. `git status` / `git diff --stat`: every changed file belongs to exactly one lane's list; a file
   outside every list is reported, not kept silently.
2. Fix only integration breaks: an import or call across two lanes' files, a marker or fixture one
   lane registers and another uses, a merge of two lanes' expectations. Never redo a lane's work;
   report what a lane left undone.
3. Run, each once, every command under its 60 s budget unless it is a test run:
   - the default `uv run pytest` — budget ≤20 s wall, zero failures (report the slowest 10);
   - `uv run pytest -m corpus` once, to show the opt-in tier still passes;
   - every `kernel_*` `--check` and `uv run elysium research kernel --check`: green, outputs
     byte-identical to `HEAD`'s (diff the generated files), timed cold / warm / unchanged;
   - the corpus probe set (T2's table), timed, reply sizes;
   - one `uv run elysium test` with three prefixes in one boot against the existing binaries
     (e.g. `Elysium.Substrate.Schedule Elysium.Substrate.NpcKernelSelect19 Elysium.Content.Hints`):
     under 25 s wall, a summary per prefix;
   - `uv run elysium build --help` / `test --help` show the wait and `--no-wait` behaviour.
4. Compare every number against the spec's step-1 targets; a miss is reported with its measurement,
   not hidden.
5. When green: one commit, `feat(tooling): step 1 wave 1 -- <summary>`, body listing per lane what
   landed and the before/after numbers. Do not commit a red wave.

Report ≤300 words: green / red per target with numbers, what you fixed, what is left for which lane.
