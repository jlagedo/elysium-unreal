#include "Map/ElysiumWorldGeometry.h"

#include "ElysiumBrushComponent.h"
#include "ElysiumCollisionChannels.h"
#include "ElysiumWorldCollisionActor.h"
#include "Map/ElysiumRetailMaskRecipe.h"
#include "ElysiumRetailSite.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "NavigationData.h"
#include "NavigationSystemTypes.h"

// 0018 story 6, wave 2: the Unreal queries behind the geometry seam. The map actor's `TraceRetail`
// and the NPC body's `QueryRoute` / `NavRaycast` resolve their own inputs and land here.

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGeometry, Log, All);

static TAutoConsoleVariable<float> CVarElysiumGeomBudgetLogSeconds(
	TEXT("elysium.geom.BudgetLogSeconds"), 0.f,
	TEXT("0018 story 6: log the geometry seam's cost (peak per-frame path tests, totals, ms) to ")
	TEXT("LogElysiumGeometry once per this many seconds. 0 = off."),
	ECVF_Default);

namespace ElysiumWorldGeometryDetail
{
	// A rejected world hit is excluded and the query asked again: Unreal's single and multi traces
	// both stop at the FIRST blocking hit, so a hit the recipe does not admit (an ignored entity's
	// body, a character missing its mask bit) cannot be skipped by reading further down one result
	// list. Bounded so a pathological pile cannot spin; past the bound the trace answers clear.
	constexpr int32 MaxWorldRetraces = 32;

	// The contact tolerance: Source's `DIST_EPSILON`, 1/32 unit (0.079375 cm). Source's box trace
	// stops that far short of a plane, so a box resting on a floor top does not start in solid.
	// Unreal's sweep with `bFindInitialOverlaps` can report zero-separation contact as
	// `bStartPenetrating`, which would read as `startsolid` and refuse every stand, cover and fit
	// test on flat ground. So the box is asked SHRUNK by this much on every side: a body within
	// DIST_EPSILON of the box (touching it, or up to 1/32 unit into it) is contact, not solid. A hit
	// the shrunk box makes is backed off along the ray until the full box stands DIST_EPSILON off the
	// surface again, which is where Source's endpos stands. A line has no box and is asked as is.
	//
	// The shrink alone does not settle it: Chaos can still report a shrunk box on cooked convex
	// bodies as overlapping. So every start-in-solid candidate is measured as well: the component's
	// own penetration test (`ComputePenetration`, the deepest penetration over ALL its shapes) with
	// the FULL box. At most DIST_EPSILON deep is contact, not solid; deeper is solid. Retail's own
	// box-in-brush test calls any positive depth solid (and a zero-depth touch clear); the 1/32-unit
	// allowance over that absorbs cooking and float noise and is named as that.
	constexpr double ContactToleranceCm = 0.03125 * 2.54;

