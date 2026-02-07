#include "ComfyUIBlueprintLibrary.h"
#include "ComfyUIModule.h"
#include "ComfyUISettings.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"
#include "Modules/ModuleManager.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"
#include "Interfaces/IPluginManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/TextureFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
    void EnsureWebSocketConnected()
    {
        if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
        {
            TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
            if (WSHandler.IsValid() && !WSHandler->IsConnected())
            {
                const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
                FString BaseUrl = Settings ? Settings->BaseUrl : TEXT("http://127.0.0.1:8188");
                
                // Convert HTTP URL to WebSocket URL
                FString WsUrl = BaseUrl.Replace(TEXT("http://"), TEXT("ws://")).Replace(TEXT("https://"), TEXT("wss://"));
                WsUrl += TEXT("/ws");
                
                // Generate client ID
                FString ClientId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
                
                WSHandler->Connect(WsUrl, ClientId);
            }
        }
    }
    
    bool TryEnsurePortable()
    {
        if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
        {
            return Module->EnsurePortableRunning();
        }

        return false;
    }

    FString GetBaseUrl()
    {
        const UComfyUISettings* Settings = GetDefault<UComfyUISettings>();
        if (Settings && !Settings->BaseUrl.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Using BaseUrl from settings: %s"), *Settings->BaseUrl);
            return Settings->BaseUrl;
        }
    
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Settings not found or BaseUrl empty, using default: http://127.0.0.1:8188"));
        return TEXT("http://127.0.0.1:8188");
    }

    TSharedPtr<FJsonObject> MakeNode(const FString& ClassType)
    {
        TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
        Node->SetStringField(TEXT("class_type"), ClassType);
        Node->SetObjectField(TEXT("inputs"), MakeShared<FJsonObject>());
        return Node;
    }

    void SetInput(const TSharedPtr<FJsonObject>& Node, const FString& Key, const TSharedPtr<FJsonValue>& Value)
    {
        Node->GetObjectField(TEXT("inputs"))->SetField(Key, Value);
    }

    void SetInputString(const TSharedPtr<FJsonObject>& Node, const FString& Key, const FString& Value)
    {
        SetInput(Node, Key, MakeShared<FJsonValueString>(Value));
    }

    void SetInputNumber(const TSharedPtr<FJsonObject>& Node, const FString& Key, double Value)
    {
        SetInput(Node, Key, MakeShared<FJsonValueNumber>(Value));
    }

    void SetInputLink(const TSharedPtr<FJsonObject>& Node, const FString& Key, int32 NodeId, int32 SlotIndex)
    {
        TArray<TSharedPtr<FJsonValue>> LinkValues;
        LinkValues.Add(MakeShared<FJsonValueString>(FString::FromInt(NodeId)));  // Node ID as STRING
        LinkValues.Add(MakeShared<FJsonValueNumber>(SlotIndex));                  // Slot as NUMBER
        SetInput(Node, Key, MakeShared<FJsonValueArray>(LinkValues));
    }
}

bool UComfyUIBlueprintLibrary::EnsurePortableRunning()
{
    return TryEnsurePortable();
}

void UComfyUIBlueprintLibrary::WatchWorkflowCompletion(const FString& PromptId, const FComfyUIWorkflowCompleteDelegate& OnComplete)
{
    EnsureWebSocketConnected();
    
    if (FComfyUIModule* Module = FModuleManager::GetModulePtr<FComfyUIModule>(TEXT("ComfyUI")))
    {
        TSharedPtr<FComfyUIWebSocketHandler> WSHandler = Module->GetWebSocketHandler();
        if (WSHandler.IsValid())
        {
            WSHandler->WatchPrompt(PromptId, OnComplete);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("ComfyUI: WebSocket handler not available"));
            OnComplete.ExecuteIfBound(false, PromptId);
        }
    }
}

void UComfyUIBlueprintLibrary::CheckComfyUIReady(const FComfyUIResponseDelegate& OnComplete)
{
    FString BaseUrl = GetBaseUrl();
    FString FullUrl = BaseUrl + TEXT("/system_stats");
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Checking readiness at: %s"), *FullUrl);
    
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FullUrl);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(5.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            const bool bReady = bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
            OnComplete.ExecuteIfBound(bReady, bReady ? TEXT("{\"status\":\"ready\"}") : TEXT("{\"status\":\"not_ready\"}"), TEXT(""));
        });

    Request->ProcessRequest();
}

