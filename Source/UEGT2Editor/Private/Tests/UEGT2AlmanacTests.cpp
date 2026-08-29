// Fairhaven (UEGT2) - the date, the clock and the thermometer.
//
// The calendar has to agree with the town's own week, and the temperature has
// to move the way the land says it should: colder up the mountain, warmer in
// the west, colder at night and in a storm. A readout that says 24 degrees on a
// snowfield is worse than no readout at all, because it is believed.
#include "Misc/AutomationTest.h"
#include "NPC/UEGT2NPCTypes.h"
#include "World/UEGT2Almanac.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2CalendarTest,
	"UEGT2.World.Calendar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2CalendarTest::RunTest(const FString& Parameters)
{
	const FUEGT2Date First = UEGT2DateFromDayIndex(0);
	TestEqual(TEXT("day zero is the first"), First.Day, 1);
	TestEqual(TEXT("of the first month"), First.Month, 1);
	TestEqual(TEXT("of year one"), First.Year, 1);

	// The calendar's weekday must be the same one the routines use, or the HUD
	// says Marketday on a day the market is shut.
	for (int32 DayIndex = 0; DayIndex < 400; ++DayIndex)
	{
		const FUEGT2Date Date = UEGT2DateFromDayIndex(DayIndex);
		if (IsMarketDay(DayIndex))
		{
			TestEqual(TEXT("market day is the calendar's market day"),
				UEGT2WeekdayName(Date.Weekday).ToString(), FString(TEXT("Marketday")));
		}
		if (IsRestDay(DayIndex))
		{
			TestEqual(TEXT("rest day is the calendar's rest day"),
				UEGT2WeekdayName(Date.Weekday).ToString(), FString(TEXT("Restday")));
		}
		TestTrue(TEXT("the day is in range"), Date.Day >= 1 && Date.Day <= 30);
		TestTrue(TEXT("the month is in range"), Date.Month >= 1 && Date.Month <= 12);
		TestTrue(TEXT("the season is in range"), Date.Season >= 0 && Date.Season <= 3);
	}

	// A year rolls over, and does not roll back.
	TestEqual(TEXT("the last day of year one"), UEGT2DateFromDayIndex(359).Year, 1);
	TestEqual(TEXT("is followed by year two"), UEGT2DateFromDayIndex(360).Year, 2);
	TestEqual(TEXT("on its first day"), UEGT2DateFromDayIndex(360).Day, 1);

	// Ordinals: the eleventh to the thirteenth are the ones the simple rule
	// gets wrong, and a thirty-day month reaches them every time.
	TestTrue(TEXT("1st"), UEGT2FormatDate(UEGT2DateFromDayIndex(0)).ToString().Contains(TEXT("1st")));
	TestTrue(TEXT("2nd"), UEGT2FormatDate(UEGT2DateFromDayIndex(1)).ToString().Contains(TEXT("2nd")));
	TestTrue(TEXT("3rd"), UEGT2FormatDate(UEGT2DateFromDayIndex(2)).ToString().Contains(TEXT("3rd")));
	TestTrue(TEXT("11th"), UEGT2FormatDate(UEGT2DateFromDayIndex(10)).ToString().Contains(TEXT("11th")));
	TestTrue(TEXT("12th"), UEGT2FormatDate(UEGT2DateFromDayIndex(11)).ToString().Contains(TEXT("12th")));
	TestTrue(TEXT("13th"), UEGT2FormatDate(UEGT2DateFromDayIndex(12)).ToString().Contains(TEXT("13th")));
	TestTrue(TEXT("21st"), UEGT2FormatDate(UEGT2DateFromDayIndex(20)).ToString().Contains(TEXT("21st")));

	// The clock never shows a sixtieth minute or a twenty-fourth hour.
	TestEqual(TEXT("midnight"), UEGT2FormatClock(0.0f).ToString(), FString(TEXT("00:00")));
	TestEqual(TEXT("half past two"), UEGT2FormatClock(14.5f).ToString(), FString(TEXT("14:30")));
	TestEqual(TEXT("a whisker before midnight"),
		UEGT2FormatClock(23.99999f).ToString(), FString(TEXT("23:59")));
	TestEqual(TEXT("and it wraps rather than reading 24"),
		UEGT2FormatClock(24.0f).ToString(), FString(TEXT("00:00")));
	TestFalse(TEXT("no time of day is nameless"), UEGT2TimeOfDayName(3.0f).IsEmpty());
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2TemperatureTest,
	"UEGT2.World.Temperature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2TemperatureTest::RunTest(const FString& Parameters)
{
	// A fixed reference: the town square, sea level, mid-afternoon, midsummer,
	// clear. Everything else is compared against this.
	const int32 Midsummer = 90;   // a quarter of the way through a 360 day year
	const float Town = UEGT2TemperatureC(0.0f, 0.0f, 0.0f, 15.0f, Midsummer,
		EUEGT2Weather::Clear);
	TestTrue(TEXT("a summer afternoon in town is warm but not absurd"),
		Town > 18.0f && Town < 40.0f);

	// Height. The mountains top out at 440 m and the snow line is at 300.
	const float Summit = UEGT2TemperatureC(0.0f, 0.0f, 44000.0f, 15.0f, Midsummer,
		EUEGT2Weather::Clear);
	TestTrue(TEXT("it is colder 440 m up"), Summit < Town);

	// Distance. West is tropical, north-east is the mountain range.
	const float Tropics = UEGT2TemperatureC(-140000.0f, 0.0f, 0.0f, 15.0f, Midsummer,
		EUEGT2Weather::Clear);
	const float Uplands = UEGT2TemperatureC(150000.0f, 0.0f, 0.0f, 15.0f, Midsummer,
		EUEGT2Weather::Clear);
	TestTrue(TEXT("the west is warmer than the town"), Tropics > Town);
	TestTrue(TEXT("the north-east is colder"), Uplands < Town);

	// The hour.
	const float Night = UEGT2TemperatureC(0.0f, 0.0f, 0.0f, 4.0f, Midsummer,
		EUEGT2Weather::Clear);
	TestTrue(TEXT("four in the morning is colder than three in the afternoon"),
		Night < Town);

	// The season.
	const float Midwinter = UEGT2TemperatureC(0.0f, 0.0f, 0.0f, 15.0f, 270,
		EUEGT2Weather::Clear);
	TestTrue(TEXT("midwinter is colder than midsummer"), Midwinter < Town);

	// The weather.
	const float Storm = UEGT2TemperatureC(0.0f, 0.0f, 0.0f, 15.0f, Midsummer,
		EUEGT2Weather::Storm);
	TestTrue(TEXT("a storm is colder than a clear sky"), Storm < Town);

	// The extremes stay inside a range a thermometer would recognise.
	float Lowest = 1000.0f;
	float Highest = -1000.0f;
	for (int32 Day = 0; Day < 360; Day += 7)
	{
		for (int32 HourStep = 0; HourStep < 24; HourStep += 3)
		{
			for (int32 Weather = 0; Weather < (int32)EUEGT2Weather::Count; ++Weather)
			{
				const float Cold = UEGT2TemperatureC(168000.0f, 0.0f, 44000.0f,
					(float)HourStep, Day, (EUEGT2Weather)Weather);
				const float Hot = UEGT2TemperatureC(-150000.0f, 0.0f, 0.0f,
					(float)HourStep, Day, (EUEGT2Weather)Weather);
				Lowest = FMath::Min(Lowest, Cold);
				Highest = FMath::Max(Highest, Hot);
			}
		}
	}
	TestTrue(TEXT("the coldest place is below freezing"), Lowest < 0.0f);
	TestTrue(TEXT("but not absurdly so"), Lowest > -40.0f);
	TestTrue(TEXT("the hottest place is hot"), Highest > 25.0f);
	TestTrue(TEXT("but not absurdly so"), Highest < 55.0f);

	// Fahrenheit is Fahrenheit.
	TestEqual(TEXT("freezing in C"), UEGT2FormatTemperature(0.0f, false).ToString(),
		FString(TEXT("0°C")));
	TestEqual(TEXT("freezing in F"), UEGT2FormatTemperature(0.0f, true).ToString(),
		FString(TEXT("32°F")));
	TestEqual(TEXT("body heat in F"), UEGT2FormatTemperature(37.0f, true).ToString(),
		FString(TEXT("99°F")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
