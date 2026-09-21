#include "ElysiumClassRegistry.h"

#include "Substrate/ElysiumNpcKernelBindings.h"

#include "ElysiumEntityDefs.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumClass, Log, All);

// The variant marshalling category as a name (EElysiumVariantType is a plain enum, not a
// UENUM — the substrate carries no reflection, so there is no StaticEnum to query).
static const TCHAR* ElysiumVariantTypeName(EElysiumVariantType T)
{
	switch (T)
	{
	case EElysiumVariantType::Void:   return TEXT("Void");
	case EElysiumVariantType::Bool:   return TEXT("Bool");
	case EElysiumVariantType::Int:    return TEXT("Int");
	case EElysiumVariantType::Float:  return TEXT("Float");
	case EElysiumVariantType::String: return TEXT("String");
	case EElysiumVariantType::Vector: return TEXT("Vector");
	case EElysiumVariantType::Handle: return TEXT("Handle");
	default:                          return TEXT("?");
	}
}

// --- Registry ---------------------------------------------------------------------------

// The base factory, defined with the base class below and used by RegisterStub above it.
static TUniquePtr<FElysiumEntity> MakeBaseEntity();

FElysiumClassRegistry& FElysiumClassRegistry::Get()
{
	// Function-local static: constructed on first use, so registrar statics in any TU can
	// register during their own static init without a fixed-order dependency.
	static FElysiumClassRegistry Instance;
	return Instance;
}

FElysiumClassDesc& FElysiumClassRegistry::Register(FName ClassName, FName BaseName, FElysiumEntityFactory Factory)
{
	TUniquePtr<FElysiumClassDesc>& Slot = Classes.Add(ClassName, MakeUnique<FElysiumClassDesc>());
	FElysiumClassDesc& Desc = *Slot;
	Desc.ClassName = ClassName;
	Desc.BaseName = BaseName;
	Desc.Factory = Factory;
	return Desc;
}

FElysiumClassDesc* FElysiumClassRegistry::RegisterStub(FName ClassName, FName BaseName)
{
	if (Classes.Contains(ClassName))
	{
		return nullptr;   // an implementation got here first; leave it alone
	}
	FElysiumClassDesc& Desc = Register(ClassName, BaseName, &MakeBaseEntity);
	Desc.bStub = true;
	return &Desc;
}

const FElysiumClassDesc* FElysiumClassRegistry::Find(FName ClassName) const
{
	const TUniquePtr<FElysiumClassDesc>* Slot = Classes.Find(ClassName);
	return Slot ? Slot->Get() : nullptr;
}

const FElysiumClassDesc* FElysiumClassRegistry::BaseDesc() const
{
	return Find(ElysiumBaseClassName());
}

FElysiumInputThunk FElysiumClassRegistry::FindInput(const FElysiumClassDesc& Desc, FName Input) const
{
	for (const FElysiumClassDesc* D = &Desc; D != nullptr; D = D->BaseName.IsNone() ? nullptr : Find(D->BaseName))
	{
		if (const FElysiumInputThunk* Thunk = D->Inputs.Find(Input))
		{
			return *Thunk;
		}
	}
	return nullptr;
}

const FElysiumFieldAccessor* FElysiumClassRegistry::FindField(const FElysiumClassDesc& Desc, FName Field) const
{
	for (const FElysiumClassDesc* D = &Desc; D != nullptr; D = D->BaseName.IsNone() ? nullptr : Find(D->BaseName))
	{
		if (const FElysiumFieldAccessor* Acc = D->Fields.Find(Field))
		{
			return Acc;
		}
	}
	return nullptr;
}

TArray<FName> FElysiumClassRegistry::SaveFields(const FElysiumClassDesc& Desc) const
{
	TArray<FName> Names;
	TSet<FName> Seen;
	// Derived first, so a shadowed base row is skipped by the Seen set rather than by ordering luck.
	for (const FElysiumClassDesc* D = &Desc; D != nullptr; D = D->BaseName.IsNone() ? nullptr : Find(D->BaseName))
	{
		for (const TPair<FName, FElysiumFieldAccessor>& F : D->Fields)
		{
			if (F.Value.bSave && F.Value.Get && F.Value.Set && !Seen.Contains(F.Key))
			{
				Seen.Add(F.Key);
				Names.Add(F.Key);
			}
		}
	}
	Names.Sort(FNameLexicalLess());
	return Names;
}

