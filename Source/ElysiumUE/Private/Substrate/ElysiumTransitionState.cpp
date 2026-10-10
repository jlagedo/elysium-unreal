#include "ElysiumTransitionState.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRetailSite.h"
#include "ElysiumSaveTypes.h"
#include "Substrate/ElysiumSaveRestoreGame.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumTransition, Log, All);

// Map-transition save state (L0-r030, `walks/L0-r030.md`). Every body below was read (`vtmb_code` /
// `vtmb_asm`) before it was written; the address at each line is the retail body it reproduces.
// Helper names carry the `TransitionState` prefix: the unity build shares one anonymous namespace
// across Substrate/*.cpp.

namespace ElysiumTransitionState
{
	namespace
	{
		using ElysiumSaveRestore::FAdjacencyRow;
		using ElysiumSaveRestore::FEntityTableRow;

		const TCHAR* GGlobalSaveFn = TEXT("Global::FUN_10057850");
		const TCHAR* GGlobalRestoreFn = TEXT("Global::FUN_100578e0");
		const TCHAR* GAddGlobalFn = TEXT("FUN_10057740");
		const TCHAR* GBuildListFn = TEXT("FUN_101c7ff0");
		const TCHAR* GRowsFn = TEXT("FUN_101a3c40");

		void TransitionStateSite(IElysiumRetailSiteSink* Sites, const TCHAR* Tag, const TCHAR* Fn, uint32 Va,
			const TCHAR* Phase, const FString& Payload)
		{
			if (Sites != nullptr) Sites->Site(Tag, Fn, Va, Phase, Payload);
		}

		// `Q_strncpy` (a VSTDLIB import): n-1 characters and a terminator.
		void TransitionStateStrncpy(char* Dst, const char* Src, int32 Bytes)
		{
			FMemory::Memzero(Dst, Bytes);
			FCStringAnsi::Strncpy(Dst, Src ? Src : "", Bytes);
		}

		FString TransitionStateText(const char* Ansi)
		{
			return FString(ANSI_TO_TCHAR(Ansi ? Ansi : ""));
		}

		FString TransitionStateEntityText(const FElysiumEntity& E)
		{
			if (!E.TargetName.IsEmpty()) return E.TargetName;
			return E.Def ? E.Def->Classname : FString(TEXT("<unnamed>"));
		}

		const TCHAR* TransitionStateOrNull(const char* S)
		{
			return S ? TEXT("set") : TEXT("NULL");
		}

		// The entity at a table row's edict index in `World` (the port's one index space).
		FElysiumEntity* TransitionStateEntityAt(FElysiumEntityWorld& World, int32 Index)
		{
			const TArray<TUniquePtr<FElysiumEntity>>& List = World.Entities();
			if (!List.IsValidIndex(Index) || !List[Index]) return nullptr;
			FElysiumEntity* E = List[Index].Get();
			return E->IsDead() ? nullptr : E;
		}

		// `CBaseCombatCharacter::Inventory_Find(classname)`: the first carried item of that classname.
		// An L2 call from L0 (`hooks.tsv` carries no row: reported as a planning fault).
		FElysiumEntity* TransitionStateInventoryFind(FElysiumEntityWorld& World, const FElysiumPlayer& Player, const FString& Classname)
		{
			for (const FElysiumEntityHandle& Slot : Player.Inventory.Slots)
			{
				FElysiumEntity* Item = World.Resolve(Slot);
				if (Item && Item->Def && Item->Def->Classname.Equals(Classname, ESearchCase::IgnoreCase)) return Item;
			}
			return nullptr;
		}

		bool TransitionStateInventoryHolds(FElysiumEntityWorld& World, const FElysiumPlayer& Player, const FElysiumEntity& E)
		{
			for (const FElysiumEntityHandle& Slot : Player.Inventory.Slots)
			{
				if (World.Resolve(Slot) == &E) return true;
			}
			return false;
		}

		// The two CChangeLevel words through the registry (datamap `m_szMapName` +0x598 key `map`,
		// `m_szLandmarkName` +0x5b8 key `landmark`).
		FString TransitionStateReadField(const FElysiumEntity& E, const TCHAR* Key)
		{
			if (E.Class == nullptr) return FString();
			const FElysiumFieldAccessor* Acc = FElysiumClassRegistry::Get().FindField(*E.Class, FName(Key));
			return Acc && Acc->Get ? Acc->Get(E).ToString() : FString();
		}

		void TransitionStateWriteField(FElysiumEntity& E, const TCHAR* Key, const FString& Value)
		{
			if (E.Class == nullptr) return;
			const FElysiumFieldAccessor* Acc = FElysiumClassRegistry::Get().FindField(*E.Class, FName(Key));
			if (Acc && Acc->Set) Acc->Set(E, FElysiumVariant::String(Value));
		}

		// FUN_101c7690 (FindLandmark; audit row `L0.entity_core.landmark-lookup`): FindByName from the
		// start, the first whose classname `__strcmpi`s "info_landmark" (0x10559c18). On a miss with an
		// entity: `DevWarning("%s can't find landmark %s")` and, under the `developer` ConVar, the
		// trigger's virtual +0x10c and `m_debugOverlays |= 2` -- the developer arm is that story's, not
		// this one's. The port's lookup is `FElysiumEntityWorld::FindLandmark` (exact name).
		FElysiumEntity* TransitionStateFindLandmark(FElysiumEntityWorld& World, const FString& Name, const FElysiumEntity* Trigger)
		{
			if (!Name.IsEmpty())
			{
				if (FElysiumEntity* Landmark = World.FindLandmark(Name)) return Landmark;
			}
			if (Trigger != nullptr)
			{
				UE_LOG(LogElysiumTransition, Warning, TEXT("%s can't find landmark %s"), *TransitionStateEntityText(*Trigger), *Name); // 0x1059f99c
				return nullptr;
			}
			UE_LOG(LogElysiumTransition, Warning, TEXT("Can't find landmark %s"), *Name); // 0x1059f9bc
			return nullptr;
		}

