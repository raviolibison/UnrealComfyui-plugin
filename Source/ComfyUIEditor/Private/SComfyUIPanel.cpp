#include "SComfyUIPanel.h"
#include "ComfyUIBlueprintLibrary.h"
#include "ComfyUIModule.h"
#include "ComfyUISettings.h"
#include "ComfyUIWebSocketHandler.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

#define LOCTEXT_NAMESPACE "SComfyUIPanel"

void SComfyUIPanel::Construct(const FArguments& InArgs)
{
    // Initialize resolution options
    ResolutionOptions.Add(MakeShared<FString>(TEXT("512x512")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("768x768")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("1024x576")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("1024x1024")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("1280x720")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("1920x1080")));
    SelectedResolution = ResolutionOptions[5]; // Default 1920x1080

    StatusText = TEXT("Ready");
    CurrentFilenamePrefix = TEXT("UE_Editor");

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        .Padding(10.0f)
        [
            SNew(SVerticalBox)

            // Title
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 10)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Title", "ComfyUI Image Generator"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
            ]

            // Positive Prompt
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PromptLabel", "Prompt:"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 10)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("PromptHint", "Describe the image you want to generate..."))
                .OnTextChanged(this, &SComfyUIPanel::OnPromptTextChanged)
                .MinDesiredWidth(400)
            ]

            // Negative Prompt
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("NegativePromptLabel", "Negative Prompt (Optional):"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 10)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("NegativePromptHint", "What to avoid in the image..."))
                .OnTextChanged(this, &SComfyUIPanel::OnNegativePromptTextChanged)
                .MinDesiredWidth(400)
            ]

            // Resolution Dropdown
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ResolutionLabel", "Resolution:"))
                ]

                + SHorizontalBox::Slot()
                .Padding(10, 0, 0, 0)
                .AutoWidth()
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&ResolutionOptions)
                    .OnSelectionChanged(this, &SComfyUIPanel::OnResolutionChanged)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
                    {
                        return SNew(STextBlock).Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(SelectedResolution)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return SelectedResolution.IsValid() ?
                                FText::FromString(*SelectedResolution) :
                                FText::FromString(TEXT("Select..."));
                        })
                    ]
                ]
            ]

            // Generate Button
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 15, 0, 10)
            [
                SNew(SButton)
                .Text(LOCTEXT("GenerateButton", "Generate Image"))
                .OnClicked(this, &SComfyUIPanel::OnGenerateClicked)
                .HAlign(HAlign_Center)
            ]

            // Status Text
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 10)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return FText::FromString(StatusText); })
                .ColorAndOpacity_Lambda([this]()
                {
                    if (StatusText.Contains(TEXT("Error")))
                        return FLinearColor::Red;
                    if (StatusText.Contains(TEXT("Generating")) || StatusText.Contains(TEXT("Waiting")))
                        return FLinearColor::Yellow;
                    if (StatusText.Contains(TEXT("Complete")))
                        return FLinearColor::Green;
                    return FLinearColor::White;
                })
            ]

            // Image Preview
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0, 10)
            [
                SAssignNew(PreviewImage, SImage)
                .Image(FAppStyle::GetBrush("Checkerboard"))
            ]
        ]
    ];
}

// ============================================================================
// UI Callbacks
// ============================================================================

void SComfyUIPanel::OnPromptTextChanged(const FText& NewText)
{
    PromptText = NewText.ToString();
}

void SComfyUIPanel::OnNegativePromptTextChanged(const FText& NewText)
{
    NegativePromptText = NewText.ToString();
}

void SComfyUIPanel::OnResolutionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
    SelectedResolution = NewSelection;
}

