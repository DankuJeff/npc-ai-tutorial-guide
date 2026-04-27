// Copyright 2026 Tyler Munstock. All Rights Reserved.
#include "AI/ClaudeNPCCharacter.h"
#include "AI/ClaudeNPCAIController.h"
#include "AI/NPCSubtitleWidget.h"
#include "AI/ClaudeNPCSubsystem.h"
#include "AI/ElevenLabsSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Components/AudioComponent.h"

AClaudeNPCCharacter::AClaudeNPCCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// NPC faces movement direction while running.
	// Overridden to false in Idle/Watching so we can rotate toward the player in Tick.
	GetCharacterMovement()->bOrientRotationToMovement = true;
	bUseControllerRotationYaw = false;

	AIControllerClass = AClaudeNPCAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}

void AClaudeNPCCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Cache the AI controller and bind move-complete callback.
	// PlacedInWorldOrSpawned guarantees possession happens before BeginPlay.
	NPCController = Cast<AClaudeNPCAIController>(GetController());
	if (NPCController.IsValid())
	{
		NPCController->OnNPCMoveCompleted.AddDynamic(this, &AClaudeNPCCharacter::OnAIControllerMoveCompleted);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("AClaudeNPCCharacter: AIController not found at BeginPlay. NavMesh movement will not work."));
	}

	// Bind subsystem delegates.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UClaudeNPCSubsystem* Claude = GI->GetSubsystem<UClaudeNPCSubsystem>())
		{
			Claude->OnResponseReceived.AddDynamic(this, &AClaudeNPCCharacter::OnClaudeResponseReceived);
		}

		if (UElevenLabsSubsystem* ElevenLabs = GI->GetSubsystem<UElevenLabsSubsystem>())
		{
			// OnSpeechCompleted is timer-driven from PCM duration — reliable for procedural waves.
			ElevenLabs->OnSpeechCompleted.AddDynamic(this, &AClaudeNPCCharacter::OnSpeechFinished);
			ElevenLabs->OnAudioFailed.AddDynamic(this, &AClaudeNPCCharacter::OnElevenLabsAudioFailed);
		}
	}

	// Create subtitle widget and add to viewport. Hidden until first dialogue.
	if (SubtitleWidgetClass)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			SubtitleWidget = CreateWidget<UNPCSubtitleWidget>(PC, SubtitleWidgetClass);
			if (SubtitleWidget)
			{
				SubtitleWidget->AddToViewport();
				SubtitleWidget->HideSubtitle();
			}
		}
	}

	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void AClaudeNPCCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentState == ENPCState::Idle || CurrentState == ENPCState::Watching)
	{
		TickFacePlayer(DeltaTime);
	}
}

// ─── State machine ────────────────────────────────────────────────────────────

void AClaudeNPCCharacter::SetState(ENPCState NewState)
{
	if (CurrentState == NewState) return;

	CurrentState = NewState;

	// Orient to movement while running; face player manually in Tick when stationary.
	const bool bMoving = (NewState == ENPCState::Demonstrating || NewState == ENPCState::Following);
	GetCharacterMovement()->bOrientRotationToMovement = bMoving;

	OnNPCStateChanged.Broadcast(NewState);

	// When the NPC arrives at the Watching position after its demo run, fire an
	// intro line to AXIOM so it speaks as the player approaches the obstacle.
	// Only fires if a challenge has been configured (ChallengeName is not empty).
	if (NewState == ENPCState::Watching && !CurrentChallengeContext.ChallengeName.IsEmpty())
	{
		SendContextMessage(CurrentChallengeContext);
	}
}

