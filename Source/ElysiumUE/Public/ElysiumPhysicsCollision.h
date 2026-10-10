#pragma once

#include "CoreMinimal.h"

/** A real, immutable single-ledge collision surface, in IVP metres. This is construction
 * geometry, not an entity bounds record or a simulated body. Triangle/edge feature order is
 * retail's generated box seed order. Consumers can use the complete compact surface directly. */
class ELYSIUMUE_API FElysiumPhysicsCollide final
{
public:
	FElysiumPhysicsCollide(const FElysiumPhysicsCollide&) = delete;
	FElysiumPhysicsCollide& operator=(const FElysiumPhysicsCollide&) = delete;
	TConstArrayView<uint8> CompactSurface() const { return Bytes; }
	FVector3f Vertex(int32 Index) const;
	uint32 TriangleWord(int32 Triangle, int32 Word) const;
	FVector3f MassCenter() const;
	FVector3f RotationInertia() const;
	float Radius() const;

private:
	friend class FElysiumPhysicsCollision;
	FElysiumPhysicsCollide(const FVector3f& SourceMins, const FVector3f& SourceMaxs);
	~FElysiumPhysicsCollide() = default;
	TArray<uint8> Bytes;
};

/** Collision007's independent box-construction service. Cache entries borrow raw geometry
 * pointers; returned allocations are released explicitly with DestroyCollide. As in retail,
 * destruction does not invalidate the cache, and service destruction releases cache storage
 * only. Never use a destroyed shape (or destroy a cached allocation twice). */
class ELYSIUMUE_API FElysiumPhysicsCollision final
{
public:
	FElysiumPhysicsCollision() = default;
	~FElysiumPhysicsCollision() = default;
	FElysiumPhysicsCollision(const FElysiumPhysicsCollision&) = delete;
	FElysiumPhysicsCollision& operator=(const FElysiumPhysicsCollision&) = delete;
	FElysiumPhysicsCollide* BBoxToCollide(const FVector3f& SourceMins, const FVector3f& SourceMaxs);
	static void DestroyCollide(FElysiumPhysicsCollide* Geometry);

private:
	struct FBoxCacheEntry
	{
		FVector3f Mins;
		FVector3f Maxs;
		FElysiumPhysicsCollide* Geometry;
	};
	TArray<FBoxCacheEntry> BoxCache;
};
