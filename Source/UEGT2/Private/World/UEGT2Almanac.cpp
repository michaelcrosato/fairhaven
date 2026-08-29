#include "World/UEGT2Almanac.h"

#include "NPC/UEGT2NPCTypes.h"

#define LOCTEXT_NAMESPACE "UEGT2Almanac"

namespace
{
	constexpr int32 DaysPerMonth = UEGT2DaysPerWeek * UEGT2WeeksPerMonth;   // 30
	constexpr int32 DaysPerYear = DaysPerMonth * UEGT2MonthsPerYear;        // 360

	/** Fold a possibly negative index into 0..Modulus-1. */
	int32 Fold(int32 Value, int32 Modulus)
	{
		return ((Value % Modulus) + Modulus) % Modulus;
	}

	// --- The climate map ----------------------------------------------------
	// These come from Tools/Terrain/world_config.py, which is what actually
	// shaped the land: the west is tropical, the north-east is a mountain
	// range, and the snow line is at 300 m. Keeping the numbers in step is what
	// stops the readout claiming it is 24 degrees on a glacier.
	constexpr float TropicsStartX = -26000.0f;
	constexpr float TropicsFullX = -150000.0f;
	constexpr float MountainStartX = 22000.0f;
	constexpr float MountainFullX = 168000.0f;

	/** Temperate sea-level average, before anything is applied to it. */
	constexpr float BaseC = 14.0f;
	constexpr float TropicsWarmthC = 12.0f;
	constexpr float NorthernChillC = 7.0f;
	/** The environmental lapse rate: 6.5 degrees per kilometre climbed. */
	constexpr float LapseCPerKm = 6.5f;
	constexpr float DiurnalSwingC = 6.0f;
	constexpr float SeasonSwingC = 8.0f;

	float WeatherOffsetC(EUEGT2Weather Weather)
	{
		switch (Weather)
		{
		case EUEGT2Weather::Clear:    return 2.0f;
		case EUEGT2Weather::Cloudy:   return 0.0f;
		case EUEGT2Weather::Overcast: return -2.0f;
		case EUEGT2Weather::Foggy:    return -3.0f;
		case EUEGT2Weather::Storm:    return -4.5f;
		default:                      return 0.0f;
		}
	}
}

// ---------------------------------------------------------------------------
FUEGT2Date UEGT2DateFromDayIndex(int32 DayIndex)
{
	const int32 Folded = Fold(DayIndex, DaysPerYear);

	FUEGT2Date Date;
	Date.Year = 1 + FMath::FloorToInt((float)DayIndex / (float)DaysPerYear);
	Date.Month = 1 + (Folded / DaysPerMonth);
	Date.Day = 1 + (Folded % DaysPerMonth);
	// The weekday runs off the raw index, not the folded one, so it keeps in
	// step with IsMarketDay and IsRestDay across a year boundary. 360 is a whole
	// number of six-day weeks, so the two agree anyway - but only by luck, and
	// this does not depend on the luck holding.
	Date.Weekday = Fold(DayIndex, UEGT2DaysPerWeek);
	Date.Season = ((Date.Month - 1) / 3) % 4;
	return Date;
}

FText UEGT2WeekdayName(int32 Weekday)
{
	// Day 2 is market day and day 5 is rest day (UEGT2NPCTypes.cpp); the names
	// have to agree with that or the calendar lies about the town's own week.
	static const FText Names[UEGT2DaysPerWeek] = {
		LOCTEXT("Day0", "Firstday"), LOCTEXT("Day1", "Toilday"),
		LOCTEXT("Day2", "Marketday"), LOCTEXT("Day3", "Middleday"),
		LOCTEXT("Day4", "Craftday"), LOCTEXT("Day5", "Restday"),
	};
	return Names[FMath::Clamp(Weekday, 0, UEGT2DaysPerWeek - 1)];
}

FText UEGT2MonthName(int32 Month)
{
	static const FText Names[UEGT2MonthsPerYear] = {
		LOCTEXT("M1", "Thawmoon"),   LOCTEXT("M2", "Seedfall"),
		LOCTEXT("M3", "Brightening"), LOCTEXT("M4", "Highsun"),
		LOCTEXT("M5", "Longlight"),  LOCTEXT("M6", "Hayrise"),
		LOCTEXT("M7", "Harvest"),    LOCTEXT("M8", "Goldfall"),
		LOCTEXT("M9", "Emberdusk"),  LOCTEXT("M10", "Mistmoon"),
		LOCTEXT("M11", "Darkening"), LOCTEXT("M12", "Yearsend"),
	};
	return Names[FMath::Clamp(Month - 1, 0, UEGT2MonthsPerYear - 1)];
}

FText UEGT2SeasonName(int32 Season)
{
	static const FText Names[4] = {
		LOCTEXT("Spring", "spring"), LOCTEXT("Summer", "summer"),
		LOCTEXT("Autumn", "autumn"), LOCTEXT("Winter", "winter"),
	};
	return Names[FMath::Clamp(Season, 0, 3)];
}

