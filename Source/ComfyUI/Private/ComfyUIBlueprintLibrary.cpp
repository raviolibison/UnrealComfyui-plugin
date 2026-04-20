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
#include "Interfaces/IPluginManager.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"

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

static FString BuildPromptWrapperJson(const TSharedPtr<FJsonObject>& PromptObject, const FString& ClientId)
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
    SetInputString(ClipNode, TEXT("type"), TEXT("flux2"));
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

    /// RandomNoise
    auto NoiseNode = MakeNode(TEXT("RandomNoise"));
    int32 ActualSeed = Params.Seed >= 0 ? Params.Seed : FMath::RandRange(0, MAX_int32);
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Building workflow with seed: %d (Params.Seed was: %d)"), ActualSeed, Params.Seed);
    SetInputNumber(NoiseNode, TEXT("noise_seed"), ActualSeed);
    Graph->SetObjectField(FString::FromInt(NoiseId), NoiseNode);

    // Flux2Scheduler
    auto SchedulerNode = MakeNode(TEXT("Flux2Scheduler"));
    SetInputNumber(SchedulerNode, TEXT("steps"), Params.Steps);
    SetInputNumber(SchedulerNode, TEXT("width"), Params.Width);  
    SetInputNumber(SchedulerNode, TEXT("height"), Params.Height);
    SetInputString(SchedulerNode, TEXT("scheduler"), Params.Scheduler);
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

