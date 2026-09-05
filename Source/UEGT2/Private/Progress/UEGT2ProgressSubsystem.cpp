#include "Progress/UEGT2ProgressSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Autosave/UEGT2AutosaveSubsystem.h"
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
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSave.h"
#include "Progress/UEGT2CheckpointStorage.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "UObject/StrongObjectPtr.h"
#include "World/UEGT2SkyController.h"

#define LOCTEXT_NAMESPACE "UEGT2Progress"

/** Only plain data and weak handles cross callback boundaries. No saved UObject. */
struct FUEGT2AutosaveOperation
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AUEGT2PlayerController> Controller;
	uint64 Generation = 0;
	uint64 PersistenceRevision = 0;
	FString Slot;
	FString Map;
	TSet<FName> LandmarkIds;
	FString SourceSlot;
	int64 Sequence = 0;
	bool bWrite = false;
	bool bCaptured = false;
	bool bReadFailed = false;
	FText ReadReason;
	TArray<uint8> Bytes;
};

namespace UEGT2Progress
{
	const TCHAR* DefaultSlot = TEXT("Fairhaven_Journey");
	const TCHAR* SmokePrefix = TEXT("UEGT2_ProgressSmoke_");

	UUEGT2ProgressSave* DecodeCandidate(const TArray<uint8>& Bytes, const FString& Map,
		const TSet<FName>& Ids, FText& Reason)
	{
		UUEGT2ProgressSave* Candidate = UUEGT2ProgressSave::Decode(Bytes, Reason);
		return Candidate && Candidate->Validate(Map, Ids, Reason) ? Candidate : nullptr;
	}

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

bool UUEGT2ProgressSubsystem::ResolveSlot(FString& OutSlot, bool* bOutAutosaveSmoke) const
{
	if (bOutAutosaveSmoke) { *bOutAutosaveSmoke = false; }
	FString Mode, RequestedSlot, AutoMode;
	const bool bSmoke = FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), Mode);
	const bool bOverride = FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSlot="), RequestedSlot);
	const bool bAutoSmoke = FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke="), AutoMode);
	const bool bBareProgress = FParse::Param(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke"));
	const bool bBareAuto = FParse::Param(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke"));
	if (bAutoSmoke || bBareAuto)
	{
		if (!bAutoSmoke || bSmoke || bBareProgress || !bOverride
			|| (AutoMode != TEXT("Write") && AutoMode != TEXT("Read") && AutoMode != TEXT("Disabled"))
			|| !RequestedSlot.StartsWith(UEGT2Progress::SmokePrefix, ESearchCase::CaseSensitive))
		{
			return false;
		}
		const FString RunId = RequestedSlot.Mid(FCString::Strlen(UEGT2Progress::SmokePrefix));
		FGuid Guid;
		FString Directory;
		if (!FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid)
			|| !FParse::Value(FCommandLine::Get(), TEXT("UserDir="), Directory)
			|| Directory.IsEmpty() || FPaths::IsRelative(Directory)) { return false; }
		FPaths::NormalizeDirectoryName(Directory);
		FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(), TEXT("Saved/AutosaveSmoke"), RunId));
		FPaths::NormalizeDirectoryName(Expected);
		if (!Directory.Equals(Expected, ESearchCase::IgnoreCase)) { return false; }
		OutSlot = RequestedSlot;
		if (bOutAutosaveSmoke) { *bOutAutosaveSmoke = true; }
		return true;
	}
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

bool UUEGT2ProgressSubsystem::IsAutosaveSmoke() const
{
	FString Slot;
	bool bSmoke = false;
	return ResolveSlot(Slot, &bSmoke) && bSmoke;
}

IUEGT2CheckpointStorage& UUEGT2ProgressSubsystem::GetStorage() const
{
	if (!Storage.IsValid()) { Storage = UEGT2CreateCheckpointStorage(); }
	return *Storage;
}

