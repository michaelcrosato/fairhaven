#include "Services/UEGT2ServicesSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Survey/UEGT2SurveySubsystem.h"
#include "UEGT2LogChannels.h"

#define LOCTEXT_NAMESPACE "UEGT2Services"

namespace UEGT2Services
{
	constexpr int32 CategoryCount = static_cast<int32>(EUEGT2ServiceCategory::Count);
	bool IsFinite(const FVector& Point)
	{
		return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z);
	}
	FText CategoryName(EUEGT2ServiceCategory Category)
	{
		switch (Category)
		{
		case EUEGT2ServiceCategory::Food: return LOCTEXT("Food", "Food");
		case EUEGT2ServiceCategory::Washroom: return LOCTEXT("Washroom", "Washroom");
		case EUEGT2ServiceCategory::Rest: return LOCTEXT("Rest", "Rest");
		case EUEGT2ServiceCategory::PaidWork: return LOCTEXT("PaidWork", "Paid work");
		case EUEGT2ServiceCategory::FoodAtHome: return LOCTEXT("FoodAtHome", "Food at home");
		case EUEGT2ServiceCategory::Sleep: return LOCTEXT("Sleep", "Sleep");
		default: return FText::GetEmpty();
		}
	}
	FText VenueName(const AUEGT2Amenity& Amenity)
	{
		const FText Authored = Amenity.GetVenueName();
		if (!Authored.IsEmptyOrWhitespace()) { return Authored; }
		switch (Amenity.GetKind())
		{
		case EUEGT2AmenityKind::Food: return LOCTEXT("FoodCounter", "Food counter");
		case EUEGT2AmenityKind::Tavern: return LOCTEXT("Tavern", "Tavern");
		case EUEGT2AmenityKind::Washroom: return LOCTEXT("WashroomVenue", "Washroom");
		case EUEGT2AmenityKind::Seat: return LOCTEXT("Seat", "Seat");
		case EUEGT2AmenityKind::Work: return LOCTEXT("Workplace", "Workplace");
		case EUEGT2AmenityKind::Market: return LOCTEXT("MarketStall", "Market stall");
		case EUEGT2AmenityKind::Larder: return LOCTEXT("HomeKitchen", "Home kitchen");
		case EUEGT2AmenityKind::Bed: return LOCTEXT("Bed", "Bed");
		default: return FText::GetEmpty();
		}
	}
	bool Describe(const UWorld* World, AUEGT2Amenity* Amenity, FUEGT2ServiceEntry& Out)
	{
		if (!IsValid(Amenity) || Amenity->IsActorBeingDestroyed() || Amenity->GetWorld() != World
			|| !IsFinite(Amenity->GetActorLocation())
			|| static_cast<uint8>(Amenity->GetJobRole()) >= static_cast<uint8>(EUEGT2NPCRole::Count)) { return false; }
		Out.Activity = Amenity->GetActivity();
		Out.JobRole = Amenity->GetJobRole();
		Out.CostPerHour = UEGT2PriceFor(Out.JobRole, Out.Activity);
		Out.WagePerHour = UEGT2WageFor(Out.JobRole, Out.Activity);
		if (!FMath::IsFinite(Out.CostPerHour) || !FMath::IsFinite(Out.WagePerHour)
			|| Out.CostPerHour < 0.0f || Out.WagePerHour < 0.0f) { return false; }
		switch (Amenity->GetKind())
		{
		case EUEGT2AmenityKind::Food:
		case EUEGT2AmenityKind::Tavern: Out.Category = EUEGT2ServiceCategory::Food; break;
		case EUEGT2AmenityKind::Washroom: Out.Category = EUEGT2ServiceCategory::Washroom; break;
		case EUEGT2AmenityKind::Seat: Out.Category = EUEGT2ServiceCategory::Rest; break;
		case EUEGT2AmenityKind::Larder: Out.Category = EUEGT2ServiceCategory::FoodAtHome; break;
		case EUEGT2AmenityKind::Bed: Out.Category = EUEGT2ServiceCategory::Sleep; break;
		case EUEGT2AmenityKind::Work:
		case EUEGT2AmenityKind::Market:
			if (Out.WagePerHour <= 0.0f) { return false; }
			Out.Category = EUEGT2ServiceCategory::PaidWork;
			break;
		default: return false;
		}
		Out.CategoryName = CategoryName(Out.Category);
		Out.Name = VenueName(*Amenity);
		Out.Amenity = Amenity;
		return true;
	}
	bool TieBefore(const AUEGT2Amenity& Candidate, const AUEGT2Amenity& Previous)
	{
		if (Candidate.GetKind() != Previous.GetKind()) { return Candidate.GetKind() < Previous.GetKind(); }
		const int32 NameOrder = Candidate.GetVenueName().ToString().Compare(Previous.GetVenueName().ToString(), ESearchCase::CaseSensitive);
		if (NameOrder != 0) { return NameOrder < 0; }
		const FVector A = Candidate.GetActorLocation(), B = Previous.GetActorLocation();
		if (A.X != B.X) { return A.X < B.X; }
		if (A.Y != B.Y) { return A.Y < B.Y; }
		if (A.Z != B.Z) { return A.Z < B.Z; }
		return Candidate.GetFName().LexicalLess(Previous.GetFName());
	}
}

