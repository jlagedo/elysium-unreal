#pragma once

#include "CoreMinimal.h"

// VtMB's KeyValues reader (`VKeyValues`, `vampire.dll`), shared by every runtime consumer of the
// `.res`/`.vmt`/`vdata` grammar: the sound schemes, the sign definitions, the rulebook tables, the
// camera shots. The retail chain is `docs/specs/layers/L0-entity/walks/L0-r003.md`:
//
//   * `0x1023eff0` builds a 256-entry membership table from a delimiter string;
//   * `0x102473e0` builds the two tables once -- A `{}()'` and B `{}()':`;
//   * `0x10247280` is the tokenizer: skip bytes <= 0x20 (controls, space, and every byte >= 0x80,
//     which is negative as a signed char), `//` and `/* */` comments at token start, a quoted string
//     with `\"` as its one escape (raw bytes otherwise, newlines kept), a one-byte delimiter token, or
//     a bare word that ends at whitespace or a delimiter (never at `"` or `/`). Returns the next
//     cursor, or NULL at end of text; reports whether the token was quoted;
//   * `0x101f2f30` wraps it over a cursor and the shared token buffer `0x1073AA80`;
//   * `0x101f2360` parses a block recursively: `}` or an empty key closes, every child is appended in
//     file order, `{` recurses, and an unquoted leaf is typed by `strtol`/`strtod` (int, float or
//     string) while a quoted leaf stays a string;
//   * `0x101f2180` reads a whole file into named roots: each top-level key must be followed by `{`,
//     a missing brace logs `ERROR: parsing KeyValue in file %s, expecting {, got {` and the node is
//     reused for the next key; roots chain through `+0x18`.
//
// The table selector byte `DAT_10753641` is never written, so table B is live: `:` is a delimiter
// (`a:b` is three tokens). Names are stored verbatim (`0x101f2090`); retail's in-tree name compare is
// `__strcmpi` (`0x1043E780`), case-insensitive, which the port's lowercase lookup index reproduces.
//
// Two representation choices, named: the lexer runs on TCHARs where retail runs on bytes (a byte >= 0x80
// and a TCHAR >= 0x80 are classified alike, and a quoted string keeps either verbatim), and a cursor past
// the terminating NUL (retail: after an unterminated quote) reads NUL here where retail reads the heap.

struct IElysiumRetailSiteSink;   // ElysiumRetailSite.h: the `retail_site` tap the reader reports through

namespace ElysiumKeyValues
{
	// Node `+0x10`.
	enum class EKvType : uint8
	{
		Block = 0,   // a `{ }` child, or a node whose value was never read (SetName/Clear zero the type)
		String = 1,  // a quoted leaf, or an unquoted one neither strtol nor strtod consumed
		Int = 2,     // strtol consumed as much as strtod: `+0x08` holds the int
		Float = 3,   // strtod consumed more: `+0x08` holds the float
	};

	struct FKvNode
	{
		// --- The retail node (`VKeyValues`, 0x38 bytes from pool `0x1073D280`) -----------------------
		FString Name;                   // `+0x14`, verbatim as `0x101f2090` interns it
		EKvType Type = EKvType::Block;  // `+0x10`
		FString StringValue;            // `+0x0C`: every leaf's text (`0x101f2f70` interns it, typed or not)
		int32 IntValue = 0;             // `+0x08` read as int bits (Type == Int)
		float FloatValue = 0.f;         // `+0x08` read as float bits (Type == Float)
		// `+0x1C` first child, each child's `+0x18` next sibling: leaves and blocks in file order.
		TArray<TSharedPtr<FKvNode>> Children;

		// --- The port's lookup index over `Children`, keys folded to lower ------------------------
		// Retail compares names with `__strcmpi` (`0x1043E780`): a lowercase index answers the same
		// lookups. Repeated block keys are preserved in order (a scheme's `RandomSound`, a sign's
		// `TextBlock`), and so are repeated leaf keys -- `Values` keeps the last, `Pairs` keeps them all.
		TMap<FString, FString> Values;                     // leaf key -> value (last wins)
		TArray<TPair<FString, FString>> Pairs;             // the same leaves in file order, repeats kept
		TArray<TPair<FString, TSharedPtr<FKvNode>>> Kids;  // ordered child blocks (repeatable keys)

		const FString* Value(const TCHAR* Key) const { return Values.Find(FString(Key).ToLower()); }
		FString Str(const TCHAR* Key, const FString& Def) const { const FString* V = Value(Key); return V ? *V : Def; }
		float   Flt(const TCHAR* Key, float Def) const { const FString* V = Value(Key); return V ? FCString::Atof(**V) : Def; }
		int32   Int(const TCHAR* Key, int32 Def) const { const FString* V = Value(Key); return V ? FCString::Atoi(**V) : Def; }
		bool    Bool(const TCHAR* Key, bool Def) const { const FString* V = Value(Key); return V ? (FCString::Atoi(**V) != 0) : Def; }

