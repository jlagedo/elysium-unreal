#pragma once

// A scratch corpus root, installed for the scope of one test.
//
// Some production readers resolve their input through `FElysiumContentPaths`, so a test that wants
// to hand one a fabricated file has to move the root rather than the file.
// `FElysiumContentPaths::CorpusRoot()` takes a `-ElysiumCorpusRoot=` command-line pin ahead of its
// `Content/ElysiumCorpus` default and reads no environment variable at all, so the pin is the whole
// override -- appending is enough, because `FParse::Value` takes the first match and the
// replacement is written to the front.
//
// It was `FElysiumScratchContentRoot` until 0018 story 21-6, when the export root it also moved
// (`ELYSIUM_EXPORT_ROOT` plus a `-ElysiumContentRoot=` pin that outranked it) ceased to exist. The
// rename is deliberate: a caller that wanted the old meaning should not compile.
//
// This overrides the pin, restores it, and removes the scratch tree on the way out.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

struct FElysiumScratchCorpusRoot
{
	/** @param Name  A suite-unique leaf; two suites sharing one would race on the same tree. */
	explicit FElysiumScratchCorpusRoot(const TCHAR* Name)
		: Root(FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("ElysiumTests") / Name))
		, SavedCommandLine(FCommandLine::Get())
	{
		IFileManager::Get().MakeDirectory(*Root, /*Tree*/ true);
		FCommandLine::Set(*FString::Printf(TEXT("-ElysiumCorpusRoot=\"%s\" %s"),
			*Root, *SavedCommandLine));
	}

	~FElysiumScratchCorpusRoot()
	{
		FCommandLine::Set(*SavedCommandLine);
		IFileManager::Get().DeleteDirectory(*Root, /*RequireExists*/ false, /*Tree*/ true);
	}

	FElysiumScratchCorpusRoot(const FElysiumScratchCorpusRoot&) = delete;
	FElysiumScratchCorpusRoot& operator=(const FElysiumScratchCorpusRoot&) = delete;

	/** Makes `Root/<Leaf>` and answers it, for the domain subdirectory a reader expects. */
	FString Directory(const TCHAR* Leaf) const
	{
		const FString Path = Root / Leaf;
		IFileManager::Get().MakeDirectory(*Path, /*Tree*/ true);
		return Path;
	}

	/** Whether the override actually took. A caller asserts this rather than skipping on it. */
	bool IsInstalled() const { return FPaths::IsSamePath(FElysiumContentPaths::CorpusRoot(), Root); }

	FString Root;

private:
	FString SavedCommandLine;
};

#endif   // WITH_DEV_AUTOMATION_TESTS
