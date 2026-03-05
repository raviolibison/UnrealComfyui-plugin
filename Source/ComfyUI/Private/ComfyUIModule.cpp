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

    // 2. Check settings
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    if (!Settings || !Settings->bAutoStartPortable)
    {
        return false;
    }

    FString Executable = Settings->PortableExecutable; // e.g., "start_from_unreal.bat"
    if (Executable.IsEmpty())
    {
        return false;
    }

    FString WorkingDir = Settings->PortableRoot;
    FString FullExecutablePath;

    // 3. Logic to find the file
    // A. If the user manually set a path in Project Settings, respect it absolutely.
    if (!WorkingDir.IsEmpty())
    {
        FullExecutablePath = FPaths::Combine(WorkingDir, Executable);
    }
    // B. Otherwise, auto-detect the folder inside the Plugin directory
    else if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
    {
        FString PluginDir = Plugin->GetBaseDir();
        
        // List of common folder names to check
        TArray<FString> CandidateFolders;
        CandidateFolders.Add(TEXT("ComfyUIPortable"));
        CandidateFolders.Add(TEXT("ComfyUI_windows_portable"));
        CandidateFolders.Add(TEXT("ComfyUI"));
        
        bool bFound = false;

        // Check specific folders first
        for (const FString& FolderName : CandidateFolders)
        {
            FString TestDir = FPaths::Combine(PluginDir, FolderName);
            FString TestExe = FPaths::Combine(TestDir, Executable);

            if (FPaths::FileExists(TestExe))
            {
                WorkingDir = TestDir;
                FullExecutablePath = TestExe;
                bFound = true;
                UE_LOG(LogTemp, Log, TEXT("ComfyUI: Auto-detected installation at: %s"), *WorkingDir);
                break;
            }
        }

        // C. Fallback: Search the entire plugin directory recursively
        if (!bFound)
        {
             TArray<FString> FoundFiles;
             IFileManager::Get().FindFilesRecursive(FoundFiles, *PluginDir, *Executable, true, false);
             if (FoundFiles.Num() > 0)
             {
                 FullExecutablePath = FoundFiles[0]; // First match
                 WorkingDir = FPaths::GetPath(FullExecutablePath);
                 UE_LOG(LogTemp, Log, TEXT("ComfyUI: Found executable via search at: %s"), *FullExecutablePath);
             }
        }
    }

    // 4. Validation
    if (WorkingDir.IsEmpty() || !FPaths::FileExists(FullExecutablePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Could not find '%s' in Plugin directory. Please check your folder structure or Project Settings."), *Executable);
        return false;
    }

    // 5. Launch
    PortableHandle = FPlatformProcess::CreateProc(
        *FullExecutablePath,
        *Settings->PortableArgs,
        true, false, false,
        nullptr, 0,
        *WorkingDir,
        nullptr);

    return PortableHandle.IsValid();
}

TSharedPtr<FComfyUIWebSocketHandler> FComfyUIModule::GetWebSocketHandler()
{
    return WebSocketHandler;
}

IMPLEMENT_MODULE(FComfyUIModule, ComfyUI)