bool UUEGT2ProgressSubsystem::IsAvailable() const
{
	FString Slot;
	return !bShuttingDown && bFeatureEnabled && !IsRunningCommandlet()
		&& !UUEGT2CaptureSubsystem::IsCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsLifeCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsWalkSmokeRequested()
		&& !UUEGT2CaptureSubsystem::IsFlySoakRequested()
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2HudSizeSmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2AutoWalkSmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2ServicesSmoke"))
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
		if (!GetStorage().Exists(PhysicalSlot)) { continue; }
		if (bOutHadFile) { *bOutHadFile = true; }
		TArray<uint8> Bytes;
		if (!GetStorage().Read(PhysicalSlot, Bytes)
			|| Bytes.IsEmpty() || Bytes.Num() > UUEGT2ProgressSave::MaxEncodedBytes)
		{
			OutReason = LOCTEXT("Unreadable", "The journey checkpoint could not be read.");
			continue;
		}
		TStrongObjectPtr<UUEGT2ProgressSave> Candidate(UEGT2Progress::DecodeCandidate(Bytes, Map, LandmarkIds, OutReason));
		if (!Candidate.IsValid())
		{
			continue;
		}
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
	if (bJourneyActive != bActive || (bActive && JourneyWorld.Get() != GetWorld()))
	{
		InvalidateAutosaveContext();
	}
	bJourneyActive = bActive;
	JourneyWorld = bActive ? GetWorld() : nullptr;
	bAvailabilityCached = false;
}

void UUEGT2ProgressSubsystem::RequestNewJourney()
{
	bNewJourneyRequested = true;
	InvalidateAutosaveContext();
	SetJourneyActive(false);
	StatusText = FText::GetEmpty();
}

bool UUEGT2ProgressSubsystem::ConsumeNewJourneyRequest()
{
	const bool bRequested = bNewJourneyRequested;
	bNewJourneyRequested = false;
	return bRequested;
}

bool UUEGT2ProgressSubsystem::IsJourneyActive(const UWorld* World) const
{
	return !bShuttingDown && bJourneyActive && World && World == GetWorld() && JourneyWorld.Get() == World;
}

void UUEGT2ProgressSubsystem::InvalidateAutosaveContext()
{
	++JourneyGeneration;
	bAutosaveCacheValid = false;
	AutosaveStatus = FUEGT2AutosaveStatus();
	AutosaveCacheWorld.Reset();
	AutosaveCacheSlot.Reset();
	PendingAutosaveRefresh.Reset();
	// An already submitted platform operation has no cancel handle. Retain its
	// identity until completion so a new world cannot start a competing writer.
}

void UUEGT2ProgressSubsystem::Deinitialize()
{
	bShuttingDown = true;
	InvalidateAutosaveContext();
	AutosaveOperation.Reset();
	Storage.Reset();
	Super::Deinitialize();
}

FUEGT2AutosaveStatus UUEGT2ProgressSubsystem::GetAutosaveStatus() const
{
	FUEGT2AutosaveStatus Result = AutosaveStatus;
	Result.SuccessfulWrites = AutosaveSuccessfulWrites;
	Result.bBusy = AutosaveOperation.IsValid();
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	if (!Autosave || !Autosave->IsEnabled())
	{
		Result.bAvailable = false;
		Result.Text = LOCTEXT("AutoDisabled", "Autosave is turned off. Existing checkpoints are kept.");
		return Result;
	}
	FString Slot;
	if (!ResolveSlot(Slot) || !bAutosaveCacheValid || AutosaveCacheWorld.Get() != GetWorld()
		|| AutosaveCacheSlot != Slot + TEXT("_Auto")
		|| AutosaveCacheRevision != UUEGT2GameUserSettings::Get()->GetPersistenceRevision())
	{
		Result.bAvailable = false;
		if (bAutosaveCacheValid)
		{
			Result.Text = LOCTEXT("AutoRefreshNeeded", "The automatic checkpoint needs to be checked again.");
		}
	}
	if (Result.bBusy) { Result.Text = LOCTEXT("AutoBusy", "Checking or saving the automatic checkpoint..."); }
	return Result;
}

bool UUEGT2ProgressSubsystem::RequestAutosave(AUEGT2PlayerController* Controller)
{
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	return Autosave && Autosave->CanAutosaveNow(Controller) && StartAutosaveOperation(Controller, true);
}

