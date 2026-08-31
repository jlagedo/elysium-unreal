#include "Scripting/ElysiumScriptFS.h"

#include "ElysiumContentPaths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumScriptFs, Log, All);

namespace
{
	// 0 = denials only, 1 = + writes and reads with no mirror behind them, 2 = every resolve.
	// Level 1 is the interesting one: a write is a script mutating game state, and an unmirrored
	// read is a script asking for content this rebuild does not carry (the `.lip` probes that gate
	// alternate dialogue lines, vamputil.py:1039 -- so the branch it takes is a known divergence,
	// not a silent one).
	int32 GScriptFsLog = 1;
	FAutoConsoleVariableRef CVarScriptFsLog(
		TEXT("elysium.script.fs.log"),
		GScriptFsLog,
		TEXT("Script filesystem logging: 0 = denials only, 1 = + writes and unmirrored reads, 2 = every resolve."),
		ECVF_Default);

	// One line per distinct unmirrored path, not one per call -- the `.lip` probes fire per dialogue
	// line and would otherwise bury the log.
	TSet<FString>& WarnedPaths()
	{
		static TSet<FString> Set;
		return Set;
	}

	// Sandbox-relative tree prefix -> the mirror directory it is served from. Ordered: the first
	// match wins, so `vdata/signs/` must precede `vdata/`.
	//
	// Most mounts are the trees the offline pipeline mirrors under Root(). `vdata/` is the
	// exception: it now serves from FElysiumContentPaths::VdataDir(), the export_v2 capsule import
	// onto CorpusRoot() (docs/project/seam_migration.md "Settled"); `vdata/signs/` stays on Root()'s
	// legacy `signs/` mirror, not yet migrated. `python/` lands on out/scripts because that is where
	// UE_extract_scripts.py puts VtMB's `Vampire/python/` tree; VtMB's own `Vampire/scripts/`
	// (kb_act.lst and the Valve script files) is a different tree and has no mirror, which is why
	// hunter mode's keybinding copy resolves to nothing and says so.
	const TArray<TPair<FString, FString>>& Mounts()
	{
		static const TArray<TPair<FString, FString>> Table = []()
		{
			const FString Root = FPaths::ConvertRelativePathToFull(FElysiumContentPaths::Root());
			TArray<TPair<FString, FString>> T;
			T.Emplace(TEXT("cfg/"),         Root / TEXT("cfg"));
			T.Emplace(TEXT("vdata/signs/"), Root / TEXT("signs")); // extracted flat + lowercased
			T.Emplace(TEXT("vdata/"),       FElysiumContentPaths::VdataDir());
			T.Emplace(TEXT("python/"),      Root / TEXT("scripts"));
			T.Emplace(TEXT("dlg/"),         Root / TEXT("dlg"));
			T.Emplace(TEXT("sound/"),       Root / TEXT("sound"));
			return T;
		}();
		return Table;
	}

	// Absolute in the Windows sense a VtMB script could produce: a drive-qualified path or a UNC
	// share. A single leading separator is NOT absolute here -- in the real game it would be
	// cwd-drive-relative, and reading it as sandbox-relative is both safer and what the script means.
	bool IsRootedPath(const FString& P)
	{
		return (P.Len() >= 2 && P[1] == TEXT(':')) || P.StartsWith(TEXT("//"));
	}

	FString ToPlatform(const FString& P)
	{
		FString Out = P;
		FPaths::MakePlatformFilename(Out);
		return Out;
	}
}

const FString& FElysiumScriptFS::VirtualRoot()
{
	static const FString Root = ToPlatform(
		FPaths::ConvertRelativePathToFull(FElysiumContentPaths::ScriptFsRoot()));
	return Root;
}

void FElysiumScriptFS::EnsureOverlayRoot()
{
	IFileManager::Get().MakeDirectory(*VirtualRoot(), /*Tree*/ true);
}

