#include "World/UEGT2Weather.h"

#define LOCTEXT_NAMESPACE "UEGT2Weather"

namespace UEGT2WeatherTable
{
	/**
	 * Indexed by EUEGT2Weather. Fog densities are absolute because the map ships
	 * with 0.012 and the presets need to be able to go both thinner and far
	 * thicker; sun and sky are scales so that a storm at midnight is still dark
	 * rather than being lifted back up to a fixed value.
	 */
	const FUEGT2WeatherPreset Presets[] =
	{
		// Clear
		[]
		{
			FUEGT2WeatherPreset P;
			P.SunIntensityScale = 1.0f;
			P.SkyLightScale = 1.0f;
			P.FogDensity = 0.012f;
			P.FogMaxOpacity = 0.92f;
			P.CloudBottomAltitudeKm = 5.0f;
			return P;
		}(),
		// Cloudy
		[]
		{
			FUEGT2WeatherPreset P;
			P.SunIntensityScale = 0.75f;
			P.SunTint = FLinearColor(0.86f, 0.90f, 0.98f);
			P.SunTintStrength = 0.35f;
			P.SkyLightScale = 0.90f;
			P.FogDensity = 0.020f;
			P.FogMaxOpacity = 0.94f;
			P.FogTint = FLinearColor(0.62f, 0.68f, 0.76f);
			P.FogTintStrength = 0.30f;
			P.CloudBottomAltitudeKm = 4.0f;
			return P;
		}(),
		// Overcast
		[]
		{
			FUEGT2WeatherPreset P;
			P.SunIntensityScale = 0.35f;
			P.SunTint = FLinearColor(0.78f, 0.82f, 0.90f);
			P.SunTintStrength = 0.70f;
			P.SkyLightScale = 0.70f;
			P.FogDensity = 0.045f;
			P.FogMaxOpacity = 0.96f;
			P.FogTint = FLinearColor(0.52f, 0.56f, 0.62f);
			P.FogTintStrength = 0.60f;
			P.CloudBottomAltitudeKm = 2.2f;
			return P;
		}(),
		// Foggy
		[]
		{
			FUEGT2WeatherPreset P;
			P.SunIntensityScale = 0.50f;
			P.SunTint = FLinearColor(0.92f, 0.92f, 0.94f);
			P.SunTintStrength = 0.55f;
			P.SkyLightScale = 0.80f;
			P.FogDensity = 0.180f;
			P.FogMaxOpacity = 0.99f;
			P.FogTint = FLinearColor(0.74f, 0.77f, 0.80f);
			P.FogTintStrength = 0.75f;
			P.CloudBottomAltitudeKm = 3.0f;
			return P;
		}(),
		// Storm
		[]
		{
			FUEGT2WeatherPreset P;
			P.SunIntensityScale = 0.18f;
			P.SunTint = FLinearColor(0.62f, 0.66f, 0.78f);
			P.SunTintStrength = 0.85f;
			P.SkyLightScale = 0.50f;
			P.FogDensity = 0.090f;
			P.FogMaxOpacity = 0.98f;
			P.FogTint = FLinearColor(0.26f, 0.29f, 0.35f);
			P.FogTintStrength = 0.80f;
			P.CloudBottomAltitudeKm = 1.6f;
			return P;
		}(),
	};

	static_assert(UE_ARRAY_COUNT(Presets) == (int32)EUEGT2Weather::Count,
		"Every EUEGT2Weather entry needs a preset.");
}

const FUEGT2WeatherPreset& GetWeatherPreset(EUEGT2Weather Weather)
{
	const int32 Index = (int32)Weather;
	if (Index < 0 || Index >= (int32)EUEGT2Weather::Count)
	{
		return UEGT2WeatherTable::Presets[(int32)EUEGT2Weather::Clear];
	}
	return UEGT2WeatherTable::Presets[Index];
}

FText GetWeatherDisplayName(EUEGT2Weather Weather)
{
	switch (Weather)
	{
	case EUEGT2Weather::Clear:    return LOCTEXT("WeatherClear", "Clear");
	case EUEGT2Weather::Cloudy:   return LOCTEXT("WeatherCloudy", "Cloudy");
	case EUEGT2Weather::Overcast: return LOCTEXT("WeatherOvercast", "Overcast");
	case EUEGT2Weather::Foggy:    return LOCTEXT("WeatherFoggy", "Foggy");
	case EUEGT2Weather::Storm:    return LOCTEXT("WeatherStorm", "Storm");
	default:                      return LOCTEXT("WeatherUnknown", "Unknown");
	}
}

bool ParseWeatherName(const FString& Name, EUEGT2Weather& OutWeather)
{
	static const TCHAR* Names[] = { TEXT("clear"), TEXT("cloudy"), TEXT("overcast"),
		TEXT("foggy"), TEXT("storm") };
	static_assert(UE_ARRAY_COUNT(Names) == (int32)EUEGT2Weather::Count,
		"Every EUEGT2Weather entry needs a parse name.");

	for (int32 Index = 0; Index < (int32)EUEGT2Weather::Count; ++Index)
	{
		if (Name.Equals(Names[Index], ESearchCase::IgnoreCase))
		{
			OutWeather = (EUEGT2Weather)Index;
			return true;
		}
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