	// The retail ray, as Unreal sweeps it. Source's `Ray_t(start, end, mins, maxs)` places the box
	// RELATIVE to the traced point; Unreal's shapes are centred on it. So the query runs from
	// `start + centre` to `end + centre` and the reported position has `centre` taken off again.
	struct FRetailShape
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector Centre = FVector::ZeroVector;
		FCollisionShape Shape;
		// The unshrunk box, for the penetration measure (a point-sized box for a zero box).
		FCollisionShape FullShape;
		// A zero box: a line.
		bool bLine = true;
		// `start == end`: an overlap (`IsAreaClear 0x102a0fb0` traces `pos -> pos`).
		bool bOverlap = false;
	};

	FRetailShape ShapeOf(const FElysiumRetailTrace& Request)
	{
		FRetailShape Out;
		Out.Centre = (Request.MinsCm + Request.MaxsCm) * 0.5;
		const FVector Extent = ((Request.MaxsCm - Request.MinsCm) * 0.5).GetAbs();
		Out.Start = Request.StartCm + Out.Centre;
		Out.End = Request.EndCm + Out.Centre;
		Out.bLine = Extent.IsNearlyZero();
		Out.bOverlap = Request.StartCm.Equals(Request.EndCm);
		// The box, less the contact tolerance on every side (never below a point).
		const FVector Asked = Out.bLine
			? Extent : (Extent - FVector(ContactToleranceCm)).ComponentMax(FVector::ZeroVector);
		// An overlap needs a volume to ask about: a zero box at one point is asked as a point-sized
		// box. No recovered retail caller traces a zero box from a point to itself.
		Out.Shape = FCollisionShape::MakeBox(
			Out.bOverlap ? Asked.ComponentMax(FVector(0.01)) : Asked);
		Out.FullShape = FCollisionShape::MakeBox(Extent.ComponentMax(FVector(0.01)));
		return Out;
	}

	// The entity behind one hit body. A brush entity's body is a component of the map actor, so
	// the actor cannot name it; the component carries its own handle.
	FElysiumEntityHandle HandleOf(const UPrimitiveComponent* Component, const AActor* Actor,
		TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
	{
		if (const UElysiumBrushComponent* Brush = Cast<UElysiumBrushComponent>(Component))
		{
			return Brush->GetOwningEntity();
		}
		return Actor != nullptr ? ToHandle(Actor) : FElysiumEntityHandle::Invalid();
	}

	enum class EAdmit : uint8
	{
		Admit,
		RejectComponent,
		RejectActor,
	};

	// Whether the world answer keeps one blocking body.
	EAdmit AdmitWorldHit(const UPrimitiveComponent* Component, const AActor* Actor,
		const FElysiumRetailMaskRecipe& Recipe, const FElysiumRetailTrace& Request,
		TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
	{
		// Characters are never folded into the world answer. `CharacterMaskBit` already drops them
		// (the NPC capsule, the player's hull); this guards a character body that does not wear it.
		if (Actor != nullptr && Actor->IsA<APawn>())
		{
			return EAdmit::RejectActor;
		}
		// The filter's pass entity and its second (`CTraceFilterSimpleTwoEnt`), whichever body
		// they stand in. Named only when there is someone to ignore: `ToHandle` may walk the entity
		// world for an actor it does not recognise.
		if (Request.Ignore.IsEmpty())
		{
			return EAdmit::Admit;
		}
		const FElysiumEntityHandle Entity = HandleOf(Component, Actor, ToHandle);
		if (Entity.IsSet() && Request.Ignore.Contains(Entity))
		{
			return EAdmit::RejectComponent;
		}
		return EAdmit::Admit;
	}

	void Exclude(EAdmit Verdict, const UPrimitiveComponent* Component, const AActor* Actor,
		FCollisionQueryParams& Params)
	{
		if (Verdict == EAdmit::RejectActor && Actor != nullptr)
		{
			Params.AddIgnoredActor(Actor);
		}
		else if (Component != nullptr)
		{
			Params.AddIgnoredComponent(Component);
		}
	}

	// Whether `Component`'s overlap with the full box centred at `Centre` is contact only: nowhere
	// deeper than the contact tolerance. `ComputePenetration` reports the deepest penetration over
	// every shape of the body, so a body that holds the box anywhere reads as solid.
	bool IsContactOnly(UPrimitiveComponent* Component, const FCollisionShape& FullShape,
		const FVector& Centre)
	{
		if (Component == nullptr)
		{
			return false;
		}
		FMTDResult Penetration;
		if (!Component->ComputePenetration(Penetration, FullShape, Centre, FQuat::Identity))
		{
			return true;   // the exact test finds no overlap at all
		}
		return Penetration.Distance <= ContactToleranceCm;
	}

	// The first admitted blocking body overlapping `Shape` centred at `Centre`, or null. A body the
	// full box only touches (`IsContactOnly`) is not solid and is passed over.
	const FOverlapResult* FirstAdmittedOverlap(UWorld& World, const FVector& Centre,
		const FCollisionShape& Shape, const FCollisionShape& FullShape, ECollisionChannel Channel,
		const FCollisionQueryParams& Params, const FElysiumRetailMaskRecipe& Recipe,
		const FElysiumRetailTrace& Request, TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle,
		TArray<FOverlapResult>& Scratch)
	{
		Scratch.Reset();
		World.OverlapMultiByChannel(Scratch, Centre, FQuat::Identity, Channel, Shape, Params);
		for (const FOverlapResult& Overlap : Scratch)
		{
			if (Overlap.bBlockingHit
				&& AdmitWorldHit(Overlap.GetComponent(), Overlap.GetActor(), Recipe, Request, ToHandle)
					== EAdmit::Admit
				&& !IsContactOnly(Overlap.GetComponent(), FullShape, Centre))
			{
				return &Overlap;
			}
		}
		return nullptr;
	}

	// The contents signature of a world-collision body (`UElysiumWorldCollisionComponent`); -1 for
	// any other body. A mover's own signature is private to `UElysiumBrushComponent` and reads -1.
	int32 SignatureOf(const UPrimitiveComponent* Component)
	{
		const UElysiumWorldCollisionComponent* Body = Cast<UElysiumWorldCollisionComponent>(Component);
		return Body != nullptr ? static_cast<int32>(Body->Signature) : -1;
	}

	// The world answer: brushes, and the movers and props the recipe admits, on its channel.
	void TraceWorld(UWorld& World, const FRetailShape& Q, const FElysiumRetailMaskRecipe& Recipe,
		const FElysiumRetailTrace& Request, FCollisionQueryParams& Params,
		TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle, FElysiumRetailTraceResult& Out)
	{
		TArray<FOverlapResult> Scratch;
		if (Q.bOverlap)
		{
			// A start == end trace is an overlap: it answers only whether the box starts in solid.
			// The fraction stays 1 (retail `IsAreaClear` reads the flags beside it, R2 § 4); a body
			// that holds the whole zero-length trace holds its end too, so both flags go together.
			if (const FOverlapResult* Solid = FirstAdmittedOverlap(World, Q.Start, Q.Shape,
					Q.FullShape, Recipe.Channel, Params, Recipe, Request, ToHandle, Scratch))
			{
				Out.bStartSolid = true;
				Out.bAllSolid = true;
				Out.HitEntity = HandleOf(Solid->GetComponent(), Solid->GetActor(), ToHandle);
				Out.HitSignature = SignatureOf(Solid->GetComponent());
			}
			return;
		}

		for (int32 Attempt = 0; Attempt < MaxWorldRetraces; ++Attempt)
		{
			FHitResult Hit;
			const bool bBlocked = Q.bLine
				? World.LineTraceSingleByChannel(Hit, Q.Start, Q.End, Recipe.Channel, Params)
				: World.SweepSingleByChannel(Hit, Q.Start, Q.End, FQuat::Identity, Recipe.Channel,
					Q.Shape, Params);
			if (!bBlocked || !Hit.bBlockingHit)
			{
				return;
			}
			UPrimitiveComponent* Component = Hit.GetComponent();
			const AActor* Actor = Hit.GetActor();
			const EAdmit Verdict = AdmitWorldHit(Component, Actor, Recipe, Request, ToHandle);
			if (Verdict != EAdmit::Admit)
			{
				Exclude(Verdict, Component, Actor, Params);
				continue;
			}
			// A box that starts merely touching a body is not in solid (`ContactToleranceCm`).
			// Moving into the contact, it is stopped where it stands: fraction 0, not start-solid,
			// as Source's box resting DIST_EPSILON off a plane and swept into it. Moving off or
			// along it, the contact is not in the way: asked again without initial overlaps (an
			// initially-overlapping shape is then passed over, the rest of the body still met).
			const bool bContact = Hit.bStartPenetrating && !Q.bLine
				&& IsContactOnly(Component, Q.FullShape, Q.Start);
			if (bContact && FVector::DotProduct(Q.End - Q.Start, Hit.Normal) >= 0.0)
			{
				if (!Params.bFindInitialOverlaps)
				{
					return;   // already asked without them: nothing further blocks
				}
				Params.bFindInitialOverlaps = false;
				continue;
			}

			// The shrunk box met the surface with the full box DIST_EPSILON into it: back off along
			// the ray until the full box stands DIST_EPSILON off it (twice the tolerance along the
			// normal), never behind the start. A line hit stands as reported.
			double Time = Hit.Time;
			const FVector Delta = Q.End - Q.Start;
			const double Length = Delta.Size();
			if (!Q.bLine && !Hit.bStartPenetrating && Length > UE_KINDA_SMALL_NUMBER)
			{
				const double Cosine = FMath::Abs(FVector::DotProduct(Delta / Length, Hit.ImpactNormal));
				const double BackOffCm = 2.0 * ContactToleranceCm / FMath::Max(Cosine, 0.1);
				Time = FMath::Max(0.0, Time - BackOffCm / Length);
			}
			Out.Fraction = static_cast<float>(Time);
			Out.EndPosCm = Request.StartCm + (Request.EndCm - Request.StartCm) * Time;
			Out.Normal = Hit.ImpactNormal;
			Out.HitEntity = HandleOf(Component, Actor, ToHandle);
			Out.HitSignature = SignatureOf(Component);
			// The shape's index in its body (Unreal truncates it to a byte). Debug only.
			Out.ElementIndex = static_cast<int32>(Hit.ElementIndex);
			// The named mapping for Source's two flags. Unreal's `bStartPenetrating` (under
			// `bFindInitialOverlaps`) is their union: the trace starts in solid. It is reported as
			// `startsolid`, and as `allsolid` too when the trace's end also stands in admitted solid
			// -- the sweep found no exit. A solid the trace leaves and a second one it ends in read as
			// all-solid here where Source would say start-solid only.
			if (Hit.bStartPenetrating && !bContact)
			{
				Out.bStartSolid = true;
				const FCollisionShape EndShape = Q.bLine
					? FCollisionShape::MakeBox(FVector(0.01)) : Q.Shape;
				const FCollisionShape EndFullShape = Q.bLine
					? FCollisionShape::MakeBox(FVector(0.01)) : Q.FullShape;
				FCollisionQueryParams EndParams = Params;
				EndParams.bFindInitialOverlaps = true;
				Out.bAllSolid = FirstAdmittedOverlap(World, Q.End, EndShape, EndFullShape,
					Recipe.Channel, EndParams, Recipe, Request, ToHandle, Scratch) != nullptr;
			}
			return;
		}
		UE_LOG(LogElysiumGeometry, Verbose,
			TEXT("TraceRetail mask 0x%x: %d rejected bodies in a row, answered clear"),
			static_cast<uint32>(Request.RetailMask), MaxWorldRetraces);
	}

	// One character met, merged into the list at its nearest contact.
	struct FCharacterContact
	{
		FElysiumEntityHandle Entity;
		float Fraction = 1.f;
		bool bStartSolid = false;
	};

	void NoteCharacter(const FCharacterContact& Contact, const FElysiumRetailTrace& Request,
		FElysiumRetailTraceResult& Out)
	{
		if (!Contact.Entity.IsSet() || Request.Ignore.Contains(Contact.Entity))
		{
			return;   // not a game entity, or the filter's pass entity
		}
		for (FElysiumRetailTraceCharacter& Known : Out.Characters)
		{
			if (Known.Entity == Contact.Entity)
			{
				if (Contact.Fraction < Known.Fraction)
				{
					Known.Fraction = Contact.Fraction;
					Known.bStartSolid = Contact.bStartSolid;
				}
				return;
			}
		}
		FElysiumRetailTraceCharacter& Added = Out.Characters.AddDefaulted_GetRef();
		Added.Entity = Contact.Entity;
		Added.Fraction = Contact.Fraction;
		Added.bStartSolid = Contact.bStartSolid;
	}

	// Every character body the same ray, box or overlap meets, nearest first. No channel and no
	// mask filter: the object types ARE the characters (NPC capsules on `Pawn`, the player's hull on
	// its own channel). One entry per entity, at its nearest contact.
	void ListCharacters(UWorld& World, const FRetailShape& Q, const FElysiumRetailTrace& Request,
		const AActor* IgnoreSelf, TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle,
		FElysiumRetailTraceResult& Out)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ElysiumCollision::PlayerChannel);
		static const FName TraceTag(TEXT("ElysiumRetailTraceCharacters"));
		FCollisionQueryParams Params(TraceTag, /*bTraceComplex*/ false);
		Params.bFindInitialOverlaps = true;
		Params.bReturnPhysicalMaterial = false;
		if (IgnoreSelf != nullptr)
		{
			Params.AddIgnoredActor(IgnoreSelf);
		}

		if (Q.bOverlap)
		{
			TArray<FOverlapResult> Overlaps;
			World.OverlapMultiByObjectType(Overlaps, Q.Start, FQuat::Identity, Objects, Q.Shape, Params);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				// As the world answer's overlap: in solid, fraction 1.
				const AActor* Actor = Overlap.GetActor();
				FCharacterContact Contact;
				Contact.Entity = Actor != nullptr ? ToHandle(Actor) : FElysiumEntityHandle::Invalid();
				Contact.Fraction = 1.f;
				Contact.bStartSolid = true;
				NoteCharacter(Contact, Request, Out);
			}
		}
		else
		{
			TArray<FHitResult> Hits;
			if (Q.bLine)
			{
				World.LineTraceMultiByObjectType(Hits, Q.Start, Q.End, Objects, Params);
			}
			else
			{
				World.SweepMultiByObjectType(Hits, Q.Start, Q.End, FQuat::Identity, Objects, Q.Shape,
					Params);
			}
			for (const FHitResult& Hit : Hits)
			{
				const AActor* Actor = Hit.GetActor();
				FCharacterContact Contact;
				Contact.Entity = Actor != nullptr ? ToHandle(Actor) : FElysiumEntityHandle::Invalid();
				Contact.Fraction = Hit.Time;
				Contact.bStartSolid = Hit.bStartPenetrating;
				NoteCharacter(Contact, Request, Out);
			}
		}
		Out.Characters.Sort(
			[](const FElysiumRetailTraceCharacter& A, const FElysiumRetailTraceCharacter& B)
			{
				return A.Fraction < B.Fraction;
			});
	}

	// Times one seam query into the budget.
	struct FBudgetScope
	{
		FElysiumGeometryBudget::EKind Kind;
		uint64 StartCycles;

		explicit FBudgetScope(FElysiumGeometryBudget::EKind InKind)
			: Kind(InKind)
			, StartCycles(FPlatformTime::Cycles64())
		{
		}
		~FBudgetScope()
		{
			FElysiumGeometryBudget::Count(Kind,
				FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartCycles));
		}
	};
}