TUniquePtr<FElysiumEntity> FElysiumClassRegistry::Create(const FElysiumEntityDef& Def, FElysiumEntityHandle Handle) const
{
	const FElysiumClassDesc* Desc = Find(FName(*Def.Classname));
	bool bRecord = false;
	if (Desc == nullptr)
	{
		Desc = BaseDesc();   // unregistered classname -> inert record on the base class
		bRecord = true;
	}
	check(Desc != nullptr);   // the base always registers

	TUniquePtr<FElysiumEntity> Ent = Desc->Factory ? Desc->Factory() : MakeUnique<FElysiumEntity>();
	// A stub descriptor names inputs but implements none, so its entities are inert records just
	// as an unregistered classname's are — the debug surfaces must not report otherwise.
	Ent->bRecordOnly = bRecord || Desc->bStub;
	Ent->Construct(Def, Handle, *Desc);
	return Ent;
}

void FElysiumClassRegistry::ForEach(TFunctionRef<void(const FElysiumClassDesc&)> Fn) const
{
	for (const TPair<FName, TUniquePtr<FElysiumClassDesc>>& Pair : Classes)
	{
		if (Pair.Value) { Fn(*Pair.Value); }
	}
}

// --- The base class: CBaseEntity --------------------------------------------------------
// Every entity's chain terminates here. The three base inputs and the base keyfield contract
// (python_bridge.md) reach every subclass through the chain walk, so they register once.

static TUniquePtr<FElysiumEntity> MakeBaseEntity()
{
	return MakeUnique<FElysiumEntity>();
}

static FElysiumClassRegistrar GRegBaseEntity(
	ElysiumBaseClassName(), NAME_None, &MakeBaseEntity,
	[](FElysiumClassDesc& D)
	{
		// Base inputs — received by nearly every classname (entity_io.md "Base inputs").
		D.Input(TEXT("Kill"),         [](FElysiumEntity& E, const FElysiumInputArgs&) { E.Kill(); });
		D.Input(TEXT("ScriptHide"),   [](FElysiumEntity& E, const FElysiumInputArgs&) { E.ScriptHide(); });
		D.Input(TEXT("ScriptUnhide"), [](FElysiumEntity& E, const FElysiumInputArgs&) { E.ScriptUnhide(); });
		D.Input(TEXT("PlayDialogFile"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ E.PlayDialogFile(A.Param.ToString()); });
		D.Input(TEXT("SetSoundOverrideEnt"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ E.SetSoundOverrideEnt(A.Param.ToString()); });
		D.Input(TEXT("SetFakeSilence"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ E.SetFakeSilence(A.Param.ToInt() != 0); });

		// Base keyfields — the `CBaseEntity` datamap, generated from the replay by
		// `gen_kernel_bindings` (0019 story 2 pass B). Every row retail declares, with retail's own
		// flags: `SAVE|KEY` is readable and saved but NOT keyable, because VtMB's Python write gate
		// is the `INPUT` bit and not `KEY` (`docs/vtmb/python_bridge.md` § "The write path"). Spawn
		// keyvalue application ignores the gate either way, so an authored key still lands.
		ElysiumNpcKernelBindings::AddBaseEntityFields(D);

		// The two rows the replay does not put on `CBaseEntity`. Retail declares `use_icon` and
		// `locked_icon` on `CBaseButton`, `CBaseDoor`, `CPushable`, `CPropSwitch` and kin, one copy
		// each; this port carries the reticle pair once on the base, which is a generalisation and
		// not a recovered row — so it stays hand-written where the generated table cannot claim it.
		D.Field(TEXT("use_icon"),        &FElysiumEntity::UseIcon);
		D.Field(TEXT("locked_icon"),     &FElysiumEntity::LockedIcon);
	});

// --- Verification command ---------------------------------------------------------------
// `elysium.classes [classname]` inspects the registry, the same way `elysium.ents` verifies
// the def parser before the world consumes it. No arg lists the registered classes; a
// classname resolves it (or shows the inert-record fallback), dumps the chain-walked input +
// field tables, then constructs a throwaway probe entity and exercises the base contract:
// apply keyvalues, then fire ScriptHide -> ScriptUnhide -> Kill *through the registry* so the
// chain lookup and the dormancy switch are proven end-to-end. It spawns nothing persistent.

