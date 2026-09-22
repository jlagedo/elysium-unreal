#include "Substrate/ElysiumScheduleCorpus.h"

#include "Dom/JsonObject.h"
#include "ElysiumContentPaths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Substrate/ElysiumScheduleText.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSchedules, Log, All);

namespace
{
	constexpr int32 GNumCategories = static_cast<int32>(EElysiumIdCategory::Count);

	/** The sidecar's spelling of each category, under `spaces`. Lower case throughout. */
	const TCHAR* SpaceKey(EElysiumIdCategory Category)
	{
		switch (Category)
		{
		case EElysiumIdCategory::Schedule:  return TEXT("schedule");
		case EElysiumIdCategory::Task:      return TEXT("task");
		case EElysiumIdCategory::Condition: return TEXT("condition");
		case EElysiumIdCategory::SquadSlot: return TEXT("squadslot");
		default:                            return TEXT("?");
		}
	}

	/** The sidecar's spelling under `registrations`, which is NOT the same for squad slots: the
	 *  space key is `squadslot` and the registration key is `squadSlot`. Retail's own
	 *  `CAI_LocalIdSpace::Register` takes the lower-case one as its category argument. */
	const TCHAR* RegistrationKey(EElysiumIdCategory Category)
	{
		return Category == EElysiumIdCategory::SquadSlot ? TEXT("squadSlot") : SpaceKey(Category);
	}

	bool ReadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutRoot, FString& OutError)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			OutError = FString::Printf(TEXT("unreadable: %s"), *Path);
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			OutError = FString::Printf(TEXT("not JSON: %s"), *Path);
			return false;
		}
		return true;
	}
}

namespace
{
	TArray<TFunction<void(FElysiumScheduleCorpus&)>>& PortProgramProviders()
	{
		static TArray<TFunction<void(FElysiumScheduleCorpus&)>> Providers;
		return Providers;
	}
}

const TCHAR* ElysiumIdCategoryName(EElysiumIdCategory Category)
{
	return SpaceKey(Category);
}

FElysiumScheduleCorpus& FElysiumScheduleCorpus::Get()
{
	static FElysiumScheduleCorpus Corpus;
	return Corpus;
}

void FElysiumScheduleCorpus::Reset()
{
	bLoaded = false;
	LoadError.Reset();
	for (FElysiumIdNamespace& Space : Namespaces)
	{
		Space.Reset();
	}
	Programs.Reset();
	Ops.Reset();
	Measured = FElysiumScheduleCensus();
	ActivityNames.Reset();
	ModelNames.Reset();
	SoundNames.Reset();
	Order.Reset();
	ByKey.Reset();
	ByClass.Reset();
}

bool FElysiumScheduleCorpus::EnsureLoaded()
{
	if (bLoaded)
	{
		return LoadError.IsEmpty();
	}

	// Set FIRST, so a failure is remembered rather than retried on every touch. A corpus that is
	// not there is a deployment problem, and asking the disk again per NPC would turn it into a
	// frame-rate problem as well.
	bLoaded = true;

	const FString Directory =
		FElysiumContentPaths::CorpusRoot() / TEXT("ai") / TEXT("schedules");
	FString Error;
	if (!LoadFrom(Directory, Error))
	{
		LoadError = Error;
		UE_LOG(LogElysiumSchedules, Error,
			TEXT("the schedule corpus did not load: %s -- run `uv run elysium import ai-schedules`"),
			*Error);
		return false;
	}
	UE_LOG(LogElysiumSchedules, Log, TEXT("%s"), *DescribeCensus());

	// The constants check belongs HERE and not in `LoadFrom`, because it is a claim about the
	// DEPLOYED corpus. A disagreement is a bug in this runtime's constants rather than in the
	// corpus, so it is loud and it does not refuse the load: the game still runs on retail's own
	// data, and the log carries both numbers. A test that loads a scratch corpus asks for the
	// check itself, on the rows its tree actually carries.
	TArray<FString> NumberErrors;
	if (!VerifyNumbers(NumberErrors))
	{
		for (const FString& Row : NumberErrors)
		{
			UE_LOG(LogElysiumSchedules, Error, TEXT("ElysiumScheduleNumbers: %s"), *Row);
		}
	}
	return true;
}

