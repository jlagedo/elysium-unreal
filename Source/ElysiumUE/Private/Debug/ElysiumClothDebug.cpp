#include "Debug/ElysiumClothDebug.h"

#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "ChaosClothAsset/ClothSimulationModel.h"
#include "ChaosClothAsset/ClothSimulationProxy.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ElysiumClothDebug
{
	namespace
	{
		const FDrawToggle DrawToggleTable[] = {
			{ TEXT("AnimMeshWired"), "Anim mesh",
			  "The pose the garment is skinned FROM. Wrong here means bones or leader pose, not\n"
			  "cloth -- no solver setting can move it." },
			{ TEXT("PhysMeshWired"), "Sim mesh", "The particles the solver actually moves." },
			{ TEXT("MaxDistances"), "Max distances",
			  "Per particle, how far it may leave the anim mesh. A pinned particle draws nothing." },
			{ TEXT("MaxDistanceValues"), "  ...as values", "The same, as numbers." },
			{ TEXT("LongRangeConstraint"), "Tethers",
			  "Long range attachment. Nothing drawn means nothing holds the garment on the body." },
			{ TEXT("Collision"), "Collision", "The physics asset bodies the garment collides with." },
			{ TEXT("Backstops"), "Backstops", "The inner limit each particle is pushed back out to." },
			{ TEXT("AnimDrive"), "Anim drive", "The pull back toward the skinned pose." },
			{ TEXT("EdgeConstraint"), "Edges", "The stretch constraint set." },
			{ TEXT("BendingConstraint"), "Bending", "The bend constraint set." },
			{ TEXT("SelfCollision"), "Self collision", "Surface-against-itself contacts." },
			{ TEXT("Bounds"), "Bounds", "What the renderer culls the garment against." },
			{ TEXT("Gravity"), "Gravity", "Direction and scale, after the config's own multiplier." },
			{ TEXT("PointVelocities"), "Velocities", "Per particle, this frame." },
			{ TEXT("PointNormals"), "Normals", "The simulated surface's normals." },
			{ TEXT("LocalSpace"), "Local space", "The bone the solver simulates relative to." },
			{ TEXT("TeleportReset"), "Teleport/reset", "Flashes on the frames the solver was reset." },
			{ TEXT("ParticleIndices"), "Particle indices", "Index labels, for reading a report back." },
		};
	}

	TConstArrayView<FDrawToggle> DrawToggles()
	{
		return TConstArrayView<FDrawToggle>(DrawToggleTable);
	}

	IConsoleVariable* DrawCVar(const TCHAR* Suffix)
	{
		return IConsoleManager::Get().FindConsoleVariable(
			*(FString(TEXT("p.ChaosCloth.DebugDraw")) + Suffix));
	}

	namespace
	{
		IConsoleVariable* MasterCVar()
		{
			return IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.DebugDraw.Enabled"));
		}
	}

	bool IsDrawEnabled()
	{
		const IConsoleVariable* const Master = MasterCVar();
		return Master != nullptr && Master->GetBool();
	}

	void SetDrawEnabled(bool bEnabled)
	{
		if (IConsoleVariable* const Master = MasterCVar())
		{
			Master->Set(bEnabled, ECVF_SetByConsole);
		}
	}

	void SetDraw(const TCHAR* Suffix, bool bOn)
	{
		if (IConsoleVariable* const CVar = DrawCVar(Suffix))
		{
			CVar->Set(bOn, ECVF_SetByConsole);
		}
		if (bOn)
		{
			SetDrawEnabled(true);
		}
	}

	int32 NumActiveDraws()
	{
		int32 Active = 0;
		for (const FDrawToggle& Toggle : DrawToggleTable)
		{
			const IConsoleVariable* const CVar = DrawCVar(Toggle.Suffix);
			Active += (CVar != nullptr && CVar->GetBool()) ? 1 : 0;
		}
		return Active;
	}

	void ClearDraws()
	{
		for (const FDrawToggle& Toggle : DrawToggleTable)
		{
			if (IConsoleVariable* const CVar = DrawCVar(Toggle.Suffix))
			{
				CVar->Set(false, ECVF_SetByConsole);
			}
		}
	}
}

#if !UE_BUILD_SHIPPING

