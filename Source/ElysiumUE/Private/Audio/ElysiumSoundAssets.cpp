#include "ElysiumSoundAssets.h"

#include "ElysiumContentPaths.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSoundAssets, Log, All);

namespace
{
	const TCHAR* GSoundsRoot = TEXT("/ElysiumBaked/Sounds");

	// The index every question below is answered from: the object paths the bake wrote, grouped by
	// the package they live in. Built once from the asset registry (or fabricated by a test), and
	// held as plain strings because the answers are path questions -- nothing here loads an asset.
	struct FIndex
	{
		TSet<FString> Objects;                        // full object paths
		TMap<FString, TArray<FString>> ByFolder;      // package path -> asset names, sorted
		TSet<FString> Folders;                        // every package path that holds an asset

		void AddObjectPath(const FString& ObjectPath)
		{
			FString Package, Asset;
			if (!ObjectPath.Split(TEXT("."), &Package, &Asset, ESearchCase::CaseSensitive,
					ESearchDir::FromEnd))
			{
				return;
			}
			Objects.Add(ObjectPath);
			const FString Folder = FPaths::GetPath(Package);
			ByFolder.FindOrAdd(Folder).Add(Asset);
			// Every ancestor down to the root, so a walk can ask about a directory that holds only
			// sub-directories (`usable/openable` above its per-group folders).
			for (FString Walk = Folder; Walk.Len() > 0 && Walk != TEXT("/");
				Walk = FPaths::GetPath(Walk))
			{
				bool bAlready = false;
				Folders.Add(Walk, &bAlready);
				if (bAlready) { break; }
			}
		}

		void Sort()
		{
			for (TPair<FString, TArray<FString>>& Pair : ByFolder) { Pair.Value.Sort(); }
		}
	};

	FCriticalSection GIndexMutex;
	TUniquePtr<FIndex> GIndex;
	TUniquePtr<FIndex> GOverride;   // the test seam's fabricated key set

	// The one synchronous scan. In a cooked game the registry already carries every baked package,
	// so this is an index read; in an uncooked `-game`/editor run it is the first thing that walks
	// `/ElysiumBaked/Sounds` off disk, which is why it happens once, lazily, and says how long it
	// took. ~10,900 units.
	TUniquePtr<FIndex> BuildFromRegistry()
	{
		TUniquePtr<FIndex> Built = MakeUnique<FIndex>();
		IAssetRegistry* Registry = IAssetRegistry::Get();
		if (Registry == nullptr)
		{
			UE_LOG(LogElysiumSoundAssets, Warning,
				TEXT("no asset registry -- every sound reference will miss"));
			return Built;
		}
		const double Started = FPlatformTime::Seconds();
		Registry->ScanPathsSynchronous({ FString(GSoundsRoot) }, /*bForceRescan*/ false);

		TArray<FAssetData> Assets;
		Registry->GetAssetsByPath(FName(GSoundsRoot), Assets, /*bRecursive*/ true);
		for (const FAssetData& Asset : Assets)
		{
			Built->AddObjectPath(Asset.GetSoftObjectPath().ToString());
		}
		Built->Sort();
		UE_LOG(LogElysiumSoundAssets, Log,
			TEXT("indexed %d baked sound assets under %s in %.0f ms"),
			Built->Objects.Num(), GSoundsRoot, (FPlatformTime::Seconds() - Started) * 1000.0);
		if (Built->Objects.IsEmpty())
		{
			UE_LOG(LogElysiumSoundAssets, Warning,
				TEXT("no baked sound assets under %s -- every request will miss "
					 "(run `uv run elysium bake sounds`)"), GSoundsRoot);
		}
		return Built;
	}

	const FIndex& Index()
	{
		FScopeLock Lock(&GIndexMutex);
		if (GOverride.IsValid())
		{
			return *GOverride;
		}
		if (!GIndex.IsValid())
		{
			GIndex = BuildFromRegistry();
		}
		return *GIndex;
	}

	// A corpus-relative directory as a package path: every segment through the same sound fold
	// `BakedUnit` applies to a sound key's directories (space to hyphen, then the run collapse,
	// then the leading/trailing underscore strip a directory keeps).
	FString FolderPackage(const FString& Folder)
	{
		FString Out(GSoundsRoot);
		TArray<FString> Parts;
		Folder.ParseIntoArray(Parts, TEXT("/"), true);
		for (const FString& Part : Parts)
		{
			Out /= FElysiumContentPaths::SoundSafeName(Part, /*bStem*/ false);
		}
		return Out;
	}

