#include "ElysiumEyeTuningConfig.h"

#include "ElysiumContentPaths.h"

#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEyeTuning, Log, All);

const UElysiumEyeTuningConfig* UElysiumEyeTuningConfig::Load()
{
	// Rooted rather than re-resolved, the same shape `UElysiumClothTuningConfig::Load` uses: the
	// asset is authored content with no map epoch of its own.
	static bool bResolved = false;
	static UElysiumEyeTuningConfig* Cached = nullptr;
	if (!bResolved)
	{
		bResolved = true;
		const FString Path = FElysiumContentPaths::AuthoredEyeTuning();
		Cached = LoadObject<UElysiumEyeTuningConfig>(nullptr, *Path);
		if (Cached != nullptr)
		{
			Cached->AddToRoot();
		}
		else
		{
			// Unlike cloth, an absent asset has a well-defined neutral fallback (every caller
			// composes against a zero baseline), so this is a warning, not an error.
			UE_LOG(LogElysiumEyeTuning, Warning,
				TEXT("the authored eye tuning asset '%s' is not on the mount -- every eye composes "
				     "against a neutral (0 / zero-vector) baseline."),
				*Path);
		}
	}
	return Cached;
}
