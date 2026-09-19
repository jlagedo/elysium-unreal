#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AiInfra/ElysiumConversationPlaceActor.h"
#include "AiInfra/ElysiumHintActor.h"
#include "AiInfra/ElysiumInfraIndex.h"
#include "AiInfra/ElysiumInterestingPlaceActor.h"
#include "AiInfra/ElysiumNpcMakerActor.h"
#include "ElysiumBakedTags.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMapEntities.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Map/ElysiumInfraAdoption.h"

// 0018 story 2's bake goals, read off the baked levels themselves (the `NavJumpLink.Tutorial`
// pattern): the family counts the stage pins (`pipeline/tests/test_map_ai_infra.py`), one actor per
// declared entity, spot checks against authored values, and def parity — the defs the actors
// rebuild are byte-equal to the entity table's at the same indices, so adopting a level that
// nobody edited changes no entity.

static constexpr EAutomationTestFlags GElysiumInfraContentFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumInfraContentTests
{
	struct FPins
	{
		int32 Hints = 0;
		int32 Patrol = 0;
		int32 Places = 0;
		int32 Conversations = 0;
		int32 Makers = 0;
		int32 Npcs = 0;
	};

	struct FBaked
	{
		UWorld* World = nullptr;
		TArray<AElysiumInfraActor*> Actors;
		TArray<AElysiumInfraIndex*> Indices;
	};

	bool LoadBaked(FAutomationTestBase& Test, const TCHAR* Map, FBaked& Out)
	{
		const FString Package = FElysiumContentPaths::BakedLevel(Map);
		Out.World = LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + Map));
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s baked level exists"), Map), Out.World)
			|| !Test.TestNotNull(TEXT("persistent level"), Out.World->PersistentLevel.Get()))
		{
			return false;
		}
		for (AActor* Actor : Out.World->PersistentLevel->Actors)
		{
			if (AElysiumInfraActor* Infra = Cast<AElysiumInfraActor>(Actor))
			{
				Out.Actors.Add(Infra);
			}
			else if (AElysiumInfraIndex* Index = Cast<AElysiumInfraIndex>(Actor))
			{
				Out.Indices.Add(Index);
			}
		}
		return true;
	}

	void CheckLevel(FAutomationTestBase& Test, const TCHAR* Map, const FPins& Pins, FBaked& Baked)
	{
		TMap<FName, int32> ByFamily;
		TSet<int32> Seen;
		int32 Patrol = 0;
		for (const AElysiumInfraActor* Actor : Baked.Actors)
		{
			++ByFamily.FindOrAdd(Actor->FamilyTag());
			Test.TestFalse(*FString::Printf(TEXT("%s: entity %d stands once"), Map, Actor->EntityIndex),
				Seen.Contains(Actor->EntityIndex));
			Seen.Add(Actor->EntityIndex);
			Test.TestTrue(*FString::Printf(TEXT("%s: entity %d carries its family and index tags"), Map,
				Actor->EntityIndex), Actor->ActorHasTag(Actor->FamilyTag())
				&& ElysiumBakedTags::ParseEntityIndex(Actor->Tags) == Actor->EntityIndex);
			Patrol += Actor->SourceClassname.Equals(TEXT("info_node_patrol_point"), ESearchCase::IgnoreCase) ? 1 : 0;
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: hints"), Map), ByFamily.FindRef(ElysiumBakedTags::InfraHint), Pins.Hints);
		Test.TestEqual(*FString::Printf(TEXT("%s: of which patrol points"), Map), Patrol, Pins.Patrol);
		Test.TestEqual(*FString::Printf(TEXT("%s: places"), Map), ByFamily.FindRef(ElysiumBakedTags::InfraPlace), Pins.Places);
		Test.TestEqual(*FString::Printf(TEXT("%s: conversation places"), Map),
			ByFamily.FindRef(ElysiumBakedTags::InfraConversation), Pins.Conversations);
		Test.TestEqual(*FString::Printf(TEXT("%s: makers"), Map), ByFamily.FindRef(ElysiumBakedTags::InfraMaker), Pins.Makers);
		Test.TestEqual(*FString::Printf(TEXT("%s: placed NPCs"), Map), ByFamily.FindRef(ElysiumBakedTags::InfraNpc), Pins.Npcs);
		if (Test.TestEqual(*FString::Printf(TEXT("%s: one declared-set index"), Map), Baked.Indices.Num(), 1))
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: the index declares every actor"), Map),
				Baked.Indices[0]->DeclaredIndices.Num(), Baked.Actors.Num());
		}

		// Def parity: the entity table as the transport loads it, adopted, is unchanged.
		FElysiumEntityDefs Defs;
		if (!Test.TestTrue(*FString::Printf(TEXT("%s: the entity table loads"), Map),
			ElysiumEntityDefSource::Load(Map, Defs) != EElysiumEntityDefSource::None))
		{
			return;
		}
		FElysiumEntityDefs Before;
		Before.Defs = Defs.Defs;
		ElysiumInfraAdoption::FInput Input;
		for (AElysiumInfraActor* Actor : Baked.Actors)
		{
			Input.Actors.Add(Actor);
		}
		for (AElysiumInfraIndex* Index : Baked.Indices)
		{
			Input.Indices.Add(Index);
		}
		ElysiumInfraAdoption::FResult Result;
		FString Error;
		Test.TestTrue(*FString::Printf(TEXT("%s: the baked set adopts (%s)"), Map, *Error),
			ElysiumInfraAdoption::Apply(Input, Defs, Result, Error));
		Test.TestEqual(*FString::Printf(TEXT("%s: every declared def rewritten"), Map), Result.Replaced, Baked.Actors.Num());
		Test.TestEqual(*FString::Printf(TEXT("%s: the table keeps its length"), Map), Defs.Num(), Before.Num());
		int32 Diverged = 0;
		for (int32 i = 0; i < Defs.Num() && i < Before.Num(); ++i)
		{
			const FElysiumEntityDef& A = Defs.Defs[i];
			const FElysiumEntityDef& B = Before.Defs[i];
			bool bSame = A.Classname == B.Classname && A.TargetName.Equals(B.TargetName, ESearchCase::CaseSensitive)
				&& A.bStartHidden == B.bStartHidden && A.Keys.Num() == B.Keys.Num() && A.Outputs.Num() == B.Outputs.Num();
			if (bSame)
			{
				auto ItA = A.Keys.CreateConstIterator();
				auto ItB = B.Keys.CreateConstIterator();
				for (; ItA && ItB; ++ItA, ++ItB)
				{
					bSame &= ItA->Key.Equals(ItB->Key, ESearchCase::CaseSensitive)
						&& ItA->Value.Equals(ItB->Value, ESearchCase::CaseSensitive);
				}
				for (int32 o = 0; bSame && o < A.Outputs.Num(); ++o)
				{
					const FElysiumOutputDef& X = A.Outputs[o];
					const FElysiumOutputDef& Y = B.Outputs[o];
					bSame &= X.Name == Y.Name && X.Target == Y.Target && X.Input == Y.Input
						&& X.Param.Equals(Y.Param, ESearchCase::CaseSensitive) && X.Delay == Y.Delay
						&& X.Times == Y.Times && X.Python.Equals(Y.Python, ESearchCase::CaseSensitive);
				}
			}
			if (!bSame)
			{
				++Diverged;
				if (Diverged <= 5)
				{
					Test.AddError(FString::Printf(TEXT("%s: entity %d (%s) rebuilt differently"), Map, i, *B.Classname));
				}
			}
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: rebuilt defs byte-equal the entity table's"), Map), Diverged, 0);
	}

	template <typename TActor>
	TActor* FindBy(FBaked& Baked, TFunctionRef<bool(const TActor&)> Pred)
	{
		for (AElysiumInfraActor* Actor : Baked.Actors)
		{
			TActor* Typed = Cast<TActor>(Actor);
			if (Typed != nullptr && Pred(*Typed))
			{
				return Typed;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraContentTutorialTest,
	"Elysium.Content.InfraActors.Tutorial", GElysiumInfraContentFlags)
bool FElysiumInfraContentTutorialTest::RunTest(const FString&)
{
	using namespace ElysiumInfraContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, TEXT("sp_tutorial_1"), Baked))
	{
		return false;
	}
	FPins Pins;
	Pins.Hints = 49;
	Pins.Patrol = 37;
	Pins.Places = 29;
	Pins.Makers = 14;
	Pins.Npcs = 20;
	CheckLevel(*this, TEXT("sp_tutorial_1"), Pins, Baked);

	// `pt1`: the thug's interesting place at his spawn (spec 0018 § Witness data).
	const AElysiumInterestingPlaceActor* Pt1 = FindBy<AElysiumInterestingPlaceActor>(Baked,
		[](const AElysiumInterestingPlaceActor& Place) { return Place.TargetName == TEXT("pt1"); });
	if (TestNotNull(TEXT("pt1 stands"), Pt1))
	{
		TestEqual(TEXT("pt1 group_id"), Pt1->Place.group_id, 2);
		TestTrue(TEXT("pt1 enabled"), Pt1->Place.enabled);
		TestEqual(TEXT("pt1 min_time"), Pt1->Place.min_time, 30.0f);
		TestEqual(TEXT("pt1 max_time"), Pt1->Place.max_time, 60.0f);
	}
	const AElysiumNpcMakerActor* Blueblood = FindBy<AElysiumNpcMakerActor>(Baked,
		[](const AElysiumNpcMakerActor& Maker) { return Maker.TargetName == TEXT("blueblood_maker"); });
	TestTrue(TEXT("blueblood_maker stands with its NPCType"), Blueblood != nullptr && !Blueblood->Maker.NPCType.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraContentHubTest,
	"Elysium.Content.InfraActors.Hub", GElysiumInfraContentFlags)
bool FElysiumInfraContentHubTest::RunTest(const FString&)
{
	using namespace ElysiumInfraContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, TEXT("sm_hub_1"), Baked))
	{
		return false;
	}
	FPins Pins;
	Pins.Hints = 274;
	Pins.Patrol = 34;
	Pins.Places = 76;
	Pins.Makers = 48;
	Pins.Npcs = 38;
	CheckLevel(*this, TEXT("sm_hub_1"), Pins, Baked);
	const AElysiumHintActor* Patrol = FindBy<AElysiumHintActor>(Baked,
		[](const AElysiumHintActor& Hint) { return Hint.Hint.HintType == 10000 && !Hint.Hint.Group.IsEmpty(); });
	TestNotNull(TEXT("a patrol point stands with its Group"), Patrol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInfraContentSoc3Test,
	"Elysium.Content.InfraActors.Soc3", GElysiumInfraContentFlags)
