#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ComfyUIRequestTypes.h"

// ============================================================================
// FComfyWorkflowParams
// A small descriptor that travels with every workflow submission.
// It tells OnWorkflowComplete what to search for and what to do with the result.
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

    /** If true and TargetActors are set, auto-applies the result texture to actors */
    bool bAutoApply = false;
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
    float Img2ImgStrength = 0.75f;

    TSharedPtr<class SImage> PreviewImage;
    TSharedPtr<FSlateBrush> ImageBrush;
    TWeakPtr<SComfyUIPanel> WeakThis;

    // Resolution controls
    TArray<TSharedPtr<FString>> WidthOptions;
    TArray<TSharedPtr<FString>> HeightOptions;
    TSharedPtr<FString> SelectedWidth;
    TSharedPtr<FString> SelectedHeight;
    int32 CustomWidth = 1024;
    int32 CustomHeight = 1024;

    // Target actors
    TArray<TWeakObjectPtr<AActor>> TargetActors;
    bool bAutoApplyEnabled = false;

    // Current generation state
    FString CurrentPreviewImagePath;
    FString CurrentPromptId;
    FString CurrentFilenamePrefix = TEXT("UE_Editor");

    // Material
    UMaterial* BaseMaterial = nullptr;
    TMap<TWeakObjectPtr<AActor>, UMaterialInstanceDynamic*> ActorMaterialMap;

    // Composure
    FString LastImportedImagePath;
    TWeakObjectPtr<UTexture2D> LastImportedTexture;

    // Connection polling
    int32 ConnectionAttempts = 0;
    FTimerHandle ConnectionTimerHandle;

    // -------------------------------------------------------------------------
    // Generic Workflow System
    // -------------------------------------------------------------------------

    /**
     * The single submission function used by ALL workflows.
     * Wraps the JSON, POSTs to /prompt, registers the WebSocket watcher,
     * and carries Params through to OnWorkflowComplete via a lambda capture.
     */
    void SubmitWorkflow(const FComfyWorkflowParams& Params);

    /**
     * The single completion callback used by ALL workflows.
     * Params tells it what prefix to search for and what to do with the result.
     */
    void OnWorkflowComplete(bool bSuccess, const FString& PromptId, FComfyWorkflowParams Params);

    // -------------------------------------------------------------------------
    // Workflow Builders
    // Each one constructs an FComfyWorkflowParams and calls SubmitWorkflow.
    // -------------------------------------------------------------------------
    void StartGeneration();     // Standard FLUX.2 text-to-image
    void StartImg2Img();        // Img2Img from current preview
    void Start360Generation();  // 360 HDRI from current preview

    // -------------------------------------------------------------------------
    // UI Callbacks
    // -------------------------------------------------------------------------
    FReply OnStartComfyClicked();
    FReply OnGenerateClicked();
    FReply OnImg2ImgClicked();
    FReply OnGenerate360Clicked();
    FReply OnApplyToActorsClicked();
    FReply OnApplyToComposureClicked();
    FReply OnImportClicked();
    FReply OnAddSelectedClicked();
    FReply OnClearAllClicked();

    void OnPromptTextChanged(const FText& NewText);
    void OnNegativePromptTextChanged(const FText& NewText);
    void OnWidthChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void OnHeightChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void OnCustomWidthChanged(int32 NewValue);
    void OnCustomHeightChanged(int32 NewValue);
    void OnAutoApplyCheckChanged(ECheckBoxState NewState);

    // -------------------------------------------------------------------------
    // Post-completion actions (called from OnWorkflowComplete)
    // -------------------------------------------------------------------------
    void Apply360ToSkyLight(UTexture2D* Texture360);
    void ApplyMaterialToTargetActors(UTexture2D* Texture);
    void ApplyTextureToComposurePlates(UTexture2D* Texture);

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    void PollComfyConnection();
    void UpdateStatus(const FString& Status);
    void LoadAndDisplayImage(const FString& FilePath);
    void LoadBaseMaterial();
    TArray<AActor*> GetSelectedActors();
    void AddSelectedActorsToList();
    void ClearTargetActors();
    void RemoveActorFromList(AActor* Actor);
    UMeshComponent* GetMeshComponentFromActor(AActor* Actor);
    bool IsActorValidForMaterial(AActor* Actor);

    /** Loads a workflow JSON file from the plugin's workflows/ folder */
    bool LoadWorkflowFromFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutWorkflow);

    /** Serializes a workflow object to a JSON string */
    FString SerializeWorkflow(const TSharedPtr<FJsonObject>& WorkflowObj);
};
