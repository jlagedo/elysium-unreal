#pragma once

#include "CoreMinimal.h"
#include "ElysiumMapTransportSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

// Single source of truth for the external export root. Runtime code only reads intermediates
// produced before launch; it never invokes the offline Python pipeline.
struct FElysiumContentPaths
{
	static FString Root()
	{
		FString Value;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ElysiumContentRoot="), Value))
		{
			Value = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_EXPORT_ROOT"));
		}
		if (Value.IsEmpty())
		{
			const FString WorkRoot =
				FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
			if (!WorkRoot.IsEmpty())
			{
				Value = WorkRoot / TEXT("exports");
			}
		}
		if (Value.IsEmpty())
		{
			return FString();
		}
		Value = FPaths::ConvertRelativePathToFull(Value);
		FPaths::NormalizeDirectoryName(Value);
		return Value;
	}
	static bool IsConfigured() { return !Root().IsEmpty(); }

	// The deployed, gitignored, runtime-parsed corpus written by `uv run elysium import` --
	// distinct from Root(), which is the offline pipeline's loose export tree. vdata is the first
	// family imported onto it (docs/project/seam_migration.md "Settled"); more follow as the
	// migration converges the runtime onto one resolution mechanism. `-ElysiumCorpusRoot=` mirrors
	// Root()'s `-ElysiumContentRoot=` pin so tests/dev can redirect the corpus independently.
	static FString CorpusRoot()
	{
		FString Value;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ElysiumCorpusRoot="), Value))
		{
			Value = FPaths::ProjectContentDir() / TEXT("ElysiumCorpus");
		}
		Value = FPaths::ConvertRelativePathToFull(Value);
		FPaths::NormalizeDirectoryName(Value);
		return Value;
	}
	// Offline/content-test completeness signal only. Gameplay reads the artifacts that are present
	// and must never refuse to boot solely because this marker exists.
	//
	// The corpus is incomplete per DOMAIN, and the aggregate marker says only that SOMETHING is.
	// A domain is named for the export bundle that clears it -- `npc`, `audio`, `vdata`, `scenes`,
	// `scripts`, plus `maps` for the map exports -- so a test that reads one domain abstains only
	// while THAT domain is missing. `pipeline/src/elysium_pipeline/clean.py` writes both.
	static FString IncompleteMarker() { return Root() / TEXT(".elysium-incomplete"); }
	static FString IncompleteMarker(const TCHAR* Domain)
	{
		return IncompleteMarker() + TEXT(".") + Domain;
	}
	static bool IsIncomplete()
	{
		const FString Value = Root();
		return !Value.IsEmpty() && IFileManager::Get().FileExists(*IncompleteMarker());
	}
	static bool IsIncomplete(const TCHAR* Domain)
	{
		const FString Value = Root();
		return !Value.IsEmpty() && IFileManager::Get().FileExists(*IncompleteMarker(Domain));
	}

	// Original authored content under `/Game/ElysiumAuthored`. The one tracked package namespace.
	// Nothing here is generated and nothing here is derived from the user's install, so a clean or a
	// regeneration must never write to or remove it (`Content/CLAUDE.md`).
	static FString AuthoredMount() { return TEXT("/Game/ElysiumAuthored"); }
	// What every VtMB garment is made of, in Chaos's own terms, typed by
	// Public/ElysiumClothTuningConfig.h. The authored payload states a garment's shape and its
	// constraint graph and nothing about its material -- VtMB's solver had no density, friction or
	// thickness to state -- so that call is authored here rather than decoded or compiled in.
	static FString AuthoredClothTuning()
	{
		const FString Asset = TEXT("DA_ClothTuning");
		return AuthoredMount() / TEXT("Cloth") / Asset + TEXT(".") + Asset;
	}
	// The corpus-wide iris size / eye shift baseline every rendered eye composes against, typed by
	// Public/ElysiumEyeTuningConfig.h (R4.5). Beside AuthoredClothTuning(), the same "authored
	// judgement with no generator" shape.
	static FString AuthoredEyeTuning()
	{
		const FString Asset = TEXT("DA_EyeTuning");
		return AuthoredMount() / TEXT("Eyes") / Asset + TEXT(".") + Asset;
	}

	// The project's own top-level `/Game/ElysiumGenerated` namespace: every package this project
	// itself generates rather than authors or decodes from the VtMB install. A text prefix of
	// `/Game/ElysiumAuthored` (`AuthoredMount()`) -- the two are never interchangeable.
	static FString GeneratedMount() { return TEXT("/Game/ElysiumGenerated"); }

	// The empty boot map both the front end and the character stage stand in
	// (`GameDefaultMap`/`EditorStartupMap` in Config/DefaultEngine.ini). Its own subfolder, so its
	// package never collides with a sibling subfolder's.
	static FString BootMount() { return GeneratedMount() / TEXT("Boot"); }
	// The dialogue camera profile set, a sibling of the boot map rather than nested under it.
	static FString CameraDir() { return GeneratedMount() / TEXT("Camera"); }
	static FString DialogueCameraSet()
	{
		const FString Asset = TEXT("DA_ElysiumDialogueCameraSet");
		return CameraDir() / Asset + TEXT(".") + Asset;
	}

	// The hand-authored world material masters (`make_world_materials.py`), under the project's own
	// generated namespace rather than VtMB's -- like Audio and UI below, none of them carries a
	// VtMB-authored identity worth keeping distinct.
	static FString MaterialsDir() { return GeneratedMount() / TEXT("Materials"); }
	static FString Material(const FString& Name) { return MaterialsDir() / Name + TEXT(".") + Name; }

	// The sound concurrency/class tables addressed by category and the UI font faces
	// (`make_ui_fonts.py`), under the project's own generated namespace rather than VtMB's --
	// unlike the materials and cloth above, neither carries a VtMB-authored identity worth keeping
	// distinct.
	static FString AudioDir() { return GeneratedMount() / TEXT("Audio"); }
	static FString AudioConcurrency(const FString& Category)
	{
		const FString Asset = TEXT("Concurrency_") + Category;
		return AudioDir() / Asset + TEXT(".") + Asset;
	}
	static FString AudioSoundClass(const FString& Category)
	{
		const FString Asset = TEXT("SC_") + Category;
		return AudioDir() / Asset + TEXT(".") + Asset;
	}

