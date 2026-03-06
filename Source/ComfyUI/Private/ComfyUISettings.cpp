#include "ComfyUISettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

UComfyUISettings::UComfyUISettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("ComfyUI");
}

FString UComfyUISettings::GetEffectivePortableRoot() const
{
	// If user explicitly set a path, use it
	if (!PortableRoot.IsEmpty())
	{
		return PortableRoot;
	}

	// Auto-detect from plugin directory
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealComfyui-plugin"));
	if (Plugin.IsValid())
	{
		FString PluginBaseDir = Plugin->GetBaseDir();
		
		// Check common folder names
		TArray<FString> CandidateFolders;
		CandidateFolders.Add(TEXT("ComfyUI_windows_portable"));
		CandidateFolders.Add(TEXT("ComfyUIPortable"));
		CandidateFolders.Add(TEXT("ComfyUI"));
		
		for (const FString& Folder : CandidateFolders)
		{
			FString TestPath = FPaths::Combine(PluginBaseDir, Folder);
			FString TestExe = FPaths::Combine(TestPath, PortableExecutable);
			if (FPaths::FileExists(TestExe))
			{
				UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Auto-detected PortableRoot: %s"), *TestPath);
				return TestPath;
			}
		}
		
		// Fallback: return the most common path
		FString DefaultPath = FPaths::Combine(PluginBaseDir, TEXT("ComfyUI_windows_portable"));
		UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Using default PortableRoot (not validated): %s"), *DefaultPath);
		return DefaultPath;
	}

	UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to find plugin for auto-detection!"));
	return TEXT("");
}
