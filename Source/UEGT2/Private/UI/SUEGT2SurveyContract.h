// Fairhaven - one survey objective, checked and paid at its signpost.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AUEGT2PlayerController;
class AUEGT2SurveyContract;
class UUEGT2SurveyContractSubsystem;
class SButton;
struct FUEGT2SurveyContractEntry;

class SUEGT2SurveyContract : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUEGT2SurveyContract) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2PlayerController>, Controller)
		SLATE_ARGUMENT(TWeakObjectPtr<AUEGT2SurveyContract>, Board)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	TSharedPtr<SWidget> GetInitialFocusWidget() const;

private:
	TSharedRef<SWidget> BuildRow(const FUEGT2SurveyContractEntry& Entry);
	bool CanUseBoard() const;
	FReply ClaimPayment();

	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	TWeakObjectPtr<AUEGT2SurveyContract> Board;
	TWeakObjectPtr<UUEGT2SurveyContractSubsystem> Contract;
	FSimpleDelegate OnClose;
	TSharedPtr<SButton> ResumeButton;
	TSharedPtr<SButton> ClaimButton;
	FText StatusText;
	bool bReadyToClaim = false;
};
