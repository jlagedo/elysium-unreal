# Decision log (append-only)

The project's one decision log, referenced from `roadmap.md` (the work tracker). Record a
decision when it is made, dated, newest first — including "pending" decisions with their
trigger. A behavioural divergence from retail lands here carrying both the faithful and the
chosen behaviour (`remaster-direction.md`'s governing rule). Entries are never rewritten —
append a correction as a new entry.

- **2026-07-25** — **The input path: four owner calls settling roadmap 10.6.** Design:
  `docs/input-architecture.md`. Enhanced Input becomes the driver while **the VtMB console command
  string stays the action's identity** — one `UInputAction` per bindable command, executed through
  `FElysiumConsole` on `Started`/`Completed`, so an action bound to a patch *alias* (`vm_feed` →
  `checkFeed()`) is indistinguishable from one bound to a compiled verb, as in VtMB. Four calls:
  1. **First-party PlayStation support via `GameInputWindows` (new work, Feel/Presentation axis).**
     The engine's beta GameInput plugin replaces XInput+RawInput; Xbox pads need no configuration,
     DS4/DualSense arrive as `GameInputFamilyHid` and get hand-authored `FGameInputDeviceConfiguration`
     entries (VID `054C`; PIDs `05C4`/`09CC`/`0CE6`/`0DF2`) mapping onto standard `Gamepad_*` keys plus
     `bOverrideHardwareDeviceIdString` for glyph swapping. **`WinDualShock` is ruled out**: its
     `Build.cs` reflects on `LibScePad` and compiles to `DUALSHOCK4_SUPPORT=0` without the licensed
     Sony platform extension. Consequences accepted: `GameInputRedist.msi` ships with the game
     (Win10 19H1 floor, joins 10.5), and adaptive triggers/haptics are deferred (the seam is the
     plugin's `GameInputHapticAudioDevice`). A third-party FAB plugin was rejected — the project
     vendors only MIT code. **No original to reproduce**: `controls.md` records that VtMB ships raw
     joystick cvars, no UI, no default binds, and a `joystick.cfg` that does not exist.
  2. **Binding state lives in `UEnhancedInputUserSettings`; `cfg/config.cfg` is a one-way
     projection.** The key profile is authoritative (slots, conflict query, SaveGame persistence
     keyed by `MappingName`, all engine-supported). `FElysiumConfigWriter` emits Valve-format text
     into `out/cfg/config.cfg` on every `ApplySettings`, mirroring `Host_WriteConfiguration`, so
     `vamputil.py`'s `FixKeyBindings` and any bind-reading level script resolve a faithful view
     through the 9.3b `nt.getcwd` redirect. An existing `config.cfg` with no profile beside it is
     imported once on first run. **Faithful behaviour:** the text file *is* the settings model and
     the engine rewrites it on exit. **Ours:** the profile is the model and the text is a view —
     rejected dual ownership over fidelity to a storage format, since nothing in VtMB's logic layer
     depends on the file being writable by the game.
  3. **Defaults are the Unofficial Patch 11.5 set** (64 actions), not retail's 39 — the richer,
     better-labelled, already-grouped inventory that is the players' mental model: arrows strafe,
     `,`/`.` turn, ten `vhotkey` slots, numpad camera verbs, `+lookup`/`+lookdown`, `autospeed`/
     `automove`, `skip`. Two carve-outs, both divergences from *both* shipped default sets:
     **`vphysicshand` is dropped** (dead bind — the item exists, the console verb appears in no
     binary, and the patch rebinds `p` to `skip` regardless), and **`kb_def.lst`'s disagreements
     with `default.cfg`** (the `[`/`]` swap, retail's double-bound `,` landing on `pause`, the
     patch's double-bound `p`) resolve to the `default.cfg` reading — we have one defaults source
     where VtMB has two that were never generated from each other, so "Use Defaults" reproduces a
     fresh config rather than VtMB's divergent one.
  4. **The dev layer occupies no bare key a player can bind, and a test enforces it.** Console
     returns to `` ` `` — VtMB's own `toggleconsole` key, and already one console via the 9.3b
     bridge — which frees F10 for `snapshot`; Cog's shell shortcuts move to `Ctrl+F1`–`Ctrl+F4`
     (`FCogInputChord` derives from `FInputChord`, so this is configuration); every other dev key
     uses `UEnhancedInputComponent::BindDebugKey`, which takes an `FInputChord` directly and never
     enters a mapping context. Reserving `` ` `` and `ESCAPE` costs the player nothing: neither
     `toggleconsole` nor `cancelselect` appears in `kb_act.lst` in retail *or* the patch, so VtMB
     itself treats them as non-rebindable. `FElysiumReservedKeys` is consumed by the rebinding
     widget's key filter, a **Substrate-tier test** asserting no default mapping in any generated
     IMC lands on a reserved key, and a dev-build startup collision check — so a future action with
     `DefaultPrimary=F1` fails `test.bat` rather than silently shadowing the debug menu. This trades
     `debug-tooling.md`'s bare-F1 ergonomic for the player's defaults staying undistorted by a dev
     tool. `elysium.input.ReserveDebugKeys 0` A/Bs it in dev builds.
  **Deferred, not decided:** CommonUI/CommonInput adoption. It owns gamepad UI focus/back-routing
  and the glyph swap that consumes call 1's hardware-device id, and it shapes the UI foundation
  rather than bolting onto it — so the call belongs to **8.6**, not 10.6.

- **2026-07-24** — **9.3b/B5 as-built: the console bridge, and three calls it forced.** The `ccmd`/`cvar`
  console surface (`FElysiumConsole` + the two `vampire` objects) is a faithful port — `ccmd`
  attribute-set executes, `user.cfg`'s `alias patchtype "setPlus()"` drives the Basic/Plus switch, the
  round trip is Python→alias→Python. Three sub-decisions worth recording:
  1. **File-root redirect (reproduce, via an interpreter-local shim).** VtMB's file-touching scripts
     resolve paths as `nt.getcwd() + "\\" + sys.moddir + "\\<tree>\\…"`. Rather than emulate a `Vampire/`
     install dir or `chdir` the UE process (which UE relative-path logic, logs, and crash dumps ride on),
     the VM sets `sys.moddir = "."` and monkeypatches **its own** `nt.getcwd` to the absolute `out/` root.
     VtMB's file layer then reads/writes into our content mirror — `out/` *is* the mod dir — contained to
     the interpreter. `setPlus`'s `FixKeyBindings` finds `out/cfg/config.cfg` and no-ops cleanly, so
     `setPlus` reaches its Tutorial branch. **Not covered:** the handful of call sites that use `moddir`
     *alone* without `getcwd()` (the haven-PC `open("./vdata/hackterminals/haven_pc.txt")`); those stay
     process-cwd-relative and IOError → error-to-false. Inconsequential on `sp_tutorial_1` (post-`Enable`),
     tracked as a gap for maps that depend on the haven personalization / hunter asset-swap file I/O.
  2. **`Character` is a compatibility stub (divergence, reproduce-with-caveat).** VtMB's `vampire` binds a
     mutable old-style **`Character`** class (player+NPC), and the patch monkeypatches it
     (`vamputil.py:3270` `Character.Near = _Near`). Our 24 Character methods dispatch off the
     `Entity`/`Player` C getattro, not a shared class, and a C extension type rejects attribute assignment —
     so `Character` is bound as a mutable old-style Python stub that absorbs the import + monkeypatch. The
     **faithful** behaviour is that `Near` becomes a real method on characters; **ours** is that the patch
     lands on the stub and does not reach live C entity instances. The only corpus use is the unused
     `AnimalRadar` path, so nothing observable regresses, and the import completing is what unblocks the
     entire real `vamputil.py`. A future unification of NPCs under a mutable Character type would close this.
  3. **Console→Python fallthrough noise policy.** VtMB resolves a console word against its ConVar/ConCommand
     registry, else falls through to Python. We have no engine registry, so the rule is: cfg alias → expand;
     known cfg cvar → set; else try Python. A `NameError`/`SyntaxError` means "not Python" → an engine
     cvar/command we do not model (`rope_shake`, `+speed`, `cl_detailfade`) → dropped with a **Verbose** log,
     not a traceback; a Python body that *runs* and raises PyErr_Prints error-to-false like every other eval.
  *Verified:* fresh New Game runs `unhidePlus() → c.patchtype="" → setPlus() → trig_popup_move.Enable()`
  unassisted (MCP I/O history); `Elysium.Substrate.Console` unit test green. As-built: `roadmap-archive.md`
  9.3b / B5.

- **2026-07-24** — **RE correction: `.dlg` column 12 is the Malkavian-PC line, not a "short menu label"; shown
  only to a Malkavian player.** A side-by-side (Tremere PC) showed our choice text as the Malkavian variants
  ("I shall undertake your dark tutelage") where retail shows the normal lines ("Okay. I could use the help.").
  Root cause: our field map called col-12 an abbreviated menu label and `MenuLabel()` *always preferred* it, so
  every PC saw Malkavian text. **RE (`tools/out/dlg`):** 9,576 rows carry both col-1 and col-12; col-12 differs
  from col-1 in **97%**, and the differences are unmistakable Malkavian-speak ("Behave, I am your kind of
  monster" vs "Calm down, I'm not one of them"; "Are we fleeing or complaining?" vs "Alright, let's just get
  out of here"). So col-12 is the **Malkavian variant of the row's text** (NPC subtitle or PC choice), shown
  *instead of* col-1/2 when the PC is Malkavian — VtMB's "Madness (unique dialog)" clan feature. This is
  distinct from the separate-row Malkavian mechanism (e.g. `jack_tutorial` id 12 "Who are you?" gated
  `not IsClan` vs id 13 "The rain of ages" gated `IsClan`, which *branch* differently); col-12 is the inline
  variant used when the Malkavian line goes to the same place. **Decision (reproduce):** `FElysiumDlgLine`
  renames the field to `TextMalkavian`; `RawFor(bMale,bMalk)`/`DisplayText(bMale,bMalk)` pick col-12 iff the
  player is Malkavian (else the gendered col-1/2); the conversation carries `bMalk` (from the sheet clan ==
  Malkavian), and the box uses it for both subtitle and choices. Raw `Text()` still returns col-1/2 verbatim
  for tests/RE. Corrects `game_runtime.md` §5 (col-12 row + the branching step). Covered by
  `Elysium.Substrate.Dlg{Parse,Display}` and a real-data `Elysium.Content.DlgJackTutorial` assertion (id 23).
- **2026-07-24** — **`pc`/`npc` bound in scripting + `IsClan` made real + a Tremere dev-boot mock; player-sheet
  consumption surveyed across the test bench.** The bare `play.bat <map>` boot skipped chargen, so `pc` was
  unbound and every clan/gender/stat-gated dialogue choice error-to-falsed (the "Who are you?" vs `[Continue]`
  symptom). Fixes: (1) both script hosts now bind **`pc`** (the sheet-backed player Character) and **`npc`**
  (the firing entity / conversation partner) — CPython binds `pc` once at VM start (`__main__.pc = FindPlayer()`)
  and `npc` per-eval from `Ctx.Self`; the ElysiumExpr host resolves both names alongside `self`/`activator`.
  (2) **`IsClan`/`IsPCMalk` are now real** (in the shared `ElysiumScriptNatives` table): they read the player
  sheet clan (2..8 encoding) instead of the old return-0 / false stub — `IsClan` was previously not even in
  the table. (3) The dev boot seeds a **mock character via `BeginNewGame(Tremere, male)`** (interim stand-in
  for chargen, 9.4) so the sheet the gates read exists; per owner call this is always-on for `play.bat <map>`
  (the unseeded A/B path is dropped). `CalcFeat` stays a stub (9.4), so skill-gated lines remain hidden;
  clan/gender/`base_*` gating now works. Covered by the green Substrate tier (CPythonWriters, Expr).

  **Player-sheet consumption survey (`tools/dlg_sheet_survey.py`) — 10 exported test-bench maps, 25 NPC
  `.dlg` (each byte-identical to the UP install), tutorial-first.** Drives the 9.4 sheet/Character-API build
  order.
  - **A. Reads that gate dialogue (build first):** **clan** (`IsClan`, 258 refs, #1, tutorial) — full set across
    the bench is Malkavian/Nosferatu/Ventrue/Toreador/Tremere/Brujah/Gangrel; **skill-checks** Persuasion/
    Intimidate/Seduction (tutorial) + explicit `CalcFeat("inspection"/"haggle")` — all gated by `CalcFeat` (9.4);
    **gender** `pc.IsMale()` (34, sm_hub — *real*, reads the sheet); **attribute/ability reads** `pc.intelligence`/
    `wits`/`computers`/`charisma`/`manipulation`/`humanity`/`bloodpool`; **disciplines** — all eleven `base_<disc>`
    via one predicate, `jack_tutorial` id 1081's starting-condition sum.
  - **B. State mutations the dialogues drive (the Character-API surface — findings):** **Quests** `SetQuest`(66),
    `GetQuestState`(10) — *real* (quest map). **Inventory** `HasItem`(72), `RemoveItem`(26), `GiveItem`(10),
    `AmmoCount`, `GiveAmmo`, `HasWeaponEquipped` — *stubbed* (no inventory system). **Money** `CurrentMoney`(17),
    `MoneyRemove`(15), `MoneyAdd`(13) — *stubbed* (no sheet money). **Vitae/frenzy** `Bloodloss`(12),
    `SeductiveFeed`(10), `FrenzyTrigger`(3), `bloodpool` — *stubbed*. **Humanity/XP** `HumanityAdd`,
    `AwardExperience`, `BumpStat` — *stubbed*. **Faction** `IsFollowerOf`(5) — *stubbed*; `getattr` (reflection).
    Backing status is the `ElysiumScriptNatives` table: quests + `IsMale`/clan/`base_*` are live; everything else
    in B is a logged stub, and that stub list is the concrete 9.4 work-list (inventory, money, blood/frenzy,
    humanity/XP as the four backing systems).

- **2026-07-24** — **Dialogue `[stage directions]` are stripped on screen, kept in the data.** Comparing
  the live game (Unofficial Patch) against ours on `jack_tutorial` line 11 surfaced two of them rendered
  raw: `[laughing at something no one else thinks is funny]` and `[chuckle]`. **RE:** the corpus carries
  2,303 such spans; every one is free-form English VO-recording direction — emotion/delivery (`[sarcastic]`,
  `[Blows smoke]`), pacing (`[pause]`), or a speaker attribution on a multi-VO line (`[Cop Buddy2:]`,
  `[Therese]`) — with hand-typo variants of the same note (`[cough cough]`/`[cough, cough]`/`[Cough,
  cough]`), i.e. authored for a human reader, not an engine token set. **Not consumed by lip-sync or the
  expression engine** — VtMB drives both from **separate per-line files resolved by path**, not from the
  subtitle string: lip-sync from `sound/character/dlg/<hub>/<stem>/line<ID>_col_e.lip` (Source phoneme data,
  `VERSION 1.2`), expression/gesture from the sibling `.vcd` (Faceposer choreography channels) — see
  `game_runtime.md` §4/§5, *"one subtitle source, the `.dlg` text field."* Verified concretely on the exact
  flagged line: `jack_tutorial/line11_col_e.lip`'s `PLAINTEXT` block reads `"What a scene man! … whattaya
  say?"` with the brackets **already absent** (and even normalized differently from the subtitle — "plop you
  out"/"whattaya say" vs "plop ya out"/"Whaddya say?"), and its `WORDS` phoneme table is built from that
  cleaned text, so `[laughing…]`/`[chuckle]` never enter the lip/vcd path. The display layer strips them
  from the subtitle (confirmed by the retail/UP screenshot). `game_runtime.md` already named col-1's
  `[stage directions]` as "part of the string" (a parse fact); this adds the **display** fact. **Data-mismatch ruled out first (per the owner's prompt):** our
  `out/dlg/main characters/jack_tutorial.dlg` is **byte-identical** (same md5) to the install's
  `Unofficial_Patch/dlg/…` copy — the extractor is patch-first, so we already carry the UP text; the UP
  file itself still contains the brackets, so this is a display transform, not a version gap. **Decision
  (reproduce):** strip `[...]` for display only — the parser stays verbatim (tests/RE compare raw), and
  `FElysiumDlgLine::DisplayText()`/`DisplayMenuLabel()` (via `ElysiumDlgText::StripStageDirections`, which
  drops each `[...]` span, collapses the whitespace, and leaves an unterminated `[` intact) feed the box.
  Nothing is destroyed — a future remaster expression layer could still read the raw `[Amused]`. Covered
  by `Elysium.Substrate.DlgDisplay`. (Separately confirmed *not* a bug: the same comparison showed our box
  offering `[ Continue ]` where retail offers "Who are you?" — both of line 11's PC choices are clan-gated
  (`not IsClan(pc,"Malkavian")` / `IsClan(pc,"Malkavian")`), and a bare `play.bat` boot skips chargen so
  `pc` is unbound → both gates error-to-false → terminal. Faithful data; boot through New Game to bind `pc`.)
- **2026-07-24** — **RE5 resolved without the running game: VtMB dice are data-driven; the golden-test
  premise was void.** The plan was a `vroll` golden test against retail. Decompiling the full roll
  cluster (ctor `FUN_101d88b0`, roller `FUN_101d8b40`, `vroll` handler `0x100d7040`, the RNG/table
  path, loader `FUN_101d92b0`) showed the only RNG-dependent step — the die-face distribution — is a
  **100-entry weighting table loaded from `vdata/system/DiceRolls.txt`**, so the ground truth is a
  shipped data file readable offline (no retail run). Findings: (1) the console difficulty is
  **human-scale** — `vroll` passes `atoi(arg2)` raw, ctor stores `[0xb] = diff − 1`, die succeeds when
  physical face ≥ difficulty; (2) each face = `WeightTable[RandomInt(0,99)]`, and the shipped
  `Normal`/`Heavy`/`Light` tables are **all uniform d10** (`face = r99/10`), so `rng(0..9)` is faithful
  today, but the mechanism is genuinely data-driven ("make your own", per the file's own comment);
  (3) `RollResult` tier names confirmed 1:1 (`Botched/Failure/Partial Success/Success/Critical
  Success`); (4) `HealthModifiers` (`[0xe]`) is data-driven and all-0 in this install; (5) the pool is
  computed from the character sheet (`f(attribute+ability)`), not a CLI arg. **Decision:** the runtime
  resolver (9.6) will *load* `DiceRolls.txt` (its `TableWeightings`/`HealthModifiers`) rather than
  hard-code uniformity, so a data mod that reweights a die is honoured — matching the engine. New
  extract task **PL9** copies the file into the offline mirror. `recovered/dice-system.md` promoted to
  canonical; RE5 closed `[x]`. Ghidra dumps under `tools/ghidra/out/re5_*` (local-only).
- **2026-07-24** — **9.1 + B4 as-built: `.dlg` parser + dlgexpr + branch machine, and the in-game
  conversation runner with a visual-novel dialogue box.** Shipped together (B4 is 9.1's core made
  playable). Several calls worth recording:
  - **NPC-line col-4 is an *action* (exec), not a condition — resolved by data.** `game_runtime.md`
    §7 left open whether an NPC line's col-4 is ever evaluated as a gate. `jack_tutorial.dlg`'s entry
    line (id 11) carries col-4 `G.Story_State = -3` — an **assignment**. Evaluating that "as a
    condition" would syntax-error → error-to-false → the entry line would be wrongly skipped. So NPC
    col-4 (with col-5) is executed when the line is spoken; only a **PC choice**'s col-4 is the eval
    gate. `game_runtime.md` §5/§7 corrected in the same pass.
  - **Entry-point selection is interim, flagged pending RE.** The branch machine opens at the first
    NPC line with non-empty display text (the blank leading NPC lines 1–4 are not real turns). VtMB's
    exact opener-selection among gated leading NPC lines is not yet RE'd; the test-bench maps each have
    a single content opener, so this is faithful there. Recorded as a known limitation, not a divergence.
  - **dlgexpr rides the one evaluator, via a front-normalizer.** 9.1 does not add a second grammar to
    `ElysiumExpr`; it rewrites the dlgexpr surface into the pure-Python subset the installed host already
    speaks — skill-checks (`Seduction 7`) → `CalcFeat("Seduction") >= 7` (implicit `>=`), and the
    condition-level `&`/`|` → `and`/`or` (dlgexpr has no bitwise operators), the action-level `&` → `;`.
    Conditions/actions then route through `FElysiumEntityWorld::EvalCondition` (the installed CPython/expr
    host), so field-5 writes land in the same `G` the level script reads and inherit error-to-false (RE3).
  - **`CalcFeat` stays a stub (Int 0) until 9.4**, so skill-gated PC choices evaluate false and stay
    hidden. Faithful once feats exist; the tutorial beat is `G.`/`IsClan`-gated, so unaffected. Noted,
    not worked around.
  - **The interim dialogue UI is a purpose-built native-Slate visual-novel box, not the sign path and
    not VGUI.** Owner call ("visual-novel simple, geometry + transparency, as much native UE5 as we
    can"): `SElysiumDialogueBox` (translucent slab + numbered `SButton` rows, engine fonts, no art),
    added to the viewport by the HUD while a conversation is open, with the controller in
    `FInputModeUIOnly` (the VN freezes the world) and number-key/click selection. It is explicitly
    interim — 9.2 replaces it on the 8.6 UI stack. *Verified in the built game:* firing
    `Jack.StartPlayerDialogRemote` opens `jack_tutorial.dlg`, the box renders line 11 + the live-gated
    "Who are you?" choice, walking 11→21→id-22 runs the field-5 action to `G.Tut_Jack == 1`, and closing
    fires `OnDialogEnd → DialogPostProcess`. As-built detail: `roadmap-archive.md` 9.1 / B4.
- **2026-07-24** — **10.8 as-built: OpenLevel migration landed; texture cache re-scoped to the map
  actor (one correction to the plan below).** The migration shipped as scoped — hard travel via
  `OpenLevel`, the next-tick defer and `IsPlayerSeated` gate retired, cross-map state confirmed
  GI-scoped (the CPython host resolves the live entity world each call). **One correction:** the plan
  (and `map-architecture.md`'s old "Ownership" section) assumed `FElysiumTextureCache` was a
  process-wide *weak* index with the map actor holding the strong refs, so engine GC would free
  textures once `FlushAll`-on-travel was retired. It was actually a process-wide *strong*-ref static
  table whose only release was `FlushAll` — dropping `FlushAll` with no replacement would have leaked
  every map's textures. Resolution (owner-approved during the work): make the cache a **per-map
  instance owned by the map actor**, threaded into the material factory / static-mesh builder, so its
  strong refs drop with the actor and GC reclaims the textures. This keeps the intra-map dedup (each
  unique texture decoded once — the engine has no path-keyed cache for loose transient textures, its
  dedup being at the `.uasset`/`LoadObject` layer this project bypasses) while making freeing the
  engine's job. The weak-process-index model is moot under hard travel (only one map ever resident),
  so it was dropped rather than built. As-built detail: `roadmap-archive.md` 10.8.
- **2026-07-24** — **Map-lifecycle model: adopt OpenLevel (engine hard travel); retire the bespoke
  persistent-world content-swap.** Owner call. **Decision:** the target map-lifecycle model is UE5
  standard hard travel (`UGameplayStatics::OpenLevel` → `UEngine::LoadMap`) into a single reused shell
  `.umap`; on load, `AElysiumMapActor` reads the target VtMB map + landmark from GI-scoped state and
  builds all content in code (async behind a loading screen). This replaces `UElysiumMapSubsystem::Travel`'s
  in-place swap (destroy the old map actor, `FElysiumTextureCache::FlushAll`, `ForceGarbageCollection(true)`,
  spawn a fresh actor in the same persistent `UWorld`). **Options weighed:** (A) harden the swap;
  (B) OpenLevel hard travel — **chosen**; (C) seamless travel — rejected (multiplayer machinery; its one
  payoff, no hitch, is unavailable because the transition floor is the synchronous content build, not the
  travel). **Level streaming / World Partition are rejected outright:** both stream *authored `.umap`
  assets*, and this project builds every map in code from `tools/out/` intermediates — there is no asset to
  stream. **Why B:** it is the UE5 standard for a discrete-map single-player game, which is VtMB's own model
  (`trigger_changelevel` + loading screen), so faithful by the charter's default-to-reproduce. It hands world
  teardown + GC to the engine, retiring bespoke lifecycle code: the per-travel `ForceGarbageCollection(true)`
  (non-idiomatic — the engine expects incremental GC — and the trigger that detonated the orphaned half-built
  skeletal mesh), `FlushAll`-on-travel, the `RequestLandmarkTravel` next-tick defer (teardown no longer runs
  under the actor tick), and the `IsPlayerSeated` stale-pawn gate (a fresh world + pawn per map has no stale
  position). The swap's only unique upside — seamless / double-buffered transitions — is a modernization VtMB
  has no faithful version of; it is deferred behind an explicit future owner call if ever wanted. **Latency is
  model-independent:** the async/time-sliced build (roadmap 10.4) works under OpenLevel — the heavy build runs
  in the shell world's `BeginPlay` behind a loading screen, not inside `LoadMap`; OpenLevel forfeits only
  keeping the previous map resident during load (double-buffering, 2× memory), which VtMB never did.
  **Preconditions (already true; verified during migration):** all cross-map state is GI-scoped and must
  survive `LoadMap` — `UElysiumMapSubsystem` (incl. the `NextLandmarkSpawn`/`PendingTravel` carry-over),
  `UElysiumGameStateSubsystem` (`G`/quest/sheet), `UElysiumAudioSubsystem`, the CPython VM / script host. The
  generation-checked `Entity` handles correctly report "deleted" for a torn-down world (desired). World-attached
  audio voices die on travel (faithful — the discrete loading screen breaks audio continuity anyway).
  **Status:** a deliberate refactor, not a bug fix (the crash that prompted the review was fixed at source and
  was not a swap-architecture flaw). Scoped as roadmap **10.8**; trigger = the next map-lifecycle work, ahead
  of 10.4 (async travel now builds on the OpenLevel foundation).
- **2026-07-24** — **8.4: physics props/hinges are Chaos bodies + constraints; the RE'd I/O surface
  diverges from later Source; collision is convex-decomposed offline.** Five calls landed with the
  `prop_physics` / `phys_hinge` runtime. **(1) RE divergence — VtMB has `Wake`, not `EnableMotion`/
  `DisableMotion`/`Sleep`.** Datamaps recovered from `vampire.dll` (CPhysicsProp vtable `0x10474c44`,
  CPhysHinge `0x10447a54`; `DumpGrep`/`DumpDatamap` over the persisted `vtmb` Ghidra project):
  `CPhysicsProp`/`CBreakableProp` exposes **`Wake`** (`InputWake`), `Break`, and the skin inputs
  (`Skin`/`SetSkin`/`FadeToSkin`/`SetSkinFadeTime`) — **`EnableMotion`/`DisableMotion`/`OnMotionEnabled`
  do not exist** (added in later Source), and **no `InputSleep` symbol exists** in the binary (every
  `Sleep` hit is NPC AI). So the leaf exposes `Wake` only; motion is not script-toggled. Outputs: `OnBreak`
  (fired), plus the `OnBreakLevel1..8`/`OnBreakLastLevel`/`OnBreakConstraint` gib chain (declared by the
  datamap, **undriven** — no gib decomposition system). `CPhysHinge`/`CPhysConstraint`: fields
  `attach1`/`attach2`/`forcelimit`/`torquelimit`/`hingefriction`/`hingeaxis`; inputs `TurnOn`/`TurnOff`/
  `Break`; output `OnBreak`. **(2) Convex-from-render-mesh is decomposed offline (owner call, opted for
  the multi-hull path over the single-hull spec baseline).** A single whole-model hull is coarse for
  concave props (chairs, furniture — `chairoffice`→27 hulls, `retro_chair`→32, `trashgarage`→30 vs a
  `bottle`→1 once decomposed). `prop_collision.py` runs **CoACD** (approximate convex decomposition,
  optional dep) over each `prop_physics` model's decoded OBJ and emits a `props/<stem>.hulls` sidecar in
  the **exact world-collider format** (one hull per line, flat Unreal-cm verts); the runtime cooks one
  `FKConvexElem` per line. Deterministic, free at runtime (the exporter-side-merge pattern). CoACD absent
  or failing → a single whole-model hull line (the baseline spec), so the pipeline never hard-fails. **(3)
  `hinge_axis` converts at export.** `phys_hinge.hingeaxis` is a second raw-Source point left unconverted
  in `keys`; `UE_bsp_to_scene` now emits `hinge_axis` = normalized `source_dir_to_unreal(origin→hingeaxis)`,
  read verbatim (the "convert at export, never at runtime" rule) — the pivot is the already-converted
  top-level origin. **(4) A second `PostSpawn()` pass (Source's `Activate()`).** A constraint must resolve
  its `attach1`/`attach2` bodies *after* every entity has `Spawn()`'d, so `FElysiumEntityWorld::Load`
  runs a `PostSpawn()` pass after the spawn loop; the hinge builds a `UPhysicsConstraintComponent` there
  (twist axis = `hinge_axis`, swings/linear locked → one rotational DOF; `forcelimit`/`torquelimit` = 0 →
  unbreakable). **(5) Per-entity simulating body, A/B-gated.** `prop_physics` stands a per-entity
  `UStaticMeshComponent` (convex-cooked, `PhysicsActor` profile) via `BuildPhysPropVisual`, cached under a
  `#phys` key so a model shared with a non-solid `prop_dynamic` doesn't clash; `SetSimulatePhysics` +
  `override_mass` (>0 overrides, −1 keeps computed). New `elysium.PhysicsProps` (default 1) A/Bs
  simulation — 0 stands the body static/non-solid (visual parity). World `.hulls`/`.dispcol` colliders are
  `BlockAll`, so bodies land on the floor. **Deferred (recorded):** physics-driven constraint break firing
  `OnBreak` (the `OnConstraintBroken` delegate needs a UObject; the plain-C++ leaf fires `OnBreak` only on
  the explicit `Break`/`TurnOff` input); the gib `OnBreakLevel*` chain; runtime multi-convex for later maps
  if concave furniture becomes gameplay-relevant (the offline path already covers it). Verified: `build.bat`
  green; re-export emits `hinge_axis` + decomposed `.hulls`; `test.bat` Content/Substrate green. The
  Chaos settle/push feel + hinge swing await an owner in-game play test (like 4.1's mover note — physics
  feel is the one thing headless coverage can't judge).
- **2026-07-24** — **8.3: dynamic props render per-entity; orientation converts at export; Skin/anim are
  deferred stubs.** Three owner calls landed with the `prop_dynamic` render path. **(1) Per-entity
  `UStaticMeshComponent`, not shared ISMs.** The roadmap text ("the ISM path grows per-instance
  addressability per `entity_visuals.md` R2") pointed at the Godot MultiMesh design, but every runtime
  writer on a prop is per-entity (hide = `SetVisibility`, move = `SetRelativeLocationAndRotation`,
  model-swap = destroy+rebuild), so an individual component per prop sharing a per-stem cached
  `UStaticMesh` is the lower-risk fit; geometry is trivial (~20k tris/map, the perf constraint is
  lights), so the extra components are negligible. GAME_LUMP static props keep their grouped-ISM
  `.props` path (no identity, no double-draw). **(2) `model_quat` converts at export, read verbatim.**
  `.ents` left `angles` as a raw Source string (only `origin` was pre-converted), and props need full
  3-axis orientation; rather than duplicate the coordinate math at runtime, `write_entities` now emits
  `model_quat = source_angles_to_unreal_quat(angles)` (the same function the `.props` path uses) for
  every `model_mesh` entity, and the runtime reads it 1:1 — honouring the load-bearing "convert at
  export, never at runtime" rule. Absent → identity (older exports still load). **(3) `Skin` /
  `SetAnimation` are logged stubs.** Faithful `prop_dynamic` selects an alternate skin family / plays a
  skeletal sequence; the prop decode is LOD0 static geometry, skin 0 only (no alternate skins or skeleton
  exported), so these can only record the request until a skin-family / skeletal-prop export exists (a
  tracked follow-up). `Break` (hide + `OnBreak`) and the base ScriptHide/Unhide + 9.3
  SetOrigin/SetAngles/SetModel body-follow are real; props are non-solid (collision is 8.4).
  **Incidental fix (out of 8.3 scope, recorded):** the re-export tripped a latent working-tree bug in
  `UE_bsp_to_scene.py` — the decal-material branch wrote a 4-tuple into `mat_info` where the `.mtl`
  writer and the world-material branch use a 5-tuple `(albedo, emis, alphatest, translucent, additive)`,
  so a map with a decal material crashed the `.mtl` write mid-file. Fixed by appending the missing
  `additive=False` (water/decal is never additive); the tutorial `.mtl` regenerated whole (2476 lines).
  Verified: `build.bat` + `test.bat Content`/`Substrate` green; `elysium.PropBodies` A/Bs the bodies.
- **2026-07-24** — **9.3: the unblocked entity-manipulation surface landed; the Character-method fill
  is delegated to its backing systems.** The four `Entity` writers are real (runtime `Origin` +
  body-follow hooks move/re-face/re-skin the NPC body; `RenameEntity` re-keys the name index), the
  scripted two-phase spawn is real (`SpawnRuntimeEntity` split into `CreateRuntimeEntityNoSpawn` +
  `CallEntitySpawn`, guarded by `bSpawnCalled`), and the field-table audit registered the one genuine
  gap (`npc.times_talked`, read-only, resolves to 0 until B4). **Owner call (scope):** the 22 unbacked
  Character methods stay fail-closed stubs rather than growing a throwaway backing — inventory
  (~280 corpus calls, the largest demand) is a **tracked follow-up**, feats/stats are 9.4,
  disposition/camera/barter are B-track. **Honesty caveat, recorded:** `CreateEntityNoSpawn` on a
  leafless class (`item_*`, `prop_*`) yields a logic-valid but **bodiless** entity (findable, I/O-wired,
  no mesh) until a per-entity prop/item render path lands; NPCs get visible bodies. Verified: `build.bat`
  green; `Elysium.Substrate.RuntimeSpawn` (two-phase create + rename + runtime origin), the
  `times_talked` field check, and **`Elysium.Substrate.CPythonWriters`** — which drives the writers +
  two-phase spawn through the **real embedded-CPython glue** (`SetName` re-key, `SetOrigin((x,y,z))`,
  `SetModel`/`GetModelName` round-trip, `CreateEntityNoSpawn`→`CallEntitySpawn`) — all pass; and a live
  `sp_tutorial_1` load confirmed CPython + `tutorial.py` import + world build healthy with no errors.
- **2026-07-24** — **7.4 $envmap is a Lumen roughness channel, not a baked-cube sample.** Owner call.
  VtMB's DX8 `$envmap` world path sampled a baked cubemap (`reflect(v,n)` → mask → tint → additive).
  The remaster's render path is fully dynamic (HWRT Lumen), and 7.5 is "Real reflections — Lumen", so
  `M_World_Opaque` does **not** sample the exported `tex/cube/` faces: instead the `$envmapmask` (or a
  uniform white mask when a reflective surface carries none) drives an `EnvStrength`-scaled drop of
  Roughness from the calibrated 0.5 toward 0.15, and Lumen produces the reflection. Base roughness is
  held at exactly the value the old unconnected pin defaulted to (0.5), so non-reflective surfaces keep
  their calibrated look; `envtint`/`envmapcontrast`/`envmapsaturation` are parsed offline but unused by
  the world graph (DX8 artifacts). The cube faces stay exported for the 2D sky. 7.5 tunes reflectivity.
  On `sp_tutorial_1`: 240 of 439 world materials are reflective. `elysium.EnvReflect 0` A/Bs the whole
  path off (matte).
- **2026-07-24** — **The dev boot path seeds `Linux_Wine=1` to suppress `popup_linux`.** The tutorial's
  `linux_check` (`logic_pythoncheck`, `python_script "G.Linux_Wine == 1"`) fires `OnFalse ->
  popup_linux.OpenWindow` — the Unofficial Patch's "an important Python script has not compiled
  correctly / you are in a Linux Wine environment, run Loader.exe" warning — whenever `G.Linux_Wine`
  is not 1 when `logic_auto.OnMapLoad -> linux_check.Test` fires (t=0.1). `Linux_Wine=1` is the patch's
  "Python works" sentinel (set by `vamputil.setBasic`/`setPlus`); `BeginNewGame` already seeds it, but
  the **bare `-ElysiumMap` dev path** (`play.bat`/`profile.bat`) skips `BeginNewGame`, so the check read
  `OnFalse` and the popup fired. Our embedded CPython always runs, so the warning is a false alarm on
  that path — `AElysiumGameMode::BeginPlay` now seeds `Linux_Wine=1` before the bare `Travel`, so
  `linux_check` reads `OnTrue`. (The value is a *suppressor*: setting it to 0 would *show* the popup, on
  New Game too.) Verified headless: no `popup_linux.OpenWindow` delivery on a bare `sp_tutorial_1` load.
- **2026-07-24** — **B3 minimal NPC presence: real glTF bodies for all `npc_*`, and runtime entity
  spawn.** Owner call (four forks). (1) NPCs stand their **real** `out/npc/<stem>.glb` skeletal body
  (not a placeholder) — PL4 already emitted the glbs and 8.2 the loader, so the faithful result is the
  cheap one; the model→stem map is the lowercased basename of the `.mdl` key, verified 1:1 for every
  tutorial NPC, so no `npc_manifest.json` lookup. (2) **All** `npc_*` with a model get a body at load
  (cached per stem, gated by `elysium.NpcBodies`), not just the beat's two — "stands the model at its
  origin" applies to every character. (3) `StartPlayerDialogRemote` fires `OnDialogBegin` only and leaves
  the session open; a manual **`EndDialog`** input (fireable via `ent_fire`) fires `OnDialogEnd`, keeping
  B3/B4 cleanly split until B4's `.dlg` runner replaces the manual close. (4) `npc_maker.Spawn` spawns
  exactly one child and **ignores `Flag_StartDisabled`** (retail fires `blueblood_maker.Spawn` without
  enabling it) — `SpawnFrequency`/`MaxLiveChildren`/`MaxNPCCount` are unmodelled (No AI). **Substrate
  consequence:** the entity world gained `SpawnRuntimeEntity` — a runtime-synthesized def stored past the
  map's immutable def array, appended to `EntityList` (Resolve indexes it directly, so identity needs only
  the append; `ResolveTargets` copies target pointers before firing, so a maker spawning mid-delivery is
  safe). The NPC skeletal body is a `USkeletalMeshComponent` on the map actor (not a separate actor),
  torn down with the world like the brush bodies. Facing uses the entity `angles` yaw (negated for the
  Source→Unreal Y reflection); exact facing is cosmetic and left to a later feel pass. **Feeds 8.5**
  (→ `[~]`); its `scripted_sequence` anim-at-marker and bank retargeting stay open.
- **2026-07-24** — **7.2 decals: deferred `UDecalComponent` chosen; the PMC-parity stage skipped.**
  Owner call. The task was framed as two stages (translucent PMC mesh for parity → `UDecalComponent`
  later, decide after both render); we built only the deferred path. A deferred decal writes the
  GBuffer *before* the lighting pass, so it is lit exactly like its host wall — **Lumen indirect
  bounce included** — which VtMB's bounce-dominated look needs and the PMC translucent path cannot
  give. Building the endgame path once avoids maintaining a throwaway. **Pipeline consequence:** the
  exporter no longer meshes decals — the `infodecal` projection now emits a `<map>.decals` projector
  sidecar (material + centre + room-normal + s/t axes + half-extents, Unreal cm) instead of
  `_decals.obj`; the runtime builds one `UDecalComponent` per line off a new `M_Decal` deferred
  master (regular `BLEND_Translucent` — the old `DecalBlendMode` is deprecated/no-op since UE 5.2).
  `M_Decal` is authored here even though it is listed under 7.4's master-material set, because the
  decal path needs it now; 7.4 still owns the rest (`M_World_*`, water, etc.). **Orientation took two
  capture passes to settle** (the API docs don't spell out the decal UV frame): a deferred decal maps
  texture **U→local Z, V→local Y** (not the intuitive U→Y), so `BuildDecals` uses
  `MakeFromXZ(Normal, SDir)` with the surface horizontal `SDir` on local Z and
  `DecalSize = depth×HalfH×HalfW`; and because V (local +Y) points up while VtMB authors V top-down,
  `M_Decal` samples at `(U, 1-V)` to un-flip vertically. The first attempt (`TDir` on Z) rendered
  decals rotated 90° + stretched; the second was upright but upside-down. `elysium.DecalFlipU` remains
  a horizontal-mirror knob. A/B via `elysium.Decals`; depth via `elysium.DecalDepth`. **Screenshot
  tooling:** `AElysiumHUD::DrawSignPanel` now gates on `elysium.DrawSigns` (the shot harness sets it 0
  so a map-load `game_sign` popup — the tutorial's `linux_check` Python-compile warning — does not
  cover every plate), and `shots.bat` renders **off-screen** (`-RenderOffScreen -ForceRes`), so no
  game window opens.
- **2026-07-24** — **PL4 done: shared-bank NPC format over the per-NPC monolith.** Include-model
  resolution cracked from data + VAMPTools: `NumIncludeModels`@404 / `IncludeModelIndex`@408 →
  `StudioModelGroup[]` (**stride 116**, not the naive 8 — `int Filler[27]` after the two index
  fields; `FilenameIndex`@0 relative to the group-entry base). The include tree is a recursive DAG
  (`frenzy`/`pc_idles` reappear), resolved cycle-deduped. Every bank bone name is present in the
  NPC skeleton (`move_and_ranged` 60 / `stances` 53 bones, 0 missing in a 69-bone gangmember), so
  clips retarget by **bone name** with no proportion rig. **Format decision (informed by measured
  cost):** baking the full include tree into each NPC glb measured **~94 MB / ~90k accessors per
  NPC** (81 MB anim payload + 13 MB JSON) → ~4.2 GB for 45 NPCs and ~1 GB resident on a busy map.
  Rejected. Adopted the shared-bank decomposition instead — which is also how modern engines and
  VtMB's own `virtualmodel` handle it: `out/npc/<npc>.glb` (mesh + skeleton + own clips),
  `out/npc/banks/<bank>.glb` (skeleton + a shared bank's clips, no mesh, decoded once),
  `npc_manifest.json` (`clip → owning-stem`). The runtime (8.5) loads a bank once and applies its
  clips to any NPC skeletal mesh by bone name via glTFRuntime — verified in the vendored plugin
  source (`LoadSkeletalAnimationFromTracksAndMorphTargets` binds tracks to the ref skeleton by
  `FindBoneIndex(BoneName)`, and `LoadSkeletalAnimation(mesh, …)` takes an external mesh). Full
  cast: **45 NPCs / 62 banks / ~410 MB** (243 MB shared banks + 149 MB meshes + 31 MB textures) in
  ~3.5 min, vs 4.2 GB. Bank stems keep the sub-path so male/female (and clan) banks that share a
  basename stay distinct. Pipeline: `mdl_skel.resolve_tree`/`local_sequences`,
  `mdl_gltf.export_npc`/`export_bank`, `npc_export.py` (`export_all.py --npc`). Runtime consumption
  is 8.5. *Feeds:* 8.5.
- **2026-07-24** — **Brush touch requires a pawn toucher, and waits until the pawn is seated.**
  Bug fix. `elysium.newgame` (or any travel) *from an already-loaded map* warped the player off
  the tutorial porch into the downtown alley and looked like the spawn "moving to the next spawn".
  Cause: two unrelated things collided. (1) Every brush body on a map is a component of the one
  `AElysiumMapActor`, so when the new map builds its bodies, Unreal fires begin/end overlap for
  every trigger∩trigger and trigger∩solid pair at once — and `UElysiumBrushComponent::RouteTouch`
  forwarded *all* of them as touches, since it never checked who was touching. (2) The pawn carries
  over from the previous map and is placed at the new map's spawn only on the first tick *after*
  the entity world exists, so for a moment it stands wherever the old map left it. Together, the
  phantom geometry-overlap touches ran real scripted beats — `trig_feed_fix.OnStartTouch ->
  fix_fade.Fade -> teleport_player.Teleport` — and teleported the player. Fix: `RouteTouch` now
  routes only when the overlapping actor `IsA<APawn>` (VtMB never treats geometry∩geometry as a
  touch — only movers touch), and only once `AElysiumMapActor::IsPlayerSeated()` is true (the pawn
  has been moved to this map's info_player_start/landmark, or this map requested no placement).
  Verified: `elysium.newgame` from a loaded `sp_tutorial_1` now produces zero `teleport_player`
  deliveries, zero `fix_fade`/`trig_feed_fix` fires, and exactly one legitimate touch — the player
  genuinely standing in the (inert, StartDisabled-on-arrival) `trig_theater_to_tutorial` changelevel
  volume at the porch, which fires nothing. Do not remove either guard. When NPC pawns (8.5) and
  physics props (8.4) can trip triggers, widen the `APawn` test rather than dropping it.
- **2026-07-24** — **Agentic QA is Layer 3 of the debug architecture, not a new track (P2.7–2.9).**
  `debug-tooling.md` already framed the `elysium.*` console verbs as "the thin scriptable layer for
  `-ExecCmds` automation and headless runs" — the third consumer beside the human (Cog) and the
  script. An AI agent is that third consumer made first-class: the same runtime state, reached
  through structured MCP tools instead of parsed console text. Owner call — chosen shape and why:
  (1) **Direct `AddTool()` registration, not the Toolset-Registry adapter** — the adapter is
  editor-only, but this project's whole loop is `-game`/cooked with no editor content loop, so tools
  register through `IModelContextProtocolModule::AddTool()`, which serves in every target. (2)
  **Tools live inside `ElysiumUE`, not a separate module** — `FElysiumEntityWorld` and the substrate
  carry no `ELYSIUMUE_API` exports (plain C++, Unreal supplies bodies only), so a sibling module
  couldn't link them; the MCP layer follows the vendored-Cog precedent (non-Shipping/editor private
  dep). (3) **On-by-default in dev, loopback + no-auth** — the server auto-starts wherever the plugin
  is present (editor target only, so never in Shipping/Test), because a QA surface you must remember
  to enable is one you forget; `-NoElysiumMcp` opts out. (4) **Two-tier tests** — a `-nullrhi` content-free suite that is the real
  regression net (substrate paths), plus a content-gated suite that self-skips on an empty
  `tools/out` so a fresh checkout stays green; the LLM is never the oracle — it *drives* Unreal's own
  automation runner. (5) **Screenshots share the profiler's vantages** (`ElysiumVantages.h`) so a
  look regression and a cost regression are the same frame; baselines are game-derived → gitignored.
  Industry survey that informed this (Epic's in-box `ModelContextProtocol` plugin, the community
  `unreal-mcp` servers, Gauntlet vs. functional-test guidance, the fire→screenshot→assert playtest
  loop): the editor-centric MCP toolsets buy little here, so only `AutomationTestToolset` +
  `LiveCodingToolset` are enabled beside ours; Gauntlet is deferred (single platform, no net
  sessions). Full design: `debug-tooling.md` Layer 3.
- **2026-07-24** — **Field-6 payloads evaluate in `__main__`, not the level module (B2 revises
  9.3a).** RE first. `G`'s type object (`PyDataManager`, `0x1058fa08`) carries a `tp_as_mapping`
  (`0x1058f9f8`) whose `mp_subscript` (`0x1019b4a0`) literally **tail-jumps into `tp_getattr`**
  after `PyString_AsString`, and whose `mp_ass_subscript` (`0x1019b720`) calls `tp_setattr` the
  same way — so `G[k]` **is** `G.k`, default-on-miss 0 and all. Reproduced, because
  `DialogPostProcess()` calls `saveState()` (`for k in G.keys(): G_tut[k] = G[k]`) on its first
  line and would otherwise never reach the beat branch. Two knowing divergences on that surface:
  a **non-string key on assignment raises `TypeError`** where retail tail-calls `PyDict_SetItem`
  with the manager object in the dict slot (a latent bug no shipped script reaches — every G key
  is a string), and our proxy keeps a **`has_key` method** that retail's 2-entry table does not
  have (retail resolves `G.has_key` to a flag read of 0; no script calls it, and the method
  predates this task on the expr host's G surface too).
  The namespace change is the second half. 9.3a evaluated payloads in the **level module's** dict;
  VtMB wraps every payload as `__main__.%s` (`0x1055e370`), which resolves the leading name as an
  attribute of `__main__` — so the level script's own `def`s must be *in* `__main__`, and so must
  the engine globals. Measured over the 10 exported maps' 363 field-6 payloads, the module-dict
  choice breaks 2 of them: `hw_609_1` fires a bare `FindPlayer().ClearActiveDisciplines()` and
  `hollywood.py`, unlike `tutorial.py`, never aliases `FindPlayer` at module level. `LoadLevelScript`
  now merges the imported module's public top-level names into `__main__` and everything evaluates
  there. A function keeps its defining module's globals, so `DialogPostProcess` still reads its own
  `G_tut`/`Find`/`statemap`; only the entry-point lookup moved. `__main__.Level = __name__` is the
  cross-check that retail imports-then-merges rather than exec'ing into `__main__` — the assignment
  only carries information if `__name__` is the script's own module name.
  Third, smaller call: the bootstrap's `IsClan`/`IsIdling` stubs (vamputil's, not engine API — they
  exist only so `tutorial.py`'s import-time guard resolves) now return **0** instead of a truthy
  stub object. A truthy predicate made every clan gate take its *first* branch, i.e. silently play
  as Brujah; 0 falls to the `else`, which is the honest "no clan matched". Real behaviour arrives
  with the real `vamputil` in 9.3b/B5.
- **2026-07-23** — **B1 landed, and `SF_FADE_STAYOUT` is the flag that brings the screen *back*.**
  RE first, over `vampire.dll` + `client.dll`. `CEnvFade::InputFade` (`0x10100F90`) maps
  spawnflags to the client's fade flags as `SF_FADE_IN`(0x1) → **0**, else `0x2` (`FFADE_OUT`)
  `| 0x20` when `SF_FADE_STAYOUT`(0x8); `SF_FADE_MODULATE`(0x2) → `|0x4`; `SF_FADE_ONLYONE`(0x4)
  sends to the activator alone. It then fires `OnBeginFade` at delay 0 and returns — no think, no
  second output. `CViewEffects::FadeCalculate` (`client.dll` `0x10197190`) is where the meaning of
  `0x20` lives: when a fade passes both `FadeEnd` and `FadeReset` it is normally **dropped**, but
  with `0x20` it instead flips to a fade-in (`flags &= ~0x22 | 0x1`), negates its speed and takes
  one more `duration` to uncover. The client's genuinely-permanent bit is `0x8` (which re-pushes
  `FadeReset` to `curtime + 0.1` every frame) and `env_fade` never sets it; `0x40` disconnects to
  the menu when the fade ends. **4.5 had this backwards** — it held `STAYOUT` covered forever and
  ramped the plain fade back — which would have left warp #2 on a black screen through Jack's
  dialogue. Corrected: cover over `duration`, hold `holdtime`, then uncover over `duration` only
  when `SF_FADE_STAYOUT`, else expire. That reproduces `teleport_fade`'s authored timing exactly
  (`duration 1`, `holdtime 2`, clear at t=4 — precisely when its `Jack.StartPlayerDialogRemote`
  wire fires). **`OnEndFade` and `ReverseFade` are not VtMB** (no such string in `vampire.dll`);
  the `ReverseFade` input is removed rather than kept as a debug affordance, so the Inspector's
  input list stays a faithful mirror of the datamap. `SF_FADE_IN`'s flag-0 path is reproduced
  as-is including its quirk — `FadeCalculate`'s `flags & 0x3` test fails, so the colour sits flat
  at full alpha for `holdtime + duration` and then snaps clear, with no ramp either way; no
  tutorial `env_fade` sets it. The fade stays **one slot** rather than VtMB's fade list: the list
  sums colours and maxes alphas, which is indistinguishable from one slot while every fade on a
  map is the same colour, and all seven on the tutorial are black.
- **2026-07-23** — **Inspection is click-to-select, not crosshair-follow (P2.6).** Owner call. The
  live crosshair inspector is removed: the Entity Inspector no longer traces the camera ray every
  frame. Instead, while the Cog menu owns the mouse, LMB over the world picks whatever is under the
  cursor and RMB clears — no pause (the world keeps running under the cursor; Time Scale stops it
  when wanted). The pick lives in `RenderTick`, which Cog runs for every window regardless of
  visibility, so it works with the inspector closed. Three things this settled:
  **(1) Physics cannot answer the pick, so two of the three sources are CPU ray-casts.** Under the
  default `elysium.BrushCollision 1` the world *render* mesh is built with collision off — the
  `.hulls` convex set is the collider, and it carries no material and no face, so a trace could
  never name the surface it hit (the old crosshair readout only worked at all under
  `elysium.BrushCollision 0`). Separately, a solid prop's cooked collision is a **single convex hull
  of the whole model**, and non-solid props have none. So `ElysiumPick::Trace` physics-traces only
  the entity bodies (`LineTraceMulti` on `ECC_Visibility`, which returns trigger overlaps too) and
  CPU-casts the prop instances and the world/sky sections against their real triangles. The map
  actor retains the geometry for it (`FPropPickSoup` per unique model, ~3 MB on the tutorial; a
  section → OBJ-group-key table), all `#if !UE_BUILD_SHIPPING`.
  **(2) A world pick highlights the BSP face, and that falls out of the exporter for free.**
  `UE_bsp_to_scene.py`'s `emit()` appends a fresh vertex per face corner (no dedup across faces) and
  `BuildMeshFromObj` remaps sections keyed on the *global* index, so the triangles of one face share
  local indices and adjacent faces share none. Flooding across shared edges therefore stops exactly
  at the face boundary — no position weld, no normal threshold, no bleeding around a corner. The
  coplanarity guard only bites on displacement grids, where one face is a whole curved patch. The
  flood costs an edge map over the section, so it runs on click; hover previews the single triangle.
  **(3) The highlight is imgui, not scene geometry.** Considered and rejected: custom-depth stencil
  outline (best silhouette, but cannot outline an invisible trigger volume and lights every instance
  of a prop model at once), a retained x-ray box (bounds-only, coarse on a world section), and a
  material tint/checkerboard on `M_VtMB_World` (cheapest code, worst granularity — the world OBJ is
  grouped per material, so it would highlight every face sharing that texture map-wide). Projected
  fill + outline + label is the only one exact for all three target kinds, needs no assets, and
  keeps drawing when the world is time-scaled to a stop, since Cog's render tick is not the game
  tick. Known bound: an entity's highlight is one box per def hull (the hulls are vertex sets with
  no faces) — exact for the axis-aligned box brushes nearly every trigger and door is made of.
  **(4) A World Viz gizmo marker outranks the geometry it is drawn over.** Owner call. The marker is
  a deliberate "select me" handle, and it is the *only* clickable representation the ~1,000 bodiless
  entities on a map have (on `sp_tutorial_1`, 394 of 1,868 records are `light`/`light_spot` alone —
  they are in the `.ents` lump as inert records, so they carry gizmos). Three sub-calls: **drawn is
  the whole test** — `Visible` depth-tests the cubes so one behind geometry does not pick, `All` is
  x-ray so any does, `Off` skips the source — which needs the nearest *rendered* hit tracked apart
  from the pick winner, since brush bodies render nothing and must not occlude a marker behind them;
  **the hit test is the exact 28 cm cube**, not a screen-space tolerance, so dense clusters stay
  separable at the cost of distant markers being small targets; and **clicking a cluster again
  cycles** to the next marker behind the current one, wrapping. With gizmos on, the bodiless-entity
  perpendicular fallback (2 m, inherited from the `ent_*` picker) is suppressed — it is far looser
  than the cube and would undo the precision — and stays armed only with gizmos off. The marker's
  anchor and size are consolidated into one definition (`ElysiumGizmoColor.h`) shared by the ISM
  layer, the label/beam overlays and the pick; they had been duplicated in two files, and the
  what-you-see-is-what-you-click rule only holds if they cannot drift.
  The `ent_fire` crosshair picker and the HUD's `+use` reticle are unaffected; they are separate
  systems.

- **2026-07-23** — **Direction: VtMB *remastered*, not a pixel-perfect recreation.** Owner call.
  Tone, ambience, feel and game logic are kept; craft is raised with tools 2004 did not have.
  Written up as `docs/remaster-direction.md` (the charter); `rebuild-strategy.md` principle 7
  and this doc's north star restated to match. Four decisions carry the weight:
  **(1) Three change layers, three rules.** *Presentation* (UI, type, HUD, textures, post) modernizes
  freely under the art-direction test, no approval gate. *Feel* (movement, camera, combat) is
  built faithful first, kept A/B-able, and polished one delta at a time by explicit call.
  *Logic and content* (entity semantics, I/O, scripts, dialogue, stats, saves) is reproduced.
  Layer assignment happens before the work, not after — the boundary is *game state*, not
  visibility.
  **(2) The governing rule: we only change what we understand, and only on an explicit call.**
  RE comes first — a behavioural divergence may only be *proposed* once the faithful behaviour
  is known and recorded, and it lands only with a dated owner decision in this log carrying both
  the faithful and the chosen behaviour. Default resolves to reproduce. This is why the RE
  backlog does not shrink under a remaster direction: you cannot judge what to keep until you
  know what is there.
  **(3) The UI drops its faithful path; the world keeps its.** The pixel-faithful VGUI port is
  **not built** — there is no classic UI mode. VtMB's screen structure (inventory, panel
  anatomy, reading order, palette, iconography, strings) is kept and **re-skinned**: vector/SDF
  type replacing the `.fnt` bitmap atlases, resolution-independent layout replacing the 640×480
  proportional canvas and the patch's `//ws-fix` pairs, restrained motion, gamepad-navigable
  components. The bitmap fonts are the loudest defect in the game on a modern display, and
  illegibility was hardware, not art direction. `m0_menu_build.md` and the `.res`/scheme/`.fnt`
  decoders become **reference and extraction machinery** (PL8), not a runtime layout stack. The
  world is untouched by this: geometry/placement/lighting stay anchored to VtMB's data and the
  lightmap calibration, with `elysium.EnhancedTextures` as the A/B toggle on top.
  **(4) Asset enhancement leaves the Options list and becomes scope** (remaster axis 2), with
  its tiers, its per-family curation and its A/B toggle unchanged, and its **P10 sequencing
  unchanged** — it is polish on a shipped look, not a blocker.
  Task deltas: P8 renamed *Characters & UI*; **8.6** recast from "VGUI menu port" to the UI
  foundation (design system + shell + menus + New Game); **8.8** recast from "sign window — VGUI
  fidelity" to sign/popup panels on that foundation (the `CSignUI` 1024×768 canvas model stays
  the *intent* reference, not the runtime coordinate system; the `Font_640`…`Font_1600`
  per-resolution overrides are dropped — vector type scales continuously); **8.9** (HUD on the
  foundation) and **8.10** (accessibility & options backing) added; **4.7** gains the
  faithful-first feel-layer note; **9.2** dialogue content verbatim, presentation modern;
  **PL8** added for the UI source inventory. Three risks registered: UI losing VtMB's voice,
  polish leaking into the logic layer, and having no classic mode to A/B against.

- **2026-07-23** — **9.3a: the level scripts are wired; CPython is the default host.** The 5.5 PoC
  proved the embed offline but nothing connected it to a map — `LoadLevelScript` was reachable only
  from `elysium.py.load` and the Cog button, and the map-load host was still ElysiumExpr. Three
  decisions closed that:
  **(1) The import happens off the parsed `.ents`, before the spawn pass** — not off the live entity
  world after it. VtMB runs the level script's top-level code first and spawns entities into that
  namespace; doing it after `FElysiumEntityWorld::Load` would leave a spawn-time field-6 payload
  evaluating against a module that does not exist yet. Hence `FElysiumEntityDefs::LevelScriptModule()`
  rather than a worldspawn walk over the built world.
  **(2) Importing is a host capability, and the module name is remembered.**
  `IElysiumScriptHost::LoadLevelScript` defaults to "this host cannot import", which the expr and null
  hosts inherit truthfully; `SetScriptHost` re-imports the remembered module into whatever host is
  installed next. Without that, `elysium.script.cpython 1` mid-map would install a CPython host with a
  bare `__main__` and every level constant would read NameError — an A/B that lies.
  **(3) A CPython VM that fails to start falls back to the expr host.** Its evals would all be Void,
  which is exactly what error-to-false looks like (RE3) — so a missing `python27.dll` in a packaged
  build would degrade the entire scripting surface **silently and plausibly**. `MakePreferredScriptHost`
  checks `IsUsable()` and warns.
  Routing `EvalScript` through the installed host came with it: the console verbs and the Cog eval box
  hard-wired ElysiumExpr, so with CPython live they would report NameError for names the game itself
  resolves — a debug surface contradicting the runtime. `IElysiumScriptHost::Eval` gains an optional
  `OutError` (a Void return cannot distinguish "evaluated to None" from "raised"), and the eval/exec
  split collapses to one path: the CPython host already tries `Py_eval_input` then `Py_file_input`, and
  `ElysiumExpr::Exec` returns its last statement's value.
  Verified in the built game, not offline: `sp_tutorial_1` imports `tutorial` at map load into host
  `cpython`; `elysium.eval cCelerity` = **8**; `G.Tutorial_Discflags |= cCelerity` flips `G` to **8**;
  `elysium.script.cpython 0` returns NameError; travel to `sm_pawnshop_1` re-imports per map.
  That last one surfaced the first real gap: `santamonica` raises `ImportError: cannot import name
  RandomLine` — the bootstrap's `vamputil` stub is thinner than the retail module. Logged, non-fatal,
  map load continues. It is 9.3 work, and it is evidence for doing `Entity.__getattr__` and a real
  `vamputil` before hand-porting the 24 Character methods.

- **2026-07-23** — **There is a fifth scripting surface: the console (9.3b + PL5d).**
  `python_bridge.md` lists four Python surfaces (level scripts, `.dlg`, entity field-6,
  `logic_pythoncheck`); the console is a fifth path and it is **bidirectional**. Scripts execute
  console commands by attribute-assigning on `__main__.ccmd` (`c.patchtype = ""`), and a command
  the console cannot resolve falls through to Python. The Unofficial Patch uses that round trip as
  its whole Basic/Plus switch — the install variant lives in `cfg/user.cfg`
  (`alias patchtype "setPlus()"`), not in any script or map, so `setPlus`/`setBasic` appear
  uncalled to any search of `.py`/`.ents`/`.dlg`/`.bsp`. Found while asking why `trig_popup_move`
  never fires: `logic_auto.OnMapLoad -> unhidePlus()` is wired on 107 of 108 maps and is the
  ignition for the whole chain, so with `ccmd` unbound every Plus-mode entity tweak silently
  no-ops and `G.Patch_Plus` stays 0. Tracked as **9.3b**, with the cfg copy as **PL5d**.
- **2026-07-23** — **The sign panel draws on a 1024×768 canvas, uniformly scaled by height (4.10).**
  The layout is **client.dll's `CSignUI`**, not `vampire.dll` — the game DLL owns only the entity and
  `LoadSignData`, which is why the earlier `sign.txt` dump had the file resolution but no paint code.
  `FUN_10061520` parses; `FUN_10061830` (background) and `FUN_10060560` (text block) map the authored
  rect through the doubles at `0x10227ec8` = 1/1024 and `0x10227eb8` = 1/768 and call SetPos/SetSize
  (`FUN_101a9950`). Three findings the data alone could not give:
  (1) **The scale is uniform, driven by height** — `FUN_100cd100`, which the width math divides by
  1024, is a 4:3-proportional width (`ScreenH·4/3`), not the backbuffer width, so `Wide·A/1024`
  collapses to `Wide·ScreenH/768`. The canvas keeps its aspect and letterboxes horizontally rather
  than stretching. Measured against the retail game on 16:9: panel width is **1.513×** the screen
  width, where a stretch model predicts 2.0× and this one predicts 1.50×.
  (2) **Text blocks are children of the panel**, so `XPos`/`YPos` are offsets inside the
  `BackgroundImage` rect, not screen coordinates — `FUN_10061830` is a `CSignUI` method setting the
  panel's own bounds. The shipped data corroborates: the patch moved `tutorial_popup_moving1`'s body
  text 324 → 836 in the same edit that widened the background 1024 → 2048, and a centred 2048-wide
  panel starts at virtual −512, so −512+836 lands on retail's original pixel.
  (3) When `XPos + YPos == 0` the background takes a **centring branch**, which is why
  `interface/Pop_Ups/general` at `2048×1024` deliberately overscans and bleeds off every edge.
  Three keys the entity-side survey had missed also turned up: **`HideHUD`**, and **`ClientCommand`**
  inside a **`Rules`** block (with `CloseOnLeftClick`, whose retail default is the panel's constructed
  value = **true**, and `MinShowTime`). Fonts pick by *exact* screen-width match on
  640/800/1024/1280/1600, else the plain `Font`, else `"Default"`.
- **2026-07-23** — **Sign windows tracked, split substrate/UI (4.10 + 8.8 + PL5c).** `game_sign` /
  `prop_sign` had no task: the only mention anywhere was `entity_visuals.md` R5, which called them
  "textured quads/decals… small, cosmetic; last" — wrong, and the reason they were never scheduled.
  The decompile settles what they are (`CGameSign::LoadSignData` `FUN_10212da0` /
  `CPropSign::LoadSignData` `FUN_10212200`): a **full-screen VGUI window** whose `definition_file`
  is a `SignData` KeyValues panel, opened by `OpenWindow` or `+use`, with a first-true `Sign
  { dependency filename }` redirect evaluated as Python (`Py_eval_input`, error-to-false) — i.e. a
  UI + scripting subsystem, not geometry. They are also **load-bearing for the tutorial**: 71 of the
  73 `game_sign` in the game sit on `sp_tutorial_1` as the `popup_*` help windows, and `tutorial.py`'s
  beat machine opens/rewrites them directly, so "tutorial completable as retail" (P9) can't be met
  without them. **Split:** the entity classes + a Canvas panel land in P4 as **4.10** (so the
  substrate is complete and the tutorial's 51 `OpenWindow` wires stop dropping, before P5's scripts
  start firing them), and the pixel-faithful panel waits for the VGUI stack the menu port brings, as
  **8.8**. The definitions + background materials export as **PL5c**. R5 in `entity_visuals.md` is
  corrected to point here.
- **2026-07-22** — **5.5 decided: embed CPython 2.x (option c), plus a working PoC.** The survey
  settles it. The 36 loose level scripts (out/scripts, **16,473 lines** excl. the bundled 2.1 stdlib)
  are full Python 2.1, not an expression dialect: **1,119 `def`, 45 old-style `class`, 345 `for` /
  26 `while`, 30 `try`/23 `except`, 162 `import`, 627 `print` statements, 3 `exec`, list-comps,
  `lambda`, `%`-formatting**, importing `random`/`time`/`types`/`struct`/`string` (+ VtMB's own
  `lib/` pickle/string/random). That is decisively past `ElysiumExpr` (an expression evaluator — no
  statements/classes/control-flow/exceptions/imports); option (a) would mean reimplementing all of
  CPython 2 + a stdlib, and (b) transpiling 16.5k lines of dynamic Py2 with `exec`/pickle/old-style
  classes is huge and fragile. **The 2.1→2.7 delta is ~0** (checked every script: **zero**
  string-exceptions — the one thing removed in 2.6 — and **zero** `from __future__`; classic `/`
  division and old-style-`class` are the default on both), so a maintained 2.7 fork runs the retail
  scripts 1:1. VtMB's own VM is stock CPython 2.1 (`vampire_python21.dll`, 653 exports; `.pyc` magic
  60202), and it saves *through* `pickle` — so a real embed aligns with the R8 save model rather than
  fighting it (G is proxied so C++ and Python share one store).
  **Fork chosen: `qnox/python-2.7`** (CPython **2.7.18**, actively maintained — release `v20260109`,
  Jan 2026). Its `x86_64-pc-windows-msvc` install_only build is **MSC v.1944 (VS2022) / 64-bit** — the
  same toolchain family as UE 5.8 — with headers + `python27.lib` + `python27.dll` + stdlib; verified
  running (classic `7/2==3`, stdlib imports). Tauthon (2.7.18 + Py3 backports, semi-maintained) is the
  fallback; actual 2.1 isn't worth the modern-MSVC pain given the ~0 delta.
  **PoC pulled forward from 9.3 (per the scope call):** vendored the SDK at
  `Source/ElysiumUE/ThirdParty/CPython27/` (dll+lib+headers+PythonHome/Lib); `Build.cs` wires it Win64-
  only (delay-load `python27.dll`, `ELYSIUM_WITH_CPYTHON`, stdlib as a RuntimeDependency); the include
  shim (`ThirdParty/ElysiumPython.h`) undefs `_DEBUG` across `<Python.h>` (else `Py_DEBUG` ABI + the
  `python27_d.lib` auto-link break the build — the standard PythonScriptPlugin trick). New
  `FElysiumPythonVM` (process-global; `GetDllHandle` the vendored dll → `Py_SetPythonHome` → `Py_NoSite`
  → `Py_Initialize`) registers a `vampire` C-module whose only *real* binding is **`G` proxied onto
  `UElysiumGameStateSubsystem`** (attribute get/set = flag read/write, default-0 / assign-None-deletes,
  `keys`/`has_key`/`ClearAll`); a Python **bootstrap** stands up forgiving stubs for the natives 9.3 will
  make C (`FindPlayer`/`FindEntityByName`/… + a stub `vamputil`), and stdout/stderr route to the UE log.
  `FElysiumCPythonScriptHost` slots into the existing `IElysiumScriptHost` seam (`elysium.script.cpython
  [0|1]`), alongside `elysium.py.smoke`/`exec`/`load`/`fire` verbs and a **CPython panel in the
  `Elysium.Scripting` Cog window** (status/version, host toggle, load-level-script, list + fire the On*
  callbacks, a python exec box; the G table + eval log below reflect its evals). **Validated offline
  with the vendored interpreter against the real `tutorial.py`:** it imports cleanly (its module-level
  code + `from vamputil import *` run, `levelscript` loads, `cCelerity=8` resolves), its On* callbacks
  execute, and field-6-shaped statements resolve level-script constants —
  **`G.Tutorial_Discflags |= cCelerity` flips G to 8**, exactly the acceptance ElysiumExpr can never
  meet (NameError). In-engine, all CPython C++ TUs **compile**; the full editor link + in-editor run is
  blocked only by the parallel **P8 glTFRuntime** WIP (plugin not yet installed). **Remainder = 9.3:**
  replace the Python native-stubs with the real C `vampire` bindings (54 methods), auto-load the map's
  `worldspawn.levelscript` at map load, and pin field-6 ↔ level-script name resolution.

- **2026-07-22** — 4.5 landed (tutorial logic/point/brush + trigger classes). **Grounded in the
  decompile, not guessed:** `tools/ghidra/run.ps1 -Script DumpGrep` against the analyzed `vampire.dll`
  recovered every class factory + datamap by classname/field-string anchor. **The one real VtMB
  divergence is `logic_case_toggle`** (`FUN_101344f0` / core `FUN_101346e0`): its `InValue` is a
  *delta* that advances a current-case pointer that many **configured** cases (skipping empty slots,
  wrapping 0..15) and fires the landed case — not stock `logic_case`'s value-string match (whose class
  is 4 bytes smaller, lacking the current-index int). This is why the tutorial wires
  `math_counter.OutValue → logic_case_toggle.InValue`: the counter value is the advance amount. Both
  are implemented (shared Case base); the value flows through a new Source-`COutput<T>` seam
  (`FireOutput(name, activator, value)` fills an empty map-param, else the authored param wins).
  `math_counter`/`logic_timer` confirmed **stock** (present, unmodified). `func_brush` is nearly a
  base entity (`FUN_1013dd30` — trivial ctor); its Solidity/Enable/Disable fold into the body's single
  `SetDormant` switch so they don't fight dormancy. `env_fade` renders on one screen-fade state held on
  the entity world and polled by `AElysiumHUD`. The player is reached from the plain-C++ substrate via
  a new `FElysiumEntityWorld::GetPlayerPawn()` seam (point_teleport, trigger_hurt). Debug: `GetDebugState`
  on every class + a dedicated `Elysium.Logic` Cog window. **RE4 (Ghidra datamap export)** is now the
  proven, low-cost method for any future class pass — DumpGrep on the persisted project, no re-import.
  `trigger_stealth_mod`/`trigger_inventory_check`/`trigger_environmental_audio` left as inert records
  (their stealth/inventory/RoomDSP systems don't exist yet).
- **2026-07-22** — 5.3 landed (native bindings). **Grounded in the exported field-6, not the table
  count:** across all 10 exported maps only `FindPlayer()` (7×) + Character methods off it
  (`RemoveItem`/`SewerMap`/`GiveItem`/`ClearActiveDisciplines`) are native; the heavy callees
  (`spawnCopCar`, `resetHos`, the `cXxx` discipline constants, the level's `On*` callbacks) are all
  **level-script** names → 5.5, correctly still NameError, so the tutorial's `G.Discflags |= cCelerity`
  keeps no-opping. **Zero field-6 references `self`/`activator`** — they are I/O *targets*, not Python
  names — so those bind from the eval context but sit **below** the module globals as a harmless
  fallback. **Stub policy = log-only (roadmap default):** only the two systems that already exist wire
  for real (`SetQuest`/`GetQuestState` → the quest map); the other 9 globals + 22 methods log a stub +
  a sensible default (predicate globals read false). `ScheduleTask` (deferred source) and `ChangeMap`
  (travel) stay stubs to respect their phase owners (5.4 / P4). **Forgiving dispatch:** any attribute
  off a `Character` object binds (unlisted names too), so a call outside the known-24 runs to the
  generic stub instead of raising — matching retail's `__getattr__` fall-through and the slice
  acceptance (`FindPlayer().ClearActiveDisciplines()` runs). NPC handles accept Character methods too.
  One static `GNativeBindings` table drives both membership and the debug view. **Precedence caveat for
  5.5:** `OneOfSet` is both a native global and a `vamputil.py` helper exec'd into `__main__` (the
  latter wins in retail); when level-script names resolve, the level-script definition must shadow the
  native one. Debug: the `Elysium.Scripting` window gains a Native-bindings table (kind/status/live
  call-count) + a Recent-native-calls log, fed by `UElysiumGameStateSubsystem`'s native-call ring.
- **2026-07-22** — 5.2 landed (expression evaluator). **One evaluator, no second dispatch:**
  `ElysiumExpr` (lexer + recursive-descent AST parser + tree-walk) is exception-free — every error
  collapses to Void (error-to-false, RE3). **Scope beyond the literal task line** (calls / attr /
  literals / compare / and-or): added assignment statements + the full `+ - * / % | & ^ << >> **`
  operator set + `None`, because the tutorial's real field-6 is 60% `G.<flag> = <expr>` (46 of 77,
  the only operator being `|` for the `Tutorial_Discflags` accumulation) and the P5 slice needs
  flags to flip. Python-2 semantics (floor int div/mod, `and`/`or` return an operand, chained
  comparisons). **`ent.Input()` dispatches through the existing chokepoints** (`EnqueueInput
  "!self"` targeting the one handle) rather than a new synchronous path — visible in the queue
  window, single-steppable, serializable. **Live field-6 is opt-in** (`elysium.script.live`,
  default off; the null host stays the map-load default) so 5.2 changes no map-load behaviour; 5.4
  makes it the default and adds logic_pythoncheck + ScheduleTask. Bare names + native globals
  (FindPlayer, `cCelerity`, DialogPostProcess) are NameErrors until 5.3/5.5, so `G.x = G.x |
  cCelerity` correctly no-ops for now while plain `G.x = 1` flips visibly. Debug surface: live `G`
  table + eval/exec box + recent-eval log in the `Elysium.Scripting` Cog window, echoed by
  `elysium.eval` / `elysium.exec`.
- **2026-07-22** — 1.6 landed (starter classes). **`logic_auto` owns map-load ignition** via a
  one-shot think (first world tick), replacing the generic `FElysiumEntityWorld::FireMapLoadOutputs`
  bootstrap (removed) — matches retail (logic_auto fires on the first server think, after every
  target has spawned) and exercises the think path. **`CBaseTrigger` is a registry-only chain node**
  (no entity carries that classname) so `trigger_multiple`/`trigger_once` share Enable/Disable/Toggle
  + StartDisabled/wait through one base; this is the pattern the P4.5 trigger family extends.
  **Trigger activation filter reduces to the `ALLOW_CLIENTS` (0x1) bit** in P1.6 because the only
  toucher is the player and its activator is unresolved (the pawn is not an entity until P4);
  empirically the tutorial's triggers carry 0x1 (43/44 `trigger_multiple`, all `trigger_once`), and
  the one `0x8` physics-only trigger *should* ignore the player — so the reduction is faithful, not a
  shortcut. **Remove-on-fire / fast-retrigger spawnflags left unmodelled** for `logic_relay`/
  `logic_auto`: those bits are unconfirmed for VtMB (RE1 only covered button + trigger flags, and
  buttons diverge from stock), and firing `Kill()` on a wrong bit is a worse failure than a spurious
  re-fire, which per-output `times` already bounds. Revisit if a tutorial relay over-fires.
- **2026-07-22** — 1.1 landed. Currency types are plain C++ structs (R1, no reflection):
  `FElysiumVariant` carries the seven runtime categories with total, never-throwing
  coercions (a Void variant is the falsy / error-to-false case). `FElysiumEntityHandle` is
  the value only — `IsSet()` is structural; true falsy-when-dead/stale is decided by
  `FElysiumEntityWorld::Resolve` (1.4), not here. **The `G` store and quest map key
  case-sensitively** (custom `KeyFuncs` + `FCrc::StrCrc32`) because they mirror Python dicts;
  UE's default `FString`/`FName` maps are case-insensitive, which would silently merge
  distinct flags. `G` is variant-valued, default-0-on-miss, and assigning Void deletes the
  key (decompiled `tp_getattr`/`tp_setattr`, `python_bridge.md`). Clock + `G` + quests live on
  the GI subsystem so they survive travel.
- **2026-07-22** — 0.5 Cog spike closed: vendored the **main Cog plugin only** (upstream `cb1b435`
  on `main`, MIT) into `Plugins/Cog/`, minimal deps, `UElysiumCogSubsystem` registering stock
  CogEngine windows. Builds + runs on UE 5.8 with **no source patches** (the flagged ImPlot
  `INFINITY` MSVC error did not reproduce on VS 14.50). Build products gitignored; provenance
  (base commit) recorded here so future updates re-base off `main@cb1b435`.
- **2026-07** — Cog (MIT, vendored, dev-only) adopted as the debug UI shell after tooling
  research; Slate console superseded. (`debug-tooling.md`)
- **2026-07** — Entity object model adopted: plain-C++ entities with optional Unreal bodies;
  one name table per class (I/O + Python + keyvalues + saves + inspector); generation-checked
  handles on stable `.ents` indices; one clock + one queue, no `FTimerManager`; two
  instrumented chokepoints; dormancy as one switch. (`engine-core.md`)
- **2026-07** — Queue-serviced-before-thinks tick order chosen (retail order unknown, RE2).
- **2026-07-22** — RE2 resolved: **retail is think-first** — `Physics_RunThinkFunctions` then
  `CEventQueue::ServiceEvents` in the `vampire.dll` server frame (addresses in
  `roadmap-archive.md` → "Ghidra extraction", RE2 note).
  The provisional queue-first choice diverges from retail. Decision for task 1.4: match retail
  (think-first) unless save-determinism argues for queue-first; `engine-core.md` Tick note now
  documents think-first.
- **2026-07-22** — 1.4 tick-order resolved and shipped: **think-first**. No save/load exists yet
  (M6/P9), so no determinism argument for queue-first; `FElysiumEntityWorld::Tick` runs
  `RunThinks(now)` then `ServiceEvents(now)`, matching retail. Save-determinism can revisit at P9
  if needed (the queue and think times both serialize regardless of service order).
- **2026-07** — Debug-substrate-before-M3 sequencing chosen ("foundation now"): P1 → P2 → P4.
- **Standing (from strategy)** — fully dynamic lighting committed (HWRT Lumen + MegaLights +
  VSM, DX12/SM6 mandatory; no baked GI — lump-8 bake parked as low-end contingency);
  no game content in `.uasset`s ever (extend `.emc`-style caches instead); variable timestep
  matching retail (no fixed tick); Unreal-native substitutions where they beat porting
  (Chaos physics props, NavMesh+BT AI, Single Layer Water, deferred decals, Cable ropes,
  native audio submixes); Nanite not applicable; bring-your-own-game legal posture.
- **2026-07-22** — Web-research de-risk pass over the open questions:
  **MegaLights is Production-Ready in UE 5.8** and the per-light control is the
  "MegaLights Shadow Method" property (3.1 is a straightforward property set); MegaLights
  does not light water and Single Layer Water gets forced-mirror Lumen reflections
  (accepted for VtMB water — noted on 7.3). **Audio libraries pinned:** `dr_wav` for
  MS-ADPCM/IMA-ADPCM (6.1), `dr_mp3`/`minimp3` for MP3 (6.2) — all public domain,
  single-header, MP3 patents expired. **CPython 2.x embed fallback confirmed viable**
  (maintained 2.7.18 forks build with VS2019+) — 5.5 decision itself stays pending on the
  script survey. **Cog** (UE 5.5+, active) and **glTFRuntime** (UE 5.7, active) both look
  healthy but lack explicit 5.8 confirmation — the 0.5 and 8.2 spikes stand. **Lumen Lite**
  (new 5.8 medium-quality GI) recorded as an Options entry only — the HWRT-Lumen commitment
  is unchanged; revisit trigger: 10.3 floor validation fails or a sub-DXR audience matters.
- **2026-07-22** — Ghidra extraction pass over the RE-drivable open items (`vampire.dll`; dumps in
  `tools/ghidra/out/re*.txt`). **RE3 confirmed** two load-bearing assumptions with recovered logic:
  `Entity.__setattr__` (`FUN_10195a10`) writes through the same datamap walk as `__getattr__`, falls
  through to the instance `__dict__` on unknown names, holds `_entity_ptr_` read-only, gates writes
  on record flag bit `0x8`, and marshals per VtMB's shifted `fieldtype_t`; **error-to-false** is real —
  `logic_pythoncheck` (`FUN_10135290`) evals with `Py_eval_input`, `PyErr_Print`s on a raise, and
  returns false, and the exec/field-6 paths (`FUN_100ce8a0`/`FUN_100ce990`) swallow errors likewise.
  **RE1 partially** recovered (`CBaseButton` layout/vftable `0x1045293c`, use-icon offsets), spawnflag
  bit→behavior map still pending the Use/Touch handlers. **RE4** (datamap JSON export) and **RE3 `G`
  default-0** scoped with exact seeds — see `roadmap-archive.md` → "Ghidra extraction —
  findings + plan".
- **2026-07-22 (cont.)** — Ghidra pass continued. **RE4 method confirmed**: VtMB datamaps are
  runtime-built, so the export decompiles per-class **datamap builders** (`CBaseEntity` =
  `FUN_100a22f0`), not a static `.data` walk; the CBaseEntity base keyfield/input/output contract is
  extracted and recorded in `python_bridge.md` (RE4 → `[~]`). **RE1** (button spawnflag bits) and
  **`G` default-0** both hit the same wall — their target functions are `m_pfn*`/vtable/immediate-
  reached and carry no references in Ghidra's DB, so xref discovery stalls; unblock via an
  `EnableAIF` re-import or direct PE method-table/vtable parsing (recorded as the "shared blocker").
- **2026-07-22 (cont. 2)** — `EnableAIF` re-import of `vampire.dll` ran and unblocked both stalled
  items. **`G` default-0 CONFIRMED** → RE3 fully closed (`[x]`): G is `PyDataManager`; `tp_getattr`
  `0x1019b3d0` returns `PyInt_FromLong(0)` on a flag miss. **RE1 `CBaseButton::Spawn` map recovered**
  (`m_spawnflags`@`+0x204`; `FUN_100c8d60`) — bits `0x1`/`0x40`/`0x100`/`0x400`/`0x800`/`0x1000`
  decoded into `entity_io.md`; bits `0x20`/`0x2000` + `trigger_multiple` filter still to do (RE1 stays
  `[~]`). Findings live in `python_bridge.md` (G) + `entity_io.md` (button); the AIF-analyzed DB is now
  the project baseline.
- **2026-07-22 (cont. 3)** — **0.4 sidecar space audit done.** Traced all five downstream-consumed
  sidecars through `UE_bsp_to_scene.py`: `.ents`, `.sprites`, `.spawn`, `.water`, `_decals.obj` are
  **already emitted in Unreal cm** (every one routes through `source_to_unreal`/`INCH_TO_CM`, decals
  with winding reversed) — the exporter had been fully migrated and the audit found **no code
  straggler**. The `rebuild-strategy.md` contract table was stale (`.sprites` "Godot metres", `.spawn`
  "Source coords"); corrected, and space made explicit on `.ents`/`.water` too. **PL7 is empty.**
  Runtime `.spawn` reader confirmed verbatim.
- **2026-07-22** — Asset-enhancement direction accepted (design; not scheduled). Stance:
  **faithful baseline + opt-in, code-driven remaster layer** that preserves VtMB's grimy
  gothic-punk art direction, always as an `elysium.EnhancedTextures` A/B toggle. Adjudication
  test: serve the art direction / fix a technical deficit that fights the dynamic relight → in;
  invent or override an artist decision → out. Sequence is dictated by the dynamic relight —
  **delight albedo first** (recover true base color from 2004 painted-in shading), then
  super-resolve, then derive normal/roughness/AO from the *delit* albedo, metallic by hand-mask
  only. Tier 0 (delight + upscale) is a real deficit fix; Tier 1 (PBR synthesis) is
  style-anchored, curated per material family, budget-gated by the 3060/12 GB floor. Scaffolding
  already exists (`upscale_bench.py`/`sky_upscale.py`/`retex_dds.py`); runtime hooks are
  `M_VtMB_World`'s planned normal/envmask slots. Written up in `asset-enhancement.md`; strategy
  principle 6a; Options + risk-register entries added. Web-research pass confirmed the technique
  set (AI PBR-from-diffuse, de-lighting tools, ESRGAN game-remaster practice) and their caveats
  (guesswork needing curation; delighters tuned for photoscans, not hand-painted art).
- **2026-07-24 (cont.)** — **NPC skeletal crash fixed** (map-switch GC fault in
  `~FDuplicatedVerticesBuffer` → `FRHIResource::Release` on a garbage RHI pointer). Root cause: a few
  VtMB `.mdl` skeletons carry **more than one parent-less bone** (`regular_cop` bones 0/1, `prophet`
  bones 0/59), but `mdl_gltf._assemble_skinned` emitted only the first as the glTF scene +
  `skin.skeleton` root. glTFRuntime builds its bone map by traversing from that single root down
  `children`, so bones under any other root are absent from the map; a vertex weighted to one makes
  `FillSkeletalMeshRenderData` bail mid-loop, and because it `SetNumUninitialized`s the render-section
  array up front, the aborted build leaves a half-built `USkeletalMesh` whose trailing
  `FSkelMeshRenderSection`s are never constructed — the next GC (a map switch) faults destructing them.
  Three-layer fix: **(1) export** — `_assemble_skinned` unifies multiple roots under one synthetic
  `__elysium_skeleton_root` node, appended after the mesh node so node-index==bone-index holds and left
  out of `skin.joints` so `JOINTS_0` still maps 1:1; re-export drops the whole NPC set to 0/45 multi-root.
  **(2) runtime** — `ElysiumNpcVisual::LoadMesh` sets `bIgnoreMissingBones=true`, so any future unmapped
  bone degrades (weight rebinds to the next valid bone) instead of aborting the mesh. **(3) vendored
  glTFRuntime** — `FillSkeletalMeshRenderData` constructs all render sections up front, so any early
  return is crash-safe. `regular_cop`/`prophet` re-exported; the other 43 were already single-root. **8.2
  glTFRuntime skeletal path confirmed.**
- **2026-07-24** — **Ropes render on the stock `CableComponent` plugin (roadmap 8.7).** New
  dependency accepted: `UCableComponent` (the roadmap's stated target) over an offline-catenary
  `UProceduralMesh` alternative. Rationale: `CableComponent` is a **first-party, enabled-by-default,
  non-beta** UE 5.8 plugin (read from the installed `CableComponent.uplugin`: `IsBetaVersion:false`,
  `EnabledByDefault:true`, already-compiled `UnrealEditor-CableComponent.dll`) — low-risk, and its
  Verlet strand hangs into a catenary on its own from `CableLength = span + slack`, so no offline sag
  math. Enabled in `ElysiumUE.uproject` + `Build.cs`. Accepted trade-off: cables **simulate each tick**
  (settle from straight over ~1 s; trivial CPU cost at 70 cables), vs the alternative's static
  deterministic geometry. Built as a pure-visual `.ropes` sidecar → `AElysiumMapActor::BuildRopes`
  (the 7.2-decal pattern), **not** the entity substrate — a cable spans two entities and rope nodes
  have no meaningful I/O, so they stay inert `.ents` records. **RE finding recorded:**
  `entity_visuals.md` R3 had the node roles backwards — from `sp_tutorial_1`, **`move_rope` is the
  chain start** (31 nodes = the 31 topological starts), `keyframe_rope` the continuations (76);
  resolution is topological (start = untargeted by any `NextKey`), so it is classname-independent.
  Doc corrected in place. **`NextKey` resolution — grounded in the decompile, not guessed:**
  `sp_tutorial_1` reuses the rope names `tele4..tele9` across **two separate wire installations ~200 m
  apart** (both in the playable world — the `sky_camera` PVS classifier confirms neither sits in the
  3D skybox), and a last-wins name index cross-linked them into six ~199 m cables slashing across the
  map. RE (vampire.dll via Ghidra): `keyframe_rope`/`move_rope` are stock Source **`CRopeKeyframe`**,
  and `Activate` resolves `NextKey` with **`FindEntityByName(NULL, m_iNextLinkName)` = the first entity
  of that name in spawn/entity order** (= the entity-lump order the exporter iterates), so the exporter
  uses **first-wins**. First-wins and a nearest-position heuristic give the identical 70 segments on the
  tutorial (each installation is lump-contiguous); first-wins is the faithful engine rule. Also drops
  coincident-endpoint (< 1 cm) links — zero-length chain artifacts.
- **2026-07-25** — **Rope shape is driven by `Type`, not `Subdiv` (roadmap 8.7 follow-up).** A
  full audit of the rope pipeline, export → in-game placement, after the cables still read wrong.
  **Placement is not the defect and never was:** re-derived every rope node straight from the
  entity lump on all seven exported maps and the emitted endpoints match the BSP 1:1; first-wins
  `NextKey` binding and a nearest-position heuristic pick the **same** target for every emitted
  segment on every map (`differs = 0`), so the 2026-07-24 first-wins fix holds. The 3D-skybox
  hypothesis is also cleared: a sweep of all 108 maps finds **no** rope node inside any
  `sky_camera` room, so no rope needs the `world(v) = scale · (v − origin)` miniature transform
  that `SkyMesh` carries — the `.ropes` path correctly bypasses it. The remaining apparent
  "broken links" are **map-data typos** (`chop06 → chop7`, `tele8 → tele9`, …): 122 dangling
  `NextKey` names across the 108 maps, which the engine also warns about and draws nothing for.
  The exporter now logs them instead of dropping them silently, since silent drops are
  indistinguishable from a linking bug.
  **What was actually wrong — RE'd in `vampire.dll` (`CRopeKeyframe`, addresses in
  `entity_visuals.md` R3):** the simulated node count `m_nSegments` comes from the **`Type`**
  keyvalue (`KeyValue` `0x1019f2b0`: 0 → 10, 1 → 4, else → 2; `Activate` `0x1019e310` clamps to
  `[2, 10]`), **not** from `Subdiv` — `Subdiv` is client-side render tessellation between physics
  nodes, capped by client.dll's `rope_subdiv`. The runtime had been inventing
  `NumSegments = clamp(Subdiv × 3, 4, 16)`, which both used the wrong field and exceeded VtMB's
  hard max of 10. The consequence is structural, not cosmetic: **677 of the game's 2,688 rope
  nodes (25%) are `Type 2` = two nodes = one span between two locked points, which cannot sag at
  all** — the observatory's lift cables, the hanging-lamp and crucifix chains. We were giving them
  nine Verlet spans and gravity; a 48.8 m `Type 2` cable was drooping ~5 m. `Dangling` was dropped
  too (`KeyValue` clears `ROPE_LOCK_END_POINT`, so the far end must swing free — 51 nodes
  game-wide), as were `RopeShader` (overrides `RopeMaterial`), `Collide`, `Barbed`, `Breakable`.
  Sidecar widened to 12 tokens (`nodes` replaces `subdiv`, `flags` added — `UCableComponent` has no
  separate render tessellation, so `Subdiv` has nowhere to go); runtime sets
  `NumSegments = nodes − 1` and `bAttachEnd = !Dangling`, and raises `SolverIterations` 2 → 8 so
  the shape settles on its catenary rather than short of it.
  **`CableLength = span + slack` is confirmed, not assumed:** `RecalculateLength` (`0x1019e5d0`)
  sets `m_RopeLength = (int)|B − A|` and `RopeThink` (`0x1019efb0`) sets
  `m_RopeLength = (int)|B − A| + m_Slack`, with both endpoints locked by ctor default
  (`m_fLockedPoints = 3`). **Left faithful, flagged as the open question:** that rest length forces
  a deep catenary — replaying UE's exact Verlet solve over the real data gives a median **4.0 m**
  sag on `sm_hub_1`'s 25–28 m street wires (`Slack 80` = 2.03 m of surplus over a ~26 m span), and
  it converges on the analytic catenary, so it is the authored slack talking, not solver error.
  That is the dominant remaining visual difference and it is **not** yet A/B'd against the running
  original; if it turns out too deep, the divergence needs its own dated entry here.
  **Also fixed:** `write_ropes` parsed origins with a bare `float()`, so `hw_jewelry_1`'s
  comma-decimal chandelier origins (`"-3496,92 -3147,1 140"`) raised `ValueError` and took that
  map's whole export down. Now parsed with C `atof` semantics, matching what the engine reads.
- **2026-07-25 (cont.)** — **The rope misplacement itself was a runtime bug: `UCableComponent`'s
  unset `AttachEndTo` resolves to the owner's ROOT component, not the cable.** In-game verification
  of the morning's `Type`/flags fix (MCP screenshots against the rope-node gizmos) showed every
  cable still wrong the same way the user reported: starts pinned correctly on the pole insulators,
  far ends all converging into the chophouse — the room that happens to sit next to the world
  origin. Root cause read from engine source (`EngineTypes.cpp`,
  `FBaseComponentReference::ExtractComponent`): with no `OtherActor`, no `ComponentProperty`, and no
  `PathToComponent`, `AttachEndTo.GetComponent(GetOwner())` returns **`GetRootComponent()`** — it is
  never null, so `GetEndPositions`' `EndComponent = this` fallback is dead code. Our cables hang off
  the map actor's `SceneRoot` (identity, world origin), so `EndLocation = B − A` was read as the
  **absolute world point `B − A`**: every one of the 70 cables' far ends landed within ~20 m of the
  origin, hundreds of metres from its pole ("connecting across the map"), and no cable ever reached
  its next node ("close ropes don't connect"). The stock `CableActor` never trips this because its
  cable IS the root component, making the fallback self-referential and accidentally correct — which
  is why the standard usage pattern hid the bug. Fix: `EndLocation = B` (SceneRoot space **is**
  world space; one line, `BuildRopes`). Verified in-game at three sites: the chophouse chains hang
  dead vertical, the spawn-street wires sag pole-to-pole, the alley network threads its node chain.
  The 2026-07-25 entry's export-side verdict stands: the sidecar endpoints were correct all along —
  the earlier in-game symptom was this runtime bug, not the chain resolution.
- **2026-07-25 (cont. 2)** — **Correction: rope rest length is not `span + Slack`. Half the
  computation lives in `client.dll`, and it shortens every rope by 100 units.** The two entries
  above assert `CableLength = span + slack` "confirmed, not assumed" from the server half alone,
  and flag the resulting deep catenary as the open question. Both are wrong, and the flagged
  question is now answered by RE rather than by an A/B. `C_RopeKeyframe::RecomputeSprings`
  (client.dll `0x100bf1a0`, reached from the shared `m_Slack`/`m_RopeLength` RecvProxy `0x100be290`)
  computes `springDist = (m_RopeLength + m_Slack − 100) / (nodes − 1)`, and
  `CBaseRopePhysics::ResetSpringLength` (`0x10128ae0`) floors it at 0. Three consequences, none
  visible from the server side or the FGD: **`Slack` is applied twice** (once server-side into
  `m_RopeLength` by `RopeThink`, again here), a flat **−100 units** is subtracted (`LEA EAX,[EAX +
  EDX*0x1 + -0x64]`), and the divide is an **integer** one (`CDQ`/`IDIV`), so the per-segment
  length truncates. Rest length is therefore `springDist × (nodes − 1)` ≈ `(int)|B − A| + 2·Slack −
  100`, not `span + Slack`.
  The −100 dominates at VtMB's scale: authored `Slack` runs 0..100 across the whole game, so **most
  ropes come out at or below their straight span and hang taut**, and the `max(0, …)` floor lets a
  short one collapse to a dead-straight chord. On `sp_tutorial_1` that reclassifies 26 of 70 cables
  from sagging to taut and cuts the chophouse meat-hook links from 2.6× their span to 1.5×; the
  Santa Monica street wires drop from ~12% surplus to ~9%, a visible droop rather than a swag.
  Sag was never a solver artefact and is genuinely simulated: `C_RopeKeyframe::Init` (`0x100c04d0`)
  lerps the nodes along the straight chord, then runs `RunRopeSimulation(5.0f)` (`0x100bf360`)
  because the ctor's `m_RopeFlags = 0x48` sets bit `0x40`.
  **The rest length is resolved offline, in the exporter**, and the sidecar's column 9 changes
  meaning from `slack_cm` to `rest_cm` (still 12 tokens). The arithmetic is integral and in Source
  units, which is where the exporter already works; putting it at runtime would mean converting cm
  back to inches to reproduce a truncation. `BuildRopes` now assigns `CableLength = RestCm`
  verbatim, and `RestCm < |B − A|` is the normal case, not an error.
  **Also corrected — the exporter's keyvalue defaults, read from the ctor (`0x1019dc80`) instead of
  guessed:** `Slack` 0 (was 25), `TextureScale` 4 and datamap-clamped to `[0.1, 10]` (was 1), and a
  rope with no `Type` key at all keeps `m_nSegments` 5 (was being treated as `Type 0` → 10).
  Verified in-game by MCP screenshot: the chophouse chains hang vertical with a short loop at the
  hook instead of a deep V, and the spawn-street wires droop gently from the pole.
  **Still not A/B'd against the running original** — this is the engine's own arithmetic reproduced
  exactly, so it needs no divergence entry, but nobody has put the two side by side.
- **2026-07-25 (cont. 3)** — **The hardware floor rises to an RTX 4060-class / 16 GB GPU at 1440p
  native, and the shipped render tier becomes stock Epic scalability.** Owner call. The previous
  config targeted an RTX 3060 at 1080p and paid for it with settings that were compensation, not
  tuning: `r.ScreenPercentage=66` plus a `r.Tonemapper.Sharpen=0.5` pass to claw back what the
  upscale lost, `ScreenProbeGather.TracingOctahedronResolution=2` (against 8 at *both* High and
  Epic in UE 5.8's `BaseScalability.ini` — below every shipped bucket), `DownsampleFactor=32`
  (Medium), quarter-res reflections, and `IntegrateDownsampleFactor=2`, which Epic documents as
  enabled by *no* default scalability or device preset because it "can cause extra noise and soften
  normals".
  Three of those existed only to serve constraints the `.uasset` bake removed. **Hit lighting**
  (`r.Lumen.Reflections.HardwareRayTracing.HitLighting=1`) was the fix for a *totally empty* surface
  cache — every reflection ray read back black against runtime-built `UProceduralMeshComponent`
  geometry that could never hold cards. The baked level has full DDC-fitted card coverage, and Epic's
  own guidance is "we don't recommend using it for games". The **VSM directional LOD bias** (`1.0`)
  worked around the non-Nanite marking-job-queue overflow caused by huge single-section PMC world
  surfaces; the world is Nanite now, and a full profile run logs **zero** overflows with the bias
  gone. `r.Lumen.HardwareRayTracing.MaxTraceDistance` was never a real cvar — it does not appear
  anywhere in the 5.8 renderer source, so the line did nothing.
  The tier is now Epic (3) across all eleven `sg.` groups rather than a hand-rolled cvar set,
  following Epic's guidance that the stock buckets are tuned to hold indirect lighting consistent as
  they scale; only three deviations remain (HWRT scene culling, 16x anisotropy, a 3 GB streaming
  pool). `[SystemSettings]` stays the home for it because `ECVF_SetBySystemSettingsIni` (0x05)
  outranks `ECVF_SetByScalability` (0x02) and `ECVF_SetByGameSetting` (0x03), so neither the
  first-run hardware benchmark nor a future settings menu can pull the tier down.
  Measured on `sp_tutorial_1`, RTX 5070 Ti, 2560x1440 **native**: total GPU 5.08 / 5.86 / 5.89 /
  6.18 / 6.11 ms across the five vantages, against 4.87 / 5.25 / 5.32 / 5.72 / 5.43 for the old
  config at 66% of the same output — **+0.2 to +0.7 ms for 2.3x the pixels and Medium→Epic Lumen.**
  The 4060 figure is extrapolated from that card, not measured; validating the floor still needs
  4060-class hardware.
- **Pending** — 5.5 level-script execution strategy (interpreter vs transpile vs CPython);
  10.6 EnhancedInput migrate-or-remove.
