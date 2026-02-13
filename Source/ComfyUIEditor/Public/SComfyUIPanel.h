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

private:
    // UI State
    FString PromptText;
    FString NegativePromptText;
    FString StatusText;
    TSharedPtr<class SImage> PreviewImage;
    TSharedPtr<FSlateBrush> ImageBrush;

    // Resolution presets
    TArray<TSharedPtr<FString>> ResolutionOptions;
    TSharedPtr<FString> SelectedResolution;

    // UI Callbacks
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
};
