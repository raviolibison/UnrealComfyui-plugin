#include "ComfyUIModule.h"

#include "ComfyUISettings.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

void FComfyUIModule::StartupModule()
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Module Started"));
    WebSocketHandler = MakeShared<FComfyUIWebSocketHandler>();
}

void FComfyUIModule::ShutdownModule()
{
    if (WebSocketHandler.IsValid())
    {
        WebSocketHandler->Disconnect();
        WebSocketHandler.Reset();
    }
    
    
    if (PortableHandle.IsValid())
    {
        FPlatformProcess::TerminateProc(PortableHandle);
        PortableHandle.Reset();
    }
}

TSharedPtr<FComfyUIWebSocketHandler> FComfyUIModule::GetWebSocketHandler()
{
    return WebSocketHandler;
}

bool FComfyUIModule::EnsurePortableRunning()
{
    if (PortableHandle.IsValid() && FPlatformProcess::IsProcRunning(PortableHandle))
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Already running"));
        return true;
    }

    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    if (!Settings)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Settings is null"));
        return false;
    }
    
    if (!Settings->bAutoStartPortable)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: AutoStart is disabled"));
        return false;
    }

    FString WorkingDir = Settings->PortableRoot;
    if (WorkingDir.IsEmpty())
    {
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
        {
            WorkingDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("ComfyUI_windows_portable"));
        }
        else
        {
            WorkingDir = FPaths::ProjectDir();
        }
    }

    const FString Executable = Settings->PortableExecutable;

    if (Executable.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Executable is empty"));
        return false;
    }

    // Build full path to executable
    FString FullExecutablePath = FPaths::Combine(WorkingDir, Executable);
    FPaths::NormalizeFilename(FullExecutablePath);
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Working Dir: %s"), *WorkingDir);
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Full Path: %s"), *FullExecutablePath);
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: File Exists: %s"), FPaths::FileExists(FullExecutablePath) ? TEXT("YES") : TEXT("NO"));

    // Try launching the bat file directly with ShellExecute-style parameters
    uint32 ProcessID = 0;
    PortableHandle = FPlatformProcess::CreateProc(
        *FullExecutablePath,  // Direct path to .bat
        TEXT(""),             // No arguments
        true,                 // bLaunchDetached
        false,                // bLaunchHidden
        false,                // bLaunchReallyHidden
        &ProcessID,           // Get process ID
        0,                    // Normal priority
        *WorkingDir,          // Working directory
        nullptr,              // No pipe
        nullptr);             // No pipe

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Process ID: %d"), ProcessID);
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Handle Valid: %s"), PortableHandle.IsValid() ? TEXT("YES") : TEXT("NO"));
    
    if (PortableHandle.IsValid())
    {
        FPlatformProcess::Sleep(1.0f);  // Wait a full second
        const bool bRunning = FPlatformProcess::IsProcRunning(PortableHandle);
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Still Running After 1s: %s"), bRunning ? TEXT("YES") : TEXT("NO"));
        
        if (!bRunning)
        {
            int32 ReturnCode = 0;
            FPlatformProcess::GetProcReturnCode(PortableHandle, &ReturnCode);
            UE_LOG(LogTemp, Error, TEXT("ComfyUI: Process exited with code: %d"), ReturnCode);
        }
        
        return bRunning;
    }
    
    return false;
}


IMPLEMENT_MODULE(FComfyUIModule, ComfyUI)