void AClaudeNPCCharacter::SetSpeaking(bool bNewSpeaking)
{
	if (bIsSpeaking == bNewSpeaking) return;
	bIsSpeaking = bNewSpeaking;
	OnSpeakingChanged.Broadcast(bIsSpeaking);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void AClaudeNPCCharacter::StartDemonstration(const TArray<FNPCWaypoint>& Waypoints)
{
	if (Waypoints.IsEmpty()) return;

	if (bIsSpeaking)
	{
		// Defer until speech finishes — NPC should not move while talking.
		// Overwrites any previously pending request; the latest call wins.
		bHasPendingDemonstration = true;
		bHasPendingMove          = false;
		PendingDemoWaypoints     = Waypoints;
		UE_LOG(LogTemp, Log, TEXT("AClaudeNPCCharacter: StartDemonstration deferred — NPC is speaking."));
		return;
	}

	// Clear any stale pending state before executing.
	bHasPendingDemonstration = false;
	bHasPendingMove          = false;

	DemoWaypoints         = Waypoints;
	ActiveWaypointIndex   = 0;
	bInDemoSequence       = true;
	bPendingJumpLanding   = false;

	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;
	SetState(ENPCState::Demonstrating);
	AdvanceToNextWaypoint();
}

void AClaudeNPCCharacter::NotifyPlayerAtCheckpoint()
{
	SetState(ENPCState::Idle);
}

void AClaudeNPCCharacter::MoveToPosition(FVector TargetLocation, bool bRunSpeed)
{
	if (!NPCController.IsValid()) return;

	if (bIsSpeaking)
	{
		// Defer until speech finishes — NPC should not move while talking.
		// Overwrites any previously pending request; the latest call wins.
		bHasPendingMove          = true;
		bHasPendingDemonstration = false;
		PendingMoveTarget        = TargetLocation;
		bPendingMoveRunSpeed     = bRunSpeed;
		UE_LOG(LogTemp, Log, TEXT("AClaudeNPCCharacter: MoveToPosition deferred — NPC is speaking."));
		return;
	}

	// Clear any stale pending state before executing.
	bHasPendingMove          = false;
	bHasPendingDemonstration = false;

	bInDemoSequence = false;
	GetCharacterMovement()->MaxWalkSpeed = bRunSpeed ? RunSpeed : WalkSpeed;
	SetState(ENPCState::Following);
	NPCController->MoveToLocation(TargetLocation, WaypointAcceptanceRadius);
}

void AClaudeNPCCharacter::SendContextMessage(const FNPCContext& Context)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UClaudeNPCSubsystem* Claude = GI->GetSubsystem<UClaudeNPCSubsystem>())
		{
			Claude->SendMessage(Context);
		}
	}
}

void AClaudeNPCCharacter::SpeakLine(const FString& Text)
{
	if (Text.IsEmpty()) return;

	// Cancel any line currently playing before starting this one.
	if (bIsSpeaking)
	{
		GetWorldTimerManager().ClearTimer(SubtitleFallbackTimer);
		SetSpeaking(false);
	}

	if (SubtitleWidget)
	{
		SubtitleWidget->HideSubtitle();
	}

	SetSpeaking(true);

	if (SubtitleWidget)
	{
		SubtitleWidget->ShowSubtitle(Text);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElevenLabsSubsystem* ElevenLabs = GI->GetSubsystem<UElevenLabsSubsystem>())
		{
			ElevenLabs->SpeakText(Text);
		}
	}
}

// ─── Demo waypoint sequence ───────────────────────────────────────────────────

void AClaudeNPCCharacter::AdvanceToNextWaypoint()
{
	if (!bInDemoSequence) return;

	if (ActiveWaypointIndex >= DemoWaypoints.Num())
	{
		// Reached the end of the demo path — enter Watching state.
		bInDemoSequence = false;
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
		SetState(ENPCState::Watching);
		return;
	}

	if (NPCController.IsValid())
	{
		NPCController->MoveToLocation(DemoWaypoints[ActiveWaypointIndex].Location, WaypointAcceptanceRadius);
	}
}

void AClaudeNPCCharacter::OnAIControllerMoveCompleted(bool bReachedTarget)
{
	if (bInDemoSequence)
	{
		// Guard against a stale callback arriving after the waypoint array was cleared.
		if (ActiveWaypointIndex >= DemoWaypoints.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("AClaudeNPCCharacter: OnAIControllerMoveCompleted — ActiveWaypointIndex %d out of range (%d). Ignoring."), ActiveWaypointIndex, DemoWaypoints.Num());
			return;
		}

		// Capture the waypoint we just arrived at before incrementing.
		const FNPCWaypoint CompletedWaypoint = DemoWaypoints[ActiveWaypointIndex];
		ActiveWaypointIndex++;

		if (CompletedWaypoint.bIsJumpLaunchPoint && ActiveWaypointIndex < DemoWaypoints.Num())
		{
			// Stop NavMesh steering so it doesn't fight the jump arc.
			if (NPCController.IsValid())
			{
				NPCController->StopMovement();
			}

			// Calculate and apply the launch velocity using projectile motion math.
			// The next waypoint is the intended landing target.
			const FVector LandingTarget = DemoWaypoints[ActiveWaypointIndex].Location;
			const float   GravityZ      = GetWorld()->GetGravityZ();
			const FVector LaunchVel     = CalcJumpLaunchVelocity(GetActorLocation(), LandingTarget, CompletedWaypoint.JumpArcTime, GravityZ);

			// bXYOverride + bZOverride = replace current velocity entirely.
			// Air control is minimal by default so the arc follows the calculation cleanly.
			LaunchCharacter(LaunchVel, /*bXYOverride=*/true, /*bZOverride=*/true);
			bPendingJumpLanding = true;
			// Landed() will advance the sequence once the NPC hits the ground.
		}
		else
		{
			AdvanceToNextWaypoint();
		}
	}
	else if (CurrentState == ENPCState::Following)
	{
		// Arrived at repositioning target — return to Idle.
		SetState(ENPCState::Idle);
	}
}

void AClaudeNPCCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (bPendingJumpLanding)
	{
		bPendingJumpLanding = false;

		// ActiveWaypointIndex currently points at the landing waypoint —
		// the NPC is already there, so skip past it and continue the sequence.
		ActiveWaypointIndex++;
		AdvanceToNextWaypoint();
	}
}

// ─── Subsystem callbacks ──────────────────────────────────────────────────────

void AClaudeNPCCharacter::OnClaudeResponseReceived(const FString& ResponseText, bool bSuccess)
{
	if (!bSuccess || ResponseText.IsEmpty()) return;

	// If a previous line is still playing (or its fallback timer is running),
	// cancel it cleanly before starting the new one. ElevenLabsSubsystem already
	// stopped the audio in SpeakText(); we just need to sync the NPC-side state.
	if (bIsSpeaking)
	{
		GetWorldTimerManager().ClearTimer(SubtitleFallbackTimer);
		SetSpeaking(false);
	}

	if (SubtitleWidget)
	{
		SubtitleWidget->HideSubtitle();
	}

	SetSpeaking(true);

	const FString CleanText = UElevenLabsSubsystem::StripAsteriskExpressions(ResponseText);

	if (SubtitleWidget)
	{
		SubtitleWidget->ShowSubtitle(CleanText);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UElevenLabsSubsystem* ElevenLabs = GI->GetSubsystem<UElevenLabsSubsystem>())
		{
			ElevenLabs->SpeakText(CleanText);
		}
	}
}

void AClaudeNPCCharacter::OnElevenLabsAudioFailed(const FString& ErrorMessage)
{
	// TTS failed — subtitle already visible from ShowSubtitle.
	// Clear it after a fixed read window so it doesn't stick on screen.
	GetWorldTimerManager().SetTimer(
		SubtitleFallbackTimer,
		this,
		&AClaudeNPCCharacter::OnSpeechFinished,
		SubtitleFallbackDuration,
		/*bLoop=*/false
	);
}

void AClaudeNPCCharacter::OnSpeechFinished()
{
	GetWorldTimerManager().ClearTimer(SubtitleFallbackTimer);
	SetSpeaking(false);
	if (SubtitleWidget)
	{
		SubtitleWidget->HideSubtitle();
	}

	// Execute any movement request that was deferred while the NPC was speaking.
	// Demonstration takes priority over a plain reposition if somehow both are pending.
	if (bHasPendingDemonstration)
	{
		bHasPendingDemonstration = false;
		TArray<FNPCWaypoint> WaypointsCopy = MoveTemp(PendingDemoWaypoints);
		PendingDemoWaypoints.Reset();
		StartDemonstration(WaypointsCopy);
	}
	else if (bHasPendingMove)
	{
		bHasPendingMove = false;
		MoveToPosition(PendingMoveTarget, bPendingMoveRunSpeed);
	}
}

// ─── Jump helpers ─────────────────────────────────────────────────────────────

FVector AClaudeNPCCharacter::CalcJumpLaunchVelocity(
	FVector Source,
	FVector Target,
	float   ArcTime,
	float   GravityZ)
{
	// Projectile motion per axis:
	//   Horizontal:  V0 = Δpos / t
	//   Vertical:    V0z = Δz/t − 0.5 * g * t   (g is negative in UE5)
	// This gives an arc that passes through Target after exactly ArcTime seconds.

	const FVector Delta = Target - Source;

	return FVector(
		Delta.X / ArcTime,
		Delta.Y / ArcTime,
		Delta.Z / ArcTime - 0.5f * GravityZ * ArcTime
	);
}

// ─── AnimBP helpers ───────────────────────────────────────────────────────────

float AClaudeNPCCharacter::GetMovementSpeed() const
{
	// XY plane speed only — Z velocity would make the blend space spike during jumps.
	const FVector Vel = GetVelocity();
	return FVector2D(Vel.X, Vel.Y).Size();
}

bool AClaudeNPCCharacter::GetIsInAir() const
{
	return GetCharacterMovement() && GetCharacterMovement()->IsFalling();
}

// ─── Player rotation ──────────────────────────────────────────────────────────

void AClaudeNPCCharacter::TickFacePlayer(float DeltaTime)
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player) return;

	FVector ToPlayer = Player->GetActorLocation() - GetActorLocation();
	ToPlayer.Z = 0.f;
	if (ToPlayer.IsNearlyZero()) return;

	FRotator TargetRotation = ToPlayer.Rotation();
	FRotator NewRotation = FMath::RInterpConstantTo(
		GetActorRotation(),
		TargetRotation,
		DeltaTime,
		FacePlayerRotationSpeed
	);
	SetActorRotation(NewRotation);
}
