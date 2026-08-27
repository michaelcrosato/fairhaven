// Fairhaven (UEGT2) - automation tests over the dev mode support tables.
//
// These cover the parts of dev mode that are pure functions over data, which is
// most of what can actually be got wrong silently: a weather preset edited into
// nonsense, a new enum entry added without a row, or a console name that no
// longer parses. Behaviour that needs a pawn and a sky is covered by
// Scripts/Smoke-Packaged.ps1 instead.
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "World/UEGT2Weather.h"

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2WeatherPresetsTest,
	"UEGT2.Dev.WeatherPresets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2WeatherPresetsTest::RunTest(const FString& Parameters)
{
	for (int32 Index = 0; Index < (int32)EUEGT2Weather::Count; ++Index)
	{
		const EUEGT2Weather Weather = (EUEGT2Weather)Index;
		const FUEGT2WeatherPreset& Preset = GetWeatherPreset(Weather);
		const FString Name = GetWeatherDisplayName(Weather).ToString();

		TestTrue(FString::Printf(TEXT("%s has a display name"), *Name), !Name.IsEmpty());

		// A zero sun scale would black the world out with no way back from the
		// menu; a scale above 1 would be brighter than noon.
		TestTrue(FString::Printf(TEXT("%s sun scale in (0,1]"), *Name),
			Preset.SunIntensityScale > 0.0f && Preset.SunIntensityScale <= 1.0f);
		TestTrue(FString::Printf(TEXT("%s sky scale in (0,1]"), *Name),
			Preset.SkyLightScale > 0.0f && Preset.SkyLightScale <= 1.0f);

		// The menu's fog slider tops out at 0.25, so a preset above that would
		// be unreachable once the player touched the slider.
		TestTrue(FString::Printf(TEXT("%s fog density in (0,0.25]"), *Name),
			Preset.FogDensity > 0.0f && Preset.FogDensity <= 0.25f);
		TestTrue(FString::Printf(TEXT("%s fog opacity in (0,1]"), *Name),
			Preset.FogMaxOpacity > 0.0f && Preset.FogMaxOpacity <= 1.0f);
		TestTrue(FString::Printf(TEXT("%s tint strengths in [0,1]"), *Name),
			Preset.SunTintStrength >= 0.0f && Preset.SunTintStrength <= 1.0f
			&& Preset.FogTintStrength >= 0.0f && Preset.FogTintStrength <= 1.0f);
		TestTrue(FString::Printf(TEXT("%s cloud deck above ground"), *Name),
			Preset.CloudBottomAltitudeKm > 0.0f);
	}
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2WeatherOrderingTest,
	"UEGT2.Dev.WeatherOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2WeatherOrderingTest::RunTest(const FString& Parameters)
{
	const FUEGT2WeatherPreset& Clear = GetWeatherPreset(EUEGT2Weather::Clear);
	const FUEGT2WeatherPreset& Overcast = GetWeatherPreset(EUEGT2Weather::Overcast);
	const FUEGT2WeatherPreset& Foggy = GetWeatherPreset(EUEGT2Weather::Foggy);
	const FUEGT2WeatherPreset& Storm = GetWeatherPreset(EUEGT2Weather::Storm);

	// The relationships that make the presets mean what they are called. A
	// table edit that inverts one of these compiles and looks fine until you
	// switch to it in game.
	TestTrue(TEXT("clear is the brightest"),
		Clear.SunIntensityScale >= Overcast.SunIntensityScale
		&& Clear.SunIntensityScale >= Storm.SunIntensityScale);
	TestTrue(TEXT("storm is darker than overcast"),
		Storm.SunIntensityScale < Overcast.SunIntensityScale);
	TestTrue(TEXT("foggy has the thickest fog"),
		Foggy.FogDensity > Clear.FogDensity && Foggy.FogDensity > Storm.FogDensity);
	TestTrue(TEXT("clear has the thinnest fog"),
		Clear.FogDensity < Overcast.FogDensity);
	TestTrue(TEXT("storm has the lowest cloud deck"),
		Storm.CloudBottomAltitudeKm < Clear.CloudBottomAltitudeKm);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2WeatherParseTest,
	"UEGT2.Dev.WeatherParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2WeatherParseTest::RunTest(const FString& Parameters)
{
	// uegt2.Weather takes the display name lowercased, so every entry has to
	// round-trip or the console command silently cannot reach it.
	for (int32 Index = 0; Index < (int32)EUEGT2Weather::Count; ++Index)
	{
		const EUEGT2Weather Expected = (EUEGT2Weather)Index;
		const FString Name = GetWeatherDisplayName(Expected).ToString();

		EUEGT2Weather Parsed = EUEGT2Weather::Count;
		if (TestTrue(FString::Printf(TEXT("'%s' parses"), *Name), ParseWeatherName(Name, Parsed)))
		{
			TestEqual(FString::Printf(TEXT("'%s' round-trips"), *Name), (int32)Parsed, Index);
		}

		EUEGT2Weather Upper = EUEGT2Weather::Count;
		TestTrue(FString::Printf(TEXT("'%s' parses case-insensitively"), *Name),
			ParseWeatherName(Name.ToUpper(), Upper) && Upper == Expected);
	}

	EUEGT2Weather Unused = EUEGT2Weather::Clear;
	TestFalse(TEXT("nonsense is rejected"), ParseWeatherName(TEXT("hurricane"), Unused));

	// Out of range clamps rather than reading off the end of the table.
	TestEqual(TEXT("out-of-range weather falls back to Clear"),
		GetWeatherPreset((EUEGT2Weather)99).FogDensity,
		GetWeatherPreset(EUEGT2Weather::Clear).FogDensity);
	return true;
}

// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2TeleportTargetsTest,
	"UEGT2.Dev.TeleportTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2TeleportTargetsTest::RunTest(const FString& Parameters)
{
	// The dev menu's Teleport tab is built straight off this list, so an empty
	// or duplicated tour would produce an empty or ambiguous tab.
	const TArray<FUEGT2Viewpoint>& Tour = UUEGT2CaptureSubsystem::GetTour();
	TestTrue(TEXT("the tour has viewpoints"), Tour.Num() > 0);

	TSet<FName> Seen;
	for (const FUEGT2Viewpoint& Point : Tour)
	{
		TestFalse(FString::Printf(TEXT("viewpoint %s is not a duplicate"), *Point.Name.ToString()),
			Seen.Contains(Point.Name));
		Seen.Add(Point.Name);

		TestTrue(FString::Printf(TEXT("viewpoint %s is named"), *Point.Name.ToString()),
			!Point.Name.IsNone());
		// The landscape is a 4.032 km square centred on the town, so it runs to
		// 201600 uu from centre. Anything past that is off the terrain and a
		// teleport there drops the player through the world.
		TestTrue(FString::Printf(TEXT("viewpoint %s is inside the world"), *Point.Name.ToString()),
			FMath::Abs(Point.Location.X) <= 195000.0f && FMath::Abs(Point.Location.Y) <= 195000.0f);
		TestTrue(FString::Printf(TEXT("viewpoint %s spawns above ground"), *Point.Name.ToString()),
			Point.HeightAboveGround > 0.0f);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
