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
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Engine/TextureCube.h"
#include "ImageUtils.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/PackageName.h"

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

    StatusText = TEXT("Connecting...");
    WeakThis = SharedThis(this);

    PollComfyConnection();

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot().Padding(10.0f)
        [
            SNew(SVerticalBox)

            // --- Connection Status ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,15)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\u25CF")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 24))
                    .ColorAndOpacity_Lambda([this]() {
                        return bIsComfyReady ? FLinearColor::Green : FLinearColor::Red;
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() {
                        return bIsComfyReady
                            ? LOCTEXT("ServerReady", "ComfyUI Connected")
                            : LOCTEXT("ServerOffline", "ComfyUI Offline");
                    })
                ]
            ]

            // --- Prompt ---
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("PromptLabel", "Prompt:")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,10)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("PromptHint", "Describe the image..."))
                .Text(FText::FromString(PromptText))
                .OnTextChanged(this, &SComfyUIPanel::OnPromptTextChanged)
            ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(LOCTEXT("NegPromptLabel", "Negative Prompt:")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,10)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("NegPromptHint", "What to avoid..."))
                .Text(FText::FromString(NegativePromptText))
                .OnTextChanged(this, &SComfyUIPanel::OnNegativePromptTextChanged)
            ]

            // --- Resolution ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("WidthLabel", "Width: ")) ]
                + SHorizontalBox::Slot().Padding(10,0,0,0).AutoWidth()
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&WidthOptions)
                    .OnSelectionChanged(this, &SComfyUIPanel::OnWidthChanged)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                        return SNew(STextBlock).Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(SelectedWidth)
                    [ SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(*SelectedWidth); }) ]
                ]
                + SHorizontalBox::Slot().Padding(10,0,0,0).AutoWidth()
                [
                    SNew(SNumericEntryBox<int32>)
                    .Visibility_Lambda([this]() {
                        return (*SelectedWidth == TEXT("Custom")) ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    .Value_Lambda([this]() { return TOptional<int32>(CustomWidth); })
                    .OnValueChanged(this, &SComfyUIPanel::OnCustomWidthChanged)
                    .MinValue(64).MaxValue(8192).MinDesiredValueWidth(100)
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(20,0,0,0).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(LOCTEXT("HeightLabel", "Height: ")) ]
                + SHorizontalBox::Slot().Padding(10,0,0,0).AutoWidth()
                [
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&HeightOptions)
                    .OnSelectionChanged(this, &SComfyUIPanel::OnHeightChanged)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                        return SNew(STextBlock).Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(SelectedHeight)
                    [ SNew(STextBlock).Text_Lambda([this](){ return FText::FromString(*SelectedHeight); }) ]
                ]
                + SHorizontalBox::Slot().Padding(10,0,0,0).AutoWidth()
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

            // --- Generate / Browse ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,15,0,5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("GenerateButton", "Generate Image"))
                    .OnClicked(this, &SComfyUIPanel::OnGenerateClicked)
                    .IsEnabled_Lambda([this]() { return bIsComfyReady; })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10,0,0,0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("BrowseButton", "Browse Input..."))
                    .OnClicked(this, &SComfyUIPanel::OnImg2ImgBrowseClicked)
                    .ToolTipText(LOCTEXT("BrowseTooltip", "Load an existing image into Preview A"))
                ]
            ]

            // --- Preview A ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,5).HAlign(HAlign_Center)
            [
                SNew(SBox)
                .WidthOverride_Lambda([this]() -> FOptionalSize {
                    if (ImageBrushA.IsValid() && ImageBrushA->GetResourceObject())
                    {
                        const FVector2D S = ImageBrushA->ImageSize;
                        if (S.X > 0 && S.Y > 0)
                            return FOptionalSize(S.X * FMath::Min(FMath::Min(800.f/S.X, 600.f/S.Y), 1.f));
                    }
                    return FOptionalSize();
                })
                .HeightOverride_Lambda([this]() -> FOptionalSize {
                    if (ImageBrushA.IsValid() && ImageBrushA->GetResourceObject())
                    {
                        const FVector2D S = ImageBrushA->ImageSize;
                        if (S.X > 0 && S.Y > 0)
                            return FOptionalSize(S.Y * FMath::Min(FMath::Min(800.f/S.X, 600.f/S.Y), 1.f));
                    }
                    return FOptionalSize();
                })
                [ SAssignNew(PreviewImageA, SImage) ]
            ]

            // --- Preview A Actions ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,15)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ImportAButton", "Import to Project"))
                    .IsEnabled_Lambda([this]() { return !PreviewImagePathA.IsEmpty(); })
                    .OnClicked_Lambda([this]() { return OnImportClicked(PreviewImagePathA); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10,0,0,0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ComposureAButton", "Apply to Composure"))
                    .IsEnabled_Lambda([this]() { return !PreviewImagePathA.IsEmpty(); })
                    .OnClicked_Lambda([this]() { return OnApplyToComposureClicked(PreviewImagePathA); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10,0,0,0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("360AButton", "Generate 360\u00b0 HDRI"))
                    .IsEnabled_Lambda([this]() { return bIsComfyReady && !PreviewImagePathA.IsEmpty(); })
                    .OnClicked_Lambda([this]() { return OnGenerate360Clicked(PreviewImagePathA); })
                ]
            ]

            // --- Img2Img Section ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Img2ImgLabel", "Refine / Edit (Img2Img):"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,10)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("Img2ImgHint", "Describe the edit..."))
                .Text(FText::FromString(Img2ImgPromptText))
                .OnTextChanged_Lambda([this](const FText& T){ Img2ImgPromptText = T.ToString(); })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
            [
                SNew(SButton)
                .Text(LOCTEXT("Img2ImgButton", "Run Img2Img"))
                .IsEnabled_Lambda([this]() { return bIsComfyReady && !PreviewImagePathA.IsEmpty(); })
                .OnClicked(this, &SComfyUIPanel::OnImg2ImgClicked)
                .ToolTipText(LOCTEXT("Img2ImgTooltip", "Refine the image in Preview A using the edit prompt above"))
            ]

            // --- Preview B ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,5).HAlign(HAlign_Center)
            [
                SNew(SBox)
                .WidthOverride_Lambda([this]() -> FOptionalSize {
                    if (ImageBrushB.IsValid() && ImageBrushB->GetResourceObject())
                    {
                        const FVector2D S = ImageBrushB->ImageSize;
                        if (S.X > 0 && S.Y > 0)
                            return FOptionalSize(S.X * FMath::Min(FMath::Min(800.f/S.X, 600.f/S.Y), 1.f));
                    }
                    return FOptionalSize();
                })
                .HeightOverride_Lambda([this]() -> FOptionalSize {
                    if (ImageBrushB.IsValid() && ImageBrushB->GetResourceObject())
                    {
                        const FVector2D S = ImageBrushB->ImageSize;
                        if (S.X > 0 && S.Y > 0)
                            return FOptionalSize(S.Y * FMath::Min(FMath::Min(800.f/S.X, 600.f/S.Y), 1.f));
                    }
                    return FOptionalSize();
                })
                [ SAssignNew(PreviewImageB, SImage) ]
            ]

            // --- Preview B Actions ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,15)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ImportBButton", "Import to Project"))
                    .IsEnabled_Lambda([this]() { return !PreviewImagePathB.IsEmpty(); })
                    .OnClicked_Lambda([this]() { return OnImportClicked(PreviewImagePathB); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10,0,0,0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ComposureBButton", "Apply to Composure"))
                    .IsEnabled_Lambda([this]() { return !PreviewImagePathB.IsEmpty(); })
                    .OnClicked_Lambda([this]() { return OnApplyToComposureClicked(PreviewImagePathB); })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(10,0,0,0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("360BButton", "Generate 360\u00b0 HDRI"))
                    .IsEnabled_Lambda([this]() { return bIsComfyReady && !PreviewImagePathB.IsEmpty(); })
                    .OnClicked_Lambda([this]() { return OnGenerate360Clicked(PreviewImagePathB); })
                ]
            ]

            // --- Status ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,5)
            [
                SNew(STextBlock)
                .Text_Lambda([this]() { return FText::FromString(StatusText); })
                .Justification(ETextJustify::Center)
                .ColorAndOpacity_Lambda([this]() {
                    if (StatusText.Contains("Error") || StatusText.Contains("Offline"))
                        return FLinearColor::Red;
                    if (StatusText.Contains("Generating") || StatusText.Contains("Waiting")
                        || StatusText.Contains("Uploading") || StatusText.Contains("Running")
                        || StatusText.Contains("Downloading"))
                        return FLinearColor::Yellow;
                    return FLinearColor::White;
                })
            ]
        ]
    ];
}