namespace
{
	/**
	 * What the BUILT model carries for one garment, which is what the solver receives.
	 *
	 * Deliberately not the numbers the generator wrote: every stage between a cloth collection and
	 * a simulation model drops rather than complains — a compaction renumbers, a facade copies only
	 * its own schema, a property whose absence disables its constraint. A garment reporting its
	 * particles and none of the rest is a build defect, not a tuning one.
	 */
	void ReportGarment(const UChaosClothComponent& Cloth, FOutputDevice& Ar)
	{
		const AActor* const Owner = Cloth.GetOwner();
		const UChaosClothAsset* const Asset = Cast<UChaosClothAsset>(Cloth.GetAsset());
		Ar.Logf(TEXT("%s  asset=%s"), *GetNameSafe(Owner), *GetNameSafe(Asset));

		// A garment with no leader pose does not follow the body at all; it is the first thing to
		// rule out because it looks exactly like a simulation failure.
		Ar.Logf(TEXT("  leader pose: %s   simulation: %s"),
			Cloth.LeaderPoseComponent.IsValid() ? TEXT("bound") : TEXT("NONE"),
			Cloth.IsSimulationSuspended() ? TEXT("suspended")
				: Cloth.IsSimulationEnabled() ? TEXT("running") : TEXT("disabled"));

		const TSharedPtr<const FChaosClothSimulationModel> Model =
			Asset != nullptr ? Asset->GetClothSimulationModel() : nullptr;
		if (!Model.IsValid() || !Model->IsValidLodIndex(0))
		{
			Ar.Logf(TEXT("  built model: NONE"));
			return;
		}

		const FChaosClothSimulationLodModel& Lod = Model->ClothSimulationLodModels[0];
		int32 Kinematic = 0;
		if (const TArray<float>* const MaxDistance = Lod.WeightMaps.Find(FName(TEXT("MaxDistance"))))
		{
			for (const float Value : *MaxDistance)
			{
				// The solver's own threshold, not a tolerance of ours.
				Kinematic += Value < 0.1f ? 1 : 0;
			}
		}
		int32 Tethers = 0;
		for (const TArray<TTuple<int32, int32, float>>& Batch : Lod.TetherData.Tethers)
		{
			Tethers += Batch.Num();
		}
		Ar.Logf(TEXT("  built: %d vertices, %d triangles, %d kinematic, %d tethers, %d weight maps"),
			Model->GetNumVertices(0), Model->GetNumTriangles(0), Kinematic, Tethers,
			Lod.WeightMaps.Num());
		if (Tethers == 0)
		{
			Ar.Logf(TEXT("  no tethers - nothing holds this garment on the body"));
		}
		if (Kinematic == 0)
		{
			Ar.Logf(TEXT("  no kinematic vertices - the garment is in free fall"));
		}

		if (const UE::Chaos::ClothAsset::FClothSimulationProxy* const Proxy =
				Cloth.GetClothSimulationProxy())
		{
			// The live counterpart of the counts above. They disagree when the solver rejected
			// something the model carries — which is the whole class of failure that reads as a
			// tuning problem and is not one.
			Ar.Logf(TEXT("  live: %d kinematic, %d dynamic, %d iterations, %d substeps, %.2f ms"),
				Proxy->GetNumKinematicParticles(), Proxy->GetNumDynamicParticles(),
				Proxy->GetNumIterations(), Proxy->GetNumSubsteps(),
				Proxy->GetSimulationTime());
		}
	}

	void ClothCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Ar)
	{
		using namespace ElysiumClothDebug;

		const TConstArrayView<FDrawToggle> Toggles = DrawToggles();

		if (Args.Num() >= 1 && Args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			for (const FDrawToggle& Toggle : Toggles)
			{
				const IConsoleVariable* const CVar = DrawCVar(Toggle.Suffix);
				Ar.Logf(TEXT("  %s %-22s %s"),
					CVar == nullptr ? TEXT("--") : CVar->GetBool() ? TEXT("on") : TEXT("  "),
					Toggle.Suffix, ANSI_TO_TCHAR(Toggle.Help));
			}
			return;
		}

		if (Args.Num() >= 1 && Args[0].Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			ClearDraws();
			Ar.Logf(TEXT("cloth overlays cleared"));
			return;
		}

		if (Args.Num() >= 1 && Args[0].Equals(TEXT("draw"), ESearchCase::IgnoreCase))
		{
			if (Args.Num() < 2)
			{
				Ar.Logf(TEXT("elysium.garment draw <name> [0|1]   (names: elysium.garment list)"));
				return;
			}
			// Matched anywhere in the name so the whole cvar suffix never has to be typed:
			// `anim` reaches AnimMeshWired, `range` reaches LongRangeConstraint.
			const FDrawToggle* Match = nullptr;
			for (const FDrawToggle& Toggle : Toggles)
			{
				if (FCString::Strifind(Toggle.Suffix, *Args[1]) != nullptr)
				{
					Match = &Toggle;
					break;
				}
			}
			if (Match == nullptr)
			{
				Ar.Logf(TEXT("no overlay matching '%s' - elysium.garment list"), *Args[1]);
				return;
			}
			IConsoleVariable* const CVar = DrawCVar(Match->Suffix);
			if (CVar == nullptr)
			{
				Ar.Logf(TEXT("%s is not available in this build"), Match->Suffix);
				return;
			}
			const bool bOn = Args.Num() >= 3 ? (FCString::Atoi(*Args[2]) != 0) : !CVar->GetBool();
			SetDraw(Match->Suffix, bOn);
			Ar.Logf(TEXT("%s %s"), Match->Suffix, bOn ? TEXT("on") : TEXT("off"));
			return;
		}

		// No arguments: the state of every garment in the world, then the overlays that are on.
		int32 Found = 0;
		if (World != nullptr)
		{
			for (TObjectIterator<UChaosClothComponent> It; It; ++It)
			{
				if (It->GetWorld() == World && IsValid(*It))
				{
					ReportGarment(**It, Ar);
					++Found;
				}
			}
		}
		if (Found == 0)
		{
			Ar.Logf(TEXT("no garment components in this world"));
		}
		Ar.Logf(TEXT("overlays on: %d, chaos debug draw %s   "
			"(elysium.garment list | draw <name> | none)"),
			NumActiveDraws(), IsDrawEnabled() ? TEXT("on") : TEXT("OFF"));
	}
}

// `garment` rather than `cloth` so the verb never collides with a `Cloth` cvar: console names are
// case-insensitive, and registering a command over a variable of the same name is fatal at
// startup rather than a warning.
static FAutoConsoleCommandWithWorldArgsAndOutputDevice GElysiumGarmentCommand(
	TEXT("elysium.garment"),
	TEXT("Garment state and Chaos cloth overlays. `elysium.garment` reports what every built "
		 "garment carries; `list` names the overlays; `draw <name> [0|1]` toggles one; `none` "
		 "clears them."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ClothCommand));

#endif  // !UE_BUILD_SHIPPING