		// FUN_101c7e00 (`__cdecl`, the ADJACENCY row writer): 0 when rows / map / landmark / edict is
		// NULL; 0 when a row k < i already pairs this landmark edict with this map (`__strcmpi`);
		// otherwise `Q_strncpy` the two names (0x20 each), `+0x40 := edict`, `+0x44.. := ` the
		// landmark's slot-220 origin (virtual +0x370 through edict+0x40's object); returns 1.
		int32 TransitionStateAddTransitionToList(FSaveRestoreData& Data, int32 Count, const char* MapName,
			const char* LandmarkName, const FElysiumEntity* Landmark)
		{
			if (MapName == nullptr || LandmarkName == nullptr || Landmark == nullptr) return 0;
			for (int32 K = 0; K < Count && K < Data.Adjacency.Num(); ++K)
			{
				const FAdjacencyRow& Row = Data.Adjacency[K];
				if (Row.Landmark == Landmark->Handle && FCStringAnsi::Stricmp(Row.MapName, MapName) == 0)
				{
					TransitionStateSite(Data.Sites, TEXT("adjacent_row"), TEXT("FUN_101c7e00"), 0x101c7e00u, TEXT("dup"),
						FString::Printf(TEXT("row=%d map=%s landmark=%s pent=#%d"), K, *TransitionStateText(MapName),
							*TransitionStateText(LandmarkName), Landmark->Handle.Index));
					return 0;
				}
			}
			if (Data.Adjacency.Num() <= Count) Data.Adjacency.SetNum(Count + 1);
			FAdjacencyRow& Row = Data.Adjacency[Count];
			TransitionStateStrncpy(Row.MapName, MapName, 0x20);
			TransitionStateStrncpy(Row.LandmarkName, LandmarkName, 0x20);
			Row.Landmark = Landmark->Handle;
			Row.LandmarkOrigin = Landmark->LocalOriginWord(); // slot 220 `GetOrigin`
			TransitionStateSite(Data.Sites, TEXT("adjacent_row"), TEXT("FUN_101c7e00"), 0x101c7e00u, TEXT("write"),
				FString::Printf(TEXT("row=%d map=%s landmark=%s pent=#%d origin=%g,%g,%g"), Count, *TransitionStateText(Row.MapName),
					*TransitionStateText(Row.LandmarkName), Landmark->Handle.Index, Row.LandmarkOrigin.X, Row.LandmarkOrigin.Y, Row.LandmarkOrigin.Z));
			return 1;
		}

		// FUN_101a08f0: the table row index of an entity's handle, or -1.
		int32 TransitionStateRowOf(const FSaveRestoreData& Data, const FElysiumEntity& E)
		{
			for (int32 I = 0; I < Data.EntityTable.Num(); ++I)
			{
				if (Data.EntityTable[I].EdictIndex == E.Handle.Index) return I;
			}
			return -1;
		}

		// FUN_101a0980: OR the two words into row `idx` when `0 <= idx <= count`. The `<=` admits
		// `idx == count`, one row past the table (a retail overrun the port cannot write).
		void TransitionStateOrRowFlags(FSaveRestoreData& Data, int32 Idx, uint32 Lo, uint32 Hi)
		{
			if (Idx < 0 || Idx > Data.EntityTable.Num()) return;
			if (Idx == Data.EntityTable.Num()) return; // retail writes past the allocation here; nothing to land on
			Data.EntityTable[Idx].FlagsLo |= Lo;
			Data.EntityTable[Idx].FlagsHi |= Hi;
		}

		// The transition mask bit for row j: `MOV EAX,1; SHL EAX,CL; CDQ` (asm 0x101c8560-0x101c8573 and
		// the engine's 0x20098130-0x20098141): a 32-bit shift by `j & 31`, sign-extended into the high
		// word -- transition 31 sets bit 31 and every high bit, transitions >= 32 alias bits 0..27.
		void TransitionStateMaskBit(int32 J, uint32& OutLo, uint32& OutHi)
		{
			const int32 Low = static_cast<int32>(1u << (J & 31));
			OutLo = static_cast<uint32>(Low);
			OutHi = Low < 0 ? 0xffffffffu : 0u;
		}

		// FUN_101c7fa0: NULL -> 0; `ObjectCaps() & 0x100` clear -> virtual +0x178 once, twice when the
		// first answer is not 0xb (effect unrecovered); returns 1 for every non-NULL entity.
		int32 TransitionStateScreen(const FElysiumEntity* E)
		{
			return E != nullptr ? 1 : 0;
		}

		// FUN_101c7ff0 (`__cdecl`, about 1.6 KB; the corpus record stops at its first RET):
		// `(rows, cap, oldLevel, landmarkName)`. Returns the row count.
		int32 TransitionStateBuildChangeList(FSaveRestoreData& Data, int32 Capacity, const char* OldLevel, const char* LandmarkName)
		{
			Data.Adjacency.Reset();
			int32 Count = 0;
			FElysiumEntityWorld* World = Data.World;
			if (World == nullptr) return Count;

			// First loop: `FindByClassname(&gEntList, prev, "trigger_changelevel")` (FUN_100f7380), each
			// `__RTDynamicCast` to CChangeLevel (0x1055ef50; a failed cast is skipped).
			const TArray<TUniquePtr<FElysiumEntity>>& List = World->Entities();
			for (int32 Index = 0; Index < List.Num(); ++Index)
			{
				FElysiumEntity* Trigger = List[Index].Get();
				if (Trigger == nullptr || Trigger->IsDead() || Trigger->Def == nullptr) continue;
				if (!Trigger->Def->Classname.Equals(TEXT("trigger_changelevel"), ESearchCase::IgnoreCase)) continue;
				FString MapName = TransitionStateReadField(*Trigger, TEXT("map"));           // m_szMapName +0x598
				const FString TriggerLandmark = TransitionStateReadField(*Trigger, TEXT("landmark")); // m_szLandmarkName +0x5b8

				// `lm = FUN_101c7690(trigger+0x5b8, trigger)`.
				FElysiumEntity* Landmark = TransitionStateFindLandmark(*World, TriggerLandmark, Trigger);

				// The special case (asm 0x101c8060-0x101c80fb), only with a landmark argument: when it and
				// the trigger's own landmark are both "taxi_landmark" (0x1059fa38) and oldLevel is set,
				// `Q_strncpy(trigger+0x598, oldLevel, 0x20)`; the same for "sewer_map_landmark" (0x1059fa48).
				// The rewrite persists on the entity: the hub's placeholder destination becomes the map
				// being left, so its row names the level the carried entities come from.
				if (LandmarkName != nullptr && OldLevel != nullptr)
				{
					for (const char* Special : { "taxi_landmark", "sewer_map_landmark" })
					{
						const auto TriggerLandmarkAnsi = StringCast<ANSICHAR>(*TriggerLandmark);
						if (FCStringAnsi::Stricmp(LandmarkName, Special) == 0 && FCStringAnsi::Stricmp(TriggerLandmarkAnsi.Get(), Special) == 0)
						{
							char Rewritten[0x20];
							TransitionStateStrncpy(Rewritten, OldLevel, 0x20);
							const FString Before = MapName;
							MapName = TransitionStateText(Rewritten);
							TransitionStateWriteField(*Trigger, TEXT("map"), MapName);
							TransitionStateSite(Data.Sites, TEXT("adjacent_rewrite"), GBuildListFn, 0x101c7ff0u, TEXT("write"),
								FString::Printf(TEXT("trigger=%s landmark=%s m_szMapName=%s -> %s"), *TransitionStateEntityText(*Trigger),
									*TriggerLandmark, *Before, *MapName));
						}
					}
				}

				// The row write; `i` advances only on a non-zero return.
				const auto MapAnsi = StringCast<ANSICHAR>(*MapName);
				const auto LandmarkAnsi = StringCast<ANSICHAR>(*TriggerLandmark);
				if (TransitionStateAddTransitionToList(Data, Count, MapAnsi.Get(), LandmarkAnsi.Get(), Landmark) != 0)
				{
					++Count;
					if (Count >= Capacity)
					{
						// `Error("Too many level transitions on this map, possible corruption.")` (0x1059fb84) is
						// fatal in retail; the port stops the walk and reports.
						UE_LOG(LogElysiumTransition, Error, TEXT("Too many level transitions on this map, possible corruption."));
						break;
					}
				}
			}

			// Second pass (asm 0x101c8164 on), only when the save data's entity table (`+0x1330`) is
			// non-empty: for each transition j, every entity is classified and the candidates' rows get
			// bit j. `pSaveData` is `*(pGlobals+0x20)`: here this save data.
			if (Data.EntityTable.Num() == 0) return Count;
			const FElysiumPlayer* Player = World->FindPlayer(); // engine slot 0x98 PEntityOfEntIndex(1), then Instance
			for (int32 J = 0; J < Count; ++J)
			{
				struct FCandidate { FElysiumEntity* Entity; uint32 Lo; uint32 Hi; };
				TArray<FCandidate> Candidates;
				for (int32 Index = 0; Index < List.Num(); ++Index)
				{
					FElysiumEntity* E = List[Index].Get();
					if (E == nullptr || E->IsDead()) continue;
					const int32 Caps = E->ObjectCaps();                 // virtual +0x1d4
					if (Caps < 0) continue;                             // FCAP_DONT_SAVE: "DON'T SAVE %s"
					uint32 Lo = 0, Hi = 0;
					if ((Caps & ElysiumEntityCaps::AcrossTransition) != 0)
					{
						// FENTTABLE_MOVEABLE: one of `ent+0xa8 != 0`, `ent+0xa0`'s object having `+0xa8 != 0`,
						// the player's `+0x1c64` handle being this entity, or this entity in the player's
						// `+0x14c0` list (count `+0x14cc`). The two `+0xa8` arms are UNRECOVERED (walk Open 6);
						// the player arms are its carried set: the active weapon and the inventory slots.
						const bool bPlayerCarries = Player != nullptr
							&& (World->Resolve(Player->Inventory.ActiveWeapon) == E || TransitionStateInventoryHolds(*World, *Player, *E));
						if (bPlayerCarries) Hi |= EntTableMoveable;
					}
					// FENTTABLE_GLOBAL: `m_iGlobalname` set and `FUN_100a8220(ent) == 0` (EFL_DORMANT clear).
					if (!E->GlobalName.IsEmpty() && !E->IsEflDormant()) Hi |= EntTableGlobal;
					if ((Lo | Hi) == 0) continue;                        // "Failed %s"
					if (Candidates.Num() >= 0x200)
					{
						UE_LOG(LogElysiumTransition, Warning, TEXT("Too many entities across a transition!"));
						break;
					}
					Candidates.Add({ E, Lo, Hi });                       // "Saving %s"
					if (Data.Sites != nullptr)
					{
						TransitionStateSite(Data.Sites, TEXT("transition_flags"), GBuildListFn, 0x101c7ff0u, TEXT("candidate"),
							FString::Printf(TEXT("j=%d entity=%s lo=0x%08x hi=0x%08x"), J, *TransitionStateEntityText(*E), Lo, Hi));
					}
				}
				for (const FCandidate& C : Candidates)
				{
					if (TransitionStateScreen(C.Entity) == 0) continue;  // "Screened out %s"
					const int32 Idx = TransitionStateRowOf(Data, *C.Entity); // FUN_101a08f0
					uint32 BitLo = 0, BitHi = 0;
					TransitionStateMaskBit(J, BitLo, BitHi);
					TransitionStateOrRowFlags(Data, Idx, BitLo | C.Lo, BitHi | C.Hi); // FUN_101a0980
				}
			}
			return Count;
		}

