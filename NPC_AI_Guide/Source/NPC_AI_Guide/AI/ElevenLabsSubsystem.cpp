// Copyright 2026 Tyler Munstock. All Rights Reserved.
#include "AI/ElevenLabsSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Kismet/GameplayStatics.h"

// pcm_22050 = 22 050 Hz, 16-bit, mono
const FString UElevenLabsSubsystem::ElevenLabsBaseURL =
	TEXT("https://api.elevenlabs.io/v1/text-to-speech");

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void UElevenLabsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadSecretsConfig();
}

void UElevenLabsSubsystem::LoadSecretsConfig()
{
	const FString SecretsPath = FPaths::ProjectConfigDir() / TEXT("Secrets.ini");

	FConfigFile SecretsConfig;
	SecretsConfig.Read(SecretsPath);

	const FString Section = TEXT("NPCGuide.APIKeys");
	SecretsConfig.GetString(*Section, TEXT("ElevenLabsAPIKey"), APIKey);
	SecretsConfig.GetString(*Section, TEXT("ElevenLabsVoiceId"), VoiceId);

	if (APIKey.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("UElevenLabsSubsystem: ElevenLabsAPIKey not found in Config/Secrets.ini. TTS calls will fail."));
	}
	if (VoiceId.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("UElevenLabsSubsystem: ElevenLabsVoiceId not set. SpeakText will fail until VoiceId is configured."));
	}
}

// ─── Public API ───────────────────────────────────────────────────────────────

void UElevenLabsSubsystem::SpeakText(const FString& Text)
{
	if (APIKey.IsEmpty())
	{
		const FString Msg = TEXT("ElevenLabsAPIKey is not set in Config/Secrets.ini.");
		UE_LOG(LogTemp, Error, TEXT("UElevenLabsSubsystem: %s"), *Msg);
		OnAudioFailed.Broadcast(Msg);
		return;
	}

	if (VoiceId.IsEmpty())
	{
		const FString Msg = TEXT("VoiceId is not set. Configure ElevenLabsVoiceId in Config/Secrets.ini or set it on the subsystem.");
		UE_LOG(LogTemp, Error, TEXT("UElevenLabsSubsystem: %s"), *Msg);
		OnAudioFailed.Broadcast(Msg);
		return;
	}

	if (Text.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("UElevenLabsSubsystem: SpeakText called with empty string — ignoring."));
		return;
	}

	// Strip *stage direction* expressions — these are expression guides for the voice
	// actor, not spoken words. The subtitle widget receives the original text with them.
	const FString SpeakableText = StripAsteriskExpressions(Text);
	if (SpeakableText.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("UElevenLabsSubsystem: Text was entirely asterisk expressions after stripping — ignoring TTS call."));
		return;
	}

	// Stop any currently playing voice line before starting a new one.
	StopCurrentSpeech();

	// Increment request ID so any in-flight response from a previous call is discarded.
	const int32 ThisRequestId = ++CurrentRequestId;

	// Build JSON body
	TSharedPtr<FJsonObject> RequestBody = MakeShared<FJsonObject>();
	RequestBody->SetStringField(TEXT("text"),     SpeakableText);
	RequestBody->SetStringField(TEXT("model_id"), ModelId);

	FString BodyString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(RequestBody.ToSharedRef(), Writer);

	// URL: /v1/text-to-speech/{voice_id}?output_format=pcm_22050
	const FString URL = FString::Printf(
		TEXT("%s/%s?output_format=pcm_22050"),
		*ElevenLabsBaseURL,
		*VoiceId
	);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(URL);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("xi-api-key"),   APIKey);
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(BodyString);
	// Prevent the request from hanging indefinitely if ElevenLabs is unreachable.
	HttpRequest->SetTimeout(15.0f);

	// Capture ThisRequestId in the lambda so stale responses can be discarded.
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[this, ThisRequestId](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
		{
			OnHTTPResponse(Req, Resp, bSuccess, ThisRequestId);
		}
	);

	HttpRequest->ProcessRequest();
}

// ─── Private ──────────────────────────────────────────────────────────────────

