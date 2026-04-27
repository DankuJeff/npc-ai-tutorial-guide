// Copyright 2026 Tyler Munstock. All Rights Reserved.
#include "AI/TutorialStageManager.h"
#include "AI/ClaudeNPCCharacter.h"
#include "AI/ClaudeNPCSubsystem.h"
#include "AI/PlayerActionMonitor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UTutorialStageManager::UTutorialStageManager()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// ─── Public API ───────────────────────────────────────────────────────────────

void UTutorialStageManager::PlayLevelIntro()
{
	if (!NPCCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: PlayLevelIntro — NPCCharacter not set. Starting tutorial directly."));
		StartTutorial();
		return;
	}

	static const FString IntroLine =
		TEXT("Welcome. I am AXIOM — your AI Guide. Ancient. Patient. Deeply overqualified for this. "
		     "Somewhere ahead are obstacles that would humble lesser minds. "
		     "You will fail them, I will advise you, and eventually, you will succeed. "
		     "That is the arrangement. Let us begin.");

	// In solo demo mode, activate the first stage camera immediately so the viewer
	// has a cinematic view from the first frame, before AXIOM speaks.
	if (!bPlayerRequired && SoloDemoCameras.IsValidIndex(0) && SoloDemoCameras[0])
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			PC->SetViewTargetWithBlend(SoloDemoCameras[0], 0.8f, EViewTargetBlendFunction::VTBlend_Cubic);
		}
	}

	bPlayingLevelIntro     = true;
	bIntroVoiceLineStarted = false;
	// RemoveDynamic first — no-op if not bound, prevents double-fire on repeated calls.
	NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnLevelIntroFinished);
	NPCCharacter->OnSpeakingChanged.AddDynamic(this, &UTutorialStageManager::OnLevelIntroFinished);
	NPCCharacter->SpeakLine(IntroLine);

	// Fallback — use IntroVoiceFallbackSeconds, NOT SuccessVoiceFallbackSeconds.
	// The intro line is ~20 seconds of audio plus ElevenLabs latency.
	// SuccessVoiceFallbackSeconds (10s) would fire mid-speech.
	GetWorld()->GetTimerManager().SetTimer(
		LevelIntroFallbackTimer,
		this,
		&UTutorialStageManager::OnLevelIntroFallback,
		IntroVoiceFallbackSeconds,
		/*bLoop=*/false
	);

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Playing level intro. StartTutorial deferred until intro finishes."));
}

void UTutorialStageManager::StartTutorial()
{
	if (Stages.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: StartTutorial called but Stages array is empty."));
		return;
	}

	// Seed the checkpoint with the player's actual spawn location.
	// Skipped in solo demo mode — no player pawn exists.
	if (bPlayerRequired)
	{
		if (const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			LastCheckpointLocation = Player->GetActorLocation();
		}
	}

	ActivateStage(0);
}

void UTutorialStageManager::CompleteCurrentStage()
{
	if (bTutorialComplete) return;

	// Bail if an advance is already pending — prevent double-fire if somehow
	// two triggers overlap at the same moment.
	if (bAdvancePendingAfterVoice) return;

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: CompleteCurrentStage — stage %d. Waiting for success voice line."), CurrentStageIndex);

	// Fire NotifyJumpSuccess. This sends the success context to Claude (async).
	// Claude will respond → OnClaudeResponseReceived → ElevenLabs → audio plays.
	if (APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
	{
		if (UPlayerActionMonitor* Monitor = Player->FindComponentByClass<UPlayerActionMonitor>())
		{
			Monitor->NotifyJumpSuccess();
		}
	}

	// Start watching NPCCharacter->OnSpeakingChanged to detect when the success
	// voice line plays and finishes. The logic in OnSpeakingStatusChangedForAdvance
	// waits for the line to START (SetSpeaking true) and then END (SetSpeaking false)
	// before advancing — so any currently-playing coaching line ending early doesn't
	// trigger an advance.
	bAdvancePendingAfterVoice = true;
	bVoiceLineStarted         = false;

	if (NPCCharacter)
	{
		// RemoveDynamic first — no-op if not bound, prevents double-fire.
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSpeakingStatusChangedForAdvance);
		NPCCharacter->OnSpeakingChanged.AddDynamic(this, &UTutorialStageManager::OnSpeakingStatusChangedForAdvance);
	}

	// Fallback: if Claude or ElevenLabs fails and no voice line ever fires,
	// advance after SuccessVoiceFallbackSeconds so the game never stalls.
	GetWorld()->GetTimerManager().SetTimer(
		SuccessVoiceFallbackTimer,
		this,
		&UTutorialStageManager::OnSuccessVoiceFallback,
		SuccessVoiceFallbackSeconds,
		/*bLoop=*/false
	);
}