		// The port's stand-in for pass 1's creation (FUN_10136580 CreateEntityByName / FUN_1015d790 the
		// player on its edict) and the per-row restore FUN_101a3380. Named modernization: the Player
		// block (`FElysiumPlayer::Hydrate`) has already made the destination's copy of the player and of
		// every carried item before this runs, so the row resolves to that copy instead of creating a
		// second one. A row no copy answers for resolves NULL, as a failed create does.
		FElysiumEntity* TransitionStateResolveCreated(FElysiumEntityWorld& Destination, const FEntityTableRow& Row, bool bPlayerRow)
		{
			FElysiumPlayer* Player = Destination.FindPlayer();
			if (bPlayerRow) return Player;
			if (Player != nullptr)
			{
				if (FElysiumEntity* Carried = TransitionStateInventoryFind(Destination, *Player, Row.Classname)) return Carried;
			}
			return nullptr;
		}

		// The global-name branch FUN_101a3c40 runs after a successful row restore on an entity with
		// `m_iGlobalname`: FindGlobal; absent -> `Warning("Global Entity %s (%s) not in table!!!")` and
		// AddGlobal(name, current map, GLOBAL_ON); present with state != 2 -> MakeDormant when the
		// record's level is not the current map; present with state == 2 -> UTIL_Remove (FUN_101cd970).
		// Returns false when the entity was removed (the row is not counted).
		bool TransitionStateGlobalNameBranch(FSaveRestoreData& Data, FElysiumEntityWorld& Destination, FElysiumEntity& E)
		{
			FGlobalState& Table = GlobalStateTable();
			const auto NameAnsi = StringCast<ANSICHAR>(*E.GlobalName);
			const FGlobalRecord* Record = Table.Find(NameAnsi.Get());
			const FString CurrentMap = Destination.MapName();
			if (Record == nullptr)
			{
				UE_LOG(LogElysiumTransition, Warning, TEXT("Global Entity %s (%s) not in table!!!"), *E.GlobalName, E.Def ? *E.Def->Classname : TEXT("")); // 0x10593114
				const auto MapAnsi = StringCast<ANSICHAR>(*CurrentMap);
				Table.Add(NameAnsi.Get(), MapAnsi.Get(), GlobalStateOn, Data.Sites);
				TransitionStateSite(Data.Sites, TEXT("global_merge"), GRowsFn, 0x101a3c40u, TEXT("global_name"),
					FString::Printf(TEXT("global=%s found=0 state=1 level=%s"), *E.GlobalName, *CurrentMap));
				return true;
			}
			if (Record->State != GlobalStateDead)
			{
				const auto MapAnsi = StringCast<ANSICHAR>(*CurrentMap);
				const bool bOtherMap = FCStringAnsi::Stricmp(MapAnsi.Get(), Record->LevelName) != 0;
				if (bOtherMap) E.MakeDormant(); // CBaseEntity::MakeDormant 0x100a8060
				TransitionStateSite(Data.Sites, TEXT("global_merge"), GRowsFn, 0x101a3c40u, TEXT("global_name"),
					FString::Printf(TEXT("global=%s found=1 state=%d level=%s dormant=%d"), *E.GlobalName, Record->State,
						*TransitionStateText(Record->LevelName), bOtherMap ? 1 : 0));
				return true;
			}
			TransitionStateSite(Data.Sites, TEXT("global_merge"), GRowsFn, 0x101a3c40u, TEXT("global_name"),
				FString::Printf(TEXT("global=%s found=1 state=2 level=%s removed=1"), *E.GlobalName, *TransitionStateText(Record->LevelName)));
			E.Kill(); // FUN_101cd970 (UTIL_Remove)
			return false;
		}