bool FElysiumScheduleCorpus::LoadFrom(const FString& Directory, FString& OutError)
{
	Reset();
	bLoaded = true;

	// --- the vocabulary: the two squad slots, seeded before any space takes a global base --------
	//
	// `0x10316e80` puts `SQUAD_SLOT_ATTACK1` and `SQUAD_SLOT_ATTACK2` straight into the namespace
	// during the base's precache, before a single space initialises. No class registers a local
	// squad-slot name, so these two are that namespace's whole contents -- and seeding them first
	// is what makes the first space's global base 1,000,000,002 rather than 1,000,000,000.
	{
		TSharedPtr<FJsonObject> Vocabulary;
		FString Error;
		if (!ReadJsonFile(Directory / TEXT("vocabulary.json"), Vocabulary, Error))
		{
			OutError = Error;
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
		if (Vocabulary->TryGetArrayField(TEXT("squadSlots"), Slots))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Slots)
			{
				const TSharedPtr<FJsonObject>* Row = nullptr;
				if (!Value->TryGetObject(Row))
				{
					continue;
				}
				FString Name;
				double GlobalId = 0.0;
				if ((*Row)->TryGetStringField(TEXT("name"), Name)
					&& (*Row)->TryGetNumberField(TEXT("globalId"), GlobalId))
				{
					Namespaces[static_cast<int32>(EElysiumIdCategory::SquadSlot)]
						.Insert(Name, static_cast<int32>(GlobalId));
				}
			}
		}
	}

	if (!ReadUnits(Directory, OutError))
	{
		return false;
	}

	TArray<FElysiumScheduleSpaceUnit*> Ordered;
	OrderUnits(Ordered);
	for (FElysiumScheduleSpaceUnit* Unit : Ordered)
	{
		LoadUnit(Directory, *Unit);
	}

	Ops.Build(Namespace(EElysiumIdCategory::Task));

	// The port's own bodies for registered names the corpus has no text for. After the texts,
	// because they name their steps out of the task namespace; before the census, because their
	// steps are steps.
	for (const TFunction<void(FElysiumScheduleCorpus&)>& Provider : PortProgramProviders())
	{
		Provider(*this);
	}

	Measured.PortedTasks = Ops.NumPorted();
	Measured.UnportedTasks = Ops.NumUnported();
	Measured.UnportedSteps = Ops.Measure(Programs);

	Measured.Programs = Programs.Num();
	Measured.ScheduleNames = Namespace(EElysiumIdCategory::Schedule).Num();
	Measured.TaskNames = Namespace(EElysiumIdCategory::Task).Num();
	Measured.ConditionNames = Namespace(EElysiumIdCategory::Condition).Num();
	Measured.SquadSlotNames = Namespace(EElysiumIdCategory::SquadSlot).Num();
	Measured.InternedActivities = ActivityNames.InternedCount();
	Measured.InternedModels = ModelNames.InternedCount();
	Measured.InternedSounds = SoundNames.InternedCount();
	for (const FElysiumScheduleProgram& Program : Programs.Programs())
	{
		Measured.Steps += Program.Tasks.Num();
	}

	OutError.Reset();
	return true;
}

