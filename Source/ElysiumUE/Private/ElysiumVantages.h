#pragma once

#include "CoreMinimal.h"

// The fixed camera vantages the headless harnesses drive. Shared by FElysiumProfileRun (per-pass
// GPU timings) and FElysiumShotRun (screenshot regression) so a shot and its perf sample come from
// the exact same viewpoint — a look regression and a cost regression line up frame-for-frame.
//
// The frame cost and the framing of a VtMB scene are both view-dependent (how many of the map's
// hundreds of lights are in frustum), so a repeatable baseline needs a fixed camera. Each entry is
// keyed to a map (empty Map = any map). The map-agnostic "spawn" entry holds wherever the .spawn
// point drops the pawn; hand-picked vantages carry explicit world-cm position + rotation.
//
// The hand-picked points were read off the debug HUD (metres + yaw only), so position is
// metres*100 and pitch/roll default to level (0). Refine any point in-game with `elysium.campos`,
// which logs a paste-ready line carrying the exact pitch.
struct FElysiumVantage
{
	const TCHAR* Map;         // empty: applies to any map
	const TCHAR* Name;
	bool         bUseSpawn;   // true: read the live pawn transform instead of Loc/Rot
	FVector      Loc;         // world centimetres
	FRotator     Rot;         // pitch, yaw, roll (degrees)
};

namespace ElysiumVantages
{
	inline const FElysiumVantage Table[] =
	{
		{ TEXT(""),              TEXT("spawn"), true,  FVector::ZeroVector,              FRotator::ZeroRotator },

		// sp_tutorial_1 — four vantages near spawn (HUD "you" metres -> cm, yaw only).
		{ TEXT("sp_tutorial_1"), TEXT("t1"),   false, FVector( -60.f,  680.f,  70.f),   FRotator(0.f, -138.f, 0.f) },
		{ TEXT("sp_tutorial_1"), TEXT("t2"),   false, FVector(1320.f,  270.f,  60.f),   FRotator(0.f,  149.f, 0.f) },
		{ TEXT("sp_tutorial_1"), TEXT("t3"),   false, FVector(-1030.f, -100.f, 70.f),   FRotator(0.f,  -49.f, 0.f) },
		{ TEXT("sp_tutorial_1"), TEXT("t4"),   false, FVector(-2760.f, -600.f, 160.f),  FRotator(0.f, -154.f, 0.f) },
		// sm_hub_1 — two vantages.
		{ TEXT("sm_hub_1"),      TEXT("h1"),   false, FVector(-4220.f, 6170.f, -120.f), FRotator(0.f,  -40.f, 0.f) },
		{ TEXT("sm_hub_1"),      TEXT("h2"),   false, FVector(-4760.f, -1180.f, -120.f),FRotator(0.f,   29.f, 0.f) },

		// Sky vantages (sky-ambience B6): the level vantages above frame walls, so a sky change
		// barely moves their pixels. These pitch up from the same two points to put the backdrop
		// and the 3D-skybox miniature in frame together — the two things a sky regression breaks.
		// Both maps draw sky (`la` and `pier`) and both run the miniature pass.
		{ TEXT("sp_tutorial_1"), TEXT("t1sky"), false, FVector( -60.f,  680.f,  70.f),  FRotator(25.f, -138.f, 0.f) },
		{ TEXT("sm_hub_1"),      TEXT("h1sky"), false, FVector(-4220.f, 6170.f, -120.f),FRotator(28.f,  -40.f, 0.f) },

		// sm_pawnshop_1 — three interior vantages (HUD "you" metres -> cm, yaw only).
		{ TEXT("sm_pawnshop_1"), TEXT("p1"),   false, FVector(-4490.f, 6720.f, 530.f),  FRotator(0.f, -139.f, 0.f) },
		{ TEXT("sm_pawnshop_1"), TEXT("p2"),   false, FVector(-5160.f, 6300.f, 550.f),  FRotator(0.f,   95.f, 0.f) },
		{ TEXT("sm_pawnshop_1"), TEXT("p3"),   false, FVector(-5350.f, 6280.f, 530.f),  FRotator(0.f,  127.f, 0.f) },
	};

	inline constexpr int32 Num = UE_ARRAY_COUNT(Table);

	// Indices into Table that apply to Map (its own vantages plus the map-agnostic ones), optionally
	// narrowed to one by Selector (an index into the candidate list, or a vantage name; "" / "all"
	// keeps them all). Shared resolution so the profiler and the shot run pick identical vantages.
	inline void Resolve(const FString& Map, const FString& Selector, TArray<int32>& Out)
	{
		Out.Reset();
		TArray<int32> Candidates;
		for (int32 i = 0; i < Num; ++i)
		{
			const FString CamMap = Table[i].Map;
			if (CamMap.IsEmpty() || CamMap == Map)
			{
				Candidates.Add(i);
			}
		}

		if (!Selector.IsEmpty() && Selector != TEXT("all"))
		{
			if (Selector.IsNumeric())
			{
				const int32 Idx = FCString::Atoi(*Selector);
				if (Candidates.IsValidIndex(Idx))
				{
					Out = { Candidates[Idx] };
					return;
				}
			}
			else
			{
				for (int32 i : Candidates)
				{
					if (Selector.Equals(Table[i].Name, ESearchCase::IgnoreCase))
					{
						Out = { i };
						return;
					}
				}
			}
			// Selector missed: fall through to all candidates (the caller logs it).
		}

		Out = MoveTemp(Candidates);
	}
}