		// FUN_101a3c40 (`__cdecl`, 2221 B): `(save, maskLo, maskHi)`, three passes over the entity
		// table (`save+0x1334` x `save+0x1330`). Returns the rows transferred, kept or merged.
		int32 TransitionStateCreateTransitionRows(FSaveRestoreData& Data, uint32 MaskLo, uint32 MaskHi, FElysiumEntityWorld& Destination)
		{
			TArray<FElysiumPlayer*> Players; // the local list FUN_101a5680 / FUN_101a55a0 fill in pass 1
			int32 Rows = 0;
			FRestore R(&Data);
			auto Selected = [MaskLo, MaskHi](const FEntityTableRow& Row)
			{
				return (Row.FlagsLo & MaskLo) != 0 || (Row.FlagsHi & MaskHi) != 0;
			};
			auto SeekRow = [&R, &Data](int32 Pos)
			{
				// `if (-1 < pos && pos < save[3]) { save[2] = pos; save[1] = save[0] + pos }`
				if (Pos > -1 && Pos < Data.Size) R.Seek(Pos);
			};

			// Pass 1: create. A row with data (+0x18 != 0) and an edict index (+0x04 != 0); no classname
			// warns. Retail splits on the edict index against `pGlobals+0x14` (maxClients): a client
			// edict's row makes the player on that edict (FUN_1015d790) and joins the player list, warning
			// `"ENTITY IS NOT A PLAYER: %d"` without FENTTABLE_PLAYER; any other selected row is created by
			// classname (FUN_10136580). The port has no edict index space, so the client-edict test is
			// the FENTTABLE_PLAYER word, which retail sets on exactly those rows.
			for (int32 I = 0; I < Data.EntityTable.Num(); ++I)
			{
				FEntityTableRow& Row = Data.EntityTable[I];
				FElysiumEntity* Created = nullptr;
				if (Row.Size != 0 && Row.EdictIndex != 0)
				{
					if (Row.Classname.IsEmpty())
					{
						UE_LOG(LogElysiumTransition, Warning, TEXT("Entity with data saved, but with no classname")); // 0x10593204
					}
					else if (Selected(Row))
					{
						const bool bPlayerRow = (Row.FlagsHi & EntTablePlayer) != 0;
						Created = TransitionStateResolveCreated(Destination, Row, bPlayerRow);
						if (bPlayerRow && Created != nullptr)
						{
							if ((Row.FlagsHi & EntTablePlayer) == 0) UE_LOG(LogElysiumTransition, Warning, TEXT("ENTITY IS NOT A PLAYER: %d"), I); // 0x105930f0
							Players.Add(static_cast<FElysiumPlayer*>(Created));
						}
					}
				}
				Row.Handle = Created ? Created->Handle : FElysiumEntityHandle::Invalid(); // +0x10 := handle, or -1
			}
			// Named modernization: the Player block hydrates the destination's player before this runs,
			// so the player list retail fills from the client rows is the destination's player.
			if (Players.IsEmpty())
			{
				if (FElysiumPlayer* Player = Destination.FindPlayer()) Players.Add(Player);
			}

			// Pass 2: the player rows (FENTTABLE_PLAYER, entity present, selected).
			for (int32 I = 0; I < Data.EntityTable.Num(); ++I)
			{
				FEntityTableRow& Row = Data.EntityTable[I];
				FElysiumEntity* E = Destination.Resolve(Row.Handle);
				SeekRow(Row.Location);
				if (E == nullptr || !Selected(Row) || (Row.FlagsHi & EntTablePlayer) == 0) continue;
				UE_LOG(LogElysiumTransition, Verbose, TEXT("Transferring %s (%d)"), *Row.Classname, E->Handle.Index); // 0x105931e8
				// FUN_101a3380 (the entity's restore from the row's stream): the hydrate stands for it.
				const bool bRestored = true;
				bool bCounted = true;
				if (bRestored && !E->GlobalName.IsEmpty())
				{
					bCounted = TransitionStateGlobalNameBranch(Data, Destination, *E);
				}
				if (bCounted)
				{
					Row.FlagsLo = 0;
					Row.FlagsHi = EntTableRemoved; // (0, 0x40000000): transferred
					++Rows;
					Row.RestoredEdictIndex = E->Handle.Index; // +0x0c := the edict index
					Data.TransferredEntities.Add(E->Handle);   // FUN_1011a580
					TransitionStateSite(Data.Sites, TEXT("transition_row"), GRowsFn, 0x101a3c40u, TEXT("transfer"),
						FString::Printf(TEXT("id=%d classname=%s arm=player rows=%d"), Row.Id, *Row.Classname, Rows));
				}
				// FUN_100f6ce0 (the post-restore flush over DAT_10703668's callbacks): unported here.
			}

			// Pass 3: every other selected row with an entity.
			for (int32 I = 0; I < Data.EntityTable.Num(); ++I)
			{
				FEntityTableRow& Row = Data.EntityTable[I];
				FElysiumEntity* E = Destination.Resolve(Row.Handle);
				SeekRow(Row.Location);
				if (E != nullptr && Selected(Row) && (Row.FlagsHi & EntTablePlayer) == 0)
				{
					if ((Row.FlagsHi & EntTableGlobal) == 0)
					{
						UE_LOG(LogElysiumTransition, Verbose, TEXT("Transferring %s (%d)"), *Row.Classname, E->Handle.Index);
						const bool bRestored = true; // FUN_101a3380: the hydrate stands for it
						bool bOwnershipCheck = true;
						if (bRestored && !E->GlobalName.IsEmpty())
						{
							bOwnershipCheck = TransitionStateGlobalNameBranch(Data, Destination, *E);
						}
						if (bOwnershipCheck)
						{
							// The ownership check: a player of the list owns it when `Inventory_Find(classname)`
							// answers this entity, a second handle on the player (offset UNRECOVERED) is it, or it
							// is in the player's `+0x14c0` list.
							bool bOwned = false;
							for (FElysiumPlayer* Player : Players)
							{
								if (Player == nullptr) continue;
								if (TransitionStateInventoryFind(Destination, *Player, Row.Classname) == E
									|| TransitionStateInventoryHolds(Destination, *Player, *E))
								{
									bOwned = true;
									break;
								}
							}
							if (bOwned)
							{
								++Rows;
								Row.FlagsLo = 0;
								Row.FlagsHi = EntTableRemoved;
								UE_LOG(LogElysiumTransition, Verbose, TEXT("Transferred %s (%d)"), *Row.Classname, E->Handle.Index); // 0x10593190
								TransitionStateSite(Data.Sites, TEXT("transition_row"), GRowsFn, 0x101a3c40u, TEXT("transfer"),
									FString::Printf(TEXT("id=%d classname=%s arm=transferred rows=%d"), Row.Id, *Row.Classname, Rows));
							}
							else
							{
								// "Suppressing %s" then FUN_101cd970 (UTIL_Remove) on the created entity. The port
								// created nothing in pass 1 (the hydrate made only the carried copies), so there is
								// no second entity to remove; the row keeps its flags, as retail's does.
								UE_LOG(LogElysiumTransition, Verbose, TEXT("Suppressing %s"), *Row.Classname); // 0x105931ac
								TransitionStateSite(Data.Sites, TEXT("transition_row"), GRowsFn, 0x101a3c40u, TEXT("transfer"),
									FString::Printf(TEXT("id=%d classname=%s arm=suppressed rows=%d"), Row.Id, *Row.Classname, Rows));
							}
						}
					}
					else
					{
						// "Merging changes for global: %s" -> FUN_101a3470 on the Entities handler 0x1072bb44
						// (the field merge of a global entity from the saved row). Its body is unwalked (r029
						// Open 10): PLANNING FAULT, reported; the result the port has is 0, so the row is not
						// counted, as a failed merge is not.
						UE_LOG(LogElysiumTransition, Verbose, TEXT("Merging changes for global: %s"), *Row.Classname); // 0x105931c0
						const int32 MergeResult = 0;
						TransitionStateSite(Data.Sites, TEXT("global_merge"), GRowsFn, 0x101a3c40u, TEXT("merge"),
							FString::Printf(TEXT("global=%s result=%d fn=FUN_101a3470 va=0x101a3470 unported=1"), *E->GlobalName, MergeResult));
						if (MergeResult > 0)
						{
							++Rows;
							Row.RestoredEdictIndex = E->Handle.Index;
							Data.TransferredEntities.Add(E->Handle); // FUN_1011a580
						}
					}
					// FUN_100f6ce0: unported here (see pass 2).
				}
				SeekRow(Row.Location + Row.Size); // the stream advanced past this row's data
			}
			return Rows;
		}
	}

