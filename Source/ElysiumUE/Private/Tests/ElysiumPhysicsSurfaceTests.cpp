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
#endif
