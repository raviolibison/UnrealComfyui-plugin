#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ComfyUISettings.generated.h"

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "ComfyUI"))
class COMFYUI_API UComfyUISettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Connection",
        meta = (DisplayName = "Base URL"))
    FString BaseUrl = TEXT("http://127.0.0.1:8188");

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Portable",
        meta = (DisplayName = "Auto Start Portable"))
    bool bAutoStartPortable = false;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Portable",
        meta = (DisplayName = "Portable Root Path"))
    FString PortableRoot;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Portable",
        meta = (DisplayName = "Portable Executable"))
    FString PortableExecutable = TEXT("run_nvidia_gpu.bat");

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Portable",
        meta = (DisplayName = "Portable Arguments"))
    FString PortableArgs;
};
