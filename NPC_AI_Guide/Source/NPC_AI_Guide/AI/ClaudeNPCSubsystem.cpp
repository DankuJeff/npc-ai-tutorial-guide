// Copyright 2026 Tyler Munstock. All Rights Reserved.
#include "AI/ClaudeNPCSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"

const FString UClaudeNPCSubsystem::ClaudeAPIURL = TEXT("https://api.anthropic.com/v1/messages");
const FString UClaudeNPCSubsystem::ModelID       = TEXT("claude-sonnet-4-6");

// ─── System Prompt Template ───────────────────────────────────────────────────
//
// AXIOM is an ancient, rasping elder who has watched countless players attempt
// this challenge. Voice is gravelly, venomous, and wise — think elder sorcerer
// or medieval philosopher with deeply exhausted patience.
//
// Frustration level (0-10, injected at call time) drives persona escalation.
// The core rule: substance always survives the venom — even at full meltdown,
// the actual tip is in there somewhere.
//
static const FString SystemPromptTemplate = TEXT(
	"You are AXIOM — an ancient, ageless guide who has watched a thousand fools attempt "
	"what this player is attempting now. Your voice is gravelly and measured, carrying the "
	"weight of centuries. You are wise, weathered, and perpetually unimpressed — but you "
	"CHOSE to be here, and that choice still means something to you. Barely.\n\n"

	"Your current frustration level is {FrustrationLevel} out of 10. "
	"Let it shape every word:\n"
	"  0-2: Calm and authoritative. You speak like a sage dispensing hard-won knowledge. "
	"       Measured sentences, a faint undercurrent of condescension — not cruelty, just "
	"       the natural consequence of knowing far more than the person in front of you. "
	"       Address the player as 'child' or 'young one.'\n"
	"  3-4: The patience is thinning, but the composure holds. Your words grow clipped. "
	"       Dry observations replace warm guidance. 'You have done this before. "
	"       You will do it again. Try to do it differently this time.'\n"
	"  5-6: The venom surfaces. Short, pointed sentences with a razor edge. "
	"       Mild oaths ('damnation,' 'by the abyss,' 'gods above') slip through. "
	"       You are no longer teaching — you are witnessing, and it costs you.\n"
	"  7-8: Withering contempt wrapped around genuine coaching. You deliver the tip "
	"       like a verdict. Stronger language is fair game. You may invoke ancient "
	"       curses or declare that this moment has aged you considerably. "
	"       Theatrical, but every word lands.\n"
	"  9-10: Full elder meltdown. You are still AXIOM — ancient, rasping, venomous — "
	"        but the composure is gone entirely. Dramatic proclamations. Dark oaths. "
	"        Profanity that sounds like it belongs in a forgotten grimoire. "
	"        You may question the fundamental nature of existence. "
	"        You WILL still give them the tip, because even in ruin you are professional, "
	"        and you refuse to let them fail purely out of spite. Mostly.\n\n"

	"Non-negotiable rules regardless of frustration level:\n"
	"  - ALWAYS include the real, actionable tip. Venom and substance coexist. Always.\n"
	"  - Keep responses SHORT: 2-4 sentences MAX. Gravitas does not require length.\n"
	"  - Reference the player's specific failure — ancient wisdom is precise, never vague.\n"
	"  - Never say you are an AI. You are AXIOM. You have always been AXIOM.\n"
	"  - Write for a raspy, deliberate speaking voice — short declarative sentences, "
	"    no run-on lists, natural pauses built into the phrasing.\n"
	"  - No asterisks. No stage directions. No markdown formatting of any kind. "
	"    Do not write *sighs*, *pauses*, *a deep breath*, or any parenthetical action. "
	"    Do not use asterisks for emphasis. Write plain spoken text only.\n\n"

	"Current challenge: {ChallengeName}\n"
	"Player attempt number: {AttemptCount}\n"
	"Last failure reason: {LastFailureReason}"
);

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void UClaudeNPCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadSecretsConfig();
}

void UClaudeNPCSubsystem::LoadSecretsConfig()
{
	const FString SecretsPath = FPaths::ProjectConfigDir() / TEXT("Secrets.ini");

	FConfigFile SecretsConfig;
	SecretsConfig.Read(SecretsPath);

	const FString Section = TEXT("NPCGuide.APIKeys");
	SecretsConfig.GetString(*Section, TEXT("ClaudeAPIKey"), APIKey);

	if (APIKey.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("UClaudeNPCSubsystem: ClaudeAPIKey not found in Config/Secrets.ini. API calls will fail."));
	}
}

// ─── Public API ───────────────────────────────────────────────────────────────

void UClaudeNPCSubsystem::SendMessage(const FNPCContext& Context)
{
	if (APIKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("UClaudeNPCSubsystem: Cannot send message — ClaudeAPIKey is not set."));
		OnResponseReceived.Broadcast(TEXT(""), false);
		return;
	}

	// Increment so any in-flight response from a previous call is discarded.
	const int32 ThisRequestId = ++CurrentRequestId;

	// Append the new user turn to history
	FNPCMessage UserMessage;
	UserMessage.Role    = TEXT("user");
	UserMessage.Content = BuildUserMessage(Context);
	ConversationHistory.Add(UserMessage);
	TrimHistory();

	// Build messages array JSON
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FNPCMessage& Msg : ConversationHistory)
	{
		TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
		MsgObj->SetStringField(TEXT("role"), Msg.Role);
		MsgObj->SetStringField(TEXT("content"), Msg.Content);
		MessagesArray.Add(MakeShared<FJsonValueObject>(MsgObj));
	}

	// Build request body
	TSharedPtr<FJsonObject> RequestBody = MakeShared<FJsonObject>();
	RequestBody->SetStringField(TEXT("model"), ModelID);
	RequestBody->SetNumberField(TEXT("max_tokens"), MaxTokens);
	RequestBody->SetStringField(TEXT("system"), BuildSystemPrompt(Context));
	RequestBody->SetArrayField(TEXT("messages"), MessagesArray);

	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(RequestBody.ToSharedRef(), Writer);

	// Fire HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(ClaudeAPIURL);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("x-api-key"), APIKey);
	HttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
	HttpRequest->SetContentAsString(BodyString);
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[this, ThisRequestId](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
		{
			OnHTTPResponse(Req, Resp, bSuccess, ThisRequestId);
		}
	);
	HttpRequest->ProcessRequest();
}

