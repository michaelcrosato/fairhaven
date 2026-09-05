#include "Autosave/UEGT2AutosaveSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "World/UEGT2SkyController.h"

UUEGT2AutosaveSubsystem* UUEGT2AutosaveSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UUEGT2AutosaveSubsystem>() : nullptr;
}

bool UUEGT2AutosaveSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UUEGT2AutosaveSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldStarted = true;
	// A short interval belongs only to a validated, isolated smoke run. A typo
	// in diagnostic arguments must never accelerate writes to a player's saves.
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(&InWorld);
	float RequestedInterval = 0.0f;
	if (Progress && Progress->IsAutosaveSmoke()
		&& FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveIntervalSeconds="), RequestedInterval)
		&& FMath::IsFinite(RequestedInterval) && RequestedInterval >= 1.0f && RequestedInterval <= 300.0f)
	{
		IntervalSeconds = RequestedInterval;
		UE_LOG(LogUEGT2Autosave, Log, TEXT("Isolated autosave smoke interval: %.2f seconds."), IntervalSeconds);
	}
}

void UUEGT2AutosaveSubsystem::Deinitialize()
{
	bWorldStarted = false;
	Super::Deinitialize();
}

TStatId UUEGT2AutosaveSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2AutosaveSubsystem, STATGROUP_Tickables);
}

bool UUEGT2AutosaveSubsystem::IsAvailable() const
{
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	return bFeatureEnabled && Progress && Progress->IsAvailable();
}

bool UUEGT2AutosaveSubsystem::IsEnabled() const
{
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(GetWorld());
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	return bFeatureEnabled && Progress && Progress->IsEnabled() && Settings && Settings->GetAutosaveEnabled();
}

bool UUEGT2AutosaveSubsystem::CanAutosaveNow(const AUEGT2PlayerController* Controller) const
{
	const UWorld* World = GetWorld();
	const UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(World);
	if (!IsEnabled() || !World || World->IsPaused() || !Progress || !Progress->IsJourneyActive(World)
		|| !IsValid(Controller) || Controller->GetWorld() != World
		|| Controller->GetMenuState() != EUEGT2MenuState::None || Controller->IsDialogueOpen()) { return false; }
	const AUEGT2Character* Player = Cast<AUEGT2Character>(Controller->GetPawn());
	const UUEGT2NeedsComponent* Life = Player ? Player->GetLife() : nullptr;
	const UCharacterMovementComponent* Movement = Player ? Player->GetCharacterMovement() : nullptr;
	const UCapsuleComponent* Capsule = Player ? Player->GetCapsuleComponent() : nullptr;
	const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	const AUEGT2SkyController* Sky = AUEGT2SkyController::Get(World);
	if (!IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetWorld() != World
		|| !Life || !Life->HasBegunPlay() || !Movement || !Capsule || !Movement->IsMovingOnGround()
		|| Player->bIsCrouched || Player->IsFlyEnabled() || Player->IsNoclipEnabled()
		|| Player->GetActorLocation().ContainsNaN() || Controller->GetControlRotation().ContainsNaN()
		|| !UUEGT2NeedsComponent::IsValidProgress(Life->GetNeeds(), Life->GetPurse(), Life->GetTrade())
		|| !Director || Director->GetPopulation() == 0 || !IsValid(Sky) || Sky->IsActorBeingDestroyed()
		|| Director->GetDayIndex() < 0 || Director->GetDayIndex() > 1000000
		|| !FMath::IsFinite(Sky->GetTimeOfDay()) || Sky->GetTimeOfDay() < 0.0f || Sky->GetTimeOfDay() >= 24.0f)
	{
		return false;
	}
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0.0f || HalfHeight < Radius) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AutosaveStanding), false, Player);
	return !World->OverlapBlockingTestByProfile(Player->GetActorLocation(), FQuat::Identity,
		TEXT("Pawn"), FCollisionShape::MakeCapsule(Radius, HalfHeight), Params);
}

void UUEGT2AutosaveSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	UUEGT2ProgressSubsystem* Progress = UUEGT2ProgressSubsystem::Get(World);
	if (!World || !Progress || !FMath::IsFinite(DeltaTime) || DeltaTime < 0.0f) { return; }
	const uint64 Journey = Progress->GetJourneyGeneration();
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	const uint64 Revision = Settings ? Settings->GetPersistenceRevision() : 0;
	const bool bEnabled = IsEnabled();
	const FUEGT2AutosaveStatus Status = Progress->GetAutosaveStatus();
	if (Journey != ObservedJourney || Status.SuccessfulWrites != ObservedWrites
		|| Revision != ObservedSettings || !bEnabled)
	{
		ElapsedSeconds = RetrySeconds = 0.0f;
		ObservedJourney = Journey;
		ObservedWrites = Status.SuccessfulWrites;
		ObservedSettings = Revision;
	}
	AUEGT2PlayerController* Controller = Cast<AUEGT2PlayerController>(World->GetFirstPlayerController());
	if (!bEnabled || World->IsPaused() || !Progress->IsJourneyActive(World) || !Controller
		|| Controller->GetMenuState() != EUEGT2MenuState::None) { return; }
	// Only play ticks count. A skipped calendar day never becomes five minutes
	// of play, and closing a pause menu does not reset an existing interval.
	ElapsedSeconds = FMath::Min(IntervalSeconds, ElapsedSeconds + DeltaTime);
	RetrySeconds = FMath::Max(0.0f, RetrySeconds - DeltaTime);
	if (ElapsedSeconds < IntervalSeconds || RetrySeconds > 0.0f || Status.bBusy) { return; }
	RetrySeconds = 5.0f;
	if (CanAutosaveNow(Controller))
	{
		// Keep this interval due until verification succeeds. A failed disk write
		// or a pause during an async read retries without accumulating requests.
		Progress->RequestAutosave(Controller);
	}
}