FString UComfyUIBlueprintLibrary::BuildQwenGenerateWorkflowJson(const FComfyUIQwenGenerateParams& Params)
{
    // Load the template as a string and parse it
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    // --- Node 245: UNETLoader ---
    TSharedPtr<FJsonObject> Node245 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node245Inputs = MakeShared<FJsonObject>();
    Node245Inputs->SetStringField(TEXT("unet_name"), Params.UnetName);
    Node245Inputs->SetStringField(TEXT("weight_dtype"), TEXT("default"));
    Node245->SetObjectField(TEXT("inputs"), Node245Inputs);
    Node245->SetStringField(TEXT("class_type"), TEXT("UNETLoader"));
    Root->SetObjectField(TEXT("245"), Node245);

    // --- Node 246: CLIPLoader ---
    TSharedPtr<FJsonObject> Node246 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node246Inputs = MakeShared<FJsonObject>();
    Node246Inputs->SetStringField(TEXT("clip_name"), Params.ClipName);
    Node246Inputs->SetStringField(TEXT("type"), TEXT("qwen_image"));
    Node246Inputs->SetStringField(TEXT("device"), TEXT("default"));
    Node246->SetObjectField(TEXT("inputs"), Node246Inputs);
    Node246->SetStringField(TEXT("class_type"), TEXT("CLIPLoader"));
    Root->SetObjectField(TEXT("246"), Node246);

    // --- Node 247: VAELoader ---
    TSharedPtr<FJsonObject> Node247 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node247Inputs = MakeShared<FJsonObject>();
    Node247Inputs->SetStringField(TEXT("vae_name"), Params.VaeName);
    Node247->SetObjectField(TEXT("inputs"), Node247Inputs);
    Node247->SetStringField(TEXT("class_type"), TEXT("VAELoader"));
    Root->SetObjectField(TEXT("247"), Node247);

    // --- Node 248: EmptySD3LatentImage ---
    TSharedPtr<FJsonObject> Node248 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node248Inputs = MakeShared<FJsonObject>();
    Node248Inputs->SetNumberField(TEXT("width"), Params.Width);
    Node248Inputs->SetNumberField(TEXT("height"), Params.Height);
    Node248Inputs->SetNumberField(TEXT("batch_size"), 1);
    Node248->SetObjectField(TEXT("inputs"), Node248Inputs);
    Node248->SetStringField(TEXT("class_type"), TEXT("EmptySD3LatentImage"));
    Root->SetObjectField(TEXT("248"), Node248);

    // --- Node 249: CLIPTextEncode (Positive) ---
    TSharedPtr<FJsonObject> Node249 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node249Inputs = MakeShared<FJsonObject>();
    Node249Inputs->SetStringField(TEXT("text"), Params.PositivePrompt);
    TArray<TSharedPtr<FJsonValue>> Clip249Val;
    Clip249Val.Add(MakeShared<FJsonValueString>(TEXT("246")));
    Clip249Val.Add(MakeShared<FJsonValueNumber>(0));
    Node249Inputs->SetArrayField(TEXT("clip"), Clip249Val);
    Node249->SetObjectField(TEXT("inputs"), Node249Inputs);
    Node249->SetStringField(TEXT("class_type"), TEXT("CLIPTextEncode"));
    Root->SetObjectField(TEXT("249"), Node249);

    // --- Node 250: CLIPTextEncode (Negative) --- empty for Qwen
    TSharedPtr<FJsonObject> Node250 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node250Inputs = MakeShared<FJsonObject>();
    Node250Inputs->SetStringField(TEXT("text"), TEXT(""));
    TArray<TSharedPtr<FJsonValue>> Clip250Val;
    Clip250Val.Add(MakeShared<FJsonValueString>(TEXT("246")));
    Clip250Val.Add(MakeShared<FJsonValueNumber>(0));
    Node250Inputs->SetArrayField(TEXT("clip"), Clip250Val);
    Node250->SetObjectField(TEXT("inputs"), Node250Inputs);
    Node250->SetStringField(TEXT("class_type"), TEXT("CLIPTextEncode"));
    Root->SetObjectField(TEXT("250"), Node250);

    // --- Node 251: PrimitiveInt (Steps) ---
    TSharedPtr<FJsonObject> Node251 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node251Inputs = MakeShared<FJsonObject>();
    Node251Inputs->SetNumberField(TEXT("value"), Params.Steps);
    Node251->SetObjectField(TEXT("inputs"), Node251Inputs);
    Node251->SetStringField(TEXT("class_type"), TEXT("PrimitiveInt"));
    Root->SetObjectField(TEXT("251"), Node251);

    // --- Node 252: PrimitiveFloat (CFG) ---
    TSharedPtr<FJsonObject> Node252 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node252Inputs = MakeShared<FJsonObject>();
    Node252Inputs->SetNumberField(TEXT("value"), Params.CFGScale);
    Node252->SetObjectField(TEXT("inputs"), Node252Inputs);
    Node252->SetStringField(TEXT("class_type"), TEXT("PrimitiveFloat"));
    Root->SetObjectField(TEXT("252"), Node252);

    // --- Node 260: ModelSamplingAuraFlow (Shift) ---
    TSharedPtr<FJsonObject> Node260 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node260Inputs = MakeShared<FJsonObject>();
    Node260Inputs->SetNumberField(TEXT("shift"), Params.Shift);
    TArray<TSharedPtr<FJsonValue>> Model260Val;
    Model260Val.Add(MakeShared<FJsonValueString>(TEXT("245")));
    Model260Val.Add(MakeShared<FJsonValueNumber>(0));
    Node260Inputs->SetArrayField(TEXT("model"), Model260Val);
    Node260->SetObjectField(TEXT("inputs"), Node260Inputs);
    Node260->SetStringField(TEXT("class_type"), TEXT("ModelSamplingAuraFlow"));
    Root->SetObjectField(TEXT("260"), Node260);

    // --- Node 261: KSampler ---
    TSharedPtr<FJsonObject> Node261 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node261Inputs = MakeShared<FJsonObject>();
    int32 ActualSeed = Params.Seed < 0 ? FMath::Rand() : Params.Seed;
    Node261Inputs->SetNumberField(TEXT("seed"), ActualSeed);
    TArray<TSharedPtr<FJsonValue>> StepsRef, CfgRef, ModelRef, PosRef, NegRef, LatentRef;
    StepsRef.Add(MakeShared<FJsonValueString>(TEXT("251"))); StepsRef.Add(MakeShared<FJsonValueNumber>(0));
    CfgRef.Add(MakeShared<FJsonValueString>(TEXT("252")));   CfgRef.Add(MakeShared<FJsonValueNumber>(0));
    ModelRef.Add(MakeShared<FJsonValueString>(TEXT("260"))); ModelRef.Add(MakeShared<FJsonValueNumber>(0));
    PosRef.Add(MakeShared<FJsonValueString>(TEXT("249")));   PosRef.Add(MakeShared<FJsonValueNumber>(0));
    NegRef.Add(MakeShared<FJsonValueString>(TEXT("250")));   NegRef.Add(MakeShared<FJsonValueNumber>(0));
    LatentRef.Add(MakeShared<FJsonValueString>(TEXT("248"))); LatentRef.Add(MakeShared<FJsonValueNumber>(0));
    Node261Inputs->SetArrayField(TEXT("steps"), StepsRef);
    Node261Inputs->SetArrayField(TEXT("cfg"), CfgRef);
    Node261Inputs->SetStringField(TEXT("sampler_name"), Params.Sampler);
    Node261Inputs->SetStringField(TEXT("scheduler"), Params.Scheduler);
    Node261Inputs->SetNumberField(TEXT("denoise"), 1.0);
    Node261Inputs->SetArrayField(TEXT("model"), ModelRef);
    Node261Inputs->SetArrayField(TEXT("positive"), PosRef);
    Node261Inputs->SetArrayField(TEXT("negative"), NegRef);
    Node261Inputs->SetArrayField(TEXT("latent_image"), LatentRef);
    Node261->SetObjectField(TEXT("inputs"), Node261Inputs);
    Node261->SetStringField(TEXT("class_type"), TEXT("KSampler"));
    Root->SetObjectField(TEXT("261"), Node261);

    // --- Node 262: VAEDecode ---
    TSharedPtr<FJsonObject> Node262 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node262Inputs = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Samples262, Vae262;
    Samples262.Add(MakeShared<FJsonValueString>(TEXT("261"))); Samples262.Add(MakeShared<FJsonValueNumber>(0));
    Vae262.Add(MakeShared<FJsonValueString>(TEXT("247")));     Vae262.Add(MakeShared<FJsonValueNumber>(0));
    Node262Inputs->SetArrayField(TEXT("samples"), Samples262);
    Node262Inputs->SetArrayField(TEXT("vae"), Vae262);
    Node262->SetObjectField(TEXT("inputs"), Node262Inputs);
    Node262->SetStringField(TEXT("class_type"), TEXT("VAEDecode"));
    Root->SetObjectField(TEXT("262"), Node262);

    // --- Node 60: SaveImage ---
    TSharedPtr<FJsonObject> Node60 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node60Inputs = MakeShared<FJsonObject>();
    Node60Inputs->SetStringField(TEXT("filename_prefix"), Params.FilenamePrefix);
    TArray<TSharedPtr<FJsonValue>> Images60;
    Images60.Add(MakeShared<FJsonValueString>(TEXT("262"))); Images60.Add(MakeShared<FJsonValueNumber>(0));
    Node60Inputs->SetArrayField(TEXT("images"), Images60);
    Node60->SetObjectField(TEXT("inputs"), Node60Inputs);
    Node60->SetStringField(TEXT("class_type"), TEXT("SaveImage"));
    Root->SetObjectField(TEXT("60"), Node60);

    // Serialize
    FString OutputJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return OutputJson;
}