void UUEGT2ProgressSubsystem::RefreshAutosaveAvailability(AUEGT2PlayerController* Controller)
{
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	FString Slot;
	if (!IsValid(Controller) || Controller->GetWorld() != GetWorld()
		|| Controller->GetMenuState() != EUEGT2MenuState::Main
		|| !Autosave || !Autosave->IsEnabled() || !ResolveSlot(Slot)) { return; }
	if (bAutosaveCacheValid && AutosaveCacheWorld.Get() == GetWorld()
		&& AutosaveCacheSlot == Slot + TEXT("_Auto")
		&& AutosaveCacheRevision == UUEGT2GameUserSettings::Get()->GetPersistenceRevision()) { return; }
	if (AutosaveOperation.IsValid())
	{
		PendingAutosaveRefresh = Controller;
		return;
	}
	StartAutosaveOperation(Controller, false);
}

bool UUEGT2ProgressSubsystem::StartAutosaveOperation(AUEGT2PlayerController* Controller, bool bWrite)
{
	check(IsInGameThread());
	if (bShuttingDown || AutosaveOperation.IsValid() || !IsEnabled()
		|| !IsValid(Controller) || Controller->GetWorld() != GetWorld()) { return false; }
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	if (!Autosave || !Autosave->IsEnabled()) { return false; }
	const auto Operation = MakeShared<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>();
	if (!ResolveSlot(Operation->Slot)
		|| !UEGT2Progress::GatherLandmarks(GetWorld(), Operation->LandmarkIds, Operation->ReadReason))
	{
		AutosaveStatus.Text = Operation->ReadReason;
		return false;
	}
	Operation->Slot += TEXT("_Auto");
	Operation->World = GetWorld();
	Operation->Controller = Controller;
	Operation->Generation = JourneyGeneration;
	Operation->PersistenceRevision = UUEGT2GameUserSettings::Get()->GetPersistenceRevision();
	Operation->Map = UEGT2Progress::MapIdentity(GetWorld());
	Operation->bWrite = bWrite;
	Operation->ReadReason = LOCTEXT("AutoMissing", "No compatible automatic checkpoint is available yet.");
	AutosaveOperation = Operation;
	UE_LOG(LogUEGT2Autosave, Log, TEXT("Autosave %s started for %s."), bWrite ? TEXT("request") : TEXT("availability check"), *Operation->Slot);
	ReadAutosaveSlot(Operation, 0);
	return true;
}

bool UUEGT2ProgressSubsystem::IsAutosaveOperationCurrent(
	const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation) const
{
	const UUEGT2GameUserSettings* Settings = UUEGT2GameUserSettings::Get();
	if (!Settings || bShuttingDown || AutosaveOperation != Operation || Operation->Generation != JourneyGeneration
		|| Operation->PersistenceRevision != Settings->GetPersistenceRevision()
		|| !Operation->World.IsValid() || Operation->World.Get() != GetWorld()
		|| !Operation->Controller.IsValid() || Operation->Controller->GetWorld() != GetWorld()) { return false; }
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	FString Slot;
	return Autosave && Autosave->IsEnabled() && ResolveSlot(Slot)
		&& Slot + TEXT("_Auto") == Operation->Slot
		&& (!Operation->bWrite || Operation->bCaptured || Autosave->CanAutosaveNow(Operation->Controller.Get()));
}