// ============================================================================
// Generic Workflow System
// ============================================================================

void SComfyUIPanel::SubmitWorkflow(const FComfyWorkflowParams& Params)
{
    TSharedPtr<FJsonObject> PromptObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Params.WorkflowJson);
    if (!FJsonSerializer::Deserialize(Reader, PromptObject) || !PromptObject.IsValid())
    {
        UpdateStatus(TEXT("Error: Invalid workflow JSON"));
        return;
    }

    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetObjectField(TEXT("prompt"), PromptObject);
    Wrapper->SetStringField(TEXT("client_id"), TEXT("unrealplugin"));

    FString RequestBody;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
    FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);

    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + TEXT("/prompt"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestBody);

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

            Panel->StartHistoryPoller(PromptId, CapturedParams);

            if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
            {
                TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
                if (!WSHandler.IsValid()) return;

                FString WsUrl = BaseUrl
                    .Replace(TEXT("http://"), TEXT("ws://"))
                    .Replace(TEXT("https://"), TEXT("wss://"))
                    + TEXT("/ws?clientId=unrealplugin");

                // Build the watch+register logic as a lambda so we can defer it
                // until the socket is actually connected
                auto RegisterWatcher = [CapturedWeakThis, CapturedParams, PromptId, WSHandler]()
                    {
                        TSharedPtr<SComfyUIPanel> InnerPanel = CapturedWeakThis.Pin();
                        if (!InnerPanel.IsValid()) return;

                        // Clear our own previous stale watcher if any
                        if (!InnerPanel->CurrentPromptId.IsEmpty())
                            WSHandler->UnwatchPrompt(InnerPanel->CurrentPromptId);

                        FComfyUIWorkflowCompleteDelegateNative CompleteDelegate;
                        CompleteDelegate.BindLambda(
                            [CapturedWeakThis, CapturedParams, PromptId](bool bSuccess, const FString& InPromptId)
                            {
                                TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                                if (Panel.IsValid())
                                    Panel->OnWorkflowComplete(bSuccess, PromptId, CapturedParams);
                            });

                        WSHandler->WatchPrompt(PromptId, CompleteDelegate);
                    };

                if (WSHandler->IsConnected())
                {
                    // Already connected — register immediately
                    RegisterWatcher();
                }
                else
                {
                    // Not connected yet — defer registration until handshake completes
                    WSHandler->OnConnectedEvent.AddLambda([RegisterWatcher, WSHandler]()
                        {
                            RegisterWatcher();
                            WSHandler->OnConnectedEvent.Clear();
                        });

                    WSHandler->Connect(WsUrl);
                }
            }
        });

    Request->ProcessRequest();
}

