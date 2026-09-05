// Fairhaven - choose a wake-up hour at the player's lodgings.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AUEGT2Amenity;
class AUEGT2PlayerController;
class UUEGT2RestSubsystem;
class SButton;

class SUEGT2RestPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEGT2RestPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2PlayerController>, Controller)
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2Amenity>, Bed)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	/** Cancel is always available, including when the bed or feature becomes unavailable. */
	TSharedPtr<SWidget> GetInitialFocusWidget() const;

private:
	void RefreshPreview();
	FReply ChangeHour(int32 Delta);
	FReply Sleep();
	bool CanSleepAtBed(FText& Reason) const;
	bool CanChooseHour() const;
	bool CanConfirmSleep() const;
	FText GetStatusText() const;
	EVisibility GetPreviewVisibility() const;

	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	TWeakObjectPtr<AUEGT2Amenity> Bed;
	TWeakObjectPtr<UUEGT2RestSubsystem> Rest;
	FSimpleDelegate OnClose;
	TSharedPtr<SButton> CancelButton;

	int32 WakeHour = 6;
	bool bPreviewValid = false;
	bool bSubmitting = false;
	FText CurrentTimeText;
	FText DurationText;
	FText WakeText;
	FText StatusText;
};
