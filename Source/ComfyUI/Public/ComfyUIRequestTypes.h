#pragma once

#include "CoreMinimal.h"
#include "ComfyUIRequestTypes.generated.h"

USTRUCT(BlueprintType)
struct FComfyUILoraSpec
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    float Strength = 1.0f;
};

USTRUCT(BlueprintType)
struct FComfyUISimpleWorkflowParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString PositivePrompt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString NegativePrompt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Checkpoint;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    TArray<FComfyUILoraSpec> Loras;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Steps = 25;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    float CFGScale = 7.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Width = 512;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Height = 512;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Seed = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Sampler = TEXT("euler");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Scheduler = TEXT("normal");
};

USTRUCT(BlueprintType)
struct FComfyUIFlux2WorkflowParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString PositivePrompt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString NegativePrompt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString UnetName = TEXT("flux-2-klein-4b.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString ClipName = TEXT("qwen_3_4b.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString VaeName = TEXT("flux2-vae.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Steps = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    float CFGScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Width = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Height = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Seed = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Sampler = TEXT("euler");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString FilenamePrefix = TEXT("UE_Flux2");
};

USTRUCT(BlueprintType)
struct FComfyUISubmitOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString ClientId;
};


DECLARE_DYNAMIC_DELEGATE_ThreeParams(FComfyUIResponseDelegate, bool, bSuccess, const FString&, ResponseJson, const FString&, PromptId);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FComfyUIWorkflowCompleteDelegate, bool, bSuccess, const FString&, PromptId);