namespace ElysiumRetailSweep
{
	void ClearTrace(const FVector& Start, const FVector& Delta, FElysiumRetailTraceResult& Out,
		IElysiumRetailSiteSink* Sites)
	{
		// `FUN_1023f3d0` `0x1023f3d0`, straight-line (`walks/L0-r011.md`). The result's port-only
		// defaults (normal, entity, signature) are what retail leaves unwritten; reset, then the writes.
		Out = FElysiumRetailTraceResult();
		Out.StartPosCm = Start;                      // out[0..2] = start
		Out.EndPosCm = Start;                        // out[3..5] = start
		Out.EndPosCm.X = Out.EndPosCm.X + Delta.X;   // then each component += delta, in place
		Out.EndPosCm.Y = Delta.Y + Out.EndPosCm.Y;
		Out.EndPosCm.Z = Delta.Z + Out.EndPosCm.Z;
		Out.bStartSolid = false;                     // +0x37 = 0
		Out.bAllSolid = false;                       // +0x36 = 0
		Out.Fraction = 1.0f;                         // +0x2c = 0x3f800000
		Out.Contents = 0;                            // +0x30 = 0
		if (Sites)
		{
			Sites->Site(TEXT("world_clear"), TEXT("FUN_1023f3d0"), 0x1023f3d0u, TEXT("write"),
				FString::Printf(TEXT("start=%g,%g,%g end=%g,%g,%g fraction=1 contents=0 allsolid=0 startsolid=0"),
					Out.StartPosCm.X, Out.StartPosCm.Y, Out.StartPosCm.Z, Out.EndPosCm.X, Out.EndPosCm.Y, Out.EndPosCm.Z));
		}
	}

