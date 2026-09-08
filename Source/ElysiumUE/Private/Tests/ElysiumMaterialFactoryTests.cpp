// Elysium.Substrate.MaterialFactory -- the factory shape: `FElysiumMaterialFactory::Create(MI_)` is a dynamic
// child of the imported instance and nothing else. No master selection, no texture, no feature
// switch -- the MID carries zero overrides of its own, so every VMT-derived value is the instance's.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Visual/ElysiumMaterialFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/Package.h"

static constexpr EAutomationTestFlags GElysiumMaterialFactoryTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMaterialFactoryTest,
	"Elysium.Substrate.MaterialFactory", GElysiumMaterialFactoryTestFlags)
bool FElysiumMaterialFactoryTest::RunTest(const FString&)
{
	// Null in, null out: a rope whose `MI_` did not load gets no MID, not a MID off some default.
	TestNull(TEXT("no imported instance -> no child"),
		FElysiumMaterialFactory::Create(nullptr, GetTransientPackage()));

	// A stand-in for an imported `MI_`: a constant instance off the engine default, exactly the
	// asset class the material lane writes. Constructed rather than loaded, so the tier stays
	// content-free.
	UMaterialInstanceConstant* Imported = NewObject<UMaterialInstanceConstant>(
		GetTransientPackage(), TEXT("MI_ElysiumFactoryTest"), RF_Transient);
#if WITH_EDITOR
	Imported->SetParentEditorOnly(UMaterial::GetDefaultMaterial(MD_Surface));
#else
	Imported->Parent = UMaterial::GetDefaultMaterial(MD_Surface);
#endif

	UMaterialInstanceDynamic* Mid = FElysiumMaterialFactory::Create(Imported, GetTransientPackage());
	if (!TestNotNull(TEXT("an imported instance yields a dynamic child"), Mid))
	{
		return true;
	}
	TestTrue(TEXT("the child's parent is the imported instance itself, not its master"),
		Mid->Parent == Imported);
	TestEqual(TEXT("no scalar override of its own"), Mid->ScalarParameterValues.Num(), 0);
	TestEqual(TEXT("no vector override of its own"), Mid->VectorParameterValues.Num(), 0);
	TestEqual(TEXT("no texture override of its own"), Mid->TextureParameterValues.Num(), 0);
	TestEqual(TEXT("no static-switch override of its own (a switch is the instance's, never the runtime's)"),
		Mid->GetStaticParameters().StaticSwitchParameters.Num(), 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
