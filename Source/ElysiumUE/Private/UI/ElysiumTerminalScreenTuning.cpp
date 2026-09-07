#include "UI/ElysiumTerminalScreenTuning.h"

#include "ElysiumContentPaths.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTerminalScreenTuning, Log, All);

namespace
{
	const TCHAR* GTerminalScreenTuningPath =
		TEXT("/Game/ElysiumAuthored/UI/DA_ElysiumTerminalScreenTuning.DA_ElysiumTerminalScreenTuning");

	// The bake's own asset prefixes for a placed model's two representations.
	const TCHAR* GBakedMeshPrefixes[] = { TEXT("SM_"), TEXT("SK_") };

	const UObject* MeshAssetOf(const UPrimitiveComponent* Body)
	{
		if (const UStaticMeshComponent* AsStatic = Cast<UStaticMeshComponent>(Body))
		{
			return AsStatic->GetStaticMesh();
		}
		if (const USkeletalMeshComponent* AsSkeletal = Cast<USkeletalMeshComponent>(Body))
		{
			return AsSkeletal->GetSkeletalMeshAsset();
		}
		return nullptr;
	}
}

const UElysiumTerminalScreenTuning* UElysiumTerminalScreenTuning::Load()
{
	// One attempt per process, success or failure: this is asked once per monitor bound per map
	// load, and a missing package would otherwise re-run a failing `LoadObject` every time.
	static TStrongObjectPtr<UElysiumTerminalScreenTuning> Table;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		Table = TStrongObjectPtr<UElysiumTerminalScreenTuning>(
			LoadObject<UElysiumTerminalScreenTuning>(nullptr, GTerminalScreenTuningPath));
		if (!Table.IsValid())
		{
			// Verbose, not Warning: no entries is the table's DEFAULT state and every terminal draws
			// unflipped without it. Only a model whose glass reads mirrored needs a row.
			UE_LOG(LogElysiumTerminalScreenTuning, Verbose,
				TEXT("terminal screen tuning: '%s' is absent; every monitor draws its render target "
					"unflipped"), GTerminalScreenTuningPath);
		}
	}
	return Table.Get();
}

FString UElysiumTerminalScreenTuning::ModelIdForBody(const UPrimitiveComponent* Body)
{
	const UObject* Mesh = MeshAssetOf(Body);
	const UPackage* Package = Mesh ? Mesh->GetPackage() : nullptr;
	if (!Package)
	{
		return FString();
	}
	// `/ElysiumBaked/Models/<dirs>/SM_<leaf>` is `FElysiumContentPaths::BakedUnit`'s own output for
	// `vtmb:model:<dirs>/<leaf>`, so the id is that address read back off the asset rather than
	// plumbed through the registration call. A mesh outside the baked mount (a test double, an
	// authored placeholder) has no model id and takes the default entry.
	const FString Root = FElysiumContentPaths::BakedMount() / TEXT("Models/");
	FString PackagePath = Package->GetName();
	if (!PackagePath.StartsWith(Root, ESearchCase::IgnoreCase))
	{
		return FString();
	}
	FString Relative = PackagePath.RightChop(Root.Len());
	FString Directories;
	FString Leaf = Relative;
	int32 LastSlash = INDEX_NONE;
	if (Relative.FindLastChar(TEXT('/'), LastSlash))
	{
		Directories = Relative.Left(LastSlash);
		Leaf = Relative.RightChop(LastSlash + 1);
	}
	for (const TCHAR* Prefix : GBakedMeshPrefixes)
	{
		if (Leaf.StartsWith(Prefix, ESearchCase::CaseSensitive))
		{
			Leaf = Leaf.RightChop(FCString::Strlen(Prefix));
			break;
		}
	}
	return Directories.IsEmpty() ? Leaf : Directories / Leaf;
}

FElysiumTerminalScreenTuningEntry UElysiumTerminalScreenTuning::FindForBody(
	const UPrimitiveComponent* Body)
{
	const UElysiumTerminalScreenTuning* Table = Load();
	const FString ModelId = ModelIdForBody(Body);
	if (!Table || ModelId.IsEmpty())
	{
		return FElysiumTerminalScreenTuningEntry();
	}
	const FElysiumTerminalScreenTuningEntry* Found = Table->Models.Find(FName(*ModelId));
	return Found ? *Found : FElysiumTerminalScreenTuningEntry();
}
