# Reproducible Ghidra research specifications

These tracked JSON files are the durable index for multi-session VtMB binary
research. The Ghidra project and generated dumps are derived from the user's game
and remain under gitignored `$ELYSIUM_WORK_ROOT/research/ghidra/`; a specification preserves enough
context to reconstruct an investigation after that project is lost or re-imported.

Each specification records:

- the Ghidra program and pinned source-binary size, hashes, and image base;
- seed functions with working aliases, confidence, roles, and dump limits;
- confirmed findings and deliberately retained eliminated leads;
- direct and interface relationships between functions;
- field-displacement probes and unresolved questions.

The wrapper validates the specification, optionally verifies the source DLL, runs
one address per headless invocation, waits for Ghidra's project lock to clear, and
builds a local `INDEX.md` beside the generated decompile, assembly, xref, and field
dumps.

```powershell
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json --dry-run
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json `
  --binary "E:\path\to\Vampire\cl_dlls\client.dll"
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json `
  --address 10091110 --kinds funcs,asm,xrefs
uv run elysium research animation-pose research/cases/animation-pose/specs/animation_pose.json --index-only
```

The three animation specifications divide the call chain at binary boundaries:

| Specification | Program | Scope |
|---|---|---|
| `animation_pose.json` | `client.dll` | local decode, blends, transitions, hierarchy, entity/root composition, render submission |
| `animation_skinning.json` | `engine.dll` | `VEngineModel006`, `SetupBones` callback, `TStudioRender012` bridge |
| `animation_studiorender.json` | `StudioRender.dll` | inverse bind, skin palette, vertex deformation, mesh submission |

Working aliases and hypotheses remain in the specification. Once assembly and
decompile establish a VtMB format or behavior fact, write it into the owning
`docs/` topic in the same change; the generated context pack is evidence, not
project documentation.
