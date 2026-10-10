#include "ElysiumKeyValues.h"

#include "ElysiumRetailSite.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumKeyValues, Log, All);

namespace ElysiumKeyValues
{
namespace
{
	// `DAT_10753641`, the table selector `0x10247280` reads at `0x102472a3`: `.bss`, written nowhere in
	// the image, so it is 0 at run time and table B (colon is a delimiter) is the only live table.
	constexpr uint8 GTokenTableSelector = 0;

	// `0x10247280`'s whitespace: every byte whose SIGNED value is <= 0x20 -- the controls, space, and
	// every byte >= 0x80 (negative). A TCHAR >= 0x80 classifies the same way (header note).
	bool IsRetailSpace(TCHAR C)
	{
		return C <= TEXT('\x20') || C >= TEXT('\x80');
	}

	// `table[c]` for a token-starting or word-continuing character. A TCHAR >= 0x80 never reaches a
	// probe as a token start (it is whitespace); as a word continuation retail's negative index reads
	// before the table, and both outcomes end the word, which `IsRetailSpace` already does.
	bool InTable(const uint8* Table, TCHAR C)
	{
		return static_cast<uint32>(C) < 0x80u && Table[static_cast<int32>(C)] != 0;
	}

	// A token or value as a one-line trace payload: the controls a quoted value may carry are shown
	// escaped so the trace stays one event per line.
	FString Shown(const FString& S)
	{
		FString Out;
		Out.Reserve(S.Len());
		for (const TCHAR C : S)
		{
			switch (C)
			{
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default: Out.AppendChar(C); break;
			}
		}
		return Out;
	}

	FString CursorText(int32 Cursor)
	{
		return Cursor == INDEX_NONE ? FString(TEXT("null")) : FString::Printf(TEXT("%d"), Cursor);
	}

	// `_strtol` (`0x104319A8` -> `_strtoxl`, VC6 SP5 libc), base 10: leading `isspace` (space, \t \n
	// \v \f \r), an optional sign, decimal digits. `OutEnd` is the end pointer as an index: 0 when
	// nothing was consumed (a sign with no digit consumes nothing). Overflow clamps to LONG_MAX /
	// LONG_MIN.
	int32 RetailStrtol(const FString& S, int32& OutEnd)
	{
		int32 I = 0;
		const int32 N = S.Len();
		auto At = [&S, N](int32 Index) -> TCHAR { return Index < N ? S[Index] : TEXT('\0'); };
		while (At(I) == TEXT(' ') || At(I) == TEXT('\t') || At(I) == TEXT('\n') || At(I) == TEXT('\v')
			|| At(I) == TEXT('\f') || At(I) == TEXT('\r'))
		{
			++I;
		}
		bool bNegative = false;
		if (At(I) == TEXT('+') || At(I) == TEXT('-'))
		{
			bNegative = At(I) == TEXT('-');
			++I;
		}
		const int32 DigitsStart = I;
		uint64 Acc = 0;
		bool bOverflow = false;
		while (At(I) >= TEXT('0') && At(I) <= TEXT('9'))
		{
			if (!bOverflow)
			{
				Acc = Acc * 10 + static_cast<uint64>(At(I) - TEXT('0'));
				if (Acc > static_cast<uint64>(MAX_int32) + (bNegative ? 1u : 0u))
				{
					bOverflow = true;
				}
			}
			++I;
		}
		if (I == DigitsStart)
		{
			OutEnd = 0;
			return 0;
		}
		OutEnd = I;
		if (bOverflow)
		{
			return bNegative ? MIN_int32 : MAX_int32;
		}
		return bNegative ? static_cast<int32>(0u - static_cast<uint32>(Acc)) : static_cast<int32>(Acc);
	}

