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
    // Watch for workflow completion via WebSocket
    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void WatchWorkflowCompletion(const FString& PromptId, const FComfyUIWorkflowCompleteDelegate& OnComplete);
    
    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static bool EnsurePortableRunning();

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void CheckComfyUIReady(const FComfyUIResponseDelegate& OnComplete);

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void WaitForComfyUIReady(float TimeoutSeconds, const FComfyUIResponseDelegate& OnComplete);

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static void SubmitWorkflowJson(const FString& WorkflowJson, const FComfyUISubmitOptions& Options, const FComfyUIResponseDelegate& OnComplete);

    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static FString BuildFlux2WorkflowJson(const FComfyUIFlux2WorkflowParams& Params);

    // Generate unique asset name with timestamp
    UFUNCTION(BlueprintPure, Category = "ComfyUI")
    static FString GenerateUniqueAssetName(const FString& BasePath, const FString& Prefix);

    // Get the ComfyUI output folder path
    UFUNCTION(BlueprintPure, Category = "ComfyUI")
    static FString GetComfyUIOutputFolder();

    // Load image from file as runtime texture (temporary)
    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static UTexture2D* LoadImageFromFile(const FString& FilePath);


    // Import image as permanent asset in Content Browser
    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static UTexture2D* ImportImageAsAsset(const FString& SourceFilePath, const FString& DestinationPath);

    // Get the latest image file from ComfyUI output folder
    UFUNCTION(BlueprintCallable, Category = "ComfyUI")
    static FString GetLatestOutputImage(const FString& FilenamePrefix);


private:
    static FString BuildPromptWrapperJson(const TSharedPtr<FJsonObject>& PromptObject, const FString& ClientId);

    static void PollComfyUIReady(float TimeoutSeconds, float ElapsedTime, const FComfyUIResponseDelegate& OnComplete);
};
