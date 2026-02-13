#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ComfyUIRequestTypes.h"
#include "ComfyUIBlueprintLibrary.generated.h"

UCLASS()
class COMFYUI_API UComfyUIBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // --- Lifecycle ---

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static bool EnsurePortableRunning();

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void CheckComfyUIReady(const FComfyUIResponseDelegate& OnComplete);

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void WaitForComfyUIReady(float TimeoutSeconds, const FComfyUIResponseDelegate& OnComplete);

    // --- Workflow Submission ---

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void SubmitWorkflowJson(const FString& WorkflowJson, const FComfyUISubmitOptions& Options, const FComfyUIResponseDelegate& OnComplete);

    // --- Workflow Builders ---

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static FString BuildSimpleWorkflowJson(const FComfyUISimpleWorkflowParams& Params);

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static FString BuildFlux2WorkflowJson(const FComfyUIFlux2WorkflowParams& Params);

    // --- Image Loading ---

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static UTexture2D* LoadImageFromFile(const FString& FilePath);

    UFUNCTION(BlueprintPure, Category = "ComfyUI")
    static FString GetComfyUIOutputFolder();

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static FString GetLatestOutputImage(const FString& FilenamePrefix);

    UFUNCTION(BlueprintCallable, Category = "ComfyUI", meta = (DisplayName = "Import Image As Asset"))
    static UTexture2D* ImportImageAsAsset(const FString& SourceFilePath, const FString& DestAssetPath);

    UFUNCTION(BlueprintPure, Category = "ComfyUI")
    static FString GenerateUniqueAssetName(const FString& BasePath, const FString& BaseName);

    // --- WebSocket / Monitoring ---

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void WatchWorkflowCompletion(const FString& PromptId, const FComfyUIWorkflowCompleteDelegate& OnComplete);

private:
    static FString GetBaseUrl();
    static void TryEnsurePortable();
    static FString BuildPromptWrapperJson(const TSharedPtr<FJsonObject>& PromptObject, const FString& ClientId);
    static void PollComfyUIReady(float TimeoutSeconds, float ElapsedTime, const FComfyUIResponseDelegate& OnComplete);
};
