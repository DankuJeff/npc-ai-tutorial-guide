// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "ElevenLabsSubsystem.generated.h"

/**
 * Fired when audio is ready and playing.
 * AudioComponent can be used by Blueprint to track speaking state (bind OnAudioFinished on it).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCAudioReady, UAudioComponent*, AudioComponent);

/** Fired when the ElevenLabs request fails. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCAudioFailed, const FString&, ErrorMessage);

/**
 * Fired when a voice line finishes playing naturally.
 * Driven by a timer calculated from the PCM data duration — more reliable than
 * binding to UAudioComponent::OnAudioFinished, which does not fire consistently
 * for procedural sound waves in UE5.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNPCSpeechCompleted);

/**
 * UElevenLabsSubsystem
 *
 * GameInstanceSubsystem that converts text to voice via the ElevenLabs API.
 * Requests raw PCM audio (22 050 Hz, 16-bit mono) and feeds it directly into
 * a USoundWaveProcedural — no disk I/O, no MP3 decoder required.
 *
 * Bind OnAudioReady to drive the NPC Speaking state and feed dialogue into
 * the speech bubble widget. Bind OnAudioFailed to handle degraded-mode fallback
 * (text-only display).
 */
UCLASS()
class NPC_AI_GUIDE_API UElevenLabsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ─── Public API ───────────────────────────────────────────────────────────

	/**
	 * Convert text to speech and play it.
	 * Fires OnAudioReady on success, OnAudioFailed on error.
	 * Automatically stops any currently playing line before starting the new one.
	 * If a previous HTTP request is still in-flight, its response is discarded.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void SpeakText(const FString& Text);

	/**
	 * Immediately stop any voice line that is currently playing and clear subtitle state.
	 * Called automatically by SpeakText — also available to Blueprint for manual cancellation.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void StopCurrentSpeech();

	// ─── Delegates ────────────────────────────────────────────────────────────

	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnNPCAudioReady OnAudioReady;

	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnNPCAudioFailed OnAudioFailed;

	/** Fired when the current voice line finishes playing. Bind this to drive subtitle hide. */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnNPCSpeechCompleted OnSpeechCompleted;

	// ─── Config ───────────────────────────────────────────────────────────────

	/**
	 * ElevenLabs Voice ID for AXIOM.
	 * Loaded from Config/Secrets.ini at startup.
	 * Can be overridden here in Blueprint defaults for testing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FString VoiceId;

	/** ElevenLabs model. eleven_turbo_v2_5 is fast enough for real-time NPC dialogue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FString ModelId = TEXT("eleven_turbo_v2_5");

private:
	FString APIKey;

	// Kept as UPROPERTY to prevent GC while audio is playing.
	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> ActiveSoundWave;

	// Cached so StopCurrentSpeech can halt playback mid-line.
	UPROPERTY()
	TObjectPtr<UAudioComponent> ActiveAudioComponent;

	// Request counter — lets OnHTTPResponse discard stale responses.
	int32 CurrentRequestId = 0;

	// Timer that fires OnSpeechCompleted after the PCM audio duration elapses.
	// Cleared by StopCurrentSpeech so cancelled lines don't fire the delegate.
	FTimerHandle SpeechCompletedTimer;

	static const int32 PCMSampleRate  = 22050;
	static const int32 PCMNumChannels = 1;
	static const FString ElevenLabsBaseURL;

	void LoadSecretsConfig();
	void OnHTTPResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, int32 RequestId);
	UAudioComponent* PlayPCMData(const TArray<uint8>& PCMData);

	UFUNCTION()
	void OnSpeechTimerFinished();

public:
	// Strips all *...* expressions from text — stage directions and emphasis alike.
	// Call before ShowSubtitle and SpeakText to keep both in sync.
	static FString StripAsteriskExpressions(const FString& Input);

};
