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
	OnConnectedEvent.Broadcast();
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

    FString Type;
    if (!JsonObject->TryGetStringField(TEXT("type"), Type))
        return;

    // execution_complete fires once per prompt with the exact prompt_id
    // This is the correct signal for multi-user scenarios — each client
    // only reacts to their own job, not the global queue emptying
    if (Type == TEXT("execution_complete"))
    {
        const TSharedPtr<FJsonObject>* DataObject;
        if (JsonObject->TryGetObjectField(TEXT("data"), DataObject))
        {
            FString PromptId;
            if ((*DataObject)->TryGetStringField(TEXT("prompt_id"), PromptId))
            {
                UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: execution_complete for prompt %s"), *PromptId);

                if (FComfyUIWorkflowCompleteDelegateNative* Callback = PromptCallbacks.Find(PromptId))
                {
                    Callback->ExecuteIfBound(true, PromptId);
                    PromptCallbacks.Remove(PromptId);
                }
                else
                {
                    // Not our prompt — belongs to another user, ignore it
                    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: execution_complete for untracked prompt %s (another user's job)"), *PromptId);
                }
            }
        }
    }
    // Handle errors — also prompt-specific
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

void FComfyUIWebSocketHandler::UnwatchPrompt(const FString& PromptId)
{
    if (PromptCallbacks.Remove(PromptId) > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Removed stale watcher for prompt %s (total watchers: %d)"),
            *PromptId, PromptCallbacks.Num());
    }
}
