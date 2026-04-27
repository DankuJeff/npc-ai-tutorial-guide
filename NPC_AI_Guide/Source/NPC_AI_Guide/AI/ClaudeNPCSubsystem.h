// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "AI/NPCTypes.h"
#include "ClaudeNPCSubsystem.generated.h"

// Fired on the game thread when Claude responds (or when the request fails).
// ResponseText is the NPC's dialogue; bSuccess is false if the API call failed.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNPCResponseReceived, const FString&, ResponseText, bool, bSuccess);

/**
 * UClaudeNPCSubsystem
 *
 * GameInstanceSubsystem that owns all Claude API communication for the NPC Guide.
 * Maintains a sliding window of conversation history and assembles per-call context
 * into the AXIOM system prompt.
 *
 * AXIOM persona: ancient, gravelly elder — wise and venomous, with centuries of
 * exhausted patience. Frustration level (0-10 in FNPCContext) escalates the tone
 * from measured sage to full elder meltdown while always delivering the real tip.
 *
 * Bind OnResponseReceived in Blueprint to drive speech bubble + ElevenLabsSubsystem.
 */
UCLASS()
class NPC_AI_GUIDE_API UClaudeNPCSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ─── Public API ───────────────────────────────────────────────────────────

	/**
	 * Send the current game context to Claude and receive AXIOM's coaching response.
	 * Fires OnResponseReceived when the response arrives.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void SendMessage(const FNPCContext& Context);

	/** Clear conversation history (e.g., when moving to a new challenge zone). */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void ResetHistory();

	// ─── Delegates ────────────────────────────────────────────────────────────

	/** Broadcast when Claude responds or when the request fails. */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnNPCResponseReceived OnResponseReceived;

	// ─── Config ───────────────────────────────────────────────────────────────

	/**
	 * Maximum number of messages kept in the sliding window.
	 * Counts individual messages (not pairs), so 10 = 5 exchanges.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	int32 MaxHistoryMessages = 10;

	/** Max tokens Claude may generate per response. Keep low — NPC lines are short. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	int32 MaxTokens = 200;

private:
	TArray<FNPCMessage> ConversationHistory;
	FString APIKey;

	// Incremented on every SendMessage call. OnHTTPResponse discards any response
	// whose captured ID no longer matches — prevents stale coaching lines from
	// interrupting a success line that was requested afterward.
	int32 CurrentRequestId = 0;

	static const FString ClaudeAPIURL;
	static const FString ModelID;

	void LoadSecretsConfig();
	FString BuildSystemPrompt(const FNPCContext& Context) const;
	FString BuildUserMessage(const FNPCContext& Context) const;
	void TrimHistory();

	void OnHTTPResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, int32 RequestId);
};
