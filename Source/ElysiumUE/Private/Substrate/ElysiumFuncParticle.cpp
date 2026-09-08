// func_particle -- CFuncParticle (`docs/vtmb/effects.md` §3.1):
// the env_particle leaf on a brush. `Activate` forces attach mode 15 (a uniform random point in
// the brush's world AABB, no solid test) whatever the key says, and folds the brush's size scalar
// -- clamp(volume / 128^3, 0.01, 100) in Source units -- into the one rate float. The 98 rows are
// all rain boxes; the rain look is weather's, the class body is this lane's.

#include "Substrate/ElysiumEnvParticle.h"

#include "ElysiumClassRegistry.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumFuncParticle, Log, All);

class FElysiumFuncParticle final : public FElysiumEnvParticle
{
public:
	virtual void Spawn() override
	{
		BrushBoundsCm = FBox(ForceInit);
		VolumeScale = 1.0f;
		if (Def)
		{
			for (const FElysiumConvexHull& Hull : Def->Hulls)
			{
				for (const FVector& Vertex : Hull.Vertices)
				{
					BrushBoundsCm += Origin + Vertex;
				}
			}
		}
		if (BrushBoundsCm.IsValid)
		{
			// CFuncParticle::Activate's sizeScalar over the Source-unit extents: a 128-unit cube is 1.
			const FVector ExtentIn = BrushBoundsCm.GetSize() / 2.54;
			const double Volume = FMath::Abs(ExtentIn.X) * FMath::Abs(ExtentIn.Y) * FMath::Abs(ExtentIn.Z);
			VolumeScale = static_cast<float>(FMath::Clamp(Volume / 2097152.0, 0.01, 100.0));
		}
		else
		{
			UE_LOG(LogElysiumFuncParticle, Warning,
				TEXT("%s carries no brush hulls; the emitter spawns at its origin with unity volume"),
				*DebugString());
		}
		FElysiumEnvParticle::Spawn();
	}

	virtual void Activate() override
	{
		FElysiumEnvParticle::Activate();
		AttachType = 15;
		Publish();
	}
};

static TUniquePtr<FElysiumEntity> MakeFuncParticle() { return MakeUnique<FElysiumFuncParticle>(); }

// Derives from env_particle in the registry too: the inputs and fields resolve up the chain.
static FElysiumClassRegistrar GRegFuncParticle(
	TEXT("func_particle"), TEXT("env_particle"), &MakeFuncParticle,
	[](FElysiumClassDesc&) {});
