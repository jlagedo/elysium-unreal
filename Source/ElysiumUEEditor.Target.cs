using UnrealBuildTool;
using System.Collections.Generic;

public class ElysiumUEEditorTarget : TargetRules
{
	public ElysiumUEEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ElysiumUE");

		// Plain unity, no adaptive working set (spec 0002 T6, measured 2026-10-03). Adaptive unity
		// takes every file `git status` lists out of its blob and compiles it alone, and puts it back
		// when it leaves the list: a commit of a ~300-file wave rebuilt 53 blobs (226 s), and a hot
		// header edited mid-story compiled every working-set file as its own translation unit (the
		// wave-2 integrator's 9 m 24 s). Without it a .cpp edit recompiles its blob -- 5-35 s, the
		// kernel's own blobs 5 s -- and a commit recompiles nothing.
		bUseAdaptiveUnityBuild = false;
	}
}
