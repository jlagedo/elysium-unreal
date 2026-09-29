// The witness verbs for the geometry seam (0018 story 6, lane D). Live-game only: they read the
// running map's entity world and its collision world.
//
//   elysium.geom.sightlane <npc targetname> <place targetname>...
//   elysium.geom.trace <x y z> <x y z> <mask>
//   elysium.geom.stand <npc targetname> <x y z>
//   elysium.geom.cover <npc targetname> <x y z>
//   elysium.geom.route <npc targetname> <x y z>
//
// `sightlane` is the before/after of the sight change: from the NPC's eye to each named place, the
// OLD +use channel (`ELYSIUM_USE_CHANNEL`, a plain line trace) beside the retail sight rule
// (`ElysiumNpcSight::Visible`, mask `0x2804091`, the NPC as looker, no target), one line per place and
// a summary of how many flipped and why. `trace` is one retail trace through the map's own
// `IElysiumEmbodiment::TraceRetail`. `stand`, `cover` and `route` put a question to a kernel body at a
// world point (centimetres) and print the inputs beside the answer.

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumUseIcons.h"
#include "ElysiumWorldCollisionActor.h"
#include "ElysiumWorldServices.h"
#include "Map/ElysiumRetailMaskRecipe.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSightTrace.h"
#include "Visual/ElysiumNpcBody.h"

#include "CollisionQueryParams.h"
#include "Engine/GameInstance.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodySetup.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGeomVerbs, Log, All);

namespace ElysiumGeometryVerbsPrivate
{
	// The retail sight mask every sight caller passes.
	constexpr int32 SightMask = 0x2804091;
	// `MASK_NPCSOLID`, the stand and cover probes' mask.
	constexpr int32 NpcSolidMask = 0x202400b;
	// Key of "the block had no contents signature" in the per-signature tallies.
	constexpr int32 NoSignature = -1;

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

	// Three consecutive arguments from `First` as a world point, centimetres.
	bool ParsePoint(const TArray<FString>& Args, int32 First, FVector& OutCm)
	{
		if (Args.Num() < First + 3 || !Args[First].IsNumeric() || !Args[First + 1].IsNumeric()
			|| !Args[First + 2].IsNumeric())
		{
			return false;
		}
		OutCm = FVector(FCString::Atod(*Args[First]), FCString::Atod(*Args[First + 1]),
			FCString::Atod(*Args[First + 2]));
		return true;
	}

	// The census spelling of a contents signature: P N S p for player, npc, sight, pedestrian.
	FString SignatureSpelling(int32 Signature)
	{
		if (Signature < 0)
		{
			return TEXT("no-signature");
		}
		return FString::Printf(TEXT("%c%c%c%c"), (Signature & 1) ? TEXT('P') : TEXT('-'),
			(Signature & 2) ? TEXT('N') : TEXT('-'), (Signature & 4) ? TEXT('S') : TEXT('-'),
			(Signature & 8) ? TEXT('p') : TEXT('-'));
	}

	// The centroid of one convex of a world-collision body, world centimetres. False = no such
	// element on that body.
	bool ElementCentroid(const UElysiumWorldCollisionComponent& Body, int32 Element, FVector& OutCm)
	{
		if (Body.Body == nullptr)
		{
			return false;
		}
		const TArray<FKConvexElem>& Elems = Body.Body->AggGeom.ConvexElems;
		if (!Elems.IsValidIndex(Element) || Elems[Element].VertexData.IsEmpty())
		{
			return false;
		}
		FVector Sum = FVector::ZeroVector;
		for (const FVector& Vertex : Elems[Element].VertexData)
		{
			Sum += Body.GetComponentTransform().TransformPosition(Vertex);
		}
		OutCm = Sum / Elems[Element].VertexData.Num();
		return true;
	}

	// The convex a retail trace result names (signature and element only), read off whichever
	// world-collision actor carries that signature. False = the level holds no such body.
	bool ConvexCentroid(UWorld* World, int32 Signature, int32 Element, FVector& OutCm)
	{
		if (World == nullptr || Signature < 0 || Element < 0)
		{
			return false;
		}
		for (TActorIterator<AElysiumWorldCollisionActor> It(World); It; ++It)
		{
			for (const UElysiumWorldCollisionComponent* Component : It->Bodies)
			{
				if (Component != nullptr && Component->Signature == Signature
					&& ElementCentroid(*Component, Element, OutCm))
				{
					return true;
				}
			}
		}
		return false;
	}

