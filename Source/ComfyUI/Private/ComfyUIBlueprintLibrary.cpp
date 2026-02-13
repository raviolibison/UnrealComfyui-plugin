#include "ComfyUIBlueprintLibrary.h"
#include "ComfyUIModule.h"
#include "ComfyUISettings.h"
#include "ComfyUIWebSocketHandler.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "TimerManager.h"
#include "Engine/Engine.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/TextureFactory.h"
#include "EditorFramework/AssetImportData.h"
#endif

// ============================================================================
// Helpers
// ============================================================================

namespace
{
    TSharedPtr<FJsonObject> MakeNode(const FString& ClassType)
    {
        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("class_type"), ClassType);
        Node->SetObjectField(TEXT("inputs"), MakeShared<FJsonObject>());
        return Node;
    }

    void SetInputString(TSharedPtr<FJsonObject>& Node, const FString& Key, const FString& Value)
    {
        Node->GetObjectField(TEXT("inputs"))->SetStringField(Key, Value);
    }

    void SetInputNumber(TSharedPtr<FJsonObject>& Node, const FString& Key, double Value)
    {
        Node->GetObjectField(TEXT("inputs"))->SetNumberField(Key, Value);
    }

    void SetInputBool(TSharedPtr<FJsonObject>& Node, const FString& Key, bool Value)
    {
        Node->GetObjectField(TEXT("inputs"))->SetBoolField(Key, Value);
    }

    void SetInputLink(TSharedPtr<FJsonObject>& Node, const FString& Key, int32 SourceNodeId, int32 OutputIndex = 0)
    {
        TArray<TSharedPtr<FJsonValue>> LinkArray;
        LinkArray.Add(MakeShared<FJsonValueString>(FString::FromInt(SourceNodeId)));
        LinkArray.Add(MakeShared<FJsonValueNumber>(OutputIndex));
        Node->GetObjectField(TEXT("inputs"))->SetArrayField(Key, LinkArray);
    }

    void EnsureWebSocketConnected()
    {
        if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
        {
            TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
            if (WSHandler.IsValid() && !WSHandler->IsConnected())
            {
                const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
                FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");
                FString WsUrl = BaseUrl.Replace(TEXT("http://"), TEXT("ws://")).Replace(TEXT("https://"), TEXT("wss://"));
                WsUrl += TEXT("/ws");
                WSHandler->Connect(WsUrl);
            }
        }
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

FString UComfyUIBlueprintLibrary::GetBaseUrl()
{
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    return Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");
}

void UComfyUIBlueprintLibrary::TryEnsurePortable()
{
    if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
    {
        Module->EnsurePortableRunning();
    }
}

FString UComfyUIBlueprintLibrary::BuildPromptWrapperJson(const TSharedPtr<FJsonObject>& PromptObject, const FString& ClientId)
{
    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetObjectField(TEXT("prompt"), PromptObject);
    if (!ClientId.IsEmpty())
    {
        Wrapper->SetStringField(TEXT("client_id"), ClientId);
    }

    FString OutputString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);
    return OutputString;
}

// ============================================================================
// Lifecycle
// ============================================================================

bool UComfyUIBlueprintLibrary::EnsurePortableRunning()
{
    TryEnsurePortable();
    return true;
}

void UComfyUIBlueprintLibrary::CheckComfyUIReady(const FComfyUIResponseDelegate& OnComplete)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(GetBaseUrl() + TEXT("/system_stats"));
    Request->SetVerb(TEXT("GET"));
    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            const bool bOk = bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
            OnComplete.ExecuteIfBound(bOk, bOk ? Response->GetContentAsString() : TEXT("{\"error\":\"not ready\"}"));
        });
    Request->ProcessRequest();
}

void UComfyUIBlueprintLibrary::WaitForComfyUIReady(float TimeoutSeconds, const FComfyUIResponseDelegate& OnComplete)
{
    TryEnsurePortable();
    PollComfyUIReady(TimeoutSeconds, 0.0f, OnComplete);
}

