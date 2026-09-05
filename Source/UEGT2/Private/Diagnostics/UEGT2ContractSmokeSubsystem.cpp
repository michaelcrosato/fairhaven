#include "Diagnostics/UEGT2ContractSmokeSubsystem.h"

#include "Contracts/UEGT2SurveyContract.h"
#include "Contracts/UEGT2SurveyContractSubsystem.h"
#include "Diagnostics/UEGT2CaptureSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "Interaction/UEGT2InteractionComponent.h"
#include "Interaction/UEGT2WorldInteractables.h"
#include "Kismet/GameplayStatics.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NPC/UEGT2NPCDirector.h"
#include "Player/UEGT2Character.h"
#include "Player/UEGT2NeedsComponent.h"
#include "Player/UEGT2PlayerController.h"
#include "Progress/UEGT2ProgressSubsystem.h"
#include "Settings/UEGT2GameUserSettings.h"
#include "UEGT2LogChannels.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "World/UEGT2SkyController.h"

namespace UEGT2ContractSmoke
{
	constexpr float StartingCoins = 137.625f;
	constexpr float PaidCoins = 155.625f;
	constexpr int32 SavedDay = 7;
	constexpr float SavedHour = 13.25f;
	FUEGT2NPCNeeds Needs()
	{
		FUEGT2NPCNeeds Value; Value.Energy = 0.73f; Value.Fed = 0.42f; Value.Relief = 0.61f; Value.Company = 0.28f; return Value;
	}
	struct FContext
	{
		AUEGT2PlayerController* PC = nullptr;
		AUEGT2Character* Player = nullptr;
		UUEGT2NeedsComponent* Life = nullptr;
		UUEGT2SurveyContractSubsystem* Contract = nullptr;
		UUEGT2ProgressSubsystem* Progress = nullptr;
		UUEGT2NPCDirector* Director = nullptr;
		UUEGT2GameUserSettings* Settings = nullptr;
		AUEGT2SkyController* Sky = nullptr;
		explicit FContext(UWorld* World)
		{
			PC = World ? Cast<AUEGT2PlayerController>(World->GetFirstPlayerController()) : nullptr;
			Player = PC ? Cast<AUEGT2Character>(PC->GetPawn()) : nullptr; Life = Player ? Player->GetLife() : nullptr;
			Contract = UUEGT2SurveyContractSubsystem::Get(World); Progress = UUEGT2ProgressSubsystem::Get(World);
			Director = UUEGT2NPCDirector::Get(World); Settings = UUEGT2GameUserSettings::Get(); Sky = AUEGT2SkyController::Get(World);
		}
		bool IsValid() const { return PC && Player && Life && Life->HasBegunPlay() && Contract && Progress && Director && Settings && Sky; }
	};
	struct FSlotBytes
	{
		TArray<uint8> A, B;
		bool bA = false, bB = false;
		bool Read(const FString& Slot)
		{
			A.Reset(); B.Reset(); bA = UGameplayStatics::DoesSaveGameExist(Slot + TEXT("_A"), 0); bB = UGameplayStatics::DoesSaveGameExist(Slot + TEXT("_B"), 0);
			return (!bA || UGameplayStatics::LoadDataFromSlot(A, Slot + TEXT("_A"), 0))
				&& (!bB || UGameplayStatics::LoadDataFromSlot(B, Slot + TEXT("_B"), 0));
		}
		bool HasSave() const { return bA || bB; }
		bool Equals(const FSlotBytes& Other) const { return bA == Other.bA && bB == Other.bB && A == Other.A && B == Other.B; }
	};
	bool bTravelRequested = false;
	FSlotBytes BeforeTravel, BeforeDisabled;
	TSharedPtr<SWidget> FindContractText(const TSharedRef<SWidget>& Widget, const FText& Caption, int32& Budget, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return nullptr; }
		if (Widget->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Caption.ToString()) { return Widget; }
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindContractText(Children->GetChildAt(Index), Caption, Budget, Depth + 1)) { return Found; }
		}
		return nullptr;
	}
	TSharedPtr<SWidget> FindContractButton(const TSharedRef<SWidget>& Widget, const FText& Caption, int32& Budget, int32 Depth = 0)
	{
		if (--Budget < 0 || Depth > 32 || !Widget->GetVisibility().IsVisible()) { return nullptr; }
		FChildren* Children = Widget->GetChildren();
		if (Widget->GetType() == TEXT("SButton") && Children->Num() == 1)
		{
			const TSharedRef<SWidget> Child = Children->GetChildAt(0);
			if (Child->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Child)->GetText().ToString() == Caption.ToString()) { return Widget; }
		}
		for (int32 Index = 0; Index < Children->Num() && Budget > 0; ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindContractButton(Children->GetChildAt(Index), Caption, Budget, Depth + 1)) { return Found; }
		}
		return nullptr;
	}
}

