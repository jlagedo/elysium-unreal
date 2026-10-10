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

	// --- `FUN_101f39d0`'s three GLOBAL scratch buffers (`walks/L0-r007.md`) -------------------------
	// `DAT_1073dad0` (the fixed-slash copy of the name, `Q_strncpy` 0x104), `DAT_1074b180` (the first
	// path component) and `DAT_1073dc50` (the remainder after the first `\`). They are process globals
	// in retail and stay globals here: a child call writes the same buffers its parent's later
	// children are handed a POINTER into, so a matching child that misses leaves its REMAINDER where
	// the parent's next child reads the whole name (N4). Reproduced, not repaired.
	constexpr int32 GGroupPathCapacity = 0x104;
	TCHAR GGroupPath[GGroupPathCapacity];       // DAT_1073dad0
	TCHAR GGroupComponent[GGroupPathCapacity];  // DAT_1074b180 (its retail size is not recovered; a component never exceeds the path)
	TCHAR GGroupRemainder[GGroupPathCapacity];  // DAT_1073dc50 (likewise)

	// `Q_strncpy` (vstdlib `0x100037b0`): CRT `strncpy(dst, src, n)` then `dst[n - 1] = 0`. Spelled out
	// so a source aliasing the destination (the identity copy a non-matching node's children make)
	// behaves as the CRT's byte loop does.
	void QStrncpy(TCHAR* Dst, const TCHAR* Src, int32 N)
	{
		if (N <= 0)
		{
			return;
		}
		int32 I = 0;
		for (; I < N && Src[I] != TEXT('\0'); ++I)
		{
			Dst[I] = Src[I];
		}
		for (; I < N; ++I)
		{
			Dst[I] = TEXT('\0');
		}
		Dst[N - 1] = TEXT('\0');
	}

	void GroupSite(IElysiumRetailSiteSink* Sites, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload)
	{
		if (Sites != nullptr)
		{
			Sites->Site(TEXT("folder_group"), Fn, Va, Phase, Payload);
		}
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

	int32 FOwner::GroupIndex(const TCHAR* Name, IElysiumRetailSiteSink* Sites)
	{
		// `iVar1 = FUN_101f39d0(T + 0x10, name); if (iVar1 == -1) iVar1 = *(int*)(T + 0x10); return iVar1;`
		// -- the root node's own key is the miss value, so a group the category does not hold plays the
		// sounds directly under the category (`audio_pipeline.md` § 7b, "A miss is not silence").
		const int32 Found = FindGroup(Root, Name, Sites);
		const int32 Result = Found == -1 ? Root.Key : Found;
		GroupSite(Sites, TEXT("FUN_101f42a0"), 0x101f42a0u, TEXT("return"),
			FString::Printf(TEXT("name=%s found=%d index=%d"), Name != nullptr && *Name != TEXT('\0') ? Name : TEXT("(empty)"), Found != -1 ? 1 : 0, Result));
		return Result;
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

	int32 FindGroup(FNode& Node, const TCHAR* Name, IElysiumRetailSiteSink* Sites)
	{
		// N0. `Q_strncpy(DAT_1073dad0, name, 0x104)`; `Q_FixSlashes(DAT_1073dad0, '\')` (every `/` to `\`);
		//     `puVar2 = strchr(DAT_1073dad0, '\')` (`FUN_10431f30`). A NULL `name` faults in retail's
		//     `Q_strncpy`; no caller passes one (every caller substitutes `""`), and the port reads `""`.
		QStrncpy(GGroupPath, Name != nullptr ? Name : TEXT(""), GGroupPathCapacity);
		for (TCHAR* C = GGroupPath; *C != TEXT('\0'); ++C)
		{
			if (*C == TEXT('/'))
			{
				*C = TEXT('\\');
			}
		}
		const TCHAR* Backslash = FCString::Strchr(GGroupPath, TEXT('\\'));
		// N1. The first component: with a `\`, `Q_strncpy(DAT_1074b180, DAT_1073dad0, (pos of the backslash) + 1)`
		//     -- the bytes before the `\`, NUL-terminated -- else the whole string is the component.
		const TCHAR* Component = GGroupPath;
		int32 RemainderOffset = 0;
		if (Backslash != nullptr)
		{
			RemainderOffset = static_cast<int32>(Backslash - GGroupPath) + 1;   // iVar6
			QStrncpy(GGroupComponent, GGroupPath, RemainderOffset);
			Component = GGroupComponent;
		}
		// N2. An EMPTY component answers -1 at once: the children are not searched (so `""` and a name
		//     opening with `\` always miss).
		if (*Component == TEXT('\0'))
		{
			GroupSite(Sites, TEXT("FUN_101f39d0"), 0x101f39d0u, TEXT("branch"),
				FString::Printf(TEXT("node=%s arm=empty"), NodeLabel(Node)));
			return -1;
		}
		// N3. `__strcmpi(node.name (NULL as ""), component)`. On a match with no `\` left the node's own
		//     key (`*(int*)node`) is the answer.
		const TCHAR* Next = GGroupPath;   // puVar5: what the children are handed -- the WHOLE fixed string
		const bool bMatch = FCString::Stricmp(*Node.Name, Component) == 0;
		if (bMatch && Backslash == nullptr)
		{
			GroupSite(Sites, TEXT("FUN_101f39d0"), 0x101f39d0u, TEXT("return"),
				FString::Printf(TEXT("node=%s component=%s key=%d"), NodeLabel(Node), Component, Node.Key));
			return Node.Key;
		}
		if (bMatch)
		{
			// N4. The REMAINDER after the `\` is copied to `DAT_1073dc50` and the children receive THAT.
			//     (A byte loop, no bound: the remainder is shorter than the 0x104 path.)
			int32 I = 0;
			for (; GGroupPath[RemainderOffset + I] != TEXT('\0'); ++I)
			{
				GGroupRemainder[I] = GGroupPath[RemainderOffset + I];
			}
			GGroupRemainder[I] = TEXT('\0');
			Next = GGroupRemainder;
		}
		GroupSite(Sites, TEXT("FUN_101f39d0"), 0x101f39d0u, TEXT("branch"),
			FString::Printf(TEXT("node=%s component=%s match=%d next=%s children=%d"), NodeLabel(Node), Component,
				bMatch ? 1 : 0, Next, Node.Children.Num()));
		// N5. Every child in order with `Next` -- a POINTER into the global buffer, which the child's own
		//     N0 / N4 may have rewritten by the time the next sibling reads it (retail's aliasing). The
		//     first answer that is not -1 wins.
		for (const TUniquePtr<FNode>& Child : Node.Children)
		{
			if (!Child.IsValid())
			{
				continue;
			}
			const int32 Found = FindGroup(*Child, Next, Sites);
			if (Found != -1)
			{
				return Found;
			}
		}
		return -1;
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
