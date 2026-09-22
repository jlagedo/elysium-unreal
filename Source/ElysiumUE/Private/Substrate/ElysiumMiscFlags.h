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
	// matrix) is 0006's (first-disciplines); `ElysiumDisciplines::NotifyBumped` stands in for it here.
	inline constexpr uint32 ObfBumpedObject = 0x100;

	// Name 0 of the 22, `Unconscious`. Its reader is `CBaseCombatCharacter::IsUnconscious`
	// (`0x10341aa0`), whose whole body past the scope-trace push is `return (m_iMiscFlags & 1) != 0`
	// -- and that predicate is the last refusal of `CBaseCombatCharacter::CanBeFedUponBy`
	// (`0x10339800`). Story 29d, Conditions10.
	inline constexpr uint32 Unconscious = 0x1;

	// Name 18 of the 22, `No_Resist_Feeding`. Read by `CAI_BaseNPCTroika::CanBeFedUponBy`
	// (`0x102c4a60`, `102c4a8a PUSH 0x40000`): when the feeder IS this body's own follower boss the
	// feed is refused unless this flag stands, so your own follower or ghoul may only feed on you
	// while it does. Story 29d, Conditions10.
	inline constexpr uint32 NoResistFeeding = 0x40000;

	bool ParseName(const FString& Name, uint32& OutMask);

	// `0x1030d390`, the schedule parser's `MiscFlag:` resolver -- and NOT the same body as
	// `ParseName` (`0x1033cb00`) even though both walk the same 22 names. This one answers the raw
	// INDEX 0-21, which the parser stores as an unconverted 32-bit word, where `ParseName` answers
	// the MASK. The difference is live: `MiscFlag:Unconscious` and `MiscFlag:D_Targeted` store 0
	// and 1, not 1 and 2.
	//
	// It cannot fail. An unknown name silently reads 0, which is `Unconscious` -- retail's own
	// behaviour, and one of the three operand forms that never refuse a text.
	int32 ParseScheduleIndex(const FString& Name);

	// The table itself, for the parser's diagnostics and the corpus cross-check.
	int32 NumNames();
	const TCHAR* NameAt(int32 Index);
	inline void Set(uint32& Word, uint32 Mask) { Word |= Mask; }
	inline void Clear(uint32& Word, uint32 Mask) { Word &= ~Mask; }
	inline bool Has(uint32 Word, uint32 Mask) { return Mask != 0 && (Word & Mask) == Mask; }
}
