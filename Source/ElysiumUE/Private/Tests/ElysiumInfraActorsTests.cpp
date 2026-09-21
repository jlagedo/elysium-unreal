#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AiInfra/ElysiumConversationPlaceActor.h"
#include "AiInfra/ElysiumHintActor.h"
#include "AiInfra/ElysiumInfraIndex.h"
#include "AiInfra/ElysiumInfraKeyfields.h"
#include "AiInfra/ElysiumInterestingPlaceActor.h"
#include "AiInfra/ElysiumKeyfieldAccess.h"
#include "AiInfra/ElysiumNpcMakerActor.h"
#include "AiInfra/ElysiumNpcPlacementActor.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "ElysiumBakedTags.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapEntities.h"
#include "Map/ElysiumInfraAdoption.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "Tests/ElysiumPlayerWorldFixture.h"

// 0018 story 2: the baked infrastructure actors and the def they rebuild.
//
// The typed keyfield structs are authoritative; the actor keeps every authored pair and rebuilds
// its entity def by the rule `AElysiumInfraActor` states — byte for byte when untouched, the typed
// value when a property changed, and the entity table's own fold. The adoption pass rewrites the
// def at the actor's own BSP index, in place, and only after the whole declared set checked out.

static constexpr EAutomationTestFlags GElysiumInfraTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumInfraActorsTests
{
	template <typename TActor>
	TActor* Spawn(UWorld* World, int32 EntityIndex, const TCHAR* Classname, const FVector& Where,
		TArray<FString> Keys, TArray<FString> Values)
	{
		TActor* Actor = World->SpawnActor<TActor>(Where, FRotator::ZeroRotator);
		if (Actor != nullptr)
		{
			Actor->ConfigureBakedIdentity(EntityIndex, Classname, TEXT(""));
			Actor->ApplyBakedKeyvalues(Keys, Values);
			Actor->Tags = { Actor->FamilyTag(), ElysiumBakedTags::EntityIndex(EntityIndex) };
		}
		return Actor;
	}

	FString KeysText(const TMap<FString, FString>& Keys)
	{
		TArray<FString> Parts;
		for (const TPair<FString, FString>& Pair : Keys)
		{
			Parts.Add(Pair.Key + TEXT("=") + Pair.Value);
		}
		return FString::Join(Parts, TEXT("|"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraKeyfieldsRoundTripTest,
	"Elysium.Substrate.InfraKeyfields.RoundTrip", GElysiumInfraTestFlags)
bool FElysiumInfraKeyfieldsRoundTripTest::RunTest(const FString&)
{
	using namespace ElysiumKeyfieldAccess;
	FElysiumPlaceKeyfields Place;
	const UScriptStruct* Struct = FElysiumPlaceKeyfields::StaticStruct();

	// The property IS the external, and it resolves case-insensitively like the registry's FName.
	const FProperty* MaxTime = FindProperty(Struct, TEXT("MAX_TIME"));
	const FProperty* MinBounds = FindProperty(Struct, TEXT("min_bounds"));
	const FProperty* GroupId = FindProperty(Struct, TEXT("group_id"));
	const FProperty* Enabled = FindProperty(Struct, TEXT("enabled"));
	const FProperty* Type = FindProperty(Struct, TEXT("type"));
	if (!TestNotNull(TEXT("max_time"), MaxTime) || !TestNotNull(TEXT("min_bounds"), MinBounds)
		|| !TestNotNull(TEXT("group_id"), GroupId) || !TestNotNull(TEXT("enabled"), Enabled)
		|| !TestNotNull(TEXT("type"), Type))
	{
		return false;
	}
	TestNull(TEXT("a key with no datamap row is no property"), FindProperty(Struct, TEXT("testflags")));

	// The dirty values the corpus authors, parsed the runtime's way (CRT atof/atoi).
	struct FCase { const FProperty* Property; const TCHAR* Raw; const TCHAR* Formatted; };
	const FCase Cases[] =
	{
		{ MaxTime, TEXT("23523235.0"), TEXT("23523236") },   // binary32 rounds it, as retail's float does
		{ MaxTime, TEXT(".5"), TEXT("0.5") },
		{ MaxTime, TEXT("-3496,92"), TEXT("-3496") },         // longest numeric prefix
		{ GroupId, TEXT("1.5"), TEXT("1") },                  // atoi on a float spelling
		{ GroupId, TEXT(" 12abc"), TEXT("12") },
		{ Enabled, TEXT("true"), TEXT("0") },                 // atoi("true") == 0
		{ Enabled, TEXT("2"), TEXT("1") },
		{ MinBounds, TEXT("0  0 72"), TEXT("0 0 72") },       // doubled whitespace
		{ MinBounds, TEXT("1 2"), TEXT("0 0 0") },            // under three parts reads as zero
		{ Type, TEXT("Citizen_Idle"), TEXT("Citizen_Idle") },
	};
	for (const FCase& Case : Cases)
	{
		Apply(Case.Property, &Place, Case.Raw);
		TestTrue(*FString::Printf(TEXT("'%s' matches what it applied"), Case.Raw), Matches(Case.Property, &Place, Case.Raw));
		const FString Formatted = Format(Case.Property, &Place);
		TestEqual(*FString::Printf(TEXT("'%s' formats"), Case.Raw), Formatted, FString(Case.Formatted));
		TestTrue(*FString::Printf(TEXT("'%s' formats to a string that parses back to the same value"), Case.Raw),
			Matches(Case.Property, &Place, Formatted));
	}
	Apply(MaxTime, &Place, TEXT("0"));
	TestTrue(TEXT("zero is zero"), IsZero(MaxTime, &Place));
	TestFalse(TEXT("a different string does not match"), Matches(MaxTime, &Place, TEXT("0.25")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraActorsSpawnTest,
	"Elysium.Substrate.InfraActors.Spawn", GElysiumInfraTestFlags)
bool FElysiumInfraActorsSpawnTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this))
	{
		return false;
	}
	UWorld* World = Fixture.World;
	const TArray<AElysiumInfraActor*> Actors =
	{
		World->SpawnActor<AElysiumHintActor>(), World->SpawnActor<AElysiumInterestingPlaceActor>(),
		World->SpawnActor<AElysiumConversationPlaceActor>(), World->SpawnActor<AElysiumNpcMakerActor>(),
		World->SpawnActor<AElysiumNpcPlacementActor>(),
	};
	const FName Families[] = { ElysiumBakedTags::InfraHint, ElysiumBakedTags::InfraPlace,
		ElysiumBakedTags::InfraConversation, ElysiumBakedTags::InfraMaker, ElysiumBakedTags::InfraNpc };
	for (int32 i = 0; i < Actors.Num(); ++i)
	{
		AElysiumInfraActor* Actor = Actors[i];
		if (!TestNotNull(TEXT("the family actor spawns"), Actor))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s adopts under its family tag"), *Actor->GetClass()->GetName()),
			Actor->FamilyTag(), Families[i]);
		TestNotNull(TEXT("a cooked-safe scene root"), Actor->GetRootComponent());
		TestFalse(TEXT("no tick"), Actor->PrimaryActorTick.bCanEverTick);
#if WITH_EDITORONLY_DATA
		TestNotNull(TEXT("an editor billboard"), Actor->FindComponentByClass<UBillboardComponent>());
		TestNotNull(TEXT("an editor arrow"), Actor->FindComponentByClass<UArrowComponent>());
#endif
		TArray<ElysiumKeyfieldAccess::FView> Views;
		Actor->GetKeyfieldViews(Views);
		TestTrue(TEXT("the base entity's rows come last"), Views.Num() > 0
			&& Views.Last().Struct == FElysiumBaseEntityKeyfields::StaticStruct());
	}
	TestNotNull(TEXT("the index actor spawns"), World->SpawnActor<AElysiumInfraIndex>());

	// The typed properties are reflected where the bake and the Details panel see them.
	AElysiumHintActor* Hint = Cast<AElysiumHintActor>(Actors[0]);
	if (Hint != nullptr)
	{
		Hint->ApplyBakedKeyvalues({ TEXT("hinttype"), TEXT("Group"), TEXT("StartHintDisabled") },
			{ TEXT("10000"), TEXT("A1"), TEXT("1") });
		TestEqual(TEXT("hinttype reaches the typed row"), Hint->Hint.HintType, 10000);
		TestEqual(TEXT("Group keeps its exact case"), Hint->Hint.Group, FString(TEXT("A1")));
		TestEqual(TEXT("StartHintDisabled"), Hint->Hint.StartHintDisabled, 1);
		TestNotNull(TEXT("the hint row is a reflected property"),
			AElysiumHintActor::StaticClass()->FindPropertyByName(TEXT("Hint")));
	}
	AElysiumNpcMakerActor* Maker = Cast<AElysiumNpcMakerActor>(Actors[3]);
	if (Maker != nullptr)
	{
		Maker->ApplyBakedKeyvalues({ TEXT("NPCType"), TEXT("hint_groups"), TEXT("model"), TEXT("Flag_ZombieAIType") },
			{ TEXT("npc_VHuman"), TEXT("1 2"), TEXT("models/a.mdl"), TEXT("3") });
		TestEqual(TEXT("the maker's own row"), Maker->Maker.NPCType, FString(TEXT("npc_VHuman")));
		TestEqual(TEXT("the zombie row"), Maker->Maker.Flag_ZombieAIType, 3);
		TestEqual(TEXT("a child row lands in the child template"), Maker->ChildTemplate.hint_groups, FString(TEXT("1 2")));
		TestEqual(TEXT("a CBaseEntity row lands in the base"), Maker->Base.model, FString(TEXT("models/a.mdl")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraActorsSaveReloadTest,
	"Elysium.Substrate.InfraActors.SaveReload", GElysiumInfraTestFlags)
bool FElysiumInfraActorsSaveReloadTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this))
	{
		return false;
	}
	using ElysiumInfraActorsTests::Spawn;
	AElysiumInterestingPlaceActor* Source = Spawn<AElysiumInterestingPlaceActor>(Fixture.World, 41,
		TEXT("intersting_place"), FVector(1.0, 2.0, 3.0),
		{ TEXT("type"), TEXT("max_time"), TEXT("testflags"), TEXT("max_time") },
		{ TEXT("Dance"), TEXT("23523235.0"), TEXT("4"), TEXT(".5") });
	FElysiumInfraOutput Out;
	Out.Name = TEXT("OnNPCArrived");
	Out.Target = TEXT("relay");
	Out.Input = TEXT("Trigger");
	Out.Times = 0;
	Source->SetBakedOutputs({ Out, Out });

	TArray<uint8> Bytes;
	FObjectWriter Writer(Source, Bytes);
	AElysiumInterestingPlaceActor* Loaded = Fixture.World->SpawnActor<AElysiumInterestingPlaceActor>();
	FObjectReader Reader(Loaded, Bytes);

	TestEqual(TEXT("entity index"), Loaded->EntityIndex, 41);
	TestEqual(TEXT("authored classname"), Loaded->SourceClassname, FString(TEXT("intersting_place")));
	TestEqual(TEXT("typed row"), Loaded->Place.type, FString(TEXT("Dance")));
	TestEqual(TEXT("a repeated key leaves its last value"), Loaded->Place.max_time, 0.5f);
	TestEqual(TEXT("every authored pair, in order, repeats kept"), Loaded->AuthoredKeys.Num(), 4);
	TestEqual(TEXT("...a key with no datamap row among them"), Loaded->AuthoredKeys[2].Key, FString(TEXT("testflags")));
	TestEqual(TEXT("repeated output rows"), Loaded->Outputs.Num(), 2);
	TestEqual(TEXT("...with the authored times"), Loaded->Outputs[1].Times, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraActorsDefEmitTest,
	"Elysium.Substrate.InfraActors.DefEmit", GElysiumInfraTestFlags)
bool FElysiumInfraActorsDefEmitTest::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this))
	{
		return false;
	}
	using namespace ElysiumInfraActorsTests;
	AElysiumInterestingPlaceActor* Place = Spawn<AElysiumInterestingPlaceActor>(Fixture.World, 5,
		TEXT("intersting_place"), FVector::ZeroVector,
		{ TEXT("origin"), TEXT("max_time"), TEXT("min_bounds"), TEXT("testflags"), TEXT("max_time"), TEXT("Max_Time") },
		{ TEXT("0 0 0"), TEXT("23523235.0"), TEXT("0  0 72"), TEXT("4"), TEXT(".5"), TEXT("7") });

	TMap<FString, FString> Keys;
	Place->BuildDefKeys(Keys);
	// The entity table's fold: one slot per exact spelling, first position, last value; then the
	// map's own case-insensitive add (the two spellings of max_time become one key).
	TMap<FString, FString> Expected;
	Expected.Add(TEXT("origin"), TEXT("0 0 0"));
	Expected.Add(TEXT("max_time"), TEXT(".5"));
	Expected.Add(TEXT("min_bounds"), TEXT("0  0 72"));
	Expected.Add(TEXT("testflags"), TEXT("4"));
	Expected.Add(TEXT("Max_Time"), TEXT("7"));
	TestEqual(TEXT("an untouched actor rebuilds its keys byte for byte"), KeysText(Keys), KeysText(Expected));

	// An edit reaches the def as the typed value; an unauthored row reaches it only once non-zero.
	Place->Place.min_bounds = FVector(1.0, 2.0, 3.0);
	Place->Place.rating = 4;
	Place->BuildDefKeys(Keys);
	TestEqual(TEXT("a changed property emits its typed value"), Keys.FindRef(TEXT("min_bounds")), FString(TEXT("1 2 3")));
	TestEqual(TEXT("an untouched neighbour keeps its authored spelling"), Keys.FindRef(TEXT("testflags")), FString(TEXT("4")));
	TestEqual(TEXT("an unauthored row set in the editor is emitted"), Keys.FindRef(TEXT("rating")), FString(TEXT("4")));
	TestFalse(TEXT("an unauthored zero row is not"), Keys.Contains(TEXT("max_npcs")));

	FElysiumEntityDef Def;
	Def.Classname = TEXT("intersting_place");
	Place->TargetName = TEXT("spot");
	Place->ApplyBakedKeyvalues({ TEXT("origin"), TEXT("StartHidden") }, { TEXT("0 0 0"), TEXT("1") });
	Place->ApplyToDef(Def);
	TestEqual(TEXT("targetname"), Def.TargetName, FString(TEXT("spot")));
	TestTrue(TEXT("StartHidden is recomputed from the keys"), Def.bStartHidden);

	// The rebuild re-derives nothing: a staged row is already what retail's row parser produced,
	// so `times` travels verbatim and the authored-0 rewrite stays where 0018 story 21-7 put it,
	// in `UE_map_sidecars.split_output`. A staged row cannot carry 0 any more; if one is placed
	// here by hand, the def says what the actor says.
	FElysiumInfraOutput Out;
	Out.Name = TEXT("OnNPCArrived");
	Out.Times = 0;
	Place->SetBakedOutputs({ Out });
	Place->ApplyToDef(Def);
	TestEqual(TEXT("times travels verbatim"), Def.Outputs[0].Times, 0);
	return true;
}