UUEGT2ServicesSubsystem* UUEGT2ServicesSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UUEGT2ServicesSubsystem>() : nullptr;
}

bool UUEGT2ServicesSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UUEGT2ServicesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UUEGT2GameUserSettings::OnSettingsApplied.AddUObject(this, &UUEGT2ServicesSubsystem::RefreshFromSettings);
}

void UUEGT2ServicesSubsystem::Deinitialize()
{
	UUEGT2GameUserSettings::OnSettingsApplied.RemoveAll(this);
	DropTracking(TEXT("world ended"));
	Super::Deinitialize();
}

bool UUEGT2ServicesSubsystem::IsAvailable() const
{
	if (!bFeatureEnabled) { DropTracking(TEXT("nearby services disabled")); }
	return bFeatureEnabled;
}

bool UUEGT2ServicesSubsystem::IsEnabled() const
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const bool bEnabled = IsAvailable() && Settings && Settings->GetNearbyServicesEnabled();
	if (!bEnabled) { DropTracking(TEXT("nearby services disabled")); }
	return bEnabled;
}

void UUEGT2ServicesSubsystem::RefreshFromSettings() { IsEnabled(); }

void UUEGT2ServicesSubsystem::DropTracking(const TCHAR* Reason) const
{
	if (TrackedKind != EUEGT2AmenityKind::Count)
	{
		UE_LOG(LogUEGT2Services, Log, TEXT("Stopped tracking service: %s."), Reason);
	}
	TrackedAmenity.Reset();
	TrackedKind = EUEGT2AmenityKind::Count;
	TrackedJobRole = EUEGT2NPCRole::Villager;
	TrackedVenueName = FText::GetEmpty();
}