bool FElysiumScheduleCorpus::ReadUnits(const FString& Directory, FString& OutError)
{
	TArray<FString> Directories;
	IFileManager::Get().FindFiles(Directories, *(Directory / TEXT("*")), false, true);
	Directories.Sort();

	for (const FString& Leaf : Directories)
	{
		const FString SidecarPath = Directory / Leaf / TEXT("space.json");
		if (!IFileManager::Get().FileExists(*SidecarPath))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Root;
		FString Error;
		if (!ReadJsonFile(SidecarPath, Root, Error))
		{
			// One unreadable sidecar must not cost the other fifty-five their programs; it costs
			// its own class them, which is what retail's first-failure rule costs too.
			UE_LOG(LogElysiumSchedules, Error, TEXT("%s"), *Error);
			continue;
		}

		TUniquePtr<FElysiumScheduleSpaceUnit> Unit = MakeUnique<FElysiumScheduleSpaceUnit>();
		Unit->Key = Leaf;
		Root->TryGetStringField(TEXT("className"), Unit->ClassName);

		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (Root->TryGetArrayField(TEXT("classNames"), Names))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Names)
			{
				FString Name;
				if (Value->TryGetString(Name))
				{
					Unit->ClassNames.Add(Name);
				}
			}
		}
		if (Unit->ClassNames.IsEmpty() && !Unit->ClassName.IsEmpty())
		{
			Unit->ClassNames.Add(Unit->ClassName);
		}

		const TSharedPtr<FJsonObject>* Spaces = nullptr;
		if (Root->TryGetObjectField(TEXT("spaces"), Spaces))
		{
			for (int32 Index = 0; Index < GNumCategories; ++Index)
			{
				const TSharedPtr<FJsonObject>* Space = nullptr;
				if (!(*Spaces)->TryGetObjectField(
						SpaceKey(static_cast<EElysiumIdCategory>(Index)), Space))
				{
					continue;
				}
				// An absent or null `parentUnit` is a root for loading purposes. For a schedule
				// space that means the base; for a squad-slot space it means the standalone root
				// nothing registers into, which the export proves is the only one.
				FString ParentUnit;
				(*Space)->TryGetStringField(TEXT("parentUnit"), ParentUnit);
				Unit->ParentKey[Index] = ParentUnit;
			}
		}

		FElysiumScheduleSpaceUnit* Raw = Unit.Get();
		Order.Add(MoveTemp(Unit));
		ByKey.Add(Raw->Key, Raw);
		for (const FString& Name : Raw->ClassNames)
		{
			ByClass.Add(Name.ToLower(), Raw);
		}
	}

	if (Order.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no space.json under %s"), *Directory);
		return false;
	}
	OutError.Reset();
	return true;
}

void FElysiumScheduleCorpus::OrderUnits(TArray<FElysiumScheduleSpaceUnit*>& OutOrdered)
{
	// Parent before child, on the SCHEDULE parent -- the graph the sidecar's `parentUnit` spells.
	// The other three categories parent inside the same unit pair, so ordering on one orders all.
	TSet<FString> Placed;
	TArray<FElysiumScheduleSpaceUnit*> Pending;
	Pending.Reserve(Order.Num());
	for (const TUniquePtr<FElysiumScheduleSpaceUnit>& Unit : Order)
	{
		Pending.Add(Unit.Get());
	}

	while (!Pending.IsEmpty())
	{
		bool bProgressed = false;
		for (int32 Index = 0; Index < Pending.Num();)
		{
			FElysiumScheduleSpaceUnit* Unit = Pending[Index];
			const FString& Parent = Unit->ParentKey[static_cast<int32>(EElysiumIdCategory::Schedule)];
			if (Parent.IsEmpty() || Placed.Contains(Parent))
			{
				OutOrdered.Add(Unit);
				Placed.Add(Unit->Key);
				Pending.RemoveAt(Index);
				bProgressed = true;
				continue;
			}
			++Index;
		}
		if (!bProgressed)
		{
			// Nothing moved, so what is left is a cycle or descends from a parent that is not
			// there. Either is a corpus defect: skip them and load the rest, because a child
			// loaded before its parent would take a range the parent then cannot have -- silent
			// wrong ids rather than a missing class.
			for (FElysiumScheduleSpaceUnit* Unit : Pending)
			{
				Unit->bSkipped = true;
				++Measured.UnitsSkipped;
				UE_LOG(LogElysiumSchedules, Error,
					TEXT("space `%s` parents on `%s`, which is missing or in a cycle; skipped"),
					*Unit->Key,
					*Unit->ParentKey[static_cast<int32>(EElysiumIdCategory::Schedule)]);
			}
			break;
		}
	}
}

