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

   
    SelectedResolution.Reset();
    SelectedModelFamilyOption.Reset();
    SelectedImg2ImgUpscaleOption.Reset();
    QwenSelectedSampler.Reset();
    QwenSelectedScheduler.Reset();
    FluxSelectedSampler.Reset();
    FluxSelectedScheduler.Reset();
    PreviewImageA.Reset();
    PreviewImageB.Reset();
    ImageBrushA.Reset();
    ImageBrushB.Reset();
    TabSwitcher.Reset();
    LastImportedTexture.Reset();

    // ---- Model family options -----------------------------------------------
    ModelFamilyOptions.Add(MakeShared<FString>(TEXT("Flux")));
    ModelFamilyOptions.Add(MakeShared<FString>(TEXT("Qwen")));
    SelectedModelFamilyOption = ModelFamilyOptions[1];   // default Qwen
    SelectedModelFamily = EComfyUIModelFamily::Qwen;

    // ---- Img2Img upscale options --------------------------------------------
    Img2ImgUpscaleOptions.Add(MakeShared<FString>(TEXT("No upscale")));
    Img2ImgUpscaleOptions.Add(MakeShared<FString>(TEXT("2x upscale")));
    Img2ImgUpscaleOptions.Add(MakeShared<FString>(TEXT("4x upscale")));
    SelectedImg2ImgUpscaleOption = Img2ImgUpscaleOptions[0];

    // ---- Sampler / scheduler ------------------------------------------------
    for (auto& S : TArray<FString>{ TEXT("euler"), TEXT("euler_ancestral"), TEXT("dpmpp_2m"),
                                     TEXT("dpmpp_2m_sde"), TEXT("dpmpp_3m_sde"), TEXT("heun"),
                                     TEXT("res_multistep") })
        SamplerOptions.Add(MakeShared<FString>(S));

    for (auto& S : TArray<FString>{ TEXT("simple"), TEXT("karras"), TEXT("exponential"),
                                     TEXT("sgm_uniform"), TEXT("beta") })
        SchedulerOptions.Add(MakeShared<FString>(S));

    auto FindOption = [](TArray<TSharedPtr<FString>>& Opts,
        const FString& Val) -> TSharedPtr<FString>
        {
            if (Opts.Num() == 0) return nullptr; 
            for (auto& O : Opts) if (*O == Val) return O;
            return Opts[0];
        };

    QwenSelectedSampler = FindOption(SamplerOptions, QwenSettings.Sampler);
    QwenSelectedScheduler = FindOption(SchedulerOptions, QwenSettings.Scheduler);
    FluxSelectedSampler = FindOption(SamplerOptions, FluxSettings.Sampler);
    FluxSelectedScheduler = FindOption(SchedulerOptions, FluxSettings.Scheduler);

    // ---- Resolution presets (model-aware) -----------------------------------
    RebuildResolutionOptions();   // populates ResolutionOptions, sets SelectedResolution

    // ---- Status / polling ---------------------------------------------------
    StatusText = TEXT("Connecting...");
    WeakThis = SharedThis(this);
    PollComfyConnection();

    ChildSlot
        [
            SNew(SVerticalBox)

                // --- Tab buttons ---
                + SVerticalBox::Slot().AutoHeight().Padding(0)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        [
                            SNew(SButton)
                                .HAlign(HAlign_Center)
                                .Text(LOCTEXT("TabGenerate", "Generate"))
                                .OnClicked_Lambda([this]() {
                                TabSwitcher->SetActiveWidgetIndex(0);
                                return FReply::Handled();
                                    })
                        ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                        [
                            SNew(SButton)
                                .HAlign(HAlign_Center)
                                .Text(LOCTEXT("TabSettings", "Settings"))
                                .OnClicked_Lambda([this]() {
                                TabSwitcher->SetActiveWidgetIndex(1);
                                return FReply::Handled();
                                    })
                        ]
                ]

            // --- Tab content ---
            + SVerticalBox::Slot().FillHeight(1.0f)
                [
                    SAssignNew(TabSwitcher, SWidgetSwitcher)
                        + SWidgetSwitcher::Slot()[BuildGenerateTab()]
                        + SWidgetSwitcher::Slot()[BuildSettingsTab()]
                ]
        ];
}

