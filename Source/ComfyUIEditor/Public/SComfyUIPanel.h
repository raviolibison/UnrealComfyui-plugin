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
// Resolution preset
// ============================================================================

enum class EUpscaleMode : uint8 { None, TwoX, FourX };

struct FResolutionOption
{
    FString       Label;        // shown in dropdown
    int32         GenWidth  = 1024;
    int32         GenHeight = 1024;
    int32         FinalWidth  = 1024;  // after upscale (display only)
    int32         FinalHeight = 1024;
    EUpscaleMode  UpscaleMode = EUpscaleMode::None;
    bool          bIsCustom   = false;
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
    // ---- Tab layout --------------------------------------------------------
    TSharedPtr<SWidgetSwitcher> TabSwitcher;
    TSharedRef<SWidget> BuildGenerateTab();
    TSharedRef<SWidget> BuildSettingsTab();

    // ---- UI state ----------------------------------------------------------
    FString PromptText = TEXT("Beautiful Scandinavian forest, big open foreground with tire tracks and puddles");
    FString NegativePromptText = TEXT("cartoon, low quality, blurry, distorted, unrealistic, people, vehicles");
    FString StatusText;
    bool bIsComfyReady = false;

    // ---- Previews ----------------------------------------------------------
    TSharedPtr<class SImage> PreviewImageA;
    TSharedPtr<FSlateBrush>  ImageBrushA;
    FString PreviewImagePathA;

    TSharedPtr<class SImage> PreviewImageB;
    TSharedPtr<FSlateBrush>  ImageBrushB;
    FString PreviewImagePathB;

    TWeakPtr<SComfyUIPanel> WeakThis;

    // ---- Model family ------------------------------------------------------
    EComfyUIModelFamily SelectedModelFamily = EComfyUIModelFamily::Qwen;
    TArray<TSharedPtr<FString>> ModelFamilyOptions;
    TSharedPtr<FString> SelectedModelFamilyOption;

    // ---- Resolution --------------------------------------------------------
    // Populated by RebuildResolutionOptions() whenever the model family changes.
    TArray<TSharedPtr<FResolutionOption>> ResolutionOptions;
    TSharedPtr<FResolutionOption>         SelectedResolution;

    // Free-entry fields shown only when the Custom row is selected.
    int32 CustomWidth  = 1024;
    int32 CustomHeight = 1024;

    // ---- Img2Img upscale (separate from generation upscale) ----------------
    EUpscaleMode Img2ImgUpscaleMode = EUpscaleMode::None;
    TArray<TSharedPtr<FString>> Img2ImgUpscaleOptions;
    TSharedPtr<FString>         SelectedImg2ImgUpscaleOption;

    // ---- Generation state --------------------------------------------------
    FString CurrentPromptId;
    FString CurrentFilenamePrefix = TEXT("UE_Editor");

    // ---- Img2Img -----------------------------------------------------------
    FString Img2ImgPromptText = TEXT("Edit the image...");

    // ---- Timers ------------------------------------------------------------
    FTimerHandle ConnectionTimerHandle;
    FTimerHandle PollingTimerHandle;
    FString      PollingPromptId;

    // ---- Composure ---------------------------------------------------------
    FString LastImportedImagePath;
    TWeakObjectPtr<UTexture2D> LastImportedTexture;

    // ---- Per-model sampler/scheduler settings ------------------------------
    FQwenSettings QwenSettings;
    FFluxSettings FluxSettings;
    TArray<TSharedPtr<FString>> SamplerOptions;
    TArray<TSharedPtr<FString>> SchedulerOptions;
    TSharedPtr<FString> QwenSelectedSampler;
    TSharedPtr<FString> QwenSelectedScheduler;
    TSharedPtr<FString> FluxSelectedSampler;
    TSharedPtr<FString> FluxSelectedScheduler;

    // ---- HDR ---------------------------------------------------------------
    FString       ConvertImageToHDR(const FString& SourceImagePath);
    UTextureCube* ImportHDRToProject(const FString& HdrFilePath, const FString& AssetName);
    void          ApplyTextureToHDRIBackdrop(UTextureCube* Texture);

    // ---- Workflow system ---------------------------------------------------
    void SubmitWorkflow(const FComfyWorkflowParams& Params);
    void OnWorkflowComplete(bool bSuccess, const FString& PromptId, FComfyWorkflowParams Params);
    void StartGeneration();
    void StartImg2Img();
    void Start360Generation(const FString& SourcePath);

    // ---- Resolution helpers ------------------------------------------------
    /** Rebuild ResolutionOptions for the active model family. */
    void RebuildResolutionOptions();

    /** Helper used inside RebuildResolutionOptions(). */
    void AddResPreset(const FString& Label,
                      int32 GenW, int32 GenH,
                      EUpscaleMode Mode,
                      int32 FinalW, int32 FinalH);

    /** Read back the effective gen and final dimensions for current selection. */
    void GetEffectiveResolution(int32& OutGenW, int32& OutGenH,
                                int32& OutFinalW, int32& OutFinalH) const;

    bool IsCustomResSelected() const;

    // ---- Workflow patching helpers -----------------------------------------
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

    // ---- UI Callbacks ------------------------------------------------------
    FReply OnGenerateClicked();
    FReply OnImg2ImgBrowseClicked();
    FReply OnImg2ImgClicked();
    FReply OnGenerate360Clicked(FString SourcePath);
    FReply OnApplyToComposureClicked(FString SourcePath);
    FReply OnImportClicked(FString SourcePath);
    void   OnPromptTextChanged(const FText& NewText);
    void   OnNegativePromptTextChanged(const FText& NewText);
    void   OnModelFamilyChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void   OnResolutionChanged(TSharedPtr<FResolutionOption> NewSelection, ESelectInfo::Type SelectInfo);

    // ---- Misc helpers ------------------------------------------------------
    void    PollComfyConnection();
    void    StartHistoryPoller(const FString& PromptId, const FComfyWorkflowParams& Params);
    void    StopHistoryPoller();
    void    UpdateStatus(const FString& Status);
    void    LoadAndDisplayImage(const FString& FilePath, bool bPreviewB);
    void    ImportImageToProject(const FString& ImagePath, const FString& AssetNamePrefix);
    void    ApplyTextureToComposurePlates(UTexture2D* Texture);
    void    UploadImageToComfyUI(const FString& LocalFilePath,
                                  TFunction<void(bool, const FString&)> OnComplete);
    void    DownloadImageFromComfyUI(const FString& Filename,
                                     TFunction<void(bool, const FString&)> OnComplete);
    FString GetLocalTempFolder() const;
    bool    LoadWorkflowFromFile(const FString& RelativePath,
                                 TSharedPtr<FJsonObject>& OutWorkflow);
    FString SerializeWorkflow(const TSharedPtr<FJsonObject>& WorkflowObj);
};
