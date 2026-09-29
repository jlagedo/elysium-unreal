// The witness verbs for the geometry seam (0018 story 6, lane D). Live-game only: they read the
// running map's entity world and its collision world.
//
//   elysium.geom.sightlane <npc targetname> <place targetname>...
//   elysium.geom.trace <x y z> <x y z> <mask>
//
// `sightlane` is the before/after of the sight change: from the NPC's eye to each named place, the
// OLD +use channel (`ELYSIUM_USE_CHANNEL`, a plain line trace) beside the retail sight rule
// (`ElysiumNpcSight::Visible`, mask `0x2804091`, the NPC as looker, no target), one line per place and
// a summary of how many flipped and why. `trace` is one retail trace through the map's own
// `IElysiumEmbodiment::TraceRetail`, in world centimetres, for a check at a coordinate.

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumUseIcons.h"
#include "ElysiumWorldCollisionActor.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcSightTrace.h"

#include "CollisionQueryParams.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGeomVerbs, Log, All);

namespace ElysiumGeometryVerbsPrivate
{
	FElysiumEntityWorld* CurrentEntityWorld(UWorld* World)
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		if (const UGameInstance* Instance = World->GetGameInstance())
		{
			if (UElysiumMapSubsystem* Maps = Instance->GetSubsystem<UElysiumMapSubsystem>())
			{
				if (AElysiumMapActor* Map = Maps->GetCurrentMap())
				{
					return Map->GetEntityWorld();
				}
			}
		}
		return nullptr;
	}

	FString VectorText(const FVector& Cm)
	{
		return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Cm.X, Cm.Y, Cm.Z);
	}

	// A retail mask as typed: `0x2804091` or decimal.
	bool ParseMask(const FString& Text, int32& OutMask)
	{
		if (Text.IsEmpty())
		{
			return false;
		}
		if (Text.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
		{
			OutMask = static_cast<int32>(FCString::Strtoi64(*Text.Mid(2), nullptr, 16));
			return Text.Len() > 2;
		}
		if (!Text.IsNumeric())
		{
			return false;
		}
		OutMask = FCString::Atoi(*Text);
		return true;
	}

	// The convex a hit names: its centroid, read off the collision actor the map stood, so a blocker
	// is placed by where the brush IS and not only by where the ray met it. False = the map holds no
	// such body (a transient collider, an entity's brush), and the caller prints the hit point.
	bool ConvexCentroid(UWorld* World, int32 Signature, int32 Element, FVector& OutCentroidCm)
	{
		if (World == nullptr || Signature < 0 || Element < 0)
		{
			return false;
		}
		for (TActorIterator<AElysiumWorldCollisionActor> It(World); It; ++It)
		{
			for (const UElysiumWorldCollisionComponent* Component : It->Bodies)
			{
				if (Component == nullptr || Component->Body == nullptr
					|| Component->Signature != Signature)
				{
					continue;
				}
				const TArray<FKConvexElem>& Elems = Component->Body->AggGeom.ConvexElems;
				if (!Elems.IsValidIndex(Element) || Elems[Element].VertexData.IsEmpty())
				{
					continue;
				}
				FVector Sum = FVector::ZeroVector;
				for (const FVector& Vertex : Elems[Element].VertexData)
				{
					Sum += Component->GetComponentTransform().TransformPosition(Vertex);
				}
				OutCentroidCm = Sum / Elems[Element].VertexData.Num();
				return true;
			}
		}
		return false;
	}

	FString DescribeBlock(UWorld* World, const FElysiumRetailTraceResult& Trace)
	{
		FVector Centroid;
		const FString Where = ConvexCentroid(World, Trace.HitSignature, Trace.ElementIndex, Centroid)
			? FString::Printf(TEXT("centroid %s"), *VectorText(Centroid))
			: FString::Printf(TEXT("hit %s"), *VectorText(Trace.EndPosCm));
		return FString::Printf(TEXT("%d %d %s"), Trace.HitSignature, Trace.ElementIndex, *Where);
	}

	void SightLane(const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* Entities = CurrentEntityWorld(World);
		if (Entities == nullptr)
		{
			UE_LOG(LogElysiumGeomVerbs, Warning, TEXT("elysium.geom.sightlane: no live world (load a map first)"));
			return;
		}
		IElysiumEmbodiment* Embodiment = Entities->Embodiment();
		if (Args.Num() < 2)
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("usage: elysium.geom.sightlane <npc targetname> <place targetname>..."));
			return;
		}
		if (Embodiment == nullptr)
		{
			UE_LOG(LogElysiumGeomVerbs, Warning, TEXT("elysium.geom.sightlane: the world has no embodiment"));
			return;
		}
		const FElysiumEntity* Npc = Entities->FindByName(Args[0]);
		if (Npc == nullptr)
		{
			UE_LOG(LogElysiumGeomVerbs, Warning, TEXT("elysium.geom.sightlane: no live entity named '%s'"),
				*Args[0]);
			return;
		}
		// The NPC's eye as the kernel reads it: `EyePosition()`, `GetAbsOrigin() + m_vecViewOffset`
		// (the standing view offset for a character), in world centimetres.
		const FVector Eye = Npc->EyePosition();

		// The +use trace must not start inside the NPC's own body: drop whatever the use channel
		// finds at the eye itself.
		FCollisionQueryParams UseParams(FName(TEXT("ElysiumGeomSightLane")), false);
		TArray<FOverlapResult> AtEye;
		World->OverlapMultiByChannel(AtEye, Eye, FQuat::Identity, ELYSIUM_USE_CHANNEL,
			FCollisionShape::MakeSphere(1.0f), UseParams);
		for (const FOverlapResult& Overlap : AtEye)
		{
			if (AActor* Actor = Overlap.GetActor())
			{
				UseParams.AddIgnoredActor(Actor);
			}
		}

		UE_LOG(LogElysiumGeomVerbs, Display, TEXT("sightlane: npc '%s' eye %s, %d place(s)"), *Args[0],
			*VectorText(Eye), Args.Num() - 1);
		int32 Compared = 0;
		int32 Flipped = 0;
		int32 UseBlockedSightClear = 0;
		int32 UseClearSightBlocked = 0;
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			const FElysiumEntity* Place = Entities->FindByName(Args[Index]);
			if (Place == nullptr)
			{
				UE_LOG(LogElysiumGeomVerbs, Warning, TEXT("sightlane: place '%s': no live entity"),
					*Args[Index]);
				continue;
			}
			const FVector Target = Place->Origin;

			FHitResult UseHit;
			const bool bUseBlocked = World->LineTraceSingleByChannel(UseHit, Eye, Target,
				ELYSIUM_USE_CHANNEL, UseParams);
			const FString UseText = bUseBlocked
				? FString::Printf(TEXT("blocked(%s)"),
					UseHit.GetActor() != nullptr ? *UseHit.GetActor()->GetName() : TEXT("<no actor>"))
				: FString(TEXT("clear"));

			ElysiumNpcSight::FVisibleQuery Query;
			Query.EyeCm = Eye;
			Query.TargetCm = Target;
			Query.Mask = 0x2804091;
			Query.Looker = Npc->Handle;
			Query.World = Entities;   // so a listed character's NPC-transparency resolves
			FElysiumEntityHandle Blocker;
			const bool bVisible = ElysiumNpcSight::Visible(*Embodiment, Query, &Blocker);
			FString SightText = TEXT("visible");
			if (!bVisible)
			{
				// What the ray met, by the same retail trace: signature, element and where.
				FElysiumRetailTrace Request;
				Request.StartCm = Eye;
				Request.EndCm = Target;
				Request.RetailMask = Query.Mask;
				Request.Ignore.Add(Npc->Handle);
				FElysiumRetailTraceResult Result;
				Result.EndPosCm = Target;
				const bool bAnswered = Embodiment->TraceRetail(Request, Result);
				if (Blocker.IsSet())
				{
					const FElysiumEntity* Hit = Entities->Resolve(Blocker);
					SightText = FString::Printf(TEXT("blocked(entity %s '%s')"), *Blocker.ToString(),
						Hit != nullptr ? *Hit->TargetName : TEXT("?"));
				}
				else
				{
					SightText = FString::Printf(TEXT("blocked(%s)"),
						bAnswered && Result.HitSignature >= 0 ? *DescribeBlock(World, Result)
							: TEXT("static world"));
				}
			}

			++Compared;
			// Flipped = the two channels disagree: use blocked and sight visible, or use clear and sight blocked.
			const bool bFlipped = bUseBlocked == bVisible;
			if (bFlipped)
			{
				++Flipped;
				if (bUseBlocked)
				{
					++UseBlockedSightClear;
				}
				else
				{
					++UseClearSightBlocked;
				}
			}
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("sightlane: %s, use: %s, sight: %s%s"),
				*Args[Index], *UseText, *SightText, bFlipped ? TEXT("  [flipped]") : TEXT(""));
		}
		UE_LOG(LogElysiumGeomVerbs, Display,
			TEXT("sightlane: %d place(s) compared, %d flipped: %d use-blocked but sight-clear (the +use "
				"channel met a body retail's sight does not: a brush the sight mask lets through, or an "
				"actor that is no solid prop), %d use-clear but sight-blocked (retail's sight mask meets a "
				"sight-only brush or a solid prop the +use channel ignores)"),
			Compared, Flipped, UseBlockedSightClear, UseClearSightBlocked);
	}

	void TraceVerb(const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* Entities = CurrentEntityWorld(World);
		const IElysiumEmbodiment* Embodiment = Entities != nullptr ? Entities->Embodiment() : nullptr;
		if (Embodiment == nullptr)
		{
			UE_LOG(LogElysiumGeomVerbs, Warning, TEXT("elysium.geom.trace: no live world (load a map first)"));
			return;
		}
		int32 Mask = 0;
		if (Args.Num() != 7 || !ParseMask(Args[6], Mask))
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("usage: elysium.geom.trace <x y z> <x y z> <mask>  (world centimetres; mask 0x2804091 or decimal)"));
			return;
		}
		FElysiumRetailTrace Request;
		Request.StartCm = FVector(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		Request.EndCm = FVector(FCString::Atod(*Args[3]), FCString::Atod(*Args[4]), FCString::Atod(*Args[5]));
		Request.RetailMask = Mask;
		FElysiumRetailTraceResult Result;
		Result.EndPosCm = Request.EndCm;
		const bool bAnswered = Embodiment->TraceRetail(Request, Result);
		if (!bAnswered)
		{
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("trace: no collision world answered (headless)"));
			return;
		}
		UE_LOG(LogElysiumGeomVerbs, Display,
			TEXT("trace %s -> %s mask 0x%x: fraction %.4f endpos %s normal %s startsolid %d allsolid %d "
				"hit-entity %s hit-body %s"),
			*VectorText(Request.StartCm), *VectorText(Request.EndCm), static_cast<uint32>(Mask),
			Result.Fraction, *VectorText(Result.EndPosCm), *VectorText(Result.Normal),
			Result.bStartSolid ? 1 : 0, Result.bAllSolid ? 1 : 0, *Result.HitEntity.ToString(),
			Result.HitSignature >= 0 ? *DescribeBlock(World, Result) : TEXT("none"));
		for (const FElysiumRetailTraceCharacter& Character : Result.Characters)
		{
			const FElysiumEntity* Body = Entities->Resolve(Character.Entity);
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("trace:   character %s '%s' fraction %.4f%s"),
				*Character.Entity.ToString(), Body != nullptr ? *Body->TargetName : TEXT("?"),
				Character.Fraction, Character.bStartSolid ? TEXT(" startsolid") : TEXT(""));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumGeomSightLaneCmd(
	TEXT("elysium.geom.sightlane"),
	TEXT("elysium.geom.sightlane <npc targetname> <place targetname>... -- from the NPC's eye to each place, the old +use trace "
		"beside retail's sight mask 0x2804091 (ElysiumNpcSight::Visible); one line per place, then how many flipped and why"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ElysiumGeometryVerbsPrivate::SightLane));

static FAutoConsoleCommandWithWorldAndArgs GElysiumGeomTraceCmd(
	TEXT("elysium.geom.trace"),
	TEXT("elysium.geom.trace <x y z> <x y z> <mask> -- one retail trace through the map's TraceRetail (world cm; mask 0x2804091 "
		"or decimal): fraction, endpos, hit body (signature element centroid), characters met"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ElysiumGeometryVerbsPrivate::TraceVerb));
