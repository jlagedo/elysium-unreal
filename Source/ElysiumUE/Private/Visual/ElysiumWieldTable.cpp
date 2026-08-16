#include "ElysiumWieldTable.h"

#include "ElysiumContentPaths.h"

#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumWield, Log, All);

// The rows are a pure data holder the bake fills. What lives here is the one door the runtime
// resolves through, so the table is loaded once and a miss is reported once.

const UElysiumWieldTable* UElysiumWieldTable::Load()
{
	// Rooted rather than re-resolved, the same shape `ElysiumEntityBodies::BodyGraphClass` uses: the
	// table outlives any one map epoch, and nothing else holds a reference that would keep it alive
	// between two of them.
	static bool bResolved = false;
	static UElysiumWieldTable* Cached = nullptr;
	if (!bResolved)
	{
		bResolved = true;
		const FString Path = FElysiumContentPaths::BakedWieldTable();
		Cached = LoadObject<UElysiumWieldTable>(nullptr, *Path);
		if (Cached != nullptr)
		{
			Cached->AddToRoot();
		}
		else
		{
			UE_LOG(LogElysiumWield, Warning,
				TEXT("wield table '%s' is not on the mount -- no character can draw a weapon with "
				     "geometry. Run `uv run elysium export wield`."),
				*Path);
		}
	}
	return Cached;
}

EElysiumWieldResult UElysiumWieldTable::FindRow(FName Classname, bool bFemale,
	const FElysiumWieldModelRef*& OutRef)
{
	OutRef = nullptr;

	const UElysiumWieldTable* const Table = Load();
	if (Table == nullptr)
	{
		return EElysiumWieldResult::NoTable;
	}

	// The bake case-folds every key to lower because VtMB compares classnames case-insensitively and
	// the manifest is written from authored text of mixed case, so the lookup folds too rather than
	// requiring its callers to.
	const FName Key(*Classname.ToString().ToLower());
	const FElysiumWieldRow* const Row = Table->Rows.Find(Key);
	if (Row == nullptr)
	{
		return EElysiumWieldResult::UnknownItem;
	}

	if (!Row->bShowsWieldModel)
	{
		return EElysiumWieldResult::WorldModel;
	}

	const FElysiumWieldModelRef& Ref = bFemale ? Row->Female : Row->Male;
	// A null mesh and a `None` binding are the same authored answer reached two ways, and the bake
	// writes them together; either one alone would be a table the bake did not produce.
	if (Ref.Binding == EElysiumWieldBinding::None || Ref.Mesh.IsNull())
	{
		return EElysiumWieldResult::NoGeometry;
	}

	OutRef = &Ref;
	return EElysiumWieldResult::Found;
}