	bool SegmentSphereOverlap(const FVector& A, const FVector& D, const FVector& C, double R1, double R2,
		IElysiumRetailSiteSink* Sites)
	{
		// `FUN_1023ffa0` `0x1023ffa0` (`walks/L0-r011.md`), on the x87 stack: double here.
		// 5 (first): `rs = r1 + r2`, stored over the r1 slot.
		const double Rs = R1 + R2;
		// 1. `p = dot(C - A, D)`, summed x, z, y as the asm orders it.
		const double P = ((C.X - A.X) * D.X + (C.Z - A.Z) * D.Z) + (C.Y - A.Y) * D.Y;
		double T = 0.0;
		// 2. `p > 0.0` (`_DAT_104454c4`; a NaN takes this arm: `FCOMP; TEST AH,0x41; JP`).
		if (!(P <= 0.0))
		{
			const double D2 = D.X * D.X + D.Y * D.Y + D.Z * D.Z;   // summed x, y, z
			// `p <= d2 ? p / d2 : 1.0` (`FCOMP; AND EAX,0x4100; JNZ`); `d2 == 0` is reachable only with a
			// non-finite `p` (a zero D gives `p == 0`, arm 3).
			T = P <= D2 ? P / D2 : 1.0;
		}
		// 3. else `t = 0.0`: `p == 0` and `p < 0` alike.
		// 4. `Q = A + D * t` (`thunk_FUN_10139500` `0x10139500`, VectorMA).
		const double Qx = A.X + D.X * T;
		const double Qy = A.Y + D.Y * T;
		const double Qz = A.Z + D.Z * T;
		// 5. `rs * rs < |Q - C|^2` (summed z, y, x) -> 0, else 1. Strict: an exact touch is 1, a NaN 0.
		const double Dist2 = (Qz - C.Z) * (Qz - C.Z) + (Qy - C.Y) * (Qy - C.Y) + (Qx - C.X) * (Qx - C.X);
		const bool bOverlap = !(Rs * Rs < Dist2);
		if (Sites)
		{
			Sites->Site(TEXT("segment_sphere"), TEXT("FUN_1023ffa0"), 0x1023ffa0u, TEXT("return"),
				FString::Printf(TEXT("p=%g t=%g dist2=%g rs2=%g result=%d"), P, T, Dist2, Rs * Rs, bOverlap ? 1 : 0));
		}
		return bOverlap;
	}