void UElevenLabsSubsystem::OnHTTPResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bConnectedSuccessfully,
	int32 RequestId)
{
	// Discard if a newer SpeakText call has already been issued
	if (RequestId != CurrentRequestId)
	{
		return;
	}

	if (!bConnectedSuccessfully || !Response.IsValid())
	{
		const FString Msg = TEXT("ElevenLabs HTTP request failed — no connection or invalid response.");
		UE_LOG(LogTemp, Error, TEXT("UElevenLabsSubsystem: %s"), *Msg);
		OnAudioFailed.Broadcast(Msg);
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	if (StatusCode != 200)
	{
		const FString Msg = FString::Printf(
			TEXT("ElevenLabs API returned status %d. Body: %s"),
			StatusCode,
			*Response->GetContentAsString()
		);
		UE_LOG(LogTemp, Error, TEXT("UElevenLabsSubsystem: %s"), *Msg);
		OnAudioFailed.Broadcast(Msg);
		return;
	}

	const TArray<uint8>& PCMData = Response->GetContent();
	if (PCMData.IsEmpty())
	{
		const FString Msg = TEXT("ElevenLabs returned empty audio data.");
		UE_LOG(LogTemp, Error, TEXT("UElevenLabsSubsystem: %s"), *Msg);
		OnAudioFailed.Broadcast(Msg);
		return;
	}

	UAudioComponent* AudioComponent = PlayPCMData(PCMData);
	if (!AudioComponent)
	{
		const FString Msg = TEXT("Failed to create AudioComponent from PCM data.");
		UE_LOG(LogTemp, Error, TEXT("UElevenLabsSubsystem: %s"), *Msg);
		OnAudioFailed.Broadcast(Msg);
		return;
	}

	OnAudioReady.Broadcast(AudioComponent);
}

UAudioComponent* UElevenLabsSubsystem::PlayPCMData(const TArray<uint8>& PCMData)
{
	// 16-bit mono PCM: 2 bytes per sample
	const int32 NumSamples = PCMData.Num() / 2;
	// Add a small buffer so the timer fires slightly after the audio finishes rather
	// than slightly before. The audio engine queues the PCM asynchronously, so
	// the playback start lags the timer start by a small amount.
	const float Duration   = (static_cast<float>(NumSamples) / static_cast<float>(PCMSampleRate)) + 0.2f;

	USoundWaveProcedural* SoundWave = NewObject<USoundWaveProcedural>(GetGameInstance());
	SoundWave->SetSampleRate(PCMSampleRate);
	SoundWave->NumChannels    = PCMNumChannels;
	SoundWave->SoundGroup     = SOUNDGROUP_Voice;
	SoundWave->bLooping       = false;
	SoundWave->Duration       = Duration;
	SoundWave->QueueAudio(PCMData.GetData(), PCMData.Num());

	// Hold a reference to prevent GC while audio plays
	ActiveSoundWave = SoundWave;

	UWorld* World = GetGameInstance()->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("UElevenLabsSubsystem: No valid World to spawn audio in."));
		return nullptr;
	}

	UAudioComponent* AudioComponent = UGameplayStatics::SpawnSound2D(
		World,
		SoundWave,
		/* VolumeMultiplier */ 1.0f,
		/* PitchMultiplier  */ 1.0f,
		/* StartTime        */ 0.0f,
		/* ConcurrencySettings */ nullptr,
		/* bPersistAcrossLevelTransition */ false,
		/* bAutoDestroy */ true
	);

	// Cache so StopCurrentSpeech can halt it mid-line.
	ActiveAudioComponent = AudioComponent;

	// UAudioComponent::OnAudioFinished does not fire reliably for procedural
	// sound waves in UE5. Use a timer driven by the known PCM duration instead.
	UWorld* TimerWorld = GetGameInstance()->GetWorld();
	if (TimerWorld)
	{
		TimerWorld->GetTimerManager().SetTimer(
			SpeechCompletedTimer,
			this,
			&UElevenLabsSubsystem::OnSpeechTimerFinished,
			Duration,
			/*bLoop=*/false
		);
	}

	return AudioComponent;
}

void UElevenLabsSubsystem::OnSpeechTimerFinished()
{
	ActiveAudioComponent = nullptr;
	ActiveSoundWave      = nullptr;
	OnSpeechCompleted.Broadcast();
}

FString UElevenLabsSubsystem::StripAsteriskExpressions(const FString& Input)
{
	FString Out;
	Out.Reserve(Input.Len());

	int32 i = 0;
	while (i < Input.Len())
	{
		if (Input[i] != TEXT('*'))
		{
			Out += Input[i++];
			continue;
		}

		// Find the matching closing asterisk.
		const int32 CloseIdx = Input.Find(TEXT("*"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i + 1);
		if (CloseIdx == INDEX_NONE)
		{
			// Unmatched asterisk — skip it, keep the rest as normal text.
			i++;
			continue;
		}

		// Strip all *...* content entirely — stage directions and emphasis alike.
		// The system prompt instructs AXIOM to write plain text only, so anything
		// remaining in asterisks is noise and must not reach TTS or subtitles.
		i = CloseIdx + 1;
	}

	// Collapse whitespace runs left by stripped stage directions, then trim.
	FString Prev;
	do
	{
		Prev = Out;
		Out.ReplaceInline(TEXT("  "), TEXT(" "));
		Out.ReplaceInline(TEXT("\n\n"), TEXT("\n"));
	}
	while (Out != Prev);

	Out.TrimStartAndEndInline();
	return Out;
}

void UElevenLabsSubsystem::StopCurrentSpeech()
{
	// Cancel the completion timer so OnSpeechCompleted doesn't fire for a
	// line that was manually stopped.
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(SpeechCompletedTimer);
	}

	if (ActiveAudioComponent && ActiveAudioComponent->IsPlaying())
	{
		ActiveAudioComponent->Stop();
	}
	ActiveAudioComponent = nullptr;
	ActiveSoundWave      = nullptr;
}
