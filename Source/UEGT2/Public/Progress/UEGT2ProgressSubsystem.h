// Fairhaven - explicit, local journey checkpoints. No autosaves.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UEGT2ProgressSubsystem.generated.h"

class AUEGT2PlayerController;
class UUEGT2ProgressSave;

UCLASS(Config = Game, DefaultConfig)
class UEGT2_API UUEGT2ProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UUEGT2ProgressSubsystem* Get(const UWorld* World);

	/** Maintainer and diagnostic gate, independent of the player's preference. */
	bool IsAvailable() const;
	bool IsEnabled() const;
	bool HasSavedProgress() const;
	bool SaveProgress(AUEGT2PlayerController* Controller);
	bool LoadProgress(AUEGT2PlayerController* Controller);
	FText GetStatusText() const;

	void SetJourneyActive(bool bActive);
	void RequestNewJourney();
	bool ConsumeNewJourneyRequest();

	/** Hard off switch. The player preference cannot override this. */
	UPROPERTY(Config) bool bFeatureEnabled = true;

private:
	bool ResolveSlot(FString& OutSlot) const;
	bool Fail(const FText& Reason);
	UUEGT2ProgressSave* ReadCheckpoint(const FString& Slot, const FString& Map,
		const TSet<FName>& LandmarkIds, FString& OutSourceSlot, FText& OutReason,
		bool* bOutHadFile = nullptr) const;

	bool bJourneyActive = false;
	bool bNewJourneyRequested = false;
	TWeakObjectPtr<UWorld> JourneyWorld;
	mutable FText StatusText;
	mutable TWeakObjectPtr<UWorld> AvailabilityWorld;
	mutable FString AvailabilitySlot;
	mutable bool bAvailabilityCached = false;
	mutable bool bHasCheckpoint = false;
};
