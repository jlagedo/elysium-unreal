// The env_sprite billboard proxy:
// one quad per view, Source's glow rule off the Sprites page, and the per-sprite occlusion query
// that gates the blend. The one piece of rendering code the task allows; it draws nothing the
// bake did not write and reads no file.
#include "ElysiumSpriteComponent.h"

#include "ElysiumSpriteGlow.h"
#include "ElysiumSpriteSettings.h"

#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "CoreGlobals.h"
#include "DynamicMeshBuilder.h"
#include "Engine/CollisionProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneManagement.h"
#include "SceneView.h"

ElysiumSpriteGlow::FParams ElysiumSpriteGlow::FParams::FromSettings()
{
	FParams P;
	if (const UElysiumSpriteSettings* Page = GetDefault<UElysiumSpriteSettings>())
	{
		P.Falloff = Page->GlowFalloff;
		P.MinBrightness = Page->GlowMinBrightness;
		P.SizePerDistance = Page->GlowSizePerDistance;
		P.FadeInSeconds = Page->GlowFadeInSeconds;
		P.FadeOutSeconds = Page->GlowFadeOutSeconds;
		P.QueryFootprintPerDistance = Page->SpriteQueryFootprintPerDistance;
		P.QueryFixedHalfInches = Page->SpriteQueryFixedHalfInches;
		P.QueryGrid = FMath::Clamp(Page->SpriteQueryGrid, 1, 8);
	}
	return P;
}

namespace
{
	// A corona's bounds have to hold its quad at any distance it can be seen from; the quad grows
	// with the distance, so the bounds are its size at this range. Beyond it the frustum test
	// may drop a corona that would still be drawn in 2004 -- 200 m, past any VtMB sightline.
	constexpr float GlowBoundsDistanceCm = 20000.0f;
	// A view key is never reused, so a per-view state this sprite has not been drawn for in this
	// many render-thread frames is dead weight and is dropped when the next new view arrives.
	constexpr uint32 StaleViewFrames = 300;
}

