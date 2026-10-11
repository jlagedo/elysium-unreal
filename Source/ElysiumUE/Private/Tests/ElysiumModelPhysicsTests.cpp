#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumPlayerWorldFixture.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumModelCatalogues.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumPhysicsPropComponent.h"
#include "ElysiumPhysicalMaterial.h"
#include "ElysiumPhysicsSurfaceProps.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Visual/ElysiumPreparedPropModels.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"

namespace ElysiumModelPhysicsTest
{
	const TCHAR* BodyId = TEXT("vtmb:model:scenery/street/trashcan1/trashcan1_base");
	const TCHAR* AbsentId = TEXT("vtmb:model:scenery/structural/society/chair");
	UStaticMesh* BodyMesh()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/ElysiumBaked/Models/scenery/street/trashcan1/SM_trashcan1_base.SM_trashcan1_base"));
	}
	FElysiumEntityDef Def(const TCHAR* SourceModel, const TCHAR* Name)
	{
		FElysiumEntityDef Result;
		Result.Classname = TEXT("prop_physics"); Result.TargetName = Name;
		Result.ModelMesh = SourceModel; Result.Keys.Add(TEXT("model"), SourceModel);
		Result.Keys.Add(TEXT("spawnflags"), TEXT("1"));
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsNativeTest, "Elysium.Substrate.ModelPhysics.NativeInputs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsNativeTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Source = ElysiumModelPhysicsTest::BodyMesh();
	if (!TestNotNull(TEXT("public-imported real .phy witness"), Source)) return false;
	const auto* Cooked = UElysiumModelPhysicsInputs::Find(Source);
	if (!TestNotNull(TEXT("public importer supplied cooked body inputs"), Cooked)
		|| !TestNotNull(TEXT("public importer cooked selected native solid"), Cooked->SelectedBody.Get())) return false;
	TestTrue(TEXT("source witness has authored positive mass"), Cooked->MassKg > 0.f);
	auto* Owner = Fixture.World->SpawnActor<AActor>();
	auto* Component = NewObject<UElysiumPhysicsPropComponent>(Owner);
	auto* Mesh = DuplicateObject<UStaticMesh>(Source, Owner);
	Component->SetStaticMesh(Mesh); Component->RegisterComponent(); Owner->AddInstanceComponent(Component);
	FElysiumPhysicsSurfaceProps Surfaces;
	Surfaces.ParseSurfaceData(TEXT("authored-test"), TEXT("default { friction 0.8 elasticity 0.2 density 1000 } metal { friction 0.4 elasticity 0.7 density 7800 }"));
	bool bOk; FString Error;
	const FString Json = TEXT("{\"sourceBody\":true,\"firstSolidHullCount\":1,\"massCenterCm\":[2,3,4],\"solidPairs\":[{\"key\":\"index\",\"value\":\"9\"},{\"key\":\"mass\",\"value\":\"2\"},{\"key\":\"MASS\",\"value\":\"8\"},{\"key\":\"damping\",\"value\":\"0.25\"},{\"key\":\"rotdamping\",\"value\":\"0.5\"},{\"key\":\"inertia\",\"value\":\"1\"},{\"key\":\"surfaceprop\",\"value\":\"metal\"}]}");
	UElysiumModelPhysicsInputs::ApplyJson(Mesh, Json, bOk, Error);
	if (!TestTrue(TEXT("authored editor input projection"), bOk)) return false;
	TestTrue(TEXT("real Chaos create"), ElysiumModelPhysics::Create(Component, Surfaces, 2.f, 0.f, true, Error));
	auto* Body = ElysiumModelPhysics::Identity(Component);
	if (!TestNotNull(TEXT("native dynamic identity"), Body)) return false;
	TestEqual(TEXT("last mass pair then entity massbias"), Component->GetMass(), 16.f);
	TestEqual(TEXT("actual Chaos particle linear damping"), static_cast<float>(Body->GetPhysicsActor()->GetGameThreadAPI().LinearEtherDrag()), .25f);
	TestEqual(TEXT("actual Chaos particle angular damping"), static_cast<float>(Body->GetPhysicsActor()->GetGameThreadAPI().AngularEtherDrag()), .5f);
	TestTrue(TEXT("native COM local output"), Body->GetMassSpaceLocal().GetTranslation().Equals(FVector(2,3,4), .001));
	TestFalse(TEXT("authored start asleep"), ElysiumModelPhysics::IsActive(Component));
	Body->WakeInstance(); TestTrue(TEXT("native wake activity"), ElysiumModelPhysics::IsActive(Component));
	const FVector BaseInertia = Body->GetBodyInertiaTensor();
	const auto* Material = Cast<UElysiumPhysicalMaterial>(Body->GetSimplePhysicalMaterial());
	if (!TestNotNull(TEXT("body uses existing material type"), Material)) return false;
	TestEqual(TEXT("native body material friction"), Material->Friction, .4f);
	TestEqual(TEXT("native body material restitution input"), Material->Restitution, .7f);
	TestEqual(TEXT("material density native conversion"), Material->Density, 7.8f);
	UElysiumModelPhysicsInputs::ApplyJson(Mesh, Json.Replace(TEXT("\"inertia\",\"value\":\"1\""), TEXT("\"inertia\",\"value\":\"2\"")), bOk, Error);
	TestTrue(TEXT("recreate selected native body"), ElysiumModelPhysics::Create(Component, Surfaces, 2.f, 0.f, false, Error));
	TestTrue(TEXT("native tensor preserves authored linear inertia multiplier"), ElysiumModelPhysics::Identity(Component)->GetBodyInertiaTensor().Equals(BaseInertia * 2.f, .1));
	TestTrue(TEXT("COM survives native recreation"), ElysiumModelPhysics::Identity(Component)->GetMassSpaceLocal().GetTranslation().Equals(FVector(2,3,4), .001));
	TestTrue(TEXT("override mass wins after massbias"), ElysiumModelPhysics::Create(Component, Surfaces, 100.f, 5.f, false, Error));
	TestEqual(TEXT("actual final native override mass"), Component->GetMass(), 5.f);
	const auto* BeforeMalformed = UElysiumModelPhysicsInputs::Find(Mesh);
	UElysiumModelPhysicsInputs::ApplyJson(Mesh, TEXT("{\"sourceBody\":true,\"solidPairs\":[],\"massCenterCm\":[1,2]}"), bOk, Error);
	TestFalse(TEXT("malformed COM rejected"), bOk);
	TestTrue(TEXT("malformed replacement leaves prior cooked inputs"), UElysiumModelPhysicsInputs::Find(Mesh) == BeforeMalformed);
	UElysiumModelPhysicsInputs::ApplyJson(Mesh, Json.Replace(TEXT("\"inertia\",\"value\":\"1\""), TEXT("\"inertia\",\"value\":\"0\"")), bOk, Error);
	TestFalse(TEXT("zero inertia has explicit unsupported adapter outcome"), ElysiumModelPhysics::Create(Component, Surfaces, 0.f, 0.f, false, Error));
	TestNull(TEXT("invalid input failure leaves no dynamic identity"), ElysiumModelPhysics::Identity(Component));
	UElysiumModelPhysicsInputs::ApplyJson(Mesh, Json, bOk, Error);
	Component->SetWorldScale3D(FVector(2));
	TestTrue(TEXT("native uniform scale creation"), ElysiumModelPhysics::Create(Component, Surfaces, 0.f, 0.f, false, Error));
	TestTrue(TEXT("authored COM follows scaled native hull"), ElysiumModelPhysics::Identity(Component)->GetMassSpaceLocal().GetTranslation().Equals(FVector(4,6,8), .001));
	UElysiumModelPhysicsInputs::ApplyJson(Mesh, TEXT("{\"sourceBody\":false,\"solidPairs\":[]}"), bOk, Error);
	TestFalse(TEXT("absent source replacement fails creation"), ElysiumModelPhysics::Create(Component, Surfaces, 0.f, 0.f, false, Error));
	TestNull(TEXT("failed replacement retires prior native identity"), ElysiumModelPhysics::Identity(Component));
	TestFalse(TEXT("failed replacement has no static backend actor either"), Component->GetBodyInstance()->IsValidBodyInstance());
	ElysiumModelPhysics::Destroy(Component); ElysiumModelPhysics::Destroy(Component);
	TestNull(TEXT("idempotent destroy"), ElysiumModelPhysics::Identity(Component));
	TestFalse(TEXT("destroy removed backend actor"), Component->GetBodyInstance()->IsValidBodyInstance());
	Component->DestroyComponent();
	TestFalse(TEXT("missing asset/component creation failure"), ElysiumModelPhysics::Create(nullptr, Surfaces, 0.f, 0.f, false, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsCallerTest, "Elysium.Substrate.ModelPhysics.PlacedCaller",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsCallerTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred();
	auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	const TArray<FString> Ids = { ElysiumModelPhysicsTest::BodyId, ElysiumModelPhysicsTest::AbsentId };
	TSet<FSoftObjectPath> Paths; FString Error;
	if (!TestTrue(TEXT("actual placed catalogue producer"), FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error))) return false;
	TArray<UObject*> Assets;
	for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
	auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
	if (!TestTrue(TEXT("production resident model preparation"), Prepared.IsValid())) { AddError(Error); return false; }
	FElysiumWorldServices Services; Services.Embodiment = Map;
	FElysiumEntityWorld EntityWorld(Map, nullptr, Services);
	EntityWorld.Load(FElysiumEntityDefs());
	auto BodyDef = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("body"));
	BodyDef.Keys.Add(TEXT("massbias"), TEXT("2")); BodyDef.Keys.Add(TEXT("override_mass"), TEXT("9"));
	BodyDef.Origin = FVector(11, 22, 33);
	BodyDef.Keys.Add(TEXT("angles"), TEXT("17 31 23"));
	auto* Prop = static_cast<FElysiumPhysProp*>(EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MoveTemp(BodyDef))));
	if (!TestNotNull(TEXT("actual entity factory and Spawn caller"), Prop)
		|| !TestNotNull(TEXT("actual prepared factory Chaos body"), ElysiumModelPhysics::Identity(Prop->Visual))) return false;
	TestTrue(TEXT("entity reference is actual native actor, not component/body storage"),
		Prop->VPhysicsGetObject() == ElysiumModelPhysics::ActorIdentity(Prop->Visual));
	FElysiumEntityDef PointDef;
	PointDef.Classname = TEXT("info_target"); PointDef.TargetName = TEXT("identity_alias");
	auto* Point = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MoveTemp(PointDef)));
	auto* Other = static_cast<FElysiumPhysProp*>(EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(
		ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("other_body")))));
	if (!TestNotNull(TEXT("registered base receiver"), Point)
		|| !TestNotNull(TEXT("second real native producer"), Other)
		|| !TestNotNull(TEXT("second actual actor identity"), Other->VPhysicsGetObject())) return false;
	void* OutputObjects[2] = { Other->VPhysicsGetObject(), Other->VPhysicsGetObject() };
	TestEqual(TEXT("base absent body returns zero with positive capacity"), Point->VPhysicsGetObjectList(OutputObjects, 2), 0);
	TestTrue(TEXT("absent output untouched"), OutputObjects[0] == Other->VPhysicsGetObject());
	TestEqual(TEXT("absent body does not dereference null output"), Point->VPhysicsGetObjectList(nullptr, 1), 0);
	Point->VPhysicsSetObject(Prop->VPhysicsGetObject());
	TestEqual(TEXT("actual base list returns exactly one"), Point->VPhysicsGetObjectList(OutputObjects, 2), 1);
	TestTrue(TEXT("base output aliases actual producer"), OutputObjects[0] == Prop->VPhysicsGetObject());
	TestTrue(TEXT("base list never writes element one"), OutputObjects[1] == Other->VPhysicsGetObject());
	TestEqual(TEXT("zero capacity preserves output"), Point->VPhysicsGetObjectList(OutputObjects, 0), 0);
	TestEqual(TEXT("negative capacity never dereferences null output"), Point->VPhysicsGetObjectList(nullptr, -1), 0);
	TestTrue(TEXT("rejected output still retains prior value"), OutputObjects[0] == Prop->VPhysicsGetObject());
	AddExpectedError(TEXT("Overwriting physics object for info_target"), EAutomationExpectedErrorFlags::Contains, 2);
	Point->VPhysicsSetObject(Other->VPhysicsGetObject());
	Point->VPhysicsSetObject(Other->VPhysicsGetObject()); // identical nonnull assignment also warns.
	TestTrue(TEXT("replacement changes actual alias"), Point->VPhysicsGetObject() == Other->VPhysicsGetObject());
	TestNotNull(TEXT("overwrite does not destroy first producer"), ElysiumModelPhysics::ActorIdentity(Prop->Visual));
	Point->VPhysicsSetObject(nullptr);
	Point->VPhysicsSetObject(nullptr); // absent-to-absent is silent.
	TestNotNull(TEXT("clear does not destroy replacement producer"), ElysiumModelPhysics::ActorIdentity(Other->Visual));
	TestEqual(TEXT("cleared base list is empty"), Point->VPhysicsGetObjectList(nullptr, MAX_int32), 0);
	for (const auto& Row : ElysiumNpcKernelShape::EntitySlotRows())
	{
		if (Row.Slot == 233) TestTrue(TEXT("complete base list is a hand body in census"), Row.Body == EElysiumNpcSlotBody::Hand);
	}
	Other->ScriptHide();
	TestNull(TEXT("actual ScriptHide retirement clears entity reference"), Other->VPhysicsGetObject());
	TestNull(TEXT("actual ScriptHide retires native actor"), ElysiumModelPhysics::ActorIdentity(Other->Visual));
	Other->ScriptUnhide();
	TestNotNull(TEXT("actual ScriptUnhide recreates native actor"), ElysiumModelPhysics::ActorIdentity(Other->Visual));
	TestTrue(TEXT("unhide publishes current live identity, regardless of allocator reuse"),
		Other->VPhysicsGetObject() == ElysiumModelPhysics::ActorIdentity(Other->Visual));
	Other->InputBreak(FElysiumInputArgs());
	TestNull(TEXT("actual Break retirement clears entity reference"), Other->VPhysicsGetObject());
	TestNull(TEXT("actual Break retires native actor"), ElysiumModelPhysics::ActorIdentity(Other->Visual));
	TestEqual(TEXT("production caller mass precedence"), Prop->Visual->GetMass(), 9.f);
	TestEqual(TEXT("real datamap massbias field"), Prop->MassBias, 2.f);
	TestEqual(TEXT("real datamap override_mass field"), Prop->MassOverride, 9.f);
	TestTrue(TEXT("actual entity-authored creation pose"), Prop->Visual->GetComponentLocation().Equals(Prop->Origin));
	TestTrue(TEXT("actual native creation pitch yaw roll"), ElysiumModelPhysics::Identity(Prop->Visual)->GetUnrealWorldTransform().GetRotation().Equals(ElysiumPlayerView::ToUnreal(Prop->Angles).Quaternion(), .001));
	TestEqual(TEXT("production authored physics solid word"), Prop->GetSolid(), 6);
	TestEqual(TEXT("production physics movetype word"), Prop->GetMoveType(), 7);
	TestFalse(TEXT("production authored sleep flag"), ElysiumModelPhysics::IsActive(Prop->Visual));
	void* SleepingObject = nullptr;
	TestEqual(TEXT("live asleep body remains enumerable"), Prop->VPhysicsGetObjectList(&SleepingObject, 1), 1);
	TestTrue(TEXT("sleep does not retire native identity"), SleepingObject == ElysiumModelPhysics::ActorIdentity(Prop->Visual));
	Prop->InputWake(FElysiumInputArgs()); TestTrue(TEXT("real Wake input"), ElysiumModelPhysics::IsActive(Prop->Visual));
	Prop->SetRuntimeOrigin(FVector(44,55,66));
	TWeakObjectPtr<UStaticMeshComponent> OldComponent = Prop->Visual;
	FString AbsentPath(TEXT("models/scenery/structural/society/chair.mdl"));
	FString BodyPath(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"));
	Prop->SetModel(AbsentPath.GetCharArray().GetData());
	TestTrue(TEXT("model replacement retires component"), !OldComponent.IsValid() || OldComponent->IsBeingDestroyed());
	TestNull(TEXT("replacement no-phy model has no dynamic identity"), ElysiumModelPhysics::Identity(Prop->Visual));
	TestNull(TEXT("failed replacement clears entity reference"), Prop->VPhysicsGetObject());
	Prop->SetModel(BodyPath.GetCharArray().GetData());
	TestNotNull(TEXT("replacement production native identity"), ElysiumModelPhysics::Identity(Prop->Visual));
	TestTrue(TEXT("replacement entity reference is the current live native actor"),
		Prop->VPhysicsGetObject() == ElysiumModelPhysics::ActorIdentity(Prop->Visual));
	TestTrue(TEXT("replacement uses current entity pose"), Prop->Visual->GetComponentLocation().Equals(Prop->Origin));
	Prop->Kill(); TestNull(TEXT("entity Kill retires native identity"), ElysiumModelPhysics::Identity(Prop->Visual));
	TestNull(TEXT("Kill clears entity reference"), Prop->VPhysicsGetObject());
	auto AbsentDef = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/structural/society/chair.mdl"), TEXT("absent"));
	AbsentDef.Origin = FVector(10000, 10000, 10000);
	auto* Absent = static_cast<FElysiumPhysProp*>(EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MoveTemp(AbsentDef))));
	if (!TestNotNull(TEXT("real no-phy prop remains entity"), Absent) || !TestNotNull(TEXT("real no-phy visual remains"), Absent->Visual)) return false;
	TestTrue(TEXT("no-phy visual has native fallback geometry"), Absent->Visual->GetStaticMesh()->GetBodySetup()->AggGeom.GetElementCount() > 0);
	TestNull(TEXT("fallback geometry does not invent source body"), ElysiumModelPhysics::Identity(Absent->Visual));
	TestNull(TEXT("no-phy entity has no reference"), Absent->VPhysicsGetObject());
	TestFalse(TEXT("no-phy has no backend static actor either"), Absent->Visual->GetBodyInstance()->IsValidBodyInstance());
	TestFalse(TEXT("no-phy production caller inert"), Absent->bSimulating);
	TestEqual(TEXT("no-phy production collision admission"), Absent->Visual->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	FHitResult NoBodyHit;
	const FVector QueryCenter = Absent->Visual->Bounds.Origin;
	TestFalse(TEXT("world visibility query passes through no-phy visual fallback"), Fixture.World->LineTraceSingleByChannel(NoBodyHit,
		QueryCenter - FVector(500,0,0), QueryCenter + FVector(500,0,0), ECC_Visibility));
	TestEqual(TEXT("no-phy retail SOLID_NONE"), Absent->GetSolid(), 0);
	TestEqual(TEXT("no-phy retail MOVETYPE_NONE"), Absent->GetMoveType(), 0);
	auto* Live = static_cast<FElysiumPhysProp*>(EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(
		ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("live_teardown")))));
	if (!TestNotNull(TEXT("live body before world teardown"), ElysiumModelPhysics::Identity(Live->Visual))) return false;
	TestEqual(TEXT("native body receives actual cooked source mass"), Live->Visual->GetMass(), UElysiumModelPhysicsInputs::Find(Live->Visual->GetStaticMesh())->MassKg);
	TestEqual(TEXT("calloc default massbias"), Live->MassBias, 0.f);
	TestEqual(TEXT("calloc default mass override"), Live->MassOverride, 0.f);
	TWeakObjectPtr<UStaticMeshComponent> LastComponent = Live->Visual;
	// Exercise explicit adapter retirement, then leave an actual live actor for world teardown.
	Live->VPhysicsDestroyObject();
	TestNull(TEXT("explicit adapter retirement clears entity reference"), Live->VPhysicsGetObject());
	TestEqual(TEXT("retired production list empty"), Live->VPhysicsGetObjectList(nullptr, 1), 0);
	Live->SetModel(AbsentPath.GetCharArray().GetData());
	Live->SetModel(BodyPath.GetCharArray().GetData());
	TestTrue(TEXT("model replacement creates a current actual actor"), Live->VPhysicsGetObject() != nullptr
		&& Live->VPhysicsGetObject() == ElysiumModelPhysics::ActorIdentity(Live->Visual));
	LastComponent = Live->Visual;
	EntityWorld.Teardown();
	TestTrue(TEXT("world teardown retires live backend body"), !LastComponent.IsValid()
		|| !LastComponent->GetBodyInstance()->IsValidBodyInstance());
	ElysiumPreparedProps::Release(Map);
	return true;
}
#endif
