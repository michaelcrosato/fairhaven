#include "Survey/UEGT2SurveySubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "Services/UEGT2ServicesSubsystem.h"
#include "UEGT2LogChannels.h"

#define LOCTEXT_NAMESPACE "UEGT2Survey"

namespace UEGT2Survey
{
	bool IsFinite(const FVector& Point)
	{
		return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z);
	}

	FText CompassPoint(int32 Index)
	{
		switch (Index)
		{
		case 0: return LOCTEXT("North", "N");
		case 1: return LOCTEXT("NorthEast", "NE");
		case 2: return LOCTEXT("East", "E");
		case 3: return LOCTEXT("SouthEast", "SE");
		case 4: return LOCTEXT("South", "S");
		case 5: return LOCTEXT("SouthWest", "SW");
		case 6: return LOCTEXT("West", "W");
		default: return LOCTEXT("NorthWest", "NW");
		}
	}
}

UUEGT2SurveySubsystem* UUEGT2SurveySubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UUEGT2SurveySubsystem>() : nullptr;
}

bool UUEGT2SurveySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UUEGT2SurveySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UUEGT2GameUserSettings::OnSettingsApplied.AddUObject(this, &UUEGT2SurveySubsystem::RefreshFromSettings);
}

void UUEGT2SurveySubsystem::RefreshFromSettings() { IsEnabled(); }

void UUEGT2SurveySubsystem::Deinitialize()
{
	UUEGT2GameUserSettings::OnSettingsApplied.RemoveAll(this);
	DropTracking(TEXT("world ended"));
	Super::Deinitialize();
}

bool UUEGT2SurveySubsystem::IsAvailable() const
{
	if (!bFeatureEnabled) { DropTracking(TEXT("journal disabled")); }
	return bFeatureEnabled;
}

bool UUEGT2SurveySubsystem::IsEnabled() const
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const bool bEnabled = IsAvailable() && Settings && Settings->GetSurveyJournalEnabled();
	if (!bEnabled) { DropTracking(TEXT("journal disabled")); }
	return bEnabled;
}

void UUEGT2SurveySubsystem::DropTracking(const TCHAR* Reason) const
{
	if (!TrackedId.IsNone())
	{
		UE_LOG(LogUEGT2Survey, Log, TEXT("Stopped tracking %s: %s."), *TrackedId.ToString(), Reason);
	}
	TrackedLandmark.Reset();
	TrackedId = NAME_None;
}

AUEGT2Landmark* UUEGT2SurveySubsystem::GetValidTrackedLandmark() const
{
	if (!IsEnabled())
	{
		DropTracking(TEXT("journal disabled"));
		return nullptr;
	}
	AUEGT2Landmark* Landmark = TrackedLandmark.Get();
	if (!Landmark || Landmark->IsActorBeingDestroyed() || Landmark->GetWorld() != GetWorld()
		|| TrackedId.IsNone() || Landmark->GetPersistentId() != TrackedId || !Landmark->IsDiscovered())
	{
		DropTracking(TEXT("target no longer available or surveyed"));
		return nullptr;
	}
	return Landmark;
}

TMap<FName, AUEGT2Landmark*> UUEGT2SurveySubsystem::GatherLandmarks() const
{
	TMap<FName, AUEGT2Landmark*> Unique;
	TSet<FName> Ambiguous;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed()) { continue; }
			const FName Id = It->GetPersistentId();
			if (Id.IsNone() || Ambiguous.Contains(Id)) { continue; }
			if (Unique.Contains(Id))
			{
				// An ID must name one place. Never make enumeration order decide
				// which duplicate the player is sent towards.
				Unique.Remove(Id);
				Ambiguous.Add(Id);
			}
			else { Unique.Add(Id, *It); }
		}
	}
	if (AUEGT2Landmark* Tracked = GetValidTrackedLandmark())
	{
		AUEGT2Landmark* const* Current = Unique.Find(TrackedId);
		if (!Current || *Current != Tracked) { DropTracking(TEXT("target identity is ambiguous")); }
	}
	return Unique;
}