void UComfyUIBlueprintLibrary::PollComfyUIReady(float TimeoutSeconds, float ElapsedTime, const FComfyUIResponseDelegate& OnComplete)
{
    if (ElapsedTime >= TimeoutSeconds)
    {
        OnComplete.ExecuteIfBound(false, TEXT("{\"error\":\"timeout\"}"));
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(GetBaseUrl() + TEXT("/system_stats"));
    Request->SetVerb(TEXT("GET"));
    Request->OnProcessRequestComplete().BindLambda(
        [TimeoutSeconds, ElapsedTime, OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            if (bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
            {
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Ready!"));
                OnComplete.ExecuteIfBound(true, TEXT("{\"status\":\"ready\"}"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Not ready yet, retrying... (%.1fs elapsed)"), ElapsedTime + 1.0f);

                if (GEngine && GEngine->GetWorldContexts().Num() > 0)
                {
                    UWorld* World = GEngine->GetWorldContexts()[0].World();
                    if (World)
                    {
                        FTimerHandle TimerHandle;
                        World->GetTimerManager().SetTimer(
                            TimerHandle,
                            [TimeoutSeconds, ElapsedTime, OnComplete]()
                            {
                                PollComfyUIReady(TimeoutSeconds, ElapsedTime + 1.0f, OnComplete);
                            },
                            1.0f,
                            false
                        );
                    }
                }
            }
        });
    Request->ProcessRequest();
}

// ============================================================================
// Workflow Submission
// ============================================================================

void UComfyUIBlueprintLibrary::SubmitWorkflowJson(const FString& WorkflowJson, const FComfyUISubmitOptions& Options, const FComfyUIResponseDelegate& OnComplete)
{
    TryEnsurePortable();

    TSharedPtr<FJsonObject> PromptObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorkflowJson);
    if (!FJsonSerializer::Deserialize(Reader, PromptObject) || !PromptObject.IsValid())
    {
        OnComplete.ExecuteIfBound(false, TEXT("{\"error\":\"Invalid workflow JSON\"}"));
        return;
    }

    FString RequestBody = BuildPromptWrapperJson(PromptObject, Options.ClientId);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(GetBaseUrl() + TEXT("/prompt"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestBody);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            const bool bOk = bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
            const FString ResponseText = Response.IsValid() ? Response->GetContentAsString() : TEXT("{\"error\":\"no response\"}");
            OnComplete.ExecuteIfBound(bOk, ResponseText);
        });
    Request->ProcessRequest();
}

// ============================================================================
// Workflow Builders
// ============================================================================

FString UComfyUIBlueprintLibrary::BuildSimpleWorkflowJson(const FComfyUISimpleWorkflowParams& Params)
{
    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();

    int32 NodeId = 1;
    const int32 CheckpointId = NodeId++;
    const int32 PositiveId = NodeId++;
    const int32 NegativeId = NodeId++;
    const int32 LatentId = NodeId++;
    const int32 SamplerId = NodeId++;
    const int32 VaeDecodeId = NodeId++;
    const int32 SaveImageId = NodeId++;

    // CheckpointLoaderSimple
    auto CheckpointNode = MakeNode(TEXT("CheckpointLoaderSimple"));
    SetInputString(CheckpointNode, TEXT("ckpt_name"), Params.Checkpoint);
    Graph->SetObjectField(FString::FromInt(CheckpointId), CheckpointNode);

    // Handle LoRAs
    int32 LastModelId = CheckpointId;
    int32 LastClipId = CheckpointId;
    int32 LastModelOutput = 0;
    int32 LastClipOutput = 1;

    for (const auto& Lora : Params.Loras)
    {
        const int32 LoraId = NodeId++;
        auto LoraNode = MakeNode(TEXT("LoraLoader"));
        SetInputString(LoraNode, TEXT("lora_name"), Lora.Name);
        SetInputNumber(LoraNode, TEXT("strength_model"), Lora.Strength);
        SetInputNumber(LoraNode, TEXT("strength_clip"), Lora.Strength);
        SetInputLink(LoraNode, TEXT("model"), LastModelId, LastModelOutput);
        SetInputLink(LoraNode, TEXT("clip"), LastClipId, LastClipOutput);
        Graph->SetObjectField(FString::FromInt(LoraId), LoraNode);
        LastModelId = LoraId;
        LastClipId = LoraId;
        LastModelOutput = 0;
        LastClipOutput = 1;
    }

    // CLIPTextEncode (positive)
    auto PosNode = MakeNode(TEXT("CLIPTextEncode"));
    SetInputString(PosNode, TEXT("text"), Params.PositivePrompt);
    SetInputLink(PosNode, TEXT("clip"), LastClipId, LastClipOutput);
    Graph->SetObjectField(FString::FromInt(PositiveId), PosNode);

    // CLIPTextEncode (negative)
    auto NegNode = MakeNode(TEXT("CLIPTextEncode"));
    SetInputString(NegNode, TEXT("text"), Params.NegativePrompt);
    SetInputLink(NegNode, TEXT("clip"), LastClipId, LastClipOutput);
    Graph->SetObjectField(FString::FromInt(NegativeId), NegNode);

    // EmptyLatentImage
    auto LatentNode = MakeNode(TEXT("EmptyLatentImage"));
    SetInputNumber(LatentNode, TEXT("width"), Params.Width);
    SetInputNumber(LatentNode, TEXT("height"), Params.Height);
    SetInputNumber(LatentNode, TEXT("batch_size"), 1);
    Graph->SetObjectField(FString::FromInt(LatentId), LatentNode);

    // KSampler
    auto SamplerNode = MakeNode(TEXT("KSampler"));
    SetInputLink(SamplerNode, TEXT("model"), LastModelId, LastModelOutput);
    SetInputLink(SamplerNode, TEXT("positive"), PositiveId, 0);
    SetInputLink(SamplerNode, TEXT("negative"), NegativeId, 0);
    SetInputLink(SamplerNode, TEXT("latent_image"), LatentId, 0);
    SetInputNumber(SamplerNode, TEXT("seed"), Params.Seed >= 0 ? Params.Seed : FMath::RandRange(0, MAX_int32));
    SetInputNumber(SamplerNode, TEXT("steps"), Params.Steps);
    SetInputNumber(SamplerNode, TEXT("cfg"), Params.CFGScale);
    SetInputString(SamplerNode, TEXT("sampler_name"), Params.Sampler);
    SetInputString(SamplerNode, TEXT("scheduler"), Params.Scheduler);
    SetInputNumber(SamplerNode, TEXT("denoise"), 1.0);
    Graph->SetObjectField(FString::FromInt(SamplerId), SamplerNode);

    // VAEDecode
    auto VaeDecNode = MakeNode(TEXT("VAEDecode"));
    SetInputLink(VaeDecNode, TEXT("samples"), SamplerId, 0);
    SetInputLink(VaeDecNode, TEXT("vae"), CheckpointId, 2);
    Graph->SetObjectField(FString::FromInt(VaeDecodeId), VaeDecNode);

    // SaveImage
    auto SaveNode = MakeNode(TEXT("SaveImage"));
    SetInputLink(SaveNode, TEXT("images"), VaeDecodeId, 0);
    SetInputString(SaveNode, TEXT("filename_prefix"), TEXT("UE_Generated"));
    Graph->SetObjectField(FString::FromInt(SaveImageId), SaveNode);

    FString OutputString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(Graph.ToSharedRef(), Writer);
    return OutputString;
}

FString UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(const FComfyUIFlux2WorkflowParams& Params)
{
    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();

    int32 NodeId = 10;
    const int32 UnetId = NodeId++;
    const int32 ClipId = NodeId++;
    const int32 VaeId = NodeId++;
    const int32 PositiveId = NodeId++;
    const int32 NegativeId = NodeId++;
    const int32 GuiderId = NodeId++;
    const int32 LatentId = NodeId++;
    const int32 NoiseId = NodeId++;
    const int32 SchedulerId = NodeId++;
    const int32 SamplerId = NodeId++;
    const int32 VaeDecodeId = NodeId++;
    const int32 SaveImageId = NodeId++;

    // UNETLoader
    auto UnetNode = MakeNode(TEXT("UNETLoader"));
    SetInputString(UnetNode, TEXT("unet_name"), Params.UnetName);
    SetInputString(UnetNode, TEXT("weight_dtype"), TEXT("default"));
    Graph->SetObjectField(FString::FromInt(UnetId), UnetNode);

    // CLIPLoader
    auto ClipNode = MakeNode(TEXT("CLIPLoader"));
    SetInputString(ClipNode, TEXT("clip_name"), Params.ClipName);
    SetInputString(ClipNode, TEXT("type"), TEXT("flux"));
    Graph->SetObjectField(FString::FromInt(ClipId), ClipNode);

    // VAELoader
    auto VaeNode = MakeNode(TEXT("VAELoader"));
    SetInputString(VaeNode, TEXT("vae_name"), Params.VaeName);
    Graph->SetObjectField(FString::FromInt(VaeId), VaeNode);

    // CLIPTextEncode (positive)
    auto PosNode = MakeNode(TEXT("CLIPTextEncode"));
    SetInputString(PosNode, TEXT("text"), Params.PositivePrompt);
    SetInputLink(PosNode, TEXT("clip"), ClipId, 0);
    Graph->SetObjectField(FString::FromInt(PositiveId), PosNode);

    // CLIPTextEncode (negative)
    auto NegNode = MakeNode(TEXT("CLIPTextEncode"));
    SetInputString(NegNode, TEXT("text"), Params.NegativePrompt.IsEmpty() ? TEXT("") : Params.NegativePrompt);
    SetInputLink(NegNode, TEXT("clip"), ClipId, 0);
    Graph->SetObjectField(FString::FromInt(NegativeId), NegNode);

    // CFGGuider
    auto GuiderNode = MakeNode(TEXT("CFGGuider"));
    SetInputLink(GuiderNode, TEXT("model"), UnetId, 0);
    SetInputLink(GuiderNode, TEXT("positive"), PositiveId, 0);
    SetInputLink(GuiderNode, TEXT("negative"), NegativeId, 0);
    SetInputNumber(GuiderNode, TEXT("cfg"), Params.CFGScale);
    Graph->SetObjectField(FString::FromInt(GuiderId), GuiderNode);

    // EmptyFlux2LatentImage
    auto LatentNode = MakeNode(TEXT("EmptyFlux2LatentImage"));
    SetInputNumber(LatentNode, TEXT("width"), Params.Width);
    SetInputNumber(LatentNode, TEXT("height"), Params.Height);
    SetInputNumber(LatentNode, TEXT("batch_size"), 1);
    Graph->SetObjectField(FString::FromInt(LatentId), LatentNode);

    // RandomNoise
    auto NoiseNode = MakeNode(TEXT("RandomNoise"));
    SetInputNumber(NoiseNode, TEXT("noise_seed"), Params.Seed >= 0 ? Params.Seed : FMath::RandRange(0, MAX_int32));
    Graph->SetObjectField(FString::FromInt(NoiseId), NoiseNode);

    // Flux2Scheduler
    auto SchedulerNode = MakeNode(TEXT("Flux2Scheduler"));
    SetInputLink(SchedulerNode, TEXT("model"), UnetId, 0);
    SetInputNumber(SchedulerNode, TEXT("steps"), Params.Steps);
    Graph->SetObjectField(FString::FromInt(SchedulerId), SchedulerNode);

    // SamplerCustomAdvanced
    auto SamplerNode = MakeNode(TEXT("SamplerCustomAdvanced"));
    SetInputLink(SamplerNode, TEXT("guider"), GuiderId, 0);
    SetInputLink(SamplerNode, TEXT("noise"), NoiseId, 0);
    SetInputLink(SamplerNode, TEXT("sampler"), SamplerId, 0);
    SetInputLink(SamplerNode, TEXT("sigmas"), SchedulerId, 0);
    SetInputLink(SamplerNode, TEXT("latent_image"), LatentId, 0);
    Graph->SetObjectField(FString::FromInt(SamplerId), SamplerNode);

    // KSamplerSelect (to provide sampler to SamplerCustomAdvanced)
    // Note: We need a separate KSamplerSelect node
    const int32 SamplerSelectId = NodeId++;
    auto SamplerSelectNode = MakeNode(TEXT("KSamplerSelect"));
    SetInputString(SamplerSelectNode, TEXT("sampler_name"), Params.Sampler);
    Graph->SetObjectField(FString::FromInt(SamplerSelectId), SamplerSelectNode);

    // Fix SamplerCustomAdvanced to reference KSamplerSelect
    SamplerNode->GetObjectField(TEXT("inputs"))->RemoveField(TEXT("sampler"));
    SetInputLink(SamplerNode, TEXT("sampler"), SamplerSelectId, 0);

    // VAEDecode
    auto VaeDecNode = MakeNode(TEXT("VAEDecode"));
    SetInputLink(VaeDecNode, TEXT("samples"), SamplerId, 0);
    SetInputLink(VaeDecNode, TEXT("vae"), VaeId, 0);
    Graph->SetObjectField(FString::FromInt(VaeDecodeId), VaeDecNode);

    // SaveImage
    auto SaveNode = MakeNode(TEXT("SaveImage"));
    SetInputLink(SaveNode, TEXT("images"), VaeDecodeId, 0);
    SetInputString(SaveNode, TEXT("filename_prefix"), Params.FilenamePrefix);
    Graph->SetObjectField(FString::FromInt(SaveImageId), SaveNode);

    FString OutputString;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(Graph.ToSharedRef(), Writer);
    return OutputString;
}

// ============================================================================
// Image Loading
// ============================================================================

FString UComfyUIBlueprintLibrary::GetComfyUIOutputFolder()
{
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString Root = Settings ? Settings->PortableRoot : TEXT("");

    if (Root.IsEmpty())
    {
        return TEXT("");
    }

    return FPaths::Combine(Root, TEXT("ComfyUI"), TEXT("output"));
}

UTexture2D* UComfyUIBlueprintLibrary::LoadImageFromFile(const FString& FilePath)
{
    TArray<uint8> RawFileData;
    if (!FFileHelper::LoadFileToArray(RawFileData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to load file: %s"), *FilePath);
        return nullptr;
    }

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

    EImageFormat Format = EImageFormat::PNG;
    if (FilePath.EndsWith(TEXT(".jpg")) || FilePath.EndsWith(TEXT(".jpeg")))
    {
        Format = EImageFormat::JPEG;
    }

    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(Format);
    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to decompress image: %s"), *FilePath);
        return nullptr;
    }

    TArray<uint8> UncompressedBGRA;
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
    {
        return nullptr;
    }

    const int32 Width = ImageWrapper->GetWidth();
    const int32 Height = ImageWrapper->GetHeight();

    UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
    if (!Texture)
    {
        return nullptr;
    }

    void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, UncompressedBGRA.GetData(), UncompressedBGRA.Num());
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
    Texture->UpdateResource();

    return Texture;
}

FString UComfyUIBlueprintLibrary::GetLatestOutputImage(const FString& FilenamePrefix)
{
    FString OutputFolder = GetComfyUIOutputFolder();
    if (OutputFolder.IsEmpty())
    {
        return TEXT("");
    }

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(OutputFolder, TEXT("*.png")), true, false);

    FString LatestFile;
    FDateTime LatestTime = FDateTime::MinValue();

    for (const FString& File : Files)
    {
        if (!FilenamePrefix.IsEmpty() && !File.StartsWith(FilenamePrefix))
        {
            continue;
        }

        FString FullPath = FPaths::Combine(OutputFolder, File);
        FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
        if (ModTime > LatestTime)
        {
            LatestTime = ModTime;
            LatestFile = FullPath;
        }
    }

    return LatestFile;
}