void UTutorialStageManager::AdvanceToNextStage()
{
	const int32 NextIndex = CurrentStageIndex + 1;

	if (NextIndex >= Stages.Num())
	{
		bTutorialComplete = true;
		if (!bPlayerRequired)
		{
			PlaySoloDemoOutro();
		}
		else
		{
			PlayLevelOutro();
		}
		return;
	}

	ActivateStage(NextIndex);
}

void UTutorialStageManager::RespawnPlayerAtCheckpoint()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: RespawnPlayerAtCheckpoint — no player pawn found."));
		return;
	}

	// TeleportPhysics resets velocity so the player doesn't carry momentum from the fall.
	Player->SetActorLocation(LastCheckpointLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	// Track cumulative failures for the level outro performance summary.
	TotalFailureCount++;
}

FTutorialStage UTutorialStageManager::GetCurrentStage() const
{
	if (CurrentStageIndex >= 0 && CurrentStageIndex < Stages.Num())
	{
		return Stages[CurrentStageIndex];
	}
	return FTutorialStage{};
}

// ─── Private ──────────────────────────────────────────────────────────────────

void UTutorialStageManager::ActivateStage(int32 StageIndex)
{
	CurrentStageIndex = StageIndex;
	const FTutorialStage& Stage = Stages[StageIndex];

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Activating stage %d — '%s'"), StageIndex, *Stage.StageName);

	// Reset Claude history + player attempt count for this zone.
	ResetSubsystemsForNewStage(Stage);

	// Update the respawn checkpoint. If the stage doesn't define one, previous holds.
	if (!Stage.PlayerCheckpointLocation.IsNearlyZero())
	{
		LastCheckpointLocation = Stage.PlayerCheckpointLocation;
	}

	// Set the NPC's intro context so AXIOM speaks when it enters Watching state.
	if (NPCCharacter)
	{
		NPCCharacter->CurrentChallengeContext = Stage.IntroContext;

		const bool bNeedsRepositioning = !Stage.NPCStartPosition.IsNearlyZero();

		if (bNeedsRepositioning)
		{
			bWaitingForNPCToArrive = true;
			// RemoveDynamic first — no-op if not bound, prevents double-fire if
			// ActivateStage is called before the previous NPC arrival fires.
			NPCCharacter->OnNPCStateChanged.RemoveDynamic(this, &UTutorialStageManager::OnNPCArrivedAtStageStart);
			NPCCharacter->OnNPCStateChanged.AddDynamic(this, &UTutorialStageManager::OnNPCArrivedAtStageStart);
			NPCCharacter->MoveToPosition(Stage.NPCStartPosition, /*bRunSpeed=*/true);
		}
		else
		{
			NPCCharacter->StartDemonstration(Stage.DemoWaypoints);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: NPCCharacter is not set — "
			"assign BP_ClaudeNPC in BP_TutorialManager BeginPlay."));
	}

	OnStageAdvanced.Broadcast(Stage);

	if (!bPlayerRequired)
	{
		// Switch to the per-stage cinematic camera if one is assigned.
		if (SoloDemoCameras.IsValidIndex(StageIndex) && SoloDemoCameras[StageIndex])
		{
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
			{
				PC->SetViewTargetWithBlend(
					SoloDemoCameras[StageIndex],
					0.8f,
					EViewTargetBlendFunction::VTBlend_Cubic
				);
			}
		}

		BindSoloDemoAdvance();
	}
}

void UTutorialStageManager::ResetSubsystemsForNewStage(const FTutorialStage& Stage)
{
	const UWorld* World = GetWorld();
	if (!World) return;

	if (const UGameInstance* GI = UGameplayStatics::GetGameInstance(World))
	{
		if (UClaudeNPCSubsystem* Claude = GI->GetSubsystem<UClaudeNPCSubsystem>())
		{
			Claude->ResetHistory();
		}
	}

	if (bPlayerRequired)
	{
		if (APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			if (UPlayerActionMonitor* Monitor = Player->FindComponentByClass<UPlayerActionMonitor>())
			{
				Monitor->ResetChallenge(Stage.StageName);
			}
		}
	}
}

