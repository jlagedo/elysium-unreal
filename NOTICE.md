# Notice

## No affiliation

Elysium-Unreal is an independent, non-commercial research project. It is not affiliated with,
authorized by, endorsed by, or sponsored by Paradox Interactive AB, White Wolf Publishing,
Activision Publishing Inc., Troika Games, Valve Corporation, or Epic Games Inc.

## Trademarks

*Vampire: The Masquerade*, *Bloodlines* and *World of Darkness* are trademarks of Paradox
Interactive AB. *Source* is a trademark of Valve Corporation. *Unreal* and *Unreal Engine* are
trademarks of Epic Games, Inc. All other trademarks are the property of their respective owners.
They appear here only to identify the software this project reads and interoperates with. That is
nominative use and implies no endorsement.

## No game content is distributed

This repository contains no assets, code or data from *Vampire: The Masquerade – Bloodlines* or
from the Source engine. No textures, models, animations, sounds, maps, scripts, dialogue, binaries
or decompiled source are present in the repository or anywhere in its history. Git is configured to
refuse game-derived output: see [`.gitignore`](.gitignore) and [`.gitattributes`](.gitattributes).

Running the project requires a legally obtained installation of the original game. Everything the
pipeline derives from that installation is written outside the repository, under your own work
root, and stays on your machine.

## How the original game is read

- `ELYSIUM_VTMB_ROOT` points at an installed copy. It is opened read-only and never written to.
- The pipeline parses the game's own shipped file formats from an already-installed game.
- No copy protection, DRM, licence check or other technological protection measure is bypassed,
  disabled or circumvented at any point. The retail game runs no anti-tamper layer over its data
  files, and this project neither contains nor requires any tool that would defeat one.
- No modified, cracked or DRM-stripped copy of the game is used, required or supported.

## Reverse engineering

`docs/vtmb/` and `research/` record observed and recovered *behavior* of the retail game: file
format layouts, field meanings, formulas, constants and control flow, established by studying a
lawfully obtained copy. This is factual description gathered to build an independent, interoperable
program. No decompiler output, original source code, or original comments or identifiers are
reproduced.

## Non-commercial

This project is not monetized. It takes no donations, sponsorship, advertising or payment of any
kind, and no build containing game-derived content is distributed.

## Contributions

Do not submit game assets, extracted data, decompiled source, or baked content packages. Pull
requests carrying game-derived files will be rejected. Describe recovered behavior in your own
words; do not paste decompiler output.

## Rights holders

If you hold rights in *Vampire: The Masquerade – Bloodlines*, or in any other work referenced here,
and you have a concern about this repository, please open an issue or contact the maintainer. It
will be addressed promptly.
