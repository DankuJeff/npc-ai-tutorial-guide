// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "AI/NPCTypes.h"
#include "PlayerActionMonitor.generated.h"

/** Fired when a jump attempt resolves (success or failure).
 *  Bind this in Blueprint to react visually (e.g., checkpoint flash, respawn camera). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnJumpChallengeResult, const FNPCContext&, Context, bool, bSucceeded);

/**
 * UPlayerActionMonitor
 *
 * UActorComponent placed on the player character. Monitors jump attempts within a
 * challenge zone and sends coaching context to UClaudeNPCSubsystem when an attempt
 * resolves (success or failure).
 *
 * Setup:
 *   1. Add to your BP_ThirdPersonCharacter as a component.
 *   2. Set ChallengeName on component (or call ResetChallenge() from the zone trigger).
 *   3. Wire a kill volume below the gap → NotifyJumpFailed().
 *   4. Wire the landing pad trigger → NotifyJumpSuccess().
 *
 * Jump classification uses velocity recorded at the moment the player leaves the ground.
 * Frustration level scales linearly with AttemptCount — drives AXIOM's escalation.
 */
UCLASS(ClassGroup=(NPC_AI), meta=(BlueprintSpawnableComponent))
class NPC_AI_GUIDE_API UPlayerActionMonitor : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerActionMonitor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ─── Blueprint API ────────────────────────────────────────────────────────

	/**
	 * Call from a kill volume / respawn trigger below the gap.
	 * Records the failed attempt, classifies the failure from launch data,
	 * and sends FNPCContext to UClaudeNPCSubsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void NotifyJumpFailed();

	/**
	 * Call from the landing pad trigger when the player clears the challenge.
	 * Sends a success context to AXIOM and resets the attempt state.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void NotifyJumpSuccess();

	/**
	 * Reset attempt count and set a new challenge name.
	 * Call this when the player enters a new challenge zone.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void ResetChallenge(const FString& NewChallengeName);

	// ─── Delegates ────────────────────────────────────────────────────────────

	/** Broadcast when any attempt resolves (success or failure). Blueprint can
	 *  use this to trigger visual feedback independent of Claude's response. */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnJumpChallengeResult OnJumpChallengeResult;

	// ─── Config ───────────────────────────────────────────────────────────────

	/** Name of the current challenge, injected into every Claude API call.
	 *  Set in Blueprint defaults or call ResetChallenge() from a zone trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FString ChallengeName = TEXT("Double Jump Challenge");

	/** Horizontal speed (cm/s) below which a jump is classified as "no run-up."
	 *  Tune to match the character's run speed for this challenge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI", meta = (ClampMin = 0, ClampMax = 2000))
	float MinRunUpSpeed = 250.f;

	/** Minimum Z velocity at launch to count as an intentional jump (vs. walking off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI", meta = (ClampMin = 0, ClampMax = 2000))
	float MinJumpZVelocity = 100.f;

	// ─── Read-only state ──────────────────────────────────────────────────────

	UFUNCTION(BlueprintPure, Category = "NPC AI")
	int32 GetAttemptCount() const { return AttemptCount; }

	UFUNCTION(BlueprintPure, Category = "NPC AI")
	int32 GetFrustrationLevel() const { return ComputeFrustrationLevel(); }

private:
	// ── Launch data captured on takeoff ─────────────────────────────────────

	FVector LaunchVelocity      = FVector::ZeroVector;
	bool    bWasIntentionalJump = false;

	// ── Attempt tracking ─────────────────────────────────────────────────────

	int32 AttemptCount = 0;

	// ── Console commands ──────────────────────────────────────────────────────

	// npc.SetAttempts <N> — injects AttemptCount and fires AXIOM immediately.
	// Registered in BeginPlay, unregistered in EndPlay.
	IConsoleCommand* SetAttemptsCmd = nullptr;

	void ExecSetAttempts(const TArray<FString>& Args);

	// ── Internal helpers ─────────────────────────────────────────────────────

	/** Bound to owner's MovementModeChangedDelegate — records velocity at takeoff. */
	UFUNCTION()
	void OnOwnerMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode);

	FString DetermineFailureReason() const;

	/** Maps AttemptCount → 0-10 frustration level for AXIOM's escalation. */
	int32 ComputeFrustrationLevel() const;

	void SendContextToSubsystem(const FNPCContext& Context) const;
};
