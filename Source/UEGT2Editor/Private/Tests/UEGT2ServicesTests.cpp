#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Services/UEGT2ServicesSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"

#include <limits>

namespace UEGT2ServicesTests
{
	struct FSettingsScope
	{
		UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
		bool bServices = Settings && Settings->GetNearbyServicesEnabled();
		bool bSurvey = Settings && Settings->GetSurveyJournalEnabled();
		FSettingsScope()
		{
			if (Settings) { Settings->SetNearbyServicesEnabled(true); Settings->SetSurveyJournalEnabled(true); }
		}
		~FSettingsScope()
		{
			if (Settings) { Settings->SetNearbyServicesEnabled(bServices); Settings->SetSurveyJournalEnabled(bSurvey); }
		}
	};

	struct FWorld
	{
		UWorld* World = nullptr;
		UUEGT2ServicesSubsystem* Services = nullptr;
		UUEGT2SurveySubsystem* Survey = nullptr;
		AUEGT2PlayerController* Controller = nullptr;
		AUEGT2Character* Player = nullptr;

		explicit FWorld(EWorldType::Type Type = EWorldType::Game)
		{
			if (!GEngine) { return; }
			World = UWorld::CreateWorld(Type, false);
			if (!World) { return; }
			GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
			Services = UUEGT2ServicesSubsystem::Get(World);
			Survey = UUEGT2SurveySubsystem::Get(World);
			if (Services) { Services->bFeatureEnabled = true; }
			if (Survey) { Survey->bFeatureEnabled = true; }
			Controller = World->SpawnActor<AUEGT2PlayerController>();
			Player = World->SpawnActor<AUEGT2Character>(FVector(0, 0, 90), FRotator::ZeroRotator);
			if (Controller && Player)
			{
				Controller->Possess(Player);
				if (Services) { Player->DispatchBeginPlay(); }
			}
		}
		~FWorld()
		{
			if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		}
		bool Ready() const { return Services && Survey && Controller && Player; }
		AUEGT2Amenity* Add(EUEGT2AmenityKind Kind, const FVector& Point, const TCHAR* Venue = TEXT(""),
			EUEGT2NPCRole Role = EUEGT2NPCRole::Villager, FName ObjectName = NAME_None)
		{
			FActorSpawnParameters Params;
			Params.Name = ObjectName;
			AUEGT2Amenity* Amenity = World ? World->SpawnActor<AUEGT2Amenity>(Point, FRotator::ZeroRotator, Params) : nullptr;
			if (Amenity) { Amenity->ConfigureAmenity(Kind, Venue, Role); }
			return Amenity;
		}
		AUEGT2Landmark* Landmark(FName Id, bool bDiscovered = true)
		{
			AUEGT2Landmark* Result = World->SpawnActor<AUEGT2Landmark>(FVector(0, 20000, 0), FRotator::ZeroRotator);
			if (Result)
			{
				Result->PersistentId = Id;
				Result->SetLandmarkName(FText::FromString(TEXT("Test landmark")));
				Result->SetDiscovered(bDiscovered);
			}
			return Result;
		}
	};

