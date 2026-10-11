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
#include "ElysiumSaveArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

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
	Moving->Visual->SetEnableGravity(false);
	Moving->Visual->SetLinearDamping(.75f); // actual runtime setter also used by water's damping observer
	const FTransform NativePose(FRotator(19, 37, 11), FVector(5000, 4000, 3000), Moving->Visual->GetComponentScale());
	MovingBody->SetBodyTransform(NativePose, ETeleportType::TeleportPhysics);
	MovingBody->SetLinearVelocity(FVector(75, -20, 8), false);
	MovingBody->SetAngularVelocityInRadians(FVector(.2, -.4, .6), false);
	Sleeping->Visual->SetWorldLocation(FVector(-5000, -4000, 3000));
	ElysiumModelPhysics::Identity(Sleeping->Visual)->PutInstanceToSleep();
	Removed->Kill();
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
	RemovedAlias->VPhysicsSetObject(Moving->VPhysicsGetObject()); RemovedAlias->Kill();
	TestNotNull(TEXT("source alias Kill leaves referenced model producer alive"), ElysiumModelPhysics::Identity(Moving->Visual));
	FElysiumMapSnapshot Snapshot; Source.Freeze(Snapshot);
	TestEqual(TEXT("Physics saves eligible fields including aliases and absent body"), Snapshot.PhysicsBodies.Num(), 5);
	const FElysiumSavedPhysicsBody* AbsentField = Snapshot.PhysicsBodies.FindByPredicate(
		[BodylessIndex](const FElysiumSavedPhysicsBody& State) { return State.EntityIndex == BodylessIndex; });
	TestTrue(TEXT("actual Physics save carries count0 field state"), AbsentField && !AbsentField->bHasObject);
	TestFalse(TEXT("registered source alias admitted"), Snapshot.bPhysicsSaveFailed);
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
	TestFalse(TEXT("gravity state persisted"), RestoredState.bGravity);
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
	TestEqual(TEXT("base alias native collision profile persists"), AliasState.CollisionProfile, NativeState.CollisionProfile);
	UStaticMeshComponent* AliasComponent = ElysiumModelPhysics::ComponentFor(RestoredAlias->VPhysicsGetObject());
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
	TWeakObjectPtr<UStaticMeshComponent> TeardownComponent = ElysiumModelPhysics::ComponentFor(RestoredTeardownAlias->VPhysicsGetObject());
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
#endif