	bool HullClipPrelude(const FVector& RayStart, const FVector& RayStartOffset, const FVector& RayDelta,
		const FVector& RayExtents, const FVector& BoxCentre, const FVector& BoxHalf, double Tolerance,
		FElysiumRetailTraceResult& Out, IElysiumRetailSiteSink* Sites)
	{
		// `FUN_10241620` `0x10241620`, the hull path (`ray+0x30 == 0`), as far as the walk read it:
		// `ClearTrace(ray.start + ray.startOffset, ray.delta, trace)`, then the sphere early-out with
		// `r1 = sqrt(|extents|^2) + sqrt(|half|^2)` (two `fsqrt` `0x101371d0` calls, float) and
		// `r2 = tolerance`; a failed test returns 0 with the trace as ClearTrace wrote it.
		ClearTrace(RayStart + RayStartOffset, RayDelta, Out, Sites);
		const float ExtentRadius = FMath::Sqrt(static_cast<float>(RayExtents.X * RayExtents.X + RayExtents.Y * RayExtents.Y + RayExtents.Z * RayExtents.Z));
		const float HalfRadius = FMath::Sqrt(static_cast<float>(BoxHalf.X * BoxHalf.X + BoxHalf.Y * BoxHalf.Y + BoxHalf.Z * BoxHalf.Z));
		return SegmentSphereOverlap(RayStart, RayDelta, BoxCentre, static_cast<double>(ExtentRadius + HalfRadius), Tolerance, Sites);
	}
}