void SComfyUIPanel::OnWorkflowComplete(bool bSuccess, const FString& PromptId, FComfyWorkflowParams Params)
{

    StopHistoryPoller();

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: OnWorkflowComplete - Success: %d, PromptId: %s"),
        bSuccess, *PromptId);

    if (!bSuccess)
    {
        UpdateStatus(TEXT("Error: Workflow failed"));
        return;
    }

    UpdateStatus(TEXT("Fetching result..."));

    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

    TWeakPtr<SComfyUIPanel> CapturedWeakThis = WeakThis;

    if (GEditor)
    {
        FTimerHandle DelayTimer;
        GEditor->GetTimerManager()->SetTimer(
            DelayTimer,
            [CapturedWeakThis, Params, BaseUrl, PromptId]()
            {
                TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                if (!Panel.IsValid()) return;

                // Step 1: fetch /history to get the output filename
                TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HistoryRequest = FHttpModule::Get().CreateRequest();
                HistoryRequest->SetURL(BaseUrl + TEXT("/history/") + PromptId);
                HistoryRequest->SetVerb(TEXT("GET"));
                HistoryRequest->OnProcessRequestComplete().BindLambda(
                    [CapturedWeakThis, Params, BaseUrl, PromptId](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
                    {
                        TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                        if (!Panel.IsValid()) return;

                        if (!bSucceeded || !Response.IsValid())
                        {
                            Panel->UpdateStatus(TEXT("Error: Could not fetch history"));
                            return;
                        }

                        TSharedPtr<FJsonObject> History;
                        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                        if (!FJsonSerializer::Deserialize(Reader, History) || !History.IsValid())
                        {
                            Panel->UpdateStatus(TEXT("Error: Could not parse history"));
                            return;
                        }

                        // Find first non-temp image in outputs
                        FString OutputFilename;
                        const TSharedPtr<FJsonObject>* PromptHistory;
                        if (History->TryGetObjectField(PromptId, PromptHistory))
                        {
                            const TSharedPtr<FJsonObject>* Outputs;
                            if ((*PromptHistory)->TryGetObjectField(TEXT("outputs"), Outputs))
                            {
                                for (auto& NodePair : (*Outputs)->Values)
                                {
                                    const TSharedPtr<FJsonObject>* NodeOutput;
                                    if (NodePair.Value->TryGetObject(NodeOutput))
                                    {
                                        const TArray<TSharedPtr<FJsonValue>>* Images;
                                        if ((*NodeOutput)->TryGetArrayField(TEXT("images"), Images) && Images->Num() > 0)
                                        {
                                            FString Filename = (*Images)[0]->AsObject()->GetStringField(TEXT("filename"));
                                            if (!Filename.Contains(TEXT("_temp_")))
                                            {
                                                OutputFilename = Filename;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (OutputFilename.IsEmpty())
                        {
                            Panel->UpdateStatus(TEXT("Error: No output image found in history"));
                            return;
                        }

                        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Output filename: %s"), *OutputFilename);

                        // Step 2: download the image via /view
                        Panel->UpdateStatus(TEXT("Downloading result..."));
                        Panel->DownloadImageFromComfyUI(OutputFilename,
                            [CapturedWeakThis, Params](bool bDownloadSuccess, const FString& LocalPath)
                            {
                                TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                                if (!Panel.IsValid()) return;

                                if (!bDownloadSuccess || LocalPath.IsEmpty())
                                {
                                    Panel->UpdateStatus(TEXT("Error: Failed to download result image"));
                                    return;
                                }

                                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Downloaded to: %s"), *LocalPath);

                                if (Params.bUpdatePreview)
                                {
                                    if (Params.bTargetPreviewB)
                                        Panel->PreviewImagePathB = LocalPath;
                                    else
                                        Panel->PreviewImagePathA = LocalPath;

                                    Panel->LoadAndDisplayImage(LocalPath, Params.bTargetPreviewB);
                                }

                                if (Params.bConvertToHDRI)
                                {
                                    // Convert downloaded panorama to .hdr
                                    FString HdrPath = Panel->ConvertImageToHDR(LocalPath);
                                    if (!HdrPath.IsEmpty())
                                    {
                                        // Import as HDR texture
                                        UTextureCube* HdrTexture = Panel->ImportHDRToProject(
                                            HdrPath, Params.OutputPrefix);
                                        if (HdrTexture)
                                        {
                                            Panel->ApplyTextureToHDRIBackdrop(HdrTexture);
                                        }
                                    }
                                }
                                else if (Params.bAutoImport)
                                {
                                    Panel->ImportImageToProject(LocalPath, Params.OutputPrefix);
                                }

                                Panel->UpdateStatus(Params.CompleteStatus);
                            });
                    });
                HistoryRequest->ProcessRequest();
            },
            0.5f,
            false
        );
    }
}

// ============================================================================
// Workflow Builders
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

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson    = UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(FluxParams);
    WorkflowParams.OutputPrefix    = CurrentFilenamePrefix;
    WorkflowParams.RunningStatus   = TEXT("Generating image...");
    WorkflowParams.CompleteStatus  = TEXT("Done! Import to project or run Img2Img.");
    WorkflowParams.bUpdatePreview  = true;
    WorkflowParams.bAutoImport     = false;
    WorkflowParams.bTargetPreviewB = false;

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::StartImg2Img()
{
    TSharedPtr<FJsonObject> WorkflowObj;
    if (!LoadWorkflowFromFile(TEXT("workflows/img2img-API.json"), WorkflowObj))
    {
        UpdateStatus(TEXT("Error: img2img workflow file not found"));
        return;
    }

    // PreviewImagePathA is always the source — either a downloaded result or a browsed+uploaded image
    FString Filename = FPaths::GetCleanFilename(PreviewImagePathA);

    // Images uploaded via /upload/image land in ComfyUI's input folder
    // Images that came from /view (generated outputs) need [output] suffix
    // We track this by whether the local path is in our temp folder
    bool bIsDownloadedOutput = PreviewImagePathA.StartsWith(GetLocalTempFolder());
    FString NodeImageValue = bIsDownloadedOutput
        ? Filename + TEXT(" [output]")
        : Filename;

    if (TSharedPtr<FJsonObject> ImageNode = WorkflowObj->GetObjectField(TEXT("32")))
        ImageNode->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), NodeImageValue);

    if (TSharedPtr<FJsonObject> PromptNode = WorkflowObj->GetObjectField(TEXT("2")))
        PromptNode->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("text"), Img2ImgPromptText);

    if (TSharedPtr<FJsonObject> SeedNode = WorkflowObj->GetObjectField(TEXT("16")))
    {
        int32 NewSeed = FMath::Abs((int32)(FDateTime::Now().GetTicks() % MAX_int32));
        SeedNode->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("seed"), NewSeed);
    }

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson    = SerializeWorkflow(WorkflowObj);
    WorkflowParams.OutputPrefix    = TEXT("Flux2-Klein-Edit");
    WorkflowParams.RunningStatus   = TEXT("Running img2img...");
    WorkflowParams.CompleteStatus  = TEXT("Img2Img complete! Import to project or run again.");
    WorkflowParams.bUpdatePreview  = true;
    WorkflowParams.bAutoImport     = false;
    WorkflowParams.bTargetPreviewB = true;

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::Start360Generation(const FString& SourcePath)
{
    TSharedPtr<FJsonObject> WorkflowObj;
    if (!LoadWorkflowFromFile(TEXT("workflows/360_Kontext-Small-API.json"), WorkflowObj))
    {
        UpdateStatus(TEXT("Error: 360 workflow file not found"));
        return;
    }

    FString Filename = FPaths::GetCleanFilename(SourcePath);
    bool bIsDownloadedOutput = SourcePath.StartsWith(GetLocalTempFolder());
    FString NodeImageValue = bIsDownloadedOutput
        ? Filename + TEXT(" [output]")
        : Filename;

    if (TSharedPtr<FJsonObject> Node147 = WorkflowObj->GetObjectField(TEXT("147")))
        Node147->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), NodeImageValue);

    if (TSharedPtr<FJsonObject> Node142 = WorkflowObj->GetObjectField(TEXT("142")))
        Node142->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), NodeImageValue);

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson   = SerializeWorkflow(WorkflowObj);
    WorkflowParams.OutputPrefix   = TEXT("Kontext_Upscale");
    WorkflowParams.RunningStatus  = TEXT("Generating 360\u00b0 panorama...");
    WorkflowParams.CompleteStatus = TEXT("360\u00b0 HDRI generated and imported to project!");
    WorkflowParams.bUpdatePreview = false;
    WorkflowParams.bAutoImport    = false;
	WorkflowParams.bConvertToHDRI = true;

    SubmitWorkflow(WorkflowParams);
}

