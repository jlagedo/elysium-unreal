#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumClassFields.h"

// A subclass vector keyfield takes its value from the keyvalue string it spawns with.
//
// `FElysiumEntity::Construct` hands every authored keyvalue to a field's setter as a string
// variant. The subclass registration helper used to read that through `ToVector()`, which answers
// zero for anything but a vector variant, so every subclass `FVector` keyfield spawned as the
// origin whatever the map said. The base `Field` has always parsed the string; the subclass path
// now does the same, and still takes a vector variant (a restore, a Python vector) as it is.

namespace ElysiumClassFieldsTests
{
	struct FVectorProbe : FElysiumEntity
	{
		FVector Point = FVector::ZeroVector;
	};
}

static constexpr EAutomationTestFlags GElysiumClassFieldsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumClassFieldsSubclassVectorTest,
	"Elysium.Substrate.ClassFields.SubclassVector", GElysiumClassFieldsFlags)
bool FElysiumClassFieldsSubclassVectorTest::RunTest(const FString&)
{
	using ElysiumClassFieldsTests::FVectorProbe;

	FElysiumClassDesc D;
	ElysiumAddClassField(D, TEXT("point"), &FVectorProbe::Point);
	const FElysiumFieldAccessor* Acc = D.Fields.Find(FName(TEXT("point")));
	if (!TestNotNull(TEXT("the vector field registered"), Acc))
	{
		return false;
	}

	FVectorProbe Probe;
	Acc->Set(Probe, FElysiumVariant::String(TEXT("128  -32.5 72")));
	TestEqual(TEXT("a keyvalue string parses, doubled spaces included"), Probe.Point,
		FVector(128.0, -32.5, 72.0));

	Acc->Set(Probe, FElysiumVariant::String(TEXT("1 2")));
	TestEqual(TEXT("fewer than three components reads as zero, as the base field does"), Probe.Point,
		FVector::ZeroVector);

	Acc->Set(Probe, FElysiumVariant::Vector(FVector(4.0, 5.0, 6.0)));
	TestEqual(TEXT("a vector variant is taken as it is"), Probe.Point, FVector(4.0, 5.0, 6.0));
	return true;
}

#endif
