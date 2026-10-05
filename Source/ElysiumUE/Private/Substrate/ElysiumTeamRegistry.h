#pragma once

#include "CoreMinimal.h"
#include <map>
#include <string>

class FElysiumCombatCharacter;
class FElysiumEntity;

// 0x10751140: one CTeamManager table, owned by the game system, reset at both level boundaries.
// 0x1024b5e0: byte-string identity only; no actors, members, squads or relationship matrix.
class FElysiumTeamRegistry final
{
public:
	static constexpr uint16 InvalidSymbol = 0xffff; // 0x10230880 / 0x10323930.
	FElysiumTeamRegistry(); // 0x10230750: initialize the symbol table.
	~FElysiumTeamRegistry(); // 0x102307b0: destroy it and its string storage.
	FElysiumTeamRegistry(const FElysiumTeamRegistry&) = delete; // 0x10751140: single owner.
	FElysiumTeamRegistry& operator=(const FElysiumTeamRegistry&) = delete; // 0x10751140.

	uint16 FindOrInsert(const ANSICHAR* Name); // 0x10230880 -> 0x1024b5e0; ! removal belongs to AddToTeam.
	void LevelInitPreEntity(); // 0x10230820 -> 0x1024b880: clear before any entity registers.
	void LevelShutdownPostEntity(); // 0x10230860 -> 0x1024b880: clear after entities die.
	void LevelInitPostEntity() const; // 0x102308f0 -> 0x1024b940: enumerate, never reset.
	uint16 GetLiveCount() const; // 0x1024b880: table +0x12 live count, for boundary observation.
	uint32 GetStoredStringBytes() const; // 0x1024b880: string-pool payload including terminators.

private:
	void Clear(); // 0x1024b880: erase tree, free strings, zero live count.
	std::map<std::string, uint16> SymbolsByName; // 0x1024b5e0: normalized BYTE strings -> WORD.
	uint16 NextSymbol = 0; // 0x10230750 / 0x1024b5e0: append-only symbols within one level.
};

// 0x103426b0: entity candidate's +0x9c self-cast, then 0x10323930 (low-byte bool).
bool ElysiumTeamFilter(const FElysiumCombatCharacter& Character, const FElysiumEntity* Candidate);
