#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityWorld.h"

// The `retail_site` tap (`docs/specs/layers/harness.md`, `Arena/README.md` § The test instrument): a port
// line that carries a retail address reports the semantic event a record measures -- `entry`, a
// branch, a write, `return` -- with the retail values at that point. A pure function (a sound level,
// the KeyValues reader) has no world and no entity of its own, so it reports through this sink and
// the caller decides who the event is about: the entity whose Spawn called it, or a named utility.
// Null sink: no event, no cost beyond the null test.
struct IElysiumRetailSiteSink
{
	virtual ~IElysiumRetailSiteSink() = default;
	virtual void Site(const TCHAR* Tag, const TCHAR* RetailFn, uint32 RetailVa, const TCHAR* Phase,
		const FString& Payload) = 0;
};

// The sink for an event about one entity: `FElysiumEntityWorld::EmitRetailSite(Entity, ...)`. A null
// world (an entity spawned outside a world, as some tests do) reports nothing.
struct FElysiumEntityRetailSites final : public IElysiumRetailSiteSink
{
	FElysiumEntityRetailSites(FElysiumEntityWorld* InWorld, const FElysiumEntity& InEntity)
		: World(InWorld), Entity(InEntity) {}

	virtual void Site(const TCHAR* Tag, const TCHAR* RetailFn, uint32 RetailVa, const TCHAR* Phase,
		const FString& Payload) override
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(Entity, Tag, RetailFn, RetailVa, Phase, Payload);
		}
	}

	FElysiumEntityWorld* World;
	const FElysiumEntity& Entity;
};

// The sink for a utility with no entity (the KeyValues reader driven by a record): the event names
// `Name` in the trace's entity column and carries no handle.
struct FElysiumNamedRetailSites final : public IElysiumRetailSiteSink
{
	FElysiumNamedRetailSites(FElysiumEntityWorld& InWorld, const FString& InName)
		: World(InWorld), Name(InName) {}

	virtual void Site(const TCHAR* Tag, const TCHAR* RetailFn, uint32 RetailVa, const TCHAR* Phase,
		const FString& Payload) override
	{
		World.EmitRetailSite(Name, Tag, RetailFn, RetailVa, Phase, Payload);
	}

	FElysiumEntityWorld& World;
	FString Name;
};
