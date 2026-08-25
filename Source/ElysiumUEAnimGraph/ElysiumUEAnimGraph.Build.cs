using UnrealBuildTool;

// The editor half of this project's one authored animation node, and nothing else.
//
// `UAnimGraphNode_Base` lives in `AnimGraph`, which declares
// `[SupportedTargetTypes(TargetType.Editor, TargetType.Program)]` -- it cannot link into a Game
// target under any preprocessor guard, and UHT does not honour `#if WITH_EDITOR` around a
// `UCLASS` (it emits the registration unguarded while the declaration is preprocessed away). So
// the editor-facing node needs a module of its own, while the node it wraps --
// `FAnimNode_ElysiumPostAdditive` -- stays in the runtime module where the compiled Animation
// Blueprint class needs it.
//
// The generator that places the node does NOT depend on this module: it resolves node classes by
// path through `FindObject<UClass>`, the same way it already reaches
// `/Script/BlendStackEditor.AnimGraphNode_BlendStack`. What matters is that the module is
// LOADED wherever the UNCOOKED Animation Blueprint is opened -- the editor, the commandlets, and
// every `-game` launch of the editor binary, which regenerates the Blueprint on load and drops a
// node whose class it cannot resolve with no diagnostic. That is why the module is declared
// `UncookedOnly` rather than `Editor`, exactly as `BlendStackEditor` is: an `Editor` module does
// not load under `-game`, and the body then poses the reference pose for the whole run.
public class ElysiumUEAnimGraph : ModuleRules
{
	public ElysiumUEAnimGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// AnimGraph carries UAnimGraphNode_Base; BlueprintGraph carries UK2Node, which it
			// derives from, and whose virtuals the generated registration emits calls to, so it
			// has to be linked by name rather than reached transitively. UnrealEd carries the
			// editor types the node's interface names.
			"AnimGraph", "BlueprintGraph", "UnrealEd",
			// The runtime node this wraps.
			"ElysiumUE"
		});
	}
}