	// --- the global-entity table -------------------------------------------------------------------

	void FGlobalState::Add(const char* Name, const char* LevelName, int32 State, IElysiumRetailSiteSink* Sites)
	{
		// FUN_10057740: `node = _calloc(0x68, 1)`; `node+0x64 = head; head = node` (the prepend, before
		// the copies); `Q_strncpy(node, name ? name : "", 0x40)`; `Q_strncpy(node+0x40, level ? level : "",
		// 0x20)`; `node+0x60 = state`; `count += 1`.
		TransitionStateSite(Sites, TEXT("global_add"), GAddGlobalFn, 0x10057740u, TEXT("entry"),
			FString::Printf(TEXT("name=%s level=%s state=%d"), *TransitionStateText(Name), *TransitionStateText(LevelName), State));
		FGlobalRecord Node;
		FMemory::Memzero(&Node, sizeof(Node));
		Chain.Insert(Node, 0);
		TransitionStateStrncpy(Chain[0].Name, Name, 0x40);
		TransitionStateStrncpy(Chain[0].LevelName, LevelName, 0x20);
		Chain[0].State = State;
		++ListCount;
		TransitionStateSite(Sites, TEXT("global_add"), GAddGlobalFn, 0x10057740u, TEXT("return"), FString::Printf(TEXT("count=%d"), ListCount));
	}

	const FGlobalRecord* FGlobalState::Find(const char* Name) const
	{
		// FUN_100576a0: a NULL name answers NULL; otherwise the walk from the head through +0x64 with
		// `__strcmpi(name, node)` (0x1043e780), the first match or NULL.
		if (Name == nullptr) return nullptr;
		for (const FGlobalRecord& Node : Chain)
		{
			if (FCStringAnsi::Stricmp(Name, Node.Name) == 0) return &Node;
		}
		return nullptr;
	}

	FGlobalRecord* FGlobalState::Find(const char* Name)
	{
		return const_cast<FGlobalRecord*>(static_cast<const FGlobalState*>(this)->Find(Name));
	}

	void FGlobalState::Clear()
	{
		// FUN_100579f0: `_free` each node along +0x64; then FUN_10057680: `head = 0; count = 0`.
		Chain.Reset();
		ListCount = 0;
	}

	int32 FGlobalState::Save(FSave& S, IElysiumRetailSiteSink* Sites) const
	{
		// FUN_10057850 (`__thiscall`, ECX = the table, one arg: the ISave adapter; RET 4).
		TransitionStateSite(Sites, TEXT("global_save"), GGlobalSaveFn, 0x10057850u, TEXT("entry"),
			FString::Printf(TEXT("count=%d head=%s"), ListCount, Chain.IsEmpty() ? TEXT("null") : TEXT("set")));
		// 1. `adapter.slot3("GLOBAL", table, NULL, desc 0x1053fc54, 1)`: the one field `m_listCount`
		//    (INTEGER at table+0x04). CSave::vfunc3 0x101a0260 answers 1 on every path, so the `return 0`
		//    exit (asm 0x10057870-0x1005787a) is dead; the port writes the dword (encoding: D6).
		S.WriteInt(&ListCount, 1);
		const int32 HeaderResult = 1;
		TransitionStateSite(Sites, TEXT("global_save"), GGlobalSaveFn, 0x10057850u, TEXT("header"), FString::Printf(TEXT("result=%d"), HeaderResult));
		// 2. `n = table+0x04; node = table+0x00; n <= 0 -> return 1`.
		int32 Count = ListCount;
		if (Count <= 0)
		{
			TransitionStateSite(Sites, TEXT("global_save"), GGlobalSaveFn, 0x10057850u, TEXT("return"), TEXT("result=1"));
			return 1;
		}
		// 3. The loop: a NULL node at the top of any pass returns 1 (a short chain ends silently); else
		//    `adapter.slot3("GENT", node, NULL, desc 0x1053fc80, 3)`, a dead `return 0`, the count re-read
		//    from +0x04, `node = node->next`, `i++`, while `i < count`.
		int32 I = 0;
		do
		{
			if (!Chain.IsValidIndex(I))
			{
				TransitionStateSite(Sites, TEXT("global_save"), GGlobalSaveFn, 0x10057850u, TEXT("return"), TEXT("result=1 arm=null_node"));
				return 1;
			}
			const FGlobalRecord& Node = Chain[I];
			S.WriteData(&Node, sizeof(FGlobalRecord));
			TransitionStateSite(Sites, TEXT("global_save"), GGlobalSaveFn, 0x10057850u, TEXT("record"),
				FString::Printf(TEXT("name=%s level=%s state=%d"), *TransitionStateText(Node.Name), *TransitionStateText(Node.LevelName), Node.State));
			Count = ListCount;
			++I;
		} while (I < Count);
		// 4. Returns 1.
		TransitionStateSite(Sites, TEXT("global_save"), GGlobalSaveFn, 0x10057850u, TEXT("return"), TEXT("result=1"));
		return 1;
	}

