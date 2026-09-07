#include "ElysiumSaveArchive.h"

#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/MemoryWriter.h"

// The payload's schema id, registered as an engine custom version so a nested engine serializer
// sees it on the archive (`docs/architecture/save-architecture.md` §2). The number is also written into the prologue,
// because a raw memory archive carries no custom-version container of its own.
const FGuid FElysiumSaveVersion::GUID(0x45'4C'59'53, 0x53'41'56'45, 0x31'31'2E'39, 0x50'4C'44'00);

namespace
{
	FCustomVersionRegistration GRegisterElysiumSaveVersion(
		FElysiumSaveVersion::GUID, FElysiumSaveVersion::Latest, TEXT("ElysiumSave"));

	// The compressor. Oodle is the engine default and is available in every configuration.
	const FName GElysiumSaveCompressor = NAME_Oodle;
}

// Value types.

FArchive& operator<<(FArchive& Ar, FElysiumVariant& V)
{
	uint8 Type = static_cast<uint8>(V.Type);
	Ar << Type;
	if (Ar.IsLoading())
	{
		// An unknown type code from a newer payload collapses to Void rather than reading garbage
		// off the following bytes — but the payload's own version gate should have caught it first.
		V = FElysiumVariant();
		V.Type = (Type <= static_cast<uint8>(EElysiumVariantType::Handle))
			? static_cast<EElysiumVariantType>(Type) : EElysiumVariantType::Void;
	}
	switch (V.Type)
	{
	case EElysiumVariantType::Bool:   Ar << V.AsBool;   break;
	case EElysiumVariantType::Int:    Ar << V.AsInt;    break;
	case EElysiumVariantType::Float:  Ar << V.AsFloat;  break;
	case EElysiumVariantType::String: Ar << V.AsString; break;
	case EElysiumVariantType::Vector: Ar << V.AsVector; break;
	case EElysiumVariantType::Handle: Ar << V.AsHandle; break;
	default: break;   // Void carries nothing
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumEntityHandle& H)
{
	// `{Index, bWasValid}` — the epoch is dropped. Re-stamping is ApplySnapshot's job (§6).
	bool bWasSet = H.IsSet();
	Ar << H.Index;
	Ar << bWasSet;
	if (Ar.IsLoading())
	{
		H.Epoch = 0;
		if (!bWasSet)
		{
			H.Index = INDEX_NONE;
		}
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumIOEvent& E)
{
	Ar << E.FireTime;
	Ar << E.Target;
	Ar << E.Input;
	Ar << E.Param;
	Ar << E.PythonSrc;      // a deferred script's SOURCE STRING is how ScheduleTask survives a save
	Ar << E.Activator;
	Ar << E.Caller;
	Ar << E.Serial;
	// The wire the record came from, so a delivery that lands after a restore still attributes to
	// the authored row that produced it rather than reading as an unattributed event. Appended at
	// the end of the record and read behind its own version, so an `EventClock` payload restores
	// with no wire instead of being refused — additive, like the two schemas before it. Only the
	// identity travels; the tally it feeds measures one session and is rebuilt from zero.
	if (Ar.IsSaving() || Ar.CustomVer(FElysiumSaveVersion::GUID) >= FElysiumSaveVersion::WireIdentity)
	{
		Ar << E.Wire.SourceIndex;
		Ar << E.Wire.Output;
		Ar << E.Wire.Row;
	}
	else if (Ar.IsLoading())
	{
		E.Wire = FElysiumWireRef();
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumOutputDef& O)
{
	Ar << O.Name << O.Target << O.Input << O.Param << O.Delay << O.Times << O.Python;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumConvexHull& H)
{
	Ar << H.Vertices;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumEntityDef& D)
{
	Ar << D.Classname << D.TargetName << D.Origin << D.Keys;
	Ar << D.Model << D.Hulls << D.Contents << D.bBlocksPlayer;
	Ar << D.bStartHidden << D.bSky;
	Ar << D.ModelMesh << D.ModelQuat << D.HingeAxis;
	Ar << D.Outputs;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, ElysiumRng::FState& S)
{
	Ar << S.Seed << S.Current;
	return Ar;
}

// Blocks.

FArchive& operator<<(FArchive& Ar, FElysiumSheet& S)
{
	// Fixed-length arrays, written in container order. The slot count is compiled (`ElysiumSheetSlots.h`)
	// and so is stable across installs, but the arrays serialise with their own length anyway, which
	// is what makes a future slot-count change a version bump rather than a corrupt read.
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		Ar << S.Base[i];
		Ar << S.Current[i];
	}
	Ar << S.Extra;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumXpEntry& E)
{
	Ar << E.Entry << E.Amount;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumAssignedQuest& Q)
{
	Ar << Q.Title << Q.Table << Q.Quest << Q.State << Q.Order << Q.bUnread;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumGlobalEmailRecord& R)
{
	// One `GLOBAL_EMAIL`: the terminal's `targetname` and its 128 flag integers. The array is
	// re-squared on load so a hand-edited or truncated payload cannot hand a terminal a short one.
	Ar << R.Name;
	Ar << R.Flags;
	if (Ar.IsLoading())
	{
		R.Name = R.Name.Left(FElysiumGlobalEmailRecord::NameMax);
		R.Flags.SetNumZeroed(FElysiumGlobalEmailRecord::FlagCount);
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumLawState& L)
{
	Ar << L.Criminal << L.Supernatural << L.Investigate;
	// The two timed channels' deadlines and act counts. This operator is called MID-RECORD
	// (between the XP accumulators and `bUnkillable`), so the block is gated on its own version
	// rather than appended blindly: a `Stealth` payload skips these bytes entirely and restores
	// three bare levels with no deadline, which is exactly what it was written with.
	//
	// A restored level with no deadline would never age out, so the load installs the sentinel and
	// the first think leaves it alone — the levels then hold until something writes them, rather
	// than being silently expired on the first frame after a load.
	if (Ar.IsSaving() || Ar.CustomVer(FElysiumSaveVersion::GUID) >= FElysiumSaveVersion::Law)
	{
		Ar << L.CriminalExpiry << L.SupernaturalExpiry;
		Ar << L.CriminalCount << L.SupernaturalCount;
	}
	else if (Ar.IsLoading())
	{
		L.CriminalExpiry = -1.0;
		L.SupernaturalExpiry = -1.0;
		L.CriminalCount = 0;
		L.SupernaturalCount = 0;
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumPoliceState& P)
{
	Ar << P.MasqueradeTimerNext;
	Ar << P.bResponsePending << P.ResponseSeverity;
	Ar << P.ResponseWitness << P.ResponsePosition << P.ResponseDeadline;
	Ar << P.GraceUntil << P.GraceSpawned;
	Ar << P.CopsInPursuit << P.HuntersInPursuit;
	Ar << P.bHeightenedAlert << P.HeightenedAlertExpiry;
	if (Ar.IsLoading())
	{
		// A payload from another build must not be able to hand the response arithmetic a negative
		// severity or a negative pursuit count; the deadlines are absolute times and mean whatever
		// the restored clock says they mean.
		P.ResponseSeverity = FMath::Max(0, P.ResponseSeverity);
		P.GraceSpawned = FMath::Max(0, P.GraceSpawned);
		P.CopsInPursuit = FMath::Max(0, P.CopsInPursuit);
		P.HuntersInPursuit = FMath::Max(0, P.HuntersInPursuit);
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumPlayerRecord& R)
{
	Ar << R.Name;
	Ar << R.Sheet;
	Ar << R.Money;
	const int32 Version = Ar.CustomVer(FElysiumSaveVersion::GUID);
	if (Ar.IsSaving() || Version >= FElysiumSaveVersion::BodyIdentity)
	{
		Ar << R.ArmorSlot;
	}
	else if (Ar.IsLoading())
	{
		// Version 6 has no appearance identity; retail's first body slot is the restore default.
		R.ArmorSlot = 0;
	}
	if (Ar.IsLoading())
	{
		R.ArmorSlot = FMath::Clamp(R.ArmorSlot, 0, 5);
	}
	Ar << R.Health << R.MaxHealth;
	Ar << R.ExperienceLog << R.Effects;
	// `m_GlobalEmailFlags` — the per-terminal-name email flag records (§12). This slot previously
	// held an unwritten `TArray<FString>` placeholder; nothing ever filled it, so an older payload
	// reads and discards it and there is nothing to migrate.
	if (Version >= FElysiumSaveVersion::TerminalEmail)
	{
		Ar << R.GlobalEmail;
	}
	else
	{
		TArray<FString> LegacyEmailPlaceholder;
		Ar << LegacyEmailPlaceholder;
	}
	// The award accumulators travel with the ledger they belong to: the residue is real state (one
	// bonus XP per 100 awards falls out of it), so dropping it would quietly cost a point.
	Ar << R.ExperienceRemainder << R.LifetimeExperience;
	Ar << R.Law;
	Ar << R.bUnkillable;
	// The journal rides with the record because that is where m_QuestList lives; the quest map it
	// reflects is in the Session block, and only a state change writes both.
	Ar << R.Journal;
	// The selected hub tab is a datamap field on VtMB's player, so it persists with the character
	// rather than with the panel that reads it.
	Ar << R.QuestLogArea;
	// The History is the choice; the trait-effect group it names rides in `Effects` above, so a
	// patched `histories000.txt` re-applies on load exactly as a patched rulebook does.
	Ar << R.HistoryId;
	// An in-progress feed. Appended, and read behind its own version, so a payload written
	// before feeding existed simply restores with no feed rather than being refused.
	if (Ar.IsSaving() || Version >= FElysiumSaveVersion::Feeding)
	{
		Ar << R.FeedMap;
		Ar << R.Feed.NextPulse << R.Feed.Interval << R.Feed.StartTime;
		Ar << R.Feed.Target;
		Ar << R.Feed.BloodStolen;
		Ar << R.Feed.bContinuation;
		Ar << R.Feed.Peer;
		Ar << R.Feed.bVictim << R.Feed.bFrozenByFeed;
		uint8 Phase = static_cast<uint8>(R.Feed.Phase);
		Ar << Phase;
		Ar << R.Feed.PhaseDeadline;
		if (Ar.IsLoading())
		{
			R.Feed.Phase = static_cast<EElysiumFeedPhase>(
				FMath::Min<uint8>(Phase, static_cast<uint8>(EElysiumFeedPhase::ReleaseTail)));
			R.Feed.bInterrupting = false;   // a teardown never survives a save
		}
	}
	else if (Ar.IsLoading())
	{
		R.Feed = FElysiumFeedState();
		R.FeedMap.Reset();
	}
	// 13.2 — the discipline block. The player entity is excluded from the map snapshot, so its
	// half of the domain rides here: the selection, the cast counter, and what the sheet's own
	// `Active_*` slots cannot say — each owned expiry event's deadline and serial, the trait-effect
	// groups each activation installed, the tracked targeted effects and the recovery deadlines.
	// The events themselves ride the map snapshot's queue, which is why `DisciplineMap` scopes the
	// block: hydrating into any other map tears it down rather than leaving a slot with no event.
	// Appended behind its own version, so an older supported payload restores with no disciplines
	// rather than being refused.
	if (Ar.IsSaving() || Version >= FElysiumSaveVersion::Disciplines)
	{
		Ar << R.DisciplineMap;
		Ar << R.SelectedDiscipline << R.SelectedTier << R.DisciplineCastCount;
		// The field list lives on the state itself so the NPC leaf writes the
		// identical bytes; the prologue above stays here because it is player-record state, not
		// discipline state. The stream shape is byte-for-byte what this block already wrote.
		R.Disciplines.Serialize(Ar);
		if (Ar.IsLoading())
		{
			// The bus cursor is session state, not simulation state: a restored character starts
			// from the live bus rather than replaying a window that no longer exists.
			R.Disciplines.SoundCursor = 0;
		}
	}
	else if (Ar.IsLoading())
	{
		R.Disciplines.Reset();
		R.DisciplineMap.Reset();
		R.SelectedDiscipline = INDEX_NONE;
		R.SelectedTier = 0;
		R.DisciplineCastCount = 0;
	}
	// 13.1 — the stealth block. Appended to the END of the player record and read behind its own
	// version, so an older supported payload restores with a default surface rather than being
	// refused.
	//
	// It is written and read as ONE group and there is no partial arm: `docs/vtmb/stealth.md` is
	// explicit that a restored sample triplet must never be combined with newly defaulted derived
	// values, so a payload either carries the samples, the rotation index, all four derived values
	// AND the generation that ties them together, or it carries none of them and the surface
	// re-derives from scratch on the first think.
	if (Ar.IsSaving() || Version >= FElysiumSaveVersion::Stealth)
	{
		Ar << R.StealthMap;
		Ar << R.StealthModRaw;
		Ar << R.Stealth.NextUpdateTime;
		Ar << R.Stealth.VisionScalar << R.Stealth.ConeScalar << R.Stealth.HearingReductionCm;
		Ar << R.Stealth.NextSampleIndex;
		for (int32 i = 0; i < FElysiumStealthSurface::NumSamples; ++i)
		{
			Ar << R.Stealth.Samples[i];
		}
		Ar << R.Stealth.LightOnMe;
		Ar << R.Stealth.LightRow << R.Stealth.StealthRow;
		uint8 Eligible = R.Stealth.bEligible ? 1 : 0;
		Ar << Eligible;
		Ar << R.Stealth.Generation;
		if (Ar.IsLoading())
		{
			R.Stealth.bEligible = Eligible != 0;
			// A payload from another build must not be able to index off the end of a table or of
			// the sample triplet; the values themselves are floats and clamp at their own reads.
			R.Stealth.NextSampleIndex = FMath::Clamp(R.Stealth.NextSampleIndex, 0,
				FElysiumStealthSurface::NumSamples - 1);
			R.Stealth.LightRow = FMath::Clamp(R.Stealth.LightRow, 0, 10);
			R.Stealth.StealthRow = FMath::Clamp(R.Stealth.StealthRow, 0, 10);
			R.Stealth.Generation = FMath::Max(0, R.Stealth.Generation);
		}
	}
	else if (Ar.IsLoading())
	{
		R.Stealth.Reset();
		R.StealthModRaw = 0;
		R.StealthMap.Reset();
	}
	// The police-response / Masquerade-timer / pursuit block. Appended to the END of the
	// player record and read behind its own version, so an older supported payload restores with a
	// clean street rather than being refused.
	//
	// Deliberately NOT scoped by a map name the way `FeedMap`, `DisciplineMap` and `StealthMap` are:
	// every deadline in it is on the session clock, which persists across map travel. The one member
	// that names a map entity is `ResponseWitness`, and `FElysiumPlayer::Hydrate` rebases-or-drops
	// that single handle exactly as the feed's target handle is.
	if (Ar.IsSaving() || Version >= FElysiumSaveVersion::Law)
	{
		Ar << R.Police;
	}
	else if (Ar.IsLoading())
	{
		R.Police = FElysiumPoliceState();
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumEntityState& S)
{
	Ar << S.Index << S.ClassName << S.TargetName << S.Origin;
	Ar << S.bDead << S.bHidden << S.bSpawnCalled;
	if (Ar.IsSaving() || Ar.CustomVer(FElysiumSaveVersion::GUID) >= FElysiumSaveVersion::Activation)
	{
		Ar << S.bActivateCalled;
	}
	Ar << S.NextThink << S.SavedNextThink;
	Ar << S.OutputTimesRemaining;
	Ar << S.Fields;
	Ar << S.LeafState;
	Ar << S.bRuntime;
	if (S.bRuntime)
	{
		Ar << S.Def;
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumSavedFade& F)
{
	Ar << F.bActive << F.Color << F.MaxAlpha << F.Duration << F.HoldTime;
	Ar << F.bFadeIn << F.bAutoReverse << F.StartTime;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumMapSnapshot& M)
{
	Ar << M.MapName << M.DefCount << M.FrozenAt;
	Ar << M.Entities;
	Ar << M.AbsentEntities;
	Ar << M.Queue << M.QueueNextSerial;
	if (Ar.IsSaving() || Ar.CustomVer(FElysiumSaveVersion::GUID) >= FElysiumSaveVersion::EventClock)
	{
		Ar << M.QueueLastEnqueue;
	}
	Ar << M.Fade;
	if (Ar.IsSaving() || Ar.CustomVer(FElysiumSaveVersion::GUID) >= FElysiumSaveVersion::Weather)
	{
		Ar << M.Weather;
	}
	// The schema the snapshot's opaque leaf blobs were written at. It has to be recorded rather than
	// assumed, because a leaf archive is constructed from bytes and carries no version of its own —
	// see `FElysiumMapSnapshot::SchemaVersion`.
	//
	// A file older than this field predates the record, and every blob in it was written by the build
	// that wrote the file, so the file's own version IS the blobs' version. Reading it back that way
	// keeps every leaf gate from `NpcMaker` onwards aligned with the bytes the writer emitted,
	// instead of consuming as `Latest`.
	if (Ar.IsSaving() || Ar.CustomVer(FElysiumSaveVersion::GUID) >= FElysiumSaveVersion::WeaponAnimEvent)
	{
		Ar << M.SchemaVersion;
	}
	else
	{
		M.SchemaVersion = Ar.CustomVer(FElysiumSaveVersion::GUID);
	}
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumWeatherState& W)
{
	Ar << W.InitialWetness << W.CurrentWetness << W.TargetWetness;
	Ar << W.TransitionStart << W.TransitionDuration;
	Ar << W.WetnessFadeIn << W.WetnessFadeOut << W.WetnessFadeTarget;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumSessionBlock& S)
{
	Ar << S.ClockNow;
	Ar << S.Globals;
	Ar << S.Quests;
	Ar << S.RngSessionSeed << S.Rng;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumWorldBlock& W)
{
	Ar << W.CurrentMap << W.PlayerOrigin << W.PlayerYaw << W.bHasPlacement;
	Ar << W.VisitedMaps;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumSaveHeaderData& H)
{
	Ar << H.PayloadVersion << H.Map << H.Label << H.ClanName << H.Clan;
	Ar << H.PlaytimeSeconds << H.Timestamp << H.Kind;
	return Ar;
}

// The payload.

namespace ElysiumSave
{

void SerializePayload(FElysiumSaveArchive& Ar, FElysiumSavePayload& Payload)
{
	// The four blocks, in the order §3 lists them. Each is self-delimiting, so a version that adds
	// a fifth appends rather than interleaves.
	Ar << Payload.Session;
	Ar << Payload.Player;

	// `Maps` is written as a **name-sorted** array rather than straight out of the TMap: §8 wants two
	// saves of the same state to be byte-identical, and a hash map's iteration order is a property of
	// its insertion history, not of the state it holds.
	if (Ar.IsSaving())
	{
		TArray<FString> Names;
		Payload.Maps.GenerateKeyArray(Names);
		Names.Sort();
		int32 Count = Names.Num();
		Ar << Count;
		for (const FString& Name : Names)
		{
			FString Key = Name;
			Ar << Key;
			Ar << Payload.Maps[Name];
		}
	}
	else
	{
		int32 Count = 0;
		Ar << Count;
		Payload.Maps.Empty(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			FString Key;
			Ar << Key;
			FElysiumMapSnapshot Snap;
			Ar << Snap;
			Payload.Maps.Add(Key, MoveTemp(Snap));
		}
	}

	Ar << Payload.World;
}

bool Write(const FElysiumSavePayload& Payload, TArray<uint8>& OutBytes, FString& OutError)
{
	OutError.Reset();
	OutBytes.Reset();

	// 1. The blocks, into an uncompressed staging buffer.
	TArray<uint8> Blocks;
	{
		FMemoryWriter Inner(Blocks, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Inner, FElysiumSaveVersion::Latest);
		SerializePayload(Ar, const_cast<FElysiumSavePayload&>(Payload));
		if (Inner.IsError())
		{
			OutError = TEXT("payload serialization failed");
			return false;
		}
	}

	// 2. The prologue, uncompressed, so a reader can refuse before inflating anything.
	{
		FMemoryWriter Head(OutBytes, /*bIsPersistent*/ true);
		uint32 Magic = ElysiumSaveMagic;
		int32 Version = FElysiumSaveVersion::Latest;
		int32 Floor = FElysiumSaveVersion::MinSupported;
		int32 Uncompressed = Blocks.Num();
		Head << Magic << Version << Floor << Uncompressed;
	}

	// 3. One compressed stream over the whole block set. VtMB's per-section zlib exists to bound
	//    memory on a 2004 machine; one stream is simpler and smaller.
	TArray<uint8> Compressed;
	{
		FArchiveSaveCompressedProxy Comp(Compressed, GElysiumSaveCompressor);
		Comp.Serialize(Blocks.GetData(), Blocks.Num());
		Comp.Flush();
		if (Comp.IsError())
		{
			OutError = TEXT("payload compression failed");
			return false;
		}
	}
	OutBytes.Append(Compressed);
	return true;
}

int32 PeekVersion(const TArray<uint8>& Bytes)
{
	if (Bytes.Num() < static_cast<int32>(sizeof(uint32) + 3 * sizeof(int32)))
	{
		return 0;
	}
	FMemoryReader Head(Bytes);
	uint32 Magic = 0;
	int32 Version = 0;
	Head << Magic << Version;
	return (Magic == ElysiumSaveMagic) ? Version : 0;
}

bool Read(const TArray<uint8>& Bytes, FElysiumSavePayload& OutPayload, FString& OutError)
{
	OutError.Reset();
	OutPayload = FElysiumSavePayload();

	constexpr int32 PrologueSize = static_cast<int32>(sizeof(uint32) + 3 * sizeof(int32));
	if (Bytes.Num() < PrologueSize)
	{
		OutError = TEXT("not an Elysium payload (too short)");
		return false;
	}

	uint32 Magic = 0;
	int32 Version = 0;
	int32 Floor = 0;
	int32 Uncompressed = 0;
	{
		FMemoryReader Head(Bytes);
		Head << Magic << Version << Floor << Uncompressed;
	}
	if (Magic != ElysiumSaveMagic)
	{
		OutError = TEXT("not an Elysium payload (bad magic)");
		return false;
	}
	// Both directions are refused with a reason, never half-read (§2): a payload older than this
	// build's floor has no declared upgrade path, and one newer than this build was written by a
	// schema we do not have.
	if (Version < FElysiumSaveVersion::MinSupported)
	{
		OutError = FString::Printf(
			TEXT("save schema %d is below this build's supported floor (%d) and has no upgrade path"),
			Version, static_cast<int32>(FElysiumSaveVersion::MinSupported));
		return false;
	}
	if (Version > FElysiumSaveVersion::Latest)
	{
		OutError = FString::Printf(
			TEXT("save schema %d was written by a newer build (this one reads up to %d)"),
			Version, static_cast<int32>(FElysiumSaveVersion::Latest));
		return false;
	}

	TArray<uint8> Compressed(Bytes.GetData() + PrologueSize, Bytes.Num() - PrologueSize);
	TArray<uint8> Blocks;
	Blocks.SetNumUninitialized(Uncompressed);
	{
		FArchiveLoadCompressedProxy Decomp(Compressed, GElysiumSaveCompressor);
		if (Decomp.IsError())
		{
			OutError = TEXT("payload decompression failed");
			return false;
		}
		Decomp.Serialize(Blocks.GetData(), Blocks.Num());
		if (Decomp.IsError())
		{
			OutError = TEXT("payload decompression failed (truncated stream)");
			return false;
		}
	}

	FMemoryReader Inner(Blocks, /*bIsPersistent*/ true);
	Inner.SetCustomVersion(FElysiumSaveVersion::GUID, Version, TEXT("ElysiumSave"));
	FElysiumSaveArchive Ar(Inner, Version);
	SerializePayload(Ar, OutPayload);
	if (Inner.IsError())
	{
		OutError = TEXT("payload deserialization failed");
		OutPayload = FElysiumSavePayload();
		return false;
	}
	return true;
}

// The readable dump.

void Describe(const FElysiumSavePayload& Payload, TArray<FString>& OutLines)
{
	OutLines.Reset();

	OutLines.Add(FString::Printf(TEXT("session.clock = %.3f"), Payload.Session.ClockNow));
	OutLines.Add(FString::Printf(TEXT("session.rng.seed = %d"), Payload.Session.RngSessionSeed));
	for (int32 i = 0; i < Payload.Session.Rng.Num(); ++i)
	{
		OutLines.Add(FString::Printf(TEXT("session.rng.%s = %d (from %d)"),
			ElysiumRng::Name(static_cast<EElysiumRngStream>(i)),
			Payload.Session.Rng[i].Current, Payload.Session.Rng[i].Seed));
	}
	for (const TPair<FString, FElysiumVariant>& G : Payload.Session.Globals)
	{
		OutLines.Add(FString::Printf(TEXT("session.G.%s = %s"), *G.Key, *G.Value.Describe()));
	}
	for (const TPair<FString, int32>& Q : Payload.Session.Quests)
	{
		OutLines.Add(FString::Printf(TEXT("session.quest.%s = %d"), *Q.Key, Q.Value));
	}

	const FElysiumPlayerRecord& P = Payload.Player;
	OutLines.Add(FString::Printf(TEXT("player.name = %s"),
		P.Name.IsEmpty() ? TEXT("(unnamed)") : *P.Name));
	OutLines.Add(FString::Printf(TEXT("player.clan = %d (%s)"), P.Sheet.Clan(),
		FElysiumSheet::ClanName(P.Sheet.Clan())));
	OutLines.Add(FString::Printf(TEXT("player.male = %d"), P.Sheet.IsMale() ? 1 : 0));
	OutLines.Add(FString::Printf(TEXT("player.health = %d/%d"), P.Health, P.MaxHealth));
	OutLines.Add(FString::Printf(TEXT("player.money = %d"), P.Money));
	OutLines.Add(FString::Printf(TEXT("player.law = %d/%d/%d"),
		P.Law.Criminal, P.Law.Supernatural, P.Law.Investigate));
	// The deadlines and act counts beside the levels, then the response/pursuit block.
	OutLines.Add(FString::Printf(TEXT("player.law.expiry = %.3f/%.3f acts %d/%d"),
		P.Law.CriminalExpiry, P.Law.SupernaturalExpiry, P.Law.CriminalCount, P.Law.SupernaturalCount));
	OutLines.Add(FString::Printf(
		TEXT("player.police = response %d sev %d due %.3f, grace %d until %.3f, cops %d, hunters %d, "
			"alert %d until %.3f, masquerade window %.3f"),
		P.Police.bResponsePending ? 1 : 0, P.Police.ResponseSeverity, P.Police.ResponseDeadline,
		P.Police.GraceSpawned, P.Police.GraceUntil,
		P.Police.CopsInPursuit, P.Police.HuntersInPursuit,
		P.Police.bHeightenedAlert ? 1 : 0, P.Police.HeightenedAlertExpiry,
		P.Police.MasqueradeTimerNext));
	OutLines.Add(FString::Printf(TEXT("player.unkillable = %d"), P.bUnkillable ? 1 : 0));
	// The stealth group, as one line plus its aggregate: a diff that shows a moved scalar without a
	// moved generation is exactly the split-generation restore the block exists to prevent.
	OutLines.Add(FString::Printf(
		TEXT("player.stealth = light %.3f rows %d/%d sight %.2f cone %.2f hearing %.1fcm ")
		TEXT("(gen %d, next sample %d, map %s)"),
		P.Stealth.LightOnMe, P.Stealth.LightRow, P.Stealth.StealthRow, P.Stealth.VisionScalar,
		P.Stealth.ConeScalar, P.Stealth.HearingReductionCm, P.Stealth.Generation,
		P.Stealth.NextSampleIndex, P.StealthMap.IsEmpty() ? TEXT("(none)") : *P.StealthMap));
	OutLines.Add(FString::Printf(TEXT("player.stealth.modifier.raw = %d"), P.StealthModRaw));
	// The sheet, by slot. Only what is non-zero: VtMB's own writer omits the all-zero slots, and 148
	// rows a dump would drown the diff this exists to be read as.
	for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
	{
		const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
		{
			const int32 Base = P.Sheet.GetBase(Container, Slot.Index);
			const int32 Cur  = P.Sheet.GetCurrent(Container, Slot.Index);
			if (Base != 0 || Cur != 0)
			{
				OutLines.Add(FString::Printf(TEXT("player.sheet.%s.%s = %d/%d"),
					ElysiumTraitContainerName(Container), Slot.Datamap, Base, Cur));
			}
		}
	}
	for (const TPair<FName, int32>& S : P.Sheet.Extra)
	{
		OutLines.Add(FString::Printf(TEXT("player.sheet.extra.%s = %d"), *S.Key.ToString(), S.Value));
	}
	for (const FElysiumXpEntry& X : P.ExperienceLog)
	{
		OutLines.Add(FString::Printf(TEXT("player.xp += %s (%d)"), *X.Entry, X.Amount));
	}
	OutLines.Add(FString::Printf(TEXT("player.xp.lifetime = %.0f (%.0f pending)"),
		P.LifetimeExperience, P.ExperienceRemainder));
	for (const FString& E : P.Effects)    { OutLines.Add(FString::Printf(TEXT("player.effect %s"), *E)); }
	for (const FElysiumGlobalEmailRecord& E : P.GlobalEmail)
	{
		// Only the set flags: 128 zeroes per terminal would drown the diff.
		FString Set;
		for (int32 Index = 0; Index < E.Flags.Num(); ++Index)
		{
			if (E.Flags[Index] != 0)
			{
				Set += FString::Printf(TEXT(" %d=%d"), Index, E.Flags[Index]);
			}
		}
		OutLines.Add(FString::Printf(TEXT("player.email %s%s"), *E.Name,
			Set.IsEmpty() ? TEXT(" (all clear)") : *Set));
	}
	// The journal, as stored — assignment order, which is also `Order` order.
	for (const FElysiumAssignedQuest& Q : P.Journal)
	{
		OutLines.Add(FString::Printf(TEXT("player.quest.%s = state %d (table %d, quest %d, order %d%s)"),
			*Q.Title, Q.State, Q.Table, Q.Quest, Q.Order, Q.bUnread ? TEXT(", unread") : TEXT("")));
	}
	OutLines.Add(FString::Printf(TEXT("player.questlog.area = %d"), P.QuestLogArea));
	OutLines.Add(FString::Printf(TEXT("player.history = %d"), P.HistoryId));

	OutLines.Add(FString::Printf(TEXT("world.map = %s"), *Payload.World.CurrentMap));
	OutLines.Add(FString::Printf(TEXT("world.placement = %s yaw %.1f%s"),
		*Payload.World.PlayerOrigin.ToString(), Payload.World.PlayerYaw,
		Payload.World.bHasPlacement ? TEXT("") : TEXT(" (none)")));
	for (const FString& M : Payload.World.VisitedMaps)
	{
		OutLines.Add(FString::Printf(TEXT("world.visited %s"), *M));
	}

	// Maps in name order, so two dumps of the same run diff cleanly.
	TArray<FString> MapNames;
	Payload.Maps.GenerateKeyArray(MapNames);
	MapNames.Sort();
	for (const FString& Name : MapNames)
	{
		const FElysiumMapSnapshot& Snap = Payload.Maps[Name];
		OutLines.Add(FString::Printf(
			TEXT("map[%s] entities=%d defs=%d queue=%d absent=%d frozen=%.3f leafschema=%d"),
			*Name, Snap.Entities.Num(), Snap.DefCount, Snap.Queue.Num(), Snap.AbsentEntities.Num(),
			Snap.FrozenAt, Snap.SchemaVersion));
		if (Snap.Fade.bActive)
		{
			OutLines.Add(FString::Printf(TEXT("map[%s].fade a=%.2f dur=%.2f hold=%.2f start=%.3f"),
				*Name, Snap.Fade.MaxAlpha, Snap.Fade.Duration, Snap.Fade.HoldTime, Snap.Fade.StartTime));
		}
		for (int32 Idx : Snap.AbsentEntities)
		{
			OutLines.Add(FString::Printf(TEXT("map[%s].absent #%d"), *Name, Idx));
		}
		for (const FElysiumEntityState& S : Snap.Entities)
		{
			const FString Head = FString::Printf(TEXT("map[%s].#%d(%s)"),
				*Name, S.Index, *S.ClassName.ToString());
			if (S.bDead)    { OutLines.Add(Head + TEXT(".dead = 1")); }
			if (S.bHidden)  { OutLines.Add(Head + TEXT(".hidden = 1")); }
			if (S.bRuntime) { OutLines.Add(Head + TEXT(".runtime = 1")); }
			if (S.NextThink != ELYSIUM_NEVER_THINK)
			{
				OutLines.Add(Head + FString::Printf(TEXT(".nextthink = %.3f"), S.NextThink));
			}
			if (!S.TargetName.IsEmpty())
			{
				OutLines.Add(Head + FString::Printf(TEXT(".targetname = %s"), *S.TargetName));
			}
			for (const TPair<FName, FElysiumVariant>& F : S.Fields)
			{
				OutLines.Add(Head + FString::Printf(TEXT(".%s = %s"),
					*F.Key.ToString(), *F.Value.Describe()));
			}
			if (S.LeafState.Num() > 0)
			{
				OutLines.Add(Head + FString::Printf(TEXT(".leafstate = %d bytes"), S.LeafState.Num()));
			}
		}
		for (const FElysiumIOEvent& E : Snap.Queue)
		{
			OutLines.Add(FString::Printf(TEXT("map[%s].queue @%.3f %s.%s(%s)%s"),
				*Name, E.FireTime, *E.Target, *E.Input.ToString(), *E.Param.ToString(),
				E.PythonSrc.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" py:%s"), *E.PythonSrc)));
		}
	}
}

}   // namespace ElysiumSave
