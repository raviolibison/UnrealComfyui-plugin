#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ComfyUIRequestTypes.h"

// ============================================================================
// FComfyWorkflowParams
// A small descriptor that travels with every workflow submission.
// ============================================================================
struct FComfyWorkflowParams
{
    /** The fully patched workflow JSON, ready to POST to /prompt */
    FString WorkflowJson;

    /** Filename prefix to pass to GetLatestOutputImage after completion */
    FString OutputPrefix;

    /** Status message shown while the workflow is running */
    FString RunningStatus;

    /** Status message shown when the workflow completes successfully */
    FString CompleteStatus;

    /** If true, the output image replaces CurrentPreviewImagePath and updates the preview */
    bool bUpdatePreview = true;

    /** If true, auto-imports the result to the UE project (used for 360 generation) */
    bool bAutoImport = false;
    
    bool bTargetPreviewB = false;
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
    // -------------------------------------------------------------------------
    // UI State
    // -------------------------------------------------------------------------
    FString PromptText = TEXT("Beautiful tropical beach, big open foreground with tire tracks and puddles");
    FString NegativePromptText = TEXT("cartoon, anime, low quality, blurry, distorted, unrealistic, people, vehicles");
    FString StatusText;
    bool bIsComfyReady = false;

    // Preview A — text2img / browsed image
    TSharedPtr<class SImage> PreviewImageA;
    TSharedPtr<FSlateBrush> ImageBrushA;
    FString PreviewImagePathA;

    // Preview B — img2img result
    TSharedPtr<class SImage> PreviewImageB;
    TSharedPtr<FSlateBrush> ImageBrushB;
    FString PreviewImagePathB;
    
    TWeakPtr<SComfyUIPanel> WeakThis;

    // Resolution controls
    TArray<TSharedPtr<FString>> WidthOptions;
    TArray<TSharedPtr<FString>> HeightOptions;
    TSharedPtr<FString> SelectedWidth;
    TSharedPtr<FString> SelectedHeight;
    int32 CustomWidth = 1024;
    int32 CustomHeight = 1024;

    // Current generation state
    FString CurrentPromptId;
    FString CurrentFilenamePrefix = TEXT("UE_Editor");

    // Img2Img input — set via file picker, falls back to preview image
    FString Img2ImgInputPath;
    FString Img2ImgPromptText = TEXT("Edit the image...");

    // Connection polling
    int32 ConnectionAttempts = 0;
    FTimerHandle ConnectionTimerHandle;

    // Composure
    FString LastImportedImagePath;
    TWeakObjectPtr<UTexture2D> LastImportedTexture;

    // -------------------------------------------------------------------------
    // Generic Workflow System
    // -------------------------------------------------------------------------
    void SubmitWorkflow(const FComfyWorkflowParams& Params);
    void OnWorkflowComplete(bool bSuccess, const FString& PromptId, FComfyWorkflowParams Params);

    // -------------------------------------------------------------------------
    // Workflow Builders
    // -------------------------------------------------------------------------
    void StartGeneration();
    void StartImg2Img();
    void Start360Generation(const FString& SourcePath);

    // -------------------------------------------------------------------------
    // UI Callbacks
    // -------------------------------------------------------------------------
    FReply OnStartComfyClicked();
    FReply OnGenerateClicked();
    FReply OnImg2ImgBrowseClicked();
    FReply OnImg2ImgClicked();
    FReply OnGenerate360Clicked(FString SourcePath);
    FReply OnApplyToComposureClicked(FString SourcePath);
    FReply OnImportClicked(FString SourcePath);

    void OnPromptTextChanged(const FText& NewText);
    void OnNegativePromptTextChanged(const FText& NewText);
    void OnWidthChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void OnHeightChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void OnCustomWidthChanged(int32 NewValue);
    void OnCustomHeightChanged(int32 NewValue);

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    void PollComfyConnection();
    void UpdateStatus(const FString& Status);
    void LoadAndDisplayImage(const FString& FilePath, bool bPreviewB);
    void ImportImageToProject(const FString& ImagePath, const FString& AssetNamePrefix);
    void ApplyTextureToComposurePlates(UTexture2D* Texture);
    FString GetComfyInputFolder() const;

    bool LoadWorkflowFromFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutWorkflow);
    FString SerializeWorkflow(const TSharedPtr<FJsonObject>& WorkflowObj);
};
