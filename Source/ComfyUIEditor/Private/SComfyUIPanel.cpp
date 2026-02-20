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
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Selection.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Input/SCheckBox.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "CompositingElement.h"
#include "ComposurePlayerCompositingTarget.h"
#include "Kismet/GameplayStatics.h"


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

    LoadBaseMaterial();
    
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
            // --- NEW: AUTO-APPLY SECTION ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 20, 0, 10)
            [
                SNew(SVerticalBox)
                
                // Target Actors List Header
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 5)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("TargetActorsLabel", "Target Actors:"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                ]
                
                // Target actors display (locked-in list)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(10, 5, 0, 5)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() {
                        if (TargetActors.Num() == 0)
                        {
                            return FText::FromString(TEXT("(No actors added)"));
                        }
                        
                        FString ActorList;
                        int32 ValidCount = 0;
                        for (const TWeakObjectPtr<AActor>& WeakActor : TargetActors)
                        {
                            if (WeakActor.IsValid())
                            {
                                if (ValidCount > 0) ActorList += TEXT("\n");
                                ActorList += FString::Printf(TEXT("• %s"), *WeakActor->GetActorLabel());
                                ValidCount++;
                            }
                        }
                        
                        if (ValidCount == 0)
                        {
                            return FText::FromString(TEXT("(No valid actors)"));
                        }
                        
                        return FText::FromString(ActorList);
                    })
                    .ColorAndOpacity_Lambda([this]() {
                        return TargetActors.Num() > 0 ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f);
                    })
                ]
                
                // Buttons: Add Selected & Clear All
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 5, 0, 0)
                [
                    SNew(SHorizontalBox)
                    
                    // Add Selected button
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("AddSelectedButton", "+ Add Selected"))
                        .OnClicked(this, &SComfyUIPanel::OnAddSelectedClicked)
                        .IsEnabled_Lambda([this]() {
                            return GetSelectedActors().Num() > 0;
                        })
                        .ToolTipText(LOCTEXT("AddSelectedTooltip", "Add currently selected actors to the target list"))
                    ]
                    
                    // Clear All button
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(10, 0, 0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("ClearAllButton", "Clear All"))
                        .OnClicked(this, &SComfyUIPanel::OnClearAllClicked)
                        .IsEnabled_Lambda([this]() {
                            return TargetActors.Num() > 0;
                        })
                        .ToolTipText(LOCTEXT("ClearAllTooltip", "Remove all actors from the target list"))
                    ]
                ]
                
                // Auto-apply checkbox
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 10, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SCheckBox)
                        .IsChecked_Lambda([this]() { 
                            return bAutoApplyEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; 
                        })
                        .OnCheckStateChanged(this, &SComfyUIPanel::OnAutoApplyCheckChanged)
                        .IsEnabled_Lambda([this]() {
                            return TargetActors.Num() > 0;
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .Padding(5, 0, 0, 0)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("AutoApplyLabel", "Auto-apply to target actors on generation"))
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

            // --- ACTION BUTTONS ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 10, 0, 10)
            [
                SNew(SHorizontalBox)
                
                // Apply to Actors button
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ApplyToActorsButton", "Apply to Actors"))
                    .OnClicked(this, &SComfyUIPanel::OnApplyToActorsClicked)
                    .IsEnabled_Lambda([this]() { 
                        return !CurrentPreviewImagePath.IsEmpty() && TargetActors.Num() > 0; 
                    })
                    .ToolTipText(LOCTEXT("ApplyToActorsTooltip", "Apply texture to target actors (temporary)"))
                ]
                
                // Apply to Composure button - NEW!
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(10, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ApplyToComposureButton", "Apply to Composure"))
                    .OnClicked(this, &SComfyUIPanel::OnApplyToComposureClicked)
                    .IsEnabled_Lambda([this]() { 
                        return !CurrentPreviewImagePath.IsEmpty(); 
                    })
                    .ToolTipText(LOCTEXT("ApplyToComposureTooltip", "Apply texture to Composure plate layers"))
                ]
                
                // Save to Project button
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(10, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("SaveToProjectButton", "Save to Project"))
                    .OnClicked(this, &SComfyUIPanel::OnImportClicked)
                    .IsEnabled_Lambda([this]() { 
                        return !CurrentPreviewImagePath.IsEmpty(); 
                    })
                    .ToolTipText_Lambda([this]() {
                        if (TargetActors.Num() > 0)
                        {
                            return LOCTEXT("SaveWithMaterialsTooltip", "Save texture and materials (permanent)");
                        }
                        return LOCTEXT("SaveTextureOnlyTooltip", "Save texture to project");
                    })
                ]
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
        UpdateStatus(TEXT("Error: No preview image available"));
        return FReply::Handled();
    }
    
    
    // Import texture as permanent asset
    FDateTime Now = FDateTime::Now();
    FString TextureName = FString::Printf(TEXT("T_Generated_%s"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
    FString TextureAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(TEXT("/Game/GeneratedTextures"), TextureName);
    
    UTexture2D* PermanentTexture = UComfyUIBlueprintLibrary::ImportImageAsAsset(CurrentPreviewImagePath, TextureAssetPath);
    if (!PermanentTexture)
    {
        UpdateStatus(TEXT("Error: Failed to create texture asset"));
        return FReply::Handled();
    }
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Created permanent texture: %s"), *TextureAssetPath);
    
    // If we have target actors, also create materials
    if (TargetActors.Num() > 0 && BaseMaterial)
    {
        int32 SuccessCount = 0;
        
        for (const TWeakObjectPtr<AActor>& WeakActor : TargetActors)
        {
            if (!WeakActor.IsValid())
            {
                continue;
            }
            
            AActor* Actor = WeakActor.Get();
            UMeshComponent* MeshComp = GetMeshComponentFromActor(Actor);
            
            if (!MeshComp)
            {
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Actor '%s' no longer has mesh component, skipping"), *Actor->GetActorLabel());
                continue;
            }
            
            // Generate unique material name
            FString MaterialName = FString::Printf(TEXT("MI_%s_%s"), 
                *Actor->GetActorLabel().Replace(TEXT(" "), TEXT("_")), 
                *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
            FString MaterialAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(TEXT("/Game/GeneratedMaterials"), MaterialName);
            
            // Create the material instance asset
            UPackage* Package = CreatePackage(*MaterialAssetPath);
            UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(
                Package, 
                FName(*FPaths::GetBaseFilename(MaterialAssetPath)), 
                RF_Public | RF_Standalone
            );
            
            if (!MaterialInstance)
            {
                UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to create material instance for actor '%s'"), *Actor->GetActorLabel());
                continue;
            }
            
            // Set parent material
            MaterialInstance->SetParentEditorOnly(BaseMaterial);
            
            // Set texture parameter
            MaterialInstance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColorTexture")), PermanentTexture);
            
            // Mark as modified and save
            MaterialInstance->MarkPackageDirty();
            MaterialInstance->PostEditChange();
            
            // Notify asset registry
            FAssetRegistryModule::AssetCreated(MaterialInstance);
            
            // Apply the permanent material to the mesh
            MeshComp->SetMaterial(0, MaterialInstance);
            
            // Remove from dynamic material map since we're now using permanent material
            ActorMaterialMap.Remove(WeakActor);
            
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Created permanent material for '%s': %s"), *Actor->GetActorLabel(), *MaterialAssetPath);
            SuccessCount++;
        }
        
        if (SuccessCount > 0)
        {
            UpdateStatus(FString::Printf(TEXT("Saved texture and %d material(s) to project"), SuccessCount));
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Saved texture and %d material(s)"), SuccessCount);
        }
        else
        {
            UpdateStatus(FString::Printf(TEXT("Saved texture to: %s (no materials created)"), *TextureAssetPath));
        }
    }
    else
    {
        // No target actors, just texture
        UpdateStatus(FString::Printf(TEXT("Saved texture to: %s"), *TextureAssetPath));
        
        LastImportedImagePath.Empty();
        LastImportedTexture.Reset(); 
    }
    
    // Clear preview path so user can't accidentally import twice
    CurrentPreviewImagePath.Empty();
    
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

    LastImportedImagePath.Empty();
    LastImportedTexture.Reset(); 

    UpdateStatus(TEXT("Loading image..."));

    // Capture weak pointer AND filename prefix for the lambda
    TWeakPtr<SComfyUIPanel> CapturedWeakThis = WeakThis;  // Copy the weak pointer
    FString FilePrefix = CurrentFilenamePrefix;
    bool bShouldAutoApply = bAutoApplyEnabled;

    if (GEditor)
    {
        FTimerHandle DelayTimer;
        GEditor->GetTimerManager()->SetTimer(
            DelayTimer,
            [CapturedWeakThis, FilePrefix, bShouldAutoApply]()  // Capture both
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

                UTexture2D* Texture = UComfyUIBlueprintLibrary::LoadImageFromFile(ImagePath);

                if (!Texture)
                {
                    Panel->UpdateStatus(TEXT("Error: Could not load image"));
                    return;
                }

                if (bShouldAutoApply && Panel->TargetActors.Num() > 0)
                {
                    Panel->ApplyMaterialToTargetActors(Texture);
                }
                else
                {
                    Panel->UpdateStatus(TEXT("Preview ready! Click 'Import to Project' to save."));
                }
                
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

TArray<AActor*> SComfyUIPanel::GetSelectedActors()
{
    TArray<AActor*> SelectedActors;
    
    if (!GEditor)
    {
        return SelectedActors;
    }
    
    USelection* Selection = GEditor->GetSelectedActors();
    if (!Selection)
    {
        return SelectedActors;
    }
    
    // Get all selected actors
    for (FSelectionIterator It(*Selection); It; ++It)
    {
        if (AActor* Actor = Cast<AActor>(*It))
        {
            SelectedActors.Add(Actor);
        }
    }
    
    return SelectedActors;
}
void SComfyUIPanel::OnAutoApplyCheckChanged(ECheckBoxState NewState)
{
    bAutoApplyEnabled = (NewState == ECheckBoxState::Checked);
    
    if (bAutoApplyEnabled)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Auto-apply enabled"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Auto-apply disabled"));
    }
}

FReply SComfyUIPanel::OnAddSelectedClicked()
{
    AddSelectedActorsToList();
    return FReply::Handled();
}

FReply SComfyUIPanel::OnClearAllClicked()
{
    ClearTargetActors();
    return FReply::Handled();
}

UMeshComponent* SComfyUIPanel::GetMeshComponentFromActor(AActor* Actor)
{
    if (!Actor)
    {
        return nullptr;
    }
    
    // Try to find StaticMeshComponent first (most common)
    if (UStaticMeshComponent* StaticMesh = Actor->FindComponentByClass<UStaticMeshComponent>())
    {
        return StaticMesh;
    }
    
    // Try SkeletalMeshComponent (for characters, animated meshes)
    if (USkeletalMeshComponent* SkeletalMesh = Actor->FindComponentByClass<USkeletalMeshComponent>())
    {
        return SkeletalMesh;
    }
    
    // No mesh component found
    return nullptr;
}

bool SComfyUIPanel::IsActorValidForMaterial(AActor* Actor)
{
    if (!Actor)
    {
        return false;
    }
    
    return GetMeshComponentFromActor(Actor) != nullptr;
}
void SComfyUIPanel::ApplyMaterialToTargetActors(UTexture2D* Texture)
{
    if (!Texture)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Cannot apply null texture"));
        return;
    }
    
    if (!BaseMaterial)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Base material not loaded"));
        return;
    }
    
    int32 AppliedCount = 0;
    int32 FailedCount = 0;
    
    // Clean up invalid actor entries
    TArray<TWeakObjectPtr<AActor>> InvalidActors;
    for (auto& Pair : ActorMaterialMap)
    {
        if (!Pair.Key.IsValid())
        {
            InvalidActors.Add(Pair.Key);
        }
    }
    for (const TWeakObjectPtr<AActor>& InvalidActor : InvalidActors)
    {
        ActorMaterialMap.Remove(InvalidActor);
    }
    
    // Apply material to each target actor
    for (const TWeakObjectPtr<AActor>& WeakActor : TargetActors)
    {
        if (!WeakActor.IsValid())
        {
            continue;
        }
        
        AActor* Actor = WeakActor.Get();
        UMeshComponent* MeshComp = GetMeshComponentFromActor(Actor);
        
        if (!MeshComp)
        {
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Actor '%s' no longer has mesh component, skipping"), *Actor->GetActorLabel());
            FailedCount++;
            continue;
        }
        
        // Get or create material instance for this actor
        UMaterialInstanceDynamic* DynMaterial = nullptr;
        
        if (ActorMaterialMap.Contains(WeakActor))
        {
            // Reuse existing material
            DynMaterial = ActorMaterialMap[WeakActor];
            UE_LOG(LogTemp, Log, TEXT("ComfyUI: Reusing material for actor '%s'"), *Actor->GetActorLabel());
        }
        else
        {
            // Create new material instance for this actor
            DynMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, MeshComp);
            ActorMaterialMap.Add(WeakActor, DynMaterial);
            
            // Apply material to slot 0
            MeshComp->SetMaterial(0, DynMaterial);
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Created new material for actor '%s'"), *Actor->GetActorLabel());
        }
        
        if (DynMaterial)
        {
            // Update texture parameter
            DynMaterial->SetTextureParameterValue(TEXT("BaseColorTexture"), Texture);
            AppliedCount++;
            UE_LOG(LogTemp, Log, TEXT("ComfyUI: Applied texture to actor '%s'"), *Actor->GetActorLabel());
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to create material for actor '%s'"), *Actor->GetActorLabel());
            FailedCount++;
        }
    }
    
    if (AppliedCount > 0)
    {
        UpdateStatus(FString::Printf(TEXT("Applied material to %d actor(s)"), AppliedCount));
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Applied material to %d actor(s), %d failed"), AppliedCount, FailedCount);
    }
    else
    {
        UpdateStatus(TEXT("Failed to apply materials"));
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to apply materials to any actors"));
    }
}

