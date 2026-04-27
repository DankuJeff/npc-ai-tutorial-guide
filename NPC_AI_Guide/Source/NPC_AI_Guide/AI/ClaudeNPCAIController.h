// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ClaudeNPCAIController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCMoveCompleted, bool, bReachedTarget);

/**
 * AClaudeNPCAIController
 *
 * Minimal AAIController for AClaudeNPCCharacter.
 * Translates UE5's path-following result into a Blueprint-friendly delegate
 * so the character can sequence demo waypoints without subclassing the controller.
 */
UCLASS()
class NPC_AI_GUIDE_API AClaudeNPCAIController : public AAIController
{
	GENERATED_BODY()

public:
	/** Fired when the current MoveToLocation request finishes (success or failure). */
	UPROPERTY(BlueprintAssignable, Category = "NPC AI")
	FOnNPCMoveCompleted OnNPCMoveCompleted;

protected:
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
};
