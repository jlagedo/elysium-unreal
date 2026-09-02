// The env_sprite billboard proxy (R6.1, `docs/architecture/seam_map_map.md` -> "Sprites (R6.1)"):
// one quad per view, Source's glow rule off the Sprites page, and the per-sprite occlusion query
// that gates the blend. The one piece of rendering code the task allows; it draws nothing the
// bake did not write and reads no file.
#include "ElysiumSpriteComponent.h"

#include "ElysiumSpriteGlow.h"
#include "ElysiumSpriteSettings.h"

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
	// One occlusion-bounds array per view in flight (a reflection capture renders six faces in
	// one frame): the renderer keeps the pointer until the queries are built.
	constexpr int32 QuerySlots = 8;
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

	// VtMB's pixel-visibility quad as a grid of boxes at the origin, facing the view.
	virtual const TArray<FBoxSphereBounds>* GetOcclusionQueries(const FSceneView* View) const override
	{
		TArray<FBoxSphereBounds>& Out = QueryBounds[QuerySlot];
		QuerySlot = (QuerySlot + 1) % QuerySlots;
		Out.Reset();
		const FVector Origin = GetLocalToWorld().GetOrigin();
		const float DistCm = static_cast<float>((View->ViewMatrices.GetViewOrigin() - Origin).Size());
		const float Half = ElysiumSpriteGlow::QueryHalfSizeCm(DistCm, Glow);
		const int32 N = FMath::Max(Glow.QueryGrid, 1);
		const float Cell = 2.0f * Half / N;
		const FVector Right = View->GetViewRight();
		const FVector Up = View->GetViewUp();
		const FVector Extent(Cell * 0.5f);
		for (int32 J = 0; J < N; ++J)
		{
			for (int32 I = 0; I < N; ++I)
			{
				const FVector Center = Origin
					+ Right * ((I + 0.5f) * Cell - Half)
					+ Up * ((J + 0.5f) * Cell - Half);
				Out.Emplace(Center, Extent, Extent.Size());
			}
		}
		return &Out;
	}

	// The renderer's answer, one frame later: `true` is occluded. The visible fraction is the
	// target the smoothing chases in the next draw.
	virtual void AcceptOcclusionResults(const FSceneView* View, TArray<bool>* Results, int32 ResultsStart, int32 NumResults) override
	{
		if (!Results || NumResults <= 0)
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
		VisibleTarget = static_cast<float>(Visible) / static_cast<float>(NumResults);
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if (!Material)
		{
			return;
		}
		// The smoothing runs on the render thread's clock, once per frame whatever the view
		// count; a gap of more than a second (the sprite was culled) snaps to the target, so a
		// corona coming back into view does not fade in from a stale value.
		const double Now = ViewFamily.Time.GetRealTimeSeconds();
		float Delta = LastSmoothTime < 0.0 ? 0.0f : static_cast<float>(Now - LastSmoothTime);
		if (Delta < 0.0f || Delta > 1.0f)
		{
			Delta = 0.0f;
			VisibleCurrent = VisibleTarget;
		}
		VisibleCurrent = ElysiumSpriteGlow::Smooth(VisibleCurrent, VisibleTarget, Delta, Glow);
		LastSmoothTime = Now;

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
			const float Alpha = (Color.A / 255.0f) * Brightness * VisibleCurrent;
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
	UMaterialInterface* Material;
	FMaterialRelevance MaterialRelevance;
	FVector2D SizeInches;
	FColor Color;
	int32 RenderMode;
	int32 RenderFx;
	bool bUpright;
	ElysiumSpriteGlow::FParams Glow;

	mutable TArray<FBoxSphereBounds> QueryBounds[QuerySlots];
	mutable int32 QuerySlot = 0;
	// Render-thread state: the query's last answer and the smoothed fraction the quad draws with.
	// Fully visible until a query answers, so a renderer with queries off draws every sprite.
	mutable float VisibleTarget = 1.0f;
	mutable float VisibleCurrent = 1.0f;
	mutable double LastSmoothTime = -1.0;
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