FString UComfyUIBlueprintLibrary::BuildQwenEditWorkflowJson(const FComfyUIQwenEditParams& Params)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    // --- Node 41: LoadImage (source) ---
    TSharedPtr<FJsonObject> Node41 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node41Inputs = MakeShared<FJsonObject>();
    Node41Inputs->SetStringField(TEXT("image"), Params.InputImageFilename);
    Node41->SetObjectField(TEXT("inputs"), Node41Inputs);
    Node41->SetStringField(TEXT("class_type"), TEXT("LoadImage"));
    Root->SetObjectField(TEXT("41"), Node41);

    // --- Node 171: FluxKontextImageScale ---
    TSharedPtr<FJsonObject> Node171 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node171Inputs = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Image171;
    Image171.Add(MakeShared<FJsonValueString>(TEXT("41"))); Image171.Add(MakeShared<FJsonValueNumber>(0));
    Node171Inputs->SetArrayField(TEXT("image"), Image171);
    Node171->SetObjectField(TEXT("inputs"), Node171Inputs);
    Node171->SetStringField(TEXT("class_type"), TEXT("FluxKontextImageScale"));
    Root->SetObjectField(TEXT("171"), Node171);

    // --- Node 172: VAELoader ---
    TSharedPtr<FJsonObject> Node172 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node172Inputs = MakeShared<FJsonObject>();
    Node172Inputs->SetStringField(TEXT("vae_name"), Params.VaeName);
    Node172->SetObjectField(TEXT("inputs"), Node172Inputs);
    Node172->SetStringField(TEXT("class_type"), TEXT("VAELoader"));
    Root->SetObjectField(TEXT("172"), Node172);

    // --- Node 173: CLIPLoader ---
    TSharedPtr<FJsonObject> Node173 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node173Inputs = MakeShared<FJsonObject>();
    Node173Inputs->SetStringField(TEXT("clip_name"), Params.ClipName);
    Node173Inputs->SetStringField(TEXT("type"), TEXT("qwen_image"));
    Node173Inputs->SetStringField(TEXT("device"), TEXT("default"));
    Node173->SetObjectField(TEXT("inputs"), Node173Inputs);
    Node173->SetStringField(TEXT("class_type"), TEXT("CLIPLoader"));
    Root->SetObjectField(TEXT("173"), Node173);

    // --- Node 174: UNETLoader ---
    TSharedPtr<FJsonObject> Node174 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node174Inputs = MakeShared<FJsonObject>();
    Node174Inputs->SetStringField(TEXT("unet_name"), Params.UnetName);
    Node174Inputs->SetStringField(TEXT("weight_dtype"), TEXT("default"));
    Node174->SetObjectField(TEXT("inputs"), Node174Inputs);
    Node174->SetStringField(TEXT("class_type"), TEXT("UNETLoader"));
    Root->SetObjectField(TEXT("174"), Node174);

    // Helper lambdas for node refs
    auto MakeRef = [](const FString& NodeId, int32 Output) {
        TArray<TSharedPtr<FJsonValue>> Ref;
        Ref.Add(MakeShared<FJsonValueString>(NodeId));
        Ref.Add(MakeShared<FJsonValueNumber>(Output));
        return Ref;
        };

    // --- Node 175: TextEncodeQwenImageEditPlus (Positive) ---
    TSharedPtr<FJsonObject> Node175 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node175Inputs = MakeShared<FJsonObject>();
    Node175Inputs->SetStringField(TEXT("prompt"), Params.Instruction);
    Node175Inputs->SetArrayField(TEXT("clip"), MakeRef(TEXT("173"), 0));
    Node175Inputs->SetArrayField(TEXT("vae"), MakeRef(TEXT("172"), 0));
    Node175Inputs->SetArrayField(TEXT("image1"), MakeRef(TEXT("171"), 0));
    Node175->SetObjectField(TEXT("inputs"), Node175Inputs);
    Node175->SetStringField(TEXT("class_type"), TEXT("TextEncodeQwenImageEditPlus"));
    Root->SetObjectField(TEXT("175"), Node175);

    // --- Node 176: TextEncodeQwenImageEditPlus (Negative) --- empty prompt
    TSharedPtr<FJsonObject> Node176 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node176Inputs = MakeShared<FJsonObject>();
    Node176Inputs->SetStringField(TEXT("prompt"), TEXT(""));
    Node176Inputs->SetArrayField(TEXT("clip"), MakeRef(TEXT("173"), 0));
    Node176Inputs->SetArrayField(TEXT("vae"), MakeRef(TEXT("172"), 0));
    Node176Inputs->SetArrayField(TEXT("image1"), MakeRef(TEXT("171"), 0));
    Node176->SetObjectField(TEXT("inputs"), Node176Inputs);
    Node176->SetStringField(TEXT("class_type"), TEXT("TextEncodeQwenImageEditPlus"));
    Root->SetObjectField(TEXT("176"), Node176);

    // --- Node 177: ModelSamplingAuraFlow (Shift) ---
    TSharedPtr<FJsonObject> Node177 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node177Inputs = MakeShared<FJsonObject>();
    Node177Inputs->SetNumberField(TEXT("shift"), Params.Shift);
    Node177Inputs->SetArrayField(TEXT("model"), MakeRef(TEXT("174"), 0));
    Node177->SetObjectField(TEXT("inputs"), Node177Inputs);
    Node177->SetStringField(TEXT("class_type"), TEXT("ModelSamplingAuraFlow"));
    Root->SetObjectField(TEXT("177"), Node177);

    // --- Node 178: CFGNorm ---
    TSharedPtr<FJsonObject> Node178 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node178Inputs = MakeShared<FJsonObject>();
    Node178Inputs->SetNumberField(TEXT("strength"), 1.0);
    Node178Inputs->SetArrayField(TEXT("model"), MakeRef(TEXT("177"), 0));
    Node178->SetObjectField(TEXT("inputs"), Node178Inputs);
    Node178->SetStringField(TEXT("class_type"), TEXT("CFGNorm"));
    Root->SetObjectField(TEXT("178"), Node178);

    // --- Node 179: FluxKontextMultiReferenceLatentMethod (Positive) ---
    TSharedPtr<FJsonObject> Node179 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node179Inputs = MakeShared<FJsonObject>();
    Node179Inputs->SetStringField(TEXT("reference_latents_method"), TEXT("index_timestep_zero"));
    Node179Inputs->SetArrayField(TEXT("conditioning"), MakeRef(TEXT("175"), 0));
    Node179->SetObjectField(TEXT("inputs"), Node179Inputs);
    Node179->SetStringField(TEXT("class_type"), TEXT("FluxKontextMultiReferenceLatentMethod"));
    Root->SetObjectField(TEXT("179"), Node179);

    // --- Node 180: FluxKontextMultiReferenceLatentMethod (Negative) ---
    TSharedPtr<FJsonObject> Node180 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node180Inputs = MakeShared<FJsonObject>();
    Node180Inputs->SetStringField(TEXT("reference_latents_method"), TEXT("index_timestep_zero"));
    Node180Inputs->SetArrayField(TEXT("conditioning"), MakeRef(TEXT("176"), 0));
    Node180->SetObjectField(TEXT("inputs"), Node180Inputs);
    Node180->SetStringField(TEXT("class_type"), TEXT("FluxKontextMultiReferenceLatentMethod"));
    Root->SetObjectField(TEXT("180"), Node180);

    // --- Node 181: PrimitiveInt (Steps) ---
    TSharedPtr<FJsonObject> Node181 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node181Inputs = MakeShared<FJsonObject>();
    Node181Inputs->SetNumberField(TEXT("value"), Params.Steps);
    Node181->SetObjectField(TEXT("inputs"), Node181Inputs);
    Node181->SetStringField(TEXT("class_type"), TEXT("PrimitiveInt"));
    Root->SetObjectField(TEXT("181"), Node181);

    // --- Node 182: PrimitiveFloat (CFG) ---
    TSharedPtr<FJsonObject> Node182 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node182Inputs = MakeShared<FJsonObject>();
    Node182Inputs->SetNumberField(TEXT("value"), Params.CFGScale);
    Node182->SetObjectField(TEXT("inputs"), Node182Inputs);
    Node182->SetStringField(TEXT("class_type"), TEXT("PrimitiveFloat"));
    Root->SetObjectField(TEXT("182"), Node182);

    // --- Node 183: KSampler ---
    TSharedPtr<FJsonObject> Node183 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node183Inputs = MakeShared<FJsonObject>();
    int32 ActualSeed = Params.Seed < 0 ? FMath::Rand() : Params.Seed;
    Node183Inputs->SetNumberField(TEXT("seed"), ActualSeed);
    Node183Inputs->SetArrayField(TEXT("steps"), MakeRef(TEXT("181"), 0));
    Node183Inputs->SetArrayField(TEXT("cfg"), MakeRef(TEXT("182"), 0));
    Node183Inputs->SetStringField(TEXT("sampler_name"), Params.Sampler);
    Node183Inputs->SetStringField(TEXT("scheduler"), Params.Scheduler);
    Node183Inputs->SetNumberField(TEXT("denoise"), 1.0);
    Node183Inputs->SetArrayField(TEXT("model"), MakeRef(TEXT("178"), 0));
    Node183Inputs->SetArrayField(TEXT("positive"), MakeRef(TEXT("179"), 0));
    Node183Inputs->SetArrayField(TEXT("negative"), MakeRef(TEXT("180"), 0));
    Node183Inputs->SetArrayField(TEXT("latent_image"), MakeRef(TEXT("186"), 0));
    Node183->SetObjectField(TEXT("inputs"), Node183Inputs);
    Node183->SetStringField(TEXT("class_type"), TEXT("KSampler"));
    Root->SetObjectField(TEXT("183"), Node183);

    // --- Node 184: VAEDecode ---
    TSharedPtr<FJsonObject> Node184 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node184Inputs = MakeShared<FJsonObject>();
    Node184Inputs->SetArrayField(TEXT("samples"), MakeRef(TEXT("183"), 0));
    Node184Inputs->SetArrayField(TEXT("vae"), MakeRef(TEXT("172"), 0));
    Node184->SetObjectField(TEXT("inputs"), Node184Inputs);
    Node184->SetStringField(TEXT("class_type"), TEXT("VAEDecode"));
    Root->SetObjectField(TEXT("184"), Node184);

    // --- Node 186: VAEEncode ---
    TSharedPtr<FJsonObject> Node186 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node186Inputs = MakeShared<FJsonObject>();
    Node186Inputs->SetArrayField(TEXT("pixels"), MakeRef(TEXT("171"), 0));
    Node186Inputs->SetArrayField(TEXT("vae"), MakeRef(TEXT("172"), 0));
    Node186->SetObjectField(TEXT("inputs"), Node186Inputs);
    Node186->SetStringField(TEXT("class_type"), TEXT("VAEEncode"));
    Root->SetObjectField(TEXT("186"), Node186);

    // --- Node 9: SaveImage ---
    TSharedPtr<FJsonObject> Node9 = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Node9Inputs = MakeShared<FJsonObject>();
    Node9Inputs->SetStringField(TEXT("filename_prefix"), Params.FilenamePrefix);
    Node9Inputs->SetArrayField(TEXT("images"), MakeRef(TEXT("184"), 0));
    Node9->SetObjectField(TEXT("inputs"), Node9Inputs);
    Node9->SetStringField(TEXT("class_type"), TEXT("SaveImage"));
    Root->SetObjectField(TEXT("9"), Node9);

    // Serialize
    FString OutputJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return OutputJson;
}