FReply SComfyUIPanel::OnGenerateClicked()
{
    if (PromptText.IsEmpty())
    {
        UpdateStatus(TEXT("Error: Please enter a prompt"));
        return FReply::Handled();
    }

    UpdateStatus(TEXT("Checking ComfyUI..."));

    // Check if ComfyUI is ready via direct HTTP
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");
    Request->SetURL(BaseUrl + TEXT("/system_stats"));
    Request->SetVerb(TEXT("GET"));

    Request->OnProcessRequestComplete().BindLambda(
        [this, BaseUrl](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
    {
        if (!bSucceeded || !Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
        {
            UpdateStatus(TEXT("Error: ComfyUI not ready"));
            return;
        }

        // Build workflow
        int32 Width, Height;
        GetResolutionFromString(*SelectedResolution, Width, Height);

        FComfyUIFlux2WorkflowParams Params;
        Params.PositivePrompt = PromptText;
        Params.NegativePrompt = NegativePromptText;
        Params.Width = Width;
        Params.Height = Height;
        Params.FilenamePrefix = CurrentFilenamePrefix;

        FString WorkflowJson = UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(Params);

        UpdateStatus(TEXT("Submitting workflow..."));

        // Build prompt wrapper
        TSharedPtr<FJsonObject> PromptObject;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorkflowJson);
        FJsonSerializer::Deserialize(Reader, PromptObject);

        TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
        Wrapper->SetObjectField(TEXT("prompt"), PromptObject);

        FString RequestBody;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
        FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);

        // Submit workflow via direct HTTP
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> SubmitRequest = FHttpModule::Get().CreateRequest();
        SubmitRequest->SetURL(BaseUrl + TEXT("/prompt"));
        SubmitRequest->SetVerb(TEXT("POST"));
        SubmitRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        SubmitRequest->SetContentAsString(RequestBody);

        SubmitRequest->OnProcessRequestComplete().BindLambda(
            [this](FHttpRequestPtr, FHttpResponsePtr SubmitResponse, bool bSubmitSucceeded)
        {
            if (!bSubmitSucceeded || !SubmitResponse.IsValid() || !EHttpResponseCodes::IsOk(SubmitResponse->GetResponseCode()))
            {
                UpdateStatus(TEXT("Error: Failed to submit workflow"));
                return;
            }

            // Parse prompt_id
            TSharedPtr<FJsonObject> JsonResponse;
            const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(SubmitResponse->GetContentAsString());
            if (FJsonSerializer::Deserialize(JsonReader, JsonResponse) && JsonResponse.IsValid())
            {
                CurrentPromptId = JsonResponse->GetStringField(TEXT("prompt_id"));
            }

            UpdateStatus(TEXT("Generating image..."));

            // Watch for completion via WebSocket
            FComfyUIWorkflowCompleteDelegateNative CompleteDelegate;
            CompleteDelegate.BindRaw(this, &SComfyUIPanel::OnGenerationComplete);

            if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
            {
                TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
                if (WSHandler.IsValid())
                {
                    WSHandler->WatchPrompt(CurrentPromptId, CompleteDelegate);
                }
            }
        });

        SubmitRequest->ProcessRequest();
    });

    Request->ProcessRequest();

    return FReply::Handled();
}

// ============================================================================
// Generation Callbacks
// ============================================================================

void SComfyUIPanel::OnWorkflowSubmitted(bool bSuccess, const FString& ResponseJson, const FString& PromptId)
{
    if (!bSuccess)
    {
        UpdateStatus(TEXT("Error: Failed to submit workflow"));
        return;
    }

    CurrentPromptId = PromptId;
    UpdateStatus(TEXT("Generating image..."));

    FComfyUIWorkflowCompleteDelegateNative CompleteDelegate;
    CompleteDelegate.BindRaw(this, &SComfyUIPanel::OnGenerationComplete);

    if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
    {
        TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
        if (WSHandler.IsValid())
        {
            WSHandler->WatchPrompt(PromptId, CompleteDelegate);
        }
    }
}

void SComfyUIPanel::OnGenerationComplete(bool bSuccess, const FString& PromptId)
{
    if (!bSuccess)
    {
        UpdateStatus(TEXT("Error: Generation failed"));
        return;
    }

    UpdateStatus(TEXT("Loading image..."));

    // Get latest image
    FString ImagePath = UComfyUIBlueprintLibrary::GetLatestOutputImage(CurrentFilenamePrefix);

    if (ImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: Could not find generated image"));
        return;
    }

    // Import as asset
    FString AssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(TEXT("/Game/GeneratedTextures"), TEXT("Generated"));
    UTexture2D* Texture = UComfyUIBlueprintLibrary::ImportImageAsAsset(ImagePath, AssetPath);

    if (Texture)
    {
        UpdateStatus(FString::Printf(TEXT("Complete! Saved to: %s"), *AssetPath));
        LoadAndDisplayImage(ImagePath);
    }
    else
    {
        UpdateStatus(TEXT("Error: Failed to import image"));
    }
}

// ============================================================================
// Helpers
// ============================================================================

void SComfyUIPanel::UpdateStatus(const FString& Status)
{
    StatusText = Status;
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: %s"), *Status);
}

void SComfyUIPanel::LoadAndDisplayImage(const FString& FilePath)
{
    UTexture2D* Texture = UComfyUIBlueprintLibrary::LoadImageFromFile(FilePath);
    if (!Texture)
    {
        return;
    }

    ImageBrush = MakeShared<FSlateBrush>();
    ImageBrush->SetResourceObject(Texture);
    ImageBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());

    if (PreviewImage.IsValid())
    {
        PreviewImage->SetImage(ImageBrush.Get());
    }
}

void SComfyUIPanel::GetResolutionFromString(const FString& ResString, int32& OutWidth, int32& OutHeight)
{
    FString WidthStr, HeightStr;
    if (ResString.Split(TEXT("x"), &WidthStr, &HeightStr))
    {
        OutWidth = FCString::Atoi(*WidthStr);
        OutHeight = FCString::Atoi(*HeightStr);
    }
    else
    {
        OutWidth = 1920;
        OutHeight = 1080;
    }
}

#undef LOCTEXT_NAMESPACE
