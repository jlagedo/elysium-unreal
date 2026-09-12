#pragma once

#include "CoreMinimal.h"

// CBaseCombatCharacter::m_iMiscFlags. The 22 names at 0x10619ec8 are resolved by
// 0x1033cb00, case insensitively; unknown names yield zero. This is a plain bit word:
// AddMiscFlag (0x1033c6b0) ORs it, and discipline expiry does not clear it.
namespace ElysiumMiscFlags
{
	// Name 8 of the 22, `Obf_Bumped_Object`: the one bit a code path writes outside the authored
	// discipline tables -- the player's touch handler `0x10147690` ORs it on every frame the
	// player's hull is in solid contact with a character. Its retail reader (Obfuscate's break
	// matrix) is 0007's; `ElysiumDisciplines::NotifyBumped` stands in for it here.
	inline constexpr uint32 ObfBumpedObject = 0x100;

	bool ParseName(const FString& Name, uint32& OutMask);
	inline void Set(uint32& Word, uint32 Mask) { Word |= Mask; }
	inline void Clear(uint32& Word, uint32 Mask) { Word &= ~Mask; }
	inline bool Has(uint32 Word, uint32 Mask) { return Mask != 0 && (Word & Mask) == Mask; }
}