// ============================================================================
// UI Callbacks
// ============================================================================

FReply SComfyUIPanel::OnGenerateClicked()
{
    UpdateStatus(TEXT("Submitting workflow..."));
    StartGeneration();
    return FReply::Handled();
}

FReply SComfyUIPanel::OnImg2ImgBrowseClicked()
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (!DesktopPlatform) return FReply::Handled();

    TArray<FString> OutFiles;
    bool bOpened = DesktopPlatform->OpenFileDialog(
        FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
        TEXT("Select Input Image"),
        TEXT(""),
        TEXT(""),
        TEXT("Image Files (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (bOpened && OutFiles.Num() > 0)
    {
        FString SelectedFile = OutFiles[0];
        UpdateStatus(TEXT("Uploading image to ComfyUI..."));

        // Display immediately from local path for instant feedback
        PreviewImagePathA = SelectedFile;
        LoadAndDisplayImage(SelectedFile, false);

        // Upload to ComfyUI in background — StartImg2Img will use the filename
        UploadImageToComfyUI(SelectedFile,
            [this](bool bSuccess, const FString& Filename)
            {
                if (bSuccess)
                    UpdateStatus(FString::Printf(TEXT("Input ready: %s"), *Filename));
                else
                    UpdateStatus(TEXT("Error: Failed to upload image to ComfyUI"));
            });
    }

    return FReply::Handled();
}

FReply SComfyUIPanel::OnImg2ImgClicked()
{
    if (PreviewImagePathA.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No input image. Generate or browse an image first."));
        return FReply::Handled();
    }
    UpdateStatus(TEXT("Submitting img2img workflow..."));
    StartImg2Img();
    return FReply::Handled();
}

FReply SComfyUIPanel::OnGenerate360Clicked(FString SourcePath)
{
    if (SourcePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image available. Generate an image first."));
        return FReply::Handled();
    }

    // If source is a browsed (non-downloaded) image, upload it first
    bool bIsDownloadedOutput = SourcePath.StartsWith(GetLocalTempFolder());
    if (!bIsDownloadedOutput)
    {
        UpdateStatus(TEXT("Uploading image to ComfyUI..."));
        UploadImageToComfyUI(SourcePath,
            [this, SourcePath](bool bSuccess, const FString& Filename)
            {
                if (bSuccess)
                {
                    UpdateStatus(TEXT("Submitting 360\u00b0 workflow..."));
                    Start360Generation(SourcePath);
                }
                else
                {
                    UpdateStatus(TEXT("Error: Failed to upload image"));
                }
            });
        return FReply::Handled();
    }

    UpdateStatus(TEXT("Submitting 360\u00b0 workflow..."));
    Start360Generation(SourcePath);
    return FReply::Handled();
}

FReply SComfyUIPanel::OnImportClicked(FString SourcePath)
{
    if (SourcePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image to import"));
        return FReply::Handled();
    }
    ImportImageToProject(SourcePath, TEXT("T_Generated"));
    return FReply::Handled();
}

FReply SComfyUIPanel::OnApplyToComposureClicked(FString SourcePath)
{
    if (SourcePath.IsEmpty())
    {
        UpdateStatus(TEXT("Error: No preview image available"));
        return FReply::Handled();
    }

    UTexture2D* TextureToApply = nullptr;
    if (LastImportedImagePath == SourcePath && LastImportedTexture.IsValid())
    {
        TextureToApply = LastImportedTexture.Get();
    }
    else
    {
        FDateTime Now = FDateTime::Now();
        FString TextureName = FString::Printf(TEXT("T_Composure_%s"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
        FString TextureAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(
            TEXT("/Game/GeneratedTextures"), TextureName);
        TextureToApply = UComfyUIBlueprintLibrary::ImportImageAsAsset(SourcePath, TextureAssetPath);
        if (!TextureToApply)
        {
            UpdateStatus(TEXT("Error: Failed to import texture for Composure"));
            return FReply::Handled();
        }
        LastImportedImagePath = SourcePath;
        LastImportedTexture = TextureToApply;
    }

    ApplyTextureToComposurePlates(TextureToApply);
    return FReply::Handled();
}

// ============================================================================
// Import
// ============================================================================

void SComfyUIPanel::ImportImageToProject(const FString& ImagePath, const FString& AssetNamePrefix)
{
    FDateTime Now = FDateTime::Now();
    FString TextureName = FString::Printf(TEXT("%s_%s"),
        *AssetNamePrefix, *Now.ToString(TEXT("%Y%m%d_%H%M%S")));
    FString TextureAssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(
        TEXT("/Game/GeneratedTextures"), TextureName);

    UTexture2D* Texture = UComfyUIBlueprintLibrary::ImportImageAsAsset(ImagePath, TextureAssetPath);
    if (Texture)
    {
        UpdateStatus(FString::Printf(TEXT("Imported: %s"), *TextureAssetPath));
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Imported texture to %s"), *TextureAssetPath);
    }
    else
    {
        UpdateStatus(TEXT("Error: Failed to import texture"));
    }
}

// ============================================================================
// HDR Conversion
// ============================================================================

FString SComfyUIPanel::ConvertImageToHDR(const FString& SourceImagePath)
{
    // Load image bytes
    TArray<uint8> RawFileData;
    if (!FFileHelper::LoadFileToArray(RawFileData, *SourceImagePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to read source: %s"), *SourceImagePath);
        return FString();
    }

    // Decode via ImageWrapper
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

    EImageFormat DetectedFormat = EImageFormat::PNG;
    if (SourceImagePath.EndsWith(TEXT(".jpg")) || SourceImagePath.EndsWith(TEXT(".jpeg")))
        DetectedFormat = EImageFormat::JPEG;

    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(DetectedFormat);
    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to decode image"));
        return FString();
    }

    TArray<uint8> RawRGBA;
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawRGBA))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to get raw pixel data"));
        return FString();
    }

    const int32 Width = ImageWrapper->GetWidth();
    const int32 Height = ImageWrapper->GetHeight();
    const int32 NumPixels = Width * Height;

    // Convert to float HDR with highlight boost
    // - Gamma decode (2.2) to linearize
    // - Boost highlights based on luminance to simulate HDR headroom
    TArray<FLinearColor> HDRPixels;
    HDRPixels.SetNum(NumPixels);

    for (int32 i = 0; i < NumPixels; i++)
    {
        // BGRA byte order
        const int32 Idx = i * 4;
        float B = RawRGBA[Idx + 0] / 255.0f;
        float G = RawRGBA[Idx + 1] / 255.0f;
        float R = RawRGBA[Idx + 2] / 255.0f;

        // Gamma decode to linear
        R = FMath::Pow(R, 2.2f);
        G = FMath::Pow(G, 2.2f);
        B = FMath::Pow(B, 2.2f);

        // Luminance-based highlight boost
        // Bright areas (sky, light sources) get pushed significantly higher
        // to give the HDRI Backdrop meaningful light intensity variation
        float Luminance = 0.2126f * R + 0.7152f * G + 0.0722f * B;
        float Boost = 1.0f + FMath::Pow(FMath::Clamp(Luminance, 0.0f, 1.0f), 2.0f) * 8.0f;

        HDRPixels[i] = FLinearColor(R * Boost, G * Boost, B * Boost, 1.0f);
    }

    
    // Write as 32-bit float EXR — UE imports this perfectly as HDR, no dialog
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString HdrPath = FPaths::Combine(
        FPaths::GetPath(SourceImagePath),
        FPaths::GetBaseFilename(SourceImagePath) + TEXT("_") + Timestamp + TEXT(".exr")
    );

    TSharedPtr<IImageWrapper> ExrWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
    if (!ExrWrapper.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to create EXR wrapper"));
        return FString();
    }

    // Pack HDRPixels into raw RGBA float array
    TArray<uint8> FloatData;
    FloatData.SetNumUninitialized(NumPixels * 4 * sizeof(float));
    float* FloatPtr = reinterpret_cast<float*>(FloatData.GetData());

    for (int32 i = 0; i < NumPixels; i++)
    {
        FloatPtr[i * 4 + 0] = HDRPixels[i].R;
        FloatPtr[i * 4 + 1] = HDRPixels[i].G;
        FloatPtr[i * 4 + 2] = HDRPixels[i].B;
        FloatPtr[i * 4 + 3] = 1.0f;
    }

    ExrWrapper->SetRaw(FloatPtr, FloatData.Num(), Width, Height, ERGBFormat::RGBAF, 32);

    const TArray64<uint8>& CompressedEXR = ExrWrapper->GetCompressed();
    if (CompressedEXR.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to compress EXR"));
        return FString();
    }

    if (!FFileHelper::SaveArrayToFile(CompressedEXR, *HdrPath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to save EXR to: %s"), *HdrPath);
        return FString();
    }

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI HDR: Written EXR to: %s"), *HdrPath);
    return HdrPath;
}


UTextureCube* SComfyUIPanel::ImportHDRToProject(const FString& HdrFilePath, const FString& AssetName)
{
#if WITH_EDITOR
    FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    FString AssetPath = UComfyUIBlueprintLibrary::GenerateUniqueAssetName(
        TEXT("/Game/GeneratedTextures/HDRI"), AssetName + TEXT("_") + Timestamp);

    FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
    FString AssetNameClean = FPackageName::GetLongPackageAssetName(PackageName);

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to create package: %s"), *PackageName);
        return nullptr;
    }

    // Read the EXR file
    TArray<uint8> HdrData;
    if (!FFileHelper::LoadFileToArray(HdrData, *HdrFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to read EXR: %s"), *HdrFilePath);
        return nullptr;
    }

    // Decode EXR to raw float pixels
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    TSharedPtr<IImageWrapper> ExrWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
    if (!ExrWrapper.IsValid() || !ExrWrapper->SetCompressed(HdrData.GetData(), HdrData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to decode EXR"));
        return nullptr;
    }

    TArray64<uint8> RawData;
    if (!ExrWrapper->GetRaw(ERGBFormat::RGBAF, 32, RawData))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to get raw float data from EXR"));
        return nullptr;
    }

    const int32 W = ExrWrapper->GetWidth();
    const int32 H = ExrWrapper->GetHeight();

    // Create UTextureCube asset
    UTextureCube* Texture = NewObject<UTextureCube>(Package, *AssetNameClean, RF_Public | RF_Standalone);
    if (!Texture)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Failed to create UTextureCube"));
        return nullptr;
    }

    // Initialize source as equirectangular longlat — UE will treat it as a cubemap
    Texture->Source.Init(W, H, 1, 1, TSF_RGBA32F, RawData.GetData());
    Texture->CompressionSettings = TC_HDR;
    Texture->SRGB = false;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->LODGroup = TEXTUREGROUP_Skybox;

    Texture->UpdateResource();
    Texture->PostEditChange();
    Texture->MarkPackageDirty();

    FAssetRegistryModule::AssetCreated(Texture);

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI HDR: Imported UTextureCube: %s"), *AssetPath);
    return Texture;
#else
    return nullptr;
#endif
}