TArray<FUEGT2SurveyEntry> UUEGT2SurveySubsystem::GetEntries() const
{
	TArray<FUEGT2SurveyEntry> Entries;
	if (!IsEnabled())
	{
		DropTracking(TEXT("journal disabled"));
		return Entries;
	}
	for (const TPair<FName, AUEGT2Landmark*>& Pair : GatherLandmarks())
	{
		Entries.Add({ Pair.Key, Pair.Value->GetLandmarkName(), Pair.Value->IsDiscovered() });
	}
	Entries.Sort([](const FUEGT2SurveyEntry& A, const FUEGT2SurveyEntry& B)
	{
		const int32 Compared = A.Name.ToString().Compare(B.Name.ToString(), ESearchCase::IgnoreCase);
		return Compared == 0 ? A.Id.LexicalLess(B.Id) : Compared < 0;
	});
	return Entries;
}

bool UUEGT2SurveySubsystem::TrackLandmark(FName Id)
{
	if (!IsEnabled())
	{
		DropTracking(TEXT("journal disabled"));
		UE_LOG(LogUEGT2Survey, Log, TEXT("Tracking refused: journal disabled."));
		return false;
	}
	const TMap<FName, AUEGT2Landmark*> Landmarks = GatherLandmarks();
	AUEGT2Landmark* const* Found = Landmarks.Find(Id);
	if (!Found || !(*Found)->IsDiscovered())
	{
		UE_LOG(LogUEGT2Survey, Log, TEXT("Tracking refused for %s: no unique surveyed place."), *Id.ToString());
		return false;
	}
	if (TrackedLandmark.Get() != *Found || TrackedId != Id)
	{
		TrackedLandmark = *Found;
		TrackedId = Id;
		UE_LOG(LogUEGT2Survey, Log, TEXT("Tracking %s [%s]."), *(*Found)->GetLandmarkName().ToString(), *Id.ToString());
	}
	if (UUEGT2ServicesSubsystem* Services = UUEGT2ServicesSubsystem::Get(GetWorld())) { Services->ClearTracking(); }
	return true;
}

void UUEGT2SurveySubsystem::ClearTracking()
{
	DropTracking(TEXT("player cleared directions"));
}

FName UUEGT2SurveySubsystem::GetTrackedLandmarkId() const
{
	return GetValidTrackedLandmark() ? TrackedId : NAME_None;
}

bool UUEGT2SurveySubsystem::GetTrackedDirection(const FVector& Origin, float ViewYaw,
	FUEGT2SurveyDirection& Out) const
{
	Out = FUEGT2SurveyDirection();
	AUEGT2Landmark* Landmark = GetValidTrackedLandmark();
	if (!Landmark || !CalculateDirection(Origin, Landmark->GetActorLocation(), ViewYaw, Out)) { return false; }
	Out.Id = TrackedId;
	Out.Name = Landmark->GetLandmarkName();
	return true;
}

bool UUEGT2SurveySubsystem::CalculateDirection(const FVector& Origin, const FVector& Target,
	float ViewYaw, FUEGT2SurveyDirection& Out)
{
	Out = FUEGT2SurveyDirection();
	if (!UEGT2Survey::IsFinite(Origin) || !UEGT2Survey::IsFinite(Target) || !FMath::IsFinite(ViewYaw)) { return false; }
	const double DeltaX = Target.X - Origin.X;
	const double DeltaY = Target.Y - Origin.Y;
	const double Metres = FMath::Sqrt(DeltaX * DeltaX + DeltaY * DeltaY) / 100.0;
	if (!FMath::IsFinite(Metres) || Metres > MAX_flt) { return false; }
	Out.DistanceMetres = static_cast<float>(Metres);
	Out.bNearby = Metres <= 10.0;
	if (Out.bNearby)
	{
		Out.CompassDirection = LOCTEXT("Nearby", "Nearby");
		return true;
	}
	const double Bearing = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(DeltaY, DeltaX)) + 360.0, 360.0);
	const double NormalizedYaw = FMath::Fmod(static_cast<double>(ViewYaw), 360.0);
	Out.BearingDegrees = static_cast<float>(Bearing);
	Out.RelativeBearingDegrees = static_cast<float>(FMath::Fmod(Bearing - NormalizedYaw + 540.0, 360.0) - 180.0);
	// Float output can round a double just below an excluded endpoint up to it.
	if (Out.BearingDegrees >= 360.0f) { Out.BearingDegrees -= 360.0f; }
	if (Out.RelativeBearingDegrees >= 180.0f) { Out.RelativeBearingDegrees -= 360.0f; }
	const int32 CompassIndex = FMath::FloorToInt((Bearing + 22.5) / 45.0) % 8;
	Out.CompassDirection = UEGT2Survey::CompassPoint(CompassIndex);
	return true;
}

#undef LOCTEXT_NAMESPACE
