#pragma once

#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"

class FComfyUIWebSocketHandler;

class COMFYUI_API FComfyUIModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Only starts if bAutoStartPortable is true in settings */
    bool EnsurePortableRunning();
    
    /** Force-starts ComfyUI regardless of bAutoStartPortable (for user-initiated starts) */
    bool ForceStartPortable();

    TSharedPtr<FComfyUIWebSocketHandler> GetWebSocketHandler();

private:
    /** Internal launch logic shared by both methods */
    bool LaunchPortable();
    
    FProcHandle PortableHandle;
    TSharedPtr<FComfyUIWebSocketHandler> WebSocketHandler;
};
