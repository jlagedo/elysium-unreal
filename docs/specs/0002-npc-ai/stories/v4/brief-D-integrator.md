# Brief — V4d integrator: D1, D2, D3 (one commit; never push)

Final, 2026-10-04. Run only after **V4c integration and all three D lane reports**. Read
AGENTS.md first, HANDOVER.md, brief-D-ragdoll.md, packets-S14.md, packets-spike.md,
S1 §§1/3, S4 §§e/f.1, S13 §1 and Arena/README.md. Read V4c's final diff/commit and actual
verdicts: the concurrent snapshot is not the baseline. Preserve V4a/V4b/V11/V4o and V4c's
corpse clocks/fade/solid 4. Owner ruling settled: Chaos fall, .phy bodies/masses/limits,
retail state/event ordering. No calibration or permission question to re-ask.

No file named `report*.md`. Worker responses/scratch outside repo carry run reports;
recovery belongs in named docs; verdict table goes in the single commit message.

## Files and ownership

The exact D1/D2/D3 manifests are in brief-D-ragdoll.md. **Every pairwise intersection is
empty**: D1 builder/header/Build.cs/phy recovery; D2 only import_characters.py; D3 runtime
headers/bodies, transaction/release tests and death recovery. Re-expand paths and verify before
build 1. Coders never write Arena or one another's paths; apply their owed lines serially now.

Additional integration paths:

- `Arena/scenarios/combat/damage_lethal_death.json`, `verbs_stealth_kill.json`,
  `corpse_removed_unseen.json`, `corpse_kept_seen.json`, `corpse_kindred_burns.json`,
  `corpse_pedestrian_stays.json`, `corpse_fades.json` — measurements/retail assertions only.
- `docs/specs/0002-npc-ai/spec.md`, `docs/specs/TRACKER.md`,
  `docs/specs/0002-npc-ai/stories/v1/divergences.md`, `stories/v1/triage.md` — V4d close
  and remaining placement only; expand the last two beneath 0002-npc-ai before staging.
- Lane-owned recovery docs for the measured frame and serial review corrections. Existing
  S14 is read-only evidence; do not overwrite its snapshot findings with run outcomes.
- Only the scoped imports' generated asset packages/receipts required by repository tracking
  policy. Inspect explicit paths; no full-corpus rollout, map bake or unrelated package churn.

A necessary extra source/test/probe file is declared by exact path/function/source before
editing and before build 1. No broad refactor or generated slot edits. The existing floor
probe is sufficient for rest; asset binding/simulation needs separate positive observation,
not a fabricated passing probe. No sound-event vocabulary is owed: D3's audio recording arm
asserts the burning emission.

## The job

1. **Integrate owed lines before building.** Read all three reports (each under 350 words).
   D1/D2 must agree the builder's Python signature, distinct _RAGDOLL path, receipt fields
   and frame recipe version. D3 must include the capability and real Spine2 fallback:
   source rig + missing PhysicsAsset is a bake fault; source rig + valid fallback reaches
   StartBodyRagdoll on the kill tick. Capability-only or asset-only fixes are incomplete.
   The selector executes the same zero-force/bone−1 transaction; ordinary death never enters
   DIE and keeps current pose despite the required weighted draw. Check restore does not
   replay OnDeath/RNG/clocks or bypass the source predicate.

   Release must run on actual Kill, including already hidden entities, and destructor before
   motor removal; not Event_Killed or model-replacement DestroyMotor. Use explicit Visual,
   because simulation can detach it. Verify stop/release/children/claims/destroy/null order
   and retired-world guard. World teardown's weak list must tolerate already-destroyed meshes.
   Pedestrians keep their body. Preserve V4c think names, alpha, maker assignment and solid 4.
   Recheck the burn sound after C2: add D3's exact one emission only if absent; channel 0,
   volume 1, attenuation 0.8, pitch 100/native 1. Correct only stale recovery prose; current combat
   creation arms are already corrected, physics-interaction's burn-static sentence is not.
2. **Read every diff before build 1.** Inspect implementation AND tests for C4458/C4459
   shadowed locals, missing includes, incomplete types, declarations/signatures, double
   definitions and generated/hand-body collisions. Check PhysicsAsset, ConstraintInstance,
   skeletal component/provenance/physics data includes and editor-only dependency guards.
   HANDOVER requires an uncommitted Codex review: load its codex-cli skill/method before
   that review, requesting these same checks. Apply review fixes before compiling. No coder
   source edits are assumed finished until their reports and diffs agree.
