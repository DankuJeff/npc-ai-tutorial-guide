// Copyright 2026 Tyler Munstock. All Rights Reserved.
#include "AI/ClaudeNPCAIController.h"
#include "Navigation/PathFollowingComponent.h"

void AClaudeNPCAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	OnNPCMoveCompleted.Broadcast(Result.IsSuccess());
}