void UComfyUIBlueprintLibrary::WaitForComfyUIReady(float TimeoutSeconds, const FComfyUIResponseDelegate& OnComplete)
{
    PollComfyUIReady(TimeoutSeconds, 0.0f, OnComplete);
}

FString UComfyUIBlueprintLibrary::GenerateUniqueAssetName(const FString& BasePath, const FString& Prefix)
{
    // Get current timestamp
    FDateTime Now = FDateTime::Now();
    
    // Format: Prefix_YYYYMMDD_HHMMSS
    FString Timestamp = FString::Printf(TEXT("%s_%04d%02d%02d_%02d%02d%02d"),
        *Prefix,
        Now.GetYear(),
        Now.GetMonth(),
        Now.GetDay(),
        Now.GetHour(),
        Now.GetMinute(),
        Now.GetSecond()
    );
    
    // Combine with base path
    FString FullPath = FPaths::Combine(BasePath, Timestamp);
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Generated unique asset name: %s"), *FullPath);
    
    return FullPath;
}

void UComfyUIBlueprintLibrary::PollComfyUIReady(float TimeoutSeconds, float ElapsedTime, const FComfyUIResponseDelegate& OnComplete)
{
    if (ElapsedTime >= TimeoutSeconds)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Timeout waiting for ComfyUI to be ready"));
        OnComplete.ExecuteIfBound(false, TEXT("{\"error\":\"timeout\"}"), TEXT(""));
        return;
    }

    FString BaseUrl = GetBaseUrl();
    FString FullUrl = BaseUrl + TEXT("/system_stats");
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Polling at: %s (%.1fs elapsed)"), *FullUrl, ElapsedTime);
    
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(FullUrl);
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(2.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [TimeoutSeconds, ElapsedTime, OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            const bool bReady = bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
            
            if (bReady)
            {
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Ready!"));
                OnComplete.ExecuteIfBound(true, TEXT("{\"status\":\"ready\"}"), TEXT(""));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Not ready yet, retrying..."));
                
                // Use FTSTicker instead of World timer
                FTSTicker::GetCoreTicker().AddTicker(
                    FTickerDelegate::CreateLambda(
                        [TimeoutSeconds, ElapsedTime, OnComplete](float DeltaTime)
                        {
                            PollComfyUIReady(TimeoutSeconds, ElapsedTime + 1.0f, OnComplete);
                            return false; // Don't repeat, we handle our own recursion
                        }
                    ),
                    1.0f // Delay 1 second
                );
            }
        });

    Request->ProcessRequest();
}

void UComfyUIBlueprintLibrary::SubmitWorkflowJson(const FString& WorkflowJson, const FComfyUISubmitOptions& Options, const FComfyUIResponseDelegate& OnComplete)
{
    TryEnsurePortable();

    TSharedPtr<FJsonObject> PromptObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WorkflowJson);
    if (!FJsonSerializer::Deserialize(Reader, PromptObject) || !PromptObject.IsValid())
    {
        OnComplete.ExecuteIfBound(false, TEXT("{\"error\":\"Invalid workflow JSON\"}"), TEXT(""));
        return;
    }

    FString RequestBody = BuildPromptWrapperJson(PromptObject, Options.ClientId);
    
    // ADD THIS LOGGING:
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Full request body being sent to /prompt:"));
    UE_LOG(LogTemp, Warning, TEXT("%s"), *RequestBody);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(GetBaseUrl() + TEXT("/prompt"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetContentAsString(RequestBody);

    Request->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
        {
            const bool bOk = bSucceeded && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode());
            const FString ResponseText = Response.IsValid() ? Response->GetContentAsString() : TEXT("{}");
            
            // Extract prompt_id from response
            FString PromptId;
            if (bOk)
            {
                TSharedPtr<FJsonObject> JsonResponse;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseText);
                if (FJsonSerializer::Deserialize(Reader, JsonResponse) && JsonResponse.IsValid())
                {
                    JsonResponse->TryGetStringField(TEXT("prompt_id"), PromptId);
                }
            }
            
            OnComplete.ExecuteIfBound(bOk, ResponseText, PromptId);
        });

    Request->ProcessRequest();
}