// ============================================================================
// Image Loading
// ============================================================================

FString UComfyUIBlueprintLibrary::GetComfyUIOutputFolder()
{
    const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
    FString Root = Settings ? Settings->GetEffectivePortableRoot() : TEXT("");

    if (Root.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: PortableRoot is empty in settings!"));
        
        // Try to auto-detect
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
        {
            FString PluginDir = Plugin->GetBaseDir();
            TArray<FString> CandidateFolders;
            CandidateFolders.Add(TEXT("ComfyUI_windows_portable"));
            CandidateFolders.Add(TEXT("ComfyUIPortable"));
            CandidateFolders.Add(TEXT("ComfyUI"));
            
            for (const FString& Folder : CandidateFolders)
            {
                FString TestPath = FPaths::Combine(PluginDir, Folder, TEXT("ComfyUI"), TEXT("output"));
                if (FPaths::DirectoryExists(TestPath))
                {
                    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Auto-detected output folder: %s"), *TestPath);
                    return TestPath;
                }
            }
        }
        
        return TEXT("");
    }

    FString OutputPath = FPaths::Combine(Root, TEXT("ComfyUI"), TEXT("output"));
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Output folder: %s"), *OutputPath);
    return OutputPath;
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
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Output folder is empty!"));
        return TEXT("");
    }

    if (!FPaths::DirectoryExists(OutputFolder))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Output folder does not exist: %s"), *OutputFolder);
        return TEXT("");
    }

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *FPaths::Combine(OutputFolder, TEXT("*.png")), true, false);

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Found %d PNG files in output folder"), Files.Num());
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Looking for prefix: '%s'"), *FilenamePrefix);

    FString LatestFile;
    FDateTime LatestTime = FDateTime::MinValue();

    for (const FString& File : Files)
    {
        UE_LOG(LogTemp, Log, TEXT("ComfyUI: Checking file: %s"), *File);
        
        if (!FilenamePrefix.IsEmpty() && !File.StartsWith(FilenamePrefix))
        {
            UE_LOG(LogTemp, Log, TEXT("ComfyUI: Skipping (prefix mismatch): %s"), *File);
            continue;
        }

        FString FullPath = FPaths::Combine(OutputFolder, File);
        FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
        
        UE_LOG(LogTemp, Log, TEXT("ComfyUI: File time: %s - %s"), *File, *ModTime.ToString());
        
        if (ModTime > LatestTime)
        {
            LatestTime = ModTime;
            LatestFile = FullPath;
        }
    }

    if (LatestFile.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: No matching files found!"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Latest file: %s"), *LatestFile);
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
