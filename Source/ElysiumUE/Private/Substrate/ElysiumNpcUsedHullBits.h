#pragma once

#include "CoreMinimal.h"

// The global used-hull mask `DAT_10610be8` and its three free functions — not `CAI_BaseNPC` bodies:
// `0x102f9950` reads it, `0x102f9900` clears it (and the companion word `DAT_1093412c` with it),
// `0x102f9920` ORs bits in. Their callers are the precache paths (`CBasePlayer::Precache
// 0x1016e820`, `CBaseCombatCharacter::Precache 0x10340360`, `CNPC_VCamera::Precache 0x103689c0`),
// `0x102f5bd0`, `CAI_BaseNPC::SetHullSizeNormal 0x10273070` and `CAI_TestHull::Spawn 0x102d72f0`.
//
// The mask is `.data` whose on-disk initialiser is `0xffffffff` and whose first runtime write is
// `0x102f9900`'s zero, at the start of the node-graph build. **Nothing in this runtime builds a node
// graph and nothing precaches a hull**, so the mask here starts at **0** — retail's own post-clear,
// pre-precache value — and the writers are ported so the test hull's pick has the lever its own arms
// need.
namespace ElysiumNpcUsedHullBits
{
	// `0x102f9950` — `return DAT_10610be8;`.
	int32 Get();

	// `0x102f9900` — `DAT_10610be8 = 0; DAT_1093412c = 0;`.
	void Clear();

	// `0x102f9920` — `DAT_10610be8 |= Bits;`.
	void Add(int32 Bits);
}
