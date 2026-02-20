using UnrealBuildTool;

public class ComfyUIEditor : ModuleRules
{
    public ComfyUIEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "ComfyUI"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Slate",
                "SlateCore",
                "UnrealEd",
                "ToolMenus",
                "WorkspaceMenuStructure",
                "InputCore",
                "ImageWrapper",
                "HTTP",
                "Json",
                "JsonUtilities"
            }
        );
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "Composure"  // or try "ComposureFramework"
            });
        }
    }
}
