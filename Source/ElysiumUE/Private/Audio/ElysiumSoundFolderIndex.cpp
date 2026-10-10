#include "Audio/ElysiumSoundFolderIndex.h"

#include "ElysiumRetailSite.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSoundFolder, Log, All);

namespace
{
	void FolderSite(IElysiumRetailSiteSink* Sites, const TCHAR* Tag, const TCHAR* Fn, uint32 Va, const TCHAR* Phase,
		const FString& Payload)
	{
		if (Sites != nullptr)
		{
			Sites->Site(Tag, Fn, Va, Phase, Payload);
		}
	}

	const TCHAR* NodeLabel(const ElysiumSoundFolder::FNode& Node)
	{
		return Node.Label.IsEmpty() ? TEXT("?") : *Node.Label;
	}
}

namespace ElysiumSoundFolder
{
	FString MaskText(const FNode& Node)
	{
		if (Node.Mask.IsEmpty())
		{
			return TEXT("-");
		}
		TArray<FString> Bytes;
		Bytes.Reserve(Node.Mask.Num());
		for (const uint8 B : Node.Mask)
		{
			Bytes.Add(FString::FromInt(B));
		}
		return FString::Join(Bytes, TEXT(","));
	}

	int32 FOwner::Total() const
	{
		// `*(owner + 8) == 0` -> 0; else `n = *(*(owner + 8) + 0x14)`, `sum(counts[0..n-1])`. A count the
		// table names but the array does not hold would be a read past the array in retail; the port
		// reads 0 there (UNRECOVERED: the owner's construction keeps the two in step).
		if (CategoryCount <= 0)
		{
			return 0;
		}
		int32 Sum = 0;
		for (int32 Cat = 0; Cat < CategoryCount; ++Cat)
		{
			Sum += Counts.IsValidIndex(Cat) ? Counts[Cat] : 0;
		}
		return Sum;
	}

	void FOwner::AddRange(int32 Category, int32 Hi, IElysiumRetailSiteSink* Sites)
	{
		// `iVar1 = counts[cat]; if (iVar1 < hi + 1) { counts[cat] = hi + 1; FUN_101f3ba0(T + 0x10, cat, hi,
		// iVar1 - 1); }`. `counts[cat]` outside the array is retail's read past it; the port grows the
		// array to the category (zero counts between), stated.
		if (Category < 0)
		{
			FolderSite(Sites, TEXT("folder_addrange"), TEXT("FUN_101f4330"), 0x101f4330u, TEXT("branch"),
				FString::Printf(TEXT("cat=%d hi=%d refused=negative_category"), Category, Hi));
			return;
		}
		if (!Counts.IsValidIndex(Category))
		{
			Counts.SetNumZeroed(Category + 1);
		}
		const int32 Old = Counts[Category];
		const bool bGrow = Old < Hi + 1;
		FolderSite(Sites, TEXT("folder_addrange"), TEXT("FUN_101f4330"), 0x101f4330u, TEXT("branch"),
			FString::Printf(TEXT("cat=%d hi=%d count=%d grow=%d lo=%d"), Category, Hi, Old, bGrow ? 1 : 0, Old - 1));
		if (!bGrow)
		{
			return;
		}
		Counts[Category] = Hi + 1;
		InsertRange(Root, Category, Hi, Old - 1, Sites);
	}

	FNode* FOwner::Find(int32 Key, int32 Category, int32 Index, IElysiumRetailSiteSink* Sites)
	{
		FNode* Found = Key != -1 ? FindNode(Root, Key, Category, Index, Sites) : nullptr;
		FolderSite(Sites, TEXT("folder_find"), TEXT("FUN_101f42d0"), 0x101f42d0u, TEXT("return"),
			FString::Printf(TEXT("key=%d cat=%d idx=%d node=%s"), Key, Category, Index, Found != nullptr ? NodeLabel(*Found) : TEXT("null")));
		return Found;
	}