class FElysiumSpriteSceneProxy final : public FPrimitiveSceneProxy
{
public:
	explicit FElysiumSpriteSceneProxy(const UElysiumSpriteComponent* Component)
		: FPrimitiveSceneProxy(Component)
		, Material(Component->Material)
		, SizeInches(Component->SizeInches)
		, Color(Component->Color)
		, RenderMode(Component->RenderMode)
		, RenderFx(Component->RenderFx)
		, bUpright(Component->bUpright)
		, Glow(ElysiumSpriteGlow::FParams::FromSettings())
	{
		if (Material)
		{
			MaterialRelevance = Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform());
		}
		bVFRequiresPrimitiveUniformBuffer = true;
	}

	// The frame the proxy went live on, the guard `FHierarchicalStaticMeshSceneProxy` uses
	// (`HierarchicalInstancedStaticMesh.cpp` 872-877): occlusion results issued against the
	// previous proxy's query set must not be accepted by this one.
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
	{
		FPrimitiveSceneProxy::CreateRenderThreadResources(RHICmdList);
		SceneProxyCreatedFrameNumberRenderThread = GFrameNumberRenderThread;
	}

	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View) && Material != nullptr;
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = false;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = false;
		return Result;
	}

	// The master is depth-test-off, and the engine's default (`!bDisableDepthTest`) would skip
	// every occlusion query for it; the query is the whole point here.
	virtual bool CanBeOccluded() const override { return true; }
	virtual bool HasSubprimitiveOcclusionQueries() const override { return true; }

	// VtMB's pixel-visibility quad as a grid of flat samples at the sprite origin, facing the view.
	// The half-size is the render mode's own (`ElysiumSpriteGlow::QueryHalfSizeCm`: screen-constant
	// for mode 3, VtMB's fixed 3 units otherwise) clamped to the card it gates, and each sample is
	// flattened along the sight line: the renderer rasterises an axis-aligned box's front faces
	// with `CF_DepthNearOrEqual` and calls the sub-query visible if any pixel passes
	// (`SceneOcclusion.cpp` 508-539, 1254; `SceneVisibility.cpp` 2833), so a cube of side `Cell`
	// would straddle the wall behind the sprite and answer off its own back half.
	virtual const TArray<FBoxSphereBounds>* GetOcclusionQueries(const FSceneView* View) const override
	{
		const FMatrix ToWorld = GetLocalToWorld();
		const FVector Origin = ToWorld.GetOrigin();
		const float DistCm = static_cast<float>((View->ViewMatrices.GetViewOrigin() - Origin).Size());
		const float Half = ElysiumSpriteGlow::QueryHalfSizeCm(
			DistCm, RenderMode, CardHalfSizeCm(DistCm, ToWorld), Glow);
		const int32 N = FMath::Max(Glow.QueryGrid, 1);
		const double Cell = 2.0 * Half / N;
		const FVector Right = View->GetViewRight();
		const FVector Up = View->GetViewUp();
		const FVector Dir = View->GetViewDirection();
		// The exact axis-aligned bounds of the flat `Cell x Cell` card facing the view: each world
		// axis takes the card's own two axes plus the sample half-thickness along the sight line.
		// A per-axis `1 - |Dir|` heuristic would only thin an axis-aligned view -- off axis it
		// shrinks all three, leaving a cube that still straddles the wall behind while covering a
		// fraction of the cell it is meant to sample.
		const double Thickness = ElysiumSpriteGlow::QuerySampleThicknessCm;
		const double HalfCell = Cell * 0.5;
		const auto AxisExtent = [&](double R, double U, double D)
		{
			return HalfCell * (FMath::Abs(R) + FMath::Abs(U)) + Thickness * FMath::Abs(D);
		};
		const FVector Extent(
			AxisExtent(Right.X, Up.X, Dir.X),
			AxisExtent(Right.Y, Up.Y, Dir.Y),
			AxisExtent(Right.Z, Up.Z, Dir.Z));
		const double Radius = Extent.Size();

		UE::TUniqueLock Lock(ViewStatesMutex);
		FViewOcclusionState& State = FindOrAddViewState(View->GetViewKey());
		// The frame this view asked on: staleness is "asked and never answered", so a renderer that
		// stops asking freezes the fraction instead of fading the card out.
		State.QueriedFrameNumber = GFrameNumberRenderThread;
		TArray<FBoxSphereBounds>& Out = State.Bounds;
		Out.Reset(N * N);
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const FVector Center = Origin
					+ Right * ((I + 0.5) * Cell - Half)
					+ Up * ((J + 0.5) * Cell - Half);
				Out.Emplace(Center, Extent, Radius);
			}
		}
		return &Out;
	}

	// The renderer's answer, one frame later: `true` is occluded. The visible fraction is the
	// target the smoothing chases in the next draw, kept per view (the same proxy answers a
	// player view, a reflection capture's six faces and an editor viewport in one frame) and
	// stamped with the frame it arrived on, because an answer that stops arriving is not an
	// answer. `FHierarchicalStaticMeshSceneProxy::AcceptOcclusionResults`
	// (`HierarchicalInstancedStaticMesh.cpp` 1983-2015) is the shape this mirrors: the creation
	// frame guards against results issued for a previous proxy's query set, and the mutex against
	// two views landing at once.
	virtual void AcceptOcclusionResults(const FSceneView* View, TArray<bool>* Results, int32 ResultsStart, int32 NumResults) override
	{
		if (Results == nullptr || NumResults <= 0
			|| SceneProxyCreatedFrameNumberRenderThread >= GFrameNumberRenderThread)
		{
			return;
		}
		int32 Visible = 0;
		for (int32 K = 0; K < NumResults; ++K)
		{
			if (!(*Results)[ResultsStart + K])
			{
				++Visible;
			}
		}

		UE::TUniqueLock Lock(ViewStatesMutex);
		FViewOcclusionState& State = FindOrAddViewState(View->GetViewKey());
		if (State.Bounds.Num() != NumResults)
		{
			// Not this view's grid: the sample count changed under the results.
			return;
		}
		State.Target = static_cast<float>(Visible) / static_cast<float>(NumResults);
		State.AcceptedFrameNumber = GFrameNumberRenderThread;
		State.bEverAccepted = true;
	}

	// The fraction this view draws with, advanced on the render thread's clock. VtMB's rule
	// (`GlowBlend` `100c257e`-`100c2586`): a stale or absent query reads 0 and the corona fades
	// back in at `1 / r_glowfadein`. Unreal answers "unoccluded" generously -- on first sight,
	// after a history trim, on a camera cut, a teleport or a >45 deg turn
	// (`bIgnoreExistingQueries`, `SceneVisibility.cpp` 5571-5586) and whenever a result is
	// unavailable -- so a query asked more than `QueryStaleFrames` frames ago and still unanswered
	// reads 0, and one all-visible frame can raise the blend by at most a
	// `MaxSmoothStepFraction` step instead of snapping it to full. Until a query has ever answered
	// the card draws whole, which keeps the contract that a renderer with sub-queries off draws
	// every sprite; the stored fraction is held at full over those frames, so the first real answer
	// smooths down from what is on screen rather than popping dark and fading back in.
	float AdvanceVisibility(uint32 ViewKey, double NowSeconds) const
	{
		UE::TUniqueLock Lock(ViewStatesMutex);
		FViewOcclusionState& State = FindOrAddViewState(ViewKey);
		const float Elapsed = State.LastSmoothTime < 0.0
			? 0.0f
			: static_cast<float>(NowSeconds - State.LastSmoothTime);
		State.LastSmoothTime = NowSeconds;
		if (!State.bEverAccepted)
		{
			State.Current = 1.0f;
			return State.Current;
		}
		// Stale is a query that was issued and never answered. The drawing frame is no measure: a
		// primitive the renderer still draws but has stopped occlusion-testing (a selected actor in
		// the editor, `r.AllowOcclusionQueries 0`, a view with no scene state) would otherwise fade
		// to nothing with nothing in front of it.
		const bool bStale = State.QueriedFrameNumber > State.AcceptedFrameNumber
			&& State.QueriedFrameNumber - State.AcceptedFrameNumber > ElysiumSpriteGlow::QueryStaleFrames;
		const float Desired = bStale ? 0.0f : State.Target;
		// The step cap is a fraction of the fade actually taken, so it stays below the fade whatever
		// the Sprites page holds.
		const float FadeSeconds = Desired < State.Current ? Glow.FadeOutSeconds : Glow.FadeInSeconds;
		const float Delta = FMath::Clamp(
			Elapsed, 0.0f, ElysiumSpriteGlow::MaxSmoothStepFraction * FadeSeconds);
		State.Current = ElysiumSpriteGlow::Smooth(State.Current, Desired, Delta, Glow);
		return State.Current;
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!Material)
		{
			return;
		}
		// The visible fraction is per view and advances on the render thread's clock, one step
		// per draw (`AdvanceVisibility`).
		const double Now = ViewFamily.Time.GetRealTimeSeconds();

		const FMatrix ToWorld = GetLocalToWorld();
		const FVector Origin = ToWorld.GetOrigin();
		const float ActorScale = static_cast<float>(ToWorld.GetScaleVector().GetAbsMax());

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if (!(VisibilityMap & (1 << ViewIndex)))
			{
				continue;
			}
			const FSceneView* View = Views[ViewIndex];
			const FVector ToView = View->ViewMatrices.GetViewOrigin() - Origin;
			const float DistCm = static_cast<float>(ToView.Size());

			const float WidthCm = ElysiumSpriteGlow::WorldSizeCm(
				static_cast<float>(SizeInches.X), RenderMode, RenderFx, DistCm, ActorScale, Glow);
			const float HeightCm = ElysiumSpriteGlow::WorldSizeCm(
				static_cast<float>(SizeInches.Y), RenderMode, RenderFx, DistCm, ActorScale, Glow);
			const float Brightness = ElysiumSpriteGlow::Brightness(DistCm, RenderMode, RenderFx, Glow);
			const float Alpha = (Color.A / 255.0f) * Brightness * AdvanceVisibility(View->GetViewKey(), Now);
			if (Alpha <= 0.0f || WidthCm <= 0.0f || HeightCm <= 0.0f)
			{
				continue;
			}

			// The quad's axes: the view's own right/up for a full billboard; world Z and the
			// horizontal perpendicular to the sight line for `parallel_upright`.
			FVector Right = View->GetViewRight();
			FVector Up = View->GetViewUp();
			if (bUpright)
			{
				Up = FVector::UpVector;
				const FVector Flat = FVector(ToView.X, ToView.Y, 0.0).GetSafeNormal();
				if (!Flat.IsNearlyZero())
				{
					Right = FVector::CrossProduct(Up, Flat).GetSafeNormal();
				}
			}

			const FColor VertexColor(Color.R, Color.G, Color.B,
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Alpha * 255.0f), 0, 255)));
			const FVector3f HalfW = FVector3f(Right) * (WidthCm * 0.5f);
			const FVector3f HalfH = FVector3f(Up) * (HeightCm * 0.5f);
			const FVector3f Normal = FVector3f(FVector::CrossProduct(Right, Up).GetSafeNormal());
			const FVector3f TangentX = FVector3f(Right);

			FDynamicMeshBuilder Builder(View->GetFeatureLevel());
			auto AddCorner = [&](const FVector3f& Position, float U, float V)
			{
				FDynamicMeshVertex Vertex;
				Vertex.Position = Position;
				Vertex.TextureCoordinate[0] = FVector2f(U, V);
				Vertex.Color = VertexColor;
				Vertex.SetTangents(TangentX, FVector3f(Up), Normal);
				return Builder.AddVertex(Vertex);
			};
			// Local space is the origin; the translation carries the world position so the
			// vertex floats stay small.
			const int32 V0 = AddCorner(-HalfW + HalfH, 0.0f, 0.0f);
			const int32 V1 = AddCorner(HalfW + HalfH, 1.0f, 0.0f);
			const int32 V2 = AddCorner(HalfW - HalfH, 1.0f, 1.0f);
			const int32 V3 = AddCorner(-HalfW - HalfH, 0.0f, 1.0f);
			Builder.AddTriangle(V0, V1, V2);
			Builder.AddTriangle(V0, V2, V3);
			Builder.GetMesh(FTranslationMatrix(Origin), Material->GetRenderProxy(),
				GetDepthPriorityGroup(View), /*bDisableBackfaceCulling*/ true,
				/*bReceivesDecals*/ false, ViewIndex, Collector);
		}
	}

	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }

