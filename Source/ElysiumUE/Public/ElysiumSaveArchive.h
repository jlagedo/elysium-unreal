#pragma once

#include "CoreMinimal.h"
#include "ElysiumSaveTypes.h"
#include "Serialization/ArchiveProxy.h"

// 11.9 — the payload writer/reader (`save-architecture.md` §2). An `FArchiveProxy`, so every stock
// `operator<<` still works and our own free operators compose with them, and it carries the one
// thing a bare archive cannot: **the schema id**, so a reader can branch or refuse (`Version()`).
//
// `FElysiumEntityHandle` is generation-checked, and the epoch a save was written under will never
// match the restored world's, so a handle serializes as `{Index, bWasValid}` with the epoch
// dropped. **Re-stamping is the applier's job, not the archive's**
// (`FElysiumEntityWorld::ApplySnapshot`): it is the only code that knows the live epoch, and an
// index that no longer resolves reads as Invalid — the same falsy value a killed entity already
// produces and which every call site already handles (§6).
struct FElysiumSaveArchive : public FArchiveProxy
{
	FElysiumSaveArchive(FArchive& InInner, int32 InVersion)
		: FArchiveProxy(InInner)
		, PayloadVersion(InVersion)
	{
		InInner.UsingCustomVersion(FElysiumSaveVersion::GUID);
		InInner.SetCustomVersion(FElysiumSaveVersion::GUID, InVersion, TEXT("ElysiumSave"));
	}

	int32 Version() const { return PayloadVersion; }

private:
	int32 PayloadVersion = FElysiumSaveVersion::Latest;
};

// --- The value types -----------------------------------------------------------------------------

FArchive& operator<<(FArchive& Ar, FElysiumVariant& V);
FArchive& operator<<(FArchive& Ar, FElysiumEntityHandle& H);
FArchive& operator<<(FArchive& Ar, FElysiumIOEvent& E);
FArchive& operator<<(FArchive& Ar, FElysiumOutputDef& O);
FArchive& operator<<(FArchive& Ar, FElysiumConvexHull& H);
FArchive& operator<<(FArchive& Ar, FElysiumEntityDef& D);
FArchive& operator<<(FArchive& Ar, ElysiumRng::FState& S);

// --- The blocks ----------------------------------------------------------------------------------

FArchive& operator<<(FArchive& Ar, FElysiumSheet& S);
FArchive& operator<<(FArchive& Ar, FElysiumXpEntry& E);
FArchive& operator<<(FArchive& Ar, FElysiumAssignedQuest& Q);
FArchive& operator<<(FArchive& Ar, FElysiumLawState& L);
FArchive& operator<<(FArchive& Ar, FElysiumPlayerRecord& R);
FArchive& operator<<(FArchive& Ar, FElysiumEntityState& S);
FArchive& operator<<(FArchive& Ar, FElysiumSavedFade& F);
FArchive& operator<<(FArchive& Ar, FElysiumMapSnapshot& M);
FArchive& operator<<(FArchive& Ar, FElysiumSessionBlock& S);
FArchive& operator<<(FArchive& Ar, FElysiumWorldBlock& W);
FArchive& operator<<(FArchive& Ar, FElysiumSaveHeaderData& H);

namespace ElysiumSave
{
	// Serialize the whole payload (the four blocks, in order). Both directions.
	void SerializePayload(FElysiumSaveArchive& Ar, FElysiumSavePayload& Payload);

	// Freeze a payload to bytes: the `ELYS` prologue (magic + version + supported floor,
	// uncompressed so a reader can refuse before inflating) followed by one Oodle-compressed
	// stream over the four blocks. One stream, not VtMB's per-section zlib — that exists to bound
	// memory on a 2004 machine.
	bool Write(const FElysiumSavePayload& Payload, TArray<uint8>& OutBytes, FString& OutError);

	// The mirror. Reports a readable reason on a bad magic, an unsupported version or a failed
	// decompress — never a silent half-read.
	bool Read(const TArray<uint8>& Bytes, FElysiumSavePayload& OutPayload, FString& OutError);

	// The payload's version without inflating it (reads the prologue only). 0 = not a payload.
	int32 PeekVersion(const TArray<uint8>& Bytes);

	// A readable name/value dump of a payload, one line per value — what `elysium.save.diff`
	// compares, so "why did this not persist" is a text diff rather than a debugger session.
	void Describe(const FElysiumSavePayload& Payload, TArray<FString>& OutLines);
}
