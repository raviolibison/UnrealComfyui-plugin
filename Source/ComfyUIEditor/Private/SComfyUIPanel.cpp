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
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "SComfyUIPanel"

// ============================================================================
// Construct
// ============================================================================

void SComfyUIPanel::Construct(const FArguments& InArgs)
{
    WidthOptions.Add(MakeShared<FString>(TEXT("512")));
    WidthOptions.Add(MakeShared<FString>(TEXT("768")));
    WidthOptions.Add(MakeShared<FString>(TEXT("1024")));
    WidthOptions.Add(MakeShared<FString>(TEXT("1280")));
    WidthOptions.Add(MakeShared<FString>(TEXT("1920")));
    WidthOptions.Add(MakeShared<FString>(TEXT("Custom")));
    SelectedWidth = WidthOptions[2];

    HeightOptions.Add(MakeShared<FString>(TEXT("512")));
    HeightOptions.Add(MakeShared<FString>(TEXT("768")));
    HeightOptions.Add(MakeShared<FString>(TEXT("1024")));
    HeightOptions.Add(MakeShared<FString>(TEXT("720")));
    HeightOptions.Add(MakeShared<FString>(TEXT("1080")));
    HeightOptions.Add(MakeShared<FString>(TEXT("Custom")));
    SelectedHeight = HeightOptions[2];

    StatusText = TEXT("Offline");
    WeakThis = SharedThis(this);

    LoadBaseMaterial();
    ConnectionAttempts = 0;
    PollComfyConnection();

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot()
        .Padding(10.0f)
        [
            SNew(SVerticalBox)

            // --- Server Control ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 0, 0, 15)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text_Lambda([this]() {
                        return bIsComfyReady
                            ? LOCTEXT("ServerRunning", "ComfyUI Running")
                            : LOCTEXT("StartServer", "Start ComfyUI");
                    })
                    .OnClicked(this, &SComfyUIPanel::OnStartComfyClicked)
                    .IsEnabled_Lambda([this](){ return !bIsComfyReady; })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(10, 0, 0, 0)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\u25CF")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 24))
                    .ColorAndOpacity_Lambda([this]() {
                        return bIsComfyReady ? FLinearColor::Green : FLinearColor::Red;
                    })
                    .ToolTipText_Lambda([this](){
                        return bIsComfyReady
                            ? LOCTEXT("OnlineTooltip", "Server is Online")
                            : LOCTEXT("OfflineTooltip", "Server is Offline");
                    })
                ]
            ]

            // Divider
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(SSpacer).Size(FVector2D(0, 10))
            ]

            // --- Prompts ---
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
                .Text(FText::FromString(PromptText))
                .OnTextChanged(this, &SComfyUIPanel::OnPromptTextChanged)
                .MinDesiredWidth(400)
            ]
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
                .Text(FText::FromString(NegativePromptText))
                .OnTextChanged(this, &SComfyUIPanel::OnNegativePromptTextChanged)
            ]

            // --- Resolution ---
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
                    .MinValue(64).MaxValue(8192).MinDesiredValueWidth(100)
                ]
            ]
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
                    .MinValue(64).MaxValue(8192).MinDesiredValueWidth(100)
                ]
            ]

            // --- Img2Img Strength ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(LOCTEXT("Img2ImgStrengthLabel", "Img2Img Strength: "))
                ]
                + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                [
                    SNew(SNumericEntryBox<float>)
                    .Value_Lambda([this]() { return TOptional<float>(Img2ImgStrength); })
                    .OnValueChanged_Lambda([this](float NewValue) {
                        Img2ImgStrength = FMath::Clamp(NewValue, 0.0f, 1.0f);
                    })
                    .MinValue(0.0f).MaxValue(1.0f)
                    .MinSliderValue(0.0f).MaxSliderValue(1.0f)
                    .AllowSpin(true)
                    .MinDesiredValueWidth(60)
                ]
            ]

            // --- Target Actors ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 20, 0, 10)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("TargetActorsLabel", "Target Actors:"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(10, 5, 0, 5)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() {
                        if (TargetActors.Num() == 0)
                            return FText::FromString(TEXT("(No actors added)"));
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
                        return ValidCount == 0
                            ? FText::FromString(TEXT("(No valid actors)"))
                            : FText::FromString(ActorList);
                    })
                    .ColorAndOpacity_Lambda([this]() {
                        return TargetActors.Num() > 0 ? FLinearColor::White : FLinearColor(0.5f, 0.5f, 0.5f);
                    })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("AddSelectedButton", "+ Add Selected"))
                        .OnClicked(this, &SComfyUIPanel::OnAddSelectedClicked)
                        .IsEnabled_Lambda([this]() { return GetSelectedActors().Num() > 0; })
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("ClearAllButton", "Clear All"))
                        .OnClicked(this, &SComfyUIPanel::OnClearAllClicked)
                        .IsEnabled_Lambda([this]() { return TargetActors.Num() > 0; })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SCheckBox)
                        .IsChecked_Lambda([this]() {
                            return bAutoApplyEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        })
                        .OnCheckStateChanged(this, &SComfyUIPanel::OnAutoApplyCheckChanged)
                        .IsEnabled_Lambda([this]() { return TargetActors.Num() > 0; })
                    ]
                    + SHorizontalBox::Slot().Padding(5, 0, 0, 0).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("AutoApplyLabel", "Auto-apply to target actors on generation"))
                    ]
                ]
            ]

            // --- Generate Button ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 20, 0, 10)
            [
                SNew(SButton)
                .Text(LOCTEXT("GenerateButton", "Generate Image"))
                .OnClicked(this, &SComfyUIPanel::OnGenerateClicked)
                .HAlign(HAlign_Center)
                .IsEnabled_Lambda([this]() { return bIsComfyReady; })
            ]

            // --- Action Buttons ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 10, 0, 10)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ApplyToActorsButton", "Apply to Actors"))
                    .OnClicked(this, &SComfyUIPanel::OnApplyToActorsClicked)
                    .IsEnabled_Lambda([this]() {
                        return !CurrentPreviewImagePath.IsEmpty() && TargetActors.Num() > 0;
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ApplyToComposureButton", "Apply to Composure"))
                    .OnClicked(this, &SComfyUIPanel::OnApplyToComposureClicked)
                    .IsEnabled_Lambda([this]() { return !CurrentPreviewImagePath.IsEmpty(); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Img2ImgButton", "Img2Img"))
                    .OnClicked(this, &SComfyUIPanel::OnImg2ImgClicked)
                    .IsEnabled_Lambda([this]() {
                        return !CurrentPreviewImagePath.IsEmpty() && bIsComfyReady;
                    })
                    .ToolTipText(LOCTEXT("Img2ImgTooltip", "Run img2img on the current preview image"))
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Generate360Button", "Generate 360° HDRI"))
                    .OnClicked(this, &SComfyUIPanel::OnGenerate360Clicked)
                    .IsEnabled_Lambda([this]() { return !CurrentPreviewImagePath.IsEmpty(); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("SaveToProjectButton", "Save to Project"))
                    .OnClicked(this, &SComfyUIPanel::OnImportClicked)
                    .IsEnabled_Lambda([this]() { return !CurrentPreviewImagePath.IsEmpty(); })
                ]
            ]

            // --- Status ---
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0, 5)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return FText::FromString(StatusText); })
                .Justification(ETextJustify::Center)
                .ColorAndOpacity_Lambda([this]() {
                    if (StatusText.Contains("Error") || StatusText.Contains("Offline"))
                        return FLinearColor::Red;
                    if (StatusText.Contains("Generating") || StatusText.Contains("Waiting")
                        || StatusText.Contains("Launching") || StatusText.Contains("Running"))
                        return FLinearColor::Yellow;
                    return FLinearColor::White;
                })
            ]

            // --- Preview ---
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0, 10)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride_Lambda([this]() -> FOptionalSize {
                    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
                    {
                        const FVector2D ImageSize = ImageBrush->ImageSize;
                        if (ImageSize.X > 0 && ImageSize.Y > 0)
                        {
                            float Scale = FMath::Min(FMath::Min(800.0f / ImageSize.X, 600.0f / ImageSize.Y), 1.0f);
                            return FOptionalSize(ImageSize.X * Scale);
                        }
                    }
                    return FOptionalSize();
                })
                .HeightOverride_Lambda([this]() -> FOptionalSize {
                    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
                    {
                        const FVector2D ImageSize = ImageBrush->ImageSize;
                        if (ImageSize.X > 0 && ImageSize.Y > 0)
                        {
                            float Scale = FMath::Min(FMath::Min(800.0f / ImageSize.X, 600.0f / ImageSize.Y), 1.0f);
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
// Generic Workflow System
// ============================================================================

void SComfyUIPanel::SubmitWorkflow(const FComfyWorkflowParams& Params)
{
    // Parse the workflow JSON into an object
    TSharedPtr<FJsonObject> PromptObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Params.WorkflowJson);
    if (!FJsonSerializer::Deserialize(Reader, PromptObject) || !PromptObject.IsValid())
    {
        UpdateStatus(TEXT("Error: Invalid workflow JSON"));
        return;
    }

    // Wrap it in { "prompt": { ... } }
    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetObjectField(TEXT("prompt"), PromptObject);

    FString RequestBody;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);

    // Grab base URL
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

    // POST to /prompt
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + TEXT("/prompt"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestBody);

    // Capture everything the completion handler will need
    FComfyWorkflowParams CapturedParams = Params;
    TWeakPtr<SComfyUIPanel> CapturedWeakThis = WeakThis;

    Request->OnProcessRequestComplete().BindLambda(
        [CapturedParams, CapturedWeakThis, BaseUrl](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
            if (!Panel.IsValid()) return;

            if (!bSucceeded || !Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
            {
                Panel->UpdateStatus(TEXT("Error: Failed to submit workflow"));
                return;
            }

            // Extract prompt_id
            TSharedPtr<FJsonObject> JsonResponse;
            const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
            if (!FJsonSerializer::Deserialize(JsonReader, JsonResponse) || !JsonResponse.IsValid())
            {
                Panel->UpdateStatus(TEXT("Error: Could not parse prompt_id"));
                return;
            }

            FString PromptId = JsonResponse->GetStringField(TEXT("prompt_id"));
            Panel->CurrentPromptId = PromptId;
            Panel->UpdateStatus(CapturedParams.RunningStatus);

            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Submitted workflow, prompt_id: %s"), *PromptId);

            // Register WebSocket watcher — bind to the single OnWorkflowComplete,
            // carrying CapturedParams so it knows what to do when done
            if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
            {
                TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
                if (WSHandler.IsValid())
                {
                    if (!WSHandler->IsConnected())
                    {
                        FString WsUrl = BaseUrl
                            .Replace(TEXT("http://"), TEXT("ws://"))
                            .Replace(TEXT("https://"), TEXT("wss://"))
                            + TEXT("/ws");
                        WSHandler->Connect(WsUrl);
                    }

                    FComfyUIWorkflowCompleteDelegateNative CompleteDelegate;
                    CompleteDelegate.BindLambda(
                        [CapturedWeakThis, CapturedParams](bool bSuccess, const FString& InPromptId)
                        {
                            TSharedPtr<SComfyUIPanel> InnerPanel = CapturedWeakThis.Pin();
                            if (InnerPanel.IsValid())
                            {
                                InnerPanel->OnWorkflowComplete(bSuccess, InPromptId, CapturedParams);
                            }
                        });

                    WSHandler->WatchPrompt(PromptId, CompleteDelegate);
                }
            }
        });

    Request->ProcessRequest();
}

void SComfyUIPanel::OnWorkflowComplete(bool bSuccess, const FString& PromptId, FComfyWorkflowParams Params)
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: OnWorkflowComplete - Success: %d, PromptId: %s, Prefix: %s"),
        bSuccess, *PromptId, *Params.OutputPrefix);

    if (!bSuccess)
    {
        UpdateStatus(TEXT("Error: Workflow failed"));
        return;
    }

    UpdateStatus(TEXT("Loading output image..."));

    TWeakPtr<SComfyUIPanel> CapturedWeakThis = WeakThis;

    // Small delay to let ComfyUI finish writing the file
    if (GEditor)
    {
        FTimerHandle DelayTimer;
        GEditor->GetTimerManager()->SetTimer(
            DelayTimer,
            [CapturedWeakThis, Params]()
            {
                TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                if (!Panel.IsValid()) return;

                // Find the output image
                FString ImagePath = UComfyUIBlueprintLibrary::GetLatestOutputImage(Params.OutputPrefix);
                if (ImagePath.IsEmpty())
                {
                    Panel->UpdateStatus(FString::Printf(
                        TEXT("Error: No output image found with prefix '%s'"), *Params.OutputPrefix));
                    return;
                }

                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Found output image: %s"), *ImagePath);

                // Optionally update preview
                if (Params.bUpdatePreview)
                {
                    Panel->CurrentPreviewImagePath = ImagePath;
                    Panel->LoadAndDisplayImage(ImagePath);
                }

                // Optionally auto-apply to actors
                if (Params.bAutoApply && Panel->TargetActors.Num() > 0)
                {
                    UTexture2D* Texture = UComfyUIBlueprintLibrary::LoadImageFromFile(ImagePath);
                    if (Texture)
                    {
                        Panel->ApplyMaterialToTargetActors(Texture);
                    }
                }

                // Special post-processing for 360 output
                // We detect this by prefix — keeps the struct simple without needing an enum
                if (Params.OutputPrefix == TEXT("Kontext_Upscale"))
                {
                    UTexture2D* Texture360 = UComfyUIBlueprintLibrary::ImportImageAsAsset(
                        ImagePath,
                        UComfyUIBlueprintLibrary::GenerateUniqueAssetName(
                            TEXT("/Game/GeneratedTextures"),
                            FString::Printf(TEXT("T_360_HDRI_%s"),
                                *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")))
                        )
                    );

                    if (Texture360)
                    {
                        Texture360->SetForceMipLevelsToBeResident(30.0f);
                        Texture360->WaitForStreaming();
                        Texture360->SRGB = true;
                        Texture360->UpdateResource();
                        FlushRenderingCommands();
                        Panel->Apply360ToSkyLight(Texture360);
                    }
                    else
                    {
                        Panel->UpdateStatus(TEXT("Error: Failed to import 360° texture"));
                    }
                    return;
                }

                Panel->UpdateStatus(Params.CompleteStatus);
            },
            0.5f,
            false
        );
    }
}

// ============================================================================
// Workflow Builders
// Each one builds an FComfyWorkflowParams and calls SubmitWorkflow.
// ============================================================================

void SComfyUIPanel::StartGeneration()
{
    int32 Width  = (*SelectedWidth  == TEXT("Custom")) ? CustomWidth  : FCString::Atoi(**SelectedWidth);
    int32 Height = (*SelectedHeight == TEXT("Custom")) ? CustomHeight : FCString::Atoi(**SelectedHeight);

    FComfyUIFlux2WorkflowParams FluxParams;
    FluxParams.PositivePrompt = PromptText;
    FluxParams.NegativePrompt = NegativePromptText;
    FluxParams.Width          = Width;
    FluxParams.Height         = Height;
    FluxParams.FilenamePrefix = CurrentFilenamePrefix;
    FluxParams.Seed           = FMath::Abs((int32)(FDateTime::Now().GetTicks() % MAX_int32));

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: StartGeneration seed: %d"), FluxParams.Seed);

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson    = UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(FluxParams);
    WorkflowParams.OutputPrefix    = CurrentFilenamePrefix;
    WorkflowParams.RunningStatus   = TEXT("Generating image...");
    WorkflowParams.CompleteStatus  = TEXT("Preview ready! Save to project or run Img2Img.");
    WorkflowParams.bUpdatePreview  = true;
    WorkflowParams.bAutoApply      = bAutoApplyEnabled;

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::StartImg2Img()
{
    FString Filename = FPaths::GetCleanFilename(CurrentPreviewImagePath);
    FString OutputReference = Filename + TEXT(" [output]");

    TSharedPtr<FJsonObject> WorkflowObj;
    if (!LoadWorkflowFromFile(TEXT("workflows/img2img-API.json"), WorkflowObj))
    {
        UpdateStatus(TEXT("Error: img2img workflow file not found"));
        return;
    }

    // --- Patch your workflow node IDs here ---
    // Replace "YOUR_LOAD_IMAGE_NODE_ID" with the actual node ID from your API export.

    if (TSharedPtr<FJsonObject> ImageNode = WorkflowObj->GetObjectField(TEXT("YOUR_LOAD_IMAGE_NODE_ID")))
    {
        ImageNode->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), OutputReference);
    }

    if (TSharedPtr<FJsonObject> PromptNode = WorkflowObj->GetObjectField(TEXT("YOUR_CLIP_TEXT_NODE_ID")))
    {
        PromptNode->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("text"), PromptText);
    }

    if (TSharedPtr<FJsonObject> SamplerNode = WorkflowObj->GetObjectField(TEXT("YOUR_KSAMPLER_NODE_ID")))
    {
        SamplerNode->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("denoise"), Img2ImgStrength);
        SamplerNode->GetObjectField(TEXT("inputs"))->SetNumberField(
            TEXT("noise_seed"), FMath::Abs((int32)(FDateTime::Now().GetTicks() % MAX_int32)));
    }
    // --- End patch ---

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson   = SerializeWorkflow(WorkflowObj);
    WorkflowParams.OutputPrefix   = TEXT("UE_Img2Img");
    WorkflowParams.RunningStatus  = TEXT("Running img2img...");
    WorkflowParams.CompleteStatus = TEXT("Img2Img complete! Save to project or run again.");
    WorkflowParams.bUpdatePreview = true;
    WorkflowParams.bAutoApply     = bAutoApplyEnabled;

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::Start360Generation()
{
    FString Filename = FPaths::GetCleanFilename(CurrentPreviewImagePath);
    FString OutputReference = Filename + TEXT(" [output]");

    TSharedPtr<FJsonObject> WorkflowObj;
    if (!LoadWorkflowFromFile(TEXT("workflows/360_Kontext-Small-API.json"), WorkflowObj))
    {
        UpdateStatus(TEXT("Error: 360 workflow file not found"));
        return;
    }

    // Patch image input nodes
    if (TSharedPtr<FJsonObject> Node147 = WorkflowObj->GetObjectField(TEXT("147")))
        Node147->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), OutputReference);

    if (TSharedPtr<FJsonObject> Node142 = WorkflowObj->GetObjectField(TEXT("142")))
        Node142->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), OutputReference);

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson   = SerializeWorkflow(WorkflowObj);
    WorkflowParams.OutputPrefix   = TEXT("Kontext_Upscale");
    WorkflowParams.RunningStatus  = TEXT("Generating 360° panorama...");
    WorkflowParams.CompleteStatus = TEXT("360° applied to scene lighting!");
    WorkflowParams.bUpdatePreview = false; // 360 doesn't replace the 2D preview
    WorkflowParams.bAutoApply     = false;

    SubmitWorkflow(WorkflowParams);
}

