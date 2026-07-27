#include "ElysiumSaveArchive.h"

#include "Serialization/ArchiveLoadCompressedProxy.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/MemoryWriter.h"

// The payload's schema id, registered as an engine custom version so a nested engine serializer
// sees it on the archive (`save-architecture.md` §2). The number is also written into the prologue,
// because a raw memory archive carries no custom-version container of its own.
const FGuid FElysiumSaveVersion::GUID(0x45'4C'59'53, 0x53'41'56'45, 0x31'31'2E'39, 0x50'4C'44'00);

namespace
{
	FCustomVersionRegistration GRegisterElysiumSaveVersion(
		FElysiumSaveVersion::GUID, FElysiumSaveVersion::Latest, TEXT("ElysiumSave"));

	// The compressor. Oodle is the engine default and is available in every configuration.
	const FName GElysiumSaveCompressor = NAME_Oodle;
}

// ================================================================================================
// Value types
// ================================================================================================

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

// ================================================================================================
// Blocks
// ================================================================================================

FArchive& operator<<(FArchive& Ar, FElysiumSheet& S)
{
	Ar << S.Clan << S.bMale << S.Stats;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumXpEntry& E)
{
	Ar << E.Entry << E.Amount;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumLawState& L)
{
	Ar << L.Criminal << L.Supernatural << L.Investigate;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumPlayerRecord& R)
{
	Ar << R.Sheet;
	Ar << R.Money << R.Humanity << R.BloodPool << R.Masquerade;
	Ar << R.Health << R.MaxHealth;
	Ar << R.ExperienceLog << R.Effects << R.EmailFlags;
	Ar << R.Law;
	Ar << R.bUnkillable;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FElysiumEntityState& S)
{
	Ar << S.Index << S.ClassName << S.TargetName;
	Ar << S.bDead << S.bHidden << S.bSpawnCalled;
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
	Ar << M.Fade;
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

// ================================================================================================
// The payload
// ================================================================================================

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

// --- The readable dump ---------------------------------------------------------------------------

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
	OutLines.Add(FString::Printf(TEXT("player.clan = %d (%s)"), P.Sheet.Clan,
		FElysiumSheet::ClanName(P.Sheet.Clan)));
	OutLines.Add(FString::Printf(TEXT("player.male = %d"), P.Sheet.bMale ? 1 : 0));
	OutLines.Add(FString::Printf(TEXT("player.health = %d/%d"), P.Health, P.MaxHealth));
	OutLines.Add(FString::Printf(TEXT("player.money = %d"), P.Money));
	OutLines.Add(FString::Printf(TEXT("player.humanity = %d"), P.Humanity));
	OutLines.Add(FString::Printf(TEXT("player.blood = %d"), P.BloodPool));
	OutLines.Add(FString::Printf(TEXT("player.masquerade = %d"), P.Masquerade));
	OutLines.Add(FString::Printf(TEXT("player.law = %d/%d/%d"),
		P.Law.Criminal, P.Law.Supernatural, P.Law.Investigate));
	OutLines.Add(FString::Printf(TEXT("player.unkillable = %d"), P.bUnkillable ? 1 : 0));
	for (const TPair<FName, int32>& S : P.Sheet.Stats)
	{
		OutLines.Add(FString::Printf(TEXT("player.stat.%s = %d"), *S.Key.ToString(), S.Value));
	}
	for (const FElysiumXpEntry& X : P.ExperienceLog)
	{
		OutLines.Add(FString::Printf(TEXT("player.xp += %s (%d)"), *X.Entry, X.Amount));
	}
	for (const FString& E : P.Effects)    { OutLines.Add(FString::Printf(TEXT("player.effect %s"), *E)); }
	for (const FString& E : P.EmailFlags) { OutLines.Add(FString::Printf(TEXT("player.email %s"), *E)); }

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
		OutLines.Add(FString::Printf(TEXT("map[%s] entities=%d defs=%d queue=%d absent=%d frozen=%.3f"),
			*Name, Snap.Entities.Num(), Snap.DefCount, Snap.Queue.Num(), Snap.AbsentEntities.Num(),
			Snap.FrozenAt));
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