TSharedRef<SWidget> SComfyUIPanel::BuildGenerateTab()
{
    return SNew(SScrollBox)
        + SScrollBox::Slot().Padding(10.0f)
        [
            SNew(SVerticalBox)

                // --- Connection Status ---
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 15)
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
                    + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0).VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                                .Text_Lambda([this]() {
                                return bIsComfyReady
                                    ? LOCTEXT("ServerReady", "ComfyUI Connected")
                                    : LOCTEXT("ServerOffline", "ComfyUI Offline");
                                    })
                        ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                                .Text_Lambda([this]() {
                                return FText::FromString(FString::Printf(TEXT("Model: %s"),
                                    SelectedModelFamily == EComfyUIModelFamily::Qwen ? TEXT("Qwen") : TEXT("Flux")));
                                    })
                                .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                        ]
                ]

            // --- Prompt ---
            + SVerticalBox::Slot().AutoHeight()
                [SNew(STextBlock).Text(LOCTEXT("PromptLabel", "Prompt:"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 10)
                [
                    SNew(SEditableTextBox)
                        .HintText(LOCTEXT("PromptHint", "Describe the image..."))
                        .Text(FText::FromString(PromptText))
                        .OnTextChanged(this, &SComfyUIPanel::OnPromptTextChanged)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(STextBlock).Text(LOCTEXT("NegPromptLabel", "Negative Prompt:"))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 10)
                [
                    SNew(SEditableTextBox)
                        .HintText(LOCTEXT("NegPromptHint", "What to avoid..."))
                        .Text(FText::FromString(NegativePromptText))
                        .OnTextChanged(this, &SComfyUIPanel::OnNegativePromptTextChanged)
                ]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
                [
                    SNew(SHorizontalBox)

                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(STextBlock).Text(LOCTEXT("ResLabel", "Resolution: "))
                        ]

                        + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                        [
                            SNew(SComboBox<TSharedPtr<FResolutionOption>>)
                                .OptionsSource(&ResolutionOptions)
                                .OnSelectionChanged(this, &SComfyUIPanel::OnResolutionChanged)
                                .OnGenerateWidget_Lambda([](TSharedPtr<FResolutionOption> Item) -> TSharedRef<SWidget> {
                                if (!Item.IsValid())
                                    return SNew(STextBlock).Text(FText::GetEmpty());
                                return SNew(STextBlock).Text(FText::FromString(Item->Label));
                                    })
                                // Do NOT pass InitiallySelectedItem — let the combo default to the
                                // first entry.  We drive the displayed text via the child lambda.
                                [
                                    SNew(STextBlock).Text_Lambda([this]() -> FText {
                                        if (!SelectedResolution.IsValid())
                                            return FText::GetEmpty();
                                        return FText::FromString(SelectedResolution->Label);
                                        })
                                ]
                        ]

                    // "W:" + custom width (hidden unless Custom selected)
                    + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                                .Text(LOCTEXT("CustomWLabel", "W:"))
                                .Visibility_Lambda([this]() {
                                return IsCustomResSelected() ? EVisibility::Visible : EVisibility::Collapsed;
                                    })
                        ]
                    + SHorizontalBox::Slot().Padding(4, 0, 0, 0).AutoWidth()
                        [
                            SNew(SNumericEntryBox<int32>)
                                .Visibility_Lambda([this]() {
                                return IsCustomResSelected() ? EVisibility::Visible : EVisibility::Collapsed;
                                    })
                                .Value_Lambda([this]() { return TOptional<int32>(CustomWidth); })
                                .OnValueChanged_Lambda([this](int32 V) { CustomWidth = FMath::Max(64, V); })
                                .MinValue(64).MaxValue(8192).MinDesiredValueWidth(70)
                        ]

                    // "H:" + custom height (hidden unless Custom selected)
                    + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                                .Text(LOCTEXT("CustomHLabel", "H:"))
                                .Visibility_Lambda([this]() {
                                return IsCustomResSelected() ? EVisibility::Visible : EVisibility::Collapsed;
                                    })
                        ]
                    + SHorizontalBox::Slot().Padding(4, 0, 0, 0).AutoWidth()
                        [
                            SNew(SNumericEntryBox<int32>)
                                .Visibility_Lambda([this]() {
                                return IsCustomResSelected() ? EVisibility::Visible : EVisibility::Collapsed;
                                    })
                                .Value_Lambda([this]() { return TOptional<int32>(CustomHeight); })
                                .OnValueChanged_Lambda([this](int32 V) { CustomHeight = FMath::Max(64, V); })
                                .MinValue(64).MaxValue(8192).MinDesiredValueWidth(70)
                        ]
                ]

            // Warning for custom resolution
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)
                [
                    SNew(STextBlock)
                        .Visibility_Lambda([this]() {
                        return IsCustomResSelected() ? EVisibility::Visible : EVisibility::Collapsed;
                            })
                        .Text(LOCTEXT("CustomResWarning",
                            "Custom resolution may produce lower quality. Model works best at the listed presets."))
                        .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
                        .ColorAndOpacity(FLinearColor(1.f, 0.75f, 0.f, 0.85f))
                        .AutoWrapText(true)
                ]

            // --- Generate / Browse ---
            + SVerticalBox::Slot().AutoHeight().Padding(0, 15, 0, 5)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("GenerateButton", "Generate Image"))
                                .OnClicked(this, &SComfyUIPanel::OnGenerateClicked)
                                .IsEnabled_Lambda([this]() { return bIsComfyReady; })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("BrowseButton", "Browse Input..."))
                                .OnClicked(this, &SComfyUIPanel::OnImg2ImgBrowseClicked)
                                .ToolTipText(LOCTEXT("BrowseTooltip", "Load an existing image into Preview A"))
                        ]
                ]

            // --- Preview A ---
            + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 5).HAlign(HAlign_Center)
                [
                    SNew(SBox)
                        .WidthOverride_Lambda([this]() -> FOptionalSize {
                        if (ImageBrushA.IsValid() && ImageBrushA->GetResourceObject())
                        {
                            const FVector2D S = ImageBrushA->ImageSize;
                            if (S.X > 0 && S.Y > 0)
                                return FOptionalSize(S.X * FMath::Min(FMath::Min(800.f / S.X, 600.f / S.Y), 1.f));
                        }
                        return FOptionalSize();
                            })
                        .HeightOverride_Lambda([this]() -> FOptionalSize {
                        if (ImageBrushA.IsValid() && ImageBrushA->GetResourceObject())
                        {
                            const FVector2D S = ImageBrushA->ImageSize;
                            if (S.X > 0 && S.Y > 0)
                                return FOptionalSize(S.Y * FMath::Min(FMath::Min(800.f / S.X, 600.f / S.Y), 1.f));
                        }
                        return FOptionalSize();
                            })
                        [SAssignNew(PreviewImageA, SImage)]
                ]

            // --- Preview A Actions ---
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 15)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("ImportAButton", "Import to Project"))
                                .IsEnabled_Lambda([this]() { return !PreviewImagePathA.IsEmpty(); })
                                .OnClicked_Lambda([this]() { return OnImportClicked(PreviewImagePathA); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("ComposureAButton", "Apply to Composure"))
                                .IsEnabled_Lambda([this]() { return !PreviewImagePathA.IsEmpty(); })
                                .OnClicked_Lambda([this]() { return OnApplyToComposureClicked(PreviewImagePathA); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("360AButton", "Generate 360\u00b0 HDRI"))
                                .IsEnabled_Lambda([this]() { return bIsComfyReady && !PreviewImagePathA.IsEmpty(); })
                                .OnClicked_Lambda([this]() { return OnGenerate360Clicked(PreviewImagePathA); })
                        ]
                ]

            // --- Img2Img ---
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
                [
                    SNew(STextBlock)
                        .Text(LOCTEXT("Img2ImgLabel", "Refine / Edit (Img2Img):"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 10)
                [
                    SNew(SEditableTextBox)
                        .HintText(LOCTEXT("Img2ImgHint", "Describe the edit..."))
                        .Text(FText::FromString(Img2ImgPromptText))
                        .OnTextChanged_Lambda([this](const FText& T) { Img2ImgPromptText = T.ToString(); })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 5)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(STextBlock).Text(LOCTEXT("Img2ImgUpscaleLabel", "Upscale: "))]
                        + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                        [
                            SNew(SComboBox<TSharedPtr<FString>>)
                                .OptionsSource(&Img2ImgUpscaleOptions)
                                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Val, ESelectInfo::Type) {
                                SelectedImg2ImgUpscaleOption = Val;
                                if (*Val == TEXT("2x Upscale"))      Img2ImgUpscaleMode = EUpscaleMode::TwoX;
                                else if (*Val == TEXT("4x Upscale")) Img2ImgUpscaleMode = EUpscaleMode::FourX;
                                else                                  Img2ImgUpscaleMode = EUpscaleMode::None;
                                    })
                                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                                return SNew(STextBlock).Text(FText::FromString(*Item));
                                    })
                                .InitiallySelectedItem(SelectedImg2ImgUpscaleOption)
                                [SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(SelectedImg2ImgUpscaleOption.IsValid()
                                    ? *SelectedImg2ImgUpscaleOption : TEXT("No Upscale"));
                                    })]
                        ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                [
                    SNew(SButton)
                        .Text(LOCTEXT("Img2ImgButton", "Run Img2Img"))
                        .IsEnabled_Lambda([this]() { return bIsComfyReady && !PreviewImagePathA.IsEmpty(); })
                        .OnClicked(this, &SComfyUIPanel::OnImg2ImgClicked)
                ]

                // --- Preview B ---
                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 5).HAlign(HAlign_Center)
                [
                    SNew(SBox)
                        .WidthOverride_Lambda([this]() -> FOptionalSize {
                        if (ImageBrushB.IsValid() && ImageBrushB->GetResourceObject())
                        {
                            const FVector2D S = ImageBrushB->ImageSize;
                            if (S.X > 0 && S.Y > 0)
                                return FOptionalSize(S.X * FMath::Min(FMath::Min(800.f / S.X, 600.f / S.Y), 1.f));
                        }
                        return FOptionalSize();
                            })
                        .HeightOverride_Lambda([this]() -> FOptionalSize {
                        if (ImageBrushB.IsValid() && ImageBrushB->GetResourceObject())
                        {
                            const FVector2D S = ImageBrushB->ImageSize;
                            if (S.X > 0 && S.Y > 0)
                                return FOptionalSize(S.Y * FMath::Min(FMath::Min(800.f / S.X, 600.f / S.Y), 1.f));
                        }
                        return FOptionalSize();
                            })
                        [SAssignNew(PreviewImageB, SImage)]
                ]

            // --- Preview B Actions ---
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 15)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("ImportBButton", "Import to Project"))
                                .IsEnabled_Lambda([this]() { return !PreviewImagePathB.IsEmpty(); })
                                .OnClicked_Lambda([this]() { return OnImportClicked(PreviewImagePathB); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("ComposureBButton", "Apply to Composure"))
                                .IsEnabled_Lambda([this]() { return !PreviewImagePathB.IsEmpty(); })
                                .OnClicked_Lambda([this]() { return OnApplyToComposureClicked(PreviewImagePathB); })
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SButton)
                                .Text(LOCTEXT("360BButton", "Generate 360\u00b0 HDRI"))
                                .IsEnabled_Lambda([this]() { return bIsComfyReady && !PreviewImagePathB.IsEmpty(); })
                                .OnClicked_Lambda([this]() { return OnGenerate360Clicked(PreviewImagePathB); })
                        ]
                ]

            // --- Status ---
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5)
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
        ];
}