void FElysiumScheduleCorpus::LoadUnit(const FString& Directory, FElysiumScheduleSpaceUnit& Unit)
{
	const FString SidecarPath = Directory / Unit.Key / TEXT("space.json");
	TSharedPtr<FJsonObject> Root;
	FString Error;
	if (!ReadJsonFile(SidecarPath, Root, Error))
	{
		Unit.bSkipped = true;
		++Measured.UnitsSkipped;
		return;
	}

	// --- Init, all four, before a single name is registered --------------------------------------
	const TSharedPtr<FJsonObject>* Spaces = nullptr;
	Root->TryGetObjectField(TEXT("spaces"), Spaces);
	for (int32 Index = 0; Index < GNumCategories; ++Index)
	{
		const EElysiumIdCategory Category = static_cast<EElysiumIdCategory>(Index);
		const TSharedPtr<FJsonObject>* Space = nullptr;
		if (Spaces == nullptr || !(*Spaces)->TryGetObjectField(SpaceKey(Category), Space))
		{
			// Both roots declare three spaces, not four: neither initialises a squad-slot space,
			// and a class that has one parents it on the standalone root instead.
			continue;
		}
		const FElysiumLocalIdSpace* Parent = nullptr;
		if (!Unit.ParentKey[Index].IsEmpty())
		{
			if (FElysiumScheduleSpaceUnit** Found = ByKey.Find(Unit.ParentKey[Index]))
			{
				Parent = &(*Found)->Spaces[Index];
			}
		}
		Unit.Spaces[Index].Init(Namespaces[Index], Parent);
	}

	// --- every name, in retail's order: schedules, tasks, conditions, squad slots -----------------
	//
	// ALL of them before the first text is parsed, which is what makes a forward reference inside
	// one class resolve.
	const TSharedPtr<FJsonObject>* Registrations = nullptr;
	if (Root->TryGetObjectField(TEXT("registrations"), Registrations))
	{
		for (int32 Index = 0; Index < GNumCategories; ++Index)
		{
			const EElysiumIdCategory Category = static_cast<EElysiumIdCategory>(Index);
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (!(*Registrations)->TryGetArrayField(RegistrationKey(Category), Rows))
			{
				continue;
			}
			TArray<TPair<int32, FString>> Pairs;
			Pairs.Reserve(Rows->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Rows)
			{
				const TSharedPtr<FJsonObject>* Row = nullptr;
				if (!Value->TryGetObject(Row))
				{
					continue;
				}
				FString Name;
				double LocalId = 0.0;
				if (!(*Row)->TryGetStringField(TEXT("name"), Name)
					|| !(*Row)->TryGetNumberField(TEXT("localId"), LocalId))
				{
					continue;
				}
				Pairs.Emplace(static_cast<int32>(LocalId), MoveTemp(Name));
			}

			// **Sorted, and this is retail's own step, not tidiness.** An init body builds its
			// `(name, id)` pair vectors in whatever order the source lists them, SORTS them, and
			// only then loops over `Register`. The sidecar carries the append order, because that
			// is what the image says; the sort is what makes the FIRST registration the lowest id,
			// and `CAI_LocalIdSpace` needs exactly that -- a space takes its local base from its
			// first registration, and every later id below that base is refused.
			//
			// `CNPC_VZombie` is the proof: it appends `0x161` before `0x15e`, so registering in
			// append order loses three ids and then refuses its own first text for naming a
			// schedule that never registered.
			Pairs.Sort([](const TPair<int32, FString>& A, const TPair<int32, FString>& B)
			{
				return A.Key < B.Key;
			});
			for (const TPair<int32, FString>& Pair : Pairs)
			{
				Unit.Spaces[Index].Register(
					Pair.Value, Pair.Key, SpaceKey(Category), *Unit.ClassName);
			}
		}
	}

	// --- the texts, in the order the owner's body fed them ----------------------------------------
	const TArray<TSharedPtr<FJsonValue>>* Texts = nullptr;
	if (!Root->TryGetArrayField(TEXT("texts"), Texts))
	{
		return;
	}

	struct FTextRow
	{
		int32 Order = 0;
		FString File;
	};
	TArray<FTextRow> Rows;
	for (const TSharedPtr<FJsonValue>& Value : *Texts)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		if (!Value->TryGetObject(Row))
		{
			continue;
		}
		FTextRow Entry;
		double OrderValue = 0.0;
		(*Row)->TryGetNumberField(TEXT("order"), OrderValue);
		Entry.Order = static_cast<int32>(OrderValue);
		if ((*Row)->TryGetStringField(TEXT("file"), Entry.File))
		{
			Rows.Add(MoveTemp(Entry));
		}
	}
	Rows.Sort([](const FTextRow& A, const FTextRow& B) { return A.Order < B.Order; });
	Unit.NumTexts = Rows.Num();
	Measured.Texts += Rows.Num();
	++Measured.Units;

	FElysiumScheduleParseContext Context;
	Context.ClassName = Unit.ClassName;
	Context.ScheduleSpace = &Unit.Spaces[static_cast<int32>(EElysiumIdCategory::Schedule)];
	Context.TaskSpace = &Unit.Spaces[static_cast<int32>(EElysiumIdCategory::Task)];
	Context.Conditions = &Namespaces[static_cast<int32>(EElysiumIdCategory::Condition)];
	Context.Activities = &ActivityNames;
	Context.Models = &ModelNames;
	Context.Sounds = &SoundNames;

	for (const FTextRow& Row : Rows)
	{
		FString Body;
		if (!FFileHelper::LoadFileToString(Body, *(Directory / Unit.Key / Row.File)))
		{
			Unit.bAllTextsLoaded = false;
			Unit.FailureFile = Row.File;
			Unit.FailureRow = TEXT("unreadable");
			Unit.FailureMessage = TEXT("the text file is missing from the deployed corpus");
			++Measured.ParseFailures;
			break;
		}

		FElysiumScheduleParseResult Result = ElysiumScheduleText::Parse(Body, Context);
		Measured.SkippedConditions += Result.SkippedConditions;
		Measured.UnknownFlags += Result.UnknownFlags;

		const bool bFailed = Result.Failed();
		if (bFailed)
		{
			Unit.bAllTextsLoaded = false;
			Unit.FailureFile = Row.File;
			Unit.FailureRow = ElysiumScheduleParseFailureName(Result.Failure);
			Unit.FailureMessage = Result.Message;
			++Measured.ParseFailures;
		}

		// The programs land either way: retail links a schedule node into the manager before
		// parsing its tasks and no error path unlinks it, so a failed text leaves its record behind.
		Programs.AddAll(MoveTemp(Result));
		if (bFailed)
		{
			UE_LOG(LogElysiumSchedules, Error,
				TEXT("%s/%s refused (%s): %s -- the rest of this class's texts are not loaded"),
				*Unit.Key, *Row.File, *Unit.FailureRow, *Unit.FailureMessage);
			break;   // retail's own rule: the owner stops at the first failure
		}
		++Unit.NumParsed;
	}
}

