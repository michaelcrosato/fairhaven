#include "Rest/UEGT2RestSubsystem.h"

#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Interaction/UEGT2Amenity.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"

#define LOCTEXT_NAMESPACE "UEGT2Rest"

UUEGT2RestSubsystem* UUEGT2RestSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UUEGT2RestSubsystem>() : nullptr;
}

bool UUEGT2RestSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UUEGT2RestSubsystem::IsAvailable() const
{
	return bFeatureEnabled && !UUEGT2CaptureSubsystem::IsCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsWalkSmokeRequested() && !UUEGT2CaptureSubsystem::IsFlySoakRequested();
}

bool UUEGT2RestSubsystem::IsEnabled() const
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	return IsAvailable() && Settings && Settings->GetSleepUntilEnabled();
}

bool UUEGT2RestSubsystem::CanSleepAt(const AUEGT2PlayerController* Controller,
	const AUEGT2Amenity* Bed, FText& Reason) const
{
	Reason = FText::GetEmpty();
	if (!IsEnabled()) { Reason = LOCTEXT("Disabled", "Sleep until is turned off for this session."); return false; }
	if (bCommitting) { Reason = LOCTEXT("Busy", "You are already resting."); return false; }
	const UWorld* World = GetWorld();
	const AUEGT2Character* Player = Controller ? Cast<AUEGT2Character>(Controller->GetPawn()) : nullptr;
	const UUEGT2NeedsComponent* Life = Player ? Player->FindComponentByClass<UUEGT2NeedsComponent>() : nullptr;
	if (!World || !IsValid(Controller) || Controller->GetWorld() != World
		|| Controller->GetMenuState() == EUEGT2MenuState::Main || !IsValid(Player)
		|| Player->GetWorld() != World || !Life || !Life->HasBegunPlay())
	{
		Reason = LOCTEXT("NoPlayer", "Start a visit before choosing a wake time."); return false;
	}
	if (!IsValid(Bed) || Bed->IsActorBeingDestroyed() || Bed->GetWorld() != World
		|| Bed->GetKind() != EUEGT2AmenityKind::Bed || !Bed->CanInteract(Player))
	{
		Reason = LOCTEXT("NoBed", "This bed is no longer available."); return false;
	}
	const double DistanceSq = FVector::DistSquared(Player->GetActorLocation(), Bed->GetActorLocation());
	const float Range = Bed->GetUseRange();
	if (!FMath::IsFinite(DistanceSq) || !FMath::IsFinite(Range) || Range <= 0.0f || DistanceSq > FMath::Square(Range))
	{
		Reason = LOCTEXT("TooFar", "Move closer to the bed to sleep."); return false;
	}
	const UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround() || Player->IsFlyEnabled() || Player->IsNoclipEnabled() || Player->bIsCrouched)
	{
		Reason = LOCTEXT("Stand", "Stand on the ground beside the bed to sleep."); return false;
	}
	if (!UUEGT2NeedsComponent::IsValidProgress(Life->GetNeeds(), Life->GetPurse(), Life->GetTrade()))
	{
		Reason = LOCTEXT("InvalidLife", "Your current needs cannot be advanced."); return false;
	}
	const UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	FUEGT2RestPreview Preview;
	if (!Director) { Reason = LOCTEXT("NoTown", "The town is not ready to advance time."); return false; }
	// At the maximum supported date, an earlier selected hour may cross the
	// limit while a later one is still valid. Eligibility uses the latest hour;
	// GetPreview validates the player's actual choice separately.
	return Director->CanAdvanceForRest(23, Preview, Reason);
}

bool UUEGT2RestSubsystem::GetPreview(const AUEGT2PlayerController* Controller, const AUEGT2Amenity* Bed,
	int32 WakeHour, FUEGT2RestPreview& Out, FText& Reason) const
{
	Out = FUEGT2RestPreview();
	return CanSleepAt(Controller, Bed, Reason)
		&& UUEGT2NPCDirector::Get(GetWorld())->CanAdvanceForRest(WakeHour, Out, Reason);
}

bool UUEGT2RestSubsystem::SleepUntil(AUEGT2PlayerController* Controller, AUEGT2Amenity* Bed,
	int32 WakeHour, FText& Reason)
{
	FUEGT2RestPreview Preview;
	if (!GetPreview(Controller, Bed, WakeHour, Preview, Reason)) { return false; }
	if (!GetWorld()->IsPaused() || !Controller->IsRestPanelOpen())
	{
		Reason = LOCTEXT("NotPaused", "Choose a wake time from the bed's sleep panel."); return false;
	}
	AUEGT2Character* Player = CastChecked<AUEGT2Character>(Controller->GetPawn());
	UUEGT2NeedsComponent* Life = Player->FindComponentByClass<UUEGT2NeedsComponent>();
	FUEGT2NPCNeeds Needs = Life->GetNeeds();
	FUEGT2Purse Purse = Life->GetPurse();
	const EUEGT2NPCRole Trade = Life->GetTrade();
	// Calculate the player's candidate before the town commits. Explicit sleep
	// runs for the selected duration even after energy reaches its normal stop point.
	if (!UEGT2AdvanceLife(Preview.DurationHours, EUEGT2Activity::Sleep, Trade, Needs, Purse)
		|| !UUEGT2NeedsComponent::IsValidProgress(Needs, Purse, Trade))
	{
		Reason = LOCTEXT("InvalidResult", "Your needs could not be advanced for this rest."); return false;
	}
	TGuardValue<bool> Committing(bCommitting, true);
	const double Started = FPlatformTime::Seconds();
	if (!UUEGT2NPCDirector::Get(GetWorld())->AdvanceForRest(WakeHour, Preview, Reason)) { return false; }
	// No world tick occurs inside this paused transaction. The already validated
	// snapshot resets transient activity through the same durable-state setter.
	Controller->CloseDialogue();
	Player->GetCharacterMovement()->StopMovementImmediately();
	for (TActorIterator<AUEGT2Pickup> It(GetWorld()); It; ++It) { It->ReleaseIfCarriedBy(Player); }
	const bool bApplied = Life->RestoreProgress(Needs, Purse, Trade);
	check(bApplied);
	UE_LOG(LogUEGT2Rest, Log, TEXT("Slept %.6f h at %s: day %d %.6f -> day %d %02d:00, %.3f coins, %.1f ms."),
		Preview.DurationHours, *Bed->GetVenueName().ToString(), Preview.StartDayIndex, Preview.StartHour,
		Preview.WakeDayIndex, Preview.WakeHour, Purse.Coins, (FPlatformTime::Seconds() - Started) * 1000.0);
	return true;
}

#undef LOCTEXT_NAMESPACE
