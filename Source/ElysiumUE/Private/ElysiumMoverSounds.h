#pragma once

#include "CoreMinimal.h"

// The offline mover-soundgroup manifest, loaded once from out/sound/usable/soundgroups.json.
// VtMB resolves a mover's `soundgroup` token by directory convention (no data file): the WAVs live
// under sound/usable/<category>/<soundgroup>/<subkey>.wav — doors draw from "openable" (subkeys
// open/close/swing/locked, RE: CBaseDoor::Spawn @0x100ef060), buttons from "switches" (on/off, RE:
// CBaseButton::Spawn @0x100c8810). The map is: category -> group(lower) -> subkey -> sound-relative
// WAV path. Populated by UE_extract_sounds.py; empty if the manifest is absent. Movers resolve
// through it in InitMoverSounds; the Cog Audio window browses it.
const TMap<FString, TMap<FString, TMap<FName, FString>>>& ElysiumMoverSoundManifest();
