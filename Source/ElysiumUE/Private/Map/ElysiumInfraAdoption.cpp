#include "Map/ElysiumInfraAdoption.h"

#include "AiInfra/ElysiumInfraActor.h"
#include "AiInfra/ElysiumInfraIndex.h"
#include "ElysiumBakedTags.h"
#include "ElysiumEntityDefs.h"

namespace ElysiumInfraAdoption
{
	bool Apply(const FInput& Input, FElysiumEntityDefs& Defs, FResult& OutResult, FString& OutError)
	{
		OutResult = FResult();
		OutError.Reset();
		TArray<FString> Problems;

		if (Input.Indices.Num() == 0)
		{
			// A level baked before the infrastructure lane: nothing is declared, so nothing is
			// adopted, and an infrastructure actor standing without the index is itself a problem.
			if (Input.Actors.Num() > 0 || Input.StrayCount > 0)
			{
				OutError = FString::Printf(TEXT("%d infrastructure actor(s) and no declared-set index"),
					Input.Actors.Num() + Input.StrayCount);
				return false;
			}
			return true;
		}
		OutResult.bActive = true;
		if (Input.Indices.Num() > 1)
		{
			Problems.Add(FString::Printf(TEXT("%d declared-set index actors (expected 1)"), Input.Indices.Num()));
		}
		if (Input.StrayCount > 0)
		{
			Problems.Add(FString::Printf(TEXT("%d actor(s) carry an infrastructure tag but are no infrastructure class"),
				Input.StrayCount));
		}
		const AElysiumInfraIndex* Index = Input.Indices[0];

		// Every actor, by the entity index it claims.
		TMap<int32, const AElysiumInfraActor*> ByIndex;
		for (const AElysiumInfraActor* Actor : Input.Actors)
		{
			if (Actor == nullptr)
			{
				continue;
			}
			const int32 Tagged = ElysiumBakedTags::ParseEntityIndex(Actor->Tags);
			if (Tagged != Actor->EntityIndex)
			{
				Problems.Add(FString::Printf(TEXT("%s: tag says entity %d, property says %d"),
					*Actor->GetName(), Tagged, Actor->EntityIndex));
				continue;
			}
			if (!Actor->ActorHasTag(Actor->FamilyTag()))
			{
				Problems.Add(FString::Printf(TEXT("entity %d: %s does not carry its family tag %s"),
					Actor->EntityIndex, *Actor->GetClass()->GetName(), *Actor->FamilyTag().ToString()));
			}
			if (const AElysiumInfraActor** Existing = ByIndex.Find(Actor->EntityIndex))
			{
				Problems.Add(FString::Printf(TEXT("entity %d: adopted twice (%s, %s)"),
					Actor->EntityIndex, *(*Existing)->GetName(), *Actor->GetName()));
				continue;
			}
			ByIndex.Add(Actor->EntityIndex, Actor);
		}

		// The declared set, exactly.
		TSet<int32> Declared;
		for (int32 i = 0; i < Index->DeclaredIndices.Num(); ++i)
		{
			const int32 Entity = Index->DeclaredIndices[i];
			Declared.Add(Entity);
			if (!Defs.Defs.IsValidIndex(Entity))
			{
				Problems.Add(FString::Printf(TEXT("entity %d: outside the %d-row entity table"), Entity, Defs.Num()));
				continue;
			}
			const AElysiumInfraActor* const* Found = ByIndex.Find(Entity);
			if (Found == nullptr)
			{
				Problems.Add(FString::Printf(TEXT("entity %d (%s): declared, no actor"), Entity,
					*Defs.Defs[Entity].Classname));
				continue;
			}
			const AElysiumInfraActor* Actor = *Found;
			const FName Family = Index->DeclaredFamilies.IsValidIndex(i) ? Index->DeclaredFamilies[i] : NAME_None;
			if (Actor->FamilyTag() != Family)
			{
				Problems.Add(FString::Printf(TEXT("entity %d: declared %s, adopted as %s"), Entity,
					*Family.ToString(), *Actor->FamilyTag().ToString()));
			}
			const FElysiumEntityDef& Def = Defs.Defs[Entity];
			if (!Def.Classname.Equals(Actor->SourceClassname, ESearchCase::IgnoreCase))
			{
				Problems.Add(FString::Printf(TEXT("entity %d: table has %s, actor authored %s"), Entity,
					*Def.Classname, *Actor->SourceClassname));
			}
			const double Gap = FVector::Dist(Actor->GetActorLocation(), Def.Origin);
			if (Gap > OriginToleranceCm)
			{
				Problems.Add(FString::Printf(TEXT("entity %d: actor stands %.2f cm from its def"), Entity, Gap));
			}
		}
		for (const TPair<int32, const AElysiumInfraActor*>& Pair : ByIndex)
		{
			if (!Declared.Contains(Pair.Key))
			{
				Problems.Add(FString::Printf(TEXT("entity %d: actor %s is not in the declared set"),
					Pair.Key, *Pair.Value->GetName()));
			}
		}

		if (Problems.Num() > 0)
		{
			OutError = FString::Printf(TEXT("%d problem(s): %s"), Problems.Num(),
				*FString::Join(Problems, TEXT("; ")));
			return false;
		}

		// Only now does anything change.
		for (const int32 Entity : Index->DeclaredIndices)
		{
			ByIndex.FindChecked(Entity)->ApplyToDef(Defs.Defs[Entity]);
			++OutResult.Replaced;
		}
		return true;
	}
}