	int32 FGlobalState::Restore(FRestore& R, IElysiumRetailSiteSink* Sites)
	{
		// FUN_100578e0 (`__thiscall`, ECX = the table, one arg: the IRestore adapter; RET 4).
		// 1. FUN_100579f0(table), unconditional: every node freed, head and count zeroed.
		TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("clear"), FString::Printf(TEXT("count_before=%d"), ListCount));
		Clear();
		// 2. `restore.slot3("GLOBAL", table, NULL, desc 0x1053fc54, 1)` (CRestore::vfunc3 0x101a1b70: the
		//    name token must match, else `Msg("Expected %s found %s!")`, a rewind and 0). Failure returns 0
		//    with the table left cleared. The port's mismatch is a read past the stream's end.
		if (R.Data != nullptr) R.Data->bReadPastEnd = false;
		R.ReadInt(&ListCount, 1, 0);
		if (R.Data == nullptr || R.Data->bReadPastEnd)
		{
			ListCount = 0;
			TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("header"), TEXT("read=0 count=0"));
			TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("return"), TEXT("result=0"));
			return 0;
		}
		// 3. `n = table+0x04` (what the header read just stored); `table+0x04 = 0`.
		const int32 N = ListCount;
		ListCount = 0;
		TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("header"), FString::Printf(TEXT("read=1 count=%d"), N));
		// 4. `n <= 0 -> return 1`.
		if (N <= 0)
		{
			TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("return"), TEXT("result=1"));
			return 1;
		}
		// 5. For `i < n`: `restore.slot3("GENT", &rec, NULL, desc 0x1053fc80, 3)` into a stack record (not
		//    zeroed first); failure returns 0 with `i` records prepended. Success: FUN_10057740(table, name
		//    if its first byte is set else NULL, levelName likewise, state).
		for (int32 I = 0; I < N; ++I)
		{
			FGlobalRecord Record;
			R.ReadData(&Record, sizeof(FGlobalRecord));
			if (R.Data->bReadPastEnd)
			{
				TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("return"),
					FString::Printf(TEXT("result=0 prepended=%d"), I));
				return 0;
			}
			Record.Name[63] = '\0';
			Record.LevelName[31] = '\0';
			Add(Record.Name[0] != '\0' ? Record.Name : nullptr, Record.LevelName[0] != '\0' ? Record.LevelName : nullptr, Record.State, Sites);
			TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("record"),
				FString::Printf(TEXT("i=%d name=%s level=%s state=%d count=%d"), I, *TransitionStateText(Record.Name),
					*TransitionStateText(Record.LevelName), Record.State, ListCount));
		}
		// 6. Returns 1.
		TransitionStateSite(Sites, TEXT("global_restore"), GGlobalRestoreFn, 0x100578e0u, TEXT("return"), TEXT("result=1"));
		return 1;
	}

	FGlobalState& GlobalStateTable()
	{
		static FGlobalState Table;
		return Table;
	}

	void SaveGlobalState(FSaveRestoreData* Data)
	{
		// CServerGameDLL::vfunc15 0x1011b040 (`__thiscall`, RET 4; thunk 0x1000242d) -> FUN_10057a30
		// (`__cdecl`): CSave ctor FUN_1019f870 on `save` (vtable, +0x18 = save, +0x1c = save+0x18 or 0,
		// the 0x80-byte staging buffer); FUN_10057850(&DAT_106be530, &adapter), its result not read;
		// the inlined dtor (+0x10 = 0; buffer freed when state +0x0c != -1; capacity 0). Returns void: a
		// failed GLOBAL write is silent. The engine calls it after slot 13 writes the GameHeader group
		// (CSaveRestore::vfunc12 0x20095980, asm 0x20095a85-0x20095a92).
		IElysiumRetailSiteSink* Sites = Data ? Data->Sites : nullptr;
		TransitionStateSite(Sites, TEXT("global_save_entry"), TEXT("Global::FUN_10057a30"), 0x10057a30u, TEXT("entry"),
			FString::Printf(TEXT("save=%s"), Data ? TEXT("set") : TEXT("null")));
		FSave Adapter(Data);
		GlobalStateTable().Save(Adapter, Sites);
	}

	void RestoreGlobalState(FSaveRestoreData* Data)
	{
		// CServerGameDLL::vfunc16 0x1011b060 (`__thiscall`, RET 4; thunk 0x1000f5a6) -> FUN_10057ac0
		// (`__cdecl`): CRestore ctor FUN_101a12a0 on `restore`; FUN_100578e0(&DAT_106be530, &adapter),
		// result discarded; the same teardown. Engine caller: the function at 0x20095ea0-0x20095f2d
		// (inside CSaveRestore::vfunc5), after slot 14 reads the GameHeader group, gated on a stack
		// argument whose meaning is unrecovered.
		IElysiumRetailSiteSink* Sites = Data ? Data->Sites : nullptr;
		TransitionStateSite(Sites, TEXT("global_restore"), TEXT("Global::FUN_10057ac0"), 0x10057ac0u, TEXT("entry"),
			FString::Printf(TEXT("restore=%s"), Data ? TEXT("set") : TEXT("null")));
		FRestore Adapter(Data);
		GlobalStateTable().Restore(Adapter, Sites);
	}

	void ResetGlobalState()
	{
		// CServerGameDLL::vfunc7 0x10057b50: FUN_100579f0(&DAT_106be530) -- the table cleared -- then
		// `DAT_10580ae8 = 1` and a tail jump through FUN_101bdb50()'s slot 0 (both unrecovered; the
		// engine phase that calls slot 7 is unrecovered too).
		GlobalStateTable().Clear();
	}

	int32 DispatchSpawnGlobalArm(FElysiumEntityWorld& World, FElysiumEntity& Entity)
	{
		// DispatchSpawn 0x101d1280, after `CBaseEntity::PostSpawn`: `if (m_iGlobalname != 0) { rec =
		// FUN_100576a0(table, name); if (!rec) FUN_10057740(table, name, pGlobals+0x24 /*mapname*/, 1);
		// else if (rec+0x60 == 2) return -1; else if (__strcmpi(mapname, rec+0x40) != 0) { MakeDormant;
		// Relink; return 0 } }; Relink; return 0`.
		if (Entity.GlobalName.IsEmpty()) return 0;
		FGlobalState& Table = GlobalStateTable();
		const auto NameAnsi = StringCast<ANSICHAR>(*Entity.GlobalName);
		const auto MapAnsi = StringCast<ANSICHAR>(*World.MapName());
		FElysiumEntityRetailSites Sites(&World, Entity);
		const FGlobalRecord* Record = Table.Find(NameAnsi.Get());
		if (Record == nullptr)
		{
			Table.Add(NameAnsi.Get(), MapAnsi.Get(), GlobalStateOn, World.HasAiTraceSink() ? &Sites : nullptr);
			Sites.Site(TEXT("global_spawn"), TEXT("DispatchSpawn"), 0x101d1280u, TEXT("branch"),
				FString::Printf(TEXT("globalname=%s arm=add level=%s state=1"), *Entity.GlobalName, *World.MapName()));
			return 0;
		}
		if (Record->State == GlobalStateDead)
		{
			Sites.Site(TEXT("global_spawn"), TEXT("DispatchSpawn"), 0x101d1280u, TEXT("branch"),
				FString::Printf(TEXT("globalname=%s arm=dead level=%s"), *Entity.GlobalName, *TransitionStateText(Record->LevelName)));
			return -1;
		}
		if (FCStringAnsi::Stricmp(MapAnsi.Get(), Record->LevelName) != 0)
		{
			Entity.MakeDormant(); // CBaseEntity::MakeDormant 0x100a8060; Relink follows (no port body)
			Sites.Site(TEXT("global_spawn"), TEXT("DispatchSpawn"), 0x101d1280u, TEXT("branch"),
				FString::Printf(TEXT("globalname=%s arm=dormant level=%s state=%d"), *Entity.GlobalName, *TransitionStateText(Record->LevelName), Record->State));
			return 0;
		}
		Sites.Site(TEXT("global_spawn"), TEXT("DispatchSpawn"), 0x101d1280u, TEXT("branch"),
			FString::Printf(TEXT("globalname=%s arm=same_level level=%s state=%d"), *Entity.GlobalName, *TransitionStateText(Record->LevelName), Record->State));
		return 0;
	}

	// --- the adjacency table -----------------------------------------------------------------------

	void BuildAdjacentMapList(FSaveRestoreData& Data, const char* OldLevel, const char* LandmarkName)
	{
		// CServerGameDLL::BuildAdjacentMapList 0x1011b9f0 (`__stdcall`, RET 8; slot 23). Arms: the
		// profiler scope (no game effect); `save = *(pGlobals+0x20)`, NULL skips; `save+0x18 =
		// FUN_101c7ff0(save+0x1c, 0x3c, oldLevel, landmarkName)` through the thunk chain 0x1000a93e ->
		// 0x101c7f80 -> 0x10009c50; the profiler exit. `save` is this data (the engine's one allocation).
		TransitionStateSite(Data.Sites, TEXT("adjacent_build"), TEXT("CServerGameDLL::BuildAdjacentMapList"), 0x1011b9f0u, TEXT("entry"),
			FString::Printf(TEXT("save=set oldLevel=%s landmarkName=%s"), OldLevel ? *TransitionStateText(OldLevel) : TEXT("NULL"),
				LandmarkName ? *TransitionStateText(LandmarkName) : TEXT("NULL")));
		Data.ConnectionCount = TransitionStateBuildChangeList(Data, ElysiumSaveRestore::AdjacencyCapacity, OldLevel, LandmarkName);
		TransitionStateSite(Data.Sites, TEXT("adjacent_build"), TEXT("CServerGameDLL::BuildAdjacentMapList"), 0x1011b9f0u, TEXT("return"),
			FString::Printf(TEXT("connectionCount=%d"), Data.ConnectionCount));
	}

	// --- the transition list -----------------------------------------------------------------------

	int32 CreateEntityTransitionList(FSaveRestoreData& Data, uint32 MaskLo, uint32 MaskHi, FElysiumEntityWorld& Destination)
	{
		// CServerGameDLL::CreateEntityTransitionList 0x1011b590 (`__stdcall`, RET 0xc; slot 22).
		const TCHAR* Fn = TEXT("CServerGameDLL::CreateEntityTransitionList");
		TransitionStateSite(Data.Sites, TEXT("transition_mask"), Fn, 0x1011b590u, TEXT("entry"),
			FString::Printf(TEXT("save=set maskLo=0x%08x maskHi=0x%08x rows=%d"), MaskLo, MaskHi, Data.EntityTable.Num()));
		// 1. Profiler scope enter (no game effect). 2. `rc = FUN_101a3c40(save, maskLo, maskHi)` (asm 0x1011b657).
		const int32 Rc = TransitionStateCreateTransitionRows(Data, MaskLo, MaskHi, Destination);
		TransitionStateSite(Data.Sites, TEXT("entity_rows"), GRowsFn, 0x101a3c40u, TEXT("return"), FString::Printf(TEXT("rows=%d"), Rc));
		TransitionStateSite(Data.Sites, TEXT("transition_mask"), Fn, 0x1011b590u, TEXT("branch"), FString::Printf(TEXT("rows=%d"), Rc));
		using ElysiumSaveRestore::EGameBlock;
		struct FBlockRef { const TCHAR* Idx; EGameBlock Block; const TCHAR* Name; uint32 Slot7; uint32 Slot8; };
		// A = FUN_100cffa0 -> 0x106e70a8 (EventQueue; slot 7 0x100cff70, slot 8 0x10043c00), B = FUN_10045700 ->
		// 0x106bda60 (Physics; 0x100440c0, 0x100447f0), C = FUN_1030c390 -> 0x10936b5c (AI; 0x1030c210,
		// 0x10043c00), D = FUN_1019b360 -> 0x1072b354 (Python; 0x1019b130, 0x10043c00). AI and Python are
		// the L4 / L5 handlers registered by L0 (hooks.tsv has no row: a planning fault, reported); their
		// slot 8 and the EventQueue's are the empty FUN_10043c00, so only the Physics post hook acts.
		const FBlockRef Blocks[4] = {
			{ TEXT("A"), EGameBlock::EventQueue, TEXT("EventQueue"), 0x100cff70u, 0x10043c00u },
			{ TEXT("B"), EGameBlock::Physics, TEXT("Physics"), 0x100440c0u, 0x100447f0u },
			{ TEXT("C"), EGameBlock::Ai, TEXT("AI"), 0x1030c210u, 0x10043c00u },
			{ TEXT("D"), EGameBlock::Python, TEXT("Python"), 0x1019b130u, 0x10043c00u },
		};
		// 3./4. `rc != 0`: CRestore ctor FUN_101a12a0 on `save` (asm 0x1011b66a), then for A, B, C, D:
		// `X.slot7(&r, 0, 0)` (vtable +0x1c; asm 0x1011b673-0x1011b6c4); then FUN_10032b00 (`r+0x10 = 0`)
		// and FUN_1002cbd0 (the staging buffer freed when state != -1; capacity 0).
		if (Rc != 0)
		{
			FRestore Adapter(&Data);
			for (const FBlockRef& Ref : Blocks)
			{
				TransitionStateSite(Data.Sites, TEXT("transition_mask"), Fn, 0x1011b590u, TEXT("block"),
					FString::Printf(TEXT("tag=restore idx=%s name=%s slot=7 va=0x%08x args=(r,0,0)"), Ref.Idx, Ref.Name, Ref.Slot7));
				ElysiumSaveRestore::GameBlockHandler(Ref.Block).Restore(Adapter, 0, 0);
			}
		}
		// 5. Post hooks, unconditional: `X.slot8()` for A, B, C, D (vtable +0x20; asm 0x1011b6f8-0x1011b725).
		for (const FBlockRef& Ref : Blocks)
		{
			TransitionStateSite(Data.Sites, TEXT("transition_mask"), Fn, 0x1011b590u, TEXT("block"),
				FString::Printf(TEXT("tag=post idx=%s name=%s slot=8 va=0x%08x"), Ref.Idx, Ref.Name, Ref.Slot8));
			ElysiumSaveRestore::GameBlockHandler(Ref.Block).PostRestore();
		}
		// 6. Profiler scope exit; `MOV EAX, EBP`: rc.
		TransitionStateSite(Data.Sites, TEXT("transition_mask"), Fn, 0x1011b590u, TEXT("return"), FString::Printf(TEXT("rc=%d"), Rc));
		return Rc;
	}

	int32 EntityPatchWrite(const FSaveRestoreData& Data, FElysiumMapSnapshot& Snapshot)
	{
		// engine.dll 0x200973c0 (EntityPatchWrite; Source's shape): `int count; int ids[count]` of every
		// row carrying FENTTABLE_REMOVED, beside the saved map as `.HL3`. The port's `.HL3` is the
		// snapshot's `AbsentEntities`: the rows written are added to what an earlier departure wrote.
		int32 Written = 0;
		for (const FEntityTableRow& Row : Data.EntityTable)
		{
			if ((Row.FlagsHi & EntTableRemoved) == 0) continue;
			Snapshot.AbsentEntities.AddUnique(Row.EdictIndex);
			++Written;
		}
		TransitionStateSite(Data.Sites, TEXT("transition_load"), TEXT("CSaveRestore::vfunc10"), 0x20097d00u, TEXT("hl3"),
			FString::Printf(TEXT("map=%s fn=EntityPatchWrite va=0x200973c0 written=%d absent=%d"), *Snapshot.MapName, Written, Snapshot.AbsentEntities.Num()));
		return Written;
	}

	void EntityPatchRead(FSaveRestoreData& Data, const FElysiumMapSnapshot& Snapshot)
	{
		// EntityPatchRead (Source's shape; the engine's VtMB body unrecovered): every `.HL3` id's row has
		// its flags set to FENTTABLE_REMOVED alone, so the mask test skips it.
		for (FEntityTableRow& Row : Data.EntityTable)
		{
			if (!Snapshot.AbsentEntities.Contains(Row.EdictIndex)) continue;
			Row.FlagsLo = 0;
			Row.FlagsHi = EntTableRemoved;
		}
	}

	int32 EngineLoadAdjacentEnts(FElysiumEntityWorld& World, ISnapshotStore& Store, const FString& OldLevel,
		const FString& LandmarkName, IElysiumRetailSiteSink* Sites)
	{
		// CSaveRestore::vfunc10 0x20097d00 (engine.dll). What the walk recovered: slot 23 with (oldLevel,
		// landmarkName) at 0x20097d73; per adjacent save slot 20 (0x20097e25; the `sp_masquerade_1`
		// branch tested at 0x20097d76-0x20097d8f repeats it at 0x20098173 -- meaning unrecovered); the
		// mask over the save's ADJACENCY rows naming the current level (0x20098130-0x20098141); slot 22
		// only on a non-zero mask (0x20097ff2 / 0x2009834c); 0x200973c0 on a non-zero rc. The loop's
		// shape between those calls is Source's `LoadAdjacentEnts`: each adjacent map once, the previous
		// level required among the connections.
		const TCHAR* Fn = TEXT("CSaveRestore::vfunc10");
		const auto OldLevelAnsi = StringCast<ANSICHAR>(*OldLevel);
		const auto LandmarkAnsi = StringCast<ANSICHAR>(*LandmarkName);
		FSaveRestoreData Current;          // the current level's save data (`pGlobals+0x20`)
		Current.Reset();
		Current.World = &World;
		Current.Sites = Sites;
		BuildAdjacentMapList(Current, OldLevelAnsi.Get(), LandmarkAnsi.Get());
		TransitionStateSite(Sites, TEXT("transition_load"), Fn, 0x20097d00u, TEXT("entry"),
			FString::Printf(TEXT("map=%s oldLevel=%s landmarkName=%s connectionCount=%d"), *World.MapName(), *OldLevel, *LandmarkName, Current.ConnectionCount));

		const auto CurrentMapAnsi = StringCast<ANSICHAR>(*World.MapName());
		bool bFoundPrevious = false;
		int32 Moved = 0;
		for (int32 I = 0; I < Current.ConnectionCount && I < Current.Adjacency.Num(); ++I)
		{
			const FAdjacencyRow& Row = Current.Adjacency[I];
			if (FCStringAnsi::Stricmp(Row.MapName, OldLevelAnsi.Get()) == 0) bFoundPrevious = true;
			bool bSeen = false;
			for (int32 Test = 0; Test < I; ++Test)
			{
				if (FCStringAnsi::Stricmp(Current.Adjacency[Test].MapName, Row.MapName) == 0) { bSeen = true; break; }
			}
			if (bSeen) continue; // each map once
			const FString RowMap = TransitionStateText(Row.MapName);
			FElysiumMapSnapshot* Saved = Store.FindMutable(RowMap);
			if (Saved == nullptr || Saved->BlockStream.IsEmpty()) continue; // no save for that map

			// The adjacent map's save data: slot 20 reads its directory and entity table.
			FSaveRestoreData Data;
			Data.Reset();
			Data.Bytes = Saved->BlockStream;
			Data.Size = Data.Bytes.Num();
			Data.bGrowable = false;
			Data.World = &World;
			Data.Sites = Sites;
			FElysiumMapSnapshot Decoded;
			ElysiumSaveRestore::FGameContext Context;
			Context.World = &World;
			Context.Decoded = &Decoded;
			ElysiumSaveRestore::BindTransitionContext(Data, Context);
			ElysiumSaveRestore::FBlockSet& Set = ElysiumSaveRestore::GameBlockSet();
			{
				FRestore Position(&Data);
				Position.Seek(Saved->BlockHeaderStart);
				ElysiumSaveRestore::ReadRestoreHeaders(Set, &Data); // slot 20 0x1011b950
			}
			EntityPatchRead(Data, *Saved);

			// The mask: for each of that save's ADJACENCY rows whose map is the current level,
			// `1 << (j & 31)` sign-extended into (lo, hi).
			uint32 MaskLo = 0, MaskHi = 0;
			for (int32 J = 0; J < Saved->Adjacency.Num(); ++J)
			{
				const auto SavedMapAnsi = StringCast<ANSICHAR>(*Saved->Adjacency[J].MapName);
				if (FCStringAnsi::Strcmp(SavedMapAnsi.Get(), CurrentMapAnsi.Get()) != 0) continue;
				uint32 BitLo = 0, BitHi = 0;
				TransitionStateMaskBit(J, BitLo, BitHi);
				MaskLo |= BitLo;
				MaskHi |= BitHi;
			}
			TransitionStateSite(Sites, TEXT("transition_load"), Fn, 0x20097d00u, TEXT("mask"),
				FString::Printf(TEXT("map=%s rows=%d adjacency=%d maskLo=0x%08x maskHi=0x%08x"), *RowMap, Data.EntityTable.Num(), Saved->Adjacency.Num(), MaskLo, MaskHi));
			if ((MaskLo | MaskHi) != 0)
			{
				const int32 Rc = CreateEntityTransitionList(Data, MaskLo, MaskHi, World); // slot 22 0x1011b590
				if (Rc != 0)
				{
					EntityPatchWrite(Data, *Saved); // 0x200973c0
					Moved += Rc;
				}
			}
			ElysiumSaveRestore::UnbindTransitionContext();
		}
		if (!bFoundPrevious)
		{
			// Source: `Host_Error("Level transition ERROR\nCan't find connection to %s from %s\n")`. The
			// engine's VtMB arm is unrecovered; the port reports and continues.
			UE_LOG(LogElysiumTransition, Warning, TEXT("Level transition ERROR: can't find connection to %s from %s"), *OldLevel, *World.MapName());
		}
		TransitionStateSite(Sites, TEXT("transition_load"), Fn, 0x20097d00u, TEXT("return"),
			FString::Printf(TEXT("moved=%d found_previous=%d"), Moved, bFoundPrevious ? 1 : 0));
		return Moved;
	}
}
