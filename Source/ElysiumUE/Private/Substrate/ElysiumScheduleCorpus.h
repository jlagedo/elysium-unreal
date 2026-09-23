// The schedule corpus: retail's 691 texts, loaded and compiled at run time.
//
// This is the recipe the 56 init bodies perform, done once instead of 56 times. Per space: seed the
// four global namespaces, `Init` the four `CAI_LocalIdSpace`s against the parent space, register
// EVERY name (schedules, then tasks, then conditions, then squad slots -- the order that makes
// forward references inside a class resolve), then parse that space's texts in the order its body
// fed them, stopping at the first failure exactly as retail's owner does.
//
// **Load order is the corpus's `parentUnit` graph, topologically.** It is NOT the C++ class tree,
// and the two genuinely differ: `CNPC_VTzimisce`'s census base is `CNPC_VBaseBoss` but its space
// parents on Troika. A class's local ids are only meaningful relative to the space it was
// registered in, so a child loaded before its parent takes a range the parent then cannot have.
//
// **Stated divergence.** Retail loads the base eagerly from `CWorld::Precache`, Troika behind a
// once-guard, and each species on first touch in an order the oracle records as unrecovered. This
// runtime loads everything at the first touch of anything, in one sorted topological order. Nothing
// observable changes -- no space ever searches a sibling, so no id depends on when a sibling loaded
// -- and the port gains reproducible global ids, which the save CRC, the census and every test
// stand on.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Substrate/ElysiumIdNamespace.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumScheduleManager.h"
#include "Substrate/ElysiumSymbolRegistry.h"
#include "Templates/Function.h"
#include "Substrate/ElysiumTaskOps.h"
#include "Templates/UniquePtr.h"

/** The four id categories, in the order a space initialises and registers them. */
enum class EElysiumIdCategory : uint8
{
	Schedule = 0,
	Task = 1,
	Condition = 2,
	SquadSlot = 3,
	Count = 4,
};

const TCHAR* ElysiumIdCategoryName(EElysiumIdCategory Category);

/** One deployed `<space>/space.json`, and what loading it produced. */
struct FElysiumScheduleSpaceUnit
{
	/** The directory name, which is the unit key the graph is spelled in: `cnpc_vbrujah`. */
	FString Key;

	/** The class whose init body this unit is. */
	FString ClassName;

	/** Every class that runs this space. 77 classes share 58 spaces: a class with no init body of
	 *  its own inherits the slot-580 getter of the class above it, so `CNPC_VRat` runs
	 *  `CNPC_VScurrying`'s vocabulary and `CPayphone` runs Troika's. */
	TArray<FString> ClassNames;

	/** The unit this one's spaces parent on, per category. Empty at a root, and empty for a
	 *  squad-slot space (every one of those parents on a root space nothing registers into). */
	FString ParentKey[static_cast<int32>(EElysiumIdCategory::Count)];

	/** The four spaces. A unit that declares only three (both roots do -- neither initialises a
	 *  squad-slot space) leaves the fourth uninitialised, and `IsEmpty` says so. */
	FElysiumLocalIdSpace Spaces[static_cast<int32>(EElysiumIdCategory::Count)];

	/** How many texts the sidecar listed, and how many parsed before one failed. */
	int32 NumTexts = 0;
	int32 NumParsed = 0;

	/** Retail's real slot-452 answer for this class: false once a text failed. */
	bool bAllTextsLoaded = true;

	/** The failure that stopped this space, if one did. `File` is the `.sch` the log names. */
	FString FailureFile;
	FString FailureRow;
	FString FailureMessage;

	/** Skipped because its parent was missing or the graph had a cycle. */
	bool bSkipped = false;

	const FElysiumLocalIdSpace& Space(EElysiumIdCategory Category) const
	{
		return Spaces[static_cast<int32>(Category)];
	}
};

/** What one load measured. Every field is a count the coverage meter or a test reads. */
struct FElysiumScheduleCensus
{
	int32 Units = 0;
	int32 UnitsSkipped = 0;
	int32 Texts = 0;
	int32 Programs = 0;
	int32 ParseFailures = 0;
	int32 Steps = 0;

	int32 ScheduleNames = 0;
	int32 TaskNames = 0;
	int32 ConditionNames = 0;
	int32 SquadSlotNames = 0;

	/** Task identities the corpus names, split by whether this runtime has a body for one. */
	int32 PortedTasks = 0;
	int32 UnportedTasks = 0;
	/** Steps that name an identity with no body -- the size of the hole, not its count. */
	int32 UnportedSteps = 0;

	/** Interrupt names no class registered. Retail `DevMsg`s each and drops the bit. */
	int32 SkippedConditions = 0;
	/** `Flags` words that read 0, an authored `NONE` included. */
	int32 UnknownFlags = 0;

	int32 InternedActivities = 0;
	int32 InternedModels = 0;
	int32 InternedSounds = 0;
};

