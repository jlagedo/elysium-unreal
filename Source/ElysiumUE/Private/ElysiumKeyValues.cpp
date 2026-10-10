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

	FString CursorText(int32 Cursor)
	{
		return Cursor == INDEX_NONE ? FString(TEXT("null")) : FString::Printf(TEXT("%d"), Cursor);
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

// --- Shared CRT models -------------------------------------------------------------------------------

int32 RetailAtol(const FString& S)
{
	// `_atol` (`0x104313bc`, VC6): `while (isspace(c)) c = *++p;` one sign; `total = 10 * total + digit`
	// in a 32-bit long (wraps, never clamps); stops at the first non-digit; `sign ? -total : total`.
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
	uint32 Total = 0;
	while (At(I) >= TEXT('0') && At(I) <= TEXT('9'))
	{
		Total = Total * 10u + static_cast<uint32>(At(I) - TEXT('0'));
		++I;
	}
	return static_cast<int32>(bNegative ? (0u - Total) : Total);
}

int32 RetailFtol(double X)
{
	// `__ftol` (`0x10431320`): control word rounding = truncate, `FISTP qword`; EAX = the low dword.
	// Outside int64 (NaN, +-Inf, |x| >= 2^63) the store is the integer indefinite, low dword 0.
	if (!FMath::IsFinite(X) || FMath::Abs(X) >= 9223372036854775808.0)
	{
		return 0;
	}
	const int64 Truncated = static_cast<int64>(X);
	return static_cast<int32>(static_cast<uint32>(static_cast<uint64>(Truncated) & 0xffffffffull));
}

// --- The file loader's class: lookup and typed access ---------------------------------------------

const TCHAR* FileTypeCode(EKvType Type)
{
	switch (Type)
	{
	case EKvType::String: return TEXT("0");
	case EKvType::Int:    return TEXT("1");
	case EKvType::Float:  return TEXT("2");
	default:              return TEXT("unset");
	}
}

void Reindex(FKvNode& Node)
{
	Node.Kids.Reset();
	Node.Values.Reset();
	Node.Pairs.Reset();
	for (const TSharedPtr<FKvNode>& Child : Node.Children)
	{
		IndexChild(Node, Child);
	}
}

TSharedPtr<FKvNode> CreateChild(FKvNode& Parent, const FString& Name)
{
	// `0x10248870`: `operator_new(0x1c)`, ctor `0x10247ba0` (`*node = 0; 0x10247cf0(node, name)`: copy the
	// name, zero `+0x14 +0x10 +0x04 +0x18`, clear `DAT_10753f68[0]`); `+0x08` / `+0x0C` never written.
	// Then append: `+0x14` empty -> first child; else walk `+0x10` (`0x10248a30`) to the tail and link
	// (`0x10248a50`); the new node's `+0x10 = 0`.
	TSharedPtr<FKvNode> Child = MakeShared<FKvNode>();
	Child->Name = Name;
	Parent.Children.Add(Child);
	return Child;
}

namespace
{
	// `0x10248900` with the owning list reported, so the port's index over that list can be rebuilt by
	// a writer (`SetString`); `Depth` is the chain recursion (`create` is 0 below the first level).
	FKvNode* FindKeyIn(FKvNode& Node, const FString& Key, EKvCreate Create, FKvNode*& OutOwner, EKvFound& OutFound)
	{
		// 1-2. `p = this->+0x14; while (p) { if (!strcmpi(name(p), key)) return p; p = p->+0x10; }`.
		for (const TSharedPtr<FKvNode>& Child : Node.Children)
		{
			if (Child->Name.Equals(Key, ESearchCase::IgnoreCase))
			{
				OutOwner = &Node;
				OutFound = EKvFound::Own;
				return Child.Get();
			}
		}
		// 3-4. Not in the own list: `this->+0x18` set -> `FindRec(chain, key, 0)` (own list, then its
		// chain; never creates); found -> return it, nothing created.
		if (TSharedPtr<FKvNode> Chain = Node.Chain.Pin())
		{
			FKvNode* InChain = FindKeyIn(*Chain, Key, EKvCreate::No, OutOwner, OutFound);
			if (InChain != nullptr)
			{
				OutFound = EKvFound::Chain;
				return InChain;
			}
		}
		// 5. `create` -> `CreateChild(this, key)` (`0x10248870`, appended at the tail of `this`).
		if (Create == EKvCreate::Yes)
		{
			OutOwner = &Node;
			OutFound = EKvFound::Created;
			TSharedPtr<FKvNode> Made = CreateChild(Node, Key);
			Reindex(Node);
			return Made.Get();
		}
		// 6. NULL.
		OutOwner = nullptr;
		OutFound = EKvFound::None;
		return nullptr;
	}

	const TCHAR* FoundText(EKvFound Found)
	{
		switch (Found)
		{
		case EKvFound::Own:     return TEXT("own");
		case EKvFound::Chain:   return TEXT("chain");
		case EKvFound::Created: return TEXT("created");
		default:                return TEXT("none");
		}
	}

	// `strcmpi(name, NULL)`: the one retail caller passing a NULL key (`0x101b2c60`) reads leaves,
	// whose child list is empty, so the compare never runs; the port compares `""` instead of faulting.
	FString KeyText(const TCHAR* Key)
	{
		return Key != nullptr ? FString(Key) : FString();
	}
}

FKvNode* FindKey(FKvNode& Node, const TCHAR* Key, EKvCreate Create, EKvFound* OutFound, IElysiumRetailSiteSink* Sites)
{
	// `0x10248900`.
	FKvNode* Owner = nullptr;
	EKvFound Found = EKvFound::None;
	const FString KeyString = KeyText(Key);
	FKvNode* Result = FindKeyIn(Node, KeyString, Create, Owner, Found);
	if (OutFound != nullptr)
	{
		*OutFound = Found;
	}
	if (Sites != nullptr)
	{
		Sites->Site(TEXT("kv.find"), TEXT("FUN_10248900"), 0x10248900u, TEXT("return"),
			FString::Printf(TEXT("key=%s create=%d source=%s name=%s"), *Shown(KeyString), Create == EKvCreate::Yes ? 1 : 0,
				FoundText(Found), Result != nullptr ? *Shown(Result->Name) : TEXT("null")));
	}
	return Result;
}

void SetString(FKvNode& Node, const TCHAR* Key, const FString& Value, IElysiumRetailSiteSink* Sites)
{
	// `0x102490e0`. 1. `p = FindRec(this, key, 1)`: own list, chain, or a new child of `this`.
	FKvNode* Owner = nullptr;
	EKvFound Found = EKvFound::None;
	const FString KeyString = KeyText(Key);
	FKvNode* P = FindKeyIn(Node, KeyString, EKvCreate::Yes, Owner, Found);
	if (Sites != nullptr)
	{
		Sites->Site(TEXT("kv.find"), TEXT("FUN_10248900"), 0x10248900u, TEXT("return"),
			FString::Printf(TEXT("key=%s create=1 source=%s name=%s"), *Shown(KeyString), FoundText(Found),
				P != nullptr ? *Shown(P->Name) : TEXT("null")));
	}
	// 2. `p == NULL` -> return (allocation failure only).
	if (P == nullptr)
	{
		return;
	}
	// 3. `free(p->+0x04)`; `p->+0x04 = copy(value)`; `p->+0x0C = 0`. `+0x08` keeps its bits.
	P->StringValue = Value;
	P->Type = EKvType::String;
	if (Sites != nullptr)
	{
		Sites->Site(TEXT("kv.setstr"), TEXT("FUN_102490e0"), 0x102490e0u, TEXT("write"),
			FString::Printf(TEXT("key=%s new=%s type=0"), *Shown(KeyString), *Shown(Value)));
	}
	if (Owner != nullptr)
	{
		Reindex(*Owner);
	}
}

FString GetString(FKvNode& Node, const TCHAR* Key, const FString& Default, IElysiumRetailSiteSink* Sites)
{
	// `0x10248cd0`. 1. `node = this; if (key) node = FindRec(this, key, 0)`.
	FKvNode* Found = &Node;
	if (Key != nullptr)
	{
		Found = FindKey(Node, Key, EKvCreate::No, nullptr, Sites);
	}
	const FString KeyString = KeyText(Key);
	// 2. `node == NULL` -> return `default`.
	if (Found == nullptr)
	{
		if (Sites != nullptr)
		{
			Sites->Site(TEXT("kv.getstr"), TEXT("FUN_10248cd0"), 0x10248cd0u, TEXT("return"),
				FString::Printf(TEXT("key=%s result=%s default=1"), *Shown(KeyString), *Shown(Default)));
		}
		return Default;
	}
	// 3. Dispatch on `+0x0C`: 1 -> "%d" (`0x105461f0`); 2 -> "%f" (`0x10554f28`) of `(double)(float)`;
	//    3 -> "%d"; anything else -> the text with no writeback. `Q_snprintf(buf[64], 0x40, ...)`: an
	//    int32 or a float32 under `%f` is at most 46 characters, so the bound never cuts.
	FString Formatted;
	bool bFormatted = false;
	switch (Found->Type)
	{
	case EKvType::Int:
		Formatted = FString::Printf(TEXT("%d"), Found->IntValue);
		bFormatted = true;
		break;
	case EKvType::Float:
		Formatted = FString::Printf(TEXT("%f"), static_cast<double>(Found->FloatValue));
		bFormatted = true;
		break;
	default:
		break;
	}
	if (Sites != nullptr)
	{
		Sites->Site(TEXT("kv.getstr"), TEXT("FUN_10248cd0"), 0x10248cd0u, TEXT("branch"),
			FString::Printf(TEXT("key=%s type=%s formatted=%s"), *Shown(KeyString), FileTypeCode(Found->Type),
				bFormatted ? *Shown(Formatted) : TEXT("none")));
	}
	if (bFormatted)
	{
		// 6. `FUN_102490e0(this, key, buf)`: the ORIGINAL `this` and `key` (the same search finds the
		//    same node, in `this`'s list or its chain); its text becomes `buf`, its type 0.
		SetString(Node, Key, Formatted, Sites);
	}
	// 7. `return node->+0x04`, re-read after the writeback.
	const FString& Result = Found->StringValue;
	if (Sites != nullptr)
	{
		Sites->Site(TEXT("kv.getstr"), TEXT("FUN_10248cd0"), 0x10248cd0u, TEXT("return"),
			FString::Printf(TEXT("key=%s result=%s default=0"), *Shown(KeyString), *Shown(Result)));
	}
	return Result;
}

int32 GetInt(FKvNode& Node, const TCHAR* Key, int32 Default, IElysiumRetailSiteSink* Sites)
{
	// `0x10248bb0`. 1. `if (key) node = FindRec(this, key, 0)`.
	FKvNode* Found = &Node;
	if (Key != nullptr)
	{
		Found = FindKey(Node, Key, EKvCreate::No, nullptr, Sites);
	}
	const FString KeyString = KeyText(Key);
	auto Return = [&KeyString, Default, Sites](const TCHAR* Type, int32 Result) -> int32
	{
		if (Sites != nullptr)
		{
			Sites->Site(TEXT("kv.getint"), TEXT("FUN_10248bb0"), 0x10248bb0u, TEXT("return"),
				FString::Printf(TEXT("key=%s type=%s default=%d result=%d"), *Shown(KeyString), Type, Default, Result));
		}
		return Result;
	};
	// 2. `node == NULL` -> `default`.
	if (Found == nullptr)
	{
		return Return(TEXT("none"), Default);
	}
	switch (Found->Type)
	{
	case EKvType::String:
		// 3. Type 0: `atoi(+0x04)` (`_atol`, wrapping).
		return Return(FileTypeCode(Found->Type), RetailAtol(Found->StringValue));
	case EKvType::Float:
		// 4. Type 2: `__ftol(float +0x08)`, the low 32 bits.
		return Return(FileTypeCode(Found->Type), RetailFtol(static_cast<double>(Found->FloatValue)));
	case EKvType::Int:
		// 5. Otherwise (1, 3, others): `+0x08` as int.
		return Return(FileTypeCode(Found->Type), Found->IntValue);
	default:
		// A block: retail's `+0x0C` is heap garbage here (UNRECOVERED), its `+0x04` NULL. The port's
		// block reads as type 0 with `""`, `atoi("") = 0`.
		return Return(FileTypeCode(Found->Type), 0);
	}
}

} // namespace ElysiumKeyValues
