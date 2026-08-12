#pragma once

#include "CoreMinimal.h"

class FElysiumEntity;
struct FElysiumClanTemplate;

#if WITH_DEV_AUTOMATION_TESTS
namespace ElysiumNpcTestHooks
{
	// Inject a template already resolved through the real clan table into a headless NPC. Production
	// reaches the same method through UElysiumRulebookSubsystem; this only supplies that missing
	// dependency to the content-free entity-world fixture.
	bool ApplyResolvedTemplate(FElysiumEntity& Entity, const FElysiumClanTemplate& Resolved);
}
#endif
