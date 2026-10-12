#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumPlayerWorldFixture.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMapEntities.h"
#include "ElysiumContentPaths.h"
#include "Substrate/ElysiumRetailConVars.h"
#include "HAL/IConsoleManager.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumMapPlaces.h"
#include "ElysiumModelCatalogues.h"
#include "ElysiumModelPhysicsInputs.h"
#include "ElysiumPhysicsPropComponent.h"
#include "ElysiumPhysicalMaterial.h"
#include "ElysiumPhysicsSurfaceProps.h"
#include "Substrate/ElysiumModelPhysics.h"
#include "Substrate/ElysiumPhysProp.h"
#include "Substrate/ElysiumProp.h"
#include "Substrate/ElysiumNpc.h"
#include "ElysiumCollisionChannels.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Visual/ElysiumPreparedPropModels.h"
#include "Engine/StaticMesh.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "ElysiumSaveArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "PBDRigidsSolver.h"
#include "Player/ElysiumCommandBus.h"
#include "Debug/ElysiumConsole.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumTriggerBase.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumMapCollisionPayload.h"
#include "ElysiumWorldCollisionActor.h"
#include "Misc/ScopeExit.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysicsInterfaceCore.h"
#include <limits>

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
	FElysiumSavedPhysicsBody BeforeMaterial, AfterMaterial;
	void* MaterialActor=ElysiumModelPhysics::ActorIdentity(Component);
	if (!TestTrue(TEXT("capture ordinary metal mechanics"),ElysiumModelPhysics::Capture(MaterialActor,BeforeMaterial))) return false;
	if (!TestTrue(TEXT("ordinary metal attaches normal shadow"),ElysiumModelPhysics::SetShadow(MaterialActor,10000.f,10000.f,false,false))) return false;
	const auto* ReservedMaterial=Cast<UElysiumPhysicalMaterial>(Body->GetSimplePhysicalMaterial());
	if (!TestNotNull(TEXT("normal shadow selects typed reserved row"),ReservedMaterial)) return false;
	TestEqual(TEXT("reserved row inherits first default density independently of ordinary metal recipe"),ReservedMaterial->RawDensity,1000.f);
	TestEqual(TEXT("reserved row overrides friction"),ReservedMaterial->Friction,.8f);
	TestEqual(TEXT("reserved row overrides elasticity"),ReservedMaterial->RawElasticity,0.f);
	ElysiumModelPhysics::RemoveShadow(MaterialActor);
	if (!TestTrue(TEXT("capture removed normal shadow"),ElysiumModelPhysics::Capture(MaterialActor,AfterMaterial))) return false;
	TestEqual(TEXT("material switch retains authored ordinary recipe"),AfterMaterial.Recipe.Surface,FString(TEXT("metal")));
	TestEqual(TEXT("material removal retains reserved current identity"),AfterMaterial.CurrentSurface,FString(TEXT("$MATERIAL_INDEX_SHADOW")));
	TestTrue(TEXT("material setter and removal preserve original native mass inertia and frame"),AfterMaterial.CurrentMassKg==BeforeMaterial.CurrentMassKg && AfterMaterial.CurrentInertia==BeforeMaterial.CurrentInertia && AfterMaterial.CurrentInverseInertia==BeforeMaterial.CurrentInverseInertia && AfterMaterial.CurrentMassFrame.Equals(BeforeMaterial.CurrentMassFrame,0.));
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
	// Selected leaf225 must obey the same current-field lifetime, even when its former Visual lives.
	auto* Detached = static_cast<FElysiumPhysProp*>(EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(
		ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("detached_producer")))));
	auto* Supplied = static_cast<FElysiumPhysProp*>(EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(
		ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("supplied_producer")))));
	if (!Detached || !Supplied || !Detached->VPhysicsGetObject() || !Supplied->VPhysicsGetObject()) return false;
	void* DetachedActor = Detached->VPhysicsGetObject();
	Detached->VPhysicsSetObject(nullptr); Detached->VPhysicsDestroyObject();
	TestTrue(TEXT("clear then leaf225 leaves former native producer alive"), ElysiumModelPhysics::ActorIdentity(Detached->Visual) == DetachedActor);
	Detached->VPhysicsSetObject(Supplied->VPhysicsGetObject()); Detached->VPhysicsDestroyObject();
	TestTrue(TEXT("overwrite then leaf225 preserves unrelated former native producer"), ElysiumModelPhysics::ActorIdentity(Detached->Visual) == DetachedActor);
	TestNull(TEXT("overwrite then leaf225 destroys supplied other native producer"), ElysiumModelPhysics::ActorIdentity(Supplied->Visual));
	Detached->Kill();
	TestTrue(TEXT("Kill art gating preserves detached former native producer"), ElysiumModelPhysics::ActorIdentity(Detached->Visual) == DetachedActor);
	ElysiumModelPhysics::Destroy(Detached->Visual); // environment/native owner cleanup of detached incarnation.
	for (const auto& Row : ElysiumNpcKernelShape::EntitySlotRows())
	{
		if (Row.Slot == 233) TestTrue(TEXT("complete base list is a hand body in census"), Row.Body == EElysiumNpcSlotBody::Hand);
	}
	void* OtherActor = Other->VPhysicsGetObject();
	const auto OtherToken = ElysiumModelPhysics::CaptureIdentity(OtherActor);
	Other->ScriptHide();
	TestTrue(TEXT("actual ScriptHide retains current native field and incarnation"), Other->VPhysicsGetObject() == OtherActor && ElysiumModelPhysics::IsCurrentOutput(OtherToken));
	Other->ScriptUnhide();
	TestTrue(TEXT("actual ScriptUnhide retains native actor"), ElysiumModelPhysics::ActorIdentity(Other->Visual) == OtherActor && ElysiumModelPhysics::IsCurrentOutput(OtherToken));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsSaveTest, "Elysium.Substrate.ModelPhysics.SavedBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsSaveTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred();
	auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	TSet<FSoftObjectPath> Paths; FString Error;
	if (!TestTrue(TEXT("actual model catalogue"), FElysiumPreparedPropModels::GatherPaths(Placed, Skins,
		{ ElysiumModelPhysicsTest::BodyId }, Paths, Error))) return false;
	TArray<UObject*> Assets;
	for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
	auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins,
		{ ElysiumModelPhysicsTest::BodyId }, Assets, Error);
	if (!TestTrue(TEXT("production model preparation"), Prepared.IsValid())) return false;
	FElysiumWorldServices Services; Services.Embodiment = Map;
	FElysiumEntityDefs Defs; Defs.MapName = TEXT("model_physics_save");
	FElysiumEntityDefs SourceDefs = Defs;
	FElysiumEntityWorld Source(Map, nullptr, Services); Source.Load(MoveTemp(SourceDefs));
	const TCHAR* Model = TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl");
	auto* Moving = static_cast<FElysiumPhysProp*>(Source.Resolve(Source.SpawnRuntimeEntity(ElysiumModelPhysicsTest::Def(Model, TEXT("moving")))));
	auto* Sleeping = static_cast<FElysiumPhysProp*>(Source.Resolve(Source.SpawnRuntimeEntity(ElysiumModelPhysicsTest::Def(Model, TEXT("sleeping")))));
	auto* Removed = static_cast<FElysiumPhysProp*>(Source.Resolve(Source.SpawnRuntimeEntity(ElysiumModelPhysicsTest::Def(Model, TEXT("removed")))));
	if (!Moving || !Sleeping || !Removed || !TestNotNull(TEXT("real Spawn actor"), Moving->VPhysicsGetObject())) return false;
	TestNotNull(TEXT("actual successful model creation registered recipe"), ElysiumModelPhysics::RecipeFor(Moving->VPhysicsGetObject()));
	TestTrue(TEXT("base retail physics admission"), Moving->ShouldSavePhysics());
	FBodyInstance* MovingBody = ElysiumModelPhysics::Identity(Moving->Visual);
	FElysiumSavedPhysicsBody BeforeController;
	if (!ElysiumModelPhysics::Capture(Moving->VPhysicsGetObject(),BeforeController)) return false;
	TestTrue(TEXT("production native body admits temporary shadow attachment"),ElysiumModelPhysics::SetShadow(Moving->VPhysicsGetObject(),10000.f,10000.f,false,false));
	ElysiumModelPhysics::RemoveShadow(Moving->VPhysicsGetObject());
	FElysiumSavedPhysicsBody AfterController;
	if (!ElysiumModelPhysics::Capture(Moving->VPhysicsGetObject(),AfterController)) return false;
	TestEqual(TEXT("removed controller retains reserved current material"),AfterController.CurrentSurface,FString(TEXT("$MATERIAL_INDEX_SHADOW")));
	TestFalse(TEXT("reserved current material does not infer installed controller"),AfterController.Shadow.bInstalled);
	TestEqual(TEXT("material switch retains authored recipe spelling"),AfterController.Recipe.Surface,BeforeController.Recipe.Surface);
	TestTrue(TEXT("removed-controller save producer retains original Chaos pair and mass frame"),
		AfterController.CurrentInertia==BeforeController.CurrentInertia && AfterController.CurrentInverseInertia==BeforeController.CurrentInverseInertia &&
		AfterController.CurrentMassFrame.Equals(BeforeController.CurrentMassFrame,1.e-6));
	Moving->Visual->SetEnableGravity(false);
	Moving->Visual->SetLinearDamping(.75f); // actual runtime setter also used by water's damping observer
	const FTransform NativePose(FRotator(19, 37, 11), FVector(5000, 4000, 3000), Moving->Visual->GetComponentScale());
	MovingBody->SetBodyTransform(NativePose, ETeleportType::TeleportPhysics);
	MovingBody->SetLinearVelocity(FVector(75, -20, 8), false);
	MovingBody->SetAngularVelocityInRadians(FVector(.2, -.4, .6), false);
	Sleeping->Visual->SetWorldLocation(FVector(-5000, -4000, 3000));
	ElysiumModelPhysics::Identity(Sleeping->Visual)->PutInstanceToSleep();
	auto* Bodyless = static_cast<FElysiumPhysProp*>(Source.Resolve(Source.SpawnRuntimeEntity(ElysiumModelPhysicsTest::Def(Model, TEXT("bodyless")))));
	if (!Bodyless || !TestNotNull(TEXT("bodyless witness initially has actual Spawn body"), Bodyless->VPhysicsGetObject())) return false;
	const int32 BodylessIndex = Bodyless->Handle.Index;
	Bodyless->VPhysicsDestroyObject(); // actual retail operation, not a serializer helper
	TestNull(TEXT("explicit destruction leaves current field null"), Bodyless->VPhysicsGetObject());
	FElysiumSavedPhysicsBody NativeState;
	if (!TestTrue(TEXT("real native output captured"), ElysiumModelPhysics::Capture(Moving->VPhysicsGetObject(), NativeState))) return false;
	TestFalse(TEXT("native pose differs from entity creation pose"), NativeState.Pose.GetLocation().Equals(Moving->Origin));
	FElysiumEntityDef AliasDef; AliasDef.Classname = TEXT("info_target");
	FElysiumEntity* Alias = Source.Resolve(Source.SpawnRuntimeEntity(AliasDef));
	if (!Alias) return false;
	Alias->VPhysicsSetObject(Moving->VPhysicsGetObject());
	const int32 AliasIndex = Alias->Handle.Index;
	FElysiumEntity* TeardownAlias = Source.Resolve(Source.SpawnRuntimeEntity(AliasDef));
	if (!TeardownAlias) return false;
	TeardownAlias->VPhysicsSetObject(Moving->VPhysicsGetObject());
	const int32 TeardownAliasIndex = TeardownAlias->Handle.Index;
	FElysiumEntity* RemovedAlias = Source.Resolve(Source.SpawnRuntimeEntity(AliasDef));
	if (!RemovedAlias) return false;
	RemovedAlias->VPhysicsSetObject(Removed->VPhysicsGetObject()); RemovedAlias->Kill();
	TestNull(TEXT("generic alias Kill retires pointed native producer"), ElysiumModelPhysics::Identity(Removed->Visual));
	TestNull(TEXT("producer reference retires before release"), Removed->VPhysicsGetObject());
	Removed->Kill();
	FElysiumMapSnapshot Snapshot; Source.Freeze(Snapshot);
	TestEqual(TEXT("Physics saves eligible fields including aliases and absent body"), Snapshot.PhysicsBodies.Num(), 5);
	const FElysiumSavedPhysicsBody* AbsentField = Snapshot.PhysicsBodies.FindByPredicate(
		[BodylessIndex](const FElysiumSavedPhysicsBody& State) { return State.EntityIndex == BodylessIndex; });
	TestTrue(TEXT("actual Physics save carries count0 field state"), AbsentField && !AbsentField->bHasObject);
	TestFalse(TEXT("registered source alias admitted"), Snapshot.bPhysicsSaveFailed);
	for (const auto& State : Snapshot.PhysicsBodies)
	{
		if (State.bHasObject)
		{
			TestEqual(*FString::Printf(TEXT("native saved response count #%d"), State.EntityIndex), State.CollisionResponses.Num(), static_cast<int32>(sizeof(FCollisionResponseContainer().EnumArray)));
			TestFalse(*FString::Printf(TEXT("native saved response enum #%d"), State.EntityIndex), State.CollisionResponses.ContainsByPredicate([](uint8 Response) { return Response > ECR_Block; }));
		}
		TArray<uint8> FieldBytes; FMemoryWriter FieldWriter(FieldBytes, true); FieldWriter << const_cast<FElysiumSavedPhysicsBody&>(State);
		FElysiumSavedPhysicsBody FieldRead; FMemoryReader FieldReader(FieldBytes, true); FieldReader << FieldRead;
		TestFalse(*FString::Printf(TEXT("actual captured row decodes #%d"), State.EntityIndex), FieldReader.IsError());
	}
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true); FElysiumSaveArchive SaveAr(Writer, FElysiumSaveVersion::Latest);
	SaveAr << Snapshot;
	FElysiumMapSnapshot Decoded;
	FMemoryReader Reader(Bytes, true); FElysiumSaveArchive LoadAr(Reader, FElysiumSaveVersion::Latest);
	LoadAr << Decoded;
	if (!TestFalse(TEXT("actual map archive/Physics block read"), Reader.IsError())
		|| !TestEqual(TEXT("persisted Physics rows decode"), Decoded.PhysicsBodies.Num(), 5)) return false;
	const int32 MovingIndex = Moving->Handle.Index;
	const int32 SleepingIndex = Sleeping->Handle.Index;
	Source.Teardown();
	FElysiumEntityDefs DestinationDefs = Defs;
	FElysiumEntityWorld Destination(Map, nullptr, Services); Destination.Load(MoveTemp(DestinationDefs));
	// The consumer must read the persisted block; no decoded-array handoff can satisfy this.
	Decoded.PhysicsBodies.Reset();
	if (!TestTrue(TEXT("actual ApplySnapshot reconstructs through Physics block"), Destination.ApplySnapshot(Decoded) >= 0)) return false;
	auto* Restored = static_cast<FElysiumPhysProp*>(Destination.Resolve(FElysiumEntityHandle(MovingIndex, Destination.GetEpoch())));
	auto* RestoredSleep = static_cast<FElysiumPhysProp*>(Destination.Resolve(FElysiumEntityHandle(SleepingIndex, Destination.GetEpoch())));
	FElysiumEntity* RestoredAlias = Destination.Resolve(FElysiumEntityHandle(AliasIndex, Destination.GetEpoch()));
	auto* RestoredBodyless = static_cast<FElysiumPhysProp*>(Destination.Resolve(FElysiumEntityHandle(BodylessIndex, Destination.GetEpoch())));
	if (!RestoredBodyless) return false;
	TestNull(TEXT("Destroy Freeze archive ApplySnapshot remains bodyless"), RestoredBodyless->VPhysicsGetObject());
	TestFalse(TEXT("count0 restore retires baseline Spawn native actor through final reconnect"),
		RestoredBodyless->Visual->GetBodyInstance()->IsValidBodyInstance());
	if (!Restored || !RestoredSleep || !TestNotNull(TEXT("restored actual native actor"), Restored->VPhysicsGetObject())) return false;
	FElysiumSavedPhysicsBody RestoredState;
	if (!TestTrue(TEXT("restored output has live recipe association"), ElysiumModelPhysics::Capture(Restored->VPhysicsGetObject(), RestoredState))) return false;
	TestTrue(TEXT("final entity reconnect preserves native pose"), RestoredState.Pose.Equals(NativeState.Pose, .001));
	TestTrue(TEXT("native linear velocity persisted"), RestoredState.LinearVelocity.Equals(NativeState.LinearVelocity, .001));
	TestTrue(TEXT("native angular velocity persisted in radians"), RestoredState.AngularVelocityRadians.Equals(NativeState.AngularVelocityRadians, .001));
	TestTrue(TEXT("dynamic Physics reconstruction retains selected native inertia pair and principal frame"),
		RestoredState.CurrentInertia == NativeState.CurrentInertia && RestoredState.CurrentInverseInertia == NativeState.CurrentInverseInertia &&
		RestoredState.CurrentMassFrame.Equals(NativeState.CurrentMassFrame, 1.e-6));
	TestFalse(TEXT("gravity state persisted"), RestoredState.bGravity);
	TestEqual(TEXT("actual Physics stream reconstructs detached current reserved material"),RestoredState.CurrentSurface,NativeState.CurrentSurface);
	TestFalse(TEXT("detached reserved restore creates no controller"),RestoredState.Shadow.bInstalled);
	TestEqual(TEXT("current runtime damping persisted independently from creation recipe"), RestoredState.CurrentLinearDamping, .75f);
	TestFalse(TEXT("awake state persisted"), RestoredState.bAsleep);
	TestFalse(TEXT("sleeping identity retained"), ElysiumModelPhysics::IsActive(RestoredSleep->Visual));
	TestNotNull(TEXT("sleeping recipe retained"), ElysiumModelPhysics::RecipeFor(RestoredSleep->VPhysicsGetObject()));
	TestEqual(TEXT("owned model identity persisted"), RestoredState.Recipe.ModelAsset, NativeState.Recipe.ModelAsset);
	TestEqual(TEXT("owned authored pairs persisted"), RestoredState.Recipe.SolidPairsJson, NativeState.Recipe.SolidPairsJson);
	TestEqual(TEXT("real reconstructed mass"), Restored->Visual->GetMass(), NativeState.Recipe.MassKg);
	if (!RestoredAlias || !TestNotNull(TEXT("base alias field reconstructed actual actor"), RestoredAlias->VPhysicsGetObject())) return false;
	TestTrue(TEXT("type1 allocates separate current native identities per saved field"), RestoredAlias->VPhysicsGetObject() != Restored->VPhysicsGetObject());
	FElysiumSavedPhysicsBody AliasState;
	TestTrue(TEXT("base reconstructed output registered"), ElysiumModelPhysics::Capture(RestoredAlias->VPhysicsGetObject(), AliasState));
	TestTrue(TEXT("base alias native pose persists"), AliasState.Pose.Equals(NativeState.Pose, .001));
	TestTrue(TEXT("base alias native velocity persists"), AliasState.LinearVelocity.Equals(NativeState.LinearVelocity, .001));
	TestEqual(TEXT("alias independent reconstruction retains current material"),AliasState.CurrentSurface,NativeState.CurrentSurface);
	TestFalse(TEXT("reserved alias restore does not infer shadow controller"),AliasState.Shadow.bInstalled);
	TestEqual(TEXT("base alias native collision profile persists"), AliasState.CollisionProfile, NativeState.CollisionProfile);
	UPrimitiveComponent* AliasComponent = ElysiumModelPhysics::ComponentFor(RestoredAlias->VPhysicsGetObject());
	TestFalse(TEXT("reconstructed alias service duplicates no rendered art"), AliasComponent->IsVisible());
	TestFalse(TEXT("nonnull restored base field rejects a second reconstruction"), RestoredAlias->RestoreSavedPhysics(AliasState, Error));
	TestTrue(TEXT("rejected restore retains current actor"), ElysiumModelPhysics::Identity(AliasComponent) != nullptr);
	RestoredAlias->VPhysicsSetObject(nullptr); // SetObject clears only reference, not the owned producer
	TestNotNull(TEXT("reference clear retains native saved-field producer"), ElysiumModelPhysics::Identity(AliasComponent));
	RestoredAlias->VPhysicsSetObject(ElysiumModelPhysics::ActorIdentity(AliasComponent));
	RestoredAlias->Kill();
	TestFalse(TEXT("actual base Kill retires reconstructed native body"), AliasComponent->GetBodyInstance()->IsValidBodyInstance());
	FElysiumEntity* RestoredTeardownAlias = Destination.Resolve(FElysiumEntityHandle(TeardownAliasIndex, Destination.GetEpoch()));
	if (!RestoredTeardownAlias) return false;
	TWeakObjectPtr<UPrimitiveComponent> TeardownComponent = ElysiumModelPhysics::ComponentFor(RestoredTeardownAlias->VPhysicsGetObject());
	Restored->Kill();
	FElysiumMapSnapshot AfterKill; Destination.Freeze(AfterKill);
	TestEqual(TEXT("actual Kill recipe retirement plus retained absent field observed by save"), AfterKill.PhysicsBodies.Num(), 3);
	Destination.Teardown();
	TestTrue(TEXT("world teardown retires base reconstructed body"), !TeardownComponent.IsValid()
		|| !TeardownComponent->GetBodyInstance()->IsValidBodyInstance());
	FElysiumMapSnapshot Malformed = Snapshot;
	TArray<uint8> RowBytes; FMemoryWriter RowWriter(RowBytes, true); RowWriter << Snapshot.PhysicsBodies[0];
	int32 RowOffset = INDEX_NONE;
	for (int32 Offset = 4; Offset + RowBytes.Num() <= Malformed.BlockStream.Num(); ++Offset)
		if (FMemory::Memcmp(Malformed.BlockStream.GetData() + Offset, RowBytes.GetData(), RowBytes.Num()) == 0)
		{ RowOffset = Offset; break; }
	if (!TestTrue(TEXT("actual persisted Physics row located"), RowOffset != INDEX_NONE)) return false;
	FMemoryReader PrefixReader(RowBytes, true); int32 SavedIndex; FString SavedField;
	PrefixReader << SavedIndex << SavedField;
	Malformed.BlockStream[RowOffset + PrefixReader.Tell()] = 255; // corrupt the actual persisted kind byte
	TArray<uint8> MalformedBytes; FMemoryWriter MalformedWriter(MalformedBytes, true);
	FElysiumSaveArchive MalformedSave(MalformedWriter, FElysiumSaveVersion::Latest); MalformedSave << Malformed;
	FElysiumMapSnapshot MalformedRead;
	FMemoryReader MalformedReader(MalformedBytes, true);
	FElysiumSaveArchive MalformedLoad(MalformedReader, FElysiumSaveVersion::Latest); MalformedLoad << MalformedRead;
	TestTrue(TEXT("malformed Physics recipe propagates outer archive read failure"), MalformedReader.IsError());
	FElysiumMapSnapshot Truncated = Snapshot;
	const int32 TruncatedSize = RowBytes.Num() - 1;
	FMemory::Memcpy(Truncated.BlockStream.GetData() + RowOffset - 4, &TruncatedSize, sizeof(TruncatedSize));
	TArray<uint8> TruncatedBytes; FMemoryWriter TruncatedWriter(TruncatedBytes, true);
	FElysiumSaveArchive TruncatedSave(TruncatedWriter, FElysiumSaveVersion::Latest); TruncatedSave << Truncated;
	FElysiumMapSnapshot TruncatedRead; FMemoryReader TruncatedReader(TruncatedBytes, true);
	FElysiumSaveArchive TruncatedLoad(TruncatedReader, FElysiumSaveVersion::Latest); TruncatedLoad << TruncatedRead;
	TestTrue(TEXT("truncated Physics row propagates outer archive read failure"), TruncatedReader.IsError());
	for (int32 BadFilter = 0; BadFilter < 2; ++BadFilter)
	{
		FElysiumMapSnapshot InvalidFilter = Snapshot; InvalidFilter.BlockStream.Reset();
		if (BadFilter == 0) InvalidFilter.PhysicsBodies[0].CollisionResponses.Pop();
		else InvalidFilter.PhysicsBodies[0].CollisionResponses[0] = 255;
		TArray<uint8> InvalidBytes; FMemoryWriter InvalidWriter(InvalidBytes, true);
		FElysiumSaveArchive InvalidSave(InvalidWriter, FElysiumSaveVersion::Latest); InvalidSave << InvalidFilter;
		FElysiumMapSnapshot InvalidRead; FMemoryReader InvalidReader(InvalidBytes, true);
		FElysiumSaveArchive InvalidLoad(InvalidReader, FElysiumSaveVersion::Latest); InvalidLoad << InvalidRead;
		TestTrue(*FString::Printf(TEXT("actual malformed native filter archive refusal arm%d"), BadFilter), InvalidReader.IsError());
	}
	FElysiumMapSnapshot NativeFailure = Snapshot;
	NativeFailure.BlockStream.Reset();
	NativeFailure.PhysicsBodies.Last().Recipe.InertiaScale = 0.f;
	FElysiumEntityDefs FailureDefs = Defs;
	FElysiumEntityWorld FailedDestination(Map, nullptr, Services); FailedDestination.Load(MoveTemp(FailureDefs));
	AddExpectedError(TEXT("Physics restore"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("late actual native reconstruction failure refuses ApplySnapshot"), FailedDestination.ApplySnapshot(NativeFailure), INDEX_NONE);
	FElysiumEntity* PreviouslyRestored = FailedDestination.Resolve(FElysiumEntityHandle(MovingIndex, FailedDestination.GetEpoch()));
	FElysiumEntity* PreviouslyRestoredAlias = FailedDestination.Resolve(FElysiumEntityHandle(AliasIndex, FailedDestination.GetEpoch()));
	if (!PreviouslyRestored || !PreviouslyRestoredAlias) return false;
	TestNull(TEXT("failed packet retires earlier reconstructed model actor"), PreviouslyRestored->VPhysicsGetObject());
	TestNull(TEXT("failed packet retires earlier reconstructed base actor"), PreviouslyRestoredAlias->VPhysicsGetObject());
	FailedDestination.Teardown();
	auto* InvalidComponent = NewObject<UElysiumPhysicsPropComponent>(Map);
	Map->AddInstanceComponent(InvalidComponent); InvalidComponent->RegisterComponent();
	for (const FString& InvalidName : {FString(),FString(TEXT("unknown-current-semantic-material"))})
	{
		FElysiumSavedPhysicsBody InvalidMaterial = NativeState; InvalidMaterial.CurrentSurface = InvalidName;
		TestFalse(TEXT("malformed current semantic name refuses native reconstruction"),ElysiumModelPhysics::Restore(InvalidComponent,Destination.PhysicsSurfaces(),InvalidMaterial,Error));
		TestNull(TEXT("invalid semantic name fails before any native actor creation"),ElysiumModelPhysics::ActorIdentity(InvalidComponent));
		TestNull(TEXT("invalid semantic name creates no recipe"),ElysiumModelPhysics::RecipeFor(ElysiumModelPhysics::ActorIdentity(InvalidComponent)));
	}
	ElysiumPreparedProps::Release(Map);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicsBboxRecipeTest, "Elysium.Substrate.ModelPhysics.BboxRecipeService",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumPhysicsBboxRecipeTest::RunTest(const FString&)
{
	// Finite native service evidence only. No VPhysicsInitShadow/static/Spawn caller is claimed.
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Owner = Fixture.World->SpawnActor<AActor>();
	auto* Component = NewObject<UElysiumPhysicsPropComponent>(Owner);
	Component->RegisterComponent(); Owner->AddInstanceComponent(Component);
	FElysiumPhysicsSurfaceProps Surfaces;
	Surfaces.ParseSurfaceData(TEXT("bbox-service-authored"), TEXT("default { friction 0.8 elasticity 0.2 density 1000 }"));
	FElysiumPhysicsBodyRecipe Recipe;
	Recipe.Kind = EElysiumPhysicsRecipeKind::Bbox;
	Recipe.BboxMin = FVector(-11, -23, -7); Recipe.BboxMax = FVector(31, 17, 29);
	Recipe.Surface = TEXT("default"); Recipe.MassKg = 6.f;
	Recipe.InertiaScale = 2.f; Recipe.LinearDamping = .25f; Recipe.AngularDamping = .5f;
	Recipe.bMassCenter = true; Recipe.MassCenterCm = FVector(3, 2, 1);
	Component->SetWorldLocationAndRotation(FVector(70, 80, 90), FRotator(12, 34, 56));
	FString Error;
	if (!TestTrue(TEXT("actual native box creation from explicit authored fields"), ElysiumModelPhysics::CreateBbox(Component, Surfaces, Recipe, true, Error)))
	{ AddError(Error); return false; }
	void* Actor = ElysiumModelPhysics::ActorIdentity(Component);
	TestNotNull(TEXT("bbox actual actor output"), Actor);
	TestNotNull(TEXT("bbox recipe registers only successful native output"), ElysiumModelPhysics::RecipeFor(Actor));
	const FKBoxElem& Shape = Component->GetBodySetup()->AggGeom.BoxElems[0];
	TestTrue(TEXT("native shape uses authored center"), Shape.Center.Equals((Recipe.BboxMin + Recipe.BboxMax) * .5));
	TestTrue(TEXT("native shape uses authored dimensions"), FVector(Shape.X, Shape.Y, Shape.Z).Equals(Recipe.BboxMax - Recipe.BboxMin));
	TestEqual(TEXT("actual bbox mass"), Component->GetMass(), 6.f);
	FElysiumSavedPhysicsBody Captured;
	if (!ElysiumModelPhysics::Capture(Actor, Captured)) return false;
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes, true); Writer << Captured;
	FElysiumSavedPhysicsBody Persisted; FMemoryReader Reader(Bytes, true); Reader << Persisted;
	ElysiumModelPhysics::Destroy(Component);
	TestNull(TEXT("removal retires bbox recipe"), ElysiumModelPhysics::RecipeFor(Actor));
	TestFalse(TEXT("removal has no surviving static native actor"), Component->GetBodyInstance()->IsValidBodyInstance());
	if (!TestTrue(TEXT("persisted native bbox reconstruction"), ElysiumModelPhysics::Restore(Component, Surfaces, Persisted, Error))) return false;
	FElysiumSavedPhysicsBody Rebuilt;
	TestTrue(TEXT("rebuilt box has real saved recipe"), ElysiumModelPhysics::Capture(ElysiumModelPhysics::ActorIdentity(Component), Rebuilt));
	TestTrue(TEXT("reconstructed authored bbox"), Rebuilt.Recipe.BboxMin.Equals(Recipe.BboxMin) && Rebuilt.Recipe.BboxMax.Equals(Recipe.BboxMax));
	TestTrue(TEXT("reconstructed native pose"), Rebuilt.Pose.Equals(Captured.Pose, .001));
	TestTrue(TEXT("reconstructed sleeping state"), Rebuilt.bAsleep);
	TestTrue(TEXT("current-mass restore retains authored native COM"),
		ElysiumModelPhysics::Identity(Component)->GetMassSpaceLocal().GetTranslation().Equals(Recipe.MassCenterCm, .001));
	Recipe.BboxMax = Recipe.BboxMin;
	TestFalse(TEXT("invalid dimensions reject actual create"), ElysiumModelPhysics::CreateBbox(Component, Surfaces, Recipe, false, Error));
	TestNull(TEXT("failed replacement has no registered actual actor"), ElysiumModelPhysics::ActorIdentity(Component));
	TestFalse(TEXT("failed replacement has no native static actor"), Component->GetBodyInstance()->IsValidBodyInstance());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelShadowInputsTest, "Elysium.Substrate.ModelPhysics.ModelShadowInputs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelShadowInputsTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred();
	auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	const TArray<FString> Ids = {
		TEXT("vtmb:model:scenery/structural/doorknoboffice/doorknoboffice"),
		TEXT("vtmb:model:scenery/vehicles/lightningbird/trunklid"),
		TEXT("vtmb:model:scenery/structural/ceiling_fan/ceiling_fan_blades"),
		TEXT("vtmb:model:scenery/structural/doorknoba/doorknoba"),
		TEXT("vtmb:model:scenery/structural/doorknoba/drknobantique"),
		TEXT("vtmb:model:scenery/structural/doorknoba/drknobantiquel") };
	TSet<FSoftObjectPath> Paths; FString Error;
	if (!FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error)) { AddError(Error); return false; }
	TArray<UObject*> Assets; for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
	auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
	if (!Prepared) { AddError(Error); return false; }
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		const auto* Mesh = Prepared->StaticMesh(Ids[Index], Error);
		const auto* Inputs = UElysiumModelPhysicsInputs::Find(Mesh);
		AddInfo(FString::Printf(TEXT("model-shadow input: id=%s mesh=%s inputs=%p selected=%p source=%d"),
			*Ids[Index], *GetPathNameSafe(Mesh), Inputs, Inputs ? Inputs->SelectedBody.Get() : nullptr, Inputs && Inputs->bSourceBody));
		if (!TestNotNull(*FString::Printf(TEXT("authentic typed admission %s"), *Ids[Index]), Inputs)) continue;
		TestEqual(TEXT("source absence distinct from stale metadata"), Inputs->bSourceBody, Index < 3);
		if (Index < 3) TestTrue(TEXT("first authored .phy has real selected cook"), Inputs->SelectedBody && Inputs->SelectedBody->AggGeom.ConvexElems.Num() == 1);
	}
	ElysiumPreparedProps::Release(Map);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStaticModelCallerTest, "Elysium.Substrate.ModelPhysics.StaticModelCaller",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumStaticModelCallerTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred();
	auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	const FString CopCarId = TEXT("vtmb:model:scenery/vehicles/omnishpere/copcar");
	const TArray<FString> Ids = { CopCarId, ElysiumModelPhysicsTest::AbsentId, ElysiumModelPhysicsTest::BodyId };
	TSet<FSoftObjectPath> Paths; FString Error;
	if (!TestTrue(TEXT("shipped cop_car_1 and plus_chair preparation paths"), FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error)))
	{ AddError(Error); return false; }
	TArray<UObject*> Assets;
	for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
	auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
	if (!TestTrue(TEXT("production prepared static models"), Prepared.IsValid())) { AddError(Error); return false; }
	auto* Mesh = Prepared->StaticMesh(CopCarId, Error);
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(Mesh);
	AddInfo(FString::Printf(TEXT("static input admission: mesh=%s error=%s user_data=%p source_body=%d selected=%p"),
		*GetPathNameSafe(Mesh), *Error, Inputs, Inputs && Inputs->bSourceBody, Inputs ? Inputs->SelectedBody.Get() : nullptr));
	if (!TestTrue(TEXT("authentic imported .phy and selected cook"), Inputs && Inputs->bSourceBody && Inputs->SelectedBody)) return false;
	TestEqual(TEXT("cop_car_1 authored mass"), Inputs->MassKg, 680.f);
	TestEqual(TEXT("cop_car_1 authored raw surface"), Inputs->SurfaceProperty, FString(TEXT("metal")));
	TestEqual(TEXT("cop_car_1 first binary solid cook"), Inputs->SelectedBody->AggGeom.ConvexElems.Num(), 1);
	FElysiumWorldServices Services; Services.Embodiment = Map;
	FElysiumEntityWorld EntityWorld(Map, nullptr, Services); EntityWorld.Load(FElysiumEntityDefs());
	auto MakeDef = [](const TCHAR* ModelPath, const TCHAR* Name, const TCHAR* Solid, FVector Origin)
	{
		auto Result = ElysiumModelPhysicsTest::Def(ModelPath, Name);
		Result.Classname = TEXT("prop_dynamic"); Result.Origin = Origin;
		Result.Keys.Add(TEXT("solid"), Solid); Result.Keys.Add(TEXT("spawnflags"), TEXT("0"));
		Result.Keys.Add(TEXT("angles"), TEXT("17 31 23"));
		return Result;
	};
	const TCHAR* CopCarPath = TEXT("models/scenery/vehicles/OmniShpere/CopCar.mdl");
	auto* Prop = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MakeDef(CopCarPath, TEXT("cop_car_1"), TEXT("6"), FVector(5000,4000,3000))));
	if (!Prop || !TestNotNull(TEXT("production solid6 Spawn publishes static actor"), Prop->VPhysicsGetObject())) return false;
	void* Actor = Prop->VPhysicsGetObject();
	auto* Component = Cast<UElysiumPhysicsPropComponent>(ElysiumModelPhysics::ComponentFor(Actor));
	auto* Body = ElysiumModelPhysics::Identity(Component);
	const auto* Recipe = ElysiumModelPhysics::RecipeFor(Actor);
	if (!Component || !Body || !Recipe) return false;
	TestTrue(TEXT("body instance uses actual selected .phy setup after generic baseline"), Body->GetBodySetup() == Inputs->SelectedBody.Get());
	TestTrue(TEXT("selected setup distinct from render-builder setup"), Body->GetBodySetup() != Mesh->GetBodySetup());
	TestTrue(TEXT("registered identity is actual native actor"), Actor == ElysiumModelPhysics::ActorIdentity(Component));
	TestTrue(TEXT("creation-owned game data is actual prop"), ElysiumModelPhysics::GameDataForObject(Actor) == Prop);
	TestTrue(TEXT("registered static model recipe"), Recipe->Kind == EElysiumPhysicsRecipeKind::Model && Recipe->bStatic);
	TestEqual(TEXT("static parsed mass bypasses dynamic bias"), Recipe->MassKg, Inputs->MassKg);
	TestEqual(TEXT("retail post-parse static inertia override"), Recipe->InertiaScale, 1.f);
	TestFalse(TEXT("actual static component never simulates"), Component->IsSimulatingPhysics());
	TestTrue(TEXT("actual native static particle"), ElysiumModelPhysics::ActorIdentity(Component)->GetGameThreadAPI().ObjectState() == Chaos::EObjectStateType::Static);
	const FTransform ExpectedPose(ElysiumModelPhysics::SourceRotation(Prop->GetAbsAngles()), Prop->GetAbsOrigin(), Component->GetComponentScale());
	TestTrue(TEXT("static creation retains full absolute caller pose"), Body->GetUnrealWorldTransform().Equals(ExpectedPose, .001));
	TestEqual(TEXT("native authored linear damping"), Body->LinearDamping, .01f);
	TestEqual(TEXT("native authored angular damping"), Body->AngularDamping, 1.f);
	int32 NativeShapes = 0;
	FPhysicsCommand::ExecuteRead(Body->GetPhysicsActor(), [&](const FPhysicsActorHandle&)
	{
		TArray<FPhysicsShapeHandle> ShapeHandles;
		NativeShapes = Body->GetAllShapes_AssumesLocked(ShapeHandles);
	});
	TestEqual(TEXT("actual CopCar selected cook has one native convex shape and no render triangle shape"), NativeShapes, 1);
	void* Output = nullptr;
	TestEqual(TEXT("production base list returns one current body"), Prop->VPhysicsGetObjectList(&Output, 1), 1);
	TestTrue(TEXT("production base list outputs actual current actor"), Output == Actor);
	const auto* Material = Cast<UElysiumPhysicalMaterial>(Body->GetSimplePhysicalMaterial());
	int32 SurfaceIndex = EntityWorld.PhysicsSurfaces().GetSurfaceIndex(Inputs->SurfaceProperty);
	if (SurfaceIndex < 0) SurfaceIndex = EntityWorld.PhysicsSurfaces().GetSurfaceIndex(TEXT("default"));
	const auto* Surface = EntityWorld.PhysicsSurfaces().GetSurfaceData(SurfaceIndex);
	if (!Material || !Surface) return false;
	TestEqual(TEXT("raw source/default material lookup"), Material->SourceName, EntityWorld.PhysicsSurfaces().GetSurfaceName(SurfaceIndex));
	TestEqual(TEXT("native surface friction"), Material->Friction, Surface->Friction());
	TestEqual(TEXT("native surface restitution"), Material->Restitution, Surface->Elasticity());
	const FVector QueryCenter = Inputs->SelectedBody->AggGeom.CalcAABB(ExpectedPose).GetCenter();
	// Production retail queries (WorldGeometry346/511, MapActor1913) select native simple
	// collision. UWorld's omitted-parameter default selects COMPLEX, unlike a fresh Params.
	const FCollisionQueryParams SelectedQuery(FName(TEXT("ElysiumStaticSelectedBody")), false);
	const FVector BboxOrigin(18000,17000,16000);
	auto* BboxControl = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MakeDef(CopCarPath, TEXT("same_owner_bbox_control"), TEXT("2"), BboxOrigin)));
	if (!BboxControl || !TestNotNull(TEXT("same fixture accepted bbox producer"), BboxControl->VPhysicsGetObject())) return false;
	auto* BboxComponent = ElysiumModelPhysics::ComponentFor(BboxControl->VPhysicsGetObject());
	TestTrue(TEXT("bbox control and selected model share actual owner/world"), BboxComponent && BboxComponent->GetOwner() == Component->GetOwner()
		&& BboxComponent->GetWorld() == Component->GetWorld());
	auto TraceBody = [&]()
	{
		FHitResult Hit;
		const FVector Start = QueryCenter - FVector(1000,0,0), End = QueryCenter + FVector(1000,0,0);
		const bool bWorldHit = Fixture.World->LineTraceSingleByChannel(Hit, Start, End, ElysiumCollision::SightChannel, SelectedQuery);
		int32 QueryShapes = 0;
		FPhysicsCommand::ExecuteRead(Body->GetPhysicsActor(), [&](const FPhysicsActorHandle&)
		{
			TArray<FPhysicsShapeHandle> Handles; Body->GetAllShapes_AssumesLocked(Handles);
			for (const auto& Shape : Handles) if (FPhysicsInterface::IsQueryShape(Shape)) ++QueryShapes;
		});
		AddInfo(FString::Printf(TEXT("selected native query: shown=%d shapes=%d world_hit=%d selected_hit=%d identity_current=%d"),
			!Prop->IsHidden(), QueryShapes, bWorldHit, Hit.GetComponent() == Component, Prop->VPhysicsGetObject() == Actor));
		return Hit.GetComponent() == Component;
	};
	if (!TestTrue(TEXT("first actual native fixture frame"), Fixture.TestWorld.TickTestWorld(1.f / 60.f))
		|| !TestTrue(TEXT("following native fixture publication frame"), Fixture.TestWorld.TickTestWorld(1.f / 60.f))) return false;
	const FVector BboxCenter = BboxOrigin + (Inputs->StudioMinsCm + Inputs->StudioMaxsCm) * .5;
	FHitResult BboxHit;
	Fixture.World->LineTraceSingleByChannel(BboxHit, BboxCenter - FVector(1000,0,0), BboxCenter + FVector(1000,0,0), ElysiumCollision::SightChannel, SelectedQuery);
	TestTrue(TEXT("same fixture native scene query reaches accepted bbox"), BboxHit.GetComponent() == BboxComponent);
	TestTrue(TEXT("real query reaches selected static geometry"), TraceBody());
	Prop->ScriptHide(); TestFalse(TEXT("retail hide gates actual native shapes"), TraceBody());
	TestTrue(TEXT("hide preserves static native identity"), Prop->VPhysicsGetObject() == Actor);
	Prop->ScriptUnhide(); TestTrue(TEXT("retail unhide restores native query"), TraceBody());
	TestTrue(TEXT("unhide preserves static native identity"), Prop->VPhysicsGetObject() == Actor);
	FElysiumSavedPhysicsBody State;
	TestTrue(TEXT("capture reads actual static model body"), ElysiumModelPhysics::Capture(Actor, State));
	TestTrue(TEXT("capture retains actual static recipe and pose"), State.Recipe.bStatic && State.Recipe.Kind == EElysiumPhysicsRecipeKind::Model && State.Pose.Equals(ExpectedPose, .001));
	TestEqual(TEXT("native capture retains authored semantic mass"), State.CurrentMassKg, 680.f);
	AddInfo(FString::Printf(TEXT("static branch: model=%s setup=%p selected=%p actor=%p incarnation=%llu native_shapes=%d mass=%g inertia=%g surface=%s pose=%s capture=%d"),
		*Recipe->ModelAsset, Body->GetBodySetup(), Inputs->SelectedBody.Get(), Actor, ElysiumModelPhysics::CaptureIdentity(Actor).Incarnation,
		NativeShapes, Recipe->MassKg, Recipe->InertiaScale, *State.CurrentSurface, *State.Pose.ToString(), State.Recipe.bStatic));
	// Actual shipped plus_chair source has no binary .phy; visual fallback cannot become a body.
	const FVector AbsentOrigin(10000,9000,8000);
	auto* Absent = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MakeDef(TEXT("models/scenery/structural/society/chair.mdl"), TEXT("plus_chair"), TEXT("6"), AbsentOrigin)));
	auto* AbsentMesh = Prepared->StaticMesh(ElysiumModelPhysicsTest::AbsentId, Error);
	const auto* AbsentInputs = UElysiumModelPhysicsInputs::Find(AbsentMesh);
	if (!Absent || !TestTrue(TEXT("shipped no-binary input really lacks .phy"), AbsentInputs && !AbsentInputs->bSourceBody)) return false;
	TestNull(TEXT("shipped no-binary caller assigns null"), Absent->VPhysicsGetObject());
	TestEqual(TEXT("no-binary base list returns zero"), Absent->VPhysicsGetObjectList(nullptr, 1), 0);
	TInlineComponentArray<UStaticMeshComponent*> Components(Map);
	int32 AbsentComponents = 0;
	for (auto* Art : Components)
		if (Art->GetStaticMesh() == AbsentMesh && Art->GetComponentLocation().Equals(AbsentOrigin, .01))
		{
			++AbsentComponents;
			TestFalse(TEXT("no-binary visual fallback has no native actor"), Art->GetBodyInstance()->IsValidBodyInstance());
		}
	TestTrue(TEXT("negative caller retains actual visual carrier"), AbsentComponents > 0);
	FHitResult AbsentHit;
	const FVector AbsentCenter = AbsentMesh->GetBounds().Origin + AbsentOrigin;
	TestFalse(TEXT("native query passes through no-binary art"), Fixture.World->LineTraceSingleByChannel(AbsentHit,
		AbsentCenter - FVector(500,0,0), AbsentCenter + FVector(500,0,0), ElysiumCollision::SightChannel, SelectedQuery));
	auto* NonSolid = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MakeDef(CopCarPath, TEXT("solid0_control"), TEXT("0"), FVector(14000,13000,12000))));
	if (!NonSolid) return false;
	TestNull(TEXT("authored solid0 remains nonphysical despite available .phy"), NonSolid->VPhysicsGetObject());
	TestEqual(TEXT("solid0 has no current-object list"), NonSolid->VPhysicsGetObjectList(nullptr, 1), 0);
	AddInfo(FString::Printf(TEXT("negative branch: no-binary=%s native_list=%d art_carriers=%d solid0_list=%d"),
		*AbsentMesh->GetPathName(), Absent->VPhysicsGetObjectList(nullptr, 1), AbsentComponents, NonSolid->VPhysicsGetObjectList(nullptr, 1)));
	// Join the current-reference, retirement and persisted Physics consumers in THIS world.
	auto* Hidden = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MakeDef(CopCarPath, TEXT("saved_hidden"), TEXT("3"), FVector(22000,21000,20000))));
	auto* Bodyless = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MakeDef(CopCarPath, TEXT("saved_absent"), TEXT("6"), FVector(26000,25000,24000))));
	auto* Failed = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(MakeDef(CopCarPath, TEXT("failed_replacement"), TEXT("6"), FVector(30000,29000,28000))));
	auto* Dynamic = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(ElysiumModelPhysicsTest::Def(
		TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("joined_dynamic"))));
	FElysiumEntityDef AliasDef; AliasDef.Classname = TEXT("info_target");
	auto* Alias = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(AliasDef));
	auto* Borrower = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(AliasDef));
	auto* Parent = EntityWorld.Resolve(EntityWorld.SpawnRuntimeEntity(AliasDef));
	const auto ShadowHandle = EntityWorld.CreateRuntimeEntityNoSpawn(MakeDef(CopCarPath, TEXT("joined_shadow"), TEXT("2"), FVector(34000,33000,32000)));
	auto* Shadow = EntityWorld.Resolve(ShadowHandle);
	if (!Hidden || !Bodyless || !Failed || !Dynamic || !Alias || !Borrower || !Parent || !Shadow) return false;
	Shadow->SetParent(Parent, 0); EntityWorld.CallEntitySpawn(*Shadow);
	void* ShadowActor = Shadow->VPhysicsGetObject();
	if (!TestTrue(TEXT("joined reached parent bbox supplies actual shadow owner"), ShadowActor && ElysiumModelPhysics::ShadowFor(ShadowActor)
		&& EntityWorld.PhysicsShadows().Contains(ShadowHandle))) return false;
#if WITH_EDITOR
	Borrower->VPhysicsSetObject(ShadowActor);
	const auto ShadowCompileToken = ElysiumModelPhysics::CaptureIdentity(ShadowActor);
	auto* ShadowCarrier = Cast<UElysiumPhysicsPropComponent>(ElysiumModelPhysics::ComponentFor(ShadowActor));
	const auto* BeforeCompileController = ElysiumModelPhysics::ShadowFor(ShadowActor);
	if (!ShadowCarrier) return false;
	ShadowCarrier->PreStaticMeshCompilation(); ShadowCarrier->PostStaticMeshCompilation();
	TestTrue(TEXT("actual analytic compilation callbacks preserve current actor and aliases"), Shadow->VPhysicsGetObject() == ShadowActor
		&& Borrower->VPhysicsGetObject() == ShadowActor && ElysiumModelPhysics::IsCurrentOutput(ShadowCompileToken));
	TestTrue(TEXT("actual analytic compilation callbacks preserve recipe/controller/owner membership"), ElysiumModelPhysics::RecipeFor(ShadowActor)
		&& ElysiumModelPhysics::ShadowFor(ShadowActor) == BeforeCompileController && EntityWorld.PhysicsShadows().Contains(ShadowHandle));
	Borrower->VPhysicsSwapObject(nullptr);
	auto* BboxCarrier = Cast<UElysiumPhysicsPropComponent>(BboxComponent);
	if (!BboxCarrier) return false;
	void* BeforeCompileBbox = BboxControl->VPhysicsGetObject();
	BboxCarrier->PreStaticMeshCompilation(); BboxCarrier->PostStaticMeshCompilation();
	TestTrue(TEXT("analytic compilation callback preserves actual static bbox current identity"), BboxControl->VPhysicsGetObject() == BeforeCompileBbox
		&& ElysiumModelPhysics::RecipeFor(BeforeCompileBbox));
	FHitResult CompiledBboxHit;
	Fixture.World->LineTraceSingleByChannel(CompiledBboxHit, BboxCenter - FVector(1000,0,0), BboxCenter + FVector(1000,0,0), ElysiumCollision::SightChannel, SelectedQuery);
	TestTrue(TEXT("analytic static query remains active after actual compilation callback"), CompiledBboxHit.GetComponent() == BboxCarrier);

#endif
	Shadow->VPhysicsSwapObject(nullptr);
	TestFalse(TEXT("actual shadow OWNER null Swap removes owner membership"), EntityWorld.PhysicsShadows().Contains(ShadowHandle));
	TestTrue(TEXT("owner null Swap retains native controller and producer"), ElysiumModelPhysics::ShadowFor(ShadowActor)
		&& ElysiumModelPhysics::Identity(ElysiumModelPhysics::ComponentFor(ShadowActor)));
	Shadow->VPhysicsSetObject(ShadowActor);
	Alias->VPhysicsSetObject(Actor);
	Alias->VPhysicsSetObject(Dynamic->VPhysicsGetObject());
	TestTrue(TEXT("SetObject replacement preserves former static producer"), ElysiumModelPhysics::ActorIdentity(Component) == Actor);
	Alias->VPhysicsSwapObject(nullptr);
	TestNotNull(TEXT("reference-only clear preserves dynamic producer"), ElysiumModelPhysics::ComponentFor(Dynamic->VPhysicsGetObject()));
	Alias->VPhysicsSetObject(Actor); // independently admitted saved field
	const auto AbsentToken = ElysiumModelPhysics::CaptureIdentity(Bodyless->VPhysicsGetObject());
	Borrower->VPhysicsSetObject(Bodyless->VPhysicsGetObject());
	Borrower->VPhysicsDestroyObject();
	TestNull(TEXT("borrowed225 clears actual static producer reference"), Bodyless->VPhysicsGetObject());
	TestNull(TEXT("borrowed225 clears supplied borrower"), Borrower->VPhysicsGetObject());
	TestFalse(TEXT("borrowed225 fences stale native output incarnation"), ElysiumModelPhysics::IsCurrentOutput(AbsentToken));
	Bodyless->ScriptHide(); Bodyless->ScriptUnhide(); Bodyless->SetRuntimeOrigin(FVector(26001,25000,24000));
	TestNull(TEXT("retired static producer gates do not resurrect"), Bodyless->VPhysicsGetObject());
	const auto FailedToken = ElysiumModelPhysics::CaptureIdentity(Failed->VPhysicsGetObject());
	Failed->SetRuntimeModel(CopCarPath);
	TestNotNull(TEXT("successful actual SetModel replacement publishes native body"), Failed->VPhysicsGetObject());
	TestFalse(TEXT("successful replacement rejects previous native incarnation even on address reuse"), ElysiumModelPhysics::IsCurrentOutput(FailedToken));
	const auto ReplacementToken = ElysiumModelPhysics::CaptureIdentity(Failed->VPhysicsGetObject());
	Failed->SetRuntimeModel(TEXT("models/scenery/structural/society/chair.mdl"));
	TestNull(TEXT("actual failed no-binary model replacement assigns null"), Failed->VPhysicsGetObject());
	TestFalse(TEXT("failed replacement retires old native incarnation"), ElysiumModelPhysics::IsCurrentOutput(ReplacementToken));
	// Native Physics Save owns solved pose, independently of the renderer/logical reconnect.
	const FTransform SavedPose(FRotator(13,29,7), FVector(7000,6000,5000));
	Body->SetBodyTransform(SavedPose, ETeleportType::TeleportPhysics);
	Hidden->ScriptHide();
	TestTrue(TEXT("hidden restore stimulus comes from actual retail entity state"), Hidden->IsHidden());
	const int32 ShownIndex = Prop->Handle.Index, HiddenIndex = Hidden->Handle.Index, BodylessIndex = Bodyless->Handle.Index;
	const int32 FailedIndex = Failed->Handle.Index, AliasIndex = Alias->Handle.Index, DynamicIndex = Dynamic->Handle.Index;
	const int32 BboxIndex = BboxControl->Handle.Index, ShadowIndex = Shadow->Handle.Index;
	FElysiumSavedPhysicsBody SavedStatic, SavedHidden;
	if (!ElysiumModelPhysics::Capture(Actor, SavedStatic) || !ElysiumModelPhysics::Capture(Hidden->VPhysicsGetObject(), SavedHidden)) return false;
	FElysiumMapSnapshot Snapshot; EntityWorld.Freeze(Snapshot);
	if (!TestFalse(TEXT("joined real Physics Freeze admits static/model/bbox/shadow fields"), Snapshot.bPhysicsSaveFailed)) return false;
	const auto* SavedAbsence = Snapshot.PhysicsBodies.FindByPredicate([BodylessIndex](const auto& Row) { return Row.EntityIndex == BodylessIndex; });
	TestTrue(TEXT("real Freeze saves explicit absent current field"), SavedAbsence && !SavedAbsence->bHasObject);
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes, true); FElysiumSaveArchive SaveAr(Writer, FElysiumSaveVersion::Latest); SaveAr << Snapshot;
	FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes, true); FElysiumSaveArchive LoadAr(Reader, FElysiumSaveVersion::Latest); LoadAr << Persisted;
	if (!TestFalse(TEXT("joined typed Physics archive decode"), Reader.IsError())) return false;
	const auto OldToken = ElysiumModelPhysics::CaptureIdentity(Actor);
	EntityWorld.Teardown();
	TestFalse(TEXT("source teardown retires shown static incarnation"), ElysiumModelPhysics::IsCurrentOutput(OldToken));
	FElysiumEntityWorld Destination(Map, nullptr, Services); Destination.Load(FElysiumEntityDefs());
	Persisted.PhysicsBodies.Reset();
	if (!TestTrue(TEXT("inactive ApplySnapshot consumes actual persisted Physics block"), Destination.ApplySnapshot(Persisted) >= 0)) return false;
	auto ResolveSaved = [&](int32 Index) { return Destination.Resolve(FElysiumEntityHandle(Index, Destination.GetEpoch())); };
	auto* Restored = ResolveSaved(ShownIndex); auto* RestoredHidden = ResolveSaved(HiddenIndex);
	auto* RestoredAbsent = ResolveSaved(BodylessIndex); auto* RestoredFailed = ResolveSaved(FailedIndex);
	auto* RestoredAlias = ResolveSaved(AliasIndex); auto* RestoredDynamic = ResolveSaved(DynamicIndex);
	auto* RestoredBbox = ResolveSaved(BboxIndex); auto* RestoredShadow = ResolveSaved(ShadowIndex);
	if (!Restored || !RestoredHidden || !RestoredAbsent || !RestoredFailed || !RestoredAlias || !RestoredDynamic || !RestoredBbox || !RestoredShadow) return false;
	FElysiumSavedPhysicsBody RestoredState, AliasState, HiddenState;
	if (!TestTrue(TEXT("restored actual Model+static registered body"), ElysiumModelPhysics::Capture(Restored->VPhysicsGetObject(), RestoredState))
		|| !TestTrue(TEXT("restored actual independently allocated alias body"), ElysiumModelPhysics::Capture(RestoredAlias->VPhysicsGetObject(), AliasState))
		|| !TestTrue(TEXT("restored actual hidden static body"), ElysiumModelPhysics::Capture(RestoredHidden->VPhysicsGetObject(), HiddenState))) return false;
	TestTrue(TEXT("static solved pose survives art/logical reconnect before first think"), RestoredState.Pose.Equals(SavedPose, .001)
		&& !RestoredState.Pose.GetLocation().Equals(Restored->GetAbsOrigin()));
	TestTrue(TEXT("independent saved alias retains solved static pose"), AliasState.Pose.Equals(SavedPose, .001)
		&& RestoredAlias->VPhysicsGetObject() != Restored->VPhysicsGetObject());
	TestTrue(TEXT("static saved semantic material/mass/damping survive"), RestoredState.CurrentSurface == SavedStatic.CurrentSurface
		&& RestoredState.CurrentMassKg == SavedStatic.CurrentMassKg && RestoredState.CurrentLinearDamping == SavedStatic.CurrentLinearDamping);
	TestTrue(TEXT("hidden entity saved state survives reconnect"), RestoredHidden->IsHidden());
	TestTrue(TEXT("hidden native solved pose survives reconnect"), HiddenState.Pose.Equals(SavedHidden.Pose, .001));
	TestNull(TEXT("saved borrowed225 absence survives Spawn/reconnect"), RestoredAbsent->VPhysicsGetObject());
	TestNull(TEXT("saved failed replacement remains bodyless"), RestoredFailed->VPhysicsGetObject());
	TestTrue(TEXT("joined dynamic control reconstructed actual Model body"), ElysiumModelPhysics::RecipeFor(RestoredDynamic->VPhysicsGetObject())
		&& !ElysiumModelPhysics::RecipeFor(RestoredDynamic->VPhysicsGetObject())->bStatic);
	TestTrue(TEXT("joined static bbox control retains recipe"), ElysiumModelPhysics::RecipeFor(RestoredBbox->VPhysicsGetObject())
		&& ElysiumModelPhysics::RecipeFor(RestoredBbox->VPhysicsGetObject())->Kind == EElysiumPhysicsRecipeKind::Bbox);
	TestTrue(TEXT("joined actual owner Swap absence restores controller without membership"), ElysiumModelPhysics::ShadowFor(RestoredShadow->VPhysicsGetObject())
		&& !Destination.PhysicsShadows().Contains(RestoredShadow->Handle));
	auto QuerySaved = [&](FElysiumEntity* Entity, const FTransform& Pose)
	{
		const FVector Center = Inputs->SelectedBody->AggGeom.CalcAABB(Pose).GetCenter(); FHitResult Hit;
		Fixture.World->LineTraceSingleByChannel(Hit, Center - FVector(1000,0,0), Center + FVector(1000,0,0), ElysiumCollision::SightChannel, SelectedQuery);
		return Hit.GetComponent() == ElysiumModelPhysics::ComponentFor(Entity->VPhysicsGetObject());
	};
	TestFalse(TEXT("restored hidden native geometry stays disabled"), QuerySaved(RestoredHidden, HiddenState.Pose));
	auto* HiddenBody = ElysiumModelPhysics::Identity(ElysiumModelPhysics::ComponentFor(RestoredHidden->VPhysicsGetObject()));
	int32 HiddenActiveShapes = 0;
	FPhysicsCommand::ExecuteRead(HiddenBody->GetPhysicsActor(), [&](const FPhysicsActorHandle&)
	{
		TArray<FPhysicsShapeHandle> Shapes; HiddenBody->GetAllShapes_AssumesLocked(Shapes);
		for (const auto& Shape : Shapes)
			if (FPhysicsInterface::IsQueryShape(Shape) || FPhysicsInterface::IsSimulationShape(Shape)) ++HiddenActiveShapes;
	});
	TestEqual(TEXT("restored hidden state disables every actual query/contact shape"), HiddenActiveShapes, 0);
	RestoredHidden->ScriptUnhide(); TestTrue(TEXT("actual restored Unhide enables selected native geometry"), QuerySaved(RestoredHidden, HiddenState.Pose));
	int32 RestoredOwnedBodies = 0, AbsentOwnedBodies = 0;
	for (UPrimitiveComponent* NativeComponent : ElysiumModelPhysics::WorldComponents(Destination))
	{
		FElysiumEntity* OwnerEntity = ElysiumModelPhysics::GameDataForObject(ElysiumModelPhysics::ActorIdentity(NativeComponent));
		if (OwnerEntity == Restored) ++RestoredOwnedBodies;
		if (OwnerEntity == RestoredAbsent || OwnerEntity == RestoredFailed) ++AbsentOwnedBodies;
	}
	TestEqual(TEXT("reconnect has exactly one native static body for shown producer"), RestoredOwnedBodies, 1);
	TestEqual(TEXT("saved absent/failure creates no duplicate native baseline"), AbsentOwnedBodies, 0);
	TInlineComponentArray<UStaticMeshComponent*> RestoredComponents(Map);
	int32 InertArtAllocations = 0, VisibleShownArt = 0, SelectedStaticNativeBodies = 0;
	for (UStaticMeshComponent* Art : RestoredComponents)
	{
		if (Art->GetStaticMesh() != Mesh) continue;
		if (Art->IsVisible() && Art->GetComponentLocation().Equals(Restored->GetAbsOrigin(), .01)) ++VisibleShownArt;
		if (!Art->GetBodyInstance()->IsValidBodyInstance()) continue;
		void* CountedActor = ElysiumModelPhysics::ActorIdentity(Art);
		const auto* CountedOwner = ElysiumModelPhysics::GameDataForObject(CountedActor);
		if (ElysiumModelPhysics::ComponentFor(CountedActor))
		{
			++SelectedStaticNativeBodies;
			TestTrue(TEXT("every registered CopCar body belongs to one independent saved field"),
				CountedOwner == Restored || CountedOwner == RestoredHidden || CountedOwner == RestoredAlias);
			TestTrue(TEXT("each saved field points to its registered native actor"), CountedOwner && CountedOwner->VPhysicsGetObject() == CountedActor);
			TestTrue(TEXT("every reconstructed static field uses actual selected geometry"), Art->GetBodyInstance()->GetBodySetup() == Inputs->SelectedBody.Get());
			TestNotNull(TEXT("registered static field retains saved recipe"), ElysiumModelPhysics::RecipeFor(CountedActor));
			TestFalse(TEXT("saved native carrier remains separate from visible art"), Art->IsVisible());
			continue;
		}
		// UE may retain an inert editor art allocation; it is not a retail physics object.
		++InertArtAllocations;
		TestNull(TEXT("unregistered art has no retail game data"), CountedOwner);
		TestNull(TEXT("unregistered art has no body recipe"), ElysiumModelPhysics::RecipeFor(CountedActor));
		int32 ArtQueryShapes = 0, ArtSimShapes = 0;
		FPhysicsCommand::ExecuteRead(Art->GetBodyInstance()->GetPhysicsActor(), [&](const FPhysicsActorHandle&)
		{
			TArray<FPhysicsShapeHandle> ArtShapes; Art->GetBodyInstance()->GetAllShapes_AssumesLocked(ArtShapes);
			for (const auto& ArtShape : ArtShapes)
			{
				ArtQueryShapes += FPhysicsInterface::IsQueryShape(ArtShape) ? 1 : 0;
				ArtSimShapes += FPhysicsInterface::IsSimulationShape(ArtShape) ? 1 : 0;
			}
		});
		const FVector ArtCenter = Art->GetBodyInstance()->GetBodySetup()->AggGeom.CalcAABB(Art->GetBodyInstance()->GetUnrealWorldTransform()).GetCenter();
		FHitResult ArtSightHit, ArtVisibilityHit, ArtObjectHit;
		Fixture.World->LineTraceSingleByChannel(ArtSightHit, ArtCenter - FVector(1000,0,0), ArtCenter + FVector(1000,0,0), ElysiumCollision::SightChannel, SelectedQuery);
		Fixture.World->LineTraceSingleByChannel(ArtVisibilityHit, ArtCenter - FVector(1000,0,0), ArtCenter + FVector(1000,0,0), ECC_Visibility, SelectedQuery);
		FCollisionObjectQueryParams RetailObjects; RetailObjects.AddObjectTypesToQuery(ECC_Pawn); RetailObjects.AddObjectTypesToQuery(ElysiumCollision::PlayerChannel);
		Fixture.World->LineTraceSingleByObjectType(ArtObjectHit, ArtCenter - FVector(1000,0,0), ArtCenter + FVector(1000,0,0), RetailObjects, SelectedQuery);
		TestEqual(TEXT("inert art has zero query shapes"), ArtQueryShapes, 0);
		TestEqual(TEXT("inert art has zero simulation/contact shapes"), ArtSimShapes, 0);
		TestFalse(TEXT("retail Sight query cannot hit inert art"), ArtSightHit.GetComponent() == Art);
		TestFalse(TEXT("Visibility query cannot hit inert art"), ArtVisibilityHit.GetComponent() == Art);
		TestFalse(TEXT("retail game object query cannot hit inert art"), ArtObjectHit.GetComponent() == Art);
	}
	TestEqual(TEXT("shown reconnect retains exactly one visible art carrier"), VisibleShownArt, 1);
	TestEqual(TEXT("all three independent static saved fields reconstruct one native body each"), SelectedStaticNativeBodies, 3);
	FHitResult OldArtHit;
	const FVector OldArtCenter = Inputs->SelectedBody->AggGeom.CalcAABB(ExpectedPose).GetCenter();
	TestFalse(TEXT("saved solved pose leaves no active collider at old logical/art pose"), Fixture.World->LineTraceSingleByChannel(OldArtHit,
		OldArtCenter - FVector(1000,0,0), OldArtCenter + FVector(1000,0,0), ElysiumCollision::SightChannel, SelectedQuery));
	AddInfo(FString::Printf(TEXT("reconnect census: registered_selected=%d visible_shown_art=%d inert_editor_allocations=%d inert_query_simulation=0"), SelectedStaticNativeBodies, VisibleShownArt, InertArtAllocations));
	const auto RestoredToken = ElysiumModelPhysics::CaptureIdentity(Restored->VPhysicsGetObject());
	const auto IndependentAliasToken = ElysiumModelPhysics::CaptureIdentity(RestoredAlias->VPhysicsGetObject());
	const auto BboxToken = ElysiumModelPhysics::CaptureIdentity(RestoredBbox->VPhysicsGetObject());
	const auto ShadowToken = ElysiumModelPhysics::CaptureIdentity(RestoredShadow->VPhysicsGetObject());
	UPrimitiveComponent* RestoredComponent = ElysiumModelPhysics::ComponentFor(Restored->VPhysicsGetObject());
	RestoredAlias->VPhysicsSetObject(Restored->VPhysicsGetObject()); RestoredAlias->VPhysicsDestroyObject();
	TestNull(TEXT("restored borrowed225 clears real producer reference"), Restored->VPhysicsGetObject());
	TestFalse(TEXT("restored borrowed225 releases actual backend before any gate"), RestoredComponent->GetBodyInstance()->IsValidBodyInstance());
	TestFalse(TEXT("restored borrowed225 rejects stale incarnation"), ElysiumModelPhysics::IsCurrentOutput(RestoredToken));
	TestTrue(TEXT("saved alias replacement preserves its former independently reconstructed producer"), ElysiumModelPhysics::IsCurrentOutput(IndependentAliasToken));
	Restored->ScriptHide(); Restored->ScriptUnhide(); Restored->SetRuntimeOrigin(FVector(1,2,3));
	TestFalse(TEXT("saved producer gates cannot resurrect retired native body"), RestoredComponent->GetBodyInstance()->IsValidBodyInstance());
	const auto HiddenToken = ElysiumModelPhysics::CaptureIdentity(RestoredHidden->VPhysicsGetObject());
	RestoredHidden->Kill(); TestFalse(TEXT("actual static Kill retires native incarnation"), ElysiumModelPhysics::IsCurrentOutput(HiddenToken));
	const auto DynamicToken = ElysiumModelPhysics::CaptureIdentity(RestoredDynamic->VPhysicsGetObject());
	Destination.Teardown(); TestFalse(TEXT("joined world teardown retires surviving dynamic incarnation"), ElysiumModelPhysics::IsCurrentOutput(DynamicToken));
	TestFalse(TEXT("joined teardown retires former independent alias producer"), ElysiumModelPhysics::IsCurrentOutput(IndependentAliasToken));
	TestFalse(TEXT("joined teardown retires static bbox producer"), ElysiumModelPhysics::IsCurrentOutput(BboxToken));
	TestFalse(TEXT("joined teardown retires retained shadow controller producer"), ElysiumModelPhysics::IsCurrentOutput(ShadowToken));
	AddInfo(FString::Printf(TEXT("joined static contract: archive_bytes=%d static_owned=%d absent_owned=%d independent_alias=1 shadow_owner_swap=1 stale_release=1"), Bytes.Num(), RestoredOwnedBodies, AbsentOwnedBodies));
	ElysiumPreparedProps::Release(Map);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStaticBboxCallerTest, "Elysium.Substrate.ModelPhysics.StaticBboxCaller",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumStaticBboxCallerTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred();
	auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	TSet<FSoftObjectPath> Paths; FString Error;
	if (!FElysiumPreparedPropModels::GatherPaths(Placed, Skins, { ElysiumModelPhysicsTest::BodyId }, Paths, Error)) { AddError(Error); return false; }
	TArray<UObject*> Assets;
	for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
	auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins,
		{ ElysiumModelPhysicsTest::BodyId }, Assets, Error);
	if (!TestTrue(TEXT("actual production model preparation"), Prepared.IsValid())) return false;
	const auto* Inputs = UElysiumModelPhysicsInputs::Find(ElysiumModelPhysicsTest::BodyMesh());
	if (!TestTrue(TEXT("public import supplies actual MDL hull bounds"), Inputs && Inputs->bStudioBounds)) return false;
	FElysiumWorldServices Services; Services.Embodiment = Map;
	FElysiumEntityDefs Defs; Defs.MapName = TEXT("static_bbox_native");
	FElysiumEntityWorld Source(Map, nullptr, Services); Source.Load(FElysiumEntityDefs(Defs));
	auto MakeDef = [](const TCHAR* Name)
	{
		auto Result = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), Name);
		Result.Classname = TEXT("prop_dynamic"); Result.Keys.Add(TEXT("solid"), TEXT("2"));
		Result.Keys.Add(TEXT("angles"), TEXT("17 31 23")); Result.Keys.Add(TEXT("spawnflags"), TEXT("0"));
		Result.Origin = FVector(5000, 4000, 3000);
		return Result;
	};
	auto* Prop = Source.Resolve(Source.SpawnRuntimeEntity(MakeDef(TEXT("static_bbox"))));
	if (!Prop || !TestNotNull(TEXT("actual prop_dynamic Spawn static actor"), Prop->VPhysicsGetObject())) return false;
	void* Actor = Prop->VPhysicsGetObject();
	auto* Component = ElysiumModelPhysics::ComponentFor(Actor);
	auto* Body = ElysiumModelPhysics::Identity(Component);
	const auto* Recipe = ElysiumModelPhysics::RecipeFor(Actor);
	if (!Body || !Recipe) return false;
	TestFalse(TEXT("static identity does not imply dynamic simulation"), Component->IsSimulatingPhysics());
	TestTrue(TEXT("actual Chaos backend static state"), ElysiumModelPhysics::ActorIdentity(Component)->GetGameThreadAPI().ObjectState() == Chaos::EObjectStateType::Static);
	TestTrue(TEXT("registered authored static bbox recipe"), Recipe->bStatic && Recipe->Kind == EElysiumPhysicsRecipeKind::Bbox);
	TestTrue(TEXT("native shape bounds come from authored MDL hull"), Recipe->BboxMin.Equals(Inputs->StudioMinsCm, .001) && Recipe->BboxMax.Equals(Inputs->StudioMaxsCm, .001));
	TestEqual(TEXT("raw MDL surface independent from .phy surface"), Recipe->Surface, Inputs->StudioSurfaceProperty);
	TestEqual(TEXT("null-parameter mass override retained as static input"), Recipe->MassKg, 85.f);
	TestEqual(TEXT("null-parameter inertia override retained as static input"), Recipe->InertiaScale, 1.0e24f);
	TestEqual(TEXT("actual native linear damping input"), Body->LinearDamping, .1f);
	TestEqual(TEXT("actual native angular damping input"), Body->AngularDamping, .1f);
	TestTrue(TEXT("bbox creation has zero native angles despite rotated visual"), Body->GetUnrealWorldTransform().GetRotation().Equals(FQuat::Identity, .001));
	const auto* Material = Cast<UElysiumPhysicalMaterial>(Body->GetSimplePhysicalMaterial());
	TestNotNull(TEXT("native static material produced"), Material);
	int32 SurfaceIndex = Inputs->StudioSurfaceProperty.IsEmpty() ? -1 : Source.PhysicsSurfaces().GetSurfaceIndex(Inputs->StudioSurfaceProperty);
	if (SurfaceIndex < 0) SurfaceIndex = Source.PhysicsSurfaces().GetSurfaceIndex(TEXT("default"));
	const auto* Surface = Source.PhysicsSurfaces().GetSurfaceData(SurfaceIndex);
	if (!Material || !Surface) return false;
	TestEqual(TEXT("actual static native material friction"), Material->Friction, Surface->Friction());
	TestEqual(TEXT("actual static native material restitution"), Material->Restitution, Surface->Elasticity());
	TestEqual(TEXT("raw-surface lookup/default order reflected in native material"), Material->SourceName, Source.PhysicsSurfaces().GetSurfaceName(SurfaceIndex));
	auto TraceBody = [&](const FVector& Location)
	{
		const FVector Center = Location + (Inputs->StudioMinsCm + Inputs->StudioMaxsCm) * .5;
		FHitResult Hit;
		Fixture.World->LineTraceSingleByChannel(Hit, Center - FVector(500,0,0), Center + FVector(500,0,0), ElysiumCollision::SightChannel);
		return Hit.GetComponent() == Component;
	};
	TestTrue(TEXT("real native query hits authored bbox"), TraceBody(Prop->Origin));
	Prop->ScriptHide();
	TestFalse(TEXT("real hidden native query misses"), TraceBody(Prop->Origin));
	TestTrue(TEXT("hide preserves static incarnation and recipe"), Prop->VPhysicsGetObject() == Actor && ElysiumModelPhysics::RecipeFor(Actor));
	Prop->ScriptUnhide();
	TestTrue(TEXT("real unhidden native query hits"), TraceBody(Prop->Origin));
	TestTrue(TEXT("unhide preserves actual actor"), Prop->VPhysicsGetObject() == Actor);
	// Saved native state deliberately differs from logical pose and profile defaults.
	const FTransform SavedPose(FQuat::Identity, FVector(7000,6000,5000));
	Body->SetBodyTransform(SavedPose, ETeleportType::TeleportPhysics);
	Component->SetCollisionResponseToChannel(ElysiumCollision::SightChannel, ECR_Ignore);
	Component->SetLinearDamping(.73f); Component->SetEnableGravity(false);
	Body->SetShapeCollisionEnabled(0, ECollisionEnabled::NoCollision); // native saved query state differs from logical shown.
	const int32 PropIndex = Prop->Handle.Index;
	auto* Absent = Source.Resolve(Source.SpawnRuntimeEntity(MakeDef(TEXT("static_absent"))));
	if (!Absent) return false;
	const int32 AbsentIndex = Absent->Handle.Index;
	Absent->VPhysicsDestroyObject();
	FElysiumSavedPhysicsBody StaticMechanics;
	if (!TestTrue(TEXT("static native mass properties captured before persistence"), ElysiumModelPhysics::Capture(Prop->VPhysicsGetObject(), StaticMechanics))) return false;
	FElysiumMapSnapshot Snapshot; Source.Freeze(Snapshot);
	if (!TestFalse(TEXT("actual static Physics save admitted"), Snapshot.bPhysicsSaveFailed)) return false;
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes, true); FElysiumSaveArchive SaveAr(Writer, FElysiumSaveVersion::Latest); SaveAr << Snapshot;
	FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes, true); FElysiumSaveArchive LoadAr(Reader, FElysiumSaveVersion::Latest); LoadAr << Persisted;
	if (!TestFalse(TEXT("static Physics block archive decode"), Reader.IsError())) return false;
	Source.Teardown();
	FElysiumEntityWorld Destination(Map, nullptr, Services); Destination.Load(FElysiumEntityDefs(Defs));
	Persisted.PhysicsBodies.Reset(); // reconstruction must consume the real persisted block.
	if (!TestTrue(TEXT("actual static ApplySnapshot roundtrip"), Destination.ApplySnapshot(Persisted) >= 0)) return false;
	auto* Restored = Destination.Resolve(FElysiumEntityHandle(PropIndex, Destination.GetEpoch()));
	auto* RestoredAbsent = Destination.Resolve(FElysiumEntityHandle(AbsentIndex, Destination.GetEpoch()));
	if (!Restored || !RestoredAbsent) return false;
	Component = ElysiumModelPhysics::ComponentFor(Restored->VPhysicsGetObject()); Body = ElysiumModelPhysics::Identity(Component);
	if (!TestNotNull(TEXT("actual restored static native actor"), Body)) return false;
	TestFalse(TEXT("restored static never becomes simulating"), Component->IsSimulatingPhysics());
	TestTrue(TEXT("reconnect preserves native pose differing logical origin"), Body->GetUnrealWorldTransform().Equals(SavedPose, .001));
	TestEqual(TEXT("reconnect preserves actual response differing profile"), Component->GetCollisionResponseToChannel(ElysiumCollision::SightChannel), ECR_Ignore);
	TestEqual(TEXT("actual restored damping"), Body->LinearDamping, .73f);
	TestFalse(TEXT("actual restored gravity input"), Body->bEnableGravity);
	const auto* RestoredMaterial = Cast<UElysiumPhysicalMaterial>(Body->GetSimplePhysicalMaterial());
	if (!TestNotNull(TEXT("actual reconstructed static material"), RestoredMaterial)) return false;
	TestEqual(TEXT("actual restored raw material lookup"), RestoredMaterial->SourceName, Destination.PhysicsSurfaces().GetSurfaceName(SurfaceIndex));
	TestEqual(TEXT("actual restored native friction"), RestoredMaterial->Friction, Surface->Friction());
	TestTrue(TEXT("actual restored static backend state"), ElysiumModelPhysics::ActorIdentity(Component)->GetGameThreadAPI().ObjectState() == Chaos::EObjectStateType::Static);
	FElysiumSavedPhysicsBody RestoredStatic;
	TestTrue(TEXT("static Physics reconstruction captures live selected pair/frame"), ElysiumModelPhysics::Capture(Restored->VPhysicsGetObject(), RestoredStatic));
	TestTrue(TEXT("static Physics reconstruction retains native mass frame and inertia producers"),
		RestoredStatic.CurrentInertia == StaticMechanics.CurrentInertia && RestoredStatic.CurrentInverseInertia == StaticMechanics.CurrentInverseInertia &&
		RestoredStatic.CurrentMassFrame.Equals(StaticMechanics.CurrentMassFrame, 1.e-6));
	TestEqual(TEXT("reconnect preserves actual saved shape enabled state"), Body->GetShapeCollisionEnabled(0), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("restored native query honors saved response"), TraceBody(SavedPose.GetLocation()));
	Body->SetShapeCollisionEnabled(0, ECollisionEnabled::QueryAndPhysics);
	TestFalse(TEXT("native query independently honors restored Ignore response"), TraceBody(SavedPose.GetLocation()));
	Body->SetShapeCollisionEnabled(0, ECollisionEnabled::NoCollision);
	Component->SetCollisionResponseToChannel(ElysiumCollision::SightChannel, ECR_Block);
	TestFalse(TEXT("native query independently honors restored disabled shape"), TraceBody(SavedPose.GetLocation()));
	Body->SetShapeCollisionEnabled(0, ECollisionEnabled::QueryAndPhysics);
	TestTrue(TEXT("native query returns after response and shape both enable"), TraceBody(SavedPose.GetLocation()));
	TestNull(TEXT("count0 static field survives reconnect without resurrection"), RestoredAbsent->VPhysicsGetObject());
	FElysiumMapSnapshot InvalidShape = Snapshot; InvalidShape.BlockStream.Reset();
	InvalidShape.PhysicsBodies[0].BboxCollisionEnabled = 255;
	TArray<uint8> InvalidShapeBytes; FMemoryWriter InvalidShapeWriter(InvalidShapeBytes, true);
	FElysiumSaveArchive InvalidShapeSave(InvalidShapeWriter, FElysiumSaveVersion::Latest); InvalidShapeSave << InvalidShape;
	FElysiumMapSnapshot InvalidShapeRead; FMemoryReader InvalidShapeReader(InvalidShapeBytes, true);
	FElysiumSaveArchive InvalidShapeLoad(InvalidShapeReader, FElysiumSaveVersion::Latest); InvalidShapeLoad << InvalidShapeRead;
	TestTrue(TEXT("actual malformed native shape-enabled archive refused"), InvalidShapeReader.IsError());
	FElysiumEntityDef AliasDef; AliasDef.Classname = TEXT("info_target");
	auto* Alias = Destination.Resolve(Destination.SpawnRuntimeEntity(AliasDef));
	auto* SecondAlias = Destination.Resolve(Destination.SpawnRuntimeEntity(AliasDef));
	if (!Alias || !SecondAlias) return false;
	Actor = Restored->VPhysicsGetObject();
	Alias->VPhysicsSetObject(Actor); Alias->VPhysicsSwapObject(nullptr);
	TestTrue(TEXT("Swap clear is nondestructive"), ElysiumModelPhysics::ActorIdentity(Component) == Actor);
	Alias->VPhysicsSetObject(Actor); SecondAlias->VPhysicsSetObject(Actor);
	int32 BeforeReleaseObservations = 0;
	Destination.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
	{
		if (Event.Kind != TEXT("physics_retire")) return;
		++BeforeReleaseObservations;
		TestNull(TEXT("retirement callback observes producer field already clear"), Restored->VPhysicsGetObject());
		TestNull(TEXT("retirement callback observes recipe already retired"), ElysiumModelPhysics::RecipeFor(Actor));
		TestTrue(TEXT("retirement callback precedes actual native release"), Component->GetBodyInstance()->IsValidBodyInstance());
	});
	Alias->VPhysicsDestroyObject();
	Destination.SetAiTraceSink({});
	TestEqual(TEXT("all three registered references retire before native release"), BeforeReleaseObservations, 3);
	TestNull(TEXT("borrowed225 clears supplied alias"), Alias->VPhysicsGetObject());
	TestNull(TEXT("borrowed225 clears every other native alias"), SecondAlias->VPhysicsGetObject());
	TestNull(TEXT("borrowed225 clears actual producing entity"), Restored->VPhysicsGetObject());
	TestNull(TEXT("recipe retired before actor release"), ElysiumModelPhysics::RecipeFor(Actor));
	TestFalse(TEXT("borrowed225 releases actual static backend actor"), Component->GetBodyInstance()->IsValidBodyInstance());
	Restored->ScriptHide(); Restored->ScriptUnhide(); Restored->SetRuntimeOrigin(FVector(1,2,3));
	TestFalse(TEXT("owner gates and transform cannot resurrect borrowed225 deletion"), Component->GetBodyInstance()->IsValidBodyInstance());
	// Actual map parser resolves parents before Spawn; authored names that resolve nowhere stay static.
	Destination.Teardown();
	FElysiumEntityDefs ParentDefs;
	FElysiumEntityDef ParentDef; ParentDef.Classname = TEXT("info_target"); ParentDef.TargetName = TEXT("real_parent");
	ParentDefs.Defs.Add(ParentDef);
	auto RealChild = MakeDef(TEXT("resolved_child")); RealChild.Keys.Add(TEXT("parentname"), TEXT("real_parent")); ParentDefs.Defs.Add(RealChild);
	auto MissingChild = MakeDef(TEXT("unresolved_child")); MissingChild.Keys.Add(TEXT("parentname"), TEXT("absent_parent")); ParentDefs.Defs.Add(MissingChild);
	FElysiumEntityWorld ParentWorld(Map, nullptr, Services);
	ParentWorld.Load(MoveTemp(ParentDefs));
	auto* Resolved = ParentWorld.FindByName(TEXT("resolved_child"));
	auto* Unresolved = ParentWorld.FindByName(TEXT("unresolved_child"));
	if (!Resolved || !Unresolved) return false;
	TestNotNull(TEXT("map parent resolver supplies actual moveparent before Spawn"), ParentWorld.Resolve(Resolved->MoveParent));
	TestTrue(TEXT("real SetModel hull fields precede shadow creation"),
		(FVector(Resolved->CollMins.X, -Resolved->CollMaxs.Y, Resolved->CollMins.Z) * 2.54).Equals(Inputs->StudioMinsCm, .001));
	TestNotNull(TEXT("resolved parent creates actual native shadow"), Resolved->VPhysicsGetObject());
	const auto* Shadow = ElysiumModelPhysics::ShadowFor(Resolved->VPhysicsGetObject());
	TestTrue(TEXT("real installed controller retains forbidden movement/rotation"), Shadow && !Shadow->bAllowMovement && !Shadow->bAllowRotation);
	TestTrue(TEXT("actual owner registry membership"), ParentWorld.PhysicsShadows().Contains(Resolved->Handle));
	TestNull(TEXT("authored unresolved name has no resolved moveparent"), ParentWorld.Resolve(Unresolved->MoveParent));
	TestNotNull(TEXT("authored unresolved parent name takes real static bbox branch"), Unresolved->VPhysicsGetObject());
	return true;
}

// Actual production map tick groups: native body/controller creation, moving parent transaction,
// one232 target, configured Chaos time, feedback before the queue, and saved reconnect.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumParentShadowFrameTest, "Elysium.Substrate.ModelPhysics.ParentShadowFrame",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumParentShadowFrameTest::RunTest(const FString&)
{
 const FString PreviousTimeScale = ElysiumCommandBus::Console().GetCvar(TEXT("phys_timescale"));
 ON_SCOPE_EXIT { ElysiumCommandBus::Console().Execute(FString(TEXT("phys_timescale ")) + PreviousTimeScale); };
 FPlayerWorldFixture Fixture;
 if (!Fixture.CreateWorld(*this)) return false;
 Fixture.SpawnPlayerControllerAndPawn(FVector(-1000,0,1000));
 Fixture.PlayerController->Possess(Fixture.Pawn);
 auto* Map = Fixture.SpawnMapActorDeferred();
 Map->bStageOnly = true; Map->bStageWithoutCatalogue = true;
 Map->FinishSpawning(FTransform::Identity);
 auto Tick = [&]() { return Fixture.TestWorld.TickTestWorld(.02f); };
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("production activation barrier admits initial stage"), Map->IsRuntimeActive()))
 { AddError(Map->GetMissingRuntimePrerequisites()); return false; }
 FElysiumEntityDefs Defs; Defs.MapName = TEXT("parent_shadow_native");
 FElysiumEntityDef WorldDef; WorldDef.Classname = TEXT("worldspawn"); WorldDef.TargetName = TEXT("impact_world"); Defs.Defs.Add(WorldDef);
 FElysiumEntityDef Parent; Parent.Classname = TEXT("func_door"); Parent.TargetName = TEXT("parent");
 Parent.Model = 1; Parent.Origin = FVector(3000,4000,3000);
 Parent.Keys.Add(TEXT("model"), TEXT("*1")); Parent.Keys.Add(TEXT("speed"), TEXT("100"));
 Parent.Keys.Add(TEXT("lip"), TEXT("0")); Parent.Keys.Add(TEXT("wait"), TEXT("-1"));
 FElysiumConvexHull Hull;
 for (float X : { 0.f, 120.f }) for (float Y : { -10.f, 10.f }) for (float Z : { -10.f, 10.f }) Hull.Vertices.Add(FVector(X,Y,Z));
 Parent.Hulls.Add(Hull); Defs.Defs.Add(Parent);
 auto Child = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("child"));
 Child.Classname = TEXT("prop_dynamic"); Child.Origin = FVector(3000,4500,3000);
 Child.Keys.Add(TEXT("parentname"), TEXT("parent")); Child.Keys.Add(TEXT("solid"), TEXT("2"));
 Child.Keys.Add(TEXT("spawnflags"), TEXT("0")); Defs.Defs.Add(Child);
 FElysiumEntityDef Trigger; Trigger.Classname = TEXT("trigger_multiple"); Trigger.TargetName = TEXT("native_candidate");
 Trigger.Model = 2; Trigger.Origin = FVector(3000,4500,3000); Trigger.Keys.Add(TEXT("model"), TEXT("*2"));
 Trigger.Keys.Add(TEXT("spawnflags"), TEXT("8"));
 FElysiumConvexHull TriggerHull;
 for (float X : { -15.f, 15.f }) for (float Y : { -100.f, 100.f }) for (float Z : { -100.f, 100.f }) TriggerHull.Vertices.Add(FVector(X,Y,Z));
 Trigger.Hulls.Add(TriggerHull); Defs.Defs.Add(Trigger);
 FElysiumStageSeat Seat; Seat.FeetCm = FVector(-1000,0,1000);
 FElysiumEntityDefs RestoreDefs = Defs;
 FString Error;
 if (!TestTrue(TEXT("actual scenario rebuild prepares props and parser hierarchy"), Map->RebuildStageWorld(MoveTemp(Defs), {}, Seat, Error)))
 { AddError(Error); return false; }
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("production rebuilt map becomes Active"), Map->IsRuntimeActive())) { AddError(Map->GetMissingRuntimePrerequisites()); return false; }
 auto* World = Map->GetEntityWorld();
 auto* MovingParent = World->FindByName(TEXT("parent")); auto* ShadowChild = World->FindByName(TEXT("child"));
 if (!MovingParent || !ShadowChild || !TestNotNull(TEXT("production child native incarnation"), ShadowChild->VPhysicsGetObject())) return false;
 void* Object = ShadowChild->VPhysicsGetObject();
 auto* Component = ElysiumModelPhysics::ComponentFor(Object); auto* Native = ElysiumModelPhysics::Identity(Component);
 if (!Native) return false;
 auto* Solver = Native->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>();
 if (!TestNotNull(TEXT("real scene solver exists"), Solver)) return false;
 TestTrue(TEXT("native membership from production creation"), World->PhysicsShadows().Contains(ShadowChild->Handle));
 const FVector InitialLogical = ShadowChild->GetAbsOrigin();
 World->AcceptInput(MovingParent->Handle, FName(TEXT("Open")), FElysiumVariant::Void(),
  FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 0.5"));
 const double PriorTime = Solver->GetSolverTime();
 const FVector PriorNative = Native->GetUnrealWorldTransform().GetLocation();
 int32 QueuedObservers = 0; int32 TransactionTargets = 0;
 FVector LastTransactionLogicalTarget = InitialLogical;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 {
  if (Event.Entity == ShadowChild->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=shadow_target")))
  { ++TransactionTargets; LastTransactionLogicalTarget=ShadowChild->GetAbsOrigin(); }
  if (Event.Name != TEXT("native_candidate") || Event.Kind != TEXT("input") || !Event.Text.StartsWith(TEXT("Enable "))) return;
  ++QueuedObservers;
  TestTrue(TEXT("production EventQueue input observes completed native solve"), Solver->GetSolverTime() > PriorTime && Native->GetUnrealWorldTransform().GetLocation().X > PriorNative.X);
 });
 World->EnqueueInput(TEXT("native_candidate"), FName(TEXT("Enable")), FElysiumVariant::Void(), 0.,
  FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
 for (int32 ParentWitnessFrame=0;ParentWitnessFrame<2;++ParentWitnessFrame)
 {
  const float ParentWitnessStart=MovingParent->LocalTime;
  Tick();
  void* ParentWitnessObject=MovingParent->VPhysicsGetObject();
  const auto* ParentWitnessShadow=ElysiumModelPhysics::ShadowFor(ParentWitnessObject);
  const auto* ParentWitnessBody=ElysiumModelPhysics::Identity(ElysiumModelPhysics::ComponentFor(ParentWitnessObject));
  TestTrue(TEXT("each232 transaction submits captured logical child target"),ElysiumModelPhysics::ShadowFor(Object)->Target.GetLocation().Equals(LastTransactionLogicalTarget,.001)); //retail-site emits just before public46; later feedback retains it
  AddInfo(FString::Printf(TEXT("PARENT_FRAME,%d,logicalX=%.17g,solvedX=%.17g,targetX=%.17g,local=%.9g,start=%.9g,childLogicalX=%.17g,childTargetX=%.17g"),ParentWitnessFrame,
   MovingParent->GetAbsOrigin().X,ParentWitnessBody->GetUnrealWorldTransform().GetLocation().X,ParentWitnessShadow->Target.GetLocation().X,MovingParent->LocalTime,MovingParent->StartFrameLocalTime,
   ShadowChild->GetAbsOrigin().X,ElysiumModelPhysics::ShadowFor(Object)->Target.GetLocation().X));
  if (ParentWitnessFrame==1)
  {
   TestEqual(TEXT("strict1in gate full-rewinds second parent feedback interval"),MovingParent->LocalTime,ParentWitnessStart);
   TestEqual(TEXT("full rewind stamps parent start with corrected local clock"),MovingParent->StartFrameLocalTime,ParentWitnessStart);
   TestTrue(TEXT("full rewind retains earlier child232 target without relink"),ElysiumModelPhysics::ShadowFor(Object)->Target.GetLocation().Equals(LastTransactionLogicalTarget,.001));
   TestTrue(TEXT("later parent full rewind separates logical child from already submitted target"),!LastTransactionLogicalTarget.Equals(ShadowChild->GetAbsOrigin(),.001));
  }
  for (const auto& ParentWitnessEvent:ElysiumModelPhysics::ShadowEvents(ParentWitnessObject))
   AddInfo(FString::Printf(TEXT("PARENT_EVENT,dt=%.17g,x=%.17g,target=%.17g,v=%.17g,corrected=%.17g,drag=%.17g,fraction=%.9g"),ParentWitnessEvent.Delta,ParentWitnessEvent.Position.X,ParentWitnessEvent.Target.X,ParentWitnessEvent.Velocity.X,ParentWitnessEvent.CorrectedVelocity.X,ParentWitnessEvent.LinearDrag,ParentWitnessEvent.Fraction));
 }
 World->SetAiTraceSink({});
 TestEqual(TEXT("production pending queue is serviced exactly once"), QueuedObservers, 1);
 TestEqual(TEXT("each successful native mover transaction has one232 target"), TransactionTargets, 2);
 const double SolvedDelta = Solver->GetSolverTime() - PriorTime;
 TestTrue(TEXT("actual two native solves use .02*.5, exactly once per frame"), FMath::IsNearlyEqual(SolvedDelta, .02, .00001));
 const auto* State = ElysiumModelPhysics::ShadowFor(Object);
 if (!State) return false;
 AddInfo(FString::Printf(TEXT("PARENT_SHADOW_TARGET targetX=%.17g logicalX=%.17g solvedX=%.17g difference=%.17g"),State->Target.GetLocation().X,ShadowChild->GetAbsOrigin().X,Native->GetUnrealWorldTransform().GetLocation().X,State->Target.GetLocation().X-ShadowChild->GetAbsOrigin().X));
 TestTrue(TEXT("successful parent movement changes logical inherited child origin"), ShadowChild->GetAbsOrigin().X > InitialLogical.X);
 TestTrue(TEXT("232 submits logical target after parent transaction"), State->Target.GetLocation().Equals(LastTransactionLogicalTarget, .001)); //100a69e0 submission precedes100a55b0 later parent rewind; no232 at its tail
 TestEqual(TEXT("FinishPushers submits zero arrival, no extra relink target"), State->RemainingSeconds, 0.f);
 auto* NativeCandidate = static_cast<FElysiumTriggerBase*>(World->FindByName(TEXT("native_candidate")));
 TestTrue(TEXT("native bbox query retains physical candidate pair before eligibility"), ShadowChild->IsCurrentlyTouching() && NativeCandidate && NativeCandidate->IsCurrentlyTouching());
 TestTrue(TEXT("move0 shadow prop fails retail ALLOW_PHYSICS activation"), NativeCandidate && NativeCandidate->LastTriggerTime < -1.0e15);
 FVector NativeSample;
 TestEqual(TEXT("native slot47 returns literal1"), ElysiumModelPhysics::GetShadowPosition(Object, &NativeSample, nullptr), 1);
 TestTrue(TEXT("actual native solve moved from previous native position"), NativeSample.X > PriorNative.X);
 TestTrue(TEXT("zero-arrival native controller reaches submitted position"), NativeSample.Equals(State->Target.GetLocation(), .1));
 // Finish the actual parent transaction, then submit a positive-arrival target through232.
 for (int32 Frame = 0; Frame < 90; ++Frame) Tick();
 TestFalse(TEXT("lost native candidate expires physical pair in deferred untouch"), ShadowChild->IsCurrentlyTouching());
 // Two real cooked entities supply explicit eligible step service inputs. Preserve native
 // encounter order as input, then assert the recovered append/reverse/finish rules.
 TArray<FElysiumEntityHandle> OrderCandidates;
 const FBox OrderBounds = MovingParent->Body->Bounds.GetBox();
 for (int32 Index = 0; Index < 2; ++Index)
 {
  auto OrderDef = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), *FString::Printf(TEXT("order_candidate_%d"),Index));
  OrderDef.Classname=TEXT("prop_dynamic"); OrderDef.Keys.Add(TEXT("solid"),TEXT("2")); OrderDef.Keys.Add(TEXT("spawnflags"),TEXT("0"));
  OrderDef.Origin=OrderBounds.GetCenter();
  auto* OrderEntity=World->Resolve(World->SpawnRuntimeEntity(MoveTemp(OrderDef)));
  if (!TestNotNull(TEXT("actual model creation supplies each order candidate"),OrderEntity)) return false;
  OrderEntity->SetMoveType(3,0);
  FVector OrderOrigin(OrderBounds.Min.X+30.+Index*60.,OrderBounds.GetCenter().Y,
   OrderBounds.GetCenter().Z-(OrderEntity->CollMins.Z+OrderEntity->CollMaxs.Z)*1.27);
  OrderEntity->SetAbsOrigin(OrderOrigin); OrderEntity->UpdatePhysicsShadowToCurrentPosition(0.f);
  OrderCandidates.Add(OrderEntity->Handle);
 }
 TArray<FElysiumEntityHandle> Appended,Checked,OrderSubmitted;
 TArray<FVector> OrderStarts;
 for (const auto& Handle : OrderCandidates) OrderStarts.Add(World->Resolve(Handle)->GetAbsOrigin());
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 {
  if (Event.Kind!=TEXT("retail_site")) return;
  if (OrderCandidates.Contains(Event.Entity))
  {
   if (Event.Text.Contains(TEXT("tag=pusher_append"))) Appended.Add(Event.Entity);
   if (Event.Text.Contains(TEXT("tag=pusher_check"))) Checked.Add(Event.Entity);
  }
  if (Event.Text.Contains(TEXT("tag=shadow_target"))) OrderSubmitted.Add(Event.Entity);
 });
 const FVector OrderRootStart=MovingParent->GetOrigin();
 MovingParent->Velocity=FVector(100,0,0);
 TestFalse(TEXT("two actual eligible candidates permit the production transaction"),MovingParent->PhysicsPushMove(.02f));
 MovingParent->Velocity=FVector::ZeroVector; World->SetAiTraceSink({});
 if (!TestEqual(TEXT("native geometry actually appends both candidates once"),Appended.Num(),2)) return false;
 TestEqual(TEXT("both captured candidates are checked once"),Checked.Num(),2);
 if (Checked.Num()==2) { TestTrue(TEXT("last appended candidate checked first"),Checked[0]==Appended[1]); TestTrue(TEXT("first appended candidate checked last"),Checked[1]==Appended[0]); }
 TestEqual(TEXT("whole hierarchy and both candidates each submit232"),OrderSubmitted.Num(),4);
 if (OrderSubmitted.Num()==4)
 {
  TestTrue(TEXT("reverse hierarchy submits child then root before candidates"),OrderSubmitted[0]==ShadowChild->Handle && OrderSubmitted[1]==MovingParent->Handle);
  TestTrue(TEXT("candidate submissions retain reverse append order"),OrderSubmitted[2]==Appended[1] && OrderSubmitted[3]==Appended[0]);
 }
 for (int32 Index=0;Index<OrderCandidates.Num();++Index)
 {
  auto* OrderEntity=World->Resolve(OrderCandidates[Index]);
  TestTrue(TEXT("actual candidate logical displacement is velocity times interval"),OrderEntity->GetAbsOrigin().Equals(OrderStarts[Index]+FVector(2,0,0),.001));
  OrderEntity->Kill();
 }
 MovingParent->SetOrigin(OrderRootStart); MovingParent->UpdatePhysicsShadowToCurrentPosition(0.f); ShadowChild->UpdatePhysicsShadowToCurrentPosition(0.f);
 // Complete the actual feedback phase of the direct successful transaction before changing
 // velocity to Close. Otherwise its pending .02 interval is rewound a second time by226.
 Tick();
 TestEqual(TEXT("completed success feedback consumes its local-clock interval"),MovingParent->LocalTime,MovingParent->StartFrameLocalTime);
 TestEqual(TEXT("actual prior arrival remains open before wall Close"),MovingParent->AsDoorBase()->State(),FElysiumDoorBase::EToggleState::AtTop);
 FVector CompletedRootPose; ElysiumModelPhysics::GetShadowPosition(MovingParent->VPhysicsGetObject(),&CompletedRootPose,nullptr);
 TestTrue(TEXT("wall initial logical and solved open poses agree"),MovingParent->GetAbsOrigin().Equals(CompletedRootPose,.001));
 TestTrue(TEXT("wall initial submitted and logical open poses agree"),ElysiumModelPhysics::ShadowFor(MovingParent->VPhysicsGetObject())->Target.GetLocation().Equals(MovingParent->GetAbsOrigin(),.001));
 //10038180 does not sweep the pusher itself. A genuine spawned eligible entity pinned by
 // static world geometry refuses the candidate transaction, rolling back the whole hierarchy.
 auto PushCandidateDef = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("push_candidate"));
 PushCandidateDef.Classname = TEXT("prop_dynamic"); PushCandidateDef.Keys.Add(TEXT("solid"), TEXT("2"));
 PushCandidateDef.Keys.Add(TEXT("spawnflags"), TEXT("0"));
 const FBox ParentBounds = MovingParent->Body->Bounds.GetBox();
 PushCandidateDef.Origin = ParentBounds.GetCenter();
 FElysiumEntity* PushCandidate = World->Resolve(World->SpawnRuntimeEntity(MoveTemp(PushCandidateDef)));
 if (!TestNotNull(TEXT("actual runtime model producer creates push candidate"), PushCandidate)) return false;
 PushCandidate->SetMoveType(3, 0); // actual94/93 seam on genuine entity/cooked bbox, not an injected query result
 FVector CandidateStart(ParentBounds.Min.X-PushCandidate->CollMaxs.X*2.54+.25,
  ParentBounds.GetCenter().Y, ParentBounds.GetCenter().Z-(PushCandidate->CollMins.Z+PushCandidate->CollMaxs.Z)*1.27);
 PushCandidate->SetAbsOrigin(CandidateStart); PushCandidate->UpdatePhysicsShadowToCurrentPosition(0.f);
 auto* ObstaclePayload = NewObject<UElysiumMapCollisionPayload>();
 ObstaclePayload->MapName = TEXT("parent_shadow_wall");
 FElysiumCollisionHull WallHull;
 const FVector WallCenter(CandidateStart.X+PushCandidate->CollMins.X*2.54-1., ParentBounds.GetCenter().Y, ParentBounds.GetCenter().Z);
 for (float X : { -1.f,1.f }) for (float Y : { -100.f,100.f }) for (float Z : { -100.f,100.f }) WallHull.Vertices.Add(WallCenter+FVector(X,Y,Z));
 ObstaclePayload->AuthorWorldBody(7, {WallHull}, true);
 if (!TestTrue(TEXT("actual common-world wall cooks"), ObstaclePayload->CreatePhysicsMeshes())) return false;
 auto* Obstacle = Fixture.World->SpawnActor<AElysiumWorldCollisionActor>();
 if (!Obstacle || !TestEqual(TEXT("actual world wall producer installs one native body"), Obstacle->AuthorFromPayload(ObstaclePayload),1)) return false;
 ElysiumModelPhysics::BindWorldCreationOwners(*World);
 auto* ImpactWorld = World->FindByName(TEXT("impact_world"));
 if (!TestTrue(TEXT("native sweep wall has actual creation-owned worldspawn"), ImpactWorld && ElysiumModelPhysics::GameDataForObject(ElysiumModelPhysics::ActorIdentity(Obstacle->Bodies[0])) == ImpactWorld)) return false;
 TestEqual(TEXT("actual world constructor supplies solid1"),ImpactWorld->GetSolid(),1);
 TestEqual(TEXT("actual world constructor supplies move8"),ImpactWorld->GetMoveType(),8);
 TestEqual(TEXT("ordinary world lacks permissive first-touch flags"),ImpactWorld->GetSolidFlags() & (4u|8u|0x20u),0u);
 TestNull(TEXT("world creation owner does not acquire mutable native current field"),ImpactWorld->VPhysicsGetObject());
 TestTrue(TEXT("candidate is an ordinary solid before Impact"),PushCandidate->GetSolid()!=0 && (PushCandidate->GetSolidFlags() & (4u|8u|0x20u))==0);
 ImpactWorld->TouchCallback = [](FElysiumEntity& Receiver, FElysiumEntity* Other)
 {
  Receiver.World->EmitRetailSite(Receiver, TEXT("world_impact_touch"), TEXT("CBaseEntity::Touch"), 0x100a4af0u, TEXT("callback"), Other ? Other->TargetName : TEXT("null"));
 };
 const FVector BeforeBlockedParent = MovingParent->GetAbsOrigin();
 auto RootWords=[&](const TCHAR* Phase)
 {
  FVector RootSolved; ElysiumModelPhysics::GetShadowPosition(MovingParent->VPhysicsGetObject(),&RootSolved,nullptr);
  const auto* RootShadow=ElysiumModelPhysics::ShadowFor(MovingParent->VPhysicsGetObject());
  AddInfo(FString::Printf(TEXT("%s localX=%.17g absX=%.17g solvedX=%.17g targetX=%.17g localTime=%.9g start=%.9g deadline=%.9g velocityX=%.17g state=%d"),
   Phase,MovingParent->GetOrigin().X,MovingParent->GetAbsOrigin().X,RootSolved.X,RootShadow?RootShadow->Target.GetLocation().X:0.,
   MovingParent->LocalTime,MovingParent->StartFrameLocalTime,MovingParent->PhysicsMoveDoneTime,MovingParent->Velocity.X,static_cast<int32>(MovingParent->AsDoorBase()->State())));
 };
 RootWords(TEXT("pre_wall_close"));
 const FTransform BeforeBlockedTarget = ElysiumModelPhysics::ShadowFor(Object)->Target;
 int32 BlockedTargets = 0, WorldImpactTouches = 0;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 {
  if (Event.Kind == TEXT("retail_site") && (Event.Text.Contains(TEXT("tag=pusher_candidates")) || Event.Text.Contains(TEXT("tag=pusher_check")))) AddInfo(Event.Text);
  if (Event.Entity == ShadowChild->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=shadow_target"))) ++BlockedTargets;
  if (Event.Entity == ImpactWorld->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=world_impact_touch")))
  {
   ++WorldImpactTouches;
   RootWords(TEXT("impact175"));
   TestTrue(TEXT("native world Impact175 receives original candidate"), Event.Text.Contains(TEXT("push_candidate")));
   TestFalse(TEXT("actual175 observes speculative root before rollback"),MovingParent->GetAbsOrigin().Equals(BeforeBlockedParent,.001));
  }
 });
 World->AcceptInput(MovingParent->Handle, FName(TEXT("Close")), FElysiumVariant::Void(), FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
 Tick();
 RootWords(TEXT("wall_frame1"));
 TestTrue(TEXT("actual first wall Impact creates both physical links"),ImpactWorld->IsCurrentlyTouching() && PushCandidate->IsCurrentlyTouching());
 TestEqual(TEXT("ordinary solid first Impact has no175"),WorldImpactTouches,0);
 Tick(); World->SetAiTraceSink({});
 RootWords(TEXT("wall_frame2"));
 TestTrue(TEXT("native blocker refuses whole logical parent transaction"), MovingParent->GetAbsOrigin().Equals(BeforeBlockedParent, .001));
 TestTrue(TEXT("refused candidate rolls back its logical pose"), PushCandidate->GetAbsOrigin().Equals(CandidateStart,.001));
 TestTrue(TEXT("blocked transaction leaves submitted child target unchanged"), ElysiumModelPhysics::ShadowFor(Object)->Target.Equals(BeforeBlockedTarget, .001));
 TestEqual(TEXT("blocked transaction emits no232 submission"), BlockedTargets, 0);
 TestEqual(TEXT("actual repeated world sweep dispatches worldspawn175 once before rollback"), WorldImpactTouches,1);
 Obstacle->Destroy();
 PushCandidate->Kill();
 for (int32 Frame = 0; Frame < 90; ++Frame) Tick();
 const FVector BeforeArrival = ElysiumModelPhysics::Identity(Component)->GetUnrealWorldTransform().GetLocation();
 FVector ArrivalTarget = ShadowChild->GetAbsOrigin() + FVector(100,0,0);
 ShadowChild->SetAbsOrigin(ArrivalTarget);
 ShadowChild->UpdatePhysicsShadowToCurrentPosition(.1f);
 Tick();
 State = ElysiumModelPhysics::ShadowFor(Object);
 ElysiumModelPhysics::GetShadowPosition(Object, &NativeSample, nullptr);
 TestTrue(TEXT("positive arrival uses actual .01 native delta for displacement"), FMath::IsNearlyEqual(NativeSample.X - BeforeArrival.X, 10., .2));
 TestTrue(TEXT("positive arrival countdown consumes native delta"), State && FMath::IsNearlyEqual(State->RemainingSeconds, .09f, .0001f));
 Native->SetLinearVelocity(FVector(200,0,0), false);
 TestTrue(TEXT("existing native controller changes caps"), ElysiumModelPhysics::SetShadow(Object, 10.f, 10.f, true, true));
 State = ElysiumModelPhysics::ShadowFor(Object);
 TestTrue(TEXT("existing controller retains false permissions"), State && !State->bAllowMovement && !State->bAllowRotation);
 TestTrue(TEXT("2601ac40 MIN retains requested damping cap and zero desired cap above requested speed"), State &&
  FMath::IsNearlyEqual(State->LinearDesiredLimitCm, 0.f, .001f) && FMath::IsNearlyEqual(State->LinearDampingLimitCm, 25.4f, .001f));
 FVector SecondTarget = NativeSample + FVector(100,0,0);
 TestTrue(TEXT("second actual target admitted"), ElysiumModelPhysics::UpdateShadow(Object, SecondTarget, FQuat::Identity, false, .1f));
 Tick(); ElysiumModelPhysics::GetShadowPosition(Object, &NativeSample, nullptr);
 TestTrue(TEXT("zero desired cap skips desired clamp while positive damping cap retains old velocity remainder"), FMath::IsNearlyEqual(NativeSample.X, SecondTarget.X - 88.254, .2)); //200-25.4+1000 cm/s, actual .01 event
 Native->PutInstanceToSleep();
 TestTrue(TEXT("repeated target replaces countdown"), ElysiumModelPhysics::UpdateShadow(Object, SecondTarget, FQuat::Identity, false, .2f));
 TestFalse(TEXT("repeated target does not wake native body"), Native->IsInstanceAwake());
 State = ElysiumModelPhysics::ShadowFor(Object);
 TestEqual(TEXT("repeated target retains new arrival"), State->RemainingSeconds, .2f);
 TestTrue(TEXT("negative arrival clamps at actual submission"), ElysiumModelPhysics::UpdateShadow(Object, SecondTarget, FQuat::Identity, false, -.5f));
 TestEqual(TEXT("negative arrival stores zero"), ElysiumModelPhysics::ShadowFor(Object)->RemainingSeconds, 0.f);
 const FQuat PriorRotation = Native->GetUnrealWorldTransform().GetRotation();
 ElysiumModelPhysics::SetShadow(Object,10000.f,10000.f,false,false); //independent full-frame/teleport witnesses use ordinary high caps
 FRotator SourceAngles(17,23,31); ShadowChild->SetAbsAngles(SourceAngles);
 const FQuat FullTargetRotation = ElysiumModelPhysics::SourceRotation(ShadowChild->GetAbsAngles());
 ShadowChild->UpdatePhysicsShadowToCurrentPosition(.1f);
 TestTrue(TEXT("pitch/roll target submission uses full Source frame"), ElysiumModelPhysics::ShadowFor(Object)->Target.GetRotation().Equals(FullTargetRotation, .0001));
 TestTrue(TEXT("logical angle setter does not teleport native rotation"), Native->GetUnrealWorldTransform().GetRotation().Equals(PriorRotation, .0001));
 Tick();
 TestTrue(TEXT("actual native angular solve follows full target frame"), Native->GetUnrealWorldTransform().GetRotation().AngularDistance(FQuat::Slerp(PriorRotation, FullTargetRotation, .1f)) < .002);
 // Prediction error, not distance to target, selects the installed threshold arm.
 ElysiumModelPhysics::SetShadowTeleportDistance(Object,25.f);
 const auto* EqualityState=ElysiumModelPhysics::ShadowFor(Object);
 TestTrue(TEXT("strict threshold witness supplies exact binary native distance"),EqualityState->TeleportDistanceCm==63.5f);
 FTransform EqualityPose=Native->GetUnrealWorldTransform();
 EqualityPose.SetLocation(EqualityState->PredictedPosition+FVector(63.5,0,0));
 Native->SetBodyTransform(EqualityPose,ETeleportType::TeleportPhysics);
 Native->SetLinearVelocity(FVector::ZeroVector,false); Native->SetAngularVelocityInRadians(FVector::ZeroVector,false);
 ElysiumModelPhysics::UpdateShadow(Object,EqualityPose.GetLocation()+FVector(100,0,0),EqualityPose.GetRotation(),false,.1f);
 Tick();
 const auto EqualityEvents=ElysiumModelPhysics::ShadowEvents(Object);
 TestTrue(TEXT("strict teleport equality reaches actual simulation correction"),!EqualityEvents.IsEmpty());
 for (const auto& EqualityEvent : EqualityEvents)
 {
  TestTrue(TEXT("strict teleport comparison receives literal equality"),EqualityEvent.TeleportErrorSquared==4032.25);
  AddInfo(FString::Printf(TEXT("SHADOW_EQUALITY,eventX=%.17g predictionX=%.17g error2=%.17g simulated=%d teleport=%d actual=%s expected=%s"),
   EqualityEvent.Position.X,EqualityEvent.PredictionBefore.X,EqualityEvent.TeleportErrorSquared,EqualityEvent.bSimulated,EqualityEvent.bTeleported,
   *Native->GetUnrealWorldTransform().GetLocation().ToString(),*(EqualityPose.GetLocation()+FVector(10,0,0)).ToString()));
  TestTrue(TEXT("prediction error exactly threshold does not teleport"),EqualityEvent.bSimulated && !EqualityEvent.bTeleported);
 }
 TestTrue(TEXT("strict threshold equality completes ordinary velocity solve"),FMath::IsNearlyEqual(Native->GetUnrealWorldTransform().GetLocation().X,EqualityPose.GetLocation().X+10.,.2));
 ElysiumModelPhysics::SetShadowTeleportDistance(Object, 1.f);
 const auto* ThresholdState = ElysiumModelPhysics::ShadowFor(Object);
 FTransform DisplacedNativePose = Native->GetUnrealWorldTransform();
 DisplacedNativePose.SetLocation(ThresholdState->PredictedPosition + FVector(ThresholdState->TeleportDistanceCm * 2.,0,0));
 Native->SetBodyTransform(DisplacedNativePose, ETeleportType::TeleportPhysics);
 Native->SetLinearVelocity(FVector::ZeroVector, false);
 Native->SetAngularVelocityInRadians(FVector::ZeroVector, false);
 FVector ThresholdTarget = DisplacedNativePose.GetLocation() + FVector(100,0,0);
 // Hold rotation fixed here: Chaos rotates the offset hull about its native center of mass,
 // so actor-origin translation during a simultaneous turn is not linear velocity times dt.
 ElysiumModelPhysics::UpdateShadow(Object, ThresholdTarget, DisplacedNativePose.GetRotation(), false, .1f);
 const auto BeforeTeleportCollision = Native->GetShapeCollisionEnabled(0);
 Tick();
 const auto TeleportEvents=ElysiumModelPhysics::ShadowEvents(Object);
 TestTrue(TEXT("strict greater prediction error reaches actual PT teleport"),!TeleportEvents.IsEmpty());
 for (const auto& TeleportEvent : TeleportEvents)
  TestTrue(TEXT("actual PT teleport restores every native shape flag before solve"),TeleportEvent.bSimulated && TeleportEvent.bTeleported && TeleportEvent.bCollisionRestored);
 State = ElysiumModelPhysics::ShadowFor(Object);
 TestTrue(TEXT("prediction error above threshold teleports then retains preteleport velocity delta"),
  FMath::IsNearlyEqual(Native->GetUnrealWorldTransform().GetLocation().X, ThresholdTarget.X + 10., .2));
 TestTrue(TEXT("teleport prediction retains old current rather than target"), State && State->PredictedPosition.X < ThresholdTarget.X);
 TestEqual(TEXT("threshold teleport restores native shape collision"), Native->GetShapeCollisionEnabled(0), BeforeTeleportCollision);
 ElysiumModelPhysics::SetShadowTeleportDistance(Object, 0.f);
 // No clock/controller advance on zero/negative scale; logical mover may continue to submit.
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 0"));
 const double PausedTime = Solver->GetSolverTime(); Tick();
 TestTrue(TEXT("zero scale advances no actual native time"), FMath::IsNearlyEqual(Solver->GetSolverTime(), PausedTime, .00001));
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale -1")); Tick();
 TestTrue(TEXT("negative scale rejected by actual environment gate"), FMath::IsNearlyEqual(Solver->GetSolverTime(), PausedTime, .00001));
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 0.005")); Tick();
 TestTrue(TEXT("actual dt at .0001 strict environment boundary does not advance"), FMath::IsNearlyEqual(Solver->GetSolverTime(), PausedTime, .00001));
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 20")); Tick();
 TestTrue(TEXT("scaled native delta is capped at .1 after multiplication"), FMath::IsNearlyEqual(Solver->GetSolverTime() - PausedTime, .1, .00001));
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 1"));
 FElysiumEntityDef AliasDef; AliasDef.Classname = TEXT("info_target"); AliasDef.TargetName = TEXT("shadow_alias");
 FElysiumEntity* Alias = World->Resolve(World->SpawnRuntimeEntity(AliasDef));
 if (!Alias) return false;
 Alias->VPhysicsSetObject(Object); Alias->VPhysicsSwapObject(nullptr);
 TestTrue(TEXT("reference-only clear preserves producer controller and registry"), ShadowChild->VPhysicsGetObject() == Object &&
  ElysiumModelPhysics::ShadowFor(Object) && World->PhysicsShadows().Contains(ShadowChild->Handle));
 Alias->VPhysicsSetObject(Object);
 const int32 AliasIndex = Alias->Handle.Index;
 ElysiumModelPhysics::UpdateShadow(Object, SecondTarget, FullTargetRotation, false, .2f);
 FElysiumMapSnapshot Snapshot; World->Freeze(Snapshot);
 if (!TestFalse(TEXT("actual shadow Physics save admitted"), Snapshot.bPhysicsSaveFailed)) return false;
 const auto Saved = Snapshot.PhysicsBodies.FindByPredicate([ShadowChild](const FElysiumSavedPhysicsBody& Body) { return Body.EntityIndex == ShadowChild->Handle.Index; });
 if (Saved) AddInfo(FString::Printf(TEXT("shadow saved installed=%d member=%d originalmass=%.9g"), Saved->Shadow.bInstalled, Saved->bShadowMember, Saved->Shadow.SavedMassKg));
 else AddInfo(FString::Printf(TEXT("shadow saved missing: eligible=%d rows=%d"), ShadowChild->ShouldSavePhysics(), Snapshot.PhysicsBodies.Num()));
 if (!TestTrue(TEXT("save retains original controller body state and membership"), Saved && Saved->Shadow.bInstalled && Saved->bShadowMember && FMath::IsNearlyEqual(Saved->Shadow.SavedMassKg, 85.f, .001f))) return false;
 const FTransform Submitted = Saved->Shadow.Target; const FTransform Solved = Saved->Pose;
 const FElysiumSavedPhysicsBody SavedMechanics = *Saved;
	const float PersistedOriginalLinearDamping = Saved->Shadow.SavedLinearDamping, PersistedOriginalAngularDamping = Saved->Shadow.SavedAngularDamping;
 for (int32 InvalidArm = 0; InvalidArm < 10; ++InvalidArm)
 {
  FElysiumSavedPhysicsBody Invalid = *Saved;
  if (InvalidArm == 0) Invalid.Shadow.RemainingSeconds = -1.f;
  if (InvalidArm == 1) Invalid.Shadow.RemainingSeconds = std::numeric_limits<float>::quiet_NaN();
  if (InvalidArm == 2) Invalid.Recipe.bStatic = true;
  if (InvalidArm == 3) Invalid.Shadow.bInstalled = false;
  if (InvalidArm == 4) Invalid.CurrentInertia.X = std::numeric_limits<double>::infinity();
  if (InvalidArm == 5) Invalid.CurrentInverseInertia.Y = -1.;
  if (InvalidArm == 6) Invalid.CurrentMassFrame.SetRotation(FQuat(0,0,0,0));
  if (InvalidArm == 7) Invalid.Shadow.SavedInverseInertia.Z = std::numeric_limits<double>::quiet_NaN();
  if (InvalidArm == 8) Invalid.Shadow.SavedMassFrame.SetRotation(FQuat(0,0,0,0));
  if (InvalidArm == 9) Invalid.CurrentMassFrame.SetTranslation(FVector(std::numeric_limits<double>::infinity(),0,0));
  TArray<uint8> InvalidBytes; FMemoryWriter InvalidWriter(InvalidBytes, true); InvalidWriter << Invalid;
  FElysiumSavedPhysicsBody InvalidRead; FMemoryReader InvalidReader(InvalidBytes, true); InvalidReader << InvalidRead;
  TestTrue(TEXT("malformed persisted shadow state refuses archive read"), InvalidReader.IsError());
  auto* InvalidComponent = NewObject<UElysiumPhysicsPropComponent>(Map);
  Map->AddInstanceComponent(InvalidComponent); InvalidComponent->RegisterComponent();
  FString InvalidError;
  TestFalse(TEXT("malformed shadow state refuses actual native reconstruction"), ElysiumModelPhysics::Restore(InvalidComponent, World->PhysicsSurfaces(), Invalid, InvalidError));
  TestNull(TEXT("failed shadow reconstruction leaves no native incarnation"), ElysiumModelPhysics::ActorIdentity(InvalidComponent));
  TestTrue(TEXT("failed reconstruction leaves live owner and membership intact"), ShadowChild->VPhysicsGetObject() == Object && World->PhysicsShadows().Contains(ShadowChild->Handle));
  InvalidComponent->DestroyComponent();
 }
 TArray<uint8> PhysicsArchive; FMemoryWriter PhysicsWriter(PhysicsArchive, true);
 FElysiumSaveArchive PhysicsSave(PhysicsWriter, FElysiumSaveVersion::Latest); PhysicsSave << Snapshot;
 if (!TestFalse(TEXT("actual shadow snapshot archive writes"), PhysicsWriter.IsError())) return false;
 FElysiumMapSnapshot PersistedSnapshot; FMemoryReader PhysicsReader(PhysicsArchive, true);
 FElysiumSaveArchive PhysicsLoad(PhysicsReader, FElysiumSaveVersion::Latest); PhysicsLoad << PersistedSnapshot;
 if (!TestFalse(TEXT("actual shadow snapshot archive reads"), PhysicsReader.IsError())) return false;
 Snapshot = MoveTemp(PersistedSnapshot); Snapshot.PhysicsBodies.Reset(); // restore must consume Physics block bytes
 const int32 ChildIndex = ShadowChild->Handle.Index;
 bool bRestoredThroughStage = false;
 bool bRestoredNativePose = false; bool bRestoredController = false;
 if (!TestTrue(TEXT("production rebuild creates inactive restore world"), Map->RebuildStageWorld(MoveTemp(RestoreDefs), {}, Seat, Error))) return false;
 Map->RestoreBeforeActivation = [&]()
 {
  World = Map->GetEntityWorld();
  bRestoredThroughStage = World && World->ApplySnapshot(Snapshot) >= 0;
  if (bRestoredThroughStage)
  {
   FElysiumEntity* RestoredChild = World->Resolve(FElysiumEntityHandle(ChildIndex, World->GetEpoch()));
   void* RestoredObject = RestoredChild ? RestoredChild->VPhysicsGetObject() : nullptr;
   const auto* RestoredController = ElysiumModelPhysics::ShadowFor(RestoredObject);
   const FBodyInstance* RestoredNative = ElysiumModelPhysics::Identity(ElysiumModelPhysics::ComponentFor(RestoredObject));
   bRestoredNativePose = RestoredNative && RestoredNative->GetUnrealWorldTransform().Equals(Solved, .001);
   bRestoredController = RestoredController && RestoredController->Target.Equals(Submitted, .001) &&
    FMath::IsNearlyEqual(RestoredController->RemainingSeconds, .2f, .0001f);
  }
  return bRestoredThroughStage;
 };
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("actual restore barrier reconnects before activation"), bRestoredThroughStage && Map->IsRuntimeActive())) return false;
 ShadowChild = World->Resolve(FElysiumEntityHandle(ChildIndex, World->GetEpoch()));
 if (!ShadowChild) return false;
 Object = ShadowChild->VPhysicsGetObject(); State = ElysiumModelPhysics::ShadowFor(Object);
 TestTrue(TEXT("restore barrier preserves submitted target and countdown before first simulation"), bRestoredController);
 TestTrue(TEXT("reconnect preserves submitted target without resubmitting"), State && State->Target.Equals(Submitted, .001));
 Native = ElysiumModelPhysics::Identity(ElysiumModelPhysics::ComponentFor(Object));
 TestTrue(TEXT("restore barrier preserves actual solved pose independently"), bRestoredNativePose);
	TestEqual(TEXT("actual Physics reconstruction retains absent linear damping input"),ElysiumModelPhysics::ComponentFor(Object)->GetLinearDamping(),0.f);
	TestEqual(TEXT("actual Physics reconstruction retains absent angular damping input"),ElysiumModelPhysics::ComponentFor(Object)->GetAngularDamping(),0.f);
	TestTrue(TEXT("actual Physics reconstruction retains originals independently of current zero inputs"),State && State->SavedLinearDamping==PersistedOriginalLinearDamping && State->SavedAngularDamping==PersistedOriginalAngularDamping);
	FElysiumSavedPhysicsBody RestoredMechanics;
	TestTrue(TEXT("restored shadow captures actual native mechanics producers"), ElysiumModelPhysics::Capture(Object,RestoredMechanics));
	TestEqual(TEXT("installed shadow roundtrip restores reserved current identity before first event"),RestoredMechanics.CurrentSurface,FString(TEXT("$MATERIAL_INDEX_SHADOW")));
	TestTrue(TEXT("installed shadow roundtrip restores controller independently"),RestoredMechanics.Shadow.bInstalled);
	TestTrue(TEXT("restored shadow retains finite current I and reciprocal separately from originals"),
		RestoredMechanics.CurrentInertia == SavedMechanics.CurrentInertia && RestoredMechanics.CurrentInverseInertia == SavedMechanics.CurrentInverseInertia &&
		RestoredMechanics.CurrentMassFrame.Equals(SavedMechanics.CurrentMassFrame,1.e-6));
 TestTrue(TEXT("reconnect restores owner registry membership"), World->PhysicsShadows().Contains(ShadowChild->Handle));
 const float BeforeRestoredCountdown = State->RemainingSeconds;
 auto* RestoredSolver = Native->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>();
 const double BeforeRestoredTime = RestoredSolver->GetSolverTime();
 const FVector BeforeRestoredPose = Native->GetUnrealWorldTransform().GetLocation();
 Tick();
 State = ElysiumModelPhysics::ShadowFor(Object);
 const double RestoredNativeDelta = RestoredSolver->GetSolverTime() - BeforeRestoredTime;
 TestTrue(TEXT("first scheduled restored native frame advances actual scene"), RestoredNativeDelta > 0.);
 TestTrue(TEXT("first restored native frame preserves target and consumes persisted countdown"), State && State->Target.Equals(Submitted, .001) &&
  FMath::IsNearlyEqual(State->RemainingSeconds, FMath::Max(0.f, BeforeRestoredCountdown - static_cast<float>(RestoredNativeDelta)), .0001f));
 TestTrue(TEXT("first restored native frame solves from saved native pose and velocity"), !Native->GetUnrealWorldTransform().GetLocation().Equals(BeforeRestoredPose, .001));
 Alias = World->Resolve(FElysiumEntityHandle(AliasIndex, World->GetEpoch()));
 TestTrue(TEXT("saved alias reconstructs independent controller without owner membership"), Alias && Alias->VPhysicsGetObject() != Object &&
  ElysiumModelPhysics::ShadowFor(Alias->VPhysicsGetObject()) && !World->PhysicsShadows().Contains(Alias->Handle));
	FElysiumSavedPhysicsBody InstalledAlias;
	TestTrue(TEXT("installed alias captures reconstructed current material"),Alias && ElysiumModelPhysics::Capture(Alias->VPhysicsGetObject(),InstalledAlias));
	TestTrue(TEXT("installed alias retains reserved current material and independent controller"),InstalledAlias.CurrentSurface==TEXT("$MATERIAL_INDEX_SHADOW") && InstalledAlias.Shadow.bInstalled);
 Component = ElysiumModelPhysics::ComponentFor(Object);
 FBodyInstance* RetiringBody = ElysiumModelPhysics::Identity(Component);
 const float OriginalMass = State->SavedMassKg;
	const float OriginalLinearDamping = State->SavedLinearDamping, OriginalAngularDamping = State->SavedAngularDamping;
 const FVector OriginalInertia = State->SavedInertia; const bool bOriginalGravity = State->bSavedGravity;
 const FVector OriginalInverseInertia = State->SavedInverseInertia; const FTransform OriginalMassFrame = State->SavedMassFrame;
 bool bObservedRetirement = false;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 {
  if (Event.Kind != TEXT("physics_retire") || Event.Entity != ShadowChild->Handle) return;
  bObservedRetirement = true;
  TestNull(TEXT("225 retires controller before callback/native release"), ElysiumModelPhysics::ShadowFor(Object));
  TestFalse(TEXT("225 removes owner membership before callback"), World->PhysicsShadows().Contains(ShadowChild->Handle));
  TestTrue(TEXT("controller teardown restores original native mass before release"), RetiringBody->IsValidBodyInstance() && FMath::IsNearlyEqual(RetiringBody->GetBodyMass(), OriginalMass, .001f));
  TestTrue(TEXT("controller teardown restores original native inertia and gravity"), RetiringBody->GetBodyInertiaTensor().Equals(OriginalInertia, .001) && RetiringBody->bEnableGravity == bOriginalGravity);
  FVector RetiringInverse;
  FPhysicsCommand::ExecuteRead(RetiringBody->GetPhysicsActor(),[&](const FPhysicsActorHandle& ReadActor)
  { RetiringInverse=FVector(ReadActor->GetGameThreadAPI().InvI()); }); //recipe retired; native handle is still live before release
  TestTrue(TEXT("controller teardown restores original paired inverse and principal frame before release"),
   RetiringInverse==OriginalInverseInertia && RetiringBody->GetMassSpaceLocal().Equals(OriginalMassFrame,1.e-6));
	 const auto* RetiringMaterial=Cast<UElysiumPhysicalMaterial>(RetiringBody->GetSimplePhysicalMaterial()); //GT UObject remains owned while native release is pending
	 TestTrue(TEXT("225 retains reserved current material before native release"),RetiringMaterial && RetiringMaterial->SourceName==TEXT("$MATERIAL_INDEX_SHADOW"));
	 TestEqual(TEXT("persisted shadow teardown restores original linear damping"),Component->GetLinearDamping(),OriginalLinearDamping);
	 TestEqual(TEXT("persisted shadow teardown restores original angular damping"),Component->GetAngularDamping(),OriginalAngularDamping);
 });
 ShadowChild->VPhysicsDestroyObject(); World->SetAiTraceSink({});
 TestTrue(TEXT("actual production225 exposes pre-release observer"), bObservedRetirement);
 TestNull(TEXT("225 leaves owner null"), ShadowChild->VPhysicsGetObject());
 TestNotNull(TEXT("independent saved alias survives owner deletion"), Alias ? Alias->VPhysicsGetObject() : nullptr);
 TestEqual(TEXT("deleted shadow47 fails and returns0"), ElysiumModelPhysics::GetShadowPosition(Object, nullptr, nullptr), 0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMovingBrushRestoreFrameTest, "Elysium.Substrate.ModelPhysics.MovingBrushRestoreFrame",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumMovingBrushRestoreFrameTest::RunTest(const FString&)
{
 FPlayerWorldFixture Fixture;
 if (!Fixture.CreateWorld(*this)) return false;
 Fixture.SpawnPlayerControllerAndPawn(FVector(-1000,0,1000)); Fixture.PlayerController->Possess(Fixture.Pawn);
 auto* Map = Fixture.SpawnMapActorDeferred(); Map->bStageOnly = true; Map->bStageWithoutCatalogue = true;
 Map->FinishSpawning(FTransform::Identity);
 auto Tick = [&]() { return Fixture.TestWorld.TickTestWorld(.02f); };
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("production initial activation"), Map->IsRuntimeActive())) return false;
 FElysiumEntityDefs Defs; Defs.MapName = TEXT("moving_brush_restore_native");
 FElysiumEntityDef WorldDef; WorldDef.Classname = TEXT("worldspawn"); Defs.Defs.Add(WorldDef);
 FElysiumConvexHull Hull;
 for (float X : {0.f,120.f}) for (float Y : {-10.f,10.f}) for (float Z : {-10.f,10.f}) Hull.Vertices.Add(FVector(X,Y,Z));
 FElysiumEntityDef PoseParent; PoseParent.Classname = TEXT("func_door"); PoseParent.TargetName = TEXT("pose_parent");
 PoseParent.Model = 5; PoseParent.Contents = 1; PoseParent.Hulls.Add(Hull); PoseParent.Keys.Add(TEXT("model"),TEXT("*5"));
 PoseParent.Origin = FVector(5000,7000,3000); PoseParent.Keys.Add(TEXT("angles"),TEXT("10 370 20")); Defs.Defs.Add(PoseParent);
 for (int32 Index = 0; Index < 4; ++Index)
 {
  FElysiumEntityDef Door; Door.Classname = Index == 1 ? TEXT("func_door_rotating") : TEXT("func_door");
  Door.TargetName = Index == 0 ? TEXT("moving_slide") : Index == 1 ? TEXT("moving_rotation") : Index == 2 ? TEXT("resting_slide") : TEXT("parented_slide");
  Door.Model = Index+1; Door.Origin = FVector(3000,4000+Index*1000,3000); Door.Hulls.Add(Hull); Door.Contents = 1;
  Door.Keys.Add(TEXT("model"), FString::Printf(TEXT("*%d"), Door.Model));
  Door.Keys.Add(TEXT("speed"), Index == 1 ? TEXT("30") : TEXT("20")); Door.Keys.Add(TEXT("distance"),TEXT("90"));
  Door.Keys.Add(TEXT("wait"),TEXT("2")); Door.Keys.Add(TEXT("spawnflags"),Index == 1 ? TEXT("1") : TEXT("0"));
  if (Index == 3) { Door.Keys.Add(TEXT("parentname"),TEXT("pose_parent")); Door.Keys.Add(TEXT("angles"),TEXT("15 725 35")); }
  Defs.Defs.Add(Door);
 }
 FElysiumEntityDefs RestoreDefs = Defs; FElysiumStageSeat Seat; Seat.FeetCm = FVector(-1000,0,1000); FString Error;
 if (!Map->RebuildStageWorld(MoveTemp(Defs),{},Seat,Error)) { AddError(Error); return false; }
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("production authored brush map active"),Map->IsRuntimeActive())) return false;
 FElysiumEntityWorld* World = Map->GetEntityWorld();
 FElysiumEntity* Slide = World->FindByName(TEXT("moving_slide")); FElysiumEntity* Rotation = World->FindByName(TEXT("moving_rotation"));
 FElysiumEntity* Rest = World->FindByName(TEXT("resting_slide"));
 FElysiumEntity* Parented = World->FindByName(TEXT("parented_slide"));
 if (!Slide || !Rotation || !Rest || !Parented) return false;
 FElysiumEntity* NativeParent = World->FindByName(TEXT("pose_parent"));
 if (!TestTrue(TEXT("authored parent has real native brush and attachment body"), NativeParent && NativeParent->Body && NativeParent->GetAttachBody() == NativeParent->Body && NativeParent->VPhysicsGetObject())) return false;
 TestEqual(TEXT("child has actual logical brush parent before capture"),Parented->MoveParent,NativeParent->Handle);
 TestNull(TEXT("independent solved child is never welded into scene parent"),Parented->GetAttachChild());
 World->AcceptInput(Slide->Handle,FName(TEXT("Open")),FElysiumVariant::Void(),{},{});
 World->AcceptInput(Rotation->Handle,FName(TEXT("Open")),FElysiumVariant::Void(),{},{});
 World->AcceptInput(Parented->Handle,FName(TEXT("Open")),FElysiumVariant::Void(),{},{});
 for (int32 Frame = 0; Frame < 5; ++Frame) Tick();
 TestTrue(TEXT("both authored movers remain in flight"), Slide->AsDoorBase()->MoveKind() == FElysiumMoverBase::EMoveKind::Linear && Rotation->AsDoorBase()->MoveKind() == FElysiumMoverBase::EMoveKind::Angular);
 TestTrue(TEXT("parented authored mover in flight before capture"),Parented->AsDoorBase()->MoveKind() == FElysiumMoverBase::EMoveKind::Linear && Parented->VPhysicsGetObject());
 const FVector SlidePose = Slide->GetOrigin(), RotationPose = Rotation->GetAngles();
 const FVector ParentLocal = Parented->GetOrigin(), ParentAngles = Parented->GetAngles();
 const FVector ParentAbsolute = Parented->GetAbsOrigin(), ParentAbsAngles = Parented->GetAbsAngles();
 const float ParentClock = Parented->LocalTime, ParentDeadline = Parented->PhysicsMoveDoneTime;
 TestTrue(TEXT("authored parented native mover reaches local/absolute separation"),Parented->bParentLocalPose && Parented->bNativeLocalPose && !ParentLocal.Equals(ParentAbsolute,.001));
 const float SlideClock = Slide->LocalTime, RotationClock = Rotation->LocalTime;
 const float SlideDeadline = Slide->PhysicsMoveDoneTime, RotationDeadline = Rotation->PhysicsMoveDoneTime;
 const FVector SlideVelocity = Slide->Velocity, RotationVelocity = Rotation->AngularVelocity;
 // Resting authored think is a future real auto-close receiver, not a restoration seating task.
 Rest->NextThink = static_cast<float>(World->NowSeconds()+1.25);
 const float RestThink = Rest->NextThink;
 const int32 SlideIndex = Slide->Handle.Index, RotationIndex = Rotation->Handle.Index, RestIndex = Rest->Handle.Index;
 const int32 ParentIndex = Parented->Handle.Index;
 FElysiumMapSnapshot Snapshot; World->Freeze(Snapshot);
 TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true); FElysiumSaveArchive Save(Writer,FElysiumSaveVersion::Latest); Save << Snapshot;
 FElysiumMapSnapshot Decoded; FMemoryReader Reader(Bytes,true); FElysiumSaveArchive Load(Reader,FElysiumSaveVersion::Latest); Load << Decoded;
 if (!TestFalse(TEXT("moving brush archive is valid"),Writer.IsError() || Reader.IsError())) return false;
 bool bRestored = false; int32 ArrivalOutputs = 0;
 if (!Map->RebuildStageWorld(MoveTemp(RestoreDefs),{},Seat,Error)) { AddError(Error); return false; }
 Map->RestoreBeforeActivation = [&]()
 {
  World = Map->GetEntityWorld(); bRestored = World && World->ApplySnapshot(Decoded,Snapshot.SaveBase)>=0;
  if (!bRestored) return false;
  Slide = World->Resolve(FElysiumEntityHandle(SlideIndex,World->GetEpoch()));
  Rotation = World->Resolve(FElysiumEntityHandle(RotationIndex,World->GetEpoch()));
  Rest = World->Resolve(FElysiumEntityHandle(RestIndex,World->GetEpoch()));
  Parented = World->Resolve(FElysiumEntityHandle(ParentIndex,World->GetEpoch()));
  if (!Slide || !Rotation || !Rest || !Parented) return false;
  TestEqual(TEXT("restore preserves sliding local clock"),Slide->LocalTime,SlideClock);
  TestEqual(TEXT("restore preserves angular local clock"),Rotation->LocalTime,RotationClock);
  TestEqual(TEXT("restore preserves sliding deadline"),Slide->PhysicsMoveDoneTime,SlideDeadline);
  TestEqual(TEXT("restore preserves angular deadline"),Rotation->PhysicsMoveDoneTime,RotationDeadline);
  TestEqual(TEXT("resting restore preserves authored future think"),Rest->NextThink,RestThink);
  TestTrue(TEXT("restore preserves local velocity and full angular velocity words"),Slide->Velocity.Equals(SlideVelocity,.001) && Rotation->AngularVelocity.Equals(RotationVelocity,.001));
  TestTrue(TEXT("restore retains saved in-flight logical poses"),Slide->GetOrigin().Equals(SlidePose,.001) && Rotation->GetAngles().Equals(RotationPose,.001));
  TestTrue(TEXT("parented restore retains local and absolute poses and full Euler words"),Parented->GetOrigin().Equals(ParentLocal,.001) && Parented->GetAngles().Equals(ParentAngles,.001) && Parented->GetAbsOrigin().Equals(ParentAbsolute,.002) && Parented->GetAbsAngles().Equals(ParentAbsAngles,.001));
  TestEqual(TEXT("parented restore retains local clock"),Parented->LocalTime,ParentClock);
  TestEqual(TEXT("parented restore retains deadline"),Parented->PhysicsMoveDoneTime,ParentDeadline);
  AddInfo(FString::Printf(TEXT("saved logical slide=%s restored=%s; saved rotation=%s restored=%s"),*SlidePose.ToString(),*Slide->GetOrigin().ToString(),*RotationPose.ToString(),*Rotation->GetAngles().ToString()));
  for (FElysiumEntity* Entity : {Slide,Rotation,Rest,Parented})
  {
   const auto* Recipe = ElysiumModelPhysics::RecipeFor(Entity->VPhysicsGetObject());
   const auto* Shadow = ElysiumModelPhysics::ShadowFor(Entity->VPhysicsGetObject());
   TestTrue(TEXT("false ShouldSavePhysics recreates authored brush recipe and controller"), Recipe && Recipe->Kind == EElysiumPhysicsRecipeKind::Brush && Recipe->BrushHulls.Num()==1 && Shadow && Shadow->SavedMassKg==1.f);
   TestTrue(TEXT("restored native producer is the entity query body"),Entity->Body && ElysiumModelPhysics::OwnedComponent(*Entity)==Entity->Body);
   const auto* Native = ElysiumModelPhysics::Identity(Entity->Body);
   TestTrue(TEXT("restored brush native geometry is seated at saved logical pose"),Native && Native->GetUnrealWorldTransform().GetLocation().Equals(Entity->GetAbsOrigin(),.001));
  }
  World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
  { if (Event.Kind == TEXT("output") && (Event.Entity.Index==SlideIndex || Event.Entity.Index==RotationIndex) && Event.Text.StartsWith(TEXT("OnFully"))) ++ArrivalOutputs; });
  return true;
 };
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("production restoration barrier becomes active"),bRestored && Map->IsRuntimeActive())) return false;
 TestEqual(TEXT("restore emits no invented arrivals"),ArrivalOutputs,0);
 const float ReentryClock = Slide->LocalTime;
 Tick();
 TestTrue(TEXT("first actual restored frame advances native mover clock"),Slide->LocalTime > ReentryClock);
 TestEqual(TEXT("first native frame retains resting authored think"),Rest->NextThink,RestThink);
 const auto* ParentTarget = ElysiumModelPhysics::ShadowFor(Parented->VPhysicsGetObject());
 TestTrue(TEXT("first restored frame submits inherited parented target"),ParentTarget && ParentTarget->Target.GetLocation().Equals(Parented->GetAbsOrigin(),.002));
 TestEqual(TEXT("first native frame does not invent arrival"),ArrivalOutputs,0);
 for (int32 Frame = 0; Frame < 220 && ArrivalOutputs<2; ++Frame) Tick();
 TestEqual(TEXT("both restored native feedback receivers arrive once"),ArrivalOutputs,2);
 TestTrue(TEXT("START_OPEN restored angular move reaches the once-swapped authored endpoint"),Rotation->GetAngles().Equals(FVector::ZeroVector,.001));
 TestTrue(TEXT("native arrival snap helpers write minus-one deadline and clear move kind"),Slide->PhysicsMoveDoneTime==-1.f && Rotation->PhysicsMoveDoneTime==-1.f && Slide->AsDoorBase()->MoveKind()==FElysiumMoverBase::EMoveKind::None && Rotation->AsDoorBase()->MoveKind()==FElysiumMoverBase::EMoveKind::None);
 World->SetAiTraceSink({});
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDynamicPublicationFrameTest, "Elysium.Substrate.ModelPhysics.DynamicPublicationFrame",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumDynamicPublicationFrameTest::RunTest(const FString&)
{
 const FString PreviousTimeScale = ElysiumCommandBus::Console().GetCvar(TEXT("phys_timescale"));
 ON_SCOPE_EXIT { ElysiumCommandBus::Console().Execute(FString(TEXT("phys_timescale ")) + PreviousTimeScale); };
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 1"));
 FPlayerWorldFixture Fixture;
 if (!Fixture.CreateWorld(*this)) return false;
 Fixture.SpawnPlayerControllerAndPawn(FVector(-1000,0,1000)); Fixture.PlayerController->Possess(Fixture.Pawn);
 auto* Map = Fixture.SpawnMapActorDeferred(); Map->bStageOnly = true; Map->bStageWithoutCatalogue = true;
 Map->FinishSpawning(FTransform::Identity);
 auto Tick = [&]() { return Fixture.TestWorld.TickTestWorld(.02f); };
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("initial production stage active"), Map->IsRuntimeActive())) return false;
 FElysiumEntityDefs Defs; Defs.MapName = TEXT("dynamic_publication_native");
 FElysiumEntityDef WorldDef; WorldDef.Classname = TEXT("worldspawn"); Defs.Defs.Add(WorldDef);
 auto Parent = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("dynamic"));
 Parent.Origin = FVector(3000,4000,3000); Defs.Defs.Add(Parent);
 auto SecondDef = Parent; SecondDef.TargetName = TEXT("second"); SecondDef.Origin += FVector(0,2000,0); Defs.Defs.Add(SecondDef);
 auto ThirdDef = Parent; ThirdDef.TargetName = TEXT("third"); ThirdDef.Origin += FVector(0,3000,0); Defs.Defs.Add(ThirdDef);
 auto Child = Parent; Child.TargetName = TEXT("child"); Child.Classname = TEXT("prop_dynamic");
 Child.Origin += FVector(0,500,0); Child.Keys.Add(TEXT("parentname"), TEXT("dynamic"));
 Child.Keys.Add(TEXT("solid"), TEXT("2")); Child.Keys.Add(TEXT("spawnflags"), TEXT("0")); Defs.Defs.Add(Child);
 auto GrandchildDef = Child; GrandchildDef.TargetName = TEXT("grandchild");
 GrandchildDef.Origin += FVector(0,500,0); GrandchildDef.Keys[TEXT("parentname")] = TEXT("child"); Defs.Defs.Add(GrandchildDef);
 FElysiumEntityDef Trigger; Trigger.Classname = TEXT("trigger_multiple"); Trigger.TargetName = TEXT("observer");
 Trigger.Model = 1; Trigger.Origin = Parent.Origin; Trigger.Keys.Add(TEXT("model"), TEXT("*1")); Trigger.Keys.Add(TEXT("spawnflags"), TEXT("8"));
 FElysiumConvexHull Hull;
 for (float X : { -150.f,150.f }) for (float Y : { -150.f,150.f }) for (float Z : { -150.f,150.f }) Hull.Vertices.Add(FVector(X,Y,Z));
 Trigger.Hulls.Add(Hull); Defs.Defs.Add(Trigger);
 auto AliasDef = Parent; AliasDef.Classname = TEXT("info_target"); AliasDef.TargetName = TEXT("alias"); Defs.Defs.Add(AliasDef);
 FElysiumStageSeat Seat; Seat.FeetCm = FVector(-1000,0,1000); FString Error;
 if (!Map->RebuildStageWorld(MoveTemp(Defs), {}, Seat, Error)) { AddError(Error); return false; }
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("rebuilt production stage active"), Map->IsRuntimeActive())) return false;
 auto* World = Map->GetEntityWorld(); auto* Dynamic = World->FindByName(TEXT("dynamic"));
 auto* ShadowChild = World->FindByName(TEXT("child")); auto* Alias = World->FindByName(TEXT("alias"));
 auto* Grandchild = World->FindByName(TEXT("grandchild"));
 auto* PhysicalTrigger = static_cast<FElysiumTriggerBase*>(World->FindByName(TEXT("observer")));
 if (!Dynamic || !ShadowChild || !Grandchild || !Alias || !PhysicalTrigger) return false;
 void* Object = Dynamic->VPhysicsGetObject(); auto* Component = ElysiumModelPhysics::ComponentFor(Object);
 auto* Native = ElysiumModelPhysics::Identity(Component);
 if (!TestNotNull(TEXT("actual Chaos body"), Native)) return false;
 AddInfo(FString::Printf(TEXT("native bounds=%s actor=%s logical=%s solid=%d flags=%u attach=%s"), *Component->Bounds.GetBox().ToString(), *Native->GetUnrealWorldTransform().GetLocation().ToString(), *Dynamic->GetAbsOrigin().ToString(), Dynamic->GetSolid(), Dynamic->RetailSolidFlags, *GetNameSafe(Dynamic->GetAttachBody())));
 Component->SetEnableGravity(false);
 // SetObject aliases do not replace creation-owned native game data.
 Alias->VPhysicsSetObject(Object); Dynamic->VPhysicsSetObject(nullptr);
 const FQuat FullRotation = ElysiumModelPhysics::SourceRotation(FVector(10,20,30));
 Native->SetBodyTransform(FTransform(FullRotation, Native->GetUnrealWorldTransform().GetLocation()), ETeleportType::TeleportPhysics);
 FVector PoseOrigin; FRotator PoseAngles;
 TestTrue(TEXT("live object37 outputs"), ElysiumModelPhysics::GetPosition(Object, PoseOrigin, PoseAngles));
 TestTrue(TEXT("retail object converter full pitch yaw roll"), FMath::IsNearlyEqual(PoseAngles.Pitch,10.,.001) && FMath::IsNearlyEqual(PoseAngles.Yaw,20.,.001) && FMath::IsNearlyEqual(PoseAngles.Roll,30.,.001));
 // Independent known Source rotations: normal roll remains [0,360], yaw retains its quadrant.
 for (const FVector& InputAngles : { FVector(-35,-140,-70), FVector(45,140,170), FVector(89.9,40,60) })
 {
  const FQuat NativeRotation(FRotator(-InputAngles.X,-InputAngles.Y,InputAngles.Z));
  Native->SetBodyTransform(FTransform(NativeRotation, PoseOrigin), ETeleportType::TeleportPhysics);
  ElysiumModelPhysics::GetPosition(Object, PoseOrigin, PoseAngles);
  const double ExpectedRoll = InputAngles.Z < 0 ? InputAngles.Z + 360. : InputAngles.Z;
  TestTrue(TEXT("live object37 negative angles quadrants and above gimbal threshold"),
   FMath::IsNearlyEqual(PoseAngles.Pitch,InputAngles.X,.003) && FMath::IsNearlyEqual(PoseAngles.Yaw,InputAngles.Y,.003) && FMath::IsNearlyEqual(PoseAngles.Roll,ExpectedRoll,.003));
 }
 Native->SetBodyTransform(FTransform(FQuat::Identity, PoseOrigin), ETeleportType::TeleportPhysics);
 ElysiumModelPhysics::GetPosition(Object, PoseOrigin, PoseAngles);
 TestEqual(TEXT("identity object37 preserves signed-zero roll result"), PoseAngles.Roll, 360.);
 Native->SetBodyTransform(FTransform(FullRotation, PoseOrigin), ETeleportType::TeleportPhysics);
 Native->SetLinearVelocity(FVector(300,0,0), false); Native->WakeInstance();
 const FVector Initial = Native->GetUnrealWorldTransform().GetLocation();
 int32 Origins = 0, Angles = 0, Targets = 0, Observers = 0, UntouchObservers = 0;
 bool bQueuedUntouchObserver = false;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 {
  if (Event.Entity == Dynamic->Handle && Event.Kind == TEXT("retail_site"))
  {
   if (Origins < 2 && Event.Text.Contains(TEXT("tag=physics_touch_query"))) AddInfo(Event.Text);
   if (!bQueuedUntouchObserver && Event.Text.Contains(TEXT("tag=physics_touch_query"))
    && Event.Text.Contains(TEXT("triggers=0")) && Dynamic->IsCurrentlyTouching())
   {
    bQueuedUntouchObserver = true;
    World->EnqueueInput(TEXT("observer"), FName(TEXT("Disable")), FElysiumVariant::Void(), 0., FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
   }
   if (Event.Text.Contains(TEXT("tag=set_abs_origin"))) ++Origins;
   if (Event.Text.Contains(TEXT("tag=set_abs_angles"))) { ++Angles; TestEqual(TEXT("216 precedes218"), Origins, Angles); }
  }
  if (Event.Entity == ShadowChild->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=shadow_target")))
  { ++Targets; TestTrue(TEXT("child232 follows solved angles"), Angles > 0); }
  if (Event.Name == TEXT("observer") && Event.Kind == TEXT("input") && Event.Text.StartsWith(TEXT("Enable ")))
  {
   ++Observers;
   TestTrue(TEXT("queued observer sees actual solved pose"), Dynamic->GetAbsOrigin().Equals(Native->GetUnrealWorldTransform().GetLocation(), .001));
   TestTrue(TEXT("queued observer follows touch and child232"), Dynamic->IsCurrentlyTouching() && Targets > 0);
   const auto* Shadow = ElysiumModelPhysics::ShadowFor(ShadowChild->VPhysicsGetObject());
   TestTrue(TEXT("child target is current inherited pose"), Shadow && Shadow->Target.GetLocation().Equals(ShadowChild->GetAbsOrigin(), .001));
  }
  if (Event.Name == TEXT("observer") && Event.Kind == TEXT("input") && Event.Text.StartsWith(TEXT("Disable ")))
  {
   ++UntouchObservers;
   TestFalse(TEXT("same-frame queued observer follows deferred untouch"), Dynamic->IsCurrentlyTouching());
   TestTrue(TEXT("untouch observer sees current native solved pose"), Dynamic->GetAbsOrigin().Equals(Native->GetUnrealWorldTransform().GetLocation(), .001));
  }
 });
 World->EnqueueInput(TEXT("observer"), FName(TEXT("Enable")), FElysiumVariant::Void(), 0., FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
 Tick(); Tick();
 TestEqual(TEXT("two native frames dispatch owner despite null current field"), Origins, 2);
 TestEqual(TEXT("two angle publications"), Angles, 2); TestEqual(TEXT("two child targets"), Targets, 2);
 TestEqual(TEXT("queued observer once"), Observers, 1);
 TestTrue(TEXT("native solve advances pose on following frame"), Native->GetUnrealWorldTransform().GetLocation().X > Initial.X + 6.);
 TestTrue(TEXT("following frame retains solved absolute and local words"), Dynamic->GetAbsOrigin().Equals(Native->GetUnrealWorldTransform().GetLocation(), .001) && Dynamic->GetOrigin().Equals(Dynamic->GetAbsOrigin(), .001));
 TestNull(TEXT("publication did not repopulate current object field"), Dynamic->VPhysicsGetObject());
 TestTrue(TEXT("null field publication preserves native full rotation"), Native->GetUnrealWorldTransform().GetRotation().Equals(FullRotation,.00001));
 TestTrue(TEXT("published full source angle words"), Dynamic->GetAbsAngles().Equals(FVector(10,20,30),.001));
 TestTrue(TEXT("physical move7 passes trigger eligibility"), PhysicalTrigger->LastTriggerTime > -1.0e15);
 Native->SetLinearVelocity(FVector(1000,0,0), false); ElysiumModelPhysics::Wake(Component);
 for (int32 Frame = 0; Frame < 40; ++Frame) Tick();
 TestFalse(TEXT("native geometry left candidate; deferred untouch clears pair"), Dynamic->IsCurrentlyTouching());
 TestEqual(TEXT("actual leaving frame queued observer dispatched once"), UntouchObservers, 1);
 World->SetAiTraceSink({});
 // Actual child/grandchild target: reduced near-gimbal angle words must not replace the raw frame.
 Native->SetLinearVelocity(FVector::ZeroVector, false); Native->SetAngularVelocityInRadians(FVector::ZeroVector, false);
 const FQuat ParentRotation = ElysiumModelPhysics::SourceRotation(FVector(10,0,0));
 const FQuat NearGimbal = ElysiumModelPhysics::SourceRotation(FVector(89.95,40,60));
 Native->SetBodyTransform(FTransform(ParentRotation, Native->GetUnrealWorldTransform().GetLocation()), ETeleportType::TeleportPhysics);
 const FRotator LocalNative = (ParentRotation.Inverse() * NearGimbal).Rotator();
 FVector ZeroLocal = FVector::ZeroVector; ShadowChild->SetOrigin(ZeroLocal);
 FRotator LocalSource(-LocalNative.Pitch,-LocalNative.Yaw,LocalNative.Roll); ShadowChild->SetAngles(LocalSource);
 FVector LongLocal(100000,0,0); Grandchild->SetOrigin(LongLocal); FRotator ZeroAngles = FRotator::ZeroRotator; Grandchild->SetAngles(ZeroAngles);
 ElysiumModelPhysics::Wake(Component); Tick();
 const FVector ExpectedPoint = Dynamic->GetAbsOrigin() + NearGimbal.RotateVector(LongLocal);
 const auto* GrandShadow = ElysiumModelPhysics::ShadowFor(Grandchild->VPhysicsGetObject());
 TestTrue(TEXT("near-gimbal child publishes reduced roll word"), FMath::Abs(ShadowChild->GetAbsAngles().Z) < .001);
 TestTrue(TEXT("grandchild reads raw parent frame, not reduced Euler rebuild"), Grandchild->GetAbsOrigin().Equals(ExpectedPoint,.1));
 TestTrue(TEXT("actual232 submits raw-frame grandchild point"), GrandShadow && GrandShadow->Target.GetLocation().Equals(ExpectedPoint,.1));
 const FTransform RegularPose = Native->GetUnrealWorldTransform();
 Native->SetBodyTransform(FTransform(NearGimbal, RegularPose.GetLocation()), ETeleportType::TeleportPhysics);
 ElysiumModelPhysics::GetPosition(Object, PoseOrigin, PoseAngles);
 TestTrue(TEXT("object37 near-gimbal converter literal180/alternate yaw"), FMath::IsNearlyEqual(PoseAngles.Pitch,89.95,.001) && FMath::IsNearlyEqual(PoseAngles.Yaw,160.,.001) && PoseAngles.Roll == 180.);
 Native->SetBodyTransform(RegularPose, ETeleportType::TeleportPhysics);
 // The solver, not a synthetic activity flag, decides the final sleeping step.
 auto* SleepMaterial = DuplicateObject<UPhysicalMaterial>(Native->GetSimplePhysicalMaterial(), Component);
 SleepMaterial->SleepLinearVelocityThreshold = 1.f; SleepMaterial->SleepAngularVelocityThreshold = 1.f;
 SleepMaterial->SleepCounterThreshold = 2; Component->SetPhysMaterialOverride(SleepMaterial);
 Native->SetShapeCollisionEnabled(0, ECollisionEnabled::NoCollision); // isolate sleep from moving child contacts
 Native->SetResponseToAllChannels(ECR_Ignore);
 Native->GetPhysicsActor()->GetGameThreadAPI().SetSleepThresholdMultiplier(1.f);
 Native->SetLinearVelocity(FVector(.05,0,0), false); ElysiumModelPhysics::Wake(Component);
 bool bFinalSleep = false; int32 FinalSleepWrites = 0;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 { if (Event.Entity == Dynamic->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=set_abs_origin"))) ++FinalSleepWrites; });
 for (int32 Frame = 0; Frame < 120 && !bFinalSleep; ++Frame)
 {
  const bool bWasAwake = Native->IsInstanceAwake(); const int32 BeforeWrites = FinalSleepWrites; Tick();
  if (bWasAwake && !Native->IsInstanceAwake())
  {
   bFinalSleep = true; TestEqual(TEXT("native final sleeping solve publishes226 once"), FinalSleepWrites, BeforeWrites + 1);
   TestTrue(TEXT("final sleeping logical pose matches native output"), Dynamic->GetAbsOrigin().Equals(Native->GetUnrealWorldTransform().GetLocation(), .001));
  }
 }
 TestTrue(TEXT("actual Chaos reaches automatic sleep"), bFinalSleep);
 const int32 BeforeSleepingFrame = FinalSleepWrites; Tick(); Tick();
 TestEqual(TEXT("sleeping body does not repeat final226"), FinalSleepWrites, BeforeSleepingFrame);
 World->SetAiTraceSink({});
 Native->PutInstanceToSleep();
 // A game-side SkipPhysicsUpdate notification cannot masquerade as native solve output.
 ElysiumModelPhysics::PreparePhysicsFrame(*Fixture.World, .02f, 1.f, true);
 Component->OnUpdateTransform(EUpdateTransformFlags::SkipPhysicsUpdate, ETeleportType::None);
 int32 SleepingWrites = 0;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 { if (Event.Entity == Dynamic->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=set_abs_origin"))) ++SleepingWrites; });
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 0")); Tick(); Tick();
 TestEqual(TEXT("sleeping body has no stale or presentation-produced226"), SleepingWrites, 0);
 World->SetAiTraceSink({});
 // Freeze three actual native actors in wake order. A callback retires/recreates the later one.
 auto* Second = World->FindByName(TEXT("second")); auto* Third = World->FindByName(TEXT("third"));
 auto* SecondComponent = ElysiumModelPhysics::ComponentFor(Second ? Second->VPhysicsGetObject() : nullptr);
 auto* ThirdComponent = ElysiumModelPhysics::ComponentFor(Third ? Third->VPhysicsGetObject() : nullptr);
 if (!SecondComponent || !ThirdComponent) return false;
 FElysiumSavedPhysicsBody RecreatedState;
 if (!ElysiumModelPhysics::Capture(Object, RecreatedState)) return false;
 RecreatedState.FieldName = TEXT("m_pPhysicsObject"); RecreatedState.bAsleep = false;
 SecondComponent->SetEnableGravity(false); ThirdComponent->SetEnableGravity(false);
 ElysiumModelPhysics::Identity(SecondComponent)->SetLinearVelocity(FVector(10,0,0), false, false);
 ElysiumModelPhysics::Identity(ThirdComponent)->SetLinearVelocity(FVector(10,0,0), false, false);
 //2600fbb0 retains existing active positions. Establish real sleep/removal before
 // claiming append order; the earlier native history can leave either partner awake.
 ElysiumModelPhysics::Identity(SecondComponent)->PutInstanceToSleep();
 ElysiumModelPhysics::Identity(ThirdComponent)->PutInstanceToSleep();
 TestFalse(TEXT("second native body asleep before fresh wake order"), ElysiumModelPhysics::IsActive(SecondComponent));
 TestFalse(TEXT("third native body asleep before fresh wake order"), ElysiumModelPhysics::IsActive(ThirdComponent));
 ElysiumModelPhysics::PreparePhysicsFrame(*Fixture.World, 0.f, 1.f, true);
 const auto BeforeOrderedWake = ElysiumModelPhysics::ActiveOutputs(*World);
 TestFalse(TEXT("pending second sleep removed before append"), BeforeOrderedWake.ContainsByPredicate([&](const ElysiumModelPhysics::FNativeOutput& Output) { return ElysiumModelPhysics::GameDataFor(Output,*World) == Second; }));
 TestFalse(TEXT("pending third sleep removed before append"), BeforeOrderedWake.ContainsByPredicate([&](const ElysiumModelPhysics::FNativeOutput& Output) { return ElysiumModelPhysics::GameDataFor(Output,*World) == Third; }));
 ElysiumCommandBus::Console().Execute(TEXT("phys_timescale 1"));
 ElysiumModelPhysics::Wake(SecondComponent); ElysiumModelPhysics::Wake(Component); ElysiumModelPhysics::Wake(ThirdComponent);
 TArray<FString> OrderedOwners; bool bRecreated = false;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 {
  if (Event.Kind != TEXT("retail_site") || !Event.Text.Contains(TEXT("tag=set_abs_origin"))) return;
  if (Event.Entity != Dynamic->Handle && Event.Entity != Second->Handle && Event.Entity != Third->Handle) return;
  OrderedOwners.Add(Event.Name);
  if (Event.Entity == Second->Handle && !bRecreated)
  {
   bRecreated = true; Alias->VPhysicsDestroyObject();
   TestTrue(TEXT("callback restores actual native incarnation"), Dynamic->RestoreSavedPhysics(RecreatedState, Error));
  }
 });
 Tick(); World->SetAiTraceSink({});
 TestTrue(TEXT("first native callback performed alias retirement/recreation"), bRecreated);
 TestEqual(TEXT("retired frozen native incarnation is skipped"), OrderedOwners.Num(), 2);
 if (OrderedOwners.Num() == 2)
 { TestEqual(TEXT("wake append order first"), OrderedOwners[0], FString(TEXT("second"))); TestEqual(TEXT("later surviving native callback"), OrderedOwners[1], FString(TEXT("third"))); }
 const auto CurrentObjects = ElysiumModelPhysics::ActiveOutputs(*World);
 TestTrue(TEXT("new incarnation remains registered"), CurrentObjects.ContainsByPredicate([&](const ElysiumModelPhysics::FNativeOutput& Output) { return ElysiumModelPhysics::GameDataFor(Output,*World) == Dynamic; }));
 int32 RecreatedWrites = 0;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 { if (Event.Entity == Dynamic->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=set_abs_origin"))) ++RecreatedWrites; });
 Tick(); World->SetAiTraceSink({});
 TestEqual(TEXT("new native incarnation publishes on following frame"), RecreatedWrites, 1);
 auto* NewNative = ElysiumModelPhysics::Identity(ElysiumModelPhysics::ComponentFor(Dynamic->VPhysicsGetObject()));
 auto* Solver = NewNative->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>();
 auto* ChildComponent = ElysiumModelPhysics::ComponentFor(ShadowChild->VPhysicsGetObject());
 // A real second native environment must never supply this environment's last swapped entry.
 FTestWorldWrapper OtherFixture;
 if (!OtherFixture.CreateTestWorld(EWorldType::EditorPreview)) { OtherFixture.ForwardErrorMessages(this); return false; }
 auto* OtherOwner = OtherFixture.GetTestWorld()->SpawnActor<AActor>();
 auto* OtherBody = NewObject<UElysiumPhysicsPropComponent>(OtherOwner);
 OtherBody->SetStaticMesh(CastChecked<UStaticMeshComponent>(Component)->GetStaticMesh()); OtherBody->RegisterComponent(); OtherOwner->AddInstanceComponent(OtherBody);
 FElysiumPhysicsSurfaceProps OtherSurfaces;
 OtherSurfaces.ParseSurfaceData(TEXT("other-native-world"), TEXT("default { friction 0.8 elasticity 0.2 density 1000 } metal { friction 0.4 elasticity 0.7 density 7800 }"));
 if (!TestTrue(TEXT("second environment has real awake native producer"), ElysiumModelPhysics::Create(OtherBody,OtherSurfaces,0.f,0.f,false,Error))) return false;
 FString BeforeSleepRoster;
 for (const auto& Output : ElysiumModelPhysics::ActiveOutputs(*World))
  if (auto* Owner = ElysiumModelPhysics::GameDataFor(Output,*World)) BeforeSleepRoster += Owner->TargetName + TEXT(" ");
 AddInfo(FString(TEXT("active roster before pending child removal: ")) + BeforeSleepRoster);
 ElysiumModelPhysics::Identity(ChildComponent)->PutInstanceToSleep();
 int32 SleepingFeedback = 0;
 for (const TCHAR* Scale : { TEXT("0"), TEXT("0.005"), TEXT("-1") })
 {
  ElysiumCommandBus::Console().Execute(FString(TEXT("phys_timescale ")) + Scale);
  const double BeforeTime = Solver->GetSolverTime(); const FVector BeforePose = NewNative->GetUnrealWorldTransform().GetLocation();
  OrderedOwners.Reset();
  World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
  {
   if (Event.Entity == ShadowChild->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=shadow_feedback"))) ++SleepingFeedback;
   if (Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=set_abs_origin"))
    && (Event.Entity == Dynamic->Handle || Event.Entity == Second->Handle || Event.Entity == Third->Handle)) OrderedOwners.Add(Event.Name);
  });
  Tick(); World->SetAiTraceSink({});
  TestEqual(TEXT("rejected native dt retains three awake226 callbacks"), OrderedOwners.Num(), 3);
  if (OrderedOwners.Num() == 3)
  { TestEqual(TEXT("zero dt last incarnation replaces removed child"), OrderedOwners[0], FString(TEXT("dynamic"))); TestEqual(TEXT("zero dt next surviving owner"), OrderedOwners[1], FString(TEXT("second"))); TestEqual(TEXT("zero dt final surviving owner"), OrderedOwners[2], FString(TEXT("third"))); }
  TestEqual(TEXT("rejected native dt does not advance actual scene"), Solver->GetSolverTime(), BeforeTime);
  TestTrue(TEXT("rejected native dt preserves native body pose"), NewNative->GetUnrealWorldTransform().GetLocation().Equals(BeforePose,.001));
 }
 TestEqual(TEXT("sleeping shadow excluded from229 even with owner226"), SleepingFeedback, 0);
 ElysiumModelPhysics::Wake(ChildComponent);
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 { if (Event.Entity == ShadowChild->Handle && Event.Kind == TEXT("retail_site") && Event.Text.Contains(TEXT("tag=shadow_feedback"))) ++SleepingFeedback; });
 Tick(); World->SetAiTraceSink({});
 TestEqual(TEXT("real native shadow wake admits229 on rejected dt"), SleepingFeedback, 1);
 TestTrue(TEXT("other environment native producer remains active"), ElysiumModelPhysics::IsActive(OtherBody));
 TestTrue(TEXT("auxiliary native world cleanup succeeds"), OtherFixture.DestroyTestWorld(false));
 TestTrue(TEXT("main production session remains active after auxiliary cleanup"), Map->IsRuntimeActive() && Map->GetEntityWorld() == World);
 TestTrue(TEXT("main native incarnation survives auxiliary cleanup"), ElysiumModelPhysics::IsActive(ElysiumModelPhysics::ComponentFor(Dynamic->VPhysicsGetObject())));
 TestEqual(TEXT("auxiliary cleanup preserves main physics cvar"), ElysiumCommandBus::Console().GetCvar(TEXT("phys_timescale")), FString(TEXT("-1")));
 Dynamic->VPhysicsDestroyObject();
 Alias->VPhysicsDestroyObject();
 TestNull(TEXT("alias225 retires native actor"), ElysiumModelPhysics::ComponentFor(Object));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNativeTouchDispatchTest, "Elysium.Substrate.ModelPhysics.NativeTouchDispatch",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumNativeTouchDispatchTest::RunTest(const FString&)
{
 FPlayerWorldFixture Fixture;
 if (!Fixture.CreateWorld(*this)) return false;
 Fixture.SpawnPlayerControllerAndPawn(FVector(-1000,0,1000)); Fixture.PlayerController->Possess(Fixture.Pawn);
 auto* Map = Fixture.SpawnMapActorDeferred(); Map->bStageOnly = true; Map->bStageWithoutCatalogue = true;
 Map->FinishSpawning(FTransform::Identity);
 auto Tick = [&]() { return Fixture.TestWorld.TickTestWorld(.02f); };
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("production activation barrier is reached"), Map->IsRuntimeActive())) return false;
 FElysiumEntityDefs Defs; Defs.MapName = TEXT("native_touch_dispatch");
 FElysiumEntityDef WorldDef; WorldDef.Classname = TEXT("worldspawn"); Defs.Defs.Add(WorldDef);
 auto DynamicDef = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("touch_body"));
 DynamicDef.Origin = FVector(3000,4000,3000); Defs.Defs.Add(DynamicDef);
 for (const TCHAR* Name : {TEXT("old_parent"),TEXT("new_parent")})
 { FElysiumEntityDef Point; Point.Classname = TEXT("info_target"); Point.TargetName = Name; Defs.Defs.Add(Point); }
 FElysiumEntityDef Trigger; Trigger.Classname = TEXT("trigger_multiple"); Trigger.TargetName = TEXT("touch_trigger");
 Trigger.Model = 1; Trigger.Origin = DynamicDef.Origin; Trigger.Keys.Add(TEXT("model"),TEXT("*1"));
 Trigger.Keys.Add(TEXT("spawnflags"),TEXT("8")); Trigger.Keys.Add(TEXT("wait"),TEXT("0.05"));
 FElysiumConvexHull Hull;
 for (float X : {-150.f,150.f}) for (float Y : {-150.f,150.f}) for (float Z : {-150.f,150.f}) Hull.Vertices.Add(FVector(X,Y,Z));
 Trigger.Hulls.Add(Hull); Defs.Defs.Add(Trigger);
 FElysiumStageSeat Seat; Seat.FeetCm = FVector(-1000,0,1000); FString Error;
 if (!Map->RebuildStageWorld(MoveTemp(Defs), {}, Seat, Error)) { AddError(Error); return false; }
 for (int32 Frame = 0; Frame < 20 && !Map->IsRuntimeActive(); ++Frame) Tick();
 if (!TestTrue(TEXT("actual prop/trigger stage becomes active"), Map->IsRuntimeActive())) return false;
 auto* World = Map->GetEntityWorld(); auto* Dynamic = World->FindByName(TEXT("touch_body"));
 auto* PhysicalTrigger = static_cast<FElysiumTriggerBase*>(World->FindByName(TEXT("touch_trigger")));
 auto* OldParent = World->FindByName(TEXT("old_parent")); auto* NewParent = World->FindByName(TEXT("new_parent"));
 if (!Dynamic || !PhysicalTrigger || !OldParent || !NewParent) return false;
 auto* Component = ElysiumModelPhysics::ComponentFor(Dynamic->VPhysicsGetObject());
 if (!TestNotNull(TEXT("actual prop_physics creation produces native body"),Component)) return false;
 TestEqual(TEXT("actual prop producer supplies move7"),Dynamic->GetMoveType(),7);
 TestTrue(TEXT("actual trigger Spawn installs +1ec MultiTouch"),PhysicalTrigger->TouchCallback != nullptr);
 TestEqual(TEXT("actual InitTrigger supplies BSP solid1"),PhysicalTrigger->GetSolid(),1);
 TestEqual(TEXT("actual InitTrigger supplies NOT_SOLID and TRIGGER flags"),PhysicalTrigger->GetSolidFlags() & 0xcu,0xcu);
 Component->SetEnableGravity(false);
 ElysiumModelPhysics::Wake(Component); Tick();
 if (!TestTrue(TEXT("real native geometry creates the physical links"), Dynamic->IsCurrentlyTouching() && PhysicalTrigger->IsCurrentlyTouching())) return false;
 if (!TestTrue(TEXT("real move7 native touch reaches installed MultiTouch"),PhysicalTrigger->LastTriggerTime > -1.e15)) return false;
 const float PriorFire = static_cast<float>(PhysicalTrigger->LastTriggerTime);
 for (int32 Frame = 0; Frame < 4; ++Frame) { ElysiumModelPhysics::Wake(Component); Tick(); }
 TestTrue(TEXT("repeated native175 re-arms the literal wait gate"),PhysicalTrigger->LastTriggerTime > PriorFire);
 World->AcceptInput(PhysicalTrigger->Handle,FName(TEXT("Disable")),FElysiumVariant::Void(),{},{});
 TestEqual(TEXT("live Disable removes only TRIGGER8 from initialized flags"),PhysicalTrigger->GetSolidFlags() & 0xcu,4u);
 TestFalse(TEXT("live Disable retires the physical trigger pair"),PhysicalTrigger->IsCurrentlyTouching());
 World->AcceptInput(PhysicalTrigger->Handle,FName(TEXT("Enable")),FElysiumVariant::Void(),{},{});
 TestEqual(TEXT("live Enable restores TRIGGER8 alongside NOT_SOLID4"),PhysicalTrigger->GetSolidFlags() & 0xcu,0xcu);
 ElysiumModelPhysics::Wake(Component); Tick();
 if (!TestTrue(TEXT("enabled actual native geometry recreates the pair"),Dynamic->IsCurrentlyTouching() && PhysicalTrigger->IsCurrentlyTouching())) return false;
 const int32 BeginsBeforeReciprocal = World->TouchBegins();
 World->RouteEntityTouch(Dynamic->Handle,PhysicalTrigger->Handle,true,true);
 TestEqual(TEXT("reciprocal physical encounter shares original pair key"),World->TouchBegins(),BeginsBeforeReciprocal);
 const int32 EndsBefore = World->TouchEnds();
 World->RouteEntityTouch(Dynamic->Handle,PhysicalTrigger->Handle,false,true);
 World->RouteEntityTouch(PhysicalTrigger->Handle,Dynamic->Handle,false,true);
 TestEqual(TEXT("reciprocal pair retires with one end"),World->TouchEnds(),EndsBefore+1);
 TestFalse(TEXT("retirement removes both physical link lists"),Dynamic->IsCurrentlyTouching() || PhysicalTrigger->IsCurrentlyTouching());
 ElysiumModelPhysics::Wake(Component); Tick();
 if (!TestTrue(TEXT("fresh real native reentry creates both links"),Dynamic->IsCurrentlyTouching() && PhysicalTrigger->IsCurrentlyTouching())) return false;
 Dynamic->SetParent(OldParent,0);
 auto Observe = [](FElysiumEntity& Receiver,FElysiumEntity* Other)
 { Receiver.World->EmitRetailSite(Receiver,TEXT("touch_contract"),TEXT("CBaseEntity::Touch"),0x100a4af0u,TEXT("callback"),Other?Other->TargetName:TEXT("null")); };
 OldParent->TouchCallback = Observe; NewParent->TouchCallback = Observe;
 Dynamic->TouchCallback = [](FElysiumEntity& Receiver,FElysiumEntity* Other)
 {
  Receiver.World->EmitRetailSite(Receiver,TEXT("touch_contract"),TEXT("CBaseEntity::Touch"),0x100a4af0u,TEXT("callback"),Other?Other->TargetName:TEXT("null"));
  Receiver.SetParent(Receiver.World->FindByName(TEXT("new_parent")),0);
  Receiver.UnlinkFromMoveParent(); // actual producer leaves authored+254 distinct from live+25c
  Receiver.TouchCallback = nullptr;
 };
 TArray<FString> CallbackOrder;
 World->SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
 { if (Event.Kind==TEXT("retail_site") && Event.Text.Contains(TEXT("tag=touch_contract"))) { CallbackOrder.Add(Event.Name); TestTrue(TEXT("native175 forwards original trigger receiver"),Event.Text.Contains(TEXT("touch_trigger"))); } });
 ElysiumModelPhysics::Wake(Component); Tick(); World->SetAiTraceSink({});
 TestEqual(TEXT("native175 invokes installed callback then live parent exactly once"),CallbackOrder.Num(),2);
 if (CallbackOrder.Num()==2) { TestEqual(TEXT("installed callback first"),CallbackOrder[0],FString(TEXT("touch_body"))); TestEqual(TEXT("new authored parent follows, not old or absent move parent"),CallbackOrder[1],FString(TEXT("new_parent"))); }
 TestTrue(TEXT("callback's actual unlink preserves authored parent while clearing move parent"),Dynamic->ParentHandle==NewParent->Handle && !Dynamic->MoveParent.IsSet());
 Dynamic->SetParent(nullptr,0);
 const FElysiumEntityHandle TriggerHandle = PhysicalTrigger->Handle;
 Dynamic->TouchCallback = [](FElysiumEntity& Receiver,FElysiumEntity* Other) { if (Other) Other->Kill(); Receiver.TouchCallback=nullptr; };
 ElysiumModelPhysics::Wake(Component); Tick();
 TestNull(TEXT("actual native175 callback retires the original other handle"),World->Resolve(TriggerHandle));
 TestFalse(TEXT("retirement releases the surviving native receiver's pair"),Dynamic->IsCurrentlyTouching());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBaseVelocityTest, "Elysium.Substrate.ModelPhysics.BaseVelocity",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumBaseVelocityTest::RunTest(const FString&)
{
	FPlayerWorldFixture VelocityFixture;
	if (!VelocityFixture.CreateWorld(*this)) return false;
	auto* VelocityMap = VelocityFixture.SpawnMapActorDeferred();
	auto* VelocityPlaced = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* VelocitySkins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	TSet<FSoftObjectPath> VelocityPaths; FString VelocityError;
	if (!TestTrue(TEXT("production velocity model catalogue"), FElysiumPreparedPropModels::GatherPaths(VelocityPlaced, VelocitySkins,
		{ ElysiumModelPhysicsTest::BodyId }, VelocityPaths, VelocityError))) return false;
	TArray<UObject*> VelocityAssets;
	for (const auto& AssetPath : VelocityPaths) VelocityAssets.Add(AssetPath.TryLoad());
	auto VelocityPrepared = FElysiumPreparedPropModels::Create(VelocityMap, 1, VelocityPlaced, VelocitySkins,
		{ ElysiumModelPhysicsTest::BodyId }, VelocityAssets, VelocityError);
	if (!TestTrue(TEXT("production velocity model preparation"), VelocityPrepared.IsValid())) return false;
	ON_SCOPE_EXIT { ElysiumPreparedProps::Release(VelocityMap); };
	FElysiumWorldServices VelocityServices; VelocityServices.Embodiment = VelocityMap;
	FElysiumEntityWorld VelocityWorld(VelocityMap, nullptr, VelocityServices);
	FElysiumEntityDefs VelocityDefs; VelocityDefs.MapName = TEXT("base_velocity_native"); VelocityWorld.Load(MoveTemp(VelocityDefs));
	auto VelocityDef = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("velocity_body"));
	VelocityDef.Origin = FVector(5000, 4000, 3000);
	FElysiumEntity* VelocityEntity = VelocityWorld.Resolve(VelocityWorld.SpawnRuntimeEntity(MoveTemp(VelocityDef)));
	if (!TestNotNull(TEXT("registered prop_physics receiver"), VelocityEntity)) return false;
	auto* VelocityProp = static_cast<FElysiumPhysProp*>(VelocityEntity); //FElysiumPhysProp inherits base199 directly.
	auto* VelocityNative = ElysiumModelPhysics::Identity(VelocityProp->Visual);
	if (!TestNotNull(TEXT("real Spawn selected .phy dynamic native body"), VelocityNative)) return false;
	TestTrue(TEXT("real native simulation is admitted"), VelocityProp->Visual->IsSimulatingPhysics());
	VelocityProp->Visual->SetEnableGravity(false);
	VelocityProp->Visual->SetLinearDamping(0.f); VelocityProp->Visual->SetAngularDamping(0.f);
	VelocityNative->SetBodyTransform(FTransform(FRotator(0, 90, 0), FVector(5000, 4000, 3000)), ETeleportType::TeleportPhysics);
	VelocityEntity->Velocity = FVector(700, 800, 900); VelocityEntity->AngularVelocity = FVector(9, 10, 11);
	VelocityNative->SetLinearVelocity(FVector(101, -53, 17), false);
	VelocityNative->SetAngularVelocityInRadians(FVector(-.5, .25, .75), false);
	const FVector VelocityBefore = VelocityNative->GetUnrealWorldTransform().GetLocation();
	if (!TestTrue(TEXT("actual native frame advances"), VelocityFixture.TestWorld.TickTestWorld(.02f))) return false;
	const FTransform VelocitySolved = VelocityNative->GetUnrealWorldTransform(false, true);
	const FVector VelocityLinear = VelocityNative->GetUnrealWorldVelocity();
	const FVector VelocityLocalAngular = VelocitySolved.GetRotation().UnrotateVector(VelocityNative->GetUnrealWorldAngularVelocityInRadians());
	FVector VelocityQueryLinear(-1), VelocityQueryAngular(-1);
	VelocityEntity->GetVelocity(&VelocityQueryLinear, &VelocityQueryAngular); //real base-slot virtual dispatch
	AddInfo(FString::Printf(TEXT("BASE_VELOCITY_PROOF object=%d moved_cm=%.9g native_cm_s=%s query_cm_s=%s local_rad_s=%s query_qangle_deg_s=%s"),
		VelocityEntity->VPhysicsGetObject() != nullptr, (VelocitySolved.GetLocation()-VelocityBefore).Size(), *VelocityLinear.ToString(),
		*VelocityQueryLinear.ToString(), *VelocityLocalAngular.ToString(), *VelocityQueryAngular.ToString()));
	TestTrue(TEXT("production fixture truly moved in native solve"), (VelocitySolved.GetLocation()-VelocityBefore).Size() > .5);
	TestTrue(TEXT("virtual base query reads authoritative native world cm/s"), VelocityQueryLinear.Equals(VelocityLinear, .0001));
	TestFalse(TEXT("native query differs from deliberately distinct logical velocity"), VelocityQueryLinear.Equals(VelocityEntity->Velocity, .0001));
	TestTrue(TEXT("rotated base query returns body-local pitch yaw roll degrees/s"), VelocityQueryAngular.Equals(
		FVector(VelocityLocalAngular.Y, -VelocityLocalAngular.Z, -VelocityLocalAngular.X) * 57.29578f, .0001));
	TestFalse(TEXT("native angular query differs from local logical angular fields"), VelocityQueryAngular.Equals(VelocityEntity->AngularVelocity, .0001));
	// Fixed rotated three-axis stimulus, after proving actual production solve above. The
	// native inertia principal frame is deliberately different from the body query frame.
	FTransform VelocityMassFrame = VelocityNative->GetMassSpaceLocal();
	VelocityMassFrame.SetRotation(FRotator(17, 31, 23).Quaternion());
	VelocityNative->SetMassSpaceLocal(VelocityMassFrame);
	VelocityNative->SetBodyTransform(FTransform(FRotator(0, 90, 0), FVector(5000, 4000, 3000)), ETeleportType::TeleportPhysics);
	VelocityNative->SetLinearVelocity(FVector(101, -53, 17), false);
	VelocityNative->SetAngularVelocityInRadians(FVector(-.5, .25, .75), false);
	const FVector VelocityExpectedLinear(101, -53, 17), VelocityExpectedAngular(28.64789, -42.971835, -14.323945);
	VelocityEntity->GetVelocity(&VelocityQueryLinear, &VelocityQueryAngular);
	TestTrue(TEXT("literal rotated-body pitch yaw roll independent of principal inertia axes"), VelocityQueryAngular.Equals(VelocityExpectedAngular, .0001));
	TestTrue(TEXT("literal native world cm/s has no extra inches scaling or reflection"), VelocityQueryLinear.Equals(VelocityExpectedLinear, .0001));
	const FVector VelocitySentinel(98765, -43210, 11223);
	VelocityQueryLinear = VelocitySentinel; VelocityQueryAngular = VelocitySentinel;
	VelocityEntity->GetVelocity(&VelocityQueryLinear, nullptr);
	TestTrue(TEXT("native linear-only call"), VelocityQueryLinear.Equals(VelocityExpectedLinear, .0001));
	TestEqual(TEXT("native linear-only preserves other storage"), VelocityQueryAngular, VelocitySentinel);
	VelocityQueryLinear = VelocitySentinel;
	VelocityEntity->GetVelocity(nullptr, &VelocityQueryAngular);
	TestTrue(TEXT("native angular-only call"), VelocityQueryAngular.Equals(VelocityExpectedAngular, .0001));
	TestEqual(TEXT("native angular-only preserves other storage"), VelocityQueryLinear, VelocitySentinel);
	VelocityEntity->GetVelocity(nullptr, nullptr);
	TestEqual(TEXT("native both-null leaves linear storage"), VelocityQueryLinear, VelocitySentinel);
	VelocityEntity->GetVelocity(&VelocityQueryAngular, &VelocityQueryAngular);
	TestTrue(TEXT("shared output storage receives angular last"), VelocityQueryAngular.Equals(VelocityExpectedAngular, .0001));
	FElysiumEntityDef VelocityFallbackDef; VelocityFallbackDef.Classname = TEXT("info_target"); VelocityFallbackDef.TargetName = TEXT("velocity_fallback");
	FElysiumEntity* VelocityFallback = VelocityWorld.Resolve(VelocityWorld.SpawnRuntimeEntity(VelocityFallbackDef));
	VelocityFallbackDef.TargetName = TEXT("velocity_parent");
	FElysiumEntity* VelocityParent = VelocityWorld.Resolve(VelocityWorld.SpawnRuntimeEntity(MoveTemp(VelocityFallbackDef)));
	if (!VelocityFallback || !VelocityParent) return false;
	VelocityParent->SetAngles(FRotator(19, 73, 11));
	VelocityParent->Velocity = FVector(200, 300, 400);
	VelocityFallback->SetParent(VelocityParent, 0);
	VelocityFallback->Velocity = FVector(7, -11, 13); VelocityFallback->AngularVelocity = FVector(17, -19, 23);
	VelocityFallback->EFlags |= 0x3000u;
	const uint32 VelocityDirtyBefore = VelocityFallback->EFlags;
	const FVector VelocityFallbackLinear = VelocityFallback->Velocity, VelocityFallbackAngular = VelocityFallback->AngularVelocity;
	VelocityFallback->GetVelocity(&VelocityQueryLinear, &VelocityQueryAngular);
	TestEqual(TEXT("bodyless parented query copies local velocity without absolute refresh"), VelocityQueryLinear, VelocityFallbackLinear);
	TestEqual(TEXT("bodyless query copies QAngle fields verbatim"), VelocityQueryAngular, VelocityFallbackAngular);
	TestEqual(TEXT("fallback query preserves transform dirty flags"), VelocityFallback->EFlags, VelocityDirtyBefore);
	VelocityQueryLinear = VelocitySentinel; VelocityQueryAngular = VelocitySentinel;
	VelocityFallback->GetVelocity(&VelocityQueryLinear, nullptr);
	TestEqual(TEXT("fallback linear-only"), VelocityQueryLinear, VelocityFallbackLinear);
	TestEqual(TEXT("fallback linear-only preserves other storage"), VelocityQueryAngular, VelocitySentinel);
	VelocityQueryLinear = VelocitySentinel;
	VelocityFallback->GetVelocity(nullptr, &VelocityQueryAngular);
	TestEqual(TEXT("fallback angular-only"), VelocityQueryAngular, VelocityFallbackAngular);
	TestEqual(TEXT("fallback angular-only preserves other storage"), VelocityQueryLinear, VelocitySentinel);
	VelocityFallback->GetVelocity(nullptr, nullptr);
	VelocityFallback->GetVelocity(&VelocityQueryAngular, &VelocityQueryAngular);
	TestEqual(TEXT("fallback shared output angular wins"), VelocityQueryAngular, VelocityFallbackAngular);
	void* VelocityObject = VelocityEntity->VPhysicsGetObject();
	VelocityEntity->VPhysicsSetObject(nullptr); //100a6490 reference-only clear, retains owned native producer.
	VelocityEntity->GetVelocity(&VelocityQueryLinear, &VelocityQueryAngular);
	TestEqual(TEXT("clear current object switches linear to logical field"), VelocityQueryLinear, VelocityEntity->Velocity);
	TestEqual(TEXT("clear current object switches angular to logical field"), VelocityQueryAngular, VelocityEntity->AngularVelocity);
	TestNotNull(TEXT("reference-only clear preserves native body"), ElysiumModelPhysics::ComponentFor(VelocityObject));
	VelocityEntity->VPhysicsSetObject(VelocityObject);
	auto VelocitySecondDef = ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"), TEXT("velocity_second"));
	VelocitySecondDef.Origin = FVector(-5000, -4000, 3000);
	auto* VelocitySecond = static_cast<FElysiumPhysProp*>(VelocityWorld.Resolve(VelocityWorld.SpawnRuntimeEntity(MoveTemp(VelocitySecondDef))));
	if (!VelocitySecond || !ElysiumModelPhysics::Identity(VelocitySecond->Visual)) return false;
	auto* VelocitySecondNative = ElysiumModelPhysics::Identity(VelocitySecond->Visual);
	VelocitySecondNative->SetLinearVelocity(FVector(-31, 47, -59), false);
	VelocityEntity->VPhysicsSwapObject(VelocitySecond->VPhysicsGetObject());
	VelocityEntity->GetVelocity(&VelocityQueryLinear, nullptr);
	TestTrue(TEXT("current-object replacement reads second authoritative body"), VelocityQueryLinear.Equals(FVector(-31, 47, -59), .0001));
	TestNotNull(TEXT("replacement leaves first owned body alive"), ElysiumModelPhysics::ComponentFor(VelocityObject));
	VelocityEntity->VPhysicsSwapObject(VelocityObject);
	VelocityFallback->VPhysicsSetObject(VelocityObject); //independent registered base alias.
	// Serialization eligibility is not a gameplay input. Retire only recipe association;
	// native reference/identity continue to serve real queries until actual225 removal.
	ElysiumModelPhysics::RetireRecipe(VelocityProp->Visual);
	TestNull(TEXT("velocity fixture has no save recipe"), ElysiumModelPhysics::RecipeFor(VelocityObject));
	VelocityFallback->GetVelocity(&VelocityQueryLinear, &VelocityQueryAngular);
	TestTrue(TEXT("base alias query ignores absent save recipe"), VelocityQueryLinear.Equals(VelocityExpectedLinear, .0001));
	bool bVelocityRetirementObserved = false;
	VelocityWorld.SetAiTraceSink([&](const FElysiumAiTraceEvent& VelocityEvent)
	{
		if (VelocityEvent.Kind != TEXT("physics_retire") || VelocityEvent.Entity != VelocityFallback->Handle) return;
		bVelocityRetirementObserved = true;
		TestNotNull(TEXT("retirement observer runs before native release"), ElysiumModelPhysics::Identity(VelocityProp->Visual));
		TestNull(TEXT("retirement clears alias before its gameplay query"), VelocityFallback->VPhysicsGetObject());
		VelocityFallback->GetVelocity(&VelocityQueryLinear, &VelocityQueryAngular);
		TestEqual(TEXT("pre-release alias query uses local linear fallback"), VelocityQueryLinear, VelocityFallbackLinear);
		TestEqual(TEXT("pre-release alias query uses local angular fallback"), VelocityQueryAngular, VelocityFallbackAngular);
	});
	VelocityEntity->VPhysicsDestroyObject();
	VelocityWorld.SetAiTraceSink({});
	TestTrue(TEXT("actual225 exposes alias retirement before release"), bVelocityRetirementObserved);
	TestNull(TEXT("retired current object field cleared"), VelocityEntity->VPhysicsGetObject());
	TestNull(TEXT("retired native actor is undiscoverable"), ElysiumModelPhysics::ComponentFor(VelocityObject));
	VelocityQueryLinear = VelocitySentinel;
	TestFalse(TEXT("retired native semantic query fails without stale dereference"), ElysiumModelPhysics::GetVelocity(VelocityObject, &VelocityQueryLinear, nullptr));
	TestEqual(TEXT("failed native lookup leaves optional storage untouched"), VelocityQueryLinear, VelocitySentinel);
	VelocityEntity->GetVelocity(&VelocityQueryLinear, &VelocityQueryAngular);
	TestEqual(TEXT("destroyed owner query uses local linear fallback"), VelocityQueryLinear, VelocityEntity->Velocity);
	TestNotNull(TEXT("retiring first body preserves replacement body's own identity"), VelocitySecond->VPhysicsGetObject());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelShadowCallerTest, "Elysium.Substrate.ModelPhysics.ModelShadowCaller",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelShadowCallerTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred();
	auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	const TArray<FString> Ids = {
		TEXT("vtmb:model:scenery/structural/doorknoboffice/doorknoboffice"),
		TEXT("vtmb:model:scenery/vehicles/lightningbird/trunklid"),
		TEXT("vtmb:model:scenery/structural/ceiling_fan/ceiling_fan_blades"),
		TEXT("vtmb:model:scenery/structural/doorknoba/doorknoba"),
		TEXT("vtmb:model:scenery/structural/doorknoba/drknobantique"),
		TEXT("vtmb:model:scenery/structural/doorknoba/drknobantiquel") };
	TSet<FSoftObjectPath> Paths; FString Error;
	if (!FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error)) { AddError(Error); return false; }
	TArray<UObject*> Assets; for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
	auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
	if (!Prepared.IsValid()) { AddError(Error); return false; }
	FElysiumWorldServices Services; Services.Embodiment = Map;
	FElysiumEntityDefs Defs; Defs.MapName = TEXT("model_shadow_native");
	FElysiumEntityDef Parent; Parent.Classname = TEXT("func_door"); Parent.TargetName = TEXT("real_parent");
	Parent.Model = 1; Parent.Origin = FVector(2500,4000,5000); Parent.Keys.Add(TEXT("model"), TEXT("*1"));
	FElysiumConvexHull ParentHull;
	for (float X : {0.f,120.f}) for (float Y : {-10.f,10.f}) for (float Z : {-10.f,10.f}) ParentHull.Vertices.Add(FVector(X,Y,Z));
	Parent.Hulls.Add(ParentHull); Defs.Defs.Add(Parent);
	auto MakeDef = [&](int32 ModelIndex, const TCHAR* Name, bool bParent, const TCHAR* Solid)
	{
		FElysiumEntityDef Result; Result.Classname = TEXT("prop_dynamic"); Result.TargetName = Name;
		Result.ModelMesh = FString(TEXT("models/")) + Ids[ModelIndex].RightChop(11) + TEXT(".mdl");
		Result.Origin = FVector(3000 + ModelIndex * 2000, 4000, 5000);
		if (!bParent) Result.Origin = FVector(17000 + Defs.Defs.Num() * 1000, 12000, 5000);
		Result.Keys.Add(TEXT("model"), Result.ModelMesh); Result.Keys.Add(TEXT("solid"), Solid);
		Result.Keys.Add(TEXT("angles"), TEXT("17 31 23"));
		if (bParent) Result.Keys.Add(TEXT("parentname"), TEXT("real_parent"));
		return Result;
	};
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
		Defs.Defs.Add(MakeDef(Index, *FString::Printf(TEXT("child%d"), Index), true, TEXT("6")));
	Defs.Defs.Add(MakeDef(0, TEXT("static_control"), false, TEXT("6")));
	Defs.Defs.Add(MakeDef(0, TEXT("bbox_control"), false, TEXT("2")));
	Defs.Defs.Add(MakeDef(0, TEXT("prop_alias"), false, TEXT("0")));
	FElysiumEntityDef Point; Point.Classname = TEXT("info_target"); Point.TargetName = TEXT("base_alias"); Defs.Defs.Add(Point);
	FElysiumEntityDefs DestinationDefs = Defs;
	FElysiumEntityDefs TravelDefs = Defs;
	FElysiumEntityWorld World(Map, nullptr, Services); World.Load(MoveTemp(Defs));
	auto ComponentShapeFlags = [&](UPrimitiveComponent* PhysicsComponent)
	{
		FIntPoint Flags(0,0);
		if (auto* Native = ElysiumModelPhysics::Identity(PhysicsComponent))
			FPhysicsCommand::ExecuteRead(Native->GetPhysicsActor(), [&](const FPhysicsActorHandle&)
			{
				TArray<FPhysicsShapeHandle> Shapes; Native->GetAllShapes_AssumesLocked(Shapes);
				for (const auto& Shape : Shapes) { Flags.X += FPhysicsInterface::IsQueryShape(Shape); Flags.Y += FPhysicsInterface::IsSimulationShape(Shape); }
			});
		return Flags;
	};
	auto ShapeFlags = [&](void* Object) { return ComponentShapeFlags(ElysiumModelPhysics::ComponentFor(Object)); };
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		auto* Child = static_cast<FElysiumProp*>(World.FindByName(*FString::Printf(TEXT("child%d"), Index)));
		if (!Child) return false;
		void* Object = Child->VPhysicsGetObject();
		if (Index >= 3)
		{
			TestNull(TEXT("reached studio3 genuine no-binary returns null"), Object);
			TestEqual(TEXT("genuine no-binary list exact count0"), Child->VPhysicsGetObjectList(nullptr, 1), 0);
			TestFalse(TEXT("null studio3 has no shadow membership"), World.PhysicsShadows().Contains(Child->Handle));
			Child->ScriptHide(); Child->ScriptUnhide();
			TestTrue(TEXT("no-binary remains alive after hide/unhide"), World.Resolve(Child->Handle) == Child);
		}
		else
		{
			if (!TestNotNull(TEXT("reached selected dynamic creator assigned native object"), Object)) return false;
			auto* Component = ElysiumModelPhysics::ComponentFor(Object); auto* Native = ElysiumModelPhysics::Identity(Component);
			const auto* Inputs = UElysiumModelPhysicsInputs::Find(Prepared->StaticMesh(Ids[Index], Error));
			const auto* Recipe = ElysiumModelPhysics::RecipeFor(Object); const auto* Shadow = ElysiumModelPhysics::ShadowFor(Object);
			if (!Native || !Inputs || !Recipe || !Shadow) return false;
			TestTrue(TEXT("actual body uses selected cook independently of logical art"), Native->GetBodySetup() == Inputs->SelectedBody && !Component->IsVisible());
			TestFalse(TEXT("selected model shadow recipe is dynamic"), Recipe->bStatic);
			TestEqual(TEXT("neutral dynamic mass parser retains authored mass"), Recipe->MassKg, Inputs->MassKg);
			TestEqual(TEXT("dynamic parser retains authored inertia"), Recipe->InertiaScale, Inputs->InertiaScale);
			TestTrue(TEXT("native selected geometry query and simulation shapes"), ShapeFlags(Object) == FIntPoint(1,1));
			TestTrue(TEXT("100a6150 installed no-move no-rotate controller"), Shadow->bInstalled && !Shadow->bAllowMovement && !Shadow->bAllowRotation);
			TestEqual(TEXT("controller original authored mass"), Shadow->SavedMassKg, Inputs->MassKg);
			TestEqual(TEXT("linear max speed converts inches to centimeters"), Shadow->LinearDesiredLimitCm, 25400.f);
			TestTrue(TEXT("native game data and owner registry"), ElysiumModelPhysics::GameDataForObject(Object) == Child && World.PhysicsShadows().Contains(Child->Handle));
			const FTransform Initial(ElysiumModelPhysics::SourceRotation(Child->GetAbsAngles()), Child->GetAbsOrigin());
			TestTrue(TEXT("absolute native pose precedes initial target"), Native->GetUnrealWorldTransform().Equals(Initial, .001) && Shadow->Target.Equals(Initial, .001) && Shadow->RemainingSeconds == 0.f);
			if (Index == 1)
			{
				bool bSkeletalArt = false; TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents(Map);
				for (const auto* Art : SkeletalComponents)
					if (Art->GetSkeletalMeshAsset() == Prepared->SkeletalMesh(Ids[Index], Error) && Art->IsVisible()) bSkeletalArt = true;
				TestTrue(TEXT("authentic TrunkLid selected carrier also has skeletal art"), bSkeletalArt);
			}
			AddInfo(FString::Printf(TEXT("model-shadow native: id=%s selected=%p actor=%p mass=%g originalmass=%g query=1 sim=1"), *Ids[Index], Inputs->SelectedBody.Get(), Object, Recipe->MassKg, Shadow->SavedMassKg));
		}
		TInlineComponentArray<UStaticMeshComponent*> ArtComponents(Map);
		for (auto* Art : ArtComponents)
			if (Art->GetStaticMesh() == Prepared->StaticMesh(Ids[Index], Error) && Art->GetComponentLocation().Equals(Child->GetAbsOrigin(), .01)
				&& Art != ElysiumModelPhysics::ComponentFor(Object))
			{
				TestTrue(TEXT("logical static/skeletal proxy art has no active query/simulation geometry"), ComponentShapeFlags(Art) == FIntPoint(0,0));
#if WITH_EDITOR
				Art->PreStaticMeshCompilation(); Art->PostStaticMeshCompilation();
#endif
				Art->RecreatePhysicsState();
				void* ArtActor = ElysiumModelPhysics::ActorIdentity(Art);
				TestTrue(TEXT("actual art callbacks preserve zero query/simulation flags and no gameplay association"), ComponentShapeFlags(Art) == FIntPoint(0,0)
					&& (!ArtActor || (ArtActor != Object && !ElysiumModelPhysics::GameDataForObject(ArtActor) && !ElysiumModelPhysics::RecipeFor(ArtActor))));
			}
	}
	auto* Child = World.FindByName(TEXT("child0")); auto* Removed = World.FindByName(TEXT("child2"));
	auto* BaseAlias = World.FindByName(TEXT("base_alias")); auto* PropAlias = World.FindByName(TEXT("prop_alias"));
	void* Object = Child->VPhysicsGetObject(); auto* Component = ElysiumModelPhysics::ComponentFor(Object); auto* Native = ElysiumModelPhysics::Identity(Component);
	const FTransform PriorPose = Native->GetUnrealWorldTransform();
	Child->SetRuntimeOrigin(Child->GetAbsOrigin() + FVector(100,50,25));
	TestTrue(TEXT("logical transform does not teleport selected solved body"), Native->GetUnrealWorldTransform().Equals(PriorPose, .001));
	World.Activate(0.);
	Child->UpdatePhysicsShadowToCurrentPosition(.02f); //actual232 target producer, not a logical-transform teleport.
	World.BeginNativePhysicsFrame(.02, .02);
	const auto* Submitted = ElysiumModelPhysics::ShadowFor(Object);
	TestTrue(TEXT("real transaction submits logical absolute target independently"), Submitted && Submitted->Target.GetLocation().Equals(Child->GetAbsOrigin(), .001));
	TestTrue(TEXT("target submission never copies target onto native solved pose"), Native->GetUnrealWorldTransform().Equals(PriorPose, .001));
	auto* Solver = Native->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>(); const double BeforeTime = Solver->GetSolverTime();
	if (!Fixture.TestWorld.TickTestWorld(.02f)) return false;
	World.FinishNativePhysicsFrame();
	TestTrue(TEXT("production frame advances actual native solver"), Solver->GetSolverTime() > BeforeTime);
	if (!Fixture.TestWorld.TickTestWorld(.02f)) return false;
	for (int32 Index = 3; Index < Ids.Num(); ++Index)
	{
		auto* Negative = World.FindByName(*FString::Printf(TEXT("child%d"), Index));
		const auto* Mesh = Prepared->StaticMesh(Ids[Index], Error);
		const FVector Center = Negative->GetAbsOrigin() + Mesh->GetBounds().Origin;
		FHitResult Hit; const FCollisionQueryParams Params(FName(TEXT("StudioNoBinary")), false);
		TestFalse(TEXT("native scene query passes through reached no-binary studio art"), Fixture.World->LineTraceSingleByChannel(Hit, Center-FVector(500,0,0), Center+FVector(500,0,0), ElysiumCollision::SightChannel, Params));
	}
	TestNotNull(TEXT("completed frame keeps native controller incarnation"), ElysiumModelPhysics::ShadowFor(Object));
#if WITH_EDITOR
	const auto CompilationToken = ElysiumModelPhysics::CaptureIdentity(Object);
	auto* SelectedCarrier = Cast<UElysiumPhysicsPropComponent>(Component);
	SelectedCarrier->PreStaticMeshCompilation(); SelectedCarrier->PostStaticMeshCompilation();
	TestTrue(TEXT("actual public render callbacks preserve invisible selected body and controller"), ElysiumModelPhysics::IsCurrentOutput(CompilationToken) && Child->VPhysicsGetObject() == Object && ElysiumModelPhysics::ShadowFor(Object));
#endif
	Child->ScriptHide(); TestTrue(TEXT("actual Hide clears query and simulation flags"), ShapeFlags(Object) == FIntPoint(0,0));
	BaseAlias->VPhysicsSetObject(Object); PropAlias->VPhysicsSetObject(Object);
	auto* OwnerSwap = World.FindByName(TEXT("child1")); void* SwappedObject = OwnerSwap->VPhysicsGetObject();
	OwnerSwap->VPhysicsSwapObject(nullptr);
	TestTrue(TEXT("owner null Swap retains actual actor/controller but removes membership"), ElysiumModelPhysics::ComponentFor(SwappedObject) && ElysiumModelPhysics::ShadowFor(SwappedObject) && !World.PhysicsShadows().Contains(OwnerSwap->Handle));
	OwnerSwap->VPhysicsSetObject(SwappedObject);
	TestFalse(TEXT("SetObject rebind does not recreate owner membership"), World.PhysicsShadows().Contains(OwnerSwap->Handle));
	ElysiumModelPhysics::RemoveShadow(Removed->VPhysicsGetObject()); World.RemovePhysicsShadow(*Removed);
	TestNull(TEXT("own public RemoveShadowController leaves no controller"), ElysiumModelPhysics::ShadowFor(Removed->VPhysicsGetObject()));
	TestNotNull(TEXT("controller removal retains own dynamic model recipe"), ElysiumModelPhysics::RecipeFor(Removed->VPhysicsGetObject()));
	auto* NoBody = World.FindByName(TEXT("child3"));
	const int32 ChildIndex = Child->Handle.Index, BaseIndex = BaseAlias->Handle.Index, PropIndex = PropAlias->Handle.Index;
	const int32 RemovedIndex = Removed->Handle.Index, NoBodyIndex = NoBody->Handle.Index;
	const int32 SwapIndex = OwnerSwap->Handle.Index;
	FElysiumSavedPhysicsBody Before; if (!ElysiumModelPhysics::Capture(Object, Before)) return false;
	FElysiumSavedPhysicsBody BeforeSwap; if (!ElysiumModelPhysics::Capture(SwappedObject, BeforeSwap)) return false;
	TestEqual(TEXT("actual hidden native flags captured independently of component collision profile"), Before.ModelShapeFlags, TArray<uint8>({0}));
	for (int32 InvalidArm = 0; InvalidArm < 3; ++InvalidArm)
	{
		FElysiumSavedPhysicsBody Invalid = Before;
		if (InvalidArm == 0) Invalid.ModelShapeFlags.Reset();
		if (InvalidArm == 1) Invalid.ModelShapeFlags.Add(0);
		if (InvalidArm == 2) Invalid.ModelShapeFlags[0] = 4;
		auto* InvalidCarrier = NewObject<UElysiumPhysicsPropComponent>(Map);
		InvalidCarrier->SetVisibility(false); InvalidCarrier->RegisterComponent(); Map->AddInstanceComponent(InvalidCarrier);
		FString InvalidError;
		TestFalse(TEXT("model saved flags require actual selected cook count and legal semantic bits"), ElysiumModelPhysics::Restore(InvalidCarrier, World.PhysicsSurfaces(), Invalid, InvalidError));
		TestFalse(TEXT("rejected selected-cook reconstruction leaves no native geometry"), InvalidCarrier->GetBodyInstance()->IsValidBodyInstance());
		TestTrue(TEXT("failed reconstruction preserves existing field/controller/member"), Child->VPhysicsGetObject() == Object && ElysiumModelPhysics::ShadowFor(Object) && World.PhysicsShadows().Contains(Child->Handle));
		if (InvalidArm == 2)
		{
			TArray<uint8> InvalidBytes; FMemoryWriter InvalidWriter(InvalidBytes, true); InvalidWriter << Invalid;
			FElysiumSavedPhysicsBody InvalidRead; FMemoryReader InvalidReader(InvalidBytes, true); InvalidReader << InvalidRead;
			TestTrue(TEXT("typed archive refuses unknown model native shape flag bits"), InvalidReader.IsError());
		}
		InvalidCarrier->DestroyComponent();
	}
	FElysiumMapSnapshot Snapshot; World.Freeze(Snapshot);
	if (!TestFalse(TEXT("joined real model-shadow native save admission"), Snapshot.bPhysicsSaveFailed)) return false;
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes, true); FElysiumSaveArchive SaveAr(Writer, FElysiumSaveVersion::Latest); SaveAr << Snapshot;
	FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes, true); FElysiumSaveArchive LoadAr(Reader, FElysiumSaveVersion::Latest); LoadAr << Persisted;
	if (!TestFalse(TEXT("real typed Physics archive decodes"), Reader.IsError())) return false;
	const auto OldToken = ElysiumModelPhysics::CaptureIdentity(Object); World.Teardown();
	TestFalse(TEXT("source teardown fences old incarnation"), ElysiumModelPhysics::IsCurrentOutput(OldToken));
	FElysiumEntityWorld Destination(Map, nullptr, Services); Destination.Load(MoveTemp(DestinationDefs)); Persisted.PhysicsBodies.Reset();
	if (!TestTrue(TEXT("inactive reconnect reads Physics bytes"), Destination.ApplySnapshot(Persisted) >= 0)) return false;
	auto ResolveSaved = [&](int32 Index) { return Destination.Resolve(FElysiumEntityHandle(Index, Destination.GetEpoch())); };
	auto* Restored = ResolveSaved(ChildIndex); auto* RestoredBase = ResolveSaved(BaseIndex); auto* RestoredProp = ResolveSaved(PropIndex);
	auto* RestoredRemoved = ResolveSaved(RemovedIndex); auto* RestoredAbsent = ResolveSaved(NoBodyIndex);
	if (!Restored || !RestoredBase || !RestoredProp || !RestoredRemoved || !RestoredAbsent) return false;
	auto CheckIndependentFields = [&](FElysiumEntityWorld& RestoringWorld, const TArray<FElysiumEntity*>& SavedFields)
	{
	for (auto* SavedEntity : SavedFields)
	{
		FElysiumSavedPhysicsBody Actual;
		if (!TestTrue(TEXT("independent saved field allocated actual selected native body"), ElysiumModelPhysics::Capture(SavedEntity->VPhysicsGetObject(), Actual))) return false;
		TestTrue(TEXT("independent native field retains solved pose and controller target"), Actual.Pose.Equals(Before.Pose, .001) && Actual.Shadow.Target.Equals(Before.Shadow.Target, .001));
		TestTrue(TEXT("independent native current mechanics roundtrip before first think"), Actual.LinearVelocity.Equals(Before.LinearVelocity, .001) && Actual.AngularVelocityRadians.Equals(Before.AngularVelocityRadians, .001)
			&& Actual.CurrentMassKg == Before.CurrentMassKg && Actual.CurrentInertia == Before.CurrentInertia && Actual.CurrentInverseInertia == Before.CurrentInverseInertia
			&& Actual.CurrentMassFrame.Equals(Before.CurrentMassFrame, 1.e-6) && Actual.CurrentSurface == Before.CurrentSurface
			&& Actual.CurrentLinearDamping == Before.CurrentLinearDamping && Actual.CurrentAngularDamping == Before.CurrentAngularDamping && Actual.bGravity == Before.bGravity && Actual.bAsleep == Before.bAsleep);
		TestTrue(TEXT("independent controller originals permission limits and arrival roundtrip"), Actual.Shadow.bInstalled == Before.Shadow.bInstalled && Actual.Shadow.bActive == Before.Shadow.bActive
			&& Actual.Shadow.bAllowMovement == Before.Shadow.bAllowMovement && Actual.Shadow.bAllowRotation == Before.Shadow.bAllowRotation && Actual.Shadow.bGravityMode == Before.Shadow.bGravityMode
			&& Actual.Shadow.RemainingSeconds == Before.Shadow.RemainingSeconds && Actual.Shadow.SavedMassKg == Before.Shadow.SavedMassKg
			&& Actual.Shadow.SavedInertia == Before.Shadow.SavedInertia && Actual.Shadow.SavedInverseInertia == Before.Shadow.SavedInverseInertia && Actual.Shadow.SavedMassFrame.Equals(Before.Shadow.SavedMassFrame, 1.e-6)
			&& Actual.Shadow.SavedLinearDamping == Before.Shadow.SavedLinearDamping && Actual.Shadow.SavedAngularDamping == Before.Shadow.SavedAngularDamping && Actual.Shadow.bSavedGravity == Before.Shadow.bSavedGravity
			&& Actual.Shadow.LinearDesiredLimitCm == Before.Shadow.LinearDesiredLimitCm && Actual.Shadow.AngularDesiredLimitRadians == Before.Shadow.AngularDesiredLimitRadians
			&& Actual.Shadow.LinearDampingLimitCm == Before.Shadow.LinearDampingLimitCm && Actual.Shadow.AngularDampingLimitRadians == Before.Shadow.AngularDampingLimitRadians
			&& Actual.Shadow.PredictedPosition.Equals(Before.Shadow.PredictedPosition, .001) && Actual.Shadow.TeleportDistanceCm == Before.Shadow.TeleportDistanceCm);
		TestTrue(TEXT("owner field membership independently persists"), RestoringWorld.PhysicsShadows().Contains(SavedEntity->Handle) == (SavedEntity == SavedFields[0]));
		TestTrue(TEXT("independent saved hidden native field retains actual query/sim flags"), ShapeFlags(SavedEntity->VPhysicsGetObject()) == FIntPoint(0,0));
	}
	return true;
	};
	if (!CheckIndependentFields(Destination, {Restored, RestoredBase, RestoredProp})) return false;
	TestTrue(TEXT("saved alias fields allocate independently"), Restored->VPhysicsGetObject() != RestoredBase->VPhysicsGetObject() && Restored->VPhysicsGetObject() != RestoredProp->VPhysicsGetObject() && RestoredBase->VPhysicsGetObject() != RestoredProp->VPhysicsGetObject());
	TestTrue(TEXT("removed-controller dynamic Model restores actual body without controller/member"), RestoredRemoved->VPhysicsGetObject() && !ElysiumModelPhysics::ShadowFor(RestoredRemoved->VPhysicsGetObject()) && !Destination.PhysicsShadows().Contains(RestoredRemoved->Handle));
	TestNull(TEXT("count0 studio field never recreates body on reconnect"), RestoredAbsent->VPhysicsGetObject());
	auto* RestoredSwap = ResolveSaved(SwapIndex);
	TestTrue(TEXT("actual model owner Swap absence persists native controller without membership"), RestoredSwap && RestoredSwap->VPhysicsGetObject() && ElysiumModelPhysics::ShadowFor(RestoredSwap->VPhysicsGetObject()) && !Destination.PhysicsShadows().Contains(RestoredSwap->Handle));
	FElysiumSavedPhysicsBody AfterSwap; if (!ElysiumModelPhysics::Capture(RestoredSwap->VPhysicsGetObject(), AfterSwap)) return false;
	TestEqual(TEXT("selected TrunkLid current native mass restored without geometry recalculation drift"), AfterSwap.CurrentMassKg, BeforeSwap.CurrentMassKg);
	void* RestoredObject = Restored->VPhysicsGetObject(); auto* RestoredComponent = ElysiumModelPhysics::ComponentFor(RestoredObject);
	const auto RestoredToken = ElysiumModelPhysics::CaptureIdentity(RestoredObject);
	const auto FormerPropToken = ElysiumModelPhysics::CaptureIdentity(RestoredProp->VPhysicsGetObject());
	RestoredBase->VPhysicsSetObject(RestoredObject); RestoredProp->VPhysicsSetObject(RestoredObject);
	int32 BeforeRelease = 0;
	Destination.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
	{
		if (Event.Kind != TEXT("physics_retire")) return; ++BeforeRelease;
		TestTrue(TEXT("pre-release callbacks observe all aliases null"), !Restored->VPhysicsGetObject() && !RestoredBase->VPhysicsGetObject() && !RestoredProp->VPhysicsGetObject());
		TestTrue(TEXT("pre-release callbacks observe controller recipe and member retired"), !ElysiumModelPhysics::ShadowFor(RestoredObject) && !ElysiumModelPhysics::RecipeFor(RestoredObject) && !Destination.PhysicsShadows().Contains(Restored->Handle));
		TestTrue(TEXT("callbacks occur before actual native release"), RestoredComponent->GetBodyInstance()->IsValidBodyInstance());
	});
	RestoredBase->VPhysicsDestroyObject(); Destination.SetAiTraceSink({});
	TestEqual(TEXT("borrowed225 visits all three actual references"), BeforeRelease, 3);
	TestFalse(TEXT("borrowed225 releases native and fences stale output"), RestoredComponent->GetBodyInstance()->IsValidBodyInstance() || ElysiumModelPhysics::IsCurrentOutput(RestoredToken));
	Restored->ScriptHide(); Restored->ScriptUnhide(); Restored->SetRuntimeOrigin(FVector(11,22,33));
	TestFalse(TEXT("later art gates cannot resurrect retired native model"), RestoredComponent->GetBodyInstance()->IsValidBodyInstance());
	TestTrue(TEXT("unrelated borrowed retirement preserves prop alias former owned producer"), ElysiumModelPhysics::IsCurrentOutput(FormerPropToken));
	RestoredProp->Kill(); TestFalse(TEXT("Kill releases former owned producer even when current field was cleared"), ElysiumModelPhysics::IsCurrentOutput(FormerPropToken));
	const auto ReplacementToken = ElysiumModelPhysics::CaptureIdentity(RestoredRemoved->VPhysicsGetObject());
	RestoredRemoved->SetRuntimeModel(TEXT("models/scenery/structural/doorknoboffice/doorknoboffice.mdl"));
	TestTrue(TEXT("real SetModel replaces own removed-controller producer"), RestoredRemoved->VPhysicsGetObject() && !ElysiumModelPhysics::IsCurrentOutput(ReplacementToken));
	const auto FailedToken = ElysiumModelPhysics::CaptureIdentity(RestoredRemoved->VPhysicsGetObject());
	RestoredRemoved->SetRuntimeModel(TEXT("models/scenery/structural/doorknoba/doorknoba.mdl"));
	TestTrue(TEXT("genuine no-binary SetModel retires former producer without fallback"), !RestoredRemoved->VPhysicsGetObject() && !ElysiumModelPhysics::IsCurrentOutput(FailedToken));
	AddInfo(FString::Printf(TEXT("model-shadow joined: archive_bytes=%d independent_fields=3 hidden_native_flags=0 removed_controller_body=1 count0=1 callbacks=%d"), Bytes.Num(), BeforeRelease));
	Destination.Teardown();
	FElysiumEntityWorld Travel(Map, nullptr, Services); Travel.Load(MoveTemp(TravelDefs));
	if (!TestTrue(TEXT("level-transition reconnect consumes same typed Physics block"), Travel.ApplySnapshot(Persisted, 0., true) >= 0)) return false;
	auto ResolveTravel = [&](int32 Index) { return Travel.Resolve(FElysiumEntityHandle(Index, Travel.GetEpoch())); };
	auto* TravelOwner = ResolveTravel(ChildIndex); auto* TravelBase = ResolveTravel(BaseIndex); auto* TravelProp = ResolveTravel(PropIndex);
	auto* TravelRemoved = ResolveTravel(RemovedIndex); auto* TravelSwap = ResolveTravel(SwapIndex); auto* TravelAbsent = ResolveTravel(NoBodyIndex);
	if (!TravelOwner || !TravelBase || !TravelProp || !TravelRemoved || !TravelSwap || !TravelAbsent) return false;
	if (!CheckIndependentFields(Travel, {TravelOwner, TravelBase, TravelProp})) return false;
	TestTrue(TEXT("level transition preserves independent saved native allocations"), TravelOwner->VPhysicsGetObject() != TravelBase->VPhysicsGetObject()
		&& TravelOwner->VPhysicsGetObject() != TravelProp->VPhysicsGetObject() && TravelBase->VPhysicsGetObject() != TravelProp->VPhysicsGetObject());
	TestTrue(TEXT("level transition retains removed-controller model without member"), TravelRemoved->VPhysicsGetObject() && !ElysiumModelPhysics::ShadowFor(TravelRemoved->VPhysicsGetObject()) && !Travel.PhysicsShadows().Contains(TravelRemoved->Handle));
	TestTrue(TEXT("level transition retains owner Swap controller without member"), TravelSwap->VPhysicsGetObject() && ElysiumModelPhysics::ShadowFor(TravelSwap->VPhysicsGetObject()) && !Travel.PhysicsShadows().Contains(TravelSwap->Handle));
	FElysiumSavedPhysicsBody TravelSwapState; if (!ElysiumModelPhysics::Capture(TravelSwap->VPhysicsGetObject(), TravelSwapState)) return false;
	TestEqual(TEXT("level transition retains exact current native TrunkLid mass"), TravelSwapState.CurrentMassKg, BeforeSwap.CurrentMassKg);
	TestNull(TEXT("level transition respects exact count0 studio field"), TravelAbsent->VPhysicsGetObject());
	const auto TravelToken = ElysiumModelPhysics::CaptureIdentity(TravelOwner->VPhysicsGetObject());
	Travel.Teardown(); TestFalse(TEXT("level-transition teardown fences selected native output"), ElysiumModelPhysics::IsCurrentOutput(TravelToken));
	AddInfo(TEXT("model-shadow level-transition: typed Physics block restored before first think, three independent hidden fields, removed controller, owner Swap absence, count0"));
	ElysiumPreparedProps::Release(Map);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsCorePreparedAdmissionTest, "Elysium.Substrate.ModelPhysics.CorePreparedAdmission",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsCorePreparedAdmissionTest::RunTest(const FString&)
{
 FPlayerWorldFixture Fixture;
 if (!Fixture.CreateWorld(*this)) return false;
 auto* Map = Fixture.SpawnMapActorDeferred();
 auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
 auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
 // Exact prepared core2 PhysProp addresses and first binary hull counts, source receipts retained
 // in l0-p1-remaining-bodies-20261011/core-prepared-inputs.json. No inferred package addresses.
 struct FWitness { const TCHAR* Id; const TCHAR* Path; int32 Hulls; };
 const FWitness Witnesses[] = {
		{ TEXT("vtmb:model:scenery/physics/bottle/bottle"), TEXT("/ElysiumBaked/Models/scenery/physics/bottle/SM_bottle.SM_bottle"), 1 },
		{ TEXT("vtmb:model:scenery/physics/sardine/sardine"), TEXT("/ElysiumBaked/Models/scenery/physics/sardine/SM_sardine.SM_sardine"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/retro_chair/retro_chair"), TEXT("/ElysiumBaked/Models/scenery/furniture/retro_chair/SM_retro_chair.SM_retro_chair"), 1 },
		{ TEXT("vtmb:model:scenery/misc/bottles/glassa"), TEXT("/ElysiumBaked/Models/scenery/misc/bottles/SM_glassa.SM_glassa"), 1 },
		{ TEXT("vtmb:model:scenery/misc/chairoffice/chairoffice"), TEXT("/ElysiumBaked/Models/scenery/misc/chairoffice/SM_chairoffice.SM_chairoffice"), 2 },
		{ TEXT("vtmb:model:scenery/misc/plates/crapy"), TEXT("/ElysiumBaked/Models/scenery/misc/plates/SM_crapy.SM_crapy"), 1 },
		{ TEXT("vtmb:model:scenery/misc/plates/plate4"), TEXT("/ElysiumBaked/Models/scenery/misc/plates/SM_plate4.SM_plate4"), 1 },
		{ TEXT("vtmb:model:scenery/misc/plates/plate5"), TEXT("/ElysiumBaked/Models/scenery/misc/plates/SM_plate5.SM_plate5"), 1 },
		{ TEXT("vtmb:model:scenery/misc/plates/plate7"), TEXT("/ElysiumBaked/Models/scenery/misc/plates/SM_plate7.SM_plate7"), 1 },
		{ TEXT("vtmb:model:scenery/misc/trashgarage/trashgarage"), TEXT("/ElysiumBaked/Models/scenery/misc/trashgarage/SM_trashgarage.SM_trashgarage"), 1 },
		{ TEXT("vtmb:model:scenery/structural/society/barrel"), TEXT("/ElysiumBaked/Models/scenery/structural/society/SM_barrel.SM_barrel"), 1 },
		{ TEXT("vtmb:model:scenery/structural/society/shutters"), TEXT("/ElysiumBaked/Models/scenery/structural/society/SM_shutters.SM_shutters"), 1 },
		{ TEXT("vtmb:model:scenery/structural/society/stool"), TEXT("/ElysiumBaked/Models/scenery/structural/society/SM_stool.SM_stool"), 1 },
		{ TEXT("vtmb:model:scenery/structural/warehouse/break_crate"), TEXT("/ElysiumBaked/Models/scenery/structural/warehouse/SM_break_crate.SM_break_crate"), 1 },
		{ TEXT("vtmb:model:scenery/trash/barrels/barrela"), TEXT("/ElysiumBaked/Models/scenery/trash/barrels/SM_barrela.SM_barrela"), 1 },
		{ TEXT("vtmb:model:scenery/trash/barrels/barrelb"), TEXT("/ElysiumBaked/Models/scenery/trash/barrels/SM_barrelb.SM_barrelb"), 1 },
		{ TEXT("vtmb:model:scenery/trash/barrels/barrelc"), TEXT("/ElysiumBaked/Models/scenery/trash/barrels/SM_barrelc.SM_barrelc"), 1 },
		{ TEXT("vtmb:model:scenery/trash/barrels/barreld"), TEXT("/ElysiumBaked/Models/scenery/trash/barrels/SM_barreld.SM_barreld"), 1 },
 };
 TArray<FString> Ids;
 for (const auto& Witness : Witnesses) Ids.Add(Witness.Id);
 TSet<FSoftObjectPath> Paths; FString Error;
 if (!TestTrue(TEXT("actual placed and skin catalogue path gathering"), FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error)))
 { AddError(Error); return false; }
 TArray<UObject*> Assets;
 for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
 auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
 if (!TestTrue(TEXT("all18 actual resident prepared admission"), Prepared.IsValid())) { AddError(Error); return false; }
 ON_SCOPE_EXIT { ElysiumPreparedProps::Release(Map); };
 FElysiumWorldServices Services; Services.Embodiment = Map;
 FElysiumEntityWorld EntityWorld(Map, nullptr, Services); EntityWorld.Load(FElysiumEntityDefs());
 for (const auto& Witness : Witnesses)
 {
  auto* Mesh = Prepared->StaticMesh(Witness.Id, Error);
  const FString Label = FString(Witness.Id) + TEXT(" ") + Witness.Path;
  if (!TestNotNull(*(Label + TEXT(" prepared static mesh")), Mesh)) continue;
  TestEqual(*(Label + TEXT(" exact prepared package address")), Mesh->GetPathName(), FString(Witness.Path));
  const auto* Inputs = UElysiumModelPhysicsInputs::Find(Mesh);
  AddInfo(Label + FString::Printf(TEXT(" typed=%d source=%d selected=%d hulls=%d"), Inputs != nullptr,
   Inputs && Inputs->bSourceBody, Inputs && Inputs->SelectedBody != nullptr,
   Inputs && Inputs->SelectedBody ? Inputs->SelectedBody->AggGeom.ConvexElems.Num() : 0));
  if (!TestNotNull(*(Label + TEXT(" typed body inputs")), Inputs)) continue;
  TestTrue(*(Label + TEXT(" real first binary solid")), Inputs->bSourceBody);
  if (!TestNotNull(*(Label + TEXT(" selected native body setup")), Inputs->SelectedBody.Get())) continue;
  TestEqual(*(Label + TEXT(" source first hull count")), Inputs->SelectedBody->AggGeom.ConvexElems.Num(), Witness.Hulls);
  auto* Component = NewObject<UElysiumPhysicsPropComponent>(Map);
  Component->SetVisibility(false); // selected collision cook is independent of asynchronous render compilation
  Component->SetStaticMesh(Mesh); Component->RegisterComponent(); Map->AddInstanceComponent(Component);
  if (TestTrue(*(Label + TEXT(" selected native cook creates actor")), ElysiumModelPhysics::Create(Component, EntityWorld.PhysicsSurfaces(), 0.f, 0.f, true, Error)))
  {
   int32 ShapeCount = 0;
   FPhysicsCommand::ExecuteRead(ElysiumModelPhysics::Identity(Component)->GetPhysicsActorHandle(), [&](const FPhysicsActorHandle& Actor)
   { TArray<FPhysicsShapeHandle> Shapes; FPhysicsInterface::GetAllShapes_AssumedLocked(Actor, Shapes); ShapeCount = Shapes.Num(); });
   TestEqual(*(Label + TEXT(" live native shape count")), ShapeCount, Witness.Hulls);
  }
  else AddError(Label + TEXT(" ") + Error);
  ElysiumModelPhysics::Destroy(Component); Component->DestroyComponent();
 }
 return true;
}

namespace ElysiumHiddenSpawnTest
{
 TArray<FString> Events;
 class FProbe final : public FElysiumEntity
 {
 public:
  void Spawn() override
  {
   Events.Add(TargetName + TEXT(":spawn"));
   SetSolid(6); SetSolidFlags(0); SetMoveType(7, 0);
   ThinkCallback = TEXT("spawn_think"); NextThink = 12.f;
   if (Def->Keys.Contains(TEXT("kill_spawn"))) Kill();
  }
  void OnDormancyChanged() override
  {
   if (bHidden) Events.Add(TargetName + TEXT(":hide"));
   FElysiumEntity::OnDormancyChanged();
  }
  void PostSpawn() override { Events.Add(TargetName + TEXT(":later")); }
  void Activate() override { Events.Add(TargetName + TEXT(":activate")); }
 };
 TUniquePtr<FElysiumEntity> MakeProbe() { return MakeUnique<FProbe>(); }
 FElysiumClassRegistrar Registrar(TEXT("elysium_test_hidden_spawn_probe"), ElysiumBaseClassName(), &MakeProbe, [](FElysiumClassDesc&) {});
 FElysiumEntityDef Def(const TCHAR* Name, bool Hidden)
 {
  FElysiumEntityDef Result; Result.Classname = TEXT("elysium_test_hidden_spawn_probe"); Result.TargetName = Name; Result.bStartHidden = Hidden; return Result;
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsHiddenSpawnOrderTest, "Elysium.Substrate.ModelPhysics.HiddenSpawnOrder",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsHiddenSpawnOrderTest::RunTest(const FString&)
{
 using namespace ElysiumHiddenSpawnTest;
 FElysiumEntityWorld World(nullptr, nullptr, FElysiumWorldServices());
 FElysiumEntityDefs Defs; Defs.MapName = TEXT("hidden_spawn_order");
 Defs.Defs.Add(Def(TEXT("first"), true)); Defs.Defs.Add(Def(TEXT("next"), false));
 auto Dead = Def(TEXT("dead"), true); Dead.Keys.Add(TEXT("kill_spawn"), TEXT("1")); Defs.Defs.Add(Dead);
 Events.Reset(); World.Load(MoveTemp(Defs));
 TestEqual(TEXT("101d1280 direct base hidden arm runs before next Spawn; dead arm skips it"), FString::Join(Events, TEXT(" ")),
  FString(TEXT("first:spawn first:hide next:spawn dead:spawn first:later next:later")));
 auto* First = World.FindByName(TEXT("first")); if (!First) return false;
 TestTrue(TEXT("hidden captures Spawn think callback and complete physical words"), First->bHidden && First->ThinkCallback.IsNone()
  && First->SavedThinkCallback == FName(TEXT("spawn_think")) && First->ScriptSavedSolid == 6 && First->ScriptSavedMoveType == 7 && First->ScriptSavedSolidFlags == 0);
 First->ScriptUnhide(); TestEqual(TEXT("actual unhide reinstates Spawn callback"), First->ThinkCallback, FName(TEXT("spawn_think")));
 World.Activate(0.);
 auto NoSpawn = Def(TEXT("no_spawn"), true);
 auto* Constructed = World.Resolve(World.CreateRuntimeEntityNoSpawn(NoSpawn)); if (!Constructed) return false;
 TestTrue(TEXT("Construct/noSpawn keeps StartHidden keyfield without invoking ScriptHide"), Constructed->bStartHidden && !Constructed->bHidden && !Constructed->bSavedPhysicalWordsAvailable && !Constructed->bSpawnCalled);
 Events.Reset(); World.CallEntitySpawn(*Constructed);
 TestEqual(TEXT("runtime direct hidden arm precedes later work and activation"), FString::Join(Events, TEXT(" ")),
  FString(TEXT("no_spawn:spawn no_spawn:hide no_spawn:later no_spawn:activate")));
 TestEqual(TEXT("runtime captures own Spawn-installed callback"), Constructed->SavedThinkCallback, FName(TEXT("spawn_think")));
 auto RuntimeDead = Def(TEXT("runtime_dead"), true); RuntimeDead.Keys.Add(TEXT("kill_spawn"), TEXT("1"));
 Events.Reset(); World.SpawnRuntimeEntity(RuntimeDead);
 TestEqual(TEXT("runtime dead Spawn reaches no hide/later/activation"), FString::Join(Events,TEXT(" ")), FString(TEXT("runtime_dead:spawn")));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsHiddenPropBodyTest, "Elysium.Substrate.ModelPhysics.HiddenPropBody",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsHiddenPropBodyTest::RunTest(const FString&)
{
 FPlayerWorldFixture Fixture; if (!Fixture.CreateWorld(*this)) return false;
 auto* Map = Fixture.SpawnMapActorDeferred();
 auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr,TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
 auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr,TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
 const TArray<FString> Ids = { TEXT("vtmb:model:scenery/misc/plates/plate5"), TEXT("vtmb:model:scenery/misc/chairoffice/chairoffice"), ElysiumModelPhysicsTest::AbsentId };
 TSet<FSoftObjectPath> Paths; FString Error;
 if (!FElysiumPreparedPropModels::GatherPaths(Placed,Skins,Ids,Paths,Error)) { AddError(Error); return false; }
 TArray<UObject*> Assets; for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
 auto Prepared=FElysiumPreparedPropModels::Create(Map,1,Placed,Skins,Ids,Assets,Error);
 if (!Prepared) { AddError(Error); return false; } ON_SCOPE_EXIT { ElysiumPreparedProps::Release(Map); };
 FElysiumWorldServices Services; Services.Embodiment=Map;
 FElysiumEntityDefs Defs; Defs.MapName=TEXT("hidden_prop_native");
 auto PlateDef=ElysiumModelPhysicsTest::Def(TEXT("models/scenery/misc/plates/plate5.mdl"),TEXT("plus_food"));
 PlateDef.bStartHidden=true; PlateDef.Keys.Add(TEXT("StartHidden"),TEXT("1")); PlateDef.Keys.Add(TEXT("override_mass"),TEXT("-1")); PlateDef.Origin=FVector(5000,5000,3000); Defs.Defs.Add(PlateDef);
 auto ChairDef=ElysiumModelPhysicsTest::Def(TEXT("models/scenery/misc/chairoffice/chairoffice.mdl"),TEXT("chair"));
 ChairDef.Keys.Add(TEXT("spawnflags"),TEXT("0")); ChairDef.Origin=FVector(8000,5000,3000); Defs.Defs.Add(ChairDef);
 auto AliasDef=ChairDef; AliasDef.TargetName=TEXT("prop_alias"); AliasDef.Origin=FVector(12000,5000,3000); Defs.Defs.Add(AliasDef);
 auto RemovedDef=ChairDef; RemovedDef.TargetName=TEXT("retired"); RemovedDef.Origin=FVector(15000,5000,3000); Defs.Defs.Add(RemovedDef);
 auto AbsentDef=ElysiumModelPhysicsTest::Def(TEXT("models/scenery/structural/society/chair.mdl"),TEXT("no_binary")); Defs.Defs.Add(AbsentDef);
 FElysiumEntityDef Point; Point.Classname=TEXT("info_target"); Point.TargetName=TEXT("base_alias"); Defs.Defs.Add(Point);
 auto StaticDef=PlateDef; StaticDef.Classname=TEXT("prop_dynamic"); StaticDef.TargetName=TEXT("hidden_static"); StaticDef.Keys.Add(TEXT("solid"),TEXT("6")); StaticDef.Origin=FVector(21000,5000,3000); Defs.Defs.Add(StaticDef);
 FElysiumEntityDef Parent; Parent.Classname=TEXT("func_door"); Parent.TargetName=TEXT("hidden_parent"); Parent.Model=2; Parent.Keys.Add(TEXT("model"),TEXT("*2")); Parent.Origin=FVector(24000,5000,3000);
 FElysiumConvexHull ParentHull; for(float X:{-20.f,20.f})for(float Y:{-20.f,20.f})for(float Z:{-20.f,20.f})ParentHull.Vertices.Add(FVector(X,Y,Z)); Parent.Hulls.Add(ParentHull); Defs.Defs.Add(Parent);
 auto ShadowDef=StaticDef; ShadowDef.TargetName=TEXT("hidden_shadow"); ShadowDef.Keys.Add(TEXT("parentname"),TEXT("hidden_parent")); ShadowDef.Origin=FVector(24000,5000,3000); Defs.Defs.Add(ShadowDef);
 FElysiumEntityDef BrushDef; BrushDef.Classname=TEXT("func_brush"); BrushDef.TargetName=TEXT("hidden_brush"); BrushDef.Model=1; BrushDef.Keys.Add(TEXT("model"),TEXT("*1")); BrushDef.bStartHidden=true; BrushDef.Origin=FVector(27000,5000,3000);
 FElysiumConvexHull BrushHull; for(float X:{-20.f,20.f})for(float Y:{-20.f,20.f})for(float Z:{-20.f,20.f})BrushHull.Vertices.Add(FVector(X,Y,Z)); BrushDef.Hulls.Add(BrushHull); Defs.Defs.Add(BrushDef);
 auto BornHiddenDoorDef=BrushDef; BornHiddenDoorDef.Classname=TEXT("func_door"); BornHiddenDoorDef.TargetName=TEXT("born_hidden_door"); BornHiddenDoorDef.Model=3; BornHiddenDoorDef.Keys.Add(TEXT("model"),TEXT("*3")); BornHiddenDoorDef.Origin=FVector(28000,5000,3000); Defs.Defs.Add(BornHiddenDoorDef);
 auto DestinationDefs=Defs; auto TravelDefs=Defs;
 FElysiumEntityWorld World(Map,nullptr,Services); World.Load(MoveTemp(Defs));
 for(const TCHAR* Name:{TEXT("hidden_static"),TEXT("hidden_shadow")})
 {
  auto* Generic=World.FindByName(Name); FElysiumSavedPhysicsBody State;
  if (!Generic||!TestTrue(TEXT("StartHidden follows generic native model Spawn producer"),ElysiumModelPhysics::Capture(Generic->VPhysicsGetObject(),State))) return false;
  TestTrue(TEXT("hidden static and parented model-shadow preserve selected native body and restore words"),Generic->bHidden&&Generic->ScriptSavedSolid==6&&State.ModelShapeFlags==TArray<uint8>({0}));
 }
 auto* HiddenBrush=World.FindByName(TEXT("hidden_brush"));
 if (!TestNotNull(TEXT("born-hidden brush body built before shared hidden dispatch"),HiddenBrush ? HiddenBrush->Body : nullptr)) return false;
 int32 BornHiddenBrushShapes=0, BornHiddenBrushActiveShapes=0;
 if (auto* Native=ElysiumModelPhysics::Identity(HiddenBrush->Body)) FPhysicsCommand::ExecuteRead(Native->GetPhysicsActor(),[&](const FPhysicsActorHandle&)
 {
  TArray<FPhysicsShapeHandle> Shapes; Native->GetAllShapes_AssumesLocked(Shapes); BornHiddenBrushShapes=Shapes.Num();
  for (const auto& Shape:Shapes) if (FPhysicsInterface::IsQueryShape(Shape)||FPhysicsInterface::IsSimulationShape(Shape)) ++BornHiddenBrushActiveShapes;
 });
 // This existing func_brush fixture inherits base223/no native recipe. Preserve its query-only
 // fallback gate; the distinct door below reaches the admitted P1 shadow producer.
 TestTrue(TEXT("born-hidden nonrecipe func_brush retains its original collision and hidden-word gate"),
  HiddenBrush->bHidden&&HiddenBrush->bSavedPhysicalWordsAvailable&&HiddenBrush->Body->GetCollisionEnabled()==ECollisionEnabled::NoCollision
  &&!ElysiumModelPhysics::RecipeFor(ElysiumModelPhysics::ActorIdentity(HiddenBrush->Body))&&BornHiddenBrushActiveShapes==0);
 FHitResult BornHiddenBrushHit;
 TestFalse(TEXT("born-hidden nonrecipe func_brush remains absent from the real use query"),Fixture.TestWorld.GetTestWorld()->LineTraceSingleByChannel(
  BornHiddenBrushHit,BrushDef.Origin+FVector(0,0,60),BrushDef.Origin-FVector(0,0,60),ELYSIUM_USE_CHANNEL));
 auto* BornHiddenDoor=World.FindByName(TEXT("born_hidden_door"));
 FElysiumSavedPhysicsBody BornHiddenDoorNative;
 int32 BornHiddenDoorShapes=0, BornHiddenDoorActiveShapes=0;
 if (auto* Native=BornHiddenDoor ? ElysiumModelPhysics::Identity(BornHiddenDoor->Body) : nullptr) FPhysicsCommand::ExecuteRead(Native->GetPhysicsActor(),[&](const FPhysicsActorHandle&)
 {
  TArray<FPhysicsShapeHandle> Shapes; Native->GetAllShapes_AssumesLocked(Shapes); BornHiddenDoorShapes=Shapes.Num();
  for (const auto& Shape:Shapes) if (FPhysicsInterface::IsQueryShape(Shape)||FPhysicsInterface::IsSimulationShape(Shape)) ++BornHiddenDoorActiveShapes;
 });
 TestTrue(TEXT("born-hidden actual door preserves admitted native owner/controller with every shape gated"),
  BornHiddenDoor&&BornHiddenDoor->bHidden&&BornHiddenDoor->bSavedPhysicalWordsAvailable&&BornHiddenDoor->GetSolid()==0&&(BornHiddenDoor->GetSolidFlags()&4u)!=0
  &&ElysiumModelPhysics::Capture(BornHiddenDoor->VPhysicsGetObject(),BornHiddenDoorNative)&&BornHiddenDoorNative.Recipe.Kind==EElysiumPhysicsRecipeKind::Brush
  &&BornHiddenDoorNative.Shadow.bInstalled&&BornHiddenDoorShapes==1&&BornHiddenDoorActiveShapes==0);
 auto* Plate=static_cast<FElysiumPhysProp*>(World.FindByName(TEXT("plus_food")));
 auto* Chair=static_cast<FElysiumPhysProp*>(World.FindByName(TEXT("chair")));
 auto* PropAlias=static_cast<FElysiumPhysProp*>(World.FindByName(TEXT("prop_alias")));
 auto* Retired=static_cast<FElysiumPhysProp*>(World.FindByName(TEXT("retired")));
 auto* Absent=World.FindByName(TEXT("no_binary")); auto* BaseAlias=World.FindByName(TEXT("base_alias"));
 if (!Plate||!Chair||!PropAlias||!Retired||!Absent||!BaseAlias) return false;
 auto Capture=[&](FElysiumEntity* Ent, FElysiumSavedPhysicsBody& State) { return TestTrue(TEXT("actual native saved output"), ElysiumModelPhysics::Capture(Ent->VPhysicsGetObject(),State)); };
 auto CheckWords=[&](FElysiumEntity* Ent, bool Hidden)
 {
  int32 Solid,Move,MoveCollide,Flags,Effects;
  Ent->ReadScriptPhysicalWords(Solid,Move,MoveCollide,Flags,Effects);
  return TestTrue(TEXT("actual hidden and restored logical physical words"), Hidden ? Solid==0&&Move==0&&MoveCollide==0&&Flags==4&&Effects==0xe0 : Solid==6&&Move==7&&Flags==0);
 };
 auto SameState=[&](const FElysiumSavedPhysicsBody& A,const FElysiumSavedPhysicsBody& B,bool Flags=true)
 {
  return A.Pose.Equals(B.Pose,.001)&&A.LinearVelocity.Equals(B.LinearVelocity,.001)&&A.AngularVelocityRadians.Equals(B.AngularVelocityRadians,.001)
   &&A.CurrentMassKg==B.CurrentMassKg&&A.CurrentInertia==B.CurrentInertia&&A.CurrentInverseInertia==B.CurrentInverseInertia&&A.CurrentMassFrame.Equals(B.CurrentMassFrame,1.e-6)
   &&A.CurrentSurface==B.CurrentSurface&&A.CurrentLinearDamping==B.CurrentLinearDamping&&A.CurrentAngularDamping==B.CurrentAngularDamping&&A.bGravity==B.bGravity&&A.bAsleep==B.bAsleep
   &&A.CollisionProfile==B.CollisionProfile&&A.CollisionResponses==B.CollisionResponses&&A.CollisionMask==B.CollisionMask&&(!Flags||A.ModelShapeFlags==B.ModelShapeFlags);
 };
 FElysiumSavedPhysicsBody PlateState; if (!Capture(Plate,PlateState)) return false;
 AddInfo(FString::Printf(TEXT("born-hidden plate5: hidden=%d visible=%d asleep=%d spawnflags=%u shapes=%d firstflags=%d"),Plate->bHidden,Plate->Visual->IsVisible(),PlateState.bAsleep,Plate->SpawnFlags,PlateState.ModelShapeFlags.Num(),PlateState.ModelShapeFlags.IsEmpty()?-1:PlateState.ModelShapeFlags[0]));
 TestTrue(TEXT("authentic tutorial StartHidden plate5 retains sleeping native body and zero shape eligibility"), Plate->bHidden&&!Plate->Visual->IsVisible()&&PlateState.bAsleep&&PlateState.ModelShapeFlags==TArray<uint8>({0})&&ElysiumModelPhysics::RecipeFor(Plate->VPhysicsGetObject()));
 TestTrue(TEXT("born-hidden retains unhide solid/move/flags captured after native Spawn"), Plate->bSavedPhysicalWordsAvailable&&Plate->ScriptSavedSolid==6&&Plate->ScriptSavedMoveType==7&&Plate->ScriptSavedSolidFlags==0); CheckWords(Plate,true);
 FHitResult HiddenHit; const FVector PlateCenter=PlateState.Pose.GetLocation()+Plate->Visual->GetStaticMesh()->GetBounds().Origin;
 TestFalse(TEXT("actual scene query passes through hidden plate art"),Fixture.World->LineTraceSingleByChannel(HiddenHit,PlateCenter-FVector(500,0,0),PlateCenter+FVector(500,0,0),ElysiumCollision::SightChannel));
 void* PlateObject=Plate->VPhysicsGetObject(); const auto PlateToken=ElysiumModelPhysics::CaptureIdentity(PlateObject);
 Plate->ScriptUnhide(); FElysiumSavedPhysicsBody PlateUnhidden; if (!Capture(Plate,PlateUnhidden))return false;
 TestTrue(TEXT("sleeping plate Unhide preserves incarnation and full mechanics without waking or recalculating mass"),Plate->VPhysicsGetObject()==PlateObject&&ElysiumModelPhysics::IsCurrentOutput(PlateToken)&&SameState(PlateState,PlateUnhidden,false)&&PlateUnhidden.bAsleep);
 TestEqual(TEXT("sleeping plate Unhide publishes shape eligibility without motion enable"),PlateUnhidden.ModelShapeFlags,TArray<uint8>({3})); CheckWords(Plate,false);
 Plate->ScriptHide(); FElysiumSavedPhysicsBody PlateRehidden; if (!Capture(Plate,PlateRehidden))return false;
 TestTrue(TEXT("sleeping plate Hide preserves complete prior sleeping native state"),Plate->VPhysicsGetObject()==PlateObject&&SameState(PlateState,PlateRehidden)); CheckWords(Plate,true);
 TestTrue(TEXT("contact-modification service bit is installed before first native frame"),ElysiumModelPhysics::Identity(Plate->Visual)->bContactModification);
 World.Activate(0.);
 World.BeginNativePhysicsFrame(.02,.02); if (!Fixture.TestWorld.TickTestWorld(.02f)) return false; World.FinishNativePhysicsFrame();
 if (!Fixture.TestWorld.TickTestWorld(.02f)) return false;
 FElysiumSavedPhysicsBody PlateAfterFrames; if (!Capture(Plate,PlateAfterFrames))return false;
 TestTrue(TEXT("actual first-frame filter preparation preserves hidden sleeping plate incarnation and complete native output"),Plate->VPhysicsGetObject()==PlateObject&&ElysiumModelPhysics::IsCurrentOutput(PlateToken)&&SameState(PlateState,PlateAfterFrames)&&PlateAfterFrames.bAsleep);
 auto* ChairBody=ElysiumModelPhysics::Identity(Chair->Visual); if (!ChairBody) return false;
 Chair->Visual->SetEnableGravity(false); Chair->Visual->SetLinearDamping(.37f); Chair->Visual->SetAngularDamping(.61f);
 Chair->Visual->SetMassOverrideInKg(NAME_None,17.f,true); // current mechanics must survive eligibility changes
 ChairBody->SetBodyTransform(FTransform(FRotator(17,31,23),FVector(8200,5200,3200)),ETeleportType::TeleportPhysics);
 ChairBody->SetLinearVelocity(FVector(125,-40,20),false); ChairBody->SetAngularVelocityInRadians(FVector(.1,.2,.3),false);
 FElysiumSavedPhysicsBody BeforeHide; if (!Capture(Chair,BeforeHide)) return false;
 TestEqual(TEXT("core chairoffice actual first solid has two live shapes"),BeforeHide.ModelShapeFlags.Num(),2);
 void* ChairObject=Chair->VPhysicsGetObject(); const auto ChairToken=ElysiumModelPhysics::CaptureIdentity(ChairObject);
 BaseAlias->VPhysicsSetObject(ChairObject); const auto DetachedToken=ElysiumModelPhysics::CaptureIdentity(PropAlias->VPhysicsGetObject());
 PropAlias->VPhysicsSetObject(nullptr); PropAlias->VPhysicsSetObject(ChairObject); //clear first; no overwrite-warning substitute
 Chair->ScriptHide(); FElysiumSavedPhysicsBody AfterHide; if (!Capture(Chair,AfterHide)) return false;
 TestTrue(TEXT("Hide retains native incarnation, aliases and complete mechanics"),Chair->VPhysicsGetObject()==ChairObject&&BaseAlias->VPhysicsGetObject()==ChairObject&&PropAlias->VPhysicsGetObject()==ChairObject&&ElysiumModelPhysics::IsCurrentOutput(ChairToken)&&SameState(BeforeHide,AfterHide,false));
 TestEqual(TEXT("Hide removes both hulls from query and simulation eligibility"),AfterHide.ModelShapeFlags,TArray<uint8>({0,0})); CheckWords(Chair,true);
 PropAlias->ScriptHide(); PropAlias->ScriptUnhide(); TestTrue(TEXT("art gates leave former detached producer alive"),ElysiumModelPhysics::IsCurrentOutput(DetachedToken));
#if WITH_EDITOR
 auto* HiddenCarrier=Cast<UElysiumPhysicsPropComponent>(Chair->Visual); if (!HiddenCarrier) return false;
 HiddenCarrier->PreStaticMeshCompilation(); HiddenCarrier->PostStaticMeshCompilation();
 TestTrue(TEXT("actual public hidden render callbacks retain selected incarnation and current references"),ElysiumModelPhysics::IsCurrentOutput(ChairToken)&&Chair->VPhysicsGetObject()==ChairObject);
#endif
 Chair->ScriptUnhide(); FElysiumSavedPhysicsBody AfterUnhide; if (!Capture(Chair,AfterUnhide)) return false;
 TestTrue(TEXT("Unhide retains identity/current mechanics without creator reset"),Chair->VPhysicsGetObject()==ChairObject&&SameState(BeforeHide,AfterUnhide,false));
 TestEqual(TEXT("Unhide restores both hulls collision eligibility"),AfterUnhide.ModelShapeFlags,TArray<uint8>({3,3})); CheckWords(Chair,false);
 Chair->ScriptHide(); World.Activate(0.);
 const FVector LogicalPose=Chair->Origin; const FVector SolvedPose=ChairBody->GetUnrealWorldTransform().GetLocation();
 auto* Solver=ChairBody->GetPhysicsActor()->GetSolver<Chaos::FPBDRigidsSolver>(); const double SolverTime=Solver->GetSolverTime();
 World.BeginNativePhysicsFrame(.02,.02); if (!Fixture.TestWorld.TickTestWorld(.02f)) return false; World.FinishNativePhysicsFrame();
 if (!Fixture.TestWorld.TickTestWorld(.02f)) return false;
 TestTrue(TEXT("shape collision gate does not suppress awake hidden native particle motion"),Solver->GetSolverTime()>SolverTime&&!ChairBody->GetUnrealWorldTransform().GetLocation().Equals(SolvedPose,.001));
 TestTrue(TEXT("hidden MOVETYPE_NONE logical pose remains distinct from solved pose"),Chair->Origin.Equals(LogicalPose,.001));
 // A real per-hull saved output proves final reconnect does not overwrite restored flags.
 FPhysicsCommand::ExecuteWrite(ChairBody->GetPhysicsActor(),[&](const FPhysicsActorHandle&)
 {
  TArray<FPhysicsShapeHandle> Shapes; ChairBody->GetAllShapes_AssumesLocked(Shapes);
  if (Shapes.Num()==2) { FPhysicsInterface::SetIsQueryShape(Shapes[0],true); FPhysicsInterface::SetIsSimulationShape(Shapes[1],true); }
 });
 FElysiumSavedPhysicsBody SavedChair; if (!Capture(Chair,SavedChair)) return false;
 TestEqual(TEXT("actual two-hull output carries distinct saved flags"),SavedChair.ModelShapeFlags,TArray<uint8>({1,2}));
 PropAlias->ScriptHide(); BaseAlias->ScriptHide(); Retired->VPhysicsDestroyObject(); Retired->ScriptHide(); Retired->ScriptUnhide();
 TestNull(TEXT("explicit225 never resurrects on subsequent art gates"),Retired->VPhysicsGetObject());
 TestNull(TEXT("genuine core source-no-binary has no fallback physics"),Absent->VPhysicsGetObject());
 FElysiumMapSnapshot Snapshot; World.Freeze(Snapshot);
 if (!TestFalse(TEXT("eligible hidden count1 native fields admitted by Physics block"),Snapshot.bPhysicsSaveFailed)) return false;
 const int32 ChairIndex=Chair->Handle.Index,PlateIndex=Plate->Handle.Index,PropIndex=PropAlias->Handle.Index,BaseIndex=BaseAlias->Handle.Index,RetiredIndex=Retired->Handle.Index,AbsentIndex=Absent->Handle.Index;
 const auto* HiddenRow=Snapshot.PhysicsBodies.FindByPredicate([&](const FElysiumSavedPhysicsBody& State){return State.EntityIndex==ChairIndex;});
 const auto* Count0=Snapshot.PhysicsBodies.FindByPredicate([&](const FElysiumSavedPhysicsBody& State){return State.EntityIndex==RetiredIndex;});
 TestTrue(TEXT("hidden current field saves count1 and retired field saves count0"),HiddenRow&&HiddenRow->bHasObject&&Count0&&!Count0->bHasObject);
 TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true); FElysiumSaveArchive SaveAr(Writer,FElysiumSaveVersion::Latest); SaveAr<<Snapshot;
 FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes,true); FElysiumSaveArchive LoadAr(Reader,FElysiumSaveVersion::Latest); LoadAr<<Persisted;
 if (!TestFalse(TEXT("actual typed map archive decodes hidden Physics block"),Reader.IsError())) return false;
 FElysiumSavedPhysicsBody SavedPlate; if (!Capture(Plate,SavedPlate)) return false;
 World.Teardown(); TestFalse(TEXT("source teardown fences current and detached native incarnations"),ElysiumModelPhysics::IsCurrentOutput(ChairToken)||ElysiumModelPhysics::IsCurrentOutput(DetachedToken));
 auto CheckRestore=[&](FElysiumEntityWorld& RestoringWorld)
 {
  auto Resolve=[&](int32 Index){return RestoringWorld.Resolve(FElysiumEntityHandle(Index,RestoringWorld.GetEpoch()));};
  auto* Owner=Resolve(ChairIndex); auto* Sleep=Resolve(PlateIndex); auto* Base=Resolve(BaseIndex); auto* Prop=Resolve(PropIndex);
  if (!Owner||!Sleep||!Base||!Prop) return false;
  for(auto* Ent:{Owner,Base,Prop}) { FElysiumSavedPhysicsBody State; if (!Capture(Ent,State)) return false; TestTrue(TEXT("hidden own/base/PhysProp saved fields retain exact mechanics and two-hull flags before first native frame"),SameState(SavedChair,State)&&Ent->bHidden); }
  TestTrue(TEXT("type1 independently allocates each hidden current field"),Owner->VPhysicsGetObject()!=Base->VPhysicsGetObject()&&Owner->VPhysicsGetObject()!=Prop->VPhysicsGetObject()&&Base->VPhysicsGetObject()!=Prop->VPhysicsGetObject());
  FElysiumSavedPhysicsBody SleepState; if (!Capture(Sleep,SleepState)) return false;
  TestTrue(TEXT("born-hidden sleeping plate native settings/flags persist"),SameState(SavedPlate,SleepState)); CheckWords(Sleep,true);
  TestNull(TEXT("typed reload preserves explicit225 count0"),Resolve(RetiredIndex)->VPhysicsGetObject());
  TestNull(TEXT("typed reload preserves genuine no-binary count0"),Resolve(AbsentIndex)->VPhysicsGetObject());
  const auto RestoredToken=ElysiumModelPhysics::CaptureIdentity(Owner->VPhysicsGetObject()); void* Object=Owner->VPhysicsGetObject();
  Owner->ScriptUnhide(); FElysiumSavedPhysicsBody Unhidden; if (!Capture(Owner,Unhidden)) return false;
  TestTrue(TEXT("after reload Unhide keeps reconstructed identity and mechanics"),ElysiumModelPhysics::IsCurrentOutput(RestoredToken)&&SameState(SavedChair,Unhidden,false));
  TestEqual(TEXT("after reload Unhide publishes logical eligibility for both hulls"),Unhidden.ModelShapeFlags,TArray<uint8>({3,3})); CheckWords(Owner,false);
  Owner->ScriptHide(); Base->VPhysicsSetObject(nullptr); Base->VPhysicsSetObject(Object); Prop->VPhysicsSetObject(nullptr); Prop->VPhysicsSetObject(Object);
  int32 Callbacks=0;
  RestoringWorld.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
  {
   if(Event.Kind!=TEXT("physics_retire"))return; ++Callbacks;
   TestTrue(TEXT("borrowed225 callbacks see all current fields null and recipe retired before native release"),!Owner->VPhysicsGetObject()&&!Base->VPhysicsGetObject()&&!Prop->VPhysicsGetObject()&&!ElysiumModelPhysics::RecipeFor(Object)&&ElysiumModelPhysics::ComponentFor(Object)==nullptr);
  });
  Base->VPhysicsDestroyObject(); RestoringWorld.SetAiTraceSink({});
  TestEqual(TEXT("borrowed225 retirement calls all three references"),Callbacks,3);
  TestFalse(TEXT("borrowed225 fences hidden owner incarnation"),ElysiumModelPhysics::IsCurrentOutput(RestoredToken)); Owner->ScriptUnhide(); TestNull(TEXT("post225 Unhide cannot re-create native"),Owner->VPhysicsGetObject());
  Sleep->Kill(); TestNull(TEXT("actual Kill of hidden sleeping owner retires native"),Sleep->VPhysicsGetObject());
  auto* Replacement=static_cast<FElysiumPhysProp*>(Resolve(AbsentIndex)); Replacement->SetRuntimeModel(TEXT("models/scenery/misc/plates/plate5.mdl"));
  const auto ReplacementToken=ElysiumModelPhysics::CaptureIdentity(Replacement->VPhysicsGetObject()); TestTrue(TEXT("real successful SetModel creates admitted own body"),ElysiumModelPhysics::IsCurrentOutput(ReplacementToken));
  Replacement->SetRuntimeModel(TEXT("models/scenery/structural/society/chair.mdl")); TestTrue(TEXT("real source-no-binary SetModel retires without fallback"),!Replacement->VPhysicsGetObject()&&!ElysiumModelPhysics::IsCurrentOutput(ReplacementToken));
  return true;
 };
 FElysiumEntityWorld Destination(Map,nullptr,Services); Destination.Load(MoveTemp(DestinationDefs)); Persisted.PhysicsBodies.Reset();
 if (!TestTrue(TEXT("ordinary reload consumes persisted Physics block after decoded-array reset"),Destination.ApplySnapshot(Persisted)>=0)||!CheckRestore(Destination))return false;
 Destination.Teardown();
 FElysiumEntityWorld Travel(Map,nullptr,Services); Travel.Load(MoveTemp(TravelDefs));
 if (!TestTrue(TEXT("genuine transition consumes same hidden Physics bytes before activation"),Travel.ApplySnapshot(Persisted,0.,true)>=0)||!CheckRestore(Travel))return false;
 Travel.Teardown(); AddInfo(FString::Printf(TEXT("hidden-prop joined: archive_bytes=%d plate5_sleeping=1 chairoffice_shapes=2 own/base/PhysProp_fields=3 native_motion=1 reload=1 transition=1"),Bytes.Num()));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsBodyGameFlagsTest, "Elysium.Substrate.ModelPhysics.BodyGameFlags",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumModelPhysicsBodyGameFlagsTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred();
	auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
	auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
	const TArray<FString> Ids = { ElysiumModelPhysicsTest::BodyId, ElysiumModelPhysicsTest::AbsentId };
	TSet<FSoftObjectPath> Paths; FString Error;
	if (!FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error)) { AddError(Error); return false; }
	TArray<UObject*> Assets; for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
	auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
	if (!Prepared) { AddError(Error); return false; }
	ON_SCOPE_EXIT { ElysiumPreparedProps::Release(Map); };
	FElysiumWorldServices Services; Services.Embodiment = Map;
	FElysiumEntityDefs Defs; Defs.MapName = TEXT("body_game_flags");
	const TCHAR* ModelPath = TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl");
	const TCHAR* NoBodyPath = TEXT("models/scenery/structural/society/chair.mdl");
	auto AddProp = [&](const TCHAR* Name, const TCHAR* Value)
	{
		auto Row = ElysiumModelPhysicsTest::Def(ModelPath, Name);
		Row.Origin = FVector(5000 + Defs.Defs.Num() * 1000, 5000, 3000);
		if (Value) Row.Keys.Add(TEXT("dAmAgEtYpE"), Value); // real case-insensitive map-key producer
		Defs.Defs.Add(Row);
	};
	AddProp(TEXT("one"), TEXT("1")); AddProp(TEXT("default"), nullptr);
	AddProp(TEXT("zero"), TEXT("0")); AddProp(TEXT("two"), TEXT("2")); AddProp(TEXT("negative"), TEXT("-1"));
	AddProp(TEXT("prop_alias"), nullptr); AddProp(TEXT("removed"), TEXT("1"));
	auto AbsentDef = ElysiumModelPhysicsTest::Def(NoBodyPath, TEXT("absent"));
	AbsentDef.Keys.Add(TEXT("Damagetype"), TEXT("1")); Defs.Defs.Add(AbsentDef);
	FElysiumEntityDef Point; Point.Classname = TEXT("info_target"); Point.TargetName = TEXT("base_alias"); Defs.Defs.Add(Point);
	auto StaticDef = ElysiumModelPhysicsTest::Def(ModelPath, TEXT("static")); StaticDef.Classname = TEXT("prop_dynamic");
	StaticDef.Keys.Add(TEXT("solid"), TEXT("6")); StaticDef.Origin = FVector(18000, 5000, 3000); Defs.Defs.Add(StaticDef);
	FElysiumEntityDef BrushDef; BrushDef.Classname = TEXT("func_door"); BrushDef.TargetName = TEXT("brush");
	BrushDef.Model = 1; BrushDef.Keys.Add(TEXT("model"), TEXT("*1")); BrushDef.Origin = FVector(20000, 5000, 3000);
	FElysiumConvexHull Hull; for (float X : {-20.f,20.f}) for (float Y : {-20.f,20.f}) for (float Z : {-20.f,20.f}) Hull.Vertices.Add(FVector(X,Y,Z));
	BrushDef.Hulls.Add(Hull); Defs.Defs.Add(BrushDef);
	FElysiumEntityWorld World(Map, nullptr, Services); World.Load(FElysiumEntityDefs(Defs));
	auto* One = static_cast<FElysiumPhysProp*>(World.FindByName(TEXT("one")));
	auto* PropAlias = World.FindByName(TEXT("prop_alias")); auto* BaseAlias = World.FindByName(TEXT("base_alias"));
	auto* Removed = World.FindByName(TEXT("removed")); auto* Absent = World.FindByName(TEXT("absent"));
	if (!One || !PropAlias || !BaseAlias || !Removed || !Absent) return false;
	auto CheckFlags = [&](void* Object, uint16 Expected)
	{
		uint16 Actual = 0x1234;
		return TestTrue(TEXT("actual admitted native flag reader succeeds"), ElysiumModelPhysics::GetGameFlags(Object, Actual))
			&& TestEqual(TEXT("actual native semantic word"), Actual, Expected);
	};
	if (!TestEqual(TEXT("registered mixed-case authored key produces exact integer1"), One->DamageType, 1)
		|| !CheckFlags(One->VPhysicsGetObject(), 1)) return false;
	TestEqual(TEXT("Damagetype key is independent of base GetDamageType slot189"), One->GetDamageType(), 0);
	for (const TCHAR* Name : {TEXT("default"),TEXT("zero"),TEXT("two"),TEXT("negative"),TEXT("static"),TEXT("brush")})
	{
		auto* Entity = World.FindByName(Name);
		if (!TestTrue(*FString::Printf(TEXT("zero-control %s has actual current native identity and admitted recipe"),Name),
			Entity && ElysiumModelPhysics::IsCurrentOutput(ElysiumModelPhysics::CaptureIdentity(Entity->VPhysicsGetObject())) && ElysiumModelPhysics::RecipeFor(Entity->VPhysicsGetObject()))) return false;
		if (!CheckFlags(Entity->VPhysicsGetObject(), 0)) return false;
	}
	TestNull(TEXT("genuine source-no-binary type1 creates no flags/body"), Absent->VPhysicsGetObject());
	const auto* Field = FElysiumClassRegistry::Get().FindField(*One->Class, TEXT("Damagetype"));
	if (!TestTrue(TEXT("real registered integer row is keyed and saved"), Field && Field->bMapKey && Field->bSave && Field->Type == EElysiumVariantType::Int)) return false;
	uint16 Sentinel = 0x4567; int32 ForeignStorage = 0;
	for (void* Object : { static_cast<void*>(nullptr), static_cast<void*>(&ForeignStorage) })
	{
		TestFalse(TEXT("null/foreign flag read fails"), ElysiumModelPhysics::GetGameFlags(Object, Sentinel));
		TestFalse(TEXT("null/foreign setter fails without creating state"), ElysiumModelPhysics::SetGameFlags(Object, 1));
		TestFalse(TEXT("null/foreign OR fails without creating state"), ElysiumModelPhysics::AddGameFlags(Object, 1));
	}
	TestEqual(TEXT("failed reads leave caller output untouched"), Sentinel, static_cast<uint16>(0x4567));
	void* OneObject = One->VPhysicsGetObject(); const auto OneToken = ElysiumModelPhysics::CaptureIdentity(OneObject);
	auto WrongIncarnation = OneToken; ++WrongIncarnation.Incarnation;
	TestFalse(TEXT("same live actor address with wrong incarnation cannot read"),ElysiumModelPhysics::GetGameFlags(WrongIncarnation,Sentinel));
	TestFalse(TEXT("same live actor address with wrong incarnation cannot write"),ElysiumModelPhysics::SetGameFlags(WrongIncarnation,0xffff));
	TestFalse(TEXT("same live actor address with wrong incarnation cannot OR"),ElysiumModelPhysics::AddGameFlags(WrongIncarnation,0xffff));
	CheckFlags(OneObject,1);
	FElysiumSavedPhysicsBody Before, After;
	if (!ElysiumModelPhysics::Capture(OneObject, Before)) return false;
	for (const TPair<uint32,uint16>& Case : {TPair<uint32,uint16>(0,0), {1,1}, {0x8000,0x8000}, {0xffff,0xffff}, {0x10000,0}})
	{
		TestTrue(TEXT("actual object#7 setter"), ElysiumModelPhysics::SetGameFlags(OneToken, Case.Key)); CheckFlags(OneObject, Case.Value);
	}
	ElysiumModelPhysics::SetGameFlags(OneToken, 0x8000);
	ElysiumModelPhysics::AddGameFlags(OneToken, 1); ElysiumModelPhysics::AddGameFlags(OneToken, 0x10000); ElysiumModelPhysics::AddGameFlags(OneToken, 0);
	CheckFlags(OneObject, 0x8001);
	if (!ElysiumModelPhysics::Capture(OneObject, After)) return false;
	TestTrue(TEXT("flag writes preserve actual identity and all native mechanics/filter outputs"), ElysiumModelPhysics::IsCurrentOutput(OneToken)
		&& Before.Pose.Equals(After.Pose,1.e-6) && Before.LinearVelocity == After.LinearVelocity && Before.AngularVelocityRadians == After.AngularVelocityRadians
		&& Before.CurrentMassKg == After.CurrentMassKg && Before.CurrentInertia == After.CurrentInertia && Before.CurrentInverseInertia == After.CurrentInverseInertia
		&& Before.CurrentMassFrame.Equals(After.CurrentMassFrame,1.e-6) && Before.bAsleep == After.bAsleep && Before.bGravity == After.bGravity
		&& Before.CurrentLinearDamping == After.CurrentLinearDamping && Before.CurrentAngularDamping == After.CurrentAngularDamping && Before.CurrentSurface == After.CurrentSurface
		&& Before.CollisionProfile == After.CollisionProfile && Before.CollisionMask == After.CollisionMask && Before.CollisionResponses == After.CollisionResponses && Before.ModelShapeFlags == After.ModelShapeFlags);
	BaseAlias->VPhysicsSetObject(OneObject);
	const auto DetachedToken = ElysiumModelPhysics::CaptureIdentity(PropAlias->VPhysicsGetObject());
	ElysiumModelPhysics::SetGameFlags(DetachedToken, 0x4000);
	PropAlias->VPhysicsSetObject(nullptr); PropAlias->VPhysicsSetObject(OneObject);
	ElysiumModelPhysics::AddGameFlags(BaseAlias->VPhysicsGetObject(), 2); CheckFlags(PropAlias->VPhysicsGetObject(), 0x8003);
	CheckFlags(DetachedToken.Actor, 0x4000);
	BaseAlias->VPhysicsSwapObject(DetachedToken.Actor); CheckFlags(BaseAlias->VPhysicsGetObject(),0x4000); CheckFlags(OneObject,0x8003);
	AddExpectedError(TEXT("Overwriting physics object for info_target"),EAutomationExpectedErrorFlags::Contains,1);
	BaseAlias->VPhysicsSetObject(OneObject); CheckFlags(DetachedToken.Actor,0x4000); CheckFlags(OneObject,0x8003);
	Field->Set(*One, FElysiumVariant::Int(0));
	TestEqual(TEXT("real runtime registered field writer changes authored DamageType"),One->DamageType,0); CheckFlags(OneObject,0x8003);
	One->ScriptHide(); BaseAlias->ScriptHide(); PropAlias->ScriptHide(); CheckFlags(OneObject,0x8003);
	One->ScriptUnhide(); CheckFlags(OneObject,0x8003); One->ScriptHide();
	World.Activate(0.); World.BeginNativePhysicsFrame(.02,.02);
	if (!Fixture.TestWorld.TickTestWorld(.02f)) return false; World.FinishNativePhysicsFrame();
	if (!Fixture.TestWorld.TickTestWorld(.02f)) return false;
	TestTrue(TEXT("actual hidden first native frame retains incarnation"),ElysiumModelPhysics::IsCurrentOutput(OneToken)); CheckFlags(OneObject,0x8003);
	ElysiumModelPhysics::SetGameFlags(OneObject,0x8001); // deliberate current state differs from key0
	const auto RemovedToken = ElysiumModelPhysics::CaptureIdentity(Removed->VPhysicsGetObject());
	Removed->VPhysicsDestroyObject(); TestFalse(TEXT("225 retires flag word"),ElysiumModelPhysics::GetGameFlags(RemovedToken,Sentinel));
	FElysiumMapSnapshot Snapshot; World.Freeze(Snapshot);
	if (!TestFalse(TEXT("real Physics block admits current word and aliases"),Snapshot.bPhysicsSaveFailed)) return false;
	const int32 OneIndex=One->Handle.Index, BaseIndex=BaseAlias->Handle.Index, PropIndex=PropAlias->Handle.Index, RemovedIndex=Removed->Handle.Index, AbsentIndex=Absent->Handle.Index;
	for (int32 Index : {OneIndex,BaseIndex,PropIndex})
	{
		const auto* Row=Snapshot.PhysicsBodies.FindByPredicate([Index](const FElysiumSavedPhysicsBody& State){return State.EntityIndex==Index;});
		TestTrue(TEXT("Freeze reads same live native word for every current alias"),Row && Row->bHasObject && Row->CurrentGameFlags==0x8001);
	}
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true); FElysiumSaveArchive SaveAr(Writer,FElysiumSaveVersion::Latest); SaveAr << Snapshot;
	FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes,true); FElysiumSaveArchive LoadAr(Reader,FElysiumSaveVersion::Latest); LoadAr << Persisted;
	if (!TestFalse(TEXT("real typed map archive decodes flag state"),Reader.IsError())) return false;
	FElysiumSavePayload Payload; TArray<uint8> PayloadBytes;
	if (!TestTrue(TEXT("real current payload serialization"),ElysiumSave::Write(Payload,PayloadBytes,Error))) return false;
	const int32 OldVersion=51; FMemory::Memcpy(PayloadBytes.GetData()+sizeof(uint32),&OldVersion,sizeof(OldVersion));
	FElysiumSavePayload Refused;
	TestFalse(TEXT("real schema51 payload refused before archive decode"),ElysiumSave::Read(PayloadBytes,Refused,Error)); TestTrue(TEXT("schema51 refusal names floor"),Error.Contains(TEXT("floor")));
	FElysiumSavedPhysicsBody NativeRow; ElysiumModelPhysics::Capture(OneObject,NativeRow);
	TArray<uint8> RowBytes; FMemoryWriter RowWriter(RowBytes,true); RowWriter << NativeRow; RowBytes.Pop();
	FElysiumSavedPhysicsBody Truncated; FMemoryReader RowReader(RowBytes,true); RowReader << Truncated;
	TestTrue(TEXT("truncated actual native row refused"),RowReader.IsError());
	World.Teardown();
	TestFalse(TEXT("source teardown retires current and detached words"),ElysiumModelPhysics::GetGameFlags(OneToken,Sentinel)||ElysiumModelPhysics::GetGameFlags(DetachedToken,Sentinel));
	Persisted.PhysicsBodies.Reset(); // restore must consume the persisted Physics block
	auto CheckRestored = [&](FElysiumEntityWorld& Destination)
	{
		auto Resolve=[&](int32 Index){return Destination.Resolve(FElysiumEntityHandle(Index,Destination.GetEpoch()));};
		auto* Owner=static_cast<FElysiumPhysProp*>(Resolve(OneIndex)); auto* Base=Resolve(BaseIndex); auto* Prop=Resolve(PropIndex);
		if (!Owner||!Base||!Prop) return false;
		TestEqual(TEXT("saved key0 persists independently of current native word"),Owner->DamageType,0);
		for (auto* Entity : {static_cast<FElysiumEntity*>(Owner),Base,Prop})
		{ if (!CheckFlags(Entity->VPhysicsGetObject(),0x8001)) return false; TestTrue(TEXT("final hidden reconnect retains flags before first think/native frame"),Entity->IsHidden()); }
		TestTrue(TEXT("type1 independently reconstructs formerly shared fields"),Owner->VPhysicsGetObject()!=Base->VPhysicsGetObject()&&Owner->VPhysicsGetObject()!=Prop->VPhysicsGetObject()&&Base->VPhysicsGetObject()!=Prop->VPhysicsGetObject());
		ElysiumModelPhysics::AddGameFlags(Base->VPhysicsGetObject(),2); CheckFlags(Base->VPhysicsGetObject(),0x8003); CheckFlags(Owner->VPhysicsGetObject(),0x8001); CheckFlags(Prop->VPhysicsGetObject(),0x8001);
		TestNull(TEXT("count0 retired field stays absent after reload"),Resolve(RemovedIndex)->VPhysicsGetObject()); TestNull(TEXT("count0 source-no-binary stays absent"),Resolve(AbsentIndex)->VPhysicsGetObject());
		const auto OldToken=ElysiumModelPhysics::CaptureIdentity(Owner->VPhysicsGetObject());
		Owner->SetRuntimeModel(ModelPath); CheckFlags(Owner->VPhysicsGetObject(),0);
		TestFalse(TEXT("SetModel fresh incarnation fences old token even if address is reused"),ElysiumModelPhysics::SetGameFlags(OldToken,0xffff));
		TestFalse(TEXT("retired incarnation reader fails"),ElysiumModelPhysics::GetGameFlags(OldToken,Sentinel));
		Field->Set(*Owner,FElysiumVariant::Int(1)); CheckFlags(Owner->VPhysicsGetObject(),0);
		Owner->SetRuntimeModel(NoBodyPath); TestNull(TEXT("real failed replacement has no flag state"),Owner->VPhysicsGetObject());
		Owner->SetRuntimeModel(ModelPath); CheckFlags(Owner->VPhysicsGetObject(),1);
		const auto CurrentToken=ElysiumModelPhysics::CaptureIdentity(Owner->VPhysicsGetObject());
		Base->VPhysicsSetObject(nullptr); Base->VPhysicsSetObject(CurrentToken.Actor);
		int32 Observed=0; Destination.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
		{
			if (Event.Kind!=TEXT("physics_retire")) return; ++Observed;
			TestTrue(TEXT("real retirement callback sees word retired before native release"),!Owner->VPhysicsGetObject()&&!Base->VPhysicsGetObject()&&!ElysiumModelPhysics::GetGameFlags(CurrentToken,Sentinel));
		});
		Base->VPhysicsDestroyObject(); Destination.SetAiTraceSink({}); TestEqual(TEXT("borrowed225 observes both real references"),Observed,2);
		const auto KillToken=ElysiumModelPhysics::CaptureIdentity(Prop->VPhysicsGetObject()); Prop->Kill();
		TestFalse(TEXT("Kill retires independently reconstructed word"),ElysiumModelPhysics::AddGameFlags(KillToken,1));
		auto* ComponentOwner=Destination.FindByName(TEXT("two"));
		if (!ComponentOwner) return false;
		const auto ComponentToken=ElysiumModelPhysics::CaptureIdentity(ComponentOwner->VPhysicsGetObject());
		ElysiumModelPhysics::ComponentFor(ComponentToken.Actor)->DestroyComponent();
		TestFalse(TEXT("component destruction retires native word"),ElysiumModelPhysics::SetGameFlags(ComponentToken,1));
		auto* FailedRestore=Destination.FindByName(TEXT("negative"));
		const auto FailedToken=ElysiumModelPhysics::CaptureIdentity(FailedRestore->VPhysicsGetObject());
		FElysiumSavedPhysicsBody FailedState;
		if (!ElysiumModelPhysics::Capture(FailedToken.Actor,FailedState)) return false;
		const auto* NoBodyVisual=static_cast<FElysiumPhysProp*>(Resolve(AbsentIndex))->Visual;
		FailedState.Recipe.ModelAsset=NoBodyVisual->GetStaticMesh()->GetPathName(); //actual admitted no-source-body asset
		TestFalse(TEXT("real failed selected-model restore rejects reconstruction"),ElysiumModelPhysics::Restore(ElysiumModelPhysics::ComponentFor(FailedToken.Actor),Destination.PhysicsSurfaces(),FailedState,Error));
		TestTrue(TEXT("failed restore leaves no field or old semantic word"),!FailedRestore->VPhysicsGetObject()&&!ElysiumModelPhysics::GetGameFlags(FailedToken,Sentinel));
		return true;
	};
	FElysiumEntityWorld Reload(Map,nullptr,Services); Reload.Load(FElysiumEntityDefs(Defs));
	if (!TestTrue(TEXT("ordinary reload uses persisted native Physics bytes"),Reload.ApplySnapshot(Persisted)>=0)||!CheckRestored(Reload)) return false;
	Reload.Teardown();
	FElysiumEntityWorld Travel(Map,nullptr,Services); Travel.Load(FElysiumEntityDefs(Defs));
	if (!TestTrue(TEXT("actual transition applies current flag bytes before activation"),Travel.ApplySnapshot(Persisted,0.,true)>=0)||!CheckRestored(Travel)) return false;
	Travel.Teardown();
	AddInfo(FString::Printf(TEXT("body-game-flags: registered_key1=1 key0_current8001=1 native_width16=1 aliases=1 archive_bytes=%d reload=1 transition=1 retirement=1 impact_I2_open=1"),Bytes.Num()));
	return true;
}
namespace ElysiumRemovalTest
{
	TArray<int32> Released;
	TMap<FString,int32> DestructorCleanupCalls;
	class FDestructorProbe final : public FElysiumNpc
	{
	public:
		void Spawn() override {} // only the existing destructor cleanup recipient is exercised.
		void UpdateOnRemove() override {} // legitimate higher override omits base removal.
		void Slot601(FElysiumEntity*) override { ++DestructorCleanupCalls.FindOrAdd(Def->TargetName); }
	};
	TUniquePtr<FElysiumEntity> MakeDestructorProbe() { return MakeUnique<FDestructorProbe>(); }
	FElysiumClassRegistrar DestructorRegistrar(TEXT("elysium_test_destructor_remove"), ElysiumBaseClassName(), &MakeDestructorProbe, [](FElysiumClassDesc&) {});
	class FRetainingOverride final : public FElysiumEntity
	{
	public:
		int32 RemovalCalls = 0;
		void UpdateOnRemove() override { ++RemovalCalls; QueueRemove(); } // legitimate override omits base225.
		void OnOwnedEntityTerminated(FElysiumEntity& Removed, EElysiumOwnedEntityTermination) override
		{
			Released.Add(Removed.Handle.Index);
			if (FElysiumEntity* Tail = World->FindByName(TEXT("tail"))) Tail->QueueRemove();
		}
	};
	TUniquePtr<FElysiumEntity> MakeRetainingOverride() { return MakeUnique<FRetainingOverride>(); }
	FElysiumClassRegistrar Registrar(TEXT("elysium_test_retaining_remove"), ElysiumBaseClassName(), &MakeRetainingOverride, [](FElysiumClassDesc&) {});
	class FSpawnProbe final : public FElysiumEntity
	{
	public:
		int32 PostCalls=0, ActivateCalls=0;
		void Spawn() override
		{
			if(Def->Keys.Contains(TEXT("bare")))SetEFlags(EFlagsWord()|1);
			else if(Def->Keys.Contains(TEXT("queued")))QueueRemove();
			else if(Def->Keys.Contains(TEXT("stale")))FElysiumEntity::Kill();
		}
		void PostSpawn() override { ++PostCalls; }
		void Activate() override { ++ActivateCalls; }
	};
	TUniquePtr<FElysiumEntity> MakeSpawnProbe() { return MakeUnique<FSpawnProbe>(); }
	FElysiumClassRegistrar SpawnRegistrar(TEXT("elysium_test_removal_spawn"), ElysiumBaseClassName(), &MakeSpawnProbe, [](FElysiumClassDesc&) {});
	FElysiumEntityDef Brush(const TCHAR* Name)
	{
		FElysiumEntityDef Row; Row.Classname = TEXT("func_door"); Row.TargetName = Name;
		Row.Model = 1; Row.Keys.Add(TEXT("model"), TEXT("*1")); Row.Origin = FVector(5000,5000,3000);
		FElysiumConvexHull Hull; for(float X:{-20.f,20.f})for(float Y:{-20.f,20.f})for(float Z:{-20.f,20.f})Hull.Vertices.Add(FVector(X,Y,Z));
		Row.Hulls.Add(Hull); return Row;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPendingRemovalTest, "Elysium.Substrate.ModelPhysics.PendingRemoval",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumPendingRemovalTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture; if (!Fixture.CreateWorld(*this)) return false;
	auto* Map = Fixture.SpawnMapActorDeferred(); FElysiumWorldServices Services; Services.Embodiment = Map;
	FElysiumEntityDefs Defs; Defs.MapName = TEXT("pending_removal");
	Defs.Defs.Add(ElysiumRemovalTest::Brush(TEXT("base"))); Defs.Defs.Add(ElysiumRemovalTest::Brush(TEXT("producer")));
	FElysiumEntityDef Override; Override.Classname = TEXT("elysium_test_retaining_remove"); Override.TargetName = TEXT("retaining"); Defs.Defs.Add(Override);
	Override.TargetName=TEXT("release_observer"); Defs.Defs.Add(Override);
	FElysiumEntityDef TailDef; TailDef.Classname=TEXT("info_target"); TailDef.TargetName=TEXT("tail"); Defs.Defs.Add(TailDef);
	FElysiumEntityWorld World(Map,nullptr,Services); World.Load(FElysiumEntityDefs(Defs));
	auto* Base=World.FindByName(TEXT("base")); auto* Producer=World.FindByName(TEXT("producer"));
	auto* Retaining=static_cast<ElysiumRemovalTest::FRetainingOverride*>(World.FindByName(TEXT("retaining")));
	if (!Base||!Producer||!Retaining||!Base->VPhysicsGetObject()||!Producer->VPhysicsGetObject()) return false;
	const auto BaseHandle=Base->Handle, RetainingHandle=Retaining->Handle;
	auto* Observer=World.FindByName(TEXT("release_observer")); auto* Tail=World.FindByName(TEXT("tail"));
	if (!Observer||!Tail) return false;
	const auto TailHandle=Tail->Handle;
	for(auto* Owned:{Base,static_cast<FElysiumEntity*>(Retaining),Tail})Owned->OwnerEntity=Observer->Handle;
	ElysiumRemovalTest::Released.Reset();
	const auto BaseToken=ElysiumModelPhysics::CaptureIdentity(Base->VPhysicsGetObject());
	const auto RetainingToken=ElysiumModelPhysics::CaptureIdentity(Producer->VPhysicsGetObject());
	Retaining->VPhysicsSetObject(RetainingToken.Actor);
	int32 CallbackCount=0;
	World.RemoveExternalActs=[&](FElysiumEntity& Removing)
	{
		if (&Removing!=Base) return;
		++CallbackCount;
		TestTrue(TEXT("180 observes latched registered owner before native225"),Removing.IsMarkedForDeletion() && World.Resolve(BaseHandle)==Base && ElysiumModelPhysics::IsCurrentOutput(BaseToken));
		TestEqual(TEXT("callback precedes name clear and queue append"),Removing.TargetName,FString(TEXT("base")));
		TestEqual(TEXT("queue is not yet appended during180"),World.PendingEntityRemovalCount(),0);
		Removing.QueueRemove();
	};
	Base->QueueRemove(); Base->QueueRemove();
	TestEqual(TEXT("reentry and repeat removal invoke180 once"),CallbackCount,1);
	TestTrue(TEXT("base180 retires native but retains registered pending identity"),World.Resolve(BaseHandle)==Base && !Base->IsDead() && !ElysiumModelPhysics::IsCurrentOutput(BaseToken));
	TestTrue(TEXT("base name clears before queue append"),Base->TargetName.IsEmpty() && World.PendingEntityRemovalCount()==1);
	Retaining->QueueRemove();
	TestEqual(TEXT("override180 reentry invokes it once"),Retaining->RemovalCalls,1);
	TestTrue(TEXT("legitimate override retains native current alias until release"),World.Resolve(RetainingHandle)==Retaining && ElysiumModelPhysics::IsCurrentOutput(RetainingToken));
	FElysiumMapSnapshot Snapshot; World.Freeze(Snapshot);
	const auto* PendingState=Snapshot.Entities.FindByPredicate([&](const FElysiumEntityState& Row){return Row.Index==RetainingHandle.Index;});
	if (!TestTrue(TEXT("native capture admits registered pending owner/latch/name"),PendingState && !PendingState->bDead && (PendingState->EFlags&1) && PendingState->TargetName.IsEmpty())) return false;
	const auto* PendingBody=Snapshot.PhysicsBodies.FindByPredicate([&](const FElysiumSavedPhysicsBody& Row){return Row.EntityIndex==RetainingHandle.Index;});
	if (!TestTrue(TEXT("native pending capture preserves actual current body"),PendingBody && PendingBody->bHasObject)) return false;
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true); FElysiumSaveArchive Saving(Writer,FElysiumSaveVersion::Latest); Saving<<Snapshot;
	FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes,true); FElysiumSaveArchive Loading(Reader,FElysiumSaveVersion::Latest); Loading<<Persisted;
	if (Reader.IsError()) return false;
	World.PurgeEntityRemovals();
	TestTrue(TEXT("release callback appends tail and live count drains it in the same insertion-order pass"),ElysiumRemovalTest::Released==TArray<int32>({BaseHandle.Index,RetainingHandle.Index,TailHandle.Index}) && !World.Resolve(TailHandle));
	TestTrue(TEXT("release invalidates pending handles and retained native alias"),!World.Resolve(BaseHandle)&&!World.Resolve(RetainingHandle)&&!ElysiumModelPhysics::IsCurrentOutput(RetainingToken));
	TestEqual(TEXT("purge clears queue after live-count walk"),World.PendingEntityRemovalCount(),0);
	World.Teardown(); Persisted.PhysicsBodies.Reset();
	FElysiumEntityWorld Reload(Map,nullptr,Services); Reload.Load(FElysiumEntityDefs(Defs));
	if (!TestTrue(TEXT("typed current native restore"),Reload.ApplySnapshot(Persisted)>=0)) return false;
	auto* Restored=Reload.Resolve(FElysiumEntityHandle(RetainingHandle.Index,Reload.GetEpoch()));
	if (!TestTrue(TEXT("restore preserves pending latch without released tombstone"),Restored && Restored->IsMarkedForDeletion() && !Restored->IsDead())) return false;
	TestTrue(TEXT("restore has no synthetic callback or queue replay"),Reload.PendingEntityRemovalCount()==0 && Restored->TargetName.IsEmpty() && Restored->VPhysicsGetObject());
	Reload.PurgeEntityRemovals(); TestTrue(TEXT("unqueued saved latch survives unrelated empty purge"),Reload.Resolve(Restored->Handle)==Restored);
	Reload.Teardown(); AddInfo(FString::Printf(TEXT("pending-removal native: base180225=1 override180retains=1 latch-before-callback=1 name-before-append=1 typed_bytes=%d unqueued_restore=1"),Bytes.Num())); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBrushFailureProbeTest, "Elysium.Substrate.ModelPhysics.BrushFailureProbe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumBrushFailureProbeTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture; if (!Fixture.CreateWorld(*this)) return false;
	auto* Owner=Fixture.World->SpawnActor<AActor>(); auto* Brush=NewObject<UElysiumBrushComponent>(Owner);
	Brush->RegisterComponent(); Owner->AddInstanceComponent(Brush);
	FElysiumPhysicsSurfaceProps Surfaces; Surfaces.ParseSurfaceData(TEXT("probe"),TEXT("default { friction 0.8 elasticity 0 density 1000 }"));
	FElysiumPhysicsBodyRecipe Recipe; Recipe.Kind=EElysiumPhysicsRecipeKind::Brush;
	Recipe.BrushHulls.Add(ElysiumRemovalTest::Brush(TEXT("cube")).Hulls[0].Vertices);
	bool Attempted=false; FString Error;
	if (!TestTrue(TEXT("positive admitted real native brush creation"),ElysiumModelPhysics::CreateBrush(Brush,Surfaces,Recipe,Error,&Attempted)) || !Attempted) return false;
	Recipe.BrushHulls[0]={FVector(0,0,0),FVector(10,0,0),FVector(20,0,0),FVector(30,0,0)};
	const bool Created=ElysiumModelPhysics::CreateBrush(Brush,Surfaces,Recipe,Error,&Attempted);
	AddInfo(FString::Printf(TEXT("brush-failure candidate: finite_collinear4=1 native_attempted=%d created=%d actor=%d error=%s"),Attempted,Created,Brush->BodyInstance.IsValidBodyInstance(),*Error));
	ElysiumModelPhysics::Destroy(Brush); Brush->DestroyComponent(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRemovalSpawnTest, "Elysium.Substrate.ModelPhysics.RemovalSpawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumRemovalSpawnTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture; if(!Fixture.CreateWorld(*this))return false;
	auto* Map=Fixture.SpawnMapActorDeferred(); FElysiumEntityWorld World(Map,nullptr);
	FElysiumEntityDefs Defs; Defs.MapName=TEXT("removal_spawn"); World.Load(MoveTemp(Defs)); World.Activate(0.);
	FElysiumEntityDef OtherDef; OtherDef.Classname=TEXT("info_target"); OtherDef.TargetName=TEXT("unrelated");
	const auto Other=World.CreateRuntimeEntityNoSpawn(MoveTemp(OtherDef)); World.Resolve(Other)->QueueRemove();
	for(const TCHAR* Kind:{TEXT("bare"),TEXT("queued"),TEXT("stale")})
	{
		FElysiumEntityDef Def; Def.Classname=TEXT("elysium_test_removal_spawn"); Def.TargetName=Kind; Def.Keys.Add(Kind,TEXT("1"));
		const auto Handle=World.CreateRuntimeEntityNoSpawn(MoveTemp(Def)); auto* Probe=static_cast<ElysiumRemovalTest::FSpawnProbe*>(World.Resolve(Handle));
		World.CallEntitySpawn(*Probe);
		TestTrue(TEXT("negative101d1280 skips PostSpawn and immediate runtime activation"),Probe->PostCalls==0&&Probe->ActivateCalls==0);
		if(FString(Kind)==TEXT("bare"))
			TestTrue(TEXT("bare pending116 rejects Spawn without synthetic queue; unrelated queue drains"),World.Resolve(Handle)==Probe&&Probe->IsMarkedForDeletion()&&!Probe->IsDead()&&World.PendingEntityRemovalCount()==0&&!World.Resolve(Other));
		else TestNull(TEXT("queued and stale identity negativeSpawn owners no longer resolve"),World.Resolve(Handle));
	}
	World.Teardown(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBrushCreatorFailureTest, "Elysium.Substrate.ModelPhysics.BrushCreatorFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumBrushCreatorFailureTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture; if(!Fixture.CreateWorld(*this))return false;
	auto* Map=Fixture.SpawnMapActorDeferred(); FElysiumWorldServices Services; Services.Embodiment=Map;
	FElysiumEntityDefs Core;
	if(!TestTrue(TEXT("actual deployed core entity producer loads"),ElysiumEntityDefSource::Load(TEXT("sp_tutorial_1"),Core)!=EElysiumEntityDefSource::None))return false;
	const auto* CoreDoor=Core.Defs.FindByPredicate([](const FElysiumEntityDef& Row){return Row.Classname==TEXT("func_door")&&Row.Model>0&&!Row.Hulls.IsEmpty();});
	if(!TestNotNull(TEXT("authentic core door has authored collision admission"),CoreDoor))return false;
	FElysiumEntityDefs Positive; Positive.MapName=TEXT("sp_tutorial_1"); Positive.Defs.Add(*CoreDoor);
	FElysiumEntityWorld CoreWorld(Map,nullptr,Services); CoreWorld.Load(MoveTemp(Positive));
	auto* Door=CoreWorld.Resolve(FElysiumEntityHandle(0,CoreWorld.GetEpoch()));
	if(!TestTrue(TEXT("authentic core door real native/controller success"),Door&&Door->VPhysicsGetObject()&&ElysiumModelPhysics::ShadowFor(Door->VPhysicsGetObject())&&CoreWorld.PhysicsShadows().Contains(Door->Handle)))return false;
	const auto Initial=ElysiumModelPhysics::CaptureIdentity(Door->VPhysicsGetObject());
	Door->bEdictAttached=false; TestFalse(TEXT("missing edict rejects Setup"),Door->InitBrushPhysicsShadow());
	TestTrue(TEXT("missing edict preserves old native field"),ElysiumModelPhysics::IsCurrentOutput(Initial));
	Door->bEdictAttached=true; Door->SetSolid(0); TestFalse(TEXT("admitted solid0 stops after Setup"),Door->InitBrushPhysicsShadow());
	TestTrue(TEXT("solid0 retires old field before return without removal"),!ElysiumModelPhysics::IsCurrentOutput(Initial)&&!Door->IsMarkedForDeletion());
	AddInfo(FString::Printf(TEXT("authentic-core-door: map=sp_tutorial_1 name=%s model=%d hulls=%d native_shadow=1"),*CoreDoor->TargetName,CoreDoor->Model,CoreDoor->Hulls.Num()));
	CoreWorld.Teardown();
	ElysiumRetailConVars::DeveloperDiagnosticsEnabled();
	IConsoleManager& Console=IConsoleManager::Get(); IConsoleVariable* Developer=Console.FindConsoleVariable(TEXT("developer"));
	if(!Developer)return false;
	const int32 PreviousLevel=Developer->GetInt();
	ON_SCOPE_EXIT { if(IConsoleVariable* Current=Console.FindConsoleVariable(TEXT("developer")))Current->Set(PreviousLevel,ECVF_SetByCode); };
	struct FCase { const TCHAR* MapName; int32 Level; bool Match; bool Studio; };
	const FCase Cases[]={{TEXT("ordinary"),0,false,false},{TEXT("ordinary"),2,false,false},{TEXT("SM_OCEAN"),1,true,false},{TEXT("sm_oceanhouse_2"),-1,true,false},{TEXT("sm_oceanhouse_2_extra"),0,false,false},{TEXT(""),0,false,false},{TEXT("studio_negative"),1,false,true}};
	for(const FCase& Case:Cases)
	{
		Developer->Set(Case.Level,ECVF_SetByCode);
		FElysiumEntityDefs Defs; Defs.MapName=Case.MapName; Defs.Defs.Add(ElysiumRemovalTest::Brush(TEXT("failed")));
		FElysiumEntityWorld World(Map,nullptr,Services); World.Load(MoveTemp(Defs));
		auto* Entity=World.FindByName(TEXT("failed")); if(!Entity||!Entity->VPhysicsGetObject())return false;
		const auto Identity=Entity->Handle; const auto Previous=ElysiumModelPhysics::CaptureIdentity(Entity->VPhysicsGetObject());
		// Explicitly malformed synthetic authored fixture; no stock-map corruption claim.
		const_cast<FElysiumEntityDef*>(Entity->Def)->Hulls[0].Vertices={FVector(0,0,0),FVector(10,0,0),FVector(20,0,0),FVector(30,0,0)};
		if(Case.Studio)Entity->SetRuntimeModel(TEXT("models/scenery/structural/society/chair.mdl"));
		int32 Diagnostics=0, Failures=0;
		World.SetAiTraceSink([&](const FElysiumAiTraceEvent& Event)
		{
			if(Event.Text.Contains(TEXT("brush_shadow_diagnostic")))++Diagnostics;
			if(Event.Text.Contains(TEXT("brush_shadow_failure")))++Failures;
		});
		AddExpectedError(TEXT("brush shadow creation: Chaos body creation failed"),EAutomationExpectedErrorFlags::Contains,1);
		TestFalse(TEXT("proven admitted native-null shared creator"),Entity->InitBrushPhysicsShadow());
		TestTrue(TEXT("Setup225 retires previous incarnation before actual creation failure"),!ElysiumModelPhysics::IsCurrentOutput(Previous));
		TestEqual(TEXT("type1 current developer controls diagnostic only"),Diagnostics,(!Case.Studio&&Case.Level>0)?1:0);
		TestEqual(TEXT("model classification exactly1 admits failure arm"),Failures,Case.Studio?0:1);
		const bool Remove=!Case.Studio&&!Case.Match;
		TestTrue(TEXT("literal/current map direction, empty and studio controls match removal"),Entity->IsMarkedForDeletion()==Remove && World.PendingEntityRemovalCount()==(Remove?1:0));
		TestTrue(TEXT("shared creator defers handle release while name follows removal"),World.Resolve(Identity)==Entity && Entity->TargetName.IsEmpty()==Remove);
		World.PurgeEntityRemovals(); TestTrue(TEXT("actual purge invalidates only queued owner"),(World.Resolve(Identity)==nullptr)==Remove);
		World.SetAiTraceSink(nullptr); World.Teardown();
	}
	// Public current registry object predicate: an actual command never enables diagnostics.
	Console.UnregisterConsoleObject(TEXT("developer"),false);
	IConsoleCommand* Command=Console.RegisterConsoleCommand(TEXT("developer"),TEXT("creator command negative control"),FConsoleCommandDelegate::CreateLambda([]{}));
	TestFalse(TEXT("actual developer command fails !IsCommand predicate"),ElysiumRetailConVars::DeveloperDiagnosticsEnabled());
	Console.UnregisterConsoleObject(Command,false); Developer=Console.RegisterConsoleVariable(TEXT("developer"),PreviousLevel,TEXT("Retail developer diagnostics level."));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNativePoseCacheRestoreTest, "Elysium.Substrate.ModelPhysics.NativePoseCacheRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumNativePoseCacheRestoreTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture; if(!Fixture.CreateWorld(*this))return false;
	auto* Map=Fixture.SpawnMapActorDeferred(); FElysiumWorldServices Services; Services.Embodiment=Map;
	FElysiumEntityDefs Defs; Defs.MapName=TEXT("native_pose_cache_restore");
	auto ParentDef=ElysiumRemovalTest::Brush(TEXT("cache_parent")); ParentDef.Keys.Add(TEXT("angles"),TEXT("10 370 20")); Defs.Defs.Add(ParentDef);
	for(const TCHAR* Name:{TEXT("cache_clean"),TEXT("cache_dirty")})
	{
		auto ChildDef=ElysiumRemovalTest::Brush(Name); ChildDef.Origin+=FVector(120,30,50);
		ChildDef.Keys.Add(TEXT("parentname"),TEXT("cache_parent")); ChildDef.Keys.Add(TEXT("angles"),TEXT("15 725 35")); Defs.Defs.Add(ChildDef);
	}
	FElysiumEntityWorld Source(Map,nullptr,Services); Source.Load(FElysiumEntityDefs(Defs));
	auto* Clean=Source.FindByName(TEXT("cache_clean")); auto* Dirty=Source.FindByName(TEXT("cache_dirty"));
	if(!TestTrue(TEXT("cache controls use real parented native shadows"),Clean&&Dirty&&Clean->VPhysicsGetObject()&&Dirty->VPhysicsGetObject()&&Clean->bParentLocalPose&&Dirty->bParentLocalPose))return false;
	Clean->GetAbsOrigin(); Clean->GetAbsAngles(); Dirty->GetAbsOrigin(); Dirty->GetAbsAngles();
	Dirty->SetOrigin(Dirty->GetOrigin()+FVector(11,23,37)); Dirty->SetAngles(FRotator(23,745,47));
	Clean->SetEFlags(Clean->EFlagsWord()&~0x1c000u); Dirty->SetEFlags(Dirty->EFlagsWord()&~0x1c000u); // explicit saved clean bounds/partition control.
	const uint32 CleanFlags=Clean->EFlagsWord(), DirtyFlags=Dirty->EFlagsWord();
	const FVector CleanAbsolute=Clean->Origin, CleanAngles=Clean->Angles, DirtyAbsolute=Dirty->Origin, DirtyAngles=Dirty->Angles;
	const FVector DirtyLocal=Dirty->GetOrigin(), DirtyLocalAngles=Dirty->GetAngles();
	FVector ExpectedDirtyOrigin,ExpectedDirtyAngles; Dirty->ComputeAbsolutePose(ExpectedDirtyOrigin,ExpectedDirtyAngles);
	float CleanFrame[12],DirtyFrame[12]; for(int32 Index=0;Index<12;++Index){CleanFrame[Index]=Clean->CoordinateFrame[Index];DirtyFrame[Index]=Dirty->CoordinateFrame[Index];}
	TestTrue(TEXT("control saved words distinguish clean and dirty absolute caches"),!(CleanFlags&0x800u)&&(DirtyFlags&0x800u)&&!DirtyAbsolute.Equals(ExpectedDirtyOrigin,.001));
	const int32 CleanIndex=Clean->Handle.Index,DirtyIndex=Dirty->Handle.Index;
	FElysiumMapSnapshot Snapshot; Source.Freeze(Snapshot);
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true); FElysiumSaveArchive Saving(Writer,FElysiumSaveVersion::Latest); Saving<<Snapshot;
	FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes,true); FElysiumSaveArchive Loading(Reader,FElysiumSaveVersion::Latest); Loading<<Persisted;
	if(!TestFalse(TEXT("typed native pose-cache bytes decode"),Writer.IsError()||Reader.IsError()))return false;
	Source.Teardown(); Persisted.PhysicsBodies.Reset();
	FElysiumEntityWorld Destination(Map,nullptr,Services); Destination.Load(FElysiumEntityDefs(Defs));
	bool Observed=false;
	Destination.OnSnapshotDecoded=[&]()
	{
		Observed=true; Clean=Destination.Resolve(FElysiumEntityHandle(CleanIndex,Destination.GetEpoch())); Dirty=Destination.Resolve(FElysiumEntityHandle(DirtyIndex,Destination.GetEpoch()));
		if(!Clean||!Dirty){AddError(TEXT("cache identities missing before OnRestore"));return;}
		TestEqual(TEXT("clean saved cache survives actual restore bounds/partition invalidation"),Clean->EFlagsWord(),(CleanFlags&~0x8000u)|0x14000u|(Clean->EdictIndex()!=0?0x8000u:0u));
		TestEqual(TEXT("dirty saved cache survives actual restore bounds/partition invalidation"),Dirty->EFlagsWord(),(DirtyFlags&~0x8000u)|0x14000u|(Dirty->EdictIndex()!=0?0x8000u:0u));
		TestTrue(TEXT("raw clean and stale dirty absolute caches restore before any getter"),Clean->Origin==CleanAbsolute&&Clean->Angles==CleanAngles&&Dirty->Origin==DirtyAbsolute&&Dirty->Angles==DirtyAngles);
		for(int32 Index=0;Index<12;++Index){TestEqual(TEXT("clean saved affine frame word"),Clean->CoordinateFrame[Index],CleanFrame[Index]);TestEqual(TEXT("dirty saved affine frame word"),Dirty->CoordinateFrame[Index],DirtyFrame[Index]);}
		TestTrue(TEXT("dirty local inputs and full Euler restore independently"),Dirty->GetOrigin()==DirtyLocal&&Dirty->GetAngles()==DirtyLocalAngles);
		TestTrue(TEXT("clean absolute readers preserve saved cache"),Clean->GetAbsOrigin()==CleanAbsolute&&Clean->GetAbsAngles()==CleanAngles&&!(Clean->EFlagsWord()&0x800u));
		TestTrue(TEXT("dirty real absolute reader recomputes inherited pose and clears800"),Dirty->GetAbsOrigin().Equals(ExpectedDirtyOrigin,.002)&&Dirty->GetAbsAngles().Equals(ExpectedDirtyAngles,.001)&&!(Dirty->EFlagsWord()&0x800u));
	};
	TestTrue(TEXT("real native cache restore completes"),Destination.ApplySnapshot(Persisted)>=0&&Observed);
	Destination.OnSnapshotDecoded=nullptr; Destination.Teardown();
	AddInfo(FString::Printf(TEXT("native-cache restore: clean=1 dirty=1 raw_frame12=1 full_euler=1 lazy_reader=1 typed_bytes=%d"),Bytes.Num()));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRemovalResourceLifetimeTest, "Elysium.Substrate.ModelPhysics.RemovalResourceLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumRemovalResourceLifetimeTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture; if(!Fixture.CreateWorld(*this))return false;
	Fixture.SpawnPlayerControllerAndPawn(FVector(-1000,0,1000)); Fixture.PlayerController->Possess(Fixture.Pawn);
	auto* Map=Fixture.SpawnMapActorDeferred(); Map->bStageOnly=true; Map->bStageWithoutCatalogue=true; Map->FinishSpawning(FTransform::Identity);
	for(int32 Frame=0;Frame<20&&!Map->IsRuntimeActive();++Frame)Fixture.TestWorld.TickTestWorld(.02f);
	if(!TestTrue(TEXT("resource witness has real active map/light rig host"),Map->IsRuntimeActive()))return false;
	FString Error;
	FElysiumEntityDefs Defs; Defs.MapName=TEXT("removal_resource_lifetime");
	for(const TCHAR* Name:{TEXT("detached"),TEXT("replaced")})
	{
		auto Row=ElysiumModelPhysicsTest::Def(TEXT("models/scenery/vehicles/OmniShpere/CopCar.mdl"),Name);
		Row.Classname=TEXT("prop_dynamic"); Row.Origin=FVector(5000,5000,3000);Row.Keys.Add(TEXT("solid"),TEXT("6"));Row.Keys.Add(TEXT("spawnflags"),TEXT("0"));Defs.Defs.Add(Row);
	}
	Defs.Defs.Add(ElysiumModelPhysicsTest::Def(TEXT("models/scenery/street/trashcan1/trashcan1_base.mdl"),TEXT("replacement")));
	FElysiumEntityDef LightDef;LightDef.Classname=TEXT("light_dynamic");LightDef.TargetName=TEXT("light");Defs.Defs.Add(LightDef);
	for(const TCHAR* Name:{TEXT("immediate_npc"),TEXT("queued_npc")}){FElysiumEntityDef NpcDef;NpcDef.Classname=TEXT("elysium_test_destructor_remove");NpcDef.TargetName=Name;Defs.Defs.Add(NpcDef);}
	FElysiumStageSeat Seat;Seat.FeetCm=FVector(-1000,0,1000);
	if(!Map->RebuildStageWorld(MoveTemp(Defs),{},Seat,Error)){AddError(Error);return false;}
	for(int32 Frame=0;Frame<20&&!Map->IsRuntimeActive();++Frame)Fixture.TestWorld.TickTestWorld(.02f);
	if(!Map->IsRuntimeActive()||!Map->GetEntityWorld())return false;
	FElysiumEntityWorld& World=*Map->GetEntityWorld();
	auto* Detached=World.FindByName(TEXT("detached"));auto* Replaced=World.FindByName(TEXT("replaced"));auto* Replacement=World.FindByName(TEXT("replacement"));auto* Light=World.FindByName(TEXT("light"));
	if(!TestTrue(TEXT("real static owned and replacement native producers"),Detached&&Replaced&&Replacement&&Detached->VPhysicsGetObject()&&Replaced->VPhysicsGetObject()&&Replacement->VPhysicsGetObject()))return false;
	const auto DetachedToken=ElysiumModelPhysics::CaptureIdentity(Detached->VPhysicsGetObject()),ReplacedToken=ElysiumModelPhysics::CaptureIdentity(Replaced->VPhysicsGetObject()),ReplacementToken=ElysiumModelPhysics::CaptureIdentity(Replacement->VPhysicsGetObject());
	USceneComponent* LightComponent=Light?Light->GetAttachChild():nullptr;
	if(!TestTrue(TEXT("real dynamic light component exists"),LightComponent&&LightComponent->IsRegistered()))return false;
	Detached->VPhysicsSetObject(nullptr);Replaced->VPhysicsSetObject(ReplacementToken.Actor);
	const auto DetachedHandle=Detached->Handle,ReplacedHandle=Replaced->Handle,LightHandle=Light->Handle;
	Detached->QueueRemove();Replaced->QueueRemove();Light->QueueRemove();
	TestTrue(TEXT("queue-time base225 retires current replacement but retains both detached owned carriers"),!ElysiumModelPhysics::IsCurrentOutput(ReplacementToken)&&ElysiumModelPhysics::IsCurrentOutput(DetachedToken)&&ElysiumModelPhysics::IsCurrentOutput(ReplacedToken));
	TestTrue(TEXT("owned native game data and dynamic light survive until release"),ElysiumModelPhysics::GameDataForObject(DetachedToken.Actor)==Detached&&ElysiumModelPhysics::GameDataForObject(ReplacedToken.Actor)==Replaced&&LightComponent->IsRegistered()&&World.Resolve(LightHandle)==Light);
	World.PurgeEntityRemovals();
	TestTrue(TEXT("derived release retires detached/replaced owned resources and native game data"),!ElysiumModelPhysics::IsCurrentOutput(DetachedToken)&&!ElysiumModelPhysics::IsCurrentOutput(ReplacedToken)&&!ElysiumModelPhysics::GameDataForObject(DetachedToken.Actor)&&!ElysiumModelPhysics::GameDataForObject(ReplacedToken.Actor));
	TestTrue(TEXT("dynamic light release destroys native component and invalidates all owner handles"),!LightComponent->IsRegistered()&&!World.Resolve(DetachedHandle)&&!World.Resolve(ReplacedHandle)&&!World.Resolve(LightHandle));
	auto* Immediate=World.FindByName(TEXT("immediate_npc"));auto* Queued=World.FindByName(TEXT("queued_npc"));
	if(!Immediate||!Queued)return false;
	ElysiumRemovalTest::DestructorCleanupCalls.Reset();Immediate->AsNpc()->AttackCoordinator=1;Queued->AsNpc()->AttackCoordinator=1;
	Immediate->FElysiumEntity::Kill();
	TestFalse(TEXT("old immediate Kill is not proof of destructor cleanup"),Immediate->bNpcDestructorCleanupDispatched);
	Queued->QueueRemove();World.PurgeEntityRemovals();
	TestTrue(TEXT("queued release invokes existing Troika destructor recipient once"),Queued->bNpcDestructorCleanupDispatched&&ElysiumRemovalTest::DestructorCleanupCalls.FindRef(TEXT("queued_npc"))==1);
	World.Teardown();
	TestTrue(TEXT("teardown dispatches immediate-Kill cleanup once and does not redispatch queued release"),ElysiumRemovalTest::DestructorCleanupCalls.FindRef(TEXT("immediate_npc"))==1&&ElysiumRemovalTest::DestructorCleanupCalls.FindRef(TEXT("queued_npc"))==1);
	return true;
}


// Exact remaining core2 prepared addresses. Retail/source/provenance receipt:
// l0-p1-remaining-inputs-20261012/current-core-inputs.json;8 true no-body controls.
namespace ElysiumCoreRemainingInputsTest
{
 struct FWitness { const TCHAR* Id; const TCHAR* Path; int32 Hulls; };
 const FWitness Witnesses[] = {
		{ TEXT("vtmb:model:character/monster/wolf_form/wolf_form"), TEXT("/ElysiumBaked/Models/character/monster/wolf_form/SM_wolf_form.SM_wolf_form"), 0 },
		{ TEXT("vtmb:model:scenery/furniture/coffemaker/coffemaker"), TEXT("/ElysiumBaked/Models/scenery/furniture/coffemaker/SM_coffemaker.SM_coffemaker"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/coffemaker/coffepotmetal"), TEXT("/ElysiumBaked/Models/scenery/furniture/coffemaker/SM_coffepotmetal.SM_coffepotmetal"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/autoramp/autoramp"), TEXT("/ElysiumBaked/Models/scenery/furniture/autoramp/SM_autoramp.SM_autoramp"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/computer/computer_monitor"), TEXT("/ElysiumBaked/Models/scenery/furniture/computer/SM_computer_monitor.SM_computer_monitor"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/grandfather_clock/grandfather_clock"), TEXT("/ElysiumBaked/Models/scenery/furniture/grandfather_clock/SM_grandfather_clock.SM_grandfather_clock"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/microwave/microwave"), TEXT("/ElysiumBaked/Models/scenery/furniture/microwave/SM_microwave.SM_microwave"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/phone_stylish/phone2"), TEXT("/ElysiumBaked/Models/scenery/furniture/phone_stylish/SM_phone2.SM_phone2"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/refrigerator/refrigeratorold"), TEXT("/ElysiumBaked/Models/scenery/furniture/refrigerator/SM_refrigeratorold.SM_refrigeratorold"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/sinka/sinka"), TEXT("/ElysiumBaked/Models/scenery/furniture/sinka/SM_sinka.SM_sinka"), 3 },
		{ TEXT("vtmb:model:scenery/furniture/soc_bed/soc_bed"), TEXT("/ElysiumBaked/Models/scenery/furniture/soc_bed/SM_soc_bed.SM_soc_bed"), 3 },
		{ TEXT("vtmb:model:scenery/furniture/table/foldingtable"), TEXT("/ElysiumBaked/Models/scenery/furniture/table/SM_foldingtable.SM_foldingtable"), 1 },
		{ TEXT("vtmb:model:scenery/furniture/trash_can_prep/trash_can_prep"), TEXT("/ElysiumBaked/Models/scenery/furniture/trash_can_prep/SM_trash_can_prep.SM_trash_can_prep"), 1 },
		{ TEXT("vtmb:model:scenery/misc/laser_emitter/laser_emitter"), TEXT("/ElysiumBaked/Models/scenery/misc/laser_emitter/SM_laser_emitter.SM_laser_emitter"), 0 },
		{ TEXT("vtmb:model:scenery/misc/militarycomputer/box"), TEXT("/ElysiumBaked/Models/scenery/misc/militarycomputer/SM_box.SM_box"), 1 },
		{ TEXT("vtmb:model:scenery/misc/prophetsign/sign"), TEXT("/ElysiumBaked/Models/scenery/misc/prophetsign/SM_sign.SM_sign"), 3 },
		{ TEXT("vtmb:model:scenery/misc/tools/tool2"), TEXT("/ElysiumBaked/Models/scenery/misc/tools/SM_tool2.SM_tool2"), 1 },
		{ TEXT("vtmb:model:scenery/misc/tools/tool3b"), TEXT("/ElysiumBaked/Models/scenery/misc/tools/SM_tool3b.SM_tool3b"), 1 },
		{ TEXT("vtmb:model:scenery/misc/tools/tool4a"), TEXT("/ElysiumBaked/Models/scenery/misc/tools/SM_tool4a.SM_tool4a"), 1 },
		{ TEXT("vtmb:model:scenery/plants/palmtree/palmtree"), TEXT("/ElysiumBaked/Models/scenery/plants/palmtree/SM_palmtree.SM_palmtree"), 1 },
		{ TEXT("vtmb:model:scenery/street/barbedwire/barbedwire"), TEXT("/ElysiumBaked/Models/scenery/street/barbedwire/SM_barbedwire.SM_barbedwire"), 1 },
		{ TEXT("vtmb:model:scenery/street/mailbox/mailbox"), TEXT("/ElysiumBaked/Models/scenery/street/mailbox/SM_mailbox.SM_mailbox"), 1 },
		{ TEXT("vtmb:model:scenery/structural/bradbury/boardsb"), TEXT("/ElysiumBaked/Models/scenery/structural/bradbury/SM_boardsb.SM_boardsb"), 0 },
		{ TEXT("vtmb:model:scenery/structural/santamonica/gallerynoir"), TEXT("/ElysiumBaked/Models/scenery/structural/santamonica/SM_gallerynoir.SM_gallerynoir"), 2 },
		{ TEXT("vtmb:model:scenery/structural/santamonica/gassign"), TEXT("/ElysiumBaked/Models/scenery/structural/santamonica/SM_gassign.SM_gassign"), 2 },
		{ TEXT("vtmb:model:scenery/structural/society/nightstand"), TEXT("/ElysiumBaked/Models/scenery/structural/society/SM_nightstand.SM_nightstand"), 0 },
		{ TEXT("vtmb:model:scenery/structural/society/pedestal_scrolls"), TEXT("/ElysiumBaked/Models/scenery/structural/society/SM_pedestal_scrolls.SM_pedestal_scrolls"), 0 },
		{ TEXT("vtmb:model:scenery/structural/tseng/roofa"), TEXT("/ElysiumBaked/Models/scenery/structural/tseng/SM_roofa.SM_roofa"), 0 },
		{ TEXT("vtmb:model:scenery/structural/warehouse/break_crate_small"), TEXT("/ElysiumBaked/Models/scenery/structural/warehouse/SM_break_crate_small.SM_break_crate_small"), 1 },
		{ TEXT("vtmb:model:scenery/structural/warrens/floorblock"), TEXT("/ElysiumBaked/Models/scenery/structural/warrens/SM_floorblock.SM_floorblock"), 1 },
		{ TEXT("vtmb:model:scenery/trash/blocks/blockwall2"), TEXT("/ElysiumBaked/Models/scenery/trash/blocks/SM_blockwall2.SM_blockwall2"), 1 },
		{ TEXT("vtmb:model:scenery/vehicles/1970chevymalibu/1970chevymalibu"), TEXT("/ElysiumBaked/Models/scenery/vehicles/1970chevymalibu/SM_1970chevymalibu.SM_1970chevymalibu"), 1 },
		{ TEXT("vtmb:model:scenery/vehicles/lightningbird/lightningbird"), TEXT("/ElysiumBaked/Models/scenery/vehicles/lightningbird/SM_lightningbird.SM_lightningbird"), 1 },
		{ TEXT("vtmb:model:scenery/vehicles/limo/sedan"), TEXT("/ElysiumBaked/Models/scenery/vehicles/limo/SM_sedan.SM_sedan"), 3 },
		{ TEXT("vtmb:model:weapons/knife/wield/w_m_knife"), TEXT("/ElysiumBaked/Models/weapons/knife/wield/SM_w_m_knife.SM_w_m_knife"), 0 },
		{ TEXT("vtmb:model:weapons/submachine_mac10/wield/w_m_submachine_mac10"), TEXT("/ElysiumBaked/Models/weapons/submachine_mac10/wield/SM_w_m_submachine_mac10.SM_w_m_submachine_mac10"), 0 },
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsCoreRemainingPreparedAdmissionTest,
 "Elysium.Substrate.ModelPhysics.CoreRemainingPreparedAdmission",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsCoreRemainingPreparedAdmissionTest::RunTest(const FString&)
{
 FPlayerWorldFixture Fixture;
 if (!Fixture.CreateWorld(*this)) return false;
 auto* Map = Fixture.SpawnMapActorDeferred();
 auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
 auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
 TArray<FString> Ids;
 for (const auto& Witness : ElysiumCoreRemainingInputsTest::Witnesses) Ids.Add(Witness.Id);
 TSet<FSoftObjectPath> Paths; FString Error;
 if (!TestTrue(TEXT("actual36 catalogue path gathering"), FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error)))
 { AddError(Error); return false; }
 TArray<UObject*> Assets;
 for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
 auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
 if (!TestTrue(TEXT("actual36 resident prepared admission"), Prepared.IsValid())) { AddError(Error); return false; }
 ON_SCOPE_EXIT { ElysiumPreparedProps::Release(Map); };
 FElysiumWorldServices Services; Services.Embodiment = Map;
 FElysiumEntityWorld EntityWorld(Map, nullptr, Services); EntityWorld.Load(FElysiumEntityDefs());
 for (const auto& Witness : ElysiumCoreRemainingInputsTest::Witnesses)
 {
  auto* Mesh = Prepared->StaticMesh(Witness.Id, Error);
  const FString Label = FString(Witness.Id) + TEXT(" ") + Witness.Path;
  const auto* Inputs = UElysiumModelPhysicsInputs::Find(Mesh);
  AddInfo(FString::Printf(TEXT("CORE36_ADMISSION id=%s path=%s typed=%d source=%d selected=%d hulls=%d expected=%d"),
   Witness.Id, *GetPathNameSafe(Mesh), Inputs != nullptr, Inputs && Inputs->bSourceBody, Inputs && Inputs->SelectedBody != nullptr,
   Inputs && Inputs->SelectedBody ? Inputs->SelectedBody->AggGeom.ConvexElems.Num() : 0, Witness.Hulls));
  if (!TestNotNull(*(Label + TEXT(" prepared static mesh")), Mesh)) continue;
  TestEqual(*(Label + TEXT(" exact prepared address")), Mesh->GetPathName(), FString(Witness.Path));
  if (!TestNotNull(*(Label + TEXT(" actual typed body inputs")), Inputs)) continue;
  TestEqual(*(Label + TEXT(" real source body presence")), Inputs->bSourceBody, Witness.Hulls > 0);
  if (Witness.Hulls == 0)
  {
   TestNull(*(Label + TEXT(" no selected cook for genuine source absence")), Inputs->SelectedBody.Get());
   continue;
  }
  if (!TestNotNull(*(Label + TEXT(" actual selected cook")), Inputs->SelectedBody.Get())) continue;
  TestEqual(*(Label + TEXT(" first solid hull count")), Inputs->SelectedBody->AggGeom.ConvexElems.Num(), Witness.Hulls);
  auto* Component = NewObject<UElysiumPhysicsPropComponent>(Map);
  Component->SetVisibility(false); Component->SetStaticMesh(Mesh); Component->RegisterComponent(); Map->AddInstanceComponent(Component);
  if (TestTrue(*(Label + TEXT(" selected native static actor admission")), ElysiumModelPhysics::CreateStaticModel(Component, EntityWorld.PhysicsSurfaces(), Error)))
  {
   const auto* Body = ElysiumModelPhysics::Identity(Component);
   TestTrue(*(Label + TEXT(" body uses selected setup")), Body && Body->GetBodySetup() == Inputs->SelectedBody.Get());
   int32 ShapeCount = 0;
   if (Body) FPhysicsCommand::ExecuteRead(Body->GetPhysicsActorHandle(), [&](const FPhysicsActorHandle& Actor)
   { TArray<FPhysicsShapeHandle> Shapes; FPhysicsInterface::GetAllShapes_AssumedLocked(Actor, Shapes); ShapeCount = Shapes.Num(); });
   TestEqual(*(Label + TEXT(" actual native shape count")), ShapeCount, Witness.Hulls);
  }
  else AddError(Label + TEXT(" ") + Error);
  ElysiumModelPhysics::Destroy(Component); Component->DestroyComponent();
 }
 return true;
}


namespace ElysiumCoreRemainingInputsTest
{
 struct FPlacement { int32 Model; const TCHAR* Map; int32 Row; float Mass, Inertia, Damping, RotDamping; const TCHAR* Surface; };
 // Literal first-solid parameters and source row identities retained from the two core GLBs.
 static const FPlacement Placements[] = {
  { 0, TEXT("sp_tutorial_1"), 1155, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
  { 1, TEXT("sp_tutorial_1"), 1809, 2.f, 1.f, 0.f, 0.f, TEXT("") },
  { 2, TEXT("sp_tutorial_1"), 1808, 1.f, 1.f, 0.f, 0.f, TEXT("can_pop") },
  { 3, TEXT("sp_tutorial_1"), 1251, 99.999992f, 1.f, 0.f, 0.f, TEXT("metal") },
  { 4, TEXT("sm_hub_1"), 276, 10.f, 1.f, 0.f, 0.f, TEXT("") },
  { 5, TEXT("sm_hub_1"), 2580, 35.f, 1.f, 0.f, 0.f, TEXT("") },
  { 6, TEXT("sp_tutorial_1"), 1807, 20.f, 1.f, 0.f, 0.f, TEXT("metalvent") },
  { 7, TEXT("sp_tutorial_1"), 1826, 5.f, 1.f, 0.f, 0.f, TEXT("plastic") },
  { 8, TEXT("sp_tutorial_1"), 1813, 75.f, 1.f, 0.f, 0.f, TEXT("") },
  { 9, TEXT("sp_tutorial_1"), 1828, 100.f, 1.f, 0.f, 0.f, TEXT("") },
  { 10, TEXT("sp_tutorial_1"), 1787, 75.f, 1.f, 0.f, 0.f, TEXT("") },
  { 11, TEXT("sp_tutorial_1"), 1810, 10.f, 1.f, 0.f, 0.f, TEXT("") },
  { 12, TEXT("sp_tutorial_1"), 1827, 499.999969f, 1.f, 0.f, 0.f, TEXT("") },
  { 13, TEXT("sp_tutorial_1"), 1741, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
  { 14, TEXT("sp_tutorial_1"), 1853, 100.f, 1.f, 0.f, 0.f, TEXT("") },
  { 15, TEXT("sm_hub_1"), 2523, 1.f, 1.f, 0.f, 0.f, TEXT("") },
  { 16, TEXT("sp_tutorial_1"), 1252, 1.f, 1.f, 0.f, 0.f, TEXT("") },
  { 17, TEXT("sp_tutorial_1"), 1253, 1.f, 1.f, 0.f, 0.f, TEXT("") },
  { 18, TEXT("sp_tutorial_1"), 1254, 1.f, 1.f, 0.f, 0.f, TEXT("") },
  { 19, TEXT("sp_tutorial_1"), 1593, 499.999969f, 1.f, 0.f, 0.f, TEXT("wood") },
  { 20, TEXT("sm_hub_1"), 2573, 500.000031f, 1.f, 0.f, 0.f, TEXT("") },
  { 21, TEXT("sm_hub_1"), 2584, 500.000031f, 1.f, 0.f, 0.f, TEXT("") },
  { 22, TEXT("sp_tutorial_1"), 1708, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
  { 23, TEXT("sm_hub_1"), 1377, 500.f, 1.f, 0.f, 0.f, TEXT("") },
  { 24, TEXT("sm_hub_1"), 1675, 500.f, 1.f, 0.f, 0.f, TEXT("") },
  { 25, TEXT("sp_tutorial_1"), 1825, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
  { 26, TEXT("sp_tutorial_1"), 1758, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
  { 27, TEXT("sm_hub_1"), 2574, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
  { 28, TEXT("sp_tutorial_1"), 1756, 100.f, 1.f, 0.f, 0.f, TEXT("wood") },
  { 29, TEXT("sm_hub_1"), 2583, 500.f, 1.f, 0.f, 0.f, TEXT("floorblock") },
  { 30, TEXT("sm_hub_1"), 2575, 20.f, 10.f, 1.f, 1.f, TEXT("") },
  { 31, TEXT("sm_hub_1"), 2585, 1000.00006f, 1.f, 0.1f, 5.f, TEXT("") },
  { 32, TEXT("sm_hub_1"), 2582, 29.999998f, 1.f, 0.f, 0.f, TEXT("") },
  { 33, TEXT("sm_hub_1"), 2448, 1.f, 1.f, 0.f, 0.f, TEXT("") },
  { 34, TEXT("sp_tutorial_1"), 1036, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
  { 35, TEXT("sp_tutorial_1"), 1051, 85.f, 1.f, 0.1f, 0.1f, TEXT("") },
 };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsCoreRemainingConsumersTest,
 "Elysium.Substrate.ModelPhysics.CoreRemainingConsumers",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsCoreRemainingConsumersTest::RunTest(const FString&)
{
 FPlayerWorldFixture Fixture; if (!Fixture.CreateWorld(*this)) return false;
 auto* Map = Fixture.SpawnMapActorDeferred(); FString Error;
 auto* Placed = LoadObject<UElysiumPlacedModelCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PlacedModels.DA_PlacedModels"));
 auto* Skins = LoadObject<UElysiumPropSkinCatalogue>(nullptr, TEXT("/ElysiumBaked/Models/_Corpus/DA_PropSkins.DA_PropSkins"));
 TArray<FString> Ids; for (const auto& Witness : ElysiumCoreRemainingInputsTest::Witnesses) Ids.Add(Witness.Id);
 TSet<FSoftObjectPath> Paths;
 if (!FElysiumPreparedPropModels::GatherPaths(Placed, Skins, Ids, Paths, Error)) { AddError(Error); return false; }
 TArray<UObject*> Assets; for (const auto& Path : Paths) Assets.Add(Path.TryLoad());
 auto Prepared = FElysiumPreparedPropModels::Create(Map, 1, Placed, Skins, Ids, Assets, Error);
 if (!TestTrue(TEXT("all36 resident production preparation"), Prepared.IsValid())) { AddError(Error); return false; }
 ON_SCOPE_EXIT { ElysiumPreparedProps::Release(Map); };
 TMap<FString, FElysiumEntityDefs> CoreDefs;
 for (const TCHAR* MapName : { TEXT("sp_tutorial_1"), TEXT("sm_hub_1") })
 {
  auto* Table = LoadObject<UElysiumMapEntities>(nullptr, *FElysiumContentPaths::BakedMapEntities(MapName));
  if (!TestNotNull(TEXT("actual baked core map entity table"), Table)) return false;
  FElysiumEntityDefs Actual; Table->Deserialize(Actual); CoreDefs.Add(MapName, MoveTemp(Actual));
 }
 FElysiumEntityDefs Defs; Defs.MapName = TEXT("core_remaining_native");
 // Real registered endpoint rows, with their complete authored keys and model choices.
 struct FEndpoint { const TCHAR* Map; int32 Row; const TCHAR* Name; const TCHAR* Class; };
 const FEndpoint Endpoints[] = {
  {TEXT("sp_tutorial_1"),1363,TEXT("Jack"),TEXT("npc_VVampire")},
  {TEXT("sp_tutorial_1"),1040,TEXT("sabbat_redshirt_5"),TEXT("npc_VVampire")},
  {TEXT("sp_tutorial_1"),532,TEXT("sabbat_redshirt_2"),TEXT("npc_VVampire")},
  {TEXT("sm_hub_1"),2206,TEXT("Prophet"),TEXT("npc_VDialogPedestrian")}
 };
 for (const auto& Endpoint : Endpoints)
 {
  const auto& Actual = CoreDefs.FindChecked(Endpoint.Map);
  if (!TestTrue(TEXT("real source endpoint index"), Actual.Defs.IsValidIndex(Endpoint.Row))) return false;
  const auto& Definition = Actual.Defs[Endpoint.Row];
  TestEqual(TEXT("real source endpoint name"), Definition.TargetName, FString(Endpoint.Name));
  TestEqual(TEXT("real source endpoint registered class"), Definition.Classname, FString(Endpoint.Class));
  Defs.Defs.Add(Definition);
 }
 TArray<int32> FieldIndices;
 for (const auto& Placement : ElysiumCoreRemainingInputsTest::Placements)
 {
  const auto& Actual = CoreDefs.FindChecked(Placement.Map);
  if (!TestTrue(TEXT("actual placement source index"), Actual.Defs.IsValidIndex(Placement.Row))) return false;
  const auto& Definition = Actual.Defs[Placement.Row];
  TestEqual(TEXT("actual source model identity"), ElysiumPreparedProps::ModelId(Definition.Keys.FindRef(TEXT("model"))), Ids[Placement.Model]);
  TestEqual(TEXT("selected contract authored Solid6"), Definition.Keys.FindRef(TEXT("solid")), FString(TEXT("6")));
  FieldIndices.Add(Defs.Defs.Num()); Defs.Defs.Add(Definition);
 }
 // The second actual mac10 placement resolves the fourth endpoint, not an invented parent.
 const int32 SecondMacIndex = Defs.Defs.Num(); Defs.Defs.Add(CoreDefs.FindChecked(TEXT("sp_tutorial_1")).Defs[1222]);
 FElysiumEntityDef AliasDef; AliasDef.Classname=TEXT("info_target"); AliasDef.TargetName=TEXT("core_native_alias");
 const int32 AliasIndex=Defs.Defs.Num(); Defs.Defs.Add(AliasDef);
 FElysiumWorldServices Services; Services.Embodiment = Map;
 FElysiumEntityWorld Source(Map, nullptr, Services); Source.Load(FElysiumEntityDefs(Defs));
 auto ResolveIndex = [](FElysiumEntityWorld& EntityWorld, int32 Index) { return EntityWorld.Resolve(FElysiumEntityHandle(Index, EntityWorld.GetEpoch())); };
 for (const auto& Endpoint : Endpoints)
 {
  auto* Parent=Source.FindByName(Endpoint.Name);
  if (!TestTrue(TEXT("real endpoint constructs registered NPC before parent resolution"), Parent && Parent->AsNpc())) return false;
 }
 TArray<FElysiumSavedPhysicsBody> BeforeStates; BeforeStates.SetNum(36);
 TArray<ElysiumModelPhysics::FNativeOutput> BeforeTokens;
 auto CheckProduction = [&](FElysiumEntityWorld& EntityWorld, bool bRestored)
 {
  int32 StaticCount=0, ShadowCount=0, NullCount=0;
  for (int32 ModelIndex=0; ModelIndex<Ids.Num(); ++ModelIndex)
  {
   const auto& Witness=ElysiumCoreRemainingInputsTest::Witnesses[ModelIndex];
   const auto& Placement=ElysiumCoreRemainingInputsTest::Placements[ModelIndex];
   auto* Entity=ResolveIndex(EntityWorld,FieldIndices[ModelIndex]);
   if (!TestNotNull(Witness.Id,Entity)) return false;
   const FString ParentName=Defs.Defs[FieldIndices[ModelIndex]].Keys.FindRef(TEXT("parentname"));
   auto* Parent=EntityWorld.Resolve(Entity->MoveParent);
   if (!ParentName.IsEmpty()) TestTrue(TEXT("authored parentname resolves actual endpoint handle"),Parent && Parent==EntityWorld.FindByName(ParentName));
   else TestNull(TEXT("authored unparented row remains unparented"),Parent);
   auto* Mesh=Prepared->StaticMesh(Witness.Id,Error); const auto* Inputs=UElysiumModelPhysicsInputs::Find(Mesh);
   if (!Inputs) return false;
   void* Object=Entity->VPhysicsGetObject(); void* Listed=nullptr;
   if (Witness.Hulls==0)
   {
    ++NullCount; TestNull(TEXT("genuine no-source Solid6 has no fallback body"),Object);
    TestEqual(TEXT("null source current list count"),Entity->VPhysicsGetObjectList(&Listed,1),0);
    TestFalse(TEXT("null source never registers shadow"),EntityWorld.PhysicsShadows().Contains(Entity->Handle));
    const bool bWasHidden=Entity->IsHidden(); Entity->ScriptHide(); Entity->ScriptUnhide();
    if(bWasHidden) Entity->ScriptHide();
    AddInfo(FString::Printf(TEXT("CORE36_CONSUMER map=%s row=%d id=%s parent=%s representation=source-absent static=0 shapes=0 query=0 sim=0 restored=%d"),Placement.Map,Placement.Row,Witness.Id,*ParentName,bRestored));
    TestNull(TEXT("negative hide/unhide cannot synthesize bbox"),Entity->VPhysicsGetObject());
    continue;
   }
   if (!TestNotNull(TEXT("reached production selected native body"),Object)) return false;
   auto* Component=ElysiumModelPhysics::ComponentFor(Object); auto* Native=ElysiumModelPhysics::Identity(Component);
   const auto* Recipe=ElysiumModelPhysics::RecipeFor(Object); const auto* Shadow=ElysiumModelPhysics::ShadowFor(Object);
   if (!Native || !Recipe) return false;
   TestTrue(TEXT("production body uses actual selected cook"),Native->GetBodySetup()==Inputs->SelectedBody.Get());
   TestTrue(TEXT("selected cook distinct from art cook"),Native->GetBodySetup()!=Mesh->GetBodySetup());
   TestTrue(TEXT("successful creator owns current game data"),ElysiumModelPhysics::GameDataForObject(Object)==Entity);
   TestEqual(TEXT("production current body list count"),Entity->VPhysicsGetObjectList(&Listed,1),1);
   TestTrue(TEXT("production current list identity"),Listed==Object);
   TestTrue(TEXT("actual creator uses model recipe"),Recipe->Kind==EElysiumPhysicsRecipeKind::Model);
   TestEqual(TEXT("literal first solid authored mass kg"),Inputs->MassKg,Placement.Mass);
   TestEqual(TEXT("literal first solid authored inertia"),Inputs->InertiaScale,Placement.Inertia);
   TestEqual(TEXT("literal first solid authored linear damping"),Inputs->LinearDamping,Placement.Damping);
   TestEqual(TEXT("literal first solid authored angular damping"),Inputs->AngularDamping,Placement.RotDamping);
   TestEqual(TEXT("literal raw first solid surface"),Inputs->SurfaceProperty,FString(Placement.Surface));
   TestEqual(TEXT("native recipe retains authored mass without prop bias"),Recipe->MassKg,Inputs->MassKg);
   TestEqual(TEXT("native authored damping"),Native->LinearDamping,Parent?0.f:Inputs->LinearDamping);
   TestEqual(TEXT("native authored rotational damping"),Native->AngularDamping,Parent?0.f:Inputs->AngularDamping);
   int32 Shapes=0,QueryShapes=0,SimShapes=0;
   FPhysicsCommand::ExecuteRead(Native->GetPhysicsActor(),[&](const FPhysicsActorHandle&)
   {
    TArray<FPhysicsShapeHandle> Handles; Shapes=Native->GetAllShapes_AssumesLocked(Handles);
    for(const auto& Shape:Handles) {QueryShapes+=FPhysicsInterface::IsQueryShape(Shape);SimShapes+=FPhysicsInterface::IsSimulationShape(Shape);}
   });
   TestEqual(TEXT("actual selected native convex count"),Shapes,Witness.Hulls);
   TestEqual(TEXT("current hidden state gates query shapes"),QueryShapes,Entity->IsHidden()?0:Witness.Hulls);
   TestEqual(TEXT("current hidden state gates simulation shapes"),SimShapes,Entity->IsHidden()?0:Witness.Hulls);
   const auto* Material=Cast<UElysiumPhysicalMaterial>(Native->GetSimplePhysicalMaterial());
   // Accepted2601a7e0 installs special shadow material before disabling gravity/damping.
   int32 SurfaceIndex=EntityWorld.PhysicsSurfaces().GetSurfaceIndex(Parent?TEXT("$MATERIAL_INDEX_SHADOW"):Inputs->SurfaceProperty);
   if (SurfaceIndex<0) SurfaceIndex=EntityWorld.PhysicsSurfaces().GetSurfaceIndex(TEXT("default"));
   const auto* Surface=EntityWorld.PhysicsSurfaces().GetSurfaceData(SurfaceIndex);
   if (!Material || !Surface) return false;
   TestEqual(TEXT("actual native surface/default lookup"),Material->SourceName,EntityWorld.PhysicsSurfaces().GetSurfaceName(SurfaceIndex));
   TestEqual(TEXT("actual native friction"),Material->Friction,Surface->Friction());
   TestEqual(TEXT("actual native restitution"),Material->Restitution,Surface->Elasticity());
   if (Parent)
   {
    ++ShadowCount; TestFalse(TEXT("actual resolved ornament recipe dynamic"),Recipe->bStatic);
    TestEqual(TEXT("studio parser retains inertia"),Recipe->InertiaScale,Inputs->InertiaScale);
    TestTrue(TEXT("actual resolved ornament owns shadow"),Shadow && Shadow->bInstalled && EntityWorld.PhysicsShadows().Contains(Entity->Handle));
    if (!Shadow) return false;
    TestEqual(TEXT("studio shadow saved authored mass"),Shadow->SavedMassKg,Inputs->MassKg);
    TestEqual(TEXT("shadow saves original authored damping"),Shadow->SavedLinearDamping,Inputs->LinearDamping);
    TestEqual(TEXT("shadow saves original authored rotational damping"),Shadow->SavedAngularDamping,Inputs->AngularDamping);
    TestFalse(TEXT("no move permission"),Shadow->bAllowMovement); TestFalse(TEXT("no rotation permission"),Shadow->bAllowRotation);
    TestEqual(TEXT("native speed cap cm per second"),Shadow->LinearDesiredLimitCm,25400.f);
    TestTrue(TEXT("shadow body independent of art"),Component!=Entity->GetAttachBody() && !Component->IsVisible());
   }
   else
   {
    ++StaticCount; TestTrue(TEXT("actual unparented static branch"),Recipe->bStatic && !Shadow);
    TestEqual(TEXT("static postparse inertia overwrite"),Recipe->InertiaScale,1.f);
    TestTrue(TEXT("native static state"),ElysiumModelPhysics::ActorIdentity(Component)->GetGameThreadAPI().ObjectState()==Chaos::EObjectStateType::Static);
   }
   auto* Art=Entity->GetAttachBody();
   if (!TestNotNull(TEXT("actual production art representation"),Art)) return false;
   const auto* StaticArt=Cast<UStaticMeshComponent>(Art); const auto* SkeletalArt=Cast<USkeletalMeshComponent>(Art);
   TestTrue(TEXT("actual representation selects prepared static or skeletal art"),
    (StaticArt && StaticArt->GetStaticMesh()==Mesh) || (SkeletalArt && SkeletalArt->GetSkeletalMeshAsset()==Prepared->SkeletalMesh(Witness.Id,Error)));
   FElysiumSavedPhysicsBody Actual; if (!ElysiumModelPhysics::Capture(Object,Actual)) return false;
   if (!bRestored)
   {
    const FTransform Expected(ElysiumModelPhysics::SourceRotation(Entity->GetAbsAngles()),Entity->GetAbsOrigin(),Component->GetComponentScale());
    TestTrue(TEXT("actual initial absolute native pose"),Actual.Pose.Equals(Expected,.001));
    BeforeStates[ModelIndex]=Actual; BeforeTokens.Add(ElysiumModelPhysics::CaptureIdentity(Object));
   }
   else
   {
    const auto& Before=BeforeStates[ModelIndex];
    TestTrue(TEXT("Physics archive reconnect retains native pose and semantic shapes"),Actual.Pose.Equals(Before.Pose,.001) && Actual.ModelShapeFlags==Before.ModelShapeFlags);
    TestTrue(TEXT("Physics archive reconnect retains native mass frame/inertia"),Actual.CurrentMassKg==Before.CurrentMassKg && Actual.CurrentInertia==Before.CurrentInertia && Actual.CurrentInverseInertia==Before.CurrentInverseInertia && Actual.CurrentMassFrame.Equals(Before.CurrentMassFrame,1.e-6));
    TestEqual(TEXT("Physics archive reconnect retains current WORD"),Actual.CurrentGameFlags,Before.CurrentGameFlags);
   }
   AddInfo(FString::Printf(TEXT("CORE36_CONSUMER map=%s row=%d id=%s parent=%s representation=%s static=%d shapes=%d query=%d sim=%d mass=%g inertia=%g restored=%d"),
    Placement.Map,Placement.Row,Witness.Id,*ParentName,SkeletalArt?TEXT("skeletal"):TEXT("static"),Recipe->bStatic,Shapes,QueryShapes,SimShapes,Recipe->MassKg,Recipe->InertiaScale,bRestored));
  }
  TestEqual(TEXT("actual unparented positive static count"),StaticCount,27);
  TestEqual(TEXT("actual resolved positive studio count"),ShadowCount,1);
  TestEqual(TEXT("all8 source absence controls"),NullCount,8);
  auto* SecondMac=ResolveIndex(EntityWorld,SecondMacIndex);
  TestTrue(TEXT("second real mac10 resolves redshirt2 endpoint"),SecondMac && EntityWorld.Resolve(SecondMac->MoveParent)==EntityWorld.FindByName(TEXT("sabbat_redshirt_2")));
  if (SecondMac) TestNull(TEXT("second real mac10 remains native null"),SecondMac->VPhysicsGetObject());
  return true;
 };
 if (!CheckProduction(Source,false)) return false;
 auto* Monitor=ResolveIndex(Source,FieldIndices[4]); auto* Sink=ResolveIndex(Source,FieldIndices[9]);
 void* SinkActor=Sink->VPhysicsGetObject(); auto* SinkComponent=ElysiumModelPhysics::ComponentFor(SinkActor);
 auto TraceSelected=[&](FElysiumEntity* Entity)
 {
  auto* Native=ElysiumModelPhysics::Identity(ElysiumModelPhysics::ComponentFor(Entity->VPhysicsGetObject()));
  const FVector Center=Native->GetUnrealWorldTransform().TransformPosition(Native->GetBodySetup()->AggGeom.ConvexElems[0].ElemBox.GetCenter());
  FCollisionQueryParams Params(FName(TEXT("CoreRemainingSelected")),false);
  FHitResult Hit; Fixture.World->LineTraceSingleByChannel(Hit,Center-FVector(1000,0,0),Center+FVector(1000,0,0),ElysiumCollision::SightChannel,Params);
  return Hit.GetComponent()==ElysiumModelPhysics::ComponentFor(Entity->VPhysicsGetObject());
 };
 if (!Fixture.TestWorld.TickTestWorld(.02f)) return false;
 TestTrue(TEXT("actual visible computer monitor native query"),TraceSelected(Monitor));
 TestTrue(TEXT("authored hidden multi-hull sink identity retained"),Sink->IsHidden() && SinkActor);
 TestFalse(TEXT("actual native query passes through authored hidden multi-hull sink"),TraceSelected(Sink));
 Sink->ScriptUnhide(); TestTrue(TEXT("actual unhide preserves multi-hull incarnation"),Sink->VPhysicsGetObject()==SinkActor);
 TestTrue(TEXT("actual unhide restores selected hull query"),TraceSelected(Sink));
 Sink->ScriptHide(); TestTrue(TEXT("actual hide preserves selected carrier"),ElysiumModelPhysics::ComponentFor(SinkActor)==SinkComponent);
 auto* Alias=ResolveIndex(Source,AliasIndex); Alias->VPhysicsSetObject(Monitor->VPhysicsGetObject());
 // Archive comparisons use actual current state after the native query frame and hide operations.
 for(int32 ModelIndex=0;ModelIndex<Ids.Num();++ModelIndex)
  if(auto* Entity=ResolveIndex(Source,FieldIndices[ModelIndex]);Entity && Entity->VPhysicsGetObject())
   if(!ElysiumModelPhysics::Capture(Entity->VPhysicsGetObject(),BeforeStates[ModelIndex])) return false;
 FElysiumMapSnapshot Snapshot; Source.Freeze(Snapshot);
 if (!TestFalse(TEXT("actual joined production Physics save admission"),Snapshot.bPhysicsSaveFailed)) return false;
 TArray<uint8> Bytes; FMemoryWriter Writer(Bytes,true); FElysiumSaveArchive SaveAr(Writer,FElysiumSaveVersion::Latest); SaveAr<<Snapshot;
 FElysiumMapSnapshot Persisted; FMemoryReader Reader(Bytes,true); FElysiumSaveArchive LoadAr(Reader,FElysiumSaveVersion::Latest); LoadAr<<Persisted;
 if (!TestFalse(TEXT("actual typed Physics block decodes"),Reader.IsError())) return false;
 Source.Teardown(); for(const auto& Token:BeforeTokens) TestFalse(TEXT("source retirement fences actual selected incarnation"),ElysiumModelPhysics::IsCurrentOutput(Token));
 Persisted.PhysicsBodies.Reset(); // no decoded-array shortcut: ApplySnapshot consumes persisted Physics bytes.
 FElysiumEntityWorld Reload(Map,nullptr,Services); Reload.Load(FElysiumEntityDefs(Defs));
 if (!TestTrue(TEXT("actual production native reconnect before first think"),Reload.ApplySnapshot(Persisted)>=0) || !CheckProduction(Reload,true)) return false;
 auto* RestoredMonitor=ResolveIndex(Reload,FieldIndices[4]); auto* RestoredAlias=ResolveIndex(Reload,AliasIndex);
 TestTrue(TEXT("saved alias reconstructs independent native field"),RestoredMonitor->VPhysicsGetObject() && RestoredAlias->VPhysicsGetObject() && RestoredMonitor->VPhysicsGetObject()!=RestoredAlias->VPhysicsGetObject());
 const auto RestoredToken=ElysiumModelPhysics::CaptureIdentity(RestoredMonitor->VPhysicsGetObject()); RestoredMonitor->VPhysicsDestroyObject();
 TestFalse(TEXT("explicit225 fences selected restored monitor"),ElysiumModelPhysics::IsCurrentOutput(RestoredToken));
 TestNotNull(TEXT("independent restored alias lifetime"),RestoredAlias->VPhysicsGetObject());
 Reload.Teardown();
 FElysiumEntityWorld Transition(Map,nullptr,Services); Transition.Load(FElysiumEntityDefs(Defs));
 if(!TestTrue(TEXT("native transition reconnect consumes same typed Physics bytes"),Transition.ApplySnapshot(Persisted,0.,true)>=0) || !CheckProduction(Transition,true)) return false;
 Transition.Teardown();
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelPhysicsCorePlaceholderTest,
 "Elysium.Substrate.ModelPhysics.CoreRemainingPlaceholder",
 EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumModelPhysicsCorePlaceholderTest::RunTest(const FString&)
{
 // Public import models --force reauthors this canonical incidental placeholder.
 // import_models.py1051-1090: same frozen authoring recipe; both package hashes retained.
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/ElysiumBaked/Models/_Corpus/SM_Missing.SM_Missing"));
 if(!TestNotNull(TEXT("canonical missing-model placeholder"),Mesh)) return false;
 const auto* Body=Mesh->GetBodySetup(); if(!TestNotNull(TEXT("placeholder actual bbox cook"),Body)) return false;
 TestEqual(TEXT("placeholder simple/complex trace flag"),Body->CollisionTraceFlag,CTF_UseSimpleAndComplex);
 TestEqual(TEXT("placeholder one centered box"),Body->AggGeom.BoxElems.Num(),1);
 TestEqual(TEXT("placeholder no generic convex"),Body->AggGeom.ConvexElems.Num(),0);
 if(Body->AggGeom.BoxElems.Num()!=1) return false;
 const auto& Box=Body->AggGeom.BoxElems[0];
 TestEqual(TEXT("placeholder 100cm X"),Box.X,100.f); TestEqual(TEXT("placeholder 100cm Y"),Box.Y,100.f); TestEqual(TEXT("placeholder 100cm Z"),Box.Z,100.f);
 TestEqual(TEXT("placeholder centered"),Box.Center,FVector::ZeroVector);
 TestTrue(TEXT("placeholder render bounds [-50,+50]cm"),Mesh->GetBoundingBox().Min.Equals(FVector(-50),.001) && Mesh->GetBoundingBox().Max.Equals(FVector(50),.001));
 TestEqual(TEXT("placeholder one material slot"),Mesh->GetStaticMaterials().Num(),1);
 if(Mesh->GetStaticMaterials().Num()!=1) return false;
 const auto& Slot=Mesh->GetStaticMaterials()[0]; TestEqual(TEXT("placeholder missing slot"),Slot.MaterialSlotName,FName(TEXT("missing")));
 TestEqual(TEXT("placeholder actual sentinel material"),GetPathNameSafe(Slot.MaterialInterface),FString(TEXT("/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing.MI_V2_Missing")));
#if WITH_EDITOR
 TestEqual(TEXT("placeholder one source LOD"),Mesh->GetNumSourceModels(),1);
 TestTrue(TEXT("placeholder full UV precision"),Mesh->GetSourceModel(0).BuildSettings.bUseFullPrecisionUVs);
 TestTrue(TEXT("placeholder high tangent precision"),Mesh->GetSourceModel(0).BuildSettings.bUseHighPrecisionTangentBasis);
 auto& Metadata=Mesh->GetOutermost()->GetMetaData();
 TestEqual(TEXT("placeholder unchanged canonical recipe fingerprint"),Metadata.GetValue(Mesh,TEXT("ElysiumRecipe")),FString(TEXT("479813a21ffac44b0ae65b5a3dcf09099f2cf4ca5ba9808040f24685921d58d0")));
 TestEqual(TEXT("placeholder actual models producer"),Metadata.GetValue(Mesh,TEXT("ElysiumProducer")),FString(TEXT("models")));
#endif
 AddInfo(TEXT("CORE36_PLACEHOLDER canonical 100cm centered bbox/simple-complex/missing slot/material/precision/recipe verified; incidental public force rewrite retained separately from 3425 unselected models"));
 return true;
}

#endif
