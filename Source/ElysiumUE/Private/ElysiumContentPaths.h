#pragma once

#include "CoreMinimal.h"
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

	// --- Baked content (pipeline/unreal/bake_map.py) ---------------------------------------------
	// The look of a map — world + sky geometry, materials, textures, props, lights, fog — is
	// offline-baked into real .uasset content under the /ElysiumBaked plugin mount, and the map
	// IS a real .umap the engine opens. These are package paths (a virtual content root), not
	// filesystem paths, so they take no FPaths::ProjectDir. The mount's Content/ is game-derived
	// and gitignored exactly like Root(); only the .uplugin descriptor is committed.
	static FString BakedMount() { return TEXT("/ElysiumBaked"); }
	static FString BakedMapDir(const FString& Map) { return BakedMount() / Map; }
	// The .umap UElysiumMapSubsystem::Travel opens for this map.
	static FString BakedLevel(const FString& Map) { return BakedMapDir(Map) / Map; }
	// One baked prop model, by the same OBJ stem the .props sidecar and `model_mesh` name. The
	// exporter already emits safe stems, so the bake's own safe_name() is a no-op on them and the
	// stem maps to the asset name verbatim. Package path is <dir>/SM_<stem>.SM_<stem>.
	static FString BakedPropMesh(const FString& Map, const FString& Stem)
	{
		const FString Asset = TEXT("SM_") + Stem;
		return BakedMapDir(Map) / TEXT("Props") / Asset + TEXT(".") + Asset;
	}
	static FString BakedBrushMesh(const FString& Map, const FString& Stem)
	{
		const FString Asset = TEXT("SM_") + Stem;
		return BakedMapDir(Map) / TEXT("Brushes") / Asset + TEXT(".") + Asset;
	}
	// The map's prop skin table (UElysiumPropSkinSet) -- every alternate skin family of every
	// prop model it places, as material instances resolved at bake time. Absent for a map whose
	// models all carry a single family.
	static FString BakedPropSkins(const FString& Map)
	{
		const FString Asset = TEXT("DA_") + Map + TEXT("_PropSkins");
		return BakedMapDir(Map) / TEXT("Props") / Asset + TEXT(".") + Asset;
	}

	static FString MapDir(const FString& Map) { return Root() / Map; }
	static FString MapTexDir(const FString& Map) { return MapDir(Map) / TEXT("tex"); }
	// The offline enhancement track's parallel texture set (docs/architecture/asset-enhancement.md): the
	// super-resolved siblings of `tex/`, written by pipeline/src/elysium_pipeline/enhancement/sky_upscale.py and its successors.
	// Optional and per-map; `elysium.EnhancedTextures` selects between the two, faithful by
	// default. Absent for most maps, which is why every reader tests before preferring it.
	static FString MapTexHiDir(const FString& Map) { return MapDir(Map) / TEXT("tex_hi"); }
	static FString MapObj(const FString& Map) { return MapDir(Map) / (Map + TEXT(".obj")); }
	static FString MapSkyObj(const FString& Map) { return MapDir(Map) / (Map + TEXT("_sky.obj")); }
	static FString MapSpawn(const FString& Map) { return MapDir(Map) / (Map + TEXT(".spawn")); }
	static FString MapSky(const FString& Map) { return MapDir(Map) / (Map + TEXT(".sky")); }
	static FString MapEnv(const FString& Map) { return MapDir(Map) / (Map + TEXT(".env")); }
	static FString MapLights(const FString& Map) { return MapDir(Map) / (Map + TEXT(".lights")); }
	static FString MapProps(const FString& Map) { return MapDir(Map) / (Map + TEXT(".props")); }
	static FString MapPropsDir(const FString& Map) { return MapDir(Map) / TEXT("props"); }
	static FString MapEnts(const FString& Map) { return MapDir(Map) / (Map + TEXT(".ents")); }
	static FString MapHulls(const FString& Map) { return MapDir(Map) / (Map + TEXT(".hulls")); }
	static FString MapDispCol(const FString& Map) { return MapDir(Map) / (Map + TEXT(".dispcol")); }
	// Decals (7.2): one deferred-decal projector per line (material + centre + normal + s/t axes +
	// half-extents, Unreal cm), written by UE_bsp_to_scene.py. Materials ride the shared <map>.mtl.
	static FString MapDecals(const FString& Map) { return MapDir(Map) / (Map + TEXT(".decals")); }
	static FString MapRopes(const FString& Map) { return MapDir(Map) / (Map + TEXT(".ropes")); }

	// Audio (P6). WAVs are game-global (shared across maps), so they live in one mirror of
	// VtMB's `sound/` tree, not per-map. Rel is the engine-relative path under sound/ (e.g.
	// "Environmental/Fire/Fire_Roaring.wav"), matching an ambient_generic `message` value.
	static FString SoundDir() { return Root() / TEXT("sound"); }
	static FString SoundFile(const FString& Rel) { return SoundDir() / Rel; }

	// Choreographed scenes and their phoneme sidecars (PL9), mirrored verbatim from the install by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_scenes.py. Both trees mirror VtMB's `sound/` layout with that prefix already
	// stripped, so a `logic_choreographed_scene`'s SceneFile ("sound/Character/dlg/.../x.vcd") reads
	// back as scenes/Character/dlg/.../x.vcd. Rel is that stripped path — run a raw keyvalue through
	// ElysiumScene::NormalizeSceneRel first, which also folds separators and case.
	static FString ScenesDir() { return Root() / TEXT("scenes"); }
	static FString SceneFile(const FString& Rel) { return ScenesDir() / Rel; }
	static FString LipDir() { return Root() / TEXT("lip"); }
	static FString LipFile(const FString& Rel) { return LipDir() / Rel; }

	// Scripting (P5). VtMB's level scripts + dialogue are game-global loose plain-text,
	// mirrored under the export root's scripts/ and dlg/ directories by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_scripts.py. A
	// worldspawn `levelscript` value (e.g. "tutorial") names the hub module, which lives at
	// scripts/<module>/<module>.py — imported into the embedded CPython VM at map load (9.3a).
	static FString ScriptsDir() { return Root() / TEXT("scripts"); }
	static FString DlgDir() { return Root() / TEXT("dlg"); }
	static FString ScriptModuleFile(const FString& Module) { return ScriptsDir() / Module / (Module + TEXT(".py")); }
	// Console config (PL5d / 9.3b). VtMB's `cfg/*.cfg` alias + cvar tables (Valve console syntax),
	// mirrored under the export root's cfg/ directory by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_cfg.py. The runtime console bridge
	// (FElysiumConsole) seeds its alias/cvar store from these; `user.cfg` carries the Basic/Plus
	// `patchtype` alias. VtMB's file-touching scripts reach the same tree through the script
	// filesystem's `cfg/` mount (FElysiumScriptFS), which is what makes `FixKeyBindings` resolve.
	static FString CfgDir() { return Root() / TEXT("cfg"); }
	static FString CfgFile(const FString& File) { return CfgDir() / File; }

	// VtMB's whole RPG/rules layer is Valve-KeyValues text under `vdata/`, mirrored verbatim by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_vdata.py (PL5b). Per-table consumer map: `docs/vtmb/vdata-catalog.md`.
	static FString VdataDir() { return Root() / TEXT("vdata"); }
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

	// Signs (P4.10 / PL5c). VtMB's sign+popup panels are game-global `SignData` KeyValues files,
	// mirrored flat and lowercased under the export root's signs/ directory by
	// pipeline/src/elysium_pipeline/exporters/UE_extract_signs.py (a `definition_file`
	// keyvalue's `vdata/Signs/` prefix and authored case are dropped). Their `BackgroundImage`
	// materials decode to signs/tex/, keyed by signs/backgrounds.json.
	static FString SignsDir() { return Root() / TEXT("signs"); }
	static FString SignFile(const FString& Leaf) { return SignsDir() / Leaf; }
	static FString SignTexDir() { return SignsDir() / TEXT("tex"); }
	static FString SignBackgrounds() { return SignsDir() / TEXT("backgrounds.json"); }

	// UI source (roadmap PL8, pipeline/src/elysium_pipeline/exporters/UE_extract_ui.py). The `.res` layouts and both schemes are
	// mirrored as **design intent** and are not executed as layout; what the runtime actually reads
	// is the authored string table (menu labels are `VMainMenu_BTN_*` tokens — docs/vtmb/vtmb-ui.md §2)
	// and the decoded art (the title lockup, HUD frames, clan icons). Game-derived, so gitignored
	// and regenerable like every other exported mirror.
	static FString UiDir() { return Root() / TEXT("ui"); }
	static FString UiStrings() { return UiDir() / TEXT("strings.json"); }
	static FString UiMenuDir() { return UiDir() / TEXT("menu"); }
	static FString UiTitle() { return UiMenuDir() / TEXT("title.png"); }
	// The menu particle scene's sprite sheet, decoded to PNG: the blood cels, the glow, and the 15
	// `mm_<clan>` sect/clan sigils VtMB drifts across its own menu backdrop. The menu draws one of
	// them as its seal (`mm_cam` in the front end, the PC's clan in a session), so the emitter graph
	// is not reproduced but its art is. Stem is the sprite name without extension.
	static FString UiMenuSprite(const FString& Stem) { return UiMenuDir() / TEXT("sprites") / (Stem + TEXT(".png")); }
	static FString UiArt(const FString& Rel) { return UiDir() / TEXT("art") / Rel; }

	// Fonts. The sign/popup panel's typeface set — hand-authored/game-agnostic OFL faces committed
	// under Content/Fonts (not the game-derived export root), mapping VtMB's authored face names
	// (ParagraphText/Newsprint/Headline/...) onto vector type. Read verbatim off disk at draw time.
	static FString FontsDir() { return FPaths::ProjectContentDir() / TEXT("Fonts"); }
	static FString FontFile(const FString& File) { return FontsDir() / File; }

	// NPCs (P8 8.2). Skeletal characters export as one standard glTF 2.0 file per model (mesh +
	// StudioBone skeleton + one animation) under the export root's npc/ directory, written by
	// pipeline/src/elysium_pipeline/formats/mdl_gltf.py. The runtime
	// loads them through glTFRuntime (glTF is self-describing, so no UE_-style pre-conversion — the
	// plugin does the glTF->UE basis/scale change). Stem is the model name, e.g. "gangmember_male_2".
	static FString NpcDir() { return Root() / TEXT("npc"); }
	static FString NpcGlb(const FString& Stem) { return NpcDir() / (Stem + TEXT(".glb")); }

	// The shared animation banks and their resolution sidecars (8.5). A VtMB NPC's own .mdl
	// carries only its own clips — mostly dialogue — and pulls idle/locomotion/combat from banks
	// through the studiohdr include DAG, so a bank glb is a skeleton + clips with no mesh, applied
	// to any NPC by bone name. `npc_index.json` names every character and bank with its glb and
	// counts (~47 KB, read once); `clips/<stem>.json` is one character's whole resolved vocabulary
	// (~95 KB), read only for the stems a map actually places — the full npc_manifest.json is
	// 15.8 MB and exists for the offline probes. The index also carries the 56 player bodies
	// (PL13), which no map references and 8.11a resolves by clan through `clandoc000.txt`.
	// NpcBankGlb takes the index's own relative path ("banks/x.glb").
	static FString NpcIndex() { return NpcDir() / TEXT("npc_index.json"); }
	static FString NpcClips(const FString& Stem) { return NpcDir() / TEXT("clips") / (Stem + TEXT(".json")); }
	static FString NpcBankGlb(const FString& RelGlb) { return NpcDir() / RelGlb; }
	static FString AnimatedPropGlb(const FString& RelGlb) { return NpcDir() / RelGlb; }

	// The labelled sky set (debug, sky-ambience RE-A2/B1). Six self-describing face images —
	// suffix, predicted axis, TOP banner, up arrow, tagged corners, edge neighbours — authored by
	// research/tooling/probes/sky_probe.py, which also installs them into the *original* game so the two ends of the
	// orientation chain are checked against one set of faces. Named `<skyname><face>.png`, unlike
	// the per-map `tex/sky_<face>.png`. `elysium.SkyProbe 1` builds the cube from these.
	static FString SkyProbeDir() { return Root() / TEXT("_skyprobe"); }

	// Light-edit sessions (debug). The Lights Cog window's Save writes one JSON per map — the
	// hand-disabled set plus every hand-set attribute, keyed by `.lights` line index — so a survey
	// done by eye in-game comes back out as data. One file per map, overwritten each save; under
	// Root(), so it is game-derived and gitignored like the rest.
	static FString LightEditsDir() { return Root() / TEXT("_lights"); }
	static FString LightEdits(const FString& Map) { return LightEditsDir() / (Map + TEXT(".json")); }
};