		// True when the key is present at all (an authored-but-empty value is meaningful: a sign's
		// `"XPos" ""` selects CSignUI's centring branch, which differs from XPos being absent).
		bool Has(const TCHAR* Key) const { return Value(Key) != nullptr; }

		// Every value authored under Key, in file order. A block MAY repeat a leaf key and mean it:
		// 17 of `stats.txt`'s Active_Disciplines carry two `IncPredependency` gates ("BloodPool > 0"
		// and "Health < Max_Health"), and one `clandoc000.txt` General block names `M_Hands` twice.
		// `Values` keeps only the last of those, so a reader that needs the whole set reads here.
		void ValuesFor(const TCHAR* Key, TArray<FString>& Out) const
		{
			const FString L = FString(Key).ToLower();
			for (const TPair<FString, FString>& P : Pairs) { if (P.Key == L) { Out.Add(P.Value); } }
		}

		const FKvNode* Child(const TCHAR* Key) const
		{
			const FString L = FString(Key).ToLower();
			for (const TPair<FString, TSharedPtr<FKvNode>>& K : Kids) { if (K.Key == L) { return K.Value.Get(); } }
			return nullptr;
		}
	};

	// `0x101f2f30`'s cursor and the shared token buffer `0x1073AA80`, over one NUL-terminated text
	// (`0x101f2180` R3..R6: the file's bytes plus a NUL). `Text` is borrowed: the FString outlives the
	// reader. `Pos` is the cursor; `INDEX_NONE` is retail's NULL cursor.
	struct FKvReader
	{
		explicit FKvReader(const FString& InText, IElysiumRetailSiteSink* InSites = nullptr)
			: Text(*InText), Len(InText.Len()), Sites(InSites) {}

		const TCHAR* Text;
		int32 Len;
		int32 Pos = 0;
		FString Token;                            // the shared buffer `0x1073AA80`
		IElysiumRetailSiteSink* Sites = nullptr;  // the `kv_token` / `kv_leaf` / `kv_root` taps
	};

	// `0x1023eff0`: zero 256 entries, then mark each byte of `Delims` (signed index: a byte >= 0x80
	// would write before the table; neither retail string has one, and the port skips it).
	void BuildDelimiterTable(uint8 (&Table)[256], const ANSICHAR* Delims);

	// `0x102473e0`: the two tables, built once. A `0x10753440` from `{}()'` (`0x105794c0`), B
	// `0x10753540` from `{}()':` (`0x105c4ef8`); the once-flag is `0x10753640`.
	struct FKvDelimiterTables
	{
		uint8 A[256];
		uint8 B[256];
	};
	const FKvDelimiterTables& DelimiterTables();

	// `0x10247280`: the next token from `R.Pos` into `Out`, `*Quoted` (may be null) set to 1 for a
	// quoted string, 0 otherwise. Returns the next cursor, or `INDEX_NONE` for a NULL cursor in or
	// end of text (`Out` is then `""`; untouched for a NULL cursor in). Does not move `R.Pos`.
	int32 NextToken(FKvReader& R, FString& Out, uint8* Quoted);

	// `0x101f2f30`: `NextToken` over `R.Pos` into `R.Token`, which it returns; `R.Pos` becomes the
	// next cursor. The reference is to the shared buffer: the next call overwrites it.
	const FString& ReadToken(FKvReader& R, uint8* Quoted);

	// `0x101f2cf0`: a new node named `Name`, appended at the tail of `Parent`'s child list.
	TSharedPtr<FKvNode> AddChild(FKvNode& Parent, const FString& Name);
	// `0x101f2c60`: release the children, free the value and name texts, zero the type and links.
	void Clear(FKvNode& Node);
	// `0x101f2090`: intern `Name`; zero the value, the type, the children and the links.
	void SetName(FKvNode& Node, const FString& Name);

	// `0x101f2360`: parse `Node`'s block from `R` until `}`, an empty key, or end of text.
	void ParseBlock(FKvNode& Node, FKvReader& R);

	// `0x101f2180` from R7 on (the caller has read the file into `R`): the root loop. `Target`, when
	// set, is the node the first root reuses (`0x101f2e20` passes the cache node it just named after
	// the file; ECX at `0x101f218b`). Every root, in chain order, lands in `OutRoots` -- `Target`
	// first when it was used, then each node `0x101f2cd0` linked. Always true: Open/Size/alloc
	// failures (the false returns) are the file arms, which the caller owns.
	bool ParseRoots(FKvReader& R, const FString& FileName, const TSharedPtr<FKvNode>& Target,
		TArray<TSharedPtr<FKvNode>>& OutRoots);

	// The port's view of a root chain: a node whose `Children`/`Kids` are the roots in chain order
	// (retail hangs them off the cache node's `+0x18`). Null when there are no roots.
	TSharedPtr<FKvNode> RootsView(const TArray<TSharedPtr<FKvNode>>& Roots);

	// Parse a whole text with no target node and return the roots view; null when the text has no
	// roots (empty or whitespace only).
	TSharedPtr<FKvNode> ParseText(const FString& Text, IElysiumRetailSiteSink* Sites = nullptr);
}
