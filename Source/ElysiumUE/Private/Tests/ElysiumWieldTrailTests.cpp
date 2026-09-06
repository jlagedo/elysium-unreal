// The melee weapon-trail VFX's `TrailTip` socket is a synthetic bake-side attachment
// (`wield_corpus.trail_tip_from_geometry`), not a `.mdl`-authored one, so it needs its own
// coverage of its own rather than riding a shared repeat-name fixture.
//
// Self-skipping: both the wield manifest and the baked mount are gitignored and regenerable.
// The manifest itself is the enumeration source of truth for which stems carry a socket, so this
// discovers them from the native DA_WieldModels catalogue.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumModelCatalogues.h"

#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumWieldTrailTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldTrailSocketTest,
	"Elysium.Content.WieldTrailSockets", GElysiumWieldTrailTestFlags)
bool FElysiumWieldTrailSocketTest::RunTest(const FString&)
{
	const auto* Catalogue = LoadObject<UElysiumWieldCatalogue>(nullptr,
		TEXT("/ElysiumBaked/Models/_Corpus/DA_WieldModels.DA_WieldModels"));
	if (!Catalogue)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: native wield catalogue is absent; run uv run elysium import wield"));
		return true;
	}
	int32 Expected = 0, Checked = 0;
	for (const auto& Pair : Catalogue->Data.Models)
	{
		const auto& Model = Pair.Value;
		if (!Model.bHasTrailTip) continue;
		++Expected;
		USkeletalMesh* Mesh = Model.Mesh.LoadSynchronous();
		if (!TestNotNull(*FString::Printf(TEXT("%s native wield mesh"), *Pair.Key), Mesh)) continue;
		const USkeletalMeshSocket* Socket = Mesh->FindSocket(TEXT("TrailTip"));
		if (!TestNotNull(*FString::Printf(TEXT("%s bakes TrailTip"), *Pair.Key), Socket)) continue;
		++Checked;
		TestEqual(*FString::Printf(TEXT("%s TrailTip binds its declared bone"), *Pair.Key),
			Socket->BoneName, Model.TrailTipBone);
		const FTransform Baked(Socket->RelativeRotation, Socket->RelativeLocation, Socket->RelativeScale);
		const FTransform Stated(Model.TrailTipRotation, Model.TrailTipPosition);
		TestTrue(*FString::Printf(TEXT("%s TrailTip preserves the native source transform"), *Pair.Key),
			Baked.Equals(Stated));
	}
	TestTrue(TEXT("native wield catalogue carries trail sockets"), Expected > 0);
	TestEqual(TEXT("every declared trail socket is checked"), Checked, Expected);
	AddInfo(FString::Printf(TEXT("%d/%d native wield TrailTip sockets checked"), Checked, Expected));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