bool FElysiumScriptFS::NormalizeToSandbox(const FString& VirtualPath, FString& OutRelative)
{
	OutRelative.Reset();

	FString P = VirtualPath.Replace(TEXT("\\"), TEXT("/"));
	if (P.IsEmpty())
	{
		return false;
	}

	if (IsRootedPath(P))
	{
		// An absolute path is in-bounds only if it names the virtual root. Everything else is a
		// script reaching at the real machine.
		const FString RootSlash = VirtualRoot().Replace(TEXT("\\"), TEXT("/"));
		if (!P.StartsWith(RootSlash, ESearchCase::IgnoreCase))
		{
			return false;
		}
		P = P.Mid(RootSlash.Len());
		if (!P.IsEmpty() && P[0] != TEXT('/'))
		{
			return false; // a sibling whose name merely starts with the root's, not a child
		}
	}

	// Walk the segments so `..` is resolved against the sandbox rather than trusted.
	TArray<FString> Segments;
	P.ParseIntoArray(Segments, TEXT("/"), /*CullEmpty*/ true);

	TArray<FString> Stack;
	for (const FString& Seg : Segments)
	{
		if (Seg == TEXT("."))
		{
			continue;
		}
		if (Seg == TEXT(".."))
		{
			if (Stack.Num() == 0)
			{
				return false; // climbs out of the sandbox
			}
			Stack.Pop();
			continue;
		}
		Stack.Add(Seg);
	}

	// Fold one leading moddir component away, so style A (`<root>\Vampire\cfg\config.cfg`) and a
	// bare `cfg/config.cfg` land on the same sandbox-relative form and therefore the same overlay
	// file. Only the first component, and only one -- a real `Vampire` directory deeper in a tree
	// stays where it is.
	if (Stack.Num() > 0 && Stack[0].Equals(ModDir(), ESearchCase::IgnoreCase))
	{
		Stack.RemoveAt(0);
	}

	OutRelative = FString::Join(Stack, TEXT("/"));
	return true;
}

bool FElysiumScriptFS::MapToMirror(const FString& SandboxRelative, FString& OutRealPath)
{
	for (const TPair<FString, FString>& Mount : Mounts())
	{
		FString Sub;
		if (SandboxRelative.StartsWith(Mount.Key, ESearchCase::IgnoreCase))
		{
			Sub = SandboxRelative.Mid(Mount.Key.Len());
		}
		else if (SandboxRelative.Equals(Mount.Key.LeftChop(1), ESearchCase::IgnoreCase))
		{
			Sub.Reset(); // the mount point itself, which is what `nt.listdir` on a tree asks for
		}
		else
		{
			continue;
		}

		// The extractors write lowercased trees while the scripts spell paths in authored case.
		// Windows hides the difference; resolving it explicitly is what keeps a packaged build on a
		// case-sensitive filesystem working.
		const FString Exact = Sub.IsEmpty() ? ToPlatform(Mount.Value) : ToPlatform(Mount.Value / Sub);
		if (IFileManager::Get().FileExists(*Exact) || IFileManager::Get().DirectoryExists(*Exact))
		{
			OutRealPath = Exact;
			return true;
		}
		if (!Sub.IsEmpty())
		{
			const FString Lower = ToPlatform(Mount.Value / Sub.ToLower());
			if (IFileManager::Get().FileExists(*Lower) || IFileManager::Get().DirectoryExists(*Lower))
			{
				OutRealPath = Lower;
				return true;
			}
		}

		// Mounted but absent. Still a mirror path -- the caller's syscall fails against the tree the
		// file would have come from, which is the honest error.
		OutRealPath = Exact;
		return true;
	}
	return false;
}

EElysiumFsAccess FElysiumScriptFS::AccessFromMode(const FString& Mode)
{
	// `w`/`w+` truncate, so nothing carries over; `a`, `a+` and `r+` all keep the existing bytes.
	if (Mode.Contains(TEXT("w")))
	{
		return EElysiumFsAccess::Write;
	}
	if (Mode.Contains(TEXT("a")) || Mode.Contains(TEXT("+")))
	{
		return EElysiumFsAccess::Update;
	}
	return EElysiumFsAccess::Read;
}

