#include "Progress/UEGT2ProgressSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSave.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "UObject/StrongObjectPtr.h"
#include "World/UEGT2SkyController.h"

#define LOCTEXT_NAMESPACE "UEGT2Progress"

namespace UEGT2Progress
{
	constexpr int32 UserIndex = 0;
	const TCHAR* DefaultSlot = TEXT("Fairhaven_Journey");
	const TCHAR* SmokePrefix = TEXT("UEGT2_ProgressSmoke_");

	FString MapIdentity(const UWorld* World)
	{
		return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
	}

	bool GatherLandmarks(UWorld* World, TSet<FName>& OutIds, FText& OutReason)
	{
		if (!World) { OutReason = LOCTEXT("NoWorld", "The world is not ready."); return false; }
		for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
		{
			if (It->IsActorBeingDestroyed()) { continue; }
			const FName Id = It->GetPersistentId();
			if (Id.IsNone() || OutIds.Contains(Id))
			{
				OutReason = LOCTEXT("WorldIds", "This world needs a content rebuild before progress can be saved.");
				return false;
			}
			OutIds.Add(Id);
		}
		return true;
	}

	float StandingHalfHeight(const AUEGT2Character* Character)
	{
		return Character->GetClass()->GetDefaultObject<AUEGT2Character>()
			->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
			* Character->GetCapsuleComponent()->GetShapeScale();
	}

	bool CanStandAt(UWorld* World, AUEGT2Character* Character, const FVector& Location)
	{
		const UCapsuleComponent* DefaultCapsule = Character->GetClass()
			->GetDefaultObject<AUEGT2Character>()->GetCapsuleComponent();
		const float Scale = Character->GetCapsuleComponent()->GetShapeScale();
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(
			DefaultCapsule->GetUnscaledCapsuleRadius() * Scale, StandingHalfHeight(Character));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ProgressPlacement), false, Character);
		// Use the ordinary Pawn profile even if the current session has noclip
		// enabled. A disabled capsule cannot be its own safety policy.
		return !World->OverlapBlockingTestByProfile(Location, FQuat::Identity,
			TEXT("Pawn"), Shape, Params);
	}

	bool FindPlacement(UWorld* World, AUEGT2Character* Character,
		const FVector& SavedLocation, FVector& OutLocation, bool& bOutFallback)
	{
		bOutFallback = false;
		if (CanStandAt(World, Character, SavedLocation))
		{
			OutLocation = SavedLocation;
			return true;
		}
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed() && CanStandAt(World, Character, It->GetActorLocation()))
			{
				OutLocation = It->GetActorLocation();
				bOutFallback = true;
				return true;
			}
		}
		return false;
	}
}

UUEGT2ProgressSubsystem* UUEGT2ProgressSubsystem::Get(const UWorld* World)
{
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UUEGT2ProgressSubsystem>() : nullptr;
}

bool UUEGT2ProgressSubsystem::ResolveSlot(FString& OutSlot) const
{
	FString Mode, RequestedSlot;
	const bool bSmoke = FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), Mode);
	const bool bOverride = FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSlot="), RequestedSlot);
	if (!bSmoke && !bOverride)
	{
		// FParse::Value recognizes even an empty '=value', but a bare switch
		// has no value match. It is still a malformed test request, never consent
		// to read or replace the player's ordinary checkpoint.
		if (FParse::Param(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke"))
			|| FParse::Param(FCommandLine::Get(), TEXT("UEGT2ProgressSlot")))
		{
			return false;
		}
		OutSlot = UEGT2Progress::DefaultSlot;
		return true;
	}
	if (!bSmoke || !bOverride || (Mode != TEXT("Write") && Mode != TEXT("Read")
		&& Mode != TEXT("Disabled") && Mode != TEXT("NewVisit"))
		|| !RequestedSlot.StartsWith(UEGT2Progress::SmokePrefix, ESearchCase::CaseSensitive)
		|| RequestedSlot.Len() <= FCString::Strlen(UEGT2Progress::SmokePrefix) || RequestedSlot.Len() > 96)
	{
		return false;
	}
	for (TCHAR Character : RequestedSlot)
	{
		if (!((Character >= TEXT('a') && Character <= TEXT('z'))
			|| (Character >= TEXT('A') && Character <= TEXT('Z'))
			|| (Character >= TEXT('0') && Character <= TEXT('9'))
			|| Character == TEXT('_') || Character == TEXT('-')))
		{
			return false;
		}
	}
	OutSlot = RequestedSlot;
	return true;
}

