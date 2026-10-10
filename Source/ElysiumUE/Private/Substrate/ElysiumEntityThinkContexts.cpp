#include "ElysiumEntity.h"

#include "ElysiumEntityWorld.h"

int32 FElysiumEntity::GetIndexForContext(const ANSICHAR* ContextName) const
{
	// CBaseEntity::GetIndexForContext 0x100ac250: first match, plain CRT strncmp, 32 bytes.
	// A NUL terminates the comparison; casing is significant. NULL is not a supported input.
	int32 Result = -1;
	for (int32 ContextIndex = 0; ContextIndex < ThinkContexts.Num(); ++ContextIndex)
	{
		if (FCStringAnsi::Strncmp(ThinkContexts[ContextIndex].Name, ContextName, 0x20) == 0)
		{
			Result = ContextIndex;
			break;
		}
	}
	if (World)
	{
		World->EmitRetailSite(*this, TEXT("think_context"), TEXT("CBaseEntity::GetIndexForContext"),
			0x100ac250u, TEXT("return"), FString::Printf(TEXT("name=%s index=%d count=%d"),
				ANSI_TO_TCHAR(ContextName), Result, ThinkContexts.Num()));
	}
	return Result;
}

int32 FElysiumEntity::RegisterThinkContext(const ANSICHAR* ContextName)
{
	// CBaseEntity::RegisterThinkContext 0x100ac360: lookup first, then append; no name copy.
	const int32 Found = GetIndexForContext(ContextName);
	if (Found != -1)
	{
		if (World) World->EmitRetailSite(*this, TEXT("think_context"), TEXT("CBaseEntity::RegisterThinkContext"),
			0x100ac360u, TEXT("return"), FString::Printf(TEXT("arm=hit index=%d count=%d capacity=%d"),
				Found, ThinkContexts.Num(), ThinkContextCapacity));
		return Found;
	}
	const int32 NewIndex = ThinkContexts.Num();
	if (NewIndex + 1 > ThinkContextCapacity)
	{
		// CUtlMemory grow 0x100b49b0: constructed grow size 0, capacities 2,4,8,... .
		// Unreal owns the allocation; this word keeps the retail logical capacity, not TArray slack.
		do { ThinkContextCapacity = ThinkContextCapacity == 0 ? 2 : ThinkContextCapacity * 2; }
		while (ThinkContextCapacity < NewIndex + 1);
		ThinkContexts.Reserve(ThinkContextCapacity);
	}
	// The append's shift count is zero (100ac43c): the memmove arm is unreachable.
	FThinkContext& NewContext = ThinkContexts.AddDefaulted_GetRef();
	NewContext.Name = ContextName; // +0: caller pointer; the other three words are zero.
	if (World) World->EmitRetailSite(*this, TEXT("think_context"), TEXT("CBaseEntity::RegisterThinkContext"),
		0x100ac360u, TEXT("return"), FString::Printf(TEXT("arm=append index=%d count=%d capacity=%d callback=0 next=0 last=0"),
			NewIndex, ThinkContexts.Num(), ThinkContextCapacity));
	return NewIndex;
}

ANSICHAR* FElysiumEntityWorld::StageThinkContextName(const FString& Name)
{
	TUniquePtr<TArray<ANSICHAR>> Buffer = MakeUnique<TArray<ANSICHAR>>();
	const FTCHARToUTF8 Bytes(*Name);
	Buffer->Append(reinterpret_cast<const ANSICHAR*>(Bytes.Get()), Bytes.Length() + 1);
	ANSICHAR* const NamePointer = Buffer->GetData();
	ThinkContextFixtureNames.Add(MoveTemp(Buffer));
	return NamePointer;
}
