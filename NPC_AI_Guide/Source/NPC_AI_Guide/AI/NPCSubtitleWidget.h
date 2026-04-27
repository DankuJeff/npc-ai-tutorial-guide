// Copyright 2026 Tyler Munstock. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "NPCSubtitleWidget.generated.h"

/**
 * UNPCSubtitleWidget
 *
 * C++ base for the AXIOM screen-space subtitle overlay. Owns the typewriter reveal
 * logic and dynamic height sizing; Blueprint subclass (BP_NPCSubtitleWidget) handles
 * font and color styling only.
 *
 * Widget anchors to the bottom-center of the screen and grows upward as text fills in.
 * Width is fixed at SubtitleWidthFraction of the viewport width.
 *
 * Setup:
 *   1. Create a Blueprint subclass of this class.
 *   2. Name your Border widget "SubtitleBackground" and your TextBlock "SubtitleText".
 *   3. Implement OnSubtitleUpdated — set SubtitleText's text to VisibleText.
 *   4. Implement OnSubtitleHidden — hide SubtitleBackground.
 *   5. Set SubtitleWidgetClass on BP_ClaudeNPC to your subclass.
 */
UCLASS(Abstract)
class NPC_AI_GUIDE_API UNPCSubtitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Start displaying text with a typewriter effect.
	 * Cancels any in-progress reveal and restarts from the beginning.
	 */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void ShowSubtitle(const FString& Text);

	/** Clear the subtitle immediately and stop the typewriter timer. */
	UFUNCTION(BlueprintCallable, Category = "NPC AI")
	void HideSubtitle();

	/** Delay in seconds between each character reveal. Lower = faster typewriter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	float CharDelay = 0.03f;

	/** Subtitle box width as a fraction of viewport width (0.65–0.75 recommended). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI", meta = (ClampMin = 0.1, ClampMax = 1.0))
	float SubtitleWidthFraction = 0.70f;

	/** Vertical padding (px) added above and below the text inside the border. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	float SubtitlePaddingV = 18.f;

	/** Gap (px) between the bottom of the subtitle box and the screen edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	float SubtitleBottomMargin = 30.f;

	/** Minimum box height (px) — prevents a near-zero box on very short lines. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC AI")
	float MinHeightPx = 50.f;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ─── BindWidget — names must match Blueprint widget variable names exactly ──

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UBorder> SubtitleBackground;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<UTextBlock> SubtitleText;

	// ─── Blueprint events ─────────────────────────────────────────────────────

	/** Called once when a new subtitle begins. Implement in Blueprint: show SubtitleBackground. */
	UFUNCTION(BlueprintImplementableEvent, Category = "NPC AI")
	void OnSubtitleShown();

	/** Called each typewriter tick. Implement in Blueprint: set SubtitleText's text to VisibleText. */
	UFUNCTION(BlueprintImplementableEvent, Category = "NPC AI")
	void OnSubtitleUpdated(const FText& VisibleText);

	/** Called when the subtitle is cleared. Implement in Blueprint: hide SubtitleBackground. */
	UFUNCTION(BlueprintImplementableEvent, Category = "NPC AI")
	void OnSubtitleHidden();

private:
	FTimerHandle TypewriterTimer;
	FString FullText;
	int32 CharIndex = 0;

	// Cached canvas slot — set in NativeConstruct, reused every tick.
	UPROPERTY()
	TObjectPtr<UCanvasPanelSlot> BackgroundCanvasSlot;

	void TypewriterTick();

	// Reads current text desired size and resizes BackgroundCanvasSlot height.
	void UpdateWidgetLayout();
};