namespace ElysiumWorldGeometry
{
	bool Trace(UWorld& World, const FElysiumRetailTrace& Request, FElysiumRetailTraceResult& Out,
		const AActor* IgnoreSelf, TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle)
	{
		// `ClearTrace` `0x1023f3d0` with `start = ray.start`, `delta = ray.delta`: the clear result
		// every retail clip starts from (`endpos = start + delta`, which is the request's end).
		ElysiumRetailSweep::ClearTrace(Request.StartCm, Request.EndCm - Request.StartCm, Out);
		if (World.GetPhysicsScene() == nullptr)
		{
			return false;
		}
		const FElysiumRetailMaskRecipe Recipe = ElysiumRetailMask::Recipe(Request.RetailMask);
		if (Recipe.bNothing)
		{
			return true;   // mask 0 asks nothing: clear, untraced
		}
		const ElysiumWorldGeometryDetail::FBudgetScope Budget(FElysiumGeometryBudget::EKind::Trace);
		const ElysiumWorldGeometryDetail::FRetailShape Q = ElysiumWorldGeometryDetail::ShapeOf(Request);

		static const FName TraceTag(TEXT("ElysiumRetailTrace"));
		FCollisionQueryParams Params(TraceTag, /*bTraceComplex*/ false);
		Params.bFindInitialOverlaps = true;
		Params.bReturnPhysicalMaterial = false;
		if (IgnoreSelf != nullptr)
		{
			Params.AddIgnoredActor(IgnoreSelf);
		}
		// Characters never; movers only under MOVEABLE (R2 § 2: `StandardFilterRules` rejects
		// movetype 8 without it); entity props only under MONSTER (the same function rejects every
		// entity that is not a solid brush model unless the mask carries MONSTER). STATIC props are
		// always met: the engine hands them to no entity filter (R2 § 2, engine `2006aabe..2006aae5`),
		// and they wear no bit. The bits are worn by the bodies themselves: the NPC capsule and the
		// player's hull carry `CharacterMaskBit`, every brush-entity body `MoverMaskBit`, every
		// entity prop's body `PropMaskBit` unless it is `blocks_traces`, and an `npc_transparent`
		// prop `NpcTransparentMaskBit`, which the `FVisible` filter drops (0019/6,
		// `CTraceFilterFVisible::ShouldHitEntity 0x10107630`).
		Params.IgnoreMask = static_cast<FMaskFilter>(ElysiumRetailMask::QueryIgnoreMask(Recipe,
			Request.Filter == EElysiumRetailTraceFilter::FVisible));

		ElysiumWorldGeometryDetail::TraceWorld(World, Q, Recipe, Request, Params, ToHandle, Out);
		if (Recipe.bCharacters)
		{
			ElysiumWorldGeometryDetail::ListCharacters(World, Q, Request, IgnoreSelf, ToHandle, Out);
		}
		return true;
	}

