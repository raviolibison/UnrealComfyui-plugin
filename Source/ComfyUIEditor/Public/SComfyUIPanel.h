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

    /** Prefix used for asset naming on import */
    FString OutputPrefix;

    /** Status message shown while the workflow is running */
    FString RunningStatus;

    /** Status message shown when the workflow completes successfully */
    FString CompleteStatus;

    /** If true, the output image updates the preview */
    bool bUpdatePreview = true;

    /** If true, auto-imports the result to the UE project (used for 360 generation) */
    bool bAutoImport = false;

    /**If true convert the result to HDR and apply to HDRIbackdrop*/
	bool bConvertToHDRI = false;

    /** If true, result goes to Preview B, otherwise Preview A */
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
    FString PromptText = TEXT("Beautiful Scandinavian forest, big open foreground with tire tracks and puddles");
    FString NegativePromptText = TEXT("cartoon, low quality, blurry, distorted, unrealistic, people, vehicles");
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

    // Img2Img
    FString Img2ImgPromptText = TEXT("Edit the image...");

    // Connection polling
    FTimerHandle ConnectionTimerHandle;

    // Composure
    FString LastImportedImagePath;
    TWeakObjectPtr<UTexture2D> LastImportedTexture;

    /** Convert an LDR image (PNG/JPEG) to a .hdr file with highlight boosting.
 *  Returns the path to the written .hdr file, or empty string on failure. */
    FString ConvertImageToHDR(const FString& SourceImagePath);

    /** Import an .hdr file as a UTexture2D with HDR settings (no sRGB, HDR float). */
    UTexture2D* ImportHDRToProject(const FString& HdrFilePath, const FString& AssetName);

    /** Find the HDRIBackdrop actor in the current level and apply the given texture. */
    void ApplyTextureToHDRIBackdrop(UTexture2D* Texture);

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

    /** Upload a local image file to ComfyUI's input folder via /upload/image */
    void UploadImageToComfyUI(const FString& LocalFilePath, TFunction<void(bool bSuccess, const FString& Filename)> OnComplete);

    /** Download an output image from ComfyUI via /view and save to local temp folder */
    void DownloadImageFromComfyUI(const FString& Filename, TFunction<void(bool bSuccess, const FString& LocalPath)> OnComplete);

    /** Returns a local temp folder for downloaded images */
    FString GetLocalTempFolder() const;

    bool LoadWorkflowFromFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutWorkflow);
    FString SerializeWorkflow(const TSharedPtr<FJsonObject>& WorkflowObj);
};
