#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

// A model-authored `$attachment` read off the **asset**, with no component standing.
//
// VtMB's attachments are baked into the skeletal asset as ordinary Unreal sockets
// (`Editor/ElysiumSkeletalBuild.cpp`, "Attachments are model-authored data resolved offline"), and
// `FElysiumCataloguePlacedModel` keeps both the static and the skeletal representation of an
// admitted model resident. A placed prop normally stands only its static reduction, so the socket
// table has to be reached through the model's `SkeletalMesh` row rather than through a body — the
// prop's attachments are rig data, and a rigid prop's rig is its bind pose.
namespace ElysiumPlacedAttachments
{
	// The socket's transform in the mesh's own component space, composed from the ref skeleton:
	// the socket's bone-local transform times that bone's ref-pose chain up to the root. False when
	// the asset is null, declares no such socket, or names a bone the ref skeleton does not carry.
	bool RefPoseTransform(const USkeletalMesh* Mesh, FName Socket, FTransform& Out);
}
