// Fairhaven - opt-in periodic checkpoint requests during an active visit.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2AutosaveSubsystem.generated.h"

class AUEGT2PlayerController;

UCLASS(Config = Game, DefaultConfig)
class UEGT2_API UUEGT2AutosaveSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UUEGT2AutosaveSubsystem* Get(const UWorld* World);
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bWorldStarted && !IsTemplate(); }
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual TStatId GetStatId() const override;

	bool IsAvailable() const;
	bool IsEnabled() const;
	/** Live eligibility only; the progress service separately owns the I/O lock. */
	bool CanAutosaveNow(const AUEGT2PlayerController* Controller) const;
	float GetIntervalSeconds() const { return IntervalSeconds; }

	/** Independent hard switch. Disabling retains all existing checkpoints. */
	UPROPERTY(Config) bool bFeatureEnabled = true;

private:
	float IntervalSeconds = 300.0f;
	float ElapsedSeconds = 0.0f;
	float RetrySeconds = 0.0f;
	uint64 ObservedJourney = MAX_uint64;
	uint64 ObservedWrites = 0;
	uint64 ObservedSettings = 0;
	bool bWorldStarted = false;
};