bool FElysiumInfraContentSoc3Test::RunTest(const FString&)
{
	using namespace ElysiumInfraContentTests;
	FBaked Baked;
	if (!LoadBaked(*this, TEXT("sp_soc_3"), Baked))
	{
		return false;
	}
	FPins Pins;
	Pins.Hints = 40;
	Pins.Patrol = 25;
	Pins.Places = 13;
	Pins.Conversations = 2;
	Pins.Npcs = 15;
	CheckLevel(*this, TEXT("sp_soc_3"), Pins, Baked);
	const AElysiumConversationPlaceActor* Conversation = FindBy<AElysiumConversationPlaceActor>(Baked,
		[](const AElysiumConversationPlaceActor&) { return true; });
	TestTrue(TEXT("a conversation place stands with the places it names"),
		Conversation != nullptr && !Conversation->Conversation.interesting_places.IsEmpty());
	// `d1` is authored twice (rows 355 and 362); retail's first match in hint-list order is the later.
	const AElysiumHintActor* D1 = FindBy<AElysiumHintActor>(Baked,
		[](const AElysiumHintActor& Hint) { return Hint.EntityIndex == 362; });
	TestTrue(TEXT("row 362 is a d1 patrol point"), D1 != nullptr && D1->Hint.Group == TEXT("d1"));
	return true;
}

#endif