TSharedRef<SWidget> SComfyUIPanel::BuildSettingsTab()
{
    auto MakeSectionHeader = [](const FText& Label) -> TSharedRef<SWidget>
        {
            return SNew(SBorder)
                .Padding(FMargin(6, 4))
                .BorderBackgroundColor(FLinearColor(0.1f, 0.1f, 0.1f))
                [
                    SNew(STextBlock)
                        .Text(Label)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                ];
        };

    auto MakeLabel = [](const FText& Label) -> TSharedRef<SWidget>
        {
            return SNew(SBox).WidthOverride(120).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(Label)];
        };

    return SNew(SScrollBox)
        + SScrollBox::Slot().Padding(10.0f)
        [
            SNew(SVerticalBox)

                // ---- Model selector ----
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 15)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("ModelLabel", "Active Model:"))]
                        + SHorizontalBox::Slot().Padding(10, 0, 0, 0).AutoWidth()
                        [
                            SNew(SComboBox<TSharedPtr<FString>>)
                                .OptionsSource(&ModelFamilyOptions)
                                .OnSelectionChanged(this, &SComfyUIPanel::OnModelFamilyChanged)
                                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                                return SNew(STextBlock).Text(FText::FromString(*Item));
                                    })
                                .InitiallySelectedItem(SelectedModelFamilyOption)
                                [SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(*SelectedModelFamilyOption);
                                    })]
                        ]
                ]

            // ================================================================
            // Qwen Settings
            // ================================================================
            +SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [MakeSectionHeader(LOCTEXT("QwenSection", "Qwen Settings"))]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("QwenSteps", "Steps:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SNumericEntryBox<int32>)
                                .Value_Lambda([this]() { return TOptional<int32>(QwenSettings.Steps); })
                                .OnValueChanged_Lambda([this](int32 V) { QwenSettings.Steps = V; })
                                .MinValue(1).MaxValue(100).MinDesiredValueWidth(60)
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("QwenCFG", "CFG Scale:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SNumericEntryBox<float>)
                                .Value_Lambda([this]() { return TOptional<float>(QwenSettings.CFGScale); })
                                .OnValueChanged_Lambda([this](float V) { QwenSettings.CFGScale = V; })
                                .MinValue(1.0f).MaxValue(20.0f).MinDesiredValueWidth(60)
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("QwenShift", "Shift:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SNumericEntryBox<float>)
                                .Value_Lambda([this]() { return TOptional<float>(QwenSettings.Shift); })
                                .OnValueChanged_Lambda([this](float V) { QwenSettings.Shift = V; })
                                .MinValue(0.0f).MaxValue(20.0f).MinDesiredValueWidth(60)
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("QwenSampler", "Sampler:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SComboBox<TSharedPtr<FString>>)
                                .OptionsSource(&SamplerOptions)
                                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Val, ESelectInfo::Type) {
                                QwenSelectedSampler = Val;
                                QwenSettings.Sampler = *Val;
                                    })
                                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                                return SNew(STextBlock).Text(FText::FromString(*Item));
                                    })
                                .InitiallySelectedItem(QwenSelectedSampler)
                                [SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(QwenSelectedSampler.IsValid() ? *QwenSelectedSampler : TEXT("res_multistep"));
                                    })]
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 20)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("QwenScheduler", "Scheduler:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SComboBox<TSharedPtr<FString>>)
                                .OptionsSource(&SchedulerOptions)
                                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Val, ESelectInfo::Type) {
                                QwenSelectedScheduler = Val;
                                QwenSettings.Scheduler = *Val;
                                    })
                                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                                return SNew(STextBlock).Text(FText::FromString(*Item));
                                    })
                                .InitiallySelectedItem(QwenSelectedScheduler)
                                [SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(QwenSelectedScheduler.IsValid() ? *QwenSelectedScheduler : TEXT("simple"));
                                    })]
                        ]
                ]

            // ================================================================
            // Flux Settings
            // ================================================================
            +SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [MakeSectionHeader(LOCTEXT("FluxSection", "Flux Settings"))]

                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("FluxSteps", "Steps:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SNumericEntryBox<int32>)
                                .Value_Lambda([this]() { return TOptional<int32>(FluxSettings.Steps); })
                                .OnValueChanged_Lambda([this](int32 V) { FluxSettings.Steps = V; })
                                .MinValue(1).MaxValue(50).MinDesiredValueWidth(60)
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("FluxCFG", "CFG Scale:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SNumericEntryBox<float>)
                                .Value_Lambda([this]() { return TOptional<float>(FluxSettings.CFGScale); })
                                .OnValueChanged_Lambda([this](float V) { FluxSettings.CFGScale = V; })
                                .MinValue(0.0f).MaxValue(10.0f).MinDesiredValueWidth(60)
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("FluxSampler", "Sampler:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SComboBox<TSharedPtr<FString>>)
                                .OptionsSource(&SamplerOptions)
                                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Val, ESelectInfo::Type) {
                                FluxSelectedSampler = Val;
                                FluxSettings.Sampler = *Val;
                                    })
                                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                                return SNew(STextBlock).Text(FText::FromString(*Item));
                                    })
                                .InitiallySelectedItem(FluxSelectedSampler)
                                [SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(FluxSelectedSampler.IsValid() ? *FluxSelectedSampler : TEXT("euler"));
                                    })]
                        ]
                ]

            + SVerticalBox::Slot().AutoHeight().Padding(0, 4)
                [
                    SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [MakeLabel(LOCTEXT("FluxScheduler", "Scheduler:"))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(10, 0, 0, 0)
                        [
                            SNew(SComboBox<TSharedPtr<FString>>)
                                .OptionsSource(&SchedulerOptions)
                                .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Val, ESelectInfo::Type) {
                                FluxSelectedScheduler = Val;
                                FluxSettings.Scheduler = *Val;
                                    })
                                .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                                return SNew(STextBlock).Text(FText::FromString(*Item));
                                    })
                                .InitiallySelectedItem(FluxSelectedScheduler)
                                [SNew(STextBlock).Text_Lambda([this]() {
                                return FText::FromString(FluxSelectedScheduler.IsValid() ? *FluxSelectedScheduler : TEXT("normal"));
                                    })]
                        ]
                ]
        ];
}