bool UUEGT2ProgressSubsystem::IsAvailable() const
{
	FString Slot;
	return bFeatureEnabled && !IsRunningCommandlet()
		&& !UUEGT2CaptureSubsystem::IsCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsLifeCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
		&& !UUEGT2CaptureSubsystem::IsFlySoakRequested()
		&& ResolveSlot(Slot);
}

bool UUEGT2ProgressSubsystem::IsEnabled() const
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	return IsAvailable() && Settings && Settings->GetSaveProgressEnabled();
}

FText UUEGT2ProgressSubsystem::GetStatusText() const
{
	if (!IsAvailable()) { return LOCTEXT("Unavailable", "Journey checkpoints are unavailable in this run."); }
	if (!IsEnabled()) { return LOCTEXT("Disabled", "Journey checkpoints are turned off in Gameplay settings."); }
	return StatusText;
}

bool UUEGT2ProgressSubsystem::Fail(const FText& Reason)
{
	StatusText = Reason;
	UE_LOG(LogUEGT2Progress, Log, TEXT("Checkpoint not completed: %s"), *Reason.ToString());
	return false;
}

UUEGT2ProgressSave* UUEGT2ProgressSubsystem::ReadCheckpoint(const FString& Slot, const FString& Map,
	const TSet<FName>& LandmarkIds, FString& OutSourceSlot, FText& OutReason, bool* bOutHadFile) const
{
	if (bOutHadFile) { *bOutHadFile = false; }
	// Defense at the I/O boundary as well as at every public operation.
	if (!IsEnabled()) { return nullptr; }
	TStrongObjectPtr<UUEGT2ProgressSave> Best;
	OutReason = LOCTEXT("Missing", "There is no compatible journey checkpoint yet.");
	for (const TCHAR* Suffix : { TEXT("_A"), TEXT("_B") })
	{
		const FString PhysicalSlot = Slot + Suffix;
		if (!UGameplayStatics::DoesSaveGameExist(PhysicalSlot, UEGT2Progress::UserIndex)) { continue; }
		if (bOutHadFile) { *bOutHadFile = true; }
		TArray<uint8> Bytes;
		if (!UGameplayStatics::LoadDataFromSlot(Bytes, PhysicalSlot, UEGT2Progress::UserIndex)
			|| Bytes.IsEmpty() || Bytes.Num() > UUEGT2ProgressSave::MaxEncodedBytes)
		{
			OutReason = LOCTEXT("Unreadable", "The journey checkpoint could not be read.");
			continue;
		}
		TStrongObjectPtr<UUEGT2ProgressSave> Candidate(UUEGT2ProgressSave::Decode(Bytes, OutReason));
		if (!Candidate.IsValid())
		{
			continue;
		}
		if (!Candidate->Validate(Map, LandmarkIds, OutReason)) { continue; }
		if (!Best.IsValid() || Candidate->Sequence > Best->Sequence)
		{
			Best = MoveTemp(Candidate);
			OutSourceSlot = PhysicalSlot;
		}
	}
	if (Best.IsValid()) { OutReason = FText::GetEmpty(); }
	return Best.Get();
}

bool UUEGT2ProgressSubsystem::HasSavedProgress() const
{
	if (!IsEnabled()) { return false; }
	FString Slot;
	if (!ResolveSlot(Slot)) { return false; }
	UWorld* World = GetWorld();
	if (!bAvailabilityCached || AvailabilityWorld.Get() != World || AvailabilitySlot != Slot)
	{
		TSet<FName> Ids;
		FText Reason;
		FString Source;
		bool bHadFile = false;
		const bool bWorldReady = UEGT2Progress::GatherLandmarks(World, Ids, Reason);
		bHasCheckpoint = bWorldReady
			&& ReadCheckpoint(Slot, UEGT2Progress::MapIdentity(World), Ids, Source, Reason, &bHadFile) != nullptr;
		if (!bHasCheckpoint && (bHadFile || !bWorldReady))
		{
			StatusText = Reason;
			UE_LOG(LogUEGT2Progress, Log, TEXT("Checkpoint unavailable: %s"), *Reason.ToString());
		}
		AvailabilityWorld = World;
		AvailabilitySlot = Slot;
		bAvailabilityCached = true;
	}
	return bHasCheckpoint;
}

