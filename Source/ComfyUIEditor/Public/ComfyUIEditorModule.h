#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FComfyUIEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    static const FName ComfyUITabName;

private:
    void RegisterMenus();
    TSharedRef<SDockTab> SpawnComfyUITab(const FSpawnTabArgs& Args);
};
