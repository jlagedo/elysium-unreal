#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;

// Reading a leader-pose follower's actual DRAWN transform, not anything a game-thread bone query
// can answer -- see Source/ElysiumUE/CLAUDE.md, "No game-thread bone query tells you where a
// leader-pose follower is drawn." GetSocketTransform answers through the leader bone map and
// GetCurrentRefToLocalMatrices rebuilds fresh matrices from current game-thread state; both can
// report a followed weapon riding the hand while the mesh draws frozen at its reference pose off
// a proxy nothing has updated. The drawn frame lives in the mesh object's own dynamic data -- the
// last packet the render thread actually received.
namespace ElysiumRenderedBone
{
	// `OutRefToLocal` is the mesh's own bind-space-to-drawn-pose matrix for `Bone` (mesh/component
	// space, not world). `OutDrawnCS` folds in the bone's inverse bind too, so
	// `OutDrawnCS * Mesh.GetComponentTransform()` is the bone's actual drawn WORLD transform.
	// False when the bone, the mesh object or its first dynamic-data packet is missing (never
	// rendered is not riding anything) -- on the install frame the packet does not exist yet.
	bool RenderedSkinningMatrices(const USkeletalMeshComponent& Mesh, FName Bone,
		FMatrix& OutRefToLocal, FMatrix& OutDrawnCS);

	// The drawn WORLD transform of `Bone` on `Mesh`'s own skeleton -- `RenderedSkinningMatrices`
	// composed with the component transform, for a caller that only needs the bone's pose.
	bool RenderedWorldTransform(const USkeletalMeshComponent& Mesh, FName Bone, FTransform& OutWorld);
}
