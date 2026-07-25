#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class UElysiumMapSubsystem;

// The headless Lumen-card bake, sibling to FElysiumProfileRun / FElysiumShotRun. Activated by
// `-ElysiumCards` (cards.bat). Documented in docs/lumen-coverage-spike.md.
//
// It removes the human from "load each map, fit cards to everything it built, write the sidecar":
// once a map's build has settled it takes the bake items the map actor recorded — one per world
// chunk and per prop model — runs the editor surfel card builder over each, writes
// `tools/out/<map>/<map>.cards`, and travels to the next map. Exits when the list is exhausted.
//
// The point of baking through a real map load rather than a separate offline chunker is that the
// meshes fitted are *the meshes the game builds*. There is no second implementation of the
// chunking to keep in step: change `elysium.LumenCardCellCm` and the next bake simply produces
// different buckets, while an existing sidecar's keys stop matching and the runtime falls back to
// bounds cards until it is re-run.
//
// Editor builds only — `IMeshUtilities::GenerateCardRepresentationData` ray-traces through Embree,
// which is not in a shipping build. Compiles to an inert shell elsewhere.
class FElysiumCardRun
{
public:
	explicit FElysiumCardRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumCardRun();

	// True when -ElysiumCards was passed to an editor-target build.
	static bool IsRequested();

	// The map list a -ElysiumCards run will walk: `-ElysiumMap=<name>` alone, or every exported
	// map under tools/out that has a <map>.obj, sorted. Public so cards.bat's log can be checked
	// against it.
	static void ResolveMapList(TArray<FString>& OutMaps);

private:
	bool Tick(float DeltaSeconds);
	void BakeCurrent();
	void Advance();
	void Finish();

	enum class EPhase : uint8 { WaitReady, Bake, Travel, Done };

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	EPhase Phase = EPhase::WaitReady;
	int32 MapIndex = 0;
	int32 FrameInPhase = 0;

	TArray<FString> Maps;

	// Per-map tally for the closing summary.
	struct FResult
	{
		FString Map;
		int32 Written = 0;
		int32 Failed = 0;
		double Seconds = 0.0;
	};
	TArray<FResult> Results;
};