void UUEGT2ProgressSubsystem::SetJourneyActive(bool bActive)
{
	bJourneyActive = bActive;
	JourneyWorld = bActive ? GetWorld() : nullptr;
	bAvailabilityCached = false;
}

void UUEGT2ProgressSubsystem::RequestNewJourney()
{
	bNewJourneyRequested = true;
	SetJourneyActive(false);
	StatusText = FText::GetEmpty();
}

bool UUEGT2ProgressSubsystem::ConsumeNewJourneyRequest()
{
	const bool bRequested = bNewJourneyRequested;
	bNewJourneyRequested = false;
	return bRequested;
}

bool UUEGT2ProgressSubsystem::SaveProgress(AUEGT2PlayerController* Controller)
{
	using namespace UEGT2Progress;
	if (!IsEnabled()) { return Fail(GetStatusText()); }
	UWorld* World = GetWorld();
	if (!Controller || Controller->GetWorld() != World || !bJourneyActive || JourneyWorld.Get() != World
		|| Controller->GetMenuState() != EUEGT2MenuState::Pause || !World->IsPaused())
	{
		return Fail(LOCTEXT("PauseFirst", "Pause an active journey before saving progress."));
	}
	AUEGT2Character* Character = Cast<AUEGT2Character>(Controller->GetPawn());
	UUEGT2NeedsComponent* Life = Character ? Character->GetLife() : nullptr;
	UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	AUEGT2SkyController* Sky = AUEGT2SkyController::Get(World);
	if (!Life || !Life->HasBegunPlay() || !Director || !Sky)
	{
		return Fail(LOCTEXT("NotReady", "The player and world must finish loading before using a checkpoint."));
	}
	TSet<FName> Ids;
	FText Reason;
	FString Slot, SourceSlot;
	if (!ResolveSlot(Slot) || !GatherLandmarks(World, Ids, Reason)) { return Fail(Reason); }
	const FString Map = MapIdentity(World);
	TStrongObjectPtr<UUEGT2ProgressSave> Previous(ReadCheckpoint(Slot, Map, Ids, SourceSlot, Reason));
	TStrongObjectPtr<UUEGT2ProgressSave> Snapshot(NewObject<UUEGT2ProgressSave>());
	Snapshot->Sequence = Previous.IsValid() ? Previous->Sequence + 1 : 1;
	Snapshot->MapPackageName = Map;
	Snapshot->PlayerLocation = Character->GetActorLocation();
	Snapshot->PlayerLocation.Z += StandingHalfHeight(Character) - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Snapshot->ViewRotation = Controller->GetControlRotation().GetNormalized();
	Snapshot->Needs = Life->GetNeeds();
	Snapshot->Purse = Life->GetPurse();
	Snapshot->Trade = Life->GetTrade();
	Snapshot->DayIndex = Director->GetDayIndex();
	Snapshot->Hour = Sky->GetTimeOfDay();
	// A pause can land between the sky's actor tick and the director's tick.
	// Capture the same midnight crossing the director will observe next.
	if (Snapshot->Hour + 12.0f < Director->GetHour()) { ++Snapshot->DayIndex; }
	Snapshot->Weather = Sky->GetWeather();
	for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->IsDiscovered()) { Snapshot->DiscoveredLandmarks.Add(It->GetPersistentId()); }
	}
	Snapshot->DiscoveredLandmarks.Sort(FNameLexicalLess());
	if (!Snapshot->Validate(Map, Ids, Reason)) { return Fail(Reason); }
	TArray<uint8> Bytes;
	if (!Snapshot->Encode(Bytes))
	{
		return Fail(LOCTEXT("SerializeFailed", "Progress could not be prepared for saving. The previous checkpoint is unchanged."));
	}
	// One logical checkpoint, two rotating files. Never overwrite the newest
	// valid snapshot: a partial disk write leaves the last journey recoverable.
	const FString TargetSlot = Slot + (SourceSlot == Slot + TEXT("_A") ? TEXT("_B") : TEXT("_A"));
	if (!IsEnabled() || !UGameplayStatics::SaveDataToSlot(Bytes, TargetSlot, UserIndex))
	{
		return Fail(LOCTEXT("WriteFailed", "Progress could not be written. The previous checkpoint is unchanged."));
	}
	TArray<uint8> Verification;
	if (!UGameplayStatics::LoadDataFromSlot(Verification, TargetSlot, UserIndex) || Verification != Bytes)
	{
		return Fail(LOCTEXT("VerifyFailed", "The checkpoint could not be verified. The previous checkpoint is still available."));
	}
	bAvailabilityCached = false;
	StatusText = LOCTEXT("Saved", "Journey checkpoint saved.");
	UE_LOG(LogUEGT2Progress, Log, TEXT("Saved checkpoint %lld: %s, day %d at %.2f, %.3f coins, %d surveys."),
		Snapshot->Sequence, *TargetSlot, Snapshot->DayIndex, Snapshot->Hour,
		Snapshot->Purse.Coins, Snapshot->DiscoveredLandmarks.Num());
	return true;
}