private:
	// Half the drawn card for this view, in cm: the square the occlusion sample is clamped into,
	// inscribed in the quad so no sample tests a region the card does not cover.
	float CardHalfSizeCm(float DistCm, const FMatrix& ToWorld) const
	{
		const float ActorScale = static_cast<float>(ToWorld.GetScaleVector().GetAbsMax());
		const float WidthCm = ElysiumSpriteGlow::WorldSizeCm(
			static_cast<float>(SizeInches.X), RenderMode, RenderFx, DistCm, ActorScale, Glow);
		const float HeightCm = ElysiumSpriteGlow::WorldSizeCm(
			static_cast<float>(SizeInches.Y), RenderMode, RenderFx, DistCm, ActorScale, Glow);
		return FMath::Min(WidthCm, HeightCm) * 0.5f;
	}

	// One sprite draws into every view of a frame, and the queries are issued, answered and
	// smoothed per view; state shared across views would let a reflection capture's occluded face
	// dim the player's corona. Held behind a unique pointer because `GetOcclusionQueries` hands
	// the renderer a pointer into `Bounds` that must stay valid while another view's insert
	// rehashes the map.
	struct FViewOcclusionState
	{
		TArray<FBoxSphereBounds> Bounds;
		float Target = 0.0f;
		float Current = 0.0f;
		double LastSmoothTime = -1.0;
		uint32 QueriedFrameNumber = 0;
		uint32 AcceptedFrameNumber = 0;
		uint32 TouchedFrameNumber = 0;
		bool bEverAccepted = false;
	};

	// Render thread, `ViewStatesMutex` held by the caller.
	FViewOcclusionState& FindOrAddViewState(uint32 ViewKey) const
	{
		TUniquePtr<FViewOcclusionState>* Found = ViewStates.Find(ViewKey);
		if (Found == nullptr)
		{
			// A view key is never reused: a state untouched for `StaleViewFrames` belongs to a
			// view that is gone, and dropping it here keeps a long session's map bounded.
			for (auto It = ViewStates.CreateIterator(); It; ++It)
			{
				if (GFrameNumberRenderThread > It.Value()->TouchedFrameNumber + StaleViewFrames)
				{
					It.RemoveCurrent();
				}
			}
			Found = &ViewStates.Add(ViewKey, MakeUnique<FViewOcclusionState>());
		}
		(*Found)->TouchedFrameNumber = GFrameNumberRenderThread;
		return **Found;
	}

	UMaterialInterface* Material;
	FMaterialRelevance MaterialRelevance;
	FVector2D SizeInches;
	FColor Color;
	int32 RenderMode;
	int32 RenderFx;
	bool bUpright;
	ElysiumSpriteGlow::FParams Glow;
	uint32 SceneProxyCreatedFrameNumberRenderThread = MAX_uint32;

	// Render-thread state. `GetOcclusionQueries` is called both from the visibility task and from
	// the occlusion cull, and `AcceptOcclusionResults` from whichever view's readback lands first,
	// so every touch takes the mutex -- the shape
	// `FHierarchicalStaticMeshSceneProxy::AcceptOcclusionResults` uses.
	mutable TMap<uint32, TUniquePtr<FViewOcclusionState>> ViewStates;
	mutable UE::FMutex ViewStatesMutex;
};

