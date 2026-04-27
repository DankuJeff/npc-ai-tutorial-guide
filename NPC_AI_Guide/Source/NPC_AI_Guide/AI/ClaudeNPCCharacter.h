// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AI/NPCTypes.h"
#include "ClaudeNPCCharacter.generated.h"

class AClaudeNPCAIController;
class UNPCSubtitleWidget;
class UAudioComponent;

/**
 * AClaudeNPCCharacter
 *
 * The physical AXIOM guide NPC. Runs the course alongside the player:
 *   1. Demonstrates an obstacle by running through Blueprint-configured waypoints.
 *   2. Enters Watching state at the end, facing where the player will arrive.
 *   3. When the player clears the checkpoint, Blueprint drives it to the next obstacle.
 *
 * Claude API responses automatically trigger ElevenLabs TTS + typewriter subtitles.
 * Speaking never blocks movement — AXIOM can talk while running.
 *
 * Setup:
 *   - Subclass in Blueprint (BP_ClaudeNPC). Set SubtitleWidgetClass to your
 *     BP_NPCSubtitleWidget subclass. Drop into level.
 *   - Bind OnNPCStateChanged in AnimBP to drive locomotion (Idle/Run blend space).
 *   - Call StartDemonstration() from Blueprint with an array of world-space waypoints.
 *   - Wire a Box Trigger overlap → NotifyPlayerAtCheckpoint() to advance stages.
 *
 * State machine:
 *   Idle          — standing, facing player
 *   Demonstrating — running through obstacle waypoints
 *   Watching      — standing at obstacle end, facing player's approach
 *   Following     — running to a repositioning target (MoveToPosition)
 *   Speaking      — parallel flag (bIsSpeaking); never blocks the above states
 */
UCLASS()
class NPC_AI_GUIDE_API AClaudeNPCCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AClaudeNPCCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void Landed(const FHitResult& Hit) override;

	// ─── Blueprint API ────────────────────────────────────────────────────────

	/**
	 * Run the NPC through Waypoints to demonstrate the current obstacle.
	 * Transitions: current state → Demonstrating → Watching (on completion).
	 *
	 * Waypoints with bIsJumpLaunchPoint = true trigger a scripted projectile jump
	 * toward the following waypoint instead of NavMesh movement. The NPC resumes
	 * normal waypoint sequencing after landing.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void StartDemonstration(const TArray<FNPCWaypoint>& Waypoints);

	/**
	 * Call from a Blueprint overlap trigger when the player reaches the checkpoint.
	 * Transitions: Watching → Idle so Blueprint can drive the next stage.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void NotifyPlayerAtCheckpoint();

	/**
	 * Move the NPC to a world-space location via NavMesh (e.g., between obstacles).
	 * bRunSpeed: true = RunSpeed, false = WalkSpeed.
	 * Transitions: → Following → Idle (on arrival).
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void MoveToPosition(FVector TargetLocation, bool bRunSpeed);

	/**
	 * Send an FNPCContext directly to UClaudeNPCSubsystem.
	 * Use from Blueprint to manually trigger an AXIOM line at any time
	 * (e.g., a custom event, cutscene beat, or zone-specific hint).
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void SendContextMessage(const FNPCContext& Context);

	/**
	 * Speak a hardcoded line of dialogue directly via ElevenLabs — bypasses
	 * Claude entirely. Use for scripted intro/outro lines where the exact
	 * text is known and does not need AI generation.
	 * Drives subtitle display and bIsSpeaking state the same as a Claude response.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void SpeakLine(const FString& Text);

	// ─── Delegates ────────────────────────────────────────────────────────────

	/** Fired on every state transition. Wire to AnimBP to drive locomotion blend space. */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnNPCStateChanged OnNPCStateChanged;

	/** Fired when AXIOM starts or finishes a line of dialogue. */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnNPCSpeakingChanged OnSpeakingChanged;

	// ─── Config ───────────────────────────────────────────────────────────────

	/**
	 * Context for the current challenge zone. Set this in Blueprint when configuring
	 * a new obstacle (ChallengeName, AttemptCount = 0). When the NPC enters the
	 * Watching state at the end of its demo run, it automatically fires an intro
	 * call to AXIOM using this context.
	 *
	 * UPlayerActionMonitor manages per-attempt context independently — this is only
	 * used for the NPC's intro line at the start of each obstacle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FNPCContext CurrentChallengeContext;

	/**
	 * Widget class for screen-space subtitles.
	 * Set this to your BP_NPCSubtitleWidget subclass in Blueprint defaults.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI | Subtitles")
	TSubclassOf<UNPCSubtitleWidget> SubtitleWidgetClass;

	/** Movement speed when walking (Idle → Watching repositioning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI | Movement")
	float WalkSpeed = 400.f;

	/** Movement speed when running (Demonstrating, MoveToPosition). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI | Movement")
	float RunSpeed = 600.f;

	/** NavMesh acceptance radius for waypoint arrival. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI | Movement")
	float WaypointAcceptanceRadius = 50.f;

	/** Rotation speed (degrees/second) when facing the player in Idle/Watching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI | Movement")
	float FacePlayerRotationSpeed = 180.f;

	/**
	 * Subtitle display duration (seconds) when ElevenLabs TTS fails.
	 * Gives the player time to read the text before it clears.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI | Subtitles")
	float SubtitleFallbackDuration = 4.f;

	// ─── Read-only state ──────────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "NPC AI")
	ENPCState GetCurrentState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "NPC AI")
	bool GetIsSpeaking() const { return bIsSpeaking; }

	/** Current XY ground speed (cm/s). Use in AnimBP blend space to drive locomotion. */
	UFUNCTION(BlueprintPure, Category = "NPC AI")
	float GetMovementSpeed() const;

	/** True when the NPC is airborne. Use in AnimBP to trigger jump animation. */
	UFUNCTION(BlueprintPure, Category = "NPC AI")
	bool GetIsInAir() const;