bool FElysiumScriptFS::Resolve(const FString& VirtualPath, EElysiumFsAccess Access,
	FString& OutRealPath, FString& OutError)
{
	FString Rel;
	if (!NormalizeToSandbox(VirtualPath, Rel))
	{
		OutError = FString::Printf(
			TEXT("[Elysium] path is outside the script sandbox: %s"), *VirtualPath);
		UE_LOG(LogElysiumScriptFs, Warning, TEXT("DENY %s"), *VirtualPath);
		return false;
	}

	// An empty remainder is the root itself -- what `nt.getcwd()` hands back, and what
	// `fileutil.isDir(getcwd() + "\\Vampire")` asks about once the moddir folds away.
	const FString OverlayPath = Rel.IsEmpty() ? VirtualRoot() : ToPlatform(VirtualRoot() / Rel);

	if (Access == EElysiumFsAccess::Read)
	{
		if (IFileManager::Get().FileExists(*OverlayPath) ||
			IFileManager::Get().DirectoryExists(*OverlayPath))
		{
			OutRealPath = OverlayPath;
			if (GScriptFsLog >= 2)
			{
				UE_LOG(LogElysiumScriptFs, Log, TEXT("read  %s -> overlay %s"), *Rel, *OutRealPath);
			}
			return true;
		}

		FString MirrorPath;
		if (MapToMirror(Rel, MirrorPath))
		{
			OutRealPath = MirrorPath;
			const bool bPresent = IFileManager::Get().FileExists(*MirrorPath) ||
				IFileManager::Get().DirectoryExists(*MirrorPath);
			if (!bPresent && GScriptFsLog >= 1 && !WarnedPaths().Contains(Rel))
			{
				WarnedPaths().Add(Rel);
				UE_LOG(LogElysiumScriptFs, Log,
					TEXT("read  %s -> not in the mirror (%s); the script sees it as missing"),
					*Rel, *MirrorPath);
			}
			else if (bPresent && GScriptFsLog >= 2)
			{
				UE_LOG(LogElysiumScriptFs, Log, TEXT("read  %s -> mirror %s"), *Rel, *OutRealPath);
			}
			return true;
		}

		// No mount covers this tree. Leave the path in the overlay so the caller's own syscall
		// fails, and say once which tree was asked for -- that list is the RE record of what the
		// scripts expect the install to hold and this rebuild does not mirror.
		OutRealPath = OverlayPath;
		if (GScriptFsLog >= 1 && !WarnedPaths().Contains(Rel))
		{
			WarnedPaths().Add(Rel);
			UE_LOG(LogElysiumScriptFs, Log,
				TEXT("read  %s -> no mirror mount for this tree; the script sees it as missing"), *Rel);
		}
		return true;
	}

	// Both write paths land in the overlay, never in Root().
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OverlayPath), /*Tree*/ true);

	if (Access == EElysiumFsAccess::Update && !IFileManager::Get().FileExists(*OverlayPath))
	{
		// Copy-on-write: an append or a read-modify-write has to start from the shipped bytes.
		FString MirrorPath;
		if (MapToMirror(Rel, MirrorPath) && IFileManager::Get().FileExists(*MirrorPath))
		{
			if (IFileManager::Get().Copy(*OverlayPath, *MirrorPath) == COPY_OK)
			{
				UE_LOG(LogElysiumScriptFs, Log, TEXT("copy-up %s from the mirror"), *Rel);
			}
			else
			{
				UE_LOG(LogElysiumScriptFs, Warning,
					TEXT("copy-up %s FAILED (%s -> %s); the script will see an empty file"),
					*Rel, *MirrorPath, *OverlayPath);
			}
		}
	}

	OutRealPath = OverlayPath;
	if (GScriptFsLog >= 1)
	{
		UE_LOG(LogElysiumScriptFs, Log, TEXT("write %s -> %s"), *Rel, *OutRealPath);
	}
	return true;
}

bool FElysiumScriptFS::ListDir(const FString& VirtualPath, TArray<FString>& OutNames, FString& OutError)
{
	OutNames.Reset();

	FString Rel;
	if (!NormalizeToSandbox(VirtualPath, Rel))
	{
		OutError = FString::Printf(
			TEXT("[Elysium] path is outside the script sandbox: %s"), *VirtualPath);
		UE_LOG(LogElysiumScriptFs, Warning, TEXT("DENY listdir %s"), *VirtualPath);
		return false;
	}

	// Union of both layers, overlay first so a name it shadows is only reported once.
	TSet<FString> Seen;
	auto Gather = [&Seen, &OutNames](const FString& Dir)
	{
		if (!IFileManager::Get().DirectoryExists(*Dir))
		{
			return;
		}
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*")), /*Files*/ true, /*Dirs*/ true);
		for (const FString& Name : Found)
		{
			bool bAlready = false;
			Seen.Add(Name.ToLower(), &bAlready);
			if (!bAlready)
			{
				OutNames.Add(Name);
			}
		}
	};

	Gather(Rel.IsEmpty() ? VirtualRoot() : ToPlatform(VirtualRoot() / Rel));
	FString MirrorPath;
	if (MapToMirror(Rel, MirrorPath))
	{
		Gather(MirrorPath);
	}
	return true;
}
