// 12.5 — the `.lip` phoneme document, and the join that turns one into flex-controller writes.
//
// A `.lip` sits beside a line's audio with the extension swapped (`line1015_col_f.mp3` ->
// `line1015_col_f.lip`) and says which phoneme is on screen when. Same uniform grammar as the
// `.vcd` — every line is a whitespace-separated word list optionally followed by a `{ … }` block —
// so this reads with `ElysiumSceneData.cpp`'s machine rather than a second one.
//
// This is the data half plus the arithmetic: no world, no entity, no engine. Two drivers own the
// clock — `ElysiumChoreoScene.cpp` for a cinematic's `speak` events and `ElysiumEntityWorld.cpp`
// for a `.dlg` turn — and both feed the same `Accumulate` below.
//
// Format, the corpus counts and the recovered envelope: `docs/vtmb/facial_animation.md`.

#pragma once

#include "CoreMinimal.h"

#include "Visual/ElysiumExpressionTable.h"

// One `<code> <phoneme> <start> <end> <volume> [<flag>]` row.
struct FElysiumLipPhoneme
{
	// **The key.** The leading integer, which `client.dll` uses as an index into the table's own
	// code->row array (`FUN_100c4940`), and which resolves through the row's *class* column here.
	// 48 distinct codes across the whole corpus, and every real phoneme table carries all 48.
	int32 Code = INDEX_NONE;
	// The second field. Carried for diagnostics only — it is **not** a usable key: the 48 codes
	// appear under 555 distinct (code, string) pairs, `k` names the row called `c` 12,444 times, and
	// `ax` shows up under nine codes naming nine different rows.
	FString Phoneme;
	// Seconds from the start of the audio.
	float Start = 0.f;
	float End = 0.f;
};

struct FElysiumLipWord
{
	FString Text;
	float Start = 0.f;
	float End = 0.f;
	TArray<FElysiumLipPhoneme> Phonemes;
};

// One phoneme's live contribution at an instant: the row to look up and the envelope weight to
// scale it by.
struct FElysiumLipSample
{
	const FElysiumLipPhoneme* Phoneme = nullptr;
	float Scale = 0.f;
};

struct FElysiumLipTrack
{
	// The normalized mirror-relative key this was loaded under (lowercased, `sound/` stripped).
	FString SourceRel;
	// `VERSION` — 1.0 / 1.1 / 1.2 on 157 / 501 / 6,478 files. A 1.0 or 1.1 row omits the sixth
	// field, which is why the row parser accepts both widths.
	FString Version;
	// `OPTIONS`. Carried for diagnostics only — nothing downstream reads them.
	FString SpeakerName;
	bool bVoiceDuck = false;

	TArray<FElysiumLipWord> Words;

	// max End over every phoneme, or over the words when a word carries none.
	float LatestTime = 0.f;

	// Rows whose field count or numbers did not parse. Counted rather than fatal, like
	// `FElysiumSceneData::NumDegenerate`.
	int32 NumMalformedRows = 0;

	// A track with no phoneme moves no mouth. 31 of the 7,136 files have no sibling audio and some
	// carry a single placeholder word with an empty block (`WORD blah 0.000 22.000`), which parses
	// cleanly and is correctly inert.
	bool bValid = false;

	int32 NumPhonemes() const;

	// The phonemes live at `Seconds` (audio-relative), with the recovered envelope's weight on each.
	//
	//     S = clamp(End - Start, BlendMin, BlendMax)
	//     A = (Start - t) / S,  dead when A >= 1,  then A = max(A, 0)
	//     B = (End   - t) / S,  dead when B <= 0,  then B = min(B, 1)
	//     Scale = B - A
	//
	// So a phoneme ramps in over the S BEFORE its authored start, and decays to exactly zero at its
	// authored end — it is not symmetric inside the span. A span shorter than S never reaches 1; its
	// peak is span/S, at the authored start. Spans abut within a word (2,075 times against 164 gaps
	// across the theatre lines), so one phoneme's decay overlaps the next one's lead-in and the
	// additive accumulate sums them.
	//
	// `Out` is reset. Empty inside an inter-word gap, which is correct: nothing accumulates and the
	// mouth relaxes to whatever else is driving it.
	void SampleAt(float Seconds, float BlendMin, float BlendMax, TArray<FElysiumLipSample>& Out) const;
};

// A line joined to the face that speaks it: the timing, the weights, and the model's own blend
// width. Held by whichever driver owns the line, and pure — it has no clock of its own.
struct FElysiumLipSyncBinding
{
	TSharedPtr<const FElysiumLipTrack> Track;
	// `expressions/<model stem>_phonemes.txt`, resolved through `ElysiumExpressions::Load`.
	TSharedPtr<const FElysiumExpressionTable> Table;

	// `studiohdr` +232/+236 — the phoneme filter, authored **per model** and genuinely varying
	// across the rigged cast: 57 carry (0.065, 0.100), 32 carry (0.080, 0.100) and `Jeanette` alone
	// (0.080, 0.105). The defaults are the modal pair, which is also what all three of sp_theatre's
	// speakers carry, and they stand in until the facial sidecar's `phoneme_filter` is plumbed
	// through to here.
	float BlendMin = 0.065f;
	float BlendMax = 0.10f;

	bool IsValid() const { return Track.IsValid() && Table.IsValid() && Track->bValid; }

	// Accumulate this line's phoneme pose at `LineSeconds` into `InOutPose`, keyed by flex-controller
	// name. **Additive, then clamped to [0,1]** — retail's `g_flexweight[i] += scale × value_i` onto
	// a surface expression and blink share, and every shipped controller is min 0 / max 1, so
	// accumulate-then-clamp is what its remap-then-add comes to.
	//
	// The row's value column alone is applied. The `$hasweighting` influence beside it is **not**:
	// `lacroix_phonemes`'s `r2` row carries 0.050 under influence 0.000, so folding the influence in
	// would silently diverge rather than be the no-op the 0/1 histogram suggests. The `.lip`'s own
	// volume column is not applied either — neither reaches the accumulate in retail.
	//
	// Returns how many phonemes contributed, and adds any phoneme string its table does not carry to
	// `OutUnresolved` so a caller can report them once instead of dropping them silently.
	int32 Accumulate(float LineSeconds, TMap<FString, float>& InOutPose,
		TArray<FString>* OutUnresolved = nullptr) const;
};

namespace ElysiumLip
{
	// A `speak` event's `param` (or a dialogue line's source path) -> the mirror-relative `.lip` key.
	// Identical folding to `ElysiumScene::NormalizeSceneRel` — both mirrors strip the same `sound/`
	// prefix — with the extension swapped for `.lip`.
	FString NormalizeLipRel(const FString& AudioPath);

	// Parse in-memory text. Never asserts: the corpus carries malformed rows and blocks no reader
	// uses, and a file that yields no phoneme comes back with bValid false.
	void ParseText(const FString& Text, const FString& SourceRel, FElysiumLipTrack& Out);

	// Load and parse through the shared cache, keyed on the normalized path. Null when the file is
	// missing or carries nothing usable — and that negative is cached too, because the scripts probe
	// for absent `.lip` files as game logic (`docs/vtmb/python_bridge.md`) and a conversation
	// replays the same lines.
	TSharedPtr<const FElysiumLipTrack> Load(const FString& AudioPath);

	// Seed the cache under a key so a headless test can drive a line without touching
	// `$ELYSIUM_EXPORT_ROOT`. Overwrites any cached entry.
	void RegisterInline(const FString& Key, const FString& Text);

	void ClearCache();
	void CacheStats(int32& OutEntries, int32& OutHits, int32& OutMisses);
}
