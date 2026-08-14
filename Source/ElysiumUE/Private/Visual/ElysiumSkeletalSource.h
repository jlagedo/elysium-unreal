#pragma once

#include "CoreMinimal.h"

/**
 * Reader for Elysium's own `.eskm` skeletal container, written by
 * `pipeline/src/elysium_pipeline/exporters/UE_mdl_skeletal.py`.
 *
 * The file is Unreal-native by the `UE_` exporter convention: centimetres, Z-up,
 * left-handed, triangle winding already reversed, quaternions already conjugated into the
 * reflected frame. Nothing here converts a coordinate -- every number is read verbatim, and
 * that is the whole reason the format exists in place of glTF.
 *
 * A bank -- a shared animation library with no geometry -- is the same file with its mesh
 * and morph sections absent, so `Vertices.Num() == 0` is how a caller tells the two apart.
 * A bank still carries a skeleton section, because its clip tracks address bones by index
 * and those names are what turn an index into a bone on the skeleton being bound to.
 */

struct FElysiumSourceBone
{
	FName Name;
	/** Index into Bones, or INDEX_NONE for a root. Always less than this bone's own index. */
	int32 Parent = INDEX_NONE;
	FTransform Local;
};

/** One model-authored socket transform, local to `Bones[Bone]`. */
struct FElysiumSourceAttachment
{
	FName Name;
	int32 Bone = INDEX_NONE;
	FTransform Local;
};

struct FElysiumSourceMaterial
{
	FString Name;
	/** Albedo path relative to the export root, or empty when the material resolved none. */
	FString Albedo;
};

struct FElysiumSourceSection
{
	FString Material;
	int32 FirstTriangle = 0;
	int32 TriangleCount = 0;
};

struct FElysiumSourceVertex
{
	FVector3f Position = FVector3f::ZeroVector;
	/**
	 * VtMB's authored shading normal, already Unreal-native and unit length.
	 *
	 * The exporter resolves it: it carries the artist's smoothing, substitutes an area-weighted
	 * geometric normal for the handful of vertices the file stores as zero, and negates for the
	 * reflected frame. Nothing downstream recomputes or repairs it.
	 */
	FVector3f Normal = FVector3f::ZAxisVector;
	FVector2f UV = FVector2f::ZeroVector;
	/** VtMB's skinned vertex carries exactly three influence slots; a zero weight is unused. */
	uint16 Bones[3] = { 0, 0, 0 };
	float Weights[3] = { 0.0f, 0.0f, 0.0f };
};

struct FElysiumSourceMorphDelta
{
	uint32 Vertex = 0;
	FVector3f Position = FVector3f::ZeroVector;
	FVector3f Normal = FVector3f::ZeroVector;
};

struct FElysiumSourceMorph
{
	FString Name;
	TArray<FElysiumSourceMorphDelta> Deltas;
};

struct FElysiumSourceTrack
{
	int32 Bone = INDEX_NONE;
	/** Either array is empty when the clip leaves that channel at the bone's bind value. */
	TArray<FVector3f> Translations;
	TArray<FQuat4f> Rotations;
};

/**
 * One clip's per-bone `weight`@0 gate, indexed by bone: 1 for a bone the clip owns, 0 for one it
 * leaves to whatever pose it is composed over.
 *
 * The distinction only exists for a clip composed as a layer, and it is not recoverable from the
 * tracks: an owned bone the clip does not animate holds its BIND pose -- a real authored pose --
 * and an unowned bone animates nothing either, so both arrive with no track.
 */
struct FElysiumSourceMask
{
	TArray<uint8> Bones;
};

struct FElysiumSourceClip
{
	FString Name;
	/**
	 * The clip this one is a difference FROM, empty for a pose of its own. Set on the derived
	 * `<additive>@<host>` clips the exporter writes once per declaring host: the tracks hold the
	 * composed pose, and subtracting this base is what turns them back into the delta in Unreal's
	 * own combine order. An additive whose base is empty was never bound to a host and is the raw
	 * VtMB clip, which no additive asset is built from.
	 */
	FString BaseName;
	int32 FrameCount = 0;
	float FrameRate = 30.0f;
	/** The raw `StudioSeqDesc.flags`; bit 0x4 marks the additive `_delta` family. */
	uint32 Flags = 0;
	/** Index into Masks, or INDEX_NONE when this clip owns every bone. */
	int32 Mask = INDEX_NONE;
	TArray<FElysiumSourceTrack> Tracks;
};

/** One baked-native recipe for the stock AnimDynamics hair proof. */
struct FElysiumSourceHairDynamicsChain
{
	FName BoundBone;
	FName ChainEnd;
	float GravityScale = 1.0f;
	float Damping = 0.9f;
	float AngularSpring = 0.0f;
	float ConeAngleDegrees = 0.0f;
};

struct FElysiumSkeletalSource
{
	TArray<FElysiumSourceBone> Bones;
	/** Optional in a version-7 container; regenerated character bodies carry the MDL attachments. */
	TArray<FElysiumSourceAttachment> Attachments;
	TArray<FElysiumSourceMaterial> Materials;
	TArray<FElysiumSourceSection> Sections;
	TArray<FElysiumSourceVertex> Vertices;
	/** Three per triangle, indexing Vertices, already wound for Unreal. */
	TArray<uint32> Indices;
	TArray<FElysiumSourceMorph> Morphs;
	/** De-duplicated across the file; a clip references one by index. */
	TArray<FElysiumSourceMask> Masks;
	TArray<FElysiumSourceClip> Clips;
	/** Optional, exact-model allow-listed hair recipes; absent on every body outside the proof. */
	TArray<FElysiumSourceHairDynamicsChain> HairDynamics;

	/**
	 * Read a whole `.eskm` off disk. Returns false with a reason in OutError; a truncated or
	 * mis-versioned file fails here rather than producing a half-built asset downstream.
	 */
	static bool Load(const FString& Path, FElysiumSkeletalSource& Out, FString& OutError);

	/**
	 * Read only the bone tree, leaving every other array empty. Same validation, same errors.
	 *
	 * A rig family's skeleton is seeded from every declared member so its bone tree and reference
	 * pose do not depend on which members a slice happened to name -- and a member contributes
	 * nothing to that but its bones. Decoding the clips too would make seeding one family cost
	 * more than baking it.
	 */
	static bool LoadBones(const FString& Path, FElysiumSkeletalSource& Out, FString& OutError);
};
