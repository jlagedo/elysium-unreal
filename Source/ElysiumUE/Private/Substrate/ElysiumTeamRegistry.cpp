#include "Substrate/ElysiumTeamRegistry.h"

#include "Logging/LogMacros.h"
#include <cctype>
#include <vector>

FElysiumTeamRegistry::FElysiumTeamRegistry() // 0x10230750.
{
	Clear(); // 0x10230750 -> 0x1024b2d0: fresh empty table.
}

FElysiumTeamRegistry::~FElysiumTeamRegistry() // 0x102307b0.
{
	Clear(); // 0x102307b0 -> 0x1024b370: release tree/string ownership.
}

uint16 FElysiumTeamRegistry::FindOrInsert(const ANSICHAR* Name) // 0x10230880.
{
	if (Name == nullptr || Name[0] == '\0') // 0x10230880: null/empty never insert.
	{
		return InvalidSymbol; // 0x10230880: 0xffff.
	}
	std::string Normalized; // 0x10230880: Q_strncpy scratch, 0x80 bytes including NUL.
	for (int32 ByteIndex = 0; ByteIndex < 127 && Name[ByteIndex] != '\0'; ++ByteIndex) // 0x10230880.
	{
		const unsigned char Byte = static_cast<unsigned char>(Name[ByteIndex]); // 0x10230880: byte, not TCHAR.
		Normalized.push_back(static_cast<char>(std::tolower(Byte))); // 0x10230880: CRT _strlwr, byte lowercase.
	}
	const auto Existing = SymbolsByName.find(Normalized); // 0x1024b5e0 -> 0x1024b400: lookup first.
	if (Existing != SymbolsByName.end()) // 0x1024b5e0: reuse existing index.
	{
		return Existing->second; // 0x1024b5e0.
	}
	const uint16 InsertedSymbol = NextSymbol; // 0x1024b5e0 -> 0x1024b9e0: append-only tree node index.
	SymbolsByName.emplace(Normalized, InsertedSymbol); // 0x1024b5e0: own normalized bytes, including partial UTF-8.
	++NextSymbol; // 0x1024b5e0: increment WORD live count; no per-entity membership.
	return InsertedSymbol; // 0x1024b5e0: returned WORD.
}

void FElysiumTeamRegistry::Clear() // 0x1024b880.
{
	SymbolsByName.clear(); // 0x1024b880: erase tree and free every owned string.
	NextSymbol = 0; // 0x1024b880: reset live count; numeric identities never survive a level.
}

void FElysiumTeamRegistry::LevelInitPreEntity() // 0x10230820.
{
	Clear(); // 0x10230820 -> 0x1024b880.
}

void FElysiumTeamRegistry::LevelShutdownPostEntity() // 0x10230860.
{
	Clear(); // 0x10230860 -> 0x1024b880.
}

void FElysiumTeamRegistry::LevelInitPostEntity() const // 0x102308f0.
{
	std::vector<const std::string*> NamesBySymbol(NextSymbol, nullptr); // 0x1024b940: enumerate node-index order.
	for (const auto& Registered : SymbolsByName) // 0x1024b940: diagnostics only, no state writes.
	{
		NamesBySymbol[Registered.second] = &Registered.first; // 0x1024b940: symbol -> stored name.
	}
	for (uint32 SymbolIndex = 0; SymbolIndex < NamesBySymbol.size(); ++SymbolIndex) // 0x1024b940.
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("%4d : %hs"), static_cast<int32>(SymbolIndex), // 0x1024b940: DevMsg.
			NamesBySymbol[SymbolIndex]->c_str()); // 0x1024b940: byte string, no identity reconversion.
	}
}

uint16 FElysiumTeamRegistry::GetLiveCount() const // 0x1024b880: observe table +0x12.
{
	return NextSymbol; // 0x1024b880.
}

uint32 FElysiumTeamRegistry::GetStoredStringBytes() const // 0x1024b880: observe live string payload.
{
	uint32 StoredBytes = 0; // 0x1024b880.
	for (const auto& Registered : SymbolsByName) // 0x1024b880: string pools are released on reset.
	{
		StoredBytes += static_cast<uint32>(Registered.first.size()) + 1; // 0x1024b5e0: strlen + NUL.
	}
	return StoredBytes; // 0x1024b880: zero after either boundary.
}
