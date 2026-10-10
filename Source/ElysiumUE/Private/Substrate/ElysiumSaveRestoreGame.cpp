#include "Substrate/ElysiumSaveRestoreGame.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumRetailSite.h"
#include "ElysiumSaveArchive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// The "Game" set (0x1072bae8) and its five handlers over the port's snapshot (L0-r029). The set and
// the slot protocol are `ElysiumSaveRestoreBlocks.cpp`; the handler bodies below are the port's own
// (retail's are unwalked: Open 10) and say so at each line.

namespace ElysiumSaveRestore
{
	namespace
	{
		FGameContext* GameContextOf(FSaveRestoreData* Data)
		{
			return Data ? static_cast<FGameContext*>(Data->Context) : nullptr;
		}

		// One row's bytes through the port's archive (named modernization: the datamap writer's
		// encoding, CSave::vfunc2 0x1019f930, is unrecovered).
		template <typename T>
		void GameRowToBytes(T& Row, int32 Version, double Base, TArray<uint8>& OutBytes)
		{
			OutBytes.Reset();
			FMemoryWriter Writer(OutBytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, Version, Base, Base);
			Ar << Row;
		}

		template <typename T>
		bool GameRowFromBytes(const TArray<uint8>& Bytes, int32 Version, double Base, T& OutRow)
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Reader, Version, Base, Base);
			Ar << OutRow;
			return !Reader.IsError();
		}

		// --- "Entities" (CEntitySaveRestoreBlockHandler 0x10477130, object 0x1072bb44) -----------------
		// Retail: header = the entity table (ETABLE: id, location, size, classname...), body = every
		// entity's field stream (`docs/vtmb/savegame_format.md` § `.HL1`). Port: header = the row table
		// (index, body location, body size), body = each `FElysiumEntityState` through the archive.
		class FEntitiesBlockHandler final : public IBlockHandler
		{
		public:
			struct FTableRow { int32 Index = INDEX_NONE; int32 Location = 0; int32 Size = 0; };

			virtual const char* GetBlockName() const override { return "Entities"; } // 0x1059317c

			virtual void PreSave(FSaveRestoreData* Arg) override
			{
				Context = GameContextOf(Arg);
				Table.Reset();
			}

			virtual void Save(FSave& S) override
			{
				// Slot 2 (retail 0x101a37c0, unwalked here): the rows back to back from the block's start;
				// the table remembers where each landed, for the header slot 3 writes next.
				Table.Reset();
				if (Context == nullptr || Context->Source == nullptr) return;
				const FElysiumMapSnapshot& Source = *Context->Source;
				const int32 BlockStart = S.Tell();
				TArray<uint8> Bytes;
				for (const FElysiumEntityState& Row : Source.Entities)
				{
					GameRowToBytes(const_cast<FElysiumEntityState&>(Row), Source.SchemaVersion, Source.SaveBase, Bytes);
					FTableRow& Entry = Table.AddDefaulted_GetRef();
					Entry.Index = Row.Index;
					Entry.Location = S.Tell() - BlockStart;
					Entry.Size = Bytes.Num();
					S.WriteData(Bytes.GetData(), Bytes.Num());
				}
			}

			virtual void WriteSaveHeaders(FSave& S) override
			{
				// Slot 3: the table -- dword count, then (index, location, size) per row.
				const int32 Count = Table.Num();
				S.WriteInt(&Count, 1);
				for (const FTableRow& Entry : Table)
				{
					const int32 Words[3] = { Entry.Index, Entry.Location, Entry.Size };
					S.WriteInt(Words, 3);
				}
			}

			virtual void PostSave() override { Table.Reset(); Context = nullptr; }

			virtual void ReadRestoreHeaders(FRestore& R) override
			{
				// Slot 6: the table back, from the set's seek to locHeader.
				Table.Reset();
				Context = GameContextOf(R.Data);
				const int32 Count = R.ReadInt();
				if (Count < 0 || R.Data == nullptr || Count > R.Data->Size / 12) { if (Context) Context->bRowDecodeFailed = true; return; }
				Table.AddDefaulted(Count);
				for (FTableRow& Entry : Table)
				{
					int32 Words[3] = { 0, 0, 0 };
					R.ReadInt(Words, 3, 0);
					Entry.Index = Words[0]; Entry.Location = Words[1]; Entry.Size = Words[2];
				}
			}

			virtual void Restore(FRestore& R, int32 /*P2*/, int32 /*P3*/) override
			{
				// Slot 7, from the set's seek to locBody: the rows through the table, then the port's
				// apply onto the world (`FElysiumEntityWorld::ApplyRestoredEntities`). p2 / p3 are
				// forwarded by the set and unread here (their meaning is unrecovered, Open 7).
				if (!ReadRows(R)) return;
				if (Context && Context->World && Context->Decoded)
				{
					Context->AppliedRows = Context->World->ApplyRestoredEntities(*Context->Decoded, Context->RestoreBase);
				}
			}

			virtual void PostRestore() override { Table.Reset(); Context = nullptr; }

			// The body read alone: the engine-side transition path (vfunc10) reads the entity data
			// itself after slot 20 (its loop is unrecovered); the port's stand-in reuses this reader.
			bool ReadRows(FRestore& R)
			{
				if (Context == nullptr || Context->Decoded == nullptr) return false;
				FElysiumMapSnapshot& Decoded = *Context->Decoded;
				const int32 BlockStart = R.Tell();
				Decoded.Entities.Reset();
				Decoded.Entities.Reserve(Table.Num());
				TArray<uint8> Bytes;
				for (const FTableRow& Entry : Table)
				{
					if (Entry.Size < 0 || R.Data == nullptr || Entry.Location < 0 || BlockStart + Entry.Location + Entry.Size > R.Data->Size)
					{
						Context->bRowDecodeFailed = true;
						return false;
					}
					R.Seek(BlockStart + Entry.Location);
					Bytes.SetNumUninitialized(Entry.Size);
					R.ReadData(Bytes.GetData(), Entry.Size);
					FElysiumEntityState& Row = Decoded.Entities.AddDefaulted_GetRef();
					if (!GameRowFromBytes(Bytes, Decoded.SchemaVersion, Decoded.SaveBase, Row) || Row.Index != Entry.Index)
					{
						Context->bRowDecodeFailed = true;
						return false;
					}
				}
				return true;
			}

			FGameContext* Context = nullptr;
			TArray<FTableRow> Table;
		};

		// --- "EventQueue" (CEQ_SaveRestoreBlockHandler 0x10454060, object 0x106e70a8) ------------------
		// Retail: the pending delayed I/O events. Port body: dword count, the queue's next serial and
		// its backward-clock guard word, then each `FElysiumIOEvent` as (dword size, bytes). No header.
		class FEventQueueBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "EventQueue"; } // 0x1055e5bc

			virtual void PreSave(FSaveRestoreData* Arg) override { Context = GameContextOf(Arg); }

			virtual void Save(FSave& S) override
			{
				if (Context == nullptr || Context->Source == nullptr) return;
				const FElysiumMapSnapshot& Source = *Context->Source;
				const int32 Count = Source.Queue.Num();
				S.WriteInt(&Count, 1);
				S.WriteData(&Source.QueueNextSerial, sizeof(uint64));
				S.WriteData(&Source.QueueLastEnqueue, sizeof(double));
				TArray<uint8> Bytes;
				for (const FElysiumIOEvent& Event : Source.Queue)
				{
					GameRowToBytes(const_cast<FElysiumIOEvent&>(Event), Source.SchemaVersion, Source.SaveBase, Bytes);
					const int32 Size = Bytes.Num();
					S.WriteInt(&Size, 1);
					S.WriteData(Bytes.GetData(), Size);
				}
			}

			virtual void PostSave() override { Context = nullptr; }

			virtual void ReadRestoreHeaders(FRestore& R) override { Context = GameContextOf(R.Data); }

			virtual void Restore(FRestore& R, int32 /*P2*/, int32 /*P3*/) override
			{
				if (!ReadRows(R)) return;
				if (Context && Context->World && Context->Decoded)
				{
					Context->World->ApplyRestoredQueue(*Context->Decoded, Context->RestoreBase);
				}
			}

			virtual void PostRestore() override { Context = nullptr; }

			bool ReadRows(FRestore& R)
			{
				if (Context == nullptr || Context->Decoded == nullptr) return false;
				FElysiumMapSnapshot& Decoded = *Context->Decoded;
				const int32 Count = R.ReadInt();
				if (Count < 0 || R.Data == nullptr || Count > R.Data->Size / 4) { Context->bRowDecodeFailed = true; return false; }
				R.ReadData(&Decoded.QueueNextSerial, sizeof(uint64));
				R.ReadData(&Decoded.QueueLastEnqueue, sizeof(double));
				Decoded.Queue.Reset();
				Decoded.Queue.Reserve(Count);
				TArray<uint8> Bytes;
				for (int32 I = 0; I < Count; ++I)
				{
					const int32 Size = R.ReadInt();
					if (Size < 0 || Size > R.Data->Size - R.Tell()) { Context->bRowDecodeFailed = true; return false; }
					Bytes.SetNumUninitialized(Size);
					R.ReadData(Bytes.GetData(), Size);
					FElysiumIOEvent& Event = Decoded.Queue.AddDefaulted_GetRef();
					if (!GameRowFromBytes(Bytes, Decoded.SchemaVersion, Decoded.SaveBase, Event)) { Context->bRowDecodeFailed = true; return false; }
				}
				return true;
			}

			FGameContext* Context = nullptr;
		};

		// --- "Physics" (CPhysSaveRestoreBlockHandler 0x1044943c, object 0x106bda60) --------------------
		// VPhysics object state keyed by entity and field. No port body: decisions.md D6 leaves the
		// physics serializer to the owner (the base slot answers `ShouldSavePhysics == false`). The
		// handler is registered so the dispatch order is retail's; every slot is the default no-op.
		class FPhysicsBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "Physics"; } // 0x10539b54
		};

		// --- "AI" (CAI_SaveRestoreBlockHandler 0x1049dfb0, object 0x10936b5c) -------------------------
		// L4 upward hook: NPC memory, patrol paths, interesting-place markers. Registered here (the L0
		// registration), bodies owned by L4 (`hooks.tsv` carries no row for 0x1011a0c0 -> 0x1030c390 yet).
		class FAiBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "AI"; } // 0x106129e0
		};

		// --- "Python" (CPython_SaveRestoreBlockHandler 0x10476ac8, object 0x1072b354) ------------------
		// L5 upward hook: the pickled script namespaces (Save / Restore 0x1019adc0 / 0x1019b130). The
		// port's story state lives in the session block today; the handler is registered so the
		// Python block's slot order is retail's when L5 fills it.
		class FPythonBlockHandler final : public IBlockHandler
		{
		public:
			virtual const char* GetBlockName() const override { return "Python"; } // 0x1055e570
		};

		FEntitiesBlockHandler& GameEntitiesHandler() { static FEntitiesBlockHandler H; return H; }
		FEventQueueBlockHandler& GameEventQueueHandler() { static FEventQueueBlockHandler H; return H; }
		FPhysicsBlockHandler& GamePhysicsHandler() { static FPhysicsBlockHandler H; return H; }
		FAiBlockHandler& GameAiHandler() { static FAiBlockHandler H; return H; }
		FPythonBlockHandler& GamePythonHandler() { static FPythonBlockHandler H; return H; }
	}

	FBlockSet& GameBlockSet()
	{
		// The static set (ctor FUN_101a47b0 at static init, "Game") with the five `AddBlockHandler`
		// calls of DLLInit (0x1011a0c0: asm 0x1011a21e, 0x1011a235, 0x1011a24c, 0x1011a262, 0x1011a279).
		static FBlockSet Set("Game");
		static bool bRegistered = false;
		if (!bRegistered)
		{
			bRegistered = true;
			Set.AddBlockHandler(&GameEntitiesHandler());
			Set.AddBlockHandler(&GameEventQueueHandler());
			Set.AddBlockHandler(&GamePhysicsHandler());
			Set.AddBlockHandler(&GameAiHandler());
			Set.AddBlockHandler(&GamePythonHandler());
		}
		return Set;
	}

	void EncodeMapBlocks(FElysiumMapSnapshot& Snapshot, IElysiumRetailSiteSink* Sites)
	{
		FGameContext Context;
		Context.Source = &Snapshot;
		FSaveRestoreData Data;
		Data.Reset();
		Data.Sites = Sites;
		Data.Context = &Context;
		int32 HeaderStart = 0;
		EngineSaveGameState(GameBlockSet(), Data, HeaderStart); // vfunc13 0x20096470: 17, 18, 19
		Snapshot.BlockStream = MoveTemp(Data.Bytes);
		Snapshot.BlockHeaderStart = HeaderStart;
	}

	namespace
	{
		// The snapshot's scalars the handler bodies read while decoding (the section's own words,
		// outside the block set), copied before the chain runs.
		void GameCopyScalars(const FElysiumMapSnapshot& From, FElysiumMapSnapshot& To)
		{
			To.MapName = From.MapName;
			To.DefCount = From.DefCount;
			To.SaveBase = From.SaveBase;
			To.FrozenAt = From.FrozenAt;
			To.SchemaVersion = From.SchemaVersion;
			To.AbsentEntities = From.AbsentEntities;
			To.Fade = From.Fade;
			To.Weather = From.Weather;
			To.ComfortTargets = From.ComfortTargets;
			To.BlockHeaderStart = From.BlockHeaderStart;
		}

		void GameLoadStream(const FElysiumMapSnapshot& Snapshot, FSaveRestoreData& Data)
		{
			Data.Reset();
			Data.Bytes = Snapshot.BlockStream;
			Data.Size = Data.Bytes.Num();
			Data.bGrowable = false; // the file's buffer: reads only
		}
	}

	bool DecodeMapBlocks(FElysiumMapSnapshot& Snapshot)
	{
		FElysiumMapSnapshot Decoded;
		GameCopyScalars(Snapshot, Decoded);
		FGameContext Context;
		Context.Decoded = &Decoded;
		FSaveRestoreData Data;
		GameLoadStream(Snapshot, Data);
		Data.Context = &Context;
		EngineLoadGameState(GameBlockSet(), Data, Snapshot.BlockHeaderStart, 0, 0, 0); // vfunc9 0x200975f0: 20, 21; no world
		if (Context.bRowDecodeFailed || Data.bReadPastEnd) return false;
		Snapshot.Entities = MoveTemp(Decoded.Entities);
		Snapshot.Queue = MoveTemp(Decoded.Queue);
		Snapshot.QueueNextSerial = Decoded.QueueNextSerial;
		Snapshot.QueueLastEnqueue = Decoded.QueueLastEnqueue;
		return true;
	}

	int32 RestoreMapBlocks(const FElysiumMapSnapshot& Snapshot, FElysiumEntityWorld& World, FElysiumMapSnapshot& Decoded,
		double RestoreBase, bool bLevelTransition, IElysiumRetailSiteSink* Sites)
	{
		GameCopyScalars(Snapshot, Decoded);
		FGameContext Context;
		Context.Decoded = &Decoded;
		Context.World = &World;
		Context.RestoreBase = RestoreBase;
		FSaveRestoreData Data;
		int32 HeaderStart = Snapshot.BlockHeaderStart;
		if (Snapshot.BlockStream.IsEmpty())
		{
			// A snapshot built in memory and never frozen (a harness envelope, a test row): the engine
			// hands the set a stream, so one is encoded from the arrays first -- the same chain
			// `Freeze` ran, with no world to observe it.
			FElysiumMapSnapshot Encoded;
			GameCopyScalars(Snapshot, Encoded);
			Encoded.Entities = Snapshot.Entities;
			Encoded.Queue = Snapshot.Queue;
			Encoded.QueueNextSerial = Snapshot.QueueNextSerial;
			Encoded.QueueLastEnqueue = Snapshot.QueueLastEnqueue;
			EncodeMapBlocks(Encoded, nullptr);
			Data.Reset();
			Data.Bytes = MoveTemp(Encoded.BlockStream);
			Data.Size = Data.Bytes.Num();
			Data.bGrowable = false;
			HeaderStart = Encoded.BlockHeaderStart;
		}
		else
		{
			GameLoadStream(Snapshot, Data);
		}
		Data.Sites = Sites;
		Data.Context = &Context;
		FBlockSet& Set = GameBlockSet();
		if (!bLevelTransition)
		{
			// vfunc9 0x200975f0: slot 20 then slot 21 (p2 / p3: the engine's two dwords, unrecovered
			// meaning; the port passes 0, 0 and the set forwards them to every handler's slot 7).
			EngineLoadGameState(Set, Data, HeaderStart, 0, 0, 0);
		}
		else
		{
			// vfunc10 0x20097d00: slot 20 only. The engine then reads the entity data itself (its loop is
			// unrecovered); the port's stand-in reads the rows through the Entities handler's table and
			// the queue through the EventQueue body, then applies them as the ordinary path does. No set
			// slot 8 runs: the header vector keeps its records until the next slot 1 / 6 / 8.
			EngineLevelTransition(Set, Data, HeaderStart);
			const TArray<FBlockHeader>& Records = Set.HeaderRecords();
			FRestore R(&Data);
			R.Seek(0);
			const int32 T0 = R.Tell();
			auto FindRecord = [&Records](const char* Name) -> const FBlockHeader*
			{
				for (const FBlockHeader& Record : Records) if (FCStringAnsi::Strcmp(Record.szName, Name) == 0) return &Record;
				return nullptr;
			};
			if (const FBlockHeader* Entities = FindRecord("Entities"); Entities && Entities->locBody != -1)
			{
				R.Seek(T0 + Entities->locBody);
				if (GameEntitiesHandler().ReadRows(R)) Context.AppliedRows = World.ApplyRestoredEntities(Decoded, RestoreBase);
			}
			if (const FBlockHeader* Queue = FindRecord("EventQueue"); Queue && Queue->locBody != -1)
			{
				R.Seek(T0 + Queue->locBody);
				GameEventQueueHandler().Context = &Context;
				if (GameEventQueueHandler().ReadRows(R)) World.ApplyRestoredQueue(Decoded, RestoreBase);
				GameEventQueueHandler().Context = nullptr;
			}
			GameEntitiesHandler().Context = nullptr;
		}
		if (Context.bRowDecodeFailed || Data.bReadPastEnd) return INDEX_NONE;
		return Context.AppliedRows;
	}
}
