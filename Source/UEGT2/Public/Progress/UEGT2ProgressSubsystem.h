// Fairhaven - manual checkpoints and a separate optional automatic checkpoint.
#pragma once

#include "CoreMinimal.h"
#include "Progress/UEGT2AutosaveTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UEGT2ProgressSubsystem.generated.h"

class AUEGT2PlayerController;
class UUEGT2ProgressSave;
class IUEGT2CheckpointStorage;
struct FUEGT2AutosaveOperation;

UCLASS(Config = Game, DefaultConfig)
class UEGT2_API UUEGT2ProgressSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	static UUEGT2ProgressSubsystem* Get(const UWorld* World);

	/** Maintainer and diagnostic gate, independent of the player's preference. */
	bool IsAvailable() const;
	bool IsEnabled() const;
	bool HasSavedProgress() const;
	bool SaveProgress(AUEGT2PlayerController* Controller);
	bool LoadProgress(AUEGT2PlayerController* Controller);
	FText GetStatusText() const;
	bool IsJourneyActive(const UWorld* World) const;
	bool IsAutosaveSmoke() const;
	uint64 GetJourneyGeneration() const { return JourneyGeneration; }
	bool RequestAutosave(AUEGT2PlayerController* Controller);
	void RefreshAutosaveAvailability(AUEGT2PlayerController* Controller);
	FUEGT2AutosaveStatus GetAutosaveStatus() const;
	bool LoadAutosavedProgress(AUEGT2PlayerController* Controller);

	void SetJourneyActive(bool bActive);
	void RequestNewJourney();
	bool ConsumeNewJourneyRequest();

	/** Hard off switch. The player preference cannot override this. */
	UPROPERTY(Config) bool bFeatureEnabled = true;

private:
	friend struct FUEGT2AutosaveTestAccess;
	IUEGT2CheckpointStorage& GetStorage() const;
	bool CaptureSnapshot(AUEGT2PlayerController* Controller, int64 Sequence,
		UUEGT2ProgressSave& Snapshot, FText& Reason) const;
	bool RestoreSnapshot(AUEGT2PlayerController* Controller, const UUEGT2ProgressSave& Snapshot,
		bool& bOutFallback, FText& Reason);
	bool LoadCheckpoint(AUEGT2PlayerController* Controller, bool bAutosave);
	bool StartAutosaveOperation(AUEGT2PlayerController* Controller, bool bWrite);
	bool IsAutosaveOperationCurrent(const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation) const;
	void ReadAutosaveSlot(const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation, int32 Index);
	void ConsumeAutosaveSlot(const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation,
		int32 Index, bool bPresent, const TArray<uint8>& Bytes);
	void FinishAutosave(const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation,
		bool bSuccess, const FText& Reason);
	void InvalidateAutosaveContext();
	bool ResolveSlot(FString& OutSlot, bool* bOutAutosaveSmoke = nullptr) const;
	bool Fail(const FText& Reason);
	UUEGT2ProgressSave* ReadCheckpoint(const FString& Slot, const FString& Map,
		const TSet<FName>& LandmarkIds, FString& OutSourceSlot, FText& OutReason,
		bool* bOutHadFile = nullptr) const;

	bool bJourneyActive = false;
	uint64 JourneyGeneration = 0;
	uint64 AutosaveSuccessfulWrites = 0;
	uint64 AutosaveCacheRevision = 0;
	bool bShuttingDown = false;
	mutable TSharedPtr<IUEGT2CheckpointStorage> Storage;
	TSharedPtr<FUEGT2AutosaveOperation, ESPMode::ThreadSafe> AutosaveOperation;
	FUEGT2AutosaveStatus AutosaveStatus;
	TWeakObjectPtr<UWorld> AutosaveCacheWorld;
	FString AutosaveCacheSlot;
	bool bAutosaveCacheValid = false;
	TWeakObjectPtr<AUEGT2PlayerController> PendingAutosaveRefresh;
	bool bNewJourneyRequested = false;
	TWeakObjectPtr<UWorld> JourneyWorld;
	mutable FText StatusText;
	mutable TWeakObjectPtr<UWorld> AvailabilityWorld;
	mutable FString AvailabilitySlot;
	mutable bool bAvailabilityCached = false;
	mutable bool bHasCheckpoint = false;
};
