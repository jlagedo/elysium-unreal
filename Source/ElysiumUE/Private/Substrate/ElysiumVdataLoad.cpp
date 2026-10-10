#include "Substrate/ElysiumVdataLoad.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumRulebook.h"

#include "Misc/FileHelper.h"

namespace ElysiumVdata
{
	using ElysiumKeyValues::FKvNode;

	bool ReadVdata(const TCHAR* Rel, TSharedPtr<FKvNode>& OutRoot, FString& OutError)
	{
		// `0x101f2e20(name, fs)` on a cache miss: a node named after the file (`ctor(name, 1)`), then
		// `0x101f2180` with that node as the target (ECX). R1..R6 of `0x101f2180`: Open `rb`
		// (`0x105596CC`), Size, `malloc(size + 1)`, Read, Close, `buf[size] = 0`; a failed Open is the
		// false return. The text arrives as TCHARs here (`ElysiumKeyValues.h`'s representation note).
		const FString Path = FElysiumContentPaths::VdataFile(Rel);
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			OutError = FString::Printf(TEXT("not found: %s"), *Path);
			return false;
		}
		TSharedPtr<FKvNode> Target = MakeShared<FKvNode>();
		ElysiumKeyValues::SetName(*Target, Rel);
		ElysiumKeyValues::FKvReader Reader(Raw);
		TArray<TSharedPtr<FKvNode>> Roots;
		ElysiumKeyValues::ParseRoots(Reader, Path, Target, Roots);
		// Retail returns 1 for an empty file too (the target keeps the requested name and has no
		// children): the consumer's root lookup is what fails. The view holds the root chain.
		OutRoot = ElysiumKeyValues::RootsView(Roots);
		if (!OutRoot.IsValid())
		{
			OutRoot = MakeShared<FKvNode>();
		}
		return true;
	}

	const FKvNode* RootBlock(const TSharedPtr<FKvNode>& Root, const TCHAR* Key,
		const TCHAR* Rel, FString& OutError)
	{
		const FKvNode* Block = Root.IsValid() ? Root->Child(Key) : nullptr;
		if (Block == nullptr)
		{
			OutError = FString::Printf(TEXT("no %s block in %s"), Key,
				*FElysiumContentPaths::VdataFile(Rel));
		}
		return Block;
	}

	void Index(TMap<FString, int32>& Map, const FString& Key, int32 Value)
	{
		if (!Key.IsEmpty())
		{
			Map.Add(ElysiumFold(Key), Value);
		}
	}
}
