#include "ComfyUIModule.h"
#include "ComfyUISettings.h"
#include "ComfyUIWebSocketHandler.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

#if WITH_EDITOR
#include "ISettingsModule.h"
#endif

void FComfyUIModule::StartupModule()
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Module started"));

    // Create WebSocket handler
    WebSocketHandler = MakeShared<FComfyUIWebSocketHandler>();
}

void FComfyUIModule::ShutdownModule()
{
    // Clean up WebSocket
    if (WebSocketHandler.IsValid())
    {
        WebSocketHandler->Disconnect();
        WebSocketHandler.Reset();
    }

    // Clean up if ComfyUI is running
    if (PortableHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(PortableHandle);
        PortableHandle.Reset();
    }
}

bool FComfyUIModule::EnsurePortableRunning()
{
    // 1. If already running, do nothing
    if (PortableHandle.IsValid() && FPlatformProcess::IsProcRunning(PortableHandle))
    {
        return true;
    }

    // 2. Only auto-start if the setting is enabled
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    if (!Settings || !Settings->bAutoStartPortable)
    {
        return false;
    }

    return LaunchPortable();
}

bool FComfyUIModule::ForceStartPortable()
{
    // 1. If already running, do nothing
    if (PortableHandle.IsValid() && FPlatformProcess::IsProcRunning(PortableHandle))
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Already running"));
        return true;
    }

    return LaunchPortable();
}

bool FComfyUIModule::LaunchPortable()
{
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    if (!Settings)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Settings not available"));
        return false;
    }

    FString Executable = Settings->PortableExecutable;
    if (Executable.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: PortableExecutable is empty"));
        return false;
    }

    // Use the effective root (auto-detects if PortableRoot is empty)
    FString WorkingDir = Settings->GetEffectivePortableRoot();
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Effective PortableRoot: %s"), *WorkingDir);
    
    if (WorkingDir.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Could not determine PortableRoot"));
        return false;
    }

    FString FullExecutablePath = FPaths::Combine(WorkingDir, Executable);

    // If not found at the direct path, try searching
    if (!FPaths::FileExists(FullExecutablePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Executable not found at: %s, searching..."), *FullExecutablePath);
        
        // Try plugin directory search as fallback
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
        {
            FString PluginDir = Plugin->GetBaseDir();
            TArray<FString> FoundFiles;
            IFileManager::Get().FindFilesRecursive(FoundFiles, *PluginDir, *Executable, true, false);
            if (FoundFiles.Num() > 0)
            {
                FullExecutablePath = FoundFiles[0];
                WorkingDir = FPaths::GetPath(FullExecutablePath);
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Found executable via recursive search: %s"), *FullExecutablePath);
            }
        }
    }

    // Final validation
    if (!FPaths::FileExists(FullExecutablePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Could not find executable '%s'. Tried path: %s"), *Executable, *FullExecutablePath);
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Please set the correct 'Portable Root Path' in Project Settings > Plugins > ComfyUI"));
        return false;
    }

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Launching: %s (working dir: %s)"), *FullExecutablePath, *WorkingDir);

    // Launch
    PortableHandle = FPlatformProcess::CreateProc(
        *FullExecutablePath,
        *Settings->PortableArgs,
        true, false, false,
        nullptr, 0,
        *WorkingDir,
        nullptr);

    if (PortableHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Process launched successfully"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to create process"));
    }

    return PortableHandle.IsValid();
}

TSharedPtr<FComfyUIWebSocketHandler> FComfyUIModule::GetWebSocketHandler()
{
    return WebSocketHandler;
}

IMPLEMENT_MODULE(FComfyUIModule, ComfyUI)
