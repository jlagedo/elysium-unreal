#pragma once

#include "CoreMinimal.h"

// The baked sound family's address space (AUD1.2, owner call 2026-09-08).
//
// Every V2 `sound` unit is baked to one `USoundWave` under `/ElysiumBaked/Sounds/**/SW_<name>`, so
// "does this reference resolve" is an asset-existence question rather than a file probe, and
// "resolve the soundgroup" is retail's directory walk carried out over package paths. Nothing here
// opens a file, and nothing here loads an asset: it answers only from the asset registry's index.
//
// The key is the corpus-relative path below `sound/`, folded exactly as
// `UElysiumAudioSubsystem::NormalizeSourcePath` folds it -- lower case, forward slashes, EXTENSION
// KEPT, because 13 shipped stems ship as both `.wav` and `.mp3` and the two are different units.
// The fold to an asset name is `FElysiumContentPaths::BakedUnit("vtmb:sound:" + key, "SW")`, whose
// Python twin is `elysium_pipeline.asset_paths.baked_unit`; both must agree exactly or the runtime
// asks for a package the bake did not write, and `pipeline/tests/fixtures/sound_asset_paths.json`
// is the contract both sides are tested against (65 entries).
//
// Sounds take their OWN fold (`FElysiumContentPaths::SoundSafeName`, `asset_names.sound_safe_name`)
// rather than the shared one: a space becomes a HYPHEN before the run collapse, so the 14
// space-vs-underscore twin pairs in the corpus stay distinct, and a stem keeps a leading
// underscore so `_period.wav` is addressable at all.
namespace ElysiumSoundAssets
{
	// The bake's package root. One scan of this path answers every question below.
	ELYSIUMUE_API const TCHAR* PackageRoot();

	// One resolved reference.
	//
	// `LoopObjectPath` is set only for a unit whose `smpl`/`cue ` region has a real intro ahead of
	// the loop body: the bake splits those into `SW_<name>_intro` (a one-shot) and `SW_<name>_loop`
	// (`bLooping`), and the voice plays the first and chains the second. Three units in the shipped
	// corpus are like that (`area/hollywood/warrens/flow_on.wav`,
	// `environmental/machines/steam2.wav`, `steam3.wav`). A whole-file loop is ONE asset with
	// `bLooping` set at bake, so it comes back here as a plain `ObjectPath`.
	struct FRef
	{
		FString ObjectPath;
		FString LoopObjectPath;

		bool IsValid() const { return !ObjectPath.IsEmpty(); }
		bool HasIntroLoopPair() const { return !LoopObjectPath.IsEmpty(); }
	};

	// The object path a key names, whether or not the bake carries it. Empty only for a key the
	// baked-unit contract rejects (empty, or a segment that is `.`, `..` or starts with `_`).
	ELYSIUMUE_API FString ObjectPathFor(const FString& Rel);
	// The same, for one of the split loop halves (`Role` is "intro" or "loop").
	ELYSIUMUE_API FString VariantObjectPathFor(const FString& Rel, const TCHAR* Role);

	// Does the bake carry this key -- as a whole asset, or as an intro+loop pair?
	ELYSIUMUE_API bool Exists(const FString& Rel);
	ELYSIUMUE_API FRef Resolve(const FString& Rel);

	// Retail's directory walk, over packages. `Folder` is a corpus-relative directory
	// (`usable/openable/door_wood`), folded like a key.
	ELYSIUMUE_API bool FolderExists(const FString& Folder);
	// Every key the bake carries directly under `Folder`, sorted, as `<folder>/<name>.<ext>`.
	//
	// These are FOLDED spellings recovered from the asset name (`SW_pl_step1_wav` ->
	// `pl_step1.wav`): an enumerated member has no authored spelling for the registry to have kept.
	// The recovery is exact in the only direction that matters -- folding one of these keys again
	// lands back on the asset it came from -- which is what lets an enumerated pick be submitted as
	// an ordinary request.
	ELYSIUMUE_API TArray<FString> ListFolder(const FString& Folder);
	// The immediate sub-directory names under `Folder`, sorted (the soundgroup walk's group list).
	ELYSIUMUE_API TArray<FString> ListSubfolders(const FString& Folder);

	// How many baked sound assets the index answered with, and whether it has been built. For the
	// audio debugger and the diagnostics only.
	ELYSIUMUE_API int32 Count();

	// Drop the index so the next question rebuilds it. Called when the registry gains assets a
	// running session did not scan (a bake landing under a live editor).
	ELYSIUMUE_API void Invalidate();

	// Test seam. While one of these is alive the index is the fabricated key set instead of the
	// asset registry, so a test can pin resolution order and the directory walk without VtMB data
	// and without a baked asset (no test may read either -- commit 44ac84f6).
	class ELYSIUMUE_API FScopedKeySet
	{
	public:
		explicit FScopedKeySet(TArray<FString> Keys);
		~FScopedKeySet();

		FScopedKeySet(const FScopedKeySet&) = delete;
		FScopedKeySet& operator=(const FScopedKeySet&) = delete;
	};
}
