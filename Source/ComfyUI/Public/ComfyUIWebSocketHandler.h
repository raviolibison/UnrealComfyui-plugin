#pragma once

#include "CoreMinimal.h"
#include "IWebSocket.h"
#include "ComfyUIRequestTypes.h"

class COMFYUI_API FComfyUIWebSocketHandler : public TSharedFromThis<FComfyUIWebSocketHandler>
{
public:
    FComfyUIWebSocketHandler();
    ~FComfyUIWebSocketHandler();

    void Connect(const FString& Url);
    void Disconnect();
    bool IsConnected() const;

    void WatchPrompt(const FString& PromptId, const FComfyUIWorkflowCompleteDelegateNative& Callback);

private:
    void OnConnected();
    void OnConnectionError(const FString& Error);
    void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void OnMessage(const FString& Message);

    TSharedPtr<IWebSocket> WebSocket;
    TMap<FString, FComfyUIWorkflowCompleteDelegateNative> PromptCallbacks;
    bool bIsConnected = false;
};
