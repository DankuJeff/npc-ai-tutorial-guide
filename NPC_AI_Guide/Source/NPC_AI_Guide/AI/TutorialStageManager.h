// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraActor.h"
#include "AI/NPCTypes.h"
#include "TutorialStageManager.generated.h"

class AClaudeNPCCharacter;

/**
 * UTutorialStageManager
 *
 * UActorComponent that sequences the full tutorial course.
 * Drop it on a BP_TutorialManager actor in the level.
 *
 * Responsibilities:
 *   - Advances through FTutorialStage entries in order.
 *   - On each advance: resets Claude conversation history and PlayerActionMonitor
 *     attempt count so AXIOM starts fresh with each new obstacle.
 *   - Moves the NPC to the stage start position, then triggers the demo run.
 *   - Broadcasts OnStageAdvanced / OnTutorialComplete for any Blueprint reactions
 *     (e.g., UI zone name display, camera cut, particle burst on completion).
 *
 * Setup:
 *   1. Create BP_TutorialManager actor, add UTutorialStageManager as a component.
 *   2. Set NPCCharacter reference to BP_ClaudeNPC in the level.
 *   3. Configure the Stages array (one FTutorialStage per challenge zone).
 *   4. Call StartTutorial() from BeginPlay or a level trigger.
 *   5. Wire each BP_ProgressionTrigger → CompleteCurrentStage().
 */
UCLASS(ClassGroup=(NPC_AI), meta=(BlueprintSpawnableComponent))
class NPC_AI_GUIDE_API UTutorialStageManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UTutorialStageManager();

	// ─── Config ───────────────────────────────────────────────────────────────

	/**
	 * All challenge zones in sequence.
	 * Configure in Blueprint defaults — one FTutorialStage per obstacle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<FTutorialStage> Stages;

	/**
	 * Reference to the AXIOM NPC in the level.
	 * Set this in BP_TutorialManager's Details panel to your BP_ClaudeNPC instance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TObjectPtr<AClaudeNPCCharacter> NPCCharacter;

	/**
	 * How long to wait after CompleteCurrentStage before forcing the advance
	 * if no voice line fires (Claude or ElevenLabs failure fallback).
	 * Set generously — typical TTS round-trip is 2-4 seconds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (ClampMin = 2.0, ClampMax = 30.0))
	float SuccessVoiceFallbackSeconds = 10.0f;

	/**
	 * How long to wait for the level intro voice line before forcing StartTutorial.
	 * Must be longer than SuccessVoiceFallbackSeconds — the intro line is ~20 seconds
	 * of audio, plus 2-4 seconds ElevenLabs latency.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (ClampMin = 10.0, ClampMax = 60.0))
	float IntroVoiceFallbackSeconds = 35.0f;

	/**
	 * If false, the NPC runs the full tutorial solo — no player character required.
	 * Stages auto-advance when the NPC finishes speaking its intro line for each obstacle.
	 * The final stage plays a hardcoded outro line via ElevenLabs.
	 * Use this to record the NPC demo run or test stage layout without a player present.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	bool bPlayerRequired = true;

	/**
	 * One CineCameraActor per stage, in stage order.
	 * Only used when bPlayerRequired = false.
	 * On each stage activation the player controller blends to the matching camera.
	 * Place CineCameraActors in the level with a Look-At constraint targeting BP_ClaudeNPC,
	 * then assign them here in Blueprint defaults.
	 * If a stage index has no matching entry, the view target is left unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<TObjectPtr<ACameraActor>> SoloDemoCameras;

	/**
	 * Camera that sweeps in to face the NPC just before the solo demo outro line plays.
	 * Only used when bPlayerRequired = false.
	 * Place a CineCameraActor directly in front of BP_ClaudeNPC (facing it),
	 * slightly low to shoot upward for a heroic angle.
	 * The blend takes OutroCameraBlendSeconds — AXIOM speaks the moment the camera settles.
	 * If not assigned, the outro line plays from whichever camera is already active.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TObjectPtr<ACameraActor> OutroCamera = nullptr;

	/**
	 * Duration of the cinematic blend from the final stage camera to OutroCamera.
	 * 2.0 seconds gives a smooth orbital sweep without feeling like dead air.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial", meta = (ClampMin = 0.5, ClampMax = 5.0))
	float OutroCameraBlendSeconds = 2.0f;

	// ─── Delegates ────────────────────────────────────────────────────────────

	/**
	 * Fired when a new stage activates.
	 * Carries the full FTutorialStage for any Blueprint-side reactions
	 * (zone name display, environment swap, etc.).
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FOnStageAdvanced OnStageAdvanced;

	/**
	 * Fired after the player completes the final stage.
	 * Use to show a win screen, roll credits, or transition to a new level.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Tutorial")
	FOnTutorialComplete OnTutorialComplete;

	// ─── Blueprint API ────────────────────────────────────────────────────────

	/**
	 * Speak AXIOM's level intro line and then automatically call StartTutorial
	 * once it finishes. Call this from BP_TutorialManager BeginPlay instead of
	 * calling StartTutorial directly — gives the player context before obstacles begin.
	 * Falls back to calling StartTutorial immediately if TTS fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void PlayLevelIntro();

	/**
	 * Begin the tutorial from stage 0.
	 * Called automatically by PlayLevelIntro after the intro voice line ends.
	 * Can also be called directly if you want to skip the intro (debug, etc).
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void StartTutorial();

	/**
	 * Call this from BP_ProgressionTrigger when the player clears an obstacle.
	 *
	 * Fires NotifyJumpSuccess (which sends the success context to Claude for
	 * AXIOM's congratulatory line), then waits for that voice line to finish
	 * before calling AdvanceToNextStage internally.
	 *
	 * If Claude or ElevenLabs fails to produce audio, a fallback timer
	 * (SuccessVoiceFallbackSeconds) forces the advance so the game never stalls.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void CompleteCurrentStage();

	/**
	 * Advance directly to the next stage without waiting for any voice line.
	 * Use only if you need to skip AXIOM's success line (e.g. debug, cutscene).
	 * Normal gameplay should go through CompleteCurrentStage instead.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void AdvanceToNextStage();

	/**
	 * Sends a performance-aware prompt to Claude for AXIOM's closing statement,
	 * waits for it to finish speaking, then broadcasts OnTutorialComplete.
	 * Called automatically by AdvanceToNextStage when the last stage is cleared —
	 * do not call this directly from Blueprint.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void PlayLevelOutro();

	// ─── Checkpoint / Respawn ─────────────────────────────────────────────────

	/**
	 * Teleport the player back to the last saved checkpoint.
	 * Wire this in each BP_KillPlaneTrigger overlap alongside NotifyJumpFailed().
	 * Checkpoint is initialized to the player's spawn location at StartTutorial()
	 * and updated to each stage's PlayerCheckpointLocation on stage activation.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	void RespawnPlayerAtCheckpoint();

	/** Returns the current checkpoint world location. */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	FVector GetLastCheckpointLocation() const { return LastCheckpointLocation; }

	// ─── Read-only state ──────────────────────────────────────────────────────

	/** Zero-based index of the active stage. -1 = tutorial not started. */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	int32 GetCurrentStageIndex() const { return CurrentStageIndex; }

	/** Total number of configured stages. */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	int32 GetTotalStages() const { return Stages.Num(); }

	/** True after the final stage has been completed. */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	bool IsTutorialComplete() const { return bTutorialComplete; }

	/**
	 * Returns the active FTutorialStage by value.
	 * Returns a default-constructed stage if the tutorial hasn't started.
	 */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	FTutorialStage GetCurrentStage() const;