bool UUEGT2ContractSmokeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString Value; return FParse::Value(FCommandLine::Get(), TEXT("UEGT2ContractSmoke="), Value) && Super::ShouldCreateSubsystem(Outer);
}
bool UUEGT2ContractSmokeSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game; }
TStatId UUEGT2ContractSmokeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UUEGT2ContractSmokeSubsystem, STATGROUP_Tickables); }
void UUEGT2ContractSmokeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld); bRequested = true; StartedSeconds = FPlatformTime::Seconds(); bAfterTravel = UEGT2ContractSmoke::bTravelRequested;
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2ContractSmoke="), Phase); FParse::Value(FCommandLine::Get(), TEXT("UEGT2ContractSlot="), Slot);
	FString UserDirectory, Other; FParse::Value(FCommandLine::Get(), TEXT("UserDir="), UserDirectory); FPaths::NormalizeDirectoryName(UserDirectory);
	RunId = FPaths::GetCleanFilename(UserDirectory); FGuid Guid;
	FString Expected = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved/ContractSmoke"), RunId)); FPaths::NormalizeDirectoryName(Expected);
	if (!Check(FGuid::ParseExact(RunId, EGuidFormats::Digits, Guid) && Slot == TEXT("UEGT2_ContractSmoke_") + RunId
		&& !FPaths::IsRelative(UserDirectory) && UserDirectory.Equals(Expected, ESearchCase::IgnoreCase), TEXT("expected matching ContractSmoke GUID slot and packaged UserDir"))) { return; }
	if (!Check(Phase == TEXT("Write") || Phase == TEXT("Read") || Phase == TEXT("NewVisit") || Phase == TEXT("Disabled"), TEXT("unknown contract smoke phase"))) { return; }
	if (!Check(!UUEGT2CaptureSubsystem::IsCaptureRequested() && !UUEGT2CaptureSubsystem::IsLifeCaptureRequested()
		&& !UUEGT2CaptureSubsystem::IsWalkSmokeRequested() && !UUEGT2CaptureSubsystem::IsFlySoakRequested()
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2SurveySmoke")) && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2RestSmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2HudSizeSmoke")) && !FParse::Param(FCommandLine::Get(), TEXT("UEGT2AutoWalkSmoke"))
		&& !FParse::Param(FCommandLine::Get(), TEXT("UEGT2ServicesSmoke"))
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSmoke="), Other) && !FParse::Value(FCommandLine::Get(), TEXT("UEGT2ProgressSlot="), Other)
		&& !FParse::Value(FCommandLine::Get(), TEXT("UEGT2AutosaveSmoke="), Other), TEXT("contract smoke cannot share another diagnostic"))) { return; }
	FParse::Value(FCommandLine::Get(), TEXT("UEGT2ContractCapture="), CaptureDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("ResX="), ExpectedWidth); FParse::Value(FCommandLine::Get(), TEXT("ResY="), ExpectedHeight);
	if (!CaptureDirectory.IsEmpty())
	{
		if (!Check(Phase == TEXT("Write") && !FPaths::IsRelative(CaptureDirectory) && FPaths::GetCleanFilename(CaptureDirectory) == RunId
			&& IFileManager::Get().MakeDirectory(*CaptureDirectory, true), TEXT("contract capture needs Write and an absolute GUID-owned directory"))) { return; }
		ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &UUEGT2ContractSmokeSubsystem::HandleScreenshot);
	}
	UE_LOG(LogUEGT2Diag, Log, TEXT("Contract smoke starting: phase=%s slot=%s resolution=%dx%d"), *Phase, *Slot, ExpectedWidth, ExpectedHeight);
}
void UUEGT2ContractSmokeSubsystem::Deinitialize()
{
	UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle); RestorePreferences(); Super::Deinitialize();
}
void UUEGT2ContractSmokeSubsystem::RestorePreferences()
{
	if (!bChanged) { return; } UEGT2ContractSmoke::FContext C(GetWorld());
	if (C.Settings) { C.Settings->SetSaveProgressEnabled(bOriginalSave); C.Settings->SetAutosaveEnabled(bOriginalAutosave); C.Settings->SetTownSurveyContractEnabled(bOriginalContract); }
	if (C.Contract) { C.Contract->bFeatureEnabled = bOriginalGate; }
	if (C.Sky) { C.Sky->SetDayNightCycleEnabled(bOriginalClock); }
	bChanged = false;
}
bool UUEGT2ContractSmokeSubsystem::Check(bool Condition, const TCHAR* Reason) { if (!Condition) { Finish(false, Reason); } return Condition; }
void UUEGT2ContractSmokeSubsystem::Finish(bool Success, const TCHAR* Reason)
{
	if (bFinished) { return; } bFinished = true; RestorePreferences();
	if (Success) { UE_LOG(LogUEGT2Diag, Log, TEXT("UEGT2_CONTRACT_SMOKE_COMPLETE phase=%s slot=%s %s"), *Phase, *Slot, Reason); }
	else { UE_LOG(LogUEGT2Diag, Error, TEXT("UEGT2_CONTRACT_SMOKE_FAILED phase=%s slot=%s step=%d %s"), *Phase, *Slot, static_cast<int32>(Step), Reason); }
	FPlatformMisc::RequestExitWithStatus(false, Success ? 0 : 1);
}
void UUEGT2ContractSmokeSubsystem::SetStep(EStep Next) { Step = Next; StepStartedSeconds = FPlatformTime::Seconds(); }
void UUEGT2ContractSmokeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime); if (bFinished) { return; } const double Now = FPlatformTime::Seconds();
	if (Now - StartedSeconds > 150.0) { Finish(false, TEXT("contract phase exceeded 150 seconds")); return; }
	if (Step == EStep::Startup) { if (Now - StartedSeconds >= 8.0) { StartCheck(); } return; }
	if (Step == EStep::BoardImage || Step == EStep::IncompleteImage || Step == EStep::EligibleImage || Step == EStep::PaidImage || Step == EStep::SettingsImage)
	{
		if (bScreenshotComplete && Now - StepStartedSeconds >= 0.3) { Advance(); }
		else if (!bScreenshotRequested && Now - StepStartedSeconds >= 1.0) { bScreenshotRequested = true; PendingFile = CaptureFile; FScreenshotRequest::RequestScreenshot(true); }
		else if (Now - StepStartedSeconds > 30.0) { Finish(false, TEXT("contract screenshot callback timed out")); }
		return;
	}
	if (Step != EStep::WaitingForTravel && Now - StepStartedSeconds >= 0.3) { Advance(); }
}
void UUEGT2ContractSmokeSubsystem::StartCheck()
{
	using namespace UEGT2ContractSmoke; FContext C(GetWorld());
	if (!Check(C.IsValid() && C.Player->GetInteraction() && FSlateApplication::IsInitialized(), TEXT("contract fixture context unavailable"))) { return; }
	for (TActorIterator<AUEGT2SurveyContract> It(GetWorld()); It; ++It)
	{
		if (!Check(!Board.IsValid(), TEXT("generated world contains multiple survey contract boards"))) { return; } Board = *It;
	}
	if (!Check(Board.IsValid(), TEXT("generated contract signpost missing"))) { return; }
	for (FName Id : UUEGT2SurveyContractSubsystem::RequiredLandmarkIds())
	{
		AUEGT2Landmark* Target = nullptr;
		for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It)
		{
			if (It->GetPersistentId() == Id) { if (!Check(!Target, TEXT("duplicate required contract landmark"))) { return; } Target = *It; }
		}
		if (!Check(Target != nullptr, *FString::Printf(TEXT("contract marker missing: %s"), *Id.ToString()))) { return; } Targets.Add(Target);
	}
	if (!Check(Targets.Num() == 3 && C.Contract->GetReward() == 18.0f, TEXT("contract objective/reward changed; update the explicit packaged regression"))) { return; }
	bOriginalSave = C.Settings->GetSaveProgressEnabled(); bOriginalAutosave = C.Settings->GetAutosaveEnabled();
	bOriginalContract = C.Settings->GetTownSurveyContractEnabled(); bOriginalGate = C.Contract->bFeatureEnabled; bOriginalClock = C.Sky->IsDayNightCycleEnabled(); bChanged = true;
	C.Settings->SetSaveProgressEnabled(true); C.Settings->SetAutosaveEnabled(false); C.Settings->SetTownSurveyContractEnabled(true); C.Contract->bFeatureEnabled = true;
	if (!Check(C.Progress->IsEnabled(), TEXT("dedicated contract diagnostic slot is not admitted by Progress"))) { return; }
	if (Phase == TEXT("NewVisit") && bAfterTravel)
	{
		FSlotBytes After; APlayerStart* Start = nullptr;
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It) { if (!Start || It->GetName() < Start->GetName()) { Start = *It; } }
		if (!Check(After.Read(Slot) && BeforeTravel.Equals(After) && !C.Contract->IsPaid() && Start
			&& C.PC->GetMenuState() == EUEGT2MenuState::None && C.Director->GetDayIndex() == 0 && AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == 0
			&& C.Life->GetTrade() == EUEGT2NPCRole::Villager && C.Life->GetNeeds().Energy > 0.95f && C.Life->GetNeeds().Fed > 0.95f
			&& C.Life->GetPurse().Coins == UEGT2StartingCoins(EUEGT2NPCRole::Villager) && FVector::Dist2D(C.Player->GetActorLocation(), Start->GetActorLocation()) < 100.0,
			TEXT("New Visit did not reset paid/player/discovery state or changed the paid checkpoint"))) { return; }
		bTravelRequested = false; Finish(true, TEXT("New Visit reset the contract and player while preserving the paid checkpoint")); return;
	}
	C.Sky->SetDayNightCycleEnabled(false); C.PC->ShowPauseMenu(); C.Player->GetCharacterMovement()->StopMovementImmediately();
	C.Contract->RestorePaidState(false);
	for (TActorIterator<AUEGT2Landmark> It(GetWorld()); It; ++It) { It->SetDiscovered(false); }
	if (Phase == TEXT("Write"))
	{
		FSlotBytes Before;
		if (!Check(Before.Read(Slot) && !Before.HasSave() && C.Life->RestoreProgress(Needs(), FUEGT2Purse(StartingCoins), EUEGT2NPCRole::Smith)
			&& C.Director->RestoreCalendar(SavedDay, SavedHour, EUEGT2Weather::Cloudy), TEXT("fresh contract fixture could not initialize"))) { return; }
		C.PC->CloseMenu(); if (PositionAt(Board.Get())) { SetStep(EStep::BoardReady); } return;
	}
	if (!Check(C.Life->RestoreProgress(FUEGT2NPCNeeds(), FUEGT2Purse(3.25f), EUEGT2NPCRole::Villager)
		&& C.Director->RestoreCalendar(1, 5.0f, EUEGT2Weather::Storm), TEXT("cannot seed pre-load contract state"))) { return; }
	if (Phase == TEXT("Disabled"))
	{
		if (!Check(BeforeDisabled.Read(Slot) && BeforeDisabled.HasSave(), TEXT("disabled phase has no prior paid checkpoint"))) { return; }
		C.Settings->SetTownSurveyContractEnabled(false); C.Settings->ApplyNonResolutionSettings();
	}
	if (!Check(C.PC->ContinueProgress(), TEXT("new process could not Continue paid checkpoint")) || !CheckState(PaidCoins, true, 3)) { return; }
	if (Phase == TEXT("NewVisit"))
	{
		if (!Check(BeforeTravel.Read(Slot) && BeforeTravel.HasSave(), TEXT("New Visit has no paid checkpoint"))) { return; }
		C.PC->ShowMainMenu(); bTravelRequested = true; SetStep(EStep::WaitingForTravel); C.PC->StartPlaying(); return;
	}
	if (PositionAt(Board.Get())) { SetStep(Phase == TEXT("Disabled") ? EStep::DisabledBoard : EStep::ReadBoard); }
}
bool UUEGT2ContractSmokeSubsystem::CheckState(float Coins, bool Paid, int32 Discoveries)
{
	using namespace UEGT2ContractSmoke; FContext C(GetWorld());
	if (!Check(C.IsValid(), TEXT("contract state context disappeared"))) { return false; }
	const FUEGT2NPCNeeds& Actual = C.Life->GetNeeds(); const FUEGT2NPCNeeds Expected = Needs();
	const bool SameNeeds = FMath::IsNearlyEqual(Actual.Energy, Expected.Energy, 0.0001f) && FMath::IsNearlyEqual(Actual.Fed, Expected.Fed, 0.0001f)
		&& FMath::IsNearlyEqual(Actual.Relief, Expected.Relief, 0.0001f) && FMath::IsNearlyEqual(Actual.Company, Expected.Company, 0.0001f);
	return Check(SameNeeds && C.Life->GetPurse().Coins == Coins && C.Life->GetTrade() == EUEGT2NPCRole::Smith && C.Contract->IsPaid() == Paid
		&& C.Director->GetDayIndex() == SavedDay && C.Director->GetHour() == SavedHour && C.Sky->GetWeather() == EUEGT2Weather::Cloudy
		&& AUEGT2Landmark::GetDiscoveredCount(GetWorld()) == Discoveries,
		*FString::Printf(TEXT("contract state mismatch: paid=%d/%d coins=%.6f/%.6f needs_delta=(%.9f,%.9f,%.9f,%.9f) trade=%d day=%d hour=%.9f discoveries=%d/%d"),
			C.Contract->IsPaid(), Paid, C.Life->GetPurse().Coins, Coins, Actual.Energy - Expected.Energy, Actual.Fed - Expected.Fed,
			Actual.Relief - Expected.Relief, Actual.Company - Expected.Company, static_cast<int32>(C.Life->GetTrade()), C.Director->GetDayIndex(), C.Director->GetHour(),
			AUEGT2Landmark::GetDiscoveredCount(GetWorld()), Discoveries));
}
bool UUEGT2ContractSmokeSubsystem::PositionAt(AUEGT2InteractableActor* Target)
{
	UEGT2ContractSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && IsValid(Target), TEXT("contract probe target disappeared"))) { return false; }
	const FVector Point = Target->GetInteractionPoint();
	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		const float Angle = FMath::DegreesToRadians(((ApproachIndex + Attempt) % 8) * 45.0f);
		const FVector Candidate(Point.X + FMath::Cos(Angle) * 175.0f, Point.Y + FMath::Sin(Angle) * 175.0f, Target->GetActorLocation().Z + 95.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ContractSmokeApproach), false, C.Player); FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Candidate + FVector(0, 0, 68), Point, ECC_Visibility, Params) && Hit.GetActor() != Target) { continue; }
		ApproachIndex = (ApproachIndex + Attempt + 1) % 8;
		if (!Check(C.Player->TeleportTo(Candidate, C.Player->GetActorRotation(), false, true), TEXT("contract approach teleport failed"))) { return false; }
		C.Player->GetCharacterMovement()->StopMovementImmediately(); C.PC->SetControlRotation((Point - C.Player->GetPawnViewLocation()).Rotation()); return true;
	}
	return Check(false, *FString::Printf(TEXT("all eight interaction approaches blocked for %s"), *GetNameSafe(Target)));
}
bool UUEGT2ContractSmokeSubsystem::Probe(AUEGT2InteractableActor* Target)
{
	UEGT2ContractSmoke::FContext C(GetWorld());
	if (!Check(C.IsValid() && C.Player->GetInteraction() && IsValid(Target), TEXT("contract probe context missing"))) { return false; }
	UUEGT2InteractionComponent* Interaction = C.Player->GetInteraction();
	if (Interaction->GetFocusedActor() != Target)
	{
		if (++ProbeAttempts >= 8) { return Check(false, *FString::Printf(TEXT("probe failed to focus %s after eight attempts; actual=%s"), *GetNameSafe(Target), *GetNameSafe(Interaction->GetFocusedActor()))); }
		if (PositionAt(Target)) { StepStartedSeconds = FPlatformTime::Seconds(); } return false;
	}
	ProbeAttempts = 0; return Check(Interaction->TryInteract(), *FString::Printf(TEXT("real interaction refused %s"), *GetNameSafe(Target)));
}
TSharedPtr<SWidget> UUEGT2ContractSmokeSubsystem::FindButton(const FText& Caption) const
{
	TSharedPtr<SWidget> Root = FSlateApplication::Get().GetKeyboardFocusedWidget();
	for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
	int32 Budget = 512; return Root.IsValid() ? UEGT2ContractSmoke::FindContractButton(Root.ToSharedRef(), Caption, Budget) : nullptr;
}
bool UUEGT2ContractSmokeSubsystem::HasFocus(const FText& Caption) const
{
	const TSharedPtr<SWidget> Button = FindButton(Caption);
	return Button.IsValid() && Button->IsEnabled() && FSlateApplication::Get().GetKeyboardFocusedWidget() == Button;
}
bool UUEGT2ContractSmokeSubsystem::SlateKey(FKey Key)
{
	const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
	const bool Down = FSlateApplication::Get().ProcessKeyDownEvent(Event); const bool Up = FSlateApplication::Get().ProcessKeyUpEvent(Event);
	UE_LOG(LogUEGT2Diag, Log, TEXT("Contract smoke Slate: key=%s down=%d up=%d"), *Key.ToString(), Down, Up);
	return Key != EKeys::Gamepad_FaceButton_Bottom || Check(Down && Up, TEXT("contract button did not accept gamepad A"));
}
void UUEGT2ContractSmokeSubsystem::Advance()
{
	using namespace UEGT2ContractSmoke; FContext C(GetWorld());
	if (!Check(C.IsValid() && Board.IsValid(), TEXT("contract/board context disappeared"))) { return; }
	FText Reason;
	const FText Resume = NSLOCTEXT("UEGT2SurveyContract", "Resume", "Resume");
	const FText Claim = NSLOCTEXT("UEGT2SurveyContract", "Claim", "Claim Payment");
	const FText Paid = NSLOCTEXT("UEGT2SurveyContract", "PaidButton", "Paid");
	switch (Step)
	{
	case EStep::BoardReady:
		if (C.Player->GetInteraction()->GetFocusedActor() != Board.Get())
		{
			if (!Check(++ProbeAttempts < 8, TEXT("cannot focus generated signpost for world screenshot"))) { return; }
			if (PositionAt(Board.Get())) { SetStep(EStep::BoardReady); } return;
		}
		ProbeAttempts = 0;
		if (!CheckState(StartingCoins, false, 0)) { return; }
		BeginCapture(EStep::BoardImage, TEXT("01_Board.png")); break;
	case EStep::BoardImage:
		if (Probe(Board.Get())) { SetStep(EStep::FirstPanel); } break;
	case EStep::FirstPanel:
	{
		const auto Entries = C.Contract->GetEntries(C.PC);
		if (!Check(Entries.Num() == 3, TEXT("contract page does not expose exactly three objectives"))) { return; }
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			if (!Check(Entries[Index].Id == Targets[Index]->GetPersistentId() && Entries[Index].bAvailable && !Entries[Index].bDiscovered
				&& !Entries[Index].Name.IsEmpty(), TEXT("initial contract checklist does not match real unvisited markers"))) { return; }
		}
		const TSharedPtr<SWidget> Button = FindButton(Claim);
		if (!Check(C.PC->IsSurveyContractOpen() && C.PC->GetSurveyContractBoard() == Board.Get() && GetWorld()->IsPaused()
			&& HasFocus(Resume) && Button.IsValid() && !Button->IsEnabled() && !C.Contract->TryClaim(C.PC, Board.Get(), Reason),
			TEXT("real sign did not open paused unfinished page or allowed early claim")) || !CheckState(StartingCoins, false, 0)) { return; }
		BeginCapture(EStep::IncompleteImage, TEXT("02_NotSurveyed.png")); break;
	}
	case EStep::IncompleteImage:
		if (!Check(HasFocus(Resume), TEXT("unfinished page lost initial Resume focus"))) { return; }
		if (SlateKey(EKeys::Gamepad_FaceButton_Bottom)) { SetStep(EStep::ResumeFirst); } break;
	case EStep::ResumeFirst:
		if (!Check(!C.PC->IsMenuOpen() && !GetWorld()->IsPaused(), TEXT("real Resume did not close contract page"))) { return; }
		MarkerIndex = 0; if (PositionAt(Targets[MarkerIndex].Get())) { SetStep(EStep::Marker); } break;
	case EStep::Marker:
		if (!Probe(Targets[MarkerIndex].Get())) { return; }
		if (!Check(Targets[MarkerIndex]->IsDiscovered(), TEXT("ordinary marker interaction did not survey the required place"))
			|| !CheckState(StartingCoins, false, MarkerIndex + 1)) { return; }
		UE_LOG(LogUEGT2Diag, Log, TEXT("Contract smoke surveyed via real probe: %s"), *Targets[MarkerIndex]->GetPersistentId().ToString());
		++MarkerIndex;
		if (MarkerIndex < Targets.Num()) { if (PositionAt(Targets[MarkerIndex].Get())) { SetStep(EStep::Marker); } }
		else if (PositionAt(Board.Get())) { SetStep(EStep::ReturnBoard); }
		break;
	case EStep::ReturnBoard:
		if (Probe(Board.Get())) { SetStep(EStep::EligiblePanel); } break;
	case EStep::EligiblePanel:
	{
		const TSharedPtr<SWidget> Button = FindButton(Claim);
		if (!Check(C.PC->IsSurveyContractOpen() && GetWorld()->IsPaused() && HasFocus(Resume)
			&& Button.IsValid() && Button->IsEnabled() && C.Contract->CanClaim(C.PC, Board.Get(), Reason),
			TEXT("completed real surveys did not enable payment on return to the sign")) || !CheckState(StartingCoins, false, 3)) { return; }
		BeginCapture(EStep::EligibleImage, TEXT("03_ClaimReady.png")); break;
	}
	case EStep::EligibleImage:
		if (!Check(HasFocus(Resume), TEXT("eligible page lost initial Resume focus"))) { return; }
		SlateKey(EKeys::Gamepad_DPad_Right); SetStep(EStep::ClaimFocus); break;
	case EStep::ClaimFocus:
		if (!Check(HasFocus(Claim), TEXT("natural D-pad Right did not focus Claim Payment"))) { return; }
		if (SlateKey(EKeys::Gamepad_FaceButton_Bottom)) { SetStep(EStep::Claimed); } break;
	case EStep::Claimed:
	{
		const TSharedPtr<SWidget> Button = FindButton(Paid);
		if (!CheckState(PaidCoins, true, 3) || !Check(C.PC->IsSurveyContractOpen() && GetWorld()->IsPaused() && HasFocus(Resume)
			&& Button.IsValid() && !Button->IsEnabled() && !C.Contract->TryClaim(C.PC, Board.Get(), Reason),
			TEXT("successful claim did not become disabled Paid/refocus Resume, or repeated claim succeeded")) || !CheckState(PaidCoins, true, 3)) { return; }
		BeginCapture(EStep::PaidImage, TEXT("04_Paid.png")); break;
	}
	case EStep::PaidImage:
	{
		C.Settings->SetTownSurveyContractEnabled(false); C.Settings->ApplyNonResolutionSettings();
		if (!Check(!C.Contract->IsEnabled() && C.Contract->IsPaid() && !C.Contract->TryClaim(C.PC, Board.Get(), Reason), TEXT("player off forgot or repaid the contract"))) { return; }
		C.Settings->SetTownSurveyContractEnabled(true); C.Contract->bFeatureEnabled = false; C.Settings->ApplyNonResolutionSettings();
		if (!Check(!C.Contract->IsAvailable() && C.Contract->IsPaid() && !C.Contract->TryClaim(C.PC, Board.Get(), Reason), TEXT("hard off forgot or repaid the contract"))
			|| !CheckState(PaidCoins, true, 3)) { return; }
		// Save from the actual pause root with the contract unavailable. Capture
		// must still retain its paid state alongside the credited purse.
		C.PC->ShowPauseMenu();
		if (!Check(C.PC->SaveProgress(), TEXT("pause Save Progress failed with contract hard-off"))) { return; }
		FSlotBytes Written;
		if (!Check(Written.Read(Slot) && Written.HasSave() && C.PC->HasSavedProgress(), TEXT("contract Write has no readable isolated checkpoint"))) { return; }
		C.PC->ShowSettingsPage(3); SetStep(EStep::SettingsReady); break;
	}
	case EStep::SettingsReady:
	case EStep::SettingsImage:
	{
		TSharedPtr<SWidget> Root = FSlateApplication::Get().GetKeyboardFocusedWidget();
		for (int32 Depth = 0; Root.IsValid() && Root->GetType() != TEXT("SUEGT2Menu") && Depth < 32; ++Depth) { Root = Root->GetParentWidget(); }
		if (!Check(Root.IsValid() && Root->GetType() == TEXT("SUEGT2Menu"), TEXT("contract setting focus has no menu ancestor"))) { return; }
		int32 Budget = 1024;
		const TSharedPtr<SWidget> Label = FindContractText(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "TownSurveyContractSetting", "Town Survey Contract"), Budget);
		Budget = 1024;
		const TSharedPtr<SWidget> Hint = FindContractText(Root.ToSharedRef(), NSLOCTEXT("UEGT2Menu", "TownSurveyContractUnavailable",
			"The Town Survey Contract is disabled for this session. Your preference, surveys and any payment earned are kept."), Budget);
		if (!Check(Label.IsValid() && Hint.IsValid(), TEXT("hard-off contract setting or preserved-state hint is missing"))) { return; }
		TSharedPtr<SWidget> Scroll = Label; bool bDisabledAncestor = false;
		for (int32 Depth = 0; Scroll.IsValid() && Scroll->GetType() != TEXT("SScrollBox") && Depth < 32; ++Depth)
		{
			bDisabledAncestor |= !Scroll->IsEnabled(); Scroll = Scroll->GetParentWidget();
		}
		if (!Check(Scroll.IsValid() && Scroll->GetType() == TEXT("SScrollBox") && bDisabledAncestor
			&& !C.PC->IsSurveyContractAvailable() && C.Settings->GetTownSurveyContractEnabled(), TEXT("contract hard-off row is enabled or lost the retained preference"))) { return; }
		if (Step == EStep::SettingsReady)
		{
			StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(Label, false, EDescendantScrollDestination::Center);
			BeginCapture(EStep::SettingsImage, TEXT("05_ContractSetting.png"));
		}
		else
		{
			const FSlateRect Bounds = Scroll->GetCachedGeometry().GetLayoutBoundingRect();
			if (!Check(Bounds.ContainsRect(Label->GetCachedGeometry().GetLayoutBoundingRect())
				&& Bounds.ContainsRect(Hint->GetCachedGeometry().GetLayoutBoundingRect()), TEXT("contract setting or preservation hint remained clipped after scrolling"))
				|| !CheckState(PaidCoins, true, 3)) { return; }
			Finish(true, TEXT("real board/markers, natural claim, exact reward, repeat/off gates, paid checkpoint and visible disabled setting verified"));
		}
		break;
	}
	case EStep::ReadBoard:
		if (Probe(Board.Get())) { SetStep(EStep::ReadPanel); } break;
	case EStep::ReadPanel:
	{
		const TSharedPtr<SWidget> Button = FindButton(Paid);
		if (!Check(C.PC->IsSurveyContractOpen() && GetWorld()->IsPaused() && HasFocus(Resume) && Button.IsValid() && !Button->IsEnabled()
			&& !C.Contract->TryClaim(C.PC, Board.Get(), Reason), TEXT("fresh-process paid contract offered or granted repeat payment")) || !CheckState(PaidCoins, true, 3)) { return; }
		if (!SlateKey(EKeys::Gamepad_FaceButton_Bottom) || !Check(!C.PC->IsMenuOpen(), TEXT("paid-page Resume did not close")) || !CheckState(PaidCoins, true, 3)) { return; }
		Finish(true, TEXT("fresh-process Continue restored paid state and exact purse; repeat claim refused")); break;
	}
	case EStep::DisabledBoard:
		if (!Probe(Board.Get())) { return; }
		if (!Check(!C.Contract->IsEnabled() && C.Contract->IsAvailable() && !C.PC->IsSurveyContractOpen() && !C.PC->IsMenuOpen()
			&& !C.Contract->TryClaim(C.PC, Board.Get(), Reason), TEXT("player-off real sign opened or claimed the contract")) || !CheckState(PaidCoins, true, 3)) { return; }
		C.Settings->SetTownSurveyContractEnabled(true); C.Contract->bFeatureEnabled = false; C.Settings->ApplyNonResolutionSettings(); SetStep(EStep::HardDisabledBoard); break;
	case EStep::HardDisabledBoard:
	{
		if (!Probe(Board.Get())) { return; }
		if (!Check(!C.Contract->IsAvailable() && C.Settings->GetTownSurveyContractEnabled() && !C.PC->IsSurveyContractOpen()
			&& !C.Contract->TryClaim(C.PC, Board.Get(), Reason), TEXT("hard-off real sign opened/claimed or changed saved preference")) || !CheckState(PaidCoins, true, 3)) { return; }
		C.Contract->bFeatureEnabled = true;
		FSlotBytes After;
		if (!Check(C.Contract->IsPaid() && After.Read(Slot) && BeforeDisabled.Equals(After), TEXT("disabled load or gate transitions changed paid checkpoint bytes"))) { return; }
		Finish(true, TEXT("paid checkpoint loaded while player-off; real sign/off gates preserved paid state, purse and checkpoint bytes")); break;
	}
	default: break;
	}
}
void UUEGT2ContractSmokeSubsystem::BeginCapture(EStep Next, const TCHAR* Name)
{
	SetStep(Next); CaptureFile = FPaths::Combine(CaptureDirectory, Name); bScreenshotRequested = false; bScreenshotComplete = CaptureDirectory.IsEmpty();
}
void UUEGT2ContractSmokeSubsystem::HandleScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	if (PendingFile.IsEmpty() || bFinished) { return; }
	if (!Check(Width == ExpectedWidth && Height == ExpectedHeight && Bitmap.Num() == Width * Height, TEXT("contract screenshot resolution incorrect"))) { return; }
	TArray<FColor> Opaque = Bitmap; for (FColor& Pixel : Opaque) { Pixel.A = 255; }
	TArray64<uint8> Png; FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Opaque.GetData(), Opaque.Num()), Png);
	if (!Check(Png.Num() > 0 && FFileHelper::SaveArrayToFile(Png, *PendingFile), TEXT("cannot save contract screenshot"))) { return; }
	UE_LOG(LogUEGT2Diag, Log, TEXT("Contract screenshot: %s"), *PendingFile); PendingFile.Reset(); bScreenshotComplete = true;
}