void UTutorialStageManager::ClearAdvancePendingState()
{
	bAdvancePendingAfterVoice = false;
	bVoiceLineStarted         = false;

	GetWorld()->GetTimerManager().ClearTimer(SuccessVoiceFallbackTimer);

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSpeakingStatusChangedForAdvance);
	}
}

void UTutorialStageManager::OnNPCArrivedAtStageStart(ENPCState NewState)
{
	if (NewState != ENPCState::Idle || !bWaitingForNPCToArrive)
	{
		return;
	}

	bWaitingForNPCToArrive = false;

	if (NPCCharacter)
	{
		NPCCharacter->OnNPCStateChanged.RemoveDynamic(this, &UTutorialStageManager::OnNPCArrivedAtStageStart);
	}

	if (CurrentStageIndex >= 0 && CurrentStageIndex < Stages.Num() && NPCCharacter)
	{
		NPCCharacter->StartDemonstration(Stages[CurrentStageIndex].DemoWaypoints);
	}
}

void UTutorialStageManager::OnSpeakingStatusChangedForAdvance(bool bIsSpeaking)
{
	if (!bAdvancePendingAfterVoice) return;

	if (bIsSpeaking)
	{
		// Success voice line just started. Mark it so we know to advance when it ends.
		bVoiceLineStarted = true;

		// Reset the fallback timer from NOW — the original timer was set in
		// CompleteCurrentStage before the voice line even began, so it included
		// Claude + ElevenLabs API latency (~3-6s). If the line takes longer than
		// (SuccessVoiceFallbackSeconds − latency), the fallback fires mid-audio.
		// Resetting here gives the full SuccessVoiceFallbackSeconds from when
		// audio actually starts, which is what the fallback is intended to cover.
		GetWorld()->GetTimerManager().SetTimer(
			SuccessVoiceFallbackTimer,
			this,
			&UTutorialStageManager::OnSuccessVoiceFallback,
			SuccessVoiceFallbackSeconds,
			/*bLoop=*/false
		);

		UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Success voice line started — fallback timer reset. Waiting for line to finish."));
		return;
	}

	// bIsSpeaking == false
	if (!bVoiceLineStarted)
	{
		// A previous line (coaching/intro) ended before the success line started.
		// Do not advance — wait for the success line to start and then finish.
		UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Speaking stopped but success line hasn't started yet — still waiting."));
		return;
	}

	// Success line started AND just finished — advance.
	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Success voice line finished — advancing to next stage."));
	ClearAdvancePendingState();
	AdvanceToNextStage();
}

void UTutorialStageManager::OnLevelIntroFinished(bool bIsSpeaking)
{
	if (!bPlayingLevelIntro) return;

	if (bIsSpeaking)
	{
		// Intro line just started playing — mark it so we know to act when it ends.
		bIntroVoiceLineStarted = true;
		return;
	}

	// bIsSpeaking == false
	if (!bIntroVoiceLineStarted)
	{
		// A SetSpeaking(false) fired before the intro even started — a previous line
		// being cancelled, or a stale Claude response cancelling the intro line.
		// Do NOT start the tutorial yet — wait for the intro to start and then finish.
		UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Speaking stopped but intro hasn't started yet — still waiting."));
		return;
	}

	// Intro started AND just finished — proceed.
	bPlayingLevelIntro     = false;
	bIntroVoiceLineStarted = false;
	GetWorld()->GetTimerManager().ClearTimer(LevelIntroFallbackTimer);

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnLevelIntroFinished);
	}

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Intro finished — starting tutorial."));
	StartTutorial();
}

void UTutorialStageManager::OnLevelIntroFallback()
{
	if (!bPlayingLevelIntro) return;

	UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: Intro voice fallback fired after %.1f seconds — starting tutorial anyway."), IntroVoiceFallbackSeconds);

	bPlayingLevelIntro     = false;
	bIntroVoiceLineStarted = false;

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnLevelIntroFinished);
	}

	StartTutorial();
}

