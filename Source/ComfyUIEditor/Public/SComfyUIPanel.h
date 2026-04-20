#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "ComfyUIRequestTypes.h"

// ============================================================================
// FComfyWorkflowParams
// ============================================================================
struct FComfyWorkflowParams
{
    FString WorkflowJson;
    FString OutputPrefix;
    FString RunningStatus;
    FString CompleteStatus;
    bool bUpdatePreview = true;
    bool bAutoImport = false;
    bool bConvertToHDRI = false;
    bool bTargetPreviewB = false;
};

// ============================================================================
// Per-model settings
// ============================================================================
struct FQwenSettings
{
    int32 Steps = 30;
    float CFGScale = 4.0f;
    float Shift = 3.0f;
    FString Sampler = TEXT("res_multistep");
    FString Scheduler = TEXT("simple");
};

struct FFluxSettings
{
    int32 Steps = 4;
    float CFGScale = 1.0f;
    FString Sampler = TEXT("euler");
    FString Scheduler = TEXT("simple");
};

// ============================================================================
// Resolution option
// ============================================================================

enum class EUpscaleMode : uint8 { None, TwoX, FourX };

struct FResolutionOption
{
    FString Label;
    int32 Width;
    int32 Height;
    EUpscaleMode UpscaleMode;
    int32 GenWidth;
    int32 GenHeight;
};

// ============================================================================
// SComfyUIPanel
// ============================================================================
class SComfyUIPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SComfyUIPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SComfyUIPanel();

private:
    // Tab switcher
    TSharedPtr<SWidgetSwitcher> TabSwitcher;
    TSharedRef<SWidget> BuildGenerateTab();
    TSharedRef<SWidget> BuildSettingsTab();

    // UI State
    FString PromptText = TEXT("Beautiful Scandinavian forest, big open foreground with tire tracks and puddles");
    FString NegativePromptText = TEXT("cartoon, low quality, blurry, distorted, unrealistic, people, vehicles");
    FString StatusText;
    bool bIsComfyReady = false;

    // Preview A
    TSharedPtr<class SImage> PreviewImageA;
    TSharedPtr<FSlateBrush> ImageBrushA;
    FString PreviewImagePathA;

    // Preview B
    TSharedPtr<class SImage> PreviewImageB;
    TSharedPtr<FSlateBrush> ImageBrushB;
    FString PreviewImagePathB;

    TWeakPtr<SComfyUIPanel> WeakThis;

    // Model family
    EComfyUIModelFamily SelectedModelFamily = EComfyUIModelFamily::Qwen;
    TArray<TSharedPtr<FString>> ModelFamilyOptions;
    TSharedPtr<FString> SelectedModelFamilyOption;

    // Resolution
    TArray<TSharedPtr<FResolutionOption>> ResolutionOptions;
    TSharedPtr<FResolutionOption> SelectedResolution;
    EUpscaleMode Img2ImgUpscaleMode = EUpscaleMode::None;
    TArray<TSharedPtr<FString>> Img2ImgUpscaleOptions;
    TSharedPtr<FString> SelectedImg2ImgUpscaleOption;


    // Generation state
    FString CurrentPromptId;
    FString CurrentFilenamePrefix = TEXT("UE_Editor");

    // Img2Img
    FString Img2ImgPromptText = TEXT("Edit the image...");

    // Timers
    FTimerHandle ConnectionTimerHandle;
    FTimerHandle PollingTimerHandle;
    FString PollingPromptId;

    // Composure
    FString LastImportedImagePath;
    TWeakObjectPtr<UTexture2D> LastImportedTexture;

    // Per-model settings
    FQwenSettings QwenSettings;
    FFluxSettings FluxSettings;
    TArray<TSharedPtr<FString>> SamplerOptions;
    TArray<TSharedPtr<FString>> SchedulerOptions;
    TSharedPtr<FString> QwenSelectedSampler;
    TSharedPtr<FString> QwenSelectedScheduler;
    TSharedPtr<FString> FluxSelectedSampler;
    TSharedPtr<FString> FluxSelectedScheduler;

    // HDR
    FString ConvertImageToHDR(const FString& SourceImagePath);
    UTextureCube* ImportHDRToProject(const FString& HdrFilePath, const FString& AssetName);
    void ApplyTextureToHDRIBackdrop(UTextureCube* Texture);

    // Workflow system
    void SubmitWorkflow(const FComfyWorkflowParams& Params);
    void OnWorkflowComplete(bool bSuccess, const FString& PromptId, FComfyWorkflowParams Params);
    void StartGeneration();
    void StartImg2Img();
    void Start360Generation(const FString& SourcePath);

    // Workflow patching helpers
    FString GetUpscalerModel(EUpscaleMode Mode) const;
    void PatchUpscaler(TSharedPtr<FJsonObject>& WorkflowObj,
        EUpscaleMode Mode,
        const FString& UpscaleLoaderNodeId,
        const FString& UpscaleNodeId,
        const FString& SaveNodeId,
        const FString& PreUpscaleOutputNodeId,
        int32 PreUpscaleOutputSlot) const;
    void DisableNode(TSharedPtr<FJsonObject>& WorkflowObj, const FString& NodeId) const;
    void PatchSaveImageInput(TSharedPtr<FJsonObject>& WorkflowObj,
        const FString& SaveNodeId,
        const FString& SourceNodeId,
        int32 SourceSlot) const;

    // UI Callbacks
    FReply OnGenerateClicked();
    FReply OnImg2ImgBrowseClicked();
    FReply OnImg2ImgClicked();
    FReply OnGenerate360Clicked(FString SourcePath);
    FReply OnApplyToComposureClicked(FString SourcePath);
    FReply OnImportClicked(FString SourcePath);
    void OnPromptTextChanged(const FText& NewText);
    void OnNegativePromptTextChanged(const FText& NewText);
    void OnModelFamilyChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void OnResolutionChanged(TSharedPtr<FResolutionOption> NewSelection, ESelectInfo::Type SelectInfo);

    // Helpers
    void PollComfyConnection();
    void StartHistoryPoller(const FString& PromptId, const FComfyWorkflowParams& Params);
    void StopHistoryPoller();
    void UpdateStatus(const FString& Status);
    void LoadAndDisplayImage(const FString& FilePath, bool bPreviewB);
    void ImportImageToProject(const FString& ImagePath, const FString& AssetNamePrefix);
    void ApplyTextureToComposurePlates(UTexture2D* Texture);
    void UploadImageToComfyUI(const FString& LocalFilePath, TFunction<void(bool, const FString&)> OnComplete);
    void DownloadImageFromComfyUI(const FString& Filename, TFunction<void(bool, const FString&)> OnComplete);
    FString GetLocalTempFolder() const;
    bool LoadWorkflowFromFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutWorkflow);
    FString SerializeWorkflow(const TSharedPtr<FJsonObject>& WorkflowObj);
};