3. **One `uv run elysium build --arm`; maximum two builds total.** Arm must be included so
   later arm testing does not cause hidden compilation. Build 2 is reserved for a necessary
   measured frame/integration correction, after complete diff review again. A third build
   means stop and report placement. Wait on completion; no polling. Reserve the full arena
   until after the final binary and asset corrections.
4. **First scoped bake and controlled measurement; wider bake waits.** No default capsule
   assets as acceptance. D1 must return 15 real regular_cop bodies/14 joints with exact hull
   and mass receipts. Before scheduling import, time the read-only inventory/preflight
   query path used by stage_characters (which inventories models before --bodies filtering).
   A selector existing does not prove that lookup is within budget. At >10 s log/optimize;
   at 60 s stop/optimize before the bake; never retry/widen. This verifier did not time that
   entire importer inventory. Bakes themselves are not query commands.

   ```text
   uv run elysium import characters --bodies character/npc/common/cop_variant/regular_cop/regular_cop
   ```

   Allow **2–5 minutes**, planning allowance including editor startup, not a measured SLA.
   No --force: current mesh/animation recipes stay reusable. Inspect the independent ragdoll
   recipe and actual mesh attachment even on reuse. First measurement is the brief's one
   controlled editor/lab check: hulls/pivot against right thigh/calf bind mesh, signed knee
   sweep to −95/+4 Source z stops, mirrored knee/elbow check. Capture matrices/endpoint signs,
   authored masses/counts, screenshot and recipe in phy_vphysics.md. No production 34-body
   import until it settles solid→bone and constraint frames. If correction is needed, send
   the measured transforms to D1/D2, integrate serially, review/build 2, re-run the same scoped
   command with updated ragdoll recipe; allow another **2–5 minutes**. No global version bump.
   If the diagnostic still cannot identify a consistent frame, stop the wider bake and
   report the exact failure; do not claim V4d complete.

   After the frame gate, import S14's complete 34-key union. Exact PowerShell command lines:

   ```powershell
   $v4dBodies = @(Get-Content -LiteralPath docs/specs/0002-npc-ai/stories/v4/packets-S14.md | ForEach-Object { if ($_ -match '^\| (character/[^ ]+) \| (hub|tutorial|both) \|$') { $Matches[1] } })
   if ($v4dBodies.Count -ne 34 -or @($v4dBodies | Sort-Object -Unique).Count -ne 34) { throw 'V4d body scope must be 34 unique canonical keys' }
   $v4dBodyArgs = @(); foreach ($v4dBody in $v4dBodies) { $v4dBodyArgs += @('--bodies', $v4dBody) }
   uv run elysium import characters @v4dBodyArgs
   ```

   The small packet lookup/count is a query: time it with 60 s timeout before import. It names
   both witness maps' dependencies and all record bodies; include closure may add bank units.
   Allow **4–8 minutes**, estimated. regular_cop, Sabbat_Henchman, female_citizen_2,
   Shovelhead and garment-bearing bum_male are already in the union. Read import receipts:
   source-no-physics skipped; missing authored joins/limits/hulls failed; no animation/mesh
   re-import attributed to the new recipe; asset attached and reusable. Repeat scoped import
   only for a changed frame recipe/failure, not as a timing study. No full-corpus or map bake.