void SComfyUIPanel::AddSelectedActorsToList()
{
    TArray<AActor*> SelectedActors = GetSelectedActors();
    
    if (SelectedActors.Num() == 0)
    {
        UpdateStatus(TEXT("No actors selected"));
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: No actors selected"));
        return;
    }
    
    int32 AddedCount = 0;
    int32 InvalidCount = 0;
    TArray<FString> InvalidActorNames;
    
    for (AActor* Actor : SelectedActors)
    {
        if (!Actor)
        {
            continue;
        }
        
        // Validate actor has mesh component
        if (!IsActorValidForMaterial(Actor))
        {
            InvalidCount++;
            InvalidActorNames.Add(Actor->GetActorLabel());
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Skipping actor '%s' - no mesh component found"), *Actor->GetActorLabel());
            continue;  // ← IMPORTANT: Skip this actor, don't add it
        }
        
        // Check if already in list
        bool bAlreadyAdded = false;
        for (const TWeakObjectPtr<AActor>& ExistingActor : TargetActors)
        {
            if (ExistingActor.IsValid() && ExistingActor.Get() == Actor)
            {
                bAlreadyAdded = true;
                break;
            }
        }
        
        if (!bAlreadyAdded)
        {
            TargetActors.Add(Actor);  // ← ONLY log should be here
            AddedCount++;
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Added actor to list: %s"), *Actor->GetActorLabel());
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("ComfyUI: Actor '%s' already in list, skipping"), *Actor->GetActorLabel());
        }
    }
    
    // Update status with results
    if (AddedCount > 0 && InvalidCount == 0)
    {
        UpdateStatus(FString::Printf(TEXT("Added %d actor(s) to target list"), AddedCount));
    }
    else if (AddedCount > 0 && InvalidCount > 0)
    {
        UpdateStatus(FString::Printf(TEXT("Added %d actor(s), skipped %d (no mesh component)"), AddedCount, InvalidCount));
    }
    else if (InvalidCount > 0)
    {
        FString InvalidNames = FString::Join(InvalidActorNames, TEXT(", "));
        UpdateStatus(FString::Printf(TEXT("Cannot add: %s - no mesh component found"), *InvalidNames));
    }
    else
    {
        UpdateStatus(TEXT("Selected actor(s) already in list"));
    }
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Added %d actor(s) to target list. Total: %d. Skipped %d invalid."), AddedCount, TargetActors.Num(), InvalidCount);
}

