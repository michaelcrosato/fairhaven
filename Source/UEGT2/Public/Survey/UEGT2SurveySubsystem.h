// Fairhaven - a journal of the places this explorer has surveyed.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2SurveySubsystem.generated.h"

class AUEGT2Landmark;

/** A view of live landmark state, never a second discovery ledger. */
struct UEGT2_API FUEGT2SurveyEntry
{
	FName Id = NAME_None;
	FText Name;
	bool bDiscovered = false;
};

/** Horizontal guidance. Nearby has neutral bearings rather than a flickering arrow. */
struct UEGT2_API FUEGT2SurveyDirection
{
	FName Id = NAME_None;
	FText Name;
	float DistanceMetres = 0.0f;
	/** Clockwise from north, in [0, 360). */
	float BearingDegrees = 0.0f;
	/** Clockwise from the view, in [-180, 180). */
	float RelativeBearingDegrees = 0.0f;
	FText CompassDirection;
	bool bNearby = false;
};

/** No tick: roster refreshes are explicit, direction queries use one weak actor. */
UCLASS(Config = Game, DefaultConfig)
class UEGT2_API UUEGT2SurveySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UUEGT2SurveySubsystem* Get(const UWorld* World);
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool IsAvailable() const;
	bool IsEnabled() const;
	TArray<FUEGT2SurveyEntry> GetEntries() const;
	bool TrackLandmark(FName Id);
	void ClearTracking();
	FName GetTrackedLandmarkId() const;
	bool GetTrackedDirection(const FVector& Origin, float ViewYaw, FUEGT2SurveyDirection& Out) const;

	/** +X north, +Y east; metres from horizontal centimetres, nearby at <=10m. */
	static bool CalculateDirection(const FVector& Origin, const FVector& Target,
		float ViewYaw, FUEGT2SurveyDirection& Out);

	/** Hard gate, independent of the player's journal preference. */
	UPROPERTY(Config) bool bFeatureEnabled = true;

private:
	void RefreshFromSettings();
	TMap<FName, AUEGT2Landmark*> GatherLandmarks() const;
	AUEGT2Landmark* GetValidTrackedLandmark() const;
	void DropTracking(const TCHAR* Reason) const;

	mutable TWeakObjectPtr<AUEGT2Landmark> TrackedLandmark;
	mutable FName TrackedId = NAME_None;
};
