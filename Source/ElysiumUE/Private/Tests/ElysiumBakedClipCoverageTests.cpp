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
// NOTHING IS LOADED. A bank owner resolves to `_banks`; a body's own owner resolves to its one
// family folder. The fresh-process character verifier owns deeper load and compatibility checks.
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

	// The rig-family folders the character bake wrote. A family is
	// named for its lowest-sorted member and is recomputed per bake run, so the set is discovered
	// rather than declared.
	TArray<FString> AnimFamilyFolders()
	{
		TArray<FString> Families;
		FString Dir;
		if (!FPackageName::TryConvertLongPackageNameToFilename(
			FElysiumContentPaths::BakedCharacterDir() / TEXT("Anims"), Dir))
		{
			return Families;
		}
		// Names only, directories only -- the family folder name IS the family.
		IFileManager::Get().FindFiles(Families, *(Dir / TEXT("*")), /*Files=*/false,
			/*Directories=*/true);
		Families.Sort();
		return Families;
	}

	FString FamilyFolderForOwner(const TArray<FString>& Families, const FString& Owner)
	{
		for (const FString& Family : Families)
		{
			FString Dir;
			if (FPackageName::TryConvertLongPackageNameToFilename(
				FElysiumContentPaths::BakedCharacterDir() / TEXT("Anims") / Family / Owner, Dir)
				&& IFileManager::Get().DirectoryExists(*Dir))
			{
				return Family;
			}
		}
		return FString();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBakedClipCoverageTest,
	"Elysium.Content.BakedClipCoverage", GElysiumClipCoverageFlags)
