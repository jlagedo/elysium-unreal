#pragma once

#include "CoreMinimal.h"

// CBaseCombatCharacter::m_iMiscFlags. The 22 names at 0x10619ec8 are resolved by
// 0x1033cb00, case insensitively; unknown names yield zero. This is a plain bit word:
// AddMiscFlag (0x1033c6b0) ORs it, and discipline expiry does not clear it.
namespace ElysiumMiscFlags
{
	bool ParseName(const FString& Name, uint32& OutMask);
	inline void Set(uint32& Word, uint32 Mask) { Word |= Mask; }
	inline void Clear(uint32& Word, uint32 Mask) { Word &= ~Mask; }
	inline bool Has(uint32 Word, uint32 Mask) { return Mask != 0 && (Word & Mask) == Mask; }
}