void UUEGT2ProgressSubsystem::ReadAutosaveSlot(
	const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation, int32 Index)
{
	if (!IsAutosaveOperationCurrent(Operation))
	{
		FinishAutosave(Operation, false, LOCTEXT("AutoInterrupted", "Autosave deferred because the visit or saving controls changed."));
		return;
	}
	const FString PhysicalSlot = Operation->Slot + (Index == 0 ? TEXT("_A") : TEXT("_B"));
	TWeakObjectPtr<UUEGT2ProgressSubsystem> WeakThis(this);
	GetStorage().ExistsAsync(PhysicalSlot, [WeakThis, Operation, PhysicalSlot, Index](IUEGT2CheckpointStorage::EPresence Presence)
	{
		UUEGT2ProgressSubsystem* Self = WeakThis.Get();
		if (!Self) { return; }
		if (!Self->IsAutosaveOperationCurrent(Operation))
		{
			Self->FinishAutosave(Operation, false, LOCTEXT("AutoReadInterrupted", "Autosave deferred because the visit or saving controls changed."));
			return;
		}
		if (Presence == IUEGT2CheckpointStorage::EPresence::Missing)
		{
			Self->ConsumeAutosaveSlot(Operation, Index, false, TArray<uint8>());
			return;
		}
		if (Presence == IUEGT2CheckpointStorage::EPresence::Unreadable)
		{
			Operation->bReadFailed = true;
			Operation->ReadReason = LOCTEXT("AutoPresenceFailed", "The automatic checkpoint could not be checked. Existing checkpoints are kept.");
			if (Operation->bWrite) { Self->FinishAutosave(Operation, false, Operation->ReadReason); }
			else { Self->ConsumeAutosaveSlot(Operation, Index, false, TArray<uint8>()); }
			return;
		}
		Self->GetStorage().ReadAsync(PhysicalSlot, [WeakThis, Operation, Index](bool bRead, const TArray<uint8>& Bytes)
		{
			UUEGT2ProgressSubsystem* Owner = WeakThis.Get();
			if (!Owner) { return; }
			// An existing unreadable file might be the newest good checkpoint.
			// Never treat a transport failure as an empty rotation slot.
			if (!Owner->IsAutosaveOperationCurrent(Operation))
			{
				Owner->FinishAutosave(Operation, false, LOCTEXT("AutoLoadInterrupted", "Autosave deferred because the visit or saving controls changed."));
				return;
			}
			if (!bRead)
			{
				Operation->bReadFailed = true;
				Operation->ReadReason = LOCTEXT("AutoTransportFailed", "The automatic checkpoint could not be read. Existing checkpoints are kept.");
				if (Operation->bWrite) { Owner->FinishAutosave(Operation, false, Operation->ReadReason); }
				else { Owner->ConsumeAutosaveSlot(Operation, Index, false, TArray<uint8>()); }
				return;
			}
			Owner->ConsumeAutosaveSlot(Operation, Index, true, Bytes);
		});
	});
}

void UUEGT2ProgressSubsystem::ConsumeAutosaveSlot(
	const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation,
	int32 Index, bool bPresent, const TArray<uint8>& Bytes)
{
	TWeakObjectPtr<UUEGT2ProgressSubsystem> WeakThis(this);
	const FString PhysicalSlot = Operation->Slot + (Index == 0 ? TEXT("_A") : TEXT("_B"));
	if (bPresent)
	{
		TStrongObjectPtr<UUEGT2ProgressSave> Candidate(UEGT2Progress::DecodeCandidate(
			Bytes, Operation->Map, Operation->LandmarkIds, Operation->ReadReason));
		if (Candidate.IsValid() && Candidate->Sequence > Operation->Sequence)
		{
			Operation->Sequence = Candidate->Sequence;
			Operation->SourceSlot = PhysicalSlot;
		}
	}
	if (Index == 0) { ReadAutosaveSlot(Operation, 1); return; }
	if (!Operation->bWrite)
	{
		// A transport failure does not establish absence. Leave that result
		// retryable on the next explicit refresh; a valid fallback is conclusive.
		FinishAutosave(Operation, Operation->Sequence > 0 || !Operation->bReadFailed, Operation->Sequence > 0
			? LOCTEXT("AutoReady", "Your automatic checkpoint is ready to continue.") : Operation->ReadReason);
		return;
	}
	// Snapshot live objects only after the reads finish; time may have moved
	// while the worker was busy. Encoding and all validation stay on this thread.
	TStrongObjectPtr<UUEGT2ProgressSave> Snapshot(NewObject<UUEGT2ProgressSave>());
	FText Reason;
	if (!CaptureSnapshot(Operation->Controller.Get(), Operation->Sequence + 1, *Snapshot.Get(), Reason)
		|| !Snapshot->Validate(Operation->Map, Operation->LandmarkIds, Reason)
		|| !Snapshot->Encode(Operation->Bytes))
	{
		FinishAutosave(Operation, false, Reason.IsEmpty()
			? LOCTEXT("AutoEncodeFailed", "The automatic checkpoint could not be prepared.") : Reason);
		return;
	}
	Operation->bCaptured = true;
	const FString Target = Operation->Slot
		+ (Operation->SourceSlot == Operation->Slot + TEXT("_A") ? TEXT("_B") : TEXT("_A"));
	GetStorage().WriteAsync(Target, Operation->Bytes, [WeakThis, Operation, Target](bool bWritten)
	{
		UUEGT2ProgressSubsystem* Owner = WeakThis.Get();
		if (!Owner) { return; }
		if (!Owner->IsAutosaveOperationCurrent(Operation) || !bWritten)
		{
			Owner->FinishAutosave(Operation, false, LOCTEXT("AutoWriteFailed", "Autosave was interrupted or could not be written. The previous checkpoint is kept."));
			return;
		}
		Owner->GetStorage().ReadAsync(Target, [WeakThis, Operation](bool bVerified, const TArray<uint8>& Verification)
		{
			UUEGT2ProgressSubsystem* Current = WeakThis.Get();
			if (!Current) { return; }
			const bool bSuccess = Current->IsAutosaveOperationCurrent(Operation)
				&& bVerified && Verification == Operation->Bytes;
			Current->FinishAutosave(Operation, bSuccess, bSuccess
				? LOCTEXT("AutoSaved", "Automatic checkpoint saved. Your manual checkpoint is unchanged.")
				: LOCTEXT("AutoVerifyFailed", "Autosave could not be verified. The previous checkpoint is kept."));
		});
	});
}

