// Fairhaven (UEGT2) - one actor that owns the look of the sky.
//
// Finds the sun, sky light, atmosphere, clouds and fog in the level and drives
// them from a single TimeOfDay plus a weather preset. It also spawns and drives
// a moon, because below the horizon the sun is switched off and something has
// to light the night.
//
// The day/night cycle is on by default. The placed actor in the map serialises
// DayLengthMinutes = 0 (see Tools/Python/uegt2/lighting.py), so BeginPlay
// promotes a zero day length to DefaultDayLengthMinutes rather than relying on
// a C++ default the map would override. Screenshot tours and the walk smoke
// force it back to frozen so they stay reproducible.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/UEGT2Weather.h"
#include "UEGT2SkyController.generated.h"

class ADirectionalLight;
class AExponentialHeightFog;
class ASkyLight;
class APostProcessVolume;
class ASkyAtmosphere;
class AVolumetricCloud;

UCLASS(ClassGroup = "UEGT2")
class UEGT2_API AUEGT2SkyController : public AActor
{
	GENERATED_BODY()

public:
	AUEGT2SkyController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** The first sky controller in the world, or null if the map has none. */
	static AUEGT2SkyController* Get(const UWorld* World);

	/** Hour of day, 0-24. 10.5 is the default warm mid-morning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float TimeOfDay = 10.5f;

	/** Real minutes for a full 24 hour cycle. 0 freezes the sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky", meta = (ClampMin = "0.0"))
	float DayLengthMinutes = 0.0f;

	/** Master switch for the moving sun. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky")
	bool bDayNightCycleEnabled = true;

	/** Used when the cycle is on but the map serialised a zero day length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky", meta = (ClampMin = "1.0"))
	float DefaultDayLengthMinutes = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky")
	float NoonIntensity = 75000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky")
	float MaxSunElevation = 58.0f;

	/**
	 * Moonlight in lux. Far brighter than a real full moon (~0.25 lux) on
	 * purpose: the night exposure floor below only buys back so much, and a
	 * physically honest moon renders as pure black.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky")
	float MoonIntensity = 20.0f;

	/**
	 * Auto exposure EV100 range, driven from the time of day.
	 *
	 * This has to be here rather than in lighting.py: that stage bakes a fixed
	 * floor of 10.5 EV into the post process volume, which is daylight. Night
	 * sits near EV 0, so a fixed daylight floor clamps it about ten stops too
	 * dark and the screen goes black. The day values match what lighting.py
	 * writes, so noon is unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky") float DayExposureMinEV = 10.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky") float DayExposureMaxEV = 14.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky") float NightExposureMinEV = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky") float NightExposureMaxEV = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Sky")
	FLinearColor MoonColour = FLinearColor(0.55f, 0.66f, 0.95f);

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Sky")
	void SetTimeOfDay(float Hours);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Sky")
	float GetTimeOfDay() const { return TimeOfDay; }

	/** Turn the moving sun on or off. Leaves the current hour alone. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Sky")
	void SetDayNightCycleEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Sky")
	bool IsDayNightCycleEnabled() const { return bDayNightCycleEnabled && DayLengthMinutes > KINDA_SMALL_NUMBER; }

	/** Real minutes per in-game day. Values at or below zero freeze the sun. */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Sky")
	void SetDayLengthMinutes(float Minutes);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Sky")
	float GetDayLengthMinutes() const { return DayLengthMinutes; }

	UFUNCTION(BlueprintCallable, Category = "UEGT2|Weather")
	void SetWeather(EUEGT2Weather NewWeather);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Weather")
	EUEGT2Weather GetWeather() const { return Weather; }

	/** Manual fog density on top of the preset. Negative means "use the preset". */
	UFUNCTION(BlueprintCallable, Category = "UEGT2|Weather")
	void SetFogDensityOverride(float Density);

	UFUNCTION(BlueprintPure, Category = "UEGT2|Weather")
	float GetEffectiveFogDensity() const;

	/** True between dusk and dawn, for anything that wants to know. */
	UFUNCTION(BlueprintPure, Category = "UEGT2|Sky")
	bool IsNight() const;

	/** Re-find the sky actors and push the current state onto them. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "UEGT2|Sky")
	void RefreshSky();

private:
	void CacheSkyActors();
	void ApplySky();
	void EnsureMoon();
	float SunElevationDegrees() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Weather", meta = (AllowPrivateAccess = "true"))
	EUEGT2Weather Weather = EUEGT2Weather::Clear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UEGT2|Weather", meta = (AllowPrivateAccess = "true"))
	float FogDensityOverride = -1.0f;

	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun = nullptr;
	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Moon = nullptr;
	UPROPERTY(Transient) TObjectPtr<ASkyLight> SkyLight = nullptr;
	UPROPERTY(Transient) TObjectPtr<AExponentialHeightFog> Fog = nullptr;
	UPROPERTY(Transient) TObjectPtr<AVolumetricCloud> Clouds = nullptr;
	UPROPERTY(Transient) TObjectPtr<APostProcessVolume> PostProcess = nullptr;
};
