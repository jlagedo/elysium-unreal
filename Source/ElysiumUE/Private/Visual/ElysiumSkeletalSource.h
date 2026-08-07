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
};
