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
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "SComfyUIPanel"

void SComfyUIPanel::Construct(const FArguments& InArgs)
{
    // Initialize width options
    WidthOptions.Add(MakeShared<FString>(TEXT("512")));
    WidthOptions.Add(MakeShared<FString>(TEXT("768")));
    WidthOptions.Add(MakeShared<FString>(TEXT("1024")));
    WidthOptions.Add(MakeShared<FString>(TEXT("1280")));
    WidthOptions.Add(MakeShared<FString>(TEXT("1920")));
    WidthOptions.Add(MakeShared<FString>(TEXT("Custom")));
    SelectedWidth = WidthOptions[2]; // Default to 1024
    
    // Initialize height options
    HeightOptions.Add(MakeShared<FString>(TEXT("512")));
    HeightOptions.Add(MakeShared<FString>(TEXT("768")));
    HeightOptions.Add(MakeShared<FString>(TEXT("1024")));
    HeightOptions.Add(MakeShared<FString>(TEXT("720")));
    HeightOptions.Add(MakeShared<FString>(TEXT("1080")));
    HeightOptions.Add(MakeShared<FString>(TEXT("Custom")));
    SelectedHeight = HeightOptions[2]; // Default to 1024
    
    StatusText = TEXT("Offline");
    CurrentFilenamePrefix = TEXT("UE_Editor");

    WeakThis = SharedThis(this);
    
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

            // Width
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(LOCTEXT("WidthLabel", "Width: "))
                ]
                + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&WidthOptions)
                    .OnSelectionChanged(this, &SComfyUIPanel::OnWidthChanged)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                        return SNew(STextBlock).Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(SelectedWidth)
                    [
                        SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(*SelectedWidth); })
                    ]
                ]
                + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                [
                    SNew(SNumericEntryBox<int32>)
                    .Visibility_Lambda([this]() { 
                        return (*SelectedWidth == TEXT("Custom")) ? EVisibility::Visible : EVisibility::Collapsed; 
                    })
                    .Value_Lambda([this]() { return TOptional<int32>(CustomWidth); })
                    .OnValueChanged(this, &SComfyUIPanel::OnCustomWidthChanged)
                    .MinValue(64)
                    .MaxValue(8192)
                    .MinDesiredValueWidth(100)
                ]
            ]
            
            // Height
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(LOCTEXT("HeightLabel", "Height: "))
                ]
                + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&HeightOptions)
                    .OnSelectionChanged(this, &SComfyUIPanel::OnHeightChanged)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                        return SNew(STextBlock).Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(SelectedHeight)
                    [
                        SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(*SelectedHeight); })
                    ]
                ]
                + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                [
                    SNew(SNumericEntryBox<int32>)
                    .Visibility_Lambda([this]() { 
                        return (*SelectedHeight == TEXT("Custom")) ? EVisibility::Visible : EVisibility::Collapsed; 
                    })
                    .Value_Lambda([this]() { return TOptional<int32>(CustomHeight); })
                    .OnValueChanged(this, &SComfyUIPanel::OnCustomHeightChanged)
                    .MinValue(64)
                    .MaxValue(8192)
                    .MinDesiredValueWidth(100)
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
            // --- IMPORT BUTTON ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 10, 0, 10)
            [
                SNew(SButton)
                .Text(LOCTEXT("ImportButton", "Import to Project"))
                .OnClicked(this, &SComfyUIPanel::OnImportClicked)
                .HAlign(HAlign_Center)
                .IsEnabled_Lambda([this]() { return !CurrentPreviewImagePath.IsEmpty(); })  // Only enabled when preview exists
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
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride_Lambda([this]() -> FOptionalSize
                {
                    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
                    {
                        const FVector2D ImageSize = ImageBrush->ImageSize;
                        if (ImageSize.X > 0 && ImageSize.Y > 0)
                        {
                            const float MaxWidth = 800.0f;
                            const float MaxHeight = 600.0f;
                            
                            float Scale = FMath::Min(MaxWidth / ImageSize.X, MaxHeight / ImageSize.Y);
                            Scale = FMath::Min(Scale, 1.0f);
                            
                            return FOptionalSize(ImageSize.X * Scale);
                        }
                    }
                    return FOptionalSize();
                })
                .HeightOverride_Lambda([this]() -> FOptionalSize
                {
                    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
                    {
                        const FVector2D ImageSize = ImageBrush->ImageSize;
                        if (ImageSize.X > 0 && ImageSize.Y > 0)
                        {
                            const float MaxWidth = 800.0f;
                            const float MaxHeight = 600.0f;
                            
                            float Scale = FMath::Min(MaxWidth / ImageSize.X, MaxHeight / ImageSize.Y);
                            Scale = FMath::Min(Scale, 1.0f);
                            
                            return FOptionalSize(ImageSize.Y * Scale);
                        }
                    }
                    return FOptionalSize();
                })
                [
                    SAssignNew(PreviewImage, SImage)
                ]
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
    Request->SetTimeout(3.0f); // Fast timeout

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

    
    // Get width and height from dropdowns/custom inputs
    int32 Width = (*SelectedWidth == TEXT("Custom")) ? CustomWidth : FCString::Atoi(**SelectedWidth);
    int32 Height = (*SelectedHeight == TEXT("Custom")) ? CustomHeight : FCString::Atoi(**SelectedHeight);

    FComfyUIFlux2WorkflowParams Params;
    Params.PositivePrompt = PromptText;
    Params.NegativePrompt = NegativePromptText;
    Params.Width = Width;
    Params.Height = Height;
    Params.FilenamePrefix = CurrentFilenamePrefix;

    int32 UniqueSeed = FMath::Abs(FDateTime::Now().GetTicks() % MAX_int32);
    Params.Seed = UniqueSeed;

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: Set Params.Seed to: %d"), Params.Seed);

    // Build the JSON
    FString WorkflowJson = UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(Params);

    TSharedPtr<FJsonObject> PromptObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorkflowJson);
    FJsonSerializer::Deserialize(Reader, PromptObject);

    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetObjectField(TEXT("prompt"), PromptObject);

    FString RequestBody;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> SubmitRequest = FHttpModule::Get().CreateRequest();
    SubmitRequest->SetURL(BaseUrl + TEXT("/prompt"));
    SubmitRequest->SetVerb(TEXT("POST"));
    SubmitRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    SubmitRequest->SetContentAsString(RequestBody);

    // Capture BaseUrl in the lambda
    SubmitRequest->OnProcessRequestComplete().BindLambda(
        [this, BaseUrl](FHttpRequestPtr, FHttpResponsePtr SubmitResponse, bool bSubmitSucceeded)
    {
        if (!bSubmitSucceeded || !SubmitResponse.IsValid() || !EHttpResponseCodes::IsOk(SubmitResponse->GetResponseCode()))
        {
            UpdateStatus(TEXT("Error: Failed to submit workflow"));
            UE_LOG(LogTemp, Error, TEXT("ComfyUI Panel: Workflow submission failed"));
            return;
        }

        // Extract Prompt ID
        TSharedPtr<FJsonObject> JsonResponse;
        const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(SubmitResponse->GetContentAsString());
        if (FJsonSerializer::Deserialize(JsonReader, JsonResponse) && JsonResponse.IsValid())
        {
            CurrentPromptId = JsonResponse->GetStringField(TEXT("prompt_id"));
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: Submitted workflow with prompt_id: %s"), *CurrentPromptId);
        }
        else
        {
            UpdateStatus(TEXT("Error: Failed to parse prompt_id"));
            UE_LOG(LogTemp, Error, TEXT("ComfyUI Panel: Could not parse prompt_id from response"));
            return;
        }

        UpdateStatus(TEXT("Generating image..."));

        // Bind WebSocket listener
        FComfyUIWorkflowCompleteDelegateNative CompleteDelegate;
        CompleteDelegate.BindSP(SharedThis(this), &SComfyUIPanel::OnGenerationComplete);

        if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
        {
            TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
            if (WSHandler.IsValid())
            {
                if (WSHandler->IsConnected())
                {
                    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: WebSocket is connected, watching prompt %s"), *CurrentPromptId);
                    WSHandler->WatchPrompt(CurrentPromptId, CompleteDelegate);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: WebSocket NOT connected, attempting to connect..."));
                    
                    // Try to connect WebSocket
                    FString WsUrl = BaseUrl.Replace(TEXT("http://"), TEXT("ws://")).Replace(TEXT("https://"), TEXT("wss://"));
                    WsUrl += TEXT("/ws");
                    WSHandler->Connect(WsUrl);
                    
                    // Register watcher anyway - it should work once connected
                    WSHandler->WatchPrompt(CurrentPromptId, CompleteDelegate);
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("ComfyUI Panel: WebSocket handler is INVALID"));
                UpdateStatus(TEXT("Error: WebSocket unavailable"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("ComfyUI Panel: Could not get ComfyUI module"));
            UpdateStatus(TEXT("Error: Module unavailable"));
        }
    });

    SubmitRequest->ProcessRequest();
}

FReply SComfyUIPanel::OnImportClicked()
{
    if (CurrentPreviewImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image to import"));
        return FReply::Handled();
    }

    // Generate unique name with timestamp
    FDateTime Now = FDateTime::Now();
    FString UniqueName = FString::Printf(TEXT("Generated_%s"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
    
    FString AssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(TEXT("/Game/GeneratedTextures"), UniqueName);
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: Importing to: %s"), *AssetPath);
    
    UTexture2D* Texture = UComfyUIBlueprintLibrary::ImportImageAsAsset(CurrentPreviewImagePath, AssetPath);

    if (Texture)
    {
        UpdateStatus(FString::Printf(TEXT("Imported to: %s"), *AssetPath));
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: Successfully imported texture to %s"), *AssetPath);
        
        // Clear preview path so user can't accidentally import twice
        CurrentPreviewImagePath.Empty();
    }
    else
    {
        UpdateStatus(TEXT("Error: Failed to import image"));
        UE_LOG(LogTemp, Error, TEXT("ComfyUI Panel: ImportImageAsAsset returned nullptr"));
    }

    return FReply::Handled();
}

void SComfyUIPanel::OnGenerationComplete(bool bSuccess, const FString& PromptId)
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: OnGenerationComplete CALLED - Success: %d, PromptId: %s"), bSuccess, *PromptId);

    if (!bSuccess)
    {
        UpdateStatus(TEXT("Error: Generation failed"));
        return;
    }

    UpdateStatus(TEXT("Loading image..."));

    // Capture weak pointer AND filename prefix for the lambda
    TWeakPtr<SComfyUIPanel> CapturedWeakThis = WeakThis;  // Copy the weak pointer
    FString FilePrefix = CurrentFilenamePrefix;

    if (GEditor)
    {
        FTimerHandle DelayTimer;
        GEditor->GetTimerManager()->SetTimer(
            DelayTimer,
            [CapturedWeakThis, FilePrefix]()  // Capture both
            {
                // Check if panel still exists
                TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                if (!Panel.IsValid())
                {
                    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: Panel destroyed, skipping image load"));
                    return;
                }

                // Get latest image
                FString ImagePath = UComfyUIBlueprintLibrary::GetLatestOutputImage(FilePrefix);
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: Latest image path: %s"), *ImagePath);

                if (ImagePath.IsEmpty())
                {
                    Panel->UpdateStatus(TEXT("Error: Could not find generated image"));
                    UE_LOG(LogTemp, Error, TEXT("ComfyUI Panel: GetLatestOutputImage returned empty path"));
                    return;
                }

                // Store the preview path for later import
                Panel->CurrentPreviewImagePath = ImagePath;
                Panel->UpdateStatus(TEXT("Preview ready! Click 'Import to Project' to save."));
                Panel->LoadAndDisplayImage(ImagePath);
            },
            0.5f,
            false
        );
    }
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



// ============================================================================
// Helpers
// ============================================================================
void SComfyUIPanel::OnWidthChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
    SelectedWidth = NewSelection;
}

void SComfyUIPanel::OnHeightChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
    SelectedHeight = NewSelection;
}

void SComfyUIPanel::OnCustomWidthChanged(int32 NewValue)
{
    CustomWidth = NewValue;
}

void SComfyUIPanel::OnCustomHeightChanged(int32 NewValue)
{
    CustomHeight = NewValue;
}

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
        UE_LOG(LogTemp, Error, TEXT("ComfyUI Panel: Failed to load texture from %s"), *FilePath);
        return;
    }

    // Keep the texture alive by adding to root
    Texture->AddToRoot();
    
    // Remove old texture from root if it exists
    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
    {
        if (UTexture2D* OldTexture = Cast<UTexture2D>(ImageBrush->GetResourceObject()))
        {
            OldTexture->RemoveFromRoot();
        }
    }

    ImageBrush = MakeShared<FSlateBrush>();
    ImageBrush->SetResourceObject(Texture);
    ImageBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    ImageBrush->DrawAs = ESlateBrushDrawType::Image;
    ImageBrush->Tiling = ESlateBrushTileType::NoTile;  // Don't tile

    if (PreviewImage.IsValid())
    {
        PreviewImage->SetImage(ImageBrush.Get());
    }
}

SComfyUIPanel::~SComfyUIPanel()
{
    // Clean up timer
    if (GEditor && ConnectionTimerHandle.IsValid())
    {
        GEditor->GetTimerManager()->ClearTimer(ConnectionTimerHandle);
    }
    
    // Remove texture from root to allow garbage collection
    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
    {
        if (UTexture2D* OldTexture = Cast<UTexture2D>(ImageBrush->GetResourceObject()))
        {
            OldTexture->RemoveFromRoot();
        }
    }
}

#undef LOCTEXT_NAMESPACE