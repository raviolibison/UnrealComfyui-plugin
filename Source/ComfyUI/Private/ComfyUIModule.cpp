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

    // Use plugin-relative path by default, override from settings only if explicitly set
    FString WorkingDir;
    if (!Settings->PortableRoot.IsEmpty())
    {
        WorkingDir = Settings->PortableRoot;
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Using custom PortableRoot from settings: %s"), *WorkingDir);
    }
    else
    {
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
        {
            WorkingDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("ComfyUI_windows_portable"));
        }
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Using auto-detected PortableRoot: %s"), *WorkingDir);
    }

    if (WorkingDir.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Could not determine PortableRoot"));
        return false;
    }

    FString PythonPath = FPaths::Combine(WorkingDir, TEXT("python_embeded/python.exe"));
    FString ScriptPath = FPaths::Combine(WorkingDir, TEXT("ComfyUI/main.py"));

    if (!FPaths::FileExists(PythonPath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: python_embeded/python.exe not found at: %s"), *PythonPath);
        return false;
    }

    if (!FPaths::FileExists(ScriptPath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: ComfyUI/main.py not found at: %s"), *ScriptPath);
        return false;
    }

    FString Args = FString::Printf(TEXT("\"%s\" --gpu-only"), *ScriptPath.Replace(TEXT("/"), TEXT("\\")));

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Launching python: %s"), *PythonPath);
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Args: %s"), *Args);

    FString BatPath = FPaths::Combine(WorkingDir, TEXT("start_from_unreal.bat"));
    FString FinalBat = BatPath.Replace(TEXT("/"), TEXT("\\"));

    PortableHandle = FPlatformProcess::CreateProc(
        TEXT("cmd.exe"),
        *FString::Printf(TEXT("/c \"%s\""), *FinalBat),
        true, false, false,
        nullptr, 0,
        *WorkingDir.Replace(TEXT("/"), TEXT("\\")),
        nullptr);
    
    
    if (PortableHandle.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Launched successfully"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to launch"));
    }

    return PortableHandle.IsValid();
}

TSharedPtr<FComfyUIWebSocketHandler> FComfyUIModule::GetWebSocketHandler()
{
    return WebSocketHandler;
}

IMPLEMENT_MODULE(FComfyUIModule, ComfyUI)
