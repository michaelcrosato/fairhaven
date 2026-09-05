// Fairhaven - choosing how long to sleep at the existing bed amenity.
#pragma once

#include "CoreMinimal.h"
#include "Rest/UEGT2RestTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2RestSubsystem.generated.h"

class AUEGT2Amenity;
class AUEGT2PlayerController;

/** Explicit skipped life, independent of checkpoints and Unreal's timer clock. */
UCLASS(Config = Game, DefaultConfig)
class UEGT2_API UUEGT2RestSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UUEGT2RestSubsystem* Get(const UWorld* World);
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	bool IsAvailable() const;
	bool IsEnabled() const;
	bool CanSleepAt(const AUEGT2PlayerController* Controller, const AUEGT2Amenity* Bed, FText& Reason) const;
	bool GetPreview(const AUEGT2PlayerController* Controller, const AUEGT2Amenity* Bed,
		int32 WakeHour, FUEGT2RestPreview& Out, FText& Reason) const;
	/** Commit only from the paused bed panel; failure leaves life and calendar unchanged. */
	bool SleepUntil(AUEGT2PlayerController* Controller, AUEGT2Amenity* Bed, int32 WakeHour, FText& Reason);

	/** Independent hard gate. Off restores ordinary, continuous sleep. */
	UPROPERTY(Config) bool bFeatureEnabled = true;

private:
	bool bCommitting = false;
};