void UUEGT2ProgressSubsystem::FinishAutosave(
	const TSharedRef<FUEGT2AutosaveOperation, ESPMode::ThreadSafe>& Operation, bool bSuccess, const FText& Reason)
{
	check(IsInGameThread());
	if (AutosaveOperation != Operation) { return; }
	const bool bCurrent = IsAutosaveOperationCurrent(Operation);
	AutosaveOperation.Reset();
	if (bSuccess && bCurrent)
	{
		AutosaveCacheWorld = Operation->World;
		AutosaveCacheSlot = Operation->Slot;
		AutosaveCacheRevision = Operation->PersistenceRevision;
		bAutosaveCacheValid = true;
		AutosaveStatus.bAvailable = Operation->bWrite || Operation->Sequence > 0;
		if (Operation->bWrite) { ++AutosaveSuccessfulWrites; }
	}
	else
	{
		bAutosaveCacheValid = false;
		AutosaveStatus.bAvailable = false;
	}
	if (bCurrent) { AutosaveStatus.Text = Reason; }
	UE_LOG(LogUEGT2Autosave, Log, TEXT("Autosave %s: %s"),
		bSuccess && bCurrent ? TEXT("completed") : TEXT("not completed"), *Reason.ToString());
	AUEGT2PlayerController* Refresh = PendingAutosaveRefresh.Get();
	PendingAutosaveRefresh.Reset();
	if (Refresh && !bShuttingDown) { RefreshAutosaveAvailability(Refresh); }
}

