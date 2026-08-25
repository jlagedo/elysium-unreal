# Frida discovery probes

Frida is an opt-in, development-only discovery backend for the retail capture
toolchain. Its hooks do not intentionally alter arguments, returns, or game
state, but dynamic instrumentation is invasive and may affect process memory or
timing. It does not replace the hash-pinned native capture or supply runtime
acceptance evidence on its own. A useful discovery graduates into the retained
native instrument before it closes a behaviour question.

Install the locked optional dependency once in the active checkout:

```powershell
uv sync --extra research-frida
```

The synthetic smoke never opens the retail install. It proves the IA-32 agent,
module observer, three read-only exported-function hooks, caller/register/stack
records, and clean process teardown:

```powershell
uv run elysium research frida_probe smoke
```

The supervised variant additionally proves that the retained launcher keeps the
target suspended until the Frida collector signals readiness:

```powershell
uv run elysium research frida_probe smoke --supervised
```

Attach-mode discovery is deliberately bounded:

```powershell
uv run elysium research frida_probe attach `
  --pid 1234 --recipe smoke --duration-seconds 30
```

An operator-driven scene can detach before that backstop through a fresh stop
file. The controller refuses a pre-existing file so stale state cannot produce
an empty successful capture:

```powershell
uv run elysium research frida_probe attach `
  --pid 1234 --recipe life7_theatre_oracle --duration-seconds 600 `
  --stop-file "$env:ELYSIUM_WORK_ROOT/research/frida/stop-life7"
```

Create the named file only after the authored terminal boundary. The agent
flushes and detaches normally and records `stop_reason=stop-file` in the
manifest.

A profiled retail recipe runs through the existing supervised launcher:

```powershell
uv run elysium research frida_probe launch `
  --recipe cap2_8_callers `
  --startup-profile unofficial-patch-save
```

Retail hook recipes name semantic targets from
`research/tooling/capture/contracts/binary_profiles.json`. Before launch, the
controller verifies the exact owner-installed module files against those
profiles. In-process activation also requires the approved module path, image
size, and declared prologue bytes; a mismatch emits a failure and installs no
hook. Synthetic smoke hooks use fixture-only exports and never relax the retail
gate.

Every session writes `manifest.json`, line-buffered `events.jsonl`, and any
launcher finalization below `$ELYSIUM_WORK_ROOT/research/frida/`. Absolute
addresses are retained with module-relative RVAs. Generated evidence and game
bytes never enter Git.