void FElysiumScheduleCorpus::AddPortProgramProvider(TFunction<void(FElysiumScheduleCorpus&)> Provider)
{
	PortProgramProviders().Add(MoveTemp(Provider));
}

int32 FElysiumScheduleCorpus::AddPortProgram(FElysiumScheduleProgram&& Program)
{
	EnsureLoaded();
	const int32 GlobalId =
		Namespace(EElysiumIdCategory::Schedule).Find(Program.Name);
	if (GlobalId == INDEX_NONE)
	{
		// Verbose, not an Error: a scratch corpus in a test legitimately registers none of these
		// names, and the provider is run on every load. A real miss is visible at the consumer --
		// `ElysiumAiScriptedSchedule::ProgramFor` answers `None` and the director pushes nothing.
		UE_LOG(LogElysiumSchedules, Verbose,
			TEXT("Elysium: no class registers the schedule name '%s', so the port body that would "
				"supply it is not loaded"), *Program.Name);
		return ElysiumScheduleId::None;
	}
	Program.GlobalId = GlobalId;
	Programs.Add(MoveTemp(Program));
	return GlobalId;
}

const FElysiumScheduleSpaceUnit* FElysiumScheduleCorpus::Unit(const FString& Key) const
{
	const FElysiumScheduleSpaceUnit* const* Found = ByKey.Find(Key);
	return Found != nullptr ? *Found : nullptr;
}