	// What a plain hit met, by whatever the hit itself names. A world-collision component reads as
	// its contents signature, element and centroid (`OutSignature` set); anything else -- a static
	// mesh, a prop, a mover -- reads as its actor label, class, component and collision profile
	// (`OutSignature` = -1), so a block by a body that has no signature is still placed.
	FString DescribeHit(const FHitResult& Hit, int32& OutSignature)
	{
		OutSignature = NoSignature;
		const UPrimitiveComponent* Component = Hit.GetComponent();
		const AActor* Actor = Hit.GetActor();
		if (const UElysiumWorldCollisionComponent* Body = Cast<UElysiumWorldCollisionComponent>(Component))
		{
			OutSignature = static_cast<int32>(Body->Signature);
			FVector Centroid;
			return FString::Printf(TEXT("sig %s (%d) element %d %s"), *SignatureSpelling(OutSignature),
				OutSignature, Hit.ElementIndex,
				ElementCentroid(*Body, Hit.ElementIndex, Centroid)
					? *FString::Printf(TEXT("centroid %s"), *VectorText(Centroid))
					: *FString::Printf(TEXT("hit %s"), *VectorText(Hit.ImpactPoint)));
		}
		return FString::Printf(TEXT("no signature: actor '%s' (%s) component '%s' profile '%s' at %s"),
			Actor != nullptr ? *Actor->GetActorNameOrLabel() : TEXT("<none>"),
			Actor != nullptr ? *Actor->GetClass()->GetName() : TEXT("-"),
			Component != nullptr ? *Component->GetName() : TEXT("<none>"),
			Component != nullptr ? *Component->GetCollisionProfileName().ToString() : TEXT("-"),
			*VectorText(Hit.ImpactPoint));
	}

	// What a retail trace result met (no component in hand): signature, element and centroid, or the
	// hit entity, or -- when the world answer is clear -- that.
	FString DescribeRetail(UWorld* World, const FElysiumEntityWorld* Entities,
		const FElysiumRetailTraceResult& Result)
	{
		if (Result.Fraction >= 1.f && !Result.bStartSolid)
		{
			return TEXT("clear");
		}
		FString Text = FString::Printf(TEXT("fraction %.3f"), Result.Fraction);
		if (Result.HitEntity.IsSet())
		{
			const FElysiumEntity* Hit = Entities != nullptr ? Entities->Resolve(Result.HitEntity) : nullptr;
			Text += FString::Printf(TEXT(" entity %s '%s'"), *Result.HitEntity.ToString(),
				Hit != nullptr ? *Hit->TargetName : TEXT("?"));
		}
		if (Result.HitSignature >= 0)
		{
			FVector Centroid;
			Text += FString::Printf(TEXT(" sig %s (%d) element %d %s"),
				*SignatureSpelling(Result.HitSignature), Result.HitSignature, Result.ElementIndex,
				ConvexCentroid(World, Result.HitSignature, Result.ElementIndex, Centroid)
					? *FString::Printf(TEXT("centroid %s"), *VectorText(Centroid))
					: *FString::Printf(TEXT("hit %s"), *VectorText(Result.EndPosCm)));
		}
		else
		{
			Text += FString::Printf(TEXT(" no signature at %s"), *VectorText(Result.EndPosCm));
		}
		return Text;
	}

