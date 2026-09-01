#include "ElysiumMapTransportSettings.h"

UElysiumMapTransportSettings::UElysiumMapTransportSettings()
{
	CategoryName = TEXT("Elysium");
	SectionName = TEXT("Map Transport");
}

namespace ElysiumMapTransport
{
	bool IsMapOnNewTransport(const FString& MapName, const UElysiumMapTransportSettings& Settings)
	{
		for (const FName& Listed : Settings.MapsOnNewTransport)
		{
			if (Listed.ToString().Equals(MapName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	bool IsMapOnNewTransport(const FString& MapName)
	{
		const UElysiumMapTransportSettings* Settings = GetDefault<UElysiumMapTransportSettings>();
		return Settings != nullptr && IsMapOnNewTransport(MapName, *Settings);
	}
}