UTexture2D* UComfyUIBlueprintLibrary::ImportImageAsAsset(const FString& SourceFilePath, const FString& DestAssetPath)
{
#if WITH_EDITOR
    UTextureFactory* Factory = NewObject<UTextureFactory>();
    Factory->AddToRoot();

    TArray<uint8> RawData;
    if (!FFileHelper::LoadFileToArray(RawData, *SourceFilePath))
    {
        Factory->RemoveFromRoot();
        return nullptr;
    }

    const uint8* DataPtr = RawData.GetData();
    UTexture2D* Texture = Cast<UTexture2D>(
        Factory->FactoryCreateBinary(
            UTexture2D::StaticClass(),
            CreatePackage(*DestAssetPath),
            FName(*FPaths::GetBaseFilename(DestAssetPath)),
            RF_Public | RF_Standalone,
            nullptr,
            *FPaths::GetExtension(SourceFilePath),
            DataPtr,
            DataPtr + RawData.Num(),
            GWarn
        )
    );

    Factory->RemoveFromRoot();

    if (Texture)
    {
        FAssetRegistryModule::AssetCreated(Texture);
        Texture->MarkPackageDirty();
    }

    return Texture;
#else
    return LoadImageFromFile(SourceFilePath);
#endif
}

FString UComfyUIBlueprintLibrary::GenerateUniqueAssetName(const FString& BasePath, const FString& BaseName)
{
    FString AssetPath = FPaths::Combine(BasePath, BaseName);
    int32 Counter = 0;

    while (FPackageName::DoesPackageExist(AssetPath))
    {
        Counter++;
        AssetPath = FPaths::Combine(BasePath, FString::Printf(TEXT("%s_%03d"), *BaseName, Counter));
    }

    return AssetPath;
}

// ============================================================================
// WebSocket / Monitoring
// ============================================================================

void UComfyUIBlueprintLibrary::WatchWorkflowCompletion(const FString& PromptId, const FComfyUIWorkflowCompleteDelegate& OnComplete)
{
    EnsureWebSocketConnected();

    if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
    {
        TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
        if (WSHandler.IsValid())
        {
            // Bridge from native delegate to dynamic delegate
            FComfyUIWorkflowCompleteDelegateNative NativeDelegate;
            NativeDelegate.BindLambda([OnComplete](bool bSuccess, const FString& InPromptId)
            {
                OnComplete.ExecuteIfBound(bSuccess, InPromptId);
            });
            WSHandler->WatchPrompt(PromptId, NativeDelegate);
        }
    }
}