const FElysiumScheduleSpaceUnit* FElysiumScheduleCorpus::UnitForClass(const FString& ClassName) const
{
	const FElysiumScheduleSpaceUnit* const* Found = ByClass.Find(ClassName.ToLower());
	return Found != nullptr ? *Found : nullptr;
}

const FElysiumLocalIdSpace* FElysiumScheduleCorpus::SpaceFor(
	const FString& ClassName, EElysiumIdCategory Category) const
{
	const FElysiumScheduleSpaceUnit* Found = UnitForClass(ClassName);
	return Found != nullptr ? &Found->Spaces[static_cast<int32>(Category)] : nullptr;
}

bool FElysiumScheduleCorpus::VerifyNumbers(TArray<FString>& OutErrors) const
{
	const int32 Before = OutErrors.Num();
	for (const FElysiumScheduleNumberRow& Row : ElysiumSched::CheckedRows())
	{
		const FElysiumScheduleSpaceUnit* Found = Unit(Row.Unit);
		if (Found == nullptr)
		{
			OutErrors.Add(FString::Printf(
				TEXT("`%s` names unit `%s`, which the corpus does not carry"), Row.Name, Row.Unit));
			continue;
		}
		const FElysiumLocalIdSpace& Space = Found->Space(EElysiumIdCategory::Schedule);
		const int32 GlobalId =
			Space.Namespace != nullptr ? Space.Namespace->Find(Row.Name) : INDEX_NONE;
		if (GlobalId == INDEX_NONE)
		{
			OutErrors.Add(FString::Printf(
				TEXT("`%s` is not registered in `%s`"), Row.Name, Row.Unit));
			continue;
		}
		const int32 LocalId = Space.GlobalToLocal(GlobalId);
		if (LocalId != Row.LocalId)
		{
			OutErrors.Add(FString::Printf(
				TEXT("`%s` in `%s` is local 0x%x here and 0x%x in the corpus"),
				Row.Name, Row.Unit, Row.LocalId, LocalId));
		}
	}
	return OutErrors.Num() == Before;
}

FString FElysiumScheduleCorpus::DescribeCensus() const
{
	const int32 Identities = Measured.PortedTasks + Measured.UnportedTasks;
	const float PortedShare =
		Identities > 0 ? (100.0f * Measured.PortedTasks) / Identities : 0.0f;
	return FString::Printf(
		TEXT("schedules %d in %d spaces (%d skipped), %d parse failure(s); ")
		TEXT("tasks %d steps over %d identities, %d ported (%.0f%%), %d unported reached by %d step(s)"),
		Measured.Programs, Measured.Units, Measured.UnitsSkipped, Measured.ParseFailures,
		Measured.Steps, Identities, Measured.PortedTasks, PortedShare,
		Measured.UnportedTasks, Measured.UnportedSteps);
}

#if WITH_DEV_AUTOMATION_TESTS
FElysiumScheduleProgram* FElysiumScheduleCorpus::MutableProgram(int32 GlobalId)
{
	EnsureLoaded();
	return const_cast<FElysiumScheduleProgram*>(Programs.FindById(GlobalId));
}
#endif