	FElysiumNpc* ResolveNpc(FElysiumEntityWorld& Entities, const FString& Name, const TCHAR* Verb)
	{
		FElysiumEntity* Entity = Entities.FindByName(Name);
		FElysiumNpc* Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
		if (Npc == nullptr)
		{
			UE_LOG(LogElysiumGeomVerbs, Warning, TEXT("%s: no live NPC named '%s'"), Verb, *Name);
		}
		return Npc;
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
		const ECollisionChannel SightChannel = ElysiumRetailMask::Recipe(SightMask).Channel;

		// The plain traces must not start inside the NPC's own body: drop whatever the use channel
		// finds at the eye itself (the sight-channel trace shares the list).
		FCollisionQueryParams PlainParams(FName(TEXT("ElysiumGeomSightLane")), false);
		TArray<FOverlapResult> AtEye;
		World->OverlapMultiByChannel(AtEye, Eye, FQuat::Identity, ELYSIUM_USE_CHANNEL,
			FCollisionShape::MakeSphere(1.0f), PlainParams);
		for (const FOverlapResult& Overlap : AtEye)
		{
			if (AActor* Actor = Overlap.GetActor())
			{
				PlainParams.AddIgnoredActor(Actor);
			}
		}

		UE_LOG(LogElysiumGeomVerbs, Display, TEXT("sightlane: npc '%s' eye %s, %d place(s)"), *Args[0],
			*VectorText(Eye), Args.Num() - 1);
		int32 Compared = 0;
		int32 Flipped = 0;
		int32 UseBlockedSightClear = 0;
		int32 UseClearSightBlocked = 0;
		// How many places each channel was blocked by, per contents signature (-1: a body with none):
		// the two halves of story 3's pin read here -- the 17 sight-only brushes start blocking sight,
		// the window and grate brushes stop movement and now let it through.
		TMap<int32, int32> UseBlocksBySignature;
		TMap<int32, int32> SightBlocksBySignature;
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

			// (a) The old +use channel, blocker placed by actor AND by signature.
			FHitResult UseHit;
			const bool bUseBlocked = World->LineTraceSingleByChannel(UseHit, Eye, Target,
				ELYSIUM_USE_CHANNEL, PlainParams);
			FString UseText = TEXT("clear");
			if (bUseBlocked)
			{
				int32 UseSignature = NoSignature;
				const FString Described = DescribeHit(UseHit, UseSignature);
				UseText = FString::Printf(TEXT("blocked(%s: %s)"),
					UseHit.GetActor() != nullptr ? *UseHit.GetActor()->GetName() : TEXT("<no actor>"),
					*Described);
				++UseBlocksBySignature.FindOrAdd(UseSignature);
			}

			// (b) Retail's sight rule.
			ElysiumNpcSight::FVisibleQuery Query;
			Query.EyeCm = Eye;
			Query.TargetCm = Target;
			Query.Mask = SightMask;
			Query.Looker = Npc->Handle;
			Query.World = Entities;   // so a listed character's NPC-transparency resolves
			FElysiumEntityHandle Blocker;
			const bool bVisible = ElysiumNpcSight::Visible(*Embodiment, Query, &Blocker);
			FString SightText = TEXT("visible");
			if (!bVisible)
			{
				// The block, placed two ways. The retail trace: what `TraceRetail` says the same ray
				// meets. The sight channel: a plain line on the channel the mask's recipe names, whose
				// hit names its own component. They can differ (the kernel fold, a body that is not a
				// world-collision component), and a body with no signature is placed by actor label
				// and collision profile instead.
				FElysiumRetailTrace Request;
				Request.StartCm = Eye;
				Request.EndCm = Target;
				Request.RetailMask = SightMask;
				Request.Ignore.Add(Npc->Handle);
				FElysiumRetailTraceResult Result;
				Result.EndPosCm = Target;
				const bool bAnswered = Embodiment->TraceRetail(Request, Result);
				FString Retail = bAnswered ? DescribeRetail(World, Entities, Result)
					: FString(TEXT("no collision world answered"));
				if (Blocker.IsSet())
				{
					const FElysiumEntity* Hit = Entities->Resolve(Blocker);
					Retail += FString::Printf(TEXT(" [Visible's blocker: entity %s '%s']"),
						*Blocker.ToString(), Hit != nullptr ? *Hit->TargetName : TEXT("?"));
				}

				FHitResult SightHit;
				FString Plain = TEXT("clear");
				int32 SightSignature = NoSignature;
				if (World->LineTraceSingleByChannel(SightHit, Eye, Target, SightChannel, PlainParams))
				{
					Plain = DescribeHit(SightHit, SightSignature);
				}
				++SightBlocksBySignature.FindOrAdd(SightSignature);
				SightText = FString::Printf(TEXT("blocked(retail: %s; sight-channel: %s)"), *Retail, *Plain);
			}

			++Compared;
			// Flipped = the two channels disagree: use blocked and sight visible, or use clear and
			// sight blocked.
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
		for (const TPair<int32, int32>& Row : UseBlocksBySignature)
		{
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("sightlane: +use blocked %d place(s) by %s"),
				Row.Value, *SignatureSpelling(Row.Key));
		}
		for (const TPair<int32, int32>& Row : SightBlocksBySignature)
		{
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("sightlane: sight blocked %d place(s) by %s"),
				Row.Value, *SignatureSpelling(Row.Key));
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
		FVector Start;
		FVector End;
		if (Args.Num() != 7 || !ParsePoint(Args, 0, Start) || !ParsePoint(Args, 3, End)
			|| !ParseMask(Args[6], Mask))
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("usage: elysium.geom.trace <x y z> <x y z> <mask>  (world centimetres; mask 0x2804091 or decimal)"));
			return;
		}
		FElysiumRetailTrace Request;
		Request.StartCm = Start;
		Request.EndCm = End;
		Request.RetailMask = Mask;
		FElysiumRetailTraceResult Result;
		Result.EndPosCm = Request.EndCm;
		if (!Embodiment->TraceRetail(Request, Result))
		{
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("trace: no collision world answered (headless)"));
			return;
		}
		UE_LOG(LogElysiumGeomVerbs, Display,
			TEXT("trace %s -> %s mask 0x%x: fraction %.4f endpos %s normal %s startsolid %d allsolid %d hit: %s"),
			*VectorText(Request.StartCm), *VectorText(Request.EndCm), static_cast<uint32>(Mask),
			Result.Fraction, *VectorText(Result.EndPosCm), *VectorText(Result.Normal),
			Result.bStartSolid ? 1 : 0, Result.bAllSolid ? 1 : 0, *DescribeRetail(World, Entities, Result));
		for (const FElysiumRetailTraceCharacter& Character : Result.Characters)
		{
			const FElysiumEntity* Body = Entities->Resolve(Character.Entity);
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("trace:   character %s '%s' fraction %.4f%s"),
				*Character.Entity.ToString(), Body != nullptr ? *Body->TargetName : TEXT("?"),
				Character.Fraction, Character.bStartSolid ? TEXT(" startsolid") : TEXT(""));
		}
	}

	// `FElysiumNpc::CanStandAt` (`CAI_BaseNPCTroika::CanStandAt 0x102a0ed0`): the kernel's stand test
	// at a world point, mask `0x202400b`. `CanStandAt` takes Source units in the port's axes.
	void StandVerb(const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* Entities = CurrentEntityWorld(World);
		FVector Point;
		if (Entities == nullptr || Args.Num() != 4 || !ParsePoint(Args, 1, Point))
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("usage: elysium.geom.stand <npc targetname> <x y z>  (world centimetres; needs a live map)"));
			return;
		}
		FElysiumNpc* Npc = ResolveNpc(*Entities, Args[0], TEXT("elysium.geom.stand"));
		if (Npc == nullptr)
		{
			return;
		}
		const FVector Units = Point / ElysiumMove::U;
		const bool bStands = Npc->CanStandAt(Units, NpcSolidMask);
		UE_LOG(LogElysiumGeomVerbs, Display,
			TEXT("stand: npc '%s' origin %s, point %s cm = %s units, mask 0x%x -> %s"), *Args[0],
			*VectorText(Npc->Origin), *VectorText(Point), *VectorText(Units),
			static_cast<uint32>(NpcSolidMask), bStands ? TEXT("stands") : TEXT("no ground"));
	}

	// `FElysiumNpc::IsValidCover` (slot 548 `0x1028af20`) with no hint: a near-zero-length standing
	// hull probe at the point, mask `0x202400b`; false only when the box starts solid. Takes cm.
	void CoverVerb(const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* Entities = CurrentEntityWorld(World);
		FVector Point;
		if (Entities == nullptr || Args.Num() != 4 || !ParsePoint(Args, 1, Point))
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("usage: elysium.geom.cover <npc targetname> <x y z>  (world centimetres; needs a live map)"));
			return;
		}
		FElysiumNpc* Npc = ResolveNpc(*Entities, Args[0], TEXT("elysium.geom.cover"));
		if (Npc == nullptr)
		{
			return;
		}
		const bool bValid = Npc->IsValidCover(Point, nullptr);
		UE_LOG(LogElysiumGeomVerbs, Display,
			TEXT("cover: npc '%s' origin %s, point %s cm, hint none, mask 0x%x -> %s"), *Args[0],
			*VectorText(Npc->Origin), *VectorText(Point), static_cast<uint32>(NpcSolidMask),
			bValid ? TEXT("valid") : TEXT("refused (starts solid, or its hint group is unmet)"));
	}

	// The NPC's motor -- its body actor, which is the `IElysiumNpcMotor` (`FElysiumScriptedCharacter::
	// Motor` is protected, so the body is found by owning entity) -- asked `QueryRoute` and
	// `NavRaycast` from the NPC's feet to the point.
	void RouteVerb(const TArray<FString>& Args, UWorld* World)
	{
		FElysiumEntityWorld* Entities = CurrentEntityWorld(World);
		FVector Point;
		if (Entities == nullptr || Args.Num() != 4 || !ParsePoint(Args, 1, Point))
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("usage: elysium.geom.route <npc targetname> <x y z>  (world centimetres; needs a live map)"));
			return;
		}
		FElysiumNpc* Npc = ResolveNpc(*Entities, Args[0], TEXT("elysium.geom.route"));
		if (Npc == nullptr)
		{
			return;
		}
		const IElysiumNpcMotor* Motor = nullptr;
		for (TActorIterator<AElysiumNpcBody> It(World); It; ++It)
		{
			if (It->GetOwningEntity() == Npc->Handle)
			{
				Motor = *It;
				break;
			}
		}
		if (Motor == nullptr)
		{
			UE_LOG(LogElysiumGeomVerbs, Warning, TEXT("elysium.geom.route: '%s' has no body (no motor)"),
				*Args[0]);
			return;
		}
		const FVector From = Npc->Origin;
		UE_LOG(LogElysiumGeomVerbs, Display, TEXT("route: npc '%s' from %s to %s (%.1f cm straight)"),
			*Args[0], *VectorText(From), *VectorText(Point), FVector::Dist(From, Point));

		FElysiumNpcRouteQuery RouteQuery;
		RouteQuery.DestCm = Point;
		FElysiumNpcRouteAnswer Route;
		if (Motor->QueryRoute(RouteQuery, Route))
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("route: QueryRoute default filter, partial refused -> reachable %d partial %d length %.1f cm"),
				Route.bReachable ? 1 : 0, Route.bPartial ? 1 : 0, Route.LengthCm);
		}
		else
		{
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("route: QueryRoute: no NavMesh behind this motor"));
		}
		RouteQuery.bAcceptPartial = true;
		FElysiumNpcRouteAnswer Partial;
		if (Motor->QueryRoute(RouteQuery, Partial))
		{
			UE_LOG(LogElysiumGeomVerbs, Display,
				TEXT("route: QueryRoute default filter, partial accepted -> reachable %d partial %d length %.1f cm"),
				Partial.bReachable ? 1 : 0, Partial.bPartial ? 1 : 0, Partial.LengthCm);
		}

		FElysiumNpcNavRaycast RayQuery;
		RayQuery.FromCm = From;
		RayQuery.ToCm = Point;
		FElysiumNpcNavRaycastAnswer Ray;
		if (Motor->NavRaycast(RayQuery, Ray))
		{
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("route: NavRaycast -> hit %d%s"), Ray.bHit ? 1 : 0,
				Ray.bHit ? *FString::Printf(TEXT(" at %s"), *VectorText(Ray.HitCm)) : TEXT(""));
		}
		else
		{
			UE_LOG(LogElysiumGeomVerbs, Display, TEXT("route: NavRaycast: no NavMesh behind this motor"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GElysiumGeomSightLaneCmd(
	TEXT("elysium.geom.sightlane"),
	TEXT("elysium.geom.sightlane <npc targetname> <place targetname>... -- from the NPC's eye to each place, the old +use trace "
		"beside retail's sight mask 0x2804091 (ElysiumNpcSight::Visible); one line per place, blocks placed by signature, then how many flipped and why"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ElysiumGeometryVerbsPrivate::SightLane));

static FAutoConsoleCommandWithWorldAndArgs GElysiumGeomTraceCmd(
	TEXT("elysium.geom.trace"),
	TEXT("elysium.geom.trace <x y z> <x y z> <mask> -- one retail trace through the map's TraceRetail (world cm; mask 0x2804091 "
		"or decimal): fraction, endpos, hit body (signature element centroid), characters met"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ElysiumGeometryVerbsPrivate::TraceVerb));

static FAutoConsoleCommandWithWorldAndArgs GElysiumGeomStandCmd(
	TEXT("elysium.geom.stand"),
	TEXT("elysium.geom.stand <npc targetname> <x y z> -- the kernel body's CanStandAt at a world point (cm), mask 0x202400b"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ElysiumGeometryVerbsPrivate::StandVerb));

static FAutoConsoleCommandWithWorldAndArgs GElysiumGeomCoverCmd(
	TEXT("elysium.geom.cover"),
	TEXT("elysium.geom.cover <npc targetname> <x y z> -- the kernel body's IsValidCover(point, no hint) at a world point (cm)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ElysiumGeometryVerbsPrivate::CoverVerb));

static FAutoConsoleCommandWithWorldAndArgs GElysiumGeomRouteCmd(
	TEXT("elysium.geom.route"),
	TEXT("elysium.geom.route <npc targetname> <x y z> -- the NPC motor's QueryRoute (reachable, partial, length cm) and NavRaycast from the NPC to a world point (cm)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ElysiumGeometryVerbsPrivate::RouteVerb));
