using UnrealBuildTool;

public class ElysiumUE : ModuleRules
{
	public ElysiumUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			// Runtime asset loading: build meshes in code (no editor bake) and
			// decode textures from disk into transient UTexture2D.
			"ProceduralMeshComponent", "ImageWrapper", "ImageCore", "RenderCore", "RHI",
			// Dev console UI is built directly in Slate.
			"Slate", "SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });
	}
}