FString UComfyUIBlueprintLibrary::GetComfyUIOutputFolder()
{
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ComfyUI")))
    {
        return FPaths::Combine(Plugin->GetBaseDir(), TEXT("ComfyUI_windows_portable/ComfyUI/output"));
    }
    return TEXT("");
}

UTexture2D* UComfyUIBlueprintLibrary::LoadImageFromFile(const FString& FilePath)
{
    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Image file not found: %s"), *FilePath);
        return nullptr;
    }

    // Load image file into byte array
    TArray<uint8> RawFileData;
    if (!FFileHelper::LoadFileToArray(RawFileData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to load image file: %s"), *FilePath);
        return nullptr;
    }

    // Detect image format
    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
    
    EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(RawFileData.GetData(), RawFileData.Num());
    if (ImageFormat == EImageFormat::Invalid)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Invalid image format: %s"), *FilePath);
        return nullptr;
    }

    // Create image wrapper
    TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
    if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to decompress image: %s"), *FilePath);
        return nullptr;
    }

    // Get raw image data
    TArray<uint8> UncompressedBGRA;
    if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBGRA))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to get raw image data: %s"), *FilePath);
        return nullptr;
    }

    // Create texture
    UTexture2D* Texture = UTexture2D::CreateTransient(
        ImageWrapper->GetWidth(),
        ImageWrapper->GetHeight(),
        PF_B8G8R8A8
    );

    if (!Texture)
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to create texture"));
        return nullptr;
    }

    // Copy pixel data
    void* TextureData = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, UncompressedBGRA.GetData(), UncompressedBGRA.Num());
    Texture->GetPlatformData()->Mips[0].BulkData.Unlock();

    // Update texture
    Texture->UpdateResource();

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Successfully loaded image: %dx%d from %s"), 
        ImageWrapper->GetWidth(), ImageWrapper->GetHeight(), *FilePath);

    return Texture;
}