bool UUEGT2ProgressSubsystem::CaptureSnapshot(AUEGT2PlayerController* Controller, int64 Sequence,
	UUEGT2ProgressSave& Snapshot, FText& Reason) const
{
	using namespace UEGT2Progress;
	UWorld* World = GetWorld();
	AUEGT2Character* Character = IsValid(Controller) && Controller->GetWorld() == World
		? Cast<AUEGT2Character>(Controller->GetPawn()) : nullptr;
	UUEGT2NeedsComponent* Life = Character ? Character->GetLife() : nullptr;
	UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	AUEGT2SkyController* Sky = AUEGT2SkyController::Get(World);
	if (!Life || !Life->HasBegunPlay() || !Director || !Sky)
	{
		Reason = LOCTEXT("CaptureNotReady", "The player and world must finish loading before using a checkpoint.");
		return false;
	}
	TSet<FName> Ids;
	if (!GatherLandmarks(World, Ids, Reason)) { return false; }
	const FString Map = MapIdentity(World);
	Snapshot.DiscoveredLandmarks.Reset();
	Snapshot.Sequence = Sequence;
	Snapshot.MapPackageName = Map;
	Snapshot.PlayerLocation = Character->GetActorLocation();
	Snapshot.PlayerLocation.Z += StandingHalfHeight(Character) - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Snapshot.ViewRotation = Controller->GetControlRotation().GetNormalized();
	Snapshot.Needs = Life->GetNeeds();
	Snapshot.Purse = Life->GetPurse();
	Snapshot.Trade = Life->GetTrade();
	Snapshot.DayIndex = Director->GetDayIndex();
	Snapshot.Hour = Sky->GetTimeOfDay();
	// A pause can land between the sky's actor tick and the director's tick.
	// Capture the same midnight crossing the director will observe next.
	if (Snapshot.Hour + 12.0f < Director->GetHour()) { ++Snapshot.DayIndex; }
	Snapshot.Weather = Sky->GetWeather();
	for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && It->IsDiscovered()) { Snapshot.DiscoveredLandmarks.Add(It->GetPersistentId()); }
	}
	Snapshot.DiscoveredLandmarks.Sort(FNameLexicalLess());
	return Snapshot.Validate(Map, Ids, Reason);
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
	if (!CaptureSnapshot(Controller, Previous.IsValid() ? Previous->Sequence + 1 : 1, *Snapshot.Get(), Reason))
	{
		return Fail(Reason);
	}
	TArray<uint8> Bytes;
	if (!Snapshot->Encode(Bytes))
	{
		return Fail(LOCTEXT("SerializeFailed", "Progress could not be prepared for saving. The previous checkpoint is unchanged."));
	}
	// One logical checkpoint, two rotating files. Never overwrite the newest
	// valid snapshot: a partial disk write leaves the last journey recoverable.
	const FString TargetSlot = Slot + (SourceSlot == Slot + TEXT("_A") ? TEXT("_B") : TEXT("_A"));
	if (!IsEnabled() || !GetStorage().Write(TargetSlot, Bytes))
	{
		return Fail(LOCTEXT("WriteFailed", "Progress could not be written. The previous checkpoint is unchanged."));
	}
	TArray<uint8> Verification;
	if (!GetStorage().Read(TargetSlot, Verification) || Verification != Bytes)
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

bool UUEGT2ProgressSubsystem::RestoreSnapshot(AUEGT2PlayerController* Controller,
	const UUEGT2ProgressSave& Snapshot, bool& bFallback, FText& Reason)
{
	using namespace UEGT2Progress;
	UWorld* World = GetWorld();
	AUEGT2Character* Character = IsValid(Controller) && Controller->GetWorld() == World
		? Cast<AUEGT2Character>(Controller->GetPawn()) : nullptr;
	UUEGT2NeedsComponent* Life = Character ? Character->GetLife() : nullptr;
	UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	TSet<FName> Ids;
	if (!Life || !Life->HasBegunPlay() || !Director || !AUEGT2SkyController::Get(World)
		|| !GatherLandmarks(World, Ids, Reason) || !Snapshot.Validate(MapIdentity(World), Ids, Reason))
	{
		if (Reason.IsEmpty()) { Reason = LOCTEXT("RestoreNotReady", "The player and world are not ready to restore progress."); }
		return false;
	}
	FVector Destination;
	if (!FindPlacement(World, Character, Snapshot.PlayerLocation, Destination, bFallback))
	{
		Reason = LOCTEXT("Blocked", "Neither the checkpoint nor the player start has room to stand. Progress was not loaded.");
		return false;
	}
	// Everything has been validated before any live state changes. The target
	// passed the full standing capsule query, so do not let current crouch or
	// noclip settings change the teleport's collision policy.
	if (!Character->TeleportTo(Destination, FRotator(0.0, Snapshot.ViewRotation.Yaw, 0.0), false, true))
	{
		Reason = LOCTEXT("TeleportFailed", "The player could not be moved to the checkpoint.");
		return false;
	}
	Character->ClearDevMovement();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->ClearAccumulatedForces();
	Movement->SetMovementMode(MOVE_Falling);
	Character->UnCrouch();
	Movement->UnCrouch();
	Controller->SetControlRotation(Snapshot.ViewRotation);
	Controller->CloseDialogue();
	for (TActorIterator<AUEGT2Pickup> It(World); It; ++It)
	{
		It->ReleaseIfCarriedBy(Character);
	}
	Life->RestoreProgress(Snapshot.Needs, Snapshot.Purse, Snapshot.Trade);
	Director->RestoreCalendar(Snapshot.DayIndex, Snapshot.Hour, Snapshot.Weather);
	const TSet<FName> Discovered(Snapshot.DiscoveredLandmarks);
	for (TActorIterator<AUEGT2Landmark> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed()) { It->SetDiscovered(Discovered.Contains(It->GetPersistentId())); }
	}
	return true;
}