void SComfyUIPanel::ApplyTextureToHDRIBackdrop(UTextureCube* Texture)
{
    if (!Texture || !GEditor || !GEditor->GetEditorWorldContext().World())
    {
        UpdateStatus(TEXT("Error: No texture or no editor world"));
        return;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    int32 BackdropsUpdated = 0;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!Actor) continue;

        if (!Actor->GetClass()->GetName().Contains(TEXT("HDRIBackdrop")))
            continue;

        UE_LOG(LogTemp, Warning, TEXT("ComfyUI HDR: Found HDRIBackdrop actor: %s"), *Actor->GetName());

        FProperty* CubemapProp = Actor->GetClass()->FindPropertyByName(TEXT("Cubemap"));
        if (!CubemapProp)
        {
            UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Could not find Cubemap property on HDRIBackdrop"));
            continue;
        }

        FObjectProperty* ObjProp = CastField<FObjectProperty>(CubemapProp);
        if (!ObjProp)
        {
            UE_LOG(LogTemp, Error, TEXT("ComfyUI HDR: Cubemap property is not an FObjectProperty"));
            continue;
        }

        ObjProp->SetObjectPropertyValue(
            CubemapProp->ContainerPtrToValuePtr<void>(Actor), Texture);

        FPropertyChangedEvent PropEvent(CubemapProp);
        Actor->PostEditChangeProperty(PropEvent);
        Actor->Modify();
        BackdropsUpdated++;

        UE_LOG(LogTemp, Warning, TEXT("ComfyUI HDR: Applied cubemap to HDRIBackdrop: %s"),
            *Actor->GetName());
    }

    if (GEditor)
        GEditor->RedrawAllViewports();

    if (BackdropsUpdated > 0)
        UpdateStatus(FString::Printf(TEXT("Applied HDRI to %d backdrop(s)"), BackdropsUpdated));
    else
        UpdateStatus(TEXT("No HDRIBackdrop actor found in scene. Place one first."));
}

