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

UENUM(BlueprintType)
enum class EComfyUIModelFamily : uint8
{
    Flux    UMETA(DisplayName = "Flux"),
    Qwen    UMETA(DisplayName = "Qwen")
};

USTRUCT(BlueprintType)
struct FComfyUIQwenGenerateParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString PositivePrompt;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString UnetName = TEXT("qwen_image_2512_fp8_e4m3fn.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString ClipName = TEXT("qwen_2.5_vl_7b_fp8_scaled.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString VaeName = TEXT("qwen_image_vae.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Steps = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    float CFGScale = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    float Shift = 7.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Width = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Height = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Seed = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Sampler = TEXT("res_multistep");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Scheduler = TEXT("simple");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString FilenamePrefix = TEXT("UE_Qwen");
};

// Qwen-Image-Edit-2511 (image to image / instruction editing)
USTRUCT(BlueprintType)
struct FComfyUIQwenEditParams
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Instruction;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString InputImageFilename; // filename on ComfyUI server after upload

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString UnetName = TEXT("qwen_image_edit_2511_fp8mixed.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString ClipName = TEXT("qwen_2.5_vl_7b_fp8_scaled.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString VaeName = TEXT("qwen_image_vae.safetensors");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Steps = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    float CFGScale = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    float Shift = 7.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Width = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Height = 1024;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    int32 Seed = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Sampler = TEXT("res_multistep");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString Scheduler = TEXT("simple");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString FilenamePrefix = TEXT("UE_QwenEdit");
};

USTRUCT(BlueprintType)
struct FComfyUISubmitOptions
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ComfyUI")
    FString ClientId;
};

// Delegates
DECLARE_DYNAMIC_DELEGATE_TwoParams(FComfyUIResponseDelegate, bool, bSuccess, const FString&, ResponseJson);

// Dynamic delegate for Blueprint-exposed workflow completion
DECLARE_DYNAMIC_DELEGATE_TwoParams(FComfyUIWorkflowCompleteDelegate, bool, bSuccess, const FString&, PromptId);

// Non-dynamic delegates for C++ internal use (editor panel, websocket)
DECLARE_DELEGATE_ThreeParams(FComfyUIResponseDelegateNative, bool /*bSuccess*/, const FString& /*ResponseJson*/, const FString& /*PromptId*/);
DECLARE_DELEGATE_TwoParams(FComfyUIWorkflowCompleteDelegateNative, bool /*bSuccess*/, const FString& /*PromptId*/);