private:
	// ── State machine ────────────────────────────────────────────────────────

	ENPCState CurrentState = ENPCState::Idle;
	bool bIsSpeaking = false;

	void SetState(ENPCState NewState);
	void SetSpeaking(bool bNewSpeaking);

	// ── Demo waypoint sequence ───────────────────────────────────────────────

	TArray<FNPCWaypoint> DemoWaypoints;
	int32 ActiveWaypointIndex = 0;
	bool bInDemoSequence = false;

	// True while the NPC is airborne after a scripted jump launch.
	// Prevents normal move-complete logic from firing; Landed() resumes the sequence.
	bool bPendingJumpLanding = false;

	// ── Pending movement (deferred while speaking) ────────────────────────────
	//
	// StartDemonstration and MoveToPosition queue here when called while bIsSpeaking.
	// OnSpeechFinished executes the stored request after SetSpeaking(false).
	//
	// Only one pending request is kept at a time — a newer call overwrites an older one.
	// bHasPendingDemonstration takes priority over bHasPendingMove if both are somehow set.

	bool              bHasPendingDemonstration = false;
	TArray<FNPCWaypoint> PendingDemoWaypoints;

	bool              bHasPendingMove      = false;
	FVector           PendingMoveTarget    = FVector::ZeroVector;
	bool              bPendingMoveRunSpeed = false;

	void AdvanceToNextWaypoint();

	/**
	 * Calculate the launch velocity needed to travel from Source to Target
	 * in ArcTime seconds under standard gravity.
	 * Uses projectile motion: V0 = ΔPos/t − 0.5 * g * t  (per axis).
	 */
	static FVector CalcJumpLaunchVelocity(FVector Source, FVector Target, float ArcTime, float GravityZ);

	UFUNCTION()
	void OnAIControllerMoveCompleted(bool bReachedTarget);

	// ── Subsystem callbacks ──────────────────────────────────────────────────

	UFUNCTION()
	void OnClaudeResponseReceived(const FString& ResponseText, bool bSuccess);

	UFUNCTION()
	void OnElevenLabsAudioFailed(const FString& ErrorMessage);

	UFUNCTION()
	void OnSpeechFinished();

	// ── Helpers ──────────────────────────────────────────────────────────────

	void TickFacePlayer(float DeltaTime);

	// ── Cached refs ──────────────────────────────────────────────────────────

	TWeakObjectPtr<AClaudeNPCAIController> NPCController;

	UPROPERTY()
	TObjectPtr<UNPCSubtitleWidget> SubtitleWidget;

	FTimerHandle SubtitleFallbackTimer;
};