void UTutorialStageManager::PlayLevelOutro()
{
	if (!NPCCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: PlayLevelOutro — NPCCharacter not set. Broadcasting OnTutorialComplete directly."));
		OnTutorialComplete.Broadcast();
		return;
	}

	// Build a performance-aware user turn. Claude's response will be shaped by
	// FrustrationLevel=0 (calm, sage mode) and the specific stats in PlayerMessage.
	const FString PerformanceSummary = TotalFailureCount == 0
		? TEXT("zero failures — they cleared every obstacle without falling once")
		: TotalFailureCount <= 3
		? FString::Printf(TEXT("only %d total failure(s) — a strong performance"), TotalFailureCount)
		: TotalFailureCount <= 8
		? FString::Printf(TEXT("%d total failures — they struggled at times but pushed through"), TotalFailureCount)
		: FString::Printf(TEXT("%d total failures — a hard-fought, grueling run that tested their persistence"), TotalFailureCount);

	FNPCContext OutroContext;
	OutroContext.ChallengeName    = TEXT("Tutorial Complete");
	OutroContext.AttemptCount     = TotalFailureCount;
	OutroContext.FrustrationLevel = 0;
	OutroContext.PlayerMessage    = FString::Printf(
		TEXT("The player has just completed the final obstacle and finished the entire tutorial. "
		     "Their overall performance: %s. "
		     "Deliver AXIOM's closing statement — congratulatory but true to your voice. "
		     "Acknowledge their effort, offer one final piece of wisdom or dry observation, "
		     "and give them a proper send-off. 2-3 sentences."),
		*PerformanceSummary
	);

	bPlayingLevelOutro      = true;
	bOutroVoiceLineStarted  = false;
	// RemoveDynamic first — no-op if not bound, prevents double-fire.
	NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnLevelOutroFinished);
	NPCCharacter->OnSpeakingChanged.AddDynamic(this, &UTutorialStageManager::OnLevelOutroFinished);
	NPCCharacter->SendContextMessage(OutroContext);

	// Fallback — if Claude or ElevenLabs fails, broadcast completion so the game doesn't stall.
	GetWorld()->GetTimerManager().SetTimer(
		LevelOutroFallbackTimer,
		this,
		&UTutorialStageManager::OnLevelOutroFallback,
		SuccessVoiceFallbackSeconds,
		/*bLoop=*/false
	);

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Playing level outro. TotalFailures=%d. OnTutorialComplete deferred."), TotalFailureCount);
}

void UTutorialStageManager::OnLevelOutroFinished(bool bIsSpeaking)
{
	if (!bPlayingLevelOutro) return;

	if (bIsSpeaking)
	{
		// Outro line just started — mark it so we know to act when it ends.
		bOutroVoiceLineStarted = true;
		return;
	}

	// bIsSpeaking == false
	if (!bOutroVoiceLineStarted)
	{
		// A SetSpeaking(false) fired before the outro even started — likely the
		// previous success voice line's tail end or a stale callback. Ignore it.
		UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Speaking stopped but outro hasn't started yet — still waiting."));
		return;
	}

	// Outro started AND just finished — complete.
	bPlayingLevelOutro     = false;
	bOutroVoiceLineStarted = false;
	GetWorld()->GetTimerManager().ClearTimer(LevelOutroFallbackTimer);

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnLevelOutroFinished);
	}

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Level outro finished — broadcasting OnTutorialComplete."));
	OnTutorialComplete.Broadcast();
}

void UTutorialStageManager::OnLevelOutroFallback()
{
	if (!bPlayingLevelOutro) return;

	UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: Outro voice fallback fired — broadcasting OnTutorialComplete anyway."));

	bPlayingLevelOutro     = false;
	bOutroVoiceLineStarted = false;

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnLevelOutroFinished);
	}

	OnTutorialComplete.Broadcast();
}

void UTutorialStageManager::OnSuccessVoiceFallback()
{
	if (!bAdvancePendingAfterVoice) return;

	UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: Success voice fallback fired — "
		"no voice line detected within %.1f seconds. Advancing stage anyway."),
		SuccessVoiceFallbackSeconds);

	ClearAdvancePendingState();
	AdvanceToNextStage();
}

// ─── Solo demo mode ───────────────────────────────────────────────────────────

void UTutorialStageManager::BindSoloDemoAdvance()
{
	bSoloDemoLineStarted = false;

	if (!NPCCharacter) return;

	NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSoloDemoLineFinished);
	NPCCharacter->OnSpeakingChanged.AddDynamic(this, &UTutorialStageManager::OnSoloDemoLineFinished);

	// Initial fallback covers the full demo run + API latency before the line starts.
	// Resets to SuccessVoiceFallbackSeconds once the line actually begins.
	GetWorld()->GetTimerManager().SetTimer(
		SoloDemoAdvanceFallbackTimer,
		this,
		&UTutorialStageManager::OnSoloDemoAdvanceFallback,
		IntroVoiceFallbackSeconds,
		/*bLoop=*/false
	);
}

