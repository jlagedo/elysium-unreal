#pragma once

#include "CoreMinimal.h"
#include "ElysiumChannels.h"

#if !UE_BUILD_SHIPPING

// The one recorder every harness run writes through (CCC0).
//
// It replaces the per-harness format each self-driving run grew for itself. What a run produces is
// a CSV of the frame channels it declared and a `.channels.json` manifest beside it carrying those
// declarations, the run's own metadata, the constants the geometry was derived from, and the run
// channels — so the comparator needs no knowledge of the harness that wrote it, and a camera or
// animation producer joins by declaring rows in `ElysiumChannels::Defs()` rather than by adding a
// second file.
//
// **A name that is not in the registry is refused at `Open`, not written.** That is the whole
// point: the failure mode being closed is a value that reaches disk with nothing that knows how to
// compare it.
//
// `Serialize` is the whole of the behaviour and touches no filesystem, so the format is asserted
// with no world (`Elysium.Substrate.ChannelRecorder`); `Write` is a thin wrapper over it.
class FElysiumChannelRecorder
{
public:
	// Declare this run's frame channels, in column order. Fails on an unknown name, a duplicate, or
	// a name whose registry entry is not frame-scoped.
	bool Open(TArrayView<const TCHAR* const> FrameChannels, FString& OutError);

	void BeginFrame();
	void Set(const TCHAR* Name, double Value);
	void Set(const TCHAR* Name, int32 Value) { Set(Name, static_cast<double>(Value)); }
	void Set(const TCHAR* Name, bool bValue) { Set(Name, bValue ? 1.0 : 0.0); }
	// Fails when a declared channel went unwritten this frame — a hole in a column is indistinguishable
	// from a zero once it is text.
	bool EndFrame(FString& OutError);

	// One value for the whole course. Must be a run-scoped registry name; an unknown one is dropped
	// and reported through `Errors()` rather than silently accepted.
	void SetRun(const TCHAR* Name, double Value);
	void SetRun(const TCHAR* Name, int32 Value) { SetRun(Name, static_cast<double>(Value)); }
	void SetRun(const TCHAR* Name, bool bValue) { SetRun(Name, bValue ? 1.0 : 0.0); }

	// Free-form run metadata. Not compared — it describes the run rather than measuring it.
	void SetMeta(const TCHAR* Key, const FString& Value);
	void SetMetaNumber(const TCHAR* Key, double Value);
	// The constants the geometry and the tuning came from, so a red bracket's manifest diff names
	// the constant that moved instead of leaving it to be guessed.
	void SetConstant(const TCHAR* Name, double Value);
	// A per-course tuning override, recorded because a run under one is not comparable with a run
	// without it.
	void SetOverride(const TCHAR* Name, double Value);

	void Serialize(FString& OutCsv, FString& OutManifest) const;
	bool Write(const FString& Dir, const FString& Stem, FString& OutError) const;

	int32 FrameCount() const { return FrameCount_; }
	bool IsOpen() const { return Columns.Num() > 0; }
	const TArray<FString>& Errors() const { return DeferredErrors; }

	void Reset();

private:
	int32 IndexOf(const TCHAR* Name) const;

	// The declared frame channels, in column order.
	TArray<const ElysiumChannels::FChannelDef*> Columns;
	// Frames × Columns, row-major.
	TArray<double> Values;
	// Which columns the frame in flight has written.
	TBitArray<> Written;
	int32 FrameCount_ = 0;
	bool bFrameOpen = false;

	TArray<TPair<const ElysiumChannels::FChannelDef*, double>> RunValues;
	TArray<TPair<FString, FString>> MetaStrings;
	TArray<TPair<FString, double>> MetaNumbers;
	TArray<TPair<FString, double>> Constants;
	TArray<TPair<FString, double>> Overrides;

	// Problems that cannot fail their caller (a bad `SetRun` mid-course), surfaced at write time.
	TArray<FString> DeferredErrors;
};

#endif // !UE_BUILD_SHIPPING