// ============================================================================
// Generic Workflow System
// ============================================================================

void SComfyUIPanel::AddResPreset(const FString& Label,
    int32 GenW, int32 GenH,
    EUpscaleMode Mode,
    int32 FinalW, int32 FinalH)
{
    auto Opt = MakeShared<FResolutionOption>();
    Opt->Label = Label;
    Opt->GenWidth = GenW;
    Opt->GenHeight = GenH;
    Opt->FinalWidth = FinalW;
    Opt->FinalHeight = FinalH;
    Opt->UpscaleMode = Mode;
    Opt->bIsCustom = false;
    ResolutionOptions.Add(Opt);
}

void SComfyUIPanel::RebuildResolutionOptions()
{
    ResolutionOptions.Empty();

    if (SelectedModelFamily == EComfyUIModelFamily::Qwen)
    {
        // ── Native generation (no upscale) ──────────────────────────────────
        //   Qwen 2512 native training res: 1328×1328 (~1.77 MP)
        //   Practical fast alternative: 1024×1024
        //   Non-square targets same ~1.77 MP budget → 1536×864, 864×1536
        AddResPreset(TEXT("1024×1024"), 1024, 1024, EUpscaleMode::None, 1024, 1024);
        AddResPreset(TEXT("1328×1328 (native)"), 1328, 1328, EUpscaleMode::None, 1328, 1328);
        AddResPreset(TEXT("1280×720"), 1280, 720, EUpscaleMode::None, 1280, 720);
        AddResPreset(TEXT("1536×864"), 1536, 864, EUpscaleMode::None, 1536, 864);
        AddResPreset(TEXT("720×1280"), 720, 1280, EUpscaleMode::None, 720, 1280);
        AddResPreset(TEXT("864×1536"), 864, 1536, EUpscaleMode::None, 864, 1536);

        // ── 2x upscale ───────────────────────────────────────────────────────
        AddResPreset(TEXT("2048×2048"), 1024, 1024, EUpscaleMode::TwoX, 2048, 2048);
        AddResPreset(TEXT("2656×2656"), 1328, 1328, EUpscaleMode::TwoX, 2656, 2656);
        AddResPreset(TEXT("2560×1440"), 1280, 720, EUpscaleMode::TwoX, 2560, 1440);
        AddResPreset(TEXT("3072×1728"), 1536, 864, EUpscaleMode::TwoX, 3072, 1728);
        AddResPreset(TEXT("1440×2560"), 720, 1280, EUpscaleMode::TwoX, 1440, 2560);
        AddResPreset(TEXT("1728×3072"), 864, 1536, EUpscaleMode::TwoX, 1728, 3072);

        // ── 4x upscale ───────────────────────────────────────────────────────
        AddResPreset(TEXT("4096×4096"), 1024, 1024, EUpscaleMode::FourX, 4096, 4096);
        AddResPreset(TEXT("5120×2880"), 1280, 720, EUpscaleMode::FourX, 5120, 2880);
        AddResPreset(TEXT("2880×5120"), 720, 1280, EUpscaleMode::FourX, 2880, 5120);
    }
    else // Flux.2 Klein 4B
    {
        // ── Native generation (no upscale) ──────────────────────────────────
        //   Flux.2 Klein supports up to 4 MP; sweet spot 1–2 MP.
        AddResPreset(TEXT("1024×1024"), 1024, 1024, EUpscaleMode::None, 1024, 1024);
        AddResPreset(TEXT("1280×720"), 1280, 720, EUpscaleMode::None, 1280, 720);
        AddResPreset(TEXT("1536×864"), 1536, 864, EUpscaleMode::None, 1536, 864);
        AddResPreset(TEXT("1920×1080"), 1920, 1080, EUpscaleMode::None, 1920, 1080);
        AddResPreset(TEXT("720×1280"), 720, 1280, EUpscaleMode::None, 720, 1280);
        AddResPreset(TEXT("864×1536"), 864, 1536, EUpscaleMode::None, 864, 1536);
        AddResPreset(TEXT("1080×1920"), 1080, 1920, EUpscaleMode::None, 1080, 1920);

        // ── 2x upscale ───────────────────────────────────────────────────────
        AddResPreset(TEXT("2048×2048"), 1024, 1024, EUpscaleMode::TwoX, 2048, 2048);
        AddResPreset(TEXT("2560×1440"), 1280, 720, EUpscaleMode::TwoX, 2560, 1440);
        AddResPreset(TEXT("3072×1728"), 1536, 864, EUpscaleMode::TwoX, 3072, 1728);
        AddResPreset(TEXT("3840×2160"), 1920, 1080, EUpscaleMode::TwoX, 3840, 2160);
        AddResPreset(TEXT("1440×2560"), 720, 1280, EUpscaleMode::TwoX, 1440, 2560);
        AddResPreset(TEXT("1728×3072"), 864, 1536, EUpscaleMode::TwoX, 1728, 3072);
        AddResPreset(TEXT("2160×3840"), 1080, 1920, EUpscaleMode::TwoX, 2160, 3840);

        // ── 4x upscale ───────────────────────────────────────────────────────
        AddResPreset(TEXT("4096×4096"), 1024, 1024, EUpscaleMode::FourX, 4096, 4096);
        AddResPreset(TEXT("5120×2880"), 1280, 720, EUpscaleMode::FourX, 5120, 2880);
        AddResPreset(TEXT("7680×4320"), 1920, 1080, EUpscaleMode::FourX, 7680, 4320);
        AddResPreset(TEXT("2880×5120"), 720, 1280, EUpscaleMode::FourX, 2880, 5120);
        AddResPreset(TEXT("4320×7680"), 1080, 1920, EUpscaleMode::FourX, 4320, 7680);
    }

    // ── Custom (always last) ─────────────────────────────────────────────────
    auto CustomOpt = MakeShared<FResolutionOption>();
    CustomOpt->Label = TEXT("Custom...");
    CustomOpt->GenWidth = CustomWidth;
    CustomOpt->GenHeight = CustomHeight;
    CustomOpt->FinalWidth = CustomWidth;
    CustomOpt->FinalHeight = CustomHeight;
    CustomOpt->UpscaleMode = EUpscaleMode::None;
    CustomOpt->bIsCustom = true;
    ResolutionOptions.Add(CustomOpt);

    // Safety check — should never happen, but guard anyway
    if (ResolutionOptions.Num() == 0)
    {
        auto Fallback = MakeShared<FResolutionOption>();
        Fallback->Label = TEXT("1024×1024");
        Fallback->GenWidth = 1024;
        Fallback->GenHeight = 1024;
        Fallback->FinalWidth = 1024;
        Fallback->FinalHeight = 1024;
        ResolutionOptions.Add(Fallback);
    }

    // Try to restore previous selection by label; always fall back to index 0.
    FString PrevLabel = SelectedResolution.IsValid() ? SelectedResolution->Label : TEXT("");
    SelectedResolution = ResolutionOptions[0];   // guaranteed non-null

    for (auto& Opt : ResolutionOptions)
    {
        if (Opt.IsValid() && Opt->Label == PrevLabel)
        {
            SelectedResolution = Opt;
            break;
        }
    }

    // Final paranoia check
    check(SelectedResolution.IsValid());
}