	static FString UiFontsDir() { return GeneratedMount() / TEXT("UI") / TEXT("Fonts"); }
	static FString UiFontFace(const FString& Face) { return UiFontsDir() / Face + TEXT(".") + Face; }

	// Baked map look (`pipeline/unreal/bake_map.py`).
	// The look of a map — world + sky geometry, materials, textures, props, lights, fog — is
	// offline-baked into real .uasset content under the /ElysiumBaked plugin mount, and the map
	// IS a real .umap the engine opens. These are package paths (a virtual content root), not
	// filesystem paths, so they take no FPaths::ProjectDir. The mount's Content/ is game-derived
	// and gitignored exactly like Root(); only the .uplugin descriptor is committed.
	static FString BakedMount() { return TEXT("/ElysiumBaked"); }
	static FString BakedMapDir(const FString& Map)
	{
		const FString Level = BakedUnit(TEXT("vtmb:map:") + Map, TEXT(""));
		int32 Slash = INDEX_NONE;
		return Level.FindLastChar(TEXT('/'), Slash) ? Level.Left(Slash) : FString();
	}
	// The .umap UElysiumMapSubsystem::Travel opens for this map.
	static FString BakedLevel(const FString& Map) { return BakedMapDir(Map) / Map; }
	// This map's entity table as cooked content (R4.1): the `UElysiumMapEntities` that replaces
	// `<map>.ents`. One asset per map, beside the map's own .umap, because an entity table is a
	// property of one map and of nothing else. Its Python twin is
	// `elysium_pipeline.importers.map_entities.asset_path`.
	static FString BakedMapEntities(const FString& Map)
	{
		const FString Asset = TEXT("DA_") + Map + TEXT("_Entities");
		return BakedMapDir(Map) / Asset + TEXT(".") + Asset;
	}
	// This map's collision as cooked content (R4.2): the `UElysiumMapCollisionPayload` that
	// replaces `<map>.hulls`, `<map>.dispcol` and the per-brush-entity runtime cook. One asset per
	// map, beside `DA_<map>_Entities`. Its Python twin is
	// `elysium_pipeline.importers.map_collision.asset_path`.
	static FString BakedMapCollision(const FString& Map)
	{
		const FString Asset = TEXT("DA_") + Map + TEXT("_Collision");
		return BakedMapDir(Map) / Asset + TEXT(".") + Asset;
	}
	// This map's hand-tuned light overrides (R4.3): the `UElysiumLightCalibration` merge-row asset
	// `UElysiumLightRig::Adopt` applies on top of the calibrated baseline. One asset per map, beside
	// `DA_<map>_Entities`/`DA_<map>_Collision`; unlike those, no producer writes it -- authored
	// directly in the editor, absent until an owner hand-tunes a map.
	static FString BakedMapLightCalibration(const FString& Map)
	{
		const FString Asset = TEXT("DA_") + Map + TEXT("_LightCalibration");
		return BakedMapDir(Map) / Asset + TEXT(".") + Asset;
	}
	// This map's environment as cooked content (R4.4): the `UElysiumMapEnvironment` that replaces
	// `<map>.env`, `<map>.sky` and `<map>.spawn` -- the 2D-sky/fog set, the 3D-skybox miniature's
	// placement transform, and the initial player spawn. One asset per map, beside
	// `DA_<map>_Entities`/`DA_<map>_Collision`. Its Python twin is
	// `elysium_pipeline.importers.map_environment.asset_path`.
	static FString BakedMapEnvironment(const FString& Map)
	{
		const FString Asset = TEXT("DA_") + Map + TEXT("_Environment");
		return BakedMapDir(Map) / Asset + TEXT(".") + Asset;
	}
	// Shared corpus (`pipeline/unreal/bake_map.py` CorpusBake).
	// A texture, a material and a static model belong to the user's install, not to a map:
	// `materials/metal/metalox` decodes to the same bytes whichever BSP named it, and one doorknob
	// model is one doorknob however many maps hang it on a door. So each is decoded once and baked
	// once here, and every map references the single asset rather than carrying a copy.
	//
	// Its Python twin is `elysium_pipeline.shared_corpus`, which owns the key rules and the
	// shared/per-map predicate; these paths must agree with it exactly.
	static FString BakedSharedDir() { return BakedMount() / TEXT("Shared"); }
	static FString BakedSharedTextures() { return BakedSharedDir() / TEXT("Textures"); }
	static FString BakedSharedMaterials() { return BakedSharedDir() / TEXT("Materials"); }
	static FString BakedSharedMeshes() { return BakedSharedDir() / TEXT("Meshes"); }
	// The texture lane's package root (`uv run elysium import textures`): one `T_` per
	// `vtmb:texture:` unit, `docs/architecture/seam_map_texture.md` -> "Identity and naming".
	static FString BakedTexturesDir() { return BakedMount() / TEXT("Textures"); }
	// The baked-unit contract's object-path resolver. Python's asset_paths.baked_unit returns
	// the corresponding package path; both walk pipeline/tests/fixtures/baked_paths.json.
	static FString BakedUnit(const FString& Id, const FString& Prefix,
		const FString& Role = FString(), const FString& Label = FString())
	{
		FString Kind, Key;
		if (!Id.StartsWith(TEXT("vtmb:"), ESearchCase::CaseSensitive)
			|| !Id.Mid(5).Split(TEXT(":"), &Kind, &Key)) return FString();
		static const TMap<FString, FString> Roots = {
			{TEXT("texture"), TEXT("Textures")}, {TEXT("material"), TEXT("Materials")},
			{TEXT("model"), TEXT("Models")}, {TEXT("surface-property"), TEXT("SurfaceProperties")},
			{TEXT("map"), TEXT("Maps")}, {TEXT("expression-table"), TEXT("ExpressionTables")},
			{TEXT("sound"), TEXT("Sounds")}, {TEXT("particle"), TEXT("Particles")},
			{TEXT("scene"), TEXT("Scenes")}};
		const FString* Root = Roots.Find(Kind);
		if (!Root) return FString();
		const auto ValidSegment = [](const FString& Part)
		{
			return !Part.IsEmpty() && Part != TEXT(".") && Part != TEXT("..")
				&& !Part.StartsWith(TEXT("_")) && !Part.Contains(TEXT("/"))
				&& !Part.Contains(TEXT("\\")) && !Part.Contains(TEXT(":"));
		};
		TArray<FString> Parts;
		Key.ParseIntoArray(Parts, TEXT("/"), false);
		if (Parts.IsEmpty()) return FString();
		for (FString& Part : Parts)
		{
			if (!ValidSegment(Part)) return FString();
			Part = SafeName(Part);
		}
		FString Package = BakedMount() / *Root;
		for (int32 I = 0; I < Parts.Num() - 1; ++I) Package /= Parts[I];
		const FString Base = Parts.Last();
		FString Asset;
		if (Prefix.IsEmpty())
		{
			if (Kind != TEXT("map") || !Role.IsEmpty() || !Label.IsEmpty()) return FString();
			Package /= Base;
			Asset = Base;
		}
		else
		{
			static const TSet<FString> Prefixes = {TEXT("T"), TEXT("TC"), TEXT("TA"), TEXT("MI"),
				TEXT("SM"), TEXT("SK"), TEXT("SKEL"), TEXT("A"), TEXT("BS"), TEXT("CLOTH"),
				TEXT("PHYS"), TEXT("DYN"), TEXT("PM"), TEXT("DA"), TEXT("NS"), TEXT("SW")};
			if (!Prefixes.Contains(Prefix)) return FString();
			if (!Label.IsEmpty())
			{
				if (!ValidSegment(Label)) return FString();
				Package /= Base;
			}
			Asset = Prefix + TEXT("_") + (Label.IsEmpty() ? Base : SafeName(Label));
			if (!Role.IsEmpty())
			{
				if (!ValidSegment(Role)) return FString();
				Asset += TEXT("_") + SafeName(Role);
			}
		}
		return Package / Asset + TEXT(".") + Asset;
	}
	// `<dir>/<stem>` (a `materials/`-relative path, the bare unit key) ->
	// `/ElysiumBaked/Textures/<dir>/T_<safe stem>.T_<safe stem>`, the object path of the imported 2D
	// texture. The C++ twin of `importers.textures.asset_path_for(key, "Texture2D")`: every path
	// part through `MaterialSafeName` (`asset_names.safe_name`), the stem with the `T_` prefix. The
	// UI names its art by exactly this key (`UI/ElysiumUiArt.h`). Empty for an empty key.
	static FString BakedTexture(const FString& Key)
	{
		return BakedTextureOfClass(Key, TEXT("T_"));
	}
	// The `TC_` twin of `BakedTexture` for a key whose unit decoded to six faces --
	// `importers.textures.CLASS_PREFIX["TextureCube"]`. The prefix is the ONLY difference: one
	// unit key names at most one asset, and the class it took is a property of the decoded image,
	// so a cube is addressed by the same key with the class's own prefix. VtMB's baked env probes
	// (`maps/<map>/cubemapdefault`, `maps/<map>/c<x>_<y>_<z>`) are the corpus's cubes.
	static FString BakedCubeTexture(const FString& Key)
	{
		return BakedTextureOfClass(Key, TEXT("TC_"));
	}
	// The one fold both spellings share: the key's directories through `MaterialSafeName`, the stem
	// under the asset class's prefix.
	static FString BakedTextureOfClass(const FString& Key, const TCHAR* ClassPrefix)
	{
		FString Prefix(ClassPrefix);
		Prefix.RemoveFromEnd(TEXT("_"));
		return BakedUnit(TEXT("vtmb:texture:") + Key, Prefix);
	}
	// The material lane's package root (`uv run elysium import materials`): one `MI_` per
	// `vtmb:material:` unit, `docs/architecture/seam_map_material.md` -> "Identity and naming".
	static FString BakedMaterialsDir() { return BakedMount() / TEXT("Materials"); }
	// `vtmb:material:<dir>/<stem>` -> `/ElysiumBaked/Materials/<dir>/MI_<safe stem>.MI_<safe stem>`,
	// the object path of the imported instance. The C++ twin of
	// `importers.materials.asset_path_for`: every path part goes through `MaterialSafeName`
	// (`asset_names.safe_name`), the stem takes the `MI_` prefix. Empty for anything that is not a
	// `vtmb:material:` id, so a stale sidecar token never folds to a path that happens to exist.
	static FString BakedMaterial(const FString& MaterialId)
	{
		return MaterialId.StartsWith(TEXT("vtmb:material:"), ESearchCase::CaseSensitive)
			? BakedUnit(MaterialId, TEXT("MI")) : FString();
	}
	// The corpus instance of a world material VBSP patched into ONE map
	// (`shared_corpus.base_material` / `cubemap_of`): the face's texdata names
	// `maps/<map>/<material>`, so the unit key carries that prefix and the instance lands under
	// `/ElysiumBaked/Materials/maps/<map>/<dir>/MI_<safe stem>`.
	// It is an ordinary corpus instance -- a child of the unpatched unit's own `MI_`, staged by the
	// same `import materials` run -- not a per-map package: the V2 lane authors NO `<map>/Materials`
	// package at all. `Material` is the bare authored key (`ground/streetasan`), without the
	// `maps/<map>/` prefix this composes and without the `@cubemapdefault` tag the export's own
	// `world_material_key` carries.
	static FString BakedMapMaterial(const FString& Map, const FString& Material)
	{
		if (Map.IsEmpty() || Material.IsEmpty())
		{
			return FString();
		}
		return BakedMaterial(TEXT("vtmb:material:maps/") + Map + TEXT("/") + Material);
	}
	// The PROJECTOR twin of the instance above (R7.2 ruling 2): every `$decal` unit and every
	// `decalmodulate` unit stages a second shared instance beside its surface one, in the same
	// package directory, named `MI_<safe stem>_Decal` and parented to `M_V2_Decal`. A
	// `UDecalComponent` draws only an `MD_DeferredDecal` material, so this is the asset a placed
	// or laid decal binds -- never `BakedMaterial`'s, which is surface-domain.
	//
	// Empty for anything that is not a `vtmb:material:` id, for the same reason `BakedMaterial` is:
	// a stale token must not fold to a path that happens to exist.
	static FString BakedDecalMaterial(const FString& MaterialId)
	{
		const FString Surface = BakedMaterial(MaterialId);
		if (Surface.IsEmpty())
		{
			return FString();
		}
		// `<dir>/MI_<stem>.MI_<stem>` -> `<dir>/MI_<stem>_Decal.MI_<stem>_Decal`, off the one
		// name both halves of the object path already carry.
		FString Package, Asset;
		Surface.Split(TEXT("."), &Package, &Asset, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		const FString DecalAsset = Asset + TEXT("_Decal");
		return Package + TEXT("_Decal.") + DecalAsset;
	}
	// `asset_names.safe_name`: every run of characters outside [A-Za-z0-9_] becomes one
	// underscore, leading and trailing underscores are stripped, and a name that folds away
	// entirely is `unnamed`. NOT BakedAssetName (which keeps the leading/trailing run) and NOT
	// PropModelStem (which keeps `.` and `-` and lower-cases) — three folds, three contracts.
	static FString MaterialSafeName(const FString& Text) { return SafeName(Text); }
	static FString SafeName(const FString& Text)
	{
		FString Out;
		Out.Reserve(Text.Len());
		bool bInRun = false;
		for (TCHAR Ch : Text)
		{
			const bool bLegal = (Ch >= TEXT('a') && Ch <= TEXT('z')) ||
				(Ch >= TEXT('A') && Ch <= TEXT('Z')) ||
				(Ch >= TEXT('0') && Ch <= TEXT('9')) || Ch == TEXT('_');
			if (bLegal)
			{
				Out.AppendChar(Ch);
				bInRun = false;
			}
			else if (!bInRun)
			{
				Out.AppendChar(TEXT('_'));
				bInRun = true;
			}
		}
		while (Out.RemoveFromStart(TEXT("_"))) {}
		while (Out.RemoveFromEnd(TEXT("_"))) {}
		return Out.IsEmpty() ? FString(TEXT("unnamed")) : Out;
	}
	// The V2 model corpus (R1): one `UStaticMesh` per referenced model unit, imported from the
	// published GLB with its slots bound to the V2 material instances and its collision cooked from
	// VtMB's own convex hulls (docs/architecture/seam_map_model.md -> "Import" -> "Identity and
	// naming"). A sibling of the legacy shared bake at the mount root, never over it: the two
	// corpora carry the same asset names, stems and slot names, so which root a map reads is the
	// only difference between them. Its Python twin is
	// `elysium_pipeline.importers.models.PACKAGE_ROOT`.
	static FString BakedMeshes() { return BakedMount() / TEXT("Meshes"); }
	// Which model corpus one map's props resolve against (R5.1). Per map, never per system: a map
	// listed under `MapsOnV2Models` reads the V2 corpus, every other map keeps the legacy shared
	// bake exactly as it always has. The two lists on that settings page are deliberately distinct
	// -- `sp_theatre` is on the R4.6 entity/collision/environment transport but its models have not
	// been imported, so its props must keep resolving at the legacy root.
	static FString BakedMeshesFor(const FString& Map)
	{
		return ElysiumMapTransport::IsMapOnV2Models(Map) ? BakedMeshes() : BakedSharedMeshes();
	}
	// One baked static model, by the same stem the `.props` sidecar, `model_mesh` and the V2 lane
	// all name (the whole model path folded -- `PropModelStem` below). Package path is
	// <dir>/SM_<stem>.SM_<stem>. The map is required rather than defaulted so no call site can
	// silently land on the legacy root for a map that has been cut over.
	static FString BakedPropMesh(const FString& Stem, const FString& Map)
	{
		const FString Asset = TEXT("SM_") + SafeName(Stem);
		return BakedMeshesFor(Map) / Asset + TEXT(".") + Asset;
	}
	// An item's ground model is a static model like any other and sits in the same corpus. The
	// separate name is kept because the caller's intent differs, not because the asset does.
	static FString BakedItemMesh(const FString& Stem, const FString& Map)
	{
		return BakedPropMesh(Stem, Map);
	}
	// The prop skin table: every alternate skin family of every model, resolved to the material
	// instances at bake time. One table per corpus, because a model's skin families and the
	// materials they repaint are both properties of the install rather than of a map -- but which
	// of the two corpora a map reads is still the map's own cutover call.
	static FString BakedPropSkins(const FString& Map)
	{
		const FString Asset = TEXT("DA_ElysiumPropSkins");
		return BakedMeshesFor(Map) / Asset + TEXT(".") + Asset;
	}
	static FString BakedBrushMesh(const FString& Map, const FString& Stem)
	{
		const FString Asset = TEXT("SM_") + Stem;
		return BakedMapDir(Map) / TEXT("Brushes") / Asset + TEXT(".") + Asset;
	}
	// The decoded-model stem for a VtMB `models/...mdl` path, which is the whole path folded --
	// NOT its base filename. `models/items/Rings/Ground/Ring03.mdl` is
	// `models_items_rings_ground_ring03`, and that is the name the bake gives the asset.
	//
	// Its Python twin is `elysium_pipeline.formats.mdl.sanitize`: lower case, then every
	// character outside [a-z0-9._-] replaced one-for-one by an underscore. Unlike BakedAssetName
	// above it does NOT collapse runs and it keeps `.` and `-`, so the two must not be swapped.
	static FString PropModelStem(const FString& ModelPath)
	{
		FString Raw = ModelPath;
		Raw.RemoveFromEnd(TEXT(".mdl"), ESearchCase::IgnoreCase);
		FString Out;
		Out.Reserve(Raw.Len());
		for (TCHAR Ch : Raw)
		{
			Ch = FChar::ToLower(Ch);
			const bool bLegal = (Ch >= TEXT('a') && Ch <= TEXT('z')) ||
				(Ch >= TEXT('0') && Ch <= TEXT('9')) ||
				Ch == TEXT('.') || Ch == TEXT('_') || Ch == TEXT('-');
			Out.AppendChar(bLegal ? Ch : TEXT('_'));
		}
		return Out;
	}
	// One Niagara system per VtMB emitter definition the map places, authored at bake time from
	// the compiled particle closure. `Definition` is the bare definition name (`impact_flesh_emitter`).
	static FString BakedParticleSystem(const FString& Map, const FString& Definition)
	{
		const FString Asset = TEXT("NS_") + Definition;
		return BakedMapDir(Map) / TEXT("Particles") / Asset + TEXT(".") + Asset;
	}

	// Baked characters (`pipeline/unreal/bake_characters.py`).
	// The cast bakes to the same mount as the maps but is not per-map: a character outlives any
	// map epoch. `Stem` is the model name the export uses ("smiling_jack"); `Owner` is the stem
	// that OWNS a clip — the body itself for its own dialogue clips, the bank stem for everything
	// resolved through the include DAG, which is npc_manifest.json's own ownership resolution.
	// `Clip` is the sequence label with no leading '@'.
	// Every body carries its OWN USkeleton, seeded from its own container alone — the same shape
	// an animated prop or a wielded weapon has. A shared bank is still one UAnimSequence rather
	// than one per body: banks bake once onto bank skeletons of their own, and each body skeleton
	// declares those compatible, so the engine remaps a bank clip by bone name at evaluation.
	// The player animation graph's generated class. A local, regenerable package like every other
	// under `/Game/ElysiumGenerated`, rebuilt from the tracked graph text by
	// `pipeline/unreal/make_player_anim_bp.py`.
	static FString PlayerAnimBlueprintClass()
	{
		return GeneratedMount() / TEXT("Animation/ABP_ElysiumBiped.ABP_ElysiumBiped_C");
	}

	// Baked animated props (`pipeline/unreal/bake_characters.py`).
	// Apart from the cast only in folder and lifetime: a prop owns its own skeleton the same way
	// a body does, and its stem is the whole address. It plays no shared bank, so nothing is
	// declared compatible with it.
	static FString BakedPropDir() { return BakedMount() / TEXT("Props"); }
	static FString BakedPropPackage(const FString& Stem) { return BakedPropDir() / Stem; }
	static FString BakedPropSkeleton(const FString& Stem)
	{
		const FString Asset = TEXT("SKEL_") + Stem;
		return BakedPropPackage(Stem) / Asset + TEXT(".") + Asset;
	}
	static FString BakedPropSkeletalMesh(const FString& Stem)
	{
		const FString Asset = TEXT("SK_") + Stem;
		return BakedPropPackage(Stem) / Asset + TEXT(".") + Asset;
	}
	static FString BakedPropAnim(const FString& Stem, const FString& Clip)
	{
		const FString Asset = TEXT("A_") + BakedAssetName(Clip);
		return BakedPropPackage(Stem) / Asset + TEXT(".") + Asset;
	}
	static FString BakedPropBlendSpace(const FString& Stem, const FString& Label)
	{
		const FString Asset = TEXT("BS_") + BakedAssetName(Label);
		return BakedPropPackage(Stem) / Asset + TEXT(".") + Asset;
	}

	// Baked wield corpus (`pipeline/unreal/bake_wield.py`).
	// The geometry a drawn weapon puts in a character's hand. Shaped like an animated prop — a
	// weapon owns a private skeleton and its stem is the whole address — but kept apart because
	// the corpus and its lifetime are the item definitions', not the cast's.
	static FString BakedItemsDir() { return BakedMount() / TEXT("Items"); }
	static FString BakedWieldDir() { return BakedItemsDir() / TEXT("Wield"); }
	static FString BakedWieldPackage(const FString& Stem) { return BakedWieldDir() / Stem; }
	static FString BakedWieldMesh(const FString& Stem)
	{
		const FString Asset = TEXT("SK_") + Stem;
		return BakedWieldPackage(Stem) / Asset + TEXT(".") + Asset;
	}
	// The table `(classname, sex)` resolves through, typed by Public/ElysiumWieldTable.h. Its rows
	// carry soft references to the packages above, so a resolved row is followed rather than
	// rebuilt by name — BakedWieldMesh exists for the caller that has a stem and no row.
	static FString BakedWieldTable()
	{
		const FString Asset = TEXT("DA_WieldModels");
		return BakedItemsDir() / Asset + TEXT(".") + Asset;
	}

	static FString BakedCharacterDir() { return BakedMount() / TEXT("Characters"); }
	static FString BakedCharacterSkeletonPrefix() { return TEXT("SKEL_Elysium_"); }
	static FString BakedCharacterSkeleton(const FString& Stem)
	{
		const FString Asset = BakedCharacterSkeletonPrefix() + Stem;
		return BakedCharacterDir() / TEXT("Skeletons") / Asset + TEXT(".") + Asset;
	}
	// One body, whoever is wearing it. The player-material variant is not a separate asset: every
	// section is instanced from the one body master whose parameters the player path drives, so the
	// PC and an NPC differ in the ModelAlpha set on the component and in nothing on disk. The
	// argument stays because the runtime's own visual cache key still distinguishes the two.
	static FString BakedCharacterMesh(const FString& Stem, bool /*bPlayerMaterial*/ = false)
	{
		const FString Asset = TEXT("SK_") + Stem;
		return BakedCharacterDir() / TEXT("Meshes") / Asset + TEXT(".") + Asset;
	}
	// A body's Chaos cloth asset (`make_cloth_assets.py`), decoded from the user's VtMB install like
	// the skeleton and mesh above rather than authored, so it bakes to the same per-character mount.
	static FString BakedCharacterClothDir() { return BakedCharacterDir() / TEXT("Cloth"); }
	static FString BakedCharacterCloth(const FString& Stem)
	{
		const FString Asset = TEXT("CLOTH_") + Stem;
		return BakedCharacterClothDir() / Asset + TEXT(".") + Asset;
	}
	// Where a bank's clips live, apart from the bodies'. Banks are packaged once and reused by
	// compatible body skeletons.
	static FString BakedBankFolder()
	{
		return TEXT("_banks");
	}
	// One of a body's own clips. `Owner` is the body's stem — a body's non-bank clips are always
	// its own (the export asserts it), so the stem is the whole address.
	static FString BakedCharacterAnim(const FString& Owner, const FString& Clip)
	{
		const FString Asset = TEXT("A_") + BakedAssetName(Clip);
		return BakedCharacterDir() / TEXT("Anims") / Owner / Asset + TEXT(".") + Asset;
	}
	// One of a shared bank's clips, under `BakedBankFolder()`.
	static FString BakedBankAnim(const FString& Bank, const FString& Clip)
	{
		const FString Asset = TEXT("A_") + BakedAssetName(Clip);
		return BakedCharacterDir() / TEXT("Anims") / BakedBankFolder() / Bank / Asset
			+ TEXT(".") + Asset;
	}
	// One blend grid, as a UBlendSpace. It sits with the sequences it samples rather than in a
	// directory of its own — the `BS_` prefix disambiguates it from their `A_` the way `MI_`, `T_`
	// and `SK_` do elsewhere on the mount — because a grid's cells are always clips of the same
	// owner, so the two are written by the same pass and go stale together.
	//
	// `Host` names the clip a LAYER grid was composed with, and is empty for a grid that stands on
	// its own. A layer's cells are masked overlays whose pose only means anything accumulated onto
	// a particular host, so the bake writes one asset per declaring host and the label alone does
	// not identify one — asking for the bare label found nothing for 299 of the mount's 527 spaces.
	static FString BakedCharacterBlendSpace(const FString& Owner,
		const FString& Label, const FString& Host = FString())
	{
		const FString Asset = TEXT("BS_")
			+ BakedAssetName(Host.IsEmpty() ? Label : Label + TEXT("@") + Host);
		return BakedCharacterDir() / TEXT("Anims") / Owner / Asset + TEXT(".") + Asset;
	}
	static FString BakedBankBlendSpace(const FString& Bank,
		const FString& Label, const FString& Host = FString())
	{
		const FString Asset = TEXT("BS_")
			+ BakedAssetName(Host.IsEmpty() ? Label : Label + TEXT("@") + Host);
		return BakedCharacterDir() / TEXT("Anims") / BakedBankFolder() / Bank / Asset
			+ TEXT(".") + Asset;
	}
	// Every run of characters illegal in an Unreal object name folds to a single underscore. Model
	// stems are already safe, but 14 of the 2,494 shipped clip labels are not —
	// `claws_aggressive_walk#50`, `wolf_Form_attack[Bite]`, `Lacroix_Line1_col_E&F`. The runtime
	// and the bake must agree exactly or the runtime asks for a package the bake did not write;
	// folding introduces no collision on the shipped corpus, case-insensitively, within any owner.
	//
	// Its Python twin is `elysium_pipeline.asset_names.baked_asset_name`, and that module is where
	// the contract is stated. It is NOT `bake_lib.safe_name`, which additionally strips leading and
	// trailing underscores and substitutes a fallback for a name that folds away entirely: the two
	// disagree on the 4 labels ending in an illegal character. They never meet on one input — clip
	// and blend-space labels take this fold on both sides, texture and material names take the
	// other on both — so a caller crossing from one namespace to the other must pick deliberately.
	static FString BakedAssetName(const FString& Raw)
	{
		FString Out;
		Out.Reserve(Raw.Len());
		bool bInRun = false;
		for (const TCHAR Ch : Raw)
		{
			const bool bLegal = (Ch >= TEXT('A') && Ch <= TEXT('Z')) ||
				(Ch >= TEXT('a') && Ch <= TEXT('z')) ||
				(Ch >= TEXT('0') && Ch <= TEXT('9')) || Ch == TEXT('_');
			if (bLegal)
			{
				Out.AppendChar(Ch);
				bInRun = false;
			}
			else if (!bInRun)
			{
				// One underscore per maximal illegal run, leading and trailing runs included, which
				// is what re.sub(r'[^A-Za-z0-9_]+', '_', ...) does.
				Out.AppendChar(TEXT('_'));
				bInRun = true;
			}
		}
		return Out;
	}

	// The shared corpus on disk (pipeline/src/elysium_pipeline/exporters/UE_extract_corpus.py):
	// every decoded texture, every static model, and the two documents that describe them. One
	// decode per source identity, so nothing here is addressed by a map.
	static FString SharedDir() { return Root() / TEXT("shared"); }
	static FString SharedTexDir() { return SharedDir() / TEXT("tex"); }
	static FString SharedPropsDir() { return SharedDir() / TEXT("props"); }
	static FString SharedManifest() { return SharedDir() / TEXT("manifest.json"); }
	static FString SharedMaterials() { return SharedDir() / TEXT("materials.json"); }
	// A sky's six faces are ordinary corpus textures under `materials/skybox/<skyname><face>`, so
	// two maps that share a sky share one set. `ElysiumEnvironment::BuildSkyCubeFrom` appends its
	// own face suffixes to this prefix, exactly as it does for the labelled probe set.
	// Its Python twin is `shared_corpus.sky_face_prefix`.
	static FString SkyFacePrefix(const FString& SkyName)
	{
		return TEXT("skybox_") + SkyName.ToLower();
	}

	// `items/ground_models.json` names which `vdata/items` definition stands on which corpus mesh
	// stem, its triangle count, and every model the install did not carry. The meshes themselves
	// are the corpus's, addressed by `BakedItemMesh`.
	static FString ItemsDir() { return Root() / TEXT("items"); }
	static FString ItemGroundModels() { return ItemsDir() / TEXT("ground_models.json"); }
	// `items/wield_models.json`'s `models` table, and the Unreal-native `.eskm` beside it each row
	// names -- the wield-model analogue of `NpcSource`, checked against the baked `SK_<stem>` the
	// same way (`wield_corpus.wield_dir`/`manifest_path`).
	static FString WieldDir() { return ItemsDir() / TEXT("wield"); }
	static FString WieldManifest() { return ItemsDir() / TEXT("wield_models.json"); }
	static FString WieldSource(const FString& Stem) { return WieldDir() / (Stem + TEXT(".eskm")); }

	static FString MapDir(const FString& Map) { return Root() / Map; }
	// A map's own texture directory. It holds only `tex/cube/` now -- the env cubemaps VBSP baked
	// per map and per position. Every surface texture is the corpus's.
	static FString MapTexDir(const FString& Map) { return MapDir(Map) / TEXT("tex"); }
	static FString MapObj(const FString& Map) { return MapDir(Map) / (Map + TEXT(".obj")); }
	// The new export lane's readiness marker (docs/project/seam_migration.md R2.4; the ruling is
	// `docs/architecture/map-architecture.md` "The export-readiness gate"). An empty file a new-lane
	// producer writes only once every sidecar `Travel` depends on is complete on disk for this map --
	// `UElysiumMapSubsystem::HasTravelableExport` accepts this OR MapObj, so the gate never goes
	// without both while the lanes coexist. Nothing writes it yet; R3.2 is the first producer.
	static FString MapExportReady(const FString& Map) { return MapDir(Map) / (Map + TEXT(".ready")); }
	static FString MapSkyObj(const FString& Map) { return MapDir(Map) / (Map + TEXT("_sky.obj")); }
	static FString MapSpawn(const FString& Map) { return MapDir(Map) / (Map + TEXT(".spawn")); }
	static FString MapSky(const FString& Map) { return MapDir(Map) / (Map + TEXT(".sky")); }
	static FString MapEnv(const FString& Map) { return MapDir(Map) / (Map + TEXT(".env")); }
	static FString MapLights(const FString& Map) { return MapDir(Map) / (Map + TEXT(".lights")); }
	static FString MapProps(const FString& Map) { return MapDir(Map) / (Map + TEXT(".props")); }
	static FString MapEnts(const FString& Map) { return MapDir(Map) / (Map + TEXT(".ents")); }
	static FString MapHulls(const FString& Map) { return MapDir(Map) / (Map + TEXT(".hulls")); }
	static FString MapDispCol(const FString& Map) { return MapDir(Map) / (Map + TEXT(".dispcol")); }
	// Decals: one deferred-decal projector per line (material + centre + normal + s/t axes +
	// half-extents, Unreal cm), written by UE_bsp_to_scene.py. Materials ride the shared <map>.mtl.
	static FString MapDecals(const FString& Map) { return MapDir(Map) / (Map + TEXT(".decals")); }
	static FString MapRopes(const FString& Map) { return MapDir(Map) / (Map + TEXT(".ropes")); }

	// Audio. WAVs are game-global (shared across maps), so they live in one mirror of VtMB's
	// `sound/` tree, not per-map. Rel is the engine-relative path under sound/ (e.g.
	// "Environmental/Fire/Fire_Roaring.wav"), matching an ambient_generic `message` value.
	static FString SoundDir() { return Root() / TEXT("sound"); }
	static FString SoundFile(const FString& Rel) { return SoundDir() / Rel; }

	// Choreographed scenes and their phoneme sidecars, mirrored verbatim from the install by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_scenes.py. Both trees mirror VtMB's `sound/` layout with that prefix already
	// stripped, so a `logic_choreographed_scene`'s SceneFile ("sound/Character/dlg/.../x.vcd") reads
	// back as scenes/Character/dlg/.../x.vcd. Rel is that stripped path — run a raw keyvalue through
	// ElysiumScene::NormalizeSceneRel first, which also folds separators and case.
	static FString ScenesDir() { return Root() / TEXT("scenes"); }
	static FString SceneFile(const FString& Rel) { return ScenesDir() / Rel; }
	static FString LipDir() { return Root() / TEXT("lip"); }
	static FString LipFile(const FString& Rel) { return LipDir() / Rel; }

	// Faceposer's flex-controller weight tables, mirrored flat from the install's own
	// `expressions/` directory by the same exporter. The shipped `.vfe` is the compiled twin of the
	// readable `.txt`, so only the `.txt` is mirrored and only it is read. Leaf is the file name with
	// its extension — a scene's `expression` event names the stem in `param`, and lipsync names
	// `<model stem>_phonemes`.
	static FString ExpressionsDir() { return Root() / TEXT("expressions"); }
	static FString ExpressionFile(const FString& Leaf) { return ExpressionsDir() / Leaf; }

	// Scripting. VtMB's level scripts + dialogue are game-global loose plain-text, mirrored under
	// the export root's scripts/ and dlg/ directories by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_scripts.py. A
	// worldspawn `levelscript` value (e.g. "tutorial") names the hub module, which lives at
	// scripts/<module>/<module>.py — imported into the embedded CPython VM at map load.
	static FString ScriptsDir() { return Root() / TEXT("scripts"); }
	static FString DlgDir() { return Root() / TEXT("dlg"); }
	static FString ScriptModuleFile(const FString& Module) { return ScriptsDir() / Module / (Module + TEXT(".py")); }
	// Console config. VtMB's `cfg/*.cfg` alias + cvar tables (Valve console syntax),
	// mirrored under the export root's cfg/ directory by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_cfg.py. The runtime console bridge
	// (FElysiumConsole) seeds its alias/cvar store from these. `user.cfg` records the source install's
	// personal Basic/Plus choice, but the runtime replaces only `patchtype` with Elysium's Plus
	// selector after parsing. VtMB's file-touching scripts reach the same tree through the script
	// filesystem's `cfg/` mount (FElysiumScriptFS), which is what makes `FixKeyBindings` resolve.
	static FString CfgDir() { return Root() / TEXT("cfg"); }
	static FString CfgFile(const FString& File) { return CfgDir() / File; }

	// VtMB's whole RPG/rules layer is Valve-KeyValues text under `vdata/`, now sourced from the
	// export_v2 capsule import (`uv run elysium import vdata`) onto CorpusRoot() rather than from
	// Root(). Per-table consumer map: `docs/vtmb/vdata-catalog.md`. Signs stay on the legacy
	// export root (SignsDir() below) -- a deliberate divergence, not yet migrated. See
	// docs/project/seam_migration.md "Settled": rulebook tables read corpus-only, terminal
	// definitions stay overlay-first over this same directory.
	static FString VdataDir() { return CorpusRoot() / TEXT("vdata"); }
	static FString VdataFile(const FString& Rel) { return VdataDir() / Rel; }

	// The script filesystem's writable overlay (FElysiumScriptFS). VtMB's scripts write as well as
	// read — `haven_pc.txt` takes the PC's name, the Unofficial Patch's hunter mode copies `- hunter`
	// asset variants over the shipped ones — and every one of those writes lands here instead of in
	// Root(), which is game-derived pipeline output a re-export regenerates. It doubles as the VM's
	// virtual install root: a path that ever escaped the shim would land inside the sandbox rather
	// than in the project tree. Under Saved/ because it is per-user mutable state, not content.
	static FString ScriptFsRoot() { return FPaths::ProjectSavedDir() / TEXT("Elysium/ScriptFS"); }
	// An NPC's `dialogname` keyfield already carries the `dlg/` prefix ("dlg/Main Characters/
	// jack_tutorial.dlg"), so it resolves straight under the content root. Case differs from the
	// lowercased on-disk mirror, but the Windows target's file system is case-insensitive.
	static FString DlgFromDialogname(const FString& DialogName) { return Root() / DialogName; }

	// Signs. VtMB's sign+popup panels are game-global `SignData` KeyValues files,
	// mirrored flat and lowercased under the export root's signs/ directory by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_signs.py (a `definition_file`
	// keyvalue's `vdata/Signs/` prefix and authored case are dropped). Their `BackgroundImage`
	// materials are the imported `T_` assets (`UI/ElysiumUiArt.h`, R6.6).
	static FString SignsDir() { return Root() / TEXT("signs"); }
	static FString SignFile(const FString& Leaf) { return SignsDir() / Leaf; }

	// UI source (pipeline/src/elysium_pipeline/exporters/UE_extract_ui.py). The `.res` layouts and both schemes are
	// mirrored as **design intent** and are not executed as layout; what the runtime actually reads
	// is the authored string table (menu labels are `VMainMenu_BTN_*` tokens — docs/vtmb/vtmb-ui.md §2).
	// Every picture the UI draws is an imported `T_` asset (`UI/ElysiumUiArt.h`, R6.6); no art is
	// read off this tree. Game-derived, so gitignored and regenerable like every other exported mirror.
	static FString UiDir() { return Root() / TEXT("ui"); }
	static FString UiStrings() { return UiDir() / TEXT("strings.json"); }

	// Fonts. The sign/popup panel's typeface set — hand-authored/game-agnostic OFL faces committed
	// under Content/Fonts (not the game-derived export root), mapping VtMB's authored face names
	// (ParagraphText/Newsprint/Headline/...) onto vector type. Read verbatim off disk at draw time.
	static FString FontsDir() { return FPaths::ProjectContentDir() / TEXT("Fonts"); }
	static FString FontFile(const FString& File) { return FontsDir() / File; }

	// NPCs. Skeletal characters export under the export root's npc/ directory; the game plays
	// only baked assets off the mount, and the .eskm container is what the character bake reads.
	// Stem is the model name, e.g. "gangmember_male_2".
	static FString NpcDir() { return Root() / TEXT("npc"); }
	// The Unreal-native skeletal container the character bake reads (`UE_mdl_skeletal.py`). It
	// needs no import transform, so it is also the reference a baked asset is checked against:
	// whatever it says a bone's bind pose is, is what the bake had to write.
	static FString NpcSource(const FString& Stem) { return NpcDir() / (Stem + TEXT(".eskm")); }
	static FString NpcBankSource(const FString& Stem)
	{
		return NpcDir() / TEXT("banks") / (Stem + TEXT(".eskm"));
	}

	// The shared animation banks and their resolution sidecars. A VtMB NPC's own .mdl
	// carries only its own clips — mostly dialogue — and pulls idle/locomotion/combat from banks
	// through the studiohdr include DAG, so a bank is a skeleton + clips with no mesh, applied
	// to any NPC through skeleton compatibility. `npc_index.json` names every character and bank
	// with counts (~47 KB, read once); `clips/<stem>.json` is one character's whole resolved
	// vocabulary (~95 KB), read only for the stems a map actually places — the full
	// npc_manifest.json is 15.8 MB and exists for the offline probes. The index also carries the
	// 56 player bodies, which no map references and are resolved by clan through
	// `clandoc000.txt`.
	static FString NpcIndex() { return NpcDir() / TEXT("npc_index.json"); }
	static FString NpcClips(const FString& Stem) { return NpcDir() / TEXT("clips") / (Stem + TEXT(".json")); }

	// The facial flex rig beside a rigged NPC's glb: the FACS flexdesc names, the 44
	// flex controllers, the 60 RPN flex rules, the amplitude jaw and the per-morph target ramps,
	// index-aligned with the glb's morph targets. RelPath is `npc_index.json`'s own
	// `npcs[stem].facial` ("facial/<stem>.json"); a model with no flex rig names none.
	static FString NpcFacial(const FString& RelPath) { return NpcDir() / RelPath; }

	// The procedural bone rule table beside a driven model's glb: per driven bone, its
	// control bone, the axis as a converted direction, and the six-entry pos/quat table the runtime
	// blends. RelPath is `npc_index.json`'s own `procedural` value — "procedural/<stem>.json", or
	// "animated_props/procedural/<stem>.json" for a skeletal prop. A model with no `ProcType == 1`
	// bone names none; 130 of the 185 exported models carry one.
	static FString NpcProcedural(const FString& RelPath) { return NpcDir() / RelPath; }

	// The blend spaces a model's multi-cell sequences declare. A VtMB sequence can name a
	// grid of animations rather than one — a 9x1 `move_yaw` locomotion fan, a 3x3 weapon-aim layer —
	// and the exporter bakes every cell as its own clip beside a sidecar naming the axes, the pose
	// parameter driving each, and which clip sits in each cell. Without it a grid label resolves to
	// the base cell, which on a symmetric yaw fan is the -180 degree extreme: `walk` plays backwards.
	// RelPath is `npc_index.json`'s own `blends` value — "blends/<stem>.json", or
	// "animated_props/blends/<stem>.json" for a skeletal prop. A model whose every sequence is a
	// single cell names none, which is most of them.
	static FString NpcBlends(const FString& RelPath) { return NpcDir() / RelPath; }

	// VtMB's authored renderer-cloth payload for one character, decoded offline into a simulation
	// mesh: particles, constraints, collision primitives and the per-render-vertex substitution
	// maps (`docs/vtmb/secondary_motion.md`). Written only for the 60 installed models whose
	// `MDLHeader.Flags` carries 0x400, so a miss is the ordinary case rather than a fault.
	//
	// The runtime does not read this. It is the generator's input — `make_cloth_assets.py` turns it
	// into a `UChaosClothAsset` under /ElysiumBaked/Characters/Cloth, and the game loads that. The
	// path is here so a debug surface can report whether a body's garment was ever exported, which
	// is what separates "this character has no cloth" from "the export did not run".
	static FString NpcGarmentDir() { return NpcDir() / TEXT("garment"); }
	static FString NpcGarment(const FString& Stem) { return NpcGarmentDir() / (Stem + TEXT(".json")); }

	// The eyeball pair beside a character's glb: the eye's bone and resting basis, the iris
	// scale and texture, and the eyelid flexdescs the renderer's eye pass writes back into the flex
	// weights. RelPath is `npc_index.json`'s own `npcs[stem].eyes` value ("eyes/<stem>.json").
	// Named separately from the flex rig because a player body carries eyeballs and no flex rig.
	static FString NpcEyes(const FString& RelPath) { return NpcDir() / RelPath; }

	// Light-probe output (debug, `ElysiumLightProbe.cpp`). R4.3 retired the Lights Cog window's
	// hand-survey JSON that used to share this directory (`UElysiumLightCalibration` replaces it as
	// cooked content); the probe's own `<map>.probe.json` still lands here.
	static FString LightEditsDir() { return Root() / TEXT("_lights"); }
};
