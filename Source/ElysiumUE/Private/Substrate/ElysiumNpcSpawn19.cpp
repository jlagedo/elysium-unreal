// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- `CAI_BaseNPCTroika`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcSpawn19.inl` (included inside `class FElysiumNpc`) or generated in
// `ElysiumNpcSlots.inl` for a slot body.
//
// Owns (Spawn19's `rule` rows): 0x102bf340 CAI_BaseNPCTroika::Event_Killed, 0x10298d30
// CAI_BaseNPCTroika::Spawn. Walked prose: `docs/vtmb/npc-ai/lifecycle.md` § "Story 8, family Spawn19"
// (Spawn) and § "The death chain, kill to corpse" (Event_Killed).

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcWitness.h"

// -------------------------------------------------------------------------------------------------
// Slot 144 -- `CAI_BaseNPCTroika::Event_Killed` `0x102bf340`
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Event_Killed(void* InInfo)
{
	const FElysiumTakeDamageInfo* const Info = static_cast<const FElysiumTakeDamageInfo*>(InInfo);

	// The death stimulus, unless the damage packet's `+0x49` byte suppresses it: `0x102ca2a0` on the
	// act store `DAT_109253f8` with (type 1 = criminal, source this, the origin raised by 16.0,
	// level 3, radius 100000.0, offender NULL, victim this, 1). This runtime's act store is the law
	// event bus; its record carries channel, severity, origin and offender -- the radius, the victim
	// and the trailing flag have no port field (listed as unrecovered).
	if (!Spawn19DamageInfoSuppressesStimulus(Info))                                      // 0x102bf34d / 0x102bf354
	{
		FVector StimulusOrigin = GetAbsOrigin();                                         // 0x102bf35a slot 217
		StimulusOrigin.Z += Spawn19DeathStimulusRaiseUnits * ElysiumMove::U;             // 0x102bf37f
		ElysiumNpcWitness::PublishLawEvent(World, ElysiumNpcWitness::EChannel::Criminal,
			Spawn19DeathStimulusLevel, StimulusOrigin, FElysiumEntityHandle::Invalid());         // 0x102bf3a0 -> 0x102ca2a0
		++Spawn19DeathStimulusBroadcasts;
	}

	FElysiumNpcBase::Event_Killed(InInfo);                                               // 0x102bf3a8 -> 0x10265ad0

	ClearScheduleHint(Spawn19HintReuseSeconds);                                          // 0x102bf3b4 ClearHintNode(5.0)
	Slot601(this);                                                                       // 0x102bf3be slot 601, unconditional
	// `0x102b53d0(0, "Leaving interesting place (Event_Killed)")` -- the release
	// `LeaveInterestingPlaceOnRemove` stands for (its string is the only difference).
	LeaveInterestingPlaceOnRemove();                                                     // 0x102bf3cd
	if (IsInDialog())                                                                    // 0x102bf3d4 / 0x102bf3db
	{
		StopDialogOnRemove();                                                            // 0x102bf3df -> 0x102c0bb0
	}

	// `m_iName` set: `PyRun_SimpleString(va("MarkAsDead(\"%s\")", m_iName))`. The port's script
	// host runs the line synchronously through `EvalCondition` (error-to-false, which is retail's
	// `PyRun_SimpleString` printing and returning); `vamputil.MarkAsDead` is the shipped callee.
	if (!TargetName.IsEmpty())                                                           // 0x102bf3ec
	{
		Spawn19LastMarkAsDeadLine = FString::Printf(TEXT("MarkAsDead(\"%s\")"), *TargetName); // 0x102bf3f4 -> 0x101d3730
		++Spawn19MarkAsDeadLines;
		if (World != nullptr)
		{
			World->EvalCondition(Spawn19LastMarkAsDeadLine, Handle,
				FElysiumEntityHandle::Invalid());                                        // 0x102bf3fa [0x109f36fc]
		}
	}

	// `m_hClosestPlayer` (`+0x628c`) live: the player's closest-NPC cache is offered this NPC at
	// 99999.9 (`0x101828b0`). The port's cache is the chain player's (`UpdateClosestNpc`).
	if (World != nullptr && Senses.Memory.ClosestPlayer.IsSet())                         // 0x102bf40c
	{
		if (World->Resolve(Senses.Memory.ClosestPlayer) != nullptr)                      // 0x102bf428 / 0x102bf42e
		{
			UpdateClosestNpc(this, Spawn19ClosestNpcOffer);                              // 0x102bf438 -> 0x101828b0
		}
	}

	// Port bookkeeping, after retail's whole body (so `SetState(7)`'s slot-463 dispatch at
	// `0x10265dba` runs exactly as retail orders it): a body `CreateCorpse` took out of the world
	// vacates the port's arbiter — every body-owner token, the running program, a pushed order, an
	// open conversation — and its mind refuses every later acquisition.
	if (bDeathCommitted)
	{
		ReleaseAllBodyOwnership(TEXT("killed"), /*bDeadMind=*/true);
	}
}

