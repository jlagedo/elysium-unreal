#pragma once

#include "CoreMinimal.h"

// The $envmap reflection channel (roadmap 7.5), shared by everything that binds it: the
// runtime material factory, the map actor's live overrides, and the tests.
//
// VtMB composites its cubemap reflection as `(base + cube*mask*tint) * lightmap * 2` -- an
// ALBEDO term the light then multiplies, not an additive overlay -- so a reflective surface in
// an unlit room stays dark. That is already a light-modulated specular response in all but
// name, which is what makes a real reflection channel the faithful port rather than a
// liberty. The exported `tex/cube/` faces are never sampled; Lumen resolves the reflection
// against the live scene. Full RE + the whole-game authoring survey: docs/reflections.md.
//
// Keep these in sync with tools/make_world_materials.py (the master's parameter defaults) and
// tools/bake_map.py (what a baked material instance binds over them).
namespace ElysiumReflections
{
	// The non-reflective end is Lambert, which is what VtMB's world is (docs/lighting.md:
	// METALLIC 0, SPECULAR 0, ROUGHNESS 1) and what the light rig already assumes when it sets
	// specular_scale = 0 on every source. A surface reflects because its VMT carries $envmap.
	inline constexpr float RoughBase = 1.0f;
	inline constexpr float RoughReflect = 0.15f;
	inline constexpr float SpecBase = 0.0f;
	inline constexpr float SpecReflect = 0.5f;

	// Parameter names on the three lit world masters, authored by make_world_materials.py.
	// Bound by fixed name -- a rename that misses one of these binds nothing and is silent at
	// runtime, so the Content test tier asserts the master carries every one.
	namespace Params
	{
		inline const FName EnvMask(TEXT("EnvMask"));
		inline const FName EnvStrength(TEXT("EnvStrength"));
		inline const FName EnvTint(TEXT("EnvTint"));
		inline const FName MetalMask(TEXT("MetalMask"));
		inline const FName RoughBase(TEXT("RoughBase"));
		inline const FName RoughReflect(TEXT("RoughReflect"));
		inline const FName SpecBase(TEXT("SpecBase"));
		inline const FName SpecReflect(TEXT("SpecReflect"));
	}
}
