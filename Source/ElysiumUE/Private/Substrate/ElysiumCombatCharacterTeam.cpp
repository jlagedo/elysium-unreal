#include "ElysiumPlayer.h"

#include "Containers/StringConv.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumTeamRegistry.h"

void FElysiumCombatCharacter::AddToTeam(const FString& Name) // 0x103239a0.
{
	const FTCHARToUTF8 NameBytes(*Name); // 0x10230880: Source's byte-string boundary; truncate bytes below.
	const ANSICHAR* RegistrationName = reinterpret_cast<const ANSICHAR*>(NameBytes.Get()); // 0x10323a0a.
	if (RegistrationName[0] == '!') // 0x10323a0e: strip exactly one leading !.
	{
		++RegistrationName; // 0x10323a13: !!name becomes !name.
	}
	// 0x10751140: a worldless inspection probe has no game system/table; m_TeamSymbol answers invalid.
	TeamSymbol = World ? World->TeamRegistry().FindOrInsert(RegistrationName) // 0x10323a1a / 0x10323a21.
		: FElysiumTeamRegistry::InvalidSymbol; // 0x10323a21: WORD only; TeamName is untouched.
	(void)GetTeamSymbol(); // 0x10323a28 -> 0x10323a70: final read discarded.
}

uint16 FElysiumCombatCharacter::GetTeamSymbol() const // 0x10323a70 / thunk 0x1000bc44.
{
	return TeamSymbol; // 0x10323a70: unsigned WORD at +0x10b0.
}

bool FElysiumCombatCharacter::IsSameTeam(const FElysiumCombatCharacter* Other) const // 0x10323930.
{
	if (GetTeamSymbol() == FElysiumTeamRegistry::InvalidSymbol || Other == nullptr) // 0x10323930: self first.
	{
		return false; // 0x10323930: invalid self or null other, AL=0.
	}
	if (Other->GetTeamSymbol() == FElysiumTeamRegistry::InvalidSymbol) // 0x10323930: invalid other.
	{
		return false; // 0x10323930: AL=0.
	}
	return Other->GetTeamSymbol() == GetTeamSymbol(); // 0x10323930: repeat getter reads, low-byte equality.
}

bool ElysiumTeamFilter(const FElysiumCombatCharacter& Character, const FElysiumEntity* Candidate) // 0x103426b0.
{
	const FElysiumCombatCharacter* CandidateCharacter = Candidate ? Candidate->AsCombatCharacter() // 0x10342706: +0x9c.
		: nullptr; // 0x10342702: null candidate -> null self-cast.
	return Character.IsSameTeam(CandidateCharacter); // 0x10342711 -> 0x10008d7d / 0x10323930: preserve AL.
}