void SComfyUIPanel::GetEffectiveResolution(int32& OutGenW, int32& OutGenH,
    int32& OutFinalW, int32& OutFinalH) const
{
    if (SelectedResolution.IsValid() && SelectedResolution->bIsCustom)
    {
        OutGenW = OutFinalW = CustomWidth;
        OutGenH = OutFinalH = CustomHeight;
    }
    else if (SelectedResolution.IsValid())
    {
        OutGenW = SelectedResolution->GenWidth;
        OutGenH = SelectedResolution->GenHeight;
        OutFinalW = SelectedResolution->FinalWidth;
        OutFinalH = SelectedResolution->FinalHeight;
    }
    else
    {
        OutGenW = OutFinalW = 1024;
        OutGenH = OutFinalH = 1024;
    }
}

bool SComfyUIPanel::IsCustomResSelected() const
{
    return SelectedResolution.IsValid() && SelectedResolution->bIsCustom;
}

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

    // Clean up the watcher whether WS fired or poller fired
    if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
    {
        TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
        if (WSHandler.IsValid())
            WSHandler->UnwatchPrompt(PromptId);
    }

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
    if (!SelectedResolution.IsValid()) return;

    int32 GenW, GenH, FinalW, FinalH;
    GetEffectiveResolution(GenW, GenH, FinalW, FinalH);

    const EUpscaleMode UpscaleMode =
        IsCustomResSelected() ? EUpscaleMode::None : SelectedResolution->UpscaleMode;

    UE_LOG(LogTemp, Warning,
        TEXT("ComfyUI: StartGeneration — GenW=%d GenH=%d FinalW=%d FinalH=%d UpscaleMode=%d"),
        GenW, GenH, FinalW, FinalH, (int32)UpscaleMode);

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.OutputPrefix = CurrentFilenamePrefix;
    WorkflowParams.RunningStatus = TEXT("Generating image...");
    WorkflowParams.CompleteStatus = TEXT("Done! Import to project or run Img2Img.");
    WorkflowParams.bUpdatePreview = true;
    WorkflowParams.bAutoImport = false;
    WorkflowParams.bTargetPreviewB = false;

    const int32 Seed = FMath::Abs((int32)(FDateTime::Now().GetTicks() % MAX_int32));

    // =========================================================================
    // QWEN  (qwen_Image_2512_API.json)
    // =========================================================================
    // Node map:
    //   248  EmptySD3LatentImage  — width, height
    //   249  CLIPTextEncode       — positive prompt text
    //   250  CLIPTextEncode       — negative prompt text
    //   252  PrimitiveFloat       — CFG value
    //   260  ModelSamplingAuraFlow — shift
    //   261  KSampler             — seed, steps, sampler_name, scheduler
    //   262  VAEDecode
    //   263  UpscaleModelLoader   — model_name
    //   264  ImageUpscaleWithModel — image from 262, upscale_model from 263
    //   268  SaveImage            — images from 264 (or 262 when no upscale)
    // =========================================================================
    if (SelectedModelFamily == EComfyUIModelFamily::Qwen)
    {
        TSharedPtr<FJsonObject> WorkflowObj;
        if (!LoadWorkflowFromFile(TEXT("workflows/qwen_Image_2512_API.json"), WorkflowObj))
        {
            UpdateStatus(TEXT("Error: Qwen generate workflow not found"));
            return;
        }

        // Latent resolution
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("248")))
        {
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("width"), GenW);
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("height"), GenH);
        }

        // Positive prompt
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("249")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("text"), PromptText);

        // CFG
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("252")))
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("value"), QwenSettings.CFGScale);

        // Shift
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("260")))
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("shift"), QwenSettings.Shift);

        // KSampler
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("261")))
        {
            auto Inputs = N->GetObjectField(TEXT("inputs"));
            Inputs->SetNumberField(TEXT("seed"), Seed);
            Inputs->SetNumberField(TEXT("steps"), QwenSettings.Steps);
            Inputs->SetStringField(TEXT("sampler_name"), QwenSettings.Sampler);
            Inputs->SetStringField(TEXT("scheduler"), QwenSettings.Scheduler);
        }

        // Filename prefix + upscale routing
        if (UpscaleMode == EUpscaleMode::None)
        {
            // Disable the upscale nodes and wire SaveImage directly to VAEDecode
            DisableNode(WorkflowObj, TEXT("263"));
            DisableNode(WorkflowObj, TEXT("264"));
            PatchSaveImageInput(WorkflowObj, TEXT("268"), TEXT("262"), 0);
        }
        else
        {
            // Set the upscale model; 264 already wires 263→262→268
            if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("263")))
                N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("model_name"),
                    GetUpscalerModel(UpscaleMode));
        }

        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("268")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("filename_prefix"),
                CurrentFilenamePrefix);

        WorkflowParams.WorkflowJson = SerializeWorkflow(WorkflowObj);
    }

    // =========================================================================
    // FLUX  (Flux_2_klein_API.json)
    // =========================================================================
    // Node map:
    //   17  EmptyFlux2LatentImage — width, height
    //   18  RandomNoise          — noise_seed
    //   19  Flux2Scheduler       — steps, width, height
    //   20  KSamplerSelect       — sampler_name
    //   22  VAEDecode
    //   24  UpscaleModelLoader   — model_name
    //   25  ImageUpscaleWithModel — image from 22, upscale_model from 24
    //   33  SaveImage            — images from 25 (or 22 when no upscale)
    // =========================================================================
    else
    {
        TSharedPtr<FJsonObject> WorkflowObj;
        if (!LoadWorkflowFromFile(TEXT("workflows/Flux_2_klein_API.json"), WorkflowObj))
        {
            UpdateStatus(TEXT("Error: Flux generate workflow not found"));
            return;
        }

        // Latent resolution
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("17")))
        {
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("width"), GenW);
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("height"), GenH);
        }

        // Scheduler resolution + steps (Flux2Scheduler needs matching dims)
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("19")))
        {
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("width"), GenW);
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("height"), GenH);
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("steps"), FluxSettings.Steps);
        }

        // Seed
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("18")))
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("noise_seed"), Seed);

        // Sampler
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("20")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("sampler_name"),
                FluxSettings.Sampler);

        // Positive prompt
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("14")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("text"), PromptText);

        // Filename prefix + upscale routing
        if (UpscaleMode == EUpscaleMode::None)
        {
            // Disable the upscale nodes and wire SaveImage directly to VAEDecode
            DisableNode(WorkflowObj, TEXT("24"));
            DisableNode(WorkflowObj, TEXT("25"));
            PatchSaveImageInput(WorkflowObj, TEXT("33"), TEXT("22"), 0);
        }
        else
        {
            // Set the upscale model; 25 already wires 24→22→33
            if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("24")))
                N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("model_name"),
                    GetUpscalerModel(UpscaleMode));
        }

        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("33")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("filename_prefix"),
                CurrentFilenamePrefix);

        WorkflowParams.WorkflowJson = SerializeWorkflow(WorkflowObj);
    }

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::StartImg2Img()
{
    if (PreviewImagePathA.IsEmpty()) return;
    if (!SelectedResolution.IsValid()) return;

    const EUpscaleMode UpscaleMode = Img2ImgUpscaleMode;

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.OutputPrefix = TEXT("Edit");
    WorkflowParams.RunningStatus = TEXT("Running img2img...");
    WorkflowParams.CompleteStatus = TEXT("Edit complete!");
    WorkflowParams.bUpdatePreview = true;
    WorkflowParams.bAutoImport = false;
    WorkflowParams.bTargetPreviewB = true;

    FString Filename = FPaths::GetCleanFilename(PreviewImagePathA);
    bool bIsOutput = PreviewImagePathA.StartsWith(GetLocalTempFolder());
    FString ImageValue = bIsOutput ? Filename + TEXT(" [output]") : Filename;

    int32 Seed = FMath::Abs((int32)(FDateTime::Now().GetTicks() % MAX_int32));

    if (SelectedModelFamily == EComfyUIModelFamily::Qwen)
    {
        TSharedPtr<FJsonObject> WorkflowObj;
        if (!LoadWorkflowFromFile(TEXT("workflows/qwen_image_edit_2511_API.json"), WorkflowObj))
        {
            UpdateStatus(TEXT("Error: Qwen edit workflow not found"));
            return;
        }

        // Fix model name
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("174")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("unet_name"),
                TEXT("qwen_image_edit_2511_fp8mixed.safetensors"));

        // Patch image
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("41")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), ImageValue);

        // Patch prompt
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("175")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("prompt"), Img2ImgPromptText);

        // Patch seed
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("183")))
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("seed"), Seed);

        // Patch sampler settings
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("183")))
        {
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("sampler_name"), QwenSettings.Sampler);
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("scheduler"), QwenSettings.Scheduler);
        }
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("181")))
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("value"), QwenSettings.Steps);
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("182")))
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("value"), QwenSettings.CFGScale);

        // Fix missing VAE on node 186
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("186")))
        {
            TArray<TSharedPtr<FJsonValue>> VaeRef;
            VaeRef.Add(MakeShared<FJsonValueString>(TEXT("172")));
            VaeRef.Add(MakeShared<FJsonValueNumber>(0));
            N->GetObjectField(TEXT("inputs"))->SetArrayField(TEXT("vae"), VaeRef);
        }

        // Patch filename
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("9")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("filename_prefix"), TEXT("Edit"));

        // Handle upscaler
        if (UpscaleMode == EUpscaleMode::None)
        {
            DisableNode(WorkflowObj, TEXT("187"));
            DisableNode(WorkflowObj, TEXT("188"));
            PatchSaveImageInput(WorkflowObj, TEXT("9"), TEXT("184"), 0);
        }
        else
        {
            if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("187")))
                N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("model_name"), GetUpscalerModel(UpscaleMode));
        }

        WorkflowParams.WorkflowJson = SerializeWorkflow(WorkflowObj);
    }
    else // Flux
    {
        TSharedPtr<FJsonObject> WorkflowObj;
        if (!LoadWorkflowFromFile(TEXT("workflows/img2img-API.json"), WorkflowObj))
        {
            UpdateStatus(TEXT("Error: Flux img2img workflow not found"));
            return;
        }

        // Patch image
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("32")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), ImageValue);

        // Patch prompt
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("2")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("text"), Img2ImgPromptText);

        // Patch seed
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("16")))
            N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("seed"), Seed);

        // Patch filename
        if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("3")))
            N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("filename_prefix"), TEXT("Edit"));

        // Handle upscaler
        if (UpscaleMode == EUpscaleMode::None)
        {
            DisableNode(WorkflowObj, TEXT("18"));
            DisableNode(WorkflowObj, TEXT("20"));
            PatchSaveImageInput(WorkflowObj, TEXT("3"), TEXT("14"), 0);
        }
        else
        {
            if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("18")))
                N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("model_name"), GetUpscalerModel(UpscaleMode));
        }

        WorkflowParams.WorkflowJson = SerializeWorkflow(WorkflowObj);
    }

    SubmitWorkflow(WorkflowParams);
}

