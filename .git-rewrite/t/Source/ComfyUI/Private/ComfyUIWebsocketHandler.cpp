#include "ComfyUIWebSocketHandler.h"
#include "WebSocketsModule.h"
#include "Serialization/JsonSerializer.h"

FComfyUIWebSocketHandler::FComfyUIWebSocketHandler()
{
}

FComfyUIWebSocketHandler::~FComfyUIWebSocketHandler()
{
    Disconnect();
}

void FComfyUIWebSocketHandler::Connect(const FString& Url)
{
    if (bIsConnected)
    {
        return;
    }

    FWebSocketsModule& Module = FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    WebSocket = Module.CreateWebSocket(Url, TEXT("ws"));

    WebSocket->OnConnected().AddRaw(this, &FComfyUIWebSocketHandler::OnConnected);
    WebSocket->OnConnectionError().AddRaw(this, &FComfyUIWebSocketHandler::OnConnectionError);
    WebSocket->OnClosed().AddRaw(this, &FComfyUIWebSocketHandler::OnClosed);
    WebSocket->OnMessage().AddRaw(this, &FComfyUIWebSocketHandler::OnMessage);

    WebSocket->Connect();
}

void FComfyUIWebSocketHandler::Disconnect()
{
    if (WebSocket.IsValid())
    {
        WebSocket->Close();
        WebSocket.Reset();
    }
    bIsConnected = false;
}

bool FComfyUIWebSocketHandler::IsConnected() const
{
    return bIsConnected;
}

void FComfyUIWebSocketHandler::OnConnected()
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Connected"));
    bIsConnected = true;
}

void FComfyUIWebSocketHandler::OnConnectionError(const FString& Error)
{
    UE_LOG(LogTemp, Error, TEXT("ComfyUI WebSocket: Connection error - %s"), *Error);
    bIsConnected = false;
}

void FComfyUIWebSocketHandler::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Closed (%d: %s)"), StatusCode, *Reason);
    bIsConnected = false;
}

void FComfyUIWebSocketHandler::OnMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> JsonObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return;
    }

    FString Type = JsonObject->GetStringField(TEXT("type"));

    // Use queue_remaining status as the completion signal
    if (Type == TEXT("status"))
    {
        const TSharedPtr<FJsonObject>* DataObject;
        if (JsonObject->TryGetObjectField(TEXT("data"), DataObject))
        {
            const TSharedPtr<FJsonObject>* StatusObject;
            if ((*DataObject)->TryGetObjectField(TEXT("status"), StatusObject))  // Fixed: added closing quote
            {
                const TSharedPtr<FJsonObject>* ExecInfoObject;
                if ((*StatusObject)->TryGetObjectField(TEXT("exec_info"), ExecInfoObject))
                {
                    int32 QueueRemaining = (*ExecInfoObject)->GetIntegerField(TEXT("queue_remaining"));
                    
                    // When queue empties, trigger all pending callbacks
                    if (QueueRemaining == 0 && PromptCallbacks.Num() > 0)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Queue complete, triggering %d callback(s)"), PromptCallbacks.Num());
                        
                        // Trigger all pending callbacks
                        TArray<FString> CompletedPrompts;
                        for (auto& Pair : PromptCallbacks)
                        {
                            UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Executing callback for prompt %s"), *Pair.Key);
                            Pair.Value.ExecuteIfBound(true, Pair.Key);
                            CompletedPrompts.Add(Pair.Key);
                        }
                        
                        // Clear all callbacks
                        for (const FString& PromptId : CompletedPrompts)
                        {
                            PromptCallbacks.Remove(PromptId);
                        }
                    }
                }
            }
        }
    }
    // Handle errors
    else if (Type == TEXT("execution_error"))
    {
        const TSharedPtr<FJsonObject>* DataObject;
        if (JsonObject->TryGetObjectField(TEXT("data"), DataObject))
        {
            FString PromptId;
            if ((*DataObject)->TryGetStringField(TEXT("prompt_id"), PromptId))
            {
                UE_LOG(LogTemp, Error, TEXT("ComfyUI WebSocket: Execution error for prompt %s"), *PromptId);

                if (FComfyUIWorkflowCompleteDelegateNative* Callback = PromptCallbacks.Find(PromptId))
                {
                    Callback->ExecuteIfBound(false, PromptId);
                    PromptCallbacks.Remove(PromptId);
                }
            }
        }
    }
}

void FComfyUIWebSocketHandler::WatchPrompt(const FString& PromptId, const FComfyUIWorkflowCompleteDelegateNative& Callback)
{
    PromptCallbacks.Add(PromptId, Callback);
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Registered watcher for prompt %s (total watchers: %d)"), *PromptId, PromptCallbacks.Num());
}
