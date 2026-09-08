#pragma once

// A map slice: the real `<map>.ents` cut down to a named set of entities plus everything their
// authored rows reach, parsed through the production parser and ready for a headless
// `FElysiumEntityWorld`. The authored rows are what the game loads, so a map/port mismatch is
// caught by the slice test and nowhere later.
//
// The helper is generic — map name plus targetnames — so every tutorial beat (terminal, elevator,
// lockpicks, fan, Jack's teleports) builds its fixture the same way. It is skipped, never failed,
// without the export root, like the other content tests.
//
// An entity in the slice that needs something the headless world cannot give it — a brush body
// for a touch or look trigger, a level-script Python namespace for a field-6 call, a target that
// is not in the map — is a named seam in `Seams`, never a silent pass. The test prints them.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "Misc/Paths.h"

struct FElysiumMapSlice
{
	FString MapName;
	// The slice in `.ents` order, worldspawn first (its `levelscript` names the hub module).
	FElysiumEntityDefs Defs;
	// Targetnames kept, in `.ents` order. Unnamed entities reached through a wildcard are listed
	// by classname.
	TArray<FString> Kept;
	// What the headless world cannot complete for this slice, one line each.
	TArray<FString> Seams;

	static FString EntsPath(const FString& Map)
	{
		return FElysiumContentPaths::Root() / Map / (Map + TEXT(".ents"));
	}

	static bool Available(const FString& Map)
	{
		return FElysiumContentPaths::IsConfigured() && FPaths::FileExists(EntsPath(Map));
	}

	// Keep `Roots` and the closure of every output row's target, every `parentname` and every
	// `target` key, transitively. Targetname comparison is case-insensitive; a trailing `*` on a
	// row's target is VtMB's prefix wildcard (`CGlobalEntityList::FindEntityByName`). A root that
	// names no entity is a seam, not an error — `item_k_*` inventory classnames are named in a
	// slice for the reader's sake and live in a container's `equipN`, not in the map.
	static bool Build(const FString& Map, const TArray<FString>& Roots, FElysiumMapSlice& Out,
		FString& OutError)
	{
		Out = FElysiumMapSlice();
		Out.MapName = Map;
		FElysiumEntityDefs All;
		if (!FElysiumEntityDefs::Parse(EntsPath(Map), All))
		{
			OutError = FString::Printf(TEXT("could not parse %s"), *EntsPath(Map));
			return false;
		}

		auto Matches = [](const FString& Name, const FString& Pattern)
		{
			if (Pattern.IsEmpty() || Name.IsEmpty())
			{
				return false;
			}
			if (Pattern.EndsWith(TEXT("*")))
			{
				return Name.StartsWith(Pattern.LeftChop(1), ESearchCase::IgnoreCase);
			}
			return Name.Equals(Pattern, ESearchCase::IgnoreCase);
		};
		auto IsProcedural = [](const FString& Target)
		{
			return Target.StartsWith(TEXT("!"));   // !self / !activator / !player / !caller
		};

		TArray<bool> Keep;
		Keep.SetNumZeroed(All.Num());
		TArray<FString> Pending = Roots;
		TSet<FString> Requested;
		while (!Pending.IsEmpty())
		{
			const FString Pattern = Pending.Pop(EAllowShrinking::No);
			if (Pattern.IsEmpty() || IsProcedural(Pattern))
			{
				continue;
			}
			bool bAlready = false;
			Requested.Add(Pattern.ToLower(), &bAlready);
			if (bAlready)
			{
				continue;
			}
			bool bFound = false;
			for (int32 Index = 0; Index < All.Num(); ++Index)
			{
				const FElysiumEntityDef& Def = All.Defs[Index];
				if (!Matches(Def.TargetName, Pattern))
				{
					continue;
				}
				bFound = true;
				if (Keep[Index])
				{
					continue;
				}
				Keep[Index] = true;
				for (const FElysiumOutputDef& Row : Def.Outputs)
				{
					Pending.Add(Row.Target);
				}
				if (const FString* Parent = Def.Keys.Find(TEXT("parentname")))
				{
					Pending.Add(*Parent);
				}
				if (const FString* Target = Def.Keys.Find(TEXT("target")))
				{
					Pending.Add(*Target);
				}
			}
			if (!bFound)
			{
				Out.Seams.Add(FString::Printf(TEXT("'%s' names no entity in %s"),
					*Pattern, *Map));
			}
		}

		const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
		Out.Defs.MapName = All.MapName;
		Out.Defs.SkyScale = All.SkyScale;
		Out.Defs.SkyOrigin = All.SkyOrigin;
		for (int32 Index = 0; Index < All.Num(); ++Index)
		{
			const FElysiumEntityDef& Def = All.Defs[Index];
			const bool bWorldspawn = Def.Classname == TEXT("worldspawn");
			if (!Keep[Index] && !bWorldspawn)
			{
				continue;
			}
			if (!bWorldspawn)
			{
				const FString Label = Def.TargetName.IsEmpty()
					? FString::Printf(TEXT("<%s>"), *Def.Classname) : Def.TargetName;
				Out.Kept.Add(Label);
				if (Registry.Find(FName(*Def.Classname)) == nullptr)
				{
					Out.Seams.Add(FString::Printf(TEXT("%s: class %s is not registered (inert record)"),
						*Label, *Def.Classname));
				}
				if (Def.IsBrush())
				{
					Out.Seams.Add(FString::Printf(
						TEXT("%s: brush %s has no body headless; touch/look never fires"),
						*Label, *Def.Classname));
				}
				for (const FElysiumOutputDef& Row : Def.Outputs)
				{
					if (!Row.Python.IsEmpty())
					{
						Out.Seams.Add(FString::Printf(
							TEXT("%s.%s: field-6 python '%s' needs the level-script namespace"),
							*Label, *Row.Name, *Row.Python));
					}
					if (!Row.Target.IsEmpty() && !IsProcedural(Row.Target))
					{
						bool bResolves = false;
						for (int32 Other = 0; Other < All.Num() && !bResolves; ++Other)
						{
							bResolves = Matches(All.Defs[Other].TargetName, Row.Target);
						}
						if (!bResolves)
						{
							Out.Seams.Add(FString::Printf(
								TEXT("%s.%s -> '%s' names no entity in %s (legal no-op)"),
								*Label, *Row.Name, *Row.Target, *Map));
						}
					}
				}
			}
			Out.Defs.Defs.Add(Def);
		}
		return true;
	}

	FString Report() const
	{
		FString Text = FString::Printf(TEXT("map slice %s: %d entities [%s]"), *MapName,
			Kept.Num(), *FString::Join(Kept, TEXT(", ")));
		for (const FString& Seam : Seams)
		{
			Text += TEXT("\n  seam: ") + Seam;
		}
		return Text;
	}
};

#endif   // WITH_DEV_AUTOMATION_TESTS
