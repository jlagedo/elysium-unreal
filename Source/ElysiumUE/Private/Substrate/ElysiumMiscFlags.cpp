#include "Substrate/ElysiumMiscFlags.h"

bool ElysiumMiscFlags::ParseName(const FString& Name, uint32& OutMask)
{
	static const TCHAR* const Names[] =
	{
		TEXT("Unconscious"), TEXT("D_Targeted"), TEXT("Allow_Fort_Soak"),
		TEXT("Allow_Thaum_Exp"), TEXT("Gave_Fighting_Wpns"), TEXT("Allow_Discipline_Fx"),
		TEXT("Update_Auto_Leveling"), TEXT("Picked_Up_Item"), TEXT("Obf_Bumped_Object"),
		TEXT("Has_Special_Dmg_Mod"), TEXT("Has_Special_Hit_Mod"), TEXT("Was_Hateful"),
		TEXT("Double_Humanity_Mods"), TEXT("Feed_Bonus_Opp_Gender"), TEXT("Feed_Bonus_Tramps"),
		TEXT("Increased_Rat_Feed"), TEXT("Cannot_Rat_Feed"), TEXT("Forced_BloodShield"),
		TEXT("No_Resist_Feeding"), TEXT("No_Ragdoll_Death"), TEXT("Gain_Stealth_Atk_Bonus"),
		TEXT("Fired_Gun"),
	};
	OutMask = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		if (Name.Equals(Names[Index], ESearchCase::IgnoreCase))
		{
			OutMask = 1u << Index;
			return true;
		}
	}
	return false;
}