namespace ElysiumInfraActorsTests
{
	// A three-row table: worldspawn, a maker, a hint row; the transport's defs for it.
	FElysiumEntityDefs MakeDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__infra_adoption__");
		FElysiumEntityDef World;
		World.Classname = TEXT("worldspawn");
		Defs.Defs.Add(World);
		FElysiumEntityDef Maker;
		Maker.Classname = TEXT("npc_maker");
		Maker.TargetName = TEXT("blueblood_maker");
		Maker.Origin = FVector(100.0, 0.0, 0.0);
		Maker.Keys.Add(TEXT("NPCType"), TEXT("npc_VHuman"));
		Maker.Keys.Add(TEXT("hint_groups"), TEXT("1 2"));
		Maker.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/thug/thug.mdl"));
		Maker.Keys.Add(TEXT("origin"), TEXT("0 0 0"));
		FElysiumOutputDef Spawned;
		Spawned.Name = TEXT("OnSpawnNPC");
		Spawned.Target = TEXT("relay");
		Spawned.Input = TEXT("Trigger");
		Spawned.Delay = 0.5f;
		Maker.Outputs.Add(Spawned);
		Maker.Outputs.Add(Spawned);
		Defs.Defs.Add(Maker);
		FElysiumEntityDef Hint;
		Hint.Classname = TEXT("info_node_patrol_point");
		Hint.Origin = FVector(0.0, 200.0, 0.0);
		Hint.Keys.Add(TEXT("hinttype"), TEXT("10000"));
		Hint.Keys.Add(TEXT("Group"), TEXT("A1"));
		Defs.Defs.Add(Hint);
		return Defs;
	}

	struct FAdoptionWorld
	{
		AElysiumNpcMakerActor* Maker = nullptr;
		AElysiumHintActor* Hint = nullptr;
		AElysiumInfraIndex* Index = nullptr;
	};

	FAdoptionWorld StandActors(UWorld* World)
	{
		FAdoptionWorld Out;
		Out.Maker = Spawn<AElysiumNpcMakerActor>(World, 1, TEXT("npc_maker"), FVector(100.0, 0.0, 0.0),
			{ TEXT("NPCType"), TEXT("hint_groups"), TEXT("model"), TEXT("origin") },
			{ TEXT("npc_VHuman"), TEXT("1 2"), TEXT("models/character/npc/common/thug/thug.mdl"), TEXT("0 0 0") });
		Out.Maker->TargetName = TEXT("blueblood_maker");
		FElysiumInfraOutput Spawned;
		Spawned.Name = TEXT("OnSpawnNPC");
		Spawned.Target = TEXT("relay");
		Spawned.Input = TEXT("Trigger");
		Spawned.Delay = 0.5f;
		Out.Maker->SetBakedOutputs({ Spawned, Spawned });
		Out.Hint = Spawn<AElysiumHintActor>(World, 2, TEXT("info_node_patrol_point"), FVector(0.0, 200.0, 0.0),
			{ TEXT("hinttype"), TEXT("Group") }, { TEXT("10000"), TEXT("A1") });
		Out.Index = World->SpawnActor<AElysiumInfraIndex>();
		Out.Index->ConfigureDeclaredSet({ 1, 2 }, { ElysiumBakedTags::InfraMaker, ElysiumBakedTags::InfraHint });
		return Out;
	}

	ElysiumInfraAdoption::FInput InputOf(const FAdoptionWorld& Stood)
	{
		ElysiumInfraAdoption::FInput Input;
		Input.Actors = { Stood.Maker, Stood.Hint };
		Input.Indices = { Stood.Index };
		return Input;
	}

	bool SameDefs(const FElysiumEntityDefs& A, const FElysiumEntityDefs& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Num(); ++i)
		{
			const FElysiumEntityDef& X = A.Defs[i];
			const FElysiumEntityDef& Y = B.Defs[i];
			if (X.Classname != Y.Classname || X.TargetName != Y.TargetName || X.Origin != Y.Origin
				|| KeysText(X.Keys) != KeysText(Y.Keys) || X.Outputs.Num() != Y.Outputs.Num())
			{
				return false;
			}
			for (int32 o = 0; o < X.Outputs.Num(); ++o)
			{
				const FElysiumOutputDef& P = X.Outputs[o];
				const FElysiumOutputDef& Q = Y.Outputs[o];
				if (P.Name != Q.Name || P.Target != Q.Target || P.Input != Q.Input || P.Param != Q.Param
					|| P.Delay != Q.Delay || P.Times != Q.Times || P.Python != Q.Python)
				{
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraAdoptionReplaceTest,
	"Elysium.Substrate.InfraAdoption.Replace", GElysiumInfraTestFlags)
bool FElysiumInfraAdoptionReplaceTest::RunTest(const FString&)
{
	using namespace ElysiumInfraActorsTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this))
	{
		return false;
	}
	FAdoptionWorld Stood = StandActors(Fixture.World);

	// Untouched actors rebuild exactly the transport's defs, at the same indices, none added.
	FElysiumEntityDefs Defs = MakeDefs();
	const FElysiumEntityDefs Before = MakeDefs();
	ElysiumInfraAdoption::FResult Result;
	FString Error;
	TestTrue(TEXT("the declared set adopts"), ElysiumInfraAdoption::Apply(InputOf(Stood), Defs, Result, Error));
	TestEqual(TEXT("...with no error"), Error, FString());
	TestTrue(TEXT("...active"), Result.bActive);
	TestEqual(TEXT("...rewriting both declared rows"), Result.Replaced, 2);
	// `FElysiumNpcMaker` builds each child from its own def's keys and output rows
	// (`ElysiumNpcMaker.cpp`, the child template), so equal maker defs spawn equal children.
	TestTrue(TEXT("untouched actors rebuild the transport's defs exactly, output rows included"),
		SameDefs(Defs, Before));

	// The asset transport's defs take the same pass.
	UElysiumMapEntities* Asset = NewObject<UElysiumMapEntities>(GetTransientPackage(), NAME_None, RF_Transient);
	for (const FElysiumEntityDef& Def : Before.Defs)
	{
		FElysiumMapEntityRow Row;
		Row.Classname = Def.Classname;
		Row.TargetName = Def.TargetName;
		Row.Origin = Def.Origin;
		Row.Keys = Def.Keys;
		Asset->Entities.Add(Row);
	}
	FElysiumEntityDefs FromAsset;
	Asset->Deserialize(FromAsset, 1.f, FVector::ZeroVector);
	TestTrue(TEXT("the asset transport adopts too"), ElysiumInfraAdoption::Apply(InputOf(Stood), FromAsset, Result, Error));
	TestTrue(TEXT("...to the same defs"), SameDefs(FromAsset, Before));

	// An edit reaches its own row and no other.
	Stood.Maker->Maker.NPCType = TEXT("npc_VCop");
	FElysiumEntityDefs Edited = MakeDefs();
	ElysiumInfraAdoption::Apply(InputOf(Stood), Edited, Result, Error);
	TestEqual(TEXT("the edited maker's def carries the edit"), Edited.Defs[1].Keys.FindRef(TEXT("NPCType")),
		FString(TEXT("npc_VCop")));
	TestEqual(TEXT("the table keeps its length"), Edited.Num(), Before.Num());
	TestEqual(TEXT("the hint row is untouched"), KeysText(Edited.Defs[2].Keys), KeysText(Before.Defs[2].Keys));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraAdoptionRefusalsTest,
	"Elysium.Substrate.InfraAdoption.Refusals", GElysiumInfraTestFlags)