	// `_strtod` (`0x1043190E`, VC6 SP5 libc, `__fltin` -> `___strgtold12` at `0x1043B1BD`): leading
	// space / tab / LF / CR, an optional sign, digits with an optional `.` (the locale decimal point
	// `DAT_106b0d60`), an optional exponent introduced by `D`, `E`, `d` or `e` with an optional sign
	// and at least one digit (fewer: the exponent is not consumed). There is NO hex state and NO
	// `inf`/`nan` state: `0x1A` consumes `0`, `inf` and `nan` consume nothing. `OutEnd` as above.
	double RetailStrtod(const FString& S, int32& OutEnd)
	{
		int32 I = 0;
		const int32 N = S.Len();
		auto At = [&S, N](int32 Index) -> TCHAR { return Index < N ? S[Index] : TEXT('\0'); };
		auto IsDigit = [](TCHAR C) { return C >= TEXT('0') && C <= TEXT('9'); };
		while (At(I) == TEXT(' ') || At(I) == TEXT('\t') || At(I) == TEXT('\n') || At(I) == TEXT('\r'))
		{
			++I;
		}
		const int32 NumberStart = I;
		if (At(I) == TEXT('+') || At(I) == TEXT('-'))
		{
			++I;
		}
		int32 Digits = 0;
		while (IsDigit(At(I))) { ++I; ++Digits; }
		if (At(I) == TEXT('.'))
		{
			++I;
			while (IsDigit(At(I))) { ++I; ++Digits; }
		}
		if (Digits == 0)
		{
			OutEnd = 0;
			return 0.0;
		}
		int32 End = I;
		if (At(I) == TEXT('D') || At(I) == TEXT('E') || At(I) == TEXT('d') || At(I) == TEXT('e'))
		{
			int32 J = I + 1;
			if (At(J) == TEXT('+') || At(J) == TEXT('-')) { ++J; }
			if (IsDigit(At(J)))
			{
				while (IsDigit(At(J))) { ++J; }
				End = J;
			}
		}
		OutEnd = End;
		// The consumed text in the C grammar (`D`/`d` is VC's own exponent letter): the value is the
		// double `_strtod` returns, which the caller rounds to float (`FSTP float`).
		FString Number = S.Mid(NumberStart, End - NumberStart);
		Number.ReplaceInline(TEXT("D"), TEXT("e"));
		Number.ReplaceInline(TEXT("d"), TEXT("e"));
		return FCString::Atod(*Number);
	}

	// The lookup index over `Parent.Children` for one child whose type is final.
	void IndexChild(FKvNode& Parent, const TSharedPtr<FKvNode>& Child)
	{
		const FString Key = Child->Name.ToLower();
		if (Child->Type == EKvType::Block)
		{
			Parent.Kids.Emplace(Key, Child);
		}
		else
		{
			Parent.Values.Add(Key, Child->StringValue);
			Parent.Pairs.Emplace(Key, Child->StringValue);
		}
	}

