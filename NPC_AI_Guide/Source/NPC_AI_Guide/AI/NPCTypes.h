// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "NPCTypes.generated.h"

// Single entry in the NPC's sliding window conversation history.
USTRUCT(BlueprintType)
struct FNPCMessage
{
	GENERATED_BODY()

	// "user" or "assistant"
	UPROPERTY(BlueprintReadWrite, Category = "NPC AI")
	FString Role;

	UPROPERTY(BlueprintReadWrite, Category = "NPC AI")
	FString Content;
};

// Snapshot of game state passed with every Claude API call.
// UPlayerActionMonitor assembles this; Blueprint can also populate it manually for testing.
USTRUCT(BlueprintType)
struct FNPCContext
{
	GENERATED_BODY()

	// Name of the current challenge zone, e.g. "Double Jump Challenge"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FString ChallengeName;

	// How many times the player has attempted this challenge (drives frustration escalation)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	int32 AttemptCount = 0;

	// Short description of why the last attempt failed, e.g. "jumped too early"
	// Leave empty if the player succeeded or hasn't tried yet.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FString LastFailureReason;

	// 0-10. Injected into the system prompt to drive persona escalation.
	// 0-2: patient/encouraging, 3-5: sarcastic, 6-8: frustrated, 9-10: full meltdown.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	int32 FrustrationLevel = 0;

	// Optional direct message from the player (future: voice input or UI prompt).
	// If empty, AXIOM reacts to the game context alone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FString PlayerMessage;
};

/**
 * A single waypoint in a demo path.
 * Normal waypoints use NavMesh (MoveToLocation).
 * Jump waypoints stop NavMesh movement and launch the NPC toward the NEXT waypoint
 * using a calculated projectile arc, then resume from the landing point.
 *
 * Place the jump waypoint at the edge of the gap (the take-off spot).
 * The waypoint immediately after it is the landing target.
 */
USTRUCT(BlueprintType)
struct FNPCWaypoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	FVector Location = FVector::ZeroVector;

	/**
	 * If true, the NPC launches toward the next waypoint when it arrives here
	 * instead of walking to it. JumpArcTime controls the parabola height.
	 * Leave false for all regular ground waypoints.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	bool bIsJumpLaunchPoint = false;

	/**
	 * Duration of the jump arc in seconds. Only used when bIsJumpLaunchPoint = true.
	 * Higher values = taller, slower arc. Lower = flatter, faster.
	 * 0.7-1.0 works well for most platform gaps at standard UE5 gravity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI", meta = (ClampMin = 0.1, ClampMax = 3.0))
	float JumpArcTime = 0.8f;
};

/**
 * Defines a single challenge zone in the tutorial sequence.
 * Configure an array of these on BP_TutorialManager to describe the full course.
 *
 * Flow per stage:
 *   1. UTutorialStageManager activates the stage.
 *   2. NPC runs to NPCStartPosition (skipped if zero).
 *   3. NPC runs DemoWaypoints → enters Watching state.
 *   4. C++ auto-fires IntroContext to AXIOM when Watching is entered.
 *   5. Player attempts the challenge; UPlayerActionMonitor tracks and coaches.
 *   6. Player succeeds → BP_ProgressionTrigger calls AdvanceToNextStage().
 */
USTRUCT(BlueprintType)
struct FTutorialStage
{
	GENERATED_BODY()

	/**
	 * Unique name for this challenge zone.
	 * Must match the ChallengeName set on UPlayerActionMonitor for this zone.
	 * Used in AXIOM's system prompt to refer to the specific challenge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FString StageName;

	/**
	 * World position the NPC moves to before running the demo path.
	 * Set to (0,0,0) to skip repositioning (NPC stays where it is).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FVector NPCStartPosition = FVector::ZeroVector;

	/** Waypoints for the NPC's demo run. Include jump waypoints as needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	TArray<FNPCWaypoint> DemoWaypoints;

	/**
	 * World position the player is teleported to when they fail this obstacle.
	 * Set this to the safe start of the obstacle (just before the jump approach).
	 * Leave at (0,0,0) to keep the previous stage's checkpoint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FVector PlayerCheckpointLocation = FVector::ZeroVector;

	/**
	 * Context sent to AXIOM when the NPC enters Watching state (the intro line).
	 * Set ChallengeName = StageName, AttemptCount = 0, leave others blank.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tutorial")
	FNPCContext IntroContext;

};

/** Fired when UTutorialStageManager activates a new stage. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStageAdvanced, FTutorialStage, NewStage);

/** Fired when the last stage completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTutorialComplete);

// NPC behaviour states — used by AClaudeNPCCharacter state machine (Phase 2).
UENUM(BlueprintType)
enum class ENPCState : uint8
{
	Idle          UMETA(DisplayName = "Idle"),
	Following     UMETA(DisplayName = "Following"),
	Watching      UMETA(DisplayName = "Watching"),
	Speaking      UMETA(DisplayName = "Speaking"),
	Demonstrating UMETA(DisplayName = "Demonstrating")
};

/** Fired when AClaudeNPCCharacter transitions to a new state. Bind in AnimBP. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCStateChanged, ENPCState, NewState);

/** Fired when AXIOM's speaking state changes. Drives subtitle visibility. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCSpeakingChanged, bool, bIsSpeaking);
