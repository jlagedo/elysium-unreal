#include "ElysiumMapTransportSettings.h"

UElysiumMapTransportSettings::UElysiumMapTransportSettings()
{
	CategoryName = TEXT("Elysium");
	SectionName = TEXT("Map Transport");
}

namespace ElysiumMapTransport
{
	// One membership rule, used by both lists: case-insensitive, empty means "no map is on it".
	static bool IsListed(const TArray<FName>& List, const FString& MapName)
	{
		for (const FName& Listed : List)
		{
			if (Listed.ToString().Equals(MapName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool IsMapOnNewTransport(const FString& MapName, const UElysiumMapTransportSettings& Settings)
	{
		return IsListed(Settings.MapsOnNewTransport, MapName);
	}

	bool IsMapOnNewTransport(const FString& MapName)
	{
		const UElysiumMapTransportSettings* Settings = GetDefault<UElysiumMapTransportSettings>();
		return Settings != nullptr && IsMapOnNewTransport(MapName, *Settings);
	}

	bool IsMapOnV2Models(const FString& MapName, const UElysiumMapTransportSettings& Settings)
	{
		return IsListed(Settings.MapsOnV2Models, MapName);
	}

	bool IsMapOnV2Models(const FString& MapName)
	{
		const UElysiumMapTransportSettings* Settings = GetDefault<UElysiumMapTransportSettings>();
		return Settings != nullptr && IsMapOnV2Models(MapName, *Settings);
	}
}
