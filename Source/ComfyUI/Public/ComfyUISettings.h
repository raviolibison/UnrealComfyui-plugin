#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ComfyUISettings.generated.h"

UCLASS(Config = Engine, DefaultConfig)
class COMFYUI_API UComfyUISettings : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ComfyUI")
    FString BaseUrl = TEXT("http://127.0.0.1:8188");

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ComfyUI|Portable")
    bool bAutoStartPortable = false;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ComfyUI|Portable", meta = (EditCondition = "bAutoStartPortable"))
    FString PortableRoot;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ComfyUI|Portable", meta = (EditCondition = "bAutoStartPortable"))
    FString PortableExecutable = TEXT("python");

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ComfyUI|Portable", meta = (EditCondition = "bAutoStartPortable"))
    FString PortableArgs = TEXT("main.py --listen 127.0.0.1 --port 8188");

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ComfyUI")
    FString DefaultCheckpoint;

    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "ComfyUI")
    TArray<FString> DefaultLoras;
};
