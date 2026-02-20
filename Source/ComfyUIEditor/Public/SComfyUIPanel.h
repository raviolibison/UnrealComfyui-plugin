#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "ComfyUIRequestTypes.h"

class SComfyUIPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SComfyUIPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SComfyUIPanel();

private:
    // UI State
    FString PromptText;
    FString NegativePromptText;
    FString StatusText;
    bool bIsComfyReady = false; // <--- This was likely missing!

    TSharedPtr<class SImage> PreviewImage;
    TSharedPtr<FSlateBrush> ImageBrush;

    TWeakPtr<SComfyUIPanel> WeakThis;

    // Resolution controls
    TArray<TSharedPtr<FString>> WidthOptions;
    TArray<TSharedPtr<FString>> HeightOptions;
    TSharedPtr<FString> SelectedWidth;
    TSharedPtr<FString> SelectedHeight;

    //Target Actor array
    TArray<TWeakObjectPtr<AActor>> TargetActors;
    
    int32 CustomWidth = 1024;
    int32 CustomHeight = 1024;
    
    void OnWidthChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void OnHeightChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
    void OnCustomWidthChanged(int32 NewValue);
    void OnCustomHeightChanged(int32 NewValue);

    // Polling & Connection Logic
    int32 ConnectionAttempts = 0;
    FTimerHandle ConnectionTimerHandle;
    void PollComfyConnection();
    void StartGeneration(); // <--- This declaration is required for the .cpp to work

    // UI Callbacks
    FReply OnStartComfyClicked(); // <--- This too
    FReply OnGenerateClicked();
    void OnAutoApplyCheckChanged(ECheckBoxState NewState);
    void OnPromptTextChanged(const FText& NewText);
    void OnNegativePromptTextChanged(const FText& NewText);
    FReply OnAddSelectedClicked();
    FReply OnClearAllClicked();
    FReply OnApplyToActorsClicked();

    //Auto apply
    bool bAutoApplyEnabled = false;

    // Generation callbacks
    void OnWorkflowSubmitted(bool bSuccess, const FString& ResponseJson, const FString& PromptId);
    void OnGenerationComplete(bool bSuccess, const FString& PromptId);

    //Material
    UMaterial* BaseMaterial = nullptr;
    TMap<TWeakObjectPtr<AActor>, UMaterialInstanceDynamic*> ActorMaterialMap;
    
    

    // Helper functions
    void UpdateStatus(const FString& Status);
    void LoadAndDisplayImage(const FString& FilePath);
    void LoadBaseMaterial();
    TArray<AActor*> GetSelectedActors();
    void AddSelectedActorsToList();
    void ClearTargetActors();
    void RemoveActorFromList(AActor* Actor);
    UMeshComponent* GetMeshComponentFromActor(AActor* Actor);
    bool IsActorValidForMaterial(AActor* Actor);
    void ApplyMaterialToTargetActors(UTexture2D* Texture);
    

    // Current generation state
    FString CurrentPromptId;
    FString CurrentFilenamePrefix;
    FString CurrentPreviewImagePath;

    FReply OnImportClicked();

    //Composure
    void ApplyTextureToComposurePlates(UTexture2D* Texture);
    FReply OnApplyToComposureClicked();
    FString LastImportedImagePath;
    TWeakObjectPtr<UTexture2D> LastImportedTexture; 
    
};