bool FElysiumNpc::Spawn19DamageInfoSuppressesStimulus(const FElysiumTakeDamageInfo* Info) const
{
	(void)Info;
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 103 -- `CAI_BaseNPCTroika::Spawn` `0x10298d30`
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::TroikaSpawnBody()
{
	bSpawnCalled = true;                                                                 // 0x10298d3d m_bInitialized (+0x62e9)

	// A non-empty `m_statTemplate` (`+0x10e4`) is applied through the template manager
	// (`0x10206c30` on `DAT_1074f028` -> `0x10206aa0`, `SetCharTemplate`). The port's application is
	// `SeedSheet`, which tests the empty template itself; its unconditional `stats.txt` default seed
	// is the combat character's constructor-time work in retail (listed for the integrator). The
	// inline `strcmp(STRING(m_statTemplate), "")` (`0x10298d4c` null default, the byte loop
	// `0x10298d59` / `0x10298d5d` / `0x10298d67` / `0x10298d71`, and `0x10298d88`'s second null
	// default) is `SeedSheet`'s own empty-template test.
	SeedSheet();                                                                         // 0x10298d7e / 0x10298d93

	Precache();                                                                          // 0x10298d9c slot 104

	// `AddToTeam(m_sTeamName)` only on a non-empty team string.
	const FString TeamName = Spawn19TeamName();
	if (!TeamName.IsEmpty())                                                             // 0x10298daa null default / 0x10298db1
	{
		Spawn19AddToTeam(TeamName);                                                      // 0x10298db6 -> 0x103239a0
	}

	// `SetModel(STRING(GetModelName()))`. Slot 9 is the generated stub; the word it answers,
	// `m_ModelName`, is `FElysiumEntity::Model`.
	Spawn19SetModel(Model);                                                              // 0x10298dc4 / 0x10298dcb null default / 0x10298dd4 slot 105

	BloodColorWord = Spawn19BloodColor;                                                  // 0x10298dde
	FieldOfViewDot = Spawn19TroikaFieldOfView;                                           // 0x10298de8
	CapabilityWord |= Spawn19TroikaCapA;                                                 // 0x10298df2 CapabilitiesAdd(1)
	CapabilityWord |= Spawn19TroikaCapB;                                                 // 0x10298dfe CapabilitiesAdd(0x800000)
	CapabilityWord |= Spawn19TroikaCapC;                                                 // 0x10298e07 CapabilitiesAdd(8)
	HackedGunPosUnits = FVector(0.f, 0.f, Spawn19HackedGunHeightUnits);                 // 0x10298e0c..0x10298e18
	CurrentSpotIndex = INDEX_NONE;                                                       // 0x10298e2a m_pInterestingPlace = 0
	bAmbientArrived = false;                                                             // 0x10298e30

	// The four `m_Collision` writes, each under its own scope-trace frame (their `m_iName` null
	// defaults `0x10298e37`, `0x10298ea6`, `0x10298f17`, `0x10298f89` are the absent debug stack).
	RetailSolidFlags = 0u;                                                               // 0x10298e8d SetSolidFlags(0)
	RetailSolidFlags |= (RetailSolidFlags & 0xffffu) | Spawn19SolidFlagA;               // 0x10298efe AddSolidFlags(w | 1)
	RetailSolidFlags |= (RetailSolidFlags & 0xffffu) | Spawn19SolidFlagB;               // 0x10298f70 AddSolidFlags(w | 0x40)
	RetailSolidType = Spawn19SolidBbox;                                                  // 0x10298fd7 SetSolid(SOLID_BBOX)
	++RetailSolidSets;

	SetMoveType(Spawn19MoveTypeStep, 0);                                                 // 0x10298fed slot 93
	SetHullSizeNormal(false);                                                            // 0x10298ff6 -> 0x10273070
	++Spawn19RelinkCalls;                                                                // 0x10298ffc -> 0x101cf600 Relink

	FElysiumNpcBase::Spawn();                                                            // 0x10299006 -> 0x10273200

	InitialPosition = GetAbsOrigin();                                                    // 0x1029900f slot 217 -> +0x62a8
	InitialAngles = GetAbsAngles();                                                      // 0x10299033 slot 219 -> +0x62b4
	NPCInit();                                                                           // 0x10299057 slot 420
	ApplyDisciplineSpawnFlags();                                                         // 0x1029905f -> 0x1033df80

	RepairPoliceLevels();                                                                // 0x10299064..0x102990ed

	// The occluded-reaction ladder: a positive sum is renormalised into a CUMULATIVE ladder with
	// integer (truncating, `CDQ; IDIV`) division, chase forced to 100 first.
	const int32 Sum = PercentOccludedWalk + PercentOccludedCover + PercentOccludedWait
		+ PercentOccludedFlank + PercentOccludedChase;                                   // 0x102990f3..0x1029911c
	if (Sum > 0)                                                                         // 0x10299120 JLE
	{
		PercentOccludedChase = Spawn19OccludedHundred;                                   // 0x10299128
		int32 Running = PercentOccludedWait * Spawn19OccludedHundred / Sum;              // 0x10299138
		PercentOccludedWait = Running;                                                   // 0x10299142
		Running += PercentOccludedCover * Spawn19OccludedHundred / Sum;                  // 0x10299152 / 0x10299154
		PercentOccludedCover = Running;                                                  // 0x1029915c
		Running += PercentOccludedWalk * Spawn19OccludedHundred / Sum;                   // 0x1029916c / 0x1029916e
		PercentOccludedWalk = Running;                                                   // 0x10299174
		PercentOccludedFlank = PercentOccludedFlank * Spawn19OccludedHundred / Sum + Running; // 0x10299181 / 0x10299185
	}
	if (PercentOccludedChase != Spawn19OccludedHundred)                                  // 0x10299194
	{
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("Occluded target reaction percentages for %s (%f, %f, %f) do not add up to 100%%"),
			*DebugString(), InitialPosition.X, InitialPosition.Y, InitialPosition.Z);    // 0x102991b8 / 0x102991c3
		if (PercentOccludedChase <= 0)                                                   // 0x102991d4 JG
		{
			PercentOccludedWait = Spawn19OccludedDefaultWait;                            // 0x102991d6
			PercentOccludedCover = Spawn19OccludedDefaultCover;                          // 0x102991e0
			PercentOccludedWalk = Spawn19OccludedDefaultWalk;                            // 0x102991ea
			PercentOccludedFlank = Spawn19OccludedDefaultFlank;                          // 0x102991f4
			PercentOccludedChase = Spawn19OccludedHundred;                               // 0x102991fe
		}
	}

	// Each string is read through `STRING()`, the null default `DAT_106b8540` (`0x1029920c`,
	// `0x10299223`, `0x1029923a`); an empty `FString` is that default here.
	SetInterestingPlaceGroups(InterestingPlaceGroups);                                   // 0x10299216 -> 0x10298910
	SetHintGroups(ScheduleHost.HintGroups);                                              // 0x1029922d -> 0x102989e0
	CombatStartActivityId = ResolveCombatStartActivity(CombatStartActivity);             // 0x10299244 / 0x1029924f

	// `0x10207e60(this)`: `PrecacheModel(GetCharTemplate(this)->+0x78, 0)`.
	FPrecacheOp TemplateModel;
	TemplateModel.Channel = EPrecacheChannel::Model;
	TemplateModel.Name = CharTemplateModelName();
	TemplateModel.Flag = 0;
	IssuePrecache(TemplateModel);                                                        // 0x10299255

	AddFlag2(Spawn19TroikaFlags2);                                                       // 0x1029925e AddFlag2(4)
}

