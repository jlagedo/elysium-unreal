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
//
// vampire.dll carries a SECOND KeyValues class over the same tokenizer: the 0x1C-byte node of the
// `0x102480f0` file loader (`+0x00` name, `+0x04` value text, `+0x08` int/float, `+0x0C` type 0 string /
// 1 int / 2 float / 3 ptr, `+0x10` next sibling, `+0x14` first child, `+0x18` fallback chain) -- the
// 2003 vgui2 `KeyValues` layout (`m_sValue`, `m_iValue`, `m_iDataType`, `m_pPeer`, `m_pSub`, `m_pChain`)
// -- read by the sound schemes, the soundscapes, the signs, the keypads, the terminals, the radios, the
// quest journal and the sound-volume table (`docs/specs/layers/L0-entity/walks/L0-r004.md`). Its loader
// lives in `ElysiumKeyValuesLoader.h`; its accessors (`FindKey`, `GetInt`, `GetString`, `SetString`)
// are below. Both classes share `FKvNode` here: `Type` is the SEMANTIC type and each class's numeric
// code is spelled where a retail site reports it (`FileTypeCode`).

struct IElysiumRetailSiteSink;   // ElysiumRetailSite.h: the `retail_site` tap the reader reports through

namespace ElysiumKeyValues
{
	// Node `+0x10` of `VKeyValues`. The file loader's class spells the same kinds 0 string / 1 int /
	// 2 float (`FileTypeCode`), and leaves a block's type word UNWRITTEN (heap garbage; the port says
	// `Block`).
	enum class EKvType : uint8
	{
		Block = 0,   // a `{ }` child, or a node whose value was never read (SetName/Clear zero the type)
		String = 1,  // a quoted leaf, or an unquoted one neither strtol nor strtod consumed
		Int = 2,     // strtol consumed as much as strtod: `+0x08` holds the int
		Float = 3,   // strtod consumed more: `+0x08` holds the float
	};

	// The file loader's class code for a type (`0x10248510` arms 7/8 write 0 / 1 / 2; `0x10248cd0` and
	// `0x10248bb0` dispatch on it). A block's word is never written in retail: `unset`.
	const TCHAR* FileTypeCode(EKvType Type);

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
		// The file loader's class only: `+0x18`, the fallback chain `0x10248900` arm 3 searches when a
		// key is not in the own list. Zeroed by the name setter `0x10247cf0`; no non-NULL writer was
		// found in the corpus (walk L0-r004 open question 6: the vgui2 `ChainKeyValue`), so only the
		// harness links one.
		TWeakPtr<FKvNode> Chain;

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

	// --- Shared CRT models (VC6 SP5 libc, in-image) ------------------------------------------------

	// `_strtol` (`0x104319A8` -> `_strtoxl`), base 10: leading `isspace`, one sign, digits; `OutEnd`
	// is the end pointer as an index, 0 when nothing was consumed; overflow saturates to LONG_MAX /
	// LONG_MIN.
	int32 RetailStrtol(const FString& S, int32& OutEnd);
	// `_strtod` (`0x1043190E` -> `__fltin` -> `___strgtold12`): sign, digits, `.`, an `e E d D`
	// exponent; no hex, no inf/nan; `OutEnd` as above.
	double RetailStrtod(const FString& S, int32& OutEnd);
	// `_atoi` -> `_atol` (`0x10431447` -> `0x104313bc`): leading `isspace`, one sign, digits
	// accumulated in 32 bits with wraparound (no clamp), stopping at the first non-digit.
	int32 RetailAtol(const FString& S);
	// `__ftol` (`0x10431320`): truncate toward zero to int64 under a truncating control word, EAX =
	// the low 32 bits; a value outside int64 (+-Inf, NaN, |x| >= 2^63) stores the integer indefinite
	// `0x8000000000000000`, low word 0.
	int32 RetailFtol(double X);
	// A token or value as a one-line trace payload (controls escaped).
	FString Shown(const FString& S);

	// --- The file loader's class: lookup and typed access (`0x10248900` family) ---------------------
	//
	// `this` is a container node (a root the loader filled, or any block); the key is compared with
	// `__strcmpi` against each child in list order, first match wins.

	// `0x10248900`'s third argument.
	enum class EKvCreate : uint8 { No, Yes };

	// Where `0x10248900` found (or made) the node, for the `kv.find` site.
	enum class EKvFound : uint8 { None, Own, Chain, Created };

	// `0x10248870`: a new node named `Name`, appended at the tail of `Parent`'s `+0x14` list (walks
	// `+0x10` to the last sibling). `+0x08` and `+0x0C` are NOT written (the port's `Block` default
	// stands for the unwritten word); the FindNext buffer `DAT_10753f68` byte 0 is cleared.
	TSharedPtr<FKvNode> CreateChild(FKvNode& Parent, const FString& Name);

	// `0x10248900` `(this, key, create)`: the first child of `this` whose name matches `key`
	// case-insensitively; else the fallback chain (`+0x18`, recursive, never creating); else, when
	// `create`, a new child of `this`; else NULL. `Key` NULL compares as `""` (retail `strcmpi(name,
	// NULL)` faults on a node with children; the one caller that passes NULL, `0x101b2c60`, does so
	// on leaves). `OutFound` says which arm answered.
	FKvNode* FindKey(FKvNode& Node, const TCHAR* Key, EKvCreate Create, EKvFound* OutFound = nullptr,
		IElysiumRetailSiteSink* Sites = nullptr);

	// `0x102490e0` `(this, key, value)`: `FindKey(key, create)`; free and replace the node's text
	// (`+0x04`) with a copy of `value`; type (`+0x0C`) = 0 string. `+0x08` is untouched.
	void SetString(FKvNode& Node, const TCHAR* Key, const FString& Value, IElysiumRetailSiteSink* Sites = nullptr);

	// `0x10248cd0` `(this, key, default)`: `key` NULL -> the node is `this`, else `FindKey(key, no
	// create)`; absent -> `default`. Type 1 or 3 -> `Q_snprintf(buf, 0x40, "%d", +0x08)`
	// (`0x105461f0`); type 2 -> `"%f"` of the float widened to double (`0x10554f28`); then
	// `SetString(this, key, buf)` writes the text back and resets the type to 0. Returns the node's
	// text (`+0x04`) after any writeback -- NULL for a block (no text) reads as `""` here.
	FString GetString(FKvNode& Node, const TCHAR* Key, const FString& Default, IElysiumRetailSiteSink* Sites = nullptr);

	// `0x10248bb0` `(this, key, default)`: as above for the lookup; type 0 -> `atoi(+0x04)` (wrapping
	// `_atol`); type 2 -> `__ftol(float +0x08)`; any other type -> `+0x08` as int. No writes.
	int32 GetInt(FKvNode& Node, const TCHAR* Key, int32 Default, IElysiumRetailSiteSink* Sites = nullptr);

	// Rebuild the port's lookup index (`Kids` / `Values` / `Pairs`) over `Children` after a mutation
	// (`SetString`, `CreateChild`). The index is a port view of the retail list, never retail state.
	void Reindex(FKvNode& Node);
}