	void InsertRange(FNode& Node, int32 Category, int32 Hi, int32 Lo, IElysiumRetailSiteSink* Sites)
	{
		if (Node.Owner == nullptr)
		{
			return;
		}
		const FOwner& Owner = *Node.Owner;
		// B0. `total = FUN_101f4300(T)`.
		const int32 Total = Owner.Total();
		// B1. `a = F(T, cat, lo)`, the old last member. B2. `b = F(T, cat, hi)`, the new last member.
		const int32 A = Owner.FlatIndex(Category, Lo);
		const int32 B = Owner.FlatIndex(Category, Hi);
		FolderSite(Sites, TEXT("folder_insert"), TEXT("FUN_101f3ba0"), 0x101f3ba0u, TEXT("entry"),
			FString::Printf(TEXT("node=%s cat=%d hi=%d lo=%d total=%d a=%d b=%d old=%s"), NodeLabel(Node), Category, Hi, Lo, Total, A, B, *MaskText(Node)));
		// B3. `new = operator_new(total)`, not zeroed. With the counts in step with the mask, B6-B8 write
		//     every byte of it; the port starts from zero, which is what B7 writes anyway.
		TArray<uint8> New;
		New.SetNumZeroed(FMath::Max(Total, 0));
		// B4. `total < b` -> `DevWarning(1, "CVSoundFileFolder_t::AddRange IDX SHOULD NOT BE GREATER THAN
		//     SIZE?  HUH??? %d %d\n", b, total)`; execution continues (and B7 then writes past `new` in
		//     retail -- the port clamps every write to the buffer, a stated divergence on a path whose
		//     reachability is UNRECOVERED).
		if (Total < B)
		{
			UE_LOG(LogElysiumSoundFolder, Warning, TEXT("CVSoundFileFolder_t::AddRange IDX SHOULD NOT BE GREATER THAN SIZE?  HUH??? %d %d"), B, Total);
			FolderSite(Sites, TEXT("folder_insert"), TEXT("FUN_101f3ba0"), 0x101f3ba0u, TEXT("warning"),
				FString::Printf(TEXT("node=%s b=%d total=%d"), NodeLabel(Node), B, Total));
		}
		// B5. `node + 0x18 = total` (the new mask's length; the TArray carries it).
		const TArray<uint8>& Old = Node.Mask;
		auto OldByte = [&Old](int32 I) -> uint8 { return Old.IsValidIndex(I) ? Old[I] : 0; };   // a read past the old mask is 0 here
		// B6. `a >= 0` -> `new[0..a] = old[0..a]`.
		if (A >= 0)
		{
			for (int32 I = 0; I <= A && I < New.Num(); ++I)
			{
				New[I] = OldByte(I);
			}
		}
		// B7. `a + 1 <= b` -> `new[a+1 .. b] = 0` (a dword fill then a byte fill).
		if (A + 1 <= B)
		{
			for (int32 I = FMath::Max(A + 1, 0); I <= B && I < New.Num(); ++I)
			{
				New[I] = 0;
			}
		}
		// B8. `j` in `(b, total)`: `new[j] = old[j - (b - a)]`.
		for (int32 J = B + 1; J < Total; ++J)
		{
			if (J >= 0)
			{
				New[J] = OldByte(J - (B - A));
			}
		}
		// B9. `free(old)`; `node + 0x14 = new`.
		Node.Mask = MoveTemp(New);
		FolderSite(Sites, TEXT("folder_insert"), TEXT("FUN_101f3ba0"), 0x101f3ba0u, TEXT("return"),
			FString::Printf(TEXT("node=%s mask=%s"), NodeLabel(Node), *MaskText(Node)));
		// B10. Every child, in order, with the same (cat, hi, lo).
		for (const TUniquePtr<FNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				InsertRange(*Child, Category, Hi, Lo, Sites);
			}
		}
	}

	FNode* FindNode(FNode& Node, int32 Key, int32 Category, int32 Index, IElysiumRetailSiteSink* Sites)
	{
		// F0. `node.key == key`: the node, then each sibling, is tested on `mask[F(cat, idx)] != 0`; the
		//     loop's key test compares the next sibling's key with itself (`MOV EDI,[ESI]; MOV EAX,[ESI];
		//     CMP EAX,EDI`), so every sibling is tested whatever its key. The children are never
		//     searched on this path.
		if (Node.Key == Key)
		{
			for (FNode* N = &Node; N != nullptr; N = N->Sibling)
			{
				const int32 M = N->Owner != nullptr ? N->Owner->FlatIndex(Category, Index) : Index;
				const bool bHit = N->Mask.IsValidIndex(M) && N->Mask[M] != 0;   // a read past the mask is 0 here (retail: the byte beyond)
				FolderSite(Sites, TEXT("folder_find"), TEXT("FUN_101f3b00"), 0x101f3b00u, TEXT("branch"),
					FString::Printf(TEXT("node=%s arm=sibling key=%d flat=%d hit=%d"), NodeLabel(*N), N->Key, M, bHit ? 1 : 0));
				if (bHit)
				{
					return N;
				}
			}
			return nullptr;
		}
		// F1. `node.key != key`: each child in turn; the first non-NULL answer returns.
		FolderSite(Sites, TEXT("folder_find"), TEXT("FUN_101f3b00"), 0x101f3b00u, TEXT("branch"),
			FString::Printf(TEXT("node=%s arm=children key=%d children=%d"), NodeLabel(Node), Node.Key, Node.Children.Num()));
		for (const TUniquePtr<FNode>& Child : Node.Children)
		{
			if (!Child.IsValid())
			{
				continue;
			}
			if (FNode* Found = FindNode(*Child, Key, Category, Index, Sites))
			{
				return Found;
			}
		}
		// F2. NULL.
		return nullptr;
	}
}