void FElysiumNpc::RepairPoliceLevels()
{
	if (PlInvestigate < 1)                                                               // 0x1029907c / 0x10368d2d
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Warning: Invalid m_iPLInvestigateLevel ('pl_investigate' in world craft)")); // 0x10299084 / 0x10368d35 DevMsg
		PlInvestigate = Spawn19PlLevelRepair;                                            // 0x10299089
	}
	if (PlCriminalFlee < 1)                                                              // 0x10299095 / 0x10368d46
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Warning: Invalid m_iPLCriminalFleeLevel ('pl_criminal_flee' in world craft)")); // 0x1029909d / 0x10368d4e DevMsg
		PlCriminalFlee = Spawn19PlLevelRepair;                                           // 0x102990a2
	}
	if (PlCriminalAttack < 1)                                                            // 0x102990ae / 0x10368d5f
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Warning: Invalid m_iPLCriminalAttackLevel ('pl_criminal_attack' in world craft)")); // 0x102990b6 / 0x10368d67 DevMsg
		PlCriminalAttack = Spawn19PlLevelRepair;                                         // 0x102990bb
	}
	if (PlSupernaturalFlee < 1)                                                          // 0x102990c7 / 0x10368d78
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Warning: Invalid m_iPLSupernaturalFleeLevel ('pl_supernatural_flee' in world craft)")); // 0x102990cf / 0x10368d80 DevMsg
		PlSupernaturalFlee = Spawn19PlLevelRepair;                                       // 0x102990d4
	}
	if (PlSupernaturalAttack < 1)                                                        // 0x102990e0 / 0x10368d91
	{
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Warning: Invalid m_iPLSupernaturalAttackLevel ('pl_supernatural_attack' in world craft)")); // 0x102990e8 / 0x10368d99 DevMsg
		PlSupernaturalAttack = Spawn19PlLevelRepair;                                     // 0x102990ed
	}
}

