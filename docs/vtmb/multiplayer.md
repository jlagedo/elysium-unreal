# VtMB multiplayer — the cut "CounterBite" mode and what ships of it

VtMB was built with a team multiplayer mode — vampires against Society of Leopold hunters — that
was cut before release. The engine's networking ships intact, and so do fragments of the mode's
data and code; the maps and the round rules do not. This document records what the install
carries, what the developers have said about the design, and the fan restorations. It owns no
runtime behaviour: retail programs only ever observe `IsMultiplayer() == false`, and the port is
single-player (see *Port position*).

Evidence tags: **[VtMB]** = read from the install's own DLLs; **[data]** = read from retail VPK
text; **[UP]** = Unofficial Patch files or readme; **[web]** = developer or community statements,
cited with source and date. Retail `vampire.dll` SHA-256
`c546f4de2003624d72f54d03805e0dbe1d8157231adcc62368ff53fe6e48a76f`, `client.dll`
`e88beae0dd03af06493c71c5e8d87a6993b54e590cb6ad37cd3513c588582870`.

## 1. The design, as the developers describe it [web]

- **CounterBite.** Tim Cain's name for it, in two videos on his *Cain on Games* channel —
  "Vampire Bloodlines – Cut Multiplayer Content" (May 2023) and "CounterBite: Bloodlines
  Multiplayer Design" (April 2025). Modelled on Counter-Strike: a vampire team against a hunter
  team, where the bomb is **an ancient vampire in torpor** that the vampires try to release and the
  hunters to destroy.
- Vampires bring Disciplines; hunters bring better weapons and faith powers of their own (Numina).
  DualShockers' write-up of the 2025 video (12 April 2025) says the design had **nine "teams"**,
  starting with the Camarilla; the full list was not retrieved.
- Cain describes the mode as well balanced with good maps. **Activision judged Troika overloaded
  and handed the multiplayer to another company**; it never shipped.
- Pre-release marketing text (quoted on the Valve Developer Community page and Terra-Arcanum):
  "team-based battle of Kindred versus Kine"; players pick a side, powers and weapons; a Society of
  Leopold player is "outfitted with … stake guns, rifles and machine guns" plus Numina; each team
  has objectives the other opposes.
- Leonard Boyarsky, GameBanshee interview, 19 August 2004: "We're still not discussing
  multiplayer yet."

## 2. What the retail install carries

### Character data [data] [VtMB]

`vdata/system/clandoc000.txt` carries a `"Multiplayer" "1"` key on **ten** records: the seven
playable clans and three hunter templates.

| Record `Name` | `Clan` | Note |
|---|---|---|
| `Condotierre` | `Condotierre` | military order; firearms, weak Numina |
| `Inquisitor` | `Leopold2` | holy crusader; strong Numina, melee |
| `Society of Leopold #3` | `Leopold3` | placeholder — "A normal human being." |

The hunter descriptions are written in Numina terms. The record reader is `vampire.dll`
`FUN_101d4520` (mirror `client.dll FUN_1013ec60`): it handles the `"multiplayer"` key against the
record's `+0x9a` byte, and the `"Numina"` key alongside it.

### Game code [VtMB]

- **Rules.** `InstallGameRules` `0x10352f70` execs `game.cfg`, creates the `player_manager`
  entity, and returns a `CHalfLife2` (`0x10355740`) only when the byte at `gpGlobals + 0x30` is
  zero — otherwise `NULL`. Which `CGlobalVars` field `+0x30` is has not been pinned.
  `CHalfLife2::IsMultiplayer` (vftable `0x104a308c` slot 21, `0x101abcc0`) is `return 0`
  (`camera-view-modes.md`). **No multiplayer rules class ships.**
- **Scoring — `DT_PlayerResource`**, server builder `0x10189c00`, client receive
  `client.dll 0x100bcf50`. Beside stock `m_iPing`, `m_iPacketloss`, `m_iScore`, `m_iDeaths`,
  `m_bConnected` and `m_bSpectating`, it networks six VtMB arrays of 33 (one per player slot):

  | Field | `player_manager` offset |
  |---|---|
  | `m_iVScore` | `0x5dc` |
  | `m_iVDmgReward` | `0x660` |
  | `m_iVTeamWin` | `0x6e4` |
  | `m_iVTeamLoss` | `0x768` |
  | `m_iVRoundGoal` | `0x7ec` |
  | `m_iVSubGoal` | `0x870` |

  `m_iVRoundGoal`/`m_iVSubGoal` are the only shipped trace of round objectives. Their writers are
  **not recovered**.
- **Numina** runs through the Discipline machinery: the console commands at `0x100d87c0` and
  `0x100d97b0` activate "a given Vampire Discipline or Kine Numina"; `client.dll` has a
  `NuminaGroup` panel (`0x1017dc20`, `0x1017f470`).