// ============================================================================
// Composure
// ============================================================================

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
// Connection Polling
// ============================================================================

void SComfyUIPanel::PollComfyConnection()
{
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + TEXT("/system_stats"));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(5.0f);

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
            StatusText = TEXT("ComfyUI Offline");

            // Retry every 5 seconds
            if (GEditor)
            {
                GEditor->GetTimerManager()->SetTimer(
                    ConnectionTimerHandle,
                    FTimerDelegate::CreateRaw(this, &SComfyUIPanel::PollComfyConnection),
                    5.0f, false);
            }
        });

    Request->ProcessRequest();
}

// ============================================================================
// Network Helpers
// ============================================================================

void SComfyUIPanel::UploadImageToComfyUI(const FString& LocalFilePath, TFunction<void(bool, const FString&)> OnComplete)
{
    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *LocalFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to read file for upload: %s"), *LocalFilePath);
        OnComplete(false, TEXT(""));
        return;
    }

    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");
    FString Filename = FPaths::GetCleanFilename(LocalFilePath);

    // Build multipart/form-data body
    FString Boundary = TEXT("----ComfyUIBoundary");
    FString ContentType = FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary);

    TArray<uint8> Body;
    auto AppendStr = [&Body](const FString& Str)
    {
        FTCHARToUTF8 Converted(*Str);
        Body.Append((const uint8*)Converted.Get(), Converted.Length());
    };

    AppendStr(FString::Printf(TEXT("--%s\r\n"), *Boundary));
    AppendStr(FString::Printf(TEXT("Content-Disposition: form-data; name=\"image\"; filename=\"%s\"\r\n"), *Filename));
    AppendStr(TEXT("Content-Type: image/png\r\n\r\n"));
    Body.Append(FileData);
    AppendStr(FString::Printf(TEXT("\r\n--%s--\r\n"), *Boundary));

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(BaseUrl + TEXT("/upload/image"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), ContentType);
    Request->SetContent(Body);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete, Filename](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            if (!bSucceeded || !Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
            {
                UE_LOG(LogTemp, Error, TEXT("ComfyUI: Upload failed"));
                OnComplete(false, TEXT(""));
                return;
            }

            // Response contains the filename ComfyUI stored it as
            TSharedPtr<FJsonObject> JsonResponse;
            const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
            FString StoredFilename = Filename;
            if (FJsonSerializer::Deserialize(Reader, JsonResponse) && JsonResponse.IsValid())
            {
                FString Name;
                if (JsonResponse->TryGetStringField(TEXT("name"), Name))
                    StoredFilename = Name;
            }

            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Uploaded image as: %s"), *StoredFilename);
            OnComplete(true, StoredFilename);
        });

    Request->ProcessRequest();
}