FText UEGT2FormatDate(const FUEGT2Date& Date)
{
	// 1st, 2nd, 3rd, 4th ... and 11th, 12th, 13th, which the simple rule gets
	// wrong and which a thirty-day month reaches every time.
	const int32 Day = Date.Day;
	const int32 LastTwo = Day % 100;
	const TCHAR* Suffix =
		(LastTwo >= 11 && LastTwo <= 13) ? TEXT("th")
		: (Day % 10) == 1 ? TEXT("st")
		: (Day % 10) == 2 ? TEXT("nd")
		: (Day % 10) == 3 ? TEXT("rd") : TEXT("th");

	return FText::Format(LOCTEXT("DateLine", "{0}, {1}{2} of {3}"),
		UEGT2WeekdayName(Date.Weekday),
		FText::AsNumber(Day),
		FText::FromString(Suffix),
		UEGT2MonthName(Date.Month));
}

FText UEGT2FormatClock(float Hour)
{
	const float Folded = FMath::Fmod(FMath::Fmod(Hour, 24.0f) + 24.0f, 24.0f);
	int32 Whole = FMath::FloorToInt(Folded);
	int32 Minutes = FMath::FloorToInt((Folded - Whole) * 60.0f);
	// Rounding 23:59.7 up must not produce 23:60.
	if (Minutes >= 60)
	{
		Minutes = 0;
		Whole = (Whole + 1) % 24;
	}
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Whole, Minutes));
}

FText UEGT2TimeOfDayName(float Hour)
{
	const float H = FMath::Fmod(FMath::Fmod(Hour, 24.0f) + 24.0f, 24.0f);
	if (H < 4.0f)  { return LOCTEXT("SmallHours", "the small hours"); }
	if (H < 6.5f)  { return LOCTEXT("Dawn", "dawn"); }
	if (H < 11.0f) { return LOCTEXT("Morning", "morning"); }
	if (H < 13.5f) { return LOCTEXT("Midday", "midday"); }
	if (H < 17.5f) { return LOCTEXT("Afternoon", "afternoon"); }
	if (H < 20.0f) { return LOCTEXT("Evening", "evening"); }
	if (H < 22.0f) { return LOCTEXT("Dusk", "dusk"); }
	return LOCTEXT("Night", "night");
}

// ---------------------------------------------------------------------------
float UEGT2TemperatureC(float WorldX, float WorldY, float AltitudeCm,
	float Hour, int32 DayIndex, EUEGT2Weather Weather)
{
	float Celsius = BaseC;

	// Where you are. West is tropical, north-east is the mountain range; the
	// two cannot both apply, because X cannot be in both places at once.
	if (WorldX < TropicsStartX)
	{
		const float T = FMath::Clamp(
			(WorldX - TropicsStartX) / (TropicsFullX - TropicsStartX), 0.0f, 1.0f);
		Celsius += TropicsWarmthC * T;
	}
	else if (WorldX > MountainStartX)
	{
		const float T = FMath::Clamp(
			(WorldX - MountainStartX) / (MountainFullX - MountainStartX), 0.0f, 1.0f);
		Celsius -= NorthernChillC * T;
	}

	// How high you are. This is the term that makes the peaks bite: at 440 m
	// the lapse rate alone is nearly three degrees, on top of the northern
	// chill that got you there.
	const float AltitudeKm = FMath::Max(AltitudeCm, 0.0f) / 100000.0f;
	Celsius -= LapseCPerKm * AltitudeKm;

	// The hour. Coldest around five, warmest around three in the afternoon,
	// which is a nine hour offset from a plain cosine.
	const float H = FMath::Fmod(FMath::Fmod(Hour, 24.0f) + 24.0f, 24.0f);
	Celsius += DiurnalSwingC * FMath::Cos((H - 15.0f) * (2.0f * PI / 24.0f));

	// The season. Day zero is the start of spring, so a plain sine puts the
	// warmest day a quarter of the year later - the middle of summer - and the
	// coldest three quarters in, rather than on new year's morning.
	const int32 DayOfYear = Fold(DayIndex, DaysPerYear);
	const float SeasonPhase = ((float)DayOfYear / (float)DaysPerYear) * 2.0f * PI;
	Celsius += SeasonSwingC * FMath::Sin(SeasonPhase);

	Celsius += WeatherOffsetC(Weather);
	return Celsius;
}

FText UEGT2FormatTemperature(float Celsius, bool bFahrenheit)
{
	const float Value = bFahrenheit ? (Celsius * 9.0f / 5.0f + 32.0f) : Celsius;
	return FText::FromString(FString::Printf(TEXT("%d%s"),
		FMath::RoundToInt(Value), bFahrenheit ? TEXT("°F") : TEXT("°C")));
}

#undef LOCTEXT_NAMESPACE