void SComfyUIPanel::ClearTargetActors()
{
    int32 Count = TargetActors.Num();
    TargetActors.Empty();
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Cleared target actor list (%d actors removed)"), Count);
}

void SComfyUIPanel::RemoveActorFromList(AActor* Actor)
{
    for (int32 i = TargetActors.Num() - 1; i >= 0; i--)
    {
        if (TargetActors[i].IsValid() && TargetActors[i].Get() == Actor)
        {
            TargetActors.RemoveAt(i);
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Removed actor from list: %s"), *Actor->GetActorLabel());
            break;
        }
    }
}

void SComfyUIPanel::ApplyTextureToComposurePlates(UTexture2D* Texture)
{
    if (!Texture)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Cannot apply null texture to Composure"));
        return;
    }
    
    if (!GEditor || !GEditor->GetEditorWorldContext().World())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: No editor world available"));
        return;
    }
    
    UWorld* World = GEditor->GetEditorWorldContext().World();
    
    // Find all actors
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    
    int32 PlatesUpdated = 0;
    
    for (AActor* Actor : AllActors)
    {
        FString ClassName = Actor->GetClass()->GetName();
        
        // Look for CompositeActor
        if (ClassName == TEXT("CompositeActor"))
        {
            UE_LOG(LogTemp, Log, TEXT("ComfyUI: Found CompositeActor: %s"), *Actor->GetActorLabel());
            
            // Access CompositeLayers property
            UClass* ActorClass = Actor->GetClass();
            FProperty* LayersProp = ActorClass->FindPropertyByName(TEXT("CompositeLayers"));
            
            if (!LayersProp)
            {
                continue;
            }
            
            FArrayProperty* ArrayProp = CastField<FArrayProperty>(LayersProp);
            if (!ArrayProp)
            {
                continue;
            }
            
            FScriptArrayHelper ArrayHelper(ArrayProp, LayersProp->ContainerPtrToValuePtr<void>(Actor));
            int32 NumLayers = ArrayHelper.Num();
            
            for (int32 i = 0; i < NumLayers; i++)
            {
                UObject* LayerObj = *reinterpret_cast<UObject**>(ArrayHelper.GetRawPtr(i));
                if (!LayerObj)
                {
                    continue;
                }
                
                FString LayerClassName = LayerObj->GetClass()->GetName();
                
                // Check if it's a plate layer
                if (LayerClassName.Contains(TEXT("Plate")))
                {
                    FString LayerName = LayerObj->GetName();
                    UE_LOG(LogTemp, Log, TEXT("ComfyUI: Found plate layer: %s"), *LayerName);
                    
                    // Find and set Texture property
                    FProperty* TextureProp = LayerObj->GetClass()->FindPropertyByName(TEXT("Texture"));
                    if (TextureProp)
                    {
                        // Set the texture
                        FObjectProperty* ObjProp = CastField<FObjectProperty>(TextureProp);
                        if (ObjProp)
                        {
                            ObjProp->SetObjectPropertyValue(TextureProp->ContainerPtrToValuePtr<void>(LayerObj), Texture);
                            
                            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Set texture on plate layer '%s' to '%s'"), *LayerName, *Texture->GetName());
                            
                            // CRITICAL: Notify the layer object that a property changed
                            FPropertyChangedEvent PropertyEvent(TextureProp);
                            LayerObj->PostEditChangeProperty(PropertyEvent);
                            
                            // Mark layer as modified
                            LayerObj->Modify();
                            LayerObj->MarkPackageDirty();
                            
                            PlatesUpdated++;
                        }
                    }
                }
            }
            
            // CRITICAL: Mark the CompositeActor as modified and trigger update
            Actor->Modify();
            Actor->MarkPackageDirty();
            
            // Try to call any refresh/update functions on the CompositeActor
            // Look for common update function names
            UFunction* RefreshFunc = Actor->FindFunction(FName(TEXT("RefreshComposite")));
            if (!RefreshFunc)
            {
                RefreshFunc = Actor->FindFunction(FName(TEXT("Refresh")));
            }
            if (!RefreshFunc)
            {
                RefreshFunc = Actor->FindFunction(FName(TEXT("Update")));
            }
            if (!RefreshFunc)
            {
                RefreshFunc = Actor->FindFunction(FName(TEXT("RecomposeNow")));
            }
            
            if (RefreshFunc)
            {
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Calling refresh function: %s"), *RefreshFunc->GetName());
                Actor->ProcessEvent(RefreshFunc, nullptr);
            }
            else
            {
                UE_LOG(LogTemp, Log, TEXT("ComfyUI: No refresh function found, trying PostEditChangeProperty"));
                
                // Trigger PostEditChangeProperty on the actor itself
                FPropertyChangedEvent ActorPropertyEvent(LayersProp);
                Actor->PostEditChangeProperty(ActorPropertyEvent);
            }
        }
    }
    
    // Force viewport refresh
    if (GEditor)
    {
        GEditor->RedrawAllViewports();
    }
    
    if (PlatesUpdated > 0)
    {
        UpdateStatus(FString::Printf(TEXT("Applied texture to %d Composure plate(s)"), PlatesUpdated));
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Successfully updated %d Composure plate layer(s)"), PlatesUpdated);
    }
    else
    {
        UpdateStatus(TEXT("No Composure plate layers found"));
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: No Composure plate layers found to update"));
    }
}

