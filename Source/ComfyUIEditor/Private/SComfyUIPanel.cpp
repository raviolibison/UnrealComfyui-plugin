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
#include "Editor.h" 

#define LOCTEXT_NAMESPACE "SComfyUIPanel"

void SComfyUIPanel::Construct(const FArguments& InArgs)
{
    // Initialize resolution options
    ResolutionOptions.Add(MakeShared<FString>(TEXT("512x512")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("1024x1024")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("1280x720")));
    ResolutionOptions.Add(MakeShared<FString>(TEXT("1920x1080")));
    SelectedResolution = ResolutionOptions[3]; 

    StatusText = TEXT("Offline");
    CurrentFilenamePrefix = TEXT("UE_Editor");
    
    // Initial State: Reset connection attempts so we do a single passive check
    ConnectionAttempts = 0; 
    PollComfyConnection();

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        .Padding(10.0f)
        [
            SNew(SVerticalBox)

            // --- HEADER: Server Control ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 15)
            [
                SNew(SHorizontalBox)
                
                // Start Button
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text_Lambda([this]() { 
                        return bIsComfyReady ? LOCTEXT("ServerRunning", "ComfyUI Running") : LOCTEXT("StartServer", "Start ComfyUI"); 
                    })
                    .OnClicked(this, &SComfyUIPanel::OnStartComfyClicked)
                    .IsEnabled_Lambda([this](){ return !bIsComfyReady; }) // Disable if already running
                ]

                // Status Dot
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(10, 0, 0, 0)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\u25CF"))) // Circle character
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 24))
                    .ColorAndOpacity_Lambda([this]() {
                        return bIsComfyReady ? FLinearColor::Green : FLinearColor::Red;
                    })
                    .ToolTipText_Lambda([this](){
                         return bIsComfyReady ? LOCTEXT("OnlineTooltip", "Server is Online") : LOCTEXT("OfflineTooltip", "Server is Offline");
                    })
                ]
            ]
            
            // Divider
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(SSpacer)
                .Size(FVector2D(0, 10))
            ]

            // --- PROMPT SECTION ---
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock).Text(LOCTEXT("PromptLabel", "Prompt:"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5, 0, 10)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("PromptHint", "Describe the image..."))
                .OnTextChanged(this, &SComfyUIPanel::OnPromptTextChanged)
                .MinDesiredWidth(400)
            ]

            // Negative Prompt
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock).Text(LOCTEXT("NegPromptLabel", "Negative Prompt:"))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5, 0, 10)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("NegPromptHint", "What to avoid..."))
                .OnTextChanged(this, &SComfyUIPanel::OnNegativePromptTextChanged)
            ]

            // Resolution
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(LOCTEXT("ResLabel", "Resolution: "))
                ]
                + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&ResolutionOptions)
                    .OnSelectionChanged(this, &SComfyUIPanel::OnResolutionChanged)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                        return SNew(STextBlock).Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(SelectedResolution)
                    [
                        SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(*SelectedResolution); })
                    ]
                ]
            ]

            // --- GENERATE BUTTON ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 20, 0, 10)
            [
                SNew(SButton)
                .Text(LOCTEXT("GenerateButton", "Generate Image"))
                .OnClicked(this, &SComfyUIPanel::OnGenerateClicked)
                .HAlign(HAlign_Center)
                .IsEnabled_Lambda([this]() { return bIsComfyReady; }) // <--- LOCKED UNTIL CONNECTED
            ]

            // Status Bar
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return FText::FromString(StatusText); })
                .Justification(ETextJustify::Center)
                .ColorAndOpacity_Lambda([this]() {
                    // Simple color logic
                    if (StatusText.Contains("Error") || StatusText.Contains("Offline")) return FLinearColor::Red;
                    if (StatusText.Contains("Generating") || StatusText.Contains("Waiting") || StatusText.Contains("Launching")) return FLinearColor::Yellow;
                    return FLinearColor::White;
                })
            ]

            // Preview
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0, 10)
            [
                SAssignNew(PreviewImage, SImage)
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

FReply SComfyUIPanel::OnStartComfyClicked()
{
    // 1. Launch the process
    if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
    {
        if (Module->EnsurePortableRunning())
        {
            UpdateStatus(TEXT("Launching ComfyUI..."));
            
            // 2. Start polling. We set attempts to 1 to signal "User triggered this", so we want to loop/retry.
            ConnectionAttempts = 1;
            PollComfyConnection();
        }
        else
        {
            UpdateStatus(TEXT("Error: Failed to launch ComfyUI (Check logs/paths)"));
        }
    }
    return FReply::Handled();
}

void SComfyUIPanel::PollComfyConnection()
{
    // Simply ping the server to check status
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");
    
    Request->SetURL(BaseUrl + TEXT("/system_stats"));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(1.0f); // Fast timeout

    Request->OnProcessRequestComplete().BindLambda(
        [this](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
    {
        if (bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
        {
            // SUCCESS: Connected
            bIsComfyReady = true;
            UpdateStatus(TEXT("Connected: ComfyUI is Ready"));
            return;
        }

        // FAILED: Not ready yet
        bIsComfyReady = false;

        // RETRY LOGIC:
        // If ConnectionAttempts > 0, it means the User clicked "Start", so we actively retry.
        // If ConnectionAttempts == 0, it was just the passive check on window open, so we don't loop.
        if (ConnectionAttempts > 0 && ConnectionAttempts < 30) 
        {
            ConnectionAttempts++;
            UpdateStatus(FString::Printf(TEXT("Waiting for ComfyUI... (%ds)"), ConnectionAttempts));
            
            if (GEditor)
            {
                GEditor->GetTimerManager()->SetTimer(
                    ConnectionTimerHandle,
                    FTimerDelegate::CreateRaw(this, &SComfyUIPanel::PollComfyConnection),
                    1.0f, 
                    false
                );
            }
        }
        else if (ConnectionAttempts >= 30)
        {
             UpdateStatus(TEXT("Error: Timed out connecting to ComfyUI"));
             ConnectionAttempts = 0; // Reset
        }
        else 
        {
             // Just a passive check failed (Window opened, server wasn't running)
             StatusText = TEXT("Server Offline");
        }
    });

    Request->ProcessRequest();
}

FReply SComfyUIPanel::OnGenerateClicked()
{
    StartGeneration();
    return FReply::Handled();
}

void SComfyUIPanel::StartGeneration()
{
    UpdateStatus(TEXT("Submitting workflow..."));

    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

    int32 Width, Height;
    GetResolutionFromString(*SelectedResolution, Width, Height);

    FComfyUIFlux2WorkflowParams Params;
    Params.PositivePrompt = PromptText;
    Params.NegativePrompt = NegativePromptText;
    Params.Width = Width;
    Params.Height = Height;
    Params.FilenamePrefix = CurrentFilenamePrefix;

    // Build the JSON for the workflow
    FString WorkflowJson = UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(Params);

    // Prepare JSON wrapper {"prompt": ...}
    TSharedPtr<FJsonObject> PromptObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorkflowJson);
    FJsonSerializer::Deserialize(Reader, PromptObject);

    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetObjectField(TEXT("prompt"), PromptObject);

    FString RequestBody;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);

    // Submit via HTTP
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

        // Extract Prompt ID
        TSharedPtr<FJsonObject> JsonResponse;
        const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(SubmitResponse->GetContentAsString());
        if (FJsonSerializer::Deserialize(JsonReader, JsonResponse) && JsonResponse.IsValid())
        {
            CurrentPromptId = JsonResponse->GetStringField(TEXT("prompt_id"));
        }

        UpdateStatus(TEXT("Generating image..."));

        // Bind WebSocket listener
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
}

// ============================================================================
// Generation Callbacks
// ============================================================================

void SComfyUIPanel::OnWorkflowSubmitted(bool bSuccess, const FString& ResponseJson, const FString& PromptId)
{
    // Note: This function is kept for compatibility but logic is handled inside StartGeneration lambda
    if (!bSuccess)
    {
        UpdateStatus(TEXT("Error: Failed to submit workflow"));
        return;
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