TArray<FUEGT2ServiceEntry> UUEGT2ServicesSubsystem::GetEntries(const AUEGT2PlayerController* Controller) const
{
	using namespace UEGT2Services;
	TArray<FUEGT2ServiceEntry> Entries;
	const AUEGT2Character* Player = IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->GetWorld() == GetWorld()
		? Cast<AUEGT2Character>(Controller->GetPawn()) : nullptr;
	if (!IsEnabled() || !IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld() != GetWorld()
		|| !IsFinite(Player->GetActorLocation())) { return Entries; }
	GetTrackedAmenity();
	Entries.SetNum(CategoryCount);
	double Distances[CategoryCount];
	for (int32 Index = 0; Index < CategoryCount; ++Index)
	{
		Entries[Index].Category = static_cast<EUEGT2ServiceCategory>(Index);
		Entries[Index].CategoryName = CategoryName(Entries[Index].Category);
		Distances[Index] = MAX_dbl;
	}
	int32 FoundCount = 0;
	for (TActorIterator<AUEGT2Amenity> It(GetWorld()); It; ++It)
	{
		FUEGT2ServiceEntry Candidate;
		if (!Describe(GetWorld(), *It, Candidate)) { continue; }
		const double DistanceSquared = FVector::DistSquared2D(Player->GetActorLocation(), It->GetActorLocation());
		const double Metres = FMath::Sqrt(DistanceSquared) / 100.0;
		if (!FMath::IsFinite(Metres) || Metres > MAX_flt) { continue; }
		const int32 Index = static_cast<int32>(Candidate.Category);
		AUEGT2Amenity* Previous = Entries[Index].Amenity.Get();
		if (Previous && (DistanceSquared > Distances[Index]
			|| (DistanceSquared == Distances[Index] && !TieBefore(**It, *Previous)))) { continue; }
		if (!Previous) { ++FoundCount; }
		Candidate.DistanceMetres = static_cast<float>(Metres);
		Entries[Index] = MoveTemp(Candidate);
		Distances[Index] = DistanceSquared;
	}
	UE_LOG(LogUEGT2Services, Log, TEXT("Nearby services refreshed: %d of %d categories available."), FoundCount, CategoryCount);
	return Entries;
}

bool UUEGT2ServicesSubsystem::TrackAmenity(AUEGT2Amenity* Amenity)
{
	FUEGT2ServiceEntry Entry;
	if (!IsEnabled() || !UEGT2Services::Describe(GetWorld(), Amenity, Entry))
	{
		UE_LOG(LogUEGT2Services, Log, TEXT("Tracking refused: service unavailable."));
		return false;
	}
	const bool bChanged = TrackedAmenity.Get() != Amenity || TrackedKind != Amenity->GetKind()
		|| TrackedJobRole != Amenity->GetJobRole() || !TrackedVenueName.EqualTo(Amenity->GetVenueName());
	TrackedAmenity = Amenity;
	TrackedKind = Amenity->GetKind();
	TrackedJobRole = Amenity->GetJobRole();
	TrackedVenueName = Amenity->GetVenueName();
	if (UUEGT2SurveySubsystem* Survey = UUEGT2SurveySubsystem::Get(GetWorld())) { Survey->ClearTracking(); }
	if (bChanged)
	{
		UE_LOG(LogUEGT2Services, Log, TEXT("Tracking service %s (%s)."), *Entry.Name.ToString(), *Entry.CategoryName.ToString());
	}
	return true;
}

void UUEGT2ServicesSubsystem::ClearTracking() { DropTracking(TEXT("player cleared directions")); }

AUEGT2Amenity* UUEGT2ServicesSubsystem::GetTrackedAmenity() const
{
	if (!IsEnabled()) { return nullptr; }
	AUEGT2Amenity* Amenity = TrackedAmenity.Get();
	FUEGT2ServiceEntry Entry;
	if (!UEGT2Services::Describe(GetWorld(), Amenity, Entry) || Amenity->GetKind() != TrackedKind
		|| Amenity->GetJobRole() != TrackedJobRole || !Amenity->GetVenueName().EqualTo(TrackedVenueName))
	{
		DropTracking(TEXT("target removed or reconfigured"));
		return nullptr;
	}
	return Amenity;
}

bool UUEGT2ServicesSubsystem::GetTrackedDirection(const FVector& Origin, float ViewYaw, FUEGT2SurveyDirection& Out) const
{
	Out = FUEGT2SurveyDirection();
	AUEGT2Amenity* Amenity = GetTrackedAmenity();
	if (!Amenity || !UUEGT2SurveySubsystem::CalculateDirection(Origin, Amenity->GetActorLocation(), ViewYaw, Out)) { return false; }
	Out.Name = UEGT2Services::VenueName(*Amenity);
	return true;
}

#undef LOCTEXT_NAMESPACE