UElysiumSpriteComponent::UElysiumSpriteComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// A client-side card in VtMB: nothing collides with it, nothing traces it, it casts nothing.
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bReceivesDecals = false;
	bUseAsOccluder = false;
	SetCanEverAffectNavigation(false);
	bVisibleInRayTracing = false;
	Mobility = EComponentMobility::Static;
}

bool UElysiumSpriteComponent::IsGlow() const
{
	return ElysiumSpriteGlow::IsGlowMode(RenderMode);
}

FPrimitiveSceneProxy* UElysiumSpriteComponent::CreateSceneProxy()
{
	return new FElysiumSpriteSceneProxy(this);
}

FBoxSphereBounds UElysiumSpriteComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const float Largest = static_cast<float>(FMath::Max(SizeInches.X, SizeInches.Y));
	const float Scale = static_cast<float>(LocalToWorld.GetMaximumAxisScale());
	const ElysiumSpriteGlow::FParams P = ElysiumSpriteGlow::FParams::FromSettings();
	const float Radius = FMath::Max(ElysiumSpriteGlow::WorldSizeCm(
		Largest, RenderMode, RenderFx, GlowBoundsDistanceCm, Scale, P), 1.0f);
	return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector(Radius), Radius);
}

void UElysiumSpriteComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool) const
{
	if (Material)
	{
		OutMaterials.Add(Material);
	}
}