	void LeafSite(FKvReader& R, const FKvNode& Child)
	{
		if (R.Sites == nullptr)
		{
			return;
		}
		FString Payload = FString::Printf(TEXT("key=%s type=%d"), *Shown(Child.Name), static_cast<int32>(Child.Type));
		switch (Child.Type)
		{
		case EKvType::Int:   Payload += FString::Printf(TEXT(" num=%d str=%s"), Child.IntValue, *Shown(Child.StringValue)); break;
		case EKvType::Float: Payload += FString::Printf(TEXT(" num=%g str=%s"), Child.FloatValue, *Shown(Child.StringValue)); break;
		case EKvType::String: Payload += FString::Printf(TEXT(" str=%s"), *Shown(Child.StringValue)); break;
		default: break;
		}
		R.Sites->Site(TEXT("kv_leaf"), TEXT("FUN_101f2360"), 0x101f2360u, TEXT("write"), Payload);
	}
}

void BuildDelimiterTable(uint8 (&Table)[256], const ANSICHAR* Delims)
{
	// `0x1023eff0`. Arm 1: either pointer NULL -> nothing written.
	if (Delims == nullptr)
	{
		return;
	}
	// Arm 2: `MOV ECX,0x40; XOR EAX,EAX; REP STOSD` -- 64 dwords, 256 bytes zeroed.
	FMemory::Memzero(Table, sizeof(Table));
	// Arm 3: `MOVSX EAX,byte [ESI]; MOV byte [EAX+table],1` to the NUL. The index is signed; a byte
	// >= 0x80 would land before the table (no retail string has one; skipped here).
	for (const ANSICHAR* C = Delims; *C != '\0'; ++C)
	{
		const int32 Index = static_cast<int32>(static_cast<int8>(*C));
		if (Index >= 0)
		{
			Table[Index] = 1;
		}
	}
}

const FKvDelimiterTables& DelimiterTables()
{
	// `0x102473e0`: `DAT_10753640` is the once-flag; the two strings are PE reads.
	static const FKvDelimiterTables Tables = []()
	{
		FKvDelimiterTables Built;
		BuildDelimiterTable(Built.A, "{}()'");    // `0x105794c0` -> table A `0x10753440`
		BuildDelimiterTable(Built.B, "{}()':");   // `0x105c4ef8` -> table B `0x10753540`
		return Built;
	}();
	return Tables;
}

int32 NextToken(FKvReader& R, FString& Out, uint8* Quoted)
{
	// `0x10247280`, arms in retail order.
	// 0. `if (quoted) *quoted = 0`.
	if (Quoted != nullptr)
	{
		*Quoted = 0;
	}
	// 1. A NULL cursor returns NULL; `out` is not touched.
	if (R.Pos == INDEX_NONE)
	{
		if (R.Sites != nullptr)
		{
			R.Sites->Site(TEXT("kv_token"), TEXT("FUN_10247280"), 0x10247280u, TEXT("return"),
				TEXT("cursor=null out=untouched quoted=0"));
		}
		return INDEX_NONE;
	}
	// 2. `CALL 0x102473e0`: the tables, once.
	const FKvDelimiterTables& Tables = DelimiterTables();
	// 3. `EBP = table A; if (selector == 0) EBP = table B`.
	const uint8* Table = Tables.A;
	if (GTokenTableSelector == 0)
	{
		Table = Tables.B;
	}
	if (R.Sites != nullptr)
	{
		R.Sites->Site(TEXT("kv_token"), TEXT("FUN_10247280"), 0x10247280u, TEXT("entry"),
			FString::Printf(TEXT("selector=%d table=%s cursor=%d"), GTokenTableSelector,
				Table == Tables.B ? TEXT("B") : TEXT("A"), R.Pos));
	}
	// 4. `*out = 0`.
	Out.Reset();
	// Past the NUL (retail: a cursor one past an unterminated quote's end reads the heap) the port
	// reads NUL -- the one representation choice in this function (header note).
	auto At = [&R](int32 Index) -> TCHAR { return Index <= R.Len ? R.Text[Index] : TEXT('\0'); };
	auto Return = [&R, &Out](int32 Cursor, bool bQuoted) -> int32
	{
		if (R.Sites != nullptr)
		{
			R.Sites->Site(TEXT("kv_token"), TEXT("FUN_10247280"), 0x10247280u, TEXT("return"),
				FString::Printf(TEXT("token=%s quoted=%d cursor=%s"), *Shown(Out), bQuoted ? 1 : 0, *CursorText(Cursor)));
		}
		return Cursor;
	};
	int32 P = R.Pos;
	for (;;)   // LAB_102472c3
	{
		// 5. Skip: `c = (int8)*cursor; while (c <= 0x20) { if (c == 0) return NULL; ++cursor; }`.
		TCHAR C = At(P);
		while (IsRetailSpace(C))
		{
			if (C == TEXT('\0'))
			{
				return Return(INDEX_NONE, false);
			}
			++P;
			C = At(P);
		}
		// 6. `c == '/'`.
		if (C == TEXT('/'))
		{
			if (At(P + 1) == TEXT('/'))
			{
				// 6a. `//`: to `\n` or NUL, then back to 5.
				while (At(P) != TEXT('\0') && At(P) != TEXT('\n'))
				{
					++P;
				}
				continue;
			}
			if (At(P + 1) == TEXT('*'))
			{
				// 6b. `/*`: the cursor moves past `/*`, then to the first `*/` at or after that byte
				// (`/**/` closes, `/*/` does not); unclosed runs to the NUL and 5 returns NULL.
				P += 2;
				while (At(P) != TEXT('\0'))
				{
					if (At(P) == TEXT('*') && At(P + 1) == TEXT('/'))
					{
						P += 2;
						break;
					}
					++P;
				}
				continue;
			}
			// 6c. A lone `/` is not in the table: it starts a word (9).
		}
		else if (C == TEXT('"'))
		{
			// 7. A quoted string. `\"` -> `"` (both consumed; any other backslash is kept literally),
			// `"` ends with the cursor after it, NUL ends with the cursor one past the NUL; newlines kept.
			if (Quoted != nullptr)
			{
				*Quoted = 1;
			}
			int32 Q = P + 1;
			for (;;)   // LAB_10247330
			{
				const TCHAR Ch = At(Q);
				P = Q + 1;
				if (Ch == TEXT('\\'))
				{
					if (At(P) == TEXT('"'))
					{
						Out.AppendChar(TEXT('"'));
						Q += 2;
						continue;
					}
				}
				else if (Ch == TEXT('"') || Ch == TEXT('\0'))
				{
					return Return(P, true);
				}
				Out.AppendChar(Ch);
				Q = P;
			}
		}
		// 8. LAB_10247359: a delimiter is its own one-byte token; the cursor moves past it.
		if (InTable(Table, C))
		{
			Out.AppendChar(C);
			return Return(P + 1, false);
		}
		// 9. A bare word: `do { ++cursor; out[i++] = c; c = *cursor; if (table[c]) break; } while (c > 0x20)`.
		// A `"` or `/` inside does not end it; the stop byte is not consumed.
		do
		{
			++P;
			Out.AppendChar(C);
			C = At(P);
			if (InTable(Table, C))
			{
				break;
			}
		} while (!IsRetailSpace(C));
		return Return(P, false);
	}
}

const FString& ReadToken(FKvReader& R, uint8* Quoted)
{
	// `0x101f2f30`: `r = 0x10247280(*cursor, &buf, quoted); *cursor = r; return &buf`. Its second
	// argument (the file system) is never read.
	R.Pos = NextToken(R, R.Token, Quoted);
	return R.Token;
}

TSharedPtr<FKvNode> AddChild(FKvNode& Parent, const FString& Name)
{
	// `0x101f2cf0`: pool alloc, `ctor(name, 1)` (`0x101f1f50` -> SetName), appended at the TAIL of the
	// child list (walk `+0x18` to the last sibling, link it with `0x101f2cd0`, new `+0x18 = 0`).
	TSharedPtr<FKvNode> Child = MakeShared<FKvNode>();
	SetName(*Child, Name);
	Parent.Children.Add(Child);
	return Child;
}

void Clear(FKvNode& Node)
{
	// `0x101f2c60`: Release each child (freed at refcount 0), `+0x1C = 0`, free `+0x14` and `+0x0C`,
	// `+0x10 = 0`, `DAT_1073AA40[0] = 0`, `+0x18 = 0`, `+0x20 = 0`. `+0x08` is not reset.
	Node.Children.Reset();
	Node.Kids.Reset();
	Node.Values.Reset();
	Node.Pairs.Reset();
	Node.StringValue.Reset();
	Node.Type = EKvType::Block;
}

void SetName(FKvNode& Node, const FString& Name)
{
	// `0x101f2090`: `+0x14 = intern(name)`; zero `+0x0C`, `+0x10`, `+0x1C`, `+0x18`, `+0x20`;
	// `DAT_1073AA40[0] = 0`.
	Node.Name = Name;
	Node.StringValue.Reset();
	Node.Type = EKvType::Block;
	Node.Children.Reset();
	Node.Kids.Reset();
	Node.Values.Reset();
	Node.Pairs.Reset();
}

void ParseBlock(FKvNode& Node, FKvReader& R)
{
	// `0x101f2360`, arms in retail order. Loop head L:
	for (;;)
	{
		// 1. `key = wrapper(cursor, fs, NULL)`; `key == NULL || *key == 0` -> return. The wrapper never
		//    returns NULL; `""` is end of text, and equally a quoted empty key, which closes the block.
		const FString& Key = ReadToken(R, nullptr);
		if (Key.IsEmpty())
		{
			return;
		}
		// 2. `key == "}"` (2-byte compare against `0x10547B64` = `7D 00`); the quoted flag is not asked.
		if (Key == TEXT("}"))
		{
			return;
		}
		// 3. `child = AddChild(key)`: always creates, no name lookup.
		TSharedPtr<FKvNode> Child = AddChild(Node, Key);
		// 4. `val = wrapper(cursor, fs, &quoted)`. Copied: the recursion below reuses the shared buffer.
		uint8 Quoted = 0;
		const FString Val = ReadToken(R, &Quoted);
		// 5. `val == "{"` (`0x10547B78` = `7B 00`; a quoted `"{"` recurses too): the child is a block.
		if (Val == TEXT("{"))
		{
			LeafSite(R, *Child);
			ParseBlock(*Child, R);
			IndexChild(Node, Child);
			continue;
		}
		// 6. A leaf: `+0x0C = intern(val)` for every leaf (an end-of-text value makes a `""` string leaf).
		Child->StringValue = Val;
		if (Quoted != 0)
		{
			// 6a. Quoted: type 1, whatever the text.
			Child->Type = EKvType::String;
		}
		else
		{
			// 7. `e1 = end of strtol(val, 10)`, `e2 = end of strtod(val)`; `CMP e2,e1` unsigned.
			int32 End1 = 0;
			const int32 IntValue = RetailStrtol(Val, End1);
			int32 End2 = 0;
			const double DoubleValue = RetailStrtod(Val, End2);
			if (End2 > End1)
			{
				// 7a. strtod consumed more: `FSTP float`, type 3.
				Child->FloatValue = static_cast<float>(DoubleValue);
				Child->Type = EKvType::Float;
			}
			else if (End1 <= 0)
			{
				// 8a. strtol consumed nothing: type 1.
				Child->Type = EKvType::String;
			}
			else
			{
				// 8b. type 2, the int.
				Child->IntValue = IntValue;
				Child->Type = EKvType::Int;
			}
		}
		LeafSite(R, *Child);
		IndexChild(Node, Child);
	}
}

bool ParseRoots(FKvReader& R, const FString& FileName, const TSharedPtr<FKvNode>& InTarget,
	TArray<TSharedPtr<FKvNode>>& OutRoots)
{
	// `0x101f2180` from R7: `cursor = buf; prev = NULL; target = ECX`.
	if (R.Sites != nullptr)
	{
		R.Sites->Site(TEXT("kv_root"), TEXT("FUN_101f2180"), 0x101f2180u, TEXT("entry"),
			FString::Printf(TEXT("file=%s target=%s"), *FileName, InTarget.IsValid() ? TEXT("set") : TEXT("null")));
	}
	// `prev` (the last root a block was parsed into) is what `0x101f2cd0` links a new root to; here the
	// chain is `OutRoots`' order, so it needs no separate word.
	TSharedPtr<FKvNode> Target = InTarget;
	for (;;)
	{
		// L1. `key = wrapper(&cursor, fs, NULL)`.
		const FString Key = ReadToken(R, nullptr);
		// L2. `cursor == NULL` -> Done. An empty or whitespace-only text exits here, before `target` is touched.
		if (R.Pos == INDEX_NONE)
		{
			break;
		}
		TSharedPtr<FKvNode> Node;
		if (Target.IsValid())
		{
			// L3a. `Clear(target)` (`0x101f2c60`), `SetName(target, key)` (`0x101f2090`); `node = target`.
			Clear(*Target);
			SetName(*Target, Key);
			Node = Target;
			if (!OutRoots.Contains(Node))
			{
				OutRoots.Add(Node);
			}
			if (R.Sites != nullptr)
			{
				R.Sites->Site(TEXT("kv_root"), TEXT("FUN_101f2180"), 0x101f2180u, TEXT("branch"),
					FString::Printf(TEXT("arm=reuse name=%s"), *Shown(Key)));
			}
		}
		else
		{
			// L3b. `node = new(0x38)` + `ctor(node, key, 1)`; `if (prev) prev->+0x18 = node` (`0x101f2cd0`):
			// the roots chain, which `OutRoots` holds in order.
			Node = MakeShared<FKvNode>();
			SetName(*Node, Key);
			OutRoots.Add(Node);
			if (R.Sites != nullptr)
			{
				R.Sites->Site(TEXT("kv_root"), TEXT("FUN_101f2180"), 0x101f2180u, TEXT("branch"),
					FString::Printf(TEXT("arm=new name=%s"), *Shown(Key)));
			}
		}
		// L4. `tok = wrapper(&cursor, fs, NULL)`.
		const FString Tok = ReadToken(R, nullptr);
		// L5. `cursor == NULL` -> Done; the node from L3 stays (renamed target, or a childless root).
		if (R.Pos == INDEX_NONE)
		{
			break;
		}
		// L6. `tok == "{"` (2-byte compare, quoted flag not asked) -> parse the block; `prev = node`; `target = NULL`.
		if (Tok == TEXT("{"))
		{
			ParseBlock(*Node, R);
			Target = nullptr;
			continue;
		}
		// L7. `DevMsg("ERROR: parsing KeyValue in file %s, expecting {, got %s\n", name, "{")`: the
		// printed text is the constant `0x10547B78`, not `tok`, which is dropped. `target = node`, so
		// the next key reuses this node (L3a); `prev` is unchanged.
		UE_LOG(LogElysiumKeyValues, Log, TEXT("ERROR: parsing KeyValue in file %s, expecting {, got %s"),
			*FileName, TEXT("{"));
		if (R.Sites != nullptr)
		{
			R.Sites->Site(TEXT("kv_root"), TEXT("FUN_101f2180"), 0x101f2180u, TEXT("error"),
				FString::Printf(TEXT("file=%s expecting={ got={ dropped=%s"), *FileName, *Shown(Tok)));
		}
		Target = Node;
	}
	// Done. `free(buf)`; return 1.
	if (R.Sites != nullptr)
	{
		R.Sites->Site(TEXT("kv_root"), TEXT("FUN_101f2180"), 0x101f2180u, TEXT("return"),
			FString::Printf(TEXT("result=1 roots=%d"), OutRoots.Num()));
	}
	return true;
}

TSharedPtr<FKvNode> RootsView(const TArray<TSharedPtr<FKvNode>>& Roots)
{
	if (Roots.IsEmpty())
	{
		return nullptr;
	}
	TSharedPtr<FKvNode> View = MakeShared<FKvNode>();
	for (const TSharedPtr<FKvNode>& Root : Roots)
	{
		View->Children.Add(Root);
		IndexChild(*View, Root);
	}
	return View;
}

TSharedPtr<FKvNode> ParseText(const FString& Text, IElysiumRetailSiteSink* Sites)
{
	FKvReader Reader(Text, Sites);
	TArray<TSharedPtr<FKvNode>> Roots;
	ParseRoots(Reader, TEXT("(text)"), nullptr, Roots);
	return RootsView(Roots);
}

} // namespace ElysiumKeyValues
