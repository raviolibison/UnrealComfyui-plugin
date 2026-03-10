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

    ConnectionAttempts = 0;
    PollComfyConnection();

    ChildSlot
    [
        SNew(SScrollBox)
        + SScrollBox::Slot().Padding(10.0f)
        [
            SNew(SVerticalBox)

            // --- Server Control ---
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,15)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
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
                + SHorizontalBox::Slot().AutoWidth().Padding(10,0,0,0).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(TEXT("\u25CF")))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 24))
                    .ColorAndOpacity_Lambda([this]() {
                        return bIsComfyReady ? FLinearColor::Green : FLinearColor::Red;
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
                    .Text(LOCTEXT("360AButton", "Generate 360° HDRI"))
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
                    .Text(LOCTEXT("360BButton", "Generate 360° HDRI"))
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
                        || StatusText.Contains("Launching") || StatusText.Contains("Running"))
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

    if (GEditor)
    {
        FTimerHandle DelayTimer;
        GEditor->GetTimerManager()->SetTimer(
            DelayTimer,
            [CapturedWeakThis, Params]()
            {
                TSharedPtr<SComfyUIPanel> Panel = CapturedWeakThis.Pin();
                if (!Panel.IsValid()) return;

                FString ImagePath = UComfyUIBlueprintLibrary::GetLatestOutputImage(Params.OutputPrefix);
                if (ImagePath.IsEmpty())
                {
                    Panel->UpdateStatus(FString::Printf(
                        TEXT("Error: No output image found with prefix '%s'"), *Params.OutputPrefix));
                    return;
                }

                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Found output image: %s"), *ImagePath);

                if (Params.bUpdatePreview)
                {
                    if (Params.bTargetPreviewB)
                        Panel->PreviewImagePathB = ImagePath;
                    else
                        Panel->PreviewImagePathA = ImagePath;
                    
                    Panel->LoadAndDisplayImage(ImagePath, Params.bTargetPreviewB);
                }

                if (Params.bAutoImport)
                {
                    Panel->ImportImageToProject(ImagePath, Params.OutputPrefix);
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
    WorkflowParams.WorkflowJson   = UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(FluxParams);
    WorkflowParams.OutputPrefix   = CurrentFilenamePrefix;
    WorkflowParams.RunningStatus  = TEXT("Generating image...");
    WorkflowParams.CompleteStatus = TEXT("Done! Import to project or run Img2Img.");
    WorkflowParams.bUpdatePreview = true;
    WorkflowParams.bAutoImport    = false;
    WorkflowParams.bTargetPreviewB = false;

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::StartImg2Img()
{
    
    TSharedPtr<FJsonObject> WorkflowObj;
    if (!LoadWorkflowFromFile(TEXT("ComfyUI_windows_portable/ComfyUI/user/default/workflows/img2img-API.json"), WorkflowObj))
    {
        UpdateStatus(TEXT("Error: img2img workflow file not found"));
        return;
    }

    // Use explicitly picked file, fall back to current preview
    FString SourcePath = Img2ImgInputPath.IsEmpty() ? PreviewImagePathA : Img2ImgInputPath;
    FString Filename = FPaths::GetCleanFilename(SourcePath);

    // If the source is from the output folder, use [output] suffix for LoadImageOutput node
    // If it was picked from elsewhere it will have been copied to input folder
    bool bIsFromOutput = Img2ImgInputPath.IsEmpty(); // preview images are always in output
    FString NodeImageValue = bIsFromOutput
        ? Filename + TEXT(" [output]")
        : Filename;

    // Patch node 32 (LoadImageOutput) or node 25 (LoadImage) depending on source
    FString ImageNodeId = bIsFromOutput ? TEXT("32") : TEXT("32"); // both use 32 in current workflow
    if (TSharedPtr<FJsonObject> ImageNode = WorkflowObj->GetObjectField(ImageNodeId))
    {
        ImageNode->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), NodeImageValue);
    }

    // Patch prompt - node 2
    if (TSharedPtr<FJsonObject> PromptNode = WorkflowObj->GetObjectField(TEXT("2")))
    {
        PromptNode->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("text"), PromptText);
    }

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson   = SerializeWorkflow(WorkflowObj);
    WorkflowParams.OutputPrefix   = TEXT("Flux2-Klein-Edit");
    WorkflowParams.RunningStatus  = TEXT("Running img2img...");
    WorkflowParams.CompleteStatus = TEXT("Img2Img complete! Import to project or run again.");
    WorkflowParams.bUpdatePreview = true;
    WorkflowParams.bAutoImport    = false;
    WorkflowParams.bTargetPreviewB = true;

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::Start360Generation(const FString& SourcePath)
{
    FString Filename = FPaths::GetCleanFilename(SourcePath);
    FString OutputReference = Filename + TEXT(" [output]");

    TSharedPtr<FJsonObject> WorkflowObj;
    if (!LoadWorkflowFromFile(TEXT("ComfyUI_windows_portable/ComfyUI/user/default/workflows/360_Kontext-Small-API.json"), WorkflowObj))
    {
        UpdateStatus(TEXT("Error: 360 workflow file not found"));
        return;
    }

    if (TSharedPtr<FJsonObject> Node147 = WorkflowObj->GetObjectField(TEXT("147")))
        Node147->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), OutputReference);

    if (TSharedPtr<FJsonObject> Node142 = WorkflowObj->GetObjectField(TEXT("142")))
        Node142->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), OutputReference);

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson   = SerializeWorkflow(WorkflowObj);
    WorkflowParams.OutputPrefix   = TEXT("Kontext_Upscale");
    WorkflowParams.RunningStatus  = TEXT("Generating 360° panorama...");
    WorkflowParams.CompleteStatus = TEXT("360° HDRI generated and imported to project!");
    WorkflowParams.bUpdatePreview = false;
    WorkflowParams.bAutoImport    = true; // always auto-import 360 results

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
        FPaths::GetPath(PreviewImagePathA),
        TEXT(""),
        TEXT("Image Files (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg"),
        EFileDialogFlags::None,
        OutFiles
    );

    if (bOpened && OutFiles.Num() > 0)
    {
        FString SelectedFile = OutFiles[0];
        FString Filename = FPaths::GetCleanFilename(SelectedFile);
        FString InputFolder = GetComfyInputFolder();

        if (!InputFolder.IsEmpty())
        {
            FString DestPath = FPaths::Combine(InputFolder, Filename);
            if (IFileManager::Get().Copy(*DestPath, *SelectedFile) == COPY_OK)
            {
                Img2ImgInputPath = SelectedFile;
                PreviewImagePathA = SelectedFile;
                LoadAndDisplayImage(SelectedFile, false);
                UpdateStatus(FString::Printf(TEXT("Input loaded: %s"), *Filename));
            }
            else
            {
                UpdateStatus(TEXT("Error: Failed to copy image to ComfyUI input folder"));
            }
        }
        else
        {
            UpdateStatus(TEXT("Error: Could not find ComfyUI input folder"));
        }
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
    UpdateStatus(TEXT("Submitting 360° workflow..."));
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
// Server Control / Polling
// ============================================================================

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
// Helpers
// ============================================================================

FString SComfyUIPanel::GetComfyInputFolder() const
{
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
    {
        FString InputPath = FPaths::Combine(Plugin->GetBaseDir(),
            TEXT("ComfyUI_windows_portable"), TEXT("ComfyUI"), TEXT("input"));
        if (FPaths::DirectoryExists(InputPath))
        {
            return InputPath;
        }
    }
    return TEXT("");
}

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
    TSharedPtr<SImage>& Preview = bPreviewB ? PreviewImageB : PreviewImageA;

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

void SComfyUIPanel::OnPromptTextChanged(const FText& NewText)       { PromptText = NewText.ToString(); }
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

    auto CleanBrush = [](TSharedPtr<FSlateBrush>& Brush) {
        if (Brush.IsValid() && Brush->GetResourceObject())
            if (UTexture2D* T = Cast<UTexture2D>(Brush->GetResourceObject()))
                T->RemoveFromRoot();
    };
    CleanBrush(ImageBrushA);
    CleanBrush(ImageBrushB);
}

#undef LOCTEXT_NAMESPACE
