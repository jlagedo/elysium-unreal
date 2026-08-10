using System.IO;
using UnrealBuildTool;

public class ElysiumUE : ModuleRules
{
	public ElysiumUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Enforce Include-What-You-Use: headers pull only what they use, .cpp includes
		// its own header first. Keeps rebuild times sane as the module count grows.
		IWYUSupport = IWYUSupport.Full;

		// Public API surface. Every Public/ header only inherits from and forward-declares
		// Engine framework types, so nothing beyond these three belongs on the public deps.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine"
		});

		// Used only inside Private/*.cpp — kept off the public API so downstream modules
		// don't transitively inherit them.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore", "EnhancedInput", "GameInputBase",
			// Native NPC locomotion: Recast owns the runtime walkable graph, while the
			// engine AI/path-following stack moves character capsules over it.
			"AIModule", "NavigationSystem",
			"Niagara",
			"AudioMixer", "AudioModulation", "AudioExtensions",
			// Runtime asset loading: build meshes in code (no editor bake) and
			// decode textures from disk into transient UTexture2D.
			"ProceduralMeshComponent", "ImageWrapper", "ImageCore", "RenderCore", "RHI",
			// Generated garments: a character wears a UChaosClothAsset through a stock
			// UChaosClothComponent following its body as leader pose. Runtime, unlike the
			// builder beside it — the asset is authored offline and only ever loaded here.
			"ChaosClothAssetEngine",
			// Static props: build UStaticMesh at runtime from mesh descriptions
			// (BuildFromMeshDescriptions) with manual convex collision for solid props.
			"MeshDescription", "StaticMeshDescription", "PhysicsCore",
			// The character bake authors skeletal assets through the engine's own mesh-description
			// path -- geometry, skin weights and morph deltas -- which is what every shipped
			// importer writes into. SkeletalMeshDescription carries FSkeletalMeshAttributes;
			// AnimationCore carries the bone-weight types it stores.
			"SkeletalMeshDescription", "AnimationCore",
			// P8 NPCs: glTFRuntime loads USkeletalMesh + UAnimSequence from the .glb NPC exports
			// (out/npc, standard glTF 2.0) at runtime -- no editor import. Vendored under
			// Plugins/External/glTFRuntime; a runtime module, so it stays in every config.
			"glTFRuntime",
			// CAP7.2: VtMB's two composition stages are FAnimNode_SkeletalControlBase nodes, which
			// live in AnimGraphRuntime. A runtime module -- the nodes are native and driven from
			// UElysiumNpcAnimInstance's proxy, so none of the editor AnimGraph stack is involved.
			"AnimGraphRuntime",
			// 8.7 ropes: the stock (enabled-by-default) CableComponent plugin's UCableComponent
			// renders each overhead cable as a Verlet-simulated strand built at map load.
			"CableComponent",
			// The `.ents` entity sidecar is one JSON blob (unlike the line-based sidecars).
			"Json",
			// Dev console UI is built directly in Slate.
			"Slate", "SlateCore",
			// 8.6 the UI foundation. CommonUI is the engine-native game-UI stack: the
			// activatable-widget stack, input routing, focus and gamepad navigation that
			// roadmap 8.10 would otherwise hand-roll. The widget
			// *visual trees* are still built in C++ Slate inside UCommonActivatableWidget
			// subclasses. Back/Accept defaults come from the native CommonUIInputData class,
			// so the source-authored foundation requires no Widget Blueprint or data assets.
			"UMG", "CommonUI", "CommonInput",
			// 11.3 the loading screen. The engine's own movie player is the only thing that can
			// draw while the game thread is blocked inside LoadMap. It resolves to
			// FNullGameMoviePlayer in the editor and under -nullrhi, so the hook is an automatic
			// no-op in PIE and in the headless test tiers.
			"MoviePlayer"
		});

		// P6 audio: Audio Mixer/Modulation own semantic routing and user control buses. Loose
		// VtMB media remains procedural and uses the vendored single-header decoders below --
		// dr_wav (6.1, MS-ADPCM/IMA/PCM) and dr_mp3 (6.2, dialogue/music/radio MP3). Only the
		// include path is added -- USoundWave/USoundWaveProcedural and PlaySound2D/SpawnSound2D
		// all live in Engine (already a public dep), so no audio module dependency is needed.
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private", "ThirdParty"));

		// Cog debug UI (ImGui). CogCommon carries the interfaces that survive a Shipping
		// build; the rest of Cog is stripped from Shipping (ENABLE_COG = !UE_BUILD_SHIPPING).
		PublicDependencyModuleNames.Add("CogCommon");
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"Cog", "CogDebug", "CogEngine", "CogImgui"
			});
		}

		// P2.7 -- the agent-facing MCP surface (debug-tooling.md Layer 3). The engine's
		// experimental ModelContextProtocol plugin is NoRedist and its toolset->MCP adapter is
		// editor-only, so the .uproject pins it to the Editor target; this dep follows that pin.
		// ELYSIUM_WITH_MCP gates every call site, so the module still compiles for a Game/Shipping
		// target with no MCP plugin present.
		if (Target.Type == TargetType.Editor)
		{
			PublicDefinitions.Add("ELYSIUM_WITH_MCP=1");
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				// JsonUtilities carries FJsonObjectWrapper, the base of FModelContextProtocolToolResult.
				"ModelContextProtocol", "ModelContextProtocolEngine", "JsonUtilities",
				// Offline weather-content generation authors the one native Niagara system
				// through UE's editor stack API. No NiagaraEditor code reaches Game/Shipping.
				"NiagaraEditor",
				// The character bake (UElysiumCharacterBakeLibrary) registers each asset it writes
				// so the commandlet's own does-asset-exist checks and the verifier see it without a
				// rescan.
				"AssetRegistry",
				// CCC5: the player animation graph is generated from tracked T3D text through
				// FEdGraphUtilities -- the engine's own clipboard paste path -- plus the blueprint
				// create/compile entry points beside it. Editor-only by construction: a graph is
				// authored once and cooked into a generated class, and nothing reads UnrealEd at
				// runtime.
				"UnrealEd",
				// UElysiumClothBuildLibrary turns VtMB's authored garment payload into a
				// UChaosClothAsset. `Chaos` carries FManagedArrayCollection, which the cloth
				// collection IS; the two ChaosClothAsset modules carry the facades that write it
				// and the asset that consumes it; PhysicsCore carries the body setups the
				// authored capsules and spheres become; `ChaosCloth` carries UChaosClothConfig and
				// FClothingSimulationConfig, which is how a complete solver property set is
				// produced rather than hand-written. Editor-only: an asset is generated once
				// and the running game only ever loads the result.
				"Chaos", "ChaosCloth", "ChaosClothAsset", "PhysicsCore"
			});
			// Lumen card baking (docs/architecture/uasset-bake-spike.md). IMeshUtilities::GenerateCardRepresentationData
			// is the real surfel-fitted card builder; it ray-traces the mesh through Embree, so it only
			// exists in the editor. The bake runs here and writes a sidecar the runtime deserializes,
			// which is what keeps Embree out of the shipping build.
			PublicDefinitions.Add("ELYSIUM_WITH_CARDGEN=1");
			PrivateDependencyModuleNames.Add("MeshUtilities");
		}
		else
		{
			PublicDefinitions.Add("ELYSIUM_WITH_MCP=0");
			PublicDefinitions.Add("ELYSIUM_WITH_CARDGEN=0");
		}

		// P5.5 / 9.3 -- embedded CPython 2.7.18 (qnox/python-2.7) for VtMB level scripts.
		// VtMB's VM is stock CPython 2.1 (vampire_python21.dll); the 2.1->2.7 script delta is ~0
		// (no string-exceptions, no __future__ -- verified against all 36 loose scripts). We link
		// the vendored release DLL by its import lib + point PythonHome at the vendored stdlib at
		// runtime (ElysiumPythonVM). Win64 only; elsewhere ELYSIUM_WITH_CPYTHON=0 keeps the null/expr
		// script hosts.
		string PyRoot = Path.Combine(ModuleDirectory, "ThirdParty", "CPython27");
		string PyImportLib = Path.Combine(PyRoot, "libs", "python27.lib");
		string PyDll = Path.Combine(PyRoot, "bin", "python27.dll");
		if (Target.Platform == UnrealTargetPlatform.Win64 && File.Exists(PyImportLib) && File.Exists(PyDll))
		{
			PublicDefinitions.Add("ELYSIUM_WITH_CPYTHON=1");
			PublicIncludePaths.Add(Path.Combine(PyRoot, "include"));
			PublicAdditionalLibraries.Add(PyImportLib);
			// python27.dll exports its sentinels/type objects/flags as dllimport DATA symbols
			// (Py_None, PyType_Type, PyExc_*, Py_NoSiteFlag, ...) which /DELAYLOAD cannot thunk, so
			// we link normally and stage the DLL next to the module binary. UE loads game modules
			// with LOAD_WITH_ALTERED_SEARCH_PATH, so the co-located DLL resolves at module load.
			RuntimeDependencies.Add("$(BinaryOutputDir)/python27.dll", PyDll);
			// Ship the 2.7 stdlib tree so Py_Initialize can bootstrap (os/string/...) in a packaged
			// build; in-editor the VM reads it in place from this ThirdParty folder.
			RuntimeDependencies.Add(Path.Combine(PyRoot, "PythonHome", "Lib", "..."), StagedFileType.NonUFS);
		}
		else
		{
			PublicDefinitions.Add("ELYSIUM_WITH_CPYTHON=0");
		}
	}
}
