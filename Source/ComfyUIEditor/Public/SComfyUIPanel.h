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
    
    void OnPromptTextChanged(const FText& NewText);
    void OnNegativePromptTextChanged(const FText& NewText);
    void OnResolutionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

    // Generation callbacks
    void OnWorkflowSubmitted(bool bSuccess, const FString& ResponseJson, const FString& PromptId);
    void OnGenerationComplete(bool bSuccess, const FString& PromptId);

    // Helper functions
    void UpdateStatus(const FString& Status);
    void LoadAndDisplayImage(const FString& FilePath);
    void GetResolutionFromString(const FString& ResString, int32& OutWidth, int32& OutHeight);

    // Current generation state
    FString CurrentPromptId;
    FString CurrentFilenamePrefix;
    FString CurrentPreviewImagePath;

    FReply OnImportClicked();
};