	bool Route(const ANavigationData& NavData, const FNavAgentProperties& Agent,
		FSharedConstNavQueryFilter Filter, const FVector& FromCm, const FElysiumNpcRouteQuery& Query,
		FElysiumNpcRouteAnswer& Out, const UObject* Querier)
	{
		const ElysiumWorldGeometryDetail::FBudgetScope Budget(FElysiumGeometryBudget::EKind::PathTest);
		// Both ends projected at the nav data's default query extent, unfiltered -- what
		// `ProjectPointToNavigation(..., INVALID_NAVEXTENT, &Agent)` resolves to, and what `MoveTo`
		// projects its goal with.
		const FVector Extent = NavData.GetDefaultQueryExtent();
		FNavLocation FromOnMesh;
		FNavLocation DestOnMesh;
		if (!NavData.ProjectPoint(FromCm, FromOnMesh, Extent)
			|| !NavData.ProjectPoint(Query.DestCm, DestOnMesh, Extent))
		{
			return false;
		}
		// The querier, when the caller names one (a body's controller): custom links read it. The
		// filter arrives already resolved.
		FPathFindingQuery PathQuery(Querier, NavData,
			FromOnMesh.Location, DestOnMesh.Location, Filter);
		PathQuery.SetAllowPartialPaths(Query.bAcceptPartial);
		PathQuery.SetNavAgentProperties(Agent);
		// From here the mesh has answered, so the answer is TRUE whatever it says: false is kept
		// for "no mesh to ask" (no nav data, no agent, an end that does not project). A search that
		// finds no path, or only a partial one the query refuses, is `bReachable = false` with
		// `bPartial` and the length as found.
		const FPathFindingResult Found = NavData.FindPath(Agent, PathQuery);
		const bool bHavePath = Found.IsSuccessful() && Found.Path.IsValid();
		const bool bPartial = bHavePath && Found.Path->IsPartial();
		Out.bReachable = bHavePath && (!bPartial || Query.bAcceptPartial);
		Out.bPartial = bPartial;
		Out.LengthCm = bHavePath ? static_cast<float>(Found.Path->GetLength()) : 0.f;
		// The corners as found (0018/7: the crosswalk splice reads which curb places they pass).
		Out.PointsCm.Reset();
		if (bHavePath)
		{
			for (const FNavPathPoint& Point : Found.Path->GetPathPoints())
			{
				Out.PointsCm.Add(Point.Location);
			}
		}
		return true;
	}

	bool Raycast(const ANavigationData& NavData, FSharedConstNavQueryFilter Filter,
		const FElysiumNpcNavRaycast& Query, FElysiumNpcNavRaycastAnswer& Out)
	{
		const ElysiumWorldGeometryDetail::FBudgetScope Budget(FElysiumGeometryBudget::EKind::Raycast);
		// True from the engine = the corridor left the mesh; `HitLocation` is then the edge it left
		// by (the segment's end otherwise).
		FVector HitLocation = Query.ToCm;
		Out.bHit = NavData.Raycast(Query.FromCm, Query.ToCm, HitLocation, Filter);
		Out.HitCm = HitLocation;
		return true;
	}
}