/**
 * The one corpus.
 *
 * Nothing here is a `UObject` and nothing ticks: it is a table built once and read for the rest of
 * the session.
 */
class FElysiumScheduleCorpus
{
public:
	/** The session's corpus. */
	static FElysiumScheduleCorpus& Get();

	/** Load from the deployed corpus, once. False when the load failed; `Error()` says why.
	 *
	 *  The loaded flag is set BEFORE the work, so a failure is remembered rather than retried on
	 *  every touch -- the same rule `ElysiumRulebookSubsystem` follows. */
	bool EnsureLoaded();

	/** Load from an explicit `ai/schedules` directory. This is the real body; `EnsureLoaded` is it
	 *  pointed at `FElysiumContentPaths::CorpusRoot()`. Tests hand it a scratch tree. */
	bool LoadFrom(const FString& Directory, FString& OutError);

	void Reset();

	bool IsLoaded() const { return bLoaded; }
	const FString& Error() const { return LoadError; }
	const FElysiumScheduleCensus& Census() const { return Measured; }

	const FElysiumScheduleManager& Manager() const { return Programs; }
	const FElysiumTaskOpTable& TaskOps() const { return Ops; }

	/** The four global namespaces. Every id in this runtime is a number out of one of them. */
	const FElysiumIdNamespace& Namespace(EElysiumIdCategory Category) const
	{
		return Namespaces[static_cast<int32>(Category)];
	}

	/** The unit a key names, or null. */
	const FElysiumScheduleSpaceUnit* Unit(const FString& Key) const;

	/** The space a retail CLASS runs, by the class's own name. This is slot 580, answered from the
	 *  corpus rather than from a table typed here -- and it is how a spawned NPC finds its
	 *  programs. Case-insensitive; null for a class the corpus does not place. */
	const FElysiumLocalIdSpace* SpaceFor(const FString& ClassName, EElysiumIdCategory Category) const;

	/** The unit that owns a class's spaces, or null. */
	const FElysiumScheduleSpaceUnit* UnitForClass(const FString& ClassName) const;

	/** The units, in the order they were loaded. */
	const TArray<TUniquePtr<FElysiumScheduleSpaceUnit>>& Units() const { return Order; }

	/** Every constant in `ElysiumScheduleNumbers.h`, against the corpus's own registration table.
	 *
	 *  Appends one line per disagreement to `OutErrors` naming the unit, the name and BOTH numbers.
	 *  Answers false if any row disagreed. Called at the end of a load; a caller that wants the
	 *  detail calls it again. */
	bool VerifyNumbers(TArray<FString>& OutErrors) const;

	/** The interning registries the three run-time operand tables stand in for. Public because the
	 *  `TASK_SET_ACTIVITY` arm maps an activity id back to its name. */
	const FElysiumSymbolRegistry& Activities() const { return ActivityNames; }
	const FElysiumSymbolRegistry& Models() const { return ModelNames; }
	const FElysiumSymbolRegistry& Sounds() const { return SoundNames; }

	/** One line for a log or `elysium.schedules`. */
	FString DescribeCensus() const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only: the loaded program a global id names, mutable.
	 *
	 *  It exists for the two borrow scopes in `ElysiumSchedule.h` and for nothing else. A compiled
	 *  program is authored data; writing one at runtime outside a test is a behavioural change. */
	FElysiumScheduleProgram* MutableProgram(int32 GlobalId);
#endif

private:
	/** Read every `space.json` under `Directory` into unloaded units. */
	bool ReadUnits(const FString& Directory, FString& OutError);

	/** Parent before child; a cycle or a missing parent skips that unit and its descendants. */
	void OrderUnits(TArray<FElysiumScheduleSpaceUnit*>& OutOrdered);

	/** `Init`, register, parse -- one unit, in retail's own order. */
	void LoadUnit(const FString& Directory, FElysiumScheduleSpaceUnit& Unit);

	bool bLoaded = false;
	FString LoadError;

	FElysiumIdNamespace Namespaces[static_cast<int32>(EElysiumIdCategory::Count)];
	FElysiumScheduleManager Programs;
	FElysiumTaskOpTable Ops;
	FElysiumScheduleCensus Measured;

	FElysiumSymbolRegistry ActivityNames{ TEXT("DAT_1090fbe0") };
	FElysiumSymbolRegistry ModelNames{ TEXT("DAT_10936b74") };
	FElysiumSymbolRegistry SoundNames{ TEXT("DAT_1073dc3c") };

	/** Owned by pointer so a space's address is stable: a child's `Init` keeps a pointer to its
	 *  parent's space, and an array of values would move them out from under it. */
	TArray<TUniquePtr<FElysiumScheduleSpaceUnit>> Order;
	TMap<FString, FElysiumScheduleSpaceUnit*> ByKey;

	/** Folded class name -> the unit that owns its spaces. */
	TMap<FString, FElysiumScheduleSpaceUnit*> ByClass;
};
