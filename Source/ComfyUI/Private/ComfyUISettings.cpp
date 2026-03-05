#include "ComfyUISettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

UComfyUISettings::UComfyUISettings()
{
	
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("ComfyUI");
	// Auto-detect plugin path if not set
	if (PortableRoot.IsEmpty())
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI"));
		if (Plugin.IsValid())
		{
			FString PluginBaseDir = Plugin->GetBaseDir();
			PortableRoot = FPaths::Combine(PluginBaseDir, TEXT("ComfyUI_windows_portable"));
            
			UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Auto-detected PortableRoot: %s"), *PortableRoot);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to find plugin!"));
		}
	}
}