#include "ComfyUIWebSocketHandler.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FComfyUIWebSocketHandler::FComfyUIWebSocketHandler()
{
}

FComfyUIWebSocketHandler::~FComfyUIWebSocketHandler()
{
    Disconnect();
}

void FComfyUIWebSocketHandler::Connect(const FString& Url, const FString& InClientId)
{
    ClientId = InClientId;
    
    // Create WebSocket with client_id parameter
    FString WebSocketUrl = FString::Printf(TEXT("%s?clientId=%s"), *Url, *ClientId);
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Connecting to %s"), *WebSocketUrl);
    
    WebSocket = FWebSocketsModule::Get().CreateWebSocket(WebSocketUrl, TEXT(""));
    
    // Bind event handlers
    WebSocket->OnConnected().AddRaw(this, &FComfyUIWebSocketHandler::OnConnected);
    WebSocket->OnConnectionError().AddRaw(this, &FComfyUIWebSocketHandler::OnConnectionError);
    WebSocket->OnClosed().AddRaw(this, &FComfyUIWebSocketHandler::OnClosed);
    WebSocket->OnMessage().AddRaw(this, &FComfyUIWebSocketHandler::OnMessage);
    
    // Connect
    WebSocket->Connect();
}

void FComfyUIWebSocketHandler::Disconnect()
{
    if (WebSocket.IsValid() && WebSocket->IsConnected())
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Disconnecting"));
        WebSocket->Close();
    }
    
    WebSocket.Reset();
    PromptCallbacks.Empty();
}

bool FComfyUIWebSocketHandler::IsConnected() const
{
    return WebSocket.IsValid() && WebSocket->IsConnected();
}

void FComfyUIWebSocketHandler::WatchPrompt(const FString& PromptId, FComfyUIWorkflowCompleteDelegate Callback)
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Watching prompt %s"), *PromptId);
    PromptCallbacks.Add(PromptId, Callback);
}

void FComfyUIWebSocketHandler::OnConnected()
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Connected successfully"));
}

void FComfyUIWebSocketHandler::OnConnectionError(const FString& Error)
{
    UE_LOG(LogTemp, Error, TEXT("ComfyUI WebSocket: Connection error: %s"), *Error);
}

void FComfyUIWebSocketHandler::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Closed (Status: %d, Reason: %s, Clean: %d)"), 
        StatusCode, *Reason, bWasClean);
}

void FComfyUIWebSocketHandler::OnMessage(const FString& Message)
{
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: RAW MESSAGE: %s"), *Message);
    
    // Parse JSON message
    TSharedPtr<FJsonObject> JsonMessage;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    
    if (!FJsonSerializer::Deserialize(Reader, JsonMessage) || !JsonMessage.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Failed to parse message"));
        return;
    }
    
    // Get message type
    FString MessageType;
    if (!JsonMessage->TryGetStringField(TEXT("type"), MessageType))
    {
        return;
    }
    
    // Handle status messages for queue completion
    if (MessageType == TEXT("status"))
    {
        HandleStatusMessage(JsonMessage);
    }
    // Keep the old handler too in case some ComfyUI versions send it
    else if (MessageType == TEXT("executed") || MessageType == TEXT("execution_cached"))
    {
        HandleExecutionMessage(JsonMessage);
    }
}

void FComfyUIWebSocketHandler::HandleExecutionMessage(const TSharedPtr<FJsonObject>& JsonMessage)
{
    // Get the data object
    const TSharedPtr<FJsonObject>* DataObject;
    if (!JsonMessage->TryGetObjectField(TEXT("data"), DataObject))
    {
        return;
    }
    
    // Get prompt_id
    FString PromptId;
    if (!(*DataObject)->TryGetStringField(TEXT("prompt_id"), PromptId))
    {
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Execution update for prompt %s"), *PromptId);
    
    // Check if this is a final execution message (has output data)
    const TSharedPtr<FJsonObject>* OutputObject;
    if ((*DataObject)->TryGetObjectField(TEXT("output"), OutputObject))
    {
        // This prompt has completed - find and execute callback
        if (FComfyUIWorkflowCompleteDelegate* Callback = PromptCallbacks.Find(PromptId))
        {
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Prompt %s completed!"), *PromptId);
            Callback->ExecuteIfBound(true, PromptId);
            PromptCallbacks.Remove(PromptId);
        }
    }
}
void FComfyUIWebSocketHandler::HandleStatusMessage(const TSharedPtr<FJsonObject>& JsonMessage)
{
    // Get the data object
    const TSharedPtr<FJsonObject>* DataObject;
    if (!JsonMessage->TryGetObjectField(TEXT("data"), DataObject))
    {
        return;
    }
    
    // Get the status object
    const TSharedPtr<FJsonObject>* StatusObject;
    if (!(*DataObject)->TryGetObjectField(TEXT("status"), StatusObject))
    {
        return;
    }
    
    // Get exec_info object
    const TSharedPtr<FJsonObject>* ExecInfoObject;
    if (!(*StatusObject)->TryGetObjectField(TEXT("exec_info"), ExecInfoObject))
    {
        return;
    }
    
    // Get queue_remaining
    int32 QueueRemaining = 0;
    if (!(*ExecInfoObject)->TryGetNumberField(TEXT("queue_remaining"), QueueRemaining))
    {
        return;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Queue remaining: %d"), QueueRemaining);
    
    // If queue is now empty, trigger ALL waiting callbacks
    if (QueueRemaining == 0 && PromptCallbacks.Num() > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Queue complete! Triggering %d callbacks"), PromptCallbacks.Num());
        
        // Execute all callbacks
        for (auto& Pair : PromptCallbacks)
        {
            UE_LOG(LogTemp, Warning, TEXT("ComfyUI WebSocket: Completing prompt %s"), *Pair.Key);
            Pair.Value.ExecuteIfBound(true, Pair.Key);
        }
        
        // Clear all callbacks
        PromptCallbacks.Empty();
    }
}