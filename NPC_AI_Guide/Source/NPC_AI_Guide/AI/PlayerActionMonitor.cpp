// Copyright 2026 Tyler Munstock. All Rights Reserved.
#include "AI/PlayerActionMonitor.h"
#include "AI/ClaudeNPCSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UPlayerActionMonitor::UPlayerActionMonitor()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPlayerActionMonitor::BeginPlay()
{
	Super::BeginPlay();

	// Bind to the owner's movement mode delegate so we capture velocity at takeoff.
	// This fires whenever the character transitions between movement modes
	// (e.g., Walking → Falling on a jump, or Falling → Walking on landing).
	if (ACharacter* Owner = Cast<ACharacter>(GetOwner()))
	{
		Owner->MovementModeChangedDelegate.AddDynamic(
			this,
			&UPlayerActionMonitor::OnOwnerMovementModeChanged
		);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("UPlayerActionMonitor: Owner is not an ACharacter. Jump tracking disabled."));
	}

	// npc.SetAttempts <N>
	// Sets AttemptCount to N and immediately fires AXIOM with the resulting frustration
	// level and a randomly selected failure reason. Use during recording to skip ahead
	// to high-frustration coaching lines without dying N times.
	SetAttemptsCmd = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("npc.SetAttempts"),
		TEXT("npc.SetAttempts <N> — Sets AttemptCount to N and fires AXIOM coaching at the resulting frustration level."),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UPlayerActionMonitor::ExecSetAttempts),
		ECVF_Cheat
	);
}

void UPlayerActionMonitor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Owner = Cast<ACharacter>(GetOwner()))
	{
		Owner->MovementModeChangedDelegate.RemoveDynamic(
			this,
			&UPlayerActionMonitor::OnOwnerMovementModeChanged
		);
	}

	if (SetAttemptsCmd)
	{
		IConsoleManager::Get().UnregisterConsoleObject(SetAttemptsCmd);
		SetAttemptsCmd = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void UPlayerActionMonitor::NotifyJumpFailed()
{
	AttemptCount++;

	FNPCContext Context;
	Context.ChallengeName    = ChallengeName;
	Context.AttemptCount     = AttemptCount;
	Context.LastFailureReason = DetermineFailureReason();
	Context.FrustrationLevel  = ComputeFrustrationLevel();

	// Reset launch data so a stale velocity doesn't bleed into the next attempt.
	LaunchVelocity      = FVector::ZeroVector;
	bWasIntentionalJump = false;

	OnJumpChallengeResult.Broadcast(Context, /*bSucceeded=*/false);
	SendContextToSubsystem(Context);
}

void UPlayerActionMonitor::NotifyJumpSuccess()
{
	AttemptCount++;

	FNPCContext Context;
	Context.ChallengeName     = ChallengeName;
	Context.AttemptCount      = AttemptCount;
	Context.LastFailureReason = TEXT("");    // No failure — player cleared it
	Context.FrustrationLevel  = 0;          // Reset frustration on success

	LaunchVelocity      = FVector::ZeroVector;
	bWasIntentionalJump = false;

	OnJumpChallengeResult.Broadcast(Context, /*bSucceeded=*/true);
	SendContextToSubsystem(Context);
}

void UPlayerActionMonitor::ResetChallenge(const FString& NewChallengeName)
{
	ChallengeName       = NewChallengeName;
	AttemptCount        = 0;
	LaunchVelocity      = FVector::ZeroVector;
	bWasIntentionalJump = false;
}

// ─── Private ──────────────────────────────────────────────────────────────────

void UPlayerActionMonitor::OnOwnerMovementModeChanged(
	ACharacter*    Character,
	EMovementMode  PrevMovementMode,
	uint8          PreviousCustomMode)
{
	// We only care about the transition from grounded to airborne.
	if (PrevMovementMode != MOVE_Walking && PrevMovementMode != MOVE_NavWalking)
	{
		return;
	}

	if (!Character) return;
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement || Movement->IsMovingOnGround()) return;

	// Snapshot velocity at the moment the player leaves the ground.
	LaunchVelocity      = Character->GetVelocity();
	bWasIntentionalJump = (LaunchVelocity.Z > MinJumpZVelocity);
}

FString UPlayerActionMonitor::DetermineFailureReason() const
{
	if (!bWasIntentionalJump)
	{
		return TEXT("walked off the platform without jumping");
	}

	const float HorizontalSpeed = FVector2D(LaunchVelocity.X, LaunchVelocity.Y).Size();

	if (HorizontalSpeed < MinRunUpSpeed)
	{
		return TEXT("jumped from a near-standstill — not enough run-up speed");
	}

	// Player had speed and intent but still fell short — most common failure for
	// a double-jump challenge is timing the second jump too late or too early.
	return TEXT("jumped but fell short — second jump timing was off");
}

int32 UPlayerActionMonitor::ComputeFrustrationLevel() const
{
	// Linear scale: 1 failure = level 0, 11+ failures = level 10.
	// Keeps AXIOM patient early and escalates predictably.
	return FMath::Clamp(AttemptCount - 1, 0, 10);
}

void UPlayerActionMonitor::ExecSetAttempts(const TArray<FString>& Args)
{
	if (Args.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("npc.SetAttempts: Usage: npc.SetAttempts <N>"));
		return;
	}

	const int32 NewCount = FMath::Max(0, FCString::Atoi(*Args[0]));
	AttemptCount = NewCount;

	static const FString FailureReasons[] = {
		TEXT("walked off the platform without jumping"),
		TEXT("jumped from a near-standstill — not enough run-up speed"),
		TEXT("jumped but fell short — second jump timing was off")
	};
	const FString& ChosenReason = FailureReasons[FMath::RandRange(0, 2)];

	FNPCContext Context;
	Context.ChallengeName     = ChallengeName;
	Context.AttemptCount      = AttemptCount;
	Context.LastFailureReason = ChosenReason;
	Context.FrustrationLevel  = ComputeFrustrationLevel();

	UE_LOG(LogTemp, Log,
		TEXT("npc.SetAttempts: AttemptCount=%d FrustrationLevel=%d Reason='%s'"),
		AttemptCount, Context.FrustrationLevel, *ChosenReason);

	SendContextToSubsystem(Context);
}

void UPlayerActionMonitor::SendContextToSubsystem(const FNPCContext& Context) const
{
	const UWorld* World = GetWorld();
	if (!World) return;

	const UGameInstance* GI = UGameplayStatics::GetGameInstance(World);
	if (!GI) return;

	if (UClaudeNPCSubsystem* Claude = GI->GetSubsystem<UClaudeNPCSubsystem>())
	{
		Claude->SendMessage(Context);
	}
}
