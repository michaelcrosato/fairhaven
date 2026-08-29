// Fairhaven (UEGT2) - the date, the clock and the temperature.
//
// Pure functions, like the weather presets next door: no world, no actor, no
// widget, so the whole thing can be tested without a map. The HUD reads them
// once a frame and draws the result.
//
// There is no thermometer in the world to read, so the temperature is computed
// from the things that would actually determine it here: where you are standing
// in a world whose west is tropical and whose north-east is a 440 m mountain
// range, how high up you are, the hour, the season and the weather. It is a
// model rather than a measurement, but every term in it is real.
#pragma once

#include "CoreMinimal.h"
#include "World/UEGT2Weather.h"
#include "UEGT2Almanac.generated.h"

/** Six-day weeks, thirty-day months, twelve months. See UEGT2DaysPerWeek. */
inline constexpr int32 UEGT2WeeksPerMonth = 5;
inline constexpr int32 UEGT2MonthsPerYear = 12;

/** A day index turned into something you can put on a HUD. */
USTRUCT(BlueprintType)
struct UEGT2_API FUEGT2Date
{
	GENERATED_BODY()

	/** 1-based, as a date is written. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Almanac") int32 Day = 1;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Almanac") int32 Month = 1;
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Almanac") int32 Year = 1;

	/** 0-based day of the week; 2 is market day and 5 is rest day. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Almanac") int32 Weekday = 0;

	/** 0 spring, 1 summer, 2 autumn, 3 winter. */
	UPROPERTY(BlueprintReadOnly, Category = "UEGT2|Almanac") int32 Season = 0;
};

/** Split a running day count into a date. Negative counts fold, they do not wrap. */
UEGT2_API FUEGT2Date UEGT2DateFromDayIndex(int32 DayIndex);

UEGT2_API FText UEGT2WeekdayName(int32 Weekday);
UEGT2_API FText UEGT2MonthName(int32 Month);
UEGT2_API FText UEGT2SeasonName(int32 Season);

/** "Marketday, 12th of Highsun" - the line the HUD shows. */
UEGT2_API FText UEGT2FormatDate(const FUEGT2Date& Date);

/** "14:35". Hour is 0-24; values outside fold into the day. */
UEGT2_API FText UEGT2FormatClock(float Hour);

/** "afternoon", "dusk", "the small hours" - the time of day in words. */
UEGT2_API FText UEGT2TimeOfDayName(float Hour);

/**
 * Air temperature in Celsius where the player is standing.
 *
 * @param WorldX,WorldY  world position in centimetres
 * @param AltitudeCm     height above sea level in centimetres
 * @param Hour           0-24
 * @param DayIndex       running day count, for the season
 * @param Weather        what the sky is doing
 */
UEGT2_API float UEGT2TemperatureC(float WorldX, float WorldY, float AltitudeCm,
	float Hour, int32 DayIndex, EUEGT2Weather Weather);

/** "17 C" or "63 F", depending on the setting. */
UEGT2_API FText UEGT2FormatTemperature(float Celsius, bool bFahrenheit);