bool UUEGT2ProgressSubsystem::LoadProgress(AUEGT2PlayerController* Controller)
{
	return LoadCheckpoint(Controller, false);
}

bool UUEGT2ProgressSubsystem::LoadAutosavedProgress(AUEGT2PlayerController* Controller)
{
	const UUEGT2AutosaveSubsystem* Autosave = UUEGT2AutosaveSubsystem::Get(GetWorld());
	if (!Autosave || !Autosave->IsEnabled() || AutosaveOperation.IsValid()
		|| !IsValid(Controller) || Controller->GetWorld() != GetWorld()
		|| Controller->GetMenuState() != EUEGT2MenuState::Main)
	{
		return false;
	}
	return LoadCheckpoint(Controller, true);
}

bool UUEGT2ProgressSubsystem::LoadCheckpoint(AUEGT2PlayerController* Controller, bool bAutosave)
{
	using namespace UEGT2Progress;
	const auto ReportFailure = [this, bAutosave](const FText& Reason)
	{
		if (!bAutosave) { return Fail(Reason); }
		bAutosaveCacheValid = false;
		AutosaveStatus.bAvailable = false;
		AutosaveStatus.Text = Reason;
		UE_LOG(LogUEGT2Autosave, Log, TEXT("Autosave restore not completed: %s"), *Reason.ToString());
		return false;
	};
	if (!IsEnabled()) { return ReportFailure(GetStatusText()); }
	UWorld* World = GetWorld();
	AUEGT2Character* Character = Controller && Controller->GetWorld() == World
		? Cast<AUEGT2Character>(Controller->GetPawn()) : nullptr;
	UUEGT2NeedsComponent* Life = Character ? Character->GetLife() : nullptr;
	UUEGT2NPCDirector* Director = UUEGT2NPCDirector::Get(World);
	if (!Life || !Life->HasBegunPlay() || !Director || !AUEGT2SkyController::Get(World))
	{
		return ReportFailure(LOCTEXT("LoadNotReady", "The player and world must finish loading before continuing."));
	}
	FString Slot, SourceSlot;
	FText Reason;
	TSet<FName> Ids;
	if (!ResolveSlot(Slot) || !GatherLandmarks(World, Ids, Reason)) { return ReportFailure(Reason); }
	if (bAutosave) { Slot += TEXT("_Auto"); }
	TStrongObjectPtr<UUEGT2ProgressSave> Snapshot(ReadCheckpoint(Slot, MapIdentity(World), Ids, SourceSlot, Reason));
	if (!Snapshot.IsValid()) { return ReportFailure(Reason); }
	bool bFallback = false;
	if (!RestoreSnapshot(Controller, *Snapshot.Get(), bFallback, Reason)) { return ReportFailure(Reason); }
	InvalidateAutosaveContext();
	SetJourneyActive(true);
	FText& ResultText = bAutosave ? AutosaveStatus.Text : StatusText;
	ResultText = bFallback
		? LOCTEXT("RestoredAtStart", "Journey restored at the player start because the saved spot was blocked.")
		: LOCTEXT("Restored", "Journey checkpoint restored.");
	UE_LOG(LogUEGT2Progress, Log, TEXT("Loaded checkpoint %lld: %s, day %d at %.2f, %.3f coins, %d surveys%s."),
		Snapshot->Sequence, *SourceSlot, Snapshot->DayIndex, Snapshot->Hour, Snapshot->Purse.Coins,
		Snapshot->DiscoveredLandmarks.Num(), bFallback ? TEXT(", player-start fallback") : TEXT(""));
	return true;
}

#undef LOCTEXT_NAMESPACE
