#pragma once

#include "CoreMinimal.h"
#include "IWebSocket.h"
#include "ComfyUIRequestTypes.h"

class COMFYUI_API FComfyUIWebSocketHandler : public TSharedFromThis<FComfyUIWebSocketHandler>
{
public:
	FComfyUIWebSocketHandler();
	~FComfyUIWebSocketHandler();

	// Connect to ComfyUI WebSocket
	void Connect(const FString& Url, const FString& ClientId);
    
	// Disconnect
	void Disconnect();
    
	// Check if connected
	bool IsConnected() const;
    
	// Register callback for specific prompt completion
	void WatchPrompt(const FString& PromptId, FComfyUIWorkflowCompleteDelegate Callback);

private:
	TSharedPtr<IWebSocket> WebSocket;
	FString ClientId;
    
	// Map of prompt IDs to their callbacks
	TMap<FString, FComfyUIWorkflowCompleteDelegate> PromptCallbacks;
    
	// WebSocket event handlers
	void OnConnected();
	void OnConnectionError(const FString& Error);
	void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnMessage(const FString& Message);
    
	// Parse and handle different message types
	void HandleExecutionMessage(const TSharedPtr<FJsonObject>& JsonMessage);
	void HandleStatusMessage(const TSharedPtr<FJsonObject>& JsonMessage);
};