void SComfyUIPanel::Start360Generation(const FString& SourcePath)
{
    TSharedPtr<FJsonObject> WorkflowObj;
    if (!LoadWorkflowFromFile(TEXT("workflows/qwen_image_edit_2511_360_API.json"), WorkflowObj))
    {
        UpdateStatus(TEXT("Error: 360 workflow not found"));
        return;
    }

    FString Filename = FPaths::GetCleanFilename(SourcePath);
    bool bIsOutput = SourcePath.StartsWith(GetLocalTempFolder());
    FString ImageValue = bIsOutput ? Filename + TEXT(" [output]") : Filename;

    if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("41")))
        N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("image"), ImageValue);

    int32 Seed = FMath::Abs((int32)(FDateTime::Now().GetTicks() % MAX_int32));
    if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("183")))
        N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("seed"), Seed);
    if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("214")))
        N->GetObjectField(TEXT("inputs"))->SetNumberField(TEXT("seed"), Seed + 1);

    if (TSharedPtr<FJsonObject> N = WorkflowObj->GetObjectField(TEXT("217")))
        N->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("filename_prefix"), TEXT("360"));

    FComfyWorkflowParams WorkflowParams;
    WorkflowParams.WorkflowJson = SerializeWorkflow(WorkflowObj);
    WorkflowParams.OutputPrefix = TEXT("360_Qwen");
    WorkflowParams.RunningStatus = TEXT("Generating 360\u00b0 panorama...");
    WorkflowParams.CompleteStatus = TEXT("360\u00b0 HDRI generated!");
    WorkflowParams.bUpdatePreview = false;
    WorkflowParams.bAutoImport = false;
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

