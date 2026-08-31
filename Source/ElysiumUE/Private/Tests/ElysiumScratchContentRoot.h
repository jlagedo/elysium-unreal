#pragma once

// A scratch export root, installed for the scope of one test.
//
// Some production readers resolve their input through `FElysiumContentPaths`, so a test that wants
// to hand one a fabricated file has to move the root rather than the file. Setting
// `ELYSIUM_EXPORT_ROOT` is not enough: `FElysiumContentPaths::Root()` prefers a
// `-ElysiumContentRoot=` command-line pin over the environment, and `uv run elysium test` always
// passes that pin -- so an environment-only override is defeated on every real run, and a test
// guarding on `IsSamePath` self-skips forever while still counting as executed.
//
// This overrides both, restores both, and removes the scratch tree on the way out.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

struct FElysiumScratchContentRoot
{
	/** @param Name  A suite-unique leaf; two suites sharing one would race on the same tree. */
	explicit FElysiumScratchContentRoot(const TCHAR* Name)
		: Root(FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("ElysiumTests") / Name))
		, SavedEnvironment(FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_EXPORT_ROOT")))
		, SavedCommandLine(FCommandLine::Get())
	{
		IFileManager::Get().MakeDirectory(*Root, /*Tree*/ true);
		FPlatformMisc::SetEnvironmentVar(TEXT("ELYSIUM_EXPORT_ROOT"), *Root);
		// The pin wins over the environment, so it is what has to move. Appending is enough:
		// `FParse::Value` takes the first match, so the replacement is written to the front.
		// `FElysiumContentPaths::CorpusRoot()` takes its own separate `-ElysiumCorpusRoot=` pin
		// with no environment fallback, so a reader through `VdataDir()`/`VdataFile()` is immune
		// to the override above unless that pin moves too -- moved onto the same scratch tree.
		FCommandLine::Set(*FString::Printf(TEXT("-ElysiumContentRoot=\"%s\" -ElysiumCorpusRoot=\"%s\" %s"),
			*Root, *Root, *SavedCommandLine));
	}

	~FElysiumScratchContentRoot()
	{
		FCommandLine::Set(*SavedCommandLine);
		FPlatformMisc::SetEnvironmentVar(TEXT("ELYSIUM_EXPORT_ROOT"),
			SavedEnvironment.IsEmpty() ? TEXT("") : *SavedEnvironment);
		IFileManager::Get().DeleteDirectory(*Root, /*RequireExists*/ false, /*Tree*/ true);
	}

	FElysiumScratchContentRoot(const FElysiumScratchContentRoot&) = delete;
	FElysiumScratchContentRoot& operator=(const FElysiumScratchContentRoot&) = delete;

	/** Makes `Root/<Leaf>` and answers it, for the domain subdirectory a reader expects. */
	FString Directory(const TCHAR* Leaf) const
	{
		const FString Path = Root / Leaf;
		IFileManager::Get().MakeDirectory(*Path, /*Tree*/ true);
		return Path;
	}

	/** Whether the override actually took. A caller asserts this rather than skipping on it. */
	bool IsInstalled() const { return FPaths::IsSamePath(FElysiumContentPaths::Root(), Root); }

	FString Root;

private:
	FString SavedEnvironment;
	FString SavedCommandLine;
};

#endif   // WITH_DEV_AUTOMATION_TESTS
