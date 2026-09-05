#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"

#include <limits>

namespace UEGT2SurveyTests
{
	struct FSettingsScope
	{
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bOriginal = Settings && Settings->GetSurveyJournalEnabled();
		FSettingsScope() { if (Settings) { Settings->SetSurveyJournalEnabled(true); } }
		~FSettingsScope() { if (Settings) { Settings->SetSurveyJournalEnabled(bOriginal); } }
	};

	struct FWorld
	{
		UWorld* World = nullptr;
		UUEGT2SurveySubsystem* Survey = nullptr;

		explicit FWorld(EWorldType::Type Type = EWorldType::Game)
		{
			if (!GEngine) { return; }
			World = UWorld::CreateWorld(Type, false);
			if (World)
			{
				GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
				Survey = UUEGT2SurveySubsystem::Get(World);
				if (Survey) { Survey->bFeatureEnabled = true; }
			}
		}

		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				GEngine->DestroyWorldContext(World);
			}
		}

		AUEGT2Landmark* Add(FName Id, const TCHAR* Name, bool bDiscovered,
			const FVector& Location = FVector(10000, 0, 0))
		{
			AUEGT2Landmark* Landmark = World ? World->SpawnActor<AUEGT2Landmark>(Location, FRotator::ZeroRotator) : nullptr;
			if (Landmark)
			{
				Landmark->PersistentId = Id;
				Landmark->SetLandmarkName(FText::FromString(Name));
				Landmark->DispatchBeginPlay();
				Landmark->SetDiscovered(bDiscovered);
			}
			return Landmark;
		}
	};

	FVector AtBearing(double Degrees)
	{
		const double Radians = FMath::DegreesToRadians(Degrees);
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0) * 10000.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SurveyDirectionTest, "UEGT2.Survey.Direction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SurveyDirectionTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyTests;
	const TCHAR* Compass[] = { TEXT("N"), TEXT("NE"), TEXT("E"), TEXT("SE"), TEXT("S"), TEXT("SW"), TEXT("W"), TEXT("NW") };
	FUEGT2SurveyDirection Direction;
	const FVector Origin(1234, -5678, 200);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Compass); ++Index)
	{
		const float Degrees = Index * 45.0f;
		TestTrue(TEXT("finite cardinal and diagonal positions produce directions"),
			UUEGT2SurveySubsystem::CalculateDirection(Origin, Origin + AtBearing(Degrees) + FVector(0, 0, 9000), Degrees, Direction));
		TestTrue(TEXT("bearing respects north +X / east +Y"), FMath::IsNearlyEqual(Direction.BearingDegrees, Degrees, 0.001f));
		TestEqual(TEXT("eight compass labels"), Direction.CompassDirection.ToString(), FString(Compass[Index]));
		TestTrue(TEXT("centimetres become horizontal metres"), FMath::IsNearlyEqual(Direction.DistanceMetres, 100.0f, 0.001f));
		TestTrue(TEXT("looking at target has zero relative bearing"), FMath::IsNearlyZero(Direction.RelativeBearingDegrees, 0.001f));
		TestFalse(TEXT("100m is not nearby"), Direction.bNearby);
	}
	// Each sector includes its counterclockwise edge and excludes its clockwise
	// edge. Probe just either side to avoid trig roundoff at an exact half angle.
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Compass); ++Index)
	{
		const double Edge = Index * 45.0 + 22.5;
		UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, AtBearing(Edge - 0.001), 0, Direction);
		TestEqual(TEXT("below compass boundary"), Direction.CompassDirection.ToString(), FString(Compass[Index]));
		UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, AtBearing(Edge + 0.001), 0, Direction);
		TestEqual(TEXT("above compass boundary"), Direction.CompassDirection.ToString(), FString(Compass[(Index + 1) % 8]));
	}
	struct FRelativeCase { float Target; float View; float Expected; };
	for (const FRelativeCase& Case : { FRelativeCase{ 10, 350, 20 }, FRelativeCase{ 350, 10, -20 },
		FRelativeCase{ 90, -270, 0 }, FRelativeCase{ 90, 810, 0 }, FRelativeCase{ 180, 0, -180 },
		FRelativeCase{ 0, -180, -180 } })
	{
		UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, AtBearing(Case.Target), Case.View, Direction);
		TestTrue(TEXT("relative yaw wraps across north and repeated turns"),
			FMath::IsNearlyEqual(Direction.RelativeBearingDegrees, Case.Expected, 0.001f));
	}
	UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, AtBearing(359.999999), 0, Direction);
	TestTrue(TEXT("float rounding preserves bearing range"), Direction.BearingDegrees >= 0.0f && Direction.BearingDegrees < 360.0f);
	UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, AtBearing(179.999999), 0, Direction);
	TestTrue(TEXT("float rounding preserves relative range"), Direction.RelativeBearingDegrees >= -180.0f && Direction.RelativeBearingDegrees < 180.0f);
	UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, FVector(0, 1000, 0), -120, Direction);
	TestTrue(TEXT("exactly ten metres is nearby"), Direction.bNearby);
	TestEqual(TEXT("nearby suppresses unstable bearings"), Direction.BearingDegrees, 0.0f);
	TestEqual(TEXT("nearby suppresses relative arrow"), Direction.RelativeBearingDegrees, 0.0f);
	TestEqual(TEXT("nearby label"), Direction.CompassDirection.ToString(), FString(TEXT("Nearby")));
	TestTrue(TEXT("coincident XY is a valid nearby destination"),
		UUEGT2SurveySubsystem::CalculateDirection(Origin, Origin + FVector(0, 0, 5000), 91, Direction));
	TestEqual(TEXT("height does not invent walking distance"), Direction.DistanceMetres, 0.0f);
	TestTrue(TEXT("coincident point nearby"), Direction.bNearby);
	UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, FVector(0, 1000.1, 0), 0, Direction);
	TestFalse(TEXT("beyond ten metres resumes directions"), Direction.bNearby);
	TestEqual(TEXT("direction returns outside nearby range"), Direction.BearingDegrees, 90.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SurveyInvalidDirectionTest, "UEGT2.Survey.InvalidDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SurveyInvalidDirectionTest::RunTest(const FString& Parameters)
{
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();
	FUEGT2SurveyDirection Direction;
	for (const FVector& Bad : { FVector(NaN, 0, 0), FVector(0, Infinity, 0), FVector(0, 0, NaN) })
	{
		Direction.Id = TEXT("stale"); Direction.Name = FText::FromString(TEXT("Stale")); Direction.DistanceMetres = 88;
		TestFalse(TEXT("invalid origin rejected"), UUEGT2SurveySubsystem::CalculateDirection(Bad, FVector::ZeroVector, 0, Direction));
		TestTrue(TEXT("failure clears stale identity"), Direction.Id.IsNone());
		TestTrue(TEXT("failure clears stale name"), Direction.Name.IsEmpty());
		TestEqual(TEXT("failure clears stale distance"), Direction.DistanceMetres, 0.0f);
		TestFalse(TEXT("invalid target rejected"), UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector, Bad, 0, Direction));
	}
	TestFalse(TEXT("invalid yaw rejected"), UUEGT2SurveySubsystem::CalculateDirection(FVector::ZeroVector,
		FVector(10000, 0, 0), std::numeric_limits<float>::infinity(), Direction));
	TestFalse(TEXT("overflowing finite coordinate difference rejected"), UUEGT2SurveySubsystem::CalculateDirection(
		FVector(-MAX_dbl, 0, 0), FVector(MAX_dbl, 0, 0), 0, Direction));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SurveyRosterTest, "UEGT2.Survey.RosterAndTracking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SurveyRosterTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyTests;
	FSettingsScope Settings;
	FWorld Sim;
	if (!TestNotNull(TEXT("game world survey subsystem"), Sim.Survey)) { return false; }
	AUEGT2Landmark* Square = Sim.Add(TEXT("square"), TEXT("Town Square"), true);
	AUEGT2Landmark* Harbour = Sim.Add(TEXT("harbour"), TEXT("Harbour"), false, FVector(0, 20000, 0));
	AUEGT2Landmark* Unnamed = Sim.Add(NAME_None, TEXT("Missing identity"), true);
	if (!TestNotNull(TEXT("square"), Square) || !TestNotNull(TEXT("harbour"), Harbour) || !TestNotNull(TEXT("unnamed"), Unnamed)) { return false; }
	TArray<FUEGT2SurveyEntry> Entries = Sim.Survey->GetEntries();
	if (!TestEqual(TEXT("only authored identities enter roster"), Entries.Num(), 2)) { return false; }
	TestEqual(TEXT("roster sorted by readable name"), Entries[0].Id, FName(TEXT("harbour")));
	TestFalse(TEXT("unsurveyed entry mirrors live state"), Entries[0].bDiscovered);
	TestTrue(TEXT("surveyed entry mirrors live state"), Entries[1].bDiscovered);
	TestFalse(TEXT("undiscovered landmark cannot be tracked"), Sim.Survey->TrackLandmark(TEXT("harbour")));
	TestFalse(TEXT("empty ID cannot be tracked"), Sim.Survey->TrackLandmark(NAME_None));
	TestFalse(TEXT("unknown ID cannot be tracked"), Sim.Survey->TrackLandmark(TEXT("unknown")));
	TestTrue(TEXT("discovered landmark can be tracked"), Sim.Survey->TrackLandmark(TEXT("square")));
	TestTrue(TEXT("repeated tracking is idempotent"), Sim.Survey->TrackLandmark(TEXT("square")));
	TestFalse(TEXT("rejected selection does not replace target"), Sim.Survey->TrackLandmark(TEXT("harbour")));
	TestEqual(TEXT("valid target retained"), Sim.Survey->GetTrackedLandmarkId(), FName(TEXT("square")));
	FUEGT2SurveyDirection Direction;
	TestTrue(TEXT("live tracked direction"), Sim.Survey->GetTrackedDirection(FVector::ZeroVector, 90, Direction));
	TestEqual(TEXT("direction carries stable ID"), Direction.Id, FName(TEXT("square")));
	TestEqual(TEXT("direction carries current name"), Direction.Name.ToString(), FString(TEXT("Town Square")));
	TestEqual(TEXT("square is north of origin"), Direction.CompassDirection.ToString(), FString(TEXT("N")));
	Square->SetLandmarkName(FText::FromString(TEXT("Renamed Square")));
	Sim.Survey->GetTrackedDirection(FVector::ZeroVector, 0, Direction);
	TestEqual(TEXT("name updates without a roster scan"), Direction.Name.ToString(), FString(TEXT("Renamed Square")));

	// A newly introduced duplicate invalidates the tracked ID on the next
	// explicit roster refresh, with no permanent registry or per-frame scan.
	AUEGT2Landmark* Duplicate = Sim.Add(TEXT("square"), TEXT("Duplicate Square"), true);
	if (!TestNotNull(TEXT("duplicate actor"), Duplicate)) { return false; }
	TestEqual(TEXT("ambiguous identities omitted completely"), Sim.Survey->GetEntries().Num(), 1);
	TestTrue(TEXT("refresh drops ambiguous tracked ID"), Sim.Survey->GetTrackedLandmarkId().IsNone());
	TestFalse(TEXT("ambiguous ID cannot be selected"), Sim.Survey->TrackLandmark(TEXT("square")));
	TestTrue(TEXT("duplicate can be removed"), Duplicate->Destroy());
	TestTrue(TEXT("unique ID becomes trackable again"), Sim.Survey->TrackLandmark(TEXT("square")));
	Sim.Survey->ClearTracking(); Sim.Survey->ClearTracking();
	TestTrue(TEXT("clear is idempotent"), Sim.Survey->GetTrackedLandmarkId().IsNone());
	TestTrue(TEXT("clearing directions preserves discovery"), Square->IsDiscovered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SurveyLifecycleTest, "UEGT2.Survey.LifecycleAndIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SurveyLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyTests;
	FSettingsScope Settings;
	FWorld First, Second;
	if (!TestNotNull(TEXT("first service"), First.Survey) || !TestNotNull(TEXT("second service"), Second.Survey)) { return false; }
	AUEGT2Landmark* North = First.Add(TEXT("square"), TEXT("First square"), true, FVector(10000, 0, 0));
	AUEGT2Landmark* East = Second.Add(TEXT("square"), TEXT("Second square"), false, FVector(0, 10000, 0));
	if (!TestNotNull(TEXT("north landmark"), North) || !TestNotNull(TEXT("east landmark"), East)) { return false; }
	TestTrue(TEXT("first world selects its own surveyed place"), First.Survey->TrackLandmark(TEXT("square")));
	TestFalse(TEXT("second world cannot inherit discovery"), Second.Survey->TrackLandmark(TEXT("square")));
	East->SetDiscovered(true);
	TestTrue(TEXT("checkpoint-restored discovery is immediately trackable"), Second.Survey->TrackLandmark(TEXT("square")));
	FUEGT2SurveyDirection Direction;
	Second.Survey->GetTrackedDirection(FVector::ZeroVector, 0, Direction);
	TestEqual(TEXT("same ID resolves inside its own world"), Direction.BearingDegrees, 90.0f);
	North->SetDiscovered(false);
	TestFalse(TEXT("discovery reset drops stale directions without roster refresh"), First.Survey->GetTrackedDirection(FVector::ZeroVector, 0, Direction));
	TestTrue(TEXT("reset drops tracked ID"), First.Survey->GetTrackedLandmarkId().IsNone());
	North->SetDiscovered(true);
	TestTrue(TEXT("restoring discovery does not automatically restart tracking"), First.Survey->GetTrackedLandmarkId().IsNone());
	TestTrue(TEXT("restored discovery appears in roster"), First.Survey->GetEntries()[0].bDiscovered);
	TestTrue(TEXT("player may explicitly select restored discovery"), First.Survey->TrackLandmark(TEXT("square")));
	North->PersistentId = TEXT("changed_id");
	TestTrue(TEXT("identity change drops stale reference"), First.Survey->GetTrackedLandmarkId().IsNone());
	TestTrue(TEXT("new identity can be selected"), First.Survey->TrackLandmark(TEXT("changed_id")));
	TestTrue(TEXT("tracked actor can be destroyed"), North->Destroy());
	TestFalse(TEXT("destroyed actor cannot produce direction"), First.Survey->GetTrackedDirection(FVector::ZeroVector, 0, Direction));
	TestEqual(TEXT("remaining world keeps its own target"), Second.Survey->GetTrackedLandmarkId(), FName(TEXT("square")));
	TestTrue(TEXT("remaining world keeps its discovery"), East->IsDiscovered());
	FWorld Preview(EWorldType::EditorPreview);
	TestNull(TEXT("journal does not exist in editor preview worlds"), Preview.Survey);
	TestNull(TEXT("null world has no journal"), UUEGT2SurveySubsystem::Get(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2SurveyDisabledTest, "UEGT2.Survey.Disabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUEGT2SurveyDisabledTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2SurveyTests;
	FSettingsScope Settings;
	FWorld Sim;
	if (!TestNotNull(TEXT("settings"), Settings.Settings) || !TestNotNull(TEXT("survey service"), Sim.Survey)) { return false; }
	AUEGT2Landmark* Landmark = Sim.Add(TEXT("square"), TEXT("Square"), true);
	if (!TestNotNull(TEXT("discovered landmark"), Landmark)) { return false; }
	TestTrue(TEXT("target selected before disabling"), Sim.Survey->TrackLandmark(TEXT("square")));
	const auto CheckDisabled = [&]()
	{
		TestFalse(TEXT("effective feature gate off"), Sim.Survey->IsEnabled());
		TestEqual(TEXT("disabled journal has no entries"), Sim.Survey->GetEntries().Num(), 0);
		TestFalse(TEXT("disabled journal refuses tracking"), Sim.Survey->TrackLandmark(TEXT("square")));
		TestTrue(TEXT("disabled journal hides and clears target"), Sim.Survey->GetTrackedLandmarkId().IsNone());
		FUEGT2SurveyDirection Direction;
		TestFalse(TEXT("disabled journal has no HUD directions"), Sim.Survey->GetTrackedDirection(FVector::ZeroVector, 0, Direction));
		TestTrue(TEXT("disabled journal preserves discovery"), Landmark->IsDiscovered());
		TestEqual(TEXT("disabled journal preserves discovery count"), AUEGT2Landmark::GetDiscoveredCount(Sim.World), 1);
	};
	Settings.Settings->SetSurveyJournalEnabled(false);
	TestTrue(TEXT("player can turn its preference back on"), Sim.Survey->IsAvailable());
	CheckDisabled();
	Settings.Settings->SetSurveyJournalEnabled(true);
	TestTrue(TEXT("turning feature on does not silently resume tracking"), Sim.Survey->GetTrackedLandmarkId().IsNone());
	TestEqual(TEXT("re-enabled journal reflects unchanged discoveries"), Sim.Survey->GetEntries().Num(), 1);
	TestTrue(TEXT("player can choose a target again"), Sim.Survey->TrackLandmark(TEXT("square")));
	Sim.Survey->bFeatureEnabled = false;
	TestFalse(TEXT("player cannot override maintainer gate"), Sim.Survey->IsAvailable());
	CheckDisabled();
	Sim.Survey->bFeatureEnabled = true;
	TestTrue(TEXT("maintainer gate can restore availability"), Sim.Survey->IsEnabled());
	TestTrue(TEXT("discovery remains after both gate cycles"), Sim.Survey->GetEntries()[0].bDiscovered);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
