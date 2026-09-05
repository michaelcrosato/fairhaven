// Fairhaven - an explicit guide to existing places the player can use.
#pragma once

#include "CoreMinimal.h"
#include "Interaction/UEGT2Amenity.h"
#include "Subsystems/WorldSubsystem.h"
#include "UEGT2ServicesSubsystem.generated.h"

class AUEGT2PlayerController;
struct FUEGT2SurveyDirection;

enum class EUEGT2ServiceCategory : uint8 { Food, Washroom, Rest, PaidWork, FoodAtHome, Sleep, Count };

/** Six rows, including missing categories. Rates are coins per world hour. */
struct UEGT2_API FUEGT2ServiceEntry
{
	EUEGT2ServiceCategory Category = EUEGT2ServiceCategory::Food;
	FText CategoryName;
	FText Name;
	TWeakObjectPtr<AUEGT2Amenity> Amenity;
	float DistanceMetres = 0.0f;
	float CostPerHour = 0.0f;
	float WagePerHour = 0.0f;
	EUEGT2Activity Activity = EUEGT2Activity::Idle;
	EUEGT2NPCRole JobRole = EUEGT2NPCRole::Villager;
};

/** One explicit actor scan per guide refresh; live directions inspect one weak actor. */
UCLASS(Config = Game, DefaultConfig)
class UEGT2_API UUEGT2ServicesSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UUEGT2ServicesSubsystem* Get(const UWorld* World);
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool IsAvailable() const;
	bool IsEnabled() const;
	TArray<FUEGT2ServiceEntry> GetEntries(const AUEGT2PlayerController* Controller) const;
	bool TrackAmenity(AUEGT2Amenity* Amenity);
	void ClearTracking();
	AUEGT2Amenity* GetTrackedAmenity() const;
	bool GetTrackedDirection(const FVector& Origin, float ViewYaw, FUEGT2SurveyDirection& Out) const;

	/** Independent of the preference and every other gameplay feature. */
	UPROPERTY(Config) bool bFeatureEnabled = true;

private:
	void RefreshFromSettings();
	void DropTracking(const TCHAR* Reason) const;
	mutable TWeakObjectPtr<AUEGT2Amenity> TrackedAmenity;
	mutable EUEGT2AmenityKind TrackedKind = EUEGT2AmenityKind::Count;
	mutable EUEGT2NPCRole TrackedJobRole = EUEGT2NPCRole::Villager;
	mutable FText TrackedVenueName;
};