static void ElysiumDumpClass(const FElysiumClassRegistry& Reg, const FString& RawName)
{
	const FName ClassName(*RawName);
	const FElysiumClassDesc* Desc = Reg.Find(ClassName);
	const bool bRecord = (Desc == nullptr);
	if (bRecord)
	{
		Desc = Reg.BaseDesc();
		UE_LOG(LogElysiumClass, Display, TEXT("%s: unregistered -> inert record on %s"),
			*RawName, *ElysiumBaseClassName().ToString());
	}
	if (Desc == nullptr)
	{
		UE_LOG(LogElysiumClass, Warning, TEXT("elysium.classes: no base descriptor registered"));
		return;
	}

	// Chain-walked tables (derived shadows base): collect each unique name once.
	TSet<FName> InputNames;
	TSet<FName> FieldNames;
	for (const FElysiumClassDesc* D = Desc; D != nullptr; D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
	{
		for (const TPair<FName, FElysiumInputThunk>& I : D->Inputs) { InputNames.Add(I.Key); }
		for (const TPair<FName, FElysiumFieldAccessor>& F : D->Fields) { FieldNames.Add(F.Key); }
	}

	UE_LOG(LogElysiumClass, Display, TEXT("%s (base %s): %d inputs, %d fields (chain-resolved)"),
		*Desc->ClassName.ToString(),
		Desc->BaseName.IsNone() ? TEXT("-") : *Desc->BaseName.ToString(),
		InputNames.Num(), FieldNames.Num());

	TArray<FString> InputLines;
	for (const FName& N : InputNames) { InputLines.Add(N.ToString()); }
	InputLines.Sort();
	for (const FString& L : InputLines)
	{
		UE_LOG(LogElysiumClass, Display, TEXT("  input  %s"), *L);
	}
	TArray<FString> FieldLines;
	for (const FName& N : FieldNames)
	{
		const FElysiumFieldAccessor* Acc = Reg.FindField(*Desc, N);
		FieldLines.Add(FString::Printf(TEXT("%-16s %s%s"),
			*N.ToString(),
			Acc ? ElysiumVariantTypeName(Acc->Type) : TEXT("?"),
			(Acc && Acc->bKeyable) ? TEXT(" [key]") : TEXT("")));
	}
	FieldLines.Sort();
	for (const FString& L : FieldLines)
	{
		UE_LOG(LogElysiumClass, Display, TEXT("  field  %s"), *L);
	}

	// Probe: build one entity of this class and exercise the base contract off the tables.
	FElysiumEntityDef Probe;
	Probe.Classname = RawName;
	Probe.TargetName = TEXT("probe");
	Probe.Keys.Add(TEXT("spawnflags"), TEXT("1057"));
	Probe.Keys.Add(TEXT("health"), TEXT("100"));

	TUniquePtr<FElysiumEntity> Ent = Reg.Create(Probe, FElysiumEntityHandle(0, /*Epoch*/1));
	UE_LOG(LogElysiumClass, Display, TEXT("probe %s record-only=%s spawnflags=%d health=%d"),
		*Ent->DebugString(), Ent->IsRecordOnly() ? TEXT("yes") : TEXT("no"), Ent->SpawnFlags, Ent->Health);

	auto Fire = [&Reg, &Ent](const TCHAR* Input)
	{
		if (const FElysiumInputThunk Thunk = Reg.FindInput(*Ent->Class, FName(Input)))
		{
			Thunk(*Ent, FElysiumInputArgs{});
			UE_LOG(LogElysiumClass, Display, TEXT("  %-13s -> hidden=%d dead=%d inert=%d"),
				Input, Ent->IsHidden() ? 1 : 0, Ent->IsDead() ? 1 : 0, Ent->IsInert() ? 1 : 0);
		}
		else
		{
			UE_LOG(LogElysiumClass, Warning, TEXT("  %-13s -> no thunk (chain miss)"), Input);
		}
	};
	Fire(TEXT("ScriptHide"));
	Fire(TEXT("ScriptUnhide"));
	Fire(TEXT("Kill"));
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumClassesCmd(
	TEXT("elysium.classes"),
	TEXT("elysium.classes [classname] — list the class registry, or resolve one classname and probe the base contract"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* /*World*/)
	{
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		if (Args.Num() >= 1)
		{
			ElysiumDumpClass(Reg, Args[0]);
			return;
		}

		UE_LOG(LogElysiumClass, Display, TEXT("class registry: %d registered"), Reg.Num());
		TArray<FString> Lines;
		Reg.ForEach([&Lines](const FElysiumClassDesc& D)
		{
			Lines.Add(FString::Printf(TEXT("  %s (base %s): %d inputs, %d fields"),
				*D.ClassName.ToString(),
				D.BaseName.IsNone() ? TEXT("-") : *D.BaseName.ToString(),
				D.Inputs.Num(), D.Fields.Num()));
		});
		Lines.Sort();
		for (const FString& L : Lines)
		{
			UE_LOG(LogElysiumClass, Display, TEXT("%s"), *L);
		}
		UE_LOG(LogElysiumClass, Display,
			TEXT("unregistered classnames spawn as inert records on %s"), *ElysiumBaseClassName().ToString());
	}));