FReply SComfyUIPanel::OnApplyToActorsClicked()
{
    if (CurrentPreviewImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image available"));
        return FReply::Handled();
    }
    
    if (TargetActors.Num() == 0)
    {
        UpdateStatus(TEXT("Error: No target actors in list"));
        return FReply::Handled();
    }
    
    // Load texture from preview
    UTexture2D* Texture = UComfyUIBlueprintLibrary::LoadImageFromFile(CurrentPreviewImagePath);
    if (!Texture)
    {
        UpdateStatus(TEXT("Error: Failed to load preview texture"));
        return FReply::Handled();
    }
    
    // Apply to all target actors
    ApplyMaterialToTargetActors(Texture);
    
    return FReply::Handled();
}

void SComfyUIPanel::LoadBaseMaterial()
{
    if (BaseMaterial)
    {
        return; // Already loaded
    }
    
    BaseMaterial = LoadObject<UMaterial>(nullptr, 
        TEXT("/ComfyUI/Materials/M_ComfyUI_Base.M_ComfyUI_Base"));
    
    if (BaseMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Successfully loaded base material"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: FAILED to load base material from /ComfyUI/Materials/M_ComfyUI_Base"));
    }
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
FReply SComfyUIPanel::OnApplyToComposureClicked()
{
    if (CurrentPreviewImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image available"));
        return FReply::Handled();
    }
    
    UTexture2D* TextureToApply = nullptr;
    
    // Check if we already imported this exact image
    if (LastImportedImagePath == CurrentPreviewImagePath && LastImportedTexture.IsValid())
    {
        // Reuse the existing texture
        TextureToApply = LastImportedTexture.Get();
        UE_LOG(LogTemp, Log, TEXT("ComfyUI: Reusing previously imported texture: %s"), *TextureToApply->GetPathName());
    }
    else
    {
        // Import as new permanent texture asset
        FDateTime Now = FDateTime::Now();
        FString TextureName = FString::Printf(TEXT("T_Composure_%s"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
        FString TextureAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(TEXT("/Game/GeneratedTextures"), TextureName);
        
        TextureToApply = UComfyUIBlueprintLibrary::ImportImageAsAsset(CurrentPreviewImagePath, TextureAssetPath);
        if (!TextureToApply)
        {
            UpdateStatus(TEXT("Error: Failed to import texture"));
            return FReply::Handled();
        }
        
        // Cache it
        LastImportedImagePath = CurrentPreviewImagePath;
        LastImportedTexture = TextureToApply;
        
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Imported new texture as: %s"), *TextureAssetPath);
    }
    
    // Apply texture to Composure plates
    ApplyTextureToComposurePlates(TextureToApply);
    
    return FReply::Handled();
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