void SComfyUIPanel::DownloadImageFromComfyUI(const FString& Filename, TFunction<void(bool, const FString&)> OnComplete)
{
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

    FString Url = BaseUrl + TEXT("/view?filename=") + Filename + TEXT("&type=output");

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Url);
    Request->SetVerb(TEXT("GET"));

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete, Filename](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            if (!bSucceeded || !Response.IsValid() || !EHttpResponseCodes::IsOk(Response->GetResponseCode()))
            {
                UE_LOG(LogTemp, Error, TEXT("ComfyUI: Download failed for: %s"), *Filename);
                OnComplete(false, TEXT(""));
                return;
            }

            // Save to local temp folder
            FString TempFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ComfyUITemp"));
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            if (!PlatformFile.DirectoryExists(*TempFolder))
                PlatformFile.CreateDirectoryTree(*TempFolder);

            FString LocalPath = FPaths::Combine(TempFolder, Filename);
            const TArray<uint8>& Content = Response->GetContent();

            if (!FFileHelper::SaveArrayToFile(Content, *LocalPath))
            {
                UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to save downloaded image to: %s"), *LocalPath);
                OnComplete(false, TEXT(""));
                return;
            }

            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Downloaded image to: %s"), *LocalPath);
            OnComplete(true, LocalPath);
        });

    Request->ProcessRequest();
}

FString SComfyUIPanel::GetLocalTempFolder() const
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ComfyUITemp"));
}

// ============================================================================
// Helpers
// ============================================================================