FString UComfyUIBlueprintLibrary::GetLatestOutputImage(const FString& FilenamePrefix)
{
    FString OutputFolder = GetComfyUIOutputFolder();
    if (OutputFolder.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Could not find output folder"));
        return TEXT("");
    }

    // Find all PNG files matching prefix
    TArray<FString> FoundFiles;
    IFileManager::Get().FindFiles(FoundFiles, *(OutputFolder / (FilenamePrefix + TEXT("*.png"))), true, false);

    if (FoundFiles.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI: No files found with prefix: %s"), *FilenamePrefix);
        return TEXT("");
    }

    // Find the most recent file
    FString LatestFile;
    FDateTime LatestTime = FDateTime::MinValue();

    for (const FString& File : FoundFiles)
    {
        FString FullPath = OutputFolder / File;
        FDateTime FileTime = IFileManager::Get().GetTimeStamp(*FullPath);
        
        if (FileTime > LatestTime)
        {
            LatestTime = FileTime;
            LatestFile = FullPath;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Latest output image: %s"), *LatestFile);
    return LatestFile;
}


UTexture2D* UComfyUIBlueprintLibrary::ImportImageAsAsset(const FString& SourceFilePath, const FString& DestinationPath)
{
    if (!FPaths::FileExists(SourceFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ComfyUI: Source file not found: %s"), *SourceFilePath);
        return nullptr;
    }

    // Example DestinationPath: "/Game/GeneratedTextures/MyImage"
    FString PackageName = DestinationPath;
    FString AssetName = FPaths::GetBaseFilename(PackageName);

    // Check if asset already exists - if so, delete it first
    UPackage* ExistingPackage = FindPackage(nullptr, *PackageName);
    if (ExistingPackage)
    {
        TArray<UObject*> ObjectsInPackage;
        GetObjectsWithOuter(ExistingPackage, ObjectsInPackage);
        for (UObject* Obj : ObjectsInPackage)
        {
            Obj->ClearFlags(RF_Standalone | RF_Public);
            Obj->ConditionalBeginDestroy();
        }
        ExistingPackage->ClearFlags(RF_Standalone | RF_Public);
        ExistingPackage->ConditionalBeginDestroy();
    }

    // Create new package
    UPackage* Package = CreatePackage(*PackageName);
    Package->FullyLoad();

    // Use texture factory to import
    UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
    TextureFactory->SuppressImportOverwriteDialog();

    // Import the texture
    bool bOperationCanceled = false;
    UTexture2D* Texture = Cast<UTexture2D>(TextureFactory->FactoryCreateFile(
        UTexture2D::StaticClass(),
        Package,
        *AssetName,
        RF_Public | RF_Standalone,
        SourceFilePath,
        TEXT(""),  // Parms
        GWarn,
        bOperationCanceled    // bOutOperationCanceled
    ));

    if (Texture)
    {
        // Notify asset registry
        FAssetRegistryModule::AssetCreated(Texture);
    
        // Mark package dirty
        Package->MarkPackageDirty();
    
        // Save the package
        FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GError;

        bool bSaved = UPackage::SavePackage(Package, Texture, *PackageFileName, SaveArgs);

        if (bSaved)
        {
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Successfully imported and saved texture: %s"), *PackageName);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("ComfyUI: Failed to save package: %s"), *PackageName);
        }
    }

    return Texture;
}





FString UComfyUIBlueprintLibrary::BuildFlux2WorkflowJson(const FComfyUIFlux2WorkflowParams& Params)
{
    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();

    const int32 UnetId = 10;
    const int32 ClipId = 12;          
    const int32 VaeId = 13;
    const int32 PositiveId = 14;
    const int32 NegativeId = 15;
    const int32 GuiderId = 16;
    const int32 LatentId = 17;
    const int32 NoiseId = 18;
    const int32 SchedulerId = 19;
    const int32 SamplerId = 20;
    const int32 CustomSamplerId = 21;
    const int32 VaeDecodeId = 22;
    const int32 SaveImageId = 23;  

    // UNETLoader
    TSharedPtr<FJsonObject> UnetNode = MakeNode(TEXT("UNETLoader"));
    SetInputString(UnetNode, TEXT("unet_name"), Params.UnetName);
    SetInputString(UnetNode, TEXT("weight_dtype"), TEXT("default"));
    Graph->SetObjectField(FString::FromInt(UnetId), UnetNode);

    // CLIPLoader
    TSharedPtr<FJsonObject> ClipNode = MakeNode(TEXT("CLIPLoader"));
    SetInputString(ClipNode, TEXT("clip_name"), Params.ClipName);
    SetInputString(ClipNode, TEXT("type"), TEXT("flux2"));
    SetInputString(ClipNode, TEXT("device"), TEXT("default"));
    Graph->SetObjectField(FString::FromInt(ClipId), ClipNode);

    // VAELoader
    TSharedPtr<FJsonObject> VaeNode = MakeNode(TEXT("VAELoader"));
    SetInputString(VaeNode, TEXT("vae_name"), Params.VaeName);
    Graph->SetObjectField(FString::FromInt(VaeId), VaeNode);

    // Positive CLIPTextEncode
    TSharedPtr<FJsonObject> PositiveNode = MakeNode(TEXT("CLIPTextEncode"));
    SetInputString(PositiveNode, TEXT("text"), Params.PositivePrompt);
    SetInputLink(PositiveNode, TEXT("clip"), ClipId, 0);
    Graph->SetObjectField(FString::FromInt(PositiveId), PositiveNode);

    // Negative CLIPTextEncode
    TSharedPtr<FJsonObject> NegativeNode = MakeNode(TEXT("CLIPTextEncode"));
    SetInputString(NegativeNode, TEXT("text"), Params.NegativePrompt);
    SetInputLink(NegativeNode, TEXT("clip"), ClipId, 0);
    Graph->SetObjectField(FString::FromInt(NegativeId), NegativeNode);

    // CFGGuider
    TSharedPtr<FJsonObject> GuiderNode = MakeNode(TEXT("CFGGuider"));
    SetInputLink(GuiderNode, TEXT("model"), UnetId, 0);            
    SetInputLink(GuiderNode, TEXT("positive"), PositiveId, 0);     
    SetInputLink(GuiderNode, TEXT("negative"), NegativeId, 0);
    SetInputNumber(GuiderNode, TEXT("cfg"), Params.CFGScale);
    Graph->SetObjectField(FString::FromInt(GuiderId), GuiderNode);

    // EmptyFlux2LatentImage
    TSharedPtr<FJsonObject> LatentNode = MakeNode(TEXT("EmptyFlux2LatentImage"));
    SetInputNumber(LatentNode, TEXT("width"), Params.Width);
    SetInputNumber(LatentNode, TEXT("height"), Params.Height);
    SetInputNumber(LatentNode, TEXT("batch_size"), 1);
    Graph->SetObjectField(FString::FromInt(LatentId), LatentNode);

    // RandomNoise
    TSharedPtr<FJsonObject> NoiseNode = MakeNode(TEXT("RandomNoise"));
    SetInputNumber(NoiseNode, TEXT("noise_seed"), Params.Seed >= 0 ? Params.Seed : FMath::Rand());
    Graph->SetObjectField(FString::FromInt(NoiseId), NoiseNode);

    // Flux2Scheduler
    TSharedPtr<FJsonObject> SchedulerNode = MakeNode(TEXT("Flux2Scheduler"));
    SetInputNumber(SchedulerNode, TEXT("steps"), Params.Steps);
    SetInputNumber(SchedulerNode, TEXT("width"), Params.Width);
    SetInputNumber(SchedulerNode, TEXT("height"), Params.Height);
    Graph->SetObjectField(FString::FromInt(SchedulerId), SchedulerNode);

    // KSamplerSelect
    TSharedPtr<FJsonObject> SamplerNode = MakeNode(TEXT("KSamplerSelect"));
    SetInputString(SamplerNode, TEXT("sampler_name"), Params.Sampler);
    Graph->SetObjectField(FString::FromInt(SamplerId), SamplerNode);

    // SamplerCustomAdvanced
    TSharedPtr<FJsonObject> CustomSamplerNode = MakeNode(TEXT("SamplerCustomAdvanced"));
    SetInputLink(CustomSamplerNode, TEXT("noise"), NoiseId, 0);    
    SetInputLink(CustomSamplerNode, TEXT("guider"), GuiderId, 0);  
    SetInputLink(CustomSamplerNode, TEXT("sampler"), SamplerId, 0);
    SetInputLink(CustomSamplerNode, TEXT("sigmas"), SchedulerId, 0);
    SetInputLink(CustomSamplerNode, TEXT("latent_image"), LatentId, 0);
    Graph->SetObjectField(FString::FromInt(CustomSamplerId), CustomSamplerNode);

    // VAEDecode
    TSharedPtr<FJsonObject> VaeDecodeNode = MakeNode(TEXT("VAEDecode"));
    SetInputLink(VaeDecodeNode, TEXT("samples"), CustomSamplerId, 0);
    SetInputLink(VaeDecodeNode, TEXT("vae"), VaeId, 0);   
    Graph->SetObjectField(FString::FromInt(VaeDecodeId), VaeDecodeNode);

    // SaveImage
    TSharedPtr<FJsonObject> SaveImageNode = MakeNode(TEXT("SaveImage"));
    SetInputLink(SaveImageNode, TEXT("images"), VaeDecodeId, 0);
    SetInputString(SaveImageNode, TEXT("filename_prefix"), Params.FilenamePrefix);
    Graph->SetObjectField(FString::FromInt(SaveImageId), SaveImageNode);

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Graph.ToSharedRef(), Writer);

    UE_LOG(LogTemp, Warning, TEXT("ComfyUI: Generated workflow JSON:"));
    UE_LOG(LogTemp, Warning, TEXT("%s"), *Output);
    
    return Output;
}

FString UComfyUIBlueprintLibrary::BuildPromptWrapperJson(const TSharedPtr<FJsonObject>& PromptObject, const FString& ClientId)
{
    TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetObjectField(TEXT("prompt"), PromptObject);

    const FString EffectiveClientId = ClientId.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::Digits) : ClientId;
    Wrapper->SetStringField(TEXT("client_id"), EffectiveClientId);

    FString Output;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);
    return Output;
}