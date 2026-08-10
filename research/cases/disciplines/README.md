# VtMB disciplines research case

This offline case recovers VtMB's vampire-power system from player selection and cast intent
through eligibility, blood payment, targeted or sustained effect execution, cooldown, interruption
and teardown. It does not run or modify the game and it does not implement the Unreal discipline
system. Generated data surveys, decompilation and copied Ghidra projects remain below
`ELYSIUM_WORK_ROOT`.

## Pinned inputs

| Input | Size | SHA-256 |
|---|---:|---|
| `Vampire/dlls/vampire.dll` | 7,860,281 | `c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f` |
| `Vampire/cl_dlls/client.dll` | 3,428,425 | `e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870` |

The authored survey reads the local patch-first `vdata/system/stats.txt`,
`traiteffects000.txt` and `disciplinetgt_000.txt`...`004.txt`. Their current hashes are recorded
only as provenance because their bytes remain game-derived:

| Input | Size | SHA-256 |
|---|---:|---|
| `disciplinetgt_000.txt` | 61,347 | `2E78FAE969C298F5103F0F62364858AF5A2962543618DFC9C589255EB191F73A` |
| `disciplinetgt_001.txt` | 65,597 | `C0D0D2BD3F9C8A5E8BC5D10E26B308CB0DB5B336C910E3518CC1D2B7039242C1` |
| `disciplinetgt_002.txt` | 57,510 | `664D94D395B15EB7BCAFC1EBE5293C4A8012FA3D944948772FDC603D57CF376A` |
| `disciplinetgt_003.txt` | 43,035 | `94A069E20809461D780F796782AAE929758FE140A6836CA9846AE3035D545D48` |
| `disciplinetgt_004.txt` | 66,100 | `7C0B1814D92394B9C387BA8D5E0AEA92F51933EAFBC4450C451446FF73D20936` |
| `stats.txt` | 101,894 | `7DF42D6E1D1FF3831953C742BC2BFE4E5B86CF5FA5097041F95DBAFD96D91B01` |
| `traiteffects000.txt` | 66,863 | `9874A33ECB53E59EB572512A830458636686BC1125BBE490DAA6F8A8395BAC4A` |

The two executable specifications carry automatic hash gates. The authored table hashes identify
the exact local corpus surveyed for the catalog.

## Questions

- How do `vhotkey`, `vdiscipline`, `vdiscipline_int`, `vdiscipline_last` and
  `vdiscipline_endall` select, cast, renew and end powers?
- Which predicates enforce learned level, blood cost, cooldown, target eligibility, zone legality,
  overt/Masquerade policy and incompatible active states?
- How does the shared `DisciplineTgt` interpreter resolve AoE filters, strata mappings, chance,
  inheritance, schedules, trait effects, damage, particles, sounds, projectiles and nested casts?
- Which disciplines are targeted one-shots and which are native sustained/self states?
- What do the thirteen saved discipline slots contribute at each available level?
- How are `Active_Disciplines`, per-discipline timers, cast counters, Obfuscate rules and Protean
  transform state saved and restored?
- Which damage, movement, perception, stealth, dialogue and AI systems consume each active power?
- What ends a sustained power: explicit input, timeout, blood exhaustion, damage, combat sound,
  bump, weapon/action state, zone transition, map teardown or scripted clear?

Established facts belong in `docs/vtmb/disciplines.md`; project status belongs in RE41 and roadmap
13.2. Game-derived `disciplinetgt_*`, `stats.txt` and `traiteffects000.txt` remain in the local
export corpus and are never copied into Git.

## Procedure

1. Parse the patch-first exported discipline, stat, trait-effect and clan tables and record their
   hashes/provenance without committing their contents.
2. Run both hash-pinned specifications against private copies of the analyzed Ghidra projects.
3. Join client selection/targeting commands to the authoritative server cast path.
4. Follow the server target-data loader, cast scheduler, AoE mapping and `HitInfo` executor.
5. Recover native sustained-power consumers independently; do not infer them from help text.
6. Correlate exact animation/event rows through the existing gameplay-action and animation-event
   surveys.
7. Use retail captures only for remaining timing, targeting and presentation validation.

```powershell
uv run elysium research disciplines research/cases/disciplines/specs/disciplines_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_disciplines_server_<task>" --kinds funcs,asm,xrefs,fields,grep
uv run elysium research disciplines research/cases/disciplines/specs/disciplines_client.json --binary "<VtMB>/Vampire/cl_dlls/client.dll" --project-dir "<work>/research/ghidra/project_disciplines_client_<task>" --kinds funcs,asm,xrefs,fields,grep
```

Append `--address <address>` and narrow `--kinds` for focused reruns. Use `--dry-run` to validate a
specification without executing Ghidra.

## Established joins

- The client maps a visible learned-power ordinal to one of thirteen compiled Discipline slots;
  it does not send a display-list ordinal to the server.
- `vdiscipline_int` and `vdiscipline_last` converge on one server authority with separately
  remembered Discipline and tier state.
- `Is_Instant` selects the native active-stat/event family; non-instant powers enter the generic
  `DisciplineTgt` target and hit interpreter.
- Accepted targeted casts pay adjusted blood once, then apply the ordered mapping to each legal
  target. Projectiles defer target execution to impact; nested casts are internal effect nodes.
- Native renewables own shared-queue expiry events. `vdiscipline_endall` and
  `ClearActiveDisciplines` remove both native events and tracked targeted effects.

The remaining native consumers, Presence pulse, Masquerade witness policy, exact client tier/frame
handoff, recovery ordering and controlled retail cast matrix remain open in the consuming document.