	const FUEGT2ServiceEntry& Row(const TArray<FUEGT2ServiceEntry>& Entries, EUEGT2ServiceCategory Category)
	{
		return Entries[static_cast<int32>(Category)];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ServicesNearestTest, "UEGT2.Services.NearestAndRates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2ServicesNearestTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ServicesTests;
	FSettingsScope Settings;
	FWorld Sim;
	if (!TestTrue(TEXT("real guide world"), Sim.Ready())) { return false; }
	const TArray<FUEGT2ServiceEntry> Empty = Sim.Services->GetEntries(Sim.Controller);
	if (!TestEqual(TEXT("empty world still lists six categories"), Empty.Num(), 6)) { return false; }
	for (const FUEGT2ServiceEntry& Entry : Empty)
	{
		TestFalse(TEXT("missing category has no fabricated actor"), Entry.Amenity.IsValid());
		TestFalse(TEXT("missing category keeps semantic label"), Entry.CategoryName.IsEmpty());
	}
	AUEGT2Amenity* Tavern = Sim.Add(EUEGT2AmenityKind::Tavern, FVector(1000, 0, 0), TEXT("Test inn"));
	Sim.Add(EUEGT2AmenityKind::Food, FVector(2000, 0, 0), TEXT("Test bakery"));
	AUEGT2Amenity* Wash = Sim.Add(EUEGT2AmenityKind::Washroom, FVector(0, 3000, 0));
	AUEGT2Amenity* Seat = Sim.Add(EUEGT2AmenityKind::Seat, FVector(4000, 0, 0), TEXT("   "));
	AUEGT2Amenity* Market = Sim.Add(EUEGT2AmenityKind::Market, FVector(5000, 0, 0), TEXT("Market hiring"), EUEGT2NPCRole::Merchant);
	Sim.Add(EUEGT2AmenityKind::Work, FVector(6000, 0, 0), TEXT("Distant office"), EUEGT2NPCRole::Clerk);
	Sim.Add(EUEGT2AmenityKind::Market, FVector(100, 0, 0), TEXT("Shopping only"));
	AUEGT2Amenity* Larder = Sim.Add(EUEGT2AmenityKind::Larder, FVector(7000, 0, 0), TEXT("your lodgings"));
	AUEGT2Amenity* Bed = Sim.Add(EUEGT2AmenityKind::Bed, FVector(8000, 0, 0), TEXT("your lodgings"));
	Sim.Add(EUEGT2AmenityKind::Worship, FVector(1, 0, 0));
	Sim.Add(EUEGT2AmenityKind::Count, FVector(1, 0, 0));
	Sim.Add(EUEGT2AmenityKind::Food, FVector(1, 0, 0), TEXT("Invalid role"), EUEGT2NPCRole::Count);
	const TArray<FUEGT2ServiceEntry> Entries = Sim.Services->GetEntries(Sim.Controller);
	if (!TestEqual(TEXT("exact six fixed rows"), Entries.Num(), 6)) { return false; }
	TestEqual(TEXT("tavern participates in nearest food"), Row(Entries, EUEGT2ServiceCategory::Food).Amenity.Get(), Tavern);
	TestEqual(TEXT("washroom selected"), Row(Entries, EUEGT2ServiceCategory::Washroom).Amenity.Get(), Wash);
	TestEqual(TEXT("seat selected"), Row(Entries, EUEGT2ServiceCategory::Rest).Amenity.Get(), Seat);
	TestEqual(TEXT("paid market is work, shopping is not"), Row(Entries, EUEGT2ServiceCategory::PaidWork).Amenity.Get(), Market);
	TestEqual(TEXT("home food remains separate from nearer paid food"), Row(Entries, EUEGT2ServiceCategory::FoodAtHome).Amenity.Get(), Larder);
	TestEqual(TEXT("bed remains separate from nearer seat"), Row(Entries, EUEGT2ServiceCategory::Sleep).Amenity.Get(), Bed);
	TestEqual(TEXT("horizontal centimetres become metres"), Row(Entries, EUEGT2ServiceCategory::Food).DistanceMetres, 10.0f);
	TestEqual(TEXT("empty authored washroom has readable fallback"), Row(Entries, EUEGT2ServiceCategory::Washroom).Name.ToString(), FString(TEXT("Washroom")));
	TestEqual(TEXT("whitespace authored seat has readable fallback"), Row(Entries, EUEGT2ServiceCategory::Rest).Name.ToString(), FString(TEXT("Seat")));
	for (const FUEGT2ServiceEntry& Entry : Entries)
	{
		AUEGT2Amenity* Amenity = Entry.Amenity.Get();
		if (!TestNotNull(TEXT("available row has real actor"), Amenity)) { return false; }
		TestEqual(TEXT("cost comes from shared ledger"), Entry.CostPerHour, UEGT2PriceFor(Amenity->GetJobRole(), Amenity->GetActivity()));
		TestEqual(TEXT("wage comes from offered trade and activity"), Entry.WagePerHour, UEGT2WageFor(Amenity->GetJobRole(), Amenity->GetActivity()));
		TestEqual(TEXT("offered trade preserved"), Entry.JobRole, Amenity->GetJobRole());
	}
	TestEqual(TEXT("home food is free"), Row(Entries, EUEGT2ServiceCategory::FoodAtHome).CostPerHour, 0.0f);
	TestEqual(TEXT("bed is free"), Row(Entries, EUEGT2ServiceCategory::Sleep).CostPerHour, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ServicesTieTest, "UEGT2.Services.TiesAndExplicitRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2ServicesTieTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ServicesTests;
	FSettingsScope Settings;
	FWorld Sim;
	if (!TestTrue(TEXT("real guide world"), Sim.Ready())) { return false; }
	Sim.Add(EUEGT2AmenityKind::Tavern, FVector(3000, 4000, 0), TEXT("A tavern"));
	Sim.Add(EUEGT2AmenityKind::Food, FVector(3000, 4000, 0), TEXT("Zulu"));
	AUEGT2Amenity* Alpha = Sim.Add(EUEGT2AmenityKind::Food, FVector(-3000, 4000, 12000), TEXT("Alpha"));
	Sim.Add(EUEGT2AmenityKind::Food, FVector(3000, 4000, 0), TEXT("Alpha"));
	const auto Food = [&]() { return Row(Sim.Services->GetEntries(Sim.Controller), EUEGT2ServiceCategory::Food); };
	TestEqual(TEXT("equal distance uses kind then venue then position, not iteration order"), Food().Amenity.Get(), Alpha);
	TestEqual(TEXT("height is excluded from nearest distance"), Food().DistanceMetres, 50.0f);
	AUEGT2Amenity* NamedZ = Sim.Add(EUEGT2AmenityKind::Seat, FVector(1000, 0, 0), TEXT("Seat"), EUEGT2NPCRole::Villager, TEXT("Z_Seat"));
	AUEGT2Amenity* NamedA = Sim.Add(EUEGT2AmenityKind::Seat, FVector(1000, 0, 0), TEXT("Seat"), EUEGT2NPCRole::Villager, TEXT("A_Seat"));
	if (!TestNotNull(TEXT("named seat Z"), NamedZ) || !TestNotNull(TEXT("named seat A"), NamedA)) { return false; }
	TestEqual(TEXT("identical venue/position tie has stable final object-name order"), Row(Sim.Services->GetEntries(Sim.Controller), EUEGT2ServiceCategory::Rest).Amenity.Get(), NamedA);
	TestTrue(TEXT("selected actor tracked"), Sim.Services->TrackAmenity(Alpha));
	const FUEGT2ServiceEntry Before = Food();
	AUEGT2Amenity* Closer = Sim.Add(EUEGT2AmenityKind::Food, FVector(500, 0, 0), TEXT("New nearest"));
	TestEqual(TEXT("existing row snapshot is fixed"), Before.Amenity.Get(), Alpha);
	TestEqual(TEXT("active target does not silently switch"), Sim.Services->GetTrackedAmenity(), Alpha);
	TestEqual(TEXT("explicit refresh sees newly nearest actor"), Food().Amenity.Get(), Closer);
	TestEqual(TEXT("refresh never selects on player's behalf"), Sim.Services->GetTrackedAmenity(), Alpha);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ServicesLifecycleTest, "UEGT2.Services.LifecycleAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2ServicesLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ServicesTests;
	FSettingsScope Settings;
	FWorld First, Second;
	if (!TestTrue(TEXT("two service worlds"), First.Ready() && Second.Ready())) { return false; }
	AUEGT2Amenity* North = First.Add(EUEGT2AmenityKind::Food, FVector(2000, 0, 0), TEXT("North"));
	AUEGT2Amenity* East = Second.Add(EUEGT2AmenityKind::Food, FVector(0, 2000, 0), TEXT("East"));
	if (!TestNotNull(TEXT("north actor"), North) || !TestNotNull(TEXT("east actor"), East)) { return false; }
	TestTrue(TEXT("first world selects own actor"), First.Services->TrackAmenity(North));
	TestFalse(TEXT("foreign actor rejected"), First.Services->TrackAmenity(East));
	TestFalse(TEXT("null actor rejected"), First.Services->TrackAmenity(nullptr));
	TestEqual(TEXT("invalid choices preserve current target"), First.Services->GetTrackedAmenity(), North);
	TestEqual(TEXT("foreign controller cannot scan this world"), First.Services->GetEntries(Second.Controller).Num(), 0);
	TestEqual(TEXT("null controller cannot scan"), First.Services->GetEntries(nullptr).Num(), 0);
	TestTrue(TEXT("second world tracks independently"), Second.Services->TrackAmenity(East));
	FUEGT2SurveyDirection Direction;
	TestTrue(TEXT("service direction reuses horizontal compass"), First.Services->GetTrackedDirection(FVector::ZeroVector, 90, Direction));
	TestEqual(TEXT("north target relative to east view"), Direction.RelativeBearingDegrees, -90.0f);
	TestEqual(TEXT("direction uses venue name"), Direction.Name.ToString(), FString(TEXT("North")));
	TestTrue(TEXT("no invented persistent amenity ID"), Direction.Id.IsNone());
	TestFalse(TEXT("nonfinite origin rejected"), First.Services->GetTrackedDirection(FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 0, Direction));
	TestTrue(TEXT("failed query clears stale name"), Direction.Name.IsEmpty());
	TestFalse(TEXT("nonfinite yaw rejected"), First.Services->GetTrackedDirection(FVector::ZeroVector, std::numeric_limits<float>::infinity(), Direction));
	TestEqual(TEXT("bad observer inputs do not replace valid target"), First.Services->GetTrackedAmenity(), North);
	North->ConfigureAmenity(EUEGT2AmenityKind::Bed, TEXT("North"), EUEGT2NPCRole::Villager);
	TestNull(TEXT("changed kind drops cached selection"), First.Services->GetTrackedAmenity());
	North->ConfigureAmenity(EUEGT2AmenityKind::Food, TEXT("North"), EUEGT2NPCRole::Villager);
	TestNull(TEXT("reconfiguring back does not resurrect target"), First.Services->GetTrackedAmenity());
	TestTrue(TEXT("explicit selection after reconfigure"), First.Services->TrackAmenity(North));
	North->ConfigureAmenity(EUEGT2AmenityKind::Food, TEXT("Different venue"), EUEGT2NPCRole::Villager);
	TestNull(TEXT("changed venue drops old selection"), First.Services->GetTrackedAmenity());
	TestTrue(TEXT("new venue can be deliberately selected"), First.Services->TrackAmenity(North));
	North->ConfigureAmenity(EUEGT2AmenityKind::Food, TEXT("Different venue"), EUEGT2NPCRole::Baker);
	TestNull(TEXT("changed offered role drops old selection"), First.Services->GetTrackedAmenity());
	TestTrue(TEXT("select before destruction"), First.Services->TrackAmenity(North));
	TestTrue(TEXT("selected actor destroyed"), North->Destroy());
	TestFalse(TEXT("destroyed target cannot give direction"), First.Services->GetTrackedDirection(FVector::ZeroVector, 0, Direction));
	TestEqual(TEXT("other world retains target"), Second.Services->GetTrackedAmenity(), East);
	First.Controller->UnPossess();
	TestEqual(TEXT("unpossessed controller cannot scan"), First.Services->GetEntries(First.Controller).Num(), 0);
	FWorld Preview(EWorldType::EditorPreview);
	TestNull(TEXT("no guide subsystem in editor preview"), Preview.Services);
	TestNull(TEXT("null world has no guide"), UUEGT2ServicesSubsystem::Get(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEGT2ServicesHandoffTest, "UEGT2.Services.HandoffAndDisabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUEGT2ServicesHandoffTest::RunTest(const FString& Parameters)
{
	using namespace UEGT2ServicesTests;
	FSettingsScope Settings;
	FWorld Sim;
	if (!TestTrue(TEXT("guide world and settings"), Settings.Settings && Sim.Ready())) { return false; }
	AUEGT2Amenity* Food = Sim.Add(EUEGT2AmenityKind::Food, FVector(3000, 0, 0), TEXT("Test counter"));
	AUEGT2Landmark* Square = Sim.Landmark(TEXT("square"));
	AUEGT2Landmark* Hidden = Sim.Landmark(TEXT("hidden"), false);
	if (!TestNotNull(TEXT("food"), Food) || !TestNotNull(TEXT("surveyed landmark"), Square) || !TestNotNull(TEXT("unsurveyed landmark"), Hidden)) { return false; }
	FUEGT2NPCNeeds Needs;
	Needs.Energy = 0.73f; Needs.Fed = 0.42f; Needs.Relief = 0.61f; Needs.Company = 0.28f;
	if (!TestTrue(TEXT("exact life fixture restored"), Sim.Player->GetLife()->RestoreProgress(Needs,
		FUEGT2Purse(137.625f), EUEGT2NPCRole::Smith))) { return false; }
	TestTrue(TEXT("survey first"), Sim.Survey->TrackLandmark(TEXT("square")));
	TestFalse(TEXT("invalid service does not take over"), Sim.Services->TrackAmenity(nullptr));
	TestEqual(TEXT("survey survives rejected handoff"), Sim.Survey->GetTrackedLandmarkId(), FName(TEXT("square")));
	TestTrue(TEXT("valid service takes over"), Sim.Services->TrackAmenity(Food));
	TestTrue(TEXT("service selection retires survey direction"), Sim.Survey->GetTrackedLandmarkId().IsNone());
	TestFalse(TEXT("undiscovered survey target cannot take over"), Sim.Survey->TrackLandmark(TEXT("hidden")));
	TestEqual(TEXT("service survives rejected handoff"), Sim.Services->GetTrackedAmenity(), Food);
	TestTrue(TEXT("valid survey takes over"), Sim.Survey->TrackLandmark(TEXT("square")));
	TestNull(TEXT("survey selection retires service direction"), Sim.Services->GetTrackedAmenity());
	TestTrue(TEXT("select service for off cycle"), Sim.Services->TrackAmenity(Food));
	// No HUD query happens while the paused settings page is visible. The real
	// settings-applied event must retire the target before the player turns it on.
	Settings.Settings->SetNearbyServicesEnabled(false);
	UUEGT2GameUserSettings::OnSettingsApplied.Broadcast();
	Settings.Settings->SetNearbyServicesEnabled(true);
	TestNull(TEXT("services off/on without intermediate query cannot resurrect"), Sim.Services->GetTrackedAmenity());
	TestTrue(TEXT("select survey for same off cycle"), Sim.Survey->TrackLandmark(TEXT("square")));
	Settings.Settings->SetSurveyJournalEnabled(false);
	UUEGT2GameUserSettings::OnSettingsApplied.Broadcast();
	Settings.Settings->SetSurveyJournalEnabled(true);
	TestTrue(TEXT("survey off/on without intermediate query cannot resurrect"), Sim.Survey->GetTrackedLandmarkId().IsNone());
	TestTrue(TEXT("select before hard gate"), Sim.Services->TrackAmenity(Food));
	Sim.Services->bFeatureEnabled = false;
	TestFalse(TEXT("hard gate availability also retires tracking"), Sim.Services->IsAvailable());
	Sim.Services->bFeatureEnabled = true;
	TestNull(TEXT("availability off/on cannot resurrect"), Sim.Services->GetTrackedAmenity());
	Sim.Services->bFeatureEnabled = false;
	TestFalse(TEXT("hard-off effective query retires target"), Sim.Services->IsEnabled());
	TestEqual(TEXT("hard-off has no entries"), Sim.Services->GetEntries(Sim.Controller).Num(), 0);
	TestFalse(TEXT("hard-off refuses selection"), Sim.Services->TrackAmenity(Food));
	FUEGT2SurveyDirection Direction;
	TestFalse(TEXT("hard-off has no directions"), Sim.Services->GetTrackedDirection(FVector::ZeroVector, 0, Direction));
	TestTrue(TEXT("survey still works while services hard-off"), Sim.Survey->TrackLandmark(TEXT("square")));
	TestFalse(TEXT("disabled service cannot clear survey"), Sim.Services->TrackAmenity(Food));
	TestEqual(TEXT("disabled handoff preserves other target"), Sim.Survey->GetTrackedLandmarkId(), FName(TEXT("square")));
	Sim.Services->bFeatureEnabled = true;
	TestNull(TEXT("hard gate on never resurrects target"), Sim.Services->GetTrackedAmenity());
	Sim.Survey->bFeatureEnabled = false;
	TestTrue(TEXT("service independent of survey gate"), Sim.Services->TrackAmenity(Food));
	TestFalse(TEXT("disabled survey cannot take over"), Sim.Survey->TrackLandmark(TEXT("square")));
	TestEqual(TEXT("service survives disabled survey attempt"), Sim.Services->GetTrackedAmenity(), Food);
	Sim.Services->ClearTracking(); Sim.Services->ClearTracking();
	TestNull(TEXT("clear is idempotent"), Sim.Services->GetTrackedAmenity());
	const FUEGT2NPCNeeds& After = Sim.Player->GetLife()->GetNeeds();
	TestTrue(TEXT("all guide operations preserve exact needs"), After.Energy == Needs.Energy && After.Fed == Needs.Fed
		&& After.Relief == Needs.Relief && After.Company == Needs.Company);
	TestEqual(TEXT("guide operations preserve fractional coins"), Sim.Player->GetLife()->GetPurse().Coins, 137.625f);
	TestEqual(TEXT("guide operations preserve current trade"), Sim.Player->GetLife()->GetTrade(), EUEGT2NPCRole::Smith);
	TestTrue(TEXT("guide operations preserve discovery"), Square->IsDiscovered());
	TestFalse(TEXT("guide operations never discover places"), Hidden->IsDiscovered());
	return true;
}

#endif
