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

#if WITH_EDITOR
    // Defer settings registration until the Settings module is available
    FCoreDelegates::OnPostEngineInit.AddLambda([this]()
    {
        if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
        {
            SettingsModule->RegisterSettings(
                "Project", "Plugins", "ComfyUI",
                FText::FromString(TEXT("ComfyUI")),
                FText::FromString(TEXT("Configure ComfyUI integration settings")),
                GetMutableDefault<UComfyUISettings>()
            );
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Settings registered"));
        }
    });
#endif
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

#if WITH_EDITOR
    if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
    {
        SettingsModule->UnregisterSettings("Project", "Plugins", "ComfyUI");
    }
#endif
}

bool FComfyUIModule::EnsurePortableRunning()
{
    if (PortableHandle.IsValid() && FPlatformProcess::IsProcRunning(PortableHandle))
    {
        return true;
    }

    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    if (!Settings || !Settings->bAutoStartPortable)
    {
        return false;
    }

    FString WorkingDir = Settings->PortableRoot;
    if (WorkingDir.IsEmpty())
    {
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
        {
            WorkingDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("ComfyUIPortable"));
        }
        else
        {
            WorkingDir = FPaths::ProjectDir();
        }
    }

    const FString Executable = Settings->PortableExecutable;
    const FString Arguments = Settings->PortableArgs;

    if (Executable.IsEmpty())
    {
        return false;
    }

    PortableHandle = FPlatformProcess::CreateProc(
        *Executable,
        *Arguments,
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