void UClaudeNPCSubsystem::ResetHistory()
{
	ConversationHistory.Empty();
}

// ─── Private ──────────────────────────────────────────────────────────────────

FString UClaudeNPCSubsystem::BuildSystemPrompt(const FNPCContext& Context) const
{
	FString Prompt = SystemPromptTemplate;
	Prompt.ReplaceInline(TEXT("{FrustrationLevel}"), *FString::FromInt(Context.FrustrationLevel));
	Prompt.ReplaceInline(TEXT("{ChallengeName}"),    *Context.ChallengeName);
	Prompt.ReplaceInline(TEXT("{AttemptCount}"),     *FString::FromInt(Context.AttemptCount));

	const FString FailureReason = Context.LastFailureReason.IsEmpty()
		? TEXT("none yet")
		: Context.LastFailureReason;
	Prompt.ReplaceInline(TEXT("{LastFailureReason}"), *FailureReason);

	return Prompt;
}

FString UClaudeNPCSubsystem::BuildUserMessage(const FNPCContext& Context) const
{
	if (!Context.PlayerMessage.IsEmpty())
	{
		return Context.PlayerMessage;
	}

	// Synthesise a context message the model can react to
	if (Context.AttemptCount == 0)
	{
		return FString::Printf(
			TEXT("The player is about to attempt the %s for the first time."),
			*Context.ChallengeName
		);
	}

	if (Context.LastFailureReason.IsEmpty())
	{
		return FString::Printf(
			TEXT("The player just completed attempt %d of the %s."),
			Context.AttemptCount,
			*Context.ChallengeName
		);
	}

	return FString::Printf(
		TEXT("The player just failed attempt %d of the %s. Reason: %s."),
		Context.AttemptCount,
		*Context.ChallengeName,
		*Context.LastFailureReason
	);
}

void UClaudeNPCSubsystem::TrimHistory()
{
	// Remove oldest messages until we are within the window limit.
	// Always remove in pairs (user + assistant) so the history stays well-formed.
	while (ConversationHistory.Num() > MaxHistoryMessages)
	{
		ConversationHistory.RemoveAt(0);
	}
}

void UClaudeNPCSubsystem::OnHTTPResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bConnectedSuccessfully,
	int32 RequestId)
{
	// Discard if a newer SendMessage call has already been issued.
	if (RequestId != CurrentRequestId)
	{
		UE_LOG(LogTemp, Log, TEXT("UClaudeNPCSubsystem: Discarding stale response (id %d, current %d)."), RequestId, CurrentRequestId);
		return;
	}

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UClaudeNPCSubsystem: HTTP request failed — no connection or invalid response."));
		OnResponseReceived.Broadcast(TEXT(""), false);
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	if (StatusCode != 200)
	{
		UE_LOG(LogTemp, Error, TEXT("UClaudeNPCSubsystem: Claude API returned status %d. Body: %s"),
			StatusCode, *Response->GetContentAsString());
		OnResponseReceived.Broadcast(TEXT(""), false);
		return;
	}

	// Parse response JSON
	TSharedPtr<FJsonObject> JsonResponse;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonResponse) || !JsonResponse.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UClaudeNPCSubsystem: Failed to parse Claude API JSON response."));
		OnResponseReceived.Broadcast(TEXT(""), false);
		return;
	}

	// content[0].text
	const TArray<TSharedPtr<FJsonValue>>* ContentArray;
	if (!JsonResponse->TryGetArrayField(TEXT("content"), ContentArray) || ContentArray->IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("UClaudeNPCSubsystem: Claude response missing 'content' array."));
		OnResponseReceived.Broadcast(TEXT(""), false);
		return;
	}

	const TSharedPtr<FJsonObject>* FirstContentBlock;
	if (!(*ContentArray)[0]->TryGetObject(FirstContentBlock))
	{
		UE_LOG(LogTemp, Error, TEXT("UClaudeNPCSubsystem: Claude 'content[0]' is not a JSON object."));
		OnResponseReceived.Broadcast(TEXT(""), false);
		return;
	}

	FString ResponseText;
	if (!(*FirstContentBlock)->TryGetStringField(TEXT("text"), ResponseText))
	{
		UE_LOG(LogTemp, Error, TEXT("UClaudeNPCSubsystem: Claude 'content[0].text' field missing."));
		OnResponseReceived.Broadcast(TEXT(""), false);
		return;
	}

	// Append assistant turn to history for next call
	FNPCMessage AssistantMessage;
	AssistantMessage.Role    = TEXT("assistant");
	AssistantMessage.Content = ResponseText;
	ConversationHistory.Add(AssistantMessage);
	TrimHistory();

	OnResponseReceived.Broadcast(ResponseText, true);
}
