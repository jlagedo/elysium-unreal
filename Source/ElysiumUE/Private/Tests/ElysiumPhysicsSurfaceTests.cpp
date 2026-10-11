#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPhysicsSurfaceProps.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ElysiumSurfaceTest
{
#include "ElysiumPhysicsSurfaceGolden.inl"
	using FParameters = FElysiumPhysicsSurfaceParameters;
	struct FFloatField { const TCHAR* Name; float FParameters::* Member; };
	struct FSoundField { const TCHAR* Name; uint16 FParameters::* Member; };
	constexpr FFloatField Floats[] = {
		{TEXT("friction"), &FParameters::Friction}, {TEXT("elasticity"), &FParameters::Elasticity},
		{TEXT("density"), &FParameters::Density}, {TEXT("thickness"), &FParameters::Thickness},
		{TEXT("dampening"), &FParameters::Dampening}, {TEXT("audioreflectivity"), &FParameters::AudioReflectivity},
		{TEXT("audiohardnessfactor"), &FParameters::AudioHardnessFactor},
		{TEXT("audioroughnessfactor"), &FParameters::AudioRoughnessFactor},
		{TEXT("scraperoughthreshold"), &FParameters::ScrapeRoughThreshold},
		{TEXT("impacthardthreshold"), &FParameters::ImpactHardThreshold},
		{TEXT("audiohardminvelocity"), &FParameters::AudioHardMinVelocity},
		{TEXT("maxspeedfactor"), &FParameters::MaxSpeedFactor}, {TEXT("jumpfactor"), &FParameters::JumpFactor}
	};
	constexpr FSoundField Sounds[] = {
		{TEXT("stepleft"), &FParameters::StepLeft}, {TEXT("stepright"), &FParameters::StepRight},
		{TEXT("impact"), &FParameters::Impact}, {TEXT("impactsoft"), &FParameters::ImpactSoft},
		{TEXT("impacthard"), &FParameters::ImpactHard}, {TEXT("scrape"), &FParameters::Scrape},
		{TEXT("scrapesmooth"), &FParameters::ScrapeSmooth}, {TEXT("scraperough"), &FParameters::ScrapeRough},
		{TEXT("bulletimpact"), &FParameters::BulletImpact}, {TEXT("rolling"), &FParameters::Rolling}
	};
	constexpr const TCHAR* Weapons[] = {TEXT("metal"),TEXT("wood"),TEXT("blade"),TEXT("fist"),TEXT("bullet"),TEXT("claw")};
	constexpr const TCHAR* Outcomes[] = {TEXT("norm"),TEXT("soak"),TEXT("crit")};
	void Compare(FAutomationTestBase& Test, const FElysiumPhysicsSurfaceProps& Service, const FString& Expected)
	{
		TSharedPtr<FJsonObject> Document;
		if (!Test.TestTrue(TEXT("semantic expectation JSON is valid"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Expected), Document))) return;
		const TArray<TSharedPtr<FJsonValue>>& Rows = Document->GetArrayField(TEXT("rows"));
		Test.TestEqual(TEXT("complete ordered semantic table count"), Service.SurfaceCount(), Rows.Num());
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Golden = Rows[Index]->AsObject();
			const FString Name = Golden->GetStringField(TEXT("name"));
			Test.TestEqual(Name + TEXT(" ordered name"), Service.GetSurfaceName(Index), Name);
			const FElysiumPhysicsSurfaceData* Row = Service.GetSurfaceData(Index);
			if (!Test.TestNotNull(Name, Row)) continue;
			const TSharedPtr<FJsonObject> Physics = Golden->GetObjectField(TEXT("physics"));
			for (const FFloatField& Field : Floats)
			{
				double Number;
				const float Value = Physics->TryGetNumberField(Field.Name, Number) ? float(Number)
					: FCString::Atof(*Physics->GetStringField(Field.Name));
				Test.TestEqual(Name + TEXT(" ") + Field.Name, Row->Values.*(Field.Member), Value);
			}
			Test.TestEqual(Name + TEXT(" gamematerial"), Row->Values.GameMaterial, uint16(Golden->GetIntegerField(TEXT("gamematerial"))));
			Test.TestEqual(Name + TEXT(" climbable"), Row->Values.Climbable, uint8(Golden->GetIntegerField(TEXT("climbable"))));
			const TSharedPtr<FJsonObject> SoundValues = Golden->GetObjectField(TEXT("sounds"));
			for (const FSoundField& Field : Sounds)
				Test.TestEqual(Name + TEXT(" ") + Field.Name, Service.GetString(Row->Values.*(Field.Member)), SoundValues->GetStringField(Field.Name));
			for (int32 Weapon = 0; Weapon < 6; ++Weapon)
				for (int32 Outcome = 0; Outcome < 3; ++Outcome)
				{
					const FString Key = FString::Printf(TEXT("%s_%s_impact"), Weapons[Weapon], Outcomes[Outcome]);
					Test.TestEqual(Name + TEXT(" ") + Key, Service.GetString(Row->Values.WeaponImpact[Weapon][Outcome]), SoundValues->GetStringField(Key));
				}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicsSurfaceDeployedTest,
	"Elysium.Substrate.PhysicsSurface.Deployed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumPhysicsSurfaceDeployedTest::RunTest(const FString&)
{
	// Shipped expectations are generated from user-owned published source into ignored content.
	FString Expected;
	if (!TestTrue(TEXT("deployed semantic producer is resident"), FFileHelper::LoadFileToString(Expected,
		*(FElysiumContentPaths::CorpusRoot() / TEXT("physics/surfaceproperties.semantic.json"))))) return false;
	FElysiumPhysicsSurfaceProps* Service;
	const FElysiumPhysicsSurfaceData* Material;
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		Service = &World.PhysicsSurfaces();
		ElysiumSurfaceTest::Compare(*this, *Service, Expected);
		Material = Service->GetSurfaceData(Service->GetSurfaceIndex(TEXT("default")));
		if (!TestNotNull(TEXT("real deployed default material"), Material)) return false;
	}
	FElysiumEntityWorld NextWorld(nullptr, nullptr);
	TestTrue(TEXT("factory process service survives world teardown"), &NextWorld.PhysicsSurfaces() == Service);
	TestTrue(TEXT("material data survives world teardown"), Service->GetSurfaceData(Service->GetSurfaceIndex(TEXT("default"))) == Material);
	TestEqual(TEXT("exact filename is already parsed"), Service->ParseSurfaceData(TEXT("scripts/surfaceproperties.txt"), TEXT("other { friction 2 }")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicsSurfaceParserTest,
	"Elysium.Substrate.PhysicsSurface.Parser",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumPhysicsSurfaceParserTest::RunTest(const FString&)
{
	FElysiumPhysicsSurfaceProps Service;
	TestNull(TEXT("unparsed signed negative has no fallback"), Service.GetSurfaceData(-1));
	TestNull(TEXT("unparsed remapped index has no row0"), Service.GetSurfaceData(128));
	TestEqual(TEXT("public parser returns total count"), Service.ParseSurfaceData(TEXT("scripts/surfaceproperties.txt"), ElysiumSurfaceTest::ParserSurfaceSource), 12);
	FString CaseExpected;
	for (const TCHAR* Part : ElysiumSurfaceTest::ParserSurfaceExpectedParts) CaseExpected += Part;
	ElysiumSurfaceTest::Compare(*this, Service, CaseExpected);
	TestEqual(TEXT("ordinary symbols compare case sensitively"), Service.GetSurfaceIndex(TEXT("DEFAULT")), -1);
	TestEqual(TEXT("unknown name returns signed negative"), Service.GetSurfaceIndex(TEXT("missing")), -1);
	TestEqual(TEXT("reserved name alone compares case insensitively"), Service.GetSurfaceIndex(TEXT("$material_index_shadow")), 0xf000);
	TestEqual(TEXT("unknown dollar name has no reserved index"), Service.GetSurfaceIndex(TEXT("$other")), -1);
	TestNull(TEXT("negative data lookup stays null"), Service.GetSurfaceData(MIN_int32));
	TestNull(TEXT("in-range127 outside actual table stays null"), Service.GetSurfaceData(127));
	TestTrue(TEXT("index128 remaps to row0"), Service.GetSurfaceData(128) == Service.GetSurfaceData(0));
	TestTrue(TEXT("shadow reserved index selects final row"), Service.GetSurfaceData(0xf000) == Service.GetSurfaceData(11));
	TestEqual(TEXT("exact filename suppresses parse and all symbol writes"), Service.ParseSurfaceData(TEXT("scripts/surfaceproperties.txt"), TEXT("other { friction 2 }")), 0);
	TestEqual(TEXT("case-distinct filename parses another row"), Service.ParseSurfaceData(TEXT("SCRIPTS/SURFACEPROPERTIES.TXT"), TEXT("other { friction 2 }")), 13);
	TestEqual(TEXT("no second shadow is appended"), Service.GetSurfaceIndex(TEXT("other")), 12);
	TestEqual(TEXT("invalid symbol string is empty"), Service.GetString(0xffff), FString());
	TestTrue(TEXT("bad authored key diagnoses and continues"), Service.Diagnostics().Num() > 0);
	FElysiumPhysicsSurfaceProps EmptyFirst;
	TestEqual(TEXT("first empty source still creates shadow"), EmptyFirst.ParseSurfaceData(TEXT("empty"), TEXT("")), 1);
	TestEqual(TEXT("empty first shadow is friction.8"), EmptyFirst.GetSurfaceData(0)->Friction(), 0.8f);
	TestEqual(TEXT("later default is separate from initial shadow"), EmptyFirst.ParseSurfaceData(TEXT("later"), TEXT("default { friction 2 density 7 }")), 2);
	TestEqual(TEXT("once-only shadow was not refreshed"), EmptyFirst.GetSurfaceData(0xf000)->Density(), 0.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldMaterialResolverTest,
	"Elysium.Substrate.PhysicsSurface.WorldMaterialResolver",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumWorldMaterialResolverTest::RunTest(const FString&)
{
	FElysiumPhysicsSurfaceProps Service;
	Service.ParseSurfaceData(TEXT("world-test"), TEXT("first { friction .3 } default { friction .8 } metal { elasticity 2 } wood { friction .6 }"));
	FElysiumWorldMaterialMapping Mapping;
	FString Error;
	TestEqual(TEXT("fresh manager identity127"), Mapping.Slots[127], uint16(127));
	TestTrue(TEXT("identity feature1 is default row1"), Service.ResolveWorldMaterial(Mapping, 1, nullptr) == Service.GetSurfaceData(1));
	TestNull(TEXT("ordinary invalid mapped row stays null"), Service.ResolveWorldMaterial(Mapping, 127, nullptr));
	const TArray<FElysiumWorldMaterialDeclaration> Installed = {
		{TEXT("metal"), TEXT(" +7suffix")}, {TEXT("wood"), TEXT("7")},
		{TEXT("missing"), TEXT("8")}, {TEXT("metal"), TEXT("9")},
		{TEXT("ignored"), TEXT("128")}, {TEXT("metal"), TEXT("0x10")}};
	TestTrue(TEXT("present table installs"), Service.InstallWorldMaterialTable(Mapping, true, Installed, Error));
	const FElysiumPhysicsSurfaceData* Wood = Service.ResolveWorldMaterial(Mapping, 7, nullptr);
	if (TestNotNull(TEXT("duplicate slot resolves named row"), Wood))
		TestEqual(TEXT("duplicate slot last wins named row"), Wood->Friction(), .6f);
	TestEqual(TEXT("unknown narrows to65535"), Mapping.Slots[8], uint16(65535));
	TestTrue(TEXT("unknown resolves first authored row, not named default"), Service.ResolveWorldMaterial(Mapping, 8, nullptr) == Service.GetSurfaceData(0));
	TestTrue(TEXT("missing present slot maps row0"), Service.ResolveWorldMaterial(Mapping, 6, nullptr) == Service.GetSurfaceData(0));
	const FElysiumPhysicsSurfaceData* Metal = Service.ResolveWorldMaterial(Mapping, 9, nullptr);
	if (TestNotNull(TEXT("raw authored metal row resolves"), Metal))
		TestEqual(TEXT("typed raw elasticity2 retained"), Metal->Elasticity(), 2.f);
	const FElysiumPhysicsSurfaceData* Body = Service.GetSurfaceData(0xf000);
	TestTrue(TEXT("feature zero selects current reserved body despite mapped slot0"), Service.ResolveWorldMaterial(Mapping, 0, Body) == Body);
	TestNull(TEXT("feature zero null body has no invented fallback"), Service.ResolveWorldMaterial(Mapping, 0, nullptr));
	TestTrue(TEXT("absent block preserves prior installed manager state"), Service.InstallWorldMaterialTable(Mapping, false, {}, Error));
	TestEqual(TEXT("absent block retained wood"), Mapping.Slots[7], uint16(Service.GetSurfaceIndex(TEXT("wood"))));
	for (const TCHAR* Token : {TEXT("-1"), TEXT(" -12tail"), TEXT("2147483648"), TEXT("99999999999999999999")})
	{
		const TArray<FElysiumWorldMaterialDeclaration> Invalid = {{TEXT("metal"), TEXT("7")}, {TEXT("wood"), Token}};
		TestFalse(TEXT("unsafe authored slot refuses without OOB clone"), Service.InstallWorldMaterialTable(Mapping, true, Invalid, Error));
		TestEqual(TEXT("refusal retains previous mapping atomically"), Mapping.Slots[7], uint16(Service.GetSurfaceIndex(TEXT("wood"))));
	}
	for (const TCHAR* Token : {TEXT("+"), TEXT("-oops"), TEXT("-0tail"), TEXT("nondigit")})
	{
		TestTrue(TEXT("atoi malformed nonnumeric token is zero"), Service.InstallWorldMaterialTable(Mapping, true, {{TEXT("metal"), Token}}, Error));
		TestEqual(TEXT("decimal prefix mapped slot zero"), Mapping.Slots[0], uint16(Service.GetSurfaceIndex(TEXT("metal"))));
	}
	TestTrue(TEXT("signed decimal partial token installs"), Service.InstallWorldMaterialTable(Mapping, true, {{TEXT("wood"), TEXT("\t+12.5")}}, Error));
	TestEqual(TEXT("decimal prefix stops at punctuation"), Mapping.Slots[12], uint16(Service.GetSurfaceIndex(TEXT("wood"))));
	TestTrue(TEXT("empty present table resets mapping"), Service.InstallWorldMaterialTable(Mapping, true, {}, Error));
	TestEqual(TEXT("empty present table zero reset12"), Mapping.Slots[12], uint16(0));
	return true;
}
#endif
