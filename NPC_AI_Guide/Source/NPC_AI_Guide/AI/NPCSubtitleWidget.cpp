// Copyright 2026 Tyler Munstock. All Rights Reserved.
#include "AI/NPCSubtitleWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Engine.h"

void UNPCSubtitleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Set anchor and alignment only — no viewport size needed at this point.
	// Viewport dimensions are unreliable in NativeConstruct for New Window PIE;
	// size-dependent setup runs in UpdateWidgetLayout at first display instead.
	if (SubtitleBackground)
	{
		BackgroundCanvasSlot = Cast<UCanvasPanelSlot>(SubtitleBackground->Slot);
		if (BackgroundCanvasSlot)
		{
			BackgroundCanvasSlot->SetAnchors(FAnchors(0.5f, 1.0f));
			BackgroundCanvasSlot->SetAlignment(FVector2D(0.5f, 1.0f));
			BackgroundCanvasSlot->SetPosition(FVector2D(0.f, -SubtitleBottomMargin));
			BackgroundCanvasSlot->SetAutoSize(false);
		}
	}
}

void UNPCSubtitleWidget::NativeDestruct()
{
	// Prevent TypewriterTick from firing on a widget that is mid-reveal at the time
	// it gets removed from the viewport and garbage collected.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TypewriterTimer);
	}
	Super::NativeDestruct();
}

void UNPCSubtitleWidget::ShowSubtitle(const FString& Text)
{
	HideSubtitle(); // cancel any in-progress reveal, hides container via OnSubtitleHidden

	if (Text.IsEmpty()) return;

	FullText = Text;
	CharIndex = 0;

	// Make the container visible before the first character appears.
	OnSubtitleShown();

	// Reveal first character immediately, then set a recurring timer for the rest.
	TypewriterTick();

	if (FullText.Len() > 1)
	{
		GetWorld()->GetTimerManager().SetTimer(
			TypewriterTimer,
			this,
			&UNPCSubtitleWidget::TypewriterTick,
			CharDelay,
			/*bLoop=*/true
		);
	}
}

void UNPCSubtitleWidget::HideSubtitle()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TypewriterTimer);
	}
	FullText.Empty();
	CharIndex = 0;
	OnSubtitleHidden();
}

void UNPCSubtitleWidget::TypewriterTick()
{
	CharIndex++;
	OnSubtitleUpdated(FText::FromString(FullText.Left(CharIndex)));
	UpdateWidgetLayout();

	if (CharIndex >= FullText.Len())
	{
		GetWorld()->GetTimerManager().ClearTimer(TypewriterTimer);
	}
}

void UNPCSubtitleWidget::UpdateWidgetLayout()
{
	if (!SubtitleText || !BackgroundCanvasSlot) return;

	// Compute viewport width fresh every call — NativeConstruct fires before the
	// New Window PIE viewport is sized, so caching there gives wrong dimensions.
	FVector2D ViewportSize(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	const float WidthPx = ViewportSize.X * SubtitleWidthFraction;

	// Re-apply wrap width and slot width each call so they stay correct even if
	// the viewport resizes (e.g. window mode changes during a session).
	SubtitleText->SetWrapTextAt(WidthPx - (SubtitlePaddingV * 2.f));

	SubtitleText->ForceLayoutPrepass();
	const FVector2D TextDesired = SubtitleText->GetDesiredSize();
	const float NewHeight = FMath::Max(TextDesired.Y + SubtitlePaddingV * 2.f, MinHeightPx);
	BackgroundCanvasSlot->SetSize(FVector2D(WidthPx, NewHeight));
}