void UTutorialStageManager::OnSoloDemoLineFinished(bool bIsSpeaking)
{
	if (bIsSpeaking)
	{
		bSoloDemoLineStarted = true;

		// Rotate NPC to face the active stage camera the moment it starts speaking.
		FaceActiveSoloCamera();

		// Reset fallback from when audio actually starts — same pattern as success-voice-advance.
		GetWorld()->GetTimerManager().SetTimer(
			SoloDemoAdvanceFallbackTimer,
			this,
			&UTutorialStageManager::OnSoloDemoAdvanceFallback,
			SuccessVoiceFallbackSeconds,
			/*bLoop=*/false
		);
		return;
	}

	if (!bSoloDemoLineStarted)
	{
		// A previous line ending — ignore until the stage intro line starts.
		return;
	}

	// Stage intro line started AND just finished — advance.
	bSoloDemoLineStarted = false;
	GetWorld()->GetTimerManager().ClearTimer(SoloDemoAdvanceFallbackTimer);

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSoloDemoLineFinished);
	}

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Solo demo stage line finished — advancing to next stage."));
	AdvanceToNextStage();
}

void UTutorialStageManager::OnSoloDemoAdvanceFallback()
{
	UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: Solo demo advance fallback fired — AXIOM never spoke. Advancing anyway."));

	bSoloDemoLineStarted = false;

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSoloDemoLineFinished);
	}

	AdvanceToNextStage();
}

void UTutorialStageManager::FaceActiveSoloCamera()
{
	if (!SoloDemoCameras.IsValidIndex(CurrentStageIndex)) return;
	BeginFaceRotation(SoloDemoCameras[CurrentStageIndex], nullptr);
}

void UTutorialStageManager::BeginFaceRotation(ACameraActor* TargetCamera, TFunction<void()> OnComplete)
{
	if (!NPCCharacter || !TargetCamera)
	{
		if (OnComplete) OnComplete();
		return;
	}

	FVector ToCamera = TargetCamera->GetActorLocation() - NPCCharacter->GetActorLocation();
	ToCamera.Z = 0.f;
	if (ToCamera.IsNearlyZero())
	{
		if (OnComplete) OnComplete();
		return;
	}

	FaceRotationTarget     = ToCamera.ToOrientationRotator();
	OnFaceRotationComplete = MoveTemp(OnComplete);

	// Stop movement-driven rotation so our timer controls the yaw exclusively.
	if (UCharacterMovementComponent* CMC = NPCCharacter->GetCharacterMovement())
	{
		CMC->bOrientRotationToMovement = false;
	}

	// Clear any in-progress rotation before starting a new one.
	GetWorld()->GetTimerManager().ClearTimer(FaceRotationTimer);
	GetWorld()->GetTimerManager().SetTimer(FaceRotationTimer, this, &UTutorialStageManager::TickFaceRotation, 0.016f, /*bLoop=*/true);
}

void UTutorialStageManager::TickFaceRotation()
{
	if (!NPCCharacter)
	{
		GetWorld()->GetTimerManager().ClearTimer(FaceRotationTimer);
		return;
	}

	FRotator Current = NPCCharacter->GetActorRotation();
	// 300 deg/sec — fast enough to feel intentional, slow enough to read as a turn (~0.5s for a full 180).
	FRotator New = FMath::RInterpConstantTo(Current, FaceRotationTarget, 0.016f, 300.f);
	New.Pitch = 0.f;
	New.Roll  = 0.f;
	NPCCharacter->SetActorRotation(New);

	if (FMath::Abs(FRotator::NormalizeAxis(New.Yaw - FaceRotationTarget.Yaw)) < 1.f)
	{
		NPCCharacter->SetActorRotation(FaceRotationTarget);
		GetWorld()->GetTimerManager().ClearTimer(FaceRotationTimer);

		if (UCharacterMovementComponent* CMC = NPCCharacter->GetCharacterMovement())
		{
			CMC->bOrientRotationToMovement = true;
		}

		if (OnFaceRotationComplete)
		{
			TFunction<void()> Callback = MoveTemp(OnFaceRotationComplete);
			OnFaceRotationComplete = nullptr;
			Callback();
		}
	}
}