void SComfyUIPanel::OnResolutionChanged(TSharedPtr<FResolutionOption> NewSelection, ESelectInfo::Type)
{
    if (NewSelection.IsValid()) 
    {
        SelectedResolution = NewSelection;
    }
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

FString SComfyUIPanel::GetUpscalerModel(EUpscaleMode Mode) const
{
    switch (Mode)
    {
    case EUpscaleMode::TwoX:  return TEXT("RealESRGAN_x2plus.pth");
    case EUpscaleMode::FourX: return TEXT("4x-ESRGAN.pth");
    default: return TEXT("");
    }
}

void SComfyUIPanel::DisableNode(TSharedPtr<FJsonObject>& WorkflowObj, const FString& NodeId) const
{
    if (TSharedPtr<FJsonObject> Node = WorkflowObj->GetObjectField(NodeId))
        Node->SetNumberField(TEXT("mode"), 4);
}

void SComfyUIPanel::PatchSaveImageInput(TSharedPtr<FJsonObject>& WorkflowObj,
    const FString& SaveNodeId,
    const FString& SourceNodeId,
    int32 SourceSlot) const
{
    if (TSharedPtr<FJsonObject> SaveNode = WorkflowObj->GetObjectField(SaveNodeId))
    {
        TArray<TSharedPtr<FJsonValue>> ImagesRef;
        ImagesRef.Add(MakeShared<FJsonValueString>(SourceNodeId));
        ImagesRef.Add(MakeShared<FJsonValueNumber>(SourceSlot));
        SaveNode->GetObjectField(TEXT("inputs"))->SetArrayField(TEXT("images"), ImagesRef);
    }
}

void SComfyUIPanel::PatchUpscaler(TSharedPtr<FJsonObject>& WorkflowObj,
    EUpscaleMode Mode,
    const FString& UpscaleLoaderNodeId,
    const FString& UpscaleNodeId,
    const FString& SaveNodeId,
    const FString& PreUpscaleOutputNodeId,
    int32 PreUpscaleOutputSlot) const
{
    if (Mode == EUpscaleMode::None)
    {
        DisableNode(WorkflowObj, UpscaleLoaderNodeId);
        DisableNode(WorkflowObj, UpscaleNodeId);
        PatchSaveImageInput(WorkflowObj, SaveNodeId, PreUpscaleOutputNodeId, PreUpscaleOutputSlot);
    }
    else
    {
        if (TSharedPtr<FJsonObject> LoaderNode = WorkflowObj->GetObjectField(UpscaleLoaderNodeId))
            LoaderNode->GetObjectField(TEXT("inputs"))->SetStringField(TEXT("model_name"), GetUpscalerModel(Mode));
    }
}


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

void SComfyUIPanel::OnModelFamilyChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type)
{
    SelectedModelFamilyOption = NewSelection;
    SelectedModelFamily = (*NewSelection == TEXT("Qwen"))
        ? EComfyUIModelFamily::Qwen
        : EComfyUIModelFamily::Flux;

    // Rebuild presets so the dropdown shows model-appropriate options.
    RebuildResolutionOptions();
}

void SComfyUIPanel::OnPromptTextChanged(const FText& NewText)        { PromptText = NewText.ToString(); }
void SComfyUIPanel::OnNegativePromptTextChanged(const FText& NewText) { NegativePromptText = NewText.ToString(); }

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
