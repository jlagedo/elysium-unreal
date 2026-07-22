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
			// Dev console UI is built directly in Slate.
			"Slate", "SlateCore"
		});
	}
}
