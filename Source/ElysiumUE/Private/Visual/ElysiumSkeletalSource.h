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

struct FElysiumSkeletalSource
{
	TArray<FElysiumSourceBone> Bones;
	TArray<FElysiumSourceMaterial> Materials;
	TArray<FElysiumSourceSection> Sections;
	TArray<FElysiumSourceVertex> Vertices;
	/** Three per triangle, indexing Vertices, already wound for Unreal. */
	TArray<uint32> Indices;
	TArray<FElysiumSourceMorph> Morphs;
	/** De-duplicated across the file; a clip references one by index. */
	TArray<FElysiumSourceMask> Masks;
	TArray<FElysiumSourceClip> Clips;

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

	/**
	 * Name the bones this container's clips actually MOVE, appending to OutBones. Same validation,
	 * same errors.
	 *
	 * A clip carries a translation track wherever VtMB's own channel pointer is non-zero and
	 * wherever the exporter forces a bind track so both sides of an additive subtraction name the
	 * same container's bind -- so most tracks are the emitting model's bind pose repeated frame
	 * after frame rather than motion. Only a track that TRAVELS is animation -- VtMB stores the
	 * channel as integer counts of a per-bone scale, so a track that animates nothing still wobbles
	 * by a step -- and every other bone's translation belongs to whichever body plays the clip.
	 *
	 * That distinction is what a shared skeleton's per-bone translation retargeting has to be
	 * derived from, and it is not in the bone tree: only the clip payload says which bones move.
	 * Nothing here is decoded -- rotations are stepped over and a track stops being read the moment
	 * it is known to vary -- so the answer costs one pass over the file.
	 *
	 * Appending is deliberate: a caller unions a whole rig family's containers into one array.
	 */
	static bool LoadTranslatedBones(const FString& Path, TArray<FName>& OutBones, FString& OutError);
};