- **Side selection in chargen**: `chooseteam` (`client.dll 0x101736c0`), `hideteam`
  (`0x10173780`), and the `vclan teamchoose done` event fired by `CharEditCharPanel`,
  `CharEditStatsPanel` and `CharEditEquipPanel`. `game_runtime.md` records `chooseteam` giving
  `giftxp 9000` and the multiplayer templates.
- **Stock Source team plumbing**: `mp_teamplay` `0x10117a40`, `mp_teamlist`, `mp_teamoverride`,
  `mp_defaultteam`, `say_team` `0x100d1650`, the `TeamChange` event, `m_szTeamName`
  (`CBasePlayer::Dump`), and the `info_player_coop` / `info_player_deathmatch` spawn classes.
- **Dead multiplayer arms**: death cam, observer mode and `AddPoints`/`AddPointsToTeam` (empty
  bodies) — `camera-view-modes.md`, `npc-kernel/delete-list.md`; the multiplayer step-sound
  predicate `0x1011e3c0` (`footsteps.md`).

### Assets and UI [data]

- `models/character/pc/male/average_vampire_hunter/average_vampire_hunter_pc.mdl` — a hunter body
  built as a **player** model — and first-person hands under `models/hands/{male,female}/hunter`.
  The `society_of_leopold` NPC bodies also serve the single-player monastery, so they prove nothing.
- `GameUI.dll`'s Source multiplayer dialogs (`CCreateMultiplayerGameDialog` and its server and
  gameplay pages, `CMultiplayerAdvancedDialog`, `CMultiplayerCustomizeDialog`) with their `.res`
  files; `client.dll`'s `BTN_MULTIPLAYER`. The main-menu entry is commented out in
  `scripts/dialog_main` and `scripts/launcher.txt`, and `scripts/dialog_multiplayer` still carries
  its Valve template header `"tf2/scripts/dialog_multiplayer"` (`m0_menu_build.md` §8,
  `vtmb-ui.md`).
- `scripts/liblist.gam` names `mpentity "info_player_deathmatch"`; `scripts/settings.scr` carries
  the stock `mp_teamplay` option.
- `CNewGameDialog` starts every game with `maxplayers 1` / `deathmatch 0`.

### What is absent

- **Maps** — none of the 108 map names is a multiplayer map, and the map-entity export has no
  `info_player_deathmatch` or `info_player_coop`.
- **Round logic** — no multiplayer game-rules class, no writer of the round-goal fields recovered,
  and no objective entity found (a torpor-objective search has not been run).

## 3. Community restorations [UP] [web]

- **Unofficial Patch (Wesp5).** Readme §"Multiplayer Support": ships `cfg/multiplayer.cfg`
  (`coop 1`, `deathmatch 1`, `maxplayers 20`, `sv_lan 1`, …) and restores the hunter templates as
  `mp-condotierre`, `mp-inquisitor` and `mp-mercenary`, each starting with 250 XP, $500 and unique
  equipment. The server runs `exec multiplayer.cfg`, loads a map (test map `hw_609_1`), must use
  `noclip` or it crashes, then sets `sv_gravity 800` once clients join; clients `connect`, pick
  `vclan mp-<clan>`, and can `team`, `say` and `say_team`. **Players can't damage each other, so
  only co-op is possible**, and the campaign maps aren't playable co-op. The same readme lists
  "Counter-Bite" under *Unimplemented Content*. The patch also adds `cm_clan_symbol_mercenary`, a
  hunter chargen top bar, hunter hands and per-hunter Thaumaturgy view models, and
  `kb_act - hunter.lst`. Players report the three hunter classes differ only in model and weapon
  and share three Numina — Mind Shield, Holy Sight, Divine Light (Steam thread, 13 June 2019).
- **VTMBMP – Multiplayer restoration** (Nexus Mods `vampirebloodlines/mods/316`, first upload
  23 June 2024): a working deathmatch ruleset, one custom map and a Godot-built launcher; requires
  UP Plus 11.4 on every machine. "Counter-Bite" was planned from Cain's description and not
  finished. **Development ended July 2025**; the author invites a successor. Feature list and known
  problems: Planet Vampire thread `planetvampire.freeforums.net/thread/168` (not retrieved).
- **VTMB Prelude** (Entenschreck, Wesp5) — a single-player hunter mod, not multiplayer.
- **Bloodlines Resurgence** (c. 2013) — a newer-Source port that promised deathmatch; no release.

## 4. Port position

The port is single-player and builds no replication. That is verbatim with retail's observable
contract: every shipped program sees `CHalfLife2` rules, `IsMultiplayer() == false` and
`maxplayers 1`, so the multiplayer arms stay deleted rather than stubbed. The hunter templates are
data the chargen parser reads; the chargen popup's hunter route is a named divergence recorded in
`game_runtime.md`.

## Unrecovered

- The `gpGlobals + 0x30` field that gates `InstallGameRules`.
- Writers of `m_iVScore`, `m_iVDmgReward`, `m_iVTeamWin`, `m_iVTeamLoss`, `m_iVRoundGoal`,
  `m_iVSubGoal`.
- Any torpor-objective entity or code.
- The full nine-team list from Cain's 2025 video.
