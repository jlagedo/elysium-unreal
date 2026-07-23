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
			"InputCore", "EnhancedInput",
			// Runtime asset loading: build meshes in code (no editor bake) and
			// decode textures from disk into transient UTexture2D.
			"ProceduralMeshComponent", "ImageWrapper", "ImageCore", "RenderCore", "RHI",
			// Static props: build UStaticMesh at runtime from mesh descriptions
			// (BuildFromMeshDescriptions) with manual convex collision for solid props.
			"MeshDescription", "StaticMeshDescription", "PhysicsCore",
			// P8 NPCs: glTFRuntime loads USkeletalMesh + UAnimSequence from the .glb NPC exports
			// (out/npc, standard glTF 2.0) at runtime -- no editor import. Vendored under
			// Plugins/glTFRuntime; a runtime module, so it stays in every config.
			"glTFRuntime",
			// The `.ents` entity sidecar is one JSON blob (unlike the line-based sidecars).
			"Json",
			// Dev console UI is built directly in Slate.
			"Slate", "SlateCore"
		});

		// P6 audio: vendored single-header decoders under Private/ThirdParty (public domain) --
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
