// Fairhaven - one fixed survey contract, backed by ordinary landmark discoveries.
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Subsystems/WorldSubsystem.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "UEGT2SurveyContractSubsystem.generated.h"

class AUEGT2PlayerController;
class AUEGT2SurveyContract;

/** Explicit page snapshot. Missing or ambiguous places remain visible as unavailable. */
struct UEGT2_API FUEGT2SurveyContractEntry
{
	FName Id;
	FText Name;
	bool bAvailable = false;
	bool bDiscovered = false;
	bool bHasDirection = false;
	FUEGT2SurveyDirection Direction;
};

UCLASS(Config = Game, DefaultConfig)
class UEGT2_API UUEGT2SurveyContractSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UUEGT2SurveyContractSubsystem* Get(const UWorld* World);
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	static TConstArrayView<FName> RequiredLandmarkIds();
	static float GetReward();
	bool IsAvailable() const;
	bool IsEnabled() const;
	bool IsPaid() const { return bPaid; }
	/** Persistence restores the paid flag and purse together after snapshot validation. */
	void RestorePaidState(bool bInPaid) { bPaid = bInPaid; }
	TArray<FUEGT2SurveyContractEntry> GetEntries(const AUEGT2PlayerController* Controller) const;
	/** Cheap eligibility for the board/page; no landmark enumeration. */
	bool CanOpenAt(const AUEGT2PlayerController* Controller, const AUEGT2SurveyContract* Board, FText& Reason) const;
	/** Explicit request only: resolves the required landmarks, never a Slate frame binding. */
	bool CanClaim(const AUEGT2PlayerController* Controller, const AUEGT2SurveyContract* Board, FText& Reason) const;
	bool TryClaim(AUEGT2PlayerController* Controller, AUEGT2SurveyContract* Board, FText& Reason);

	UPROPERTY(Config) bool bFeatureEnabled = true;

private:
	bool bPaid = false;
};
