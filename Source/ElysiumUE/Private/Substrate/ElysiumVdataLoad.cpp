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
		const FString Path = FElysiumContentPaths::VdataFile(Rel);
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			OutError = FString::Printf(TEXT("not found: %s"), *Path);
			return false;
		}
		OutRoot = ElysiumKeyValues::ParseText(Raw);
		if (!OutRoot.IsValid())
		{
			OutError = FString::Printf(TEXT("empty or unparseable: %s"), *Path);
			return false;
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