void FElysiumNpc::ApplyDisciplineSpawnFlags()
{
	// `0x1033df80`, the six arms in retail's order (the scope-trace frame is absent).
	const uint32 SpawnBits = static_cast<uint32>(SpawnFlags);
	if (((SpawnBits >> 5) & 1u) != 0u) { DisciplineContextTgtFlags |= 0x1; }
	if (((SpawnBits >> 6) & 1u) != 0u) { DisciplineContextTgtFlags |= 0x2; }
	if (((SpawnBits >> 0xc) & 1u) != 0u) { DisciplineContextTgtFlags |= 0x4; }
	if (((SpawnBits >> 0xd) & 1u) != 0u) { DisciplineContextTgtFlags |= 0x8; }
	if (((SpawnBits >> 0xe) & 1u) != 0u) { DisciplineContextTgtFlags |= 0x10; }
	if (((SpawnBits >> 0xf) & 1u) != 0u) { DisciplineContextTgtFlags |= 0x20; }
}

void FElysiumNpc::SetAbsoluteAttackExtents(const FVector& AbsoluteUnits)
{
	// `0x1009b060`: `extents = abs - (maxs - mins) * 0.5`, then slot 15 `SetAttackExtents`.
	const FVector HalfBoxUnits = (LastSetSizeMaxsUnits - LastSetSizeMinsUnits) * ElysiumNpcTunables::Half;
	SetAttackExtents((AbsoluteUnits - HalfBoxUnits) * ElysiumMove::U);
}

void FElysiumNpc::Spawn19SetModel(const FString& ModelName)
{
	TCHAR EmptyName[1] = { 0 };
	FString Copy = ModelName;
	TCHAR* const Name = Copy.IsEmpty() ? EmptyName : Copy.GetCharArray().GetData();
	SetModel(Name);
}
