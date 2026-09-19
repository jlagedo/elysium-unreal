#include "AiInfra/ElysiumInfraActor.h"

#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "ElysiumEntityDefs.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

AElysiumInfraActor::AElysiumInfraActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	// A plain scene root: the billboard and arrow are editor-only, and a cooked actor still needs a
	// root for the location the adoption check reads.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

#if WITH_EDITORONLY_DATA
	Billboard = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Billboard != nullptr)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> NoteSprite(
			TEXT("/Engine/EditorResources/S_Note"));
		Billboard->Sprite = NoteSprite.Get();
		Billboard->SetupAttachment(SceneRoot);
		Billboard->bIsScreenSizeScaled = true;
	}
	Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	if (Arrow != nullptr)
	{
		Arrow->SetupAttachment(SceneRoot);
		Arrow->ArrowSize = 0.5f;
		Arrow->bIsScreenSizeScaled = true;
	}
#endif
}

void AElysiumInfraActor::ConfigureBakedIdentity(int32 InEntityIndex, const FString& InSourceClassname,
	const FString& InTargetName)
{
	EntityIndex = InEntityIndex;
	SourceClassname = InSourceClassname;
	TargetName = InTargetName;
}

bool AElysiumInfraActor::ApplyBakedKeyvalues(const TArray<FString>& Keys, const TArray<FString>& Values)
{
	if (Keys.Num() != Values.Num())
	{
		return false;
	}
	TArray<ElysiumKeyfieldAccess::FView> Views;
	GetKeyfieldViews(Views);
	AuthoredKeys.Reset(Keys.Num());
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		AuthoredKeys.Add(FElysiumInfraKey{ Keys[i], Values[i] });
		const FProperty* Property = nullptr;
		if (const ElysiumKeyfieldAccess::FView* KeyOwner = ElysiumKeyfieldAccess::FindOwner(Views, Keys[i], Property))
		{
			// In authored order, so a repeated key leaves its last value, as the keyvalue parse does.
			ElysiumKeyfieldAccess::Apply(Property, KeyOwner->Data, Values[i]);
		}
	}
	return true;
}

void AElysiumInfraActor::SetBakedOutputs(const TArray<FElysiumInfraOutput>& InOutputs)
{
	Outputs = InOutputs;
}

void AElysiumInfraActor::GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews)
{
	OutViews.Add({ FElysiumBaseEntityKeyfields::StaticStruct(), &Base });
}

void AElysiumInfraActor::GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews) const
{
	// The views are read-only in every const caller; the non-const overload is the one virtual.
	const_cast<AElysiumInfraActor*>(this)->GetKeyfieldViews(OutViews);
}