bool UUEGT2ProgressSubsystem::LoadProgress(AUEGT2PlayerController* Controller)
{
	using namespace UEGT2Progress;
	if (!IsEnabled()) { return Fail(GetStatusText()); }
	UWorld* World = GetWorld();
	AUEGT2Character* Character = Controller && Controller->GetWorld() == World
		? Cast<AUEGT2Character>(Controller->GetPawn()) : nullptr;
	UUEGT2NeedsComponent* Life = Character ? Character->GetLife() : nullptr;
	UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	if (!Life || !Life->HasBegunPlay() || !Director || !AUEGT2SkyController::Get(World))
	{
		return Fail(LOCTEXT("LoadNotReady", "The player and world must finish loading before continuing."));
	}
	FString Slot, SourceSlot;
	FText Reason;
	TSet<FName> Ids;
	if (!ResolveSlot(Slot) || !GatherLandmarks(World, Ids, Reason)) { return Fail(Reason); }
	TStrongObjectPtr<UUEGT2ProgressSave> Snapshot(ReadCheckpoint(Slot, MapIdentity(World), Ids, SourceSlot, Reason));
	if (!Snapshot.IsValid()) { return Fail(Reason); }
	FVector Destination;
	bool bFallback = false;
	if (!FindPlacement(World, Character, Snapshot->PlayerLocation, Destination, bFallback))
	{
		return Fail(LOCTEXT("Blocked", "Neither the checkpoint nor the player start has room to stand. Progress was not loaded."));
	}
	// Everything has been validated before any live state changes. The target
	// passed the full standing capsule query, so do not let current crouch or
	// noclip settings change the teleport's collision policy.
	if (!Character->TeleportTo(Destination, FRotator(0.0, Snapshot->ViewRotation.Yaw, 0.0), false, true))
	{
		return Fail(LOCTEXT("TeleportFailed", "The player could not be moved to the checkpoint."));
	}
	Character->ClearDevMovement();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->ClearAccumulatedForces();
	Movement->SetMovementMode(MOVE_Falling);
	Character->UnCrouch();
	Movement->UnCrouch();
	Controller->SetControlRotation(Snapshot->ViewRotation);
	Controller->CloseDialogue();
	for (TActorIterator<AUEGT2Pickup> It(World); It; ++It)
	{
		It->ReleaseIfCarriedBy(Character);
	}
	Life->RestoreProgress(Snapshot->Needs, Snapshot->Purse, Snapshot->Trade);
	Director->RestoreCalendar(Snapshot->DayIndex, Snapshot->Hour, Snapshot->Weather);
	const TSet<FName> Discovered(Snapshot->DiscoveredLandmarks);
	for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed()) { It->SetDiscovered(Discovered.Contains(It->GetPersistentId())); }
	}
	SetJourneyActive(true);
	StatusText = bFallback
		? LOCTEXT("RestoredAtStart", "Journey restored at the player start because the saved spot was blocked.")
		: LOCTEXT("Restored", "Journey checkpoint restored.");
	UE_LOG(LogUEGT2Progress, Log, TEXT("Loaded checkpoint %lld: %s, day %d at %.2f, %.3f coins, %d surveys%s."),
		Snapshot->Sequence, *SourceSlot, Snapshot->DayIndex, Snapshot->Hour, Snapshot->Purse.Coins,
		Snapshot->DiscoveredLandmarks.Num(), bFallback ? TEXT(", player-start fallback") : TEXT(""));
	return true;
}

#undef LOCTEXT_NAMESPACE
