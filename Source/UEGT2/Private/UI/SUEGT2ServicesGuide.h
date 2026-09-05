// Fairhaven - directions to existing places to eat, wash, rest and work.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AUEGT2Amenity;
class AUEGT2PlayerController;
class UUEGT2ServicesSubsystem;
class SButton;
struct FUEGT2ServiceEntry;

class SUEGT2ServicesGuide : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEGT2ServicesGuide) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2PlayerController>, Controller)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetInitialFocusWidget() const;

private:
	TSharedRef<SWidget> BuildRow(const FUEGT2ServiceEntry& Entry);
	bool IsGuideEnabled() const;
	bool CanTrack(const FUEGT2ServiceEntry& Entry, const FText& OriginalVenue) const;
	bool IsTracking(TWeakObjectPtr<AUEGT2Amenity> Amenity) const;
	FReply Track(const FUEGT2ServiceEntry& Entry, const FText& OriginalVenue);
	void ConfigureNavigation();

	TWeakObjectPtr<UUEGT2ServicesSubsystem> Services;
	FSimpleDelegate OnClose;
	TSharedPtr<SButton> ResumeButton;
	TSharedPtr<SButton> StopButton;
	TArray<TSharedPtr<SButton>> TrackButtons;
	FText StatusText;
};
