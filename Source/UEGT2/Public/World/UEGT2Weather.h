// Fairhaven (UEGT2) - weather presets.
//
// A preset is a plain description of how one kind of weather looks: how much of
// the sun survives it, what it does to the sky light, and how thick the fog is.
// AUEGT2SkyController is what applies it; nothing here touches the world, which
// is deliberate - it makes the table testable without a map.
#pragma once

#include "CoreMinimal.h"
#include "UEGT2Weather.generated.h"

UENUM(BlueprintType)
enum class EUEGT2Weather : uint8
{
	Clear,
	Cloudy,
	Overcast,
	Foggy,
	Storm,
	Count UMETA(Hidden)
};

/**
 * Multipliers and absolute values for one weather state. Sun and sky values are
 * scales applied on top of whatever the time of day already worked out, so
 * weather and the day/night cycle compose instead of fighting.
 */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2WeatherPreset
{
	GENERATED_BODY()

	/** Scale on the sun intensity the time of day asked for. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") float SunIntensityScale = 1.0f;

	/** Pulls the sun colour toward this. 0 keeps the time-of-day colour. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") FLinearColor SunTint = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") float SunTintStrength = 0.0f;

	/** Scale on the sky light intensity. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") float SkyLightScale = 1.0f;

	/** Absolute exponential height fog density. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") float FogDensity = 0.012f;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") float FogMaxOpacity = 0.92f;

	/** Pulls the fog inscattering colour toward this, by this much. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") FLinearColor FogTint = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") float FogTintStrength = 0.0f;

	/**
	 * Bottom of the volumetric cloud layer, in kilometres. There is no coverage
	 * parameter to drive - cloud density lives in the cloud material - but a
	 * lower, thicker-looking deck is most of what "overcast" reads as.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Weather") float CloudBottomAltitudeKm = 5.0f;
};

/** The preset table. Out-of-range values clamp to Clear rather than assert. */
UEGT2_API const FUEGT2WeatherPreset& GetWeatherPreset(EUEGT2Weather Weather);

/** Display name for the menu and the log. */
UEGT2_API FText GetWeatherDisplayName(EUEGT2Weather Weather);

/** Parse a console argument such as "storm". Returns false if unrecognised. */
UEGT2_API bool ParseWeatherName(const FString& Name, EUEGT2Weather& OutWeather);