private:
	int32   CurrentStageIndex      = -1;
	bool    bTutorialComplete      = false;
	FVector LastCheckpointLocation = FVector::ZeroVector;

	// Set to true while the NPC is running to the stage start position.
	// Prevents StartDemonstration from firing until the NPC arrives.
	bool bWaitingForNPCToArrive = false;

	// Set to true while the level intro voice line is playing.
	// StartTutorial is deferred until it finishes.
	// bIntroVoiceLineStarted prevents a stale SetSpeaking(false) from another
	// source (e.g. OnClaudeResponseReceived cancelling a parallel line) from
	// prematurely triggering StartTutorial before the intro has even begun.
	bool bPlayingLevelIntro      = false;
	bool bIntroVoiceLineStarted  = false;
	FTimerHandle LevelIntroFallbackTimer;

	// Set to true while the level outro voice line is playing.
	// OnTutorialComplete is deferred until it finishes.
	// bOutroVoiceLineStarted mirrors the intro/success-advance pattern: prevents a
	// stale SetSpeaking(false) from the preceding success line from triggering
	// OnTutorialComplete before AXIOM's closing line even begins.
	bool bPlayingLevelOutro      = false;
	bool bOutroVoiceLineStarted  = false;
	FTimerHandle LevelOutroFallbackTimer;

	// Running total of player deaths/respawns across all stages.
	// Passed to Claude as performance context in the outro prompt.
	int32 TotalFailureCount      = 0;

	// ── Success-voice advance state ───────────────────────────────────────────
	//
	// CompleteCurrentStage sets bAdvancePendingAfterVoice and binds
	// OnSpeakingStatusChangedForAdvance to NPCCharacter->OnSpeakingChanged.
	//
	// Flow:
	//   1. Trigger fires → CompleteCurrentStage → NotifyJumpSuccess (async Claude call)
	//   2. Claude responds → SetSpeaking(true) fires → bVoiceLineStarted = true
	//   3. Audio finishes → SetSpeaking(false) fires → advance
	//   4. Fallback timer fires if step 2-3 never happen (API failure)
	//
	// Handles the "NPC was already speaking" case cleanly:
	//   - If a coaching line was playing when the trigger fired, that line ends
	//     and fires SetSpeaking(false). bVoiceLineStarted is still false, so
	//     we ignore it. The success line then starts/ends normally.

	bool bAdvancePendingAfterVoice = false;
	bool bVoiceLineStarted         = false;
	FTimerHandle SuccessVoiceFallbackTimer;

	// ── Solo demo mode state ──────────────────────────────────────────────────
	// Active when bPlayerRequired = false. Mirrors the success-voice-advance pattern:
	// bind on stage activation, wait for the intro line to start and finish, then advance.

	bool bSoloDemoLineStarted       = false;
	FTimerHandle SoloDemoAdvanceFallbackTimer;

	bool bPlayingSoloDemoOutro      = false;
	bool bSoloDemoOutroLineStarted  = false;
	FTimerHandle SoloDemoOutroFallbackTimer;
	FTimerHandle OutroCameraBlendTimer;

	// ── NPC face-rotation state ───────────────────────────────────────────────
	FTimerHandle FaceRotationTimer;
	FRotator FaceRotationTarget;
	TFunction<void()> OnFaceRotationComplete;

	/** Core activation logic — shared by StartTutorial and AdvanceToNextStage. */
	void ActivateStage(int32 StageIndex);

	/** Resets Claude conversation history and PlayerActionMonitor attempt count. */
	void ResetSubsystemsForNewStage(const FTutorialStage& Stage);

	/** Cleans up advance-pending state and unbinds the speaking observer. */
	void ClearAdvancePendingState();

	/**
	 * Bound to NPCCharacter->OnNPCStateChanged while bWaitingForNPCToArrive is true.
	 * Fires StartDemonstration when the NPC transitions to Idle.
	 */
	UFUNCTION()
	void OnNPCArrivedAtStageStart(ENPCState NewState);

	/** Bound to NPCCharacter->OnSpeakingChanged during the level intro line. */
	UFUNCTION()
	void OnLevelIntroFinished(bool bIsSpeaking);

	/** Fallback if intro TTS fails — starts the tutorial anyway. */
	UFUNCTION()
	void OnLevelIntroFallback();

	/** Bound to NPCCharacter->OnSpeakingChanged during the level outro line. */
	UFUNCTION()
	void OnLevelOutroFinished(bool bIsSpeaking);

	/** Fallback if outro TTS fails — broadcasts OnTutorialComplete anyway. */
	UFUNCTION()
	void OnLevelOutroFallback();

	/**
	 * Bound to NPCCharacter->OnSpeakingChanged by CompleteCurrentStage.
	 * Watches for the success voice line to start then finish.
	 *   bIsSpeaking = true  → marks the line as started
	 *   bIsSpeaking = false AND started → advances to next stage
	 *   bIsSpeaking = false AND not started yet → ignores (old line ending)
	 */
	UFUNCTION()
	void OnSpeakingStatusChangedForAdvance(bool bIsSpeaking);

	/**
	 * Fallback timer callback. If the success voice line never fires
	 * (Claude or ElevenLabs failure), this forces the advance so the game
	 * doesn't stall.
	 */
	UFUNCTION()
	void OnSuccessVoiceFallback();

	// ── Solo demo mode callbacks ──────────────────────────────────────────────

	/** Binds OnSoloDemoLineFinished and sets the initial fallback timer. Called from ActivateStage when !bPlayerRequired. */
	void BindSoloDemoAdvance();

	/** Bound to NPCCharacter->OnSpeakingChanged when !bPlayerRequired. Auto-advances when the stage intro line finishes. */
	UFUNCTION()
	void OnSoloDemoLineFinished(bool bIsSpeaking);

	/** Fallback if AXIOM never speaks during a solo demo stage — advances anyway. */
	UFUNCTION()
	void OnSoloDemoAdvanceFallback();

	/** Fires SoloDemoOutroContext on the final stage when !bPlayerRequired. */
	void PlaySoloDemoOutro();

	/** Called after the OutroCamera blend completes — starts NPC rotation toward OutroCamera, then speaks. */
	UFUNCTION()
	void StartSoloDemoOutroLine();

	/** Fires the actual SpeakLine for the outro — called by BeginFaceRotation's OnComplete callback. */
	void FireSoloDemoOutroLine();

	/**
	 * Smooth yaw rotation toward TargetCamera over ~0.5s using a 60fps timer.
	 * Disables bOrientRotationToMovement for the duration, restores on completion.
	 * OnComplete fires when the NPC is fully facing the camera — pass nullptr for fire-and-forget.
	 */
	void BeginFaceRotation(ACameraActor* TargetCamera, TFunction<void()> OnComplete);

	/** Per-frame lerp tick for BeginFaceRotation. */
	UFUNCTION()
	void TickFaceRotation();

	/** Rotates the NPC to face the active stage camera (fire-and-forget, concurrent with speaking). */
	void FaceActiveSoloCamera();

	/** Bound to NPCCharacter->OnSpeakingChanged during the solo demo outro. */
	UFUNCTION()
	void OnSoloDemoOutroFinished(bool bIsSpeaking);

	/** Fallback if solo demo outro TTS fails — broadcasts OnTutorialComplete anyway. */
	UFUNCTION()
	void OnSoloDemoOutroFallback();
};
