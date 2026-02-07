using UnrealBuildTool;

public class ComfyUI : ModuleRules
{
    public ComfyUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "HTTP",
                "Json",
                "JsonUtilities",
                "Projects",
                "ImageWrapper",
                "WebSockets"
            }
        );
        
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]{
                "Settings",
                "UnrealEd",
                "AssetTools",
                "AssetRegistry"
            });
        }
    }
}
