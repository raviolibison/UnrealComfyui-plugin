#pragma once

#include "ComfyUIWebSocketHandler.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"

class FComfyUIModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	bool EnsurePortableRunning();

	TSharedPtr<FComfyUIWebSocketHandler> GetWebSocketHandler();

private:
	FProcHandle PortableHandle;
	TSharedPtr<FComfyUIWebSocketHandler> WebSocketHandler;
};