bool FElysiumInfraAdoptionRefusalsTest::RunTest(const FString&)
{
	using namespace ElysiumInfraActorsTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this))
	{
		return false;
	}
	FAdoptionWorld Stood = StandActors(Fixture.World);
	ElysiumInfraAdoption::FResult Result;
	FString Error;
	auto Refuses = [&](const TCHAR* What, const ElysiumInfraAdoption::FInput& Input, const TCHAR* Needle)
	{
		FElysiumEntityDefs Defs = MakeDefs();
		const bool bAdopted = ElysiumInfraAdoption::Apply(Input, Defs, Result, Error);
		TestFalse(*FString::Printf(TEXT("%s is refused"), What), bAdopted);
		TestTrue(*FString::Printf(TEXT("%s: the error names it (%s)"), What, *Error), Error.Contains(Needle));
		TestTrue(*FString::Printf(TEXT("%s: the defs are untouched"), What), SameDefs(Defs, MakeDefs()));
	};

	ElysiumInfraAdoption::FInput Missing = InputOf(Stood);
	Missing.Actors = { Stood.Maker };
	Refuses(TEXT("a declared row with no actor"), Missing, TEXT("declared, no actor"));

	ElysiumInfraAdoption::FInput Twice = InputOf(Stood);
	AElysiumHintActor* Second = Spawn<AElysiumHintActor>(Fixture.World, 2, TEXT("info_node_patrol_point"),
		FVector(0.0, 200.0, 0.0), {}, {});
	Twice.Actors.Add(Second);
	Refuses(TEXT("two actors on one row"), Twice, TEXT("adopted twice"));

	AElysiumHintActor* Wrong = Spawn<AElysiumHintActor>(Fixture.World, 2, TEXT("info_node_cover_low"),
		FVector(0.0, 200.0, 0.0), {}, {});
	ElysiumInfraAdoption::FInput Mismatch = InputOf(Stood);
	Mismatch.Actors = { Stood.Maker, Wrong };
	Refuses(TEXT("a classname the table does not have"), Mismatch, TEXT("table has"));


	AElysiumInfraIndex* Wide = Fixture.World->SpawnActor<AElysiumInfraIndex>();
	Wide->ConfigureDeclaredSet({ 1, 2, 99 }, { ElysiumBakedTags::InfraMaker, ElysiumBakedTags::InfraHint,
		ElysiumBakedTags::InfraNpc });
	ElysiumInfraAdoption::FInput OutOfRange = InputOf(Stood);
	OutOfRange.Indices = { Wide };
	Refuses(TEXT("a declared index outside the table"), OutOfRange, TEXT("outside the"));

	ElysiumInfraAdoption::FInput Orphans = InputOf(Stood);
	Orphans.Indices.Reset();
	Refuses(TEXT("actors with no declared-set index"), Orphans, TEXT("no declared-set index"));

	// A level baked before the lane: nothing declared, nothing standing, nothing changes.
	FElysiumEntityDefs Defs = MakeDefs();
	TestTrue(TEXT("a level with no infrastructure adopts nothing"),
		ElysiumInfraAdoption::Apply(ElysiumInfraAdoption::FInput(), Defs, Result, Error));
	TestFalse(TEXT("...inactive"), Result.bActive);
	TestTrue(TEXT("...and the defs are the transport's"), SameDefs(Defs, MakeDefs()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraAdoptionMovedTest,
	"Elysium.Substrate.InfraAdoption.Moved", GElysiumInfraTestFlags)
bool FElysiumInfraAdoptionMovedTest::RunTest(const FString&)
{
	using namespace ElysiumInfraActorsTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this))
	{
		return false;
	}
	FAdoptionWorld Stood = StandActors(Fixture.World);
	// A designer dragged the patrol point in the editor (a game world will not move a static root,
	// so the moved actor is stood where the drag left it): the entity follows it.
	AElysiumHintActor* Dragged = Spawn<AElysiumHintActor>(Fixture.World, 2, TEXT("info_node_patrol_point"),
		FVector(254.0, -508.0, 25.4), { TEXT("hinttype"), TEXT("Group") }, { TEXT("10000"), TEXT("A1") });
	ElysiumInfraAdoption::FInput Input = InputOf(Stood);
	Input.Actors = { Stood.Maker, Dragged };
	FElysiumEntityDefs Defs = MakeDefs();
	ElysiumInfraAdoption::FResult Result;
	FString Error;
	TestTrue(TEXT("a moved actor is adopted, not refused"),
		ElysiumInfraAdoption::Apply(Input, Defs, Result, Error));
	TestEqual(TEXT("...and counted as moved"), Result.Moved, 1);
	TestEqual(TEXT("the def stands where the actor stands"), Defs.Defs[2].Origin, FVector(254.0, -508.0, 25.4));
	TestEqual(TEXT("...and its origin keyvalue says so in Source inches, Y negated"),
		Defs.Defs[2].Keys.FindRef(TEXT("origin")), FString(TEXT("100 200 10")));
	TestEqual(TEXT("an unmoved actor keeps the table's origin"), Defs.Defs[1].Origin, FVector(100.0, 0.0, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraAdoptionReadinessTest,
	"Elysium.Substrate.InfraAdoption.Readiness", GElysiumInfraTestFlags)
bool FElysiumInfraAdoptionReadinessTest::RunTest(const FString&)
{
	// A refused level builds no entity world; its own arm names why, ahead of the generic one.
	FElysiumMapRuntimePrerequisites P;
	P.bConstructionComplete = true;
	P.bInfrastructureFailed = true;
	P.bMenuBackdrop = true;
	FString Failure;
	TestTrue(TEXT("a refused infrastructure fails the runtime"),
		P.Evaluate(Failure) == EElysiumMapReadinessResult::Failed);
	TestTrue(TEXT("...naming the infrastructure, not the missing substrate"),
		Failure.Contains(TEXT("AI infrastructure")));
	TestTrue(TEXT("Missing names it"), P.Missing().Contains(TEXT("baked AI infrastructure failed")));

	FElysiumMapRuntimePrerequisites Nav;
	Nav.bNavigationFailed = true;
	Nav.bNavigationRequired = true;
	TestFalse(TEXT("a failed navigation is not also reported as building"),
		Nav.Missing().Contains(TEXT("runtime navigation building")));
	return true;
}

#endif
