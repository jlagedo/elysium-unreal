#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

// Faceposer's flex-controller weight table — `$ELYSIUM_EXPORT_ROOT/expressions/<stem>.txt`.
//
// One file is a named set of rows over a shared key list, and a key is a flex-controller name
// (`Visual/ElysiumFacialRig.h`). 249 tables ship: 121 `<stem>_phonemes`, 121 `<stem>_expressions`
// and 7 unsuffixed. Both halves are the same grammar, so this reads both — a scene's `expression`
// event picks a row out of the expression half, and lipsync picks one per phoneme out of the other.
//
//     $keys right_cheek_raiser left_cheek_raiser wrinkler … lower_lip
//     $hasweighting
//     "Joy" "_" 0.000 0.000 … 0.390 1.000 … "Debauched Smile"
//
// `$hasweighting` is set on all 249 and means each row carries **two** floats per key — the value
// and its weight — so a row holds `2 × len($keys)` numbers between the two quoted names and the
// trailing quoted description. Format and provenance: `docs/vtmb/facial_animation.md`.

// One row: `"<name>" "<class>" <numbers…> "<description>"`.
struct FElysiumExpressionRow
{
	// The first quoted word — what an `expression` event's `param2` names, and what a phoneme row
	// is keyed by. Authored case and stray trailing spaces both occur; FindRow folds them.
	FString Name;
	// The second quoted word. `_` on every expression row; on a phoneme row it is the phoneme's IPA
	// form, written either as a single character (`k`, `h`, `s`) or as `0x….` (`0x0279`).
	FString Class;
	// `Class` as a code point — the key a `.lip` row's leading integer actually is. INDEX_NONE when
	// the class is neither a single character nor `0x….` (every `_` row, i.e. the whole expression
	// half plus `<sil>`).
	int32 PhonemeCode = INDEX_NONE;
	// The trailing quoted word, authoring commentary ("Big : voiced alveolar stop").
	FString Description;

	// Index-aligned with the table's `Keys`. `Weights` is Faceposer's influence: the fraction of the
	// key this row claims, so a key at weight 0 is one the row does not participate in at all. It is
	// exactly 0 or 1 on all 267,755 shipped entries, but it is read as the float it is.
	TArray<float> Values;
	TArray<float> Weights;
};

struct FElysiumExpressionTable
{
	// The file stem this was loaded under (`lacroix_expressions`), for diagnostics.
	FString Stem;
	// `$keys` — the flex-controller names this table writes. 22–33 entries on the modal file, 48
	// distinct names across the whole set, all drawn from the `phoneme` and `mouth` families plus
	// the eyelid/brow/nose ones the expression half adds.
	TArray<FString> Keys;
	// `$hasweighting` — two floats per key rather than one.
	bool bHasWeighting = false;
	TArray<FElysiumExpressionRow> Rows;

	// `PhonemeCode` -> row. Built at parse; empty on an expression table, whose every class is `_`.
	TMap<int32, int32> RowByPhonemeCode;

	// Case-insensitive and trimmed: `therese_expressions` authors both `"sneer"` and
	// `"Anger_No Deform "`, and a scene's `param2` matches neither exactly.
	int32 FindRow(const FString& Name) const;

	// **The lipsync lookup.** A `.lip` phoneme row's leading integer, resolved through the class
	// column — which is what `client.dll` does: `FUN_100c4940` bounds-checks the code and indexes the
	// table's own code->row array with it, never a string.
	//
	// The phoneme *string* beside it is not usable. Across the 7,136 shipped files the 48 distinct
	// codes carry 555 distinct (code, string) pairs: `k` names the row whose own name is `c` 12,444
	// times, `ay` names `aa` 10,221 times, and `ax` appears under nine different codes naming nine
	// different rows. Keying on the string picks the wrong viseme far more often than not.
	int32 FindRowByPhonemeCode(int32 Code) const;

	bool IsValid() const { return !Keys.IsEmpty() && !Rows.IsEmpty(); }

	// Never asserts. A row whose number count disagrees with `$keys` is dropped and counted rather
	// than mis-split; OutError is set only when the file yields no usable table at all.
	bool ParseText(const FString& Text, const FString& InStem, FString& OutError);

	// Rows dropped because their number count did not match `2 × len(Keys)` (or `len(Keys)` without
	// `$hasweighting`). Zero across the shipped set; surfaced so a bad edit is visible.
	int32 NumMalformedRows = 0;
};

namespace ElysiumExpressions
{
	// The expression half's `Class` for `Load` — the class half of `client.dll`'s
	// `"expressions/%s_%s.vfe"` (FUN_100C42F0 supplies "phonemes" for the lipsync half,
	// `ElysiumLip::PhonemeClass`). Only reached when a `param` names a bare model stem rather than
	// the table itself — 4 of the 23 authored params do.
	inline constexpr const TCHAR* ExpressionClass = TEXT("expressions");

	// Native diagnostic/test lookup. Gameplay uses the prepared map-epoch expression views.
	// Resolves the same explicit table/class candidates through UElysiumExpressionTables;
	// unsupported or absent source data returns null with a diagnostic. Never reads loose files.
	TSharedPtr<const FElysiumExpressionTable> Load(const FString& Param, const FString& Class);

	// Seed the cache with table text under a stem, so a headless test can drive one without touching
	// `$ELYSIUM_EXPORT_ROOT`. Overwrites any cached entry.
	void RegisterInline(const FString& Stem, const FString& Text);
#if WITH_DEV_AUTOMATION_TESTS
	/** Only explicitly registered test text, never a table cached by native diagnostic lookup. */
	TSharedPtr<const FElysiumExpressionTable> FindInlineForTest(const FString& Param, const FString& Class);
#endif

	void ClearCache();
	void CacheStats(int32& OutEntries, int32& OutHits, int32& OutMisses);
}