void AElysiumInfraActor::BuildDefKeys(TMap<FString, FString>& OutKeys) const
{
	using namespace ElysiumKeyfieldAccess;
	TArray<FView> Views;
	GetKeyfieldViews(Views);

	// The last authored string per property: the value the keyvalue parse left there.
	TMap<const FProperty*, const FString*> LastRaw;
	TSet<const FProperty*> Authored;
	for (const FElysiumInfraKey& Pair : AuthoredKeys)
	{
		const FProperty* Property = nullptr;
		if (FindOwner(Views, Pair.Key, Property) != nullptr)
		{
			LastRaw.Add(Property, &Pair.Value);
			Authored.Add(Property);
		}
	}

	// The emitted pairs, in order, before the entity table's fold.
	TArray<TPair<FString, FString>> Emitted;
	Emitted.Reserve(AuthoredKeys.Num());
	for (const FElysiumInfraKey& Pair : AuthoredKeys)
	{
		const FProperty* Property = nullptr;
		const FView* KeyOwner = FindOwner(Views, Pair.Key, Property);
		if (KeyOwner == nullptr || Matches(Property, KeyOwner->Data, *LastRaw.FindChecked(Property)))
		{
			Emitted.Emplace(Pair.Key, Pair.Value);
		}
		else
		{
			Emitted.Emplace(Pair.Key, Format(Property, KeyOwner->Data));
		}
	}
	// Datamap rows the map never authored reach the def only once someone gave them a value.
	for (const FView& View : Views)
	{
		for (TFieldIterator<FProperty> It(View.Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (Authored.Contains(Property) || FindProperty(View.Struct, Property->GetName()) != Property)
			{
				continue;
			}
			// A later view that owns the same name is shadowed by the first; emit through the owner.
			const FProperty* OwnerProperty = nullptr;
			if (FindOwner(Views, Property->GetName(), OwnerProperty) == nullptr || OwnerProperty != Property)
			{
				continue;
			}
			if (!IsZero(Property, View.Data))
			{
				Emitted.Emplace(Property->GetName(), Format(Property, View.Data));
			}
		}
	}

	// The entity table's fold (`UE_map_sidecars.collect_entity_fields`, legacy): one slot per EXACT
	// spelling, at the position of its first occurrence, holding its last value. The map then adds
	// the slots in that order, exactly as both transports load them.
	TArray<TPair<FString, FString>> Slots;
	for (const TPair<FString, FString>& Pair : Emitted)
	{
		TPair<FString, FString>* Existing = Slots.FindByPredicate(
			[&Pair](const TPair<FString, FString>& Slot) { return Slot.Key.Equals(Pair.Key, ESearchCase::CaseSensitive); });
		if (Existing != nullptr)
		{
			Existing->Value = Pair.Value;
		}
		else
		{
			Slots.Add(Pair);
		}
	}
	OutKeys.Reset();
	for (const TPair<FString, FString>& Slot : Slots)
	{
		OutKeys.Add(Slot.Key, Slot.Value);
	}
}

void AElysiumInfraActor::BuildDefOutputs(TArray<FElysiumOutputDef>& OutOutputs) const
{
	OutOutputs.Reset(Outputs.Num());
	for (const FElysiumInfraOutput& Row : Outputs)
	{
		FElysiumOutputDef& Out = OutOutputs.AddDefaulted_GetRef();
		Out.Name = Row.Name;
		Out.Target = Row.Target;
		Out.Input = Row.Input;
		Out.Param = Row.Param;
		Out.Delay = Row.Delay;
		// `UElysiumMapEntities::Deserialize` and `FElysiumEntityDefs::Parse`: an authored 0 is -1.
		Out.Times = Row.Times == 0 ? -1 : Row.Times;
		Out.Python = Row.Python;
	}
}

bool AElysiumInfraActor::ApplyToDef(FElysiumEntityDef& Def) const
{
	Def.TargetName = TargetName;
	BuildDefKeys(Def.Keys);
	BuildDefOutputs(Def.Outputs);
	// The actor's place is the entity's. The bake stands it exactly on the table's origin, so an
	// untouched actor changes nothing; a moved one carries the move into both the placement and
	// the authored `origin` keyvalue, in Source inches with Y negated back (`formats/bsp.py`
	// `source_to_unreal`, inverted).
	const FVector Location = GetActorLocation();
	const bool bMoved = !Location.Equals(Def.Origin, MovedToleranceCm);
	if (bMoved)
	{
		Def.Origin = Location;
		const double Inches = 1.0 / 2.54;
		Def.Keys.Add(TEXT("origin"), FString::Printf(TEXT("%.9g %.9g %.9g"),
			Location.X * Inches, -Location.Y * Inches, Location.Z * Inches));
	}
	// `UE_map_sidecars.build_entities`: `keys.get("StartHidden", "0") == "1"`, exact spelling.
	const FString* StartHidden = nullptr;
	for (const TPair<FString, FString>& Pair : Def.Keys)
	{
		if (Pair.Key.Equals(TEXT("StartHidden"), ESearchCase::CaseSensitive))
		{
			StartHidden = &Pair.Value;
		}
	}
	Def.bStartHidden = StartHidden != nullptr && *StartHidden == TEXT("1");
	return bMoved;
}