// ============================================================================
// UI Callbacks — just delegate to builders
// ============================================================================

FReply SComfyUIPanel::OnGenerateClicked()
{
    UpdateStatus(TEXT("Submitting workflow..."));
    StartGeneration();
    return FReply::Handled();
}

FReply SComfyUIPanel::OnImg2ImgClicked()
{
    if (CurrentPreviewImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image to use as input"));
        return FReply::Handled();
    }
    UpdateStatus(TEXT("Submitting img2img workflow..."));
    StartImg2Img();
    return FReply::Handled();
}

FReply SComfyUIPanel::OnGenerate360Clicked()
{
    if (CurrentPreviewImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image available"));
        return FReply::Handled();
    }
    UpdateStatus(TEXT("Submitting 360° workflow..."));
    Start360Generation();
    return FReply::Handled();
}

// ============================================================================
// Helpers
// ============================================================================

bool SComfyUIPanel::LoadWorkflowFromFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutWorkflow)
{
    FString WorkflowPath;
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
    {
        WorkflowPath = FPaths::Combine(Plugin->GetBaseDir(), RelativePath);
    }

    if (!FPaths::FileExists(WorkflowPath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Workflow not found at: %s"), *WorkflowPath);
        return false;
    }

    FString WorkflowJson;
    if (!FFileHelper::LoadFileToString(WorkflowJson, *WorkflowPath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to read workflow: %s"), *WorkflowPath);
        return false;
    }

    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorkflowJson);
    if (!FJsonSerializer::Deserialize(Reader, OutWorkflow) || !OutWorkflow.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to parse workflow: %s"), *WorkflowPath);
        return false;
    }

    return true;
}

FString SComfyUIPanel::SerializeWorkflow(const TSharedPtr<FJsonObject>& WorkflowObj)
{
    FString OutputString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(WorkflowObj.ToSharedRef(), Writer);
    return OutputString;
}

// ============================================================================
// Server Control
// ============================================================================

FReply SComfyUIPanel::OnStartComfyClicked()
{
    if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
    {
        if (Module->ForceStartPortable())
        {
            UpdateStatus(TEXT("Launching ComfyUI..."));
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
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

    Request->SetURL(BaseUrl + TEXT("/system_stats"));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(3.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [this](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            if (bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
            {
                bIsComfyReady = true;
                UpdateStatus(TEXT("Connected: ComfyUI is Ready"));
                return;
            }

            bIsComfyReady = false;

            if (ConnectionAttempts > 0 && ConnectionAttempts < 30)
            {
                ConnectionAttempts++;
                UpdateStatus(FString::Printf(TEXT("Waiting for ComfyUI... (%ds)"), ConnectionAttempts));
                if (GEditor)
                {
                    GEditor->GetTimerManager()->SetTimer(
                        ConnectionTimerHandle,
                        FTimerDelegate::CreateRaw(this, &SComfyUIPanel::PollComfyConnection),
                        1.0f, false);
                }
            }
            else if (ConnectionAttempts >= 30)
            {
                UpdateStatus(TEXT("Error: Timed out connecting to ComfyUI"));
                ConnectionAttempts = 0;
            }
            else
            {
                StatusText = TEXT("Server Offline");
            }
        });

    Request->ProcessRequest();
}

// ============================================================================
// Import / Apply
// ============================================================================

FReply SComfyUIPanel::OnImportClicked()
{
    if (CurrentPreviewImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image available"));
        return FReply::Handled();
    }

    FDateTime Now = FDateTime::Now();
    FString TextureName = FString::Printf(TEXT("T_Generated_%s"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
    FString TextureAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(TEXT("/Game/GeneratedTextures"), TextureName);

    UTexture2D* PermanentTexture = UComfyUIBlueprintLibrary::ImportImageAsAsset(CurrentPreviewImagePath, TextureAssetPath);
    if (!PermanentTexture)
    {
        UpdateStatus(TEXT("Error: Failed to create texture asset"));
        return FReply::Handled();
    }

    if (TargetActors.Num() > 0 && BaseMaterial)
    {
        int32 SuccessCount = 0;
        for (const TWeakObjectPtr<AActor>& WeakActor : TargetActors)
        {
            if (!WeakActor.IsValid()) continue;
            AActor* Actor = WeakActor.Get();
            UMeshComponent* MeshComp = GetMeshComponentFromActor(Actor);
            if (!MeshComp) continue;

            FString MaterialName = FString::Printf(TEXT("MI_%s_%s"),
                *Actor->GetActorLabel().Replace(TEXT(" "), TEXT("_")),
                *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
            FString MaterialAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(
                TEXT("/Game/GeneratedMaterials"), MaterialName);

            UPackage* Package = CreatePackage(*MaterialAssetPath);
            UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(
                Package,
                FName(*FPaths::GetBaseFilename(MaterialAssetPath)),
                RF_Public | RF_Standalone);

            if (!MaterialInstance) continue;

            MaterialInstance->SetParentEditorOnly(BaseMaterial);
            MaterialInstance->SetTextureParameterValueEditorOnly(
                FMaterialParameterInfo(TEXT("BaseColorTexture")), PermanentTexture);
            MaterialInstance->MarkPackageDirty();
            MaterialInstance->PostEditChange();
            FAssetRegistryModule::AssetCreated(MaterialInstance);
            MeshComp->SetMaterial(0, MaterialInstance);
            ActorMaterialMap.Remove(WeakActor);
            SuccessCount++;
        }
        UpdateStatus(FString::Printf(TEXT("Saved texture and %d material(s) to project"), SuccessCount));
    }
    else
    {
        UpdateStatus(FString::Printf(TEXT("Saved texture to: %s"), *TextureAssetPath));
    }

    CurrentPreviewImagePath.Empty();
    return FReply::Handled();
}

FReply SComfyUIPanel::OnApplyToActorsClicked()
{
    if (CurrentPreviewImagePath.IsEmpty() || TargetActors.Num() == 0)
    {
        UpdateStatus(TEXT("Error: Need a preview image and target actors"));
        return FReply::Handled();
    }
    UTexture2D* Texture = UComfyUIBlueprintLibrary::LoadImageFromFile(CurrentPreviewImagePath);
    if (Texture) ApplyMaterialToTargetActors(Texture);
    return FReply::Handled();
}

FReply SComfyUIPanel::OnApplyToComposureClicked()
{
    if (CurrentPreviewImagePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image available"));
        return FReply::Handled();
    }

    UTexture2D* TextureToApply = nullptr;
    if (LastImportedImagePath == CurrentPreviewImagePath && LastImportedTexture.IsValid())
    {
        TextureToApply = LastImportedTexture.Get();
    }
    else
    {
        FDateTime Now = FDateTime::Now();
        FString TextureName = FString::Printf(TEXT("T_Composure_%s"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
        FString TextureAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(
            TEXT("/Game/GeneratedTextures"), TextureName);
        TextureToApply = UComfyUIBlueprintLibrary::ImportImageAsAsset(CurrentPreviewImagePath, TextureAssetPath);
        if (!TextureToApply)
        {
            UpdateStatus(TEXT("Error: Failed to import texture"));
            return FReply::Handled();
        }
        LastImportedImagePath = CurrentPreviewImagePath;
        LastImportedTexture = TextureToApply;
    }

    ApplyTextureToComposurePlates(TextureToApply);
    return FReply::Handled();
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

// ============================================================================
// Post-Completion Actions
// ============================================================================

void SComfyUIPanel::Apply360ToSkyLight(UTexture2D* Texture360)
{
    if (!GEditor || !GEditor->GetEditorWorldContext().World()) return;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    // Remove old sky spheres
    TArray<AActor*> ActorsToDestroy;
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        if (It->GetActorLabel().Contains(TEXT("AI_360_SkySphere")))
            ActorsToDestroy.Add(*It);
    }
    for (AActor* Actor : ActorsToDestroy)
        World->DestroyActor(Actor);

    // Create sky sphere
    AStaticMeshActor* SkySphere = World->SpawnActor<AStaticMeshActor>();
    UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    if (SkySphere && SphereMesh)
    {
        SkySphere->GetStaticMeshComponent()->SetStaticMesh(SphereMesh);
        SkySphere->SetActorLabel(TEXT("AI_360_SkySphere"));
        SkySphere->SetActorScale3D(FVector(300.0f));

        if (BaseMaterial)
        {
            UMaterialInstanceDynamic* SkyMat = UMaterialInstanceDynamic::Create(BaseMaterial, SkySphere);
            SkyMat->SetTextureParameterValue(TEXT("BaseColorTexture"), Texture360);
            SkySphere->GetStaticMeshComponent()->SetMaterial(0, SkyMat);
            SkySphere->GetStaticMeshComponent()->SetCastShadow(false);
        }
    }

    // Find or create SkyLight
    ASkyLight* SkyLight = nullptr;
    for (TActorIterator<ASkyLight> It(World); It; ++It) { SkyLight = *It; break; }
    if (!SkyLight) SkyLight = World->SpawnActor<ASkyLight>();

    if (SkyLight && SkyLight->GetLightComponent())
    {
        USkyLightComponent* SkyLightComp = SkyLight->GetLightComponent();
        SkyLightComp->SourceType = ESkyLightSourceType::SLS_CapturedScene;
        SkyLightComp->Intensity = 2.0f;
        SkyLightComp->Mobility = EComponentMobility::Movable;
        SkyLightComp->MarkRenderStateDirty();
        SkyLightComp->RecaptureSky();
    }

    if (GEditor) GEditor->RedrawAllViewports();
    UpdateStatus(TEXT("360° applied to scene lighting!"));
}

void SComfyUIPanel::ApplyMaterialToTargetActors(UTexture2D* Texture)
{
    if (!Texture || !BaseMaterial) return;

    int32 AppliedCount = 0;

    // Clean up stale entries
    for (auto It = ActorMaterialMap.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid()) It.RemoveCurrent();
    }

    for (const TWeakObjectPtr<AActor>& WeakActor : TargetActors)
    {
        if (!WeakActor.IsValid()) continue;
        AActor* Actor = WeakActor.Get();
        UMeshComponent* MeshComp = GetMeshComponentFromActor(Actor);
        if (!MeshComp) continue;

        UMaterialInstanceDynamic* DynMaterial = nullptr;
        if (ActorMaterialMap.Contains(WeakActor))
        {
            DynMaterial = ActorMaterialMap[WeakActor];
        }
        else
        {
            DynMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, MeshComp);
            ActorMaterialMap.Add(WeakActor, DynMaterial);
            MeshComp->SetMaterial(0, DynMaterial);
        }

        if (DynMaterial)
        {
            DynMaterial->SetTextureParameterValue(TEXT("BaseColorTexture"), Texture);
            AppliedCount++;
        }
    }

    UpdateStatus(FString::Printf(TEXT("Applied material to %d actor(s)"), AppliedCount));
}

void SComfyUIPanel::ApplyTextureToComposurePlates(UTexture2D* Texture)
{
    if (!Texture || !GEditor || !GEditor->GetEditorWorldContext().World()) return;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    int32 PlatesUpdated = 0;

    for (AActor* Actor : AllActors)
    {
        if (Actor->GetClass()->GetName() != TEXT("CompositeActor")) continue;

        UClass* ActorClass = Actor->GetClass();
        FProperty* LayersProp = ActorClass->FindPropertyByName(TEXT("CompositeLayers"));
        FArrayProperty* ArrayProp = CastField<FArrayProperty>(LayersProp);
        if (!ArrayProp) continue;

        FScriptArrayHelper ArrayHelper(ArrayProp, LayersProp->ContainerPtrToValuePtr<void>(Actor));
        for (int32 i = 0; i < ArrayHelper.Num(); i++)
        {
            UObject* LayerObj = *reinterpret_cast<UObject**>(ArrayHelper.GetRawPtr(i));
            if (!LayerObj || !LayerObj->GetClass()->GetName().Contains(TEXT("Plate"))) continue;

            FProperty* TextureProp = LayerObj->GetClass()->FindPropertyByName(TEXT("Texture"));
            FObjectProperty* ObjProp = CastField<FObjectProperty>(TextureProp);
            if (!ObjProp) continue;

            ObjProp->SetObjectPropertyValue(TextureProp->ContainerPtrToValuePtr<void>(LayerObj), Texture);
            FPropertyChangedEvent PropertyEvent(TextureProp);
            LayerObj->PostEditChangeProperty(PropertyEvent);
            LayerObj->Modify();
            PlatesUpdated++;
        }

        Actor->Modify();
        FPropertyChangedEvent ActorPropertyEvent(LayersProp);
        Actor->PostEditChangeProperty(ActorPropertyEvent);
    }

    if (GEditor) GEditor->RedrawAllViewports();
    UpdateStatus(PlatesUpdated > 0
        ? FString::Printf(TEXT("Applied texture to %d Composure plate(s)"), PlatesUpdated)
        : TEXT("No Composure plate layers found"));
}

// ============================================================================
// UI Event Handlers
// ============================================================================

void SComfyUIPanel::OnPromptTextChanged(const FText& NewText)       { PromptText = NewText.ToString(); }
void SComfyUIPanel::OnNegativePromptTextChanged(const FText& NewText) { NegativePromptText = NewText.ToString(); }
void SComfyUIPanel::OnWidthChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type)  { SelectedWidth = NewSelection; }
void SComfyUIPanel::OnHeightChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type) { SelectedHeight = NewSelection; }
void SComfyUIPanel::OnCustomWidthChanged(int32 NewValue)  { CustomWidth = NewValue; }
void SComfyUIPanel::OnCustomHeightChanged(int32 NewValue) { CustomHeight = NewValue; }

void SComfyUIPanel::OnAutoApplyCheckChanged(ECheckBoxState NewState)
{
    bAutoApplyEnabled = (NewState == ECheckBoxState::Checked);
}

// ============================================================================
// Actor Helpers
// ============================================================================

TArray<AActor*> SComfyUIPanel::GetSelectedActors()
{
    TArray<AActor*> Result;
    if (!GEditor) return Result;
    USelection* Selection = GEditor->GetSelectedActors();
    if (!Selection) return Result;
    for (FSelectionIterator It(*Selection); It; ++It)
        if (AActor* Actor = Cast<AActor>(*It))
            Result.Add(Actor);
    return Result;
}

void SComfyUIPanel::AddSelectedActorsToList()
{
    TArray<AActor*> SelectedActors = GetSelectedActors();
    if (SelectedActors.Num() == 0) { UpdateStatus(TEXT("No actors selected")); return; }

    int32 AddedCount = 0, InvalidCount = 0;
    for (AActor* Actor : SelectedActors)
    {
        if (!Actor) continue;
        if (!IsActorValidForMaterial(Actor)) { InvalidCount++; continue; }

        bool bAlreadyAdded = false;
        for (const TWeakObjectPtr<AActor>& Existing : TargetActors)
            if (Existing.IsValid() && Existing.Get() == Actor) { bAlreadyAdded = true; break; }

        if (!bAlreadyAdded) { TargetActors.Add(Actor); AddedCount++; }
    }

    UpdateStatus(FString::Printf(TEXT("Added %d actor(s), skipped %d invalid"), AddedCount, InvalidCount));
}

void SComfyUIPanel::ClearTargetActors()  { TargetActors.Empty(); }
void SComfyUIPanel::RemoveActorFromList(AActor* Actor)
{
    for (int32 i = TargetActors.Num() - 1; i >= 0; i--)
        if (TargetActors[i].IsValid() && TargetActors[i].Get() == Actor)
            { TargetActors.RemoveAt(i); break; }
}

UMeshComponent* SComfyUIPanel::GetMeshComponentFromActor(AActor* Actor)
{
    if (!Actor) return nullptr;
    if (UStaticMeshComponent* SM = Actor->FindComponentByClass<UStaticMeshComponent>()) return SM;
    if (USkeletalMeshComponent* SK = Actor->FindComponentByClass<USkeletalMeshComponent>()) return SK;
    return nullptr;
}

bool SComfyUIPanel::IsActorValidForMaterial(AActor* Actor)
{
    return Actor && GetMeshComponentFromActor(Actor) != nullptr;
}

// ============================================================================
// Display / Material
// ============================================================================

void SComfyUIPanel::UpdateStatus(const FString& Status)
{
    StatusText = Status;
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: %s"), *Status);
}

void SComfyUIPanel::LoadAndDisplayImage(const FString& FilePath)
{
    UTexture2D* Texture = UComfyUIBlueprintLibrary::LoadImageFromFile(FilePath);
    if (!Texture) return;

    Texture->AddToRoot();

    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
        if (UTexture2D* Old = Cast<UTexture2D>(ImageBrush->GetResourceObject()))
            Old->RemoveFromRoot();

    ImageBrush = MakeShared<FSlateBrush>();
    ImageBrush->SetResourceObject(Texture);
    ImageBrush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    ImageBrush->DrawAs = ESlateBrushDrawType::Image;
    ImageBrush->Tiling = ESlateBrushTileType::NoTile;

    if (PreviewImage.IsValid())
        PreviewImage->SetImage(ImageBrush.Get());
}

void SComfyUIPanel::LoadBaseMaterial()
{
    if (BaseMaterial) return;
    BaseMaterial = LoadObject<UMaterial>(nullptr,
        TEXT("/ComfyUI/Materials/M_ComfyUI_Base"));

    if (BaseMaterial)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Base material loaded"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to load base material"));
    }
        
}

// ============================================================================
// Destructor
// ============================================================================

SComfyUIPanel::~SComfyUIPanel()
{
    if (GEditor && ConnectionTimerHandle.IsValid())
        GEditor->GetTimerManager()->ClearTimer(ConnectionTimerHandle);

    if (ImageBrush.IsValid() && ImageBrush->GetResourceObject())
        if (UTexture2D* OldTexture = Cast<UTexture2D>(ImageBrush->GetResourceObject()))
            OldTexture->RemoveFromRoot();
}

#undef LOCTEXT_NAMESPACE
