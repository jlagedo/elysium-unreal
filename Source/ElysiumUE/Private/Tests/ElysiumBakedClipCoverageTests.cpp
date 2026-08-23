// Every clip the runtime can resolve for a baked body is on the mount.
//
// WHY THIS IS A CONTRACT AND NOT A COVERAGE STATISTIC. Runtime has no second character build and
// no glTF animation fallback. A bank clip is authored once in the shared bank namespace and reused
// by compatible body skeletons. Missing one is a missing semantic bake stage, not permission to
// apply a corrective rotation at runtime.
//
// The property asserted here is not "the facing looks right", which the repo has no visual oracle
// for. It is that every clip name runtime can resolve has exactly one package in the namespace its
// owner selects. Any visible quarter-turn must remain visible until its missing semantic stage is
// identified.
//
// EVERY CELL OF A GRID, not the neutral pick. A grid's cell is chosen from pose parameters driven
// at runtime, so checking only what `move_yaw = 0` selects would leave eight of a nine-cell fan
// unverified -- and the cell is exactly what `ResolveGridClip` hands to `LoadBakedClip`. A blend
// space standing on the mount does not excuse its cells: `ResolveClip` and `ResolveClipFromBank`
// never consult the space, they resolve a cell and ask for that clip by name.
//
// NOTHING IS LOADED. A bank owner resolves under `_banks`; a body's own owner resolves to the one
// folder named for its own stem. The fresh-process character verifier owns deeper load and
// compatibility checks.
//
// TWO REACHES, BECAUSE THERE ARE TWO RESOLVERS. `ResolveClip`/`ResolveAssets` are driven by a
// body's own resolved vocabulary, which is enumerable per stem. `ResolveClipFromBank` is handed a
// bank stem and a clip name straight from a choreographed scene with no vocabulary in between, so
// its clip names are not enumerable -- but the BANKS it can be pointed at are: one per `bonerename`
// actor in the index's cinematic sets. A bank with no folder on the mount cannot answer any name,
// so checking the folder settles every clip it owns at once.
//
// Autolayer targets and additives are covered by `Elysium.Content.UpperBodyLayerArming` and are
// deliberately not repeated here -- they are composed rather than selected, the bake writes them
// under the derived `Label@Host` name, so a missing layer is a missing overlay.
//
// A body the export has not covered is COUNTED, not failed: it stands no mesh at all
// (`ElysiumNpcVisual::LoadMesh` fails by name), which is a visible missing body rather than a
// silently rotated one, and a partial export is the ordinary development state. The summary line
// reports how much of the indexed cast the run actually certified, so a vacuous pass is legible.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumNpcVisual.h"

#include "HAL/FileManager.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

static constexpr EAutomationTestFlags GElysiumClipCoverageFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Errors reported per body before the rest are summarised. One broken owner can miss on every
	// label it owns, and a thousand identical lines bury the other bodies.
	constexpr int32 GMaxErrorsPerBody = 8;

	bool PackageExists(const FString& ObjectPath)
	{
		return FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath));
	}

	// Which of the two clip namespaces an owner's packages live in. There is no lookup table for
	// this on the mount and none is wanted: the runtime itself decides by probing, so the test
	// decides the same way and in the same order (`ElysiumNpcVisual::LoadBakedClip`).
	enum class EOwnerKind : uint8
	{
		None,
		Bank,
		Body,
	};

	FString AnimsDir()
	{
		return FElysiumContentPaths::BakedCharacterDir() / TEXT("Anims");
	}

	bool FolderExists(const FString& PackagePath)
	{
		FString Dir;
		return FPackageName::TryConvertLongPackageNameToFilename(PackagePath, Dir)
			&& IFileManager::Get().DirectoryExists(*Dir);
	}

	// Bank first, exactly as the runtime probes. The two namespaces cannot collide -- `_banks` is
	// not a legal model stem -- so the order settles the answer rather than merely preferring one.
	EOwnerKind OwnerKindOf(const FString& Owner)
	{
		if (FolderExists(AnimsDir() / FElysiumContentPaths::BakedBankFolder() / Owner))
		{
			return EOwnerKind::Bank;
		}
		return FolderExists(AnimsDir() / Owner) ? EOwnerKind::Body : EOwnerKind::None;
	}

	FString AnimPathFor(const EOwnerKind Kind, const FString& Owner, const FString& Clip)
	{
		return Kind == EOwnerKind::Bank
			? FElysiumContentPaths::BakedBankAnim(Owner, Clip)
			: FElysiumContentPaths::BakedCharacterAnim(Owner, Clip);
	}

	FString BlendSpacePathFor(const EOwnerKind Kind, const FString& Owner, const FString& Label)
	{
		return Kind == EOwnerKind::Bank
			? FElysiumContentPaths::BakedBankBlendSpace(Owner, Label)
			: FElysiumContentPaths::BakedCharacterBlendSpace(Owner, Label);
	}

	// `-ElysiumCoverageStems=a,b,c` narrows the run while iterating. The default is the whole
	// indexed cast, because a subset cannot answer the question this test exists to answer.
	TArray<FString> CoverageStems(const FElysiumNpcIndex& Index)
	{
		FString Raw;
		if (FParse::Value(FCommandLine::Get(), TEXT("ElysiumCoverageStems="), Raw) && !Raw.IsEmpty())
		{
			TArray<FString> Selected;
			Raw.ParseIntoArray(Selected, TEXT(","), /*InCullEmpty=*/true);
			for (FString& Stem : Selected)
			{
				Stem.TrimStartAndEndInline();
			}
			if (!Selected.IsEmpty())
			{
				return Selected;
			}
		}
		TArray<FString> Stems;
		Index.Npcs.GenerateKeyArray(Stems);
		Stems.Sort();
		return Stems;
	}
}


#endif // WITH_DEV_AUTOMATION_TESTS