	// `SW_pl_step1_wav` -> `pl_step1.wav`. The inverse only exists because the extension is kept in
	// the asset name; anything that is not a whole baked unit (an `_intro`/`_loop` half) has no key
	// spelling and is skipped by the caller. The recovered spelling is the FOLDED one -- a space
	// comes back as the hyphen the bake wrote -- and folding it again is the identity, which is
	// what makes an enumerated member submittable as an ordinary key.
	bool KeyFromAssetName(const FString& AssetName, FString& OutLeaf)
	{
		if (!AssetName.StartsWith(TEXT("SW_"), ESearchCase::CaseSensitive))
		{
			return false;
		}
		FString Stem = AssetName.RightChop(3);
		for (const TCHAR* Ext : { TEXT("wav"), TEXT("mp3") })
		{
			const FString Suffix = FString(TEXT("_")) + Ext;
			if (Stem.EndsWith(Suffix, ESearchCase::IgnoreCase))
			{
				OutLeaf = Stem.LeftChop(Suffix.Len()) + TEXT(".") + Ext;
				return true;
			}
		}
		return false;
	}
}

const TCHAR* ElysiumSoundAssets::PackageRoot()
{
	return GSoundsRoot;
}

FString ElysiumSoundAssets::ObjectPathFor(const FString& Rel)
{
	return FElysiumContentPaths::BakedUnit(TEXT("vtmb:sound:") + Rel, TEXT("SW"));
}

FString ElysiumSoundAssets::VariantObjectPathFor(const FString& Rel, const TCHAR* Role)
{
	return FElysiumContentPaths::BakedUnit(TEXT("vtmb:sound:") + Rel, TEXT("SW"), FString(Role));
}

bool ElysiumSoundAssets::Exists(const FString& Rel)
{
	return Resolve(Rel).IsValid();
}

ElysiumSoundAssets::FRef ElysiumSoundAssets::Resolve(const FString& Rel)
{
	FRef Out;
	const FString Base = ObjectPathFor(Rel);
	if (Base.IsEmpty())
	{
		return Out;
	}
	const FIndex& Idx = Index();
	if (Idx.Objects.Contains(Base))
	{
		Out.ObjectPath = Base;
		return Out;
	}
	// The split-loop shape: the unit exists only as its two halves.
	const FString Intro = VariantObjectPathFor(Rel, TEXT("intro"));
	const FString Loop = VariantObjectPathFor(Rel, TEXT("loop"));
	if (!Intro.IsEmpty() && Idx.Objects.Contains(Intro) && Idx.Objects.Contains(Loop))
	{
		Out.ObjectPath = Intro;
		Out.LoopObjectPath = Loop;
	}
	return Out;
}

bool ElysiumSoundAssets::FolderExists(const FString& Folder)
{
	return Index().Folders.Contains(FolderPackage(Folder));
}

TArray<FString> ElysiumSoundAssets::ListFolder(const FString& Folder)
{
	TArray<FString> Out;
	const FString Package = FolderPackage(Folder);
	const TArray<FString>* Names = Index().ByFolder.Find(Package);
	if (Names == nullptr)
	{
		return Out;
	}
	const FString Prefix = Folder.IsEmpty() ? FString() : Folder + TEXT("/");
	for (const FString& Name : *Names)
	{
		FString Leaf;
		if (KeyFromAssetName(Name, Leaf))
		{
			Out.Add(Prefix + Leaf);
		}
	}
	Out.Sort();
	return Out;
}

TArray<FString> ElysiumSoundAssets::ListSubfolders(const FString& Folder)
{
	TArray<FString> Out;
	const FString Package = FolderPackage(Folder);
	const FString Prefix = Package + TEXT("/");
	for (const FString& Known : Index().Folders)
	{
		if (!Known.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FString Tail = Known.RightChop(Prefix.Len());
		FString Head;
		if (Tail.Split(TEXT("/"), &Head, nullptr))
		{
			Tail = Head;
		}
		Out.AddUnique(Tail);
	}
	Out.Sort();
	return Out;
}

int32 ElysiumSoundAssets::Count()
{
	return Index().Objects.Num();
}

void ElysiumSoundAssets::Invalidate()
{
	FScopeLock Lock(&GIndexMutex);
	GIndex.Reset();
}

ElysiumSoundAssets::FScopedKeySet::FScopedKeySet(TArray<FString> Keys)
{
	TUniquePtr<FIndex> Built = MakeUnique<FIndex>();
	for (const FString& Key : Keys)
	{
		// A fabricated member is stated as an ordinary key, including the `<key>|intro` /
		// `<key>|loop` spelling for a split loop unit, so a test writes what the bake wrote.
		FString Base, Role;
		const FString Path = Key.Split(TEXT("|"), &Base, &Role)
			? VariantObjectPathFor(Base, *Role) : ObjectPathFor(Key);
		if (!Path.IsEmpty())
		{
			Built->AddObjectPath(Path);
		}
	}
	Built->Sort();
	FScopeLock Lock(&GIndexMutex);
	GOverride = MoveTemp(Built);
}

ElysiumSoundAssets::FScopedKeySet::~FScopedKeySet()
{
	FScopeLock Lock(&GIndexMutex);
	GOverride.Reset();
}