void SComfyUIPanel::StartHistoryPoller(const FString& PromptId, const FComfyWorkflowParams& Params)
{
    PollingPromptId = PromptId;

    if (!GEditor) return;

    FComfyWorkflowParams CapturedParams = Params;
    TWeakPtr<SComfyUIPanel> CapturedWeakThis = WeakThis;

    GEditor->GetTimerManager()->SetTimer(
        PollingTimerHandle,
        [CapturedWeakThis, PromptId, CapturedParams]()
        {
            TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
            if (!Panel.IsValid()) return;

            // If WS already handled this, stop polling
            if (Panel->PollingPromptId != PromptId)
            {
                Panel->StopHistoryPoller();
                return;
            }

            const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
            FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");

            TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
            Request->SetURL(BaseUrl + TEXT("/history/") + PromptId);
            Request->SetVerb(TEXT("GET"));

            Request->OnProcessRequestComplete().BindLambda(
                [CapturedWeakThis, PromptId, CapturedParams](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
                {
                    TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                    if (!Panel.IsValid()) return;

                    // If WS already handled this prompt, bail
                    if (Panel->PollingPromptId != PromptId) return;

                    if (!bSucceeded || !Response.IsValid()) return;

                    TSharedPtr<FJsonObject> History;
                    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
                    if (!FJsonSerializer::Deserialize(Reader, History) || !History.IsValid()) return;

                    // Check if our prompt has a completed entry
                    const TSharedPtr<FJsonObject>* PromptHistory;
                    if (!History->TryGetObjectField(PromptId, PromptHistory)) return;

                    // Check for execution error
                    const TSharedPtr<FJsonObject>* StatusObj;
                    if ((*PromptHistory)->TryGetObjectField(TEXT("status"), StatusObj))
                    {
                        FString StatusStr;
                        if ((*StatusObj)->TryGetStringField(TEXT("status_str"), StatusStr))
                        {
                            if (StatusStr == TEXT("error"))
                            {
                                UE_LOG(LogTemp, Error, TEXT("ComfyUI Poller: Prompt %s errored"), *PromptId);
                                Panel->StopHistoryPoller();
                                Panel->OnWorkflowComplete(false, PromptId, CapturedParams);
                                return;
                            }
                            // Not finished yet
                            if (StatusStr != TEXT("success"))
                                return;
                        }
                    }

                    // Check outputs exist
                    const TSharedPtr<FJsonObject>* Outputs;
                    if (!(*PromptHistory)->TryGetObjectField(TEXT("outputs"), Outputs)) return;
                    if ((*Outputs)->Values.Num() == 0) return;

                    // Looks complete — hand off to OnWorkflowComplete
                    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Poller: Detected completion for prompt %s (WS fallback)"), *PromptId);
                    Panel->StopHistoryPoller();
                    Panel->OnWorkflowComplete(true, PromptId, CapturedParams);
                });

            Request->ProcessRequest();
        },
        5.0f,  // poll every 5 seconds
        true   // looping
    );

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Poller: Started for prompt %s"), *PromptId);
}

void SComfyUIPanel::StopHistoryPoller()
{
    if (GEditor && PollingTimerHandle.IsValid())
    {
        GEditor->GetTimerManager()->ClearTimer(PollingTimerHandle);
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI Poller: Stopped"));
    }
    PollingPromptId = TEXT("");
}
bool SComfyUIPanel::LoadWorkflowFromFile(const FString& RelativePath, TSharedPtr<FJsonObject>& OutWorkflow)
{
    FString WorkflowPath;
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
        WorkflowPath = FPaths::Combine(Plugin->GetBaseDir(), RelativePath);

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

void SComfyUIPanel::UpdateStatus(const FString& Status)
{
    StatusText = Status;
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI Panel: %s"), *Status);
}

void SComfyUIPanel::LoadAndDisplayImage(const FString& FilePath, bool bPreviewB)
{
    UTexture2D* Texture = UComfyUIBlueprintLibrary::LoadImageFromFile(FilePath);
    if (!Texture) return;

    Texture->AddToRoot();

    TSharedPtr<FSlateBrush>& Brush = bPreviewB ? ImageBrushB : ImageBrushA;
    TSharedPtr<SImage>& Preview    = bPreviewB ? PreviewImageB : PreviewImageA;

    if (Brush.IsValid() && Brush->GetResourceObject())
        if (UTexture2D* Old = Cast<UTexture2D>(Brush->GetResourceObject()))
            Old->RemoveFromRoot();

    Brush = MakeShared<FSlateBrush>();
    Brush->SetResourceObject(Texture);
    Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
    Brush->DrawAs = ESlateBrushDrawType::Image;
    Brush->Tiling = ESlateBrushTileType::NoTile;

    if (Preview.IsValid())
        Preview->SetImage(Brush.Get());
}

void SComfyUIPanel::OnPromptTextChanged(const FText& NewText)        { PromptText = NewText.ToString(); }
void SComfyUIPanel::OnNegativePromptTextChanged(const FText& NewText) { NegativePromptText = NewText.ToString(); }
void SComfyUIPanel::OnWidthChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type)  { SelectedWidth = NewSelection; }
void SComfyUIPanel::OnHeightChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type) { SelectedHeight = NewSelection; }
void SComfyUIPanel::OnCustomWidthChanged(int32 NewValue)  { CustomWidth = NewValue; }
void SComfyUIPanel::OnCustomHeightChanged(int32 NewValue) { CustomHeight = NewValue; }

// ============================================================================
// Destructor
// ============================================================================

SComfyUIPanel::~SComfyUIPanel()
{
    if (GEditor && ConnectionTimerHandle.IsValid())
        GEditor->GetTimerManager()->ClearTimer(ConnectionTimerHandle);

    if (GEditor && PollingTimerHandle.IsValid())         
        GEditor->GetTimerManager()->ClearTimer(PollingTimerHandle);

    auto CleanBrush = [](TSharedPtr<FSlateBrush>& Brush) {
        if (Brush.IsValid() && Brush->GetResourceObject())
            if (UTexture2D* T = Cast<UTexture2D>(Brush->GetResourceObject()))
                T->RemoveFromRoot();
    };
    CleanBrush(ImageBrushA);
    CleanBrush(ImageBrushB);
}

#undef LOCTEXT_NAMESPACE
