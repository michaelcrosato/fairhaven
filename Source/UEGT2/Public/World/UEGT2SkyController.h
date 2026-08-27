// Fairhaven (UEGT2) - one actor that owns the look of the sky.
//
// Finds the sun, sky light, atmosphere and fog in the level and drives them
// from a single TimeOfDay value. Frozen by default so screenshots and playtests
// are reproducible; set DayLengthMinutes above zero for a moving sun.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UEGT2SkyController.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class ASkyLight;

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2SkyController : public AActor
{
	GENERATED_BODY()

public:
	AUEGT2SkyController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Hour of day, 0-24. 10.5 is the default warm mid-morning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float TimeOfDay = 10.5f;

	/** Real minutes for a full 24 hour cycle. 0 freezes the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky", meta = (ClampMin = "0.0"))
	float DayLengthMinutes = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky")
	float NoonIntensity = 75000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky")
	float MaxSunElevation = 58.0f;

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Sky")
	void SetTimeOfDay(float Hours);

	/** Re-find the sky actors and push the current time onto them. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "UEGT2|Sky")
	void RefreshSky();

private:
	void CacheSkyActors();
	void ApplyTimeOfDay();

	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun = nullptr;
	UPROPERTY(Transient) TObjectPtr<ASkyLight> SkyLight = nullptr;
	UPROPERTY(Transient) TObjectPtr<AExponentialHeightFog> Fog = nullptr;
};