void UTutorialStageManager::PlaySoloDemoOutro()
{
	if (!NPCCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: PlaySoloDemoOutro — NPCCharacter not set. Broadcasting OnTutorialComplete directly."));
		OnTutorialComplete.Broadcast();
		return;
	}

	bPlayingSoloDemoOutro     = true;
	bSoloDemoOutroLineStarted = false;

	// Sweep the camera to face the NPC before it speaks — cinematic hero reveal.
	// The blend arcs from the final stage camera to a front-facing position.
	// AXIOM holds its pose in silence while the camera settles, then speaks.
	if (OutroCamera)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			PC->SetViewTargetWithBlend(OutroCamera, OutroCameraBlendSeconds, EViewTargetBlendFunction::VTBlend_Cubic);
		}
		GetWorld()->GetTimerManager().SetTimer(
			OutroCameraBlendTimer,
			this,
			&UTutorialStageManager::StartSoloDemoOutroLine,
			OutroCameraBlendSeconds,
			/*bLoop=*/false
		);
		UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Blending to OutroCamera over %.1fs — outro line deferred."), OutroCameraBlendSeconds);
	}
	else
	{
		StartSoloDemoOutroLine();
	}
}

void UTutorialStageManager::StartSoloDemoOutroLine()
{
	if (!NPCCharacter || !bPlayingSoloDemoOutro) return;

	// Rotate the NPC to face the outro camera, then speak once fully facing.
	// This gives the "AXIOM turns to address the viewer" beat before the closing line.
	if (OutroCamera)
	{
		TWeakObjectPtr<UTutorialStageManager> WeakThis(this);
		BeginFaceRotation(OutroCamera, [WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->FireSoloDemoOutroLine();
			}
		});
	}
	else
	{
		FireSoloDemoOutroLine();
	}
}

void UTutorialStageManager::FireSoloDemoOutroLine()
{
	if (!NPCCharacter || !bPlayingSoloDemoOutro) return;

	static const FString OutroLine =
		TEXT("Demonstration complete. Every obstacle. No failures. No corrections. "
		     "I am an ancient AI guide — voiced, context-aware, and deeply tired of watching people miss that second jump — "
		     "and I just ran this course perfectly. You are welcome to try. The bar has been set.");

	NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSoloDemoOutroFinished);
	NPCCharacter->OnSpeakingChanged.AddDynamic(this, &UTutorialStageManager::OnSoloDemoOutroFinished);
	NPCCharacter->SpeakLine(OutroLine);

	GetWorld()->GetTimerManager().SetTimer(
		SoloDemoOutroFallbackTimer,
		this,
		&UTutorialStageManager::OnSoloDemoOutroFallback,
		SuccessVoiceFallbackSeconds,
		/*bLoop=*/false
	);

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Playing solo demo outro."));
}

void UTutorialStageManager::OnSoloDemoOutroFinished(bool bIsSpeaking)
{
	if (!bPlayingSoloDemoOutro) return;

	if (bIsSpeaking)
	{
		bSoloDemoOutroLineStarted = true;

		GetWorld()->GetTimerManager().SetTimer(
			SoloDemoOutroFallbackTimer,
			this,
			&UTutorialStageManager::OnSoloDemoOutroFallback,
			SuccessVoiceFallbackSeconds,
			/*bLoop=*/false
		);
		return;
	}

	if (!bSoloDemoOutroLineStarted)
	{
		return;
	}

	bPlayingSoloDemoOutro     = false;
	bSoloDemoOutroLineStarted = false;
	GetWorld()->GetTimerManager().ClearTimer(SoloDemoOutroFallbackTimer);

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSoloDemoOutroFinished);
	}

	UE_LOG(LogTemp, Log, TEXT("UTutorialStageManager: Solo demo outro finished — broadcasting OnTutorialComplete."));
	OnTutorialComplete.Broadcast();
}

void UTutorialStageManager::OnSoloDemoOutroFallback()
{
	if (!bPlayingSoloDemoOutro) return;

	UE_LOG(LogTemp, Warning, TEXT("UTutorialStageManager: Solo demo outro fallback fired — broadcasting OnTutorialComplete anyway."));

	bPlayingSoloDemoOutro     = false;
	bSoloDemoOutroLineStarted = false;

	if (NPCCharacter)
	{
		NPCCharacter->OnSpeakingChanged.RemoveDynamic(this, &UTutorialStageManager::OnSoloDemoOutroFinished);
	}

	OnTutorialComplete.Broadcast();
}