bool FElysiumBakedClipCoverageTest::RunTest(const FString&)
{
	if (FElysiumContentPaths::IsIncomplete(TEXT("npc")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the npc export domain is marked incomplete"));
		return true;
	}

	FElysiumNpcIndex Index;
	FString IndexError;
	if (!Index.Load(IndexError) || !Index.IsValid())
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc index (%s) -- run: uv run elysium export characters"),
			*IndexError));
		return true;
	}

	const TArray<FString> Stems = CoverageStems(Index);
	const TArray<FString> Families = AnimFamilyFolders();
	TMap<FString, FString> OwnerFolders;
	auto FolderFor = [&Families, &OwnerFolders](const FString& Owner) -> FString
	{
		if (const FString* Cached = OwnerFolders.Find(Owner))
		{
			return *Cached;
		}
		return OwnerFolders.Add(Owner, FamilyFolderForOwner(Families, Owner));
	};

	// An owner's grid sidecar plus the labels that sidecar binds as autolayer TARGETS. A target is
	// composed onto a host rather than selected, so the bake writes it once per declaring host under
	// the derived `Label@Host` name and the bare label is on the mount for nothing
	// (`FElysiumContentPaths::BakedCharacterBlendSpace`). Those labels reach the mount through
	// `ResolveLayerAssets`, so they are outside this contract and
	// belong to `Elysium.Content.UpperBodyLayerArming`.
	struct FOwnerTables
	{
		TSharedPtr<FElysiumBlendTable> Table;
		TSet<FString> LayerTargets;
	};
	TMap<FString, TSharedPtr<FOwnerTables>> Tables;
	auto TableFor = [&Tables](const FString& Owner) -> const FOwnerTables&
	{
		if (const TSharedPtr<FOwnerTables>* Cached = Tables.Find(Owner))
		{
			return **Cached;
		}
		TSharedPtr<FOwnerTables> Entry = MakeShared<FOwnerTables>();
		TSharedPtr<FElysiumBlendTable> Table = MakeShared<FElysiumBlendTable>();
		FString TableError;
		if (Table->Load(FString::Printf(TEXT("blends/%s.json"), *Owner), TableError))
		{
			Entry->Table = Table;
			for (const TPair<FString, FElysiumAutoLayerBinding>& Binding : Table->AutoLayers)
			{
				for (const FString& Target : Binding.Value.Clips)
				{
					Entry->LayerTargets.Add(Target);
				}
			}
		}
		// Most owners declare no multi-cell sequence and ship no sidecar at all.
		Tables.Add(Owner, Entry);
		return *Entry;
	};

	int32 BodiesIndexed = 0;
	int32 BodiesOnMount = 0;
	int32 BodiesWithoutVocabulary = 0;
	int32 LabelsChecked = 0;
	int32 LayersSkipped = 0;
	int32 GridsChecked = 0;
	int32 CellsChecked = 0;
	int32 SpacesMissing = 0;
	int32 MissingAssets = 0;

	for (const FString& Stem : Stems)
	{
		++BodiesIndexed;
		if (!ElysiumNpcVisual::IsStemBaked(Stem))
		{
			// Not on the mount: this body stands nothing, which the runtime reports by name.
			continue;
		}
		++BodiesOnMount;
		FElysiumNpcClipSet Clips;
		FString ClipError;
		if (!Clips.Load(Stem, ClipError))
		{
			++BodiesWithoutVocabulary;
			AddError(FString::Printf(
				TEXT("%s: stands on the mount and has no clip vocabulary (%s), so every label it is "
				     "asked for is unresolved"), *Stem, *ClipError));
			continue;
		}

		// Sorted so a failing run names the same label first every time.
		TArray<FString> Labels;
		Clips.Clips.GetKeys(Labels);
		Labels.Sort([](const FString& A, const FString& B) { return A < B; });

		int32 ReportedForBody = 0;
		int32 MissingForBody = 0;
		auto Report = [&](const FString& Message)
		{
			++MissingForBody;
			++MissingAssets;
			if (ReportedForBody < GMaxErrorsPerBody)
			{
				++ReportedForBody;
				AddError(Message);
			}
		};

		for (const FString& Label : Labels)
		{
			const FElysiumNpcClip& Clip = Clips.Clips[Label];
			// The owner column decides where the clip comes from, exactly as `ResolveClip` reads it.
			const FString Owner = Clip.IsOwnedBy(Stem) ? Stem : Clip.Owner;
			const FOwnerTables& Owned = TableFor(Owner);
			if (Clip.IsAdditive() || Owned.LayerTargets.Contains(Label))
			{
				// Composed, never selected: its asset is the derived `Label@Host` form, one per
				// declaring host, and it is armed through the fallback-free layer path.
				++LayersSkipped;
				continue;
			}
			++LabelsChecked;

			const FString Folder = FolderFor(Owner);
			if (Folder.IsEmpty())
			{
				Report(FString::Printf(
					TEXT("%s: '%s' is owned by '%s', which has no folder on the mount"),
					*Stem, *Label, *Owner));
				continue;
			}

			const FElysiumBlendTable* Table = Owned.Table.Get();
			const FElysiumBlendGrid* Grid = Table != nullptr ? Table->Find(Label) : nullptr;
			if (Grid == nullptr)
			{
				if (!PackageExists(FElysiumContentPaths::BakedCharacterAnim(Folder, Owner, Label)))
				{
					Report(FString::Printf(
						TEXT("%s: '%s' is owned by '%s' and is not on the mount"),
						*Stem, *Label, *Owner));
				}
				continue;
			}

			++GridsChecked;
			if (!PackageExists(
				FElysiumContentPaths::BakedCharacterBlendSpace(Folder, Owner, Label)))
			{
				// Not a failure on its own: the resolver downgrades to the selected cell, which the
				// loop below is what holds to. Counted so the summary can say it happened.
				++SpacesMissing;
			}

			for (const FElysiumBlendCell& Cell : Grid->Cells)
			{
				if (Cell.Clip.IsEmpty())
				{
					// The exporter records a cell whose animation did not bake as a null, and
					// `SelectCell` walks past it rather than playing silence. Nothing is asked of
					// the mount for one.
					continue;
				}
				++CellsChecked;
				if (!PackageExists(
					FElysiumContentPaths::BakedCharacterAnim(Folder, Owner, Cell.Clip)))
				{
					Report(FString::Printf(
						TEXT("%s: grid '%s' on '%s' has cell [%d][%d] = '%s', which is not on the mount"),
						*Stem, *Label, *Owner, Cell.Axis[0], Cell.Axis[1], *Cell.Clip));
				}
			}
		}

		if (MissingForBody > ReportedForBody)
		{
			AddError(FString::Printf(TEXT("%s: %d further clips are not on the mount (%d reported)"),
				*Stem, MissingForBody - ReportedForBody, ReportedForBody));
		}
	}

	// The scene half. `ResolveClipFromBank` is handed a bank stem and a clip name straight from a
	// choreographed scene, with no vocabulary in between, so the loop above cannot reach it -- but
	// the set of banks a scene can name IS enumerable: it is the cinematic sets' roots, one bank per
	// `bonerename` actor. Each of these banks is packaged once in the shared bank namespace.
	TSet<FString> CinematicBanks;
	for (const TPair<FString, FElysiumCinematicSet>& Set : Index.Cinematics)
	{
		for (const TPair<FString, FString>& Root : Set.Value.Roots)
		{
			if (!Root.Value.IsEmpty())
			{
				CinematicBanks.Add(Root.Value);
			}
		}
	}
	TArray<FString> CinematicStems = CinematicBanks.Array();
	CinematicStems.Sort();
	int32 CinematicUnbaked = 0;
	for (const FString& BankStem : CinematicStems)
	{
		if (!FamilyFolderForOwner(Families, BankStem).IsEmpty())
		{
			continue;
		}
		++CinematicUnbaked;
		if (CinematicUnbaked <= GMaxErrorsPerBody)
		{
			AddError(FString::Printf(
				TEXT("cinematic bank '%s' is named by a scene and is not on the mount"),
				*BankStem));
		}
	}
	if (CinematicUnbaked > GMaxErrorsPerBody)
	{
		AddError(FString::Printf(
			TEXT("%d further cinematic banks are not on the mount (%d reported)"),
			CinematicUnbaked - GMaxErrorsPerBody, GMaxErrorsPerBody));
	}
	AddInfo(FString::Printf(TEXT("%d of %d scene-named cinematic banks are on the mount"),
		CinematicStems.Num() - CinematicUnbaked, CinematicStems.Num()));

	AddInfo(FString::Printf(
		TEXT("certified %d of %d indexed bodies over %d family folders: %d labels, %d grids, "
		     "%d grid cells; %d clips missing"),
		BodiesOnMount, BodiesIndexed, Families.Num(), LabelsChecked, GridsChecked, CellsChecked,
		MissingAssets));
	if (LayersSkipped > 0)
	{
		AddInfo(FString::Printf(
			TEXT("%d composed labels (additives and autolayer targets) belong to the fallback-free "
			     "layer path and are covered by Elysium.Content.UpperBodyLayerArming"),
			LayersSkipped));
	}
	if (SpacesMissing > 0)
	{
		AddInfo(FString::Printf(
			TEXT("%d of %d grids stand no UBlendSpace and resolve through their cells instead"),
			SpacesMissing, GridsChecked));
	}
	if (BodiesWithoutVocabulary > 0)
	{
		AddInfo(FString::Printf(TEXT("%d baked bodies carry no clip vocabulary"),
			BodiesWithoutVocabulary));
	}
	if (BodiesOnMount == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no indexed body is on the baked mount -- "
			"run: uv run elysium export characters"));
	}
	else if (MissingAssets == 0 && CinematicUnbaked == 0)
	{
		AddInfo(TEXT("every body and scene checked resolves native clips from shared bank assets"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