namespace ElysiumWorldGeometryDetail
{
	FElysiumGeometryBudget GElysiumGeometryCurrent;
	FElysiumGeometryBudget GElysiumGeometryPeak;

	// One log period: the busiest frame's path tests and cost, and the period's totals.
	struct FBudgetPeriod
	{
		double StartSeconds = 0.0;
		int32 PeakPathTests = 0;
		double PeakFrameMs = 0.0;
		int64 Frames = 0;
		int64 PathTests = 0;
		int64 Raycasts = 0;
		int64 Traces = 0;
		double Ms = 0.0;
	};
	FBudgetPeriod GElysiumGeometryPeriod;
	uint64 GElysiumGeometryPeriodFrame = 0;

	// Running totals since the process started.
	int64 GElysiumGeometryTotalQueries = 0;
	double GElysiumGeometryTotalMs = 0.0;

	void MaybeLogPeriod()
	{
		const float PeriodSeconds = CVarElysiumGeomBudgetLogSeconds.GetValueOnGameThread();
		const double Now = FPlatformTime::Seconds();
		FBudgetPeriod& Period = GElysiumGeometryPeriod;
		if (PeriodSeconds <= 0.f || Period.StartSeconds == 0.0)
		{
			if (PeriodSeconds <= 0.f)
			{
				Period = FBudgetPeriod();
			}
			Period.StartSeconds = Now;
			return;
		}
		if (Now - Period.StartSeconds < PeriodSeconds)
		{
			return;
		}
		UE_LOG(LogElysiumGeometry, Log,
			TEXT("geometry budget %.1fs: peak frame %d path tests (peak frame %.3f ms); %lld frames: ")
			TEXT("%lld path tests, %lld nav raycasts, %lld traces, %.3f ms; lifetime %lld queries, %.1f ms"),
			Now - Period.StartSeconds, Period.PeakPathTests, Period.PeakFrameMs, Period.Frames,
			Period.PathTests, Period.Raycasts, Period.Traces, Period.Ms,
			GElysiumGeometryTotalQueries, GElysiumGeometryTotalMs);
		Period = FBudgetPeriod();
		Period.StartSeconds = Now;
	}
}

// The seam's cost, counted and never enforced: retail has no per-frame budget on these queries --
// nothing refuses a trace or a route for cost -- so this only measures what the story holds the
// port to.
void FElysiumGeometryBudget::Count(EKind Kind, double InMs)
{
	check(IsInGameThread());
	FElysiumGeometryBudget& Current = ElysiumWorldGeometryDetail::GElysiumGeometryCurrent;
	FElysiumGeometryBudget& PeakFrame = ElysiumWorldGeometryDetail::GElysiumGeometryPeak;
	ElysiumWorldGeometryDetail::FBudgetPeriod& Period = ElysiumWorldGeometryDetail::GElysiumGeometryPeriod;
	if (Current.Frame != GFrameCounter)
	{
		Current = FElysiumGeometryBudget();
		Current.Frame = GFrameCounter;
	}
	if (ElysiumWorldGeometryDetail::GElysiumGeometryPeriodFrame != GFrameCounter)
	{
		ElysiumWorldGeometryDetail::GElysiumGeometryPeriodFrame = GFrameCounter;
		++Period.Frames;
	}
	switch (Kind)
	{
	case EKind::PathTest:
		++Current.PathTests;
		++Period.PathTests;
		break;
	case EKind::Raycast:
		++Current.Raycasts;
		++Period.Raycasts;
		break;
	case EKind::Trace:
		++Current.Traces;
		++Period.Traces;
		break;
	}
	Current.Ms += InMs;
	Period.Ms += InMs;
	Period.PeakPathTests = FMath::Max(Period.PeakPathTests, Current.PathTests);
	Period.PeakFrameMs = FMath::Max(Period.PeakFrameMs, Current.Ms);
	++ElysiumWorldGeometryDetail::GElysiumGeometryTotalQueries;
	ElysiumWorldGeometryDetail::GElysiumGeometryTotalMs += InMs;
	if (Current.Frame == PeakFrame.Frame || Current.Ms > PeakFrame.Ms)
	{
		PeakFrame = Current;
	}
	ElysiumWorldGeometryDetail::MaybeLogPeriod();
}

const FElysiumGeometryBudget& FElysiumGeometryBudget::Peak()
{
	return ElysiumWorldGeometryDetail::GElysiumGeometryPeak;
}