5. **Measure floor bounds and finish records before their acceptance run.** Launch
   `uv run elysium gr --arena`, then `elysium.gr_scenario damage_lethal_death` for the real
   regular_cop fall. Check D3's success/terminal-release diagnostics for the actual asset path,
   body count and active simulation. Observe attached _RAGDOLL and simulation separately; the floor
   probe alone could pass a held lying pose. Sample the **drawn mesh's Bip01 Pelvis** height
   over WorldStatic floor and speed through settling, not component origin, capsule, on_ground
   or sleep. Rest is **under ~5 cm/s**. Set the body's height bound from real .phy rest samples,
   with measured cm, recipe, small stated margin and deadline in about/notes.24 cm is historical
   provisional data, not a universal limit. Do not widen it to conceal wrong joints/hulls.

   Both primary death records use regular_cop, so share its measured asset-specific bound.
   Other floor probes, if added to the four corpse records before removal, get their own body
   measurement; never probe a removed corpse at end. Verify bum_male garment/hair visually,
   one witness-map floor kill (all seven current records stage in the arena), and one save/load
   presentation smoke: no crash/standing resurrected ragdoll, no RNG/output/clock replay.
   Save semantics belong V6; no exact simulated-pose persistence claim.

   | record | Preserve staging and acceptance |
   |---|---|
   | damage_lethal_death | direct TutorialThug/regular_cop, idle, scalar kill t=3; death/OnDeath/corpse once on kill tick; Dead/alive=false, real fall and floor/rest by end; remove V4d known_red only when proved |
   | verbs_stealth_kill | direct regular_cop arena_mark, spawnflags 4, knife/crouch behind; grapple kill and floor/rest at end; no SEE_PLAYER/combat reentry; **not fade child**, no +13.8 timing repair |
   | corpse_removed_unseen | regular_cop, player away; removed at death+10 poll, not before+9.5 and by +10.5; no draw/physics body or child left, no ensure/crash |
   | corpse_kindred_burns | placed tutorial sabbat_redshirt_1/Sabbat_Henchman, bit 9 clear, watched; unconditional+10 removal, same lower/upper windows; real simulating asset released cleanly; audio arm proves one burning emission |
   | corpse_pedestrian_stays | hub pedestrian_female/female_citizen_2, player away, bit 9 clear; no automatic removal through death +25, visual remains/rests |
   | corpse_fades | tutorial stealth_victim_maker/Shovelhead child; flags 0x204, watched; no removal before death +13, gone by +14.5; later fade beats burn +10; simulating mesh released cleanly |
   | corpse_kept_seen | ordinary regular_cop watched control; retain/rearm through death +12, visual remains |

   Preserve V4c's relative timing repairs. Add missing no move/task/schedule-after-dies assertions
   in the two primary records using existing kind/after vocabulary; no new death program.
   Trace check is stronger than the current within 0.5/1.0 expectation windows: OnDeath and
   corpse are on the kill tick, exactly once, entity origin/use/feed/loot anchor remain at the
   death spot, no NPC move/task/schedule after death. Do not confuse visual pelvis motion
   with a gameplay move event. Logs and rendered scene must show clean removal even while
   IsSimulatingPhysics remains true at rest; entity existsfalse alone cannot prove cleanup.
6. **Validation after the last build/assets, in order.** Named records, default tier, arm
   tier, then **one full arena run**:

   ```text
   uv run elysium arena damage_lethal_death verbs_stealth_kill corpse_removed_unseen corpse_kindred_burns corpse_pedestrian_stays corpse_fades corpse_kept_seen
   uv run elysium test
   uv run elysium test arm
   uv run elysium arena
   ```

   Verify D3's provenance-backed NpcCombat.Death, release and burn-sound arms and retain C2
   NpcKernelAnim.RagdollSeed/FadeClock/pedestrian tests. No extra family boot after complete
   default/arm tiers. If build 2 is needed, repeat required final subset/tiers on that binary
   before the sole full run. Record actual totals/times/baseline verdict movement; never
   import HANDOVER totals as current. All other verdicts unchanged or triaged by retail
   address; keep existing unrelated failures/intermittents attributed to their owners.
7. **Recovery/close.** phy_vphysics holds accepted measured transform/signs; lifecycle/combat/
   physics docs agree on capability/fallback/burn tail and terminal visual lifetime. Record
   Chaos as named visual modernization, no state change. Tick V4d/spec/tracker only after
   real fall, four clocks, seen control, tiers and full arena acceptance. No owner question
   for solver tuning.0014 retains impulse/hitbox producer, prop_ragdoll, friction/surface
   simulation, full-corpus rollout and BurnModel look; V6 retains corpse saves. A pipeline
   inventory timeout blocks dependent scheduling until optimized, not an excuse to run full
   corpus or change unrelated recipes.
8. **Commit once by explicit path; never push.** Review git diff --check/status and package
   churn. Stage the actual approved lane/integration/record/recovery/generated paths with
   `git add -- <path> ...`; spell every path, including individual permitted asset packages.
   Never git add . / -A, directory-wide staging or unrelated owner/V4c files. One commit
   describes final behavior and includes this table for all seven records:

   ```text
   record | before V4d (actual V4c baseline) | after | remaining owner
   ```

   Include default/arm/full-arena totals, build count/times, scoped-bake count/times/keys,
   diagnostic frame verdict and per-body floor measurements. No report*.md path, second
   commit or push. Final response≤ 300 words reports results and precise remaining placement.

Query rules throughout: 10 s warns/logs, every query 60 s hard timeout, optimize before retry,
no whole read over ~200 KB for one item. Lookup address/name/slot before docs searches.
Wait by command completion/notification, never polling or sleep loops. Shadowing is an error.
