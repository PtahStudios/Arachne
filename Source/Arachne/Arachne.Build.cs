using UnrealBuildTool;

public class Arachne : ModuleRules
{
	public Arachne(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Headers are included relative to the module root: "Creature/ArachnePawn.h", "AI/ArachneBrainComponent.h", ...
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });   // Test/ArachneTraversalTest report
	}
}
